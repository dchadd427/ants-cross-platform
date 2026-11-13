// A fake of the site's chat hub (docs/HUB.md) on a real socket, for the checks of the chat dock (tests/scripts/web_home_chat_hub_check.js, tests/scripts/web_home_chat_browser.js). It speaks the hub's protocol, version 1,
// at /hub/ws: down welcome (you, login, online, history), line, del, online, err; up say and ping; and it keeps the hub's own pace for a writer (a burst of 3, then one line every 2 seconds, the same text twice
// within 30 seconds refused), so that a page that sends faster than it would is seen. It is also the hub's bad days on demand: a socket that says nothing, frames that are not JSON, a frame of megabytes, a
// welcome that comes in pieces, an upgrade answered 503 with the JSON of a missing hub (what nginx says) or 404 (a site that has no such door), a socket that is cut. Plain node, no packages.
// The two names (docs/HUB.md "Names"): a `you` with `k` (the key of the account), `pick` ("display" or "username") and `other` (the name that is not shown) is an account that can choose; its lines carry its key,
// and a {"t":"pick","pick":...} is answered as the real hub does: the names change places, every socket is closed (1000) and the next welcome says who the account is now; where the account cannot choose it is
// {"t":"err","code":"pick"}. `line(id, who, text, mod, extra)` takes more fields (k, u).
// The roles (docs/HUB.md "Roles", "Moderation"): `you.role` is none, mod or admin (`mod` is true for both); `hub.you(you)` changes it and tells every socket with a `you` frame; a moderator's command in a `say`
// is read as the hub reads it (the seven words, any case; a role of none gets `bad` and nothing is posted; a role that may not gets `power`; a name that is nobody's in `people` ({ name: role }) gets `who`; a
// person of the same or a higher rank, or oneself, gets `power`; `o.roleDown` makes every command `role`; a done one is answered `ok`, a kick and a ban also make the hub's own line `<name> was kicked.` with
// `sys: true`, a line of a moderator or an administrator carries `role`, and /bans answers the private `bans` frame from `o.bans`); `hub.bye(why)` sends the `bye` frame and closes the sockets (a kick also ends the login: the next welcome names nobody).
//   const hub = await startHub({ you: { name: 'maple.leaf', mod: false }, login: true, history: [line(1, 'pebble', 'hello')] });  hub.url; hub.say('pebble', 'hi'); hub.drop(); await hub.close();
'use strict';
const http = require('http');
const crypto = require('crypto');

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const line = (id, who, text, mod, extra) => Object.assign({ id, at: 1700000000 + id, who, text, mod: !!mod }, extra);

function frame(opcode, payload) {                       // a server's frame: whole, not masked
    const n = payload.length;
    const head = n < 126 ? Buffer.from([128 | opcode, n]) : n < 65536 ? Buffer.from([128 | opcode, 126, n >> 8, n & 255]) : Buffer.concat([Buffer.from([128 | opcode, 127]), Buffer.alloc(8)]);
    if (n >= 65536) head.writeBigUInt64BE(BigInt(n), 2);
    return Buffer.concat([head, payload]);
}

