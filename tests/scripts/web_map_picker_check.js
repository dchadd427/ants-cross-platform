// Runs the page's OWN code for the map list (web/front/map_picker.js): the pure parts on tables (the list as the server sends it, the four sorts, the search) and the whole picker with a small fake of the
// browser's DOM: the loading, error and empty states, the Detail and Thumbnails views and the remembered switch, the choice by mouse and by keyboard, the focus, and that nothing a server says becomes markup.
// tests/scripts/test_web_map_picker.py runs this with node (the quick tier). usage: node web_map_picker_check.js web/front/map_picker.js     (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');
const P = require(path.resolve(process.argv[2]));

let checks = 0;
let failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }
const tick = () => new Promise((resolve) => setImmediate(resolve));
async function settle() { for (let i = 0; i < 10; i++) await tick(); }

// ---- a list as the server sends it ------------------------------------------------------------------------------------------------------------------------------------------------------------------
const M = (file, extra) => Object.assign({ file, width: 40, height: 30, minutes: 10, players: 4, description: 'by someone', plays: 0, thumb: '/maps/previews/' + encodeURIComponent(file.replace(/\.lvl$/i, '')) + '.webp' }, extra);
const RAW = {
    format: 1, maps: [
        M('TINY.LVL', { original: true, width: 30, height: 20, plays: 5, thumb: '/front/preview_tiny.png', description: 'Tiny map with no PowerUps' }),
        M('TREASURE.LVL', { original: true, width: 60, height: 60, plays: 90, thumb: '/front/preview_treasure.png', description: "One person's trash" }),
        M('Zebra.lvl', { plays: 10, width: 20, height: 20 }),
        M('apple.lvl', { plays: 10, description: 'Fruit <b>fight</b>' }),
        M('Mango tree.lvl', { plays: 300, width: 100, height: 90 }),
        M('Dune.lvl', { plays: 0, description: 'sand' }),
        M('Alpha.lvl', { plays: 0, width: 20, height: 20 })
    ]
};

