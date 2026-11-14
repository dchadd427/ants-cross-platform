// OPT-IN, not part of ./run_tests.sh: the bar at the top of every page (web/site_nav.css, web/site_nav.js; docs/SITE_NAV.md) in a REAL browser (Chromium through Playwright), on a small static
// server of the repository's own files (the pages with their clean addresses, /live and /history answered with made-up lists). For each page (Matches, Match, Legal, Changelog, Map maker,
// Sprites and sounds), each size (320, 390, 450, 721, 768, 1280 wide and a phone held sideways, 667 x 375) and with no match live and two live, it checks:
//   1. the page does not scroll sideways, and nothing in the bar is wider than the window;
//   2. the bar's visible buttons are 44 px tall or more and do not overlap each other;
//   3. a phone (under 720 px): Home and the Menu are shown, the row of pages is not; from 720 px: the row of four is shown (Home, Matches, Map maker, Discord) and the phone's Home is not;
//   4. the page that is open is marked aria-current in the row (Matches for Matches and Match, Map maker), and in the Menu for Legal, Changelog and Sprites and sounds;
//   5. the live mark, at every width: with two live it says "2 live", is named "2 matches live, see them on the front page" and links to "/" (on Matches and Match, which ask when they open); with none it is hidden;
//   6. nothing moves when the count arrives (the Home button, the row and the Menu button keep their places);
//   6b. the bar draws no logo (no picture in it), a link to the front page is shown, and the first button starts at the page's left edge (no gap where the logo was);
//   7. the Menu: opens with every link (the four and the five small ones), Escape closes it and the focus goes back to its button, a press outside closes it too.
//   --shots DIR takes a picture of every page at every size (PREFIX_page_size_liveN.png); --root DIR names the tree to serve (default: this repository); --prefix names the pictures; --no-checks takes
//   pictures only (for a tree from before the bar).
//
//   node tests/scripts/web_site_nav_browser.js [--shots DIR] [--root DIR] [--prefix after]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be run), 1 a check failed.
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const { spawnSync } = require('child_process');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the site bar was NOT run in a browser'); process.exit(0); }

