#!/usr/bin/env node
// The properties of the map in the map maker (web/front/mapmaker_props.js; docs/MAP_EDITOR.md): the words and arithmetic of the dialog, the summary, the change that the history takes, and
// the dialog itself on a small fake of the DOM (tests/scripts/web_mapmaker_fake_dom.js) with a stand-in for the map core. No browser and no real core: the real core is held to the same
// calls by tests/scripts/web_mapmaker_core_check.js (case I) and tests/scripts/lvl_core_check.js (case F), the browser by the opt-in check.
//
//   node tests/scripts/web_mapmaker_props_check.js [--front DIR]
//
//   --front DIR  the folder of the mapmaker_*.js files (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the words: bytes of a text (a letter outside a to z takes 2 or 3), where a text is cut, how a description of the file is read (UTF-8 or one letter for a byte), the summary lines
//   B  the change: the core's edits become splices from the last to the first, cut down to the bytes that differ, and a change that does not make the new file is refused
//   C  the dialog on a Classic map: its words, the counter, the box that stops at what fits, the accented letters, a text of 30 bytes with the core's sentence and Cut, the steppers, the ants
//   D  the dialog on an Extended map: the larger box, the limit of 255, the refusal of 256 that keeps Apply off, a line break that becomes a space
//   E  Apply and Cancel: Apply gives the history ONE change and closes, Cancel and Escape change nothing, a refusal keeps the dialog open with its sentence
//   F  text of a person or a file is text: markup in a description is shown as it is, and nothing but the core's answer is put into the history
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const path = require('path');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.resolve(__dirname, '..', '..', 'web', 'front');
const Props = require(path.join(FRONT, 'mapmaker_props.js')), Dialog = require(path.join(FRONT, 'mapmaker_dialog.js'));
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }

// ---- a stand-in for the core: a made-up file and the contract of setProperties (web/lvl_core_api.js) ----
// version 8: minutes (2), the description (30), the default ant type (2), the eggs (2); version 9: minutes (2), the description's length (2), its bytes, the type (2), the eggs (2). The sentences are the checker's.
const SENTENCE30 = 'the description is 30 bytes long: at most 29 fit, because the original reads 30 bytes and a text needs a zero byte at its end (a letter outside a to z takes 2 or 3 bytes)';
function sentenceFor(kind, n) { return kind === 'extended' ? 'the description is ' + n + ' bytes long: an Extended map holds at most 255' : SENTENCE30.replace('30 bytes long', n + ' bytes long'); }
function makeFile(kind, text, minutes, eggs, type) {
    const desc = typeof text === 'string' ? Buffer.from(text, 'utf8') : Buffer.from(text), head = kind === 'classic' ? 32 : 4 + desc.length, out = new Uint8Array(head + 4);
    const word = (at, v) => { out[at] = v & 255; out[at + 1] = v >> 8; };
    word(0, minutes);
    if (kind === 'classic') out.set(desc.subarray(0, 30), 2); else { word(2, desc.length); out.set(desc, 4); }
    word(head, type); word(head + 2, eggs);
    return out;
}
function standIn(log) {
    const word = (b, at) => b[at] | (b[at + 1] << 8);
    function layout(b, kind) { const len = kind === 'classic' ? 30 : word(b, 2), head = kind === 'classic' ? 32 : 4 + len; return { descAt: kind === 'classic' ? 2 : 2, descLen: kind === 'classic' ? 30 : 2 + len, head, kind }; }
    function kindOf(b) { return b.kind; }
    return {
        readReport(b) {
            const kind = b.kind, l = layout(b, kind), len = kind === 'classic' ? 30 : l.descLen - 2, at = kind === 'classic' ? 2 : 4;
            let text = '';
            for (let i = 0; i < len && b[at + i] !== 0; i++) text += String.fromCharCode(b[at + i]);
            return { status: 0, json: { loaded: true, version: kind === 'classic' ? 8 : 9, description: text, minutes: word(b, 0), eggs: word(b, l.head + 2), default_type_tile: word(b, l.head) } };
        },
        setProperties(b, description, minutes, eggs, tile, profile) {
            log.push({ description: Array.from(description), minutes, eggs, tile, profile });
            const kind = b.kind, l = layout(b, kind), limit = this.limit || (kind === 'classic' ? 29 : 255), most = this.refuseLonger || (kind === 'classic' ? 30 : 255), reasons = [], warnings = [];
            const read = this.readReport(b).json;
            if (description.length > most) reasons.push({ code: 'description_too_long', message: sentenceFor(kind, description.length) });
            else if (description.length > limit) warnings.push({ code: 'description_too_long', message: sentenceFor(kind, description.length) });
            if (this.refuseEggs && eggs !== read.eggs) reasons.push({ code: 'eggs_not_in_file', message: 'the file ends before its egg word, so it has no place to keep the eggs' });
            const json = { ok: reasons.length === 0, changed: false, bytes: 0, description_bytes: description.length, description_limit: limit, edits: [], reasons, warnings };
            if (reasons.length) return { status: 0, text: '', json };
            const edits = [], pieces = [];
            const now = Buffer.from(Array.from(b.subarray(l.descAt, l.descAt + (kind === 'classic' ? 30 : l.descLen))));
            if (minutes !== read.minutes) edits.push({ at: 0, removed: 2, text: Buffer.from([minutes & 255, minutes >> 8]) });
            const same = read.description === Buffer.from(description).toString('latin1');
            if (!same) {
                const field = kind === 'classic' ? Buffer.concat([Buffer.from(description), Buffer.alloc(30 - description.length)]) : Buffer.concat([Buffer.from([description.length & 255, description.length >> 8]), Buffer.from(description)]);
                edits.push({ at: 2, removed: kind === 'classic' ? 30 : l.descLen, text: field });
            }
            if (tile !== read.default_type_tile) edits.push({ at: l.head, removed: 2, text: Buffer.from([tile & 255, tile >> 8]) });
            if (eggs !== read.eggs) edits.push({ at: l.head + 2, removed: 2, text: Buffer.from([eggs & 255, eggs >> 8]) });
            let out = Buffer.from(b), shift = 0;
            for (const e of edits) { out = Buffer.concat([out.subarray(0, e.at + shift), e.text, out.subarray(e.at + shift + e.removed)]); json.edits.push({ at: e.at, removed: e.removed, inserted: e.text.length }); shift += e.text.length - e.removed; }
            const bytes = new Uint8Array(out); bytes.kind = kind;
            json.changed = edits.length > 0; json.bytes = bytes.length;
            return { status: 0, text: '', json, bytes };
        }
    };
}

