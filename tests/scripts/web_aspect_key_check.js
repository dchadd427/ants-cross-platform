// Runs the pages' OWN code that decides the shape of the picture and remembers the player's choice, on a table of addresses and of what a browser had remembered
// (web/shell.html: everything between ANTS_PAGE_BEGIN and ANTS_PAGE_END, and the selector's block; web/lobby.html, the front page: its helpers, the table of the shapes, the block of the picture's
// shape, the selector's listener, and the functions that build the addresses of its hand-offs: the Rejoin / START address of a lobby (rejoinQuery) and the address of a game on this computer (localGameQuery)).
// A first visit (nothing under the key `ants.aspect.v2` that names a shape, and no ?aspect= that does) starts in the shape NEAREST the player's screen (AntsShapes.firstVisit; 16:9 for a phone held upright
// and for a screen that cannot be asked), the same on both pages and never stored: only the choice of the selector (the key) or ?aspect= in the address ends it. Both pages have every shape of the
// table (4:3, 3:2, 16:10, 16:9, 20:9, 21:9, 32:9) as a dropdown with one entry for each row (the shape and its canvas): the front page's is a native one that web/shapes.js fills, the game page's two are made
// by web/shape_picker.js (a button and a list that works in fullscreen; its own check is web_shape_picker_check.js, and a fake of it stands here), with the quiet line "16:10 fills your screen" under it for the shape
// nearest to the computer's screen (within 8 per cent). A Classic 4:3 that the first versions of the pages remembered under `ants.aspect` is NOT read any more (every browser
// starts with the first visit's shape once), and the selectors write the new key and nothing else. The game page's selector changes the picture AT ONCE, in the running game (no reload, no question, the room and the seat
// stay): what it stores, tells the game (ants_set_aspect), puts in the address and shows is checked here on a page of fakes; "fills your screen" and the shapes' boxes too.
// The front page's selector (the lobby is the page's only mode) keeps the match and the room, changes the shape of the page at once, remembers it, and the address of the hand-off to the game
// (START, Rejoin, a game for one) carries it (`aspect=`).
// tests/scripts/test_web_aspect_default.py runs this with node (the quick tier); tests/scripts/web_aspect_check.py is the opt-in check in a real browser.
// usage: node web_aspect_key_check.js web/shell.html web/lobby.html     (exit 0: every row holds; every differing row is printed)
'use strict';
const fs = require('fs');
const nodePath = require('path');

// the pages load web/shapes.js, the one table of the shapes, before their own code: it is the global AntsShapes of the code that is run here (a function made by `new Function` reads the globals)
globalThis.AntsShapes = require(nodePath.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

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

function makeWindow(search, storage, answerToConfirm, screen, inner, framed) {
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
        innerWidth: inner ? inner[0] : undefined,                                   // (the window's own size: only the upright / sideways question reads it)
        innerHeight: inner ? inner[1] : undefined,
        parent: framed ? {} : undefined,                                            // (a page in a frame has another window above it)
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
        style: { props: {}, setProperty(name, value) { this.props[name] = value; } },
    };
    return el;
}

// a dropdown (a <select>) as a browser has it for the page's code: the page fills it from the table (fillSelect makes the options with the document's createElement), its `value` is the chosen entry,
// and the person's choice is a new value and a `change` event. Key events are listened to for the page's own reasons (the game's window listener must not see a dropdown's keys).
function fakeSelect() {
    const select = {
        options: [],
        listeners: {},
        value: '',
        appendChild(option) { select.options.push(option); return option; },
        addEventListener(type, fn) { (select.listeners[type] = select.listeners[type] || []).push(fn); },
        fire(type, event) { (select.listeners[type] || []).forEach((fn) => fn(event || {})); },
    };
    return select;
}
// the game page's pickers (web/shape_picker.js, which its own check runs): the page makes them from the table into two hosts, shows each the shape that the picture has and is told of a pick by `onPick`; a picker says nothing
// when the shape that it shows is chosen again (tests/scripts/web_shape_picker_check.js), so `choose` is the person's, and the page's own function is `opts.onPick` (what `press` calls: a pick of any shape, the shown one too)
function fakePickers() {
    const made = [];
    return { made, create(shapes, opts) { const picker = { shapes, opts, shown: opts.value, set(name) { picker.shown = name; }, choose(shape) { if (shape !== picker.shown) opts.onPick(shape); } }; made.push(picker); return picker; } };
}
const fakeCreate = (tag) => { if (tag !== 'option') throw new Error('the page made a <' + tag + '>'); return {}; };
// the person chooses another entry: the dropdown has the new value and says `change` (choosing the entry that is shown says nothing: no event)
function choose(select, shape) { select.value = shape; select.fire('change'); }
function pick(r, shape) { choose(r.select, shape); }
const shown = (r) => r.select.value;
// what a dropdown has after the page filled it: the shapes of the table, in its order, each as an entry with its words and its canvas
const ENTRIES = AntsShapes.rows.map((row) => [row.name, row.label + ' \u00b7 ' + row.width + ' x ' + row.height]);

const SHELL_SHAPES = AntsShapes.names();                                         // the game page's dropdown, from the narrowest shape to the widest

