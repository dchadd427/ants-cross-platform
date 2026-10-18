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
    const dirty = P.parseList({ format: 1, maps: [null, 5, { file: '../x.lvl' }, { file: 'a\\b.lvl' }, { file: 'a/b.lvl' }, { file: 'ok.lvl', width: -1, minutes: 'x', plays: 1.5, players: 4, thumb: 'https://evil.example/x.png', description: 'a\u0007b' },
        { file: 'ok2.lvl', thumb: '/maps/previews/../../etc.webp' }, { file: 'ok3.lvl', thumb: '/maps/previews/a b.webp' }, { file: 'ok4.lvl', thumb: '//evil.example/x.png' }, { file: 'ok5.lvl', thumb: '/front/preview_tiny.png' }, { file: 'dots..lvl', thumb: '/maps/previews/dots..webp' }, { file: 'no ending' }] });
    same('entries that are no map are left out', dirty.map((m) => m.file), ['ok.lvl', 'ok2.lvl', 'ok3.lvl', 'ok4.lvl', 'ok5.lvl', 'dots..lvl']);
    same('numbers that are not whole counts become 0, a control character becomes a blank', [dirty[0].width, dirty[0].minutes, dirty[0].plays, dirty[0].description], [0, 0, 0, 'a b']);
    same('a picture is the site\'s own path or none', dirty.map((m) => m.thumb), ['', '', '', '', '/front/preview_tiny.png', '/maps/previews/dots..webp']);
    {   // a map that a player sent and the owner approved has a picture of the site's own, a PNG: the same name rules as the others
        const approved = P.parseList({ format: 1, maps: [{ file: 'Run One.lvl', thumb: '/maps/previews/Run%20One.png', maker: 'Mia' }, { file: 'a.lvl', thumb: '/maps/previews/%2e%2e%2fx.png' }, { file: 'b.lvl', thumb: '/maps/previews/x.gif' },
            { file: 'c.lvl', thumb: '/maps/previews/a%2Fb.png' }, { file: 'd.lvl', thumb: '/maps/previews/.png' }, { file: 'e.lvl', thumb: '/maps/previews/x.png?y=1' }, { file: 'f.lvl', thumb: 'https://evil.example/maps/previews/x.png' }] });
        same('a picture of an approved map is a PNG of the site, and no other address passes', approved.map((m) => m.thumb), ['/maps/previews/Run%20One.png', '', '', '', '', '', '']);
    }
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

