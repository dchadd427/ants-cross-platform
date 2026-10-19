#!/usr/bin/env node
// My maps of the map maker (web/front/mapmaker_mine.js and the list and delete calls of mapmaker_store.js; docs/MAP_EDITOR.md) on a small fake of the DOM
// (tests/scripts/web_mapmaker_fake_dom.js) and a small fake of the browser's database. No browser: the real list, Open, Copy and Delete are held to the same calls by the opt-in browser check.
//
//   node tests/scripts/web_mapmaker_mine_check.js [--front DIR]
//
//   --front DIR  the folder of the mapmaker_*.js files (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the store: the saved maps newest first, a delete only from the drafts store (its name is free again), and a browser that keeps nothing
//   B  the list: a row for each saved map with its name, kind tags, size and check line, the filters and their counts, and the empty and unavailable states
//   C  Open and Copy hand the saved map to the page (nothing is written), and a map that cannot be read can only be deleted
//   D  Delete: the clear question first (the safe choice has the focus), Keep it changes nothing, Delete it deletes one map, and a refusal of the browser is said
//   E  Escape and Back, a list that is closed while it loads, a store that is not open yet
//   G  Delete takes the record's own key (a nameless record, a key that is not its name)
//   H  the tabs My maps and My submissions (the second is the player's submissions, given by the page: mapmaker_submit.js)
//   F  Copy of a saved map (not the open one): the source, the default name and the two ways of making the copy
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const path = require('path');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.resolve(__dirname, '..', '..', 'web', 'front');
const Store = require(path.join(FRONT, 'mapmaker_store.js')), Mine = require(path.join(FRONT, 'mapmaker_mine.js')), Convert = require(path.join(FRONT, 'mapmaker_convert.js')), Dialog = require(path.join(FRONT, 'mapmaker_dialog.js'));
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }

// a fake of the browser's database: object stores in maps, getAll, add, put, get, delete, getAllKeys (the log names the store of each call)
function fakeIdb(opts) {
    opts = opts || {};
    const stores = { drafts: new Map(), work: new Map() }, log = [];
    function request(fn) { const r = {}; setImmediate(() => { try { r.result = fn(); if (r.onsuccess) r.onsuccess(); } catch (e) { r.error = e; if (r.onerror) r.onerror({ preventDefault() {} }); } }); return r; }
    function objectStore(name, tx) {
        const m = stores[name];
        function job(fn) { const r = request(fn); tx.pending++; setImmediate(() => setImmediate(() => { if (--tx.pending === 0 && !tx.failed && tx.oncomplete) tx.oncomplete(); })); return r; }
        return {
            add(rec) { log.push('add ' + name + ' ' + rec.key); return job(() => { if (m.has(rec.key)) throw Object.assign(new Error('exists'), { name: 'ConstraintError' }); m.set(rec.key, rec); }); },
            put(rec, key) { log.push('put ' + name + ' ' + key); return job(() => { m.set(key, rec); }); },
            get(key) { return job(() => m.get(key)); },
            getAll() { log.push('getAll ' + name); return job(() => { if (opts.listFails) throw new Error('no'); return Array.from(m.values()); }); },
            delete(key) { log.push('delete ' + name + ' ' + key); return job(() => { if (opts.deleteFails) throw new Error('no'); m.delete(key); }); },
            getAllKeys() { return job(() => Array.from(m.keys())); }
        };
    }
    return {
        stores, log,
        open() {
            if (opts.throws) throw new Error('SecurityError');
            const r = {};
            setImmediate(() => {
                if (opts.error) return r.onerror && r.onerror({});
                r.result = { transaction() { const tx = { pending: 0, failed: false }; tx.objectStore = (n) => objectStore(n, tx); return tx; }, createObjectStore() {} };
                if (r.onupgradeneeded) r.onupgradeneeded();
                r.onsuccess();
            });
            return r;
        }
    };
}
const later = () => new Promise((r) => setTimeout(r, 15));
function opened(idb) { const store = Store.create({ idb, fetch: null }); return new Promise((res) => store.open(() => res(store))); }
function rec(name, kind, saved, bytes) { return { key: name.toLowerCase(), name, kind, saved, bytes: bytes || new Uint8Array([kind === 'extended' ? 9 : 8, 1, 2]) }; }

