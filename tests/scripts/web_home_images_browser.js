// OPT-IN, not part of ./run_tests.sh: the images of Home's chat in a REAL browser (Chromium through Playwright) against the REAL hub (`python3 -m hub`, with its test login) and the REAL converter (imgconv/, with
// the real Pillow), behind a small stand-in for the site's nginx: it serves web/, passes /hub/ws and POST /hub/img/up to the hub (the hub's test login believes `X-Hub-Test-Login: <id>:<name>`, which the stand-in
// takes from a cookie) and serves /hub/img/<name> from the hub's image folder with the headers of docker/nginx.conf. It looks at what the fakes of tests/scripts/web_home_images_check.js and
// web_home_images_dock_check.js cannot say, because the real parts do it (docs/HUB_IMAGES.md, "The page"):
//   1. the operator's account adds a real PNG by the button's file chooser with a caption, sends it, and BOTH tabs (its own and a reader's) show the thumbnail, loaded from /hub/img/ at the size the hub reserved;
//      the file is the converter's (a PNG with a text chunk and a location comes back with neither), and the viewer opens it whole;
//   2. a real animated GIF stays a GIF, moves while it is in the log (its address changes from the still to the .gif) and rests when the reader switches animation off, and when the device asks for reduced motion;
//   3. a reader who may not post has no button, a paste of a file is told off in words, and a request to the upload door is refused (403 images);
//   4. the author deletes its own image (the thumbnail goes from both tabs and the file is a 404); an image that is not an image is refused on the card (nothing is sent) and by the door (415).
// With `--shots DIR` it writes pictures (the posted image, the viewer, the reader's switch in the account's menu, the phone) for a human to look at.
//
//   node tests/scripts/web_home_images_browser.js [--web DIR] [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm), a Chromium that it can start, python3 with the hub's own folder (hub/) and Pillow; without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none
// could be made), 1 a check failed or the page broke.
'use strict';
const fs = require('fs');
const os = require('os');
const net = require('net');
const http = require('http');
const path = require('path');
const { spawn, spawnSync } = require('child_process');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { try { playwright = require('playwright-core'); } catch (e2) { /* not installed */ } }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the chat images were NOT run in a browser'); process.exit(0); }
const ROOT = path.resolve(__dirname, '..', '..');
const python = spawnSync('python3', ['-c', 'import hub, PIL'], { cwd: ROOT });
if (python.status !== 0) { console.log('SKIP: python3 cannot import the hub and Pillow: the chat images were NOT run in a browser'); process.exit(0); }

const arg = (name, fallback) => { const at = process.argv.indexOf(name); return at !== -1 ? process.argv[at + 1] : fallback; };
const WEB = path.resolve(arg('--web', path.join(ROOT, 'web')));
const SHOTS = arg('--shots', '');
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json', '.svg': 'image/svg+xml', '.jpg': 'image/jpeg', '.gif': 'image/gif', '.woff2': 'font/woff2' };
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
const ACCOUNTS = { owner: '200_owner.one', reader: '100_maple.leaf' };                  // (the cookie: the account's id, an underscore, its name; the operator is the account 200)

function makeImages(dir) {                                                                // real files made by Pillow: a PNG with a text chunk (a comment, the sort of thing that must not come back), a GIF of 6 frames, a text file
    const code = `
import sys
from PIL import Image, ImageDraw, PngImagePlugin
d = sys.argv[1]
im = Image.new("RGB", (1200, 800), (30, 90, 70))
g = ImageDraw.Draw(im)
for i in range(0, 1200, 60):
    g.rectangle([i, 0, i + 30, 800], fill=(60, 150, 110))
g.ellipse([450, 250, 750, 550], fill=(240, 200, 80))
info = PngImagePlugin.PngInfo()
info.add_text("Comment", "SECRET-LOCATION-37.7749N-122.4194W")
im.save(d + "/photo.png", pnginfo=info)
frames = []
for k in range(6):
    f = Image.new("RGB", (240, 160), (20, 20, 20))
    ImageDraw.Draw(f).ellipse([10 + k * 30, 40, 70 + k * 30, 100], fill=(240, 120 + k * 20, 60))
    frames.append(f)
frames[0].save(d + "/ball.gif", save_all=True, append_images=frames[1:], duration=120, loop=0)
open(d + "/notes.txt", "w").write("not an image")
`;
    const done = spawnSync('python3', ['-c', code, dir], { cwd: ROOT });
    if (done.status !== 0) throw new Error('could not make the images: ' + done.stderr);
}

