#!/usr/bin/env node
// Save as a new map and the drafts of the map maker (web/front/mapmaker_store.js, and the Save dialog and the name check of mapmaker_convert.js; docs/MAP_EDITOR.md) on a small fake of the DOM
// (tests/scripts/web_mapmaker_fake_dom.js) and a small fake of the browser's database. No browser: the real database is held to the same calls by the opt-in browser check.
//
//   node tests/scripts/web_mapmaker_save_check.js [--front DIR]
//
//   --front DIR  the folder of the mapmaker_*.js files (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the names (blanks and capitals do not count) and the list of the site's maps
//   B  the store on a fake database: the names of the drafts are read, a draft is added and never written over, the one slot of unsaved work, and a browser that keeps nothing
//   C  the site's list of maps: the six originals are always taken, the rest when the list has come, and a list that does not come is said
//   D  the Save dialog: the words of the pictures, a free name, a taken name (the open map's, a library map's, a draft's) with the one-click next name, what Save does and refuses
//   E  the same name check in Copy and Start an Extended map, and a default name that is taken
//   F  the default name of a Classic map made from an Extended one, when it is taken
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const path = require('path');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.resolve(__dirname, '..', '..', 'web', 'front');
const Store = require(path.join(FRONT, 'mapmaker_store.js')), Convert = require(path.join(FRONT, 'mapmaker_convert.js')), Dialog = require(path.join(FRONT, 'mapmaker_dialog.js'));
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }

// ---- a fake of the browser's database: what the store calls (open, object stores, add, put, get, delete, getAllKeys), answered at once ----
function fakeIdb(opts) {
    opts = opts || {};
    const stores = { drafts: new Map(), work: new Map() }, log = [];
    function request(fn) { const r = {}; setImmediate(() => { try { r.result = fn(); if (r.onsuccess) r.onsuccess(); } catch (e) { r.error = e; if (r.onerror) r.onerror({ preventDefault() {} }); } }); return r; }
    function objectStore(name, tx) {
        const m = stores[name];
        function job(fn) { const r = request(fn); tx.pending++; setImmediate(() => setImmediate(() => { if (--tx.pending === 0 && !tx.failed && tx.oncomplete) tx.oncomplete(); })); const onerror = () => { tx.failed = true; }; Object.defineProperty(r, 'onerror', { set(f) { this._f = (e) => { onerror(); f(e); }; }, get() { return this._f; } }); return r; }
        return {
            add(rec) { log.push('add ' + rec.key); return job(() => { if (opts.full) throw Object.assign(new Error('full'), { name: 'QuotaExceededError' }); if (m.has(rec.key)) throw Object.assign(new Error('exists'), { name: 'ConstraintError' }); m.set(rec.key, rec); }); },
            put(rec, key) { log.push('put ' + key); return job(() => { m.set(key, rec); }); },
            get(key) { return job(() => m.get(key)); },
            delete(key) { log.push('delete ' + key); return job(() => { m.delete(key); }); },
            getAllKeys() { return job(() => Array.from(m.keys())); }
        };
    }
    return {
        stores, log,
        open() {
            if (opts.throws) throw new Error('SecurityError');
            const r = {};
            setImmediate(() => {
                if (opts.blocked && r.onblocked) r.onblocked();
                if (opts.error) return r.onerror && r.onerror({});
                r.result = { transaction(name) { if (opts.txThrows) throw new Error('no'); const tx = { pending: 0, failed: false }; tx.objectStore = (n) => objectStore(n, tx); return tx; }, createObjectStore() {} };
                if (r.onupgradeneeded) r.onupgradeneeded();
                r.onsuccess();
            });
            return r;
        }
    };
}
const later = () => new Promise((r) => setTimeout(r, 15));
function opened(idb, fetchFn) { const store = Store.create({ idb, fetch: fetchFn || null }); return new Promise((res) => store.open(() => res(store))); }

