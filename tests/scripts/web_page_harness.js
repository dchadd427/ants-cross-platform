// The fake browser that runs the front page's OWN code (web/lobby.html: its markup, its inline script, the real scripts of web/front/ and web/home/) in node, with a small fake of the DOM, a fake
// clock and a scripted game server (FakeSocket: env.arrive() opens the newest socket and plays a Welcome and a Room), or any WebSocket that is handed in (options.WebSocket, with options.realTime: the
// real clock; tests/scripts/web_lobby_server_check.js runs the page against a real ants_server this way). One page per runLobby(search, stored, options):
//   search: the address's query; stored: what local storage holds (THROWS: a browser that refuses its storage); options: session (what this tab's session storage holds), noSession, clipboard (false:
//   none), copyResult, share, fetch, noFetch, ua, random (what Math.random() says: 0.5), hash (the address's #...), noWebSocket, WebSocket, realTime, noHome (without web/home/home.js: the page says that it could not load all of
//   its parts), stayHome (the page opens on Home and nobody presses My room: without it, a plain visit presses My room, as a person does, so that the checks of the room itself have a room).
// Used by tests/scripts/web_name_check.js (the page's rules, its name card, its room), tests/scripts/web_home_screen_check.js (Home and the Room) and tests/scripts/web_lobby_server_check.js.
'use strict';
const fs = require('fs');
const path = require('path');
const nodeCrypto = require('crypto');

