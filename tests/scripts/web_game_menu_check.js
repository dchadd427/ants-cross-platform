// Runs the page's OWN code for the settings menu (web/game_menu.js, AntsGameMenu.forPage, which web/shell.html calls once the game runs), without a browser, on a strict fake of the DOM that is written here
// (a node that throws when the script reads or writes what it does not know, and when it writes markup: every text must go in with textContent) and a model of the game's exports
// (Module._ants_menu_describe / get / set / do / open: the rows are tests/data/game_menu_match.json and game_menu_watch.json, which tests/test_app/test_game_menu.cpp keeps equal to what the game says).
//   * the rows: every row of the table is drawn once, in its section, with its label, its hint and its NEW tag, and a row that is not in the table is not drawn; a recording has no Game section; the Controls list
//     is the view-only list of a computer or of a touch screen; the page's own rows (Fullscreen, Picture shape, Fullscreen mouse) use the page's functions and buttons;
//   * the values: a slider shows the game's value and changes it (Master volume while it is dragged, the others at the release), a switch and a choice change it, a button does its act, and a change is read back;
//   * the way in and out: the buttons (header, under the picture, the replay's bar; none over the picture in fullscreen) show only when the game runs and the style is there and are off when a dialog needs its answer;
//     Esc's request opens it; it opens inside the stage, the page behind it is inert, the focus goes to Resume, Tab stays inside, Esc closes it (once: a held key does nothing), a click on the dimmed area does not,
//     the focus goes back to the canvas, the game is told (and the keys never reach the page's other listeners); the pointer lock is let go and asked for again; a dialog that needs its answer closes it;
//   * Leave game: asks in the words of what it will do (a drop out, a forfeit, the end of a game), Esc and Stay stay, only a recording asks nothing, "Back to the matches" in a recording;
//   * the style: fetched with the script, once more after a failure, then said in the toast; the buttons wait for it.
// tests/scripts/test_web_game_menu.py runs this with node (the quick tier). usage: node web_game_menu_check.js web/game_menu.js tests/data     (exit 0: every check holds; every failure is printed)
'use strict';

const fs = require('fs');
const path = require('path');

let failed = 0;
let checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
        failed++;
    }
}
function ok(label, cond) { expect(label, !!cond, true); }

const modulePath = process.argv[2], dataDir = process.argv[3];
if (!modulePath || !dataDir) { console.log('usage: web_game_menu_check.js game_menu.js data-folder'); process.exit(2); }
const Menu = require(path.resolve(modulePath));
const AntsShapes = require(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'));       // (the page's one table of shapes: the page hands it to the menu as `shapes`)
const AntsShapePicker = require(path.resolve(__dirname, '..', '..', 'web', 'shape_picker.js'));       // (the page's picker of the shape: the page hands the menu a function that makes one)
const MATCH = fs.readFileSync(path.join(dataDir, 'game_menu_match.json'), 'utf8').trim();
const WATCH = fs.readFileSync(path.join(dataDir, 'game_menu_watch.json'), 'utf8').trim();

// ---- the strict fake of the DOM --------------------------------------------------------------------------------------------------------------------------------------------------
const NEVER = new Set(['innerHTML', 'outerHTML', 'insertAdjacentHTML', 'outerText', 'innerText', 'insertAdjacentElement', 'write']);
const SETTABLE = new Set(['id', 'className', 'type', 'value', 'min', 'max', 'step', 'hidden', 'disabled', 'title', 'rel', 'href', 'onload', 'onerror', 'checked', 'textContent', 'parentNode', 'own', 'name']);
const READABLE_EMPTY = new Set(['then', 'toJSON', 'nodeType', 'asymmetricMatch', 'constructor']);

function makeDocument() {
    const doc = { listeners: {}, active: null, timers: [], links: [], idMap: null };
    function parseSimple(part) {
        const m = /^([a-z0-9]*)((?:\[[^\]]+\]|:not\(\[[^\]]+\]\)|[.#][\w-]+)*)$/.exec(part);
        if (!m) throw new Error('the fake DOM does not know the selector ' + part);
        return { tag: m[1], quals: m[2].match(/:not\(\[[^\]]+\]\)|\[[^\]]+\]|[.#][\w-]+/g) || [] };
    }
    function parseSelector(text) { return text.split(',').map((part) => part.trim().split(/\s+/).map(parseSimple)); }   // (a list; a part may name a descendant: "a b")
    function matchesSimple(node, sel) {
        if (sel.tag && node.tagName !== sel.tag) return false;
        return sel.quals.every((q) => {
            if (q[0] === '#') return node.id === q.slice(1);
            if (q[0] === '.') return node.className.split(/\s+/).includes(q.slice(1));
            const negate = q.startsWith(':not('), inner = negate ? q.slice(5, -1) : q;
            const am = /^\[([\w-]+)(?:=("?)([^"\]]*)\2)?\]$/.exec(inner);
            const has = am[1] === 'disabled' ? !!node.disabled : node.hasAttribute(am[1]);
            const good = has && (am[3] === undefined || node.getAttribute(am[1]) === am[3]);
            return negate ? !good : good;
        });
    }
    function matchesOne(node, chain) {
        if (!matchesSimple(node, chain[chain.length - 1])) return false;
        let up = node.parentNode;
        for (let i = chain.length - 2; i >= 0; i--) {
            while (up && !matchesSimple(up, chain[i])) up = up.parentNode;
            if (!up) return false;
            up = up.parentNode;
        }
        return true;
    }
    class Node {
        constructor(tag) {
            this.tagName = tag;
            this.className = '';
            this.children = [];
            this.parentNode = null;
            this.attrs = {};
            this.listeners = {};
            this.own = '';
            this.style = {};
            this.id = '';
            this.type = '';
            this.value = '';
            this.min = '';
            this.max = '';
            this.step = '';
            this.title = '';
            this.hidden = false;
            this.disabled = false;
            this.scrollTop = 0;
            this.offsetTop = 0;
            this.offsetHeight = 0;
            this.offsetWidth = 0;
            this.clientHeight = 0;
        }
        get nextSibling() { const kids = this.parentNode ? this.parentNode.children : []; return kids[kids.indexOf(this) + 1] || null; }
        getBoundingClientRect() { return { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 }; }
        appendChild(c) { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = this; this.children.push(c); return c; }
        insertBefore(c, ref) {
            if (c.parentNode) c.parentNode.removeChild(c);
            c.parentNode = this;
            const at = ref ? this.children.indexOf(ref) : -1;
            if (ref && at < 0) throw new Error('the fake DOM: insertBefore with a reference that is not a child');
            if (at < 0) this.children.push(c); else this.children.splice(at, 0, c);
            return c;
        }
        removeChild(c) { const at = this.children.indexOf(c); if (at < 0) throw new Error('the fake DOM: removeChild of a node that is not a child'); this.children.splice(at, 1); c.parentNode = null; return c; }
        remove() { if (this.parentNode) this.parentNode.removeChild(this); }
        setAttribute(k, v) { this.attrs[k] = String(v); if (k === 'id') this.id = String(v); }
        getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
        removeAttribute(k) { delete this.attrs[k]; }
        hasAttribute(k) { return k in this.attrs; }
        addEventListener(type, fn, capture) { (this.listeners[type] = this.listeners[type] || []).push({ fn, capture: !!capture }); }
        removeEventListener(type, fn, capture) { this.listeners[type] = (this.listeners[type] || []).filter((l) => !(l.fn === fn && l.capture === !!capture)); }
        get classList() {
            const n = this, list = () => n.className.split(/\s+/).filter(Boolean);
            return {
                add: (c) => { if (!list().includes(c)) n.className = list().concat(c).join(' '); },
                remove: (c) => { n.className = list().filter((x) => x !== c).join(' '); },
                contains: (c) => list().includes(c),
                toggle: (c, on) => { const l = list().filter((x) => x !== c); if (on) l.push(c); n.className = l.join(' '); },
            };
        }
        get textContent() { return this.own + this.children.map((c) => c.textContent).join(''); }
        set textContent(v) { this.own = String(v); this.children.forEach((c) => { c.parentNode = null; }); this.children = []; }
        get firstChild() { return this.children[0] || null; }
        all() { return this.children.reduce((a, c) => a.concat([c], c.all()), []); }
        querySelectorAll(text) { const sels = parseSelector(text); return this.all().filter((n) => sels.some((s) => matchesOne(n, s))); }
        querySelector(text) { return this.querySelectorAll(text)[0] || null; }
        contains(o) { return o === this || this.all().includes(o); }
        inert() { for (let n = this; n; n = n.parentNode) if (n.hasAttribute('inert')) return true; return false; }
        get offsetParent() {
            if (!doc.body.contains(this) || this.inert() === 'x') return null;
            for (let n = this; n && n !== doc.body; n = n.parentNode) if (n.style.display === 'none' || n.hidden) return null;
            return doc.body;
        }
        focus() { if (this.disabled || this.inert() || !doc.body.contains(this)) return; doc.active = this; doc.focusLog.push(this.id || this.className || this.tagName); }
        blur() { if (doc.active === this) doc.active = doc.body; }
        click() { if (this.disabled) return; this.fire('click', {}); }          // (a click() of the script reaches an inert node: only a person's pointer and keys do not)
        fire(type, e) {
            e = Object.assign({ type, target: this, stopped: false, prevented: false, preventDefault() { this.prevented = true; }, stopPropagation() { this.stopped = true; } }, e);
            for (let n = this; n && !e.stopped; n = n.parentNode) for (const l of (n.listeners[type] || []).slice()) l.fn(e);
            return e;
        }
    }
    const handler = {
        get(t, prop, recv) {
            if (typeof prop === 'symbol' || prop in t) return Reflect.get(t, prop, recv);
            if (READABLE_EMPTY.has(prop)) return undefined;
            throw new Error('the fake DOM: the page code read the unknown property ' + prop);
        },
        set(t, prop, v, recv) {
            if (NEVER.has(prop)) throw new Error('the fake DOM: the page code wrote ' + prop + ': every text goes in with textContent');
            if (!(prop in t) && !SETTABLE.has(prop)) throw new Error('the fake DOM: the page code wrote the unknown property ' + prop);
            return Reflect.set(t, prop, v, recv);
        },
    };
    doc.focusLog = [];
    doc.createElement = (tag) => {
        const n = new Proxy(new Node(tag), handler);
        if (tag === 'link') doc.links.push(n);
        return n;
    };
    doc.createTextNode = (text) => { const n = doc.createElement('#text'); n.own = String(text); return n; };
    doc.body = doc.createElement('body');
    doc.head = doc.createElement('head');
    doc.root = doc.createElement('html');
    doc.root.appendChild(doc.head);
    doc.root.appendChild(doc.body);
    doc.active = doc.body;
    Object.defineProperty(doc, 'activeElement', { get: () => doc.active });
    doc.getElementById = (id) => doc.root.all().find((n) => n.id === id) || null;
    doc.querySelector = (s) => doc.root.querySelector(s);
    doc.querySelectorAll = (s) => doc.root.querySelectorAll(s);
    doc.addEventListener = (type, fn, capture) => { (doc.listeners[type] = doc.listeners[type] || []).push({ fn, capture: !!capture }); };
    doc.removeEventListener = (type, fn, capture) => { doc.listeners[type] = (doc.listeners[type] || []).filter((l) => !(l.fn === fn && l.capture === !!capture)); };
    doc.count = (type) => (doc.listeners[type] || []).length;
    // a key goes to the document's capture listeners (the panel listens there), and to the active element's own only when no capture listener stopped it. An event carries a `code` as a browser's does: the key
    // that is pressed (the place on the keyboard: it stays the same when `key` changes between a press and its lift, as with a letter and Shift)
    const codeOf = (key) => (/^[a-z]$/i.test(key) ? 'Key' + key.toUpperCase() : /^[0-9]$/.test(key) ? 'Digit' + key : key === ' ' ? 'Space' : /^(Shift|Control|Alt|Meta)$/.test(key) ? key + 'Left' : key);
    doc.codeOf = codeOf;
    doc.key = (key, extra) => {
        const e = Object.assign({ type: 'keydown', key, code: codeOf(key), shiftKey: false, repeat: false, target: doc.active, prevented: false, stopped: false, immediate: false, preventDefault() { this.prevented = true; }, stopPropagation() { this.stopped = true; }, stopImmediatePropagation() { this.stopped = true; this.immediate = true; } }, extra || {});
        for (const l of (doc.win && doc.win.listeners[e.type] || []).filter((x) => x.capture).slice()) { if (e.immediate) break; l.fn(e); }       // (the window's capture phase comes first: the page's own keys, the picker's)
        if (!e.stopped) for (const l of (doc.listeners[e.type] || []).filter((x) => x.capture).slice()) l.fn(e);
        e.reachedPage = !e.stopped;
        return e;
    };
    doc.emit = (type) => { for (const l of (doc.listeners[type] || []).slice()) l.fn({ type }); };
    return doc;
}