// ---- A ----
function caseA() {
    equal([Store.clean('  Sunny   Picnic '), Store.key('  Sunny   PICNIC ')], ['Sunny Picnic', 'sunny picnic'], 'blanks round a name and between words count once, capitals not at all');
    equal(Object.keys(Store.libraryNames({ format: 1, maps: [{ file: 'POPcOrN.lvl' }, { file: 'TINY.LVL' }, { file: 5 }, null, { file: '!!!Bridges v4!!!.lvl' }] })).sort(), ['!!!bridges v4!!!', 'popcorn', 'tiny'], 'the names of the site\'s maps are the file names without .lvl');
    equal([Store.libraryNames(null), Store.libraryNames({ format: 2, maps: [] }), Store.libraryNames({ format: 1, maps: 5 })], [null, null, null], 'what is no list is none');
    equal([Store.key('Caf\u0065\u0301'), Store.key('Caf\u00e9')].every((k) => k === 'caf\u00e9'), true, 'a letter written in two ways is one name (NFC)');
    equal([Store.key('Sunny\u200bPicnic'), Store.key('\ufeffSunny Picnic\u200d'), Store.key('Sunny\u00a0\u00a0Picnic')], ['sunnypicnic', 'sunny picnic', 'sunny picnic'], 'zero-width characters count for nothing and a no-break space is a blank');
    equal([Convert.numbered('Heaven 0105', () => false), Convert.numbered('Heaven 0009', () => false), Convert.numbered('Name 9', () => false), Convert.numbered('Name 099', () => false), Convert.numbered('Name', () => false), Convert.numbered('Big 12345678901234567890', () => false)],
        ['Heaven 0106', 'Heaven 0010', 'Name 10', 'Name 100', 'Name 2', 'Big 12345678901234567890 2'], 'a number keeps its leading zeros, and a number too long to count is part of the name');
    equal(Store.ORIGINALS, ['tiny', 'small', 'medium', 'gauntlet', 'treasure', 'islands'], 'the six originals');
}

// ---- B ----
async function caseB() {
    const idb = fakeIdb();
    idb.stores.drafts.set('old one', { key: 'old one', name: 'Old One', bytes: new Uint8Array([1]) });
    let store = await opened(idb);
    equal([store.status(), store.has('OLD  one'), store.has('new one')], ['ready', true, false], 'the names of the drafts are read (the keys only), capitals and blanks do not count');
    const bytes = new Uint8Array([9, 8, 7]);
    let answer = await new Promise((res) => store.add('  New   One ', 'classic', bytes, res));
    equal(answer, '', 'a new draft is added');
    const rec = idb.stores.drafts.get('new one');
    check(rec && rec.name === 'New One' && rec.kind === 'classic' && Buffer.compare(Buffer.from(rec.bytes), Buffer.from(bytes)) === 0 && typeof rec.saved === 'number', 'with its name, kind, bytes and time');
    check(store.has('new one'), 'and its name is taken at once');
    answer = await new Promise((res) => store.add('NEW ONE', 'extended', new Uint8Array([0]), res));
    equal(answer, 'exists', 'a name that a draft has is refused');
    equal(Array.from(idb.stores.drafts.get('new one').bytes), [9, 8, 7], 'and the draft is not written over');
    answer = await new Promise((res) => store.add('Old One', 'classic', new Uint8Array([2]), res));
    equal([answer, Array.from(idb.stores.drafts.get('old one').bytes)], ['exists', [1]], 'a draft of an earlier visit is not written over either');
    equal(idb.log.filter((l) => /^put drafts|^put old|^put new/.test(l)).length, 0, 'the store never puts a draft (it only adds)');
    // a database that fails a write
    const full = fakeIdb({ full: true });
    store = await opened(full);
    answer = await new Promise((res) => store.add('Big', 'classic', bytes, res));
    equal([answer, store.has('Big')], ['failed', false], 'a write that fails is a failure, and the name is not taken');
    // a database that says blocked and then opens: the answer is one, the first
    let calls = 0;
    const late = Store.create({ idb: fakeIdb({ blocked: true }) });
    await new Promise((res) => late.open(() => { calls++; res(); }));
    await later();
    equal([calls, late.status()], [1, 'unavailable'], 'the answer of open comes once');
    // the words of the object's own prototype are not taken names
    const own = await opened(fakeIdb());
    equal(['constructor', 'toString', '__proto__', 'hasOwnProperty', 'valueOf'].map((n) => own.has(n)), [false, false, false, false, false], 'a name that is a word of Object is free');
    check(Object.getPrototypeOf(Store.libraryNames({ format: 1, maps: [] })) === null, 'the table of library names has no prototype');
    // the slot of unsaved work
    store = await opened(fakeIdb());
    equal(await new Promise((res) => store.readWork(res)), null, 'no work at first');
    await new Promise((res) => store.keepWork('Picnic', 'classic', bytes, res));
    let work = await new Promise((res) => store.readWork(res));
    check(work && work.name === 'Picnic' && work.kind === 'classic' && Array.from(work.bytes).join() === '9,8,7', 'the work is kept');
    await new Promise((res) => store.keepWork('Picnic', 'classic', new Uint8Array([5]), res));
    work = await new Promise((res) => store.readWork(res));
    equal(Array.from(work.bytes), [5], 'the slot holds the newest work (it is not a map)');
    check(!store.has('Picnic'), 'the work holds no name');
    await new Promise((res) => store.clearWork(res));
    equal(await new Promise((res) => store.readWork(res)), null, 'the work is cleared');
    // a browser that keeps nothing
    for (const [what, idbLike] of [['no database', null], ['a database that cannot be opened', fakeIdb({ throws: true })], ['one that errors', fakeIdb({ error: true })], ['one that has no transactions', fakeIdb({ txThrows: true })]]) {
        const s = await opened(idbLike);
        equal(s.status(), 'unavailable', what + ' is unavailable');
        const a = await new Promise((res) => s.add('X', 'classic', bytes, res));
        const w = await new Promise((res) => s.readWork(res));
        const k = await new Promise((res) => s.keepWork('X', 'classic', bytes, res));
        equal([a, w, k], ['failed', null, 'failed'], what + ': nothing is kept, and nothing throws');
        check(!s.has('X') && s.has('tiny'), what + ': the six originals are still taken');
    }
}

