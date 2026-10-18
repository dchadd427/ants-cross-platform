// Which tiles are solid (docs/MAP_EDITOR.md, "Bigger objects"): the tile flag 0x01 of an id, the one that the original copies into the layer-1 solid bit of a cell when it puts an object there and
// clears when it takes the object away (SetTile; docs/reverse_engineering/map_loader_fidelity.md, section 1). The remake reads that bit from the file and from nowhere else, so an object that
// the editor puts must set it, or ants walk through a rock and a power-up does nothing. The table is the tile flags of src/ants_sim/movement_tables_data.inc (kTileFlags), bit 0 of each id, four
// ids to a hex digit (the lowest id in the lowest bit); tests/scripts/web_mapmaker_objects_check.js holds it to that file. A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapSolid = factory();
})(this, function () {
    'use strict';

    var COUNT = 1344;               // the ids of the game's tiles
    var BITS = '000000000000000c7000000000000000040000001000000000000000000fff1efffffffffffff7effffffffffffffffffffff020e0870000' +
        '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000' +
        '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000fff700';

    // is the tile's flag 0x01 set (false for an id that is not one of the game's tiles, and for an empty cell)
    function is(id) {
        if (id < 0 || id >= COUNT) return false;
        return ((parseInt(BITS.charAt(id >> 2), 16) >> (id & 3)) & 1) === 1;
    }

    // does an object of this tile own the solid bit of the cells it stands on (the editor sets it when it puts the object and clears it when it takes the object away): the solid tiles but the
    // hills, whose solid bits are the home's (a few cells of a library hill have the bit, and the eraser and a hill put again leave the bits of layer 1 as they were)
    function sets(id) { return is(id) && !(id >= 245 && id <= 248); }

    // does taking an object of this tile away clear the solid bit of its cells: every object but the hills (the original clears the bit whenever it clears a cell, whatever the tile: the library has
    // bombs, plants and other tiles that are not in the table and stand on cells that carry the bit); an empty cell is not an object
    function clears(id) { return id !== 0x7FFE && !(id >= 245 && id <= 248); }

    return { COUNT: COUNT, BITS: BITS, is: is, sets: sets, clears: clears };
});
