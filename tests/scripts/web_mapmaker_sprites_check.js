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
//   C  the pictures show at every zoom (the ground only as its smooth average color under 4 device pixels a cell or with more than 12,000 cells in view, the objects always pictures); a frame without the atlas is the flat one
//   D  a pan moves the layer and paints only the strip that came into view; the same view again paints nothing; a stroke paints again only the part round its cells (with the margin that the
//      pictures reach) and not before; another map, a reset, a new size or a zoom beyond a third paints it all
//   E  a zoom that changes a little is the last picture stretched (one draw), and a crisp frame follows when it rests
//   F  a toy that is not in the sheet is fetched alone from /asset_catalog/sprites/ the first time, drawn when it is there, and a 404 draws nothing and breaks nothing
//   G  the starting ants: the ant and a ring in the team's color at 12 pixels a cell and up, a dot below, each ant facing the way its cell says (J), the ghost of a bigger object is see-through
//   H  chips: a picture fitted in a canvas for a ground tile and for an object, nothing for a tile that has none
//   J  the facing of a start ant: made up from its cell alone (1 to 7: never north, evenly spread, the same every time), the picture of that facing from the sheet in the ant's type and team color,
//      the west three turned round about the ant's middle (one pixel right of the mirrored origin, as the game's blitter does), nothing of it saved in the map, an atlas without the facings draws the south picture
//   I  the open map's dictionary: a tile is the picture of the animation that its entry names (as the game's renderer does), "." draws nothing, a changed name paints the layer again, the flat colors, the
//      ghost of a bigger object, a plant, a pile and a thing picked up, and the ground tile of the edges' before and after (tests/scripts/web_mapmaker_names_check.js holds the rule to the 540 maps)
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
const File = load('mapmaker_file.js'), Tiles = load('mapmaker_tiles.js'), Sprites = load('mapmaker_sprites.js'), View = load('mapmaker_view.js'), Blocks = load('mapmaker_blocks.js'), Edit = load('mapmaker_edit.js');

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
    const state = { alpha: 1, tx: 0, flip: 1 };
    function makeCtx(owner) {
        const ctx = { owner, imageSmoothingEnabled: true, imageSmoothingQuality: 'low', fillStyle: '', strokeStyle: '', lineWidth: 1 };
        Object.defineProperty(ctx, 'globalAlpha', { get: () => state.alpha, set: (v) => { state.alpha = v; } });
        const stack = [];
        ctx.save = () => { stack.push([state.alpha, state.tx, state.flip]); log.push({ op: 'save', owner, a: [] }); };
        ctx.restore = () => { if (stack.length) [state.alpha, state.tx, state.flip] = stack.pop(); log.push({ op: 'restore', owner, a: [] }); };
        ctx.translate = (x) => { state.tx += x * state.flip; };          // (only a turn round about a vertical line is made: translate, then scale(-1, 1))
        ctx.scale = (x) => { state.flip *= x; };
        for (const op of ['beginPath', 'rect', 'clip', 'fill', 'stroke', 'setTransform', 'ellipse', 'arc', 'moveTo', 'lineTo', 'strokeRect']) ctx[op] = (...a) => { log.push({ op, owner, a }); };
        ctx.fillRect = (...a) => { log.push({ op: 'fillRect', owner, a, fill: ctx.fillStyle }); };
        ctx.drawImage = (img, ...a) => {
            // an object's picture is scaled once into a canvas of its own and put on the layer as it is: the scaling is not a draw of the map, the put is (written as the draw of the whole picture that it stands for)
            if (img && img.url && /objects\.png|sprite_/.test(img.url) && a.length === 8 && a[4] === 0 && a[5] === 0 && owner.scaledFrom === undefined) { owner.scaledFrom = { img, src: a.slice(0, 4), size: [a[6], a[7]] }; return; }
            if (img && img.scaledFrom && a.length === 2) { log.push({ op: 'drawImage', owner, img: img.scaledFrom.img, a: img.scaledFrom.src.concat([a[0], a[1], img.scaledFrom.size[0], img.scaledFrom.size[1]]), alpha: state.alpha }); return; }
            log.push({ op: 'drawImage', owner, img, a, alpha: state.alpha, tx: state.tx, flip: state.flip });
        };
        ctx.createImageData = (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(4 * w * h) });
        ctx.putImageData = (img, x, y) => { log.push({ op: 'putImageData', owner, img, a: [x, y] }); }; ctx.getImageData = () => ({ data: [0, 0, 0, 0] });
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
            return Promise.resolve({ ok: true, json: () => Promise.resolve(opts.badIndex ? Object.assign(JSON.parse(text), { v: 2 }) : opts.oldIndex ? (() => { const j = JSON.parse(text); delete j.faces; delete j.antFaces; return j; })() : opts.noBomber ? (() => { const j = JSON.parse(text); delete j.faces[String(j.antTypes['64'].id)]; return j; })() : JSON.parse(text)) });
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
function setName(f, id, name) { const at = f.dictAt + 2 + 11 * id; for (let k = 0; k < 11; k++) f.bytes[at + k] = k < name.length ? name.charCodeAt(k) : 0; }
function put2(f, r, c, id, flags) { const at = f.layer2 + 6 * (r * f.columns + c); f.bytes[at] = id & 255; f.bytes[at + 1] = id >> 8; f.bytes[at + 2] = flags; f.bytes[at + 3] = 0; }
function tile1(f, r, c) { const at = f.layer1 + 6 * (r * f.columns + c); return f.bytes[at] | (f.bytes[at + 1] << 8); }