// ---- the game: the model of Module._ants_menu_* --------------------------------------------------------------------------------------------------------------------------------
function makeGame(json) {
    const g = {
        where: 0, needsAnswer: 0, leave: 2, opened: false, calls: [],
        values: { 1: 100, 2: 65, 3: 99, 4: 0, 8: 0, 9: 50, 10: 1, 11: 1, 15: 0, 16: 1 },
        describes: 0, saves: [],
    };
    g.module = {
        UTF8ToString: (p) => p,
        _ants_menu_describe: () => { g.describes++; return json; },
        _ants_menu_get: (what) => (what === 100 ? g.where : what === 101 ? g.needsAnswer : what === 102 ? g.leave : (what in g.values ? g.values[what] : -1)),
        _ants_menu_set: (id, v, save) => { g.calls.push(['set', id, v]); g.saves.push([id, v, save]); if (!(id in g.values)) return 0; g.values[id] = v; return 1; },
        _ants_menu_do: (what) => { g.calls.push(['do', what]); return 1; },
        _ants_menu_open: (on) => { g.calls.push(['open', on]); g.opened = !!on; },
    };
    return g;
}

// ---- the page ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function makePage(opts) {
    opts = opts || {};
    const doc = makeDocument(), win = { matchMedia: (q) => ({ matches: !!opts.touch && q === '(hover: none)' }), listeners: {} };
    // the window takes listeners too (the page's own pointer-lock handler is there, in the capture phase, and stops the event before the document's)
    win.addEventListener = (type, fn, capture) => { (win.listeners[type] = win.listeners[type] || []).push({ fn, capture: !!capture }); };
    win.removeEventListener = (type, fn, capture) => { win.listeners[type] = (win.listeners[type] || []).filter((l) => !(l.fn === fn && l.capture === !!capture)); };
    win.count = (type) => (win.listeners[type] || []).length;
    win.emit = (type) => { for (const l of (win.listeners[type] || []).filter((x) => x.capture).slice()) l.fn({ type, stopPropagation() {} }); };
    doc.win = win;
    const game = makeGame(opts.watch ? WATCH : MATCH);
    if (opts.watch) { game.where = 1; game.leave = 4; }
    const make = (tag, id, parent, cls) => { const n = doc.createElement(tag); if (id) n.setAttribute('id', id); if (cls) n.className = cls; (parent || doc.body).appendChild(n); return n; };
    const header = make('header'), menuBtn = make('a', 'menu-btn', header, 'btn sm');
    const stage = make('div', 'game-stage'), box = make('div', 'game-container', stage), canvas = make('canvas', 'canvas', box), chat = make('input', 'chat-keys', box);
    stage.setAttribute('data-aspect', '16:9');
    const shapes = {};                                                                   // (what the page's own dropdown does with a pick: antsPickShape; the page's `pickShape` is that function)
    const auto = { on: !!opts.auto, now: '20:9' };                                                    // (the page's source of the shape: Automatic's, or a pick's)
    const pickShape = opts.noPick ? undefined : (shape) => { shapes.clicked = (shapes.clicked || []).concat(shape); auto.on = shape === AntsShapes.AUTO; if (!opts.keepShape) stage.setAttribute('data-aspect', auto.on ? '20:9' : shape); };
    win.screen = opts.screen || { width: 1440, height: 900 };
    win.innerWidth = 1440;
    win.innerHeight = 900;
    const lockState = { on: true };                                                       // (the page's object `lock`: the pointer lock's setting, what the Fullscreen mouse row reads and writes)
    const lock = opts.noLock ? undefined : { available: () => true, get: () => lockState.on, set: (on) => { lockState.on = !!on; game.calls.push(['lock', on ? 'on' : 'off']); } };
    const touchState = { on: true, sets: [] };                                           // (the page's object `touchKeys`: the hotkey buttons' setting, what the Touch buttons row reads and writes)
    const touchKeys = opts.touchKeys ? { available: () => opts.touchKeys !== 'no', get: () => touchState.on, set: (on) => { touchState.on = !!on; touchState.sets.push(!!on); } } : undefined;
    const rbar = make('div', 'rbar'), picwrap = make('div', 'picwrap', rbar);
    const toast = make('div', 'toast');
    toast.hidden = true;
    const asked = [], intervals = [], state = { fullscreen: false, locked: false, ready: true, locks: [], enters: 0, leaves: 0 };
    const watchers = [];
    const page = {
        onShape: (fn) => { watchers.push(fn); return () => { const at = watchers.indexOf(fn); if (at !== -1) watchers.splice(at, 1); }; },
        doc, win, shapes: AntsShapes, picker: (o) => AntsShapePicker.create(AntsShapes, o), pickShape, isAutomatic: opts.auto ? () => auto.on : undefined, automatic: opts.auto ? () => auto.now : undefined, lock, touchKeys, stage, canvas, toast, module: () => (opts.noModule ? null : game.module), ready: () => state.ready, replay: !!opts.watch, live: !!opts.live, version: 'v0.12.111', query: '?v=7',
        isFullscreen: () => state.fullscreen, enter: () => { state.enters++; state.fullscreen = true; }, leave: () => { state.leaves++; state.fullscreen = false; },
        isLocked: () => state.locked, releaseLock: () => { state.locks.push('release'); state.locked = false; }, requestLock: () => { state.locks.push('request'); state.locked = true; },
        every: (fn, ms) => { intervals.push({ fn, ms }); return intervals.length; },
        cancelsKey: opts.noCancel ? undefined : (e) => { asked.push(e.key + (e.ctrlKey ? '+ctrl' : '')); return !!(e.ctrlKey ? 'ahlnopqs'.indexOf(e.key) >= 0 : /^F[123]$/.test(e.key)); },       // (the page's ANTS_PAGE.cancelsBrowserKey, in the cases that matter here)
    };
    const menu = Menu.forPage(page);
    const p = { shapeChanged: () => watchers.slice().forEach((fn) => fn()), asked, doc, win, game, state, page, menu, auto, stage, canvas, header, menuBtn, picwrap, rbar, toast, chat, shapes, lockState, touchState, intervals, box };
    p.styleLoaded = () => { doc.links.forEach((l) => { if (l.onload) l.onload(); }); };
    p.poll = () => intervals.forEach((i) => i.fn());
    p.ready = () => { p.styleLoaded(); p.poll(); return p; };
    p.open = () => { win.antsGameMenuAsk(); return doc.getElementById('game-menu'); };
    p.q = (sel) => doc.root.querySelector(sel);
    p.qa = (sel) => doc.root.querySelectorAll(sel);
    p.rows = () => p.qa('.gm-row').map((r) => ({ name: r.querySelector('.gm-name') ? r.querySelector('.gm-name').textContent : null, fresh: !!r.querySelector('.gm-new'), hint: r.querySelector('.gm-hint') ? r.querySelector('.gm-hint').textContent : null }));
    p.tab = (id) => { doc.getElementById('gm-tab-' + id).click(); };
    return p;
}

const MATCH_ROWS = JSON.parse(MATCH).rows, WATCH_ROWS = JSON.parse(WATCH).rows;
const names = (rows, sec) => rows.filter((r) => r.sec === sec).map((r) => r.label);

// ---- the buttons: when they show, where they stand, when they are off ---------------------------------------------------------------------------------------------------
{
    const p = makePage();
    ok('the script defines the function that the game calls for Esc', typeof p.win.antsGameMenuAsk === 'function');
    ok('the style is fetched with the script (one link)', p.doc.links.length === 1 && p.doc.links[0].rel === 'stylesheet' && p.doc.links[0].href === 'game_menu.css?v=7');
    const all = p.qa('.gm-btn');
    expect('one button, in the header, none under the picture and none over it (there is no strip under the picture)', all.map((b) => b.id), ['settings-btn']);
    expect('the header button stands right after Menu, at every width', p.header.children.map((c) => c.id), ['menu-btn', 'settings-btn']);
    ok('none is in the stage, the element that goes fullscreen: nothing is laid over the game\'s picture', all.every((b) => !p.stage.contains(b)) && !p.q('#settings-btn-fs'));
    ok('all hide until the style is there', all.every((b) => b.hidden && b.style.display === 'none'));
    ok('every one is a real button with the page\'s look and the word Settings', all.every((b) => b.tagName === 'button' && b.type === 'button' && /(^| )btn( |$)/.test(b.className) && b.textContent === 'Settings' && b.getAttribute('aria-label') === 'Settings' && b.getAttribute('aria-haspopup') === 'dialog'));
    p.styleLoaded();
    p.poll();
    expect('with the style and a running game the header button shows', [p.q('#settings-btn').hidden], [false]);
    p.state.fullscreen = true;
    p.poll();
    ok('fullscreen makes no button over the picture', !p.q('#settings-btn-fs') && all.every((b) => !p.stage.contains(b)));
    p.state.fullscreen = false;
    p.state.ready = false;
    p.poll();
    ok('none shows before the game runs', all.every((b) => b.hidden));
    p.state.ready = true;
    p.game.needsAnswer = 1;
    p.poll();
    ok('a dialog that needs its answer leaves them on, for the small form (the title says which)', all.filter((b) => !b.hidden).every((b) => !b.disabled && b.title === 'Settings: picture shape and fullscreen mouse'));
    p.game.needsAnswer = 0;
    p.game.where = -1;
    p.poll();
    ok('so does a screen that is not a match', all.filter((b) => !b.hidden).every((b) => !b.disabled && b.title === 'Settings: picture shape and fullscreen mouse'));
    p.game.where = 0;
    p.poll();
    ok('and in a match with nothing waiting the title is the full menu\'s', all.filter((b) => !b.hidden).every((b) => !b.disabled && b.title === 'Settings: sound, picture, controls and Leave game'));
    expect('the poll is 250 ms', p.intervals.map((i) => i.ms), [250]);
    expect('...once', p.intervals.length, 1);
    p.game.where = -1;
    p.poll();
    ok('the game\'s Esc outside a match opens nothing (the small form is for the button)', !p.open() && !p.menu.isOpen());
    p.game.where = 0;
    p.state.ready = false;
    p.poll();
    ok('Esc opens nothing before the game runs', !p.open() && !p.menu.isOpen());
    p.state.ready = true;
    p.game.where = 0;
    p.poll();
    p.q('#settings-btn').click();
    ok('a click on a button opens the panel', p.menu.isOpen() && p.q('#game-menu') !== null);
}
{
    const p = makePage({ watch: true });
    expect('a recording has one button, in the replay bar before the Picture button', p.rbar.children.map((c) => c.id), ['settings-btn-bar', 'picwrap']);
    ok('and none in the header, under the picture or in the stage', !p.q('#settings-btn') && !p.q('#settings-btn-fs'));
}
{
    const p = makePage({ noModule: true }).ready();
    ok('a game that is not there: the buttons are off, Esc opens nothing', p.qa('.gm-btn').every((b) => b.disabled && b.title === 'Not now') && !p.open());
}
{
    const p = makePage();
    p.state.ready = true;
    p.poll();
    ok('without the style the buttons stay hidden and nothing opens, from Esc or a press on one', p.qa('.gm-btn').every((b) => b.hidden) && !p.open() && (p.q('#settings-btn').click(), !p.menu.isOpen()));
    const link = p.doc.links[0];
    link.onerror();
    ok('a style that fails is dropped and fetched once more', !link.parentNode && p.doc.links.length === 1);
    p.doc.timers = [];
}

