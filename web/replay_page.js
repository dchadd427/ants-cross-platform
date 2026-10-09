/* The small helpers that the two pages of the replays share (docs/REPLAYS.md "Watching"): the list (watch.html) and the game page in replay mode (play.html?replay=<file>, web/shell.html),
   which also watches a match that is still being played (play.html?live=<id>). Plain functions with no page in them, so that tests/scripts/web_replay_check.js runs them on a table.
   Everything that comes from a recording or from the server's list of live matches (a name that a player typed, a map, an id) is TEXT: the pages put it in with textContent and never as markup. */
(function (root) {
    'use strict';

    // The file name of a replay, as the server's store makes it (ants-<MAP>-<YYYYMMDD>-<HHMMSS>Z[-N].antsrep) and as docker/nginx.conf lets it through; nothing else is ever fetched.
    var FILE_RE = /^ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?\.antsrep$/;
    var SEATS = ['Green', 'Red', 'Blue', 'Black'];                       // the colours of seats 0 - 3
    var MAPS = { TINY: 'Tiny', SMALL: 'Small', MEDIUM: 'Medium', GAUNTLET: 'Gauntlet', TREASURE: 'Treasure', ISLANDS: 'Islands' };     // the original's six, which the pages have a picture of
    // The id of a match that is being played, as the server makes it (<MAP>-<YYYYMMDD>-<HHMMSS>Z[-N]: the map's stem and the UTC time the match began) and as docker/nginx.conf lets it through; nothing else is ever fetched.
    var LIVE_RE = /^[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?$/;
    var LIVE_LAG = 10;          // seconds: what the picture of a live match is behind the players at best (the age of the snapshot that the page holds, and the 2 s that the game holds back)
    var AT_LIVE = 1.5;          // a playhead that is less than this many seconds behind the game's Limit is at the present
    var FOLLOW_SLACK = 8;       // ... and one that follows the match stays there while it is less than this behind: the snapshots come in steps (a poll brings up to 7 s of the match at once)
    var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

    // The replay that an address asks for (?replay=<file>): the name when it has the shape above, else null
    function fileOf(search) {
        var q;
        try { q = new URLSearchParams(search); } catch (e) { return null; }
        var file = q.get('replay');
        return typeof file === 'string' && FILE_RE.test(file) ? file : null;
    }

    // The live match that an address asks for (?live=<id>): the id when it has the shape above, else null
    function liveIdOf(search) {
        var q;
        try { q = new URLSearchParams(search); } catch (e) { return null; }
        var id = q.get('live');
        return typeof id === 'string' && LIVE_RE.test(id) ? id : null;
    }
    // The map's stem of an id ("TREASURE"), and when the match began in seconds since 1970 (UTC, from the stamp in the id); '' and null for text of another shape
    function liveMap(id) {
        var m = /^([A-Za-z0-9_]{1,24})-[0-9]{8}-[0-9]{6}Z/.exec(typeof id === 'string' ? id : '');
        return m ? m[1] : '';
    }
    function startedOf(id) {
        var m = /^[A-Za-z0-9_]{1,24}-([0-9]{4})([0-9]{2})([0-9]{2})-([0-9]{2})([0-9]{2})([0-9]{2})Z/.exec(typeof id === 'string' ? id : '');
        if (!m) return null;
        var ms = Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +m[6]);
        return isFinite(ms) ? Math.floor(ms / 1000) : null;
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

    // "Started 8:21 AM" for a match that began today, "Started Yesterday, 11:50 PM" for one that began on another day (a long match across midnight); '' for no time
    function startedText(started, now) {
        var text = whenText(started, now);
        return text ? 'Started ' + text.replace(/^Today, /, '') : '';
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

    // The speed that the game reports (percent: 50, 100, 200 ...) as the bar writes it: "½×", "1×", "2×"
    function speedText(percent) {
        return (percent === 50 ? '\u00bd' : percent / 100) + '\u00d7';
    }

    // ---- watching a match that is being played ----
    // What the door answered for GET /live/<id> (docs/REPLAYS.md "Watching a live match"): status and the parsed JSON body (null when it was not JSON). 200 is the match so far; 404 with `ended` and the
    // name of the kept replay ('' when it was not kept) is a match that is over; any other 404 is a match that is not live; everything else (a 5xx, a refused connection: status 0) is a feed that fails.
    function liveVerdict(status, body) {
        if (status === 200) return { kind: 'running', replay: '' };
        if (status !== 404) return { kind: 'error', replay: '' };
        if (body && typeof body === 'object' && body.ended === true) return { kind: 'ended', replay: typeof body.replay === 'string' && FILE_RE.test(body.replay) ? body.replay : '' };
        return { kind: 'notlive', replay: '' };
    }
    // Is the playhead of a live match at the present? `gap` is the seconds from the playhead to the game's Limit (the last turn that can be played now), `was` whether it was at the present a moment ago and
    // `playing` whether it plays. The Limit moves in steps (a poll brings up to a few seconds of the match at once), so a playhead that follows it, at 1x or faster, is inside the step most of the time:
    // once at the present it stays there while the gap is less than FOLLOW_SLACK. A paused or dragged playhead, and one that was moved away, is at the present only inside AT_LIVE (the caller says
    // `was` is false after the viewer moved it).
    function atLive(was, gap, playing) {
        if (typeof gap !== 'number' || !(gap >= 0)) return false;
        return gap < AT_LIVE || (!!was && !!playing && gap < FOLLOW_SLACK);
    }
    // The words of the tag over a live picture, after "LIVE": waiting for the match while the feed has stopped and the playhead has caught up; "Paused · 0:59 behind"; "about 10 s behind" at the present;
    // else the speed and how far behind the players. `gap` is the playhead's seconds behind the game's Limit; the picture is LIVE_LAG seconds behind the players at best, so "behind" is gap + LIVE_LAG.
    function liveTag(o) {
        var behind = lengthText(Math.round(Math.max(0, o.gap || 0)) + LIVE_LAG) + ' behind';
        if (o.waiting) return 'waiting for the match';
        if (o.paused) return 'Paused \u00b7 ' + behind;
        if (o.at) return 'about ' + LIVE_LAG + ' s behind';
        return o.speed + ' \u00b7 ' + behind;
    }
    // The matches of the list's answer (`{ live: [{ id, map, started, turns, seconds, players }], count }`) as the page shows them, and only those: a match whose id has the shape above, at most 50
    // of them, newest first as the server sends them; a number that is not one is 0 (seconds from the turns when only they are there), a player that is not a string is dropped, at most four of them.
    function liveEntriesOf(data) {
        var list = data && typeof data === 'object' && Array.isArray(data.live) ? data.live : [];
        var out = [];
        list.forEach(function (e) {
            if (out.length >= 50 || !e || typeof e !== 'object' || typeof e.id !== 'string' || !LIVE_RE.test(e.id)) return;
            var seconds = typeof e.seconds === 'number' && isFinite(e.seconds) ? e.seconds : typeof e.turns === 'number' && isFinite(e.turns) ? e.turns / 20 : 0;
            out.push({
                id: e.id,
                map: typeof e.map === 'string' ? e.map : liveMap(e.id),
                started: typeof e.started === 'number' && isFinite(e.started) ? e.started : startedOf(e.id),
                seconds: Math.max(0, Math.floor(seconds)),
                players: (Array.isArray(e.players) ? e.players : []).filter(function (p) { return typeof p === 'string'; }).slice(0, 4)
            });
        });
        return out;
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

    var api = { FILE_RE: FILE_RE, LIVE_RE: LIVE_RE, SEATS: SEATS, LIVE_LAG: LIVE_LAG, AT_LIVE: AT_LIVE, FOLLOW_SLACK: FOLLOW_SLACK, fileOf: fileOf, liveIdOf: liveIdOf, liveMap: liveMap, startedOf: startedOf, endedOf: endedOf, whenText: whenText, startedText: startedText,
                lengthText: lengthText, mapKey: mapKey, mapTitle: mapTitle, speedText: speedText, liveVerdict: liveVerdict, atLive: atLive, liveTag: liveTag, liveEntriesOf: liveEntriesOf, parsePlayer: parsePlayer, playersOf: playersOf, chipsOf: chipsOf };
    if (typeof module === 'object' && module && module.exports) module.exports = api;
    else root.AntsReplay = api;
})(typeof window !== 'undefined' ? window : this);
