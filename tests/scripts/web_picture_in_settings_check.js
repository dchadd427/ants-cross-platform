// The game page with its game stood in for (no WebAssembly build): what the page does once the picture's shape and the fullscreen mouse live in the settings menu only (docs/GAME_MENU.md).
// A real Chromium (Playwright, when the machine has it: the exit status is 3 and nothing is checked when it does not) opens web/shell.html, served from a copy with its @@ placeholders filled, and the
// stand-in is the Module's menu calls (the rows of tests/data/game_menu_match.json) and the page's own postRun hook, which is what the game's start calls:
//   * there is no strip under the game: the box takes the window's height that the strip had, and the WHOLE framed box (its border and red shadow too) is inside the window with at least the page's side margin
//     under it, as large as that allows (1280 x 800 and 844 x 390: the height decides; 390 x 844, 360 x 740 and 448 x 998: the picture is as wide as the page);
//   * the header has ONE Settings button after Menu at 360, 390, 844 and 1280 px, 44 x 44 or more, named "Settings", and the header stays one row on a 360 px phone (Menu, Fullscreen, Settings, More);
//   * a tap on it (390 px, touch) opens the menu, Video has a Picture shape row, and a choice changes the page's shape at once, is remembered under ants.aspect.v2 and put in the address;
//   * on a computer the Fullscreen mouse row switches Locked / Free and the choice is remembered under ants.pointerlock; on a touch screen the row is not there;
//   * the game's own call for Esc (the stand-in has no game to press the key in) and the button open the same menu;
//   * outside a match (the stand-in answers "no match screen"): the button opens the small form with Fullscreen, Picture shape and Fullscreen mouse, and with listeners that imitate the game's window system (SDL's: key
//     presses and typed text on the window in the bubble phase, mouse on the canvas) only the lifts of keys and a bare modifier's press reach them while it is up, for typed letters, Enter, Space, Tab, an arrow, Ctrl+S
//     and a click; Enter on Close closes it although the stand-in cancels `keypress`; a shape pick and Locked / Free work; a phone at 390 x 844 opens it by touch; the full menu of a match still lets a letter through
//     and answers Enter on Resume.
// usage: node web_picture_in_settings_check.js <the folder of web/>     (exit 0: every check holds; 1: one fails; 3: no browser to run it in)
'use strict';
const fs = require('fs'), http = require('http'), os = require('os'), path = require('path');
let chromium;
for (const where of ['playwright', '/opt/node-tools/node_modules/playwright', process.env.PLAYWRIGHT_MODULE || '']) {
    if (!where) continue;
    try { chromium = require(where).chromium; if (chromium) break; } catch (e) { /* try the next place */ }
}
const web = process.argv[2];
if (!web) { console.log('usage: web_picture_in_settings_check.js web'); process.exit(2); }
if (!chromium) { console.log('SKIP: no Playwright on this machine'); process.exit(3); }

const dataDir = path.resolve(web, '..', 'tests', 'data');
const MATCH = fs.readFileSync(path.join(dataDir, 'game_menu_match.json'), 'utf8').trim();
const folder = fs.mkdtempSync(path.join(os.tmpdir(), 'ants-pis-'));
for (const name of fs.readdirSync(web)) {
    const from = path.join(web, name);
    if (fs.statSync(from).isDirectory()) fs.cpSync(from, path.join(folder, name), { recursive: true });
    else fs.copyFileSync(from, path.join(folder, name));
}
for (const name of ['shell.html']) {
    const file = path.join(folder, name);
    fs.writeFileSync(file, fs.readFileSync(file, 'utf8').replace(/@@SITE_TITLE@@|@@SITE_FOOTER@@/g, '').replace(/@@GAME_VERSION@@/g, 'v0.0.0').replace(/@@BUILD_ID@@/g, 'test').replace(/@@BUILD_DATE@@/g, '2026-01-01').replace(/@@BUILD_TIMESTAMP@@/g, '1').replace(/@@DATA_SIZE@@/g, '0'));
}
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png', '.ttf': 'font/ttf', '.json': 'application/json' };
const server = http.createServer((q, r) => {
    let p = decodeURIComponent(q.url.split('?')[0]);
    if (p.startsWith('/replays/') || p.startsWith('/live/')) { r.writeHead(200, { 'content-type': 'application/octet-stream' }); r.end(Buffer.from('ANTSREP0')); return; }      // (a recording's page asks for its file: any bytes will do, the game is not there)
    if (p === '/play') p = '/shell.html';
    const f = path.join(folder, p);
    fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); r.end('no'); } else { r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); } });
});

