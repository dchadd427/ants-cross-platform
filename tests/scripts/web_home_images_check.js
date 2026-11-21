// Runs the images of Home's chat (web/home/pics.js, web/home/post.js, and the image parts of web/home/chat.js; docs/HUB_IMAGES.md, docs/HOME_CHAT.md "Images") in node, on a small fake page of this file's own (the parts
// take their page as an argument, as the dock's other parts do). What it checks:
//   1. the pure parts: the address of an image is made only from a checked name and type (a name that is not 32 lower-case hex digits, a type that is not jpg, png or gif, a size that is not a number: no address);
//      what the hub's answers to an upload mean in words; the sizes in words; which file is an image; which drag carries files; chat.js reads `img` and `you.img` and nothing else of them;
//   2. the reader: a thumbnail for each line (a button with an img of the frame's own size, the still, lazy, the caption for its alt), none for an image that is not good; the viewer (a dialog, Escape, the
//      focus back, Tab kept inside, "Open in a new tab" a plain link); Delete only for a moderator, an administrator or the image's own author, after a question, by `/del` or by `delimg`; an animated image
//      is the GIF only while it is on the screen, the window is shown, the reader's switch is on and the device does not ask for reduced motion; the switch is kept in the browser and works without storage;
//   3. the poster: the button beside the microphone (and the phone's two rows), a paste or a drop or the chooser, a card for a good image and for a bad one (not an image, too big), the upload (the one request,
//      its two headers, the bytes as they are, the progress), the line with the caption, and every answer of the hub in plain words; a paste of words is left alone;
//   4. the sources: no markup written into the page, no storage but the reader's one switch, no address that is not /hub/img/.
// tests/scripts/test_web_home_images.py runs this with node (the quick tier). usage: node web_home_images_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_home_images_check.js lobby.html'); process.exit(2); }
const home = path.join(path.dirname(path.resolve(lobbyPath)), 'home');
const picsSource = fs.readFileSync(path.join(home, 'pics.js'), 'utf8'), postSource = fs.readFileSync(path.join(home, 'post.js'), 'utf8');
const Pics = require(path.join(home, 'pics.js')), Post = require(path.join(home, 'post.js')), Chat = require(path.join(home, 'chat.js'));

let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }
const NAME = '0123456789abcdef0123456789abcdef', OTHER = 'fedcba9876543210fedcba9876543210';
const IMG = (o) => Object.assign({ n: NAME, x: 'png', w: 1280, h: 720, tw: 360, th: 203, a: false }, o || {});