const arg = (name) => (process.argv.indexOf(name) !== -1 ? process.argv[process.argv.indexOf(name) + 1] : null);
const ROOT = path.resolve(arg('--root') || path.join(__dirname, '..', '..'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const PREFIX = arg('--prefix') || 'after';
const CHECKS = process.argv.indexOf('--no-checks') === -1;
const SIZES = [['320', 320, 640], ['390', 390, 844], ['450', 450, 800], ['721', 721, 900], ['768', 768, 900], ['1280', 1280, 800], ['667x375', 667, 375]];
const PAGES = [   // name, address, what the row marks (null: in the Menu), the Menu's mark
    ['matches', '/matches', 'Matches', null], ['match', '/match?id=ants-TINY-20261015-051557Z', 'Matches', null],
    ['legal', '/legal', null, 'Legal'], ['changelog', '/changelog', null, 'Changelog'], ['mapmaker', '/mapmaker', 'Map maker', null], ['catalog', '/asset_catalog/', null, 'Sprites and sounds'],
];
let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}

const seat = (n, color, name, bot, o) => Object.assign({ seat: n, color, ['col' + 'our']: color, name, bot, score: 0, killed: 0, lost: 0, hatched: 0, bombs_planted: 0, bombs_defused: 0, fires_lit: 0, left: false, winner: false }, o || {});
const REC = {
    id: 'ants-TINY-20261015-051557Z', mode: 'highest-score', format: '2v2', draw: false, result: 'decided', quitter: null, seconds: 360, turns: 7200, game: 'v0.12.41', recording: 'watch', file: 'ants-TINY-20261015-051557Z.antsrep',
    ended: Math.floor(Date.now() / 1000) - 3600, map: 'TINY',
    seats: [seat(0, 'Green', '', 'Hard', { score: 2550, killed: 7, lost: 4, winner: true }), seat(1, 'Red', '', 'Hard', { score: 1010, killed: 7, lost: 5 }), seat(2, 'Blue', 'Ann', '', { score: 400, killed: 3, lost: 4 }), seat(3, 'Black', 'Bea', '', { score: 70, killed: 2, lost: 6 })],
    rows: [{ seats: [0, 2], score: 2950, killed: 10, lost: 8, hatched: 0, winner: true }, { seats: [1, 3], score: 1080, killed: 9, lost: 11, hatched: 0, winner: false }],
};
let liveCount = 0;
function liveList() {
    const out = [];
    for (let i = 0; i < liveCount; i++) out.push({ id: 'ants-TINY-2026101' + (5 + i) + '-051557Z', map: 'TINY', seconds: 100 + i, turns: 2000, players: [{ name: 'Ann', bot: '', color: 'Green' }, { name: '', bot: 'Hard', color: 'Red' }] });
    return { live: out };
}

function pageText(file, extra) {
    let text = fs.readFileSync(path.join(ROOT, file), 'utf8');
    text = text.replace('@@SITE_TITLE@@', '').replace('@@SITE_FOOTER@@', '');
    return extra ? extra(text) : text;
}
const CHANGELOG = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'ants_nav_')), 'changelog.html');
spawnSync('python3', [path.join(ROOT, 'tools', 'changelog_to_html.py'), path.join(ROOT, 'CHANGELOG.md'), CHANGELOG, '--version', 'v1.2.3', '--build-id', 'abc1234'], { cwd: ROOT });
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.json': 'application/json' };
const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://x');
    const send = (code, type, body) => { res.writeHead(code, { 'Content-Type': type, 'Cache-Control': 'no-store' }); res.end(body); };
    const p = url.pathname;
    if (p === '/matches') return send(200, TYPES['.html'], pageText('web/matches.html'));
    if (p === '/match') return send(200, TYPES['.html'], pageText('web/match.html'));
    if (p === '/legal') return send(200, TYPES['.html'], fs.readFileSync(path.join(ROOT, 'web/legal.html')));
    if (p === '/mapmaker') return send(200, TYPES['.html'], fs.readFileSync(path.join(ROOT, 'web/mapmaker.html')));
    if (p === '/changelog') return send(200, TYPES['.html'], fs.readFileSync(CHANGELOG));
    if (p === '/asset_catalog/') return send(200, TYPES['.html'], fs.readFileSync(path.join(ROOT, 'asset_catalog/index.html')));
    if (p === '/live') return send(200, TYPES['.json'], JSON.stringify(liveList()));
    if (p === '/history' || p === '/history/') return send(200, TYPES['.json'], JSON.stringify({ history: [REC, Object.assign({}, REC, { id: 'ants-TINY-20261015-041557Z' })], count: 2, total: 2 }));
    if (p.indexOf('/history/') === 0) return send(200, TYPES['.json'], JSON.stringify(REC));
    if (p === '/') return send(200, TYPES['.html'], '<!doctype html><title>front</title><p>front page');
    const file = path.join(ROOT, p.indexOf('/asset_catalog/') === 0 ? '' : 'web', p);
    if (file.indexOf(ROOT) === 0 && fs.existsSync(file) && fs.statSync(file).isFile()) return send(200, TYPES[path.extname(file)] || 'application/octet-stream', fs.readFileSync(file));
    return send(404, 'text/plain', 'no');
});