// ---- the panel: what is drawn --------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const p = makePage().ready();
    const scrim = p.open();
    ok('Esc\'s request opens the panel', !!scrim && p.menu.isOpen());
    ok('it is a child of the stage, the element that goes fullscreen', scrim.parentNode === p.stage);
    const panel = p.q('.gm-panel');
    expect('a modal dialog named by its title', [panel.getAttribute('role'), panel.getAttribute('aria-modal'), panel.getAttribute('aria-labelledby'), p.q('#gm-title').textContent], ['dialog', 'true', 'gm-title', 'Settings']);
    expect('four sections in a tab list', p.qa('[role=tab]').map((t) => t.textContent), ['Sound', 'Video', 'Controls', 'Game']);
    expect('the first is chosen, and the only one in the tab order', p.qa('[role=tab]').map((t) => [t.getAttribute('aria-selected'), t.getAttribute('tabindex')]), [['true', '0'], ['false', '-1'], ['false', '-1'], ['false', '-1']]);
    expect('the Sound section: the table\'s rows in the table\'s order', p.rows().map((r) => r.name), names(MATCH_ROWS, 'sound'));
    expect('the NEW tags are on Master volume and Mute only', p.rows().map((r) => r.fresh), [true, false, false, true]);
    expect('the hints are the table\'s', p.rows().map((r) => r.hint), MATCH_ROWS.filter((r) => r.sec === 'sound').map((r) => r.hint));
    expect('the note says that the game runs', p.q('.gm-note').textContent, 'The game keeps running while this menu is open.');
    expect('Resume and Leave game', p.qa('.gm-btns button').map((b) => b.textContent), ['Resume', 'Leave game']);
    p.tab('video');
    expect('the Video section (the picture\'s shape and the mouse are the page\'s rows)', p.rows().map((r) => r.name), names(MATCH_ROWS, 'video'));
    p.tab('game');
    expect('the Game section', p.rows().map((r) => r.name), names(MATCH_ROWS, 'game'));
    ok('the version is the page\'s', p.qa('.gm-info').length === 1 && p.q('.gm-info').textContent === 'v0.12.111');
    const drawn = [];
    ['sound', 'video', 'game'].forEach((s) => { p.tab(s); drawn.push.apply(drawn, p.rows().map((r) => r.name)); });
    expect('every row of the table is drawn once, and nothing else', drawn.slice().sort(), MATCH_ROWS.map((r) => r.label).sort());
    p.tab('controls');
    ok('the Controls list: the mouse, the keys, and a note, with no control in it', p.qa('.gm-gh').map((h) => h.textContent).join() === 'MOUSE,KEYS' && p.qa('.gm-krow').length === Menu.CONTROLS.mouse.length + Menu.CONTROLS.keys.length && p.qa('.gm-fine').length === 1 && p.qa('.gm-panel input').length === 0);
    ok('...each group\'s lines are in one grid under its heading (two columns of pairs on a wide panel: both columns have the heading above them)', p.qa('.gm-kgrid').length === 2 && p.qa('.gm-kgrid')[0].children.length === Menu.CONTROLS.mouse.length && p.qa('.gm-kgrid')[1].children.length === Menu.CONTROLS.keys.length);
    ok('...the Esc line says what Esc does now', p.qa('.gm-krow').some((r) => r.textContent.indexOf('Esc') === 0 && r.textContent.indexOf('Deselect; then this menu') > 0));
    ok('...a chord is its keys', p.qa('.gm-krow').some((r) => r.querySelectorAll('.gm-kbd').map((k) => k.textContent).join('+') === 'Ctrl+N+P' && r.textContent.indexOf('/') > 0));
    p.menu.close();
}
{
    const p = makePage({ touch: true }).ready();
    p.open();
    p.tab('controls');
    ok('a touch screen has the touch list and not the mouse\'s', p.qa('.gm-gh').map((h) => h.textContent).join() === 'TOUCH,KEYS' && p.qa('.gm-krow').length === Menu.CONTROLS.touch.length + Menu.CONTROLS.keys.length);
    p.tab('video');
    ok('...and no Fullscreen mouse row (a finger never locks)', p.rows().map((r) => r.name).indexOf('Fullscreen mouse') < 0 && p.rows().length === names(MATCH_ROWS, 'video').length - 1);
    expect('the tabs are a row there', p.q('[role=tablist]').getAttribute('aria-orientation'), 'horizontal');
}
{
    // the hotkey buttons' own row (web/touch_keys.js): a row of the page, not of the game's table, in Video right after the Fullscreen mouse row, on a touch screen only
    const t = makePage({ touch: true, touchKeys: true }).ready();
    t.open();
    t.tab('video');
    const videoNow = t.rows().map((r) => r.name);
    expect('a touch screen with the hotkey buttons has a Touch buttons row at the end of the Video rows that it shows', videoNow, names(MATCH_ROWS, 'video').filter((n) => n !== 'Fullscreen mouse').slice(0, 2).concat(['Touch buttons'], names(MATCH_ROWS, 'video').slice(3)));
    const seg = t.qa('.gm-k-touchkeys .gm-opt');
    expect('...On and Off, On marked (the default)', [seg.map((b) => b.textContent), seg.map((b) => b.getAttribute('aria-checked'))], [['On', 'Off'], ['true', 'false']]);
    seg[1].click();
    expect('...Off is stored through the page\'s object and marked', [t.touchState.sets, t.qa('.gm-k-touchkeys .gm-opt').map((b) => b.getAttribute('aria-checked'))], [[false], ['false', 'true']]);
    t.qa('.gm-k-touchkeys .gm-opt')[0].click();
    expect('...and On again', [t.touchState.sets, t.touchState.on], [[false, true], true]);
    t.menu.close();
    const n = makePage({ touch: true, touchKeys: 'no' }).ready();
    n.open();
    n.tab('video');
    ok('a page whose buttons are not available (not a touch screen) has no Touch buttons row', n.rows().map((r) => r.name).indexOf('Touch buttons') < 0);
    const w = makePage({ touch: true }).ready();
    w.open();
    w.tab('video');
    ok('a page without the hotkey buttons has no Touch buttons row', w.rows().map((r) => r.name).indexOf('Touch buttons') < 0);
}

// the small form (the gear outside a match or under a dialog) has no Touch buttons row, on a touch screen with the buttons too: the setting changes nothing there
const outsideTouch = (() => { const o = makePage({ touch: true, touchKeys: true }).ready(); o.game.where = -1; o.poll(); return o; })();
{
    const p = outsideTouch;
    p.q('#settings-btn').click();
    ok('the small form on a touch screen with the hotkey buttons: a panel with no tabs', p.menu.isOpen() && p.qa('[role=tab]').length === 0);
    expect('...has no Touch buttons row (the table\'s page rows only)', [p.rows().map((r) => r.name).indexOf('Touch buttons') < 0, p.qa('.gm-k-touchkeys').length], [true, 0]);
}
{
    const p = makePage({ noLock: true }).ready();
    p.open();
    p.tab('video');
    ok('a page without the pointer lock has no Fullscreen mouse row', p.rows().map((r) => r.name).indexOf('Fullscreen mouse') < 0);
}
{
    const p = makePage({ noPick: true }).ready();
    p.open();
    p.tab('video');
    ok('a page without its own pick of the shape (no dropdown to follow) has no Picture shape row', p.rows().map((r) => r.name).indexOf('Picture shape') < 0 && !p.q('.pick-select'));
}
{
    const p = makePage({ keepShape: true }).ready();                                     // (a page that did not take the pick: the game's picture stays as it was)
    p.open();
    p.tab('video');
    const pick = p.q('.gm-pick .pick-select');
    pick.click();
    p.q('#gm-picture-o4').click();
    expect('a pick that the page did not take: the dropdown shows what the game has, not what was asked', [p.shapes.clicked, pick.textContent], [['20:9'], '16:9 \u00b7 960 x 540']);
}
{   // the Automatic entry of the Picture shape list: the first row, in use until a shape is picked, chosen again to go back
    const p = makePage({ auto: true }).ready();
    p.open();
    p.tab('video');
    const pick = p.q('.gm-pick .pick-select');
    expect('the Picture shape list has Automatic first, and says what it gives now', [p.qa('.gm-pick [role=option]').length, (p.q('#gm-picture-oauto') || {}).textContent, p.q('#gm-picture-oauto') && p.q('#gm-picture-oauto').getAttribute('data-shape')], [8, 'Automatic (now 20:9)', AntsShapes.AUTO]);
    expect('Automatic is in use at the start: the button says it', [pick.textContent, p.q('#gm-picture-oauto').getAttribute('aria-selected')], ['Automatic (now 20:9)', 'true']);
    pick.click();
    p.q('#gm-picture-o3').click();
    expect('a shape chosen over Automatic is the page\'s pick and the button shows the shape', [p.shapes.clicked, pick.textContent, p.q('#gm-picture-oauto').getAttribute('aria-selected')], [['16:9'], '16:9 \u00b7 960 x 540', 'false']);
    pick.click();
    p.q('#gm-picture-oauto').click();
    expect('Automatic chosen again is handed to the page as its AUTO, and the button says Automatic', [p.shapes.clicked, pick.textContent], [['16:9', AntsShapes.AUTO], 'Automatic (now 20:9)']);
    p.auto.now = '16:9'; p.stage.setAttribute('data-aspect', '16:9'); p.shapeChanged();
    expect('the panel open over a turn: the button and the row say what Automatic gives now, and Automatic stays the row in use', [pick.textContent, p.q('#gm-picture-oauto').textContent, p.q('#gm-picture-oauto').getAttribute('aria-selected')], ['Automatic (now 16:9)', 'Automatic (now 16:9)', 'true']);
}
{
    const p = makePage({ watch: true }).ready();
    const scrim = p.open();
    ok('a recording opens it too', !!scrim);
    expect('Sound, Video and Controls only (no Game: chat and quick help do nothing there)', p.qa('[role=tab]').map((t) => t.textContent), ['Sound', 'Video', 'Controls']);
    expect('the tag says what is watched', p.q('.gm-tag').textContent, 'Watching a replay');
    expect('the note says that the replay keeps playing and where the pause is', p.q('.gm-note').textContent, 'The replay keeps playing. Pause it with the bar.');
    expect('"Back to the matches" is the second button', p.qa('.gm-btns button').map((b) => b.textContent), ['Resume', 'Back to the matches']);
    expect('the Sound section is the same', p.rows().map((r) => r.name), names(WATCH_ROWS, 'sound'));
    p.tab('video');
    expect('the Video section is the same rows', p.rows().map((r) => r.name), names(WATCH_ROWS, 'video'));
    p.tab('controls');
    ok('the Controls list is the view-only one', p.qa('.gm-krow').length === Menu.CONTROLS.mouseWatch.length + Menu.CONTROLS.keys.filter((k) => k[2] !== 'watch').length);
    p.menu.close();
    const live = makePage({ watch: true, live: true }).ready();
    live.open();
    expect('a live match that is watched has its own tag and note', [live.q('.gm-tag').textContent, live.q('.gm-note').textContent], ['Watching live', 'The match keeps playing.']);
    live.q('.gm-back').click();
    expect('Back to the matches asks nothing: it goes at once', [live.menu.isOpen(), live.game.calls.filter((c) => c[0] === 'do')], [false, [['do', 1]]]);
}