// ---- a small fake page --------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const writes = [];                                  // (every write of markup: none is allowed)
class El {
    constructor(tag, doc) { this.tagName = String(tag).toLowerCase(); this.doc = doc; this.children = []; this.parentNode = null; this.attrs = {}; this.listeners = {}; this.className = ''; this._text = ''; this.hidden = false; this.disabled = false; this.value = ''; this.checked = false; this.files = []; this.style = {}; }
    set textContent(v) { this.children.forEach((c) => { c.parentNode = null; }); this.children = []; this._text = String(v); }
    get textContent() { return this._text + this.children.map((c) => c.textContent).join(''); }
    set innerHTML(v) { writes.push(String(v)); }
    get isConnected() { let n = this; while (n.parentNode) n = n.parentNode; return n === this.doc.body || n === this.doc.root; }
    appendChild(c) { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = this; this.children.push(c); return c; }
    insertBefore(c, ref) { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = this; const at = ref ? this.children.indexOf(ref) : -1; if (at < 0) this.children.push(c); else this.children.splice(at, 0, c); return c; }
    removeChild(c) { this.children = this.children.filter((x) => x !== c); c.parentNode = null; return c; }
    get nextSibling() { const p = this.parentNode; return p ? p.children[p.children.indexOf(this) + 1] || null : null; }
    setAttribute(k, v) { this.attrs[k] = String(v); if (k === 'class') this.className = String(v); }
    getAttribute(k) { return Object.prototype.hasOwnProperty.call(this.attrs, k) ? this.attrs[k] : null; }
    removeAttribute(k) { delete this.attrs[k]; }
    addEventListener(t, fn) { (this.listeners[t] = this.listeners[t] || []).push(fn); }
    fire(t, ev) { ev = Object.assign({ target: this, defaultPrevented: false, preventDefault() { this.defaultPrevented = true; }, stopImmediatePropagation() { this.stopped = true; }, stopPropagation() {} }, ev || {}); for (const fn of (this.listeners[t] || [])) { fn(ev); if (ev.stopped) break; } return ev; }
    click() { if (this.disabled) return; this.doc.clicks.push(this); this.fire('click'); }
    focus() { this.doc.activeElement = this; }
    all(test) { const out = []; const walk = (n) => { n.children.forEach((c) => { if (test(c)) out.push(c); walk(c); }); }; walk(this); return out; }
    querySelectorAll(sel) { return this.all(matcher(sel)); }
    querySelector(sel) { return this.querySelectorAll(sel)[0] || null; }
    contains(o) { for (let n = o; n; n = n.parentNode) if (n === this) return true; return false; }
    getContext() { return { drawImage() {} }; }
    toBlob(cb, type, q) { this.doc.blobs.push({ w: this.width, h: this.height, type, q }); cb({ size: 123456, type }); }
}
function matcher(sel) { return (e) => sel.split(',').some((one) => { one = one.trim(); const m = /^([a-z]*)((?:\.[A-Za-z0-9_-]+)*)$/.exec(one); if (!m) return false; return (!m[1] || e.tagName === m[1]) && m[2].split('.').filter(Boolean).every((c) => String(e.className).split(/\s+/).indexOf(c) >= 0); }); }
function page(o) {
    o = o || {};
    const doc = { hidden: false, activeElement: null, clicks: [], listeners: {}, root: null, byId: {}, blobs: [] };
    doc.createElement = (t) => new El(t, doc); doc.createElementNS = (ns, t) => new El(t, doc); doc.createTextNode = (t) => { const e = new El('#text', doc); e._text = t; return e; };
    doc.body = new El('body', doc); doc.getElementById = (id) => doc.byId[id] || null;
    doc.addEventListener = (t, fn) => { (doc.listeners[t] = doc.listeners[t] || []).push(fn); };
    doc.removeEventListener = (t, fn) => { doc.listeners[t] = (doc.listeners[t] || []).filter((f) => f !== fn); };
    doc.fire = (t, ev) => { ev = Object.assign({ preventDefault() { this.defaultPrevented = true; }, stopPropagation() { this.stopped = true; } }, ev || {}); for (const fn of (doc.listeners[t] || []).slice()) fn(ev); return ev; };
    const store = {}, observers = [];
    const win = { innerWidth: o.width || 1280, innerHeight: o.height || 800, localStorage: o.noStorage ? { getItem() { throw new Error('refused'); }, setItem() { throw new Error('refused'); } } : { getItem: (k) => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); } }, store, observers,
        matchMedia: () => ({ matches: !!o.reduced, addEventListener() {} }), setTimeout: () => 0, URL: { createObjectURL: () => 'blob:made', revokeObjectURL: (u) => { win.revoked.push(u); } }, revoked: [] };
    if (o.observer !== false) win.IntersectionObserver = class { constructor(fn, opts) { this.fn = fn; this.opts = opts; this.watched = []; observers.push(this); } observe(n) { this.watched.push(n); } unobserve(n) { this.watched = this.watched.filter((x) => x !== n); } };
    win.XMLHttpRequest = class { constructor() { this.headers = {}; this.upload = {}; win.requests.push(this); } open(m, u) { this.method = m; this.url = u; } setRequestHeader(k, v) { this.headers[k] = v; } send(b) { this.body = b; } abort() { this.aborted = true; } };
    win.requests = [];
    win.MutationObserver = class { constructor(fn) { this.fn = fn; win.mutations = this; } observe() {} disconnect() {} };
    return { doc, win, store };
}
const cls = (e) => String(e.className).split(/\s+/);
const has = (e, c) => cls(e).indexOf(c) >= 0;
const byClass = (root, c) => root.all((e) => has(e, c));

