// The map maker's hills, starting ants and food piles (docs/MAP_EDITOR.md, "Hills, ants and food"), as plain arithmetic on the bytes of an open map. A hill is cells of layer 2 (4 x 4 round its
// entrance); the starting ants are records of block 1 (one record for each ant, the tile of the team's start marker, then row and column); a food pile is a record of block 2 (row, column, units,
// points for each unit, the number of stages, then a threshold and a tile for each stage): docs/MAP_FORMAT.md, "The layout". Nothing here touches a page and nothing changes the bytes: every
// function answers a CHANGE (the cells of layer 2 that get other words, the splices of the bytes after the layers, the names that the dictionary needs), and mapmaker_edit.js applies it, keeps
// it as a patch and undoes it. The splices are exact, the smallest piece of the file that differs, so everything else stays byte for byte as it was, and they hold their place as an offset
// from the end of layer 2, which does not move when the dictionary grows. Both kinds of file are handled: version 8 (an object's anchor is absolute) and version 9 (relative to its cell).
// A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_edit.js'), require('./mapmaker_solid.js'));
    else root.MapBlocks = factory(root.MapEdit, root.MapSolid);
})(this, function (Edit, Solid) {
    'use strict';

    var EMPTY2 = 0x7FFE;            // layer 2: no object in this cell; as a food stage: the pile is gone
    var MIN_ANTS = 1, MAX_ANTS = 64;                    // the starting ants of a team (the checker wants one; the 540 maps of the library have 1 to 62 for a team)
    var MIN_STAGES = 2, MAX_STAGES = 6;                 // the stages of a pile (a pile that is eaten must end with the stage that makes it vanish: see FOOD)
    var HILL = 4;                                       // a hill is 4 x 4 cells, its entrance the cell in the second row and column

    // The four teams in the order of the game's seats, with the tile of their hill (layer 2) and of their start marker (block 1), and the names that the game's dictionary has for them
    var TEAMS = [
        { key: 'green', label: 'Green', hill: 248, hillName: 'GREENHILL', marker: 154, markerName: 'GSTART' },
        { key: 'red', label: 'Red', hill: 247, hillName: 'REDHILL', marker: 155, markerName: 'RSTART' },
        { key: 'blue', label: 'Blue', hill: 246, hillName: 'BLUEHILL', marker: 153, markerName: 'USTART' },
        { key: 'black', label: 'Black', hill: 245, hillName: 'BLACKHILL', marker: 152, markerName: 'BSTART' }
    ];

    // The piles that the editor puts, by the number of stages: the game's own kinds (docs/MAP_FORMAT.md; each is the commonest in the library for its size). `units` are the bites of the pile,
    // `points` what a bite is worth, `stages` the pairs (units left, tile shown): the pile shows the tile of the last pair whose number is not below the units left, and the last pair, with the
    // empty tile, makes the pile vanish when it is eaten. `names` are the game's names of the tiles.
    var FOOD = {
        2: { units: 10, points: 30, stages: [[10, 280], [0, EMPTY2]] },
        3: { units: 20, points: 25, stages: [[20, 264], [10, 265], [0, EMPTY2]] },
        4: { units: 30, points: 25, stages: [[30, 264], [20, 265], [10, 266], [0, EMPTY2]] },
        5: { units: 30, points: 25, stages: [[30, 373], [20, 374], [10, 375], [5, 376], [0, EMPTY2]] },
        6: { units: 80, points: 20, stages: [[80, 267], [60, 268], [40, 269], [20, 270], [10, 271], [0, EMPTY2]] }
    };
    var FOOD_NAMES = {
        280: 'fdfrl1', 281: 'fdfrl2', 282: 'fdfrl3', 283: 'fdfrl4', 284: 'fdfrl5', 285: 'fdfrl6', 286: 'fdfrl7',
        253: 'fdburgr1', 254: 'fdburgr2', 255: 'fdburgr3', 256: 'fdsuckr1', 257: 'fdsuckr2', 258: 'fdsuckr3', 259: 'fdsuckr4',
        260: 'fdchoc1', 261: 'fdchoc2', 262: 'fdchoc3', 263: 'fdchoc4', 264: 'fdpretz1', 265: 'fdpretz2', 266: 'fdpretz3',
        267: 'fdskit1', 268: 'fdskit2', 269: 'fdskit3', 270: 'fdskit4', 271: 'fdskit5',
        272: 'fdgumw1', 273: 'fdgumw2', 274: 'fdgumw3', 275: 'fdgumw4', 276: 'fdcola1', 277: 'fdcola2', 278: 'fdcola3', 279: 'fdcola4',
        340: 'fdpezp1', 341: 'fdpezb1', 342: 'fdpez2p1', 343: 'fdpez2b1',
        356: 'lunchbox', 357: 'fdpmeat1', 358: 'fdpmeat2', 359: 'fdpmeat3', 360: 'fdpmeat4',
        361: 'fdcornd1', 362: 'fdcornd2', 363: 'fdcornd3', 364: 'fdcornd4', 365: 'fdcornd5',
        366: 'fdjelo1', 367: 'fdjelo2', 368: 'fdjelo3',
        369: 'fdcrakr1', 370: 'fdcrakr2', 371: 'fdcrakr3', 372: 'fdcrakr4', 373: 'fdcraks1', 374: 'fdcraks2', 375: 'fdcraks3', 376: 'fdcraks4',
        378: 'fdsteak1', 379: 'fdsteak2', 380: 'fdsteak3', 381: 'fdsteak4',
        382: 'fdtwink1', 383: 'fdtwink2', 384: 'fdtwink3', 385: 'fdeggs1', 386: 'fdeggs2', 387: 'fdeggs3', 388: 'fdeggs4',
        390: 'fdpizz1', 391: 'fdpizz2', 392: 'fdpizz3', 393: 'fdpizz4',
        394: 'fdfish1', 395: 'fdfish2', 396: 'fdfish3', 397: 'fdfish4', 398: 'fdfish5',
        399: 'fdchick1', 400: 'fdchick2', 401: 'fdchick3', 402: 'fdchick4', 403: 'fdchick5',
        1331: 'fdcer1', 1332: 'fdcer2', 1333: 'fdcer3'
    };
    var FOOD_ITEMS = [
        [253, 'Burger', 30, 30, [253, 254, 255]],
        [256, 'Lollipop', 40, 25, [256, 257, 258, 259]],
        [260, 'Chocolate bar', 40, 30, [260, 261, 262, 263]],
        [264, 'Pretzel', 30, 25, [264, 265, 266]],
        [267, 'Skittles', 80, 20, [267, 268, 269, 270, 271]],
        [272, 'Gummy worms', 40, 25, [272, 273, 274, 275]],
        [275, 'Single gummy worm', 10, 25, [275]],
        [276, 'Cola cap', 30, 25, [276, 277, 278, 279]],
        [280, 'Froot Loop (yellow)', 10, 30, [280]],
        [281, 'Froot Loop (blue)', 10, 30, [281]],
        [282, 'Froot Loop (orange)', 10, 30, [282]],
        [283, 'Froot Loop (purple)', 10, 30, [283]],
        [284, 'Froot Loop (green)', 10, 30, [284]],
        [285, 'Froot Loop (pink)', 10, 30, [285]],
        [286, 'Froot Loop (red)', 10, 30, [286]],
        [340, 'PEZ candy (pink)', 10, 30, [340]],
        [341, 'PEZ candy (yellow)', 10, 30, [341]],
        [342, 'PEZ candy (orange)', 10, 30, [342]],
        [343, 'PEZ candy (purple)', 10, 30, [343]],
        [356, 'Lunchbox', 60, 30, [356, 357, 358, 359, 360]],
        [357, 'Potted meat', 40, 25, [357, 358, 359, 360]],
        [361, 'Corn dog', 50, 30, [361, 362, 363, 364, 365]],
        [366, 'Jell-O', 30, 25, [366, 367, 368]],
        [369, 'Round crackers', 30, 25, [369, 370, 371, 372]],
        [373, 'Square crackers', 30, 25, [373, 374, 375, 376]],
        [378, 'Steak', 50, 35, [378, 379, 380, 381]],
        [382, 'Twinkie', 30, 30, [382, 383, 384]],
        [385, 'Fried eggs', 40, 30, [385, 386, 387, 388]],
        [390, 'Pizza slice', 40, 30, [390, 391, 392, 393]],
        [394, 'Fish', 50, 30, [394, 395, 396, 397, 398]],
        [399, 'Chicken leg', 50, 30, [399, 400, 401, 402, 403]],
        [1331, 'Cereal', 30, 25, [1331, 1332, 1333]]
    ];
    var PLANT_NAMES = { 404: 'clover1', 405: 'clover2', 406: 'clover3', 407: 'clover4', 408: 'clovers', 410: 'flower1a', 411: 'flower1b', 412: 'flower2a', 413: 'flower2b', 414: 'flower3a', 415: 'flower3b', 416: 'flower3c', 420: 'dflower1', 421: 'flower1' };
    var POWERUP_NAMES = [[62, 'pu_comb'], [63, 'pu_thief'], [64, 'pu_bomb'], [65, 'pu_swim'], [66, 'pu_mason']];

    function rd(b, at) { return b[at] | (b[at + 1] << 8); }
    function rd32(b, at) { return (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0; }
    function wr(b, at, v) { b[at] = v & 255; b[at + 1] = (v >> 8) & 255; }
    function wr32(b, at, v) { b[at] = v & 255; b[at + 1] = (v >>> 8) & 255; b[at + 2] = (v >>> 16) & 255; b[at + 3] = (v >>> 24) & 255; }
    function team(index) { return TEAMS[index] || null; }
    function teamOfMarker(tile) { var base = (tile >> 12) <= 5 ? (tile & 0x0FFF) : tile; for (var i = 0; i < 4; i++) if (TEAMS[i].marker === base) return i; return -1; }
    function typeOfMarker(tile) { return teamOfMarker(tile) >= 0 ? ((tile >> 12) & 15) : 0; }
    function markerTile(file, teamIndex, antType) { var m = TEAMS[teamIndex].marker; return file && file.version === 9 && antType > 0 && antType <= 5 ? (m | ((antType & 15) << 12)) : m; }
    function teamOfHill(tile) { for (var i = 0; i < 4; i++) if (TEAMS[i].hill === tile) return i; return -1; }
    function clampTo(n, lo, hi) { n = Math.round(+n); return isNaN(n) ? lo : Math.max(lo, Math.min(hi, n)); }

    // ---- the lists after the layers ----

    // Where the lists are and what is in them, read as the original reads them: block 1 (a word, then 6 bytes a record), block 2 (a word, then a pile of 10 bytes and 4 for each stage) and
    // block 4 (after the 4 bytes of block 3: a word, then 8 bytes for a stem waypoint or 52 bytes for a flower dropper). `rel` is an offset from the end of layer 2 (file.blocksAt).
    function layout(file) {
        var cached = file.blockLayout;
        if (cached && cached.bytes === file.bytes && cached.size === file.bytes.length) return cached.value;
        var b = file.bytes, base = file.blocksAt, out = { ok1: false, ok2: false, ok4: false, records: [], piles: [], waypoints: [], b1: null, b2: null, b4: null }, at = base, i, n;
        file.blockLayout = { bytes: b, size: b.length, value: out };
        if (at + 2 > b.length) return out;
        n = rd(b, at);
        if (at + 2 + 6 * n > b.length) return out;
        for (i = 0; i < n; i++) out.records.push({ rel: 2 + 6 * i, tile: rd(b, at + 2 + 6 * i), row: rd(b, at + 4 + 6 * i), column: rd(b, at + 6 + 6 * i) });
        out.b1 = { rel: 0, length: 2 + 6 * n }; out.ok1 = true;
        at += 2 + 6 * n;
        if (at + 2 > b.length) return out;
        var start = at, count = rd(b, at);
        at += 2;
        for (i = 0; i < count; i++) {
            if (at + 10 > b.length) return out;
            var stages = rd(b, at + 8);
            if (stages === 0 || at + 10 + 4 * stages > b.length) return out;
            var pile = { rel: at - base, length: 10 + 4 * stages, row: rd(b, at), column: rd(b, at + 2), units: rd(b, at + 4), points: rd(b, at + 6), stages: [] };
            for (var k = 0; k < stages; k++) pile.stages.push([rd(b, at + 10 + 4 * k), rd(b, at + 12 + 4 * k)]);
            out.piles.push(pile);
            at += pile.length;
        }
        out.b2 = { rel: start - base, length: at - start }; out.ok2 = true;
        if (at + 6 > b.length) return out;
        at += 4;
        var start4 = at, count4 = rd(b, at);
        at += 2;
        for (i = 0; i < count4; i++) {
            if (at + 8 > b.length) { out.waypoints = []; return out; }
            var flag = rd32(b, at + 4), wLen = flag !== 0 ? 52 : 8;
            if (at + wLen > b.length) { out.waypoints = []; return out; }
            out.waypoints.push({ rel: at - base, length: wLen, row: rd(b, at), column: rd(b, at + 2), flag: flag, interval: flag ? rd32(b, at + 8) : 0 });
            at += wLen;
        }
        out.b4 = { rel: start4 - base, length: at - start4 }; out.ok4 = true;
        return out;
    }

    // The bytes of a list: block 1 from its records, block 2 from its piles, block 4 from its waypoints
    function block1Bytes(records) {
        var out = new Uint8Array(2 + 6 * records.length);
        wr(out, 0, records.length);
        for (var i = 0; i < records.length; i++) { wr(out, 2 + 6 * i, records[i].tile); wr(out, 4 + 6 * i, records[i].row); wr(out, 6 + 6 * i, records[i].column); }
        return out;
    }
    function pileBytes(pile) {
        var out = new Uint8Array(10 + 4 * pile.stages.length);
        wr(out, 0, pile.row); wr(out, 2, pile.column); wr(out, 4, pile.units); wr(out, 6, pile.points); wr(out, 8, pile.stages.length);
        for (var k = 0; k < pile.stages.length; k++) { wr(out, 10 + 4 * k, pile.stages[k][0]); wr(out, 12 + 4 * k, pile.stages[k][1]); }
        return out;
    }
    function block2Bytes(file, piles) {
        var parts = [new Uint8Array(2)], total = 2, i;
        wr(parts[0], 0, piles.length);
        for (i = 0; i < piles.length; i++) {
            var p = piles[i], bytes = p.keep ? file.bytes.slice(file.blocksAt + p.rel, file.blocksAt + p.rel + p.length) : pileBytes(p);
            parts.push(bytes); total += bytes.length;
        }
        var out = new Uint8Array(total), at = 0;
        for (i = 0; i < parts.length; i++) { out.set(parts[i], at); at += parts[i].length; }
        return out;
    }
    function waypointBytes(wp) {
        var out = new Uint8Array(wp.flag ? 52 : 8);
        wr(out, 0, wp.row); wr(out, 2, wp.column); wr32(out, 4, wp.flag ? 1 : 0);
        if (wp.flag) {
            wr32(out, 8, wp.interval || 60);
            var view = new DataView(out.buffer), probs = wp.probs || [0.2, 0.2, 0.2, 0.2, 0.2];
            for (var k = 0; k < 5; k++) view.setFloat64(12 + 8 * k, probs[k], true);
        }
        return out;
    }
    function block4Bytes(file, waypoints) {
        var parts = [new Uint8Array(2)], total = 2, i;
        wr(parts[0], 0, waypoints.length);
        for (i = 0; i < waypoints.length; i++) {
            var w = waypoints[i], bytes;
            if (w.keep) {
                bytes = file.bytes.slice(file.blocksAt + w.rel, file.blocksAt + w.rel + w.length);
                if (w.row !== undefined) wr(bytes, 0, w.row);
                if (w.column !== undefined) wr(bytes, 2, w.column);
            } else bytes = waypointBytes(w);
            parts.push(bytes); total += bytes.length;
        }
        var out = new Uint8Array(total), at = 0;
        for (i = 0; i < parts.length; i++) { out.set(parts[i], at); at += parts[i].length; }
        return out;
    }

    // The smallest splice that turns the bytes of a list into the new ones (the common start and the common end stay): {rel, removed, inserted}, or null when nothing differs
    function diffSplice(file, rel, length, next) {
        var old = file.bytes.subarray(file.blocksAt + rel, file.blocksAt + rel + length), head = 0, tail = 0;
        while (head < old.length && head < next.length && old[head] === next[head]) head++;
        while (tail < old.length - head && tail < next.length - head && old[old.length - 1 - tail] === next[next.length - 1 - tail]) tail++;
        if (head === old.length && head === next.length) return null;
        return { rel: rel + head, removed: old.slice(head, old.length - tail), inserted: next.slice(head, next.length - tail) };
    }

    // ---- the change ----

    function emptyChange() { return { names: [], cells: [], splices: [] }; }
    // The cell words that a change gives to a cell of layer 2: the last word for a cell wins
    function putCell(change, idx, t, f, a) {
        for (var i = 0; i < change.cells.length; i++) if (change.cells[i][0] === idx) { change.cells[i] = [idx, t, f, a]; return; }
        change.cells.push([idx, t, f, a]);
    }
    function takeObject(file, change, idx) {
        var cells = Edit.objectCells(file, idx);
        for (var i = 0; i < cells.length; i++) putCell(change, cells[i], EMPTY2, 0, 0);
    }
    function addName(change, id, name) { for (var i = 0; i < change.names.length; i++) if (change.names[i][0] === id) return; change.names.push([id, name]); }
    // The splices of a change are applied in the order they are listed, so the one that is later in the file comes first (it does not move the earlier one)
    function sortSplices(change) { change.splices.sort(function (a, b) { return b.rel - a.rel; }); return change; }

    // ---- hills ----

    // Where the hill of a team is: the cells of layer 2 that hold its tile (a map has one hill for each team; an opened map may have more, or part of one)
    function hillCells(file, teamIndex) {
        var tile = TEAMS[teamIndex].hill, out = [], b = file.bytes, at = file.layer2, i;
        for (i = 0; i < file.cells; i++) if ((b[at + 6 * i] | (b[at + 6 * i + 1] << 8)) === tile) out.push(i);
        return out;
    }

    // The hill of a team put with its entrance on (row, column): 4 x 4 cells (one row and one column before the entrance, two after), each anchored at the entrance, the entrance flagged. The
    // team's hill goes from where it was (only one for each team), and an object under the new one goes whole; another team's hill under it is never taken away: {error}, as when there is no room on the map.
    function placeHill(file, teamIndex, row, column) {
        var t = TEAMS[teamIndex];
        if (!t) return { error: 'There is no such team.' };
        if (row < 1 || column < 1 || row + 2 >= file.rows || column + 2 >= file.columns) return { error: 'A hill is 4 x 4 cells and there is no room for a whole one there. Try a place a little further from the edge of the map.' };
        var r, c, i;
        for (r = row - 1; r <= row + 2; r++) for (c = column - 1; c <= column + 2; c++) {
            var other = teamOfHill(Edit.tile(file, 2, r * file.columns + c));
            if (other >= 0 && other !== teamIndex) return { error: TEAMS[other].label + '\u2019s hill is here. Move it first, or take it away with Erase.' };       // (another team's hill is never taken away without being asked)
        }
        var change = emptyChange(), old = hillCells(file, teamIndex);
        for (i = 0; i < old.length; i++) putCell(change, old[i], EMPTY2, 0, 0);
        for (r = row - 1; r <= row + 2; r++) for (c = column - 1; c <= column + 2; c++) {
            var idx = r * file.columns + c;
            if (Edit.tile(file, 2, idx) !== EMPTY2) takeObject(file, change, idx);
        }
        for (r = row - 1; r <= row + 2; r++) for (c = column - 1; c <= column + 2; c++) {
            var word = file.version === 8 ? ((row & 255) | ((column & 255) << 8)) : (((row - r) & 255) | (((column - c) & 255) << 8));
            putCell(change, r * file.columns + c, t.hill, r === row && c === column ? 1 : 0, word);
        }
        addName(change, t.hill, t.hillName);
        return change;
    }

    // ---- starting ants ----

    // The cells where `count` ants stand together round (row, column): the cell itself and then the nearest ones, passing cells that hold an object, solid ground or an ant of another team (when
    // there are not enough of the good ones, the rest of the cells will do). Fewer than `count` only on a map with fewer cells.
    function startCells(file, lay, row, column, count, teamIndex) {
        var taken = {}, i, r, c;
        for (i = 0; i < lay.records.length; i++) {
            var rec = lay.records[i];
            if (teamOfMarker(rec.tile) >= 0 && teamOfMarker(rec.tile) !== teamIndex) taken[rec.row * file.columns + rec.column] = true;
        }
        var radius = Math.max(2, Math.ceil(Math.sqrt(count)) + 1), good = [], rest = [];
        for (;;) {
            good = []; rest = [];
            for (r = Math.max(0, row - radius); r <= Math.min(file.rows - 1, row + radius); r++) for (c = Math.max(0, column - radius); c <= Math.min(file.columns - 1, column + radius); c++) {
                var idx = r * file.columns + c;
                if (taken[idx]) continue;
                var item = { r: r, c: c, d: (r - row) * (r - row) + (c - column) * (c - column) };
                if (Edit.tile(file, 2, idx) === EMPTY2 && (Edit.flags(file, 1, idx) & 1) === 0) good.push(item); else rest.push(item);
            }
            if (good.length >= count || (radius >= file.rows && radius >= file.columns)) break;
            radius *= 2;
        }
        function order(a, b) { return a.d - b.d || a.r - b.r || a.c - b.c; }
        good.sort(order); rest.sort(order);
        return good.concat(rest).slice(0, count);
    }

    // The starting ants of a team put round (row, column): `count` records of the team's start marker, standing together; the team's ants that were there go (only one start for each team).
    function placeStart(file, teamIndex, row, column, count, antType) {
        var t = TEAMS[teamIndex], lay = layout(file);
        if (!t) return { error: 'There is no such team.' };
        if (!lay.ok1) return { error: 'The list of starting ants in this file cannot be changed here: the file ends in the middle of it.' };
        var n = clampTo(count, MIN_ANTS, MAX_ANTS), cells = startCells(file, lay, row, column, n, teamIndex), kept = [], insertAt = -1, i;
        for (i = 0; i < lay.records.length; i++) {
            if (teamOfMarker(lay.records[i].tile) === teamIndex) { if (insertAt < 0) insertAt = kept.length; } else kept.push(lay.records[i]);
        }
        if (insertAt < 0) insertAt = kept.length;
        var mTile = markerTile(file, teamIndex, antType), fresh = cells.map(function (cell) { return { tile: mTile, row: cell.r, column: cell.c }; });
        var records = kept.slice(0, insertAt).concat(fresh, kept.slice(insertAt)), change = emptyChange(), splice = diffSplice(file, lay.b1.rel, lay.b1.length, block1Bytes(records));
        if (splice) change.splices.push(splice);
        addName(change, t.marker, t.markerName);
        return change;
    }

    function eraseStart(file, teamIndex) {
        var t = TEAMS[teamIndex], lay = layout(file), change = emptyChange();
        if (!t || !lay.ok1) return change;
        var kept = lay.records.filter(function (rec) { return teamOfMarker(rec.tile) !== teamIndex; });
        if (kept.length === lay.records.length) return change;
        change.splices.push(diffSplice(file, lay.b1.rel, lay.b1.length, block1Bytes(kept)));
        return change;
    }

    // ---- food piles ----

    function stageTiles(pile) { var out = []; for (var k = 0; k < pile.stages.length; k++) if (pile.stages[k][1] !== EMPTY2) out.push(pile.stages[k][1]); return out; }

    // The cells of layer 2 that show a pile (the library's files hold them too: the pile's first stage on its cell, round its anchor): they go with the pile
    function takePileCells(file, change, pile) {
        if (pile.row >= file.rows || pile.column >= file.columns) return;
        var idx = pile.row * file.columns + pile.column, tile = Edit.tile(file, 2, idx);
        if (tile !== EMPTY2 && stageTiles(pile).indexOf(tile) >= 0) takeObject(file, change, idx);
    }

    function foodItem(id) {
        for (var i = 0; i < FOOD_ITEMS.length; i++) if (FOOD_ITEMS[i][0] === id) return { id: FOOD_ITEMS[i][0], name: FOOD_NAMES[FOOD_ITEMS[i][0]], label: FOOD_ITEMS[i][1], units: FOOD_ITEMS[i][2], points: FOOD_ITEMS[i][3], tiles: FOOD_ITEMS[i][4] };
        return null;
    }

    function customPile(set, custom) {
        if (!custom) return set;
        var it = custom.id ? foodItem(custom.id) : null;
        var tiles = it ? it.tiles : set.stages.slice(0, -1).map(function (s) { return s[1]; });
        var m = tiles.length, units = clampTo(custom.units || (it ? it.units : set.units), m, 1000), points = clampTo(custom.points || (it ? it.points : set.points), 1, 500);
        if (!it && units === set.units && points === set.points) return set;
        var stages = [[units, tiles[0]]], prev = units;
        for (var k = 1; k < m; k++) {
            var th = Math.max(m - k, Math.min(prev - 1, Math.round(units * (m - k) / m)));
            stages.push([th, tiles[k]]); prev = th;
        }
        stages.push([0, EMPTY2]);
        return { units: units, points: points, stages: stages };
    }

    // A food pile put on (row, column) with `stages` stages (or `custom` {id, units, points}; a pile that was on that cell goes)
    function placePile(file, row, column, stages, custom) {
        var lay = layout(file);
        if (!lay.ok2) return { error: 'The list of food piles in this file cannot be changed here: it does not end where its count says.' };
        if (row < 0 || column < 0 || row >= file.rows || column >= file.columns) return { error: 'There is no room for a food pile there.' };
        var set = FOOD[clampTo(stages, MIN_STAGES, MAX_STAGES)], change = emptyChange(), i;
        if (custom) set = customPile(set, custom);
        var list = [];
        for (i = 0; i < lay.piles.length; i++) {
            var p = lay.piles[i];
            if (p.row === row && p.column === column) takePileCells(file, change, p); else list.push({ keep: true, rel: p.rel, length: p.length });
        }
        list.push({ row: row, column: column, units: set.units, points: set.points, stages: set.stages });
        var splice = diffSplice(file, lay.b2.rel, lay.b2.length, block2Bytes(file, list));
        if (splice) change.splices.push(splice);
        set.stages.forEach(function (s) { if (s[1] !== EMPTY2) addName(change, s[1], FOOD_NAMES[s[1]]); });
        return change;
    }

    // ---- plants (walk-under canopy & power-up droppers) ----

    // A top-layer plant put on (row, column) in block 1 and block 4: `dropper` is 0 / falsy for a walk-under canopy plant (flag = 0, 8 bytes in block 4) or a positive interval in seconds (or
    // {interval, probs}) for a power-up dropping plant (flag = 1, 52 bytes in block 4).
    function placePlant(file, row, column, tile, dropper) {
        if (!PLANT_NAMES[tile]) return { error: 'There is no such plant.' };
        var lay = layout(file);
        if (!lay.ok1 || !lay.ok4) return { error: 'The list of plants in this file cannot be changed here: the file ends in the middle of it.' };
        if (row < 0 || column < 0 || row >= file.rows || column >= file.columns) return { error: 'There is no room for a plant there.' };
        var idx = row * file.columns + column, why = inTheWay(file, idx);
        if (why) return { error: blockedText(file, idx) };
        var change = emptyChange(), records = [], waypoints = [], i;
        takeObject(file, change, idx);
        for (i = 0; i < lay.records.length; i++) {
            var r = lay.records[i];
            if (!(PLANT_NAMES[r.tile] && r.row === row && r.column === column)) records.push(r);
        }
        records.push({ tile: tile, row: row, column: column });
        for (i = 0; i < lay.waypoints.length; i++) {
            var w = lay.waypoints[i];
            if (!(w.row === row && w.column === column)) waypoints.push({ keep: true, rel: w.rel, length: w.length });
        }
        var interval = typeof dropper === 'number' ? dropper : (dropper && dropper.interval ? dropper.interval : 0);
        if (interval > 0 || (dropper && typeof dropper === 'object')) {
            waypoints.push({ row: row, column: column, flag: 1, interval: clampTo(interval || 60, 1, 3600), probs: dropper && dropper.probs || [0.2, 0.2, 0.2, 0.2, 0.2] });
            for (i = 0; i < POWERUP_NAMES.length; i++) addName(change, POWERUP_NAMES[i][0], POWERUP_NAMES[i][1]);
        } else {
            waypoints.push({ row: row, column: column, flag: 0 });
        }
        var sp1 = diffSplice(file, lay.b1.rel, lay.b1.length, block1Bytes(records));
        var sp4 = diffSplice(file, lay.b4.rel, lay.b4.length, block4Bytes(file, waypoints));
        if (sp1) change.splices.push(sp1);
        if (sp4) change.splices.push(sp4);
        addName(change, tile, PLANT_NAMES[tile]);
        return sortSplices(change);
    }

    // ---- taking things away (the Erase tool); picking things up and moving them is mapmaker_pickup.js ----

    // Where the ants, the plants, the piles and the waypoints of the lists are, by cell (made once for a reading of the lists: an eraser with a big brush asks for many cells)
    function indexOf(file, lay) {
        if (lay.index) return lay.index;
        var markers = {}, plants = {}, piles = {}, waypoints = {}, i;
        for (i = 0; i < lay.records.length; i++) {
            var rec = lay.records[i], cell = rec.row * file.columns + rec.column;
            if (rec.row >= file.rows || rec.column >= file.columns) continue;
            if (teamOfMarker(rec.tile) >= 0) (markers[cell] = markers[cell] || []).push(rec.tile);
            else if (PLANT_NAMES[rec.tile]) (plants[cell] = plants[cell] || []).push(i);
        }
        for (i = 0; i < lay.piles.length; i++) {
            var p = lay.piles[i];
            if (p.row < file.rows && p.column < file.columns) (piles[p.row * file.columns + p.column] = piles[p.row * file.columns + p.column] || []).push(i);
        }
        for (i = 0; i < lay.waypoints.length; i++) {
            var w = lay.waypoints[i];
            if (w.row < file.rows && w.column < file.columns) (waypoints[w.row * file.columns + w.column] = waypoints[w.row * file.columns + w.column] || []).push(i);
        }
        lay.index = { markers: markers, plants: plants, piles: piles, waypoints: waypoints };
        return lay.index;
    }

    // What stands on the cell `idx` besides an object of layer 2: the start of a team (an ant of it is on the cell: all its ants go together), a plant / dropper and a food pile (the cell is its
    // anchor, or part of the object that its anchor holds). A change, or null when there is nothing.
    function eraseAt(file, idx) {
        var lay = layout(file), ix = indexOf(file, lay), change = emptyChange(), i;
        var tiles = lay.ok1 ? ix.markers[idx] : null, plantHits = lay.ok1 ? ix.plants[idx] : null;
        if (tiles || plantHits) {
            var gone = {}, dropIdx = {};
            if (tiles) for (i = 0; i < tiles.length; i++) gone[teamOfMarker(tiles[i])] = true;
            if (plantHits) for (i = 0; i < plantHits.length; i++) dropIdx[plantHits[i]] = true;
            var kept = lay.records.filter(function (rec, k) { return !gone[teamOfMarker(rec.tile)] && !dropIdx[k]; });
            var sp1 = diffSplice(file, lay.b1.rel, lay.b1.length, block1Bytes(kept));
            if (sp1) change.splices.push(sp1);
        }
        if (lay.ok2 && lay.piles.length) {
            var anchor = Edit.tile(file, 2, idx) !== EMPTY2 ? Edit.anchorIndex(file, idx) : -1, hits = {}, any = false;
            (ix.piles[idx] || []).forEach(function (k) { hits[k] = true; any = true; });
            (anchor >= 0 && ix.piles[anchor] || []).forEach(function (k) { hits[k] = true; any = true; });
            if (any) {
                var list = [];
                for (i = 0; i < lay.piles.length; i++) {
                    var p = lay.piles[i];
                    if (hits[i]) takePileCells(file, change, p); else list.push({ keep: true, rel: p.rel, length: p.length });
                }
                var sp2 = diffSplice(file, lay.b2.rel, lay.b2.length, block2Bytes(file, list));
                if (sp2) change.splices.push(sp2);
            }
        }
        if (lay.ok4 && plantHits && ix.waypoints[idx]) {
            var wpDrop = {}, wpList = [];
            for (i = 0; i < ix.waypoints[idx].length; i++) wpDrop[ix.waypoints[idx][i]] = true;
            for (i = 0; i < lay.waypoints.length; i++) if (!wpDrop[i]) wpList.push({ keep: true, rel: lay.waypoints[i].rel, length: lay.waypoints[i].length });
            var sp4 = diffSplice(file, lay.b4.rel, lay.b4.length, block4Bytes(file, wpList));
            if (sp4) change.splices.push(sp4);
        }
        if (!change.splices.length && !change.cells.length) return null;
        return sortSplices(change);
    }

    // What stands on the cell `idx` that another tool owns and that nothing may be put over without being asked: 'hill' (a cell of a team's hill), 'ants' (a start of a team is on the cell) and
    // 'pile' (the cell is a food pile's, or part of the object that a pile's cell anchors); '' when there is none. A list that does not read has none to find.
    function inTheWay(file, idx) {
        var t = Edit.tile(file, 2, idx);
        if (teamOfHill(t) >= 0) return 'hill';
        var lay = layout(file), ix = indexOf(file, lay);
        if (lay.ok1 && ix.markers[idx]) return 'ants';
        if (lay.ok2 && lay.piles.length) {
            var anchor = t !== EMPTY2 ? Edit.anchorIndex(file, idx) : -1;
            if (ix.piles[idx] || (anchor >= 0 && ix.piles[anchor])) return 'pile';
        }
        return '';
    }

    // The sentence for what is in the way of an object (inTheWay) on the cell `idx`, or '' when nothing is: the Objects tool and the Bigger tool refuse to take a hill, a team's starting ants or a food
    // pile away, and say so in these words
    function blockedText(file, idx) {
        var why = inTheWay(file, idx);
        if (!why) return '';
        if (why === 'hill') return TEAMS[teamOfHill(Edit.tile(file, 2, idx))].label + '\u2019s hill is here. Move it first, or take it away with Erase.';
        if (why === 'ants') return 'Starting ants are in the way. Move them first, or take them away with Erase.';
        return 'A food pile is in the way. Take it away with Erase first.';
    }

    // What the map has now, for the palette's line: the cells of each team's hill and the ants and food piles on the map (-1 for a list that cannot be read)
    function facts(file, scanHills) {
        var lay = layout(file), out = { hills: [0, 0, 0, 0], ants: [0, 0, 0, 0], piles: lay.ok2 ? lay.piles.length : -1, listsOk: lay.ok1 && lay.ok2 }, i;
        if (scanHills) for (i = 0; i < 4; i++) out.hills[i] = hillCells(file, i).length;
        for (i = 0; i < lay.records.length; i++) { var tm = teamOfMarker(lay.records[i].tile); if (tm >= 0) out.ants[tm]++; }
        return out;
    }

    // Resize the open map to (newRows x newCols), keeping rows from `keepRow` and columns from `keepCol` when shrinking, or padding at `padRow` and `padCol` when growing, as one undoable CHANGE.
    function resizeMap(file, newRows, newCols, keepRow, keepCol, padRow, padCol) {
        newRows = clampTo(newRows, 1, 1024); newCols = clampTo(newCols, 1, 1024);
        var oldRows = file.rows, oldCols = file.columns;
        var kr = newRows < oldRows ? clampTo(keepRow || 0, 0, oldRows - newRows) : 0;
        var kc = newCols < oldCols ? clampTo(keepCol || 0, 0, oldCols - newCols) : 0;
        var pr = newRows > oldRows ? clampTo(padRow !== undefined ? padRow : Math.floor((newRows - oldRows) / 2), 0, newRows - oldRows) : 0;
        var pc = newCols > oldCols ? clampTo(padCol !== undefined ? padCol : Math.floor((newCols - oldCols) / 2), 0, newCols - oldCols) : 0;
        var dr = pr - kr, dc = pc - kc, newCells = newRows * newCols, lay = layout(file);
        var l1 = new Uint8Array(6 * newCells), l2 = new Uint8Array(6 * newCells), r, c, i;
        for (i = 0; i < newCells; i++) wr(l2, 6 * i, EMPTY2);
        // hills and multi-cell objects whose footprint does not fit whole in the resized map are dropped whole
        var dropHills = {}, dropCells = {};
        for (var tm = 0; tm < 4; tm++) {
            var hc = hillCells(file, tm);
            for (i = 0; i < hc.length; i++) {
                var hr = ((hc[i] / oldCols) | 0) + dr, hcol = (hc[i] % oldCols) + dc;
                if (hr < 0 || hcol < 0 || hr >= newRows || hcol >= newCols) { dropHills[TEAMS[tm].hill] = true; break; }
                if (Edit.flags(file, 2, hc[i]) & 1) {
                    if (hr < 1 || hcol < 1 || hr + 2 >= newRows || hcol + 2 >= newCols) { dropHills[TEAMS[tm].hill] = true; break; }
                }
            }
        }
        for (i = 0; i < file.cells; i++) {
            var ot = Edit.tile(file, 2, i);
            if (ot === EMPTY2 || teamOfHill(ot) >= 0 || dropCells[i]) continue;
            var ocells = Edit.objectCells(file, i), clipped = false, k;
            for (k = 0; k < ocells.length; k++) {
                var cr = ((ocells[k] / oldCols) | 0) + dr, cc = (ocells[k] % oldCols) + dc;
                if (cr < 0 || cc < 0 || cr >= newRows || cc >= newCols) { clipped = true; break; }
            }
            if (clipped) for (k = 0; k < ocells.length; k++) dropCells[ocells[k]] = true;
        }
        for (r = 0; r < newRows; r++) for (c = 0; c < newCols; c++) {
            var or = r - dr, oc = c - dc, nIdx = r * newCols + c;
            if (or >= 0 && or < oldRows && oc >= 0 && oc < oldCols) {
                var oIdx = or * oldCols + oc, t1 = Edit.tile(file, 1, oIdx), f1 = Edit.flags(file, 1, oIdx), a1 = Edit.third(file, 1, oIdx);
                var t2 = Edit.tile(file, 2, oIdx), f2 = Edit.flags(file, 2, oIdx), a2 = Edit.third(file, 2, oIdx);
                if (or === 0 && r > 0 && !(Solid && Solid.sets(t2))) f1 &= ~1;
                if (r === 0) f1 |= 1;
                if (t2 !== EMPTY2) {
                    var anc = Edit.anchorIndex(file, oIdx), ar = anc >= 0 ? ((anc / oldCols) | 0) : or, ac = anc >= 0 ? (anc % oldCols) : oc;
                    var nar = ar + dr, nac = ac + dc;
                    if (dropHills[t2] || dropCells[oIdx] || nar < 0 || nac < 0 || nar >= newRows || nac >= newCols) {
                        if (r > 0 && (Solid && Solid.clears(t2) || dropHills[t2])) f1 &= ~1;
                        t2 = EMPTY2; f2 = 0; a2 = 0;
                    } else if (!(t2 >= 129 && t2 <= 132) && file.version === 8) {
                        a2 = (nar & 255) | ((nac & 255) << 8);
                    } else if (!(t2 >= 129 && t2 <= 132)) {
                        a2 = ((nar - r) & 255) | (((nac - c) & 255) << 8);
                    }
                }
                wr(l1, 6 * nIdx, t1); wr(l1, 6 * nIdx + 2, f1); wr(l1, 6 * nIdx + 4, a1);
                wr(l2, 6 * nIdx, t2); wr(l2, 6 * nIdx + 2, f2); wr(l2, 6 * nIdx + 4, a2);
            } else {
                var er = clampTo(or, 0, oldRows - 1), ec = clampTo(oc, 0, oldCols - 1), gt = Edit.tile(file, 1, er * oldCols + ec);
                wr(l1, 6 * nIdx, gt); wr(l1, 6 * nIdx + 2, r === 0 ? 1 : 0); wr(l1, 6 * nIdx + 4, 0xCDCD);
            }
        }
        var b1 = new Uint8Array(0), b2 = new Uint8Array(0), b34 = file.bytes.subarray(file.blocksAt);
        if (lay.ok1) {
            var recs = [];
            for (i = 0; i < lay.records.length; i++) {
                var nr = lay.records[i].row + dr, nc = lay.records[i].column + dc;
                if (nr >= 0 && nc >= 0 && nr < newRows && nc < newCols) recs.push({ tile: lay.records[i].tile, row: nr, column: nc });
            }
            b1 = block1Bytes(recs);
            b34 = file.bytes.subarray(file.blocksAt + lay.b1.length);
        }
        if (lay.ok1 && lay.ok2) {
            var piles = [];
            for (i = 0; i < lay.piles.length; i++) {
                var p = lay.piles[i], pr2 = p.row + dr, pc2 = p.column + dc;
                if (pr2 >= 0 && pc2 >= 0 && pr2 < newRows && pc2 < newCols) piles.push({ row: pr2, column: pc2, units: p.units, points: p.points, stages: p.stages });
            }
            b2 = block2Bytes(file, piles);
            b34 = file.bytes.subarray(file.blocksAt + lay.b2.rel + lay.b2.length);
        }
        if (lay.ok1 && lay.ok2 && lay.ok4) {
            var wps = [];
            for (i = 0; i < lay.waypoints.length; i++) {
                var w = lay.waypoints[i], wr2 = w.row + dr, wc2 = w.column + dc;
                if (wr2 >= 0 && wc2 >= 0 && wr2 < newRows && wc2 < newCols) wps.push({ keep: true, rel: w.rel, length: w.length, row: wr2, column: wc2 });
            }
            var b3 = file.bytes.subarray(file.blocksAt + lay.b2.rel + lay.b2.length, file.blocksAt + lay.b4.rel);
            var b4 = block4Bytes(file, wps);
            var rest = file.bytes.subarray(file.blocksAt + lay.b4.rel + lay.b4.length);
            b34 = new Uint8Array(b3.length + b4.length + rest.length);
            b34.set(b3, 0); b34.set(b4, b3.length); b34.set(rest, b3.length + b4.length);
        }
        var dims = new Uint8Array(8); wr32(dims, 0, newRows); wr32(dims, 4, newCols);
        var next = new Uint8Array(8 + l1.length + l2.length + b1.length + b2.length + b34.length), pos = 0;
        [dims, l1, l2, b1, b2, b34].forEach(function (part) { next.set(part, pos); pos += part.length; });
        var baseAt = file.layer1 - 8, prev = file.bytes.subarray(baseAt), h = 0, t = 0;
        while (h < prev.length && h < next.length && prev[h] === next[h]) h++;
        while (t < prev.length - h && t < next.length - h && prev[prev.length - 1 - t] === next[next.length - 1 - t]) t++;
        var change = emptyChange();
        if (file.version === 9 && (newRows > 90 || newCols > 256) && rd(file.bytes, 14) === 0) {
            change.splices.push({ abs: true, at: 14, removed: file.bytes.slice(14, 16), inserted: new Uint8Array([1, 0]) });
        }
        if (h < prev.length || h < next.length) change.splices.push({ abs: true, at: baseAt + h, removed: prev.slice(h, prev.length - t), inserted: next.slice(h, next.length - t) });
        return change;
    }

    return {
        EMPTY2: EMPTY2, MIN_ANTS: MIN_ANTS, MAX_ANTS: MAX_ANTS, MIN_STAGES: MIN_STAGES, MAX_STAGES: MAX_STAGES, HILL: HILL, TEAMS: TEAMS, FOOD: FOOD, FOOD_NAMES: FOOD_NAMES, FOOD_ITEMS: FOOD_ITEMS, PLANT_NAMES: PLANT_NAMES,
        team: team, teamOfMarker: teamOfMarker, typeOfMarker: typeOfMarker, markerTile: markerTile, teamOfHill: teamOfHill, clampTo: clampTo, foodItem: foodItem,
        layout: layout, block1Bytes: block1Bytes, block2Bytes: block2Bytes, block4Bytes: block4Bytes, diffSplice: diffSplice,
        hillCells: hillCells, placeHill: placeHill, startCells: startCells, placeStart: placeStart, eraseStart: eraseStart, placePile: placePile, placePlant: placePlant,
        eraseAt: eraseAt, resizeMap: resizeMap, inTheWay: inTheWay, blockedText: blockedText, facts: facts
    };
});
