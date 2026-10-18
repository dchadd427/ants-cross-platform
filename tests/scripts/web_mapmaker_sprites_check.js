#!/usr/bin/env node
// The map maker's sprites (web/front/mapmaker_sprites.js, the pictures' part of mapmaker_view.js and mapmaker_canvas.js; docs/MAP_EDITOR.md, "Sprites") run against the REAL atlas that tools/map_atlas.py
// makes and a recording stand-in for the canvas, with no browser (the opt-in browser check looks at the real pixels):
//
//   node tests/scripts/web_mapmaker_sprites_check.js [--front DIR] [--atlas DIR]
//
//   --front DIR   the folder of the mapmaker_*.js files to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//   --atlas DIR   a folder made by tools/map_atlas.py (default: made here in a temporary folder)
//
//   A  loading: the index and the two sheets come in, the average colors replace the flat ones, a missing file, a bad index and a picture that does not load each leave the page flat (onFail)
//   B  a whole frame: every visible cell has its ground tile from the right place of the sheet, the cells tile the view without a gap, the objects come only from anchor cells, in row order,
//      each at its cell's corner plus the picture's own offset; hills and the other objects of the game's order; nothing is drawn for an id that has no picture
//   C  the pictures show from 10 pixels a cell up and not below; a frame without the atlas is the flat one
//   D  a pan moves the layer and paints only the strip that came into view; the same view again paints nothing; a stroke paints again only the part round its cells (with the margin that the
//      pictures reach) and not before; another map, a reset, a new size or a zoom beyond a third paints it all
//   E  a zoom that changes a little is the last picture stretched (one draw), and a crisp frame follows when it rests
//   F  a toy that is not in the sheet is fetched alone from /asset_catalog/sprites/ the first time, drawn when it is there, and a 404 draws nothing and breaks nothing
//   G  the starting ants: the ant and a ring in the team's color at 12 pixels a cell and up, a dot below; the ghost of a bigger object is see-through
//   H  chips: a picture fitted in a canvas for a ground tile and for an object, nothing for a tile that has none
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
let ATLAS = argv.includes('--atlas') ? path.resolve(argv[argv.indexOf('--atlas') + 1]) : '';
if (!ATLAS) {
    ATLAS = fs.mkdtempSync(path.join(os.tmpdir(), 'ants_atlas_'));
    const made = spawnSync(process.env.PYTHON || 'python3', [path.join(ROOT, 'tools', 'map_atlas.py'), ATLAS], { encoding: 'utf8' });
    if (made.status !== 0) { console.log('the atlas could not be made: ' + made.stdout + made.stderr); process.exit(1); }
}
const load = (name) => require(path.join(FRONT, name));
const File = load('mapmaker_file.js'), Tiles = load('mapmaker_tiles.js'), Sprites = load('mapmaker_sprites.js'), View = load('mapmaker_view.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }

const INDEX = JSON.parse(fs.readFileSync(path.join(ATLAS, 'atlas.json'), 'utf8'));
const EMPTY2 = 0x7FFE;

// ---- the stand-ins: a canvas that records what is drawn on it, images that load at once from the atlas folder or the repository's sprites ----
function pngSize(file) { const b = fs.readFileSync(file); return [b.readUInt32BE(16), b.readUInt32BE(20)]; }

function environment(opts) {
    opts = opts || {};
    const log = [], images = [], timers = [], clock = { t: 1000 };
    const state = { alpha: 1 };
    function makeCtx(owner) {
        const ctx = { owner, imageSmoothingEnabled: true, imageSmoothingQuality: 'low', fillStyle: '', strokeStyle: '', lineWidth: 1 };
        Object.defineProperty(ctx, 'globalAlpha', { get: () => state.alpha, set: (v) => { state.alpha = v; } });
        const stack = [];
        ctx.save = () => { stack.push(state.alpha); log.push({ op: 'save', owner, a: [] }); };
        ctx.restore = () => { if (stack.length) state.alpha = stack.pop(); log.push({ op: 'restore', owner, a: [] }); };
        for (const op of ['beginPath', 'rect', 'clip', 'fill', 'stroke', 'setTransform', 'ellipse', 'arc', 'moveTo', 'lineTo', 'strokeRect']) ctx[op] = (...a) => { log.push({ op, owner, a }); };
        ctx.fillRect = (...a) => { log.push({ op: 'fillRect', owner, a, fill: ctx.fillStyle }); };
        ctx.drawImage = (img, ...a) => {
            // an object's picture is scaled once into a canvas of its own and put on the layer as it is: the scaling is not a draw of the map, the put is (written as the draw of the whole picture that it stands for)
            if (img && img.url && /objects\.png|sprite_/.test(img.url) && a.length === 8 && a[4] === 0 && a[5] === 0 && owner.scaledFrom === undefined) { owner.scaledFrom = { img, src: a.slice(0, 4), size: [a[6], a[7]] }; return; }
            if (img && img.scaledFrom && a.length === 2) { log.push({ op: 'drawImage', owner, img: img.scaledFrom.img, a: img.scaledFrom.src.concat([a[0], a[1], img.scaledFrom.size[0], img.scaledFrom.size[1]]), alpha: state.alpha }); return; }
            log.push({ op: 'drawImage', owner, img, a, alpha: state.alpha });
        };
        ctx.createImageData = (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(4 * w * h) });
        ctx.putImageData = () => {}; ctx.getImageData = () => ({ data: [0, 0, 0, 0] });
        return ctx;
    }
    function makeCanvas() {
        const c = { width: 300, height: 150, style: {}, children: [], appendChild(n) { c.children.push(n); return n; } };
        const ctx = makeCtx(c);
        c.getContext = () => ctx;
        return c;
    }
    const doc = { createElement: (tag) => (tag === 'canvas' ? makeCanvas() : { tag, style: {}, children: [], appendChild(n) { this.children.push(n); return n; } }) };
    class Image {
        set src(url) {
            this.url = url; images.push(this);
            let size = null;
            const local = /mapmaker_atlas\/(.*)$/.exec(url), far = /\/asset_catalog\/sprites\/(sprite_\d+\.png)$/.exec(url);
            if (local && fs.existsSync(path.join(ATLAS, local[1]))) size = pngSize(path.join(ATLAS, local[1]));
            else if (far && !opts.farMissing && fs.existsSync(path.join(ROOT, 'asset_catalog', 'sprites', far[1]))) size = pngSize(path.join(ROOT, 'asset_catalog', 'sprites', far[1]));
            if (opts.imageFails && local && local[1] === opts.imageFails) size = null;
            this.pending = size;
        }
        get src() { return this.url; }
    }
    function settle() { for (const i of images.splice(0)) { if (i.pending) { i.width = i.pending[0]; i.height = i.pending[1]; if (i.onload) i.onload(); } else if (i.onerror) i.onerror(); } }
    const win = {
        devicePixelRatio: opts.dpr || 1, Image,
        performance: { now: () => clock.t },
        setTimeout: (f, ms) => { timers.push({ f, ms }); return timers.length; },
        fetch: (url) => {
            const name = url.replace(/^.*mapmaker_atlas\//, '');
            if (opts.fetchFails) return Promise.reject(new Error('offline'));
            if (opts.missing && name === opts.missing) return Promise.resolve({ ok: false, json: () => Promise.reject(new Error('404')) });
            const text = fs.readFileSync(path.join(ATLAS, name), 'utf8');
            return Promise.resolve({ ok: true, json: () => Promise.resolve(opts.badIndex ? Object.assign(JSON.parse(text), { v: 2 }) : JSON.parse(text)) });
        }
    };
    return { log, doc, win, settle, timers, clock, makeCanvas, images };
}

async function ready(opts) {
    const env = environment(opts), events = { ready: 0, fail: 0, change: 0 };
    const sprites = Sprites.create({ doc: env.doc, win: env.win, onReady: () => events.ready++, onFail: () => events.fail++, onChange: () => events.change++ });
    sprites.load();
    await new Promise((r) => setImmediate(r)); await new Promise((r) => setImmediate(r));
    env.settle();
    await new Promise((r) => setImmediate(r));
    return { env, sprites, events };
}

function treasure() { return File.parse(new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'TREASURE.LVL')))); }
function put2(f, r, c, id, flags) { const at = f.layer2 + 6 * (r * f.columns + c); f.bytes[at] = id & 255; f.bytes[at + 1] = id >> 8; f.bytes[at + 2] = flags; f.bytes[at + 3] = 0; }
function tile1(f, r, c) { const at = f.layer1 + 6 * (r * f.columns + c); return f.bytes[at] | (f.bytes[at + 1] << 8); }

