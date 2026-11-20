// Runs the page's OWN code for the HUD's Fullscreen button (web/hud_fullscreen.js, AntsHudFullscreen.forPage, which web/shell.html calls), without a browser, with fakes for the page and a model of the
// game's button (Module._ants_probe 75 and 76), and checks:
//   * a click on the stage while the game says that its pointer is on its button enters fullscreen once, inside the click, and one while the page is fullscreen leaves it (the page's own enter and leave:
//     what the bar's Fullscreen control uses, so the two agree);
//   * a click that is not on the button (the game's own click on the map, a click when the game says -1) asks for nothing; so does a click of another mouse button and a click before the game is ready;
//   * a tap that the game has taken whole already (its count of the button's acts went up since the last look, its pointer state is back to 0) counts as a click on the button;
//   * a FINGER sends no click in a browser (the game cancels the touch), so a tap is read from the touch events: one finger, up within 400 ms and 10 px of where it came down, down and up on the button's place
//     (the game's rectangle, in the picture's pixels, found through the canvas's box on the page at any scale) enters or leaves fullscreen once, inside the touchend; a finger that starts or ends elsewhere, is held
//     longer, moves, is one of two, is cancelled, or lands while no button would act or before the game runs does nothing; the click that a browser may send after a tap that was taken is ignored, a later click is not;
//   * a click that a browser sends after a finger has lifted (a tap in a black bar of the fullscreen, outside the canvas, is one) is ignored whatever the game's pointer says, and a mouse click acts only when its
//     press began on the button too (a box that is dragged over it is no click on it);
//   * the game is told whether the page is fullscreen at the first look and whenever that changes (the browser's own, the page's own, by the button or by something else), and every poll looks again;
//   * nothing is asked of the game and nothing is told to it until the page says that it runs: a game in the browser has lazy stand-ins for its functions until its program is up, and the first call of one
//     before that leaves it unusable for the life of the page (every later probe answers -1, so the button would never act); the model of the game here breaks the same way;
//   * the script listens for the click and the touches on the stage (the capture phase, before the page's own handlers) and for nothing else.
// tests/scripts/test_web_hud_fullscreen.py runs this with node (the quick tier). usage: node web_hud_fullscreen_check.js web/hud_fullscreen.js     (exit 0: every check holds; every failure is printed)
'use strict';

let failed = 0;
let checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
        failed++;
    }
}

const modulePath = process.argv[2];
if (!modulePath) { console.log('usage: web_hud_fullscreen_check.js hud_fullscreen.js'); process.exit(2); }
const H = require(require('path').resolve(modulePath));