// ---- a page: a document, the dialogs, a map that is open ----
function setup(kind, text, minutes, eggs, type, options) {
    options = options || {};
    const doc = makeDocument(), host = doc.createElement('div'), top = doc.createElement('header'), body = doc.createElement('div'), opener = doc.createElement('button');
    [host, top, body].forEach((n) => doc.body.appendChild(n));
    top.appendChild(opener); opener.focus();
    const asked = [], core = standIn(asked), dialogs = Dialog.create(doc, host, [top, body]);
    core.refuseEggs = !!options.refuseEggs; core.limit = options.limit || 0;
    const bytes = makeFile(kind, text, minutes, eggs, type); bytes.kind = kind;
    const map = { bytes }, applied = [];
    const props = Props.read(core, bytes);
    const ctl = Props.open({ doc, dialogs, core, props, bytes: () => map.bytes, apply: (change) => { applied.push(change); return options.applyRefuses || ''; } });
    const q = (s) => host.querySelector(s), all = (s) => host.querySelectorAll(s);
    return {
        doc, host, dialogs, ctl, asked, applied, map, props, q, all, opener, core,
        type(value, id) { const box = q(id || '#p-desc'); box.value = value; box.fire('input', {}); },
        button(label) { return all('button').filter((b) => b.textContent === label)[0]; },
        counter() { return q('#p-count').children[0].textContent; },
        note() { return all('.note.bad')[0] || null; },
        ant() { return all('.ant').filter((b) => b.getAttribute('aria-checked') === 'true').map((b) => b.getAttribute('data-type')); },
        number(id) { return q(id).value; }
    };
}
const DESC = 'A picnic with many ants';