// ---- the page, as far as the list needs it ----
function page() {
    const doc = makeDocument(), els = {};
    const ids = { mine: 'section', 'mine-list': 'div', 'mine-status': 'p', 'mine-note': 'p', 'mine-chips': 'span', 'mine-new': 'button', 'mine-back': 'button', 'mine-title': 'h1', 'mine-tabs': 'div', 'mine-link': 'button' };
    Object.keys(ids).forEach((id) => { const n = doc.createElement(ids[id]); n.id = id; els[id] = n; doc.body.appendChild(n); });
    return { doc, els, $: (id) => els[id] };
}
function texts(node) { return node.all().filter((n) => n.tagName === 'button').map((b) => b.textContent); }
function button(node, text) { return node.all().find((n) => n.tagName === 'button' && n.textContent === text); }

// the environment of the list: the files are stand-ins (a first byte of 8 is a Classic map, of 9 an Extended map, of 0 a file that cannot be read)
function setup(store, extra) {
    const p = page(), queue = [], calls = [], shown = { n: 0, hidden: 0 };
    const dialogs = { opened: [], messages: [], open(spec) { this.opened.push(spec); const ctl = { close() {}, el: {} }; return ctl; }, message(t, m) { this.messages.push([t, m]); }, isOpen() { return !!this.open_; } };
    const env = Object.assign({
        doc: p.doc, $: p.$, store, dialogs,
        parse: (bytes) => (bytes[0] === 8 ? { kind: 'classic', rows: 31, columns: 31 } : bytes[0] === 9 ? { kind: 'extended', rows: 300, columns: 400 } : null),
        check: (bytes) => (bytes[1] === 1 ? { works: true, text: bytes[0] === 9 ? 'Works in the new version' : 'Works in the original game' } : { works: false, text: 'Does not work in the original game' }),
        thumb: () => p.doc.createElement('canvas'), tags: (host, kind) => { host.textContent = kind === 'extended' ? 'Extended map|New version only' : 'Classic map'; },
        later: (f) => queue.push(f),
        onOpen: (r) => calls.push(['open', r.name]), onCopy: (r) => calls.push(['copy', r.name]), onNew: () => calls.push(['new']), onBack: () => calls.push(['back']),
        onShow: () => { shown.n++; }, onHide: () => { shown.hidden++; }
    }, extra || {});
    const mine = Mine.create(env), waiting = () => store.status() === 'pending';
    return { p, mine, dialogs, calls, shown, flush() { const now = queue.splice(0); now.forEach((f) => f()); while (queue.length && !waiting()) queue.splice(0).forEach((f) => f()); } };
}
async function loaded(store, extra) { const s = setup(store, extra); s.mine.show(); await later(); s.flush(); return s; }
function names(s) { return s.p.els['mine-list'].children.map((r) => r.querySelector('.t1').textContent.split(/Classic|Extended/)[0].trim()); }

