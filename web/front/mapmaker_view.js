// The map maker's canvas: where the map is on the screen (pan and zoom as plain arithmetic) and how it is drawn (docs/MAP_EDITOR.md). The view is the point of the map in the middle of the
// canvas (`cx`, `cy`, in tiles) and the size of a tile on the screen (`scale`, in CSS pixels). Drawing touches only what can be seen: the tiles inside the canvas (one rectangle each), and
// when a tile is smaller than 5 pixels the pixels of the canvas (one lookup each), so a 512 x 512 map costs no more than a small one. A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_tiles.js'));
    else root.MapView = factory(root.MapTiles);
})(this, function (Tiles) {
    'use strict';

    var MAX_SCALE = 48;             // the biggest tile on the screen
    var MARGIN = 8;                 // the space kept round a map that is shown whole
    var GRID_FROM = 12;             // a tile of this many pixels or more is drawn with a thin line round it
    var RECTS_FROM = 5;             // below this the canvas is filled pixel by pixel

    function fitScale(file, w, h) { return Math.max(0.05, Math.min((w - MARGIN) / file.columns, (h - MARGIN) / file.rows)); }
    function minScale(file, w, h) { return Math.min(fitScale(file, w, h), MAX_SCALE); }

    function clamp(view, file, w, h) {
        view.scale = Math.max(minScale(file, w, h), Math.min(MAX_SCALE, view.scale));
        var hw = w / (2 * view.scale), hh = h / (2 * view.scale);
        view.cx = file.columns <= 2 * hw ? file.columns / 2 : Math.max(hw, Math.min(file.columns - hw, view.cx));
        view.cy = file.rows <= 2 * hh ? file.rows / 2 : Math.max(hh, Math.min(file.rows - hh, view.cy));
        return view;
    }

    // The first view of a map: all of it when it is a map of at most 10,000 cells (the size of a Classic map), else a stretch of it round its middle at about 22 pixels a tile
    function create(file, w, h) {
        var fit = fitScale(file, w, h);
        var view = { scale: file.cells <= 10000 ? Math.min(fit, MAX_SCALE) : Math.max(fit, 22), cx: file.columns / 2, cy: file.rows / 2 };
        return clamp(view, file, w, h);
    }

    function isWhole(view, file, w, h) { return file.columns * view.scale <= w + 0.5 && file.rows * view.scale <= h + 0.5; }

    function toScreen(view, w, h, row, column) { return { x: (column - view.cx) * view.scale + w / 2, y: (row - view.cy) * view.scale + h / 2 }; }

    function cellAt(view, file, w, h, x, y) {
        var column = Math.floor((x - w / 2) / view.scale + view.cx), row = Math.floor((y - h / 2) / view.scale + view.cy);
        return row < 0 || column < 0 || row >= file.rows || column >= file.columns ? null : { row: row, column: column };
    }

    // The same map point stays under (x, y) while the scale changes by `factor`
    function zoomAt(view, file, w, h, factor, x, y) {
        var column = (x - w / 2) / view.scale + view.cx, row = (y - h / 2) / view.scale + view.cy;
        view.scale *= factor;
        view.scale = Math.max(minScale(file, w, h), Math.min(MAX_SCALE, view.scale));
        view.cx = column - (x - w / 2) / view.scale;
        view.cy = row - (y - h / 2) / view.scale;
        return clamp(view, file, w, h);
    }

    function panBy(view, file, w, h, dx, dy) {
        view.cx -= dx / view.scale;
        view.cy -= dy / view.scale;
        return clamp(view, file, w, h);
    }

    // Put a cell in the middle (and zoom in to at least `atLeast` pixels a tile, when given)
    function centerOn(view, file, w, h, row, column, atLeast) {
        if (atLeast && view.scale < atLeast) view.scale = atLeast;
        view.cx = Math.max(0, Math.min(file.columns - 1, column)) + 0.5;
        view.cy = Math.max(0, Math.min(file.rows - 1, row)) + 0.5;
        return clamp(view, file, w, h);
    }

    // Keep a cell inside the canvas (the keyboard moves a cursor; the view follows it)
    function reveal(view, file, w, h, row, column) {
        var p = toScreen(view, w, h, row, column), s = view.scale, pad = Math.min(s * 2, 40);
        if (p.x < pad) view.cx -= (pad - p.x) / s; else if (p.x + s > w - pad) view.cx += (p.x + s - (w - pad)) / s;
        if (p.y < pad) view.cy -= (pad - p.y) / s; else if (p.y + s > h - pad) view.cy += (p.y + s - (h - pad)) / s;
        return clamp(view, file, w, h);
    }

    // The tiles that can be seen: first and last row and column, inside the map (null when none)
    function visible(view, file, w, h) {
        var c0 = Math.max(0, Math.floor(view.cx - w / (2 * view.scale))), c1 = Math.min(file.columns - 1, Math.ceil(view.cx + w / (2 * view.scale)) - 1);
        var r0 = Math.max(0, Math.floor(view.cy - h / (2 * view.scale))), r1 = Math.min(file.rows - 1, Math.ceil(view.cy + h / (2 * view.scale)) - 1);
        return c0 > c1 || r0 > r1 ? null : { r0: r0, r1: r1, c0: c0, c1: c1 };
    }

    // The tiles of the map as the first of the two ways: a rectangle for each tile that can be seen. Returns the number of rectangles drawn.
    function drawRects(ctx, view, file, w, h, vis) {
        var s = view.scale, x0 = w / 2 - view.cx * s, y0 = h / 2 - view.cy * s, count = 0, r, c;
        for (r = vis.r0; r <= vis.r1; r++) {
            var ya = Math.round(y0 + r * s), yb = Math.round(y0 + (r + 1) * s);
            for (c = vis.c0; c <= vis.c1; c++) {
                var xa = Math.round(x0 + c * s), xb = Math.round(x0 + (c + 1) * s);
                ctx.fillStyle = Tiles.css(file.tile1(r, c), file.tile2(r, c));
                ctx.fillRect(xa, ya, xb - xa, yb - ya);
                count++;
            }
        }
        if (s >= GRID_FROM) {
            ctx.strokeStyle = 'rgba(0,0,0,.16)'; ctx.lineWidth = 1; ctx.beginPath();
            for (r = vis.r0; r <= vis.r1 + 1; r++) { var gy = Math.round(y0 + r * s) + 0.5; ctx.moveTo(Math.round(x0 + vis.c0 * s), gy); ctx.lineTo(Math.round(x0 + (vis.c1 + 1) * s), gy); }
            for (c = vis.c0; c <= vis.c1 + 1; c++) { var gx = Math.round(x0 + c * s) + 0.5; ctx.moveTo(gx, Math.round(y0 + vis.r0 * s)); ctx.lineTo(gx, Math.round(y0 + (vis.r1 + 1) * s)); }
            ctx.stroke();
        }
        return count;
    }

    // The second way (tiles smaller than 5 pixels): one lookup for each pixel of the canvas, put on the canvas as one picture. `scratch` is a canvas that the caller keeps between frames.
    function drawPixels(ctx, view, file, w, h, scratch) {
        var t = Tiles.packedTables(), b = file.bytes, l1 = file.layer1, l2 = file.layer2, s = view.scale;
        var cw = Math.max(1, Math.ceil(w)), ch = Math.max(1, Math.ceil(h));
        if (scratch.width !== cw || scratch.height !== ch) { scratch.width = cw; scratch.height = ch; }
        var sctx = scratch.getContext('2d'), image = scratch.keptImage;
        if (!image || image.width !== cw || image.height !== ch) image = scratch.keptImage = sctx.createImageData(cw, ch);        // (one picture kept for every frame)
        var px = new Uint32Array(image.data.buffer);
        var x0 = w / 2 - view.cx * s, y0 = h / 2 - view.cy * s, columns = new Int32Array(cw), x, y;
        for (x = 0; x < cw; x++) { var c = Math.floor((x + 0.5 - x0) / s); columns[x] = c < 0 || c >= file.columns ? -1 : c; }
        var back = Tiles.hexToPacked('#0b1612');
        for (y = 0; y < ch; y++) {
            var r = Math.floor((y + 0.5 - y0) / s), row = y * cw;
            if (r < 0 || r >= file.rows) { for (x = 0; x < cw; x++) px[row + x] = back; continue; }
            var base = r * file.columns;
            for (x = 0; x < cw; x++) {
                var cc = columns[x];
                if (cc < 0) { px[row + x] = back; continue; }
                var at = 6 * (base + cc), o = t.objects[b[l2 + at] | (b[l2 + at + 1] << 8)];
                px[row + x] = o !== 0 ? o : t.ground[b[l1 + at] | (b[l1 + at + 1] << 8)];
            }
        }
        sctx.putImageData(image, 0, 0);
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(scratch, 0, 0, cw, ch);
        return cw * ch;
    }

    // The start markers (a dot in the team's color) and the food piles (a gold square): drawn over the tiles when a tile is big enough to hold them
    function drawMarks(ctx, view, file, w, h, vis) {
        var s = view.scale, x0 = w / 2 - view.cx * s, y0 = h / 2 - view.cy * s, i;
        function inside(m) { return m.row >= vis.r0 && m.row <= vis.r1 && m.column >= vis.c0 && m.column <= vis.c1; }
        var pad = Math.max(1, Math.round(s * 0.2)), size = Math.max(2, Math.round(s) - 2 * pad);
        for (i = 0; i < file.food.length; i++) {
            var f = file.food[i];
            if (!inside(f)) continue;
            ctx.fillStyle = Tiles.FOOD; ctx.fillRect(Math.round(x0 + f.column * s) + pad, Math.round(y0 + f.row * s) + pad, size, size);
            ctx.strokeStyle = 'rgba(0,0,0,.55)'; ctx.lineWidth = 1; ctx.strokeRect(Math.round(x0 + f.column * s) + pad + 0.5, Math.round(y0 + f.row * s) + pad + 0.5, size - 1, size - 1);
        }
        for (i = 0; i < file.markers.length; i++) {
            var m = file.markers[i];
            if (!inside(m)) continue;
            ctx.beginPath(); ctx.arc(x0 + (m.column + 0.5) * s, y0 + (m.row + 0.5) * s, Math.max(1.5, s * 0.3), 0, 6.2832);
            ctx.fillStyle = Tiles.TEAMS[m.team]; ctx.fill(); ctx.strokeStyle = '#f4ece2'; ctx.lineWidth = 1.5; ctx.stroke();
        }
    }

    // One frame. `opts`: scratch (a canvas for the pixel way), cursor ({row, column} or null). Returns {rects, pixels}: what was drawn, for the tests.
    function draw(ctx, view, file, w, h, opts) {
        opts = opts || {};
        ctx.fillStyle = '#0b1612'; ctx.fillRect(0, 0, w, h);
        var vis = visible(view, file, w, h), done = { rects: 0, pixels: 0 };
        if (!vis) return done;
        if (view.scale >= RECTS_FROM) {
            done.rects = drawRects(ctx, view, file, w, h, vis);
            if (view.scale >= 6) drawMarks(ctx, view, file, w, h, vis);
        } else done.pixels = drawPixels(ctx, view, file, w, h, opts.scratch || document.createElement('canvas'));
        var cur = opts.cursor;
        if (cur && cur.row >= 0 && cur.column >= 0 && cur.row < file.rows && cur.column < file.columns) {
            var p = toScreen(view, w, h, cur.row, cur.column), side = Math.max(view.scale, 6);
            ctx.strokeStyle = '#000'; ctx.lineWidth = 4; ctx.strokeRect(p.x - 1.5, p.y - 1.5, side + 3, side + 3);
            ctx.strokeStyle = '#f6d058'; ctx.lineWidth = 2.5; ctx.strokeRect(p.x - 1.5, p.y - 1.5, side + 3, side + 3);
        }
        return done;
    }

    // The small map of the whole map (the minimap): a picture of the map at most `width` pixels wide and `height` high (never bigger than one pixel a tile), made once for a file,
    // with the visible part drawn over it for each frame. `makeCanvas(w, h)` makes a canvas.
    function miniSize(file, width, height) {
        var k = Math.min(1, width / file.columns, height / file.rows);
        return { w: Math.max(1, Math.round(file.columns * k)), h: Math.max(1, Math.round(file.rows * k)) };
    }

    function miniBase(file, width, height, makeCanvas) {
        var size = miniSize(file, width, height), canvas = makeCanvas(size.w, size.h), ctx = canvas.getContext('2d');
        drawPixels(ctx, { scale: size.w / file.columns, cx: file.columns / 2, cy: file.rows / 2 }, file, size.w, size.h, makeCanvas(1, 1));
        var k = size.w / file.columns;
        for (var i = 0; i < file.markers.length; i++) {
            var m = file.markers[i], x = Math.round(m.column * k) - 2, y = Math.round(m.row * k) - 2;
            ctx.fillStyle = Tiles.TEAMS[m.team]; ctx.fillRect(x, y, 5, 5);
            ctx.strokeStyle = '#f4ece2'; ctx.lineWidth = 1; ctx.strokeRect(x + 0.5, y + 0.5, 4, 4);
        }
        return canvas;
    }

    // The frame of the visible part over the small map, in the small map's pixels
    function miniFrame(view, file, w, h, miniW, miniH) {
        var kx = miniW / file.columns, ky = miniH / file.rows, hw = w / (2 * view.scale), hh = h / (2 * view.scale);
        return { x: (view.cx - hw) * kx, y: (view.cy - hh) * ky, w: 2 * hw * kx, h: 2 * hh * ky };
    }

    // A click on the small map: the map point it means
    function miniPoint(file, miniWidth, miniHeight, x, y) {
        return { row: Math.max(0, Math.min(file.rows - 1, Math.floor(y / miniHeight * file.rows))), column: Math.max(0, Math.min(file.columns - 1, Math.floor(x / miniWidth * file.columns))) };
    }

    return {
        MAX_SCALE: MAX_SCALE, create: create, clamp: clamp, fitScale: fitScale, minScale: minScale, isWhole: isWhole, toScreen: toScreen, cellAt: cellAt, zoomAt: zoomAt, panBy: panBy,
        centerOn: centerOn, reveal: reveal, visible: visible, draw: draw, miniSize: miniSize, miniBase: miniBase, miniFrame: miniFrame, miniPoint: miniPoint
    };
});
