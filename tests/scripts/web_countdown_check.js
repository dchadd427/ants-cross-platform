// Runs the page's OWN code for the countdown's Fullscreen button (web/countdown_button.js, AntsCountdown.forPage, which web/shell.html calls), without a browser, with fakes for the page and a
// model of the game's countdown card (Module._ants_probe 60 - 69: the new card of 480 x 236 units at a scale of 1.5 canvas pixels to the unit, as at 720p and 1080p), and checks:
//   * the button is shown while the game says that a countdown runs (probe 60 >= 0), the page's game is ready and the page is not fullscreen, and hidden in every other case (no countdown, the game
//     not ready, fullscreen of the browser's or the page's own, a game without the export);
//   * it lies over the place that the game gave (probes 61 - 64, the canvas's pixels) in the stage's coordinates, the size of the game's place scaled with the picture (within 1 px), at least 28 CSS pixels tall and 80 wide, never wider than the card (probe 69), with its text 0.55 x the height (12 to 20 px), and stays inside the
//     card (its bottom above the card's bottom edge, probe 67, its top not over the rows, probe 68) and the picture; checked for a phone held sideways (844 x 390, 740 x 360, 667 x 375) and a desktop window (1280 x 800);
//   * a card too small for it has no button of its own: the button of the finger floor (28 CSS pixels) may not reach up over the rows, so a picture below about 365 CSS pixels (a phone upright) gets none and
//     the header's Full button is the way; it never flickers (shown from one size on, at every larger one), it comes and goes with the picture's size at the next poll, and a picture of a tablet's, a
//     phone's held sideways or a desktop's size is as it was;
//   * it fades with the card while the player looks round the map: the card's level (probe 66) is its opacity, nothing when the card is solid;
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

// The game's countdown card at `f` canvas pixels to the unit in the map view (17, 20, 762 x 500) of the 960 x 540 picture, for `rows` players (a model of countdown_card.hpp): where its button is (x, y, w, h),
// its bottom edge, the bottom of what lies above the button (the rows or the seconds) and its width
function cardAt(f, rows) {
    const w = Math.round(480 * f), h = Math.round(236 * f);
    const x = 17 + Math.floor((762 - w) / 2), y = 20 + Math.max(0, Math.floor((500 - h) / 2) - Math.round(16 * f));
    const total = rows * 21 + (rows - 1) * 9, lowest = Math.max(180, 74 + Math.floor((106 - total + 1) / 2) + total);
    return { button: [x + Math.round(364 * f), y + Math.round(196 * f), Math.round(94 * f), Math.round(22 * f)], bottom: y + h, content: y + Math.round(lowest * f), width: w };
}

