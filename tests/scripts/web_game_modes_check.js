// Runs the page's OWN code for the lobby's game types, without a browser: the rows of front/game_modes.js and its pure functions (the mode a new room starts with, the tag, the guest's line, the
// unknown mode, the remembered choice with storage that is missing or refuses) and the control of front/game_type.js on a small fake of the DOM (the closed field, the list, the keys, the guest's
// line, a row added as data). tests/scripts/test_web_game_type.py runs this with node (the quick tier) and changes one thing at a time in a COPY of the scripts: the check must then fail.
// usage: node web_game_modes_check.js web/front/game_modes.js web/front/game_type.js     (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');
const M = require(path.resolve(process.argv[2]));
const T = require(path.resolve(process.argv[3]));

let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }

// ---- the rows ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
same('two modes today, by number and word', M.MODES.map((m) => [m.id, m.word]), [[0, 'highest-score'], [1, '187']]);
same('the names', M.MODES.map((m) => m.name), ['Highest score', '187']);
same('the lines', M.MODES.map((m) => m.line), ['The original game. Food and power-ups; the highest score wins.', 'A fight with no food. Kills decide.']);
same('the tags: none for the default', M.MODES.map((m) => m.tag), ['', '187']);
same('the notes: only 187 has one', M.MODES.map((m) => m.note), ['', 'No food and no power-ups. The most kills wins.']);
same('exactly one default, the one with no tag', M.MODES.filter((m) => m.def).map((m) => m.word), ['highest-score']);
check('ids and words are unique', new Set(M.MODES.map((m) => m.id)).size === M.MODES.length && new Set(M.MODES.map((m) => m.word)).size === M.MODES.length);
check('every word is a word of the server (POST /rooms)', M.MODES.every((m) => ['highest-score', '187'].indexOf(m.word) !== -1));
check('no row warns about computer players', !/bot|computer/i.test(JSON.stringify(M.MODES)));
same('byId', [M.byId(0).word, M.byId(1).word, M.byId(2), M.byId(-1), M.byId('1')], ['highest-score', '187', null, null, null]);
same('byWord', [M.byWord('187').id, M.byWord('highest-score').id, M.byWord('1'), M.byWord(''), M.byWord(null), M.byWord(undefined), M.byWord('Highest score')], [1, 0, null, null, null, null, null]);

// ---- what a new room starts with ----------------------------------------------------------------------------------------------------------------------------------------------------------
same('nothing remembered: the default', M.initialOf(null).word, 'highest-score');
same('187 remembered', M.initialOf('187').word, '187');
same('highest-score remembered', M.initialOf('highest-score').word, 'highest-score');
same('an unknown remembered word: the default', [M.initialOf('no-such-mode').word, M.initialOf('').word, M.initialOf(7).word, M.initialOf(undefined).word, M.initialOf('__proto__').word], ['highest-score', 'highest-score', 'highest-score', 'highest-score', 'highest-score']);

// ---- the words of a mode, known and unknown -------------------------------------------------------------------------------------------------------------------------------------------------------
same('view of 0', M.viewOf(0), { known: true, id: 0, name: 'Highest score', tag: '', line: 'The original game. Food and power-ups; the highest score wins.', note: '' });
same('view of 1', M.viewOf(1), { known: true, id: 1, name: '187', tag: '187', line: 'A fight with no food. Kills decide.', note: 'No food and no power-ups. The most kills wins.' });
same('view of an unknown mode', M.viewOf(5), { known: false, id: 5, name: 'New mode', tag: 'New mode', line: '', note: '' });
same('an unknown mode with the server\'s name', [M.viewOf(5, 'Brawl').name, M.viewOf(5, 'Brawl').tag, M.viewOf(5, '').name, M.viewOf(5, 3).name], ['Brawl', 'New mode', 'New mode', 'New mode']);
same('a known mode ignores the server\'s name', M.viewOf(1, 'Other').name, '187');
same('the tags', [M.tagOf(0), M.tagOf(1), M.tagOf(9), M.tagOf(undefined), M.tagOf(-3)], ['', '187', 'New mode', 'New mode', 'New mode']);
same('the guest line', [M.guestLine(0), M.guestLine(1), M.guestLine(4), M.guestLine(4, 'Brawl')], ['Game type: Highest score', 'Game type: 187', 'Game type: New mode', 'Game type: Brawl']);

