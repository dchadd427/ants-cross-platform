/* The game page's part of the bar at the top (docs/SITE_NAV.md "The game page"; site_nav.js builds the Menu, game_bar.css has the rules). The page calls AntsGameBar.start({ doc, win, busy, running, leave, ask, focus }):
   busy() is true while a match runs here or a room is joined, running() while a match is played, leave() lets go of the seat, ask() is the question (true: leave), focus() gives the focus to the game.
   Links: while busy, Home asks first and every other link opens a new tab. Keys: while the Menu or the question is open or the focus is in the bar, no key reaches the game (a held key stays the bar's until its lift);
   a closed Menu or question gives the focus back to the game. Menu lines: the note, a recording's tools, "Reload page" (it asks first while a game is played). */
(function (root) {
    'use strict';
    var MODIFIER = /^(Shift|Control|Alt|AltGraph|Meta|OS|CapsLock|NumLock|ScrollLock|Fn)$/;
    function start(page) {
        var d = page.doc, w = page.win, bar = d.querySelector('.sb'), btn = bar && bar.querySelector('.sbtn'), kept = {}, dialog = null;
        if (!btn) return null;
        function box() { return d.getElementById('sbm'); }
        function menuOpen() { var b = box(); return !!b && !b.hidden; }
        function inBar(n) { var b = box(); return !!n && n !== d.body && (bar.contains(n) || (!!b && b.contains(n))); }
        function mine() { return !!dialog || menuOpen() || inBar(d.activeElement); }
        function busy() { try { return !!page.busy(); } catch (err) { return false; } }
        function playing() { try { return !d.body.classList.contains('replay') && !!page.running && !!page.running(); } catch (err) { return false; } }
        function focusGame() { try { if (page.focus) page.focus(); } catch (err) { /* nothing to focus */ } }
        function make(tag, cls, text) { var n = d.createElement(tag); n.className = cls; n.textContent = text; return n; }
        function onBackdrop(node, close) {         // a press that starts and ends on the backdrop itself closes the box as its Cancel does (a drag from inside the box, a text selection or a scroll does not)
            var down = false, hit = false;
            node.addEventListener('pointerdown', function (e) { down = e.target === node; hit = false; });
            node.addEventListener('pointerup', function (e) { hit = down && e.target === node; down = false; });
            node.addEventListener('pointercancel', function () { down = hit = false; });
            node.addEventListener('click', function (e) { var h = hit; hit = false; if (h && e.target === node) close(); });
        }
        function closeAsk() { if (!dialog) return; dialog.parentNode.removeChild(dialog); dialog = null; focusGame(); }

        function onKey(e) {
            var id = e.code || e.key, keys, at;
            if (kept[id] && !e.repeat) delete kept[id];                      // (a fresh press: the lift of the one before was lost)
            else if (kept[id]) { e.stopPropagation(); if (e.key === 'Enter' || e.key === ' ') e.preventDefault(); return; }      // (a repeat: the game would take it for a press, a browser presses a focused button again)
            if (!mine() || MODIFIER.test(e.key) || (e.target && e.target.id === 'settings-btn' && (e.key === 'Enter' || e.key === ' '))) return;      // (the gear presses itself: web/game_menu.js)
            if (!e.repeat) kept[id] = true;
            e.stopPropagation();
            if (e.key === 'Escape') { e.preventDefault(); if (!e.repeat && dialog) closeAsk(); else if (!e.repeat && menuOpen()) btn.click(); }
            else if (e.key === 'Tab' && dialog) {                              // (the question keeps the focus between its two buttons)
                keys = dialog.querySelectorAll('button'); at = d.activeElement;
                if (e.shiftKey && at === keys[0]) { e.preventDefault(); keys[1].focus(); } else if (!e.shiftKey && at === keys[1]) { e.preventDefault(); keys[0].focus(); }
            }
        }
        d.addEventListener('keydown', onKey, true);
        d.addEventListener('keypress', function (e) { if (kept[e.code || e.key]) e.stopPropagation(); }, true);
        d.addEventListener('keyup', function (e) {
            var id = e.code || e.key;
            if (!kept[id]) return;
            delete kept[id];
            e.stopPropagation();                                               // (the game would cancel the lift, and a browser presses a focused button at Space's lift)
        }, true);

        d.addEventListener('click', function (e) {                             // the links
            var a = e.target && e.target.closest ? e.target.closest('a') : null, b = box(), href = a && a.getAttribute('href'), modified = e.ctrlKey || e.metaKey || e.shiftKey || e.altKey;
            if (!a || !(bar.contains(a) || (b && b.contains(a))) || !href || href.charAt(0) === '#' || !busy()) return;
            if (href === '/') { if (!page.ask()) e.preventDefault(); else if (!modified) page.leave(); return; }      // (Home, and the mark of live matches: a click for another tab keeps the seat)
            if (a.getAttribute('target') === '_blank' || modified || e.button) return;
            e.preventDefault();
            try { w.open(href, '_blank', 'noopener,noreferrer'); } catch (err) { /* a blocked window: nothing opens, the match goes on */ }
        }, true);
        d.addEventListener('click', function (e) {                             // a press with the pointer gives the focus back to the game (the Menu's button and the gear see to themselves)
            var c = e.detail > 0 && e.target && e.target.closest ? e.target.closest('.sb button, .sb a') : null;
            if (c && c !== btn && !c.classList.contains('gm-top')) focusGame();
        });

        function action(cls, text, run) {
            var a = make('a', 'gb-x ' + cls, text);
            a.setAttribute('href', '#');
            a.setAttribute('role', 'button');
            a.addEventListener('click', function (e) { e.preventDefault(); if (menuOpen()) btn.click(); run(); });
            return a;
        }
        function press(id) { return function () { var n = id === 'dl' ? bar.querySelector('[data-dl]') : d.getElementById(id); if (n) n.click(); }; }
        function askReload() {
            var card = make('div', 'gb-card', ''), row = make('div', 'gb-btns', ''), no = make('button', 'btn', 'Cancel'), yes = make('button', 'btn', 'Reload');
            dialog = make('div', 'gb-ask', '');
            dialog.setAttribute('role', 'dialog');
            dialog.setAttribute('aria-modal', 'true');
            dialog.setAttribute('aria-label', 'Reload the page?');
            card.appendChild(make('p', '', 'Reload the page? A game against the computer on this device will end; an online match comes back by itself.'));
            no.type = yes.type = 'button';
            no.addEventListener('click', closeAsk);
            yes.addEventListener('click', function () { closeAsk(); w.location.reload(); });
            onBackdrop(dialog, closeAsk);
            row.appendChild(no);
            row.appendChild(yes);
            card.appendChild(row);
            dialog.appendChild(card);
            d.body.appendChild(dialog);
            no.focus();
        }
        btn.addEventListener('click', function () {                            // the Menu's own lines, made at each opening
            var b = box(), top = [], small, all, ref, i;
            if (!menuOpen()) { focusGame(); return; }
            [].slice.call(b.querySelectorAll('.gb-x')).forEach(function (n) { n.parentNode.removeChild(n); });
            if (busy()) top.push(make('p', 'gb-x gb-note', 'A match is running here. Home asks first; the other links open in a new tab.'));
            top.push(action('gb-own gb-copy', 'Copy link', press('copy-btn')));
            top.push(action('gb-own gb-dl', 'Download', press('dl')));
            if (d.getElementById('own-more')) top.push(action('gb-own gb-file', 'Open another file', press('own-more')));
            ref = b.firstChild;
            top.forEach(function (n) { b.insertBefore(n, ref); });
            small = b.querySelector('.sbsmall');
            if (small) small.appendChild(action('gb-reload', 'Reload page', function () { if (playing()) askReload(); else w.location.reload(); }));
            all = b.querySelectorAll('a');
            for (i = 0; i < all.length; i++) if (all[i].offsetParent !== null) { all[i].focus(); break; }      // (the first line that shows: the note takes no focus)
        });
    }
    root.AntsGameBar = { start: start };
})(window);