// ---- the values ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const p = makePage().ready();
    p.open();
    const sliders = p.qa('.gm-panel input');
    expect('three sliders in Sound, each with the table\'s range', sliders.map((s) => [s.type, s.min, s.max, s.step]), [['range', '0', '99', '1'], ['range', '0', '99', '1'], ['range', '0', '99', '1']]);
    expect('they show what the game says (a value past the end is the end)', sliders.map((s) => s.value), ['99', '65', '99']);
    expect('...and the number beside each', p.qa('.gm-val').map((v) => v.textContent), ['99', '65', '99']);
    const master = sliders[0], music = sliders[1], sfx = sliders[2];
    expect('the track is green up to the thumb, as drawn (--f is the share of the range)', sliders.map((s) => s.getAttribute('style')), ['--f:100%', '--f:65.65656565656566%', '--f:100%']);
    master.value = '40';
    master.fire('input', {});
    expect('...and the green follows the thumb while it is dragged', master.getAttribute('style'), '--f:40.4040404040404%');
    expect('Master volume acts while it is dragged', p.game.calls.filter((c) => c[0] === 'set'), [['set', 1, 40]]);
    expect('...and the number follows', p.qa('.gm-val')[0].textContent, '40');
    expect('...heard and not written (the save flag is 0 while it is dragged)', p.game.saves, [[1, 40, 0]]);
    master.fire('change', {});
    expect('...and at the release', p.game.calls.filter((c) => c[0] === 'set').length, 2);
    expect('...it is written (the flag is 1)', p.game.saves[1], [1, 40, 1]);
    p.game.calls.length = 0;
    music.value = '30';
    music.fire('input', {});
    expect('the original\'s sliders do not act while dragged (the music would restart at every step)', [p.game.calls.length, p.qa('.gm-val')[1].textContent], [0, '30']);
    music.fire('change', {});
    expect('...they act at the release', p.game.calls, [['set', 2, 30]]);
    sfx.value = '77';
    sfx.fire('change', {});
    expect('Sound effects volume is row 3', p.game.calls[1], ['set', 3, 77]);
    p.game.calls.length = 0;
    const mute = p.qa('[role=radiogroup]')[0];
    expect('Mute everything is an On / Off pair, Off chosen', [mute.children.map((b) => b.textContent), mute.children.map((b) => b.getAttribute('aria-checked'))], [['On', 'Off'], ['false', 'true']]);
    mute.children[0].click();
    expect('On mutes (value 1 of row 4) and is shown pressed in', [p.game.calls, mute.children.map((b) => b.getAttribute('aria-checked')), mute.children[0].className.indexOf(' on') > 0], [[['set', 4, 1]], ['true', 'false'], true]);
    mute.children[1].click();
    expect('Off unmutes', [p.game.calls[1], mute.children.map((b) => b.getAttribute('aria-checked'))], [['set', 4, 0], ['false', 'true']]);
    ok('a pair is a radio group named by its row, its buttons radios', mute.getAttribute('role') === 'radiogroup' && mute.getAttribute('aria-labelledby') === p.qa('.gm-name')[3].id && mute.children.every((b) => b.getAttribute('role') === 'radio'));
    ok('a slider is named by its row', sliders.every((s, i) => s.getAttribute('aria-labelledby') === p.qa('.gm-name')[i].id));

    p.game.calls.length = 0;
    p.tab('video');
    const video = p.qa('[role=radiogroup]');
    expect('the Video section: the mouse, the two rows of the ant health and the damage and heal numbers are button groups (the picture\'s shape is a dropdown)', video.map((g) => g.children.map((b) => b.textContent)), [['Locked', 'Free'], ['Hurt ants', 'All ants', 'Off'], ['Bar', 'Number'], ['On', 'Off']]);
    const dropdown = p.qa('.pick-select');
    expect('one dropdown in the panel: the picture\'s shape, the page\'s picker (a button), in the row of its own', [dropdown.length, dropdown[0].tagName, dropdown[0].parentNode.className + '/' + dropdown[0].parentNode.parentNode.className, dropdown[0].parentNode.parentNode.parentNode.querySelector('.gm-name').textContent], [1, 'button', 'pick/gm-ctl gm-pick', 'Picture shape']);
    ok('...no native select is in the panel', !p.q('select') && p.qa('option').length === 0);
    const pick = dropdown[0];
    ok('...with the page\'s look and the menu\'s own size (the classes pick-select and gm-select)', pick.className === 'pick-select gm-select');
    ok('...named by its row', pick.getAttribute('aria-labelledby') === pick.parentNode.parentNode.parentNode.querySelector('.gm-name').id);
    const rows = p.qa('.gm-pick [role=option]');
    expect('...with one entry for each row of the page\'s table (web/shapes.js), in its order: the shape and its canvas', rows.map((o) => o.textContent), AntsShapes.rows.map((r) => r.label + ' \u00b7 ' + r.width + ' x ' + r.height));
    expect('...and as many as the game\'s own row has words (the table of the game is the one that counts)', rows.length, JSON.parse(MATCH).rows.filter((r) => r.key === 'picture')[0].words.length);
    expect('...the shape that the page has is the shown one', [pick.textContent, rows.map((o) => o.getAttribute('aria-selected')).indexOf('true')], ['16:9 \u00b7 960 x 540', 3]);
    expect('...and a quiet line under it names the shape that fills the screen (a 1440 x 900 laptop)', p.qa('.gm-fits').map((n) => n.textContent), ['16:10 fills your screen']);
    ok('...which the dropdown is described by (aria-describedby names the line)', !!p.q('.gm-fits').id && pick.getAttribute('aria-describedby') === p.q('.gm-fits').id);
    ok('...the list is shut, in the panel (the stage goes fullscreen with it), and a press on the button opens it', p.q('.gm-pick [role=listbox]').hidden === true && p.q('.gm-scrim').contains(p.q('.gm-pick [role=listbox]')) && (pick.click(), p.q('.gm-pick [role=listbox]').hidden === false));
    p.q('#gm-picture-o5').click();
    expect('a choice is the page\'s own function, called (the selector, the address and the stored choice stay one function)', [p.shapes.clicked, p.stage.getAttribute('data-aspect'), p.game.calls.length, p.q('.gm-pick [role=listbox]').hidden], [['21:9'], '21:9', 0, true]);
    expect('...and the dropdown follows the page (what the page has now)', pick.textContent, '21:9 \u00b7 1260 x 540');
    pick.click();
    p.q('#gm-picture-o6').click();
    expect('...every shape of the table can be chosen (32:9 is the last)', [p.shapes.clicked, p.stage.getAttribute('data-aspect'), pick.textContent], [['21:9', '32:9'], '32:9', '32:9 \u00b7 1920 x 540']);
    pick.click();
    p.q('#gm-picture-o6').click();
    expect('the shape that is shown, chosen again, is no choice: the page is not called', p.shapes.clicked, ['21:9', '32:9']);
    ok('the dropdown is in the panel\'s own focus order and no button of a shape is there', !p.qa('.gm-panel button').some((b) => /^\d+:\d+$/.test(b.textContent)) && pick.getAttribute('disabled') === null);
    expect('the mouse starts Locked', video[0].children.map((b) => b.getAttribute('aria-checked')), ['true', 'false']);
    video[0].children[1].click();
    expect('Free is the page\'s setting (the page\'s lock object is told, the row shows it)', [p.game.calls, p.lockState.on, video[0].children.map((b) => b.getAttribute('aria-checked'))], [[['lock', 'off']], false, ['false', 'true']]);
    expect('Ant health starts on the game\'s value (Hurt ants), Show health as on Bar', [video[1], video[2]].map((g) => g.children.map((b) => b.getAttribute('aria-checked'))), [['true', 'false', 'false'], ['true', 'false']]);
    video[1].children[2].click();
    expect('Ant health is row 8, and its words are its values in their order (Off is 2), shown pressed in', [p.game.calls[1], video[1].children.map((b) => b.getAttribute('aria-checked'))], [['set', 8, 2], ['false', 'false', 'true']]);
    video[1].children[1].click();
    expect('All ants is value 1 of row 8', p.game.calls[2], ['set', 8, 1]);
    video[2].children[1].click();
    expect('Show health as is row 15: Number is value 1, and is shown pressed in', [p.game.calls[3], video[2].children.map((b) => b.getAttribute('aria-checked'))], [['set', 15, 1], ['false', 'true']]);
    expect('...and the game\'s own rows are kept at once (the save flag is 1)', p.game.saves.filter((x) => x[0] === 8 || x[0] === 15), [[8, 2, 1], [8, 1, 1], [15, 1, 1]]);
    const scroll = p.qa('.gm-panel input')[0];
    scroll.value = '12';
    scroll.fire('change', {});
    expect('the scroll speed is row 9', p.game.calls[4], ['set', 9, 12]);
    expect('Damage & heal numbers is the row of its name, On (value 1) by default, a switch after Show health as', [p.qa('.gm-name').map((n) => n.textContent).filter((t) => /numbers$/.test(t)), video[3].children.map((b) => b.getAttribute('aria-checked'))], [['Damage & heal numbers'], ['true', 'false']]);
    video[3].children[1].click();
    expect('Off is row 16, value 0, shown pressed in, and kept at once', [p.game.calls[5], video[3].children.map((b) => b.getAttribute('aria-checked')), p.game.saves[p.game.saves.length - 1]], [['set', 16, 0], ['false', 'true'], [16, 0, 1]]);
    video[3].children[0].click();
    expect('On is value 1 of row 16', [p.game.calls[6], video[3].children.map((b) => b.getAttribute('aria-checked'))], [['set', 16, 1], ['true', 'false']]);

    p.game.calls.length = 0;
    const fsButton = p.qa('.gm-act')[0];
    expect('the Fullscreen row offers to enter', fsButton.textContent, 'Enter fullscreen');
    fsButton.click();
    expect('its button is the page\'s enter (a click of the player\'s: the browser\'s permission)', [p.state.enters, p.game.calls.length], [1, 0]);
    p.poll();
    expect('and the word follows the page\'s state', p.qa('.gm-act')[0].textContent, 'Leave fullscreen');
    p.qa('.gm-act')[0].click();
    expect('...so that a second press leaves', [p.state.leaves, p.state.fullscreen], [1, false]);

    p.tab('game');
    const game = p.qa('[role=radiogroup]');
    expect('the Game section has the two switches', game.length, 2);
    game[0].children[1].click();
    game[1].children[1].click();
    expect('Participate in chat is row 10 and the quick help at startup row 11', p.game.calls.slice(-2), [['set', 10, 0], ['set', 11, 0]]);
    p.game.calls.length = 0;
    p.qa('.gm-act')[0].click();
    expect('Quick help (F1) closes the panel and then opens the original\'s help (act 2)', [p.menu.isOpen(), p.game.calls], [false, [['open', 0], ['do', 2]]]);
    const q = makePage().ready();
    q.open();
    q.tab('game');
    q.game.calls.length = 0;
    q.qa('.gm-act')[1].click();
    expect('the original\'s options window is act 3, after the panel closed', [q.menu.isOpen(), q.game.calls], [false, [['open', 0], ['do', 3]]]);
}

