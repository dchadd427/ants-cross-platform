// The search and the filters of the map index (web/map_library.js, docs/MAP_PAGES.md) run on the real script with a small fake of the DOM: the page stays as it is until a control is used, the matching maps
// are drawn 48 to a page with a pager of their own, clearing every control puts the page back, "Most played" asks /maps once, and everything that came with the list is put in as text.
// Run by tests/scripts/test_map_pages.py (node); prints a line for every group and "N failed" at the end.
'use strict';
const fs = require('fs'), path = require('path');
const SOURCE = fs.readFileSync(path.join(__dirname, '..', '..', 'web', 'map_library.js'), 'utf8');
let failed = 0;
function check(ok, what) { if (!ok) { failed++; console.log('FAIL: ' + what); } }

class El {
    constructor(tag) { this.tagName = tag; this.children = []; this.attrs = {}; this.handlers = {}; this.className = ''; this.hidden = false; this.value = ''; this.parentNode = null; this._html = ''; this._text = ''; this.disabled = false;
        const classes = this; this.classList = { add: (c) => { classes.className = classes.className.split(' ').filter(Boolean).concat(c).join(' '); },
            toggle: (c, on) => { const has = classes.className.split(' ').indexOf(c) >= 0; if (on === undefined ? !has : on) { if (!has) classes.classList.add(c); } else classes.className = classes.className.split(' ').filter((x) => x !== c).join(' '); } }; }
    appendChild(c) { c.parentNode = this; this.children.push(c); return c; }
    set textContent(v) { this.children = []; this._text = String(v); }
    get textContent() { return this._text + this.children.map((c) => c.textContent).join(''); }
    set innerHTML(v) { this.children = []; this._text = ''; this._html = String(v); }
    get innerHTML() { return this._html; }
    addEventListener(type, fn) { (this.handlers[type] = this.handlers[type] || []).push(fn); }
    fire(type) { (this.handlers[type] || []).forEach((f) => f.call(this, { target: this })); }
    setAttribute(k, v) { this.attrs[k] = String(v); }
    getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
    scrollIntoView() { this.scrolled = true; }
    querySelectorAll(sel) { return (this.kinds || []).filter(() => sel === '[data-kind]'); }
    find(tag, cls) { const out = []; (function walk(n) { n.children.forEach((c) => { if ((!tag || c.tagName === tag) && (!cls || c.className.split(' ').indexOf(cls) >= 0)) out.push(c); walk(c); }); })(this); return out; }
}

function setup(rows, withPlayers) {
    const ids = {}, d = { createElement: (tag) => new El(tag), getElementById: (id) => ids[id] || null };
    for (const id of ['tools', 'list', 'count', 'pager', 'q', 'f-size', 'f-length', 'f-order', 'filters', 'list-h'].concat(withPlayers ? ['f-players'] : [])) { ids[id] = new El('x'); ids[id].parentNode = new El('label'); }
    ids.tools.hidden = true;
    ids.tools.kinds = ['', 'original', 'player'].map((k, i) => { const b = new El('button'); b.attrs['data-kind'] = k; b.attrs['aria-pressed'] = i === 0 ? 'true' : 'false'; return b; });
    ids.list._html = '<li>the static page</li>'; ids.count._text = '487 maps · 1 to 48'; ids.pager._html = '<a>1</a>';
    const asked = [];
    const sandbox = { document: d, window: { ANTS_MAP_LIBRARY: { format: 1, page: 48, players: withPlayers ? [2, 4] : [4], sizes: [['small', 31], ['medium', 60], ['large', null]], lengths: [['short', 10], ['middle', 15], ['long', null]], maps: rows } }, fetch: (url) => { asked.push(url); return Promise.resolve({ ok: true, json: () => Promise.resolve(sandbox.answer) }); }, Object, Math, Infinity, Array, String };
    sandbox.answer = { format: 1, maps: [] };
    new Function('document', 'window', 'fetch', 'Object', 'Math', 'Infinity', 'Array', 'String', SOURCE)(sandbox.document, sandbox.window, sandbox.fetch, Object, Math, Infinity, Array, String);
    return { ids, asked, sandbox, d };
}
const row = (slug, name, say, w, minutes, original, file, players) => [slug, name, say, w, w, minutes, '/maps/previews/' + slug + '.webp', original ? 1 : 0, file || name + '.lvl', players || 4];
const ROWS = [row('tiny', 'Tiny', 'Tiny map', 31, 6, true, 'TINY.LVL'), row('treasure', 'Treasure', "One person's trash...", 60, 12, true, 'TREASURE.LVL'), row('alley-ants', 'Alley Ants', 'Expert Map', 31, 12), row('war', 'WAR OF ALL ants', '', 31, 12),
              row('treasure-2', 'Treasure 2', 'the return of trez...', 60, 12), row('big', 'Big One', 'huge', 100, 20), row('mid', 'Middle', 'ok', 40, 15), row('evil', '<img src=x onerror=alert(1)>', '<b>bold</b>', 31, 10)];
