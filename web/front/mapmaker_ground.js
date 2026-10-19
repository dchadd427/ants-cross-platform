// The kinds of ground (docs/MAP_EDITOR.md, "Ground"): the 239 ground tiles of layer 1 (ids 431 to 669) sorted by the game's own names, and the sheet that lists the tiles of one kind. There are
// five kinds: Gravel (g), Dirt (d), Slate (s), Mud (m or M) and Water (w). A name is <own kind><other kind><shape><variant>: gd03a is a gravel cell that blends toward dirt, with the dirt on
// its north side (shapes 02 to 09: NW corner, N, NE corner, W, E, SW corner, S, SE corner); 01 is the plain tile (g01a); a letter after the number is a variant of the same picture. The names
// are those of src/ants_assets/lvl_tile_names.inc (tests/scripts/web_mapmaker_ground_check.js holds this table to that list and to the library's maps). A lazily loaded, plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapGround = factory();
})(this, function () {
    'use strict';

    var FIRST = 431;
    var NAMES = ('MW02a MW03a MW04a MW05a MW06a MW07a MW08a MW09a M01a M01b M01c M01d m01e md02a md02b md03a md03b md04a md04b md05a md05b md06a md06b md07a md07b md08a md08b md09a md09b ' +
        'mg02a mg02b mg03a mg03b mg04a mg04b mg05a mg05b mg06a mg06b mg07a mg07b mg08a mg08b mg09a mg09b w01a wm02a wm03a wm04a wm05a wm06a wm07a wm08a wm09a g01a g01b d01a d01b d01c d01d ' +
        'gd09a gd09b gd08a gd08b gd07a gd07b gd06a gd06b gd05a gd05b gd04a gd04b gd03a gd03b gd02a gd02b gm02a gm02b gm03a gm03b gm04a gm04b gm05a gm05b gm06a gm06b gm07a gm07b gm08a gm08b gm09a gm09b ' +
        'ds02a ds02b ds03a ds03b ds04a ds04b ds05a ds05b ds06a ds06b ds07a ds07b ds08a ds08b ds09a ds09b dg02a dg02b dg03a dg03b dg04a dg04b dg05a dg05b dg06a dg06b dg07a dg07b dg08a dg08b dg09a dg09b ' +
        'dm02a dm02b dm03a dm03b dm04a dm04b dm05a dm05b dm06a dm06b dm07a dm07b dm08a dm08b dm09a dm09b s01a s01b s01c s01d s01e s01f s01g s01h s01i gs02a gs02b gs03a gs03b gs04a gs04b gs05a gs05b ' +
        'gs06a gs06b gs07a gs07b gs08a gs08b gs09a gs09b ms02a ms02b ms03a ms03b ms04a ms04b ms05a ms05b ms06a ms06b ms07a ms07b ms08a ms08b ms09a ms09b sm02a sm02b sm03a sm03b sm04a sm04b sm05a sm05b ' +
        'sm06a sm06b sm07a sm07b sm08a sm08b sm09a sm09b sd02a sd02b sd03a sd03b sd04a sd04b sd05a sd05b sd06a sd06b sd07a sd07b sd08a sd08b sd09a sd09b sg02a sg02b sg03a sg03b sg04a sg04b sg05a sg05b ' +
        'sg06a sg06b sg07a sg07b sg08a sg08b sg09a sg09b m01d_a m01d_b m01d_c m01d_d m01d_e m01e_a m01e_b m01e_c m01e_d m01e_e').split(' ');
    // the five kinds in the order of the palette: the key, the letter of the game's names, the name on the screen, the plain tile that its chip paints
    var KINDS = [
        { key: 'gravel', letter: 'g', label: 'Gravel', plain: 485 }, { key: 'dirt', letter: 'd', label: 'Dirt', plain: 487 }, { key: 'slate', letter: 's', label: 'Slate', plain: 571 },
        { key: 'mud', letter: 'm', label: 'Mud', plain: 439 }, { key: 'water', letter: 'w', label: 'Water', plain: 476 }
    ];
    var SHAPES = [[2, 'north-west corner'], [3, 'north edge'], [4, 'north-east corner'], [5, 'west edge'], [6, 'east edge'], [7, 'south-west corner'], [8, 'south edge'], [9, 'south-east corner']];
    var UNUSED = { 527: 1, 533: 1, 537: 1 };            // dirt next to slate at three corners: in the game's list, in no map of the library

    function kindOfLetter(ch) { ch = ch.toLowerCase(); for (var i = 0; i < KINDS.length; i++) if (KINDS[i].letter === ch) return KINDS[i].key; return null; }
    function kind(key) { for (var i = 0; i < KINDS.length; i++) if (KINDS[i].key === key) return KINDS[i]; return null; }

    var INFO = {};
    NAMES.forEach(function (name, i) {
        var m = /^([gdsmMw])([gdsmMwW])(\d\d)([a-z])$/.exec(name), id = FIRST + i;
        INFO[id] = m ? { id: id, name: name, kind: kindOfLetter(m[1]), other: kindOfLetter(m[2]), shape: +m[3], variant: m[4] } : { id: id, name: name, kind: kindOfLetter(name.charAt(0)), other: null, shape: 1, variant: name.slice(3) };
    });

    function info(id) { return INFO[id] || null; }
    function kindOf(id) { return INFO[id] ? INFO[id].kind : null; }
    function shapeName(shape) { for (var i = 0; i < SHAPES.length; i++) if (SHAPES[i][0] === shape) return SHAPES[i][1]; return ''; }
    // the words for a tile: "Gravel", or "Gravel next to dirt: north edge"
    function label(id) {
        var t = INFO[id];
        if (!t) return '';
        return kind(t.kind).label + (t.other ? ' next to ' + kind(t.other).label.toLowerCase() + ': ' + shapeName(t.shape) : '');
    }
    // the plain tile of the kind first, then the ones that look the same (the game's other plain tiles of the kind, mostly in no map)
    function plainIds(key) {
        var out = [kind(key).plain];
        NAMES.forEach(function (n, i) { var t = INFO[FIRST + i]; if (t.kind === key && !t.other && t.id !== out[0]) out.push(t.id); });
        return out;
    }
    // the kinds that `key` has edge tiles toward, in the order of the palette
    var OTHERS = {};
    function others(key) {
        return OTHERS[key] || (OTHERS[key] = KINDS.filter(function (k) { return NAMES.some(function (n, i) { var t = INFO[FIRST + i]; return t.kind === key && t.other === k.key; }); }).map(function (k) { return k.key; }));
    }
    // the tile ids of `key` next to `other`: [{shape, id (the "a" tile), variants: [other ids of the same picture]}] for the shapes 2 to 9
    function edges(key, other) {
        return SHAPES.map(function (s) {
            var out = { shape: s[0], id: 0, variants: [] };
            NAMES.forEach(function (n, i) {
                var t = INFO[FIRST + i];
                if (t.kind !== key || t.other !== other || t.shape !== s[0]) return;
                if (t.variant === 'a') out.id = t.id; else out.variants.push(t.id);
            });
            return out;
        });
    }
    // The walking class of each ground tile (ids 431 to 669), one digit each: the engine's table kTileTerrain (src/ants_sim/movement_tables_data.inc; a test holds this string to it). Fix edges never
    // changes a cell to a tile of another class, so what it does cannot change where ants walk.
    var TERRAIN = '33333333333333030303030303030303030303030303022222222200400000000000000000000000000000000000404040404040404040404040404040404040404040404040111000000000000000000000030303030303030301010101010101010101010101010101010101010101010103333333333';
    function terrain(id) { return id >= FIRST && id < FIRST + TERRAIN.length ? +TERRAIN.charAt(id - FIRST) : -1; }
    // the game's dictionary name of any ground tile
    function name(id) { return INFO[id] ? INFO[id].name : ''; }

    // ---- the sheet: every tile of one kind (S5 of the pictures) ----
    // env: {doc, kind (the key), chosen() -> the tile id painted now, choose(id), pic(node, id, px) puts the tile's picture in `node` (a flat color when there is no atlas), brush() -> 1/3/5/9, setBrush(n)}
    // Fills `body` (a dialog's body); returns {refresh()} that puts the gold ring on the chosen tile again.
    function sheet(env, body) {
        var doc = env.doc, k = kind(env.kind), chips = [], brushes = [];
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function chip(id, cls, dim) {
            var b = el('button', 'gt' + (cls ? ' ' + cls : '')), i = el('i'), t = INFO[id];
            b.type = 'button'; b.setAttribute('role', 'radio'); b.setAttribute('data-tile', String(id));
            b.setAttribute('aria-label', t.name + ': ' + label(id) + (UNUSED[id] ? ' (no map uses it)' : '')); b.title = t.name + (UNUSED[id] ? ' (no map uses it)' : '');
            env.pic(i, id, 52); b.appendChild(i);
            if (dim) b.className += ' dim';
            b.addEventListener('click', function () { env.choose(id); refresh(); });
            chips.push(b);
            return b;
        }
        function same(ids, text) {          // the tiles that look the same, in one line (small buttons), not as chips
            var line = el('p', 'kmore');
            line.appendChild(el('span', '', text));
            ids.forEach(function (id) {
                var b = el('button', 'gl', INFO[id].name);
                b.type = 'button'; b.setAttribute('role', 'radio'); b.setAttribute('data-tile', String(id)); b.setAttribute('aria-label', INFO[id].name + ': a tile that looks the same');
                b.addEventListener('click', function () { env.choose(id); refresh(); });
                chips.push(b); line.appendChild(b);
            });
            return line;
        }
        function refresh() {
            var on = String(env.chosen());
            chips.forEach(function (b) { b.setAttribute('aria-checked', String(b.getAttribute('data-tile') === on)); });
            brushes.forEach(function (b) { b.setAttribute('aria-checked', String(+b.getAttribute('data-brush') === env.brush())); });
        }
        var low = k.label.toLowerCase(), plain = plainIds(k.key);
        body.appendChild(el('p', 'lead', 'Pick any single tile to paint with. The picture shows what the tile looks like; the pictures around the plain tile are the edge and corner pieces that blend ' + low + ' into the other kinds.'));
        var sheetEl = el('div', 'ksheet'), top = el('div', 'kgroup');
        sheetEl.setAttribute('role', 'radiogroup'); sheetEl.setAttribute('aria-label', k.label + ' tiles');
        top.appendChild(el('h3', '', 'Plain ' + low));
        var row = el('div', 'krow'), main = chip(plain[0]);
        main.appendChild(el('span', '', INFO[plain[0]].name)); row.appendChild(main); top.appendChild(row);
        if (plain.length > 1) top.appendChild(same(plain.slice(1), plain.length - 1 + (plain.length === 2 ? ' more looks' : ' more look') + ' the same:'));
        sheetEl.appendChild(top);
        var groups = el('div', 'kgroups');
        others(k.key).forEach(function (o) {
            var g = el('div', 'kgroup'), grid = el('div', 'compass'), list = edges(k.key, o), at = 0, vs = [];
            g.appendChild(el('h3', '', k.label + ' next to ' + kind(o).label));
            for (var cell = 0; cell < 9; cell++) {
                if (cell === 4) { var mid = el('span', 'gt mid'), mi = el('i'); env.pic(mi, plain[0], 52); mid.appendChild(mi); mid.setAttribute('aria-hidden', 'true'); grid.appendChild(mid); continue; }
                var e = list[at++];
                grid.appendChild(chip(e.id, '', UNUSED[e.id]));
                vs = vs.concat(e.variants);
            }
            g.appendChild(grid);
            g.appendChild(el('p', 'kcap', kind(o).label + ' lies on the side the picture shows'));
            if (vs.length) g.appendChild(same(vs, 'The same look:'));
            groups.appendChild(g);
        });
        sheetEl.appendChild(groups);
        body.appendChild(sheetEl);
        var brush = el('div', 'kbrush');
        brush.setAttribute('role', 'radiogroup'); brush.setAttribute('aria-label', 'Brush size in cells');
        brush.appendChild(el('span', '', 'Brush'));
        [1, 3, 5, 9].forEach(function (n) {
            var b = el('button', 'eb', String(n));
            b.type = 'button'; b.setAttribute('role', 'radio'); b.setAttribute('data-brush', String(n)); b.setAttribute('aria-label', 'Brush of ' + n + (n === 1 ? ' cell' : ' cells across'));
            b.addEventListener('click', function () { env.setBrush(n); refresh(); });
            brushes.push(b); brush.appendChild(b);
        });
        body.appendChild(brush);
        refresh();
        return { refresh: refresh };
    }

    return { FIRST: FIRST, LAST: FIRST + NAMES.length - 1, NAMES: NAMES, KINDS: KINDS, SHAPES: SHAPES, UNUSED: UNUSED, info: info, kind: kind, kindOf: kindOf, terrain: terrain, TERRAIN: TERRAIN, label: label, name: name, plainIds: plainIds, others: others, edges: edges, sheet: sheet };
});