// the frame of a view at `scale` over the cells (cx, cy), in a w x h canvas; returns the log of the calls that it made
function frame(rig, file, view, w, h, extra) {
    const ctx = rig.env.makeCanvas().getContext('2d');
    ctx.owner.name = 'main';
    rig.env.log.length = 0;
    const done = View.draw(ctx, view, file, w, h, Object.assign({ sprites: rig.sprites, dpr: 1, scratch: rig.env.makeCanvas() }, extra || {}));
    return { calls: rig.env.log.slice(), done, ctx };
}
const fromSheet = (calls, name) => calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url && c.img.url.indexOf(name) >= 0);
// a tile of the ground (drawn from a canvas: the scaled sheet or the atlas's own picture), the layer put on the canvas (2 numbers, or the whole of it stretched), a picture of an object
const isTile = (c, w, h) => c.op === 'drawImage' && c.a.length === 8 && !(c.img && c.img.width === c.a[2] && c.a[2] === w && c.a[3] === h && c.a[0] === 0 && c.a[1] === 0) && !(c.img && c.img.url);          // (the pictures that come from a file: objects, far toys, and the sheet while a tile is first scaled)
const isObject = (c) => c.op === 'drawImage' && c.img && c.img.url && /objects\.png/.test(c.img.url);

