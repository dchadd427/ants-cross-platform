// Runs the page's OWN code for the changelog's pages (the inline script that tools/changelog_to_html.py writes into changelog.html) against the page that the tool made, with a small fake of the
// browser's DOM: which releases show, the page tabs and the Newer / Older bar, the address (a release id, with dots or dashes, in any case) as the one state, Back and Forward, a phone's five
// to a page, a short list that needs no pages, a missing version list, the focus, the scroll, and that no text becomes markup.
// tests/scripts/test_changelog_to_html.py runs this with node (the quick tier) and shows that it fails on broken copies of the page.
// usage: node web_changelog_pages_check.js changelog.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const vm = require('vm');

const html = fs.readFileSync(process.argv[2], 'utf8');
const found = /<script>\n([\s\S]*?)\n<\/script>/.exec(html);
if (!found) { console.log('FAIL the page has no inline script'); process.exit(1); }
const SCRIPT = found[1];

let checks = 0;
let failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }

// ---- the rule, written out again here (the page's script has to agree with it): SIZE releases to a page (SIZE_PHONE on a phone), the count of pages is the rounded quotient, the last page has the rest
const SIZE = 10, SIZE_PHONE = 5;
const pagesFor = (n, size) => Math.max(1, Math.round(n / size));
const pageOf = (k, n, size) => Math.min(Math.floor(k / size), pagesFor(n, size) - 1);
const firstOf = (p, size) => p * size;
const lastOf = (p, n, size) => (p === pagesFor(n, size) - 1 ? n - 1 : (p + 1) * size - 1);

// ---- a small fake of the DOM, enough for the script ---------------------------------------------------------------------------------------------------------------------------------------------
class Node {
    constructor(doc, tag) { this.doc = doc; this.tagName = tag.toUpperCase(); this.attrs = {}; this.children = []; this.parentNode = null; this.className = ''; this.hidden = false; this._text = ''; }
    get id() { return this.attrs.id || ''; }
    set id(v) { this.attrs.id = String(v); }
    set innerHTML(v) { throw new Error('the script made markup from text (innerHTML)'); }
    set outerHTML(v) { throw new Error('the script made markup from text (outerHTML)'); }
    insertAdjacentHTML() { throw new Error('the script made markup from text (insertAdjacentHTML)'); }
    get classList() {
        const node = this, names = () => node.className.split(/\s+/).filter(Boolean);
        const set = (list) => { node.className = list.join(' '); };
        return {
            add: (c) => { if (!names().includes(c)) set(names().concat(c)); },
            remove: (c) => set(names().filter((x) => x !== c)),
            contains: (c) => names().includes(c),
            toggle(c, force) { const on = force === undefined ? !names().includes(c) : !!force; if (on) this.add(c); else this.remove(c); return on; }
        };
    }
    get textContent() { return this.tagName === '#TEXT' ? this._text : this.children.map((c) => c.textContent).join(''); }
    set textContent(v) { this.children = []; if (String(v) !== '') { const t = new Node(this.doc, '#text'); t._text = String(v); t.parentNode = this; this.children.push(t); } }
    setAttribute(k, v) { this.attrs[k] = String(v); }
    getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
    removeAttribute(k) { delete this.attrs[k]; }
    appendChild(c) { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = this; this.children.push(c); return c; }
    insertBefore(c, ref) { if (c.parentNode) c.parentNode.removeChild(c); const at = this.children.indexOf(ref); if (at < 0) throw new Error('insertBefore: the reference is not a child'); c.parentNode = this; this.children.splice(at, 0, c); return c; }
    removeChild(c) { const at = this.children.indexOf(c); if (at < 0) throw new Error('removeChild: not a child'); this.children.splice(at, 1); c.parentNode = null; return c; }
    contains(n) { for (let x = n; x; x = x.parentNode) if (x === this) return true; return false; }
    addEventListener(type, f) { (this.listeners = this.listeners || {}); (this.listeners[type] = this.listeners[type] || []).push(f); }
    click() { const ev = { type: 'click', target: this }; for (let x = this; x; x = x.parentNode) ((x.listeners || {}).click || []).forEach((f) => f(ev)); }
    focus(options) { this.doc.activeElement = this; this.doc.focused.push(options); }
    scrollIntoView() { this.doc.scrolled.push(this); }
    walk(tag) { const out = []; const go = (n) => n.children.forEach((c) => { if (c.tagName === tag.toUpperCase()) out.push(c); go(c); }); go(this); return out; }
    getElementsByTagName(tag) { return this.walk(tag); }
}

