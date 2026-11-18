// A small fake of the DOM for the checks of the map maker's dialogs (no dependency): nodes with children, attributes, classes, a few selectors, listeners and the focus, a document with
// the keys that a dialog listens for. It is what the dialogs of web/front/mapmaker_dialog.js and mapmaker_props.js use, and nothing more: the opt-in browser check
// (tests/scripts/test_web_mapmaker.sh) covers what a fake cannot (layout, real focus, pointer and touch events).
//
// The focus and the boxes behave as a browser's do where a page can get them wrong: a box that is taken out of the page (or moved, which is the same to a browser) loses the focus; setting a
// box's value to another text puts the caret at its end; and `doc.typeKey`, `doc.typeText` and `doc.compose` are a person at the keyboard (`doc.typeCheck`, `doc.caretCheck` and `doc.composeCheck` judge what they did): every key goes to the element that has the focus
// (to the page's body when none has: nothing happens), the box stops at its maxlength, a key is one `input` event with the caret and the selection where a browser has them, a composition
// (an input method) is cut off when its box is taken from under it.
'use strict';

function makeDocument() {
    const doc = { listeners: {}, activeElement: null };
    function matches(node, text) {                                          // (a list with commas; a part may hold descendants: "a b")
        return text.split(',').some((part) => {
            const chain = part.trim().split(/\s+/);
            if (!simple(node, chain[chain.length - 1])) return false;
            let up = node.parentNode;
            for (let i = chain.length - 2; i >= 0; i--) {
                while (up && !simple(up, chain[i])) up = up.parentNode;
                if (!up) return false;
                up = up.parentNode;
            }
            return true;
        });
    }
    function simple(node, part) {
        return [part].some((part) => {
            const m = /^\s*([a-z0-9]*)((?:\[[^\]]+\]|:not\((?:\[[^\]]+\])\)|:checked|[.#][\w-]+)*)\s*$/.exec(part);
            if (!m) throw new Error('the fake DOM does not know the selector ' + part);
            if (m[1] && node.tagName !== m[1]) return false;
            const rest = m[2].match(/:not\(\[[^\]]+\]\)|\[[^\]]+\]|:checked|[.#][\w-]+/g) || [];
            return rest.every((q) => {
                if (q === ':checked') return !!node.checked;
                if (q[0] === '#') return node.id === q.slice(1);
                if (q[0] === '.') return node.className.split(/\s+/).includes(q.slice(1));
                const negate = q.startsWith(':not('), inner = negate ? q.slice(5, -1) : q;
                const am = /^\[([\w-]+)(?:=("?)([^"\]]*)\2)?\]$/.exec(inner);
                const has = am[1] === 'disabled' ? !!node.disabled : node.hasAttribute(am[1]);
                const ok = has && (am[3] === undefined || node.getAttribute(am[1]) === am[3]);
                return negate ? !ok : ok;
            });
        });
    }
    function lose(c) { if (doc.activeElement && c.contains && c.contains(doc.activeElement)) doc.activeElement = doc.body; }        // (a box that leaves the page loses the focus: the browser's body has it)
    function node(tagName) {
        const n = { tagName, className: '', children: [], parentNode: null, attrs: {}, listeners: {}, own: '', style: {}, disabled: false, checked: false, id: '', type: '', selectionStart: 0, selectionEnd: 0, composing: false };
        let value = '';
        Object.defineProperty(n, 'value', { get: () => value, set: (v) => { v = String(v); if (v !== value) { value = v; n.selectionStart = n.selectionEnd = v.length; } } });
        n.edit = (v, caret) => { value = v; n.selectionStart = n.selectionEnd = caret; };       // (what a person's typing does: the caret goes where the typing ended)
        n.setSelectionRange = (a, b) => { n.selectionStart = Math.max(0, Math.min(value.length, a)); n.selectionEnd = Math.max(n.selectionStart, Math.min(value.length, b === undefined ? a : b)); };
        n.select = () => n.setSelectionRange(0, value.length);
        n.appendChild = (c) => { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = n; n.children.push(c); return c; };
        n.insertBefore = (c, ref) => { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = n; const at = ref ? n.children.indexOf(ref) : -1; if (at < 0) n.children.push(c); else n.children.splice(at, 0, c); return c; };
        n.removeChild = (c) => { lose(c); n.children.splice(n.children.indexOf(c), 1); c.parentNode = null; return c; };
        n.setAttribute = (k, v) => { n.attrs[k] = String(v); if (k === 'id') n.id = String(v); };
        n.getAttribute = (k) => (k in n.attrs ? n.attrs[k] : null);
        n.removeAttribute = (k) => { delete n.attrs[k]; };
        n.hasAttribute = (k) => k in n.attrs;
        n.addEventListener = (t, f) => { (n.listeners[t] = n.listeners[t] || []).push(f); };
        n.removeEventListener = (t, f) => { n.listeners[t] = (n.listeners[t] || []).filter((x) => x !== f); };
        n.classList = { add: (c) => { if (!n.className.split(/\s+/).includes(c)) n.className = (n.className + ' ' + c).trim(); }, toggle: (c, on) => { const l = n.className.split(/\s+/).filter(Boolean).filter((x) => x !== c); if (on) l.push(c); n.className = l.join(' '); } };
        n.all = () => n.children.reduce((a, c) => a.concat([c], c.all ? c.all() : []), []);
        n.querySelectorAll = (s) => n.all().filter((x) => matches(x, s));
        n.querySelector = (s) => n.querySelectorAll(s)[0] || null;
        n.contains = (o) => o === n || n.all().includes(o);
        n.focus = () => { doc.activeElement = n; };
        n.click = () => { if (!n.disabled) n.fire('click', {}); };
        n.fire = (type, e) => { e.type = type; e.target = e.target || n; for (const f of n.listeners[type] || []) f(e); };
        let next;                                                                                           // (a check may name the next node itself: the page's order of a canvas and its line)
        Object.defineProperty(n, 'nextSibling', { get: () => { if (next !== undefined) return next; const l = n.parentNode ? n.parentNode.children : []; return l[l.indexOf(n) + 1] || null; }, set: (v) => { next = v; } });
        Object.defineProperty(n, 'firstChild', { get: () => n.children[0] || null });
        Object.defineProperty(n, 'offsetParent', { get: () => (doc.body.contains(n) ? doc.body : null) });
        Object.defineProperty(n, 'textContent', { get: () => n.own + n.children.map((c) => c.textContent).join(''), set: (v) => { n.own = String(v); n.children.forEach((c) => { lose(c); c.parentNode = null; }); n.children = []; } });
        Object.defineProperty(n, 'isConnected', { get: () => doc.body.contains(n) });
        return n;
    }
    doc.createElement = node;
    doc.createTextNode = (t) => { const n = node('#text'); n.own = String(t); return n; };
    doc.body = node('body');
    doc.querySelectorAll = (sel) => doc.body.querySelectorAll(sel);
    doc.querySelector = (sel) => doc.body.querySelector(sel);
    doc.getElementById = (id) => doc.body.all().find((x) => x.id === id) || null;
    doc.activeElement = doc.body;
    doc.addEventListener = (t, f) => { (doc.listeners[t] = doc.listeners[t] || []).push(f); };
    doc.removeEventListener = (t, f) => { doc.listeners[t] = (doc.listeners[t] || []).filter((x) => x !== f); };
    // a key goes to the document's listeners (the dialog listens there) after the target's own
    doc.key = (key, extra) => {
        const e = Object.assign({ type: 'keydown', key, shiftKey: false, target: doc.activeElement, prevented: false, preventDefault() { this.prevented = true; }, stopPropagation() {} }, extra || {});
        for (const f of (doc.listeners.keydown || []).slice()) f(e);
        return e;
    };
    // ---- a person at the keyboard ----
    const NOT_TEXT = ['radio', 'checkbox', 'range', 'file', 'button', 'submit', 'hidden', 'color'];
    const texty = (n) => n.tagName === 'textarea' || (n.tagName === 'input' && NOT_TEXT.indexOf(n.type || 'text') < 0);
    function limitOf(n) { const m = n.maxLength > 0 ? n.maxLength : parseInt(n.getAttribute('maxlength'), 10); return m > 0 ? m : Infinity; }
    // One key to the element that has the focus. Returns {field, input} where `field` is that element (null when the page's body has the focus: the key goes nowhere) and `input` is whether an `input` event was made.
    doc.typeKey = (key, extra) => {
        const t = doc.activeElement;
        if (!t || t === doc.body || !t.fire) return { field: null, input: false };
        const e = Object.assign({ type: 'keydown', key, shiftKey: false, target: t, prevented: false, preventDefault() { this.prevented = true; }, stopPropagation() {} }, extra || {});
        for (const f of (t.listeners.keydown || []).slice()) f(e);
        for (const f of (doc.listeners.keydown || []).slice()) f(e);
        if (e.prevented || !texty(t) || t.disabled || t.hasAttribute('readonly')) return { field: t, input: false };
        const v = t.value, a = t.selectionStart, b = t.selectionEnd;
        let text = '', from = a, to = b, kind = 'insertText';
        if (key.length === 1) text = key;
        else if (key === 'Enter' && t.tagName === 'textarea') { text = '\n'; kind = 'insertLineBreak'; }
        else if (key === 'Backspace') { kind = 'deleteContentBackward'; if (a === b) from = Math.max(0, a - 1); }
        else if (key === 'Delete') { kind = 'deleteContentForward'; if (a === b) to = Math.min(v.length, b + 1); }
        else return { field: t, input: false };
        if (text) text = text.slice(0, Math.max(0, limitOf(t) - (v.length - (to - from))));                 // (a box stops at its maxlength)
        if (!text && from === to) return { field: t, input: false };
        t.edit(v.slice(0, from) + text + v.slice(to), from + text.length);
        t.fire('input', { inputType: kind, data: text || null, isComposing: false });
        return { field: t, input: true };
    };
    // Characters one by one; `after(char, result)` runs after each (a check of the focus after every key)
    doc.typeText = (text, after) => Array.from(text).map((ch, i) => { const r = doc.typeKey(ch === '\n' ? 'Enter' : ch); if (after) after(ch, r, i); return r; });
    // An input method: the steps are what the box holds of the composition (a, ab, abc), each one an `input` event with isComposing true; then the composition ends. Returns false when the box was taken away
    // (or lost the focus) in the middle: the composition is cut off, as a browser cuts it
    doc.compose = (steps) => {
        const t = doc.activeElement;
        if (!t || t === doc.body || !texty(t)) return false;
        const start = t.selectionStart, base = t.value.slice(0, start), rest = t.value.slice(t.selectionEnd);
        t.composing = true; t.fire('compositionstart', { data: '' });
        for (const s of steps) {
            if (doc.activeElement !== t || !t.isConnected) { t.composing = false; return false; }
            t.edit(base + s + rest, start + s.length);
            t.fire('input', { inputType: 'insertCompositionText', data: s, isComposing: true });
        }
        if (doc.activeElement !== t || !t.isConnected) { t.composing = false; return false; }
        t.composing = false; t.fire('compositionend', { data: steps[steps.length - 1] });
        return true;
    };
    // What typing does to a box. The person clicks in it (it takes the focus) and types the characters of `text` one by one; after EVERY key the box that `find()` gives must be the very node that was
    // typed in, still in the page, with the focus. At the end it holds `expected` (default: what it held plus `text`) with the caret at its end. Returns '' or the first thing that is wrong.
    doc.typeCheck = (find, text, expected) => {
        const field = find();
        if (!field) return 'the box is not in the page';
        field.focus();
        const was = field.value, chars = Array.from(text);
        for (let i = 0; i < chars.length; i++) {
            const r = doc.typeKey(chars[i] === '\n' ? 'Enter' : chars[i]), at = 'after key ' + (i + 1) + ' (' + JSON.stringify(chars[i]) + '): ';
            if (r.field !== field) return at + 'the key did not reach the box: the focus is on ' + (doc.activeElement ? doc.activeElement.tagName : 'nothing');
            if (!field.isConnected || find() !== field) return at + 'the box was taken out of the page and another put in its place';
            if (doc.activeElement !== field) return at + 'the box lost the focus';
        }
        const want = expected === undefined ? was + text : expected;
        if (field.value !== want) return 'the box holds ' + JSON.stringify(field.value) + ', not ' + JSON.stringify(want);
        if (field.selectionStart !== field.value.length || field.selectionEnd !== field.value.length) return 'the caret is at ' + field.selectionStart + ', not at the end (' + field.value.length + ')';
        return '';
    };
    // The same for an input method: the steps are the composition's text (`n`, `ni`, `\u4f60`); it must not be cut off, and the box is the same node at the end
    doc.composeCheck = (find, steps) => {
        const field = find();
        if (!field) return 'the box is not in the page';
        field.focus();
        const start = field.selectionStart;
        if (!doc.compose(steps)) return 'the composition was cut off: the box was taken from under it';
        if (!field.isConnected || find() !== field || doc.activeElement !== field) return 'the box is not the same node with the focus after the composition';
        const last = steps[steps.length - 1];
        return field.value.slice(start, start + last.length) === last && field.selectionStart === start + last.length ? '' : 'the box holds ' + JSON.stringify(field.value) + ' with the caret at ' + field.selectionStart + ' and not the composed ' + JSON.stringify(last) + ' at ' + start;
    };
    // A caret in the middle of a text, a key typed there: the text is changed at the caret and the caret is after the new character, the focus stays
    doc.caretCheck = (find, at, key) => {
        const field = find();
        if (!field) return 'the box is not in the page';
        field.focus(); field.setSelectionRange(at, at);
        const before = field.value, r = doc.typeKey(key);
        if (r.field !== field || find() !== field || doc.activeElement !== field) return 'the key did not stay in the same box with the focus';
        const want = before.slice(0, at) + key + before.slice(at);
        if (field.value !== want || field.selectionStart !== at + 1 || field.selectionEnd !== at + 1) return 'the box holds ' + JSON.stringify(field.value) + ' with the caret at ' + field.selectionStart + ', not ' + JSON.stringify(want) + ' with the caret at ' + (at + 1);
        return '';
    };
    return doc;
}

module.exports = { makeDocument };
