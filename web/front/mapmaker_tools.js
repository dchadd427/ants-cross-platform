// The map maker's tools in the page (docs/MAP_EDITOR.md): which tool is chosen, the palette of the tool (tiles, or the team and the number of ants, or the stages of a pile), the brush, the
// fill, undo and redo, the keys for them, and the painter that the canvas calls while a mouse or a finger paints or taps (web/front/mapmaker_canvas.js). The arithmetic of a stroke, the patches
// and the history are web/front/mapmaker_edit.js; hills, ants and food piles are web/front/mapmaker_blocks.js; bigger objects are web/front/mapmaker_objects.js; the tools and their tiles are
// web/front/mapmaker_palette.js. This script only
// joins them to the buttons. A browser script (it needs a document).
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_edit.js'), require('./mapmaker_palette.js'), require('./mapmaker_blocks.js'), require('./mapmaker_tiles.js'), require('./mapmaker_objects.js'));
    else root.MapTools = factory(root.MapEdit, root.MapPalette, root.MapBlocks, root.MapTiles, root.MapObjects);
})(this, function (Edit, Palette, Blocks, Tiles, Objects) {
    'use strict';

    var HINTS = {
        ground: 'Tap or click to paint the ground. Drag to paint a line. Fill works for one click.',
        objects: 'Tap or click to put an object. Drag to put a line of them.',
        bigger: 'Tap or click to put the object whole. The dot is the cell you tap. Erase takes it away whole.',
        hills: 'Tap or click to put the hill. The dot is its door. Put it again to move it.',
        ants: 'Tap or click to start the team\u2019s ants here. Put them again to move them.',
        food: 'Tap or click to put a food pile.',
        erase: 'Tap or click to take something away: an object, a hill, a team\u2019s ants or a pile.',
        move: 'Drag to look around. Wheel or two fingers to zoom.'
    };
    var PHONE_HINTS = {
        ground: 'Tap to paint. Drag for a line.', objects: 'Tap to put one. Drag for a line.', bigger: 'Tap to put one whole. Erase takes it whole.', hills: 'Tap to put the hill. Tap again to move it.', ants: 'Tap to start the ants here.',
        food: 'Tap to put a food pile.', erase: 'Tap to take something away.', move: 'Drag to look around.'
    };

    function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }

    // env: {doc, win, canvas (MapCanvas), host: {tools (buttons with data-tool), box, title, swatches, name, tile (the phone's one button for the palette), hint, brushes (buttons with data-brush), fill, undo, redo,
    //       stepper, stepperLabel, stepLess, stepValue, stepMore},
    //       file() -> the open map's reader view, rebase(bytes) -> the reader view of other bytes (the dictionary grew, or a list changed its length), onEdit() after a change, busy() -> a dialog is open,
    //       dialogs (MapDialog.create: the palette of a phone is a sheet), say(title, text) -> a plain message}
    function create(env) {
        var doc = env.doc, canvas = env.canvas, host = env.host;
        var state = { tool: 'ground', ground: 485, objects: 302, big: Palette.BIGGER[0][0], brush: 1, fill: false, team: 0, ants: 4, stages: 4 };
        var editor = null, current = null, told = false, fits = '';                     // `fits`: a note when fewer ants than asked for found room, until the next choice
        var ground = null, sprites = null;                                              // the lazily loaded parts (mapmaker_ground.js, mapmaker_sprites.js): the kinds' sheets and the game's pictures
        var ICONS = { ground: [0, 485], objects: [1, 302], bigger: [1, 1323], hills: [1, 248], food: [1, 264] };           // the tool buttons' pictures: [layer 2?, tile]

        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function paints() { return Palette.paints(state.tool); }
        function places() { return Palette.places(state.tool); }
        function hasTeams() { return state.tool === 'hills' || state.tool === 'ants'; }
        // a picture in a chip (the atlas), or false when there is none yet (the chip keeps its flat color)
        function picture(node, layer2, id, px) { return !!sprites && sprites.ready() && sprites.chip(node, layer2, id, px); }
        // the palette's item of a tile, or any ground tile that the sheet of a kind offers (the game's own name goes into the map's dictionary)
        function itemOf(tool, id) {
            var item = Palette.itemOf(tool, id);
            if (!item && tool === 'ground' && ground && ground.info(id)) item = { id: id, name: ground.name(id), label: ground.label(id), kind: ground.kindOf(id), color: Tiles.css(id, Tiles.EMPTY2), custom: true };
            return item;
        }
        function kindItem(item) { return Palette.items('ground').filter(function (k) { return k.kind === item.kind; })[0]; }          // (the kind's chip: its plain tile)
        function tileOf() { return state.tool === 'ground' ? state.ground : (state.tool === 'objects' ? state.objects : null); }
        function teamNow() { return Palette.teams()[state.team]; }
        function bigNow() { return Palette.itemOf('bigger', state.big); }
        function bigText(item) { return item.label + ' \u00b7 ' + plural(item.cells.length, 'cell', 'cells'); }
        // the name and, under it, the cells, in a node (a button or a pick): two lines, so that a long name does not break in the middle
        function bigLines(node, item) {
            var b = item.box;
            node.textContent = '';
            node.appendChild(el('b', 'pkn', plural(item.cells.length, 'cell', 'cells') + ', ' + (b.c1 - b.c0 + 1) + ' x ' + (b.r1 - b.r0 + 1)));
            node.appendChild(el('small', 'pkc', item.label));
        }
        // (the squares are <em>, not <span>: the text of the palette button is the first <span> in it)
        // the shape of a bigger object in its chip: a small square for each cell of its box, filled where the object stands, the anchor outlined
        function footprint(chip, item) {
            var b = item.box, rows = b.r1 - b.r0 + 1, cols = b.c1 - b.c0 + 1, size = Math.max(3, Math.min(9, Math.floor(26 / Math.max(rows, cols))));
            chip.textContent = ''; chip.className = 'fp';
            chip.style.background = 'none';
            chip.style.gridTemplateColumns = 'repeat(' + cols + ', ' + size + 'px)'; chip.style.gridAutoRows = size + 'px';
            for (var r = b.r0; r <= b.r1; r++) for (var c = b.c0; c <= b.c1; c++) {
                var cell = el('em', r === 0 && c === 0 ? 'org' : '');
                if (item.cells.some(function (rc) { return rc[0] === r && rc[1] === c; })) cell.style.background = item.color;
                chip.appendChild(cell);
            }
        }
        function heading(tool) { return tool === 'bigger' ? 'Bigger objects' : Palette.tool(tool).label; }

        // ---- the buttons ----
        function showButtons() {
            host.undo.disabled = !editor || !editor.history.canUndo();
            host.redo.disabled = !editor || !editor.history.canRedo();
            var on = paints();
            host.fill.disabled = !on || state.tool === 'erase';
            host.fill.setAttribute('aria-pressed', String(state.fill && !host.fill.disabled));
            Array.prototype.forEach.call(host.brushes, function (b) {
                var n = +b.getAttribute('data-brush');
                b.disabled = !on;
                b.setAttribute('aria-checked', String(n === state.brush));
                b.tabIndex = n === state.brush ? 0 : -1;
            });
        }

        function showTools() {
            Array.prototype.forEach.call(host.tools, function (b) {
                var tool = Palette.tool(b.getAttribute('data-tool'));
                b.disabled = !(tool && Palette.usable(tool.id));
                b.setAttribute('aria-pressed', String(tool.id === state.tool));
            });
            var hasPalette = !!(Palette.tool(state.tool) || {}).palette, panel = host.box.parentNode;
            if (panel && panel.classList) panel.classList.toggle('tilemode', state.tool === 'bigger');          // (a long list: the one button and its sheet, on a desktop too)
            host.box.hidden = !hasPalette;
            host.tile.hidden = !hasPalette;
            host.hint.textContent = '';
            host.hint.appendChild(el('span', 'dsk', HINTS[state.tool]));
            host.hint.appendChild(el('span', 'mob', PHONE_HINTS[state.tool]));
            if (hasPalette) showPalette();
            showButtons();
            canvas.refresh();
        }

        // What the map has of the thing that the tool puts, in a few words (the line under the palette)
        function infoText() {
            if (state.tool === 'ground' || state.tool === 'objects') {
                var item = itemOf(state.tool, tileOf());
                if (!item) return '';
                return state.tool === 'objects' ? item.label : (item.custom ? item.label + ' (' + item.name + ')' : item.label + (ground ? ': tap it again for all its tiles' : ''));
            }
            if (state.tool === 'bigger') return bigText(bigNow());
            var file = env.file ? env.file() : null;
            if (!file || !(state.tool === 'hills' || state.tool === 'ants' || state.tool === 'food')) return '';
            var facts = Blocks.facts(file, state.tool === 'hills'), who = teamNow().label, set = Blocks.FOOD[state.stages];
            if (state.tool === 'hills') return who + ': ' + (facts.hills[state.team] ? 'has a hill' : 'no hill yet');
            if (state.tool === 'ants') return who + ': ' + (facts.ants[state.team] ? plural(facts.ants[state.team], 'ant', 'ants') + fits : 'no ants yet');
            return plural(set.units, 'bite', 'bites') + ' of ' + set.points + ' points. ' + (facts.piles < 0 ? '' : plural(facts.piles, 'pile', 'piles') + ' on the map.');
        }

        function stepValue() { return state.tool === 'ants' ? state.ants : state.stages; }
        function stepRange() { return state.tool === 'ants' ? [Blocks.MIN_ANTS, Blocks.MAX_ANTS] : [Blocks.MIN_STAGES, Blocks.MAX_STAGES]; }
        function stepLabel() { return state.tool === 'ants' ? 'Ants at the start' : 'Stages of the pile'; }
        function stepNoun() { return state.tool === 'ants' ? 'ants' : 'stages'; }
        function stepBy(d) {
            var r = stepRange(), v = Math.max(r[0], Math.min(r[1], stepValue() + d));
            if (state.tool === 'ants') state.ants = v; else state.stages = v;
            fits = '';
            showPalette();
        }

        function showInfo() { if (host.name) host.name.textContent = infoText(); }

        // The team swatches: a chip and the team's name
        function teamButton(team, cls, onPick) {
            var b = el('button', cls), chip = el('i'), on = team.id === state.team;
            b.type = 'button'; b.setAttribute('role', 'radio'); b.setAttribute('aria-checked', String(on)); b.setAttribute('data-tile', String(team.id));
            chip.className = 'tc'; chip.style.background = team.color; if (state.tool === 'hills' && picture(chip, true, team.hill, 26)) chip.className = 'tc pic'; b.appendChild(chip); b.appendChild(el('span', '', team.label));
            b.addEventListener('click', function () { onPick(team.id); });
            return b;
        }

        function showPalette() {
            var tool = state.tool, label = heading(tool);
            host.title.textContent = label;
            host.swatches.textContent = '';
            host.tile.querySelector('i').style.border = '';
            host.stepper.hidden = !(tool === 'ants' || tool === 'food');
            host.swatches.hidden = tool === 'food' || tool === 'bigger';
            host.swatches.className = hasTeams() ? 'swatches teams' : (tool === 'ground' ? 'swatches grounds' : 'swatches');
            var chip = host.tile.querySelector('i'), text = host.tile.querySelector('span');
            chip.textContent = '';
            if (hasTeams()) {
                host.swatches.setAttribute('aria-label', 'Team of the ' + (tool === 'hills' ? 'hill' : 'ants'));
                Palette.teams().forEach(function (team) {
                    var b = teamButton(team, 'swatch team', chooseTeam), on = team.id === state.team;
                    b.tabIndex = on ? 0 : -1; b.title = team.label;
                    host.swatches.appendChild(b);
                });
                chip.className = 'tc'; chip.style.background = teamNow().color; if (tool === 'hills' && picture(chip, true, teamNow().hill, 26)) chip.className = 'tc pic';
                text.textContent = tool === 'ants' ? teamNow().label + ' · ' + plural(state.ants, 'ant', 'ants') : teamNow().label;
                host.tile.setAttribute('aria-label', label + ': ' + text.textContent + '. Change');
            } else if (tool === 'bigger') {
                var big = bigNow();
                if (!picture(chip, true, big.id, 30)) footprint(chip, big);
                bigLines(text, big);
                host.tile.setAttribute('aria-label', label + ': ' + bigText(big) + '. Change');
            } else if (tool === 'food') {
                chip.className = ''; chip.style.background = Tiles.FOOD; picture(chip, true, Blocks.FOOD[state.stages].stages[0][1], 26);
                text.textContent = plural(state.stages, 'stage', 'stages');
                host.tile.setAttribute('aria-label', label + ': ' + text.textContent + '. Change');
            } else {
                var list = Palette.items(tool), chosen = tileOf(), have = itemOf(tool, chosen), kinded = tool === 'ground';
                host.swatches.setAttribute('aria-label', label + ' to paint with');
                list.forEach(function (item) {
                    var b = el('button', kinded ? 'swatch gk' : 'swatch'), on = kinded ? !!have && item.kind === have.kind : item.id === chosen;
                    b.type = 'button'; b.setAttribute('role', 'radio'); b.setAttribute('aria-checked', String(on)); b.tabIndex = on ? 0 : -1;
                    b.setAttribute('aria-label', item.label); b.title = item.label + ' (' + item.name + ')' + (kinded && on && ground ? '. Tap again for all its tiles.' : '');
                    b.setAttribute('data-tile', String(item.id));
                    var swatchChip = el('i'); swatchChip.style.background = item.color; picture(swatchChip, !kinded, item.id, kinded ? 34 : 26); b.appendChild(swatchChip);
                    if (kinded) b.appendChild(el('span', '', item.label));
                    b.addEventListener('click', function () { if (kinded) pickKind(item, on); else chooseTile(item.id); });
                    host.swatches.appendChild(b);
                });
                if (have) {
                    chip.className = ''; chip.style.background = have.color;
                    text.textContent = have.custom ? kindItem(have).label + ' \u00b7 ' + have.name : have.label;
                    picture(chip, !kinded, have.id, 26);
                    host.tile.setAttribute('aria-label', label + ': ' + text.textContent + '. Change');
                }
            }
            if (!host.stepper.hidden) {
                var r = stepRange();
                host.stepperLabel.textContent = stepLabel(); host.stepValue.textContent = String(stepValue());
                host.stepLess.setAttribute('aria-label', 'Fewer ' + stepNoun()); host.stepMore.setAttribute('aria-label', 'More ' + stepNoun());
                host.stepLess.disabled = stepValue() <= r[0]; host.stepMore.disabled = stepValue() >= r[1];
            }
            showInfo();
        }

        // A stepper made for a sheet: a label, the buttons and the number; the page's own (beside the map) is in the markup
        function sheetStepper(onChange) {
            var row = el('div', 'stepper sheet'), less = el('button', 'stp', '−'), more = el('button', 'stp', '+'), val = el('output', 'stv'), r = stepRange();
            less.type = 'button'; more.type = 'button'; less.setAttribute('aria-label', 'Fewer ' + stepNoun()); more.setAttribute('aria-label', 'More ' + stepNoun()); val.setAttribute('aria-live', 'polite');
            function show() { val.textContent = String(stepValue()); less.disabled = stepValue() <= r[0]; more.disabled = stepValue() >= r[1]; }
            less.addEventListener('click', function () { stepBy(-1); show(); onChange(); });
            more.addEventListener('click', function () { stepBy(1); show(); onChange(); });
            row.appendChild(el('span', 'stl', stepLabel()));
            var group = el('span', 'stbtns'); group.appendChild(less); group.appendChild(val); group.appendChild(more); row.appendChild(group);
            show();
            return row;
        }

        // A phone has no room for the palette beside the map: its one button opens the same choices as a sheet, with their names
        function openPicker() {
            if (!env.dialogs || env.busy()) return;
            var tool = state.tool, label = heading(tool);
            env.dialogs.open({
                title: label, cls: 'narrow',
                build: function (body, ctl) {
                    var grid = el('div', 'pickgrid');
                    if (hasTeams()) {
                        grid.setAttribute('role', 'radiogroup'); grid.setAttribute('aria-label', 'Team of the ' + (tool === 'hills' ? 'hill' : 'ants'));
                        Palette.teams().forEach(function (team) {
                            grid.appendChild(teamButton(team, 'pick', function (id) { chooseTeam(id); ctl.close(); }));
                        });
                        body.appendChild(grid);
                    } else if (tool !== 'food') {
                        var list = Palette.items(tool), chosen = tool === 'bigger' ? state.big : tileOf(), have = itemOf(tool, chosen);
                        grid.setAttribute('role', 'radiogroup'); grid.setAttribute('aria-label', tool === 'bigger' ? label : label + ' to paint with');
                        list.forEach(function (item) {
                            var b = el('button', 'pick'), chip = el('i'), on = tool === 'ground' ? !!have && item.kind === have.kind : item.id === chosen;
                            b.type = 'button'; b.setAttribute('role', 'radio'); b.setAttribute('aria-checked', String(on)); b.setAttribute('data-tile', String(item.id));
                            if (tool === 'bigger') { if (!picture(chip, true, item.id, 30)) footprint(chip, item); } else { chip.style.background = item.color; picture(chip, tool !== 'ground', item.id, 30); }
                            b.appendChild(chip);
                            var who = el('span', '', ''); if (tool === 'bigger') { bigLines(who, item); b.setAttribute('aria-label', bigText(item)); } else who.textContent = item.label;
                            b.appendChild(who);
                            b.addEventListener('click', function () { if (tool === 'ground' && on && ground) return pickKind(item, true); chooseTile(item.id); ctl.close(); });
                            grid.appendChild(b);
                        });
                        body.appendChild(grid);
                        if (tool === 'bigger') body.appendChild(el('p', 'pickinfo', 'Each one covers several cells. A tap puts it whole, and Erase takes it away whole.'));
                        if (tool === 'ground') body.appendChild(el('p', 'pickinfo', 'The ground changes how fast ants walk. Tap one to paint with it' + (ground ? ', or tap the chosen one again for all its tiles.' : '.')));
                    }
                    if (tool === 'ants' || tool === 'food') {
                        var about = el('p', 'pickinfo', infoText());
                        body.appendChild(sheetStepper(function () { about.textContent = infoText(); }));
                        body.appendChild(about);
                    }
                },
                focus: tool === 'food' ? '.stp:not([disabled])' : '.pick[aria-checked="true"]',
                buttons: (tool === 'ground' && ground && itemOf('ground', state.ground) ? [{ text: 'All ' + kindItem(itemOf('ground', state.ground)).label.toLowerCase() + ' tiles', onClick: function () { pickKind(kindItem(itemOf('ground', state.ground)), true); } }] : []).concat([{ text: 'Close', ghost: true }])
            });
        }

        // A kind's chip: the first tap paints with its plain tile, a tap on the chosen kind opens the sheet of all its tiles (S5): any single tile can be painted
        function pickKind(item, chosen) {
            if (!chosen || !ground) return chooseTile(item.id);
            var k = ground.kind(item.kind);
            if (!env.dialogs) return;
            env.dialogs.open({
                title: k.label + ': all its tiles', cls: 'ksheetbox', focus: '.gt[aria-checked="true"], .gl[aria-checked="true"]',
                build: function (body) {
                    ground.sheet({
                        doc: doc, kind: k.key, chosen: function () { return state.ground; }, choose: function (id) { state.ground = id; showPalette(); },
                        pic: function (node, id, px) { node.style.background = Tiles.css(id, Tiles.EMPTY2); picture(node, false, id, px); },
                        brush: function () { return state.brush; }, setBrush: function (n) { state.brush = n; showButtons(); canvas.refresh(); }
                    }, body);
                },
                buttons: [{ text: 'Close', ghost: true }]
            });
        }

        function chooseTile(id) {
            if (state.tool === 'ground') state.ground = id; else if (state.tool === 'bigger') state.big = id; else state.objects = id;
            showPalette();
        }

        function chooseTeam(id) { state.team = id; fits = ''; showPalette(); }

        function chooseTool(id) {
            if (!Palette.usable(id)) return;
            if (canvas.isStroking()) return;
            state.tool = id; fits = '';
            showTools();
        }

        // ---- undo and redo ----
        function changed(dirty) {
            if (!dirty) return;
            canvas.markDirty(dirty);
            canvas.refreshMini();
            showButtons();
            showInfo();
            env.onEdit();
        }
        function undo() { if (editor && !canvas.isStroking()) changed(editor.undo()); }
        function redo() { if (editor && !canvas.isStroking()) changed(editor.redo()); }

        // ---- the painter: what the canvas calls ----
        function startStroke() {
            var kind = state.tool, id = tileOf();
            if (id !== null) {
                var item = itemOf(kind, id), made = Edit.ensureTile(env.file(), id, item ? item.name : '');
                if (made.error) { if (env.say) env.say('This tile cannot be used', 'The map already holds as many different tiles as a map file can name.'); return null; }
                if (made.grew) editor.rebase(env.rebase(made.bytes));
                if (made.named || made.grew) env.onEdit();
            }
            return editor.begin(kind, id);
        }

        // A whole hill, a team's ants, a food pile or a bigger object, put with one tap
        function place(cell) {
            var file = env.file(), change;
            if (state.tool === 'hills') change = Blocks.placeHill(file, state.team, cell.row, cell.column);
            else if (state.tool === 'ants') change = Blocks.placeStart(file, state.team, cell.row, cell.column, state.ants);
            else if (state.tool === 'bigger') change = Objects.place(file, bigNow(), cell.row, cell.column);
            else change = Blocks.placePile(file, cell.row, cell.column, state.stages);
            if (change.error) { if (env.say) env.say('It cannot be put there', change.error); return; }
            var was = env.file(), dirty = editor.act(change);
            fits = '';
            if (state.tool === 'ants') { var put = Blocks.facts(env.file(), false).ants[state.team]; if (put < state.ants) fits = ' (only ' + put + ' fit)'; }
            if (dirty) changed(dirty);
            else { showInfo(); if (env.file() !== was) env.onEdit(); }                    // (nothing changed on the map, but the dictionary may have got a name: the page is told)
        }

        var painter = {
            // the size of the square that the canvas marks under the pointer: the brush, a hill (4 x 4), a cell for ants and food (a bigger object marks its own box: shape); 0 for a tool that does not mark
            size: function () { return !editor ? 0 : (paints() ? state.brush : (places() ? (state.tool === 'hills' ? Blocks.HILL : 1) : 0)); },
            // the picture that follows the pointer: a bigger object's tile id (0 for the other tools)
            ghost: function () { return editor && state.tool === 'bigger' ? state.big : 0; },
            // a tool that puts one whole thing with a tap: a drag looks around instead of painting
            tapOnly: function () { return !!editor && places(); },
            // the box of a bigger object, counted from the cell that is tapped
            shape: function () { return editor && state.tool === 'bigger' ? bigNow().box : null; },
            // the hill's door is marked in the square, and the cell that anchors a bigger object
            door: function () { return !!editor && (state.tool === 'hills' || state.tool === 'bigger'); },
            begin: function (cell) {
                if (!editor) return false;
                if (places()) { place(cell); return false; }
                if (!paints()) return false;
                current = startStroke();
                told = false;
                if (!current) return false;
                if (state.fill && state.tool !== 'erase') {
                    current.fill(cell.row, cell.column);
                    canvas.markDirty(current.takeDirty());
                    painter.end();
                    state.fill = false;                                    // (Fill is for one click: the next press paints again)
                    showButtons();
                    return false;
                }
                var changed = current.stamp(cell.row, cell.column, state.brush);
                canvas.markDirty(current.takeDirty());
                var why = current.refusal();
                if (why) {
                    told = true;
                    if (env.say) env.say('It cannot be put there', why);
                    if (!changed) { current.end(); current = null; return false; }
                }
                return true;
            },
            extend: function (from, to) {
                if (!current) return;
                current.segment(from.row, from.column, to.row, to.column, state.brush);
                canvas.markDirty(current.takeDirty());
                var why = current.refusal();
                if (why && !told) { told = true; if (env.say) env.say('It cannot be put there', why); }
            },
            end: function () {
                if (!current) return;
                var n = current.end();
                current = null;
                if (n) { canvas.refreshMini(); showButtons(); showInfo(); env.onEdit(); }
            },
            cancel: function () {
                if (!current) return;
                var d = current.cancel();
                current = null;
                canvas.markDirty(d);
            }
        };

        // ---- the keys ----
        function typing(t) { var tag = t && t.tagName ? t.tagName.toLowerCase() : ''; return tag === 'input' || tag === 'textarea' || tag === 'select' || !!(t && t.isContentEditable); }

        function onKey(e) {
            if (e.defaultPrevented || !editor || env.busy() || typing(e.target)) return;
            var mod = e.ctrlKey || e.metaKey, k = String(e.key).toLowerCase();
            if (mod && !e.altKey) {
                if (k === 'z') { e.preventDefault(); if (e.shiftKey) redo(); else undo(); }
                else if (k === 'y') { e.preventDefault(); redo(); }
                return;
            }
            if (mod || e.altKey) return;
            var tool = Palette.toolOfKey(k);
            if (tool && k.length === 1) { if (Palette.usable(tool.id)) { e.preventDefault(); chooseTool(tool.id); } return; }
            if (k === 'b' && !host.fill.disabled) { e.preventDefault(); state.fill = !state.fill; showButtons(); }
            else if ((k === '[' || k === ']') && paints()) {
                e.preventDefault();
                var at = Edit.BRUSHES.indexOf(state.brush) + (k === ']' ? 1 : -1);
                state.brush = Edit.BRUSHES[Math.max(0, Math.min(Edit.BRUSHES.length - 1, at))];
                showButtons(); canvas.refresh();
            }
        }

        function arrows(e) {              // the palette is one group: arrows go from swatch to swatch
            var list = Array.prototype.slice.call(host.swatches.querySelectorAll('.swatch')), at = list.indexOf(doc.activeElement), step = 0;
            if (e.key === 'ArrowRight' || e.key === 'ArrowDown') step = 1; else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') step = -1; else return;
            e.preventDefault();
            var next = list[(at + step + list.length) % list.length];
            if (next) { var id = +next.getAttribute('data-tile'); if (hasTeams()) chooseTeam(id); else chooseTile(id); host.swatches.querySelector('[aria-checked="true"]').focus(); }
        }

        // ---- wiring ----
        Array.prototype.forEach.call(host.tools, function (b) { b.addEventListener('click', function () { chooseTool(b.getAttribute('data-tool')); }); });
        Array.prototype.forEach.call(host.brushes, function (b) {
            b.addEventListener('click', function () { state.brush = +b.getAttribute('data-brush'); showButtons(); canvas.refresh(); });
            b.addEventListener('keydown', function (e) {
                var at = Edit.BRUSHES.indexOf(state.brush), step = e.key === 'ArrowRight' || e.key === 'ArrowDown' ? 1 : (e.key === 'ArrowLeft' || e.key === 'ArrowUp' ? -1 : 0);
                if (!step) return;
                e.preventDefault();
                state.brush = Edit.BRUSHES[Math.max(0, Math.min(Edit.BRUSHES.length - 1, at + step))];
                showButtons(); canvas.refresh();
                Array.prototype.filter.call(host.brushes, function (n) { return +n.getAttribute('data-brush') === state.brush; })[0].focus();
            });
        });
        host.fill.addEventListener('click', function () { state.fill = !state.fill; showButtons(); });
        host.tile.addEventListener('click', openPicker);
        host.stepLess.addEventListener('click', function () { stepBy(-1); });
        host.stepMore.addEventListener('click', function () { stepBy(1); });
        host.undo.addEventListener('click', undo);
        host.redo.addEventListener('click', redo);
        host.swatches.addEventListener('keydown', arrows);
        doc.addEventListener('keydown', onKey);
        canvas.setPainter(painter);
        showTools();

        return {
            // A map is open (a new one, or one from the disk): the history starts empty
            setMap: function (file) { editor = Edit.create(file, { rebase: env.rebase, erase: Blocks.eraseAt, guard: Blocks.blockedText }); current = null; showButtons(); showInfo(); },
            choose: chooseTool, undo: undo, redo: redo,
            // The lazily loaded parts came in (mapmaker_ground.js: the sheets of the kinds; mapmaker_sprites.js: the pictures, or null when the atlas failed): the palette and the tool buttons show them
            use: function (g, s) {
                if (g) ground = g;
                sprites = s || null;
                Array.prototype.forEach.call(host.tools, function (b) { var icon = ICONS[b.getAttribute('data-tool')], i = b.querySelector('i'); if (icon && i) picture(i, !!icon[0], icon[1], 18); });
                showPalette();
            },
            // A CHANGE that is not a tap (mapmaker_props.js: the properties of the map) as one step of the history; false when a stroke is going on or the change did nothing
            act: function (change) { if (!editor || canvas.isStroking()) return false; var dirty = editor.act(change); if (dirty) changed(dirty); return !!dirty; },
            isClean: function () { return !editor || editor.history.isClean(); },
            isSaved: function () { return !editor || editor.history.isSaved(); },
            markSaved: function () { if (editor) editor.history.markSaved(); },
            markUnsaved: function () { if (editor) editor.history.markUnsaved(); },
            state: function () { return { tool: state.tool, ground: state.ground, objects: state.objects, brush: state.brush, fill: state.fill, team: state.team, ants: state.ants, stages: state.stages, big: state.big, history: editor ? editor.history.size() : null }; }
        };
    }

    return { create: create };
});