// ---- the picture of a map that a match record names (history_page.js: the cards of the match pages) -------------------------------------------------------------------------------------------------------------
{
    same('a plain name', P.previewPath('POPcOrN.lvl'), '/maps/previews/POPcOrN.webp');
    same('the ending may be missing or in capitals', [P.previewPath('POPcOrN'), P.previewPath('POPcOrN.LVL')], ['/maps/previews/POPcOrN.webp', '/maps/previews/POPcOrN.webp']);
    same('a space and "!" are escaped as the server does (encodeURIComponent leaves "!")', P.previewPath('Pop Water!!!.lvl'), '/maps/previews/Pop%20Water%21%21%21.webp');
    same('every sign the server escapes', P.previewPath("a'b(c)d#e$f&g+h,i;j=k@l[m]n^o`p{q}r~s.lvl"), '/maps/previews/a%27b%28c%29d%23e%24f%26g%2Bh%2Ci%3Bj%3Dk%40l%5Bm%5Dn%5Eo%60p%7Bq%7Dr~s.webp');
    same('"(" and ")" too', P.previewPath('(a)!.lvl'), '/maps/previews/%28a%29%21.webp');
    same('a dot inside stays, the case stays', P.previewPath('Mr.Big_Map-2.lvl'), '/maps/previews/Mr.Big_Map-2.webp');
    same('a percent sign is escaped, never read as an escape', P.previewPath('100%25 sure.lvl'), '/maps/previews/100%2525%20sure.webp');
    same('only the ending is cut, once', P.previewPath('x.lvl.lvl'), '/maps/previews/x.lvl.webp');
    same('a name that is not ASCII has no picture', [P.previewPath('Käse.lvl'), P.previewPath('地図.lvl'), P.previewPath('a\u0000b.lvl')], ['', '', '']);
    same('a name that fails the game\'s rule has no picture', [P.previewPath('.hidden.lvl'), P.previewPath('a/b.lvl'), P.previewPath('a\\b.lvl'), P.previewPath('a:b.lvl'), P.previewPath('a?b.lvl'), P.previewPath('a"b.lvl'), P.previewPath('a|b.lvl'),
        P.previewPath('a*b.lvl'), P.previewPath('.lvl'), P.previewPath(''), P.previewPath('x'.repeat(61) + '.lvl'), P.previewPath('../x.lvl'), P.previewPath(null), P.previewPath(5), P.previewPath(undefined)], ['', '', '', '', '', '', '', '', '', '', '', '', '', '', '']);
    same('the longest name', P.previewPath('x'.repeat(60) + '.lvl'), '/maps/previews/' + 'x'.repeat(60) + '.webp');
    same('the six originals keep their own art (no community address for them)', ['MEDIUM.LVL', 'Small.lvl', 'tiny', 'GAUNTLET', 'treasure.LVL', 'Islands.lvl'].map((n) => P.previewPath(n)), ['', '', '', '', '', '']);
    same('percentEncode', [P.percentEncode('a b'), P.percentEncode('!'), P.percentEncode('~._-'), P.percentEncode('%')], ['a%20b', '%21', '~._-', '%25']);
    // with the server's list
    const list = P.parseList({ format: 1, maps: [
        M('MEDIUM.LVL', { original: true, thumb: '/front/preview_medium.png' }), M('POPcOrN.lvl'), M('Pop Water!!!.lvl', { thumb: '/maps/previews/Pop%20Water%21%21%21.webp' }), M('NoPic.lvl', { thumb: undefined })
    ] });
    same('a map of the list has the list\'s picture', P.thumbFor('POPcOrN.lvl', list), '/maps/previews/POPcOrN.webp');
    same('... with or without the ending, in any case', [P.thumbFor('POPcOrN', list), P.thumbFor('popcorn.lvl', list), P.thumbFor('POPCORN.LVL', list)], ['/maps/previews/POPcOrN.webp', '/maps/previews/POPcOrN.webp', '/maps/previews/POPcOrN.webp']);
    same('a map the list holds without a picture has none (the server has none)', P.thumbFor('NoPic.lvl', list), '');
    same('a map the list does not hold has the address its name makes', P.thumbFor('Other Map!.lvl', list), '/maps/previews/Other%20Map%21.webp');
    same('no list yet: the name makes it', P.thumbFor('Pop Water!!!.lvl', null), '/maps/previews/Pop%20Water%21%21%21.webp');
    same('an original is none, with the list or without', [P.thumbFor('MEDIUM.lvl', list), P.thumbFor('medium', null), P.thumbFor('Small.LVL', list)], ['', '', '']);
    same('a name that fails the rule is none even when a list is there', [P.thumbFor('a/b.lvl', list), P.thumbFor('Käse.lvl', list), P.thumbFor('', list), P.thumbFor(null, list)], ['', '', '', '']);
    same('an exact name wins over one that differs in case', P.thumbFor('Aa.lvl', P.parseList({ format: 1, maps: [M('aa.lvl', { thumb: '/maps/previews/aa.webp' }), M('Aa.lvl', { thumb: '/maps/previews/Aa.webp' })] })), '/maps/previews/Aa.webp');
}

