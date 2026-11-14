// What the game page hands the hotkey buttons (web/shell.html: the call of AntsTouchKeys.forPage) and the settings menu (touchKeys), RUN as the page writes it, with every name that it uses made a marker or a
// model: the buttons know the game and the page only through this call, so when the page stops handing a piece, or hands the wrong one, they show where they must not (to a mouse, to a watcher) or never.
//   * touch(): the settings menu's own rule for a touch screen (the primary input cannot hover, or a finger was seen), not a second rule;
//   * watching(): the page's recording / live mode; ready(): the page's flag; running(): the game's _ants_match_running is 1; ask() and menu(): the game's _ants_probe and _ants_menu_get, -1 where it has none;
//   * the stage, the canvas and isFullscreen are the page's, relayout() is the page's relayout (not forced), store() is the browser's localStorage;
//   * the settings menu is handed the buttons' object, so that its Touch buttons row works.
// usage: node web_touch_keys_wiring_check.js <web/shell.html>     (exit 0: every row holds)
'use strict';
const fs = require('fs');
const path = process.argv[2];
if (!path) { console.log('usage: web_touch_keys_wiring_check.js shell.html'); process.exit(2); }
const text = fs.readFileSync(path, 'utf8');
function callOf(start) {
    const from = text.indexOf(start);
    if (from < 0) { console.log('FAIL the page has no call of ' + start); process.exit(1); }
    let depth = 0, end = -1;
    for (let i = text.indexOf('(', from); i < text.length; i++) {
        if (text[i] === '(') depth++;
        else if (text[i] === ')') { depth--; if (depth === 0) { end = i; break; } }
    }
    return text.slice(from, end + 1);
}
let failed = 0, checks = 0;
function expect(label, got, want) { checks++; if (JSON.stringify(got) !== JSON.stringify(want)) { failed++; console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); } }

const markers = {};
const marker = (name) => (markers[name] = markers[name] || Object.assign(function () { return undefined; }, { marker: name }));
function run(call, extra) {
    let captured = null;
    const hover = { none: true };
    const fakeWindow = Object.assign({ AntsTouchKeys: { forPage(page) { captured = page; return marker('antsTouchKeysObject'); } }, matchMedia: (q) => ({ matches: q === '(hover: none)' ? hover.none : false }), localStorage: marker('localStorage') }, extra && extra.window);
    const state = Object.assign({ ANTS_REPLAY_MODE: false, isReadyToPlay: true, Module: {} }, extra && extra.vars);
    const scope = new Proxy({}, { has: () => true, get: (t, name) => (name === Symbol.unscopables ? undefined : name === 'window' ? fakeWindow : name === 'document' ? marker('document') : name === 'undefined' ? undefined : name in state ? state[name] : marker(String(name))), set: (t, name, v) => { state[name] = v; return true; } });
    try { new Function('scope', 'with (scope) { ' + call.replace(/^AntsTouchKeys/, 'window.AntsTouchKeys') + '; }')(scope); } catch (e) { failed++; console.log('FAIL the call does not run: ' + e.message); }
    return { page: captured, hover, state, window: fakeWindow };
}

