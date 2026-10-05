// Runs the game page's OWN code for a touch screen (web/shell.html, everything between ANTS_TOUCH_BEGIN and ANTS_TOUCH_END), without a browser, with fakes for the page:
//   * what it registers, with which options: the gesture listeners are not passive and are on the canvas AND the document (iOS Safari), touchstart / touchend / touchcancel are passive (the page
//     never cancels, stops or delays a touch: the sound's unlock on touchend needs them), the box cancels contextmenu, and no touchmove listener is added;
//   * the browser's pinch (gesture events) is cancelled while a finger of the game is down or when the gesture is over the game, and ONLY then (the guide keeps the browser's own pinch);
//   * a touchcancel of a finger of the game tells the game (Module._ants_touch_cancel), one of any other finger does not, and a page without the game does not throw;
//   * a trackpad's pinch (gesture events and no fingers) still zooms the game through the wheel (25 % is one notch, the unit of the zoom's levels); with fingers on the game it does not
//     (their own pinch zooms it: the game's touch model);
//   * a lift that was missed cannot keep the count of fingers up (a touchend with no finger left on the page clears it).
// tests/scripts/test_web_touch.py runs this with node (the quick tier). usage: node web_touch_check.js web/shell.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');

function between(text, begin, end, path) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(path + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(path + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
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
if (!shellPath) { console.log('usage: web_touch_check.js shell.html'); process.exit(2); }

// An event target that remembers what was registered on it
function target(name) {
    const t = {
        name,
        listeners: [],
        addEventListener(type, fn, options) {
            const o = typeof options === 'object' && options !== null ? options : { capture: !!options };
            t.listeners.push({ type, fn, capture: !!o.capture, passive: o.passive, once: !!o.once });
        },
        of(type) { return t.listeners.filter((l) => l.type === type); },
    };
    return t;
}

// An event with spies on what a handler may do to it
function event(type, fields) {
    const e = Object.assign({ type, prevented: false, stopped: false, stoppedNow: false }, fields || {});
    e.preventDefault = () => { e.prevented = true; };
    e.stopPropagation = () => { e.stopped = true; };
    e.stopImmediatePropagation = () => { e.stoppedNow = true; };
    return e;
}

// The page: the box (it contains the canvas and whatever has `in_box`), the canvas, the window, the document, the game (Module) and the wheel event
function page(game) {
    const box = target('box');
    box.contains = (x) => x === box || (!!x && x.in_box === true);
    const canvas = target('canvas');
    canvas.in_box = true;
    canvas.dispatched = [];
    canvas.dispatchEvent = (e) => { canvas.dispatched.push(e); return true; };
    const win = target('window');
    const doc = target('document');
    const calls = { cancels: 0 };
    let Module;                                                               // 'running': the game answers; 'stopped': its call throws; 'none': no game yet (Module is undefined)
    if (game === 'running') Module = { _ants_touch_cancel() { calls.cancels++; } };
    else if (game === 'stopped') Module = { _ants_touch_cancel() { throw new Error('the game stopped'); } };
    function WheelEvent(type, init) { this.type = type; Object.assign(this, init); }
    return { box, canvas, win, doc, Module, WheelEvent, calls };
}

function load(text, p) {
    const block = between(text, 'ANTS_TOUCH_BEGIN', 'ANTS_TOUCH_END', shellPath);
    const args = ['boxElement', 'canvas', 'window', 'document', 'Module', 'WheelEvent'];
    const make = new Function(...args, block + '\nreturn antsGameTouches;');
    return make(p.box, p.canvas, p.win, p.doc, p.Module, p.WheelEvent);
}

const inside = { in_box: true };            // an element inside the game's box
const outside = { in_box: false };          // the guide, the header: anywhere else

function touch(id, where) { return { identifier: id, target: where || inside }; }

function fire(who, type, e) {
    for (const l of who.of(type)) l.fn(e);
    return e;
}

try {
    const text = fs.readFileSync(shellPath, 'utf8');

    // ---- what is registered, and how
    {
        const p = page('running');
        load(text, p);
        const kinds = [];
        for (const t of [p.box, p.canvas, p.win, p.doc]) for (const l of t.listeners) kinds.push(t.name + ':' + l.type);
        expect('no touchmove listener anywhere (the page cancels nothing that moves)', kinds.filter((k) => /touchmove/.test(k)), []);
        for (const type of ['gesturestart', 'gesturechange', 'gestureend']) {
            expect(type + ' on the document: one listener, not passive', p.doc.of(type).map((l) => l.passive), [false]);
            expect(type + ' on the canvas: one listener, not passive', p.canvas.of(type).map((l) => l.passive), [false]);
        }
        expect('touchstart on the box: passive, capture', p.box.of('touchstart').map((l) => [l.passive, l.capture]), [[true, true]]);
        expect('touchend on the window: passive, capture', p.win.of('touchend').map((l) => [l.passive, l.capture]), [[true, true]]);
        expect('touchcancel on the window: passive, capture', p.win.of('touchcancel').map((l) => [l.passive, l.capture]), [[true, true]]);
        expect('contextmenu on the box', p.box.of('contextmenu').length, 1);
        // none of the touch listeners ever cancels, stops or delays a touch event
        for (const type of ['touchstart', 'touchend', 'touchcancel']) {
            const who = type === 'touchstart' ? p.box : p.win;
            const e = event(type, { changedTouches: [touch(1)], touches: [touch(1)] });
            fire(who, type, e);
            expect(type + ' is never cancelled or stopped by the page', [e.prevented, e.stopped, e.stoppedNow], [false, false, false]);
        }
    }

    // ---- contextmenu
    {
        const p = page('running');
        load(text, p);
        const e = fire(p.box, 'contextmenu', event('contextmenu'));
        expect('a long press over the game is no context menu', e.prevented, true);
    }

    // ---- the browser's pinch (iOS Safari's gesture events): cancelled over the game and while a finger of the game is down, left alone elsewhere
    {
        const p = page('running');
        const count = load(text, p);
        for (const type of ['gesturestart', 'gesturechange', 'gestureend']) {
            const over = fire(p.doc, type, event(type, { target: inside, scale: 1.5 }));
            expect(type + ' over the game: cancelled', over.prevented, true);
            const away = fire(p.doc, type, event(type, { target: outside, scale: 1.5 }));
            expect(type + ' over the guide with no finger of the game: the browser pinches the page as it always did', away.prevented, false);
        }
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(7)], touches: [touch(7)] }));
        expect('a finger of the game is counted', count(), 1);
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(8)], touches: [touch(7), touch(8)] }));
        expect('two fingers are counted', count(), 2);
        for (const type of ['gesturestart', 'gesturechange', 'gestureend']) {
            const e = fire(p.doc, type, event(type, { target: outside, scale: 1.5 }));
            expect(type + ' with fingers on the game, whatever its target: cancelled', e.prevented, true);
        }
        fire(p.win, 'touchend', event('touchend', { changedTouches: [touch(7)], touches: [touch(8)] }));
        expect('a finger lifts', count(), 1);
        fire(p.win, 'touchend', event('touchend', { changedTouches: [touch(8)], touches: [] }));
        expect('the last finger lifts', count(), 0);
        const after = fire(p.doc, 'gesturechange', event('gesturechange', { target: outside, scale: 1.2 }));
        expect('with the fingers gone the page is pinchable again', after.prevented, false);
        // a finger that began elsewhere on the page (the guide, the header) is not the game's
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(9, outside)], touches: [touch(9, outside)] }));
        expect('a finger that began outside the box is not counted', count(), 0);
    }

    // ---- touchcancel: the game is told when a finger of the game was cancelled, and only then (a touch that lands with no other touch of the game down tells it as well: see below, so the
    // counts here are the ones that the touchcancel adds)
    {
        const p = page('running');
        const count = load(text, p);
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(3)], touches: [touch(3)] }));
        let told = p.calls.cancels;
        fire(p.win, 'touchcancel', event('touchcancel', { changedTouches: [touch(3)], touches: [] }));
        expect('a cancelled finger of the game: the game is told once', p.calls.cancels - told, 1);
        expect('... and it is no longer counted', count(), 0);
        told = p.calls.cancels;
        fire(p.win, 'touchcancel', event('touchcancel', { changedTouches: [touch(3)], touches: [] }));
        expect('the same cancel again (or SDL\'s lift after it): nothing more', p.calls.cancels - told, 0);
        fire(p.win, 'touchcancel', event('touchcancel', { changedTouches: [touch(4)], touches: [] }));
        expect('a cancelled finger that was never the game\'s: the game is not told', p.calls.cancels - told, 0);
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(5)], touches: [touch(5)] }));
        fire(p.win, 'touchend', event('touchend', { changedTouches: [touch(5)], touches: [] }));
        told = p.calls.cancels;
        fire(p.win, 'touchcancel', event('touchcancel', { changedTouches: [touch(5)], touches: [] }));
        expect('a finger that lifted and is then reported cancelled: nothing', p.calls.cancels - told, 0);
        // two fingers, one cancelled: the game is told (it forgets every finger: the model has no half measures)
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(10), touch(11)], touches: [touch(10), touch(11)] }));
        told = p.calls.cancels;
        fire(p.win, 'touchcancel', event('touchcancel', { changedTouches: [touch(10)], touches: [touch(11)] }));
        expect('one of two fingers cancelled: the game is told', p.calls.cancels - told, 1);
        expect('... and the other is still counted (it lifts as itself)', count(), 1);
    }
    {
        const none = page('none');                                            // no game yet (Module is undefined)
        load(text, none);
        fire(none.box, 'touchstart', event('touchstart', { changedTouches: [touch(1)], touches: [touch(1)] }));
        let threw = false;
        try { fire(none.win, 'touchcancel', event('touchcancel', { changedTouches: [touch(1)], touches: [] })); } catch (e) { threw = true; }
        expect('a cancel before the game runs is nothing', threw, false);
        const broken = page('stopped');                                      // a game that stopped (its call throws)
        load(text, broken);
        fire(broken.box, 'touchstart', event('touchstart', { changedTouches: [touch(1)], touches: [touch(1)] }));
        threw = false;
        try { fire(broken.win, 'touchcancel', event('touchcancel', { changedTouches: [touch(1)], touches: [] })); } catch (e) { threw = true; }
        expect('a game that has stopped does not make the page throw', threw, false);
    }

    // ---- a lift that is never delivered (the browser's own list is the truth): a touch that lands with no other touch of the game down starts the page's count and the game's fingers again
    {
        const p = page('running');
        const count = load(text, p);
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(1)], touches: [touch(1)] }));
        expect('the first touch of the game: the game is told that no other finger can be down (its cancel waits behind what SDL holds)', p.calls.cancels, 1);
        expect('(and it is counted)', count(), 1);
        // its touchend never comes. The next touch lands alone (the browser's list has this one only): the stale finger is forgotten by the page and the game
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(2)], touches: [touch(2)] }));
        expect('a touch that lands when the browser lists no other touch: the game is told again', p.calls.cancels, 2);
        expect('... and the stale finger is not counted any more (the page\'s own gesture guards must not stay on)', count(), 1);
        // a second finger lands while the first is down (the browser lists both): nobody is told, nothing is forgotten
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(3)], touches: [touch(2), touch(3)] }));
        expect('a second finger of the game: the game is not told', p.calls.cancels, 2);
        expect('... two are counted', count(), 2);
        // a finger of the page outside the box does not make this one a second finger
        fire(p.win, 'touchend', event('touchend', { changedTouches: [touch(2), touch(3)], touches: [] }));
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(4)], touches: [touch(9, outside), touch(4)] }));
        expect('a touch of the game while another finger rests on the guide: it is the game\'s first', p.calls.cancels, 3);
        // two touches that land in one event: both are new, none was down before
        fire(p.win, 'touchend', event('touchend', { changedTouches: [touch(4)], touches: [touch(9, outside)] }));
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(5), touch(6)], touches: [touch(5), touch(6)] }));
        expect('two touches that land together: told once, both counted', [p.calls.cancels, count()], [4, 2]);
        // a game that is not there, or that has stopped, does not make the page throw
        const none = page('none');
        load(text, none);
        let threw = false;
        try { fire(none.box, 'touchstart', event('touchstart', { changedTouches: [touch(1)], touches: [touch(1)] })); } catch (e) { threw = true; }
        expect('a first touch before the game runs is nothing', threw, false);
        const broken = page('stopped');
        load(text, broken);
        threw = false;
        try { fire(broken.box, 'touchstart', event('touchstart', { changedTouches: [touch(1)], touches: [touch(1)] })); } catch (e) { threw = true; }
        expect('a game that has stopped does not make the page throw at a first touch', threw, false);
    }

    // ---- a lift that was missed cannot keep the count up
    {
        const p = page('running');
        const count = load(text, p);
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(1)], touches: [touch(1)] }));
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(2)], touches: [touch(1), touch(2)] }));
        expect('two fingers', count(), 2);
        fire(p.win, 'touchend', event('touchend', { changedTouches: [touch(2)], touches: [] }));          // finger 1's own end was never heard: no finger is left on the page
        expect('a touchend with no finger left on the page clears the count', count(), 0);
    }

    // ---- a TRACKPAD's pinch (gesture events, no fingers): the wheel, 25 % a notch, cancelled for the page; with fingers on the game: no wheel
    {
        const p = page('running');
        const count = load(text, p);
        const start = fire(p.canvas, 'gesturestart', event('gesturestart', { target: p.canvas, scale: 1, clientX: 100, clientY: 80 }));
        expect('the pinch begins: cancelled for the page', start.prevented, true);
        const grow = fire(p.canvas, 'gesturechange', event('gesturechange', { target: p.canvas, scale: 1.25, clientX: 100, clientY: 80 }));
        expect('the pinch grows by 25 %: cancelled, and one wheel notch rolled away is sent to the game', [grow.prevented, p.canvas.dispatched.length], [true, 1]);
        const wheel = p.canvas.dispatched[0] || {};
        expect('the wheel event: -100 (rolled away zooms in), pixels, ctrl, at the pinch\'s place', [Math.round(wheel.deltaY), wheel.deltaMode, wheel.ctrlKey, wheel.clientX, wheel.clientY, wheel.type], [-100, 0, true, 100, 80, 'wheel']);
        fire(p.canvas, 'gesturechange', event('gesturechange', { target: p.canvas, scale: 1.25, clientX: 100, clientY: 80 }));
        expect('no change of the scale: no notch', p.canvas.dispatched.length, 1);
        const shrink = fire(p.canvas, 'gesturechange', event('gesturechange', { target: p.canvas, scale: 1.0, clientX: 100, clientY: 80 }));
        expect('the pinch shrinks back by 20 % (a notch the other way: 1 / 1.25)', [shrink.prevented, p.canvas.dispatched.length, Math.round((p.canvas.dispatched[1] || {}).deltaY)], [true, 2, 100]);
        const end = fire(p.canvas, 'gestureend', event('gestureend', { target: p.canvas, scale: 1 }));
        expect('the pinch ends: cancelled', end.prevented, true);
        // with fingers on the game
        fire(p.box, 'touchstart', event('touchstart', { changedTouches: [touch(1)], touches: [touch(1)] }));
        fire(p.canvas, 'gesturestart', event('gesturestart', { target: p.canvas, scale: 1 }));
        const live = fire(p.canvas, 'gesturechange', event('gesturechange', { target: p.canvas, scale: 2, clientX: 5, clientY: 5 }));
        expect('with a finger of the game down the pinch is cancelled and sends no wheel (the game\'s own pinch zooms)', [live.prevented, p.canvas.dispatched.length], [true, 2]);
        expect('(the count is 1)', count(), 1);
        fire(p.win, 'touchend', event('touchend', { changedTouches: [touch(1)], touches: [] }));
        fire(p.canvas, 'gesturestart', event('gesturestart', { target: p.canvas, scale: 1 }));
        fire(p.canvas, 'gesturechange', event('gesturechange', { target: p.canvas, scale: 1.25, clientX: 5, clientY: 5 }));
        expect('with the fingers gone the trackpad\'s pinch zooms through the wheel again', p.canvas.dispatched.length, 3);
    }
} catch (e) {
    console.log('ERROR ' + (e && e.stack ? e.stack : e));
    failed++;
}

console.log(checks + ' checks, ' + failed + ' failures');
process.exit(failed === 0 ? 0 : 1);
