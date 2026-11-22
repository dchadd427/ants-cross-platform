// The commands of the chat dock of Home (docs/HOME_CHAT.md, "Roles and commands"): the helper (a `/` as the first character opens the account's own commands over the field), the /bots builder (a box over the page, a sheet on a phone,
// that only WRITES the command into the field: Enter sends it) and the private answers to the hub's `reply` frames (the hub sends codes and numbers, this page says the words). Loaded through the dock's env.load, with cmds.css, only
// when a `cmds` frame has a row (a guest or a reader downloads nothing). Everything from the hub or the game server (a description, a map name, a person's name) is DOM text only (createElement, textContent), never markup. This file
// sends nothing: a command goes through the dock as typed. The builder asks the admin page's API (same origin, GET only) for the maps and for the number that run at once, and reaches only an account whose rows hold /bots.
// ES5, no module system: a page gets AntsHomeCmds, node gets module.exports (tests/scripts/web_home_cmds_check.js).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeCmds = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var LIMIT = { rows: 24, args: 160, desc: 60, group: 24, count: 10000, every: 120, map: 96, close: 3, name: 32, queues: 5, wait: 3600, frame: 1e9 };
    var KEEP = 30;       // replays of bot matches kept an hour (docs/SERVER_REPLAYS.md)
    var POP = 'gchat-cmds', PICK = '/admin_map_pick.js', PICK_CSS = '/admin.css', OPTIONS = '/admin/api/bot-options', SUMMARY = '/admin/api/bot-matches/summary';
    var TEXT = {
        group: 'Commands', listLabel: 'Commands', top: 'Commands you can use', topNarrow: 'Commands', inAll: ' in all', match: ' match', matches: ' matches', none: 'No command of yours starts with ', noneEnd: '. Type /help to see them.',
        choose: 'Choose with ', then: ', then ', esc: 'Esc closes', unknown: 'That command is not one that you have. Type /help to see the commands.', generic: 'That did not work, try again',
        role: 'Could not confirm your role, try again in a moment', title: 'Start bot matches', close: 'Close', cancel: 'Cancel', write: 'Write the command', howMany: 'How many', every: 'One every', unit: 'minutes (0: as fast as there is room)',
        up: 'Up to 10,000. What the server cannot run at once waits in a queue.', map: 'Map', rotation: 'The rotation', rules: 'Rules', bots: 'Bots', level: 'Level', teams: 'Teams', loading: 'Loading the maps\u2026',
        badCount: 'Matches: use a number from 1 to 10,000.', badEvery: 'One every: 0 to 120 minutes, in whole or half minutes.', badMap: 'That map\u2019s name cannot be typed into the chat (it is too long, or holds a < or >). Choose another map.',
        noMaps: 'The list of maps did not come. The batch uses the rotation.', mapTitle: 'Choose a map'
    };
    var RULES = [['random', 'A coin'], ['highest', 'Highest score'], ['187', '187']], BOTS = [['random', '2 to 4'], ['2', '2'], ['3', '3'], ['4', '4']];
    var LEVELS = [['mixed', 'Medium or Hard'], ['easy', 'Easy'], ['medium', 'Medium'], ['hard', 'Hard'], ['any', 'Any']], PRESETS = [1, 10, 100, 1000];
    var SPLITS = { 4: [['12-34', 'Bots 1 and 2 against 3 and 4'], ['13-24', 'Bots 1 and 3 against 2 and 4'], ['14-23', 'Bots 1 and 4 against 2 and 3']], 3: [['1-23', 'Bot 1 against 2 and 3'], ['2-13', 'Bot 2 against 1 and 3'], ['3-12', 'Bot 3 against 1 and 2']] };
    var NUMBERS = ['no', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten'];

    // ---- the pure parts -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    function clean(s, max) {        // text from outside: controls and invisible characters out, one space for blanks, cut to `max`
        s = String(s).replace(/[\u0000-\u001f\u007f-\u009f\u00ad\u061c\u200b-\u200f\u202a-\u202e\u2060-\u206f\ufeff]/g, ' ').replace(/\s+/g, ' ').replace(/^ | $/g, '');
        if (s.length > max) { s = s.slice(0, max); var c = s.charCodeAt(s.length - 1); if (c >= 0xd800 && c <= 0xdbff) s = s.slice(0, -1); s = s.replace(/ $/, '') + '\u2026'; }
        return s;
    }
    function int(v) { return typeof v === 'number' && v >= 0 && v <= LIMIT.frame && v === Math.floor(v) ? v : 0; }
    function n1(n) { return n.toLocaleString('en-US'); }
    function matchesOf(n) { return n1(n) + (n === 1 ? ' match' : ' matches'); }
    function peopleOf(n) { return n + (n === 1 ? ' person' : ' people'); }
    function wordOf(n) { return n >= 1 && n <= 10 ? NUMBERS[n] : n1(n); }
    function spanOf(sec) {
        sec = Math.max(0, Math.round(sec));
        if (sec < 60) return sec + ' s';
        var m = Math.round(sec / 60), h = Math.floor(m / 60);
        return m < 60 ? m + ' min' : h + ' h' + (m % 60 ? ' ' + (m % 60) + ' min' : '');
    }
    function spacingOf(sec) {
        if (sec < 60) return 'every ' + sec + ' seconds';
        var m = sec / 60;
        return m === 1 ? 'every minute' : 'every ' + (m === Math.floor(m) ? m : m.toFixed(1)) + ' minutes';
    }
    function rowsOf(list) {  // the hub's `cmds` rows, checked and cleaned (at most 24, one for each word)
        var out = [], seen = {}, i, r;
        if (!Array.isArray(list)) return out;
        for (i = 0; i < list.length && out.length < LIMIT.rows; i++) {
            r = list[i];
            if (!r || typeof r !== 'object' || typeof r.w !== 'string' || !/^\/[a-z][a-z0-9]{0,15}$/.test(r.w) || seen[r.w] === true) continue;
            seen[r.w] = true;
            out.push({ w: r.w, a: typeof r.a === 'string' ? clean(r.a, LIMIT.args) : '', d: typeof r.d === 'string' ? clean(r.d, LIMIT.desc) : '', g: (typeof r.g === 'string' && clean(r.g, LIMIT.group)) || TEXT.group });
        }
        return out;
    }
    function narrow(rows, typed) {
        var q = String(typed).toLowerCase();
        return q === '/' ? rows.slice() : rows.filter(function (r) { return r.w.indexOf(q) === 0; });
    }
    function countOf(text) {
        var t = String(text).replace(/[ ,]/g, '');
        if (!/^[0-9]{1,5}$/.test(t)) return -1;
        var n = parseInt(t, 10);
        return n >= 1 && n <= LIMIT.count ? n : -1;
    }
    function everyOf(text) {
        var t = String(text).replace(/^\s+|\s+$/g, '').replace(',', '.');
        if (t === '') return 0;
        if (!/^[0-9]{1,3}(\.(0|5|00|50))?$/.test(t)) return -1;
        var v = parseFloat(t);
        return v <= LIMIT.every ? v : -1;
    }
    function splitsOf(bots) { return SPLITS[bots] || []; }
    function teamChoices(bots) { return [['random', 'A draw'], ['free', 'Free for all']].concat(splitsOf(bots)); }
    function teamsOf(s) { return teamChoices(s.bots).some(function (c) { return c[0] === s.teams; }) ? s.teams : 'random'; }
    function startState() { return { count: 1, every: 0, map: null, rules: 'random', bots: 'random', levels: 'mixed', teams: 'random' }; }
    function mapText(file) { var t = clean(file, 400); return t.length >= 1 && t.length <= LIMIT.map && !/[<>]/.test(t) ? t : ''; }
    function stateOk(s) { return s.count >= 1 && s.count <= LIMIT.count && s.every >= 0 && s.every <= LIMIT.every && s.every * 2 === Math.floor(s.every * 2) && (!s.map || mapText(s.map.file) !== ''); }
    function commandOf(s) {  // the line the builder writes (docs/HUB_COMMANDS.md): a choice left open is left out
        var c = '/bots ' + s.count, teams = teamsOf(s);
        if (s.every > 0 && s.count > 1) c += ' every ' + s.every;
        if (s.rules !== 'random') c += ' rules ' + s.rules;
        if (s.bots !== 'random') c += ' bots ' + s.bots;
        if (s.levels !== 'mixed') c += ' levels ' + s.levels;
        if (teams !== 'random') c += ' teams ' + teams;
        if (s.map && mapText(s.map.file)) c += ' map ' + mapText(s.map.file);
        return c;
    }
    function planOf(s, atOnce) {   // what will happen, in plain words: [{ bad, parts: [text | { b: text }] }] (atOnce 0: the server has not said)
        var n = s.count, out = [];
        if (!(n >= 1 && n <= LIMIT.count)) return [{ bad: true, parts: [TEXT.badCount] }];
        if (!(s.every >= 0 && s.every <= LIMIT.every && s.every * 2 === Math.floor(s.every * 2))) return [{ bad: true, parts: [TEXT.badEvery] }];
        if (s.map && mapText(s.map.file) === '') return [{ bad: true, parts: [TEXT.badMap] }];
        if (n === 1) out.push({ parts: ['One match starts now.'] });
        else if (s.every > 0) {
            out.push({ parts: ['One starts now, then one ' + spacingOf(s.every * 60) + ': the last of ' + matchesOf(n) + ' starts in about ' + spanOf((n - 1) * s.every * 60) + '.'] });
            out.push({ parts: [(atOnce ? 'No more than ' + atOnce + ' run at once; a' : 'A') + ' start that comes while the server is full waits for room.'] });
        } else if (atOnce && n <= atOnce) out.push({ parts: ['All ' + matchesOf(n) + ' start now, one a second.'] });
        else if (atOnce) {
            out.push({ parts: [{ b: n1(atOnce) }, ' run at once, ', { b: n1(n - atOnce) }, ' wait. A new one starts as each one ends, until all ' + n1(n) + ' have played or you type /bots stop.'] });
            out.push({ parts: ['People\u2019s matches always come first: the bots give way when the server is busy.'] });
        } else out.push({ parts: ['The server runs as many as it has room for at once; the rest wait, and a new one starts as each one ends, until all ' + n1(n) + ' have played or you type /bots stop.'] });
        if (n > 3) out.push({ parts: ['The server keeps ' + KEEP + ' of them an hour as replays; the rest are played but not kept (watch them live while they run).'] });
        return out;
    }

    // the words of the hub's `reply` frames (docs: the hub sends a code and numbers, this page says it)
    var BOTS_ERR = {
        usage: 'That is not how /bots is typed. Choose it in the list of commands (type /) to open the box that writes it for you.', bad_count: TEXT.badCount, bad_every: TEXT.badEvery,
        bad_option: 'A word after the number is not one that /bots knows, is typed twice, or does not go with the others (a split needs 3 or 4 bots).',
        no_map: 'That map cannot be played now. Choose another.', map_187: 'ISLANDS is not played in 187 (nobody could be struck there). Choose another map or the rules Highest score.',
        no_replays: 'This server keeps no replays, so it plays no bot matches.', door: 'The game server did not answer, so nothing was started. Try again in a moment.', off: 'Bot matches are not set up on this chat.', role: TEXT.role
    };
    var IGNORE_ERR = {
        usage: ['Type /ignore NAME, for example /ignore pebble.', 'Type /unignore NAME, for example /unignore pebble.'], nobody: 'Nobody in the chat has that name.', ambiguous: 'Several people show that name, so it cannot be told which one you mean.',
        self: 'You cannot ignore yourself.', staff: 'A moderator or an administrator cannot be ignored.', full: 'Your list is full (50 people). Type /unignore NAME to make room.', not: 'That person is not on your list.'
    };
    function heldWords(why, atOnce, busy) {
        if (why === 'cap') return atOnce ? 'the limit of ' + atOnce + ' bot matches at once is reached' : 'the limit of bot matches at once is reached';
        if (why === 'places') return 'the server has no places left for more bot matches';
        if (why === 'people') return 'people\u2019s matches are using the server';
        if (why === 'busy') return 'the server is busy' + (busy ? ' (' + busy + '%)' : '');
        return 'there is no room yet';
    }
    function startedOf(m, now, clock) {
        var asked = int(m.asked), started = int(m.started), queued = int(m.queued), every = int(m.every_s), lines = [], at = int(m.at_once), late;
        asked = asked || started + queued;
        if (!started && queued) lines.push('Queued ' + matchesOf(asked) + '. None has started yet: ' + heldWords(m.held, at) + '. They start as soon as there is room.');
        else lines.push('Started ' + matchesOf(asked) + '.');
        if (!queued) return lines;
        if (every > 0) {
            late = clock(new Date(now + (asked - 1) * every * 1000));
            lines.push((started === 1 ? 'One is going now' : started ? n1(started) + ' are going now' : 'None is going yet') + '; ' + (queued === 1 ? 'the other one starts in ' + spanOf(every) + ' (at ' + late + ').' : 'the next ' + wordOf(queued) + ' start ' + spacingOf(every) + ' (the last one at ' + late + ').'));
        } else if (started) {
            lines.push((started === 1 ? 'The first one is going; ' : 'The first ' + n1(started) + ' are going; ') + n1(queued) + ' wait and start as each one ends.');
            lines.push('Type /bots status to see how far it is, /bots stop to end it.');
        }
        return lines;
    }
    function batchOf(q, at, busy) {   // one queue of the status: the bar and the lines under it
        var total = int(q.total), rem = int(q.remaining), fin = int(q.finished), run = int(q.running), kept = Math.min(int(q.kept), fin), every = int(q.every_s), next = typeof q.next_in === 'number' && q.next_in >= 0 ? Math.round(q.next_in) : -1;
        var done = total ? Math.min(100, fin * 100 / total) : 0, going = total ? Math.min(100 - done, run * 100 / total) : 0, nums = [[n1(fin), 'done'], [n1(run), 'running'], [n1(rem), 'waiting']], fine = [];
        if (every > 0 && rem > 0) { if (next >= 0) nums.push([spanOf(next), 'to the next start']); nums.push(['about ' + spanOf(Math.max(next, 0) + (rem - 1) * every), 'to the last start']); }
        if (q.held === true || (typeof q.held === 'string' && q.held !== '')) fine.push('Waiting for room: ' + heldWords(typeof q.held === 'string' ? q.held : q.why, at, busy) + '.');
        else if (rem > 0 && !every) fine.push('The next one starts as soon as one ends.');
        if (fin) fine.push(kept + ' of the ' + n1(fin) + ' that ended ' + (kept === 1 ? 'is' : 'are') + ' kept as replays (' + KEEP + ' an hour); the rest could only be watched live.');
        return { title: 'Batch of ' + matchesOf(total), by: typeof q.by === 'string' ? clean(q.by, LIMIT.name) : '', done: done, running: going, nums: nums, fine: fine };
    }
    function replyOf(m, typed, now, clock) {   // a `reply` frame as { cls, lines, batches }, { help: true }, or null
        var c = typeof m.c === 'string' ? m.c : '', r = typeof m.r === 'string' ? m.r : '', say = function (t, cls) { return { cls: cls || '', lines: [t], batches: [] }; }, bad = function (t) { return say(t, 'gc-err'); }, n, at, list;
        clock = clock || function (d) { return ('0' + d.getHours()).slice(-2) + ':' + ('0' + d.getMinutes()).slice(-2); };
        if (r === 'unknown') return bad(TEXT.unknown);
        if (r === 'role') return bad(TEXT.role);
        if (c === 'help' && r === 'list') return { help: true };
        if (r === 'rate') { n = Math.min(LIMIT.wait, Math.max(1, int(m.wait))); return bad('Too fast: wait ' + n + (n === 1 ? ' second' : ' seconds') + ' before the next ' + (/^[a-z]{1,16}$/.test(c) && c !== 'unknown' ? '/' + c : 'command') + '.'); }
        if (c === 'ignore' || c === 'unignore') {
            if (r === 'ok') { n = int(m.n); return say(c === 'ignore' ? 'Done. You will not see that person\u2019s lines any more. You ignore ' + (n ? peopleOf(n) : 'nobody') + ' now.' : 'Done. You see that person\u2019s lines again. You ignore ' + (n ? peopleOf(n) : 'nobody') + ' now.'); }
            if (r === 'usage') return bad(IGNORE_ERR.usage[c === 'ignore' ? 0 : 1]);
            return Object.prototype.hasOwnProperty.call(IGNORE_ERR, r) && r !== 'usage' ? bad(IGNORE_ERR[r]) : bad(TEXT.generic);
        }
        if (c !== 'bots') return bad(TEXT.generic);
        if (r === 'started') return { cls: '', lines: startedOf(m, now, clock), batches: [] };
        if (r === 'stopped') {
            n = int(m.stopped); at = int(m.dropped);
            return say(!n && !at ? 'Nothing was running or waiting.' : 'Stopped: ' + (n ? matchesOf(n) + ' ended' : '') + (n && at ? ' and ' : '') + (at ? (at === 1 ? '1 waiting start' : n1(at) + ' waiting starts') + ' cancelled' : '') + '.');
        }
        if (r === 'status') {
            list = Array.isArray(m.queues) ? m.queues.slice(0, LIMIT.queues) : [];
            at = int(m.at_once); n = int(m.running);
            if (!list.length) return say(n ? matchesOf(n) + (n === 1 ? ' is' : ' are') + ' running; no batch is waiting.' : 'No bot match is running and no batch is waiting.');
            return { cls: 'gc-status', lines: list.length > 1 ? ['Bot matches: ' + n1(n) + ' running, ' + n1(int(m.waiting)) + ' waiting' + (at ? ' (up to ' + n1(at) + ' at once)' : '') + '.'] : [], batches: list.map(function (q) { return q && typeof q === 'object' ? batchOf(q, at, int(m.busy)) : batchOf({}, at, 0); }) };
        }
        if (r === 'bad_map') {
            var close = (Array.isArray(m.close) ? m.close : []).filter(function (x) { return typeof x === 'string' && x; }).slice(0, LIMIT.close).map(function (x) { return clean(x, LIMIT.map); }), asked = /(?:^|\s)map\s+(.+)$/i.exec(String(typed || ''));
            return bad((asked ? 'No map called \u201c' + clean(asked[1], LIMIT.map) + '\u201d.' : 'No map has that name.') + (close.length ? ' Did you mean ' + (close.length > 1 ? close.slice(0, -1).join(', ') + ' or ' + close[close.length - 1] : close[0]) + '?' : ''));
        }
        if (r === 'queue_full') return bad('Not started: ' + n1(int(m.asked)) + ' more would wait with the ' + n1(int(m.waiting)) + ' that wait already, and the server holds ' + n1(int(m.max)) + ' at the most. Type /bots stop to clear them, or ask for fewer.');
        return Object.prototype.hasOwnProperty.call(BOTS_ERR, r) ? bad(BOTS_ERR[r]) : bad(TEXT.generic);
    }

    // ---- the mounted part ------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    function mount(env) {
        var doc = env.doc, win = env.win, dock = env.dock, field = dock.field, form = field.parentNode, body = doc.body;
        var now = env.now || function () { return Date.now(); }, rows = [], pop = null, sel = 0, shown = [], last = null;
        var box = null, back = null, adm = null, atOnce = 0, atAsked = false, busy = false, S = startState(), refreshBuilder = null;

        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
        function put(parent, kids) { kids.forEach(function (k) { if (k) parent.appendChild(k); }); return parent; }
        function getJson(path, done) {   // a GET of the admin API (same origin, the cookie): done(status, json or null)
            var f = win.fetch;
            if (typeof f !== 'function') { done(0, null); return; }
            try { f.call(win, path, { credentials: 'same-origin', cache: 'no-store' }).then(function (r) { return r.json().then(function (j) { done(r.status, j); }, function () { done(r.status, null); }); }, function () { done(0, null); }); } catch (e) { done(0, null); }
        }
        function clock(d) {
            try { return new win.Intl.DateTimeFormat(undefined, { hour: 'numeric', minute: '2-digit' }).format(d); } catch (e) { return ('0' + d.getHours()).slice(-2) + ':' + ('0' + d.getMinutes()).slice(-2); }
        }

        // ---- the helper ----
        function paint(q) {
            var list = el('div', 'gh-body'), top = el('div', 'gh-top'), cap = '', foot = el('div', 'gh-foot'), how = el('span'), all = q === '/';
            put(top, [el('b', '', all ? TEXT.top : TEXT.topNarrow), el('span', '', shown.length + (shown.length === 1 ? TEXT.match : all ? TEXT.inAll : TEXT.matches))]);
            if (!shown.length) list.appendChild(el('p', 'gh-none', TEXT.none + q + TEXT.noneEnd));
            shown.forEach(function (c, i) {
                if (all && c.g !== cap) { cap = c.g; list.appendChild(el('p', 'gh-cap', cap)); }
                var r = el('button', 'gh-row'), w = el('span', 'gh-w');
                r.type = 'button'; r.id = POP + '-' + i; r.tabIndex = -1; r.setAttribute('role', 'option'); r.setAttribute('aria-selected', i === sel ? 'true' : 'false');
                if (q.length > 1) put(w, [el('mark', '', c.w.slice(0, q.length)), doc.createTextNode(c.w.slice(q.length))]); else w.textContent = c.w;
                put(r, [w, el('span', 'gh-d', c.d), c.a ? el('span', 'gh-a', c.w + ' ' + c.a) : null]);
                r.addEventListener('click', function () { choose(c); });
                list.appendChild(r);
            });
            put(how, [doc.createTextNode(TEXT.choose), el('kbd', '', '\u2191'), doc.createTextNode(' '), el('kbd', '', '\u2193'), doc.createTextNode(TEXT.then), el('kbd', '', 'Tab')]);
            put(foot, [how, el('span', '', TEXT.esc)]);
            pop.textContent = '';
            put(pop, [top, list, foot]);
            var chosen = shown[sel];
            if (chosen) field.setAttribute('aria-activedescendant', POP + '-' + sel); else field.removeAttribute('aria-activedescendant');
            var bx = dock.root.getBoundingClientRect(), f = form.getBoundingClientRect(), phone = (win.innerWidth || 1000) <= 700 || (win.innerHeight || 1000) <= 500;
            pop.style.maxHeight = Math.max(150, Math.min(440, phone ? f.top - bx.top - 14 : f.top - 10)) + 'px';
            var s = pop.querySelector('[aria-selected="true"]');
            if (s && s.scrollIntoView) s.scrollIntoView({ block: 'nearest' });
        }
        function openFor(v) {
            var q = v.toLowerCase();
            shown = narrow(rows, q); sel = 0;
            if (!pop) { pop = el('div', 'gh-pop'); pop.id = POP; pop.addEventListener('pointerdown', function (e) { if (e && e.preventDefault) e.preventDefault(); });       // (a press keeps the focus in the field: a phone's keyboard stays up)
                pop.setAttribute('role', 'listbox'); pop.setAttribute('aria-label', TEXT.listLabel); form.appendChild(pop); field.setAttribute('aria-controls', POP); }
            paint(q);
        }
        function close() {
            if (pop && pop.parentNode) pop.parentNode.removeChild(pop);
            pop = null; shown = [];
            field.removeAttribute('aria-activedescendant'); field.removeAttribute('aria-controls');
        }
        function sync() {   // the list follows the field: open for a slash and letters, closed for any other text; only a changed text counts (Escape stays closed)
            var v = field.value;
            if (v === last) return;
            last = v;
            if (!rows.length || field.disabled || !/^\/[A-Za-z]*$/.test(v)) close(); else openFor(v);
        }
        function setField(v) {
            field.value = v;
            if (dock.grow) dock.grow();
            field.focus();
            if (field.setSelectionRange) field.setSelectionRange(v.length, v.length);
            sync();
        }
        function choose(c) {   // a row chosen: its word goes into the field (a space where names follow); /bots opens the builder
            close();
            if (c.w === '/bots') { setField('/bots '); builder(); return; }
            setField(c.w + (c.a ? ' ' : ''));
        }
        function key(e) {   // the dock's field asks first: true when the list took the key
            var k = e && e.key, v, c;
            if (!pop || !k) return false;
            var stop = function () { if (e.preventDefault) e.preventDefault(); };
            if (k === 'ArrowDown' || k === 'ArrowUp') { if (shown.length) { sel = Math.max(0, Math.min(shown.length - 1, sel + (k === 'ArrowDown' ? 1 : -1))); paint(field.value.toLowerCase()); } stop(); return true; }
            if (k === 'Escape') { close(); stop(); if (e.stopPropagation) e.stopPropagation(); return true; }
            if (!shown.length || e.shiftKey || e.isComposing) return false;
            c = shown[sel];
            if (k === 'Tab') { choose(c); stop(); return true; }
            if (k === 'Enter') {
                v = field.value.replace(/^\s+|\s+$/g, '').toLowerCase();
                if (v === c.w && !c.a) return false;   // a whole word of a command that takes nothing more is sent, not chosen again
                choose(c); stop(); return true;
            }
            return false;
        }
        field.addEventListener('blur', function () { if (pop && !(pop.contains && doc.activeElement && pop.contains(doc.activeElement))) close(); });

        // ---- the builder ----
        function closeBuilder() {
            [box, back].forEach(function (n) { if (n && n.parentNode) n.parentNode.removeChild(n); });
            box = back = refreshBuilder = null;
        }
        function loadPick(done) {   // the admin page's list of maps: its script and style, once, on the first press of Map
            if (win.AntsAdminMapPick) { done(true); return; }
            try {
                var css = el('link'), js = el('script');
                css.rel = 'stylesheet'; css.href = PICK_CSS;
                js.src = PICK; js.async = true; js.onload = function () { done(!!win.AntsAdminMapPick); }; js.onerror = function () { done(false); };
                (doc.head || body).appendChild(css); (doc.head || body).appendChild(js);
            } catch (e) { done(false); }
        }
        function pickMap(note, mapBtn) {
            if (busy || adm) return;   // (a second press while the maps come, or while the list is open, is the same press)
            busy = true;
            note.textContent = TEXT.loading;
            getJson(OPTIONS, function (status, j) {
                busy = false;
                var maps = status === 200 && j && Array.isArray(j.maps) ? j.maps.slice(0, 2000).filter(function (m) { return m && typeof m.file === 'string' && typeof m.name === 'string'; }) : null, shell = null;
                if (!maps) { note.textContent = TEXT.noMaps; S.map = null; if (refreshBuilder) refreshBuilder(); return; }
                loadPick(function (ok) {
                    if (!ok || !box) { note.textContent = ok ? '' : TEXT.noMaps; return; }
                    note.textContent = '';
                    var inert = [];
                    var picker = win.AntsAdminMapPick.make({
                        doc: doc, el: el, add: function (p) { for (var i = 1; i < arguments.length; i++) if (arguments[i]) p.appendChild(arguments[i]); return p; },
                        button: function (label, cls, onclick) { var b = el('button', 'btn ' + cls, label); b.type = 'button'; if (onclick) b.addEventListener('click', onclick); return b; },
                        typed: function (tag, cls, text) { var n = el(tag, cls, clean(text, 200)); n.setAttribute('data-untrusted', 'typed'); return n; },
                        questionBox: function (dlg, cancel, go, closeDialog) {
                            var scrim = el('div', 'scrim'), down = false, hit = false;
                            scrim.appendChild(dlg);
                            scrim.addEventListener('pointerdown', function (e) { down = e.target === scrim; hit = false; });
                            scrim.addEventListener('pointerup', function (e) { hit = down && e.target === scrim; down = false; });
                            scrim.addEventListener('click', function (e) { var h = hit; hit = false; if (h && e.target === scrim) closeDialog(); });
                            return { node: scrim, focus: cancel, onKey: function () {} };
                        },
                        inertOthers: function (keep) { for (var n = keep; n && n !== body && n.parentNode; n = n.parentNode) Array.prototype.slice.call(n.parentNode.children).forEach(function (x) { if (x !== n) { x.setAttribute('inert', ''); inert.push(x); } }); },
                        clearInert: function () { inert.forEach(function (x) { x.removeAttribute('inert'); }); inert = []; }
                    });
                    adm = el('div', 'adm gcm-adm'); body.appendChild(adm);
                    picker.open({
                        host: adm, title: TEXT.mapTitle, mode: 'choose', current: S.map ? S.map.file : 'random:rotation', cap: atOnce || 0, randomMax: int(j.random_max_minutes) || 30, maps: function () { return maps; },
                        randoms: function () { return [['random:rotation', 'Random from the rotation', 'The default: the server draws the map from its rotation lists']]; }, rotFiles: function () { return {}; },
                        onPick: function (m) { S.map = /^random:/.test(String(m.file)) ? null : { file: String(m.file), name: String(m.name) }; picker.close(); if (refreshBuilder) refreshBuilder(); },
                        onClose: function () { if (adm && adm.parentNode) adm.parentNode.removeChild(adm); adm = null; if (mapBtn && mapBtn.focus) mapBtn.focus(); }
                    });
                });
            });
        }
        function build() {
            var d = el('div', 'gb'), head = el('div', 'gb-head'), x = el('button', 'gb-x', '\u00d7'), bd = el('div', 'gb-body'), title = el('h3', '', TEXT.title);
            d.setAttribute('role', 'dialog'); d.setAttribute('aria-modal', 'true'); d.setAttribute('aria-labelledby', 'gb-title'); title.id = 'gb-title'; title.tabIndex = -1;
            x.type = 'button'; x.setAttribute('aria-label', TEXT.close); put(head, [title, x]);
            var r1 = el('div', 'gb-row'), l1 = el('label', '', TEXT.howMany), n1i = el('input', 'gb-in gb-num'), c1 = el('div'), pre = el('div', 'gb-presets');
            n1i.id = 'gb-n'; l1.setAttribute('for', 'gb-n'); n1i.type = 'text'; n1i.setAttribute('inputmode', 'numeric'); n1i.setAttribute('autocomplete', 'off'); n1i.value = String(S.count);
            PRESETS.forEach(function (v) { var b = el('button', '', n1(v)); b.type = 'button'; b.addEventListener('click', function () { S.count = v; n1i.value = String(v); refresh(); }); pre.appendChild(b); });
            put(c1, [n1i, pre, el('p', 'gb-hint', TEXT.up)]); put(r1, [l1, c1]);
            var r2 = el('div', 'gb-row'), l2 = el('label', '', TEXT.every), p2 = el('div', 'gb-pair'), n2 = el('input', 'gb-in gb-num');
            n2.id = 'gb-e'; l2.setAttribute('for', 'gb-e'); n2.type = 'text'; n2.setAttribute('inputmode', 'decimal'); n2.setAttribute('autocomplete', 'off'); n2.value = String(S.every);
            put(p2, [n2, el('span', 'gb-unit', TEXT.unit)]); put(r2, [l2, p2]);
            var r3 = el('div', 'gb-row'), l3 = el('span', 'gb-l', TEXT.map), mapBtn = el('button', 'gb-in gb-map'), mapName = el('span'), mapNote = el('p', 'gb-hint'), c3 = el('div');
            mapBtn.type = 'button'; mapBtn.setAttribute('aria-haspopup', 'dialog'); put(mapBtn, [mapName, el('i', '', '\u25be')]); put(c3, [mapBtn, mapNote]); put(r3, [l3, c3]);
            mapBtn.addEventListener('click', function () { pickMap(mapNote, mapBtn); });
            var segs = {}, teamSel = null;
            function seg(label, k, opts) {
                var r = el('div', 'gb-row'), l = el('span', 'gb-l', label), s = el('div', 'gb-seg'); segs[k] = { box: s, opts: opts };
                opts.forEach(function (o) { var b = el('button', '', o[1]); b.type = 'button'; b.addEventListener('click', function () { S[k] = o[0]; S.teams = teamsOf(S); refresh(); }); s.appendChild(b); });
                return put(r, [l, s]);
            }
            function pick(label, k, opts) {
                var r = el('div', 'gb-row'), l = el('label', '', label), s = el('select', 'gb-in gb-sel');
                l.setAttribute('for', 'gb-' + k); s.id = 'gb-' + k; segs[k] = { select: s, opts: opts };
                s.addEventListener('change', function () { S[k] = s.value; refresh(); });
                if (k === 'teams') teamSel = s; else opts.forEach(function (o) { var op = el('option', '', o[1]); op.value = o[0]; s.appendChild(op); });
                return put(r, [l, s]);
            }
            put(bd, [r1, r2, r3, seg(TEXT.rules, 'rules', RULES), seg(TEXT.bots, 'bots', BOTS), pick(TEXT.level, 'levels', LEVELS), pick(TEXT.teams, 'teams', [])]);
            var foot = el('div', 'gb-foot'), planBox = el('div', 'gb-plan'), cmd = el('code', 'gb-cmd'), btns = el('div', 'gb-btns'), cancel = el('button', 'btn', TEXT.cancel), write = el('button', 'btn', TEXT.write);
            planBox.setAttribute('role', 'status'); cancel.type = 'button'; write.type = 'button'; put(btns, [cancel, write]); put(foot, [planBox, cmd, btns]); put(d, [head, bd, foot]);
            function refresh() {
                var good = stateOk(S), opts = teamChoices(S.bots);
                planBox.textContent = '';
                planOf(S, atOnce).forEach(function (l) {
                    var p = el('p', l.bad ? 'gb-bad' : '');
                    l.parts.forEach(function (t) { p.appendChild(typeof t === 'string' ? doc.createTextNode(t) : el('b', '', t.b)); });
                    planBox.appendChild(p);
                });
                cmd.textContent = commandOf(S);
                mapName.textContent = S.map ? clean(S.map.name, 60) : TEXT.rotation;
                [].forEach.call(pre.children, function (b, i) { b.setAttribute('aria-pressed', S.count === PRESETS[i] ? 'true' : 'false'); });
                if (teamSel.getAttribute('data-n') !== S.bots) {   // a split needs three or four bots
                    teamSel.textContent = '';
                    opts.forEach(function (o) { var op = el('option', '', o[1]); op.value = o[0]; teamSel.appendChild(op); });
                    teamSel.setAttribute('data-n', S.bots);
                }
                teamSel.value = S.teams;
                Object.keys(segs).forEach(function (k) { var g = segs[k]; if (g.select) { if (k !== 'teams') g.select.value = S[k]; } else [].forEach.call(g.box.children, function (b, i) { b.setAttribute('aria-pressed', S[k] === g.opts[i][0] ? 'true' : 'false'); }); });
                write.disabled = !good; write.setAttribute('aria-disabled', good ? 'false' : 'true');
            }
            n1i.addEventListener('input', function () { S.count = countOf(n1i.value); refresh(); });
            n2.addEventListener('input', function () { var v = everyOf(n2.value); S.every = v; refresh(); });
            x.addEventListener('click', closeBuilder); cancel.addEventListener('click', function () { closeBuilder(); field.focus(); });
            write.addEventListener('click', function () { if (!stateOk(S)) return; var c = commandOf(S); closeBuilder(); setField(c); });
            refreshBuilder = refresh; d.setAttribute('data-ready', 'true');
            refresh();
            return { node: d, first: n1i, title: title };
        }
        function builder() {
            closeBuilder();
            if (!(S.count >= 1)) S.count = 1;   // (a count that was not a number when the box was closed is one again)
            if (!(S.every >= 0)) S.every = 0;
            if (!atAsked) { atAsked = true; getJson(SUMMARY, function (status, j) { if (status === 200 && j && int(j.at_once)) { atOnce = int(j.at_once); if (refreshBuilder) refreshBuilder(); } else atAsked = false; }); }
            var b = build();
            box = b.node; back = el('div', 'gb-back');
            back.addEventListener('click', closeBuilder);
            body.appendChild(back); body.appendChild(box);
            var fine = typeof win.matchMedia === 'function' && win.matchMedia('(pointer: fine)').matches;
            (fine ? b.first : b.title).focus();
        }
        doc.addEventListener('keydown', function (e) {   // Escape closes the builder; Tab goes round inside it
            if (!box || !e || adm) return;   // (while the admin page's list of maps is open the keys are its own)
            if (e.key === 'Escape') { closeBuilder(); field.focus(); return; }
            if (e.key !== 'Tab') return;
            var items = Array.prototype.slice.call(box.querySelectorAll('button, input, select')).filter(function (n) { return !n.disabled && !n.hidden; }), at = items.indexOf(doc.activeElement);
            if (!items.length) return;
            if (at < 0 || (e.shiftKey && at === 0) || (!e.shiftKey && at === items.length - 1)) { if (e.preventDefault) e.preventDefault(); items[e.shiftKey ? items.length - 1 : 0].focus(); }
        }, true);

        // ---- the private answers ----
        function echo(typed) { var d = el('div', 'gc-pl gc-echo'); d.appendChild(el('bdi', '', clean(typed, 200))); return d; }
        function reply(m, typed) {
            var r = replyOf(m, typed, now(), clock);
            if (!r) return;
            if (r.help) { setField('/'); return; }
            dock.block(function (row) {
                if (typed && r.cls !== 'gc-status') row.appendChild(echo(typed));          // (a status says what it is: its first line is the batch, so that a short log shows the bar)
                r.lines.forEach(function (l) { row.appendChild(el('div', 'gc-pl', l)); });
                r.batches.forEach(function (b) {
                    var t = el('div', 'gc-pl', b.title), bar = el('div', 'gc-bar'), a = el('i'), g = el('i'), nums = el('div', 'gc-nums');
                    if (b.by) put(t, [doc.createTextNode(' by '), el('bdi', '', b.by)]);
                    a.style.width = b.done + '%'; g.style.left = b.done + '%'; g.style.width = b.running + '%';
                    bar.setAttribute('role', 'progressbar'); bar.setAttribute('aria-valuemin', '0'); bar.setAttribute('aria-valuemax', '100'); bar.setAttribute('aria-valuenow', String(Math.round(b.done))); bar.setAttribute('aria-label', b.title);
                    b.nums.forEach(function (n) { put(nums, [put(el('span'), [el('b', '', n[0]), doc.createTextNode(' ' + n[1])])]); });
                    put(bar, [a, g]); put(row, [t, bar, nums]);
                    b.fine.forEach(function (f) { row.appendChild(el('div', 'gc-fine', f)); });
                });
            }, r.cls);
        }
        return {
            rows: function (list) { rows = rowsOf(list); last = null; if (!rows.length) { close(); closeBuilder(); } else sync(); },
            reply: reply, key: key, sync: sync, builder: builder, closeBuilder: closeBuilder, state: function () { return { rows: rows.slice(), open: pop !== null, shown: shown.slice(), selected: sel, builder: box !== null, atOnce: atOnce }; }
        };
    }

    return { LIMIT: LIMIT, TEXT: TEXT, KEEP: KEEP, clean: clean, rowsOf: rowsOf, narrow: narrow, countOf: countOf, everyOf: everyOf, teamChoices: teamChoices, startState: startState, mapText: mapText, stateOk: stateOk, commandOf: commandOf, planOf: planOf, replyOf: replyOf, mount: mount };
});
