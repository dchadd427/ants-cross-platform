// What the tools of the map maker can put on a map (docs/MAP_EDITOR.md): the seven tools, which of them draw in this version, and for the Ground and Objects tools the tiles that the palette
// offers, each with the plain name the person reads, the name that the tile has in the game's own dictionary (the names of Table 4 of ants.chd, as src/ants_assets/lvl_tile_names.inc
// lists them: the page has no ants.chd, and tests/scripts/web_mapmaker_edit_check.js holds this table to that list) and the color that the canvas already uses for it. The palette holds
// the plain tiles and the objects that stand on one cell; bigger objects (trees, flowers) come with a later change. A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_tiles.js'));
    else root.MapPalette = factory(root.MapTiles);
})(this, function (Tiles) {
    'use strict';

    // [id, the game's name for the tile, the name on the screen]
    var GROUND = [
        [485, 'g01a', 'Grass'], [486, 'g01b', 'Grass 2'], [487, 'd01a', 'Dirt'], [488, 'd01b', 'Dirt 2'], [571, 's01a', 'Sand'], [572, 's01b', 'Sand 2'],
        [439, 'M01a', 'Mud'], [476, 'w01a', 'Water']
    ];
    var OBJECTS = [
        [302, 'smlrock1', 'Rock 1'], [303, 'smlrock2', 'Rock 2'], [304, 'smlrock3', 'Rock 3'], [305, 'smlrock4', 'Rock 4'], [307, 'smlrock6', 'Rock 5'], [308, 'smlrock7', 'Rock 6'],
        [62, 'pu_comb', 'Combat ant power-up'], [63, 'pu_thief', 'Thief ant power-up'], [64, 'pu_bomb', 'Bomber ant power-up'], [65, 'pu_swim', 'Swimmer ant power-up'], [66, 'pu_mason', 'Fire ant power-up']
    ];
    // the tools: `draws` is true when the tool works in this version of the page; `key` is its letter; `layer` is what it puts on the map
    var TOOLS = [
        { id: 'ground', label: 'Ground', key: 'g', draws: true, palette: true },
        { id: 'objects', label: 'Objects', key: 'o', draws: true, palette: true },
        { id: 'hills', label: 'Hills', key: 'h', draws: false },
        { id: 'ants', label: 'Ants', key: 'a', draws: false },
        { id: 'food', label: 'Food', key: 'f', draws: false },
        { id: 'erase', label: 'Erase', key: 'e', draws: true },
        { id: 'move', label: 'Move', key: 'm', draws: false, pans: true }
    ];

    function items(tool) {
        var list = tool === 'ground' ? GROUND : (tool === 'objects' ? OBJECTS : []);
        return list.map(function (row) {
            return { id: row[0], name: row[1], label: row[2], color: tool === 'ground' ? Tiles.css(row[0], Tiles.EMPTY2) : Tiles.css(0, row[0]) };
        });
    }

    function tool(id) { for (var i = 0; i < TOOLS.length; i++) if (TOOLS[i].id === id) return TOOLS[i]; return null; }
    function toolOfKey(key) { var k = String(key).toLowerCase(); for (var i = 0; i < TOOLS.length; i++) if (TOOLS[i].key === k) return TOOLS[i]; return null; }
    // does the tool put cells on the map in this version (Ground, Objects, Erase)
    function paints(id) { var t = tool(id); return !!(t && t.draws); }
    // the tile that an item stands for, for a painting tool (null for Erase)
    function itemOf(id, tileId) { var list = items(id); for (var i = 0; i < list.length; i++) if (list[i].id === tileId) return list[i]; return null; }

    return { TOOLS: TOOLS, GROUND: GROUND, OBJECTS: OBJECTS, items: items, tool: tool, toolOfKey: toolOfKey, paints: paints, itemOf: itemOf };
});
