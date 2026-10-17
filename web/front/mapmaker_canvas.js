// The map maker's canvas in the page (docs/MAP_EDITOR.md): it keeps the view of one map (web/front/mapmaker_view.js does the arithmetic and the drawing) and answers the person's hands: a mouse
// drags and its wheel zooms, one finger drags and two fingers pinch, the keyboard moves a marked square (arrows), zooms (plus, minus) and shows the whole map (zero). The small map of the
// whole map (shown while only a part is on the screen) is a way to look somewhere else. A browser script (it needs a document).
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_view.js'));
    else root.MapCanvas = factory(root.MapView);
})(this, function (View) {
    'use strict';

    var STEP = 1.5;                 // what a press of + or - zooms by
    var TAP = 6;                    // a finger or a mouse that moves less than this many pixels is a tap, not a drag

    // env: {doc, win, canvas, minimap (the box), mini (its canvas), miniCap, where (the status line), zoomIn, zoomOut (the buttons), onCursor(cell)}
    function create(env) {
        var doc = env.doc, win = env.win, canvas = env.canvas, file = null, view = null, cursor = null, hover = null, w = 0, h = 0, dpr = 1, frame = 0;
        var scratch = doc.createElement('canvas'), mini = null, pointers = {}, drag = null, pinch = null;

        function makeCanvas(cw, ch) { var c = doc.createElement('canvas'); c.width = cw; c.height = ch; return c; }

        function measure() {
            var box = canvas.getBoundingClientRect();
            w = Math.max(1, Math.round(box.width)); h = Math.max(1, Math.round(box.height));
            dpr = Math.min(3, win.devicePixelRatio || 1);
            canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr);
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

        function paint() {
            frame = 0;
            if (!file) return;
            var ctx = canvas.getContext('2d');
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            View.draw(ctx, view, file, w, h, { scratch: scratch, cursor: cursor });
            chrome();
        }

        function redraw() { if (!frame) frame = win.requestAnimationFrame ? win.requestAnimationFrame(paint) : (paint(), 0); }

        function setFile(next) {
            file = next;
            cursor = null; hover = null;
            measure();
            if (!file) { env.minimap.hidden = true; mini = null; return; }
            view = View.create(file, w, h);
            mini = View.miniBase(file, 168, 168, makeCanvas);
            env.miniCap.textContent = 'Whole map: ' + file.rows + ' x ' + file.columns;
            paint();
        }

        function at(e) { var box = canvas.getBoundingClientRect(); return { x: e.clientX - box.left, y: e.clientY - box.top }; }
        function select(cell) { cursor = cell; if (env.onCursor) env.onCursor(cell); }

        function down(e) {
            if (!file || (e.pointerType === 'mouse' && e.button !== 0)) return;
            canvas.focus({ preventScroll: true });
            try { canvas.setPointerCapture(e.pointerId); } catch (x) { /* a pointer that is already gone */ }
            var p = at(e);
            pointers[e.pointerId] = p;
            var ids = Object.keys(pointers);
            if (ids.length === 1) drag = { x: p.x, y: p.y, moved: false };
            else if (ids.length === 2) { drag = null; pinch = { d: distance(), mid: middle() }; }
        }

        function distance() { var a = pointers[Object.keys(pointers)[0]], b = pointers[Object.keys(pointers)[1]]; return Math.max(1, Math.hypot(a.x - b.x, a.y - b.y)); }
        function middle() { var a = pointers[Object.keys(pointers)[0]], b = pointers[Object.keys(pointers)[1]]; return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }; }

        function move(e) {
            if (!file) return;
            var p = at(e);
            if (!pointers[e.pointerId]) {
                if (e.pointerType === 'mouse') { hover = View.cellAt(view, file, w, h, p.x, p.y); chrome(); }
                return;
            }
            var before = pointers[e.pointerId];
            pointers[e.pointerId] = p;
            if (pinch && Object.keys(pointers).length === 2) {
                var d = distance(), mid = middle();
                View.zoomAt(view, file, w, h, d / pinch.d, mid.x, mid.y);
                View.panBy(view, file, w, h, mid.x - pinch.mid.x, mid.y - pinch.mid.y);
                pinch = { d: d, mid: mid };
                redraw();
            } else if (drag) {
                if (!drag.moved && Math.hypot(p.x - drag.x, p.y - drag.y) < TAP) return;
                drag.moved = true;
                View.panBy(view, file, w, h, p.x - before.x, p.y - before.y);
                redraw();
            }
        }

        function up(e) {
            if (!pointers[e.pointerId]) return;
            var p = at(e), tapped = drag && !drag.moved && e.type === 'pointerup';
            delete pointers[e.pointerId];
            pinch = null;
            if (Object.keys(pointers).length === 1) { var id = Object.keys(pointers)[0]; drag = { x: pointers[id].x, y: pointers[id].y, moved: true }; }
            else if (!Object.keys(pointers).length) drag = null;
            if (tapped && file) { select(View.cellAt(view, file, w, h, p.x, p.y)); redraw(); }
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

        function key(e) {
            if (!file || e.ctrlKey || e.metaKey || e.altKey) return;
            var handled = true;
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
        canvas.addEventListener('pointerleave', function () { hover = null; if (file) chrome(); });
        canvas.addEventListener('wheel', wheel, { passive: false });
        canvas.addEventListener('keydown', key);
        env.zoomIn.addEventListener('click', function () { zoomBy(STEP); });
        env.zoomOut.addEventListener('click', function () { zoomBy(1 / STEP); });
        var miniDown = false;
        env.mini.addEventListener('pointerdown', function (e) { miniDown = true; try { env.mini.setPointerCapture(e.pointerId); } catch (x) { /* gone */ } miniGo(e); });
        env.mini.addEventListener('pointermove', function (e) { if (miniDown) miniGo(e); });
        env.mini.addEventListener('pointerup', function () { miniDown = false; });
        env.mini.addEventListener('pointercancel', function () { miniDown = false; });
        if (win.ResizeObserver) new win.ResizeObserver(function () { if (file) { var keep = { cx: view.cx, cy: view.cy, scale: view.scale }; measure(); view.cx = keep.cx; view.cy = keep.cy; view.scale = keep.scale; View.clamp(view, file, w, h); paint(); } }).observe(canvas);

        return {
            setFile: setFile, redraw: redraw,
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
