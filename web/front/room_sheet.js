// The Rules sheet of the front page's Room (web/lobby.html; docs/PLAY_IN_BROWSER.md "Your room"): the room's rules (who can join, Fog of War, Direct connection, the note about recordings) are behind one button, "Rules", in
// the room's header, and open in a card over the page (a sheet from the bottom on a phone: front/room_sheet.css). The rules themselves are the controls of web/home/games.js (who can join), front/room_fog.js and
// front/lobby_direct.js, which stand in the card's rows; this part only opens and closes the card and looks after the focus. It knows nothing of the room.
//   * Open: a press on the button. Close: Done, the cross, Escape, a press on the dark ground outside the card (a press that starts inside the card and ends outside does not close it) or the page (close()).
//   * While it is open the page behind it takes no key or tap (hold(true): the page's inert) and the focus is in the card; closing gives the focus back to the button.
//   * ES5 and no module system: a page gets the global AntsRoomSheet, node gets module.exports (the DOM comes in as "doc").
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsRoomSheet = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    // o: { doc, back (the dark ground: hidden while closed), box (the card), opener (the button), closers ([the cross, Done]), hold(on) (the page behind takes no press while on) }. Returns { open(), close(), isOpen() }.
    function create(o) {
        var back = o.back, box = o.box, opener = o.opener, shown = false;
        function open() {
            if (shown || opener.disabled) return;
            shown = true;
            back.hidden = false;
            opener.setAttribute('aria-expanded', 'true');
            if (o.hold) o.hold(true);
            if (box.focus) box.focus();
        }
        function close(keepFocus) {
            if (!shown) return;
            shown = false;
            back.hidden = true;
            opener.setAttribute('aria-expanded', 'false');
            if (o.hold) o.hold(false);
            if (keepFocus !== false && opener.focus) opener.focus();
        }
        opener.addEventListener('click', open);
        (o.closers || []).forEach(function (b) { b.addEventListener('click', function () { close(); }); });
        back.addEventListener('keydown', function (e) { if (e && e.key === 'Escape') { if (e.preventDefault) e.preventDefault(); close(); } });
        var down = false, hit = false;       // (a press that starts and ends on the dark ground closes the card; a drag from inside it, a text selection or a scroll does not)
        back.addEventListener('pointerdown', function (e) { down = e.target === back; hit = false; });
        back.addEventListener('pointerup', function (e) { hit = down && e.target === back; down = false; });
        back.addEventListener('pointercancel', function () { down = hit = false; });
        back.addEventListener('click', function (e) { var h = hit; hit = false; if (h && e.target === back) close(); });
        return { open: open, close: function () { close(false); }, isOpen: function () { return shown; } };
    }

    return { create: create };
});
