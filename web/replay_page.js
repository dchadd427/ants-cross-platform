/* The small helpers that the two pages of the replays share (docs/REPLAYS.md "Watching"): the list (watch.html) and the game page in replay mode (play.html?replay=<file>, web/shell.html).
   Plain functions with no page in them, so that tests/scripts/web_replay_check.js runs them on a table. Everything that comes from a recording (a name that a player typed, a map) is TEXT:
   the pages put it in with textContent and never as markup. */
(function (root) {
    'use strict';

    // The file name of a replay, as the server's store makes it (ants-<MAP>-<YYYYMMDD>-<HHMMSS>Z[-N].antsrep) and as docker/nginx.conf lets it through; nothing else is ever fetched.
    var FILE_RE = /^ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?\.antsrep$/;
    var SEATS = ['Green', 'Red', 'Blue', 'Black'];                       // the colours of seats 0 - 3
    var MAPS = { TINY: 'Tiny', SMALL: 'Small', MEDIUM: 'Medium', GAUNTLET: 'Gauntlet', TREASURE: 'Treasure', ISLANDS: 'Islands' };     // the original's six, which the pages have a picture of
    var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

    // The replay that an address asks for (?replay=<file>): the name when it has the shape above, else null
    function fileOf(search) {
        var q;
        try { q = new URLSearchParams(search); } catch (e) { return null; }
        var file = q.get('replay');
        return typeof file === 'string' && FILE_RE.test(file) ? file : null;
    }

    // When the match ended, in seconds since 1970 (UTC), from the stamp in the file's name; null for a name of another shape
    function endedOf(file) {
        var m = /^ants-[A-Za-z0-9_]{1,24}-([0-9]{4})([0-9]{2})([0-9]{2})-([0-9]{2})([0-9]{2})([0-9]{2})Z/.exec(typeof file === 'string' ? file : '');
        if (!m) return null;
        var ms = Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +m[6]);
        return isFinite(ms) ? Math.floor(ms / 1000) : null;
    }

    // "7:32 AM" in the visitor's own time zone
    function clockText(date) {
        var h = date.getHours(), m = date.getMinutes();
        return (h % 12 === 0 ? 12 : h % 12) + ':' + (m < 10 ? '0' : '') + m + (h < 12 ? ' AM' : ' PM');
    }
    function dayStart(date) { return new Date(date.getFullYear(), date.getMonth(), date.getDate()); }
    // The day and the time of a match in the visitor's own time zone: "Today, 7:32 AM", "Yesterday, 10:15 PM", "Oct 6, 11:30 AM" (with the year for another year). `ended` is seconds since 1970, `now` a Date
    function whenText(ended, now) {
        if (typeof ended !== 'number' || !isFinite(ended)) return '';
        var at = new Date(ended * 1000);
        var days = Math.round((dayStart(now).getTime() - dayStart(at).getTime()) / 86400000);
        var day = days === 0 ? 'Today' : days === 1 ? 'Yesterday' : MONTHS[at.getMonth()] + ' ' + at.getDate() + (at.getFullYear() !== now.getFullYear() ? ', ' + at.getFullYear() : '');
        return day + ', ' + clockText(at);
    }

    // The length of a match as a clock: 12:00, 0:07, 61:05
    function lengthText(seconds) {
        var s = Math.max(0, Math.floor(typeof seconds === 'number' && isFinite(seconds) ? seconds : 0));
        return Math.floor(s / 60) + ':' + (s % 60 < 10 ? '0' : '') + (s % 60);
    }

    // The map of a recording: the key of one of the original's six ("treasure"), else ''
    function mapKey(map) {
        var name = typeof map === 'string' ? map.replace(/\.lvl$/i, '').toUpperCase() : '';
        return Object.prototype.hasOwnProperty.call(MAPS, name) ? name.toLowerCase() : '';
    }
    // ... and what the pages call it: "Treasure"; a map that is not one of the six by the name of its file without the ending
    function mapTitle(map) {
        var name = typeof map === 'string' ? map.replace(/\.lvl$/i, '') : '';
        var up = name.toUpperCase();
        return Object.prototype.hasOwnProperty.call(MAPS, up) ? MAPS[up] : name;
    }

    // A player as the list gives it: "Green (Mika)" is the colour word and, after the first " (", what the player typed (or "Bot (Medium)" for a computer player); a bare "Green" typed nothing
    function parsePlayer(text) {
        var s = typeof text === 'string' ? text : '';
        var m = /^([A-Za-z]+) \(([\s\S]*)\)$/.exec(s);
        return m ? { colour: m[1], name: m[2] } : { colour: s, name: '' };
    }
    // The players of a recording that the game reports (the roster's bits, the names of the four seats), in the list's words
    function playersOf(roster, names) {
        var out = [];
        for (var seat = 0; seat < 4; seat++) {
            if (!((roster >> seat) & 1)) continue;
            var name = names && typeof names[seat] === 'string' ? names[seat] : '';
            out.push(name ? SEATS[seat] + ' (' + name + ')' : SEATS[seat]);
        }
        return out;
    }
    // The chips of a match as a list element: a square in the colour and the name (the colour word where nothing was typed); the colour word is a tip and for a screen reader. Text only.
    function chipsOf(doc, players) {
        var ul = doc.createElement('ul');
        ul.className = 'chips';
        ul.setAttribute('aria-label', 'Who played');
        (players || []).forEach(function (p) {
            var q = parsePlayer(p);
            var li = doc.createElement('li');
            li.className = 'chip';
            li.setAttribute('data-c', SEATS.indexOf(q.colour) !== -1 ? q.colour : '');
            li.title = q.name ? q.colour + ': ' + q.name : q.colour;
            var square = doc.createElement('i');
            var nm = doc.createElement('span');
            nm.className = 'nm';
            nm.textContent = q.name || q.colour;
            li.appendChild(square);
            li.appendChild(nm);
            ul.appendChild(li);
        });
        return ul;
    }

    var api = { FILE_RE: FILE_RE, SEATS: SEATS, fileOf: fileOf, endedOf: endedOf, whenText: whenText, lengthText: lengthText, mapKey: mapKey, mapTitle: mapTitle, parsePlayer: parsePlayer, playersOf: playersOf, chipsOf: chipsOf };
    if (typeof module === 'object' && module && module.exports) module.exports = api;
    else root.AntsReplay = api;
})(typeof window !== 'undefined' ? window : this);