// ---- opening and closing -------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const p = makePage().ready();
    p.state.locked = true;
    let scrim = p.open();
    expect('the game is told, and the pointer lock is let go', [p.game.calls[0], p.state.locks], [['open', 1], ['release']]);
    expect('the page behind it is inert: the header, the picture\'s box, the toast', [p.header.hasAttribute('inert'), p.box.hasAttribute('inert'), p.rbar.hasAttribute('inert'), p.toast.hasAttribute('inert')], [true, true, true, true]);
    ok('the stage is not inert (the panel is in it) and the panel is not', !p.stage.hasAttribute('inert') && !scrim.hasAttribute('inert'));
    expect('the focus is on Resume', p.doc.activeElement.textContent, 'Resume');
    ok('the lift of a key that went down before the panel opened is left to the game (it would think a key still held, and read the next press as a repeat)', p.doc.key('q', { type: 'keyup' }).reachedPage);
    expect('a second request opens nothing more', [p.win.antsGameMenuAsk(), p.qa('.gm-scrim').length, p.game.calls.filter((c) => c[0] === 'open').length], [undefined, 1, 1]);

    // keys
    const tab = p.doc.key('Tab');
    ok('Tab from Resume goes on inside (the browser moves it) and nothing reaches the page', !tab.prevented && !tab.reachedPage);
    p.qa('.gm-leave')[0].focus();
    let e = p.doc.key('Tab');
    const firstControl = p.qa('.gm-panel button').filter((b) => !b.disabled && b.getAttribute('tabindex') !== '-1')[0];
    ok('Tab from the last control wraps to the first (the cross where it shows, else the chosen section)', e.prevented && p.doc.activeElement === firstControl);
    e = p.doc.key('Tab', { shiftKey: true });
    ok('Shift+Tab from the first wraps to the last', e.prevented && p.doc.activeElement.textContent === 'Leave game');
    p.doc.active = p.doc.body;
    e = p.doc.key('Tab');
    ok('Tab with the focus outside comes back in', e.prevented && scrim.contains(p.doc.activeElement));
    e = p.doc.key('a');
    ok('a letter is left to the page (the game drops it: it was told)', e.reachedPage && !e.prevented);
    e = p.doc.key('s', { ctrlKey: true });
    ok('so is Ctrl+S: the page cancels the browser\'s own reaction to it, as it does in the game', e.reachedPage);
    e = p.doc.key('F1');
    ok('...and F1', e.reachedPage);
    e = p.doc.key('Enter');
    ok('Enter and Space are the panel\'s own', !e.reachedPage && !p.doc.key(' ').reachedPage);
    // the Picture shape's dropdown (the page's picker): its keys are its own (taken in the window's capture phase, ahead of the panel's), the game is not told
    p.tab('video');
    const pick = p.q('.gm-pick .pick-select'), list = p.q('.gm-pick [role=listbox]');
    pick.focus();
    e = p.doc.key('ArrowDown');
    ok('ArrowDown on the closed dropdown opens the list: taken (not the panel\'s section keys, and the page behind does not get it: the game would scroll the map), the picture is not changed', e.prevented && !e.reachedPage && p.doc.activeElement === pick && !list.hidden && !p.shapes.clicked);
    e = p.doc.key('ArrowDown');
    ok('ArrowDown in the open list moves the mark (taken the same way), and chooses nothing', e.prevented && !e.reachedPage && pick.getAttribute('aria-activedescendant') === 'gm-picture-o4' && !p.shapes.clicked);
    e = p.doc.key('Enter');
    expect('Enter chooses the marked row: the picture of the running game is changed at once (the page\'s function), the list closes, the panel stays open and the focus stays on the dropdown', [p.shapes.clicked, list.hidden, p.menu.isOpen(), p.doc.activeElement === pick, e.prevented], [['20:9'], true, true, true, true]);
    p.doc.key('ArrowUp');
    e = p.doc.key('Escape');
    expect('Esc with the list open closes the list ALONE: the panel stays open, the key is taken (it is not the panel\'s Esc, which would close it), the focus is on the dropdown', [list.hidden, p.menu.isOpen(), e.prevented, e.reachedPage, p.doc.activeElement === pick, p.game.opened], [true, true, true, false, true, true]);
    e = p.doc.key('Escape');
    expect('...and the next Esc, with the list shut, is the panel\'s: it closes', [p.menu.isOpen(), e.prevented], [false, true]);
    p.open();
    p.tab('video');
    const again = p.q('.gm-pick .pick-select');
    again.focus();
    p.doc.key('Enter');
    e = p.doc.key('Tab');
    ok('Tab on the open list closes it and goes on inside (the browser moves it): not prevented, not for the page', !e.prevented && !e.reachedPage);
    ok('...and the list is shut', p.q('.gm-pick [role=listbox]').hidden);
    ok('the window has no listener of the picker once the panel is closed (it goes with the panel)', (p.menu.close(), p.win.count('keydown') === 0 && p.win.count('keyup') === 0));
    scrim = p.open();                                                                    // (the panel again: a new one)
    p.tab('video');
    const held = p.win.count('keydown');
    p.tab('sound');
    ok('a section that leaves the Video one takes the dropdown\'s window listeners with it (and its list, if it was open)', held === 1 && p.win.count('keydown') === 0 && p.win.count('keyup') === 0);
    p.tab('video');
    // the dropdown is in the panel's own focus list (the wrap at the ends uses it): with every button and input turned off it is the only control, and Tab / Shift+Tab hold the focus on it
    const others = p.qa('.gm-scrim button').concat(p.qa('.gm-scrim input')).filter((n) => !n.disabled && n.className.indexOf('pick-select') < 0);
    others.forEach((n) => { n.disabled = true; });
    const alone = p.q('.gm-pick .pick-select');
    alone.focus();
    e = p.doc.key('Tab');
    const eBack = p.doc.key('Tab', { shiftKey: true });
    ok('the dropdown alone in the panel is both the first and the last control: Tab and Shift+Tab wrap onto it (the browser does not leave the panel)', e.prevented && eBack.prevented && p.doc.activeElement === alone);
    others.forEach((n) => { n.disabled = false; });
    p.qa('[role=tab]')[0].focus();
    p.tab('sound');
    e = p.doc.key('ArrowDown');
    expect('the arrow keys move between the sections, and the section follows', [e.prevented, p.doc.activeElement.textContent, p.qa('[role=tab]')[1].getAttribute('aria-selected')], [true, 'Video', 'true']);
    p.doc.key('End');
    expect('End goes to the last', p.doc.activeElement.textContent, 'Game');
    p.doc.key('ArrowRight');
    expect('...and the arrows wrap', p.doc.activeElement.textContent, 'Sound');
    p.doc.key('Home');
    expect('Home goes to the first', p.doc.activeElement.textContent, 'Sound');

    // the dimmed area, Esc
    scrim.fire('click', { target: scrim });
    scrim.fire('mousedown', { target: scrim });
    ok('a click on the dimmed area does nothing', p.menu.isOpen());
    e = p.doc.key('Escape', { repeat: true });
    ok('a held Esc does nothing (and is not the page\'s)', p.menu.isOpen() && e.prevented && !e.reachedPage);
    p.state.locks.length = 0;
    p.game.calls.length = 0;
    p.doc.active = p.doc.body;
    e = p.doc.key('Escape');
    ok('Esc closes it, even with the focus on the body', !p.menu.isOpen() && p.q('#game-menu') === null && e.prevented && !e.reachedPage);
    expect('the game is told it may take its keys again', p.game.calls, [['open', 0]]);
    expect('the pointer lock is asked for again, as it was held', p.state.locks, ['request']);
    expect('the page is live again', [p.header.hasAttribute('inert'), p.box.hasAttribute('inert'), p.toast.hasAttribute('inert')], [false, false, false]);
    ok('the focus is on the canvas', p.doc.activeElement === p.canvas);
    expect('the pointer lock and fullscreen listeners are gone (the keys\' stay while Esc is held: below)', [p.win.count('pointerlockchange'), p.doc.count('fullscreenchange')], [0, 0]);
    e = p.doc.key('Escape', { type: 'keyup' });
    ok('the lift of the Esc that closed it is the panel\'s too', !e.reachedPage);
    ['Tab', 'Enter', ' ', 'ArrowDown', 'ArrowUp', 'ArrowRight', 'Home', 'End'].forEach((k) => p.doc.key(k, { type: 'keyup' }));         // (the keys pressed on the way, lifted)
    expect('...and then no listener is left on the document or the window', [p.doc.count('keydown'), p.doc.count('keypress'), p.doc.count('keyup'), p.win.count('pointerlockchange'), p.doc.count('fullscreenchange')], [0, 0, 0, 0, 0]);
    e = p.doc.key('Escape');
    ok('with the panel gone the document takes no Esc of its own', !e.prevented && e.reachedPage);
}
{
    const p = makePage().ready();
    p.open();
    p.state.locks.length = 0;
    p.qa('.gm-resume')[0].click();
    ok('Resume closes it without asking for a lock that was not held (a window with the mouse free)', !p.menu.isOpen() && p.state.locks.length === 0);
    p.state.fullscreen = true;
    p.open();
    p.qa('.gm-resume')[0].click();
    expect('in fullscreen Resume asks for the lock (the click is the gesture)', p.state.locks.filter((x) => x === 'request').length, 1);
    p.open();
    p.qa('.gm-x')[0].click();
    ok('the cross closes it too', !p.menu.isOpen());
    expect('the cross is named', p.q('.gm-x') === null, true);
    p.open();
    ok('...with the words of the table', p.q('.gm-x').getAttribute('aria-label') === 'Close the settings' && p.q('.gm-esc').textContent.indexOf('closes') > 0);
}
{
    // the focus goes back to where it came from: the Settings button when a key opened it, the game's canvas for a click, a tap or Esc
    const p = makePage().ready();
    const b = p.q('#settings-btn');
    b.fire('click', { detail: 0 });
    ok('opened with the keyboard through the button', p.menu.isOpen());
    p.qa('.gm-resume')[0].click();
    ok('...the focus goes back to the button', p.doc.activeElement === b);
    b.fire('click', { detail: 1 });
    p.qa('.gm-resume')[0].click();
    ok('opened with a click: the canvas has it', p.doc.activeElement === p.canvas);
    b.fire('click', { detail: 0 });
    b.disabled = true;
    p.qa('.gm-resume')[0].click();
    ok('a button that is off by then does not take it', p.doc.activeElement === p.canvas);
    p.open();
    p.qa('.gm-resume')[0].click();
    ok('Esc (the game\'s own ask) leaves it to the canvas', p.doc.activeElement === p.canvas);
}
{
    // the pointer lock while it is up: the browser takes the lock again when fullscreen begins, and the panel gives it back
    const p = makePage().ready();
    p.open();
    p.state.locked = true;
    p.state.locks.length = 0;
    p.win.emit('pointerlockchange');
    ok('a lock that comes while the panel is up is let go', p.state.locks.length === 1 && p.state.locks[0] === 'release' && !p.state.locked);
    expect('the listener is on the window, in the capture phase (the page\'s own handler stops the event before the document\'s)', [p.win.count('pointerlockchange'), p.doc.count('pointerlockchange')], [1, 0]);
    p.state.locked = true;
    p.state.locks.length = 0;
    p.poll();
    ok('...and the poll lets go of one that no event told of', p.state.locks.length === 1 && !p.state.locked);
    p.state.locked = true;
    p.doc.emit('fullscreenchange');
    ok('so is the one that fullscreen asks for', !p.state.locked);
}
{
    // a dialog that needs its answer closes it, and the panel is not opened over one
    const p = makePage().ready();
    p.state.locked = true;
    p.open();
    p.game.needsAnswer = 1;
    p.state.locks.length = 0;
    p.poll();
    ok('a dialog that needs its answer closes the panel', !p.menu.isOpen());
    expect('...without taking the pointer lock back (the dialog has the screen)', p.state.locks, []);
    expect('...and the game is told', p.game.calls.slice(-1)[0], ['open', 0]);
    ok('and Esc opens nothing over it', !p.open());
    p.game.needsAnswer = 0;
    p.open();
    p.game.where = -1;
    p.poll();
    ok('the match ending (the results) closes it', !p.menu.isOpen());
}