// ---- A: the words ----
function caseA() {
    equal([Props.utf8Length('abc'), Props.utf8Length('é'), Props.utf8Length('€'), Props.utf8Length('😀'), Props.utf8Length('')], [3, 2, 3, 4, 0], 'the bytes of a text in UTF-8');
    equal(Props.utf8Length('Café été des fourmis'), 23, 'the picture\'s "Café été des fourmis" is 20 letters and 23 bytes');
    equal([Props.cutTo('abcdef', 4), Props.cutTo('ééé', 5), Props.cutTo('ééé', 6), Props.cutTo('a😀', 4), Props.cutTo('short', 29), Props.cutTo('abc', 0)], ['abcd', 'éé', 'ééé', 'a', 'short', ''], 'a text is cut after whole letters');
    const utf = Props.decodeDescription(new Uint8Array([0x43, 0x61, 0x66, 0xC3, 0xA9])), latin = Props.decodeDescription(new Uint8Array([0x43, 0x61, 0x66, 0xE9])), control = Props.decodeDescription(new Uint8Array([0x61, 0x09, 0x0A, 0x62]));
    equal([utf.text, latin.text, control.text], ['Café', 'Café', 'a  b'], 'a description of the file: UTF-8 when it is valid, else one letter for each byte; a control character is a space');
    equal([utf.bytes.length, latin.bytes.length], [5, 4], 'and the file\'s own bytes are kept');
    equal(Props.summary({ description: DESC, minutes: 12, eggs: 9, tile: 0x7FFE }), { description: '“A picnic with many ants”', time: '12 minutes · 9 eggs for each team', ant: 'No default ant type', phone: '“A picnic with many ants” · 12 min' }, 'the summary of the picture');
    equal(Props.summary({ description: '', minutes: 1, eggs: 1, tile: 63 }), { description: 'No description', time: '1 minute · 1 egg for each team', ant: 'Thief ant by default', phone: 'No description · 1 min' }, 'no description, one minute, one egg, a type');
    equal([62, 63, 64, 65, 66, 0x7FFE, 0, 67, 65535].map(Props.typeIndex), [1, 2, 3, 4, 5, 0, 0, 0, 0], 'a tile id is a default ant type (62 to 66), any other is none');
    equal(Props.TYPES.map((t) => t.label), ['None', 'Combat ant', 'Thief ant', 'Bomber ant', 'Swimmer ant', 'Fire ant'], 'the six choices, in the order of the picture');
    equal(Props.TYPES.map((t) => t.sprite), [811, 897, 1085, 774, 963, 676], 'each choice carries its standing ant sprite id');
    equal([Props.RANGES.minutes, Props.RANGES.eggs], [{ min: 1, max: 120 }, { min: 0, max: 255 }], 'minutes 1 to 120, eggs 0 to 255');
    const file = makeFile('classic', DESC, 12, 9, 0x7FFE); file.kind = 'classic';
    const read = Props.read(standIn([]), file);
    check(!read.error && read.description === DESC && read.minutes === 12 && read.eggs === 9 && read.tile === 0x7FFE && read.kind === 'classic' && read.raw.length === 23, 'the properties are read as the core reports them');
    equal(Props.read({ readReport: () => ({ status: 1, error: 'no' }) }, new Uint8Array(1)).error, 'no', 'a core that cannot read says so');
    equal(Props.read({ readReport: () => ({ status: 0, json: { loaded: false, reason: 'cut short' } }) }, new Uint8Array(1)).error, 'cut short', 'a file that does not load says why');
}

// ---- B: the change ----
function caseB() {
    const old = Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]), neu = Uint8Array.from([1, 2, 30, 4, 5, 6, 7, 80, 90, 91, 92, 10]);
    const change = Props.changeOf(old, [{ at: 2, removed: 1, inserted: 1 }, { at: 7, removed: 2, inserted: 4 }], neu);
    check(change && change.names.length === 0 && change.cells.length === 0 && change.splices.length === 2, 'two edits are two splices');
    equal(change.splices.map((s) => [s.abs, s.at]), [[true, 7], [true, 2]], 'put on the file from the last to the first, at the places of the old file');
    equal(change.splices.map((s) => [Array.from(s.removed), Array.from(s.inserted)]), [[[8, 9], [80, 90, 91, 92]], [[3], [30]]], 'with the bytes that went out and came in');
    // equal bytes at the ends of a piece are not part of the change
    const trimmed = Props.changeOf(Uint8Array.from([5, 6, 7, 8, 9]), [{ at: 0, removed: 5, inserted: 5 }], Uint8Array.from([5, 6, 70, 8, 9]));
    equal(trimmed.splices.map((s) => [s.at, Array.from(s.removed), Array.from(s.inserted)]), [[2, [7], [70]]], 'a piece is cut down to the bytes that differ');
    equal(Props.changeOf(old, [{ at: 0, removed: 3, inserted: 3 }], old).splices, [], 'an edit that changes nothing is no splice');
    check(Props.changeOf(old, [{ at: 2, removed: 1, inserted: 1 }], Uint8Array.from([1, 2, 31, 4, 5, 6, 7, 8, 9, 99])) === null, 'edits that do not make the new file are refused');
    check(Props.changeOf(old, [{ at: 9, removed: 5, inserted: 1 }], neu) === null && Props.changeOf(old, [{ at: -1, removed: 1, inserted: 1 }], neu) === null, 'an edit outside the file is refused');
    check(Props.changeOf(old, [{ at: 2, removed: 1, inserted: 40 }], neu) === null, 'an edit that reaches past the new file is refused');
    check(Props.changeOf(old, [{ at: 2, removed: 1, inserted: 1 }], Uint8Array.from([1, 2, 30, 4, 5, 6, 7, 8, 9])) === null, 'a new file of another length than the edits make is refused');
}