async function caseA() {
    const r = await ready();
    check(r.sprites.ready() && r.events.ready === 1 && r.events.fail === 0, 'the atlas loads: ready once, no failure');
    equal([r.sprites.FROM, r.sprites.ANT_FROM], [10, 12], 'pictures from 10 pixels a cell, the ant from 12');
    Tiles.setAverages('000000'.repeat(239), {});     // (put the flat colors away, then load again below to see the averages come from the atlas)
    const again = await ready();
    check(Tiles.css(485, EMPTY2) === '#' + INDEX.ground.avg.substr((485 - 431) * 6, 6), 'the average colors of the pictures replace the flat ones: ' + Tiles.css(485, EMPTY2));
    check(Tiles.css(0, 62) === '#' + ('00000' + INDEX.objects.items['62'][6].toString(16)).slice(-6) && Tiles.css(0, 410) === '#' + ('00000' + INDEX.far['410'][3].toString(16)).slice(-6), 'and those of the objects, in the sheet and fetched alone');
    check(again.sprites.ready(), 'ready again');
    for (const [what, opts, name] of [['the index is not there', { missing: 'atlas.json' }], ['the network is down', { fetchFails: true }], ['the index is of another version', { badIndex: true }], ['a sheet does not load', { imageFails: 'objects.png' }], ['the ground sheet does not load', { imageFails: 'ground.png' }]]) {
        const f = await ready(opts);
        check(!f.sprites.ready() && f.events.fail === 1 && f.events.ready === 0, what + ': onFail once, never ready');
        check(!View.useSprites({ scale: 20 }, f.sprites), what + ': the canvas keeps its flat colors');
    }
    const none = environment(); none.win.fetch = null;
    const s = Sprites.create({ doc: none.doc, win: none.win, onFail: () => { none.failed = true; } });
    s.load();
    check(none.failed === true && !s.ready(), 'a browser without fetch is a failure, not a crash');
}