// ---- A ----
async function caseA() {
    const idb = fakeIdb();
    idb.stores.drafts.set('old', rec('Old', 'classic', 100));
    idb.stores.drafts.set('new', rec('New', 'extended', 300));
    idb.stores.drafts.set('mid', rec('Mid', 'classic', 200));
    idb.stores.work.set('current', { name: 'Work', bytes: new Uint8Array([1]) });
    const store = await opened(idb);
    let got = await new Promise((res) => store.list((e, l) => res([e, l])));
    equal([got[0], got[1].map((r) => r.name)], ['', ['New', 'Mid', 'Old']], 'the saved maps come newest first');
    equal(idb.log, ['getAll drafts'], 'the list reads the drafts store only');
    equal(store.has('mid'), true, 'a saved map\'s name is taken');
    const before = idb.stores.work.size;
    const answer = await new Promise((res) => store.remove('mid', res));
    equal([answer, Array.from(idb.stores.drafts.keys()).sort(), store.has('Mid'), idb.stores.work.size === before], ['', ['new', 'old'], false, true], 'a delete removes that one saved map frees its name and leaves the slot of unsaved work');
    equal(idb.log.filter((l) => /^delete/.test(l)), ['delete drafts mid'], 'and it is a delete from the drafts store only');
    equal(await new Promise((res) => store.remove('nobody', res)), '', 'deleting a name that is not there is not an error');
    equal(idb.log.filter((l) => /^delete/.test(l)), ['delete drafts mid', 'delete drafts nobody'], 'still only the drafts store');
    const bad = await opened(fakeIdb({ listFails: true, deleteFails: true }));
    got = await new Promise((res) => bad.list((e, l) => res([e, l])));
    equal(got, ['failed', []], 'a list that the browser refuses is a failure with nothing in it');
    equal(await new Promise((res) => bad.remove('x', res)), 'failed', 'a delete that the browser refuses is a failure');
    const none = Store.create({ idb: fakeIdb({ error: true }) });
    await new Promise((res) => none.open(res));
    equal([await new Promise((res) => none.list((e) => res(e))), await new Promise((res) => none.remove('x', res))], ['failed', 'failed'], 'a browser that keeps nothing fails both, and does not throw');
}

// ---- B ----
async function caseB() {
    const idb = fakeIdb();
    idb.stores.drafts.set('treasure copy', rec('Treasure copy', 'classic', 400));
    idb.stores.drafts.set('big lake', rec('Big Lake', 'extended', 300));
    idb.stores.drafts.set('gauntlet practice', rec('Gauntlet practice', 'classic', 200, new Uint8Array([8, 0, 0])));
    const s = await loaded(await opened(idb));
    const list = s.p.els['mine-list'];
    equal(names(s), ['Treasure copy', 'Big Lake', 'Gauntlet practice'], 'a row for each saved map, newest first');
    equal(s.p.els['mine'].hidden, false, 'the screen is shown');
    check(s.shown.n === 1, 'the page is told (it puts the editor away)');
    const first = list.children[1];
    equal([first.querySelector('.t1').textContent, first.querySelector('.t2').textContent, first.querySelector('.t3').textContent], ['Big Lake Extended map|New version only', '300 x 400 · saved in this browser', 'Works in the new version'], 'a row has the name, the kind tags, the size, and the line of the check');
    equal(list.children[2].querySelector('.t3').textContent, 'Does not work in the original game', 'a map that does not work says so');
    check(list.children[2].querySelector('.dot').className === 'dot bad' && first.querySelector('.dot').className === 'dot ok', 'with a red or a green dot');
    check(first.querySelector('.mthumb').children.length === 1, 'and the small picture of the map');
    equal(texts(s.p.els['mine-chips']), ['All (3)', 'Classic (2)', 'Extended (1)'], 'the filters count the kinds');
    equal(s.p.els['mine-chips'].children.map((c) => c.getAttribute('aria-pressed')), ['true', 'false', 'false'], 'All is chosen at first');
    s.p.els['mine-chips'].children[2].click();
    equal(names(s), ['Big Lake'], 'the Extended filter shows the Extended maps only');
    equal(s.p.els['mine-chips'].children.map((c) => c.getAttribute('aria-pressed')), ['false', 'false', 'true'], 'and is the one that is pressed');
    s.p.els['mine-chips'].children[1].click();
    equal(names(s), ['Treasure copy', 'Gauntlet practice'], 'the Classic filter shows the Classic maps');
    equal(s.p.els['mine-status'].textContent, '2 saved maps shown.', 'the count is said to a reader of the screen');
    // shown again, it starts with All
    s.mine.hide(); s.mine.show(); await later(); s.flush();
    equal([names(s).length, s.p.els['mine-chips'].children.map((c) => c.getAttribute('aria-pressed'))], [3, ['true', 'false', 'false']], 'a list that is opened again shows all the maps');
    // nothing saved
    const e = await loaded(await opened(fakeIdb()));
    equal([e.p.els['mine-list'].children.length, e.p.els['mine-chips'].hidden, /no saved maps yet/.test(e.p.els['mine-note'].textContent)], [0, true, true], 'nothing saved: a plain sentence and no filters');
    // no Extended map: its filter says so
    const one = fakeIdb(); one.stores.drafts.set('a', rec('A', 'classic', 1));
    const o = await loaded(await opened(one));
    o.p.els['mine-chips'].children[2].click();
    equal([o.p.els['mine-list'].children.length, /No extended maps/i.test(o.p.els['mine-note'].textContent)], [0, true], 'a filter with no map says so');
    // a browser that keeps nothing
    const nothing = Store.create({ idb: null });
    await new Promise((res) => nothing.open(res));
    const u = await loaded(nothing);
    equal([u.p.els['mine-list'].children.length, /keeps no saved maps/.test(u.p.els['mine-note'].textContent), /Download/.test(u.p.els['mine-note'].textContent)], [0, true, true], 'a browser that keeps nothing says so, and points to Download');
    // a browser that will not list
    const f = Store.create({ idb: fakeIdb({ listFails: true }) });
    await new Promise((res) => f.open(res));
    const fl = await loaded(f);
    equal(/would not show your saved maps/.test(fl.p.els['mine-note'].textContent), true, 'a list that fails is said, not shown as empty');
}