const a = run(callOf('AntsTouchKeys.forPage('), {});
if (!a.page) { console.log('FAIL nothing was handed over'); process.exit(1); }
const page = a.page;
expect('the page\'s own stage, canvas and fullscreen test are handed over', [page.stage && page.stage.marker, page.canvas && page.canvas.marker, page.isFullscreen && page.isFullscreen.marker], ['stageElement', 'canvas', 'isFullscreen']);
expect('the browser\'s storage is the page\'s localStorage', page.store().marker, 'localStorage');
expect('a touch screen (the primary input cannot hover): yes', page.touch(), true);
a.hover.none = false;
expect('a mouse (the primary input hovers): no (the settings menu\'s rule, not a second one)', page.touch(), false);
a.window.antsFinger = true;
expect('...unless a finger was seen last', page.touch(), true);
expect('watching is the page\'s recording / live mode', [run(callOf('AntsTouchKeys.forPage('), { vars: { ANTS_REPLAY_MODE: true } }).page.watching(), page.watching()], [true, false]);
expect('ready is the page\'s flag', [run(callOf('AntsTouchKeys.forPage('), { vars: { isReadyToPlay: false } }).page.ready(), page.ready()], [false, true]);
const game = { calls: [], _ants_match_running() { return 1; }, _ants_probe(n) { game.calls.push(['probe', n]); return 40 + n; }, _ants_menu_get(n) { game.calls.push(['menu', n]); return 70 + n; } };
const g = run(callOf('AntsTouchKeys.forPage('), { vars: { Module: game } }).page;
expect('running: the game\'s match is running (1)', g.running(), true);
game._ants_match_running = () => 0;
expect('...and is not when the game says 0', g.running(), false);
expect('ask() is the game\'s _ants_probe, menu() the game\'s _ants_menu_get', [g.ask(50), g.menu(101), game.calls], [90, 171, [['probe', 50], ['menu', 101]]]);
const old = run(callOf('AntsTouchKeys.forPage('), { vars: { Module: {} } }).page;
expect('a game that has none of them: running no, ask and menu -1', [old.running(), old.ask(50), old.menu(100)], [false, -1, -1]);
let relayouts = [];
const r = run(callOf('AntsTouchKeys.forPage('), { vars: { relayout: (force) => relayouts.push(force) } }).page;
r.relayout();
expect('relayout() is the page\'s, not forced', relayouts, [false]);

const m = (function () {
    let captured = null;
    const fakeWindow = { AntsGameMenu: { forPage(p) { captured = p; } }, AntsShapes: marker('AntsShapes'), AntsShapePicker: { create() { return null; } } };
    const scope = new Proxy({}, { has: () => true, get: (t, name) => (name === Symbol.unscopables ? undefined : name === 'window' ? fakeWindow : name === 'document' ? { getElementById() { return { textContent: 'v0.0.0' }; } } : name === 'undefined' ? undefined : name === 'ANTS_PAGE' ? { cancelsBrowserKey: marker('cancelsBrowserKey') } : marker(String(name))) });
    new Function('scope', 'with (scope) { ' + callOf('AntsGameMenu.forPage(').replace(/^AntsGameMenu/, 'window.AntsGameMenu') + '; }')(scope);
    return captured;
})();
expect('the settings menu is handed the buttons\' object (the Touch buttons row works only through it)', m && m.touchKeys && m.touchKeys.marker, 'antsTouchKeys');

// The page's layout round: the buttons first (their row or rail changes the stage's padding), then the chrome, then the fit, which reads the stage's padding (what the buttons leave of the stage is the picture's room)
function functionOf(name) {
    const from = text.indexOf('function ' + name + '(');
    if (from < 0) { failed++; console.log('FAIL the page has no function ' + name); return null; }
    let depth = 0, end = -1;
    for (let i = text.indexOf('{', from); i < text.length; i++) {
        if (text[i] === '{') depth++;
        else if (text[i] === '}') { depth--; if (depth === 0) { end = i; break; } }
    }
    return text.slice(from, end + 1);
}
function runFunction(name, vars) {
    const source = functionOf(name);
    if (!source) return null;
    const scope = new Proxy(vars, { has: () => true, get: (t, k) => (k === Symbol.unscopables ? undefined : k in t ? t[k] : globalThis[k]), set: (t, k, v) => { t[k] = v; return true; } });
    return new Function('scope', 'with (scope) { ' + source + '; return ' + name + '; }')(scope);
}
{
    const log = [];
    const relayout = runFunction('relayout', { window: { antsReplayPlaceBar: () => log.push('replay bar') }, antsTouchKeys: { refresh: (quiet) => log.push('buttons ' + quiet) }, measureChrome: () => log.push('chrome'), layoutBox: (force) => log.push('fit ' + force) });
    relayout(true);
    expect('the page\'s layout round: the buttons are laid out first (quietly: the page is laying out itself), then the chrome is measured, then the picture is fitted', log, ['replay bar', 'buttons true', 'chrome', 'fit true']);
    log.length = 0;
    runFunction('relayout', { window: {}, antsTouchKeys: null, measureChrome: () => log.push('chrome'), layoutBox: () => log.push('fit') })(false);
    expect('...and a page without the buttons (a recording) lays out as before', log, ['chrome', 'fit']);
    const fits = [];
    const box = { style: {} };
    const layoutBox = runFunction('layoutBox', {
        stageElement: { clientWidth: 800, clientHeight: 400 }, boxElement: box, pageBoxKey: '', pageDispatching: false, ANTS_ASPECT: 'aspect',
        window: { getComputedStyle: () => ({ paddingLeft: '72px', paddingRight: '2px', paddingTop: '3px', paddingBottom: '56px' }), devicePixelRatio: 2, dispatchEvent: () => true },
        Event: function () {}, snapBox: () => {}, keepLockedCursor: () => {},
        ANTS_PAGE: { fitBox: (w, h, dpr, aspect) => { fits.push([w, h, dpr, aspect]); return { bw: 1, bh: 1, cssW: w, cssH: h }; } },
    });
    layoutBox(false);
    expect('the fit works in what the stage\'s padding leaves (the rail beside the picture, the row under it): 800 - 72 - 2 wide, 400 - 3 - 56 high', [fits, box.style.width, box.style.height], [[[726, 341, 2, 'aspect']], '726px', '341px']);
}