// ---- the remembered choice ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
function store(initial) { const d = Object.assign({}, initial); return { d, getItem: (k) => (k in d ? d[k] : null), setItem: (k, v) => { d[k] = String(v); } }; }
const refuses = { getItem() { throw new Error('refused'); }, setItem() { throw new Error('refused'); } };
let s = store();
same('remember keeps the word', [M.remember(s, 1), s.d[M.STORAGE_KEY]], [true, '187']);
same('recall gives it back', M.recall(s), '187');
same('remember of an unknown number keeps nothing', [M.remember(s, 9), s.d[M.STORAGE_KEY]], [false, '187']);
same('with no storage', [M.recall(null), M.remember(null, 1), M.wanted(null, 0)], [null, false, -1]);
same('storage that refuses: no throw, the default', [M.recall(refuses), M.remember(refuses, 1), M.wanted(refuses, 0), M.wanted(refuses, 1)], [null, false, -1, 0]);
same('wanted: the room has it already', [M.wanted(store({ [M.STORAGE_KEY]: '187' }), 1), M.wanted(store(), 0)], [-1, -1]);
same('wanted: the room is on another mode', [M.wanted(store({ [M.STORAGE_KEY]: '187' }), 0), M.wanted(store({ [M.STORAGE_KEY]: 'x' }), 1), M.wanted(store(), 1)], [1, 0, 0]);
same('wanted: a room on a mode that is not known is brought to the remembered one', M.wanted(store({ [M.STORAGE_KEY]: '187' }), 9), 1);
same('the key', M.STORAGE_KEY, 'ants.lobby.gameType');

// ---- the control on a small fake of the DOM ---------------------------------------------------------------------------------------------------------------------------------------------
function makeDoc() {
    const byId = {}, docHandlers = {};
    function Node(tag) {
        this.tag = tag; this.attrs = {}; this.children = []; this.parent = null; this.handlers = {}; this._text = ''; this.hidden = false; this.className = ''; this.id = '';
        const self = this;
        const classes = () => self.className.split(/\s+/).filter(Boolean);
        this.classList = {
            add(c) { if (classes().indexOf(c) === -1) self.className = classes().concat(c).join(' '); },
            remove(c) { self.className = classes().filter((x) => x !== c).join(' '); },
            toggle(c, on) { if (on) this.add(c); else this.remove(c); },
            contains(c) { return classes().indexOf(c) !== -1; }
        };
    }
    Node.prototype.setAttribute = function (k, v) { this.attrs[k] = String(v); if (k === 'id') this.id = String(v); };
    Node.prototype.getAttribute = function (k) { return k === 'id' ? this.id : (k in this.attrs ? this.attrs[k] : null); };
    Node.prototype.removeAttribute = function (k) { delete this.attrs[k]; };
    Node.prototype.appendChild = function (c) { c.parent = this; this.children.push(c); return c; };
    Node.prototype.addEventListener = function (t, f) { (this.handlers[t] = this.handlers[t] || []).push(f); };
    Node.prototype.contains = function (n) { for (; n; n = n.parent) if (n === this) return true; return false; };
    Node.prototype.scrollIntoView = function () { this.scrolled = (this.scrolled || 0) + 1; };
    Object.defineProperty(Node.prototype, 'textContent', {
        get() { return this.tag === '#text' ? this._text : this.children.length ? this.children.map((c) => c.textContent).join('') : this._text; },
        set(v) { this.children = []; this._text = String(v); }
    });
    Object.defineProperty(Node.prototype, 'lastChild', { get() { return this.children[this.children.length - 1]; } });
    const doc = {
        createElement(tag) {
            const n = new Node(tag);
            const set = Object.getOwnPropertyDescriptor(Node.prototype, 'id');
            void set;
            return n;
        },
        createTextNode(t) { const n = new Node('#text'); n._text = t; return n; },
        addEventListener(t, f) { (docHandlers[t] = docHandlers[t] || []).push(f); },
        find(id) { return find(doc.root, id); },
        pointerdown(target) { (docHandlers.pointerdown || []).forEach((f) => f({ target })); }
    };
    function find(n, id) { if (n.id === id) return n; for (const c of n.children) { const r = find(c, id); if (r) return r; } return null; }
    void byId;
    return doc;
}
function key(node, k, mods) {
    let prevented = false;
    (node.handlers.keydown || []).forEach((f) => f(Object.assign({ key: k, preventDefault() { prevented = true; } }, mods)));
    return prevented;
}
function click(node) { (node.handlers.click || []).forEach((f) => f({})); }
function all(n, test, out) { out = out || []; if (test(n)) out.push(n); n.children.forEach((c) => all(c, test, out)); return out; }
function build(modes, extra) {
    const doc = makeDoc();
    const host = doc.createElement('div');
    doc.root = host;
    const chosen = [];
    const timers = [];
    const c = T.create(Object.assign({ doc, host, modes, onChoose: (id) => chosen.push(id), setTimeout: (f) => { timers.push(f); return timers.length; }, clearTimeout: () => {} }, extra || {}));
    const q = (cls) => all(host, (n) => n.className.split(/\s+/).indexOf(cls) !== -1)[0];
    return { doc, host, c, chosen, timers, q, combo: doc.find('gt-combo'), list: doc.find('gt-list'), opt: (i) => doc.find('gt-opt-' + i) };
}