async function caseB() {
    const rig = await ready(), f = treasure();
    put2(f, 3, 3, 99, 1);          // an id that has no picture (an anchor): nothing is drawn for it
    put2(f, 4, 3, 302, 0);         // a rock on a cell that is not an anchor: nothing is drawn
    const view = { scale: 20, cx: f.columns / 2, cy: f.rows / 2 }, w = 600, h = 400;
    View.clamp(view, f, w, h);
    const vis = View.visible(view, f, w, h), out = frame(rig, f, view, w, h);
    check(out.done.sprites === 1 && out.done.rects === 0 && out.done.pixels === 0, 'at 20 pixels a cell the frame is the pictures');
    const T = 20, g = INDEX.ground, ground = out.calls.filter((c) => c.op === 'drawImage' && c.a.length === 8 && c.a[2] === T && c.a[3] === T && c.owner !== out.ctx.owner);
    const cells = (vis.r1 - vis.r0 + 1) * (vis.c1 - vis.c0 + 1);
    check(ground.length === cells, 'every visible cell has its ground tile (' + ground.length + ' of ' + cells + ')');
    const x0 = w / 2 - view.cx * view.scale, y0 = h / 2 - view.cy * view.scale;
    let area = 0, wrong = 0;
    for (const c of ground) {
        const [sx, sy, sw, sh, dx, dy, dw, dh] = c.a;
        area += dw * dh;
        const col = Math.round((dx - Math.round(x0)) / T), row = Math.round((dy - Math.round(y0)) / T), id = tile1(f, row, col), k = id - g.first;
        if (k < 0 || sx !== (k % g.columns) * T || sy !== Math.floor(k / g.columns) * T) wrong++;
    }
    equal(wrong, 0, 'each one is the tile of its cell, from the right place of the sheet');
    check(Math.abs(area - cells * T * T) < 1, 'the cells tile the view with no gap and no overlap (' + area + ' pixels)');
    // the objects: only anchors, in row order, each at its cell's corner plus the picture's offset
    const all = out.calls.filter(isObject), drawn = all.filter((c) => !(c.a[2] === 23 && c.a[3] === 40 && c.a[0] === INDEX.objects.items[String(INDEX.ant)][0]));      // (the starting ants come last: their own check is G)
    const items = INDEX.objects.items, k = view.scale / 32;
    const expected = [];
    for (let r = Math.max(0, vis.r0 - 8); r <= Math.min(f.rows - 1, vis.r1 + 8); r++) for (let c = Math.max(0, vis.c0 - 8); c <= Math.min(f.columns - 1, vis.c1 + 8); c++) {
        const at = f.layer2 + 6 * (r * f.columns + c), id = f.bytes[at] | (f.bytes[at + 1] << 8);
        if (id === EMPTY2 || !(f.bytes[at + 2] & 1) || !items[id]) continue;
        const it = items[id], x = Math.round(Math.round(x0) + c * T + it[4] * k), y = Math.round(Math.round(y0) + r * T + it[5] * k);
        if (x > w || y > h || x + Math.round(it[2] * k) < 0 || y + Math.round(it[3] * k) < 0) continue;
        expected.push([it[0], it[1], it[2], it[3]]);
    }
    equal(drawn.map((c) => c.a.slice(0, 4)), expected, 'the objects: the anchored ones only, in row order, from the sheet');
    check(drawn.length > 10 && drawn.every((c) => c.a[6] === Math.round(c.a[2] * k) && c.a[7] === Math.round(c.a[3] * k)), 'each scaled like the cell, to whole pixels (' + drawn.length + ' objects)');
    const hills = drawn.filter((c) => c.a[2] === 128 && c.a[3] === 128);
    check(hills.length === 4, 'the four hills of Treasure, 128 x 128 each: ' + hills.length);
    // a cell's picture is at the cell's corner plus the offset: take the first anchored rock
    const rocks = [];
    for (let r = 0; r < f.rows; r++) for (let c = 0; c < f.columns; c++) { const at = f.layer2 + 6 * (r * f.columns + c); if ((f.bytes[at] | (f.bytes[at + 1] << 8)) === 302 && (f.bytes[at + 2] & 1)) rocks.push([r, c]); }
    check(rocks.length > 0, 'Treasure has a small rock to look at');
    const rock = rocks.find(([r, c]) => r >= vis.r0 && r <= vis.r1 && c >= vis.c0 && c <= vis.c1);
    if (rock) {
        const mine = drawn.filter((c) => c.a[0] === items['302'][0] && c.a[1] === items['302'][1] && c.a[4] === Math.round(Math.round(x0) + rock[1] * T + items['302'][4] * k) && c.a[5] === Math.round(Math.round(y0) + rock[0] * T + items['302'][5] * k));
        check(mine.length === 1, 'a small rock is at its cell\'s corner plus the offset of the picture (8, 8)');
    }
    check(!drawn.some((c) => c.a[4] === Math.round(x0) + 3 * T && c.a[5] === Math.round(y0) + 4 * T), 'a rock on a cell that is not an anchor is not drawn');
}

async function caseC() {
    const rig = await ready(), f = treasure(), w = 600, h = 400;
    for (const [scale, sprites] of [[48, true], [20, true], [12, true], [10, true], [9.99, false], [8, false], [6, false], [5, false], [3, false]]) {
        const view = { scale, cx: f.columns / 2, cy: f.rows / 2 }, out = frame(rig, f, view, w, h);
        check((out.done.sprites === 1) === sprites && (sprites ? out.done.rects + out.done.pixels === 0 : out.done.rects + out.done.pixels > 0), scale + ' pixels a cell: ' + (sprites ? 'pictures' : 'flat colors') + ' (' + JSON.stringify(out.done) + ')');
        const squares = out.calls.filter((c) => (c.op === 'fillRect' && c.fill === Tiles.FOOD) || c.op === 'strokeRect').length;
        check(sprites || scale < 6 ? squares === 0 : squares > 0, scale + ' pixels a cell: ' + (sprites ? 'no gold square for the food pile (the picture shows it)' : (scale >= 6 ? 'the food pile is its gold square' : 'no square at all')));
        if (sprites && scale === 20) {
            const T = 20, g = INDEX.ground, prepared = new Set(out.calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url && /ground\.png/.test(c.img.url)).map((c) => (c.a[5] / T) * g.columns + c.a[4] / T));
            const used = new Set(out.calls.filter((c) => isTile(c, w, h)).map((c) => (c.a[1] / T) * g.columns + c.a[0] / T));
            check(used.size > 3 && Array.from(used).every((k) => prepared.has(k)), 'every tile of the scaled sheet that is drawn from was scaled first (' + used.size + ' tiles)');
        }
    }
    const flat = Sprites.create({ doc: rig.env.doc, win: rig.env.win });
    const out = frame({ env: rig.env, sprites: flat }, f, { scale: 20, cx: 30, cy: 30 }, w, h);
    check(out.done.sprites === 0 && out.done.rects > 0, 'before the atlas is in (or when it is not), the frame is the flat one');
    const no = frame({ env: rig.env, sprites: null }, f, { scale: 20, cx: 30, cy: 30 }, w, h);
    check(no.done.sprites === 0 && no.done.rects > 0, 'and with no sprites object at all');
}

