// The map maker's hills, starting ants and food piles (docs/MAP_EDITOR.md, "Hills, ants and food"), as plain arithmetic on the bytes of an open map. A hill is cells of layer 2 (4 x 4 round its
// entrance); the starting ants are records of block 1 (one record for each ant, the tile of the team's start marker, then row and column); a food pile is a record of block 2 (row, column, units,
// points for each unit, the number of stages, then a threshold and a tile for each stage): docs/MAP_FORMAT.md, "The layout". Nothing here touches a page and nothing changes the bytes: every
// function answers a CHANGE (the cells of layer 2 that get other words, the splices of the bytes after the layers, the names that the dictionary needs), and mapmaker_edit.js applies it, keeps
// it as a patch and undoes it. The splices are exact, the smallest piece of the file that differs, so everything else stays byte for byte as it was, and they hold their place as an offset
// from the end of layer 2, which does not move when the dictionary grows. Both kinds of file are handled: version 8 (an object's anchor is absolute) and version 9 (relative to its cell).
// A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_edit.js'));
    else root.MapBlocks = factory(root.MapEdit);
})(this, function (Edit) {
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
    var FOOD_NAMES = { 280: 'fdfrl1', 264: 'fdpretz1', 265: 'fdpretz2', 266: 'fdpretz3', 373: 'fdcraks1', 374: 'fdcraks2', 375: 'fdcraks3', 376: 'fdcraks4', 267: 'fdskit1', 268: 'fdskit2', 269: 'fdskit3', 270: 'fdskit4', 271: 'fdskit5' };

    function rd(b, at) { return b[at] | (b[at + 1] << 8); }
    function wr(b, at, v) { b[at] = v & 255; b[at + 1] = (v >> 8) & 255; }
    function team(index) { return TEAMS[index] || null; }
    function teamOfMarker(tile) { for (var i = 0; i < 4; i++) if (TEAMS[i].marker === tile) return i; return -1; }
    function teamOfHill(tile) { for (var i = 0; i < 4; i++) if (TEAMS[i].hill === tile) return i; return -1; }
    function clampTo(n, lo, hi) { n = Math.round(+n); return isNaN(n) ? lo : Math.max(lo, Math.min(hi, n)); }

    // ---- the lists after the layers ----

    // Where the two lists are and what is in them, read as the original reads them: block 1 (a word, then 6 bytes a record) and block 2 (a word, then a pile of 10 bytes and 4 for each stage).
    // `rel` is an offset from the end of layer 2 (file.blocksAt). A list that does not read (the file is cut short; a pile without stages, where the original stops reading the food) is not
    // `ok1` / `ok2`, and then it is not changed: the page says so. Answers {ok1, ok2, records: [{rel, tile, row, column}], b1: {rel, length}, piles: [{rel, length, row, column, units, points, stages: [[threshold, tile]]}], b2: {rel, length}}.
    function layout(file) {
        var cached = file.blockLayout;
        if (cached && cached.bytes === file.bytes && cached.size === file.bytes.length) return cached.value;
        var b = file.bytes, base = file.blocksAt, out = { ok1: false, ok2: false, records: [], piles: [], b1: null, b2: null }, at = base, i, n;
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
        return out;
    }

    // The bytes of a list: block 1 from its records, block 2 from its piles (a pile that is kept is copied from the file as it is)
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
    function placeStart(file, teamIndex, row, column, count) {
        var t = TEAMS[teamIndex], lay = layout(file);
        if (!t) return { error: 'There is no such team.' };
        if (!lay.ok1) return { error: 'The list of starting ants in this file cannot be changed here: the file ends in the middle of it.' };
        var n = clampTo(count, MIN_ANTS, MAX_ANTS), cells = startCells(file, lay, row, column, n, teamIndex), kept = [], insertAt = -1, i;
        for (i = 0; i < lay.records.length; i++) {
            if (lay.records[i].tile === t.marker) { if (insertAt < 0) insertAt = kept.length; } else kept.push(lay.records[i]);
        }
        if (insertAt < 0) insertAt = kept.length;
        var fresh = cells.map(function (cell) { return { tile: t.marker, row: cell.r, column: cell.c }; });
        var records = kept.slice(0, insertAt).concat(fresh, kept.slice(insertAt)), change = emptyChange(), splice = diffSplice(file, lay.b1.rel, lay.b1.length, block1Bytes(records));
        if (splice) change.splices.push(splice);
        addName(change, t.marker, t.markerName);
        return change;
    }

    function eraseStart(file, teamIndex) {
        var t = TEAMS[teamIndex], lay = layout(file), change = emptyChange();
        if (!t || !lay.ok1) return change;
        var kept = lay.records.filter(function (rec) { return rec.tile !== t.marker; });
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

    // A food pile put on (row, column) with `stages` stages (a pile that was on that cell goes)
    function placePile(file, row, column, stages) {
        var lay = layout(file);
        if (!lay.ok2) return { error: 'The list of food piles in this file cannot be changed here: it does not end where its count says.' };
        if (row < 0 || column < 0 || row >= file.rows || column >= file.columns) return { error: 'There is no room for a food pile there.' };
        var set = FOOD[clampTo(stages, MIN_STAGES, MAX_STAGES)], change = emptyChange(), i;
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

    // ---- taking things away (the Erase tool) ----

    // Where the ants and the piles of the lists are, by cell (made once for a reading of the lists: an eraser with a big brush asks for many cells)
    function indexOf(file, lay) {
        if (lay.index) return lay.index;
        var markers = {}, piles = {}, i;
        for (i = 0; i < lay.records.length; i++) {
            var rec = lay.records[i];
            if (teamOfMarker(rec.tile) >= 0 && rec.row < file.rows && rec.column < file.columns) (markers[rec.row * file.columns + rec.column] = markers[rec.row * file.columns + rec.column] || []).push(rec.tile);
        }
        for (i = 0; i < lay.piles.length; i++) {
            var p = lay.piles[i];
            if (p.row < file.rows && p.column < file.columns) (piles[p.row * file.columns + p.column] = piles[p.row * file.columns + p.column] || []).push(i);
        }
        lay.index = { markers: markers, piles: piles };
        return lay.index;
    }

    // What stands on the cell `idx` besides an object of layer 2: the start of a team (an ant of it is on the cell: all its ants go together) and a food pile (the cell is its anchor, or part of
    // the object that its anchor holds). A change, or null when there is nothing.
    function eraseAt(file, idx) {
        var lay = layout(file), ix = indexOf(file, lay), change = emptyChange(), i;
        var tiles = lay.ok1 ? ix.markers[idx] : null;
        if (tiles) {
            var gone = {};
            for (i = 0; i < tiles.length; i++) gone[tiles[i]] = true;
            var kept = lay.records.filter(function (rec) { return !gone[rec.tile]; });
            change.splices.push(diffSplice(file, lay.b1.rel, lay.b1.length, block1Bytes(kept)));
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
                change.splices.push(diffSplice(file, lay.b2.rel, lay.b2.length, block2Bytes(file, list)));
            }
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

    // What the map has now, for the palette's line: the cells of each team's hill and the ants and food piles on the map (-1 for a list that cannot be read)
    function facts(file, scanHills) {
        var lay = layout(file), out = { hills: [0, 0, 0, 0], ants: [0, 0, 0, 0], piles: lay.ok2 ? lay.piles.length : -1, listsOk: lay.ok1 && lay.ok2 }, i;
        if (scanHills) for (i = 0; i < 4; i++) out.hills[i] = hillCells(file, i).length;
        for (i = 0; i < lay.records.length; i++) { var tm = teamOfMarker(lay.records[i].tile); if (tm >= 0) out.ants[tm]++; }
        return out;
    }

    return {
        EMPTY2: EMPTY2, MIN_ANTS: MIN_ANTS, MAX_ANTS: MAX_ANTS, MIN_STAGES: MIN_STAGES, MAX_STAGES: MAX_STAGES, HILL: HILL, TEAMS: TEAMS, FOOD: FOOD, FOOD_NAMES: FOOD_NAMES,
        team: team, teamOfMarker: teamOfMarker, teamOfHill: teamOfHill, clampTo: clampTo,
        layout: layout, block1Bytes: block1Bytes, block2Bytes: block2Bytes, diffSplice: diffSplice,
        hillCells: hillCells, placeHill: placeHill, startCells: startCells, placeStart: placeStart, eraseStart: eraseStart, placePile: placePile, eraseAt: eraseAt, inTheWay: inTheWay, facts: facts
    };
});
