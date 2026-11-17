// The Shift button of the hotkey buttons (web/touch_keys.js and web/touch_keys.css, docs/TOUCH.md "The Shift button") in a real Chromium (Playwright, when the machine has it: the exit status is 3 and nothing is
// checked when it does not) with the browser's own touch events (the DevTools protocol's touchStart, touchMove and touchEnd: real TouchEvents, with their lists, their phases and their order) on a bare page whose
// canvas stands in for the game's. What the game does with a key is MODELLED here, the way it is read in SDL's code and in the game's (src/ants_app/application_input.cpp, handle_mouse_button):
//   * SDL's modifier state changes when the browser's key event ARRIVES (the key handler of the window runs at once and sets it);
//   * the game reads it when it HANDLES the finger's lift, in its next frame: the lift is queued by the canvas's own touchend handler and handled by the next animation frame.
// So a Shift that goes up in the same breath as the lift would be read as no Shift. The checks: for a tap and a drag with Shift latched or locked, the state that the model reads at the touchstart and at the
// handling of the lift is Shift DOWN; with Shift off it is up and no Shift key is ever sent; a hold and two fingers (pan, zoom) leave the latch alone; Shift is down 120 ms after the lift and not much longer;
// a blur, a hidden page, a finger that the browser takes and the setting turned off let it go at once. NOT checked here (see docs/TOUCH.md): SDL's own Emscripten code (that a page-made ShiftLeft reaches its state
// as the page-made Control of the chords does: the opt-in browser check against a built game page), and the game's selection (test_touch_app, which sets SDL's modifier state itself).
// usage: node web_touch_keys_shift_check.js touch_keys.js touch_keys.css     (exit 0: every check holds; 1: one fails; 3: no browser to run it in)
'use strict';
const fs = require('fs');
let chromium;
for (const where of ['playwright', '/opt/node-tools/node_modules/playwright', process.env.PLAYWRIGHT_MODULE || '']) {
    if (!where) continue;
    try { chromium = require(where).chromium; if (chromium) break; } catch (e) { /* try the next place */ }
}
const [script, style] = process.argv.slice(2);
if (!script || !style) { console.log('usage: web_touch_keys_shift_check.js touch_keys.js touch_keys.css'); process.exit(2); }
if (!chromium) { console.log('SKIP: no Playwright on this machine'); process.exit(3); }

