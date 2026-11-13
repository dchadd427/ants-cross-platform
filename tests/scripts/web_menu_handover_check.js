// What the game page hands the settings menu (web/shell.html: the call of AntsGameMenu.forPage in antsLoadGameMenu), RUN as the page writes it, with every name that it uses made a marker: the menu's two rows that are the
// page's own, the Picture shape row and the Fullscreen mouse row, work only through `pickShape` and `lock`, so the call must hand over the page's own antsPickShape and antsLock (not a copy, not nothing), and what the menu
// asks of the page must be there: the stage, the canvas, the module, the fullscreen calls. A page that stops handing one of them leaves its row out of the menu without a word (no other control has them any more).
// usage: node web_menu_handover_check.js <web/shell.html>     (exit 0: every row holds)
'use strict';
const fs = require('fs');
const path = process.argv[2];
if (!path) { console.log('usage: web_menu_handover_check.js shell.html'); process.exit(2); }
const text = fs.readFileSync(path, 'utf8');
const from = text.indexOf('AntsGameMenu.forPage(');
if (from < 0) { console.log('FAIL the page has no call of AntsGameMenu.forPage'); process.exit(1); }
let depth = 0, end = -1;
for (let i = text.indexOf('(', from); i < text.length; i++) {          // the call's own parentheses (the object has no string with one in it)
    if (text[i] === '(') depth++;
    else if (text[i] === ')') { depth--; if (depth === 0) { end = i; break; } }
}
const call = text.slice(from, end + 1);
let failed = 0, checks = 0;
function expect(label, got, want) { checks++; if (JSON.stringify(got) !== JSON.stringify(want)) { failed++; console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); } }

const markers = {};
const marker = (name) => (markers[name] = markers[name] || Object.assign(function () { return undefined; }, { marker: name }));
let captured = null;
const fakeWindow = { AntsGameMenu: { forPage(page) { captured = page; } }, AntsShapes: marker('AntsShapes'), AntsShapePicker: { create() { return null; } } };
const fakeDocument = { getElementById() { return { textContent: 'v0.0.0' }; } };
const scope = new Proxy({}, { has: () => true, get: (target, name) => (name === Symbol.unscopables ? undefined : name === 'window' ? fakeWindow : name === 'document' ? fakeDocument : name === 'undefined' ? undefined : marker(String(name))) });
try {
    new Function('scope', 'with (scope) { ' + call.replace(/^AntsGameMenu/, 'window.AntsGameMenu') + '; }')(scope);
} catch (e) { failed++; console.log('FAIL the call does not run: ' + e.message); }
if (!captured) { console.log('FAIL nothing was handed over'); process.exit(1); }

expect('the Picture shape row: the page\'s own function antsPickShape is handed over', captured.pickShape && captured.pickShape.marker, 'antsPickShape');
expect('the Fullscreen mouse row: the page\'s own object antsLock is handed over', captured.lock && captured.lock.marker, 'antsLock');
expect('the stage, the canvas and the toast are the page\'s', [captured.stage && captured.stage.marker, captured.canvas && captured.canvas.marker, captured.toast && captured.toast.marker], ['stageElement', 'canvas', 'toast']);
expect('the fullscreen and pointer calls are the page\'s', ['enter', 'leave', 'isFullscreen', 'isLocked', 'releaseLock', 'requestLock'].map((k) => captured[k] && captured[k].marker), ['enterFullscreen', 'leaveFullscreen', 'isFullscreen', 'isLocked', 'releaseLock', 'requestLock']);
expect('the menu can make the picture\'s picker from the page\'s table', [typeof captured.picker, typeof captured.shapes], ['function', 'function']);
expect('the game is asked through the page\'s Module, and the page says when the game is ready', [typeof captured.module, typeof captured.ready, captured.module() && captured.module().marker], ['function', 'function', 'Module']);
expect('the menu knows the mode (a recording, a live match)', [captured.replay && captured.replay.marker, captured.live && captured.live.marker], ['ANTS_REPLAY_MODE', 'ANTS_LIVE_MODE']);

console.log('web menu handover: ' + checks + ' checks, ' + failed + ' failures');
process.exit(failed ? 1 : 0);
