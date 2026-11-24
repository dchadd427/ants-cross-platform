// The Fog of War switch of the front page's Room (web/lobby.html; protocol 28, docs/PLAY_MATCH_SETUP.md "Fog of War"): one row of the Rules sheet (front/room_sheet.js): the name, the switch (role "switch", the look of the
// Direct connection switch) and one line of what it does. For the leader it works; for a guest it shows the room's state and takes no press. It knows nothing of the room: the page says what to show (paint) and is told what the leader
// pressed (onPress). The question that turning fog on asks when computer players are planned is built here (question) and asked by the page.
//   * ES5 and no module system: a page gets the global AntsRoomFog, node gets module.exports (the DOM comes in as doc).
//   * Nothing here puts a text into markup: every text is set with textContent.
//   * The ids and classes (fg-switch, fg-label, fg-note, fogsw) are fixed, so the control exists once on a page.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsRoomFog = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var TEXT = {
        label: 'Fog of War',
        note: 'Hides the map on players’ screens. Not cheat-proof yet. No bots with fog.',
        yes: 'Turn fog on',
        refusal: 'Bots cannot play with Fog of War. Set the bot colors to Open or Closed first.'
    };
    // "Fog of War has no computer players. Set Blue and Black to Open and turn fog on?" (names: the colors that hold a planned computer player)
    function question(names) {
        var list = names.length < 2 ? names.join('') : names.slice(0, -1).join(', ') + ' and ' + names[names.length - 1];
        return 'Fog of War has no computer players. Set ' + list + ' to Open and turn fog on?';
    }

    // o: { doc, host (the rule's row in the sheet), onPress(on) }. Returns { paint(state), switchNode, note }.
    // state: { on, guest, disabled }
    function create(o) {
        var doc = o.doc, host = o.host;
        function el(tag, cls, text) {
            var n = doc.createElement(tag);
            if (cls) n.className = cls;
            if (text !== undefined) n.textContent = text;
            return n;
        }
        var row = el('span', 'dr-row');
        var label = el('span', 'dr-label', TEXT.label); label.id = 'fg-label';
        var sw = el('button', 'dr-switch'); sw.type = 'button'; sw.id = 'fg-switch';
        sw.setAttribute('role', 'switch'); sw.setAttribute('aria-checked', 'false'); sw.setAttribute('aria-labelledby', 'fg-label'); sw.setAttribute('aria-describedby', 'fg-note');
        sw.appendChild(el('span', 'dr-knob')); sw.disabled = true;       // (no press until the page says what the room is)
        row.appendChild(label); row.appendChild(sw);
        var note = el('p', 'fg-note', TEXT.note); note.id = 'fg-note';
        host.className = (host.className ? host.className + ' ' : '') + 'fogsw direct';
        host.appendChild(row); host.appendChild(note);
        var on = false;
        sw.addEventListener('click', function () { if (!sw.disabled && o.onPress) o.onPress(!on); });
        function paint(s) {
            on = !!s.on;
            host.classList.toggle('is-on', on);
            sw.setAttribute('aria-checked', String(on)); sw.disabled = !!s.disabled;
        }
        return { paint: paint, switchNode: sw, note: note };
    }

    return { create: create, TEXT: TEXT, question: question };
});
