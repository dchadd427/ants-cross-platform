// The section "Bot matches" of the admin page (docs/MAP_SUBMISSIONS.md, "Bot matches on the admin page"; the rules of the matches and of the schedules are docs/SERVER_REPLAYS.md): start matches of computer players now, see and stop the
// ones that go, and edit the schedules that start them by themselves. It is the page's second file (admin_page.js would pass its size otherwise) and is loaded before it: the page hands this file its own helpers (the
// one object `h` of make()), so a word is made and a call is sent in one way in both. Every word that comes from the server (a schedule's name, the name of a map of a player, the admin who started a match) goes in as
// text (`typed`: marked data-untrusted), never as markup. ES5 and no module system, as the page: a browser gets the global AntsAdminBots, node gets module.exports (tests/scripts/web_admin_check.js runs it with the page).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsAdminBots = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var CAP = 8;                                                // the limit of matches at once (the server's kBotMatchesMax says it too, in `cap`)
    var RULES = [['highest-score', 'Highest score'], ['187', '187'], ['random', 'Random']];
    var COUNTS = [['2', '2'], ['3', '3'], ['4', '4'], ['random', 'Random']];
    var LEVELS = [['rmh', 'Random: Medium/Hard'], ['easy', 'Easy'], ['medium', 'Medium'], ['hard', 'Hard'], ['random', 'Random: any level']];
    var LEVEL_WORD = { rmh: 'Medium or Hard', easy: 'Easy', medium: 'Medium', hard: 'Hard', random: 'any level' };
    var TEAMS = [['ffa', 'Free for all'], ['split', 'A split'], ['random', 'Random']];
    var SPLITS = {
        4: [['12-34', 'Bots 1 and 2 against 3 and 4'], ['13-24', 'Bots 1 and 3 against 2 and 4'], ['14-23', 'Bots 1 and 4 against 2 and 3']],
        3: [['1-23', 'Bot 1 against 2 and 3'], ['2-13', 'Bot 2 against 1 and 3'], ['3-12', 'Bot 3 against 1 and 2']]
    };
    var SEATS = ['Green', 'Red', 'Blue', 'Black'];
    var RANDOM_MAPS = [['random:rotation', 'Random from the rotation'], ['random:original', 'Random from the original maps'], ['random:players', 'Random from the players’ maps']];
    var GROUPS = [['original', 'The original’s maps'], ['library', 'The library (in the rotation)'], ['players', 'The players’ maps (approved)']];
    var OFF_LINE = 'Bot matches are not available on this server.';
    var NO_REPLAYS_LINE = 'This server keeps no replays, so it plays no bot matches: a match that nobody could look at again is not played.';

    function defaults() { return { count: 1, map: 'random:rotation', rules: 'random', bots: 'random', levels: ['rmh', 'rmh', 'rmh', 'rmh'], teams: 'random', split: '' }; }
    // the levels of four bots, whatever the list that came has (a schedule that was saved with two or three bots may carry only those): a level that is missing or unknown is the draw of Medium or Hard
    function paddedLevels(list) {
        var out = [], known = LEVELS.map(function (c) { return c[0]; });
        for (var i = 0; i < 4; i++) out.push(list && known.indexOf(list[i]) >= 0 ? list[i] : 'rmh');
        return out;
    }
    function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }
    function minutesAgo(seconds, nowMs) { return Math.max(0, Math.floor((nowMs / 1000 - seconds) / 60)); }
    function sidesWords(teams) {                               // "ffa", or the sides by seat: "03-12" is Green and Black against Red and Blue
        if (teams === 'ffa') return 'Free for all';
        return String(teams).split('-').map(function (side) { return side.split('').map(function (n) { return SEATS[parseInt(n, 10)] || ''; }).join(' and '); }).join(' against ');
    }

    // what a batch is, in words (one line of a schedule's row)
    function summary(w, maps) {
        var parts = [plural(w.count, 'match', 'matches')], mp = null;
        maps.forEach(function (m) { if (m.file === w.map) mp = m; });
        parts.push(w.map === 'random:rotation' ? 'a random map of the rotation' : w.map === 'random:original' ? 'a random original map' : w.map === 'random:players' ? 'a random players’ map' : mp ? mp.name : w.map);
        parts.push(w.rules === 'random' ? 'random rules' : w.rules === '187' ? 'rules 187' : 'rules Highest score');
        parts.push(w.bots === 'random' ? '2 to 4 bots' : w.bots + ' bots');
        var lv = w.levels.slice(0, w.bots === 'random' ? 4 : parseInt(w.bots, 10)), same = lv.every(function (x) { return x === lv[0]; });
        parts.push(same ? 'levels ' + (lv[0] === 'rmh' || lv[0] === 'random' ? '' : 'all ') + LEVEL_WORD[lv[0]] : 'levels ' + lv.map(function (x) { return LEVEL_WORD[x]; }).join(', '));
        parts.push(w.teams === 'ffa' ? 'free for all' : w.teams === 'random' ? 'random teams' : (SPLITS[w.bots] || []).filter(function (x) { return x[0] === w.split; }).map(function (x) { return x[1].toLowerCase(); })[0] || 'a split');
        return parts.join(' · ');
    }

    function make(h) {
        var doc = h.doc, env = h.env, el = h.el, add = h.add, button = h.button, panel = h.panel, call = h.call, message = h.message, typed = h.typed, when = h.when, untilWords = h.untilWords;
        var minutesWords = h.minutesWords, refused = h.refused, toast = h.toast, questionBox = h.questionBox, inertOthers = h.inertOthers, clearInert = h.clearInert, tick = h.tick, ASK_DELAY = h.ASK_DELAY, UNTRUSTED = h.UNTRUSTED;

        // a row of choices (one is chosen): the chips; `off` marks the ones that cannot be chosen now
        function chipGroup(labelText, name, choices, value, onChange) {
            var f = el('div', 'f'), l = el('div', 'flab', labelText), row = el('div', 'chips bchips'), cur = value, off = {};
            l.id = name + '-l';
            row.setAttribute('role', 'radiogroup');
            row.setAttribute('aria-labelledby', l.id);
            function paint() {
                Array.prototype.slice.call(row.children).forEach(function (b, i) {
                    var v = choices[i][0];
                    b.className = 'chip' + (v === cur ? ' on' : '') + (off[v] ? ' dis' : '');
                    b.setAttribute('aria-checked', v === cur ? 'true' : 'false');
                    if (off[v]) b.setAttribute('aria-disabled', 'true'); else b.removeAttribute('aria-disabled');
                });
            }
            choices.forEach(function (c) {
                var b = el('button', 'chip', c[1]);
                b.type = 'button';
                b.setAttribute('role', 'radio');
                b.addEventListener('click', function () { if (off[c[0]] || cur === c[0]) return; cur = c[0]; paint(); onChange(cur); });
                add(row, b);
            });
            add(f, l, row);
            paint();
            return { box: f, label: l, get: function () { return cur; }, set: function (v) { cur = v; paint(); }, off: function (v, on) { off[v] = on; paint(); } };
        }
        // a number from min to max, with the buttons that take one away and add one
        function stepper(labelText, name, min, max, value, onChange) {
            var f = el('div', 'f'), l = el('div', 'flab', labelText), row = el('div', 'step'), cur = value;
            l.id = name + '-l';
            row.setAttribute('role', 'group');
            row.setAttribute('aria-labelledby', l.id);
            var out = el('output', 'stepv', String(cur));
            out.setAttribute('aria-live', 'polite');
            var minus = button('−', '', function () { go(cur - 1); }), plus = button('+', '', function () { go(cur + 1); });
            minus.setAttribute('aria-label', 'One less');
            plus.setAttribute('aria-label', 'One more');
            function go(n) { if (n < min || n > max) return; cur = n; out.textContent = String(cur); onChange(cur); }
            add(row, minus, out, plus);
            add(f, l, row);
            return { box: f, label: l, get: function () { return cur; }, set: function (n) { cur = n; out.textContent = String(cur); } };
        }
        function selectBox(id) {
            var w = el('div', 'sel'), s = el('select');
            s.id = id;
            add(w, s);
            return { wrap: w, select: s };
        }

        // The choices of one batch of bot matches, as Start now and a schedule both have them: { node, value(), set(v), onChange, setCountLabel }
        function choices(pre, maps, rotation, init, countLabel, countWords, randomMax) {
            var v = {}, listeners = [];
            Object.keys(init).forEach(function (k) { v[k] = k === 'levels' ? paddedLevels(init[k]) : init[k]; });
            var node = el('div');
            function changed() { sync(); listeners.forEach(function (fn) { fn(v); }); }
            var count = stepper(countLabel, pre + '-count', 1, CAP, v.count, function (n) { v.count = n; changed(); });
            var countHint = el('div', 'fh');
            add(count.box, countHint);

            var mapF = el('div', 'f'), mapL = el('label', null, 'Map'), mapS = selectBox(pre + '-map'), mapHint = el('div', 'fh');
            mapL.setAttribute('for', pre + '-map');
            RANDOM_MAPS.forEach(function (r) { var o = el('option', null, r[1]); o.value = r[0]; add(mapS.select, o); });
            GROUPS.forEach(function (g) {
                var og = el('optgroup'), any = false;
                og.label = g[1];
                maps.forEach(function (m) {
                    if (m.group !== g[0]) return;
                    var o = el('option', null, m.group === 'players' ? m.name + ' (' + m.width + ' x ' + m.height + ')' : m.name);
                    if (m.group === 'players') o.setAttribute('data-untrusted', UNTRUSTED);
                    o.value = m.file;
                    add(og, o);
                    any = true;
                });
                if (any) add(mapS.select, og);
            });
            mapS.select.value = v.map;
            mapS.select.addEventListener('change', function () { v.map = mapS.select.value; changed(); });
            add(mapF, mapL, mapS.wrap, mapHint);

            var rules = chipGroup('Rules', pre + '-rules', RULES, v.rules, function (x) { v.rules = x; changed(); });
            var bots = chipGroup('Bots', pre + '-bots', COUNTS, v.bots, function (x) { v.bots = x; changed(); });
            var botsHint = el('div', 'fh');
            add(bots.box, botsHint);

            var lvF = el('div', 'f'), lvL = el('div', 'flab', 'Level of each bot'), lvBox = el('div', 'lvbox'), lvHint = el('div', 'fh'), lvSel = [];
            for (var i = 0; i < 4; i++) {
                (function (i) {
                    var row = el('div', 'lv'), lab = el('label', 'ln', 'Bot ' + (i + 1)), s = selectBox(pre + '-lv' + i);
                    lab.setAttribute('for', pre + '-lv' + i);
                    LEVELS.forEach(function (c) { var o = el('option', null, c[1]); o.value = c[0]; add(s.select, o); });
                    s.select.value = v.levels[i];
                    s.select.addEventListener('change', function () { v.levels[i] = s.select.value; changed(); });
                    add(row, lab, s.wrap);
                    lvSel.push(row);
                    add(lvBox, row);
                })(i);
            }
            add(lvF, lvL, lvBox, lvHint);

            var teams = chipGroup('Teams', pre + '-teams', TEAMS, v.teams, function (x) { v.teams = x; changed(); });
            var splitF = el('div', 'f'), splitS = selectBox(pre + '-split'), splitL = el('label', 'sr', 'Which split'), teamHint = el('div', 'fh');
            splitL.setAttribute('for', pre + '-split');
            add(splitF, splitL, splitS.wrap);
            splitS.select.addEventListener('change', function () { v.split = splitS.select.value; changed(); });
            add(teams.box, teamHint);

            add(node, count.box, mapF, rules.box, bots.box, lvF, teams.box, splitF);

            function nbots() { return v.bots === 'random' ? 4 : parseInt(v.bots, 10); }
            function mapOf(file) { for (var k = 0; k < maps.length; k++) if (maps[k].file === file) return maps[k]; return null; }
            function sync() {
                var nb = nbots(), mp = mapOf(v.map);
                count.set(v.count);
                countHint.textContent = countWords;
                mapS.select.value = v.map;
                mapHint.className = 'fh';
                if (mp && mp.file === 'ISLANDS.LVL' && v.rules === '187') { mapHint.textContent = 'ISLANDS is not played in 187: its water joins no two hills, so nobody could be struck. Choose another map or the rules Highest score.'; mapHint.className = 'fh bad'; }
                else if (mp) mapHint.textContent = mp.name + ' plays ' + (mp.minutes ? 'up to ' + plural(mp.minutes, 'minute', 'minutes') : 'its own time limit') + '.' + (mp.places > 1 ? ' It is a big map: each match on it counts as ' + mp.places + ' places of the server.' : '') + (mp.random === false ? ' A random pick never takes it (over ' + (randomMax || 30) + ' minutes); only a choice that names it does.' : '');
                else if (v.map === 'random:rotation') mapHint.textContent = 'The rotation: ' + (rotation['highest-score'] || 0) + ' maps for Highest score, ' + (rotation['187'] || 0) + ' for 187.';
                else if (v.map === 'random:original') mapHint.textContent = 'TINY, SMALL, MEDIUM, GAUNTLET, TREASURE and ISLANDS (ISLANDS only in Highest score).';
                else if (v.map === 'random:players') {
                    var mine = maps.filter(function (m) { return m.group === 'players'; }), picked = mine.filter(function (m) { return m.random !== false; }).length;
                    mapHint.textContent = 'The maps of the players that you approved: ' + mine.length + ' now' + (picked < mine.length ? ', ' + picked + ' of them for a random pick (the others play over ' + (randomMax || 30) + ' minutes: name one to play it)' : '') + '.';
                }
                else mapHint.textContent = '';
                botsHint.textContent = v.bots === 'random' ? 'A random number from 2 to 4, on random seats.' : 'On random seats of the four.';
                lvSel.forEach(function (row, i) { row.hidden = i >= nb; row.querySelector('select').value = v.levels[i]; });
                lvHint.textContent = v.bots === 'random' ? 'Bots 3 and 4 play only when there are that many bots.' : '';
                lvHint.hidden = !lvHint.textContent;
                var can = nb >= 3 && v.bots !== 'random';
                teams.off('split', !can);
                if (!can && v.teams === 'split') { v.teams = 'random'; teams.set('random'); }
                teamHint.textContent = v.bots === 'random' ? 'A split needs a number of bots: choose 2, 3 or 4.' : nb === 2 ? 'Two bots can only play free for all.' : v.teams === 'random' ? 'Free for all or any split, each as likely as another.' : '';
                teamHint.hidden = !teamHint.textContent;
                splitF.hidden = !(v.teams === 'split' && can);
                if (can) {
                    var list = SPLITS[nb];
                    if (!v.split || !list.some(function (x) { return x[0] === v.split; }) || splitS.select.getAttribute('data-n') !== String(nb)) {
                        splitS.select.textContent = '';
                        list.forEach(function (x) { var o = el('option', null, x[1]); o.value = x[0]; add(splitS.select, o); });
                        splitS.select.setAttribute('data-n', String(nb));
                        if (!list.some(function (x) { return x[0] === v.split; })) v.split = list[0][0];
                    }
                    splitS.select.value = v.split;
                }
            }
            sync();
            return {
                node: node,
                value: function () { var o = {}; Object.keys(v).forEach(function (k) { o[k] = k === 'levels' ? v.levels.slice(0, nbots()) : v[k]; }); if (o.teams !== 'split') o.split = ''; return o; },
                set: function (w) { Object.keys(w).forEach(function (k) { v[k] = k === 'levels' ? paddedLevels(w[k]) : w[k]; }); rules.set(v.rules); bots.set(v.bots); teams.set(v.teams); sync(); },
                onChange: function (fn) { listeners.push(fn); },
                setCountLabel: function (t) { count.label.textContent = t; }
            };
        }

        function render(host) {
            var st = { matches: [], schedules: null, maps: [], rotation: {}, randomMax: 30, scheduling: true, places: null, replays: true, off: false, busy: false, saving: false, keyOff: null, editing: null, loaded: false, dead: false, capShown: false };
            h.setCleanup(function () { st.dead = true; if (st.keyOff) { st.keyOff(); st.keyOff = null; } });
            var startMsg = el('div'), runBox = el('div'), schedBox = el('div');

            // ---- Start now ----
            var now = panel('Start now', 'Starts bot matches at once, each in a room of its own on the live list. They play at the normal speed, 6 to 20 minutes, and are kept as replays.');
            now.className += ' b-now';
            var nowForm = el('div'), startBtn = null, picked = null;
            add(now, nowForm);

            function startLabel() { var n = picked ? picked.value().count : 1; return 'Start ' + plural(n, 'bot match', 'bot matches'); }
            function drawNow() {
                nowForm.textContent = '';
                picked = choices('now', st.maps, st.rotation, defaults(), 'Matches to start', CAP + ' at the most, with the ones that run now.', st.randomMax);
                startBtn = button(startLabel(), 'big ok', function () { start(); });
                picked.onChange(function () { startBtn.textContent = startLabel(); });
                add(nowForm, picked.node, startMsg, add(el('div', 'startrow'), startBtn));
                paintStart();
            }
            function paintStart() {
                var full = st.matches.length >= CAP, off = full || !st.replays;
                startBtn.disabled = off;
                startBtn.className = 'btn big ok' + (off ? ' dis' : '');
                if (!st.replays) { say('bad', [NO_REPLAYS_LINE]); st.capShown = false; }
                else if (full && !startMsg.firstChild) { say('bad', ['All ' + CAP + ' places for bot matches are taken. Stop one, or wait until one ends.']); st.capShown = true; }
                else if (!full && st.capShown) { say('bad', null); st.capShown = false; }
            }
            function say(cls, parts) {
                startMsg.textContent = '';
                if (!parts) return;
                var m = message(null, cls);
                parts.forEach(function (n) { add(m, typeof n === 'string' ? doc.createTextNode(n) : n); });
                add(startMsg, m);
            }
            // why a batch started fewer matches than it asked for (the server says it in stopped_for)
            function stoppedWords(why) {
                if (why === 'places') return 'The rest would take more places than bot matches may have.';
                if (why === 'busy') return 'People’s matches began to use most of the server’s places, so the rest were not started.';
                if (why === 'cap') return 'The limit of ' + CAP + ' bot matches at once was reached, so the rest were not started.';
                if (why === 'budget') return 'The store has no room left this hour for more recordings of bot matches, so the rest were not started.';
                return 'The rest could not be started.';
            }
            function failure(r) {
                var e = r.json && r.json.error, j = r.json || {};
                if (r.status === 409 && e === 'cap') return ['Not started: ' + plural(j.running, 'bot match runs', 'bot matches run') + ' and ' + plural(j.asked, 'more would', 'more would') + ' be over the limit of ' + CAP + '. Start ' + j.free + ' or fewer, or stop some first.'];
                if (r.status === 409 && e === 'busy') return ['Not started: people’s matches use ' + j.people + ' of the server’s ' + j.total + ' places. Bot matches wait, so that people are not slowed. Try again in a few minutes.'];
                if (r.status === 409 && e === 'places') return ['Not started: these matches would take more places than bot matches may have (' + j.used + ' of ' + j.max + ' are taken). Choose a smaller map or fewer matches, or stop some first.'];
                if (r.status === 409 && e === 'map_187') return ['Not started: ISLANDS is not played in 187 (nobody could be struck there). Choose another map or the rules Highest score.'];
                if (r.status === 409 && e === 'no_map') return ['Not started: that map cannot be played now. Choose another.'];
                if (r.status === 409 && e === 'budget') return ['Not started: the store keeps only ' + (j.per_hour || 30) + ' recordings of bot matches an hour, and that many are kept or on their way. A match that would not be kept is not played. Try again later.'];
                if (r.status === 503 && e === 'no_replays') return ['Not started: the server keeps no replays, and a match that nobody could look at again is not played.'];
                if (r.status === 503 && e === 'bots_off') return [OFF_LINE];
                if (r.status === 403 && e === 'csrf') return ['The page’s token is old. Reload the page.'];
                if (r.status === 429) return ['Too many requests. Wait a few seconds and try again.'];
                return ['That did not work. Reload the page.'];
            }
            function start() {
                if (st.busy) return;
                var body = picked.value();
                st.busy = true;
                call('POST', '/admin/api/bot-matches', body).then(function (r) {
                    st.busy = false;
                    if (st.dead) return;
                    if (r.status === 200) {
                        var j = r.json || {}, n = j.started || 0;
                        say('ok', [(j.asked && n < j.asked ? 'Started ' + n + ' of ' + plural(j.asked, 'bot match', 'bot matches') + '. ' + stoppedWords(j.stopped_for) + ' ' : 'Started ' + plural(n, 'bot match', 'bot matches') + '. ') + 'They show on the live list in about 30 seconds.']);
                        return loadAll(true);
                    }
                    if (refused(r)) return;
                    say('bad', failure(r));
                }, function () { st.busy = false; say('bad', ['The server did not answer. Try again.']); });
            }

            // ---- Running ----
            var run = panel('Running', 'Matches of computer players on the server now. People’s matches are not listed here.');
            run.className += ' b-run';
            add(run, runBox);
            function seatsOf(m) {
                var box = el('div', 'seats');
                m.seats.forEach(function (s) { var sp = el('span'); add(sp, el('i', 'bdot c' + s.seat), doc.createTextNode((SEATS[s.seat] || '') + ' ' + (LEVEL_WORD[s.level] || s.level))); add(box, sp); });
                return box;
            }
            function drawRun() {
                runBox.textContent = '';
                var n = st.matches.length, pct = Math.min(100, Math.round(n * 100 / CAP));
                var head = el('div', 'bhead'), cap = el('span', 'cap'), refresh = button('Refresh', 'sm', function () { loadAll(true); });
                add(cap, el('b', null, String(n)), doc.createTextNode(' of ' + CAP + ' places taken'));
                add(head, cap, refresh);
                var meter = el('div', 'meter'), fill = el('i', n >= CAP ? 'b' : n >= 6 ? 'w' : '');
                fill.style.width = pct + '%';
                meter.setAttribute('role', 'img');
                meter.setAttribute('aria-label', n + ' of ' + CAP);
                add(meter, fill);
                add(runBox, head, meter);
                var title = run.querySelector('h2'), old = title.querySelector('.n');
                if (old) title.removeChild(old);
                add(title, el('span', 'n', '(' + n + ')'));
                if (!n) add(runBox, el('p', 'none', 'No bot match is running.'));
                else {
                    var list = el('div', 'blist');
                    st.matches.forEach(function (m) {
                        var r = el('div', 'brow'), l = el('div'), name = el('div', 'bn');
                        add(name, typed('span', null, m.name), el('span', 'rtag ' + (m.rules === '187' ? 'k187' : 'hs'), m.rules === '187' ? '187' : 'Highest score'), m.teams !== 'ffa' ? el('span', 'rtag team', 'Teams') : null, m.live ? null : el('span', 'tg warn', 'Starting'));
                        var bs = el('div', 'bs');
                        add(bs, doc.createTextNode(sidesWords(m.teams) + ' · started ' + when(m.started, env.now()) + ' (' + minutesAgo(m.started, env.now()) + ' min ago) · '), m.schedule ? doc.createTextNode('schedule ') : doc.createTextNode('Start now by '), typed('span', null, m.schedule || m.by));
                        add(l, name, seatsOf(m), bs);
                        var bb = el('div', 'bb'), watch;
                        if (m.live) { watch = el('a', 'btn', 'Watch'); watch.setAttribute('href', '/play?live=' + encodeURIComponent(m.live)); watch.setAttribute('target', '_blank'); watch.setAttribute('rel', 'noopener'); }
                        else { watch = button('Watch', 'dis', null); watch.disabled = true; watch.setAttribute('title', 'It can be watched when the match has begun, in about 30 seconds.'); }
                        add(bb, watch, button('Stop', 'bad', function () { stopOne(m); }));
                        add(r, l, bb);
                        add(list, r);
                    });
                    add(runBox, list, add(el('div', 'stopall'), button('Stop all ' + n, 'bad', function () { askStopAll(); })));
                }
                var pl = st.places;
                add(runBox, el('div', 'note', 'The limit is ' + CAP + ' bot matches at once, from the schedules and from Start now together. A start is also refused while people’s matches use most of the server' + (pl ? ': ' + pl.people + ' of its ' + pl.total + ' places now' : '') + '. A big map counts as more places.'));
                h.setRunning(n);
            }
            function stopOne(m) {
                if (st.busy) return;
                st.busy = true;
                call('POST', '/admin/api/bot-matches/' + encodeURIComponent(m.id) + '/stop', {}).then(function (r) {
                    st.busy = false;
                    if (st.dead) return;
                    if (r.status === 200 || (r.status === 404 && r.json && r.json.error === 'gone')) return loadAll(true);
                    if (refused(r)) return;
                    toast('That did not work. Reload the page.');
                }, function () { st.busy = false; toast('The server did not answer. Try again.'); });
            }
            // a question before something that cannot be undone: the box of the page's own questions
            function ask(title, lead, goLabel, goClass, onGo) {
                if (st.keyOff) return;
                var dlg = el('div', 'dlg');
                dlg.setAttribute('role', 'dialog');
                dlg.setAttribute('aria-modal', 'true');
                dlg.setAttribute('aria-labelledby', 'dlg-title');
                var hd = el('h2', null);
                hd.id = 'dlg-title';
                add(hd, typeof title === 'string' ? doc.createTextNode(title) : title);
                add(dlg, add(el('div', 'dlg-in'), hd, el('div', 'lead', lead)));
                var shell, opened = tick();
                function closeAsk() {
                    if (st.keyOff) { st.keyOff(); st.keyOff = null; }
                    if (shell.node.parentNode) shell.node.parentNode.removeChild(shell.node);
                    clearInert();
                }
                var cancel = button('Cancel', 'ghost', closeAsk);
                var go = button(goLabel, goClass, function () { if (tick() - opened < ASK_DELAY) return; closeAsk(); onGo(); });
                add(dlg, add(el('div', 'dlg-foot'), cancel, go));
                shell = questionBox(dlg, cancel, go, closeAsk);
                add(host, shell.node);
                inertOthers(shell.node);
                doc.addEventListener('keydown', shell.onKey, true);
                st.keyOff = function () { doc.removeEventListener('keydown', shell.onKey, true); };
                shell.focus.focus();
            }
            function askStopAll() {
                var n = st.matches.length;
                ask('Stop all ' + plural(n, 'bot match', 'bot matches') + '?', 'Every bot match on the server ends now, the ones that you started and the ones that a schedule started. A match that has run for 30 seconds is kept as a replay up to where it stopped. The schedules go on.', 'Stop all ' + n, 'bad', function () {
                    call('POST', '/admin/api/bot-matches/stop-all', {}).then(function (r) {
                        if (st.dead) return;
                        if (refused(r)) return;
                        if (r.status === 200) { say('ok', ['Stopped ' + plural(r.json.stopped, 'bot match', 'bot matches') + '.']); loadAll(true); }
                        else toast('That did not work. Reload the page.');
                    }, function () { toast('The server did not answer. Try again.'); });
                });
            }

            // ---- Schedules ----
            var sched = panel('Schedules', 'A schedule starts bot matches by itself. Times are Pacific time.');
            sched.className += ' b-sched';
            add(sched, schedBox);
            function whenWords(s) {
                if (s.when.kind === 'every') return 'Every ' + minutesWords(s.when.minutes);
                var t = s.when.times.slice().sort();
                return 'Every day at ' + (t.length > 1 ? t.slice(0, -1).join(', ') + ' and ' + t[t.length - 1] : t[0]) + ' (Pacific time)';
            }
            function nextWords(s) {
                if (!st.scheduling) return 'Not running: the server’s settings switch every schedule off' + (s.last ? ' · last start ' + when(s.last, env.now()) : '');
                if (!s.on) return 'Off: it starts nothing' + (s.last ? ' · last start ' + when(s.last, env.now()) : '');
                return 'Next start ' + untilWords(s.next, env.now()) + (s.last ? ' · last start ' + when(s.last, env.now()) : ' · no start yet');
            }
            function sendSchedule(s, patch) {
                if (st.switching) return;
                st.switching = true;
                call('POST', '/admin/api/bot-schedules/' + s.id, patch).then(function (r) {
                    st.switching = false;
                    if (st.dead) return;
                    if (r.status === 200) return loadAll(true);
                    if (refused(r)) return;
                    toast('That did not work. Reload the page.');
                }, function () { st.switching = false; toast('The server did not answer. Try again.'); });
            }
            function drawSched() {
                if (st.editing && schedBox.querySelector('.sform')) return;                       // (a form that is open keeps what was typed when the lists are asked again)
                schedBox.textContent = '';
                var list = st.schedules;
                var title = sched.querySelector('h2'), old = title.querySelector('.n');
                if (old) title.removeChild(old);
                if (list === null) { add(schedBox, message('The schedules could not be loaded.')); return; }
                add(title, el('span', 'n', '(' + list.length + ')'));
                if (st.editing) { add(schedBox, scheduleForm(st.editing)); return; }
                if (!st.scheduling) add(schedBox, message('Scheduled bot matches are switched off on this server: its settings say so (the operator set ANTS_BOT_MATCH_EVERY_MIN to 0). The schedules below are kept and can be edited, but none of them starts a match. Start now still works.'));
                if (!list.length) add(schedBox, el('p', 'none', 'No schedule. Nothing starts by itself.'));
                else {
                    var box = el('div', 'blist');
                    list.forEach(function (s) {
                        var r = el('div', 'brow' + (s.on ? '' : ' off')), l = el('div'), name = el('div', 'bn');
                        add(name, typed('span', null, s.name), el('span', 'tg ' + (s.on ? 'active' : 'mute'), s.on ? 'On' : 'Off'), s.default ? el('span', 'rtag team', 'Default') : null);
                        add(l, name, el('div', 'bs cream', whenWords(s)), el('div', 'bs', summary(s.what, st.maps)), el('div', 'bs', nextWords(s)));
                        var bb = el('div', 'bb');
                        add(bb, button(s.on ? 'Turn off' : 'Turn on', '', function () { sendSchedule(s, { on: !s.on }); }), button('Edit', '', function () { st.editing = s; drawSched(); }), button('Delete', 'bad', function () { askDelete(s); }));
                        add(r, l, bb);
                        add(box, r);
                    });
                    add(schedBox, box);
                }
                add(schedBox, add(el('div', 'sfoot'), button('Add a schedule', 'big', function () { st.editing = { id: null, name: 'Schedule ' + (list.length + 1), on: true, when: { kind: 'every', minutes: 60 }, what: defaults() }; drawSched(); })));
            }
            function askDelete(s) {
                var t = el('span');
                add(t, doc.createTextNode('Delete '), typed('span', null, s.name), doc.createTextNode('?'));
                ask(t, 'It starts nothing from now on.' + (s.default ? ' This is the schedule that the server began with (' + whenWords(s).toLowerCase() + '): until you add another, nothing starts by itself.' : '') + ' Matches that it started and that still run go on.', 'Delete', 'bad', function () {
                    call('POST', '/admin/api/bot-schedules/' + s.id + '/delete', {}).then(function (r) {
                        if (st.dead) return;
                        if (r.status === 200 || (r.status === 404 && r.json && r.json.error === 'gone')) return loadAll(true);
                        if (refused(r)) return;
                        toast('That did not work. Reload the page.');
                    }, function () { toast('The server did not answer. Try again.'); });
                });
            }
            function scheduleForm(s) {
                var form = el('div', 'sform'), msgBox = el('div'), timeMsg = el('div'), kind = s.when.kind, minutes = s.when.minutes || 60, minBox = null, times = (s.when.times || ['18:00']).slice(), on = s.on;
                add(form, el('h3', 'shead', s.id === null ? 'New schedule' : 'Edit schedule'));
                var nameF = el('div', 'f'), nameL = el('label', null, 'Name (only for you)'), nameI = el('input', 'tinput');
                nameL.setAttribute('for', 'sc-name'); nameI.id = 'sc-name'; nameI.type = 'text'; nameI.maxLength = 40; nameI.value = s.name; nameI.setAttribute('autocomplete', 'off');
                add(nameF, nameL, add(el('div', 'inw'), nameI));
                var whenG = chipGroup('When', 'sc-when', [['every', 'Every N minutes'], ['times', 'At times of the day']], kind, function (x) { kind = x; drawWhen(); });
                var whenBox = el('div');
                function drawWhen() {
                    whenBox.textContent = '';
                    if (kind === 'every') {
                        var f = el('div', 'f'), l = el('label', null, 'Minutes between two starts'), inw = el('div', 'inw num'), i = el('input', 'tinput'), pre = el('span', 'chips');
                        l.setAttribute('for', 'sc-min'); i.id = 'sc-min'; i.type = 'text'; i.value = String(minutes); i.setAttribute('inputmode', 'numeric'); i.setAttribute('autocomplete', 'off');
                        minBox = i;                                                                              // (read when Save is pressed: whatever is in the box, typed, pasted or filled in)
                        [['15 min', 15], ['30 min', 30], ['1 hour', 60], ['2 hours', 120], ['6 hours', 360]].forEach(function (p) { var c = el('button', 'chip', p[0]); c.type = 'button'; c.addEventListener('click', function () { minutes = p[1]; i.value = String(p[1]); }); add(pre, c); });
                        add(inw, i);
                        add(f, l, add(el('div', 'minrow'), inw, pre), el('div', 'fh', '5 to 1440 (a day). Counted from the last start, so a restart of the server does not start one at once.'));
                        add(whenBox, f);
                    } else {
                        var f2 = el('div', 'f'), l2 = el('div', 'flab', 'Times of the day (Pacific time, 24 hours)'), tl = el('div', 'times');
                        times.sort();
                        times.forEach(function (t) {
                            var c = el('span', 'tm2'), x = el('button', null, '×');
                            x.type = 'button'; x.setAttribute('aria-label', 'Remove ' + t);
                            x.addEventListener('click', function () { times = times.filter(function (y) { return y !== t; }); drawWhen(); });
                            add(c, doc.createTextNode(t), x);
                            add(tl, c);
                        });
                        if (!times.length) add(tl, el('p', 'none', 'No time yet.'));
                        var addRow = el('div', 'addtime'), inw2 = el('div', 'inw'), ti = el('input', 'tinput');
                        ti.id = 'sc-time'; ti.type = 'text'; ti.setAttribute('placeholder', '18:30'); ti.setAttribute('inputmode', 'numeric'); ti.setAttribute('autocomplete', 'off'); ti.setAttribute('aria-label', 'A time to add, like 18:30');
                        add(inw2, ti);
                        add(addRow, inw2, button('Add time', '', function () {
                            var m = /^([01]?[0-9]|2[0-3]):([0-5][0-9])$/.exec(ti.value.replace(/^\s+|\s+$/g, ''));
                            timeMsg.textContent = '';
                            if (!m) { add(timeMsg, message('A time looks like 18:30 (24 hours).', 'bad')); return; }
                            var t = (m[1].length === 1 ? '0' : '') + m[1] + ':' + m[2];
                            if (times.indexOf(t) < 0) {
                                if (times.length >= 12) { add(timeMsg, message('At most 12 times.', 'bad')); return; }
                                times.push(t);
                            }
                            drawWhen();
                        }));
                        add(f2, l2, tl, addRow, timeMsg, el('div', 'fh', 'Every day, at the clock of Pacific time: the change between standard and daylight time is followed. At most 12 times.'));
                        add(whenBox, f2);
                    }
                }
                drawWhen();
                function sayForm(cls, parts) { msgBox.textContent = ''; if (!parts) return; var m = message(null, cls); parts.forEach(function (n) { add(m, doc.createTextNode(n)); }); add(msgBox, m); }
                var what = choices('sc', st.maps, st.rotation, s.what, 'Matches each time', '1 to ' + CAP + '. When fewer places are free at that time, it starts as many as fit.', st.randomMax);
                var onG = chipGroup('The schedule is', 'sc-on', [['on', 'On'], ['off', 'Off']], on ? 'on' : 'off', function (x) { on = x === 'on'; });
                var save = button('Save', 'big ok', function () {
                    if (st.saving) return;                                                                     // (a second click while the first is on its way is the same save, not another schedule)
                    var typedMinutes = kind === 'every' ? String(minBox ? minBox.value : minutes).replace(/^\s+|\s+$/g, '') : '';
                    var body = { name: nameI.value, on: on, when: kind === 'every' ? { kind: 'every', minutes: /^[0-9]{1,4}$/.test(typedMinutes) ? parseInt(typedMinutes, 10) : -1 } : { kind: 'times', times: times }, what: what.value() };
                    st.saving = true;
                    save.disabled = true;
                    call('POST', '/admin/api/bot-schedules' + (s.id === null ? '' : '/' + s.id), body).then(function (r) {
                        st.saving = false;
                        if (st.dead) return;
                        save.disabled = false;
                        if (r.status === 200) { st.editing = null; return loadAll(true); }
                        if (refused(r)) return;
                        var e = r.json && r.json.error;
                        sayForm('bad', [e === 'bad_every' ? 'Every N minutes: a whole number from 5 to 1440.' : e === 'no_times' ? 'Add at least one time of the day.' : e === 'bad_name' ? 'Give the schedule a name of 1 to 40 characters.' : e === 'too_many' ? 'There are 20 schedules already. Delete one first.' : e === 'gone' ? 'That schedule is not there any more. Cancel and reload the page.' : 'That did not work. Reload the page.']);
                    }, function () { st.saving = false; save.disabled = false; sayForm('bad', ['The server did not answer. Try again.']); });
                });
                add(form, nameF, whenG.box, whenBox, el('hr'), el('div', 'flab', 'What it starts'), what.node, el('hr'), onG.box, msgBox,
                    add(el('div', 'sfoot'), button('Cancel', 'ghost', function () { st.editing = null; drawSched(); }), save));
                return form;
            }

            add(host, el('p', 'b-lead', 'Start bot matches now or by a schedule. They run on the game server like the matches of people, so they have a limit: ' + CAP + ' at once.'));
            var left = el('div', 'cc'), right = el('div', 'cc');
            add(left, now);
            add(right, run, sched);
            add(host, add(el('div', 'bgrid'), left, right));

            function loadAll(keepForm) {
                return Promise.all([call('GET', '/admin/api/bot-matches'), call('GET', '/admin/api/bot-options'), call('GET', '/admin/api/bot-schedules')]).then(function (rs) {
                    if (st.dead) return;
                    for (var i = 0; i < rs.length; i++) if (refused(rs[i])) return;
                    if (rs[0].status === 503 && rs[0].json && rs[0].json.error === 'bots_off') {            // (a server that was given no way to run them: one line instead of the panels)
                        host.textContent = '';
                        add(host, message(OFF_LINE));
                        h.setRunning(0);
                        return;
                    }
                    st.matches = rs[0].status === 200 ? rs[0].json.matches || [] : [];
                    st.places = rs[0].status === 200 ? rs[0].json.places || null : null;
                    st.replays = rs[0].status === 200 ? rs[0].json.replays !== false : true;
                    st.maps = rs[1].status === 200 ? rs[1].json.maps || [] : [];
                    st.rotation = rs[1].status === 200 ? rs[1].json.rotation || {} : {};
                    st.randomMax = rs[1].status === 200 && rs[1].json.random_max_minutes ? rs[1].json.random_max_minutes : 30;
                    st.schedules = rs[2].status === 200 ? rs[2].json.schedules || [] : null;
                    st.scheduling = rs[2].status === 200 ? rs[2].json.scheduling !== false : true;
                    if (!st.loaded) { st.loaded = true; drawNow(); } else if (!keepForm) drawNow();
                    else paintStart();
                    drawRun();
                    drawSched();
                }, function () { toast('The server did not answer. Reload the page.'); });
            }
            return loadAll(false);
        }

        // the number of matches that go, for the badge of the tab: asked once when the page opens, and again by every draw of the Running panel
        function count() {
            return call('GET', '/admin/api/bot-matches').then(function (r) {
                if (r.status === 200 && r.json && r.json.matches) h.setRunning(r.json.matches.length);
            }, function () {});
        }

        return { render: render, count: count };
    }

    // the words of the Log for a line about the matches (the page's Log has the rest)
    var LOG_ACTIONS = { bot_started: 'Started bot matches: ', bot_stopped: 'Stopped the bot match on ', bot_stopped_all: 'Stopped all bot matches: ', bot_schedule_added: 'Added the schedule ', bot_schedule_changed: 'Changed the schedule ', bot_schedule_on: 'Turned on the schedule ', bot_schedule_off: 'Turned off the schedule ', bot_schedule_deleted: 'Deleted the schedule ' };

    return { make: make, logActions: LOG_ACTIONS, sidesWords: sidesWords, summary: summary };
});