// the frame of a view at `scale` over the cells (cx, cy), in a w x h canvas; returns the log of the calls that it made
function frameOf(rig, file, view, w, h, extra) {
    const ctx = rig.env.makeCanvas().getContext('2d');
    ctx.owner.name = 'main';
    rig.env.log.length = 0;
    const done = View.draw(ctx, view, file, w, h, Object.assign({ sprites: rig.sprites, dpr: 1, scratch: rig.env.makeCanvas() }, extra || {}));
    return { calls: rig.env.log.slice(), done, ctx };
}
function frame(rig, file, view, w, h, extra) { return frameOf(rig, file, view, w, h, extra); }
const fromSheet = (calls, name) => calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url && c.img.url.indexOf(name) >= 0);
// a tile of the ground (drawn from a canvas: the scaled sheet or the atlas's own picture), the layer put on the canvas (2 numbers, or the whole of it stretched), a picture of an object
const isTile = (c, w, h) => c.op === 'drawImage' && c.a.length === 8 && !(c.img && c.img.width === c.a[2] && c.a[2] === w && c.a[3] === h && c.a[0] === 0 && c.a[1] === 0) && !(c.img && c.img.url);          // (the pictures that come from a file: objects, far toys, and the sheet while a tile is first scaled)
const isObject = (c) => c.op === 'drawImage' && c.img && c.img.url && /objects\.png/.test(c.img.url);
// the pictures of the standing ants of every type, facing and team color (where each is in the sheet), and a draw of one of them
const ANT_SOURCES = new Set();
for (const id of [String(INDEX.ant)].concat(Object.keys(INDEX.faces).map(String)).concat(Object.values(INDEX.faces).reduce((a, b) => a.concat(b), []).map(String)).concat(Object.values(INDEX.antTypes).map((t) => String(t.id)))) {
    ANT_SOURCES.add(INDEX.objects.items[id][0] + ',' + INDEX.objects.items[id][1]);
    for (const team of ['blue', 'red', 'green']) { const at = (INDEX.antFaces[id] || (id === String(INDEX.ant) ? INDEX.ants : null) || Object.values(INDEX.antTypes).find((t) => String(t.id) === id) || {})[team]; if (at) ANT_SOURCES.add(at[0] + ',' + at[1]); }
}
const isAnt = (c) => isObject(c) && ANT_SOURCES.has(c.a[0] + ',' + c.a[1]);

async function caseA() {
    const r = await ready();
    check(r.sprites.ready() && r.events.ready === 1 && r.events.fail === 0, 'the atlas loads: ready once, no failure');
    equal([Sprites.TINY, Sprites.MAX_TEXTURED, Sprites.ANT_FROM], [4, 12000, 12], 'smooth ground under 4 pixels a cell or over 12,000 cells, the ant from 12 pixels');
    Tiles.setAverages('000000'.repeat(239), {});     // (put the flat colors away, then load again below to see the averages come from the atlas)
    const again = await ready();
    check(Tiles.css(485, EMPTY2) === '#' + INDEX.ground.avg.substr((485 - 431) * 6, 6), 'the average colors of the pictures replace the flat ones: ' + Tiles.css(485, EMPTY2));
    check(Tiles.css(0, 62) === '#' + ('00000' + INDEX.objects.items['62'][6].toString(16)).slice(-6) && Tiles.css(0, 410) === '#' + ('00000' + INDEX.objects.items['410'][6].toString(16)).slice(-6) && Tiles.css(0, 347) === '#' + ('00000' + INDEX.far['347'][3].toString(16)).slice(-6), 'and those of the objects, in the sheet and fetched alone');
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
    // the objects: only anchors, in row order, each at its cell's corner plus the picture's offset, followed by the top-layer plants of Block 1
    const all = out.calls.filter(isObject), drawn = all.filter((c) => !isAnt(c));      // (the starting ants come last: their own check is G)
    const items = INDEX.objects.items, k = view.scale / 32;
    const expected = [];
    for (let r = Math.max(0, vis.r0 - 8); r <= Math.min(f.rows - 1, vis.r1 + 8); r++) for (let c = Math.max(0, vis.c0 - 8); c <= Math.min(f.columns - 1, vis.c1 + 8); c++) {
        const at = f.layer2 + 6 * (r * f.columns + c), id = f.bytes[at] | (f.bytes[at + 1] << 8);
        if (id === EMPTY2 || !(f.bytes[at + 2] & 1) || !items[id]) continue;
        const it = items[id], x = Math.round(Math.round(x0) + c * T + it[4] * k), y = Math.round(Math.round(y0) + r * T + it[5] * k);
        if (x > w || y > h || x + Math.round(it[2] * k) < 0 || y + Math.round(it[3] * k) < 0) continue;
        expected.push([it[0], it[1], it[2], it[3]]);
    }
    for (const p of f.plants || []) {
        if (p.row < Math.max(0, vis.r0 - 8) || p.row > Math.min(f.rows - 1, vis.r1 + 8) || p.column < Math.max(0, vis.c0 - 8) || p.column > Math.min(f.columns - 1, vis.c1 + 8)) continue;
        const it = items[p.id];
        if (!it) continue;
        const x = Math.round(Math.round(x0) + (p.column + 0.5) * T + it[4] * k), y = Math.round(Math.round(y0) + (p.row + 0.5) * T + it[5] * k);
        if (x > w || y > h || x + Math.round(it[2] * k) < 0 || y + Math.round(it[3] * k) < 0) continue;
        expected.push([it[0], it[1], it[2], it[3]]);
    }
    equal(drawn.map((c) => c.a.slice(0, 4)), expected, 'the objects: the anchored ones only, in row order, then the top-layer plants, from the sheet');
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
    // a food pile put by the editor lives in block 2 while its layer-2 cell stays empty: its stage picture is drawn at 10 px/cell and up, and its gold mark on the small map
    let cur = treasure();
    const ed = Edit.create(cur, { rebase: (b) => { cur = File.parse(b); return cur; }, erase: Blocks.eraseAt });
    for (const st of [2, 3, 4, 5, 6]) {
        const pr = 20, pc = 25, wantTile = Blocks.FOOD[st].stages[0][1], fit = items[wantTile];
        ed.act(Blocks.placePile(cur, pr, pc, st));
        check(cur.tile2(pr, pc) === EMPTY2, 'a ' + st + '-stage pile put by the editor leaves layer 2 empty');
        const pout = frame(rig, cur, view, w, h).calls.filter((c) => isObject(c) && c.a[0] === fit[0] && c.a[1] === fit[1] && c.a[4] === Math.round(Math.round(x0) + pc * T + fit[4] * k) && c.a[5] === Math.round(Math.round(y0) + pr * T + fit[5] * k));
        check(pout.length === 1, 'a ' + st + '-stage food pile in block 2 draws its stage-0 picture (' + wantTile + ')');
        rig.env.log.length = 0;
        View.miniBase(cur, 168, 168, rig.env.makeCanvas);
        check(rig.env.log.filter((c) => c.op === 'fillRect' && c.fill === Tiles.MINI_FOOD).length === cur.food.length, 'the small map marks every food pile of block 2');
        ed.undo();
        const gone = frame(rig, cur, view, w, h).calls.filter((c) => isObject(c) && c.a[4] === Math.round(Math.round(x0) + pc * T + fit[4] * k) && c.a[5] === Math.round(Math.round(y0) + pr * T + fit[5] * k));
        check(gone.length === 0, 'and when the pile is undone its picture is gone');
    }
}

