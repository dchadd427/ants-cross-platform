// The game's own pictures on the map maker's canvas (docs/MAP_EDITOR.md, "Sprites"). The atlas (mapmaker_atlas/: ground.png, objects.png and atlas.json, made by tools/map_atlas.py when the
// web image is built from the public decoded sprites and the animation table) is fetched when the editor opens; this script loads it, draws with it, and says when it is ready. A cell is a
// 32 pixel picture: the picture of a tile id is frame 0 of its animation drawn at the cell's top-left corner plus the frame's own x/y offset. The ground is drawn first, from a layer kept
// between frames with the objects on it (a pan moves it and paints only the strip that came into view; a stroke repaints only the part round its cells; one scaled copy of the 239 tiles
// for each zoom); the objects are drawn only from their anchor cells (the flagged ones), in row order, as the game draws them; then, over the layer, the starting ants. From 10 pixels a cell up the pictures are drawn; below that the canvas keeps its
// flat colors (mapmaker_view.js), which are the pictures' average colors once the atlas is in (mapmaker_tiles.js). A few big toys are not in the sheet: each is fetched alone from
// /asset_catalog/sprites/ the first time a map shows it. A browser script (it needs a document); a failed atlas leaves the page with the flat colors.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_tiles.js'));
    else root.MapSprites = factory(root.MapTiles);
})(this, function (Tiles) {
    'use strict';

    var CELL = 32, EMPTY2 = 0x7FFE, FROM = 10, ANT_FROM = 12, REACH = 8, BACK = '#0b1612', MAX_CACHED = 3, MAX_DIRTY = 60000, SETTLE = 140;

    // env: {doc, win, base ('mapmaker_atlas/'), far ('/asset_catalog/sprites/'), onChange() (a picture arrived: draw again), onReady(), onFail()}
    function create(env) {
        var doc = env.doc, win = env.win, base = env.base || 'mapmaker_atlas/', farBase = env.far || '/asset_catalog/sprites/';
        var index = null, groundImg = null, objectsImg = null, ready = false, failed = false, farImgs = {}, tiles = [];
        var layer = null, state = null, pending = [], everything = true, later = 0, pics = {}, picCount = 0;

        function el(tag) { return doc.createElement(tag); }

        function image(url, done, fail) {
            var img = new win.Image();
            img.onload = function () { done(img); };
            img.onerror = fail;
            img.src = url;
            return img;
        }

        function fail() { if (failed) return; failed = true; ready = false; if (env.onFail) env.onFail(); }

        // Fetch the index and the two sheets; the average colors replace the flat ones when all three are in
        function load() {
            if (!win.fetch) return fail();
            win.fetch(base + 'atlas.json').then(function (r) { if (!r.ok) throw new Error('atlas'); return r.json(); }).then(function (json) {
                if (!json || json.v !== 1 || !json.ground || !json.objects) throw new Error('atlas');
                var waiting = 2;
                function one() { if (--waiting) return; index = json; objectsAvg(); ready = true; everything = true; if (env.onReady) env.onReady(); }
                groundImg = image(base + json.ground.png, one, fail);
                objectsImg = image(base + json.objects.png, one, fail);
            }).then(null, fail);
        }

        function objectsAvg() {
            var avg = {}, id;
            for (id in index.objects.items) if (index.objects.items.hasOwnProperty(id)) avg[id] = index.objects.items[id][6];
            for (id in index.far) if (index.far.hasOwnProperty(id)) avg[id] = index.far[id][3];
            Tiles.setAverages(index.ground.avg, avg);
        }

        // ---- the ground ----
        // the 239 tiles scaled to t pixels each, in the sheet's own 16 columns, each made the first time it is needed: one copy for each zoom that was seen lately
        function scaled(t) {
            for (var i = 0; i < tiles.length; i++) if (tiles[i].t === t) { var hit = tiles.splice(i, 1)[0]; tiles.push(hit); return hit; }
            var g = index.ground, c = el('canvas'), entry = { t: t, canvas: c, done: new Uint8Array(g.count) };
            c.width = g.columns * t; c.height = Math.ceil(g.count / g.columns) * t;
            tiles.push(entry);
            if (tiles.length > MAX_CACHED) tiles.shift();
            return entry;
        }
        function prepare(entry, k) {
            var g = index.ground, ctx = entry.canvas.getContext('2d'), t = entry.t;
            ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'high';
            ctx.drawImage(groundImg, (k % g.columns) * CELL, ((k / g.columns) | 0) * CELL, CELL, CELL, (k % g.columns) * t, ((k / g.columns) | 0) * t, t, t);
            entry.done[k] = 1;
        }

        function groundCell(lctx, file, st, sheet, r, c) {
            var b = file.bytes, id = b[file.layer1 + 6 * (r * file.columns + c)] | (b[file.layer1 + 6 * (r * file.columns + c) + 1] << 8), g = index.ground, k = id - g.first;
            var xa = st.ox + Math.round(c * st.S), xb = st.ox + Math.round((c + 1) * st.S), ya = st.oy + Math.round(r * st.S), yb = st.oy + Math.round((r + 1) * st.S);
            if (k >= 0 && k < g.count) {
                if (sheet) { if (!sheet.done[k]) prepare(sheet, k); lctx.drawImage(sheet.canvas, (k % g.columns) * st.T, ((k / g.columns) | 0) * st.T, st.T, st.T, xa, ya, xb - xa, yb - ya); }
                else lctx.drawImage(groundImg, (k % g.columns) * CELL, ((k / g.columns) | 0) * CELL, CELL, CELL, xa, ya, xb - xa, yb - ya);
            } else { lctx.fillStyle = Tiles.css(id, EMPTY2); lctx.fillRect(xa, ya, xb - xa, yb - ya); }
        }

        // The pixel rectangle (x, y, w, h) of the layer, painted again: the ground of its cells, then the objects that reach into it
        function paintRect(lctx, file, st, sheet, vis, x, y, w, h) {
            lctx.save(); lctx.beginPath(); lctx.rect(x, y, w, h); lctx.clip();          // (the cells and pictures are cut at the rectangle: nothing outside it is touched, so the objects beside it stay whole)
            lctx.fillStyle = BACK; lctx.fillRect(x, y, w, h);
            var r0 = Math.max(vis.r0, Math.floor((y - st.oy) / st.S) - 1), r1 = Math.min(vis.r1, Math.ceil((y + h - st.oy) / st.S) + 1);
            var c0 = Math.max(vis.c0, Math.floor((x - st.ox) / st.S) - 1), c1 = Math.min(vis.c1, Math.ceil((x + w - st.ox) / st.S) + 1);
            for (var r = r0; r <= r1; r++) for (var c = c0; c <= c1; c++) groundCell(lctx, file, st, sheet, r, c);
            objects(lctx, file, st, x, y, w, h);
            lctx.restore();
        }

        // The cells that changed (and the pictures that reach beyond them): their bounding box with a margin, painted again
        function mend(lctx, file, st, sheet, vis, lw, lh) {
            var r0 = 1e9, r1 = -1, c0 = 1e9, c1 = -1;
            for (var i = 0; i < pending.length; i++) {
                var r = (pending[i] / file.columns) | 0, c = pending[i] - r * file.columns;
                if (r < r0) r0 = r; if (r > r1) r1 = r; if (c < c0) c0 = c; if (c > c1) c1 = c;
            }
            if (r1 < vis.r0 - REACH || r0 > vis.r1 + REACH || c1 < vis.c0 - REACH || c0 > vis.c1 + REACH) return;
            var x = Math.max(0, st.ox + Math.round((c0 - REACH) * st.S)), y = Math.max(0, st.oy + Math.round((r0 - REACH) * st.S));
            var xe = Math.min(lw, st.ox + Math.round((c1 + 1 + REACH) * st.S)), ye = Math.min(lh, st.oy + Math.round((r1 + 1 + REACH) * st.S));
            if (xe > x && ye > y) paintRect(lctx, file, st, sheet, vis, x, y, xe - x, ye - y);
        }

        // Bring the layer up to date for this view: whole when the map, the zoom or the size changed (or it is the first time), else moved and mended
        function groundLayer(file, view, w, h, dpr, vis) {
            var lw = Math.max(1, Math.round(w * dpr)), lh = Math.max(1, Math.round(h * dpr)), S = view.scale * dpr, T = Math.max(1, Math.round(S));
            var st = { file: file, S: S, T: T, lw: lw, lh: lh, ox: Math.round((w / 2 - view.cx * view.scale) * dpr), oy: Math.round((h / 2 - view.cy * view.scale) * dpr) };
            if (!layer) layer = el('canvas');
            if (state && state.S !== S) { pics = {}; picCount = 0; }
            if (layer.width !== lw || layer.height !== lh) { layer.width = lw; layer.height = lh; state = null; }
            var lctx = layer.getContext('2d'), sheet = T <= 64 ? scaled(T) : null;
            lctx.imageSmoothingEnabled = T !== CELL;
            var dx = state ? st.ox - state.ox : 0, dy = state ? st.oy - state.oy : 0;
            if (everything || !state || state.file !== file || state.S !== S || Math.abs(dx) >= lw || Math.abs(dy) >= lh) {
                paintRect(lctx, file, st, sheet, vis, 0, 0, lw, lh);
            } else {
                if (dx || dy) {
                    lctx.imageSmoothingEnabled = false;
                    lctx.drawImage(layer, dx, dy);
                    lctx.imageSmoothingEnabled = T !== CELL;
                    if (dx) paintRect(lctx, file, st, sheet, vis, dx > 0 ? 0 : lw + dx, 0, Math.abs(dx), lh);
                    if (dy) paintRect(lctx, file, st, sheet, vis, 0, dy > 0 ? 0 : lh + dy, lw, Math.abs(dy));
                }
                if (pending.length) mend(lctx, file, st, sheet, vis, lw, lh);
            }
            pending = []; everything = false; st.at = win.performance ? win.performance.now() : 0; state = st;
        }

        // ---- the objects ----
        function farImage(id) {
            var f = farImgs[id];
            if (f !== undefined) return f && f.width ? f : null;
            var entry = index.far[id];
            if (!entry) { farImgs[id] = 0; return null; }
            farImgs[id] = image(farBase + 'sprite_' + ('000' + entry[0]).slice(-4) + '.png', function () { everything = true; if (env.onChange) env.onChange(); }, function () { farImgs[id] = 0; });
            return null;
        }

        // A picture scaled once to iw x ih pixels (kept until the zoom changes): drawn after that without scaling, so where it lands changes none of its pixels
        function scaledPicture(key, img, sx, sy, sw, sh, iw, ih) {
            var id = key + ':' + iw + 'x' + ih, c = pics[id];
            if (c) return c;
            if (picCount > 400) { pics = {}; picCount = 0; }
            c = el('canvas'); c.width = iw; c.height = ih;
            var cx = c.getContext('2d'); cx.imageSmoothingEnabled = true; cx.imageSmoothingQuality = 'medium';
            cx.drawImage(img, sx, sy, sw, sh, 0, 0, iw, ih);
            pics[id] = c; picCount++;
            return c;
        }

        // The anchored objects whose pictures reach into the pixel rectangle (x, y, w, h) of the layer, in row order: from the cells of the rectangle and a margin round them (a picture reaches beyond its cell)
        function objects(lctx, file, st, x, y, w, h) {
            function u(o) { return b[o] | (b[o + 1] << 8); }
            var S = st.S, k = S / CELL, b = file.bytes, items = index.objects.items, far = index.far, food = {}, a = file.blocksAt, n, i, m, s;
            var r0 = Math.max(0, Math.floor((y - st.oy) / S) - REACH), r1 = Math.min(file.rows - 1, Math.ceil((y + h - st.oy) / S) + REACH);
            var c0 = Math.max(0, Math.floor((x - st.ox) / S) - REACH), c1 = Math.min(file.columns - 1, Math.ceil((x + w - st.ox) / S) + REACH);
            if ((a += 2 + 6 * u(a)) + 2 <= b.length) for (n = u(a), a += 2, i = 0; i < n && (m = u(a + 8)) && a + 10 + 4 * m <= b.length; i++, a += 10 + 4 * m)
                for (s = 0; s < m; s++) if (u(a + 4) <= u(a + 10 + 4 * s)) food[u(a) * file.columns + u(a + 2)] = u(a + 12 + 4 * s);
            lctx.save(); lctx.beginPath(); lctx.rect(x, y, w, h); lctx.clip(); lctx.imageSmoothingEnabled = true; lctx.imageSmoothingQuality = 'medium';
            for (var r = r0; r <= r1; r++) {
                var at = file.layer2 + 6 * (r * file.columns + c0);
                for (var c = c0; c <= c1; c++, at += 6) {
                    var id = u(at), ft = food[r * file.columns + c];
                    if (id === EMPTY2 && ft) id = ft; else if (!(b[at + 2] & 1)) continue;
                    if (id === EMPTY2) continue;
                    var it = items[id], dx, dy, iw, ih, img, sx = 0, sy = 0, sw, sh;
                    if (it) { img = objectsImg; sx = it[0]; sy = it[1]; sw = it[2]; sh = it[3]; dx = st.ox + c * S + it[4] * k; dy = st.oy + r * S + it[5] * k; }
                    else if (far[id]) { img = farImage(id); if (!img) continue; sw = img.width; sh = img.height; dx = st.ox + c * S + far[id][1] * k; dy = st.oy + r * S + far[id][2] * k; }
                    else continue;
                    iw = Math.max(1, Math.round(sw * k)); ih = Math.max(1, Math.round(sh * k)); dx = Math.round(dx); dy = Math.round(dy);
                    if (dx > x + w || dy > y + h || dx + iw < x || dy + ih < y) continue;
                    lctx.drawImage(scaledPicture(it ? id : 'f' + id, img, sx, sy, sw, sh, iw, ih), dx, dy);          // (a picture of whole pixels at whole pixels: moved by whole pixels it is the same picture, so the kept layer equals a frame made from nothing)
                }
            }
            if (file.plants) for (i = 0; i < file.plants.length; i++) {
                var p = file.plants[i], pit = items[p.id];
                if (!pit || p.row < r0 || p.row > r1 || p.column < c0 || p.column > c1) continue;
                iw = Math.max(1, Math.round(pit[2] * k)); ih = Math.max(1, Math.round(pit[3] * k));
                dx = Math.round(st.ox + (p.column + 0.5) * S + pit[4] * k); dy = Math.round(st.oy + (p.row + 0.5) * S + pit[5] * k);
                if (dx > x + w || dy > y + h || dx + iw < x || dy + ih < y) continue;
                lctx.drawImage(scaledPicture(p.id, objectsImg, pit[0], pit[1], pit[2], pit[3], iw, ih), dx, dy);
            }
            lctx.restore();
        }

        // One object's picture at a cell, see-through (the ghost of a bigger object or plant that follows the pointer)
        function ghost(ctx, view, w, h, id, row, column) {
            var it = index.objects.items[id], s = view.scale, k = s / CELL, half = (id >= 404 && id <= 421 && id !== 409 && id !== 417 && id !== 418) ? 0.5 : 0;
            if (!it) return;
            ctx.save(); ctx.globalAlpha = 0.62; ctx.imageSmoothingEnabled = true;
            ctx.drawImage(objectsImg, it[0], it[1], it[2], it[3], w / 2 + (column + half - view.cx) * s + it[4] * k, h / 2 + (row + half - view.cy) * s + it[5] * k, it[2] * k, it[3] * k);
            ctx.restore();
        }

        // The starting ants: the game's standing worker ant in its team's color on its cell center, with a ring in the team's color at its feet (black has a thin white edge)
        function ants(ctx, view, file, w, h, vis) {
            var it = index.objects.items[index.ant], teamAnts = index.ants, s = view.scale, k = s / CELL, x0 = w / 2 - view.cx * s, y0 = h / 2 - view.cy * s;
            if (!it) return;
            ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'medium';
            for (var i = 0; i < file.markers.length; i++) {
                var m = file.markers[i];
                if (m.row < vis.r0 - 2 || m.row > vis.r1 + 2 || m.column < vis.c0 - 2 || m.column > vis.c1 + 2) continue;
                var cx = x0 + (m.column + 0.5) * s, ay = y0 + (m.row + 0.5) * s, cy = ay + 11 * k, rx = 12 * k, ry = 5.5 * k, line = Math.max(1.6, 2.4 * k), src = teamAnts && teamAnts[m.team] || it;
                ctx.beginPath(); ctx.ellipse(cx, cy, rx, ry, 0, 0, 6.2832);
                ctx.fillStyle = Tiles.TEAMS[m.team]; ctx.globalAlpha = 0.28; ctx.fill(); ctx.globalAlpha = 1;
                if (m.team === 'black') { ctx.strokeStyle = '#f4ece2'; ctx.lineWidth = line + 2; ctx.stroke(); }
                ctx.strokeStyle = Tiles.TEAMS[m.team]; ctx.lineWidth = line; ctx.stroke();
                ctx.drawImage(objectsImg, src[0], src[1], it[2], it[3], cx + it[4] * k, ay + it[5] * k, it[2] * k, it[3] * k);
            }
        }

        // One frame of the map's tiles in sprite mode (the caller has painted the background and draws the cursor over it). opts: {dpr, ghost: {id, row, column} or null}
        function draw(ctx, view, file, w, h, vis, opts) {
            var dpr = opts.dpr || 1, now = win.performance ? win.performance.now() : 0, f = state ? view.scale * dpr / state.S : 1;
            ctx.save(); ctx.setTransform(1, 0, 0, 1, 0, 0);
            if (state && state.file === file && !everything && !pending.length && f !== 1 && f > 0.75 && f < 1.33 && now - state.at < SETTLE && layer.width === Math.round(w * dpr) && layer.height === Math.round(h * dpr)) {
                // while the zoom keeps changing the last whole picture of the ground and objects is stretched (a frame that is cheap), and a crisp one is made when it rests
                ctx.imageSmoothingEnabled = true;
                ctx.drawImage(layer, 0, 0, layer.width, layer.height, Math.round((w / 2 - view.cx * view.scale) * dpr) - state.ox * f, Math.round((h / 2 - view.cy * view.scale) * dpr) - state.oy * f, layer.width * f, layer.height * f);
                if (!later) later = win.setTimeout(function () { later = 0; if (env.onChange) env.onChange(); }, SETTLE + 10);
            } else {
                groundLayer(file, view, w, h, dpr, vis);
                ctx.drawImage(layer, 0, 0);
            }
            ctx.restore();
            if (view.scale >= ANT_FROM) ants(ctx, view, file, w, h, vis);
            if (opts.ghost) ghost(ctx, view, w, h, opts.ghost.id, opts.ghost.row, opts.ghost.column);
        }

        // A picture in a chip: a canvas of px x px (CSS pixels) put in `node`, the tile's picture scaled to fit (layer 1: a ground tile, layer 2: an object of the sheet). False when there is none.
        function chip(node, layer2, id, px) {
            if (!ready) return false;
            var dpr = win.devicePixelRatio || 1, side = Math.round(px * dpr), cv = el('canvas'), ctx, g = index.ground, it, sc;
            cv.width = cv.height = side; cv.style.width = cv.style.height = px + 'px';
            ctx = cv.getContext('2d'); ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'high';
            if (!layer2) {
                var k = id - g.first;
                if (k < 0 || k >= g.count) return false;
                ctx.drawImage(groundImg, (k % g.columns) * CELL, ((k / g.columns) | 0) * CELL, CELL, CELL, 0, 0, side, side);
            } else {
                it = index.objects.items[id];
                if (!it) return false;
                sc = Math.min(side / it[2], side / it[3], 1.5 * dpr);
                ctx.drawImage(objectsImg, it[0], it[1], it[2], it[3], (side - it[2] * sc) / 2, (side - it[3] * sc) / 2, it[2] * sc, it[3] * sc);
            }
            node.textContent = ''; node.style.background = 'none'; node.style.border = '0';
            node.appendChild(cv);
            return true;
        }

        return {
            load: load, FROM: FROM, ANT_FROM: ANT_FROM, draw: draw, chip: chip,
            ready: function () { return ready; }, failed: function () { return failed; },
            // A ground tile's picture drawn s by s pixels at (x, y) of a canvas (the before and after of Fix edges: mapmaker_edges.js); false when there is none
            ground: function (ctx, id, x, y, s) {
                var g = index && index.ground, k = g ? id - g.first : -1;
                if (!ready || k < 0 || k >= g.count) return false;
                ctx.drawImage(groundImg, (k % g.columns) * CELL, ((k / g.columns) | 0) * CELL, CELL, CELL, x, y, s, s);
                return true;
            },
            // cells (indexes, row * columns + column) that changed: the ground layer paints them again in the next frame
            invalidate: function (list) { if (pending.length + list.length > MAX_DIRTY) { everything = true; pending = []; return; } for (var i = 0; i < list.length; i++) pending.push(list[i]); },
            // everything must be painted again (another map, other bytes)
            reset: function () { everything = true; pending = []; }
        };
    }

    return { create: create, FROM: FROM, ANT_FROM: ANT_FROM };
});
