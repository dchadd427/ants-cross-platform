/* The small helpers that the pages of the replays share (docs/REPLAYS.md "Watching a replay"): the pages of the match history (live.html, matches.html, match.html) and the game page in replay mode (play.html?replay=<file>, web/shell.html),
   which also watches a match that is still being played (play.html?live=<id>). Plain functions with no page in them, so that tests/scripts/web_replay_check.js runs them on a table.
   Everything that comes from a recording or from the server's list of live matches (a name that a player typed, a map, an id) is TEXT: the pages put it in with textContent and never as markup. */
(function (root) {
    'use strict';

    // The file name of a replay, as the server's store makes it (ants-<MAP>-<YYYYMMDD>-<HHMMSS>Z[-N].antsrep) and as docker/nginx.conf lets it through; nothing else is ever fetched.
    var FILE_RE = /^ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?\.antsrep$/;
    var SEATS = ['Green', 'Red', 'Blue', 'Black'];                       // the colors of seats 0 - 3
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

    // A player as the list gives it: "Green (Mika)" is the color word and, after the first " (", what the player typed (or "Bot (Medium)" for a computer player); a bare "Green" typed nothing
    function parsePlayer(text) {
        var s = typeof text === 'string' ? text : '';
        var m = /^([A-Za-z]+) \(([\s\S]*)\)$/.exec(s);
        return m ? { color: m[1], name: m[2] } : { color: s, name: '' };
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
    // The chips of a match as a list element: a square in the color and the name (the color word where nothing was typed); the color word is a tip and for a screen reader. Text only.
    function chipsOf(doc, players) {
        var ul = doc.createElement('ul');
        ul.className = 'chips';
        ul.setAttribute('aria-label', 'Who played');
        (players || []).forEach(function (p) {
            var q = parsePlayer(p);
            var li = doc.createElement('li');
            li.className = 'chip';
            li.setAttribute('data-c', SEATS.indexOf(q.color) !== -1 ? q.color : '');
            li.title = q.name ? q.color + ': ' + q.name : q.color;
            var square = doc.createElement('i');
            var nm = doc.createElement('span');
            nm.className = 'nm';
            nm.textContent = q.name || q.color;
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
    // are the game's even where the bar lies (the edge scrolls with the bar up), and a bar that has gone away (idle, or hidden by the viewer: gone) is not there to be hit. The page gives `env`: doc, win, stage (the arrow's
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
            if (!bar || !bar.classList.contains('over') || bar.classList.contains('idle') || bar.classList.contains('gone')) return null;
            var el = env.doc.elementFromPoint(cx, cy);
            return el && bar.contains(el) ? el : null;
        }
        // the handle of a bar that is hidden or only peeks (a button of its own, beside the bar): a locked mouse presses it too
        function handleUnder(cx, cy) {
            if (!env.replayMode()) return null;
            var bar = env.doc.getElementById('rbar'), h = env.doc.getElementById('b-handle');
            if (!bar || !h || h.hidden || !bar.classList.contains('over')) return null;
            var el = env.doc.elementFromPoint(cx, cy);
            return el && (el === h || h.contains(el)) ? el : null;
        }
        function reach(cx, cy) { return under(cx, cy) || handleUnder(cx, cy); }
        function interactive(el) { return el && el.closest ? el.closest('button, a[href], summary, input, select') : null; }
        function fire(target, type, at, buttons) {
            target.dispatchEvent(new env.PointerEvent(type, { bubbles: true, cancelable: true, view: env.win, clientX: at.x, clientY: at.y, pointerId: 1000, pointerType: 'mouse', isPrimary: true, button: type === 'pointermove' ? -1 : 0, buttons: buttons }));
        }
        function hit(at) { return at.edge ? null : reach(at.x, at.y); }
        var api = {
            mode: function () { return inBar; },
            under: reach,
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

    // ---- a replay file of the visitor's own (docs/REPLAYS.md "A file of your own") ----
    // The page reads the file on this device and sends it nowhere. Before the game is loaded for it, the page asks the cheap questions that the game's own reader asks first (src/ants_replay/replay.cpp, decode):
    // how big it is, the 8 bytes that open every replay, a HEAD chunk right after them that is not longer than a head can be and whose checksum is right. A file that fails them is no replay of ours and
    // costs nothing (the game, 15 MB, is not even asked for). The rest (the rules that made it, the map, every other chunk) is the game's to judge: it reads the whole file again with its strict reader and
    // says what it finds in a card.
    var LOCAL_MAX_BYTES = 2097164;                                       // replay::kMaxFileBytesWithMap: 1 MiB for the match, 1 MiB for the map that it may carry, 12 bytes for the frame of the map's chunk
    var HEAD_MAX_BYTES = 4096;                                           // replay::kMaxHeadBytes
    var MAGIC = [0x89, 0x41, 0x52, 0x50, 0x4c, 0x0d, 0x0a, 0x1a];        // 89 "ARPL" 0D 0A 1A: a transfer that changes line ends spoils it, and a file of another kind never begins so
    var HEAD_AT = 8;                                                     // the HEAD chunk follows the magic at once
    var crcTable = null;

    // CRC-32 (the one of zlib and PNG) of bytes[from, to), going on from `crc` (0 to start): the same function as replay::crc32, so crc32(crc32(tag), payload) is a chunk's checksum
    function crc32(bytes, from, to, crc) {
        if (!crcTable) {
            crcTable = [];
            for (var n = 0; n < 256; n++) {
                var c = n;
                for (var k = 0; k < 8; k++) c = c & 1 ? (0xedb88320 ^ (c >>> 1)) >>> 0 : c >>> 1;
                crcTable.push(c);
            }
        }
        var x = ~(crc || 0) >>> 0;
        for (var i = from; i < to; i++) x = (crcTable[(x ^ bytes[i]) & 0xff] ^ (x >>> 8)) >>> 0;
        return ~x >>> 0;
    }
    function u32(bytes, at) { return (bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (bytes[at + 3] << 24)) >>> 0; }

    // May a file of this size (a File's own `size`: asked before one byte of it is read) be a replay? 0 is no replay, and nothing past the longest file there can be is one
    function localSizeOk(size) { return typeof size === 'number' && isFinite(size) && size > 0 && size <= LOCAL_MAX_BYTES; }
    // The verdict on the bytes of a file: { ok: true }, or { ok: false, kind: 'big' } (longer than a replay can be: asked again here, whatever the caller did) or { ok: false, kind: 'bad', why } (it does not
    // start as a replay does, or its head is damaged, or it was cut inside its head; `why` is for a test and for nobody else: the card says one thing for all of them). A file that was cut LATER still plays up
    // to the cut, as in the game, and passes. Nothing in the head's fields is read here.
    function localCheck(bytes) {
        function bad(why) { return { ok: false, kind: 'bad', why: why }; }
        if (!bytes || typeof bytes.length !== 'number') return bad('nothing');
        if (bytes.length > LOCAL_MAX_BYTES) return { ok: false, kind: 'big' };
        if (bytes.length < HEAD_AT + 12) return bad('short');
        for (var i = 0; i < MAGIC.length; i++) if (bytes[i] !== MAGIC[i]) return bad('magic');
        if (bytes[HEAD_AT] !== 0x48 || bytes[HEAD_AT + 1] !== 0x45 || bytes[HEAD_AT + 2] !== 0x41 || bytes[HEAD_AT + 3] !== 0x44) return bad('first chunk');          // "HEAD"
        var length = u32(bytes, HEAD_AT + 4), end = HEAD_AT + 8 + length;
        if (length > HEAD_MAX_BYTES) return bad('head length');
        if (end + 4 > bytes.length) return bad('head cut');
        if (crc32(bytes, HEAD_AT + 8, end, crc32(bytes, HEAD_AT, HEAD_AT + 4, 0)) !== u32(bytes, end)) return bad('head checksum');          // (the checksum covers the tag and the payload, not the length between them)
        return { ok: true };
    }

    // The name of a file as the page shows it: text, with what could hide or reorder the words around it taken out (controls, the zero-width and the bidirectional marks, the line and paragraph
    // separators, the byte order mark), the ends trimmed and at most 100 characters; '' when nothing is left. It is only ever put in with textContent.
    function ownName(name) {
        var s = '';
        for (var i = 0; typeof name === 'string' && i < name.length; i++) {
            var c = name.charCodeAt(i);
            if (c < 0x20 || (c >= 0x7f && c <= 0x9f) || (c >= 0x200b && c <= 0x200f) || (c >= 0x2028 && c <= 0x202e) || (c >= 0x2060 && c <= 0x206f) || c === 0xfeff) continue;
            s += name.charAt(i);
        }
        s = s.replace(/^\s+|\s+$/g, '');
        return s.length > 100 ? s.slice(0, 99) + String.fromCharCode(0x2026) : s;
    }
    // "14.2 MB" (binary megabytes, as the limit "2 MB" is)
    function sizeText(bytes) {
        var n = typeof bytes === 'number' && isFinite(bytes) && bytes > 0 ? bytes : 0;
        if (n >= 1073741824) return (n / 1073741824).toFixed(1) + ' GB';
        if (n >= 1048576) return (n / 1048576).toFixed(1) + ' MB';
        return Math.max(1, Math.round(n / 1024)) + ' KB';
    }

    // ---- the hand-off from the page that took the file to the player ----
    // The Matches page cannot give a file to another page by an address (2 MB), and sends it nowhere, so it leaves it in the browser's own storage (IndexedDB) under a new random id and goes to
    // play.html?local=<id>; the player reads it from there. ONE record at most: a second file takes the place of the first. The record stays after it was read (a reload of the player must still
    // work) and is thrown away when it is older than a day (every page that opens it looks first: purge). A record that is not there (an old link, another tab took the place, the browser's data was
    // cleared) is a card with a way to choose the file again, never a game that starts on something else. Every call can fail (a browser that keeps no site data has no IndexedDB, or refuses it,
    // or the file does not fit): a failed call rejects and the page says so.
    var OWN_DB = 'ants-own-replay', OWN_STORE = 'slot', OWN_KEY = 'file';
    var OWN_ID_RE = /^[0-9a-f]{32}$/;
    var OWN_KEEP_MS = 24 * 60 * 60 * 1000;                               // a record is kept about a day
    var OWN_SKEW_MS = 60 * 1000;                                         // ... and a clock that was put back by up to a minute since it was written does not lose it

    // The record that an address asks for (?local=<id>): the id when it has the shape that the page makes (32 hex digits), else null
    function ownIdOf(search) {
        var q;
        try { q = new URLSearchParams(search); } catch (e) { return null; }
        var id = q.get('local');
        return typeof id === 'string' && OWN_ID_RE.test(id) ? id : null;
    }
    function ownFresh(rec, now) {
        var age = now - rec.at;
        return typeof rec.at === 'number' && age >= -OWN_SKEW_MS && age < OWN_KEEP_MS;
    }
    function ownRecord(rec) {
        return !!rec && typeof rec === 'object' && typeof rec.id === 'string' && OWN_ID_RE.test(rec.id) && typeof rec.name === 'string' && !!rec.bytes && typeof rec.bytes.length === 'number';
    }
    // A new id: 32 hex digits from the browser's random source (an id is a name that makes an old link find nothing, not a secret: only pages of this site can read the storage)
    function ownRandomId(win) {
        var bytes = new Uint8Array(16), hex = '', crypto = win && (win.crypto || win.msCrypto);
        if (crypto && typeof crypto.getRandomValues === 'function') crypto.getRandomValues(bytes);
        else for (var i = 0; i < 16; i++) bytes[i] = Math.floor(Math.random() * 256);
        for (var j = 0; j < 16; j++) hex += (bytes[j] < 16 ? '0' : '') + bytes[j].toString(16);
        return hex;
    }
    // `env`: indexedDB (or nothing), now() (ms since 1970) and random() (a new id). Returns { put(name, bytes) -> id, read(id) -> the record or null, purge() -> true / false } (promises).
    function ownStore(env) {
        var opened = null;
        function open() {
            if (opened) return opened;
            opened = new Promise(function (resolve, reject) {
                var request;
                try { request = env.indexedDB.open(OWN_DB, 1); } catch (e) { reject(e); return; }
                request.onupgradeneeded = function () { try { request.result.createObjectStore(OWN_STORE); } catch (e) { /* made already */ } };
                request.onsuccess = function () { resolve(request.result); };
                request.onerror = function () { reject(request.error || new Error('the storage did not open')); };
                request.onblocked = function () { reject(new Error('the storage is blocked')); };
            });
            opened.then(null, function () { opened = null; });                       // (a failed opening is tried again by the next call)
            return opened;
        }
        // one transaction: `work(store)` makes the request that is wanted; the answer is its result once the transaction has completed
        function run(mode, work) {
            return open().then(function (db) {
                return new Promise(function (resolve, reject) {
                    var tx, request;
                    try { tx = db.transaction(OWN_STORE, mode); request = work(tx.objectStore(OWN_STORE)); } catch (e) { reject(e); return; }
                    tx.oncomplete = function () { resolve(request ? request.result : undefined); };
                    tx.onerror = tx.onabort = function () { reject(tx.error || new Error('the storage refused')); };
                });
            });
        }
        return {
            put: function (name, bytes) {
                var rec = { id: env.random(), name: name, at: env.now(), bytes: bytes };
                return run('readwrite', function (store) { return store.put(rec, OWN_KEY); }).then(function () { return rec.id; });
            },
            read: function (id) {
                if (typeof id !== 'string' || !OWN_ID_RE.test(id)) return Promise.resolve(null);
                return run('readonly', function (store) { return store.get(OWN_KEY); }).then(function (rec) { return ownRecord(rec) && rec.id === id && ownFresh(rec, env.now()) ? rec : null; });
            },
            purge: function () {
                return run('readwrite', function (store) {
                    var got = store.get(OWN_KEY);
                    got.onsuccess = function () { var rec = got.result; if (rec !== undefined && !(ownRecord(rec) && ownFresh(rec, env.now()))) store.delete(OWN_KEY); };
                    return got;
                }).then(function () { return true; }, function () { return false; });
            }
        };
    }
    // The store of a page (its IndexedDB, its clock, its random source). A browser that blocks site data can throw at the mere mention of window.indexedDB: the store is then one that fails every call, and the
    // page says so when it is used.
    function ownStoreOf(win) {
        var idb = null;
        try { idb = win.indexedDB; } catch (e) { idb = null; }
        return ownStore({ indexedDB: idb, now: function () { return Date.now(); }, random: function () { return ownRandomId(win); } });
    }

    // The bytes of a File (or Blob): its own arrayBuffer() where there is one, else a FileReader
    function ownBytes(env, file) {
        return new Promise(function (resolve, reject) {
            if (typeof file.arrayBuffer === 'function') { file.arrayBuffer().then(function (buffer) { resolve(new Uint8Array(buffer)); }, reject); return; }
            var reader = new env.FileReader();
            reader.onload = function () { resolve(new Uint8Array(reader.result)); };
            reader.onerror = function () { reject(reader.error || new Error('the file could not be read')); };
            reader.readAsArrayBuffer(file);
        });
    }
    // A file that the visitor chose or dropped, taken over: the size is asked from the File before a byte is read, the bytes are checked (localCheck), and only a file that passes is kept for the player.
    // env: store (ownStore) and FileReader (when a File has no arrayBuffer()). Answers { ok: true, id, name, size } or { ok: false, kind, name, size } with kind 'big' (more than a replay can be),
    // 'bad' (no replay of ours, empty or unreadable) or 'nostore' (the browser would not keep it for the player); `name` is the file's name as text (ownName). A file that fails leaves the record that
    // was there as it was.
    function ownOpen(env, file) {
        var name = ownName(file && file.name), size = file && typeof file.size === 'number' ? file.size : 0;
        function no(kind) { return { ok: false, kind: kind, name: name, size: size }; }
        if (!file) return Promise.resolve(no('bad'));
        if (!localSizeOk(size)) return Promise.resolve(no(size > LOCAL_MAX_BYTES ? 'big' : 'bad'));
        return ownBytes(env, file).then(function (bytes) {
            var verdict = localCheck(bytes);
            if (!verdict.ok) return no(verdict.kind);
            return env.store.put(name, bytes).then(function (id) { return { ok: true, id: id, name: name, size: size }; }, function () { return no('nostore'); });
        }, function () { return no('bad'); });
    }

    // ---- the strip "Got a replay file?" of the Matches page (web/matches.html) ----
    // A button that opens the browser's file chooser, a drop of a file anywhere on the page (a computer; a veil shows while a file is dragged over the page, and the strip lights up), and under the strip a
    // card for a file that is refused. A file that passes goes to the player at once, in this tab. Nothing is sent: the file goes from the chooser or the drop into the browser's storage (ownOpen) and
    // to the player. The words of the cards are the ones of the approved pictures.
    var VEIL_MS = 900;                                                   // a drag that has said nothing for this long (a drag that was cancelled can leave without a word) takes the veil down
    // The title and the sentence of the card for a refused file `r` ({ kind, size } as ownOpen answers)
    function refusalWords(r) {
        if (r && r.kind === 'big') return { title: 'That file is too big', text: 'A replay file is at most ' + Math.floor(LOCAL_MAX_BYTES / 1048576) + ' MB. This one is ' + sizeText(r.size) + ', so it is not a replay of a match.' };
        if (r && r.kind === 'nostore') return { title: 'This browser will not keep the file for the player', text: 'The page hands the file to the player through the storage that the browser keeps for this site, and this browser does not let the site use it (private or blocked site data). Allow site data for this site and choose the file again.' };
        return { title: 'That file is not a readable replay', text: 'It does not start like an Ants replay, or it is damaged (a download that stopped halfway, or a file that was changed).' };
    }
    function hasFiles(ev) {
        var types = ev && ev.dataTransfer && ev.dataTransfer.types;
        for (var i = 0; types && i < types.length; i++) if (types[i] === 'Files') return true;
        return false;
    }
    // env: doc, win, store (ownStore), FileReader, go(url) (the page goes there), setTimeout / clearTimeout (the window's). The page's pieces (by id): opener (the strip), open-btn, open-file (the
    // chooser, an <input type="file"> with no `accept`: a phone's chooser greys out a file of an ending that it does not know), dropveil, open-note.
    function opener(env) {
        var doc = env.doc, win = env.win;
        function el(id) { return doc.getElementById(id); }
        var strip = el('opener'), button = el('open-btn'), input = el('open-file'), veil = el('dropveil'), note = el('open-note');
        var busy = false, depth = 0, timer = null;
        function make(tag, className, text) {
            var node = doc.createElement(tag);
            if (className) node.className = className;
            if (text !== undefined) node.textContent = text;
            return node;
        }
        function hideNote() { note.textContent = ''; note.hidden = true; }
        function choose() { hideNote(); input.click(); }
        function showNote(r) {
            var words = refusalWords(r), card = make('div', 'card bad'), row = make('div', 'row'), again = make('button', 'btn sm'), icon = make('span', 'ic');
            card.setAttribute('role', 'alert');
            card.appendChild(make('h3', '', words.title));
            card.appendChild(make('p', '', words.text));
            if (r && r.name) card.appendChild(make('p', 'fn', r.name));
            icon.setAttribute('aria-hidden', 'true');
            again.type = 'button'; again.appendChild(icon); again.appendChild(doc.createTextNode('Choose another file'));
            again.addEventListener('click', choose);
            row.appendChild(again); row.appendChild(make('span', 'lab', 'Nothing was sent anywhere'));
            card.appendChild(row);
            note.textContent = '';
            note.appendChild(card);
            note.hidden = false;
        }
        function veilOn(on) {
            if (!on) { depth = 0; if (timer !== null) { env.clearTimeout.call(undefined, timer); timer = null; } }
            veil.hidden = !on;
            strip.classList.toggle('drag', !!on);
        }
        // a file that was chosen or dropped: the first one only; a second one while the first is on its way is ignored
        function take(file) {
            if (busy || !file) return;
            busy = true;
            hideNote();
            ownOpen(env, file).then(function (r) {
                if (r.ok) { env.go('/play.html?local=' + r.id); return; }                  // (the page is leaving: busy stays until it is shown again from the browser's memory, pageshow)
                busy = false;
                showNote(r);
            });
        }
        return {
            take: take,
            wire: function () {
                if (typeof win.File !== 'function' || typeof Promise !== 'function') return false;                  // (a browser that cannot read a file has no strip)
                strip.hidden = false;
                env.store.purge();
                button.addEventListener('click', choose);
                input.addEventListener('change', function () { var file = input.files && input.files[0]; input.value = ''; take(file); });
                // a drag of a FILE over the page: the drop is the page's (preventDefault: else the browser leaves for the file), a drag of anything else is none of its business
                doc.addEventListener('dragenter', function (ev) { if (!hasFiles(ev)) return; ev.preventDefault(); depth++; veilOn(true); });
                doc.addEventListener('dragover', function (ev) {
                    if (!hasFiles(ev)) return;
                    ev.preventDefault();
                    try { ev.dataTransfer.dropEffect = 'copy'; } catch (e) { /* a drag that cannot say */ }
                    veilOn(true);
                    if (timer !== null) env.clearTimeout.call(undefined, timer);
                    timer = env.setTimeout.call(undefined, function () { timer = null; veilOn(false); }, VEIL_MS);
                });
                doc.addEventListener('dragleave', function (ev) { if (!hasFiles(ev)) return; depth = Math.max(0, depth - 1); if (!depth) veilOn(false); });
                doc.addEventListener('drop', function (ev) {
                    if (!hasFiles(ev)) return;
                    ev.preventDefault();
                    veilOn(false);
                    take(ev.dataTransfer.files && ev.dataTransfer.files[0]);
                });
                win.addEventListener('pageshow', function (ev) { if (ev && ev.persisted) { busy = false; veilOn(false); } });
                return true;
            }
        };
    }

    // ---- the player's own mode (web/shell.html at play.html?local=<id>) ----
    // The page of a file of the visitor's own is the replay page with these differences: the file is read from the browser's storage (where the Matches page left it) and not fetched; the header says "Your
    // file" and its name, and has no Copy link, Stats or Download (the link and the stats are those of the server's own recordings, and the file is on this device already); every card that could offer the
    // file for download offers to open another one; and a card for a record that is not there (choose the file again). env: doc, win, store (ownStore), FileReader, search (the address's query), say(text) (a
    // note on the page), go(url) (the page goes there), infoOf(bytes) (what the head says: { map, roster, names } or null: web/map_files.js infoOfReplay). Everything that comes from the file (its name, its map,
    // the players) is put in as text.
    function mk(doc, tag, className, text) {
        var node = doc.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined) node.textContent = text;
        return node;
    }
    function ownPage(env) {
        var doc = env.doc, id = ownIdOf(env.search), name = '', head = null, input = null, busy = false;
        // Enter and Space press a button here ourselves: the game's key handling (SDL) takes the browser's own press of a focused button away on this page (as for the camera button)
        function press(node, fn) {
            node.type = 'button';
            node.addEventListener('click', fn);
            node.addEventListener('keydown', function (ev) {
                if (ev.key !== 'Enter' && ev.key !== ' ') return;
                ev.preventDefault();
                if (!ev.repeat) node.click();
            });
            node.addEventListener('keyup', function (ev) { if (ev.key === ' ') ev.preventDefault(); });
            return node;
        }
        // the chooser (made when first wanted), then the same way as the Matches page: the file is checked and kept, and the page goes to it; a file that is refused is a note
        function pick() {
            if (!input) {
                input = mk(doc, 'input');
                input.type = 'file';
                input.tabIndex = -1;
                input.setAttribute('aria-hidden', 'true');
                input.style.cssText = 'position:fixed;left:-9999px;top:0;width:1px;height:1px;opacity:0';
                input.addEventListener('change', function () {
                    var file = input.files && input.files[0];
                    input.value = '';
                    if (!file || busy) return;
                    busy = true;
                    ownOpen(env, file).then(function (r) {
                        if (r.ok) { env.go('/play.html?local=' + r.id); return; }
                        busy = false;
                        env.say(refusalWords(r).title + '.');
                    });
                });
                doc.body.appendChild(input);
            }
            input.click();
        }
        return {
            id: id,
            pick: pick,
            // The file: { bytes, name } from the record of the address, or { error: 'again' } when there is none (no id, another id, too old, taken by another file) or { error: 'load', text } when the browser
            // would not give it (the page then has a card with the reason). Old records are thrown away first; the bytes are checked again, whoever put them there.
            read: function () {
                function fail(text) { return { error: 'load', text: text }; }
                if (!id) return Promise.resolve({ error: 'again' });
                return env.store.purge().then(function () { return env.store.read(id); }).then(function (rec) {
                    if (!rec) return { error: 'again' };
                    var bytes = rec.bytes instanceof Uint8Array ? rec.bytes : Uint8Array.from(rec.bytes);
                    if (!localCheck(bytes).ok) return fail('the file that this page kept is damaged');
                    name = ownName(rec.name);
                    head = env.infoOf ? env.infoOf(bytes) : null;
                    return { bytes: bytes, name: name };
                }, function () { return fail('this browser does not let the page read the file back (site data may be blocked)'); });
            },
            map: function () { return head && head.map ? head.map : ''; },
            // Whether the game's reason for failure 4 is that the site has no such map: the folder's own words (player.cpp load_map: "the map X is not in ..."), which the game says only for a file that carries no map
            // (load_map_or_embedded gives a carried map's own reasons). Anything else (a map that the game refuses: "the rules word is 2", "team green: 3 start marker(s) lie outside the 1 x 1 grid") is the game's reason, said as it is.
            lacks: function (why) { return typeof why === 'string' && /^the map .+ is not in /.test(why); },
            // the header of the page: the map, "Your file" and the file's name and length, the players (what the game reported, else what the file's head says: a file that the game never reads has them too)
            header: function (info, seconds, cut) {
                var title = doc.getElementById('r-title'), meta = doc.getElementById('r-meta'), small = mk(doc, 'small'), from = info && info.map ? info : head;
                title.textContent = '';
                meta.textContent = '';
                title.appendChild(doc.createTextNode(mapTitle(from && from.map) || 'A match to watch'));
                if (name || head) {                                                   // (no file, no "Your file": the card that asks for the file again has none to name)
                    small.appendChild(mk(doc, 'span', 'pill own', 'Your file'));
                    small.appendChild(doc.createTextNode(' '));
                    if (name) small.appendChild(mk(doc, 'bdi', '', name));                  // (a unit: a name that reads right to left cannot take the length that follows it into its own order)
                    if (seconds) small.appendChild(doc.createTextNode((name ? ' · ' : '') + lengthText(seconds)));
                    if (cut) { small.appendChild(doc.createTextNode(' ')); small.appendChild(mk(doc, 'span', 'pill cut', 'Cut short')); }
                    title.appendChild(small);
                }
                if (from && typeof from.roster === 'number') meta.appendChild(chipsOf(doc, playersOf(from.roster, from.names)));
            },
            // the header's buttons: the way back is "Matches" here, and a narrow window's More has a way to open another file
            chrome: function () {
                var back = doc.getElementById('all-btn'), list = doc.querySelector('.header-actions.rep-only .more-list');
                if (back) back.textContent = '‹ Matches';
                if (list) { var more = press(mk(doc, 'button', 'btn sm', 'Open another file'), pick); more.id = 'own-more'; list.appendChild(more); }
            },
            // The card of a kind that a file of your own has its own words for ('older', 'newer', 'nomap': the site has no such map, 'changed': it has another file of that name, 'again': no record, 'cant':
            // the rest), or null. ctx: made (what made the file, "v0.11.0"), version (this page's), map (the name of the map), why (a reason, as text). { title, body (nodes), buttons (nodes) }
            card: function (kind, ctx) {
                var body = [], buttons = [], title = '', page = ctx.version || 'this version', made = ctx.made || 'a version of the game';
                function para(parts, className) {
                    var node = mk(doc, 'p', className);
                    parts.forEach(function (part) { node.appendChild(typeof part === 'string' ? doc.createTextNode(part) : mk(doc, 'b', '', part.b)); });
                    body.push(node);
                }
                function open(label) { buttons.push(press(mk(doc, 'button', 'btn', label), pick)); }
                function all() { var a = mk(doc, 'a', 'btn', '‹ All matches'); a.href = '/matches.html'; buttons.push(a); }
                var onMap = ctx.map || ctx.map === 0 ? ['This match was played on the map ', { b: ctx.map }, ', and a replay needs the same map file. '] : ['This match was played on a map, and a replay needs the same map file. '];
                if (kind === 'older') {
                    title = 'Recorded by an older version';
                    para(['This match was played on ', { b: made }, '. The rules of the game have changed since, and a replay only plays on a version with the same rules, so this version (', { b: page }, ') can’t show it.']);
                    open('Open another file'); all();
                } else if (kind === 'newer') {
                    title = 'Recorded by a newer version';
                    para(['This match was played on ', { b: made }, ', and this page is ', { b: page }, '. Load the page again to get the newer version.']);
                    var again = mk(doc, 'a', 'btn', 'Load the page again');
                    again.href = '#';
                    again.addEventListener('click', function (ev) { ev.preventDefault(); env.win.location.reload(); });
                    open('Open another file'); buttons.push(again); all();
                } else if (kind === 'nomap' || kind === 'changed') {
                    title = 'This site doesn’t have the map';
                    para(onMap.concat(kind === 'changed' ? 'The site has a map of that name, but it is not the same file (the map was changed after this match was played).'
                                                         : 'The site no longer has it (a map that was taken off, or one that was only on the recorder’s own computer).'));
                    open('Open another file'); all();
                } else if (kind === 'again') {
                    title = 'Choose the replay file again';
                    para(['The file you opened is no longer here: this page keeps it for a day, and only one file at a time. Choose it again. It stays on this device: nothing is uploaded.']);
                    open('Open a replay file'); all();
                } else if (kind === 'cant') {
                    title = 'This replay can’t be played here';
                    para(['The match could not be set up on this page.']);
                    if (typeof ctx.why === 'string' && ctx.why) para([ctx.why.charAt(0).toUpperCase() + ctx.why.slice(1) + (/[.!?]$/.test(ctx.why) ? '' : '.')], 'rm-why');
                    open('Open another file'); all();
                } else return null;
                return { title: title, body: body, buttons: buttons };
            }
        };
    }
    function ownPageOf(win, extra) {
        return ownPage({ doc: win.document, win: win, store: ownStoreOf(win), FileReader: win.FileReader, search: win.location.search, say: extra.say, go: function (url) { win.location.replace(url); }, infoOf: extra.infoOf });
    }

    // THE BAR'S HIDING (docs/REPLAYS.md, "Watching a replay": fullscreen). The bar that lies over the picture has three states. `open` is the one the page starts in (the bar is up and, while the match
    // plays, goes away by itself after a rest of the pointer, as ever). `hidden` is the viewer's choice (the chevron on the bar, or the B key): nothing brings the bar back by chance, neither a move
    // of the mouse, a click on the map or the minimap, nor a key; a thin handle stays at the bottom edge. `peek` is the bar called by the mouse: the pointer RESTS for ZONE_DWELL_MS (it moves less than ZONE_REST_PX) in the band of ZONE_PX along the picture's
    // bottom, but not in its last ZONE_GAP pixels (the game's edge scroll rests there) (a drag with a button down, or a pass that is shorter than that, calls nothing), and it goes back to `hidden` when the pointer has been away from the band and the bar for ZONE_LEAVE_MS (a button
    // down, or the page's `busy()`, is a touch of the bar). A finger never calls it by hover: the page feeds this only with the moves of a mouse or a pen. The page gives `env`: setTimeout, clearTimeout,
    // zone() (the picture's rectangle: left, right, bottom, height, and rows, the picture's height in game pixels), bar() (the bar's rectangle, or null while it is not there), busy() and change(mode). tests/scripts/web_replay_check.js runs it.
    var ZONE_PX = 48, ZONE_GAP = 12, ZONE_REST_PX = 4, ZONE_DWELL_MS = 150, ZONE_LEAVE_MS = 700;      // (ZONE_GAP: the least gap; the last pixels of the picture are the game's edge scroll, which rests there: they never call the bar)
    function gapOf(z) { return z.rows > 0 && z.height > 0 ? Math.max(ZONE_GAP, Math.ceil(5 * z.height / z.rows) + 2) : ZONE_GAP; }       // (the game's 5 px scroll strip, as it lies on the screen at this scale, plus 2 px; never less than ZONE_GAP)
    function barZone(env) {
        var mode = 'open', dwell = 0, leave = 0, at = null, anchor = null;                  // at: where the pointer last was, { x, y, buttons }, or null when it is out of the page
        function set(next) {
            env.clearTimeout(dwell); env.clearTimeout(leave); dwell = 0; leave = 0;
            if (next === mode) return;
            mode = next;
            env.change(mode);
        }
        function inZone() {
            var z = env.zone();
            return !!(at && z && at.x >= z.left && at.x <= z.right && at.y >= z.bottom - ZONE_PX && at.y < z.bottom - gapOf(z));
        }
        function inBar() {
            var b = env.bar();
            return !!(at && b && at.x >= b.left && at.x <= b.right && at.y >= b.top && at.y <= b.bottom);
        }
        function away() { return !at || !(inZone() || inBar() || at.buttons || env.busy()); }
        function look() {
            if (mode === 'hidden') {
                if (at && !at.buttons && inZone()) {
                    if (dwell && anchor && (Math.abs(at.x - anchor.x) > ZONE_REST_PX || Math.abs(at.y - anchor.y) > ZONE_REST_PX)) { env.clearTimeout(dwell); dwell = 0; }       // (a rest: a pointer that goes on moving starts the delay again)
                    if (!dwell) { anchor = { x: at.x, y: at.y }; dwell = env.setTimeout(function () { dwell = 0; if (mode === 'hidden' && at && !at.buttons && inZone()) set('peek'); }, ZONE_DWELL_MS); }
                }
                else { env.clearTimeout(dwell); dwell = 0; }
            } else if (mode === 'peek') {
                if (inZone() || inBar()) { env.clearTimeout(leave); leave = 0; }
                else if (!leave) arm();
            }
        }
        function arm() {                                                     // (the pointer is away: in 700 ms the bar goes, unless a button or a busy bar holds it, which is looked at again)
            leave = env.setTimeout(function () { leave = 0; if (mode !== 'peek') return; if (away()) set('hidden'); else if (!inZone() && !inBar()) arm(); }, ZONE_LEAVE_MS);
        }
        return {
            mode: function () { return mode; },
            hide: function () { set('hidden'); },                                // the viewer's choice
            open: function () { set('open'); },                                  // the handle, the key, a tap
            settle: function () { if (mode === 'peek') set('hidden'); },         // (fullscreen ended: a bar that was only called goes back)
            pointer: function (x, y, buttons) { at = { x: x, y: y, buttons: buttons || 0 }; look(); },
            gone: function () { at = null; look(); }                             // the pointer left the page
        };
    }

    var api = { barPointer: barPointer, barZone: barZone, ZONE_PX: ZONE_PX, ZONE_GAP: ZONE_GAP, gapOf: gapOf, ZONE_REST_PX: ZONE_REST_PX, ZONE_DWELL_MS: ZONE_DWELL_MS, ZONE_LEAVE_MS: ZONE_LEAVE_MS, REWIND_SECONDS: REWIND_SECONDS, rewindTurn: rewindTurn, rewindControl: rewindControl, CAM_KEY: CAM_KEY, CAM_NOTE_MS: CAM_NOTE_MS, camLoad: camLoad, camSave: camSave, camLabel: camLabel, camTitle: camTitle, camNoteDue: camNoteDue, cameraControl: cameraControl, FILE_RE: FILE_RE, LIVE_RE: LIVE_RE, SEATS: SEATS, LIVE_LAG: LIVE_LAG, AT_LIVE: AT_LIVE, FOLLOW_SLACK: FOLLOW_SLACK, fileOf: fileOf, liveIdOf: liveIdOf, liveMap: liveMap, startedOf: startedOf, endedOf: endedOf, whenText: whenText, startedText: startedText,
                lengthText: lengthText, mapKey: mapKey, mapTitle: mapTitle, speedText: speedText, liveVerdict: liveVerdict, atLive: atLive, liveTag: liveTag, liveEntriesOf: liveEntriesOf, parsePlayer: parsePlayer, playersOf: playersOf, chipsOf: chipsOf,
                LOCAL_MAX_BYTES: LOCAL_MAX_BYTES, HEAD_MAX_BYTES: HEAD_MAX_BYTES, crc32: crc32, localSizeOk: localSizeOk, localCheck: localCheck, ownName: ownName, sizeText: sizeText,
                OWN_KEEP_MS: OWN_KEEP_MS, ownIdOf: ownIdOf, ownRandomId: ownRandomId, ownStore: ownStore, ownStoreOf: ownStoreOf, ownOpen: ownOpen,
                refusalWords: refusalWords, opener: opener, ownPage: ownPage, ownPageOf: ownPageOf };
    if (typeof module === 'object' && module && module.exports) module.exports = api;
    else root.AntsReplay = api;
})(typeof window !== 'undefined' ? window : this);