// ---- 1. the pure parts ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    same('an address of an image: the name, the type', [Pics.address(IMG()), Pics.address(IMG({ x: 'gif' })), Pics.address(IMG({ x: 'jpg' }), true)], ['/hub/img/' + NAME + '.png', '/hub/img/' + NAME + '.gif', '/hub/img/' + NAME + '.t.jpg']);
    same('...and no address from a name or a type that is not the hub\'s', [Pics.address(IMG({ n: NAME.toUpperCase() })), Pics.address(IMG({ n: NAME.slice(1) })), Pics.address(IMG({ n: '../' + NAME.slice(3) })), Pics.address(IMG({ n: NAME + 'a' })),
        Pics.address(IMG({ x: 'svg' })), Pics.address(IMG({ x: 'webp' })), Pics.address(IMG({ x: 'jpg.exe' })), Pics.address(null), Pics.address(IMG({ tw: 0 })), Pics.address(IMG({ th: 'x' })), Pics.address(IMG({ tw: 401 })), Pics.address(IMG({ th: 1.5 })), Pics.address(IMG({ w: -1 })), Pics.address(IMG({ h: 99999 })), Pics.address(IMG({ w: '1280' })), Pics.address('png'), Pics.address(IMG({ x: ['png'] })), Pics.address(IMG({ n: { toString() { return NAME; } } }))],
        new Array(18).fill(''));
    same('the sizes in words', Pics.sizeWords(IMG()), '1280\u00a0\u00d7\u00a0720 \u00b7 PNG');
    same('the words of the hub\'s answers to an upload', [Post.wordsOf(413, 'big'), Post.wordsOf(415, 'type'), Post.wordsOf(422, 'pixels'), Post.wordsOf(422, 'bad'), Post.wordsOf(422, 'frames'), Post.wordsOf(429, 'rate'), Post.wordsOf(429, 'busy'), Post.wordsOf(503, 'down'), Post.wordsOf(503, 'full'),
        Post.wordsOf(403, 'images'), Post.wordsOf(401, 'login'), Post.wordsOf(0, null), Post.wordsOf(500, 'error'), Post.wordsOf(503, 'something else'), Post.wordsOf(418, '<b>x</b>')], ['big', 'not', 'pixels', 'bad', 'frames', 'rate', 'busy', 'down', 'full', 'images', 'login', 'wrong', 'wrong', 'down', 'wrong']);
    same('a size in words', [Post.kb(300 * 1024), Post.kb(5 * 1048576), Post.kb(50 * 1024)], ['300\u00a0KB', '5\u00a0MB', '50\u00a0KB']);
    const png = { type: 'image/png' }, gif = { type: 'image/gif' }, pdf = { type: 'application/pdf' };
    check('which file is an image: the first of a list that is PNG, JPEG, WebP or GIF; none of the others', Post.imageOf([pdf, png]) === png && Post.imageOf([pdf]) === null && Post.imageOf([{ type: 'image/svg+xml' }, { type: 'image/bmp' }, { type: 'image/heic' }]) === null && Post.imageOf(null) === null && Post.imageOf([gif]) === gif);
    check('which drag carries files', Post.hasFile({ types: ['text/plain', 'Files'] }) && !Post.hasFile({ types: ['text/plain'] }) && !Post.hasFile(null) && Post.hasFile({ types: [], files: [png] }));
    same('chat.js reads `img` of a line: a checked name and a type; a line with no words is still a line when it has a good image', [Chat.lineOf({ id: 1, who: 'a', text: '', img: IMG() }).img.n, Chat.lineOf({ id: 1, who: 'a', text: '', img: { n: 'nope' } }), Chat.lineOf({ id: 1, who: 'a', text: '', img: { n: 'abc123' } }), Chat.lineOf({ id: 1, who: 'a', text: '', img: { n: NAME.toUpperCase() } }), Chat.lineOf({ id: 1, who: 'a', text: '', img: { n: NAME + 'a' } }), Chat.lineOf({ id: 1, who: 'a', text: '' }), Chat.lineOf({ id: 1, who: 'a', text: 'hi', img: 'x' }).img, Chat.lineOf({ id: 1, who: 'a', text: 'hi', sys: true, img: IMG() }).img],
        [NAME, null, null, null, null, null, null, null]);
    same('...and `you.img`: on, role, off, wait, and nothing else', [Chat.youOf({ name: 'a', img: 'on' }).img, Chat.youOf({ name: 'a', img: 'role' }).img, Chat.youOf({ name: 'a', img: 'off' }).img, Chat.youOf({ name: 'a', img: 'wait' }).img, Chat.youOf({ name: 'a', img: 'yes' }).img, Chat.youOf({ name: 'a' }).img], ['on', 'role', 'off', 'wait', '', '']);
    same('the words for a refusal of a line with an image are the dock\'s own', [Chat.ERR.img, Chat.ERR.imgid, /Active Anters/.test(Chat.ERR.imgrole), /ask a moderator/.test(Chat.ERR.imgoff), /Messages still work/.test(Chat.ERR.imgwait)], ['You cannot post images now', 'That image is not there any more: add it again', true, true, true]);
}

