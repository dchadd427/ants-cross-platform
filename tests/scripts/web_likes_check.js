// Runs the OWN code of the plays and hearts of the match pages (web/replay_likes.js) on a STRICT fake of the browser's DOM (an element with no innerHTML), a fake door (GET /counts/mine, POST /counts/like and
// /counts/unlike, as docs/REPLAY_COUNTS.md says) and a fake clock: the counts of a record (0, 1, 1,234, "1 play", whatever is not a whole number is 0), the cards of the list and the result of the match page (a
// heart that is a button, a plain count for a recording that is gone, the dim zeros, the count that the sort brightens), the one look at the door (the ids of the cards, at most 50, none for a plain count; a heart is
// lit for a replay that this visitor likes; likes that are off make plain counts), a tap (the heart changes at once, the token goes in the header, the answer's count is the one shown, one call at a time for a
// replay, the heart goes back with one plain line for every way a call can fail, a token that is refused is asked for once more and the call is made once more), and a visitor who is not logged in (the card
// with the way in, and no call at all). tests/scripts/test_web_likes.py runs it with node (the quick tier) and again on changed copies, which it must refuse.
// usage: node web_likes_check.js web/replay_likes.js web/history_page.js          (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');
const [likesPath, historyPath] = process.argv.slice(2).map((p) => path.resolve(p));
if (!historyPath) { console.log('usage: web_likes_check.js replay_likes.js history_page.js'); process.exit(2); }
const L = require(likesPath);
const H = require(historyPath);

let checks = 0, failures = 0;
function expect(label, got, want) {
    checks++;
    const a = JSON.stringify(got), b = JSON.stringify(want);
    if (a !== b) { failures++; console.log('FAIL ' + label + ': got ' + a + ', wanted ' + b); }
}
process.on('unhandledRejection', (e) => { failures++; process.exitCode = 1; console.log('FAIL a promise was rejected and nobody caught it: ' + e); });
const settle = () => new Promise((resolve) => setImmediate(resolve));

// ---- a strict fake DOM: elements with text, attributes, a place, children and listeners; no innerHTML ----
class Node {
    constructor(tag, text) { this.tag = tag; this.children = []; this.parentNode = null; this.className = ''; this.attrs = {}; this.listeners = {}; this.id = ''; this._text = text; this.rect = { left: 400, top: 300, width: 60, height: 30 }; this.styleProps = {}; this.style = { setProperty: (k, v) => { this.styleProps[k] = v; } }; }
    set innerHTML(v) { throw new Error('innerHTML is never used'); }
    detach(c) { if (c.parentNode) c.parentNode.removeChild(c); }
    appendChild(c) { this.detach(c); c.parentNode = this; this.children.push(c); return c; }
    insertBefore(c, ref) { this.detach(c); const i = ref ? this.children.indexOf(ref) : -1; this.children.splice(i < 0 ? this.children.length : i, 0, c); c.parentNode = this; return c; }
    removeChild(c) { const i = this.children.indexOf(c); if (i >= 0) this.children.splice(i, 1); c.parentNode = null; return c; }
    replaceChild(n, old) { this.detach(n); const i = this.children.indexOf(old); this.children.splice(i, 1, n); n.parentNode = this; old.parentNode = null; return old; }
    get firstChild() { return this.children[0] || null; }
    get nextSibling() { const p = this.parentNode; return p ? p.children[p.children.indexOf(this) + 1] || null : null; }
    setAttribute(k, v) { this.attrs[k] = String(v); if (k === 'id') this.id = String(v); }
    getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
    addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); }
    click() { (this.listeners.click || []).forEach((fn) => fn({ target: this })); }
    focus() { this.doc.activeElement = this; }
    getBoundingClientRect() { const r = this.rect; return { left: r.left, top: r.top, width: r.width, height: r.height, right: r.left + r.width, bottom: r.top + r.height }; }
    set textContent(t) { this.children.forEach((c) => { c.parentNode = null; }); this.children = []; this._text = String(t); }
    get textContent() { return (this._text === undefined ? '' : this._text) + this.children.map((c) => c.textContent).join(''); }
    has(cls) { return this.className.split(' ').includes(cls); }
    is(sel) { return sel[0] === '.' ? this.has(sel.slice(1)) : sel[0] === '#' ? this.id === sel.slice(1) : this.tag === sel; }
    all(test, out = []) { this.children.forEach((c) => { if (test(c)) out.push(c); c.all(test, out); }); return out; }
    byClass(cls) { return this.all((n) => n.has(cls)); }
    querySelector(sel) { return this.all((n) => n.is(sel))[0] || null; }
}
class Doc {
    constructor() { this.body = this.make('body'); this.listeners = {}; this.activeElement = null; }
    make(tag) { const n = new Node(tag); n.doc = this; return n; }
    createElement(tag) { return this.make(tag); }
    createElementNS(ns, tag) { return this.make(tag); }
    createTextNode(t) { const n = this.make('#text'); n._text = t; return n; }
    addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); }
    fire(type, ev) { (this.listeners[type] || []).forEach((fn) => fn(ev)); }
}

