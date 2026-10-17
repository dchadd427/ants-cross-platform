// The "Game type" control of the front page's lobby (web/lobby.html), made from the rows of front/game_modes.js: for the leader ONE field that opens a list (a drop-down under the field; a bottom sheet
// with big rows on a phone, by front/game_type.css), for a guest one read-only line. It is the ARIA select-only combobox: the field has focus all the time and names the option under the cursor with
// aria-activedescendant; the list is a listbox of options, the chosen one aria-selected. It knows nothing of the room: the page says what to show (paint) and is told what the leader chose (onChoose).
//   * Open: a click or a tap on the field, or Enter, Space, Down or Up on it. Choose: a click or a tap on a row, or Up, Down, Home, End and Enter or Space. Escape or a click outside closes it (Tab too).
//     Typing the first letters of a name jumps to that mode.
//   * ES5 and no module system: a page gets the global AntsGameType, node gets module.exports (the DOM comes in as `doc`).
//   * Nothing here puts a name into markup: every text is set with textContent.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsGameType = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    // o: { doc, host (the element that holds the control), modes (AntsGameModes), onChoose(id), setTimeout?, clearTimeout? }. Returns { paint(state), close(), isOpen(), field }.
    // state: { id (the room's mode number), guest, disabled, serverName? }
    function create(o) {
        var doc = o.doc, host = o.host, M = o.modes, rows = M.MODES;
        var open = false, active = -1, last = '', typed = '', typedTimer = null, state = { id: M.defaultMode().id, guest: false, disabled: false };
        var setT = o.setTimeout || function (f, ms) { return setTimeout(f, ms); }, clearT = o.clearTimeout || function (t) { clearTimeout(t); };

        function el(tag, cls, text) {
            var n = doc.createElement(tag);
            if (cls) n.className = cls;
            if (text !== undefined) n.textContent = text;
            return n;
        }
        host.className = (host.className ? host.className + ' ' : '') + 'gtype';
        var label = el('span', 'gt-label', 'Game type'); label.id = 'gt-label';
        var field = el('div', 'gt-field');
        var combo = el('div', 'gt-combo'); combo.id = 'gt-combo';
        combo.setAttribute('role', 'combobox'); combo.setAttribute('tabindex', '0');
        combo.setAttribute('aria-haspopup', 'listbox'); combo.setAttribute('aria-expanded', 'false'); combo.setAttribute('aria-controls', 'gt-list');
        combo.setAttribute('aria-labelledby', 'gt-label gt-name'); combo.setAttribute('aria-describedby', 'gt-line gt-note');
        var name = el('span', 'gt-name'); name.id = 'gt-name';
        var arrow = el('i', 'gt-arrow'); arrow.setAttribute('aria-hidden', 'true');
        combo.appendChild(name); combo.appendChild(arrow);
        var pop = el('div', 'gt-pop'); pop.hidden = true;
        var title = el('div', 'gt-title', 'Game type'); title.setAttribute('aria-hidden', 'true');
        var list = el('ul', 'gt-list'); list.id = 'gt-list'; list.setAttribute('role', 'listbox'); list.setAttribute('aria-labelledby', 'gt-label');
        var items = rows.map(function (m, i) {
            var li = el('li', 'gt-opt'); li.id = 'gt-opt-' + m.id; li.setAttribute('role', 'option'); li.setAttribute('aria-selected', 'false'); li.setAttribute('data-mode', String(m.id));
            var check = el('span', 'gt-check'); check.setAttribute('aria-hidden', 'true');
            var txt = el('span', 'gt-otxt'); txt.appendChild(el('span', 'gt-oname', m.name)); txt.appendChild(el('span', 'gt-oline', m.line));
            li.appendChild(check); li.appendChild(txt);
            li.addEventListener('mousedown', function (e) { e.preventDefault(); });         // (the field keeps the focus)
            li.addEventListener('click', function () { choose(i); });
            li.addEventListener('mousemove', function () { if (active !== i) setActive(i); });
            list.appendChild(li);
            return li;
        });
        pop.appendChild(title); pop.appendChild(list);
        field.appendChild(combo); field.appendChild(pop);
        var line = el('p', 'gt-line'); line.id = 'gt-line';
        var note = el('p', 'gt-note'); note.id = 'gt-note';
        var guest = el('p', 'gt-guest'); var glabel = el('span', 'gt-glabel', 'Game type:'); var gname = el('b', 'gt-gname');
        guest.appendChild(glabel); guest.appendChild(doc.createTextNode(' ')); guest.appendChild(gname);
        var gline = el('p', 'gt-gline');
        var scrim = el('div', 'gt-scrim');
        [label, field, line, note, guest, gline, scrim].forEach(function (n) { host.appendChild(n); });

        function selectedIndex() { for (var i = 0; i < rows.length; i++) if (rows[i].id === state.id) return i; return -1; }
        function setActive(i) {
            active = i;
            items.forEach(function (li, k) { li.classList.toggle('active', k === i); });
            if (i >= 0) {
                combo.setAttribute('aria-activedescendant', items[i].id);
                if (items[i].scrollIntoView) items[i].scrollIntoView({ block: 'nearest' });
            } else combo.removeAttribute('aria-activedescendant');
        }
        function show() {
            if (open || state.guest || state.disabled) return;
            open = true;
            pop.hidden = false; host.classList.add('open'); combo.setAttribute('aria-expanded', 'true');
            var at = selectedIndex();
            setActive(at < 0 ? 0 : at);
        }
        function close() {
            if (!open) return;
            open = false;
            pop.hidden = true; host.classList.remove('open'); combo.setAttribute('aria-expanded', 'false');
            setActive(-1);
        }
        function choose(i) {
            var m = rows[i];
            close();
            if (m && m.id !== state.id) o.onChoose(m.id);
        }
        function typeAhead(ch) {
            typed += ch.toLowerCase();
            if (typedTimer) clearT(typedTimer);
            typedTimer = setT(function () { typed = ''; typedTimer = null; }, 700);
            var from = active < 0 ? 0 : active;
            // a repeated letter steps through the names that start with it; a longer word looks from the one that is active
            var start = typed.length > 1 && typed.split('').every(function (c) { return c === typed.charAt(0); }) ? from + 1 : from;
            var word = typed.length > 1 && typed.split('').every(function (c) { return c === typed.charAt(0); }) ? typed.charAt(0) : typed;
            for (var k = 0; k < rows.length; k++) {
                var i = (start + k) % rows.length;
                if (rows[i].name.toLowerCase().indexOf(word) === 0) { if (!open) show(); setActive(i); return; }
            }
        }
        combo.addEventListener('click', function () { if (open) close(); else show(); });
        combo.addEventListener('keydown', function (e) {
            if (state.guest || state.disabled || e.altKey || e.ctrlKey || e.metaKey) return;
            var k = e.key;
            if (!open) {
                if (k === 'ArrowDown' || k === 'ArrowUp' || k === 'Enter' || k === ' ') { e.preventDefault(); show(); }
                else if (k.length === 1) { e.preventDefault(); typeAhead(k); }
                return;
            }
            if (k === 'ArrowDown') { e.preventDefault(); setActive(Math.min(rows.length - 1, active + 1)); }
            else if (k === 'ArrowUp') { e.preventDefault(); setActive(Math.max(0, active - 1)); }
            else if (k === 'Home') { e.preventDefault(); setActive(0); }
            else if (k === 'End') { e.preventDefault(); setActive(rows.length - 1); }
            else if (k === 'Enter' || k === ' ') { e.preventDefault(); choose(active); }
            else if (k === 'Escape') { e.preventDefault(); close(); }
            else if (k === 'Tab') close();
            else if (k.length === 1) { e.preventDefault(); typeAhead(k); }
        });
        doc.addEventListener('pointerdown', function (e) { if (open && !field.contains(e.target)) close(); }, true);
        scrim.addEventListener('click', close);

        function paint(s) {
            var stamp = JSON.stringify([s.id, !!s.guest, !!s.disabled, s.serverName || '']);
            if (stamp === last) return;
            last = stamp;
            state = { id: s.id, guest: !!s.guest, disabled: !!s.disabled };
            var v = M.viewOf(s.id, s.serverName);
            if (state.guest || state.disabled) close();
            host.classList.toggle('guest', state.guest);
            field.hidden = line.hidden = state.guest; label.hidden = state.guest;
            note.hidden = state.guest || !v.note; guest.hidden = !state.guest; gline.hidden = !state.guest || !v.line;
            name.textContent = v.name; line.textContent = v.line; line.hidden = line.hidden || !v.line; note.textContent = v.note;
            gname.textContent = v.name; gline.textContent = v.line;
            combo.setAttribute('aria-disabled', String(state.disabled)); combo.setAttribute('tabindex', state.disabled ? '-1' : '0');
            items.forEach(function (li, i) { li.setAttribute('aria-selected', String(rows[i].id === s.id)); li.classList.toggle('sel', rows[i].id === s.id); });
        }
        paint({ id: state.id, guest: false, disabled: true });
        return { paint: paint, close: close, isOpen: function () { return open; }, field: combo };
    }

    return { create: create };
});
