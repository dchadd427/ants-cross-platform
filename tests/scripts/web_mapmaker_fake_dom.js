// A small fake of the DOM for the checks of the map maker's dialogs (no dependency): nodes with children, attributes, classes, a few selectors, listeners and the focus, a document with
// the keys that a dialog listens for. It is what the dialogs of web/front/mapmaker_dialog.js and mapmaker_props.js use, and nothing more: the opt-in browser check
// (tests/scripts/test_web_mapmaker.sh) covers what a fake cannot (layout, real focus, pointer and touch events).
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
    function node(tagName) {
        const n = { tagName, className: '', children: [], parentNode: null, attrs: {}, listeners: {}, own: '', style: {}, disabled: false, checked: false, value: '', id: '', type: '' };
        n.appendChild = (c) => { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = n; n.children.push(c); return c; };
        n.insertBefore = (c, ref) => { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = n; const at = ref ? n.children.indexOf(ref) : -1; if (at < 0) n.children.push(c); else n.children.splice(at, 0, c); return c; };
        n.removeChild = (c) => { n.children.splice(n.children.indexOf(c), 1); c.parentNode = null; return c; };
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
        Object.defineProperty(n, 'firstChild', { get: () => n.children[0] || null });
        Object.defineProperty(n, 'offsetParent', { get: () => (doc.body.contains(n) ? doc.body : null) });
        Object.defineProperty(n, 'textContent', { get: () => n.own + n.children.map((c) => c.textContent).join(''), set: (v) => { n.own = String(v); n.children.forEach((c) => { c.parentNode = null; }); n.children = []; } });
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
    return doc;
}

module.exports = { makeDocument };
