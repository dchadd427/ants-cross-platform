// The rig of the checks of the public room and the Games list (tests/scripts/web_home_games_check.js and web_home_games_room_check.js): the fake browser of tests/scripts/web_page_harness.js with its fake site (GET /lobbies), the
// time that goes by in steps, and the selectors of what web/home/games.js makes. One rig per check: rig(process.argv[2]) is web/lobby.html.
'use strict';
const fs = require('fs');
const path = require('path');
globalThis.AntsShapes = require(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

module.exports = function rig(lobbyPath) {
if (!lobbyPath) { console.log('usage: web_home_games_check.js lobby.html'); process.exit(2); }
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const Games = require(path.join(lobbyDir, 'home', 'games.js'));
const H = require('./web_page_harness.js')(lobbyPath);
const { NET0, KEY, E, C, hex, roomMessage, welcome, reject, helloBytes, helloName, helloRoom, grouped, runLobby, RULES0 } = H;

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) {
    const a = JSON.stringify(got);
    const b = JSON.stringify(want);
    check(label, a === b, 'got ' + a + ', wanted ' + b);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the fake site and the helpers
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const flush = async () => { for (let i = 0; i < 16; i++) await Promise.resolve(); };      // (the promises of one look settle)
// A fake GET /lobbies: `body` is the answer (an object, or `text` as it is), `status`; reject / throws / hang make it fail; times says at which moments of the clock it was asked
function fakeSite() {
    const n = { calls: [], times: [], reads: 0, length: undefined, status: 200, body: { format: 1, lobbies: [], count: 0 }, text: null, reject: false, throws: false, hang: false, clock: () => 0 };
    n.fetch = (url, init) => {
        if (url !== '/lobbies') return Promise.reject(new Error('this site has no ' + url + ' here'));
        n.calls.push({ url, init }); n.times.push(n.clock());
        if (n.throws) throw new Error('fetch throws');
        if (n.hang) return new Promise(() => {});
        if (n.reject) return Promise.reject(new Error('the network is down'));
        return Promise.resolve({ status: n.status, headers: { get: (k) => (n.length !== undefined && String(k).toLowerCase() === 'content-length' ? String(n.length) : null) }, text: () => { n.reads++; return Promise.resolve(n.text !== null ? n.text : JSON.stringify(n.body)); } });
    };
    return n;
}
const row = (code, host, o) => Object.assign({ code, host, map: 'TREASURE.LVL', mode: 'highest-score', people: 2, bots: 0, free: 2, size: 4, age: 10 }, o || {});
const listOf = (rows) => ({ format: 1, lobbies: rows, count: rows.length });
// One page on Home with its fake site; the page's lazy load runs (env.advance(0)) unless options.lazy says no; env.site is the site, env.looks() how many looks at the list there were
async function page(o) {
    o = o || {};
    const site = o.site || fakeSite();
    const env = runLobby(o.search || '', o.stored || { 'ants.name': 'Maya' }, Object.assign({ stayHome: true, fakeClock: true, lazy: true, fetch: site.fetch }, o.options || {}));
    site.clock = () => env.clock;
    env.site = site;
    env.looks = () => site.calls.length;
    env.advance(0);
    await flush();
    return env;
}
// time goes by in steps of at most 500 ms, with the promises of a look settled after each (so that the next wait starts where the look ended)
// (the server of a room pings every second: a socket that heard nothing for 8 s is given up by the client, so every open socket is pinged after each step)
const PING = Uint8Array.from([NET0.MSG.Ping, 0, 0, 0, 1, 0, 0, 0, 0]);
async function wait(env, ms) {
    for (let left = ms; left > 0; left -= 500) {
        env.advance(Math.min(500, left));
        for (const s of env.sockets) if (s.readyState === 1 && !s.closed) s.receive(PING);
        await flush();
    }
}
const classes = (e) => String(e.className).split(/\s+/);
const find = (root, cls) => root.findAll((e) => classes(e).indexOf(cls) !== -1);
const first = (root, cls) => find(root, cls)[0] || null;
const home = (env, cls) => first(env.$('home'), cls);
const tap = (b) => { if (b && !b.disabled) b.click(); };                              // (a disabled button takes no press, as in a browser)
const pressEscape = (env) => (env.doc.listeners.keydown || []).forEach((fn) => fn({ key: 'Escape' }));
const pressElsewhere = (env, target) => (env.doc.capturing.click || []).concat(env.doc.listeners.click || []).forEach((fn) => fn({ target }));
const popOf = (env) => home(env, 'gpop');
const rowsOf = (env) => find(popOf(env), 'gitem').filter((e) => e.tagName === 'li');
const joinOf = (li) => li.findAll((e) => e.tagName === 'button')[0];
const textOf = (e) => e.textContent;
const stripOf = (env) => first(env.root, 'gstrip');
const stripText = (env) => (stripOf(env).hidden ? '' : stripOf(env).children[0].textContent);
const stripButtons = (env) => stripOf(env).children[1].children;
const pillOf = (env) => env.home('games').children[1];
const dialogOf = (env) => first(env.body, 'gconfirm');       // (the card is the body's: it is over both screens)
const visibilities = (sock) => sock.sent.filter((b) => b[0] === 35).map((b) => b[1]);
const leaves = (sock) => sock.sent.filter((b) => b[0] === NET0.MSG.Leave).length;
const seatsOf = (names) => [[C, names[0]], [names[1] ? C : E, names[1] || ''], [names[2] ? C : E, names[2] || ''], [names[3] ? C : E, names[3] || '']];
const PUBLIC = 11, PRIVATE = 3, STARTING = 15;                                         // the Room's flags: lobby + the leader starts (3), + public (8), + START waits (4)
// the page makes its room (My room), the server welcomes it: the socket of the room, with its seats as the server says them
// (zero: a seat with no key, as the server gives a seat that the game page does not take over: START then makes no hand-off, so the page stays and its Room can be looked at while START waits)
function myRoom(env, names, flags, o, zero) {
    env.home('mine').click();
    const sock = env.arrive(Object.assign({ key: KEY, seats: seatsOf(names || ['Maya']), room: Object.assign({ flags: flags === undefined ? PRIVATE : flags }, o || {}) }, zero ? { key: new Uint8Array(16), flags: 0 } : {}));
    return sock;
}
// the page is a guest in the room of somebody else (by a link, as it would): the socket, with the host's seat 0 and this page's seat 1
function guestRoom(env, host, flags) {
    env.$('name-step-go').click();
    return env.arrive({ seat: 1, seats: seatsOf([host, 'Maya']), room: { you: 1, leader: 0, flags: flags === undefined ? PRIVATE : flags } });
}
const roomAs = (names, me, flags, o) => roomMessage(seatsOf(names), Object.assign({ you: me, leader: 0, flags }, o || {}));


const done = (title) => { console.log(title + ': ' + checks + ' checks, ' + failures + ' failed'); process.exit(failures === 0 ? 0 : 1); };
const broke = (e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); };
return { check, same, flush, fakeSite, row, listOf, page, wait, classes, find, first, home, tap, pressEscape, pressElsewhere, popOf, rowsOf, joinOf, textOf, stripOf, stripText, stripButtons, pillOf, dialogOf, visibilities, leaves, seatsOf,
         PUBLIC, PRIVATE, STARTING, myRoom, guestRoom, roomAs, Games, H, NET0, KEY, E, C, hex, roomMessage, welcome, reject, helloBytes, helloName, helloRoom, grouped, runLobby, RULES0, lobbyDir, done, broke };
};
