// OPT-IN, not part of ./run_tests.sh: the chat dock of Home in a REAL browser (Chromium through Playwright) against the REAL hub (`python3 -m hub`, with its test login), behind a small stand-in for the site's nginx that
// passes /hub/ws and tells the hub who is at the other end (the hub's test login believes `X-Hub-Test-Login: <id>:<name>`, which the stand-in takes from a cookie). It looks at what the fake hub of
// tests/scripts/fake_hub.js cannot say, because the real hub does it: the lines that the page sent and the hub answers, or does not answer, in a way that the page does not expect (docs/HOME_SCREEN.md, "The chat dock"):
//   1. a line that the hub composes (NFC) before it says it back, and the next refusal (two tabs of one account: the second tab uses up the account's three lines, the first is refused with `rate`): the refused line
//      is the one that comes back into the field, not the composed one;
//   2. a line from which the hub takes out a character that its Unicode does not know (U+1FAE9): the same;
//   3. a command of a moderator ("/mute victim 1": the hub takes it and says nothing): the same, and the command is never put back into the field.
//
//   node tests/scripts/web_home_chat_real_hub_browser.js [--web DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox; --web: the folder that is served, web/ by default)
//
// Needs `playwright` (npm), a Chromium that it can start and python3 with the hub's own folder (hub/); without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be made), 1 a
// check failed or the page broke.
'use strict';
const fs = require('fs');
const os = require('os');
const net = require('net');
const http = require('http');
const path = require('path');
const { spawn, spawnSync } = require('child_process');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the chat dock was NOT run against the real hub'); process.exit(0); }
const python = spawnSync('python3', ['-c', 'import hub'], { cwd: path.resolve(__dirname, '..', '..') });
if (python.status !== 0) { console.log('SKIP: python3 cannot import the hub: the chat dock was NOT run against the real hub'); process.exit(0); }

const ROOT = path.resolve(__dirname, '..', '..');
const webAt = process.argv.indexOf('--web');
const WEB = path.resolve(webAt !== -1 ? process.argv[webAt + 1] : path.join(ROOT, 'web'));
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml' };
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const freePort = () => new Promise((resolve, reject) => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); }); s.on('error', reject); });
async function until(label, fn, ms) {
    const end = Date.now() + (ms || 10000);
    for (;;) {
        let v = null;
        try { v = await fn(); } catch (e) { v = null; }
        if (v) return v;
        if (Date.now() > end) throw new Error('timeout: ' + label);
        await sleep(50);
    }
}

const ACCOUNTS = { maple: '100_maple.leaf', mod: '200_mod.one', victim: '300_victim' };         // (the cookie: the account's id, an underscore, its name)