// ---- C ----
async function caseC() {
    const idb = fakeIdb();
    idb.stores.drafts.set('good', rec('Good', 'classic', 200));
    idb.stores.drafts.set('broken', rec('Broken', 'classic', 100, new Uint8Array([0, 0, 0])));
    const s = await loaded(await opened(idb));
    const [good, broken] = s.p.els['mine-list'].children;
    button(good, 'Open').click(); button(good, 'Copy…').click();
    equal(s.calls, [['open', 'Good'], ['copy', 'Good']], 'Open and Copy give the saved map to the page');
    check(button(good, 'Open').getAttribute('aria-label') === 'Open Good' && button(good, 'Delete…').getAttribute('aria-label') === 'Delete Good', 'each button names its map for a reader of the screen');
    equal(texts(broken), ['Delete…'], 'a map that cannot be read can only be deleted');
    equal(broken.querySelector('.t1').textContent, 'Broken ', 'and it wears no kind tag, because its kind is not known');
    equal(broken.querySelector('.t2').textContent, 'This saved map cannot be read.', 'and says why');
    check(idb.log.every((l) => /^getAll/.test(l)), 'showing the list writes nothing');
    s.p.els['mine-new'].click(); s.p.els['mine-back'].click();
    equal(s.calls.slice(2), [['new'], ['back']], 'New map and Back to the map go to the page');
    // a saved map whose bytes cannot be read, newest of all: the page's picture maker throws on a missing map, and the rows after it still get their picture and check line
    const idb2 = fakeIdb();
    idb2.stores.drafts.set('broken', rec('Broken', 'classic', 300, new Uint8Array([0, 0, 0])));
    idb2.stores.drafts.set('good', rec('Good', 'classic', 200));
    const tdoc = makeDocument();
    const t = await loaded(await opened(idb2), { thumb: (file) => { if (!file) throw new TypeError('Cannot read properties of null'); return tdoc.createElement('canvas'); } });
    const [bad2, good2] = t.p.els['mine-list'].children;
    equal([good2.querySelector('.mthumb').children.length, /Works in the original game/.test(good2.querySelector('.t3').textContent), bad2.querySelector('.mthumb').children.length], [1, true, 0], 'an unreadable saved map gets no picture, and the next row is still filled in');
}

