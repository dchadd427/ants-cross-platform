// What the game page hands the settings menu (web/shell.html: the call of AntsGameMenu.forPage in antsLoadGameMenu), RUN as the page writes it, with every name that it uses made a marker: the menu's two rows that are the
// page's own, the Picture shape row and the Fullscreen mouse row, work only through `pickShape` and `lock` (and the small form's keys through `cancelsKey`), so the call must hand over the page's own antsPickShape and antsLock (not a copy, not nothing), and what the menu
// asks of the page must be there: the stage, the canvas, the module, the fullscreen calls. A page that stops handing one of them leaves its row out of the menu without a word (no other control has them any more).
// And the page's ready callback, run the same way, loads the menu once the game runs, in the frame of a match page as well (the frame's bar has the Settings button and the panel of a recording's own page).
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
const scope = new Proxy({}, { has: () => true, get: (target, name) => (name === Symbol.unscopables ? undefined : name === 'window' ? fakeWindow : name === 'document' ? fakeDocument : name === 'undefined' ? undefined : name === 'ANTS_PAGE' ? { cancelsBrowserKey: marker('cancelsBrowserKey') } : marker(String(name))) });
try {
    new Function('scope', 'with (scope) { ' + call.replace(/^AntsGameMenu/, 'window.AntsGameMenu') + '; }')(scope);
} catch (e) { failed++; console.log('FAIL the call does not run: ' + e.message); }
if (!captured) { console.log('FAIL nothing was handed over'); process.exit(1); }

expect('the Picture shape row: the page\'s own function antsPickShape is handed over', captured.pickShape && captured.pickShape.marker, 'antsPickShape');
expect('the Fullscreen mouse row: the page\'s own object antsLock is handed over', captured.lock && captured.lock.marker, 'antsLock');
expect('the small form keeps the game from every key through the page\'s own canceller of the browser\'s keys, handed over (a page that stops handing it leaves Ctrl+S and F1 to the browser)', captured.cancelsKey && captured.cancelsKey.marker, 'cancelsBrowserKey');
expect('the stage, the canvas and the toast are the page\'s', [captured.stage && captured.stage.marker, captured.canvas && captured.canvas.marker, captured.toast && captured.toast.marker], ['stageElement', 'canvas', 'toast']);
expect('the fullscreen and pointer calls are the page\'s', ['enter', 'leave', 'isFullscreen', 'isLocked', 'releaseLock', 'requestLock'].map((k) => captured[k] && captured[k].marker), ['enterFullscreen', 'leaveFullscreen', 'isFullscreen', 'isLocked', 'releaseLock', 'requestLock']);
expect('the menu can make the picture\'s picker from the page\'s table', [typeof captured.picker, typeof captured.shapes], ['function', 'function']);
expect('the game is asked through the page\'s Module, and the page says when the game is ready', [typeof captured.module, typeof captured.ready, captured.module() && captured.module().marker], ['function', 'function', 'Module']);
expect('the menu knows the mode (a recording, a live match)', [captured.replay && captured.replay.marker, captured.live && captured.live.marker], ['ANTS_REPLAY_MODE', 'ANTS_LIVE_MODE']);

// Where the page loads the menu: its ready callback (Module.postRun), RUN as the page writes it, loads the menu once the game runs, in the frame of a match page (play?replay=...&embed=1) as well as on the page's own address:
// the frame has the same Settings button and panel as a recording's own page. The names the callback uses are markers; the two the check watches are the page's flag for the frame and its loader of the menu.
const ready = text.indexOf('postRun: [function');
let readyEnd = -1;
if (ready >= 0) {
    depth = 0;
    for (let i = text.indexOf('{', ready); i < text.length; i++) {
        if (text[i] === '{') depth++;
        else if (text[i] === '}') { depth--; if (depth === 0) { readyEnd = i; break; } }
    }
}
if (readyEnd < 0) { failed++; console.log('FAIL the page has no ready callback (Module.postRun)'); }
else {
    const callback = text.slice(text.indexOf('function', ready), readyEnd + 1);
    const loadsOf = (embed) => {
        let loads = 0;
        const names = { ANTS_EMBED: embed, antsLoadGameMenu: function () { loads++; }, window: {}, progressContainer: { style: {} } };
        const around = new Proxy({}, { has: () => true, get: (target, name) => (name === Symbol.unscopables ? undefined : name === 'undefined' ? undefined : Object.prototype.hasOwnProperty.call(names, name) ? names[name] : function () { return undefined; }) });
        try { new Function('scope', 'with (scope) { (' + callback + ')(); }')(around); } catch (e) { failed++; console.log('FAIL the ready callback does not run: ' + e.message); }
        return loads;
    };
    expect('a game page that is ready loads the settings menu once', loadsOf(false), 1);
    expect('the frame of a match page loads the settings menu once too', loadsOf(true), 1);
}

console.log('web menu handover: ' + checks + ' checks, ' + failed + ' failures');
process.exit(failed ? 1 : 0);
