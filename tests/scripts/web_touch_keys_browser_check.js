// The hotkey buttons (web/touch_keys.js, docs/TOUCH.md "The hotkey buttons") against a REAL game in a phone-sized browser (opt-in: tests/scripts/test_web_touch_keys.sh; Playwright, which CI does not install).
// It opens the game page of a running web image (a match for one against a computer player), emulates a phone with touch, and reads what the GAME believes through its probes (ants_probe 50: the ants that
// are picked, plus 1000 for a hill, plus 100000 for an ant that is inspected; 3 and 4: the view's origin; 5: a dialog is open; 30 - 34: the touch model's counters; 0 and 1: the pointer):
//   * upright (390 x 844, 360 x 640, 320 x 568): the buttons are one row under the picture (two rows on 320), every target at least 44 x 44 CSS px with at least 6 px between, and the picture's box is
//     the size that it has with the buttons off (the row takes no room from it);
//   * All ants picks the ants (the count is the game's own), None clears, Home picks the hill, Next picks one ant and moves the view, Stop leaves the selection as it is, None is dimmed with nothing picked and
//     then sends nothing, More opens the list and a choice sends its key (Quick help opens the game's dialog, and the buttons are hidden while it is up and come back when it closes);
//   * a tap on a button is no tap on the game: the touch model's counters, the pointer and what is picked are as before when a button that changes nothing is tapped;
//   * the settings panel hides the buttons while it is up (a tap on the gear, then Resume); the Touch buttons row turns them off and on, remembered under ants.touchkeys;
//   * sideways (844 x 390, 740 x 360, 667 x 375) in the page: one column in the page's free margin left of the stage, the picture exactly the size it has with the buttons off; in fullscreen (the
//     browser's, and the page's own where there is no Fullscreen API) a rail at the left edge inside the stage, the box beside it with the shape of the game's canvas to the pixel; turning upright
//     moves the buttons to the row and back;
//   * the picture never moves or changes size when the buttons hide (a dialog, the settings panel, the chat field) and come back, upright and sideways, in the page and in fullscreen;
//   * a mouse over the rail never scrolls the map; a tap that closes the More list is not a tap of the game; a button with Control held on the keyboard is the letter alone;
//   * a mouse as the primary pointer (a computer): no buttons.
// The engines: Chromium; WebKit and Firefox too when Playwright has them (TK_ENGINES=chromium,webkit,firefox; one that is not installed is said and left out).
// usage: ANTS_WEB_URL=http://127.0.0.1:19980/ node web_touch_keys_browser_check.js [--shots DIR]     (exit 0: every check holds; 1: one fails; 3: no browser or no page to run it in)
'use strict';
const fs = require('fs'), path = require('path');
let pw;
for (const where of ['playwright', '/opt/node-tools/node_modules/playwright', process.env.PLAYWRIGHT_MODULE || '']) {
    if (!where) continue;
    try { pw = require(where); if (pw) break; } catch (e) { /* try the next place */ }
}
const base = (process.env.ANTS_WEB_URL || '').replace(/\/?$/, '/');
if (!pw || !base || base === '/') { console.log('SKIP: no Playwright, or ANTS_WEB_URL names no page'); process.exit(3); }
const shotsAt = process.argv.indexOf('--shots') > 0 ? process.argv[process.argv.indexOf('--shots') + 1] : '';
if (shotsAt) fs.mkdirSync(shotsAt, { recursive: true });
let failed = 0, checks = 0;
function expect(label, got, want) { checks++; if (JSON.stringify(got) !== JSON.stringify(want)) { failed++; console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); } }
function ok(label, cond, detail) { checks++; if (!cond) { failed++; console.log('FAIL ' + label + (detail === undefined ? '' : ': ' + JSON.stringify(detail))); } }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function waitFor(page, expr, ms) { const end = Date.now() + ms; while (Date.now() < end) { try { if (await page.evaluate(expr)) return true; } catch (e) { /* the page is still loading */ } await sleep(250); } return false; }