// ---- 2. the reader --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function reader(o) {
    o = o || {};
    const p = page(o), log = new El('div', p.doc), slot = new El('div', p.doc); slot.className = 'gc-slot'; p.doc.byId['gchat-login-slot'] = slot; const root = new El('section', p.doc); p.doc.body.appendChild(root); root.appendChild(log);
    const sw = new El('div', p.doc); sw.className = 'gc-sw'; slot.appendChild(sw);
    const calls = [], lines = { 7: { id: 7, who: 'maple', text: 'a map', own: !!o.own, img: true }, 8: { id: 8, who: 'pebble', text: '', own: false, img: true } };
    const dock = { root, log, you: () => (o.role ? { name: 'x', role: o.role } : null), line: (id) => lines[id] || null, clock: () => ({ time: '10:38 AM' }), command: (text, done) => { calls.push(['command', text]); setTimeout.last = done; o.answer = done; }, delimg: (id, done) => { calls.push(['delimg', id]); o.answer = done; } };
    const api = Pics.mount({ doc: p.doc, win: p.win, dock });
    return { p, api, log, calls, slot, o };
}
function line(r, e) { const row = new El('div', r.p.doc); row.className = 'gc-line'; r.log.appendChild(row); r.api.draw(row, e); return row; }
{
    const r = reader();
    const row = line(r, { id: 7, who: 'maple', text: 'a map', at: 1, img: IMG() });
    const b = byClass(row, 'gc-thumb')[0], im = b && b.querySelector('img');
    check('a thumbnail: a button in a span after the words, with an img of the still', !!b && b.tagName === 'button' && b.type === 'button' && !!im && im.getAttribute('src') === '/hub/img/' + NAME + '.t.jpg' && has(b.parentNode, 'gc-pic'));
    check('...the frame\'s own size (the log does not jump), lazy, async, not draggable, the caption for its alt', im.getAttribute('width') === '360' && im.getAttribute('height') === '203' && im.getAttribute('loading') === 'lazy' && im.getAttribute('decoding') === 'async' && im.getAttribute('alt') === 'a map' && im.getAttribute('draggable') === 'false');
    check('...a name for a screen reader that says what a press does', /Open the image/.test(b.getAttribute('aria-label')) && /a map/.test(b.getAttribute('aria-label')));
    const bare = line(r, { id: 8, who: 'pebble', text: '', img: IMG({ n: OTHER, x: 'jpg' }) });
    check('an image with no words: "Image from NAME" is its alt, and its name for a screen reader says who', byClass(bare, 'gc-thumb')[0].querySelector('img').getAttribute('alt') === 'Image from pebble' && /by pebble/.test(byClass(bare, 'gc-thumb')[0].getAttribute('aria-label')));
    const none = line(r, { id: 9, who: 'x', text: 'y', img: IMG({ n: 'bad' }) });
    check('an image that is not good gets nothing drawn', byClass(none, 'gc-thumb').length === 0);
    im.fire('error');
    check('a file that does not load is a quiet box that says so and cannot be pressed', has(b, 'gc-nopic') && b.disabled && /Image not available/.test(b.textContent) && !b.querySelector('img'));
    check('nothing was written as markup', writes.length === 0, writes.join('|'));
}
{
    const r = reader({ role: 'none' });
    const row = line(r, { id: 7, who: 'maple', text: 'a map', at: 1, img: IMG() }), b = byClass(row, 'gc-thumb')[0];
    r.p.doc.focus = null; b.focus(); b.click();
    const scrim = r.p.doc.body.querySelector('.gv-scrim');
    check('a press opens the viewer: a dialog over the page that holds the whole image, the name, the time, the caption', !!scrim && scrim.getAttribute('role') === 'dialog' && scrim.getAttribute('aria-modal') === 'true' && scrim.querySelector('.gv-img').getAttribute('src') === '/hub/img/' + NAME + '.png'
        && /maple/.test(scrim.querySelector('.gv-who').textContent) && /10:38 AM/.test(scrim.textContent) && /a map/.test(scrim.querySelector('.gv-cap').textContent));
    const link = scrim.querySelector('a');
    check('"Open in a new tab" is a plain link to the file, with no referrer and no opener', link.getAttribute('href') === '/hub/img/' + NAME + '.png' && link.getAttribute('target') === '_blank' && /noopener/.test(link.getAttribute('rel')) && /noreferrer/.test(link.getAttribute('rel')));
    check('a reader has no Delete', !scrim.all((e) => /Delete/.test(e.textContent) && e.tagName === 'button').length);
    check('the focus is in the box', r.p.doc.activeElement !== null && scrim.contains(r.p.doc.activeElement));
    const esc = r.p.doc.fire('keydown', { key: 'Escape' });
    check('Escape closes it, takes the key for itself and gives the focus back to the thumbnail', !r.p.doc.body.querySelector('.gv-scrim') && esc.defaultPrevented && r.p.doc.activeElement === b);
    b.click(); const again = r.p.doc.body.querySelector('.gv-scrim');
    const buttons = again.all((e) => e.tagName === 'button' || e.tagName === 'a');
    buttons[buttons.length - 1].focus(); const tab = r.p.doc.fire('keydown', { key: 'Tab', shiftKey: false });
    check('Tab from the last control goes to the first (the focus stays in the box), and Shift+Tab from the first to the last', tab.defaultPrevented && r.p.doc.activeElement === buttons[0]);
    buttons[0].focus(); r.p.doc.fire('keydown', { key: 'Tab', shiftKey: true });
    check('...and back', r.p.doc.activeElement === buttons[buttons.length - 1]);
    again.fire('click', { target: again });
    check('a press on the dimmed page around the box closes it', !r.p.doc.body.querySelector('.gv-scrim'));
}
for (const [who, role, own, word, how] of [['a moderator', 'mod', false, 'Delete this image', ['command', '/del 7']], ['an administrator', 'admin', false, 'Delete this image', ['command', '/del 7']], ['a moderator who is the author too', 'mod', true, 'Delete this image', ['command', '/del 7']], ['the author', 'none', true, 'Delete my image', ['delimg', 7]]]) {
    const r = reader({ role, own }), row = line(r, { id: 7, who: 'maple', text: 'a map', img: IMG() });
    byClass(row, 'gc-thumb')[0].click();
    let scrim = r.p.doc.body.querySelector('.gv-scrim'), del = scrim.all((e) => e.tagName === 'button' && e.textContent === word)[0];
    check(who + ' has "' + word + '"', !!del);
    del.click();
    check('...and is asked first: the file goes from the server, and it cannot be undone', /deleted from the site’s server/.test(scrim.querySelector('.gv-ask').textContent) && !r.calls.length);
    scrim.all((e) => e.textContent === 'Cancel')[0].click();
    check('...Cancel goes back with nothing done', !r.calls.length && !!scrim.querySelector('a'));
    scrim.all((e) => e.tagName === 'button' && e.textContent === word)[0].click(); scrim.all((e) => e.textContent === 'Delete')[0].click();
    same('...and Delete sends ' + how[0], r.calls[0], how);
    r.o.answer('rate');
    check('...a refusal is said in words and the box stays', !!r.p.doc.body.querySelector('.gv-scrim') && /Too fast/.test(scrim.querySelector('.gv-ask').textContent));
    scrim.all((e) => e.textContent === 'Delete')[0].click(); r.o.answer('ok');
    check('...and the hub\'s word that the line is gone closes the box', !r.p.doc.body.querySelector('.gv-scrim'));
}
{
    const r = reader({ role: 'mod' }), row = line(r, { id: 7, who: 'maple', text: 'a map', img: IMG() });
    check('a person who is staff and the author too is offered the staff\'s delete (the hub\'s /del has the log)', (byClass(row, 'gc-thumb')[0].click(), r.p.doc.body.querySelector('.gv-scrim').all((e) => e.textContent === 'Delete this image').length === 1));
}
{
    const gif = IMG({ x: 'gif', a: true }), still = '/hub/img/' + NAME + '.t.jpg', moving = '/hub/img/' + NAME + '.gif';
    const r = reader(), row = line(r, { id: 7, who: 'maple', text: '', img: gif }), im = byClass(row, 'gc-thumb')[0].querySelector('img'), io = r.p.win.observers[0];
    check('an animated image starts as its still and is watched on the log', im.getAttribute('src') === still && io && io.watched[0] === im && io.opts.root === r.log);
    check('...a marker shows that it moves', byClass(byClass(row, 'gc-thumb')[0], 'gc-zoom').length === 1);
    io.fn([{ target: im, isIntersecting: true }]);
    check('...it plays while it is on the screen', im.getAttribute('src') === moving);
    io.fn([{ target: im, isIntersecting: false }]);
    check('...and rests when it is off the screen', im.getAttribute('src') === still);
    io.fn([{ target: im, isIntersecting: true }]); r.p.doc.hidden = true; r.p.doc.fire('visibilitychange');
    check('...and while the window is hidden', im.getAttribute('src') === still);
    r.p.doc.hidden = false; r.p.doc.fire('visibilitychange');
    check('...and plays again when the window comes back', im.getAttribute('src') === moving);
    const cb = r.slot.querySelector('.gp-sw').querySelector('input');
    check('the reader\'s switch is a row in the account\'s dropdown (the picture switch\'s own classes), on at first', !!cb && cb.checked === true && /Play animated images/.test(r.slot.textContent));
    cb.checked = false; cb.fire('change');
    check('...off: the still, and the choice is kept in the browser', im.getAttribute('src') === still && r.p.win.store['ants.chat.animate'] === '0');
    cb.checked = true; cb.fire('change');
    check('...on again', im.getAttribute('src') === moving && r.p.win.store['ants.chat.animate'] === '1');
    byClass(row, 'gc-thumb')[0].click();
    check('the viewer shows the moving image while it moves', r.p.doc.body.querySelector('.gv-img').getAttribute('src') === moving);
    r.api.close();
    const stored = reader({}); stored.p.win.store['ants.chat.animate'] = '0';
    const second = Pics.mount({ doc: stored.p.doc, win: stored.p.win, dock: { root: stored.log, log: stored.log } });
    check('a reader who turned it off finds it off', second.state().on === false);
    const quiet = reader({ reduced: true }), qrow = line(quiet, { id: 7, who: 'a', text: '', img: gif }), qim = byClass(qrow, 'gc-thumb')[0].querySelector('img');
    quiet.p.win.observers[0].fn([{ target: qim, isIntersecting: true }]);
    check('a device that asks for reduced motion is shown the still, even when the image is on the screen', qim.getAttribute('src') === still && quiet.api.state().reduced === true);
    byClass(qrow, 'gc-thumb')[0].click();
    check('...and in the viewer', quiet.p.doc.body.querySelector('.gv-img').getAttribute('src') === still);
    const refused = reader({ noStorage: true }), rrow = line(refused, { id: 7, who: 'a', text: '', img: gif }), rcb = refused.slot.querySelector('.gp-sw').querySelector('input');
    rcb.checked = false; rcb.fire('change');
    check('a browser that refuses its storage still has the switch for the visit', byClass(rrow, 'gc-thumb')[0].querySelector('img').getAttribute('src') === still);
    const bare = reader({ observer: false }), brow = line(bare, { id: 7, who: 'a', text: '', img: gif });
    check('a browser with no IntersectionObserver plays it (nothing says that it is off the screen)', byClass(brow, 'gc-thumb')[0].querySelector('img').getAttribute('src') === moving);
    check('a still image is never watched', (() => { const s = reader(); line(s, { id: 7, who: 'a', text: '', img: IMG() }); return s.p.win.observers.length === 0 || s.p.win.observers[0].watched.length === 0; })());
}

