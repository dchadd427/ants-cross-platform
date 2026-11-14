// The game page's bar (web/game_bar.js and web/game_bar.css on the real web/site_nav.js, web/site_nav.css and the real markup of the bar in web/shell.html; docs/SITE_NAV.md "The game page") in a real Chromium
// (Playwright, when the machine has it: exit status 3 and nothing checked when it does not). The page around the bar is a stand-in: a canvas, a "game" that counts the keys that reach it, and a match that is
// running or not. What it holds:
//   links:  with no match every link of the bar and of its Menu acts as on any page; while a match runs Home asks (No stays, Yes lets go of the seat and goes, a click that opens another tab leaves the seat),
//           the mark of live matches is Home too, every other link opens a new tab and never takes this one, a link that is a new tab anyway is left alone;
//   keys:   with the focus on the game a key reaches it; with the focus in the bar or the Menu open no key reaches it (down, press, up), a key kept stays kept until its lift (repeats too), a key that was down
//           before keeps its lift, a bare Shift goes on, Enter presses the focused control, Esc closes the Menu and the game never hears it, a fresh press ends the keeping of the one before;
//   focus:  a press with the pointer gives the focus back to the game, except on the Menu's button, which has opened the Menu;
//   reload: "Reload page" reloads at once with no game playing, and while a game is played (not in a recording or a live view) asks first, Cancel focused, the keys the question's, Esc and Cancel stay;
//   resize: the open Menu stays through a change of height alone and the page's own resize, and closes at a new width;
//   Menu:   the lines the page adds each time it opens (the note while a match runs, "Reload page" last of the small links, a recording's Copy link, Download and Open another file on a narrow window only and
//           not for a file of your own or a live view, each one pressing the button of the bar that it stands for).
// usage: node web_game_bar_check.js game_bar.js game_bar.css [shell.html [site_nav.js site_nav.css]]     (exit 0: every check holds; 1: one fails; 3: no browser to run it in)
'use strict';
const fs = require('fs');
const path = require('path');
let chromium;
for (const where of ['playwright', '/opt/node-tools/node_modules/playwright', process.env.PLAYWRIGHT_MODULE || '']) {
    if (!where) continue;
    try { chromium = require(where).chromium; if (chromium) break; } catch (e) { /* try the next place */ }
}
const WEB = path.join(__dirname, '..', '..', 'web');
const [script, style, shell = path.join(WEB, 'shell.html'), navScript = path.join(WEB, 'site_nav.js'), navStyle = path.join(WEB, 'site_nav.css')] = process.argv.slice(2);
if (!script || !style) { console.log('usage: web_game_bar_check.js game_bar.js game_bar.css [shell.html [site_nav.js site_nav.css]]'); process.exit(2); }
if (!chromium) { console.log('SKIP: no Playwright on this machine'); process.exit(3); }

let failed = 0, checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) { console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
}

const page = fs.readFileSync(shell, 'utf8');
const bar = /<header class="sb"[\s\S]*?<\/header>/.exec(page)[0].replace(/@@GAME_VERSION@@/g, 'v1.2.3').replace(/@@BUILD_ID@@/g, 'abc1234').replace(/@@BUILD_DATE@@/g, '2026-01-02 09:30 PT');
// the page's own rules that the bar leans on: the button, the tokens the site's style reads, and what hides the recording's pieces
const BASE = '*, ::before, ::after { box-sizing: border-box; } :root { --cream: #f4ece2; --gold: #f6d058; --edge: #050a08; --frame: #2b6b4f; --frame-hi: #3b976f; --frame-lo: #13634b; --shadow: #9d0d17; --bevel-sm: none; } body { margin: 0; padding: 14px 20px; }'
    + ' .btn { display: inline-flex; align-items: center; justify-content: center; min-height: 44px; padding: 8px 18px; font: 16px sans-serif; color: var(--cream); background: #17382f; border: 2px solid var(--edge); }'
    + ' .sr { position: absolute; width: 1px; height: 1px; overflow: hidden; } body:not(.replay) .rep-only { display: none !important; } .rhead { min-width: 0; } #game-stage { height: 200px; }'
    + ' body.live [data-dl], body.nodl [data-dl], body.ownfile [data-dl], body.ownfile #copy-btn, body.ownfile #stats-btn { display: none !important; }';
