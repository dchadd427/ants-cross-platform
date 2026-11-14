// The touch buttons for the game's hotkeys (docs/TOUCH.md "The hotkey buttons", docs/CONTROLS.md "Touch Controls").
// A phone has no Ctrl+A. During a match that this player plays, on a device whose primary pointer is touch, the page shows a few real buttons that press the same keys a keyboard would:
// All ants (Ctrl+A), Stop (Ctrl+S), Home (Ctrl+H), Next (Ctrl+N), None (Esc) and More (Previous ant Ctrl+P, Health marks Ctrl+L, Quick help F1, Quick chat 1 - 4 F9 - F12).
//   * A press is the key events of a keyboard on the game's canvas, in a keyboard's order and nothing more: for a Ctrl chord keydown Control, keydown letter, keyup letter, keyup Control (key, code,
//     keyCode and which, ctrlKey on the letter and on the first lift); for a single key a keydown and a keyup. They bubble through the document (the settings panel's key guard sees them) to the
//     window, where the game's window system listens. One press is one key: the buttons act on `click` (never on a finger that rests), and the events are never repeats.
//   * The buttons are hidden, and a press sends nothing, while: the setting is off (the key ants.touchkeys), the primary pointer is not touch, the game is not ready, a recording or a live match is
//     watched, no match of this player is running, the match screen is not up (the room, the results), a dialog or a vote or a lost link waits for its answer, the countdown card is up, the settings
//     panel is open or the chat's phone keyboard has the focus. Every press asks again first.
//   * None is dimmed (aria-disabled, no key) while the game says that nothing is picked (ants_probe 50); a game that does not answer leaves it live. Esc with nothing picked opens the settings menu,
//     as on a keyboard.
//   * Where: one class on the page's stage says it. Upright (the window taller than wide): a row under the picture in the page, at the bottom of the stage in fullscreen. Sideways: a rail along the
//     left edge of the stage, outside the picture (the page's own fit code works in what the stage's padding leaves). A finger on a button reaches the game as nothing: the buttons are outside the
//     picture's box, and the mouse events that follow a touch stop here (the touch events themselves are the page's and are left alone).
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsTouchKeys, node gets module.exports (tests/scripts/web_touch_keys_check.js runs it with fakes).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsTouchKeys = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var KEY = 'ants.touchkeys';
    var POLL_MS = 250;
    var CTRL = { key: 'Control', code: 'ControlLeft', vk: 17, loc: 1 };
    function letter(c) { return { key: c, code: 'Key' + c.toUpperCase(), vk: c.toUpperCase().charCodeAt(0), loc: 0 }; }
    function fn(n) { return { key: 'F' + n, code: 'F' + n, vk: 111 + n, loc: 0 }; }
    var ESC = { key: 'Escape', code: 'Escape', vk: 27, loc: 0 };
    // id, the word on the button, its name for a screen reader, the keys (a Ctrl chord has ctrl: true)
    var MAIN = [
        { id: 'all', text: 'All ants', name: 'Select all ants', k: letter('a'), ctrl: true },
        { id: 'stop', text: 'Stop', name: 'Stop the selected ants', k: letter('s'), ctrl: true },
        { id: 'home', text: 'Home', name: 'Select the home hill', k: letter('h'), ctrl: true },
        { id: 'next', text: 'Next', name: 'Select the next ant', k: letter('n'), ctrl: true },
        { id: 'none', text: 'None', name: 'Select none', k: ESC, ctrl: false }
    ];
    var MORE = [
        { id: 'prev', text: 'Previous ant', name: 'Select the previous ant', k: letter('p'), ctrl: true },
        { id: 'health', text: 'Health marks', name: 'Health marks on or off', k: letter('l'), ctrl: true },
        { id: 'help', text: 'Quick help', name: 'Quick help', k: fn(1), ctrl: false },
        { id: 'chat1', text: 'Quick chat 1', name: 'Quick chat 1', k: fn(9), ctrl: false },
        { id: 'chat2', text: 'Quick chat 2', name: 'Quick chat 2', k: fn(10), ctrl: false },
        { id: 'chat3', text: 'Quick chat 3', name: 'Quick chat 3', k: fn(11), ctrl: false },
        { id: 'chat4', text: 'Quick chat 4', name: 'Quick chat 4', k: fn(12), ctrl: false }
    ];

    // The remembered choice: only "off" turns the buttons off (a missing or other value is on)
    function setting(stored) { return stored !== 'off'; }

    // The key events of one press, in order: [type, key, ctrlKey]
    function sequence(def) {
        if (!def.ctrl) return [['keydown', def.k, false], ['keyup', def.k, false]];
        return [['keydown', CTRL, true], ['keydown', def.k, true], ['keyup', def.k, true], ['keyup', CTRL, false]];
    }

    function makeEvent(win, type, k, ctrl) {
        var e = new win.KeyboardEvent(type, { bubbles: true, cancelable: true, composed: true, view: win, key: k.key, code: k.code, location: k.loc, ctrlKey: ctrl, repeat: false, keyCode: k.vk, which: k.vk, charCode: 0 });
        if (e.keyCode !== k.vk) { try { Object.defineProperty(e, 'keyCode', { get: function () { return k.vk; } }); } catch (err) { /* read-only: the init's value stays */ } }
        if (e.which !== k.vk) { try { Object.defineProperty(e, 'which', { get: function () { return k.vk; } }); } catch (err) { /* the same */ } }
        return e;
    }

    // page: { doc, win, stage, canvas, touch(), ready(), watching(), running(), ask(n) (ants_probe), menu(n) (ants_menu_get), isFullscreen(), relayout(), every(fn, ms), store() (a Storage or null) }
    function forPage(page) {
        var doc = page.doc, win = page.win, stage = page.stage;
        var on = true, open = false, shown = false, mode = '', cols = 1, rowTotal = 0, built = {};
        try { var st = page.store && page.store(); on = setting(st ? st.getItem(KEY) : null); } catch (err) { on = true; }

        var bar = doc.createElement('div');
        bar.id = 'touch-keys';
        bar.className = 'tk';
        bar.setAttribute('role', 'group');
        bar.setAttribute('aria-label', 'Game keys');
        bar.hidden = true;
        var more = doc.createElement('div');
        more.className = 'tk-more';
        more.hidden = true;
        var moreBtn = null;

        function button(def, cls) {
            var b = doc.createElement('button');
            b.type = 'button';
            b.className = 'btn tk-btn ' + cls;
            b.textContent = def.text;
            b.setAttribute('aria-label', def.name);
            b.setAttribute('data-key', def.id);
            b.addEventListener('click', function () { press(def, b); });
            built[def.id] = b;
            return b;
        }
        var i;
        for (i = 0; i < MAIN.length; i++) bar.appendChild(button(MAIN[i], 'tk-' + MAIN[i].id));
        moreBtn = doc.createElement('button');
        moreBtn.type = 'button';
        moreBtn.className = 'btn tk-btn tk-morebtn';
        moreBtn.textContent = 'More';
        moreBtn.setAttribute('aria-label', 'More keys');
        moreBtn.setAttribute('aria-haspopup', 'true');
        moreBtn.setAttribute('aria-expanded', 'false');
        moreBtn.addEventListener('click', function () { if (!why()) toggle(!open); else refresh(); blur(moreBtn); });
        bar.appendChild(moreBtn);
        for (i = 0; i < MORE.length; i++) more.appendChild(button(MORE[i], 'tk-item'));
        bar.appendChild(more);
        // A finger that lands on a button is no mouse for the game: the compatibility mouse events that follow a touch (and a mouse's own) stop here, the touch events are left alone (the sound's unlock needs them)
        ['mousedown', 'mouseup', 'mousemove', 'mouseover', 'mouseout', 'dblclick'].forEach(function (name) { bar.addEventListener(name, function (e) { e.stopPropagation(); }); });
        bar.addEventListener('contextmenu', function (e) { e.preventDefault(); });
        bar.addEventListener('keydown', function (e) { if (e.key === 'Escape' && open) { e.stopPropagation(); e.preventDefault(); toggle(false); blur(moreBtn); } });
        stage.appendChild(bar);

        function blur(b) { try { if (b && b.blur) b.blur(); } catch (err) { /* nothing to blur */ } }
        function ask(n) { try { return page.ask(n); } catch (err) { return -1; } }
        function menu(n) { try { return page.menu(n); } catch (err) { return -1; } }

        // Why the buttons are not usable now ('' when they are)
        function why() {
            if (!on) return 'setting';
            if (!page.touch()) return 'pointer';
            if (!page.ready()) return 'loading';
            if (page.watching()) return 'watching';
            if (!page.running()) return 'no match';
            if (menu(100) !== 0) return 'screen';
            if (menu(101) !== 0) return 'dialog';
            if (ask(60) >= 0 || ask(5) !== 0) return 'card';
            if (doc.getElementById('game-menu')) return 'settings';
            var a = doc.activeElement;
            if (a && a.id === 'chat-keys') return 'chat';
            return '';
        }

        function toggle(want) {
            open = !!want;
            more.hidden = !open;
            moreBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
            bar.classList.toggle('tk-open', open);
        }

        function press(def, b) {
            blur(b);
            if (why()) { refresh(); return false; }
            if (def.id === 'none' && ask(50) === 0) return false;      // (dimmed: nothing is picked; nothing is sent)
            var seq = sequence(def), target = page.canvas || doc.body;
            if (open && b.className.indexOf('tk-item') !== -1) toggle(false);
            for (var s = 0; s < seq.length; s++) target.dispatchEvent(makeEvent(win, seq[s][0], seq[s][1], seq[s][2]));
            refresh();
            return true;
        }

        function upright() { return win.innerHeight > win.innerWidth; }

        function setClass(name, want) { if (stage.classList) stage.classList.toggle(name, !!want); }
        function size(prop, px) { stage.style.setProperty(prop, px + 'px'); }

        function layout(visible) {
            var up = visible && upright(), full = visible && page.isFullscreen();
            var cols2 = visible && !up && stage.clientHeight > 0 && stage.clientHeight < 306 ? 2 : 1;        // (a short stage: two columns of 60 px, All ants across both; 306 = six targets of 44 px, five gaps and the rail's padding)
            setClass('tk-on', visible);
            setClass('tk-up', up);
            setClass('tk-side', visible && !up);
            setClass('tk-full', full);
            setClass('tk-page', visible && !full);
            setClass('tk-two', cols2 === 2);
            setClass('tk-wrap', up && stage.clientWidth < 316);        // (a narrow row: All ants has a row of its own)
            var rail = cols2 * 60 + (cols2 - 1) * 6 + 12;
            if (visible) size('--tk-rail', rail);
            var total = up ? (bar.offsetHeight || 48) + 8 : 0;                // (measured with the classes in place: the row may wrap on a narrow screen)
            if (visible) size('--tk-row', total);
            var next = visible ? (up ? 'up' : 'side') + (full ? ' full' : ' page') : '';
            var changed = next !== mode || cols2 !== cols || total !== rowTotal;
            mode = next; cols = cols2; rowTotal = total;
            return changed;
        }

        // Show or hide, lay out; `quiet`: the page is laying out itself (do not ask it to)
        function refresh(quiet) {
            var visible = !why();
            if (visible !== shown) {
                shown = visible;
                bar.hidden = !visible;
                bar.style.display = visible ? '' : 'none';
                if (!visible) toggle(false);
            }
            var changed = layout(visible);
            if (visible) {
                var none = built.none, dim = ask(50) === 0;
                none.setAttribute('aria-disabled', dim ? 'true' : 'false');
                none.classList.toggle('tk-dim', dim);
            }
            if (changed && !quiet && page.relayout) page.relayout();
            return visible;
        }

        doc.addEventListener('pointerdown', function (e) { if (open && !bar.contains(e.target)) toggle(false); }, true);
        win.addEventListener('resize', function () { refresh(); });
        (page.every || function (f, ms) { return setInterval(f, ms); })(function () { refresh(); }, POLL_MS);
        refresh(true);

        return {
            bar: bar, refresh: refresh, press: function (id) { var d = null, k; for (k = 0; k < MAIN.length; k++) if (MAIN[k].id === id) d = MAIN[k]; for (k = 0; k < MORE.length; k++) if (MORE[k].id === id) d = MORE[k]; return d ? press(d, built[id]) : false; },
            why: why, visible: function () { return shown; }, toggle: toggle, isOpen: function () { return open; },
            // the settings menu's row (web/game_menu.js): there on a touch screen only
            available: function () { return !!page.touch(); },
            get: function () { return on; },
            set: function (value) {
                on = !!value;
                try { var s = page.store && page.store(); if (s) s.setItem(KEY, on ? 'on' : 'off'); } catch (err) { /* private window: not remembered */ }
                refresh();
            }
        };
    }

    return { forPage: forPage, setting: setting, sequence: sequence, MAIN: MAIN, MORE: MORE, KEY: KEY, POLL_MS: POLL_MS };
});
