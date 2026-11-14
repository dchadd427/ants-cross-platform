// The shapes of the picture, as the web pages know them: ONE table, read by the game page (web/shell.html), the front page (web/lobby.html) and the settings menu (web/game_menu.js).
// The game has the same rows in include/ants_app/canvas_layout.hpp (kAspectTable); tests/scripts/test_web_shapes.py fails when the two tables differ, and when a page keeps a list of its own.
//   * A row is a shape: its name (the text of `?aspect=`, `--aspect` and the remembered value: "4:3"), its words, the game's canvas in pixels and the widest the page's slot gets. The rows are in the order of the
//     dropdowns, narrowest to widest, and a shape's place in them is the number that the game's `ants_set_aspect` takes.
//   * What follows from a row is worked out here and nowhere else: the shape in the smallest whole numbers (the page's box is a whole number of those steps, and the CSS slot has that ratio), the entries of the dropdowns ("16:9 \u00b7 960 x 540"; the game page's Picture window and the settings menu make their list from the rows, web/shape_picker.js), which shape fills a screen (and the line that says so), and which shape a first visit starts in.
// ES5, no module system: a page gets the global AntsShapes, node gets module.exports (tests/scripts/web_aspect_key_check.js and the other checks of the pages run the pages' code with it).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsShapes = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    // THE TABLE (compared with kAspectTable, field by field: name, label, width, height). `slot` is the widest the game's slot gets in CSS pixels (the page only): 960 for the classic picture, the page's 1280
    // for the others, and a canvas wider than that its own width, so that a monitor shows it at 1x (tests/scripts/test_web_shapes.py states the rule).
    // SHAPE_TABLE_BEGIN
    var TABLE = [
        { name: '4:3', label: 'Classic 4:3', width: 640, height: 480, slot: 960 },
        { name: '3:2', label: '3:2', width: 960, height: 640, slot: 1280 },
        { name: '16:10', label: '16:10', width: 960, height: 600, slot: 1280 },
        { name: '16:9', label: '16:9', width: 960, height: 540, slot: 1280 },
        { name: '20:9', label: '20:9', width: 1200, height: 540, slot: 1280 },
        { name: '21:9', label: '21:9', width: 1260, height: 540, slot: 1280 },
        { name: '32:9', label: '32:9', width: 1920, height: 540, slot: 1920 }
    ];
    // SHAPE_TABLE_END

    // Two questions about a screen, asked of every row of the table (a row added later counts with no other change): forScreen, the selector's "fills your screen" tag, which has a cutoff
    // (the nearest shape counts when it is within 8 per cent of the screen's shape), and automatic, the shape that Automatic shows, which has none.
    var FILLS_WITHIN = 0.08;
    var FALLBACK = '16:9';
    var AUTO = 'auto';                       // the name that the settings list gives its Automatic row (no shape is called so)
    var WAIT_MS = 300;                       // the quiet that a change of the window's size or way needs before the picture is picked again

    function gcd(a, b) { return b === 0 ? a : gcd(b, a % b); }

    var ROWS = TABLE.map(function (row, place) {
        var step = gcd(row.width, row.height);
        return { name: row.name, label: row.label, text: row.label + ' \u00b7 ' + row.width + ' x ' + row.height, width: row.width, height: row.height, slot: row.slot, place: place, stepW: row.width / step, stepH: row.height / step };
    });
    var BY_NAME = {};
    ROWS.forEach(function (row) { BY_NAME[row.name] = row; });

    // The row of a shape's name, or null for a text that is no shape (a number, a blank behind a name, a name of the object's own: nothing but the names counts)
    function rowOf(text) {
        return (typeof text === 'string' && Object.prototype.hasOwnProperty.call(BY_NAME, text)) ? BY_NAME[text] : null;
    }

    // The name if the text is one of the shapes, else null (an address, a remembered value)
    function parse(text) {
        var row = rowOf(text);
        return row ? row.name : null;
    }

    // The shape's place in the table (what the game's ants_set_aspect takes), -1 for a text that is no shape
    function placeOf(text) {
        var row = rowOf(text);
        return row ? row.place : -1;
    }

    // The names, left to right
    function names() {
        return ROWS.map(function (row) { return row.name; });
    }

    // The row whose shape is the closest to `ratio` (a ratio of ratios; a tie goes to the first row, the narrower) and how far off it is (0.05: 5 per cent)
    function closest(ratio) {
        var best = null;
        var bestOff = Infinity;
        ROWS.forEach(function (row) {
            var shape = row.stepW / row.stepH;
            var off = Math.max(shape / ratio, ratio / shape) - 1;
            if (off < bestOff) { bestOff = off; best = row; }
        });
        return { row: best, off: bestOff };
    }

    function isSize(w, h) { return w > 0 && h > 0 && isFinite(w) && isFinite(h); }

    // The shape that fills a screen of screenW x screenH (see FILLS_WITHIN), or null; an upright screen is no shape's
    function forScreen(screenW, screenH) {
        if (!isSize(screenW, screenH)) return null;
        var found = closest(screenW / screenH);
        return found.off <= FILLS_WITHIN ? found.row.name : null;
    }

    // The quiet line under a dropdown: which shape fills a screen of screenW x screenH ("16:10 fills your screen"), '' when none does
    function fillsLine(screenW, screenH) {
        var name = forScreen(screenW, screenH);
        return name ? rowOf(name).label + ' fills your screen' : '';
    }

    // Which way the device is held: 'sideways' or 'upright'. Asked of the screen's orientation (screen.orientation.type: Android, desktops, iPhone Safari 16.4 and later), else window.orientation (90 and -90 are
    // sideways: older iPhones and an installed iPhone app), else the media query (orientation: landscape), else sideways. NEVER from screen.width against screen.height: Android swaps them on a turn, an iPhone does not.
    function wayOf(win) {
        try {
            var type = win.screen && win.screen.orientation && win.screen.orientation.type;
            if (typeof type === 'string' && /^(portrait|landscape)/.test(type)) return type.indexOf('portrait') === 0 ? 'upright' : 'sideways';
        } catch (e) { /* no orientation of the screen */ }
        try { if (typeof win.orientation === 'number') return Math.abs(win.orientation) === 90 ? 'sideways' : 'upright'; } catch (e) { /* none */ }
        try { if (typeof win.matchMedia === 'function') return win.matchMedia('(orientation: landscape)').matches ? 'sideways' : 'upright'; } catch (e) { /* none */ }
        return 'sideways';
    }

    // The shape that Automatic shows for the page's `win` (never stored, never in the address). Held sideways: the closest shape to the screen's own ratio, the long side over the short one (so that a screen whose
    // numbers swap on a turn and one whose numbers do not give the same), no cutoff. Held upright: FALLBACK (until the tall shapes). A screen that cannot be asked or has a size that is none gives FALLBACK.
    function automatic(win) {
        try {
            if (wayOf(win) !== 'sideways') return FALLBACK;
            var screenW = win.screen.width;
            var screenH = win.screen.height;
            if (!isSize(screenW, screenH)) return FALLBACK;
            return closest(Math.max(screenW, screenH) / Math.min(screenW, screenH)).row.name;
        } catch (e) {
            return FALLBACK;
        }
    }

    // The player's hand picks, one for each way the device is held (a pick holds only for the way it was made in). What the browser stores is the text '{"sideways":"4:3","upright":"16:9"}' (either part may be missing).
    function picks(text) {
        var found = {};
        try {
            var data = JSON.parse(text);
            ['sideways', 'upright'].forEach(function (way) { var name = parse(data[way]); if (name) found[way] = name; });
        } catch (e) { /* nothing was stored, or not this */ }
        return found;
    }
    // The shape picked for `way`, or null
    function pickFor(text, way) { return picks(text)[way] || null; }
    // The stored text after `name` is picked for `way` (name null: that way's pick is forgotten); '' when nothing is left
    function withPick(text, way, name) {
        var found = picks(text);
        if (name) found[way] = name; else delete found[way];
        return found.sideways || found.upright ? JSON.stringify(found) : '';
    }

    // Watch for a change of the way the device is held, in the page's `win`: each event restarts a wait (WAIT_MS), and when nothing has come for that long `settled()` is called, once, if the width, the way or the
    // fullscreen state differs from what it was at the last call (a change of height alone is the address bar coming and going: the picture's box follows it by itself). A window being dragged gives one call at its end.
    // Events: resize and orientationchange of the window, change of screen.orientation, fullscreenchange and webkitfullscreenchange of the document, resize of visualViewport. Gives back stop().
    function watch(win, settled, ms) {
        var doc = win.document, timer = null, ends = [];
        function full() { try { return !!(doc && (doc.fullscreenElement || doc.webkitFullscreenElement)); } catch (e) { return false; } }
        function state() { return win.innerWidth + '|' + wayOf(win) + '|' + full(); }
        var last = state();
        function again() { timer = null; var now = state(); if (now !== last) { last = now; settled(); } }
        function bump() { if (timer !== null) win.clearTimeout(timer); timer = win.setTimeout(again, ms === undefined ? WAIT_MS : ms); }
        function on(target, type) { if (target && target.addEventListener) { target.addEventListener(type, bump); ends.push(function () { target.removeEventListener(type, bump); }); } }
        on(win, 'resize'); on(win, 'orientationchange'); on(win.screen && win.screen.orientation, 'change');
        on(doc, 'fullscreenchange'); on(doc, 'webkitfullscreenchange'); on(win.visualViewport, 'resize');
        return { stop: function () { if (timer !== null) win.clearTimeout(timer); timer = null; ends.forEach(function (end) { end(); }); ends = []; } };
    }

    return { rows: ROWS, names: names, rowOf: rowOf, parse: parse, placeOf: placeOf, forScreen: forScreen, fillsLine: fillsLine, wayOf: wayOf, automatic: automatic, picks: picks, pickFor: pickFor, withPick: withPick, watch: watch, closest: closest, FILLS_WITHIN: FILLS_WITHIN, FALLBACK: FALLBACK, WAIT_MS: WAIT_MS, AUTO: AUTO };
});
