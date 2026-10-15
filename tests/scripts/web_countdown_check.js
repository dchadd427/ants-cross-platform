// Runs the page's OWN code for the countdown's Fullscreen button (web/countdown_button.js, AntsCountdown.forPage, which web/shell.html calls), without a browser, with fakes for the page and a
// model of the game's countdown card (Module._ants_probe 60 - 64), and checks:
//   * the button is shown while the game says that a countdown runs (probe 60 >= 0), the page's game is ready and the page is not fullscreen, and hidden in every other case (no countdown, the game
//     not ready, fullscreen of the browser's or the page's own, a game without the export);
//   * it lies over the place that the game gave (probes 61 - 64, the canvas's pixels) in the stage's coordinates, at least 120 x 44 CSS pixels, and stays inside the picture;
//   * a click on it asks for fullscreen once (the page's own enterFullscreen: the click is the user gesture that a browser wants) and hides it; a click when the page is fullscreen already asks for nothing;
//   * the button is made by the script (a child of the stage, with the page's .btn look), the poll runs every 250 ms, the card going away (the first turn) hides the button, and the page's other code is
//     not touched: no listener on the canvas, none on the document.
// tests/scripts/test_web_countdown.py runs this with node (the quick tier). usage: node web_countdown_check.js web/countdown_button.js     (exit 0: every check holds; every failure is printed)
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
if (!modulePath) { console.log('usage: web_countdown_check.js countdown_button.js'); process.exit(2); }
const C = require(require('path').resolve(modulePath));

// The page: a stage at (10, 20) that is 480 x 270 CSS pixels with the canvas filling it (the game's picture 960 x 540: 0.5 CSS pixel per game pixel), and a game with a countdown card
function page(opts) {
    opts = opts || {};
    const calls = { fullscreen: 0, intervals: [], listeners: 0, children: [] };
    const game = { left: 28, button: [405, 306, 150, 32], logical: [960, 540] };
    const rect = { left: 10, top: 20, width: 480, height: 270 };
    const canvas = { getBoundingClientRect: () => rect, addEventListener() { calls.listeners++; } };
    const stage = { getBoundingClientRect: () => rect, appendChild(el) { calls.children.push(el); } };
    const state = { ready: opts.ready !== false, fullscreen: !!opts.fullscreen };
    const made = [];
    const doc = {
        createElement(tag) {
            const el = { tag, style: {}, listeners: [], addEventListener(type, fn) { el.listeners.push({ type, fn }); calls.listeners++; } };
            made.push(el);
            return el;
        },
        addEventListener() { calls.listeners++; },
    };
    const ask = opts.noGame ? () => -1 : (what) => {
        if (what === 40) return game.logical[0];
        if (what === 41) return game.logical[1];
        if (what === 60) return game.left;
        if (what >= 61 && what <= 64) return game.button[what - 61];
        return -1;
    };
    const p = { calls, game, canvas, stage, state, doc, made, ask };
    p.start = () => {
        const intervals = calls.intervals;
        C.forPage({
            doc, stage, canvas, ask, ready: () => state.ready, isFullscreen: () => state.fullscreen,
            enter: () => { calls.fullscreen++; state.fullscreen = true; },
            every: (fn, ms) => { intervals.push({ fn, ms }); return intervals.length; },
        });
        p.button = made[made.length - 1];
        return p;
    };
    return p;
}

function poll(p) { for (const i of p.calls.intervals) i.fn(); }
function click(p) { for (const l of p.button.listeners) if (l.type === 'click') l.fn({}); }