// ---- a page: the DOM, the window with its timers, the door ----
const IDS = Array.from({ length: 80 }, (_, i) => 'ants-TREASURE-20261124-' + String(100000 + i) + 'Z');
const TOKEN = 'a1b2c3d4e5f60718293a4b5c6d7e8f90', TOKEN2 = '0f9e8d7c6b5a49382716f5e4d3c2b1a0';
function setup(o) {
    o = o || {};
    const doc = new Doc(), timers = [], door = { signed: o.signed !== false, liked: new Set(o.liked || []), csrf: TOKEN, counts: {}, log: [], queue: [], held: [], offline: !!o.offline, off: !!o.off, offWord: o.offWord === undefined ? 'likes_off' : o.offWord };
    const win = { innerWidth: 1000, scrollX: 0, scrollY: 0, setTimeout: (fn, ms) => { timers.push({ fn, ms, on: true }); return timers.length; }, clearTimeout: (id) => { if (timers[id - 1]) timers[id - 1].on = false; } };
    const reply = (status, body) => ({ status, ok: status >= 200 && status < 300, json: () => (body === undefined ? Promise.reject(new Error('no json')) : Promise.resolve(body)) });
    function fetcher(url, init) {
        init = init || {};
        const call = { url, method: init.method || 'GET', credentials: init.credentials, headers: init.headers, hasBody: 'body' in init };
        door.log.push(call);
        if (door.offline) return Promise.reject(new Error('offline'));
        let out = null;
        if (door.queue.length) { const q = door.queue.shift(); if (q === 'throw') throw new Error('fetch is not there'); if (q === 'reject') return Promise.reject(new Error('offline')); out = q.hold ? { hold: true, answer: reply(q.status, q.body) } : reply(q.status, q.body); }
        else {
            const u = new URL(url, 'http://x'), m = /^\/counts\/(like|unlike)\/(.+)$/.exec(u.pathname);
            if (u.pathname === '/counts/mine') {
                const ids = (u.searchParams.get('ids') || '').split(',').filter(Boolean);
                out = door.off ? reply(503, door.offWord === null ? undefined : { error: door.offWord }) : door.signed ? reply(200, { signed_in: true, liked: ids.filter((i) => door.liked.has(i)), csrf: door.csrf }) : reply(200, { signed_in: false, liked: [] });
            } else if (m) {
                if (door.off) out = reply(503, { error: 'likes_off' });
                else if (!door.signed) out = reply(401, { error: 'not_signed_in' });
                else if (!init.headers || init.headers['X-Ants-Csrf'] !== door.csrf) out = reply(403, { error: 'csrf' });
                else {
                    door.counts[m[2]] = door.counts[m[2]] === undefined ? 7 : door.counts[m[2]];
                    if (m[1] === 'like' && !door.liked.has(m[2])) { door.liked.add(m[2]); door.counts[m[2]]++; }
                    if (m[1] === 'unlike' && door.liked.has(m[2])) { door.liked.delete(m[2]); door.counts[m[2]]--; }
                    out = reply(200, { liked: door.liked.has(m[2]), likes: door.counts[m[2]] });
                }
            } else out = reply(404, { error: 'not found' });
        }
        if (out && out.hold) return new Promise((resolve) => { door.held.push(() => resolve(out.answer)); });
        return Promise.resolve(out);
    }
    const likes = L.create({ document: doc, win, fetch: o.noFetch ? null : fetcher, numberText: H.numberText, login: (id) => '/hub/login?next=match&id=' + id });
    return { doc, win, door, likes, timers, say: (status, body, hold) => door.queue.push({ status, body, hold }), release: () => { const f = door.held.shift(); if (f) f(); } };
}
function card() {
    const li = new Node('li'), info = new Node('div'), act = new Node('div'), rec = new Node('span', 'Replay'), go = new Node('a', 'Stats & replay');
    li.className = 'm'; info.className = 'info'; act.className = 'act'; rec.className = 'rec'; go.className = 'btn sm go';
    info.appendChild(new Node('h3', 'Treasure')); act.appendChild(rec); act.appendChild(go); li.appendChild(info); li.appendChild(act);
    return li;
}
function hero() {
    const s = new Node('section'), meta = new Node('p', 'Today, 5:45 PM · 12:00 long'), acts = new Node('div'), show = new Node('button', '▶ Show replay'), dl = new Node('a', 'Download file'), cp = new Node('button', 'Copy link');
    s.className = 'panel hero'; meta.className = 'meta'; acts.className = 'acts'; show.id = 'show'; show.className = 'btn';
    acts.appendChild(show); acts.appendChild(dl); acts.appendChild(cp); s.appendChild(meta); s.appendChild(acts);
    return s;
}
const heartsOf = (n) => n.all((x) => x.has('lk'));
const lit = (n) => heartsOf(n).map((h) => h.has('on'));
const numbers = (n) => heartsOf(n).map((h) => h.byClass('n')[0].textContent);
const rec = (i, plays, likes) => ({ id: IDS[i], plays, likes });
const gets = (s) => s.door.log.filter((c) => c.method === 'GET').map((c) => c.url);
const posts = (s) => s.door.log.filter((c) => c.method === 'POST');
const cardsOf = (s) => s.doc.body.byClass('lkask');

