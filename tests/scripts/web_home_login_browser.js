// OPT-IN, not part of ./run_tests.sh: the whole Discord login of the chat hub in a REAL browser (Chromium through Playwright): the front page (web/lobby.html with web/home/), the REAL docker/nginx.conf in front,
// the real hub process, a FAKE Discord (tests/scripts/fake_discord.py) on another host name (so that the browser sees two sites, as it does with the real Discord), and, when a built ants_server is given, a real
// game server for the room whose seat must survive the round trip. tests/scripts/login_site.py starts all of it. What no fake DOM can show:
//   * the places and sizes of the Log in button, the chip and the account line at 390 x 844 (a phone), 844 x 390 (held sideways) and 1280 x 800 (a desktop) next to the approved pictures, every target 44 px;
//   * click Log in, the consent page of the fake Discord, the way back, and Home logged in (the cookie that Discord's navigation could not carry back is not needed for the page to load: the socket has it);
//   * the cookie as the browser keeps it (__Host-, Secure, HttpOnly, SameSite=Strict, Path=/, no Domain), that the page's script cannot read it, that no code, state or token is in the address, the storage or the page;
//   * a line written as the visitor and read by another browser, a reload, Log out, Forget me (the lines go from other screens at once), a banned account (reads, cannot write), a young account (cannot write),
//     a Discord that does not answer (the calm page and a link back) while the chat goes on for everybody else;
//   * with a game server: a visitor who is in a room logs in and is back in the same seat (the tab's session storage survives the round trip), also through /hub/login?next=room.
//
//   node tests/scripts/web_home_login_browser.js [--shots DIR] [--server PATH]      (ANTS_SERVER=/path/to/ants_server names a game server of the page's protocol; CHROME=/path/to/chromium a browser)
//
// Needs `playwright` (npm), a Chromium that it can start, nginx and python3 on the path; without them it says so and exits 0 (nothing was checked). --shots DIR saves a picture of each state as
// login8_<scene>_<phone|side|desktop>.png. Exit status: 0 every check passed (or the check could not be made), 1 a check failed or the page broke.
'use strict';
const fs = require('fs');
const path = require('path');
const readline = require('readline');
const { spawn, spawnSync } = require('child_process');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the login was NOT run in a browser'); process.exit(0); }
if (spawnSync('nginx', ['-v']).error) { console.log('SKIP: no nginx program: the login was NOT run in a browser'); process.exit(0); }

const arg = (name) => (process.argv.indexOf(name) !== -1 ? process.argv[process.argv.indexOf(name) + 1] : null);
const shotsAt = arg('--shots');
const serverBinary = arg('--server') || process.env.ANTS_SERVER || '';
const SIZES = { phone: [390, 844], side: [844, 390], desktop: [1280, 800] };
const OLD = '261842000123456789';
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const snowflake = (ageDays) => ((BigInt(Date.now() - 1420070400000 - Math.round(ageDays * 86400000)) << 22n) | 5n).toString();