{
    const u = build(M);
    check('the field starts disabled until the page paints', u.combo.getAttribute('aria-disabled') === 'true');
    u.c.paint({ id: 0, guest: false, disabled: false });
    check('the field is a combobox with a listbox, closed', u.combo.getAttribute('role') === 'combobox' && u.combo.getAttribute('aria-haspopup') === 'listbox' && u.combo.getAttribute('aria-expanded') === 'false' && u.list.getAttribute('role') === 'listbox');
    same('the closed field: name, line, no note', [u.q('gt-name').textContent, u.q('gt-line').textContent, u.q('gt-line').hidden, u.q('gt-note').hidden], ['Highest score', M.MODES[0].line, false, true]);
    check('the guest line is hidden for the leader and the field is shown', u.q('gt-guest').hidden && u.q('gt-gline').hidden && !u.q('gt-field').hidden && !u.q('gt-label').hidden);
    same('one option for each row, the chosen one aria-selected', [u.opt(0).getAttribute('role'), u.opt(0).getAttribute('aria-selected'), u.opt(1).getAttribute('aria-selected')], ['option', 'true', 'false']);
    same('the option text', [u.opt(1).textContent], ['187A fight with no food. Kills decide.']);
    check('the list starts closed', u.q('gt-pop').hidden);
    key(u.combo, 'ArrowDown');
    same('Down opens, on the chosen row', [u.c.isOpen(), u.q('gt-pop').hidden, u.combo.getAttribute('aria-expanded'), u.combo.getAttribute('aria-activedescendant')], [true, false, 'true', 'gt-opt-0']);
    check('the chosen row is scrolled into view', u.opt(0).scrolled >= 1);
    key(u.combo, 'ArrowDown'); same('Down: the next row', u.combo.getAttribute('aria-activedescendant'), 'gt-opt-1');
    key(u.combo, 'ArrowDown'); same('Down at the end stays', u.combo.getAttribute('aria-activedescendant'), 'gt-opt-1');
    key(u.combo, 'ArrowUp'); same('Up', u.combo.getAttribute('aria-activedescendant'), 'gt-opt-0');
    key(u.combo, 'End'); same('End', u.combo.getAttribute('aria-activedescendant'), 'gt-opt-1');
    key(u.combo, 'Home'); same('Home', u.combo.getAttribute('aria-activedescendant'), 'gt-opt-0');
    key(u.combo, 'End');
    key(u.combo, 'Escape');
    same('Escape closes and chooses nothing', [u.c.isOpen(), u.chosen, u.combo.getAttribute('aria-activedescendant')], [false, [], null]);
    key(u.combo, 'Enter'); key(u.combo, 'End'); key(u.combo, 'Enter');
    same('Enter opens, End, Enter chooses 187 and closes', [u.c.isOpen(), u.chosen], [false, [1]]);
    key(u.combo, ' '); check('Space opens', u.c.isOpen()); key(u.combo, ' ');
    same('Space on the chosen row chooses nothing new', [u.c.isOpen(), u.chosen], [false, [1]]);
    click(u.combo); check('a click opens', u.c.isOpen());
    click(u.combo); check('a second click closes', !u.c.isOpen());
    click(u.combo); click(u.opt(1)); same('a click on a row chooses it and closes', [u.c.isOpen(), u.chosen], [false, [1, 1]]);
    u.c.paint({ id: 1, guest: false, disabled: false });
    same('painted on 187: the name, the line, the note, the mark', [u.q('gt-name').textContent, u.q('gt-line').textContent, u.q('gt-note').hidden, u.q('gt-note').textContent, u.opt(1).getAttribute('aria-selected'), u.opt(0).getAttribute('aria-selected')], ['187', M.MODES[1].line, false, M.MODES[1].note, 'true', 'false']);
    click(u.combo); click(u.opt(0)); same('a click on the other row', u.chosen, [1, 1, 0]);
    click(u.combo); u.doc.pointerdown(u.doc.root); same('a click on the page outside closes', [u.c.isOpen(), u.chosen.length], [false, 3]);
    click(u.combo); u.doc.pointerdown(u.opt(0)); check('a click inside the list does not close it', u.c.isOpen());
    u.c.close(); u.c.paint({ id: 0, guest: false, disabled: false }); u.chosen.length = 0;
    click(u.combo); key(u.combo, 'End'); key(u.combo, 'Tab');
    same('Tab takes the option under the cursor and closes (the focus goes on: Tab is not prevented)', [u.c.isOpen(), u.chosen[u.chosen.length - 1], key(u.combo, 'Tab')], [false, 1, false]);
    u.c.paint({ id: 0, guest: false, disabled: false }); u.chosen.length = 0;
    click(u.combo); u.combo.handlers.blur.forEach((f) => f({ relatedTarget: u.opt(0) }));
    check('a focus moved into the control itself does not close it', u.c.isOpen());
    u.combo.handlers.blur.forEach((f) => f({ relatedTarget: null }));
    check('a blur with no target (a click on the page) is left to the pointer rule', u.c.isOpen());
    u.combo.handlers.blur.forEach((f) => f({ relatedTarget: { parent: null } }));
    same('the focus moved elsewhere by a script closes the list and chooses nothing', [u.c.isOpen(), u.chosen.length], [false, 0]);
    check('the field is named by the label only', u.combo.getAttribute('aria-labelledby') === 'gt-label');
    {   // paint is a no-op when [id, guest, disabled, serverName] is unchanged: an open list keeps its row
        click(u.combo); key(u.combo, 'End');
        const before = [u.c.isOpen(), u.combo.getAttribute('aria-activedescendant')];
        let writes = 0; const nameNode = u.q('gt-name'), was = nameNode._text;
        Object.defineProperty(nameNode, 'textContent', { get() { return was; }, set() { writes++; } });
        u.c.paint({ id: 0, guest: false, disabled: false }); u.c.paint({ id: 0, guest: false, disabled: false, serverName: '' });
        same('an unchanged paint writes nothing at all', writes, 0);
        same('an unchanged paint leaves the open list and its row alone', [u.c.isOpen(), u.combo.getAttribute('aria-activedescendant')], before);
        u.c.paint({ id: 0, guest: false, disabled: false, serverName: 'x' }); check('a changed server name is painted (the stamp counts it)', u.c.isOpen());
        u.c.close();
    }
    click(u.combo); u.c.paint({ id: 1, guest: true, disabled: false });
    check('the leader becomes a guest while the list is open: it closes', !u.c.isOpen());
    same('the guest: the field and its line are hidden, the read-only line shows', [u.q('gt-field').hidden, u.q('gt-line').hidden, u.q('gt-note').hidden, u.q('gt-guest').hidden, u.q('gt-guest').textContent, u.q('gt-gline').textContent, u.q('gt-gline').hidden], [true, true, true, false, 'Game type: 187', M.MODES[1].line, false]);
    key(u.combo, 'ArrowDown'); click(u.combo); check('a guest cannot open it', !u.c.isOpen());
    u.c.paint({ id: 0, guest: true, disabled: false });
    same('the guest line follows the room', [u.q('gt-guest').textContent, u.q('gt-gline').textContent], ['Game type: Highest score', M.MODES[0].line]);
    u.c.paint({ id: 0, guest: false, disabled: true });
    key(u.combo, 'ArrowDown'); click(u.combo);
    same('a disabled field (START pressed, or offline) does not open', [u.c.isOpen(), u.combo.getAttribute('aria-disabled'), u.combo.getAttribute('tabindex')], [false, 'true', '-1']);
    u.c.paint({ id: 0, guest: false, disabled: false });
    check('and opens again when it can', (click(u.combo), u.c.isOpen()));
    u.c.close();
    key(u.combo, '1');
    same('a digit that starts a name opens the list on it', [u.c.isOpen(), u.combo.getAttribute('aria-activedescendant')], [true, 'gt-opt-1']);
    u.c.close(); u.timers.forEach((f) => f()); key(u.combo, 'h');
    same('a letter that starts a name', u.combo.getAttribute('aria-activedescendant'), 'gt-opt-0');
    u.timers.forEach((f) => f()); key(u.combo, 'q'); same('a letter that starts none moves nothing', u.combo.getAttribute('aria-activedescendant'), 'gt-opt-0');
    check('a key with a modifier is left to the browser', key(u.combo, 'ArrowDown', { ctrlKey: true }) === false);
}

