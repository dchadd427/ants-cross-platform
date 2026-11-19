// Runs the page's OWN code for the HUD's Fullscreen button (web/hud_fullscreen.js, AntsHudFullscreen.forPage, which web/shell.html calls), without a browser, with fakes for the page and a model of the
// game's button (Module._ants_probe 75 and 76), and checks:
//   * a click on the stage while the game says that its pointer is on its button enters fullscreen once, inside the click, and one while the page is fullscreen leaves it (the page's own enter and leave:
//     what the bar's Fullscreen control uses, so the two agree);
//   * a click that is not on the button (the game's own click on the map, a click when the game says -1) asks for nothing; so does a click of another mouse button and a click before the game is ready;
//   * a tap that the game has taken whole already (its count of the button's acts went up since the last look, its pointer state is back to 0) counts as a click on the button;
//   * the game is told whether the page is fullscreen at the first look and whenever that changes (the browser's own, the page's own, by the button or by something else), and every poll looks again;
//   * the script listens for the click on the stage (the capture phase, before the page's own handlers) and for nothing else.
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
    const game = { on: opts.on === undefined ? 0 : opts.on, clicks: 0, probe: opts.noGame ? -1 : 1 };
    const state = { ready: opts.ready !== false, fullscreen: !!opts.fullscreen };
    const calls = { enter: 0, leave: 0, told: [], listeners: [], intervals: [] };
    const stage = { addEventListener(type, fn, capture) { calls.listeners.push({ type, capture: !!capture, fn }); } };
    const ask = (what) => {
        if (game.probe < 0) return -1;
        if (what === 75) return game.on;
        if (what === 76) return game.clicks;
        return -1;
    };
    const p = { game, state, calls, stage };
    p.start = () => {
        p.api = H.forPage({
            stage, ask, ready: () => state.ready, isFullscreen: () => state.fullscreen,
            enter: () => { calls.enter++; state.fullscreen = true; },
            leave: () => { calls.leave++; state.fullscreen = false; },
            tell: (on) => { calls.told.push(on); },
            every: (fn, ms) => { calls.intervals.push({ fn, ms }); return calls.intervals.length; },
        });
        return p;
    };
    p.click = (event) => calls.listeners[0].fn(event || { button: 0 });
    p.poll = () => calls.intervals.forEach((i) => i.fn());
    return p;
}

try {
    // ---- what it listens to
    {
        const p = page().start();
        expect('one listener: the click, on the stage, in the capture phase', p.calls.listeners.map((l) => [l.type, l.capture]), [['click', true]]);
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