// the game, stood in for: the menu's calls answer from the table of rows; then the page's own hook that the game's start calls
const STAND_IN = `(function () {
    var values = { 1: 100, 2: 65, 3: 99, 4: 0, 8: 0, 9: 50, 10: 1, 11: 1, 15: 0 };
    Module.UTF8ToString = function (p) { return p; };
    Module._ants_menu_describe = function () { return ${JSON.stringify(MATCH)}; };
    Module._ants_menu_get = function (w) { return w === 100 ? (window.__where === undefined ? 0 : window.__where) : w === 101 ? 0 : w === 102 ? 2 : (w in values ? values[w] : -1); };
    Module._ants_menu_set = function (id, v) { values[id] = v; return 1; };
    Module._ants_menu_do = function () { return 1; };
    window.__told = [];
    Module._ants_menu_open = function (o) { window.__told.push(o); };
    Module.postRun[0]();
    return 1;
})()`;

// the game's window system, imitated (SDL 2.28.4 for Emscripten: key presses, typed text and key lifts on the window in the bubble phase, the mouse on the canvas, the release on the document); what each handler returns is
// what the game does with the event (true: it cancels it, as the real one cancels a keypress)
const LISTENERS = `(function () {
    window.__got = [];
    var nav = function (e) { var k = e.keyCode; return k === 8 || k === 9 || (k >= 37 && k <= 40) || (k >= 112 && k <= 135) || e.ctrlKey; };
    window.addEventListener('keydown', function (e) { window.__got.push('down:' + e.key); if (nav(e)) e.preventDefault(); }, false);
    window.addEventListener('keypress', function (e) { window.__got.push('press:' + e.key); e.preventDefault(); }, false);
    window.addEventListener('keyup', function (e) { window.__got.push('up:' + e.key); }, false);
    var c = document.getElementById('canvas');
    ['mousedown', 'mousemove', 'wheel', 'touchstart', 'touchend'].forEach(function (n) { c.addEventListener(n, function (e) { if (e.isTrusted) window.__got.push(n); }, false); });
    return 1;
})()`;
const taken = (page) => page.evaluate(() => { const g = window.__got; window.__got = []; return g; });

let failed = 0, checks = 0;
function check(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) { failed++; console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); }
}
const ok = (label, v) => check(label, !!v, true);

async function open(browser, port, w, h, touch, query) {
    const context = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: touch ? 2 : 1, isMobile: touch, hasTouch: touch });
    const page = await context.newPage();
    await page.goto('http://localhost:' + port + '/play' + (query || '?map=TREASURE'), { waitUntil: 'load' });
    await page.waitForTimeout(400);
    await page.evaluate(STAND_IN);
    await page.waitForFunction(() => { const b = document.getElementById('settings-btn') || document.getElementById('settings-btn-bar'); return b && !b.hidden && !b.disabled; }, null, { timeout: 8000 });
    await page.waitForTimeout(300);
    return { context, page };
}
const geometry = (page) => page.evaluate(() => {
    const rect = (e) => { const b = e.getBoundingClientRect(); return { x: b.x, y: b.y, w: b.width, h: b.height }; };
    const stage = document.getElementById('game-stage'), header = document.querySelector('header'), actions = document.querySelector('.header-actions'), box = document.getElementById('game-container');
    // how far the box's shadows reach below it: the largest of (offset down + blur + spread) of the box-shadow
    const reach = (getComputedStyle(box).boxShadow.match(/-?[\d.]+px -?[\d.]+px -?[\d.]+px -?[\d.]+px/g) || []).map((t) => { const n = t.match(/-?[\d.]+/g).map(Number); return n[1] + n[2] + n[3]; }).reduce((a, b) => Math.max(a, b), 0);
    const button = document.getElementById('settings-btn'), pad = parseFloat(getComputedStyle(document.body).paddingBottom) || 0;
    return { box: rect(box), reach, side: parseFloat(getComputedStyle(document.body).paddingLeft) || 0, scrollY: window.scrollY, stage: rect(stage), header: rect(header), actions: rect(actions), button: rect(button), name: button.getAttribute('aria-label'), pad, innerW: innerWidth, innerH: innerHeight, scrollW: document.documentElement.scrollWidth,
             buttonsInHeader: Array.prototype.map.call(document.querySelectorAll('.gm-btn'), (b) => b.id), strip: !!document.getElementById('view-bar') || !!document.getElementById('aspect-select') || !!document.getElementById('lock-on') };
});