{   // a mode that is added as a row: the control grows by a row, nothing else changes
    M.MODES.push({ id: 2, word: 'brawl', name: 'Brawl', tag: 'BR', line: 'Everyone fights.', note: '' }, { id: 3, word: 'zen', name: 'Zen', tag: '', line: 'Calm.', note: 'Take your time.' });
    const u = build(M);
    same('the added rows reach the functions too', [M.tagOf(2), M.guestLine(3), M.initialOf('brawl').id, M.wanted(store({ [M.STORAGE_KEY]: 'brawl' }), 0)], ['BR', 'Game type: Zen', 2, 2]);
    u.c.paint({ id: 2, guest: false, disabled: false });
    same('four options, the new one marked', [all(u.host, (n) => n.getAttribute('role') === 'option').length, u.opt(2).getAttribute('aria-selected'), u.q('gt-name').textContent], [4, 'true', 'Brawl']);
    key(u.combo, 'z'); same('type-ahead finds the added mode', u.combo.getAttribute('aria-activedescendant'), 'gt-opt-3');
    key(u.combo, 'Enter'); same('choosing it says its number', u.chosen, [3]);
    u.c.paint({ id: 3, guest: false, disabled: false });
    same('its note shows', [u.q('gt-note').hidden, u.q('gt-note').textContent], [false, 'Take your time.']);
    M.MODES.length = 2;
}

{   // a mode that this list does not hold
    const u = build(M);
    u.c.paint({ id: 6, guest: false, disabled: false });
    same('the leader sees New mode, no row marked, no line, no note', [u.q('gt-name').textContent, u.q('gt-line').hidden, u.q('gt-note').hidden, all(u.host, (n) => n.getAttribute('aria-selected') === 'true').length], ['New mode', true, true, 0]);
    key(u.combo, 'ArrowDown'); same('the list opens on the first row', u.combo.getAttribute('aria-activedescendant'), 'gt-opt-0');
    key(u.combo, 'Enter'); same('choosing a known row from it works', u.chosen, [0]);
    u.c.paint({ id: 6, guest: true, disabled: false });
    same('the guest line says New mode', [u.q('gt-guest').textContent, u.q('gt-gline').hidden], ['Game type: New mode', true]);
    u.c.paint({ id: 6, guest: true, disabled: false, serverName: 'Brawl' });
    same('and the server\'s name when it sends one', u.q('gt-guest').textContent, 'Game type: Brawl');
}

console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