// The page: a stage at (10, 20) that is 576 x 324 CSS pixels with the canvas filling it (the game's picture 960 x 540: 0.6 CSS pixel per game pixel, the least scale at which the card holds the button), and a game with a countdown card
function page(opts) {
    opts = opts || {};
    const calls = { fullscreen: 0, intervals: [], listeners: 0, children: [] };
    const game = Object.assign({ left: 28, logical: [960, 540], opacity: 255 }, cardAt(1.5, 4));
    // opts.win: a window [w, h] that the picture (960 x 540 game pixels) is fitted into; by default the stage is 576 x 324 CSS pixels at (10, 20)
    let rect = { left: 10, top: 20, width: 576, height: 324 };
    if (opts.win) { const f = Math.min(opts.win[0] / 960, opts.win[1] / 540); rect = { left: (opts.win[0] - 960 * f) / 2, top: (opts.win[1] - 540 * f) / 2, width: 960 * f, height: 540 * f }; }
    const canvas = { getBoundingClientRect: () => rect, addEventListener() { calls.listeners++; } };
    const stage = { getBoundingClientRect: () => (opts.win ? { left: 0, top: 0, width: opts.win[0], height: opts.win[1] } : rect), appendChild(el) { calls.children.push(el); } };
    const resize = (width) => { rect = { left: 10, top: 20, width, height: width * 540 / 960 }; };      // (the page's picture changes size: a turn of the phone, a window dragged)
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
        if (what === 66) return game.opacity;
        if (what === 67) return game.bottom;
        if (what === 68) return game.content;
        if (what === 69) return game.width;
        return -1;
    };
    const p = { calls, game, canvas, stage, state, doc, made, ask, resize };
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
        expect('one poll every 250 ms and one every 60 ms for the fade', p.calls.intervals.map((i) => i.ms), [250, 60]);
        expect('one click listener on the button, nothing on the canvas or the document', [p.button.listeners.map((l) => l.type), p.calls.listeners], [['click'], 1]);
        expect('hidden until the poll has looked', [p.button.hidden, p.button.style.display], [true, 'none']);
        expect('made by the script: a button of the page\'s look with the id that the tests and the stylesheet know, a child of the stage', [p.button.tag, p.button.type, p.button.id, p.button.className, p.calls.children.length, p.calls.children[0] === p.button], ['button', 'button', 'countdown-fs', 'btn', 1, true]);
        expect('floats over the picture, above the canvas, with the page\'s .btn minimums and padding set aside', [p.button.style.position, p.button.style.zIndex, p.button.style.minWidth, p.button.style.minHeight, p.button.style.padding], ['absolute', '4', '0px', '0px', '0px 4px']);
    }

    // ---- shown while a countdown runs, over the game's place
    {
        const p = page();
        p.start();
        poll(p);
        expect('a countdown is on: the button is shown', [p.button.hidden, p.button.style.display], [false, '']);
        // the game's place (584, 363) 141 x 33 canvas pixels: 0.6 CSS pixel each, so 84.6 x 19.8 CSS pixels; the button is 84.6 wide and 28 tall (the height floor), centred on it, its top kept under what lies over it
        // (the rows' bottom at 344, 19 canvas pixels over the place: 206.4) and its bottom 2 px above the card's bottom edge (423: 253.8, 27 canvas pixels under the place's bottom)
        const near = (got, want) => Math.abs(parseFloat(got) - want) <= 0.01;
        expect('over the game\'s place: 84.6 wide, 28 tall (the height floor), centred across, its text 15.4 px', [near(p.button.style.width, 84.6), p.button.style.height, near(p.button.style.left, 350.4), near(p.button.style.top, 213.7), p.button.style.fontSize], [true, '28px', true, true, '15.4px']);
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

    // ---- the button is the game's place scaled with the picture: a phone held sideways, a desktop window
    {
        const wins = [[844, 390], [740, 360], [667, 375], [1280, 800]];
        for (const win of wins) {
            const p = page({ win });
            p.start();
            poll(p);
            const f = Math.min(win[0] / 960, win[1] / 540);
            const [bx, by, bw, bh] = p.game.button;
            const slotW = bw * f, slotH = bh * f;                    // the slot in CSS pixels
            const wantH = Math.max(28, slotH), wantW = Math.max(80, slotW);
            const w = parseFloat(p.button.style.width), h = parseFloat(p.button.style.height), fs = parseFloat(p.button.style.fontSize);
            const left = parseFloat(p.button.style.left), top = parseFloat(p.button.style.top);
            const label = win.join('x') + ' (slot ' + slotW.toFixed(1) + ' x ' + slotH.toFixed(1) + ' CSS px)';
            expect(label + ': the button\'s width is the scaled slot\'s, 80 at least (within 1 px)', Math.abs(w - wantW) <= 1, true);
            expect(label + ': the button\'s height is the scaled slot\'s, 28 at least (within 1 px)', Math.abs(h - wantH) <= 1, true);
            expect(label + ': at least 28 CSS pixels tall', h >= 28, true);
            expect(label + ': the text is 0.55 x the height (12 to 20 px) on one line', [Math.abs(fs - Math.min(20, Math.max(12, h * 0.55))) <= 0.1, p.button.style.whiteSpace], [true, 'nowrap']);
            expect(label + ': no page minimum is left to grow the box, a little side padding', [p.button.style.boxSizing, p.button.style.minWidth, p.button.style.minHeight, p.button.style.padding], ['border-box', '0px', '0px', '0px 4px']);
            expect(label + ': never wider than the card', w <= p.game.width * f + 0.01, true);
            const rectLeft = (win[0] - 960 * f) / 2, rectTop = (win[1] - 540 * f) / 2;
            expect(label + ': inside the picture', [left >= rectLeft - 0.01, top >= rectTop - 0.01, left + w <= rectLeft + 960 * f + 0.01, top + h <= rectTop + 540 * f + 0.01], [true, true, true, true]);
            const cx = rectLeft + (bx + bw / 2) * f;
            expect(label + ': centred across on the game\'s place', Math.abs(left + w / 2 - cx) <= 1, true);
            const cardBottom = rectTop + p.game.bottom * f, rowsBottom = rectTop + p.game.content * f;
            expect(label + ': inside the card: its bottom at least 2 px above the card\'s bottom edge', top + h <= cardBottom - 2 + 0.01, true);
            expect(label + ': and its top not over the rows', top >= rowsBottom - 0.01, true);
            if (win[0] >= 900) expect(label + ': the desktop: exactly the scaled slot', [Math.abs(w - slotW) <= 1, Math.abs(h - slotH) <= 1], [true, true]);
        }
        const ph = page({ win: [844, 390] });
        ph.start();
        poll(ph);
        expect('a phone held sideways: the button is a finger\'s size: 28 tall, 80 or more wide', parseFloat(ph.button.style.height) === 28 && parseFloat(ph.button.style.width) >= 80, true);
    }

    // ---- a card too small for the button has none: a phone upright, a tiny picture
    {
        // the picture in the page of a 390 pt phone held upright is 344 CSS px wide (0.36), of a 402 pt one 356: the card is 258 to 266 px wide, the slot 12 px high
        for (const win of [[344, 194], [356, 200], [120, 68]]) {
            const p = page({ win });
            p.start();
            poll(p);
            expect(win.join('x') + ': the card has no room for a finger\'s button: none is shown (the header\'s Full is the way)', [p.button.hidden, p.button.style.display], [true, 'none']);
        }
        // a phone held sideways with the header on (about 404 wide), a tablet and a desktop have it, inside the card
        for (const win of [[404, 227], [576, 324], [640, 360], [800, 450], [960, 540], [1280, 720], [1600, 900]]) {
            const p = page({ win });
            p.start();
            poll(p);
            expect(win.join('x') + ': a picture of a phone\'s held sideways, a tablet\'s or a desktop\'s size has the button', p.button.hidden, false);
        }
        // the rule itself: shown, the button never reaches up over the rows; hidden only where it would; never a flicker
        let seen = false, flicker = false;
        for (let width = 200; width <= 1400; width += 8) {
            const p = page({ win: [width, width * 540 / 960] });
            p.start();
            poll(p);
            const f = width / 960;
            const shown = !p.button.hidden;
            const [, by, , bh] = p.game.button;
            if (shown) {
                seen = true;
                const rowsBottom = p.canvas.getBoundingClientRect().top + p.game.content * f;
                const top = parseFloat(p.button.style.top);
                if (!(top >= rowsBottom - 0.01)) { expect(width + ' wide: the button is not over the rows', top, '>= ' + rowsBottom); }
            } else if (seen) flicker = true;
            const intrusion = 28 - (bh * f + (p.game.bottom - by - bh + by - p.game.content) * f - 2);
            expect(width + ' wide: shown exactly where the floor-sized button fits between the rows and the card\'s bottom edge', shown, intrusion <= 0);
        }
        expect('shown from one size on and at every larger one (no flicker)', [seen, flicker], [true, false]);
        // it follows the picture: a phone turned (the picture grows), a window dragged
        const q = page();
        q.start();
        poll(q);
        expect('a picture of 0.6: the button is there', q.button.hidden, false);
        q.resize(344);
        poll(q);
        expect('the picture shrinks to a phone upright\'s size: the next poll takes it away', [q.button.hidden, q.button.style.display], [true, 'none']);
        q.resize(960);
        poll(q);
        expect('and it grows again: the button is back, over the place', [q.button.hidden, q.button.style.display, q.button.style.height], [false, '', '33px']);
    }

    // ---- fewer players: the rows end higher, the card is the same size; a card of another scale; the button follows what the game says
    {
        for (const rows of [1, 2, 3]) {
            const p = page();
            Object.assign(p.game, cardAt(1.5, rows));
            p.start();
            poll(p);
            expect(rows + ' rows: the button is there, under the rows', [p.button.hidden, parseFloat(p.button.style.top) >= p.game.content * 0.6 - 0.01], [false, true]);
        }
        const one = page();
        Object.assign(one.game, cardAt(1.0, 4));
        one.start();
        poll(one);
        const [, , bw, bh] = one.game.button;
        expect('a card of one canvas pixel to the unit (a window of the picture\'s own size): the place is 94 x 22 and the button its scaled size, 28 tall at least', [bw, bh, parseFloat(one.button.style.height) >= 28], [94, 22, true]);
    }

    // ---- the card is see-through while the player looks round: the button takes the card's level
    {
        const p = page();
        p.start();
        poll(p);
        expect('a solid card: the button has no opacity of its own', p.button.style.opacity, '');
        p.game.opacity = 219;
        for (const i of p.calls.intervals) if (i.ms === 60) i.fn();
        expect('the card is see-through (219 of 255): the button has that opacity, at the next fade poll', p.button.style.opacity, '0.86');
        p.game.opacity = 255;
        for (const i of p.calls.intervals) if (i.ms === 60) i.fn();
        expect('solid again: nothing', p.button.style.opacity, '');
        p.game.opacity = -1;
        for (const i of p.calls.intervals) if (i.ms === 60) i.fn();
        expect('no card: nothing', p.button.style.opacity, '');
        const q = page({ ready: false });
        q.start();
        q.game.opacity = 100;
        for (const i of q.calls.intervals) if (i.ms === 60) i.fn();
        expect('a game that is not ready is not asked', q.button.style.opacity, undefined);
    }

    // ---- a big place is the button's size; a place at the edge is kept inside the picture
    {
        const p = page();
        p.game.button = [400, 400, 400, 100];
        p.game.bottom = 540;
        p.game.content = 380;
        p.game.width = 960;
        p.start();
        poll(p);
        expect('a place that is bigger than a finger is the button\'s size', [p.button.style.width, p.button.style.height], ['240px', '60px']);
        expect('and its text is 20 px at most', p.button.style.fontSize, '20px');
        const q = page();
        q.game.button = [900, 520, 150, 32];
        q.game.bottom = 560;
        q.game.content = 500;
        q.start();
        poll(q);
        const left = parseFloat(q.button.style.left), top = parseFloat(q.button.style.top), w = parseFloat(q.button.style.width), h = parseFloat(q.button.style.height);
        expect('a place at the edge is kept inside the picture (576 x 324 CSS px)', [left >= 0, top >= 0, left + w <= 576, top + h <= 324], [true, true, true, true]);
        const bad = page();
        bad.game.bottom = -1;
        bad.start();
        poll(bad);
        expect('a card that says no edge (an older game): no button', bad.button.hidden, true);
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
