// Fix edges (docs/MAP_EDITOR.md, "Fix edges"): the button that puts the game's edge and corner pieces where two kinds of ground meet. The game does not blend the ground by itself: a map file
// must hold the edge tiles (gd03a is gravel with dirt on its north side), so the page picks them from the kinds of the neighbors, with the table of mapmaker_ground.js. `plan` is the rule (a
// pure function of a map's layer 1), `change` makes a CHANGE of mapmaker_edit.js of it (one step of the history: one Undo brings all of it back) and `open` is the dialog with a before and an
// after. Nothing changes what a cell is (its kind, its solid bit, its layer 2): an edge tile walks like the plain tile of its first letter. A lazily loaded, plain ES5 script (a page and node can load it).
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_file.js'), require('./mapmaker_tiles.js'));
    else root.MapEdges = factory(root.MapFile, root.MapTiles);
})(this, function (File, Tiles) {
    'use strict';

    var WINDOW = { columns: 48, rows: 32 };               // the most cells that a preview shows (the cells around the first change)
    var NOTE = 'Edge pieces you placed by hand stay, unless the rule has a better one for that cell. Where the game has no edge piece (for example water beside gravel, dirt or slate) the plain tile stays. One Undo brings everything back.';

    // Who wears the piece where two kinds meet: only ONE of the two cells gets it (the game's own maps do it that way: almost every boundary that has a piece has it on one side only), the kind of
    // the higher rank. The order is what those maps show for each pair (gravel, dirt and slate wear it toward mud 93 to 96 times in 100, water 75, gravel toward slate 75, slate toward dirt 88;
    // gravel and dirt split evenly)
    var RANK = { mud: 0, dirt: 1, slate: 2, gravel: 3, water: 4 };

    // The cells round a stroke's area that its plan works out as well, so that no cell it changes rests on an out-of-date piece of the stroke's earlier steps (none: about 2 random stroke maps in 100
    // end wrong; one was enough for every map tried; three keep a margin)
    var CONTEXT = 3;

    // The piece for a cell that has the other kind on these orthogonal sides (n, s, w, e): one side makes an edge piece, two sides that meet a corner piece (02 NW, 03 N, 04 NE, 05 W, 06 E,
    // 07 SW, 08 S, 09 SE); a cell with the other kind on opposite sides, on three sides or only at a diagonal gets none (0: the game has no piece for it)
    function shapeOf(n, s, w, e) {
        var sides = n + s + w + e;
        if (sides === 1) return n ? 3 : w ? 5 : e ? 6 : 8;
        if (sides !== 2 || (n && s) || (w && e)) return 0;
        return n ? (w ? 2 : 4) : (w ? 7 : 9);
    }

    // The shapes that face a cell from the neighbor on its north, south, west and east side (the neighbor to the north faces south with 7, 8 or 9, and so on)
    var TOWARD = [[7, 8, 9], [2, 3, 4], [4, 6, 9], [2, 5, 7]];

    // The other kind that a cell of kind `kind` has its pieces toward, from the kinds round it alone (no tile is read): the one with the most sides (a tie goes to the lower rank)
    function pick(kindAt, G, r, c, kind) {
        var list = G.others(kind), best = '', bestCount = 0, i;
        for (i = 0; i < list.length; i++) {
            var o = list[i], count = RANK[o] < RANK[kind] ? (kindAt(r - 1, c) === o) + (kindAt(r + 1, c) === o) + (kindAt(r, c - 1) === o) + (kindAt(r, c + 1) === o) : 0;
            if (count > bestCount || (count && count === bestCount && RANK[o] < RANK[best])) { best = o; bestCount = count; }
        }
        return best;
    }
    // the shape that the sides of the kind `best` make round a cell (0: none: opposite sides, three or four sides)
    function rawShape(kindAt, r, c, best) { return best ? shapeOf(kindAt(r - 1, c) === best, kindAt(r + 1, c) === best, kindAt(r, c - 1) === best, kindAt(r, c + 1) === best) : 0; }

    // The tile that a cell of layer 1 gets from the LOWER kinds beside it (the higher kind wears the piece): its own when it is right, when the rule has no answer for it, or when the new tile would
    // walk differently. A piece that the rule cannot place (a wm piece on water beside gravel, a piece toward a kind that is not beside it) is the map maker's choice and stays. `primAt` gives the
    // tile that a cell ENDS with at this step (a neighbor may change its own piece in the same pass).
    function primary(kindAt, G, r, c, id, primAt) {
        var t = G.info(id);
        if (!t) return id;
        var n = kindAt(r - 1, c), s = kindAt(r + 1, c), w = kindAt(r, c - 1), e = kindAt(r, c + 1), best = pick(kindAt, G, r, c, t.kind), i;
        // One boundary, one wearer: the game's own maps never have both facing cells wearing a piece. A side whose neighbor already wears a piece that faces this cell (its other kind is this cell's
        // kind and its shape points here) is already drawn and does not count (only a side that holds a LOWER kind asks, so the asking goes down in rank and stops). A cell with the other kind on
        // opposite sides or on three gets no piece of its own whatever its neighbors wear: the cells beside it wear (`lowered`).
        function worn(dr, dc, d) { var u = G.info(primAt(r + dr, c + dc)); return !!(u && u.other === t.kind && TOWARD[d].indexOf(u.shape) >= 0); }
        var shape = rawShape(kindAt, r, c, best) ? shapeOf(n === best && !worn(-1, 0, 0), s === best && !worn(1, 0, 1), w === best && !worn(0, -1, 2), e === best && !worn(0, 1, 3)) : 0;
        if (!shape || (t.other === best && t.shape === shape)) return id;
        var pieces = G.edges(t.kind, best);
        for (i = 0; i < pieces.length; i++) if (pieces[i].shape === shape) return pieces[i].id && G.terrain(pieces[i].id) === G.terrain(id) ? pieces[i].id : id;
        return id;
    }

    // The cell of the LOWER kind wears the piece where the higher kind cannot (a strip of water one cell wide in mud, an inlet, a lone cell: the higher cell has the lower kind on opposite sides or
    // on three, so no piece fits it, and the mud beside it wears the piece toward it, as the game's own MEDIUM draws its lakes). `p` is the tile that `primary` left here: only a plain tile takes a
    // piece, on the sides that the higher cell does not draw (it draws a side when its sides make a piece for this kind, or when its tile already faces this cell). The answer comes from the kinds
    // and the higher cell's own pieces, never from this cell's, so a second pass finds the same.
    function lowered(kindAt, G, r, c, p, primAt) {
        var t = G.info(p), list = G.others(t.kind), best = '', bestSides = null, bestCount = 0, i, d, at = [[r - 1, c], [r + 1, c], [r, c - 1], [r, c + 1]];
        for (i = 0; i < list.length; i++) {
            var o = list[i], sides = [0, 0, 0, 0], count = 0;
            if (RANK[o] < RANK[t.kind]) continue;
            for (d = 0; d < 4; d++) {
                var nr = at[d][0], nc = at[d][1], u;
                if (kindAt(nr, nc) !== o) continue;
                var x = pick(kindAt, G, nr, nc, o);
                if (x === t.kind && rawShape(kindAt, nr, nc, x)) continue;                      // (its sides make a piece toward this kind)
                u = G.info(primAt(nr, nc));
                if (u && u.other === t.kind && TOWARD[d].indexOf(u.shape) >= 0) continue;     // (it wears a piece that faces this cell already)
                sides[d] = 1; count++;
            }
            if (count > bestCount || (count && count === bestCount && RANK[o] < RANK[best])) { best = o; bestSides = sides; bestCount = count; }
        }
        var shape = best ? shapeOf(bestSides[0], bestSides[1], bestSides[2], bestSides[3]) : 0, pieces = shape ? G.edges(t.kind, best) : [];
        for (i = 0; i < pieces.length; i++) if (pieces[i].shape === shape) return pieces[i].id && G.terrain(pieces[i].id) === G.terrain(p) ? pieces[i].id : p;
        return p;
    }

    // The tile that a cell ends with: what `primary` left, and a plain tile that is still plain gets `lowered`
    function ends(kindAt, G, r, c, p, primAt) {
        var t = G.info(p);
        return t && !t.other ? lowered(kindAt, G, r, c, p, primAt) : p;
    }

    // The cells of `region` ({r0, r1, c0, c1}, both ends in; null: the whole map) that should have another tile of layer 1: {cells: [[index, tile]] in row order, count, region}
    function plan(file, region, G) {
        var rows = file.rows, columns = file.columns, r0 = 0, r1 = rows - 1, c0 = 0, c1 = columns - 1, cells = [], r, c;
        if (region) { r0 = Math.max(0, region.r0); r1 = Math.min(rows - 1, region.r1); c0 = Math.max(0, region.c0); c1 = Math.min(columns - 1, region.c1); }
        function kindAt(row, column) { return row < 0 || column < 0 || row >= rows || column >= columns ? null : G.kindOf(file.tile1(row, column)); }
        // The tile that a cell ends with at the first step: what the rule wants inside the area, its own outside. Worked out once for each cell, on the tiles as they were when the button was
        // pressed (the result does not depend on the order of the scan).
        var prims = new Int32Array(rows * columns).fill(-1);
        function primAt(row, column) {
            if (row < 0 || column < 0 || row >= rows || column >= columns) return file.tile1(row, column);
            var k = row * columns + column;
            if (prims[k] < 0) { var id = file.tile1(row, column); prims[k] = row >= r0 && row <= r1 && column >= c0 && column <= c1 ? primary(kindAt, G, row, column, id, primAt) : id; }
            return prims[k];
        }
        for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) {
            var id = file.tile1(r, c), now = ends(kindAt, G, r, c, primAt(r, c), primAt);
            if (now !== id) cells.push([r * columns + c, now]);
        }
        return { cells: cells, count: cells.length, region: { r0: r0, r1: r1, c0: c0, c1: c1 } };
    }

    // Auto edges on the cells of a ground stroke (`painted` cells painted with `tileId`, and their border `region`): hand-placed tiles outside `painted` stay as they were. The rule is worked out
    // over CONTEXT more cells all round (a piece that the rule made earlier is worked out again there too); only the cells of `region` can change.
    function strokePlan(file, region, G, tileId, painted, beforeTile) {
        var rows = file.rows, columns = file.columns, R0 = Math.max(0, region.r0), R1 = Math.min(rows - 1, region.r1), C0 = Math.max(0, region.c0), C1 = Math.min(columns - 1, region.c1), cells = [], r, c;
        var r0 = Math.max(0, region.r0 - CONTEXT), r1 = Math.min(rows - 1, region.r1 + CONTEXT), c0 = Math.max(0, region.c0 - CONTEXT), c1 = Math.min(columns - 1, region.c1 + CONTEXT);
        var span = c1 - c0 + 1, prims = new Int32Array((r1 - r0 + 1) * span).fill(-1), starts = new Int32Array((r1 - r0 + 1) * span).fill(-1);
        function oldAt(row, col) { return row < 0 || col < 0 || row >= rows || col >= columns ? 0 : beforeTile(row * columns + col); }
        function oldKind(row, col) { return row < 0 || col < 0 || row >= rows || col >= columns ? null : G.kindOf(oldAt(row, col)); }
        function baseAt(row, col) { if (row < 0 || col < 0 || row >= rows || col >= columns) return 0; var k = row * columns + col; return painted[k] ? tileId : beforeTile(k); }
        function kindAt(row, col) { return row < 0 || col < 0 || row >= rows || col >= columns ? null : G.kindOf(baseAt(row, col)); }
        function inArea(row, col) { return row >= r0 && row <= r1 && col >= c0 && col <= c1; }
        // The tile that the rule starts from: the stroke's tile where it painted, the plain tile where the cell is plain or holds a piece that the rule would have put there on the map as it was,
        // the cell's own tile where it is a hand-placed one (a piece the rule would not have put, a look-alike, a tile that is not ground)
        function startAt(row, col) {
            if (row < 0 || col < 0 || row >= rows || col >= columns) return 0;
            var k = row * columns + col, bId = baseAt(row, col);
            if (!inArea(row, col)) return bId;
            var idx = (row - r0) * span + (col - c0);
            if (starts[idx] >= 0) return starts[idx];
            if (painted[k]) return (starts[idx] = tileId);
            var t = G.info(bId), plain = t ? G.kind(t.kind).plain : bId;
            return (starts[idx] = !t || bId === plain || (t.variant === 'a' && ends(oldKind, G, row, col, primary(oldKind, G, row, col, plain, oldAt), oldAt) === bId) ? plain : bId);
        }
        // a cell of the area that the rule may change: painted, or plain at its start
        function editable(row, col) { var t = G.info(baseAt(row, col)); return !!(t && (painted[row * columns + col] || startAt(row, col) === G.kind(t.kind).plain)); }
        function primAt(row, col) {
            if (row < 0 || col < 0 || row >= rows || col >= columns) return 0;
            if (!inArea(row, col)) return baseAt(row, col);
            var idx = (row - r0) * span + (col - c0);
            if (prims[idx] < 0) prims[idx] = editable(row, col) ? primary(kindAt, G, row, col, startAt(row, col), primAt) : baseAt(row, col);
            return prims[idx];
        }
        for (r = R0; r <= R1; r++) for (c = C0; c <= C1; c++) {
            var k = r * columns + c, now = editable(r, c) ? ends(kindAt, G, r, c, primAt(r, c), primAt) : baseAt(r, c);
            if (now !== file.tile1(r, c)) cells.push([k, now]);
        }
        return { cells: cells, count: cells.length, region: { r0: R0, r1: R1, c0: C0, c1: C1 } };
    }

    // The dictionary of the file with the names of the pieces that a plan puts (docs/MAP_FORMAT.md: a tile that the dictionary names "." is not drawn by the original, and a tile outside it is refused):
    // one splice of the whole dictionary, so that Undo takes the names back too and the bytes are exactly the ones before. Null when every piece is named already.
    function dictionary(file, made, G) {
        var count = file.names, b = file.bytes, ids = {}, top = count - 1, i, need = false;
        for (i = 0; i < made.cells.length; i++) {
            var id = made.cells[i][1], slot = file.dictAt + 2 + 11 * id;
            if (ids[id]) continue;
            ids[id] = 1; top = Math.max(top, id);
            if (id >= count || (b[slot] === 0x2E && b[slot + 1] === 0)) need = true;
        }
        if (!need) return null;
        var old = 2 + 11 * count, out = new Uint8Array(2 + 11 * (top + 1)), k, c;
        for (i = 0; i < old; i++) out[i] = b[file.dictAt + i];                  // (a new array: the map's own bytes are only read here)
        for (i = count; i <= top; i++) out[2 + 11 * i] = 0x2E;
        out[0] = top & 255; out[1] = top >> 8;
        Object.keys(ids).forEach(function (key) {
            var id = +key, at = 2 + 11 * id;
            if (id < count && !(b[file.dictAt + at] === 0x2E && b[file.dictAt + at + 1] === 0)) return;
            for (k = 0; k < 11; k++) out[at + k] = 0;
            var name = G.name(id);
            for (c = 0; c < name.length; c++) out[at + c] = name.charCodeAt(c);
        });
        return { abs: true, at: file.dictAt, removed: b.slice(file.dictAt, file.dictAt + old), inserted: out };
    }

    // A CHANGE for Edit.act (mapmaker_edit.js) from a plan: the tiles of layer 1 (their flags, solid bits and layer 2 stay as they are) and the dictionary's names for them (a splice)
    function change(file, made, G) {
        var splice = dictionary(file, made, G);
        return { names: [], cells: [], solid: [], splices: splice ? [splice] : [], ground: made.cells };
    }

    // ---- the dialog ----

    function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }

    // The cells a preview shows: the region, or the window of WINDOW around the first change when it is bigger
    function windowOf(made, columns) {
        var g = made.region, w = Math.min(WINDOW.columns, g.c1 - g.c0 + 1), h = Math.min(WINDOW.rows, g.r1 - g.r0 + 1);
        var mid = made.count ? made.cells[0][0] : 0, row = made.count ? (mid / columns) | 0 : (g.r0 + g.r1) >> 1, col = made.count ? mid - row * columns : (g.c0 + g.c1) >> 1;
        var r0 = Math.max(g.r0, Math.min(g.r1 - h + 1, row - (h >> 1))), c0 = Math.max(g.c0, Math.min(g.c1 - w + 1, col - (w >> 1)));
        return { r0: r0, c0: c0, w: w, h: h };
    }

    // env: {doc, dialogs (MapDialog.create), ground (MapGround), file() -> the open map's reader view, visible() -> {r0, r1, c0, c1} or null, sprites() -> MapSprites (ready) or null,
    //       apply(change) -> true when it was made (one step of the history)}
    function open(env) {
        var doc = env.doc, G = env.ground, file = env.file(), vis = env.visible(), plans = {}, cards = {}, scope = vis ? 'part' : 'all', ctl, count, sample, before, after, shown;
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function made(which) { return plans[which] || (plans[which] = plan(file, which === 'part' ? vis : null, G)); }

        function paint(cv, made, win, applied) {
            var s = Math.max(2, Math.min(16, Math.floor(480 / win.w), Math.floor(320 / win.h))), ctx = cv.getContext('2d'), sprites = env.sprites && env.sprites(), tab = Tiles.tableOf(file), by = {}, i, r, c;          // (the table of the dictionary once, not for each tile)
            cv.width = win.w * s; cv.height = win.h * s;
            if (applied) for (i = 0; i < made.cells.length; i++) by[made.cells[i][0]] = made.cells[i][1];
            ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'high';
            for (r = 0; r < win.h; r++) for (c = 0; c < win.w; c++) {
                var idx = (win.r0 + r) * file.columns + win.c0 + c, id = by[idx] !== undefined ? by[idx] : file.tile1(win.r0 + r, win.c0 + c);
                if (sprites && sprites.ground && sprites.ground(ctx, id, c * s, r * s, s, tab, by[idx] !== undefined)) continue;
                ctx.fillStyle = Tiles.css(id, File.EMPTY2); ctx.fillRect(c * s, r * s, s, s);
            }
        }

        function show() {
            var m = made(scope), win = windowOf(m, file.columns);
            Object.keys(cards).forEach(function (k) { cards[k].label.classList.toggle('on', k === scope); cards[k].input.checked = k === scope; });
            paint(before, m, win, false); paint(after, m, win, true);
            count.textContent = 'After: ' + (m.count ? plural(m.count, 'cell', 'cells') + (m.count === 1 ? ' changes' : ' change') : 'nothing changes');
            sample.textContent = win.w * win.h < (m.region.r1 - m.region.r0 + 1) * (m.region.c1 - m.region.c0 + 1) ? 'The pictures show ' + win.w + ' by ' + win.h + ' cells of this area, around the first change.' : '';
            sample.hidden = !sample.textContent;
            ctl.setDisabled('fix', !m.count);
            shown = m;
        }

        function card(which, title, line) {
            var label = el('label', 'kind'), input = el('input');
            input.type = 'radio'; input.name = 'scope'; input.value = which;
            label.appendChild(input); label.appendChild(el('span', 'rb')).setAttribute('aria-hidden', 'true');
            label.appendChild(el('span', 'kt', title)); label.appendChild(el('span', 'kl', line));
            if (which === 'part' && !vis) { label.className += ' off'; input.disabled = true; }
            input.addEventListener('change', function () { if (input.checked) { scope = which; show(); } });
            cards[which] = { label: label, input: input };
            return label;
        }

        ctl = env.dialogs.open({
            title: 'Fix edges', cls: 'edges',
            build: function (body, c) {
                ctl = c;
                body.appendChild(el('p', 'lead', 'Put the right edge and corner pieces where two kinds of ground meet.'));
                var kinds = el('fieldset', 'kinds stacked');
                kinds.setAttribute('aria-label', 'Where to fix the edges');
                kinds.appendChild(card('part', 'The part I can see', 'What is on the screen now.'));
                kinds.appendChild(card('all', 'The whole map', 'Every cell of the map.'));
                body.appendChild(kinds);
                var pair = el('div', 'edpair'), a = el('figure', 'edfig'), b = el('figure', 'edfig');
                before = el('canvas'); after = el('canvas');
                before.setAttribute('role', 'img'); before.setAttribute('aria-label', 'The area before'); after.setAttribute('role', 'img'); after.setAttribute('aria-label', 'The area after');
                count = el('figcaption', '');
                a.appendChild(before); a.appendChild(el('figcaption', '', 'Before')); b.appendChild(after); b.appendChild(count);
                pair.appendChild(a); pair.appendChild(b);
                body.appendChild(pair);
                sample = el('p', 'edsample'); body.appendChild(sample);
                body.appendChild(el('p', 'ednote', NOTE));
                show();
            },
            buttons: [
                { id: 'cancel', text: 'Cancel', ghost: true },
                { id: 'fix', text: 'Fix edges', onClick: function () { if (shown && shown.count) env.apply(change(file, shown, G)); } }
            ],
            focus: 'input:checked'
        });
        ctl.setDisabled('fix', !shown.count);                              // (the buttons come after the body is built)
        return ctl;
    }

    return { plan: plan, strokePlan: strokePlan, change: change, dictionary: dictionary, open: open, shapeOf: shapeOf, primary: primary, lowered: lowered, RANK: RANK, WINDOW: WINDOW };
});