// ---- C ----
async function caseC() {
    let store = await opened(fakeIdb());
    equal([store.libraryState(), store.has('Treasure'), store.has('islands'), store.has('Popcorn')], ['failed', true, true, false], 'with no way to ask, the six originals are taken and the state says failed');
    const list = { format: 1, maps: [{ file: 'POPcOrN.lvl' }, { file: 'TINY.LVL' }] };
    let asked = [];
    store = await opened(fakeIdb(), (u) => { asked.push(u); return Promise.resolve({ ok: true, json: () => Promise.resolve(list) }); });
    equal(store.libraryState(), 'pending', 'before the list comes the state is pending');
    store.loadLibrary(); store.loadLibrary();
    await later();
    equal([asked, store.libraryState(), store.has('popcorn'), store.has('POPCORN'), store.has('Unknown')], [['/maps'], 'ready', true, true, false], 'the list is asked once, from the site itself, and its names are taken');
    equal(store.libraryList().map((m) => m.name), ['POPcOrN', 'Tiny'], 'libraryList exposes the parsed map entries');
    let fetched = null;
    store = await opened(fakeIdb(), (u) => Promise.resolve({ ok: true, arrayBuffer: () => Promise.resolve(new Uint8Array([8, 0, u.length]).buffer) }));
    await new Promise((res) => store.fetchMap('Mosh Pit.lvl', (err, b) => { fetched = [err, b && b[2]]; res(); }));
    equal(fetched, ['', '/maps/files/Mosh%20Pit.lvl'.length], 'fetchMap downloads /maps/files/<percent-encoded>');
    // asking: the answer comes, or does not come (the timer says so)
    let fired = [], timer = null;
    const waits = Store.create({ idb: null, fetch: () => new Promise(() => {}), later: (f, ms) => { timer = [f, ms]; } });
    let heard = 0;
    waits.loadLibrary();
    waits.onLibrary(() => heard++);
    equal([waits.libraryState(), heard, timer && timer[1]], ['pending', 0, 8000], 'while the list is awaited the state is pending and a timer of 8 seconds runs');
    timer[0]();
    equal([waits.libraryState(), heard], ['failed', 1], 'a list that never answers is a list that did not come, and who waited is told');
    let n = 0; waits.onLibrary(() => n++);
    equal(n, 1, 'asking after the answer is answered at once');
    let late = null;
    store = await opened(fakeIdb(), () => Promise.resolve({ ok: true, json: () => Promise.resolve(list) }));
    store = Store.create({ idb: null, fetch: () => Promise.resolve({ ok: true, json: () => Promise.resolve(list) }), later: (f) => { late = f; } });
    store.loadLibrary();
    await later();
    late();
    equal(store.libraryState(), 'ready', 'the timer that fires after the list came changes nothing');
    store = await opened(fakeIdb(), () => Promise.resolve({ ok: true, json: () => Promise.resolve(list) }));
    let got = 0; store.loadLibrary(); store.onLibrary(() => got++);
    await later();
    equal([store.libraryState(), got], ['ready', 1], 'a list that comes tells who waited, once');
    for (const [what, fn] of [['a 404', () => Promise.resolve({ ok: false, json: () => Promise.resolve(list) })], ['a network error', () => Promise.reject(new Error('x'))], ['a throw', () => { throw new Error('y'); }], ['a bad body', () => Promise.resolve({ ok: true, json: () => Promise.resolve({ nope: 1 }) })], ['bad json', () => Promise.resolve({ ok: true, json: () => Promise.reject(new Error('z')) })]]) {
        store = await opened(fakeIdb(), fn);
        store.loadLibrary();
        await later();
        equal([store.libraryState(), store.has('tiny'), store.has('popcorn')], ['failed', true, false], what + ': the list did not come, and the page goes on');
    }
}