async function caseD() {
    const rig = await ready(), f = treasure(), w = 600, h = 400, T = 20;
    const view = { scale: T, cx: 30, cy: 30 };
    const first = frame(rig, f, view, w, h), isGround = (c) => isTile(c, w, h);
    const base = first.calls.filter(isGround).length;
    const same = frame(rig, f, view, w, h);
    check(same.calls.filter(isGround).length === 0 && same.calls.filter(isObject).filter((c) => c.a[2] !== 23).length === 0, 'the same view again paints no cell and no object (but the ants, which go over the layer): the layer is kept');
    check(same.calls.some((c) => c.op === 'drawImage' && c.a.length === 2), 'it only puts the layer on the canvas');
    // a pan of 7 pixels to the left (the map moves left): the strip on the right is new, the rest is the layer moved
    const panned = { scale: T, cx: 30 + 7 / T, cy: 30 };
    const moved = frame(rig, f, panned, w, h), strip = moved.calls.filter(isGround);
    check(strip.length > 0 && strip.length <= (Math.ceil(h / T) + 2) * 2, 'a pan paints only the strip that came into view: ' + strip.length + ' cells of ' + base);
    const shift = moved.calls.filter((c) => c.op === 'drawImage' && c.a.length === 2 && c.a[0] !== 0);
    check(shift.length === 1 && shift[0].a[0] === -7 && shift[0].a[1] === 0, 'after the layer was moved by 7 pixels: ' + JSON.stringify(shift.map((c) => c.a)));
    check(strip.every((c) => c.a[4] + c.a[6] > w - 7 - T - 1), 'all of those cells are at the strip on the right (give or take the cell beside it)');
    // a bigger pan: the exposed strips (37 pixels at the right, 29 at the bottom) are covered by the tiles that were painted, pixel for pixel, and nothing else was painted
    const big = frame(rig, f, { scale: T, cx: 30 + 44 / T, cy: 30 + 29 / T }, w, h), covered = new Uint8Array(w * h);
    for (const c of big.calls.filter(isGround)) for (let y = Math.max(0, c.a[5]); y < Math.min(h, c.a[5] + c.a[7]); y++) for (let x = Math.max(0, c.a[4]); x < Math.min(w, c.a[4] + c.a[6]); x++) covered[y * w + x] = 1;
    let holes = 0, extra = 0;
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const exposed = x >= w - 44 || y >= h - 29; if (exposed && !covered[y * w + x]) holes++; if (!exposed && covered[y * w + x] && x < w - 44 - T - 1 && y < h - 29 - T - 1) extra++; }
    check(holes === 0, 'a pan of 44 and 29 pixels: the strips that came into view are painted whole (' + holes + ' pixels left)');
    check(extra === 0, 'and the part that was only moved is not painted again (' + extra + ' pixels)');
    const back = frame(rig, f, { scale: T, cx: 30 + 7 / T, cy: 30 }, w, h);       // (return to the view of the next checks)
    check(back.calls.filter(isGround).length > 0, 'a pan back paints the strip on the other side');
    // the same pan, up and down
    const down = frame(rig, f, { scale: T, cx: 30 + 7 / T, cy: 30 + 9 / T }, w, h);
    check(down.calls.filter(isGround).length <= (Math.ceil(w / T) + 2) * 2 && down.calls.filter((c) => c.op === 'drawImage' && c.a.length === 2 && c.a[0] === 0 && c.a[1] === -9).length === 1, 'a pan down paints a strip along the bottom');
    // a stroke: only the part round the cell (the margin that the pictures reach: 8 cells), and not before
    const idx = 30 * f.columns + 30;
    rig.sprites.invalidate([idx]);
    const mended = frame(rig, f, { scale: T, cx: 30 + 7 / T, cy: 30 + 9 / T }, w, h), cellsMended = mended.calls.filter(isGround).length;
    check(cellsMended > 0 && cellsMended <= 21 * 21, 'a changed cell: only the part round it is painted again (' + cellsMended + ' cells)');
    const quiet = frame(rig, f, { scale: T, cx: 30 + 7 / T, cy: 30 + 9 / T }, w, h);
    check(quiet.calls.filter(isGround).length === 0, 'and the next frame paints nothing');
    rig.sprites.invalidate([0]);
    const far = frame(rig, f, { scale: T, cx: 30 + 7 / T, cy: 30 + 9 / T }, w, h);
    check(far.calls.filter(isGround).length === 0, 'a change far outside the view paints nothing');
    // a hill anchored 3 cells to the left of a changed cell reaches it (its picture is 4 cells wide, from one cell before its anchor): the mend paints the hill again
    let hill = null;
    for (let r = 0; r < f.rows && !hill; r++) for (let c = 0; c < f.columns && !hill; c++) { const at = f.layer2 + 6 * (r * f.columns + c), id = f.bytes[at] | (f.bytes[at + 1] << 8); if (id >= 245 && id <= 248 && (f.bytes[at + 2] & 1)) hill = [r, c]; }
    check(!!hill, 'Treasure has a hill');
    const near = frame(rig, f, { scale: T, cx: hill[1] + 0.5, cy: hill[0] + 0.5 }, w, h);                 // (the hill's anchor is in the middle of the view)
    rig.sprites.invalidate([hill[0] * f.columns + hill[1] + 3]);
    const hillAgain = frame(rig, f, { scale: T, cx: hill[1] + 0.5, cy: hill[0] + 0.5 }, w, h).calls.filter((c) => isObject(c) && c.a[2] === 128 && c.a[3] === 128);
    check(near.calls.filter((c) => isObject(c) && c.a[2] === 128).length >= 1 && hillAgain.length >= 1, 'a change next to a picture that reaches it paints the picture again, though its anchor is elsewhere (' + hillAgain.length + ' hill)');
    // another map, a reset, another size, a zoom beyond a third: everything
    const other = treasure();
    const again = frame(rig, other, { scale: T, cx: 30, cy: 30 }, w, h).calls.filter(isGround).length;
    check(again === base, 'another map paints it all (' + again + ' cells, as many as the first frame: ' + base + ')');
    rig.sprites.reset();
    check(frame(rig, other, { scale: T, cx: 30, cy: 30 }, w, h).calls.filter(isGround).length === base, 'a reset paints it all');
    check(frame(rig, other, { scale: T, cx: 30, cy: 30 }, w + 20, h).calls.filter(isGround).length > base, 'another size paints it all');
    rig.env.clock.t += 1000;
    const bigger = frame(rig, other, { scale: T * 1.5, cx: 30, cy: 30 }, w + 20, h);
    const bv = View.visible({ scale: T * 1.5, cx: 30, cy: 30 }, other, w + 20, h), seen = (bv.r1 - bv.r0 + 1) * (bv.c1 - bv.c0 + 1);
    check(bigger.calls.filter((c) => isTile(c, w + 20, h) && c.a[2] === 30).length === seen, 'a zoom beyond a third paints it all, with tiles of the new size (' + seen + ' cells)');
    // many changed cells at once
    const many = []; for (let i = 0; i < 70000; i++) many.push(i % 3600);
    rig.sprites.invalidate(many);
    check(frame(rig, other, { scale: T * 1.5, cx: 30, cy: 30 }, w + 20, h).calls.filter((c) => isTile(c, w + 20, h) && c.a[2] === 30).length > 100, 'a flood of changed cells is a whole frame, not a long list');
}

