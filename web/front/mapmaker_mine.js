// My maps (docs/MAP_EDITOR.md): the list of the maps the player saved in this browser: open one as the working map, copy it, or delete the player's own saved copy (after a clear question).
// The page judges nothing: the kind, the size and the check are read from the saved bytes by the functions it is given. A plain ES5 script.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapMine = factory();
})(this, function () {
    'use strict';

    var FILTERS = [['all', 'All'], ['classic', 'Classic'], ['extended', 'Extended']];
    var NONE = 'You have no saved maps yet. Use “Save as new...” in the editor to keep a map here.';
    var OFF = 'This browser keeps no saved maps (a private window, or site data is blocked), so the list is empty. The map maker still works: use Download to keep a map on your device.';

    // env: {doc, $ (an element of the page by id), store, dialogs, parse(bytes) -> file or null, check(bytes) -> {works, text} or null, thumb(file) -> canvas, tags(host, kind), later(fn, ms),
    //       onOpen(rec), onCopy(rec), onNew(), onBack(), onShow(), onHide()}
    function create(env) {
        var doc = env.doc, $ = env.$, host = $('mine'), list = $('mine-list'), status = $('mine-status'), note = $('mine-note'), chips = $('mine-chips');
        var rows = [], filter = 'all', shown = false, token = 0;

        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function button(cls, text, onClick, label) { var b = el('button', cls, text); b.type = 'button'; if (label) b.setAttribute('aria-label', label); b.addEventListener('click', onClick); return b; }
        function label(rec) { return typeof rec.name === 'string' && rec.name ? rec.name : ''; }          // (a record with no name is shown and asked about without one)
        function count(kind) { return rows.filter(function (r) { return kind === 'all' || (r.file && r.file.kind === kind); }).length; }
        function say(text) { status.textContent = ''; status.textContent = text; }

        function showChips() {
            chips.textContent = '';
            chips.hidden = !rows.length;
            FILTERS.forEach(function (f) {
                var b = button('mchip' + (f[0] === filter ? ' on' : ''), f[1] + ' (' + count(f[0]) + ')', function () { filter = f[0]; showList(); b.focus(); });
                b.setAttribute('aria-pressed', f[0] === filter ? 'true' : 'false');
                chips.appendChild(b);
            });
        }

        function showList() {
            showChips();
            var shownRows = rows.filter(function (r) { return filter === 'all' || (r.file && r.file.kind === filter); });
            list.textContent = '';
            note.textContent = !rows.length ? NONE : !shownRows.length ? 'No ' + filter + ' maps are saved yet.' : '';
            shownRows.forEach(function (r) { list.appendChild(r.node); });
            say(shownRows.length === 1 ? '1 saved map shown.' : shownRows.length + ' saved maps shown.');
        }

        // the thumbnail and the check are made one row at a time, so a long list never freezes the page
        function fill(queue, mine) {
            var r = queue.shift();
            if (!r || mine !== token) return;
            if (rows.indexOf(r) >= 0) {
                var pic = env.thumb(r.file), got = env.check(r.rec.bytes);
                if (pic) r.thumb.appendChild(pic);
                r.line.textContent = '';
                r.line.appendChild(el('span', 'dot ' + (got && got.works ? 'ok' : 'bad')));
                r.line.appendChild(el('span', got && got.works ? 'st-ok' : 'st-no', got ? got.text : 'Not checked'));
            }
            env.later(function () { fill(queue, mine); }, 0);
        }

        function makeRow(rec) {
            var bytes = new Uint8Array(rec.bytes || 0), file = label(rec) ? env.parse(bytes) : null, name = label(rec), r = { rec: rec, file: file }, tags = el('span', 'tags'), acts = el('div', 'macts');
            r.node = el('div', 'mrow'); r.thumb = el('span', 'mthumb'); r.thumb.setAttribute('aria-hidden', 'true');
            r.line = el('div', 't3');
            if (file) env.tags(tags, file.kind);
            var size = el('div', 't2', file ? file.rows + ' x ' + file.columns : 'This saved map cannot be read.');
            if (file) size.appendChild(el('span', 'hidem', ' · saved in this browser'));
            if (file) {
                acts.appendChild(button('btn sm', 'Open', function () { env.onOpen(rec); }, 'Open ' + (name || 'this saved map')));
                acts.appendChild(button('btn sm', 'Copy…', function () { env.onCopy(rec); }, 'Copy ' + (name || 'this saved map')));
            }
            acts.appendChild(button('btn sm ghost', 'Delete…', function () { confirmDelete(r); }, 'Delete ' + (name || 'this unreadable saved map')));
            var t1 = el('div', 't1', name + ' ');
            t1.appendChild(tags);
            var mid = el('div', 'mmid'); mid.appendChild(t1); mid.appendChild(size); mid.appendChild(r.line);
            r.node.appendChild(r.thumb); r.node.appendChild(mid); r.node.appendChild(acts);
            return r;
        }

        // the question before a saved map is deleted: it is the player's own copy in this browser; the default choice is to keep it
        function confirmDelete(r) {
            var name = label(r.rec);
            env.dialogs.open({
                title: 'Delete this saved map?', cls: 'narrow',
                build: function (body) {
                    body.appendChild(el('p', 'fatalmsg', (name ? '“' + name + '”' : 'This unreadable saved map') + ' will be removed from this browser, and it cannot be brought back. A copy you downloaded stays where you saved it, and the game’s own maps are never touched.'));
                },
                buttons: [{ id: 'keep', text: 'Keep it', ghost: true }, { id: 'delete', text: 'Delete it', onClick: function () { remove(r); } }]
            });
        }
        function remove(r) {
            env.store.remove(r.rec.key, function (error) {
                if (error) return env.dialogs.message('The map could not be deleted', 'The browser would not delete it. Nothing was changed.');
                rows.splice(rows.indexOf(r), 1);
                showList();
                say((label(r.rec) ? '“' + label(r.rec) + '”' : 'The unreadable saved map') + ' was deleted.');
                $('mine-new').focus();
            });
        }

        function load() {
            var mine = ++token;
            rows = []; list.textContent = ''; chips.hidden = true;
            note.textContent = 'Looking at your saved maps…';
            if (env.store.status() === 'pending') return env.later(function () { if (mine === token && shown) load(); }, 150);
            if (env.store.status() !== 'ready') { note.textContent = OFF; return say(OFF); }
            env.store.list(function (error, recs) {
                if (mine !== token || !shown) return;
                if (error) { note.textContent = 'The browser would not show your saved maps. Nothing was changed.'; return say(note.textContent); }
                rows = recs.map(makeRow);
                showList();
                fill(rows.slice(), mine);
            });
        }

        function show() {
            shown = true; filter = 'all';
            host.hidden = false;
            if (env.onShow) env.onShow();
            load();
            $('mine-title').focus();
        }
        function hide() {
            if (!shown) return;
            shown = false; token++;
            host.hidden = true;
            if (env.onHide) env.onHide();
        }

        $('mine-new').addEventListener('click', function () { env.onNew(); });
        $('mine-back').addEventListener('click', function () { env.onBack(); });
        doc.addEventListener('keydown', function (e) { if (shown && e.key === 'Escape' && !env.dialogs.isOpen()) env.onBack(); });
        return { show: show, hide: hide, isOpen: function () { return shown; } };
    }

    return { create: create };
});