// ---- 3. the poster --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function poster(o) {
    o = o || {};
    const p = page(o), root = new El('section', p.doc), form = new El('form', p.doc), field = new El('textarea', p.doc), send = new El('button', p.doc), tools = new El('span', p.doc), emoji = new El('button', p.doc), mic = new El('button', p.doc);
    form.className = 'gc-form gt-on'; send.className = 'btn gc-send'; send.textContent = 'Send'; tools.className = 'gt-tools'; emoji.className = 'gt-emoji'; mic.className = 'gt-mic'; field.setAttribute('placeholder', 'Say something to everyone');
    tools.appendChild(emoji); tools.appendChild(mic); [field, tools, send].forEach((c) => form.appendChild(c)); root.appendChild(form); p.doc.body.appendChild(root);
    const sent = [], dock = { root, form, field, grow() {}, sendImage: (id, words, done) => { sent.push([id, words]); o.sendDone = done; return o.refuse || ''; } };
    const api = Post.mount({ doc: p.doc, win: p.win, dock });
    const submit = () => { const ev = root.fire('submit', { target: form }); if (!ev.stopped) form.fire('submit'); return ev; };       // (the DOM's order: the box in the capture phase, then the form's own listeners unless it stopped it)
    return { p, api, form, field, send, tools, sent, root, o, submit };
}
const file = (type, size, name) => ({ type, size, name: name === undefined ? 'shot.png' : name, slice() { return this; } });
{
    const r = poster();
    const btn = r.tools.querySelector('.gt-pic');
    check('the button stands beside the emoji and the microphone, before the microphone, with a name and a hint', !!btn && r.tools.children.indexOf(btn) === 1 && r.tools.children[2] === r.tools.querySelector('.gt-mic') && btn.getAttribute('aria-label') === 'Add an image' && /paste or drop/.test(btn.getAttribute('title')));
    check('a window has no second row', !has(r.form, 'gp-two'));
    const phone = poster({ width: 390, height: 844 });
    check('a phone held upright has the two rows (four buttons leave the one row too little to type in)', has(phone.form, 'gp-two') && !has(poster({ width: 800, height: 400 }).form, 'gp-two'));
    const later = (() => { const q = page({}), f = new El('form', q.doc); f.className = 'gc-form'; const fld = new El('textarea', q.doc), tl = new El('span', q.doc), mic = new El('button', q.doc); tl.className = 'gt-tools'; mic.className = 'gt-mic'; tl.appendChild(mic); f.appendChild(fld); const root = new El('section', q.doc); root.appendChild(f); q.doc.body.appendChild(root); Post.mount({ doc: q.doc, win: q.win, dock: { root, form: f, field: fld, grow() {}, sendImage() { return ''; } } }); return { q, f, tl, mic }; })();
    check('a dock whose helper buttons come later gets the button when they do (and not before)', !later.tl.querySelector('.gt-pic') && (later.f.appendChild(later.tl), later.q.win.mutations.fn(), later.tl.children[0].className.indexOf('gt-pic') >= 0));
    r.api.add(file('image/png', 214 * 1024));
    let card = r.form.querySelector('.gp-card');
    check('an image that is added shows a card above the field: ready, its name or "Pasted image", its size, and that the details are removed', !!card && /Image ready to send/.test(card.textContent) && /shot\.png/.test(card.textContent) && /214\u00a0KB/.test(card.textContent) && /Location and camera details are removed/.test(card.textContent));
    check('...the field is the caption and Send says what it sends', r.field.getAttribute('placeholder') === 'Add a caption (optional)' && r.send.textContent === 'Send image');
    check('...the picture is the browser\'s own object address and the card has a button that removes it', card.querySelector('.gp-th').getAttribute('src') === 'blob:made' && !!card.querySelector('.gp-x'));
    card.querySelector('.gp-x').click();
    check('removing it takes the card, lets go of the address and puts the words back', !r.form.querySelector('.gp-card') && r.p.win.revoked.length === 1 && r.field.getAttribute('placeholder') === 'Say something to everyone' && r.send.textContent === 'Send');
    r.api.add(file('image/gif', 8 * 1024, 'meme.gif'));
    check('a GIF says that it stays animated, within its limits', /stays animated/.test(r.form.querySelector('.gp-card').textContent) && !/Location/.test(r.form.querySelector('.gp-card').textContent));
    r.api.add(file('image/png', 9.4 * 1048576, 'holiday.png'));
    card = r.form.querySelector('.gp-card');
    check('a file over 4 MB is refused before it is sent: its name, the most, and a button for another', has(card, 'gp-bad') && /too big/.test(card.textContent) && /holiday\.png/.test(card.textContent) && /4 MB/.test(card.textContent) && !!card.querySelector('.gp-again') && r.field.getAttribute('placeholder') === 'Say something to everyone');
    r.api.add(file('application/pdf', 1000, 'a.pdf'));
    check('a file that is not an image is said so: nothing was sent', /not an image/.test(r.form.querySelector('.gp-card').textContent) && /PNG, JPEG, WebP or GIF/.test(r.form.querySelector('.gp-card').textContent) && r.p.win.requests.length === 0);
    r.api.add(file('image/svg+xml', 100, 'a.svg'));
    check('an SVG is not an image of the chat', has(r.form.querySelector('.gp-card'), 'gp-bad'));
}
{   // a photo over 2 MiB is shrunk in a canvas (2,048 px at the most, as a JPEG; a PNG of up to 8 MiB stays a PNG); a GIF never (it would stop moving), and a file under 2 MiB goes as it is
    const run = (type, size) => { const r = poster(), seen = []; r.p.win.createImageBitmap = (f) => { seen.push(f.type); return { then(ok) { ok({ width: 4000, height: 3000, close() {} }); } }; }; r.api.add(file(type, size)); return { r, seen, card: r.form.querySelector('.gp-card') }; };
    const jpg = run('image/jpeg', 3 * 1048576);
    check('a 3 MB photo is drawn into a canvas of 2048 x 1536 and sent as a JPEG of quality 0.85 (its size is the new one)', jpg.seen.join() === 'image/jpeg' && jpg.r.p.doc.blobs.length === 1 && jpg.r.p.doc.blobs[0].w === 2048 && jpg.r.p.doc.blobs[0].h === 1536 && jpg.r.p.doc.blobs[0].type === 'image/jpeg' && jpg.r.p.doc.blobs[0].q === 0.85 && /121\u00a0KB/.test(jpg.card.textContent));
    const png = run('image/png', 3 * 1048576);
    check('...a PNG of that size stays a PNG (a screenshot would blur)', png.r.p.doc.blobs[0].type === 'image/png');
    const big = run('image/png', 9 * 1048576);
    check('...a PNG of 9 MB becomes a JPEG', big.r.p.doc.blobs[0].type === 'image/jpeg');
    const gif = run('image/gif', 3 * 1048576);
    check('a GIF of 3 MB is never drawn (it goes as it is and keeps its frames)', gif.seen.length === 0 && gif.r.p.doc.blobs.length === 0 && /3\u00a0MB/.test(gif.card.textContent) && /stays animated/.test(gif.card.textContent));
    const bigGif = run('image/gif', 5 * 1048576);
    check('...and one over 4 MB is too big (nothing can shrink it)', has(bigGif.card, 'gp-bad') && /too big/.test(bigGif.card.textContent));
    const small = run('image/jpeg', 1048576);
    check('a photo under 2 MB goes as it is', small.seen.length === 0 && /Image ready/.test(small.card.textContent));
    const huge = run('image/jpeg', 30 * 1048576);
    check('a photo of 30 MB is shrunk first, then judged by its new size (the page does not refuse what it can make small)', huge.seen.length === 1 && /Image ready/.test(huge.card.textContent));
}
{
    const r = poster();
    const text = r.field.fire('paste', { clipboardData: { files: [], items: [{ kind: 'string', type: 'text/plain' }], types: ['text/plain'] } });
    check('a paste of words is left alone', !text.defaultPrevented && !r.form.querySelector('.gp-card'));
    const f = file('image/png', 5000, 'pasted.png'), ev = r.field.fire('paste', { clipboardData: { files: [f], items: [], types: ['Files'] } });
    check('a paste of an image is taken (it is not put in the field) and shows the card', ev.defaultPrevented && !!r.form.querySelector('.gp-card'));
    const viaItems = poster(), ev2 = viaItems.field.fire('paste', { clipboardData: { files: [], items: [{ kind: 'file', type: 'image/png', getAsFile: () => file('image/png', 100, '') }] } });
    check('...also where the browser lists it in the clipboard\'s items', ev2.defaultPrevented && /Pasted image/.test(viaItems.form.querySelector('.gp-card').textContent));
    const d = poster(), over = d.root.fire('dragenter', { dataTransfer: { types: ['Files'] } });
    check('a file dragged over the dock shows what a drop does', has(d.root, 'gp-over') && /Drop the image to add it/.test(d.root.querySelector('.gp-dropmsg').textContent));
    const hover = d.root.fire('dragover', { dataTransfer: { types: ['Files'] } });
    check('...and is let in (a drop is not refused by the page)', hover.defaultPrevented);
    d.root.fire('dragleave', { dataTransfer: { types: ['Files'] } });
    check('...and leaving takes the message away', !has(d.root, 'gp-over') && !d.root.querySelector('.gp-dropmsg'));
    d.root.fire('dragenter', { dataTransfer: { types: ['Files'] } });
    const drop = d.root.fire('drop', { dataTransfer: { types: ['Files'], files: [file('image/jpeg', 70000, 'photo.jpg')] } });
    check('a drop of an image shows the card and takes the message away; the browser does not open the file', drop.defaultPrevented && /photo\.jpg/.test(d.form.querySelector('.gp-card').textContent) && !d.root.querySelector('.gp-dropmsg'));
    const other = poster().root.fire('drop', { dataTransfer: { types: ['text/plain'] } });
    check('a drop of words is left alone', !other.defaultPrevented);
    const bad = poster(); bad.root.fire('drop', { dataTransfer: { types: ['Files'], files: [file('application/zip', 100, 'a.zip')] } });
    check('a drop of a file that is no image says so', /not an image/.test(bad.form.querySelector('.gp-card').textContent));
}
{
    const r = poster();
    r.api.add(file('image/png', 214 * 1024)); r.field.value = 'the best spot';
    const ev = r.submit();
    check('Send with an image waiting is the image\'s: the dock\'s own submit does not see it', ev.defaultPrevented && ev.stopped);
    const req = r.p.win.requests[0];
    check('the upload is one POST to /hub/img/up with the page\'s header and the type of bytes, and nothing else', req.method === 'POST' && req.url === '/hub/img/up' && req.headers['Content-Type'] === 'application/octet-stream' && req.headers['X-Ants-Image'] === '1' && Object.keys(req.headers).length === 2);
    check('...the bytes are the file itself', req.body && req.body.size === 214 * 1024);
    check('...the card shows that it is sending', /Sending the image/.test(r.form.querySelector('.gp-card').textContent) && !!r.form.querySelector('.gp-bar'));
    req.upload.onprogress({ lengthComputable: true, loaded: 50, total: 100 });
    check('...and how far', r.form.querySelector('.gp-bar').getAttribute('aria-valuenow') === '50');
    const again = r.submit();
    check('a second Send while it goes does nothing more (one upload)', again.defaultPrevented && r.p.win.requests.length === 1);
    req.status = 200; req.responseText = JSON.stringify({ id: NAME, w: 1, h: 1, x: 'png', anim: false, still: false }); req.onload();
    same('the id goes to the dock with the caption', r.sent, [[NAME, 'the best spot']]);
    r.o.sendDone('ok');
    check('the line that comes back ends it: the field, the card and the address are cleared', r.field.value === '' && !r.form.querySelector('.gp-card') && r.p.win.revoked.length === 1 && r.send.textContent === 'Send');
    const q = poster(); q.api.add(file('image/png', 100)); q.submit(); q.p.win.requests[0].status = 200; q.p.win.requests[0].responseText = JSON.stringify({ id: NAME }); q.p.win.requests[0].onload(); q.o.sendDone('img');
    check('a line that the hub refuses leaves the image waiting, so that Send may be pressed again, and says so', /did not take it/.test(q.form.querySelector('.gp-card').textContent) && /Image ready/.test(q.form.querySelector('.gp-card').textContent) && q.api.state().waiting && !q.api.state().busy);
    const w = poster({ refuse: 'offline' }); w.api.add(file('image/png', 100)); w.submit(); w.p.win.requests[0].status = 200; w.p.win.requests[0].responseText = JSON.stringify({ id: NAME }); w.p.win.requests[0].onload();
    check('a dock that cannot send the line (offline) leaves the image waiting too', /did not take it/.test(w.form.querySelector('.gp-card').textContent) && !w.api.state().busy);
}
for (const [status, body, words, again] of [[413, '{"error":"big"}', /too big/, true], [415, '{"error":"type"}', /not an image/, true], [422, '{"error":"pixels"}', /too many pixels/, true], [422, '{"error":"bad"}', /could not be read/, true], [422, '{"error":"frames"}', /too long/, true], [429, '{"error":"rate"}', /too fast/, false],
    [429, '{"error":"busy"}', /busy converting/, false], [503, '{"error":"down"}', /not available right now/, false], [503, '{"error":"full"}', /no room/, false], [403, '{"error":"images"}', /cannot post images/, false], [401, '{"error":"login"}', /Log in/, false], [500, 'not json', /did not work/, false],
    [403, 'a page, not JSON', /cannot post images/, false], [401, '', /Log in/, false], [413, '<html>', /too big/, true], [415, '', /not an image/, true], [429, '<html>', /too fast/, false], [503, '<html>', /not available right now/, false]]) {
    const r = poster(); r.api.add(file('image/png', 100)); r.field.value = 'words'; r.submit();
    const req = r.p.win.requests[0]; req.status = status; req.responseText = body; req.onload();
    const card = r.form.querySelector('.gp-card');
    check('the hub says ' + status + ' ' + body.slice(0, 30) + ': the card says it in plain words' + (again ? ', with a button for another image' : ', and Send may be pressed again'), has(card, 'gp-bad') && words.test(card.textContent) && !!card.querySelector('.gp-again') === again && r.sent.length === 0 && r.field.value === 'words', card.textContent);
}
{
    const r = poster(); r.api.add(file('image/png', 100)); r.submit(); r.p.win.requests[0].onerror();
    check('a network that fails says so and keeps what was written', /not sent/.test(r.form.querySelector('.gp-card').textContent) && r.field.value === '');
    const x = poster(); x.api.add(file('image/png', 100)); x.submit(); const req = x.p.win.requests[0]; x.form.querySelector('.gp-x').click();
    check('removing the image while it goes aborts the upload and a late answer does nothing', req.aborted === true && (req.status = 200, req.responseText = JSON.stringify({ id: NAME }), req.onload(), x.sent.length === 0));
}
check('Send with no image waiting is not touched', (() => { const e = poster(), ev = e.submit(); return !ev.defaultPrevented && !ev.stopped; })());