// ---- the pure parts -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const list = P.parseList(RAW);
    same('every entry of a good list is kept', list.length, 7);
    same('an original is named by its word', [list[0].name, list[1].name, list[0].original, list[2].original], ['Tiny', 'Treasure', true, false]);
    same('a map of the players is named by its file name without the ending', list[4].name, 'Mango tree');
    same('displayName', [P.displayName('Mango tree.LVL', false), P.displayName('GAUNTLET.LVL', true)], ['Mango tree', 'Gauntlet']);
    check('no list when it is not format 1, not an object or has no maps', P.parseList(null) === null && P.parseList({ format: 2, maps: [] }) === null && P.parseList({ format: 1 }) === null && P.parseList('x') === null);
    const dirty = P.parseList({ format: 1, maps: [null, 5, { file: '../x.lvl' }, { file: 'a\\b.lvl' }, { file: 'ok.lvl', width: -1, minutes: 'x', plays: 1.5, players: 4, thumb: 'https://evil.example/x.png', description: 'a\u0007b' },
        { file: 'ok2.lvl', thumb: '/maps/previews/../../etc.webp' }, { file: 'ok3.lvl', thumb: '/maps/previews/a b.webp' }, { file: 'ok4.lvl', thumb: '//evil.example/x.png' }, { file: 'ok5.lvl', thumb: '/front/preview_tiny.png' }, { file: 'dots..lvl', thumb: '/maps/previews/dots..webp' }, { file: 'no ending' }] });
    same('entries that are no map are left out', dirty.map((m) => m.file), ['ok.lvl', 'ok2.lvl', 'ok3.lvl', 'ok4.lvl', 'ok5.lvl', 'dots..lvl']);
    same('numbers that are not whole counts become 0, a control character becomes a blank', [dirty[0].width, dirty[0].minutes, dirty[0].plays, dirty[0].description], [0, 0, 0, 'a b']);
    same('a picture is the site\'s own path or none', dirty.map((m) => m.thumb), ['', '', '', '', '/front/preview_tiny.png', '/maps/previews/dots..webp']);
    // sorts
    const names = (mode) => P.sortMaps(list, mode).map((m) => m.name);
    same('default: the originals first in the server\'s order, then the others by plays most first, ties by name', names('default'), ['Tiny', 'Treasure', 'Mango tree', 'apple', 'Zebra', 'Alpha', 'Dune']);
    same('most played: every map, most first, ties by name', names('plays'), ['Mango tree', 'Treasure', 'apple', 'Zebra', 'Tiny', 'Alpha', 'Dune']);
    same('name: case ignored', names('name'), ['Alpha', 'apple', 'Dune', 'Mango tree', 'Tiny', 'Treasure', 'Zebra']);
    same('size: smallest area first, ties by name', names('size'), ['Alpha', 'Zebra', 'Tiny', 'apple', 'Dune', 'Treasure', 'Mango tree']);
    same('the list that is sorted is not changed', list.map((m) => m.name), ['Tiny', 'Treasure', 'Zebra', 'apple', 'Mango tree', 'Dune', 'Alpha']);
    // search
    const found = (q) => P.filterMaps(list, q).map((m) => m.name);
    same('no search shows all', found('').length, 7);
    same('a search is on the name', found('MANGO'), ['Mango tree']);
    same('a search is on the description too', found('sand'), ['Dune']);
    same('every word has to be there, in the name or the description', [found('mango tree'), found('fruit mango'), found('sand dune')], [['Mango tree'], [], ['Dune']]);
    same('a word that nothing has finds nothing', found('zzzz'), []);
    same('blanks around the words are nothing', found('  mango   tree '), ['Mango tree']);
    same('plays are written with thousands', [P.playsText(0), P.playsText(999), P.playsText(1000), P.playsText(1234567)], ['0', '999', '1,000', '1,234,567']);
    same('what a row says of a map', [P.metaText(list[0]), P.metaText(list[2])], ['Shipped with the game', '4 players · 10 min · 20×20']);
    same('the Map Info line of a map of the players', P.infoText(list[5]), 'sand (10 min)');
}

