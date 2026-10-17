// The game types of the front page's lobby (web/lobby.html), as DATA: one row for each mode (the control of front/game_type.js, the tag by the room's name and the guest's line are all made from these
// rows, so the page needs no new button or code for a new mode). A new mode is NOT only a row: its number must also be the protocol's last game mode (LAST_GAME_MODE of front/lobby_net.js, sim::kLastGameMode and
// valid_game_mode of the server), because the codec refuses a Room message above it (an older client must refuse a newer room: a protocol bump), and the simulation must play it. The pure functions that decide what the page shows and what it remembers have no browser in it, so that node can run them
// (tests/scripts/web_game_modes_check.js).
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsGameModes, node gets module.exports.
//   * A row: id (the mode's number in the plan message and the Room message of network protocol 17: sim::GameMode), word (the protocol's word of POST /rooms and of the room status, which is also what a
//     browser remembers), name (the field's text), tag (the short name by the room's name, '' for none: the default mode has none), line (one line under the name), note (shown under the field when
//     the mode is chosen, '' for none) and, on one row only, def: true (the mode that a browser starts with when it remembers nothing, or something that this list does not hold).
//   * The wire says a mode by its number. A number that no row holds is shown as "New mode", a defensive display: the codec refuses such a room today, so it cannot reach the page over the wire.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsGameModes = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var MODES = [
        { id: 0, word: 'highest-score', name: 'Highest score', tag: '', line: 'The original game. Food and power-ups; the highest score wins.', note: '', def: true },
        { id: 1, word: '187', name: '187', tag: '187', line: 'A fight with no food. Kills decide.', note: 'No food and no power-ups. The most kills wins.' }
    ];
    var UNKNOWN_NAME = 'New mode';
    var STORAGE_KEY = 'ants.lobby.gameType';       // the leader's last choice, by its word (per browser: localStorage, and the page works without it)

    function byId(id) {
        for (var i = 0; i < MODES.length; i++) if (MODES[i].id === id) return MODES[i];
        return null;
    }
    function byWord(word) {
        for (var i = 0; i < MODES.length; i++) if (typeof word === 'string' && MODES[i].word === word) return MODES[i];
        return null;
    }
    function defaultMode() {
        for (var i = 0; i < MODES.length; i++) if (MODES[i].def) return MODES[i];
        return MODES[0];
    }
    // The mode that a new room starts with: the remembered word's mode, else the default (nothing remembered, or a word that this list does not hold)
    function initialOf(word) { return byWord(word) || defaultMode(); }
    // The words of a mode for the page: { known, id, name, tag, line, note }. `id` is what the Room message says; `serverName` is a name that the server sent for a mode that this list does not hold, if it did.
    // An unknown mode is "New mode" for its tag and its name, and has no line and no note: nothing is claimed of a mode that is not known.
    function viewOf(id, serverName) {
        var m = byId(id);
        if (m) return { known: true, id: m.id, name: m.name, tag: m.tag, line: m.line, note: m.note };
        var name = typeof serverName === 'string' && serverName !== '' ? serverName : UNKNOWN_NAME;
        return { known: false, id: id, name: name, tag: UNKNOWN_NAME, line: '', note: '' };
    }
    function tagOf(id) { return viewOf(id).tag; }
    function guestLine(id, serverName) { return 'Game type: ' + viewOf(id, serverName).name; }

    // The leader's last choice. Storage may be missing, full or refused: nothing here ever throws.
    function recall(storage) {
        try { return storage ? storage.getItem(STORAGE_KEY) : null; } catch (e) { return null; }
    }
    function remember(storage, id) {
        var m = byId(id);
        if (!m || !storage) return false;
        try { storage.setItem(STORAGE_KEY, m.word); return true; } catch (e) { return false; }
    }
    // The mode number a new room should take when the room's own is `roomId`: the remembered choice, or -1 when the room has it already
    function wanted(storage, roomId) {
        var id = initialOf(recall(storage)).id;
        return id === roomId ? -1 : id;
    }

    return { MODES: MODES, UNKNOWN_NAME: UNKNOWN_NAME, STORAGE_KEY: STORAGE_KEY, byId: byId, byWord: byWord, defaultMode: defaultMode, initialOf: initialOf, viewOf: viewOf, tagOf: tagOf,
        guestLine: guestLine, recall: recall, remember: remember, wanted: wanted };
});