async function caseE() {
    const rig = await ready(), f = treasure(), w = 600, h = 400, T = 20;
    frame(rig, f, { scale: T, cx: 30, cy: 30 }, w, h);
    rig.env.clock.t += 20;
    const stretched = frame(rig, f, { scale: T * 1.1, cx: 30, cy: 30 }, w, h);
    const cells = stretched.calls.filter((c) => isTile(c, w, h)), whole = stretched.calls.filter((c) => c.op === 'drawImage' && c.a.length === 8 && c.a[0] === 0 && c.a[1] === 0 && c.a[2] === w && c.a[3] === h);
    check(cells.length === 0 && whole.length === 1 && Math.abs(whole[0].a[6] - w * 1.1) < 1e-6, 'a zoom of 10% is the last picture stretched by 1.1: one draw, no cell');
    check(rig.env.timers.length === 1 && rig.env.timers[0].ms > 140, 'and a crisp frame is asked for when it rests');
    rig.env.clock.t += 500;
    const crisp = frame(rig, f, { scale: T * 1.1, cx: 30, cy: 30 }, w, h);
    check(crisp.calls.filter((c) => isTile(c, w, h) && c.a[2] === 22).length > 100, 'after it rested the frame is made again, crisp (tiles of 22)');
    rig.env.clock.t += 20;
    const jump = frame(rig, f, { scale: T * 1.1 * 1.6, cx: 30, cy: 30 }, w, h);
    check(jump.calls.filter((c) => isTile(c, w, h) && c.a[2] === 35).length > 100, 'a zoom that is more than a third at once is never stretched');
}

