// The two drag handles of Home (docs/HOME_SCREEN.md, "The splitters"): one between the Games list and the players list, one along the top of the chat dock. A wide window only (701 px and up); a phone has its
// strip of views and no handle. Pointer events (mouse, pen, touch), the keyboard (arrows 16 px, Shift 64, Home, End, Enter resets) and a double click. The sizes are kept in this browser only (localStorage,
// every read and write inside try), clamped to the window each time it changes. Text and properties only, never markup. ES5: a page gets AntsHomeSplit, node gets module.exports (tests/scripts/web_home_split_check.js).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeSplit = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var KEY = 'ants.home.split.v1';                     // {"p": the players' width, "c": the chat's height}, in px
    var WIDE = '(min-width: 701px)';
    var PAD = 24, GAP = 14;                             // px: the body's side padding and the gap between the panels (home.css)
    var SIDE_MIN = 240, CHAT_MIN = 120, CHAT_SHARE = 0.7;
    var STEP = 16, BIG = 64;
    var TEXT = { x: 'Resize the games and players lists', y: 'Resize the chat' };

    // ---- the pure parts ----
    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi < lo ? lo : hi, v)); }
    // the players' width: each of the two lists keeps SIDE_MIN at least (a window too narrow for both keeps the minimum for the players)
    function sideLimits(bodyWidth) { var room = bodyWidth - 2 * PAD - GAP; return { min: SIDE_MIN, max: Math.max(SIDE_MIN, Math.floor(room - SIDE_MIN)) }; }
    // the chat's height: at least CHAT_MIN, at most 70% of the body (the window under the bar)
    function chatLimits(bodyHeight) { return { min: CHAT_MIN, max: Math.max(CHAT_MIN, Math.floor(bodyHeight * CHAT_SHARE)) }; }
    // the size that a key makes (null: the key is not ours). kind 'x': Left makes the players wider, Right narrower; kind 'y': Up makes the chat taller, Down shorter; Home the least, End the most
    function keyed(kind, key, shift, now, lim) {
        var s = shift ? BIG : STEP, plus = kind === 'x' ? 'ArrowLeft' : 'ArrowUp', minus = kind === 'x' ? 'ArrowRight' : 'ArrowDown';
        if (key === plus) return clamp(now + s, lim.min, lim.max);
        if (key === minus) return clamp(now - s, lim.min, lim.max);
        if (key === 'Home') return lim.min;
        if (key === 'End') return lim.max;
        return null;
    }
    // a drag: the handle moved by d px along its axis (the players' width and the chat's height grow as the handle goes left or up)
    function dragged(start, d, lim) { return clamp(Math.round(start - d), lim.min, lim.max); }
    function parse(raw) {
        var out = { p: null, c: null }, o;
        try { o = JSON.parse(raw); } catch (e) { return out; }
        if (!o || typeof o !== 'object') return out;
        if (typeof o.p === 'number' && isFinite(o.p) && o.p > 0 && o.p < 10000) out.p = Math.round(o.p);
        if (typeof o.c === 'number' && isFinite(o.c) && o.c > 0 && o.c < 10000) out.c = Math.round(o.c);
        return out;
    }

    // ---- the handles ----
    // mount({ doc, win, host (Home's div), main (the body: the lists mount in it) }) returns { attach, sizes, reset }
    function mount(env) {
        var doc = env.doc, win = env.win, host = env.host, main = env.main;
        var want = { p: null, c: null }, handles = {}, chatBox = null;

        function store() { try { return win.localStorage; } catch (e) { return null; } }
        function load() { try { var s = store(); return parse(s ? s.getItem(KEY) : null); } catch (e) { return { p: null, c: null }; } }
        function save() {
            try {
                var s = store();
                if (!s) return;
                if (want.p === null && want.c === null) s.removeItem(KEY); else s.setItem(KEY, JSON.stringify(want));
            } catch (e) { /* the sizes are not kept: they still hold for this visit */ }
        }
        function wide() { try { return win.matchMedia ? !!win.matchMedia(WIDE).matches : (win.innerWidth || 0) >= 701; } catch (e) { return true; } }
        function bodyHeight() { var bar = host.querySelector ? host.querySelector('.hbar') : null; return (host.clientHeight || 0) - (bar ? bar.offsetHeight || 0 : 0); }
        function limits(kind) { return kind === 'x' ? sideLimits(main.clientWidth || 0) : chatLimits(bodyHeight()); }
        function current(kind) {
            var box = kind === 'x' ? (main.querySelector ? main.querySelector('.pl') : null) : chatBox, r = box && box.getBoundingClientRect ? box.getBoundingClientRect() : null;
            var v = r ? (kind === 'x' ? r.width : r.height) : 0;
            return Math.round(v > 0 ? v : (want[kind === 'x' ? 'p' : 'c'] || 0));
        }
        function paint(kind, value) {                    // the size in the page's own style, and in what a reader of the screen is told
            var h = handles[kind], lim = limits(kind);
            if (value === null) {
                host.style.removeProperty(kind === 'x' ? '--pl-w' : '--gc-h');
                host.removeAttribute(kind === 'x' ? 'data-split-w' : 'data-split-h');
            } else {
                host.style.setProperty(kind === 'x' ? '--pl-w' : '--gc-h', clamp(value, lim.min, lim.max) + 'px');
                host.setAttribute(kind === 'x' ? 'data-split-w' : 'data-split-h', '1');
            }
            if (h) { h.setAttribute('aria-valuemin', String(lim.min)); h.setAttribute('aria-valuemax', String(lim.max)); h.setAttribute('aria-valuenow', String(clamp(value === null ? current(kind) : value, lim.min, lim.max))); }
        }
        function set(kind, value, keep) {
            var k = kind === 'x' ? 'p' : 'c';
            if (value === null) want[k] = null;
            else { var lim = limits(kind); want[k] = clamp(Math.round(value), lim.min, lim.max); }
            paint(kind, want[k]);
            if (keep !== false) save();
        }

        function make(kind, parent) {
            var h = doc.createElement('div'), line = doc.createElement('i');
            h.className = 'sp sp-' + kind;
            h.setAttribute('role', 'separator'); h.setAttribute('tabindex', '0');
            h.setAttribute('aria-orientation', kind === 'x' ? 'vertical' : 'horizontal');
            h.setAttribute('aria-label', TEXT[kind]);
            h.setAttribute('aria-controls', kind === 'x' ? 'hmain' : 'gchat');
            h.setAttribute('title', TEXT[kind] + ' (drag, or use the arrow keys; Enter resets)');
            line.className = 'sp-line'; line.setAttribute('aria-hidden', 'true');
            h.appendChild(line);
            var drag = null;
            function end(e) {
                if (!drag) return;
                try { if (h.releasePointerCapture && e && e.pointerId !== undefined) h.releasePointerCapture(e.pointerId); } catch (x) { /* it is released already */ }
                drag = null;
                doc.documentElement.classList.remove('sp-drag-' + kind);
                h.classList.remove('sp-live');
                save();
            }
            h.addEventListener('pointerdown', function (e) {
                if (e.button !== undefined && e.button !== 0) return;
                if (e.isPrimary === false) return;
                drag = { from: kind === 'x' ? e.clientX : e.clientY, start: current(kind), id: e.pointerId };
                try { if (h.setPointerCapture && e.pointerId !== undefined) h.setPointerCapture(e.pointerId); } catch (x) { /* a pointer that has gone */ }
                doc.documentElement.classList.add('sp-drag-' + kind);
                h.classList.add('sp-live');
                if (e.preventDefault) e.preventDefault();
                h.focus();
            });
            h.addEventListener('pointermove', function (e) {
                if (!drag || (drag.id !== undefined && e.pointerId !== undefined && e.pointerId !== drag.id)) return;
                set(kind, dragged(drag.start, (kind === 'x' ? e.clientX : e.clientY) - drag.from, limits(kind)), false);
            });
            h.addEventListener('pointerup', end);
            h.addEventListener('pointercancel', end);
            h.addEventListener('lostpointercapture', end);
            h.addEventListener('keydown', function (e) {
                if (e.key === 'Enter') { set(kind, null); if (e.preventDefault) e.preventDefault(); return; }
                var v = keyed(kind, e.key, !!e.shiftKey, current(kind), limits(kind));
                if (v === null) return;
                set(kind, v);
                if (e.preventDefault) e.preventDefault();
            });
            h.addEventListener('dblclick', function () { set(kind, null); });
            h.addEventListener('focus', function () { paint(kind, want[kind === 'x' ? 'p' : 'c']); });
            parent.appendChild(h);
            handles[kind] = h;
            paint(kind, want[kind === 'x' ? 'p' : 'c']);
            return h;
        }

        // the sizes that were kept are put on Home before the lists come, so that nothing moves when they do
        var kept = load();
        if (wide()) { want.p = kept.p; want.c = kept.c; }
        make('x', main);
        if (want.p !== null) paint('x', want.p);
        if (want.c !== null) paint('y', want.c);

        function attach() {                              // the chat dock's handle: made when the dock is there (idempotent)
            var box = doc.getElementById ? doc.getElementById('gchat') : null;
            if (!box || box === chatBox) return;
            chatBox = box;
            make('y', box);
            fit();
        }
        function fit() {                                 // the window changed: the sizes that were asked for are clamped to the room there is now; a phone has none
            if (!wide()) {
                if (want.p !== null || want.c !== null) { want.p = null; want.c = null; save(); }
                paint('x', null); paint('y', null);
                return;
            }
            paint('x', want.p); paint('y', want.c);
        }
        win.addEventListener('resize', fit);
        if (win.matchMedia) { try { var mq = win.matchMedia(WIDE); if (mq.addEventListener) mq.addEventListener('change', fit); } catch (e) { /* the resize is enough */ } }
        attach();
        try { if (win.MutationObserver) new win.MutationObserver(attach).observe(host, { childList: true }); } catch (e) { /* the dock's own call, attach(), is the other way */ }       // (the dock comes after the first paint: its box is Home's child)
        return { attach: attach, fit: fit, sizes: function () { return { p: want.p, c: want.c }; }, reset: function () { set('x', null); set('y', null); } };
    }

    return { KEY: KEY, STEP: STEP, BIG: BIG, SIDE_MIN: SIDE_MIN, CHAT_MIN: CHAT_MIN, clamp: clamp, sideLimits: sideLimits, chatLimits: chatLimits, keyed: keyed, dragged: dragged, parse: parse, mount: mount };
});