// ---- a small fake of the browser's DOM, enough for the picker ---------------------------------------------------------------------------------------------------------------------------------------
class Node {
    constructor(doc, tag) { this.doc = doc; this.tagName = tag.toUpperCase(); this.attrs = {}; this.children = []; this.parentNode = null; this.className = ''; this.listeners = {}; this._text = ''; this.hidden = false; this.disabled = false; this.value = ''; this.offsetTop = 0; this.isText = false; }
    get textContent() { return this.isText ? this._text : this.children.map((c) => c.textContent).join(''); }
    set textContent(v) { this.children = []; if (String(v) !== '') { const t = this.doc.createTextNode(String(v)); t.parentNode = this; this.children.push(t); } }
    appendChild(c) { if (c.isFragment) { c.children.slice().forEach((k) => this.appendChild(k)); c.children = []; return c; } c.parentNode = this; this.children.push(c); return c; }
    setAttribute(k, v) { this.attrs[k] = String(v); }
    getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
    removeAttribute(k) { delete this.attrs[k]; }
    get id() { return this.attrs.id || ''; }
    set id(v) { this.attrs.id = v; }
    addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); }
    contains(n) { for (let x = n; x; x = x.parentNode) if (x === this) return true; return false; }
    focus() { this.doc.activeElement = this; }
    scrollIntoView() {}
    closest(sel) { for (let x = this; x; x = x.parentNode) if (x.matches && x.matches(sel)) return x; return null; }
    matches(sel) { return sel.split(',').some((one) => matchOne(this, one.trim())); }
    querySelectorAll(sel) { const out = []; const walk = (n) => { n.children.forEach((c) => { if (!c.isText && c.matches(sel)) out.push(c); walk(c); }); }; walk(this); return out; }
    querySelector(sel) { return this.querySelectorAll(sel)[0] || null; }
    dispatch(type, init) {
        const ev = Object.assign({ type, target: this, key: '', shiftKey: false, defaultPrevented: false, preventDefault() { this.defaultPrevented = true; }, stopPropagation() { this.stopped = true; } }, init || {});
        for (let x = this; x && !ev.stopped; x = x.parentNode) (x.listeners && x.listeners[type] || []).forEach((f) => f(ev));
        return ev;
    }
}
function matchOne(node, sel) {
    const re = /(\.[\w-]+)|(\[[\w-]+(?:="[^"]*")?\])/g;
    let m, any = false;
    const tag = /^[a-z][a-z0-9]*/i.exec(sel);
    if (tag) { if (node.tagName !== tag[0].toUpperCase()) return false; any = true; }
    while ((m = re.exec(sel))) {
        any = true;
        if (m[1]) { if (!(' ' + node.className + ' ').includes(' ' + m[1].slice(1) + ' ')) return false; }
        else {
            const kv = /\[([\w-]+)(?:="([^"]*)")?\]/.exec(m[2]);
            if (!(kv[1] in node.attrs)) return false;
            if (kv[2] !== undefined && node.attrs[kv[1]] !== kv[2]) return false;
        }
    }
    return any;
}
function makeDoc() {
    const doc = { activeElement: null };
    doc.createElement = (tag) => new Node(doc, tag);
    doc.createTextNode = (t) => { const n = new Node(doc, '#text'); n.isText = true; n._text = t; return n; };
    doc.createDocumentFragment = () => { const n = new Node(doc, '#frag'); n.isFragment = true; return n; };
    return doc;
}

function response(body, ok) { return { ok: ok !== false, json: () => Promise.resolve(body) }; }
function rig(options) {
    options = options || {};
    const doc = makeDoc();
    const overlay = new Node(doc, 'div'); overlay.hidden = true;
    const host = new Node(doc, 'div'); overlay.appendChild(host);
    const store = {};
    const storage = options.storage === undefined ? { getItem: (k) => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); } } : options.storage;
    const log = { fetches: [], chosen: [], lists: 0, closed: 0 };
    let answer = options.answer || (() => Promise.resolve(response(RAW)));
    const picker = P.create({
        host, doc, storage, fetch: (url, init) => { log.fetches.push({ url, init }); return answer(); },
        onChoose: (e) => log.chosen.push(e.file), onList: () => { log.lists++; }, onClose: () => { log.closed++; }
    });
    return { picker, host, overlay, doc, store, log, setAnswer: (f) => { answer = f; },
        rows: () => host.querySelectorAll('[data-file]'), names: () => host.querySelectorAll('[data-file]').map((n) => n.querySelector('.pk-nm').children[0].textContent),
        sel: () => host.querySelectorAll('[data-file]').filter((n) => n.attrs['aria-selected'] === 'true').map((n) => n.attrs['data-file']),
        btn: (label) => host.querySelectorAll('button').find((b) => b.textContent === label), out: () => host.querySelector('.pk-out') };
}