const unescape = (s) => s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&amp;/g, '&');

// a page: sections (ids), the labels of the version list (id -> text; none: no list at all), the hash, a phone or not
function rig(options) {
    const doc = { activeElement: null, scrolled: [], focused: [], listeners: {} };
    const window = { listeners: {}, addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); } };
    const location = { hash: options.hash || '' };
    const mq = { matches: !!options.phone, listeners: [] };
    if (options.legacy) mq.addListener = (f) => mq.listeners.push(f); else mq.addEventListener = (t, f) => { if (t === 'change') mq.listeners.push(f); };
    window.matchMedia = (q) => { mq.query = q; return mq; };
    window.location = location;
    const el = (tag, cls) => { const n = new Node(doc, tag); if (cls) n.className = cls; return n; };
    const body = el('body'), main = el('main');
    body.appendChild(main);
    main.appendChild(el('div', 'intro panel'));
    let details = null;
    if (options.labels) {
        details = el('details', 'versions panel'); details.id = 'versions'; details.setAttribute('open', '');
        details.appendChild(el('summary'));
        const nav = el('nav'); details.appendChild(nav);
        options.ids.forEach((id) => { const a = el('a'); a.setAttribute('href', '#' + id); a.textContent = options.labels[id]; nav.appendChild(a); });
        main.appendChild(details);
    }
    const secs = options.ids.map((id) => { const s = el('section', 'panel'); s.id = id; main.appendChild(s); return s; });
    doc.getElementsByTagName = (t) => (t === 'main' ? [main] : body.walk(t));
    doc.getElementById = (id) => { let hit = null; const go = (n) => n.children.forEach((c) => { if (c.id === id) hit = c; go(c); }); go(body); return hit; };
    doc.createElement = (tag) => new Node(doc, tag);
    const sandbox = { document: doc, window, location, setTimeout, console };
    let thrown = null;
    try { vm.runInNewContext(SCRIPT, sandbox, { timeout: 5000 }); } catch (e) { thrown = e; }
    const r = {
        doc, main, details, secs, mq, location, thrown, window,
        visible: () => secs.filter((s) => !s.hidden).map((s) => s.id),
        tabs: () => main.children.filter((c) => c.className.split(/\s+/).includes('tabs'))[0],
        tabList: () => { const t = r.tabs(); return t ? t.children : []; },
        foot: () => main.children.filter((c) => c.className.split(/\s+/).includes('foot'))[0],
        go: (hash) => { location.hash = hash; (window.listeners.hashchange || []).forEach((f) => f({ type: 'hashchange' })); },
        phone: (on) => { mq.matches = on; mq.listeners.forEach((f) => f({ matches: on })); },
        followed: (node) => { let a = node; while (a && a.tagName !== 'A') a = a.parentNode; const href = a.getAttribute('href'); node.click(); if (href === null) return false; r.go(href); return true; }
    };
    return r;
}
const names = (n, prefix) => Array.from({ length: n }, (_, i) => (prefix || 'v1-0-') + (n - i));
const labelsFor = (ids) => { const o = {}; ids.forEach((id) => { o[id] = id.replace(/^v(\d+)-(\d+)-(\d+)$/, 'v$1.$2.$3'); }); return o; };
const cls = (n, c) => n.className.split(/\s+/).includes(c);