server.listen(0, async () => {
    const port = server.address().port;
    const browser = await chromium.launch({ args: ['--no-sandbox'] });
    try {
        // the whole framed box (border and shadow too) is inside the window, with at least the page's side margin under it, and is as large as that allows
        const sizes = [[1280, 800, false, 'height'], [844, 390, true, 'height'], [390, 844, true, 'width'], [360, 740, true, 'width'], [448, 998, true, 'width']];
        for (const [w, h, touch, limit] of sizes) {
            const { context, page } = await open(browser, port, w, h, touch);
            const g = await geometry(page);
            const at = w + ' x ' + h;
            ok(at + ': no strip under the picture (no view bar, no Picture dropdown, no mouse buttons)', !g.strip);
            const room = g.innerH - (g.box.y + g.box.h + g.reach);
            ok(at + ': the box with its ring and shadow ends ' + room.toFixed(1) + ' px above the window\'s bottom edge, at least the page\'s side margin (' + g.side + ' px)', room >= g.side - 0.5);
            ok(at + ': the box starts inside the window and the page is at the top (' + g.box.y.toFixed(0) + ', scroll ' + g.scrollY + ')', g.box.y >= 0 && g.scrollY === 0);
            ok(at + ': nothing scrolls sideways', g.scrollW <= g.innerW);
            if (limit === 'height') ok(at + ': as large as that allows: ' + (room - g.side).toFixed(1) + ' px of slack is less than one step of the picture', room - g.side >= -0.5 && room - g.side <= 20);
            else ok(at + ': as large as that allows: the picture is as wide as the page (' + g.box.w.toFixed(1) + ' of ' + (g.innerW - 2 * g.side) + ')', Math.abs(g.box.w - (g.innerW - 2 * g.side)) <= 8);
            await context.close();
        }
        // one Settings button, after Menu, big enough and named, and the header on one row
        for (const [w, h, touch] of [[360, 740, true], [390, 844, true], [844, 390, true], [1280, 800, false]]) {
            const { context, page } = await open(browser, port, w, h, touch);
            const g = await geometry(page);
            const at = w + ' x ' + h;
            check(at + ': one Settings button in the page', g.buttonsInHeader, ['settings-btn']);
            check(at + ': named Settings', g.name, 'Settings');
            ok(at + ': at least 44 x 44 (' + g.button.w.toFixed(0) + ' x ' + g.button.h.toFixed(0) + ')', g.button.w >= 43.5 && g.button.h >= 43.5);
            ok(at + ': it is in the header, in the row of Menu', g.button.y >= g.header.y - 1 && g.button.y + g.button.h <= g.header.y + g.header.h + 1);
            if (w <= 700 || h <= 520) ok(at + ': the header is one row (' + g.header.h.toFixed(0) + ' px)', g.header.h <= 50);
            ok(at + ': no sideways scroll', g.scrollW <= g.innerW);
            const nextTo = await page.evaluate(() => document.getElementById('settings-btn').previousElementSibling.id);
            check(at + ': right after Menu', nextTo, 'menu-btn');
            // the header draws no logo: Menu is the way to the front page, it starts at the header's left edge (no gap where a logo was), and the page's heading is still there for a screen reader
            const head = await page.evaluate(() => { const h = document.querySelector('header'), m = document.getElementById('menu-btn'), r = (e) => e.getBoundingClientRect(); return { pictures: h.querySelectorAll('img').length, href: m.getAttribute('href'), gap: r(m).left - r(h).left, shown: r(m).width > 0, h1: [...document.querySelectorAll('h1')].map((e) => e.textContent) }; });
            ok(at + ': the header has no picture, Menu leads to the front page and starts at the header\'s left edge, and the h1 names the page (' + JSON.stringify(head) + ')', head.pictures === 0 && head.href === '/' && head.shown && head.gap <= 1 && head.h1.length === 1 && /Ants/.test(head.h1[0]));
            await context.close();
        }
        // a tap on a phone opens the menu; Video has the Picture shape row; a choice changes the page's shape at once and is remembered
        {
            const { context, page } = await open(browser, port, 390, 844, true);
            await page.tap('#settings-btn');
            await page.waitForSelector('#game-menu', { timeout: 4000 });
            await page.tap('#gm-tab-video');
            await page.waitForTimeout(300);
            const rows = await page.evaluate(() => Array.prototype.map.call(document.querySelectorAll('.gm-row .gm-name'), (n) => n.textContent));
            ok('390 px, a tap on the header\'s Settings opens the menu, and Video has the Picture shape row (' + rows.join(', ') + ')', rows.indexOf('Picture shape') >= 0);
            ok('... and no Fullscreen mouse row on a touch screen', rows.indexOf('Fullscreen mouse') < 0);
            const before = await page.evaluate(() => document.getElementById('game-stage').getAttribute('data-aspect'));
            await page.tap('#gm-picture');
            await page.waitForTimeout(200);
            await page.tap('#gm-picture-o6');                                       // 32:9, the last row of the table
            await page.waitForTimeout(500);
            const after = await page.evaluate(() => ({ shape: document.getElementById('game-stage').getAttribute('data-aspect'), kept: localStorage.getItem('ants.aspect.v2'), address: location.search, open: !!document.getElementById('game-menu') }));
            check('a choice in the row changes the shape at once (was ' + before + '), remembers it, puts it in the address and leaves the menu open', after, { shape: '32:9', kept: '32:9', address: '?map=TREASURE&aspect=32:9', open: true });
            await context.close();
        }
        // a computer: Esc opens the same menu, the Fullscreen mouse row switches Locked / Free and is remembered
        {
            const { context, page } = await open(browser, port, 1280, 800, false);
            await page.evaluate(() => window.antsGameMenuAsk());                       // (what the game's own Esc calls: the stand-in has no game to press it in)
            await page.waitForSelector('#game-menu', { timeout: 4000 });
            await page.click('#gm-tab-video');
            await page.waitForTimeout(300);
            const state = () => page.evaluate(() => { const row = Array.prototype.filter.call(document.querySelectorAll('.gm-row'), (r) => r.querySelector('.gm-name') && r.querySelector('.gm-name').textContent === 'Fullscreen mouse')[0]; return row ? { pressed: Array.prototype.map.call(row.querySelectorAll('button'), (b) => b.getAttribute('aria-checked')), kept: localStorage.getItem('ants.pointerlock'), words: Array.prototype.map.call(row.querySelectorAll('button'), (b) => b.textContent) } : null; });
            const first = await state();
            check('1280 px, the Esc call of the game opens the menu and the Fullscreen mouse row starts Locked', first && [first.words, first.pressed, first.kept], [['Locked', 'Free'], ['true', 'false'], null]);
            await page.evaluate(() => { const row = Array.prototype.filter.call(document.querySelectorAll('.gm-row'), (r) => r.querySelector('.gm-name') && r.querySelector('.gm-name').textContent === 'Fullscreen mouse')[0]; row.querySelectorAll('button')[1].click(); });
            await page.waitForTimeout(200);
            const free = await state();
            check('Free: pressed, remembered', free && [free.pressed, free.kept], [['false', 'true'], 'off']);
            await page.evaluate(() => { const row = Array.prototype.filter.call(document.querySelectorAll('.gm-row'), (r) => r.querySelector('.gm-name') && r.querySelector('.gm-name').textContent === 'Fullscreen mouse')[0]; row.querySelectorAll('button')[0].click(); });
            await page.waitForTimeout(200);
            const locked = await state();
            check('Locked again: pressed, remembered', locked && [locked.pressed, locked.kept], [['true', 'false'], 'on']);
            await page.click('.gm-x').catch(() => {});
            await context.close();
        }
        // the rematch strip shown (an online match is over): the room under the column is under the strip, the picture-to-strip gap is the strip's own, and picture, strip and ring fit the window
        for (const [w, h, touch] of [[1280, 800, false], [844, 390, true]]) {
            const { context, page } = await open(browser, port, w, h, touch);
            await page.evaluate(() => { const s = document.getElementById('rematch'); s.hidden = false; s.style.minHeight = '88px'; });
            await page.waitForTimeout(500);
            const g = await page.evaluate(() => {
                const r = (e) => { const b = e.getBoundingClientRect(); return { y: b.y, h: b.height, bottom: b.bottom }; };
                const stage = document.getElementById('game-stage'), strip = document.getElementById('rematch'), box = document.getElementById('game-container');
                return { stage: r(stage), strip: r(strip), box: r(box), side: parseFloat(getComputedStyle(document.body).paddingLeft), innerH: innerHeight, stageMargin: parseFloat(getComputedStyle(stage).marginBottom), stripMargin: parseFloat(getComputedStyle(strip).marginBottom), shadow: 6 };
            });
            const at = w + ' x ' + h + ', strip shown';
            check(at + ': no room under the picture (the stage has no bottom margin)', g.stageMargin, 0);
            ok(at + ': the gap from the picture to the strip is the strip\'s own 16 px (' + (g.strip.y - g.stage.bottom).toFixed(1) + ')', Math.abs(g.strip.y - g.stage.bottom - 16) <= 1);
            const room = g.innerH - (g.strip.bottom + g.shadow);
            ok(at + ': the strip with its shadow ends ' + room.toFixed(1) + ' px above the window\'s bottom edge, at least the side margin (' + g.side + ' px)', room >= g.side - 0.5);
            ok(at + ': ... and the picture is as large as that allows (slack ' + (room - g.side).toFixed(1) + ' px)', room - g.side <= 20);
            await context.close();
        }
        // a recording and a live match: the bar's Settings button opens the menu, Video has the Picture shape row and a choice changes the shape at once
        for (const [query, label] of [['?replay=ants-TEST-20260101-000000Z.antsrep', 'a replay'], ['?live=LIVE-1', 'a live match']]) {
            const { context, page } = await open(browser, port, 1280, 800, false, query);
            await page.evaluate(() => { const b = document.getElementById('rbar'); if (b) b.hidden = false; for (const q of ['.veil', '#rm']) { const e = document.querySelector(q); if (e) e.hidden = true; } });      // (the stand-in has no recording to show: the page's own notice of that is put away)
            await page.evaluate(() => document.getElementById('settings-btn-bar').click());      // (a script's click: the stand-in's bar may not take a pointer)
            await page.waitForSelector('#game-menu', { timeout: 4000 });
            await page.click('#gm-tab-video');
            await page.waitForTimeout(300);
            const rows = await page.evaluate(() => Array.prototype.map.call(document.querySelectorAll('.gm-row .gm-name'), (n) => n.textContent));
            ok(label + ': the bar\'s Settings opens the menu and Video has the Picture shape row (' + rows.join(', ') + ')', rows.indexOf('Picture shape') >= 0);
            await page.click('#gm-picture');
            await page.waitForTimeout(200);
            await page.click('#gm-picture-o6');
            await page.waitForTimeout(400);
            const after = await page.evaluate(() => ({ shape: document.getElementById('game-stage').getAttribute('data-aspect'), kept: localStorage.getItem('ants.aspect.v2') }));
            check(label + ': a choice changes the shape at once and is remembered', after, { shape: '32:9', kept: '32:9' });
            await context.close();
        }
        // a recording, a live view or a file of one's own: the header's Menu is not there, so a Home button is first in the row and the first Tab stop, in the window, and in the plain page it is not drawn
        for (const [w, h, touch] of [[1280, 800, false], [1000, 700, false], [844, 390, true], [390, 844, true], [360, 740, true], [320, 568, true]]) {
            for (const query of ['?replay=x.antsrep', '?live=x']) {
                const { context, page } = await open(browser, port, w, h, touch, query);
                const at = w + ' x ' + h + ' ' + query;
                const g = await page.evaluate(() => {
                    const b = document.getElementById('home-btn'), r = b.getBoundingClientRect(), others = [...document.querySelectorAll('header .btn')].filter((e) => e !== b && e.getClientRects().length && e.getBoundingClientRect().width > 0).map((e) => e.getBoundingClientRect());
                    return { shown: b.getClientRects().length > 0 && getComputedStyle(b).visibility !== 'hidden', href: b.getAttribute('href'), x: r.left, y: r.top, r: r.right, b: r.bottom, h: r.height, w: r.width, innerW: innerWidth, innerH: innerHeight, scrollW: document.documentElement.scrollWidth,
                        focus: ([...document.querySelectorAll('a[href], button, summary, input, select, textarea, [tabindex]')].find((e) => e.tabIndex >= 0 && !e.disabled && e.checkVisibility({ visibilityProperty: true }) && !e.closest('[inert]')) || {}).id, sameHeight: Math.abs(r.height - document.getElementById('all-btn').getBoundingClientRect().height) < 1, menuShown: !!document.getElementById('menu-btn').getClientRects().length,
                        overlaps: others.filter((o) => o.left < r.right - 0.5 && r.left < o.right - 0.5 && o.top < r.bottom - 0.5 && r.top < o.bottom - 0.5).length, others: others.filter((o) => o.right > innerWidth + 0.5).length };
                });
                ok(at + ': Home is drawn, to "/", inside the window (' + JSON.stringify([Math.round(g.x), Math.round(g.y), Math.round(g.r), Math.round(g.b)]) + '), as high as All matches, over no other button, and the header scrolls nowhere sideways ' + JSON.stringify(g), g.shown && g.href === '/' && g.x >= 0 && g.r <= g.innerW && g.y >= 0 && g.b <= g.innerH && g.sameHeight && g.overlaps === 0 && g.others === 0 && g.scrollW <= g.innerW);
                check(at + ': it is first in the Tab order of the page (the header\'s Menu is not drawn here)', [g.focus, g.menuShown], ['home-btn', false]);
                await context.close();
            }
        }
        {
            const { context, page } = await open(browser, port, 1280, 800, false);
            ok('1280 x 800, the plain page: the recording\'s Home button is not drawn (Menu is)', await page.evaluate(() => document.getElementById('home-btn').getClientRects().length === 0 && document.getElementById('menu-btn').getClientRects().length > 0));
            await context.close();
        }
        // the header stays one row where Feedback leaves it (a window of 1000 px, a phone on its side at 780 px)
        for (const [w, h, touch] of [[1000, 700, false], [780, 390, true]]) {
            const { context, page } = await open(browser, port, w, h, touch);
            const g = await geometry(page);
            ok(w + ' x ' + h + ': the header is one row (' + g.header.h.toFixed(0) + ' px)', g.header.h <= 50);
            await context.close();
        }
        // the header's button on a computer opens the same menu as Esc
        {
            const { context, page } = await open(browser, port, 1280, 800, false);
            await page.click('#settings-btn');
            await page.waitForSelector('#game-menu', { timeout: 4000 });
            ok('1280 px, a click on the header\'s Settings opens the menu', true);
            await context.close();
        }
        // outside a match: the small form, and what reaches the game while it is up
        {
            const { context, page } = await open(browser, port, 1280, 800, false);
            await page.evaluate(LISTENERS);
            await page.evaluate(() => { window.__where = -1; });
            await page.waitForFunction(() => { const b = document.getElementById('settings-btn'); return b && !b.disabled && b.title.indexOf('picture shape') >= 0; }, null, { timeout: 4000 });
            await page.click('#settings-btn');
            await page.waitForSelector('#game-menu', { timeout: 4000 });
            const shape = await page.evaluate(() => ({ tabs: document.querySelectorAll('#game-menu [role=tab]').length, rows: Array.prototype.map.call(document.querySelectorAll('#game-menu .gm-name'), (n) => n.textContent), active: document.activeElement.textContent, told: window.__told.slice() }));
            check('outside a match: the small form has no tabs and the three rows of the page', [shape.tabs, shape.rows], [0, ['Fullscreen', 'Picture shape', 'Fullscreen mouse']]);
            check('...the focus is on Close, and the game was told it is open', [shape.active, shape.told], ['Close', [1]]);
            await taken(page);
            for (const k of ['s', 'q', 'x', 'c', 't', 'h', 'Tab', 'ArrowDown', 'Backspace', 'F9', 'Delete', 'Control+s', 'Shift']) await page.keyboard.press(k);
            await page.focus('#game-menu .pick-select');
            await page.keyboard.press('Space');
            await page.keyboard.press('Escape');
            const got = await taken(page);
            check('outside a match with the small form up: only the lifts of keys and a bare modifier\'s press reach the game\'s listeners', got.filter((g) => !/^up:/.test(g) && g !== 'down:Shift' && g !== 'down:Control'), []);
            ok('...and the lifts do (the game would think a key still held otherwise): ' + got.filter((g) => /^up:/.test(g)).length, got.filter((g) => /^up:/.test(g)).length >= 10);
            check('...and the panel is still up (no key closed it)', await page.evaluate(() => !!document.getElementById('game-menu')), true);
            await page.mouse.move(40, 400);
            await page.mouse.down();
            await page.mouse.up();
            await page.mouse.wheel(0, 60);
            check('...a click and the wheel on the dimmed picture reach nothing of the game\'s', await taken(page), []);
            // a shape pick and Locked / Free work from here
            await page.click('#game-menu .pick-select');
            await page.click('#gm-picture-o5');
            const picked = await page.evaluate(() => ({ stage: document.getElementById('game-stage').getAttribute('data-aspect'), stored: localStorage.getItem('ants.aspect.v2'), address: location.search }));
            check('a shape pick sets the stage\'s shape, the stored choice and the address', [picked.stage, picked.stored, /aspect=21:9/.test(picked.address)], ['21:9', '21:9', true]);
            await page.click('#game-menu [role=radio]:nth-of-type(2)');
            check('Free is remembered under ants.pointerlock', await page.evaluate(() => localStorage.getItem('ants.pointerlock')), 'off');
            await taken(page);
            await page.focus('.gm-resume');
            await page.keyboard.press('Enter');
            await page.waitForTimeout(150);
            check('Enter on Close closes it although the game cancels the keypress, and the game was told', [await page.evaluate(() => !document.getElementById('game-menu')), await page.evaluate(() => window.__told.slice(-1)[0])], [true, 0]);
            check('...and what reached the game was the lift only', (await taken(page)).filter((g) => !/^up:/.test(g)), []);
            // the full menu of a match: a letter goes on, Enter presses Resume
            await page.evaluate(() => { window.__where = 0; });
            await page.waitForFunction(() => document.getElementById('settings-btn').title.indexOf('sound') >= 0, null, { timeout: 4000 });
            await page.click('#settings-btn');
            await page.waitForSelector('#game-menu .gm-tab', { timeout: 4000 });
            await taken(page);
            await page.keyboard.press('s');
            ok('in a match the full menu is unchanged: a letter still reaches the page and the game (the game takes none while the panel is up)', (await taken(page)).indexOf('down:s') >= 0);
            await page.focus('.gm-resume');
            await page.keyboard.press('Enter');
            await page.waitForTimeout(150);
            check('...Enter on Resume closes it although the game cancels the keypress', await page.evaluate(() => !document.getElementById('game-menu')), true);
            // the small form closes itself when the match has the screen
            await page.evaluate(() => { window.__where = -1; });
            await page.waitForFunction(() => document.getElementById('settings-btn').title.indexOf('picture shape') >= 0, null, { timeout: 4000 });
            await page.click('#settings-btn');
            await page.waitForSelector('#game-menu', { timeout: 4000 });
            await page.evaluate(() => { window.__where = 0; });
            await page.waitForFunction(() => !document.getElementById('game-menu'), null, { timeout: 4000 });
            ok('a match that begins closes the small form', true);
            await context.close();
        }
        // a phone, by touch
        {
            const { context, page } = await open(browser, port, 390, 844, true);
            await page.evaluate(() => { window.__where = -1; });
            await page.waitForFunction(() => document.getElementById('settings-btn').title.indexOf('picture shape') >= 0, null, { timeout: 4000 });
            await page.tap('#settings-btn');
            await page.waitForSelector('#game-menu', { timeout: 4000 });
            const names = await page.evaluate(() => ({ rows: Array.prototype.map.call(document.querySelectorAll('#game-menu .gm-name'), (n) => n.textContent), box: (() => { const r = document.querySelector('#game-menu .gm-panel').getBoundingClientRect(); return [r.left >= 0, r.right <= innerWidth, r.top >= 0, r.bottom <= innerHeight]; })() }));
            check('390 x 844 by touch: the small form has Fullscreen and Picture shape (no mouse row) and fits the screen', [names.rows, names.box], [['Fullscreen', 'Picture shape'], [true, true, true, true]]);
            await page.tap('.gm-resume');
            check('...Close closes it', await page.evaluate(() => !document.getElementById('game-menu')), true);
            await context.close();
        }
    } catch (e) {
        failed++;
        console.log('FAIL ' + (e && e.stack || e));
    }
    await browser.close();
    server.close();
    fs.rmSync(folder, { recursive: true, force: true });
    console.log('web picture in settings: ' + checks + ' checks, ' + failed + ' failures');
    process.exit(failed ? 1 : 0);
});