// ---- C: the dialog on a Classic map ----
function caseC() {
    const p = setup('classic', DESC, 12, 9, 0x7FFE);
    check(p.dialogs.isOpen() && p.q('h2').textContent === 'Map properties' && p.q('.dlg').getAttribute('aria-labelledby') === p.q('h2').id, 'a dialog named Map properties opens');
    equal(p.q('.lead').textContent, 'What is saved in the map file besides the cells. The check looks at it as soon as you press Apply.', 'the sentence of the picture');
    check(p.q('.dlg').className.split(' ').includes('props'), 'with its own width');
    check(p.q('#p-desc').tagName === 'input' && p.q('#p-desc').value === DESC && p.doc.activeElement === p.q('#p-desc'), 'the description is a line, filled in, with the focus');
    equal(p.all('label').map((l) => [l.textContent, l.htmlFor]), [['Description', 'p-desc'], ['Length of a match', 'p-minutes'], ['Eggs at the start', 'p-eggs']], 'the boxes have their labels');
    equal([p.counter(), p.q('#p-count').children[1].textContent], ['23 of 29 characters', 'Classic map'], 'the counter says characters and the kind');
    equal(p.q('.help').textContent, 'A Classic map has room for 29 characters (the original reads 30 and needs the last one to end the text).', 'the help of a Classic map, with the core\'s number');
    equal([p.number('#p-minutes'), p.number('#p-eggs')], ['12', '9'], 'the numbers of the map');
    equal(p.all('.unit').map((u) => u.textContent), ['minutes', 'for each team'], 'with their units');
    equal(p.all('.pfield .help').map((u) => u.textContent).slice(2, 4), ['What the lobby offers as the length of a game on this map.', 'Every team starts with the same number of eggs.'], 'and their help');
    equal(p.all('.ant').map((b) => b.textContent), ['None', 'Combat ant', 'Thief ant', 'Bomber ant', 'Swimmer ant', 'Fire ant'], 'six ant types');
    equal([p.ant(), p.q('.ants').getAttribute('role'), p.all('.ant')[0].getAttribute('role')], [['none'], 'radiogroup', 'radio'], 'one is chosen: none');
    equal(p.all('.dlg-foot button').map((b) => b.textContent), ['Cancel', 'Apply'], 'Cancel and Apply');
    check(p.note() === null && !p.button('Apply').disabled, 'no note, Apply is on');
    // the first question to the core is the map as it is, with the bytes of the file
    equal([p.asked.length, p.asked[0].description.length, p.asked[0].minutes, p.asked[0].eggs, p.asked[0].tile, p.asked[0].profile], [1, 23, 12, 9, 0x7FFE, 'extended'], 'the core is asked once, for the map as it is, as a place that takes both kinds');
    // typing: the counter follows, and the box stops at what fits
    p.type('A picnic');
    equal(p.counter(), '8 of 29 characters', 'the counter follows the text');
    p.type('Café été des fourmis');
    equal(p.counter(), '23 of 29 characters (letters like é count double)', 'accented letters count double, and the counter says so');
    p.type('Cafe ete des fourmis');
    equal(p.counter(), '20 of 29 characters', 'plain letters say nothing of it');
    check(p.q('.cutnote').hidden === true, 'no "Cut to fit" note while nothing was cut');
    const more = p.asked.length;
    p.type('x'.repeat(29));
    equal([p.counter(), p.q('#p-count').className, p.note()], ['29 of 29 characters', 'pcount full', null], 'the 29th byte fits (the number turns gold)');
    p.type('x'.repeat(29) + 'y');
    equal([p.q('#p-desc').value.length, p.counter()], [29, '29 of 29 characters'], 'a 30th byte is not taken: the box stops at 29');
    check(p.q('.cutnote').hidden === false && p.q('.cutnote').textContent === 'Cut to fit: the text was longer than the box holds.', 'a text that was cut says "Cut to fit"');
    p.type('short');
    check(p.q('.cutnote').hidden === true, 'and the note goes with the next key that cuts nothing');
    p.type('x'.repeat(28) + 'é');
    equal([p.q('#p-desc').value, p.counter()], ['x'.repeat(28), '28 of 29 characters'], 'a letter that would take the text past 29 bytes is not taken whole nor in half');
    p.type('x'.repeat(28) + 'yééé');
    equal(p.q('#p-desc').value, 'x'.repeat(28) + 'y', 'a long paste is cut to what fits');
    check(p.asked.length === more, 'the core is not asked while the text fits (the counter is the page\'s arithmetic)');
    p.button('Cancel').click();
}