// web/shell.html: returns what the page decides and the dropdowns of its selector (the strip under the game and the replay's Picture window), with the quiet line under each
function runShell(path, search, stored, options) {
    const text = fs.readFileSync(path, 'utf8');
    const page = between(text, 'ANTS_PAGE_BEGIN', 'ANTS_PAGE_END', path);
    const selector = between(text, 'ANTS_SELECTOR_BEGIN', 'ANTS_SELECTOR_END', path);
    const hook = text.match(/postRun: \[(function\(\) \{[\s\S]*?\n {12}\})\]/);                  // (the page's own postRun hook: run as it is written, not a copy of it)
    if (!hook) throw new Error(path + ': the postRun hook of the Module was not found');
    const storage = makeStorage(stored);
    const win = makeWindow(search, storage, !(options && options.decline), options && options.screen, options && options.inner, options && options.framed);
    win.AntsReplay = { fileOf() { return 'ants-TEST-20260101-000000Z.antsrep'; }, liveIdOf() { return 'LIVE-1'; } };         // (replay_page.js: only a spectator's address asks for it)
    const stage = element({ 'data-aspect': '16:9' });
    const hosts = [element(), element()];                                           // (where the strip under the game and the replay's Picture window get their dropdowns)
    const makers = fakePickers();
    const notes = [element(), element()];
    notes.forEach((note) => { note.hidden = true; });                               // (the markup hides every line: the script shows them)
    const viewBar = element();
    viewBar.classes = [];
    viewBar.classList = { add(name) { viewBar.classes.push(name); }, contains(name) { return viewBar.classes.indexOf(name) !== -1; } };
    const body = element();
    const doc = {
        body,
        getElementById(id) { if (id === 'game-stage') return stage; if (id === 'view-bar') return viewBar; if (id === 'aspect-pick') return hosts[0]; if (id === 'pic-pick') return hosts[1]; throw new Error('the page asked for #' + id); },
        createElement: fakeCreate,
        querySelectorAll(selectorText) { if (selectorText === '.fit-note') return notes; throw new Error('the page asked for ' + selectorText); },
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
    const code = 'var isReadyToPlay = ' + ready + '; var Module = fakeModule; var holdsThisSeat = fakeHolds; var stageElement = fakeStage; var relayoutCalls = []; var progressContainer = { style: {} }; function hideLoadingScreen() {} function antsLoadGameMenu() {}\n'
        + 'function relayout(force) { events.push("relayout"); relayoutCalls.push(force); }\n' + page + '\n' + selector
        + '\nvar postRunHook = ' + hook[1] + ';'
        + '\nreturn { aspect: ANTS_ASPECT, source: ANTS_ASPECT_SOURCE, args: ANTS_ARGS, key: ANTS_ASPECT_KEY, page: ANTS_PAGE, relayoutCalls: relayoutCalls, sync: antsSyncAspect, set: antsSetAspect,'
        + ' postRun: postRunHook, ready: function () { return isReadyToPlay; }, setReady: function (on) { isReadyToPlay = on; }, now: function () { return ANTS_ASPECT; }, pick: antsPickShape };';
    const result = new Function('window', 'document', 'fakeModule', 'fakeHolds', 'fakeStage', 'events', 'AntsShapePicker', code)(win, doc, fake, holdsFn, stage, events, makers);
    result.storage = storage;
    result.win = win;
    result.holdsCalls = holdsCalls;
    result.stage = stage;
    result.body = body;
    result.hosts = hosts;
    result.pickers = makers.made;
    result.notes = notes;
    result.viewBar = viewBar;
    result.events = events;
    result.press = (shape) => makers.made[0].opts.onPick(shape);                  // (a pick of the strip's dropdown, as the page is told of it)
    // (read when asked: what the dropdowns show and what the quiet line says change as they are used)
    Object.defineProperty(result, 'checked', { get() { return makers.made.map((picker) => picker.shown).join('+'); } });
    Object.defineProperty(result, 'tags', { get() { const said = notes.map((note) => (note.hidden ? '' : note.textContent)); return said[0] === said[1] ? said[0].replace(/ fills your screen$/, '') : 'THE TWO LINES DIFFER: ' + said.join(' | '); } });
    return result;
}
// what the slot's CSS reads of each shape: its steps (the shape in the smallest whole numbers) and the widest the slot gets (the classic picture is not shown wider than 960 CSS pixels)
const SLOT_VARS = Object.fromEntries(AntsShapes.rows.map((row) => [row.name, { '--ar-w': String(row.stepW), '--ar-h': String(row.stepH), '--stage-max': row.slot + 'px' }]));
// ... which for the shapes of the table is: the classic picture is 960 CSS pixels wide at most, the others the page's 1280, and the widest canvas, 32:9, its own 1920 (a monitor shows it at 1x)
const SLOT_RULE = { '4:3': ['4', '3', '960px'], '3:2': ['3', '2', '1280px'], '16:10': ['8', '5', '1280px'], '16:9': ['16', '9', '1280px'], '20:9': ['20', '9', '1280px'], '21:9': ['7', '3', '1280px'], '32:9': ['32', '9', '1920px'] };
const checkedString = (shape) => shape + '+' + shape;                              // (the two dropdowns of the game page show the same shape)
const tagOf = (shape) => (shape ? AntsShapes.rowOf(shape).label : '');             // (the quiet line says the shape's words: "Classic 4:3 fills your screen")

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
    const win = makeWindow(search, storage, !(options && options.decline), options && options.screen, options && options.inner, options && options.framed);
    if (options && options.noScreen) win.screen = undefined;                          // a window that has no screen to ask
    const select = fakeSelect();
    const body = element();
    // the Picture row: its dropdown, and the quiet line under it (hidden until the page shows it)
    const note = element();
    note.hidden = true;
    const doc = {
        body,
        createElement: fakeCreate,
        getElementById(id) {
            if (id === 'aspect-select') return select;
            if (id === 'fit-note') return note;
            throw new Error('the page asked for #' + id);
        },
    };
    // handoff: the address of the game page that START (and the Rejoin button) open, built as the page builds it, with the page's `aspect` as it is NOW (onStarting and the Rejoin button pass the variable);
    // solo: the address of a game for one on this computer (startAlone passes the variable too)
    const code = 'var DEFAULT_MAP_KEY = Rules.DEFAULT_MAP_KEY; var mapByKey = Rules.mapByKey; var codeText = Rules.codeText;\n'
        + helpers + shapes + fill + lobby + shape + rejoin + selector
        + '\nreturn { aspect: aspect, key: ASPECT_KEY, now: function () { return aspect; },'
        + ' handoff: function (code, seat, name) { return rejoinQuery({ room: code, seat: seat }, name, pickedShape); }, solo: function (name) { return localGameQuery(DEFAULT_MAP_KEY, [], name, pickedShape); }, picked: function () { return pickedShape; } };';
    const result = new Function('window', 'document', 'Rules', code)(win, doc, rules);
    result.storage = storage;
    result.win = win;
    result.select = select;
    result.note = note;
    result.body = body;
    // (read when asked) the words of the quiet line under the dropdown, '' while it is hidden
    Object.defineProperty(result, 'tags', { get() { return note.hidden ? '' : note.textContent.replace(/ fills your screen$/, ''); } });
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
    ['an address that is no shape is ignored', '?aspect=5:4', {}, '16:9', 'default'],
    ['an address that is no shape, a remembered 4:3', '?aspect=wide', { [NEW_KEY]: '4:3' }, '4:3', 'remembered'],
    ['a browser that refuses its storage (a private window)', '', THROWS, '16:9', 'default'],
    ['a browser that refuses its storage, the address asks for 4:3', '?aspect=4:3', THROWS, '4:3', 'address'],
];

