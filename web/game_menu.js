// The settings menu of the game page (docs/GAME_MENU.md; the game's side: src/ants_app/application_game_menu.cpp and its one table of rows, game_menu.hpp).
// Esc with nothing to cancel, or a Settings button, opens a panel over the running game: Sound, Video, Controls, Game, Resume and Leave game.
//   * The rows come from the game as one JSON text (Module._ants_menu_describe): this script draws what the table says. A row's value is read with Module._ants_menu_get and changed with Module._ants_menu_set;
//     Fullscreen, Picture shape and Fullscreen mouse are the page's own (its buttons are clicked: one function for the selector, the address and the stored choice). The Controls list is data of this file.
//   * The panel is a child of the stage (it goes fullscreen with it), fixed over the window. The pointer lock is let go while it is up and asked for again when it closes; the page behind it is inert; Tab stays
//     inside; Esc closes it (in the question of Leave game it means stay; a held key does nothing); the game is told (Module._ants_menu_open) and takes no input meanwhile; a click on the dimmed area does nothing;
//     a dialog that needs an answer closes it (ask 101). Leave game asks in the words of what it will do (ask 102). A recording has no Game section.
//   * It makes its own Settings buttons and fetches its own stylesheet; every text goes in with textContent. ES5, no module system: a page gets the global AntsGameMenu, node gets module.exports
//     (tests/scripts/web_game_menu_check.js runs it on a strict fake of the DOM).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsGameMenu = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var POLL_MS = 250;
    var FOCUSABLE = 'button:not([disabled]), input:not([disabled])';
    var CSS_FILE = 'game_menu.css';
    var USED = /^(Tab|Enter| |Spacebar|Arrow(Left|Right|Up|Down)|Home|End)$/; // the keys of the panel's own controls (Esc is handled before)

    // The Controls list: what the game's keys and the mouse do (docs/CONTROLS.md "Camera & Hotkeys"). [keys, what]: the mouse and touch lines name their input in words, a key line is a list of keys (a word that
    // starts with ~ is the text between two keys); `watch` leaves out what a recording ignores.
    var CONTROLS = {
        mouse: [['Left click', 'Select, order'], ['Right click', 'Special order, move'], ['Drag', 'Rubber band'], ['Wheel', 'Zoom'], ['Middle button', 'Zoom 1'], ['Screen edge', 'Scroll the map']],
        mouseWatch: [['Wheel', 'Zoom'], ['Middle button', 'Zoom 1'], ['Screen edge', 'Scroll the map'], ['Minimap', 'Move the view']],
        touch: [['Tap', 'Select or order'], ['Hold, then lift', 'Right click'], ['Drag', 'Rubber band'], ['Two fingers', 'Move the map'], ['Pinch', 'Zoom']],
        touchWatch: [['Two fingers', 'Move the map'], ['Pinch', 'Zoom'], ['Minimap', 'Move the view']],
        keys: [[['Esc'], 'Deselect; then this menu'], [['F1'], 'Quick help'], [['F9', '~\u2013', 'F12'], 'Quick chat', 'watch'], [['Enter'], 'Send chat', 'watch'], [['Ctrl', 'A'], 'Select all ants', 'watch'],
               [['Ctrl', 'H'], 'Home hill', 'watch'], [['Ctrl', 'N', '~/', 'P'], 'Next / previous ant', 'watch'], [['Ctrl', 'S'], 'Stop', 'watch'], [['Ctrl', 'L'], 'Hit point numbers'],
               [['Ctrl', 'O'], 'Original options'], [['Ctrl', 'Q'], 'Quit question'], [['F2', '~/', 'F3'], 'Vote (network match)', 'watch']],
        note: 'On a Mac, Cmd works as Ctrl.'
    };

    function forPage(page) {
        var doc = page.doc, win = page.win || (doc && doc.defaultView) || {}, stage = page.stage, canvas = page.canvas;
        var SHAPES = page.shapes.names(); // the places of the picture's choices: the page's one table of shapes (web/shapes.js; game_menu.hpp's kPicture row is made from the game's, in the same order)
        var info = null; // the game's table, once read
        var scrim = null, panel = null, ask = null, state = null;
        var dimmed = [], opener = null, opening = false, styleReady = false, styleFailed = 0, wasLocked = false, behind = [], removers = [], shown = { askKind: -1 };
        var buttons = [];

        // ---- the game's side: every call is guarded (a game that is not there answers -1) ----
        function mod() { try { return page.module ? page.module() : null; } catch (e) { return null; } }
        function call(name, a, b, c) {
            var m = mod();
            if (!m || typeof m[name] !== 'function') return -1;
            try { return m[name](a, b, c); } catch (e) { return -1; }
        }
        function get(what) { return call('_ants_menu_get', what); }
        function describe() {
            var m = mod();
            if (!m || typeof m._ants_menu_describe !== 'function' || typeof m.UTF8ToString !== 'function') return null;
            try { var text = m.UTF8ToString(m._ants_menu_describe()); return text ? JSON.parse(text) : null; } catch (e) { return null; }
        }
        function ready() { return !!(page.ready ? page.ready() : true); }

        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function touchy() { return !!win.antsFinger || !!(win.matchMedia && win.matchMedia('(hover: none)').matches); }
        function listen(target, type, fn, capture) { target.addEventListener(type, fn, !!capture); removers.push(function () { target.removeEventListener(type, fn, !!capture); }); }
        function indexOf(list, item) { for (var i = 0; i < list.length; i++) if (list[i] === item) return i; return -1; }
        function insertAfter(node, ref) { var parent = ref.parentNode, kids = parent.children; parent.insertBefore(node, kids[indexOf(kids, ref) + 1] || null); }

        // ---- what the game says now ----
        function where() { return get(100); } // -1: no match screen, 0: a match, 1: watched
        function answered() { return get(101) === 0; } // nothing needs an answer first
        function available() { return ready() && styleReady && where() >= 0 && answered(); }

        // ---- the page's own rows: its functions, by name ----
        function shapeNow() { var v = stage.getAttribute('data-aspect'); return Math.max(0, indexOf(SHAPES, v)); }
        function lockButton(on) { return doc.getElementById(on ? 'lock-on' : 'lock-off'); }
        var PAGE_ROWS = {
            picture: {
                has: function () { return !!doc.querySelector('button[data-aspect]'); },
                read: shapeNow,
                write: function (i) { var b = doc.querySelector('button[data-aspect="' + SHAPES[i] + '"]'); if (b) b.click(); }
            },
            lockmouse: {
                has: function () { return !!lockButton(true) && !touchy(); },
                read: function () { return lockButton(true).getAttribute('aria-checked') === 'true' ? 0 : 1; },
                write: function (i) { var b = lockButton(i === 0); if (b) b.click(); }
            }
        };

        // ---- the Settings buttons: the page's own look, made here ----
        function show(node, on) { node.hidden = !on; node.style.display = on ? '' : 'none'; } // (the page's .btn has a display of its own, which hidden alone does not beat)
        function makeButton(cls, id) {
            var b = el('button', 'btn sm gm-btn ' + cls, 'Settings');
            b.type = 'button';
            b.id = id;
            show(b, false);
            b.setAttribute('aria-haspopup', 'dialog');
            b.addEventListener('click', function (e) { open(e && e.detail === 0 ? b : null); }); // (a press with the keyboard has the focus given back to the button; a click or a tap gives it to the game)
            buttons.push(b);
            return b;
        }
        function placeButtons() {
            var menu = doc.getElementById('menu-btn'), bar = doc.getElementById('view-bar'), picwrap = doc.getElementById('picwrap');
            if (page.replay) {
                if (picwrap && picwrap.parentNode) picwrap.parentNode.insertBefore(makeButton('gm-bar', 'settings-btn-bar'), picwrap);
                return;
            }
            if (menu && menu.parentNode) insertAfter(makeButton('gm-wide', 'settings-btn'), menu);
            if (bar) bar.insertBefore(makeButton('gm-narrow', 'settings-btn-narrow'), bar.firstChild);
        }

        function refresh() {
            var up = available();
            for (var i = 0; i < buttons.length; i++) {
                var b = buttons[i];
                show(b, styleReady && ready());
                b.disabled = !up;
                b.title = up ? 'Settings: sound, picture, controls and Leave game' : 'Not now';
            }
            if (state && (where() < 0 || !answered())) close({ relock: false }); // a dialog needs its answer, or the match is over
            else if (state) {
                keepPointerFree(); // (a lock that came while the panel is up: the page's own handler stops the event before the panel's listener, so the poll catches it too)
                if (ask && get(102) !== shown.askKind) showAsk(false); // (the question follows Leave game: it may be redrawn here, it never leaves by itself)
            }
            if (state && state.fsButton) state.fsButton.textContent = state.fsWords[page.isFullscreen && page.isFullscreen() ? 1 : 0];
        }

        // ---- the panel ----
        function slider(row, labelId) {
            var wrap = el('div', 'gm-ctl gm-slide'), input = el('input'), out = el('span', 'gm-val');
            input.type = 'range';
            input.min = String(row.min);
            input.max = String(row.max);
            input.step = '1';
            input.setAttribute('aria-labelledby', labelId);
            var now = Math.max(row.min, Math.min(row.max, get(row.id)));
            input.value = String(now);
            out.textContent = String(now);
            out.setAttribute('aria-hidden', 'true');
            function fill() { input.setAttribute('style', '--f:' + (input.value - row.min) * 100 / (row.max - row.min) + '%'); }
            fill();
            input.addEventListener('input', function () {
                fill();
                out.textContent = String(input.value);
                if (row.live) call('_ants_menu_set', row.id, parseInt(input.value, 10), 0); // (heard while it is dragged, kept at the release)
            });
            input.addEventListener('change', function () { out.textContent = String(input.value); call('_ants_menu_set', row.id, parseInt(input.value, 10), 1); });
            wrap.appendChild(input);
            wrap.appendChild(out);
            return wrap;
        }

        // On / Off (value 1 is the first word) or a choice of words: radio buttons, as the page's selector has them
        function choice(row, labelId, read, write, values) {
            var group = el('div', 'gm-ctl gm-seg' + (row.words.length > 3 ? ' gm-seg4' : '')), items = [];
            group.setAttribute('role', 'radiogroup');
            group.setAttribute('aria-labelledby', labelId);
            function mark() { var now = read(); for (var i = 0; i < items.length; i++) { var on = values[i] === now; items[i].setAttribute('aria-checked', on ? 'true' : 'false'); items[i].classList.toggle('on', on); } }
            row.words.forEach(function (word, i) {
                var b = el('button', 'btn gm-opt', word);
                b.type = 'button';
                b.setAttribute('role', 'radio');
                b.addEventListener('click', function () { write(values[i]); mark(); });
                items.push(b);
                group.appendChild(b);
            });
            mark();
            return group;
        }

        function action(row, labelId) {
            var b = el('button', 'btn gm-act', row.words[0]);
            b.type = 'button';
            b.setAttribute('aria-labelledby', labelId);
            if (row.key === 'fullscreen') {
                state.fsButton = b;
                state.fsWords = row.words;
                b.textContent = row.words[page.isFullscreen && page.isFullscreen() ? 1 : 0];
                b.addEventListener('click', function () { if (page.isFullscreen()) page.leave(); else page.enter(); });
            } else {
                var which = row.key === 'quickhelp' ? 2 : 3;
                b.addEventListener('click', function () { close({ relock: false }); call('_ants_menu_do', which); });
            }
            return b;
        }

        function rowOf(row, n) {
            var line = el('div', 'gm-row gm-k-' + row.key + (row.kind === 'button' && !row.hint ? ' gm-btnrow' : '')), lab = el('div', 'gm-lab'), name = el('span', 'gm-name', row.label), id = 'gm-l-' + n, control;
            name.id = id;
            lab.appendChild(name);
            if (row.new) lab.appendChild(el('span', 'gm-new', 'NEW'));
            if (row.hint) lab.appendChild(el('div', 'gm-hint', row.hint));
            line.appendChild(lab);
            var own = row.page ? PAGE_ROWS[row.key] : null;
            if (row.page && (!own && row.kind !== 'button')) return null;
            if (own && !own.has()) return null;
            if (row.kind === 'slider') control = slider(row, id);
            else if (row.kind === 'switch') control = choice(row, id, function () { return get(row.id); }, function (v) { call('_ants_menu_set', row.id, v, 1); }, [1, 0]);
            else if (row.kind === 'choice') control = choice(row, id, own.read, own.write, row.words.map(function (w, i) { return i; }));
            else if (row.kind === 'button') control = action(row, id);
            else { control = el('div', 'gm-ctl gm-info', page.version || ''); }
            line.appendChild(control);
            return line;
        }

        function controlsList(watching) {
            var box = el('div', 'gm-keys'), touch = touchy();
            function group(title, list) {
                var grid = el('div', 'gm-kgrid'); // (two columns on a wide panel, the lines in pairs under one heading, as the picture has them)
                box.appendChild(el('h3', 'gm-gh', title));
                box.appendChild(grid);
                list.forEach(function (item) {
                    if (watching && item[2] === 'watch') return;
                    var line = el('div', 'gm-row gm-krow'), keys = el('div', 'gm-lab');
                    if (typeof item[0] === 'string') keys.appendChild(el('span', 'gm-input', item[0]));
                    else item[0].forEach(function (part) { keys.appendChild(part.charAt(0) === '~' ? el('span', 'gm-plus', part.slice(1)) : el('kbd', 'gm-kbd', part)); });
                    line.appendChild(keys);
                    line.appendChild(el('div', 'gm-ctl gm-what', item[1]));
                    grid.appendChild(line);
                });
            }
            if (touch) group('TOUCH', watching ? CONTROLS.touchWatch : CONTROLS.touch);
            else group('MOUSE', watching ? CONTROLS.mouseWatch : CONTROLS.mouse);
            group('KEYS', CONTROLS.keys);
            box.appendChild(el('p', 'gm-fine', 'View only. ' + CONTROLS.note));
            return box;
        }

        function fillPane(id) {
            var pane = state.pane, watching = state.watching;
            pane.textContent = '';
            state.fsButton = null;
            if (id === 'controls') { pane.appendChild(controlsList(watching)); return; }
            var n = 0;
            info.rows.forEach(function (row) { if (row.sec === id) { var line = rowOf(row, ++n); if (line) pane.appendChild(line); } });
        }

        function selectTab(id, focus) {
            state.tabs.forEach(function (t) {
                var on = t.section === id;
                t.button.setAttribute('aria-selected', on ? 'true' : 'false');
                t.button.setAttribute('tabindex', on ? '0' : '-1');
                t.button.classList.toggle('on', on);
                if (on) { state.pane.setAttribute('aria-labelledby', t.button.id); if (focus) t.button.focus(); }
            });
            fillPane(id);
        }

        function build() {
            var w = info.words, watching = where() === 1;
            state = { watching: watching, tabs: [], pane: null, fsButton: null, fsWords: ['', ''] };
            scrim = el('div', 'gm-scrim');
            scrim.id = 'game-menu';
            panel = el('div', 'gm-panel');
            panel.setAttribute('role', 'dialog');
            panel.setAttribute('aria-modal', 'true');
            panel.setAttribute('aria-labelledby', 'gm-title');
            var head = el('div', 'gm-head'), title = el('h2', 'gm-title banner', w.title);
            title.id = 'gm-title';
            head.appendChild(title);
            if (watching) head.appendChild(el('span', 'gm-tag', page.live ? w.tagLive : w.tagReplay));
            var esc = el('span', 'gm-esc'), kbd = el('kbd', 'gm-kbd', 'Esc');
            esc.appendChild(kbd);
            esc.appendChild(doc.createTextNode(' ' + w.esc));
            head.appendChild(esc);
            var x = el('button', 'btn gm-x', '\u00d7');
            x.type = 'button';
            x.setAttribute('aria-label', w.close);
            x.addEventListener('click', function () { close(); });
            head.appendChild(x);
            panel.appendChild(head);
            var tabs = el('div', 'gm-tabs');
            tabs.setAttribute('role', 'tablist');
            tabs.setAttribute('aria-orientation', touchy() ? 'horizontal' : 'vertical');
            info.sections.forEach(function (s, i) {
                var b = el('button', 'btn gm-tab', s.label);
                b.type = 'button';
                b.id = 'gm-tab-' + s.id;
                b.setAttribute('role', 'tab');
                b.setAttribute('aria-controls', 'gm-pane');
                b.addEventListener('click', function () { selectTab(s.id, false); });
                state.tabs.push({ section: s.id, button: b });
                tabs.appendChild(b);
            });
            panel.appendChild(tabs);
            state.pane = el('div', 'gm-pane');
            state.pane.id = 'gm-pane';
            state.pane.setAttribute('role', 'tabpanel');
            panel.appendChild(state.pane);
            var note = el('div', 'gm-note'), clock = el('span', 'gm-clock');
            clock.setAttribute('aria-hidden', 'true');
            note.appendChild(clock);
            note.appendChild(el('span', '', watching ? (page.live ? w.noteLive : w.noteReplay) : w.note));
            panel.appendChild(note);
            var btns = el('div', 'gm-btns');
            state.resume = el('button', 'btn gm-resume', w.resume);
            state.resume.type = 'button';
            state.resume.addEventListener('click', function () { close(); });
            state.leave = el('button', 'btn gm-leave' + (watching ? ' gm-back' : ''), watching ? w.back : w.leave);
            state.leave.type = 'button';
            state.leave.addEventListener('click', function () { showAsk(true); });
            btns.appendChild(state.resume);
            btns.appendChild(state.leave);
            panel.appendChild(btns);
            scrim.appendChild(panel);
            selectTab(info.sections[0].id, false);
        }

        // ---- Leave game ----
        function leaveNow() { close({ relock: false }); call('_ants_menu_do', 1); }
        // the table says which kinds ask ("asks") and in what words; a kind that asks nothing leaves when Leave game is clicked, and a poll never leaves: a question that is up stays as it is (or is drawn again) when the kind changes
        function showAsk(clicked) {
            var kind = get(102), words = info.words, found = null;
            words.ask.forEach(function (a) { if (a.kind === kind) found = a; });
            if (!found || !found.asks) { if (clicked) leaveNow(); return; }
            hideAsk(true);
            shown.askKind = kind;
            ask = el('div', 'gm-ask');
            ask.setAttribute('role', 'alertdialog');
            ask.setAttribute('aria-modal', 'true');
            ask.setAttribute('aria-labelledby', 'gm-ask-t');
            ask.setAttribute('aria-describedby', 'gm-ask-b');
            var t = el('h3', 'gm-ask-t', found.title), b = el('p', 'gm-ask-b', found.body), n = el('p', 'gm-ask-n'), clock = el('span', 'gm-clock');
            t.id = 'gm-ask-t';
            b.id = 'gm-ask-b';
            clock.setAttribute('aria-hidden', 'true');
            n.appendChild(clock);
            n.appendChild(el('span', '', words.deciding));
            var row = el('div', 'gm-ask-btns'), stay = el('button', 'btn gm-stay', words.stay), go = el('button', 'btn gm-leave', found.go);
            stay.type = 'button';
            go.type = 'button';
            stay.addEventListener('click', function () { hideAsk(); });
            go.addEventListener('click', leaveNow);
            row.appendChild(stay);
            row.appendChild(go);
            [t, b, n, row].forEach(function (c) { ask.appendChild(c); });
            panel.appendChild(ask);
            for (var c = 0; c < panel.children.length; c++) if (panel.children[c] !== ask) { panel.children[c].setAttribute('inert', ''); dimmed.push(panel.children[c]); } // (the rest of the panel waits for the answer)
            panel.setAttribute('data-asking', '1');
            stay.focus();
        }
        function hideAsk(quiet) {
            if (!ask) return;
            panel.removeChild(ask);
            ask = null;
            for (var d = 0; d < dimmed.length; d++) dimmed[d].removeAttribute('inert');
            dimmed = [];
            shown.askKind = -1;
            panel.removeAttribute('data-asking');
            if (!quiet) state.leave.focus();
        }

        // ---- open and close ----
        // The rest of the page takes no keyboard or pointer while the panel is up: every sibling on the way from the panel up to the body is inert
        function inertBehind() {
            behind = [];
            for (var node = scrim; node && node.parentNode && node !== doc.body; node = node.parentNode) {
                var kids = node.parentNode.children;
                for (var i = 0; i < kids.length; i++) {
                    var k = kids[i];
                    if (k === node || /^(script|link|style)$/i.test(k.tagName || '') || (k.hasAttribute && k.hasAttribute('inert'))) continue;
                    k.setAttribute('inert', '');
                    behind.push(k);
                }
            }
        }

        function focusables() {
            var root = ask || panel, found = [], all = root.querySelectorAll(FOCUSABLE);
            for (var i = 0; i < all.length; i++) {
                var n = all[i];
                if (n.getAttribute && n.getAttribute('tabindex') === '-1') continue;
                if (n.offsetParent !== null || n === doc.activeElement) found.push(n);
            }
            return found;
        }

        function onKey(e) {
            var key = e.key;
            if (key === 'Escape') {
                e.preventDefault();
                e.stopPropagation();
                if (e.repeat) return; // (a held Esc is not a second press)
                if (ask) hideAsk(); else close();
                return;
            }
            if (USED.test(key)) e.stopPropagation(); // (only the keys that the panel uses are its own; the others reach the page, which keeps the browser's reactions to Ctrl+S, F1 and the like off, and the game, which takes none while the panel is up; the lifts are left alone, or the game's window system reads the next press as a repeat)
            if (key === 'Tab') {
                var items = focusables();
                if (!items.length) return;
                var first = items[0], last = items[items.length - 1], root = ask || panel;
                if (e.shiftKey && (doc.activeElement === first || !root.contains(doc.activeElement))) { e.preventDefault(); last.focus(); }
                else if (!e.shiftKey && (doc.activeElement === last || !root.contains(doc.activeElement))) { e.preventDefault(); first.focus(); }
                return;
            }
            var at = -1;
            state.tabs.forEach(function (t, i) { if (t.button === doc.activeElement) at = i; });
            if (at >= 0 && !ask && /^(ArrowLeft|ArrowRight|ArrowUp|ArrowDown|Home|End)$/.test(key)) {
                var step = key === 'ArrowLeft' || key === 'ArrowUp' ? -1 : key === 'ArrowRight' || key === 'ArrowDown' ? 1 : 0, n = state.tabs.length;
                var to = key === 'Home' ? 0 : key === 'End' ? n - 1 : (at + step + n) % n;
                e.preventDefault();
                selectTab(state.tabs[to].section, true);
            }
        }

        // Fullscreen begins while the panel is up (its own row): the page asks for the lock then; the panel needs the free pointer
        function keepPointerFree() { if (page.isLocked && page.isLocked()) page.releaseLock(); }

        function open(from) {
            if (state || opening) return false;
            if (!available()) return false;
            info = info || describe();
            if (!info || !info.rows || !info.sections || !info.sections.length) return false;
            opening = true;
            opener = from || null;
            try {
                wasLocked = !!(page.isLocked && page.isLocked());
                build();
                stage.appendChild(scrim);
                inertBehind();
                call('_ants_menu_open', 1);
                if (page.releaseLock) page.releaseLock();
                listen(doc, 'keydown', onKey, true);
                ['pointerlockchange', 'webkitpointerlockchange', 'mozpointerlockchange'].forEach(function (name) { if (win.addEventListener) listen(win, name, keepPointerFree, true); }); // (the window in the capture phase, as the page's own handler: it stops the event before the document's)
                ['fullscreenchange', 'webkitfullscreenchange'].forEach(function (name) { listen(doc, name, function () { refresh(); }, false); });
                state.resume.focus();
            } finally { opening = false; }
            return true;
        }

        function close(opts) {
            if (!state) return;
            var again = !(opts && opts.relock === false);
            hideAsk(true);
            for (var i = 0; i < removers.length; i++) removers[i]();
            removers = [];
            if (scrim.parentNode) scrim.parentNode.removeChild(scrim);
            for (var j = 0; j < behind.length; j++) behind[j].removeAttribute('inert');
            behind = [];
            scrim = panel = state = null;
            call('_ants_menu_open', 0);
            if (again && page.requestLock && (wasLocked || (page.isFullscreen && page.isFullscreen()))) page.requestLock(); // (Resume is a click: the gesture that the browser wants)
            try {
                var back = opener && !opener.disabled && !opener.hidden ? opener : canvas;
                opener = null;
                if (back && back.focus) back.focus();
            } catch (e) { /* no focus to give */ }
        }

        function say(text) { var t = page.toast; if (!t) return; t.textContent = text; t.hidden = false; setTimeout(function () { t.hidden = true; }, 5000); }

        // ---- the style: fetched with the script, retried once; the buttons stay hidden until it is there ----
        function loadStyle() {
            if (!doc.head || !doc.createElement) { styleReady = true; return; }
            var link = doc.createElement('link');
            link.rel = 'stylesheet';
            link.href = CSS_FILE + (page.query || '');
            link.onload = function () { styleReady = true; refresh(); };
            link.onerror = function () {
                if (link.parentNode) link.parentNode.removeChild(link);
                if (++styleFailed < 2) setTimeout(loadStyle, 2000);
                else say('The settings menu could not load its style. Press Reset to try again.');
            };
            doc.head.appendChild(link);
        }

        placeButtons();
        win.antsGameMenuAsk = function () { open(null); }; // (the game's Esc with nothing to cancel: src/ants_app/application_game_menu.cpp)
        (page.every || function (fn, ms) { return setInterval(fn, ms); })(refresh, POLL_MS);
        loadStyle();
        return { open: open, close: close, isOpen: function () { return !!state; }, refresh: refresh, buttons: buttons, onKey: onKey, setStyleReady: function (v) { styleReady = v; refresh(); } };
    }

    return { forPage: forPage, POLL_MS: POLL_MS, CONTROLS: CONTROLS };
});