class Site {                                              // login_site.py: the whole site, and a line of commands for the fake Discord and the hub's command line
    constructor(args) {
        this.proc = spawn('python3', [path.join(__dirname, 'login_site.py')].concat(args), { stdio: ['pipe', 'pipe', 'inherit'] });
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

async function main() {
    const site = new Site((serverBinary ? ['--server', serverBinary] : []).concat(['--consent']));
    let info;
    try { info = await site.up(); } catch (e) { console.log('SKIP: ' + e.message + ': the login was NOT run in a browser'); process.exit(0); }
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the login was NOT run in a browser'); site.stop(); process.exit(0); }
    const errors = [];
    const shot = async (page, scene, size) => { if (shotsAt) { fs.mkdirSync(shotsAt, { recursive: true }); await page.screenshot({ path: path.join(shotsAt, 'login8_' + scene + '_' + size + '.png') }); } };

    async function open(size, o) {
        o = o || {};
        const context = o.context || await browser.newContext({ viewport: { width: SIZES[size][0], height: SIZES[size][1] }, deviceScaleFactor: 1 });
        const page = await context.newPage();
        page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
        await page.goto(info.site + (o.path || '/'), { waitUntil: 'load' });
        await page.waitForSelector('#gchat', { timeout: 8000 });
        if (o.live !== false) await page.waitForSelector('#gchat[data-state="live"]', { timeout: 8000 });
        return { page, context };
    }
    const you = (page) => page.evaluate(() => document.getElementById('gchat').getAttribute('data-you'));
    const waitYou = (page, want) => page.waitForFunction((w) => document.getElementById('gchat') && document.getElementById('gchat').getAttribute('data-you') === w && document.getElementById('gchat').getAttribute('data-state') === 'live', want, { timeout: 10000 });
    const rect = (page, selector) => page.evaluate((s) => { const e = document.querySelector(s); if (!e || e.hidden || !e.getClientRects().length) return null; const r = e.getBoundingClientRect(); return { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height) }; }, selector);
    const noSideways = (page) => page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth && document.body.scrollWidth <= window.innerWidth);
    const text = (page, selector) => page.evaluate((s) => { const e = document.querySelector(s); return e ? e.innerText : null; }, selector);
    const plain = (page, selector) => page.evaluate((s) => { const e = document.querySelector(s); return e ? e.textContent.replace(/\s+/g, ' ').trim() : null; }, selector);
    async function logIn(page, how) {                    // the button of the bar or of the dock, the consent page of the fake Discord (a first visit; a returning one is sent back at once), and the way back
        await page.click(how === 'dock' ? '#gchat .gc-login' : '.hlogin');
        const end = Date.now() + 20000;
        while (Date.now() < end) {
            if (page.url().indexOf(info.discord) === 0 && await page.$('#go').catch(() => null)) await page.click('#go').catch(() => {});
            const done = page.url().indexOf(info.site) === 0 && await page.evaluate(() => { const d = document.getElementById('gchat'); return !!d && d.getAttribute('data-you') === 'yes' && d.getAttribute('data-state') === 'live'; }).catch(() => false);
            if (done) return;
            await sleep(100);
        }
        throw new Error('the login did not come back to the page: ' + page.url());
    }
    const sessionCookie = async (context) => (await context.cookies()).find((c) => c.name === '__Host-ants_hub');

    // ---- logged out, at the three sizes: the chip, the field that is off and the Log in button, as drawn (lobbyhome_x_chat_out_*) ----
    for (const size of Object.keys(SIZES)) {
        const { page, context } = await open(size);
        if (size !== 'desktop') { await page.click('#gchat .gc-head'); await sleep(150); }                // (a phone's dock is a strip: it opens upward)
        const chip = await rect(page, '.hlogin'), button = await rect(page, '#gchat .gc-login'), field = await rect(page, '#gchat .gc-input');
        const label = size + ': logged out: ';
        check(label + 'the chip is in the bar, the dock has the field (off) and the Log in button in Send\'s place, and no Send', !!chip && !!button && !!field && !(await rect(page, '#gchat .gc-send')));
        check(label + 'every target is at least 44 px (the chip 46 high, the button and field 48)', !!chip && chip.h >= 44 && chip.w >= 44 && (size === 'desktop' ? chip.w > 80 : chip.w <= 50), JSON.stringify(chip));
        if (size === 'desktop') check(label + 'the chip says Log in with the mark of Discord and the dock says it too, and the field says why it is off', (await text(page, '.hlogin')).trim() === 'Log in' && (await page.evaluate(() => document.querySelector('#gchat .gc-input').disabled && document.querySelector('#gchat .gc-input').placeholder)) === 'Log in with Discord to chat');
        check(label + 'nothing scrolls sideways', await noSideways(page));
        check(label + 'the dock is read only for the visitor: the guest line is there, the account line is not', (await rect(page, '#gchat .gc-acct')) === null);
        check(label + 'the field and the button are on the screen with the panel out, each 44 px or more (48 as drawn)', !!button && button.h >= 44 && !!field && field.h >= 44, JSON.stringify([button, field]));
        await shot(page, 'x_chat_out', size);
        await context.close();
    }

