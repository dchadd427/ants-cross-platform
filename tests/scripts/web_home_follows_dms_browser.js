// OPT-IN, not part of ./run_tests.sh: follows and private messages (docs/HUB_PEOPLE.md "Follows", docs/HUB_MESSAGES.md; docs/HOME_CHAT.md "Private messages and follows") in a REAL browser (Chromium through Playwright) against the REAL
// hub (`python3 -m hub`, with its test login), behind a small stand-in for the site's nginx that passes /hub/ws and tells the hub who is at the other end (the hub's test login believes `X-Hub-Test-Login: <id>:<name>`, which the
// stand-in takes from a cookie). Four accounts (hazel, pebble, fern, juniper) each have a page. At 1920 x 1080, 1366 x 768, 1280 x 800 and a phone (390 x 844, pixel ratio 2) it checks, for hazel:
//   1. the person's sheet of a name in the players list (Message, Follow, Ignore, Report, and the dim "Open a room first"): every target is 44 px or more on a touch screen, it lies inside the window;
//   2. Follow: the Following tab (count, the person under what they do, the star in Everyone), the hub's own answer; a person who is not here under "Not here"; the word that nobody is told;
//   3. a public room that a person she follows opens (the page of fern says it to the real hub): the toast with Join, the bell's row, and no frame of fern's holds anything of it;
//   4. a private message: the Messages button (in the chat's head, with its count), the panel (a sheet from the bottom on a phone), the conversation, Send (a message is drawn only when the hub's echo comes), the other page's toast, count and
//      bell row, the read that takes them away;
//   5. Block: the blocked person's message is not shown to hazel (no toast, no count), and the sender sees an ordinary send; Report: the card, and the hub's copy of the message (read with `python3 -m hub.admin reports`);
//   6. nothing is wider than the window, nothing sticks out, no page error.
//   --shots DIR takes the pictures of the build (the names of the approved set: 01_menu_follow_desktop.png ... 14_dm_report_phone.png).
//   node tests/scripts/web_home_follows_dms_browser.js [--web DIR] [--shots DIR]      (CHROME=/path/to/chromium names a browser; as root it runs with --no-sandbox)
// Needs `playwright` (npm), a Chromium that it can start and python3 with the hub's own folder (hub/); without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be made), 1 a check failed.
'use strict';
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

let playwright = null;
try { playwright = require('playwright'); } catch (e) { /* not installed */ }
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): follows and private messages were NOT run in a real browser'); process.exit(0); }
const ROOT = path.resolve(__dirname, '..', '..');
const python = spawnSync('python3', ['-c', 'import hub'], { cwd: ROOT });
if (python.status !== 0) { console.log('SKIP: python3 cannot import the hub: follows and private messages were NOT run against the real hub'); process.exit(0); }

const arg = (name) => { const i = process.argv.indexOf(name); return i === -1 ? null : process.argv[i + 1]; };
const WEB = path.resolve(arg('--web') || path.join(ROOT, 'web'));
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const SIZES = [['1920', 1920, 1080, 1, false], ['1366', 1366, 768, 1, false], ['1280', 1280, 800, 1, false], ['phone', 390, 844, 2, true]];
const { startStack: startStackIn, openPage, until, sleep } = require('./web_home_hub_rig.js');
const startStack = () => startStackIn(WEB);