// ---- D ----
async function caseD() {
    const idb = fakeIdb();
    idb.stores.drafts.set('one', rec('One', 'classic', 200));
    idb.stores.drafts.set('two', rec('Two', 'extended', 100));
    idb.stores.work.set('current', { name: 'Work', bytes: new Uint8Array([1]) });
    const s = await loaded(await opened(idb));
    button(s.p.els['mine-list'].children[0], 'Delete…').click();
    const spec = s.dialogs.opened[0], body = s.p.doc.createElement('div');
    spec.build(body);
    equal(idb.log.filter((l) => /^delete/.test(l)), [], 'nothing is deleted by the first click');
    equal([spec.title, spec.buttons.map((b) => b.id), spec.buttons[0].ghost], ['Delete this saved map?', ['keep', 'delete'], true], 'a question first, and the safe choice is the first button (it gets the focus)');
    check(/“One” will be removed from this browser/.test(body.textContent) && /cannot be brought back/.test(body.textContent) && /game’s own maps are never touched/.test(body.textContent), 'it says which map, that it cannot be brought back and that the game\'s maps are never touched: ' + body.textContent);
    check(!spec.buttons[0].onClick, 'Keep it does nothing');
    equal(s.p.els['mine-list'].children.length, 2, 'so both maps are still there');
    spec.buttons[1].onClick({});
    await later();
    equal([Array.from(idb.stores.drafts.keys()), idb.log.filter((l) => /^delete/.test(l)), idb.stores.work.has('current')], [['two'], ['delete drafts one'], true], 'Delete it deletes that one map from the drafts, and the work slot stays');
    equal([names(s), texts(s.p.els['mine-chips']), s.p.els['mine-status'].textContent], [['Two'], ['All (1)', 'Classic (0)', 'Extended (1)'], '“One” was deleted.'], 'the row goes, the counts follow, and the deletion is said');
    // the browser refuses
    const refuse = fakeIdb({ deleteFails: true });
    refuse.stores.drafts.set('x', rec('X', 'classic', 1));
    const r = await loaded(await opened(refuse));
    button(r.p.els['mine-list'].children[0], 'Delete…').click();
    r.dialogs.opened[0].buttons[1].onClick({});
    await later();
    equal([names(r), r.dialogs.messages.map((m) => m[0])], [['X'], ['The map could not be deleted']], 'a delete that the browser refuses leaves the map in the list and says so');
    // the last one
    button(s.p.els['mine-list'].children[0], 'Delete…').click();
    s.dialogs.opened[1].buttons[1].onClick({});
    await later();
    equal([s.p.els['mine-list'].children.length, /no saved maps yet/.test(s.p.els['mine-note'].textContent)], [0, true], 'the last map deleted leaves the empty state');
}

// ---- E ----
async function caseE() {
    const idb = fakeIdb();
    idb.stores.drafts.set('a', rec('A', 'classic', 1));
    const s = await loaded(await opened(idb));
    s.p.doc.key('Escape');
    equal(s.calls, [['back']], 'Escape goes back to the map');
    s.dialogs.open_ = true;
    s.p.doc.key('Escape');
    equal(s.calls.length, 1, 'but not while a question is open (Escape closes that)');
    s.dialogs.open_ = false;
    s.mine.hide();
    s.p.doc.key('Escape');
    equal([s.calls.length, s.p.els['mine'].hidden, s.shown.hidden, s.mine.isOpen()], [1, true, 1, false], 'a list that is closed hears no key, and the page is told');
    // closed while it loads: the answer is not shown
    const slow = setup(await opened(idb));
    slow.mine.show(); slow.mine.hide();
    await later(); slow.flush();
    equal(slow.p.els['mine-list'].children.length, 0, 'a list that was closed before the answer came shows nothing');
    // the store is not open yet: the list waits and then shows
    const late = Store.create({ idb: fakeIdb() });
    const l = setup(late);
    l.mine.show();
    l.flush();
    equal([l.p.els['mine-list'].children.length, l.p.els['mine-note'].textContent], [0, 'Looking at your saved maps…'], 'a store that is not open yet is waited for (the page says it looks)');
    await new Promise((res) => late.open(res));
    l.flush(); await later(); l.flush();
    equal([l.p.els['mine-list'].children.length, /no saved maps yet/.test(l.p.els['mine-note'].textContent)], [0, true], 'then the list is shown');
}

