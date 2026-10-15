/* The small helpers that the pages of the replays share (docs/REPLAYS.md "Watching a replay"): the pages of the match history (live.html, matches.html, match.html) and the game page in replay mode (play.html?replay=<file>, web/shell.html),
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

    // ---- the camera of a replay (docs/REPLAYS.md "Camera"): Auto follows the action, Manual leaves the picture where it is put ----
    // The choice is the button's and is remembered on this device (localStorage, `ants.cam`: "auto" or "manual"; Auto when nothing is kept or the browser will not say). Taking the camera by hand does not
    // write it: the viewer did not choose Manual for the next visit. Every access to the storage may throw (a private window, blocked site data): nothing then is remembered and the page works as ever.
    var CAM_KEY = 'ants.cam';
    var CAM_NOTE_MS = 7000;     // how long the note stays over the picture
    function camLoad(storage) {
        try { return storage && storage.getItem(CAM_KEY) === 'manual' ? 'manual' : 'auto'; } catch (e) { return 'auto'; }
    }
    function camSave(storage, mode) {
        try {
            if (!storage) return false;
            storage.setItem(CAM_KEY, mode === 'manual' ? 'manual' : 'auto');
            return true;
        } catch (e) { return false; }
    }
    // The words of the button for the mode that is on (the button names what the camera is doing now), and what pressing it does
    function camLabel(auto) { return auto ? 'Auto camera' : 'Manual camera'; }
    function camTitle(auto) {
        return auto ? 'Auto camera: the picture follows the action. Press for Manual camera: the picture stays where you put it.'
                    : 'Manual camera: the picture stays where you put it. Press for Auto camera: the picture follows the action again.';
    }
    // Is the note due? `taken` is how many times the game says the viewer took the camera, `seen` how many the page has looked at, `shown` that the note has been up in this visit
    function camNoteDue(taken, seen, shown) { return !shown && typeof taken === 'number' && taken > seen; }

    // The camera button and its note, for the game page in replay mode. `env` has what the page has: `doc`, `storage` (or null), `get(what)` and `command(what, value)` (the game: Module._ants_replay_get and _do),
    // `VALUE` (camera, taken) and `DO` (camera) as the page numbers them, and `setTimeout`. The game moves the view and says how it stands; this is the page's half: the button, what the device chose, the note.
    function cameraControl(env) {
        var pref = camLoad(env.storage), sent = false, seen = 0, shown = false, painted = null;
        function el(id) { return env.doc.getElementById(id); }
        function paint(auto) {
            if (painted === auto) return;
            painted = auto;
            var b = el('b-cam');
            b.setAttribute('data-mode', auto ? 'auto' : 'manual');
            b.setAttribute('aria-pressed', auto ? 'true' : 'false');                  // (the name stays "Auto camera", a toggle that is pressed while Auto is on; the title says what a press does)
            b.title = camTitle(auto);
            el('cam-lbl').textContent = camLabel(auto);
        }
        function note(show) {
            el('camnote').hidden = !show;
            if (show) env.setTimeout.call(undefined, function () { el('camnote').hidden = true; }, CAM_NOTE_MS);          // (called with no `this`: a browser's own setTimeout refuses another)
        }
        paint(pref !== 'manual');
        var api = {
            // every look at the game: the first time that it can hear, it is told what this device chose (it starts on Auto); then the note when the viewer has taken the camera (once in a visit), and the button as the game says
            look: function (ready) {
                if (!ready) return;
                if (!sent) { sent = true; env.command(env.DO.camera, pref === 'manual' ? 0 : 1); }
                var taken = env.get(env.VALUE.taken);
                if (camNoteDue(taken, seen, shown)) { shown = true; note(true); }
                seen = Math.max(seen, taken);
                paint(env.get(env.VALUE.camera) === 1);
            },
            // the button: the other camera, remembered; Auto glides back to the busiest place now, else the last thing that happened, and locks on it (the game does it)
            press: function () {
                var auto = env.get(env.VALUE.camera) === 1;
                env.command(env.DO.camera, auto ? 0 : 1);
                pref = auto ? 'manual' : 'auto';
                camSave(env.storage, pref);
                note(false);
                paint(!auto);
            },
            // the button's events: a press, and Enter and Space (pressed here, as the Picture button's: the game's own key handling takes the browser's press of a focused button away); `after` repaints the bar
            wire: function (after) {
                var b = el('b-cam');
                b.addEventListener('click', function () { api.press(); after(); });
                b.addEventListener('keydown', function (ev) {
                    if (ev.key !== 'Enter' && ev.key !== ' ') return;
                    ev.preventDefault();
                    if (!ev.repeat) b.click();
                });
                b.addEventListener('keyup', function (ev) { if (ev.key === ' ') ev.preventDefault(); });
            },
            // fullscreen: the bar lies over the picture's bottom, the note goes above it
            fullscreen: function (over) { el('camnote').classList.toggle('up', !!over); },
            pref: function () { return pref; }
        };
        return api;
    }

    // ---- Back 30 seconds (docs/REPLAYS.md "Watching a replay") ----
    // The button right of Play / Pause: a left-pointing double triangle with "30" under it. It seeks through the page's own seek (the timeline's: the game plays the match again from its first turn up to the
    // turn, as for a drag), so the play / pause state and the speed stay. It is made here (the page's markup has none) and is not there for a match that is being played (?live=).
    var REWIND_SECONDS = 30, TURNS_PER_SECOND = 20;
    // The turn that a press goes to from `turn`: 30 seconds (600 turns) earlier, never before the first turn
    function rewindTurn(turn, seconds, tps) {
        var back = (typeof seconds === 'number' ? seconds : REWIND_SECONDS) * (typeof tps === 'number' ? tps : TURNS_PER_SECOND);
        return Math.max(0, Math.floor(typeof turn === 'number' && isFinite(turn) ? turn : 0) - back);
    }
    // `env`: `doc`, `turn()` (the turn that the playhead is at, or on its way to) and `seek(seconds)` (the page's seek). Returns { button, press, show(on), wire(after) }.
    function rewindControl(env) {
        var ns = 'http://www.w3.org/2000/svg';
        var b = env.doc.createElement('button');
        b.className = 'btn rplay rew'; b.type = 'button'; b.id = 'b-rew';
        b.title = 'Back ' + REWIND_SECONDS + ' seconds';
        b.setAttribute('aria-label', b.title);
        var svg = env.doc.createElementNS(ns, 'svg'), path = env.doc.createElementNS(ns, 'path'), n = env.doc.createElement('span');
        svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('aria-hidden', 'true');
        path.setAttribute('d', 'M11.5 4.5v15L2 12zM22 4.5v15L12.5 12z');
        n.className = 'n'; n.textContent = String(REWIND_SECONDS);
        svg.appendChild(path); b.appendChild(svg); b.appendChild(n);
        var play = env.doc.getElementById('b-play');
        play.parentNode.insertBefore(b, play.nextSibling);
        var api = {
            button: b,
            press: function () { env.seek(rewindTurn(env.turn()) / TURNS_PER_SECOND); },
            show: function (on) { b.hidden = !on; },
            // a press, and Enter and Space (pressed here, as the camera button's: the game's key handling takes the browser's press of a focused button away); `after` repaints the bar
            wire: function (after) {
                b.addEventListener('click', function () { api.press(); after(); });
                b.addEventListener('keydown', function (ev) {
                    if (ev.key !== 'Enter' && ev.key !== ' ') return;
                    ev.preventDefault();
                    if (!ev.repeat) b.click();
                });
                b.addEventListener('keyup', function (ev) { if (ev.key === ' ') ev.preventDefault(); });
            }
        };
        return api;
    }

    // THE BAR UNDER A LOCKED POINTER (docs/REPLAYS.md, "Watching a replay"). A locked pointer is not over anything: the browser sends every mouse event to the canvas and nobody else, so the bar of a
    // fullscreen replay (page elements over the picture) could not be reached. The game's cursor (the one that scrolls the map at the picture's edge) is the pointer here: while it is over the visible
    // bar the motion is NOT handed to the game (the game is told that its pointer left), a small arrow is drawn over the bar, and the page makes the pointer events of a mouse at that place for what is
    // under it (the timeline's drag, the wake of the bar); a press and a release on the same button are its click, and the wheel over the bar does not zoom the map. The outermost pixels of the picture
    // are the game's even where the bar lies (the edge scrolls with the bar up), and a bar that has gone away (idle) is not there to be hit. The page gives `env`: doc, win, stage (the arrow's
    // parent), canvas, PointerEvent, MouseEvent, replayMode(), handing() (the page is handing the game an event of its own), cursorAt() ({ x, y, edge } of the locked cursor on the page, or null) and
    // leave() (tell the game that its pointer left). tests/scripts/web_replay_check.js runs it with a page of fakes.
    function barPointer(env) {
        var inBar = false;                                                  // the game's cursor is over the bar (or a press that began there goes on)
        var down = null;                                                    // the element that the press began on (the timeline's drag goes on beyond the bar)
        var control = null;                                                 // ... and its button or link, found at the press (the bar draws its icons again as the match plays)
        var swallow = false;                                                // the click that follows a release that the bar took is the bar's too
        var taken = false;                                                  // the press that is going on was the bar's (a button other than the left one, which the game never saw)
        var mark = null;
        function arrow(at) {                                                // (made when first wanted: nothing of it is in the page's markup)
            if (!mark) {
                mark = env.doc.createElement('div');
                mark.id = 'rcursor';
                mark.setAttribute('aria-hidden', 'true');
                mark.style.cssText = 'position:fixed;left:0;top:0;width:20px;height:26px;z-index:90;pointer-events:none';
                var ns = 'http://www.w3.org/2000/svg', svg = env.doc.createElementNS(ns, 'svg'), path = env.doc.createElementNS(ns, 'path');
                svg.setAttribute('viewBox', '0 0 20 26'); svg.setAttribute('width', '20'); svg.setAttribute('height', '26'); svg.style.display = 'block';
                path.setAttribute('d', 'M2 1.5v19l5-4.4 3.3 7.4 3.4-1.5-3.3-7.2H17z'); path.setAttribute('fill', '#fff'); path.setAttribute('stroke', '#000'); path.setAttribute('stroke-width', '1.6'); path.setAttribute('stroke-linejoin', 'round');
                svg.appendChild(path); mark.appendChild(svg);
                env.stage.appendChild(mark);
            }
            mark.hidden = !at;
            mark.style.display = at ? 'block' : 'none';
            if (at) mark.style.transform = 'translate(' + (at.x - 2) + 'px, ' + (at.y - 1.5) + 'px)';       // (the arrow's tip is at 2, 1.5 of its picture)
        }
        function under(cx, cy) {
            if (!env.replayMode()) return null;
            var bar = env.doc.getElementById('rbar');
            if (!bar || !bar.classList.contains('over') || bar.classList.contains('idle')) return null;
            var el = env.doc.elementFromPoint(cx, cy);
            return el && bar.contains(el) ? el : null;
        }
        function interactive(el) { return el && el.closest ? el.closest('button, a[href], summary, input, select') : null; }
        function fire(target, type, at, buttons) {
            target.dispatchEvent(new env.PointerEvent(type, { bubbles: true, cancelable: true, view: env.win, clientX: at.x, clientY: at.y, pointerId: 1000, pointerType: 'mouse', isPrimary: true, button: type === 'pointermove' ? -1 : 0, buttons: buttons }));
        }
        function hit(at) { return at.edge ? null : under(at.x, at.y); }
        var api = {
            mode: function () { return inBar; },
            under: under,
            // the lock was let go (or fullscreen ended): a drag in progress is cancelled, the arrow goes
            reset: function () {
                if (down) { var at = env.cursorAt() || { x: 0, y: 0 }; fire(down, 'pointercancel', at, 0); }
                inBar = false; down = null; control = null; swallow = false; taken = false;
                if (mark) arrow(null);
            },
            hideMark: function () { if (mark) arrow(null); },
            // the locked cursor moved; true: the bar has it
            move: function (buttons) {
                var at = env.cursorAt();
                if (!at) return false;
                var target = down || hit(at);
                if (!target) { if (inBar) { inBar = false; arrow(null); } return false; }
                if (!inBar) { inBar = true; env.leave(); }
                arrow(at);
                fire(target, 'pointermove', at, buttons);
                return true;
            },
            // a press, a release or a click of the locked mouse; true: it is the bar's (and the game must not get it)
            button: function (type, button) {
                var at = env.cursorAt();
                if (!at) return false;
                if (type === 'click') { var due = swallow || !!hit(at); swallow = false; return due; }
                var over = hit(at);
                if (type === 'mousedown') {
                    swallow = false; taken = !!over;
                    if (!over) return false;
                    if (button === 0) { down = over; control = interactive(over); fire(over, 'pointerdown', at, 1); }
                    return true;
                }
                if (!down) { var mine = taken; taken = false; return mine; }   // (a release of a press that the game saw, on the picture, is the game's even where the bar lies: it must see the button go up)
                var from = down, pressed = control;
                down = null; control = null; swallow = true; taken = false;
                fire(from, 'pointerup', at, 0);
                if (pressed) { if (!pressed.disabled && over && interactive(over) === pressed) pressed.click(); }
                else if (over) over.dispatchEvent(new env.MouseEvent('click', { bubbles: true, cancelable: true, view: env.win, clientX: at.x, clientY: at.y }));       // (a click on the bar's own ground: the Picture window closes, as it does for a free pointer)
                return true;
            }
        };
        function onButton(e) {
            if (!env.replayMode() || env.handing() || !e.isTrusted || e.target !== env.canvas || !env.cursorAt()) return;
            if (api.button(e.type, e.button)) { e.stopPropagation(); e.preventDefault(); }
        }
        ['mousedown', 'mouseup', 'click'].forEach(function (name) { env.win.addEventListener(name, onButton, true); });      // (window capture: before SDL's and the page's own)
        env.win.addEventListener('wheel', function (e) {
            var at = env.replayMode() && env.cursorAt();
            if (at && e.target === env.canvas && hit(at)) { e.stopPropagation(); e.preventDefault(); }
        }, { capture: true, passive: false });
        return api;
    }

    var api = { barPointer: barPointer, REWIND_SECONDS: REWIND_SECONDS, rewindTurn: rewindTurn, rewindControl: rewindControl, CAM_KEY: CAM_KEY, CAM_NOTE_MS: CAM_NOTE_MS, camLoad: camLoad, camSave: camSave, camLabel: camLabel, camTitle: camTitle, camNoteDue: camNoteDue, cameraControl: cameraControl, FILE_RE: FILE_RE, LIVE_RE: LIVE_RE, SEATS: SEATS, LIVE_LAG: LIVE_LAG, AT_LIVE: AT_LIVE, FOLLOW_SLACK: FOLLOW_SLACK, fileOf: fileOf, liveIdOf: liveIdOf, liveMap: liveMap, startedOf: startedOf, endedOf: endedOf, whenText: whenText, startedText: startedText,
                lengthText: lengthText, mapKey: mapKey, mapTitle: mapTitle, speedText: speedText, liveVerdict: liveVerdict, atLive: atLive, liveTag: liveTag, liveEntriesOf: liveEntriesOf, parsePlayer: parsePlayer, playersOf: playersOf, chipsOf: chipsOf };
    if (typeof module === 'object' && module && module.exports) module.exports = api;
    else root.AntsReplay = api;
})(typeof window !== 'undefined' ? window : this);