function caseC2() {
    // a map that came with 30 bytes: the core's sentence in red, Cut it to 29, and Apply stays on (a Classic map of 30 bytes is a warning)
    const text = 'Treasure hunt of the tiny ants';
    const p = setup('classic', text, 12, 9, 0x7FFE);
    equal([p.counter(), p.q('#p-count').className], ['30 of 29 characters', 'pcount over'], 'a map that came with 30 bytes says 30 of 29, in red');
    check(p.note() !== null && p.note().textContent === '!The description is 30 bytes long: at most 29 fit, because the original reads 30 bytes and a text needs a zero byte at its end (a letter outside a to z takes 2 or 3 bytes).Cut it to 29', 'the core\'s own sentence, capitalized, with a full stop, and the button');
    check(p.note().getAttribute('role') === 'alert' && p.q('#p-desc').className === 'over', 'the note is an alert, the box is marked');
    check(!p.button('Apply').disabled, 'Apply stays on: the core writes it and the check warns');
    p.type(text + 'x');
    equal(p.q('#p-desc').value, text, 'a text that is too long cannot grow');
    p.type('Treasure hunt of the tiny ant');
    equal([p.counter(), p.note()], ['29 of 29 characters', null], 'made shorter by hand, the note goes');
    const c = setup('classic', text, 12, 9, 0x7FFE);
    c.button('Cut it to 29').click();
    equal([c.q('#p-desc').value, c.counter(), c.note(), c.doc.activeElement === c.q('#p-desc')], ['Treasure hunt of the tiny ant', '29 of 29 characters', null, true], 'Cut it to 29 cuts the text and the focus goes back to the box');
    // accented letters at the cut
    p.type('é'.repeat(15));
    equal(p.counter(), '28 of 29 characters (letters like é count double)', '15 letters of 2 bytes are 30 bytes: the box takes 14 of them');
}

function caseC3() {
    // the steppers
    const p = setup('classic', DESC, 12, 9, 0x7FFE);
    const less = (id) => p.q('#' + id).parentNode.children[0], more = (id) => p.q('#' + id).parentNode.children[2];
    less('p-minutes').click(); less('p-minutes').click();
    equal(p.number('#p-minutes'), '10', 'the minus button takes one off');
    more('p-minutes').click();
    equal(p.number('#p-minutes'), '11', 'the plus button adds one');
    p.type('120', '#p-minutes'); p.q('#p-minutes').fire('change', {});
    check(p.number('#p-minutes') === '120' && more('p-minutes').disabled && !less('p-minutes').disabled, 'minutes stop at 120: plus is off');
    p.type('500', '#p-minutes'); p.q('#p-minutes').fire('change', {});
    equal(p.number('#p-minutes'), '120', 'a number typed above the range goes to the most');
    p.type('0', '#p-minutes'); p.q('#p-minutes').fire('change', {});
    check(p.number('#p-minutes') === '1' && less('p-minutes').disabled, 'a minute is the least: 0 becomes 1 and minus is off');
    p.type('1a2b', '#p-minutes');
    equal(p.number('#p-minutes'), '12', 'only digits stay in the box');
    p.q('#p-minutes').fire('change', {});
    p.type('', '#p-minutes'); p.q('#p-minutes').fire('change', {});
    equal(p.number('#p-minutes'), '12', 'an empty box goes back to the number it had');
    p.type('255', '#p-eggs'); p.q('#p-eggs').fire('change', {});
    check(p.number('#p-eggs') === '255' && more('p-eggs').disabled, 'eggs stop at 255');
    p.type('0', '#p-eggs'); p.q('#p-eggs').fire('change', {});
    check(p.number('#p-eggs') === '0' && less('p-eggs').disabled, 'a team may start with no egg');
    equal([p.q('#p-minutes').getAttribute('inputmode'), p.q('#p-minutes').type], ['numeric', 'text'], 'a number box that a phone gives a number pad');
    equal([less('p-minutes').getAttribute('aria-label'), more('p-eggs').getAttribute('aria-label')], ['Fewer minutes', 'More eggs'], 'the buttons are named');
    // the ant type: a click, the arrow keys, and the "none" of the file
    p.all('.ant')[2].click();
    equal([p.ant(), p.all('.ant').map((b) => b.tabIndex)], [['thief'], [-1, -1, 0, -1, -1, -1]], 'a click chooses Thief ant, and it alone is in the tab order');
    p.all('.ant')[2].fire('keydown', { key: 'ArrowRight', preventDefault() {} });
    equal([p.ant(), p.doc.activeElement === p.all('.ant')[3]], [['bomber'], true], 'the arrow keys move and choose');
    p.all('.ant')[3].fire('keydown', { key: 'ArrowLeft', preventDefault() {} }); p.all('.ant')[2].fire('keydown', { key: 'ArrowUp', preventDefault() {} });
    equal(p.ant(), ['combat'], 'left and up go back');
    p.all('.ant')[1].fire('keydown', { key: 'ArrowLeft', preventDefault() {} }); p.all('.ant')[0].fire('keydown', { key: 'ArrowLeft', preventDefault() {} });
    equal(p.ant(), ['fire'], 'and wrap round');
    p.all('.ant')[0].click();
    p.button('Apply').click();
    equal(p.asked[p.asked.length - 1].tile, 0x7FFE, 'None gives the tile of a map with no type');
    // a map that has a "none" of its own keeps it
    const q = setup('classic', DESC, 12, 9, 0);
    equal(q.ant(), ['none'], 'a file with the tile 0 shows none');
    q.all('.ant')[3].click(); q.all('.ant')[0].click(); q.button('Apply').click();
    equal(q.asked[q.asked.length - 1].tile, 0, 'and None gives it its own none back (nothing else changes)');
    const odd = setup('classic', DESC, 12, 9, 12345);
    equal(odd.ant(), ['none'], 'a tile that is no ant type is shown as none');
}