// ---- the generated page itself: its sections and its version list, in the page's own order ------------------------------------------------------------------------------------------------------
const pageIds = Array.from(html.matchAll(/<section id="([^"]+)" class="panel">/g)).map((m) => m[1]);
const listHtml = /<details class="versions panel" id="versions" open><summary>[^<]*<\/summary><nav aria-label="Versions">([\s\S]*?)<\/nav><\/details>/.exec(html);
const pageLabels = {};
if (listHtml) Array.from(listHtml[1].matchAll(/<a href="#([^"]+)">([^<]*)<\/a>/g)).forEach((m) => { pageLabels[m[1]] = unescape(m[2]); });
check('the page has its releases and its list of versions in the shape that the script reads', pageIds.length >= 2 && !!listHtml && JSON.stringify(Object.keys(pageLabels)) === JSON.stringify(pageIds), pageIds.length + ' sections');
const isVersion = (t) => /^v\d/.test(t);
const endText = (id) => (isVersion(pageLabels[id]) ? pageLabels[id] : 'older');

{
    const N = pageIds.length, P = pagesFor(N, SIZE);
    const r = rig({ ids: pageIds, labels: pageLabels });
    check('the script runs on the real page without an error', r.thrown === null, r.thrown && r.thrown.message);
    check('the media query is the stylesheet\'s phone query', /^\(max-width: \d+px\)$/.test(r.mq.query || '') && html.includes('@media ' + r.mq.query), r.mq.query);
    same('the first page shows its releases and no other', r.visible(), pageIds.slice(0, lastOf(0, N, SIZE) + 1));
    same('the count of tabs is the rounded quotient', r.tabList().length, P);
    r.tabList().forEach((tab, p) => {
        const first = pageIds[firstOf(p, SIZE)], last = pageIds[lastOf(p, N, SIZE)];
        same('tab ' + (p + 1) + ': the link to the first release of its page, named by its versions', [tab.getAttribute('href'), tab.children[0].tagName, tab.children[0].textContent, tab.children[1].textContent], ['#' + first, 'B', pageLabels[first], 'to ' + endText(last)]);
        same('tab ' + (p + 1) + ' says what it is to a screen reader', tab.getAttribute('aria-label'), 'Page ' + (p + 1) + ', ' + pageLabels[first] + ' to ' + endText(last));
        check('tab ' + (p + 1) + ' is the chosen one only on page 1', cls(tab, 'on') === (p === 0) && (tab.getAttribute('aria-current') === 'page') === (p === 0));
    });
    same('the tabs are above the list of versions, which is folded; the Newer / Older bar is the last thing in main', [r.main.children.indexOf(r.tabs()) + 1 === r.main.children.indexOf(r.details), r.details.getAttribute('open'), r.main.children[r.main.children.length - 1] === r.foot()], [true, null, true]);
    same('the first release of the page has the class "first" (the deep-link frame is for the others)', r.secs.map((s, k) => cls(s, 'first') === (k % SIZE === 0 && k <= firstOf(P - 1, SIZE))).every(Boolean), true);
    const prev = r.foot().children[0], where = r.foot().children[1], next = r.foot().children[2];
    same('the bars are navigations with names: the tabs have the id that the page\'s own links name, the bar below says what it is for', [r.tabs().id, r.tabs().tagName, r.tabs().getAttribute('aria-label'), r.foot().tagName, r.foot().getAttribute('aria-label')], ['pages-top', 'NAV', 'Release pages', 'NAV', 'Newer and older releases']);
    same('the two buttons say Newer and Older (with the guillemets of the page\'s text)', [r.foot().children[0].textContent, r.foot().children[2].textContent, cls(r.foot().children[0], 'btn'), cls(r.foot().children[2], 'btn')], ['\u00ab Newer', 'Older \u00bb', true, true]);
    same('on page 1 Newer is off: no link, disabled, dim', [prev.getAttribute('href'), prev.getAttribute('aria-disabled'), cls(prev, 'off'), prev.getAttribute('rel')], [null, 'true', true, 'prev']);
    same('and Older leads to the first release of page 2', [next.getAttribute('href'), next.getAttribute('aria-disabled'), cls(next, 'off'), next.getAttribute('rel')], ['#' + pageIds[firstOf(1, SIZE)], null, false, 'next']);
    same('the bar says where it is, as a status for a screen reader', [where.textContent, where.getAttribute('role')], ['Page 1 of ' + P, 'status']);
    check('the list of versions marks the releases of the page that shows', r.details.children[1].children.every((a, k) => cls(a, 'here') === (k <= lastOf(0, N, SIZE))));
    check('nothing was scrolled while the first page was set up (no address)', r.doc.scrolled.length === 0);

    // every release: its address opens its page (a dot or a dash, any case), and scrolls to it
    let bad = [];
    pageIds.forEach((id, k) => {
        const want = pageIds.slice(firstOf(pageOf(k, N, SIZE), SIZE), lastOf(pageOf(k, N, SIZE), N, SIZE) + 1);
        for (const form of ['#' + id, '#' + id.replace(/-/g, '.'), '#' + id.toUpperCase()]) {
            r.doc.scrolled.length = 0;
            r.go(form);
            if (JSON.stringify(r.visible()) !== JSON.stringify(want)) bad.push(form + ' shows ' + r.visible().length);
            const wantTarget = k === firstOf(pageOf(k, N, SIZE), SIZE) ? r.tabs() : r.secs[k];
            if (r.doc.scrolled.length !== 1 || r.doc.scrolled[0] !== wantTarget) bad.push(form + ' scrolled wrongly');
        }
    });
    same('the address of every release (#v0-12-30, #v0.12.30, #V0-12-30) opens the page that has it and scrolls to the release, or to the tabs for the first of a page', bad, []);
    same('a page\'s tab and the bar\'s Older / Newer are ordinary links that lead to the page they name', (() => {
        const out = [];
        for (let p = 0; p < P; p++) {
            r.go(r.tabList()[p].getAttribute('href'));
            if (JSON.stringify(r.visible()) !== JSON.stringify(pageIds.slice(firstOf(p, SIZE), lastOf(p, N, SIZE) + 1))) out.push('tab ' + p);
            if (r.foot().children[1].textContent !== 'Page ' + (p + 1) + ' of ' + P) out.push('status ' + p);
            const pv = r.foot().children[0], nx = r.foot().children[2];
            if ((pv.getAttribute('href') === null) !== (p === 0) || (nx.getAttribute('href') === null) !== (p === P - 1)) out.push('off ' + p);
            if (p > 0 && pv.getAttribute('href') !== '#' + pageIds[firstOf(p - 1, SIZE)]) out.push('prev ' + p);
            if (p < P - 1 && nx.getAttribute('href') !== '#' + pageIds[firstOf(p + 1, SIZE)]) out.push('next ' + p);
            if (r.tabList().filter((t) => cls(t, 'on')).length !== 1 || !cls(r.tabList()[p], 'on') || r.tabList()[p].getAttribute('aria-current') !== 'page' || r.tabList().filter((t) => t.getAttribute('aria-current')).length !== 1) out.push('on ' + p);
            if (!r.details.children[1].children.every((a, k) => cls(a, 'here') === (pageOf(k, N, SIZE) === p))) out.push('here ' + p);
        }
        return out;
    })(), []);
    // the address that names nothing, and the empty one
    r.go('#' + pageIds[firstOf(2, SIZE)]);
    const there = r.visible();
    r.go('#nothing-like-it');
    same('an address that names no release leaves the page as it is', r.visible(), there);
    r.go('#%E0%A4%A');
    same('and so does one that is not even valid text', r.visible(), there);
    r.go('#');
    same('no address (or a bare #) is the first page', r.visible(), pageIds.slice(0, lastOf(0, N, SIZE) + 1));
    // Back and Forward are the same address changes
    const trail = [1, 3, 0, 2].map((p) => '#' + pageIds[firstOf(Math.min(p, P - 1), SIZE)]);
    const seen = [];
    trail.forEach((h) => { r.go(h); seen.push(r.visible()[0]); });
    same('Back and Forward (the browser sets the address again) show the pages in the order of the trail', seen, trail.map((h) => h.slice(1)));
    // focus: after a click in the bars the focus is on the tab of the new page (the browser clears it when it follows an address); a change of the address from elsewhere takes nothing
    r.go('#');
    r.doc.activeElement = null;
    r.doc.focused.length = 0;
    r.followed(r.foot().children[2]);
    same('after a click on Older the focus is on the tab of the new page (without a scroll of its own)', [r.doc.activeElement === r.tabList()[1], JSON.stringify(r.doc.focused)], [true, JSON.stringify([{ preventScroll: true }])]);
    r.doc.activeElement = null; r.doc.focused.length = 0;
    const lastTab = Math.min(3, P - 1);
    r.followed(r.tabList()[lastTab].children[1]);                                            // (a click lands on the tab's own words: the page's link is what counts)
    same('a click on the words inside a tab counts as a click on the tab', [r.doc.activeElement === r.tabList()[lastTab], r.visible()[0]], [true, pageIds[firstOf(lastTab, SIZE)]]);
    r.doc.activeElement = null; r.doc.focused.length = 0;
    r.go('#' + pageIds[firstOf(1, SIZE)]);
    check('a change of the address that no click of the bars made does not take the focus (Back, a link elsewhere)', r.doc.activeElement === null && r.doc.focused.length === 0);
    r.tabList()[1].click();                                                                  // (a click on the tab of the page that shows: the address does not change, no event)
    r.doc.focused.length = 0;
    r.go('#' + pageIds[firstOf(2, SIZE)]);
    check('and a click that changed no address is not remembered for the next change', r.doc.activeElement === null && r.doc.focused.length === 0);
    // a phone: five to a page, the same release stays in sight
    r.go('#' + pageIds[12]);
    r.phone(true);
    const Pp = pagesFor(N, SIZE_PHONE);
    same('a phone shows five to a page', [r.tabList().length, r.visible().length], [Pp, lastOf(pageOf(10, N, SIZE_PHONE), N, SIZE_PHONE) - firstOf(pageOf(10, N, SIZE_PHONE), SIZE_PHONE) + 1]);
    same('and keeps the release that was at the top of the page in sight', r.visible()[0], pageIds[10]);
    r.go('#' + pageIds[N - 1]);
    same('on a phone the last release is on the last page, which has the rest', [r.visible().length >= SIZE_PHONE / 2, r.visible()[r.visible().length - 1], r.foot().children[1].textContent], [true, pageIds[N - 1], 'Page ' + Pp + ' of ' + Pp]);
    r.details.setAttribute('open', '');
    r.phone(false); r.phone(true);
    check('a list of versions that the visitor opened stays open when the screen changes size', r.details.getAttribute('open') === '');
    r.go('#' + pageIds[10]);
    const topOfPhone = r.visible()[0];
    r.phone(false);
    same('the desktop again: ten to a page, and the release that was at the top of the phone\'s page is on it', [r.tabList().length, r.visible().includes(topOfPhone)], [P, true]);
    same('(and exactly one pair of bars is in the page after all those changes)', [r.main.children.filter((c) => cls(c, 'tabs')).length, r.main.children.filter((c) => cls(c, 'foot')).length], [1, 1]);
}