// a made-up map of rows x columns cells, all gravel with nothing on layer 2 (only what the pictures' layer reads of a file), with the dictionary of `like`
function bigFile(like, rows, columns) {
    const cells = rows * columns, layer1 = like.layer1, layer2 = layer1 + 6 * cells, bytes = new Uint8Array(layer2 + 6 * cells + 16);
    bytes.set(like.bytes.subarray(0, layer1));
    for (let i = 0; i < cells; i++) { bytes[layer1 + 6 * i] = 485 & 255; bytes[layer1 + 6 * i + 1] = 485 >> 8; bytes[layer2 + 6 * i] = EMPTY2 & 255; bytes[layer2 + 6 * i + 1] = EMPTY2 >> 8; }
    return Object.assign({}, like, { bytes, rows, columns, cells, layer1, layer2, blocksAt: layer2 + 6 * cells, markers: [], food: [], plants: [] });
}

async function caseC() {
    const rig = await ready(), f = treasure(), w = 600, h = 400, tab = Tiles.tableOf(f);
    const frame = (r, file, view, W, H, extra) => { r.env.clock.t += 1000; return frameOf(r, file, view, W, H, extra); };          // (a second later: a frame that follows a zoom closely would be the last one stretched)
    const layerFills = (out) => out.calls.filter((c) => c.op === 'fillRect' && c.owner !== out.ctx.owner && c.fill !== Tiles.BACK);          // (a smooth cell: a rectangle of the ground's average color on the kept layer)
    const tiles = (out) => out.calls.filter((c) => isTile(c, w, h)).length, cellsOf = (file, view, W, H) => { const v = View.visible(view, file, W, H); return (v.r1 - v.r0 + 1) * (v.c1 - v.c0 + 1); };
    for (const scale of [48, 20, 12, 10, 9.99, 8, 6, 5, 4, 3.99, 3, 1.5, 1]) {
        const view = { scale, cx: f.columns / 2, cy: f.rows / 2 }, out = frame(rig, f, view, w, h), smooth = scale < 4, cells = cellsOf(f, view, w, h);
        check(out.done.sprites === 1 && out.done.rects + out.done.pixels === 0, scale + ' pixels a cell: the frame is the pictures at every zoom (' + JSON.stringify(out.done) + ')');
        const squares = out.calls.filter((c) => (c.op === 'fillRect' && c.fill === Tiles.FOOD) || c.op === 'strokeRect').length;
        check(squares === 0, scale + ' pixels a cell: no gold square for the food pile (its picture shows it)');
        if (smooth) {
            const fills = layerFills(out), want = {};
            for (let r = 0; r < f.rows; r++) for (let c = 0; c < f.columns; c++) want[Tiles.css(tile1(f, r, c), EMPTY2, tab)] = 1;
            const area = fills.reduce((t, c) => t + c.a[2] * c.a[3], 0);
            check(Math.abs(area - cells * scale * scale) < 0.005 * cells * scale * scale, scale + ' pixels a cell: the smooth cells tile the view with no gap and no overlap (' + area + ' pixels of ' + cells * scale * scale + ')');
            check(tiles(out) === 0 && fills.length === cells && fills.every((c) => want[c.fill]), scale + ' pixels a cell: the ground is the smooth average color of each cell (' + fills.length + ' of ' + cells + ' cells, ' + tiles(out) + ' tile pictures)');
        } else check(tiles(out) === cells && layerFills(out).length === 0, scale + ' pixels a cell: the ground is the tile pictures (' + tiles(out) + ' of ' + cells + ' cells)');
        check(out.calls.filter(isObject).length > 10 || scale <= 1.5, scale + ' pixels a cell: the objects are still pictures');
        if (scale === 20) {
            const T = 20, g = INDEX.ground, prepared = new Set(out.calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url && /ground\.png/.test(c.img.url)).map((c) => (c.a[5] / T) * g.columns + c.a[4] / T));
            const used = new Set(out.calls.filter((c) => isTile(c, w, h)).map((c) => (c.a[1] / T) * g.columns + c.a[0] / T));
            check(used.size > 3 && Array.from(used).every((k) => prepared.has(k)), 'every tile of the scaled sheet that is drawn from was scaled first (' + used.size + ' tiles)');
        }
    }
    // the smooth ground is counted in device pixels: a phone at 3 times shows 1.5 pixels a cell as 4.5, and 1.3 as 3.9
    for (const [scale, dpr, smooth] of [[1.5, 3, false], [1.3, 3, true], [1.9, 2, true], [2, 2, false]]) {
        const out = frame(rig, f, { scale, cx: 30, cy: 30 }, w, h, { dpr });
        check(smooth ? tiles(out) === 0 : tiles(out) > 0, scale + ' pixels a cell at ' + dpr + 'x: ' + (smooth ? 'smooth' : 'the tile pictures') + ' (' + tiles(out) + ' tile pictures)');
    }
    // more than 12,000 cells in the canvas AND in the map: smooth too, however big a tile is; the canvas of a small map never counts beyond the map
    const big = bigFile(f, 130, 100), W = 1400, H = 900;
    const above = frame(rig, big, { scale: 10, cx: 50, cy: 65 }, W, H), below = frame(rig, big, { scale: 11, cx: 50, cy: 65 }, W, H);
    check(tiles(above) === 0 && layerFills(above).length === 9000, 'a 13,000-cell map in a 1400 x 900 canvas at 10 pixels (12,600 cells in the canvas, 9,000 of them map): smooth ground (' + layerFills(above).length + ' rectangles, ' + tiles(above) + ' tile pictures)');
    check(tiles(below) === cellsOf(big, { scale: 11, cx: 50, cy: 65 }, W, H) && layerFills(below).length === 0, 'at 11 pixels (10,413 cells in the canvas): the tile pictures (' + tiles(below) + ')');
    const small = frame(rig, f, { scale: 10, cx: 30, cy: 30 }, W, H);
    check(tiles(small) === 3600, 'a 3,600-cell map in the same big canvas stays on the tile pictures (' + tiles(small) + ')');
    // a pan keeps the way of the layer, so that no strip of one kind is put beside the other
    const huge = bigFile(f, 200, 200), first = frame(rig, huge, { scale: 5, cx: 100, cy: 100 }, 700, 450), panned = frame(rig, huge, { scale: 5, cx: 100 + 10 / 5, cy: 100 }, 700, 450);
    check(layerFills(first).length > 12000 && tiles(panned) === 0 && layerFills(panned).length > 0 && layerFills(panned).length < 400, 'a pan paints only the strip that came into view, in the same smooth way (' + layerFills(first).length + ' rectangles, then ' + layerFills(panned).length + ')');
    // the smooth color follows the dictionary's names, as the pictures do
    const named = bigFile(f, 40, 40), t2 = Tiles.tableOf(named), before = Tiles.css(485, EMPTY2, t2), other = [431, 520, 571, 640, 669].find((k) => INDEX.ground.avg.substr((k - 431) * 6, 6) !== INDEX.ground.avg.substr((485 - 431) * 6, 6));
    setName(named, 485, INDEX.names[other]);
    const after = Tiles.css(485, EMPTY2, Tiles.tableOf(named)), smoothNamed = frame(rig, named, { scale: 3, cx: 20, cy: 20 }, w, h), renamed = layerFills(smoothNamed);
    check(before !== after && renamed.length > 0 && renamed.every((c) => c.fill === after), 'a tile named for another ground has that ground\'s smooth color (' + after + ' not ' + before + ')');
    setName(named, 485, '.');
    const blank = frame(rig, named, { scale: 3, cx: 20, cy: 20 }, w, h);
    check(blank.calls.filter((c) => c.op === 'fillRect' && c.owner !== blank.ctx.owner && c.fill === Tiles.BACK).length >= 1600 && layerFills(blank).length === 0, 'and "." is the black that the game leaves, as with the pictures');
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
    check(same.calls.filter(isGround).length === 0 && same.calls.filter(isObject).filter((c) => !isAnt(c)).length === 0, 'the same view again paints no cell and no object (but the ants, which go over the layer): the layer is kept');
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
    setName(f, 347, 'chicken');            // (Treasure's dictionary has "." there: the game would draw nothing)
    put2(f, 20, 25, 347, 1);               // chicken: a toy that is not in the sheet
    put2(f, 21, 25, 99999 & 0xFFFF, 1);    // (nothing: no picture, not in the index)
    const view = { scale: 20, cx: 30, cy: 24 };
    check(!!INDEX.far['347'] && !INDEX.objects.items['347'], 'chicken is fetched alone, not in the sheet');
    frame(rig, f, view, w, h);
    const mineUrl = '/asset_catalog/sprites/sprite_' + ('000' + INDEX.far['347'][0]).slice(-4) + '.png', asked = rig.env.images.filter((i) => i.url === mineUrl);
    check(asked.length === 1, 'the first frame asks for its picture: ' + mineUrl);
    const second = frame(rig, f, view, w, h);
    check(rig.env.images.filter((i) => i.url === mineUrl).length === 1 && second.calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url === mineUrl).length === 0, 'it is asked for once, and nothing is drawn in its place yet');
    rig.env.settle();
    check(rig.env.timers.length >= 0, 'the picture arrives');
    const third = frame(rig, f, view, w, h), mine = third.calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url === mineUrl);
    check(mine.length === 1, 'then it is drawn');
    if (mine.length) {
        const k = 20 / 32, far = INDEX.far['347'], x0 = Math.round(w / 2 - 30 * 20), y0 = Math.round(h / 2 - 24 * 20);
        check(mine[0].a[4] === Math.round(x0 + 25 * 20 + far[1] * k) && mine[0].a[5] === Math.round(y0 + 20 * 20 + far[2] * k) && mine[0].a[6] === Math.round(mine[0].img.width * k), 'at its cell\'s corner plus its offset, scaled like the cell');
    }
    // a 404: nothing is drawn, nothing breaks, and it is not asked for again
    const lost = await ready({ farMissing: true }), g = treasure();
    setName(g, 347, 'chicken');
    put2(g, 20, 25, 347, 1);
    frame(lost, g, view, w, h);
    lost.env.settle();
    lost.sprites.reset();
    const after = frame(lost, g, view, w, h);
    check(after.calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url === mineUrl).length === 0 && lost.env.images.filter((i) => i.url === mineUrl).length === 0, 'a missing picture draws nothing and is not asked for again');
}

