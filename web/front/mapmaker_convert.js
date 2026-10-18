// Copy and convert (docs/MAP_EDITOR.md): each way makes a NEW map under a free name and never touches the open one; the core says what fits and why not. A plain ES5 script.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_sizes.js'), require('./mapmaker_checks.js'), require('./mapmaker_props.js'), require('./mapmaker_store.js'));
    else root.MapConvert = factory(root.MapSizes, root.MapChecks, root.MapProps, root.MapStore);
})(this, function (Sizes, Checks, Props, Store) {
    'use strict';

    var MAX_NAME = 40;
    var capital = Checks.capital;
    function sentence(t) { var c = capital(String(t)); return /[.!?]$/.test(c) ? c : c + '.'; }
    var clean = Store.clean;
    function same(a, b) { return Store.key(a) === Store.key(b); }
    function room(name, tail) { return Array.from(name).slice(0, MAX_NAME - tail).join('').replace(/ +$/, ''); }
    // "Treasure" -> "Treasure 2" -> "Treasure 3" ("Heaven 0105" -> "Heaven 0106"), never over 40 characters, never a taken name
    function numbered(name, taken) {
        var m = /^(.*\S) (\d{1,9})$/.exec(clean(name)), base = m ? m[1] : clean(name), n = m ? parseInt(m[2], 10) + 1 : 2, width = m ? m[2].length : 0, tried, digits;
        do { digits = ('000000000' + n).slice(-Math.max(width, String(n).length)); tried = room(base, digits.length + 1) + ' ' + digits; n++; } while (taken(tried));
        return tried;
    }
    function suffixed(name, word) { return room(name, word.length + 1) + ' ' + word; }
    // "this map has 120 rows; a Classic map made now has at most 90 (ants ...): crop it ..." -> "this map has 120 rows and a Classic map made now has at most 90"
    function shortReason(message) { return String(message).replace(/( \(|:).*$/, '').replace('; ', ' and '); }
    // rows, columns, then the rest (the picture's order)
    function ordered(reasons) {
        var rank = function (r) { return /^rows_/.test(r.code) ? 0 : /^columns_/.test(r.code) ? 1 : 2; };
        return reasons.slice().sort(function (a, b) { return rank(a) - rank(b); });
    }

    // env: {doc, dialogs, core, limits, current() -> {name, bytes, file, kind, report}, thumb(file), tags(host, kind), onMade(bytes, name), crop(spec)}
    function create(env) {
        var doc = env.doc, core = env.core, limits = env.limits, count = 0, dialogs = env.dialogs;
        function h(tag, cls, kids, attrs) {
            var n = doc.createElement(tag), k;
            if (cls) n.className = cls;
            [].concat(kids || []).forEach(function (c) { n.appendChild(typeof c === 'string' ? doc.createTextNode(c) : c); });
            for (k in attrs || {}) n.setAttribute(k, attrs[k]);
            return n;
        }
        function button(cls, text, onClick) { var b = h('button', cls, text, { type: 'button' }); b.addEventListener('click', onClick); return b; }
        function say(title, error) { return dialogs.message(title, sentence(error)); }
        function note(cls, mark, text, extra) { return h('div', 'note ' + cls, [h('span', 'ic', mark, { 'aria-hidden': 'true' }), h('div', '', [h('div', '', text)].concat(extra || []))]); }
        function two(a, b) {
            return h('div', 'cboxes', [[a, 'ck', '✓'], [b, 'cc', '→']].map(function (s) {
                return h('div', 'cbox', [h('div', 'ch', s[0][0]), h('ul', '', s[0][1].map(function (t) { return h('li', '', [h('span', 'mk ' + s[1], s[2], { 'aria-hidden': 'true' }), h('span', '', t)]); }))]);
            }));
        }
        function sourceCard(cur) {
            var pic = env.thumb(cur.file), tags = h('span', 'tags'), head = cur.report && cur.report.headline || '';
            env.tags(tags, cur.kind);
            return h('div', 'srccard', [pic ? h('span', 'cthumb', pic) : '', h('div', 'st', [h('div', 'sh', [h('span', 'sn', cur.name), tags]),
                h('div', 'sl', cur.file.rows + ' x ' + cur.file.columns + (head ? ' · ' + head.charAt(0).toLowerCase() + head.slice(1) : ''))])]);
        }
        // a map is never replaced: a name that the open map, a library map or a draft has is taken (`taken`), and the next free one is offered in one click
        function nameField(label, value, taken, onChange) {
            var id = 'convert-name-' + (++count), input = h('input', '', [], { id: id, type: 'text', maxlength: MAX_NAME, autocomplete: 'off' }), status = h('div', '', [], { 'aria-live': 'polite' });
            var f = { box: h('div', 'cname', [h('label', '', label, { 'for': id }), input, status]), ok: false, edited: false };
            input.value = value;
            f.name = function () { return clean(input.value); };
            f.check = function () {
                var name = f.name(), next;
                status.textContent = ''; input.removeAttribute('aria-invalid');
                f.ok = !!name && !taken(name);
                if (f.ok && lib() === 'pending') { f.ok = false; status.appendChild(h('div', 'cfree', 'Checking the game\u2019s map names\u2026')); }       // (a game map's name cannot be known until its list has come)
                else if (f.ok) {
                    status.appendChild(h('div', 'cfree', [h('i', 'dot ok'), 'The name \u201c' + name + '\u201d is free.']));
                    if (lib() === 'failed') status.appendChild(h('div', 'cnever', 'The list of the game\u2019s maps could not be loaded, so a name that one of them has cannot be checked.'));
                } else {
                    input.setAttribute('aria-invalid', 'true');
                    next = name && numbered(name, taken);
                    status.appendChild(note('bad', '!', name ? 'A map called \u201c' + name + '\u201d already exists. A map is never replaced, so please pick another name.' : 'Give the new map a name.',
                        next ? [button('btn sm', 'Use \u201c' + next + '\u201d', function () { input.value = next; f.edited = true; f.check(); input.focus(); })] : []));
                }
                onChange();
            };
            input.addEventListener('input', function () { f.edited = true; f.check(); });
            if (lib() === 'pending') env.onLibrary(function () { f.check(); });
            return f;
        }
        function lib() { return env.libraryState ? env.libraryState() : 'ready'; }
        function takenFor(cur) { return env.taken || function (n) { return same(n, cur.name); }; }
        // a default name that is taken becomes the next free one
        function free(name, taken) { return taken(name) ? numbered(name, taken) : name; }
        function work(cur, name) { return { bytes: cur.bytes, sourceName: cur.name, name: name, crop: null }; }

        function copy(given) {                    // (a saved map of My maps is given: the open map is not the one that is copied)
            var cur = given || env.current(), ext = cur.kind === 'extended', choice = 'same', nm, cards = {}, ctl;
            var down = ext ? core.convertToClassic(cur.bytes, 'new_map') : null, fits = !ext || (down.status === 0 && down.json.ok);
            if (down && down.status !== 0) return say('The copy cannot be made', down.error);
            function card(which, title, kind, line, off) {
                var input = h('input', '', [], { type: 'radio', name: 'copy-kind' });
                input.disabled = !!off;
                input.addEventListener('change', function () { if (input.checked) { choice = which; show(); } });
                cards[which] = { label: h('label', 'kind' + (off ? ' off' : ''), [input, h('span', 'rb', [], { 'aria-hidden': 'true' }), h('span', 'kt', [title, h('span', 'kn', kind), (which === 'same') === ext ? h('span', 'badge-nv', 'New version only') : '']), h('span', 'kl', line)]), input: input };
                return cards[which].label;
            }
            function show() {
                Object.keys(cards).forEach(function (k) { cards[k].label.classList.toggle('on', k === choice); cards[k].input.checked = k === choice; });
                ctl.buttons.make.textContent = choice === 'same' ? 'Make new map' : 'Next: what changes';
                ctl.setDisabled('make', !nm.ok);
            }
            ctl = dialogs.open({
                title: 'Copy this map', focus: 'input:checked',
                build: function (body) {
                    var other = ext ? card('other', 'Classic map', ' (convert)', fits ? 'Works in the original game and in the new version.' : ['Not yet: ' + shortReason(ordered(down.json.reasons)[0].message) + '. ', button('link', 'See why and what to do', function () { ctl.close(); classic(work(cur, null)); })], !fits)
                        : card('other', 'Extended map', ' (start an Extended map from this one)', 'Bigger limits. Works in the new version only. Any Classic map can be the start.');
                    nm = nameField('Name of the new map', numbered(cur.name, takenFor(cur)), takenFor(cur), function () { if (ctl) show(); });
                    body.appendChild(sourceCard(cur));
                    body.appendChild(h('p', 'lead', 'Make a new map as:', { id: 'copy-lead' }));
                    body.appendChild(h('div', 'kinds stacked', [card('same', ext ? 'Extended map' : 'Classic map', ' (same kind)', ext ? 'Works in the new version only.' : 'Works in the original game and in the new version.'), other],
                        { role: 'radiogroup', 'aria-labelledby': 'copy-lead' }));
                    body.appendChild(nm.box);
                    body.appendChild(h('p', 'cnever', 'This makes a new map. “' + cur.name + '” is never changed or replaced.'));
                    nm.check();
                },
                buttons: [{ id: 'cancel', text: 'Cancel', ghost: true }, { id: 'make', text: 'Make new map', keepOpen: true, onClick: function (c) {
                    nm.check();
                    if (choice === 'same') { if (nm.ok) { c.close(); env.onMade(cur.bytes.slice(), nm.name()); } return; }
                    if (!nm.ok) return;
                    c.close();
                    if (ext) classic(work(cur, nm.edited ? nm.name() : null)); else startExtended(nm.edited ? nm.name() : null, cur);
                } }]
            });
            show();
            return ctl;
        }

        function startExtended(carried, given) {
            var cur = given || env.current(), nm, ctl, side = Math.round(Math.sqrt(limits.extended.cells));
            ctl = dialogs.open({
                title: 'Start an Extended map from this one?', focus: 'input',
                build: function (body) {
                    nm = nameField('Name of the new Extended map', carried || free(suffixed(cur.name, 'Extended'), takenFor(cur)), takenFor(cur), function () { if (ctl) ctl.setDisabled('make', !nm.ok); });
                    body.appendChild(h('p', 'lead', cur.name + ' (' + cur.file.rows + ' x ' + cur.file.columns + ') will be the start of a new Extended map.'));
                    body.appendChild(two(['What stays the same', ['How the map plays. Every tile, hill, food pile and ant stays where it is.', 'Your Classic map “' + cur.name + '”. It is never touched.']],
                        ['What changes', ['The new map is an Extended map: the original game cannot open it.', 'The limits get bigger: up to ' + side + ' x ' + side + ' cells.']]));
                    body.appendChild(nm.box); nm.check();
                },
                buttons: [{ id: 'cancel', text: 'Cancel', ghost: true }, { id: 'make', text: 'Make the new Extended map', keepOpen: true, onClick: function (c) {
                    nm.check();
                    if (!nm.ok) return;
                    var made = core.convertToExtended(cur.bytes);
                    c.close();
                    if (made.status !== 0 || !made.json.ok) return say('The Extended map could not be made', made.status !== 0 ? made.error : made.json.reasons[0].message);
                    env.onMade(made.bytes, nm.name());
                } }]
            });
            ctl.setDisabled('make', !nm.ok);
            return ctl;
        }

        // w: {bytes (the copy being worked on), sourceName, name (of the new map or null), crop: null or {row, column, rows, columns, notes}}
        function classic(w) {
            var res = core.convertToClassic(w.bytes, 'new_map');
            if (res.status !== 0) return say('The Classic map could not be made', res.error);
            return res.json.ok ? fits(w, res) : cannot(w, res);
        }

        function fits(w, res) {
            var name = w.name || free(suffixed(w.sourceName, 'Classic'), takenFor(env.current())), cr = w.crop, r = core.readReport(w.bytes, 'extended').json, c = limits.classic;
            var right = ['The copy keeps the limits of a Classic map: at most ' + c.rows + ' rows and ' + c.columns + ' columns.', 'The “New version only” badge goes away.'];
            if (cr) right.unshift('Only the part you chose is kept: rows ' + cr.row + ' to ' + (cr.row + cr.rows - 1) + ', columns ' + cr.column + ' to ' + (cr.column + cr.columns - 1) + '.');
            return dialogs.open({
                title: 'Make a Classic map from this one?',
                build: function (body) {
                    body.appendChild(h('p', 'lead', w.sourceName + (cr ? ', cropped to ' + r.rows + ' x ' + r.columns + ',' : ' (' + r.rows + ' x ' + r.columns + ', Extended map)') + ' fits a Classic map. The Classic map will be a new map.'));
                    body.appendChild(two(['What stays the same', [cr ? 'How the map plays inside the part you chose.' : 'How the map plays.', 'Your Extended map. It is never touched.']], ['What changes', right]));
                    (cr ? cr.notes : []).concat(res.json.notes).forEach(function (n) { body.appendChild(note('', 'i', sentence(n.message))); });
                },
                buttons: [{ id: 'cancel', text: 'Cancel', ghost: true }, { id: 'make', text: 'Make the new Classic map', onClick: function () { env.onMade(res.bytes, name); } }]
            });
        }

        function cannot(w, res) {
            var reasons = ordered(res.json.reasons), sizes = reasons.some(function (r) { return /^(rows_|columns_|anchor_)/.test(r.code); }), text = reasons.some(function (r) { return r.code === 'description_too_long'; }), c = limits.classic;
            function act(label, fn) { return button('btn sm', label, function () { dialogs.close(); fn(w); }); }
            return dialogs.open({
                title: 'This map cannot be a Classic map yet',
                build: function (body) {
                    body.appendChild(h('p', 'lead', 'A Classic map has to fit the original game. These things stop it:'));
                    body.appendChild(h('div', 'creasons', reasons.map(function (r) { return note('bad', '✕', capital(r.message)); })));
                    if (sizes || text) body.appendChild(h('div', 'cdo', 'What you can do'));
                    body.appendChild(h('div', 'cacts', [sizes ? act('Crop it to fit...', crop) : '', text ? act('Shorten the description...', shorten) : '']));
                    if (sizes) body.appendChild(h('p', 'chelp', 'Crop opens a movable frame of the biggest size a Classic map may have (' + c.rows + ' rows, up to ' + c.columns + ' columns, ' + Sizes.number(c.cells) + ' cells in all). Nothing changes until you save a copy.'));
                },
                buttons: [{ id: 'close', text: 'Close', ghost: true }, { id: 'keep', text: 'Keep it Extended' }]
            });
        }

        function crop(w) {
            var info = core.readReport(w.bytes, 'extended').json;
            env.crop({
                name: w.sourceName, bytes: w.bytes, rows: info.rows, columns: info.columns,
                onUse: function (f) {
                    var made = core.crop(w.bytes, f.row, f.column, f.rows, f.columns, 'extended'), was = w.crop || { row: 0, column: 0, notes: [] };
                    if (made.status !== 0 || !made.json.ok) return say('The part could not be made', made.status !== 0 ? made.error : made.json.reasons[0].message);
                    classic({ bytes: made.bytes, sourceName: w.sourceName, name: w.name, crop: { row: was.row + f.row, column: was.column + f.column, rows: f.rows, columns: f.columns, notes: was.notes.concat(made.json.notes) } });
                },
                onCancel: function () { classic(w); }
            });
        }

        // the Map properties box on the copy (the core's answer to Apply is the copy's new bytes); whatever closes it, the conversion is asked again
        function shorten(w) {
            var props = Props.read(core, w.bytes), next = w, last = null;
            if (props.error) return say('The description cannot be shortened', props.error);
            return Props.open({
                doc: doc, props: props, bytes: function () { return next.bytes; },
                core: { setProperties: function () { var a = core.setProperties.apply(core, arguments); if (a.status === 0 && a.json.ok) last = a; return a; } },
                dialogs: { message: dialogs.message, open: function (spec) { spec.onClose = function () { classic(next); }; return dialogs.open(spec); } },
                apply: function () { next = { bytes: last.bytes, sourceName: w.sourceName, name: w.name, crop: w.crop }; return ''; }
            });
        }

        // Save as a new map: the map on the screen goes into the browser as a NEW draft under a free name (`keep` adds it and never replaces one); `done` runs after it was kept
        function save(done) {
            var cur = env.current(), taken = takenFor(cur), nm, ctl, tail, busy = false;
            if (!env.canKeep()) {
                return dialogs.open({
                    title: 'This browser cannot keep maps', cls: 'narrow',
                    build: function (body) { body.appendChild(h('p', 'fatalmsg', 'This browser does not let the page keep files, so the map cannot be saved here. Download it to keep it on your device.')); },
                    buttons: [{ id: 'download', text: 'Download', onClick: env.download }, { id: 'close', text: 'Close', ghost: true }]
                });
            }
            function after() {                                  // the Save after this one: the next free number of the name that is chosen (or offered)
                var base = nm.ok ? nm.name() : numbered(nm.name() || cur.name, taken);
                tail.textContent = 'The editor then shows the new map, so the next Save makes one more new map (\u201c' + numbered(base, taken) + '\u201d).';
                if (ctl) ctl.setDisabled('make', !nm.ok);
            }
            ctl = dialogs.open({
                title: 'Save as a new map', focus: 'input',
                build: function (body) {
                    tail = h('p', 'cnever');
                    nm = nameField('Name of the new map', numbered(cur.name, taken), taken, after);
                    body.appendChild(h('p', 'lead', ['Your changes are saved as a ', h('b', '', 'new map'), '. The map you opened, \u201c' + cur.name + '\u201d, stays exactly as it was: no map is ever replaced.']));
                    body.appendChild(nm.box);
                    body.appendChild(tail);
                    nm.check();
                },
                buttons: [{ id: 'cancel', text: 'Cancel', ghost: true }, { id: 'make', text: 'Save as a new map', keepOpen: true, onClick: function (c) {
                    nm.check();
                    if (!nm.ok || busy) return;
                    busy = true;
                    env.keep(nm.name(), function (error, bytes) {
                        busy = false;
                        if (error === 'exists') return nm.check();                  // another window made it first: the name is taken now
                        c.close();
                        if (error) return say('The map could not be saved', error);
                        env.onKept(bytes, nm.name());
                        if (done) done();
                    });
                } }]
            });
            ctl.setDisabled('make', !nm.ok);
            return ctl;
        }

        function convert() { var cur = env.current(); return cur.kind === 'classic' ? startExtended(null) : classic(work(cur, null)); }
        return { copy: copy, save: save, startExtended: startExtended, classic: classic, convert: convert, shorten: shorten, crop: crop };
    }

    return { create: create, numbered: numbered, suffixed: suffixed, shortReason: shortReason, ordered: ordered };
});