// ---- outside a match: the small form -----------------------------------------------------------------------------------------------------------------------------------------
const PAGE_ROW_LABELS = MATCH_ROWS.filter((r) => r.page === 1).map((r) => r.label);
const SMALL_NOTE = 'Sound, ant health, controls and the game\'s own settings are here during a match.';
const outside = (opts) => { const p = makePage(opts).ready(); p.game.where = -1; p.poll(); return p; };
{
    const p = outside();
    ok('where is -1 (the opening picture, the quick help, the setup and room screens, the results): the buttons are on', p.qa('.gm-btn').every((b) => !b.disabled));
    p.q('#settings-btn').click();
    ok('...and a press opens a panel', p.menu.isOpen());
    expect('...the small form: no tabs, no tab list, and one pane', [p.qa('[role=tab]').length, p.qa('[role=tablist]').length, p.qa('.gm-pane').length], [0, 0, 1]);
    expect('...its rows are exactly the table\'s rows of the page, in the table\'s order (the three are the table\'s `page: 1`)', p.rows().map((r) => r.name), PAGE_ROW_LABELS);
    expect('...three of them', PAGE_ROW_LABELS, ['Fullscreen', 'Picture shape', 'Fullscreen mouse']);
    expect('...under the heading of their section', p.qa('.gm-gh').map((h) => h.textContent), ['Video']);
    expect('...no row of the game (no slider, no switch of the game), no Controls list, no version', [p.qa('.gm-panel input').length, p.qa('.gm-info').length, p.qa('.gm-krow').length, p.qa('.gm-btnrow').length], [0, 0, 0, 0]);
    expect('the title, the Esc hint and the cross are the full menu\'s', [p.q('#gm-title').textContent, p.q('.gm-esc').textContent.indexOf('closes') > 0, p.q('.gm-x').getAttribute('aria-label')], ['Settings', true, 'Close the settings']);
    expect('one quiet line says where the rest is', p.q('.gm-note').textContent, SMALL_NOTE);
    expect('one button: Close (no Resume, no Leave game)', p.qa('.gm-btns button').map((b) => b.textContent), ['Close']);
    expect('the focus starts on Close', p.doc.activeElement.textContent, 'Close');
    ok('it is a child of the stage, a modal dialog, with the page behind it inert', p.q('#game-menu').parentNode === p.stage && p.q('.gm-panel').getAttribute('aria-modal') === 'true' && p.header.hasAttribute('inert') && p.box.hasAttribute('inert'));
    p.q('.gm-resume').click();
    ok('Close closes it', !p.menu.isOpen() && p.q('#game-menu') === null);
}
{
    // when the screen changes under it
    const p = outside();
    p.q('#settings-btn').click();
    p.game.leave = 0;
    p.poll();
    p.poll();
    ok('the screen stays the same: the poll keeps it (it is not the old rule, which closed a panel while where was -1)', p.menu.isOpen());
    p.game.needsAnswer = 1;
    p.game.where = 0;
    p.poll();
    ok('...a match that begins under a dialog: it stays (the full menu is not possible)', p.menu.isOpen());
    p.state.locked = false;
    p.state.locks.length = 0;
    p.game.needsAnswer = 0;
    p.poll();
    ok('the dialog is answered, the match has the screen: the small form closes at the next poll', !p.menu.isOpen());
    expect('...without asking for the pointer lock, the game is told, and the focus goes to the canvas', [p.state.locks, p.game.calls.slice(-1)[0], p.doc.activeElement === p.canvas], [[], ['open', 0], true]);
    const q = outside();
    q.state.fullscreen = true;
    q.q('#settings-btn').click();
    q.state.locks.length = 0;
    q.game.where = 0;
    q.poll();
    expect('...even in fullscreen (the match needs the eyes and hands)', [q.menu.isOpen(), q.state.locks], [false, []]);
    const f = makePage().ready();
    f.open();
    f.game.where = -1;
    f.poll();
    ok('the full menu open and the match over: it closes and no small form takes its place', !f.menu.isOpen() && f.q('#game-menu') === null);
    f.game.needsAnswer = 1;
    f.game.where = 0;
    f.poll();
    ok('...a dialog does not turn the full menu into the small form either', f.q('#game-menu') === null);
    const m = makePage().ready();
    m.open();
    m.game.needsAnswer = 1;
    m.poll();
    ok('the full menu open and a dialog comes up: it closes', !m.menu.isOpen());
}
{
    const p = makePage().ready();
    p.game.needsAnswer = 1;
    p.poll();
    p.q('#settings-btn').click();
    ok('a match under a dialog (the countdown, a vote, a lost link) opens the small form too', p.menu.isOpen() && p.qa('[role=tab]').length === 0 && p.rows().length === 3);
    expect('the game is told, as for the full menu', p.game.calls, [['open', 1]]);
    p.menu.close();
}
{
    // which rows the page can serve
    const t = outside({ touch: true }); t.q('#settings-btn').click();
    expect('a touch screen has no Fullscreen mouse row (a finger never locks), the others are there', t.rows().map((r) => r.name), ['Fullscreen', 'Picture shape']);
    const l = outside({ noLock: true }); l.q('#settings-btn').click();
    expect('a page without the pointer lock: no Fullscreen mouse row', l.rows().map((r) => r.name), ['Fullscreen', 'Picture shape']);
    const k = outside({ noPick: true }); k.q('#settings-btn').click();
    expect('a page without its own pick of the shape: no Picture shape row', k.rows().map((r) => r.name), ['Fullscreen', 'Fullscreen mouse']);
    const none = outside({ noPick: true, noLock: true });
    ok('a page with neither is not the game page: the buttons are off and say so, nothing opens', none.qa('.gm-btn').every((b) => b.disabled && b.title === 'Not now') && (none.q('#settings-btn').click(), !none.menu.isOpen()));
    const gone = outside({ noModule: true });
    ok('with no game to ask there is no table to draw: off', gone.qa('.gm-btn').every((b) => b.disabled) && (gone.q('#settings-btn').click(), !gone.menu.isOpen()));
    const early = makePage({ noCancel: true });
    early.state.ready = false;
    early.styleLoaded();
    early.poll();
    ok('before the game runs there is no button, and a press on the hidden one opens nothing', early.qa('.gm-btn').every((b) => b.hidden) && (early.q('#settings-btn').click(), !early.menu.isOpen()));
    const w = outside({ watch: true });
    w.game.where = 1; w.game.needsAnswer = 1; w.poll();
    w.q('#settings-btn-bar').click();
    ok('a watched match under a dialog: the recording\'s bar has the small form, with no tag of "watching"', w.menu.isOpen() && w.rows().length === 3 && w.qa('.gm-tag').length === 0);
}
{
    // the controls do the page's things and nothing of the game's rows
    const p = outside();
    p.q('#settings-btn').click();
    const before = p.game.calls.length;
    p.q('.gm-act').click();
    expect('Fullscreen is the page\'s enter, then leave (the word follows)', [p.state.enters, p.qa('.gm-act')[0].textContent], [1, 'Enter fullscreen']);
    p.poll();
    expect('...the word follows the page\'s state', p.qa('.gm-act')[0].textContent, 'Leave fullscreen');
    p.qa('.gm-act')[0].click();
    expect('...and a second press leaves', [p.state.leaves, p.state.fullscreen], [1, false]);
    const pick = p.q('.gm-pick .pick-select');
    pick.click();
    p.q('#gm-picture-o5').click();
    expect('a shape pick calls the page\'s function with the name', [p.shapes.clicked, p.stage.getAttribute('data-aspect')], [['21:9'], '21:9']);
    const group = p.q('[role=radiogroup]');
    group.children[1].click();
    group.children[0].click();
    expect('Free and Locked are the page\'s lock setting', [p.game.calls.filter((c) => c[0] === 'lock'), p.lockState.on], [[['lock', 'off'], ['lock', 'on']], true]);
    p.qa('.gm-panel button').forEach((b) => { if (!b.disabled && b.className.indexOf('gm-resume') < 0 && b.className.indexOf('gm-x') < 0) b.click(); });
    expect('pressing every control of the small form never sets or does a row of the game', p.game.calls.filter((c) => c[0] === 'set' || c[0] === 'do'), []);
    ok('...and the game was told only that the panel is open', p.game.calls.filter((c) => c[0] === 'open').length === 1 && before === 1);
    p.menu.close();
}
{
    // keys: nothing reaches the game but the lifts and a bare modifier's press
    const p = outside();
    p.q('#settings-btn').click();
    const typed = ['s', 'q', 'x', 'c', 't', 'h', 'F1', 'Backspace', 'ArrowDown', 'F9', 'Enter', ' ', 'Tab', 'a', 'Delete'];
    const down = typed.map((k) => [k, p.doc.key(k).reachedPage]);
    expect('no key press reaches the page and the game behind it (S would start a match, Q and X quit, T opens the chat line)', down.filter((d) => d[1]), []);
    const press = typed.map((k) => [k, p.doc.key(k, { type: 'keypress' }).reachedPage]);
    expect('...no typed text either (the keypress of each is stopped)', press.filter((d) => d[1]), []);
    const ctrlS = p.doc.key('s', { ctrlKey: true });
    ok('...Ctrl+S too, and the page\'s own cancelling of the browser\'s reaction is done here', !ctrlS.reachedPage && ctrlS.prevented);
    const f1 = p.doc.key('F1');
    ok('...F1 is cancelled for the browser, a plain S is not', f1.prevented && !p.doc.key('s').prevented);
    expect('the page\'s canceller is asked for the keys that are not the panel\'s own, and not for the panel\'s own', [p.asked.indexOf('q') >= 0, p.asked.indexOf('F1') >= 0, p.asked.indexOf('s+ctrl') >= 0, p.asked.indexOf('Enter'), p.asked.indexOf(' '), p.asked.indexOf('Tab'), p.asked.indexOf('ArrowDown')], [true, true, true, -1, -1, -1, -1]);
    ['Shift', 'Control', 'Alt', 'Meta', 'AltGraph', 'CapsLock', 'NumLock', 'ScrollLock', 'OS', 'Fn'].forEach((m) => ok('a bare ' + m + ' press goes on (the game keeps count of what is held)', p.doc.key(m).reachedPage));
    ['Shift', 'z'].forEach((k) => ok('the lift of ' + k + ' goes on (a key whose press the game saw, or a bare modifier)', p.doc.key(k, { type: 'keyup' }).reachedPage));
    ['s', 'Enter', 'F1'].forEach((k) => ok('the lift of ' + k + ' is the panel\'s (its press was)', !p.doc.key(k, { type: 'keyup' }).reachedPage));
    p.menu.close();
    ['q', 'x', 'c', 't', 'h', 'Backspace', 'ArrowDown', 'F9', ' ', 'Tab', 'a', 'Delete'].forEach((k) => p.doc.key(k, { type: 'keyup' }));
    expect('the listeners go with the panel once every kept key is lifted', [p.doc.count('keydown'), p.doc.count('keypress'), p.doc.count('keyup')], [0, 0, 0]);
    ok('...and a letter is the page\'s again', p.doc.key('s').reachedPage);
    const none = outside({ noCancel: true });
    none.q('#settings-btn').click();
    const e = none.doc.key('s', { ctrlKey: true });
    ok('a page that hands no canceller: the key still stops, nothing breaks', !e.reachedPage && !e.prevented);
}
{
    // the full menu is unchanged: only its own keys stop; a letter and its text go on (the game drops them: it was told). Enter and Space stop as keypress too
    const p = makePage().ready();
    p.open();
    ok('a letter and its text go on in the full menu', p.doc.key('s').reachedPage && p.doc.key('s', { type: 'keypress' }).reachedPage);
    ok('...a Ctrl+S, an F1, a bare modifier and a lift too', p.doc.key('s', { ctrlKey: true }).reachedPage && p.doc.key('F1').reachedPage && p.doc.key('Shift').reachedPage && p.doc.key('s', { type: 'keyup' }).reachedPage);
    expect('...the page\'s canceller is not asked here (the page\'s own listener does it)', p.asked, []);
    ok('its own keys stop: Enter, Space, Tab, an arrow', ['Enter', ' ', 'Tab', 'ArrowDown'].every((k) => !p.doc.key(k).reachedPage));
    const bare = makePage().ready();
    bare.open();
    ok('the keypress of Enter and Space stops too, whatever came before it: the game cancels keypress, which would stop a native press of Enter on a button', !bare.doc.key('Enter', { type: 'keypress' }).reachedPage && !bare.doc.key(' ', { type: 'keypress' }).reachedPage);
    ok('...a letter\'s keypress goes on', bare.doc.key('s', { type: 'keypress' }).reachedPage);
    bare.menu.close();
    ok('...the lift of a letter that went down in the full menu goes on (the game saw its press)', p.doc.key('s', { type: 'keyup' }).reachedPage);
    p.menu.close();
    ['Enter', ' ', 'Tab', 'ArrowDown'].forEach((k) => p.doc.key(k, { type: 'keyup' }));
    expect('...the listeners go with the panel once the keys are lifted', [p.doc.count('keydown'), p.doc.count('keypress'), p.doc.count('keyup')], [0, 0, 0]);
}
{
    // the keys of the small form's panel: Esc, Tab, a click on the dimmed area, the focus
    const p = outside();
    const b = p.q('#settings-btn');
    b.fire('click', { detail: 0 });
    ok('a key press on the button opens it', p.menu.isOpen());
    p.doc.key('Escape', { repeat: true });
    ok('a held Esc does nothing', p.menu.isOpen());
    let e = p.doc.key('Escape');
    ok('Esc closes it once (and it stops at the panel)', !p.menu.isOpen() && e.prevented && !e.reachedPage);
    ok('...the focus goes back to the button that a key opened it from', p.doc.activeElement === b);
    b.fire('click', { detail: 1 });
    const scrim = p.q('#game-menu');
    scrim.fire('click', { target: scrim });
    scrim.fire('mousedown', { target: scrim });
    ok('a click on the dimmed area does nothing', p.menu.isOpen());
    p.qa('.gm-btns button')[0].focus();
    e = p.doc.key('Tab');
    ok('Tab from the last control wraps to the first', e.prevented && p.doc.activeElement === p.qa('.gm-panel button').filter((x) => !x.disabled)[0]);
    e = p.doc.key('Tab', { shiftKey: true });
    ok('Shift+Tab from the first wraps to the last (Close)', e.prevented && p.doc.activeElement.textContent === 'Close');
    p.qa('.gm-x')[0].click();
    ok('the cross closes it, and a click opened it: the canvas has the focus', !p.menu.isOpen() && p.doc.activeElement === p.canvas);
    p.q('#settings-btn').click();
    p.q('.gm-pick .pick-select').focus();
    p.doc.key('Enter');
    e = p.doc.key('ArrowDown');
    ok('the picture\'s list works by keys (the picker takes them first), and the panel stays', p.menu.isOpen() && e.prevented && !e.reachedPage && !p.q('.gm-pick [role=listbox]').hidden);
    e = p.doc.key('Escape');
    ok('Esc with the list open closes the list alone', p.menu.isOpen() && p.q('.gm-pick [role=listbox]').hidden);
    p.menu.close();
}
{
    // the game is told, and the pointer lock
    const p = outside();
    p.state.locked = true;
    p.q('#settings-btn').click();
    expect('the game is told 1 at open, and the lock is let go', [p.game.calls, p.state.locks], [[['open', 1]], ['release']]);
    p.state.locked = true;
    p.win.emit('pointerlockchange');
    ok('a lock that comes while it is up is let go', !p.state.locked);
    p.state.locked = true;
    p.poll();
    ok('...and the poll lets go of one that no event told of', !p.state.locked);
    p.state.locks.length = 0;
    p.game.calls.length = 0;
    p.q('.gm-resume').click();
    expect('the game is told 0 at close, and the lock is asked for again, as it was held', [p.game.calls, p.state.locks], [[['open', 0]], ['request']]);
    const q = outside();
    q.state.fullscreen = true;
    q.q('#settings-btn').click();
    q.state.locks.length = 0;
    q.q('.gm-resume').click();
    expect('in fullscreen Close asks for the lock (the click is the gesture)', q.state.locks, ['request']);
    const r = outside();
    r.q('#settings-btn').click();
    r.q('.gm-resume').click();
    expect('with the mouse free in a window it asks for nothing', r.state.locks, ['release']);
}
{
    const p = outside();
    ok('the game\'s Esc request outside a match opens nothing', !p.open() && !p.menu.isOpen());
    p.game.where = 0;
    p.game.needsAnswer = 1;
    p.poll();
    ok('...nor under a dialog', !p.open() && !p.menu.isOpen());
    p.game.needsAnswer = 0;
    p.poll();
    ok('...and in a match with nothing waiting it opens the full menu', !!p.open() && p.qa('[role=tab]').length === 4);
}

