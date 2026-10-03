// Runs the pages' OWN code that decides the shape of the picture and remembers the player's choice, on a table of addresses and of what a browser had remembered
// (web/shell.html: everything between ANTS_PAGE_BEGIN and ANTS_PAGE_END, and the selector's block; web/four.html: its helpers, the block of the shape and the selector's listener).
// 16:9 is the default everywhere: only the choice of the selector (the key `ants.aspect.v2`) or ?aspect=4:3 in the address gives the classic picture. A Classic 4:3 that the first
// versions of the pages remembered under `ants.aspect` is NOT read any more (every browser starts 16:9 once), and the selectors write the new key and nothing else.
// tests/scripts/test_web_aspect_default.py runs this with node (the quick tier); tests/scripts/web_aspect_check.py is the opt-in check in a real browser.
// usage: node web_aspect_key_check.js web/shell.html web/four.html     (exit 0: every row holds; every differing row is printed)
'use strict';
const fs = require('fs');

const NEW_KEY = 'ants.aspect.v2';
const OLD_KEY = 'ants.aspect';

// The lines between the line of `begin` and the line of `end`
function between(text, begin, end, path) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(path + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(path + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}

// localStorage as a browser gives it: `initial` is what was stored before; THROWS makes every call throw, as a private window of some browsers does
const THROWS = 'THROWS';
function makeStorage(initial) {
    const data = initial === THROWS ? {} : Object.assign({}, initial);
    const writes = [];
    const has = (key) => Object.prototype.hasOwnProperty.call(data, key);
    return {
        data,
        writes,
        getItem(key) { if (initial === THROWS) throw new Error('storage is blocked'); return has(key) ? data[key] : null; },
        setItem(key, value) { if (initial === THROWS) throw new Error('storage is blocked'); writes.push([key, String(value)]); data[key] = String(value); },
        removeItem(key) { if (initial === THROWS) throw new Error('storage is blocked'); delete data[key]; },
    };
}

function makeWindow(search, storage, answerToConfirm) {
    const win = {
        assigned: [],
        asked: [],
        location: {
            search,
            protocol: 'https:',
            host: 'example.test',
            href: 'https://example.test/' + search,
            assign(url) { win.assigned.push(url); },
        },
        localStorage: storage,
        confirm(question) { win.asked.push(question); return answerToConfirm; },
    };
    return win;
}

function element(attributes) {
    const el = {
        attributes: Object.assign({}, attributes || {}),
        listeners: {},
        value: '',
        getAttribute(name) { return Object.prototype.hasOwnProperty.call(el.attributes, name) ? el.attributes[name] : null; },
        setAttribute(name, value) { el.attributes[name] = String(value); },
        addEventListener(type, fn) { el.listeners[type] = fn; },
        classList: { add() {}, contains() { return false; } },
    };
    return el;
}

// web/shell.html: returns what the page decides and the buttons of its selector
function runShell(path, search, stored, options) {
    const text = fs.readFileSync(path, 'utf8');
    const page = between(text, 'ANTS_PAGE_BEGIN', 'ANTS_PAGE_END', path);
    const selector = between(text, 'ANTS_SELECTOR_BEGIN', 'ANTS_SELECTOR_END', path);
    const storage = makeStorage(stored);
    const win = makeWindow(search, storage, !(options && options.decline));
    const stage = element({ 'data-aspect': '16:9' });
    const buttons = [element({ 'data-aspect': '16:9', 'aria-checked': 'true' }), element({ 'data-aspect': '4:3', 'aria-checked': 'false' })];
    const body = element();
    const doc = {
        body,
        getElementById(id) { if (id === 'game-stage') return stage; throw new Error('the page asked for #' + id); },
        querySelectorAll(selectorText) { if (selectorText === '.seg button[data-aspect]') return buttons; throw new Error('the page asked for ' + selectorText); },
    };
    const code = page + '\n' + selector + '\nreturn { aspect: ANTS_ASPECT, source: ANTS_ASPECT_SOURCE, args: ANTS_ARGS, embed: ANTS_EMBED, key: ANTS_ASPECT_KEY };';
    const result = new Function('window', 'document', code)(win, doc);
    result.storage = storage;
    result.win = win;
    result.stage = stage;
    result.buttons = buttons;
    result.checked = buttons.map((b) => b.attributes['data-aspect'] + '=' + b.attributes['aria-checked']).join(' ');
    return result;
}

// web/four.html
function runFour(path, search, stored, options) {
    const text = fs.readFileSync(path, 'utf8');
    const helpers = between(text, 'HELPERS_BEGIN', 'HELPERS_END', path);
    const shape = between(text, 'SHAPE_BEGIN', 'SHAPE_END', path);
    const selector = between(text, 'SELECTOR_BEGIN', 'SELECTOR_END', path);
    const storage = makeStorage(stored);
    const win = makeWindow(search, storage, !(options && options.decline));
    const select = element();
    const body = element();
    const doc = {
        body,
        getElementById(id) { if (id === 'aspect-select') return select; throw new Error('the page asked for #' + id); },
    };
    const inRoom = options && options.room ? "'demo-room'" : "''";
    const code = 'var room = ' + inRoom + ';\n' + helpers + shape + selector + '\nreturn { aspect: aspect, fromAddress: aspectFromAddress, key: ASPECT_KEY };';
    const result = new Function('window', 'document', code)(win, doc);
    result.storage = storage;
    result.win = win;
    result.select = select;
    result.body = body;
    return result;
}

// [what it is, the address's query, what the browser had stored, the shape, where it came from]
const table = [
    ['nothing remembered, no address', '', {}, '16:9', 'default'],
    ['the selector remembered 4:3', '', { [NEW_KEY]: '4:3' }, '4:3', 'remembered'],
    ['the selector remembered 16:9', '', { [NEW_KEY]: '16:9' }, '16:9', 'remembered'],
    ['THE OLD KEY with 4:3 (the first versions of the pages) is not read', '', { [OLD_KEY]: '4:3' }, '16:9', 'default'],
    ['the old key with 4:3 and the new key with 16:9', '', { [OLD_KEY]: '4:3', [NEW_KEY]: '16:9' }, '16:9', 'remembered'],
    ['the old key with 16:9 and the new key with 4:3', '', { [OLD_KEY]: '16:9', [NEW_KEY]: '4:3' }, '4:3', 'remembered'],
    ['the old key with 4:3, the address asks for 4:3', '?aspect=4:3', { [OLD_KEY]: '4:3' }, '4:3', 'address'],
    ['the address asks for 4:3, nothing remembered', '?aspect=4:3', {}, '4:3', 'address'],
    ['the address asks for 4:3 with the colon escaped', '?aspect=4%3A3', {}, '4:3', 'address'],
    ['the address asks for 16:9 over a remembered 4:3', '?aspect=16:9', { [NEW_KEY]: '4:3' }, '16:9', 'address'],
    ['another parameter and ?aspect=4:3', '?x=1&aspect=4:3&y=2', {}, '4:3', 'address'],
    ['the first of a repeated parameter wins', '?aspect=16:9&aspect=4:3', {}, '16:9', 'address'],
    ['a remembered value that is no shape', '', { [NEW_KEY]: 'junk' }, '16:9', 'default'],
    ['a remembered value with a blank behind it', '', { [NEW_KEY]: '4:3 ' }, '16:9', 'default'],
    ['an address that is no shape is ignored', '?aspect=21:9', {}, '16:9', 'default'],
    ['an address that is no shape, a remembered 4:3', '?aspect=wide', { [NEW_KEY]: '4:3' }, '4:3', 'remembered'],
    ['a browser that refuses its storage (a private window)', '', THROWS, '16:9', 'default'],
    ['a browser that refuses its storage, the address asks for 4:3', '?aspect=4:3', THROWS, '4:3', 'address'],
];

let failed = 0;
function expect(label, got, want) {
    if (got !== want) {
        console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
        failed++;
    }
}

const [shellPath, fourPath] = [process.argv[2], process.argv[3]];
if (!shellPath || !fourPath) { console.log('usage: web_aspect_key_check.js shell.html four.html'); process.exit(2); }

try {
    // ---- the game page ----
    for (const [label, search, stored, aspect, source] of table) {
        const r = runShell(shellPath, search, stored);
        expect('shell.html, ' + label + ' (the shape)', r.aspect, aspect);
        expect('shell.html, ' + label + ' (where it came from)', r.source, source);
        expect('shell.html, ' + label + ' (--aspect for the game)', r.args.slice(-2).join(' '), '--aspect ' + aspect);
        expect('shell.html, ' + label + ' (the stage)', r.stage.attributes['data-aspect'], aspect);
        expect('shell.html, ' + label + ' (the selector shows it)', r.checked, aspect === '16:9' ? '16:9=true 4:3=false' : '16:9=false 4:3=true');
        expect('shell.html, ' + label + ' (nothing was written by merely loading)', JSON.stringify(r.storage.writes), '[]');
        expect('shell.html, the key', r.key, NEW_KEY);
    }
    // a frame of four.html (?embed=1) takes the address only: what the browser remembered does not reach it
    for (const [label, search, stored, aspect, source] of [
        ['a frame, 4:3 remembered', '?embed=1', { [NEW_KEY]: '4:3' }, '16:9', 'default'],
        ['a frame, the old 4:3 remembered', '?embed=1', { [OLD_KEY]: '4:3' }, '16:9', 'default'],
        ['a frame whose address says 4:3', '?embed=1&aspect=4:3', {}, '4:3', 'address'],
        ['a frame of a room, nothing remembered', '?join=/ws&room=abc&seat=1&embed=1', {}, '16:9', 'default'],
    ]) {
        const r = runShell(shellPath, search, stored);
        expect('shell.html, ' + label + ' (the shape)', r.aspect, aspect);
        expect('shell.html, ' + label + ' (where it came from)', r.source, source);
        expect('shell.html, ' + label + ' (embedded)', r.embed, true);
    }
    // the selector: it writes the new key and nothing else, and 4:3 only when it is clicked
    {
        const r = runShell(shellPath, '', {});
        r.buttons[1].listeners.click();                                              // Classic 4:3
        expect('shell.html selector, 4:3 clicked: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '4:3']]));
        expect('shell.html selector, 4:3 clicked: the old key is untouched', Object.prototype.hasOwnProperty.call(r.storage.data, OLD_KEY), false);
        expect('shell.html selector, 4:3 clicked: the page reloads with ?aspect=4:3', r.win.assigned.length === 1 && /[?&]aspect=4:3(&|$)/.test(r.win.assigned[0]), true);
        const again = runShell(shellPath, '', r.storage.data);                      // the next visit: what the selector remembered
        expect('shell.html selector, the next visit', again.aspect + ' ' + again.source, '4:3 remembered');
    }
    {
        const r = runShell(shellPath, '', {});
        r.buttons[0].listeners.click();                                              // 16:9 when it is 16:9: remembered, nothing to restart
        expect('shell.html selector, 16:9 clicked: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '16:9']]));
        expect('shell.html selector, 16:9 clicked: no reload', r.win.assigned.length, 0);
    }
    {
        const r = runShell(shellPath, '?join=/ws&room=abc&seat=1', {}, { decline: true });     // in a room the page asks first; No: nothing is written, nothing moves
        r.buttons[1].listeners.click();
        expect('shell.html selector in a room: it asks', r.win.asked.length, 1);
        expect('shell.html selector in a room, answered No: nothing is written', JSON.stringify(r.storage.writes), '[]');
        expect('shell.html selector in a room, answered No: no reload', r.win.assigned.length, 0);
    }
    {
        const r = runShell(shellPath, '', THROWS);                                  // a private window: the click still reloads with the address, nothing breaks
        r.buttons[1].listeners.click();
        expect('shell.html selector without storage: the page reloads with ?aspect=4:3', r.win.assigned.length === 1 && /[?&]aspect=4:3(&|$)/.test(r.win.assigned[0]), true);
    }

    // ---- the Play online page ----
    for (const [label, search, stored, aspect, source] of table) {
        const r = runFour(fourPath, search, stored);
        expect('four.html, ' + label + ' (the shape)', r.aspect, aspect);
        expect('four.html, ' + label + ' (the body)', r.body.attributes['data-aspect'], aspect);
        expect('four.html, ' + label + ' (the selector)', r.select.value, aspect);
        expect('four.html, ' + label + ' (the address names it)', r.fromAddress, source === 'address');
        expect('four.html, ' + label + ' (nothing was written by merely loading)', JSON.stringify(r.storage.writes), '[]');
        expect('four.html, the key', r.key, NEW_KEY);
    }
    {
        const r = runFour(fourPath, '', { [OLD_KEY]: '4:3' });
        r.select.value = '4:3';
        r.select.listeners.change();                                                 // the player picks Classic 4:3
        expect('four.html selector, 4:3 picked: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '4:3']]));
        expect('four.html selector, 4:3 picked: the old key is untouched', r.storage.data[OLD_KEY], '4:3');
        expect('four.html selector, 4:3 picked: the page reloads with ?aspect=4:3', r.win.assigned.length === 1 && /[?&]aspect=(4:3|4%3A3)(&|$)/.test(r.win.assigned[0]), true);
        const again = runFour(fourPath, '', r.storage.data);
        expect('four.html selector, the next visit', again.aspect, '4:3');
    }
    {
        const r = runFour(fourPath, '', {});
        r.select.value = '16:9';
        r.select.listeners.change();                                                 // the same picture: remembered, nothing to restart
        expect('four.html selector, 16:9 picked: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '16:9']]));
        expect('four.html selector, 16:9 picked: no reload', r.win.assigned.length, 0);
    }
    {
        const r = runFour(fourPath, '', {}, { room: true, decline: true });          // in a room it asks first; No: nothing is written, the select goes back
        r.select.value = '4:3';
        r.select.listeners.change();
        expect('four.html selector in a room: it asks', r.win.asked.length, 1);
        expect('four.html selector in a room, answered No: nothing is written', JSON.stringify(r.storage.writes), '[]');
        expect('four.html selector in a room, answered No: no reload', r.win.assigned.length, 0);
        expect('four.html selector in a room, answered No: the select shows 16:9 again', r.select.value, '16:9');
    }
} catch (e) {
    console.log('FAIL ' + e.message);
    failed++;
}

if (failed === 0) console.log('web aspect key: ' + table.length + ' rows x 2 pages and the selectors, 0 failures');
process.exit(failed === 0 ? 0 : 1);
