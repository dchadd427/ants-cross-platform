// The touch buttons for the game's hotkeys (docs/TOUCH.md "The hotkey buttons", docs/CONTROLS.md "Touch Controls").
// A phone has no Ctrl+A. During a match that this player plays, on a device whose primary pointer is touch, the page shows a few real buttons that press the same keys a keyboard would:
// All ants (Ctrl+A), Stop (Ctrl+S), Home (Ctrl+H), Next (Ctrl+N), None (Esc), Shift (below) and More (Previous ant Ctrl+P, Health marks Ctrl+L, Quick help F1, Quick chat 1 - 4 F9 - F12).
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
//   * Shift (docs/TOUCH.md "The Shift button") is the one button that sends no key by itself: a tap lights it (latched: the next tap or drag on the picture is a Shift-click, then it lets go), a second tap
//     puts it out, a press of 450 ms locks it (every tap and drag is a Shift-click until it is tapped again). The game reads Shift from SDL's key state when the finger LEAVES the picture (the
//     left release), so the page sends a keyboard's Shift down just before the finger lands (a capture touchstart on the picture, before SDL's own) and up 120 ms after the last finger has left (SDL
//     takes the lift a frame later, and its key state changes when the key event arrives, so an up sent at the lift would be read as no Shift). One finger that taps or drags uses a latch up; a hold
//     (a right click), a lingering finger and two fingers (pan, zoom) do not. Shift never stays down: a blur, a hidden page, a touchcancel and the buttons hiding let it go at once.
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
    var SHIFT = { key: 'Shift', code: 'ShiftLeft', vk: 16, loc: 1 };
    var SETTLE_MS = 120;                 // Shift goes up this long after the last finger left the picture
    var LOCK_MS = 450;                   // a press on Shift this long locks it
    var TAP_MS = 400, HOLD_MS = 450, SLOP = 8;      // the game's own (include/ants_app/touch_control.hpp: kTapMs, kHoldMs, kSlopCssPx): which finger on the picture was a tap or a drag
    // id, the word on the button, its name for a screen reader, the keys (a Ctrl chord has ctrl: true)
    var MAIN = [
        { id: 'all', text: 'All ants', name: 'Select all ants', k: letter('a'), ctrl: true },
        { id: 'stop', text: 'Stop', name: 'Stop the selected ants', k: letter('s'), ctrl: true },
        { id: 'home', text: 'Home', name: 'Select the home hill', k: letter('h'), ctrl: true },
        { id: 'next', text: 'Next', name: 'Select the next ant', k: letter('n'), ctrl: true },
        { id: 'none', text: 'None', name: 'Select none', k: ESC, ctrl: false },
        { id: 'shift', text: 'Shift', name: 'Shift', k: SHIFT, ctrl: false }
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

    function makeEvent(win, type, k, ctrl, shift) {
        var e = new win.KeyboardEvent(type, { bubbles: true, cancelable: true, composed: true, view: win, key: k.key, code: k.code, location: k.loc, ctrlKey: ctrl, shiftKey: !!shift, repeat: false, keyCode: k.vk, which: k.vk, charCode: 0 });
        if (e.keyCode !== k.vk) { try { Object.defineProperty(e, 'keyCode', { get: function () { return k.vk; } }); } catch (err) { /* read-only: the init's value stays */ } }
        if (e.which !== k.vk) { try { Object.defineProperty(e, 'which', { get: function () { return k.vk; } }); } catch (err) { /* the same */ } }
        return e;
    }

    // page: { doc, win, stage, canvas, touch(), ready(), watching(), running(), ask(n) (ants_probe), menu(n) (ants_menu_get), isFullscreen(), relayout(), every(fn, ms), store() (a Storage or null),
    //         now() (ms), after(fn, ms) (a timer: its handle), cancel(handle): the last three are the clock and timers of the Shift button, and default to Date.now, setTimeout and clearTimeout }
    function forPage(page) {
        var doc = page.doc, win = page.win, stage = page.stage;
        var on = true, open = false, shown = false, roomOn = false, mode = '', rowTotal = 0, built = {}, down = {}, folded = 0;
        var shiftMode = 'off', shiftSent = false, settle = 0, lockTimer = 0, lockFired = false, pic = null;      // Shift: off, latched or locked; our Shift is down; its release timer; the press that locks; the finger on the picture
        var now = page.now || function () { return Date.now(); }, after = page.after || function (fn, ms) { return setTimeout(fn, ms); }, cancel = page.cancel || function (h) { clearTimeout(h); };
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
            if (def.id === 'shift') shiftButton(b); else b.addEventListener('click', function () { press(def, b); });
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
            if (!isDown && (e.key === 'Meta' || /^(Meta|OS)/.test(c))) {      // (an Apple keyboard sends no lift for a letter let go while Cmd is down: with Cmd up the letters are forgotten, as on a blur)
                for (var k in down) if (!/^(Control|Shift|Alt|Meta|OS)/.test(k)) delete down[k];
            }
        }
        win.addEventListener('keydown', function (e) { note(e, true); }, true);
        win.addEventListener('keyup', function (e) { note(e, false); }, true);
        win.addEventListener('blur', function () { down = {}; });
        function ctrlDown() { return !!(down.ControlLeft || down.ControlRight); }
        function shiftHeld() { return !!(down.ShiftLeft || down.ShiftRight); }

        function blur(b) { try { if (b && b.blur) b.blur(); } catch (err) { /* nothing to blur */ } }
        function ask(n) { try { return page.ask(n); } catch (err) { return -1; } }
        function menu(n) { try { return page.menu(n); } catch (err) { return -1; } }

        // ---- Shift (the header says how it works)
        function shiftLook() {
            var b = built.shift;
            b.setAttribute('aria-pressed', shiftMode === 'off' ? 'false' : 'true');
            b.setAttribute('data-state', shiftMode);
        }
        function setShift(m) { if (m !== shiftMode) { shiftMode = m; shiftLook(); } }
        function shiftDown() {
            if (shiftSent || shiftHeld()) return;                          // (down already, ours or the keyboard's: a keyboard cannot press a key that is down)
            shiftSent = true;
            (page.canvas || doc.body).dispatchEvent(makeEvent(win, 'keydown', SHIFT, ctrlDown(), true));
        }
        function shiftUp() {
            if (settle) { cancel(settle); settle = 0; }
            if (!shiftSent) return;
            shiftSent = false;
            (page.canvas || doc.body).dispatchEvent(makeEvent(win, 'keyup', SHIFT, ctrlDown(), false));
        }
        function shiftReset() {                                            // the key, the finger that was tracked, the press that locks and the latch: all let go
            pic = null;
            shiftUp();
            if (lockTimer) { cancel(lockTimer); lockTimer = 0; }
            setShift('off');
        }
        // A tap lights Shift (a second tap puts it out); a press of LOCK_MS locks it. Nothing is sent here: the keys go with the fingers on the picture.
        function shiftButton(b) {
            b.setAttribute('aria-pressed', 'false');
            b.setAttribute('data-state', 'off');
            b.addEventListener('pointerdown', function () {
                lockFired = false;
                if (lockTimer) cancel(lockTimer);
                lockTimer = after(function () { lockTimer = 0; if (why()) { refresh(); return; } lockFired = true; setShift('locked'); }, LOCK_MS);
            });
            ['pointerup', 'pointercancel', 'pointerleave'].forEach(function (name) { b.addEventListener(name, function () { if (lockTimer) { cancel(lockTimer); lockTimer = 0; } }); });
            b.addEventListener('click', function () {
                blur(b);
                if (lockFired) { lockFired = false; return; }              // (the press that locked it: its click is not a tap)
                if (why()) { refresh(); return; }
                setShift(shiftMode === 'off' ? 'latched' : 'off');
            });
        }
        // The fingers on the picture (the browser's own list says which are down: a lift that never came cannot keep a sequence alive)
        function onPicture(t) { var n = t && t.target, c = page.canvas; return !!n && !!c && (n === c || (!!c.contains && c.contains(n))); }
        function fingers(list) { var n = 0; if (list) for (var i = 0; i < list.length; i++) if (onPicture(list[i])) n++; return n; }
        win.addEventListener('touchstart', function (e) {
            var t = null, c = e.changedTouches || [], i;
            for (i = 0; i < c.length && !t; i++) if (onPicture(c[i])) t = c[i];
            if (!t) return;                                                // (a finger of the page: a button, the margin)
            if (fingers(e.touches) > 1) { if (pic) pic.multi = true; return; }      // a second finger: the map moves and zooms, and no latch is used up
            pic = null;
            if (shiftMode === 'off' || why()) { shiftUp(); return; }       // (a Shift that is waiting to go up goes now: this finger must not be a Shift-click)
            if (settle) { cancel(settle); settle = 0; }
            shiftDown();                                                   // (before SDL's own touchstart on the canvas: the key state is Shift's when the game sees the finger)
            pic = { t0: now(), x0: t.clientX, y0: t.clientY, id: t.identifier, multi: false, cls: '' };
        }, { capture: true, passive: true });
        win.addEventListener('touchmove', function (e) {
            if (!pic || pic.multi || pic.cls) return;
            var c = e.changedTouches || [], i, dx, dy;
            for (i = 0; i < c.length; i++) {
                if (c[i].identifier !== pic.id) continue;
                dx = c[i].clientX - pic.x0; dy = c[i].clientY - pic.y0;
                if (dx * dx + dy * dy > SLOP * SLOP) pic.cls = now() - pic.t0 < HOLD_MS ? 'drag' : 'hold';       // (as the game's model: out of the slop before the hold time is a drag)
            }
        }, { capture: true, passive: true });
        // The last finger left the picture: a tap or a drag of ONE finger uses a latch up; Shift goes up SETTLE_MS later (a cancel: at once, no click was made)
        function pictureEnd(e, cancelled) {
            if (!pic || fingers(e.touches) > 0) return;
            var done = pic, used = !cancelled && !done.multi && (done.cls === 'drag' || (!done.cls && now() - done.t0 < TAP_MS));
            pic = null;
            if (used && shiftMode === 'latched') setShift('off');
            if (cancelled) shiftUp(); else if (shiftSent) settle = after(function () { settle = 0; shiftUp(); }, SETTLE_MS);
        }
        win.addEventListener('touchend', function (e) { pictureEnd(e, false); }, { capture: true, passive: true });
        win.addEventListener('touchcancel', function (e) { pictureEnd(e, true); }, { capture: true, passive: true });
        win.addEventListener('blur', shiftReset);
        win.addEventListener('pagehide', shiftReset);
        doc.addEventListener('visibilitychange', function () { if (doc.hidden || doc.visibilityState === 'hidden') shiftReset(); });

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
            for (var s = 0; s < seq.length; s++) target.dispatchEvent(makeEvent(win, seq[s][0], seq[s][1], seq[s][2], shiftSent || shiftHeld()));
            refresh();
            return true;
        }

        function upright() { return win.innerHeight > win.innerWidth; }

        function setClass(name, want) { if (stage.classList) stage.classList.toggle(name, !!want); }
        function size(prop, px) { stage.style.setProperty(prop, px + 'px'); }
        function px(value) { var n = parseFloat(value); return isFinite(n) ? n : 0; }
        function rect() { try { return stage.getBoundingClientRect(); } catch (err) { return { left: 0, top: 0 }; } }
        function safeLeft() { try { return px(win.getComputedStyle(probe).paddingLeft); } catch (err) { return 0; } }
        function safeBottom() { try { return px(win.getComputedStyle(probe).paddingBottom); } catch (err) { return 0; } }

        // The row or the rail holds all seven; where a column cannot (a short window), Next goes first in the list under More (level 1), and Home after it, so that the list begins with Home and then Next (level 2)
        function fold(level) {
            if (level === folded) return;
            folded = level;
            bar.insertBefore(built.next, built.none); built.next.className = 'btn tk-btn tk-next';             // (both back in the bar first, Next before Home's place: Home goes before Next)
            bar.insertBefore(built.home, built.next); built.home.className = 'btn tk-btn tk-home';
            if (level >= 1) { more.insertBefore(built.next, more.children[0] || null); built.next.className = 'btn tk-btn tk-item tk-next'; }
            if (level >= 2) { more.insertBefore(built.home, more.children[0] || null); built.home.className = 'btn tk-btn tk-item tk-home'; }
        }

        // The room is the stage's classes: a match of this player's on a touch screen with the setting on, whatever hides the buttons for a while
        function layout(room) {
            var up = room && upright(), side = room && !up, full = room && page.isFullscreen();
            setClass('tk-on', room);
            setClass('tk-up', up);
            setClass('tk-side', side);
            setClass('tk-full', full);
            setClass('tk-page', room && !full);
            setClass('tk-wrap', up && stage.clientWidth < 364);        // (a narrow row: All ants has a row of its own; 364 = All ants at twice a target and six targets of 44 px, with six gaps)
            setClass('tk-narrow', up && stage.clientWidth < 294);      // (under 294, six targets and five gaps, the six after All ants stand three to a row)
            var rim = false, level = 0, two = false, rail = 72, railH = 0, btnH = 44;
            if (side) {
                if (full) {
                    setClass('tk-rim', false);
                    setClass('tk-two', false);
                    fold(0);
                    var height = bar.offsetHeight || stage.clientHeight;      // (the rail stands inside the safe areas: what it is high is what is left of the stage)
                    two = height > 0 && height < 356;          // (a short stage: two columns of 60 px, All ants across both; 356 = seven targets of 44 px, six gaps and the rail's padding)
                    rail = two ? 138 : 72;
                } else {
                    var r = rect(), margin = r.left - safeLeft();
                    // the room from the stage's top to the window's bottom (less the phone's bottom edge): the rail may be as high as that, so More never stands below the window
                    var below = win.innerHeight - (r.top + (win.pageYOffset || 0)) - safeBottom();
                    // 76 = the rail's 60 px, 6 px of air and the page's 10 px frame (drawn over the window's edge); 142 = two columns of 60 px, 6 px between, the same air and frame
                    rim = margin >= 76;
                    if (rim) {
                        railH = Math.max(stage.clientHeight, below);
                        if (railH >= 344) level = 0;                    // seven targets of 44 px and six gaps in one column
                        else if (railH >= 294) level = 1;              // six targets and five gaps: Next stands under More
                        else if (margin >= 142) two = true;            // too short for six: two columns in the margin (four rows of 44 px and three gaps)
                        else level = 2;                                // ...and no room for two columns: five in one column (Home and Next under More), the targets as high as the room allows
                    } else {
                        level = 1;                                     // (inside the stage, one column; never two columns)
                        railH = Math.max(stage.clientHeight, Math.min(below, 306));      // (306 = six targets, five gaps and the column's padding)
                        if (railH < 306) { level = 2; railH = Math.max(stage.clientHeight, Math.min(below, 256)); }      // (256 = five targets, four gaps and the padding; a short stage may not hold them: the column stands as low as the window)
                    }
                    if (level === 2) btnH = Math.max(40, Math.min(44, Math.floor((railH - (rim ? 0 : 12) - 24) / 5)));
                }
            }
            setClass('tk-rim', rim);
            setClass('tk-two', two);
            if (side && !full) fold(level); else if (!side) fold(0);
            if (room) size('--tk-rail', rail);
            if (side && !full) { size('--tk-rail-h', railH); size('--tk-btn-h', btnH); }
            var total = up ? (bar.offsetHeight || 48) + 8 : 0;                // (measured with the classes in place: the row may wrap on a narrow screen)
            if (room) size('--tk-row', total);
            var next = room ? (up ? 'up' : 'side') + (full ? ' full' : ' page') + (rim ? ' rim' : '') + (level ? ' fold' + level : '') + (two ? ' two' : '') : '';
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
                if (!visible) shiftReset();
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
            bar: bar, shield: shield, refresh: refresh, press: function (id) { var d = null, k; for (k = 0; k < MAIN.length; k++) if (MAIN[k].id === id) d = MAIN[k]; for (k = 0; k < MORE.length; k++) if (MORE[k].id === id) d = MORE[k]; return d && d.id !== 'shift' ? press(d, built[id]) : false; },
            shift: function () { return shiftMode; }, shiftDown: function () { return shiftSent; },
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