function names(ctx) { return ctx.ids.list.find('a').map((a) => a.textContent); }
function type(ctx, text) { ctx.ids.q.value = text; ctx.ids.q.fire('input'); }
function pick(ctx, id, value) { ctx.ids[id].value = value; ctx.ids[id].fire('change'); }

// ---- A: nothing changes until a control is used
{
    const c = setup(ROWS, false);
    check(c.ids.tools.hidden === false, 'A the search and the filters appear when the list is there');
    check(c.ids.list.innerHTML === '<li>the static page</li>' && c.ids.count.textContent === '487 maps · 1 to 48' && c.ids.pager.innerHTML === '<a>1</a>', 'A the page is as it was before a control is used');
    check(c.asked.length === 0, 'A nothing is asked of the site before "Most played" is chosen');
    console.log('RUNNING: A the page stays as it is ... done');
}
// ---- B: the search, the kind, the size and the length
{
    const c = setup(ROWS, false);
    type(c, 'treasure');
    check(JSON.stringify(names(c)) === JSON.stringify(['Treasure', 'Treasure 2']), 'B the search finds a name: ' + names(c));
    check(c.ids.count.textContent === '2 maps', 'B the count says how many: ' + c.ids.count.textContent);
    type(c, 'ONE TRASH');
    check(JSON.stringify(names(c)) === JSON.stringify(['Treasure']), 'B the search reads the words of the line too, in any case, every word: ' + names(c));
    type(c, 'zzz');
    check(c.ids.list.find('li', 'empty').length === 1 && c.ids.count.textContent === '0 maps', 'B no match says so');
    type(c, '');
    check(c.ids.list.innerHTML === '<li>the static page</li>' && c.ids.count.textContent === '487 maps · 1 to 48' && c.ids.pager.innerHTML === '<a>1</a>', 'B clearing the search puts the page back');
    c.ids.tools.kinds[1].fire('click');
    check(JSON.stringify(names(c)) === JSON.stringify(['Tiny', 'Treasure']), 'B Original: ' + names(c));
    check(c.ids.tools.kinds[1].attrs['aria-pressed'] === 'true' && c.ids.tools.kinds[0].attrs['aria-pressed'] === 'false', 'B the pressed button is marked');
    c.ids.tools.kinds[2].fire('click');
    check(names(c).indexOf('Tiny') < 0 && names(c).length === 6, 'B Made by players: ' + names(c));
    c.ids.tools.kinds[0].fire('click');
    check(c.ids.list.innerHTML === '<li>the static page</li>', 'B All with nothing else is the page again');
    pick(c, 'f-size', 'small');
    check(JSON.stringify(names(c).sort()) === JSON.stringify(['<img src=x onerror=alert(1)>', 'Alley Ants', 'Tiny', 'WAR OF ALL ants']), 'B small (up to 31): ' + names(c));
    pick(c, 'f-size', 'medium');
    check(JSON.stringify(names(c).sort()) === JSON.stringify(['Middle', 'Treasure', 'Treasure 2']), 'B medium (up to 60, over 31): ' + names(c));
    pick(c, 'f-size', 'large');
    check(JSON.stringify(names(c)) === JSON.stringify(['Big One']), 'B large: ' + names(c));
    pick(c, 'f-size', '');
    pick(c, 'f-length', 'short');
    check(JSON.stringify(names(c).sort()) === JSON.stringify(['<img src=x onerror=alert(1)>', 'Tiny']), 'B up to 10 minutes: ' + names(c));
    pick(c, 'f-length', 'middle');
    check(JSON.stringify(names(c).sort()) === JSON.stringify(['Alley Ants', 'Middle', 'Treasure', 'Treasure 2', 'WAR OF ALL ants']), 'B 11 to 15: ' + names(c));
    pick(c, 'f-length', 'long');
    check(JSON.stringify(names(c)) === JSON.stringify(['Big One']), 'B over 15: ' + names(c));
    pick(c, 'f-length', 'middle'); pick(c, 'f-size', 'medium');
    check(JSON.stringify(names(c).sort()) === JSON.stringify(['Middle', 'Treasure', 'Treasure 2']), 'B the filters together: ' + names(c));
    console.log('RUNNING: B the search and the filters ... done');
}
// ---- C: the order
function C() {
    const c = setup(ROWS, false);
    pick(c, 'f-order', 'name');
    check(JSON.stringify(names(c)) === JSON.stringify(['Alley Ants', 'Big One', '<img src=x onerror=alert(1)>', 'Middle', 'Tiny', 'Treasure', 'Treasure 2', 'WAR OF ALL ants']), 'C Name, A to Z, the signs in front of a name not counted: ' + names(c));
    check(c.asked.length === 0, 'C a name order asks nothing of the site');
    c.sandbox.answer = { format: 1, maps: [{ file: 'treasure 2.lvl', plays: 9 }, { file: 'TREASURE.LVL', plays: 30 }, { file: 'x.lvl', plays: 'many' }, null] };
    pick(c, 'f-order', 'plays');
    check(c.asked.length === 1 && c.asked[0] === '/maps', 'C Most played asks the list of maps once: ' + c.asked);
    return Promise.resolve().then(() => Promise.resolve()).then(() => new Promise((r) => setTimeout(r, 5))).then(() => {
        check(JSON.stringify(names(c).slice(0, 3)) === JSON.stringify(['Treasure', 'Treasure 2', 'Tiny']), 'C the most played come first, then the order of the list: ' + names(c));
        const when = c.ids.list.find('span', 'when').map((s) => s.textContent);
        check(when[0].indexOf('30 plays') >= 0 && when[1].indexOf('9 plays') >= 0 && !/ \u00b7 \d+ plays?$/.test(when[2]), 'C the cards say the plays that came: ' + when.slice(0, 3));
        pick(c, 'f-order', 'name'); pick(c, 'f-order', 'plays');
        check(c.asked.length === 1, 'C it is asked once');
        console.log('RUNNING: C the order ... done');
    });
}
// ---- D: more than 48 maps, the pager, the text of a player, a list that does not load
function D() {
    const many = [];
    for (let i = 0; i < 100; i++) many.push(row('m' + i, 'Map ' + i, i === 0 ? '<script>alert(1)</script>' : 'line ' + i, 31, 12));
    const c = setup(many, true);
    pick(c, 'f-order', 'name');
    check(c.ids.list.find('li', 'm').length === 48, 'D 48 to a page: ' + c.ids.list.find('li', 'm').length);
    check(c.ids.count.textContent === '100 maps · 1 to 48', 'D the count: ' + c.ids.count.textContent);
    const buttons = c.ids.pager.find('button');
    const label = (b) => b.textContent;
    check(JSON.stringify(buttons.map(label)) === JSON.stringify(['‹ Back', '1', '2', '3', 'Next ›']), 'D the pager of page 1: ' + buttons.map(label));
    check(buttons[0].disabled === true && buttons[1].attrs['aria-current'] === 'page', 'D Back is off on the first page and the page is marked');
    buttons[4].fire('click');
    check(c.ids.list.find('li', 'm').length === 48 && c.ids.count.textContent === '100 maps · 49 to 96' && c.ids['list-h'].scrolled === true, 'D Next shows the next 48 and goes to the list: ' + c.ids.count.textContent);
    c.ids.pager.find('button')[3].fire('click');
    check(c.ids.list.find('li', 'm').length === 4 && c.ids.count.textContent === '100 maps · 97 to 100', 'D the last page has what is left: ' + c.ids.count.textContent);
    check(c.ids.pager.find('button').filter((b) => b.disabled).length === 2, 'D Next is off on the last page (and the page itself)');
    pick(c, 'f-order', 'name');
    check(c.ids.pager.find('button').some((b) => b.attrs['aria-current'] === 'page' && b.textContent === '1'), 'D a new choice starts at page 1');
    type(c, 'script');
    check(c.ids.list.find('li', 'm').length === 1, 'D the search reads the text of the line');
    const card = c.ids.list.find('li', 'm')[0];
    check(card.find('a')[0].textContent === 'Map 0' && card.find('p', 'say')[0].textContent === '<script>alert(1)</script>' && card.find('script').length === 0, 'D a line that looks like markup is text');
    check(card.find('a')[0].href === '/map/m0', 'D the card links to the page of the map: ' + card.find('a')[0].href);
    check(card.find('img')[0].src === '/maps/previews/m0.webp' && card.find('img')[0].alt === '', 'D the picture is the one in the list');
    type(c, ''); pick(c, 'f-order', '');
    pick(c, 'f-players', '4');
    check(c.ids.list.find('li', 'm').length === 48 && c.ids.count.textContent === '100 maps \u00b7 1 to 48', 'D the Players choice alone is a choice: ' + c.ids.count.textContent);
    pick(c, 'f-players', '2');
    check(c.ids.list.find('li', 'empty').length === 1, 'D the Players choice filters (no map has 2 players here)');
    const long = []; for (let i = 0; i < 500; i++) long.push(row('l' + i, 'Long ' + i, '', 31, 12));
    const d2 = setup(long, false); pick(d2, 'f-order', 'name');
    check(JSON.stringify(d2.ids.pager.children.map((x) => x.textContent)) === JSON.stringify(['\u2039 Back', '1', '2', '3', '\u2026', '11', 'of 11', 'Next \u203a']), 'D the pager of page 1 of 11 names the first three and the last: ' + d2.ids.pager.children.map((x) => x.textContent));
    const evil = setup(ROWS, false);
    type(evil, 'img');
    check(evil.ids.list.find('a').length === 1 && evil.ids.list.find('a')[0].textContent === '<img src=x onerror=alert(1)>' && evil.ids.list.find('img').length === 1, 'D a name that looks like markup is text: one picture, the map\'s own');
    console.log('RUNNING: D more than 48, the text of a player ... done');
}
// ---- E: the script does nothing on a page that is not the index, and a list that is not there
function E() {
    const calm = new Function('document', 'window', SOURCE);
    calm({ getElementById: () => null }, {});
    calm({ getElementById: () => null }, { ANTS_MAP_LIBRARY: { maps: [] } });
    const ids = {};
    for (const id of ['tools', 'list', 'count', 'pager']) ids[id] = new El('x');
    ids.tools.hidden = true;
    calm({ getElementById: (id) => ids[id] || null, createElement: (t) => new El(t) }, {});
    check(ids.tools.hidden === true, 'E without the list the filters stay hidden');
    console.log('RUNNING: E no list, no page ... done');
}
C().then(() => { D(); E(); console.log(failed + ' failed'); process.exit(failed ? 1 : 0); });
