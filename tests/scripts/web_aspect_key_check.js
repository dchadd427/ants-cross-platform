// Runs the pages' OWN code that decides the shape of the picture and remembers the player's choice, on a table of addresses and of what a browser had remembered
// (web/shell.html: everything between ANTS_PAGE_BEGIN and ANTS_PAGE_END, and the selector's block; web/lobby.html, the front page: its helpers, the table of the four shapes, the block of the picture's
// shape, the selector's listener, and the functions that build the addresses of its hand-offs: the Rejoin / START address of a lobby (rejoinQuery) and the address of a game on this computer (localGameQuery)).
// 16:9 is the default everywhere: only the choice of the selector (the key `ants.aspect.v2`) or ?aspect= in the address gives another picture (both pages have the same four shapes, 4:3, 16:10, 16:9 and
// 21:9: the front page's footer has them as its "Screen" buttons "Classic 4:3", "16:10", "16:9" and "21:9", with the "fills your screen" tag under the one nearest to the computer's screen). A Classic 4:3 that the first versions of the pages remembered under `ants.aspect` is NOT read any more (every browser
// starts 16:9 once), and the selectors write the new key and nothing else. The game page's selector changes the picture AT ONCE, in the running game (no reload, no question, the room and the seat
// stay): what it stores, tells the game (ants_set_aspect), puts in the address and shows is checked here on a page of fakes; "fills your screen" and the shapes' boxes too.
// The front page's selector (the lobby is the page's only mode) keeps the match and the room, changes the shape of the page at once, remembers it, and the address of the hand-off to the game
// (START, Rejoin, a game for one) carries it (`aspect=`).
// tests/scripts/test_web_aspect_default.py runs this with node (the quick tier); tests/scripts/web_aspect_check.py is the opt-in check in a real browser.
// usage: node web_aspect_key_check.js web/shell.html web/lobby.html     (exit 0: every row holds; every differing row is printed)
'use strict';
const fs = require('fs');
const nodePath = require('path');

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
const shown = (r) => ['4:3', '16:10', '16:9', '21:9'].filter((shape) => r.radios[shape].checked).join('+');

const SHELL_SHAPES = ['4:3', '16:10', '16:9', '21:9'];                       // the game page's selector, left to right