function page(opts) {
    opts = opts || {};
    const game = { on: opts.on === undefined ? 0 : opts.on, clicks: 0, probe: opts.noGame ? -1 : 1, broken: false, button: opts.noButton ? null : { x: 939, y: 7, w: 21, h: 23 }, canvas: { w: 960, h: 600 } };
    const box = opts.box || { left: 100, top: 80, width: 960, height: 600 };                // the canvas on the page, in CSS pixels (960 x 600 picture pixels at the scale of 1)
    const state = { ready: opts.ready !== false, fullscreen: !!opts.fullscreen };
    const calls = { enter: 0, leave: 0, told: [], listeners: [], intervals: [] };
    const stage = { addEventListener(type, fn, capture) { calls.listeners.push({ type, capture: !!capture, fn }); } };
    // A call of the game's functions before it runs breaks them for good (Emscripten's lazy export stand-ins: the page's Module._ants_probe stays undefined, so every later answer is -1)
    const ask = (what) => {
        if (!state.ready) game.broken = true;
        if (game.probe < 0 || game.broken) return -1;
        if (what === 75) return game.on;
        if (what === 76) return game.clicks;
        if (what >= 70 && what <= 73) return game.button ? [game.button.x, game.button.y, game.button.w, game.button.h][what - 70] : -1;
        if (what === 40) return game.canvas.w;
        if (what === 41) return game.canvas.h;
        return -1;
    };
    const p = { game, state, calls, stage };
    p.start = () => {
        p.api = H.forPage({
            stage, ask, rect: () => box, ready: () => state.ready, isFullscreen: () => state.fullscreen,
            enter: () => { calls.enter++; state.fullscreen = true; },
            leave: () => { calls.leave++; state.fullscreen = false; },
            tell: (on) => { if (!state.ready) game.broken = true; if (!game.broken) calls.told.push(on); },
            every: (fn, ms) => { calls.intervals.push({ fn, ms }); return calls.intervals.length; },
        });
        return p;
    };
    p.click = (event) => calls.listeners.find((l) => l.type === 'click').fn(event || { button: 0 });
    p.down = (event) => calls.listeners.find((l) => l.type === 'mousedown').fn(event || { button: 0 });
    // a finger: touch(type, [ { id, x, y } ... the fingers that are down after this event ], [ the fingers that changed ], time stamp)
    p.touch = (type, down, changed, at) => {
        const l = calls.listeners.find((x) => x.type === type);
        if (l) l.fn({ type, touches: down.map((f) => ({ identifier: f.id, clientX: f.x, clientY: f.y })), changedTouches: changed.map((f) => ({ identifier: f.id, clientX: f.x, clientY: f.y })), timeStamp: at });
    };
    // one finger that goes down at (x0, y0) at time t0 and up at (x1, y1) at time t1
    p.tap = (x0, y0, t0, x1, y1, t1) => {
        p.touch('touchstart', [{ id: 7, x: x0, y: y0 }], [{ id: 7, x: x0, y: y0 }], t0);
        p.touch('touchend', [], [{ id: 7, x: x1 === undefined ? x0 : x1, y: y1 === undefined ? y0 : y1 }], t1 === undefined ? t0 + 90 : t1);
    };
    p.onButton = () => ({ x: box.left + (939 + 10) * box.width / 960, y: box.top + (7 + 11) * box.height / 600 });       // the middle of the button's place on the page
    p.poll = () => calls.intervals.forEach((i) => i.fn());
    return p;
}