async function caseF() {
    const rig = await ready(), f = treasure(), w = 600, h = 400;
    put2(f, 20, 25, 410, 1);               // flower1a: a toy that is not in the sheet
    put2(f, 21, 25, 99999 & 0xFFFF, 1);    // (nothing: no picture, not in the index)
    const view = { scale: 20, cx: 30, cy: 24 };
    check(!!INDEX.far['410'] && !INDEX.objects.items['410'], 'flower1a is fetched alone, not in the sheet');
    frame(rig, f, view, w, h);
    const mineUrl = '/asset_catalog/sprites/sprite_' + ('000' + INDEX.far['410'][0]).slice(-4) + '.png', asked = rig.env.images.filter((i) => i.url === mineUrl);
    check(asked.length === 1, 'the first frame asks for its picture: ' + mineUrl);
    const second = frame(rig, f, view, w, h);
    check(rig.env.images.filter((i) => i.url === mineUrl).length === 1 && second.calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url === mineUrl).length === 0, 'it is asked for once, and nothing is drawn in its place yet');
    rig.env.settle();
    check(rig.env.timers.length >= 0, 'the picture arrives');
    const third = frame(rig, f, view, w, h), mine = third.calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url === mineUrl);
    check(mine.length === 1, 'then it is drawn');
    if (mine.length) {
        const k = 20 / 32, far = INDEX.far['410'], x0 = Math.round(w / 2 - 30 * 20), y0 = Math.round(h / 2 - 24 * 20);
        check(mine[0].a[4] === Math.round(x0 + 25 * 20 + far[1] * k) && mine[0].a[5] === Math.round(y0 + 20 * 20 + far[2] * k) && mine[0].a[6] === Math.round(mine[0].img.width * k), 'at its cell\'s corner plus its offset, scaled like the cell');
    }
    // a 404: nothing is drawn, nothing breaks, and it is not asked for again
    const lost = await ready({ farMissing: true }), g = treasure();
    put2(g, 20, 25, 410, 1);
    frame(lost, g, view, w, h);
    lost.env.settle();
    lost.sprites.reset();
    const after = frame(lost, g, view, w, h);
    check(after.calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url === mineUrl).length === 0 && lost.env.images.filter((i) => i.url === mineUrl).length === 0, 'a missing picture draws nothing and is not asked for again');
}