// ---- 4. the sources -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
for (const [name, source] of [['pics.js', picsSource], ['post.js', postSource]]) {
    check(name + ' writes no markup (no innerHTML, no outerHTML, no insertAdjacentHTML, no document.write, no eval)', !/innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\(|new Function/.test(source));
}
check('pics.js keeps one thing in the browser (the switch), and post.js nothing', (picsSource.match(/setItem|getItem/g) || []).length === 2 && !/localStorage|sessionStorage|indexedDB|document\.cookie/.test(postSource));
check('the only addresses of images are /hub/img/ (pics.js) and /hub/img/up (post.js), and no other site', (picsSource.match(/'\/hub\/[^']*'/g) || []).join() === "'/hub/img/'" && (postSource.match(/'\/hub\/[^']*'/g) || []).join() === "'/hub/img/up'"
    && !/https?:\/\/|\/\/[a-z]+\./.test((picsSource + postSource).replace(/^\s*\/\/.*$/gm, '').replace(/'http:\/\/www\.w3\.org\/2000\/svg'/g, '')));
check('the upload is made with the two headers and only with them', (postSource.match(/setRequestHeader\(/g) || []).length === 2 && /'X-Ants-Image', '1'/.test(postSource) && /'Content-Type', 'application\/octet-stream'/.test(postSource));
console.log('home chat images: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
