// The choice of a color on the front page's Room (web/lobby.html; docs/PLAY_IN_BROWSER.md "Your room", docs/HOME_SCREEN.md "The Room"): the leader presses an open color's button and a short menu opens under it: Open, Easy bot, Medium bot, Hard bot, Closed.
// It is drawn by the page (not the browser's own menu), so that it looks like the rest of the room on a phone too. It knows nothing of the room: the page says what is chosen and what is off (open) and is told what was picked
// (onPick) or pressed while off (onBlocked: the three bots are off with Fog of War, and the page says why).
//   * A menu of items (role "menu", each item "menuitemradio"); the pressed button has aria-expanded. Open: a press on the button. Choose: a press on an item, or Up, Down, Home, End and Enter or Space. Escape closes and gives the focus
//     back to the button; Tab, a press elsewhere, a scroll that moves the button or a resize close it.
//   * A phone held upright draws the four colors as tiles (tile()) and has no menu and no dots: a press on the leader's tile opens the color's sheet (createSheet()), a card from the bottom with what the desktop has on the row: Team 1 and
//     Team 2, the Color row that replaces the dragging (move there, or swap with the player who is there), Remove (it asks first), and for a color that no person holds the same five choices as the menu. A guest's tile is a picture: nothing opens.
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

    // ---- a phone held upright: the tile of a color ------------------------------------------------------------------------------------------------------------------------------------
    function node(doc, tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
    // Fills the color's li (the page has set its class and label and emptied it) with the tile of a card of front/lobby_rules.js viewOf(): the ant and, at its right, the team's tag, the status mark and, on the leader's tile, an arrow;
    // under them the color's name with the You and Host tags, and the name on a line of its own (it ends in dots where it does not fit). words is { on, off }: what the mark says to a screen reader. The leader's tile has a button over
    // all of it (data-tap holds the seat): the tile's one target. A guest's tile has no button: it is a picture.
    function tile(doc, li, card, words) {
        var box = node(doc, 'div', 'antbox'), txt = node(doc, 'div', 'txt'), line = node(doc, 'div', 'tline'), name = node(doc, 'span', 'pname'), tail = node(doc, 'div', 'tail'), shown = '', team = 0, i;
        box.appendChild(node(doc, 'span', 'ant ant-' + card.color.id));
        li.appendChild(box);
        line.appendChild(node(doc, 'span', 'cname', card.color.name));
        for (i = 0; i < card.who.length; i++) { if (card.who[i].cls === 'you' || card.who[i].cls === 'htag') line.appendChild(node(doc, 'span', card.who[i].cls, card.who[i].text)); else if (!shown) shown = card.who[i].text; }
        name.appendChild(node(doc, 'span', 'nm', shown || nameOf(card.mode)));
        txt.appendChild(line); txt.appendChild(name);
        if (card.stat) txt.appendChild(node(doc, 'span', card.statCls, card.stat));
        li.appendChild(txt);
        if (card.teams) { for (i = 0; i < card.teams.length; i++) if (card.teams[i].pressed) team = card.teams[i].side; } else if (card.tab) team = Number(card.tab.charAt(card.tab.length - 1));
        if (team) { var tag = node(doc, 'span', 'tteam t' + team); tag.appendChild(node(doc, 'span', 'tw', 'Team ')); tag.appendChild(node(doc, 'span', '', String(team))); tail.appendChild(tag); }
        if (card.ready) { var mark = node(doc, 'span', 'tmark ' + (card.ready.on ? 'on' : 'no') + (card.ready.bot ? ' bot' : '')); mark.appendChild(node(doc, 'span', 'sr', card.ready.on ? words.on : words.off)); tail.appendChild(mark); }
        if (card.grip) tail.appendChild(node(doc, 'i', 'tchev'));
        li.appendChild(tail);
        if (card.grip) {
            var tap = node(doc, 'button', 'tap');
            tap.type = 'button'; tap.setAttribute('data-key', 'tap:' + card.seat); tap.setAttribute('data-tap', String(card.seat)); tap.setAttribute('aria-haspopup', 'dialog'); tap.setAttribute('aria-label', 'Change ' + card.label);
            li.appendChild(tap);
        }
    }

    // ---- a phone held upright: the leader's sheet for a color -------------------------------------------------------------------------------------------------------------------------
    // The sheet of the Room's leader for one color (the page draws it from front/lobby_rules.js sheetOf(), and draws it again whenever the room changes while it is open). It knows nothing of the room: it shows what it is given and says what was
    // pressed. o: { doc, hold(on) (the page behind takes no press while it is open) }. Returns { open(spec), paint(spec), close(), isOpen(), seat(), back }.
    // spec: sheetOf()'s { seat, id, name, kind, title, parts, teams, colors, mode, remove } and { off (the plan's words that are off), note (a line that the page says: a refused press), onTeam(side), onColor(seat), onMode(word), onBlocked(word),
    // onRemove(), onRemoveYes(), onRemoveNo(), onClose(keepFocus) }.
    //   * Open: the page (a press on a tile). Close: Done, the cross, Escape, a press that starts and ends on the dark ground (a drag from inside the card does not) or the page (close()); the focus goes into the card, and a close that a person
    //     made gives it back (onClose(true)); a close by the page (the player left, the window turned) does not.
    //   * A press on a team, a color or a choice is told to the page and nothing else happens here: the card shows the new state when the page draws it again (the focus stays on the same button).
    //   * Remove asks first: its press is told to the page, which draws the question (Remove, Keep); the focus is on Keep.
    var SHEET = { team: 'Team', color: 'Color', remove: 'Remove from the room', yes: 'Remove', keep: 'Keep', done: 'Done', close: 'Close', openLine: 'takes the next player who joins' };
    function createSheet(o) {
        var doc = o.doc, back = node(doc, 'div', 'rs-back ss-back'), box = node(doc, 'div', 'rs ss'), shown = false, spec = null, drawn = '', asking = false, down = false, hit = false;
        back.hidden = true;
        box.setAttribute('role', 'dialog'); box.setAttribute('aria-modal', 'true'); box.setAttribute('aria-labelledby', 'ss-title'); box.setAttribute('tabindex', '-1');
        back.appendChild(box); doc.body.appendChild(back);
        function close(keep) {
            if (!shown) return;
            var s = spec;
            shown = false; spec = null; drawn = ''; back.hidden = true;
            if (o.hold) o.hold(false);
            if (s && s.onClose) s.onClose(!!keep);
        }
        function say(name) { return function () { if (spec && spec[name]) spec[name].apply(null, arguments); }; }
        function button(keyed, key, cls, text, press) {
            var b = node(doc, 'button', cls, text);
            b.type = 'button'; b.setAttribute('data-key', key); b.addEventListener('click', press); keyed[key] = b;
            return b;
        }
        function paint(s) {
            if (!shown) return;
            if (JSON.stringify(s) === drawn) { spec = s; return; }       // (a card that the room did not change is not made again: a press that is under the finger would be lost)
            drawn = JSON.stringify(s);
            var keyed = {}, was = doc.activeElement && box.contains(doc.activeElement) ? doc.activeElement.getAttribute('data-key') : null, ghost = s.kind === 'open' || s.kind === 'nobody', called = s.title || nameOf(s.mode), top = node(doc, 'div', 'rs-top'), who = node(doc, 'div', 'ss-who'), ab = node(doc, 'div', 'antbox'), head = node(doc, 'div'), sub = node(doc, 'p', 'ss-sub'), nm = node(doc, 'b', '', s.name), title = node(doc, 'h3', '', called);
            spec = s;
            box.textContent = '';
            title.id = 'ss-title'; nm.setAttribute('data-c', s.id);
            ab.appendChild(node(doc, 'span', 'ant ant-' + s.id + (ghost ? ' ghost' : '')));
            sub.appendChild(nm); if (s.parts.length) sub.appendChild(node(doc, 'span', '', ' \u00b7 ' + s.parts.join(' \u00b7 ')));
            head.appendChild(title); head.appendChild(sub); who.appendChild(ab); who.appendChild(head); top.appendChild(who);
            var x = button(keyed, 'x', 'rs-x', undefined, function () { close(true); }); x.setAttribute('aria-label', SHEET.close);
            top.appendChild(x); box.appendChild(top);
            if (s.teams) {
                var team = node(doc, 'div', 'rs-rule inline'), set = node(doc, 'div', 'ss-team');
                set.setAttribute('role', 'group'); set.setAttribute('aria-label', 'Team of ' + called);
                s.teams.forEach(function (t) {
                    var b = button(keyed, 'team:' + t.side, 'ss-tb', 'Team ' + t.side, function () { say('onTeam')(t.side); });
                    b.setAttribute('aria-pressed', String(t.pressed)); if (t.disabled) b.setAttribute('aria-disabled', 'true');
                    set.appendChild(b);
                });
                team.appendChild(node(doc, 'b', '', SHEET.team)); team.appendChild(set); box.appendChild(team);
            }
            if (s.colors) {
                var rule = node(doc, 'div', 'rs-rule inline'), row = node(doc, 'div', 'ss-colors');
                row.setAttribute('role', 'group'); row.setAttribute('aria-label', 'Color of ' + called);
                s.colors.forEach(function (c) {
                    var b = button(keyed, 'color:' + c.seat, 'ss-cb', undefined, function () { if (!c.here) say('onColor')(c.seat); });
                    b.setAttribute('data-c', c.id); b.setAttribute('aria-label', c.label); b.setAttribute('aria-pressed', String(c.here));
                    b.appendChild(node(doc, 'span', 'ant ant-' + c.id + (c.kind === 'open' || c.kind === 'nobody' ? ' ghost' : '')));
                    row.appendChild(b);
                });
                rule.appendChild(node(doc, 'b', '', SHEET.color)); rule.appendChild(row); rule.appendChild(node(doc, 'p', '', 'Tap a color to move ' + called + ' there. A player on it swaps places.')); box.appendChild(rule);
            }
            if (s.mode) {
                var list = node(doc, 'div', 'ss-list');
                list.setAttribute('role', 'radiogroup'); list.setAttribute('aria-label', s.name + ' is');
                CHOICES.forEach(function (c, k) {
                    var on = c.word === s.mode, off = !!(s.off && s.off.indexOf(c.word) !== -1), line = c.word === 'open' ? SHEET.openLine : c.line;
                    var b = button(keyed, 'mode:' + c.word, 'ss-item' + (on ? ' sel' : '') + (off ? ' off' : '') + (k === CHOICES.length - 1 ? ' sep' : ''), undefined, function () { if (off) say('onBlocked')(c.word); else if (!on) say('onMode')(c.word); });
                    b.setAttribute('role', 'radio'); b.setAttribute('aria-checked', String(on)); b.setAttribute('data-word', c.word); if (off) b.setAttribute('aria-disabled', 'true');
                    b.appendChild(node(doc, 'span', 'sm-name', c.name)); if (line) b.appendChild(node(doc, 'small', 'sm-line', line));
                    list.appendChild(b);
                });
                box.appendChild(list);
            }
            var asked = s.remove === 2;
            if (s.remove === 1) box.appendChild(button(keyed, 'rm', 'ss-remove', SHEET.remove, say('onRemove')));
            else if (asked) {
                var ask = node(doc, 'div', 'ss-ask'), line = 'Remove ' + called + ' from the room?';
                ask.setAttribute('role', 'group'); ask.setAttribute('aria-label', line);
                ask.appendChild(node(doc, 'p', '', line));
                ask.appendChild(button(keyed, 'yes', 'ss-yes', SHEET.yes, say('onRemoveYes'))); ask.appendChild(button(keyed, 'no', 'ss-keep', SHEET.keep, say('onRemoveNo')));
                box.appendChild(ask);
            }
            if (s.note) { var note = node(doc, 'p', 'ss-note', s.note); note.setAttribute('role', 'status'); box.appendChild(note); }
            box.appendChild(button(keyed, 'done', 'btn', SHEET.done, function () { close(true); }));
            var focus = asked && !asking ? keyed.no : !asked && asking && keyed.rm ? keyed.rm : was && keyed[was] ? keyed[was] : null;       // (the question that has just opened: the safe answer has the focus; one that is answered with Keep: back on Remove)
            asking = asked;
            if (focus && focus.focus) focus.focus();
        }
        function open(s) {
            if (shown) { paint(s); return; }
            shown = true; asking = false; back.hidden = false;
            if (o.hold) o.hold(true);
            paint(s);
            if (box.focus) box.focus();
        }
        back.addEventListener('keydown', function (e) { if (e && e.key === 'Escape') { if (e.preventDefault) e.preventDefault(); close(true); } });
        back.addEventListener('pointerdown', function (e) { down = e.target === back; hit = false; });
        back.addEventListener('pointerup', function (e) { hit = down && e.target === back; down = false; });
        back.addEventListener('pointercancel', function () { down = hit = false; });
        back.addEventListener('click', function (e) { var h = hit; hit = false; if (h && e.target === back) close(true); });
        return { open: open, paint: paint, close: function () { close(false); }, isOpen: function () { return shown; }, seat: function () { return shown && spec ? spec.seat : -1; }, back: back, box: box };
    }

    return { create: create, createSheet: createSheet, tile: tile, CHOICES: CHOICES, nameOf: nameOf };
});
