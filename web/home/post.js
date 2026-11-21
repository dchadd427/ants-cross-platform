// Posting an image in Home's chat (docs/HUB_IMAGES.md, docs/HOME_CHAT.md "Images"): the button beside the emoji and the microphone, a paste or a drop of an image, the card above the field that shows it (the field is its
// caption), and the upload and the line that follow. Loaded by web/home/chat.js (env.load) only when the hub's `you` says `img: "on"`, with web/home/post.css; nobody else downloads it. ES5, no module system: a page
// gets AntsHomePost, node gets module.exports (tests/scripts/web_home_images_check.js).
//   * Nothing here decides anything: the hub decides who may post, what an image is, how big, and what comes of it (the converter writes a new file; the original is never kept). This file only makes the page nicer:
//     it says at once what the hub would say (not an image, too big), and shrinks a big picture before it goes up (a canvas, no more than 2,048 px on its longer side) so that a phone's photo is a fraction of its size.
//   * The upload is one request, POST /hub/img/up, with the bytes as they are, `Content-Type: application/octet-stream` and `X-Ants-Image: 1` (the login's cookie goes along by itself; no other site can send that
//     header), made with an XMLHttpRequest for its progress. Its answer is the id of the upload, which the next `say` frame carries (chat.js `sendImage`): the account's own line then has the image. A refused line
//     leaves the card as it is, so that the person may press Send again (the id is good for ten minutes).
//   * The picture that waits is shown only as an object address of the browser (createObjectURL) in the card and is let go of when the card goes. An animated GIF keeps its animation (the converter writes it again,
//     within 100 frames, 15 seconds and 400 px), and the card says so in plain words.
//   * Everything that is said (a file's name, the hub's words) is DOM text only, never markup.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomePost = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var LIMIT = { bytes: 4 * 1024 * 1024, shrink: 2 * 1024 * 1024, side: 2048 }, ACCEPT = 'image/png,image/jpeg,image/webp,image/gif', OK = /^image\/(png|jpeg|webp|gif)$/;
    var PATH = '/hub/img/up', SVG = 'http://www.w3.org/2000/svg';
    var TEXT = {
        add: 'Add an image', addTitle: 'Add an image (you can also paste or drop one)', drop: 'Drop the image to add it', dropNote: 'PNG, JPEG, WebP or GIF, up to 4 MB. You can add a caption before it is sent.', card: 'Image to send',
        ready: 'Image ready to send', sending: 'Sending the image…', pasted: 'Pasted image', remove: 'Remove the image', closeIt: 'Close', send: 'Send image', plain: 'Send', caption: 'Add a caption (optional)',
        clean: 'Location and camera details are removed.', moving: 'An animated GIF stays animated (at most 100 frames, 15 seconds and 400 pixels).', bigT: 'That image is too big', notT: 'That is not an image the chat can use',
        another: 'Choose another image', big: ' is too big. The most is 4 MB.', not: 'Images must be PNG, JPEG, WebP or GIF. Nothing was sent.',
        frames: 'That animated image is too long (the most is 100 frames and 15 seconds).', pixels: 'That image has too many pixels.', bad: 'That file could not be read as an image. Nothing was sent.',
        rate: 'You are adding images too fast. Wait a moment.', busy: 'The chat is busy converting images. Try again in a moment.', down: 'Images are not available right now. Messages still work.', full: 'The chat has no room for more images now.',
        images: 'You cannot post images now.', notSent: 'The image was not sent', login: 'Log in with Discord to post images.', wrong: 'That did not work, try again.', refused: 'The chat did not take it. Try again.'
    };
    var CODES = { 413: 'big', 415: 'not', 429: 'rate', 401: 'login', 403: 'images' };

    function kb(n) { return n >= 1048576 ? (Math.round(n / 104857.6) / 10) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB'; }
    function wordsOf(status, code) {                                    // what the hub's answer to an upload means, as one of TEXT's keys
        if (code === 'busy' || code === 'full' || code === 'frames' || code === 'pixels' || code === 'bad' || code === 'down' || code === 'big' || code === 'not' || code === 'type' || code === 'rate' || code === 'images' || code === 'login') return code === 'type' ? 'not' : code;
        return CODES[status] || (status === 503 ? 'down' : 'wrong');
    }
    function imageOf(files) {                                           // the first file that is an image in a list of files (a paste, a drop, the chooser), or null
        for (var i = 0; files && i < files.length; i++) if (files[i] && OK.test(String(files[i].type))) return files[i];
        return null;
    }
    function hasFile(data) {                                            // whether a drag or a paste carries a file (an image or not: a drop of anything else on the page must not navigate away)
        if (!data) return false;
        var t = data.types, i;
        for (i = 0; t && i < t.length; i++) if (t[i] === 'Files') return true;
        return !!(data.files && data.files.length);
    }

    function mount(env) {
        var doc = env.doc, win = env.win, dock = env.dock, form = dock.form, field = dock.field, send = null, card = null, over = null, btn = null, chooser = null, pending = null, url = '', xhr = null, busy = false, token = 0;

        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
        function icon(d, cls) {
            var s = doc.createElementNS(SVG, 'svg'), p = doc.createElementNS(SVG, 'path');
            s.setAttribute('viewBox', '0 0 24 24'); s.setAttribute('aria-hidden', 'true'); s.setAttribute('focusable', 'false'); if (cls) s.setAttribute('class', cls); p.setAttribute('d', d); s.appendChild(p); return s;
        }
        var PIC = 'M3.5 5h17v14h-17zM3.5 17.5l5-4.5 4 3.2 3-2.7 5 4.5M9 8.4a1.2 1.2 0 1 0 0 .01', X = 'M6 6l12 12M18 6L6 18', BROKEN = 'M3.5 5h17v14h-17zM3.5 17.5l5-4.5 4 3.2M14 8l6 6M20 8l-6 6';

        // ---- the button, beside the emoji and the microphone (talk.js makes those: this waits for them)
        function place() {
            var tools = form.querySelector ? form.querySelector('.gt-tools') : null, mic;
            if (btn && btn.parentNode) return true;
            if (!tools) return false;
            btn = el('button', 'btn gt-btn gt-pic'); btn.type = 'button'; btn.setAttribute('aria-label', TEXT.add); btn.setAttribute('title', TEXT.addTitle); btn.appendChild(icon(PIC, 'gt-ico'));
            btn.addEventListener('click', function () { choose(); });
            mic = tools.querySelector('.gt-mic'); tools.insertBefore(btn, mic || null);
            if ((win.innerWidth || 0) <= 700 && (win.innerHeight || 0) >= 501) form.className += ' gp-two';       // (a phone held upright: four buttons leave the one row too little to type in: two rows)
            return true;
        }
        function wait(tries) {
            if (place() || tries <= 0) return;
            if (win.MutationObserver) { var o = new win.MutationObserver(function () { if (place()) o.disconnect(); }); o.observe(form, { childList: true, subtree: true }); }
            else win.setTimeout(function () { wait(tries - 1); }, 300);
        }
        function choose() {
            if (!chooser) {
                chooser = el('input'); chooser.type = 'file'; chooser.setAttribute('accept', ACCEPT); chooser.hidden = true; chooser.setAttribute('aria-hidden', 'true'); chooser.setAttribute('tabindex', '-1');
                chooser.addEventListener('change', function () { var f = chooser.files && chooser.files[0]; if (f) add(f); chooser.value = ''; });
                form.appendChild(chooser);
            }
            chooser.click();
        }

        // ---- the card of the image that waits (the emoji picker's place, above the field); the field is its caption
        function drawCard(o) {
            var c = el('div', 'gp-card' + (o.state === 'bad' ? ' gp-bad' : '') + (o.state === 'sending' ? ' gp-sending' : '')), main = el('div'), x, th, bar;
            c.setAttribute('role', 'group'); c.setAttribute('aria-label', TEXT.card);
            if (o.src) { th = el('img', 'gp-th'); th.setAttribute('src', o.src); th.setAttribute('alt', ''); c.appendChild(th); } else { th = el('span', 'gp-ph'); th.appendChild(icon(BROKEN)); c.appendChild(th); }
            main.appendChild(el('p', 'gp-t', o.title)); main.appendChild(el('p', 'gp-s', o.line)); if (o.note) main.appendChild(el('p', 'gp-s' + (o.warn ? ' gp-warn' : ''), o.note)); c.appendChild(main);
            x = el('button', 'gp-x'); x.type = 'button'; x.setAttribute('aria-label', o.state === 'bad' ? TEXT.closeIt : TEXT.remove); x.setAttribute('title', o.state === 'bad' ? TEXT.closeIt : TEXT.remove); x.appendChild(icon(X));
            x.addEventListener('click', function () { clear(); field.focus && field.focus(); }); c.appendChild(x);
            if (o.state === 'sending') { bar = el('div', 'gp-bar'); bar.setAttribute('role', 'progressbar'); bar.setAttribute('aria-valuemin', '0'); bar.setAttribute('aria-valuemax', '100'); bar.setAttribute('aria-valuenow', String(o.progress || 0)); var fill = el('i'); fill.style.width = (o.progress || 0) + '%'; bar.appendChild(fill); c.appendChild(bar); }
            if (o.again) { var a = el('button', 'btn gp-again', TEXT.another); a.type = 'button'; a.addEventListener('click', function () { clear(); choose(); }); c.appendChild(a); }
            return c;
        }
        function show(o) {
            if (card && card.parentNode) card.parentNode.removeChild(card);
            card = drawCard(o); form.appendChild(card);
            if (!send) send = form.querySelector('.gc-send');
            var ready = o.state === 'ready' || o.state === 'sending';
            if (send && send.setAttribute) { if (ready) { if (!send.getAttribute('data-plain')) send.setAttribute('data-plain', send.textContent); send.textContent = TEXT.send; } else if (send.getAttribute('data-plain')) send.textContent = send.getAttribute('data-plain'); }
            if (ready && field.getAttribute && !field.getAttribute('data-plain')) { field.setAttribute('data-plain', field.getAttribute('placeholder') || ''); field.setAttribute('placeholder', TEXT.caption); }
            if (!ready && field.getAttribute && field.getAttribute('data-plain') !== null) { field.setAttribute('placeholder', field.getAttribute('data-plain')); field.removeAttribute('data-plain'); }
        }
        function clear() {
            token++; if (xhr) { try { xhr.abort(); } catch (e) { /* gone already */ } xhr = null; }
            if (url && win.URL && win.URL.revokeObjectURL) win.URL.revokeObjectURL(url); url = ''; pending = null; busy = false;
            if (card && card.parentNode) card.parentNode.removeChild(card); card = null;
            if (send && send.getAttribute && send.getAttribute('data-plain')) send.textContent = send.getAttribute('data-plain');
            if (field.getAttribute && field.getAttribute('data-plain') !== null) { field.setAttribute('placeholder', field.getAttribute('data-plain')); field.removeAttribute('data-plain'); }
        }
        function refuse(file, why) { clear(); show({ state: 'bad', title: why === 'big' ? TEXT.bigT : TEXT.notT, line: why === 'big' ? (file && file.name ? file.name : TEXT.pasted) + TEXT.big : TEXT[why] || TEXT.not, again: true }); }

        // ---- an image arrives (the button, a paste, a drop): checked, shrunk where that helps, shown
        function add(file) {
            if (busy) return;
            if (!OK.test(String(file.type))) { refuse(file, 'not'); return; }
            clear();
            var mine = ++token, made = file.name ? file.name : TEXT.pasted;
            shrink(file, function (blob, dims) {
                if (mine !== token) return;
                if (blob.size > LIMIT.bytes) { refuse(file, 'big'); return; }
                url = win.URL && win.URL.createObjectURL ? win.URL.createObjectURL(blob) : '';
                pending = { blob: blob, caption: '' };
                show({ state: 'ready', src: url, title: TEXT.ready, line: (file.name ? made : TEXT.pasted) + ' · ' + (dims ? dims.w + ' × ' + dims.h + ' · ' : '') + kb(blob.size), note: file.type === 'image/gif' ? TEXT.moving : TEXT.clean, warn: file.type === 'image/gif' });
            });
        }
        function shrink(file, done) {                                   // a canvas, only for a picture that is over 2 MiB (and never a GIF: it would stop moving); otherwise the file as it is
            if (file.type === 'image/gif' || file.size <= LIMIT.shrink || !win.createImageBitmap || !doc.createElement) { done(file, null); return; }
            win.createImageBitmap(file).then(function (bmp) {
                var k = Math.min(1, LIMIT.side / Math.max(bmp.width, bmp.height)), w = Math.max(1, Math.round(bmp.width * k)), h = Math.max(1, Math.round(bmp.height * k)), c = doc.createElement('canvas'), keep = file.type === 'image/png' && file.size < 4 * LIMIT.shrink;
                c.width = w; c.height = h; c.getContext('2d').drawImage(bmp, 0, 0, w, h); if (bmp.close) bmp.close();
                c.toBlob(function (blob) { done(blob || file, blob ? { w: w, h: h } : null); }, keep ? 'image/png' : 'image/jpeg', 0.85);
            }, function () { done(file, null); });
        }

        // ---- Send: the upload, then the line (the field's words are the caption)
        function upload(blob, ok, fail) {
            var req = new win.XMLHttpRequest(), mine = token;
            xhr = req; req.open('POST', PATH); req.setRequestHeader('Content-Type', 'application/octet-stream'); req.setRequestHeader('X-Ants-Image', '1');
            if (req.upload) req.upload.onprogress = function (ev) { if (mine === token && ev && ev.lengthComputable) show({ state: 'sending', src: url, title: TEXT.sending, line: kb(blob.size), progress: Math.round(100 * ev.loaded / ev.total) }); };
            req.onload = function () {
                if (mine !== token) return;
                var data = null; try { data = JSON.parse(req.responseText); } catch (e) { data = null; }
                if (req.status === 200 && data && typeof data.id === 'string' && /^[0-9a-f]{32}$/.test(data.id)) ok(data.id); else fail(req.status, data && data.error);
            };
            req.onerror = function () { if (mine === token) fail(0, null); }; req.ontimeout = req.onerror; req.timeout = 45000;
            req.send(blob);
        }
        function submit() {
            if (!pending || busy) return;
            var blob = pending.blob, mine = token, words = field.value;
            busy = true; show({ state: 'sending', src: url, title: TEXT.sending, line: kb(blob.size), progress: 0 });
            function failed(status, code) {
                if (mine !== token) return;
                busy = false; xhr = null;
                var key = wordsOf(status, code), again = ['big', 'not', 'pixels', 'bad', 'frames'].indexOf(key) >= 0;
                show({ state: 'bad', title: key === 'big' ? TEXT.bigT : again ? TEXT.notT : TEXT.notSent, line: key === 'big' ? 'The most is 4 MB.' : TEXT[key] || TEXT.wrong, again: again });
            }
            upload(blob, function (id) {
                if (mine !== token) return;
                xhr = null;
                var why = dock.sendImage(id, words, function (code) {
                    if (mine !== token) return; busy = false;
                    if (code === 'ok') { field.value = ''; if (dock.grow) dock.grow(); clear(); return; }
                    show({ state: 'ready', src: url, title: TEXT.ready, line: kb(blob.size), note: TEXT.refused, warn: true });
                });
                if (why) { busy = false; show({ state: 'ready', src: url, title: TEXT.ready, line: kb(blob.size), note: TEXT.refused, warn: true }); }
            }, failed);
        }

        // ---- the ways in: the form's own submit while an image waits, a paste, a drop
        (dock.root && dock.root.addEventListener ? dock.root : form).addEventListener('submit', function (ev) {       // (capture, on the box above the form: it is heard before the dock's own listener of the form, in every browser)
            if (!pending || (ev.target && ev.target !== form)) return;
            ev.preventDefault(); ev.stopImmediatePropagation(); submit();
        }, true);
        field.addEventListener('paste', function (ev) {
            var data = ev.clipboardData, f = data ? imageOf(data.files) : null, i;
            if (!f && data && data.items) for (i = 0; i < data.items.length && !f; i++) if (data.items[i].kind === 'file' && OK.test(String(data.items[i].type)) && data.items[i].getAsFile) f = data.items[i].getAsFile();
            if (!f) return;
            ev.preventDefault(); add(f);
        });
        function overOn(on) {
            if (!dock.root) return;
            if (on && !over) {
                over = el('div', 'gp-dropmsg'); over.appendChild(icon(PIC)); over.appendChild(el('b', '', TEXT.drop)); over.appendChild(el('span', '', TEXT.dropNote));
                dock.root.className += ' gp-over'; dock.root.appendChild(over);
            } else if (!on && over) { if (over.parentNode) over.parentNode.removeChild(over); over = null; dock.root.className = String(dock.root.className).replace(/ ?gp-over/, ''); }
        }
        if (dock.root && dock.root.addEventListener) {
            var depth = 0;
            dock.root.addEventListener('dragenter', function (ev) { if (hasFile(ev.dataTransfer)) { depth++; overOn(true); } });
            dock.root.addEventListener('dragover', function (ev) { if (hasFile(ev.dataTransfer)) ev.preventDefault(); });
            dock.root.addEventListener('dragleave', function (ev) { if (hasFile(ev.dataTransfer) && --depth <= 0) { depth = 0; overOn(false); } });
            dock.root.addEventListener('drop', function (ev) {
                if (!hasFile(ev.dataTransfer)) return;
                ev.preventDefault(); depth = 0; overOn(false);
                var f = imageOf(ev.dataTransfer.files);
                if (f) add(f); else if (ev.dataTransfer.files && ev.dataTransfer.files[0]) refuse(ev.dataTransfer.files[0], 'not');
            });
        }
        wait(20);
        return { add: add, submit: submit, clear: clear, place: place, state: function () { return { waiting: !!pending, busy: busy, button: !!(btn && btn.parentNode) }; } };
    }

    return { LIMIT: LIMIT, ACCEPT: ACCEPT, TEXT: TEXT, PATH: PATH, imageOf: imageOf, hasFile: hasFile, wordsOf: wordsOf, kb: kb, mount: mount };
});