// the rows about the other shapes (both pages have every shape of the table): [what it is, the address's query, what the browser had stored, the shape, where it came from]
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
    ['the selector remembered 3:2', '', { [NEW_KEY]: '3:2' }, '3:2', 'remembered'],
    ['the selector remembered 20:9', '', { [NEW_KEY]: '20:9' }, '20:9', 'remembered'],
    ['the selector remembered 32:9', '', { [NEW_KEY]: '32:9' }, '32:9', 'remembered'],
    ['the address asks for 3:2, nothing remembered', '?aspect=3:2', {}, '3:2', 'address'],
    ['the address asks for 20:9 with the colon escaped', '?aspect=20%3A9', {}, '20:9', 'address'],
    ['the address asks for 32:9 over a remembered 16:10', '?aspect=32:9', { [NEW_KEY]: '16:10' }, '32:9', 'address'],
    ['the address asks for 16:9 over a remembered 32:9', '?aspect=16:9', { [NEW_KEY]: '32:9' }, '16:9', 'address'],
    ['a remembered ratio that is no shape: 5:4', '', { [NEW_KEY]: '5:4' }, '16:9', 'default'],
    ['a remembered ratio that is no shape: 19.5:9', '', { [NEW_KEY]: '19.5:9' }, '16:9', 'default'],
    ['a remembered "fit" (the table has no such row yet)', '', { [NEW_KEY]: 'fit' }, '16:9', 'default'],
    ['an address that is no shape: fit', '?aspect=fit', {}, '16:9', 'default'],
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
        expect('shell.html, ' + label + ' (the slot\'s custom properties)', JSON.stringify(r.body.style.props), JSON.stringify(SLOT_VARS[aspect]));
        expect('shell.html, ' + label + ' (... which are the rule: the shape\'s steps, and 960 / 1280 / its canvas\'s width as the widest slot)', Object.values(r.body.style.props).join(' '), SLOT_RULE[aspect].join(' '));
        expect('shell.html, ' + label + ' (the dropdowns show it)', r.checked, checkedString(aspect));
        expect('shell.html, ' + label + ' (two dropdowns are made from the page\'s table, one in each host, named and described by the page\'s own words, each showing the chosen shape)', JSON.stringify(r.pickers.map((picker) => [picker.shapes === AntsShapes, picker.opts.host === (picker.opts.id === 'aspect-select' ? r.hosts[0] : r.hosts[1]), picker.opts.id, picker.opts.labelledBy, picker.opts.describedBy, picker.opts.value, typeof picker.opts.onPick])), JSON.stringify([[true, true, 'aspect-select', 'view-bar-label', 'fit-note', aspect, 'function'], [true, true, 'pic-select', 'picpop-h', 'pic-note', aspect, 'function']]));
        expect('shell.html, ' + label + ' (nothing was written by merely loading)', JSON.stringify(r.storage.writes), '[]');
        expect('shell.html, the key', r.key, NEW_KEY);
    }
    // a spectator (a replay or a live match) is a visitor like any other: ?aspect=, then what the selector remembered, then 16:9 (it used to be 16:9 whatever the address and the browser said)
    for (const [label, search, stored, aspect, source] of [
        ['a replay, nothing remembered', '?replay=a', {}, '16:9', 'default'],
        ['a replay, 4:3 remembered', '?replay=a', { [NEW_KEY]: '4:3' }, '4:3', 'remembered'],
        ['a replay, 21:9 remembered', '?replay=a', { [NEW_KEY]: '21:9' }, '21:9', 'remembered'],
        ['a replay, the address asks for 16:10 over a remembered 4:3', '?replay=a&aspect=16:10', { [NEW_KEY]: '4:3' }, '16:10', 'address'],
        ['a replay, the address asks for 4:3', '?aspect=4:3&replay=a', {}, '4:3', 'address'],
        ['a replay, storage refused, the address asks for 21:9', '?replay=a&aspect=21:9', THROWS, '21:9', 'address'],
        ['a live match, nothing remembered', '?live=a', {}, '16:9', 'default'],
        ['a live match, 16:10 remembered', '?live=a', { [NEW_KEY]: '16:10' }, '16:10', 'remembered'],
        ['a live match, the address asks for 21:9 over a remembered 4:3', '?live=a&aspect=21:9', { [NEW_KEY]: '4:3' }, '21:9', 'address'],
    ]) {
        const r = runShell(shellPath, search, stored);
        expect('shell.html, ' + label + ' (the shape)', r.aspect, aspect);
        expect('shell.html, ' + label + ' (where it came from)', r.source, source);
        expect('shell.html, ' + label + ' (--aspect for the game)', r.args.slice(-2).join(' '), '--aspect ' + aspect);
        expect('shell.html, ' + label + ' (the stage)', r.stage.attributes['data-aspect'], aspect);
        expect('shell.html, ' + label + ' (the dropdowns show it)', r.checked, checkedString(aspect));
    }
    {   // the spectator's pick is the player's: remembered under the same key and put in the address
        const r = runShell(shellPath, '?replay=a', {});
        r.press('4:3');
        expect('shell.html, a replay: a pick of 4:3 is remembered, shown and given to the stage', JSON.stringify(r.storage.writes) + ' ' + r.now() + ' ' + r.stage.attributes['data-aspect'], JSON.stringify([[NEW_KEY, '4:3']]) + ' 4:3 4:3');
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
        expect('shell.html, the shapes\' numbers (the selector\'s order, the game\'s kAllAspects)', SHELL_SHAPES.map((s) => page.shapeNumber(s)).join(','), '0,1,2,3,4,5,6');
        expect('shell.html, a text that is no shape has no number', [page.shapeNumber('5:4'), page.shapeNumber('fit'), page.shapeNumber(''), page.shapeNumber(null), page.shapeNumber(undefined), page.shapeNumber(16)].join(','), '-1,-1,-1,-1,-1,-1');
        // [the screen, its shape]: common screens (CSS pixels), then screens that no shape fills
        for (const [w, h, shape] of [[1920, 1080, '16:9'], [1366, 768, '16:9'], [2560, 1440, '16:9'], [3840, 2160, '16:9'], [1440, 900, '16:10'], [1680, 1050, '16:10'], [1920, 1200, '16:10'], [2560, 1600, '16:10'],
                                      [3440, 1440, '21:9'], [2560, 1080, '21:9'], [3840, 1600, '21:9'], [1280, 1024, '4:3'], [1024, 768, '4:3'], [1600, 1200, '4:3'], [2256, 1504, '3:2'], [2736, 1824, '3:2'], [1500, 1000, '3:2'],
                                      [3840, 1080, '32:9'], [5120, 1440, '32:9'], [915, 412, '20:9'], [2400, 1080, '20:9'], [7680, 1080, null], [1000, 1000, null], [768, 1024, null], [0, 0, null], [NaN, 600, null], [1920, -1, null], [Infinity, 1080, null]]) {
            expect('shell.html, the shape nearest to a ' + w + ' x ' + h + ' screen', page.shapeForScreen(w, h), shape);
        }
        // the box: the biggest whole number of steps of the shape's smallest whole numbers that fits, at least one; the canvas's scale is the same on both axes
        for (const [availW, availH, dpr, shape, bw, bh] of [[1280, 720, 1, '16:9', 1280, 720], [1280, 720, 1, '16:10', 1152, 720], [1280, 720, 1, '21:9', 1274, 546], [960, 720, 1, '4:3', 960, 720],
                                                           [1280, 800, 2, '16:10', 2560, 1600], [1200, 700, 1.5, '21:9', 1799, 771], [10, 10, 1, '21:9', 7, 3], [10, 10, 1, '16:10', 8, 5], [0, 0, 1, '16:10', 8, 5],
                                                           [1280, 720, 1, '3:2', 1080, 720], [1368, 912, 2, '3:2', 2736, 1824], [10, 10, 1, '3:2', 9, 6], [1280, 720, 1, '20:9', 1280, 576], [1200, 540, 2, '20:9', 2400, 1080], [10, 10, 1, '20:9', 20, 9],
                                                           [1920, 540, 1, '32:9', 1920, 540], [1920, 540, 2, '32:9', 3840, 1080], [1280, 720, 1, '32:9', 1280, 360], [1700, 900, 1, '32:9', 1696, 477], [10, 10, 1, '32:9', 32, 9]]) {
            const fit = page.fitBox(availW, availH, dpr, shape);
            const canvas = { '4:3': [640, 480], '3:2': [960, 640], '16:10': [960, 600], '16:9': [960, 540], '20:9': [1200, 540], '21:9': [1260, 540], '32:9': [1920, 540] }[shape];
            expect('shell.html, the box of ' + shape + ' in ' + availW + ' x ' + availH + ' at ' + dpr, fit.bw + ' x ' + fit.bh, bw + ' x ' + bh);
            expect('shell.html, the box of ' + shape + ' in ' + availW + ' x ' + availH + ' at ' + dpr + ': the canvas fills it exactly (the same scale on both axes)', fit.bw * canvas[1] === fit.bh * canvas[0], true);
        }
        expect('shell.html, a box for a text that is no shape is the 16:9 one', JSON.stringify(page.fitBox(1280, 720, 1, 'wide')), JSON.stringify(page.fitBox(1280, 720, 1, '16:9')));
    }

    // "fills your screen": the quiet line under each dropdown names the shape nearest to the computer's screen when it is within 8 per cent of it, and there is no line when none is (the same line under both
    // dropdowns); the bar has room for it
    // (the shape that is shown on a first visit follows the screen: nearest, with no cutoff, so a screen with no line still has a picture)
    for (const [w, h, shape, first] of [[1920, 1080, '16:9', '16:9'], [1440, 900, '16:10', '16:10'], [3440, 1440, '21:9', '21:9'], [1280, 1024, '4:3', '4:3'], [5120, 1440, '32:9', '32:9'], [2736, 1824, '3:2', '3:2'], [844, 390, '20:9', '20:9'],
                                         [1000, 1000, '', '4:3'], [7680, 1080, '', '32:9'], [1427, 1000, '3:2', '3:2'], [1919, 1000, '16:9', '16:9'], [1921, 1000, '', '16:9'], [2036, 1000, '', '20:9'], [2800, 1000, '', '21:9']]) {
        const r = runShell(shellPath, '', {}, { screen: { width: w, height: h } });
        expect('shell.html, the line on a ' + w + ' x ' + h + ' screen', r.tags, tagOf(shape));
        expect('shell.html, the line on a ' + w + ' x ' + h + ' screen: the words', r.notes[0].hidden ? 'hidden' : r.notes[0].textContent, shape ? tagOf(shape) + ' fills your screen' : 'hidden');
        expect('shell.html, the line on a ' + w + ' x ' + h + ' screen: the bar makes room for it', r.viewBar.classes.join(' '), shape ? 'has-fit-note' : '');
        expect('shell.html, the line on a ' + w + ' x ' + h + ' screen: the shape that is shown is the screen\'s nearest, with or without a line', r.checked, checkedString(first));
    }
    {
        const r = runShell(shellPath, '?aspect=21:9', {}, { screen: { width: 1440, height: 900 } });
        expect('shell.html, the line stays under the screen\'s shape (16:10) while another is shown', r.tags, '16:10');
        r.press('4:3');
        expect('shell.html, ... and when another shape is pressed', r.tags, '16:10');
    }
    {
        const r = runShell(shellPath, '', {}, { screen: { get width() { throw new Error('no screen'); }, height: 900 } });   // (a browser that refuses to say: no tag, the selector works)
        expect('shell.html, a screen that cannot be asked: no line, no breakage', r.tags + '|' + r.viewBar.classes.join(' ') + '|' + r.notes.map((n) => n.hidden).join(), '||true,true');
        r.press('16:10');
        expect('shell.html, ... and the selector still works', r.aspect + ' ' + r.now(), '16:9 16:10');
    }

    // THE SELECTOR CHANGES THE PICTURE AT ONCE: no reload, no question, nothing to leave; it remembers, tells the game, sizes the box and puts the shape in the address
    for (const shape of ['4:3', '3:2', '16:10', '20:9', '21:9', '32:9']) {
        const r = runShell(shellPath, '', {});
        r.press(shape);
        const what = 'shell.html selector, ' + shape + ' pressed';
        expect(what + ': what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, shape]]));
        expect(what + ': the old key is untouched', Object.prototype.hasOwnProperty.call(r.storage.data, OLD_KEY), false);
        expect(what + ': the page does not reload, nothing is asked', r.win.assigned.length + ' ' + r.win.asked.length, '0 0');
        expect(what + ': the picture is the new shape', r.now(), shape);
        expect(what + ': the stage names the shape (data-aspect: what the tests and the menu read; the CSS takes the custom properties below)', r.stage.attributes['data-aspect'], shape);
        expect(what + ': the slot\'s ratio and widest width, as custom properties of the table\'s rows (what the CSS reads)', JSON.stringify(r.body.style.props), JSON.stringify(SLOT_VARS[shape]));
        expect(what + ': ... which are the rule', Object.values(r.body.style.props).join(' '), SLOT_RULE[shape].join(' '));
        expect(what + ': the selector shows it', r.checked, checkedString(shape));
        expect(what + ': a game that has not started starts with it (--aspect)', r.args.slice(-2).join(' '), '--aspect ' + shape);
        expect(what + ': the box is laid out again, and the game\'s window system told (a forced layout)', JSON.stringify(r.relayoutCalls), '[true]');
        expect(what + ': the address has it, without a new entry in the history', r.win.history.replaced.length === 1 && new RegExp('^/\\?aspect=' + shape + '$').test(r.win.history.replaced[0]), true);
        const again = runShell(shellPath, '', r.storage.data);                      // the next visit: what the selector remembered
        expect('shell.html selector, the next visit', again.aspect + ' ' + again.source, shape + ' remembered');
    }
    {
        const r = runShell(shellPath, '', {});
        r.press('16:9');                                              // 16:9 when it is 16:9 (a change event that names the shape that is shown): remembered, nothing to change
        expect('shell.html selector, 16:9 chosen on 16:9: what was written', JSON.stringify(r.storage.writes), JSON.stringify([[NEW_KEY, '16:9']]));
        expect('shell.html selector, 16:9 chosen on 16:9: nothing moves', r.win.assigned.length + ' ' + r.relayoutCalls.length + ' ' + r.win.history.replaced.length + ' ' + r.events.length, '0 0 0 0');
    }
    {
        // one shape after another: each choice is its own change (16:9 to 32:9 to 3:2 to 20:9 to 21:9 to 4:3 to 16:10 and back), the last wins; every change tells the game (by the shape's place in the table) and lays the box out, the game first
        const calls = [];
        const sequence = ['32:9', '3:2', '20:9', '21:9', '4:3', '16:10', '16:9'];
        const r = runShell(shellPath, '?join=/ws&room=abc&seat=1&name=Ann', {}, { ready: true, module: (win, events) => ({ _ants_set_aspect(n) { events.push('game:' + n); calls.push(n); return 1; }, _ants_leave_match() { win.left = true; } }) });
        for (const shape of sequence) r.press(shape);
        expect('shell.html selector, seven choices in a joined match: the game is told each shape\'s number, then the box is laid out, every time', r.events.join(' '), 'game:6 relayout game:1 relayout game:4 relayout game:5 relayout game:0 relayout game:2 relayout game:3 relayout');
        expect('shell.html selector, seven choices in a joined match: the picture is the last', r.now() + ' ' + r.checked, '16:9 ' + checkedString('16:9'));
        expect('shell.html selector, seven choices in a joined match: no reload, no question, the game is not told to leave', r.win.assigned.length + ' ' + r.win.asked.length + ' ' + (r.win.left === true), '0 0 false');
        expect('shell.html selector, seven choices in a joined match: the room, the seat and the name stay in the address, the shape follows', r.win.history.replaced.map((u) => /[?&]join=(%2F|\/)ws/.test(u) && /[?&]room=abc(&|$)/.test(u) && /[?&]seat=1(&|$)/.test(u) && /[?&]name=Ann(&|$)/.test(u)).join(',') + ' ' + r.win.history.replaced.map((u) => (u.match(/aspect=([^&]*)/) || [])[1]).join(' '), 'true,true,true,true,true,true,true ' + sequence.join(' '));
        expect('shell.html selector, seven choices in a joined match: the choice of each press is remembered', JSON.stringify(r.storage.writes), JSON.stringify(sequence.map((v) => [NEW_KEY, v])));
    }
    // the same in a joined match of a browser that holds its seat's key and in one that does not (a waiting room): no question, no Leave (the seat is kept: the connection is never touched)
    for (const holds of [true, false]) {
        const r = runShell(shellPath, '?join=/ws&room=abc', {}, { ready: true, holds, module: (win) => ({ _ants_match_running() { return 1; }, _ants_set_aspect() { return 1; }, _ants_leave_match() { win.left = true; } }) });
        r.press('21:9');
        expect('shell.html selector, a room' + (holds ? ' with the key held' : ' still waiting') + ': no question, no Leave, no reload', r.win.asked.length + ' ' + (r.win.left === true) + ' ' + r.win.assigned.length, '0 false 0');
        expect('shell.html selector, a room' + (holds ? ' with the key held' : ' still waiting') + ': the picture changed, and the key lookup was not needed', r.now() + ' ' + r.holdsCalls.length, '21:9 0');
    }
    {
        const r = runShell(shellPath, '', {}, { ready: true, module: () => ({ _ants_match_running() { return 1; }, _ants_set_aspect() { return 1; } }) });
        r.press('16:10');
        expect('shell.html selector, a match of this computer: no question, no reload, the picture changed', r.win.asked.length + ' ' + r.win.assigned.length + ' ' + r.now(), '0 0 16:10');
    }
    // a game that is not up yet: nothing to tell; it starts with the shape (--aspect), and the postRun hook asks again for a game that began with the shape before
    {
        const calls = [];
        const r = runShell(shellPath, '', {}, { ready: false, module: () => ({ _ants_set_aspect(n) { calls.push(n); return 1; } }) });
        r.press('21:9');
        expect('shell.html selector, the game not ready yet: it is not told (an export called while the program compiles is undefined for good)', JSON.stringify(calls), '[]');
        expect('shell.html selector, the game not ready yet: the page changed all the same', r.now() + ' ' + r.args.slice(-2).join(' ') + ' ' + JSON.stringify(r.relayoutCalls), '21:9 --aspect 21:9 [true]');
        r.postRun();                                                                 // (the page's own hook, as it is written in shell.html: the game is up)
        expect('shell.html, the postRun hook gives the game the shape that the page has now (it began with another)', r.ready() + ' ' + JSON.stringify(calls), 'true [5]');
        expect('shell.html, ... and again: the same number, the game keeps it', r.sync() + ' ' + JSON.stringify(calls), '1 [5,5]');
    }
    // a name that is "--aspect" (a hand-made address) is a value, not the option: the press changes the option's own value, and the name and the option stay as they were. The game's start puts the
    // program's name in front of the arguments (the same array: no place of the option can be kept), and the name gate adds a name at the end
    {
        const r = runShell(shellPath, '?join=/ws&room=abc&seat=1&name=--aspect', {}, { ready: false, module: () => ({ _ants_set_aspect() { return 1; } }) });
        const before = r.args.length;
        r.args.unshift('./this.program');
        r.press('21:9');
        const nameAt = r.args.indexOf('--name');
        expect('shell.html selector, a name that reads "--aspect" and the program name in front: the name and the option are as they were; the option has the new shape',
               JSON.stringify([r.args.length === before + 1, r.args[nameAt + 1], r.args.slice(-2), r.args.filter((a) => a === '--aspect').length]), '[true,"--aspect",["--aspect","21:9"],2]');
        const late = runShell(shellPath, '?join=/ws&room=abc&seat=1', {}, { ready: false, module: () => ({ _ants_set_aspect() { return 1; } }) });
        late.args.unshift('./this.program');
        late.args.push('--name', '--aspect');
        late.press('16:10');
        expect('shell.html selector, a name that reads "--aspect" added after the option: the option has the new shape, the name stays',
               JSON.stringify([late.args.slice(-4), late.args.filter((a) => a === '16:10').length]), '[["--aspect","16:10","--name","--aspect"],1]');
        const plain = runShell(shellPath, '', {}, { ready: false, module: () => ({ _ants_set_aspect() { return 1; } }) });
        plain.args.unshift('./this.program');
        plain.press('4:3');
        expect('shell.html selector, the program name in front of the arguments: the option still gets the new shape', plain.args.join(' ').slice(-24), './this.program --aspect 4:3'.slice(-24));
    }
    for (const [what, module] of [['a game without the export', () => ({})], ['a game whose export throws', () => ({ _ants_set_aspect() { throw new Error('the game is gone'); } })], ['no Module at all', () => undefined]]) {
        const r = runShell(shellPath, '?join=/ws&room=abc&seat=1', {}, { ready: true, module });
        r.press('16:10');
        expect('shell.html selector with ' + what + ': the page changes all the same (box, stage, address), nothing reloads', r.now() + ' ' + JSON.stringify(r.relayoutCalls) + ' ' + r.win.assigned.length + ' ' + r.stage.attributes['data-aspect'], '16:10 [true] 0 16:10');
        expect('shell.html selector with ' + what + ': ... and the sync says that the game did not answer', r.sync(), -1);
    }
    {
        const r = runShell(shellPath, '', {}, { ready: true, module: () => ({ _ants_set_aspect() { return 0; } }) });
        r.press('16:10');
        expect('shell.html, a game that does not know the number: the sync returns what the game said', r.sync(), 0);
    }
    {
        const r = runShell(shellPath, '', THROWS);                                  // a private window: the click still changes the picture, nothing breaks
        r.press('16:10');
        expect('shell.html selector without storage: the picture changes (no reload)', r.now() + ' ' + r.win.assigned.length + ' ' + JSON.stringify(r.relayoutCalls), '16:10 0 [true]');
    }
    {
        const r = runShell(shellPath, '', {});                                       // an address that cannot be rewritten: the picture changes, the browser remembers
        r.win.history.replaceState = () => { throw new Error('a sandboxed frame'); };
        r.press('21:9');
        expect('shell.html selector, an address that cannot be rewritten: the picture changes all the same', r.now() + ' ' + JSON.stringify(r.storage.writes), '21:9 ' + JSON.stringify([[NEW_KEY, '21:9']]));
    }
    {
        const r = runShell(shellPath, '', {});
        r.set('5:4');                                                                // a text that is no shape never changes anything
        r.set(null);
        r.set('16:9');
        expect('shell.html, antsSetAspect with a text that is no shape (or the shape it has) changes nothing', r.now() + ' ' + r.relayoutCalls.length + ' ' + r.win.history.replaced.length, '16:9 0 0');
    }

    // ---- the front page ----
    for (const [label, search, stored, aspect, source] of table.concat(shellOnly)) {
        const r = runLobby(lobbyPath, search, stored);
        expect('lobby.html, ' + label + ' (the shape)', r.aspect, aspect);
        expect('lobby.html, ' + label + ' (the body)', r.body.attributes['data-aspect'], aspect);
        expect('lobby.html, ' + label + ' (the entry that is chosen)', shown(r), aspect);
        expect('lobby.html, ' + label + ' (the dropdown has one entry for each row of the table, in its order, each with its canvas)', JSON.stringify(r.select.options.map((o) => [o.value, o.textContent])), JSON.stringify(ENTRIES));
        expect('lobby.html, ' + label + ' (nothing was written by merely loading)', JSON.stringify(r.storage.writes), '[]');
        expect('lobby.html, the key', r.key, NEW_KEY);
        // a pick (the address, a stored shape) goes into the hand-off; a first visit's shape does NOT: the game page works out its own, when it loads (a phone that opens the front page upright and is turned
        // sideways must not hand its upright 16:9 on)
        const named = source === 'default' ? '' : '&aspect=' + aspect;
        expect('lobby.html, ' + label + ' (the START hand-off carries a pick, and only a pick)', r.handoff('k7m2xq', 1, 'Ann'), '?join=/ws&room=k7m2xq&seat=1&name=Ann' + named);
        expect('lobby.html, ' + label + ' (a game for one carries a pick, and only a pick)', r.solo('Ann').endsWith('&name=Ann' + named), true);
    }
    // "fills your screen": the quiet line under the dropdown names the shape nearest to the computer's screen, and there is none when no shape is within 8 per cent of it (the same rule as the game page's, on the same screens)
    for (const [label, screen, shape, first] of [
        ['a 1920 x 1080 monitor', { width: 1920, height: 1080 }, '16:9', '16:9'], ['a 1440 x 900 laptop', { width: 1440, height: 900 }, '16:10', '16:10'], ['a 3440 x 1440 ultrawide', { width: 3440, height: 1440 }, '21:9', '21:9'],
        ['a 1280 x 1024 monitor', { width: 1280, height: 1024 }, '4:3', '4:3'], ['a 2560 x 1080 ultrawide', { width: 2560, height: 1080 }, '21:9', '21:9'], ['a 1366 x 768 laptop', { width: 1366, height: 768 }, '16:9', '16:9'],
        ['a 5120 x 1440 super ultrawide (32:9)', { width: 5120, height: 1440 }, '32:9', '32:9'], ['a 2736 x 1824 tablet (3:2)', { width: 2736, height: 1824 }, '3:2', '3:2'], ['a phone held sideways, 844 x 390', { width: 844, height: 390 }, '20:9', '20:9'],
        ['a screen wider than 32:9 (7680 x 1080): nearest, no line', { width: 7680, height: 1080 }, '', '32:9'],
        ['a square screen', { width: 1000, height: 1000 }, '', '4:3'], ['a screen of no size', { width: 0, height: 0 }, '', '16:9'],
        ['a screen that is not a number', { width: NaN, height: 1080 }, '', '16:9'], ['a phone held upright (390 x 844)', { width: 390, height: 844 }, '', '16:9'],
        ['a screen 5 per cent off 3:2 (1427 x 1000): near enough', { width: 1427, height: 1000 }, '3:2', '3:2'], ['a screen 9 per cent off 16:9 (1940 x 1000): too far from every shape', { width: 1940, height: 1000 }, '', '16:9'],
    ]) {
        const r = runLobby(lobbyPath, '', {}, { screen });
        expect('lobby.html, ' + label + ': the line', r.tags, tagOf(shape));
        expect('lobby.html, ' + label + ': the words', r.note.hidden ? 'hidden' : r.note.textContent, shape ? tagOf(shape) + ' fills your screen' : 'hidden');
        expect('lobby.html, ' + label + ': what is chosen is the screen\'s nearest shape, with or without a line (until the player picks)', shown(r) + ' ' + r.now(), first + ' ' + first);
        const shell = runShell(shellPath, '', {}, { screen });
        expect('lobby.html, ' + label + ': the same line as the game page\'s', r.tags, shell.tags);
    }
    {
        const r = runLobby(lobbyPath, '', {}, { noScreen: true });                       // a window with no screen to ask: no tag, no error, the page still has its shape
        expect('lobby.html, a window with no screen to ask: no line, 16:9 chosen', r.tags + '|' + r.note.hidden + '|' + shown(r), '|true|16:9');
        expect('lobby.html, the default screen of the test (1920 x 1080) names 16:9', runLobby(lobbyPath, '', {}).tags, '16:9');
    }
    {
        const r = runLobby(lobbyPath, '', { [NEW_KEY]: '21:9' }, { screen: { width: 1440, height: 900 } });   // the tag follows the screen, the checked shape follows the choice: they are two things
        expect('lobby.html, 21:9 remembered on a 1440 x 900 laptop: 21:9 is chosen and the line names 16:10', shown(r) + ' ' + r.tags, '21:9 16:10');
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
    for (const picked of ['16:10', '3:2', '20:9', '21:9', '32:9']) {                  // the other shapes of the table: picked like the first
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
        r.select.fire('change');                                                     // the same picture again (a change event that names the shape that is already there): remembered, nothing to change
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

    // ---- A FIRST VISIT: nothing stored that names a shape, no ?aspect= that does: the shape NEAREST the player's screen, the same on both pages, and never stored ----
    // [what it is, the device's screen (CSS pixels), the window's inner size (undefined: not asked), the shape]. Nearest by the ratio of ratios, no cutoff; ties and the middles between shapes are checked
    // on both sides (the middle of 4:3 and 3:2 is 1.4142, of 3:2 and 16:10 1.5492, of 16:10 and 16:9 1.6866, of 16:9 and 20:9 1.9876, of 20:9 and 21:9 2.2771, of 21:9 and 32:9 2.8802).
    for (const [label, [w, h], inner, shape, framed] of [
        ['a 1920 x 1080 monitor', [1920, 1080], undefined, '16:9'], ['a 1366 x 768 laptop', [1366, 768], undefined, '16:9'], ['a 3840 x 2160 monitor', [3840, 2160], undefined, '16:9'],
        ['a 1440 x 900 laptop', [1440, 900], undefined, '16:10'], ['a 2560 x 1600 laptop', [2560, 1600], undefined, '16:10'], ['a 1920 x 1200 monitor', [1920, 1200], undefined, '16:10'],
        ['a 2256 x 1504 laptop (3:2)', [2256, 1504], undefined, '3:2'], ['a 2736 x 1824 tablet (3:2)', [2736, 1824], undefined, '3:2'], ['a 1500 x 1000 window-sized screen (3:2)', [1500, 1000], undefined, '3:2'],
        ['a 3440 x 1440 ultrawide', [3440, 1440], undefined, '21:9'], ['a 2560 x 1080 ultrawide', [2560, 1080], undefined, '21:9'],
        ['a 3840 x 1080 super ultrawide (32:9)', [3840, 1080], undefined, '32:9'], ['a 5120 x 1440 super ultrawide (32:9)', [5120, 1440], undefined, '32:9'], ['a 7680 x 1080 screen: no cutoff, the widest shape', [7680, 1080], undefined, '32:9'],
        ['a 1024 x 768 screen', [1024, 768], undefined, '4:3'], ['a 1600 x 1200 screen', [1600, 1200], undefined, '4:3'],
        ['a 1280 x 1024 (5:4) screen picks Classic 4:3', [1280, 1024], undefined, '4:3'], ['a square screen', [1000, 1000], undefined, '4:3'], ['a screen far narrower than 4:3 but sideways (1001 x 1000)', [1001, 1000], undefined, '4:3'],
        ['1.414 just on the 4:3 side of the middle of 4:3 and 3:2', [1414, 1000], undefined, '4:3'], ['1.415 just on the 3:2 side', [1415, 1000], undefined, '3:2'],
        ['1.549 just on the 3:2 side of the middle of 3:2 and 16:10', [1549, 1000], undefined, '3:2'], ['1.550 just on the 16:10 side', [1550, 1000], undefined, '16:10'],
        ['1.686 just on the 16:10 side of the middle of 16:10 and 16:9', [1686, 1000], undefined, '16:10'], ['1.688 just on the 16:9 side', [1688, 1000], undefined, '16:9'],
        ['1.987 just on the 16:9 side of the middle of 16:9 and 20:9', [1987, 1000], undefined, '16:9'], ['1.988 just on the 20:9 side', [1988, 1000], undefined, '20:9'],
        ['2.277 just on the 20:9 side of the middle of 20:9 and 21:9', [2277, 1000], undefined, '20:9'], ['2.278 just on the 21:9 side', [2278, 1000], undefined, '21:9'],
        ['2.880 just on the 21:9 side of the middle of 21:9 and 32:9', [2880, 1000], undefined, '21:9'], ['2.881 just on the 32:9 side', [2881, 1000], undefined, '32:9'],
        // a phone held sideways picks by its landscape ratio
        ['a phone held sideways, 844 x 390 (2.164: 20:9 is nearer than 21:9)', [844, 390], undefined, '20:9'], ['a phone held sideways, 915 x 412', [915, 412], undefined, '20:9'], ['a phone held sideways, 800 x 360', [800, 360], undefined, '20:9'],
        ['a phone held sideways, 932 x 430 (2.167)', [932, 430], undefined, '20:9'], ['a phone held sideways, 2400 x 1080 (the pixels of a 20:9 phone)', [2400, 1080], undefined, '20:9'],
        ['a 16:9 phone held sideways, 640 x 360', [640, 360], undefined, '16:9'], ['a tablet held sideways, 1024 x 768', [1024, 768], undefined, '4:3'], ['a tablet held sideways, 1280 x 800', [1280, 800], undefined, '16:10'],
        // a device held upright is told to turn it and starts in 16:9, whatever its ratio (the picture is as wide as the screen)
        ['a phone held upright, 390 x 844', [390, 844], [390, 664], '16:9'], ['a phone held upright, 360 x 800', [360, 800], [360, 650], '16:9'],
        ['a tablet held upright, 768 x 1024 (not Classic 4:3)', [768, 1024], [768, 900], '16:9'], ['a portrait monitor, 1080 x 1920, in a portrait window', [1080, 1920], [1080, 1700], '16:9'],
        ['a phone that reports an upright screen but has a sideways window (844 x 340): sideways, 20:9', [390, 844], [844, 340], '20:9'],
        ['a tablet that reports an upright screen but has a sideways window: sideways, 4:3', [768, 1024], [1024, 700], '4:3'],
        // in a frame (the replay in a match page) the frame's own window says nothing: an upright device stays 16:9 even though its frame is wide
        ['an upright tablet with a wide frame of a match page (the frame is no sign that it is held sideways)', [768, 1024], [1024, 700], '16:9', true], ['an upright phone with a wide frame', [390, 844], [390, 245], '16:9', true],
        ['a landscape screen in a frame: the screen decides', [1440, 900], [800, 500], '16:10', true],
        // the screen, not the window: a narrow or tall window on a landscape screen changes nothing
        ['a 1440 x 900 screen with a tall narrow window', [1440, 900], [500, 900], '16:10'], ['a 3440 x 1440 screen with a small window', [3440, 1440], [800, 500], '21:9'], ['a 3840 x 1080 screen with a small window', [3840, 1080], [800, 500], '32:9'],
        // numbers that are none: no screen to ask
        ['a screen of no size', [0, 0], undefined, '16:9'], ['a negative height', [1920, -1], undefined, '16:9'], ['a width that is not a number', [NaN, 1080], undefined, '16:9'], ['an infinite width', [Infinity, 1080], undefined, '16:9'],
    ]) {
        const screen = { width: w, height: h };
        const shell = runShell(shellPath, '', {}, { screen, inner, framed });
        const lobby = runLobby(lobbyPath, '', {}, { screen, inner, framed });
        expect('first visit, shell.html, ' + label + ' (the shape)', shell.aspect + ' ' + shell.source, shape + ' default');
        expect('first visit, shell.html, ' + label + ' (--aspect for the game, the stage, the dropdowns)', shell.args.slice(-2).join(' ') + ' ' + shell.stage.attributes['data-aspect'] + ' ' + shell.checked, '--aspect ' + shape + ' ' + shape + ' ' + checkedString(shape));
        expect('first visit, shell.html, ' + label + ' (loading stores nothing)', JSON.stringify(shell.storage.writes) + JSON.stringify(shell.storage.data), '[]{}');
        expect('first visit, lobby.html, ' + label + ' (the shape, the body, the chosen entry)', lobby.aspect + ' ' + lobby.body.attributes['data-aspect'] + ' ' + shown(lobby), shape + ' ' + shape + ' ' + shape);
        expect('first visit, lobby.html, ' + label + ' (loading stores nothing)', JSON.stringify(lobby.storage.writes) + JSON.stringify(lobby.storage.data), '[]{}');
        expect('first visit, lobby.html, ' + label + ' (START and a game for one hand NO shape on: the game page decides when it loads)', lobby.handoff('k7m2xq', 1, 'Ann') + ' ' + /aspect/.test(lobby.solo('Ann')), '?join=/ws&room=k7m2xq&seat=1&name=Ann false');
        expect('first visit, ' + label + ': the two pages show the same shape and tag the same one', shell.aspect + ' ' + shell.tags, lobby.aspect + ' ' + lobby.tags);
    }
    {   // the line names the shape within 8 per cent of the screen, and that is the shape that a first visit starts in whenever there is one
        for (const [w, h] of [[1920, 1080], [1440, 900], [3440, 1440], [1280, 1024], [1024, 768], [1366, 768], [2560, 1080], [1680, 1050], [2736, 1824], [3840, 1080], [915, 412]]) {
            const r = runShell(shellPath, '', {}, { screen: { width: w, height: h } });
            expect('first visit, a ' + w + ' x ' + h + ' screen: the line names the shape that is shown', r.tags, tagOf(r.aspect));
        }
    }
    {   // devicePixelRatio and the browser's zoom scale both numbers of the screen: only the ratio counts
        for (const [w, h, shape] of [[720, 450, '16:10'], [1440, 900, '16:10'], [2880, 1800, '16:10'], [4320, 2700, '16:10'], [960, 540, '16:9'], [2560, 1440, '16:9'], [1720, 720, '21:9'], [3440, 1440, '21:9'], [1368, 912, '3:2'], [2736, 1824, '3:2'], [1920, 540, '32:9'], [3840, 1080, '32:9']]) {
            expect('first visit, a ' + w + ' x ' + h + ' screen at any device pixel ratio', runShell(shellPath, '', {}, { screen: { width: w, height: h } }).aspect + ' ' + runLobby(lobbyPath, '', {}, { screen: { width: w, height: h } }).aspect, shape + ' ' + shape);
        }
    }
    {   // a browser that cannot say: 16:9, no breakage (the screen is missing, or asking for it throws)
        for (const [label, options] of [['no screen at all', { noScreen: true }], ['a screen that throws', { screen: { get width() { throw new Error('no screen'); }, height: 900 } }], ['a window with no inner size on an upright screen', { screen: { width: 390, height: 844 } }]]) {
            const lobby = runLobby(lobbyPath, '', {}, options);
            expect('first visit, lobby.html, ' + label + ': 16:9', lobby.aspect + ' ' + shown(lobby), '16:9 16:9');
            if (!options.noScreen) expect('first visit, shell.html, ' + label + ': 16:9', runShell(shellPath, '', {}, options).aspect, '16:9');
        }
        const noScreenShell = (() => { try { return runShell(shellPath, '', {}, { noScreen: true }).aspect; } catch (e) { return 'threw: ' + e.message; } })();
        expect('first visit, shell.html, no screen at all: 16:9', noScreenShell, '16:9');
    }
    // WHO KEEPS WHAT: only a stored text that names a shape is a pick; the address beats it; nothing but a press of a selector stores
    {
        const laptop = { screen: { width: 1440, height: 900 } };                        // nearest: 16:10
        for (const [label, search, stored, aspect, source] of [
            ['nothing stored', '', {}, '16:10', 'default'],
            ['16:9 stored (somebody who picked 16:9 keeps it: a stored 16:9 is not "nothing")', '', { [NEW_KEY]: '16:9' }, '16:9', 'remembered'],
            ['21:9 stored', '', { [NEW_KEY]: '21:9' }, '21:9', 'remembered'],
            ['32:9 stored', '', { [NEW_KEY]: '32:9' }, '32:9', 'remembered'],
            ['3:2 stored', '', { [NEW_KEY]: '3:2' }, '3:2', 'remembered'],
            ['4:3 stored', '', { [NEW_KEY]: '4:3' }, '4:3', 'remembered'],
            ['16:10 stored (the shape that is nearest anyway is still a pick)', '', { [NEW_KEY]: '16:10' }, '16:10', 'remembered'],
            ['the old key 4:3 (not read)', '', { [OLD_KEY]: '4:3' }, '16:10', 'default'],
            ['the old key 16:9 (not read)', '', { [OLD_KEY]: '16:9' }, '16:10', 'default'],
            ['junk stored (no shape: no pick)', '', { [NEW_KEY]: 'junk' }, '16:10', 'default'],
            ['a shape that the table does not have stored (5:4)', '', { [NEW_KEY]: '5:4' }, '16:10', 'default'],
            ['an empty text stored', '', { [NEW_KEY]: '' }, '16:10', 'default'],
            ['the address asks for 16:9, nothing stored', '?aspect=16:9', {}, '16:9', 'address'],
            ['the address asks for 4:3 over a stored 21:9', '?aspect=4:3', { [NEW_KEY]: '21:9' }, '4:3', 'address'],
            ['an address that is no shape: the screen decides', '?aspect=wide', {}, '16:10', 'default'],
            ['an address that is no shape over a stored 21:9: the pick', '?aspect=wide', { [NEW_KEY]: '21:9' }, '21:9', 'remembered'],
            ['a browser that refuses its storage', '', THROWS, '16:10', 'default'],
            ['a replay, nothing stored', '?replay=a', {}, '16:10', 'default'],
            ['a replay in the frame of a match page, nothing stored', '?replay=a&embed=1', {}, '16:10', 'default'],
            ['a replay in a frame, 16:9 stored', '?replay=a&embed=1', { [NEW_KEY]: '16:9' }, '16:9', 'remembered'],
            ['a live match, nothing stored', '?live=a', {}, '16:10', 'default'],
            ['a live match, the address asks for 21:9', '?live=a&aspect=21:9', {}, '21:9', 'address'],
            ['a game of a room, nothing stored', '?join=/ws&room=abc&seat=1&name=Ann', {}, '16:10', 'default'],
        ]) {
            const r = runShell(shellPath, search, stored, laptop);
            expect('who keeps what, shell.html, ' + label, r.aspect + ' ' + r.source + ' ' + JSON.stringify(r.storage.writes), aspect + ' ' + source + ' []');
        }
        for (const [label, search, stored, aspect] of [
            ['nothing stored', '', {}, '16:10'], ['16:9 stored', '', { [NEW_KEY]: '16:9' }, '16:9'], ['21:9 stored', '', { [NEW_KEY]: '21:9' }, '21:9'], ['the old key 4:3', '', { [OLD_KEY]: '4:3' }, '16:10'],
            ['junk stored', '', { [NEW_KEY]: 'junk' }, '16:10'], ['5:4 stored (no row)', '', { [NEW_KEY]: '5:4' }, '16:10'], ['3:2 stored', '', { [NEW_KEY]: '3:2' }, '3:2'], ['32:9 stored', '', { [NEW_KEY]: '32:9' }, '32:9'], ['the address asks for 16:9', '?aspect=16:9', {}, '16:9'],
            ['the address asks for 4:3 over a stored 21:9', '?aspect=4:3', { [NEW_KEY]: '21:9' }, '4:3'], ['an address that is no shape', '?aspect=wide', {}, '16:10'], ['no storage', '', THROWS, '16:10'],
        ]) {
            const r = runLobby(lobbyPath, search, stored, laptop);
            expect('who keeps what, lobby.html, ' + label, r.aspect + ' ' + shown(r) + ' ' + JSON.stringify(r.storage.writes), aspect + ' ' + aspect + ' []');
        }
    }
    {   // the first visit's shape is not a stored choice: it follows the screen at every visit, and stays out of the storage whatever the page does
        for (const [name, run] of [['shell.html', (screen, stored) => runShell(shellPath, '', stored, { screen })], ['lobby.html', (screen, stored) => runLobby(lobbyPath, '', stored, { screen })]]) {
            const first = run({ width: 1440, height: 900 }, {});
            expect('a default is not stored, ' + name + ': the first visit shows 16:10 and writes nothing', first.aspect + ' ' + JSON.stringify(first.storage.writes) + JSON.stringify(first.storage.data), '16:10 []{}');
            const again = run({ width: 1920, height: 1080 }, first.storage.data);          // the next visit, on another monitor, with what the first visit left behind (nothing)
            expect('a default is not stored, ' + name + ': the next visit follows the new screen', again.aspect, '16:9');
        }
    }
    {   // a change event that names the shape that is already the default IS a pick: it is stored, and it ends the default (the dropdown itself says nothing when the shown entry is chosen again: below)
        const shell = runShell(shellPath, '', {}, { screen: { width: 1440, height: 900 } });
        shell.press('16:10');                                              // 16:10, the shape that is shown
        expect('a pick of the shape that is the default, shell.html: stored', JSON.stringify(shell.storage.writes) + ' ' + shell.now(), JSON.stringify([[NEW_KEY, '16:10']]) + ' 16:10');
        const next = runShell(shellPath, '', shell.storage.data, { screen: { width: 1920, height: 1080 } });
        expect('a pick of the shape that is the default, shell.html: the next visit on another screen keeps it', next.aspect + ' ' + next.source, '16:10 remembered');
        const lobby = runLobby(lobbyPath, '', {}, { screen: { width: 1440, height: 900 } });
        pick(lobby, '16:10');
        expect('a pick of the shape that is the default, lobby.html: stored', JSON.stringify(lobby.storage.writes), JSON.stringify([[NEW_KEY, '16:10']]));
        const lobbyNext = runLobby(lobbyPath, '', lobby.storage.data, { screen: { width: 1920, height: 1080 } });
        expect('a pick of the shape that is the default, lobby.html: the next visit on another screen keeps it', lobbyNext.aspect, '16:10');
        const game = runShell(shellPath, '', lobby.storage.data, { screen: { width: 3440, height: 1440 } });
        expect('a pick of the shape that is the default, lobby.html: the game page reads it', game.aspect + ' ' + game.source, '16:10 remembered');
    }
    {   // the hand-off to the game: nothing before a pick, the shape after any press (the shown shape too), and a phone that opens the front page upright is not frozen into 16:9
        const phone = runLobby(lobbyPath, '', {}, { screen: { width: 390, height: 844 }, inner: [390, 664] });
        expect('hand-off, a phone upright on the front page: the page shows 16:9 and START hands no shape on', phone.aspect + ' ' + phone.handoff('k7m2xq', 1, 'Ann') + ' ' + phone.picked(), '16:9 ?join=/ws&room=k7m2xq&seat=1&name=Ann ');
        const laptop = runLobby(lobbyPath, '', {}, { screen: { width: 1440, height: 900 } });
        expect('hand-off, a laptop that has not picked: no shape', laptop.picked() + '|' + laptop.handoff('k7m2xq', 1, 'Ann').includes('aspect') + '|' + laptop.solo('Ann').includes('aspect'), '|false|false');
        expect('hand-off, the shown entry chosen again says nothing (a dropdown sends no event; opening it is no pick either): the page listens to change and to nothing else', Object.keys(laptop.select.listeners).join(), 'change');
        const shellPick = runShell(shellPath, '', {}, { screen: { width: 1440, height: 900 } });
        shellPick.pickers[0].choose('16:10');
        expect('hand-off, the game page too: the picker says nothing when the shape that it shows is chosen again (its own check), so a pick is a shape that is not the shown one', JSON.stringify(shellPick.storage.writes), '[]');
        shellPick.pickers[1].choose('21:9');
        expect('hand-off, ... and a choice in either dropdown is the page\'s pick: stored, and both dropdowns show it', JSON.stringify(shellPick.storage.writes) + ' ' + shellPick.checked, JSON.stringify([[NEW_KEY, '21:9']]) + ' 21:9+21:9');
        laptop.select.fire('change');                                                   // (the value is the shape that is shown)
        expect('hand-off, a change that names the shown shape: it is a pick and goes on', laptop.picked() + ' ' + laptop.handoff('k7m2xq', 1, 'Ann').endsWith('&aspect=16:10') + ' ' + laptop.solo('Ann').endsWith('&aspect=16:10'), '16:10 true true');
        expect('hand-off, ... and it is stored', JSON.stringify(laptop.storage.writes), JSON.stringify([[NEW_KEY, '16:10']]));
        const stored = runLobby(lobbyPath, '', { [NEW_KEY]: '16:9' }, { screen: { width: 1440, height: 900 } });
        expect('hand-off, a stored 16:9 on a 16:10 laptop: goes on', stored.handoff('k7m2xq', 1, 'Ann').endsWith('&aspect=16:9'), true);
        const asked = runLobby(lobbyPath, '?aspect=21:9', {}, { screen: { width: 1440, height: 900 } });
        expect('hand-off, the front page\'s own address asks for 21:9: goes on, and stores nothing', asked.handoff('k7m2xq', 1, 'Ann').endsWith('&aspect=21:9') + ' ' + JSON.stringify(asked.storage.writes), 'true []');
    }
    {   // a pick of 16:9 on a screen whose nearest is another shape: kept (the old default is a choice like any other)
        const shell = runShell(shellPath, '', {}, { screen: { width: 3440, height: 1440 } });
        expect('16:9 on a 21:9 screen, shell.html: the first visit shows 21:9', shell.aspect, '21:9');
        shell.press('16:9');
        const next = runShell(shellPath, '', shell.storage.data, { screen: { width: 3440, height: 1440 } });
        expect('16:9 picked on a 21:9 screen, shell.html: the next visit shows 16:9', next.aspect + ' ' + next.source, '16:9 remembered');
        const lobby = runLobby(lobbyPath, '', {}, { screen: { width: 3440, height: 1440 } });
        pick(lobby, '16:9');
        expect('16:9 picked on a 21:9 screen, lobby.html: the next visit shows 16:9', runLobby(lobbyPath, '', lobby.storage.data, { screen: { width: 3440, height: 1440 } }).aspect, '16:9');
    }
    {   // the address changes nothing in the storage, whatever the screen
        const r = runShell(shellPath, '?aspect=4:3', {}, { screen: { width: 3440, height: 1440 } });
        const l = runLobby(lobbyPath, '?aspect=4:3', {}, { screen: { width: 3440, height: 1440 } });
        expect('the address wins over the screen and is not stored', r.aspect + ' ' + l.aspect + ' ' + JSON.stringify(r.storage.writes.concat(l.storage.writes)), '4:3 4:3 []');
    }

    // THE NEAREST SHAPE IS WORKED OUT OVER THE TABLE'S ROWS: a row that the table gets later is considered with no other change. shapes.js is loaded with extra rows put into its table (the way the
    // pages' table will grow), and asked the same questions.
    {
        const source = fs.readFileSync(nodePath.resolve(__dirname, '..', '..', 'web', 'shapes.js'), 'utf8');
        const loadWith = (rows, replaceAll) => {
            const end = source.indexOf('    // SHAPE_TABLE_END');
            const begin = source.indexOf('    var TABLE = [');
            if (end < 0 || begin < 0) throw new Error('shapes.js: the table markers are missing');
            const tableEnd = source.lastIndexOf('];', end);
            const text = replaceAll ? source.slice(0, begin) + '    var TABLE = [' + rows + source.slice(tableEnd) : source.slice(0, tableEnd) + ',' + rows + source.slice(tableEnd);
            const holder = { exports: {} };
            new Function('module', text)(holder);
            return holder.exports;
        };
        const win = (w, h, iw, ih) => ({ screen: { width: w, height: h }, innerWidth: iw, innerHeight: ih });
        const more = loadWith("{ name: '5:4', label: '5:4', width: 960, height: 768, slot: 1280 }, { name: '48:9', label: '48:9', width: 2880, height: 540, slot: 2880 }", false);
        expect('the table with two more rows still has its own seven first', more.names().join(' '), '4:3 3:2 16:10 16:9 20:9 21:9 32:9 5:4 48:9');
        for (const [label, w, h, shape] of [['1280 x 1024 is 5:4 now', 1280, 1024, '5:4'], ['7680 x 1440 (5.33) is 48:9 now', 7680, 1440, '48:9'], ['1920 x 1080 is still 16:9', 1920, 1080, '16:9'], ['1440 x 900 is still 16:10', 1440, 900, '16:10'],
                                           ['3440 x 1440 is still 21:9', 3440, 1440, '21:9'], ['3840 x 1080 is still 32:9', 3840, 1080, '32:9'], ['1024 x 768 is still Classic 4:3', 1024, 768, '4:3'], ['2736 x 1824 is still 3:2', 2736, 1824, '3:2']]) {
            expect('a table with extra rows, first visit: ' + label, more.firstVisit(win(w, h)), shape);
        }
        expect('a table with extra rows: the line finds the new rows too (a 1280 x 1024 screen is 5:4, 7680 x 1440 is 48:9)', more.fillsLine(1280, 1024) + ' ' + more.fillsLine(7680, 1440), '5:4 fills your screen 48:9 fills your screen');
        expect('a table with extra rows: a phone held upright is still 16:9, a screen that cannot be asked too', more.firstVisit(win(390, 844, 390, 664)) + ' ' + more.firstVisit({}) + ' ' + more.firstVisit(null), '16:9 16:9 16:9');
        expect('the real table: 7680 x 1440 is 32:9 (no 48:9 row)', AntsShapes.firstVisit(win(7680, 1440)), '32:9');
        // a tie goes to the first of the table (the narrower): 4:1 is as far from 2:1 as from 8:1
        const tie = loadWith("\n        { name: '2:1', label: '2:1', width: 200, height: 100, slot: 1280 },\n        { name: '8:1', label: '8:1', width: 800, height: 100, slot: 1280 }\n    ", true);
        expect('a tie between two rows goes to the narrower, the first of the table', tie.closest(4).row.name, '2:1');
        expect('closest says how far off the nearest row is (ratio of ratios, minus one)', tie.closest(4).off, 1);
    }
} catch (e) {
    console.log('FAIL ' + e.message);
    failed++;
}

if (failed === 0) console.log('web aspect key: ' + (table.length + shellOnly.length) + ' rows x 2 pages (every shape of the table on both), the "fills your screen" lines, the first visit\'s shape on a table of screens, and the selectors (the game page\'s and the lobby\'s), 0 failures');
process.exit(failed === 0 ? 0 : 1);
