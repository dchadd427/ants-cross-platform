// The dialogs of the map maker (docs/MAP_EDITOR.md): the one box that every dialog is (a box on a desktop, a sheet from the bottom on a phone: mapmaker.css; it holds the focus, closes on
// Escape and leaves the page behind it inert), and the dialogs themselves: New map (the two kinds, the size boxes, the live line, the core's own refusal of a size), the checks sheet, a
// plain message and the menu of a phone. Every sentence of the core is put in with textContent. A browser script (it needs a document).
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_sizes.js'), require('./mapmaker_checks.js'));
    else root.MapDialog = factory(root.MapSizes, root.MapChecks);
})(this, function (Sizes, Checks) {
    'use strict';

    var FOCUSABLE = 'button:not([disabled]), input:not([disabled]):not([type=hidden]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';
    var KIND_WORDS = {
        classic: { title: 'Classic map', line: 'Works in the original game and in the new version.' },
        extended: { title: 'Extended map', line: 'Bigger and taller. Works in the new version only: the original game cannot open it.' }
    };
    var EXTENDED_NOTE = 'Only the new version can open an Extended map.';
    var first = 0;

    function create(doc, host, behind) {
        var current = null;

        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }

        function closeCurrent() { if (current) current.close(); }

        // spec: {title (a heading) or label (no heading), cls, build(body, ctl), buttons: [{id, text, ghost, disabled, keepOpen, onClick(ctl)}], focus (a selector), onClose}
        function open(spec) {
            closeCurrent();
            var opener = doc.activeElement;
            var scrim = el('div', 'scrim'), box = el('div', 'dlg' + (spec.cls ? ' ' + spec.cls : ''));
            box.setAttribute('role', 'dialog');
            box.setAttribute('aria-modal', 'true');
            var inner = el('div', 'dlg-in');
            if (spec.title) {
                var h = el('h2', '', spec.title);
                h.id = 'dlg-title-' + (++first);
                box.setAttribute('aria-labelledby', h.id);
                inner.appendChild(h);
            } else box.setAttribute('aria-label', spec.label || 'Dialog');
            var foot = el('div', 'dlg-foot'), buttons = {}, removeKeys = function () {};
            var ctl = { body: inner, foot: foot, el: box, buttons: buttons, closed: false };
            ctl.close = function () {
                if (ctl.closed) return;
                ctl.closed = true;
                removeKeys();
                if (scrim.parentNode) scrim.parentNode.removeChild(scrim);
                for (var i = 0; i < behind.length; i++) behind[i].removeAttribute('inert');
                if (current === ctl) current = null;
                if (spec.onClose) spec.onClose();
                if (opener && opener.focus && doc.body.contains(opener) && !opener.disabled) opener.focus();
            };
            ctl.setDisabled = function (id, off) { if (buttons[id]) buttons[id].disabled = !!off; };
            if (spec.build) spec.build(inner, ctl);
            (spec.buttons || []).forEach(function (b) {
                var button = el('button', 'btn' + (b.ghost ? ' ghost' : ''), b.text);
                button.type = 'button';
                if (b.disabled) button.disabled = true;
                button.addEventListener('click', function () { if (b.onClick) b.onClick(ctl); if (!b.keepOpen) ctl.close(); });
                buttons[b.id || b.text] = button;
                foot.appendChild(button);
            });
            box.appendChild(inner);
            box.appendChild(foot);
            scrim.appendChild(box);
            scrim.addEventListener('click', function (e) { if (e && e.target === scrim) ctl.close(); });
            // The keys are heard on the document while the dialog is open: a click on plain text moves the focus to the page's body, and Escape must still close it
            function onKey(e) {
                if (e.key === 'Escape') { e.stopPropagation(); ctl.close(); return; }
                if (e.key !== 'Tab') return;
                var items = Array.prototype.filter.call(box.querySelectorAll(FOCUSABLE), function (n) { return n.offsetParent !== null || n === doc.activeElement; });
                if (!items.length) return;
                var firstItem = items[0], last = items[items.length - 1];
                if (e.shiftKey && (doc.activeElement === firstItem || !box.contains(doc.activeElement))) { e.preventDefault(); last.focus(); }
                else if (!e.shiftKey && (doc.activeElement === last || !box.contains(doc.activeElement))) { e.preventDefault(); firstItem.focus(); }
            }
            doc.addEventListener('keydown', onKey, true);
            removeKeys = function () { doc.removeEventListener('keydown', onKey, true); };
            for (var i = 0; i < behind.length; i++) behind[i].setAttribute('inert', '');
            host.appendChild(scrim);
            current = ctl;
            var target = (spec.focus && box.querySelector(spec.focus)) || box.querySelector('input:checked') || box.querySelector('input, button');
            if (target) target.focus();
            return ctl;
        }

        function message(title, text, buttonText) {
            return open({ title: title, cls: 'narrow', build: function (body) { body.appendChild(el('p', 'fatalmsg', text)); }, buttons: [{ text: buttonText || 'Close' }] });
        }

        function menu(items) {
            return open({
                label: 'Menu', cls: 'narrow',
                build: function (body) {
                    var list = el('div', 'menulist');
                    items.forEach(function (it) {
                        var b = el('button', 'btn' + (it.cls ? ' ' + it.cls : ''), it.text);
                        b.type = 'button';
                        if (it.disabled) b.disabled = true;
                        b.addEventListener('click', function () { current.close(); it.onClick(); });
                        list.appendChild(b);
                        if (it.hint) list.appendChild(el('p', 'menuhint', it.hint));
                    });
                    body.appendChild(list);
                }, buttons: [{ text: 'Close', ghost: true }]
            });
        }

        // The checks card over the map. handlers.onShow(row, column) is called by a "Show me" link (the sheet closes first).
        function checks(report, handlers) {
            return open({
                label: 'Checks', cls: 'checks',
                build: function (body, ctl) {
                    body.style.padding = '0';
                    body.appendChild(Checks.render(doc, report, { onShow: function (r, c) { ctl.close(); if (handlers && handlers.onShow) handlers.onShow(r, c); } }));
                },
                buttons: [{ id: 'close', text: 'Close', ghost: true }]
            });
        }

        return { open: open, close: closeCurrent, message: message, menu: menu, checks: checks, isOpen: function () { return !!current; } };
    }

    // The New map dialog. env: {doc, dialogs (the result of create), limits (MapSizes.limitsOf), make(kind, rows, columns) -> a core answer {status, bytes | error}, onMade(bytes, kind)}
    function newMap(env) {
        var doc = env.doc, limits = env.limits, kind = 'classic', remembered = { classic: ['31', '31'], extended: ['300', '400'] };
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        var rowsIn, columnsIn, liveEl, noteBox, cards = {}, ctl;

        function lineParts(parent, parts) { parts.forEach(function (p) { parent.appendChild(p[1] ? el('b', '', p[0]) : doc.createTextNode(p[0])); }); }

        function card(which) {
            var label = el('label', 'kind'), input = el('input');
            input.type = 'radio'; input.name = 'kind'; input.value = which;
            label.appendChild(input);
            label.appendChild(el('span', 'rb')).setAttribute('aria-hidden', 'true');
            var title = el('span', 'kt', KIND_WORDS[which].title);
            if (which === 'extended') title.appendChild(el('span', 'badge-nv', 'New version only'));
            label.appendChild(title);
            label.appendChild(el('span', 'kl', KIND_WORDS[which].line));
            var limit = el('span', 'kx');
            lineParts(limit, Sizes.limitLine(which, limits));
            label.appendChild(limit);
            input.addEventListener('change', function () { if (input.checked) choose(which); });
            cards[which] = { label: label, input: input };
            return label;
        }

        // Another kind: each kind keeps the sizes that were typed for it, unless `keepSizes` (a Classic size that is too big, carried over to an Extended map)
        function choose(which, keepSizes) {
            remembered[kind] = [rowsIn.value, columnsIn.value];
            kind = which;
            if (!keepSizes) { rowsIn.value = remembered[kind][0]; columnsIn.value = remembered[kind][1]; }
            update();
        }

        function within(rows, columns) {
            if (kind === 'classic') { var c = limits.classic; return { rows: rows <= c.rows, columns: columns <= c.columns, cells: rows * columns <= c.cells }; }
            var e = limits.extended;
            return { rows: rows <= e.longest, columns: columns <= e.longest, cells: rows * columns <= e.cells };
        }

        function setNote(cls, mark, text, button) {
            noteBox.textContent = '';
            if (!text) return;
            var note = el('div', 'note' + (cls ? ' ' + cls : ''));
            note.appendChild(el('span', 'ic', mark)).setAttribute('aria-hidden', 'true');
            var body = el('div');
            body.appendChild(el('div', '', text));
            if (button) body.appendChild(button);
            note.appendChild(body);
            noteBox.appendChild(note);
        }

        function update() {
            Object.keys(cards).forEach(function (k) { cards[k].label.classList.toggle('on', k === kind); cards[k].input.checked = k === kind; });
            rowsIn.removeAttribute('aria-invalid'); columnsIn.removeAttribute('aria-invalid');
            var live = Sizes.liveLine(kind, rowsIn.value, columnsIn.value, limits);
            liveEl.textContent = '';
            if (!live) {
                liveEl.textContent = 'Type a whole number of rows and of columns, at least 1.';
                if (Sizes.wholeNumber(rowsIn.value) === null) rowsIn.setAttribute('aria-invalid', 'true');
                if (Sizes.wholeNumber(columnsIn.value) === null) columnsIn.setAttribute('aria-invalid', 'true');
                setNote('', '', '');
                ctl.setDisabled('make', true);
                return;
            }
            liveEl.appendChild(el('b', '', Sizes.number(live.cells)));
            liveEl.appendChild(doc.createTextNode((live.cells === 1 ? ' cell of ' : ' cells of ') + Sizes.number(live.max) + ' · the file will be ' + live.text));
            var fit = within(live.rows, live.columns);
            if (fit.rows && fit.columns && fit.cells) {
                setNote('', kind === 'extended' ? 'i' : '', kind === 'extended' ? EXTENDED_NOTE : '');
                ctl.setDisabled('make', false);
                return;
            }
            if (!fit.rows || !fit.cells) rowsIn.setAttribute('aria-invalid', 'true');
            if (!fit.columns || !fit.cells) columnsIn.setAttribute('aria-invalid', 'true');
            var refused = env.make(kind, live.rows, live.columns), why = refused.status === 0 ? { text: 'That size is not allowed here.', offersExtended: false } : Sizes.refusal(refused.error);
            var button = null;
            if (kind === 'classic' && why.offersExtended) {
                button = el('button', 'btn sm', 'Make it an Extended map');
                button.type = 'button';
                button.addEventListener('click', function () { choose('extended', true); cards.extended.input.focus(); });
            }
            setNote('bad', '!', why.text, button);
            ctl.setDisabled('make', true);
        }

        function make() {
            var rows = Sizes.wholeNumber(rowsIn.value), columns = Sizes.wholeNumber(columnsIn.value);
            var made = env.make(kind, rows, columns);
            if (made.status !== 0) { setNote('bad', '!', Sizes.refusal(made.error).text); return false; }
            env.onMade(made.bytes, kind);
            return true;
        }

        ctl = env.dialogs.open({
            title: 'New map', focus: 'input:checked',
            build: function (body) {
                body.appendChild(el('p', 'lead', 'What kind of map do you want to make?')).id = 'kind-lead';
                var group = el('div', 'kinds');
                group.setAttribute('role', 'radiogroup'); group.setAttribute('aria-labelledby', 'kind-lead');
                group.appendChild(card('classic')); group.appendChild(card('extended'));
                body.appendChild(group);
                var fields = el('div', 'fields');
                function field(id, text) {
                    var wrap = el('div', 'fld'), label = el('label', '', text), input = el('input');
                    label.htmlFor = id; input.id = id; input.type = 'text'; input.setAttribute('inputmode', 'numeric'); input.setAttribute('autocomplete', 'off'); input.maxLength = 6;
                    wrap.appendChild(label); wrap.appendChild(input); fields.appendChild(wrap);
                    input.addEventListener('input', update);
                    input.addEventListener('keydown', function (e) { if (e.key === 'Enter' && !ctl.buttons.make.disabled) { e.preventDefault(); ctl.buttons.make.click(); } });
                    return input;
                }
                rowsIn = field('size-rows', 'Rows');
                fields.appendChild(el('span', 'times', '×')).setAttribute('aria-hidden', 'true');
                columnsIn = field('size-columns', 'Columns');
                liveEl = el('div', 'live'); liveEl.setAttribute('aria-live', 'polite');
                fields.appendChild(liveEl);
                body.appendChild(fields);
                noteBox = el('div'); noteBox.setAttribute('aria-live', 'polite');
                body.appendChild(noteBox);
                rowsIn.value = remembered.classic[0]; columnsIn.value = remembered.classic[1];
            },
            buttons: [{ id: 'cancel', text: 'Cancel', ghost: true }, { id: 'make', text: 'Make map', keepOpen: true, onClick: function (c) { if (make()) c.close(); } }]
        });
        update();
        return ctl;
    }

    // The Resize map dialog. env: {doc, dialogs, limits, kind, rows, columns, onResize(rows, columns)}
    function resizeMap(env) {
        var doc = env.doc, limits = env.limits, kind = env.kind || 'classic';
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        var rowsIn, columnsIn, liveEl, noteBox, ctl;

        function within(rows, columns) {
            if (kind === 'classic') { var c = limits.classic; return { rows: rows <= c.rows, columns: columns <= c.columns, cells: rows * columns <= c.cells }; }
            var e = limits.extended;
            return { rows: rows <= e.longest, columns: columns <= e.longest, cells: rows * columns <= e.cells };
        }

        function setNote(cls, mark, text) {
            noteBox.textContent = '';
            if (!text) return;
            var note = el('div', 'note' + (cls ? ' ' + cls : ''));
            note.appendChild(el('span', 'ic', mark)).setAttribute('aria-hidden', 'true');
            var body = el('div');
            body.appendChild(el('div', '', text));
            note.appendChild(body);
            noteBox.appendChild(note);
        }

        function update() {
            rowsIn.removeAttribute('aria-invalid'); columnsIn.removeAttribute('aria-invalid');
            var live = Sizes.liveLine(kind, rowsIn.value, columnsIn.value, limits);
            liveEl.textContent = '';
            if (!live) {
                liveEl.textContent = 'Type a whole number of rows and of columns, at least 1.';
                if (Sizes.wholeNumber(rowsIn.value) === null) rowsIn.setAttribute('aria-invalid', 'true');
                if (Sizes.wholeNumber(columnsIn.value) === null) columnsIn.setAttribute('aria-invalid', 'true');
                setNote('', '', '');
                ctl.setDisabled('resize', true);
                return;
            }
            liveEl.appendChild(el('b', '', Sizes.number(live.cells)));
            liveEl.appendChild(doc.createTextNode((live.cells === 1 ? ' cell of ' : ' cells of ') + Sizes.number(live.max) + ' \u00b7 the file will be ' + live.text));
            var fit = within(live.rows, live.columns);
            if (!fit.rows || !fit.columns || !fit.cells) {
                if (!fit.rows || !fit.cells) rowsIn.setAttribute('aria-invalid', 'true');
                if (!fit.columns || !fit.cells) columnsIn.setAttribute('aria-invalid', 'true');
                var lim = kind === 'classic' ? limits.classic : limits.extended;
                var msg = !fit.cells ? 'That size has ' + Sizes.number(live.cells) + ' cells, more than the ' + Sizes.number(lim.cells) + ' allowed.' :
                    (kind === 'classic' ? 'A Classic map can have at most ' + lim.rows + ' rows and ' + lim.columns + ' columns.' : 'An Extended map side can be at most ' + Sizes.number(lim.longest) + '.');
                setNote('bad', '!', msg);
                ctl.setDisabled('resize', true);
                return;
            }
            if (live.rows === env.rows && live.columns === env.columns) {
                setNote('', '', '');
                if (ctl.buttons.resize) ctl.buttons.resize.textContent = 'Resize map';
                ctl.setDisabled('resize', true);
                return;
            }
            var shrinks = live.rows < env.rows || live.columns < env.columns;
            setNote('', shrinks ? 'i' : '', shrinks ? 'This size is smaller than ' + env.rows + ' \u00d7 ' + env.columns + '. Next you will choose which part of the map to keep.' : '');
            if (ctl.buttons.resize) ctl.buttons.resize.textContent = shrinks ? 'Choose part to keep...' : 'Resize map';
            ctl.setDisabled('resize', false);
        }

        ctl = env.dialogs.open({
            title: 'Resize map', cls: 'narrow', focus: '#resize-rows',
            build: function (body) {
                body.appendChild(el('p', 'lead', 'Change the number of rows or columns (' + env.rows + ' \u00d7 ' + env.columns + ' now). New rows and columns extend the map\u2019s edge ground; if the map gets smaller, you choose which part to keep.'));
                var fields = el('div', 'fields');
                function field(id, text, val) {
                    var wrap = el('div', 'fld'), label = el('label', '', text), input = el('input');
                    label.htmlFor = id; input.id = id; input.type = 'text'; input.setAttribute('inputmode', 'numeric'); input.setAttribute('autocomplete', 'off'); input.maxLength = 6;
                    input.value = String(val);
                    wrap.appendChild(label); wrap.appendChild(input); fields.appendChild(wrap);
                    input.addEventListener('input', update);
                    input.addEventListener('keydown', function (e) { if (e.key === 'Enter' && !ctl.buttons.resize.disabled) { e.preventDefault(); ctl.buttons.resize.click(); } });
                    return input;
                }
                rowsIn = field('resize-rows', 'Rows', env.rows);
                fields.appendChild(el('span', 'times', '\u00d7')).setAttribute('aria-hidden', 'true');
                columnsIn = field('resize-columns', 'Columns', env.columns);
                liveEl = el('div', 'live'); liveEl.setAttribute('aria-live', 'polite');
                fields.appendChild(liveEl);
                body.appendChild(fields);
                noteBox = el('div'); noteBox.setAttribute('aria-live', 'polite');
                body.appendChild(noteBox);
            },
            buttons: [
                { id: 'cancel', text: 'Cancel', ghost: true },
                { id: 'resize', text: 'Resize map', keepOpen: true, onClick: function (c) { var r = Sizes.wholeNumber(rowsIn.value), col = Sizes.wholeNumber(columnsIn.value); if (r === null || col === null) return; c.close(); env.onResize(r, col); } }
            ]
        });
        update();
        return ctl;
    }

    var OPEN_SORTS = [
        { id: 'default', label: 'Originals, then plays' },
        { id: 'plays', label: 'Most played' },
        { id: 'name', label: 'Name' },
        { id: 'size', label: 'Size' }
    ];

    // The Open a map dialog: open a .lvl file from your device, or pick any current map from the game's library as a starting point (opened under a free numbered copy name).
    // env: {doc, dialogs, store, numbered(name) -> freeName, onDisk(), onChoose(bytes, name)}
    function openMap(env) {
        var doc = env.doc, store = env.store, sort = 'default', query = '', picked = '', shown = [], byFile = Object.create(null), loading = false;
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        var searchIn, sortBtns = {}, listBox, copyNote, countEl, ctl;

        function sortEntries(items) {
            var out = items.slice();
            function byName(a, b) { var x = a.name.toLowerCase(), y = b.name.toLowerCase(); return x < y ? -1 : x > y ? 1 : a.file < b.file ? -1 : a.file > b.file ? 1 : 0; }
            out.sort(function (a, b) {
                if (sort === 'name') return byName(a, b);
                if (sort === 'size') return (a.width * a.height) - (b.width * b.height) || byName(a, b);
                if (sort === 'plays') return (b.plays - a.plays) || byName(a, b);
                if (a.original !== b.original) return a.original ? -1 : 1;
                return a.original ? a.order - b.order : (b.plays - a.plays) || byName(a, b);
            });
            return out;
        }

        function filterEntries(items) {
            var words = String(query || '').toLowerCase().split(/\s+/).filter(function (w) { return w; });
            if (!words.length) return items.slice();
            return items.filter(function (m) {
                var hay = (m.name + ' ' + (m.description || '')).toLowerCase();
                return words.every(function (w) { return hay.indexOf(w) !== -1; });
            });
        }

        function metaLine(m) {
            var parts = [];
            if (m.players > 0) parts.push(m.players + ' players');
            if (m.minutes > 0) parts.push(m.minutes + ' min');
            if (m.width > 0 && m.height > 0) parts.push(m.width + '\u00d7' + m.height);
            if (m.plays > 0) parts.push(Sizes.number(m.plays) + (m.plays === 1 ? ' play' : ' plays'));
            return parts.length ? parts.join(' \u00b7 ') : (m.original ? 'Shipped with the game' : 'Community map');
        }

        function refreshSelection() {
            var nodes = listBox.querySelectorAll('[data-file]'), m = byFile[picked] || null;
            for (var i = 0; i < nodes.length; i++) {
                var on = nodes[i].getAttribute('data-file') === picked;
                nodes[i].setAttribute('aria-selected', on ? 'true' : 'false');
                nodes[i].setAttribute('tabindex', on ? '0' : '-1');
                nodes[i].classList.toggle('sel', on);
            }
            if (!loading) {
                copyNote.className = 'open-copy-note';
                copyNote.textContent = m ? 'Opens as a new copy (\u201c' + env.numbered(m.name) + '\u201d) so you can edit and save it right away.' : 'Select a game map above to use it as a starting point.';
            }
            if (ctl) ctl.setDisabled('use', loading || !m);
        }

        function renderList() {
            var all = store ? store.libraryList() : [];
            byFile = Object.create(null);
            all.forEach(function (m) { byFile[m.file] = m; });
            searchIn.placeholder = 'Search ' + all.length + ' maps by name or description\u2026';
            OPEN_SORTS.forEach(function (s) { sortBtns[s.id].classList.toggle('on', s.id === sort); sortBtns[s.id].setAttribute('aria-pressed', s.id === sort ? 'true' : 'false'); });
            shown = sortEntries(filterEntries(all));
            if (!picked || !shown.some(function (m) { return m.file === picked; })) picked = shown.length ? shown[0].file : '';
            listBox.textContent = '';
            countEl.textContent = shown.length === all.length ? all.length + ' maps shown' : shown.length + ' of ' + all.length + ' maps shown';
            if (!shown.length) {
                listBox.appendChild(el('p', 'open-empty', query ? 'No map matches \u201c' + query.trim() + '\u201d.' : 'No maps available.'));
                refreshSelection();
                return;
            }
            shown.forEach(function (m) {
                var row = el('div', 'open-row' + (m.file === picked ? ' sel' : ''));
                row.setAttribute('role', 'option');
                row.setAttribute('data-file', m.file);
                row.setAttribute('aria-selected', m.file === picked ? 'true' : 'false');
                row.setAttribute('tabindex', m.file === picked ? '0' : '-1');
                if (m.thumb) {
                    var img = el('img', 'open-thumb');
                    img.src = m.thumb; img.alt = ''; img.width = 48; img.height = 48; img.loading = 'lazy';
                    row.appendChild(img);
                } else row.appendChild(el('span', 'open-nopic'));
                var mid = el('div', 'open-mid'), t1 = el('div', 'open-nm', m.name);
                if (m.original) t1.appendChild(el('span', 'open-orig', 'Original'));
                mid.appendChild(t1);
                mid.appendChild(el('div', 'open-meta', metaLine(m)));
                row.appendChild(mid);
                row.addEventListener('click', function () { if (loading) return; picked = m.file; refreshSelection(); });
                row.addEventListener('dblclick', function () { if (loading) return; picked = m.file; refreshSelection(); choosePicked(); });
                listBox.appendChild(row);
            });
            refreshSelection();
        }

        function choosePicked() {
            var m = byFile[picked];
            if (!m || loading || !store) return;
            loading = true;
            copyNote.className = 'open-copy-note';
            copyNote.textContent = 'Loading \u201c' + m.name + '\u201d\u2026';
            ctl.setDisabled('use', true);
            store.fetchMap(m.file, function (err, bytes) {
                if (ctl.closed) return;
                loading = false;
                if (err || !bytes) {
                    copyNote.className = 'open-copy-note bad';
                    copyNote.textContent = err || 'The map file could not be downloaded.';
                    ctl.setDisabled('use', false);
                    return;
                }
                var name = env.numbered(m.name);
                ctl.close();
                env.onChoose(bytes, name);
            });
        }

        ctl = env.dialogs.open({
            title: 'Open a map', cls: 'open-dlg', focus: '#open-disk-btn',
            build: function (body) {
                var diskSec = el('div', 'open-sec');
                diskSec.appendChild(el('div', 'open-lbl', 'From your device'));
                var diskBtn = el('button', 'btn full', 'Open a .lvl file from your device\u2026');
                diskBtn.type = 'button'; diskBtn.id = 'open-disk-btn';
                diskBtn.addEventListener('click', function () { ctl.close(); env.onDisk(); });
                diskSec.appendChild(diskBtn);
                diskSec.appendChild(el('p', 'open-sub', 'Or pick an existing game map below as a starting point.'));
                body.appendChild(diskSec);

                var libSec = el('div', 'open-sec');
                libSec.appendChild(el('div', 'open-lbl', 'Start from a game map'));
                var bar = el('div', 'open-bar');
                searchIn = el('input', 'open-q');
                searchIn.type = 'search'; searchIn.autocomplete = 'off'; searchIn.spellcheck = false;
                searchIn.setAttribute('aria-label', 'Search maps by name or description');
                searchIn.addEventListener('input', function () { query = searchIn.value; renderList(); });
                bar.appendChild(searchIn);
                var sorts = el('div', 'open-sorts');
                sorts.setAttribute('role', 'group'); sorts.setAttribute('aria-label', 'Sort maps by');
                OPEN_SORTS.forEach(function (s) {
                    var b = el('button', 'open-chip', s.label);
                    b.type = 'button';
                    b.addEventListener('click', function () { sort = s.id; renderList(); });
                    sortBtns[s.id] = b;
                    sorts.appendChild(b);
                });
                bar.appendChild(sorts);
                libSec.appendChild(bar);

                listBox = el('div', 'open-list');
                listBox.setAttribute('role', 'listbox'); listBox.setAttribute('aria-label', 'Game maps');
                listBox.addEventListener('keydown', function (e) {
                    if (!shown.length || loading) return;
                    var idx = -1;
                    for (var i = 0; i < shown.length; i++) if (shown[i].file === picked) { idx = i; break; }
                    if (e.key === 'ArrowDown') { e.preventDefault(); idx = Math.min(shown.length - 1, idx + 1); }
                    else if (e.key === 'ArrowUp') { e.preventDefault(); idx = Math.max(0, idx - 1); }
                    else if (e.key === 'Enter' && idx >= 0) { e.preventDefault(); choosePicked(); return; }
                    else return;
                    picked = shown[idx].file;
                    refreshSelection();
                    var sel = listBox.querySelector('[data-file].sel');
                    if (sel) { sel.focus(); if (sel.scrollIntoView) sel.scrollIntoView({ block: 'nearest' }); }
                });
                libSec.appendChild(listBox);

                copyNote = el('p', 'open-copy-note');
                copyNote.setAttribute('aria-live', 'polite');
                libSec.appendChild(copyNote);
                countEl = el('div', 'open-count');
                libSec.appendChild(countEl);
                body.appendChild(libSec);
            },
            buttons: [
                { id: 'cancel', text: 'Cancel', ghost: true },
                { id: 'use', text: 'Use as starting point', keepOpen: true, onClick: function () { choosePicked(); } }
            ]
        });
        if (ctl.buttons.use) ctl.buttons.use.className += ' gold';
        if (store) {
            store.loadLibrary();
            if (store.libraryState() === 'pending') store.onLibrary(function () { if (!ctl.closed) renderList(); });
        }
        renderList();
        return ctl;
    }

    return { create: create, newMap: newMap, resizeMap: resizeMap, openMap: openMap, EXTENDED_NOTE: EXTENDED_NOTE, KIND_WORDS: KIND_WORDS };
});
