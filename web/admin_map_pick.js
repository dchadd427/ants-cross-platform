// The list of the server's maps on the admin page (docs/MAP_SUBMISSIONS.md, "Bot matches on the admin page"): a box over the page (a sheet from the bottom on a phone) with a search, chips that narrow the list, the three random
// picks, the original's six, the players' maps and the library in letters, one row for each map with its size, its seats, its minutes and what a room can do with it. A map that a room cannot play is dim, cannot be chosen and says
// why. The section Bot matches uses it to choose the map of a batch (Start now and every schedule) and to add a map to a list of the rotation; the maps manager of the admin page is to use the same rows. It is the second file
// of that section (admin_bots.js is loaded after it) and is given the page's own helpers (the one object `h` of make()), so a word is made in one way in both. Every word that comes from the server (a map's name) goes in as text
// (`typed`: marked data-untrusted), never as markup. ES5 and no module system, as the page: a browser gets the global AntsAdminMapPick, node gets module.exports (tests/scripts/web_admin_check.js runs it with the page).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsAdminMapPick = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var MAX_ROWS = 40;                                                // a search shows this many rows at most, and says how many more there are (the box holds a few hundred rows at the most)
    var GROUP_WORD = { original: 'Original', library: 'Library', players: 'Players’ map' };
    var ISLANDS = 'ISLANDS.LVL';
    var FOCUSABLE = 'button, input, a';

    function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }
    function letterOf(name) { var c = String(name).charAt(0).toUpperCase(); return c >= 'A' && c <= 'Z' ? c : '#'; }
    function byName(a, b) { var x = a.name.toLowerCase(), y = b.name.toLowerCase(); return x < y ? -1 : x > y ? 1 : 0; }
    function sizeWords(m) { return m.width + ' x ' + m.height; }
    // "31 x 31 · 4 players · 15 min": a map in one line
    function mapMeta(m) { return sizeWords(m) + ' · ' + plural(m.players, 'player', 'players') + ' · ' + (m.minutes ? m.minutes + ' min' : 'time limit of its own'); }
    // the words of the reason that a map cannot be played, as a sentence
    function cannotWords(m) { return 'Cannot be played: ' + (m.reason || 'a room cannot play it now') + '.'; }

    // The rows that the search of a text finds in the maps: the names that contain it, the one that starts with it first, the shorter name before the longer, then A to Z
    function search(maps, text) {
        var q = String(text).replace(/^\s+|\s+$/g, '').toLowerCase();
        if (!q) return [];
        var hits = maps.filter(function (m) { return m.name.toLowerCase().indexOf(q) >= 0; });
        hits.sort(function (a, b) {
            return (a.name.toLowerCase().indexOf(q) === 0 ? 0 : 1) - (b.name.toLowerCase().indexOf(q) === 0 ? 0 : 1) || a.name.length - b.name.length || byName(a, b);
        });
        return hits;
    }

    function make(h) {
        var doc = h.doc, el = h.el, add = h.add, button = h.button, typed = h.typed, questionBox = h.questionBox, inertOthers = h.inertOthers, clearInert = h.clearInert;
        var shown = null;                                              // the box that is open: { node, close, refresh }

        function close() {
            if (!shown) return;
            var s = shown;
            shown = null;
            s.close();
        }

        // o: { host, title, mode: 'choose' | 'add', rules, current, cap, randomMax, doneLabel, maps(), randoms(), rotFiles(), checking(), isIn(file), onPick(m), onClose() }
        // 'choose' picks one map (or one of the random picks) and closes; 'add' keeps the box open, so that a list can take several maps in one visit, and its one button, Done, ends it.
        function open(o) {
            close();
            var q = '', chip = 'all', openLetter = '', warnFile = null, note = '', alive = true;
            var maps = function () { return o.maps(); };
            var dlg = el('div', 'dlg mp'), head = el('div', 'mp-hd'), title = el('h2', null, o.title), wrap = el('div', 'mp-wrap'), top = el('div', 'mp-top'), list = el('div', 'mp-list');
            var inw = el('div', 'inw'), inp = el('input', 'tinput mp-q'), chips = el('div', 'chips mp-chips'), count = el('div', 'mp-count'), added = el('div', 'mp-added');
            title.id = 'mp-title';
            title.tabIndex = -1;
            dlg.setAttribute('role', 'dialog');
            dlg.setAttribute('aria-modal', 'true');
            dlg.setAttribute('aria-labelledby', 'mp-title');
            inp.type = 'search';
            inp.id = 'mp-q';
            inp.setAttribute('aria-label', 'Search the maps by name');
            inp.setAttribute('autocomplete', 'off');
            added.setAttribute('role', 'status');
            var clear = button('×', 'mp-clear', function () { q = ''; inp.value = ''; clear.hidden = true; draw(); inp.focus(); });
            clear.setAttribute('aria-label', 'Clear the search');
            clear.hidden = true;
            add(inw, inp, clear);

            function rotTags(file) {                                  // the lists of the rotation that hold a map, as the small tags of its row
                var lists = o.rotFiles ? o.rotFiles() : {}, out = [];
                if (lists['highest-score'] && lists['highest-score'][file]) out.push(el('span', 'rtag hs', 'Highest score'));
                if (lists['187'] && lists['187'][file]) out.push(el('span', 'rtag k187', '187'));
                return out;
            }
            var CHIPS = [['all', 'All', function () { return true; }], ['original', 'Original', function (m) { return m.group === 'original'; }], ['players', 'Players’', function (m) { return m.group === 'players'; }],
                ['library', 'Library', function (m) { return m.group === 'library'; }], ['rotation', 'Rotation', function (m) { return rotTags(m.file).length > 0; }], ['long', 'Over ' + (o.randomMax || 30) + ' min', function (m) { return m.random === false; }]];
            function inChip(m) { for (var i = 0; i < CHIPS.length; i++) if (CHIPS[i][0] === chip) return CHIPS[i][2](m); return true; }
            function drawChips() {
                var all = maps();
                chips.textContent = '';
                CHIPS.forEach(function (c) {
                    var b = el('button', 'chip' + (c[0] === chip ? ' on' : ''), c[1] + ' ' + all.filter(c[2]).length);
                    b.type = 'button';
                    b.setAttribute('aria-pressed', c[0] === chip ? 'true' : 'false');
                    b.addEventListener('click', function () { chip = c[0]; openLetter = ''; drawChips(); draw(); });
                    add(chips, b);
                });
            }

            function pick(m) {
                o.onPick(m);
                if (o.mode === 'add') { note = 'Added ' + m.name + '.'; draw(); }
            }
            function row(m, showGroup) {
                var cannot = m.playable === false, inList = o.mode === 'add' && o.isIn(m.file), asked = o.mode === 'add' && (m.random === false || (o.rules === '187' && m.file === ISLANDS));
                var off = cannot || inList, b = el('button', 'mrow' + (off ? ' dis' : '') + (m.file === o.current ? ' on' : '')), left = el('span', 'mrl'), tags = el('span', 'mtags');
                b.type = 'button';
                if (off) b.setAttribute('aria-disabled', 'true');
                if (o.mode === 'choose') b.setAttribute('aria-pressed', m.file === o.current ? 'true' : 'false');
                add(left, typed('span', 'mn', m.name), el('span', 'ms', mapMeta(m)));
                if (showGroup) add(tags, el('span', 'rtag team', GROUP_WORD[m.group] || m.group));
                rotTags(m.file).forEach(function (t) { add(tags, t); });
                if (m.random === false) add(tags, el('span', 'tg warn', 'Over ' + (o.randomMax || 30) + ' min'));
                if (m.places > 1) add(tags, el('span', 'rtag team', m.places + ' places'));
                if (m.checked === false) add(tags, el('span', 'tg mute', 'Checking'));
                if (inList) add(tags, el('span', 'tg mute', 'In the list'));
                if (m.file === o.current) add(tags, el('span', 'tg active', 'Chosen'));
                if (tags.firstChild) add(left, tags);
                if (cannot) add(left, el('span', 'mr', cannotWords(m)));
                add(b, left);
                b.addEventListener('click', function () {
                    if (off) return;
                    if (asked) { warnFile = m.file; draw(); return; }
                    pick(m);
                });
                var one = el('div', 'mrw');
                add(one, b);
                if (warnFile === m.file) {
                    var islands = o.rules === '187' && m.file === ISLANDS, w = el('div', 'mwarn');
                    w.setAttribute('role', 'alert');
                    add(w, el('div', 'mwt', islands ? 'ISLANDS is not played in 187.' : m.name + ' plays up to ' + m.minutes + ' minutes.'),
                        el('div', 'mwp', islands ? 'Its water joins no two hills, so nobody could be struck there: a 187 match on it would run to its time limit with no kills. The list draws it as often as any other map of the list. Put it in the 187 list anyway?'
                            : 'A match on it can run that long and holds one of the ' + (o.cap || 8) + ' places for bot matches all that time. The list draws it as often as any other map of the list. Put it in the list anyway?'),
                        add(el('div', 'mwb'), button('Not now', 'ghost', function () { warnFile = null; draw(); }), button('Add anyway', 'ok', function () { warnFile = null; pick(m); })));
                    add(one, w);
                }
                return one;
            }
            function randomRow(r) {
                var b = el('button', 'mrow rand' + (o.current === r[0] ? ' on' : '')), left = el('span', 'mrl');
                b.type = 'button';
                b.setAttribute('aria-pressed', o.current === r[0] ? 'true' : 'false');
                add(left, el('span', 'mn', r[1]), el('span', 'ms', r[2]));
                if (o.current === r[0]) add(left, add(el('span', 'mtags'), el('span', 'tg active', 'Chosen')));
                add(b, left);
                b.addEventListener('click', function () { o.onPick({ file: r[0], random: true }); });
                return b;
            }
            function heading(text, n) { return add(el('div', 'mgh'), el('b', null, text), el('span', null, n)); }
            function group(title, ms) {
                if (!ms.length) return;
                ms = ms.slice().sort(function (a, b) { return (a.playable === false ? 1 : 0) - (b.playable === false ? 1 : 0); });          // (the maps that cannot be played come last)
                add(list, heading(title, plural(ms.length, 'map', 'maps')));
                ms.forEach(function (m) { add(list, row(m, false)); });
            }
            function checkingWords(all) {
                var n = all.filter(function (m) { return m.checked === false; }).length;
                return n ? ' Checking ' + plural(n, 'map', 'maps') + '…' : '';
            }
            function draw() {
                if (!alive) return;
                var all = maps(), keep = list.scrollTop;
                inp.setAttribute('placeholder', 'Search ' + all.length + ' maps by name');
                list.textContent = '';
                add(list, chips, count);
                added.textContent = note;
                added.hidden = !note;
                var pool = all.filter(inChip), qq = q.replace(/^\s+|\s+$/g, '').toLowerCase();
                if (qq) {
                    var hits = search(pool, qq);
                    count.textContent = hits.length ? plural(hits.length, 'map matches', 'maps match') + ' “' + qq + '”' + (hits.length > MAX_ROWS ? '. Showing the first ' + MAX_ROWS + ': type more of the name to narrow it down.' : '.') : 'No map matches “' + qq + '”. Check the spelling, or choose All.';
                    hits.slice(0, MAX_ROWS).forEach(function (m) { add(list, row(m, true)); });
                    list.scrollTop = keep;
                    return;
                }
                var playable = pool.filter(function (m) { return m.playable !== false; }).length;
                if (chip === 'all') count.textContent = plural(pool.length, 'map', 'maps') + ' on this server' + (playable < pool.length ? ', ' + playable + ' that a room can play' : '') + '. Search by name, or open a group.' + checkingWords(pool);
                else if (chip === 'library') count.textContent = plural(pool.length, 'map', 'maps') + ' of the library, A to Z. Open a letter.' + checkingWords(pool);
                else if (chip === 'rotation') count.textContent = plural(pool.length, 'map', 'maps') + ' are in the rotation lists now.';
                else if (chip === 'long') count.textContent = plural(pool.length, 'map', 'maps') + ' play over ' + (o.randomMax || 30) + ' minutes.';
                else count.textContent = plural(pool.length, 'map', 'maps') + '.';
                if (o.mode === 'choose' && chip === 'all') {
                    add(list, heading('Random pick', ''));
                    o.randoms().forEach(function (r) { add(list, randomRow(r)); });
                }
                if (chip === 'all') {
                    group('The original’s six', pool.filter(function (m) { return m.group === 'original'; }));
                    group('Players’ maps (approved)', pool.filter(function (m) { return m.group === 'players'; }));
                } else if (chip === 'original' || chip === 'players') group(chip === 'original' ? 'The original’s six' : 'Players’ maps (approved)', pool);
                if (chip === 'all' || chip === 'library') {
                    var lib = pool.filter(function (m) { return m.group === 'library'; }).sort(byName), letters = {}, order = [];
                    lib.forEach(function (m) { var L = letterOf(m.name); if (!letters[L]) { letters[L] = []; order.push(L); } letters[L].push(m); });
                    order.sort(function (a, b) { return a === '#' ? -1 : b === '#' ? 1 : a < b ? -1 : 1; });
                    add(list, heading('The library, A to Z', plural(lib.length, 'map', 'maps')));
                    order.forEach(function (L) {
                        var hd = el('button', 'mlet' + (openLetter === L ? ' open' : ''));
                        hd.type = 'button';
                        hd.setAttribute('aria-expanded', openLetter === L ? 'true' : 'false');
                        add(hd, el('b', null, L), el('span', null, plural(letters[L].length, 'map', 'maps')), el('i', 'car'));
                        hd.addEventListener('click', function () { openLetter = openLetter === L ? '' : L; draw(); });          // (one letter is open at a time: the box never holds more than a few dozen rows)
                        add(list, hd);
                        if (openLetter === L) letters[L].forEach(function (m) { add(list, row(m, false)); });
                    });
                } else if (chip === 'rotation' || chip === 'long') pool.slice().sort(byName).forEach(function (m) { add(list, row(m, true)); });
                list.scrollTop = keep;
            }
            inp.addEventListener('input', function () { q = inp.value; clear.hidden = !q; draw(); });
            add(top, inw, added);
            add(wrap, top, list);

            var cancel = button('Cancel', 'ghost', function () { close(); });
            var done = o.doneLabel ? button(o.doneLabel, 'ok', function () { close(); }) : null;          // (where several maps can be added, one button ends it)
            add(head, title);
            add(dlg, head, wrap, add(el('div', 'dlg-foot'), done ? null : cancel, done));
            var shell = questionBox(dlg, done || cancel, done || cancel, close);
            // the keys are heard on the document: Escape closes the box, and Tab goes round inside it (the page behind is not reachable): from the last control to the first, and back
            function onKey(e) {
                if (e.key === 'Escape') { close(); return; }
                if (e.key !== 'Tab') return;
                var items = Array.prototype.slice.call(dlg.querySelectorAll(FOCUSABLE)).filter(function (n) { return !n.disabled && !n.hidden; }), now = doc.activeElement;          // (a NodeList has no filter)
                if (!items.length) return;
                var at = items.indexOf(now);
                if (at < 0 || (e.shiftKey && at === 0) || (!e.shiftKey && at === items.length - 1)) { e.preventDefault(); items[e.shiftKey ? items.length - 1 : 0].focus(); }
            }
            add(o.host, shell.node);
            inertOthers(shell.node);
            doc.addEventListener('keydown', onKey, true);
            drawChips();
            draw();
            shown = {
                node: shell.node,
                close: function () {
                    alive = false;
                    doc.removeEventListener('keydown', onKey, true);
                    if (shell.node.parentNode) shell.node.parentNode.removeChild(shell.node);
                    clearInert();
                    if (o.onClose) o.onClose();
                },
                refresh: function () { drawChips(); draw(); }
            };
            if (typeof matchMedia === 'function' && matchMedia('(pointer: fine)').matches) inp.focus(); else title.focus();          // (a phone's keyboard would cover half of the sheet: the search is focused only where there is a mouse)
        }

        return { open: open, close: close, isOpen: function () { return shown !== null; }, refresh: function () { if (shown) shown.refresh(); } };
    }

    return { make: make, mapMeta: mapMeta, sizeWords: sizeWords, cannotWords: cannotWords, search: search, letterOf: letterOf, MAX_ROWS: MAX_ROWS };
});