let failed = 0, checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) { console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const PAGE = '<!doctype html><meta name="viewport" content="width=device-width"><style>body { margin: 0; } #game-stage { position: relative; width: 340px; height: 190px; margin: 40px 0 0 25px; } canvas { display: block; touch-action: none; }</style>' +
    '<div id="game-stage"><canvas id="canvas" width="340" height="190"></canvas></div>';

// The model of the game: SDL's modifier state, the canvas's queue, and a frame that handles the queue (the next animation frame; with `lag` the lift is handled that many milliseconds after it: a stalled game.
// A timer of the page's that was set before it and falls due later runs after it, so the order of the two does not depend on the machine's load)
const MODEL = `
window.model = { shift: 0, shiftEvents: [], queue: [], log: [], lag: 0 };
window.addEventListener('keydown', (e) => { if (e.code === 'ShiftLeft') { model.shift = 1; model.shiftEvents.push(['down', e.key, e.keyCode, e.shiftKey, e.isTrusted]); } });
window.addEventListener('keyup', (e) => { if (e.code === 'ShiftLeft') { model.shift = 0; model.shiftEvents.push(['up', e.key, e.keyCode, e.shiftKey, e.isTrusted]); } });
const canvas = document.getElementById('canvas');
canvas.addEventListener('touchstart', (e) => { model.log.push({ at: 'touchstart', shift: model.shift, touches: e.touches.length }); model.queue.push('down'); e.preventDefault(); }, { passive: false });
function handle(ev) { model.log.push({ at: 'handled ' + ev, shift: model.shift }); }
canvas.addEventListener('touchend', (e) => { model.log.push({ at: 'touchend', shift: model.shift, touches: e.touches.length }); if (model.lag) setTimeout(() => handle('up'), model.lag); else model.queue.push('up'); });
function frame() {
    while (model.queue.length) handle(model.queue.shift());
    requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
`;

(async () => {
    const browser = await chromium.launch({ args: ['--no-sandbox'] });
    try {
        const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
        const page = await context.newPage();
        const cdp = await context.newCDPSession(page);
        page.on('pageerror', (e) => { console.log('FAIL the page threw: ' + e.message); failed++; });
        await page.setContent(PAGE);
        await page.addStyleTag({ content: fs.readFileSync(style, 'utf8') });
        await page.addScriptTag({ content: fs.readFileSync(script, 'utf8') });
        await page.evaluate(MODEL);
        await page.evaluate(() => {
            window.keys = AntsTouchKeys.forPage({
                doc: document, win: window, stage: document.getElementById('game-stage'), canvas: document.getElementById('canvas'), store: () => null,
                touch: () => true, ready: () => true, watching: () => false, running: () => true,
                ask: (n) => (n === 60 ? -1 : (n === 50 ? 1 : 0)), menu: () => 0, isFullscreen: () => false, relayout: () => {}, every: () => 0,
            });
            window.keys.refresh();
        });
        const box = async (sel) => { const b = await (await page.$(sel)).boundingBox(); return { x: b.x + b.width / 2, y: b.y + b.height / 2 }; };
        const canvasAt = async (dx, dy) => { const b = await (await page.$('#canvas')).boundingBox(); return { x: b.x + b.width / 2 + (dx || 0), y: b.y + b.height / 2 + (dy || 0) }; };
        const touch = (type, points) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: points.map((p, i) => ({ x: p.x, y: p.y, id: i })) });
        const state = () => page.evaluate(() => ({ mode: window.keys.shift(), held: window.keys.shiftDown(), mod: model.shift }));
        const reset = () => page.evaluate(() => { model.log.length = 0; model.shiftEvents.length = 0; model.queue.length = 0; });
        const lit = async () => { await reset(); const b = await box('[data-key=shift]'); await page.touchscreen.tap(b.x, b.y); await sleep(60); };
        // a one-finger sequence on the canvas: `moves` moves of 16 ms, `rest` ms of stillness first
        async function finger(o) {
            o = o || {};
            const p = await canvasAt();
            await touch('touchStart', [p]);
            await sleep(o.rest === undefined ? 70 : o.rest);
            for (let i = 1; i <= (o.moves || 0); i++) { await touch('touchMove', [{ x: p.x + 12 * i, y: p.y + 6 * i }]); await sleep(16); }
            await touch('touchEnd', []);
        }
        const logOf = () => page.evaluate(() => model.log.map((l) => l.at + ':' + l.shift));
        const shiftEvents = () => page.evaluate(() => model.shiftEvents.map((e) => e.join(':')));

        // the three states of the button, by a real tap and a real press
        let b = await box('[data-key=shift]');
        await page.touchscreen.tap(b.x, b.y); await sleep(60);
        expect('a tap on Shift lights it (latched, pressed)', await page.evaluate(() => { const e = document.querySelector('[data-key=shift]'); return [window.keys.shift(), e.getAttribute('aria-pressed'), e.getAttribute('data-state')]; }), ['latched', 'true', 'latched']);
        expect('...and sends no key', await shiftEvents(), []);
        await page.touchscreen.tap(b.x, b.y); await sleep(60);
        expect('a second tap puts it out', (await state()).mode, 'off');
        await touch('touchStart', [b]); await sleep(520); await touch('touchEnd', []); await sleep(60);
        expect('a press of half a second locks it', (await state()).mode, 'locked');
        await page.touchscreen.tap(b.x, b.y); await sleep(60);
        expect('...and a tap puts the lock out', (await state()).mode, 'off');

        // a tap on the picture with Shift latched: down before the game's own touchstart, read as down when the game handles the lift, up 120 ms after it
        await lit();
        await finger();
        await sleep(40);
        let log = await logOf();
        expect('latched: at the touchstart the game already has Shift down, and at the handling of the lift too (a frame after it)', log, ['touchstart:1', 'handled down:1', 'touchend:1', 'handled up:1']);
        expect('...the latch is used up at the lift, and Shift is still down 40 ms later', [(await state()).mode, (await state()).mod], ['off', 1]);
        await sleep(200);
        expect('...and it is up 120 ms after the lift: one keydown and one keyup, trusted-looking events of Shift', await shiftEvents(), ['down:Shift:16:true:false', 'up:Shift:16:false:false']);
        expect('...the state is clean', await state(), { mode: 'off', held: false, mod: 0 });

        // the same with a game that handles the lift 100 ms late (a stall): Shift is still down when it does
        await page.evaluate(() => { model.lag = 100; });
        await lit();
        await finger();
        await sleep(300);
        expect('a game that handles the lift 100 ms late still reads Shift down at the lift', (await logOf()).filter((l) => l.indexOf('handled up') === 0), ['handled up:1']);
        await page.evaluate(() => { model.lag = 0; });

        // a drag
        await lit();
        await finger({ rest: 30, moves: 8 });
        await sleep(250);
        log = await logOf();
        expect('a drag: Shift is down at the touchstart and when the lift is handled, and the latch is used up', [log[0], log[log.length - 1], (await state()).mode], ['touchstart:1', 'handled up:1', 'off']);
        expect('...one Shift key down and up for the whole drag', await shiftEvents(), ['down:Shift:16:true:false', 'up:Shift:16:false:false']);

        // Shift off: nothing is sent, and the game reads no Shift
        await reset();
        await finger();
        await sleep(250);
        expect('Shift off: the game reads no Shift at the lift, and no Shift key is sent', [(await logOf()).filter((l) => l.indexOf('handled up') === 0), await shiftEvents()], [['handled up:0'], []]);

        // a hold leaves the latch (the game's hold is a right click: Shift plays no part), the key goes around it
        await lit();
        await finger({ rest: 600 });
        await sleep(250);
        expect('a hold: the latch stays, Shift went down and up around it', [(await state()).mode, await shiftEvents()], ['latched', ['down:Shift:16:true:false', 'up:Shift:16:false:false']]);
        // two fingers: pan and zoom leave the latch, one key for both
        await reset();
        const a = await canvasAt(-40, 0), c = await canvasAt(40, 0);
        await touch('touchStart', [a]); await sleep(30); await touch('touchStart', [a, c]); await sleep(60);
        await touch('touchMove', [{ x: a.x - 20, y: a.y }, { x: c.x + 20, y: c.y }]); await sleep(30);
        await touch('touchEnd', []); await sleep(250);
        expect('two fingers: the latch stays, one Shift key down and up', [(await state()).mode, await shiftEvents()], ['latched', ['down:Shift:16:true:false', 'up:Shift:16:false:false']]);
        // ...and the next tap uses it
        await reset();
        await finger();
        await sleep(250);
        expect('the next tap is a Shift-click and uses it up', [(await logOf()).filter((l) => l.indexOf('handled up') === 0), (await state()).mode], [['handled up:1'], 'off']);

        // locked: tap after tap
        await reset();
        await touch('touchStart', [b]); await sleep(520); await touch('touchEnd', []); await sleep(60);
        await finger(); await sleep(40); await finger(); await sleep(300);
        expect('locked: two taps in a row are both Shift-clicks, and the lock stays', [(await logOf()).filter((l) => l.indexOf('handled up') === 0), (await state()).mode], [['handled up:1', 'handled up:1'], 'locked']);
        await page.touchscreen.tap(b.x, b.y); await sleep(60);

        // never left down
        const letGo = [
            ['the window loses the focus', () => window.dispatchEvent(new Event('blur'))],
            ['the page is hidden', () => { Object.defineProperty(document, 'hidden', { configurable: true, get: () => true }); document.dispatchEvent(new Event('visibilitychange')); }],
            ['the setting is turned off', () => window.keys.set(false)],
        ];
        for (const [what, fn] of letGo) {
            await lit();
            const p = await canvasAt();
            await touch('touchStart', [p]); await sleep(50);
            expect(what + ': Shift is down with the finger', (await state()).mod, 1);
            await page.evaluate(fn);
            expect(what + ': Shift is up at once, and the latch is let go', [(await state()).mod, (await state()).mode], [0, 'off']);
            await touch('touchEnd', []); await sleep(250);
            expect(what + ': and no second keyup comes', (await shiftEvents()).length, 2);
            await page.evaluate(() => { delete document.hidden; window.keys.set(true); window.keys.refresh(); });
        }
        await lit();
        const q = await canvasAt();
        await touch('touchStart', [q]); await sleep(50);
        await touch('touchCancel', []); await sleep(30);
        expect('a finger that the browser takes (touchcancel): Shift is up at once and the latch stays', [(await state()).mod, (await state()).mode], [0, 'latched']);
        await context.close();
    } finally { await browser.close(); }
    console.log(checks + ' checks, ' + failed + ' failures');
    process.exit(failed ? 1 : 0);
})().catch((e) => { console.log('FAIL the check threw: ' + (e && e.stack ? e.stack : e)); process.exit(1); });
