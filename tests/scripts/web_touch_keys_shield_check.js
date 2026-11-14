// The layer under the hotkey buttons' list (web/touch_keys.js and web/touch_keys.css, docs/TOUCH.md): the real script and style in a real Chromium (Playwright, when the machine has it: the exit status is 3 and
// nothing is checked when it does not), on a bare page whose stage is small and sits inside a page that is taller than the window, as the game page's is. With the list open and the page scrolled, the layer
// takes every tap in the WINDOW (a tap on the page below the stage, and in each corner, lands on the layer, so the tap that closes the list is the list's alone), upright and sideways; a tap on it closes the list.
// usage: node web_touch_keys_shield_check.js touch_keys.js touch_keys.css     (exit 0: every check holds; 1: one fails; 3: no browser to run it in)
'use strict';
const fs = require('fs');
let chromium;
for (const where of ['playwright', '/opt/node-tools/node_modules/playwright', process.env.PLAYWRIGHT_MODULE || '']) {
    if (!where) continue;
    try { chromium = require(where).chromium; if (chromium) break; } catch (e) { /* try the next place */ }
}
const [script, style] = process.argv.slice(2);
if (!script || !style) { console.log('usage: web_touch_keys_shield_check.js touch_keys.js touch_keys.css'); process.exit(2); }
if (!chromium) { console.log('SKIP: no Playwright on this machine'); process.exit(3); }

let failed = 0, checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) { console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
}

const PAGE = '<!doctype html><meta name="viewport" content="width=device-width"><style>body { margin: 0; } #game-stage { position: relative; width: 240px; height: 140px; margin: 60px 0 0 90px; } #below { height: 3000px; }</style>' +
    '<div id="game-stage"><canvas id="canvas" width="240" height="140"></canvas></div><div id="below"></div>';

(async () => {
    const browser = await chromium.launch({ args: ['--no-sandbox'] });
    try {
        for (const [w, h] of [[390, 844], [844, 390]]) {
            const context = await browser.newContext({ viewport: { width: w, height: h }, hasTouch: true, isMobile: true });
            const page = await context.newPage();
            await page.setContent(PAGE);
            await page.addStyleTag({ content: fs.readFileSync(style, 'utf8') });
            await page.addScriptTag({ content: fs.readFileSync(script, 'utf8') });
            await page.evaluate(() => {
                window.keys = AntsTouchKeys.forPage({
                    doc: document, win: window, stage: document.getElementById('game-stage'), canvas: document.getElementById('canvas'), store: () => null,
                    touch: () => true, ready: () => true, watching: () => false, running: () => true,
                    ask: (n) => (n === 60 ? -1 : 0), menu: () => 0, isFullscreen: () => false, relayout: () => {}, every: () => 0,
                });
                window.keys.refresh();
                window.keys.toggle(true);
            });
            for (const y of [0, 700, 2400]) {
                await page.evaluate((to) => window.scrollTo(0, to), y);
                const got = await page.evaluate(() => {
                    const s = document.querySelector('.tk-shield'), r = s.getBoundingClientRect(), W = innerWidth, H = innerHeight;
                    const pts = [[1, 1], [W - 2, 1], [1, H - 2], [W - 2, H - 2], [W / 2, H / 2], [W / 2, H - 2], [W - 2, H / 2]];
                    const bar = document.getElementById('touch-keys').getBoundingClientRect();
                    return {
                        box: [r.left, r.top, r.width, r.height, W, H],
                        hits: pts.filter((p) => !(p[0] >= bar.left && p[0] <= bar.right && p[1] >= bar.top && p[1] <= bar.bottom)).map((p) => { const e = document.elementFromPoint(p[0], p[1]); return !!e && (e === s || document.getElementById('touch-keys').contains(e)); }),
                    };
                });
                expect(w + 'x' + h + ' scrolled to ' + y + ': the layer is the whole window', got.box, [0, 0, w, h, w, h]);
                expect(w + 'x' + h + ' scrolled to ' + y + ': every point of the window (corners, edges, centre) is the layer\'s or the list\'s', got.hits.every((v) => v), true);
            }
            await page.mouse.click(w / 2, h - 2);
            expect(w + 'x' + h + ': a tap on the layer closes the list', await page.evaluate(() => window.keys.isOpen()), false);
            await context.close();
        }
    } finally { await browser.close(); }
    console.log(checks + ' checks, ' + failed + ' failures');
    process.exit(failed ? 1 : 0);
})().catch((e) => { console.log('FAIL the check threw: ' + (e && e.stack ? e.stack : e)); process.exit(1); });