// ---- F ----
function caseF() {
    function setupCopy(kind) {
        const doc = makeDocument(), host = doc.createElement('div'), top = doc.createElement('header');
        doc.body.appendChild(host); doc.body.appendChild(top);
        const made = [], seen = [], taken = new Set(['open map', 'draft']);
        const given = { name: 'Draft', bytes: new Uint8Array([8, 8]), file: { rows: 40, columns: 50 }, kind, report: { headline: 'Works' } };
        const open = { name: 'Open map', bytes: new Uint8Array([1, 1]), file: { rows: 31, columns: 31 }, kind: 'classic', report: { headline: 'Works' } };
        const core = {
            convertToExtended(bytes) { seen.push('extended ' + bytes[0]); return { status: 0, json: { ok: true, reasons: [], notes: [] }, bytes: new Uint8Array([7]) }; },
            convertToClassic(bytes) { seen.push('classic ' + bytes[0]); return { status: 0, json: { ok: true, reasons: [], notes: [] }, bytes: new Uint8Array([6]) }; },
            readReport() { return { status: 0, json: { rows: 40, columns: 50 } }; }
        };
        const dialogs = Dialog.create(doc, host, [top]);
        const convert = Convert.create({ doc, dialogs, core, limits: { classic: { rows: 90, columns: 130, cells: 10000 }, extended: { cells: 262144 } }, thumb: () => null, tags: (el, k) => { el.textContent = k; }, onMade: (b, n) => made.push([Array.from(b), n]), crop() {}, current: () => open, taken: (n) => taken.has(Store.key(n)), canKeep: () => true });
        return { convert, given, host, made, seen, doc, button: (t) => host.querySelectorAll('button').filter((b) => b.textContent === t)[0], input: () => host.querySelectorAll('input').filter((i) => i.getAttribute('type') === 'text')[0] };
    }
    let c = setupCopy('classic');
    c.convert.copy(c.given);
    equal([c.host.querySelector('.sn').textContent, c.input().value], ['Draft', 'Draft 2'], 'Copy of a saved map starts from that map (not the open one), under the next free name');
    c.button('Make new map').click();
    equal(c.made, [[[8, 8], 'Draft 2']], 'the same kind is the saved map\'s own bytes under the new name');
    c = setupCopy('classic');
    c.convert.copy(c.given);
    const other = c.host.querySelectorAll('input').filter((i) => i.getAttribute('type') === 'radio')[1];
    other.checked = true; other.fire('change', {});
    c.button('Next: what changes').click();
    check(/Draft \(40 x 50\)/.test(c.host.querySelector('.lead').textContent), 'Start an Extended map names the saved map: ' + c.host.querySelector('.lead').textContent);
    c.button('Make the new Extended map').click();
    equal([c.seen, c.made], [['extended 8'], [[[7], 'Draft Extended']]], 'and the saved map\'s bytes go to the core, not the open map\'s');
    c = setupCopy('extended');
    c.convert.copy(c.given);
    equal(c.host.querySelector('.sn').textContent, 'Draft', 'an Extended saved map is the source too');
}

// ---- G: the key of the record is what is deleted, never one worked out from its name ----
async function caseG() {
    const idb = fakeIdb();
    idb.stores.drafts.set('foo', rec('Foo', 'classic', 300));
    idb.stores.drafts.set('mismatch-key', Object.assign(rec('Foo', 'classic', 200), { key: 'mismatch-key' }));
    idb.stores.drafts.set('undefined', rec('undefined', 'classic', 100));
    idb.stores.drafts.set('Odd  KEY', Object.assign(rec('Odd', 'classic', 400), { key: 'Odd  KEY' }));
    idb.stores.drafts.set('odd', rec('Odd', 'classic', 399));
    idb.stores.drafts.set('nameless', { key: 'nameless', saved: 50, bytes: new Uint8Array([8, 1, 2]) });
    const s = await loaded(await opened(idb));
    const row = (i) => s.p.els['mine-list'].children[i];
    equal(texts(row(5)), ['Delete\u2026'], 'a record with no name is an unreadable row: Delete only');
    button(row(5), 'Delete\u2026').click();
    const body = s.p.doc.createElement('div');
    s.dialogs.opened[0].build(body);
    check(!/undefined/.test(body.textContent) && /This unreadable saved map will be removed/.test(body.textContent), 'its question does not say undefined: ' + body.textContent);
    check(!/undefined/.test(button(row(5), 'Delete\u2026').getAttribute('aria-label')), 'nor does its button');
    s.dialogs.opened[0].buttons[1].onClick({});
    await later();
    equal(Array.from(idb.stores.drafts.keys()).sort(), ['Odd  KEY', 'foo', 'mismatch-key', 'odd', 'undefined'].sort(), 'the nameless record is the one deleted, not a map called undefined');
    button(row(3), 'Delete\u2026').click();
    s.dialogs.opened[1].buttons[1].onClick({});
    await later();
    equal(Array.from(idb.stores.drafts.keys()).sort(), ['Odd  KEY', 'foo', 'odd', 'undefined'].sort(), 'a row whose key differs from its name deletes its own record, not the one its name points at');
    button(row(0), 'Delete\u2026').click();
    s.dialogs.opened[2].buttons[1].onClick({});
    await later();
    equal(Array.from(idb.stores.drafts.keys()).sort(), ['foo', 'odd', 'undefined'], 'a key with capitals and blanks is deleted as it is stored, not as a name would be cleaned');
}

