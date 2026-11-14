// Runs the real web/site_nav.js on the real markup of pages in node, with a small fake of the DOM (a parser for the page's tags, a few selectors, the Menu's button), and prints what the opened Menu holds as
// JSON. Used by tests/scripts/test_web_site_nav.py (the Menu's last line, the links, the pages' footers). Usage: node web_site_nav_menu_check.js MANIFEST.json, where the manifest is
// [{"name": ..., "path": ..., "live": ...}, ...] (the page files, stamped or not, as the test made them; "live" is what a fake /live does: {"count": N}, "fail" (the request is rejected), "status" (a 500),
// "never" (it does not answer), "notlist" (an answer that is not a list) or absent (an empty list)); the answer is {name: {footers, versionIds, items: [{tag, cls, text, href}], hrefs, fetched, hidden, mark}},
// where mark is the bar's live mark after the answer: {there, hidden, text, label, href}.
'use strict';
const fs = require('fs');
const path = require('path');

const SOURCE = fs.readFileSync(path.join(__dirname, '..', '..', 'web', 'site_nav.js'), 'utf8');
const VOID = new Set(['img', 'br', 'hr', 'input', 'meta', 'link', 'source', 'path']);

class El {
    constructor(tag) { this.tagName = tag.toUpperCase(); this.children = []; this.attrs = {}; this.style = {}; this.parentNode = null; this.listeners = {}; this.own = ''; }
    get hidden() { return this.hasAttribute('hidden'); }
    set hidden(v) { if (v) this.attrs.hidden = ''; else delete this.attrs.hidden; }
    get firstChild() { return this.children[0] || null; }
    get className() { return this.attrs['class'] || ''; }
    set className(v) { this.attrs['class'] = String(v); }
    get id() { return this.attrs.id || ''; }
    set id(v) { this.attrs.id = String(v); }
    get href() { return this.attrs.href; }
    set href(v) { this.attrs.href = String(v); }
    set target(v) { this.attrs.target = String(v); }
    set rel(v) { this.attrs.rel = String(v); }
    get classList() {
        const el = this, list = () => el.className.split(/\s+/).filter(Boolean);
        return { contains: (c) => list().indexOf(c) >= 0, toggle: (c, on) => { const l = list().filter((x) => x !== c); if (on === undefined ? list().indexOf(c) < 0 : on) l.push(c); el.className = l.join(' '); } };
    }
    appendChild(c) { c.parentNode = this; this.children.push(c); return c; }
    setAttribute(k, v) { this.attrs[k] = String(v); }
    getAttribute(k) { return Object.prototype.hasOwnProperty.call(this.attrs, k) ? this.attrs[k] : null; }
    hasAttribute(k) { return Object.prototype.hasOwnProperty.call(this.attrs, k); }
    get textContent() { return this.own + this.children.map((c) => c.textContent).join(''); }
    set textContent(t) { this.children = []; this.own = String(t); }
    addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); }
    focus() {}
    getBoundingClientRect() { return { bottom: 64 }; }
    contains(n) { for (let x = n; x; x = x.parentNode) if (x === this) return true; return false; }
    matches(part) {
        if (part[0] === '.') return this.className.split(/\s+/).indexOf(part.slice(1)) >= 0;
        if (part[0] === '#') return this.id === part.slice(1);
        return this.tagName === part.toUpperCase();
    }
    all() { const out = []; (function walk(n) { n.children.forEach((c) => { out.push(c); walk(c); }); })(this); return out; }
    querySelectorAll(sel) {
        const parts = sel.split(/\s+/), root = this;
        return this.all().filter((el) => {
            if (!el.matches(parts[parts.length - 1])) return false;
            let at = el.parentNode;
            for (let i = parts.length - 2; i >= 0; i--) {
                while (at && at !== root && !at.matches(parts[i])) at = at.parentNode;
                if (!at || at === root) return false;
                at = at.parentNode;
            }
            return true;
        });
    }
    querySelector(sel) { return this.querySelectorAll(sel)[0] || null; }
}