// ---- the rule for the count of pages, on made-up lists ------------------------------------------------------------------------------------------------------------------------------------------
for (const n of [1, 2, 4, 9, 14, 15, 16, 24, 25, 26, 34, 35, 55, 100]) {
    const ids = names(n), r = rig({ ids, labels: labelsFor(ids) });
    const P = pagesFor(n, SIZE);
    check('n=' + n + ': no error', r.thrown === null, r.thrown && r.thrown.message);
    if (P < 2) {
        same('n=' + n + ': one page is no pages: every release shows, no bars, the list of versions stays open', [r.visible().length, r.tabs() === undefined, r.foot() === undefined, r.details.getAttribute('open')], [n, true, true, '']);
    } else {
        same('n=' + n + ': ' + P + ' pages, the first with ' + SIZE, [r.tabList().length, r.visible().length], [P, SIZE]);
        r.go('#' + ids[n - 1]);
        const lastSize = r.visible().length;
        same('n=' + n + ': the last page has the rest and is never shorter than half a page', [lastSize, lastSize >= SIZE / 2], [n - (P - 1) * SIZE, true]);
    }
}
{
    const ids = names(0), r = rig({ ids, labels: labelsFor(ids) });
    check('a page with no release at all is left alone', r.thrown === null && r.tabs() === undefined && r.foot() === undefined, r.thrown && r.thrown.message);
}
// a phone's size on its own lists (a list of 12 is one page on a desktop and three on a phone)
{
    const ids = names(12), r = rig({ ids, labels: labelsFor(ids) });
    same('twelve releases: one page on a desktop', [r.visible().length, r.tabs() === undefined], [12, true]);
    r.phone(true);
    same('... two pages on a phone (5 and 7), the list folded', [r.tabList().length, r.visible().length, r.details.getAttribute('open')], [pagesFor(12, SIZE_PHONE), 5, null]);
    r.phone(false);
    same('... and one page again on a desktop: no bars, every release shows', [r.visible().length, r.tabs() === undefined, r.foot() === undefined], [12, true, true]);
    const p = rig({ ids, labels: labelsFor(ids), phone: true });
    same('a page that opens on a phone is on five to a page at once', [p.tabList().length, p.visible().length], [2, 5]);
}
// older browsers: MediaQueryList.addListener only
{
    const ids = names(12), r = rig({ ids, labels: labelsFor(ids), legacy: true });
    r.phone(true);
    same('a browser that has only addListener is told of the resize too', r.tabList().length, 2);
}
// no list of versions: the pages work, the labels are the ids; labels that are not versions
{
    const ids = names(30), r = rig({ ids });
    check('no list of versions: no error, the tabs go above the first release', r.thrown === null && r.main.children.indexOf(r.tabs()) === r.main.children.indexOf(r.secs[0]) - 1 && r.tabList()[0].children[0].textContent === ids[0], r.thrown && r.thrown.message);
    const labels = labelsFor(ids);
    labels[ids[0]] = 'Next'; labels[ids[29]] = 'Older versions';
    const q = rig({ ids, labels });
    same('a first page that starts with "Next" says so; a last page that ends with "Older versions" says "older"', [q.tabList()[0].children[0].textContent, q.tabList()[0].children[1].textContent, q.tabList()[2].children[1].textContent], ['Next', 'to ' + labels[ids[9]], 'to older']);
}
// a deep link when the page opens: the right page at once, scrolled to the release
{
    const ids = names(52), r = rig({ ids, labels: labelsFor(ids), hash: '#' + ids[23] });
    same('a page that opens with the address of a release shows its page and scrolls to the release', [r.visible()[0], r.visible().includes(ids[23]), r.doc.scrolled.length, r.doc.scrolled[0] === r.secs[23]], [ids[20], true, 1, true]);
    const f = rig({ ids, labels: labelsFor(ids), hash: '#' + ids[20] });
    same('... and for the first release of a page, to the tabs', [f.visible()[0], f.doc.scrolled[0] === f.tabs()], [ids[20], true]);
    const u = rig({ ids, labels: labelsFor(ids), hash: '#elsewhere' });
    same('an address that is no release is the first page, and nothing scrolls', [u.visible()[0], u.doc.scrolled.length], [ids[0], 0]);
}
check('no text became markup anywhere (a fake that refuses innerHTML ran every scenario)', true);

console.log('web changelog pages: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
