// The map maker's canvas in the page (docs/MAP_EDITOR.md): it keeps the view of one map (web/front/mapmaker_view.js does the arithmetic and the drawing) and answers the person's hands: a mouse
// drags and its wheel zooms, one finger drags and two fingers pinch, the keyboard moves a marked square (arrows), zooms (plus, minus) and shows the whole map (zero). The small map of the
// whole map (shown while only a part is on the screen) is a way to look somewhere else. With a painter (web/front/mapmaker_tools.js) a mouse or one finger paints instead: a press, a drag of
// cells in a line, a lift; two fingers still pan and zoom, and the middle button, the space key held down, or the Move tool pan. After a stroke only the cells that it changed (and the ones
// the marked square left and came to) are painted again, never the whole frame. A browser script (it needs a document).
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_view.js'));
    else root.MapCanvas = factory(root.MapView);
})(this, function (View) {
    'use strict';

    var STEP = 1.5;                 // what a press of + or - zooms by
    var TAP = 6;                    // a finger or a mouse that moves less than this many pixels is a tap, not a drag
    var MANY = 3000;                // more cells than this to paint again: the whole frame is painted instead
    var SMALL_TILE = 5;             // below this many pixels a tile the canvas is filled pixel by pixel (mapmaker_view.js)

    // env: {doc, win, canvas, minimap (the box), mini (its canvas), miniCap, where (the status line), zoomIn, zoomOut (the buttons), onCursor(cell)}
    function create(env) {
        var doc = env.doc, win = env.win, canvas = env.canvas, file = null, view = null, cursor = null, hover = null, w = 0, h = 0, dpr = 1, frame = 0;
        var scratch = doc.createElement('canvas'), mini = null, pointers = {}, drag = null, pinch = null;
        var painter = null, stroke = null, pending = null, spaceHeld = false, full = null, dirty = [], lastRect = null;

        function makeCanvas(cw, ch) { var c = doc.createElement('canvas'); c.width = cw; c.height = ch; return c; }

        function measure() {
            var box = canvas.getBoundingClientRect();
            w = Math.max(1, Math.round(box.width)); h = Math.max(1, Math.round(box.height));
            dpr = Math.min(3, win.devicePixelRatio || 1, Math.sqrt(16000000 / (w * h)));       // (at most 16 million pixels behind the canvas)
            canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr);
            full = null;                                                    // (a canvas that changed size is empty: the next frame is a whole one)
        }

        function chrome() {
            var cell = hover || cursor;
            env.where.textContent = cell ? 'Row ' + cell.row + ', column ' + cell.column : (file && View.isWhole(view, file, w, h) ? 'Whole map' : '');
            if (!file) return;
            canvas.setAttribute('data-scale', view.scale.toFixed(2));          // (what a test reads: the size of a tile in pixels, and the map point in the middle)
            canvas.setAttribute('data-center', Math.round(view.cy) + ',' + Math.round(view.cx));
            env.zoomIn.disabled = view.scale >= View.MAX_SCALE - 0.001;
            env.zoomOut.disabled = view.scale <= View.minScale(file, w, h) + 0.001;
            var partial = !View.isWhole(view, file, w, h);
            env.minimap.hidden = !partial;
            if (!partial || !mini) return;
            var m = env.mini, ctx = m.getContext('2d');
            if (m.width !== mini.width || m.height !== mini.height) { m.width = mini.width; m.height = mini.height; }
            ctx.drawImage(mini, 0, 0);
            var f = View.miniFrame(view, file, w, h, mini.width, mini.height);
            ctx.strokeStyle = '#000'; ctx.lineWidth = 3; ctx.strokeRect(f.x, f.y, f.w, f.h);
            ctx.strokeStyle = '#fff'; ctx.lineWidth = 1.5; ctx.strokeRect(f.x, f.y, f.w, f.h);
        }

        function brushNow() { return painter ? painter.size() : 0; }
        function markedCell() { return brushNow() > 0 ? (hover || cursor) : cursor; }          // (a tool that paints shows the brush where the mouse is)

        // The whole frame can be skipped for a few cells only when the picture is the one of the last whole frame: the same view, the same size, the same map
        function sameAsFull() {
            return !!full && full.file === file && full.cx === view.cx && full.cy === view.cy && full.scale === view.scale && full.w === w && full.h === h && full.dpr === dpr;
        }

        // The cells to paint again after the last frame: the ones that changed, and those under the marked square before and after (its lines reach a little past it)
        function cellsToPaint(rect) {
            var set = {}, list = [], margin = view.scale >= SMALL_TILE ? 1 : Math.ceil(6 / view.scale) + 1, i;
            function add(idx) { if (!set[idx]) { set[idx] = 1; list.push(idx); } }
            function addRect(a) {
                if (!a) return;
                for (var r = Math.max(0, a.r0 - margin); r <= Math.min(file.rows - 1, a.r1 + margin); r++) for (var c = Math.max(0, a.c0 - margin); c <= Math.min(file.columns - 1, a.c1 + margin); c++) { add(r * file.columns + c); if (list.length > MANY) return; }
            }
            for (i = 0; i < dirty.length && list.length <= MANY; i++) add(dirty[i]);
            addRect(lastRect); addRect(rect);
            return list;
        }

        function paint() {
            frame = 0;
            if (!file) return;
            var t0 = win.performance ? win.performance.now() : 0, ctx = canvas.getContext('2d'), brush = brushNow(), cell = markedCell(), rect = View.cursorRect(file, cell, brush || 1), kind = 'full', count = 0;
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            var list = sameAsFull() ? cellsToPaint(rect) : null;
            if (list && list.length <= MANY && (view.scale >= SMALL_TILE || scratch.keptImage)) {
                var vis = View.visible(view, file, w, h), pixels = view.scale < SMALL_TILE, box = { x0: 1e9, y0: 1e9, x1: -1, y1: -1 };
                for (var i = 0; i < list.length; i++) {
                    var r = (list[i] / file.columns) | 0, c = list[i] - r * file.columns;
                    if (!vis || r < vis.r0 || r > vis.r1 || c < vis.c0 || c > vis.c1) continue;
                    if (pixels) View.redrawPixelCell(view, file, w, h, r, c, scratch, box); else View.redrawCell(ctx, view, file, w, h, r, c);
                    count++;
                }
                if (pixels) View.blitPixels(ctx, scratch, box);
                if (cell) View.drawCursor(ctx, view, file, w, h, cell, brush || 1);
                kind = 'cells';
            } else {
                View.draw(ctx, view, file, w, h, { scratch: scratch, cursor: cell, brush: brush || 1 });
                full = { file: file, cx: view.cx, cy: view.cy, scale: view.scale, w: w, h: h, dpr: dpr };
                count = -1;
            }
            dirty = []; lastRect = rect;
            chrome();
            canvas.setAttribute('data-paint', kind + ':' + count + ':' + (win.performance ? (win.performance.now() - t0).toFixed(2) : '0'));       // (what a test reads: the last frame's way, its cells and its milliseconds)
        }

        function redraw() { if (!frame) frame = win.requestAnimationFrame ? win.requestAnimationFrame(paint) : (paint(), 0); }

        function setFile(next) {
            file = next;
            cursor = null; hover = null; setStroke(null); pending = null; full = null; dirty = []; lastRect = null;
            measure();
            if (!file) { env.minimap.hidden = true; mini = null; return; }
            view = View.create(file, w, h);
            mini = View.miniBase(file, 168, 168, makeCanvas);
            env.miniCap.textContent = 'Whole map: ' + file.rows + ' x ' + file.columns;
            paint();
        }

        function at(e) { var box = canvas.getBoundingClientRect(); return { x: e.clientX - box.left, y: e.clientY - box.top }; }
        function select(cell) { cursor = cell; if (env.onCursor) env.onCursor(cell); }

        function cellOf(p) { return View.cellAt(view, file, w, h, p.x, p.y); }

        function startStroke(id, cell) {
            if (!painter || !painter.begin(cell)) return false;
            setStroke({ id: id, last: cell });
            return true;
        }

        function setStroke(next) { stroke = next; if (next) canvas.setAttribute('data-stroke', 'on'); else canvas.removeAttribute('data-stroke'); }       // (what a test reads: a stroke is going on)

        function cancelStroke() { if (stroke) { painter.cancel(); setStroke(null); } pending = null; }

        function down(e) {
            if (!file || (e.pointerType === 'mouse' && e.button !== 0 && e.button !== 1)) return;
            canvas.focus({ preventScroll: true });
            try { canvas.setPointerCapture(e.pointerId); } catch (x) { /* a pointer that is already gone */ }
            var p = at(e);
            pointers[e.pointerId] = p;
            var ids = Object.keys(pointers);
            if (ids.length === 1) {
                var cell = cellOf(p), mouse = e.pointerType === 'mouse', panning = mouse && (e.button === 1 || spaceHeld);
                var brush = !panning && brushNow() > 0 && !!cell;
                drag = { x: p.x, y: p.y, moved: false, pan: !brush };
                if (e.button === 1) e.preventDefault();
                if (brush) {
                    hover = cell;
                    if (mouse) startStroke(e.pointerId, cell); else pending = { id: e.pointerId, cell: cell };      // (a finger waits: it may be the first of two that zoom)
                    redraw();
                }
            } else if (ids.length === 2) { cancelStroke(); drag = null; pinch = { d: distance(), mid: middle() }; }
        }

        function distance() { var a = pointers[Object.keys(pointers)[0]], b = pointers[Object.keys(pointers)[1]]; return Math.max(1, Math.hypot(a.x - b.x, a.y - b.y)); }
        function middle() { var a = pointers[Object.keys(pointers)[0]], b = pointers[Object.keys(pointers)[1]]; return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }; }

        // The stroke goes on to `cell`: the painter paints the line from the last cell
        function carryOn(cell) {
            hover = cell;
            if (cell.row !== stroke.last.row || cell.column !== stroke.last.column) { painter.extend(stroke.last, cell); stroke.last = cell; }
            chrome(); redraw();
        }

        function move(e) {
            if (!file) return;
            var p = at(e);
            if (!pointers[e.pointerId]) {
                if (e.pointerType === 'mouse') {
                    var over = cellOf(p);
                    if (over && hover && over.row === hover.row && over.column === hover.column) return;
                    hover = over; chrome();
                    if (brushNow() > 0) redraw();
                }
                return;
            }
            var before = pointers[e.pointerId];
            pointers[e.pointerId] = p;
            if (stroke && stroke.id === e.pointerId) { var inside = cellOf(p); if (inside) carryOn(inside); return; }
            if (pending && pending.id === e.pointerId) {
                if (Math.hypot(p.x - drag.x, p.y - drag.y) < TAP) return;
                var first = pending.cell, now = cellOf(p);
                pending = null;
                if (startStroke(e.pointerId, first) && now) carryOn(now);
                return;
            }
            if (pinch && Object.keys(pointers).length === 2) {
                var d = distance(), mid = middle();
                View.zoomAt(view, file, w, h, d / pinch.d, mid.x, mid.y);
                View.panBy(view, file, w, h, mid.x - pinch.mid.x, mid.y - pinch.mid.y);
                pinch = { d: d, mid: mid };
                redraw();
            } else if (drag && drag.pan) {
                if (!drag.moved && Math.hypot(p.x - drag.x, p.y - drag.y) < TAP) return;
                drag.moved = true;
                View.panBy(view, file, w, h, p.x - before.x, p.y - before.y);
                redraw();
            }
        }

        function up(e) {
            if (!pointers[e.pointerId]) return;
            var p = at(e), cancelled = e.type === 'pointercancel', tapped = drag && drag.pan && !drag.moved && !cancelled, touched = null;
            delete pointers[e.pointerId];
            pinch = null;
            if (stroke && stroke.id === e.pointerId) {
                if (cancelled) painter.cancel(); else painter.end();
                if (e.pointerType !== 'mouse') touched = stroke.last;
                setStroke(null);
            } else if (pending && pending.id === e.pointerId) {
                touched = pending.cell;
                if (!cancelled && startStroke(e.pointerId, pending.cell)) { painter.end(); }
                setStroke(null); pending = null;
            }
            if (Object.keys(pointers).length === 1) { var id = Object.keys(pointers)[0]; drag = { x: pointers[id].x, y: pointers[id].y, moved: true, pan: true }; }
            else if (!Object.keys(pointers).length) drag = null;
            if (touched && !cancelled) { hover = null; select(touched); redraw(); }
            else if (tapped && file) { select(cellOf(p)); redraw(); }
        }

        function wheel(e) {
            if (!file) return;
            e.preventDefault();
            var p = at(e), unit = e.deltaMode === 1 ? 0.05 : (e.deltaMode === 2 ? 1 : 0.0016);
            View.zoomAt(view, file, w, h, Math.exp(-e.deltaY * unit), p.x, p.y);
            redraw();
        }

        function arrow(e) {
            var dr = e.key === 'ArrowUp' ? -1 : (e.key === 'ArrowDown' ? 1 : 0), dc = e.key === 'ArrowLeft' ? -1 : (e.key === 'ArrowRight' ? 1 : 0);
            if (!dr && !dc) return false;
            var n = e.shiftKey ? 8 : 1;
            if (!cursor) cursor = View.cellAt(view, file, w, h, w / 2, h / 2) || { row: 0, column: 0 };
            else cursor = { row: Math.max(0, Math.min(file.rows - 1, cursor.row + dr * n)), column: Math.max(0, Math.min(file.columns - 1, cursor.column + dc * n)) };
            View.reveal(view, file, w, h, cursor.row, cursor.column);
            if (env.onCursor) env.onCursor(cursor);
            return true;
        }

        function cursorStyle() { canvas.classList.toggle('painting', brushNow() > 0 && !spaceHeld); }

        function key(e) {
            if (!file || e.ctrlKey || e.metaKey || e.altKey) return;
            var handled = true;
            if (e.key === ' ' && painter) { spaceHeld = true; cursorStyle(); e.preventDefault(); return; }          // (held down: a drag looks around instead of painting)
            if (e.key === 'Enter' && brushNow() > 0) {                                                                // (the keyboard paints the marked square)
                e.preventDefault();
                if (cursor && painter.begin(cursor)) painter.end();
                return;
            }
            if (!arrow(e)) {
                if (e.key === '+' || e.key === '=') View.zoomAt(view, file, w, h, STEP, w / 2, h / 2);
                else if (e.key === '-' || e.key === '_') View.zoomAt(view, file, w, h, 1 / STEP, w / 2, h / 2);
                else if (e.key === '0' || e.key === 'Home') { view.scale = 0; View.clamp(view, file, w, h); }
                else handled = false;
            }
            if (handled) { e.preventDefault(); hover = null; redraw(); }
        }

        function zoomBy(factor) { if (file) { View.zoomAt(view, file, w, h, factor, w / 2, h / 2); redraw(); } }

        function miniGo(e) {
            if (!file || !mini) return;
            var box = env.mini.getBoundingClientRect(), p = View.miniPoint(file, box.width, box.height, e.clientX - box.left, e.clientY - box.top);
            View.centerOn(view, file, w, h, p.row, p.column);
            redraw();
        }

        canvas.addEventListener('pointerdown', down);
        canvas.addEventListener('pointermove', move);
        canvas.addEventListener('pointerup', up);
        canvas.addEventListener('pointercancel', up);
        canvas.addEventListener('pointerleave', function () { hover = null; if (file) { chrome(); if (brushNow() > 0) redraw(); } });
        canvas.addEventListener('wheel', wheel, { passive: false });
        canvas.addEventListener('keydown', key);
        canvas.addEventListener('keyup', function (e) { if (e.key === ' ' && spaceHeld) { spaceHeld = false; cursorStyle(); } });
        canvas.addEventListener('blur', function () { if (spaceHeld) { spaceHeld = false; cursorStyle(); } });
        env.zoomIn.addEventListener('click', function () { zoomBy(STEP); });
        env.zoomOut.addEventListener('click', function () { zoomBy(1 / STEP); });
        var miniDown = false;
        env.mini.addEventListener('pointerdown', function (e) { miniDown = true; try { env.mini.setPointerCapture(e.pointerId); } catch (x) { /* gone */ } miniGo(e); });
        env.mini.addEventListener('pointermove', function (e) { if (miniDown) miniGo(e); });
        env.mini.addEventListener('pointerup', function () { miniDown = false; });
        env.mini.addEventListener('pointercancel', function () { miniDown = false; });
        if (win.ResizeObserver) new win.ResizeObserver(function () {
            if (!file) return;
            var keep = { cx: view.cx, cy: view.cy, scale: view.scale }, whole = View.isWhole(view, file, w, h);
            measure(); view.cx = keep.cx; view.cy = keep.cy; view.scale = whole ? 0 : keep.scale;       // (a map that was whole stays whole when the canvas changes size)
            View.clamp(view, file, w, h); paint();
        }).observe(canvas);

        return {
            setFile: setFile, redraw: redraw,
            // The same map with other bytes (the dictionary grew): the view stays where it is
            swapFile: function (next) { file = next; full = null; mini = View.miniBase(file, 168, 168, makeCanvas); redraw(); },
            // The small map again, after a stroke or an undo
            refreshMini: function () { if (file) { mini = View.miniBase(file, 168, 168, makeCanvas); redraw(); } },
            // The painter (mapmaker_tools.js): {size() -> the brush in cells, 0 when the tool does not paint, begin(cell) -> true when a drag goes on, extend(from, to), end(), cancel()}
            setPainter: function (p) { painter = p; cursorStyle(); },
            // The cells (indexes, row * columns + column) that changed: they are painted again in the next frame
            markDirty: function (list) { for (var i = 0; i < list.length; i++) dirty.push(list[i]); redraw(); },
            // The tool, the brush or the marked square changed: paint it again, and the pointer shows what the tool does
            refresh: function () { cursorStyle(); redraw(); },
            cancelStroke: function () { cancelStroke(); },
            isStroking: function () { return !!stroke; },
            // Look at a cell (the checks card's "Show me"): the marked square goes there and it is brought to the middle, at least 22 pixels a tile
            showCell: function (row, column) {
                if (!file) return;
                var r = Math.max(0, Math.min(file.rows - 1, row)), c = column >= 0 ? Math.min(file.columns - 1, column) : Math.floor(view.cx);
                select({ row: r, column: c });
                View.centerOn(view, file, w, h, r, c, 22);
                redraw();
            },
            state: function () { return { view: view, cursor: cursor, width: w, height: h }; }
        };
    }

    return { create: create };
});
