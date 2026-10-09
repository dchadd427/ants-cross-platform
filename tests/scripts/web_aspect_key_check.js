// Runs the pages' OWN code that decides the shape of the picture and remembers the player's choice, on a table of addresses and of what a browser had remembered
// (web/shell.html: everything between ANTS_PAGE_BEGIN and ANTS_PAGE_END, and the selector's block; web/lobby.html, the front page: its helpers, the block of the shape and the listeners of the picture's two buttons).
// 16:9 is the default everywhere: only the choice of the selector (the key `ants.aspect.v2`) or ?aspect= in the address gives another picture (the game page has four shapes, 4:3, 16:10, 16:9 and
// 21:9; the front page, which is not changed, has two). A Classic 4:3 that the first versions of the pages remembered under `ants.aspect` is NOT read any more (every browser starts 16:9 once), and
// the selectors write the new key and nothing else. The game page's selector changes the picture AT ONCE, in the running game (no reload, no question, the room and the seat stay): what it
// stores, tells the game (ants_set_aspect), puts in the address and shows is checked here on a page of fakes; "fills your screen" and the shapes' boxes too.
// tests/scripts/test_web_aspect_default.py runs this with node (the quick tier); tests/scripts/web_aspect_check.py is the opt-in check in a real browser.
// usage: node web_aspect_key_check.js web/shell.html web/lobby.html     (exit 0: every row holds; every differing row is printed)
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