// ---- D: the dialog on an Extended map ----
function caseD() {
    const long = 'A big garden with a river and two bridges, where the red and blue ants meet in the middle. Food grows near the water, so every team has to cross a bridge sooner or later.';
    const p = setup('extended', long, 20, 12, 62);
    check(p.q('#p-desc').tagName === 'textarea' && p.q('#p-desc').rows === 3, 'the description is a larger box of 3 lines');
    equal([p.counter(), p.q('#p-count').children[1].textContent, p.ant()], ['170 of 255 characters', 'Extended map', ['combat']], 'the counter, the kind, the type');
    equal(p.q('.help').textContent, 'An Extended map has room for 255 characters, so it can tell more about the map.', 'the help of an Extended map, with the core\'s number');
    p.type('a\nb\r\nc');
    equal(p.q('#p-desc').value, 'a b c', 'a line break becomes a space');
    p.type('x'.repeat(255));
    equal([p.counter(), p.note()], ['255 of 255 characters', null], '255 fit');
    p.type('x'.repeat(256));
    equal(p.q('#p-desc').value.length, 255, 'the 256th byte is not taken');
    p.type('é'.repeat(128));
    equal([p.q('#p-desc').value.length, p.counter()], [127, '254 of 255 characters (letters like é count double)'], 'letters of 2 bytes stop at 127 of them');
    p.button('Cancel').click();
}

// The way to a text that is too long is a map that came with one (a Classic map of 30 bytes) or a core that refuses a text that the box took: both are shown with the core's sentence
function caseD2() {
    const refused = setup('extended', 'x'.repeat(12), 20, 12, 62, { limit: 5 });
    check(refused.note() !== null && refused.counter() === '12 of 5 characters', 'a text above the limit that the core reports gets the note');
    refused.core.refuseLonger = 10;
    refused.type('y'.repeat(11));
    check(refused.note() !== null && /^!The description is 11 bytes long: an Extended map holds at most 255\.Cut it to 5$/.test(refused.note().textContent), 'the core refuses a text: its sentence, and the button');
    check(refused.button('Apply').disabled, 'Apply is off while the core refuses the text');
    refused.button('Cut it to 5').click();
    check(refused.q('#p-desc').value === 'yyyyy' && refused.note() === null && !refused.button('Apply').disabled, 'Cut it to 5 cuts the text, the note goes and Apply is on');
    // a core that cannot read the map as it is: the dialog does not open, it says why
    const doc = makeDocument(), host = doc.createElement('div'), dialogs = Dialog.create(doc, host, []);
    Props.open({ doc, dialogs, core: { setProperties: () => ({ status: 1, error: 'does not load: cut short' }) }, props: { description: '', raw: new Uint8Array(0), minutes: 1, eggs: 1, tile: 0, kind: 'classic' }, bytes: () => new Uint8Array(1), apply: () => '' });
    check(host.querySelector('.fatalmsg') && host.querySelector('.fatalmsg').textContent === 'Does not load: cut short.' && host.querySelector('#p-desc') === null, 'a map the core cannot read: a plain message, no dialog of boxes');
}