const measure = () => {       // runs in the page: where the bar's parts are
    const bar = document.querySelector('.sb');
    const box = (el) => { const r = el.getBoundingClientRect(); return { x: Math.round(r.left * 10) / 10, y: Math.round(r.top * 10) / 10, w: Math.round(r.width * 10) / 10, h: Math.round(r.height * 10) / 10 }; };
    const shown = (el) => { const r = el.getBoundingClientRect(); return r.width > 0 && r.height > 0 && getComputedStyle(el).visibility !== 'hidden'; };
    if (!bar) return null;
    const parts = [...bar.querySelectorAll(':scope > a, :scope > button, :scope > nav > a')].filter(shown).map((el) => Object.assign({ name: el.getAttribute('aria-label') || el.textContent.trim().replace(/\s+/g, ' ') || el.className, cls: el.className }, box(el)));
    const nav = bar.querySelector('nav');
    return {
        parts, scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth, bodyScrollW: document.body.scrollWidth,
        navShown: shown(nav), home: shown(bar.querySelector('.sbhome')), live: shown(bar.querySelector('.sblive')), liveText: bar.querySelector('.sblive').textContent,
        current: [...bar.querySelectorAll('nav [aria-current]')].map((a) => a.textContent.trim().replace(/\d+$/, '')), liveName: bar.querySelector('.sblive').getAttribute('aria-label'), liveHref: bar.querySelector('.sblive').getAttribute('href'),
        pictures: document.querySelectorAll('.sb img').length, wayHome: [...bar.querySelectorAll('a[href="/"]')].filter(shown).map(box), pad: parseFloat(getComputedStyle(bar).paddingLeft) || 0, menu: box(bar.querySelector('.sbtn')), bar: box(bar),
    };
};