const PHONE_UA = 'Mozilla/5.0 (iPhone; CPU iPhone OS 16_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.0 Mobile/15E148 Safari/604.1';
async function open(browser, w, h, o) {
    o = o || {};
    const context = await browser.newContext(o.mouse ? { viewport: { width: w, height: h }, deviceScaleFactor: 2 } : { viewport: { width: w, height: h }, deviceScaleFactor: 2, hasTouch: true, isMobile: true, userAgent: PHONE_UA });
    const page = await context.newPage();
    if (o.noFullscreenApi) await page.addInitScript(() => { Element.prototype.requestFullscreen = undefined; Element.prototype.webkitRequestFullscreen = undefined; });
    page.on('pageerror', (e) => { failed++; console.log('FAIL the page threw: ' + e.message); });
    await page.goto(base + 'play.html?map=' + (o.map || 'tiny') + '&bots=easy,none,none', { waitUntil: 'load' });
    ok('the game gets ready', await waitFor(page, "typeof Module !== 'undefined' && Module._ants_probe && isReadyToPlay", 90000));
    await sleep(1500);
    for (let i = 0; i < 2; i++) { await page.keyboard.press('Enter'); await sleep(1200); }
    ok('a match runs', await waitFor(page, 'Module._ants_match_running() === 1 && Module._ants_menu_get(101) === 0', 40000));
    await sleep(1000);
    return { context, page };
}
const probes = (page) => page.evaluate(() => ({ sel: Module._ants_probe(50), taps: Module._ants_probe(30), holds: Module._ants_probe(31), drags: Module._ants_probe(32), two: Module._ants_probe(33), fingers: Module._ants_probe(34), px: Module._ants_probe(0), py: Module._ants_probe(1), cx: Module._ants_probe(3), cy: Module._ants_probe(4), dialog: Module._ants_probe(5) }));
const ants = (sel) => sel % 1000 % 100000 % 1000;
const hill = (sel) => Math.floor(sel % 100000 / 1000) > 0;
const rect = (page, sel) => page.evaluate((s) => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.x, y: r.y, w: r.width, h: r.height, shown: !e.hidden && getComputedStyle(e).display !== 'none' && getComputedStyle(e).visibility !== 'hidden' && r.width > 0 }; }, sel);
async function tap(page, key) { const r = await rect(page, '#touch-keys [data-key=' + key + ']'); await page.touchscreen.tap(r.x + r.w / 2, r.y + r.h / 2); await sleep(700); }
const bar = (page) => rect(page, '#touch-keys');
const classes = (page) => page.evaluate(() => document.getElementById('game-stage').className);
const boxSize = (page) => page.evaluate(() => { const r = document.getElementById('game-container').getBoundingClientRect(); return [Math.round(r.width * 10) / 10, Math.round(r.height * 10) / 10]; });
const setting = (page, on) => page.evaluate((x) => { antsTouchKeys.set(x); return 1; }, on);                 // (the page's own object, as the settings menu's row calls it)

async function targets(page, label, count) {
    const list = await page.evaluate(() => [].map.call(document.querySelectorAll('#touch-keys > .tk-btn'), (b) => { const r = b.getBoundingClientRect(); return [r.x, r.y, r.width, r.height]; }));
    expect(label + ': ' + (count || 6) + ' buttons in the bar', list.length, count || 6);
    ok(label + ': every target is at least 44 x 44 CSS px', list.every((r) => r[2] >= 43.9 && r[3] >= 43.9), list);
    let gap = 1e9;
    for (let i = 0; i < list.length; i++) for (let j = i + 1; j < list.length; j++) {
        const a = list[i], b = list[j];
        const dx = Math.max(b[0] - (a[0] + a[2]), a[0] - (b[0] + b[2])), dy = Math.max(b[1] - (a[1] + a[3]), a[1] - (b[1] + b[3]));
        gap = Math.min(gap, Math.max(dx, dy));
    }
    ok(label + ': at least 6 px between targets (' + gap.toFixed(1) + ')', gap >= 5.9, gap);
    return list;
}
async function shot(page, name) { if (shotsAt) await page.screenshot({ path: path.join(shotsAt, name) }); }