let checks = 0;
let failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
async function main() {
    const launch = { args: process.getuid && process.getuid() === 0 ? ['--no-sandbox'] : [] };
    if (process.env.CHROME) launch.executablePath = process.env.CHROME;
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): follows and private messages were NOT run in a real browser'); return; }
    const errors = [];
    if (SHOTS) fs.mkdirSync(SHOTS, { recursive: true });

    for (const size of SIZES) {                              // every size has a hub of its own (a new database: nobody follows anybody, nobody has written, no limit is used up)
        const stack = await startStack();
        const tag = size[0] + ': ';
        const shoots = SHOTS && (size[0] === '1280' || size[4]);
        // ---- the pieces ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        async function open(account, sz) {                   // one page of an account, Home, the dock live; every WebSocket of the page is kept in window.__socks (so that a page can say what a page of a host says)
            return openPage(browser, stack, account, sz || SIZES[2], (e) => errors.push(tag + account + ': ' + e), tag + account + ': ');
        }
        const view = async (a, name) => { if (a.phone) { await a.page.evaluate((n) => { const t = document.querySelector('.htab.t-' + n); if (t) t.click(); }, name); await sleep(250); } };
        const sendFrame = (page, frame) => page.evaluate((f) => { const sock = window.__socks.filter((x) => /\/hub\/ws$/.test(x.url) && x.readyState === 1)[0]; sock.send(JSON.stringify(f)); }, frame);
        const rowOf = async (a, name) => { await view(a, 'players'); return a.page.locator('.pl-list .pl-card[data-i]').filter({ has: a.page.locator('.pl-nm', { hasText: new RegExp('^' + name + '$') }) }).first(); };
        async function openSheet(a, name) {                  // a right click on a person of the list (a long press on a phone opens the same sheet: here the page is told what the press ends in)
            const row = await rowOf(a, name);
            if (a.phone) await a.page.evaluate((n) => { const r = Array.from(document.querySelectorAll('.pl-card[data-i]')).filter((c) => c.querySelector('.pl-nm').textContent === n)[0]; r.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true })); }, name);
            else await row.click({ button: 'right' });
            await a.page.waitForSelector('.gm-shade .gm-box', { timeout: 5000 });
            await sleep(200);
        }
        const sheetButtons = (a) => a.page.evaluate(() => Array.from(document.querySelectorAll('.gm-box button')).map((b) => ({ text: (b.getAttribute('aria-label') || b.textContent).replace(/\s+/g, ' ').trim(), h: b.getBoundingClientRect().height })));
        const press = async (a, text) => { await a.page.evaluate((t) => { const b = Array.from(document.querySelectorAll('.gm-box button, .pm-panel button, .pm-card button, .pm-shade button, .al-panel button, .al-toast button, .pm-card-shade button')).filter((x) => (x.getAttribute('aria-label') || x.textContent).replace(/\s+/g, ' ').trim().indexOf(t) === 0)[0]; if (!b) throw new Error('no button ' + t); b.click(); }, text); await sleep(300); };
        const toasts = (a) => a.page.evaluate(() => Array.from(document.querySelectorAll('.al-toast')).filter((t) => !t.hidden).map((t) => t.textContent.replace(/\s+/g, ' ').trim()).join(' | '));
        const toastIs = (a, re, ms) => until(tag + a.name + ': a toast that says ' + re, async () => re.test(await toasts(a)), ms || 6000);
        const count = (a) => a.page.evaluate(() => { const n = document.querySelector('#gchat .pm-n'); return n && !n.hidden ? Number(n.textContent) : 0; });
        const bell = (a) => a.page.evaluate(() => { const m = document.querySelector('.hbar .al-bell .al-mark'); return m ? Number(m.textContent.replace('+', '')) : 0; });
        const tabOf = async (a, name) => { await view(a, 'players'); await a.page.evaluate((n) => document.querySelector('.pl-tab[data-tab="' + n + '"]').click(), name); await sleep(250); };
        async function shot(a, name) {
            const bad = await a.page.evaluate(() => {
                const out = [], w = window.innerWidth;
                if (document.documentElement.scrollWidth > w + 1) out.push('the page scrolls sideways');
                document.querySelectorAll('.pm-panel, .gm-box, .al-panel, .pm-card, .al-toast, .pm-btn, .pl-tabs').forEach((e) => { const r = e.getBoundingClientRect(); if (r.width && (r.left < -1 || r.right > w + 1 || r.bottom > window.innerHeight + 1)) out.push(e.className + ' sticks out ' + Math.round(r.left) + ',' + Math.round(r.right) + ',' + Math.round(r.bottom)); });
                return out;
            });
            bad.forEach((b) => check(tag + name + ': ' + b, false));
            if (shoots) { if (!/^(05|13)_/.test(name)) await a.page.evaluate(() => document.querySelectorAll('.al-toast').forEach((t) => { t.hidden = true; })); await sleep(200); await a.page.screenshot({ path: path.join(SHOTS, name + (a.phone ? '_phone' : '_desktop') + '.png') }); }
        }
        const pm = (a) => a.page.evaluate(() => { const p = document.querySelector('.pm-panel'); if (!p) return null; const r = p.getBoundingClientRect(); return { top: r.top, bottom: r.bottom, left: r.left, right: r.right, w: window.innerWidth, h: window.innerHeight, view: p.getAttribute('data-view'), sheet: !!document.querySelector('.pm-shade.phone') }; });
        const targetsOf = (a, sel) => a.page.evaluate((q) => Array.from(document.querySelectorAll(q)).filter((b) => b.getClientRects().length).map((b) => ({ text: (b.getAttribute('aria-label') || b.textContent).replace(/\s+/g, ' ').trim().slice(0, 30), h: b.getBoundingClientRect().height, w: b.getBoundingClientRect().width })), sel);
        const typeAndSend = async (a, text) => { await a.page.fill('.pm-panel .pm-input', text); await a.page.click('.pm-panel .pm-send'); await sleep(300); };
        const bubbles = (a) => a.page.evaluate(() => Array.from(document.querySelectorAll('.pm-panel .pm-msg')).map((m) => (m.classList.contains('me') ? 'me:' : 'them:') + m.querySelector('.pm-mt').textContent));
        const closePanel = async (a) => { await a.page.evaluate(() => { const x = document.querySelector('.pm-panel .pm-x'); if (x) x.click(); }); await sleep(250); };
        const admin = (...args) => spawnSync('python3', ['-m', 'hub.admin', '--db', stack.db, ...args], { cwd: ROOT, encoding: 'utf8' });

        try {
            const pebble = await open('pebble'), fern = await open('fern'), juniper = await open('juniper');
            const hazel = await open('hazel', size);
            hazel.name = 'hazel';
            await until(tag + 'the list has the others', () => hazel.page.evaluate(() => document.querySelectorAll('.pl-card[data-i]').length >= 3), 10000);

            // ---- 1. the sheet of a person ------------------------------------------------------------------------------------------------------------------------------------------------------------------
            await openSheet(hazel, 'pebble');
            let buttons = await sheetButtons(hazel);
            check(tag + 'the sheet of a person, no room held: Invite, Message, Follow, Ignore, Report, Cancel', buttons.map((b) => b.text).join('|') === 'Invite to my room|Message|Follow|Ignore|Report|Cancel', buttons.map((b) => b.text).join('|'));
            check(tag + 'the invitation says why it is dim', (await hazel.page.evaluate(() => document.querySelector('.gm-box').textContent)).indexOf('Open a room first') !== -1);
            check(tag + 'every row of the sheet is a target of 44 px or more', !hazel.phone || buttons.every((b) => b.h >= 43.5), JSON.stringify(buttons.filter((b) => b.h < 43.5)));
            await shot(hazel, '01_menu_follow');
            await press(hazel, 'Cancel');
            await tabOf(hazel, 'following');
            check(tag + 'Following with nobody in it says so and how to follow', await hazel.page.evaluate(() => /You follow nobody yet/.test(document.querySelector('.pl-nt').textContent) && /Press a name/.test(document.querySelector('.pl-none').textContent) && /Nobody is told/.test(document.querySelector('.pl-keep').textContent)));
            await shot(hazel, '03c_follows_empty');
            await tabOf(hazel, 'all');
            await openSheet(hazel, 'pebble');
            await press(hazel, 'Follow');
            await toastIs(hazel, /Done\. You follow pebble/);

            // ---- 2. the Following tab ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
            await view(hazel, 'players');
            await until(tag + 'the star and the count', () => hazel.page.evaluate(() => document.querySelectorAll('.pl-card .pl-star').length === 1 && /Following\s*1/.test(document.querySelector('.pl-tab[data-tab="following"]').textContent)), 6000);
            check(tag + 'Everyone: a star after pebble and nobody else', await hazel.page.evaluate(() => Array.from(document.querySelectorAll('.pl-card')).filter((c) => c.querySelector('.pl-star')).map((c) => c.querySelector('.pl-nm').textContent).join() === 'pebble'));
            check(tag + 'the two tabs are 44 px or more', (await targetsOf(hazel, '.pl-tab')).every((t) => t.h >= 43.5));
            await shot(hazel, '03b_follows_stars');
            await tabOf(hazel, 'following');
            check(tag + 'Following: pebble under Looking, and the line that nobody is told', await hazel.page.evaluate(() => Array.from(document.querySelectorAll('.pl-group')).map((g) => g.getAttribute('data-g') + ':' + Array.from(g.querySelectorAll('.pl-nm')).map((n) => n.textContent).join()).join('|') === 'look:pebble' && /Nobody is told/.test(document.querySelector('.pl-keep').textContent)));
            await openSheet(hazel, 'pebble');
            buttons = await sheetButtons(hazel);
            check(tag + 'the sheet of a person that is followed says Following', buttons.some((b) => b.text === 'Following'), buttons.map((b) => b.text).join('|'));
            await shot(hazel, '02_menu_following');
            await press(hazel, 'Cancel');
            await tabOf(hazel, 'all');
            await openSheet(hazel, 'fern'); await press(hazel, 'Follow');
            await openSheet(hazel, 'juniper'); await press(hazel, 'Follow');
            await until(tag + 'three follows', () => hazel.page.evaluate(() => /Following\s*3/.test(document.querySelector('.pl-tab[data-tab="following"]').textContent)), 6000);
            await juniper.context.close();
            await until(tag + 'juniper is away in the list', () => hazel.page.evaluate(() => Array.from(document.querySelectorAll('.pl-card[data-s="away"] .pl-nm')).some((n) => n.textContent === 'juniper')), 12000);
            await tabOf(hazel, 'following');
            check(tag + 'Following: pebble and fern under Looking, juniper under Not here', await hazel.page.evaluate(() => { const g = Array.from(document.querySelectorAll('.pl-group')).map((x) => x.getAttribute('data-g') + ':' + Array.from(x.querySelectorAll('.pl-nm')).map((n) => n.textContent).join()); return g.join('|') === 'look:fern,pebble|away:juniper'; }));
            await shot(hazel, '03_follows_list');
            await tabOf(hazel, 'all');

            // ---- 3. a public room that fern opens ---------------------------------------------------------------------------------------------------------------------------------------------------
            await view(hazel, 'chat'); await view(hazel, 'players');
            await sendFrame(fern.page, { t: 'here', at: 'wait', pub: true, map: 'TREASURE.LVL', n: 1, of: 4, code: 'f3rn22', inv: { code: 'f3rn22', map: 'TREASURE.LVL', n: 1, of: 4 } });
            await until(tag + 'the room alert of fern reaches hazel', async () => (await bell(hazel)) === 1, 8000);
            await sleep(400);
            check(tag + 'a room that a person followed opened: a toast or a line of the chat says so, with Join', await hazel.page.evaluate(() => /fern opened a room/i.test(Array.from(document.querySelectorAll('.al-toast:not([hidden]), .al-chatline')).map((t) => t.textContent).join(' ')) && Array.from(document.querySelectorAll('.al-toast:not([hidden]) button, .al-chatline button')).some((b) => /Join/.test(b.textContent))));
            await shot(hazel, '05_room_opened_toast');
            await hazel.page.evaluate(() => document.querySelector('.al-bell').click()); await sleep(450);
            check(tag + 'the bell: "fern opened a room", the map and the seats, with Join', await hazel.page.evaluate(() => { const i = document.querySelector('.al-item[data-k="room"]'); return !!i && /fern opened a room/i.test(i.textContent) && /Treasure/.test(i.textContent) && /Join/.test(i.textContent); }));
            await shot(hazel, '04_bell_room_opened');
            await hazel.page.evaluate(() => document.querySelector('.al-x').click()); await sleep(300);
            await sendFrame(fern.page, { t: 'here', at: 'look' });
            const fernSaw = await fern.page.evaluate(() => Array.from(document.querySelectorAll('.al-toast, .al-item')).map((t) => t.textContent).join(' '));
            check(tag + 'fern is told nothing of the follow', !/hazel|follow/i.test(fernSaw), fernSaw);

            // ---- 4. a private message ----------------------------------------------------------------------------------------------------------------------------------------------------------------
            await openSheet(hazel, 'pebble'); await press(hazel, 'Message');
            await until(tag + 'the panel is on the conversation with pebble', async () => { const v = await pm(hazel); return !!v && v.view === 'thread'; }, 6000);
            let box = await pm(hazel);
            check(tag + 'the panel lies inside the window' + (hazel.phone ? ' as a sheet from the bottom' : ' on the right'), !!box && box.left >= -1 && box.right <= box.w + 1 && box.bottom <= box.h + 1 && (hazel.phone ? box.sheet && box.bottom >= box.h - 2 : box.right >= box.w - 40 && !box.sheet), JSON.stringify(box));
            const nm = await hazel.page.evaluate(() => document.querySelector('.pm-panel .pm-nm').textContent);
            check(tag + 'the conversation is with pebble, and says that it is private', nm === 'pebble' && /Only you and pebble can read this/.test(await hazel.page.evaluate(() => document.querySelector('.pm-note').textContent)));
            check(tag + 'the targets of the panel are 44 px or more on a touch screen', !hazel.phone || (await targetsOf(hazel, '.pm-panel button, .pm-panel input')).every((t) => t.h >= 43.5), JSON.stringify((await targetsOf(hazel, '.pm-panel button, .pm-panel input')).filter((t) => t.h < 43.5)));
            await typeAndSend(hazel, 'hey pebble, want a 2v2 on Islands?');
            await until(tag + 'the message is drawn when the hub says it', async () => (await bubbles(hazel)).join() === 'me:hey pebble, want a 2v2 on Islands?', 5000);
            check(tag + 'the field is empty after the echo', (await hazel.page.inputValue('.pm-panel .pm-input')) === '');
            await closePanel(hazel);
            await until(tag + 'pebble has the toast, the count and the bell row', async () => (await count(pebble)) === 1 && (await bell(pebble)) === 1 && /hazel sent you a private message/.test(await toasts(pebble)), 6000);
            check(tag + 'the toast of pebble has none of the words', !/Islands/.test(await toasts(pebble)));
            check(tag + 'pebble\'s bell has one row for hazel, with Open and none of the words', await pebble.page.evaluate(() => { document.querySelector('.al-bell').click(); const rows = Array.from(document.querySelectorAll('.al-item[data-k="msg"]')); const ok = rows.length === 1 && /hazel sent you a private message/.test(rows[0].textContent) && !/Islands/.test(rows[0].textContent); document.querySelector('.al-x').click(); return ok; }));
            await pebble.page.evaluate(() => Array.from(document.querySelectorAll('.al-toast button')).filter((b) => /Open/.test(b.textContent))[0].click()).catch(() => {});
            await sleep(500);
            const pb = await pm(pebble);
            check(tag + 'pebble: Open on the toast opens the conversation, which is read (no count, no bell row)', !!pb && pb.view === 'thread' && (await count(pebble)) === 0 && (await bell(pebble)) === 0 && (await bubbles(pebble)).join() === 'them:hey pebble, want a 2v2 on Islands?', JSON.stringify([pb, await count(pebble), await bell(pebble), await bubbles(pebble)]));
            await typeAndSend(pebble, 'come on, answer me. I know you are there');
            await until(tag + 'hazel has the toast and the count', async () => (await count(hazel)) === 1 && /pebble sent you a private message/.test(await toasts(hazel)), 6000);
            await view(hazel, 'chat');
            await shot(hazel, '13_dm_toast');
            await closePanel(pebble);

            // the list, and the conversation with a message of theirs
            await hazel.page.evaluate(() => document.querySelector('#gchat .pm-btn').click()); await sleep(400);
            check(tag + 'the list: pebble with the start of the newest message and a count of 1', await hazel.page.evaluate(() => { const r = document.querySelector('.pm-row'); return !!r && /pebble/.test(r.textContent) && /come on, answer me/.test(r.textContent) && r.querySelector('.pm-u').textContent === '1' && /Messages are kept for 7 days/.test(document.querySelector('.pm-keep').textContent); }));
            await shot(hazel, '11_dm_list');
            await hazel.page.evaluate(() => document.querySelector('.pm-row').click()); await sleep(400);
            check(tag + 'opening it reads it: no count on the button, no row in the bell', (await count(hazel)) === 0 && (await bell(hazel)) === 0, JSON.stringify([await count(hazel), await bell(hazel)]));
            await shot(hazel, '10_dm_thread');

            // ---- 5. a report and a block --------------------------------------------------------------------------------------------------------------------------------------------------------------
            await hazel.page.evaluate(() => { const r = Array.from(document.querySelectorAll('.pm-panel .pm-msg.them .pm-rep')).pop(); r.click(); }); await sleep(400);
            check(tag + 'the report card: the message, three reasons and the plain line', await hazel.page.evaluate(() => { const c = document.querySelector('.pm-card'); return !!c && /come on, answer me/.test(c.querySelector('.pm-quote').textContent) && c.querySelectorAll('input[type=radio]').length === 3 && /pebble is not told who reported/.test(c.querySelector('.pm-fine').textContent); }));
            check(tag + 'the card\'s targets are 44 px or more on a touch screen', !hazel.phone || (await targetsOf(hazel, '.pm-card button, .pm-card label')).every((t) => t.h >= 43.5), JSON.stringify(await targetsOf(hazel, '.pm-card button, .pm-card label')));
            await shot(hazel, '14_dm_report');
            await press(hazel, 'Send report');
            if (hazel.phone) await until(tag + 'the sheet says thanks on the line under the field (the toast would stand under the sheet)', () => hazel.page.evaluate(() => /Thanks: the admins will look at it/.test(document.querySelector('.pm-status').textContent)), 6000);
            else await toastIs(hazel, /Thanks: the admins will look at it/, 6000);
            const reports = admin('reports');
            check(tag + 'the hub holds the copy: the message, the reason, both names, nothing else', reports.status === 0 && /report 1/.test(reports.stdout) && /reported pebble/.test(reports.stdout) && /by hazel/.test(reports.stdout) && /come on, answer me/.test(reports.stdout) && /hey pebble, want a 2v2/.test(reports.stdout) && /reason abuse/.test(reports.stdout), reports.stdout + reports.stderr);

            await hazel.page.evaluate(() => document.querySelector('.pm-panel .pm-more').click()); await sleep(250);
            await press(hazel, 'Block pebble');
            await sleep(500);
            await closePanel(hazel);
            await pebble.page.evaluate(() => document.querySelector('#gchat .pm-btn').click()); await sleep(300);
            await pebble.page.evaluate(() => document.querySelector('.pm-row').click()); await sleep(400);
            await typeAndSend(pebble, 'are you there?');
            await until(tag + 'pebble sees its own message as sent', async () => (await bubbles(pebble)).slice(-1)[0] === 'me:are you there?', 5000);
            await sleep(900);
            check(tag + 'a blocked person\'s message is not shown to hazel: no count, no toast about it, nothing new in the bell', (await count(hazel)) === 0 && !/pebble sent you a private message/.test(await toasts(hazel)) && (await bell(hazel)) === 0, JSON.stringify([await count(hazel), await toasts(hazel), await bell(hazel)]));
            check(tag + 'no word of a message is in the hub\'s log', !/answer me|2v2|are you there/.test(stack.log()));
            await closePanel(pebble);
        } catch (e) {
            console.log('FAIL ' + tag + 'the check itself broke: ' + (e && e.stack || e));
            failures++;
        } finally {
            stack.stop();
            for (const c of browser.contexts()) await c.close().catch(() => {});
        }
    }
    check('no page error', errors.length === 0, errors.join(' | '));
    await browser.close();
}

main().then(() => { console.log('follows and private messages in a browser against the real hub: ' + checks + ' checks, ' + failures + ' failed'); process.exit(failures === 0 ? 0 : 1); }, (e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
