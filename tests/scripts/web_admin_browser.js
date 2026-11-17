// OPT-IN, not part of ./run_tests.sh: the admin page (web/admin.html, admin_page.js, admin.css; docs/MAP_SUBMISSIONS.md "The admin page") in a REAL browser (Chromium through Playwright), on a small static
// server of the repository's own files under the page's real content security policy (read from docker/nginx.conf), with made-up answers for the game server's /admin/api, the hub's /hub/admin/api, /hub/me,
// /hub/logout and /busy. It draws the scenes of the approved pictures (signed out, not allowed as a player and as a moderator, Maps waiting and approved, Chat, the Ban question, a refusal and a done line, the
// Log with its three filters, the Status well and in trouble, the chip's menu, the owner's Chat) at 390 wide (a phone), 1280 wide (a desktop) and 320 wide (checks only), and checks on each:
//   1. the page does not scroll sideways, and nothing is wider than the window;
//   2. every button, link, box and select that is shown is 44 px tall (and wide) or more, and inside the window;
//   3. no error of the script and no refusal of the content security policy;
//   4. the sections are a row of tabs from 601 px and a select under that, never both;
//   5. the names are text: a name that is markup is shown as it is, and no tag of it is in the page;
//   6. a real double click on the form's Ban of a short phone, where the Ban of the question that it opens comes to stand under the second click, bans nobody (the question ignores a press in its first half second).
//   --shots DIR takes a picture of every scene (NAME_phone.png, NAME_desktop.png; the phone's is the whole page, the window for the question and the menu); --only PREFIX draws the scenes that start with it.
//   Made-up names only (basil and tess are admins, sage a moderator, pebble a player, clover.4782 the spammer). ADMIN_SHOT_THUMBS=DIR names a folder with mapthumb_a.png to mapthumb_d.png for the pictures of the
//   maps (else small drawings are made).
//
//   node tests/scripts/web_admin_browser.js [--shots DIR] [--only PREFIX]      (CHROME=/path/to/chromium names a browser, PLAYWRIGHT_MODULE=/path/to/playwright-core a module; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be run), 1 a check failed.
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');

let playwright = null;
for (const where of ['playwright', 'playwright-core', process.env.PLAYWRIGHT_MODULE || '']) {
    if (!where) continue;
    try { playwright = require(where); break; } catch (e) { /* not installed there */ }
}
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the admin page was NOT run in a browser'); process.exit(0); }

const arg = (name) => (process.argv.indexOf(name) !== -1 ? process.argv[process.argv.indexOf(name) + 1] : null);
const ROOT = path.resolve(__dirname, '..', '..');
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const ONLY = arg('--only') || '';
const NOW_MS = Date.UTC(2026, 10, 16, 21, 20, 0);            // Mon 16 Nov 2026, 13:20 Pacific
const at = (iso) => Math.floor(Date.parse(iso) / 1000);
const SIZES = { phone: { width: 390, height: 844, dsf: 2, mobile: true }, desktop: { width: 1280, height: 800, dsf: 2, mobile: false }, narrow: { width: 320, height: 640, dsf: 1, mobile: true } };
let checks = 0;
let failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }

// ---- the made-up world -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const STAMPS = { '@@GAME_VERSION@@': 'v0.16.11', '@@BUILD_ID@@': '9d41c2e', '@@BUILD_DATE@@': '2026-11-16 09:30 PT' };
const conf = fs.readFileSync(path.join(ROOT, 'docker', 'nginx.conf'), 'utf8');
const CSP = /add_header Content-Security-Policy "([^"]+)" always;/.exec(conf.slice(conf.indexOf('location = /admin {')))[1];          // (the first policy after the page's own block)
function page() {
    let text = fs.readFileSync(path.join(ROOT, 'web', 'admin.html'), 'utf8');
    for (const k of Object.keys(STAMPS)) text = text.split(k).join(STAMPS[k]);
    return text;
}
function mapRow(id, extra) { return Object.assign({ id, state: 'waiting', kind: 'classic', name: 'x', maker: 'x', note: '', width: 31, height: 31, sent_at: at('2026-11-16T16:41:00Z'), name_taken: false, has_picture: true, findings: [] }, extra); }
const MAPS = {
    waiting: [mapRow('a'.repeat(32), { name: 'Dry Canyon', maker: 'Lee', note: 'Made by hand.', thumb: 'a' }), mapRow('b'.repeat(32), { name: 'Gold Rush', maker: 'Sam', note: 'Four lanes and a gold pile in the middle.', thumb: 'b', sent_at: at('2026-11-16T15:12:00Z') }),
        mapRow('c'.repeat(32), { name: 'Big Meadow', maker: 'Mia', kind: 'extended', width: 200, height: 160, thumb: 'c', sent_at: at('2026-11-15T22:30:00Z') })],
    approved: [mapRow('d'.repeat(32), { state: 'approved', name: 'Mossy Ridge', approved_name: 'Mossy Ridge', maker: 'Fern', decided_at: at('2026-11-16T19:30:00Z'), decided_by: 'basil', thumb: 'd' }),
        mapRow('e'.repeat(32), { state: 'approved', name: 'Cave Crossing', approved_name: 'Cave Crossing', maker: 'Lee', decided_at: at('2026-11-14T20:05:00Z'), decided_by: 'tess', thumb: 'a' }),
        mapRow('f'.repeat(32), { state: 'approved', name: 'Old Bridge', approved_name: 'Old Bridge', maker: 'Sam', kind: 'extended', width: 128, height: 128, decided_at: at('2026-11-12T18:00:00Z'), decided_by: 'tess', thumb: 'b' })]
};
const REF = { basil: '1b2a3c4d5e6f7a8b', tess: '2c3d4e5f6a7b8c9d', sage: '3d4e5f6a7b8c9d0e', clover: '4e5f6a7b8c9d0e1f', spam: '5f6a7b8c9d0e1f2a', hazel: '6a7b8c9d0e1f2a3b', juniper: '7b8c9d0e1f2a3b4c', pebble: '8c9d0e1f2a3b4c5d', marlow: '9d0e1f2a3b4c5d6e', fern: 'ae1f2a3b4c5d6e7f' };
function world() {
    return {
        counts: { waiting: 3, approved: 12, declined: 2, taken_down: 1 },
        bans: [{ ref: REF.clover, name: 'clover.4782', at: at('2026-11-16T17:12:00Z'), by: REF.basil, by_name: 'basil' }, { ref: REF.spam, name: 'spam.king', at: at('2026-11-14T20:00:00Z'), by: REF.tess, by_name: 'tess' },
            { ref: '7c2e91d0a4b83f56', name: '', at: at('2026-11-09T20:00:00Z'), by: 'command line', by_name: '' }],
        mutes: [{ ref: REF.hazel, name: 'hazel_b', until: at('2026-11-16T22:51:00Z'), by: REF.sage, by_name: 'sage' }, { ref: REF.juniper, name: 'juniper.berry', until: at('2026-11-17T05:20:00Z'), by: REF.tess, by_name: 'tess' },
            { ref: REF.pebble, name: 'pebble_9', until: at('2026-11-23T19:40:00Z'), by: REF.basil, by_name: 'basil' }],
        hidden: [{ ref: REF.clover, name: 'clover.4782' }, { ref: REF.marlow, name: 'marlow.old' }],
        modlog: [
            { at: at('2026-11-16T21:02:00Z'), action: 'mute', actor: REF.basil, actor_name: 'basil', target: REF.hazel, target_name: 'hazel_b', detail: '60' },
            { at: at('2026-11-16T19:47:00Z'), action: 'del', actor: REF.sage, actor_name: 'sage', target: REF.clover, target_name: 'clover.4782', detail: '148' },
            { at: at('2026-11-16T17:12:00Z'), action: 'ban', actor: REF.basil, actor_name: 'basil', target: REF.clover, target_name: 'clover.4782', detail: '' },
            { at: at('2026-11-16T17:11:00Z'), action: 'kick', actor: REF.sage, actor_name: 'sage', target: REF.clover, target_name: 'clover.4782', detail: '' },
            { at: at('2026-11-15T20:00:00Z'), action: 'nopic', actor: REF.tess, actor_name: 'tess', target: REF.marlow, target_name: 'marlow.old', detail: '' },
            { at: at('2026-11-15T18:00:00Z'), action: 'unmute', actor: REF.tess, actor_name: 'tess', target: REF.pebble, target_name: 'pebble_9', detail: '' },
            { at: at('2026-11-14T20:00:00Z'), action: 'ban', actor: REF.tess, actor_name: 'tess', target: REF.spam, target_name: 'spam.king', detail: '' },
            { at: at('2026-11-13T20:00:00Z'), action: 'del', actor: 'terminal', actor_name: '', target: '', target_name: '', detail: '97' },
            { at: at('2026-11-12T20:00:00Z'), action: 'mute', actor: '3f9a52c07de18b44', actor_name: '', target: REF.fern, target_name: 'fern.leaf', detail: '10' }],
        audit: [{ at: at('2026-11-16T19:30:00Z'), who: 'basil', ref: REF.basil, action: 'map_approved', target: 'Mossy Ridge', detail: '' }, { at: at('2026-11-16T18:02:00Z'), who: 'tess', ref: REF.tess, action: 'map_declined', target: 'Test 1', detail: 'Same as an existing map.' },
            { at: at('2026-11-15T20:00:00Z'), who: 'basil', ref: REF.basil, action: 'map_taken_down', target: 'Old Bridge', detail: '' }, { at: at('2026-11-12T20:00:00Z'), who: 'tess', ref: REF.tess, action: 'map_put_back', target: 'Old Bridge', detail: '' }],
        busy: { matches: 2, players: 5 }, posts: [],
        status: { store: { bytes: 4404019, limit: 67108864, log: 314572, free: 0, lines: 187, oldest: at('2026-11-09T20:00:00Z'), pictures: 41, pictures_max: 1000, picture_bytes: 1153434, picture_bytes_max: 25165824, off: 6, blocked: 1 },
            health: { roles: 'ok', roles_known: 14, kind: null, status: null, retry: null }, last_picture: { kind: 'ok', age: 180 }, version: { code: '9f3c1a2e', started: at('2026-11-16T17:30:00Z') } }
    };
}
// who is here: signed out, a player with no role, a moderator, an admin, the owner
const WHO = {
    out: { me: [401, { error: 'not_signed_in' }], you: [200, { login: true, you: null }] },
    player: { me: [403, { error: 'not_allowed' }], you: [200, { login: true, you: { name: 'pebble', mod: false, role: 'none', k: 'x' } }] },
    mod: { me: [403, { error: 'not_allowed' }], you: [200, { login: true, you: { name: 'sage', mod: true, role: 'mod', k: 'x' } }] },
    admin: { me: [200, { name: 'basil', ref: REF.basil, role: 'admin', csrf: 'c'.repeat(32), untrusted: true }], you: [200, { login: true, you: { name: 'basil', mod: true, role: 'admin', k: 'x' } }] },
    owner: { me: [200, { name: 'basil', ref: REF.basil, role: 'owner', csrf: 'c'.repeat(32), untrusted: true }], you: [200, { login: true, you: { name: 'basil', mod: true, role: 'admin', k: 'x' } }] }
};
let W = world();
let who = WHO.out;
let hubRefuses = false;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.svg': 'image/svg+xml' };
function thumb(res, key) {
    const file = process.env.ADMIN_SHOT_THUMBS ? path.join(process.env.ADMIN_SHOT_THUMBS, 'mapthumb_' + key + '.png') : '';
    if (file && fs.existsSync(file)) { res.writeHead(200, { 'Content-Type': 'image/png' }); res.end(fs.readFileSync(file)); return; }
    const tone = { a: '#7a5b2b', b: '#2f6f5a', c: '#5a3f7a', d: '#7a2f2f' }[key] || '#555';
    res.writeHead(200, { 'Content-Type': 'image/svg+xml' });
    res.end('<svg xmlns="http://www.w3.org/2000/svg" width="62" height="62"><rect width="62" height="62" fill="' + tone + '"/><rect x="8" y="8" width="20" height="14" fill="#c9b27c"/><rect x="34" y="30" width="20" height="22" fill="#222"/></svg>');
}
const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://x');
    const p = url.pathname;
    const json = (status, o) => { res.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(o)); };
    let m;
    if (p === '/admin') { res.writeHead(200, { 'Content-Type': TYPES['.html'], 'Cache-Control': 'no-store', 'Content-Security-Policy': CSP }); res.end(page()); return; }
    if (p === '/admin/api/me') return json(who.me[0], who.me[1]);
    if (p === '/hub/me') return json(who.you[0], who.you[1]);
    if (p === '/live') return json(200, { live: [] });
    if (p === '/busy') return json(200, W.busy);
    if (p === '/admin/api/audit') return json(200, { entries: W.audit, untrusted: true });
    if (p === '/admin/api/maps') { const st = url.searchParams.get('state') || 'waiting'; return json(200, { state: st, counts: W.counts, maps: MAPS[st] || [] }); }
    if ((m = /^\/admin\/api\/maps\/([a-f]{32})\/picture$/.exec(p))) { const row = [].concat(MAPS.waiting, MAPS.approved).filter((x) => x.id === m[1])[0]; return thumb(res, row ? row.thumb : 'a'); }
    if (p.indexOf('/hub/admin/api/') === 0) {
        if (hubRefuses) return json(403, { error: 'admin' });
        const name = p.slice('/hub/admin/api/'.length);
        if (req.method === 'GET') {
            if (name === 'me') return json(200, { name: 'basil', role: 'admin', csrf: 'd'.repeat(64), untrusted: true });
            if (name === 'bans') return json(200, { bans: W.bans, untrusted: true });
            if (name === 'mutes') return json(200, { mutes: W.mutes, untrusted: true });
            if (name === 'hidden') return json(200, { hidden: W.hidden, untrusted: true });
            if (name === 'modlog') return json(200, { modlog: W.modlog, untrusted: true });
            if (name === 'status') return json(200, W.status);
        } else {
            let body = '';
            req.on('data', (c) => { body += c; });
            req.on('end', () => {
                const names = ['clover.4782', 'hazel_b', 'juniper.berry', 'pebble_9', 'marlow.old', 'spam.king'];
                W.posts.push(name);
                const account = JSON.parse(body || '{}').account;
                if (account && account.length < 20 && names.indexOf(account) < 0 && !/^[0-9a-f]{16}$/.test(account)) return json(404, { error: 'who' });
                return json(200, name === 'mute' ? { ok: true, until: at('2026-11-16T22:20:00Z') } : { ok: true });
            });
            return undefined;
        }
    }
    if (p === '/hub/logout') return json(200, { ok: true });
    const full = path.resolve(ROOT, 'web', decodeURIComponent(p).replace(/^\/+/, ''));
    if (full.indexOf(path.join(ROOT, 'web') + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'text/plain' }); res.end('not here'); return undefined; }
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    res.end(fs.readFileSync(full));
    return undefined;
});