    // ---- the round trip, on a desktop: click Log in, Discord's page, the way back ----
    const desk = await open('desktop');
    const page = desk.page;
    const requestsBefore = (await site.cmd('requests')).requests.length;
    await page.click('.hlogin');
    await page.waitForURL(/localhost:\d+\/oauth2\/authorize/, { timeout: 8000 });
    const atDiscord = new URL(page.url());
    check('the chip is a navigation of the whole tab to Discord (the fake one), with the application, the one scope, the way back and a state, and no secret',
        atDiscord.searchParams.get('client_id') === info.client_id && atDiscord.searchParams.get('scope') === 'identify' && atDiscord.searchParams.get('response_type') === 'code' && atDiscord.searchParams.get('redirect_uri') === info.site + '/hub/callback'
        && /^[0-9a-f]{32}$/.test(atDiscord.searchParams.get('state') || '') && atDiscord.searchParams.get('prompt') === 'none' && !/secret/i.test(atDiscord.search));
    check('Discord\'s own page is where the tab is (the first visit: the consent page), and the site\'s cookie of the state is set for the way back', !!(await page.$('#go')) && (await desk.context.cookies()).some((c) => c.name === '__Host-ants_oauth' && c.httpOnly && c.secure && c.sameSite === 'Lax'));
    await page.click('#go');
    await page.waitForURL(info.site + '/**', { timeout: 10000 });
    await waitYou(page, 'yes');
    check('the way back ends on the front page at Home, with no code, no state and no token in the address (the callback was a redirect, not a page)', page.url() === info.site + '/' && !/code=|state=|access_token/.test(page.url()));
    check('the dock knows who it is: Chatting as maple.leaf, and the chip is the circle and the name', (await plain(page, '#gchat .gc-acct')).indexOf('Chatting as maple.leaf') === 0 && (await plain(page, '.hlogin .gc-av')) === 'm' && (await plain(page, '.hlogin .gc-cn')) === 'maple.leaf');
    const cookie = await sessionCookie(desk.context);
    check('the cookie is __Host-ants_hub: Secure, HttpOnly, SameSite=Strict, Path=/, with no Domain (the host only), 30 days, 32 hex digits', !!cookie && cookie.secure === true && cookie.httpOnly === true && cookie.sameSite === 'Strict' && cookie.path === '/' && cookie.domain === '127.0.0.1'
        && /^[0-9a-f]{32}$/.test(cookie.value) && Math.abs((cookie.expires - Date.now() / 1000) - 30 * 86400) < 120, JSON.stringify(cookie));
    check('the state cookie is gone, and the page\'s script can read neither', !(await desk.context.cookies()).some((c) => c.name === '__Host-ants_oauth') && (await page.evaluate(() => document.cookie)) === '');
    const keep = await page.evaluate(() => JSON.stringify([Object.keys(localStorage), Object.keys(sessionStorage), document.documentElement.outerHTML.indexOf('access_token') !== -1]));
    check('no token, code or state is in the storage of the page or in the page', !/code|state|token|oauth|discord/i.test(keep.replace(/"ants\.[a-z.]+"/g, '')) && !JSON.parse(keep)[2], keep);
    const fakeRequests = (await site.cmd('requests')).requests.slice(requestsBefore);
    check('Discord was asked twice, by the hub and not by the page: the exchange and the question (the page made no request to it)', fakeRequests.filter((r) => r[0] === 'POST').length === 1 && fakeRequests.filter((r) => r[1].endsWith('/users/@me')).length === 1);
    await shot(page, 'x_chat_in', 'desktop');
    await shot(page, 'x_home_in', 'desktop');

    // ---- a line, read by another browser; a reload; the small menu; Log out ----
    const other = await open('desktop');
    await page.fill('#gchat .gc-input', 'hello from the browser');
    await page.click('#gchat .gc-send');
    await other.page.waitForFunction(() => /hello from the browser/.test(document.querySelector('#gchat .gc-log').innerText), null, { timeout: 8000 });
    check('a line is written as the visitor and read by another browser (the other one is a reader: no field of its own)', (await text(other.page, '#gchat .gc-log')).indexOf('maple.leaf: hello from the browser') !== -1 && (await you(other.page)) === 'no');
    await page.reload({ waitUntil: 'load' });
    await waitYou(page, 'yes');
    check('a reload keeps the login (the cookie, a new socket, the same name, the history that includes the line)', (await text(page, '#gchat .gc-log')).indexOf('maple.leaf: hello from the browser') !== -1);
    await page.click('#gchat .gc-more');
    await shot(page, 'x_menu', 'desktop');
    check('the small menu holds Forget me and says what it does; Escape puts it away and the panel stays', (await text(page, '#gchat .gc-menu')).indexOf('Deletes your lines in the chat and logs you out everywhere. A ban or a mute stays.') !== -1 && (await page.keyboard.press('Escape'), !(await rect(page, '#gchat .gc-menu'))) && (await rect(page, '#gchat .gc-acct')) !== null);
    for (const sel of ['#gchat .gc-link', '#gchat .gc-more']) { const r = await rect(page, sel); check('the target ' + sel + ' is at least 44 px', !!r && r.h >= 44 && r.w >= 44, JSON.stringify(r)); }
    await page.click('#gchat .gc-link');
    await waitYou(page, 'no');
    check('Log out: the visitor is a reader again (the chip says Log in, the field is off), the cookie is gone from the browser, the old lines are still there', (await text(page, '.hlogin')).trim() === 'Log in' && !(await sessionCookie(desk.context)) && (await page.evaluate(() => document.querySelector('#gchat .gc-input').disabled))
        && (await text(page, '#gchat .gc-log')).indexOf('maple.leaf: hello from the browser') !== -1);
    check('... and a reload is still a reader', (await page.reload({ waitUntil: 'load' }), await page.waitForSelector('#gchat[data-state="live"]'), (await you(page)) === 'no'));
    await shot(page, 'x_loggedout_again', 'desktop');