const HTML = '<!doctype html><meta name="viewport" content="width=device-width"><body class="BODY">' + bar + '<div id="game-stage"><canvas id="canvas" tabindex="-1" width="100" height="60"></canvas></div>';

// the stand-in for the game and for the page's call: window.__log is what happened
const SETUP = () => {
    window.__log = { keys: [], asks: 0, leaves: 0, opens: [], reloads: 0, focus: 0, clicks: [], answer: true };
    window.__busy = false;
    window.__running = false;
    for (const type of ['keydown', 'keypress', 'keyup']) document.addEventListener(type, (e) => window.__log.keys.push(type + ':' + e.key), false);       // (the game: it listens at the document, in the bubble phase)
    document.addEventListener('click', (e) => { const a = e.target.closest && e.target.closest('a, button'); if (a) { window.__log.clicks.push({ id: a.id || a.textContent.trim(), prevented: e.defaultPrevented }); if (a.tagName === 'A') e.preventDefault(); } }, false);      // (what a click did, and no real navigation)
    AntsGameBar.start({
        doc: document,
        win: { open: (href, target, features) => { window.__log.opens.push([href, target, features]); return {}; }, location: { reload: () => { window.__log.reloads++; } } },
        busy: () => window.__busy, running: () => window.__running, ask: () => { window.__log.asks++; return window.__log.answer; }, leave: () => { window.__log.leaves++; },
        focus: () => { window.__log.focus++; document.getElementById('canvas').focus(); },
    });
};

async function open(browser, w, h, body) {
    const context = await browser.newContext({ viewport: { width: w, height: h } });
    const p = await context.newPage();
    await p.setContent(HTML.replace('BODY', body || ''));
    await p.addStyleTag({ content: fs.readFileSync(navStyle, 'utf8') });
    await p.addStyleTag({ content: fs.readFileSync(style, 'utf8') });
    await p.addStyleTag({ content: BASE });
    await p.addScriptTag({ content: fs.readFileSync(navScript, 'utf8') });
    await p.addScriptTag({ content: fs.readFileSync(script, 'utf8') });
    await p.evaluate(SETUP);
    return { context, p };
}
const log = (p) => p.evaluate(() => JSON.parse(JSON.stringify(window.__log)));
const reset = (p) => p.evaluate(() => { const l = window.__log; l.keys = []; l.asks = 0; l.leaves = 0; l.opens = []; l.reloads = 0; l.focus = 0; l.clicks = []; l.answer = true; });
const focusGame = (p) => p.evaluate(() => document.getElementById('canvas').focus());
const menuOpen = (p) => p.evaluate(() => { const b = document.getElementById('sbm'); return !!b && !b.hidden; });
const menuLines = (p) => p.evaluate(() => [].slice.call(document.querySelectorAll('#sbm > *')).filter((n) => n.offsetParent !== null).map((n) => n.textContent.trim()));
const openMenu = async (p) => { if (!(await menuOpen(p))) await p.click('.sbtn'); };
const active = (p) => p.evaluate(() => { const a = document.activeElement; return a ? (a.id || a.textContent.trim() || a.tagName) : null; });