// where the game's pictures say that the start ant `m` of `file` is drawn in `view` (a w x h canvas): the picture in the sheet, its box, and whether it is turned round (then the origin moves by `tx`)
const ANT_TYPE_OF = { 1: 64, 2: 66, 3: 63, 4: 62, 5: 65 };
function expectedAnt(file, m, view, w, h, index) {
    index = index || INDEX;
    const spec = m.antType && index.antTypes[ANT_TYPE_OF[m.antType]], def = file.defaultType && index.antTypes[file.defaultType];
    const sid = String(spec ? spec.id : (def ? def.id : index.ant)), group = spec || def || index.ants;
    const d = Sprites.face(m.row, m.column), b = d > 4 ? 8 - d : d, fid = b === 4 || !index.faces ? sid : String(index.faces[sid][b - 1]);
    const it = index.objects.items[fid], at = fid === sid ? group[m.team] : index.antFaces[fid][m.team], s = view.scale, k = s / 32;
    const cx = w / 2 - view.cx * s + (m.column + 0.5) * s, ay = h / 2 - view.cy * s + (m.row + 0.5) * s, flip = d > 4 && fid !== sid;
    return flip ? { a: [at[0], at[1], it[2], it[3], 0, ay + it[5] * k, it[2] * k, it[3] * k], tx: cx + (1 - it[4]) * k, flip: -1 } : { a: [at[0], at[1], it[2], it[3], cx + it[4] * k, ay + it[5] * k, it[2] * k, it[3] * k], tx: 0, flip: 1 };
}
const sameDraw = (c, e) => c.flip === e.flip && Math.abs(c.tx - e.tx) < 1e-6 && c.a.length === 8 && c.a.every((v, i) => Math.abs(v - e.a[i]) < 1e-6);

