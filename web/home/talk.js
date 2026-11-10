// The chat's two small helpers (docs/HOME_SCREEN.md, "Emoji and voice"): an emoji button with a small picker, and a microphone button that types what the person says into the chat field. They stand beside Send. ES5, no module
// system: a page gets AntsHomeTalk, node gets module.exports (tests/scripts/web_home_talk_check.js). web/home/home.js loads this file and talk.css after the chat dock, once, and mounts it into the dock's own form; a page
// whose dock never comes has neither button, and what goes wrong in here stays here.
//   * Emoji: about ninety common ones in groups, own code and no library, each one a single code point (a character that the hub's text rules keep as it is: no joiner, no tag character, no variation selector;
//     tests/scripts/test_web_home_talk.py runs every one through hub/text.py). A pick goes in at the caret of the field, and only while the line stays within the hub's 200 code points (the field's own maxlength counts
//     UTF-16 units, 400, and an emoji outside the basic plane is two of them: so the count here is in code points, and 200 of them are never more than 400 units). Nothing is stored.
//   * Voice: the browser's own speech recognition (SpeechRecognition, or webkitSpeechRecognition), only on a secure page; a browser without it (Firefox) has no microphone button at all. It never sends: the words go into
//     the field, the interim ones too (replaced by the final ones), and the person presses Send. In Chrome and Edge the browser sends the audio to its maker's speech service; this page never sees audio, records nothing and
//     uploads nothing (no MediaRecorder, no getUserMedia), and keeps nothing: the button says so (title and label), and so does web/legal.html. It stops on a second press, on Escape, when the person types or sends, and
//     when the page is hidden. The language is the page's (navigator.language).
//   * Everything that the speech service returns is text from outside: it goes into the field as a value, never as markup.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeTalk = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var LIMIT = 200;                                           // code points of a chat line (the hub's own, and the dock's)
    var TEXT = {
        emoji: 'Emoji', emojiList: 'Emoji: choose one to add it to your message', voice: 'Voice to text: your browser\'s speech service hears you',
        asking: 'Your browser\'s speech service hears you. Allow the microphone if it asks.', listening: 'Listening. Press the microphone again to stop. Then press Send.',
        full: 'The message is full.', nothing: 'No speech was heard.', blocked: 'The microphone is blocked for this page.', mic: 'No microphone was found.',
        network: 'The speech service could not be reached.', failed: 'Voice to text did not work.'
    };
    var ERR = { 'not-allowed': 'blocked', 'service-not-allowed': 'blocked', 'no-speech': 'nothing', 'audio-capture': 'mic', 'network': 'network' };     // (what the browser says went wrong -> a line of TEXT; 'aborted' is the page's own stop)
    // The emoji, by group: code points in hex (one single code point each: the hub keeps it as it is, and none is newer than Unicode 9, which every Python of the hub knows)
    var GROUPS = [
        ['Faces', '1F600 1F603 1F604 1F601 1F606 1F605 1F602 1F923 1F60A 1F607 1F642 1F643 1F609 1F60D 1F618 1F60B 1F61C 1F60E 1F913 1F914 1F610 1F611 1F644 1F60F 1F62C 1F634 1F622 1F62D 1F62E 1F631 1F621 1F620 1F629 1F633'],
        ['Hands', '1F44D 1F44E 1F44C 1F44F 1F64C 1F64F 1F4AA 1F44B 1F91D 1F440'],
        ['Hearts and signs', '1F499 1F49A 1F49B 1F49C 1F494 1F495 1F4AF 1F4A5 1F525 2728 2B50 1F389 1F3C6 2705 274C 2753 2757 26A1'],
        ['Ants and nature', '1F41C 1F41D 1F41B 1F40C 1F98B 1F33F 1F332 1F335 1F337 1F33B 1F340 1F34E 1F36F 1F36A 1F355 2615 1F4A7 1F30A'],
        ['Games', '1F3AE 1F3B2 1F3AF 1F3C1 1F6A9 1F4A3 1F451 1F680 23F0 231B 1F4A4 1F4A1 1F512 1F514']
    ];

    function chr(cp) { return cp < 0x10000 ? String.fromCharCode(cp) : String.fromCharCode(0xd800 + ((cp - 0x10000) >> 10), 0xdc00 + ((cp - 0x10000) & 0x3ff)); }
    function emoji() { return GROUPS.map(function (g) { return { name: g[0], list: g[1].split(' ').map(function (h) { return chr(parseInt(h, 16)); }) }; }); }
    function points(s) { var n = 0; for (var i = 0; i < s.length; i++) { var c = s.charCodeAt(i); if (!(c >= 0xdc00 && c <= 0xdfff)) n++; } return n; }
    function head(s, n) {                                      // the first n code points of s (a pair of surrogates is never cut)
        var i = 0, k = 0;
        while (i < s.length && k < n) { var c = s.charCodeAt(i); i += c >= 0xd800 && c <= 0xdbff && i + 1 < s.length ? 2 : 1; k++; }
        return s.slice(0, i);
    }
    // `text` into `value` between the caret's two ends: the new value and the caret after it, or null when the line would pass 200 code points
    function insert(value, from, to, text) {
        var a = typeof from === 'number' && from >= 0 && from <= value.length ? from : value.length, b = typeof to === 'number' && to >= a && to <= value.length ? to : a;
        var next = value.slice(0, a) + text + value.slice(b);
        return points(next) > LIMIT ? null : { value: next, caret: a + text.length };
    }
    // what is said, into the field around the place where the person was: no markup characters (the hub refuses them), one space between words, at most what is left of the 200 code points
    function speak(before, said, after) {
        var t = String(said).replace(/[<>]/g, ' ').replace(/\s+/g, ' ').trim();
        if (!t) return { value: before + after, caret: before.length };
        t = head(t, Math.max(0, LIMIT - points(before + after) - 2));
        var lead = before && !/\s$/.test(before) ? ' ' : '', tail = after && !/^\s/.test(after) ? ' ' : '';
        return { value: before + lead + t + tail + after, caret: (before + lead + t).length };
    }
    function recognition(win) {                                // the browser's constructor, or null: none, or a page that is not secure (the microphone is not offered there)
        if (!win || win.isSecureContext !== true) return null;
        var C = win.SpeechRecognition || win.webkitSpeechRecognition;
        return typeof C === 'function' ? C : null;
    }

    function mount(env) {
        var doc = env.doc, win = env.win, dock = env.dock, field = dock.field, form = field.parentNode;
        if (!form) return null;
        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
        function button(cls, label, text) { var b = el('button', 'btn ' + cls, text); b.type = 'button'; b.setAttribute('aria-label', label); b.setAttribute('title', label); return b; }
        var send = null, i;
        for (i = 0; i < form.children.length; i++) if (String(form.children[i].className).indexOf('gc-send') !== -1) { send = form.children[i]; break; }
        var tools = el('span', 'gt-tools'), emojiBtn = button('gt-btn gt-emoji', TEXT.emoji, ''), micBtn = null;
        emojiBtn.appendChild(el('i', 'gt-face')); emojiBtn.setAttribute('aria-expanded', 'false'); emojiBtn.setAttribute('aria-controls', 'gt-pick');
        tools.appendChild(emojiBtn);
        var Ctor = recognition(win);
        if (Ctor) { micBtn = button('gt-btn gt-mic', TEXT.voice, ''); micBtn.appendChild(el('i', 'gt-mark')); micBtn.setAttribute('aria-pressed', 'false'); tools.appendChild(micBtn); }
        var say = el('p', 'gt-say'); say.setAttribute('role', 'status'); say.setAttribute('aria-live', 'polite'); say.hidden = true;
        var pick = el('div', 'gt-pick'); pick.id = 'gt-pick'; pick.setAttribute('role', 'group'); pick.setAttribute('aria-label', TEXT.emojiList); pick.hidden = true;
        var keys = [];
        emoji().forEach(function (g) {
            var row = el('div', 'gt-row'); row.setAttribute('role', 'group'); row.setAttribute('aria-label', g.name); row.appendChild(el('span', 'gt-name', g.name));
            g.list.forEach(function (e) { var b = el('button', 'gt-e', e); b.type = 'button'; b.tabIndex = -1; keys.push(b); row.appendChild(b); });
            pick.appendChild(row);
        });
        form.insertBefore(say, form.children[0] || null); form.insertBefore(pick, say.nextSibling);
        if (send) form.insertBefore(tools, send); else form.appendChild(tools);
        form.className = String(form.className) + ' gt-on';

        var open = false, run = null, at = 0, said = '';
        function note(text) { said = text; say.textContent = text; say.hidden = !text; }
        function sync() { tools.hidden = !!field.disabled; if (field.disabled || form.hidden) { drop(); close(false); } }
        function fit() {                                       // the picker lies over the log, above the form: no taller than the room above the form (72 to 180 px): up to the top of the dock's body where the dock clips what is outside it (a phone), else up to the window's top
            var clips = win.getComputedStyle && dock.root && win.getComputedStyle(dock.root).overflow === 'hidden';
            var floor = clips && form.parentNode && form.parentNode.getBoundingClientRect ? form.parentNode.getBoundingClientRect().top : 8;
            pick.style.maxHeight = Math.max(72, Math.min(180, Math.floor(form.getBoundingClientRect().top - floor - 6))) + 'px';
        }
        function rove(n) { at = Math.max(0, Math.min(keys.length - 1, n)); keys.forEach(function (b, k) { b.tabIndex = k === at ? 0 : -1; }); if (keys[at].focus) keys[at].focus(); }
        function close(back) { if (!open) return; open = false; pick.hidden = true; emojiBtn.setAttribute('aria-expanded', 'false'); if (back && field.focus) field.focus(); }
        function toggle() {
            if (open) { close(true); return; }
            open = true; pick.hidden = false; emojiBtn.setAttribute('aria-expanded', 'true'); if (said === TEXT.full) note('');
            fit(); rove(at);
        }
        function choose(text) {
            var r = insert(field.value, field.selectionStart, field.selectionEnd, text);
            if (!r) { note(TEXT.full); return; }
            note(''); field.value = r.value;
            if (field.setSelectionRange) field.setSelectionRange(r.caret, r.caret);
        }
        // ---- voice
        function paint(me, words) { if (micBtn) { micBtn.setAttribute('aria-pressed', run === me && !me.over ? 'true' : 'false'); micBtn.className = 'btn gt-btn gt-mic' + (run === me && !me.over ? ' gt-hot' : ''); } note(words); }
        function draw(me) { var r = speak(me.before, me.text, me.after); field.value = r.value; if (field.setSelectionRange) field.setSelectionRange(r.caret, r.caret); }
        function drop() {                                      // away at once: nothing that the recognizer still says is heard (a send, typing, a page that goes, a field that is off)
            var me = run; if (!me) return;
            run = null; me.over = true; paint(me, '');
            try { me.rec.abort(); } catch (e) { /* it has ended */ }
        }
        function finish(me) {
            if (run !== me) return;
            run = null; me.over = true;
            paint(me, me.error ? TEXT[ERR[me.error] || 'failed'] : me.full ? TEXT.full : (me.text.trim() ? '' : TEXT.nothing));
        }
        function listen() {
            if (run) { run.over = true; paint(run, ''); try { run.rec.stop(); } catch (e) { drop(); } return; }     // (a second press: what was heard so far stays, and the final words may still come)
            var v = field.value, a = typeof field.selectionStart === 'number' ? field.selectionStart : v.length, b = typeof field.selectionEnd === 'number' ? field.selectionEnd : a;
            var me = { rec: null, before: v.slice(0, a), after: v.slice(b), text: '', error: '', over: false, full: false };
            try {
                var rec = new Ctor();
                rec.lang = (win.navigator && win.navigator.language) || 'en-US'; rec.continuous = true; rec.interimResults = true; rec.maxAlternatives = 1;
                rec.onstart = function () { if (run === me && !me.over) paint(me, TEXT.listening); };
                rec.onresult = function (e) {
                    if (run !== me) return;
                    var t = '', rs = (e && e.results) || [];
                    for (var k = 0; k < rs.length; k++) if (rs[k] && rs[k][0] && typeof rs[k][0].transcript === 'string') t += rs[k][0].transcript + ' ';
                    me.text = t; draw(me);
                    if (points(field.value) >= LIMIT - 2 && !me.over) { me.over = true; me.full = true; try { rec.stop(); } catch (x) { /* ended */ } paint(me, TEXT.full); }
                };
                rec.onerror = function (e) { if (run === me && e && e.error !== 'aborted') me.error = String(e.error || 'failed'); };
                rec.onend = function () { finish(me); };
                me.rec = rec; run = me; paint(me, TEXT.asking);
                rec.start();
            } catch (e) { if (run === me) run = null; me.over = true; paint(me, TEXT.failed); }
        }
        emojiBtn.addEventListener('click', function () { toggle(); });
        if (micBtn) micBtn.addEventListener('click', function () { if (open) close(false); listen(); });
        keys.forEach(function (b, k) { b.addEventListener('click', function () { at = k; choose(b.textContent); }); });
        pick.addEventListener('keydown', function (e) {
            if (!e) return;
            var key = e.key, to = key === 'ArrowRight' || key === 'ArrowDown' ? at + 1 : key === 'ArrowLeft' || key === 'ArrowUp' ? at - 1 : key === 'Home' ? 0 : key === 'End' ? keys.length - 1 : -1;
            if (to === -1) return;
            if (e.preventDefault) e.preventDefault();
            rove(to);
        });
        form.addEventListener('keydown', function (e) {        // (before the dock's own Escape, which folds the panel: the picker, or the listening, is closed first)
            if (!e || e.key !== 'Escape' || !(open || run)) return;
            if (e.stopPropagation) e.stopPropagation();
            if (open) close(true); else listen();
        });
        form.addEventListener('submit', function () { drop(); close(false); });
        field.addEventListener('input', function () { if (run) drop(); else if (said === TEXT.full) note(''); });
        doc.addEventListener('visibilitychange', function () { if (doc.hidden) { drop(); close(false); } });
        doc.addEventListener('pointerdown', function (e) { if (open && e && e.target && !form.contains(e.target)) close(false); });
        win.addEventListener('pagehide', function () { drop(); });
        win.addEventListener('resize', function () { if (open) fit(); });
        dock.tap(function () { sync(); });
        sync();
        return { root: tools, pick: pick, say: say, emoji: emojiBtn, mic: micBtn, field: field, drop: drop, close: close };
    }

    return { LIMIT: LIMIT, TEXT: TEXT, ERR: ERR, GROUPS: GROUPS, emoji: emoji, points: points, head: head, insert: insert, speak: speak, recognition: recognition, mount: mount };
});