// ---- the scenes: a name, who is here, what to do after the page has opened, and whether the picture is the window alone -----------------------------------------------------------------------------------------------
async function openSection(pg, id) {                               // by the value of the select (a phone) or the id of the tab (a desktop)
    if (await pg.locator('.atabs').isVisible()) await pg.click('#tab-' + id);
    else await pg.selectOption('#asec', id);
}
const SCENES = [
    ['1_signed_out', { who: 'out' }, async (pg) => { await pg.waitForSelector('.lcard a'); }],
    ['1b_not_allowed_player', { who: 'player' }, async (pg) => { await pg.waitForSelector('.lcard .l2'); }],
    ['1c_not_allowed_moderator', { who: 'mod' }, async (pg) => { await pg.waitForSelector('.lcard .l2'); }],
    ['2_maps_waiting', { who: 'admin' }, async (pg) => { await pg.waitForSelector('.alist .arow'); }],
    ['2b_maps_approved_by_username', { who: 'admin' }, async (pg) => { await pg.waitForSelector('.alist .arow'); await pg.click('.chip:has-text("Approved")'); await pg.waitForFunction(() => /Mossy/.test(document.querySelector('.alist .arow .t1').textContent)); }],
    ['3_chat', { who: 'admin' }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); }],
    ['3b_chat_ban_confirm', { who: 'admin', window: true }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); await pg.fill('#cm-user', 'clover.4782'); await pg.click('.acts .btn.bad'); await pg.waitForSelector('.dlg'); }],
    ['3c_chat_refused', { who: 'admin' }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); await pg.fill('#cm-user', 'clove'); await pg.click('.acts .btn:has-text("Mute")'); await pg.waitForSelector('.panel .msg'); }],
    ['3d_chat_done', { who: 'admin' }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); await pg.fill('#cm-user', 'hazel_b'); await pg.click('.acts .btn:has-text("Mute")'); await pg.waitForSelector('.panel .msg.ok'); }],
    ['4_log_all', { who: 'admin' }, async (pg) => { await openSection(pg, 'log'); await pg.waitForSelector('.arow2'); }],
    ['4b_log_chat_actions', { who: 'admin' }, async (pg) => { await openSection(pg, 'log'); await pg.waitForSelector('.arow2'); await pg.click('.lhead .chip:has-text("Chat actions")'); }],
    ['4c_log_map_decisions', { who: 'admin' }, async (pg) => { await openSection(pg, 'log'); await pg.waitForSelector('.arow2'); await pg.click('.lhead .chip:has-text("Map decisions")'); }],
    ['5_status_ok', { who: 'admin' }, async (pg) => { await openSection(pg, 'status'); await pg.waitForSelector('.kv'); }],
    ['5b_status_trouble', { who: 'owner', trouble: true }, async (pg) => { await openSection(pg, 'status'); await pg.waitForSelector('.kv'); }],
    ['6_chip_menu_open', { who: 'admin', window: true }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); await pg.click('.acct'); await pg.waitForSelector('.amenu'); }],
    ['9_owner_chat', { who: 'owner' }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); }]
];

