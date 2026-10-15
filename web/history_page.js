/* The helpers that the pages of the match history share (docs/REPLAYS.md "The match pages"): the list of every match (matches.html), one match (match.html), the matches that run now (live.html) and the
   "Stats" of the game page in replay mode (shell.html). Plain functions with no page in them, so that tests/scripts/web_history_check.js runs them on a table, and two small parts that touch a document
   (which they are given): the pictures of the maps and the panel of stats on the game page. Everything that comes from the server (a name that a player typed, a map, an id) is TEXT: the pages put it in with textContent and
   never as markup. A record is read defensively: a key that this file does not know is skipped, a number that is not one is 0, a word that is not one of the server's falls back to a plain default. */
(function (root, factory) {
    'use strict';
    var api = factory(root);
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHistory = api;
})(typeof self !== 'undefined' ? self : this, function (root) {
    'use strict';

    var PAGE = 25;                                       // matches on one page of the list
    // The id of a match, as the replay store makes it (the recording's name without ".antsrep") and as docker/nginx.conf lets it through
    var ID_RE = /^ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?$/;
    var FILE_RE = /^ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?\.antsrep$/;
    var COLOURS = ['Green', 'Red', 'Blue', 'Black'];
    var FORMATS = { '1v1': '1 v 1', '2v1': '2 v 1', '2v2': '2 v 2', 'ffa3': 'Free for all', 'ffa4': 'Free for all', 'solo': 'Alone' };
    var MODES = { 'highest-score': 'Highest score', '187': '187' };

    // The id that an address asks for (?id=<id>): the id when it has the shape above, else null
    function idOf(search) {
        var q;
        try { q = new URLSearchParams(search); } catch (e) { return null; }
        var id = q.get('id');
        return typeof id === 'string' && ID_RE.test(id) ? id : null;
    }
    // The recording's file name of a match id, and the other way round
    function fileOfId(id) { return typeof id === 'string' && ID_RE.test(id) ? id + '.antsrep' : ''; }
    function idOfFile(file) { return typeof file === 'string' && FILE_RE.test(file) ? file.slice(0, -'.antsrep'.length) : ''; }

    function whole(n) { return typeof n === 'number' && isFinite(n) && n > 0 ? Math.floor(n) : 0; }
    function text(s) { return typeof s === 'string' ? s : ''; }
    function numberText(n) {
        var s = String(whole(n)), out = '';
        for (var i = s.length; i > 0; i -= 3) out = s.slice(Math.max(0, i - 3), i) + (out ? ',' + out : '');
        return out;
    }
    function formatText(f) { return Object.prototype.hasOwnProperty.call(FORMATS, f) ? FORMATS[f] : text(f); }
    function modeText(m) { return Object.prototype.hasOwnProperty.call(MODES, m) ? MODES[m] : text(m); }

    // ---- the players of a record ----
    // What a seat shows: "Pip (bot, Hard)" for a computer player with a name, "Bot (Hard)" for one that a recording of an earlier version stored with no name, what the player typed, else the colour
    // (docs/BOT_NAMES.md; the record's `bot` is the level, "Bot" when its name says none)
    function seatLabel(seat) {
        var s = seat && typeof seat === 'object' ? seat : {};
        var name = text(s.name), bot = text(s.bot);
        if (bot) {
            if (bot === 'Bot') return name ? name + ' (bot)' : 'Bot';
            return name ? name + ' (bot, ' + bot + ')' : 'Bot (' + bot + ')';
        }
        return name || (COLOURS.indexOf(s.colour) !== -1 ? s.colour : '');
    }
    function seatsOf(rec) {
        var out = [];
        (rec && Array.isArray(rec.seats) ? rec.seats : []).forEach(function (s) {
            if (s && typeof s === 'object' && COLOURS.indexOf(s.colour) !== -1) out.push(s);
        });
        return out;
    }
    // The sides of a match are the record's rows (an alliance that both seats confirm is one row, whatever the format said at the start); a record with no rows has one side for each seat, best score first.
    // A seat that dropped out of the match before the end has no row ("The match history"): it is a side of its own after the others, marked `left`, and no part of the ranking (see rankedOf).
    function sidesOf(rec) {
        var seats = seatsOf(rec), by = {};
        seats.forEach(function (s) { by[s.seat] = s; });
        var rows = rec && Array.isArray(rec.rows) ? rec.rows : [], out = [], covered = {};
        function numbers(o) { return { score: whole(o.score), killed: whole(o.killed), lost: whole(o.lost), hatched: whole(o.hatched) }; }
        rows.forEach(function (r) {
            if (!r || typeof r !== 'object' || !Array.isArray(r.seats)) return;
            var members = r.seats.map(function (i) { return by[i]; }).filter(Boolean);
            if (members.length) { out.push({ seats: members, row: numbers(r), winner: r.winner === true, left: false }); members.forEach(function (m) { covered[m.seat] = true; }); }
        });
        if (!out.length) {
            seats.sort(function (a, b) { return whole(b.score) - whole(a.score); });
            return seats.map(function (s) { return { seats: [s], row: numbers(s), winner: s.winner === true, left: s.left === true }; });
        }
        seats.forEach(function (s) { if (!covered[s.seat]) out.push({ seats: [s], row: numbers(s), winner: false, left: true }); });
        return out;
    }
    // The sides that were in the ranking: the ones that did not drop out (their scores are the verdict and the share of the score)
    function rankedOf(sides) { return sides.filter(function (s) { return !s.left; }); }
    function sideName(side) { return side.seats.map(seatLabel).join(' & '); }
    // Mode 187 has no food and no score: the engine's score there is the kills, so the pages show kills and never call it a score
    function scoreText(rec, side) { return rec && rec.mode === '187' ? numberText(side.row.killed) + ' kills' : numberText(side.row.score); }
    function winnerSide(rec, sides) {
        if (!rec || rec.draw === true) return null;
        for (var i = 0; i < sides.length; i++) if (sides[i].winner) return sides[i];
        return null;
    }
    // What stands where the winner would be when no side won: a game for one, a draw, or a match that was cut short before the rules decided
    function noWinnerText(rec) { return rec && rec.format === 'solo' ? 'Played alone' : rec && rec.draw === true ? 'A draw: nobody won' : 'No winner'; }
    function playersText(rec) {
        var seats = seatsOf(rec), bots = seats.filter(function (s) { return text(s.bot) !== ''; }).length, people = seats.length - bots, parts = [];
        if (people) parts.push(people + (people === 1 ? ' person' : ' people'));
        if (bots) parts.push(bots + (bots === 1 ? ' computer player' : ' computer players'));
        return parts.join(', ');
    }
    function howEnded(rec) {
        if (!rec) return '';
        if (rec.quitter !== null && rec.quitter !== undefined) return 'Ended by a quit';
        if (rec.draw === true) return 'The rules ended it: a draw';
        if (rec.result === 'cut') return 'Cut short: a player left or the clock ran out';
        return 'The rules ended the match';
    }
    function bestOf(rec, key) {
        var best = 0;
        seatsOf(rec).forEach(function (s) { best = Math.max(best, whole(s[key])); });
        return best;
    }
    // The share of each side in the score (the kills in mode 187), in whole percent that add up to 100 (largest remainder); [] when nobody has any
    function shareOf(rec, sides) {
        var v = sides.map(function (s) { return rec && rec.mode === '187' ? s.row.killed : s.row.score; });
        var total = v.reduce(function (a, b) { return a + b; }, 0);
        if (!total) return [];
        var parts = v.map(function (x) { var exact = x * 100 / total; return { pct: Math.floor(exact), rest: exact - Math.floor(exact) }; });
        var left = 100 - parts.reduce(function (a, p) { return a + p.pct; }, 0);
        parts.map(function (p, i) { return i; }).sort(function (a, b) { return parts[b].rest - parts[a].rest || a - b; }).slice(0, left).forEach(function (i) { parts[i].pct++; });
        return v.map(function (x, i) { return { value: x, percent: parts[i].pct }; });
    }
    // The columns of the scoreboard, in the order of the table (a "group" is a heading over some of them); no Score in mode 187
    function columnsOf(rec) {
        var cols = [];
        if (!rec || rec.mode !== '187') cols.push({ key: 'score', label: 'Score' });
        cols.push({ key: 'killed', label: 'Killed' }, { key: 'lost', label: 'Lost' }, { key: 'hatched', label: 'Hatched' },
            { key: 'bombs_planted', label: 'Bombs set', first: true }, { key: 'bombs_defused', label: 'Defused' }, { key: 'fires_lit', label: 'Fires lit' });
        return cols;
    }
    // What a recording is: "watch" (kept and played by this build), "old" (kept, other rules: cannot be played), "removed" (gone); a word that is not one is "removed" (nothing can be offered)
    function recState(rec) {
        var kind = rec && (rec.recording === 'watch' || rec.recording === 'old') ? rec.recording : 'removed';
        var file = rec && typeof rec.file === 'string' && FILE_RE.test(rec.file) ? rec.file : '';
        if (kind !== 'removed' && !file) kind = 'removed';
        var game = rec ? text(rec.game) : '';
        if (kind === 'watch') return { kind: 'watch', label: 'Replay', file: file, note: '' };
        if (kind === 'old') return { kind: 'old', label: 'Older version', file: file, note: 'The recording was made by an older version (' + (game || 'unknown') + ') and this version of the game cannot play it. The stats below are complete.' };
        return { kind: 'removed', label: 'Replay removed', file: '', note: 'Replay removed: recordings are kept for 30 days. The stats below stay.' };
    }
    // What a replay pass may add later to a record (docs/REPLAYS.md): the keys of `details`, read as plain data and never trusted: a value that is not an object is nothing
    function detailsOf(rec) {
        var d = rec && typeof rec === 'object' ? rec.details : null;
        if (!d || typeof d !== 'object' || Array.isArray(d)) return null;
        return d;
    }

    // ---- the list: its state, the words of the server and the address of a page ----
    var WHO = [['all', 'All matches', 'All'], ['people', 'With people', 'People'], ['computers', 'Computers only', 'Computers']];
    var FILTERS = [
        { key: 'format', label: 'Format', options: [['all', 'Any format'], ['1v1', '1 v 1'], ['team', 'Teams'], ['ffa', 'Free for all']] },
        { key: 'result', label: 'Result', options: [['all', 'Any result'], ['decided', 'Decided'], ['draw', 'Draw'], ['cut', 'Cut short']] },
        { key: 'rec', label: 'Replay', options: [['all', 'Any replay'], ['watch', 'Can be watched'], ['old', 'Older version'], ['removed', 'Removed']] },
        { key: 'colour', label: 'Colour', options: [['all', 'Any colour'], ['green', 'Green'], ['red', 'Red'], ['blue', 'Blue'], ['black', 'Black']] },
        { key: 'mode', label: 'Mode', options: [['all', 'Any mode'], ['highest-score', 'Highest score'], ['187', '187']] }
    ];
    var SORTS = [['new', 'Newest first'], ['old', 'Oldest first'], ['score', 'Highest score'], ['kills', 'Most kills'], ['hatched', 'Most hatched'], ['long', 'Longest'], ['short', 'Shortest']];
    var SORT_KEYS = { score: ['Top score', 'score'], kills: ['Most kills', 'killed'], hatched: ['Most hatched', 'hatched'] };       // what a sort by a count shows on each card
    var Q_MAX = 40;                                      // the server's limit for a search text

    function defaults() { return { who: 'all', format: 'all', result: 'all', rec: 'all', colour: 'all', mode: 'all', sort: 'new', q: '', page: 1 }; }
    function has(list, value) { for (var i = 0; i < list.length; i++) if (list[i][0] === value) return true; return false; }
    // A search text as the server takes it: printable ASCII only, blanks trimmed and folded, at most 40 characters (a character outside the range is dropped, as the game makes a "?" of it)
    function cleanQuery(s) {
        var t = typeof s === 'string' ? s.replace(/[^\x20-\x7e]/g, '') : '';
        t = t.replace(/\s+/g, ' ').replace(/^ /, '');
        t = t.slice(0, Q_MAX);
        return t.replace(/ $/, '');
    }
    // The state that an address asks for: every key is checked against the server's words and a key that is not one is the default (nothing is guessed)
    function stateOf(search) {
        var st = defaults(), q;
        try { q = new URLSearchParams(search); } catch (e) { return st; }
        if (has(WHO, q.get('who'))) st.who = q.get('who');
        FILTERS.forEach(function (f) { if (has(f.options, q.get(f.key))) st[f.key] = q.get(f.key); });
        if (has(SORTS, q.get('sort'))) st.sort = q.get('sort');
        st.q = cleanQuery(q.get('q'));
        var page = parseInt(q.get('page'), 10);
        st.page = isFinite(page) && page >= 1 && page <= 100000 ? page : 1;
        return st;
    }
    // The address of a page of the list (matches.html?...: the same words as the server's, so that a link can be shared and read)
    function urlOf(st, page) {
        var d = defaults(), parts = [];
        ['who', 'format', 'result', 'rec', 'colour', 'mode', 'sort'].forEach(function (k) { if (st[k] !== d[k]) parts.push(k + '=' + st[k]); });
        if (st.q) parts.push('q=' + encodeQuery(st.q));
        var p = page === undefined ? st.page : page;
        if (p > 1) parts.push('page=' + p);
        return 'matches.html' + (parts.length ? '?' + parts.join('&') : '');
    }
    // Every byte of a query is one that docker/nginx.conf lets through (letters, digits, _ = & % + . -): encodeURIComponent leaves ! ' ( ) * ~ alone, which the site's nginx refuses, so they are escaped too
    function encodeQuery(s) {
        return encodeURIComponent(s).replace(/[!'()*~]/g, function (c) { return '%' + c.charCodeAt(0).toString(16).toUpperCase(); });
    }
    // What to ask the server for a state: the list with no query at all (the newest 50, cached by the site and not held to the server's budget) serves the first two pages of the default view; every
    // filter, search, sort and later page is a query (docs/REPLAYS.md "The match history": 2 a second for each address, a box of 15 ms) that asks for exactly one page
    function planOf(st) {
        var d = defaults(), plain = ['who', 'format', 'result', 'rec', 'colour', 'mode', 'sort', 'q'].every(function (k) { return st[k] === d[k]; }) && st.page <= 2;
        if (plain) return { query: '', skip: (st.page - 1) * PAGE, take: PAGE };
        var parts = [];
        if (st.who === 'people') parts.push('who=people'); else if (st.who === 'computers') parts.push('who=computers');
        ['format', 'result', 'rec', 'colour', 'mode'].forEach(function (k) { if (st[k] !== 'all') parts.push(k + '=' + st[k]); });
        if (st.sort !== 'new') parts.push('sort=' + st.sort);
        if (st.q) parts.push('q=' + encodeQuery(st.q));
        parts.push('limit=' + PAGE, 'offset=' + (st.page - 1) * PAGE);
        return { query: parts.join('&'), skip: 0, take: PAGE };
    }
    // The filters that are not the default, for the chips under the tools: [{ key, label, value }] (the sort is not a filter)
    function activeOf(st) {
        var d = defaults(), out = [];
        if (st.who !== d.who) out.push({ key: 'who', label: 'Who', value: WHO.filter(function (w) { return w[0] === st.who; })[0][1] });
        FILTERS.forEach(function (f) {
            if (st[f.key] !== d[f.key]) out.push({ key: f.key, label: f.label, value: f.options.filter(function (o) { return o[0] === st[f.key]; })[0][1] });
        });
        if (st.q) out.push({ key: 'q', label: 'Search', value: '“' + st.q + '”' });
        return out;
    }
    function sortLabel(sort) { for (var i = 0; i < SORTS.length; i++) if (SORTS[i][0] === sort) return SORTS[i][1]; return ''; }
    // The count that a sort by a count shows on each card: { label, value } or null for the other sorts (they show the length)
    function sortKeyOf(rec, sort) {
        var k = Object.prototype.hasOwnProperty.call(SORT_KEYS, sort) ? SORT_KEYS[sort] : null;
        return k ? { label: k[0], value: bestOf(rec, k[1]) } : null;
    }
    // The line over the list: "Showing 1 to 25 of 296 matches, newest first" (every match fits) or "Showing 1 to 21 of the 21 that fit (296 in all)"
    function rangeText(count, total, offset, shown, sort) {
        if (!shown) return '';
        var from = offset + 1, to = offset + shown;
        if (count === total) return 'Showing ' + from + ' to ' + to + ' of ' + numberText(total) + ' matches, ' + (sort === 'new' ? 'newest first' : sortLabel(sort).toLowerCase());
        return 'Showing ' + from + ' to ' + to + ' of the ' + numberText(count) + ' that fit (' + numberText(total) + ' in all)';
    }
    // The pages to link to: the first, the last, the one that is shown and its neighbours, "gap" between runs that are not adjacent: [1, 2, 3, 4, 'gap', 12]
    function pagerOf(count, page, size) {
        var pages = Math.max(1, Math.ceil(whole(count) / (size || PAGE)));
        var cur = Math.min(pages, Math.max(1, page));
        var want = {};
        [1, cur - 1, cur, cur + 1, pages].forEach(function (n) { if (n >= 1 && n <= pages) want[n] = true; });
        var list = Object.keys(want).map(Number).sort(function (a, b) { return a - b; }), items = [], last = 0;
        list.forEach(function (n) { if (n - last > 1) items.push('gap'); items.push(n); last = n; });
        return { pages: pages, page: cur, items: items };
    }
    // What a failed look at the list or at a match says, from the status (0: nothing came) and the server's own words when it gave some: { text, retry } (the server's words are shown as text)
    function problemOf(status, body) {
        var said = body && typeof body === 'object' && typeof body.error === 'string' ? body.error.slice(0, 200) : '';
        if (status === 503) return { text: said || 'The match history is busy. Try again in a moment.', retry: true };
        if (status === 404) return { text: said || 'The match history is not offered right now.', retry: false };
        if (status === 400) return { text: said || 'That search is not one the server understands.', retry: false };
        if (status === 429) return { text: 'Too many requests. Try again in a moment.', retry: true };
        return { text: 'The match history can’t be loaded right now. Try again in a moment.', retry: true };
    }

    // ---- the pictures of the maps ----
    // A square for a map: one of the six has its own picture (front/preview_<word>.png); a map of the players has the picture of the map picker (AntsMapPicker.thumbFor: the server's list GET /maps
    // when it has come, else the address that the name makes); a picture that is not there leaves the dark square. env = { document, replay (AntsReplay), picker (AntsMapPicker), fetch }.
    // `node(map, group)` makes the square, `forget(group)` drops the squares of a group that is built again, `lookMaps()` asks for the list (once) and answers with it (or null), `count()` is for tests.
    function pictures(env) {
        var doc = env.document, R = env.replay, picker = env.picker, fetcher = env.fetch;
        var maps = null, asked = null, list = [];
        function el(tag, className) { var n = doc.createElement(tag); if (className) n.className = className; return n; }
        function fill(tv, map, item) {
            tv.textContent = '';
            var key = R.mapKey(map);
            var url = key || !picker ? '' : picker.thumbFor(map, maps);
            if (item) item.tried = url;
            if (!key && !url) { tv.appendChild(el('div', 'blank')); return; }
            var img = el('img');
            img.alt = '';
            img.width = 300; img.height = 300;
            if (key) {
                img.src = 'front/preview_' + key + '.png';
            } else {
                img.loading = 'lazy'; img.decoding = 'async';
                img.addEventListener('error', function () { if (img.parentNode === tv) { tv.textContent = ''; tv.appendChild(el('div', 'blank')); } });
                img.src = url;
            }
            tv.appendChild(img);
        }
        function lookMaps() {
            if (asked) return asked;
            asked = new Promise(function (resolve) {
                var request;
                try { request = fetcher('/maps', { credentials: 'same-origin' }); } catch (e) { resolve(null); return; }
                request.then(function (response) { return response.ok ? response.json() : null; }).then(function (data) {
                    var found = data && picker ? picker.parseList(data) : null;
                    if (!found) { resolve(null); return; }
                    maps = found;
                    list = list.filter(function (item) { return doc.body.contains(item.tv); });      // (squares that left the page)
                    list.forEach(function (item) { if (picker.thumbFor(item.map, maps) !== item.tried) fill(item.tv, item.map, item); });
                    resolve(maps);
                }).catch(function () { resolve(null); });
            });
            return asked;
        }
        function node(map, group) {
            var tv = el('figure', 'tv');
            var item = R.mapKey(map) ? null : { tv: tv, map: map, tried: '', group: group };
            fill(tv, map, item);
            if (item) { list.push(item); lookMaps(); }       // (the list of the maps is asked for with the first map of the players that the page shows)
            return tv;
        }
        function forget(group) { list = list.filter(function (item) { return item.group !== group; }); }
        return { node: node, forget: forget, lookMaps: lookMaps, count: function () { return list.length; }, maps: function () { return maps; } };
    }
    // The size line of a map for the facts of a match page ("60 × 60, 12 minutes"), from the server's list when it has come; '' when the map is not in it
    function mapFact(map, list) {
        var low = typeof map === 'string' ? map.toLowerCase() : '';
        for (var i = 0; list && i < list.length; i++) {
            if (list[i].file.toLowerCase() === low) {
                var e = list[i];
                return (e.width && e.height ? e.width + ' × ' + e.height : '') + (e.minutes ? (e.width && e.height ? ', ' : '') + e.minutes + ' minutes' : '');
            }
        }
        return '';
    }

    // ---- the stats of a match on the game page in replay mode (web/shell.html, web/replay_stats.css) ----
    // The server's record of the match on show (GET /history/<id>) as a panel: the "Stats" button of the header (a wide screen) opens it over the corner of the picture, a row under the controls (a phone)
    // opens it in the page. The numbers are the final ones of the match; they do not follow the picture. A recording with no record (or a server that does not answer) gets no button.
    // env = { document, fetch, file }; the elements are the page's own: stats-btn, mstats (the panel), mstats-flow (the row) and the header.
    function replayStats(env) {
        var doc = env.document, id = idOfFile(env.file);
        if (!id || typeof env.fetch !== 'function') return;
        function mk(tag, className, content) { var n = doc.createElement(tag); if (className) n.className = className; if (content !== undefined) n.textContent = content; return n; }
        function num(seat, key) { return whole(seat[key]); }
        function seatName(seat, side, rec) {
            var d = mk('div', 'nm'), sq = mk('i'); sq.setAttribute('data-c', seat.colour); d.appendChild(sq); d.appendChild(mk('span', '', seatLabel(seat)));
            if (side.winner && rec.draw !== true) d.appendChild(mk('span', 'won', 'Won'));
            if (seat.left === true) d.appendChild(mk('span', 'left', 'Left early'));
            return d;
        }
        function body(rec) {
            var sides = sidesOf(rec), win = winnerSide(rec, sides), all = columnsOf(rec).filter(function (c) { return !c.first && c.key !== 'bombs_defused' && c.key !== 'fires_lit'; });
            var box = mk('div', 'mbody'), v = mk('p', 'mv');
            if (win) { v.appendChild(mk('em', '', sideName(win))); v.appendChild(doc.createTextNode(' won')); } else v.appendChild(doc.createTextNode(noWinnerText(rec)));
            if (rankedOf(sides).length > 1) v.appendChild(mk('span', '', rankedOf(sides).map(function (sd) { return scoreText(rec, sd); }).join(' \u2013 ')));
            box.appendChild(v);
            var t = mk('table'), hr = mk('tr'), th = mk('thead'), tb = mk('tbody'), cards = mk('div', 'cards');
            hr.appendChild(mk('th', '', 'Player')); all.forEach(function (c) { hr.appendChild(mk('th', '', c.label)); }); th.appendChild(hr); t.appendChild(th);
            var extra = [{ key: 'bombs_planted', label: 'Bombs set' }, { key: 'bombs_defused', label: 'Defused' }, { key: 'fires_lit', label: 'Fires lit' }];
            function cell(c, n, container) { var d = mk('div', 'c'); d.appendChild(mk('span', '', c.label)); d.appendChild(mk('b', n ? '' : 'zero', numberText(n))); container.appendChild(d); }
            sides.forEach(function (side) {
                var won = side.winner && rec.draw !== true;
                side.seats.forEach(function (seat) {
                    var tr = mk('tr', (won ? 'w' : '') + (side.seats.length > 1 ? ' g' : '')), td = mk('td'), card = mk('div', 'sc' + (won ? ' w' : '')), top = mk('div', 'top'), sq = mk('i');
                    td.appendChild(seatName(seat, side, rec));
                    td.appendChild(mk('span', 'sub', 'Bombs set ' + num(seat, 'bombs_planted') + ', defused ' + num(seat, 'bombs_defused') + ', fires lit ' + num(seat, 'fires_lit'))); tr.appendChild(td);
                    all.forEach(function (c) { var n = num(seat, c.key); tr.appendChild(mk('td', n ? '' : 'zero', numberText(n))); });
                    tb.appendChild(tr);
                    sq.setAttribute('data-c', seat.colour); top.appendChild(sq); top.appendChild(mk('span', 'nmt', seatLabel(seat))); if (won) top.appendChild(mk('span', 'won', 'Won')); if (seat.left === true) top.appendChild(mk('span', 'left', 'Left early')); card.appendChild(top);
                    [[all, 'g'], [extra, 'g two']].forEach(function (grp) { var g = mk('div', grp[1]); grp[0].forEach(function (c) { cell(c, num(seat, c.key), g); }); card.appendChild(g); });
                    cards.appendChild(card);
                });
                if (side.seats.length > 1) {
                    var tr2 = mk('tr', 'tot g'), tc = mk('div', 'sc tot'), g2 = mk('div', 'g');
                    tr2.appendChild(mk('td', '', 'Side total')); tc.appendChild(mk('div', 'top', 'Side total'));
                    all.forEach(function (c) { tr2.appendChild(mk('td', '', numberText(side.row[c.key]))); cell(c, side.row[c.key], g2); });
                    tb.appendChild(tr2); tc.appendChild(g2); cards.appendChild(tc);
                }
            });
            t.appendChild(tb); box.appendChild(t); box.appendChild(cards);
            var mf = mk('div', 'mf'), a = mk('a', '', 'Open the match page \u203a'); mf.appendChild(mk('span', '', 'The final numbers of the match.')); a.href = '/match.html?id=' + encodeURIComponent(rec.id); mf.appendChild(a); box.appendChild(mf);
            return box;
        }
        function mount(rec) {
            var btn = doc.getElementById('stats-btn'), pop = doc.getElementById('mstats'), flow = doc.getElementById('mstats-flow');
            if (!btn || !pop || !flow) return;
            var win = winnerSide(rec, sidesOf(rec));
            var mh = mk('div', 'mh'), x = mk('button', 'x', '\u00d7');
            mh.appendChild(mk('h2', '', 'Match stats')); x.type = 'button'; x.setAttribute('aria-label', 'Close the stats'); mh.appendChild(x); pop.appendChild(mh); pop.appendChild(body(rec));
            function setOpen(open) {
                pop.hidden = !open; btn.setAttribute('aria-expanded', String(open));
                if (open) pop.style.top = Math.round(doc.querySelector('header').getBoundingClientRect().bottom + 8) + 'px';
            }
            btn.addEventListener('click', function () { setOpen(pop.hidden); });
            x.addEventListener('click', function () { setOpen(false); btn.focus(); });
            doc.addEventListener('keydown', function (ev) { if (ev.key === 'Escape' && !pop.hidden) { setOpen(false); btn.focus(); } });
            btn.hidden = false;
            var mt = mk('button', 'mt'), l = mk('span');
            mt.type = 'button'; mt.setAttribute('aria-expanded', 'false'); l.appendChild(mk('b', '', 'Match stats'));
            l.appendChild(mk('small', '', win ? sideName(win) + ' won with ' + scoreText(rec, win) : noWinnerText(rec))); mt.appendChild(l); mt.appendChild(mk('span', '', '\u25be'));
            flow.appendChild(mt); flow.appendChild(body(rec)); flow.classList.add('closed');
            mt.addEventListener('click', function () { var closed = flow.classList.toggle('closed'); mt.setAttribute('aria-expanded', String(!closed)); });
            flow.hidden = false;
        }
        var request;
        try { request = env.fetch('/history/' + id, { cache: 'no-store', credentials: 'omit' }); } catch (e) { return; }
        request.then(function (response) { return response.ok ? response.json() : null; }).then(function (rec) {
            if (rec && typeof rec === 'object' && rec.id === id && Array.isArray(rec.seats)) mount(rec);
        }).catch(function () { /* no stats: no button */ });
    }

    return {
        PAGE: PAGE, ID_RE: ID_RE, FILE_RE: FILE_RE, COLOURS: COLOURS, WHO: WHO, FILTERS: FILTERS, SORTS: SORTS, Q_MAX: Q_MAX,
        idOf: idOf, fileOfId: fileOfId, idOfFile: idOfFile, numberText: numberText, formatText: formatText, modeText: modeText,
        seatLabel: seatLabel, seatsOf: seatsOf, sidesOf: sidesOf, sideName: sideName, scoreText: scoreText, winnerSide: winnerSide, noWinnerText: noWinnerText, playersText: playersText, howEnded: howEnded, bestOf: bestOf, shareOf: shareOf,
        columnsOf: columnsOf, recState: recState, detailsOf: detailsOf, rankedOf: rankedOf,
        defaults: defaults, cleanQuery: cleanQuery, stateOf: stateOf, urlOf: urlOf, encodeQuery: encodeQuery, planOf: planOf, activeOf: activeOf, sortLabel: sortLabel, sortKeyOf: sortKeyOf,
        rangeText: rangeText, pagerOf: pagerOf, problemOf: problemOf, pictures: pictures, mapFact: mapFact, replayStats: replayStats
    };
});
