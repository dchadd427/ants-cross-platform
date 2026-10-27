// The small map's box (docs/MAP_EDITOR.md, "The small map"): the hills and starting ants over it at the size it is shown, Hide and Show, Lock, and a place of its own (dragged, or moved by the
// arrow keys), all kept in this browser. A browser script.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapMini = factory();
})(this, function () {
    'use strict';

    var KEY = 'ants_mapmaker_minimap', STEP = 16, TAP = 6, HILL = 10, DOT = 5;
    var FILL = { green: '#5dff2e', red: '#ff2d86', blue: '#3d9bff', black: '#161616' }, TEAMS = ['black', 'blue', 'red', 'green'];

    // Each team's hill: the middle of its cells of layer 2 (tiles 245 to 248), in cells
    function hills(file) {
        var b = file.bytes, i, id, s, sum = {}, out = [];
        for (i = 0; i < file.rows * file.columns; i++) {
            id = b[file.layer2 + 6 * i] | (b[file.layer2 + 6 * i + 1] << 8);
            if (id > 244 && id < 249) { s = sum[id] = sum[id] || { team: TEAMS[id - 245], x: 0, y: 0, n: 0 }; s.x += i % file.columns + 0.5; s.y += (i / file.columns | 0) + 0.5; s.n++; }
        }
        for (id in sum) out.push({ team: sum[id].team, x: sum[id].x / sum[id].n, y: sum[id].y / sum[id].n });
        return out;
    }

    // A square of s pixels centered on (x, y): an edge of one CSS pixel (px device pixels; white on the black team, black on the others), then the team's color
    function square(ctx, x, y, s, team, px) {
        var l = Math.round(x - s / 2), t = Math.round(y - s / 2);
        ctx.fillStyle = team === 'black' ? '#fff' : '#000'; ctx.fillRect(l, t, s, s);
        ctx.fillStyle = FILL[team]; ctx.fillRect(l + px, t + px, s - 2 * px, s - 2 * px);
    }

    // The hills (10 CSS pixels at least) and the starting ants (small; not beside a hill of their team) on a canvas w pixels wide
    function marks(ctx, file, list, w, px) {
        var k = w / file.columns, s = Math.max(HILL * px, Math.round(4 * k));
        list.forEach(function (h) { square(ctx, h.x * k, h.y * k, s, h.team, px); });
        file.markers.forEach(function (m) {
            var x = (m.column + 0.5) * k, y = (m.row + 0.5) * k;
            if (!list.some(function (h) { return h.team === m.team && Math.abs(h.x * k - x) < s && Math.abs(h.y * k - y) < s; })) square(ctx, x, y, DOT * px, m.team, px);
        });
    }

    // A place (the left and top of the box) kept inside the room for a box: {w, h}
    function fit(p, room, box) { return { x: Math.max(0, Math.min(p.x, room.w - box.w)), y: Math.max(0, Math.min(p.y, room.h - box.h)) }; }
    function other(a, b) { return a.x !== b.x || a.y !== b.y; }

    // env: {doc, win, minimap (the box), mini (its canvas), grip, lock, hide, show (any may be missing)}; again() asks for a new frame
    function create(env, again) {
        var box = env.minimap, st = { pos: null, off: false, lock: false }, loaded = false, down = null, tapAt = 0, cache = null;
        function store() { try { env.win.localStorage.setItem(KEY, JSON.stringify([st.pos && st.pos.x, st.pos && st.pos.y, +st.off, +st.lock])); } catch (e) { /* this visit only */ } }
        try {
            var v = JSON.parse(env.win.localStorage.getItem(KEY));
            if (Array.isArray(v) && v.length === 4) { st.off = v[2] === 1; st.lock = v[3] === 1; if (isFinite(v[0]) && isFinite(v[1]) && v[0] !== null && v[1] !== null) st.pos = { x: +v[0], y: +v[1] }; }
        } catch (e) { /* none kept */ }
        function sizeOf(n) { return { w: n.offsetWidth || 0, h: n.offsetHeight || 0 }; }
        function room() { var p = box.parentNode; return p && p.clientWidth ? { w: p.clientWidth, h: p.clientHeight } : null; }
        function put() {
            [box, env.show].forEach(function (n) {
                if (n && n.style) { n.style.left = st.pos ? st.pos.x + 'px' : ''; n.style.top = st.pos ? st.pos.y + 'px' : ''; n.style.right = st.pos ? 'auto' : ''; }
            });
        }
        function sync() {
            if (env.lock) { env.lock.setAttribute('aria-pressed', '' + st.lock); env.lock.setAttribute('aria-label', st.lock ? 'Unlock minimap position' : 'Lock minimap position'); env.lock.textContent = st.lock ? '🔒' : '🔓'; }
            if (env.grip) { env.grip.setAttribute('aria-disabled', '' + st.lock); env.grip.classList.toggle('locked', st.lock); }
        }
        function move(p, keep) { st.pos = p; put(); if (!keep) store(); }
        function here() { return { x: box.offsetLeft, y: box.offsetTop }; }
        function reset() { if (!st.lock) move(null); }
        // Inside the room whenever the room or the box changes (a lock keeps the choice, not a box outside a smaller room; the first look drops a place that does not fit)
        function place() {
            var r = room(), b = sizeOf(box.hidden && env.show ? env.show : box), p = st.pos && r && fit(st.pos, r, b);
            if (!r || !b.w) return;
            if (!loaded) { loaded = true; if (p && other(p, st.pos)) st.pos = null; put(); } else if (p && other(p, st.pos)) move(p, true);
        }
        function toggle(what) {
            st[what] = !st[what]; sync(); store();
            if (what === 'off') { box.hidden = st.off; env.show.hidden = !st.off; (st.off ? env.show : env.hide).focus(); place(); if (again) again(); }
        }
        sync();
        [['hide', 'off'], ['show', 'off'], ['lock', 'lock']].forEach(function (b) { if (env[b[0]]) env[b[0]].addEventListener('click', function () { toggle(b[1]); }); });
        if (env.grip) {
            env.grip.addEventListener('pointerdown', function (e) {
                if (st.lock || e.button > 0) return;
                down = { id: e.pointerId, x: e.clientX, y: e.clientY, from: here(), moved: false };
                try { env.grip.setPointerCapture(e.pointerId); } catch (x) { /* gone */ }
                e.preventDefault();
            });
            env.grip.addEventListener('pointermove', function (e) {
                if (!down || e.pointerId !== down.id) return;
                var dx = e.clientX - down.x, dy = e.clientY - down.y, r = room();
                if (Math.abs(dx) > TAP || Math.abs(dy) > TAP) down.moved = true;
                if (down.moved && r) move(fit({ x: down.from.x + dx, y: down.from.y + dy }, r, sizeOf(box)), true);
            });
            env.grip.addEventListener('pointerup', function (e) {
                if (!down || e.pointerId !== down.id) return;
                var moved = down.moved, now = Date.now(); down = null;
                if (moved) { store(); tapAt = 0; } else if (now - tapAt < 400) { reset(); tapAt = 0; } else tapAt = now;          // (two taps in a row: back to the corner)
            });
            env.grip.addEventListener('pointercancel', function () { down = null; });
            env.grip.addEventListener('keydown', function (e) {
                var d = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key], r = room(), p = here();
                if (e.key === 'Home') { e.preventDefault(); reset(); } else if (d) { e.preventDefault(); if (!st.lock && r) move(fit({ x: p.x + d[0] * STEP, y: p.y + d[1] * STEP }, r, sizeOf(box))); }
            });
        }
        return {
            state: function () { return { x: st.pos && st.pos.x, y: st.pos && st.pos.y, off: st.off, lock: st.lock }; },
            // Shows or hides the box and the Show map button, for a map that is shown in part or whole; true when the small map shows
            update: function (part) {
                box.hidden = !part || st.off;
                if (env.show) env.show.hidden = !(part && st.off);
                if (part) place();
                return !box.hidden;
            },
            // The small map `base` and the marks over it on the box's canvas, at the size it is shown: {ctx, w, h, px}
            paint: function (base, file) {
                var m = env.mini, ctx = m.getContext('2d'), px = Math.max(1, Math.min(3, Math.round(env.win.devicePixelRatio || 1))), w = Math.round((m.getBoundingClientRect().width || base.width) * px), h = Math.round(w * base.height / base.width), c;
                if (m.width !== w || m.height !== h) { m.width = w; m.height = h; }
                ctx.imageSmoothingEnabled = w < base.width; ctx.drawImage(base, 0, 0, w, h);
                if (!cache || cache.base !== base || cache.w !== w) {
                    c = env.doc.createElement('canvas'); c.width = w; c.height = h; marks(c.getContext('2d'), file, hills(file), w, px); cache = { base: base, w: w, layer: c };
                }
                ctx.drawImage(cache.layer, 0, 0);
                return { ctx: ctx, w: w, h: h, px: px };
            }
        };
    }

    return { create: create, hills: hills, marks: marks };
});