// ---- E: Apply and Cancel ----
function caseE() {
    const p = setup('classic', DESC, 12, 9, 0x7FFE);
    p.type('Café of the ants');
    p.all('.ant')[2].click();
    p.q('#p-minutes').parentNode.children[2].click();
    p.button('Apply').click();
    check(!p.dialogs.isOpen() && p.applied.length === 1, 'Apply closes the dialog and gives the history ONE change');
    const last = p.asked[p.asked.length - 1];
    equal([Buffer.from(last.description).toString('utf8'), last.minutes, last.eggs, last.tile, last.profile], ['Café of the ants', 13, 9, 63, 'extended'], 'made from what the boxes held, as UTF-8 bytes');
    const change = p.applied[0];
    check(change.names.length === 0 && change.cells.length === 0 && change.splices.length >= 3 && change.splices.every((s) => s.abs === true), 'a change of splices of the header and nothing else');
    check(p.doc.activeElement === p.opener, 'the focus goes back where it was');
    // the change makes the new file out of the old one
    let bytes = p.map.bytes;
    change.splices.forEach((s) => { const out = new Uint8Array(bytes.length - s.removed.length + s.inserted.length); out.set(bytes.subarray(0, s.at)); out.set(s.inserted, s.at); out.set(bytes.subarray(s.at + s.removed.length), s.at + s.inserted.length); bytes = out; });
    check(Buffer.from(bytes.subarray(2, 2 + 17)).toString('utf8') === 'Café of the ants' && bytes[0] === 13, 'applied, the file says what was asked');
    // Cancel changes nothing, nor does Escape, nor does a change that is no change
    const c = setup('classic', DESC, 12, 9, 0x7FFE);
    c.type('Something else'); c.all('.ant')[4].click();
    c.button('Cancel').click();
    check(!c.dialogs.isOpen() && c.applied.length === 0 && c.asked.length === 1, 'Cancel changes nothing, and asks the core nothing more');
    const e = setup('classic', DESC, 12, 9, 0x7FFE);
    e.type('Something else');
    e.doc.key('Escape');
    check(!e.dialogs.isOpen() && e.applied.length === 0, 'Escape is Cancel');
    const n = setup('classic', DESC, 12, 9, 0x7FFE);
    n.button('Apply').click();
    check(!n.dialogs.isOpen() && n.applied.length === 0, 'Apply with nothing changed closes and makes no step of the history');
    // Enter in the description is Apply
    const d = setup('classic', DESC, 12, 9, 0x7FFE);
    d.type('By Enter');
    d.q('#p-desc').fire('keydown', { key: 'Enter', preventDefault() {} });
    check(!d.dialogs.isOpen() && d.applied.length === 1, 'Enter in the description is Apply');
    // a typed number is taken by Apply, and by Enter in its box
    const m = setup('classic', DESC, 12, 9, 0x7FFE);
    m.type('30', '#p-minutes');
    m.button('Apply').click();
    equal(m.asked[m.asked.length - 1].minutes, 30, 'a number typed and not yet left is taken by Apply');
    const k = setup('classic', DESC, 12, 9, 0x7FFE);
    k.type('77', '#p-eggs');
    k.q('#p-eggs').fire('keydown', { key: 'Enter', preventDefault() {} });
    check(!k.dialogs.isOpen() && k.asked[k.asked.length - 1].eggs === 77, 'Enter in a number box takes it and applies');
    // a value the map came with, beyond the range, stays unless it is changed
    const big = setup('classic', DESC, 300, 400, 0x7FFE);
    equal([big.number('#p-minutes'), big.number('#p-eggs')], ['300', '400'], 'a map opened with larger numbers shows them');
    const bigMore = (id) => big.q('#' + id).parentNode.children[2], bigLess = (id) => big.q('#' + id).parentNode.children[0];
    check(bigMore('p-minutes').disabled && !bigLess('p-minutes').disabled, 'plus is off beyond the range');
    big.button('Apply').click();
    equal([big.asked[big.asked.length - 1].minutes, big.asked[big.asked.length - 1].eggs, big.applied.length], [300, 400, 0], 'applied untouched, they are the same numbers and nothing changes');
    const edit = setup('classic', DESC, 300, 400, 0x7FFE);
    edit.q('#p-minutes').parentNode.children[0].click();
    equal(edit.number('#p-minutes'), '120', 'minus from 300 goes into the range');
    edit.type('301', '#p-eggs'); edit.q('#p-eggs').fire('change', {});
    equal(edit.number('#p-eggs'), '255', 'a number typed that the map did not come with must be in the range');
    // a refusal keeps the dialog open
    const r = setup('classic', DESC, 12, 9, 0x7FFE, { refuseEggs: true });
    r.type('77', '#p-eggs'); r.q('#p-eggs').fire('change', {});
    r.button('Apply').click();
    check(r.dialogs.isOpen() && r.applied.length === 0 && r.note() !== null && /^!The file ends before its egg word/.test(r.note().textContent), 'the core\'s refusal is shown and the dialog stays');
    r.type('78', '#p-eggs'); r.q('#p-eggs').fire('change', {});
    r.button('Cancel').click();
    check(!r.dialogs.isOpen() && r.applied.length === 0, 'and Cancel still changes nothing');
    // the history says no: the sentence is shown
    const h = setup('classic', DESC, 12, 9, 0x7FFE, { applyRefuses: 'It could not be done.' });
    h.type('Another');
    h.button('Apply').click();
    check(h.dialogs.isOpen() && h.note() !== null && /^!It could not be done\./.test(h.note().textContent), 'a history that refuses is told to the person, the dialog stays');
}

