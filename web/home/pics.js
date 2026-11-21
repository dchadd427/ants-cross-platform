// The images in Home's chat, for everybody who reads it (docs/HUB_IMAGES.md, docs/HOME_CHAT.md "Images"): the thumbnail of a line that carries an image, the box that shows it full size, the way an animated one plays
// and rests, and the reader's switch for animation. Loaded by the chat dock (env.load) the first time that a line with an image is drawn, with web/home/pics.css; a visit with no image downloads none of it. ES5, no
// module system: a page gets AntsHomePics, node gets module.exports (tests/scripts/web_home_images_check.js).
//   * The hub names an image by 32 lower-case hex digits and a type (`img` of a `line`); chat.js has checked both, and this file checks them again: the only address that it makes is /hub/img/<name>.<jpg|png|gif> or
//     <name>.t.jpg, from constants and those two checked parts. No text of a line is ever an address of an image; nothing is put in the page as markup (createElement, textContent, setAttribute only).
//   * A thumbnail is a button with an img of the thumbnail's own size (width and height from the frame, so that the log does not jump), loading lazily, with the caption (or "Image from NAME") for its alt. A press
//     opens the viewer: a dialog over the page with the caption, the size, "Open in a new tab" (a plain link to the file), Close, and, for a moderator or an administrator or the image's own author, Delete (a question
//     first; a moderator's is the hub's own /del, the author's is the hub's `delimg`).
//   * An animated image is a GIF. In the log it shows the still thumbnail and swaps to the GIF only while it is on the screen (an IntersectionObserver on the log), the window is visible, the reader's switch is on and
//     the device does not ask for reduced motion (prefers-reduced-motion): otherwise the still. The switch is a box in the account's dropdown, kept in this browser only.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomePics = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var DIR = '/hub/img/', NAME = /^[0-9a-f]{32}$/, TYPES = { jpg: 1, png: 1, gif: 1 }, KEY = 'ants.chat.animate';
    var TEXT = { open: 'Open the image', from: 'Image from ', gone: 'Image not available', close: 'Close', tab: 'Open in a new tab', del: 'Delete this image', delOwn: 'Delete my image', cancel: 'Cancel', yes: 'Delete',
        ask: 'Delete this image? It goes from every screen and its file is deleted from the site’s server. This cannot be undone.', viewer: 'Image', animate: 'Play animated images', wrong: 'That did not work, try again',
        rate: 'Too fast: wait a moment and try again.' };
    var SVG = 'http://www.w3.org/2000/svg';

    function whole(n, most) { return typeof n === 'number' && n >= 1 && n <= most && Math.floor(n) === n; }
    function checked(img) { return !!img && typeof img === 'object' && typeof img.n === 'string' && NAME.test(img.n) && typeof img.x === 'string' && TYPES[img.x] === 1 && whole(img.tw, 400) && whole(img.th, 400) && whole(img.w, 8192) && whole(img.h, 8192); }
    function address(img, thumb) { return checked(img) ? DIR + img.n + (thumb ? '.t.jpg' : '.' + img.x) : ''; }
    function sizeWords(img) { return img.w + ' × ' + img.h + ' · ' + img.x.toUpperCase(); }

    function mount(env) {
        var doc = env.doc, win = env.win, dock = env.dock, store = null, on = true, reduced = false, seen = null, many = [], viewer = null, box = null;
        try { store = win.localStorage || null; } catch (e) { store = null; }
        try { on = !store || store.getItem(KEY) !== '0'; } catch (e) { on = true; }
        try { var mq = win.matchMedia && win.matchMedia('(prefers-reduced-motion: reduce)'); reduced = !!(mq && mq.matches); if (mq && mq.addEventListener) mq.addEventListener('change', function () { reduced = !!mq.matches; paint(); }); } catch (e) { reduced = false; }

        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
        function icon(d, cls) {
            var s = doc.createElementNS(SVG, 'svg'), p = doc.createElementNS(SVG, 'path');
            s.setAttribute('viewBox', '0 0 24 24'); s.setAttribute('aria-hidden', 'true'); s.setAttribute('focusable', 'false'); if (cls) s.setAttribute('class', cls);
            p.setAttribute('d', d); s.appendChild(p); return s;
        }
        function plays() { return on && !reduced && !doc.hidden; }

        // ---- the animated images that are in the log: each is a thumbnail whose picture is the still, or the GIF while it is on the screen
        function paint() { many.forEach(function (m) { show(m, plays() && m.visible); }); }
        function show(m, playing) {
            var want = playing ? address(m.img) : address(m.img, true);
            if (m.shown !== want) { m.shown = want; m.node.setAttribute('src', want); }
        }
        function watch(m) {
            many.push(m);
            if (!win.IntersectionObserver || !dock.log) { m.visible = true; show(m, plays()); return; }
            if (!seen) seen = new win.IntersectionObserver(function (list) { list.forEach(function (it) { for (var i = 0; i < many.length; i++) if (many[i].node === it.target) { many[i].visible = it.isIntersecting; show(many[i], plays() && it.isIntersecting); } }); }, { root: dock.log });
            m.visible = false; show(m, false); seen.observe(m.node);
        }
        function forget() { many = many.filter(function (m) { var live = m.node.isConnected !== false && !!m.node.parentNode; if (!live && seen) seen.unobserve(m.node); return live; }); }
        doc.addEventListener('visibilitychange', paint);

        // ---- one line: the thumbnail, after the words
        function draw(r, e) {
            var img = e.img, holder = el('span', 'gc-pic'), b = el('button', 'gc-thumb'), im, still = e.text || TEXT.from + e.who;
            if (!checked(img)) return;
            b.type = 'button'; b.setAttribute('aria-label', e.text ? TEXT.open + ': ' + e.text : TEXT.open + ' by ' + e.who);
            im = el('img'); im.setAttribute('alt', still); im.setAttribute('width', String(img.tw)); im.setAttribute('height', String(img.th)); im.setAttribute('loading', 'lazy'); im.setAttribute('decoding', 'async'); im.setAttribute('draggable', 'false');
            if (im.style && im.style.setProperty) { im.style.setProperty('--tw', String(img.tw)); im.style.setProperty('--th', String(img.th)); }
            im.setAttribute('src', address(img, true));
            im.addEventListener('error', function () {
                if (im.parentNode) im.parentNode.removeChild(im);
                b.className += ' gc-nopic'; b.disabled = true; b.textContent = ''; b.appendChild(icon('M3.5 5h17v14h-17zM3.5 17.5l5-4.5 4 3.2M14 8l6 6M20 8l-6 6')); b.appendChild(doc.createTextNode(TEXT.gone));
            });
            b.appendChild(im); if (img.a) b.appendChild(icon('M5 10V5h5M19 14v5h-5M5 5l5.5 5.5M19 19l-5.5-5.5', 'gc-zoom'));
            b.addEventListener('click', function () { open(e, b); });
            holder.appendChild(b); r.appendChild(holder);
            forget();
            if (img.a) watch({ img: img, node: im, visible: false, shown: '' });
        }

        // ---- the viewer
        function close(back) {
            if (!viewer) return;
            if (viewer.node.parentNode) viewer.node.parentNode.removeChild(viewer.node);
            doc.removeEventListener('keydown', viewer.keys, true);
            var focus = viewer.back; viewer = null;
            if (back !== false && focus && focus.focus) focus.focus();
        }
        function open(e, back) {
            var img = e.img, scrim, card, head, who, close1, im, foot, size, link, gone = null, closeB, askP;
            if (viewer) close(false);
            scrim = el('div', 'gv-scrim'); scrim.setAttribute('role', 'dialog'); scrim.setAttribute('aria-modal', 'true'); scrim.setAttribute('aria-label', TEXT.viewer + ' · ' + e.who);
            card = el('div', 'gv'); head = el('div', 'gv-head'); who = el('div', 'gv-who');
            who.appendChild(el('b', '', e.who));
            if (e.at !== undefined && dock.clock) { var c = dock.clock(e.at); if (c) who.appendChild(el('span', 'gv-when', '  ' + c.time)); }
            if (e.text) who.appendChild(el('span', 'gv-cap', e.text));
            close1 = el('button', 'btn gv-x'); close1.type = 'button'; close1.setAttribute('aria-label', TEXT.close); close1.appendChild(icon('M6 6l12 12M18 6L6 18'));
            head.appendChild(who); head.appendChild(close1);
            im = el('img', 'gv-img'); im.setAttribute('alt', e.text || TEXT.from + e.who); im.setAttribute('src', address(img, img.a && !plays()));
            foot = el('div', 'gv-foot'); size = el('span', 'gv-size', sizeWords(img));
            link = el('a', 'btn', TEXT.tab); link.setAttribute('href', address(img)); link.setAttribute('target', '_blank'); link.setAttribute('rel', 'noopener noreferrer');
            closeB = el('button', 'btn', TEXT.close); closeB.type = 'button';
            function normal() {
                foot.textContent = ''; foot.appendChild(size); foot.appendChild(link);
                var line = dock.line ? dock.line(e.id) : null, mine = !!(line && line.own), staff = !!(dock.you && dock.you() && dock.you().role !== 'none');
                if (line && (mine || staff)) { var d = el('button', 'btn gv-del', mine && !staff ? TEXT.delOwn : TEXT.del); d.type = 'button'; d.addEventListener('click', ask); foot.appendChild(d); }
                foot.appendChild(closeB);
            }
            function ask() {
                foot.textContent = ''; askP = el('p', 'gv-ask', TEXT.ask); foot.appendChild(askP);
                var no = el('button', 'btn', TEXT.cancel), yes = el('button', 'btn gv-del', TEXT.yes); no.type = 'button'; yes.type = 'button';
                no.addEventListener('click', normal);
                yes.addEventListener('click', function () {
                    yes.disabled = true;
                    var line = dock.line ? dock.line(e.id) : null, staff = !!(dock.you && dock.you() && dock.you().role !== 'none');
                    var done = function (code) { if (code === 'ok') close(); else { askP.textContent = code === 'rate' ? TEXT.rate : TEXT.wrong; yes.disabled = false; } };
                    if (line && line.own && !staff) dock.delimg(e.id, done); else dock.command('/del ' + e.id, done);
                });
                foot.appendChild(no); foot.appendChild(yes); no.focus();
            }
            normal();
            card.appendChild(head); card.appendChild(im); card.appendChild(foot); scrim.appendChild(card);
            close1.addEventListener('click', function () { close(); }); closeB.addEventListener('click', function () { close(); });
            scrim.addEventListener('click', function (ev) { if (ev.target === scrim) close(); });
            viewer = { node: scrim, back: back, id: e.id, keys: function (ev) {
                if (ev.key === 'Escape') { ev.preventDefault(); ev.stopPropagation(); close(); return; }
                if (ev.key !== 'Tab') return;
                var all = [close1].concat(Array.prototype.slice.call(foot.querySelectorAll('a, button')));
                var at = all.indexOf(doc.activeElement), next = ev.shiftKey ? (at <= 0 ? all.length - 1 : at - 1) : (at < 0 || at === all.length - 1 ? 0 : at + 1);
                ev.preventDefault(); all[next].focus();
            } };
            doc.addEventListener('keydown', viewer.keys, true);
            (doc.body || dock.root).appendChild(scrim); closeB.focus();
        }

        // ---- the reader's switch, in the account's dropdown (kept in this browser only)
        function switchRow() {
            var slot = doc.getElementById ? doc.getElementById('gchat-login-slot') : null, row, label, cb, text;
            if (!slot || slot.querySelector('.gp-sw')) return;
            row = el('div', 'gc-sw gp-sw'); label = el('label', 'gc-swl'); cb = el('input', 'gc-swb'); cb.type = 'checkbox'; cb.checked = on;
            text = el('span', 'gc-swt', TEXT.animate); label.appendChild(cb); label.appendChild(text); row.appendChild(label);
            cb.addEventListener('change', function () { on = !!cb.checked; try { if (store) store.setItem(KEY, on ? '1' : '0'); } catch (e) { /* no storage: the choice lasts until the page is closed */ } paint(); });
            slot.insertBefore(row, slot.querySelector('.gc-sw') ? slot.querySelector('.gc-sw').nextSibling : slot.firstChild);
        }
        switchRow();
        return { draw: draw, switchRow: switchRow, open: open, close: close, paint: paint, animating: plays, state: function () { return { on: on, reduced: reduced, watching: many.length, viewing: viewer ? viewer.id : null }; } };
    }

    return { DIR: DIR, KEY: KEY, TEXT: TEXT, checked: checked, address: address, sizeWords: sizeWords, mount: mount };
});
