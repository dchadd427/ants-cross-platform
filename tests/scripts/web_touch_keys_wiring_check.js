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

console.log('web touch keys wiring: ' + checks + ' checks, ' + failed + ' failures');
process.exit(failed ? 1 : 0);