// ---- which form a press on the button opens, in each state ---------------------------------------------------------------------------------------------------------------------
{
    const form = (p) => (p.q('#game-menu') ? (p.qa('[role=tab]').length >= 3 && /^Resume,(Leave game|Back to the matches)$/.test(p.qa('.gm-btns button').map((b) => b.textContent).join()) ? 'full' : p.qa('[role=tab]').length === 0 && p.qa('.gm-btns button').map((b) => b.textContent).join() === 'Close' ? 'small' : 'odd') : 'none');
    const pressed = (setup, opts) => { const p = makePage(opts).ready(); setup(p); p.poll(); p.q(opts && opts.watch ? '#settings-btn-bar' : '#settings-btn').click(); const f = form(p); p.menu.close(); return f; };
    expect('a match with nothing waiting: the button opens the FULL menu (tabs, Resume, Leave game)', pressed(() => {}), 'full');
    expect('no match screen (the opening picture, the quick help, the setup, the room, the results): the small form', pressed((p) => { p.game.where = -1; }), 'small');
    expect('a match under a dialog: the small form', pressed((p) => { p.game.needsAnswer = 1; }), 'small');
    expect('a recording that is watched, nothing waiting: the full menu', pressed(() => {}, { watch: true }), 'full');
    expect('a recording under a dialog: the small form', pressed((p) => { p.game.needsAnswer = 1; }, { watch: true }), 'small');
    expect('a live match that is watched: the full menu', pressed(() => {}, { watch: true, live: true }), 'full');
    expect('the game\'s own Esc request in a match opens the full menu', (() => { const p = makePage().ready(); p.open(); const f = form(p); p.menu.close(); return f; })(), 'full');
}
{
    const rec = outside({ watch: true });
    rec.game.where = 1; rec.game.needsAnswer = 1; rec.poll();
    rec.q('#settings-btn-bar').click();
    expect('a recording\'s small form says what is true there: the recording plays', rec.q('.gm-note').textContent.indexOf('during a match') < 0 && /recording plays/.test(rec.q('.gm-note').textContent), true);
    rec.menu.close();
    const live = outside({ watch: true, live: true });
    live.game.where = 1; live.game.needsAnswer = 1; live.poll();
    live.q('#settings-btn-bar').click();
    expect('...and a live page\'s too', live.q('.gm-note').textContent.indexOf('during a match') < 0 && /match plays/.test(live.q('.gm-note').textContent), true);
    live.menu.close();
    const p = outside();
    p.q('#settings-btn').click();
    ok('the quiet line is tied to the dialog for a screen reader (aria-describedby names it)', !!p.q('.gm-note').id && p.q('.gm-panel').getAttribute('aria-describedby') === p.q('.gm-note').id);
    p.menu.close();
}

// ---- a key held past the close -------------------------------------------------------------------------------------------------------------------------------------------------
// A key whose press the panel kept is the panel's until its lift: the keyboard goes on repeating a key that is held after the panel has closed, and the game's window system takes a repeat it never saw the press of for a
// new press (Enter held on Close would press START on the setup screen). Its repeats, the text of its press and its lift stop at the document, also after the close; a fresh press ends it; nothing else is held back.
{
    const closers = {
        'Close': (p) => p.q('.gm-resume').click(),
        'the cross': (p) => p.q('.gm-x').click(),
        'Esc': (p) => p.doc.key('Escape'),
        'the close of the page (menu.close)': (p) => p.menu.close(),
    };
    const forms = { 'small form': () => outside(), 'full menu': () => makePage().ready() };
    Object.keys(forms).forEach((fname) => Object.keys(closers).forEach((cname) => {
        const at = fname + ', closed by ' + cname + ': ';
        const p = forms[fname]();
        if (fname === 'small form') p.q('#settings-btn').click(); else p.open();
        const down = p.doc.key('Enter');
        const text = p.doc.key('Enter', { type: 'keypress' });
        closers[cname](p);
        ok(at + 'the press of Enter never reached the game, nor its text', !down.reachedPage && !text.reachedPage);
        ok(at + 'the panel is gone', !p.menu.isOpen());
        const again = p.doc.key('Enter', { repeat: true });
        ok(at + 'a repeat of the held Enter after the close does not reach the game (it would be a new press for it), and is cancelled for the browser', !again.reachedPage && again.prevented);
        ok(at + '...nor does the text of a repeat', !p.doc.key('Enter', { type: 'keypress', repeat: true }).reachedPage);
        ok(at + '...nor do more repeats', !p.doc.key('Enter', { repeat: true }).reachedPage && !p.doc.key('Enter', { repeat: true }).reachedPage);
        ok(at + 'another key goes on to the game meanwhile (nothing else is held back)', p.doc.key('s').reachedPage && p.doc.key('s', { type: 'keypress' }).reachedPage && p.doc.key('s', { type: 'keyup' }).reachedPage);
        ok(at + 'the lift of the Enter is the panel\'s', !p.doc.key('Enter', { type: 'keyup' }).reachedPage);
        p.doc.key('Escape', { type: 'keyup' });
        expect(at + 'then no listener is left', [p.doc.count('keydown'), p.doc.count('keypress'), p.doc.count('keyup')], [0, 0, 0]);
        ok(at + 'and a new Enter is the game\'s again, with its text and its lift', p.doc.key('Enter').reachedPage && p.doc.key('Enter', { type: 'keypress' }).reachedPage && p.doc.key('Enter', { type: 'keyup' }).reachedPage);
    }));
}
{
    // Esc held past the full menu's close: the menu must not come back (the game's Esc would ask for it again)
    const p = makePage().ready();
    p.open();
    p.doc.key('Escape');
    ok('the full menu closed by Esc: a held Esc\'s repeats do not reach the game', !p.menu.isOpen() && !p.doc.key('Escape', { repeat: true }).reachedPage && !p.doc.key('Escape', { repeat: true }).reachedPage);
    ok('...its lift is the panel\'s, and the next Esc is the game\'s', !p.doc.key('Escape', { type: 'keyup' }).reachedPage && p.doc.key('Escape').reachedPage);
    // a lift that was lost (the window lost the focus): a fresh press of the key is not held back
    const q = makePage().ready();
    q.open();
    q.doc.key('Escape');
    ok('a lift that never came: a fresh Esc (no repeat) goes to the game', q.doc.key('Escape').reachedPage && q.doc.count('keydown') === 0);
    // the lift of a key that the panel did not keep goes on, in the small form too (a bare modifier, a key held from before)
    const r = outside();
    r.q('#settings-btn').click();
    r.doc.key('Shift');
    ok('a key that went down before the panel opened keeps its lift, and a repeat of it passes (the game saw it go down)', r.doc.key('w', { repeat: true }).reachedPage === false && r.doc.key('Shift', { type: 'keyup' }).reachedPage);
    r.menu.close();
}
{
    // Enter held on Leave game (full menu): the question does not flicker; its repeats are cancelled for the browser and never press again
    const p = makePage().ready();
    p.open();
    p.qa('.gm-leave')[0].focus();
    p.doc.key('Enter');
    p.qa('.gm-leave')[0].click();                                                       // (the browser's press of the focused button)
    p.q('.gm-stay').focus();
    const again = p.doc.key('Enter', { repeat: true });
    ok('Enter held on Leave game: the repeats are cancelled for the browser (no second press of the focused Stay) and do not reach the game', again.prevented && !again.reachedPage && !!p.q('.gm-ask') && p.game.calls.filter((c) => c[0] === 'do').length === 0);
    p.menu.close();
}
{
    // Space presses a focused button with the real game: the game cancels every lift, so the lift of a key the panel kept must not reach it (the browser presses at the lift)
    ['small form', 'full menu'].forEach((fname) => {
        const p = fname === 'small form' ? outside() : makePage().ready();
        if (fname === 'small form') p.q('#settings-btn').click(); else p.open();
        const down = p.doc.key(' ');
        const lift = p.doc.key(' ', { type: 'keyup' });
        ok(fname + ': Space is the panel\'s from press to lift, and its lift is not cancelled for the browser', !down.reachedPage && !lift.reachedPage && !lift.prevented);
        p.menu.close();
    });
}