async function caseG() {
    const rig = await ready(), f = treasure(), w = 600, h = 400;
    check(f.markers.length === 24, 'Treasure has 24 starting ants (' + f.markers.length + ')');
    const ant = INDEX.objects.items[String(INDEX.ant)], antXs = new Set(Object.values(INDEX.ants).map((a) => a[0]));
    check(!!ant && ant[2] === 23 && ant[3] === 40 && ant[4] === -11 && ant[5] === -28 && antXs.size === 4, 'the ant is the standing worker (agst301): 23 x 40, offset -11, -28, in four team colors');
    const view = { scale: 24, cx: f.columns / 2, cy: f.rows / 2 };
    View.clamp(view, f, w, h);
    const vis = View.visible(view, f, w, h), inView = f.markers.filter((m) => m.row >= vis.r0 && m.row <= vis.r1 && m.column >= vis.c0 && m.column <= vis.c1);
    const big = frame(rig, f, view, w, h), ants = big.calls.filter(isAnt);
    check(inView.length > 6 && ants.length === inView.length, 'at 24 pixels a cell each ant in view is drawn in its team color: ' + ants.length + ' of ' + inView.length);
    check(inView.every((m) => ants.filter((c) => sameDraw(c, expectedAnt(f, m, view, w, h))).length === 1), 'each one is the picture of its type, facing and team color, at its cell\'s middle plus the picture\'s offset (turned round for the west three)');
    const rings = big.calls.filter((c) => c.op === 'ellipse');
    check(rings.length === inView.length, 'with a ring at its feet');
    const strokes = big.calls.filter((c) => c.op === 'stroke').length, blacks = inView.filter((m) => m.team === 'black').length;
    check(blacks > 0 && strokes === inView.length + blacks, 'in the color of its team, and the black one with a thin white edge (' + strokes + ' lines for ' + inView.length + ' rings and ' + blacks + ' black ones)');
    const small = frame(rig, f, { scale: 11, cx: f.columns / 2, cy: f.rows / 2 }, w, h);
    check(small.calls.filter(isAnt).length === 0 && small.calls.filter((c) => c.op === 'arc').length >= 20, 'below 12 pixels a cell the ants are dots in the team\'s color');
    for (const [scale, radius] of [[1, 1], [3, 1.2], [4.9, 1.96], [5, 1.5], [8, 2.4]]) {
        const dots = frame(rig, f, { scale, cx: f.columns / 2, cy: f.rows / 2 }, w, h).calls.filter((c) => c.op === 'arc');
        check(dots.length >= 20 && dots.every((c) => Math.abs(c.a[2] - radius) < 1e-9), 'at ' + scale + ' pixels a cell a start dot has the radius ' + radius + ' (smaller where a cell is under 5 pixels, so that it is not a blob over the cells): ' + (dots[0] ? dots[0].a[2] : 'none'));
    }
    // when the map has a specialist Default Ant Type (e.g. 64 Bomber ant), starting ants draw that specialist sprite in each team's color
    const bomberMap = Object.assign({}, f, { defaultType: 64 }), bomberEntry = INDEX.antTypes['64'], bomberSample = INDEX.objects.items[String(bomberEntry.id)];
    const bomberDraws = frame(rig, bomberMap, view, w, h).calls.filter(isAnt);
    check(bomberSample[2] > 0 && bomberSample[3] > 0 && bomberDraws.length === inView.length && inView.every((m) => bomberDraws.filter((c) => sameDraw(c, expectedAnt(bomberMap, m, view, w, h))).length === 1) && Sprites.face(30, 30) >= 1, 'with defaultType=64 (Bomber ant), every starting ant in view is drawn with the team-colored Bomber sprite, facing its way: ' + bomberDraws.length);
    const ghosted = frame(rig, f, view, w, h, { ghost: { id: 346, row: 30, column: 30 } }).calls.filter((c) => c.op === 'drawImage' && c.alpha < 1);
    check(ghosted.length === 1 && ghosted[0].a[2] === INDEX.objects.items['346'][2], 'the ghost of a bigger object is its own picture, see-through (' + (ghosted[0] || { alpha: 'none' }).alpha + ')');
    const plain = frame(rig, f, view, w, h, { ghost: null }).calls.filter((c) => c.op === 'drawImage' && c.alpha < 1);
    check(plain.length === 0, 'and nothing is see-through without one');
    check(frame(rig, f, view, w, h, { ghost: { id: 99, row: 1, column: 1 } }).calls.filter((c) => c.op === 'drawImage' && c.alpha < 1).length === 0, 'an id with no picture has no ghost');
    // the Pick up tool draws the see-through picture of a hill, a pile or a plant by the same function
    const gx = rig.env.makeCanvas().getContext('2d');
    rig.env.log.length = 0;
    check(typeof rig.sprites.ghost === 'function', 'the sprite layer offers its ghost to the Pick up tool');
    rig.sprites.ghost(gx, view, w, h, 247, 30, 30);
    const given = rig.env.log.filter((c) => c.op === 'drawImage' && c.alpha < 1);
    check(given.length === 1 && given[0].a[2] === INDEX.objects.items['247'][2], 'and it is a see-through picture of that tile (a red hill)');
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
    check(rig.sprites.chip({ style: {}, appendChild() {} }, true, 410, 30) === true && rig.sprites.chip({ style: {}, appendChild() {} }, true, 99, 30) === false && rig.sprites.chip({ style: {}, appendChild() {} }, false, 300, 30) === false && rig.sprites.chip({ style: {}, appendChild() {} }, true, 347, 30) === false, 'a plant has a chip in the sheet; no chip for a tile that has no picture in the sheet');
    const cold = Sprites.create({ doc: rig.env.doc, win: rig.env.win });
    check(cold.chip({ style: {}, appendChild() {} }, false, 485, 30) === false, 'and none before the atlas is in');
}

