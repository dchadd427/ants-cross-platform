// The colors that stand for the tiles on the map maker's canvas (docs/MAP_EDITOR.md): one color for a kind of ground and a few for the kinds of object. They are what the canvas shows while the
// game's pictures are not loaded (the atlas, mapmaker_sprites.js) and, once they are, the average color of each picture for tiles smaller than 10 pixels (setAverages). The kinds follow the tile
// ids of docs/MAP_FORMAT.md and the names of Table 4 of ants.chd: layer 1 ids 431 to 669 are ground, and the first letter of a tile's name is its kind: g Gravel, d Dirt, s Slate, m and M Mud,
// w Water (mapmaker_ground.js lists every tile); layer 2 holds the objects. A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapTiles = factory();
})(this, function () {
    'use strict';

    var EMPTY2 = 0x7FFE;
    var GROUND = {                 // the five kinds of ground: a few shades each (the average colors of the plain tiles), so that a field is not one flat color
        gravel: ['#8d7871', '#85706a', '#957f78'], dirt: ['#d6cac1', '#cfc2b8', '#ddd1c9'], slate: ['#9493aa', '#8c8ba2', '#9c9bb2'],
        mud: ['#4b4b4b', '#444444', '#525252'], water: ['#1d3d71', '#1a386a', '#214378']
    };
    // layer 1: [first id, last id, kind] (the kind is the first letter of the game's name: mapmaker_ground.js)
    var GROUND_IDS = [
        [431, 475, 'mud'], [476, 484, 'water'], [485, 486, 'gravel'], [487, 490, 'dirt'], [491, 522, 'gravel'], [523, 570, 'dirt'], [571, 579, 'slate'], [580, 595, 'gravel'], [596, 611, 'mud'], [612, 659, 'slate'], [660, 669, 'mud']
    ];
    // layer 2: [first id, last id, color]
    var OBJECT_IDS = [
        [34, 38, '#a67c52'], [62, 66, '#d65db1'], [129, 133, '#161616'], [134, 134, '#4a4a4a'], [195, 198, '#e8742a'], [205, 208, '#a67c52'], [209, 212, '#161616'],
        [245, 245, '#262626'], [246, 246, '#4a5fc8'], [247, 247, '#c0392b'], [248, 248, '#2f9a52'], [249, 249, '#262626'], [250, 250, '#4a5fc8'], [251, 251, '#2f9a52'], [252, 252, '#c0392b'],
        [253, 286, '#f2c230'], [287, 310, '#7d7d7d'], [311, 322, '#f2c230'], [323, 330, '#2f6b2a'], [340, 426, '#f2c230'], [404, 421, '#2f6b2a'], [1320, 1329, '#2f6b2a']
    ];
    var OTHER_OBJECT = '#8a7a5a';           // any other object of layer 2
    var TEAMS = { black: '#262626', blue: '#4a5fc8', green: '#2f9a52', red: '#c0392b' };
    var FOOD = '#f2c230';

    // The original game's minimap colors (include/ants_app/minimap_tables.hpp, src/ants_app/minimap_tables.inc, from the CHD palette at offset 28)
    var MINI_GROUND = {
        gravel: ['#8f776f', '#bb7b7f', '#9d6a4d', '#8f776f', '#8f776f'],
        slate: ['#8f8fab', '#8f8fab', '#8f8fab', '#8f8fab', '#8f8fab'],
        water: ['#3b3bbf', '#3b3bbf', '#3b3bbf', '#3b3bbf', '#3b3bbf'],
        mud: ['#5b574b', '#5b574b', '#5b574b', '#5b574b', '#5b574b'],
        dirt: ['#b3a79b', '#9f9787', '#b7b3cf', '#b3a79b', '#b3a79b'],
        other: ['#77777f', '#77777f', '#77777f', '#77777f', '#77777f']
    };
    var MINI_OBJECTS = [
        [34, 37, '#5b574b'], [62, 66, '#e7930b'], [134, 134, '#ffff61'], [152, 152, '#000000'], [153, 153, '#37132b'], [154, 154, '#3b976f'], [155, 155, '#e4c336'],
        [236, 244, '#235b33'], [245, 245, '#132327'], [246, 246, '#276bbf'], [247, 247, '#dd1184'], [248, 248, '#55c01e'], [253, 286, '#e7930b'], [287, 339, '#235b33'],
        [340, 343, '#e7930b'], [344, 355, '#235b33'], [356, 376, '#e7930b'], [377, 377, '#235b33'], [378, 388, '#e7930b'], [389, 389, '#235b33'], [390, 403, '#e7930b'],
        [404, 421, '#235b33'], [427, 430, '#235b33'], [1320, 1330, '#235b33'], [1331, 1333, '#e7930b'], [1334, 1334, '#235b33']
    ];
    var MINI_ANTS = { green: '#55c01e', red: '#dd1184', blue: '#276bbf', black: '#132327' };
    var MINI_PLANT = '#235b33';
    var MINI_FOOD = '#e7930b';

    function hexToPacked(hex) {          // 0xAABBGGRR, what a little-endian Uint32Array view of an ImageData holds
        var n = parseInt(hex.slice(1), 16);
        return (0xFF000000 | ((n & 0xFF) << 16) | (n & 0xFF00) | (n >> 16)) >>> 0;
    }

    var css1 = new Array(65536), css2 = new Array(65536), packed1 = null, packed2 = null, mini1 = null, mini2 = null;

    function build() {
        var i, j, k;
        for (i = 0; i < 65536; i++) { css1[i] = GROUND.gravel[1]; css2[i] = null; }
        for (i = 0; i < GROUND_IDS.length; i++) {
            var row = GROUND_IDS[i], shades = GROUND[row[2]];
            for (j = row[0]; j <= row[1]; j++) css1[j] = shades[j % 3];
        }
        for (i = 0; i < OBJECT_IDS.length; i++) for (j = OBJECT_IDS[i][0]; j <= OBJECT_IDS[i][1]; j++) css2[j] = OBJECT_IDS[i][2];
        for (k = 0; k < 65536; k++) if (css2[k] === null && k !== EMPTY2) css2[k] = OTHER_OBJECT;
    }
    build();

    // the kind of a ground tile ('gravel', 'dirt', 'slate', 'mud', 'water'), or null for an id that is not ground
    function kindOf(id) { for (var i = 0; i < GROUND_IDS.length; i++) if (id >= GROUND_IDS[i][0] && id <= GROUND_IDS[i][1]) return GROUND_IDS[i][2]; return null; }

    // The picture's own average colors (the atlas): `ground` is 6 hex digits for each id from 431, `objects` is {id: 0xRRGGBB}; the tables and the packed copies take them
    function setAverages(ground, objects) {
        var i, hex = function (n) { return '#' + ('00000' + n.toString(16)).slice(-6); };
        for (i = 0; i * 6 < ground.length; i++) css1[GROUND_IDS[0][0] + i] = '#' + ground.substr(i * 6, 6);
        for (var id in objects) if (objects.hasOwnProperty(id)) css2[+id] = hex(objects[id]);
        packed1 = packed2 = null;
    }

    // the color of a cell (the object when there is one, else the ground) as a CSS color
    function css(t1, t2) { return t2 !== EMPTY2 && css2[t2] ? css2[t2] : css1[t1 & 0xFFFF]; }

    // the same as packed colors for the pixel loop (a Uint32 for every tile id of each layer, made on first use)
    function packedTables() {
        if (!packed1) {
            packed1 = new Uint32Array(65536); packed2 = new Uint32Array(65536);
            for (var i = 0; i < 65536; i++) { packed1[i] = hexToPacked(css1[i]); packed2[i] = css2[i] && i !== EMPTY2 ? hexToPacked(css2[i]) : 0; }
        }
        return { ground: packed1, objects: packed2 };
    }

    // Packed color tables for the original game's minimap: 5 speckle entries per ground tile id, and one entry per layer-2 object id (0 for empty or bomb tiles 129..132 so terrain shows)
    function miniTables() {
        if (!mini1) {
            var i, j, s, defG = MINI_GROUND.other.map(hexToPacked), defO = hexToPacked('#77777f');
            mini1 = new Uint32Array(65536 * 5); mini2 = new Uint32Array(65536);
            for (i = 0; i < 65536; i++) {
                for (s = 0; s < 5; s++) mini1[i * 5 + s] = defG[s];
                mini2[i] = i === EMPTY2 || (i >= 129 && i <= 132) ? 0 : defO;
            }
            for (i = 0; i < GROUND_IDS.length; i++) {
                var row = GROUND_IDS[i], sp = MINI_GROUND[row[2]].map(hexToPacked);
                for (j = row[0]; j <= row[1]; j++) for (s = 0; s < 5; s++) mini1[j * 5 + s] = sp[s];
            }
            for (i = 0; i < MINI_OBJECTS.length; i++) {
                var o = MINI_OBJECTS[i], p = hexToPacked(o[2]);
                for (j = o[0]; j <= o[1]; j++) mini2[j] = p;
            }
        }
        return { ground: mini1, objects: mini2 };
    }

    return { css: css, kindOf: kindOf, setAverages: setAverages, packedTables: packedTables, miniTables: miniTables, hexToPacked: hexToPacked, TEAMS: TEAMS, FOOD: FOOD, MINI_ANTS: MINI_ANTS, MINI_PLANT: MINI_PLANT, MINI_FOOD: MINI_FOOD, EMPTY2: EMPTY2 };
});