    // ---- a returning visitor (no consent page), Forget me: the lines go from every screen ----
    await site.cmd('consent off');
    await logIn(page, 'dock');
    check('a returning visitor (prompt=none) is back at once, with no page of Discord in between (the fake sent the redirect itself)', page.url() === info.site + '/' && (await you(page)) === 'yes');
    await page.fill('#gchat .gc-input', 'a second line to forget');
    await page.click('#gchat .gc-send');
    await other.page.waitForFunction(() => /a second line to forget/.test(document.querySelector('#gchat .gc-log').innerText), null, { timeout: 8000 });
    await page.click('#gchat .gc-more');
    await page.click('#gchat .gc-forget');
    await waitYou(page, 'no');
    await other.page.waitForFunction(() => !/a second line to forget/.test(document.querySelector('#gchat .gc-log').innerText), null, { timeout: 8000 });
    check('Forget me: the visitor is a reader, the cookie is gone, and the lines that the account wrote are gone from the other browser\'s screen too, and from a new one\'s',
        !(await sessionCookie(desk.context)) && (await text(other.page, '#gchat .gc-log')).indexOf('maple.leaf') === -1 && (await text(page, '#gchat .gc-log')).indexOf('maple.leaf') === -1);

    // ---- a banned account reads and cannot write; a young one reads and cannot write ----
    await site.cmd('ban ' + OLD);
    await logIn(page, 'chip');
    await page.fill('#gchat .gc-input', 'let me in');
    await page.click('#gchat .gc-send');
    await page.waitForFunction(() => /You cannot chat here/.test(document.querySelector('#gchat .gc-note').innerText), null, { timeout: 5000 });
    check('a banned account can log in and read, and is told that it cannot write; nobody else sees a line', (await text(other.page, '#gchat .gc-log')).indexOf('let me in') === -1 && (await you(page)) === 'yes');
    await page.click('#gchat .gc-link'); await waitYou(page, 'no');
    await site.cmd('unban ' + OLD);
    await site.cmd('user ' + snowflake(2) + ' newcomer');
    await logIn(page, 'chip');
    await page.fill('#gchat .gc-input', 'hello, I am new');
    await page.click('#gchat .gc-send');
    await page.waitForFunction(() => /too new/.test(document.querySelector('#gchat .gc-note').innerText), null, { timeout: 5000 });
    check('an account of two days is told that it is too new, and writes nothing', (await text(other.page, '#gchat .gc-log')).indexOf('hello, I am new') === -1);
    await page.click('#gchat .gc-link'); await waitYou(page, 'no');
    await site.cmd('user ' + OLD + ' maple.leaf');

    // ---- Discord that does not answer: a calm page and a link back; the chat goes on ----
    await site.cmd('token_mode hang');
    await page.click('.hlogin');
    await page.waitForURL(/localhost:\d+\/oauth2\/authorize|\/hub\/callback|127\.0\.0\.1:\d+\/?$/, { timeout: 8000 });
    await page.waitForFunction((s) => location.href.indexOf(s) === 0 && /Login unavailable/.test(document.body.innerText), info.site, { timeout: 20000 });
    check('the callback page says "Login unavailable, try again later" and has a link back to the front page; nothing is shown from Discord or the visitor', /Login unavailable, try again later/.test(await text(page, 'body')) && (await page.getAttribute('main a', 'href')) === '/' && !/code|state/.test(await text(page, 'body')));
    check('... the chat of everybody else went on meanwhile (the other browser still has its link)', (await other.page.evaluate(() => document.getElementById('gchat').getAttribute('data-state'))) === 'live');
    await shot(page, 'unavailable', 'desktop');
    await page.click('main a');
    await page.waitForSelector('#gchat[data-state="live"]', { timeout: 8000 });
    check('the link goes back to Home, which is a reader with the Log in button', page.url() === info.site + '/' && (await you(page)) === 'no');
    await site.cmd('token_mode ok');
    await logIn(page, 'chip');
    await page.click('#gchat .gc-link'); await waitYou(page, 'no');
    await desk.context.close();
    await other.context.close();