// ---- D and E: the dialogs ----
function page(over) {
    const doc = makeDocument(), host = doc.createElement('div'), top = doc.createElement('header'), body = doc.createElement('div');
    [host, top, body].forEach((n) => doc.body.appendChild(n));
    const dialogs = Dialog.create(doc, host, [top, body]), taken = new Set(['sunny picnic 2', 'treasure']), made = [], kept = [], downloaded = [], done = [];
    const cur = { name: 'Sunny Picnic', bytes: new Uint8Array([1, 2, 3]), file: { rows: 31, columns: 31 }, kind: (over && over.kind) || 'classic', report: { headline: 'Works' } };
    const state = Object.assign({ canKeep: true, library: 'ready', keepAnswer: '', waiters: [] }, over);
    const env = {
        doc, dialogs, core: { convertToExtended() { return { status: 0, json: { ok: true, reasons: [], notes: [] }, bytes: new Uint8Array([7]) }; }, convertToClassic() { return { status: 0, json: { ok: true, reasons: [], notes: [] }, bytes: new Uint8Array([8]) }; }, readReport() { return { status: 0, json: { rows: 31, columns: 31 } }; } }, limits: { classic: { rows: 90, columns: 130, cells: 10000 }, extended: { side: 512, cells: 262144, longest: 1024 } },
        thumb: () => null, tags: (el, kind) => { el.textContent = kind; }, onMade: (b, n) => made.push(n), crop() {},
        current: () => cur, taken: (n) => Store.key(n) === Store.key(cur.name) || taken.has(Store.key(n)),
        canKeep: () => state.canKeep, libraryState: () => state.library, onLibrary: (f) => state.waiters.push(f), download: () => downloaded.push(1),
        keep: (name, cb) => { kept.push(name); if (state.keepAnswer === 'exists') taken.add(Store.key(name)); cb(state.keepAnswer, new Uint8Array([4, 5])); },
        onKept: (b, n) => kept.push('shown ' + n)
    };
    const convert = Convert.create(env);
    const q = (s) => host.querySelector(s), all = (s) => host.querySelectorAll(s);
    return {
        doc, host, dialogs, convert, made, kept, taken, downloaded, done, q, all, state,
        arrive(to) { state.library = to; state.waiters.splice(0).forEach((f) => f()); },
        title: () => (q('h2') ? q('h2').textContent : ''), button: (label) => all('button').filter((b) => b.textContent === label)[0],
        input: () => all('input').filter((i) => i.getAttribute('type') === 'text')[0],
        type(value) { const box = this.input(); box.value = value; box.fire('input', {}); }
    };
}