async function checks_of(pg, label, size) {
    const out = await pg.evaluate(({ w }) => {
        const found = [];
        const de = document.documentElement;
        if (de.scrollWidth > w + 1) found.push('sideways scroll: ' + de.scrollWidth + ' > ' + w);
        const bad = [];
        document.querySelectorAll('button, a.btn, a[href], input, select').forEach((n) => {
            const r = n.getBoundingClientRect();
            if (r.width === 0 || r.height === 0) return;
            const cs = getComputedStyle(n);
            if (cs.visibility === 'hidden' || cs.display === 'none') return;
            if (n.closest('[hidden], template, .sbm')) return;
            if (r.height < 43.5 || r.width < 43.5) bad.push((n.textContent || n.id || n.tagName).trim().slice(0, 24) + ' ' + Math.round(r.width) + 'x' + Math.round(r.height));
            if (r.right > w + 1 || r.left < -1) bad.push('outside: ' + (n.textContent || n.id).trim().slice(0, 24));
        });
        if (bad.length) found.push('controls: ' + bad.join('; '));
        const row = document.querySelector('.atabs'), sel = document.querySelector('.asel');
        if (row && sel) {
            const rowShown = getComputedStyle(row).display !== 'none', selShown = getComputedStyle(sel).display !== 'none';
            if (rowShown === selShown) found.push('the tabs and the select are shown ' + (rowShown ? 'both' : 'neither'));
            if (rowShown !== (w > 600)) found.push('the row of tabs is ' + (rowShown ? 'shown' : 'hidden') + ' at ' + w);
        }
        return found;
    }, { w: size.width });
    check(label, out.length === 0, out.join(' | '));
    return out.length;
}

