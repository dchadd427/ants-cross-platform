// The map maker's starting ants, one at a time and mixed (docs/MAP_EDITOR.md, "Ants: one at a time, group, mix"): the box of the Ants tool (the switch One at a time / Group, the four teams with their
// counts, Clump and Mix, the plain warnings, "Clear <team> ants...") and the arithmetic under it. A lazily loaded script (the page loads it, with mapmaker_ants.css, the first time the Ants tool is
// chosen), so the editor's eager scripts stay as they are; without it the Ants tool is the clump of before (mapmaker_tools.js). Everything it does is a CHANGE of the open file's bytes (mapmaker_blocks.js
// has the lists: block 1 holds one record of six bytes for each ant, and the plants), made of small splices {rel, removed, inserted} (a record put in or taken out and the count word) and given to the
// history as one step, so one tap, one mix and one clear are one Undo each. Nothing here writes a byte itself. The words that a person reads are put in with textContent. A plain ES5 script that
// a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_edit.js'), require('./mapmaker_blocks.js'), require('./mapmaker_view.js'));
    else root.MapAnts = factory(root.MapEdit, root.MapBlocks, root.MapView);
})(this, function (Edit, Blocks, View) {
    'use strict';

    var EMPTY2 = 0x7FFE;
    var MAX_RECORDS = 65535;                         // block 1 holds a word's worth of records, the ants and the plants together (docs/MAP_FORMAT.md, "Limits")
    var ORDER = [3, 2, 0, 1];                        // the buttons' order: Black, Blue, Green, Red (the indexes of Blocks.TEAMS, which has the game's seats)
    var COLORS = ['#2f9a52', '#c0392b', '#4a5fc8', '#262626'];      // (the same colors as mapmaker_tiles.js, by seat)
    var ANT_TYPES = [[0, 'Default / Worker', 811], [4, 'Combat', 897], [3, 'Thief', 1085], [1, 'Bomber', 774], [5, 'Swimmer', 963], [2, 'Fire', 676]];
    var LIST_ERROR = 'The list of starting ants in this file cannot be changed here: the file ends in the middle of it.';

    function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }
    function num(n) { return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }
    function label(t) { return Blocks.TEAMS[t].label; }
    function typeLabel(at) { for (var i = 0; i < ANT_TYPES.length; i++) if (ANT_TYPES[i][0] === at) return ANT_TYPES[i][1]; return ''; }
    function join(names) { return names.length < 2 ? names.join('') : names.slice(0, -1).join(', ') + ' and ' + names[names.length - 1]; }
    function word(v) { return [v & 255, (v >> 8) & 255]; }

    // ---- the arithmetic (pure: the file's bytes in, a CHANGE or a sentence out) ----

    // The cell (as its index) of each ant of a team on the map: {index: the record's place}. Made once for a reading of the list (Blocks.layout keeps it).
    function antMap(file, lay) {
        if (lay.antCells) return lay.antCells;
        var map = {}, i;
        for (i = 0; i < lay.records.length; i++) {
            var rec = lay.records[i];
            if (Blocks.teamOfMarker(rec.tile) >= 0 && rec.row < file.rows && rec.column < file.columns && map[rec.row * file.columns + rec.column] === undefined) map[rec.row * file.columns + rec.column] = i;
        }
        lay.antCells = map;
        return map;
    }
    // The team of the ant on the cell, or -1
    function teamAt(file, row, column) {
        var lay = Blocks.layout(file), at = lay.ok1 ? antMap(file, lay)[row * file.columns + column] : undefined;
        return at === undefined ? -1 : Blocks.teamOfMarker(lay.records[at].tile);
    }

    // Why an ant cannot start on the cell `idx`, in a sentence, or '' when it can: a hill, a food pile, an object and solid ground are in the way (the cells that the clump passes over too: mapmaker_blocks.js,
    // startCells). The check of the map wants only a start inside the grid (docs/MAP_FORMAT.md, `start_marker_outside_map`), so this is the editor's own care: the ant has to be able to walk.
    function whyNot(file, idx) {
        var why = Blocks.inTheWay(file, idx);
        if (why === 'hill' || why === 'pile') return Blocks.blockedText(file, idx);
        if (Edit.tile(file, 2, idx) !== EMPTY2) return 'An object stands there. Take it away with Erase first, or choose another cell.';
        if (Edit.flags(file, 1, idx) & 1) return 'That ground is solid, so an ant cannot start there. Choose another cell.';
        return '';
    }

    function bytesOf(records) {
        var out = new Uint8Array(6 * records.length), i;
        for (i = 0; i < records.length; i++) { out.set(word(records[i].tile), 6 * i); out.set(word(records[i].row), 6 * i + 2); out.set(word(records[i].column), 6 * i + 4); }
        return out;
    }
    // The count word and a piece of the list as splices (the later one first: the ones that are listed first are applied first)
    function listChange(file, lay, piece, count) {
        var change = { names: [], cells: [], splices: [piece] }, at = file.blocksAt, seen = {};
        change.splices.push({ rel: 0, removed: file.bytes.slice(at, at + 2), inserted: new Uint8Array(word(count)) });
        (piece.teams || []).forEach(function (t) { if (!seen[t]) { seen[t] = true; change.names.push([Blocks.TEAMS[t].marker, Blocks.TEAMS[t].markerName]); } });
        delete piece.teams;
        return change;
    }
    function added(file, lay, records) {
        var teams = [], i;
        for (i = 0; i < records.length; i++) { var t = Blocks.teamOfMarker(records[i].tile); if (teams.indexOf(t) < 0) teams.push(t); }
        return listChange(file, lay, { rel: lay.b1.length, removed: new Uint8Array(0), inserted: bytesOf(records), teams: teams }, lay.records.length + records.length);
    }
    function record(team, row, column, antType, file) { return { tile: Blocks.markerTile ? Blocks.markerTile(file, team, antType) : Blocks.TEAMS[team].marker, row: row, column: column }; }

    // One ant of `team` put on (row, column): a CHANGE, or {error}. A tap on a cell that has an ant is not this (takeOne).
    function putOne(file, team, row, column, antType) {
        var lay = Blocks.layout(file);
        if (!Blocks.TEAMS[team]) return { error: 'There is no such team.' };
        if (!lay.ok1) return { error: LIST_ERROR };
        if (row < 0 || column < 0 || row >= file.rows || column >= file.columns) return { error: 'There is no room for an ant there.' };
        if (antMap(file, lay)[row * file.columns + column] !== undefined) return { error: 'An ant is already on that cell.' };
        var why = whyNot(file, row * file.columns + column);
        if (why) return { error: why };
        if (lay.records.length + 1 > MAX_RECORDS) return { error: 'The file holds at most ' + num(MAX_RECORDS) + ' starting ants and plants together, and it has them. Take some away first.' };
        return added(file, lay, [record(team, row, column, antType, file)]);
    }

    // The ant on (row, column) taken away, whatever its team: a CHANGE, or null when there is none
    function takeOne(file, row, column) {
        var lay = Blocks.layout(file), at = lay.ok1 && row >= 0 && column >= 0 && row < file.rows && column < file.columns ? antMap(file, lay)[row * file.columns + column] : undefined;
        if (at === undefined) return null;
        var rel = lay.records[at].rel;
        return listChange(file, lay, { rel: rel, removed: file.bytes.slice(file.blocksAt + rel, file.blocksAt + rel + 6), inserted: new Uint8Array(0) }, lay.records.length - 1);
    }

    // The ants that stand where they cannot get out: every one of the eight cells round it is outside the map, solid or held by another ant (the cells of `extra` count as ants). A soft hint, never a refusal.
    function packedCount(file, extra) {
        var lay = Blocks.layout(file), taken = {}, n = 0, k;
        if (!lay.ok1 || lay.records.length + (extra ? extra.length : 0) > 20000) return 0;       // (a count for a map of a few thousand ants: more would be slow and means little)
        var map = antMap(file, lay), list = [], idx;
        for (k in map) if (Object.prototype.hasOwnProperty.call(map, k)) { taken[k] = true; list.push(+k); }
        (extra || []).forEach(function (c) { idx = c.r * file.columns + c.c; if (!taken[idx]) { taken[idx] = true; list.push(idx); } });
        function shut(r, c) { return r < 0 || c < 0 || r >= file.rows || c >= file.columns || !!taken[r * file.columns + c] || (Edit.flags(file, 1, r * file.columns + c) & 1) === 1; }
        list.forEach(function (i) {
            var r = (i / file.columns) | 0, c = i - r * file.columns, dr, dc, all = true;
            for (dr = -1; dr <= 1 && all; dr++) for (dc = -1; dc <= 1; dc++) if ((dr || dc) && !shut(r + dr, c + dc)) { all = false; break; }
            if (all) n++;
        });
        return n;
    }

    // The mix: the teams of `teams` (indexes of Blocks.TEAMS) put into the box {r0, c0, r1, c1}: `per` ants for each team, on every cell (gap 1) or every other cell (gap 2: the cells whose row and column,
    // counted from the box's corner, add up to an even number, so no two ants touch side by side). Cells that are not free (an object, solid ground, a hill, a pile, an ant) are passed over. The ants
    // are spread evenly over the free cells of the pattern (a box with room to spare is filled from edge to edge, a tight one is filled whole), and the teams are dealt to them in an order that is
    // shuffled by a generator seeded from the box and the choices (so the pattern is the same every time, the counts are exact, and no row or column is one team). The preview is this plan.
    // Answers {ok, cells: [{r, c, team}], need (cells), have (cells of the box), free (cells that are free and on the pattern), ants, text}.
    function planMix(file, box, teams, per, gap) {
        var lay = Blocks.layout(file), n = teams.length, ants = per * n, rows = box.r1 - box.r0 + 1, cols = box.c1 - box.c0 + 1;
        var out = { ok: false, cells: [], need: ants * gap, have: rows * cols, free: 0, ants: ants, text: '' };
        if (!lay.ok1) { out.text = LIST_ERROR; return out; }
        if (!n) { out.text = 'Choose at least one team for the mix.'; return out; }
        var map = antMap(file, lay), r, c, found = [], i;
        for (r = box.r0; r <= box.r1; r++) for (c = box.c0; c <= box.c1; c++) {
            if (gap === 2 && ((r - box.r0) + (c - box.c0)) % 2) continue;
            var idx = r * file.columns + c;
            if (map[idx] !== undefined || whyNot(file, idx)) continue;
            found.push({ r: r, c: c });
        }
        out.free = found.length;
        var sentence = num(ants) + ' ants need ' + num(out.need) + ' cells; your box has ' + num(out.have) + '.';
        if (found.length < ants) {
            out.text = out.have < out.need ? 'This box is too small. ' + sentence + ' Drag a bigger box, or put fewer ants.' : 'Too few free cells. ' + sentence + ' Only ' + num(out.free) + ' of them are free for an ant (objects, solid ground and ants are passed over). Drag a bigger box, or put fewer ants.';
            return out;
        }
        var order = [];
        for (i = 0; i < ants; i++) order.push(teams[i % n]);
        var seed = (box.r0 * 73856093) ^ (box.c0 * 19349663) ^ (box.r1 * 83492791) ^ (box.c1 * 2654435761) ^ (per * 40503) ^ (gap * 977);
        for (i = 0; i < teams.length; i++) seed ^= (teams[i] + 1) * 69069;
        function next() { seed = (seed + 0x6D2B79F5) | 0; var t = Math.imul(seed ^ (seed >>> 15), 1 | seed); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
        for (i = ants - 1; i > 0; i--) { var j = Math.floor(next() * (i + 1)), t = order[i]; order[i] = order[j]; order[j] = t; }
        var cells = order.map(function (team, k) { var at = found[Math.floor((k + 0.5) * found.length / ants)]; return { r: at.r, c: at.c, team: team }; });
        // a row or a column of two or more ants that are all one team gets one of them swapped with another team's ant elsewhere (the counts stay exact); the passes are few and the same every time
        for (var pass = 0; pass < 12 && n > 1; pass++) {
            var lines = {}, changed = false;
            cells.forEach(function (cell, k) { ['r' + cell.r, 'c' + cell.c].forEach(function (key) { (lines[key] = lines[key] || []).push(k); }); });
            Object.keys(lines).sort().forEach(function (key) {
                var list = lines[key];
                if (list.length < 2 || list.some(function (k) { return cells[k].team !== cells[list[0]].team; })) return;
                var mine = cells[list[0]].team, start = Math.floor(next() * cells.length), m;
                for (m = 0; m < cells.length; m++) {
                    var o = (start + m) % cells.length;
                    if (cells[o].team !== mine && list.indexOf(o) < 0) { var other = cells[o].team; cells[o].team = mine; cells[list[0]].team = other; changed = true; break; }
                }
            });
            if (!changed) break;
        }
        out.cells = cells;
        out.ok = true;
        out.text = out.have >= out.need ? sentence : num(ants) + ' ants fit: ' + num(out.free) + ' cells of your box are free at this spacing.';
        return out;
    }

    // A mix as a CHANGE, or {error}: the ants of the plan appended to the list (one splice and the count word)
    function putMix(file, plan, antType) {
        var lay = Blocks.layout(file);
        if (!lay.ok1) return { error: LIST_ERROR };
        if (!plan || !plan.ok) return { error: plan && plan.text || 'There is nothing to put.' };
        if (lay.records.length + plan.cells.length > MAX_RECORDS) return { error: tooMany(lay.records.length + plan.cells.length) };
        return added(file, lay, plan.cells.map(function (cell) { return record(cell.team, cell.r, cell.c, cell.antType || antType, file); }));
    }
    function tooMany(total) { return num(total) + ' ants. The file holds ' + num(MAX_RECORDS) + ' at most (all teams, plants included). Take ' + num(total - MAX_RECORDS) + (total - MAX_RECORDS === 1 ? ' ant' : ' ants') + ' away. The Put buttons stop.'; }

    // All the ants of a team taken away (Blocks.eraseStart): a CHANGE that is empty when the team has none
    function clearTeam(file, team) { return Blocks.eraseStart(file, team); }

    // The counts and the plain warnings of the box: {counts: [by seat], total, missing: [seats], over, packed, lines: [{kind: 'ok'|'warn'|'bad', text}]}; `extra` is what a mix would add
    function summary(file, extra) {
        var facts = Blocks.facts(file, false), lay = Blocks.layout(file), total = 0, missing = [], i, lines = [], plants = lay.ok1 ? lay.records.length : 0;
        for (i = 0; i < 4; i++) total += facts.ants[i];
        var out = { counts: facts.ants, total: total, records: plants, missing: [], over: false, packed: 0, lines: lines, listOk: lay.ok1 };
        ORDER.forEach(function (t) { if (!facts.ants[t]) missing.push(label(t)); });
        out.missing = missing;
        var after = plants + (extra ? extra.length : 0);
        if (after > MAX_RECORDS) { out.over = true; lines.push({ kind: 'bad', text: tooMany(after) }); }
        else if (missing.length === 1) lines.push({ kind: 'warn', text: missing[0] + ' has no ants. The Check says: this map cannot be played with ' + missing[0] + '. Put at least one ' + missing[0] + ' ant, or leave it.' });
        else if (missing.length) lines.push({ kind: 'warn', text: join(missing) + ' have no ants. The Check says: this map cannot be played with them. Put at least one ant for each, or leave them.' });
        else lines.push({ kind: 'ok', text: 'Every team has ants. The map can be played with four seats.' });
        if (file.rows > 90) lines.push({ kind: 'note', text: 'The game cannot play a map of more than 90 rows yet (this one has ' + file.rows + '): the check is fine, and Play this map will say so.' });
        out.packed = packedCount(file, extra);
        if (out.packed) lines.push({ kind: 'warn', text: 'Ants shoulder to shoulder cannot find a way out. Leave a free cell now and then. (Soft hint, the map still saves.)' });
        return out;
    }

    // ---- the box in the page ----

    // env: {doc, win, host: {tools (the column of the tools: a phone's box goes in it, before `hint`), hint, side (the column on the right, which the box takes over on a wider screen, its check line stays), cv (the node the map canvas
    //       is in), map (the map canvas)}, file(), act(change) -> bool, say(title, text), dialogs,
    //       team() / setTeam(seat), ants() / setAnts(n) (the clump's count), view() -> {view, width, height}, about() -> {ant, eggs} or null, props() (open the properties), onTool(on) }
    function create(env) {
        var doc = env.doc, win = env.win || {}, st = { mode: 'one', tap: 'put', shape: 'clump', antType: env.antType ? env.antType() : 0, mix: [true, true, true, true], per: 25, gap: 2, box: null, plan: null, note: '' };
        var panel = doc.createElement('div'), painting = false, shown = false, over = null, loop = 0, last = '';
        panel.className = 'am'; panel.hidden = true;
        panel.setAttribute('aria-label', 'Starting ants'); panel.setAttribute('role', 'group');
        // where the box is: under the tool buttons on a phone held upright, and in the right-hand column (in place of "This map", whose check line stays) on a wider screen
        var query = win.matchMedia ? win.matchMedia('(max-width: 600px)') : null, side = env.host.side || null;
        function place() {
            var narrow = !side || (query ? query.matches : false), line = side && side.querySelector('.checkline');
            if (narrow) env.host.tools.insertBefore(panel, env.host.hint || null); else side.insertBefore(panel, line || null);
            if (side && side.classList) side.classList.toggle('am-on', !narrow && shown);
        }
        if (query && query.addEventListener) query.addEventListener('change', place);

        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function button(key, text, cls, on, onClick) {
            var b = el('button', 'am-b' + (cls ? ' ' + cls : ''), text);
            b.type = 'button'; b.setAttribute('data-k', key);
            if (on !== null) b.setAttribute('aria-pressed', String(!!on));
            b.addEventListener('click', onClick);
            return b;
        }
        function head(text) { return el('div', 'am-h', text); }
        function row(cls) { return el('div', 'am-row' + (cls ? ' ' + cls : '')); }
        function chosenMix() { return ORDER.filter(function (t) { return st.mix[t]; }); }
        function file() { return env.file(); }
        function mixing() { return st.mode === 'group' && st.shape === 'mix'; }

        function setPlan() {
            st.plan = st.box && !painting ? planMix(file(), st.box, chosenMix(), st.per, st.gap) : null;
        }
        // ---- what a tap does ----
        function tap(cell) {
            var f = file(), at = teamAt(f, cell.row, cell.column), change;
            if (at >= 0) { change = takeOne(f, cell.row, cell.column); st.note = 'Took away the ' + label(at) + ' ant at row ' + cell.row + ', column ' + cell.column + '. One Undo brings it back.'; }
            else if (st.tap === 'take') { st.note = 'There is no ant on row ' + cell.row + ', column ' + cell.column + ' to take away.'; render(); return; }
            else {
                var aType = f.version === 9 ? st.antType : 0;
                change = putOne(f, env.team(), cell.row, cell.column, aType);
                if (change.error) { st.note = ''; render(); return env.say('It cannot be put there', change.error); }
                st.note = 'Put one ' + label(env.team()) + (aType ? ' ' + typeLabel(aType) : '') + ' ant at row ' + cell.row + ', column ' + cell.column + '.';
            }
            if (!env.act(change)) { st.note = ''; render(); }
        }

        function putMixNow() {
            var change = putMix(file(), st.plan, file() && file().version === 9 ? st.antType : 0);
            if (change.error) return env.say('It cannot be put there', change.error);
            var n = st.plan.cells.length;
            st.box = null; st.plan = null; st.note = 'Put ' + plural(n, 'ant', 'ants') + '. One Undo takes them all away.';
            if (!env.act(change)) { st.note = ''; }
            render(); drawOver();
        }

        function clearNow() {
            var t = env.team(), count = Blocks.facts(file(), false).ants[t];
            if (!count || !env.dialogs) return;
            env.dialogs.open({
                title: 'Clear the ' + label(t) + ' ants?', cls: 'narrow', focus: '[data-k="keep"]',
                build: function (body) { body.appendChild(el('p', '', 'This takes away all ' + plural(count, label(t) + ' ant', label(t) + ' ants') + ' from the map. The other teams stay. One Undo brings them back.')); },
                buttons: [
                    { id: 'keep', text: 'Keep them', ghost: true },
                    { id: 'clear', text: 'Take away ' + plural(count, label(t) + ' ant', label(t) + ' ants'), onClick: function () {
                        var change = clearTeam(file(), t);
                        if (env.act(change)) { st.note = 'Took away ' + plural(count, label(t) + ' ant', label(t) + ' ants') + '.'; }
                        render();
                    } }
                ]
            });
        }

        // ---- drawing ----
        function teamButton(t, text, on) {
            var b = button('t' + t, '', 'am-team' + (on ? ' on' : ''), on, function () { env.setTeam(t); st.note = ''; render(); });
            var dot = el('i', 'am-dot'); dot.style.background = COLORS[t]; b.appendChild(dot);
            b.appendChild(el('span', 'am-tn', label(t)));
            b.appendChild(el('span', 'am-tc', text));
            return b;
        }

        function stepper(key, value, lo, hi, set, what) {
            var box = row('am-step'), less = button(key + '-', '−', 'am-s', null, function () { set(Math.max(lo, value - 1)); }), more = button(key + '+', '+', 'am-s', null, function () { set(Math.min(hi, value + 1)); });
            var input = el('input', 'am-n'); input.type = 'text'; input.setAttribute('inputmode', 'numeric'); input.setAttribute('aria-label', what); input.setAttribute('data-k', key); input.value = String(value);
            less.setAttribute('aria-label', 'Fewer ' + what); more.setAttribute('aria-label', 'More ' + what);
            less.disabled = value <= lo; more.disabled = value >= hi;
            input.addEventListener('change', function () { var v = parseInt(String(input.value).replace(/[^0-9]/g, ''), 10); set(isNaN(v) ? value : Math.max(lo, Math.min(hi, v))); });
            box.appendChild(less); box.appendChild(input); box.appendChild(more);
            return box;
        }

        function render() {
            var f = file(), keep = doc.activeElement && doc.activeElement.getAttribute ? doc.activeElement.getAttribute('data-k') : null, inside = !!keep && panel.querySelector('[data-k="' + keep + '"]') !== null;
            panel.textContent = '';
            if (!f) return;
            var mix = mixing(), extra = mix && st.plan && st.plan.ok ? st.plan.cells : null, sum = summary(f, extra), t = env.team(), r;
            // the switch
            panel.appendChild(head('Starting ants'));
            r = row('am-two');
            r.appendChild(button('one', 'One at a time', '', st.mode === 'one', function () { st.mode = 'one'; clearBox(); render(); env.refresh(); }));
            r.appendChild(button('group', 'Group', '', st.mode === 'group', function () { st.mode = 'group'; st.note = ''; render(); env.refresh(); }));
            panel.appendChild(r);
            if (st.mode === 'one') {
                panel.appendChild(head('What a tap does'));
                r = row('am-two');
                r.appendChild(button('put', 'Put an ant', '', st.tap === 'put', function () { st.tap = 'put'; render(); }));
                r.appendChild(button('take', 'Take one away', '', st.tap === 'take', function () { st.tap = 'take'; render(); }));
                panel.appendChild(r);
                panel.appendChild(el('p', 'am-note', st.tap === 'put' ? 'Tap a cell: one ' + label(t) + ' ant. Tap that ant again to take it away. A drag looks around.' : 'Tap an ant to take it away, whatever its team. A drag looks around.'));
            } else {
                panel.appendChild(head('Shape'));
                r = row('am-two');
                r.appendChild(button('clump', 'Clump', '', st.shape === 'clump', function () { st.shape = 'clump'; clearBox(); render(); env.refresh(); }));
                r.appendChild(button('mix', 'Mix', '', st.shape === 'mix', function () { st.shape = 'mix'; st.note = ''; render(); env.refresh(); }));
                panel.appendChild(r);
            }
            if (mix) {
                panel.appendChild(head('Teams in the mix'));
                r = row('am-four');
                ORDER.forEach(function (tm) {
                    var b = button('m' + tm, '', 'am-team am-mixteam', st.mix[tm], function () { st.mix[tm] = !st.mix[tm]; if (!chosenMix().length) st.mix[tm] = true; setPlan(); render(); drawOver(); });
                    var dot = el('i', 'am-dot'); dot.style.background = COLORS[tm]; b.appendChild(dot);
                    b.appendChild(el('span', 'am-tn', label(tm))); b.appendChild(el('span', 'am-tc', '+' + num(st.per)));
                    r.appendChild(b);
                });
                panel.appendChild(r);
                panel.appendChild(head('Ants for each team'));
                panel.appendChild(stepper('per', st.per, 1, MAX_RECORDS, function (v) { st.per = v; setPlan(); render(); drawOver(); }, 'ants for each team'));
                panel.appendChild(head('Spacing'));
                r = row('am-two');
                r.appendChild(button('g1', 'Every cell', '', st.gap === 1, function () { st.gap = 1; setPlan(); render(); drawOver(); }));
                r.appendChild(button('g2', 'Every other cell', '', st.gap === 2, function () { st.gap = 2; setPlan(); render(); drawOver(); }));
                panel.appendChild(r);
                var total = st.per * chosenMix().length, need = total * st.gap, says = st.plan ? st.plan.text : (st.box ? '' : 'Drag a box on the map. ' + num(total) + ' ants need ' + num(need) + ' cells.');
                if (st.box && painting) says = 'Box: ' + plural(st.box.r1 - st.box.r0 + 1, 'row', 'rows') + ' x ' + plural(st.box.c1 - st.box.c0 + 1, 'column', 'columns') + '.';
                var about0 = env.about ? env.about() : null;
                var status = el('p', 'am-status' + (st.plan && !st.plan.ok ? ' warn' : ''), says);
                status.setAttribute('role', 'status');
                panel.appendChild(status);
                if (about0 && /^No default/.test(about0.ant)) panel.appendChild(el('p', 'am-note', 'This map has no default ant type, so the ants will not fight. Choose Combat ants in Properties for a brawl.'));
                r = row('am-two am-go');
                r.appendChild(button('cancel', 'Cancel', 'ghost', null, function () { clearBox(); render(); drawOver(); }));
                var put = button('put-mix', 'Put ' + num(total) + ' ants', 'go', null, putMixNow);
                put.disabled = !(st.plan && st.plan.ok) || sum.over || painting;
                r.appendChild(put);
                panel.appendChild(r);
            } else {
                panel.appendChild(head(st.mode === 'one' ? 'Team to put' : 'Team of the clump'));
                var list = row('am-teams');
                ORDER.forEach(function (tm) { list.appendChild(teamButton(tm, num(sum.counts[tm]), tm === t)); });
                panel.appendChild(list);
                if (st.mode === 'group') {
                    panel.appendChild(head('Ants in the clump'));
                    panel.appendChild(stepper('clump', env.ants(), Blocks.MIN_ANTS, Blocks.MAX_ANTS, function (v) { env.setAnts(v); render(); }, 'ants in the clump'));
                    panel.appendChild(el('p', 'am-note', 'A tap starts the clump for the team. Put it again to move it.'));
                }
            }
            if (f.version === 9) {
                panel.appendChild(head('Ant type'));
                var tgrid = row('am-two am-types');
                ANT_TYPES.forEach(function (item) {
                    var b = button('at' + item[0], '', 'am-type', st.antType === item[0], function () { st.antType = item[0]; if (env.setAntType) env.setAntType(item[0]); st.note = ''; render(); });
                    var ic = el('i', 'am-pic'); if (env.pic) env.pic(ic, item[2], 22); b.appendChild(ic);
                    b.appendChild(el('span', 'am-tn', item[1]));
                    tgrid.appendChild(b);
                });
                panel.appendChild(tgrid);
            }
            panel.appendChild(el('p', 'am-total', num(sum.total) + (sum.total === 1 ? ' ant' : ' ants') + ' in all'));
            panel.appendChild(el('p', 'am-note am-face', 'The game picks a random facing at the start of each match; the editor shows an example.'));
            var about = env.about ? env.about() : null;
            if (about) {
                var same = el('p', 'am-note', 'Same for every team: ' + about.ant + ', ' + plural(about.eggs, 'egg', 'eggs') + ' each. ');
                var link = el('button', 'am-link', 'Change in Properties'); link.type = 'button'; link.setAttribute('data-k', 'props'); link.addEventListener('click', function () { env.props(); });
                same.appendChild(link);
                if (f.version !== 9 && env.convert) {
                    same.appendChild(doc.createTextNode(' \u00b7 '));
                    var conv = el('button', 'am-link', 'Switch to Extended map for per-ant types'); conv.type = 'button'; conv.setAttribute('data-k', 'convert'); conv.addEventListener('click', function () { env.convert(); });
                    same.appendChild(conv);
                }
                panel.appendChild(same);
            }
            if (!mix) {
                var clear = button('clear', 'Clear ' + label(t) + ' ants...', 'am-clear', null, clearNow);
                clear.disabled = !sum.counts[t];
                panel.appendChild(clear);
            }
            var warn = el('div', 'am-warn');
            sum.lines.forEach(function (line) { warn.appendChild(el('p', 'am-w ' + line.kind, line.text)); });
            panel.appendChild(warn);
            if (st.note) { var note = el('p', 'am-said', st.note); note.setAttribute('role', 'status'); panel.appendChild(note); }
            if (inside) { var again = panel.querySelector('[data-k="' + keep + '"]'); if (again && again.focus) again.focus(); }
        }

        function clearBox() { st.box = null; st.plan = null; painting = false; st.note = ''; drawOver(); }

        // The box and the faint ants of the mix, on a canvas that lies over the map and is drawn again when the view moves
        function drawOver() {
            if (!over) return;
            var info = env.view ? env.view() : null, map = env.host.map, ctx = over.getContext && over.getContext('2d');
            if (!ctx || !info || !info.view || !map) return;
            var w = info.width, h = info.height, dpr = Math.min(2, win.devicePixelRatio || 1);
            if (over.width !== Math.round(w * dpr) || over.height !== Math.round(h * dpr)) { over.width = Math.round(w * dpr); over.height = Math.round(h * dpr); }
            over.style.left = map.offsetLeft + 'px'; over.style.top = map.offsetTop + 'px'; over.style.width = w + 'px'; over.style.height = h + 'px';
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            ctx.clearRect(0, 0, w, h);
            if (!st.box || !shown || !mixing()) return;
            var v = info.view, s = v.scale, a = View.toScreen(v, w, h, st.box.r0, st.box.c0), z = View.toScreen(v, w, h, st.box.r1 + 1, st.box.c1 + 1);
            if (st.plan && st.plan.ok) {
                var x0 = Math.floor(-s), y0 = Math.floor(-s), rad = Math.max(2, Math.min(s * 0.36, 14));
                st.plan.cells.forEach(function (cell) {
                    var p = View.toScreen(v, w, h, cell.r, cell.c);
                    if (p.x < x0 || p.y < y0 || p.x > w || p.y > h) return;
                    ctx.beginPath(); ctx.arc(p.x + s / 2, p.y + s / 2, rad, 0, 6.2832);
                    ctx.globalAlpha = .62; ctx.fillStyle = COLORS[cell.team]; ctx.fill();
                    ctx.globalAlpha = .9; ctx.lineWidth = 1.5; ctx.strokeStyle = '#f4ece2'; ctx.stroke();
                });
                ctx.globalAlpha = 1;
            }
            ctx.setLineDash([7, 5]); ctx.lineWidth = 2.5; ctx.strokeStyle = '#000'; ctx.strokeRect(a.x, a.y, z.x - a.x, z.y - a.y);
            ctx.lineWidth = 1.5; ctx.strokeStyle = '#f2c230'; ctx.strokeRect(a.x, a.y, z.x - a.x, z.y - a.y); ctx.setLineDash([]);
        }
        // (while a box is there the overlay follows the view: pan, zoom and a resize move it)
        function follow() {
            loop = 0;
            if (!st.box || !shown) return;
            var info = env.view(), sign = info && info.view ? [info.view.cx, info.view.cy, info.view.scale, info.width, info.height].join(',') : '';
            if (sign !== last) { last = sign; drawOver(); }
            if (win.requestAnimationFrame) loop = win.requestAnimationFrame(follow);
        }
        function watch() { if (!loop && win.requestAnimationFrame && st.box && shown) { last = ''; loop = win.requestAnimationFrame(follow); } }

        if (env.host.cv && doc.createElement('canvas').getContext) {
            over = doc.createElement('canvas'); over.className = 'am-over'; over.setAttribute('aria-hidden', 'true');
            env.host.cv.appendChild(over);
        }

        // ---- the painter the Ants tool hands the canvas to (mapmaker_tools.js) ----
        var start = null;
        var painter = {
            size: function () { return 1; },
            tapOnly: function () { return st.mode === 'one'; },
            begin: function (cell) {
                if (st.mode === 'one') { tap(cell); return false; }
                start = cell; painting = true; st.box = { r0: cell.row, c0: cell.column, r1: cell.row, c1: cell.column }; st.plan = null; st.note = '';
                render(); drawOver(); watch();
                return true;
            },
            extend: function (from, to) {
                if (!start || !st.box) return;
                st.box = { r0: Math.min(start.row, to.row), c0: Math.min(start.column, to.column), r1: Math.max(start.row, to.row), c1: Math.max(start.column, to.column) };
                render(); drawOver();
            },
            end: function () { if (!st.box) return; painting = false; setPlan(); render(); drawOver(); },
            cancel: function () { clearBox(); render(); }
        };

        return {
            painter: painter,
            // the tool asks whether the box takes the tap and the drag: One at a time and Mix do; Clump is the tool's own (the old tap)
            mine: function () { return st.mode === 'one' || st.shape === 'mix'; },
            show: function (on) {
                shown = !!on; panel.hidden = !on; place();
                if (!on) { clearBox(); } else render();
                drawOver(); watch();
            },
            // the map changed (a tap, an Undo, a clear), or another was opened: the counts are made again, and a box that was dragged is made again against the new bytes
            refresh: function () { if (!shown) return; if (st.box && !painting) setPlan(); render(); drawOver(); },
            setMap: function () { clearBox(); st.note = ''; if (shown) render(); },
            state: function () { return { mode: st.mode, tap: st.tap, shape: st.shape, antType: st.antType, mix: st.mix.slice(), per: st.per, gap: st.gap, box: st.box, plan: st.plan, note: st.note }; },
            panel: panel
        };
    }

    return {
        create: create, ORDER: ORDER, ANT_TYPES: ANT_TYPES, MAX_RECORDS: MAX_RECORDS,
        teamAt: teamAt, whyNot: whyNot, putOne: putOne, takeOne: takeOne, planMix: planMix, putMix: putMix, clearTeam: clearTeam, summary: summary, packedCount: packedCount, tooMany: tooMany
    };
});