(async () => {
    const browser = await chromium.launch({ args: ['--no-sandbox'] });
    try {
        // ---- the links ----
        {
            const { context, p } = await open(browser, 1280, 800);
            await p.click('nav a[href="/"]');
            let l = await log(p);
            expect('no match: Home goes without asking', [l.asks, l.leaves, l.clicks[0].prevented, l.opens.length], [0, 0, false, 0]);
            await reset(p);
            await p.click('nav a[href="/matches"]');
            l = await log(p);
            expect('no match: Matches is an ordinary link', [l.asks, l.opens.length, l.clicks[0].prevented], [0, 0, false]);
            await p.evaluate(() => { window.__busy = true; });
            await reset(p);
            await p.evaluate(() => { window.__log.answer = false; });
            await p.click('nav a[href="/"]');
            l = await log(p);
            expect('a match: Home asks, and No stays', [l.asks, l.leaves, l.clicks[0].prevented, l.opens.length], [1, 0, true, 0]);
            await reset(p);
            await p.click('nav a[href="/"]');
            l = await log(p);
            expect('a match: Home asks, and Yes lets go of the seat and goes', [l.asks, l.leaves, l.clicks[0].prevented, l.opens.length], [1, 1, false, 0]);
            await reset(p);
            await p.click('nav a[href="/"]', { modifiers: ['Control'] });
            l = await log(p);
            expect('a match: a click that opens another tab asks but keeps the seat', [l.asks, l.leaves, l.clicks[0].prevented], [1, 0, false]);
            await reset(p);
            await p.click('nav a[href="/matches"]');
            l = await log(p);
            expect('a match: Matches opens a new tab and does not take this one', [l.asks, l.leaves, l.clicks[0].prevented, l.opens], [0, 0, true, [['/matches', '_blank', 'noopener,noreferrer']]]);
            await reset(p);
            await p.click('nav a[href="/mapmaker"]');
            expect('a match: Map maker opens a new tab', (await log(p)).opens.map((o) => o[0]), ['/mapmaker']);
            await reset(p);
            await p.click('nav a[aria-label="Discord server"]');
            l = await log(p);
            expect('a match: Discord is a new tab anyway and is left alone', [l.opens.length, l.clicks[0].prevented, l.asks], [0, false, 0]);
            await reset(p);
            await p.evaluate(() => { const m = document.querySelector('.sblive'); m.hidden = false; m.textContent = '2 live'; });
            await p.click('.sblive');
            l = await log(p);
            expect('a match: the mark of live matches goes to the front page, so it asks like Home', [l.asks, l.leaves, l.opens.length], [1, 1, 0]);
            await reset(p);
            await openMenu(p);
            await p.click('#sbm a[href="/changelog"]');
            l = await log(p);
            expect('a match: a link of the Menu opens a new tab too', [l.opens.map((o) => o[0]), l.clicks.filter((c) => c.prevented).length], [['/changelog'], 1]);
            await reset(p);
            await openMenu(p);
            await p.click('#sbm a[href="/"]');
            l = await log(p);
            expect('a match: the Menu\'s Home asks as well', [l.asks, l.leaves], [1, 1]);
            await context.close();
        }
        // ---- the keys ----
        {
            const { context, p } = await open(browser, 1280, 800);
            await focusGame(p);
            await p.keyboard.press('a');
            let l = await log(p);
            expect('the focus on the game: a key reaches it', l.keys, ['keydown:a', 'keypress:a', 'keyup:a']);
            await reset(p);
            await p.focus('nav a[href="/matches"]');
            await p.keyboard.press('a');
            l = await log(p);
            expect('the focus in the bar: no key reaches the game (down, press, up)', l.keys, []);
            await p.keyboard.press('Tab');
            expect('the focus in the bar: Tab moves along it, and the game does not hear it', [(await log(p)).keys, await active(p)], [[], 'Map maker']);
            await reset(p);
            await p.focus('#fullscreen-btn');
            await p.evaluate(() => document.getElementById('fullscreen-btn').addEventListener('click', () => window.__log.clicks.push({ id: 'fullscreen-pressed' })));
            await p.keyboard.press('Enter');
            await p.keyboard.press(' ');
            l = await log(p);
            expect('the focus on a button: Enter and Space press it, and the game does not hear them', [l.keys, l.clicks.filter((c) => c.id === 'fullscreen-pressed').length], [[], 2]);
            await reset(p);
            await p.evaluate(() => {      // (the settings gear, which the settings menu puts in the bar and which presses itself on Enter and Space)
                const gear = document.createElement('button'); gear.id = 'settings-btn'; gear.textContent = 'Settings';
                gear.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); e.stopPropagation(); window.__log.clicks.push({ id: 'gear-pressed' }); } });
                document.addEventListener('keyup', (e) => { if (e.key === 'Enter' || e.key === ' ') e.stopPropagation(); }, true);      // (the menu keeps the lift of the key that it took)
                document.getElementById('fullscreen-btn').before(gear);
            });
            await p.focus('#settings-btn');
            await p.keyboard.press('Enter');
            l = await log(p);
            expect('the focus on the gear: it presses itself on Enter, and the game does not hear it', [l.clicks.filter((c) => c.id === 'gear-pressed').length, l.keys], [1, []]);
            await reset(p);
            // a key kept stays the bar's until its lift, the focus gone back to the game
            await p.focus('nav a[href="/matches"]');
            await p.keyboard.down('x');
            await focusGame(p);
            await p.keyboard.down('x');
            await p.keyboard.up('x');
            l = await log(p);
            expect('a key whose press the bar kept: its repeat and its lift stay the bar\'s after the focus has gone', l.keys, []);
            await p.keyboard.press('x');
            expect('a fresh press after that is the game\'s', (await log(p)).keys, ['keydown:x', 'keypress:x', 'keyup:x']);
            await reset(p);
            // a key that was down before keeps its lift
            await p.keyboard.down('q');
            await p.focus('nav a[href="/matches"]');
            await p.keyboard.up('q');
            l = await log(p);
            expect('a key that went down before the focus came to the bar: its lift is the game\'s', l.keys, ['keydown:q', 'keypress:q', 'keyup:q']);
            await reset(p);
            await p.keyboard.down('Shift');
            await p.keyboard.up('Shift');
            expect('a bare Shift in the bar goes on to the game', (await log(p)).keys, ['keydown:Shift', 'keyup:Shift']);
            await reset(p);
            // the Menu
            await focusGame(p);
            await p.click('.sbtn');
            expect('the Menu button opens the Menu', await menuOpen(p), true);
            expect('the pointer press on the Menu button leaves the focus to the Menu', (await active(p)) !== 'canvas' && (await log(p)).focus === 0, true);
            await reset(p);
            await p.keyboard.press('b');
            await p.keyboard.press('Enter');
            l = await log(p);
            expect('the Menu open: no key reaches the game, even the one that follows the Menu\'s link', l.keys, []);
            await focusGame(p);
            await p.keyboard.press('c');
            expect('the Menu open and the focus on the game: the keys are still the Menu\'s', (await log(p)).keys, []);
            await p.keyboard.down('Escape');
            expect('Esc closes the Menu', await menuOpen(p), false);
            await p.keyboard.up('Escape');
            expect('Esc: the game never hears it, down or up', (await log(p)).keys, []);
            expect('Esc: the focus goes back to the Menu\'s button', await active(p), 'BUTTON');
            await reset(p);
            await focusGame(p);
            await p.keyboard.press('Escape');
            expect('the Menu closed and the focus on the game: Esc is the game\'s', (await log(p)).keys, ['keydown:Escape', 'keyup:Escape']);
            await context.close();
        }
        // ---- the focus after a press ----
        {
            const { context, p } = await open(browser, 1280, 800);
            await p.click('#fullscreen-btn');
            expect('a press with the pointer on Fullscreen gives the focus to the game', [await active(p), (await log(p)).focus], ['canvas', 1]);
            await reset(p);
            await p.focus('#fullscreen-btn');
            await p.keyboard.press('Enter');
            expect('a press with the keyboard keeps the focus on the button', [await active(p), (await log(p)).focus], ['fullscreen-btn', 0]);
            await context.close();
        }
        // ---- the Menu's own lines ----
        {
            const { context, p } = await open(browser, 390, 844);
            await openMenu(p);
            let lines = await menuLines(p);
            expect('no match, not a recording: the Menu is the site\'s, and has no recording lines and no note', [lines.indexOf('Home'), lines.includes('Copy link'), lines.includes('Download'), lines.some((x) => /^A match is running/.test(x))], [0, false, false, false]);
            expect('"Reload page" is the last small link', await p.evaluate(() => { const a = document.querySelectorAll('#sbm .sbsmall a'); return a[a.length - 1].textContent; }), 'Reload page');
            await p.click('#sbm .gb-reload');
            expect('"Reload page" reloads and closes the Menu', [(await log(p)).reloads, await menuOpen(p)], [1, false]);
            for (let i = 0; i < 3; i++) { await p.click('.sbtn'); await p.click('.sbtn'); }
            await p.click('.sbtn');
            expect('opened again and again: one "Reload page", and no line made twice', await p.evaluate(() => [document.querySelectorAll('#sbm .gb-reload').length, document.querySelectorAll('#sbm .gb-x').length]), [1, 3]);
            await p.click('.sbtn');
            await p.evaluate(() => { window.__busy = true; });
            await p.click('.sbtn');
            lines = await menuLines(p);
            expect('a match: the Menu starts with a line that says what the links do', /^A match is running here\. Home asks first; the other links open in a new tab\.$/.test(lines[0]), true);
            expect('a match: the first link that shows has the focus', await p.evaluate(() => document.activeElement && document.activeElement.textContent.trim()), 'Home');
            await context.close();
        }
        for (const [w, h, body, wanted, label] of [
            [390, 844, 'replay', ['Copy link', 'Download'], 'a recording on a phone'],
            [390, 844, 'replay ownfile', [], 'a file of your own on a phone'],
            [390, 844, 'replay live', ['Copy link'], 'a live view on a phone'],
            [1280, 800, 'replay', [], 'a recording on a wide window (the bar has the buttons)'],
        ]) {
            const { context, p } = await open(browser, w, h, body);
            await openMenu(p);
            const lines = await menuLines(p);
            expect(label + ': the lines at the top of the Menu', lines.filter((x) => ['Copy link', 'Download', 'Open another file'].includes(x)), wanted);
            if (wanted.length) expect(label + ': they lead', lines.slice(0, wanted.length), wanted);
            await context.close();
        }
        {
            const { context, p } = await open(browser, 390, 844, 'replay ownfile');
            await p.evaluate(() => { const b = document.createElement('button'); b.id = 'own-more'; b.addEventListener('click', () => window.__log.clicks.push({ id: 'own-more-pressed' })); document.getElementById('own-hold').appendChild(b); });
            await p.click('.sbtn');
            expect('a file of your own: "Open another file" leads the Menu', (await menuLines(p))[0], 'Open another file');
            await p.click('#sbm .gb-file');
            expect('it presses the button that the recording\'s page made, and closes the Menu', [(await log(p)).clicks.filter((c) => c.id === 'own-more-pressed').length, await menuOpen(p)], [1, false]);
            await context.close();
        }
        {
            const { context, p } = await open(browser, 390, 844, 'replay');
            await p.evaluate(() => {
                document.getElementById('copy-btn').addEventListener('click', () => window.__log.clicks.push({ id: 'copy-pressed' }));
                document.querySelector('[data-dl]').addEventListener('click', (e) => { e.preventDefault(); window.__log.clicks.push({ id: 'download-pressed' }); });
            });
            await p.click('.sbtn');
            await p.click('#sbm .gb-copy');
            await p.click('.sbtn');
            await p.click('#sbm .gb-dl');
            const ids = (await log(p)).clicks.map((c) => c.id);
            expect('Copy link and Download of the Menu press the bar\'s own buttons', [ids.includes('copy-pressed'), ids.includes('download-pressed')], [true, true]);
            await context.close();
        }
        // ---- Reload page asks while a game is played ----
        {
            const { context, p } = await open(browser, 390, 844);
            const ask = () => p.evaluate(() => { const a = document.querySelector('.gb-ask'); return a ? [a.textContent.replace(/\s+/g, ' ').trim(), document.activeElement.textContent] : null; });
            await openMenu(p);
            await p.click('#sbm .gb-reload');
            expect('no game: Reload page reloads at once and asks nothing', [(await log(p)).reloads, await ask()], [1, null]);
            await p.evaluate(() => { window.__running = true; });
            await reset(p);
            await openMenu(p);
            await p.click('#sbm .gb-reload');
            expect('a game played: Reload page asks first, with Cancel focused, and has not reloaded', [(await ask()), (await log(p)).reloads, await menuOpen(p)], [['Reload the page? A game against the computer on this device will end; an online match comes back by itself.CancelReload', 'Cancel'], 0, false]);
            await p.keyboard.press('x');
            expect('the question open: no key reaches the game', (await log(p)).keys, []);
            await p.keyboard.press('Tab');
            expect('Tab goes to Reload and round again to Cancel', [await p.evaluate(() => document.activeElement.textContent), await (async () => { await p.keyboard.press('Tab'); return p.evaluate(() => document.activeElement.textContent); })()], ['Reload', 'Cancel']);
            await p.keyboard.press('Escape');
            expect('Esc cancels: the question is gone, nothing reloaded, the game never heard Esc', [await ask(), (await log(p)).reloads, (await log(p)).keys], [null, 0, []]);
            await openMenu(p);
            await p.click('#sbm .gb-reload');
            await p.keyboard.press('Enter');
            expect('Enter on Cancel cancels', [await ask(), (await log(p)).reloads], [null, 0]);
            await openMenu(p);
            await p.click('#sbm .gb-reload');
            await p.click('.gb-ask .btn >> text=Cancel');
            expect('a press on Cancel cancels and gives the focus to the game', [await ask(), (await log(p)).reloads, await active(p)], [null, 0, 'canvas']);
            await openMenu(p);
            await p.click('#sbm .gb-reload');
            await p.mouse.click(5, 5);
            expect('a press outside the card cancels', [await ask(), (await log(p)).reloads], [null, 0]);
            await openMenu(p);
            await p.click('#sbm .gb-reload');
            await p.click('.gb-ask .btn >> text=Reload');
            expect('Reload reloads', [await ask(), (await log(p)).reloads], [null, 1]);
            await context.close();
        }
        {
            const { context, p } = await open(browser, 390, 844, 'replay');
            await p.evaluate(() => { window.__running = true; });
            await openMenu(p);
            await p.click('#sbm .gb-reload');
            expect('a recording or a live view: Reload page reloads at once even if the game says it runs', [(await log(p)).reloads, await p.evaluate(() => !!document.querySelector('.gb-ask'))], [1, false]);
            await context.close();
        }
        // ---- the open Menu and a resize ----
        {
            const { context, p } = await open(browser, 390, 844);
            await openMenu(p);
            await p.setViewportSize({ width: 390, height: 700 });
            await p.waitForTimeout(300);
            expect('a change of height alone (a phone\'s address bar) leaves the Menu open', await menuOpen(p), true);
            await p.evaluate(() => window.dispatchEvent(new Event('resize')));
            expect('the page\'s own resize leaves it open', await menuOpen(p), true);
            await p.setViewportSize({ width: 844, height: 390 });
            await p.waitForTimeout(300);
            expect('a new width (turning the phone) closes it', await menuOpen(p), false);
            await context.close();
        }
        // ---- the bar's height: the same as the header it replaced, so the picture keeps its size ----
        for (const [w, h, body, high] of [[1280, 800, '', 44], [390, 844, '', 44], [844, 390, '', 44]]) {
            const { context, p } = await open(browser, w, h, body);
            const box = await p.evaluate(() => { const r = document.querySelector('header.sb').getBoundingClientRect(); return [Math.round(r.height), getComputedStyle(document.querySelector('header.sb')).marginBottom]; });
            expect(w + 'x' + h + ': the bar is 44 px high with the room under it that the header had', box, [high, h < 400 && w > 700 ? '14px' : w <= 700 ? '16px' : '18px']);
            await context.close();
        }
    } finally { await browser.close(); }
    console.log(checks + ' checks, ' + failed + ' failures');
    process.exit(failed ? 1 : 0);
})().catch((e) => { console.log('FAIL the check threw: ' + (e && e.stack ? e.stack : e)); process.exit(1); });