async function main() {
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const base = 'http://127.0.0.1:' + server.address().port;
    const executablePath = process.env.CHROME || undefined;
    const browser = await playwright.chromium.launch({ executablePath, args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] });
    if (SHOTS) fs.mkdirSync(SHOTS, { recursive: true });
    for (const [pname, address, rowMark, menuMark] of PAGES) {
        for (const [sname, width, height] of SIZES) {
            const wide = width >= 720;
            const seen = {};
            for (const live of [0, 2]) {
                liveCount = live;
                const context = await browser.newContext({ viewport: { width, height }, deviceScaleFactor: 1 });
                const page = await context.newPage();
                const errors = [];
                page.on('pageerror', (e) => errors.push(String(e)));
                const tag = pname + ' ' + sname + ' live' + live;
                await page.goto(base + address, { waitUntil: 'load' });
                await page.waitForTimeout(live ? 700 : 300);
                if (SHOTS) await page.screenshot({ path: path.join(SHOTS, PREFIX + '_' + pname + '_' + sname + '_live' + live + '.png') });
                if (CHECKS) {
                    const m = await page.evaluate(measure);
                    check(tag + ': there is a bar', !!m);
                    if (!m) { await context.close(); continue; }
                    const wideOnes = m.scrollW > m.innerW ? await page.evaluate(() => [...document.querySelectorAll('body *')].filter((e) => e.getBoundingClientRect().right > window.innerWidth + 0.5 && e.getBoundingClientRect().width > 0).slice(0, 4).map((e) => e.tagName + '.' + e.className + ' ' + Math.round(e.getBoundingClientRect().right))) : [];
                    check(tag + ': no sideways scroll', m.scrollW <= m.innerW && m.bodyScrollW <= m.innerW, JSON.stringify([m.scrollW, m.bodyScrollW, m.innerW, wideOnes]));
                    check(tag + ': every part of the bar is inside the window', m.parts.every((p) => p.x >= 0 && p.x + p.w <= m.innerW + 0.5), JSON.stringify(m.parts.filter((p) => p.x < 0 || p.x + p.w > m.innerW + 0.5)));
                    check(tag + ': every button of the bar is 44 px or more tall', m.parts.every((p) => p.h >= 44), JSON.stringify(m.parts.filter((p) => p.h < 44)));
                    let overlap = null;
                    for (let i = 0; i < m.parts.length; i++) for (let j = i + 1; j < m.parts.length; j++) {
                        const a = m.parts[i], b = m.parts[j];
                        if (a.x < b.x + b.w - 0.5 && b.x < a.x + a.w - 0.5 && a.y < b.y + b.h - 0.5 && b.y < a.y + a.h - 0.5) overlap = [a.name, b.name];
                    }
                    check(tag + ': nothing in the bar overlaps', overlap === null, JSON.stringify(overlap));
                    check(tag + ': the bar draws no picture, a link to the front page is shown (44 px tall, inside the window), and the first button starts at the left edge of the bar itself', m.pictures === 0 && m.wayHome.length >= 1 && m.wayHome.every((h) => h.h >= 44 && h.x >= 0) && m.parts[0].x - m.bar.x - m.pad <= 2, JSON.stringify([m.pictures, m.wayHome, m.parts[0], m.bar]));
                    check(tag + ': ' + (wide ? 'the row of pages is shown and the phone\'s Home is not' : 'the phone\'s Home is shown and the row of pages is not'), wide ? m.navShown && !m.home : m.home && !m.navShown);
                    if (wide) {
                        const names = m.parts.filter((p) => !/sblive/.test(p.cls)).map((p) => p.name);
                        check(tag + ': the row is Home, Matches, Map maker, Discord', JSON.stringify(names.filter((n) => n !== 'Menu' && n.length)) === JSON.stringify(['Home', 'Matches', 'Map maker', 'Discord server']), names.join('|'));
                    }
                    check(tag + ': the page that is open is marked in the row', JSON.stringify(m.current) === JSON.stringify(rowMark ? [rowMark] : []), JSON.stringify(m.current));
                    const expectLive = live && ['matches', 'match'].indexOf(pname) !== -1;
                    check(tag + ': the live mark (shown only while a match runs, at every width), its name and its link', expectLive ? m.live && m.liveText === '2 live' && m.liveName === '2 matches live, see them on the front page' && m.liveHref === '/' : !m.live, JSON.stringify([m.live, m.liveText, m.liveName, m.liveHref]));
                    seen[live] = m;
                    // the Menu
                    const open = await page.evaluate(() => { document.querySelector('.sb .sbtn').click(); const box = document.getElementById('sbm'); return box && !box.hidden ? { n: box.querySelectorAll('a').length, expanded: document.querySelector('.sb .sbtn').getAttribute('aria-expanded'), cur: [...box.querySelectorAll('[aria-current]')].map((a) => a.textContent.replace(/\d+$/, '')), inside: (() => { const r = box.getBoundingClientRect(); return r.left >= 0 && r.right <= window.innerWidth && r.bottom <= window.innerHeight + 1; })() } : null; });
                    check(tag + ': the Menu opens with the four links and the five small ones', !!open && open.n === 9 && open.expanded === 'true', JSON.stringify(open));
                    if (open) {
                        check(tag + ': the Menu marks the page that is open', JSON.stringify(open.cur) === JSON.stringify(menuMark ? [menuMark] : rowMark ? [rowMark] : []), JSON.stringify(open.cur));
                        check(tag + ': the Menu is inside the window', open.inside);
                        if (SHOTS && live === 2 && (pname === 'matches' || pname === 'mapmaker') && ['390', '1280'].indexOf(sname) !== -1) await page.screenshot({ path: path.join(SHOTS, PREFIX + '_' + pname + '_' + sname + '_menu.png') });
                    }
                    await page.keyboard.press('Escape');
                    const closed = await page.evaluate(() => ({ hidden: document.getElementById('sbm').hidden, expanded: document.querySelector('.sb .sbtn').getAttribute('aria-expanded'), focus: document.activeElement === document.querySelector('.sb .sbtn') }));
                    check(tag + ': Escape closes the Menu and the focus goes back to its button', closed.hidden && closed.expanded === 'false' && closed.focus, JSON.stringify(closed));
                    await page.click('.sb .sbtn');
                    await page.mouse.click(Math.max(2, width - 5), height - 5);
                    const outside = await page.evaluate(() => document.getElementById('sbm').hidden);
                    check(tag + ': a press outside closes the Menu', outside);
                    check(tag + ': no script error', errors.filter((e) => !/lvl_core|Module|is not defined|Unexpected token/.test(e)).length === 0, errors.join(' | '));
                }
                await context.close();
            }
            if (CHECKS && seen[0] && seen[2]) {
                const still = (a, b) => Math.abs(a.x - b.x) < 0.6 && Math.abs(a.y - b.y) < 0.6 && Math.abs(a.w - b.w) < 0.6 && Math.abs(a.h - b.h) < 0.6;
                const a = seen[0], b = seen[2];
                check(pname + ' ' + sname + ': nothing moves when the live count arrives (the first button, the Menu button and the bar)', still(a.parts[0], b.parts[0]) && still(a.menu, b.menu) && still(a.bar, b.bar), JSON.stringify([a.parts[0], b.parts[0], a.menu, b.menu]));
                if (wide) check(pname + ' ' + sname + ': ... nor the row\'s buttons', a.parts.filter((p) => !/sblive/i.test(p.cls)).every((p) => b.parts.some((q) => q.name === p.name && still(p, q))), '');
            }
        }
    }
    // the Map maker's own screens (the page's own state is set: My maps is the #mine section shown with the editor's row and body hidden, as web/front/mapmaker_app.js does; Crop to fit is the #crop section shown with the editor hidden)
    if (CHECKS) {
        const reachable = () => [...document.querySelectorAll('a[href], button, summary, input, select, textarea, [tabindex]')].filter((e) => e.tabIndex >= 0 && !e.disabled && e.checkVisibility({ visibilityProperty: true }) && !e.closest('[inert]'));
        for (const [width, height] of [[390, 844], [600, 800], [601, 800], [720, 900], [844, 390], [1280, 800], [1920, 1080]]) {
            const context = await browser.newContext({ viewport: { width, height }, deviceScaleFactor: 1 });
            const page = await context.newPage();
            await page.goto(base + '/mapmaker', { waitUntil: 'load' });
            await page.waitForTimeout(300);
            const tag = 'mapmaker ' + width + 'x' + height;
            const up = await page.evaluate((reach) => {
                const r = new Function('return (' + reach + ')')();
                const barBefore = r().filter((e) => e.closest('.sb')).length;
                document.getElementById('top').hidden = document.getElementById('body').hidden = true; document.getElementById('mine').hidden = false;
                const list = r(), home = document.getElementById('mine-home'), hb = home.getBoundingClientRect();
                const bar = document.querySelector('.sb a.sbhome');
                return { barBefore, barReachable: list.filter((e) => e.closest('.sb')).length, ids: list.map((e) => e.id || e.className).slice(0, 8), homeShown: home.checkVisibility({ visibilityProperty: true }), href: home.getAttribute('href'), homeIn: hb.left >= 0 && hb.right <= innerWidth && hb.top >= 0 && hb.bottom <= innerHeight, homeH: hb.height,
                    barHomeShown: !!bar && bar.checkVisibility({ visibilityProperty: true }) && bar.getBoundingClientRect().bottom <= innerHeight };
            }, reachable.toString());
            const wide = width >= 601;
            check(tag + ': My maps up: ' + (wide ? 'the covered bar cannot be reached with the Tab key or seen by a screen reader, and the screen has its own Home (to "/", in the window, 44 px)' : 'the bar and its Home are seen, and the screen has no second Home'),
                wide ? up.barBefore > 0 && up.barReachable === 0 && up.homeShown && up.href === '/' && up.homeIn && up.homeH >= 43.5 : up.barHomeShown && !up.homeShown, JSON.stringify(up));
            const back = await page.evaluate((reach) => { document.getElementById('mine').hidden = true; document.getElementById('top').hidden = document.getElementById('body').hidden = false; return new Function('return (' + reach + ')')()().filter((e) => e.closest('.sb')).length; }, reachable.toString());
            check(tag + ': My maps closed: the bar is in the Tab order again', back > 0, String(back));
            const crop = await page.evaluate((reach) => { document.getElementById('editor').hidden = true; document.getElementById('crop').hidden = false; const list = new Function('return (' + reach + ')')()(); const cr = document.getElementById('crop'); return { outside: list.filter((e) => !cr.contains(e)).map((e) => e.tagName + '#' + e.id), inside: list.filter((e) => cr.contains(e)).length }; }, reachable.toString());
            check(tag + ': Crop to fit up: nothing outside the step can be reached with the Tab key, and the step has its own controls (it is left by Cancel)', crop.outside.length === 0 && crop.inside > 0, JSON.stringify(crop));
            await context.close();
        }
    }
    await browser.close();
    server.close();
    console.log('web_site_nav_browser: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main().catch((e) => { console.log('ERROR ' + (e && e.stack || e)); try { server.close(); } catch (x) { /* done */ } process.exit(1); });
