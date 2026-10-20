// What the tools of the map maker can put on a map (docs/MAP_EDITOR.md): the eight tools, which of them draw in this version, and for the Ground and Objects tools the tiles that the palette
// offers, each with the plain name the person reads, the name that the tile has in the game's own dictionary (the names of Table 4 of ants.chd, as src/ants_assets/lvl_tile_names.inc
// lists them: the page has no ants.chd, and tests/scripts/web_mapmaker_edit_check.js holds this table to that list) and the color that the canvas uses for it when it has no picture. The palette holds
// the plain tiles and the objects that stand on one cell; the bigger objects (the Bigger tool: grass, leaves, rocks, toys, each over several cells) are a curated list with their footprints.
// Hills, Ants, Food and Bigger put a whole thing with one tap (the team, the number of ants, the stages of a pile are the options of the first three: mapmaker_blocks.js). A plain ES5 script
// that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_tiles.js'), require('./mapmaker_blocks.js'), require('./mapmaker_solid.js'));
    else root.MapPalette = factory(root.MapTiles, root.MapBlocks, root.MapSolid);
})(this, function (Tiles, Blocks, Solid) {
    'use strict';

    // The five kinds of ground, one chip each, painting the kind's plain tile: [id, the game's name for the tile, the name on the screen]. Gravel, Dirt, Slate, Mud, Water: there is no grass and no sand
    // (the first letter of a ground tile's name is its kind). Every other ground tile, the edge and corner pieces, is reached from the kind's own sheet (mapmaker_ground.js).
    var GROUND = [[485, 'g01a', 'Gravel'], [487, 'd01a', 'Dirt'], [571, 's01a', 'Slate'], [439, 'M01a', 'Mud'], [476, 'w01a', 'Water']];
    var OBJECTS = [
        [302, 'smlrock1', 'Rock 1'], [303, 'smlrock2', 'Rock 2'], [304, 'smlrock3', 'Rock 3'], [305, 'smlrock4', 'Rock 4'], [307, 'smlrock6', 'Rock 5'], [308, 'smlrock7', 'Rock 6'],
        [62, 'pu_comb', 'Combat ant power-up'], [63, 'pu_thief', 'Thief ant power-up'], [64, 'pu_bomb', 'Bomber ant power-up'], [65, 'pu_swim', 'Swimmer ant power-up'], [66, 'pu_mason', 'Fire ant power-up']
    ];
    var PLANTS = [
        [404, 'clover1', 'Clover 1'], [405, 'clover2', 'Clover 2'], [406, 'clover3', 'Clover 3'], [407, 'clover4', 'Clover 4'], [408, 'clovers', 'Clover patch'],
        [410, 'flower1a', 'Flower 1a'], [411, 'flower1b', 'Flower 1b'], [412, 'flower2a', 'Flower 2a'], [413, 'flower2b', 'Flower 2b'],
        [414, 'flower3a', 'Flower 3a'], [415, 'flower3b', 'Flower 3b'], [416, 'flower3c', 'Flower 3c'], [420, 'dflower1', 'Daisy flower'], [421, 'flower1', 'Flower 1']
    ];
    // The bigger objects: [the tile id, the game's name for the tile, the cells it covers as 'row,column' offsets from its anchor (the cell that is tapped)]. They are the objects that stand on
    // more than one cell in at least 30 of the 540 maps of the library, most used first (the library check, tests/scripts/web_mapmaker_objects_check.js, measures it again), without the
    // hills, the bombs, the power-ups, the food and the flowers and clovers that the flower droppers read (each of those has its own tool, or comes later). Every cell of such an object
    // holds the same tile; the anchor cell is the origin and the only one flagged. The footprint is the one that the library's maps have most often for the tile (for 27 of the 29 it is also
    // the one that the game computes from the sprite, object_footprint.cpp; for can1 and sling the maps have one cell less, and the maps win).
    var BIGGER = [
        [1323, 'grass4', '-1,-1 -1,0 0,-1 0,0'],
        [1324, 'grassmed1', '-1,-2 -1,-1 -1,0 -1,1 -1,2 0,-2 0,-1 0,0 0,1 0,2'],
        [1321, 'grass2', '-2,-1 -2,0 -1,-1 -1,0 0,-1 0,0'],
        [1322, 'grass3', '-1,-1 -1,0 0,-1 0,0'],
        [1320, 'grass1', '-2,-1 -2,0 -1,-1 -1,0 0,-1 0,0'],
        [1326, 'grassmed2', '0,-1 0,0 1,-1 1,0 2,-1 2,0 3,-1 3,0'],
        [301, 'medrock9', '0,0 0,1'],
        [237, 'bobber', '-1,0 -1,1 0,0 0,1'],
        [1329, 'grassmed4', '0,0 0,1 1,0 1,1 1,2 2,1 2,2 2,3 3,2 3,3'],
        [1327, 'grassmed3', '0,-1 0,0 1,-2 1,-1 1,0 2,-3 2,-2 2,-1 2,0 3,-3 3,-2 3,-1'],
        [242, 'slinky', '-2,-2 -2,-1 -2,0 -2,1 -1,-2 -1,-1 -1,0 -1,1 0,-2 0,-1 0,0 0,1'],
        [346, 'bearbod', '-3,-1 -3,0 -3,1 -2,-2 -2,-1 -2,0 -2,1 -2,2 -1,-3 -1,-2 -1,-1 -1,0 -1,1 -1,2 0,-3 0,-2 0,-1 0,0 0,1 0,2 1,-2 1,-1 1,0 1,1 1,2 2,-2 2,-1 2,0 2,1 2,2'],
        [329, 'mush2a', '-2,-2 -2,-1 -2,0 -2,1 -1,-2 -1,-1 -1,0 -1,1 0,-2 0,-1 0,0 0,1 1,-2 1,-1 1,0 1,1'],
        [236, 'glasses', '-1,-3 -1,-2 -1,-1 -1,0 -1,1 -1,2 -1,3 0,-3 0,-2 0,-1 0,0 0,1 0,2 0,3 1,-1 1,0 1,1 1,2 1,3'],
        [238, 'can1', '-2,-2 -2,-1 -2,0 -2,1 -1,-2 -1,-1 -1,0 -1,1 -1,2 0,-2 0,-1 0,0 0,1 0,2 1,-2 1,-1 1,0 1,1 1,2 2,-1 2,0 2,1'],
        [350, 'sling', '-1,-1 -1,0 -1,1 0,0 0,1 1,1'],
        [326, 'leaf4', '-2,-1 -2,0 -1,-1 -1,0 0,-1 0,0 1,0'],
        [418, 'ivylft', '-2,-1 -2,0 -1,-1 -1,0 0,-1 0,0'],
        [417, 'ivyrt', '-1,-1 -1,0 -1,1 0,-1 0,0 0,1'],
        [289, 'bigrock1', '-1,-2 -1,-1 -1,0 -1,1 0,-2 0,-1 0,0 0,1'],
        [325, 'leaf3', '-2,-1 -2,0 -2,1 -1,-2 -1,-1 -1,0 -1,1 0,-2 0,-1 0,0 1,-2 1,-1'],
        [409, 'plant1', '-2,-1 -2,0 -1,-2 -1,-1 -1,0 -1,1 0,-2 0,-1 0,0 0,1 1,-2 1,-1 1,0 1,1'],
        [324, 'leaf2', '-3,-1 -3,0 -2,-2 -2,-1 -2,0 -2,1 -2,2 -1,-2 -1,-1 -1,0 -1,1 -1,2 0,-2 0,-1 0,0 0,1 0,2 1,-2 1,-1 1,0 1,1 1,2'],
        [349, 'ducky2', '-1,0 -1,1 0,-2 0,-1 0,0 0,1 1,-2 1,-1 1,0 1,1 2,-1 2,0'],
        [315, 'pin3', '0,0 1,0'],
        [345, 'bearhed', '-2,-2 -2,-1 -2,0 -2,1 -1,-2 -1,-1 -1,0 -1,1 0,-2 0,-1 0,0 0,1 1,-1 1,0 1,1'],
        [292, 'bigrock4', '-1,-1 -1,0 0,-1 0,0'],
        [354, 'yelcray', '-1,0 0,-1 0,0 1,-1'],
        [352, 'pinkcray', '-1,0 0,0 1,0']
    ];
    // the tools: `draws` is true for a tool that paints cells with a brush (a drag paints a line), `places` for a tool that puts one whole thing with a tap (a drag looks around), `pans` for the
    // one that only looks around; `key` is its letter; `palette` is true when the tool has a palette (a box beside the map, a sheet on a phone)
    var TOOLS = [
        { id: 'ground', label: 'Ground', key: 'g', draws: true, palette: true },
        { id: 'objects', label: 'Objects', key: 'o', draws: true, palette: true },
        { id: 'bigger', label: 'Bigger', key: 'k', places: true, palette: true },
        { id: 'hills', label: 'Hills', key: 'h', places: true, palette: true },
        { id: 'ants', label: 'Ants', key: 'a', places: true, palette: true },
        { id: 'food', label: 'Food', key: 'f', places: true, palette: true },
        { id: 'erase', label: 'Erase', key: 'e', draws: true },
        { id: 'move', label: 'Move', key: 'm', pans: true }
    ];

    function footprint(text) { return text.split(' ').map(function (pair) { var rc = pair.split(','); return [+rc[0], +rc[1]]; }); }
    var biggerItems = null;
    // the bigger objects, each {id, name, label (the game's name), color, cells: [[row, column], ...] from the anchor, size: {rows, columns, r0, c0}, solid}
    function bigger() {
        if (!biggerItems) biggerItems = BIGGER.map(function (row) {
            var cells = footprint(row[2]), r0 = 0, r1 = 0, c0 = 0, c1 = 0;
            cells.forEach(function (rc) { r0 = Math.min(r0, rc[0]); r1 = Math.max(r1, rc[0]); c0 = Math.min(c0, rc[1]); c1 = Math.max(c1, rc[1]); });
            return { id: row[0], name: row[1], label: row[1], color: Tiles.css(0, row[0]), cells: cells, box: { r0: r0, r1: r1, c0: c0, c1: c1 }, solid: Solid.sets(row[0]) };
        });
        return biggerItems;
    }

    function food() {
        return (Blocks.FOOD_ITEMS || []).map(function (row) {
            return { id: row[0], name: Blocks.FOOD_NAMES[row[0]], label: row[1], units: row[2], points: row[3], tiles: row[4], stages: row[4].length + 1, color: Tiles.FOOD };
        });
    }

    function items(tool) {
        if (tool === 'bigger') return bigger();
        if (tool === 'food') return food();
        var list = tool === 'ground' ? GROUND : (tool === 'objects' ? OBJECTS.concat(PLANTS) : (tool === 'plants' ? PLANTS : []));
        return list.map(function (row) {
            return { id: row[0], name: row[1], label: row[2], kind: tool === 'ground' ? Tiles.kindOf(row[0]) : null, color: tool === 'ground' ? Tiles.css(row[0], Tiles.EMPTY2) : Tiles.css(0, row[0]), plant: !!(Blocks.PLANT_NAMES && Blocks.PLANT_NAMES[row[0]]) };
        });
    }

    function tool(id) { for (var i = 0; i < TOOLS.length; i++) if (TOOLS[i].id === id) return TOOLS[i]; return null; }
    function toolOfKey(key) { var k = String(key).toLowerCase(); for (var i = 0; i < TOOLS.length; i++) if (TOOLS[i].key === k) return TOOLS[i]; return null; }
    // does the tool paint with a brush (Ground, Objects, Erase)
    function paints(id) { var t = tool(id); return !!(t && t.draws); }
    // does the tool put a whole thing with one tap (Hills, Ants, Food)
    function places(id) { var t = tool(id); return !!(t && t.places); }
    // can the tool be chosen
    function usable(id) { var t = tool(id); return !!(t && (t.draws || t.places || t.pans)); }
    // the four teams of Hills and Ants, as the swatches show them: [{id: 0..3 (the game's seat), label, color}]
    function teams() { return Blocks.TEAMS.map(function (t, i) { return { id: i, key: t.key, label: t.label, color: Tiles.TEAMS[t.key], hill: t.hill }; }); }
    // the tile that an item stands for, for a painting tool (null for Erase)
    function itemOf(id, tileId) { var list = items(id); for (var i = 0; i < list.length; i++) if (list[i].id === tileId) return list[i]; return null; }

    return { TOOLS: TOOLS, GROUND: GROUND, OBJECTS: OBJECTS, PLANTS: PLANTS, BIGGER: BIGGER, items: items, tool: tool, toolOfKey: toolOfKey, paints: paints, places: places, usable: usable, teams: teams, itemOf: itemOf };
});
