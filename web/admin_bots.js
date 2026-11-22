// The section "Bot matches" of the admin page (docs/MAP_SUBMISSIONS.md, "Bot matches on the admin page"; the rules of the matches and of the schedules are docs/SERVER_REPLAYS.md): start matches of computer players now (all at
// once or one every few minutes), see and stop the ones that go and the starts that wait, edit the schedules that start them by themselves, and edit the two lists of the rotation that their random choice draws from. The map
// of a batch is chosen in the list of the server's maps (admin_map_pick.js, loaded before this file). It is the page's second file (admin_page.js would pass its size otherwise) and is loaded before it: the page hands this file
// its own helpers (the one object `h` of make()), so a word is made and a call is sent in one way in both. Every word that comes from the server (a schedule's name, the name of a map of a player, the admin who started a match) goes in as
// text (`typed`: marked data-untrusted), never as markup. ES5 and no module system, as the page: a browser gets the global AntsAdminBots, node gets module.exports (tests/scripts/web_admin_check.js runs it with the page).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsAdminBots = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var SCHEDULE_MAX = 8;                                       // the most matches that one run of a schedule starts (a schedule keeps its own limit; the server's kBotScheduleMax says it too)
    var COUNT_MAX = 10000;                                      // the most matches that one start asks for, and that wait in all (the server says both in `queue_max`)
    var CAP_FALLBACK = 80, KEEP_PER_HOUR = 30;                  // the number that run at once until the server has said (`cap`), and the replays of bot matches that the store keeps an hour (docs/SERVER_REPLAYS.md)
    var SHOWN = 5;                                              // the running matches that the list shows before its button "Show all"
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
    var RULES_NAME = { 'highest-score': 'Highest score', '187': '187' };
    var ROT_KEYS = ['highest-score', '187'];
    var EVERY_PRESETS = [[0, 'All at once'], [1, '1 min'], [2, '2 min'], [3, '3 min'], [5, '5 min'], [10, '10 min']];
    var EVERY_MAX_MIN = 120;                                    // the longest spacing of a batch, in minutes (the server's kBotSpacingMaxS says it too)
    var CATALOG_STALE_MS = 60000;                               // the list of the server's maps is asked again when it is older than this and a box wants it
    var CHECK_AGAIN_MS = 2000;                                  // while the server has not judged every map yet, the page asks again at this interval
    var RUN_AGAIN_MS = 20000;                                   // while a queue has starts to come, the Running list is asked again at this interval
    var OFF_LINE = 'Bot matches are not available on this server.';
    var NO_REPLAYS_LINE = 'This server keeps no replays, so it plays no bot matches: a match that nobody could look at again is not played.';

    function defaults() { return { count: 1, every_s: 0, map: 'random:rotation', rules: 'random', bots: 'random', levels: ['rmh', 'rmh', 'rmh', 'rmh'], teams: 'random', split: '' }; }
    // the levels of four bots, whatever the list that came has (a schedule that was saved with two or three bots may carry only those): a level that is missing or unknown is the draw of Medium or Hard
    function paddedLevels(list) {
        var out = [], known = LEVELS.map(function (c) { return c[0]; });
        for (var i = 0; i < 4; i++) out.push(list && known.indexOf(list[i]) >= 0 ? list[i] : 'rmh');
        return out;
    }
    function plural(n, one, many) { return thousands(n) + ' ' + (n === 1 ? one : many); }
    function minutesAgo(seconds, nowMs) { return Math.max(0, Math.floor((nowMs / 1000 - seconds) / 60)); }
    // the spacing of a batch in words: "one every 3 minutes", "one every 1.5 minutes", "one every 1 minute"
    function spacingWords(everyS) { var m = Math.floor(everyS / 60), half = everyS % 60 !== 0; return 'one every ' + m + (half ? '.5' : '') + (m === 1 && !half ? ' minute' : ' minutes'); }
    // a time span of whole or half minutes in words: "12 minutes", "1.5 minutes", "30 seconds"
    function inWords(sec) { var m = Math.floor(sec / 60), half = sec % 60 !== 0; return m === 0 ? sec + ' seconds' : m + (half ? '.5' : '') + (m === 1 && !half ? ' minute' : ' minutes'); }
    // when the next start of a queue comes, from now (wall-clock seconds of the server): "next start in 6 min", "next start in 4 s", what holds it back, or that it comes as soon as a match ends
    function nextStart(q, nowMs) {
        var left = q.at - nowMs / 1000;
        if (q.held) return 'its time has come: it waits for a free place';
        if (!(q.at > 0)) return 'the next one starts as soon as one ends';
        if (left <= 0) return 'next start now';
        return left < 60 ? 'next start in ' + Math.ceil(left) + ' s' : 'next start in ' + Math.ceil(left / 60) + ' min';
    }
    // a number in the thousands: "1,000"
    function thousands(n) { return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }
    // what the box of the spacing holds, in seconds: whole or half minutes from 0 to 120 (a comma is a point too); -1 for anything else
    function everySeconds(text) {
        var t = String(text).replace(/^\s+|\s+$/g, '').replace(',', '.');
        if (!/^[0-9]{1,3}(\.[0-9]{1,2})?$/.test(t)) return -1;
        var s = Math.round(parseFloat(t) * 60);
        return s % 30 === 0 && s <= EVERY_MAX_MIN * 60 && Math.abs(parseFloat(t) * 60 - s) < 0.001 ? s : -1;
    }
    function sidesWords(teams) {                               // "ffa", or the sides by seat: "03-12" is Green and Black against Red and Blue
        if (teams === 'ffa') return 'Free for all';
        return String(teams).split('-').map(function (side) { return side.split('').map(function (n) { return SEATS[parseInt(n, 10)] || ''; }).join(' and '); }).join(' against ');
    }

    // what a batch is, in words (one line of a schedule's row)
    function summary(w, maps) {
        var parts = [plural(w.count, 'match', 'matches') + (w.count > 1 && w.every_s ? ', ' + spacingWords(w.every_s) : '')], mp = null;
        maps.forEach(function (m) { if (m.file === w.map) mp = m; });
        parts.push(w.map === 'random:rotation' ? 'a random map of the rotation' : w.map === 'random:original' ? 'a random original map' : w.map === 'random:players' ? 'a random players’ map' : mp ? mp.name : w.map.replace(/\.lvl$/i, '') + ' (not on the server now)');
        parts.push(w.rules === 'random' ? 'random rules' : w.rules === '187' ? 'rules 187' : 'rules Highest score');
        parts.push(w.bots === 'random' ? '2 to 4 bots' : w.bots + ' bots');
        var lv = w.levels.slice(0, w.bots === 'random' ? 4 : parseInt(w.bots, 10)), same = lv.every(function (x) { return x === lv[0]; });
        parts.push(same ? 'levels ' + (lv[0] === 'rmh' || lv[0] === 'random' ? '' : 'all ') + LEVEL_WORD[lv[0]] : 'levels ' + lv.map(function (x) { return LEVEL_WORD[x]; }).join(', '));
        parts.push(w.teams === 'ffa' ? 'free for all' : w.teams === 'random' ? 'random teams' : (SPLITS[w.bots] || []).filter(function (x) { return x[0] === w.split; }).map(function (x) { return x[1].toLowerCase(); })[0] || 'a split');
        return parts.join(' · ');
    }

    function make(h) {
        var doc = h.doc, env = h.env, el = h.el, add = h.add, button = h.button, panel = h.panel, call = h.call, message = h.message, typed = h.typed, when = h.when, untilWords = h.untilWords;
        var minutesWords = h.minutesWords, refused = h.refused, toast = h.toast, questionBox = h.questionBox, inertOthers = h.inertOthers, clearInert = h.clearInert, tick = h.tick, ASK_DELAY = h.ASK_DELAY;
        var PICK = AntsAdminMapPick;                            // the list of the server's maps (web/admin_map_pick.js, loaded before this file: the page has no section Bot matches without it)
        var picker = PICK.make({ doc: doc, el: el, add: add, button: button, typed: typed, questionBox: questionBox, inertOthers: inertOthers, clearInert: clearInert });

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
        // a whole number from min to max in a box, with the chips that set it; an entry that is not one is refused in a line under the box, and the choice then has no count (-1: Start is off)
        function numberBox(labelText, name, min, max, value, onChange) {
            var f = el('div', 'f'), l = el('label', null, labelText), row = el('div', 'cnt'), inp = el('input', 'tinput'), chips = el('span', 'chips'), bad = el('div', 'fh bad'), cur = value;
            l.setAttribute('for', name);
            inp.id = name;
            inp.type = 'text';
            inp.setAttribute('inputmode', 'numeric');
            inp.setAttribute('autocomplete', 'off');
            inp.value = String(cur);
            bad.id = name + '-bad';
            bad.textContent = 'Use a whole number from ' + min + ' to ' + thousands(max) + '.';
            function paint() { Array.prototype.slice.call(chips.children).forEach(function (c) { c.setAttribute('aria-pressed', String(Number(c.getAttribute('data-n')) === cur)); }); }
            function line(on) {                                                           // (the refusal is in the page only while it is true)
                if (on && !bad.parentNode) { add(f, bad); inp.setAttribute('aria-describedby', bad.id); } else if (!on && bad.parentNode) { f.removeChild(bad); inp.removeAttribute('aria-describedby'); }
            }
            function take(n) { cur = n; inp.className = 'tinput' + (n < 0 ? ' err' : ''); line(n < 0); paint(); onChange(cur); }
            inp.addEventListener('input', function () { var t = inp.value.replace(/[ ,]/g, ''), n = /^[0-9]{1,6}$/.test(t) ? parseInt(t, 10) : -1; take(n >= min && n <= max ? n : -1); });
            [1, 10, 100, 1000].forEach(function (n) {
                var c = el('button', 'chip', thousands(n));
                c.type = 'button';
                c.setAttribute('data-n', String(n));
                c.addEventListener('click', function () { inp.value = String(n); take(n); });
                add(chips, c);
            });
            add(row, add(el('div', 'inw'), inp), chips);
            add(f, l, row);
            paint();
            return { box: f, label: l, get: function () { return cur; }, set: function (n) { if (n < 0 || n === cur) return; cur = n; inp.value = String(n); inp.className = 'tinput'; line(false); paint(); } };
        }
        function selectBox(id) {
            var w = el('div', 'sel'), s = el('select');
            s.id = id;
            add(w, s);
            return { wrap: w, select: s };
        }

        // The choices of one batch of bot matches, as Start now and a schedule both have them: { node, value(), set(v), onChange, setCountLabel, refresh }. `ctx` is what the section knows (the maps of the server and the
        // lists of the rotation, asked when they are needed: a call of the section can bring new ones while a form is open). `withSpacing` adds the field of the spacing: 'now' for Start now, 'run' for a schedule (its first match starts when it runs).
        function choices(pre, ctx, init, countLabel, countWords, withSpacing) {
            var v = {}, listeners = [];
            Object.keys(init).forEach(function (k) { v[k] = k === 'levels' ? paddedLevels(init[k]) : init[k]; });
            if (typeof v.every_s !== 'number') v.every_s = 0;
            var node = el('div'), first = withSpacing === 'run' ? 'when the schedule runs' : 'now';
            function changed() { sync(); listeners.forEach(function (fn) { fn(v); }); }
            var countChanged = function (n) { v.count = n; changed(); };
            var count = withSpacing === 'now' ? numberBox(countLabel, pre + '-count', 1, ctx.queueMax(), v.count, countChanged) : stepper(countLabel, pre + '-count', 1, SCHEDULE_MAX, v.count, countChanged);       // (Start now takes up to 10,000; a schedule keeps its own limit of 8)
            var countHint = el('div', 'fh');
            add(count.box, countHint);

            // one match every X minutes: the first now, the others one after another (shown when there is more than one match to space)
            var everyF = el('div', 'f'), everyL = el('label', null, 'Start one every'), everyRow = el('div', 'minrow'), everyHint = el('div', 'fh'), everyI = el('input', 'tinput'), everyChips = el('span', 'chips');
            everyL.setAttribute('for', pre + '-every');
            everyI.id = pre + '-every';
            everyI.type = 'text';
            everyI.value = v.every_s ? String(v.every_s / 60) : '0';
            everyI.setAttribute('inputmode', 'decimal');
            everyI.setAttribute('autocomplete', 'off');
            everyI.setAttribute('aria-describedby', pre + '-every-h');
            everyHint.id = pre + '-every-h';
            function everyTyped() { v.every_s = everySeconds(everyI.value); changed(); }
            everyI.addEventListener('input', everyTyped);
            EVERY_PRESETS.forEach(function (p) {
                var c = el('button', 'chip', p[1]);
                c.type = 'button';
                c.addEventListener('click', function () { everyI.value = String(p[0]); everyTyped(); });
                add(everyChips, c);
            });
            add(everyRow, add(el('div', 'inw num'), everyI), el('span', 'unit', 'minutes'), everyChips);
            add(everyF, everyL, everyRow, everyHint);

            var mapF = el('div', 'f mapf'), mapL = el('div', 'flab', 'Map'), mapBtn = el('button', 'mapbtn'), mapHint = el('div', 'fh');
            mapL.id = pre + '-map-l';
            mapBtn.id = pre + '-map';
            mapBtn.type = 'button';
            mapBtn.setAttribute('aria-haspopup', 'dialog');
            mapBtn.setAttribute('aria-expanded', 'false');
            mapBtn.setAttribute('aria-labelledby', pre + '-map-l ' + pre + '-map');
            function mapOf(file) { var maps = ctx.maps(); for (var k = 0; k < maps.length; k++) if (maps[k].file === file) return maps[k]; return null; }
            function rotationWords() { var r = ctx.rotation(); return (r['highest-score'] || 0) + ' maps for Highest score, ' + (r['187'] || 0) + ' for 187'; }
            function randomRows() {
                var mine = ctx.maps().filter(function (m) { return m.group === 'players' && m.playable !== false; }).length;
                return [['random:rotation', 'Random from the rotation', rotationWords()], ['random:original', 'Random from the original maps', 'TINY, SMALL, MEDIUM, GAUNTLET, TREASURE, ISLANDS'],
                    ['random:players', 'Random from the players’ maps', plural(mine, 'approved map', 'approved maps') + ' of the players']];
            }
            function paintMapBtn() {
                var r = randomRows().filter(function (x) { return x[0] === v.map; })[0], mp = mapOf(v.map), left = el('span', 'mbl');
                mapBtn.textContent = '';
                if (r) add(left, el('span', 'mn', r[1]), el('span', 'ms', r[2]));
                else if (mp) add(left, typed('span', 'mn', mp.name), el('span', 'ms', PICK.mapMeta(mp)));
                else add(left, typed('span', 'mn', v.map.replace(/\.lvl$/i, '')), el('span', 'ms bad', 'Not on the server now'));
                add(mapBtn, left, el('i', 'mcar'));
            }
            mapBtn.addEventListener('click', function () {
                ctx.ensure();
                mapBtn.setAttribute('aria-expanded', 'true');
                picker.open({
                    host: ctx.host, title: 'Choose a map', mode: 'choose', current: v.map, cap: ctx.cap(), randomMax: ctx.randomMax(), maps: ctx.maps, randoms: randomRows, rotFiles: ctx.rotFiles,
                    onPick: function (m) { v.map = m.file; picker.close(); changed(); },
                    onClose: function () { mapBtn.setAttribute('aria-expanded', 'false'); mapBtn.focus(); }
                });
            });
            add(mapF, mapL, mapBtn, mapHint);

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

            add(node, count.box, withSpacing ? everyF : null, mapF, rules.box, bots.box, lvF, teams.box, splitF);

            function nbots() { return v.bots === 'random' ? 4 : parseInt(v.bots, 10); }
            function mapWords(mp) {
                var randomMax = ctx.randomMax();
                return mp.name + ' plays ' + (mp.minutes ? 'up to ' + plural(mp.minutes, 'minute', 'minutes') : 'its own time limit') + '.' + (mp.places > 1 ? ' It is a big map: each match on it counts as ' + mp.places + ' places of the server.' : '') +
                    (mp.random === false ? ' A random pick never takes it (over ' + randomMax + ' minutes); only a choice that names it, or a rotation list that you put it in, does.' : '');
            }
            function sync() {
                var nb = nbots(), mp = mapOf(v.map), randomMax = ctx.randomMax();
                count.set(v.count);
                countHint.textContent = typeof countWords === 'function' ? countWords() : countWords;
                everyF.hidden = !(withSpacing && v.count > 1);
                everyHint.className = 'fh' + (v.every_s < 0 ? ' bad' : '');
                everyI.className = 'tinput' + (v.every_s < 0 ? ' err' : '');
                everyHint.textContent = v.every_s < 0 ? 'Whole or half minutes, from 0 to ' + EVERY_MAX_MIN + '.' : v.every_s === 0 ? 'All ' + thousands(v.count) + ' start as fast as the server has room. Give a time to start the first ' + first + ' and the others one after another.' :
                    'The first starts ' + first + ', then ' + spacingWords(v.every_s) + ': the last in ' + inWords((v.count - 1) * v.every_s) + '. A start that waits is lost if the server restarts' + (withSpacing === 'run' ? '.' : '; a schedule is kept.');
                paintMapBtn();
                mapHint.className = 'fh';
                if (mp && mp.file === 'ISLANDS.LVL' && v.rules === '187') { mapHint.textContent = 'ISLANDS is not played in 187: its water joins no two hills, so nobody could be struck. Choose another map or the rules Highest score.'; mapHint.className = 'fh bad'; }
                else if (mp && mp.playable === false) { mapHint.textContent = PICK.cannotWords(mp); mapHint.className = 'fh bad'; }
                else if (mp) mapHint.textContent = mapWords(mp);
                else if (v.map === 'random:rotation') mapHint.textContent = 'The rotation: ' + rotationWords() + '. You can change the lists under “Rotation”, lower on this page.';
                else if (v.map === 'random:original') mapHint.textContent = 'TINY, SMALL, MEDIUM, GAUNTLET, TREASURE and ISLANDS (ISLANDS only in Highest score).';
                else if (v.map === 'random:players') {
                    var mine = ctx.maps().filter(function (m) { return m.group === 'players' && m.playable !== false; }), picked = mine.filter(function (m) { return m.random !== false; }).length;
                    mapHint.textContent = 'The maps of the players that you approved: ' + mine.length + ' now' + (picked < mine.length ? ', ' + picked + ' of them for a random pick (the others play over ' + randomMax + ' minutes: name one to play it)' : '') + '.';
                }
                else { mapHint.textContent = 'This map is not on the server now, so a match cannot be started on it. Choose another.'; mapHint.className = 'fh bad'; }
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
            var handle = {
                node: node,
                value: function () { var o = {}; Object.keys(v).forEach(function (k) { o[k] = k === 'levels' ? v.levels.slice(0, nbots()) : v[k]; }); if (o.teams !== 'split') o.split = ''; if (withSpacing && v.count < 2) o.every_s = 0; return o; },
                set: function (w) { Object.keys(w).forEach(function (k) { v[k] = k === 'levels' ? paddedLevels(w[k]) : w[k]; }); everyI.value = v.every_s > 0 ? String(v.every_s / 60) : '0'; rules.set(v.rules); bots.set(v.bots); teams.set(v.teams); sync(); },
                onChange: function (fn) { listeners.push(fn); },
                setCountLabel: function (t) { count.label.textContent = t; },
                refresh: sync
            };
            ctx.live[pre] = handle;
            return handle;
        }

        function render(host) {
            var st = { matches: [], queues: [], cap: CAP_FALLBACK, queueMax: COUNT_MAX, showAll: false, schedules: null, maps: [], checking: 0, randomMax: 30, catAt: -1, catBusy: null, checkTimer: 0, checkTries: 0, runTimer: 0, rotLists: null, rotDefaults: {}, rotMax: 100, rotFiles: {}, draft: {}, dirty: {}, rotMsg: null, rotBusy: false,
                scheduling: true, places: null, replays: true, off: false, busy: false, saving: false, keyOff: null, editing: null, loaded: false, dead: false, noReplaysShown: false };
            h.setCleanup(function () {
                st.dead = true;
                picker.close();
                if (st.keyOff) { st.keyOff(); st.keyOff = null; }
                if (env.clearTimeout) { if (st.checkTimer) env.clearTimeout(st.checkTimer); if (st.runTimer) env.clearTimeout(st.runTimer); }
            });
            var startMsg = el('div'), runBox = el('div'), schedBox = el('div');
            var ctx = {                                                   // (what the forms and the lists ask the section when they need it)
                host: host, live: {},
                cap: function () { return st.cap; },
                queueMax: function () { return st.queueMax; },
                maps: function () { return st.maps; },
                randomMax: function () { return st.randomMax; },
                rotFiles: function () { return st.rotFiles; },
                rotation: function () { var o = {}; ROT_KEYS.forEach(function (k) { o[k] = ((st.rotLists && st.rotLists[k]) || []).filter(function (e) { return e.state !== 'skipped'; }).length; }); return o; },
                ensure: function () { if (env.now() - st.catAt > CATALOG_STALE_MS) loadCatalog(); }          // (a box that opens finds a list that is at most a minute old)
            };

            // ---- Start now ----
            var now = panel('Start now', 'Starts bot matches at once or one after another, each in a room of its own on the live list. They play at the normal speed, as long as the map’s own time limit. A few of them an hour are kept as replays.');
            now.className += ' b-now';
            var nowForm = el('div'), startBtn = null, picked = null;
            add(now, nowForm);

            function startLabel() { var n = picked ? picked.value().count : 1; return n < 1 ? 'Start bot matches' : 'Start ' + n + (n === 1 ? ' bot match' : ' bot matches'); }
            // what Start now explains under the number: how many run at once, that the rest wait, that people come first, and how many are kept as replays
            function nowCountWords() {
                return 'Any number up to ' + thousands(st.queueMax) + '. ' + st.cap + ' run at once; the rest wait and start as each one ends, and people’s matches always come first. ' + KEEP_PER_HOUR + ' an hour are kept as replays; the others are played but not kept (watch them live while they run).';
            }
            function drawNow() {
                nowForm.textContent = '';
                picked = choices('now', ctx, defaults(), 'Matches to start', nowCountWords, 'now');
                startBtn = button(startLabel(), 'big ok', function () { start(); });
                picked.onChange(function () { startBtn.textContent = startLabel(); paintStart(); });
                add(nowForm, picked.node, startMsg, add(el('div', 'startrow'), startBtn));
                paintStart();
            }
            function paintStart() {                                                       // Start is never "full": a start that finds the places taken is queued by the server; it is off for a count that is not a number and for a server that keeps no replays
                var off = picked.value().count < 1 || !st.replays;
                startBtn.disabled = off;
                startBtn.className = 'btn big ok' + (off ? ' dis' : '');
                picked.refresh();                                                         // (the words under the number name the limit that the server last said)
                if (!st.replays) { say('bad', [NO_REPLAYS_LINE]); st.noReplaysShown = true; }
                else if (st.noReplaysShown) { say('bad', null); st.noReplaysShown = false; }
            }
            function say(cls, parts) {
                startMsg.textContent = '';
                if (!parts) return;
                var m = message(null, cls);
                parts.forEach(function (n) { add(m, typeof n === 'string' ? doc.createTextNode(n) : n); });
                add(startMsg, m);
            }
            function failure(r) {
                var e = r.json && r.json.error, j = r.json || {};
                if (r.status === 409 && e === 'queue_full') return ['Not started: ' + plural(j.asked, 'more start', 'more starts') + ' would wait with the ' + thousands(j.waiting) + ' that wait already, and the server holds ' + thousands(j.max) + ' at the most. Stop the rest of a batch first, or choose fewer matches.'];
                if (r.status === 409 && e === 'map_187') return ['Not started: ISLANDS is not played in 187 (nobody could be struck there). Choose another map or the rules Highest score.'];
                if (r.status === 409 && e === 'no_map') return ['Not started: that map cannot be played now. Choose another.'];
                if (r.status === 400 && e === 'bad_count') return ['Not started: use a whole number of matches from 1 to ' + thousands(st.queueMax) + '.'];
                if (r.status === 400 && e === 'bad_spacing') return ['Not started: start one every 0 to ' + EVERY_MAX_MIN + ' minutes, in whole or half minutes.'];
                if (r.status === 503 && e === 'no_replays') return ['Not started: the server keeps no replays, and a match that nobody could look at again is not played.'];
                if (r.status === 503 && e === 'bots_off') return [OFF_LINE];
                if (r.status === 403 && e === 'csrf') return ['The page’s token is old. Reload the page.'];
                if (r.status === 429) return ['Too many requests. Wait a few seconds and try again.'];
                return ['That did not work. Reload the page.'];
            }
            // why the first start of a batch had to wait, in the server's word
            function heldWords(why) {
                if (why === 'places') return 'the server has no places left for more bot matches';
                if (why === 'people') return 'people’s matches are using the server';
                if (why === 'busy') return 'the server is busy';
                return 'the limit of ' + st.cap + ' bot matches at once is reached';
            }
            // what the page says of a batch that was started: the matches that go now and the starts that wait (a spacing, or a place that must come free)
            function startedWords(j) {
                var n = j.started || 0, q = j.queued || 0, asked = j.asked || n + q, tail = ' They show on the live list in a few seconds.';
                if (!q) return 'Started ' + plural(n, 'bot match', 'bot matches') + '.' + tail;
                if (!n) return 'Queued ' + plural(asked, 'bot match', 'bot matches') + ': none could start yet, because ' + heldWords(j.held) + '. They start as soon as there is room. A queue is lost if the server restarts.';
                if (j.every_s) return 'Started ' + plural(n, 'bot match', 'bot matches') + ' now. ' + plural(q, 'more waits', 'more wait') + ': ' + spacingWords(j.every_s) + ', the last in ' + inWords(q * j.every_s) + '. A queue is lost if the server restarts.' + tail;
                return 'Started ' + plural(n, 'bot match', 'bot matches') + ' now. ' + thousands(q) + ' more wait' + (q === 1 ? 's' : '') + ' and start as each one ends. A queue is lost if the server restarts.' + tail;
            }
            function start() {
                if (st.busy) return;
                var body = picked.value();
                if (body.count < 1) return;
                st.busy = true;
                call('POST', '/admin/api/bot-matches', body).then(function (r) {
                    st.busy = false;
                    if (st.dead) return;
                    if (r.status === 200) {
                        say('ok', [startedWords(r.json || {})]);
                        return loadAll(true);
                    }
                    if (refused(r)) return;
                    say('bad', failure(r));
                }, function () { st.busy = false; say('bad', ['The server did not answer. Try again.']); });
            }

            // ---- Running ----
            var run = panel('Running', 'Matches of computer players on the server now, and the starts that wait for their time. People’s matches are not listed here.');
            run.className += ' b-run';
            add(run, runBox);
            function seatsOf(m) {
                var box = el('div', 'seats');
                m.seats.forEach(function (s) { var sp = el('span'); add(sp, el('i', 'bdot c' + s.seat), doc.createTextNode((SEATS[s.seat] || '') + ' ' + (LEVEL_WORD[s.level] || s.level))); add(box, sp); });
                return box;
            }
            function remaining() { return st.queues.reduce(function (a, q) { return a + q.remaining; }, 0); }       // (the starts that wait, in all the queues)
            // one row for a whole batch: its bar (how many have ended of all), what its numbers are, when the next one starts, and Stop the rest (the starts that wait; what runs goes on)
            function queueRow(q) {
                var r = el('div', 'brow bq'), l = el('div'), name = el('div', 'bn'), bar = el('div', 'meter'), fill = el('i'), bs = el('div', 'bs');
                fill.style.width = (q.total ? Math.min(100, Math.round(q.finished * 100 / q.total)) : 0) + '%';
                bar.setAttribute('role', 'img');
                bar.setAttribute('aria-label', thousands(q.finished) + ' of ' + thousands(q.total) + ' done');
                add(bar, fill);
                add(name, el('span', null, 'Batch of ' + plural(q.total, 'match', 'matches')), el('span', 'rtag team', thousands(q.remaining) + ' waiting'), q.held ? el('span', 'tg warn', 'Waiting for room') : null);
                add(bs, doc.createTextNode(thousands(q.finished) + ' done, ' + thousands(q.running) + ' running, ' + thousands(q.kept) + ' kept as replays · ' + nextStart(q, env.now()) + ' · ' + (q.schedule ? 'schedule ' : 'Start now by ')), typed('span', null, q.schedule || q.by), q.name ? doc.createTextNode(' · map ') : null, q.name ? typed('span', null, q.name) : null);
                add(l, name, bar, bs);
                var stop = button('Stop the rest', 'bad', function () { stopQueue(q); });
                stop.setAttribute('aria-label', 'Stop the rest of the batch of ' + plural(q.total, 'match', 'matches'));
                add(r, l, add(el('div', 'bb'), stop));
                return r;
            }
            function drawRun() {
                runBox.textContent = '';
                var n = st.matches.length, k = remaining(), pct = Math.min(100, Math.round(n * 100 / st.cap));
                var head = el('div', 'bhead'), cap = el('span', 'cap'), refresh = button('Refresh', 'sm', function () { loadAll(true); });
                add(cap, el('b', null, String(n)), doc.createTextNode(' running, ' + st.cap + ' at once at the most'));
                add(head, cap, refresh);
                var meter = el('div', 'meter'), fill = el('i', n >= st.cap ? 'w' : '');
                fill.style.width = pct + '%';
                meter.setAttribute('role', 'img');
                meter.setAttribute('aria-label', n + ' of ' + st.cap);
                add(meter, fill);
                add(runBox, head, meter);
                var title = run.querySelector('h2'), old = title.querySelector('.n');
                if (old) title.removeChild(old);
                add(title, el('span', 'n', '(' + n + ')'));
                st.queues.forEach(function (q) { add(runBox, queueRow(q)); });
                if (!n) add(runBox, el('p', 'none', 'No bot match is running.'));
                else {
                    var list = el('div', 'blist');
                    st.matches.slice(0, st.showAll ? st.matches.length : SHOWN).forEach(function (m) {
                        var r = el('div', 'brow'), l = el('div'), name = el('div', 'bn');
                        add(name, typed('span', null, m.name), el('span', 'rtag ' + (m.rules === '187' ? 'k187' : 'hs'), m.rules === '187' ? '187' : 'Highest score'), m.teams !== 'ffa' ? el('span', 'rtag team', 'Teams') : null, m.live ? null : el('span', 'tg warn', 'Starting'));
                        var bs = el('div', 'bs');
                        add(bs, doc.createTextNode(sidesWords(m.teams) + ' · started ' + when(m.started, env.now()) + ' (' + minutesAgo(m.started, env.now()) + ' min ago) · '), m.schedule ? doc.createTextNode('schedule ') : doc.createTextNode('Start now by '), typed('span', null, m.schedule || m.by));
                        add(l, name, seatsOf(m), bs);
                        var bb = el('div', 'bb'), watch;
                        if (m.live) { watch = el('a', 'btn', 'Watch'); watch.setAttribute('href', '/play?live=' + encodeURIComponent(m.live)); watch.setAttribute('target', '_blank'); watch.setAttribute('rel', 'noopener'); }
                        else { watch = button('Watch', 'dis', null); watch.disabled = true; watch.setAttribute('title', 'It can be watched as soon as the match has begun, in a few seconds.'); }
                        add(bb, watch, button('Stop', 'bad', function () { stopOne(m); }));
                        add(r, l, bb);
                        add(list, r);
                    });
                    add(runBox, list);
                    if (n > SHOWN && !st.showAll) add(runBox, add(el('div', 'stopall'), button('Show all ' + thousands(n) + ' running matches', 'sm', function () { st.showAll = true; drawRun(); })));
                }
                if (n || k) add(runBox, add(el('div', 'stopall'), button('Stop all ' + thousands(n + k), 'bad', function () { askStopAll(); })));
                var pl = st.places;
                add(runBox, el('div', 'note', 'Up to ' + st.cap + ' bot matches run at once, from the schedules and Start now together; more wait in a queue of ' + thousands(st.queueMax) + ' at the most. People’s matches come first' + (pl ? ': ' + pl.people + ' of the server’s ' + pl.total + ' places are theirs now' : '') + '. A big map counts as more places. A queue is lost if the server restarts: a schedule is not.'));
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
            function stopQueue(q) {                                                       // the starts that wait of one batch are cancelled; the matches that run go on
                if (st.busy) return;
                st.busy = true;
                call('POST', '/admin/api/bot-matches/' + encodeURIComponent(q.id) + '/stop', {}).then(function (r) {
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
                add(dlg, add(el('div', 'dlg-in'), hd, typeof lead === 'string' ? el('div', 'lead', lead) : lead));
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
                var n = st.matches.length, k = remaining();
                var what = n && k ? plural(n, 'bot match', 'bot matches') + ' and ' + plural(k, 'waiting start', 'waiting starts') : k ? plural(k, 'waiting start', 'waiting starts') : plural(n, 'bot match', 'bot matches');
                ask('Stop all ' + what + '?', (n ? 'Every bot match on the server ends now, the ones that you started and the ones that a schedule started. A match that has run for 30 seconds is kept as a replay up to where it stopped. ' : '') + (k ? 'Every start that waits for its time is cancelled, so the batch ends. ' : '') + 'The schedules go on.', 'Stop all ' + (n + k), 'bad', function () {
                    call('POST', '/admin/api/bot-matches/stop-all', {}).then(function (r) {
                        if (st.dead) return;
                        if (refused(r)) return;
                        if (r.status === 200) { var d = r.json.dropped || 0; say('ok', ['Stopped ' + plural(r.json.stopped, 'bot match', 'bot matches') + (d ? ' and cancelled ' + plural(d, 'waiting start', 'waiting starts') : '') + '.']); loadAll(true); }
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
            // a schedule that names a map that the server does not have any more: each run is skipped until another map is chosen (not known while the list of the server's maps has not come)
            function mapGone(s) { return st.maps.length > 0 && s.what.map.indexOf('random:') !== 0 && !st.maps.some(function (x) { return x.file === s.what.map; }); }
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
                        var r = el('div', 'brow' + (s.on ? '' : ' off')), l = el('div'), name = el('div', 'bn'), gone = mapGone(s);
                        add(name, typed('span', null, s.name), el('span', 'tg ' + (s.on ? 'active' : 'mute'), s.on ? 'On' : 'Off'), s.default ? el('span', 'rtag team', 'Default') : null, gone ? el('span', 'tg warn', 'Map not on the server') : null);
                        add(l, name, el('div', 'bs cream', whenWords(s)), el('div', 'bs', summary(s.what, st.maps)), el('div', 'bs', nextWords(s)), gone ? el('div', 'mr', 'The map is not on the server now, so each run is skipped until you choose another map.') : null);
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
                ask(t, 'It starts nothing from now on.' + (s.default ? ' This is the schedule that the server began with (' + whenWords(s).toLowerCase() + '): until you add another, nothing starts by itself.' : '') + ' Matches that it started and that still run go on.' + (s.what.count > 1 && s.what.every_s ? ' Its starts that still wait go on too.' : ''), 'Delete', 'bad', function () {
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
                var what = choices('sc', ctx, s.what, 'Matches each time', '1 to ' + SCHEDULE_MAX + ': a schedule keeps its own limit. What does not fit at that time waits in the queue, as any start does.', 'run');
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
                        sayForm('bad', [e === 'bad_spacing' ? 'Start one every: 0 to ' + EVERY_MAX_MIN + ' minutes, in whole or half minutes.' : e === 'bad_every' ? 'Every N minutes: a whole number from 5 to 1440.' : e === 'no_times' ? 'Add at least one time of the day.' : e === 'bad_name' ? 'Give the schedule a name of 1 to 40 characters.' : e === 'too_many' ? 'There are 20 schedules already. Delete one first.' : e === 'gone' ? 'That schedule is not there any more. Cancel and reload the page.' : 'That did not work. Reload the page.']);
                    }, function () { st.saving = false; save.disabled = false; sayForm('bad', ['The server did not answer. Try again.']); });
                });
                add(form, nameF, whenG.box, whenBox, el('hr'), el('div', 'flab', 'What it starts'), what.node, el('hr'), onG.box, msgBox,
                    add(el('div', 'sfoot'), button('Cancel', 'ghost', function () { st.editing = null; drawSched(); }), save));
                return form;
            }

            // ---- Rotation: the two lists that "Random from the rotation" draws from ----
            var rot = panel('Rotation', 'The maps that “Random from the rotation” draws from: in Start now, in the schedules, and for the first match after the server starts. One list for each rules; each map of a list is as likely as any other.');
            rot.className += ' b-rot';
            var rotBox = el('div');
            add(rot, rotBox);
            function clone(o) { return JSON.parse(JSON.stringify(o)); }
            function fileSet(list) { var o = {}; list.forEach(function (e) { o[e.file] = true; }); return o; }
            function filesOf(list) { return list.map(function (e) { return e.file; }); }
            function sameFiles(a, b) { return JSON.stringify(filesOf(a)) === JSON.stringify(filesOf(b)); }          // (the same maps in the same order, which is the order of the draws)
            function listDirty(key) { return !!st.dirty[key]; }
            function anyDirty() { return ROT_KEYS.some(listDirty); }
            function namesOf(list) { return list.map(function (e) { return e.name; }).join(', '); }
            function skipWords(e) {
                if (e.reason === 'taken down') return 'Taken down, so it is skipped. Put it back on the Maps page, or remove it here.';
                if (e.reason === 'not on this server') return 'It is not on this server now, so it is skipped. Remove it here.';
                return 'Skipped: ' + e.reason + '.';
            }
            function refreshRotFiles() { st.rotFiles = {}; ROT_KEYS.forEach(function (k) { st.rotFiles[k] = fileSet(st.rotLists ? st.rotLists[k] || [] : []); }); }
            function rotEntryOf(m) { return { file: m.file, name: m.name, group: m.group, width: m.width, height: m.height, players: m.players, minutes: m.minutes, places: m.places, random: m.random, state: 'ok', isNew: true }; }
            function rotMsg(cls, text) { st.rotMsg = function () { return message(text, cls); }; }

            function rotRow(key, e) {
                var r = el('div', 'rrow' + (e.state === 'skipped' ? ' skip' : '') + (e.isNew ? ' new' : '')), left = el('div'), nm = el('div', 'rnm');
                add(nm, typed('span', null, e.name));
                if (e.isNew) add(nm, el('span', 'tg active', 'Added'));
                if (e.state === 'skipped') add(nm, el('span', 'tg bad', 'Skipped now'));
                else if (e.state === 'checking') add(nm, el('span', 'tg mute', 'Checking'));
                else if (e.random === false) add(nm, el('span', 'tg warn', 'Over ' + st.randomMax + ' min'));
                if (e.places > 1) add(nm, el('span', 'rtag team', e.places + ' places'));
                add(left, nm, e.width ? el('div', 'bs', PICK.mapMeta(e)) : null);          // (a map that the server has lost has no size to tell)
                if (e.state === 'skipped') add(left, el('div', 'mr', skipWords(e)));
                var rm = button('Remove', '', function () { removeFrom(key, e); });
                rm.setAttribute('aria-label', 'Remove ' + e.name + ' from ' + RULES_NAME[key]);
                add(r, left, rm);
                return r;
            }
            function rotColumn(key) {
                var col = el('div', 'rcol'), list = st.draft[key], def = st.rotDefaults[key] || [], head = el('div', 'rhead'), usable = list.filter(function (e) { return e.state !== 'skipped'; }).length;
                var isDef = sameFiles(list, def);
                add(head, el('h3', null, RULES_NAME[key]), el('span', 'tg ' + (isDef ? 'mute' : 'warn'), isDef ? 'Defaults' : 'Edited'), el('span', 'n', plural(list.length, 'map', 'maps')));
                add(col, head, el('div', 'fh rsub', !list.length ? 'No map.' : usable === list.length ? 'Draws from all ' + list.length + ' maps.' : 'Draws from ' + usable + ' of ' + list.length + ' maps now. The others are skipped, and are drawn again if they come back.'));
                if (!list.length) add(col, message('Add at least one map: a list with no map would never be drawn, so it cannot be saved.'));
                else { var box = el('div', 'blist'); list.forEach(function (e) { add(box, rotRow(key, e)); }); add(col, box); }
                var again = button('Back to the defaults', 'ghost', function () { askDefaults(key); });
                if (isDef && !listDirty(key)) { again.disabled = true; again.className += ' dis'; again.textContent = 'These are the defaults'; }
                var full = list.length >= st.rotMax, addBtn = button('Add a map', 'big', function () { addMap(key); });
                if (full) { addBtn.disabled = true; addBtn.className += ' dis'; addBtn.setAttribute('title', 'A list holds ' + st.rotMax + ' maps at the most.'); }
                add(col, add(el('div', 'sfoot rfoot'), addBtn, again));
                return col;
            }
            function drawRot() {
                if (st.dead) return;
                rotBox.textContent = '';
                if (!st.rotLists) { add(rotBox, message('The rotation could not be loaded.')); return; }
                var grid = el('div', 'rgrid');
                ROT_KEYS.forEach(function (k) { add(grid, rotColumn(k)); });
                add(rotBox, grid);
                if (st.rotMsg) add(rotBox, st.rotMsg());
                if (anyDirty()) {
                    var bar = el('div', 'rsave'), words = [];
                    ROT_KEYS.forEach(function (k) {
                        if (!listDirty(k)) return;
                        var a = st.draft[k].filter(function (e) { return e.isNew; }).length, gone = st.rotLists[k].filter(function (e) { return !fileSet(st.draft[k])[e.file]; }).length;
                        words.push(RULES_NAME[k] + ': ' + (a ? plural(a, 'map', 'maps') + ' added' : '') + (a && gone ? ', ' : '') + (gone ? plural(gone, 'map', 'maps') + ' removed' : '') + (!a && !gone ? 'changed' : ''));
                    });
                    var empty = ROT_KEYS.some(function (k) { return listDirty(k) && !st.draft[k].length; });
                    var save = button('Save the rotation', 'big ok', function () { saveRot(); });
                    if (empty || st.rotBusy) { save.disabled = true; save.className += ' dis'; }
                    add(bar, el('div', 'rsw', 'Not saved yet. ' + words.join(' · ') + '.'), add(el('div', 'sfoot'), button('Discard these changes', 'ghost', function () { discardRot(); }), save));
                    add(rotBox, bar);
                }
                add(rotBox, el('div', 'note', 'The lists are not used by “Random from the original maps” or “Random from the players’ maps”. A list may hold up to ' + st.rotMax + ' maps. The server keeps the lists, and every change is in the Log.'));
            }
            function removeFrom(key, e) {
                st.draft[key] = st.draft[key].filter(function (x) { return x.file !== e.file; });
                st.dirty[key] = !(sameFiles(st.draft[key], st.rotLists[key]) && !st.draft[key].some(function (x) { return x.isNew; }));
                st.rotMsg = null;
                drawRot();
            }
            function discardRot() {
                st.draft = clone(st.rotLists);
                st.dirty = {};
                st.rotMsg = null;
                drawRot();
            }
            function rotFailure(r, key, failedFile) {
                var e = r.json && r.json.error, j = r.json || {};
                if (r.status === 400 && e === 'empty') return RULES_NAME[key] + ' would have no map. A list needs at least one: add another before you save.';
                if (r.status === 400 && e === 'too_many') return 'A list holds ' + (j.max || st.rotMax) + ' maps at the most.';
                if (r.status === 400 && e === 'bad_map') return 'One of the maps is not on the server any more. Remove it from the list and save again.';
                if (r.status === 409 && e === 'no_map') { var name = String(j.file || failedFile || 'A map').replace(/\.lvl$/i, ''); return 'Not saved: ' + name + ' cannot be played now' + (j.why ? ' (' + j.why + ')' : '') + '. Remove it from the list, or choose another map.'; }
                if (r.status === 503 && e === 'db') return 'The server’s database did not take the list. Try again.';
                if (r.status === 403 && e === 'csrf') return 'The page’s token is old. Reload the page.';
                if (r.status === 429) return 'Too many requests. Wait a few seconds and try again.';
                return 'That did not work. Reload the page.';
            }
            // the lists that were changed are sent one after another, each whole (the file names, in the order of the draws); what the server answers is read back
            function saveRot() {
                if (st.rotBusy) return;
                var keys = ROT_KEYS.filter(listDirty);
                st.rotBusy = true;
                function one(i) {
                    if (i >= keys.length) return Promise.resolve(true);
                    var k = keys[i];
                    return call('POST', '/admin/api/bot-rotation', { rules: k, maps: filesOf(st.draft[k]) }).then(function (r) {
                        if (st.dead) return false;
                        if (refused(r)) return false;
                        if (r.status !== 200) { rotMsg('bad', rotFailure(r, k)); return false; }
                        st.dirty[k] = false;
                        return one(i + 1);
                    });
                }
                one(0).then(function (ok) {
                    if (st.dead) return;
                    return loadRotation().then(function () {
                        st.rotBusy = false;
                        if (ok) rotMsg('ok', 'Saved. The next match that draws from the rotation uses these lists.');
                        drawRot();
                    });
                }, function () { st.rotBusy = false; rotMsg('bad', 'The server did not answer. Try again.'); drawRot(); });
            }
            // the box that adds maps: the list of the server's maps, which stays open until Done (a list can take several maps in one visit); nothing is sent until Save
            function addMap(key) {
                ctx.ensure();
                picker.open({
                    host: host, title: 'Add a map to ' + RULES_NAME[key], mode: 'add', rules: key, cap: st.cap, randomMax: st.randomMax, doneLabel: 'Done', maps: ctx.maps, rotFiles: ctx.rotFiles,
                    isIn: function (file) { return st.draft[key].some(function (e) { return e.file === file; }); },
                    onPick: function (m) {
                        st.draft[key].push(rotEntryOf(m));
                        st.dirty[key] = true;
                        st.rotMsg = null;
                        drawRot();
                    },
                    onClose: function () { var b = rotBox.querySelector('.rfoot .btn'); if (b && !b.disabled) b.focus(); }
                });
            }
            function askDefaults(key) {
                var saved = st.rotLists[key], def = st.rotDefaults[key] || [], now = fileSet(saved), was = fileSet(def);
                var gone = saved.filter(function (e) { return !was[e.file]; }), back = def.filter(function (e) { return !now[e.file]; });
                var lead = el('div', 'lead dflt');
                add(lead, el('p', null, RULES_NAME[key] + ' goes back to the ' + def.length + ' maps that the server came with.'), el('p', 'dl', namesOf(def)));
                if (gone.length) add(lead, el('p', null, 'These leave the list (you added them): ' + namesOf(gone) + '.'));
                if (back.length) add(lead, el('p', null, 'These come back (you took them out): ' + namesOf(back) + '.'));
                if (listDirty(key)) add(lead, el('p', null, 'The changes to this list that are not saved yet are dropped too.'));
                add(lead, el('p', 'dl', 'It is saved at once. The other list, the schedules and the matches that go are not touched.'));
                ask('Put ' + RULES_NAME[key] + ' back to its defaults?', lead, 'Back to the defaults', 'ok', function () {
                    call('POST', '/admin/api/bot-rotation/reset', { rules: key }).then(function (r) {
                        if (st.dead) return;
                        if (refused(r)) return;
                        if (r.status !== 200) { rotMsg('bad', rotFailure(r, key)); drawRot(); return; }
                        st.dirty[key] = false;
                        return loadRotation().then(function () { rotMsg('ok', RULES_NAME[key] + ' is back to its ' + def.length + ' default maps.'); drawRot(); });
                    }, function () { rotMsg('bad', 'The server did not answer. Try again.'); drawRot(); });
                });
            }

            add(host, el('p', 'b-lead', 'Start bot matches now or by a schedule. They run on the game server like the matches of people, so only so many run at once: the rest wait in a queue, and people’s matches come first.'));
            var left = el('div', 'cc'), right = el('div', 'cc');
            add(left, now);
            add(right, run, sched);
            add(host, add(el('div', 'bgrid'), left, right));
            add(host, rot);

            // ---- what the server says ----
            function applyOptions(r) {
                st.maps = r.status === 200 ? r.json.maps || [] : [];
                st.checking = r.status === 200 ? r.json.checking || 0 : 0;
                st.randomMax = r.status === 200 && r.json.random_max_minutes ? r.json.random_max_minutes : 30;
            }
            // the lists of the rotation: a list that has unsaved changes keeps them; the others are the server's
            function applyRotation(r) {
                if (r.status !== 200 || !r.json || !r.json.lists) { st.rotLists = null; return; }
                var checking = r.json.checking || 0;
                st.rotLists = r.json.lists;
                st.rotDefaults = r.json.defaults || {};
                st.rotMax = r.json.max || 100;
                st.checking = Math.max(st.checking, checking);
                refreshRotFiles();
                ROT_KEYS.forEach(function (k) { if (!st.draft[k] || !st.dirty[k]) st.draft[k] = clone(st.rotLists[k] || []); });
            }
            // the maps that the server has not judged yet are asked about again in a moment (the call judges a few of them each time), until it has them all
            function afterCatalog() {
                st.catAt = env.now();
                if (st.checking <= 0) st.checkTries = 0;
                else if (!st.checkTimer && !st.dead && env.setTimeout && st.checkTries < 30) {
                    st.checkTries++;
                    st.checkTimer = env.setTimeout(function () { st.checkTimer = 0; if (!st.dead) loadCatalog(); }, CHECK_AGAIN_MS);
                }
                Object.keys(ctx.live).forEach(function (k) { ctx.live[k].refresh(); });
                picker.refresh();
            }
            function loadCatalog() {
                if (st.catBusy) return st.catBusy;
                st.catBusy = Promise.all([call('GET', '/admin/api/bot-options'), call('GET', '/admin/api/bot-rotation')]).then(function (rs) {
                    st.catBusy = null;
                    if (st.dead) return false;
                    for (var i = 0; i < rs.length; i++) if (refused(rs[i])) return false;
                    applyOptions(rs[0]);
                    applyRotation(rs[1]);
                    afterCatalog();
                    drawRot();
                    drawSched();
                    return true;
                }, function () { st.catBusy = null; return false; });
                return st.catBusy;
            }
            function loadRotation() {
                return call('GET', '/admin/api/bot-rotation').then(function (r) {
                    if (st.dead || refused(r)) return;
                    applyRotation(r);
                    picker.refresh();
                    Object.keys(ctx.live).forEach(function (k) { ctx.live[k].refresh(); });
                }, function () {});
            }
            // while starts wait for their time the Running list is asked again now and then, so that a start that came shows as a match that goes
            function planRun() {
                if (!remaining() || st.runTimer || st.dead || !env.setTimeout) return;
                st.runTimer = env.setTimeout(function () { st.runTimer = 0; if (!st.dead) loadAll(true); }, RUN_AGAIN_MS);
            }

            function loadAll(keepForm) {
                var cat = !st.loaded || env.now() - st.catAt > CATALOG_STALE_MS;
                var calls = [call('GET', '/admin/api/bot-matches')];
                if (cat) calls.push(call('GET', '/admin/api/bot-options'));
                calls.push(call('GET', '/admin/api/bot-schedules'));
                if (cat) calls.push(call('GET', '/admin/api/bot-rotation'));
                return Promise.all(calls).then(function (rs) {
                    if (st.dead) return;
                    for (var i = 0; i < rs.length; i++) if (refused(rs[i])) return;
                    var matches = rs[0], schedules = rs[cat ? 2 : 1];
                    if (matches.status === 503 && matches.json && matches.json.error === 'bots_off') {            // (a server that was given no way to run them: one line instead of the panels)
                        host.textContent = '';
                        add(host, message(OFF_LINE));
                        h.setRunning(0);
                        return;
                    }
                    st.matches = matches.status === 200 ? matches.json.matches || [] : [];
                    st.queues = matches.status === 200 ? matches.json.queues || [] : [];
                    st.cap = matches.status === 200 && matches.json.cap > 0 ? matches.json.cap : CAP_FALLBACK;
                    st.queueMax = matches.status === 200 && matches.json.queue_max > 0 ? matches.json.queue_max : COUNT_MAX;
                    st.places = matches.status === 200 ? matches.json.places || null : null;
                    st.replays = matches.status === 200 ? matches.json.replays !== false : true;
                    if (cat) { applyOptions(rs[1]); applyRotation(rs[3]); afterCatalog(); }
                    st.schedules = schedules.status === 200 ? schedules.json.schedules || [] : null;
                    st.scheduling = schedules.status === 200 ? schedules.json.scheduling !== false : true;
                    if (!st.loaded) { st.loaded = true; drawNow(); } else if (!keepForm) drawNow();
                    else paintStart();
                    drawRun();
                    drawSched();
                    drawRot();
                    planRun();
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
    var LOG_ACTIONS = { bot_started: 'Started bot matches: ', bot_stopped: 'Stopped the bot match on ', bot_waiting_stopped: 'Cancelled a waiting start on ', bot_stopped_all: 'Stopped all bot matches: ', bot_schedule_added: 'Added the schedule ', bot_schedule_changed: 'Changed the schedule ', bot_schedule_on: 'Turned on the schedule ',
        bot_schedule_off: 'Turned off the schedule ', bot_schedule_deleted: 'Deleted the schedule ', bot_rotation_changed: 'Changed the rotation list ', bot_rotation_reset: 'Put the rotation list back to its defaults: ' };

    return { make: make, logActions: LOG_ACTIONS, sidesWords: sidesWords, summary: summary, spacingWords: spacingWords, nextStart: nextStart, everySeconds: everySeconds };
});
