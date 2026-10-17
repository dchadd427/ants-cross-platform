// The colors that stand for the tiles on the map maker's canvas (docs/MAP_EDITOR.md): one color for a kind of ground and a few for the kinds of object, not the game's pictures (the
// pictures come with a later change). The kinds follow the tile ids of docs/MAP_FORMAT.md and the names of Table 4 of ants.chd: layer 1 ids 431 to 669 are ground (the first letter of a
// tile's name is its kind: m mud, w water, g grass, d dirt, s sand; the others of the name are the kinds it blends into), layer 2 holds the objects. A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapTiles = factory();
})(this, function () {
    'use strict';

    var EMPTY2 = 0x7FFE;
    var GROUND = {                 // the kinds of ground: a few shades each, so that a field is not one flat color
        grass: ['#6a9a44', '#5f8f3c', '#58873a'], dirt: ['#9a8460', '#917c58', '#a08c68'], sand: ['#cdbb8c', '#c6b384', '#d3c294'],
        mud: ['#7a5a3a', '#725232', '#82623f'], water: ['#2e6fa8', '#2b69a0', '#3276b0']
    };
    // layer 1: [first id, last id, kind]
    var GROUND_IDS = [
        [431, 475, 'mud'], [476, 484, 'water'], [485, 486, 'grass'], [487, 490, 'dirt'], [491, 522, 'grass'], [523, 570, 'dirt'], [571, 579, 'sand'], [580, 595, 'grass'],
        [596, 611, 'mud'], [612, 659, 'sand'], [660, 669, 'mud']
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

    function hexToPacked(hex) {          // 0xAABBGGRR, what a little-endian Uint32Array view of an ImageData holds
        var n = parseInt(hex.slice(1), 16);
        return (0xFF000000 | ((n & 0xFF) << 16) | (n & 0xFF00) | (n >> 16)) >>> 0;
    }

    var css1 = new Array(65536), css2 = new Array(65536), packed1 = null, packed2 = null;

    function build() {
        var i, j, k;
        for (i = 0; i < 65536; i++) { css1[i] = GROUND.grass[1]; css2[i] = null; }
        for (i = 0; i < GROUND_IDS.length; i++) {
            var row = GROUND_IDS[i], shades = GROUND[row[2]];
            for (j = row[0]; j <= row[1]; j++) css1[j] = shades[j % 3];
        }
        for (i = 0; i < OBJECT_IDS.length; i++) for (j = OBJECT_IDS[i][0]; j <= OBJECT_IDS[i][1]; j++) css2[j] = OBJECT_IDS[i][2];
        for (k = 0; k < 65536; k++) if (css2[k] === null && k !== EMPTY2) css2[k] = OTHER_OBJECT;
    }
    build();

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

    return { css: css, packedTables: packedTables, hexToPacked: hexToPacked, TEAMS: TEAMS, FOOD: FOOD, EMPTY2: EMPTY2 };
});