async function caseJ() {
    // the facing: 1 to 7, never north (0) or 8, the same every time, evenly spread, and not the same as its neighbors' (a made-up example, not a pattern)
    const counts = new Array(9).fill(0);
    let sameAcross = 0, sameDown = 0, total = 0;
    for (let r = 0; r < 120; r++) for (let c = 0; c < 120; c++) {
        const d = Sprites.face(r, c);
        check(Number.isInteger(d) && d >= 1 && d <= 7 && d === Sprites.face(r, c), 'the facing of (' + r + ', ' + c + ') is a whole number from 1 to 7 and the same every time: ' + d);
        counts[d]++; total++;
        if (c < 119 && d === Sprites.face(r, c + 1)) sameAcross++;
        if (r < 119 && d === Sprites.face(r + 1, c)) sameDown++;
    }
    check(counts[0] === 0 && counts[8] === 0 && counts.slice(1, 8).every((n) => n > total / 7 * 0.9 && n < total / 7 * 1.1), 'the seven facings are evenly spread over 14,400 cells: ' + counts.slice(1, 8).join(' '));
    check(sameAcross / total < 0.17 && sameDown / total < 0.17 && sameAcross / total > 0.11 && sameDown / total > 0.11, 'a neighbor has the same facing about one time in seven (' + (sameAcross / total).toFixed(3) + ' across, ' + (sameDown / total).toFixed(3) + ' down)');
    check(Sprites.face(0, 0) !== undefined && [0, 1, 2, 3].map((c) => Sprites.face(5, c)).join('') !== '4444', 'it is not one facing for every ant');
    // every type, team and facing is drawn from its picture, the west three turned round, and no frame changes the map or the markers
    const rig = await ready(), f = treasure(), w = 600, h = 400, view = { scale: 24, cx: 30, cy: 30 };
    View.clamp(view, f, w, h);
    const vis = View.visible(view, f, w, h), seen = new Set(), types = [undefined, 1, 2, 3, 4, 5];
    for (const type of types) {
        const markers = f.markers.map((m, i) => Object.assign({}, m, type ? { antType: type } : {}, { column: m.column }));
        const file = Object.assign({}, f, { markers });
        const out = frame(rig, file, view, w, h), ants = out.calls.filter(isAnt), inView = markers.filter((m) => m.row >= vis.r0 && m.row <= vis.r1 && m.column >= vis.c0 && m.column <= vis.c1);
        check(ants.length === inView.length && inView.length > 6 && inView.every((m) => ants.filter((c) => sameDraw(c, expectedAnt(file, m, view, w, h))).length === 1), 'type ' + (type || 'default') + ': each ant in view is its own facing\'s picture in its team color (' + ants.length + ' of ' + inView.length + ')');
        for (const m of inView) seen.add(Sprites.face(m.row, m.column));
        if (type === 3) check(ants.some((c) => c.flip === -1) && ants.some((c) => c.flip === 1), 'some are turned round (the west three) and some are not');
    }
    check(seen.size >= 5, 'the ants of one map face at least five ways (' + Array.from(seen).sort().join(',') + ')');
    const before = Buffer.from(f.bytes).toString('hex'), mk = JSON.stringify(f.markers);
    frame(rig, f, view, w, h); frame(rig, f, { scale: 40, cx: 20, cy: 20 }, w, h);
    check(Buffer.from(f.bytes).toString('hex') === before && JSON.stringify(f.markers) === mk, 'drawing the facings changes nothing in the map: the file bytes and the markers are as they were, nothing is stored');
    // a flipped draw is put one pixel right of the mirrored origin and restored (the next picture is not turned round)
    const out = frame(rig, f, view, w, h), calls = out.calls;
    let depth = 0, bad = 0;
    for (const c of calls) { if (c.op === 'save') depth++; if (c.op === 'restore') depth--; if (depth < 0) bad++; }
    check(depth === 0 && bad === 0 && calls.filter((c) => c.op === 'drawImage' && c.flip === -1 && c.tx === 0).length === 0 && calls.filter((c) => c.op === 'drawImage' && c.flip === 1 && c.tx !== 0).length === 0, 'every turned-round draw is inside its own save and restore, with its origin moved');
    // an atlas that has no facings (an old one) draws the south picture of each type, as before
    const old = await ready({ oldIndex: true }), oldIndex = Object.assign({}, INDEX); delete oldIndex.faces; delete oldIndex.antFaces;
    check(old.sprites.ready(), 'an atlas without the facings loads');
    const oldOut = frame(old, f, view, w, h), oldAnts = oldOut.calls.filter(isAnt), inView = f.markers.filter((m) => m.row >= vis.r0 && m.row <= vis.r1 && m.column >= vis.c0 && m.column <= vis.c1);
    // an atlas that has facings but none for one type (the Bomber): that type is drawn facing south, the others as usual
    const part = await ready({ noBomber: true }), bomberFile = Object.assign({}, f, { defaultType: 64 }), partAnts = frame(part, bomberFile, view, w, h).calls.filter(isAnt);
    check(partAnts.length === inView.length && partAnts.every((c) => c.flip === 1 && c.a[2] === INDEX.objects.items[String(INDEX.antTypes['64'].id)][2]), 'an atlas with no facings for one type draws that type facing south, and does not crash: ' + partAnts.length);
    check(oldAnts.length === inView.length && oldAnts.every((c) => c.flip === 1 && c.a[2] === 23 && c.a[3] === 40), 'it draws the south worker for every ant: ' + oldAnts.length);
}

