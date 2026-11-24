// OPT-IN, not part of ./run_tests.sh: the private messages on a phone (docs/HOME_CHAT.md "Private messages and follows") in a REAL browser (Chromium through Playwright) against the real hub, behind the stand-in for the site's nginx
// of tests/scripts/web_home_hub_rig.js. Two accounts (hazel on a phone, 390 x 844 at pixel ratio 2, pebble in a wide window) and, for hazel:
//   0. the Messages tab of the bell (the list of conversations: the bell's number counts them) and a conversation opened from it;
//   1. the conversation is a sheet with a head that holds the person's name, a way back (to the Messages tab) and an X (every target is 44 px or more, all on the screen), and the field that sits at the bottom of it;
//   2. with a keyboard-sized window (390 x 430) the head is still on the screen, the field is on the screen and a field that has the focus is not under the bottom, and the messages have room above it;
//   3. a visual viewport that shrinks alone (the keyboard of Android Chrome and of iOS does that, and the window keeps its height): the sheet follows it, and the field is just above the keyboard;
//   4. the ways out: X closes the whole sheet and leaves the page as it was (no bell list), the arrow goes back to the bell's Messages tab, Escape, the phone's Back button, a pull down on the head, a press outside;
//   5. nothing is wider than the window, no page error.
//   --shots DIR takes the pictures (name: TAG_subject_size.png; --tag before|after says which files these are: run it once on the files of the release before, with `--web DIR`, once on these).
//   node tests/scripts/web_home_dm_phone_browser.js [--web DIR] [--shots DIR] [--tag after]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
// Needs `playwright` (npm), a Chromium that it can start and python3 with the hub's own folder (hub/); without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be made).
'use strict';
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the private messages on a phone were NOT run in a real browser'); process.exit(0); }
const { startStack, openPage, until, sleep, ROOT } = require('./web_home_hub_rig.js');
if (spawnSync('python3', ['-c', 'import hub'], { cwd: ROOT }).status !== 0) { console.log('SKIP: python3 cannot import the hub: the private messages on a phone were NOT run against the real hub'); process.exit(0); }

const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const TAG = arg('--tag') || 'after';
const PHONE = ['phone', 390, 844, 2, true], WIDE = ['wide', 1280, 800, 1, false];

let checks = 0;
let failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }

async function main() {
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the private messages on a phone were NOT run in a real browser'); return; }
    const errors = [], stack = await startStack(WEB);
    if (SHOTS) fs.mkdirSync(SHOTS, { recursive: true });
    try {
        const pebble = await openPage(browser, stack, 'pebble', WIDE, (e) => errors.push('pebble: ' + e), 'pebble: ');
        const hazel = await openPage(browser, stack, 'hazel', PHONE, (e) => errors.push('hazel: ' + e), 'hazel: ');
        const page = hazel.page;
        const typeAndSend = async (a, text) => { await a.page.fill('.pm-panel .pm-input', text); await a.page.click('.pm-panel .pm-send'); await sleep(300); };
        const rect = (sel) => page.evaluate((q) => { const e = document.querySelector(q); if (!e) return null; const r = e.getBoundingClientRect(); return { top: Math.round(r.top), bottom: Math.round(r.bottom), left: Math.round(r.left), right: Math.round(r.right), h: Math.round(r.height), w: Math.round(r.width) }; }, sel);
        const state = () => page.evaluate(() => ({ sheet: !!document.querySelector('.pm-shade'), bell: !!document.querySelector('.al-shade'), tab: (document.querySelector('.al-panel') || { getAttribute: () => null }).getAttribute('data-tab'), url: location.pathname + location.hash, active: document.activeElement && (document.activeElement.className || document.activeElement.tagName) }));
        const shot = async (name) => { if (SHOTS) { await sleep(250); await page.screenshot({ path: path.join(SHOTS, TAG + '_' + name + '.png') }); } };
        const messages = async () => { await page.evaluate(() => { document.querySelector('.hbar .al-bell').click(); }); await sleep(300); await page.evaluate(() => { const t = document.querySelector('.al-tab[data-tab="messages"]'); if (t) t.click(); }); };       // (the bell, then its Messages tab)
        const firstConversation = () => page.evaluate(() => document.querySelector('.al-conv').click());
        const tab = (name) => page.evaluate((n) => { const t = document.querySelector('.htab.t-' + n); if (t) t.click(); }, name);

        // pebble writes to hazel: a conversation with some messages in it (pebble's page opens it with /msg)
        await pebble.page.evaluate(() => { const s = window.__socks.filter((x) => /\/hub\/ws$/.test(x.url) && x.readyState === 1)[0]; s.send(JSON.stringify({ t: 'say', text: '/msg hazel' })); });
        await until('pebble has the conversation open', () => pebble.page.evaluate(() => !!document.querySelector('.pm-panel .pm-input')), 8000);
        for (const text of ['hey hazel, are you around?', 'I found a map that we have not played: Islands, four players.', 'Bring the scouts. They are faster there than you think.', 'Also: do not build the second nest before you have a bomber. Last time that cost you the game.', 'See you in the room?']) await typeAndSend(pebble, text);
        await until('hazel has the count on the bell', () => page.evaluate(() => { const n = document.querySelector('.hbar .al-bell .al-mark'); return !!n && Number(n.textContent.replace('+', '')) >= 1; }), 8000);
        await tab('chat'); await sleep(300);
        await page.evaluate(() => document.querySelectorAll('.al-toast').forEach((t) => { t.hidden = true; }));

        // ---- the list, then the conversation, in the window as it is --------------------------------------------------------------------------------------------------------------------------------
        await messages(); await sleep(400);
        const tabs = await page.evaluate(() => Array.from(document.querySelectorAll('.al-tab')).map((t) => t.textContent.replace(/\s+/g, ' ').trim() + ':' + t.getAttribute('data-on')));
        check('the bell has the two tabs, Messages chosen, with the count of what is not read', tabs.length === 2 && /^Alerts/.test(tabs[0]) && /^Messages5:true$/.test(tabs[1]), JSON.stringify(tabs));
        const conv = await page.evaluate(() => { const c = document.querySelector('.al-conv'); return c ? c.textContent.replace(/\s+/g, ' ').trim() : ''; });
        check('the conversation is in the list: the person, the start of the newest message, and the count', /pebble/.test(conv) && /See you in the room\?/.test(conv) && /5$/.test(conv), conv);
        const none = await page.evaluate(() => Array.from(document.querySelectorAll('.al-item')).filter((i) => /private message/.test(i.textContent)).length);
        check('the list holds the conversation and no row for the same message in the Alerts tab', none === 0);
        await shot('list_844');
        await firstConversation(); await sleep(500);
        let top = await rect('.pm-top'), x = await rect('.pm-top .pm-x'), back = await rect('.pm-top .pm-back'), field = await rect('.pm-input'), shade = await rect('.pm-shade');
        check('the sheet is exactly the window and its head is on the screen with the arrow and the X (44 px or more)', !!top && top.top >= 0 && !!x && x.h >= 43.5 && x.w >= 43.5 && !!back && back.h >= 43.5 && x.right <= 390 && !!shade && shade.h === 844, JSON.stringify([top, x, back, shade]));
        check('the field is on the screen, at the bottom of the sheet', !!field && field.bottom <= 844 && field.bottom > 700, JSON.stringify(field));
        check('the name of the person is in the head', (await page.evaluate(() => document.querySelector('.pm-top .pm-nm').textContent)) === 'pebble');
        await shot('thread_844');

        // ---- a keyboard-sized window: the whole window is 390 x 430 ---------------------------------------------------------------------------------------------------------------------------------
        await page.setViewportSize({ width: 390, height: 430 }); await sleep(300);
        await page.focus('.pm-input'); await sleep(700);
        top = await rect('.pm-top'); field = await rect('.pm-input'); shade = await rect('.pm-shade');
        let log = await rect('.pm-log');
        check('with a window of 430 px the head is on the screen and the sheet is the window', !!top && top.top >= 0 && !!shade && shade.top === 0 && shade.h === 430, JSON.stringify([top, shade]));
        check('...the field is on the screen, not under the bottom', !!field && field.top >= 0 && field.bottom <= 430, JSON.stringify(field));
        check('...and the messages have room above it (the log is 120 px high or more)', !!log && log.h >= 120 && log.bottom <= field.top, JSON.stringify([log, field]));
        await shot('thread_430_typing');
        await page.setViewportSize({ width: 390, height: 844 }); await sleep(300);
        await page.evaluate(() => document.activeElement && document.activeElement.blur()); await sleep(500);

        // ---- the visual viewport shrinks alone: the window keeps its 844 px (a keyboard of Android Chrome or iOS), a grey keyboard is drawn in the part that it covers ---------------------------------------------------
        await page.evaluate(() => document.querySelector('.pm-x').click()); await sleep(300);
        await page.evaluate(() => {
            const listeners = {}, vv = { height: window.innerHeight, offsetTop: 0, scale: 1, addEventListener(t, f) { (listeners[t] = listeners[t] || []).push(f); }, removeEventListener(t, f) { listeners[t] = (listeners[t] || []).filter((g) => g !== f); } };
            Object.defineProperty(window, 'visualViewport', { value: vv, configurable: true });
            window.__keyboard = (on) => {
                vv.height = on ? window.innerHeight - 414 : window.innerHeight; vv.offsetTop = 0;
                (listeners.resize || []).slice().forEach((f) => f({}));
                let k = document.getElementById('mock-keyboard');
                if (on && !k) { k = document.createElement('div'); k.id = 'mock-keyboard'; k.style.cssText = 'position:fixed;left:0;right:0;bottom:0;height:414px;background:#d2d5db;z-index:99999;border-top:1px solid #9aa0a6'; document.body.appendChild(k); }
                if (!on && k) k.remove();
            };
        });
        await messages(); await sleep(400);
        await firstConversation(); await sleep(400);
        await page.focus('.pm-input');
        await page.evaluate(() => window.__keyboard(true)); await sleep(700);
        top = await rect('.pm-top'); field = await rect('.pm-input'); shade = await rect('.pm-shade'); log = await rect('.pm-log');
        check('a visual viewport of 430 px in a window of 844: the sheet is 430 px high and the head is on the screen', !!shade && shade.top === 0 && shade.h === 430 && !!top && top.top >= 0, JSON.stringify([shade, top]));
        check('...the field is just above the keyboard, not under it', !!field && field.bottom <= 430 && field.bottom > 360, JSON.stringify(field));
        check('...and the messages fill the room above it', !!log && log.h >= 120 && log.bottom <= field.top, JSON.stringify(log));
        await shot('thread_keyboard_mock');
        await page.evaluate(() => window.__keyboard(false)); await sleep(500);
        shade = await rect('.pm-shade');
        check('the keyboard goes: the sheet is the window again', !!shade && shade.h === 844, JSON.stringify(shade));
        await page.evaluate(() => document.activeElement && document.activeElement.blur()); await sleep(400);

        // ---- the ways out -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        const closedAtThePage = async (what) => { await sleep(350); const s = await state(); check(what + ': the whole sheet is gone, no bell list opened, the page is where it was', !s.sheet && !s.bell && s.url === '/', JSON.stringify(s)); };
        await page.evaluate(() => document.querySelector('.pm-x').click()); await closedAtThePage('X');
        await messages(); await sleep(300); await firstConversation(); await sleep(300);
        await page.evaluate(() => document.querySelector('.pm-back').click()); await sleep(400);
        let s = await state();
        check('the arrow goes back to the bell on its Messages tab (the conversation is gone)', !s.sheet && s.bell && s.tab === 'messages', JSON.stringify(s));
        await page.evaluate(() => document.querySelector('.al-x').click()); await closedAtThePage('X on the bell');
        await messages(); await sleep(300); await firstConversation(); await sleep(300);
        await page.keyboard.press('Escape'); await closedAtThePage('Escape');
        await messages(); await sleep(300); await firstConversation(); await sleep(300);
        await page.goBack(); await closedAtThePage('the phone\'s Back button');
        check('...and the page has not left the front page (it is still the same document)', (await page.evaluate(() => !!document.querySelector('#gchat') && location.pathname === '/')));
        await messages(); await sleep(300); await firstConversation(); await sleep(300);
        await page.evaluate(() => {
            const g = document.querySelector('.pm-grab'), fire = (t, y) => g.dispatchEvent(new PointerEvent(t, { bubbles: true, pointerType: 'touch', pointerId: 5, clientY: y, button: 0 }));
            fire('pointerdown', 40); fire('pointermove', 90); fire('pointermove', 200); fire('pointerup', 200);
        });
        await closedAtThePage('a pull down on the head');
        await messages(); await sleep(300); await firstConversation(); await sleep(300);
        await page.evaluate(() => document.querySelector('.pm-shade').dispatchEvent(new MouseEvent('click', { bubbles: true }))); await closedAtThePage('a press outside');
        await messages(); await sleep(300); await firstConversation(); await sleep(300);
        await page.evaluate(() => document.querySelector('.pm-x').click()); await sleep(500);
        check('a sheet that was closed by X leaves no entry of its own in the history', await page.evaluate(() => !(history.state && history.state.pm)));

        // ---- the report card on a phone, and a window that is too small -----------------------------------------------------------------------------------------------------------------------------------
        const wide = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
        check('nothing is wider than the window', wide <= 1, wide);
    } catch (e) {
        console.log('FAIL the check itself broke: ' + (e && e.stack || e));
        failures++;
    } finally {
        stack.stop();
        for (const c of browser.contexts()) await c.close().catch(() => {});
    }
    check('no page error', errors.length === 0, errors.join(' | '));
    await browser.close();
}

main().then(() => { console.log('the private messages on a phone in a browser against the real hub: ' + checks + ' checks, ' + failures + ' failed'); process.exit(failures === 0 ? 0 : 1); }, (e) => { console.log('FAIL ' + (e && e.stack || e)); process.exit(1); });
