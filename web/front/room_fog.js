// The Fog of War switch of the front page's Room (web/lobby.html; protocol 28, docs/PLAY_IN_BROWSER.md "Fog of War"): for the leader a switch in the room's header beside "Invite only | Public" (role "switch", the look of the
// Direct connection switch), for everybody a note under the link while fog is on, and for a guest a chip beside the room's other chip. It knows nothing of the room: the page says what to show (paint) and is told what the leader
// pressed (onPress). The question that turning fog on asks when computer players are planned is built here (question) and asked by the page.
//   * ES5 and no module system: a page gets the global AntsRoomFog, node gets module.exports (the DOM comes in as doc).
//   * Nothing here puts a text into markup: every text is set with textContent.
//   * The ids and classes (fg-switch, fg-label, fg-note, fogsw, fg-chip) are fixed, so the control exists once on a page.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsRoomFog = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var TEXT = {
        label: 'Fog of War', on: 'On', off: 'Off', chip: 'Fog of War',
        note: 'Fog hides the map from players’ screens; it is not cheat-proof yet.',
        yes: 'Turn fog on',
        refusal: 'Bots cannot play with Fog of War. Set the bot colors to Open or Nobody first.'
    };
    // "Fog of War has no computer players. Set Blue and Black to Open and turn fog on?" (names: the colors that hold a planned computer player)
    function question(names) {
        var list = names.length < 2 ? names.join('') : names.slice(0, -1).join(', ') + ' and ' + names[names.length - 1];
        return 'Fog of War has no computer players. Set ' + list + ' to Open and turn fog on?';
    }

    // o: { doc, head (the header's .head element), notes (the element of the room's notes), onPress(on) }. Returns { paint(state), switchNode, chip, note }.
    // state: { on, guest, disabled }
    function create(o) {
        var doc = o.doc, head = o.head, notes = o.notes;
        function el(tag, cls, text) {
            var n = doc.createElement(tag);
            if (cls) n.className = cls;
            if (text !== undefined) n.textContent = text;
            return n;
        }
        var lead = el('span', 'fogsw direct'), row = el('span', 'dr-row');
        var label = el('span', 'dr-label', TEXT.label); label.id = 'fg-label';
        var sw = el('button', 'dr-switch'); sw.type = 'button'; sw.id = 'fg-switch';
        sw.setAttribute('role', 'switch'); sw.setAttribute('aria-checked', 'false'); sw.setAttribute('aria-labelledby', 'fg-label'); sw.setAttribute('aria-describedby', 'fg-note');
        sw.appendChild(el('span', 'dr-knob'));
        var word = el('span', 'fg-word', TEXT.off); word.setAttribute('aria-hidden', 'true');
        row.appendChild(label); row.appendChild(sw); row.appendChild(word); lead.appendChild(row);
        lead.hidden = true; lead.id = 'fog';
        var note = el('p', 'fg-note', TEXT.note); note.id = 'fg-note'; note.hidden = true;
        var chip = el('span', 'fg-chip', TEXT.chip); chip.hidden = true;
        var before = head.querySelector ? head.querySelector('.code') : null;
        head.insertBefore(lead, before);                              // (the room's own switch, Invite only | Public, comes before it: web/home/games.js placeVis)
        notes.appendChild(note);
        var hl = head.querySelector ? head.querySelector('.hl') : null;
        (hl || head).appendChild(chip);
        var on = false;
        sw.addEventListener('click', function () { if (!sw.disabled && o.onPress) o.onPress(!on); });
        function paint(s) {
            on = !!s.on;
            var top = head.parentNode, shown = !s.guest;
            lead.hidden = !shown;
            lead.className = 'fogsw direct' + (on ? ' is-on' : '');
            if (top && top.classList) top.classList.toggle('has-fog', shown);       // (a phone's header grid has a row for the switch only while it shows)
            sw.setAttribute('aria-checked', String(on)); sw.disabled = !!s.disabled;
            word.textContent = on ? TEXT.on : TEXT.off;
            note.hidden = !on;
            chip.hidden = !(s.guest && on);
        }
        return { paint: paint, switchNode: sw, chip: chip, note: note };
    }

    return { create: create, TEXT: TEXT, question: question };
});
