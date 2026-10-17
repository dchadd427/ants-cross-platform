// The sizes of a new map on the map maker's pages (docs/MAP_EDITOR.md): the limits of each kind as the map core states them (`limits()` of web/lvl_core_api.js, never a copy of the numbers), the
// live line under the size boxes (cells, file size), and the core's refusal of a size turned into a short sentence. A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapSizes = factory();
})(this, function () {
    'use strict';

    function number(n) { return String(Math.round(n)).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }        // thousands get a comma

    // The bytes of a blank map of this many cells (docs/MAP_FORMAT.md, "The arithmetic": 50 bytes of header, 11 a name for the 486 names, 12 a cell, 12 of blocks)
    function blankBytes(cells) { return 50 + 11 * 486 + 12 * cells + 12; }

    function sizeText(bytes) {
        if (bytes >= 1073741824) return 'over 1 GB';
        return bytes >= 1048576 ? 'about ' + (bytes / 1048576).toFixed(1) + ' MB' : 'about ' + Math.round(bytes / 1024) + ' KB';
    }

    // {classic: {rows, columns, cells}, extended: {side, cells, longest}} from the answers of limits('new_map') and limits('extended'); side is the side of the biggest square
    // (openClassic is how many cells a Classic map may have when it is OPENED, not made: the extended profile's, which keeps the format's own limit of a Classic map)
    function limitsOf(newMap, extended) {
        var c = newMap.classic, e = extended.extended;
        return { classic: { rows: c.rows, columns: c.columns, cells: c.cells, openCells: extended.classic.cells }, extended: { side: Math.floor(Math.sqrt(e.cells)), cells: e.cells, longest: Math.max(e.rows, e.columns) } };
    }

    // The two lines of the kind cards as lists of [text, bold]
    function limitLine(kind, limits) {
        if (kind === 'classic') {
            var c = limits.classic;
            return [['Up to ', false], [c.rows + ' rows', true], [' and ', false], [c.columns + ' columns', true], [', ' + number(c.cells) + ' cells in all.', false]];
        }
        var e = limits.extended;
        return [['Up to ', false], [e.side + ' x ' + e.side, true], [' cells (' + number(e.cells) + ' in all). The longest side can be ' + number(e.longest) + '.', false]];
    }

    // A size box holds a whole number of at least 1 (or it is not a number yet)
    function wholeNumber(text) {
        var t = String(text).replace(/^\s+|\s+$/g, '');
        return /^\d{1,6}$/.test(t) && parseInt(t, 10) >= 1 ? parseInt(t, 10) : null;
    }

    function cellsWord(n) { return number(n) + (n === 1 ? ' cell' : ' cells'); }

    // What the side panel says of an open map: its cells against the most a map of its kind can have. A Classic map that was opened may have more cells than a new one may
    // (a library map of 16,000 cells is legal): the bar then counts against the limit of an opened Classic map.
    function mapFacts(kind, cells, bytes, limits) {
        var max = kind === 'extended' ? limits.extended.cells : (cells <= limits.classic.cells ? limits.classic.cells : limits.classic.openCells);
        return { max: max, percent: Math.max(2, Math.min(100, Math.round(cells / max * 100))), caption: number(cells) + ' of ' + number(max) + ' cells', file: sizeText(bytes) };
    }

    // The live line: {rows, columns, cells, max, bytes, text}; null while a box is not a number
    function liveLine(kind, rowsText, columnsText, limits) {
        var rows = wholeNumber(rowsText), columns = wholeNumber(columnsText);
        if (rows === null || columns === null) return null;
        var cells = rows * columns, max = kind === 'classic' ? limits.classic.cells : limits.extended.cells;
        return { rows: rows, columns: columns, cells: cells, max: max, bytes: blankBytes(cells), text: sizeText(blankBytes(cells)) };
    }

    // The core refuses a size with a sentence ("a blank map of 100 rows x 80 columns cannot be written: the grid has 100 rows x 80 columns: a Classic map made now has at most 90 rows
    // (...); make it an Extended map?"). The page shows the part that says why, with a capital and a full stop, and a button when the sentence offers an Extended map.
    function refusal(sentence) {
        var text = String(sentence).replace(/^a blank map of [^:]*? cannot be written: /, '').replace(/^the grid has \d+ rows x \d+ columns: /, '');
        var offers = /; make it an Extended map\?$/.test(text);
        text = text.replace(/; make it an Extended map\?$/, '').replace(/\b\d{4,}\b/g, number);
        text = text.charAt(0).toUpperCase() + text.slice(1);
        if (!/[.)?!]$/.test(text) || /\)$/.test(text)) text += '.';
        return { text: text, offersExtended: offers };
    }

    return { number: number, blankBytes: blankBytes, sizeText: sizeText, limitsOf: limitsOf, limitLine: limitLine, wholeNumber: wholeNumber, liveLine: liveLine, refusal: refusal, cellsWord: cellsWord, mapFacts: mapFacts };
});
