// Runs the game page's OWN code for the pointer of a FULLSCREEN page (web/shell.html), without a browser:
//   * ANTS_PAGE.edgePixel / clientFor: the pixel of the canvas that the game must read for a pointer over a bar (the nearest pixel of the picture), and the client position that the
//     game's window system (Emscripten's SDL) reads as exactly that pixel;
//   * ANTS_PAGE.lockStart / moveLocked: the game's cursor while the browser holds the pointer for the game (the pointer lock): it moves by the distance of the mouse's motion and is held to
//     the picture, so every edge and corner scrolls the map and a click goes where the cursor is;
//   * ANTS_PAGE.lockSetting and the block of the control "Fullscreen mouse: Locked / Free" (everything between ANTS_LOCK_BEGIN and ANTS_LOCK_END): the choice that the browser remembers
//     under `ants.pointerlock`, which only "off" turns off.
// tests/scripts/test_web_edge.py runs this with node (the quick tier); tests/scripts/web_edge_check.py is the opt-in check of the same things in a real browser.
// usage: node web_edge_check.js web/shell.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');

// The lines between the line of `begin` and the line of `end`
function between(text, begin, end, path) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(path + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(path + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}

const THROWS = 'THROWS';
function makeStorage(initial) {
    const data = initial === THROWS ? {} : Object.assign({}, initial);
    const writes = [];
    const has = (key) => Object.prototype.hasOwnProperty.call(data, key);
    return {
        data,
        writes,
        getItem(key) { if (initial === THROWS) throw new Error('storage is blocked'); return has(key) ? data[key] : null; },
        setItem(key, value) { if (initial === THROWS) throw new Error('storage is blocked'); writes.push([key, String(value)]); data[key] = String(value); },
    };
}

function element(attributes) {
    const el = {
        attributes: Object.assign({}, attributes || {}),
        listeners: {},
        style: {},
        getAttribute(name) { return Object.prototype.hasOwnProperty.call(el.attributes, name) ? el.attributes[name] : null; },
        setAttribute(name, value) { el.attributes[name] = String(value); },
        addEventListener(type, fn) { el.listeners[type] = fn; },
        classList: { add() {}, contains() { return false; } },
    };
    return el;
}

let failed = 0;
let checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
        failed++;
    }
}

const shellPath = process.argv[2];
if (!shellPath) { console.log('usage: web_edge_check.js shell.html'); process.exit(2); }

// ---- ANTS_PAGE: the block that the other check runs, with the same fakes (it reads the address, the storage and two elements while it runs)
function loadPage(text) {
    const page = between(text, 'ANTS_PAGE_BEGIN', 'ANTS_PAGE_END', shellPath);
    const storage = makeStorage({});
    const win = { location: { search: '', protocol: 'https:', host: 'example.test', href: 'https://example.test/', assign() {} }, localStorage: storage, confirm() { return true; } };
    const stage = element({ 'data-aspect': '16:9' });
    const doc = {
        body: element(),
        getElementById(id) { if (id === 'game-stage') return stage; throw new Error('the page asked for #' + id); },
        querySelectorAll(selectorText) { if (selectorText === '.seg button[data-aspect]') return []; throw new Error('the page asked for ' + selectorText); },
    };
    return new Function('window', 'document', page + '\nreturn ANTS_PAGE;')(win, doc);
}

// What Emscripten's SDL reads for a pointer (library_html5.js, fillMouseEventData): HEAP32 of clientX - (rect.left | 0), i.e. the truncation of the difference to a whole number
function reading(left, client) {
    return Math.trunc(client - (left | 0));
}

