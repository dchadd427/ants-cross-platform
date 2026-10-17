// The map maker's tools in the page (docs/MAP_EDITOR.md): which tool is chosen, the palette of the tool, the brush, the fill, undo and redo, the keys for them, and the painter that the canvas
// calls while a mouse or a finger paints (web/front/mapmaker_canvas.js). The arithmetic of a stroke, the patches and the history are web/front/mapmaker_edit.js; the tools and their
// tiles are web/front/mapmaker_palette.js. This script only joins them to the buttons. A browser script (it needs a document).
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_edit.js'), require('./mapmaker_palette.js'));
    else root.MapTools = factory(root.MapEdit, root.MapPalette);
})(this, function (Edit, Palette) {
    'use strict';

    var HINTS = {
        ground: 'Tap or click to paint the ground. Drag to paint a line.',
        objects: 'Tap or click to put an object. Drag to put a line of them.',
        erase: 'Tap or click to take an object away. A bigger object goes whole.',
        move: 'Drag to look around. Wheel or two fingers to zoom.'
    };
    var PHONE_HINTS = { ground: 'Tap to paint. Drag for a line.', objects: 'Tap to put one. Drag for a line.', erase: 'Tap to take an object away.', move: 'Drag to look around.' };
    var LATER = 'Hills, Ants and Food come next.';

    // env: {doc, win, canvas (MapCanvas), host: {tools (buttons with data-tool), box, title, swatches, name, hint, brushes (buttons with data-brush), fill, undo, redo},
    //       file() -> the open map's reader view, rebase(bytes) -> the reader view of the longer bytes (the dictionary grew), onEdit() after a change, busy() -> a dialog is open}
    function create(env) {
        var doc = env.doc, canvas = env.canvas, host = env.host;
        var state = { tool: 'ground', ground: 485, objects: 302, brush: 1, fill: false };
        var editor = null, current = null;

        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function paints() { return Palette.paints(state.tool); }
        function tileOf() { return state.tool === 'ground' ? state.ground : (state.tool === 'objects' ? state.objects : null); }

        // ---- the buttons ----
        function showButtons() {
            host.undo.disabled = !editor || !editor.history.canUndo();
            host.redo.disabled = !editor || !editor.history.canRedo();
            var on = paints();
            host.fill.disabled = !on || state.tool === 'erase';
            host.fill.setAttribute('aria-pressed', String(state.fill && !host.fill.disabled));
            Array.prototype.forEach.call(host.brushes, function (b) {
                var n = +b.getAttribute('data-brush');
                b.disabled = !on;
                b.setAttribute('aria-checked', String(n === state.brush));
                b.tabIndex = n === state.brush ? 0 : -1;
            });
        }

        function showTools() {
            Array.prototype.forEach.call(host.tools, function (b) {
                var tool = Palette.tool(b.getAttribute('data-tool'));
                b.disabled = !(tool && (tool.draws || tool.pans));            // (Hills, Ants and Food come in the next update)
                b.setAttribute('aria-pressed', String(tool.id === state.tool));
            });
            var hasPalette = !!(Palette.tool(state.tool) || {}).palette;
            host.box.hidden = !hasPalette;
            host.hint.textContent = '';
            host.hint.appendChild(el('span', 'dsk', HINTS[state.tool]));
            host.hint.appendChild(el('span', 'mob', PHONE_HINTS[state.tool]));
            host.hint.appendChild(el('span', 'dsk later', ' ' + LATER));
            if (hasPalette) showPalette();
            showButtons();
            canvas.refresh();
        }

        function showPalette() {
            var tool = state.tool, list = Palette.items(tool), chosen = tileOf();
            host.title.textContent = Palette.tool(tool).label;
            host.swatches.textContent = '';
            host.swatches.setAttribute('aria-label', Palette.tool(tool).label + ' to paint with');
            list.forEach(function (item) {
                var b = el('button', 'swatch'), on = item.id === chosen;
                b.type = 'button'; b.setAttribute('role', 'radio'); b.setAttribute('aria-checked', String(on)); b.tabIndex = on ? 0 : -1;
                b.setAttribute('aria-label', item.label); b.title = item.label + ' (' + item.name + ')';
                b.setAttribute('data-tile', String(item.id));
                var chip = el('i'); chip.style.background = item.color; b.appendChild(chip);
                b.addEventListener('click', function () { chooseTile(item.id); });
                host.swatches.appendChild(b);
                if (on) host.name.textContent = item.label;
            });
        }

        function chooseTile(id) {
            if (state.tool === 'ground') state.ground = id; else state.objects = id;
            showPalette();
        }

        function chooseTool(id) {
            var tool = Palette.tool(id);
            if (!tool || (!tool.draws && !tool.pans)) return;
            if (canvas.isStroking()) return;
            state.tool = id;
            showTools();
        }

        // ---- undo and redo ----
        function changed(dirty) {
            if (!dirty) return;
            canvas.markDirty(dirty);
            canvas.refreshMini();
            showButtons();
            env.onEdit();
        }
        function undo() { if (editor && !canvas.isStroking()) changed(editor.undo()); }
        function redo() { if (editor && !canvas.isStroking()) changed(editor.redo()); }

        // ---- the painter: what the canvas calls ----
        function startStroke() {
            var kind = state.tool, id = tileOf();
            if (id !== null) {
                var item = Palette.itemOf(kind, id), made = Edit.ensureTile(env.file(), id, item ? item.name : '');
                if (made.error) return null;
                if (made.grew) editor.rebase(env.rebase(made.bytes));
                if (made.named || made.grew) env.onEdit();
            }
            return editor.begin(kind, id);
        }

        var painter = {
            size: function () { return editor && paints() ? state.brush : 0; },
            begin: function (cell) {
                if (!editor || !paints()) return false;
                current = startStroke();
                if (!current) return false;
                if (state.fill && state.tool !== 'erase') {
                    current.fill(cell.row, cell.column);
                    canvas.markDirty(current.takeDirty());
                    painter.end();
                    return false;
                }
                current.stamp(cell.row, cell.column, state.brush);
                canvas.markDirty(current.takeDirty());
                return true;
            },
            extend: function (from, to) {
                if (!current) return;
                current.segment(from.row, from.column, to.row, to.column, state.brush);
                canvas.markDirty(current.takeDirty());
            },
            end: function () {
                if (!current) return;
                var n = current.end();
                current = null;
                if (n) { canvas.refreshMini(); showButtons(); env.onEdit(); }
            },
            cancel: function () {
                if (!current) return;
                var d = current.cancel();
                current = null;
                canvas.markDirty(d);
            }
        };

        // ---- the keys ----
        function typing(t) { var tag = t && t.tagName ? t.tagName.toLowerCase() : ''; return tag === 'input' || tag === 'textarea' || tag === 'select' || !!(t && t.isContentEditable); }

        function onKey(e) {
            if (e.defaultPrevented || !editor || env.busy() || typing(e.target)) return;
            var mod = e.ctrlKey || e.metaKey, k = String(e.key).toLowerCase();
            if (mod && !e.altKey) {
                if (k === 'z') { e.preventDefault(); if (e.shiftKey) redo(); else undo(); }
                else if (k === 'y') { e.preventDefault(); redo(); }
                return;
            }
            if (mod || e.altKey) return;
            var tool = Palette.toolOfKey(k);
            if (tool && k.length === 1) { if (tool.draws || tool.pans) { e.preventDefault(); chooseTool(tool.id); } return; }
            if (k === 'b' && !host.fill.disabled) { e.preventDefault(); state.fill = !state.fill; showButtons(); }
            else if ((k === '[' || k === ']') && paints()) {
                e.preventDefault();
                var at = Edit.BRUSHES.indexOf(state.brush) + (k === ']' ? 1 : -1);
                state.brush = Edit.BRUSHES[Math.max(0, Math.min(Edit.BRUSHES.length - 1, at))];
                showButtons(); canvas.refresh();
            }
        }

        function arrows(e) {              // the palette is one group: arrows go from swatch to swatch
            var list = Array.prototype.slice.call(host.swatches.querySelectorAll('.swatch')), at = list.indexOf(doc.activeElement), step = 0;
            if (e.key === 'ArrowRight' || e.key === 'ArrowDown') step = 1; else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') step = -1; else return;
            e.preventDefault();
            var next = list[(at + step + list.length) % list.length];
            if (next) { chooseTile(+next.getAttribute('data-tile')); host.swatches.querySelector('[aria-checked="true"]').focus(); }
        }

        // ---- wiring ----
        Array.prototype.forEach.call(host.tools, function (b) { b.addEventListener('click', function () { chooseTool(b.getAttribute('data-tool')); }); });
        Array.prototype.forEach.call(host.brushes, function (b) {
            b.addEventListener('click', function () { state.brush = +b.getAttribute('data-brush'); showButtons(); canvas.refresh(); });
            b.addEventListener('keydown', function (e) {
                var at = Edit.BRUSHES.indexOf(state.brush), step = e.key === 'ArrowRight' || e.key === 'ArrowDown' ? 1 : (e.key === 'ArrowLeft' || e.key === 'ArrowUp' ? -1 : 0);
                if (!step) return;
                e.preventDefault();
                state.brush = Edit.BRUSHES[Math.max(0, Math.min(Edit.BRUSHES.length - 1, at + step))];
                showButtons(); canvas.refresh();
                Array.prototype.filter.call(host.brushes, function (n) { return +n.getAttribute('data-brush') === state.brush; })[0].focus();
            });
        });
        host.fill.addEventListener('click', function () { state.fill = !state.fill; showButtons(); });
        host.undo.addEventListener('click', undo);
        host.redo.addEventListener('click', redo);
        host.swatches.addEventListener('keydown', arrows);
        doc.addEventListener('keydown', onKey);
        canvas.setPainter(painter);
        showTools();

        return {
            // A map is open (a new one, or one from the disk): the history starts empty
            setMap: function (file) { editor = Edit.create(file); current = null; showButtons(); },
            choose: chooseTool, undo: undo, redo: redo,
            isClean: function () { return !editor || editor.history.isClean(); },
            isSaved: function () { return !editor || editor.history.isSaved(); },
            markSaved: function () { if (editor) editor.history.markSaved(); },
            state: function () { return { tool: state.tool, ground: state.ground, objects: state.objects, brush: state.brush, fill: state.fill, history: editor ? editor.history.size() : null }; }
        };
    }

    return { create: create };
});