// web/shell.html: returns what the page decides and the buttons of its selector
function runShell(path, search, stored, options) {
    const text = fs.readFileSync(path, 'utf8');
    const page = between(text, 'ANTS_PAGE_BEGIN', 'ANTS_PAGE_END', path);
    const selector = between(text, 'ANTS_SELECTOR_BEGIN', 'ANTS_SELECTOR_END', path);
    const hook = text.match(/postRun: \[(function\(\) \{[\s\S]*?\n {12}\})\]/);                  // (the page's own postRun hook: run as it is written, not a copy of it)
    if (!hook) throw new Error(path + ': the postRun hook of the Module was not found');
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
    const code = 'var isReadyToPlay = ' + ready + '; var Module = fakeModule; var holdsThisSeat = fakeHolds; var stageElement = fakeStage; var relayoutCalls = []; var progressContainer = { style: {} }; function hideLoadingScreen() {}\n'
        + 'function relayout(force) { events.push("relayout"); relayoutCalls.push(force); }\n' + page + '\n' + selector
        + '\nvar postRunHook = ' + hook[1] + ';'
        + '\nreturn { aspect: ANTS_ASPECT, source: ANTS_ASPECT_SOURCE, args: ANTS_ARGS, key: ANTS_ASPECT_KEY, page: ANTS_PAGE, relayoutCalls: relayoutCalls, sync: antsSyncAspect, set: antsSetAspect,'
        + ' postRun: postRunHook, ready: function () { return isReadyToPlay; }, setReady: function (on) { isReadyToPlay = on; }, now: function () { return ANTS_ASPECT; } };';
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

// web/lobby.html.
function runLobby(path, search, stored, options) {
    const text = fs.readFileSync(path, 'utf8');
    const helpers = between(text, 'HELPERS_BEGIN', 'HELPERS_END', path);
    const fill = between(text, 'FILL_BEGIN', 'FILL_END', path);
    const lobby = between(text, 'LOBBY_BEGIN', 'LOBBY_END', path);
    const shapes = between(text, 'SHAPES_BEGIN', 'SHAPES_END', path);
    const shape = between(text, 'SHAPE_BEGIN', 'SHAPE_END', path);
    const rejoin = between(text, 'REJOIN_BEGIN', 'REJOIN_END', path);
    const selector = between(text, 'SELECTOR_BEGIN', 'SELECTOR_END', path);
    const rules = require(nodePath.resolve(nodePath.dirname(path), 'front', 'lobby_rules.js'));       // (what the page reads its maps, its default map and codeText from)
    const storage = makeStorage(stored);
    const win = makeWindow(search, storage, !(options && options.decline), options && options.screen);
    if (options && options.noScreen) win.screen = undefined;                          // a window that has no screen to ask
    const radios = radioGroup(['4:3', '16:10', '16:9', '21:9']);
    const body = element();
    // the footer: its tags (hidden until the page shows one) and the bar that gets the room for them
    const tags = {};
    for (const value of SHELL_SHAPES) { tags[value] = element(); tags[value].hidden = true; }
    const bar = element();
    bar.classes = [];
    bar.classList = { add(name) { bar.classes.push(name); }, contains(name) { return bar.classes.indexOf(name) !== -1; } };
    const doc = {
        body,
        getElementById(id) {
            const at = SHELL_SHAPES.map((value) => value.replace(':', '-'));
            if (id.indexOf('aspect-') === 0 && at.indexOf(id.slice(7)) !== -1) return radios[SHELL_SHAPES[at.indexOf(id.slice(7))]];
            if (id.indexOf('fit-') === 0 && at.indexOf(id.slice(4)) !== -1) return tags[SHELL_SHAPES[at.indexOf(id.slice(4))]];
            if (id === 'footer-bar') return bar;
            throw new Error('the page asked for #' + id);
        },
    };
    // handoff: the address of the game page that START (and the Rejoin button) open, built as the page builds it, with the page's `aspect` as it is NOW (onStarting and the Rejoin button pass the variable);
    // solo: the address of a game for one on this computer (startAlone passes the variable too)
    const code = 'var DEFAULT_MAP_KEY = Rules.DEFAULT_MAP_KEY; var mapByKey = Rules.mapByKey; var codeText = Rules.codeText;\n'
        + helpers + shapes + fill + lobby + shape + rejoin + selector
        + '\nreturn { aspect: aspect, key: ASPECT_KEY, now: function () { return aspect; },'
        + ' handoff: function (code, seat, name) { return rejoinQuery({ room: code, seat: seat }, name, aspect); }, solo: function (name) { return localGameQuery(DEFAULT_MAP_KEY, [], name, aspect); } };';
    const result = new Function('window', 'document', 'Rules', code)(win, doc, rules);
    result.storage = storage;
    result.win = win;
    result.radios = radios;
    result.body = body;
    result.bar = bar;
    // (read when asked) the tags that are shown, left to right
    Object.defineProperty(result, 'tags', { get() { return SHELL_SHAPES.filter((value) => !tags[value].hidden).join('+'); } });
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

// the rows about 16:10 and 21:9 (both pages have all four shapes): [what it is, the address's query, what the browser had stored, the shape, where it came from]
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
    // ?embed=1 is no frame of another page any more (the front page has no frames): the game page reads the remembered shape for it as for any address
    for (const [label, search, stored, aspect, source] of [
        ['?embed=1, 4:3 remembered', '?embed=1', { [NEW_KEY]: '4:3' }, '4:3', 'remembered'],
        ['?embed=1 with the address saying 16:10', '?embed=1&aspect=16:10', { [NEW_KEY]: '4:3' }, '16:10', 'address'],
    ]) {
        const r = runShell(shellPath, search, stored);
        expect('shell.html, ' + label + ' (the shape)', r.aspect, aspect);
        expect('shell.html, ' + label + ' (where it came from)', r.source, source);
        expect('shell.html, ' + label + ' (no --audio-focus: nothing is a frame)', r.args.indexOf('--audio-focus'), -1);
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
    for (const [w, h, shape] of [[1920, 1080, '16:9'], [1440, 900, '16:10'], [3440, 1440, '21:9'], [1280, 1024, '4:3'], [5120, 1440, ''], [1000, 1000, ''], [1427, 1000, '4:3'], [1467, 1000, '']]) {
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
        r.postRun();                                                                 // (the page's own hook, as it is written in shell.html: the game is up)
        expect('shell.html, the postRun hook gives the game the shape that the page has now (it began with another)', r.ready() + ' ' + JSON.stringify(calls), 'true [3]');
        expect('shell.html, ... and again: the same number, the game keeps it', r.sync() + ' ' + JSON.stringify(calls), '1 [3,3]');
    }
    // a name that is "--aspect" (a hand-made address) is a value, not the option: the press changes the option's own value, and the name and the option stay as they were. The game's start puts the
    // program's name in front of the arguments (the same array: no place of the option can be kept), and the name gate adds a name at the end
    {
        const r = runShell(shellPath, '?join=/ws&room=abc&seat=1&name=--aspect', {}, { ready: false, module: () => ({ _ants_set_aspect() { return 1; } }) });
        const before = r.args.length;
        r.args.unshift('./this.program');
        r.buttons[3].listeners.click();
        const nameAt = r.args.indexOf('--name');
        expect('shell.html selector, a name that reads "--aspect" and the program name in front: the name and the option are as they were; the option has the new shape',
               JSON.stringify([r.args.length === before + 1, r.args[nameAt + 1], r.args.slice(-2), r.args.filter((a) => a === '--aspect').length]), '[true,"--aspect",["--aspect","21:9"],2]');
        const late = runShell(shellPath, '?join=/ws&room=abc&seat=1', {}, { ready: false, module: () => ({ _ants_set_aspect() { return 1; } }) });
        late.args.unshift('./this.program');
        late.args.push('--name', '--aspect');
        late.buttons[1].listeners.click();
        expect('shell.html selector, a name that reads "--aspect" added after the option: the option has the new shape, the name stays',
               JSON.stringify([late.args.slice(-4), late.args.filter((a) => a === '16:10').length]), '[["--aspect","16:10","--name","--aspect"],1]');
        const plain = runShell(shellPath, '', {}, { ready: false, module: () => ({ _ants_set_aspect() { return 1; } }) });
        plain.args.unshift('./this.program');
        plain.buttons[0].listeners.click();
        expect('shell.html selector, the program name in front of the arguments: the option still gets the new shape', plain.args.join(' ').slice(-24), './this.program --aspect 4:3'.slice(-24));
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
    for (const [label, search, stored, aspect, source] of table.concat(shellOnly)) {
        const r = runLobby(lobbyPath, search, stored);
        expect('lobby.html, ' + label + ' (the shape)', r.aspect, aspect);
        expect('lobby.html, ' + label + ' (the body)', r.body.attributes['data-aspect'], aspect);
        expect('lobby.html, ' + label + ' (the button that is checked)', shown(r), aspect);
        expect('lobby.html, ' + label + ' (nothing was written by merely loading)', JSON.stringify(r.storage.writes), '[]');
        expect('lobby.html, the key', r.key, NEW_KEY);
        expect('lobby.html, ' + label + ' (the START hand-off names it)', r.handoff('k7m2xq', 1, 'Ann'), '?join=/ws&room=k7m2xq&seat=1&name=Ann&aspect=' + aspect);
        expect('lobby.html, ' + label + ' (a game for one names it)', r.solo('Ann').endsWith('&aspect=' + aspect), true);
    }
    // "fills your screen": the tag stands under the shape nearest to the computer's screen, and under none when no shape is within 8 per cent of it (the same rule as the game page's, on the same screens)
    for (const [label, screen, shape] of [
        ['a 1920 x 1080 monitor', { width: 1920, height: 1080 }, '16:9'], ['a 1440 x 900 laptop', { width: 1440, height: 900 }, '16:10'], ['a 3440 x 1440 ultrawide', { width: 3440, height: 1440 }, '21:9'],
        ['a 1280 x 1024 monitor', { width: 1280, height: 1024 }, '4:3'], ['a 2560 x 1080 ultrawide', { width: 2560, height: 1080 }, '21:9'], ['a 1366 x 768 laptop', { width: 1366, height: 768 }, '16:9'],
        ['a 5120 x 1440 super ultrawide (32:9)', { width: 5120, height: 1440 }, ''], ['a square screen', { width: 1000, height: 1000 }, ''], ['a screen of no size', { width: 0, height: 0 }, ''],
        ['a screen that is not a number', { width: NaN, height: 1080 }, ''], ['a phone held upright (390 x 844)', { width: 390, height: 844 }, ''],
        ['a screen 7 per cent off 4:3 (1427 x 1000): still near enough', { width: 1427, height: 1000 }, '4:3'], ['a screen 10 per cent off 4:3 and 9 off 16:10 (1467 x 1000): too far from every shape', { width: 1467, height: 1000 }, ''],
    ]) {
        const r = runLobby(lobbyPath, '', {}, { screen });
        expect('lobby.html, ' + label + ': the tag', r.tags, shape);
        expect('lobby.html, ' + label + ': the footer has room for it only when there is one', r.bar.classes.join(' '), shape ? 'has-fit-tag' : '');
        expect('lobby.html, ' + label + ': the tag does not change what is checked (16:9 until the player picks)', shown(r) + ' ' + r.now(), '16:9 16:9');
        const shell = runShell(shellPath, '', {}, { screen });
        expect('lobby.html, ' + label + ': the same shape as the game page tags', r.tags, shell.tags);
    }
    {
        const r = runLobby(lobbyPath, '', {}, { noScreen: true });                       // a window with no screen to ask: no tag, no error, the page still has its shape
        expect('lobby.html, a window with no screen to ask: no tag, no room for one, 16:9 checked', r.tags + '|' + r.bar.classes.join(' ') + '|' + shown(r), '||16:9');
        expect('lobby.html, the default screen of the test (1920 x 1080) tags 16:9', runLobby(lobbyPath, '', {}).tags, '16:9');
    }
    {
        const r = runLobby(lobbyPath, '', { [NEW_KEY]: '21:9' }, { screen: { width: 1440, height: 900 } });   // the tag follows the screen, the checked shape follows the choice: they are two things
        expect('lobby.html, 21:9 remembered on a 1440 x 900 laptop: 21:9 is checked and the tag is under 16:10', shown(r) + ' ' + r.tags, '21:9 16:10');
    }

    // THE LOBBY (the room view, the front page itself: the page's `room` is '' and no game of it sits in a frame): a pick changes the shape of the page AT ONCE, with no reload and no question, so that the
    // room and the match stay; it is remembered under the new key and nothing else; the hand-off to the game (START, Rejoin, a game for one) carries the shape that is picked
    {
        const r = runLobby(lobbyPath, '', { [OLD_KEY]: '4:3' });
        pick(r, '4:3');                                                              // the player picks 4:3
        expect('lobby.html selector, 4:3 picked: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '4:3']]));
        expect('lobby.html selector, 4:3 picked: the old key is untouched', r.storage.data[OLD_KEY], '4:3');
        expect('lobby.html selector, 4:3 picked: no reload, nothing is asked (the match and the room stay)', r.win.assigned.length + ' ' + r.win.asked.length, '0 0');
        expect('lobby.html selector, 4:3 picked: the page has the shape at once', r.now() + ' ' + r.body.attributes['data-aspect'] + ' ' + shown(r), '4:3 4:3 4:3');
        expect('lobby.html selector, 4:3 picked: the START hand-off carries it', r.handoff('k7m2xq', 1, 'Ann'), '?join=/ws&room=k7m2xq&seat=1&name=Ann&aspect=4:3');
        expect('lobby.html selector, 4:3 picked: ... and so does a game for one', r.solo('Ann').endsWith('&aspect=4:3'), true);
        const again = runLobby(lobbyPath, '', r.storage.data);
        expect('lobby.html selector, the next visit', again.aspect + ' ' + shown(again), '4:3 4:3');
    }
    for (const picked of ['16:10', '21:9']) {                                       // the two shapes that the front page learned: picked like the others
        const r = runLobby(lobbyPath, '', { [OLD_KEY]: '4:3' });
        pick(r, picked);
        expect('lobby.html selector, ' + picked + ' picked: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, picked]]));
        expect('lobby.html selector, ' + picked + ' picked: no reload, nothing is asked (the match and the room stay)', r.win.assigned.length + ' ' + r.win.asked.length, '0 0');
        expect('lobby.html selector, ' + picked + ' picked: the page has the shape at once', r.now() + ' ' + r.body.attributes['data-aspect'] + ' ' + shown(r), picked + ' ' + picked + ' ' + picked);
        expect('lobby.html selector, ' + picked + ' picked: the START hand-off carries it', r.handoff('k7m2xq', 1, 'Ann'), '?join=/ws&room=k7m2xq&seat=1&name=Ann&aspect=' + picked);
        expect('lobby.html selector, ' + picked + ' picked: ... and so does a game for one', r.solo('Ann').endsWith('&aspect=' + picked), true);
        const again = runLobby(lobbyPath, '', r.storage.data);
        expect('lobby.html selector, ' + picked + ': the next visit', again.aspect + ' ' + shown(again) + ' ' + again.handoff('k7m2xq', 1, 'Ann').endsWith('&aspect=' + picked), picked + ' ' + picked + ' true');
        const game = runShell(shellPath, '', r.storage.data);                          // ... and the game page opens the same shape: the one choice is shared
        expect('lobby.html selector, ' + picked + ' picked: the game page reads the same choice', game.aspect + ' ' + game.source, picked + ' remembered');
    }
    {
        const r = runLobby(lobbyPath, '', { [NEW_KEY]: '21:9' });                    // a 21:9 that the game page remembered is shown and handed on, not turned into 16:9
        pick(r, '16:10');
        expect('lobby.html selector, 16:10 picked over 21:9: written, shown at once, handed on', JSON.stringify(r.storage.writes) + ' ' + r.now() + ' ' + shown(r) + ' ' + r.handoff('k7m2xq', 1, 'Ann').endsWith('&aspect=16:10'), JSON.stringify([[NEW_KEY, '16:10']]) + ' 16:10 16:10 true');
    }
    {
        const r = runLobby(lobbyPath, '', { [NEW_KEY]: '4:3' });                     // 4:3 is there: picking 16:9 gives the default picture back
        pick(r, '16:9');
        expect('lobby.html selector, 16:9 picked over 4:3: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '16:9']]));
        expect('lobby.html selector, 16:9 picked over 4:3: no reload, nothing is asked', r.win.assigned.length + ' ' + r.win.asked.length, '0 0');
        expect('lobby.html selector, 16:9 picked over 4:3: the page has the shape at once, and the hand-off carries it', r.now() + ' ' + r.body.attributes['data-aspect'] + ' ' + shown(r) + ' ' + r.handoff('k7m2xq', 1, 'Ann'), '16:9 16:9 16:9 ?join=/ws&room=k7m2xq&seat=1&name=Ann&aspect=16:9');
    }
    {
        const r = runLobby(lobbyPath, '', {});
        r.radios['16:9'].listeners.change();                                         // the same picture again (a change event that names the shape that is already there): remembered, nothing to change
        expect('lobby.html selector, 16:9 again: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '16:9']]));
        expect('lobby.html selector, 16:9 again: no reload, nothing is asked, the shape is the same', r.win.assigned.length + ' ' + r.win.asked.length + ' ' + r.now() + ' ' + r.body.attributes['data-aspect'], '0 0 16:9 16:9');
    }
    {
        const r = runLobby(lobbyPath, '', {});                                       // one pick after another: each is its own change, the last wins, each is remembered, no reload ever
        pick(r, '4:3'); pick(r, '16:9'); pick(r, '4:3');
        expect('lobby.html selector, three picks (4:3, 16:9, 4:3): each is remembered, the last wins, no reload, no question', JSON.stringify(r.storage.writes) + ' ' + r.now() + ' ' + r.win.assigned.length + ' ' + r.win.asked.length,
               JSON.stringify([[NEW_KEY, '4:3'], [NEW_KEY, '16:9'], [NEW_KEY, '4:3']]) + ' 4:3 0 0');
    }
    {
        const r = runLobby(lobbyPath, '?aspect=4:3', {});                            // the address asked for 4:3: a pick of 16:9 is the player's own choice and the hand-off follows it
        pick(r, '16:9');
        expect('lobby.html selector, the address says 4:3 and 16:9 is picked: remembered, no reload, the hand-off carries 16:9', JSON.stringify(r.storage.writes) + ' ' + r.win.assigned.length + ' ' + r.handoff('k7m2xq', 1, 'Ann').endsWith('&aspect=16:9'), JSON.stringify([[NEW_KEY, '16:9']]) + ' 0 true');
    }
    {
        const r = runLobby(lobbyPath, '', THROWS);                                   // a private window: the pick still changes the page, nothing breaks, nothing reloads
        pick(r, '4:3');
        expect('lobby.html selector without storage: the page changes all the same (no reload)', r.now() + ' ' + r.body.attributes['data-aspect'] + ' ' + r.win.assigned.length + ' ' + r.win.asked.length, '4:3 4:3 0 0');
    }
} catch (e) {
    console.log('FAIL ' + e.message);
    failed++;
}

if (failed === 0) console.log('web aspect key: ' + (table.length + shellOnly.length) + ' rows x 2 pages (all four shapes on both), the "fills your screen" tags, and the selectors (the game page\'s and the lobby\'s), 0 failures');
process.exit(failed === 0 ? 0 : 1);
