// The map maker's bigger objects (docs/MAP_EDITOR.md, "Bigger objects"), as plain arithmetic on the bytes of an open map. An object that covers several cells is the same tile on each cell
// of its footprint on layer 2, every cell anchored at the cell that was tapped (the origin, the only one flagged), and a solid bit on layer 1 under each cell when the tile is solid: the shape
// that the library's maps have (the footprints are mapmaker_palette.js's BIGGER, measured from the 540 maps). Nothing here touches a page and nothing changes the bytes: `place` answers
// a CHANGE (the names for the dictionary, the cells of layer 2, the solid bits), as mapmaker_blocks.js does, and mapmaker_edit.js applies it as one step of the history. The eraser needs
// nothing of its own: it takes the cells round an anchor that hold the tile whole (mapmaker_edit.js). Both kinds of file are handled: version 8 (an object's anchor is absolute) and version 9
// (relative to its cell). A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_edit.js'), require('./mapmaker_blocks.js'), require('./mapmaker_solid.js'));
    else root.MapObjects = factory(root.MapEdit, root.MapBlocks, root.MapSolid);
})(this, function (Edit, Blocks, Solid) {
    'use strict';

    var EMPTY2 = 0x7FFE;            // layer 2: no object in this cell

    var IN_THE_WAY = {
        hill: 'A hill is in the way. Take it away with Erase first, or put this a little further off.',
        ants: 'Starting ants are in the way. Move them first, or put this a little further off.',
        pile: 'A food pile is in the way. Take it away with Erase first, or put this a little further off.'
    };

    // The cells of the footprint of `item` ({cells: [[row, column], ...] from the anchor}) put with its anchor on (row, column), cut by the map's edge: [{idx, r, c}]
    function footprint(file, item, row, column) {
        var out = [];
        for (var i = 0; i < item.cells.length; i++) {
            var r = row + item.cells[i][0], c = column + item.cells[i][1];
            if (r >= 0 && c >= 0 && r < file.rows && c < file.columns) out.push({ idx: r * file.columns + c, r: r, c: c });
        }
        return out;
    }

    // The bigger object `item` ({id, name, cells, ...}: mapmaker_palette.js) put with its anchor on (row, column). An object that is under the new one goes whole (never half of it left), and what
    // another tool owns is never taken away without being asked: a hill, starting ants and a food pile in the way give {error}, as does an anchor off the map. The same object put on the same
    // place again is a change that changes nothing.
    function place(file, item, row, column) {
        if (!item || !item.cells || row < 0 || column < 0 || row >= file.rows || column >= file.columns) return { error: 'There is no room for it there.' };
        var cells = footprint(file, item, row, column), change = { names: [[item.id, item.name]], cells: [], solid: [], splices: [] }, i, j;
        for (i = 0; i < cells.length; i++) {
            var why = Blocks.inTheWay(file, cells[i].idx);
            if (why) return { error: IN_THE_WAY[why] };
        }
        var words = {}, order = [];
        function put(idx, t, f, a) { if (!(idx in words)) order.push(idx); words[idx] = [t, f, a]; }
        for (i = 0; i < cells.length; i++) {
            var idx = cells[i].idx, old = Edit.tile(file, 2, idx);
            if (old === EMPTY2 || (idx in words)) continue;
            var gone = Edit.objectCells(file, idx);                            // (an object under the new one goes whole)
            for (j = 0; j < gone.length; j++) {
                if (gone[j] in words) continue;
                put(gone[j], EMPTY2, 0, 0);                                    // (the solid bit of a cell that is emptied goes with it: mapmaker_edit.js)
            }
        }
        for (i = 0; i < cells.length; i++) {
            var cell = cells[i], origin = cell.r === row && cell.c === column;
            put(cell.idx, item.id, origin ? 1 : 0, file.version === 8 ? ((row & 255) | ((column & 255) << 8)) : (((row - cell.r) & 255) | (((column - cell.c) & 255) << 8)));
            if (Solid.sets(item.id)) change.solid.push([cell.idx, true]);
        }
        for (i = 0; i < order.length; i++) {
            var at = order[i], w = words[at];
            if (Edit.tile(file, 2, at) === w[0] && Edit.flags(file, 2, at) === w[1] && Edit.third(file, 2, at) === w[2]) continue;      // (already so)
            change.cells.push([at, w[0], w[1], w[2]]);
        }
        change.solid = change.solid.filter(function (s) { return !!(Edit.flags(file, 1, s[0]) & 1) !== s[1] && (s[1] || s[0] >= file.columns); });
        return change;
    }

    return { footprint: footprint, place: place, IN_THE_WAY: IN_THE_WAY };
});
