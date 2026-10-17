// The map maker's view of a .lvl file (docs/MAP_EDITOR.md): reads the grid and the markers out of the bytes so that the canvas can draw them. It judges nothing: whether a file loads and
// whether the map works is the map core's answer (web/lvl_core_api.js), and a file that this reader cannot place is simply drawn as plain ground. It reads the bytes where they are (no copy of
// the cells), so a 512 x 512 map (3 MB) costs nothing more than the file itself. The layout is docs/MAP_FORMAT.md, "The layout" and "Version 9". A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapFile = factory();
})(this, function () {
    'use strict';

    var EMPTY2 = 0x7FFE;            // layer 2: no object in this cell
    var TEAM_OF_MARKER = { 152: 'black', 153: 'blue', 154: 'green', 155: 'red' };

    function u16(b, at) { return b[at] | (b[at + 1] << 8); }
    function u32(b, at) { return (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0; }

    // {ok, kind, version, rows, columns, ...} or {ok: false, error}. `bytes` is a Uint8Array.
    function parse(bytes) {
        if (!(bytes instanceof Uint8Array) || bytes.length < 46) return { ok: false, error: 'the file is too short' };
        var version = u32(bytes, 0);
        var at;
        if (version === 8) at = 40;
        else if (version === 9) {
            if (bytes.length < 20) return { ok: false, error: 'the file is too short' };
            at = 18 + u16(bytes, 16);                       // the description's length word, then the description
        } else return { ok: false, error: 'the file is not a map of version 8 or 9' };
        if (at + 2 > bytes.length) return { ok: false, error: 'the file is cut short' };
        at += 2 + 11 * (u16(bytes, at) + 1);                // the dictionary: the number of names less one, then 11 bytes a name
        if (at + 8 > bytes.length) return { ok: false, error: 'the file is cut short' };
        var rows = u32(bytes, at), columns = u32(bytes, at + 4);
        if (version === 8) { rows &= 0xFFFF; columns &= 0xFFFF; }       // the original keeps the lower 16 bits of each
        at += 8;
        var cells = rows * columns;
        if (rows < 1 || columns < 1 || cells > 4194304) return { ok: false, error: 'the grid has no usable size' };
        var layer1 = at, layer2 = at + 6 * cells, end = layer2 + 6 * cells;
        if (end > bytes.length) return { ok: false, error: 'the file is cut short inside the grid' };
        var file = {
            ok: true, bytes: bytes, version: version, kind: version === 9 ? 'extended' : 'classic', rows: rows, columns: columns, cells: cells, layer1: layer1, layer2: layer2,
            markers: [], food: [],
            // the tile ids of one cell (a cell outside the grid is plain ground: 485)
            tile1: function (r, c) { return r < 0 || c < 0 || r >= rows || c >= columns ? 485 : u16(bytes, layer1 + 6 * (r * columns + c)); },
            tile2: function (r, c) { return r < 0 || c < 0 || r >= rows || c >= columns ? EMPTY2 : u16(bytes, layer2 + 6 * (r * columns + c)); }
        };
        readBlocks(file, end);
        return file;
    }

    // Block 1 (start markers and plants) and block 2 (food piles); a block that does not read is left empty
    function readBlocks(file, at) {
        var b = file.bytes, n, i;
        if (at + 2 > b.length) return;
        n = u16(b, at); at += 2;
        if (at + 6 * n > b.length) return;
        for (i = 0; i < n; i++, at += 6) {
            var team = TEAM_OF_MARKER[u16(b, at)];
            if (team) file.markers.push({ team: team, row: u16(b, at + 2), column: u16(b, at + 4) });
        }
        if (at + 2 > b.length) return;
        n = u16(b, at); at += 2;
        for (i = 0; i < n; i++) {
            if (at + 10 > b.length) return;
            var stages = u16(b, at + 8);
            file.food.push({ row: u16(b, at), column: u16(b, at + 2) });
            at += 10 + 4 * stages;
        }
    }

    // The name of the file that a download of a map is saved as: the map's name without what a file name cannot hold, then ".lvl"
    function downloadName(name) {
        var clean = String(name || '').replace(/[\u0000-\u001f\u007f\\\/:*?"<>|]/g, '_').replace(/^[\s.]+|[\s.]+$/g, '').slice(0, 40);
        return (clean || 'map') + '.lvl';
    }

    return { parse: parse, downloadName: downloadName, EMPTY2: EMPTY2 };
});