function parse(html) {
    const clean = html.replace(/<!--[\s\S]*?-->/g, '').replace(/<(script|style)\b[^>]*>[\s\S]*?<\/\1>/g, '');
    const root = new El('html'), stack = [root], re = /<(\/?)([a-zA-Z][\w-]*)((?:\s+[\w:-]+(?:="[^"]*")?)*)\s*(\/?)>|([^<]+)/g;
    let m;
    while ((m = re.exec(clean))) {
        const top = stack[stack.length - 1];
        if (m[5] !== undefined) { if (m[5].trim()) { const t = new El('#text'); t.own = m[5]; top.appendChild(t); } continue; }
        const tag = m[2].toLowerCase();
        if (m[1]) { for (let i = stack.length - 1; i > 0; i--) if (stack[i].tagName === tag.toUpperCase()) { stack.length = i; break; } continue; }
        const el = new El(tag);
        for (const a of m[3].matchAll(/([\w:-]+)(?:="([^"]*)")?/g)) el.attrs[a[1]] = a[2] === undefined ? '' : a[2];
        top.appendChild(el);
        if (!VOID.has(tag) && !m[4]) stack.push(el);
    }
    return root;
}

async function run(file, live) {
    const root = parse(fs.readFileSync(file, 'utf8')), body = root.querySelector('body') || root, fetched = [];
    const listeners = {};
    const document = {
        body, location: { pathname: '/page' }, createElement: (t) => new El(t),
        querySelector: (s) => root.querySelector(s), addEventListener: (t, f) => { (listeners[t] = listeners[t] || []).push(f); },
    };
    const window = { innerHeight: 800, addEventListener() {} };
    const fetchFake = (url) => {
        fetched.push(url);
        if (live === 'fail') return Promise.reject(new Error('offline'));
        if (live === 'never') return new Promise(() => {});
        if (live === 'status') return Promise.resolve({ ok: false, json: () => ({ live: [{}] }) });
        if (live === 'notlist') return Promise.resolve({ ok: true, json: () => ({ live: 'x', count: 3 }) });
        const n = live && live.count ? live.count : 0;
        return Promise.resolve({ ok: true, json: () => ({ live: Array.from({ length: n }, (_, i) => ({ id: 'M-20260101-00000' + (i % 10) + 'Z' })) }) });
    };
    new Function('document', 'window', 'fetch', SOURCE)(document, window, fetchFake);
    const button = root.querySelector('.sb .sbtn');
    (button.listeners.click || []).forEach((f) => f({ target: button }));
    for (let i = 0; i < 5; i++) await new Promise((resolve) => setImmediate(resolve));          // (the answer of the fake /live, and what the bar does with it)
    const box = body.children.filter((c) => c.id === 'sbm')[0], top = root.querySelector('.sb .sblive');
    const items = box ? box.children.map((c) => ({ tag: c.tagName, cls: c.className, text: c.textContent, href: c.getAttribute('href'), links: c.querySelectorAll('a').map((a) => a.getAttribute('href')) })) : [];
    const hrefs = box ? box.querySelectorAll('a').map((a) => a.getAttribute('href')) : [];
    return { footers: root.querySelectorAll('footer').length, versionIds: ['game-version-line', 'game-version', 'game-build', 'game-build-id', 'game-build-date'].filter((i) => root.querySelector('#' + i)), items, hrefs, fetched, hidden: box ? box.hidden : null,
             mark: { there: !!top, hidden: top ? top.hidden : null, text: top ? top.textContent : null, label: top ? top.getAttribute('aria-label') : null, href: top ? top.getAttribute('href') : null } };
}

(async () => {
    const manifest = JSON.parse(fs.readFileSync(process.argv[2], 'utf8')), out = {};
    for (const page of manifest) out[page.name] = await run(page.path, page.live);
    process.stdout.write(JSON.stringify(out));
})();