async function caseG() {
    const rig = await ready(), f = treasure(), w = 600, h = 400;
    check(f.markers.length === 24, 'Treasure has 24 starting ants (' + f.markers.length + ')');
    const ant = INDEX.objects.items[String(INDEX.ant)];
    check(!!ant && ant[2] === 23 && ant[3] === 40 && ant[4] === -11 && ant[5] === -28, 'the ant is the standing worker (agst301): 23 x 40, offset -11, -28');
    const view = { scale: 24, cx: f.columns / 2, cy: f.rows / 2 };
    View.clamp(view, f, w, h);
    const vis = View.visible(view, f, w, h), inView = f.markers.filter((m) => m.row >= vis.r0 && m.row <= vis.r1 && m.column >= vis.c0 && m.column <= vis.c1);
    const big = frame(rig, f, view, w, h), ants = big.calls.filter((c) => c.op === 'drawImage' && c.a[0] === ant[0] && c.a[1] === ant[1] && c.a[2] === 23 && c.a[3] === 40);
    check(inView.length > 6 && ants.length === inView.length, 'at 24 pixels a cell each ant in view is drawn: ' + ants.length + ' of ' + inView.length);
    const rings = big.calls.filter((c) => c.op === 'ellipse');
    check(rings.length === inView.length, 'with a ring at its feet');
    const strokes = big.calls.filter((c) => c.op === 'stroke').length, blacks = inView.filter((m) => m.team === 'black').length;
    check(blacks > 0 && strokes === inView.length + blacks, 'in the color of its team, and the black one with a thin white edge (' + strokes + ' lines for ' + inView.length + ' rings and ' + blacks + ' black ones)');
    const small = frame(rig, f, { scale: 11, cx: f.columns / 2, cy: f.rows / 2 }, w, h);
    check(small.calls.filter((c) => c.op === 'drawImage' && c.a[0] === ant[0] && c.a[1] === ant[1] && c.a[2] === 23).length === 0 && small.calls.filter((c) => c.op === 'arc').length >= 20, 'below 12 pixels a cell the ants are dots in the team\'s color');
    const ghosted = frame(rig, f, view, w, h, { ghost: { id: 346, row: 30, column: 30 } }).calls.filter((c) => c.op === 'drawImage' && c.alpha < 1);
    check(ghosted.length === 1 && ghosted[0].a[2] === INDEX.objects.items['346'][2], 'the ghost of a bigger object is its own picture, see-through (' + (ghosted[0] || { alpha: 'none' }).alpha + ')');
    const plain = frame(rig, f, view, w, h, { ghost: null }).calls.filter((c) => c.op === 'drawImage' && c.alpha < 1);
    check(plain.length === 0, 'and nothing is see-through without one');
    check(frame(rig, f, view, w, h, { ghost: { id: 99, row: 1, column: 1 } }).calls.filter((c) => c.op === 'drawImage' && c.alpha < 1).length === 0, 'an id with no picture has no ghost');
}

async function caseH() {
    const rig = await ready({ dpr: 2 }), host = { style: {}, children: [], textContent: 'x', appendChild(n) { this.children.push(n); } };
    check(rig.sprites.chip(host, false, 487, 34) === true && host.children.length === 1 && host.children[0].width === 68 && host.children[0].style.width === '34px' && host.style.background === 'none', 'a ground chip: a canvas of 34 pixels, 68 behind it on a screen of two');
    let call = rig.env.log.filter((c) => c.op === 'drawImage').pop();
    equal(call.a, [((487 - 431) % 16) * 32, Math.floor((487 - 431) / 16) * 32, 32, 32, 0, 0, 68, 68], 'it holds the tile\'s picture');
    host.children = [];
    check(rig.sprites.chip(host, true, 346, 30) && host.children.length === 1, 'an object chip');
    call = rig.env.log.filter((c) => c.op === 'drawImage').pop();
    const it = INDEX.objects.items['346'], sc = Math.min(60 / it[2], 60 / it[3], 3);
    check(call.a[2] === it[2] && call.a[3] === it[3] && Math.abs(call.a[6] - it[2] * sc) < 1e-6 && Math.abs(call.a[7] - it[3] * sc) < 1e-6 && call.a[6] <= 60 && call.a[7] <= 60, 'fitted into the chip (a big bear is shrunk, not cut)');
    host.children = [];
    const rock = rig.sprites.chip(host, true, 302, 26) && rig.env.log.filter((c) => c.op === 'drawImage').pop();
    check(rock.a[6] === 13 * 3 && rock.a[7] === 13 * 3, 'a small picture is grown by one and a half, not more');
    check(rig.sprites.chip({ style: {}, appendChild() {} }, true, 99, 30) === false && rig.sprites.chip({ style: {}, appendChild() {} }, false, 300, 30) === false && rig.sprites.chip({ style: {}, appendChild() {} }, true, 410, 30) === false, 'no chip for a tile that has no picture in the sheet');
    const cold = Sprites.create({ doc: rig.env.doc, win: rig.env.win });
    check(cold.chip({ style: {}, appendChild() {} }, false, 485, 30) === false, 'and none before the atlas is in');
}

(async function () {
    for (const [name, fn] of [['A loading', caseA], ['B a whole frame', caseB], ['C pictures and flat colors', caseC], ['D the layer kept between frames', caseD], ['E the zoom', caseE], ['F a toy that is fetched alone', caseF], ['G the ants and the ghost', caseG], ['H chips', caseH]]) {
        const before = failures;
        console.log('RUNNING: ' + name + ' ...');
        try { await fn(); } catch (e) { failures++; console.log('    CRASHED: ' + (e && e.stack || e)); }
        console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
})();
