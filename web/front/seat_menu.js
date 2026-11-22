// The choice of a color on the front page's Room (web/lobby.html; docs/PLAY_IN_BROWSER.md "Your room"): the leader presses an open color's button and a short menu opens under it: Open, Easy bot, Medium bot, Hard bot, Closed.
// It is drawn by the page (not the browser's own menu), so that it looks like the rest of the room on a phone too. It knows nothing of the room: the page says what is chosen and what is off (open) and is told what was picked
// (onPick) or pressed while off (onBlocked: the three bots are off with Fog of War, and the page says why).
//   * A menu of items (role "menu", each item "menuitemradio"); the pressed button has aria-expanded. Open: a press on the button. Choose: a press on an item, or Up, Down, Home, End and Enter or Space. Escape closes and gives the focus
//     back to the button; Tab, a press elsewhere, a scroll that moves the button or a resize close it.
//   * ES5 and no module system: a page gets the global AntsSeatMenu, node gets module.exports (the DOM comes in as "doc"). Nothing here puts a text into markup: every text is set with textContent.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsSeatMenu = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    // word: what the plan says (front/lobby_rules.js KINDS); name: the item's words; line: a small line at its right ('' for none)
    var CHOICES = [
        { word: 'open', name: 'Open', line: '' },
        { word: 'easy', name: 'Easy bot', line: '' },
        { word: 'medium', name: 'Medium bot', line: '' },
        { word: 'hard', name: 'Hard bot', line: '' },
        { word: 'nobody', name: 'Closed', line: 'stays empty' }
    ];
    function nameOf(word) { for (var i = 0; i < CHOICES.length; i++) if (CHOICES[i].word === word) return CHOICES[i].name; return word; }

    // o: { doc, win }. Returns { open(spec), close(), isOpen(), box }. spec: { anchor (the button), value (the chosen word), off (the words that are off), label (the menu's name), onPick(word), onBlocked(word), onClose() }
    function create(o) {
        var doc = o.doc, win = o.win;
        var box = doc.createElement('ul'), items = [], spec = null, active = -1, at = null;       // (at: where the button was when the menu opened)
        box.className = 'sm-menu'; box.hidden = true; box.setAttribute('role', 'menu');
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        CHOICES.forEach(function (c, i) {
            var li = el('li', 'sm-item'), name = el('span', 'sm-name', c.name);
            li.setAttribute('role', 'menuitemradio'); li.setAttribute('aria-checked', 'false'); li.setAttribute('tabindex', '-1'); li.setAttribute('data-word', c.word);
            li.appendChild(name);
            if (c.line) li.appendChild(el('small', 'sm-line', c.line));
            li.addEventListener('click', function () { pick(i); });
            li.addEventListener('mousemove', function () { if (active !== i) setActive(i, false); });
            box.appendChild(li); items.push(li);
        });
        box.addEventListener('keydown', function (e) {
            var k = e.key;
            if (k === 'ArrowDown') { e.preventDefault(); setActive(Math.min(items.length - 1, active + 1), true); }
            else if (k === 'ArrowUp') { e.preventDefault(); setActive(Math.max(0, active - 1), true); }
            else if (k === 'Home') { e.preventDefault(); setActive(0, true); }
            else if (k === 'End') { e.preventDefault(); setActive(items.length - 1, true); }
            else if (k === 'Enter' || k === ' ') { e.preventDefault(); if (active >= 0) pick(active); }
            else if (k === 'Escape') { e.preventDefault(); close(true); }
            else if (k === 'Tab') close(false);
        });
        doc.body.appendChild(box);

        function setActive(i, focus) {
            active = i;
            items.forEach(function (li, k) { li.classList.toggle('active', k === i); });
            if (focus && i >= 0 && items[i].focus) items[i].focus({ preventScroll: true });
        }
        function pick(i) {
            var s = spec, c = CHOICES[i];
            if (!s) return;
            var off = !!(s.off && s.off.indexOf(c.word) !== -1);
            close(true);
            if (off) { if (s.onBlocked) s.onBlocked(c.word); }
            else if (c.word !== s.value && s.onPick) s.onPick(c.word);
        }
        function place() {
            var r = spec.anchor.getBoundingClientRect(), h = box.offsetHeight || 0, w = box.offsetWidth || 0, vw = win.innerWidth || 0, vh = win.innerHeight || 0;
            var left = Math.round(r.left), top = Math.round(r.bottom + 6);
            if (vw && w && left + w > vw - 8) left = Math.max(8, vw - 8 - w);
            if (vh && h && top + h > vh - 8) top = Math.max(8, Math.round(r.top - 6 - h));
            box.style.left = left + 'px'; box.style.top = top + 'px';
            at = { x: r.left, y: r.top };
        }
        function open(s) {
            if (spec) close(false);
            spec = s;
            items.forEach(function (li, i) {
                var c = CHOICES[i], off = !!(s.off && s.off.indexOf(c.word) !== -1), on = c.word === s.value;
                li.setAttribute('aria-checked', String(on));
                if (off) li.setAttribute('aria-disabled', 'true'); else li.removeAttribute('aria-disabled');
                li.classList.toggle('sel', on); li.classList.toggle('off', off);
            });
            box.setAttribute('aria-label', s.label || 'Choice of the color');
            box.hidden = false;
            place();
            s.anchor.setAttribute('aria-expanded', 'true');
            var chosen = 0;
            for (var i = 0; i < CHOICES.length; i++) if (CHOICES[i].word === s.value) chosen = i;
            setActive(chosen, true);
        }
        function close(back) {
            if (!spec) return;
            var s = spec;
            spec = null; active = -1;
            box.hidden = true;
            items.forEach(function (li) { li.classList.remove('active'); });
            s.anchor.setAttribute('aria-expanded', 'false');
            if (back && s.anchor.focus) s.anchor.focus();
            if (s.onClose) s.onClose();
        }
        doc.addEventListener('pointerdown', function (e) { if (spec && !box.contains(e.target) && !spec.anchor.contains(e.target)) close(false); }, true);
        function moved() { if (!spec || !at) return false; var r = spec.anchor.getBoundingClientRect(); return Math.abs(r.left - at.x) > 1 || Math.abs(r.top - at.y) > 1; }       // (a scroll that leaves the button where it was is no reason to put the menu away)
        if (win.addEventListener) { win.addEventListener('resize', function () { close(false); }); win.addEventListener('scroll', function () { if (moved()) close(false); }, true); }
        return { open: open, close: function () { close(false); }, isOpen: function () { return !!spec; }, box: box };
    }

    return { create: create, CHOICES: CHOICES, nameOf: nameOf };
});
