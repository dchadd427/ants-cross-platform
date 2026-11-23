// The tools that a person's AI may call on the open map (docs/MAP_AI.md, "The tools"): the handlers that the page registers for the names of hub/aitools.py, and for no other. The hub has
// checked the arguments against the schema before a call gets here; this script judges the map itself and answers in sentences of its own. Everything works on the open map through the map
// maker's own functions (the editor of mapmaker_edit.js, the changes of mapmaker_blocks.js, mapmaker_objects.js, mapmaker_edges.js and the core's setProperties), so the rules of Classic and
// Extended maps, what is in the way, the solid bits and the history are the person's own: each call that changes the map is ONE step of the history. A tool never calls anything that submits,
// sends or deletes: there is none in this file. Text that is read from a map goes out as data; text that arrives (a description, a name, a note) is only ever put in with textContent or into the
// map's bytes by the core. A plain ES5 script that a page and node can load; `env` is what the page gives it (see create).
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_edit.js'), require('./mapmaker_blocks.js'), require('./mapmaker_palette.js'), require('./mapmaker_tiles.js'), require('./mapmaker_objects.js'), require('./mapmaker_props.js'), require('./mapmaker_file.js'), require('./mapmaker_sizes.js'));
    else root.MapAiTools = factory(root.MapEdit, root.MapBlocks, root.MapPalette, root.MapTiles, root.MapObjects, root.MapProps, root.MapFile, root.MapSizes);
})(this, function (Edit, Blocks, Palette, Tiles, Objects, Props, File, Sizes) {
    'use strict';

    var EMPTY2 = Edit.EMPTY2, MAX_CELLS = 5000, TEAMS = Blocks.TEAMS.map(function (t) { return t.key; });
    var KIND_LETTER = { gravel: 'g', dirt: 'd', slate: 's', mud: 'm', water: 'w' };
    var GUIDES = { guide: 'ants-map-guide', tools: 'ants-mapmaker-tools', vocabulary: 'ants-map-vocabulary', terrain: 'ants-map-terrain', layout: 'ants-map-layout', 'check-and-fix': 'ants-map-check-and-fix' };
    var HILL_LETTER = { green: 'G', red: 'R', blue: 'B', black: 'K' };
    var LEGEND = { ground: { g: 'gravel', d: 'dirt', s: 'slate', m: 'mud', w: 'water', '?': 'ground tile of no kind' },
                   objects: { '.': 'nothing', G: 'green hill', R: 'red hill', B: 'blue hill', K: 'black hill', P: 'power-up', b: 'bomb', f: 'fire wall', '=': 'bridge', p: 'plant', F: 'food', O: 'bigger object', o: 'object' } };
    var WAIT_MS = 50, WAIT_TRIES = 40;
    var OUT_OF_MAP = 'That cell is outside the map.';

    function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }
    function clampName(text, n) { return Array.from(String(text)).slice(0, n).join(''); }
    function clean1(text) { return Array.from(String(text)).filter(function (c) { return c >= ' ' && c !== '\u007f'; }).join('').replace(/^\s+|\s+$/g, ''); }
    function refuse(code, message) { return { ok: false, error: code, message: message }; }
    function okay(result, cells) { return { ok: true, result: result, cells: cells || 0 }; }
    function inside(f, r, c) { return r >= 0 && c >= 0 && r < f.rows && c < f.columns; }
    function bigIds() { var o = {}; Palette.items('bigger').forEach(function (i) { o[i.id] = i; }); return o; }
    function foodIds() { var o = {}; Object.keys(Blocks.FOOD_NAMES).forEach(function (k) { o[k] = 1; }); return o; }

    // ---- looking: pure functions of a file ----

    // The letter of a ground tile (its kind) and of an object tile (its class), for the text picture of a region
    function groundLetter(id) { var k = Tiles.kindOf(id); return k ? KIND_LETTER[k] : '?'; }
    function objectLetter(id, big, food) {
        if (id === EMPTY2) return '.';
        for (var t = 0; t < 4; t++) if (Blocks.TEAMS[t].hill === id) return HILL_LETTER[TEAMS[t]];
        if (id >= 62 && id <= 66) return 'P';
        if (id >= 129 && id <= 132) return 'b';
        if (id === 134) return 'f';
        if (id === 37) return '=';
        if (Blocks.PLANT_NAMES[id]) return 'p';
        if (food[id]) return 'F';
        return big[id] ? 'O' : 'o';
    }

    // The hills of a file as the page sees them: the entrance cell of each team's hill, and whether all 16 cells are there
    function hillsOf(f) {
        var out = [];
        for (var t = 0; t < 4; t++) {
            var cells = Blocks.hillCells(f, t);
            if (!cells.length) continue;
            var entrance = cells[0];
            for (var i = 0; i < cells.length; i++) if (Edit.flags(f, 2, cells[i]) & 1) entrance = cells[i];
            out.push({ team: TEAMS[t], row: Math.floor(entrance / f.columns), column: entrance % f.columns, cells: cells.length, whole: cells.length === 16 });
        }
        return out;
    }
    function antsOf(f) { return f.markers.map(function (m) { var o = { team: m.team, row: m.row, column: m.column }; if (m.antType) o.type = Props.TYPES[m.antType].key; return o; }); }
    function pilesOf(f) {
        var lay = Blocks.layout(f);
        return lay.ok2 ? lay.piles.map(function (p) { return { row: p.row, column: p.column, stages: p.stages.length, bites: p.units, points: p.points }; }) : f.food.map(function (p) { return { row: p.row, column: p.column }; });
    }

    // The text picture of a window {row, column, rows, columns} (cut by the map's edge): ground kinds, object classes, a legend, and the hills, ants and food piles inside it
    function region(f, win) {
        var r0 = win.row, c0 = win.column, r1 = Math.min(f.rows, r0 + win.rows), c1 = Math.min(f.columns, c0 + win.columns), big = bigIds(), food = foodIds(), ground = [], objects = [], r, c;
        if (r0 >= f.rows || c0 >= f.columns) return null;
        for (r = r0; r < r1; r++) {
            var g = '', o = '';
            for (c = c0; c < c1; c++) { g += groundLetter(f.tile1(r, c)); o += objectLetter(f.tile2(r, c), big, food); }
            ground.push(g); objects.push(o);
        }
        function within(x) { return x.row >= r0 && x.row < r1 && x.column >= c0 && x.column < c1; }
        var ants = antsOf(f).filter(within), cap = 200;
        return { row: r0, column: c0, rows: r1 - r0, columns: c1 - c0, ground: ground, objects: objects, legend: LEGEND, hills: hillsOf(f).filter(within), ants: ants.slice(0, cap), ants_more: Math.max(0, ants.length - cap), food: pilesOf(f).filter(within), plants: f.plants.filter(within).map(function (p) { return { tile: p.id, row: p.row, column: p.column }; }) };
    }

    // The sentences of the checker as the doors give them: {works, headline, kind, findings: [{severity, message, row, column}]}
    function checkOf(report) {
        return { works: !!report.works, headline: String(report.headline || ''), kind: report.kind === 'extended' ? 'extended' : 'classic',
                 findings: (report.findings || []).slice(0, 60).map(function (x) { var o = { severity: String(x.severity || 'note'), message: String(x.message || '') }; if (typeof x.row === 'number' && x.row >= 0) { o.row = x.row; if (typeof x.column === 'number' && x.column >= 0) o.column = x.column; } return o; }) };
    }

    // The size of a picture of `cols x rows` cells that is at most `px` on its longest side, and how many pixels one cell is
    function pictureSize(cols, rows, px) {
        var k = px / Math.max(cols, rows);
        return { w: Math.max(1, Math.round(cols * k)), h: Math.max(1, Math.round(rows * k)), k: k };
    }

    // ---- the tools ----

    // env: core, limits (MapSizes.limitsOf), win, file(), name(), editor() (the editor of mapmaker_tools.js), changed(dirty) (what the page does after the editor changed the map), rebase(bytes), stroking(), later(fn, ms),
    // isSaved(), numbered(name), keep(name, done(error)) (a new draft of the map as it is), keepBefore(done(error)) (the safety copy), newMap(kind, rows, columns, name) (an error sentence, or ''), mini(file, w, h) (a canvas), makeCanvas(w, h), library() (the entries of the library), fetchMap(name, done(error, bytes)), fetchText(url, done(error, text)), note(text) (a line for the strip), onAct(text)
    // (what the strip says that the AI did), edges() ({MapEdges, MapGround} or null)
    function create(env) {
        var ver = 1, own = [], redoOwn = [], copied = false, tools = {};

        function props() { return Props.read(env.core, env.file().bytes); }
        function report() { return reportOf(env.file().bytes); }
        function reportOf(bytes) { var r = env.core.checkMap(bytes, 'extended'); return r.status === 0 ? checkOf(r.json) : null; }

        // ---- reading ----
        function infoOf(f, name, report0, p) {
            var t, lay = Blocks.facts(f, true), hills = hillsOf(f), teams = [], antCounts = lay.ants;
            for (t = 0; t < 4; t++) teams.push({ team: TEAMS[t], hill: hills.filter(function (h) { return h.team === TEAMS[t]; })[0] ? (hills.filter(function (h) { return h.team === TEAMS[t]; })[0].whole ? 'whole' : 'part') : 'none', ants: antCounts[t] });
            return { kind: f.kind, rows: f.rows, columns: f.columns, cells: f.cells, rules: f.kind === 'extended' ? f.rules : 0, file_bytes: f.bytes.length, name: name, description: p && !p.error ? p.description : '',
                     minutes: p && !p.error ? p.minutes : 0, eggs: p && !p.error ? p.eggs : 0, default_ant: p && !p.error ? Props.TYPES[Props.typeIndex(p.tile)].key : 'none', teams: teams, food_piles: lay.piles, plants: f.plants.length, check: report0 };
        }
        tools.map_info = function (args, done) {
            var f = env.file();
            done(okay(Object.assign(infoOf(f, env.name(), report(), props()), { version: ver })));
        };
        tools.get_region = function (args, done) {
            var r = region(env.file(), args);
            done(r ? okay(Object.assign(r, { version: ver })) : refuse('refused', 'That window is outside the map.'));
        };
        tools.check_map = function (args, done) {
            var r = report();
            done(r ? okay(Object.assign(r, { version: ver })) : refuse('tab_error', 'The check did not run.'));
        };
        tools.list_tiles = function (args, done) {
            var g = args.group, out = { version: ver };
            function pick(name) { return !g || g === name; }
            if (pick('ground')) out.ground = Palette.items('ground').map(function (i) { return { id: i.id, name: i.name, label: i.label, kind: i.kind }; });
            if (pick('objects')) out.objects = Palette.items('objects').filter(function (i) { return !i.plant; }).map(function (i) { return { id: i.id, name: i.name, label: i.label }; });
            if (pick('bigger')) out.bigger = Palette.items('bigger').map(function (i) { return { id: i.id, name: i.name, cells: i.cells.length, rows: i.box.r1 - i.box.r0 + 1, columns: i.box.c1 - i.box.c0 + 1, solid: i.solid }; });
            if (pick('plants')) out.plants = Palette.items('plants').map(function (i) { return { id: i.id, name: i.name, label: i.label }; });
            if (pick('food')) out.food = Palette.items('food').map(function (i) { return { id: i.id, name: i.name, label: i.label, bites: i.units, points: i.points, stages: i.stages }; });
            if (pick('teams')) out.teams = Palette.teams().map(function (t) { return { team: t.key, label: t.label }; });
            if (pick('ants')) out.ants = Props.TYPES.map(function (t) { return { type: t.key, label: t.label }; });
            done(okay(out));
        };
        tools.read_guide = function (args, done) {
            var skill = GUIDES[args.topic];
            if (!skill) return done(refuse('bad_args', 'There is no such guide.'));
            env.fetchText('/ai/skills/' + skill + '/SKILL.md', function (error, text) { done(error ? refuse('refused', 'The guide could not be loaded from the site just now.') : okay({ topic: args.topic, text: text })); });
        };
        tools.list_library_maps = function (args, done) {
            var items = env.library() || [];
            done(okay({ maps: items.slice(0, 700).map(function (m) { return { name: String(m.name), kind: m.kind === 'extended' ? 'extended' : 'classic', rows: m.height || undefined, columns: m.width || undefined, minutes: m.minutes || undefined, players: m.players || undefined }; }) }));
        };
        function viewOf(f, name, args, done) {
            var view = args.view || 'info';
            if (view === 'region') { var r = region(f, args); return done(r ? okay(r) : refuse('refused', 'That window is outside the map.')); }
            if (view === 'picture') return picture(f, args, done);
            var rep = reportOf(f.bytes), p = Props.read(env.core, f.bytes);
            done(okay(infoOf(f, name, rep, p)));
        }
        tools.view_library_map = function (args, done) {
            var items = env.library() || [], entry = null;
            items.forEach(function (m) { if (String(m.name).toLowerCase() === args.name.toLowerCase()) entry = m; });
            if (!entry) return done(refuse('refused', 'There is no library map with that name: see list_library_maps.'));
            env.fetchMap(entry.file, function (error, bytes) {
                var f = error ? null : File.parse(bytes);
                if (!f || !f.ok) return done(refuse('refused', 'That library map could not be loaded just now.'));
                viewOf(f, String(entry.name), args, done);
            });
        };
        function picture(f, args, done) {
            var whole = args.rows === undefined, rect = whole ? { row: 0, column: 0, rows: f.rows, columns: f.columns } : { row: args.row, column: args.column, rows: Math.min(args.rows, f.rows - args.row), columns: Math.min(args.columns, f.columns - args.column) };
            if (rect.rows < 1 || rect.columns < 1) return done(refuse('refused', 'That window is outside the map.'));
            var px = args.max_px || 800, tries = 0, base = env.mini(f, f.columns, f.rows);
            function once() {
                var size = pictureSize(rect.columns, rect.rows, px), out = env.makeCanvas(size.w, size.h), ctx = out.getContext('2d');
                ctx.imageSmoothingEnabled = false;
                ctx.drawImage(base, rect.column, rect.row, rect.columns, rect.rows, 0, 0, size.w, size.h);
                var url = out.toDataURL('image/png'), png = url.slice(url.indexOf(',') + 1);
                if (png.length > env.pictureMax && tries++ < 4) { px = Math.floor(px * 0.7); return once(); }
                if (png.length > env.pictureMax) return done(refuse('too_big', 'The picture is too big: ask for a smaller window or a smaller max_px.'));
                done(okay({ png: png, width: size.w, height: size.h, row: rect.row, column: rect.column, rows: rect.rows, columns: rect.columns, version: ver }));
            }
            once();
        }
        tools.get_picture = function (args, done) { picture(env.file(), args, done); };

        // ---- changing ----
        // The map as the person has it is the map that the AI works on: a stroke that is going on is waited for (two seconds at the most), a call that names an old `base` is refused with the new version, and
        // the first change of a session keeps the person's unsaved work as a draft "(before AI)" first
        // a few words about the map as it is now, for the answer that says that the person changed it
        function summary(f) {
            var facts = Blocks.facts(f, true), hills = facts.hills.filter(function (n) { return n > 0; }).length, ants = facts.ants.reduce(function (a, b) { return a + b; }, 0);
            return f.rows + ' x ' + f.columns + ', ' + plural(hills, 'hill', 'hills') + ', ' + plural(ants, 'starting ant', 'starting ants') + ', ' + plural(facts.piles, 'food pile', 'food piles');
        }
        // A call that the person has stopped (Stop, Turn off, a closed socket) or that the hub has cancelled (it timed out, its key was revoked) must not change anything any more: the page says so with `alive()`
        // (the arming's own number and socket, and the call's id), which is asked again at every point where work could start after a wait: each poll for the person's stroke, and when the safety copy is done
        function stopped(done) { return !!done.alive && !done.alive(); }
        function stoppedAnswer() { return refuse('refused', 'The person stopped the AI, or the call was cancelled: nothing was changed.'); }
        function waitFor(done, go, tries) {
            if (stopped(done)) return done(stoppedAnswer());
            if (!env.stroking()) return go();
            if (tries >= WAIT_TRIES) return done(refuse('busy', 'The person is drawing: try again in a moment.'));
            env.later(function () { waitFor(done, go, tries + 1); }, WAIT_MS);
        }
        function guarded(args, done, work, copyAlways) {
            waitFor(done, function () {
                if (args.base !== undefined && args.base !== ver) return done(refuse('changed_by_person', 'The person changed the map since your last answer. It is version ' + ver + ' now: ' + summary(env.file()) + '. Look again with map_info before you change anything.'));
                if ((copied && !copyAlways) || env.isSaved()) { copied = true; return done(work()); }
                env.keepBefore(function (error) {
                    if (stopped(done)) return done(stoppedAnswer());                         // (stopped while the copy was being kept: the copy stays, the map is not touched)
                    if (error) return done(refuse('refused', 'The browser could not keep a safety copy of the person\u2019s map, so nothing was changed.'));
                    copied = true;
                    done(work());
                });
            }, 0);
        }
        function ownStep() { var h = env.editor().history, id = h.top && h.top(); if (id) own.push(id); redoOwn = []; }
        function applyChange(change) {
            var dirty = env.editor().act(change);
            if (dirty) { env.changed(dirty); ownStep(); }
            return dirty;
        }
        function changeResult(dirty, rect) { var n = dirty ? Math.max(1, dirty.length) : 0, r = { version: ver, changed: n }; if (n && rect) r.rect = rect; return okay(r, n); }
        // the part of the map that a call touched, for the dashed frame that the page draws over it
        function rectOf(args) {
            var i, r0, r1, c0, c1, cs = args.cells, l = args.line, q = args.rect;
            if (q) return { row: q.row, column: q.column, rows: q.rows, columns: q.columns };
            if (l) return { row: Math.min(l.from[0], l.to[0]), column: Math.min(l.from[1], l.to[1]), rows: Math.abs(l.from[0] - l.to[0]) + 1, columns: Math.abs(l.from[1] - l.to[1]) + 1 };
            if (cs && cs.length) { r0 = r1 = cs[0][0]; c0 = c1 = cs[0][1]; for (i = 1; i < cs.length; i++) { r0 = Math.min(r0, cs[i][0]); r1 = Math.max(r1, cs[i][0]); c0 = Math.min(c0, cs[i][1]); c1 = Math.max(c1, cs[i][1]); } return { row: r0, column: c0, rows: r1 - r0 + 1, columns: c1 - c0 + 1 }; }
            if (args.fill) return { row: args.fill.row, column: args.fill.column, rows: 1, columns: 1 };
            return null;
        }
        function said(text) { if (env.onAct) env.onAct(text); }

        // the cells that a paint or an erase names, as [row, column] pairs, or {error}
        function cellsOf(args, f) {
            var out = [], i, r, c;
            if (args.cells) out = args.cells.map(function (p) { return [p[0], p[1]]; });
            else if (args.rect) { var q = args.rect; for (r = q.row; r < q.row + q.rows; r++) for (c = q.column; c < q.column + q.columns; c++) out.push([r, c]); }
            for (i = 0; i < out.length; i++) if (!inside(f, out[i][0], out[i][1])) return { error: OUT_OF_MAP + ' (row ' + out[i][0] + ', column ' + out[i][1] + ')' };
            return out;
        }
        function strokeOf(kind, tile) {
            var ed = env.editor(), f = env.file();
            if (tile !== null) {
                var item = Palette.itemOf(kind, tile), made = Edit.ensureTile(f, tile, item ? item.name : '');
                if (made.error) return { error: 'The map already holds as many different tiles as a map file can name.' };
                if (made.grew) ed.rebase(env.rebase(made.bytes));
            }
            return { stroke: ed.begin(kind, tile) };
        }
        // Runs a stroke of `kind` over the cells (or a fill or a line) and ends it as one step; the number of cells that changed, or a refusal
        function runStroke(kind, tile, args, edges) {
            var f = env.file(), cells = args.fill ? null : (args.line ? [] : cellsOf(args, f)), made = strokeOf(kind, tile);
            if (made.error) return refuse('refused', made.error);
            if (cells && cells.error) { made.stroke.cancel(); return refuse('refused', cells.error); }
            var stroke = made.stroke, n = 0, line = args.line, brush = line && line.brush ? line.brush : 1;
            if (args.fill) {
                if (!inside(f, args.fill.row, args.fill.column)) { stroke.cancel(); return refuse('refused', OUT_OF_MAP); }
                n = stroke.fill(args.fill.row, args.fill.column);
            } else if (line) {
                if (!inside(f, line.from[0], line.from[1]) || !inside(f, line.to[0], line.to[1])) { stroke.cancel(); return refuse('refused', OUT_OF_MAP); }
                n = stroke.segment(line.from[0], line.from[1], line.to[0], line.to[1], brush);
            } else for (var i = 0; i < cells.length; i++) n += stroke.stamp(cells[i][0], cells[i][1], 1);
            var why = stroke.refusal();
            if (why) { stroke.cancel(); return refuse('refused', why); }
            if (n > MAX_CELLS) { var back = stroke.cancel(); env.changed(back); return refuse('too_big', 'That would change ' + n + ' cells; one call changes at most ' + MAX_CELLS + '. Split the work.'); }
            if (edges && kind === 'ground') {
                var box = stroke.box && stroke.box();
                if (box) n += edges.E.strokeStep(env.file, box, edges.G, tile, stroke);
            }
            var dirty = stroke.takeDirty();
            if (edges && kind === 'ground') edges.E.strokeEnd(env.file, edges.G, stroke);
            var count = stroke.end();
            if (count) { env.changed(dirty.concat(stroke.takeDirty())); ownStep(); }
            var result = { version: ver, changed: count }, rect = rectOf(args);
            if (count && rect) result.rect = rect;
            return okay(result, count);
        }
        tools.paint = function (args, done) {
            var edges = null;
            if (args.layer === 'ground' && !Tiles.kindOf(args.tile)) return done(refuse('bad_args', 'Tile ' + args.tile + ' is not a ground tile: see list_tiles.'));
            if (args.layer === 'objects') {
                var item = Palette.itemOf('objects', args.tile);
                if (!item || item.plant) return done(refuse('bad_args', 'Tile ' + args.tile + ' is not a one-cell object (plants have put_plant, bigger objects put_object): see list_tiles.'));
            }
            if (args.auto_edges && args.layer === 'ground') {
                var have = env.edges();
                if (!have) return done(refuse('refused', 'The edge pieces are not loaded yet: try again in a moment, or use fix_edges later.'));
                edges = { E: have.MapEdges, G: have.MapGround };
            }
            guarded(args, done, function () {
                var r = runStroke(args.layer === 'ground' ? 'ground' : 'objects', args.tile, args, edges);
                if (r.ok) said('Painted ' + r.cells + ' cells of ' + (args.layer === 'ground' ? (Tiles.kindOf(args.tile) || 'ground') : 'objects'));
                return r;
            });
        };
        tools.erase = function (args, done) {
            guarded(args, done, function () {
                var r = runStroke('erase', null, args, null);
                if (r.ok) said('Erased ' + r.cells + ' cells');
                return r;
            });
        };
        function put(change, text, rect) {
            if (change.error) return refuse('refused', change.error);
            var dirty = applyChange(change);
            if (dirty) said(text);
            return changeResult(dirty, rect);
        }
        tools.put_object = function (args, done) {
            var f = env.file(), big = bigIds()[args.tile], item = Palette.itemOf('objects', args.tile);
            if (!inside(f, args.row, args.column)) return done(refuse('refused', OUT_OF_MAP));
            if (!big && (!item || item.plant)) return done(refuse('bad_args', 'Tile ' + args.tile + ' is not an object: see list_tiles (objects, bigger).'));
            guarded(args, done, function () {
                if (big) return put(Objects.place(env.file(), big, args.row, args.column), 'Put ' + big.name + ' at row ' + args.row + ', column ' + args.column, { row: args.row + big.box.r0, column: args.column + big.box.c0, rows: big.box.r1 - big.box.r0 + 1, columns: big.box.c1 - big.box.c0 + 1 });
                var r = runStroke('objects', args.tile, { cells: [[args.row, args.column]] }, null);
                if (r.ok) said('Put an object at row ' + args.row + ', column ' + args.column);
                return r;
            });
        };
        tools.put_hill = function (args, done) {
            guarded(args, done, function () { return put(Blocks.placeHill(env.file(), TEAMS.indexOf(args.team), args.row, args.column), 'Put the ' + args.team + ' hill at row ' + args.row + ', column ' + args.column, { row: args.row - 1, column: args.column - 1, rows: 4, columns: 4 }); });
        };
        tools.put_ants = function (args, done) {
            var type = args.type ? Props.TYPES.map(function (t) { return t.key; }).indexOf(args.type) : 0;
            if (type > 0 && env.file().kind !== 'extended') return done(refuse('refused', 'Ant types are for Extended maps: this map is a Classic map.'));
            guarded(args, done, function () {
                var f = env.file(), before = Blocks.facts(f, false).ants[TEAMS.indexOf(args.team)], change = Blocks.placeStart(f, TEAMS.indexOf(args.team), args.row, args.column, args.count, type);
                var r = put(change, 'Put ' + args.count + ' ' + args.team + ' ants at row ' + args.row + ', column ' + args.column, { row: args.row, column: args.column, rows: 1, columns: 1 });
                if (r.ok) r.result.ants_now = Blocks.facts(env.file(), false).ants[TEAMS.indexOf(args.team)], r.result.ants_before = before;
                return r;
            });
        };
        tools.put_food = function (args, done) {
            var item = args.food ? Palette.itemOf('food', args.food) : null;
            if (args.food && !item) return done(refuse('bad_args', 'Tile ' + args.food + ' is not a food item: see list_tiles (food).'));
            guarded(args, done, function () {
                var stages = args.stages || (item ? item.stages : 3), custom = item || args.bites || args.points ? { id: item ? item.id : 0, units: args.bites || (item ? item.units : Blocks.FOOD[stages].units), points: args.points || (item ? item.points : Blocks.FOOD[stages].points) } : null;
                return put(Blocks.placePile(env.file(), args.row, args.column, stages, custom), 'Put a food pile at row ' + args.row + ', column ' + args.column, { row: args.row, column: args.column, rows: 1, columns: 1 });
            });
        };
        tools.put_plant = function (args, done) {
            var item = Palette.itemOf('plants', args.tile);
            if (!item) return done(refuse('bad_args', 'Tile ' + args.tile + ' is not a plant: see list_tiles (plants).'));
            guarded(args, done, function () { return put(Blocks.placePlant(env.file(), args.row, args.column, args.tile, args.dropper || 0), 'Put ' + item.label + ' at row ' + args.row + ', column ' + args.column, { row: args.row, column: args.column, rows: 1, columns: 1 }); });
        };
        tools.set_properties = function (args, done) {
            guarded(args, done, function () {
                var p = props(), type = args.default_ant ? Props.TYPES[Props.TYPES.map(function (t) { return t.key; }).indexOf(args.default_ant)] : null;
                if (p.error) return refuse('refused', p.error);
                var answer = env.core.setProperties(env.file().bytes, args.description !== undefined ? args.description : p.raw, args.minutes !== undefined ? args.minutes : p.minutes, args.eggs !== undefined ? args.eggs : p.eggs, type ? type.tile : p.tile, 'extended');
                if (answer.status !== 0) return refuse('tab_error', 'The core did not answer.');
                if (!answer.json.ok) return refuse('refused', answer.json.reasons && answer.json.reasons.length ? answer.json.reasons[0].message : 'The properties were refused.');
                if (!answer.json.changed) return okay({ version: ver, changed: 0 });
                var change = Props.changeOf(env.file().bytes, answer.json.edits, answer.bytes);
                if (!change) return refuse('tab_error', 'The properties could not be put into the map. Nothing was changed.');
                var dirty = applyChange(change);
                said('Set the properties of the map');
                return okay({ version: ver, changed: 1 }, 1);
            });
        };
        tools.fix_edges = function (args, done) {
            var have = env.edges();
            if (!have) return done(refuse('refused', 'The edge pieces are not loaded yet: try again in a moment.'));
            guarded(args, done, function () {
                var f = env.file(), rect = args.rect, region0 = rect ? { r0: rect.row, r1: rect.row + rect.rows - 1, c0: rect.column, c1: rect.column + rect.columns - 1 } : null;
                var made = have.MapEdges.plan(f, region0, have.MapGround);
                if (!made.count) return okay({ version: ver, changed: 0 });
                var dirty = applyChange(have.MapEdges.change(f, made, have.MapGround));
                said('Fixed the edges of ' + made.count + ' cells');
                return okay({ version: ver, changed: made.count }, made.count);
            });
        };
        tools.resize_map = function (args, done) {
            var f = env.file(), lim = env.limits, big = f.kind === 'extended', cells = args.rows * args.columns;
            if (big ? (cells > lim.extended.cells || Math.max(args.rows, args.columns) > lim.extended.longest) : (args.rows > lim.classic.rows || args.columns > lim.classic.columns || cells > lim.classic.cells))
                return done(refuse('refused', 'A ' + f.kind + ' map cannot be ' + args.rows + ' x ' + args.columns + ': the most is ' + (big ? Sizes.number(lim.extended.cells) + ' cells, ' + Sizes.number(lim.extended.longest) + ' on a side' : lim.classic.rows + ' x ' + lim.classic.columns + ' (' + Sizes.number(lim.classic.cells) + ' cells)') + '.'));
            guarded(args, done, function () {
                var g = env.file(), rules = g.kind === 'extended' ? Sizes.resizedRules(g.rules, g.rows, args.rows) : undefined;
                var change = Blocks.resizeMap(g, args.rows, args.columns, args.keep_row, args.keep_column, undefined, undefined, rules);
                if (change && change.error) return refuse('refused', change.error);
                var dirty = change && applyChange(change);
                if (dirty) said('Resized the map to ' + args.rows + ' x ' + args.columns);
                return okay({ version: ver, changed: dirty ? 1 : 0, rows: env.file().rows, columns: env.file().columns }, dirty ? 1 : 0);
            });
        };
        tools.new_map = function (args, done) {
            guarded(args, done, function () {
                var error = env.newMap(args.kind, args.rows, args.columns, args.name ? clean1(clampName(args.name, 40)) || 'My map' : 'My map');
                if (error) return refuse('refused', error);
                own = []; redoOwn = [];
                said('Started a new ' + args.kind + ' map ' + args.rows + ' x ' + args.columns);
                return okay({ version: ver, rows: env.file().rows, columns: env.file().columns, kind: env.file().kind });
            }, true);
        };
        // Undo and redo of the AI's own steps: a step is the AI's while it is at the top of the history; once the person has worked on the map the steps below are theirs to undo
        function stepsOf(args) { return args.steps || 1; }
        function undoOwn(n) {
            var h = env.editor().history, k = 0;
            while (k < n && own.length && h.top() === own[own.length - 1]) {
                var dirty = env.editor().undo();
                if (dirty === null || dirty === undefined) break;
                env.changed(dirty);
                redoOwn.push(own.pop()); k++;
            }
            return k;
        }
        tools.undo = function (args, done) {
            waitFor(done, function () {
                if (args.base !== undefined && args.base !== ver) return done(refuse('changed_by_person', 'The person changed the map since your last answer (it is version ' + ver + ' now). Look again with map_info.'));
                var k = undoOwn(stepsOf(args));
                if (!k) return done(refuse('changed_by_person', 'There is no step of yours at the top of the history: the person has worked on the map since. Ask them to undo.'));
                said('Undid ' + k + (k === 1 ? ' step' : ' steps'));
                done(okay({ version: ver, undone: k }, 0));
            }, 0);
        };
        tools.redo = function (args, done) {
            waitFor(done, function () {
                if (args.base !== undefined && args.base !== ver) return done(refuse('changed_by_person', 'The person changed the map since your last answer (it is version ' + ver + ' now). Look again with map_info.'));
                var h = env.editor().history, k = 0;
                while (k < stepsOf(args) && redoOwn.length && h.canRedo()) {
                    var dirty = env.editor().redo();
                    if (dirty === null || dirty === undefined) break;
                    env.changed(dirty);
                    own.push(redoOwn.pop()); k++;
                }
                if (!k) return done(refuse('refused', 'There is nothing of yours to redo.'));
                done(okay({ version: ver, redone: k }, 0));
            }, 0);
        };
        tools.save_draft = function (args, done) {
            waitFor(done, function () {
                var wanted = clean1(args.name ? clampName(args.name, 40) : env.name()) || 'My map', name = env.numbered(wanted);
                env.keep(name, function (error) {
                    done(error ? refuse('refused', 'The browser would not keep the draft. Nothing was saved.') : okay({ version: ver, saved_as: name }));
                });
            }, 0);
        };
        tools.note = function (args, done) { if (stopped(done)) return done(stoppedAnswer()); env.note(args.text); done(okay({ version: ver })); };

        return {
            tools: tools,
            // One call: `done(frame)` where frame is {ok: true, result, cells} or {ok: false, error, message}. A tool that is not in the table is refused (the hub does not send one).
            run: function (name, args, done, alive) {
                var tool = Object.prototype.hasOwnProperty.call(tools, name) ? tools[name] : null;
                if (!tool) return done(refuse('bad_args', 'There is no such tool.'));
                if (alive) done.alive = alive;                                              // (read by waitFor and guarded: the call's own check that it is still wanted)
                try { tool(args || {}, done); } catch (e) { done(refuse('tab_error', 'The Map Maker could not do that.')); }
            },
            names: function () { return Object.keys(tools).sort(); },
            // the map changed (a stroke, an undo, a new map: by anybody)
            touch: function () { ver++; },
            version: function () { return ver; },
            // a new arming: the safety copy is made again at the first change, and the steps of the last session are no longer the AI's to undo
            arm: function () { copied = false; own = []; redoOwn = []; },
            // the button "Undo AI changes": the AI's steps that are at the top of the history
            undoAll: function () { return undoOwn(1000000); },
            ownSteps: function () { return own.length; },
            region: region, infoOf: infoOf, checkOf: checkOf, pictureSize: pictureSize, hillsOf: hillsOf
        };
    }

    return { create: create, region: region, checkOf: checkOf, pictureSize: pictureSize, hillsOf: hillsOf, antsOf: antsOf, pilesOf: pilesOf, groundLetter: groundLetter, objectLetter: objectLetter, GUIDES: GUIDES, MAX_CELLS: MAX_CELLS };
});