async function main() {
    const [hubPort, sitePort] = [await freePort(), await freePort()];
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ants_hub_browser.'));
    const origin = 'http://127.0.0.1:' + sitePort;
    const hub = spawn('python3', ['-m', 'hub'], { cwd: ROOT, env: Object.assign({}, process.env, { ANTS_HUB_PORT: String(hubPort), ANTS_HUB_HOST: '127.0.0.1', ANTS_HUB_DB: path.join(dir, 'hub.db'), ANTS_HUB_PUBLIC_URL: origin, ANTS_HUB_MODS: '200', ANTS_HUB_TEST_LOGIN: '1' }), stdio: ['ignore', 'pipe', 'pipe'] });
    let hubLog = '';
    hub.stdout.on('data', (d) => { hubLog += d; });
    hub.stderr.on('data', (d) => { hubLog += d; });
    await until('the hub answers', () => new Promise((resolve) => http.get({ host: '127.0.0.1', port: hubPort, path: '/hub/healthz' }, (res) => { res.resume(); resolve(res.statusCode === 200); }).on('error', () => resolve(false))), 15000).catch((e) => { console.log(hubLog); throw e; });

    const site = http.createServer((req, res) => {          // the files of web/, as nginx gives them (/ is lobby.html)
        const url = new URL(req.url, 'http://x');
        const file = url.pathname === '/' ? 'lobby.html' : url.pathname.replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'application/json' }); res.end('{"error":"not here"}'); return; }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        res.end(fs.readFileSync(full));
    });
    site.on('upgrade', (req, socket, head) => {             // /hub/ws to the hub, with the account that the cookie names
        if (new URL(req.url, 'http://x').pathname !== '/hub/ws') { socket.destroy(); return; }
        const who = /(?:^|;\s*)acct=([0-9]+_[a-z0-9_.]+)/.exec(req.headers.cookie || '');
        const headers = Object.assign({}, req.headers);
        delete headers.cookie;
        if (who) { const at = who[1].indexOf('_'); headers['x-hub-test-login'] = who[1].slice(0, at) + ':' + who[1].slice(at + 1); }
        const up = net.connect(hubPort, '127.0.0.1', () => {
            up.write('GET /hub/ws HTTP/1.1\r\n' + Object.entries(headers).map(([k, v]) => k + ': ' + v).join('\r\n') + '\r\n\r\n');
            if (head && head.length) up.write(head);
            up.pipe(socket); socket.pipe(up);
        });
        up.on('error', () => socket.destroy()); socket.on('error', () => up.destroy());
    });
    await new Promise((resolve) => site.listen(sitePort, '127.0.0.1', resolve));

    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the chat dock was NOT run against the real hub'); hub.kill('SIGTERM'); site.close(); return; }
    const errors = [];
    async function tab(context) {                           // one tab of the account: Home, the dock open and writing
        const page = await context.newPage();
        page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
        await page.goto(origin + '/', { waitUntil: 'load' });
        await page.waitForSelector('#gchat .gc-input', { state: 'visible', timeout: 15000 });
        await until('the dock is live and may write', () => page.evaluate(() => { const d = document.getElementById('gchat'); return d.getAttribute('data-state') === 'live' && !d.querySelector('.gc-form').hidden; }), 15000);
        return page;
    }
    async function context(account) {
        const c = await browser.newContext({ viewport: { width: 1280, height: 800 } });
        await c.addCookies([{ name: 'acct', value: ACCOUNTS[account], url: origin }]);
        return c;
    }
    const say = async (page, text) => { await page.fill('#gchat .gc-input', text); await page.press('#gchat .gc-input', 'Enter'); };
    const field = (page) => page.inputValue('#gchat .gc-input');
    const logText = (page) => page.evaluate(() => document.querySelector('#gchat .gc-log').textContent);
    const heard = (page, text) => until('"' + text + '" is in the log', async () => (await logText(page)).indexOf(text) !== -1, 8000);
    // the account has three lines at once and one more every two seconds across all its tabs (docs/HUB.md): the second tab says two, so that the next line of the first is refused
    const crowd = async (other, a, b) => { await say(other, a); await say(other, b); };

    try {
        // ---- 1 and 2. an account that is no moderator, two tabs --------------------------------------------------------------------------------------------------------------------------------------
        const maple = await context('maple');
        const one = await tab(maple), two = await tab(maple);
        const cases = [
            ['a line that the hub composes before it says it back ("cafe" and a combining accent: the hub says "café")', 'café au lait', 'café au lait', 'beta two'],
            ['a line from which the hub takes out a character that its Unicode has not assigned (U+1FAE9: the hub says "hi")', 'hi \u{1FAE9}', 'hi', 'beta three']
        ];
        for (const [what, sent, echoed, next] of cases) {
            await sleep(6500);                                 // (the account's three lines are back)
            await say(one, sent);
            await heard(one, echoed);
            await crowd(two, 'tab two says ' + next.slice(-3) + ' a', 'tab two says ' + next.slice(-3) + ' b');
            await heard(one, 'tab two says ' + next.slice(-3) + ' b');
            await say(one, next);
            await until('the hub has refused the line (the note says so)', async () => (await one.textContent('#gchat .gc-note')).indexOf('Slow down') !== -1, 8000);
            check(what + ': the refusal of "' + next + '" gives back "' + next + '" (not the line before it)', (await field(one)) === next, JSON.stringify(await field(one)));
            await one.fill('#gchat .gc-input', '');
        }
        await maple.close();

        // ---- 3. a moderator's command, which the hub takes and does not answer ----------------------------------------------------------------------------------------------------------------------
        const victim = await context('victim');
        const away = await tab(victim);
        const mod = await context('mod');
        const m1 = await tab(mod), m2 = await tab(mod), m3 = await tab(mod);       // (a command costs the account no line: three lines of the other tabs use up what it has)
        await say(m1, '/mute victim 1');
        await sleep(1500);
        check('a command of a moderator ("/mute victim 1") is taken by the hub (the field is empty and the command is not a line of the chat)', (await field(m1)) === '' && (await logText(m1)).indexOf('/mute') === -1, JSON.stringify(await field(m1)));
        await crowd(m2, 'mod tab two says a', 'mod tab two says b');
        await say(m3, 'mod tab three says c');
        await heard(m1, 'mod tab three says c');
        await say(m1, 'beta four');
        await until('the hub has refused the line', async () => (await m1.textContent('#gchat .gc-note')).indexOf('Slow down') !== -1, 8000);
        check('the refusal of "beta four" gives back "beta four", and never the command (Enter would issue it again)', (await field(m1)) === 'beta four', JSON.stringify(await field(m1)));
        await mod.close(); await victim.close();
        check('no page error', errors.length === 0, errors.join(' | '));
        void away;
    } finally {
        await browser.close();
        hub.kill('SIGTERM');
        site.close();
        try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) { /* it stays */ }
    }
}

main().then(() => { console.log('home chat against the real hub in a browser: ' + checks + ' checks, ' + failures + ' failed'); process.exit(failures === 0 ? 0 : 1); }, (e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
