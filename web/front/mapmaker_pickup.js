// The map maker's Pick up tool (docs/MAP_EDITOR.md, "Pick up"): a tap picks a thing up (a hill, a team's ant or its group, a food pile, a plant or any object), a bar says what it is and offers Move,
// Copy here, Delete and Select an area...; Move shows a see-through ghost where the thing will land, red with the reason in plain words when it cannot go there, and Done puts it there. Everything it
// does is ONE CHANGE of the open file's bytes (the cells of layer 2, the solid bits, and the lists after the layers: block 1 holds the ants and plants, block 2 the piles, block 4 the plants'
// waypoints), built from the helpers of the other tools (mapmaker_blocks.js) and given to the history as one step, so a move, a copy, a delete and the move of a whole area are one Undo each.
// The ring round what is picked and the ghost are drawn on a canvas of their own that lies over the map, so the map itself is never painted again for them. A lazily loaded script (the page
// loads it, with mapmaker_pickup.css, the first time the tool is chosen), so the editor's eager scripts stay small. The words come from fixed lists here and in the palette, never from the file.
// A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_edit.js'), require('./mapmaker_blocks.js'), require('./mapmaker_view.js'), require('./mapmaker_solid.js'), require('./mapmaker_tiles.js'), require('./mapmaker_palette.js'));
    else root.MapPickup = factory(root.MapEdit, root.MapBlocks, root.MapView, root.MapSolid, root.MapTiles, root.MapPalette);
})(this, function (Edit, Blocks, View, Solid, Tiles, Palette) {
    'use strict';

    var EMPTY2 = 0x7FFE;
    var MAX_LIST = 65535;                            // a list of the file (block 1, 2 or 4) holds a word's worth of records
    var GOLD = '#f2c230';
    // The plain word for a bigger object, by the game's own name of its tile (the names are the palette's, never the file's): the first pattern that fits
    var WORDS = [[/^grass/, 'grass'], [/rock/, 'rock'], [/^mush/, 'mushroom'], [/^leaf/, 'leaf'], [/^ivy/, 'ivy'], [/^plant/, 'plant'], [/^bobber/, 'fishing bobber'], [/^slinky/, 'slinky'], [/^bear/, 'teddy bear'],
        [/^glasses/, 'glasses'], [/^can/, 'can'], [/^sling/, 'slingshot'], [/^ducky/, 'rubber duck'], [/^pin/, 'pin'], [/cray/, 'crayon']];
    var TYPES = { 1: 'bomber ant', 2: 'fire ant', 3: 'thief ant', 4: 'combat ant', 5: 'swimmer ant' };      // (the per-ant types of an Extended map)

    function num(n) { return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }
    function plural(n, one, many) { return num(n) + ' ' + (n === 1 ? one : many); }
    function lower(s) { return s.charAt(0).toLowerCase() + s.slice(1); }
    function cap(s) { return s.charAt(0).toUpperCase() + s.slice(1); }
    function label(t) { return Blocks.TEAMS[t].label; }
    function rc(file, idx) { var r = (idx / file.columns) | 0; return { r: r, c: idx - r * file.columns }; }
    function boxOf(file, cells) {
        var b = { r0: 1e9, c0: 1e9, r1: -1, c1: -1 }, i;
        for (i = 0; i < cells.length; i++) { var p = rc(file, cells[i]); if (p.r < b.r0) b.r0 = p.r; if (p.r > b.r1) b.r1 = p.r; if (p.c < b.c0) b.c0 = p.c; if (p.c > b.c1) b.c1 = p.c; }
        return b;
    }

    // The plain word for the tile of an object (lower case): the palette's own label of a small object or a plant without its number, a word for a bigger one, else "object"
    function nameOf(tile) {
        var it = Palette.itemOf('objects', tile), big, i;
        if (it) return lower(it.label.replace(/\s+\d+[a-z]?$/, ''));
        big = Palette.itemOf('bigger', tile);
        if (big) for (i = 0; i < WORDS.length; i++) if (WORDS[i][0].test(big.name)) return WORDS[i][1];
        return tile >= 287 && tile <= 310 ? 'rock' : 'object';
    }

    // ---- what is on the map, by cell (made once for a reading of the lists: Blocks.layout keeps it) ----
    function index(file) {
        var lay = Blocks.layout(file), ix = lay.pk, i, cell;
        if (ix) return ix;
        ix = lay.pk = { lay: lay, ants: {}, plants: {}, piles: {}, wps: {} };
        function add(map, idx, v) { (map[idx] = map[idx] || []).push(v); }
        for (i = 0; i < lay.records.length; i++) {
            var rec = lay.records[i];
            if (rec.row >= file.rows || rec.column >= file.columns) continue;
            cell = rec.row * file.columns + rec.column;
            if (Blocks.teamOfMarker(rec.tile) >= 0) add(ix.ants, cell, i); else if (Blocks.PLANT_NAMES[rec.tile]) add(ix.plants, cell, i);
        }
        for (i = 0; i < lay.piles.length; i++) if (lay.piles[i].row < file.rows && lay.piles[i].column < file.columns) add(ix.piles, lay.piles[i].row * file.columns + lay.piles[i].column, i);
        for (i = 0; i < lay.waypoints.length; i++) if (lay.waypoints[i].row < file.rows && lay.waypoints[i].column < file.columns) add(ix.wps, lay.waypoints[i].row * file.columns + lay.waypoints[i].column, i);
        return ix;
    }

    // ---- the things that can be picked up: {k: 'ant' | 'plant' | 'pile' | 'obj', fp: the cells (indexes) it covers, ...} ----
    function antItem(file, ix, i) { var rec = ix.lay.records[i]; return { k: 'ant', i: i, tile: rec.tile, team: Blocks.teamOfMarker(rec.tile), fp: [rec.row * file.columns + rec.column] }; }
    function plantItem(file, ix, i) {
        var rec = ix.lay.records[i], idx = rec.row * file.columns + rec.column, w = ix.wps[idx];
        return { k: 'plant', i: i, w: w ? w[0] : -1, tile: rec.tile, fp: [idx] };
    }
    function objItem(file, idx) {
        var cells = Edit.objectCells(file, idx), tile = Edit.tile(file, 2, idx), team = Blocks.teamOfHill(tile), entrance = -1, i;
        for (i = 0; i < cells.length; i++) if (Edit.flags(file, 2, cells[i]) & 1) entrance = cells[i];
        return { k: 'obj', tile: tile, fp: cells, hill: team >= 0 && cells.length === 16 ? team : -1, entrance: entrance };
    }
    // The pile whose stage the layer-2 cell `idx` (tile t) shows: the pile on the cell's anchor (or on the cell) that has the tile among its stages, or -1
    function pileOf(file, ix, idx, t) {
        var a = Edit.anchorIndex(file, idx), list = (a >= 0 && ix.piles[a]) || ix.piles[idx], i, k;
        for (i = 0; list && i < list.length; i++) for (k = 0; k < ix.lay.piles[list[i]].stages.length; k++) if (ix.lay.piles[list[i]].stages[k][1] === t) return list[i];
        return -1;
    }
    function pileItem(file, ix, p) {
        var pile = ix.lay.piles[p], idx = pile.row * file.columns + pile.column, t = Edit.tile(file, 2, idx), objs = [], fp = [idx], i;
        if (t !== EMPTY2 && pileOf(file, ix, idx, t) === p) objs = Edit.objectCells(file, idx);
        for (i = 0; i < objs.length; i++) if (objs[i] !== idx) fp.push(objs[i]);
        return { k: 'pile', p: p, objs: objs, fp: fp, tile: pile.stages[0][1], ot: t };
    }

    // What stands on a cell, the thing on top first: starting ants (the last of a stack first), a plant, a food pile, then the object or hill. A tap again on the cell takes the next.
    function itemsAt(file, row, column) {
        var out = [], ix, idx, i, t, list, p, seen = {};
        if (!file || row < 0 || column < 0 || row >= file.rows || column >= file.columns) return out;
        ix = index(file); idx = row * file.columns + column;
        list = ix.ants[idx] || []; for (i = list.length - 1; i >= 0; i--) out.push(antItem(file, ix, list[i]));
        list = ix.plants[idx] || []; for (i = list.length - 1; i >= 0; i--) out.push(plantItem(file, ix, list[i]));
        t = Edit.tile(file, 2, idx);
        p = t !== EMPTY2 ? pileOf(file, ix, idx, t) : -1;
        list = (ix.piles[idx] || []).slice(); if (p >= 0 && list.indexOf(p) < 0) list.push(p);
        for (i = 0; i < list.length; i++) if (!seen[list[i]]) { seen[list[i]] = 1; out.push(pileItem(file, ix, list[i])); }
        if (t !== EMPTY2 && p < 0) out.push(objItem(file, idx));
        return out;
    }

    // The ants of the picked ant's team that touch each other (the eight cells round an ant, a stack of ants on a cell too), as items; the picked one is first
    function antGroup(file, item) {
        var ix = index(file), lay = ix.lay, team = item.team, seen = {}, out = [item], queue = [item.fp[0]], taken = {}, q = 0, cols = file.columns;
        taken[item.i] = true; seen[item.fp[0]] = true;
        while (q < queue.length) {
            var p = rc(file, queue[q++]), dr, dc;
            for (dr = -1; dr <= 1; dr++) for (dc = -1; dc <= 1; dc++) {
                var r = p.r + dr, c = p.c + dc, list, i;
                if (r < 0 || c < 0 || r >= file.rows || c >= cols) continue;
                list = ix.ants[r * cols + c] || [];
                for (i = 0; i < list.length; i++) {
                    if (taken[list[i]] || Blocks.teamOfMarker(lay.records[list[i]].tile) !== team) continue;
                    taken[list[i]] = true; out.push(antItem(file, ix, list[i]));
                    if (!seen[r * cols + c]) { seen[r * cols + c] = true; queue.push(r * cols + c); }
                }
            }
        }
        return out;
    }

    // Everything that is whole inside the box {r0, c0, r1, c1} (cells, both ends in): ants, plants, piles, hills and objects (an object only when every cell of it is inside the box)
    function areaItems(file, box) {
        var ix = index(file), lay = ix.lay, out = [], cols = file.columns, done = {}, i, r, c;
        function inside(idx) { var p = rc(file, idx); return p.r >= box.r0 && p.r <= box.r1 && p.c >= box.c0 && p.c <= box.c1; }
        for (i = 0; i < lay.records.length; i++) {
            var rec = lay.records[i];
            if (rec.row < box.r0 || rec.row > box.r1 || rec.column < box.c0 || rec.column > box.c1 || rec.row >= file.rows || rec.column >= cols) continue;
            if (Blocks.teamOfMarker(rec.tile) >= 0) out.push(antItem(file, ix, i)); else if (Blocks.PLANT_NAMES[rec.tile]) out.push(plantItem(file, ix, i));
        }
        for (i = 0; i < lay.piles.length; i++) {
            if (lay.piles[i].row >= file.rows || lay.piles[i].column >= cols) continue;
            var pit = pileItem(file, ix, i);
            pit.fp.forEach(function (x) { done[x] = true; });
            if (pit.fp.every(inside)) out.push(pit);
        }
        for (r = box.r0; r <= box.r1; r++) for (c = box.c0; c <= box.c1; c++) {
            var idx = r * cols + c, t = Edit.tile(file, 2, idx), a, o;
            if (t === EMPTY2 || done[idx]) continue;
            a = Edit.anchorIndex(file, idx);
            if (a >= 0 && a !== idx && Edit.tile(file, 2, a) === t && (Edit.flags(file, 2, a) & 1) && Edit.anchorIndex(file, a) === a && !inside(a)) continue;      // (its object is not whole inside the box)
            o = objItem(file, idx);
            o.fp.forEach(function (x) { done[x] = true; });
            if (pileOf(file, ix, idx, t) < 0 && o.fp.every(inside)) out.push(o);
        }
        return out;
    }

    // ---- words ----
    // [one, many] for what a thing is called when it is counted: "Red ant", "Red ants"; "grass"; "food pile", "food piles"
    function counted(x) {
        if (x.k === 'ant') return [label(x.team) + ' ant', label(x.team) + ' ants'];
        if (x.k === 'pile') return ['food pile', 'food piles'];
        if (x.k === 'plant') return ['plant', 'plants'];
        var n = x.hill >= 0 ? lower(label(x.hill)) + ' hill' : nameOf(x.tile);
        return [n, /s$/.test(n) ? n : n + 's'];
    }
    function oneName(file, it) {            // "red ant", "red combat ant" (Extended maps have an ant type for each ant), "food pile", "red hill", "rock"
        if (it.k === 'ant') { var type = file.version === 9 ? TYPES[Blocks.typeOfMarker(it.tile)] : ''; return lower(label(it.team)) + ' ' + (type ? type.replace(/ ant$/, '') + ' ant' : 'ant'); }
        if (it.k === 'plant') return nameOf(it.tile);
        if (it.k === 'pile') return 'food pile';
        return it.hill >= 0 ? lower(label(it.hill)) + ' hill' : nameOf(it.tile);
    }
    function nameOfPick(file, items, kind) {
        if (kind === 'area') return plural(items.length, 'item', 'items');
        return items.length > 1 ? plural(items.length, label(items[0].team) + ' ant', label(items[0].team) + ' ants') : 'the ' + oneName(file, items[0]);
    }
    // The words for a pick: {name (gold on the bar), rest}
    function describe(file, items, kind, group) {
        var it = items[0], lay = index(file).lay, p, foodItem, i;
        if (kind === 'area') {
            var counts = {}, order = [], parts;
            items.forEach(function (x) { var w = counted(x); if (!counts[w[0]]) { counts[w[0]] = { n: 0, w: w }; order.push(w[0]); } counts[w[0]].n++; });
            parts = order.slice(0, 4).map(function (key) { return counts[key].n + ' ' + counts[key].w[counts[key].n === 1 ? 0 : 1]; });
            return { name: 'Area: ' + plural(items.length, 'item', 'items'), rest: parts.length ? ' · ' + parts.join(', ') + (order.length > 4 ? ' and more' : '') : '' };
        }
        if (items.length > 1) return { name: plural(items.length, label(it.team) + ' ant', label(it.team) + ' ants'), rest: ' next to each other' };
        if (it.k === 'ant') {
            var total = Blocks.facts(file, false).ants[it.team], who = cap(oneName(file, it));
            return { name: who, rest: ' · one of ' + num(total) + (group > 1 ? ' · its group: ' + plural(group, label(it.team) + ' ant', label(it.team) + ' ants') + ' next to each other' : ' · no other ant of its team touches it') };
        }
        if (it.k === 'plant') {
            var w = it.w >= 0 ? lay.waypoints[it.w] : null;
            return { name: cap(nameOf(it.tile)), rest: ' · ' + (w && w.flag ? 'drops a power-up every ' + plural(w.interval, 'second', 'seconds') : 'ants walk under it') };
        }
        if (it.k === 'pile') {
            p = lay.piles[it.p]; foodItem = '';
            for (i = 0; i < Blocks.FOOD_ITEMS.length; i++) if (Blocks.FOOD_ITEMS[i][4].indexOf(p.stages[0][1]) >= 0) foodItem = Blocks.FOOD_ITEMS[i][1];
            return { name: 'Food pile', rest: (foodItem ? ' · ' + foodItem : '') + ' · ' + plural(p.units, 'bite', 'bites') + ' of ' + plural(p.points, 'point', 'points') };
        }
        if (it.hill >= 0) return { name: label(it.hill) + ' hill', rest: ' · 4 x 4 cells, with its door' };
        return { name: cap(nameOf(it.tile)), rest: ' · ' + plural(it.fp.length, 'cell', 'cells') };
    }

    // ---- can it go there ----
    // The cells of an item shifted by (dr, dc), or {edge} when one is off the map (a Classic map keeps the place of an object in one byte each of row and column)
    function shifted(file, it, dr, dc) {
        var out = [], i, hold = file.version === 8 && (it.k === 'obj' || it.k === 'pile');
        for (i = 0; i < it.fp.length; i++) {
            var p = rc(file, it.fp[i]), r = p.r + dr, c = p.c + dc;
            if (r < 0 || c < 0 || r >= file.rows || c >= file.columns) return { edge: 'map' };
            if (hold && (r > 255 || c > 255)) return { edge: 'limit' };
            out.push(r * file.columns + c);
        }
        return out;
    }
    // What moves along with the items (their cells and records may be landed on by the thing itself)
    function movingSets(items) {
        var mv = { cells: {}, ants: {}, plants: {}, piles: {} };
        items.forEach(function (it) {
            if (it.k === 'ant') mv.ants[it.i] = true;
            else if (it.k === 'plant') mv.plants[it.i] = true;
            else { if (it.k === 'pile') mv.piles[it.p] = true; (it.k === 'pile' ? it.objs : it.fp).forEach(function (x) { mv.cells[x] = true; }); }
        });
        return mv;
    }
    // What stops a thing of this kind ('hill', 'obj', 'pile', 'plant', 'ant') from standing on the cell `idx`: {why, ...} or null. What moves along does not count.
    function blockerAt(file, ix, kind, idx, mv) {
        var t2 = Edit.tile(file, 2, idx), list, i, h;
        if (t2 !== EMPTY2 && !mv.cells[idx]) {
            h = Blocks.teamOfHill(t2);
            if (h >= 0) return { why: 'hill', team: h };
            return pileOf(file, ix, idx, t2) >= 0 ? { why: 'pile' } : { why: 'obj', tile: t2 };
        }
        list = ix.piles[idx]; for (i = 0; list && i < list.length; i++) if (!mv.piles[list[i]]) return { why: 'pile' };
        list = ix.ants[idx]; for (i = 0; list && i < list.length; i++) if (!mv.ants[list[i]]) return { why: kind === 'ant' ? 'ant' : 'ants' };
        if (kind !== 'ant' && kind !== 'pile') { list = ix.plants[idx]; for (i = 0; list && i < list.length; i++) if (!mv.plants[list[i]]) return { why: 'plant' }; }
        if ((kind === 'hill' || kind === 'ant' || kind === 'pile') && Tiles.kindOf(Edit.tile(file, 1, idx)) === 'water') return { why: 'water' };
        if (kind === 'ant' && (Edit.flags(file, 1, idx) & 1) && !(mv.cells[idx] && Solid.sets(t2))) return { why: 'solid' };
        return null;
    }
    function kindOfItem(it) { return it.k === 'obj' ? (it.hill >= 0 ? 'hill' : 'obj') : it.k; }
    function blockerName(b) { return b.why === 'hill' ? 'the ' + lower(label(b.team)) + ' hill' : (b.why === 'obj' ? 'the ' + nameOf(b.tile) : (b.why === 'pile' ? 'the food pile' : 'the plant')); }

    // Whether the items can be shifted by (dr, dc) as a move or a copy: {ok: true} or {ok: false, text, cells (the cells that are in the way)}. The text is the sentence that the bar shows.
    function check(file, items, dr, dc, mode, kind) {
        var ix = index(file), lay = ix.lay, mv = movingSets(mode === 'copy' ? [] : items), i, k;
        var who = mode === 'copy' ? (items.length > 1 ? 'The copies' : 'The copy') : cap(nameOfPick(file, items, kind));
        if (kind === 'area' && mode !== 'copy') who = 'These ' + who;
        if (mode === 'copy') {
            if (items.some(function (x) { return x.k === 'obj' && x.hill >= 0; })) return { ok: false, text: 'A team has one hill, so a hill cannot be copied. Move it, or leave it out of the area.', cells: [] };
            if (lay.records.length + items.filter(function (x) { return x.k === 'ant' || x.k === 'plant'; }).length > MAX_LIST) return { ok: false, text: 'The file holds at most ' + num(MAX_LIST) + ' starting ants and plants together, and this would be more. Take some away first.', cells: [] };
            if (lay.piles.length + items.filter(function (x) { return x.k === 'pile'; }).length > MAX_LIST) return { ok: false, text: 'The file holds at most ' + num(MAX_LIST) + ' food piles.', cells: [] };
        }
        for (i = 0; i < items.length; i++) {
            var it = items[i], cells = shifted(file, it, dr, dc), kindIt = kindOfItem(it), first = null, bad = [], n = 0;
            if (cells.edge) return { ok: false, cells: [], text: who + ' cannot go here. ' + (cells.edge === 'limit' ? 'A Classic map keeps the place of an object in one number, so nothing can go past row 255 or column 255.' : (kindIt === 'hill' ? 'It needs a free 4 x 4 square inside the map.' : 'It would go off the edge of the map.')) + ' Move it a little.' };
            for (k = 0; k < cells.length; k++) {
                var b = blockerAt(file, ix, it.k === 'pile' && k > 0 ? 'obj' : kindIt, cells[k], mv);
                if (b && (!first || b.why === first.why)) { first = first || b; n++; bad.push(cells[k]); }
            }
            if (first) {
                var text, w = first.why, name = blockerName(first), named = w === 'hill' || w === 'obj' || w === 'pile' || w === 'plant';
                if (w === 'water') text = kindIt === 'hill' ? 'Part of its 4 x 4 square is water.' : 'That ground is water.';
                else if (w === 'solid') text = 'That ground is solid, so an ant cannot start there.';
                else if (w === 'ant') text = 'An ant is already there.';
                else if (w === 'ants') text = 'Starting ants are in the way.';
                else text = (kindIt === 'hill' ? 'It needs a free 4 x 4 square, and ' + name : cap(name)) + ' is in the way (' + plural(n, 'cell', 'cells') + ').';
                return { ok: false, cells: bad, text: who + ' cannot go here. ' + text + ' Move it a little' + (named ? ', or move ' + name + ' first.' : '.') };
            }
        }
        return { ok: true };
    }

    // ---- the change ----
    // The cells of an object after it is shifted, with their words: the third word names the anchor, the origin of the object (a version 8 file holds it as it is, a version 9 file as an offset); a
    // bomb or wall that is not anchored to an origin of its own keeps its word (the owner of a bomb, or 0x0101 for a neutral one)
    function shiftObject(file, cells, dr, dc) {
        var cols = file.columns, out = [], i, a, t;
        if (!cells.length) return out;
        a = Edit.anchorIndex(file, cells[0]); t = Edit.tile(file, 2, cells[0]);
        var whole = cells.length > 1 && a >= 0 && Edit.tile(file, 2, a) === t && (Edit.flags(file, 2, a) & 1) && Edit.anchorIndex(file, a) === a;
        for (i = 0; i < cells.length; i++) {
            var idx = cells[i], p = rc(file, idx), t2 = Edit.tile(file, 2, idx), anchor = whole ? a : idx, ap = rc(file, anchor), r = p.r + dr, c = p.c + dc;
            var word = Edit.third(file, 2, idx), keep = !whole && Edit.anchorIndex(file, idx) !== idx && t2 >= 129 && t2 <= 134;
            if (!keep) word = file.version === 8 ? (((ap.r + dr) & 255) | (((ap.c + dc) & 255) << 8)) : (((ap.r + dr - r) & 255) | (((ap.c + dc - c) & 255) << 8));
            out.push({ from: idx, to: r * cols + c, t: t2, f: Edit.flags(file, 2, idx), a: word });
        }
        return out;
    }

    // The change that moves (mode 'move'), copies ('copy') or takes away ('delete') the items, shifted by (dr, dc): {names, cells, solid, splices} for the history, and `landed` (what the items
    // become, for finding them again)
    function build(file, items, dr, dc, mode) {
        var ix = index(file), lay = ix.lay, cells = {}, order = [], solid = [], landed = [], i;
        var dropRec = {}, moveRec = {}, newRecs = [], dropPile = {}, movePile = {}, newPiles = [], dropWp = {}, moveWp = {}, newWps = [];
        function put(idx, t, f, a) { if (!(idx in cells)) order.push(idx); cells[idx] = [t, f, a]; }
        function objects(cellList) {
            var words = shiftObject(file, cellList, dr, dc), k;
            if (mode !== 'copy') for (k = 0; k < words.length; k++) put(words[k].from, EMPTY2, 0, 0);
            if (mode !== 'delete') for (k = 0; k < words.length; k++) { put(words[k].to, words[k].t, words[k].f, words[k].a); if (Solid.sets(words[k].t)) solid.push([words[k].to, true]); }
            return words;
        }
        items.forEach(function (it) {
            var p, rec, w;
            if (it.k === 'ant' || it.k === 'plant') {
                rec = lay.records[it.i]; w = it.k === 'plant' && it.w >= 0 ? lay.waypoints[it.w] : null;
                if (mode === 'delete') { dropRec[it.i] = true; if (w) dropWp[it.w] = true; }
                else if (mode === 'move') { moveRec[it.i] = [rec.row + dr, rec.column + dc]; if (w) moveWp[it.w] = [w.row + dr, w.column + dc]; }
                else { newRecs.push({ tile: rec.tile, row: rec.row + dr, column: rec.column + dc }); if (w) newWps.push({ keep: true, rel: w.rel, length: w.length, row: w.row + dr, column: w.column + dc }); }
                if (mode !== 'delete') landed.push({ k: it.k, tile: rec.tile, row: rec.row + dr, column: rec.column + dc });
            } else if (it.k === 'pile') {
                p = lay.piles[it.p];
                if (mode === 'delete') dropPile[it.p] = true; else if (mode === 'move') movePile[it.p] = [p.row + dr, p.column + dc]; else newPiles.push({ row: p.row + dr, column: p.column + dc, units: p.units, points: p.points, stages: p.stages });
                objects(it.objs);
                if (mode !== 'delete') landed.push({ k: 'pile', row: p.row + dr, column: p.column + dc });
            } else {
                var ws = objects(it.fp);
                if (mode !== 'delete' && ws.length) landed.push({ k: 'obj', idx: ws[0].to });
            }
        });
        var change = { names: [], cells: [], solid: solid, splices: [], landed: landed }, splices = [];
        order.forEach(function (idx) { change.cells.push([idx, cells[idx][0], cells[idx][1], cells[idx][2]]); });
        function any(o) { return Object.keys(o).length > 0; }
        if (lay.ok1 && (newRecs.length || any(dropRec) || any(moveRec))) {
            var recs = [];
            for (i = 0; i < lay.records.length; i++) if (!dropRec[i]) recs.push(moveRec[i] ? { tile: lay.records[i].tile, row: moveRec[i][0], column: moveRec[i][1] } : lay.records[i]);
            var s1 = Blocks.diffSplice(file, lay.b1.rel, lay.b1.length, Blocks.block1Bytes(recs.concat(newRecs)));
            if (s1) splices.push(s1);
        }
        if (lay.ok2 && (newPiles.length || any(dropPile) || any(movePile))) {
            var list = [];
            for (i = 0; i < lay.piles.length; i++) {
                var q = lay.piles[i];
                if (!dropPile[i]) list.push(movePile[i] ? { row: movePile[i][0], column: movePile[i][1], units: q.units, points: q.points, stages: q.stages } : { keep: true, rel: q.rel, length: q.length });
            }
            var s2 = Blocks.diffSplice(file, lay.b2.rel, lay.b2.length, Blocks.block2Bytes(file, list.concat(newPiles)));
            if (s2) splices.push(s2);
        }
        if (lay.ok4 && (newWps.length || any(dropWp) || any(moveWp))) {
            var wl = [];
            for (i = 0; i < lay.waypoints.length; i++) {
                var wq = lay.waypoints[i];
                if (!dropWp[i]) wl.push(moveWp[i] ? { keep: true, rel: wq.rel, length: wq.length, row: moveWp[i][0], column: moveWp[i][1] } : { keep: true, rel: wq.rel, length: wq.length });
            }
            var s4 = Blocks.diffSplice(file, lay.b4.rel, lay.b4.length, Blocks.block4Bytes(file, wl.concat(newWps)));
            if (s4) splices.push(s4);
        }
        splices.sort(function (a, b) { return b.rel - a.rel; });          // (the one that is later in the file is put on first, so it does not move the earlier one)
        change.splices = splices;
        return change;
    }

    // The items again after a change, from what build() said they became
    function resolve(file, landed) {
        var ix = index(file), out = [], used = {}, cols = file.columns;
        landed.forEach(function (d) {
            var list, i;
            if (d.k === 'ant' || d.k === 'plant') {
                list = (d.k === 'ant' ? ix.ants : ix.plants)[d.row * cols + d.column] || [];
                for (i = list.length - 1; i >= 0; i--) if (!used[list[i]] && ix.lay.records[list[i]].tile === d.tile) { used[list[i]] = true; out.push(d.k === 'ant' ? antItem(file, ix, list[i]) : plantItem(file, ix, list[i])); break; }
            } else if (d.k === 'pile') { list = ix.piles[d.row * cols + d.column] || []; if (list.length) out.push(pileItem(file, ix, list[list.length - 1])); }
            else if (Edit.tile(file, 2, d.idx) !== EMPTY2) out.push(objItem(file, d.idx));
        });
        return out;
    }

    // Is a picked thing still as it was (nothing has changed it since)
    function same(file, it) {
        var lay = index(file).lay, i;
        if (it.k === 'ant' || it.k === 'plant') { var rec = lay.records[it.i]; return !!rec && rec.tile === it.tile && rec.row * file.columns + rec.column === it.fp[0]; }
        if (it.k === 'pile') { var pile = lay.piles[it.p]; return !!pile && pile.row * file.columns + pile.column === it.fp[0] && Edit.tile(file, 2, it.fp[0]) === it.ot; }
        for (i = 0; i < it.fp.length; i++) if (it.fp[i] >= file.cells || Edit.tile(file, 2, it.fp[i]) !== it.tile) return false;
        return true;
    }

    // ---- the tool in the page ----
    // env: {doc, win, host: {map, cv, hint}, file() -> the open map's reader view, act(change) -> true when it was made, say(title, text), sprites() -> the sprite layer or null, view() -> {view, cursor, width, height},
    //       refresh(), focus(), busy()}
    function create(env) {
        var doc = env.doc, win = env.win, host = env.host;
        var st = { mode: 'idle', items: [], kind: 'one', group: [], copy: false, dr: 0, dc: 0, pad: false, box: null, drag: null, cycle: null, note: '', verdict: { ok: true }, target: [] };
        var shown = false, over = null, bar = null, wrap = host.cv ? host.cv.parentNode : null, loop = 0, last = '', mine = false;

        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function file() { return env.file(); }
        function how() { return st.copy ? 'copy' : 'move'; }

        // ---- the pick, the ghost and the verdict ----
        function bbox(f) { var cells = []; st.items.forEach(function (it) { for (var i = 0; i < it.fp.length; i++) cells.push(it.fp[i]); }); return cells.length ? boxOf(f, cells) : null; }
        function landing(f) { var b = bbox(f); return b ? { row: b.r0 + st.dr, column: b.c0 + st.dc } : null; }
        // the verdict for the ghost where it is, and the cells it covers (what a press on it drags)
        function evaluate() {
            var f = file(), i;
            st.verdict = st.items.length && f ? check(f, st.items, st.dr, st.dc, how(), st.kind) : { ok: true };
            if (st.mode === 'moving' && st.copy && st.dr === 0 && st.dc === 0) st.verdict = { ok: false, text: 'Tap where the copy should go.', cells: [], plain: true };
            st.target = [];
            for (i = 0; f && i < st.items.length; i++) { var g = shifted(f, st.items[i], st.dr, st.dc); st.target.push(g.edge ? [] : g); }
            return st.verdict;
        }
        function paint() { render(); drawOver(); watch(); }
        function clear(note) {
            st.mode = 'idle'; st.items = []; st.group = []; st.copy = false; st.dr = 0; st.dc = 0; st.pad = false; st.box = null; st.drag = null; st.cycle = null; st.note = note || ''; st.verdict = { ok: true }; st.target = [];
            paint();
        }
        function pick(items, kind, group, cycle, box) {
            st.items = items; st.kind = kind; st.group = group || []; st.cycle = cycle || null; st.mode = items.length ? 'picked' : 'idle'; st.copy = false; st.dr = 0; st.dc = 0; st.pad = false; st.note = ''; st.box = box || null; st.target = [];
            paint();
        }
        function tapSelect(cell) {
            var f = file(), list = itemsAt(f, cell.row, cell.column), at = 0, again = st.cycle && st.cycle.row === cell.row && st.cycle.column === cell.column && st.mode === 'picked' && st.kind !== 'area';
            if (!list.length) return clear();
            if (again) at = (st.cycle.at + 1) % list.length;
            var it = list[at];
            pick([it], 'one', it.k === 'ant' ? antGroup(f, it) : [], { row: cell.row, column: cell.column, at: at });
        }
        function startMove(items, kind, copy) {
            st.items = items; st.kind = kind; st.mode = 'moving'; st.copy = !!copy; st.dr = 0; st.dc = 0; st.note = ''; st.drag = null;
            evaluate(); paint();
            if (env.focus) env.focus();
        }
        function setDelta(dr, dc) { if (dr === st.dr && dc === st.dc) return; st.dr = dr; st.dc = dc; evaluate(); render(); drawOver(); }
        // the middle of what is picked goes to the cell (a tap)
        function centreOn(cell) {
            var b = bbox(file());
            if (b) setDelta(cell.row - Math.floor((b.r0 + b.r1) / 2), cell.column - Math.floor((b.c0 + b.c1) / 2));
        }
        function nudge(dr, dc) {
            var f = file(), b = bbox(f);
            if (!b) return;
            if (b.r0 + st.dr + dr < 0 || b.c0 + st.dc + dc < 0 || b.r1 + st.dr + dr >= f.rows || b.c1 + st.dc + dc >= f.columns) { st.note = 'It is at the edge of the map.'; return render(); }
            if (st.mode !== 'moving') { st.mode = 'moving'; st.copy = false; st.dr = 0; st.dc = 0; }
            st.note = '';
            setDelta(st.dr + dr, st.dc + dc);
            if (!st.dr && !st.dc) { evaluate(); render(); drawOver(); }
        }
        function act(change) { mine = true; var ok = env.act(change); mine = false; return ok; }
        function done() {
            var f = file();
            if (st.mode !== 'moving' || !st.items.length) return;
            if (!st.copy && !st.dr && !st.dc) return pick(st.items, st.kind, st.group, st.cycle, st.box);
            if (!evaluate().ok) return render();
            var change = build(f, st.items, st.dr, st.dc, how()), kind = st.kind, copy = st.copy, dr = st.dr, dc = st.dc, n = st.items.length, box = st.box;
            if (!act(change)) return clear('Nothing changed.');
            var items = resolve(file(), change.landed), word = copy ? 'Copied' : 'Moved', moved = [];
            if (dc) moved.push(plural(Math.abs(dc), dc < 0 ? 'column left' : 'column right', dc < 0 ? 'columns left' : 'columns right'));
            if (dr) moved.push(plural(Math.abs(dr), dr < 0 ? 'row up' : 'row down', dr < 0 ? 'rows up' : 'rows down'));
            if (!items.length) return clear(word + '.');
            pick(items, kind === 'area' ? 'area' : 'one', items.length === 1 && items[0].k === 'ant' ? antGroup(file(), items[0]) : [], null, kind === 'area' && box ? { r0: box.r0 + (copy ? dr : dr), c0: box.c0 + dc, r1: box.r1 + dr, c1: box.c1 + dc } : null);
            st.kind = kind === 'group' ? 'group' : st.kind;
            st.note = word + ' ' + (n === 1 ? 'it' : plural(n, 'thing', 'things')) + (copy ? '' : ': ' + moved.join(', ')) + '. One Undo takes it back.';
            render();
        }
        function deleteNow() {
            var f = file(), n = st.items.length;
            if (!n || st.mode !== 'picked') return;
            clear(act(build(f, st.items, 0, 0, 'delete')) ? 'Took away ' + (n === 1 ? 'it' : plural(n, 'thing', 'things')) + '. One Undo brings it back.' : 'Nothing changed.');
        }
        function startBox() { st.mode = 'boxing'; st.items = []; st.group = []; st.kind = 'area'; st.box = null; st.note = ''; st.pad = false; paint(); }
        function finishBox() {
            var f = file(), items = st.box ? areaItems(f, st.box) : [], box = st.box;
            if (!items.length) { st.mode = 'boxing'; st.items = []; st.box = null; st.note = 'Nothing is inside that box. Drag a bigger box, or Cancel.'; return paint(); }
            pick(items, 'area', [], null, box);
        }
        function letGo() { if (st.mode === 'moving' && st.items.length) pick(st.items, st.kind, st.group, st.cycle, st.box); else clear(); }

        // ---- the bar ----
        function button(key, text, cls, on) {
            var b = el('button', 'pk-b' + (cls ? ' ' + cls : ''), text);
            b.type = 'button'; b.setAttribute('data-k', key);
            b.addEventListener('click', function () { on(); });
            return b;
        }
        function row(cls) { return el('div', 'pk-row' + (cls ? ' ' + cls : '')); }
        function shiftText(dr, dc) {
            var out = [];
            if (dc) out.push(Math.abs(dc) + (dc < 0 ? ' left' : ' right'));
            if (dr) out.push(Math.abs(dr) + (dr < 0 ? ' up' : ' down'));
            return out.join(', ');
        }
        function render() {
            if (!bar) return;
            var f = file(), on = shown && st.mode !== 'idle' && !!f, line = el('p', 'pk-line'), a = row(), b = row();
            bar.hidden = !on;
            if (doc.body && doc.body.classList) doc.body.classList.toggle('pk-on', on);
            bar.textContent = '';
            if (!on) return;
            line.setAttribute('role', 'status');
            if (st.mode === 'boxing') {
                line.appendChild(el('span', 'pk-name', 'Select an area'));
                line.appendChild(el('span', 'pk-rest', ' · ' + (st.box ? plural(st.box.r1 - st.box.r0 + 1, 'row', 'rows') + ' x ' + plural(st.box.c1 - st.box.c0 + 1, 'column', 'columns') + '.' : (st.note || 'drag a box on the map to pick everything inside it.'))));
                bar.appendChild(line);
                a.appendChild(button('cancel', 'Cancel', '', clear));
                bar.appendChild(a);
                return;
            }
            if (st.mode === 'picked') {
                var d = describe(f, st.items, st.kind, st.group.length), grouped = st.kind === 'one' && st.items[0].k === 'ant' && st.group.length > 1, many = st.kind === 'area';
                var hill = st.items.length === 1 && st.items[0].k === 'obj' && st.items[0].hill >= 0, copy = button('copy', 'Copy here', '', function () { startMove(st.items, st.kind, true); });
                line.appendChild(el('span', 'pk-name', d.name)); line.appendChild(el('span', 'pk-rest', d.rest));
                bar.appendChild(line);
                if (hill) { copy.disabled = true; bar.appendChild(el('p', 'pk-said', 'A team has one hill, so it cannot be copied.')); }
                if (st.note) bar.appendChild(el('p', 'pk-said', st.note));
                if (grouped) {
                    a.className = 'pk-row two'; b.className = 'pk-row three';
                    a.appendChild(button('move', 'Move this ant', '', function () { startMove(st.items, 'one', false); }));
                    a.appendChild(button('group', 'Move its group (' + st.group.length + ')', '', function () { startMove(st.group, 'group', false); }));
                    b.appendChild(copy); b.appendChild(button('delete', 'Delete', '', deleteNow)); b.appendChild(button('cancel', 'Cancel', '', clear));
                } else if (many) {
                    a.className = 'pk-row four';
                    a.appendChild(button('move', 'Move them', '', function () { startMove(st.items, 'area', false); })); a.appendChild(copy);
                    a.appendChild(button('delete', 'Delete', '', deleteNow)); a.appendChild(button('cancel', 'Cancel', '', clear));
                } else {
                    a.className = 'pk-row three'; b.className = 'pk-row two';
                    a.appendChild(button('move', 'Move', '', function () { startMove(st.items, st.kind, false); })); a.appendChild(copy); a.appendChild(button('delete', 'Delete', '', deleteNow));
                    b.appendChild(button('area', 'Select an area...', '', startBox)); b.appendChild(button('cancel', 'Cancel', '', clear));
                }
                bar.appendChild(a);
                if (b.children.length) bar.appendChild(b);
                return;
            }
            // on its way: moving or copying
            var v = st.verdict, land = landing(f), name = nameOfPick(f, st.items, st.kind), ok = v.ok, them = st.items.length > 1 || st.kind === 'area' ? 'them' : 'it';
            var go = button('done', 'Done', '', done);
            go.disabled = !ok || (!st.copy && !st.dr && !st.dc);
            if (st.pad) {
                var pad = el('div', 'pk-pad'), side = el('div', 'pk-side'), padwrap = el('div', 'pk-padwrap');
                [['up', '↑', -1, 0], ['left', '←', 0, -1], ['right', '→', 0, 1], ['down', '↓', 1, 0]].forEach(function (arrow) {
                    var key = button(arrow[0], arrow[1], 'pk-arrow ' + arrow[0], function () { nudge(arrow[2], arrow[3]); });
                    key.setAttribute('aria-label', 'Nudge ' + arrow[0] + ' one cell');
                    pad.appendChild(key);
                });
                pad.appendChild(el('span', 'pk-one', '1 cell'));
                line.appendChild(el('span', 'pk-name', cap(name)));
                if (ok || v.plain) line.appendChild(el('span', 'pk-rest', ' ' + (st.dr || st.dc ? (st.copy ? 'Copy ' : 'Moved ') + shiftText(st.dr, st.dc) + '. ' : '') + 'Lands on row ' + land.row + ', column ' + land.column + '.'));
                side.appendChild(line);
                if (!ok && !v.plain) side.appendChild(el('p', 'pk-warn', v.text));
                if (st.note) side.appendChild(el('p', 'pk-said', st.note));
                side.appendChild(go); side.appendChild(button('cancel', 'Cancel', '', letGo));
                padwrap.appendChild(pad); padwrap.appendChild(side); bar.appendChild(padwrap);
                return;
            }
            if (ok || v.plain) {
                line.appendChild(el('span', 'pk-name', (st.copy ? 'Copying ' : 'Moving ') + name + '.'));
                line.appendChild(el('span', 'pk-rest', ' ' + (v.plain ? v.text : (st.copy ? 'Tap where the copy goes' : 'Tap the new place') + ', or drag ' + them + '. It lands on row ' + land.row + ', column ' + land.column + '.')));
                bar.appendChild(line);
            } else bar.appendChild(el('p', 'pk-warn', v.text));
            if (st.note) bar.appendChild(el('p', 'pk-said', st.note));
            a.className = 'pk-row three';
            a.appendChild(go);
            a.appendChild(button('nudge', 'Nudge arrows', '', function () { st.pad = true; render(); }));
            a.appendChild(button('cancel', 'Cancel', '', letGo));
            bar.appendChild(a);
        }

        // ---- drawing on the canvas that lies over the map ----
        function prepare() {
            var info = env.view ? env.view() : null, map = host.map, ctx = over && over.getContext && over.getContext('2d');
            if (!ctx || !info || !info.view || !map) return null;
            var w = info.width, h = info.height, dpr = Math.min(2, win.devicePixelRatio || 1);
            if (over.width !== Math.round(w * dpr) || over.height !== Math.round(h * dpr)) { over.width = Math.round(w * dpr); over.height = Math.round(h * dpr); }
            over.style.left = map.offsetLeft + 'px'; over.style.top = map.offsetTop + 'px'; over.style.width = w + 'px'; over.style.height = h + 'px';
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            ctx.clearRect(0, 0, w, h);
            return { ctx: ctx, view: info.view, w: w, h: h, s: info.view.scale };
        }
        function at(c, f, idx) { var p = rc(f, idx); return View.toScreen(c.view, c.w, c.h, p.r, p.c); }
        function seen(c, a) { return !(a.x > c.w || a.y > c.h || a.x + c.s < 0 || a.y + c.s < 0); }
        function tag(c, text, x, y) {
            c.ctx.font = '600 12px sans-serif';
            var tw = c.ctx.measureText(text).width + 10;
            c.ctx.fillStyle = 'rgba(8,19,15,.92)'; c.ctx.fillRect(x, y, tw, 18);
            c.ctx.fillStyle = '#e6fff0'; c.ctx.fillText(text, x + 5, y + 13);
        }
        // the outline of a set of cells: a line along each side that has no cell of the set beside it
        function outline(c, f, cells, color, width, dash) {
            var set = {}, i, ctx = c.ctx, s = c.s, cols = f.columns;
            for (i = 0; i < cells.length; i++) set[cells[i]] = true;
            ctx.beginPath();
            for (i = 0; i < cells.length; i++) {
                var a = at(c, f, cells[i]), p = rc(f, cells[i]);
                if (!seen(c, a)) continue;
                if (!set[cells[i] - cols]) { ctx.moveTo(a.x, a.y); ctx.lineTo(a.x + s, a.y); }
                if (!set[cells[i] + cols]) { ctx.moveTo(a.x, a.y + s); ctx.lineTo(a.x + s, a.y + s); }
                if (p.c === 0 || !set[cells[i] - 1]) { ctx.moveTo(a.x, a.y); ctx.lineTo(a.x, a.y + s); }
                if (p.c === cols - 1 || !set[cells[i] + 1]) { ctx.moveTo(a.x + s, a.y); ctx.lineTo(a.x + s, a.y + s); }
            }
            ctx.setLineDash(dash || []);
            ctx.lineWidth = width + 2; ctx.strokeStyle = 'rgba(0,0,0,.6)'; ctx.stroke();
            ctx.lineWidth = width; ctx.strokeStyle = color; ctx.stroke(); ctx.setLineDash([]);
        }
        function fill(c, f, cells, color) {
            var i, a;
            c.ctx.fillStyle = color;
            for (i = 0; i < cells.length; i++) { a = at(c, f, cells[i]); if (seen(c, a)) c.ctx.fillRect(a.x, a.y, c.s, c.s); }
        }
        function door(c, f, idx) {
            var a = at(c, f, idx);
            c.ctx.lineWidth = 2.5; c.ctx.strokeStyle = '#8be0b0'; c.ctx.strokeRect(a.x + 1, a.y + 1, c.s - 2, c.s - 2);
            tag(c, 'door', a.x + c.s / 2 - 16, a.y - 20);
        }
        function antMark(c, f, idx, team) {
            var a = at(c, f, idx), ctx = c.ctx;
            if (!seen(c, a)) return;
            ctx.globalAlpha = 0.72; ctx.beginPath(); ctx.arc(a.x + c.s / 2, a.y + c.s / 2, Math.max(2, Math.min(c.s * 0.38, 16)), 0, 6.2832);
            ctx.fillStyle = Tiles.TEAMS[Blocks.TEAMS[team].key]; ctx.fill(); ctx.lineWidth = 2; ctx.strokeStyle = '#f4ece2'; ctx.stroke(); ctx.globalAlpha = 1;
        }
        function drawOver() {
            var c = prepare(), f = file();
            if (!c || !f || !shown || st.mode === 'idle') return;
            var items = st.items, ctx = c.ctx, moving = st.mode === 'moving', i, a, z;
            if (st.box) {         // the box of an area: the one that is dragged, or the one that was picked
                a = View.toScreen(c.view, c.w, c.h, st.box.r0, st.box.c0); z = View.toScreen(c.view, c.w, c.h, st.box.r1 + 1, st.box.c1 + 1);
                ctx.fillStyle = 'rgba(242,194,48,.14)'; ctx.fillRect(a.x, a.y, z.x - a.x, z.y - a.y);
                ctx.setLineDash([7, 5]); ctx.lineWidth = 2.5; ctx.strokeStyle = '#000'; ctx.strokeRect(a.x, a.y, z.x - a.x, z.y - a.y);
                ctx.lineWidth = 1.5; ctx.strokeStyle = GOLD; ctx.strokeRect(a.x, a.y, z.x - a.x, z.y - a.y); ctx.setLineDash([]);
            }
            for (i = 0; i < items.length; i++) {         // the rings: what is picked (dashed while it is on its way: that is where it comes from)
                var it = items[i];
                if (it.fp.length === 1 && !seen(c, at(c, f, it.fp[0]))) continue;
                if (it.fp.length === 1 && !moving) fill(c, f, it.fp, 'rgba(242,194,48,.22)');
                outline(c, f, it.fp, GOLD, moving ? 2 : 3.5, moving ? [6, 4] : null);
                if (!moving && it.k === 'obj' && it.hill >= 0 && it.entrance >= 0) door(c, f, it.entrance);
            }
            if (st.mode === 'picked' && st.kind === 'one' && st.group.length > 1) for (i = 1; i < st.group.length; i++) outline(c, f, st.group[i].fp, GOLD, 2, [4, 3]);
            if (!moving) return;
            var v = st.verdict, good = v.ok || v.plain, sprites = env.sprites ? env.sprites() : null, few = items.length <= 400, b0 = bbox(f);
            for (i = 0; i < items.length; i++) {         // the ghost: see-through, green where it can go and red where it cannot
                var g = st.target[i] || [], gi = items[i];
                if (!g.length) continue;
                if (few) fill(c, f, g, good ? 'rgba(120,230,170,.30)' : 'rgba(239,83,80,.34)');
                if (gi.k === 'ant') antMark(c, f, g[0], gi.team);
                else if (sprites && sprites.ghost && c.s >= 10 && few) {
                    var o = gi.k === 'obj' && gi.entrance >= 0 ? Math.max(0, gi.fp.indexOf(gi.entrance)) : 0, gp = rc(f, g[o]);
                    sprites.ghost(ctx, c.view, c.w, c.h, gi.tile, gp.r, gp.c);
                }
                outline(c, f, g, good ? '#8be0b0' : '#f08a84', 3, null);
                if (gi.k === 'obj' && gi.hill >= 0 && gi.entrance >= 0 && good) door(c, f, g[gi.fp.indexOf(gi.entrance)]);
            }
            if (!good && v.cells && v.cells.length) outline(c, f, v.cells, '#ffd0cc', 2, [3, 3]);
            if (b0 && !st.copy && (st.dr || st.dc)) { a = View.toScreen(c.view, c.w, c.h, b0.r0, b0.c0); tag(c, 'from', a.x, a.y - 20); }
        }
        function follow() {
            loop = 0;
            if (!shown || st.mode === 'idle') return;
            var info = env.view(), sign = info && info.view ? [info.view.cx, info.view.cy, info.view.scale, info.width, info.height].join(',') : '';
            if (sign !== last) { last = sign; drawOver(); }
            if (win.requestAnimationFrame) loop = win.requestAnimationFrame(follow);
        }
        function watch() { if (!loop && win.requestAnimationFrame && shown && st.mode !== 'idle') { last = ''; loop = win.requestAnimationFrame(follow); } }

        if (wrap && doc.createElement('canvas').getContext) {
            over = doc.createElement('canvas'); over.className = 'pk-over'; over.setAttribute('aria-hidden', 'true');
            host.cv.appendChild(over);
        }
        if (wrap) {
            bar = el('div', 'pk-bar'); bar.hidden = true; bar.setAttribute('role', 'region'); bar.setAttribute('aria-label', 'Pick up');
            wrap.insertBefore(bar, host.cv.nextSibling);
        }

        // ---- the painter the Pick up tool hands the canvas to (mapmaker_tools.js) ----
        function inTarget(cell) {
            var idx = cell.row * file().columns + cell.column, i, k;
            for (i = 0; i < st.target.length; i++) for (k = 0; k < st.target[i].length; k++) if (st.target[i][k] === idx) return true;
            return false;
        }
        function hasThing(cell) { var f = file(); return !!f && itemsAt(f, cell.row, cell.column).length > 0; }
        var painter = {
            size: function () { return 0; },
            // a press on the ghost (while a thing is on its way), anywhere (while a box is drawn) or on a thing is a drag; a press elsewhere looks around, and a tap there is the tool's (tapOnly)
            canMove: function (cell) {
                if (!shown || !file()) return false;
                if (st.mode === 'boxing') return true;
                return st.mode === 'moving' ? inTarget(cell) : hasThing(cell);
            },
            tapOnly: function (cell) { return shown && !painter.canMove(cell); },
            begin: function (cell) {
                if (!shown || !file()) return false;
                if (st.mode === 'boxing') { st.drag = { kind: 'box', from: cell }; st.box = { r0: cell.row, c0: cell.column, r1: cell.row, c1: cell.column }; paint(); return true; }
                if (st.mode === 'moving') {
                    if (!inTarget(cell)) { centreOn(cell); return false; }
                    st.drag = { kind: 'ghost', from: cell, dr: st.dr, dc: st.dc };
                    return true;
                }
                if (hasThing(cell)) { st.drag = { kind: 'item', from: cell, moved: false }; return true; }
                if (st.mode === 'picked') clear();
                return false;
            },
            extend: function (from, to) {
                var d = st.drag;
                if (!d) return;
                if (d.kind === 'box') { st.box = { r0: Math.min(d.from.row, to.row), c0: Math.min(d.from.column, to.column), r1: Math.max(d.from.row, to.row), c1: Math.max(d.from.column, to.column) }; render(); drawOver(); }
                else if (d.kind === 'ghost') setDelta(d.dr + to.row - d.from.row, d.dc + to.column - d.from.column);
                else if (!d.moved) {
                    var f = file(), idx0 = d.from.row * f.columns + d.from.column, sel = null, i;
                    for (i = 0; i < st.items.length; i++) if (st.items[i].fp.indexOf(idx0) >= 0) sel = st.items;
                    if (!sel) { var list = itemsAt(f, d.from.row, d.from.column); if (!list.length) return; sel = [list[0]]; st.group = []; st.kind = 'one'; }
                    d.moved = true;
                    startMove(sel, st.kind === 'area' && sel === st.items ? 'area' : (st.kind === 'group' && sel === st.items ? 'group' : 'one'), false);
                    st.drag = d;
                    setDelta(to.row - d.from.row, to.column - d.from.column);
                } else setDelta(to.row - d.from.row, to.column - d.from.column);
            },
            end: function () {
                var d = st.drag; st.drag = null;
                if (!d) return;
                if (d.kind === 'box') return finishBox();
                if (d.kind === 'item' && !d.moved) tapSelect(d.from); else render();
            },
            cancel: function () {
                var d = st.drag; st.drag = null;
                if (!d) return;
                if (d.kind === 'box') { st.box = null; paint(); } else if (d.kind === 'ghost') setDelta(d.dr, d.dc); else if (d.moved) letGo();
            },
            // the keys: arrows nudge a thing that is picked up, Enter picks the thing on the marked cell, puts it down (Done) or starts the move, Escape lets go, Delete takes it away
            key: function (e) {
                if (!shown || !file() || (env.busy && env.busy())) return false;
                var k = e.key, step = e.shiftKey ? 8 : 1, arrows = { ArrowUp: [-1, 0], ArrowDown: [1, 0], ArrowLeft: [0, -1], ArrowRight: [0, 1] };
                if (st.mode === 'idle') {
                    if (k !== 'Enter') return false;
                    var info = env.view(), cur = info && info.cursor;
                    if (cur) tapSelect(cur);
                    return !!cur;
                }
                if (arrows[k] && st.mode !== 'boxing') { nudge(arrows[k][0] * step, arrows[k][1] * step); return true; }
                if (k === 'Escape') { letGo(); return true; }
                if (k === 'Enter') { if (st.mode === 'moving') done(); else if (st.mode === 'picked') startMove(st.items, st.kind, false); return st.mode !== 'boxing'; }
                if ((k === 'Delete' || k === 'Backspace') && st.mode === 'picked') { deleteNow(); return true; }
                return false;
            }
        };
        // Escape lets go also when the focus is on a button of the bar
        doc.addEventListener('keydown', function (e) {
            if (e.defaultPrevented || !shown || st.mode === 'idle' || e.key !== 'Escape' || (env.busy && env.busy())) return;
            var tag2 = e.target && e.target.tagName ? String(e.target.tagName).toLowerCase() : '';
            if (tag2 === 'input' || tag2 === 'textarea' || tag2 === 'select') return;
            e.preventDefault(); letGo();
        });

        return {
            painter: painter,
            // the tool is chosen (true) or another is (false)
            show: function (on) { shown = !!on; if (!on) clear(); else render(); },
            // the map changed (an Undo, another tool, the properties box...): what is picked is let go when it is not as it was
            refresh: function () {
                if (mine || st.mode === 'idle' || !st.items.length) return;
                var f = file(), i;
                for (i = 0; i < st.items.length; i++) if (!f || !same(f, st.items[i])) return clear();
            },
            setMap: function () { st.mode = 'idle'; st.items = []; st.group = []; st.box = null; st.drag = null; st.note = ''; st.target = []; render(); },
            state: function () { return { mode: st.mode, kind: st.kind, copy: st.copy, dr: st.dr, dc: st.dc, items: st.items.slice(), group: st.group.slice(), box: st.box, verdict: st.verdict, note: st.note, pad: st.pad }; },
            bar: bar, over: over
        };
    }

    return { create: create, itemsAt: itemsAt, areaItems: areaItems, antGroup: antGroup, check: check, build: build, resolve: resolve, describe: describe, nameOf: nameOf, same: same, index: index, shifted: shifted };
});