function caseD() {
    let p = page();
    p.convert.save(() => p.done.push(1));
    equal(p.title(), 'Save as a new map', 'the title of the picture');
    equal(p.q('.lead').textContent, 'Your changes are saved as a new map. The map you opened, “Sunny Picnic”, stays exactly as it was: no map is ever replaced.', 'the words of the picture');
    equal(p.q('.lead b').textContent, 'new map', 'new map is marked');
    equal(p.q('.cname label').textContent, 'Name of the new map', 'the label');
    equal(p.input().value, 'Sunny Picnic 3', 'the default is the next free number (2 is a draft)');
    check(/The name “Sunny Picnic 3” is free\./.test(p.q('.cfree').textContent), 'it says the name is free');
    equal(p.q('.cnever').textContent, 'The editor then shows the new map, so the next Save makes one more new map (“Sunny Picnic 4”).', 'the line about the next Save');
    equal(p.all('.dlg-foot button').map((b) => b.textContent), ['Cancel', 'Save as a new map'], 'the two buttons');
    check(!p.button('Save as a new map').disabled, 'a free name can be saved');
    check(p.doc.activeElement === p.input(), 'the name has the focus');
    // the open map's name, a draft's and a library map's are taken, in any case and with blanks
    p.type('sunny  PICNIC ');
    check(p.button('Save as a new map').disabled && p.input().getAttribute('aria-invalid') === 'true', 'the open map\'s name is refused');
    check(/A map called “sunny PICNIC” already exists\. A map is never replaced, so please pick another name\./.test(p.q('.note.bad').textContent), 'with the sentence of the picture: ' + p.q('.note.bad').textContent);
    check(p.button('Use “sunny PICNIC 3”') && p.q('.note.bad .btn'), 'and the next free name in one click (the picture: 3, as 2 is taken)');
    equal(p.q('.cnever').textContent, 'The editor then shows the new map, so the next Save makes one more new map (“sunny PICNIC 4”).', 'the line about the next Save names the offered name\'s next');
    p.type('Sunny Picnic 2');
    check(p.button('Save as a new map').disabled && !!p.button('Use “Sunny Picnic 3”'), 'a draft\'s name is refused');
    p.type('Treasure');
    check(p.button('Save as a new map').disabled && !!p.button('Use “Treasure 2”'), 'a library map\'s name is refused');
    p.type('');
    check(p.button('Save as a new map').disabled && /Give the new map a name\./.test(p.q('.note.bad').textContent) && !p.q('.note.bad .btn'), 'an empty name is refused, with no button');
    p.type('Sunny Picnic 2');
    p.button('Use “Sunny Picnic 3”').click();
    equal(p.input().value, 'Sunny Picnic 3', 'the button puts the name in the box');
    check(!p.button('Save as a new map').disabled && /is free/.test(p.q('.cfree').textContent) && !p.q('.note.bad') && p.doc.activeElement === p.input(), 'which is free, and the box has the focus again');
    p.button('Save as a new map').click();
    equal(p.kept, ['Sunny Picnic 3', 'shown Sunny Picnic 3'], 'Save keeps the map under the name and shows it');
    check(!p.dialogs.isOpen() && p.done.length === 1, 'the dialog closes and what waited goes on');
    // a name that became taken since the last key is checked again when Save is pressed
    p = page();
    p.convert.save();
    p.type('Later'); p.taken.add('later');
    p.button('Save as a new map').click();
    check(p.dialogs.isOpen() && p.kept.length === 0 && /already exists/.test(p.q('.note.bad').textContent), 'a name that was taken meanwhile is not saved');
    // a name that another window took first
    p = page({ keepAnswer: 'exists' });
    p.convert.save();
    p.type('Fresh');
    p.button('Save as a new map').click();
    check(p.dialogs.isOpen() && p.button('Save as a new map').disabled && /already exists/.test(p.q('.note.bad').textContent) && !!p.button('Use “Fresh 2”'), 'a name that was taken meanwhile is refused and the dialog stays');
    equal(p.kept, ['Fresh'], 'nothing is shown');
    // an error of the store
    p = page({ keepAnswer: 'The browser would not keep the map.' });
    p.convert.save(() => p.done.push(1));
    p.button('Save as a new map').click();
    check(p.title() === 'The map could not be saved' && /The browser would not keep the map\./.test(p.host.textContent) && p.done.length === 0 && p.kept.length === 1, 'a failure is said and nothing goes on');
    // Cancel and Escape
    p = page();
    p.convert.save(() => p.done.push(1));
    p.button('Cancel').click();
    check(!p.dialogs.isOpen() && p.kept.length === 0 && p.done.length === 0, 'Cancel saves nothing');
    p.convert.save(); p.doc.key('Escape');
    check(!p.dialogs.isOpen() && p.kept.length === 0, 'Escape saves nothing');
    // the list of the site's maps did not come
    p = page({ library: 'failed' });
    p.convert.save();
    check(/The list of the game’s maps could not be loaded, so a name that one of them has cannot be checked\./.test(p.host.textContent), 'the person is told when library names cannot be checked');
    p = page();
    p.convert.save();
    check(!/could not be loaded/.test(p.host.textContent), 'and not otherwise');
    // the list of the game's maps has not answered yet: no name is called free
    p = page({ library: 'pending' });
    p.convert.save(() => p.done.push(1));
    check(p.button('Save as a new map').disabled && /Checking the game\u2019s map names\u2026/.test(p.q('.cfree').textContent) && !/is free/.test(p.host.textContent), 'while the list is awaited Save is off and says it is checking');
    p.button('Save as a new map').click();
    equal(p.kept, [], 'and a press does nothing');
    p.type('Treasure');
    check(/already exists/.test(p.q('.note.bad').textContent), 'a name that is known to be taken is refused as before');
    p.type('Heaven 0105');
    check(p.button('Save as a new map').disabled, 'a name that may be a game map is not saved');
    p.arrive('ready');
    check(!p.button('Save as a new map').disabled && /is free/.test(p.q('.cfree').textContent), 'when the list has come the name is checked again and is free');
    p = page({ library: 'pending' });
    p.convert.save();
    p.arrive('failed');
    check(!p.button('Save as a new map').disabled && /could not be loaded/.test(p.host.textContent), 'when the list does not come the name is free and the note says it was not checked');
    // a browser that keeps nothing
    p = page({ canKeep: false });
    p.convert.save();
    equal(p.title(), 'This browser cannot keep maps', 'a browser that keeps nothing says so');
    check(!p.input() && !!p.button('Download') && !!p.button('Close'), 'with no name box, and a way to download');
    p.button('Download').click();
    equal(p.downloaded, [1], 'Download hands the map over');
    // a long name is cut by the box
    p = page();
    p.convert.save();
    equal(p.input().getAttribute('maxlength'), '40', 'a name has at most 40 characters');
}

