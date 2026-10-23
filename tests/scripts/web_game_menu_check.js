// Runs the page's OWN code for the settings menu (web/game_menu.js, AntsGameMenu.forPage, which web/shell.html calls once the game runs), without a browser, on a strict fake of the DOM that is written here
// (a node that throws when the script reads or writes what it does not know, and when it writes markup: every text must go in with textContent) and a model of the game's exports
// (Module._ants_menu_describe / get / set / do / open: the rows are tests/data/game_menu_match.json and game_menu_watch.json, which tests/test_app/test_game_menu.cpp keeps equal to what the game says).
//   * the rows: every row of the table is drawn once, in its section, with its label, its hint and its NEW tag, and a row that is not in the table is not drawn; a recording has no Game section; the Controls list
//     is the view-only list of a computer or of a touch screen; the page's own rows (Fullscreen, Picture shape, Fullscreen mouse) use the page's functions and buttons;
//   * the values: a slider shows the game's value and changes it (Master volume while it is dragged, the others at the release), a switch and a choice change it, a button does its act, and a change is read back;
//   * the way in and out: the buttons (header, under the picture, over the picture in fullscreen, the replay's bar) show only when the game runs and the style is there and are off when a dialog needs its answer;
//     Esc's request opens it; it opens inside the stage, the page behind it is inert, the focus goes to Resume, Tab stays inside, Esc closes it (once: a held key does nothing), a click on the dimmed area does not,
//     the focus goes back to the canvas, the game is told (and the keys never reach the page's other listeners); the pointer lock is let go and asked for again; a dialog that needs its answer closes it;
//   * Leave game: asks in the words of what it will do (a drop out, a forfeit, the end of a game), Esc and Stay stay, a held match and a recording ask nothing, "Back to the matches" in a recording;
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
        }
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
    // a key goes to the document's capture listeners (the panel listens there), and to the active element's own only when no capture listener stopped it
    doc.key = (key, extra) => {
        const e = Object.assign({ type: 'keydown', key, shiftKey: false, repeat: false, target: doc.active, prevented: false, stopped: false, preventDefault() { this.prevented = true; }, stopPropagation() { this.stopped = true; } }, extra || {});
        for (const l of (doc.listeners[e.type] || []).filter((x) => x.capture).slice()) l.fn(e);
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
        values: { 1: 100, 2: 65, 3: 99, 4: 0, 8: 1, 9: 50, 10: 1, 11: 1 },
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
    const game = makeGame(opts.watch ? WATCH : MATCH);
    if (opts.watch) { game.where = 1; game.leave = 4; }
    const make = (tag, id, parent, cls) => { const n = doc.createElement(tag); if (id) n.setAttribute('id', id); if (cls) n.className = cls; (parent || doc.body).appendChild(n); return n; };
    const header = make('header'), menuBtn = make('a', 'menu-btn', header, 'btn sm');
    const stage = make('div', 'game-stage'), box = make('div', 'game-container', stage), canvas = make('canvas', 'canvas', box), chat = make('input', 'chat-keys', box);
    stage.setAttribute('data-aspect', '16:9');
    const bar = make('div', 'view-bar'), label = make('span', 'view-bar-label', bar);
    const shapes = {};
    ['4:3', '16:10', '16:9', '21:9'].forEach((s) => {
        const b = make('button', 'aspect-' + s.replace(':', '-'), bar);
        b.setAttribute('data-aspect', s);
        b.setAttribute('aria-checked', s === '16:9' ? 'true' : 'false');
        b.addEventListener('click', () => { shapes.clicked = (shapes.clicked || []).concat(s); stage.setAttribute('data-aspect', s); });
        shapes[s] = b;
    });
    const lockOn = make('button', opts.noLock ? 'lock-x' : 'lock-on', bar), lockOff = make('button', opts.noLock ? 'lock-y' : 'lock-off', bar);
    lockOn.setAttribute('aria-checked', 'true');
    lockOff.setAttribute('aria-checked', 'false');
    lockOn.addEventListener('click', () => { lockOn.setAttribute('aria-checked', 'true'); lockOff.setAttribute('aria-checked', 'false'); game.calls.push(['lock', 'on']); });
    lockOff.addEventListener('click', () => { lockOn.setAttribute('aria-checked', 'false'); lockOff.setAttribute('aria-checked', 'true'); game.calls.push(['lock', 'off']); });
    const rbar = make('div', 'rbar'), picwrap = make('div', 'picwrap', rbar);
    const toast = make('div', 'toast');
    toast.hidden = true;
    const intervals = [], state = { fullscreen: false, locked: false, ready: true, locks: [], enters: 0, leaves: 0 };
    const page = {
        doc, win, stage, canvas, toast, module: () => (opts.noModule ? null : game.module), ready: () => state.ready, replay: !!opts.watch, live: !!opts.live, version: 'v0.12.111', query: '?v=7',
        isFullscreen: () => state.fullscreen, enter: () => { state.enters++; state.fullscreen = true; }, leave: () => { state.leaves++; state.fullscreen = false; },
        isLocked: () => state.locked, releaseLock: () => { state.locks.push('release'); state.locked = false; }, requestLock: () => { state.locks.push('request'); state.locked = true; },
        every: (fn, ms) => { intervals.push({ fn, ms }); return intervals.length; },
    };
    const menu = Menu.forPage(page);
    const p = { doc, win, game, state, page, menu, stage, canvas, header, menuBtn, bar, picwrap, rbar, toast, chat, shapes, lockOn, lockOff, intervals, box };
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
    expect('a button in the header, one over the picture for fullscreen (in the stage), one under it', all.map((b) => b.id), ['settings-btn', 'settings-btn-fs', 'settings-btn-narrow']);
    expect('the header button stands right after Menu', p.header.children.map((c) => c.id), ['menu-btn', 'settings-btn']);
    expect('the narrow one is the first thing of the bar under the picture', p.bar.children[0].id, 'settings-btn-narrow');
    ok('the fullscreen one is a child of the stage (it goes fullscreen with it)', p.stage.contains(p.q('#settings-btn-fs')) && p.q('#settings-btn-fs').parentNode === p.stage);
    ok('all hide until the style is there', all.every((b) => b.hidden && b.style.display === 'none'));
    ok('every one is a real button with the page\'s look and the word Settings', all.every((b) => b.tagName === 'button' && b.type === 'button' && /(^| )btn( |$)/.test(b.className) && b.textContent === 'Settings' && b.getAttribute('aria-haspopup') === 'dialog'));
    p.styleLoaded();
    p.poll();
    expect('with the style and a running game the header and the narrow button show', [p.q('#settings-btn').hidden, p.q('#settings-btn-narrow').hidden], [false, false]);
    ok('the fullscreen one stays hidden outside fullscreen', p.q('#settings-btn-fs').hidden && p.q('#settings-btn-fs').style.display === 'none');
    p.state.fullscreen = true;
    p.poll();
    ok('...and shows in fullscreen', !p.q('#settings-btn-fs').hidden && p.q('#settings-btn-fs').style.display === '');
    p.state.fullscreen = false;
    p.state.ready = false;
    p.poll();
    ok('none shows before the game runs', all.every((b) => b.hidden));
    p.state.ready = true;
    p.game.needsAnswer = 1;
    p.poll();
    ok('a dialog that needs its answer turns them off (and says so)', all.filter((b) => !b.hidden).every((b) => b.disabled && b.title === 'Not now'));
    p.game.needsAnswer = 0;
    p.game.where = -1;
    p.poll();
    ok('so does a screen that is not a match', all.filter((b) => !b.hidden).every((b) => b.disabled));
    p.game.where = 0;
    p.poll();
    ok('and they are on again', all.filter((b) => !b.hidden).every((b) => !b.disabled));
    expect('the poll is 250 ms', p.intervals.map((i) => i.ms), [250]);
    expect('...once', p.intervals.length, 1);
    p.game.where = -1;
    p.poll();
    ok('a click on a button that is off opens nothing', !p.open() && !p.menu.isOpen());
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
    ok('and none in the header, under the picture or in the stage', !p.q('#settings-btn') && !p.q('#settings-btn-narrow') && !p.q('#settings-btn-fs'));
}
{
    const p = makePage({ noModule: true }).ready();
    ok('a game that is not there: the buttons are off, Esc opens nothing', p.qa('.gm-btn').every((b) => b.disabled) && !p.open());
}
{
    const p = makePage();
    p.state.ready = true;
    p.poll();
    ok('without the style the buttons stay hidden and Esc opens nothing', p.qa('.gm-btn').every((b) => b.hidden) && !p.open());
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
    const p = makePage({ noLock: true }).ready();
    p.open();
    p.tab('video');
    ok('a page without the lock buttons has no Fullscreen mouse row', p.rows().map((r) => r.name).indexOf('Fullscreen mouse') < 0);
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
    expect('the Video section: the picture\'s four shapes, the mouse, the numbers', video.map((g) => g.children.map((b) => b.textContent)), [['Classic 4:3', '16:10', '16:9', '21:9'], ['Locked', 'Free'], ['On', 'Off']]);
    expect('the shape that the page has is the chosen one', video[0].children.map((b) => b.getAttribute('aria-checked')), ['false', 'false', 'true', 'false']);
    video[0].children[3].click();
    expect('a shape is the page\'s own button, clicked (the selector, the address and the stored choice stay one function)', [p.shapes.clicked, p.stage.getAttribute('data-aspect'), p.game.calls.length], [['21:9'], '21:9', 0]);
    expect('...and the choice follows it', video[0].children.map((b) => b.getAttribute('aria-checked')), ['false', 'false', 'false', 'true']);
    expect('the mouse starts Locked', video[1].children.map((b) => b.getAttribute('aria-checked')), ['true', 'false']);
    video[1].children[1].click();
    expect('Free is the page\'s Free button', [p.game.calls, p.lockOff.getAttribute('aria-checked')], [[['lock', 'off']], 'true']);
    video[2].children[1].click();
    expect('the hit point numbers are row 8 (value 0 is Off)', p.game.calls[1], ['set', 8, 0]);
    const scroll = p.qa('.gm-panel input')[0];
    scroll.value = '12';
    scroll.fire('change', {});
    expect('the scroll speed is row 9', p.game.calls[2], ['set', 9, 12]);

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
    const scrim = p.open();
    expect('the game is told, and the pointer lock is let go', [p.game.calls[0], p.state.locks], [['open', 1], ['release']]);
    expect('the page behind it is inert: the header, the bar, the picture\'s box, the toast', [p.header.hasAttribute('inert'), p.bar.hasAttribute('inert'), p.box.hasAttribute('inert'), p.rbar.hasAttribute('inert'), p.toast.hasAttribute('inert')], [true, true, true, true, true]);
    ok('the stage is not inert (the panel is in it) and the panel is not', !p.stage.hasAttribute('inert') && !scrim.hasAttribute('inert'));
    expect('the focus is on Resume', p.doc.activeElement.textContent, 'Resume');
    expect('the lifts of keys are left to the game (it would think a key still held, and read the next press as a repeat)', p.doc.count('keyup'), 0);
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
    p.qa('[role=tab]')[0].focus();
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
    expect('the page is live again', [p.header.hasAttribute('inert'), p.bar.hasAttribute('inert'), p.box.hasAttribute('inert'), p.toast.hasAttribute('inert')], [false, false, false, false]);
    ok('the focus is on the canvas', p.doc.activeElement === p.canvas);
    expect('no listener is left on the document or the window', [p.doc.count('keydown'), p.doc.count('keyup'), p.win.count('pointerlockchange'), p.doc.count('fullscreenchange')], [0, 0, 0, 0]);
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

// ---- Leave game ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const kinds = [[0, 'Leave the game?', 'This match ends. Your game is not saved.', 'Leave game'], [1, 'Leave the game?', 'You will drop out. Your ants are lost, the others play on, and you cannot come back to this match.', 'Leave game'],
                   [2, 'Forfeit and leave?', 'You give up this match. It ends now for everybody and counts as a loss for you.', 'Forfeit']];
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
    const held = makePage().ready();
    held.game.leave = 3;
    held.open();
    held.qa('.gm-leave')[0].click();
    expect('a match that is held asks nothing when Leave game is clicked: the player leaves for good (the quit dialog\'s Yes does the same)', [held.menu.isOpen(), held.q('.gm-ask'), held.game.calls.filter((c) => c[0] === 'do')], [false, null, [['do', 1]]]);
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
    expect('a match that becomes held while the question is up: the question stays as it was and nothing is sent', [p.menu.isOpen(), p.q('.gm-ask-t').textContent, p.game.calls.filter((c) => c[0] === 'do')], [true, 'Forfeit and leave?', []]);
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
