// The map maker's editing, as plain arithmetic on the bytes of a file (docs/MAP_EDITOR.md): the bytes of the open map are the one copy of the map, a stroke changes them in place, and what
// it changed is kept as a small patch (the cells that changed with their old and new words, never a copy of the map: a copy is about 3 MB at 512 x 512), so that undo and redo are a patch
// applied backwards or forwards. Nothing here touches a page: the stroke, the brush, the line between two cells, the fill, the removal of a whole object, the name that a tile needs in the
// dictionary and the bounded history are all tested on their own (tests/scripts/web_mapmaker_edit_check.js). A patch also holds the splices of a CHANGE (mapmaker_blocks.js: a hill, the
// starting ants, a food pile; mapmaker_props.js: the properties of the map), the pieces of the file that it took out and put in (after the layers, or in the header), so that they go in the same history
// as the strokes. A plain ES5 script that a page and node can load.
//
// The cells (docs/MAP_FORMAT.md, "The layout"): six bytes each, three words: the tile id, the flags (bit 0 of a layer-2 cell: the cell is the origin of its object) and a third word (a
// layer-1 cell: 0xCDCD or 0; a layer-2 cell: the anchor of its object, absolute in a version 8 file, a signed offset in a version 9 file). Bit 0 of a layer-1 cell's flags is the solid bit:
// an object whose tile is solid (mapmaker_solid.js; not a hill) sets it where it stands and clears it when it goes, as the original does.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_solid.js'));
    else root.MapEdit = factory(root.MapSolid);
})(this, function (Solid) {
    'use strict';

    var EMPTY2 = 0x7FFE;            // layer 2: no object in this cell
    var MAX_NAMES = 1344;           // the most names a dictionary can have (docs/MAP_FORMAT.md, "Limits")
    var MAX_STROKES = 200;          // the history keeps this many strokes ...
    var MAX_RECORDS = 400000;       // ... and this many changed cells in all (a record is about 16 bytes: a few megabytes at the most)
    var REACH = 32;                 // how far from its anchor the cells of one object are looked for
    var BRUSHES = [1, 2, 3, 4, 5, 6, 7, 8, 9];     // the sizes of the brush, in cells

    function rd(b, at) { return b[at] | (b[at + 1] << 8); }
    function wr(b, at, v) { b[at] = v & 255; b[at + 1] = (v >> 8) & 255; }
    function at(file, layer, idx) { return (layer === 1 ? file.layer1 : file.layer2) + 6 * idx; }
    function tile(file, layer, idx) { return rd(file.bytes, at(file, layer, idx)); }
    function flags(file, layer, idx) { return rd(file.bytes, at(file, layer, idx) + 2); }
    function third(file, layer, idx) { return rd(file.bytes, at(file, layer, idx) + 4); }
    function setCell(file, layer, idx, t, f, a) { var p = at(file, layer, idx), b = file.bytes; wr(b, p, t); wr(b, p + 2, f); wr(b, p + 4, a); }
    function s8(v) { return v > 127 ? v - 256 : v; }

    // The anchor that a layer-2 cell names, as the index of a cell, or -1 when it names a place outside the grid (a quirk of some old maps)
    function anchorIndex(file, idx) {
        var w = third(file, 2, idx), r, c;
        if (file.version === 8) { r = w & 255; c = w >> 8; }
        else {
            var row = (idx / file.columns) | 0, column = idx - row * file.columns;
            r = row + s8(w & 255); c = column + s8(w >> 8);
            if (file.rows <= 256 && file.columns <= 256) { r &= 255; c &= 255; }       // (a map of 256 x 256 or less wraps: docs/MAP_FORMAT.md, "Version 9")
        }
        return r < 0 || c < 0 || r >= file.rows || c >= file.columns ? -1 : r * file.columns + c;
    }

    // The third word of an object that stands on one cell and is its own anchor
    function ownAnchor(file, idx) {
        if (file.version !== 8) return 0;                                  // (an offset of nothing)
        var row = (idx / file.columns) | 0;
        return (row & 255) | (((idx - row * file.columns) & 255) << 8);
    }

    // The cells of the object that the layer-2 cell `idx` belongs to: the cells round its anchor that hold the same tile and name the same anchor, when the anchor cell is the origin of
    // such an object; any other cell (an anchor that is elsewhere or outside the map) stands alone. Empty for an empty cell.
    function objectCells(file, idx) {
        var t = tile(file, 2, idx);
        if (t === EMPTY2) return [];
        var a = anchorIndex(file, idx);
        if (a < 0 || tile(file, 2, a) !== t || !(flags(file, 2, a) & 1) || anchorIndex(file, a) !== a) return [idx];
        var ar = (a / file.columns) | 0, ac = a - ar * file.columns, found = [];
        for (var r = Math.max(0, ar - REACH); r <= Math.min(file.rows - 1, ar + REACH); r++) {
            for (var c = Math.max(0, ac - REACH); c <= Math.min(file.columns - 1, ac + REACH); c++) {
                var j = r * file.columns + c;
                if (tile(file, 2, j) === t && anchorIndex(file, j) === a) found.push(j);
            }
        }
        if (found.indexOf(idx) < 0) found.push(idx);
        return found;
    }

    // The cells that a brush of `size` covers when it is put on (row, column): a square round the cell, cut by the map's edge. {r0, r1, c0, c1}
    function brushRect(file, row, column, size) {
        var lo = Math.floor((size - 1) / 2), hi = size - 1 - lo;
        return { r0: Math.max(0, row - lo), r1: Math.min(file.rows - 1, row + hi), c0: Math.max(0, column - lo), c1: Math.min(file.columns - 1, column + hi) };
    }

    // The cells on the straight line from (r0, c0) to (r1, c1), the first not counted: a quick drag has no gaps. [[row, column], ...]
    function lineCells(r0, c0, r1, c1) {
        var out = [], dr = Math.abs(r1 - r0), dc = Math.abs(c1 - c0), sr = r0 < r1 ? 1 : -1, sc = c0 < c1 ? 1 : -1, err = dc - dr, r = r0, c = c0;
        while (r !== r1 || c !== c1) {
            var e2 = 2 * err;
            if (e2 > -dr) { err -= dr; c += sc; }
            if (e2 < dc) { err += dc; r += sr; }
            out.push([r, c]);
        }
        return out;
    }

    // The name a tile needs in the dictionary: a tile outside it is refused by the checker, and a name of "." is a tile that the original does not draw. `names` is the table of
    // MapPalette. Returns {bytes, file: null}: the same bytes when the tile is there or only its name is written (in place), a longer copy when the dictionary has to grow (then the
    // caller reads the file again: where the layers begin has moved), or {error} when the id is beyond the dictionary's largest size.
    function nameWord(name) { var out = [], i; for (i = 0; i < 11; i++) out.push(i < name.length ? name.charCodeAt(i) & 127 : 0); return out; }
    function ensureTile(file, id, name) {
        var b = file.bytes, count = file.names, slot = file.dictAt + 2 + 11 * id, i;
        if (id < 0 || id >= MAX_NAMES) return { error: 'tile ' + id + ' is beyond the dictionary' };
        if (id < count) {
            if (b[slot] === 0x2E && b[slot + 1] === 0 && name) { var word = nameWord(name); for (i = 0; i < 11; i++) b[slot + i] = word[i]; return { bytes: b, grew: false, named: true }; }
            return { bytes: b, grew: false, named: false };
        }
        var gap = id + 1 - count, extra = new Uint8Array(11 * gap), end = file.dictAt + 2 + 11 * count;
        for (i = 0; i < gap; i++) extra[11 * i] = 0x2E;
        if (name) { var last = nameWord(name); for (i = 0; i < 11; i++) extra[11 * (gap - 1) + i] = last[i]; }
        var out = new Uint8Array(b.length + extra.length);
        out.set(b.subarray(0, end), 0); out.set(extra, end); out.set(b.subarray(end), end + extra.length);
        wr(out, file.dictAt, id);                                          // the word is the number of names less one: the last is `id`
        return { bytes: out, grew: true, named: !!name };
    }

    // ---- the history: strokes as patches ----

    // A patch: keys (cell * 2 + layer - 1), the words before and the words after (three words a cell)
    function applyPatch(file, patch, forward) {
        var words = forward ? patch.after : patch.before, dirty = [];
        for (var i = 0; i < patch.keys.length; i++) {
            var key = patch.keys[i], layer = (key & 1) + 1, idx = key >>> 1;
            setCell(file, layer, idx, words[3 * i], words[3 * i + 1], words[3 * i + 2]);
            dirty.push(idx);
        }
        return dirty;
    }

    // What a patch costs the history: a record for each cell, and one for each 16 bytes of a splice (the bytes it took out and the bytes it put in)
    function weight(patch) {
        var n = patch.keys.length;
        for (var i = 0; patch.splices && i < patch.splices.length; i++) n += 1 + ((patch.splices[i].removed.length + patch.splices[i].inserted.length + 15) >> 4);
        return n;
    }
    function spliceAt(bytes, at, removeLength, inserted) {
        var out = new Uint8Array(bytes.length - removeLength + inserted.length);
        out.set(bytes.subarray(0, at), 0); out.set(inserted, at); out.set(bytes.subarray(at + removeLength), at + inserted.length);
        return out;
    }
    // Where a splice is in the file as it stands: a splice of the lists after the layers counts from the end of layer 2 (`rel`), one of the header counts from the start (`abs`, `at`)
    function spliceStart(file, s) { return s.abs ? s.at : file.blocksAt + s.rel; }
    function sameBytes(bytes, at, want) { if (at < 0 || at + want.length > bytes.length) return false; for (var i = 0; i < want.length; i++) if (bytes[at + i] !== want[i]) return false; return true; }

    // `apply(patch, forward)` puts a patch on the map (cells and splices) and answers the cells to paint again
    function createHistory() {
        var undo = [], redo = [], records = 0, dropped = 0, serial = 0, savedId = 0, savedDropped = 0;
        function lastId() { return undo.length ? undo[undo.length - 1].id : 0; }
        return {
            push: function (patch) {
                redo = [];
                patch.id = ++serial;
                undo.push(patch); records += weight(patch);
                while (undo.length > 1 && (undo.length > MAX_STROKES || records > MAX_RECORDS)) { records -= weight(undo.shift()); dropped++; }
            },
            undo: function (apply) { var p = undo.pop(); if (!p) return null; records -= weight(p); redo.push(p); return apply(p, false); },
            redo: function (apply) { var p = redo.pop(); if (!p) return null; undo.push(p); records += weight(p); return apply(p, true); },
            canUndo: function () { return undo.length > 0; },
            canRedo: function () { return redo.length > 0; },
            // no change has been made since the map was opened (or the last change was undone and nothing was ever dropped)
            isClean: function () { return undo.length === 0 && dropped === 0; },
            // the map was handed over (downloaded) as it is now; it is saved until another change, or an undo, moves it
            markSaved: function () { savedId = lastId(); savedDropped = dropped; },
            markUnsaved: function () { savedId = -1; },           // the map came from work that was not saved (the kept work of an earlier visit)
            isSaved: function () { return lastId() === savedId && dropped === savedDropped; },
            size: function () { return { strokes: undo.length, redo: redo.length, records: records, dropped: dropped }; }
        };
    }

    // ---- the editor: strokes on one open map ----

    // `file` is the reader's view of the bytes (MapFile.parse). `hooks.rebase(bytes)` gives the reader's view of other bytes (the dictionary grew, or a splice made the file longer or shorter:
    // the page reads them again and the canvas keeps its view), `hooks.erase(file, idx)` answers what else the eraser takes from a cell (a change, or null: mapmaker_blocks.js).
    function create(file, hooks) {
        var history = createHistory(), seen = null;
        var self = { history: history, file: file };
        hooks = hooks || {};

        // The splices of a patch, put on the bytes in the order they were made (forwards) or the other way round (backwards); the cells of the patch are put first, in place
        function applyFull(patch, forward) {
            var dirty = applyPatch(self.file, patch, forward), list = patch.splices || [], i;
            for (i = 0; i < list.length; i++) {
                var s = list[forward ? i : list.length - 1 - i], at = spliceStart(self.file, s), out = forward ? s.removed : s.inserted, into = forward ? s.inserted : s.removed;
                if (!sameBytes(self.file.bytes, at, out)) continue;                      // (cannot be: the patch is the exact inverse of what it was made from)
                self.file = hooks.rebase(spliceAt(self.file.bytes, at, out.length, into));
            }
            return dirty;
        }

        function begin(kind, tileId) {
            if (!seen || seen.length !== 2 * self.file.cells) seen = new Uint8Array(2 * self.file.cells);
            var f = self.file, keys = [], before = [], dirty = [], splices = [], closed = false, refused = '';

            function touch(layer, idx) {
                var key = idx * 2 + layer - 1;
                if (seen[key]) return;
                seen[key] = 1; keys.push(key);
                before.push(tile(f, layer, idx), flags(f, layer, idx), third(f, layer, idx));
            }
            // The solid bit of a cell of layer 1 (the other flags stay); a cell that already is as asked is not touched. Row 0 never loses it (the original's rule).
            function setSolid(idx, on) {
                if (!on && idx < f.columns) return;
                var was = flags(f, 1, idx);
                if ((was & 1) === (on ? 1 : 0)) return;
                touch(1, idx); setCell(f, 1, idx, tile(f, 1, idx), on ? (was | 1) : (was & ~1), third(f, 1, idx));
            }
            function removeObject(idx) {
                var cells = objectCells(f, idx), n = 0;
                for (var i = 0; i < cells.length; i++) {
                    var gone = tile(f, 2, cells[i]);
                    touch(2, cells[i]); setCell(f, 2, cells[i], EMPTY2, 0, 0); dirty.push(cells[i]); n++;
                    if (Solid.clears(gone)) setSolid(cells[i], false);
                }
                return n;
            }
            function putObject(idx) {
                var why = hooks.guard ? hooks.guard(f, idx) : '';                  // (a hill, a team's ants or a food pile is never taken away by a tap or a brush)
                if (why) { refused = why; return 0; }
                if (tile(f, 2, idx) === tileId && flags(f, 2, idx) === 1 && third(f, 2, idx) === ownAnchor(f, idx)) return 0;
                var n = 0, old = tile(f, 2, idx);
                if (old !== EMPTY2 && objectCells(f, idx).length > 1) n += removeObject(idx);       // (a bigger object goes whole: never half a hill)
                else if (Solid.clears(old) && !Solid.sets(tileId)) setSolid(idx, false);
                touch(2, idx); setCell(f, 2, idx, tileId, 1, ownAnchor(f, idx)); dirty.push(idx);
                if (Solid.sets(tileId)) setSolid(idx, true);
                return n + 1;
            }
            function putGround(idx) {
                if (tile(f, 1, idx) === tileId) return 0;
                touch(1, idx); setCell(f, 1, idx, tileId, flags(f, 1, idx), third(f, 1, idx)); dirty.push(idx);
                return 1;
            }
            // A change of mapmaker_blocks.js: the dictionary's names, the cells of layer 2 and the splices of the bytes after the layers, in this order. Returns the cells and splices it made.
            function change(ch) {
                var n = 0, i;
                for (i = 0; i < ch.names.length; i++) {
                    var made = ensureTile(f, ch.names[i][0], ch.names[i][1]);
                    if (made.grew) f = self.file = hooks.rebase(made.bytes);
                }
                for (i = 0; i < ch.cells.length; i++) { var c = ch.cells[i], was = tile(f, 2, c[0]); touch(2, c[0]); setCell(f, 2, c[0], c[1], c[2], c[3]); dirty.push(c[0]); n++; if (c[1] === EMPTY2 && Solid.clears(was)) setSolid(c[0], false); }
                for (i = 0; ch.ground && i < ch.ground.length; i++) { var g = ch.ground[i]; touch(1, g[0]); setCell(f, 1, g[0], g[1], flags(f, 1, g[0]), third(f, 1, g[0])); dirty.push(g[0]); n++; }          // (the tiles of layer 1 that mapmaker_edges.js picks again: their flags stay)
                for (i = 0; ch.solid && i < ch.solid.length; i++) setSolid(ch.solid[i][0], ch.solid[i][1]);                 // (the solid bits of layer 1 that go with the cells)
                for (i = 0; i < ch.splices.length; i++) {
                    var s = ch.splices[i];
                    f = self.file = hooks.rebase(spliceAt(f.bytes, spliceStart(f, s), s.removed.length, s.inserted));
                    splices.push(s); n++;
                }
                return n;
            }
            function putErase(idx) {
                var more = hooks.erase ? hooks.erase(f, idx) : null, n = tile(f, 2, idx) === EMPTY2 ? 0 : removeObject(idx);       // (what else goes is asked first: it may be told by the object's anchor)
                return more ? n + change(more) : n;
            }
            var put = kind === 'ground' ? putGround : (kind === 'objects' ? putObject : putErase);

            var stroke = {
                kind: kind,
                change: change,
                // the brush put on a cell; returns the number of cells that changed
                stamp: function (row, column, size) {
                    var rect = brushRect(f, row, column, size || 1), n = 0;
                    if (kind === 'objects' && hooks.guard) {                      // (the whole brush is refused, not the cells that are free)
                        for (var gr = rect.r0; gr <= rect.r1; gr++) for (var gc = rect.c0; gc <= rect.c1; gc++) { var why = hooks.guard(f, gr * f.columns + gc); if (why) { refused = why; return 0; } }
                    }
                    for (var r = rect.r0; r <= rect.r1; r++) for (var c = rect.c0; c <= rect.c1; c++) n += put(r * f.columns + c);
                    return n;
                },
                // the brush along the line from the last cell to this one (the last cell is not stamped again)
                segment: function (r0, c0, r1, c1, size) {
                    var line = lineCells(r0, c0, r1, c1), n = 0;
                    for (var i = 0; i < line.length; i++) n += stroke.stamp(line[i][0], line[i][1], size);
                    return n;
                },
                // fill the region of cells that are alike (4 neighbors) with the tile: the ground of the same tile, or the empty cells of the objects' layer (a field that has an object in it
                // is not filled over: the eraser takes objects away, whole)
                fill: function (row, column) {
                    if (kind === 'erase') return 0;
                    var layer = kind === 'ground' ? 1 : 2, start = row * f.columns + column, want = tile(f, layer, start);
                    if (layer === 2 && want !== EMPTY2) return 0;
                    var cells = f.cells, mark = new Uint8Array(cells), stack = new Int32Array(cells), top = 0, n = 0;
                    stack[top++] = start; mark[start] = 1;
                    while (top) {
                        var i = stack[--top], r = (i / f.columns) | 0, c = i - r * f.columns;
                        n += put(i);
                        if (c > 0 && !mark[i - 1] && tile(f, layer, i - 1) === want) { mark[i - 1] = 1; stack[top++] = i - 1; }
                        if (c < f.columns - 1 && !mark[i + 1] && tile(f, layer, i + 1) === want) { mark[i + 1] = 1; stack[top++] = i + 1; }
                        if (r > 0 && !mark[i - f.columns] && tile(f, layer, i - f.columns) === want) { mark[i - f.columns] = 1; stack[top++] = i - f.columns; }
                        if (r < f.rows - 1 && !mark[i + f.columns] && tile(f, layer, i + f.columns) === want) { mark[i + f.columns] = 1; stack[top++] = i + f.columns; }
                    }
                    return n;
                },
                // the sentence for what was in the way since the last call ('' when nothing was)
                refusal: function () { var r = refused; refused = ''; return r; },
                // the cells changed since the last call (a cell can be named twice), for the part of the screen to paint again
                takeDirty: function () { var d = dirty; dirty = []; return d; },
                // The patch of the stroke, ready for the history: only the cells whose words really differ from before
                finish: function () {
                    var k = [], b = [], a = [];
                    for (var i = 0; i < keys.length; i++) {
                        var key = keys[i], layer = (key & 1) + 1, idx = key >>> 1, t = tile(f, layer, idx), fl = flags(f, layer, idx), th = third(f, layer, idx);
                        seen[key] = 0;
                        if (t === before[3 * i] && fl === before[3 * i + 1] && th === before[3 * i + 2]) continue;
                        k.push(key); b.push(before[3 * i], before[3 * i + 1], before[3 * i + 2]); a.push(t, fl, th);
                    }
                    var patch = { keys: Uint32Array.from(k), before: Uint16Array.from(b), after: Uint16Array.from(a) };
                    if (splices.length) patch.splices = splices.slice();
                    return patch;
                },
                // the stroke is done: its patch goes to the history. Returns the number of cells it changed.
                end: function () {
                    if (closed) return 0;
                    closed = true;
                    var patch = stroke.finish();
                    if (patch.keys.length || patch.splices) history.push(patch);
                    return patch.keys.length + (patch.splices ? patch.splices.length : 0);
                },
                // the stroke is taken back (a second finger came down): the cells go back, nothing is kept. Returns the cells to paint again.
                cancel: function () {
                    if (closed) return [];
                    closed = true;
                    var patch = stroke.finish();
                    return applyFull(patch, false);
                }
            };
            return stroke;
        }

        self.begin = begin;
        self.rebase = function (next) { self.file = next; };
        // One change of mapmaker_blocks.js as one step of the history. Returns the cells to paint again, or null when it changed nothing.
        self.act = function (ch) {
            var stroke = begin('act');
            stroke.change(ch);
            var n = stroke.end();
            return n ? stroke.takeDirty() : null;
        };
        self.undo = function () { return history.undo(applyFull); };
        self.redo = function () { return history.redo(applyFull); };
        return self;
    }

    return {
        EMPTY2: EMPTY2, MAX_NAMES: MAX_NAMES, MAX_STROKES: MAX_STROKES, MAX_RECORDS: MAX_RECORDS, BRUSHES: BRUSHES,
        create: create, createHistory: createHistory, applyPatch: applyPatch, weight: weight, spliceAt: spliceAt, ensureTile: ensureTile, objectCells: objectCells, anchorIndex: anchorIndex, ownAnchor: ownAnchor,
        brushRect: brushRect, lineCells: lineCells, tile: tile, flags: flags, third: third
    };
});