function startHub(options) {
    const o = Object.assign({ you: null, login: false, history: [], online: 3, mode: 'normal', path: '/hub/ws' }, options);
    const hub = { peers: [], received: [], upgrades: 0, nextId: 100, welcomes: 0, options: o };
    const server = o.server || http.createServer((req, res) => { res.writeHead(404, { 'Content-Type': 'text/plain' }); res.end('not here'); });       // (o.server: the upgrades of a server that is there already, a page's own, are taken: the page and the hub share one address)
    const writers = new Map();                          // peer -> { tokens, at, said }
    const welcome = () => Object.assign({ t: 'welcome', v: 1, you: o.you, login: o.login, online: o.online, history: o.history }, o.welcomeExtra);       // (o.welcomeExtra: more of the welcome, for example { presence: true, me: 3 })

    server.on('upgrade', (req, socket) => {
        hub.upgrades++;
        if (req.url !== o.path) { socket.end('HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nno'); return; }
        if (o.mode === 'off') { socket.end('HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{"error":"hub_off","message":"Chat is not available right now."}'); return; }
        const key = req.headers['sec-websocket-key'];
        socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + crypto.createHash('sha1').update(key + GUID).digest('base64') + '\r\n\r\n');
        const peer = { socket, buffer: Buffer.alloc(0), open: true, origin: req.headers.origin || null };
        hub.peers.push(peer);
        writers.set(peer, { tokens: 3, at: Date.now(), said: new Map() });
        socket.on('data', (data) => { peer.buffer = Buffer.concat([peer.buffer, data]); read(peer); });
        socket.on('close', () => { peer.open = false; hub.peers = hub.peers.filter((p) => p !== peer); writers.delete(peer); });
        socket.on('error', () => {});
        if (o.mode === 'normal') { hub.welcomes++; hub.send(peer, welcome()); }
        else if (o.mode === 'pieces') { hub.welcomes++; sendPieces(peer, Buffer.from(JSON.stringify(welcome()))); }
        // mode 'silent': nothing is ever said; any other mode ('garbage', ...) is up to the test: it says what it wants with hub.send and hub.raw
    });
    function sendPieces(peer, text) {                   // one text message in several frames of 700 bytes (the first has the opcode 1, the rest 0, the last has FIN)
        let at = 0;
        const next = () => {
            if (!peer.open || at >= text.length) return;
            const part = text.slice(at, at + 700); const last = at + 700 >= text.length; const head = Buffer.from([(last ? 128 : 0) | (at === 0 ? 1 : 0), part.length < 126 ? part.length : 126]);
            peer.socket.write(Buffer.concat([head, part.length < 126 ? Buffer.alloc(0) : Buffer.from([part.length >> 8, part.length & 255]), part]));
            at += 700; setTimeout(next, 15);
        };
        next();
    }
    function read(peer) {                               // the client's frames: masked, whole, small
        for (;;) {
            const b = peer.buffer;
            if (b.length < 2) return;
            let n = b[1] & 127, at = 2;
            if (n === 126) { if (b.length < 4) return; n = b.readUInt16BE(2); at = 4; } else if (n === 127) { if (b.length < 10) return; n = Number(b.readBigUInt64BE(2)); at = 10; }
            if (b.length < at + 4 + n) return;
            const mask = b.slice(at, at + 4), payload = Buffer.from(b.slice(at + 4, at + 4 + n));
            for (let i = 0; i < n; i++) payload[i] ^= mask[i & 3];
            peer.buffer = b.slice(at + 4 + n);
            const opcode = b[0] & 15;
            if (opcode === 8) { peer.socket.end(frame(8, payload.slice(0, 2))); return; }
            if (opcode === 9) { peer.socket.write(frame(10, payload)); continue; }
            if (opcode === 1) onText(peer, payload.toString('utf8'));
        }
    }
    function onText(peer, text) {
        let m;
        try { m = JSON.parse(text); } catch (e) { hub.send(peer, { t: 'err', code: 'bad' }); return; }
        hub.received.push(m);
        if (m.t === 'ping') { hub.send(peer, { t: 'online', n: o.online }); return; }
        if (m.t === 'who' || m.t === 'here') { if (m.t === 'who' && o.who) hub.send(peer, o.who); return; }       // (the players list: o.who is the frame that answers a `who`; a `here` is taken and not answered)
        if (m.t === 'pick') {
            const y = o.you;
            if (!y || !y.other || (m.pick !== 'display' && m.pick !== 'username')) { hub.send(peer, { t: 'err', code: 'pick' }); return; }
            if (m.pick !== y.pick) o.you = Object.assign({}, y, { name: y.other, other: y.name, pick: m.pick });
            hub.closeAll(1000);
            return;
        }
        if (m.t !== 'say') { hub.send(peer, { t: 'err', code: 'bad' }); return; }
        if (!o.you) { hub.send(peer, { t: 'err', code: 'login' }); return; }
        if (typeof m.text === 'string' && /^\/(mute|unmute|del|kick|ban|unban|bans)(\s|$)/i.test(m.text.trim())) { command(peer, m.text.trim()); return; }
        const w = writers.get(peer), now = Date.now();
        w.tokens = Math.min(3, w.tokens + (now - w.at) / 2000); w.at = now;
        for (const [t, at] of w.said) if (now - at >= 30000) w.said.delete(t);
        if (typeof m.text !== 'string' || !m.text.trim()) { hub.send(peer, { t: 'err', code: 'empty' }); return; }
        if ([...m.text].length > 200) { hub.send(peer, { t: 'err', code: 'long' }); return; }
        if (w.said.has(m.text) || w.tokens < 1) { hub.rateRefusals = (hub.rateRefusals || 0) + 1; hub.send(peer, { t: 'err', code: 'rate' }); return; }
        w.tokens -= 1; w.said.set(m.text, now);
        hub.say(o.you.name, m.text, o.you.mod, Object.assign(o.you.k ? { k: o.you.k } : {}, o.you.role === 'mod' || o.you.role === 'admin' ? { role: o.you.role } : {}));       // (a line says the role that its author has, as the hub's does)
    }
    const RANK = { none: 0, mod: 1, admin: 2 }, NEEDS = { mute: 1, unmute: 1, del: 1, kick: 1, ban: 2, unban: 2, bans: 2 }, ARGS = { mute: 2, unmute: 1, del: 1, kick: 1, ban: 1, unban: 1, bans: 0 };
    function command(peer, text) {                      // a moderator's command, as the hub answers it
        const parts = text.split(/\s+/), word = parts[0].slice(1).toLowerCase(), mine = o.you.role || (o.you.mod ? 'mod' : 'none');
        const err = (code) => hub.send(peer, { t: 'err', code });
        hub.commands.push(text);
        if (mine === 'none' || parts.length - 1 !== ARGS[word]) { err('bad'); return; }
        if (RANK[mine] < NEEDS[word]) { err('power'); return; }
        if (o.roleDown) { err('role'); return; }
        if (word === 'bans') { hub.send(peer, { t: 'bans', rows: o.bans || [] }); return; }
        if (word === 'del') { if (!/^\d+$/.test(parts[1])) { err('bad'); return; } hub.send(peer, { t: 'ok', c: 'del' }); return; }
        if (word === 'mute' && !/^\d+$/.test(parts[2])) { err('bad'); return; }
        const them = (o.people || {})[parts[1]], self = parts[1] === o.you.name || parts[1] === o.you.other;
        if (them === undefined && !self) { err('who'); return; }
        if (self || RANK[them] >= RANK[mine]) { err('power'); return; }
        hub.send(peer, { t: 'ok', c: word });
        if (word === 'kick' || word === 'ban') hub.say('system', parts[1] + ' was ' + (word === 'kick' ? 'kicked' : 'banned') + '.', false, { k: '', sys: true });
    }
    hub.commands = [];
    hub.you = (you) => { o.you = you; hub.broadcast({ t: 'you', you }); };
    hub.bye = (why) => { hub.broadcast({ t: 'bye', why }); if (why === 'kicked') o.you = null; hub.closeAll(1000); };
    hub.send = (peer, obj) => { if (peer.open) peer.socket.write(frame(1, Buffer.from(typeof obj === 'string' ? obj : JSON.stringify(obj)))); };
    hub.raw = (peer, buffer) => { if (peer.open) peer.socket.write(buffer); };
    hub.broadcast = (obj) => hub.peers.forEach((p) => hub.send(p, obj));
    hub.say = (who, text, mod, extra) => { const id = hub.nextId++; hub.broadcast(Object.assign({ t: 'line' }, line(id, who, text, mod, extra))); return id; };
    hub.del = (id) => hub.broadcast({ t: 'del', id });
    hub.setOnline = (n) => { o.online = n; hub.broadcast({ t: 'online', n }); };
    hub.drop = () => hub.peers.slice().forEach((p) => p.socket.destroy());            // every socket is cut, no close frame
    hub.closeAll = (code) => hub.peers.slice().forEach((p) => p.socket.end(frame(8, Buffer.from([code >> 8, code & 255]))));
    hub.set = (changes) => Object.assign(o, changes);
    hub.close = () => new Promise((resolve) => { hub.drop(); server.close(() => resolve()); });
    const ready = () => { hub.port = server.address().port; hub.url = 'ws://127.0.0.1:' + hub.port + o.path; hub.http = 'http://127.0.0.1:' + hub.port; return hub; };
    if (o.server) { hub.close = () => { hub.drop(); return Promise.resolve(); }; return Promise.resolve(ready()); }
    return new Promise((resolve, reject) => {
        server.on('error', reject);
        server.listen(0, '127.0.0.1', () => resolve(ready()));
    });
}

module.exports = { startHub, line, frame };
