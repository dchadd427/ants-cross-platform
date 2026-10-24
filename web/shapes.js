// The shapes of the picture, as the web pages know them: ONE table, read by the game page (web/shell.html), the front page (web/lobby.html) and the settings menu (web/game_menu.js).
// The game has the same rows in include/ants_app/canvas_layout.hpp (kAspectTable); tests/scripts/test_web_shapes.py fails when the two tables differ, and when a page keeps a list of its own.
//   * A row is a shape: its name (the text of `?aspect=`, `--aspect` and the remembered value: "4:3"), the words that a selector shows, and the game's canvas in pixels. The rows are in the order
//     of the selectors, narrowest to widest, and a shape's place in them is the number that the game's `ants_set_aspect` takes.
//   * What follows from a row is worked out here and nowhere else: the shape in the smallest whole numbers (4:3, 8:5, 16:9, 7:3: the page's box is a whole number of those steps, and the CSS
//     slot has that ratio), the id that ends the ids of its buttons ("16-10"), which shape fills a screen, and which shape a first visit starts in.
// ES5, no module system: a page gets the global AntsShapes, node gets module.exports (tests/scripts/web_aspect_key_check.js and the other checks of the pages run the pages' code with it).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsShapes = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    // THE TABLE (compared with kAspectTable, field by field: name, label, width, height). `slot` is the widest the game's slot gets in CSS pixels (the page only: the classic picture is not shown
    // wider than 960, the others take the page's column up to 1280).
    // SHAPE_TABLE_BEGIN
    var TABLE = [
        { name: '4:3', label: 'Classic 4:3', width: 640, height: 480, slot: 960 },
        { name: '16:10', label: '16:10', width: 960, height: 600, slot: 1280 },
        { name: '16:9', label: '16:9', width: 960, height: 540, slot: 1280 },
        { name: '21:9', label: '21:9', width: 1260, height: 540, slot: 1280 }
    ];
    // SHAPE_TABLE_END

    // Two questions about a screen, asked of every row of the table (a row added later counts with no other change): forScreen, the selector's "fills your screen" tag, which has a cutoff
    // (the nearest shape counts when it is within 8 per cent of the screen's shape), and firstVisit, the shape a first visit starts in, which has none.
    var FILLS_WITHIN = 0.08;
    var FALLBACK = '16:9';

    function gcd(a, b) { return b === 0 ? a : gcd(b, a % b); }

    var ROWS = TABLE.map(function (row, place) {
        var step = gcd(row.width, row.height);
        return { name: row.name, label: row.label, width: row.width, height: row.height, slot: row.slot, place: place, stepW: row.width / step, stepH: row.height / step, id: row.name.replace(':', '-') };
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

    // The shape a first visit (nothing stored, no ?aspect=) starts in, for the page's `win`: the closest shape to the device's own screen (window.screen, CSS pixels: only the ratio counts), long
    // over short, no cutoff. An upright device (screen and window both taller than wide; a frame's own window says nothing), a screen that cannot be asked and a size that is none give FALLBACK. Never stored: only a selector's
    // press stores a shape (docs/PLAY_IN_BROWSER.md, "Which shape").
    function firstVisit(win) {
        try {
            var screenW = win.screen.width;
            var screenH = win.screen.height;
            if (!isSize(screenW, screenH)) return FALLBACK;
            var sideways = screenW >= screenH || (!(win.parent && win.parent !== win) && win.innerWidth > win.innerHeight);
            if (!sideways) return FALLBACK;
            return closest(Math.max(screenW, screenH) / Math.min(screenW, screenH)).row.name;
        } catch (e) {
            return FALLBACK;
        }
    }

    return { rows: ROWS, names: names, rowOf: rowOf, parse: parse, placeOf: placeOf, forScreen: forScreen, firstVisit: firstVisit, closest: closest, FILLS_WITHIN: FILLS_WITHIN, FALLBACK: FALLBACK };
});