(async () => {
    // ---- the counts of a record ----
    expect('a count is a whole number from 0 up; anything else is 0', [0, 1, 1234, 1e9, '5', null, undefined, -1, 1.5, NaN, Infinity, 1e10, [], {}, true].map(L.countOf), [0, 1, 1234, 1e9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    expect('the words for a failed call', [429, 409, 503, 404, 0, 500, 400, 403].map(L.lineOf), ['Slow down a little.', 'You have liked as many replays as can be kept.', "Liking isn't available right now.", "This replay can't be liked any more.", "Couldn't reach the server.", L.SAY.other, L.SAY.other, L.SAY.other]);

    // ---- a card of the list ----
    {
        const s = setup(), li = card();
        s.likes.card(li, rec(0, 12, 7), {});
        const act = li.querySelector('.act'), info = li.querySelector('.info');
        expect('the plays and the heart are the first thing of the card\'s actions (left of "Replay") and the last of its info (a phone)', [act.children[0].className, info.children[info.children.length - 1].className, act.children.length, info.children.length], ['pl', 'pl', 3, 2]);
        const g = act.children[0];
        expect('a group is the number of plays and a heart that is a button with its count', [g.children.map((c) => c.tag), g.children[0].className, g.children[0].textContent, g.children[1].className, g.children[1].textContent, g.children[1].attrs.type, g.children[1].attrs['aria-pressed']], [['span', 'button'], 'pv', '12', 'lk', '7', undefined, 'false']);
        expect('the heart is a button of type button', [heartsOf(li).map((h) => h.tag), heartsOf(li).map((h) => h.type)], [['button', 'button'], ['button', 'button']]);
        expect('the plays say so to a reader of the title, with "1 play" for one', [g.children[0].title, (() => { const t = setup(), c = card(); t.likes.card(c, rec(1, 1, 0), {}); return c.byClass('pv')[0].title; })()], ['12 plays of the replay', '1 play of the replay']);
        expect('both groups say the same, and the heart has a name for a reader', [info.children[info.children.length - 1].textContent, heartsOf(li).map((h) => h.attrs['aria-label'])], ['127', ['Like this replay: 7 likes', 'Like this replay: 7 likes']]);
    }
    {
        const s = setup(), li = card();
        s.likes.card(li, rec(0, 0, 0), {});
        expect('a zero is dim: the plays and the count of the heart', [li.byClass('pv').map((x) => x.className), heartsOf(li).map((h) => h.className), numbers(li)], [['pv zero', 'pv zero'], ['lk zero', 'lk zero'], ['0', '0']]);
        const t = setup(), c = card();
        t.likes.card(c, rec(0, 1234, 1234), {});
        expect('a number is written with its comma, and one play is one play', [c.byClass('pv').map((x) => x.textContent), numbers(c)], [['1,234', '1,234'], ['1,234', '1,234']]);
        const u = setup(), d = card();
        u.likes.card(d, { id: IDS[0], plays: '<img src=x onerror=1>', likes: { x: 1 } }, {});
        expect('what a record says that is not a whole number is 0 (and nothing is markup: the DOM refuses innerHTML)', [d.byClass('pv').map((x) => x.textContent), numbers(d)], [['0', '0'], ['0', '0']]);
    }
    for (const [key, want] of [['plays', [['pv key', 'pv key'], ['lk', 'lk']]], ['likes', [['pv', 'pv'], ['lk key', 'lk key']]], ['', [['pv', 'pv'], ['lk', 'lk']]]]) {
        const s = setup(), li = card();
        s.likes.card(li, rec(0, 12, 7), { key });
        expect('sorted by "' + key + '" the small count of that sort is the bright one', [li.byClass('pv').map((x) => x.className), heartsOf(li).map((h) => h.className)], want);
    }
    {
        const s = setup({ liked: [IDS[0]] }), li = card();
        s.likes.card(li, rec(0, 12, 7), { plain: true });
        expect('a recording that is gone or older keeps its numbers; its heart is a plain count: a span, no button', [heartsOf(li).map((h) => h.tag), heartsOf(li).map((h) => h.className), numbers(li), heartsOf(li).map((h) => h.getAttribute('aria-pressed')), heartsOf(li).map((h) => (h.listeners.click || []).length)], [['span', 'span'], ['lk flat', 'lk flat'], ['7', '7'], [null, null], [0, 0]]);
        heartsOf(li).forEach((h) => h.click());
        await s.likes.look();
        expect('... a plain count is never asked about, never lit and never taps', [s.door.log.length, lit(li)], [0, [false, false]]);
    }

    // ---- the result of the match page ----
    {
        const s = setup(), h = hero();
        s.likes.hero(h, rec(0, 12, 7), {});
        const meta = h.querySelector('.meta'), acts = h.querySelector('.acts');
        expect('the plays end the line under the title: a dot and "12 plays"', [meta.children.map((c) => c.className), meta.children[0].textContent, meta.children[1].textContent], [['pvdot', 'pv'], ' · ', '12 plays']);
        expect('the heart is a button beside Show replay (the next thing after it) and has an id', [acts.children.map((c) => c.className), acts.children[1].id, acts.children[1].tag, acts.children[1].textContent], [['btn', 'lk', '', ''], 'like', 'button', '7']);
        expect('the zero, the one and the thousands of the plays', [[0, 1, 2, 1234].map((n) => { const t = setup(), x = hero(); t.likes.hero(x, rec(0, n, 0), {}); return x.byClass('pv')[0].textContent + '|' + x.byClass('pv')[0].className; })], [['0 plays|pv zero', '1 play|pv', '2 plays|pv', '1,234 plays|pv']]);
        const t = setup(), x = hero();
        x.querySelector('.acts').removeChild(x.querySelector('#show'));
        t.likes.hero(x, rec(0, 3, 1), { plain: true });
        expect('a result with no Show replay (older or gone): the heart is a plain count and comes first', [x.querySelector('.acts').children[0].tag, x.querySelector('.acts').children[0].className], ['span', 'lk flat']);
        const u = setup(), y = hero();
        u.likes.hero(y, rec(0, 5, 2), { key: 'plays' });
        expect('the match page never brightens the plays', [y.byClass('pv')[0].className, y.byClass('lk')[0].className], ['pv', 'lk']);
    }

    // ---- the one look at the door ----
    {
        const s = setup({ liked: [IDS[1], IDS[7]] });
        const lis = IDS.slice(0, 4).map((id, i) => { const li = card(); s.likes.card(li, rec(i, 1, 1), { plain: i === 2 }); return li; });
        expect('before the door has answered every heart is unlit and nothing was asked yet', [lis.map(lit), s.door.log.length], [[[false, false], [false, false], [false, false], [false, false]], 0]);
        const done = s.likes.look();
        expect('one look: GET /counts/mine with the ids of the cards that can be liked (not the plain one), the cookies of the site and no token', [s.door.log.length, s.door.log[0].method, s.door.log[0].url, s.door.log[0].credentials, 'headers' in s.door.log[0] && s.door.log[0].headers !== undefined], [1, 'GET', '/counts/mine?ids=' + [IDS[0], IDS[1], IDS[3]].join(','), 'same-origin', false]);
        await done;
        expect('a replay that this visitor likes is lit on both hearts of its card, the others are not', lis.map(lit), [[false, false], [true, true], [false, false], [false, false]]);
        expect('... a lit heart says so', [lis[1].byClass('lk').map((h) => h.attrs['aria-pressed']), lis[1].byClass('lk').map((h) => h.title), lis[0].byClass('lk').map((h) => h.title)], [['true', 'true'], ['You like this replay', 'You like this replay'], ['Like this replay', 'Like this replay']]);
    }
    {
        const s = setup({ liked: [IDS[0]], signed: false });
        const li = card(); s.likes.card(li, rec(0, 1, 1), {});
        await s.likes.look();
        expect('a visitor who is not logged in has no heart lit', lit(li), [false, false]);
    }
    {
        const s = setup();
        const lis = IDS.slice(0, 60).map((id, i) => { const li = card(); s.likes.card(li, rec(i, 1, 1), {}); return li; });
        await s.likes.look();
        const ids = new URL(s.door.log[0].url, 'http://x').searchParams.get('ids').split(',');
        expect('the look asks for 50 ids at the most, the first ones, each once', [ids.length, ids[0], ids[49]], [50, IDS[0], IDS[49]]);
        const t = setup();
        t.likes.card(card(), rec(0, 1, 1), {}); t.likes.card(card(), rec(0, 1, 1), {});
        await t.likes.look();
        expect('... and a replay twice on a page is asked for once', new URL(t.door.log[0].url, 'http://x').searchParams.get('ids'), IDS[0]);
        const u = setup();
        expect('a page with nothing to like asks nothing', [await u.likes.look(), u.door.log.length], [false, 0]);
    }
    {
        const s = setup({ off: true });
        const li = card(); s.likes.card(li, rec(0, 4, 9), {});
        const where = heartsOf(li).map((h) => [h.parentNode.className, h.parentNode.children.indexOf(h)]);
        await s.likes.look();
        expect('likes that are off make every heart a plain count, in the same place, with its number', [heartsOf(li).map((h) => h.tag), heartsOf(li).map((h) => [h.parentNode.className, h.parentNode.children.indexOf(h)]), numbers(li), heartsOf(li).map((h) => h.className)], [['span', 'span'], where, ['9', '9'], ['lk flat', 'lk flat']]);
        heartsOf(li).forEach((h) => h.click());
        expect('... and a tap on one asks nothing more', s.door.log.length, 1);
    }
    {
        const c = setup({ off: true, offWord: 'counts_off' });
        const li = card(); c.likes.card(li, rec(0, 4, 9), {});
        await c.likes.look();
        expect('a counts door that is off makes the hearts plain counts as well', heartsOf(li).map((h) => h.className), ['lk flat', 'lk flat']);
    }
    for (const word of ['busy', 'unavailable', null, 'something else']) {
        const b = setup({ off: true, offWord: word });
        const li = card(); b.likes.card(li, rec(0, 4, 9), {});
        const first = await b.likes.look();
        expect('a 503 that is not "likes are off" (' + word + ') is a moment: the look failed and the hearts stay what they were', [first, heartsOf(li).map((h) => h.tag), heartsOf(li).map((h) => h.className.indexOf('flat') !== -1)], [false, ['button', 'button'], [false, false]]);
        b.door.off = false;
        heartsOf(li)[0].click();
        await Promise.resolve(); await Promise.resolve(); await Promise.resolve(); await Promise.resolve();
        expect('... and the next tap asks the door again (the look, then the like)', [b.door.log.length >= 2, b.door.log[1] && b.door.log[1].url.indexOf('/counts/mine') === 0], [true, true]);
    }
    for (const [what, set] of [['an answer that is a 500', (s) => s.say(500, { error: 'x' })], ['an answer that is not JSON', (s) => s.say(200, undefined)], ['an answer with no word of who is logged in', (s) => s.say(200, { liked: [] })], ['a logged-in answer with no token', (s) => s.say(200, { signed_in: true, liked: [] })],
        ['a logged-in answer with a token that is not 32 hex characters', (s) => s.say(200, { signed_in: true, liked: [], csrf: '<b>' })], ['a logged-in answer whose list is not a list', (s) => s.say(200, { signed_in: true, liked: 'x', csrf: TOKEN })], ['a network that is down', (s) => { s.door.offline = true; }], ['no fetch at all', null]]) {
        const s = setup(set ? {} : { noFetch: true }); if (set) set(s);
        const li = card(); s.likes.card(li, rec(0, 1, 1), {});
        let threw = null;
        try { await s.likes.look(); } catch (e) { threw = String(e); }
        expect(what + ' is no answer: nothing throws, the hearts stay buttons, unlit', [threw, heartsOf(li).map((h) => h.tag), lit(li)], [null, ['button', 'button'], [false, false]]);
        if (set && !s.door.offline) {
            heartsOf(li)[0].click(); await settle();
            expect('... a tap then asks again (the page does not know who is logged in), and the like is made', [gets(s).length, posts(s).length, lit(li), cardsOf(s).length], [2, 1, [true, true], 0]);
        }
    }

    // ---- a tap ----
    {
        const s = setup({ liked: [] });
        const li = card(); s.likes.card(li, rec(0, 12, 7), {});
        await s.likes.look();
        s.say(200, { liked: true, likes: 9 }, true);
        heartsOf(li)[0].click();
        expect('the heart changes AT ONCE on a tap, before the door has answered: lit and one more', [lit(li), numbers(li), posts(s).length], [[true, true], ['8', '8'], 1]);
        expect('... the call: POST /counts/like/<id>, the cookies of the site, the token of the look in X-Ants-Csrf, no body', [posts(s)[0].url, posts(s)[0].method, posts(s)[0].credentials, posts(s)[0].headers, posts(s)[0].hasBody], ['/counts/like/' + IDS[0], 'POST', 'same-origin', { 'X-Ants-Csrf': TOKEN }, false]);
        heartsOf(li)[0].click(); heartsOf(li)[1].click();
        expect('... one call at a time for a replay, from either of its hearts', posts(s).length, 1);
        s.release(); await settle();
        expect('the count that the door answers is the one shown (9, not 8), on both hearts, and the heart is lit', [lit(li), numbers(li), heartsOf(li).map((h) => h.attrs['aria-pressed'])], [[true, true], ['9', '9'], ['true', 'true']]);
        s.say(200, { liked: false, likes: 6 }, true);
        heartsOf(li)[1].click();
        expect('a tap on a lit heart takes the like back at once: not lit and one less', [lit(li), numbers(li)], [[false, false], ['8', '8']]);
        s.release(); await settle();
        expect('... POST /counts/unlike/<id> and the count of the answer', [posts(s)[1].url, posts(s)[1].headers, numbers(li), lit(li)], ['/counts/unlike/' + IDS[0], { 'X-Ants-Csrf': TOKEN }, ['6', '6'], [false, false]]);
        heartsOf(li)[0].click(); await settle();
        expect('a heart can be tapped again when the call is over', [posts(s).length, lit(li)], [3, [true, true]]);
    }
    {
        const s = setup({ liked: [IDS[0]] });
        const li = card(); s.likes.card(li, rec(0, 12, 7), {});
        await s.likes.look();
        expect('a heart that starts lit is lit', lit(li), [true, true]);
        heartsOf(li)[0].click(); await settle();
        expect('... and a tap unlikes it (the door has it as liked), with the count of its answer', [posts(s).map((p) => p.url), lit(li), numbers(li)], [['/counts/unlike/' + IDS[0]], [false, false], ['6', '6']]);
    }
    {
        const s = setup({ liked: [] });
        const li = card(); s.likes.card(li, rec(0, 0, 0), {});
        await s.likes.look();
        s.say(200, { liked: false, likes: 0 });
        heartsOf(li)[0].click(); await settle();
        expect('the count never goes below 0 when the like is taken back with none left', [numbers(li)], [['0', '0']]);
        const t = setup({ liked: [IDS[0]] }), c = card(); t.likes.card(c, rec(0, 0, 0), {});
        await t.likes.look();
        t.say(200, { liked: false, likes: 0 }, true);
        heartsOf(c)[0].click();
        expect('... not even for a moment', numbers(c), ['0', '0']);
        t.release(); await settle();
    }
    for (const [what, status, line] of [['too many calls (429)', 429, 'Slow down a little.'], ['as many likes as can be kept (409)', 409, 'You have liked as many replays as can be kept.'], ['a door that is shut (503)', 503, "Liking isn't available right now."],
        ['a replay that is not there (404)', 404, "This replay can't be liked any more."], ['a server that fails (500)', 500, L.SAY.other], ['a call that is not understood (400)', 400, L.SAY.other]]) {
        const s = setup({ liked: [] });
        const li = card(); s.likes.card(li, rec(0, 12, 7), {});
        await s.likes.look();
        s.say(status, { error: 'x' });
        heartsOf(li)[0].click(); await settle();
        const note = cardsOf(s)[0];
        expect(what + ': the heart goes back and one plain line says why', [lit(li), numbers(li), cardsOf(s).length, note && note.textContent, note && note.getAttribute('role'), note && note.byClass('btn').length], [[false, false], ['7', '7'], 1, line, 'alert', 0]);
        expect('... the heart can be tapped again at once', (() => { s.say(200, { liked: true, likes: 8 }); heartsOf(li)[0].click(); return lit(li); })(), [true, true]);
        await settle();
    }
    {
        const s = setup({ liked: [] });
        const li = card(); s.likes.card(li, rec(0, 12, 7), {});
        await s.likes.look();
        s.door.offline = true;
        heartsOf(li)[1].click(); await settle();
        expect('a network that is down: the heart goes back and "Couldn\'t reach the server."', [lit(li), numbers(li), cardsOf(s).map((c) => c.textContent)], [[false, false], ['7', '7'], ["Couldn't reach the server."]]);
        const t = setup({ liked: [] }), c = card(); t.likes.card(c, rec(0, 12, 7), {});
        await t.likes.look();
        t.say(200, null); t.door.queue.unshift('throw');
        heartsOf(c)[0].click(); await settle();
        expect('a fetch that throws is the same', [lit(c), cardsOf(t).map((x) => x.textContent)], [[false, false], ["Couldn't reach the server."]]);
        expect('the line goes by itself after a few seconds', (() => { const x = s.timers.filter((tm) => tm.on); x.forEach((tm) => tm.fn()); return [cardsOf(s).length, x.length]; })(), [0, 1]);
    }
    {
        const s = setup({ liked: [] });
        const li = card(); s.likes.card(li, rec(0, 12, 7), {});
        await s.likes.look();
        s.say(200, { liked: true }); heartsOf(li)[0].click(); await settle();
        expect('an answer that gives no count keeps the count of the tap, and the heart as the answer says', [numbers(li), lit(li)], [['8', '8'], [true, true]]);
        const t = setup({ liked: [] }), c = card(); t.likes.card(c, rec(0, 12, 7), {});
        await t.likes.look();
        t.say(200, { liked: true, likes: '<b>9</b>' }); heartsOf(c)[0].click(); await settle();
        expect('... and an answer whose count is text is no count', numbers(c), ['8', '8']);
    }
    // the token that the door refuses
    {
        const s = setup({ liked: [] });
        const li = card(); s.likes.card(li, rec(0, 12, 7), {});
        await s.likes.look();
        s.door.csrf = TOKEN2;                                        // (the person's token changed: the one the page holds is out of date)
        heartsOf(li)[0].click(); await settle();
        expect('a token that the door refuses (403): one more look at the door, and the call made once more with the new token', [s.door.log.map((c) => c.method + ' ' + c.url.replace(/ants-[^/,]*Z/g, 'ID')), posts(s)[1].headers], [['GET /counts/mine?ids=ID', 'POST /counts/like/ID', 'GET /counts/mine?ids=ID', 'POST /counts/like/ID'], { 'X-Ants-Csrf': TOKEN2 }]);
        expect('... and the like is there', [lit(li), numbers(li), cardsOf(s).length], [[true, true], ['8', '8'], 0]);
        const t = setup({ liked: [] }), c = card(); t.likes.card(c, rec(0, 12, 7), {});
        await t.likes.look();
        t.say(403, { error: 'csrf' }); t.say(200, { signed_in: true, liked: [], csrf: TOKEN2 }); t.say(403, { error: 'csrf' });
        heartsOf(c)[0].click(); await settle();
        expect('a token that is refused twice: the heart goes back, one plain line, and no third call', [posts(t).length, gets(t).length, lit(c), numbers(c), cardsOf(t).map((x) => x.textContent)], [2, 2, [false, false], ['7', '7'], [L.SAY.other]]);
        const u = setup({ liked: [] }), d = card(); u.likes.card(d, rec(0, 12, 7), {});
        await u.likes.look();
        u.say(403, { error: 'csrf' }); u.say(200, { signed_in: false, liked: [] });
        heartsOf(d)[0].click(); await settle();
        expect('a token that is refused because the visitor has logged out: the heart goes back and the card with the way in', [posts(u).length, lit(d), numbers(d), cardsOf(u).map((x) => x.byClass('btn').length)], [1, [false, false], ['7', '7'], [1]]);
        const v = setup({ liked: [] }), e = card(); v.likes.card(e, rec(0, 12, 7), {});
        await v.likes.look();
        v.say(403, { error: 'csrf' }); v.door.queue.push('reject');
        heartsOf(e)[0].click(); await settle();
        expect('a token that is refused and a look that cannot be made: the heart goes back, "Couldn\'t reach the server."', [posts(v).length, lit(e), cardsOf(v).map((x) => x.textContent)], [1, [false, false], ["Couldn't reach the server."]]);
    }
    {
        const s = setup({ liked: [] });
        const li = card(); s.likes.card(li, rec(0, 12, 7), {});
        await s.likes.look();
        s.say(401, { error: 'not_signed_in' });
        heartsOf(li)[0].click(); await settle();
        expect('a call that finds the visitor logged out (401): the heart goes back and the card with the way in', [lit(li), numbers(li), cardsOf(s).map((x) => x.byClass('btn').length)], [[false, false], ['7', '7'], [1]]);
        const before = s.door.log.length;
        heartsOf(li)[1].click();
        expect('... and the next tap asks nothing: the page knows now', [s.door.log.length, cardsOf(s).length], [before, 1]);
    }
    {
        const s = setup({ liked: [] });
        const li = card(); s.likes.card(li, rec(0, 12, 7), {});
        // the page has not heard from the door when the visitor taps: it asks first, then the tap goes on
        const done = s.likes.look();
        heartsOf(li)[0].click();
        expect('a tap before the door has answered waits for the look, and changes nothing yet', [lit(li), posts(s).length], [[false, false], 0]);
        await done; await settle();
        expect('... then the like is made', [lit(li), numbers(li), posts(s).length, gets(s).length], [[true, true], ['8', '8'], 1, 1]);
        const t = setup({ liked: [], offline: true });
        const c = card(); t.likes.card(c, rec(0, 12, 7), {});
        await t.likes.look();
        heartsOf(c)[0].click(); await settle();
        expect('a page that never heard from the door, with the network down: a tap asks once more and says that it cannot reach the server', [gets(t).length, posts(t).length, cardsOf(t).map((x) => x.textContent)], [2, 0, ["Couldn't reach the server."]]);
        t.door.offline = false;
        heartsOf(c)[0].click(); await settle();
        expect('... and when the network is back the next tap works', [gets(t).length, posts(t).length, lit(c)], [3, 1, [true, true]]);
    }
    {
        // a tap waits for a look that is still out; and an older look that is answered after a tap does not undo it
        const s = setup({ liked: [] });
        const li = card(); s.likes.card(li, rec(0, 12, 7), {});
        await s.likes.look();
        s.say(200, { signed_in: true, liked: [], csrf: TOKEN }, true);
        const slow = s.likes.look();
        heartsOf(li)[0].click();
        expect('a tap while a look is still out waits for it and changes nothing yet', [lit(li), posts(s).length], [[false, false], 0]);
        s.release(); await slow; await settle();
        expect('... and is made when the look is answered', [lit(li), numbers(li), posts(s).length], [[true, true], ['8', '8'], 1]);
        const t = setup({ liked: [] });
        const first = card(); t.likes.card(first, rec(0, 12, 7), {});
        await t.likes.look();
        t.say(200, { signed_in: true, liked: [], csrf: TOKEN }, true);
        const old = t.likes.look();                                  // (the look of a page that is left: its answer comes late)
        t.likes.begin();
        const next = card(); t.likes.card(next, rec(0, 12, 7), {});
        await t.likes.look();
        heartsOf(next)[0].click(); await settle();
        expect('a look of a page that was left and is answered late does not undo a like made since', [lit(next), numbers(next)], [[true, true], ['8', '8']]);
        t.release(); await old; await settle();
        expect('... the heart stays lit', [lit(next), numbers(next)], [[true, true], ['8', '8']]);
    }

    // ---- a visitor who is not logged in ----
    {
        const s = setup({ signed: false });
        const li = card(); s.likes.card(li, rec(0, 12, 7), {});
        await s.likes.look();
        const asked = s.door.log.length;
        heartsOf(li)[0].click();
        const box = cardsOf(s)[0];
        expect('a tap shows the card with the way in and calls the door for nothing', [cardsOf(s).length, s.door.log.length === asked, lit(li), numbers(li)], [1, true, [false, false], ['7', '7']]);
        expect('the card: the words, the small line and "Log in with Discord" that goes to the way in of this replay', [box.all((n) => n.tag === 'p')[0].textContent, box.all((n) => n.tag === 'small')[0].textContent, box.all((n) => n.tag === 'a').map((a) => [a.textContent, a.href, a.className])], ['Log in to like a replay.Only the count shows. Nobody sees who liked.', 'Only the count shows. Nobody sees who liked.', [['Log in with Discord', '/hub/login?next=match&id=' + IDS[0], 'btn sm']]]);
        expect('... a dialog that has a name, in the page, under the heart (the tip of its arrow is over the heart)', [box.parentNode === s.doc.body, box.getAttribute('role'), box.getAttribute('aria-label'), box.styleProps['--tip'] !== undefined, box.style && true], [true, 'dialog', 'Log in to like a replay.', true, true]);
        expect('... it is as wide as its place allows: left of the heart\'s middle, kept in the window', [box.style.left, box.style.top, box.styleProps['--tip']], ['390px', '342px', '32px']);
        const link = box.all((n) => n.tag === 'a')[0];
        expect('the link has the keyboard (a reader of the keys comes to it from the heart)', s.doc.activeElement === link, true);
        s.doc.fire('pointerdown', { target: link });
        expect('a press inside the card keeps it', cardsOf(s).length, 1);
        s.doc.fire('keydown', { key: 'Enter' });
        expect('another key keeps it', cardsOf(s).length, 1);
        s.doc.fire('keydown', { key: 'Escape' });
        expect('Escape takes it away', cardsOf(s).length, 0);
        heartsOf(li)[1].click(); heartsOf(li)[0].click();
        expect('a card for each tap is one card, the newest', cardsOf(s).length, 1);
        s.doc.fire('pointerdown', { target: s.doc.body });
        expect('a press outside takes it away', cardsOf(s).length, 0);
        heartsOf(li)[0].click();
        s.likes.begin();
        expect('a new page of the list takes it away', cardsOf(s).length, 0);
    }
    {
        const s = setup({ signed: false }), h = hero();
        s.likes.hero(h, rec(0, 12, 7), {});
        await s.likes.look();
        h.querySelector('#like').click();
        expect('the match page\'s heart says the same', [cardsOf(s).length, cardsOf(s)[0].all((n) => n.tag === 'a').map((a) => a.href), gets(s).length, posts(s).length], [1, ['/hub/login?next=match&id=' + IDS[0]], 1, 0]);
    }
    {
        const s = setup({ signed: false }), li = card();
        s.likes.card(li, rec(0, 12, 7), {});
        await s.likes.look();
        const wide = heartsOf(li)[0]; wide.rect = { left: 2, top: 10, width: 40, height: 20 };
        wide.click();
        const a = cardsOf(s)[0].style, tip = cardsOf(s)[0].styleProps['--tip'];
        expect('a heart at the left edge: the card keeps 8 px from the edge, and its arrow still points at the heart', [a.left, tip], ['8px', '6px']);
        s.win.scrollY = 500; s.win.scrollX = 20; wide.rect = { left: 960, top: 10, width: 40, height: 20 };
        wide.click();
        const b = cardsOf(s)[0].style, btip = cardsOf(s)[0].styleProps['--tip'];
        expect('a heart at the right edge, on a page that is scrolled: the card ends before the edge, in the coordinates of the page', [b.left, b.top, btip], ['776px', '542px', '216px']);
    }

    // ---- a page of the list comes after another ----
    {
        const s = setup({ liked: [IDS[0], IDS[3]] });
        const one = card(); s.likes.card(one, rec(0, 1, 1), {});
        await s.likes.look();
        s.likes.begin();
        const two = [card(), card()]; s.likes.card(two[0], rec(2, 1, 1), {}); s.likes.card(two[1], rec(3, 1, 1), {});
        await s.likes.look();
        expect('a new page asks for its own cards only, and shows its own hearts', [gets(s).map((u) => new URL(u, 'http://x').searchParams.get('ids')), two.map(lit)], [[IDS[0], IDS[2] + ',' + IDS[3]], [[false, false], [true, true]]]);
        s.door.signed = false;
        s.likes.begin();
        const three = card(); s.likes.card(three, rec(0, 1, 1), {});
        await s.likes.look();
        expect('a visitor who has logged out in the meantime sees no lit heart on the next page', lit(three), [false, false]);
    }
    await settle();
    console.log('likes: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('FAIL the check stopped: ' + (e && e.stack || e)); process.exit(1); });