// ---- a key that went down before the panel opened -----------------------------------------------------------------------------------------------------------------------------
// The game's window system, as a model: it keeps its own state of which keys are down, calls a press of a key that its state has down a REPEAT, and drops a lift of a key that its state has up. The game ignores repeats
// for Esc's request of the menu, START in a room, T, F2 / F3 and F9 - F12: a lift that never reaches it leaves the key down in its state, and the next press of that key does nothing.
function sdlModel(p) {
    const down = {}, log = [];
    const send = (key, type, extra) => {
        const e = p.doc.key(key, Object.assign({ type: type || 'keydown' }, extra || {}));
        if (e.reachedPage && e.type !== 'keypress') {
            if (e.type === 'keydown') { log.push(key + ':' + (down[e.code] ? 'repeat' : 'new')); down[e.code] = true; } else delete down[e.code];
        }
        return e;
    };
    return { send, down, log };
}
{
    [['full menu', 'Escape'], ['full menu', 'ArrowRight'], ['small form', 'Escape'], ['small form', 's'], ['small form', 'ArrowDown']].forEach(([form, key]) => {
        const at = form + ', ' + key + ' held from before the panel: ';
        const p = form === 'full menu' ? makePage().ready() : outside();
        const g = sdlModel(p);
        g.send(key);                                                                       // (the game saw it go down; there is no panel yet)
        if (form === 'full menu') p.open(); else p.q('#settings-btn').click();
        ok(at + 'the panel is up', p.menu.isOpen());
        const repeats = [1, 2, 3].map(() => g.send(key, 'keydown', { repeat: true }));
        ok(at + 'its repeats under the panel are the panel\'s (they are not the game\'s to act on)', repeats.every((e) => !e.reachedPage));
        const lift = g.send(key, 'keyup');
        ok(at + 'its lift reaches the game (the game saw the press: it must see the lift)', lift.reachedPage && !g.down[lift.code]);
        p.q('.gm-resume').click();
        g.send(key);
        expect(at + 'its next press arrives as a fresh press, not as a repeat of one that never ended', g.log.slice(-1), [key + ':new']);
        g.send(key, 'keyup');
        expect(at + 'and nothing is left listening', [p.doc.count('keydown'), p.doc.count('keypress'), p.doc.count('keyup')], [0, 0, 0]);
    });
    // a key that went down under the panel is the panel's, whatever its lift calls it (Shift changes `key` between a press and its lift, not `code`)
    const p = outside();
    p.q('#settings-btn').click();
    const down = p.doc.key('S', { code: 'KeyS', shiftKey: true });
    p.q('.gm-resume').click();
    ok('a press of Shift+S that the panel kept: the lift (as `s`, the same code) is the panel\'s too', !down.reachedPage && !p.doc.key('s', { type: 'keyup', code: 'KeyS' }).reachedPage);
    expect('...and then nothing is left listening', [p.doc.count('keydown'), p.doc.count('keypress'), p.doc.count('keyup')], [0, 0, 0]);
}
{
    // The holds stay when the panel opens again (a key is held until it is lifted, whatever the page does meanwhile): Enter held on Close, the panel opened again with the mouse, a repeat must not press the new panel's Close,
    // and its lift ends the hold
    ['small form', 'full menu'].forEach((form) => {
        const at = form + ', Enter held across a second open: ';
        const p = form === 'small form' ? outside() : makePage().ready();
        const g = sdlModel(p);
        p.q('#settings-btn').click();
        g.send('Enter');
        p.q('.gm-resume').click();
        p.q('#settings-btn').click();
        ok(at + 'the panel is up again', p.menu.isOpen());
        const again = g.send('Enter', 'keydown', { repeat: true });
        ok(at + 'a repeat is cancelled for the browser (it would press the new Close) and does not reach the game', again.prevented && !again.reachedPage && p.menu.isOpen());
        ok(at + 'the lift ends the hold and is the panel\'s', !g.send('Enter', 'keyup').reachedPage);
        p.q('.gm-resume').click();
        expect(at + 'then nothing is left listening and a new Enter is the game\'s', [p.doc.count('keydown'), p.doc.count('keypress'), p.doc.count('keyup'), g.send('Enter').reachedPage], [0, 0, 0, true]);
    });
}

// ---- the gear by keyboard ----------------------------------------------------------------------------------------------------------------------------------------------------
{
    // the game's window system takes a focused button's Enter (START on the setup screen) and cancels its press: the gear presses itself
    ['Enter', ' '].forEach((k) => {
        const p = outside();
        const b = p.q('#settings-btn');
        b.focus();
        const e = b.fire('keydown', { key: k, code: p.doc.codeOf(k), repeat: false });
        ok('a focused gear opens the panel on ' + (k === ' ' ? 'Space' : 'Enter') + ', and the key does not reach the game', p.menu.isOpen() && e.prevented && e.stopped);
        const q = outside();
        q.q('#settings-btn').fire('keydown', { key: k, code: p.doc.codeOf(k), repeat: false });
        q.qa('.gm-resume')[0].click();
        ok('...the key is still held when it closes: its repeats are the panel\'s (they would press the game\'s START)', !q.doc.key(k, { repeat: true }).reachedPage && !q.doc.key(k, { type: 'keypress', repeat: true }).reachedPage);
        ok('...so is the lift', !q.doc.key(k, { type: 'keyup' }).reachedPage && q.doc.count('keydown') === 0);
        ok('...and the lift of the key that opened it is the panel\'s while it is up', !p.doc.key(k, { type: 'keyup' }).reachedPage);
        p.qa('.gm-resume')[0].click();
        ok('...closed, the focus goes back to the gear (a key opened it)', p.doc.activeElement === b);
        p.doc.key(k, { type: 'keyup' });
        expect('...and nothing is left listening', [p.doc.count('keydown'), p.doc.count('keypress'), p.doc.count('keyup')], [0, 0, 0]);
    });
    const p = outside();
    const b = p.q('#settings-btn');
    const rep = b.fire('keydown', { key: 'Enter', code: p.doc.codeOf('Enter'), repeat: true });
    ok('a repeat on the gear opens nothing', !p.menu.isOpen() && rep.prevented);
    const tab = b.fire('keydown', { key: 'Tab', code: p.doc.codeOf('Tab') });
    ok('another key on the gear is left alone', !tab.prevented && !p.menu.isOpen());
    b.disabled = true;
    b.fire('keydown', { key: 'Enter', code: p.doc.codeOf('Enter') });
    ok('a gear that is off opens nothing', !p.menu.isOpen());
    const n = makePage().ready();
    n.game.needsAnswer = 1;
    n.poll();
    n.q('#settings-btn').fire('keydown', { key: 'Enter', code: p.doc.codeOf('Enter') });
    ok('under a dialog the gear\'s Enter opens the small form', n.menu.isOpen() && n.qa('[role=tab]').length === 0);
    n.menu.close();
    const m = makePage().ready();
    m.q('#settings-btn').fire('keydown', { key: ' ', code: p.doc.codeOf(' ') });
    ok('in a match the gear\'s Space opens the full menu', m.menu.isOpen() && m.qa('[role=tab]').length === 4);
    m.menu.close();
}

// ---- Leave game ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const kinds = [[0, 'Leave the game?', 'This match ends. Your game is not saved.', 'Leave game'], [1, 'Leave the game?', 'You will drop out. Your ants are lost, the others play on, and you cannot come back to this match.', 'Leave game'],
                   [2, 'Forfeit and leave?', 'You give up this match. It ends now for everybody and counts as a loss for you.', 'Forfeit'],
                   [3, 'Leave the game?', 'The match is held. You leave it for good and cannot come back to this match.', 'Leave game']];
    kinds.forEach((k) => {
        const p = makePage().ready();
        p.game.leave = k[0];
        p.state.locked = true;
        p.open();
        p.qa('.gm-leave')[0].click();
        const card = p.q('.gm-ask');
        ok('Leave game asks first (kind ' + k[0] + ')', card !== null && p.menu.isOpen() && p.game.calls.filter((c) => c[0] === 'do').length === 0);
        expect('...in the words of what it will do', [p.q('.gm-ask-t').textContent, p.q('.gm-ask-b').textContent, p.qa('.gm-ask-btns button').map((b) => b.textContent)], [k[1], k[2], ['Stay in the game', k[3]]]);
        expect('...with the note that the game runs on', p.q('.gm-ask-n').textContent, 'The game keeps running while you decide.');
        expect('...a modal alert dialog that names its title and its text', [card.getAttribute('role'), card.getAttribute('aria-modal'), card.getAttribute('aria-labelledby'), card.getAttribute('aria-describedby')], ['alertdialog', 'true', 'gm-ask-t', 'gm-ask-b']);
        expect('the default is Stay in the game', p.doc.activeElement.textContent, 'Stay in the game');
        let e = p.doc.key('Tab');
        ok('Tab goes on to the other button', !e.prevented);
        p.doc.activeElement.focus();
        p.qa('.gm-ask-btns button')[1].focus();
        e = p.doc.key('Tab');
        ok('...and wraps inside the card, not out of it', e.prevented && p.doc.activeElement.textContent === 'Stay in the game');
        e = p.doc.key('Escape');
        ok('Esc in the question means stay: the question goes, the panel stays', p.q('.gm-ask') === null && p.menu.isOpen() && e.prevented && p.doc.activeElement.textContent === 'Leave game');
        p.qa('.gm-leave')[0].click();
        p.qa('.gm-ask-btns button')[0].click();
        ok('Stay in the game goes back to the panel', p.q('.gm-ask') === null && p.menu.isOpen() && p.game.calls.filter((c) => c[0] === 'do').length === 0);
        p.qa('.gm-leave')[0].click();
        p.game.calls.length = 0;
        p.qa('.gm-ask-btns button')[1].click();
        expect('the button of the question leaves: the panel closes, the game is told, then it leaves (act 1)', [p.menu.isOpen(), p.game.calls], [false, [['open', 0], ['do', 1]]]);
        expect('...and no lock is asked for on the way out', p.state.locks.filter((x) => x === 'request'), []);
    });
    const watch = makePage({ watch: true }).ready();
    watch.game.leave = 4;
    watch.open();
    watch.qa('.gm-leave')[0].click();
    expect('a recording asks nothing either', [watch.menu.isOpen(), watch.game.calls.filter((c) => c[0] === 'do')], [false, [['do', 1]]]);
    // the question follows what Leave game would do now
    const p = makePage().ready();
    p.game.leave = 1;
    p.open();
    p.qa('.gm-leave')[0].click();
    p.game.leave = 2;
    p.poll();
    expect('an opponent that leaves while the question is up turns it into a forfeit', [p.q('.gm-ask-t').textContent, p.qa('.gm-ask-btns button')[1].textContent], ['Forfeit and leave?', 'Forfeit']);
    // B1: a question that is up never leaves by itself: a match that becomes held (a seat's link dropped) changes what Leave game would do, not what the player decided
    p.game.calls.length = 0;
    p.game.leave = 3;
    p.poll();
    p.poll();
    expect('a match that becomes held while the question is up: it is drawn again in the words of the held match, and nothing is sent', [p.menu.isOpen(), p.q('.gm-ask-b').textContent, p.game.calls.filter((c) => c[0] === 'do')], [true, 'The match is held. You leave it for good and cannot come back to this match.', []]);
    p.game.leave = 4;
    p.poll();
    p.poll();
    expect('a kind that asks nothing (a table\'s word) leaves the question as it is, and still nothing is sent', [p.menu.isOpen(), p.q('.gm-ask-b').textContent.slice(0, 9), p.game.calls.filter((c) => c[0] === 'do')], [true, 'The match', []]);
    p.game.leave = 1;
    p.poll();
    expect('...and a kind that asks draws it again, still with nothing sent', [p.q('.gm-ask-t').textContent, p.game.calls.filter((c) => c[0] === 'do')], ['Leave the game?', []]);
    p.game.leave = 3;
    p.poll();
    p.qa('.gm-ask-btns button')[1].click();
    expect('only the button leaves', [p.menu.isOpen(), p.game.calls.filter((c) => c[0] === 'do')], [false, [['do', 1]]]);
    // while the question is up the rest of the panel waits for the answer (not inert for a screen reader otherwise)
    const w = makePage().ready();
    w.open();
    w.qa('.gm-leave')[0].click();
    ok('the rest of the panel is inert while the question is up', ['.gm-head', '.gm-tabs', '.gm-pane', '.gm-note', '.gm-btns'].every((sel) => w.q(sel).hasAttribute('inert')) && !w.q('.gm-ask').hasAttribute('inert'));
    w.qa('.gm-ask-btns button')[0].click();
    ok('...and live again when it is answered', ['.gm-head', '.gm-tabs', '.gm-pane', '.gm-note', '.gm-btns'].every((sel) => !w.q(sel).hasAttribute('inert')) && w.doc.activeElement === w.q('.gm-leave'));
}

// ---- the style -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const p = makePage();
    p.doc.links[0].onerror();
    p.doc.links.forEach((l) => { l.onerror = null; });
    const timers = [];
    const real = global.setTimeout;
    global.setTimeout = (fn, ms) => { timers.push({ fn, ms }); return timers.length; };
    try {
        const q = makePage();
        q.doc.links[0].onerror();
        expect('a style that fails is fetched once more after two seconds', timers.map((t) => t.ms), [2000]);
        timers[0].fn();
        ok('...with a new link', q.doc.links.length === 2);
        q.doc.links[1].onerror();
        expect('...and the second failure is said in the toast for a while', [q.toast.textContent, q.toast.hidden, timers.map((t) => t.ms)], ['The settings menu could not load its style. Press Reset to try again.', false, [2000, 5000]]);
        timers[1].fn();
        ok('...and it goes again', q.toast.hidden);
    } finally { global.setTimeout = real; }
}

console.log(checks + ' checks, ' + failed + ' failures');
process.exit(failed === 0 ? 0 : 1);
