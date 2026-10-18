// The map maker's page (web/mapmaker.html; docs/MAP_EDITOR.md): starts the map core (refusing a module of another interface version), holds the map that is open (its bytes, what the core
// says of it) and joins the parts: the canvas, the side panel, New map, Open a file from the disk, the checks and Download. Nothing here judges a map: the core's answers are shown as they are.
(function () {
    'use strict';
    var doc = document, win = window;
    var NEEDED = ['LvlCore', 'createLvlCore', 'MapFile', 'MapTiles', 'MapSolid', 'MapEdit', 'MapBlocks', 'MapObjects', 'MapPalette', 'MapView', 'MapSizes', 'MapChecks', 'MapDialog', 'MapProps', 'MapCrop', 'MapConvert', 'MapStore', 'MapCanvas', 'MapTools'];
    var GROUND = 485, EGGS = 6, MINUTES = 10;          // a new map: plain grass (docs/MAP_FORMAT.md, "The blank map"), 6 eggs a team, 10 minutes
    var NEW_NAME = 'My map';

    function $(id) { return doc.getElementById(id); }
    function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }

    var opening = 0;
    var core = null, limits = null, dialogs = null, canvas = null, tools = null, store = null, map = null, checkTimer = 0, summed = null;
    var keepTimer = 0, keepReady = false, keepPending = false, keepEdits = 0;      // the unsaved work kept in the browser: a rest of 1.5 s after a change, and not before the work of the last visit was dealt with

    // The page cannot go on (a script did not load, or the module is of another version): say so in the canvas's place, with a way out
    function fatal(text) {
        var boot = $('boot');
        boot.hidden = false;
        boot.textContent = '';
        boot.appendChild(el('p', '', text));
        var again = el('button', 'btn', 'Reload the page');
        again.type = 'button';
        again.addEventListener('click', function () { win.location.reload(); });
        boot.appendChild(again);
    }

    // The core stops for good after an error of its own (docs/MAP_FORMAT.md, "In the browser"): every later answer is an error, so say it once and offer the reload
    function alive() {
        if (!core.isDead()) return true;
        fatal('The map code stopped after an error. Reload the page. (The map you were looking at is still in this window; download it first if you want to keep it.)');
        return false;
    }

    // The tags of a kind: "Classic map", or "Extended map" and the neutral badge (and the joined tag that mapmaker.css shows instead of the two below 360 px)
    function fillTags(host, kind) {
        host.textContent = '';
        if (kind === 'extended') {
            host.appendChild(el('span', 'tag tag-ext', 'Extended map'));
            host.appendChild(el('span', 'badge-nv', 'New version only'));
            host.appendChild(el('span', 'tag tag-join', 'Extended map \u00b7 New version only'));
        } else host.appendChild(el('span', 'tag tag-classic', 'Classic map'));
    }
    function setTags(id, kind) { fillTags($(id), kind); }

    // The numbers of the open map: the name, the kind, the size, the meter and the one line of the check (not the canvas: a view of the map stays where it is)
    function showFacts() {
        var f = map.file, kind = f.kind;
        $('map-name').value = map.name;
        doc.title = map.name + ' - Map maker - Ants (1998)';
        setTags('top-tags', kind); setTags('side-tags', kind);
        $('s-rows').textContent = f.rows; $('s-cols').textContent = f.columns;
        $('s-cells').textContent = MapSizes.number(f.cells);
        var facts = MapSizes.mapFacts(kind, f.cells, map.bytes.length, limits);
        $('s-file').textContent = facts.file;
        $('meter-fill').style.width = facts.percent + '%';
        $('meter').setAttribute('aria-valuemax', facts.max); $('meter').setAttribute('aria-valuenow', Math.min(f.cells, facts.max));
        $('meter-cap').textContent = facts.caption;
        showCheckLine();
        showSummary();
        $('convert-link').textContent = kind === 'classic' ? 'Start an Extended map from this one...' : 'Make a Classic map from this one...';
        var line = $('size-line');
        line.textContent = f.rows + ' x ' + f.columns + ' ';
        if (kind === 'extended') line.appendChild(el('span', 'badge-nv', 'New version only'));
    }

    // The properties in a few words (the side panel, the line under the map of a phone). They change only with a new array of bytes (a stroke writes in place), so they are read again only then.
    function showSummary() {
        if (summed && summed.bytes === map.bytes) return;
        var props = win.MapProps.read(core, map.bytes);
        summed = { bytes: map.bytes, props: props };
        var lines = props.error ? { description: '', time: '', ant: '', phone: '' } : win.MapProps.summary(props);
        $('s-desc').textContent = lines.description; $('s-time').textContent = lines.time; $('s-ant').textContent = lines.ant; $('phone-sum').textContent = lines.phone;
    }

    // The properties dialog: the core makes the new file, the history takes the pieces that changed as one step (Cancel changes nothing)
    function openProps() {
        if (canvas.isStroking() || dialogs.isOpen()) return;
        showSummary();
        if (summed.props.error) return dialogs.message('The properties cannot be opened', MapChecks.capital(summed.props.error) + '.');
        win.MapProps.open({
            doc: doc, dialogs: dialogs, core: core, props: summed.props, bytes: function () { return map.bytes; },
            apply: function (change) { return tools.act(change) ? '' : 'The properties could not be put into the map. Nothing was changed.'; }
        });
    }

    function showCheckLine() {
        var sum = MapChecks.summary(map.report);
        $('check-dot').className = 'dot ' + (sum.works ? 'ok' : 'bad');
        $('check-line').textContent = sum.text;
    }

    // The check of the map as it is now (the bytes the person is drawing on), as a place that takes both kinds judges it
    function runCheck() { return core.checkMap(map.bytes, 'extended'); }

    // After a change the check runs again once the hand has rested a moment (the card's button runs it at once)
    function checkSoon() {
        win.clearTimeout(checkTimer);
        checkTimer = win.setTimeout(function () {
            if (canvas.isStroking()) return checkSoon();
            var report = runCheck();
            if (report.status !== 0) return alive();
            map.report = report.json;
            showCheckLine();
        }, 600);
    }

    // The map has changed (a stroke, an undo, a name written in the dictionary): the numbers, and the check again soon
    function edited() {
        keepSoon();
        showFacts();
        $('check-dot').className = 'dot wait'; $('check-line').textContent = 'Checking\u2026';          // (not the old answer for the moment it takes)
        checkSoon();
    }

    // The dictionary grew, so the bytes are a new, longer array: the page reads them again and the canvas keeps its view
    function rebase(bytes) {
        var file = win.MapFile.parse(bytes);
        map.bytes = bytes; map.file = file;
        canvas.swapFile(file);
        return file;
    }

    // Take a map in: its check (as a place that takes both kinds judges it) and its grid. Returns an error sentence, or '' when the map is open.
    function setMap(bytes, name) {
        var report = core.checkMap(bytes, 'extended');
        if (report.status !== 0) { alive(); return report.error; }
        var file = win.MapFile.parse(bytes);
        if (!file.ok) return 'This file is not a map the map maker can draw: ' + file.error + '.';
        win.clearTimeout(checkTimer);
        map = { name: name, bytes: bytes, file: file, report: report.json };
        showFacts();
        canvas.setFile(file);
        tools.setMap(file);
        keepSoon();
        return '';
    }

    // A file from the disk: the core reads it first and says why when it does not
    function openBytes(bytes, name) {
        var read = core.readReport(bytes, 'extended');
        if (read.status !== 0) { alive(); return dialogs.message('This file cannot be opened', MapChecks.capital(read.error) + '.'); }
        if (!read.json.loaded) return dialogs.message('This file cannot be opened', MapChecks.capital(read.json.reason || 'It is not a map file') + '.');
        var error = setMap(bytes, name);
        if (error) dialogs.message('This file cannot be opened', error);
    }

    function openFile(file) {
        var cap = core.limits('extended').json.read_bytes;
        if (file.size > cap) return dialogs.message('This file cannot be opened', 'This file is ' + (file.size / 1048576).toFixed(1) + ' MB; the map maker opens files of up to ' + Math.round(cap / 1048576) + ' MB.');
        var name = win.Array.from(file.name.replace(/\.[^.]*$/, '')).slice(0, 40).join('').replace(/^\s+|\s+$/g, '') || 'Opened map';      // (cut by characters, not in the middle of one)
        var mine = ++opening;                                                // two files chosen quickly: only the last one is opened
        var done = function (buffer) { if (mine === opening) openBytes(new Uint8Array(buffer), name); };
        if (file.arrayBuffer) file.arrayBuffer().then(done, function () { dialogs.message('This file cannot be opened', 'The file could not be read.'); });
        else { var reader = new win.FileReader(); reader.onload = function () { done(reader.result); }; reader.onerror = function () { dialogs.message('This file cannot be opened', 'The file could not be read.'); }; reader.readAsArrayBuffer(file); }
    }

    // A map that has been drawn on is handed over as the core writes it (so that it stays a valid file); one that has not been changed goes as it came
    function fileBytes() {
        if (tools.isClean()) return { bytes: map.bytes };
        var written = core.rewrite(map.bytes, 'extended');
        if (written.status !== 0) { alive(); return { error: MapChecks.capital(written.error) + '.' }; }
        return { bytes: written.bytes };
    }
    function download() {
        var made = fileBytes(), bytes = made.bytes;
        if (made.error) return dialogs.message('The file could not be made', made.error);
        var blob = new win.Blob([bytes], { type: 'application/octet-stream' }), url = win.URL.createObjectURL(blob), a = el('a');
        a.href = url; a.download = win.MapFile.downloadName(map.name);
        doc.body.appendChild(a); a.click(); doc.body.removeChild(a);
        tools.markSaved();
        keepSoon();
        win.setTimeout(function () { win.URL.revokeObjectURL(url); }, 10000);
    }

    // The work that is not saved is kept in the browser's one slot while the person draws, and the slot is emptied when the map is saved, downloaded or replaced (store: mapmaker_store.js)
    function keepNow() {
        win.clearTimeout(keepTimer);
        if (!keepReady || !map || store.status() !== 'ready') return;
        if (canvas.isStroking()) return keepSoon();
        var mine = keepEdits;
        if (tools.isSaved()) store.clearWork(function () { if (mine === keepEdits) keepPending = false; });
        else store.keepWork(map.name, map.file.kind, map.bytes, function (error) { if (!error && mine === keepEdits) keepPending = false; });
    }
    function keepSoon() {
        keepEdits++;
        keepPending = !!tools && !tools.isSaved();
        win.clearTimeout(keepTimer);
        if (keepReady) keepTimer = win.setTimeout(keepNow, 1500);
    }

    // Save as a new map: the map goes into the browser under a free name and the saved map takes the screen (the open one is never written over)
    function keepAs(name, done) {
        var made = fileBytes();
        if (made.error) return done(made.error);
        store.add(name, map.file.kind, made.bytes, function (error) {
            done(error === 'exists' ? error : error ? 'The browser would not keep the map. Nothing was saved. You can download it instead.' : '', made.bytes);
        });
    }
    function kept(bytes, name) {
        var error = setMap(bytes, name);
        if (error) return dialogs.message('The map could not be shown', error);
        $('announce').textContent = 'Saved as \u201c' + name + '\u201d.';
    }

    // The work of the last visit, when there is some: pick it up or throw it away (closing the box picks it up: nothing is lost by a key)
    function restoreWork() {
        store.readWork(function (work) {
            var decided = false;
            function end() { keepReady = true; keepSoon(); }
            function toss() { if (!decided) { decided = true; store.clearWork(end); } }
            function pick() {
                if (decided) return;
                decided = true;
                var error = setMap(new win.Uint8Array(work.bytes), work.name);
                if (error) { store.clearWork(); dialogs.message('The unsaved work cannot be opened', error); } else tools.markUnsaved();
                end();
            }
            if (!work || !work.bytes || !work.bytes.length || !tools.isClean()) return end();
            dialogs.open({
                title: 'Pick up your unsaved work?', cls: 'narrow', onClose: pick,
                build: function (body) { body.appendChild(el('p', 'fatalmsg', 'Last time you drew on \u201c' + MapStore.clean(work.name) + '\u201d and did not save it as a new map or download it. It was kept in this browser.')); },
                buttons: [{ id: 'pick', text: 'Pick it up', onClick: pick }, { id: 'toss', text: 'Throw it away', ghost: true, onClick: toss }]
            });
        });
    }

    // What is on the screen is lost when another map takes its place (the one slot of unsaved work holds the new map then): when it has been drawn on, ask first
    function leave(then) {
        if (tools.isSaved()) return then();
        var can = store.status() === 'ready';
        dialogs.open({
            title: 'Leave this map?', cls: 'narrow',
            build: function (body) { body.appendChild(el('p', 'fatalmsg', 'You have drawn on this map and it is not saved. ' + (can ? 'Save it as a new map or download it first, or leave without it.' : 'Download it first, or leave without it.'))); },
            buttons: (can ? [{ id: 'save', text: 'Save as a new map first', onClick: function () { converterOf().save(then); } }] : []).concat([
                { id: 'download', text: 'Download it first', onClick: function () { download(); then(); } }, { id: 'leave', text: 'Leave without saving', onClick: then }, { id: 'stay', text: 'Stay here', ghost: true }])
        });
    }

    function newMap() {
        win.MapDialog.newMap({
            doc: doc, dialogs: dialogs, limits: limits,
            make: function (kind, rows, columns) {
                var made = kind === 'classic' ? core.blank(rows, columns, GROUND, EGGS, MINUTES, 'new_map') : core.blankExtended(rows, columns, GROUND, EGGS, MINUTES);
                if (made.status === 2) alive();
                return made;
            },
            onMade: function (bytes) { var error = setMap(bytes, NEW_NAME); if (error) dialogs.message('The map could not be made', error); }
        });
    }

    function showChecks() {
        win.clearTimeout(checkTimer);
        var report = runCheck();
        if (report.status !== 0) return dialogs.message('The check did not run', MapChecks.capital(report.error) + '.');
        map.report = report.json;
        showCheckLine();
        dialogs.checks(report.json, { onShow: function (row, column) { canvas.showCell(row, column); $('map').focus(); } });
    }

    // Copy and convert (mapmaker_convert.js): the new map replaces the open one after the question about unsaved drawing
    function thumb(file) { return MapView.miniBase(file, 64, 64, canvasOf); }
    function canvasOf(w, h) { var c = el('canvas'); c.width = w; c.height = h; return c; }
    function openCrop(spec) {
        var file = win.MapFile.parse(spec.bytes);
        MapCrop.open({ doc: doc, $: $, host: $('crop'), editor: $('editor'), limits: limits.classic, rows: spec.rows, columns: spec.columns, name: spec.name, tags: function (h) { fillTags(h, 'extended'); },
            picture: function () { return MapView.miniBase(file, 640, 640, canvasOf); }, onUse: spec.onUse, onCancel: spec.onCancel });
    }
    function convertEnv() {
        return {
            doc: doc, dialogs: dialogs, core: core, limits: limits, thumb: thumb, tags: fillTags, crop: openCrop,
            taken: function (name) { return MapStore.key(name) === MapStore.key(map.name) || store.has(name); },
            canKeep: function () { return store.status() === 'ready'; }, libraryState: function () { return store.libraryState(); },
            keep: keepAs, onKept: kept, download: download,
            current: function () { var r = core.checkMap(map.bytes, 'extended'); return { name: map.name, bytes: map.bytes, file: map.file, kind: map.file.kind, report: r.status === 0 ? r.json : map.report }; },
            onMade: function (bytes, name) { leave(function () { var error = setMap(bytes, name); if (error) dialogs.message('The map could not be made', error); }); }
        };
    }
    var converter;
    function converterOf() { return converter || (converter = MapConvert.create(convertEnv())); }
    function convertWith(name) { return function () { if (!dialogs.isOpen() && !canvas.isStroking()) converterOf()[name](); }; }

    var ACTIONS = {
        'new': function () { leave(newMap); }, open: function () { leave(function () { $('file-in').click(); }); }, download: download, check: showChecks, props: openProps, copy: convertWith('copy'), save: convertWith('save'), convert: convertWith('convert'),
        menu: function () { dialogs.menu([{ text: 'New', onClick: ACTIONS['new'] }, { text: 'Open', onClick: ACTIONS.open }, { text: 'Copy...', onClick: ACTIONS.copy }, { text: 'Save as new...', onClick: ACTIONS.save }, { text: 'Properties...', onClick: ACTIONS.props }, { text: 'Download', onClick: download }]); }
    };

    function start(module) {
        core = LvlCore.wrap(module);
        store = MapStore.create({ idb: (function () { try { return win.indexedDB || null; } catch (e) { return null; } })(), fetch: win.fetch ? win.fetch.bind(win) : null });
        var small = core.limits('new_map'), big = core.limits('extended');
        if (small.status !== 0 || big.status !== 0) return fatal('The map code did not answer. Reload the page.');
        limits = MapSizes.limitsOf(small.json, big.json);
        dialogs = MapDialog.create(doc, $('dialogs'), [$('top'), $('body')]);
        canvas = MapCanvas.create({
            doc: doc, win: win, canvas: $('map'), minimap: $('minimap'), mini: $('mini'), miniCap: $('mini-cap'), where: $('where'),
            zoomIn: doc.querySelector('[data-act="zoom-in"]'), zoomOut: doc.querySelector('[data-act="zoom-out"]'),
            onCursor: function (cell) { $('announce').textContent = cell ? 'Row ' + cell.row + ', column ' + cell.column : ''; }       // (said once for a key or a tap, not for every move of a mouse)
        });
        tools = MapTools.create({
            doc: doc, win: win, canvas: canvas,
            host: {
                tools: doc.querySelectorAll('[data-tool]'), box: $('palette-box'), title: $('palette-title'), swatches: $('palette'), name: $('palette-name'), hint: $('tool-hint'),
                tile: $('tile-btn'), brushes: doc.querySelectorAll('[data-brush]'), fill: $('fill-btn'), undo: $('undo-btn'), redo: $('redo-btn'),
                stepper: $('stepper'), stepperLabel: $('stepper-label'), stepLess: $('step-less'), stepValue: $('step-val'), stepMore: $('step-more')
            },
            file: function () { return map.file; }, rebase: rebase, onEdit: edited, busy: function () { return dialogs.isOpen() || !$('crop').hidden; }, dialogs: dialogs, say: function (t, m) { dialogs.message(t, m); }
        });
        // The page asks before it is closed only when the work is not safe in the browser (the browser keeps nothing, or the last change has not been written yet)
        win.addEventListener('beforeunload', function (e) { if (map && !tools.isSaved() && (store.status() !== 'ready' || keepPending)) { e.preventDefault(); e.returnValue = ''; } });
        doc.addEventListener('visibilitychange', function () { if (doc.visibilityState === 'hidden') keepNow(); });
        Array.prototype.forEach.call(doc.querySelectorAll('[data-act]'), function (b) {
            var act = ACTIONS[b.getAttribute('data-act')];
            if (!act) return;
            b.addEventListener('click', function () { act(); });
            b.disabled = false;
        });
        $('map-name').disabled = false;
        $('map-name').addEventListener('input', function () { map.name = $('map-name').value; doc.title = map.name + ' - Map maker - Ants (1998)'; });
        $('file-in').addEventListener('change', function () { var f = $('file-in').files[0]; $('file-in').value = ''; if (f) openFile(f); });
        $('boot').hidden = true;
        var made = core.blank(31, 31, GROUND, EGGS, MINUTES, 'new_map');
        var error = made.status === 0 ? setMap(made.bytes, NEW_NAME) : made.error;
        if (error) return fatal('The first map could not be made: ' + error);
        store.loadLibrary();
        store.open(restoreWork);
    }

    function boot() {
        var missing = NEEDED.filter(function (n) { return !win[n]; });
        if (missing.length) return fatal(missing.indexOf('createLvlCore') >= 0 ? 'The map code did not load. Reload the page; if it keeps failing, try again later.' : 'A part of this page did not load (' + missing.join(', ') + '). Reload the page.');
        win.createLvlCore().then(function (module) {
            var found = LvlCore.moduleVersion(module);
            if (found !== LvlCore.EXPECTED_INTERFACE) return fatal('The map code is out of date (a copy of one of its files is old in this browser). Reload the page.');
            start(module);
        }, function (e) { fatal('The map code could not start: ' + (e && e.message ? e.message : e)); });
    }

    boot();
})();
