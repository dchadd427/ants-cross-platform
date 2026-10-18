// Crop to fit (docs/MAP_EDITOR.md): a movable frame over the whole map, never bigger than a new Classic map may be. The page cuts nothing: the frame goes to the core's crop. A plain ES5 script.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_sizes.js'));
    else root.MapCrop = factory(root.MapSizes);
})(this, function (Sizes) {
    'use strict';

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
    function most(l, rows, columns) { return { rows: Math.min(l.rows, rows), columns: Math.min(l.columns, columns), cells: l.cells }; }
    function biggest(l, rows, columns) { var m = most(l, rows, columns); return { rows: m.rows, columns: Math.max(1, Math.min(m.columns, Math.floor(m.cells / m.rows))) }; }
    function start(l, rows, columns) { var b = biggest(l, rows, columns); return { row: Math.floor((rows - b.rows) / 2), column: Math.floor((columns - b.columns) / 2), rows: b.rows, columns: b.columns }; }
    function move(f, dr, dc, rows, columns) { return { row: clamp(f.row + dr, 0, rows - f.rows), column: clamp(f.column + dc, 0, columns - f.columns), rows: f.rows, columns: f.columns }; }

    // A pull at a handle by (dr, dc) cells: the held edges move within the map and the limits; when the cells are too many the frame gets shorter (`note` says so when the pull was sideways)
    function resize(l, f, handle, dr, dc, rows, columns) {
        var m = most(l, rows, columns), top = f.row, bottom = f.row + f.rows, left = f.column, right = f.column + f.columns, note = '';
        var n = handle.indexOf('n') >= 0, s = handle.indexOf('s') >= 0, w = handle.indexOf('w') >= 0, e = handle.indexOf('e') >= 0;
        if (n) top = clamp(top + dr, 0, bottom - 1);
        if (s) bottom = clamp(bottom + dr, top + 1, rows);
        if (w) left = clamp(left + dc, 0, right - 1);
        if (e) right = clamp(right + dc, left + 1, columns);
        if (bottom - top > m.rows) { if (n) top = bottom - m.rows; else bottom = top + m.rows; }
        if (right - left > m.columns) { if (w) left = right - m.columns; else right = left + m.columns; }
        if ((bottom - top) * (right - left) > m.cells) {
            var fit = Math.max(1, Math.floor(m.cells / (right - left)));
            if (n) top = bottom - fit; else if (s) bottom = top + fit; else top += Math.floor((bottom - top - fit) / 2), bottom = top + fit;
            note = w || e ? 'rows' : '';
        }
        return { frame: { row: top, column: left, rows: bottom - top, columns: right - left }, note: note };
    }

    // an arrow moves the frame by one cell
    function key(f, name, rows, columns) {
        var d = { ArrowUp: [-1, 0], ArrowDown: [1, 0], ArrowLeft: [0, -1], ArrowRight: [0, 1] }[name];
        return d ? move(f, d[0], d[1], rows, columns) : null;
    }

    function readout(f, l) {
        var cells = f.rows * f.columns;
        return { big: f.rows + ' rows x ' + f.columns + ' columns', cells: Sizes.number(cells) + ' of ' + Sizes.number(l.cells) + ' cells', percent: Math.max(2, Math.min(100, Math.round(cells / l.cells * 100))),
                 range: 'Rows ' + f.row + ' to ' + (f.row + f.rows - 1) + ' · Columns ' + f.column + ' to ' + (f.column + f.columns - 1) };
    }

    function noteText(f, l) { return 'You made the frame ' + f.columns + ' columns wide, so it can be ' + f.rows + ' rows tall at most (' + Sizes.number(l.cells) + ' cells in all). It shortened by itself.'; }

    // the tallest, the widest and a smaller frame, drawn under the map
    function shapes(l, rows, columns) {
        var tall = biggest(l, rows, columns), wc = Math.min(l.columns, columns);
        return [{ label: 'Tallest', rows: tall.rows, columns: tall.columns }, { label: 'Widest', rows: Math.max(1, Math.min(l.rows, rows, Math.floor(l.cells / wc))), columns: wc },
                { label: 'Smaller', rows: Math.max(1, Math.min(rows, Math.floor(l.rows * 2 / 3))), columns: Math.min(columns, l.rows) }];
    }

    // env: {doc, $ (an element of the screen by id), host (the screen), editor (hidden meanwhile), limits (Classic), rows, columns, name, tags(el), picture() -> an element, onUse(frame), onCancel()}
    function open(env) {
        var doc = env.doc, host = env.host, $ = env.$, L = env.limits, R = env.rows, C = env.columns, frame = start(L, R, C), grab = null;
        var stage = $('crop-stage'), box = $('crop-frame'), fill = $('crop-fill'), notes = $('crop-note'), use = $('crop-use'), cancel = $('crop-cancel');
        function el(tag, cls, text) { var n = doc.createElement(tag); n.className = cls; n.textContent = text; return n; }
        stage.insertBefore(env.picture(), stage.children[0]);
        $('crop-name').textContent = env.name; env.tags($('crop-tags'));
        $('crop-whole').textContent = 'Whole map: ' + R + ' rows x ' + C + ' columns. The dim part is left out.';
        $('crop-shapes').textContent = 'The frame can be any of these shapes, or smaller. It never has more than ' + L.rows + ' rows, ' + L.columns + ' columns or ' + Sizes.number(L.cells) + ' cells:';
        $('crop-nothing').textContent = 'Nothing changes until you save a copy. “' + env.name + '” stays as it is.';
        $('crop-chips').textContent = '';
        var chips = shapes(L, R, C).map(function (s) {
            var b = el('span', 'box', ''), chip = el('div', 'chip', '');
            b.style.width = Math.max(14, Math.round(s.columns / L.columns * 70)) + 'px'; b.style.height = Math.max(10, Math.round(s.rows / L.rows * 60)) + 'px';
            [b, el('span', 'cl', s.label), el('span', 'cn', s.rows + ' x ' + s.columns + ' · ' + Sizes.number(s.rows * s.columns))].forEach(function (n) { chip.appendChild(n); });
            $('crop-chips').appendChild(chip);
            return { s: s, chip: chip };
        });
        function note(cls, mark, text) {
            notes.textContent = '';
            if (!text) return;
            var n = el('div', 'note ' + cls, ''), ic = el('span', 'ic', mark);
            ic.setAttribute('aria-hidden', 'true'); n.appendChild(ic); n.appendChild(el('div', '', text)); notes.appendChild(n);
        }
        function show() {
            var s = box.style, r = readout(frame, L);
            s.left = frame.column / C * 100 + '%'; s.top = frame.row / R * 100 + '%'; s.width = frame.columns / C * 100 + '%'; s.height = frame.rows / R * 100 + '%';
            $('crop-big').textContent = r.big; fill.style.width = r.percent + '%'; $('crop-cells').textContent = r.cells; $('crop-range').textContent = r.range;
            chips.forEach(function (x) { x.chip.className = 'chip' + (x.s.rows === frame.rows && x.s.columns === frame.columns ? ' on' : ''); });
        }
        function set(next, why) { frame = next.frame || next; note('warn', '!', why ? noteText(frame, L) : ''); show(); }
        function pixel() { return C / (stage.getBoundingClientRect().width || C); }

        box.addEventListener('pointerdown', function (e) {
            grab = { x: e.clientX, y: e.clientY, h: (e.target.getAttribute && e.target.getAttribute('data-h')) || '', f: frame };
            if (box.setPointerCapture && e.pointerId !== undefined) box.setPointerCapture(e.pointerId);
            e.preventDefault(); e.stopPropagation(); box.focus();
        });
        box.addEventListener('pointermove', function (e) {
            if (!grab) return;
            var k = pixel(), dc = Math.round((e.clientX - grab.x) * k), dr = Math.round((e.clientY - grab.y) * k), out;
            if (grab.h) { out = resize(L, grab.f, grab.h, dr, dc, R, C); set(out, out.note); } else set(move(grab.f, dr, dc, R, C), '');
        });
        ['pointerup', 'pointercancel', 'lostpointercapture'].forEach(function (t) { box.addEventListener(t, function () { grab = null; }); });
        box.addEventListener('keydown', function (e) {
            var out = key(frame, e.key, R, C);
            if (out) { e.preventDefault(); set(out, ''); }
        });
        function leave(then) {
            doc.removeEventListener('keydown', onKey, true);
            use.removeEventListener('click', onUse); cancel.removeEventListener('click', onCancel);
            host.hidden = true; if (env.editor) env.editor.hidden = false;
            then();
        }
        function onKey(e) { if (e.key === 'Escape') { e.stopPropagation(); leave(env.onCancel); } }
        function onUse() { var f = frame; leave(function () { env.onUse(f); }); }
        function onCancel() { leave(env.onCancel); }
        use.addEventListener('click', onUse); cancel.addEventListener('click', onCancel);
        doc.addEventListener('keydown', onKey, true);
        if (env.editor) env.editor.hidden = true;
        stage.style.setProperty('--ar', C + '/' + R); stage.style.setProperty('--k', String(C / R));
        host.hidden = false;
        note('', 'i', 'The frame starts in the center, as big as a Classic map may be: ' + L.rows + ' rows, so it can be up to ' + frame.columns + ' columns wide.');
        show(); box.focus();
        return { frame: function () { return frame; }, set: set, use: onUse, cancel: onCancel };
    }

    return { start: start, move: move, resize: resize, key: key, readout: readout, noteText: noteText, shapes: shapes, open: open };
});