// ---- F: text is text ----
function caseF() {
    const markup = '<b>A</b> & "q"';
    const p = setup('classic', markup, 12, 9, 0x7FFE);
    equal(p.q('#p-desc').value, markup, 'markup in a description is shown as it is');
    p.type('<img src=x onerror=1>');
    equal(p.counter(), '21 of 29 characters', 'and counted');
    p.button('Cancel').click();
    const lines = Props.summary({ description: markup, minutes: 12, eggs: 9, tile: 0 });
    equal(lines.description, '“<b>A</b> & "q"”', 'the summary is text too (the page sets it with textContent)');
    // a description of the file that is not UTF-8 goes back as it came, unless it is touched
    const raw = Uint8Array.from([0x43, 0x61, 0x66, 0xE9, 0x20, 0x65, 0xE9]);
    const q = setup('classic', raw, 12, 9, 0x7FFE);
    equal([q.q('#p-desc').value, q.counter()], ['Café eé', '7 of 29 characters'], 'an old description is read one letter for a byte, and counted as the bytes it is');
    q.type('Café eé!');
    q.button('Apply').click();
    equal(Array.from(q.asked[q.asked.length - 1].description), [0x43, 0x61, 0x66, 0xC3, 0xA9, 0x20, 0x65, 0xC3, 0xA9, 0x21], 'a text that was touched goes as UTF-8');
    const u = setup('classic', raw, 12, 9, 0x7FFE);
    u.q('#p-desc').fire('input', {});
    u.button('Apply').click();
    check(u.applied.length === 0 && Array.from(u.asked[u.asked.length - 1].description).join() === Array.from(raw).join(), 'a text that was not touched goes back with the file\'s own bytes, and changes nothing');
    // the dialog's focus: Tab wraps from the last control to the first, with a box of lines in it
    const t = setup('extended', 'abc', 20, 12, 62);
    const items = t.all('textarea, input, button').filter((b) => !b.disabled);
    items[items.length - 1].focus();
    const key = t.doc.key('Tab');
    check(key.prevented && t.doc.activeElement === items[0], 'Tab from the last control goes to the first (the box of lines is among them)');
    t.doc.activeElement = items[0];
    const back = t.doc.key('Tab', { shiftKey: true });
    check(back.prevented && t.doc.activeElement === items[items.length - 1], 'and Shift+Tab goes back');
}

for (const [name, fn] of [['A the words', caseA], ['B the change', caseB], ['C the dialog on a Classic map', caseC], ['C2 a description of 30 bytes', caseC2], ['C3 the numbers and the ants', caseC3], ['D the dialog on an Extended map', caseD],
    ['D2 a text the core refuses', caseD2], ['E Apply and Cancel', caseE], ['F text is text', caseF]]) {
    process.stdout.write('  RUNNING: ' + name + ' ... ');
    const before = failures;
    try { fn(); } catch (e) { failures++; console.log('\n    FAILED: threw ' + (e && e.stack ? e.stack : e)); }
    if (failures === before) console.log('PASS');
}
console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