try {
    // ---- what it listens to
    {
        const p = page().start();
        expect('the listeners: the mouse down, the click and the touches, on the stage, in the capture phase', p.calls.listeners.map((l) => [l.type, l.capture]).sort(), [['click', true], ['mousedown', true], ['touchcancel', true], ['touchend', true], ['touchstart', true]]);
        expect('one poll, every 250 ms', p.calls.intervals.map((i) => i.ms), [H.POLL_MS]);
        expect('the poll is 250 ms', H.POLL_MS, 250);
    }
    // ---- a click on the game's button
    {
        const p = page({ on: 1 }).start();
        p.click();
        expect('a click on the button enters fullscreen', [p.calls.enter, p.calls.leave], [1, 0]);
        expect('and the game is told that the page is fullscreen', p.calls.told, [false, true]);
        p.click();
        expect('a click on it while fullscreen leaves', [p.calls.enter, p.calls.leave], [1, 1]);
        expect('and the game is told that it is not', p.calls.told, [false, true, false]);
    }
    // ---- clicks that are not on it
    {
        const p = page({ on: 0 }).start();
        p.click();
        expect('a click elsewhere asks for nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.game.on = 1;
        p.click({ button: 2 });
        expect('another mouse button asks for nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.state.ready = false;
        p.click();
        expect('a page whose game is not ready asks for nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        const n = page({ noGame: true }).start();
        n.click();
        expect('a game without the probe asks for nothing', [n.calls.enter, n.calls.leave], [0, 0]);
    }
    // ---- a finger on the button (a browser sends no click for it)
    {
        const p = page({ on: 0 }).start();
        const b = p.onButton();
        p.tap(b.x, b.y, 1000);
        expect('a tap on the button enters fullscreen, once', [p.calls.enter, p.calls.leave], [1, 0]);
        expect('and the game is told', p.calls.told, [false, true]);
        p.tap(b.x, b.y, 2000);
        expect('a second tap on it leaves', [p.calls.enter, p.calls.leave], [1, 1]);
        expect('and the game is told that too', p.calls.told, [false, true, false]);
        p.tap(b.x - 40, b.y, 3000);
        p.tap(b.x, b.y + 60, 3500);
        expect('a tap beside the button does nothing', [p.calls.enter, p.calls.leave], [1, 1]);
    }
    {
        // the page's own scale: the picture 346.75 CSS pixels wide at 22 px from the left (a phone held upright), and a canvas of another logical size
        const p = page({ box: { left: 21.984375, top: 75, width: 346.75, height: 195 } }).start();
        p.game.canvas = { w: 960, h: 540 };
        p.game.button = { x: 939, y: 7, w: 21, h: 23 };
        const x = 21.984375 + (939 + 10) * 346.75 / 960, y = 75 + (7 + 11) * 195 / 540;
        p.tap(x, y, 1000);
        expect('at another scale a tap on the button\'s place enters', [p.calls.enter, p.calls.leave], [1, 0]);
        p.tap(x - 1.2 * 346.75 / 960 * 21, y, 2000);
        expect('and a tap one button\'s width to its left (the Quit button) does not leave', [p.calls.enter, p.calls.leave], [1, 0]);
        p.tap(21.984375 + 937 * 346.75 / 960, y, 3000);
        expect('nor does the pixel just outside its left edge', [p.calls.enter, p.calls.leave], [1, 0]);
        p.tap(21.984375 + 939.2 * 346.75 / 960, y, 4000);
        expect('and the first pixel of it does', [p.calls.enter, p.calls.leave], [1, 1]);
    }
    {
        const p = page({ on: 0 }).start();
        const b = p.onButton();
        const left = 100 + 939, top = 80 + 7;                                              // the button's top left corner on the page (the scale is 1), 21 x 23 px
        p.tap(left - 3, b.y, 1000, left + 3, b.y, 1090);
        expect('a finger that came down 3 px off the button and lifts on it does nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.tap(left + 3, b.y, 2000, left - 3, b.y, 2090);
        expect('and one that came down on it and lifts 3 px off it does nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.tap(b.x, b.y, 3000, b.x, b.y, 3500);
        expect('a finger held for half a second (a hold: a right click) does nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.tap(left + 1, b.y, 4000, left + 12, b.y, 4090);
        expect('a finger that moved 11 px, though it stayed on the button, does nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.tap(left + 1, b.y, 4200, left + 11, b.y, 4290);
        expect('and one that moved 10 px is a tap', [p.calls.enter, p.calls.leave], [1, 0]);
        p.tap(left + 20.5, b.y, 4400);
        expect('the last pixel of the button leaves', [p.calls.enter, p.calls.leave], [1, 1]);
        p.tap(left + 21, b.y, 4500);
        expect('the edge after its right side does nothing', [p.calls.enter, p.calls.leave], [1, 1]);
        p.tap(left, b.y, 4550);
        expect('the left edge itself is on the button', [p.calls.enter, p.calls.leave], [2, 1]);
        p.tap(left - 0.5, b.y, 4560);
        expect('half a pixel left of it is not', [p.calls.enter, p.calls.leave], [2, 1]);
        p.tap(b.x, top, 4600);
        expect('the top edge itself is on the button', [p.calls.enter, p.calls.leave], [2, 2]);
        p.tap(b.x, top - 0.5, 4650);
        expect('half a pixel above it is not', [p.calls.enter, p.calls.leave], [2, 2]);
        p.tap(b.x, top + 22.5, 4700);
        expect('the last row of the button enters', [p.calls.enter, p.calls.leave], [3, 2]);
        p.tap(b.x, top + 23, 4800);
        expect('the edge after its bottom does nothing', [p.calls.enter, p.calls.leave], [3, 2]);
        p.calls.enter = 0; p.calls.leave = 0; p.state.fullscreen = false;
        p.touch('touchstart', [{ id: 1, x: b.x, y: b.y }], [{ id: 1, x: b.x, y: b.y }], 5000);
        p.touch('touchstart', [{ id: 1, x: b.x, y: b.y }, { id: 2, x: b.x - 80, y: b.y + 80 }], [{ id: 2, x: b.x - 80, y: b.y + 80 }], 5020);
        p.touch('touchend', [{ id: 2, x: b.x - 80, y: b.y + 80 }], [{ id: 1, x: b.x, y: b.y }], 5090);
        p.touch('touchend', [], [{ id: 2, x: b.x - 80, y: b.y + 80 }], 5120);
        expect('two fingers (a pinch) do nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.touch('touchstart', [{ id: 1, x: b.x - 80, y: b.y + 80 }], [{ id: 1, x: b.x - 80, y: b.y + 80 }], 5200);
        p.touch('touchstart', [{ id: 1, x: b.x - 80, y: b.y + 80 }, { id: 2, x: b.x, y: b.y }], [{ id: 2, x: b.x, y: b.y }], 5220);
        p.touch('touchend', [{ id: 1, x: b.x - 80, y: b.y + 80 }], [{ id: 2, x: b.x, y: b.y }], 5290);
        expect('a second finger that lifts on the button while the first stays down does nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.touch('touchend', [], [{ id: 1, x: b.x - 80, y: b.y + 80 }], 5400);
        p.touch('touchstart', [{ id: 3, x: b.x, y: b.y }], [{ id: 3, x: b.x, y: b.y }], 6000);
        p.touch('touchcancel', [], [{ id: 3, x: b.x, y: b.y }], 6050);
        p.touch('touchend', [], [{ id: 3, x: b.x, y: b.y }], 6090);
        expect('a touch that the browser took away does nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.touch('touchend', [], [{ id: 4, x: b.x, y: b.y }], 7000);
        expect('a lift with no landing does nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.game.button = null;
        p.tap(b.x, b.y, 8000);
        expect('a tap where the game has no button (a window is up, or the card) does nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.game.button = { x: 939, y: 7, w: 21, h: 23 };
        p.state.ready = false;
        p.tap(b.x, b.y, 9000);
        expect('a tap before the game runs does nothing', [p.calls.enter, p.calls.leave, p.game.broken], [0, 0, false]);
        p.state.ready = true;
        p.touch('touchstart', [{ id: 5, x: b.x, y: b.y }], [{ id: 5, x: b.x, y: b.y }], 10000);
        p.state.ready = false;
        p.touch('touchend', [], [{ id: 5, x: b.x, y: b.y }], 10090);
        expect('nor does a tap whose game went away before the lift', [p.calls.enter, p.calls.leave, p.game.broken], [0, 0, false]);
    }
    {
        // the click that a browser may send after a tap that was taken is the same press; a click later is a new one
        const p = page({ on: 1 }).start();
        const b = p.onButton();
        p.tap(b.x, b.y, 1000);
        p.click({ button: 0, timeStamp: 1095 });
        expect('the click that follows a taken tap is ignored', [p.calls.enter, p.calls.leave], [1, 0]);
        p.click({ button: 0, timeStamp: 1900 });
        expect('a click a moment later is a click of its own', [p.calls.enter, p.calls.leave], [1, 1]);
    }
    {
        // a tap in a black bar of the fullscreen is outside the canvas, so the game does not take the touch and the browser sends a click; the game's pointer rests on the button from the tap before
        const q = page({ on: 1 }).start();
        const b = q.onButton();
        q.tap(b.x - 80, b.y + 300, 1000);
        q.click({ button: 0, timeStamp: 1095 });
        expect('a click after a finger that lifted elsewhere does nothing, though the game\'s pointer is on the button', [q.calls.enter, q.calls.leave], [0, 0]);
        q.click({ button: 0, timeStamp: 1800 });
        expect('a click 710 ms after the lift is a mouse\'s and is judged as always', [q.calls.enter, q.calls.leave], [1, 0]);
        const r = page({ on: 1 }).start();
        r.touch('touchend', [], [{ id: 9, x: 5, y: 5 }], 3000);      // (a lift with no landing taken: still a finger's)
        r.click({ button: 0, timeStamp: 3010 });
        expect('so does a lift that was no tap at all', [r.calls.enter, r.calls.leave], [0, 0]);
        const m = page({ on: 1 }).start();
        m.click({ button: 0, timeStamp: 50 });
        expect('a click with no finger before it is judged as always', [m.calls.enter, m.calls.leave], [1, 0]);
    }
    // ---- a mouse press that began elsewhere
    {
        const p = page({ on: 0 }).start();
        p.down();                                                    // the pointer is on the map
        p.game.on = 1;                                               // dragged over the button and let go there
        p.click();
        expect('a click whose press began off the button does nothing', [p.calls.enter, p.calls.leave], [0, 0]);
        p.game.on = 1;
        p.down();
        p.click();
        expect('a press and a release on the button enter', [p.calls.enter, p.calls.leave], [1, 0]);
        p.game.on = 0;
        p.down();
        p.game.on = 1;
        p.game.clicks = 5;                                           // (the game has acted on its button: its own press began on it)
        p.click();
        expect('the game\'s own act counts even when the mouse down was read late', [p.calls.enter, p.calls.leave], [1, 1]);
        p.game.on = 1;
        p.down({ button: 2 });
        p.click();
        expect('another mouse button\'s press is not a press', [p.calls.enter, p.calls.leave], [2, 1]);
        const r = page({ on: 1 }).start();
        r.state.ready = false;
        r.down();
        r.state.ready = true;
        r.click();
        expect('a press before the game runs says nothing: the click is judged as always', [r.calls.enter, r.calls.leave, r.game.broken], [1, 0, false]);
    }
    // ---- a page that starts before its game runs (the game's program takes a moment to come up)
    {
        const p = page({ ready: false, on: 1 }).start();
        p.poll();
        p.poll();
        p.click();
        expect('nothing is asked or told while the game does not run', [p.game.broken, p.calls.told, p.calls.enter, p.calls.leave], [false, [], 0, 0]);
        p.state.ready = true;
        p.poll();
        expect('the first poll after it runs tells it (not fullscreen)', p.calls.told, [false]);
        p.click();
        expect('and then a click on its button enters fullscreen', [p.calls.enter, p.calls.leave], [1, 0]);
        p.poll();
        expect('and the game is told that the page is fullscreen', p.calls.told, [false, true]);
    }
    // ---- a tap that the game has taken whole before the click arrives
    {
        const p = page({ on: 0 }).start();
        p.game.clicks = 1;                                           // the game has seen the press and the release; its pointer state is back to 0
        p.click();
        expect('the game\'s count went up: the click is on the button', [p.calls.enter, p.calls.leave], [1, 0]);
        p.state.fullscreen = false;
        p.poll();
        p.click();
        expect('after a look the same count is not a new click', [p.calls.enter, p.calls.leave], [1, 0]);
    }
    // ---- the game is told whoever changes the fullscreen
    {
        const p = page().start();
        expect('the first look tells the game (not fullscreen)', p.calls.told, [false]);
        p.state.fullscreen = true;                                   // the bar's control, F11, the browser: not this button
        p.poll();
        expect('a poll after the page went fullscreen tells the game', p.calls.told, [false, true]);
        p.poll();
        expect('a poll with nothing changed tells it nothing more', p.calls.told, [false, true]);
        p.state.fullscreen = false;                                  // Esc
        p.poll();
        expect('and again when it leaves', p.calls.told, [false, true, false]);
    }
} catch (err) {
    console.log('ERROR ' + err.message);
    failed++;
}

console.log(checks + ' checks, ' + failed + ' failures');
process.exit(failed === 0 ? 0 : 1);