// ---- a small fake of the browser's DOM, enough for the picker ---------------------------------------------------------------------------------------------------------------------------------------
class Node {
    constructor(doc, tag) { this.doc = doc; this.tagName = tag.toUpperCase(); this.attrs = {}; this.children = []; this.parentNode = null; this.className = ''; this.listeners = {}; this._text = ''; this.hidden = false; this.disabled = false; this.value = ''; this.offsetTop = 0; this.scrollTop = 0; this.isText = false; this.scrolled = 0; }
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
    scrollIntoView() { this.doc.scrolledTo = this; }
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
    const doc = { activeElement: null, listeners: {} };
    doc.addEventListener = (t, f) => { (doc.listeners[t] = doc.listeners[t] || []).push(f); };
    doc.removeEventListener = (t, f) => { doc.listeners[t] = (doc.listeners[t] || []).filter((g) => g !== f); };
    doc.dispatch = (type, init) => { const ev = Object.assign({ type, key: '', defaultPrevented: false, preventDefault() { this.defaultPrevented = true; } }, init || {}); (doc.listeners[type] || []).slice().forEach((f) => f(ev)); return ev; };
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
    const log = { fetches: [], chosen: [], lists: 0, closed: 0, opened: 0 };
    let answer = options.answer || (() => Promise.resolve(response(RAW)));
    const timers = [], allow = { open: true, choose: true }, clock = { now: 1000000 };
    const picker = P.create({
        host, doc, storage, fetch: (url, init) => { log.fetches.push({ url, init }); return answer(init); },
        setTimeout: (f, ms) => { timers.push({ f, ms, live: true }); return timers.length; }, clearTimeout: (t) => { if (timers[t - 1]) timers[t - 1].live = false; },
        AbortController: function () { this.signal = { aborted: false }; this.abort = () => { this.signal.aborted = true; }; }, now: () => clock.now,
        matchMedia: () => ({ matches: !!options.coarse }), canOpen: () => allow.open, canChoose: () => allow.choose,
        onChoose: (e) => log.chosen.push(e.file), onList: () => { log.lists++; }, onClose: () => { log.closed++; }, onOpen: () => { log.opened++; }
    });
    const fire = (ms) => { timers.filter((t) => t.live && (ms === undefined || t.ms === ms)).forEach((t) => { t.live = false; t.f(); }); };
    return { picker, host, overlay, doc, store, log, timers, allow, clock, fire, setAnswer: (f) => { answer = f; },
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
        r.picker.load(); r.picker.load();
        same('opened again, and the page asking for the list: no second fetch for the visit', r.log.fetches.length, 1);
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
        q.value = 'MANGO'; q.dispatch('input'); r.fire(120);
        same('typing searches', [r.names(), r.host.querySelector('.pk-count').textContent], [['Mango tree'], '1 of 7 maps shown']);
        q.value = 'fight'; q.dispatch('input'); r.fire(120);
        same('the description is searched', r.names(), ['apple']);
        check('and the row does not print it (the Map Info line has it)', r.rows()[0].textContent.indexOf('fight') === -1 && r.rows()[0].textContent.indexOf('by someone') === -1);
        q.value = 'zzz'; q.dispatch('input'); r.fire(120);
        same('a search with no result says so, as the mock-up does', [r.out().textContent, r.rows().length], ['No map is called "zzz".Try fewer letters.', 0]);
        check('and the Choose button is off', r.btn('Choose map').disabled === true);
        r.picker.close(); r.picker.open('Zebra.lvl'); await settle();
        check('Choose map is on for the picked map that is on the screen', r.btn('Choose map').disabled === false);
        q.value = 'dune'; q.dispatch('input'); r.fire(120);
        same('a search that leaves matches without the picked map', [r.names(), r.sel()], [['Dune'], []]);
        check('turns Choose map off (it would choose a map that is not on screen)', r.btn('Choose map').disabled === true);
        q.value = 'zebra'; q.dispatch('input'); r.fire(120);
        check('and it is on again when the picked map is shown', r.btn('Choose map').disabled === false);
        q.value = ''; q.dispatch('input'); r.fire(120);
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
        check('the description is never markup', r.host.querySelectorAll('script').length === 0);
        check('a picture address that is not the site\'s own makes no picture', r.rows().filter((n) => n.children[0].tagName === 'SPAN').length === 1 && r.host.querySelectorAll('img').every((i) => /^\/maps\/previews\//.test(i.attrs.src)));
    }
    // 8. the rules for a name and a picture are the game's and the server's
    {
        const keep = (maps) => P.parseList({ format: 1, maps }).map((m) => m.file);
        same('a map name follows net::valid_map_name: five characters at least, no leading dot, exactly .lvl or .LVL, no forbidden character, printable',
            keep(['.lvl', 'a.lvl', '.hid.lvl', 'abcde.Lvl', 'abcde.lVL', 'ab.LVL', 'x'.repeat(61) + '.lvl', 'x'.repeat(60) + '.lvl', 'a b.LVL', 'a*b.lvl', 'a?b.lvl', 'a:b.lvl', 'a|b.lvl', 'aébc.lvl', 'a\tbc.lvl', '', 'tiny.LVL'].map((f) => ({ file: f }))),
            ['a.lvl', 'ab.LVL', 'x'.repeat(60) + '.lvl', 'a b.LVL', 'tiny.LVL']);
        const pic = (t) => P.parseList({ format: 1, maps: [{ file: 'abcde.lvl', thumb: t }] })[0].thumb;
        const good = ['/maps/previews/Bomb%20Island.webp', '/maps/previews/dots..webp', '/maps/previews/%C3%A9.webp', '/front/preview_tiny.png', '/maps/previews/!a.webp'.replace('!', '%21')];
        same('the two shapes of picture the server writes are kept', good.map(pic), good);
        const bad = ['/maps/previews/a%2Fb.webp', '/maps/previews/a%2fb.webp', '/maps/previews/a%5Cb.webp', '/maps/previews/a%5cb.webp', '/maps/previews/a%00b.webp', '/maps/previews/a%2.webp', '/maps/previews/a.webp?x=1', '/maps/previews/a.webp#x',
            '/maps/previews/../a.webp', '/maps/previews/a/b.webp', '/maps/previews/.webp', '/maps/previews/.hidden.webp', '/maps/previews/a b.webp', '/maps/previews/a.gif', '/front/preview_TINY.png', '/front/preview_tiny.png?v=1', '/front/preview_.png',
            '/front/logo.png', '/maps/files/a.webp', 'https://x.example/maps/previews/a.webp', '//x.example/maps/previews/a.webp', 'maps/previews/a.webp', '/front/preview_a.svg', '/maps/previews/a.svg', '/front/preview_a/b.png', '/maps/previews/%5Ca.webp', '/maps/previews/%5ca.webp', '/maps/previews/%2Fa.webp', '/maps/previews/%00a.webp', 'javascript:alert(1)', '/maps/previews/a\\b.webp', '/maps/previews/a"b.webp', 5, null];
        same('everything else is no picture', bad.map(pic), bad.map(() => ''));
    }
    // 9. the mock-up's words, the room's map in another case, the labels
    {
        const r = rig();
        r.picker.open('treasure.lvl'); await settle();
        same('the search box says how many maps there are', r.host.querySelector('.pk-q').attrs.placeholder, 'Search 7 maps');
        same('the fine print of the plays is under the list', r.host.querySelector('.pk-fine').textContent, 'Plays count only matches with people in them that lasted at least 30 seconds.');
        same('the room\'s map in another case is the highlighted one', r.sel(), ['TREASURE.LVL']);
        same('and find() knows it', [r.picker.find('treasure.lvl').name, r.picker.find('nothing.lvl')], ['Treasure', null]);
        same('the dialog is named by its heading', [r.host.attrs['aria-labelledby'], r.host.querySelector('h2').id], ['picker-h', 'picker-h']);
        same('a Detail row says players, minutes and size for a map of the players and not its description', [r.rows()[2].textContent.indexOf('4 players · 10 min · 100×90') !== -1, r.rows()[3].textContent.indexOf('Fruit') === -1], [true, true]);
        const q2 = r.host.querySelector('.pk-q'); q2.value = 'Mango tree'; q2.dispatch('input'); r.fire(120);
        same('the empty state quotes the text', (function () { q2.value = 'qqq  '; q2.dispatch('input'); r.fire(120); return r.out().textContent; })(), 'No map is called "qqq".Try fewer letters.');
    }
    // 10. the search waits for the typing to pause
    {
        const r = rig();
        r.picker.open(''); await settle();
        const q = r.host.querySelector('.pk-q');
        q.value = 'm'; q.dispatch('input'); q.value = 'ma'; q.dispatch('input'); q.value = 'mango'; q.dispatch('input');
        same('while typing the list is not made again', r.rows().length, 7);
        same('one wait is live, 120 ms (the earlier ones were cancelled)', r.timers.filter((t) => t.live).map((t) => t.ms), [120]);
        r.fire(120);
        same('when it pauses the last text is searched', r.names(), ['Mango tree']);
        q.value = 'ze'; q.dispatch('input');
        r.picker.close();
        check('closing cancels a wait', r.timers.filter((t) => t.live && t.ms === 120).length === 0);
        r.picker.open(''); await settle();
        q.value = 'ze'; q.dispatch('input');
        const b = r.btn('Name'); b.dispatch('click');
        same('a sort made while a search waits makes the search first', [r.names(), r.timers.filter((t) => t.live && t.ms === 120).length], [['Zebra'], 0]);
        q.value = 'ap'; q.dispatch('input');
        r.rows()[0].dispatch('click'); r.btn('Choose map').dispatch('click');
        same('choosing while a search waits chooses the row that was clicked', r.log.chosen, ['Zebra.lvl']);
    }
    // 11. where the list stands
    {
        const r = rig();
        r.picker.open('Zebra.lvl'); await settle();
        r.doc.scrolledTo = null;
        r.btn('Name').dispatch('click');
        check('after a sort the chosen row is brought into view', r.doc.scrolledTo && r.doc.scrolledTo.attrs['data-file'] === 'Zebra.lvl');
        r.doc.scrolledTo = null;
        r.btn('Thumbnails').dispatch('click');
        check('after a view change too', r.doc.scrolledTo && r.doc.scrolledTo.attrs['data-file'] === 'Zebra.lvl');
        r.out().scrollTop = 77;
        const q = r.host.querySelector('.pk-q'); q.value = 'a'; q.dispatch('input'); r.fire(120);
        same('a new search starts at the top', r.out().scrollTop, 0);
        const n = rig();
        n.picker.open(''); await settle();
        n.out().scrollTop = 41;
        n.btn('Size').dispatch('click');
        same('with no chosen row a sort leaves the list where it was', n.out().scrollTop, 41);
    }
    // 12. the list that does not come, comes late, or fails
    {
        let signal = null, late = null;
        const r = rig({ answer: (init) => { signal = init.signal; return new Promise((resolve) => { late = () => resolve(response(RAW)); }); } });
        r.picker.open('');
        same('a list that does not come: loading', [r.picker.status(), /Loading/.test(r.out().textContent)], ['loading', true]);
        same('a try is given 12 s', r.timers.filter((t) => t.live).map((t) => t.ms), [12000]);
        r.fire(12000);
        await settle();
        same('then it is an error with Try again and the request was aborted', [r.picker.status(), !!r.host.querySelector('[data-retry]'), signal.aborted], ['error', true, true]);
        late(); await settle();
        same('an answer of the aborted try that comes anyway is ignored', [r.picker.status(), r.log.lists], ['error', 0]);
        r.setAnswer(() => Promise.resolve(response(RAW)));
        r.host.querySelector('[data-retry]').dispatch('click'); await settle();
        same('Try again fetches again and the list shows', [r.picker.status(), r.rows().length, r.log.fetches.length], ['ready', 7, 2]);
        check('the timer of a good try is gone', r.timers.filter((t) => t.live).length === 0);
        // reopening after an error retries by itself; the page's soft asks wait 15 s after a failure
        const q = rig({ answer: () => Promise.reject(new Error('offline')) });
        q.picker.open(''); await settle();
        same('an error', q.picker.status(), 'error');
        q.picker.load();
        same('the page asking for the list at once after a failure does not fetch again', q.log.fetches.length, 1);
        q.clock.now += 16000;
        q.picker.load();
        same('16 s later it does', q.log.fetches.length, 2);
        await settle();
        q.setAnswer(() => Promise.resolve(response(RAW)));
        q.picker.close(); q.picker.open('');
        await settle();
        same('opening the list after an error tries again by itself', [q.picker.status(), q.log.fetches.length, q.rows().length], ['ready', 3, 7]);
        // a list that is stuck in loading is not a lock: closing and opening again after the time ran out tries again
        const w = rig({ answer: () => new Promise(() => {}) });
        w.picker.open(''); w.fire(12000); await settle(); w.picker.close();
        w.setAnswer(() => Promise.resolve(response(RAW)));
        w.picker.open(''); await settle();
        same('a stuck list does not lock the page', [w.picker.status(), w.rows().length], ['ready', 7]);
    }
    // 13. only the host: the list does not open for anybody else and a choice made after the room changed hands is not sent
    {
        const r = rig();
        r.allow.open = false;
        r.picker.open('');
        same('a joiner\'s page opens nothing (no dialog, no fetch, no hook)', [r.overlay.hidden, r.picker.isOpen(), r.log.fetches.length, r.log.opened], [true, false, 0, 0]);
        r.allow.open = true;
        r.picker.open('TINY.LVL'); await settle();
        same('the host opens it, the page is told', [r.overlay.hidden, r.log.opened], [false, 1]);
        r.rows()[3].dispatch('click');
        r.allow.choose = false;                                                   // the leader left while the list was open
        r.btn('Choose map').dispatch('click');
        same('a choice made after the room changed hands closes the list and sends nothing', [r.overlay.hidden, r.log.chosen, r.log.closed], [true, [], 1]);
        r.allow.choose = true;
        r.picker.open('TINY.LVL'); await settle();
        r.rows()[3].dispatch('click'); r.btn('Choose map').dispatch('click');
        same('while the host the choice is sent', r.log.chosen, ['apple.lvl']);
    }
    // 14. Escape anywhere, the focus on a phone
    {
        const r = rig();
        r.picker.open(''); await settle();
        same('the list listens for Escape on the document while it is open', r.doc.listeners.keydown.length, 1);
        r.doc.activeElement = null;
        const ev = r.doc.dispatch('keydown', { key: 'Escape' });
        same('Escape with the focus on the body closes it', [r.overlay.hidden, ev.defaultPrevented, r.doc.listeners.keydown.length, r.log.closed], [true, true, 0, 1]);
        r.doc.dispatch('keydown', { key: 'Escape' });
        same('a closed list does not close again', r.log.closed, 1);
        const fine = rig(); fine.picker.open(''); await settle();
        check('on a screen with a mouse the search box has the focus', fine.doc.activeElement === fine.host.querySelector('.pk-q'));
        const coarse = rig({ coarse: true }); coarse.picker.open(''); await settle();
        check('on a touch screen the dialog has the focus and no keyboard comes up', coarse.doc.activeElement === coarse.host && coarse.doc.activeElement !== coarse.host.querySelector('.pk-q'));
    }
    console.log((failures ? 'FAILED: ' + failures + ' of ' : 'ok: ') + checks + ' checks');
    process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
