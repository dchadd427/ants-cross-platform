// The properties of the open map in the map maker (docs/MAP_EDITOR.md): the description, the length of a match, the eggs that every team starts with and the default ant type. This script
// reads them for the summary, works the words of the dialog out (the counter, which counts BYTES because the file holds bytes), turns the core's answer into one CHANGE for the history
// (the pieces of the file that differ, nothing else), and builds the dialog. The page never writes a header byte itself: the core's `setProperties` makes the new file and says which pieces
// of the old one changed (web/lvl_core_api.js), and this script only puts those pieces into the history. Text typed by a person and text of the file are put in with textContent and
// .value, never as markup. A plain ES5 script that a page and node can load (the dialog needs a document).
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_checks.js'));
    else root.MapProps = factory(root.MapChecks);
})(this, function (Checks) {
    'use strict';

    var NONE = 0x7FFE;                                                    // the tile id a new map has in the place of the default ant type: no type
    // The default ant types: the tile ids that the file's block 3 holds (docs/MAP_FORMAT.md, "The layout"); any other id is none
    var TYPES = [
        { key: 'none', label: 'None', tile: NONE, color: '#6b6b6b' }, { key: 'combat', label: 'Combat ant', tile: 62, color: '#d65db1' }, { key: 'thief', label: 'Thief ant', tile: 63, color: '#d65db1' },
        { key: 'bomber', label: 'Bomber ant', tile: 64, color: '#d65db1' }, { key: 'swimmer', label: 'Swimmer ant', tile: 65, color: '#d65db1' }, { key: 'fire', label: 'Fire ant', tile: 66, color: '#d65db1' }
    ];
    var RANGES = { minutes: { min: 1, max: 120 }, eggs: { min: 0, max: 255 } };       // what a person can set; a map opened with more keeps its value and shows it

    function capital(text) { return Checks.capital(text); }
    function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }

    // ---- text and bytes ----

    // The bytes of a text in UTF-8 (a letter outside a to z takes 2 or 3 bytes, an emoji 4)
    function utf8Length(text) {
        var n = 0;
        for (var i = 0; i < text.length; i++) {
            var c = text.charCodeAt(i);
            if (c < 0x80) n += 1;
            else if (c < 0x800) n += 2;
            else if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.length && (text.charCodeAt(i + 1) & 0xFC00) === 0xDC00) { n += 4; i++; }
            else n += 3;
        }
        return n;
    }
    function encode(text) { return new TextEncoder().encode(text); }

    // The text that a person reads in a description of the file: UTF-8 when the bytes are valid UTF-8, else one letter for each byte (the old maps are DOS-era text); a control
    // character shows as a space. `bytes` is the file's description. Returns {text, bytes}: `bytes` are the file's own, which go back to the core while the text is not touched.
    function decodeDescription(bytes) {
        var text;
        try { text = new TextDecoder('utf-8', { fatal: true }).decode(bytes); } catch (e) {
            text = '';
            for (var i = 0; i < bytes.length; i++) text += String.fromCharCode(bytes[i]);
        }
        return { text: text.replace(/[\u0000-\u001f\u007f]/g, ' '), bytes: bytes };
    }
    // The bytes of the core's description (its JSON gives every byte as the code point of the same number)
    function bytesOfReport(text) {
        var out = new Uint8Array(text.length);
        for (var i = 0; i < text.length; i++) out[i] = text.charCodeAt(i) & 255;
        return out;
    }

    // The text cut after as many whole letters as fit in `limit` bytes
    function cutTo(text, limit) {
        var out = '', used = 0, letters = Array.from(text);
        for (var i = 0; i < letters.length; i++) {
            var n = utf8Length(letters[i]);
            if (used + n > limit) break;
            out += letters[i]; used += n;
        }
        return out;
    }

    function typeIndex(tile) {
        for (var i = 1; i < TYPES.length; i++) if (TYPES[i].tile === tile) return i;
        return 0;
    }
    function clamp(value, range) { return Math.max(range.min, Math.min(range.max, value)); }

    // ---- reading the properties of a map ----

    // The properties of the bytes as the core reads them: {description (text), raw (the file's bytes), minutes, eggs, tile, kind}, or {error}
    function read(core, bytes) {
        var report = core.readReport(bytes, 'extended');
        if (report.status !== 0) return { error: report.error };
        var j = report.json;
        if (!j.loaded) return { error: j.reason || 'The file does not load.' };
        var raw = bytesOfReport(j.description);
        return { description: decodeDescription(raw).text, raw: raw, minutes: j.minutes, eggs: j.eggs, tile: j.default_type_tile, kind: j.version === 9 ? 'extended' : 'classic' };
    }

    // The lines of the summary (the side panel of a desktop, the line under the map of a phone), in plain words: {description, time, ant, phone}
    function summary(props) {
        var description = props.description ? '\u201c' + props.description + '\u201d' : 'No description';
        var ant = typeIndex(props.tile);
        return {
            description: description,
            time: plural(props.minutes, 'minute', 'minutes') + ' \u00b7 ' + plural(props.eggs, 'egg', 'eggs') + ' for each team',
            ant: ant === 0 ? 'No default ant type' : TYPES[ant].label + ' by default',
            phone: description + ' \u00b7 ' + props.minutes + ' min'
        };
    }

    // ---- the change for the history ----

    // The core's edits (the pieces of the old file that changed, in the order of the file, and what takes their place in the new one) as one CHANGE of mapmaker_edit.js: splices with absolute
    // places, put on the file from the last piece to the first, so that the place of each is still where it was in the old file. A piece that holds equal bytes at its ends is cut down to the
    // bytes that differ. Returns null when the pieces do not make the new file out of the old one (never: the check that stands between the core and the history).
    function changeOf(oldBytes, edits, newBytes) {
        var splices = [], shift = 0, i;
        for (i = 0; i < edits.length; i++) {
            var e = edits[i], from = e.at + shift;
            if (e.at < 0 || e.removed < 0 || e.at + e.removed > oldBytes.length || from < 0 || from + e.inserted > newBytes.length) return null;
            var removed = oldBytes.subarray(e.at, e.at + e.removed), inserted = newBytes.subarray(from, from + e.inserted), head = 0, tail = 0;
            while (head < removed.length && head < inserted.length && removed[head] === inserted[head]) head++;
            while (tail < removed.length - head && tail < inserted.length - head && removed[removed.length - 1 - tail] === inserted[inserted.length - 1 - tail]) tail++;
            if (head + tail < removed.length || head + tail < inserted.length) splices.push({ abs: true, at: e.at + head, removed: removed.slice(head, removed.length - tail), inserted: inserted.slice(head, inserted.length - tail) });
            shift += e.inserted - e.removed;
        }
        if (oldBytes.length + shift !== newBytes.length) return null;
        var made = splices.slice().reverse(), out = oldBytes;
        for (i = 0; i < made.length; i++) {
            var s = made[i], next = new Uint8Array(out.length - s.removed.length + s.inserted.length);
            next.set(out.subarray(0, s.at), 0); next.set(s.inserted, s.at); next.set(out.subarray(s.at + s.removed.length), s.at + s.inserted.length);
            out = next;
        }
        if (out.length !== newBytes.length) return null;
        for (i = 0; i < out.length; i++) if (out[i] !== newBytes[i]) return null;
        return { names: [], cells: [], splices: made };
    }

    // ---- the dialog ----

    var CLASSIC_HELP = 'A Classic map has room for {n} characters (the original reads 30 and needs the last one to end the text).';
    var EXTENDED_HELP = 'An Extended map has room for {n} characters, so it can tell more about the map.';

    // env: {doc, dialogs (MapDialog.create), core, bytes() -> the open map's bytes, props -> the properties it has now (read), apply(change) -> '' when it was put in the history, else a sentence}
    function open(env) {
        var doc = env.doc, core = env.core, now = env.props;
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        // the first answer of the core says how much fits (29 or 255) and gives the verdict of the text the map has now
        var first = core.setProperties(env.bytes(), now.raw, now.minutes, now.eggs, now.tile, 'extended');
        if (first.status !== 0 || !first.json || !first.json.ok) return env.dialogs.message('The properties cannot be opened', capital(first.error || 'The map code did not answer') + '.');
        var kind = now.kind, limit = first.json.description_limit;
        var st = { text: now.description, minutes: now.minutes, eggs: now.eggs, tile: now.tile };
        var cutNote, textBox, countEl, countText, kindText, warnBox, helpEl, minutesBox, eggsBox, antButtons = [], ctl, verdict = { ok: true };

        function bytes() { return st.text === now.description ? now.raw : encode(st.text); }
        function lengthOf(text) { return text === now.description ? now.raw.length : utf8Length(text); }       // (the bytes that the text is in the file)
        function setNote(box, cls, mark, text, button) {
            box.textContent = '';
            if (!text) return;
            var note = el('div', 'note' + (cls ? ' ' + cls : ''));
            note.setAttribute('role', 'alert');
            note.appendChild(el('span', 'ic', mark)).setAttribute('aria-hidden', 'true');
            var body = el('div');
            body.appendChild(el('div', '', text));
            if (button) body.appendChild(button);
            note.appendChild(body);
            box.appendChild(note);
        }
        function sentence(text) { var t = capital(text); return /\.$/.test(t) ? t : t + '.'; }

        // The counter, the sentence of the core when the text is too long, and Apply: on or off
        function showText() {
            var b = bytes(), n = b.length, over = n > limit, chars = Array.from(st.text).length;
            countText.textContent = '';
            countText.appendChild(el('b', '', String(n)));
            countText.appendChild(doc.createTextNode(' of ' + limit + ' characters' + (n > chars ? ' (letters like \u00e9 count double)' : '')));
            countEl.className = 'pcount' + (over ? ' over' : n === limit ? ' full' : '');
            textBox.className = over ? 'over' : '';
            verdict = { ok: true };
            warnBox.textContent = '';
            if (over) {
                var answer = core.setProperties(env.bytes(), b, st.minutes, st.eggs, st.tile, 'extended'), say = '';
                if (answer.status === 0 && answer.json.reasons.length) { verdict = { ok: false }; say = answer.json.reasons[0].message; }
                else if (answer.status === 0 && answer.json.warnings.length) say = answer.json.warnings[0].message;
                else if (answer.status !== 0) { verdict = { ok: false }; say = answer.error; }
                var cut = el('button', 'btn sm', 'Cut it to ' + limit);
                cut.type = 'button';
                cut.addEventListener('click', function () { cutNote.hidden = true; st.text = cutTo(st.text, limit); textBox.value = st.text; showText(); textBox.focus(); });
                setNote(warnBox, 'bad', '!', sentence(say), cut);
            }
            if (ctl) ctl.setDisabled('apply', !verdict.ok);
        }

        function onInput() {
            var value = textBox.value.replace(/[\r\n]+/g, ' '), typed = value, before = lengthOf(st.text), after = lengthOf(value);
            if (after > limit && after > before) value = before <= limit ? cutTo(value, limit) : st.text;      // the box stops at what fits (a text that came in too long can be cut or made shorter)
            cutNote.hidden = value === typed;                                                                  // a pasted text that was cut says so
            st.text = value;
            if (textBox.value !== value) textBox.value = value;
            showText();
        }

        // A number with its two buttons: the range is the one of a person; the value the file has stays as it is until it is changed
        function stepper(name, label, range, unit, help) {
            var field = el('div', 'pfield'), id = 'p-' + name, row = el('div', 'row'), box = el('span', 'stepbox'), less = el('button', 'stp', '\u2212'), more = el('button', 'stp', '+');
            var input = el('input', 'num');
            var labelEl = el('label', '', label); labelEl.htmlFor = id;
            less.type = 'button'; more.type = 'button'; input.type = 'text'; input.id = id;
            input.setAttribute('inputmode', 'numeric'); input.setAttribute('autocomplete', 'off'); input.maxLength = 5;
            less.setAttribute('aria-label', 'Fewer ' + (name === 'minutes' ? 'minutes' : 'eggs')); more.setAttribute('aria-label', 'More ' + (name === 'minutes' ? 'minutes' : 'eggs'));
            function show() { input.value = String(st[name]); less.disabled = st[name] <= range.min; more.disabled = st[name] >= range.max; showText(); }
            function set(v) { st[name] = v; show(); }
            // what the box says is taken when it is left or Enter is pressed: a whole number in the range (the value that the map came with is allowed to stay); anything else goes back
            function take() { var v = parseInt(input.value, 10); set(isNaN(v) ? st[name] : (v === now[name] ? v : clamp(v, range))); }
            less.addEventListener('click', function () { set(Math.min(range.max, Math.max(range.min, st[name] - 1))); });
            more.addEventListener('click', function () { set(Math.min(range.max, Math.max(range.min, st[name] + 1))); });
            input.addEventListener('input', function () { var digits = input.value.replace(/\D/g, ''); if (digits !== input.value) input.value = digits; });
            input.addEventListener('change', take);
            input.addEventListener('keydown', function (e) { if (e.key === 'Enter') { e.preventDefault(); take(); if (ctl.buttons.apply && !ctl.buttons.apply.disabled) ctl.buttons.apply.click(); } });
            box.appendChild(less); box.appendChild(input); box.appendChild(more);
            row.appendChild(box); row.appendChild(el('span', 'unit', unit));
            field.appendChild(labelEl); field.appendChild(row); field.appendChild(el('div', 'help', help));
            field.show = show; field.take = take;
            return field;
        }

        function pickType(index) {
            st.tile = index === 0 ? (typeIndex(now.tile) === 0 ? now.tile : NONE) : TYPES[index].tile;        // (the "none" that the file has stays as it is)
            showTypes();
        }
        function showTypes() {
            var chosen = typeIndex(st.tile);
            antButtons.forEach(function (b, i) { b.setAttribute('aria-checked', String(i === chosen)); b.tabIndex = i === chosen ? 0 : -1; });
        }

        // Apply: the core makes the new file, the page puts the pieces that changed into the history as one step. Returns true when the dialog may close.
        function apply() {
            minutesBox.take(); eggsBox.take();
            var answer = core.setProperties(env.bytes(), bytes(), st.minutes, st.eggs, st.tile, 'extended');
            if (answer.status !== 0) { setNote(warnBox, 'bad', '!', sentence(answer.error)); return false; }
            if (!answer.json.ok) { setNote(warnBox, 'bad', '!', sentence(answer.json.reasons.length ? answer.json.reasons[0].message : 'The properties were refused')); return false; }
            if (!answer.json.changed) return true;
            var change = changeOf(env.bytes(), answer.json.edits, answer.bytes);
            if (!change) { setNote(warnBox, 'bad', '!', 'The properties could not be put into the map. Nothing was changed.'); return false; }
            var said = env.apply(change);
            if (said) { setNote(warnBox, 'bad', '!', said); return false; }
            return true;
        }

        ctl = env.dialogs.open({
            title: 'Map properties', cls: 'props', focus: '#p-desc',
            build: function (body) {
                body.appendChild(el('p', 'lead', 'What is saved in the map file besides the cells. The check looks at it as soon as you press Apply.'));
                var field = el('div', 'pfield'), label = el('label', '', 'Description');
                label.htmlFor = 'p-desc';
                textBox = kind === 'extended' ? el('textarea') : el('input');
                if (kind === 'extended') textBox.rows = 3; else textBox.type = 'text';
                textBox.id = 'p-desc'; textBox.setAttribute('autocomplete', 'off'); textBox.setAttribute('spellcheck', 'false'); textBox.setAttribute('aria-describedby', 'p-count');
                textBox.value = st.text;
                textBox.addEventListener('input', onInput);
                textBox.addEventListener('keydown', function (e) { if (e.key === 'Enter' && kind !== 'extended') { e.preventDefault(); if (ctl.buttons.apply && !ctl.buttons.apply.disabled) ctl.buttons.apply.click(); } });
                countEl = el('div', 'pcount'); countEl.id = 'p-count';
                countText = el('span'); countEl.appendChild(countText);
                kindText = el('span', '', kind === 'extended' ? 'Extended map' : 'Classic map'); countEl.appendChild(kindText);
                cutNote = el('div', 'help cutnote', 'Cut to fit: the text was longer than the box holds.'); cutNote.hidden = true; cutNote.setAttribute('role', 'status');
                helpEl = el('div', 'help', (kind === 'extended' ? EXTENDED_HELP : CLASSIC_HELP).replace('{n}', String(limit)));
                field.appendChild(label); field.appendChild(textBox); field.appendChild(countEl); field.appendChild(helpEl); field.appendChild(cutNote);
                body.appendChild(field);
                warnBox = el('div'); warnBox.setAttribute('aria-live', 'polite');
                body.appendChild(warnBox);
                var two = el('div', 'two');
                minutesBox = stepper('minutes', 'Length of a match', RANGES.minutes, 'minutes', 'What the lobby offers as the length of a game on this map.');
                eggsBox = stepper('eggs', 'Eggs at the start', RANGES.eggs, 'for each team', 'Every team starts with the same number of eggs.');
                two.appendChild(minutesBox); two.appendChild(eggsBox);
                body.appendChild(two);
                var set = el('div', 'pfield'), legend = el('div', 'lg', 'Default ant type'), group = el('div', 'ants');
                legend.id = 'p-ant-label';
                group.setAttribute('role', 'radiogroup'); group.setAttribute('aria-labelledby', 'p-ant-label');
                TYPES.forEach(function (t, i) {
                    var b = el('button', 'ant'), chip = el('i');
                    b.type = 'button'; b.setAttribute('role', 'radio'); b.setAttribute('data-type', t.key);
                    chip.style.background = t.color; b.appendChild(chip); b.appendChild(el('span', '', t.label));
                    b.addEventListener('click', function () { pickType(i); });
                    b.addEventListener('keydown', function (e) {
                        var step = e.key === 'ArrowRight' || e.key === 'ArrowDown' ? 1 : (e.key === 'ArrowLeft' || e.key === 'ArrowUp' ? -1 : 0);
                        if (!step) return;
                        e.preventDefault();
                        var next = (i + step + TYPES.length) % TYPES.length;
                        pickType(next); antButtons[next].focus();
                    });
                    antButtons.push(b); group.appendChild(b);
                });
                set.appendChild(legend); set.appendChild(group); set.appendChild(el('div', 'help', 'Every ant that has no type of its own is of this type, the ones that start and the ones that hatch.'));
                body.appendChild(set);
                minutesBox.show(); eggsBox.show(); showTypes();
            },
            buttons: [{ id: 'cancel', text: 'Cancel', ghost: true }, { id: 'apply', text: 'Apply', keepOpen: true, onClick: function (c) { if (apply()) c.close(); } }]
        });
        showText();
        return ctl;
    }

    return { open: open, read: read, summary: summary, changeOf: changeOf, utf8Length: utf8Length, cutTo: cutTo, decodeDescription: decodeDescription, typeIndex: typeIndex, TYPES: TYPES, RANGES: RANGES, NONE: NONE };
});
