// The colors that stand for the tiles on the map maker's canvas (docs/MAP_EDITOR.md): one color for a kind of ground and a few for the kinds of object. They are what the canvas shows while the
// game's pictures are not loaded (the atlas, mapmaker_sprites.js) and, once they are, the average color of each picture for tiles smaller than 10 pixels (setAverages). The kinds follow the tile
// ids of docs/MAP_FORMAT.md and the names of Table 4 of ants.chd: layer 1 ids 431 to 669 are ground, and the first letter of a tile's name is its kind: g Gravel, d Dirt, s Slate, m and M Mud,
// w Water (mapmaker_ground.js lists every tile); layer 2 holds the objects. It also holds the game's own way from a tile of a map to its picture (the animation: tableOf and the rules below,
// docs/MAP_EDITOR.md, "Sprites"): the map's dictionary decides, as in the game's renderer. A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapTiles = factory();
})(this, function () {
    'use strict';

    var EMPTY2 = 0x7FFE, BACK = '#0b1612';
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
        packed1 = packed2 = null; made = null; stamps++;
    }

    // ---- from a tile of a map to its picture: the game's own rule (src/ants_app/renderer.cpp, Renderer::set_level and its three users) ----
    // A cell holds a number, and the map's dictionary has a name at that place (11 bytes a name). The game draws the animation that the NAME gives: when the entry is the name of the animation of
    // that same number (compared without regard to case) the animation is the number; an entry of "." or no letter draws nothing; any other name is looked up (the exact name, the last of two
    // equal names first, then the name in lower case, the first of them): the number that the name has in the animation table, or nothing when no animation has it. The start markers (152 to 155) are never
    // drawn. So a map whose dictionary is shifted (a pile of jacks that the table calls a pink eraser) shows what its names say, not what its numbers say. `animations` is the table's names by number
    // (the atlas's `names`); without it a tile is its number, as the pictures were drawn before the table came.
    var animNames = null, animExact = null, animLower = null, made = null, stamps = 0;

    function lowerAscii(s) { return s.replace(/[A-Z]/g, function (ch) { return String.fromCharCode(ch.charCodeAt(0) + 32); }); }

    function setAnimations(names) {
        made = null; stamps++;
        if (!names || !names.length) { animNames = animExact = animLower = null; return; }
        animNames = names; animExact = Object.create(null); animLower = Object.create(null);
        for (var i = 0; i < names.length; i++) {
            animExact[names[i]] = i;                                         // (the last of two equal names wins, as in the game's table)
            var low = lowerAscii(names[i]);
            if (!(low in animLower)) animLower[low] = i;                     // (the first, here)
        }
    }

    function findAnimation(name) {
        if (name in animExact) return animExact[name];
        var low = lowerAscii(name);
        return low in animLower ? animLower[low] : -1;
    }

    // The table of one map's dictionary: {n: how many names, anim[i]: the animation of entry i or -1, blank[i]: the entry is "." or empty, pu[i]: the name begins with "pu", stamp: changes when
    // the dictionary does}, or null while there is no animation table. One table is kept (the map that is open); the dictionary's bytes are compared each time, so a name written in place changes it.
    function tableOf(file) {
        if (!animNames || !file || !file.bytes || !(file.names > 0)) return null;
        var b = file.bytes, n = file.names, at = file.dictAt + 2, i, k;
        if (at + 11 * n > b.length) return null;
        if (made && made.n === n) {
            for (k = 0; k < 11 * n && made.raw[k] === b[at + k]; k++);
            if (k === 11 * n) return made;
        }
        var raw = new Uint8Array(11 * n), anim = new Int16Array(n), blank = new Uint8Array(n), pu = new Uint8Array(n);
        for (k = 0; k < 11 * n; k++) raw[k] = b[at + k];
        for (i = 0; i < n; i++) {
            var name = '';
            for (k = 0; k < 11 && raw[11 * i + k]; k++) name += String.fromCharCode(raw[11 * i + k]);
            blank[i] = name === '' || name === '.' ? 1 : 0;
            pu[i] = lowerAscii(name).indexOf('pu') === 0 ? 1 : 0;
            if (blank[i] || (i >= 152 && i <= 155)) anim[i] = -1;
            else if (i < animNames.length && lowerAscii(animNames[i]) === lowerAscii(name)) anim[i] = i;
            else anim[i] = findAnimation(name);
        }
        made = { n: n, raw: raw, anim: anim, blank: blank, pu: pu, stamp: ++stamps };
        return made;
    }

    // What the game draws from the id itself, whatever the dictionary says: a power-up (62 to 66), an original bomb (129 to 132), the fire wall (134), a hill (245 to 248: its own template) and a bridge (34 to 38)
    function byId(id) { return (id >= 62 && id <= 66) || (id >= 129 && id <= 132) || id === 134 || (id >= 245 && id <= 248) || (id >= 34 && id <= 38); }

    // The animation drawn for the number in a cell of layer 1 (-1: nothing; the ground behind is black). `tab` is tableOf(file), null for the number itself.
    function ground(tab, id) { return tab ? (id < tab.n ? tab.anim[id] : -1) : id; }

    // ... for the number of an anchored object of layer 2: the ones above are the number; an entry outside the dictionary, ".", a name that begins with "pu" (and a start marker: the table has none for it) draw nothing
    function object(tab, id) {
        if (!tab || byId(id)) return id;
        if (id >= tab.n || tab.blank[id] || tab.pu[id]) return -1;
        return tab.anim[id];
    }

    // ... for the tile of a food pile's stage: the name's animation, or the number itself when the name gives none (Renderer::food_stage_anim)
    function stage(tab, id) {
        if (!tab) return id;
        var a = id < tab.n ? tab.anim[id] : -1;
        return a >= 0 ? a : (id < animNames.length ? id : -1);
    }

    // ... for a tile that is about to be put (a brush's picture): an entry that is "." or missing is named by the tool when the tile goes down, so its picture is the number's; any other entry stays
    function place(tab, id) { return !tab || id >= tab.n || tab.blank[id] ? id : object(tab, id); }

    // the colors of an animation: its ground color when it is a ground tile, else its object color
    function groundCss(a) { return a >= 431 && a <= 669 ? css1[a] : (css2[a] || OTHER_OBJECT); }

    // the color of a cell (the object when there is one, else the ground) as a CSS color; with a map's table the colors are those of the animations that its dictionary names
    function css(t1, t2, tab) {
        if (!tab) return t2 !== EMPTY2 && css2[t2] ? css2[t2] : css1[t1 & 0xFFFF];
        var a = t2 !== EMPTY2 ? object(tab, t2) : -1;
        if (a >= 0 && css2[a]) return css2[a];
        a = ground(tab, t1 & 0xFFFF);
        return a < 0 ? BACK : groundCss(a);
    }

    // the same as packed colors for the pixel loop (a Uint32 for every tile id of each layer, made on first use; one pair more for the map whose table it is)
    function packedTables(tab) {
        if (!packed1) {
            packed1 = new Uint32Array(65536); packed2 = new Uint32Array(65536);
            for (var i = 0; i < 65536; i++) { packed1[i] = hexToPacked(css1[i]); packed2[i] = css2[i] && i !== EMPTY2 ? hexToPacked(css2[i]) : 0; }
        }
        if (!tab) return { ground: packed1, objects: packed2 };
        if (!tab.packed) {
            var g = new Uint32Array(65536), o = new Uint32Array(65536), back = hexToPacked(BACK), j;
            for (j = 0; j < 65536; j++) {
                var a1 = ground(tab, j), a2 = j !== EMPTY2 ? object(tab, j) : -1;
                g[j] = a1 < 0 ? back : hexToPacked(groundCss(a1));
                o[j] = a2 >= 0 && css2[a2] ? hexToPacked(css2[a2]) : 0;
            }
            tab.packed = { ground: g, objects: o };
        }
        return tab.packed;
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

    return { css: css, kindOf: kindOf, setAverages: setAverages, setAnimations: setAnimations, tableOf: tableOf, ground: ground, object: object, stage: stage, place: place, byId: byId, packedTables: packedTables, miniTables: miniTables, hexToPacked: hexToPacked, TEAMS: TEAMS, FOOD: FOOD, MINI_ANTS: MINI_ANTS, MINI_PLANT: MINI_PLANT, MINI_FOOD: MINI_FOOD, EMPTY2: EMPTY2, BACK: BACK };
});