module.exports = function makeHarness(lobbyPath) {
const lobbyText = fs.readFileSync(lobbyPath, 'utf8');
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const lobbyMarkup = lobbyText.slice(lobbyText.indexOf('<body'), lobbyText.indexOf('<script'));
const lobbyScripts = [...lobbyText.matchAll(/<script([^>]*)>([\s\S]*?)<\/script>/g)];
const lobbyScript = lobbyScripts[lobbyScripts.length - 1][2];

// The scripts of front/ are run as they are, in a sandbox that has the fake WebSocket and the fake clock of one page (they are ES5 with no module system: a page gets a global, node gets module.exports)
function loadFile(rel, scope) {
    const root = {};
    new Function('self', 'WebSocket', 'setTimeout', 'clearTimeout', fs.readFileSync(path.join(lobbyDir, rel), 'utf8'))(root, scope.WebSocket, scope.setTimeout, scope.clearTimeout);
    return root;
}
const loadFront = (file, scope) => loadFile('front/' + file, scope);
const NOSCOPE = { WebSocket: function () { throw new Error('no socket here'); }, setTimeout() { return 0; }, clearTimeout() {} };
const RULES0 = loadFront('lobby_rules.js', NOSCOPE).AntsLobbyRules;
const NET0 = loadFront('lobby_net.js', NOSCOPE).AntsLobbyNet;
const CHAT0 = loadFront('lobby_chat.js', NOSCOPE).AntsLobbyChat;
const SUGGESTION = RULES0.pickName(0.5);               // the page's picked name when Math.random() says 0.5 (the fake page's does)
const hex = (bytes) => NET0.hexOf(bytes);

// the messages of a game server, as the C++ server writes them (the check of the client, tests/scripts/web_lobby_client_check.js, runs the client against the same builders)
const E = NET0.SLOT.Empty, C = NET0.SLOT.Client;
function str8(w, s) { w.push(s.length); for (let i = 0; i < s.length; i++) w.push(s.charCodeAt(i)); }
const KEY = Uint8Array.from(Array.from({ length: 16 }, (_, i) => 0x30 + i));
const welcome = (player, key, flags) => Uint8Array.from([2, player, 4].concat(Array.from(key || KEY), [flags === undefined ? 2 : flags]));
const reject = (reason) => Uint8Array.from([3, reason]);
// seats: [state, name] x 4; o: { map, you, leader, teamA, teamB, flags (3: a lobby that the leader starts; 7: and START was pressed), plan, inGame, mode }
function roomMessage(seats, o) {
    o = o || {};
    const w = [12];
    for (const s of seats) { w.push(s[0]); str8(w, s[1]); w.push(20, 0, 0); }
    str8(w, o.map === undefined ? 'TREASURE.LVL' : o.map);
    w.push(0, o.you === undefined ? 0 : o.you, o.leader === undefined ? 0 : o.leader, o.teamA === undefined ? 255 : o.teamA, o.teamB === undefined ? 255 : o.teamB, o.flags === undefined ? 3 : o.flags);
    for (const k of (o.plan || [0, 0, 0, 0])) w.push(k);
    w.push(o.inGame || 0, o.mode || 0);                                               // (protocol 17: the game mode is the last byte)
    return Uint8Array.from(w);
}
// What the page's Hello says: its name, its room, whether it brings a key and a lobby block (a page that makes the room, or one that only joins)
const helloBytes = (name, room, o) => NET0.encodeHello({ name, room, key: (o && o.key) || null, platform: 0x13, create: o && o.join ? null : { map: 'TREASURE.LVL' } });
const helloName = (b) => String.fromCharCode(...b.slice(4, 4 + b[3]));
const helloRoom = (b) => { const at = 4 + b[3] + 2 + 1; return String.fromCharCode(...b.slice(at + 1, at + 1 + b[at])); };
const CODE = /^[a-z2-9]{6}$/;
const grouped = (code) => code.slice(0, 3) + ' ' + code.slice(3);
const BLOCK_PARAMS = ['roommap', 'roomseats', 'roomteams', 'roomleaderstart'];
const blockText = (link) => { const m = /[?&]room=[^&#]*((?:&room(?:map|seats|teams|leaderstart)=[^&#]*)*)/.exec(String(link)); return m ? m[1] : null; };
const noBlockOf = (env, link) => blockText(link) === '' && BLOCK_PARAMS.every((k) => env.param(link, k) === null) && !/room(map|seats|teams|leaderstart)/.test(String(link));

class El {
    constructor(env, tag, id) {
        this.env = env; this.tagName = tag; this.id = id || ''; this.children = []; this.parent = null; this.attributes = {}; this.listeners = {};
        this._text = ''; this.style = {}; this.hidden = false; this.value = ''; this.className = ''; this.src = ''; this.alt = ''; this.readOnly = false; this._checked = false; this.disabled = false; this.selected = false;
    }
    get parentNode() { return this.parent; }
    get checked() { return this._checked; }
    set checked(on) {                                    // a radio button: checking it unchecks the others of its group (the same name), as a browser does
        const radio = (e) => e.tagName === 'input' && e.attributes.type === 'radio' && e.attributes.name;
        if (on && radio(this)) for (const other of Object.values(this.env.elements)) if (other !== this && radio(other) && other.attributes.name === this.attributes.name) other._checked = false;
        this._checked = !!on;
    }
    get placeholder() { return Object.prototype.hasOwnProperty.call(this.attributes, 'placeholder') ? this.attributes.placeholder : ''; }
    set placeholder(v) { this.attributes.placeholder = String(v); }
    get maxLength() { return Object.prototype.hasOwnProperty.call(this.attributes, 'maxlength') ? Number(this.attributes.maxlength) : -1; }
    set maxLength(v) { this.attributes.maxlength = String(v); }
    get textContent() { return this._text + this.children.map((c) => c.textContent).join(''); }
    set textContent(v) { this.children = []; this._text = String(v); }
    set innerHTML(v) { this.env.innerHTMLWrites.push(String(v)); this.children = []; this._text = String(v); }
    get innerHTML() { return this._text; }
    get firstChild() { return this.children[0] || null; }
    getBoundingClientRect() { return { top: 0, bottom: 100, left: 0, right: 100, width: 100, height: 100 }; }
    appendChild(c) { c.parent = this; this.children.push(c); return c; }
    insertBefore(c, ref) { const at = this.children.indexOf(ref); c.parent = this; if (at === -1) this.children.push(c); else this.children.splice(at, 0, c); return c; }
    removeChild(c) { this.children = this.children.filter((x) => x !== c); c.parent = null; return c; }
    replaceChild(fresh, old) {
        const at = this.children.indexOf(old);
        if (at !== -1) this.children[at] = fresh;
        fresh.parent = this; old.parent = null;
        if (this.env.elements[old.id] === old) this.env.elements[old.id] = fresh;     // (what the page asks for by that id is the new node)
        return old;
    }
    replaceWith(fresh) { if (this.parent) this.parent.replaceChild(fresh, this); }
    cloneNode(deep) {
        const copy = new El(this.env, this.tagName, this.id);
        copy.attributes = Object.assign({}, this.attributes); copy.className = this.className; copy.hidden = this.hidden; copy.value = this.value; copy._checked = this._checked; copy._text = this._text; copy.readOnly = this.readOnly; copy.disabled = this.disabled;
        if (deep) for (const c of this.children) copy.appendChild(c.cloneNode ? c.cloneNode(true) : Object.assign({}, c, { parent: null }));
        return copy;
    }
    setAttribute(k, v) { this.attributes[k] = String(v); if (k === 'class') this.className = String(v); }
    getAttribute(k) { return Object.prototype.hasOwnProperty.call(this.attributes, k) ? this.attributes[k] : null; }
    removeAttribute(k) { delete this.attributes[k]; }
    hasAttribute(k) { return Object.prototype.hasOwnProperty.call(this.attributes, k); }
    addEventListener(t, fn) { (this.listeners[t] = this.listeners[t] || []).push(fn); }
    fire(t, ev) { (this.listeners[t] || []).forEach((fn) => fn(ev || {})); }
    click() { this.fire('click', { target: this, detail: 1 }); }
    key(k) { this.fire('keydown', { key: k, preventDefault() {} }); }
    focus() { this.env.focused = this; this.env.doc.activeElement = this; }
    blur() { if (this.env.doc.activeElement === this) this.env.doc.activeElement = null; this.fire('blur', {}); }
    select() {}
    contains(o) { for (let n = o; n; n = n.parent) if (n === this) return true; return false; }
    get classList() {
        const el = this;
        const has = () => new Set(String(el.className).split(/\s+/).filter(Boolean));
        return {
            add(c) { const s = has(); s.add(c); el.className = [...s].join(' '); },
            remove(c) { const s = has(); s.delete(c); el.className = [...s].join(' '); },
            toggle(c, force) { const s = has(); const on = force === undefined ? !s.has(c) : !!force; if (on) s.add(c); else s.delete(c); el.className = [...s].join(' '); return on; },
            contains(c) { return has().has(c); },
        };
    }
    all() { return [this].concat(...this.children.map((c) => (c.all ? c.all() : []))); }
    findAll(pred) { return this.all().filter((e) => e.tagName && pred(e)); }
    // a selector of this fake: a list of simple selectors (a tag, .class, [attribute], [attribute="value"], in any mix)
    matches(selector) {
        return selector.split(',').some((part) => {
            const m = /^\s*([a-z0-9]*)((?:\.[\w-]+|\[[\w-]+(?:="[^"]*")?\])*)\s*$/i.exec(part);
            if (!m) throw new Error('the fake DOM does not know the selector ' + selector);
            if (m[1] && this.tagName !== m[1].toLowerCase()) return false;
            for (const q of m[2].match(/\.[\w-]+|\[[\w-]+(?:="[^"]*")?\]/g) || []) {
                if (q[0] === '.') { if (String(this.className).split(/\s+/).indexOf(q.slice(1)) === -1) return false; }
                else {
                    const a = /^\[([\w-]+)(?:="([^"]*)")?\]$/.exec(q);
                    if (!this.hasAttribute(a[1]) || (a[2] !== undefined && this.getAttribute(a[1]) !== a[2])) return false;
                }
            }
            return true;
        });
    }
    closest(selector) { for (let n = this; n && n.matches; n = n.parent) if (n.matches(selector)) return n; return null; }
    querySelector(selector) { return this.all().find((e) => e !== this && e.tagName && e.matches(selector)) || null; }
}

const THROWS = 'THROWS';
// One page: the markup of the page and its script, run in a fake browser. search is the address's query; stored is what local storage holds (THROWS: a browser that refuses its storage);
// options: session (what this tab's session storage holds), noSession, clipboard (false: none), copyResult, share, fetch, noFetch, ua, random (what Math.random() says: 0.5), noWebSocket.
// env.sockets are the sockets that the page opened (env.last() the newest); env.arrive() opens the newest and plays a server's Welcome and Room.
function runLobby(search, stored, options) {
    options = options || {};
    const env = { innerHTMLWrites: [], assigned: [], replaced: [], scrolled: [], opened: [], copied: [], commanded: [], shared: [], elements: {}, focused: null, confirms: [], reloads: 0, sockets: [], timers: [], clock: 0, nextTimer: 1 };
    env.root = new El(env, 'html', '');
    for (const m of lobbyMarkup.matchAll(/<(\w+)([^>]*)>/g)) {
        const idm = /\bid="([^"]+)"/.exec(m[2]);
        if (!idm) continue;
        const el = new El(env, m[1], idm[1]);
        el.hidden = /\bhidden\b/.test(m[2].replace(/aria-hidden/g, '').replace(/="[^"]*"/g, ''));       // (the boolean attribute, not a word inside a value)
        el._checked = /\bchecked\b/.test(m[2].replace(/="[^"]*"/g, ''));                                      // (the same for a radio button that is checked in the markup)
        for (const a of m[2].matchAll(/([\w-]+)="([^"]*)"/g)) { el.attributes[a[1]] = a[2]; if (a[1] === 'class') el.className = a[2]; }
        if (m[1] === 'input' && el.attributes.value !== undefined) el.value = el.attributes.value;
        el.parent = env.root;
        env.elements[idm[1]] = el;
    }
    // the markup's own text (the buttons say Join, Rejoin it, START!): what stands between a tag with an id and the next tag
    for (const m of lobbyMarkup.matchAll(/<(\w+)[^>]*\bid="([^"]+)"[^>]*>([^<]*)</g)) env.elements[m[2]]._text = m[3].replace(/&rsquo;/g, '’').replace(/&amp;/g, '&').replace(/&#8249;/g, '‹').replace(/&#8250;/g, '›');
    const doc = {
        body: new El(env, 'body', ''),
        getElementById(id) { if (!env.elements[id]) throw new Error('the page asked for #' + id + ', which its markup does not have'); return env.elements[id]; },
        createElement(tag) { return new El(env, tag, ''); },
        createTextNode(text) { return { isText: true, textContent: String(text), parent: null }; },
        activeElement: null,
        elementFromPoint() { return null; },
        execCommand(cmd) {                                // the copy command copies what the last field of the page holds (options.copyResult: false is a browser that did not copy, 'throws' one that refuses)
            const field = doc.body.children[doc.body.children.length - 1];
            env.commanded.push({ cmd, value: field ? field.value : null });
            if (options.copyResult === 'throws') throw new Error('not allowed');
            return options.copyResult !== false;
        },
        hidden: false, listeners: {},
        addEventListener(t, fn) { (doc.listeners[t] = doc.listeners[t] || []).push(fn); },
        removeEventListener() {},
    };
    env.doc = doc;
    const data = stored === THROWS ? {} : Object.assign({}, stored);
    const storage = {
        data,
        getItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); return Object.prototype.hasOwnProperty.call(data, k) ? data[k] : null; },
        setItem(k, v) { if (stored === THROWS) throw new Error('storage is blocked'); data[k] = String(v); },
        get length() { if (stored === THROWS) throw new Error('storage is blocked'); return Object.keys(data).length; },
        key(i) { if (stored === THROWS) throw new Error('storage is blocked'); const names = Object.keys(data); return i < names.length ? names[i] : null; },
        removeItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); delete data[k]; },
    };
    // the tab's own storage (the lobby keeps its room and its key there): options.session is what it holds, options.noSession is a browser that has none; where the other storage throws this one does too
    const sessionData = stored === THROWS ? {} : Object.assign({}, options.session);
    const session = {
        data: sessionData,
        getItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); return Object.prototype.hasOwnProperty.call(sessionData, k) ? sessionData[k] : null; },
        setItem(k, v) { if (stored === THROWS) throw new Error('storage is blocked'); sessionData[k] = String(v); },
        removeItem(k) { if (stored === THROWS) throw new Error('storage is blocked'); delete sessionData[k]; },
    };
    // the tab's history: the entries that the page makes (pushState, replaceState) and the moves of Back and Forward (popstate), with the address that each entry names
    const entries = [{ state: null, url: '/' + search + (options.hash || '') }];
    let at = 0;
    const goTo = (url) => { const u = new URL(url, 'https://play.test/'); win.location.pathname = u.pathname; win.location.search = u.search; win.location.hash = u.hash; win.location.href = u.href; };
    env.pushed = [];
    env.marked = [];                                // (replaceState with the address that the entry has: a mark on the entry, not a rewrite of the address, which is what env.replaced holds)
    const history = {
        get state() { return entries[at].state; },
        get length() { return entries.length; },
        replaceState(...a) { if (a[2] !== undefined && a[2] !== entries[at].url) env.replaced.push(a); else env.marked.push(a); entries[at] = { state: a[0], url: a[2] === undefined ? entries[at].url : a[2] }; if (a[2] !== undefined) goTo(a[2]); },
        pushState(...a) { env.pushed.push(a); entries.splice(at + 1); entries.push({ state: a[0], url: a[2] === undefined ? entries[at].url : a[2] }); at++; if (a[2] !== undefined) goTo(a[2]); },
    };
    env.entries = entries;
    env.move = (step) => { const to = at + step; if (to < 0 || to >= entries.length) return false; at = to; goTo(entries[at].url); (win.listeners.popstate || []).forEach((fn) => fn({ state: entries[at].state })); return true; };
    env.back = () => env.move(-1);
    env.forward = () => env.move(1);
    const win = {
        location: { search, hash: options.hash || '', href: 'https://play.test/' + search + (options.hash || ''), pathname: '/', origin: 'https://play.test', protocol: 'https:', host: 'play.test', assign(u) { env.assigned.push(u); }, reload() { env.reloads++; } },
        localStorage: storage, history,
        crypto: nodeCrypto.webcrypto,
        listeners: {},
        addEventListener(t, fn) { (win.listeners[t] = win.listeners[t] || []).push(fn); },
        removeEventListener() {},
        scrollTo(x, y) { env.scrolled.push([x, y]); },
        open(url, name, features) { env.opened.push({ url, name, features }); return { closed: false, focus() {} }; },
        confirm(q) { env.confirms.push(q); return true; },
    };
    if (!options.noSession) win.sessionStorage = session;
    // a clock that stands still until env.advance(ms): the page's toasts and the client's waits
    const setTimer = (fn, ms) => { const id = env.nextTimer++; env.timers.push({ id, at: env.clock + ms, fn }); return id; };
    const clearTimer = (id) => { env.timers = env.timers.filter((t) => t.id !== id); };
    env.advance = (ms) => {
        const until = env.clock + ms;
        for (;;) {
            const due = env.timers.filter((t) => t.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            env.timers = env.timers.filter((t) => t !== due);
            env.clock = Math.max(env.clock, due.at);
            due.fn();
        }
        env.clock = until;
    };
    class FakeSocket {
        constructor(url) { this.url = url; this.sent = []; this.closed = false; this.readyState = 0; env.sockets.push(this); }
        send(bytes) { if (this.closed || this.readyState !== 1) throw new Error('send on a socket that is not open'); this.sent.push(Uint8Array.from(bytes)); }
        close() { this.closed = true; }
        open() { this.readyState = 1; if (this.onopen) this.onopen(); return this; }
        receive(bytes) { if (this.onmessage) this.onmessage({ data: bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) }); return this; }
        drop() { this.closed = true; if (this.onclose) this.onclose({ code: 1006 }); }
    }
    const scope = options.realTime ? { WebSocket: options.WebSocket, setTimeout: (fn, ms) => setTimeout(fn, ms), clearTimeout: (id) => clearTimeout(id) }
        : { WebSocket: options.noWebSocket ? undefined : options.WebSocket || FakeSocket, setTimeout: setTimer, clearTimeout: clearTimer };
    win.AntsLobbyRules = loadFront('lobby_rules.js', scope).AntsLobbyRules;
    win.AntsLobbyNet = loadFront('lobby_net.js', scope).AntsLobbyNet;
    win.AntsLobbyChat = loadFront('lobby_chat.js', scope).AntsLobbyChat;
    win.AntsRematch = loadFront('rematch.js', scope).AntsRematch;
    win.AntsGameModes = loadFront('game_modes.js', scope).AntsGameModes;
    win.AntsGameType = loadFront('game_type.js', scope).AntsGameType;
    if (!options.noHome) win.AntsHome = loadFile('home/home.js', scope).AntsHome;
    const nav = { userAgent: options.ua || 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36', platform: 'Linux x86_64' };
    if (options.clipboard !== false) nav.clipboard = { writeText(t) { env.copied.push(t); return { then() {} }; } };       // (options.clipboard: false is a browser without the clipboard's own API: the copy command is used)
    // options.share: a browser that has navigator.share (true: it takes the data; 'throws': it throws; 'rejects': the person closes the sheet, a promise that fails)
    if (options.share === true) nav.share = (d) => { env.shared.push(d); return Promise.resolve(); };
    if (options.share === 'throws') nav.share = () => { throw new Error('not allowed'); };
    if (options.share === 'rejects') nav.share = (d) => { env.shared.push(d); return Promise.reject(new Error('AbortError')); };
    // the site's answers (options.fetch(url, init) -> a promise of a response; the default is no network at all; options.noFetch is a browser that has no fetch)
    env.fetches = [];
    const network = options.fetch || (() => Promise.reject(new Error('the test has no network')));
    const fetchSpy = options.noFetch ? undefined : (url, init) => { env.fetches.push({ url, init }); return network(url, init); };
    const fakeMath = Object.assign(Object.create(Math), { random: () => (options.random === undefined ? 0.5 : options.random) });     // (the page's picked name, and the order of its random names, are the same on every run)
    new Function('window', 'document', 'history', 'navigator', 'setInterval', 'setTimeout', 'clearTimeout', 'fetch', 'BroadcastChannel', 'Math', lobbyScript)(win, doc, history, nav, () => 0, scope.setTimeout, scope.clearTimeout, fetchSpy, undefined, fakeMath);
    env.storage = storage;
    env.session = session;
    env.win = win;
    env.body = doc.body;
    env.$ = (id) => env.elements[id];
    env.type = (id, text) => { const el = env.elements[id]; el.value = text; el.fire('input', {}); };
    env.param = (url, key) => new URL(url).searchParams.get(key);
    env.last = () => env.sockets[env.sockets.length - 1];
    // The newest socket opens and the server welcomes it as seat `seat` and tells it the room: seats [[state, name] x 4], o as roomMessage takes it (you: the seat, leader: the first)
    env.arrive = (o) => {
        o = o || {};
        const seat = o.seat === undefined ? 0 : o.seat;
        const s = env.last().open();
        s.receive(welcome(seat, o.key, o.flags));
        s.receive(roomMessage(o.seats || [[C, 'Maya'], [E, ''], [E, ''], [E, '']], Object.assign({ you: seat, leader: 0 }, o.room || {})));
        return s;
    };
    // a person presses a button of the page by its id; the card of a color by its seat; the buttons inside it
    env.card = (seat) => env.$('slots').children.find((li) => li.getAttribute('data-i') === String(seat));
    env.inCard = (seat, attr) => env.card(seat).findAll((e) => e.hasAttribute(attr))[0] || null;
    env.press = (el) => env.$('slots').fire('click', { target: el, detail: 1 });
    env.pagePlays = () => env.assigned.length > 0;
    // Home: the buttons that home.js makes (it builds its own markup), by their class
    env.home = (cls) => env.$('home').findAll((e) => String(e.className).split(/\s+/).indexOf(cls) !== -1)[0] || null;
    env.roomBar = (cls) => env.root.findAll((e) => String(e.className).split(/\s+/).indexOf('rbar') !== -1)[0].findAll((e) => String(e.className).split(/\s+/).indexOf(cls) !== -1)[0] || null;
    env.screen = () => env.body.getAttribute('data-screen');
    env.dumpAll = () => [...Object.values(env.elements), ...env.root.children, env.body].flatMap((e) => e.all()).map((e) => e.textContent + '|' + JSON.stringify(e.attributes) + '|' + e.value).join('\n') + env.innerHTMLWrites.join('') + JSON.stringify(env.replaced) + JSON.stringify(env.assigned);
    if (!options.stayHome && env.screen() === 'home' && env.sockets.length === 0) env.home('mine').click();       // (a plain visit is Home and makes no room: the tests that need one press My room, as a person does)
    return env;
}

return { lobbyText, lobbyDir, lobbyMarkup, lobbyScripts, lobbyScript, loadFile, loadFront, RULES0, NET0, CHAT0, SUGGESTION, hex, E, C, str8, KEY, welcome, reject, roomMessage, helloBytes, helloName, helloRoom,
         CODE, grouped, BLOCK_PARAMS, blockText, noBlockOf, El, THROWS, runLobby };
};
