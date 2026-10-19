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

    var WINDOW = { columns: 48, rows: 32 };               // the most cells that a preview shows (the cells round the first change)
    var NOTE = 'Edge pieces you placed by hand in this area are chosen again too. Where the game has no edge piece (for example water beside gravel, dirt or slate) the plain tile stays. One Undo brings everything back.';

    // Who wears the piece where two kinds meet: only ONE of the two cells gets it (the game's own maps do it that way: 98 in 100 of the boundaries that have a piece have it on one side only), the
    // kind of the higher rank. The order is what those maps show for each pair (gravel, dirt and slate wear it toward mud 94 to 96 times in 100, water 77, gravel toward slate 75, slate toward
    // dirt 88; gravel and dirt split evenly)
    var RANK = { mud: 0, dirt: 1, slate: 2, gravel: 3, water: 4 };

    // The piece for a cell that has the other kind on these orthogonal sides (n, s, w, e): one side makes an edge piece, two sides that meet a corner piece (02 NW, 03 N, 04 NE, 05 W, 06 E,
    // 07 SW, 08 S, 09 SE); a cell with the other kind on opposite sides, on three sides or only at a diagonal gets none (0: the game has no piece for it)
    function shapeOf(n, s, w, e) {
        var sides = n + s + w + e;
        if (sides === 1) return n ? 3 : w ? 5 : e ? 6 : 8;
        if (sides !== 2 || (n && s) || (w && e)) return 0;
        return n ? (w ? 2 : 4) : (w ? 7 : 9);
    }

    // The tile that a cell of layer 1 should have (its own, when it is right or no piece exists), by the kinds of its four sides. The cell wears a piece toward a kind of a lower rank; toward a
    // kind of a higher rank it keeps the piece it has when that is the right one (a piece placed by hand) and is the plain tile otherwise.
    function wanted(kindAt, G, r, c, id) {
        var t = G.info(id);
        if (!t) return id;
        var n = kindAt(r - 1, c), s = kindAt(r + 1, c), w = kindAt(r, c - 1), e = kindAt(r, c + 1), list = G.others(t.kind), i;
        function sides(o) { return (n === o) + (s === o) + (w === o) + (e === o); }
        var best = '', bestCount = 0;
        for (i = 0; i < list.length; i++) {
            var o = list[i], count = RANK[o] < RANK[t.kind] ? sides(o) : 0;
            if (count > bestCount || (count && count === bestCount && (o === t.other || (best !== t.other && RANK[o] < RANK[best])))) { best = o; bestCount = count; }
        }
        var shape = best ? shapeOf(n === best, s === best, w === best, e === best) : 0;
        if (!shape) {
            if (t.other && RANK[t.other] > RANK[t.kind] && shapeOf(n === t.other, s === t.other, w === t.other, e === t.other) === t.shape) return id;      // (a right piece toward a higher kind, placed by hand)
            return t.other ? G.kind(t.kind).plain : id;                              // (a piece that is not right is the plain tile again; a plain look-alike stays)
        }
        if (t.other === best && t.shape === shape) return id;                          // (the right piece, in whichever look-alike it was placed)
        var pieces = G.edges(t.kind, best);
        for (i = 0; i < pieces.length; i++) if (pieces[i].shape === shape) return pieces[i].id || id;
        return id;
    }

    // The cells of `region` ({r0, r1, c0, c1}, both ends in; null: the whole map) that should have another tile of layer 1: {cells: [[index, tile]] in row order, count, region}
    function plan(file, region, G) {
        var rows = file.rows, columns = file.columns, r0 = 0, r1 = rows - 1, c0 = 0, c1 = columns - 1, cells = [], r, c;
        if (region) { r0 = Math.max(0, region.r0); r1 = Math.min(rows - 1, region.r1); c0 = Math.max(0, region.c0); c1 = Math.min(columns - 1, region.c1); }
        function kindAt(row, column) { return row < 0 || column < 0 || row >= rows || column >= columns ? null : G.kindOf(file.tile1(row, column)); }
        for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) {
            var id = file.tile1(r, c), now = wanted(kindAt, G, r, c, id);
            if (now !== id) cells.push([r * columns + c, now]);
        }
        return { cells: cells, count: cells.length, region: { r0: r0, r1: r1, c0: c0, c1: c1 } };
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

    // The cells a preview shows: the region, or the window of WINDOW round the first change when it is bigger
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
            var s = Math.max(2, Math.min(16, Math.floor(480 / win.w), Math.floor(320 / win.h))), ctx = cv.getContext('2d'), sprites = env.sprites && env.sprites(), by = {}, i, r, c;
            cv.width = win.w * s; cv.height = win.h * s;
            if (applied) for (i = 0; i < made.cells.length; i++) by[made.cells[i][0]] = made.cells[i][1];
            ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'high';
            for (r = 0; r < win.h; r++) for (c = 0; c < win.w; c++) {
                var idx = (win.r0 + r) * file.columns + win.c0 + c, id = by[idx] !== undefined ? by[idx] : file.tile1(win.r0 + r, win.c0 + c);
                if (sprites && sprites.ground && sprites.ground(ctx, id, c * s, r * s, s)) continue;
                ctx.fillStyle = Tiles.css(id, File.EMPTY2); ctx.fillRect(c * s, r * s, s, s);
            }
        }

        function show() {
            var m = made(scope), win = windowOf(m, file.columns);
            Object.keys(cards).forEach(function (k) { cards[k].label.classList.toggle('on', k === scope); cards[k].input.checked = k === scope; });
            paint(before, m, win, false); paint(after, m, win, true);
            count.textContent = 'After: ' + (m.count ? plural(m.count, 'cell', 'cells') + (m.count === 1 ? ' changes' : ' change') : 'nothing changes');
            sample.textContent = win.w * win.h < (m.region.r1 - m.region.r0 + 1) * (m.region.c1 - m.region.c0 + 1) ? 'The pictures show ' + win.w + ' by ' + win.h + ' cells of this area, round the first change.' : '';
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

    return { plan: plan, change: change, dictionary: dictionary, open: open, shapeOf: shapeOf, wanted: wanted, RANK: RANK, WINDOW: WINDOW };
});