// ---- H: the tabs: My maps and My submissions ----
async function caseH() {
    const idb = fakeIdb();
    idb.stores.drafts.set('a', rec('A', 'classic', 100));
    const shown = [], hidden = [], uis = [];
    const sent = { count: () => 3, show: (ui) => { shown.push(1); uis.push(ui); ui.list.textContent = 'the submissions'; ui.count(4); }, hide: () => hidden.push(1), preload: () => shown.push('preload') };
    const s = setup(await opened(idb), { sent });
    s.mine.show(); await later(); s.flush();
    const tabs = s.p.els['mine-tabs'];
    equal([tabs.hidden, texts(tabs)], [false, ['My maps (1)', 'My submissions (3)']], 'two tabs, with their counts, when the page has the submissions');
    equal([shown, s.p.els['mine-title'].textContent], [['preload'], 'My maps'], 'the list of maps first, and the other part is asked to load');
    button(tabs, 'My submissions (3)').click();
    equal([shown.length, s.p.els['mine-title'].textContent, s.p.els['mine-new'].hidden, s.p.els['mine-chips'].hidden], [2, 'My submissions', true, true], 'the second tab shows the submissions and puts the maps\' own buttons away');
    equal(texts(tabs), ['My maps (1)', 'My submissions (4)'], 'the count of the submissions is the one that the list says');
    check(s.p.els['mine-list'].textContent === 'the submissions', 'the list is the submissions\'');
    check(uis[0].link === s.p.els['mine-link'], 'with the button for the status link');
    button(tabs, 'My maps (1)').click(); await later(); s.flush();
    equal([s.p.els['mine-title'].textContent, s.p.els['mine-new'].hidden, s.p.els['mine-link'].hidden, names(s)], ['My maps', false, true, ['A']], 'back to the maps');
    s.mine.hide();
    equal(hidden.length, 1, 'closing the screen tells the submissions');
    s.mine.show('sent'); await later();
    equal([s.p.els['mine-title'].textContent, shown.length], ['My submissions', 4], 'it can open on the second tab');
    const bare = setup(await opened(fakeIdb()));
    bare.mine.show(); await later(); bare.flush();
    check(bare.p.els['mine-tabs'].hidden === true, 'a page with no submissions has no tabs');
    bare.mine.show('sent'); await later(); bare.flush();
    equal(bare.p.els['mine-title'].textContent, 'My maps', 'and cannot open on one');
}

(async () => {
    const parts = [['A the store: list and delete', caseA], ['B the list, the filters and the empty states', caseB], ['C Open, Copy and a map that cannot be read', caseC], ['D Delete and its question', caseD], ['E Escape, closing and waiting', caseE], ['F Copy of a saved map', caseF], ['G Delete by the record\'s own key', caseG], ['H the tabs', caseH]];
    for (const [name, fn] of parts) {
        const was = failures;
        try { await fn(); } catch (e) { failures++; console.log('    ERROR: ' + (e && e.stack || e)); }
        console.log('RUNNING: ' + name + ' ... ' + (failures === was ? 'PASS' : 'FAIL'));
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
})();