    // ---- the phone and a phone held sideways: the chip is the mark or the circle (46 x 46), the account line is in the panel ----
    for (const size of ['phone', 'side']) {
        const o = await open(size);
        const label = size + ': ';
        check(label + 'the chip is the mark of Discord alone, a target of 44 px or more', (await rect(o.page, '.hlogin')) && (await rect(o.page, '.hlogin')).w <= 50 && (await rect(o.page, '.hlogin')).h >= 44 && (await text(o.page, '.hlogin')).trim() === '');
        await logIn(o.page, 'chip');
        const chip = await rect(o.page, '.hlogin');
        check(label + 'logged in the chip is the circle alone (a target of 44 px or more), nothing scrolls sideways', !!chip && chip.w <= 50 && chip.h >= 44 && (await noSideways(o.page)), JSON.stringify(chip));
        await o.page.click('#gchat .gc-head').catch(() => {});
        await sleep(150);
        check(label + 'the account line is in the panel, with Log out and the dots, each 44 px', !!(await rect(o.page, '#gchat .gc-acct')) && (await rect(o.page, '#gchat .gc-link')).h >= 44 && (await rect(o.page, '#gchat .gc-more')).h >= 44);
        await shot(o.page, 'x_chat_in', size);
        await o.page.click('#gchat .gc-link'); await waitYou(o.page, 'no');
        await o.context.close();
    }

    // ---- a room and the round trip ----
    if (serverBinary && info.server) {
        const o = await open('desktop');
        await o.page.click('.hbar .mine');
        await o.page.waitForSelector('#code', { timeout: 10000 });
        await o.page.waitForFunction(() => /\S/.test(document.getElementById('code').textContent) && document.querySelectorAll('#slots li.k-person').length >= 1, null, { timeout: 10000 });
        const code = await o.page.evaluate(() => document.getElementById('code').textContent.replace(/\s+/g, ''));
        const stored = await o.page.evaluate(() => sessionStorage.getItem('ants.lobby'));
        check('a visitor makes a room (My room) and holds a seat in it (the tab\'s session storage holds the room and its key)', /^[a-z2-9]{6}$/.test(code) && !!stored && stored.indexOf(code) !== -1, code + ' ' + stored);
        await o.page.click('.rhome');
        await logIn(o.page, 'chip');
        await o.page.waitForFunction(() => { const p = document.querySelector('.hbar .mine .pill'); return p && !p.hidden && /^\d\/\d$/.test(p.textContent); }, null, { timeout: 15000 });
        const back = await o.page.evaluate(() => sessionStorage.getItem('ants.lobby'));
        check('after the round trip through Discord the page is on Home, the room chip says how many are in it (the seat was taken back in the background), and the session storage still holds the same room', (await text(o.page, '.hbar .mine')).indexOf('1/') !== -1 && !!back && back.indexOf(code) !== -1);
        await o.page.click('.hbar .mine');
        await o.page.waitForFunction(() => document.body.getAttribute('data-screen') === 'room' && /\S/.test(document.getElementById('code').textContent), null, { timeout: 8000 });
        const again = await o.page.evaluate(() => document.getElementById('code').textContent.replace(/\s+/g, ''));
        check('the Room shows the SAME room code, and the visitor is in a seat of it (a person in a seat of the room, no second room made)', again === code && (await o.page.evaluate(() => document.querySelectorAll('#slots li.k-person').length)) >= 1, again);
        await shot(o.page, 'room_after_login', 'desktop');
        await o.page.goto(info.site + '/hub/login?next=room', { waitUntil: 'load' });
        await o.page.waitForURL(info.site + '/#room', { timeout: 10000 });
        await o.page.waitForFunction(() => document.body.getAttribute('data-screen') === 'room' && /\S/.test(document.getElementById('code').textContent), null, { timeout: 10000 });
        const room = await o.page.evaluate(() => document.getElementById('code').textContent.replace(/\s+/g, ''));
        check('a login started with next=room comes back to /#room, on the Room screen, in the same room', room === code && (await o.page.evaluate(() => location.hash)) === '#room', room);
        await o.context.close();
    } else {
        console.log('NOTE: no game server was given (--server PATH or ANTS_SERVER): the seat of a room across the round trip was NOT run in a browser');
    }

    check('nothing in the pages threw (pageerror)', errors.length === 0, errors.join(' | '));
    await browser.close();
    site.stop();
    console.log('home login in a browser: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main().catch((e) => { console.log('FAIL the run broke: ' + (e && e.stack || e)); process.exit(1); });