function caseE() {
    let p = page();
    p.convert.copy();
    equal(p.input().value, 'Sunny Picnic 3', 'Copy offers the next free name too (a draft has 2)');
    p.type('Treasure');
    check(p.button('Make new map').disabled && !!p.button('Use “Treasure 2”'), 'Copy refuses a library name and offers the next');
    p.button('Use “Treasure 2”').click();
    check(!p.button('Make new map').disabled, 'one click makes it free');
    p.button('Make new map').click();
    equal(p.made, ['Treasure 2'], 'and the copy is made under it');
    p = page();
    p.convert.copy();
    p.type('Fresh'); p.taken.add('fresh');
    p.button('Make new map').click();
    check(p.dialogs.isOpen() && p.made.length === 0 && /already exists/.test(p.q('.note.bad').textContent), 'Copy checks the name again when Make new map is pressed');
    p = page({ library: 'pending' });
    p.convert.copy();
    check(p.button('Make new map').disabled && /Checking/.test(p.host.textContent), 'Copy waits for the list too');
    p.arrive('ready');
    check(!p.button('Make new map').disabled, 'and goes on when it has come');
    p = page();
    p.taken.add('sunny picnic extended');
    p.convert.startExtended();
    equal(p.input().value, 'Sunny Picnic Extended 2', 'the default name of an Extended map is the next free one when it is taken');
    p.type('Sunny Picnic 2');
    check(p.button('Make the new Extended map').disabled && !!p.q('.note.bad .btn'), 'a draft\'s name is refused for an Extended map');
    p.type('Plain');
    p.taken.add('plain');
    p.button('Make the new Extended map').click();
    check(p.dialogs.isOpen() && p.made.length === 0 && /already exists/.test(p.q('.note.bad').textContent), 'a name that became taken since the last key is checked again when Make is pressed');
}

function caseF() {
    const p = page({ kind: 'extended' });
    p.taken.add('sunny picnic classic');
    p.convert.convert();
    p.button('Make the new Classic map').click();
    equal(p.made, ['Sunny Picnic Classic 2'], 'the default name of a Classic map is the next free one when it is taken');
}

async function main() {
    const cases = [['A the names', caseA], ['B the store on a fake database', caseB], ['C the site\'s list of maps', caseC], ['D the Save dialog', caseD], ['E the name check in the other dialogs', caseE], ['F a Classic name that is taken', caseF]];
    for (const [name, fn] of cases) {
        const before = failures;
        try { await fn(); } catch (e) { failures++; console.log('    ERROR: ' + (e && e.stack || e)); }
        console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}
main();