try {

    // ---- what is registered
    {
        const p = page();
        p.start();
        expect('one poll, every 250 ms', p.calls.intervals.map((i) => i.ms), [250]);
        expect('one click listener on the button, nothing on the canvas or the document', [p.button.listeners.map((l) => l.type), p.calls.listeners], [['click'], 1]);
        expect('hidden until the poll has looked', [p.button.hidden, p.button.style.display], [true, 'none']);
        expect('made by the script: a button of the page\'s look with the id that the tests and the stylesheet know, a child of the stage', [p.button.tag, p.button.type, p.button.id, p.button.className, p.calls.children.length, p.calls.children[0] === p.button], ['button', 'button', 'countdown-fs', 'btn', 1, true]);
        expect('floats over the picture, above the canvas, wide enough for a finger', [p.button.style.position, p.button.style.zIndex, p.button.style.minWidth], ['absolute', '4', '120px']);
    }

    // ---- shown while a countdown runs, over the game's place
    {
        const p = page();
        p.start();
        poll(p);
        expect('a countdown is on: the button is shown', [p.button.hidden, p.button.style.display], [false, '']);
        // the game's place (405, 306) 150 x 32 in game pixels: 0.5 CSS pixel each, so 75 x 16 CSS pixels; the button is at least 120 x 44, centred on the place
        // centre: x = 10 - 10 + (405 + 75) * 0.5 = 240, y = 20 - 20 + (306 + 16) * 0.5 = 161 (the stage's own coordinates: the canvas is at the stage's corner)
        expect('over the game\'s place, at least 120 x 44, centred on it', [p.button.style.width, p.button.style.height, p.button.style.left, p.button.style.top], ['120px', '44px', '180px', '139px']);
        p.game.left = -1;
        poll(p);
        expect('the card is gone (the first turn ran): the button goes', [p.button.hidden, p.button.style.display], [true, 'none']);
        p.game.left = 3;
        poll(p);
        expect('a card again (a match that begins again): it is back', p.button.hidden, false);
        p.game.left = 0;
        poll(p);
        expect('0 seconds left is still a countdown (the card says "starting...")', p.button.hidden, false);
    }

    // ---- a big place is the button's size; a place at the edge is kept inside the picture
    {
        const p = page();
        p.game.button = [400, 400, 400, 100];
        p.start();
        poll(p);
        expect('a place that is bigger than a finger is the button\'s size', [p.button.style.width, p.button.style.height], ['200px', '50px']);
        const q = page();
        q.game.button = [900, 520, 150, 32];
        q.start();
        poll(q);
        const left = parseFloat(q.button.style.left), top = parseFloat(q.button.style.top), w = parseFloat(q.button.style.width), h = parseFloat(q.button.style.height);
        expect('a place at the edge is kept inside the picture (480 x 270 CSS px)', [left >= 0, top >= 0, left + w <= 480, top + h <= 270], [true, true, true, true]);
    }

    // ---- when it is hidden
    {
        const p = page({ ready: false });
        p.start();
        poll(p);
        expect('the game is not ready: no button', p.button.hidden, true);
        const f = page({ fullscreen: true });
        f.start();
        poll(f);
        expect('the page is fullscreen already (the browser\'s or the page\'s own): no button', f.button.hidden, true);
        const n = page({ noGame: true });
        n.start();
        poll(n);
        expect('a page whose game has no export: no button', n.button.hidden, true);
        const z = page();
        z.canvas.getBoundingClientRect = () => ({ left: 0, top: 0, width: 0, height: 0 });
        z.start();
        poll(z);
        expect('a canvas with no size yet: no button', z.button.hidden, true);
    }

    // ---- the click
    {
        const p = page();
        p.start();
        poll(p);
        click(p);
        expect('a click asks for fullscreen once', p.calls.fullscreen, 1);
        expect('and the button is gone (the page is fullscreen)', p.button.hidden, true);
        click(p);
        expect('a click when the page is fullscreen already asks for nothing', p.calls.fullscreen, 1);
        p.state.fullscreen = false;
        poll(p);
        expect('the page leaves fullscreen with the countdown still on: the button is back', p.button.hidden, false);
    }
} catch (err) {
    console.log('ERROR ' + err.message);
    failed++;
}

console.log(checks + ' checks, ' + failed + ' failures');
process.exit(failed === 0 ? 0 : 1);
