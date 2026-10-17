// What the tools of the map maker can put on a map (docs/MAP_EDITOR.md): the seven tools, which of them draw in this version, and for the Ground and Objects tools the tiles that the palette
// offers, each with the plain name the person reads, the name that the tile has in the game's own dictionary (the names of Table 4 of ants.chd, as src/ants_assets/lvl_tile_names.inc
// lists them: the page has no ants.chd, and tests/scripts/web_mapmaker_edit_check.js holds this table to that list) and the color that the canvas already uses for it. The palette holds
// the plain tiles and the objects that stand on one cell; bigger objects (trees, flowers) come with a later change. Hills, Ants and Food put a whole thing with one tap (the team, the number of
// ants, the stages of a pile are their options: mapmaker_blocks.js). A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_tiles.js'), require('./mapmaker_blocks.js'));
    else root.MapPalette = factory(root.MapTiles, root.MapBlocks);
})(this, function (Tiles, Blocks) {
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
    // the tools: `draws` is true for a tool that paints cells with a brush (a drag paints a line), `places` for a tool that puts one whole thing with a tap (a drag looks around), `pans` for the
    // one that only looks around; `key` is its letter; `palette` is true when the tool has a palette (a box beside the map, a sheet on a phone)
    var TOOLS = [
        { id: 'ground', label: 'Ground', key: 'g', draws: true, palette: true },
        { id: 'objects', label: 'Objects', key: 'o', draws: true, palette: true },
        { id: 'hills', label: 'Hills', key: 'h', places: true, palette: true },
        { id: 'ants', label: 'Ants', key: 'a', places: true, palette: true },
        { id: 'food', label: 'Food', key: 'f', places: true, palette: true },
        { id: 'erase', label: 'Erase', key: 'e', draws: true },
        { id: 'move', label: 'Move', key: 'm', pans: true }
    ];

    function items(tool) {
        var list = tool === 'ground' ? GROUND : (tool === 'objects' ? OBJECTS : []);
        return list.map(function (row) {
            return { id: row[0], name: row[1], label: row[2], color: tool === 'ground' ? Tiles.css(row[0], Tiles.EMPTY2) : Tiles.css(0, row[0]) };
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
    function teams() { return Blocks.TEAMS.map(function (t, i) { return { id: i, key: t.key, label: t.label, color: Tiles.TEAMS[t.key] }; }); }
    // the tile that an item stands for, for a painting tool (null for Erase)
    function itemOf(id, tileId) { var list = items(id); for (var i = 0; i < list.length; i++) if (list[i].id === tileId) return list[i]; return null; }

    return { TOOLS: TOOLS, GROUND: GROUND, OBJECTS: OBJECTS, items: items, tool: tool, toolOfKey: toolOfKey, paints: paints, places: places, usable: usable, teams: teams, itemOf: itemOf };
});
