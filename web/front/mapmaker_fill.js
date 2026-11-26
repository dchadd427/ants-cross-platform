// The map maker's "Fill a box" (docs/MAP_EDITOR.md, "Fill a box"): with the switch on, the placing tools (Objects, with the plants and flowers, Food and Ants) take a dragged box instead of a tap, and put
// their thing on the cells of a pattern (every cell, every other cell, every Nth row or column, or random). The first half of this script is arithmetic on the bytes of the open file, shared with the
// AI's `fill_area` (mapmaker_ai_tools.js): which cells of the box are in the pattern, why a cell cannot take the thing (water, a bridge piece on dry land, solid ground, something already there), the
// cap of one fill, and the CHANGE that puts the whole fill into the history as one step (mapmaker_blocks.js has the lists, mapmaker_edit.js the history). Nothing here writes a byte itself. The second
// half is the box in the page: the switch's settings in the right-hand column (a sheet on a phone), the dragged box with its live preview and count drawn over the map, and the painter that the Objects,
// Food and Ants tools hand the canvas to while the switch is on (mapmaker_tools.js). A lazily loaded script (the page loads it, with mapmaker_fill.css, the first time the switch is turned on, and
// the AI part loads it for `fill_area`). The words that a person reads are put in with textContent. A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_edit.js'), require('./mapmaker_blocks.js'), require('./mapmaker_tiles.js'), require('./mapmaker_solid.js'), require('./mapmaker_view.js'), require('./mapmaker_palette.js'));
    else root.MapFill = factory(root.MapEdit, root.MapBlocks, root.MapTiles, root.MapSolid, root.MapView, root.MapPalette);
})(this, function (Edit, Blocks, Tiles, Solid, View, Palette) {
    'use strict';

    var EMPTY2 = Edit.EMPTY2;
    var BRIDGE = 37, SWIMMER = 5;                    // the Bridge chip (a piece that needs water under it) and the starting ant type that swims (the file's own number: docs/MAP_FORMAT.md, "Limits")
    var CAP_ANTS = Blocks.MAX_ANTS, CAP_THINGS = 5000;      // the most that one fill puts: ants as many as one clump, anything else as many as one call of the AI
    var N_MIN = 2, N_MAX = 9, DENSITY_MIN = 5, DENSITY_MAX = 95, SEED_MAX = 9999;
    var PATTERNS = [['all', 'Every cell'], ['checker', 'Every other cell'], ['rows', 'Every Nth row'], ['cols', 'Every Nth column'], ['random', 'Random']];
    var COLORS = ['#2f9a52', '#c0392b', '#4a5fc8', '#262626'];      // the team colors by seat (as mapmaker_tiles.js and the Ants box have them)
    var TEAM_NAMES = ['Green', 'Red', 'Blue', 'Black'];
    var KEY_LINE = ' is a skipped cell: water, rock, or something already there.';

    function plural(n, one, many) { return num(n) + ' ' + (n === 1 ? one : many); }
    function num(n) { return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }
    function ordinal(n) { var s = ['th', 'st', 'nd', 'rd'], v = n % 100; return n + (s[(v - 20) % 10] || s[v] || s[0]); }
    function clamp(v, lo, hi) { v = Math.round(+v); return isNaN(v) ? lo : Math.max(lo, Math.min(hi, v)); }

    // ---- the things that a fill puts: {kind: 'obj' | 'plant' | 'pile' | 'ant', one, many, label, ...} ----

    // an object of one cell (the Objects tool's tiles that are not plants): a bomb, a power-up, a rock, a fire wall, a bridge piece; null for any other tile
    function objectThing(id) {
        var item = Palette.itemOf('objects', id), one = 'object', many = 'objects';
        if (!item || item.plant) return null;
        if (id >= 129 && id <= 132) { one = 'bomb'; many = 'bombs'; } else if (id >= 62 && id <= 66) { one = 'power-up'; many = 'power-ups'; } else if (id >= 302 && id <= 308) { one = 'rock'; many = 'rocks'; }
        else if (id === 134) { one = 'fire wall piece'; many = 'fire wall pieces'; } else if (id === BRIDGE) { one = 'bridge piece'; many = 'bridge pieces'; }
        return { kind: 'obj', id: id, name: item.name, label: item.label, one: one, many: many };
    }
    // a plant or flower, with the seconds between the power-ups that it drops (0: none)
    function plantThing(id, dropper) {
        var item = Palette.itemOf('plants', id), clover = !!item && /clover/i.test(item.label);
        return item ? { kind: 'plant', id: id, label: item.label, dropper: clamp(dropper || 0, 0, 180), one: clover ? 'clover' : 'plant', many: clover ? 'clovers' : 'plants' } : null;
    }
    // a food pile of `stages` stages, or of a food item of the list; `custom` {id, units, points} is what Blocks.placePile takes (or null for the plain pile)
    function pileThing(stages, custom) {
        var item = custom && custom.id ? Blocks.foodItem(custom.id) : null, n = clamp(stages, Blocks.MIN_STAGES, Blocks.MAX_STAGES);
        return { kind: 'pile', stages: n, custom: custom || null, label: item ? item.label : n + '-stage pile', one: 'food pile', many: 'food piles' };
    }
    // starting ants of a team (a seat 0 to 3) and of an ant type (a number of the file; 0 is the default ant, and a Classic map has no other)
    function antThing(team, type) { return { kind: 'ant', team: team, type: type || 0, label: TEAM_NAMES[team] + ' ants', one: 'ant', many: 'ants' }; }
    // the most that one fill puts: the cap of the kind, and no more than the file's list has room for (ants and plants share block 1; a list that does not read is the change's refusal, not the cap's)
    function capOf(file, thing) {
        var lay = Blocks.layout(file), cap = thing.kind === 'ant' ? CAP_ANTS : CAP_THINGS;
        if (thing.kind === 'pile' && lay.ok2) cap = Math.min(cap, 65535 - lay.piles.length);
        if ((thing.kind === 'ant' || thing.kind === 'plant') && lay.ok1) cap = Math.min(cap, Blocks.MAX_RECORDS - lay.records.length);
        return Math.max(0, cap);
    }

    // ---- the pattern ----

    // A pattern {pattern, n, density, seed} with every number put in its range
    function norm(p) {
        p = p || {};
        var known = PATTERNS.some(function (x) { return x[0] === p.pattern; });
        return { pattern: known ? p.pattern : 'all', n: clamp(p.n === undefined ? 3 : p.n, N_MIN, N_MAX), density: clamp(p.density === undefined ? 30 : p.density, DENSITY_MIN, DENSITY_MAX), seed: clamp(p.seed === undefined ? 1 : p.seed, 1, SEED_MAX) };
    }
    // a number from 0 up to 1 for a cell and a seed (an integer hash: the same cell and seed give the same number on every machine, whatever the box is)
    function hash(seed, r, c) {
        var h = (Math.imul(seed, 374761393) + Math.imul(r + 1, 668265263) + Math.imul(c + 1, 2147483647)) | 0;
        h = Math.imul(h ^ (h >>> 13), 1274126177); h ^= h >>> 16;
        h = Math.imul(h ^ (h >>> 15), 2246822519); h ^= h >>> 13;
        return (h >>> 0) / 4294967296;
    }
    // Is the cell (r, c), which is (rr, cc) cells from the box's top left corner, on the pattern? Everything is counted from the corner, so what is dragged is what is put.
    function inPattern(p, rr, cc, r, c) {
        switch (p.pattern) {
            case 'checker': return (rr + cc) % 2 === 0;
            case 'rows': return rr % p.n === 0;
            case 'cols': return cc % p.n === 0;
            case 'random': return hash(p.seed, r, c) < p.density / 100;
            default: return true;
        }
    }

    // ---- which cells of the box can take the thing ----

    // The cells that hold something of the lists (a start of ants, a plant, a food pile): by cell
    function occupied(file) {
        var lay = Blocks.layout(file), taken = {}, i;
        for (i = 0; i < lay.records.length; i++) { var rec = lay.records[i]; if (rec.row < file.rows && rec.column < file.columns) taken[rec.row * file.columns + rec.column] = true; }
        for (i = 0; i < lay.piles.length; i++) { var pile = lay.piles[i]; if (pile.row < file.rows && pile.column < file.columns) taken[pile.row * file.columns + pile.column] = true; }
        return taken;
    }
    // Why the thing cannot go on the cell `idx`: 'taken' (a hill, ants, a plant or a pile is there), 'rock' (solid ground, or a rock or another solid object stands there), 'taken' too for any other object,
    // 'dry' (a bridge piece needs water under it), 'water' (water, which only a bridge piece and a swimmer ant of an Extended map are put on), or '' when it can go
    function whyNot(file, idx, thing, taken) {
        var t2 = Edit.tile(file, 2, idx);
        if (taken[idx] || Blocks.teamOfHill(t2) >= 0) return 'taken';
        if (Edit.flags(file, 1, idx) & 1) return 'rock';
        if (t2 !== EMPTY2) return 'taken';
        var water = Tiles.kindOf(Edit.tile(file, 1, idx)) === 'water';
        if (thing.kind === 'obj' && thing.id === BRIDGE) return water ? '' : 'dry';
        if (water && !(thing.kind === 'ant' && file.version === 9 && thing.type === SWIMMER)) return 'water';
        return '';
    }

    // The plan of a fill of the box {r0, c0, r1, c1} (inside the map): {box, thing, rows, columns, pattern (the cells on the pattern), cells (where the thing goes: at most the cap), extra (the cells that
    // the cap leaves out), skipped: [{r, c, why}], skips: {water, dry, rock, taken}, cap, pat}. A thing of a Classic map has no type but the default ant's.
    function plan(file, thing, box, pat) {
        var p = norm(pat), taken = occupied(file), cells = [], skipped = [], skips = { water: 0, dry: 0, rock: 0, taken: 0 }, hit = 0, cap = capOf(file, thing), r, c;
        if (thing.kind === 'ant' && file.version !== 9 && thing.type) thing = antThing(thing.team, 0);
        for (r = box.r0; r <= box.r1; r++) for (c = box.c0; c <= box.c1; c++) {
            if (!inPattern(p, r - box.r0, c - box.c0, r, c)) continue;
            hit++;
            var why = whyNot(file, r * file.columns + c, thing, taken);
            if (why) { skipped.push({ r: r, c: c, why: why }); skips[why]++; } else cells.push({ r: r, c: c });
        }
        var extra = cells.length > cap ? cells.slice(cap) : [];
        return { box: box, thing: thing, rows: box.r1 - box.r0 + 1, columns: box.c1 - box.c0 + 1, pattern: hit, cells: extra.length ? cells.slice(0, cap) : cells, extra: extra, skipped: skipped, skips: skips, cap: cap, pat: p };
    }

    // ---- the change ----

    function emptyChange() { return { names: [], cells: [], solid: [], splices: [] }; }
    function addName(change, id, name) { for (var i = 0; i < change.names.length; i++) if (change.names[i][0] === id) return; change.names.push([id, name]); }
    function sortSplices(change) { change.splices.sort(function (a, b) { return b.rel - a.rel; }); return change; }
    function tooMany(have, n) { return 'The file holds at most 65,535 starting ants and plants together, and it has ' + num(have) + ': ' + num(n) + ' more do not fit. Take some away first.'; }

    // Plants on many cells as ONE change (what Blocks.placePlant does for one: a record in block 1 and a waypoint in block 4, with the dropper's 52 bytes for a flower that drops power-ups)
    function placePlants(file, cells, id, dropper) {
        var lay = Blocks.layout(file), at = {}, records, waypoints = [], change = emptyChange(), i;
        if (!Blocks.PLANT_NAMES[id]) return { error: 'There is no such plant.' };
        if (!lay.ok1 || !lay.ok4) return { error: 'The list of plants in this file cannot be changed here: the file ends in the middle of it.' };
        if (lay.records.length + cells.length > Blocks.MAX_RECORDS) return { error: tooMany(lay.records.length, cells.length) };
        records = lay.records.slice();
        cells.forEach(function (cell) { at[cell.r + ',' + cell.c] = true; records.push({ tile: id, row: cell.r, column: cell.c }); });
        for (i = 0; i < lay.waypoints.length; i++) { var w = lay.waypoints[i]; if (!at[w.row + ',' + w.column]) waypoints.push({ keep: true, rel: w.rel, length: w.length }); }
        cells.forEach(function (cell) { waypoints.push(dropper > 0 ? { row: cell.r, column: cell.c, flag: 1, interval: Blocks.clampTo(dropper, 1, 3600), probs: [0.2, 0.2, 0.2, 0.2, 0.2] } : { row: cell.r, column: cell.c, flag: 0 }); });
        if (dropper > 0) Blocks.POWERUP_NAMES.forEach(function (pu) { addName(change, pu[0], pu[1]); });
        var sp1 = Blocks.diffSplice(file, lay.b1.rel, lay.b1.length, Blocks.block1Bytes(records)), sp4 = Blocks.diffSplice(file, lay.b4.rel, lay.b4.length, Blocks.block4Bytes(file, waypoints));
        if (sp1) change.splices.push(sp1);
        if (sp4) change.splices.push(sp4);
        addName(change, id, Blocks.PLANT_NAMES[id]);
        return sortSplices(change);
    }
    // Food piles on many cells as ONE change (what Blocks.placePile does for one: the piles of block 2 and the names of the stages)
    function placePiles(file, cells, stages, custom) {
        var lay = Blocks.layout(file), set = Blocks.FOOD[Blocks.clampTo(stages, Blocks.MIN_STAGES, Blocks.MAX_STAGES)], list = [], change = emptyChange(), i;
        if (!lay.ok2) return { error: 'The list of food piles in this file cannot be changed here: it does not end where its count says.' };
        if (custom) set = Blocks.customPile(set, custom);
        for (i = 0; i < lay.piles.length; i++) list.push({ keep: true, rel: lay.piles[i].rel, length: lay.piles[i].length });
        cells.forEach(function (cell) { list.push({ row: cell.r, column: cell.c, units: set.units, points: set.points, stages: set.stages }); });
        var splice = Blocks.diffSplice(file, lay.b2.rel, lay.b2.length, Blocks.block2Bytes(file, list));
        if (splice) change.splices.push(splice);
        set.stages.forEach(function (s) { if (s[1] !== EMPTY2) addName(change, s[1], Blocks.FOOD_NAMES[s[1]]); });
        return change;
    }
    // Objects on many cells (the cells of layer 2, the solid bit where the tile sets it, the tile's name in the dictionary), as the Objects tool's tap puts one
    function placeObjects(file, cells, thing) {
        var change = emptyChange();
        addName(change, thing.id, thing.name);
        cells.forEach(function (cell) {
            var idx = cell.r * file.columns + cell.c;
            change.cells.push([idx, thing.id, 1, Edit.ownAnchor(file, idx, thing.id)]);
            if (Solid.sets(thing.id)) change.solid.push([idx, true]);
        });
        return change;
    }
    // The CHANGE that puts the cells (a plan's `cells`) on the map, as one step of the history; {error} when the file cannot take it. Ants are appended as one list, as the Mix does.
    function changeOf(file, thing, cells) {
        if (thing.kind === 'obj') return placeObjects(file, cells, thing);
        if (thing.kind === 'plant') return placePlants(file, cells, thing.id, thing.dropper);
        if (thing.kind === 'pile') return placePiles(file, cells, thing.stages, thing.custom);
        var lay = Blocks.layout(file), type = file.version === 9 ? thing.type : 0;
        if (!lay.ok1) return { error: 'The list of starting ants in this file cannot be changed here: the file ends in the middle of it.' };
        if (lay.records.length + cells.length > Blocks.MAX_RECORDS) return { error: tooMany(lay.records.length, cells.length) };
        return Blocks.appendStarts(file, lay, cells.map(function (cell) { return { tile: Blocks.markerTile(file, thing.team, type), row: cell.r, column: cell.c }; }));
    }

    // ---- the words ----

    function countText(p) { return plural(p.cells.length, p.thing.one, p.thing.many); }
    function skipText(p) {
        var s = p.skips, parts = [];
        if (s.water) parts.push(s.water + ' water');
        if (s.dry) parts.push(s.dry + ' dry ground');
        if (s.rock) parts.push(s.rock + ' rock');
        if (s.taken) parts.push(s.taken + ' in the way');
        return parts.join(', ');
    }
    // the sentence of a fill that found nothing to put (also the AI's refusal)
    function nothingText(p) {
        if (p.cap === 0 && p.extra.length) return 'The file cannot take any more of these: it holds as many starting ants and plants (or food piles) as a file can.';
        return !p.pattern ? 'Nothing to put here: no cell of the box is on the pattern. Drag a bigger box, or choose another pattern.'
            : 'Nothing to put here: ' + plural(p.skipped.length, 'cell', 'cells') + ' of the pattern are skipped (' + skipText(p) + ').';
    }

    // ---- the box in the page ----

    // env: {doc, win, host: {tools (the column of the tools: the phone's line of the switch is in it, before `hint`), hint, side (the column on the right, which the box takes over on a wider screen, its
    //       check line stays), cv (the node the map canvas is in), map (the map canvas), row (the switch's line in the tools)}, file(), act(change) -> bool, say(title, text), dialogs, busy(),
    //       state() -> the tools' state (tool, objects, food, stages, units, points, dropper, team, antType), setTeam(seat), setAntType(n), antTypes() -> [[type, name, sprite id]] (the Ants box's list when
    //       it is not given), sprites() -> the pictures when they are loaded, pic(node, layer2, id, px), view() -> {view, width, height}, refresh(), stroking() and cancel() (the canvas's stroke),
    //       step() -> {top, redo} of the history }
    function create(env) {
        var doc = env.doc, win = env.win || {}, st = { pattern: 'checker', n: 3, density: 30, seed: 1, box: null, plan: null, said: '', kind: '', last: null }, shown = false, painting = false, start = null, loop = 0, last = '';
        var query = win.matchMedia ? win.matchMedia('(max-width: 600px)') : null, pane = doc.createElement('div'), over = null, badge = null, patBtn = null, phoneStatus = null, sheet = null;
        pane.className = 'fb'; pane.hidden = true; pane.setAttribute('role', 'group'); pane.setAttribute('aria-label', 'Fill a box');

        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function file() { return env.file(); }
        function narrow() { return query ? query.matches : false; }
        function pat() { return { pattern: st.pattern, n: st.n, density: st.density, seed: st.seed }; }

        // the thing that the tool puts now
        function thing() {
            var s = env.state(), f = file();
            if (s.tool === 'ants') return antThing(s.team, f && f.version === 9 ? s.antType : 0);
            if (s.tool === 'food') return pileThing(s.stages, { id: s.food, units: s.units, points: s.points });
            return Blocks.PLANT_NAMES[s.objects] ? plantThing(s.objects, s.dropper) : objectThing(s.objects);
        }
        function setPlan() { var t = st.box && file() ? thing() : null; st.plan = t ? plan(file(), t, st.box, pat()) : null; }

        // ---- the words of the status ----
        function status() {
            var p = st.plan;
            if (p && st.box) {
                var head = plural(p.rows, 'row', 'rows') + ' x ' + plural(p.columns, 'column', 'columns') + ': ' + countText(p) + '.', skips = p.skipped.length ? ' ' + plural(p.skipped.length, 'cell', 'cells') + ' skipped (' + skipText(p) + ').' : '';
                if (p.extra.length) return { cls: 'warn', text: num(p.cells.length + p.extra.length) + ' ' + p.thing.many + ' would fit this box at this spacing. One fill puts at most ' + num(p.cap) + ', so the last ' + num(p.extra.length) + ' (the dashed ones) are left out. Drag a smaller box, or space them out.' };
                if (!p.cells.length) return { cls: 'warn', text: nothingText(p) };
                return { cls: '', text: head + skips };
            }
            if (st.said) return { cls: 'said', text: st.said };
            return { cls: 'idle', text: 'Drag a box on the map. Let go, and it fills.' };
        }

        // ---- the settings: the pane in the right-hand column and the same choices in a sheet on a phone ----
        function mini(kind, n) {
            var g = el('span', 'fb-mini'), i;
            for (i = 0; i < 25; i++) g.appendChild(el('i', inPattern({ pattern: kind, n: n, density: 32, seed: 11 }, (i / 5) | 0, i % 5, (i / 5) | 0, i % 5) ? 'on' : ''));
            return g;
        }
        function patName(k) { return k === 'rows' ? 'Every ' + ordinal(st.n) + ' row' : (k === 'cols' ? 'Every ' + ordinal(st.n) + ' column' : (k === 'random' ? 'Random, ' + st.density + '%' : PATTERNS.filter(function (x) { return x[0] === k; })[0][1])); }
        function summary() {
            var s = env.state(), base = patName(st.pattern);
            return s.tool === 'ants' ? TEAM_NAMES[s.team] + ' ants, ' + base.charAt(0).toLowerCase() + base.slice(1) : base;
        }
        function stepper(value, lo, hi, set, what, key) {
            var box = el('div', 'fb-step'), less = el('button', 'fb-b fb-s', '−'), more = el('button', 'fb-b fb-s', '+'), val = el('output', 'fb-val', String(value));
            less.type = 'button'; more.type = 'button'; less.disabled = value <= lo; more.disabled = value >= hi;
            less.setAttribute('data-k', key + '-'); more.setAttribute('data-k', key + '+');
            less.setAttribute('aria-label', 'Fewer ' + what); more.setAttribute('aria-label', 'More ' + what); val.setAttribute('aria-live', 'polite');
            less.addEventListener('click', function () { set(Math.max(lo, value - 1)); }); more.addEventListener('click', function () { set(Math.min(hi, value + 1)); });
            box.appendChild(less); box.appendChild(val); box.appendChild(more);
            return box;
        }
        function chosen() { setPlan(); render(); drawOver(); env.refresh(); }
        function team(host, s) {
            host.appendChild(el('div', 'fb-h', 'Team'));
            var row = el('div', 'fb-grid four');
            [3, 2, 0, 1].forEach(function (t) {
                var b = el('button', 'fb-b fb-team'), dot = el('i', 'fb-dot');
                b.type = 'button'; b.setAttribute('aria-pressed', String(t === s.team)); b.setAttribute('data-k', 't' + t);
                dot.style.background = COLORS[t]; b.appendChild(dot); b.appendChild(el('span', 'fb-tn', TEAM_NAMES[t]));
                b.addEventListener('click', function () { env.setTeam(t); chosen(); });
                row.appendChild(b);
            });
            host.appendChild(row);
            var types = env.antTypes ? env.antTypes() : (win.MapAnts ? win.MapAnts.ANT_TYPES : []);
            if (file() && file().version === 9 && types.length) {
                host.appendChild(el('div', 'fb-h', 'Ant type'));
                var grid = el('div', 'fb-grid two');
                types.forEach(function (it) {
                    var b = el('button', 'fb-b fb-type'), ic = el('i', 'fb-pic');
                    b.type = 'button'; b.setAttribute('aria-pressed', String(s.antType === it[0])); b.setAttribute('data-k', 'at' + it[0]);
                    if (env.pic) env.pic(ic, true, it[2], 22);
                    b.appendChild(ic); b.appendChild(el('span', 'fb-tn', it[1]));
                    b.addEventListener('click', function () { env.setAntType(it[0]); chosen(); });
                    grid.appendChild(b);
                });
                host.appendChild(grid);
            }
        }
        function settings(host, inSheet) {
            var s = env.state(), t = thing(), chip, list = el('div', 'fb-pats');
            host.textContent = '';
            if (!inSheet) { host.appendChild(el('div', 'fb-h', 'Fill a box')); host.top = el('div', 'fb-top'); host.appendChild(host.top); }
            if (s.tool === 'ants') team(host, s);
            else {
                var what = el('div', 'fb-what'), words = el('span', 'fb-wt');
                chip = el('i', 'fb-chip'); what.appendChild(chip); words.appendChild(el('b', '', t ? t.label : '')); what.appendChild(words); host.appendChild(what);
                var sp = env.sprites ? env.sprites() : null, id = t ? (t.kind === 'pile' ? (t.custom && t.custom.id ? t.custom.id : Blocks.FOOD[t.stages].stages[0][1]) : t.id) : 0;
                if (sp && id) sp.chip(chip, true, id, 26); else chip.style.background = '#8aa';
            }
            host.appendChild(el('div', 'fb-h', 'Pattern'));
            list.setAttribute('role', 'radiogroup'); list.setAttribute('aria-label', 'Pattern');
            PATTERNS.forEach(function (pt) {
                var k = pt[0], on = st.pattern === k, box = el('div', 'fb-pat' + (on ? ' on' : '')), head = el('button', 'fb-pb');
                head.type = 'button'; head.setAttribute('role', 'radio'); head.setAttribute('aria-checked', String(on)); head.setAttribute('data-k', 'p-' + k);
                head.appendChild(mini(k, st.n)); head.appendChild(el('span', 'fb-pn', on ? patName(k) : pt[1]));
                head.addEventListener('click', function () { st.pattern = k; chosen(); });
                box.appendChild(head);
                if (on && (k === 'rows' || k === 'cols')) {
                    box.appendChild(stepper(st.n, N_MIN, N_MAX, function (v) { st.n = v; chosen(); }, k === 'rows' ? 'rows apart' : 'columns apart', 'n'));
                    box.appendChild(el('small', 'fb-cap', 'Every ' + ordinal(st.n) + (k === 'rows' ? ' row, counted from the top of the box.' : ' column, counted from the left side of the box.')));
                }
                if (on && k === 'random') {
                    var dens = el('div', 'fb-dens'), range = el('input', 'fb-range'), out = el('output', 'fb-val', st.density + '%'), more = el('div', 'fb-again'), fresh = el('button', 'fb-b', 'New pattern'), seed = el('small', '', '');
                    range.type = 'range'; range.min = String(DENSITY_MIN); range.max = String(DENSITY_MAX); range.step = '5'; range.value = String(st.density); range.setAttribute('aria-label', 'Density, percent of the cells'); range.setAttribute('data-k', 'density');
                    range.addEventListener('input', function () { st.density = clamp(range.value, DENSITY_MIN, DENSITY_MAX); out.textContent = st.density + '%'; head.querySelector('.fb-pn').textContent = patName('random'); setPlan(); renderBox(); drawOver(); env.refresh(); });
                    dens.appendChild(el('span', 'fb-dl', 'Density')); dens.appendChild(range); dens.appendChild(out); box.appendChild(dens);
                    fresh.type = 'button'; fresh.setAttribute('data-k', 'seed'); seed.textContent = 'Pattern ' + st.seed + '. Undo and Redo repeat it.';
                    fresh.addEventListener('click', function () { st.seed = st.seed % SEED_MAX + 1; chosen(); });
                    more.appendChild(fresh); more.appendChild(seed); box.appendChild(more);
                }
                list.appendChild(box);
            });
            host.appendChild(list);
            if (!inSheet) { host.rest = el('div', 'fb-rest'); host.appendChild(host.rest); }
        }
        // the status line, the key to the red cross and the phone's button (only the parts that say what the box is, so a slider that is being dragged is never taken out of the page)
        function renderBox() {
            var s = status(), top = pane.top, rest = pane.rest;
            if (phoneStatus) { phoneStatus.textContent = s.text; phoneStatus.className = 'toolnote fb-ps ' + s.cls; }
            if (patBtn) patBtn.querySelector('.fb-pbn').textContent = summary();
            if (!top || !rest) return;
            var line = el('p', 'fb-st ' + s.cls, s.text), key = el('p', 'fb-note'), x = el('span', 'fb-x', '×');
            top.textContent = ''; rest.textContent = '';
            line.setAttribute('role', 'status'); top.appendChild(line);
            key.appendChild(x); key.appendChild(doc.createTextNode(KEY_LINE)); rest.appendChild(key);
        }
        // everything again, with the focus where it was (a control that is made again loses it)
        function render() {
            var at = doc.activeElement, key = at && at.getAttribute ? at.getAttribute('data-k') : null, mark = key ? '[data-k="' + key + '"]' : '';
            if (!shown) return;
            settings(pane, false);
            if (sheet && sheet.isConnected !== false) settings(sheet, true);
            renderBox();
            if (key) { var again1 = (sheet && sheet.querySelector(mark)) || pane.querySelector(mark); if (again1 && again1.focus && at !== again1) again1.focus(); }
        }
        function openSheet() {
            if (!env.dialogs || env.busy()) return;
            env.dialogs.open({
                title: 'Fill a box', cls: 'narrow fbsheet', focus: '.fb-pb[aria-checked="true"]',
                build: function (body) { sheet = el('div', 'fb fbs-body'); settings(sheet, true); body.appendChild(sheet); },
                onClose: function () { sheet = null; },
                buttons: [{ text: 'Done' }]
            });
        }

        // ---- where the pieces are ----
        function place() {
            var side = env.host.side, line = side && side.querySelector('.checkline'), phone = narrow();
            if (side && !phone) side.insertBefore(pane, line || null);
            pane.hidden = !shown || phone;
            if (side && side.classList) side.classList.toggle('fb-on', shown && !phone);
            if (patBtn) patBtn.hidden = !shown || !phone;
            if (phoneStatus) phoneStatus.hidden = !shown || !phone;
        }
        function mount() {
            if (patBtn || !env.host.row) return;
            patBtn = el('button', 'tilebtn fb-patbtn'); patBtn.type = 'button'; patBtn.hidden = true; patBtn.appendChild(el('span', 'fb-pbn', ''));
            patBtn.addEventListener('click', openSheet);
            env.host.row.appendChild(patBtn);
            phoneStatus = el('p', 'toolnote fb-ps'); phoneStatus.hidden = true; phoneStatus.setAttribute('role', 'status');
            env.host.tools.insertBefore(phoneStatus, env.host.hint || null);
            if (query && query.addEventListener) query.addEventListener('change', function () { place(); render(); });
        }

        // ---- drawing: the box, the pattern's cells, the cells that are skipped (a red cross), the ants that the cap leaves out (dashed rings) and the count ----
        function ensureOver() {
            if (over || !env.host.cv || !doc.createElement('canvas').getContext) return;
            over = el('canvas', 'fb-over'); over.setAttribute('aria-hidden', 'true');
            env.host.cv.insertBefore(over, env.host.map.nextSibling);
            badge = el('div', 'fb-badge'); badge.hidden = true;
            env.host.cv.insertBefore(badge, over.nextSibling);
        }
        function ring(ctx, x, y, rad, dashed) {
            ctx.beginPath(); ctx.arc(x, y, rad, 0, 6.2832);
            if (dashed) { ctx.setLineDash([3, 3]); ctx.lineWidth = 1.5; ctx.strokeStyle = 'rgba(244,236,226,.7)'; ctx.stroke(); ctx.setLineDash([]); return; }
            ctx.globalAlpha = 0.75; ctx.fillStyle = COLORS[env.state().team]; ctx.fill();
            ctx.globalAlpha = 0.95; ctx.lineWidth = 1.5; ctx.strokeStyle = '#f4ece2'; ctx.stroke(); ctx.globalAlpha = 1;
        }
        function cross(ctx, x, y, s) {
            var m = Math.max(3, s * 0.28), w = Math.max(3, s * 0.12);
            ctx.lineCap = 'round';
            [[w + 2, 'rgba(0,0,0,.65)'], [w, '#ff7a6b']].forEach(function (pen) {
                ctx.lineWidth = pen[0]; ctx.strokeStyle = pen[1]; ctx.beginPath();
                ctx.moveTo(x - m, y - m); ctx.lineTo(x + m, y + m); ctx.moveTo(x + m, y - m); ctx.lineTo(x - m, y + m); ctx.stroke();
            });
        }
        function drawOver() {
            ensureOver();
            if (!over) return;
            var info = env.view ? env.view() : null, map = env.host.map, ctx = over.getContext('2d');
            if (!ctx || !info || !info.view || !map) return;
            var w = info.width, h = info.height, dpr = Math.min(2, win.devicePixelRatio || 1);
            if (over.width !== Math.round(w * dpr) || over.height !== Math.round(h * dpr)) { over.width = Math.round(w * dpr); over.height = Math.round(h * dpr); }
            over.style.left = map.offsetLeft + 'px'; over.style.top = map.offsetTop + 'px'; over.style.width = w + 'px'; over.style.height = h + 'px';
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, w, h);
            badge.hidden = true;
            var p = st.plan, box = st.box;
            if (!p || !box || !shown) return;
            var v = info.view, s = v.scale, a = View.toScreen(v, w, h, box.r0, box.c0), z = View.toScreen(v, w, h, box.r1 + 1, box.c1 + 1), f = file(), t = p.thing, sp = env.sprites ? env.sprites() : null;
            function seen(q) { return q.x > -s && q.y > -s && q.x < w && q.y < h; }
            ctx.fillStyle = 'rgba(242,194,48,.10)'; ctx.fillRect(a.x, a.y, z.x - a.x, z.y - a.y);
            var id = t.kind === 'pile' ? (t.custom && t.custom.id ? t.custom.id : Blocks.FOOD[t.stages].stages[0][1]) : t.id;
            // (a map seen from far away is drawn with squares: a picture for each of thousands of cells would make the drag stutter)
            var far = s < 6;
            p.cells.forEach(function (c) {
                var q = View.toScreen(v, w, h, c.r, c.c);
                if (!seen(q)) return;
                if (t.kind === 'ant' && !far) ring(ctx, q.x + s / 2, q.y + s / 2, Math.max(2, Math.min(s * 0.36, 14)), false);
                else if (t.kind !== 'ant' && sp && !far) sp.ghost(ctx, v, w, h, id, c.r, c.c, f, t.kind === 'plant' ? 'plant' : (t.kind === 'pile' ? 'pile' : undefined));
                else { ctx.fillStyle = t.kind === 'ant' ? COLORS[env.state().team] : 'rgba(242,194,48,.55)'; ctx.fillRect(q.x + Math.min(2, s / 4), q.y + Math.min(2, s / 4), Math.max(1, s - 2 * Math.min(2, s / 4)), Math.max(1, s - 2 * Math.min(2, s / 4))); }
            });
            p.extra.forEach(function (c) { var q = View.toScreen(v, w, h, c.r, c.c); if (seen(q)) ring(ctx, q.x + s / 2, q.y + s / 2, Math.max(2, Math.min(s * 0.36, 14)), true); });
            p.skipped.forEach(function (c) {
                var q = View.toScreen(v, w, h, c.r, c.c);
                if (!seen(q)) return;
                if (far) { ctx.fillStyle = '#ff7a6b'; ctx.fillRect(q.x, q.y, Math.max(1, s), Math.max(1, s)); } else cross(ctx, q.x + s / 2, q.y + s / 2, s);
            });
            ctx.setLineDash([7, 5]); ctx.lineWidth = 2.5; ctx.strokeStyle = '#000'; ctx.strokeRect(a.x, a.y, z.x - a.x, z.y - a.y);
            ctx.lineWidth = 1.5; ctx.strokeStyle = '#f2c230'; ctx.strokeRect(a.x, a.y, z.x - a.x, z.y - a.y); ctx.setLineDash([]);
            // the count, on the box's lower right corner (above it when there is no room below)
            var first = countText(p), second = '';
            if (p.extra.length) { first = num(p.cells.length) + ' ' + (p.cells.length === 1 ? t.one : t.many) + ' (cap)'; second = num(p.extra.length) + ' left out'; }
            else if (p.skipped.length) second = plural(p.skipped.length, 'cell', 'cells') + ' skipped';
            badge.textContent = '';
            badge.appendChild(el('b', '', first)); if (second) badge.appendChild(el('small', '', second));
            badge.className = 'fb-badge' + (p.extra.length ? ' warn' : '') + (!p.cells.length ? ' none' : '');
            badge.hidden = false;
            var bw = badge.offsetWidth, bh = badge.offsetHeight, bx = Math.min(w - bw - 4, Math.max(4, z.x - bw)), by = z.y + 6;
            if (by + bh > h - 4) by = Math.max(4, a.y - bh - 6);
            badge.style.left = (map.offsetLeft + bx) + 'px'; badge.style.top = (map.offsetTop + by) + 'px';
        }
        // (while a box is there the overlay follows the view: a zoom moves it)
        function follow() {
            loop = 0;
            if (!st.box || !shown) return;
            var info = env.view(), sign = info && info.view ? [info.view.cx, info.view.cy, info.view.scale, info.width, info.height].join(',') : '';
            if (sign !== last) { last = sign; drawOver(); }
            if (win.requestAnimationFrame) loop = win.requestAnimationFrame(follow);
        }
        function watch() { if (!loop && win.requestAnimationFrame && st.box && shown) { last = ''; loop = win.requestAnimationFrame(follow); } }

        // ---- putting it in the map: ONE step of the history ----
        function drop() { st.box = null; st.plan = null; start = null; painting = false; drawOver(); renderBox(); }
        function finish() {
            var p = st.plan, f = file();
            st.box = null; start = null; painting = false;
            if (!p) { drawOver(); renderBox(); return; }
            if (!p.cells.length) { st.said = nothingText(p); st.plan = null; st.kind = ''; drawOver(); renderBox(); return; }
            var change = changeOf(f, p.thing, p.cells);
            if (change.error) { st.plan = null; drawOver(); renderBox(); return env.say('It cannot be put there', change.error); }
            if (!env.act(change)) { st.plan = null; drawOver(); renderBox(); return; }
            var step = env.step ? env.step() : null;
            st.last = { id: step ? step.top : 0, text: countText(p) };
            st.kind = 'put'; st.plan = null;
            st.said = 'Put ' + countText(p) + (p.skipped.length ? ' (' + plural(p.skipped.length, 'cell', 'cells') + ' skipped: ' + skipText(p) + ')' : '') + '. One Undo takes them all away.'
                + (p.extra.length ? ' ' + num(p.extra.length) + ' more did not fit in one fill: drag the same box again for them.' : '');
            drawOver(); render();
        }
        // The history moved (an Undo or a Redo of the fill, or another change): the line in the box says so
        function watchStep() {
            var step = env.step ? env.step() : null;
            if (!st.last || !step) return;
            if (st.kind === 'put' && step.top !== st.last.id && step.redo) { st.kind = 'undone'; st.said = 'Undid the fill of ' + st.last.text + '. One Redo puts them all back.'; }
            else if (st.kind === 'undone' && step.top === st.last.id) { st.kind = 'put'; st.said = 'Put ' + st.last.text + ' back. One Undo takes them all away.'; }
            else if (step.top !== st.last.id && !step.redo) { st.last = null; st.kind = ''; st.said = ''; }
        }

        // ---- the painter that the tools hand the canvas to while the switch is on (mapmaker_tools.js) ----
        function setBox(a, b) {
            st.box = { r0: Math.min(a.row, b.row), c0: Math.min(a.column, b.column), r1: Math.max(a.row, b.row), c1: Math.max(a.column, b.column) };
            setPlan(); renderBox(); drawOver();
        }
        var painter = {
            size: function () { return 1; },
            tapOnly: function () { return false; },
            begin: function (cell) { start = cell; painting = true; st.said = ''; st.kind = ''; st.last = null; setBox(cell, cell); watch(); return true; },
            extend: function (from, to) { if (start) setBox(start, to); },
            // (a finger's tap ends in the same breath that it begins, while the canvas still counts it as a stroke, and the history takes no change then: the fill is made a moment later)
            end: function () { if (!start) return; if (env.stroking && env.stroking() && win.setTimeout) win.setTimeout(finish, 0); else finish(); },
            abandon: function () { drop(); },
            cancel: function () { drop(); },
            key: function (e) { if (e.key === 'Escape' && painting) { if (env.cancel) env.cancel(); drop(); return true; } return false; }
        };

        mount();
        return {
            painter: painter,
            // the switch is on for a tool that it is for (or off): the box and the phone's line are shown, or taken away
            show: function (on) {
                shown = !!on; mount(); place();
                if (!on) drop(); else { st.said = st.kind ? st.said : ''; render(); }
                drawOver(); watch();
            },
            // the map changed (a fill, an Undo, a tap), or the tool's choice did: the plan is made again against the new bytes
            refresh: function () { if (!shown) return; watchStep(); if (st.box && !painting) setPlan(); render(); drawOver(); },
            setMap: function () { st.said = ''; st.last = null; st.kind = ''; drop(); if (shown) render(); },
            state: function () { return { pattern: st.pattern, n: st.n, density: st.density, seed: st.seed, box: st.box, plan: st.plan, said: st.said, kind: st.kind }; },
            // for a test: the box that is dragged, without a mouse
            set: function (o) { for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k) && (k === 'pattern' || k === 'n' || k === 'density' || k === 'seed')) st[k] = o[k]; setPlan(); render(); drawOver(); },
            pane: pane
        };
    }

    return {
        create: create, plan: plan, changeOf: changeOf, norm: norm, inPattern: inPattern, whyNot: whyNot, occupied: occupied, capOf: capOf, countText: countText, skipText: skipText, nothingText: nothingText,
        objectThing: objectThing, plantThing: plantThing, pileThing: pileThing, antThing: antThing, placePlants: placePlants, placePiles: placePiles, placeObjects: placeObjects,
        PATTERNS: PATTERNS, CAP_ANTS: CAP_ANTS, CAP_THINGS: CAP_THINGS, N_MIN: N_MIN, N_MAX: N_MAX, DENSITY_MIN: DENSITY_MIN, DENSITY_MAX: DENSITY_MAX, SEED_MAX: SEED_MAX, BRIDGE: BRIDGE, SWIMMER: SWIMMER
    };
});