async function main() {
    const [hubPort, sitePort, imgPort] = [await freePort(), await freePort(), await freePort()];
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ants_images_browser.'));
    const folder = path.join(dir, 'images');
    fs.mkdirSync(folder);
    makeImages(dir);
    const origin = 'http://127.0.0.1:' + sitePort;
    const spawned = [];
    const run = (cmd, args, cwd, env) => { const p = spawn(cmd, args, { cwd, env: Object.assign({}, process.env, env), stdio: ['ignore', 'pipe', 'pipe'] }); p.log = ''; p.stdout.on('data', (d) => { p.log += d; }); p.stderr.on('data', (d) => { p.log += d; }); spawned.push(p); return p; };
    const converter = run('python3', ['server.py'], path.join(ROOT, 'imgconv'), { ANTS_IMG_PORT: String(imgPort), ANTS_IMG_HOST: '127.0.0.1', PYTHONDONTWRITEBYTECODE: '1' });
    const hub = run('python3', ['-m', 'hub'], ROOT, { ANTS_HUB_PORT: String(hubPort), ANTS_HUB_HOST: '127.0.0.1', ANTS_HUB_DB: path.join(dir, 'hub.db'), ANTS_HUB_PUBLIC_URL: origin, ANTS_HUB_OWNER: '200', ANTS_HUB_TEST_LOGIN: '1',
        ANTS_HUB_IMAGE_CONVERTER_URL: 'http://127.0.0.1:' + imgPort, ANTS_HUB_IMAGE_DIR: folder, PYTHONDONTWRITEBYTECODE: '1' });
    const stop = () => { spawned.forEach((p) => p.kill('SIGTERM')); };
    const ping = (port, p) => new Promise((resolve) => http.get({ host: '127.0.0.1', port, path: p }, (res) => { res.resume(); resolve(res.statusCode === 200); }).on('error', () => resolve(false)));
    try {
        await until('the hub answers', () => ping(hubPort, '/hub/healthz'), 20000);
        await until('the converter answers', () => ping(imgPort, '/health'), 20000);
    } catch (e) { console.log('SKIP: ' + e.message + ' (the hub says: ' + hub.log.slice(-300) + '; the converter says: ' + converter.log.slice(-300) + '): the chat images were NOT run in a browser'); stop(); return; }

    const login = (req) => { const who = /(?:^|;\s*)acct=([0-9]+_[a-z0-9_.]+)/.exec(req.headers.cookie || ''); if (!who) return ''; const at = who[1].indexOf('_'); return who[1].slice(0, at) + ':' + who[1].slice(at + 1); };
    const site = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://x');
        if (url.pathname === '/hub/img/up' && req.method === 'POST') {                                   // the upload: only the headers that the site's nginx passes
            const headers = { host: '127.0.0.1:' + hubPort, origin: req.headers.origin || '', 'content-type': req.headers['content-type'] || '', 'content-length': req.headers['content-length'] || '0', 'x-client-address': '127.0.0.1' };
            if (req.headers['sec-fetch-site']) headers['sec-fetch-site'] = req.headers['sec-fetch-site'];
            if (req.headers['x-ants-image']) headers['x-ants-image'] = req.headers['x-ants-image'];
            if (login(req)) headers['x-hub-test-login'] = login(req);
            const up = http.request({ host: '127.0.0.1', port: hubPort, path: '/hub/img/up', method: 'POST', headers }, (r) => { res.writeHead(r.statusCode, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); r.pipe(res); });
            up.on('error', () => { res.writeHead(503, { 'Content-Type': 'application/json' }); res.end('{"error":"down"}'); });
            req.pipe(up);
            return;
        }
        const img = /^\/hub\/img\/([0-9a-f]{32}(?:\.t)?\.(?:jpg|png|gif))$/.exec(url.pathname);
        if (img) {                                                                                       // a file from the volume, as docker/nginx.conf serves it
            const full = path.join(folder, img[1]);
            if (!fs.existsSync(full)) { res.writeHead(404, { 'Content-Type': 'application/json' }); res.end('{"error":"not here"}'); return; }
            res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)], 'X-Content-Type-Options': 'nosniff', 'Content-Security-Policy': "default-src 'none'; sandbox", 'Cross-Origin-Resource-Policy': 'same-origin', 'Cache-Control': 'private, max-age=3600' });
            res.end(fs.readFileSync(full));
            return;
        }
        const file = url.pathname === '/' ? 'lobby.html' : url.pathname.replace(/^\/+/, '');
        const full = path.resolve(WEB, file);
        if (full.indexOf(WEB + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'application/json' }); res.end('{"error":"not here"}'); return; }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        res.end(fs.readFileSync(full));
    });
    site.on('upgrade', (req, socket, head) => {
        if (new URL(req.url, 'http://x').pathname !== '/hub/ws') { socket.destroy(); return; }
        const headers = Object.assign({}, req.headers);
        delete headers.cookie;
        if (login(req)) headers['x-hub-test-login'] = login(req);
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
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the chat images were NOT run in a browser'); stop(); site.close(); return; }
    const errors = [];
    async function context(account, o) {
        const c = await browser.newContext(Object.assign({ viewport: { width: 1280, height: 800 } }, o || {}));
        await c.addCookies([{ name: 'acct', value: ACCOUNTS[account], url: origin }]);
        return c;
    }
    async function tab(ctx, wantPost) {
        const page = await ctx.newPage();
        page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
        await page.goto(origin + '/', { waitUntil: 'load' });
        await page.waitForSelector('#gchat', { state: 'attached' });
        if (page.viewportSize().width <= 700) await page.click('.htabs button:has-text("Chat")');                       // (a phone shows one screen at a time: its tab bar has the Chat)
        await page.waitForSelector('#gchat .gc-input', { state: 'visible', timeout: 15000 });
        await until('the dock is live and may write', () => page.evaluate(() => { const d = document.getElementById('gchat'); return d.getAttribute('data-state') === 'live' && !d.querySelector('.gc-form').hidden; }), 15000);
        if (wantPost) await until('the image button is there', () => page.$('#gchat .gt-pic'), 15000);
        return page;
    }
    const shot = async (page, name, clip) => { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, name + '.png'), clip }); } };
    const thumbs = (page) => page.$$eval('#gchat .gc-thumb img', (list) => list.map((i) => ({ src: i.getAttribute('src'), w: i.naturalWidth, h: i.naturalHeight, alt: i.getAttribute('alt'), fw: i.getAttribute('width'), fh: i.getAttribute('height') })));
    const get = (p) => new Promise((resolve) => http.get({ host: '127.0.0.1', port: sitePort, path: p }, (res) => { const chunks = []; res.on('data', (c) => chunks.push(c)); res.on('end', () => resolve({ status: res.statusCode, body: Buffer.concat(chunks), headers: res.headers })); }).on('error', () => resolve({ status: 0, body: Buffer.alloc(0), headers: {} })));
    const post = (account, body, headers) => new Promise((resolve) => {
        const r = http.request({ host: '127.0.0.1', port: sitePort, path: '/hub/img/up', method: 'POST', headers: Object.assign({ Cookie: 'acct=' + ACCOUNTS[account], Origin: origin, 'Sec-Fetch-Site': 'same-origin', 'X-Ants-Image': '1', 'Content-Type': 'application/octet-stream', 'Content-Length': String(body.length) }, headers || {}) },
            (res) => { const chunks = []; res.on('data', (c) => chunks.push(c)); res.on('end', () => resolve({ status: res.statusCode, text: Buffer.concat(chunks).toString() })); });
        r.on('error', () => resolve({ status: 0, text: '' })); r.end(body);
    });
    const sendFile = async (page, name, caption) => {
        const chooser = page.waitForEvent('filechooser');
        await page.click('#gchat .gt-pic');
        (await chooser).setFiles(path.join(dir, name));
        await page.waitForSelector('#gchat .gp-card', { timeout: 10000 });
        if (caption) await page.fill('#gchat .gc-input', caption);
    };

    try {
        // ---- 1. a PNG: the button's chooser, a caption, Send; both tabs show the thumbnail -------------------------------------------------------------------------------------------------------------------
        const owner = await context('owner'), reader = await context('reader');
        const op = await tab(owner, true), rp = await tab(reader, false);
        check('the operator has the image button, a reader who may not post has none', !!(await op.$('#gchat .gt-pic')) && !(await rp.$('#gchat .gt-pic')));
        await shot(op, '01_button_desktop', { x: 0, y: 380, width: 1280, height: 420 });
        await sendFile(op, 'photo.png', 'the best spot');
        const card = await op.textContent('#gchat .gp-card');
        check('the card says it is ready, its name and that the details are removed', /Image ready to send/.test(card) && /photo\.png/.test(card) && /Location and camera details are removed/.test(card), card);
        await shot(op, '02_card_desktop', { x: 0, y: 380, width: 1280, height: 420 });
        await rp.route('**/hub/img/*.t.jpg', (route) => setTimeout(() => route.continue(), 1500));                      // (the reader's file comes late: the box must be there before it)
        const box = () => rp.evaluate(() => { const i = document.querySelector('#gchat .gc-thumb img'); if (!i) return null; const r = i.getBoundingClientRect(); return { w: r.width, h: r.height, done: i.complete && i.naturalWidth > 0 }; });
        await op.click('#gchat .gc-send');
        const early = await until('the thumbnail\'s box is in the reader\'s log', box, 20000);
        await until('the thumbnail is in the owner\'s log', async () => (await thumbs(op)).length === 1 && (await thumbs(op))[0].w > 0, 20000);
        await until('the thumbnail is in the reader\'s log', async () => (await thumbs(rp)).length === 1 && (await thumbs(rp))[0].w > 0, 20000);
        const late = await box();
        check('the box of the thumbnail is there before its file and does not change when the file comes (the log does not jump)', !early.done && early.w > 1 && early.h > 1 && Math.abs(early.w - late.w) < 1 && Math.abs(early.h - late.h) < 1, JSON.stringify([early, late]));
        check('...and it is the thumbnail\'s own shape (1200 x 800) within 144 x 81', late.w <= 144.5 && late.h <= 81.5 && Math.abs(late.w / late.h - 1.5) < 0.03, JSON.stringify(late));
        await rp.unroute('**/hub/img/*.t.jpg');
        const [t1] = await thumbs(rp);
        check('the thumbnail is the hub\'s own file at the size that the hub reserved for it, with the caption for its alt', /^\/hub\/img\/[0-9a-f]{32}\.t\.jpg$/.test(t1.src) && String(t1.w) === t1.fw && String(t1.h) === t1.fh && t1.alt === 'the best spot', JSON.stringify(t1));
        check('the card is gone and the field is empty after the line came back', (await op.$('#gchat .gp-card')) === null && (await op.inputValue('#gchat .gc-input')) === '');
        const full = await get(t1.src.replace('.t.jpg', '.png'));
        check('the full file is a PNG that carries nothing of the comment that was in the upload', full.status === 200 && full.body.slice(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])) && full.body.indexOf('SECRET-LOCATION') === -1 && full.body.indexOf('tEXt') === -1 && full.body.indexOf('iTXt') === -1, full.status);
        check('the file is served with the site\'s headers', full.headers['x-content-type-options'] === 'nosniff' && /sandbox/.test(full.headers['content-security-policy'] || '') && full.headers['content-type'] === 'image/png');
        await shot(rp, '03_in_the_log_desktop', { x: 0, y: 380, width: 1280, height: 420 });
        await rp.click('#gchat .gc-thumb');
        await rp.waitForSelector('.gv-scrim .gv-img');
        await until('the viewer\'s picture loaded', () => rp.evaluate(() => { const i = document.querySelector('.gv-img'); return i.complete && i.naturalWidth === 1200; }), 10000);
        check('the viewer shows the whole image (1200 x 800), its link opens a new tab without a referrer, a reader has no delete', (await rp.getAttribute('.gv-scrim a', 'rel')) === 'noopener noreferrer' && (await rp.$$('.gv-scrim .gv-del')).length === 0);
        await shot(rp, '04_viewer_desktop');
        await rp.keyboard.press('Escape');
        check('Escape closes it and the focus is on the thumbnail again', (await rp.$('.gv-scrim')) === null && await rp.evaluate(() => document.activeElement && document.activeElement.className.indexOf('gc-thumb') >= 0));

        // ---- 2. an animated GIF -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        await sleep(3500);                                                                                  // (the account's upload budget)
        await sendFile(op, 'ball.gif', '');
        check('the card says that a GIF stays animated', /stays animated/.test(await op.textContent('#gchat .gp-card')));
        await op.click('#gchat .gc-send');
        await until('the GIF is in the reader\'s log, playing', async () => { const t = await thumbs(rp); return t.length === 2 && /\.gif$/.test(t[1].src) && t[1].w > 0; }, 20000);
        const gif = await thumbs(rp);
        const bytes = await get(gif[1].src);
        check('the file is a GIF with its frames (more than one image block in it)', bytes.status === 200 && bytes.body.slice(0, 3).toString() === 'GIF' && bytes.body.toString('latin1').split('\x00\x21\xf9\x04').length > 3, bytes.body.length);
        await shot(rp, '05_gif_playing_desktop', { x: 0, y: 380, width: 1280, height: 420 });
        await rp.click('.hlogin');
        const switchBox = await until('the account\'s menu has the reader\'s switch', () => rp.$('#gchat-login-slot .gp-sw input'), 5000).catch(() => null);
        check('the reader\'s switch is a row in the account\'s menu', !!switchBox);
        if (switchBox) {
            await shot(rp, '05b_the_readers_switch_desktop', await rp.evaluate(() => { const r = document.getElementById('gchat-login-slot').getBoundingClientRect(); return { x: Math.max(0, r.left - 20), y: Math.max(0, r.top - 20), width: Math.min(innerWidth, r.width + 40), height: r.height + 40 }; }));
            await rp.click('#gchat-login-slot .gp-sw label');
            await until('the GIF rests at its still when the switch is off', async () => /\.t\.jpg$/.test((await thumbs(rp))[1].src), 5000);
            check('...and the choice is kept in the browser', (await rp.evaluate(() => localStorage.getItem('ants.chat.animate'))) === '0');
            await rp.click('#gchat-login-slot .gp-sw label');
            await until('...and plays again when it is on', async () => /\.gif$/.test((await thumbs(rp))[1].src), 5000);
        }
        const quiet = await context('reader', { reducedMotion: 'reduce' });
        const qp = await tab(quiet, false);
        await until('the thumbnails are there', async () => (await thumbs(qp)).length === 2, 15000);
        check('a device that asks for reduced motion is shown the still of the GIF', /\.t\.jpg$/.test((await thumbs(qp))[1].src));
        await quiet.close();

        // ---- 3. the reader who may not post --------------------------------------------------------------------------------------------------------------------------------------------------------------------
        await rp.evaluate(() => { const f = new File([new Uint8Array([137, 80, 78, 71])], 'x.png', { type: 'image/png' }); const dt = new DataTransfer(); dt.items.add(f); document.querySelector('#gchat .gc-input').dispatchEvent(new ClipboardEvent('paste', { clipboardData: dt, bubbles: true, cancelable: true })); });
        await until('a paste of a file is told off in words (in the form\'s note, or in the dock\'s own where there is none)', async () => /Active Anters|could not check|turned off/i.test(await rp.evaluate(() => { const s = document.querySelector('#gchat .gt-say'), n = document.querySelector('#gchat .gc-note'); return (s && !s.hidden ? s.textContent : '') + ' ' + (n ? n.textContent : ''); })), 5000);
        const refused = await post('reader', fs.readFileSync(path.join(dir, 'photo.png')));
        check('the door refuses a person who may not post (403 images) and nothing is kept', refused.status === 403 && /images/.test(refused.text) && fs.readdirSync(folder).filter((n) => /\.(png|jpg|gif)$/.test(n)).length === 4, refused.status + ' ' + refused.text);

        // ---- 4. a delete by the author, and a file that is not an image -------------------------------------------------------------------------------------------------------------------------------------
        await sleep(3500);
        const before = fs.readdirSync(folder).length;
        await op.click('#gchat .gt-pic', { trial: true }).catch(() => {});
        const chooser = op.waitForEvent('filechooser');
        await op.click('#gchat .gt-pic');
        (await chooser).setFiles(path.join(dir, 'notes.txt'));
        await op.waitForSelector('#gchat .gp-card.gp-bad', { timeout: 5000 });
        check('a text file is refused on the card and nothing is sent', /not an image/.test(await op.textContent('#gchat .gp-card')) && fs.readdirSync(folder).length === before);
        const door = await post('owner', Buffer.from('not an image at all'));
        check('and by the door (415 type)', door.status === 415, door.status + ' ' + door.text);
        await op.click('#gchat .gp-card .gp-x');
        const mine = (await thumbs(op))[0];
        await op.click('#gchat .gc-line:has(.gc-thumb) .gc-thumb');
        await op.waitForSelector('.gv-scrim');
        await shot(op, '06_viewer_with_delete_desktop');
        await op.click('.gv-scrim .gv-del');
        await op.waitForSelector('.gv-scrim .gv-ask');
        await shot(op, '07_delete_question_desktop');
        await op.click('.gv-scrim .gv-foot .gv-del');
        await until('the image is gone from the author\'s log and from the reader\'s', async () => (await thumbs(op)).length === 1 && (await thumbs(rp)).length === 1, 15000);
        await until('its files are deleted: 404', async () => (await get(mine.src)).status === 404 && (await get(mine.src.replace('.t.jpg', '.png'))).status === 404, 10000);
        check('the other image is still there', (await thumbs(rp)).length === 1);

        // ---- the phone ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        const phone = await context('owner', { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true });
        const pp = await tab(phone, true);
        check('on a phone held upright the box has two rows', await pp.evaluate(() => document.querySelector('#gchat .gc-form').className.indexOf('gp-two') >= 0));
        const fit = await until('the thumbnail is in the phone\'s log', () => pp.evaluate(() => { const b = document.querySelector('#gchat .gc-thumb'), i = b && b.querySelector('img'); if (!i || !(i.complete && i.naturalWidth > 0)) return null; return { frame: b.clientWidth, img: i.getBoundingClientRect().width, h: i.getBoundingClientRect().height }; }), 20000).catch(() => null);
        check('on a phone the thumbnail\'s frame is as wide as its picture (no empty band beside it) and the picture keeps its 150 px height limit', !!fit && Math.abs(fit.frame - fit.img) < 1 && fit.h <= 150.5, JSON.stringify(fit));
        await shot(pp, '08_in_the_log_phone');
        await pp.click('.hlogin');
        const phoneSwitch = await until('the account\'s menu has the reader\'s switch on a phone too', () => pp.$('#gchat-login-slot .gp-sw input'), 5000).catch(() => null);
        check('the reader\'s switch is in the account\'s menu on a phone', !!phoneSwitch);
        if (phoneSwitch) await shot(pp, '09_the_readers_switch_phone', await pp.evaluate(() => { const r = document.getElementById('gchat-login-slot').getBoundingClientRect(); return { x: 0, y: Math.max(0, r.top - 20), width: innerWidth, height: r.height + 40 }; }));
        await phone.close();
        await owner.close(); await reader.close();
        check('no page error', errors.length === 0, errors.join(' | '));
    } finally {
        await browser.close();
        stop();
        site.close();
        try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) { /* it stays */ }
    }
}

main().then(() => { console.log('home chat images in a browser against the real hub: ' + checks + ' checks, ' + failures + ' failed'); process.exit(failures === 0 ? 0 : 1); }, (e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
