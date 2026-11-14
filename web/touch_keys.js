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
//   * Where: classes on the page's stage say it. Upright (the window taller than wide): a row under the picture in the page, at the bottom of the stage in fullscreen. Sideways in the page: one
//     column in the page's free margin left of the stage, so the picture keeps its size (where the margin is too narrow: one column of five inside the stage, Next under More); sideways in
//     fullscreen: a rail along the left edge inside the stage (the page's own fit code works in what the stage's padding leaves). The room is kept for the whole match: when the buttons hide (a
//     dialog, the settings panel, the chat field) only they go, the picture never moves or changes size. A finger on a button reaches the game as nothing: the buttons are outside the picture's
//     box, and the mouse events that follow a touch stop here (the touch events themselves are the page's and are left alone). The tap that closes the More list is the list's alone.
//   * A button never sends a key that is physically down (a keyboard cannot press a key that is down): with Control held a chord is the letter alone, with the key itself held nothing is sent.
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

    // The key events of one press, in order: [type, key, ctrlKey]. `ctrlDown`: a Control key is physically down, so the chord is the letter alone (with ctrlKey true) and no Control of ours goes down or up
    function sequence(def, ctrlDown) {
        if (!def.ctrl) return [['keydown', def.k, false], ['keyup', def.k, false]];
        if (ctrlDown) return [['keydown', def.k, true], ['keyup', def.k, true]];
        return [['keydown', CTRL, true], ['keydown', def.k, true], ['keyup', def.k, true], ['keyup', CTRL, false]];
    }

    // The reasons that only hide the buttons for a while (the match goes on and the room stays); any other reason gives the room back
    var PASSING = { dialog: 1, card: 1, settings: 1, chat: 1 };

    function makeEvent(win, type, k, ctrl) {
        var e = new win.KeyboardEvent(type, { bubbles: true, cancelable: true, composed: true, view: win, key: k.key, code: k.code, location: k.loc, ctrlKey: ctrl, repeat: false, keyCode: k.vk, which: k.vk, charCode: 0 });
        if (e.keyCode !== k.vk) { try { Object.defineProperty(e, 'keyCode', { get: function () { return k.vk; } }); } catch (err) { /* read-only: the init's value stays */ } }
        if (e.which !== k.vk) { try { Object.defineProperty(e, 'which', { get: function () { return k.vk; } }); } catch (err) { /* the same */ } }
        return e;
    }

    // page: { doc, win, stage, canvas, touch(), ready(), watching(), running(), ask(n) (ants_probe), menu(n) (ants_menu_get), isFullscreen(), relayout(), every(fn, ms), store() (a Storage or null) }
    function forPage(page) {
        var doc = page.doc, win = page.win, stage = page.stage;
        var on = true, open = false, shown = false, roomOn = false, mode = '', rowTotal = 0, built = {}, down = {}, folded = false;
        try { var st = page.store && page.store(); on = setting(st ? st.getItem(KEY) : null); } catch (err) { on = true; }

        var bar = doc.createElement('div');
        bar.id = 'touch-keys';
        bar.className = 'tk';
        bar.setAttribute('role', 'group');
        bar.setAttribute('aria-label', 'Game keys');
        bar.hidden = true;
        bar.classList.toggle('tk-gone', true);
        var more = doc.createElement('div');
        more.className = 'tk-more';
        more.hidden = true;
        var moreBtn = null;
        // A see-through layer under the list while it is open: the tap that closes the list is the list's and reaches nothing else (the game included)
        var shield = doc.createElement('div');
        shield.className = 'tk-shield';
        shield.hidden = true;
        var probe = doc.createElement('div');                               // (its padding is the phone's left safe area: see layout)
        probe.className = 'tk-probe';

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
        var MOUSE = ['mousedown', 'mouseup', 'mousemove', 'mouseover', 'mouseout', 'dblclick'];
        MOUSE.forEach(function (name) { bar.addEventListener(name, function (e) { e.stopPropagation(); }); shield.addEventListener(name, function (e) { e.stopPropagation(); }); });
        bar.addEventListener('contextmenu', function (e) { e.preventDefault(); });
        shield.addEventListener('contextmenu', function (e) { e.preventDefault(); });
        shield.addEventListener('click', function (e) { e.stopPropagation(); toggle(false); });          // (a click comes after the mouse events of a touch: until then the layer takes them all)
        bar.addEventListener('keydown', function (e) { if (e.key === 'Escape' && open) { e.stopPropagation(); e.preventDefault(); toggle(false); blur(moreBtn); } });
        stage.appendChild(shield);
        stage.appendChild(probe);
        stage.appendChild(bar);

        // The keys that the keyboard holds down now (only a real key counts: ours are not trusted): a button never sends a key that is down
        function note(e, isDown) {
            if (e.isTrusted !== true) return;
            var c = e.code || e.key;
            if (!c) return;
            if (isDown) down[c] = 1; else delete down[c];
        }
        win.addEventListener('keydown', function (e) { note(e, true); }, true);
        win.addEventListener('keyup', function (e) { note(e, false); }, true);
        win.addEventListener('blur', function () { down = {}; });
        function ctrlDown() { return !!(down.ControlLeft || down.ControlRight); }

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
            shield.hidden = !open;
            moreBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
            bar.classList.toggle('tk-open', open);
        }

        function press(def, b) {
            blur(b);
            if (why()) { refresh(); return false; }
            if (def.id === 'none' && ask(50) === 0) return false;      // (dimmed: nothing is picked; nothing is sent)
            if (open && b.className.indexOf('tk-item') !== -1) toggle(false);
            if (down[def.k.code]) return false;                           // (the key is down on the keyboard: a keyboard cannot press it again)
            var seq = sequence(def, ctrlDown()), target = page.canvas || doc.body;
            for (var s = 0; s < seq.length; s++) target.dispatchEvent(makeEvent(win, seq[s][0], seq[s][1], seq[s][2]));
            refresh();
            return true;
        }

        function upright() { return win.innerHeight > win.innerWidth; }

        function setClass(name, want) { if (stage.classList) stage.classList.toggle(name, !!want); }
        function size(prop, px) { stage.style.setProperty(prop, px + 'px'); }
        function px(value) { var n = parseFloat(value); return isFinite(n) ? n : 0; }
        function rect() { try { return stage.getBoundingClientRect(); } catch (err) { return { left: 0, top: 0 }; } }
        function safeLeft() { try { return px(win.getComputedStyle(probe).paddingLeft); } catch (err) { return 0; } }

        // Next stands in the row or the rail; in a column of five it is the first of the list under More
        function fold(want) {
            if (want === folded) return;
            folded = want;
            var next = built.next;
            if (want) { more.insertBefore(next, more.children[0] || null); next.className = 'btn tk-btn tk-item tk-next'; }
            else { bar.insertBefore(next, built.none); next.className = 'btn tk-btn tk-next'; }
        }

        // The room is the stage's classes: a match of this player's on a touch screen with the setting on, whatever hides the buttons for a while
        function layout(room) {
            var up = room && upright(), side = room && !up, full = room && page.isFullscreen();
            setClass('tk-on', room);
            setClass('tk-up', up);
            setClass('tk-side', side);
            setClass('tk-full', full);
            setClass('tk-page', room && !full);
            setClass('tk-wrap', up && stage.clientWidth < 316);        // (a narrow row: All ants has a row of its own)
            var rim = false, five = false, two = false, rail = 72, railH = 0;
            if (side) {
                if (full) {
                    setClass('tk-rim', false);
                    setClass('tk-two', false);
                    fold(false);
                    var height = bar.offsetHeight || stage.clientHeight;      // (the rail stands inside the safe areas: what it is high is what is left of the stage)
                    two = height > 0 && height < 306;          // (a short stage: two columns of 60 px, All ants across both; 306 = six targets of 44 px, five gaps and the rail's padding)
                    rail = two ? 138 : 72;
                } else {
                    var r = rect();
                    rim = r.left - safeLeft() >= 66;           // (a rail of 60 px and 6 px of air fit in the page's free margin left of the stage: the picture keeps its size)
                    if (rim) {
                        railH = Math.max(stage.clientHeight, win.innerHeight - (r.top + (win.pageYOffset || 0)));
                        five = railH < 306;
                    } else five = true;                         // (inside the stage, one column of five: Next stands under More; never two columns)
                }
            }
            setClass('tk-rim', rim);
            setClass('tk-two', two);
            if (side && !full) fold(five); else if (!side) fold(false);
            if (room) size('--tk-rail', rail);
            if (rim) size('--tk-rail-h', railH);
            var total = up ? (bar.offsetHeight || 48) + 8 : 0;                // (measured with the classes in place: the row may wrap on a narrow screen)
            if (room) size('--tk-row', total);
            var next = room ? (up ? 'up' : 'side') + (full ? ' full' : ' page') + (rim ? ' rim' : '') + (five ? ' five' : '') + (two ? ' two' : '') : '';
            var changed = next !== mode || total !== rowTotal;
            mode = next; rowTotal = total;
            return changed;
        }

        // Show or hide, lay out; `quiet`: the page is laying out itself (do not ask it to)
        function refresh(quiet) {
            var reason = why(), visible = !reason, room = visible || PASSING[reason] === 1;
            if (room !== roomOn) {
                roomOn = room;
                bar.hidden = !room;
                bar.style.display = room ? '' : 'none';
            }
            if (visible !== shown) {
                shown = visible;
                bar.classList.toggle('tk-gone', !visible);
                if (!visible) toggle(false);
            }
            var changed = layout(room);
            if (visible) {
                var none = built.none, dim = ask(50) === 0;
                none.setAttribute('aria-disabled', dim ? 'true' : 'false');
                none.classList.toggle('tk-dim', dim);
            }
            if (changed && !quiet && page.relayout) page.relayout();
            return visible;
        }

        win.addEventListener('resize', function () { refresh(); });
        (page.every || function (f, ms) { return setInterval(f, ms); })(function () { refresh(); }, POLL_MS);
        refresh(true);

        return {
            bar: bar, shield: shield, refresh: refresh, press: function (id) { var d = null, k; for (k = 0; k < MAIN.length; k++) if (MAIN[k].id === id) d = MAIN[k]; for (k = 0; k < MORE.length; k++) if (MORE[k].id === id) d = MORE[k]; return d ? press(d, built[id]) : false; },
            why: why, visible: function () { return shown; }, room: function () { return roomOn; }, toggle: toggle, isOpen: function () { return open; },
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
