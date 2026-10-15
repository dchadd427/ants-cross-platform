// Runs the OWN code of the match pages for the map pictures (web/history_page.js: AntsHistory.pictures: node, forget, lookMaps) with a small fake of the browser's DOM and of GET /maps, and the real
// AntsMapPicker. tests/scripts/test_web_matches.py runs it with node (the quick tier). usage: node web_history_pictures_check.js web/history_page.js web/front/map_picker.js
'use strict';
const path = require('path');
const [historyPath, pickerPath] = process.argv.slice(2).map((p) => path.resolve(p));
const P = require(pickerPath);
let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }
const tick = () => new Promise((r) => setImmediate(r));
async function settle() { for (let i = 0; i < 10; i++) await tick(); }

class Node {
    constructor(tag) { this.tag = tag; this.children = []; this.parentNode = null; this.className = ''; this.listeners = {}; this._text = ''; }
    appendChild(c) { c.parentNode = this; this.children.push(c); return c; }
    set textContent(t) { this.children.forEach((c) => { c.parentNode = null; }); this.children = []; this._text = t; }
    get textContent() { return this._text; }
    addEventListener(n, f) { (this.listeners[n] = this.listeners[n] || []).push(f); }
    fire(n) { (this.listeners[n] || []).forEach((f) => f()); }
    contains(n) { for (let x = n; x; x = x.parentNode) if (x === this) return true; return false; }
}

// the pictures' own code (web/history_page.js: AntsHistory.pictures), run with the real AntsMapPicker and AntsReplay's rule for the six, on a fake DOM
const H = require(historyPath);

function rig(mapsAnswer) {
    const body = new Node('body');
    const log = { fetched: [] };
    const document = { body, createElement: (t) => new Node(t) };
    const R = { mapKey: (m) => { const n = String(m).replace(/\.lvl$/i, '').toUpperCase(); return ['TINY', 'SMALL', 'MEDIUM', 'GAUNTLET', 'TREASURE', 'ISLANDS'].includes(n) ? n.toLowerCase() : ''; } };
    const fetch = (url) => { log.fetched.push(url); return mapsAnswer === 'fail' ? Promise.reject(new Error('down')) : Promise.resolve({ ok: true, json: () => Promise.resolve(mapsAnswer) }); };
    const pics = H.pictures({ document, replay: R, picker: P, fetch });
    const api = { pictureNode: (map, group) => pics.node(map, group), forgetPictures: (group) => pics.forget(group), count: () => pics.count(), lookMaps: () => pics.lookMaps() };
    return { api, body, log };
}
const img = (tv) => tv.children[0];
const attach = (r, tv) => { r.body.appendChild(tv); return tv; };

(async () => {
    {
        const r = rig({ format: 1, maps: [{ file: 'POPcOrN.lvl', thumb: '/maps/previews/POPcOrN.webp' }] });
        const a = attach(r, r.api.pictureNode('MEDIUM.LVL', 'list'));
        same('an original keeps its own picture, no list is asked for', [img(a).src, img(a).loading, r.log.fetched], ['front/preview_medium.png', undefined, []]);
        const b = attach(r, r.api.pictureNode('Pop Water!!!.lvl', 'list'));
        same('a map of the players gets the address its name makes, lazily', [img(b).tag, img(b).src, img(b).loading, img(b).decoding, img(b).alt], ['img', '/maps/previews/Pop%20Water%21%21%21.webp', 'lazy', 'async', '']);
        same('the list is asked for with the first such map, once', r.log.fetched, ['/maps']);
        r.api.pictureNode('POPcOrN.lvl', 'live');
        same('... and not again', r.log.fetched.length, 1);
        same('the squares of the maps of the players are kept (an original is not)', r.api.count(), 2);
    }
    {
        const r = rig({ format: 1, maps: [{ file: 'POPcOrN.lvl', thumb: '/maps/previews/POPcOrN.webp' }, { file: 'NoPic.lvl' }, { file: 'Same.lvl', thumb: '/maps/previews/Same.webp' }] });
        const a = attach(r, r.api.pictureNode('popcorn.lvl', 'list'));
        const n = attach(r, r.api.pictureNode('NoPic.lvl', 'list'));
        const s = attach(r, r.api.pictureNode('Same.lvl', 'list'));
        const first = img(s);
        same('before the list: the address of the name', [img(a).src, img(n).src], ['/maps/previews/popcorn.webp', '/maps/previews/NoPic.webp']);
        await settle();
        same('the list corrects a record in another case', img(a).src, '/maps/previews/POPcOrN.webp');
        same('... and a map that the server has no picture for becomes the dark square', [n.children[0].tag, n.children[0].className], ['div', 'blank']);
        check('a square whose address is right is left alone', img(s) === first);
    }
    {
        const r = rig('fail');
        const a = attach(r, r.api.pictureNode('Some Map.lvl', 'list'));
        await settle();
        same('a list that cannot be had leaves the address of the name', img(a).src, '/maps/previews/Some%20Map.webp');
        const i = img(a);
        i.fire('error');
        same('a picture that fails leaves the dark square and no image', [a.children.length, a.children[0].tag, a.children[0].className], [1, 'div', 'blank']);
    }
    {
        const r = rig({ format: 1, maps: [] });
        const a = attach(r, r.api.pictureNode('a/b.lvl', 'list'));
        const c = attach(r, r.api.pictureNode('Käse.lvl', 'list'));
        same('a name that fails the rules is the dark square at once, no image', [a.children[0].className, c.children[0].className, a.children.length], ['blank', 'blank', 1]);
    }
    {
        const r = rig({ format: 1, maps: [] });
        for (let round = 0; round < 50; round++) { r.api.forgetPictures('live'); attach(r, r.api.pictureNode('Live One.lvl', 'live')); }
        for (let round = 0; round < 5; round++) { r.api.forgetPictures('list'); attach(r, r.api.pictureNode('Old One.lvl', 'list')); attach(r, r.api.pictureNode('Old Two.lvl', 'list')); }
        same('a live box that is built every 10 seconds keeps one square, a list its own two', r.api.count(), 3);
        r.body.textContent = '';                                                  // (a box that was emptied without being built again)
        await settle();
        same('the squares that are no longer on the page are dropped when the list comes', r.api.count(), 0);
        attach(r, r.api.pictureNode('Again.lvl', 'list'));
        r.body.textContent = '';
        r.api.forgetPictures('list');
        r.api.forgetPictures('live');
        same('forgetting a box forgets all its squares', r.api.count(), 0);
    }
    console.log((failures ? 'FAILED: ' + failures + ' of ' : 'ok: ') + checks + ' checks');
    process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
