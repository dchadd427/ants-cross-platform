/* The game page's part of the bar at the top (the site's bar: web/site_nav.js builds the Menu, web/game_bar.css has the game page's own rules; docs/SITE_NAV.md "The game page"). Three jobs:
   1. The links. While a match runs on this page or a room is joined, no link of the bar or of its Menu may lose it: Home asks first (the page's question) and Yes leaves the seat; every other link opens in a new
      tab. Otherwise the links act as on any page.
   2. The keys. The game takes every key (Enter is START on its setup screen). While the Menu is open or the focus is in the bar, the keys are the bar's: they are stopped at the document before the game sees
      them, a key whose press the bar kept stays the bar's until its lift (a held key goes on repeating after the Menu has closed), Esc closes the Menu and does nothing in the game, Enter and Space press the
      focused control, and a bare Shift, Ctrl, Alt or Meta goes on to the game (its window system counts what is held). A press with the pointer gives the focus back to the game.
   3. The Menu's own lines, made each time it opens: a line that says what the links do while a match runs, "Copy link", "Download" and "Open another file" at the top for a recording on a narrow window (their
      buttons are in the bar from 701 px up), and "Reload page" last among the small links.
   The page calls AntsGameBar.start({ doc, win, busy, leave, ask, focus }) once: busy() says whether a match runs here or a room is joined, leave() lets go of the seat, ask() is the question (true: leave), focus()
   gives the focus to the game. */
(function (root) {
    'use strict';
    var MODIFIER = /^(Shift|Control|Alt|AltGraph|Meta|OS|CapsLock|NumLock|ScrollLock|Fn)$/;
    function start(page) {
        var d = page.doc, w = page.win, bar = d.querySelector('.sb'), btn = bar && bar.querySelector('.sbtn');
        if (!btn) return null;
        var kept = {};
        function box() { return d.getElementById('sbm'); }
        function menuOpen() { var b = box(); return !!b && !b.hidden; }
        function inBar(n) { var b = box(); return !!n && n !== d.body && (bar.contains(n) || (!!b && b.contains(n))); }
        function mine() { return menuOpen() || inBar(d.activeElement); }
        function own(e) { return !!e.target && e.target.id === 'settings-btn' && (e.key === 'Enter' || e.key === ' '); }      // (the gear presses itself: web/game_menu.js)
        function busy() { try { return !!page.busy(); } catch (err) { return false; } }
        function focusGame() { try { if (page.focus) page.focus(); } catch (err) { /* nothing to focus */ } }

        // ---- the keys ----
        function onKey(e) {
            var id = e.code || e.key;
            if (kept[id] && !e.repeat) delete kept[id];                      // (a fresh press: the lift of the one before was lost)
            else if (kept[id]) {                                              // (a repeat of a key that the bar kept: the game would take it for a new press, and a browser presses a focused button again)
                e.stopPropagation();
                if (e.key === 'Enter' || e.key === ' ') e.preventDefault();
                return;
            }
            if (!mine() || MODIFIER.test(e.key) || own(e)) return;
            if (!e.repeat) kept[id] = true;
            e.stopPropagation();
            if (e.key === 'Escape') { e.preventDefault(); if (!e.repeat && menuOpen()) btn.click(); }
        }
        function onPress(e) { if (kept[e.code || e.key]) e.stopPropagation(); }
        function onLift(e) {
            var id = e.code || e.key;
            if (!kept[id]) return;
            delete kept[id];
            e.stopPropagation();                                               // (the game would cancel the lift, and a browser presses a focused button at Space's lift)
        }
        d.addEventListener('keydown', onKey, true);
        d.addEventListener('keypress', onPress, true);
        d.addEventListener('keyup', onLift, true);

        // ---- the links ----
        d.addEventListener('click', function (e) {
            var a = e.target && e.target.closest ? e.target.closest('a') : null, b = box();
            if (!a || !(bar.contains(a) || (b && b.contains(a)))) return;
            var href = a.getAttribute('href');
            if (!href || href.charAt(0) === '#' || !busy()) return;
            if (href === '/') {                                              // Home (the mark of live matches goes to the front page too)
                if (!page.ask()) e.preventDefault();
                else if (!(e.ctrlKey || e.metaKey || e.shiftKey || e.altKey)) page.leave();      // (a click that opens another tab leaves this one as it is)
                return;
            }
            if (a.getAttribute('target') === '_blank' || e.ctrlKey || e.metaKey || e.shiftKey || e.altKey || e.button) return;
            e.preventDefault();
            try { w.open(href, '_blank', 'noopener,noreferrer'); } catch (err) { /* a blocked window: nothing opens, the match goes on */ }
        }, true);

        // A press with the pointer gives the focus back to the game (a key on a focused button would be the bar's). The Menu's button keeps it (the Menu is open and has it); the gear gives it back itself.
        d.addEventListener('click', function (e) {
            if (!(e.detail > 0) || !e.target || !e.target.closest) return;
            var c = e.target.closest('.sb button, .sb a');
            if (c && c !== btn && !(c.classList && c.classList.contains('gm-top'))) focusGame();
        });

        // ---- the Menu's own lines ----
        function make(tag, cls, text) { var n = d.createElement(tag); n.className = cls; n.textContent = text; return n; }
        function action(cls, text, run) {
            var a = make('a', 'gb-x ' + cls, text);
            a.setAttribute('href', '#');
            a.setAttribute('role', 'button');
            a.addEventListener('click', function (e) {
                e.preventDefault();
                if (menuOpen()) btn.click();
                run();
                if (e.detail > 0) focusGame();
            });
            return a;
        }
        function press(find) { return function () { var n = find(); if (n) n.click(); }; }
        function lines() {
            var b = box();
            if (!b) return;
            [].slice.call(b.querySelectorAll('.gb-x')).forEach(function (n) { n.parentNode.removeChild(n); });
            var top = [];
            if (busy()) top.push(make('p', 'gb-x gb-note', 'A match is running here. Home asks first; the other links open in a new tab.'));
            top.push(action('gb-own gb-copy', 'Copy link', press(function () { return d.getElementById('copy-btn'); })));
            top.push(action('gb-own gb-dl', 'Download', press(function () { return bar.querySelector('[data-dl]'); })));
            if (d.getElementById('own-more')) top.push(action('gb-own gb-file', 'Open another file', press(function () { return d.getElementById('own-more'); })));
            var ref = b.firstChild;
            top.forEach(function (n) { b.insertBefore(n, ref); });
            var small = b.querySelector('.sbsmall');
            if (small) small.appendChild(action('gb-reload', 'Reload page', function () { w.location.reload(); }));
            var all = b.querySelectorAll('a');
            for (var i = 0; i < all.length; i++) if (all[i].offsetParent !== null) { all[i].focus(); break; }      // (the first line that shows: it may be the note, which takes no focus, or a line of a recording's)
        }
        btn.addEventListener('click', function () { if (menuOpen()) lines(); });
        return { menuOpen: menuOpen };
    }
    root.AntsGameBar = { start: start };
})(window);