(async () => {
    if (SHOTS) fs.mkdirSync(SHOTS, { recursive: true });
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const base = 'http://127.0.0.1:' + server.address().port;
    const launch = { executablePath: process.env.CHROME || undefined };
    if (process.getuid && process.getuid() === 0) launch.args = ['--no-sandbox'];
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the admin page was NOT run in a browser'); server.close(); process.exit(0); }
    for (const [name, spec, steps] of SCENES) {
        if (ONLY && name.indexOf(ONLY) !== 0) continue;
        for (const sizeName of SHOTS ? ['phone', 'desktop'] : ['narrow', 'phone', 'desktop']) {
            const size = SIZES[sizeName];
            W = world();
            who = WHO[spec.who];
            hubRefuses = false;
            if (spec.trouble) { W.status.health = { roles: 'refused', roles_known: 0, kind: 'token', status: 401, retry: 600 }; W.status.last_picture = { kind: 'down', age: 120 }; }
            const ctx = await browser.newContext({ viewport: { width: size.width, height: size.height }, deviceScaleFactor: size.dsf, isMobile: size.mobile, hasTouch: size.mobile, colorScheme: 'dark' });
            await ctx.addInitScript((now) => { Date.now = () => now; }, NOW_MS);
            const pg = await ctx.newPage();
            const errors = [];
            pg.on('pageerror', (e) => errors.push(String(e)));
            pg.on('console', (m) => { if (m.type() === 'error' && !/Failed to load resource/.test(m.text())) errors.push(m.text()); });
            await pg.goto(base + '/admin', { waitUntil: 'load' });
            try { await steps(pg); } catch (e) { check(name + '_' + sizeName + ': the scene could not be drawn', false, String(e.message).split('\n')[0]); await ctx.close(); continue; }
            await pg.evaluate(() => document.fonts.ready);
            await pg.waitForTimeout(250);
            await checks_of(pg, name + '_' + sizeName, size);
            check(name + '_' + sizeName + ': no error of the script and no refusal of the policy', errors.length === 0, errors.join(' | '));
            if (SHOTS) await pg.screenshot({ path: path.join(SHOTS, name + '_' + sizeName + '.png'), fullPage: !spec.window });
            await ctx.close();
        }
    }
    {   // a real double click on the form's Ban: the second click lands on the question's own Ban, and the question must not take it for an answer
        W = world();
        who = WHO.admin;
        const ctx = await browser.newContext({ viewport: { width: 390, height: 600 }, deviceScaleFactor: 1, isMobile: true, hasTouch: true, colorScheme: 'dark' });
        const pg = await ctx.newPage();
        const errors = [];
        pg.on('pageerror', (e) => errors.push(String(e)));
        await pg.goto(base + '/admin', { waitUntil: 'load' });
        await pg.waitForSelector('.alist .arow');
        await openSection(pg, 'chat');
        await pg.waitForSelector('.p-banned .prow');
        await pg.fill('#cm-user', 'clover.4782');
        await pg.click('.acts .btn.bad');                                                      // (once, to see where the question's Ban stands)
        await pg.waitForSelector('.dlg');
        const sheet = await pg.locator('.dlg-foot .btn.bad').boundingBox();
        await pg.keyboard.press('Escape');
        await pg.waitForSelector('.dlg', { state: 'detached' });
        const form = await pg.locator('.acts .btn.bad').boundingBox();
        await pg.evaluate((dy) => window.scrollBy(0, dy), form.y + form.height / 2 - (sheet.y + sheet.height / 2));      // (the form's Ban at the height of the question's)
        const at = await pg.locator('.acts .btn.bad').boundingBox();
        const x = Math.round(at.x + at.width / 2), y = Math.round(at.y + at.height / 2);
        await pg.mouse.dblclick(x, y);
        await pg.waitForTimeout(100);
        const over = await pg.evaluate(([px, py]) => { const e = document.elementFromPoint(px, py); return e && e.closest('.dlg-foot') ? e.textContent : null; }, [x, y]);
        check('the second click of a double click lands on the Ban of the question (else this check shows nothing)', over === 'Ban', String(over));
        check('and it banned nobody: the question is open and nothing was sent', (await pg.locator('.dlg').count()) === 1 && W.posts.length === 0, JSON.stringify(W.posts));
        if ((await pg.locator('.dlg').count()) === 1) {
            await pg.waitForTimeout(700);
            await pg.locator('.dlg-foot .btn.bad').click();
            await pg.waitForTimeout(200);
            check('a press on it a moment later is the answer: the ban is sent, once', W.posts.length === 1 && W.posts[0] === 'ban', JSON.stringify(W.posts));
        }
        check('no error of the script', errors.length === 0, errors.join(' | '));
        await ctx.close();
    }
    {   // a name that is markup is text on every section: no tag of it is in the page
        const evil = '<img src=x onerror=alert(1)>';
        W = world();
        W.bans[0].name = evil;
        W.modlog[0].actor_name = evil;
        W.audit[0].target = evil;
        who = WHO.admin;
        const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
        const pg = await ctx.newPage();
        const errors = [];
        pg.on('pageerror', (e) => errors.push(String(e)));
        await pg.goto(base + '/admin', { waitUntil: 'load' });
        await pg.waitForSelector('.alist .arow');
        for (const id of ['chat', 'log', 'status']) { await openSection(pg, id); await pg.waitForTimeout(300); }
        await openSection(pg, 'chat');
        await pg.waitForSelector('.p-banned .prow');
        check('a name that is markup is shown as text in the Chat list', (await pg.locator('.p-banned .pn').first().textContent()) === evil);
        check('and no node of it is in the page', (await pg.locator('#app img').count()) === (await pg.locator('.arow img').count()));
        await openSection(pg, 'log');
        await pg.waitForSelector('.arow2');
        check('a name that is markup is shown as text in the Log', (await pg.locator('.arow2 .wh').first().textContent()) === evil);
        check('no script ran', errors.length === 0, errors.join(' | '));
        await ctx.close();
    }
    await browser.close();
    server.close();
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