async function run(engine) {
    let browser;
    try { browser = await pw[engine].launch(engine === 'chromium' ? { args: ['--no-sandbox'] } : {}); } catch (e) { console.log('NOTE ' + engine + ' is not installed here: not tried'); return; }
    const tag = engine + ': ';
    try {
        // ---- upright, the whole of what the buttons do
        let { context, page } = await open(browser, 390, 844);
        ok(tag + 'the buttons are shown in a match on a touch screen', (await bar(page)).shown);
        expect(tag + 'upright in the page: a row', (await classes(page)).split(' ').filter((c) => /^tk-/.test(c)).sort(), ['tk-on', 'tk-page', 'tk-up']);
        const stage = await rect(page, '#game-stage'), row = await bar(page), box = await rect(page, '#game-container');
        ok(tag + 'the row is directly under the picture, inside the page\'s width', row.y > box.y + box.h && row.y - (box.y + box.h) < 30 && row.x >= 0 && row.x + row.w <= 390, [row, box]);
        await targets(page, tag + '390 x 844');
        const withKeys = await boxSize(page);
        await setting(page, false); await sleep(600);
        ok(tag + 'the setting off hides the row', !(await bar(page)).shown);
        const without = await boxSize(page);
        expect(tag + 'the row takes no room from the picture (390 x 844)', withKeys, without);
        await setting(page, true); await sleep(600);
        ok(tag + 'and the row is back', (await bar(page)).shown);
        let p0 = await probes(page);
        expect(tag + 'nothing is picked at the start', p0.sel, 0);
        ok(tag + 'None is dimmed with nothing picked', await page.evaluate(() => document.querySelector('[data-key=none]').getAttribute('aria-disabled') === 'true'));
        await tap(page, 'none');
        let p1 = await probes(page);
        expect(tag + '...and a tap on it sends nothing (the settings menu does not open, nothing changes)', [p1.sel, await page.evaluate(() => !!document.getElementById('game-menu'))], [0, false]);
        await tap(page, 'all');
        p1 = await probes(page);
        ok(tag + 'All ants picks the ants (the game counts ' + ants(p1.sel) + ')', ants(p1.sel) >= 1, p1);
        const all = ants(p1.sel);
        await sleep(300);
        ok(tag + 'None is live now', await page.evaluate(() => document.querySelector('[data-key=none]').getAttribute('aria-disabled') === 'false'));
        await tap(page, 'stop');
        p1 = await probes(page);
        expect(tag + 'Stop leaves the selection as it was', ants(p1.sel), all);
        await tap(page, 'none');
        p1 = await probes(page);
        expect(tag + 'None clears', p1.sel, 0);
        await tap(page, 'home');
        p1 = await probes(page);
        ok(tag + 'Home picks the hill', hill(p1.sel) && ants(p1.sel) === 0, p1);
        await tap(page, 'none');
        const before = await probes(page);
        await tap(page, 'next');
        p1 = await probes(page);
        ok(tag + 'Next picks one ant', ants(p1.sel) === 1, p1);
        // a tap on a button is no tap on the game
        await tap(page, 'none');
        const quiet = await probes(page);
        await tap(page, 'none');
        const after = await probes(page);
        expect(tag + 'a tap on a button is no tap, hold, drag or touch of the game, and the pointer did not move', [after.taps, after.holds, after.drags, after.two, after.fingers, after.px, after.py], [quiet.taps, quiet.holds, quiet.drags, quiet.two, quiet.fingers, quiet.px, quiet.py]);
        expect(tag + '...none of the five taps above was a tap on the map', [after.taps, after.holds, after.drags], [0, 0, 0]);
        // More
        const rowBox = await bar(page);
        await tap(page, 'all');
        const moreBtn = await rect(page, '.tk-morebtn');
        await page.touchscreen.tap(moreBtn.x + moreBtn.w / 2, moreBtn.y + moreBtn.h / 2); await sleep(500);
        ok(tag + 'More opens a list of seven', await page.evaluate(() => { const m = document.querySelector('.tk-more'); return !m.hidden && getComputedStyle(m).display !== 'none' && m.children.length === 7; }));
        const items = await page.evaluate(() => [].map.call(document.querySelectorAll('.tk-more .tk-btn'), (b) => { const r = b.getBoundingClientRect(); return [r.width, r.height]; }));
        ok(tag + 'every item of the list is at least 44 x 44', items.every((r) => r[0] >= 43.9 && r[1] >= 43.9), items);
        await shot(page, 'upright_more.png');
        const sel = await probes(page);
        const help = await rect(page, '.tk-more [data-key=help]');
        await page.touchscreen.tap(help.x + help.w / 2, help.y + help.h / 2); await sleep(900);
        const d = await probes(page);
        expect(tag + 'Quick help sends F1: the game\'s dialog is open and the selection is as it was', [d.dialog, d.sel], [1, sel.sel]);
        ok(tag + '...the list closed after the choice and the buttons are hidden while the dialog is up', !(await bar(page)).shown);
        await page.keyboard.press('Escape'); await sleep(900);
        expect(tag + '...Esc closes the dialog and the buttons are back', [(await probes(page)).dialog, (await bar(page)).shown], [0, true]);
        // the settings panel
        const gear = await rect(page, '#settings-btn');
        await page.touchscreen.tap(gear.x + gear.w / 2, gear.y + gear.h / 2); await sleep(1000);
        ok(tag + 'the settings panel is open and the buttons are hidden', await page.evaluate(() => !!document.getElementById('game-menu')) && !(await bar(page)).shown);
        await page.evaluate(() => document.getElementById('gm-tab-video').click()); await sleep(400);
        const rowName = await page.evaluate(() => [].map.call(document.querySelectorAll('.gm-row .gm-name'), (n) => n.textContent));
        ok(tag + 'the Video section has a Touch buttons row', rowName.indexOf('Touch buttons') >= 0, rowName);
        await shot(page, 'settings_row.png');
        await page.evaluate(() => { const r = document.querySelector('.gm-k-touchkeys'); r.querySelectorAll('.gm-opt')[1].click(); });
        expect(tag + 'Off is remembered under ants.touchkeys', await page.evaluate(() => localStorage.getItem('ants.touchkeys')), 'off');
        await page.evaluate(() => document.querySelector('.gm-resume').click()); await sleep(900);
        ok(tag + 'closed: still no buttons (the setting is off)', !(await bar(page)).shown);
        await page.evaluate(() => document.getElementById('settings-btn').click()); await sleep(900);
        await page.evaluate(() => { document.getElementById('gm-tab-video').click(); document.querySelector('.gm-k-touchkeys').querySelectorAll('.gm-opt')[0].click(); });
        expect(tag + 'On is remembered', await page.evaluate(() => localStorage.getItem('ants.touchkeys')), 'on');
        await page.evaluate(() => document.querySelector('.gm-resume').click()); await sleep(900);
        ok(tag + 'closed: the buttons are back', (await bar(page)).shown);
        // a held Esc with nothing picked opens the settings menu (a keyboard's Esc); None never gets there
        await shot(page, 'upright.png');
        await context.close();

        // ---- Next moves the view: on the big map, the view is moved away with two fingers (Chromium's touch protocol), and Next brings an ant into it
        if (engine === 'chromium') {
            ({ context, page } = await open(browser, 390, 844, { map: 'treasure' }));
            const cdp = await context.newCDPSession(page);
            const r = await page.evaluate(() => { const b = document.getElementById('canvas').getBoundingClientRect(); return [b.x, b.y, b.width, b.height]; });
            const pts = (dx, dy) => [{ x: r[0] + r[2] * 0.3 + dx, y: r[1] + r[3] * 0.5 + dy, id: 1 }, { x: r[0] + r[2] * 0.3 + 30 + dx, y: r[1] + r[3] * 0.5 + dy, id: 2 }];
            const start = await probes(page);
            await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: pts(0, 0) });
            for (let i = 1; i <= 12; i++) { await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: pts(i * 10, i * 6) }); await sleep(30); }
            await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
            await sleep(800);
            const panned = await probes(page);
            ok(tag + 'two fingers moved the view away', panned.cx !== start.cx || panned.cy !== start.cy, [start, panned]);
            await tap(page, 'next');
            const moved = await probes(page);
            ok(tag + 'Next picks one ant and moves the view to it', ants(moved.sel) === 1 && (moved.cx !== panned.cx || moved.cy !== panned.cy), [panned, moved]);
            await context.close();
        }

        // ---- other widths
        for (const [w, h] of [[360, 640], [320, 568]]) {
            ({ context, page } = await open(browser, w, h));
            await targets(page, tag + w + ' x ' + h);
            const on = await boxSize(page), r = await bar(page), b = await rect(page, '#game-container');
            ok(tag + w + ' x ' + h + ': the row is under the picture and inside the page', r.y >= b.y + b.h && r.x >= 0 && r.x + r.w <= w + 0.5, [r, b]);
            await setting(page, false); await sleep(600);
            expect(tag + w + ' x ' + h + ': the row takes no room from the picture', on, await boxSize(page));
            await setting(page, true); await sleep(600);
            if (w === 320) await shot(page, 'upright_320.png');
            await context.close();
        }

        // ---- sideways in the page at three phone sizes: the picture is the size it has with the buttons off
        for (const [w, h] of [[844, 390], [740, 360], [667, 375]]) {
            ({ context, page } = await open(browser, w, h));
            const cls = (await classes(page)).split(' ');
            const r = await bar(page), st = await rect(page, '#game-stage');
            const count = await page.evaluate(() => document.querySelectorAll('#touch-keys > .tk-btn').length);
            ok(tag + w + ' x ' + h + ' in the page: one column in the margin (' + count + ' buttons), left of the stage, never two columns', cls.indexOf('tk-rim') >= 0 && cls.indexOf('tk-two') < 0 && r.x >= 0 && r.x + r.w <= st.x + 0.5 && r.w < 70, [cls, r, st]);
            await targets(page, tag + w + ' x ' + h + ' page', count);
            const on = await boxSize(page);
            await setting(page, false); await sleep(600);
            const off = await boxSize(page);
            expect(tag + w + ' x ' + h + ' in the page: the picture keeps its size (' + on.join(' x ') + ')', on, off);
            console.log('  size ' + w + ' x ' + h + ' page: with ' + on.join(' x ') + ', without ' + off.join(' x '));
            await context.close();
        }

        // ---- the picture never moves or changes size when the buttons hide and come back
        for (const [w, h, full] of [[390, 844, false], [844, 390, false], [844, 390, true], [390, 844, true]]) {
            ({ context, page } = await open(browser, w, h, { noFullscreenApi: full }));
            if (full) { const f = await rect(page, '#fullscreen-btn'); await page.touchscreen.tap(f.x + f.w / 2, f.y + f.h / 2); await sleep(1800); }
            const label = tag + w + ' x ' + h + (full ? ' fullscreen' : ' page');
            const place = () => page.evaluate(() => { const r = document.getElementById('game-container').getBoundingClientRect(); return [r.x, r.y, r.width, r.height].map((n) => Math.round(n * 100) / 100); });
            const home = await place();
            ok(label + ': the buttons are shown', (await bar(page)).shown);
            await page.keyboard.press('F1'); await sleep(900);
            ok(label + ': the quick help hides the buttons', !(await bar(page)).shown);
            expect(label + ': ...and the picture has not moved or changed size', await place(), home);
            await page.keyboard.press('Escape'); await sleep(900);
            expect(label + ': ...the buttons are back and the picture is where it was', [(await bar(page)).shown, await place()], [true, home]);
            await page.evaluate(() => document.getElementById('settings-btn') ? document.getElementById('settings-btn').click() : window.antsGameMenu && 0); await sleep(900);
            if (await page.evaluate(() => !!document.getElementById('game-menu'))) {
                ok(label + ': the settings panel hides the buttons', !(await bar(page)).shown);
                expect(label + ': ...and the picture has not moved or changed size', await place(), home);
                await page.evaluate(() => document.querySelector('.gm-resume').click()); await sleep(900);
                expect(label + ': ...closed: the buttons are back and the picture is where it was', [(await bar(page)).shown, await place()], [true, home]);
            }
            await page.evaluate(() => document.getElementById('chat-keys') && document.getElementById('chat-keys').focus()); await sleep(600);
            if (await page.evaluate(() => document.activeElement && document.activeElement.id === 'chat-keys')) {
                ok(label + ': the chat field hides the buttons', !(await bar(page)).shown);
                expect(label + ': ...and the picture has not moved or changed size', await place(), home);
                await page.evaluate(() => document.activeElement.blur()); await sleep(600);
                expect(label + ': ...blurred: the buttons are back and the picture is where it was', [(await bar(page)).shown, await place()], [true, home]);
            }
            await context.close();
        }

        // ---- a tap that closes the More list is not a tap of the game; a button with Control held is the letter alone
        ({ context, page } = await open(browser, 390, 844));
        {
            await tap(page, 'all');
            const q0 = await probes(page);
            const moreBtn2 = await rect(page, '.tk-morebtn');
            await page.touchscreen.tap(moreBtn2.x + moreBtn2.w / 2, moreBtn2.y + moreBtn2.h / 2); await sleep(500);
            ok(tag + 'More is open', await page.evaluate(() => antsTouchKeys.isOpen()));
            const cb = await rect(page, '#game-container');
            await page.touchscreen.tap(cb.x + cb.w * 0.5, cb.y + cb.h * 0.3); await sleep(700);
            const q1 = await probes(page);
            expect(tag + 'a tap on the map closes the list, and is no tap of the game (no tap, no hold, no drag, the same ants picked)', [await page.evaluate(() => antsTouchKeys.isOpen()), q1.taps, q1.holds, q1.drags, q1.sel], [false, q0.taps, q0.holds, q0.drags, q0.sel]);
            await tap(page, 'none');
            expect(tag + 'nothing is picked', (await probes(page)).sel, 0);
            await page.keyboard.down('Control');
            await tap(page, 'all');
            await page.keyboard.up('Control');
            ok(tag + 'Control held on the keyboard, All ants still picks the ants (the letter alone, with Control)', ants((await probes(page)).sel) >= 1, await probes(page));
            await tap(page, 'none');
            await tap(page, 'all');
            ok(tag + '...and without Control held it is a whole chord again', ants((await probes(page)).sel) >= 1);
        }
        await context.close();

        // ---- a mouse or a pen over the rail never scrolls the map (the big map, the view moved away from its edge)
        for (const full of [false, true]) {
            ({ context, page } = await open(browser, 844, 390, { map: 'treasure', noFullscreenApi: full }));
            if (full) { const f = await rect(page, '#fullscreen-btn'); await page.touchscreen.tap(f.x + f.w / 2, f.y + f.h / 2); await sleep(1800); }
            const cdp = await context.newCDPSession(page);
            const rr = await page.evaluate(() => { const b = document.getElementById('canvas').getBoundingClientRect(); return [b.x, b.y, b.width, b.height]; });
            const pts = (dx, dy) => [{ x: rr[0] + rr[2] * 0.5 + dx, y: rr[1] + rr[3] * 0.5 + dy, id: 1 }, { x: rr[0] + rr[2] * 0.5 + 30 + dx, y: rr[1] + rr[3] * 0.5 + dy, id: 2 }];
            await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: pts(0, 0) });
            for (let i = 1; i <= 12; i++) { await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: pts(i * 6, i * 4) }); await sleep(30); }
            await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
            await sleep(800);
            const s0 = await probes(page);
            const list = await page.evaluate(() => [].map.call(document.querySelectorAll('#touch-keys > .tk-btn'), (b) => { const r = b.getBoundingClientRect(); return [r.x, r.y, r.width, r.height]; }));
            const barBox = await bar(page);
            const spots = [[list[0][0] + list[0][2] / 2, list[0][1] + list[0][3] / 2], [list[0][0] + list[0][2] / 2, list[0][1] + list[0][3] + 3], [barBox.x + 2, barBox.y + barBox.h / 2]];
            for (const [x, y] of spots) { await page.mouse.move(x, y); await sleep(1300); }
            const s1 = await probes(page);
            expect(tag + (full ? 'fullscreen' : 'page') + ': a mouse resting on a button, in a gap and at the rail\'s edge does not scroll the map', [s1.cx, s1.cy], [s0.cx, s0.cy]);
            await context.close();
        }

        // ---- sideways: the rail, in the page and in fullscreen
        for (const noApi of [false, true]) {
            ({ context, page } = await open(browser, 844, 390, { noFullscreenApi: noApi }));
            const cls = (await classes(page)).split(' ');
            ok(tag + 'sideways in the page: a rail', cls.indexOf('tk-side') >= 0 && cls.indexOf('tk-up') < 0, cls);
            let r = await bar(page), b = await rect(page, '#game-container');
            ok(tag + '...outside the picture\'s box (the box starts where the rail ends)', r.x + r.w <= b.x + 0.5 && r.h > 0, [r, b]);
            await targets(page, tag + '844 x 390 page');
            ok(tag + 'sideways in the page: the rail stands in the free margin left of the stage (tk-rim), outside it, on the screen', cls.indexOf('tk-rim') >= 0 && r.x >= 0 && r.x + r.w <= (await rect(page, '#game-stage')).x + 0.5, [cls, r]);
            const sideOn = await boxSize(page);
            await setting(page, false); await sleep(600);
            expect(tag + '...and the picture is the size it has with the buttons off (nothing lost)', sideOn, await boxSize(page));
            await setting(page, true); await sleep(600);
            if (!noApi) await shot(page, 'sideways_page.png');
            const full = await rect(page, '#fullscreen-btn');
            await page.touchscreen.tap(full.x + full.w / 2, full.y + full.h / 2); await sleep(1800);
            const inFull = await page.evaluate(() => !!document.fullscreenElement || document.getElementById('game-stage').classList.contains('pseudo-fullscreen'));
            ok(tag + 'fullscreen is on (' + (noApi ? 'the page\'s own' : 'the browser\'s') + ')', inFull);
            r = await bar(page); b = await rect(page, '#game-container');
            const s = await rect(page, '#game-stage');
            ok(tag + 'fullscreen sideways: the rail is on the screen, at its left edge, the whole height', r.shown && r.x <= 0.5 && r.y <= 0.5 && Math.abs(r.h - 390) < 1, r);
            ok(tag + '...and the picture is beside it, inside the screen', b.x >= r.x + r.w - 0.5 && b.x + b.w <= 844.5 && b.y >= -0.5 && b.y + b.h <= 390.5, [r, b]);
            const shape = await page.evaluate(() => { const c = document.getElementById('canvas'), r = c.getBoundingClientRect(); return [Module._ants_probe(40), Module._ants_probe(41), r.width, r.height, c.width, c.height]; });
            ok(tag + '...with the shape of the game\'s canvas to the pixel (canvas ' + shape[4] + ' x ' + shape[5] + ' for a picture of ' + shape[0] + ' x ' + shape[1] + ')', shape[4] * shape[1] === shape[5] * shape[0] || Math.abs(shape[2] / shape[3] - shape[0] / shape[1]) < 0.01, shape);
            await targets(page, tag + '844 x 390 fullscreen');
            if (!noApi) await shot(page, 'sideways_fullscreen.png');
            // a tap on the rail is no tap on the game
            const q = await probes(page);
            await tap(page, 'home');
            ok(tag + 'a rail button works in fullscreen (Home picks the hill)', hill((await probes(page)).sel));
            const q2 = await probes(page);
            expect(tag + '...and is no tap of the game', [q2.taps, q2.holds, q2.drags, q2.fingers], [q.taps, q.holds, q.drags, q.fingers]);
            // turning upright (the window becomes taller than wide): the browser's fullscreen window cannot be resized by the test, so it is left first, and the page's own one is turned as it is
            if (!noApi) {
                await page.evaluate(() => document.exitFullscreen()); await sleep(1200);
                await page.setViewportSize({ width: 390, height: 844 }); await sleep(1200);
                const g = await rect(page, '#fullscreen-btn');
                await page.touchscreen.tap(g.x + g.w / 2, g.y + g.h / 2); await sleep(1800);
            } else {
                await page.setViewportSize({ width: 390, height: 844 }); await sleep(1500);
            }
            let c2 = (await classes(page)).split(' ');
            ok(tag + 'turned upright in fullscreen: the row at the bottom of the stage', c2.indexOf('tk-up') >= 0 && c2.indexOf('tk-side') < 0, c2);
            r = await bar(page);
            ok(tag + '...along the bottom of the screen', r.shown && Math.abs(r.y + r.h - 844) < 1.5 && r.x <= 0.5 && Math.abs(r.w - 390) < 1.5, r);
            await targets(page, tag + '390 x 844 fullscreen');
            await shot(page, noApi ? 'upright_fullscreen_pseudo.png' : 'upright_fullscreen.png');
            if (!noApi) { await page.evaluate(() => document.exitFullscreen()); await sleep(1200); }
            await page.setViewportSize({ width: 844, height: 390 }); await sleep(1500);
            c2 = (await classes(page)).split(' ');
            ok(tag + 'and back to the rail', c2.indexOf('tk-side') >= 0, c2);
            await context.close();
        }
        // turning in the page
        ({ context, page } = await open(browser, 844, 390));
        await page.setViewportSize({ width: 390, height: 844 }); await sleep(1500);
        ok(tag + 'turned upright in the page: the row', (await classes(page)).split(' ').indexOf('tk-up') >= 0);
        await context.close();

        // ---- a mouse as the primary pointer: no buttons
        ({ context, page } = await open(browser, 1280, 800, { mouse: true }));
        ok(tag + 'a computer (a mouse): no buttons', !(await bar(page)).shown);
        await context.close();
    } catch (e) {
        failed++;
        console.log('FAIL ' + tag + 'the check broke down: ' + (e && e.stack ? e.stack : e));
    }
    await browser.close();
}

(async () => {
    for (const engine of (process.env.TK_ENGINES || 'chromium').split(',')) { console.log('--- ' + engine); await run(engine.trim()); }
    console.log(checks + ' checks, ' + failed + ' failures');
    process.exit(failed ? 1 : 0);
})();