function makeWindow(search, storage, answerToConfirm, screen) {
    const win = {
        assigned: [],
        asked: [],
        location: {
            search,
            protocol: 'https:',
            host: 'example.test',
            href: 'https://example.test/' + search,
            pathname: '/',
            hash: '',
            assign(url) { win.assigned.push(url); },
        },
        history: { state: null, replaced: [], replaceState(state, title, url) { win.history.replaced.push(url); } },
        screen: screen || { width: 1920, height: 1080 },
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

// a group of radio buttons as a browser has them: checking one unchecks the others of the group (the page's own `checked = true` and the person's pick both go through this)
function radioGroup(values) {
    const group = {};
    const state = {};
    for (const value of values) {
        const el = element();
        state[value] = false;
        Object.defineProperty(el, 'checked', { get() { return state[value]; }, set(on) { if (on) for (const other of values) state[other] = false; state[value] = !!on; } });
        group[value] = el;
    }
    return group;
}
// the person picks a shape: the button is checked (the others are unchecked) and its change event comes
function pick(r, shape) { r.radios[shape].checked = true; r.radios[shape].listeners.change(); }
const shown = (r) => ['16:9', '4:3'].filter((shape) => r.radios[shape].checked).join('+');

const SHELL_SHAPES = ['4:3', '16:10', '16:9', '21:9'];                       // the game page's selector, left to right

// web/shell.html: returns what the page decides and the buttons of its selector
function runShell(path, search, stored, options) {
    const text = fs.readFileSync(path, 'utf8');
    const page = between(text, 'ANTS_PAGE_BEGIN', 'ANTS_PAGE_END', path);
    const selector = between(text, 'ANTS_SELECTOR_BEGIN', 'ANTS_SELECTOR_END', path);
    const storage = makeStorage(stored);
    const win = makeWindow(search, storage, !(options && options.decline), options && options.screen);
    const stage = element({ 'data-aspect': '16:9' });
    const buttons = SHELL_SHAPES.map((shape) => {
        const button = element({ 'data-aspect': shape, 'aria-checked': shape === '16:9' ? 'true' : 'false' });
        const tag = element();
        tag.hidden = true;                                                           // (the markup hides every tag: the script shows one)
        button.tag = tag;
        button.parentNode = { querySelector(selectorText) { return selectorText === '.fit-tag' ? tag : null; } };
        return button;
    });
    const viewBar = element();
    viewBar.classes = [];
    viewBar.classList = { add(name) { viewBar.classes.push(name); }, contains(name) { return viewBar.classes.indexOf(name) !== -1; } };
    const body = element();
    const doc = {
        body,
        getElementById(id) { if (id === 'game-stage') return stage; if (id === 'view-bar') return viewBar; throw new Error('the page asked for #' + id); },
        querySelectorAll(selectorText) { if (selectorText === '.seg button[data-aspect]') return buttons; throw new Error('the page asked for ' + selectorText); },
    };
    // the game that the page talks to: `ready` is the page's isReadyToPlay, `module` makes the Module of the program (given the window, so that it can look at where the page has gone by then, and
    // the events: what the page does, in order: the game's calls and the box's layout)
    const events = [];
    const ready = !!(options && options.ready);
    const fake = options && options.module ? options.module(win, events) : undefined;
    // holdsThisSeat is the name gate's (outside these blocks): `holds` says whether this browser holds the key of its seat (or throws, if it is a function); its arguments are recorded
    const holdsCalls = [];
    const holdsFn = options && options.holds !== undefined ? function (search, args, storage, now) { holdsCalls.push([search, args.indexOf('--join-url') !== -1, storage === win.localStorage, typeof now]); return typeof options.holds === 'function' ? options.holds() : options.holds; } : undefined;
    // (relayout is the page's box: it is sized, and the game's window system told, by the script outside these blocks; here it is a record)
    const code = 'var isReadyToPlay = ' + ready + '; var Module = fakeModule; var holdsThisSeat = fakeHolds; var stageElement = fakeStage; var relayoutCalls = [];\n'
        + 'function relayout(force) { events.push("relayout"); relayoutCalls.push(force); }\n' + page + '\n' + selector
        + '\nreturn { aspect: ANTS_ASPECT, source: ANTS_ASPECT_SOURCE, args: ANTS_ARGS, embed: ANTS_EMBED, key: ANTS_ASPECT_KEY, page: ANTS_PAGE, relayoutCalls: relayoutCalls, sync: antsSyncAspect, set: antsSetAspect,'
        + ' setReady: function (on) { isReadyToPlay = on; }, now: function () { return ANTS_ASPECT; } };';
    const result = new Function('window', 'document', 'fakeModule', 'fakeHolds', 'fakeStage', 'events', code)(win, doc, fake, holdsFn, stage, events);
    result.storage = storage;
    result.win = win;
    result.holdsCalls = holdsCalls;
    result.stage = stage;
    result.buttons = buttons;
    result.viewBar = viewBar;
    result.events = events;
    // (read when asked: what the selector shows and where the tag stands change as the buttons are pressed)
    Object.defineProperty(result, 'checked', { get() { return buttons.map((b) => b.attributes['data-aspect'] + '=' + b.attributes['aria-checked']).join(' '); } });
    Object.defineProperty(result, 'tags', { get() { return buttons.filter((b) => !b.tag.hidden).map((b) => b.attributes['data-aspect']).join('+'); } });
    return result;
}
const checkedString = (shape) => SHELL_SHAPES.map((s) => s + '=' + (s === shape ? 'true' : 'false')).join(' ');

// web/lobby.html
function runLobby(path, search, stored, options) {
    const text = fs.readFileSync(path, 'utf8');
    const helpers = between(text, 'HELPERS_BEGIN', 'HELPERS_END', path);
    const shape = between(text, 'SHAPE_BEGIN', 'SHAPE_END', path);
    const selector = between(text, 'SELECTOR_BEGIN', 'SELECTOR_END', path);
    const storage = makeStorage(stored);
    const win = makeWindow(search, storage, !(options && options.decline));
    const radios = radioGroup(['16:9', '4:3']);
    const body = element();
    const doc = {
        body,
        getElementById(id) { if (id === 'aspect-16-9') return radios['16:9']; if (id === 'aspect-4-3') return radios['4:3']; throw new Error('the page asked for #' + id); },
    };
    const inRoom = options && options.room ? "'k7m2xq'" : "''";
    const code = 'var room = ' + inRoom + ';\n' + helpers + shape + selector + '\nreturn { aspect: aspect, fromAddress: aspectFromAddress, key: ASPECT_KEY };';
    const result = new Function('window', 'document', code)(win, doc);
    result.storage = storage;
    result.win = win;
    result.radios = radios;
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
    ['an address that is no shape is ignored', '?aspect=3:2', {}, '16:9', 'default'],
    ['an address that is no shape, a remembered 4:3', '?aspect=wide', { [NEW_KEY]: '4:3' }, '4:3', 'remembered'],
    ['a browser that refuses its storage (a private window)', '', THROWS, '16:9', 'default'],
    ['a browser that refuses its storage, the address asks for 4:3', '?aspect=4:3', THROWS, '4:3', 'address'],
];

// the two shapes that only the game page has (the front page offers 16:9 and Classic 4:3): [what it is, the address's query, what the browser had stored, the shape, where it came from]
const shellOnly = [
    ['the selector remembered 16:10', '', { [NEW_KEY]: '16:10' }, '16:10', 'remembered'],
    ['the selector remembered 21:9', '', { [NEW_KEY]: '21:9' }, '21:9', 'remembered'],
    ['the address asks for 16:10, nothing remembered', '?aspect=16:10', {}, '16:10', 'address'],
    ['the address asks for 21:9, nothing remembered', '?aspect=21:9', {}, '21:9', 'address'],
    ['the address asks for 21:9 with the colon escaped', '?aspect=21%3A9', {}, '21:9', 'address'],
    ['the address asks for 21:9 over a remembered 16:10', '?aspect=21:9', { [NEW_KEY]: '16:10' }, '21:9', 'address'],
    ['the address asks for 16:9 over a remembered 21:9', '?aspect=16:9', { [NEW_KEY]: '21:9' }, '16:9', 'address'],
    ['another parameter and ?aspect=16:10', '?x=1&aspect=16:10&y=2', {}, '16:10', 'address'],
    ['an address that is no shape, a remembered 21:9', '?aspect=wide', { [NEW_KEY]: '21:9' }, '21:9', 'remembered'],
    ['a remembered 16:10 with a blank behind it', '', { [NEW_KEY]: '16:10 ' }, '16:9', 'default'],
    ['a remembered ratio that is no shape: 32:9', '', { [NEW_KEY]: '32:9' }, '16:9', 'default'],
    ['a browser that refuses its storage, the address asks for 21:9', '?aspect=21:9', THROWS, '21:9', 'address'],
];

// what the front page makes of them: it has two shapes, anything else is ignored (it shows and passes on 16:9)
const lobbyIgnores = [
    ['a remembered 16:10', '', { [NEW_KEY]: '16:10' }, '16:9', 'default'],
    ['a remembered 21:9', '', { [NEW_KEY]: '21:9' }, '16:9', 'default'],
    ['an address that asks for 21:9', '?aspect=21:9', {}, '16:9', 'default'],
    ['an address that asks for 16:10, a remembered 4:3', '?aspect=16:10', { [NEW_KEY]: '4:3' }, '4:3', 'remembered'],
];

let failed = 0;
function expect(label, got, want) {
    if (got !== want) {
        console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
        failed++;
    }
}

const [shellPath, lobbyPath] = [process.argv[2], process.argv[3]];
if (!shellPath || !lobbyPath) { console.log('usage: web_aspect_key_check.js shell.html lobby.html'); process.exit(2); }

try {
    // ---- the game page ----
    for (const [label, search, stored, aspect, source] of table.concat(shellOnly)) {
        const r = runShell(shellPath, search, stored);
        expect('shell.html, ' + label + ' (the shape)', r.aspect, aspect);
        expect('shell.html, ' + label + ' (where it came from)', r.source, source);
        expect('shell.html, ' + label + ' (--aspect for the game)', r.args.slice(-2).join(' '), '--aspect ' + aspect);
        expect('shell.html, ' + label + ' (the stage)', r.stage.attributes['data-aspect'], aspect);
        expect('shell.html, ' + label + ' (the selector shows it)', r.checked, checkedString(aspect));
        expect('shell.html, ' + label + ' (nothing was written by merely loading)', JSON.stringify(r.storage.writes), '[]');
        expect('shell.html, the key', r.key, NEW_KEY);
    }
    // a frame of the lobby's game (?embed=1) takes the address only: what the browser remembered does not reach it
    for (const [label, search, stored, aspect, source] of [
        ['a frame, 4:3 remembered', '?embed=1', { [NEW_KEY]: '4:3' }, '16:9', 'default'],
        ['a frame, 21:9 remembered', '?embed=1', { [NEW_KEY]: '21:9' }, '16:9', 'default'],
        ['a frame, the old 4:3 remembered', '?embed=1', { [OLD_KEY]: '4:3' }, '16:9', 'default'],
        ['a frame whose address says 4:3', '?embed=1&aspect=4:3', {}, '4:3', 'address'],
        ['a frame whose address says 16:10', '?embed=1&aspect=16:10', {}, '16:10', 'address'],
        ['a frame of a room, nothing remembered', '?join=/ws&room=abc&seat=1&embed=1', {}, '16:9', 'default'],
    ]) {
        const r = runShell(shellPath, search, stored);
        expect('shell.html, ' + label + ' (the shape)', r.aspect, aspect);
        expect('shell.html, ' + label + ' (where it came from)', r.source, source);
        expect('shell.html, ' + label + ' (embedded)', r.embed, true);
    }

    // the page's own logic: the number that the game takes, the shape nearest to a screen, the box of each shape
    {
        const { page } = runShell(shellPath, '', {});
        expect('shell.html, the shapes\' numbers (the selector\'s order, the game\'s kAllAspects)', SHELL_SHAPES.map((s) => page.shapeNumber(s)).join(','), '0,1,2,3');
        expect('shell.html, a text that is no shape has no number', [page.shapeNumber('3:2'), page.shapeNumber(''), page.shapeNumber(null), page.shapeNumber(undefined), page.shapeNumber(16)].join(','), '-1,-1,-1,-1,-1');
        // [the screen, its shape]: common screens (CSS pixels), then screens that no shape fills
        for (const [w, h, shape] of [[1920, 1080, '16:9'], [1366, 768, '16:9'], [2560, 1440, '16:9'], [3840, 2160, '16:9'], [1440, 900, '16:10'], [1680, 1050, '16:10'], [1920, 1200, '16:10'], [2560, 1600, '16:10'],
                                      [3440, 1440, '21:9'], [2560, 1080, '21:9'], [3840, 1600, '21:9'], [1280, 1024, '4:3'], [1024, 768, '4:3'], [1600, 1200, '4:3'], [2256, 1504, '16:10'],
                                      [5120, 1440, null], [1000, 1000, null], [768, 1024, null], [0, 0, null], [NaN, 600, null], [1920, -1, null], [Infinity, 1080, null]]) {
            expect('shell.html, the shape nearest to a ' + w + ' x ' + h + ' screen', page.shapeForScreen(w, h), shape);
        }
        // the box: the biggest whole number of steps of the shape's smallest whole numbers that fits, at least one; the canvas's scale is the same on both axes
        for (const [availW, availH, dpr, shape, bw, bh] of [[1280, 720, 1, '16:9', 1280, 720], [1280, 720, 1, '16:10', 1152, 720], [1280, 720, 1, '21:9', 1274, 546], [960, 720, 1, '4:3', 960, 720],
                                                           [1280, 800, 2, '16:10', 2560, 1600], [1200, 700, 1.5, '21:9', 1799, 771], [10, 10, 1, '21:9', 7, 3], [10, 10, 1, '16:10', 8, 5], [0, 0, 1, '16:10', 8, 5]]) {
            const fit = page.fitBox(availW, availH, dpr, shape);
            const canvas = { '4:3': [640, 480], '16:10': [960, 600], '16:9': [960, 540], '21:9': [1260, 540] }[shape];
            expect('shell.html, the box of ' + shape + ' in ' + availW + ' x ' + availH + ' at ' + dpr, fit.bw + ' x ' + fit.bh, bw + ' x ' + bh);
            expect('shell.html, the box of ' + shape + ' in ' + availW + ' x ' + availH + ' at ' + dpr + ': the canvas fills it exactly (the same scale on both axes)', fit.bw * canvas[1] === fit.bh * canvas[0], true);
        }
        expect('shell.html, a box for a text that is no shape is the 16:9 one', JSON.stringify(page.fitBox(1280, 720, 1, 'wide')), JSON.stringify(page.fitBox(1280, 720, 1, '16:9')));
    }

    // "fills your screen": under the shape nearest to the computer's screen, and nowhere else; the bar has room for it (a screen that no shape fills says nothing)
    for (const [w, h, shape] of [[1920, 1080, '16:9'], [1440, 900, '16:10'], [3440, 1440, '21:9'], [1280, 1024, '4:3'], [5120, 1440, ''], [1000, 1000, '']]) {
        const r = runShell(shellPath, '', {}, { screen: { width: w, height: h } });
        expect('shell.html, the tag on a ' + w + ' x ' + h + ' screen', r.tags, shape);
        expect('shell.html, the tag on a ' + w + ' x ' + h + ' screen: the bar makes room for it', r.viewBar.classes.join(' '), shape ? 'has-fit-tag' : '');
        expect('shell.html, the tag on a ' + w + ' x ' + h + ' screen: it does not depend on the shape that is shown', r.checked, checkedString('16:9'));
    }
    {
        const r = runShell(shellPath, '?aspect=21:9', {}, { screen: { width: 1440, height: 900 } });
        expect('shell.html, the tag stays under the screen\'s shape (16:10) while another is shown', r.tags, '16:10');
        r.buttons[0].listeners.click();
        expect('shell.html, ... and when another shape is pressed', r.tags, '16:10');
    }
    {
        const r = runShell(shellPath, '', {}, { screen: { get width() { throw new Error('no screen'); }, height: 900 } });   // (a browser that refuses to say: no tag, the selector works)
        expect('shell.html, a screen that cannot be asked: no tag, no breakage', r.tags + '|' + r.viewBar.classes.join(' '), '|');
        r.buttons[1].listeners.click();
        expect('shell.html, ... and the selector still works', r.aspect + ' ' + r.now(), '16:9 16:10');
    }

    // THE SELECTOR CHANGES THE PICTURE AT ONCE: no reload, no question, nothing to leave; it remembers, tells the game, sizes the box and puts the shape in the address
    for (const [index, shape] of [[0, '4:3'], [1, '16:10'], [3, '21:9']]) {
        const r = runShell(shellPath, '', {});
        r.buttons[index].listeners.click();
        const what = 'shell.html selector, ' + shape + ' pressed';
        expect(what + ': what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, shape]]));
        expect(what + ': the old key is untouched', Object.prototype.hasOwnProperty.call(r.storage.data, OLD_KEY), false);
        expect(what + ': the page does not reload, nothing is asked', r.win.assigned.length + ' ' + r.win.asked.length, '0 0');
        expect(what + ': the picture is the new shape', r.now(), shape);
        expect(what + ': the stage has the shape (the box\'s shape in CSS)', r.stage.attributes['data-aspect'], shape);
        expect(what + ': the selector shows it', r.checked, checkedString(shape));
        expect(what + ': a game that has not started starts with it (--aspect)', r.args.slice(-2).join(' '), '--aspect ' + shape);
        expect(what + ': the box is laid out again, and the game\'s window system told (a forced layout)', JSON.stringify(r.relayoutCalls), '[true]');
        expect(what + ': the address has it, without a new entry in the history', r.win.history.replaced.length === 1 && new RegExp('^/\\?aspect=' + shape + '$').test(r.win.history.replaced[0]), true);
        const again = runShell(shellPath, '', r.storage.data);                      // the next visit: what the selector remembered
        expect('shell.html selector, the next visit', again.aspect + ' ' + again.source, shape + ' remembered');
    }
    {
        const r = runShell(shellPath, '', {});
        r.buttons[2].listeners.click();                                              // 16:9 when it is 16:9: remembered, nothing to change
        expect('shell.html selector, 16:9 pressed on 16:9: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '16:9']]));
        expect('shell.html selector, 16:9 pressed on 16:9: nothing moves', r.win.assigned.length + ' ' + r.relayoutCalls.length + ' ' + r.win.history.replaced.length + ' ' + r.events.length, '0 0 0 0');
    }
    {
        // one shape after another: each press is its own change (16:9 to 21:9 to 4:3 to 16:10 and back), the last wins; every change tells the game and lays the box out, the game first
        const calls = [];
        const r = runShell(shellPath, '?join=/ws&room=abc&seat=1&name=Ann', {}, { ready: true, module: (win, events) => ({ _ants_set_aspect(n) { events.push('game:' + n); calls.push(n); return 1; }, _ants_leave_match() { win.left = true; } }) });
        for (const index of [3, 0, 1, 2]) r.buttons[index].listeners.click();
        expect('shell.html selector, four presses in a joined match: the game is told each shape\'s number, then the box is laid out, every time', r.events.join(' '), 'game:3 relayout game:0 relayout game:1 relayout game:2 relayout');
        expect('shell.html selector, four presses in a joined match: the picture is the last', r.now() + ' ' + r.checked, '16:9 ' + checkedString('16:9'));
        expect('shell.html selector, four presses in a joined match: no reload, no question, the game is not told to leave', r.win.assigned.length + ' ' + r.win.asked.length + ' ' + (r.win.left === true), '0 0 false');
        expect('shell.html selector, four presses in a joined match: the room, the seat and the name stay in the address, the shape follows', r.win.history.replaced.map((u) => /[?&]join=(%2F|\/)ws/.test(u) && /[?&]room=abc(&|$)/.test(u) && /[?&]seat=1(&|$)/.test(u) && /[?&]name=Ann(&|$)/.test(u)).join(',') + ' ' + r.win.history.replaced.map((u) => (u.match(/aspect=([^&]*)/) || [])[1]).join(' '), 'true,true,true,true 21:9 4:3 16:10 16:9');
        expect('shell.html selector, four presses in a joined match: the choice of each press is remembered', JSON.stringify(r.storage.writes), JSON.stringify(['21:9', '4:3', '16:10', '16:9'].map((v) => [NEW_KEY, v])));
    }
    // the same in a joined match of a browser that holds its seat's key and in one that does not (a waiting room): no question, no Leave (the seat is kept: the connection is never touched)
    for (const holds of [true, false]) {
        const r = runShell(shellPath, '?join=/ws&room=abc', {}, { ready: true, holds, module: (win) => ({ _ants_match_running() { return 1; }, _ants_set_aspect() { return 1; }, _ants_leave_match() { win.left = true; } }) });
        r.buttons[3].listeners.click();
        expect('shell.html selector, a room' + (holds ? ' with the key held' : ' still waiting') + ': no question, no Leave, no reload', r.win.asked.length + ' ' + (r.win.left === true) + ' ' + r.win.assigned.length, '0 false 0');
        expect('shell.html selector, a room' + (holds ? ' with the key held' : ' still waiting') + ': the picture changed, and the key lookup was not needed', r.now() + ' ' + r.holdsCalls.length, '21:9 0');
    }
    {
        const r = runShell(shellPath, '', {}, { ready: true, module: () => ({ _ants_match_running() { return 1; }, _ants_set_aspect() { return 1; } }) });
        r.buttons[1].listeners.click();
        expect('shell.html selector, a match of this computer: no question, no reload, the picture changed', r.win.asked.length + ' ' + r.win.assigned.length + ' ' + r.now(), '0 0 16:10');
    }
    // a game that is not up yet: nothing to tell; it starts with the shape (--aspect), and the postRun hook asks again for a game that began with the shape before
    {
        const calls = [];
        const r = runShell(shellPath, '', {}, { ready: false, module: () => ({ _ants_set_aspect(n) { calls.push(n); return 1; } }) });
        r.buttons[3].listeners.click();
        expect('shell.html selector, the game not ready yet: it is not told (an export called while the program compiles is undefined for good)', JSON.stringify(calls), '[]');
        expect('shell.html selector, the game not ready yet: the page changed all the same', r.now() + ' ' + r.args.slice(-2).join(' ') + ' ' + JSON.stringify(r.relayoutCalls), '21:9 --aspect 21:9 [true]');
        r.setReady(true);
        expect('shell.html, the postRun hook gives the game the shape that the page has now (it began with another)', r.sync() + ' ' + JSON.stringify(calls), '1 [3]');
        expect('shell.html, ... and again: the same number, the game keeps it', r.sync() + ' ' + JSON.stringify(calls), '1 [3,3]');
    }
    for (const [what, module] of [['a game without the export', () => ({})], ['a game whose export throws', () => ({ _ants_set_aspect() { throw new Error('the game is gone'); } })], ['no Module at all', () => undefined]]) {
        const r = runShell(shellPath, '?join=/ws&room=abc&seat=1', {}, { ready: true, module });
        r.buttons[1].listeners.click();
        expect('shell.html selector with ' + what + ': the page changes all the same (box, stage, address), nothing reloads', r.now() + ' ' + JSON.stringify(r.relayoutCalls) + ' ' + r.win.assigned.length + ' ' + r.stage.attributes['data-aspect'], '16:10 [true] 0 16:10');
        expect('shell.html selector with ' + what + ': ... and the sync says that the game did not answer', r.sync(), -1);
    }
    {
        const r = runShell(shellPath, '', {}, { ready: true, module: () => ({ _ants_set_aspect() { return 0; } }) });
        r.buttons[1].listeners.click();
        expect('shell.html, a game that does not know the number: the sync returns what the game said', r.sync(), 0);
    }
    {
        const r = runShell(shellPath, '', THROWS);                                  // a private window: the click still changes the picture, nothing breaks
        r.buttons[1].listeners.click();
        expect('shell.html selector without storage: the picture changes (no reload)', r.now() + ' ' + r.win.assigned.length + ' ' + JSON.stringify(r.relayoutCalls), '16:10 0 [true]');
    }
    {
        const r = runShell(shellPath, '', {});                                       // an address that cannot be rewritten: the picture changes, the browser remembers
        r.win.history.replaceState = () => { throw new Error('a sandboxed frame'); };
        r.buttons[3].listeners.click();
        expect('shell.html selector, an address that cannot be rewritten: the picture changes all the same', r.now() + ' ' + JSON.stringify(r.storage.writes), '21:9 ' + JSON.stringify([[NEW_KEY, '21:9']]));
    }
    {
        const r = runShell(shellPath, '', {});
        r.set('3:2');                                                                // a text that is no shape never changes anything
        r.set(null);
        r.set('16:9');
        expect('shell.html, antsSetAspect with a text that is no shape (or the shape it has) changes nothing', r.now() + ' ' + r.relayoutCalls.length + ' ' + r.win.history.replaced.length, '16:9 0 0');
    }

    // ---- the front page ----
    for (const [label, search, stored, aspect, source] of table) {
        const r = runLobby(lobbyPath, search, stored);
        expect('lobby.html, ' + label + ' (the shape)', r.aspect, aspect);
        expect('lobby.html, ' + label + ' (the body)', r.body.attributes['data-aspect'], aspect);
        expect('lobby.html, ' + label + ' (the button that is checked)', shown(r), aspect);
        expect('lobby.html, ' + label + ' (the address names it)', r.fromAddress, source === 'address');
        expect('lobby.html, ' + label + ' (nothing was written by merely loading)', JSON.stringify(r.storage.writes), '[]');
        expect('lobby.html, the key', r.key, NEW_KEY);
    }
    for (const [label, search, stored, aspect, source] of lobbyIgnores) {
        const r = runLobby(lobbyPath, search, stored);
        expect('lobby.html, ' + label + ' (the shape: the front page has two)', r.aspect, aspect);
        expect('lobby.html, ' + label + ' (the button that is checked)', shown(r), aspect);
        expect('lobby.html, ' + label + ' (the address names it)', r.fromAddress, source === 'address');
    }
    {
        const r = runLobby(lobbyPath, '', { [OLD_KEY]: '4:3' });
        pick(r, '4:3');                                                              // the player picks Classic 4:3
        expect('lobby.html selector, 4:3 picked: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '4:3']]));
        expect('lobby.html selector, 4:3 picked: the old key is untouched', r.storage.data[OLD_KEY], '4:3');
        expect('lobby.html selector, 4:3 picked: the page reloads with ?aspect=4:3', r.win.assigned.length === 1 && /[?&]aspect=(4:3|4%3A3)(&|$)/.test(r.win.assigned[0]), true);
        const again = runLobby(lobbyPath, '', r.storage.data);
        expect('lobby.html selector, the next visit', again.aspect + ' ' + shown(again), '4:3 4:3');
    }
    {
        const r = runLobby(lobbyPath, '', {});
        r.radios['16:9'].listeners.change();                                         // the same picture again (a change event that names the shape that is already there): remembered, nothing to restart
        expect('lobby.html selector, 16:9 again: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '16:9']]));
        expect('lobby.html selector, 16:9 again: no reload', r.win.assigned.length, 0);
    }
    {
        const r = runLobby(lobbyPath, '', {}, { room: true, decline: true });        // in a room it asks first; No: nothing is written, the buttons go back
        pick(r, '4:3');
        expect('lobby.html selector in a room: it asks', r.win.asked.length, 1);
        expect('lobby.html selector in a room, answered No: nothing is written', JSON.stringify(r.storage.writes), '[]');
        expect('lobby.html selector in a room, answered No: no reload', r.win.assigned.length, 0);
        expect('lobby.html selector in a room, answered No: 16:9 is checked again', shown(r), '16:9');
    }
    {
        const r = runLobby(lobbyPath, '', {}, { room: true });                       // in a room, answered Yes: remembered and reloaded
        pick(r, '4:3');
        expect('lobby.html selector in a room, answered Yes: remembered and reloaded', JSON.stringify(r.storage.writes) + ' ' + r.win.assigned.length, JSON.stringify([[NEW_KEY, '4:3']]) + ' 1');
    }
    {
        const r = runLobby(lobbyPath, '', { [NEW_KEY]: '4:3' });                     // 4:3 is there: picking 16:9 gives the default picture back
        pick(r, '16:9');
        expect('lobby.html selector, 16:9 picked over 4:3: remembered, the page reloads with ?aspect=16:9', JSON.stringify(r.storage.writes) + ' ' + /[?&]aspect=(16:9|16%3A9)(&|$)/.test(r.win.assigned[0] || ''), JSON.stringify([[NEW_KEY, '16:9']]) + ' true');
    }
} catch (e) {
    console.log('FAIL ' + e.message);
    failed++;
}

if (failed === 0) console.log('web aspect key: ' + table.length + ' rows x 2 pages, ' + shellOnly.length + ' rows of the game page\'s two other shapes, and the selectors, 0 failures');
process.exit(failed === 0 ? 0 : 1);