try {
    const text = fs.readFileSync(shellPath, 'utf8');
    const P = loadPage(text);

    // ---- edgePixel: the pointer's own pixel on the picture, the nearest edge pixel over a bar
    const box = { left: 0, top: 45, width: 1440, height: 810 };        // a 16:10 screen of 1440 x 900: the picture 1440 x 810 with bars of 45 px above and below
    const edge = (cx, cy, b) => { b = b || box; return P.edgePixel(b.left, b.top, b.width, b.height, cx, cy); };
    expect('on the picture: the pointer\'s own pixel', edge(700, 300), { x: 700, y: 255 });
    expect('the top bar: y is the first row', edge(700, 10), { x: 700, y: 0 });
    expect('the top bar, its last row', edge(700, 44), { x: 700, y: 0 });
    expect('the first row of the picture is itself', edge(700, 45), { x: 700, y: 0 });
    expect('the bottom bar: y is the last row', edge(700, 880), { x: 700, y: 809 });
    expect('the last row of the picture is itself', edge(700, 854), { x: 700, y: 809 });
    expect('the first row of the bottom bar', edge(700, 855), { x: 700, y: 809 });
    expect('the bottom left corner of the bars', edge(0, 899), { x: 0, y: 809 });
    expect('the top right corner of the bars', edge(1439, 0), { x: 1439, y: 0 });
    expect('beyond the screen (a position that is not on it at all)', edge(-50, 5000), { x: 0, y: 809 });
    expect('beyond the screen, the other corner', edge(99999, -99999), { x: 1439, y: 0 });
    const pillar = { left: 160, top: 0, width: 1200, height: 900 };     // a 4:3 picture on a 1520 x 900 screen: bars of 160 px at the sides
    expect('the left bar of a pillarbox', edge(20, 450, pillar), { x: 0, y: 450 });
    expect('the right bar of a pillarbox', edge(1500, 450, pillar), { x: 1199, y: 450 });
    expect('the corner of a pillarbox', edge(3, 2, pillar), { x: 0, y: 2 });
    expect('a fractional position over the picture is cut like the game cuts it', edge(700.9, 300.9), { x: 700, y: 255 });
    expect('a box that stands 1/64 short of a whole pixel (the page puts it there: snapOffset) reads from the whole pixel', P.edgePixel(99.984375, 45.984375, 1280.1875, 720.1875, 100, 46), { x: 1, y: 1 });
    expect('a box of a size that is not whole: the last pixel is floor(size) - 1', P.edgePixel(0, 0, 1280.1875, 720.1875, 5000, 5000), { x: 1279, y: 719 });
    expect('a box that has no size is one pixel', P.edgePixel(0, 0, 0, 0, 30, 30), { x: 0, y: 0 });
    expect('a position that is not a number is the corner', P.edgePixel(0, 0, 100, 100, NaN, NaN), { x: 0, y: 0 });
    expect('a box left of the window (negative left, cut toward zero like Emscripten)', P.edgePixel(-3.5, 0, 100, 100, 10, 10), { x: 13, y: 10 });

    // clientFor: the position that the game reads as exactly that pixel, for every pixel of boxes at fractional places and sizes
    {
        let wrong = 0;
        let sample = '';
        const lefts = [0, 1, 68.984375, 99.984375, 127.90625, 0.015625, -3.5, 1520.5];
        const widths = [1, 2, 960, 1280, 1280.1875, 1440];
        for (const left of lefts) {
            for (const width of widths) {
                for (let x = 0; x < Math.floor(width); x += Math.max(1, Math.floor(width / 97))) {
                    const at = P.clientFor(left, left + 7, { x, y: x });
                    if (reading(left, at.clientX) !== x || reading(left + 7, at.clientY) !== x) { wrong++; if (!sample) sample = left + ' / ' + width + ' / ' + x; }
                }
                const last = Math.floor(width) - 1;
                const at = P.clientFor(left, left, { x: last, y: last });
                if (reading(left, at.clientX) !== last) { wrong++; if (!sample) sample = 'last ' + left + ' / ' + width; }
            }
        }
        expect('clientFor: the game reads exactly the pixel that was asked for (' + sample + ')', wrong, 0);
    }
    // ... and the two together: whatever the pointer's position, the game reads a pixel of the picture, and the pointer's own where it is on it
    {
        let wrong = 0;
        let sample = '';
        for (const left of [0, 68.984375, 99.984375, 400.5]) {
            for (const width of [960, 1280, 1280.1875, 1440]) {
                const height = Math.round(width * 9 / 16);
                for (let cx = left - 60; cx < left + width + 60; cx += 7) {
                    for (let cy = 10 - 60; cy < 10 + height + 60; cy += 11) {
                        const pixel = P.edgePixel(left, 10, width, height, cx, cy);
                        const inside = cx >= left && cx < left + Math.floor(width) && cy >= 10 && cy < 10 + Math.floor(height);
                        const back = P.clientFor(left, 10, pixel);
                        const ok = pixel.x >= 0 && pixel.x <= Math.floor(width) - 1 && pixel.y >= 0 && pixel.y <= Math.floor(height) - 1 &&
                            reading(left, back.clientX) === pixel.x && Math.trunc(back.clientY - 10) === pixel.y &&
                            (!inside || (pixel.x === reading(left, cx) && pixel.y === Math.trunc(cy - 10)));
                        if (!ok) { wrong++; if (!sample) sample = JSON.stringify([left, width, cx, cy, pixel]); }
                    }
                }
            }
        }
        expect('edgePixel with clientFor: always a pixel of the picture, the pointer\'s own over it (' + sample + ')', wrong, 0);
    }

    // ---- the pointer lock's cursor: the middle of the picture, moved by the mouse, held to the picture
    expect('the lock starts in the middle of the picture', P.lockStart(1440, 810), { x: 720, y: 405 });
    const W = 1440;
    const H = 810;
    const at = P.lockStart(W, H);
    expect('a motion moves the cursor by its distance (fractions kept)', P.moveLocked(at, 10.5, -3.25, W, H), { x: 730.5, y: 401.75 });
    expect('no motion, no move', P.moveLocked(at, 0, 0, W, H), at);
    expect('pushed against the left edge: x = 0', P.moveLocked({ x: 5, y: 100 }, -80, 0, W, H), { x: 0, y: 100 });
    expect('pushed against the top edge: y = 0', P.moveLocked({ x: 100, y: 5 }, 0, -80, W, H), { x: 100, y: 0 });
    const right = P.moveLocked({ x: 1430, y: 100 }, 80, 0, W, H);
    const bottom = P.moveLocked({ x: 100, y: 800 }, 0, 80, W, H);
    expect('pushed against the right edge: the last pixel (the game reads x = 1439)', [Math.trunc(right.x), right.y], [1439, 100]);
    expect('pushed against the bottom edge: the last pixel (the game reads y = 809)', [bottom.x, Math.trunc(bottom.y)], [100, 809]);
    const corner = P.moveLocked({ x: 1430, y: 800 }, 999, 999, W, H);
    expect('pushed into a corner: its pixel', [Math.trunc(corner.x), Math.trunc(corner.y)], [1439, 809]);
    expect('the corner stays the corner however far the mouse goes on', P.moveLocked(corner, 1e9, 1e9, W, H), corner);
    expect('and comes back at once the other way (no dead zone beyond the edge)', P.moveLocked(corner, -1, -1, W, H), { x: corner.x - 1, y: corner.y - 1 });
    expect('a motion that is not a number does not move the cursor', P.moveLocked(at, NaN, Infinity, W, H), at);
    expect('undefined does not either', P.moveLocked(at, undefined, undefined, W, H), at);
    {
        let p = P.lockStart(W, H);
        for (let i = 0; i < 1000; i++) p = P.moveLocked(p, 0.1, -0.05, W, H);               // a slow mouse: steps of less than a pixel add up
        expect('1000 steps of 0.1 and -0.05 are 100 and -50: the slow mouse is not lost', [Math.round(p.x * 1e6) / 1e6, Math.round(p.y * 1e6) / 1e6], [820, 355]);
        const back = P.moveLocked(p, -100, 50, W, H);
        expect('and the way back is the middle again', [Math.round(back.x * 1e6) / 1e6, Math.round(back.y * 1e6) / 1e6], [720, 405]);
    }
    {
        // every position that the cursor can have is a pixel of the picture whatever the motions are; sizes that are not whole count by their whole part
        let bad = 0;
        let sample = '';
        let seed = 42;
        const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
        for (const [w, h] of [[960, 540], [1440, 810], [1280.1875, 720.1875], [1, 1], [320, 180], [2560, 1440]]) {
            let p = P.lockStart(w, h);
            for (let i = 0; i < 4000; i++) {
                p = P.moveLocked(p, (rnd() - 0.5) * (i % 50 === 0 ? 4000 : 60), (rnd() - 0.5) * (i % 70 === 0 ? 4000 : 60), w, h);
                const px = Math.trunc(p.x);
                const py = Math.trunc(p.y);
                if (!(px >= 0 && px <= Math.floor(w) - 1 && py >= 0 && py <= Math.floor(h) - 1)) { bad++; if (!sample) sample = JSON.stringify([w, h, p]); }
            }
        }
        expect('4000 random motions in six sizes: the cursor is always on a pixel of the picture (' + sample + ')', bad, 0);
    }

    // ---- the setting
    expect('lockSetting: nothing remembered is on', P.lockSetting(null), true);
    expect('lockSetting: "off" is off', P.lockSetting('off'), false);
    for (const v of ['on', '', '0', 'false', 'OFF', 'Off', ' off', 'off ', 'no', undefined, 'junk']) expect('lockSetting: ' + JSON.stringify(v) + ' is on (only "off" turns the lock off)', P.lockSetting(v), true);

    // the block of the control, with a page of fakes
    function runLock(stored, canHaveLock) {
        const block = between(text, 'ANTS_LOCK_BEGIN', 'ANTS_LOCK_END', shellPath);
        const storage = makeStorage(stored);
        const win = { localStorage: storage };
        const on = element({ 'data-lock': 'on', 'aria-checked': 'true' });
        const off = element({ 'data-lock': 'off', 'aria-checked': 'false' });
        const parts = [element(), element()];
        const doc = {
            querySelectorAll(selectorText) {
                if (selectorText === '.seg button[data-lock]') return [on, off];
                if (selectorText === '.lock-part') return parts;
                throw new Error('the page asked for ' + selectorText);
            },
        };
        const canvas = canHaveLock === false ? {} : { requestPointerLock() {} };
        const result = new Function('window', 'document', 'canvas', 'ANTS_PAGE', block + '\nreturn { get on() { return ANTS_POINTER_LOCK; }, key: ANTS_LOCK_KEY };')(win, doc, canvas, P);
        return { result, storage, on, off, parts, shows: () => on.attributes['aria-checked'] + '/' + off.attributes['aria-checked'] };
    }
    {
        const r = runLock({});
        expect('control: the key', r.result.key, 'ants.pointerlock');
        expect('control: nothing remembered: Locked, and nothing was written by loading', [r.result.on, r.shows(), JSON.stringify(r.storage.writes)], [true, 'true/false', '[]']);
        r.off.listeners.click();
        expect('control: Free clicked: off, shown, remembered', [r.result.on, r.shows(), JSON.stringify(r.storage.writes)], [false, 'false/true', JSON.stringify([['ants.pointerlock', 'off']])]);
        const again = runLock(r.storage.data);
        expect('control: the next visit comes back Free', [again.result.on, again.shows()], [false, 'false/true']);
        again.on.listeners.click();
        expect('control: Locked clicked again: on, shown, remembered as "on"', [again.result.on, again.shows(), JSON.stringify(again.storage.writes)], [true, 'true/false', JSON.stringify([['ants.pointerlock', 'on']])]);
        expect('control: both parts are shown where the browser has a pointer lock', r.parts.map((p) => p.style.display).join(','), ',');
    }
    {
        const r = runLock({ 'ants.pointerlock': 'junk' });
        expect('control: a remembered value that is no choice is Locked', [r.result.on, r.shows()], [true, 'true/false']);
    }
    {
        const r = runLock(THROWS);                                                           // a private window: the storage refuses everything
        expect('control: a browser that refuses its storage still starts Locked', [r.result.on, r.shows()], [true, 'true/false']);
        r.off.listeners.click();
        expect('control: ... and the click still switches (nothing is remembered)', [r.result.on, r.shows()], [false, 'false/true']);
    }
    {
        const r = runLock({}, false);
        expect('control: a browser without a pointer lock does not show the control', r.parts.map((p) => p.style.display).join(','), 'none,none');
    }
} catch (e) {
    console.log('FAIL ' + e.message);
    failed++;
}

if (failed === 0) console.log('web edge: ' + checks + ' checks, 0 failures');
process.exit(failed === 0 ? 0 : 1);