async function caseI() {
    const rig = await ready(), f = treasure(), w = 600, h = 400, T = 20, k = T / 32, items = INDEX.objects.items, view = { scale: T, cx: 30, cy: 30 };
    const x0 = Math.round(w / 2 - 30 * T), y0 = Math.round(h / 2 - 30 * T), at = (c, it, r2, c2) => c.a[4] === Math.round(x0 + c2 * T + it[4] * k) && c.a[5] === Math.round(y0 + r2 * T + it[5] * k);
    const objectsAt = (calls, it) => calls.filter((c) => isObject(c) && c.a[0] === it[0] && c.a[1] === it[1] && c.a[2] === it[2]);
    check(INDEX.names[339] === 'jacks4' && INDEX.names[302] === 'smlrock1' && !INDEX.objects.items['339'] && !!INDEX.far['339'], 'jacks4 is animation 339, fetched alone; smlrock1 (302) is in the sheet');
    // an entry that names ANOTHER animation: the picture of that one, at the cell
    setName(f, 340, 'smlrock1');
    put2(f, 20, 25, 340, 1);
    const named = frame(rig, f, view, w, h);
    check(objectsAt(named.calls, items['302']).some((c) => at(c, items['302'], 20, 25)), 'a tile whose entry is "smlrock1" is the small rock\'s picture (302), not the picture of its own number');
    check(!objectsAt(named.calls, items['340']).some((c) => at(c, items['340'], 20, 25)), 'and not the picture of its own number (340, the pink eraser)');
    // the name that gives a picture that has to be fetched: jacks4 (339) for the number 340
    setName(f, 340, 'jacks4');
    const asked = frame(rig, f, view, w, h);
    const jacksUrl = '/asset_catalog/sprites/sprite_' + ('000' + INDEX.far['339'][0]).slice(-4) + '.png';
    check(rig.env.images.some((i) => i.url === jacksUrl) && !objectsAt(asked.calls, items['340']).some((c) => at(c, items['340'], 20, 25)), 'the entry "jacks4" asks for the sprite of jacks4 (339), and draws not the pink eraser (340) of the sheet');
    rig.env.settle();
    const jacks = frame(rig, f, view, w, h).calls.filter((c) => c.op === 'drawImage' && c.img && c.img.url === jacksUrl);
    check(jacks.length === 1 && jacks[0].a[4] === Math.round(x0 + 25 * T + INDEX.far['339'][1] * k) && jacks[0].a[5] === Math.round(y0 + 20 * T + INDEX.far['339'][2] * k), 'and draws it at the cell\'s corner plus its own offset');
    // "." draws nothing: the same tile with an empty entry; a tile outside the dictionary draws nothing
    setName(f, 340, '.');
    const none = frame(rig, f, view, w, h);
    check(!none.calls.some((c) => c.op === 'drawImage' && c.img && c.img.url === jacksUrl) && !none.calls.some((c) => isObject(c) && at(c, items['302'], 20, 25)), 'an entry of "." draws nothing');
    put2(f, 20, 25, 2000, 1);
    check(frame(rig, f, view, w, h).calls.filter((c) => isObject(c) && at(c, items['302'], 20, 25)).length === 0, 'a number beyond the dictionary draws nothing');
    put2(f, 20, 25, EMPTY2, 0);
    // the ids that the game draws from the number: a power-up and a bomb whose entries are "."
    setName(f, 62, '.'); setName(f, 129, '.');
    put2(f, 20, 25, 62, 1); put2(f, 21, 25, 129, 1);
    const byId = frame(rig, f, view, w, h);
    check(objectsAt(byId.calls, items['62']).some((c) => at(c, items['62'], 20, 25)) && objectsAt(byId.calls, items['129']).some((c) => at(c, items['129'], 21, 25)), 'a power-up and an original bomb are drawn from their numbers, whatever their entries say');
    put2(f, 20, 25, EMPTY2, 0); put2(f, 21, 25, EMPTY2, 0);
    // the ground: an entry that is "." leaves the cell black, an entry that names another ground tile shows that tile
    const vis = View.visible(view, f, w, h), cellsInView = (vis.r1 - vis.r0 + 1) * (vis.c1 - vis.c0 + 1), counts = {};
    for (let r = vis.r0; r <= vis.r1; r++) for (let c = vis.c0; c <= vis.c1; c++) counts[tile1(f, r, c)] = (counts[tile1(f, r, c)] || 0) + 1;
    const commonest = Object.keys(counts).map(Number).sort((a, b) => counts[b] - counts[a])[0], group = (calls) => calls.filter((c) => isTile(c, w, h)).length;
    check(commonest >= 431 && counts[commonest] > 20, 'a tile in the view that many cells have: ' + commonest + ' (' + counts[commonest] + ' cells)');
    const before = frame(rig, f, view, w, h);
    check(group(before.calls) === 0, 'the same view again paints no cell (the layer is kept)');
    setName(f, commonest, '.');
    const blank = frame(rig, f, view, w, h);
    check(group(blank.calls) === cellsInView - counts[commonest], 'a name written in place paints the whole layer again, and the ' + counts[commonest] + ' cells of the tile whose entry is "." are not drawn from the sheet (' + group(blank.calls) + ' of ' + cellsInView + ')');
    check(blank.calls.filter((c) => c.op === 'fillRect' && c.fill === Tiles.BACK && c.owner !== blank.ctx.owner).length >= counts[commonest], 'they are the black that the game leaves');
    const other = commonest === 485 ? 486 : 485;
    setName(f, commonest, INDEX.names[other]);
    const shown = frame(rig, f, view, w, h), g = INDEX.ground, wanted = [((other - g.first) % g.columns) * T, Math.floor((other - g.first) / g.columns) * T];
    const same = shown.calls.filter((c) => isTile(c, w, h) && c.a[0] === wanted[0] && c.a[1] === wanted[1]).length;
    check(same >= counts[commonest], 'an entry that names another ground tile (' + INDEX.names[other] + ') shows that tile\'s picture in its ' + counts[commonest] + ' cells (' + same + ' with that picture)');
    setName(f, commonest, 'smlrock1');
    const rocks = frame(rig, f, view, w, h);
    check(objectsAt(rocks.calls, items['302']).length >= counts[commonest] && rocks.calls.some((c) => c.op === 'fillRect' && c.fill === Tiles.BACK && c.owner !== rocks.ctx.owner), 'an entry that names an object (smlrock1) on a ground cell draws that picture over the black, in each of the ' + counts[commonest] + ' cells');
    setName(f, commonest, INDEX.names[commonest]);
    check(group(frame(rig, f, view, w, h).calls) === cellsInView, 'the name put back paints the whole layer again');
    // the flat colors follow the names
    setName(f, 340, 'smlrock1');
    put2(f, 20, 25, 340, 1);
    const flat = frame({ env: rig.env, sprites: null }, f, { scale: 6, cx: 30, cy: 30 }, w, h), s6 = 6, fx = Math.round(w / 2 - 30 * s6 + 25 * s6), fy = Math.round(h / 2 - 30 * s6 + 20 * s6);
    check(flat.calls.some((c) => c.op === 'fillRect' && c.a[0] === fx && c.a[1] === fy && c.fill === Tiles.css(0, 302, null)), 'with no pictures at 6 pixels a cell the rectangle of a tile named smlrock1 is that rock\'s color: ' + Tiles.css(0, 302, null));
    const rockColor = Tiles.css(0, 302, null), rockPacked = Tiles.hexToPacked(rockColor), scratch = rig.env.makeCanvas(), small = { scale: 3, cx: 30, cy: 30 };
    View.draw(rig.env.makeCanvas().getContext('2d'), small, f, w, h, { scratch, sprites: null });
    const kept = scratch.keptImage, px = new Uint32Array(kept.data.buffer), pxAt = (r, c) => px[Math.round(h / 2 - 30 * 3 + r * 3 + 1) * kept.width + Math.round(w / 2 - 30 * 3 + c * 3 + 1)];
    check(pxAt(20, 25) === rockPacked, 'under 5 pixels a cell the pixel of a tile named smlrock1 is that rock\'s color');
    put2(f, 22, 25, 340, 1);
    check(View.redrawPixelCell(small, f, w, h, 22, 25, scratch, { x0: 1e9, y0: 1e9, x1: -1, y1: -1 }) === true && pxAt(22, 25) === rockPacked, 'and the cell that a stroke changed is painted again with it');
    const one = rig.env.makeCanvas().getContext('2d');
    rig.env.log.length = 0;
    View.redrawCell(one, { scale: 6, cx: 30, cy: 30 }, f, w, h, 22, 25);
    check(rig.env.log.some((c) => c.op === 'fillRect' && c.fill === rockColor), 'at 6 pixels a cell that a stroke changed is painted again with its color by name');
    put2(f, 22, 25, EMPTY2, 0);
    // the ghost: a plant is its number; a tile that is put is named by the tool when its entry is "."; a thing that is picked up keeps its entry; a pile is its stage
    const ghosts = (id, row, column, file, kind) => { const gx = rig.env.makeCanvas().getContext('2d'); rig.env.log.length = 0; rig.sprites.ghost(gx, view, w, h, id, row, column, file, kind); return rig.env.log.filter((c) => c.op === 'drawImage' && c.alpha < 1); };
    setName(f, 340, 'smlrock1'); setName(f, 346, '.'); setName(f, 410, 'smlrock1');
    check(ghosts(340, 30, 30, f).length === 1 && ghosts(340, 30, 30, f)[0].a[2] === items['302'][2] && ghosts(340, 30, 30, f)[0].a[0] === items['302'][0], 'the ghost of a tile that is about to be put shows the picture of its entry (smlrock1: the small rock)');
    check(ghosts(346, 30, 30, f).length === 1 && ghosts(346, 30, 30, f)[0].a[0] === items['346'][0], 'an entry of "." is named by the tool when the tile goes down: its picture is the number\'s');
    check(ghosts(346, 30, 30, f, 'obj').length === 0, 'a thing that is picked up and put down again keeps its entry: "." is nothing');
    check(ghosts(410, 30, 30, f, 'plant').length === 1 && ghosts(410, 30, 30, f, 'plant')[0].a[0] === items['410'][0] && ghosts(410, 30, 30, f).length === 1 && ghosts(410, 30, 30, f)[0].a[0] === items['410'][0], 'a plant is its number (the game draws plants by number), put or picked up');
    check(ghosts(340, 30, 30, f, 'pile').length === 1 && ghosts(340, 30, 30, f, 'pile')[0].a[0] === items['302'][0], 'a pile\'s stage tile is the name\'s animation too');
    setName(f, 350, 'zzz');
    check(ghosts(350, 30, 30, f, 'pile').length === 1 && ghosts(350, 30, 30, f, 'pile')[0].a[0] === items['350'][0] && ghosts(350, 30, 30, f).length === 0, 'a stage whose entry gives no animation is its number, but a tile that is put with such an entry stays nothing');
    check(ghosts(340, 30, 30, null).length === 1 && ghosts(340, 30, 30, null)[0].a[0] === items['340'][0], 'with no map the number is the picture');
    // the ground tile of the before and after of Fix edges
    const cx = rig.env.makeCanvas().getContext('2d');
    rig.env.log.length = 0;
    setName(f, 500, '.');
    check(rig.sprites.ground(cx, 500, 0, 0, 10, Tiles.tableOf(f), false) === true && rig.env.log.some((c) => c.op === 'fillRect' && c.fill === Tiles.BACK) && !rig.env.log.some((c) => c.op === 'drawImage'), 'a cell of the map whose entry is "." is black in the before picture');
    rig.env.log.length = 0;
    check(rig.sprites.ground(cx, 500, 0, 0, 10, Tiles.tableOf(f), true) === true && rig.env.log.some((c) => c.op === 'drawImage'), 'and a tile that is put there takes the number\'s picture in the after picture (the tool names it)');
    rig.env.log.length = 0;
    check(rig.sprites.ground(cx, 500, 0, 0, 10) === true && rig.env.log.some((c) => c.op === 'drawImage'), 'with no map the number is the picture');
    setName(f, 500, INDEX.names[501]);
    rig.env.log.length = 0;
    rig.sprites.ground(cx, 500, 0, 0, 10, Tiles.tableOf(f), false);
    const g501 = rig.env.log.filter((c) => c.op === 'drawImage').pop();
    check(!!g501 && g501.a[0] === ((501 - 431) % 16) * 32 && g501.a[1] === Math.floor((501 - 431) / 16) * 32, 'a cell whose entry names another ground tile shows that tile');
}

(async function () {
    for (const [name, fn] of [['A loading', caseA], ['B a whole frame', caseB], ['C pictures and flat colors', caseC], ['D the layer kept between frames', caseD], ['E the zoom', caseE], ['F a toy that is fetched alone', caseF], ['G the ants and the ghost', caseG], ['J the facing of a start ant', caseJ], ['H chips', caseH], ['I the open map\'s dictionary', caseI]]) {
        const before = failures;
        console.log('RUNNING: ' + name + ' ...');
        try { await fn(); } catch (e) { failures++; console.log('    CRASHED: ' + (e && e.stack || e)); }
        console.log('RUNNING: ' + name + ' ... ' + (failures === before ? 'PASS' : 'FAIL'));
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
})();