// The fullscreen rule of the pointer (a mouse or a pen over the stage outside the picture is the game's pointer at the picture's nearest edge): over the hotkey buttons it is the page's, never an edge
{
    const marker = "if (handingOver || e.pointerType === 'touch' || isLocked() || !isFullscreen()) return;";
    const at = text.indexOf(marker);
    const from = text.lastIndexOf("window.addEventListener('pointermove', function (e) {", at);
    const to = text.indexOf('\n        }, true);', at);
    if (at < 0 || from < 0 || to < 0) { failed++; console.log('FAIL the page has no fullscreen pointer rule of the shape this check reads'); }
    else {
        const source = text.slice(from, to + '\n        }'.length).replace(/^window\.addEventListener\('pointermove', /, 'capture(');
        const run2 = (target, edgeForwarded) => {
            const calls = [];
            const vars = { handingOver: false, quietAfterRelease: null, edgeForwarded, edgeLast: null, isLocked: () => false, isFullscreen: () => true,
                stageElement: { contains: () => true }, boxElement: { contains: (t) => t === 'box' }, canvas: { getBoundingClientRect: () => ({ left: 100, top: 0, width: 600, height: 300 }) },
                ANTS_PAGE: { overTouchKeys: (t) => !!(t && t.keys), edgePixel: () => ({ x: 0, y: 150 }) },
                handToGame: (type) => calls.push(['hand', type]), handLeaveToGame: () => calls.push(['leave']) };
            let fn = null;
            const scope = new Proxy(vars, { has: () => true, get: (t, name) => (name === Symbol.unscopables ? undefined : name === 'capture' ? (f) => { fn = f; } : name in t ? t[name] : undefined), set: (t, name, v) => { t[name] = v; return true; } });
            new Function('scope', 'with (scope) { ' + source + '); }')(scope);
            fn({ pointerType: 'mouse', target, clientX: 50, clientY: 100, buttons: 0 });
            return calls;
        };
        expect('fullscreen: a mouse over the stage outside the picture is the game\'s pointer at the picture\'s edge', run2({ id: 'stage' }, false), [['hand', 'mousemove']]);
        expect('fullscreen: over the hotkey buttons it is not (nothing is handed to the game)', run2({ keys: true }, false), []);
        expect('fullscreen: ...and a pointer that was an edge a moment ago is gone for the game', run2({ keys: true }, true), [['leave']]);
        expect('fullscreen: over the picture itself the rule does nothing', run2('box', false), []);
    }
}

console.log('web touch keys wiring: ' + checks + ' checks, ' + failed + ' failures');
process.exit(failed ? 1 : 0);