(async () => {
    // 1. opening: loading, then the list, one fetch for the visit
    {
        const r = rig();
        check('the dialog is hidden until it is opened', r.overlay.hidden === true);
        r.picker.open('Zebra.lvl');
        check('open shows the dialog', r.overlay.hidden === false && r.picker.isOpen());
        check('it says it is loading', /Loading the maps/.test(r.out().textContent));
        same('the list is asked for once, from the site, with the cookies of the site', [r.log.fetches.length, r.log.fetches[0].url, r.log.fetches[0].init.credentials], [1, '/maps', 'same-origin']);
        await settle();
        same('the page is told that the list is here', r.log.lists, 1);
        same('the default order is shown (originals first, then by plays)', r.names(), ['Tiny', 'Treasure', 'Mango tree', 'apple', 'Zebra', 'Alpha', 'Dune']);
        same('the room\'s map is the picked one', r.sel(), ['Zebra.lvl']);
        same('the count says what is shown', r.host.querySelector('.pk-count').textContent, '7 of 7 maps shown');
        check('the search box has the focus', r.doc.activeElement === r.host.querySelector('.pk-q'));
        check('the list is a listbox of options', r.out().attrs.role === 'listbox' && r.rows().every((n) => n.attrs.role === 'option'));
        same('one stop for the Tab key in the list: the picked one', r.rows().filter((n) => n.attrs.tabindex === '0').map((n) => n.attrs['data-file']), ['Zebra.lvl']);
        r.picker.close(); r.picker.open('');
        await settle();
        same('opened again: no second fetch for the visit', r.log.fetches.length, 1);
        same('nothing is picked when the room names no map', r.sel(), []);
        same('then the first row is the one stop', r.rows().filter((n) => n.attrs.tabindex === '0').length, 1);
        // the pictures
        const img = r.rows()[0].children.find((c) => c.tagName === 'IMG');
        same('a picture in the Detail view is 64 px, lazy, with no alt text (the name stands beside it)', [img.attrs.width, img.attrs.height, img.attrs.loading, img.attrs.alt, img.attrs.src], ['64', '64', 'lazy', '', '/front/preview_tiny.png']);
        check('the Original badge is on the originals only', r.rows().map((n) => !!n.querySelector('.pk-badge')).join() === 'true,true,false,false,false,false,false');
        same('a row says the plays', r.rows()[2].querySelector('.pk-pl').textContent, '300plays');
        same('and its size, players and minutes', r.rows()[2].querySelectorAll('.pk-meta')[0].textContent, '4 players · 10 min · 100×90');
    }
    // 2. the switch: Detail by default, remembered per browser, unaffected by a storage that throws
    {
        const r = rig();
        r.picker.open('');
        await settle();
        same('Detail is the default', [r.picker.view(), r.btn('Detail').attrs['aria-pressed'], r.btn('Thumbnails').attrs['aria-pressed']], ['detail', 'true', 'false']);
        r.btn('Thumbnails').dispatch('click');
        same('Thumbnails: the grid, with pictures of 130 px', [r.picker.view(), r.out().className, r.rows()[0].children.find((c) => c.tagName === 'IMG').attrs.width], ['thumbs', 'pk-out pk-grid', '130']);
        check('the grid shows a name and the plays, not the long lines', !r.rows()[2].querySelector('.pk-pl') && /300 plays/.test(r.rows()[2].textContent));
        same('the choice is kept in localStorage', r.store['ants.mapview'], 'thumbs');
        const again = rig({ storage: { getItem: () => 'thumbs', setItem() {} } });
        again.picker.open(''); await settle();
        same('a later visit opens in Thumbnails', again.picker.view(), 'thumbs');
        const junk = rig({ storage: { getItem: () => 'zzz', setItem() {} } });
        check('a value that is neither is Detail', junk.picker.view() === 'detail');
        const broken = rig({ storage: { getItem() { throw new Error('blocked'); }, setItem() { throw new Error('blocked'); } } });
        broken.picker.open(''); await settle();
        broken.btn('Thumbnails').dispatch('click');
        same('a storage that throws changes nothing but the memory', [broken.picker.view(), broken.rows().length], ['thumbs', 7]);
        const none = rig({ storage: null });
        none.picker.open(''); await settle();
        none.btn('Thumbnails').dispatch('click');
        same('no storage at all is fine', none.picker.view(), 'thumbs');
        r.btn('Detail').dispatch('click');
        same('and back to Detail', [r.picker.view(), r.store['ants.mapview']], ['detail', 'detail']);
    }
    // 3. search, sorts and the empty state
    {
        const r = rig();
        r.picker.open('');
        await settle();
        const q = r.host.querySelector('.pk-q');
        q.value = 'MANGO'; q.dispatch('input');
        same('typing searches', [r.names(), r.host.querySelector('.pk-count').textContent], [['Mango tree'], '1 of 7 maps shown']);
        q.value = 'fight'; q.dispatch('input');
        same('the description is searched', r.names(), ['apple']);
        check('and shown as text, not as markup', r.rows()[0].textContent.indexOf('<b>fight</b>') !== -1 && r.rows()[0].querySelectorAll('b').length === 1);
        q.value = 'zzz'; q.dispatch('input');
        check('a search with no result says so', /No maps found/.test(r.out().textContent) && r.rows().length === 0);
        check('and the Choose button is off', r.btn('Choose map').disabled === true);
        q.value = ''; q.dispatch('input');
        r.btn('Name').dispatch('click');
        same('sort by name', [r.names().slice(0, 3), r.btn('Name').attrs['aria-pressed'], r.btn('Originals, then plays').attrs['aria-pressed']], [['Alpha', 'apple', 'Dune'], 'true', 'false']);
        r.btn('Most played').dispatch('click');
        same('sort by plays', r.names().slice(0, 2), ['Mango tree', 'Treasure']);
        r.btn('Size').dispatch('click');
        same('sort by size', r.names().slice(0, 2), ['Alpha', 'Zebra']);
        r.btn('Originals, then plays').dispatch('click');
        same('and the default again', r.names().slice(0, 3), ['Tiny', 'Treasure', 'Mango tree']);
    }
    // 4. choosing with the mouse and with the keyboard
    {
        const r = rig();
        r.picker.open('TINY.LVL');
        await settle();
        r.rows()[3].dispatch('click');
        same('a click picks a row (it is highlighted, nothing is chosen yet)', [r.sel(), r.log.chosen], [['apple.lvl'], []]);
        r.btn('Choose map').dispatch('click');
        same('Choose map hands the entry over and closes the dialog', [r.log.chosen, r.overlay.hidden, r.log.closed], [['apple.lvl'], true, 1]);
        r.picker.open('apple.lvl'); await settle();
        r.rows()[4].dispatch('dblclick');
        same('a double click chooses at once', r.log.chosen, ['apple.lvl', 'Zebra.lvl']);
        // keyboard in the list
        r.picker.open('TINY.LVL'); await settle();
        const key = (k, extra) => r.out().dispatch('keydown', Object.assign({ key: k }, extra || {}));
        r.host.querySelector('.pk-q').dispatch('keydown', { key: 'ArrowDown' });
        check('ArrowDown in the search box goes to the list (the picked row has the focus)', r.doc.activeElement && r.doc.activeElement.attrs['data-file'] === 'TINY.LVL');
        key('ArrowDown');
        same('ArrowDown goes to the next row and picks it', [r.sel(), r.doc.activeElement.attrs['data-file']], [['TREASURE.LVL'], 'TREASURE.LVL']);
        key('End'); same('End', r.sel(), ['Dune.lvl']);
        key('ArrowDown'); same('ArrowDown at the end stays', r.sel(), ['Dune.lvl']);
        key('Home'); same('Home', r.sel(), ['TINY.LVL']);
        key('ArrowUp'); same('ArrowUp at the start stays', r.sel(), ['TINY.LVL']);
        key('PageDown'); same('PageDown goes eight on', r.sel(), ['Dune.lvl']);
        key('ArrowRight'); same('the Detail list ignores the sideways arrows', r.sel(), ['Dune.lvl']);
        const ev = key('Enter');
        same('Enter chooses the picked row', [r.log.chosen[r.log.chosen.length - 1], r.overlay.hidden, ev.defaultPrevented], ['Dune.lvl', true, true]);
        // Escape and the focus
        r.picker.open(''); await settle();
        const esc = r.host.dispatch('keydown', { key: 'Escape' });
        same('Escape closes and is not passed on', [r.overlay.hidden, esc.defaultPrevented, r.picker.isOpen()], [true, true, false]);
        r.picker.open(''); await settle();
        r.btn('Close').dispatch('click');
        same('the Close button closes', r.overlay.hidden, true);
        // the Tab key stays in the dialog
        r.picker.open('TINY.LVL'); await settle();
        r.btn('Choose map').focus();
        const t1 = r.host.dispatch('keydown', { key: 'Tab' });
        check('Tab from the last stop goes to the first (the dialog is modal)', t1.defaultPrevented && r.doc.activeElement === r.btn('Close'));
        const t2 = r.host.dispatch('keydown', { key: 'Tab', shiftKey: true });
        check('Shift+Tab from the first goes to the last', t2.defaultPrevented && r.doc.activeElement === r.btn('Choose map'));
        r.doc.activeElement = r.rows()[0];
        r.host.dispatch('keydown', { key: 'Tab' });
        check('Tab from the list goes on to Choose map', r.doc.activeElement === r.btn('Choose map'));
    }
    // 5. the grid's arrows go by rows
    {
        const r = rig();
        r.picker.open('TINY.LVL'); await settle();
        r.btn('Thumbnails').dispatch('click');
        r.rows().forEach((n, i) => { n.offsetTop = Math.floor(i / 3) * 100; });       // three in a row
        r.rows()[0].dispatch('click');
        const key = (k) => r.out().dispatch('keydown', { key: k });
        key('ArrowRight'); same('ArrowRight in the grid goes to the next map', r.sel(), ['TREASURE.LVL']);
        key('ArrowDown'); same('ArrowDown goes down a row (three on)', r.sel(), ['Zebra.lvl']);
        key('ArrowLeft'); same('ArrowLeft goes back', r.sel(), ['apple.lvl']);
        key('ArrowUp'); same('ArrowUp goes up a row', r.sel(), ['TINY.LVL']);
    }
    // 6. a list that cannot be had: the message, the way out, the retry
    {
        const r = rig({ answer: () => Promise.reject(new Error('offline')) });
        r.picker.open('');
        await settle();
        check('the error says the six originals can still be chosen with the arrows', /not available right now/.test(r.out().textContent) && /six original maps/.test(r.out().textContent));
        check('there is a Try again button', !!r.host.querySelector('[data-retry]'));
        check('the page was not told of a list', r.log.lists === 0 && r.picker.status() === 'error');
        r.setAnswer(() => Promise.resolve(response(RAW)));
        r.host.querySelector('[data-retry]').dispatch('click');
        await settle();
        same('Try again fetches again and shows the list', [r.log.fetches.length, r.rows().length, r.picker.status()], [2, 7, 'ready']);
        for (const [label, answer] of [['a server error', () => Promise.resolve(response({}, false))], ['a body that is no list', () => Promise.resolve(response({ nope: 1 }))], ['an empty list', () => Promise.resolve(response({ format: 1, maps: [] }))],
            ['a body that is not json', () => Promise.resolve({ ok: true, json: () => Promise.reject(new Error('json')) })], ['a fetch that throws', () => { throw new Error('no fetch'); }]]) {
            const q = rig({ answer });
            q.picker.open(''); await settle();
            check(label + ': the error state', q.picker.status() === 'error' && /not available/.test(q.out().textContent), q.picker.status());
        }
    }
    // 7. what a server says is text
    {
        const evil = { format: 1, maps: [M("a&amp;b 'x'.lvl", { description: '"><script>alert(1)</script>', thumb: 'javascript:alert(1)' }), M('<img src=x onerror=alert(1)>.lvl'), M('Fine.lvl')] };
        const r = rig({ answer: () => Promise.resolve(response(evil)) });
        r.picker.open('');
        await settle();
        same('a name that no file can have (markup) is left out of the list', r.names().length, 2);
        check('a name with & and a quote is shown as it is', r.names().includes("a&amp;b 'x'"), JSON.stringify(r.names()));
        check('the description is text', r.host.querySelectorAll('script').length === 0 && r.rows().some((n) => n.textContent.indexOf('"><script>alert(1)</script>') !== -1));
        check('a picture address that is not the site\'s own makes no picture', r.rows().filter((n) => n.children[0].tagName === 'SPAN').length === 1 && r.host.querySelectorAll('img').every((i) => /^\/maps\/previews\//.test(i.attrs.src)));
    }
    console.log((failures ? 'FAILED: ' + failures + ' of ' : 'ok: ') + checks + ' checks');
    process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
