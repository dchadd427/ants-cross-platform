// The picture's shape control, as the game page, the replay's Picture window and the settings menu have it: a button that shows the shape ("16:10 \u00b7 960 x 600") and a list of the rows of web/shapes.js's table under it
// (or over it, where the screen has no room), made by this script and not by the browser (docs/PLAY_IN_BROWSER.md, "Which shape"; docs/GAME_MENU.md).
//   * Why not a <select>: its list is the system's, drawn by the browser outside the page. In fullscreen it can fail to open, and under the pointer lock of a fullscreen page (the page moves its own cursor and hands the
//     replay's bar synthetic events: a script's click cannot open a native list) it never does. This list is a child of the control, so it is inside the fullscreen element, and a click, a tap or a key opens it.
//     The front page keeps its native dropdown (web/lobby.html: never fullscreen).
//   * create(shapes, opts): `shapes` is AntsShapes (the table). opts: doc, win (default: the document's), host (the element that gets the button and the list), id (the button's; the list's and the rows' follow it),
//     labelledBy (the id of the words that name it), describedBy (the id of the quiet line), className (more classes for the button), title, value (the shape shown), onPick(name) (called for a row that is not the
//     shown shape: the page changes the picture and calls set() with what it has now), now (a clock, for tests). It gives back { button, list, set(name), shape(), open(), close(refocus), isOpen(), destroy() }.
//   * Keys, on the button: Enter, Space, ArrowDown, ArrowUp and Alt+ArrowDown open the list (the shown shape is the marked row; nothing on a closed button changes the picture, on every browser alike); in the open list ArrowDown /
//     ArrowUp / Home / End move the mark, Enter and Space choose the marked row, a letter or a number marks the next row that starts with what was typed, Esc closes the list ALONE (the Picture window or the settings menu
//     stays: the key is taken in the window's capture phase, ahead of theirs), Tab closes it and moves on. A click elsewhere and a scroll close it; a resize puts it by its button again. The focus stays on the button
//     (aria-activedescendant names the marked row) and goes back to it when the list closes by a choice or by Esc. The pointer marks the row it is over (pointermove, which the replay bar's synthetic cursor makes too).
//   * Choosing the shape that is shown calls nothing (the page stores nothing: only a different shape is a choice, docs/PLAY_IN_BROWSER.md "Which shape").
// ES5, no module system: a page gets the global AntsShapePicker, node gets module.exports (tests/scripts/web_shape_picker_check.js runs it on a fake of the DOM).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsShapePicker = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var EDGE = 8;                    // the room that the list leaves at the screen's edge, CSS pixels
    var GAP = 4;                     // ... and between it and the button
    var TYPE_MS = 800;               // a letter typed within this time of the last one goes on the same word

    function inside(node, root) { for (var n = node; n; n = n.parentNode) if (n === root) return true; return false; }

    function create(shapes, opts) {
        var ROWS = shapes.rows, doc = opts.doc, win = opts.win || doc.defaultView, host = opts.host, id = opts.id;
        var shown = false, mark = -1, current = -1, typed = '', typedAt = 0, taken = {}, clock = opts.now || function () { return Date.now(); };
        var button = doc.createElement('button'), list = doc.createElement('ul'), options = [];
        button.type = 'button';
        button.id = id;
        button.className = 'pick-select' + (opts.className ? ' ' + opts.className : '');
        button.setAttribute('role', 'combobox');
        button.setAttribute('aria-haspopup', 'listbox');
        button.setAttribute('aria-expanded', 'false');
        button.setAttribute('aria-controls', id + '-list');
        if (opts.labelledBy) button.setAttribute('aria-labelledby', opts.labelledBy);
        if (opts.describedBy) button.setAttribute('aria-describedby', opts.describedBy);
        if (opts.title) button.title = opts.title;
        list.id = id + '-list';
        list.className = 'pick-list';
        list.setAttribute('role', 'listbox');
        if (opts.labelledBy) list.setAttribute('aria-labelledby', opts.labelledBy);
        list.hidden = true;
        ROWS.forEach(function (row) {
            var item = doc.createElement('li');
            item.id = id + '-o' + row.place;
            item.className = 'pick-opt';
            item.setAttribute('role', 'option');
            item.setAttribute('data-shape', row.name);
            item.setAttribute('aria-selected', 'false');
            item.textContent = row.text;
            list.appendChild(item);
            options.push(item);
        });
        host.insertBefore(button, host.firstChild);
        host.insertBefore(list, button.nextSibling);

        // what the page has now: the button says it and its row is the selected one
        function set(name) {
            current = shapes.placeOf(name);
            button.textContent = current < 0 ? '' : ROWS[current].text;
            options.forEach(function (item, i) { item.setAttribute('aria-selected', i === current ? 'true' : 'false'); });
        }

        // the marked row is scrolled into the list's own view (scrollIntoView would scroll the bar and the page too: they are outside the list's box, and a scroll closes the list)
        function reveal(item) {
            var top = item.offsetTop, bottom = top + item.offsetHeight;
            if (top < list.scrollTop) list.scrollTop = top;
            else if (bottom > list.scrollTop + list.clientHeight) list.scrollTop = bottom - list.clientHeight;
        }

        function setMark(i) {
            if (mark >= 0) options[mark].classList.remove('act');
            mark = i;
            if (i < 0) { button.removeAttribute('aria-activedescendant'); return; }
            options[i].classList.add('act');
            button.setAttribute('aria-activedescendant', options[i].id);
            reveal(options[i]);
        }

        // The list lies over the page (position: fixed, at the button's edge); the place of a fixed box is measured at 0, 0 first, because a bar that is moved with a transform or a container is the box's frame, not the screen
        function place() {
            var at = button.getBoundingClientRect(), wide = win.innerWidth, high = win.innerHeight;
            list.style.left = '0px';
            list.style.top = '0px';
            list.style.maxHeight = '';
            list.style.minWidth = at.width + 'px';
            var origin = list.getBoundingClientRect(), full = list.offsetHeight, across = list.offsetWidth;
            var below = high - at.bottom - GAP - EDGE, above = at.top - GAP - EDGE;
            var up = full > below && above > below;
            var height = Math.max(Math.min(full, up ? above : below), 0);
            var left = Math.max(EDGE, Math.min(at.left, wide - EDGE - across));
            var top = up ? at.top - GAP - height : at.bottom + GAP;
            list.style.maxHeight = height + 'px';
            list.style.left = (left - origin.left) + 'px';
            list.style.top = (top - origin.top) + 'px';
        }

        function away(e) { if (!inside(e.target, button) && !inside(e.target, list)) close(false); }
        function awayTouch(e) { if (e.pointerType === 'touch') away(e); }       // (a tap on an empty place makes no click on iOS; a finger is never locked, so its press is its own)
        function leave(e) { if (!e.target || !inside(e.target, list)) close(false); }
        function listen(on) {
            var call = on ? 'addEventListener' : 'removeEventListener';
            doc[call]('click', away, true);                       // (a click, at the document: a locked mouse's real presses go to the game's canvas, and the page hands the bar a synthetic click instead; the document sees that one, not the real one)
            doc[call]('pointerdown', awayTouch, true);
            win[call]('resize', place, false);                     // (the picture changes size while a choice is made, and the page says so with a resize: the list goes where its button now is)
            win[call]('scroll', leave, true);
        }
        function open() {
            if (shown) return;
            shown = true;
            list.hidden = false;
            button.setAttribute('aria-expanded', 'true');
            place();
            setMark(current >= 0 ? current : 0);
            listen(true);
        }
        function close(refocus) {
            if (!shown) return;
            shown = false;
            list.hidden = true;
            button.setAttribute('aria-expanded', 'false');
            setMark(-1);
            typed = '';
            listen(false);
            if (refocus) button.focus();
        }
        function choose(i) {
            close(true);
            if (i !== current && opts.onPick) opts.onPick(ROWS[i].name);
        }

        // typeahead: the next row (after the marked one) whose words start with what was typed; one letter typed again goes on to the next row with that letter
        function typeahead(letter, at) {
            typed = at - typedAt > TYPE_MS ? letter : typed + letter;
            typedAt = at;
            var word = typed.toLowerCase(), same = typed.split(letter).join('') === '', start = same ? mark + 1 : Math.max(mark, 0);
            if (same) word = letter.toLowerCase();
            for (var k = 0; k < ROWS.length; k++) {
                var i = (start + k) % ROWS.length;
                if (ROWS[i].text.toLowerCase().indexOf(word) === 0) return i;
            }
            return -1;
        }

        // The keys, in the window's capture phase (ahead of the Picture window's Esc, the menu's keys and the game's own): only the keys the control uses are taken, and only from its own button
        function onKey(e) {
            if (e.target !== button) return;
            var key = e.key, plain = !e.ctrlKey && !e.altKey && !e.metaKey, took = false;
            if (e.type === 'keyup') {
                if (taken[key]) { delete taken[key]; e.preventDefault(); e.stopImmediatePropagation(); }       // (the browser would press the button again at Space's lift)
                return;
            }
            var press = key === 'Enter' || key === ' ' || key === 'Escape';
            if (press && e.repeat) took = taken[key] === true;                                              // (a held key does it once: the repeats of a key that was taken are taken and do nothing)
            else if (!shown) {
                if ((plain && (key === 'Enter' || key === ' ' || key === 'ArrowDown' || key === 'ArrowUp')) || (key === 'ArrowDown' && e.altKey && !e.ctrlKey && !e.metaKey)) { open(); took = true; }       // (Alt+ArrowDown is the combobox's own key)
            } else if (key === 'Escape') { close(true); took = true; }
            else if (key === 'Tab') close(false);
            else if (plain && key === 'ArrowDown') { setMark(Math.min(mark + 1, ROWS.length - 1)); took = true; }
            else if (plain && key === 'ArrowUp') { setMark(Math.max(mark - 1, 0)); took = true; }
            else if (plain && key === 'Home') { setMark(0); took = true; }
            else if (plain && key === 'End') { setMark(ROWS.length - 1); took = true; }
            else if (plain && (key === 'Enter' || key === ' ')) { choose(mark); took = true; }
            else if (plain && key.length === 1) { var next = typeahead(key, clock()); if (next >= 0) setMark(next); took = true; }
            if (took) { taken[key] = true; e.preventDefault(); e.stopImmediatePropagation(); }
        }
        win.addEventListener('keydown', onKey, true);
        win.addEventListener('keyup', onKey, true);
        // the keys that are not taken do not reach the game either (its window system would scroll the map with the arrows)
        ['keydown', 'keypress', 'keyup'].forEach(function (name) { button.addEventListener(name, function (e) { e.stopPropagation(); }); });
        button.addEventListener('click', function () { if (shown) close(false); else { open(); button.focus(); } });       // (a press does not focus a button on every browser, and a synthetic click on none)
        button.addEventListener('blur', function (e) { if (shown && !(e.relatedTarget && inside(e.relatedTarget, list))) close(false); });
        // a press on a row must not take the focus from the button; the list's own touches and presses are not the bar's swipe or the game's
        list.addEventListener('mousedown', function (e) { e.preventDefault(); });
        ['pointerdown', 'pointerup'].forEach(function (name) { list.addEventListener(name, function (e) { e.stopPropagation(); }); });
        list.addEventListener('pointermove', function (e) { var i = options.indexOf(e.target); if (i >= 0 && i !== mark) setMark(i); });
        list.addEventListener('click', function (e) { var i = options.indexOf(e.target); if (i >= 0) choose(i); });

        function destroy() {
            close(false);
            win.removeEventListener('keydown', onKey, true);
            win.removeEventListener('keyup', onKey, true);
            if (button.parentNode) button.parentNode.removeChild(button);
            if (list.parentNode) list.parentNode.removeChild(list);
        }
        set(opts.value);
        return { button: button, list: list, set: set, shape: function () { return current < 0 ? null : ROWS[current].name; }, open: open, close: close, isOpen: function () { return shown; }, destroy: destroy };
    }

    return { create: create };
});
