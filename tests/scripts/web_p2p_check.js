// Runs the page's data channels (web/p2p.js, protocol 26, docs/P2P_HINTS.md) in node with a fake RTCPeerConnection, and checks the rule of the address and what the channels do:
//   * the rule of the address, for every pair of settings (this player allows direct or not, the other allows or not): only when BOTH allow is the connection made with every kind of candidate
//     (iceTransportPolicy "all", a STUN server); in every other case the policy is "relay", no STUN server is named, a candidate that is not a relay candidate is never sent and never accepted, a relay
//     candidate's related address is blanked on the way, and an SDP is sent with no candidate and no address in it;
//   * the browser's own choice is checked again: a channel of a pair that did not both allow direct whose chosen path is not the relay's is closed (and counted), a relay path is the mark "relay", a
//     direct one (both allowed) is "direct";
//   * the channels: the lower seat offers and the other answers, signals wait until the credential and the other player's readiness are known, a hint is sent three times and only on an open channel,
//     what arrives is handed up with the channel's seat and a message of one byte (the channel's own) never is, a channel that fails is tried again by the initiator a few times, a channel that says nothing is no mark;
//   * the echo (the round trip to each other player, rtt): a ping each second that the other page answers at once, the round trip of the answer to the last ping, the mean of the last five, nothing
//     for a ping or an answer that is lost, late, made up or twice, nothing after four seconds of silence, and nothing from a page of the old kind, which drops a message of one byte and never answers.
// tests/scripts/test_web_p2p.py runs this with node (the quick tier). usage: node web_p2p_check.js web/p2p.js        (exit 0: every check passed)
'use strict';

const path = require('path');
const vm = require('vm');
const fs = require('fs');

let failed = 0;
let checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
        failed++;
    }
}

const SCRIPT = path.resolve(process.argv[2] || path.join(__dirname, '..', '..', 'web', 'p2p.js'));
const sandbox = { console, setInterval, clearInterval, setTimeout, clearTimeout, Promise, Uint8Array, Map };
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(SCRIPT, 'utf8'), sandbox, { filename: SCRIPT });
const P = sandbox.AntsP2p;

const HOST = 'candidate:1 1 udp 2122260223 192.168.1.20 54321 typ host';
const SRFLX = 'candidate:2 1 udp 1686052607 198.51.100.4 54321 typ srflx raddr 192.168.1.20 rport 54321';
const RELAY = 'candidate:3 1 udp 41885439 203.0.113.9 49170 typ relay raddr 198.51.100.4 rport 54321';
const BLANKED = 'candidate:3 1 udp 41885439 203.0.113.9 49170 typ relay raddr 0.0.0.0 rport 0';
const SDP = 'v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\ns=-\r\nc=IN IP4 198.51.100.4\r\na=rtcp:9 IN IP4 198.51.100.4\r\na=candidate:1 1 udp 1 198.51.100.4 5 typ srflx\r\na=end-of-candidates\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n';
const ICE = { expires: 1700003600, username: '1700003600:ab', credential: 'c2VjcmV0', urls: ['stun:relay.example:3478', 'turn:relay.example:3478?transport=udp', 'turn:relay.example:3478?transport=tcp'] };

// ---- the rule, alone ----
const scrubbed = P.rules.scrubSdp(SDP);
expect('an SDP loses its candidates', /candidate|end-of-candidates/.test(scrubbed), false);
expect('an SDP loses its addresses', /198\.51|192\.168/.test(scrubbed), false);
expect('an SDP keeps what is not an address', scrubbed.indexOf('m=application 9 UDP/DTLS/SCTP webrtc-datachannel') >= 0 && /^v=0\r\n/.test(scrubbed) && /\r\n$/.test(scrubbed), true);
expect('the blank addresses stay blank', P.rules.scrubSdp('c=IN IP6 2001:db8::1\r\n').indexOf('c=IN IP6 ::') === 0, true);
expect('a relay candidate passes a pair that did not both allow direct, with its related address blanked', P.rules.passCandidate(false, '0|' + RELAY), '0|' + BLANKED);
expect('a host candidate does not', P.rules.passCandidate(false, '0|' + HOST), null);
expect('a srflx candidate does not', P.rules.passCandidate(false, '0|' + SRFLX), null);
expect('a prflx candidate does not', P.rules.passCandidate(false, '0|candidate:4 1 udp 1 1.2.3.4 5 typ prflx'), null);
expect('a candidate with no type does not', P.rules.passCandidate(false, '0|candidate:5 1 udp 1 1.2.3.4 5'), null);
expect('a word that only starts like relay does not', P.rules.passCandidate(false, '0|candidate:6 1 udp 1 1.2.3.4 5 typ relayed'), null);
expect('the end of the candidates always passes', [P.rules.passCandidate(false, '0|'), P.rules.passCandidate(true, '0|')], ['0|', '0|']);
expect('a malformed text does not', [P.rules.passCandidate(false, 'candidate'), P.rules.passCandidate(false, '|x'), P.rules.passCandidate(false, 'x|candidate:1 typ relay'), P.rules.passCandidate(true, 'x')], [null, null, null, null]);
expect('a pair that both allow direct passes every kind', [P.rules.passCandidate(true, '0|' + HOST), P.rules.passCandidate(true, '0|' + SRFLX)], ['0|' + HOST, '0|' + SRFLX]);
expect('a pair that both allow direct is given the STUN server, the others only the relay', [P.rules.serversFor(ICE, true).length, P.rules.serversFor(ICE, false).length, P.rules.serversFor(ICE, false)[0].urls.length, P.rules.serversFor(ICE, true)[0].urls], [2, 1, 2, ['stun:relay.example:3478']]);
expect('the relay is given its credential', P.rules.serversFor(ICE, false)[0].username + '/' + P.rules.serversFor(ICE, false)[0].credential, '1700003600:ab/c2VjcmV0');
expect('the setting is off unless it is 1', [P.directSetting({ getItem: () => '1' }), P.directSetting({ getItem: () => '0' }), P.directSetting({ getItem: () => null }), P.directSetting({ getItem: () => 'true' }), P.directSetting({ getItem: () => { throw new Error('blocked'); } })], [true, false, false, false, false]);
expect('the setting has the name that the room writes', P.rules.SETTING, 'ants.direct');

// ---- the channels, with a fake browser ----
class FakeDC {
    constructor() { this.sent = []; this.readyState = 'connecting'; this.closed = false; }
    send(b) { this.sent.push(b); }
    close() { this.closed = true; this.readyState = 'closed'; }
    open() { this.readyState = 'open'; if (this.onopen) this.onopen(); }
    receive(bytes) { if (this.onmessage) this.onmessage({ data: new Uint8Array(bytes).buffer }); }
}
class FakePC {
    constructor(cfg) { this.cfg = cfg; this.localDescription = null; this.remote = null; this.added = []; this.closed = false; this.connectionState = 'new'; this.chosen = 'relay'; FakePC.all.push(this); }
    createDataChannel(label, opts) { this.label = label; this.dcopts = opts; this.dc = new FakeDC(); return this.dc; }
    createOffer() { return Promise.resolve({ type: 'offer', sdp: SDP }); }
    createAnswer() { return Promise.resolve({ type: 'answer', sdp: SDP }); }
    setLocalDescription(d) { this.localDescription = { type: d.type, sdp: d.sdp }; return Promise.resolve(); }
    setRemoteDescription(d) { this.remote = d; return Promise.resolve(); }
    addIceCandidate(c) { this.added.push(c); return Promise.resolve(); }
    getStats() {
        const m = new Map();
        m.set('t', { id: 't', type: 'transport', selectedCandidatePairId: 'p' });
        m.set('p', { id: 'p', type: 'candidate-pair', localCandidateId: 'l' });
        m.set('l', { id: 'l', type: 'local-candidate', candidateType: this.chosen });
        return Promise.resolve(m);
    }
    close() { this.closed = true; }
}
FakePC.all = [];
const flush = () => new Promise((r) => setImmediate(r)).then(() => new Promise((r) => setImmediate(r))).then(() => new Promise((r) => setImmediate(r)));

function rig(me, mine, ready, directMask) {
    FakePC.all = [];
    const timers = [];
    const clock = { t: 1000 };
    const env = { RTCPeerConnection: FakePC, now: () => clock.t, setTimeout: (f, ms) => { timers.push({ f, ms, live: true }); return timers.length - 1; }, clearTimeout: (h) => { if (timers[h]) timers[h].live = false; } };
    const c = P.create(env);
    c.open(me, mine, ICE);
    c.peers(ready, directMask);
    return { c, timers, clock };
}
const drainSignals = (c) => { const out = []; let s; while ((s = c.takeSignal())) out.push(s); return out; };

(async () => {
    // the four pairs of settings: a pair is direct only when both allow it
    for (const mine of [false, true]) {
        for (const theirs of [false, true]) {
            const mask = (mine ? 1 : 0) | (theirs ? 2 : 0);
            const r = rig(0, mine, 0b11, mask);
            await flush();
            const pc = FakePC.all[0];
            const both = mine && theirs;
            const label = 'me ' + mine + ', the other ' + theirs + ': ';
            expect(label + 'one connection, to the other seat', FakePC.all.length, 1);
            expect(label + 'the policy is "all" only when both allow', pc.cfg.iceTransportPolicy, both ? 'all' : 'relay');
            expect(label + 'a STUN server only when both allow', pc.cfg.iceServers.some((s) => s.urls.some((u) => /^stun/.test(u))), both);
            expect(label + 'a channel that the game can count on: unordered, no retransmits, negotiated', pc.dcopts, { negotiated: true, id: 1, ordered: false, maxRetransmits: 0 });
            const offer = drainSignals(r.c)[0];
            expect(label + 'the offer is for the other seat and shows no address', [offer.to, offer.kind, /candidate|198\.51|192\.168/.test(offer.text)], [1, 1, false]);
            pc.onicecandidate({ candidate: { candidate: HOST, sdpMLineIndex: 0 } });
            pc.onicecandidate({ candidate: { candidate: SRFLX, sdpMLineIndex: 0 } });
            pc.onicecandidate({ candidate: { candidate: RELAY, sdpMLineIndex: 0 } });
            pc.onicecandidate({ candidate: null });
            const sent = drainSignals(r.c).map((s) => s.text);
            expect(label + 'what is sent', sent, both ? ['0|' + HOST, '0|' + SRFLX, '0|' + RELAY, '0|'] : ['0|' + BLANKED, '0|']);
            expect(label + 'the refusals are counted', r.c.stats().refusedOut, both ? 0 : 2);
            // the answer and what comes in
            r.c.signal(1, 2, SDP);
            await flush();
            expect(label + 'the answer is set without candidates or addresses', /candidate|198\.51/.test(pc.remote.sdp), false);
            r.c.signal(1, 3, '0|' + HOST);
            r.c.signal(1, 3, '0|' + RELAY);
            r.c.signal(1, 3, '0|');
            await flush();
            expect(label + 'what is accepted', pc.added.map((a) => a.candidate), both ? [HOST, RELAY] : [BLANKED]);
            expect(label + 'the refusals are counted', r.c.stats().refusedIn, both ? 0 : 1);
            r.c.close();
        }
    }

    // the browser's own choice
    {
        const r = rig(0, false, 0b11, 0);
        await flush();
        const pc = FakePC.all[0];
        pc.chosen = 'host';                                     // a path that is not the relay's for a pair that did not both allow direct
        pc.dc.open();
        await flush();
        expect('a path that is not the relay\'s is closed and counted', [pc.closed, r.c.stats().policyBreaks, r.c.link(1)], [true, 1, 0]);
        r.c.close();
    }
    {
        const r = rig(0, false, 0b11, 0);
        await flush();
        const pc = FakePC.all[0];
        pc.dc.open();
        await flush();
        expect('a relay path is the mark relay', r.c.link(1), 2);
        expect('a channel that is not up has the mark connecting, a seat with none has none', [r.c.link(2), r.c.link(3)], [0, 0]);
        r.clock.t += 5000;
        expect('a channel that has said nothing for a while is no mark', r.c.link(1), 1);
        pc.dc.receive([0]);                                      // the old heartbeat
        expect('the old heartbeat brings it back and is never given to the game', [r.c.link(1), r.c.takeHint()], [2, null]);
        r.c.close();
    }
    {
        const r = rig(0, true, 0b11, 0b11);
        await flush();
        const pc = FakePC.all[0];
        pc.chosen = 'srflx';
        pc.dc.open();
        await flush();
        expect('a direct path of a pair that both allowed is the mark direct', r.c.link(1), 3);
        pc.chosen = 'relay';
        r.c.close();
    }

    // who offers, who answers, and what waits
    {
        const low = rig(0, false, 0b11, 0);
        const high = rig(1, false, 0b11, 0);
        await flush();
        const answerer = high;
        FakePC.all = [];
        answerer.c.signal(0, 1, SDP);
        await flush();
        const reply = drainSignals(answerer.c)[0];
        expect('the higher seat answers', [FakePC.all.length, reply.to, reply.kind, /candidate|198\.51/.test(reply.text)], [1, 0, 2, false]);
        expect('an offer to the lower seat is ignored', (() => { const n = FakePC.all.length; low.c.signal(1, 1, SDP); return FakePC.all.length === n; })(), true);
        low.c.close();
        high.c.close();
    }
    {
        FakePC.all = [];
        const env = { RTCPeerConnection: FakePC, now: () => 1, setTimeout: () => 0, clearTimeout: () => {} };
        const c = P.create(env);
        c.signal(0, 1, SDP);                                    // before the credential and before the other seat is known ready
        c.peers(0b11, 0);
        await flush();
        expect('nothing is made before the credential', FakePC.all.length, 0);
        c.open(1, false, ICE);
        await flush();
        expect('a signal that waited is taken when the credential and the readiness are known', [FakePC.all.length, drainSignals(c).length], [1, 1]);
        c.peers(0b10, 0);                                       // the other seat is gone
        expect('a seat that is no longer ready loses its channel', FakePC.all[0].closed, true);
        c.close();
    }

    // hints
    {
        const r = rig(0, false, 0b111, 0);
        await flush();
        const [a, b] = FakePC.all;
        a.dc.open();
        const hint = new Uint8Array([36, 1, 0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]);
        r.c.sendHint(hint);
        expect('a hint is sent three times, on a channel that is open only', [a.dc.sent.length, b.dc.sent.length], [P.rules.COPIES, 0]);
        expect('three copies', P.rules.COPIES, 3);
        a.dc.readyState = 'closing';                            // a channel that was up and is on its way down
        r.c.sendHint(hint);
        expect('a channel that is closing is sent nothing', a.dc.sent.length, P.rules.COPIES);
        a.dc.readyState = 'open';
        a.dc.receive(Array.from(hint));
        const got = r.c.takeHint();
        expect('what arrives is handed up with the seat of the channel', [got.from, Array.from(got.bytes)], [1, Array.from(hint)]);
        expect('a message of one byte is the channel\'s own, not a hint', (a.dc.receive([0]), r.c.takeHint()), null);
        for (let i = 0; i < 400; i++) a.dc.receive(Array.from(hint));
        let n = 0;
        while (r.c.takeHint()) n++;
        expect('what waits for the game is limited', n <= 256, true);
        r.c.close();
    }

    // a channel that fails is tried again by the initiator
    {
        const r = rig(0, false, 0b11, 0);
        await flush();
        FakePC.all[0].connectionState = 'failed';
        FakePC.all[0].onconnectionstatechange();
        expect('a failed channel is let go of', FakePC.all[0].closed, true);
        const retry = r.timers.filter((t) => t.live && t.ms >= 1000 && t.ms < 12000)[0];
        expect('and tried again after a wait', !!retry, true);
        retry.f();
        await flush();
        expect('with a new connection', FakePC.all.length, 2);
        r.c.close();
    }
    {
        const r = rig(1, false, 0b11, 0);
        await flush();
        expect('a seat that does not offer starts nothing by itself', FakePC.all.length, 0);
        r.c.close();
    }
    {
        const r = rig(0, false, 0b11, 0);
        await flush();
        const stale = r.timers.filter((t) => t.live && t.ms === 12000)[0];
        expect('a channel that does not come up in time is given up', !!stale, true);
        stale.f();
        expect('... and let go of', FakePC.all[0].closed, true);
        r.c.close();
    }

    // ---- the echo ----
    // One page with a channel that the test drives: the clock, the second's tick and what arrives are the test's
    const PING = 0x40, PONG = 0x80;
    async function solo() {
        const r = { t: 1000, ticks: [] };
        const env = { RTCPeerConnection: FakePC, now: () => r.t, setTimeout: () => 0, clearTimeout: () => {},
            setInterval: (f, ms) => { r.ticks.push({ f, ms, live: true }); return r.ticks.length - 1; }, clearInterval: (h) => { r.ticks[h].live = false; } };
        FakePC.all = [];
        r.c = P.create(env);
        r.c.open(0, false, ICE);
        r.c.peers(0b11, 0);
        await flush();
        r.dc = FakePC.all[0].dc;
        r.dc.open();
        await flush();
        r.tick = () => r.ticks.forEach((x) => { if (x.live) x.f(); });
        r.sent = () => r.dc.sent.map((b) => Array.from(b));
        return r;
    }
    // Two pages (seats 0 and 1) with a wire between their channels: what one sends arrives `delay[side]` ms later unless `lose(side, bytes)` says it is lost; the clock, the ticks and the arrivals
    // are moved together by advance(ms) and nothing happens in between
    async function duo() {
        const w = { t: 1000, events: [], ticks: [], delay: [20, 20], lose: () => false, log: [[], []] };
        const page = (me) => {
            FakePC.all = [];
            const env = { RTCPeerConnection: FakePC, now: () => w.t, setTimeout: () => 0, clearTimeout: () => {},
                setInterval: (f, ms) => { w.ticks.push({ f, ms, next: w.t + ms, live: true }); return w.ticks.length - 1; }, clearInterval: (h) => { w.ticks[h].live = false; } };
            const c = P.create(env);
            c.open(me, false, ICE);
            c.peers(0b11, 0);
            return c;
        };
        w.a = page(0);
        await flush();
        const dcA = FakePC.all[0].dc;
        w.b = page(1);
        w.b.signal(0, 1, SDP);                                   // (the lower seat offers: the higher one starts with the offer)
        await flush();
        const dcB = FakePC.all[0].dc;
        const dcs = [dcA, dcB];
        dcs.forEach((dc, side) => {
            dc.send = (bytes) => {
                const copy = Array.from(bytes);
                w.log[side].push(copy);
                if (!w.lose(side, copy)) w.events.push({ at: w.t + w.delay[side], to: dcs[1 - side], bytes: copy });
            };
        });
        dcA.open();
        dcB.open();
        await flush();
        w.advance = (ms) => {
            const end = w.t + ms;
            for (;;) {
                const due = [];
                w.events.forEach((e) => due.push({ at: e.at, go: () => { w.events.splice(w.events.indexOf(e), 1); e.to.receive(e.bytes); } }));
                w.ticks.forEach((x) => { if (x.live) due.push({ at: x.next, go: () => { x.next += x.ms; x.f(); } }); });
                const next = due.filter((d) => d.at <= end).sort((x, y) => x.at - y.at)[0];
                if (!next) break;
                w.t = Math.max(w.t, next.at);
                next.go();
            }
            w.t = end;
        };
        w.second = (roundTrip) => { w.delay = [roundTrip / 2, roundTrip / 2]; w.advance(1000); };     // (called half a second after a tick: the tick, its answer and the other page's own are all inside)
        w.advance(500);
        return w;
    }

    {   // what goes on the wire
        const r = await solo();
        expect('a ping each second', r.ticks.map((x) => x.ms), [1000]);
        for (let i = 0; i < 18; i++) r.tick();
        expect('the pings count 0 to 15 and begin again, each one byte', r.sent(), Array.from({ length: 18 }, (_, i) => [PING | (i & 15)]));
        r.c.close();
    }
    {   // an answer at once, with the number of the ping, and only on an open channel
        const r = await solo();
        r.dc.receive([PING | 5]);
        r.dc.receive([PING | 15]);
        expect('a ping is answered at once with its own number', r.sent(), [[PONG | 5], [PONG | 15]]);
        r.dc.readyState = 'closing';
        r.dc.receive([PING | 1]);
        expect('a channel that is closing is not answered on', r.sent().length, 2);
        r.dc.readyState = 'open';
        r.dc.receive([PING | 2, 1, 2]);
        expect('a message of more than one byte is a hint, even when it starts like a ping: not answered, given to the game', [r.sent().length, r.c.takeHint().from], [2, 1]);
        r.dc.receive([0]);
        expect('the old heartbeat is not answered', r.sent().length, 2);
        r.c.close();
    }
    {   // an old page drops every message of one byte, so the new ones cost it nothing: the game never gets one, and a page of the new kind sends no other message of its own
        const r = await solo();
        for (let b = 0; b < 256; b++) r.dc.receive([b]);
        expect('no message of one byte reaches the game', r.c.takeHint(), null);
        r.tick();
        r.tick();
        expect('the page\'s own messages are of one byte', r.sent().filter((m) => m.length !== 1).length, 0);
        r.c.close();
    }
    {   // what counts as an answer
        const r = await solo();
        r.dc.receive([PONG | 3]);
        expect('an answer that nobody asked for is no sample', r.c.rtt(1), -1);
        r.tick();                                                // ping 0
        r.t += 30;
        r.dc.receive([PONG | 1]);
        expect('an answer with another number is no sample', r.c.rtt(1), -1);
        r.dc.receive([PONG | 0]);
        expect('the round trip is the clock\'s gap between the ping and its answer', r.c.rtt(1), 30);
        r.t += 10;
        r.dc.receive([PONG | 0]);
        expect('an answer that comes twice counts once', r.c.rtt(1), 30);
        r.t += 960;
        r.tick();                                                // ping 1
        r.t += 1000;
        r.tick();                                                // ping 2: the answer to ping 1 is late
        r.t += 5;
        r.dc.receive([PONG | 1]);
        expect('an answer to an older ping is no sample', r.c.rtt(1), 30);
        r.t += 20;
        r.dc.receive([PONG | 2]);
        expect('the answer to the last ping is; the mean of 30 and 25 is rounded up', r.c.rtt(1), 28);
        r.c.close();
    }
    {   // a number that is no longer one
        const r = await solo();
        r.tick();
        r.t += 40;
        r.dc.receive([PONG | 0]);
        r.t += 3999;
        expect('a round trip is still read after 3999 ms of silence', r.c.rtt(1), 40);
        r.t += 2;
        expect('and is not after 4001 ms', r.c.rtt(1), -1);
        r.tick();
        r.t += 12;
        r.dc.receive([PONG | 1]);
        expect('an answer brings a number back', r.c.rtt(1) >= 0, true);
        r.c.close();
    }
    {   // no channel, no number
        const r = await solo();
        r.tick();
        r.t += 40;
        r.dc.receive([PONG | 0]);
        expect('a channel with an answer has a number, a seat with no channel and the own seat have none', [r.c.rtt(1), r.c.rtt(2), r.c.rtt(0), r.c.rtt(7)], [40, -1, -1, -1]);
        const before = r.sent().length;
        r.dc.readyState = 'closing';
        r.tick();
        expect('a channel that is closing has none and is sent no ping', [r.c.rtt(1), r.sent().length], [-1, before]);
        r.dc.readyState = 'open';
        r.c.close();
        expect('a game that closed the channels has none, and no ping goes on', [r.c.rtt(1), r.ticks.some((x) => x.live)], [-1, false]);
        const early = rig(0, false, 0b11, 0);
        await flush();
        expect('a channel that never opened has none', [early.c.rtt(1), early.c.link(1)], [-1, 1]);
        early.c.close();
    }
    {   // a page of the old kind: its heartbeat is the only thing it says and it answers nothing
        const r = await solo();
        const seen = [];
        for (let i = 0; i < 10; i++) { r.tick(); r.t += 1000; r.dc.receive([0]); seen.push(r.c.rtt(1)); }
        expect('a page that never answers never has a number, and its channel is a mark all the same', [seen, r.c.link(1)], [Array(10).fill(-1), 2]);
        r.c.close();
    }

    {   // two pages that answer each other
        const w = await duo();
        w.second(40);
        expect('the round trip is the clock\'s gap, on both pages', [w.a.rtt(1), w.b.rtt(0)], [40, 40]);
        expect('each page sent its ping and its answer', w.log, [[[PING | 0], [PONG | 0]], [[PING | 0], [PONG | 0]]]);
        w.a.close();
        w.b.close();
    }
    {
        const w = await duo();
        w.delay = [15, 35];
        w.advance(1000);
        expect('the round trip is the way there and the way back, whichever is slower', [w.a.rtt(1), w.b.rtt(0)], [50, 50]);
        w.a.close();
        w.b.close();
    }
    {   // the mean of the last five round trips
        const w = await duo();
        [20, 40, 60, 80, 100].forEach((rt) => w.second(rt));
        expect('the mean of five', [w.a.rtt(1), w.b.rtt(0)], [60, 60]);
        w.second(120);
        expect('a sixth takes the first one out', [w.a.rtt(1), w.b.rtt(0)], [80, 80]);
        w.a.close();
        w.b.close();
    }
    {   // a ping or an answer that is lost gives no sample
        const w = await duo();
        w.second(20);
        w.second(20);
        w.lose = (side, bytes) => side === 0 && (bytes[0] & 0xF0) === PING;       // page 0's ping is lost
        w.second(60);
        expect('a lost ping gives page 0 no sample, and the other page its own', [w.a.rtt(1), w.b.rtt(0)], [20, 33]);
        w.lose = (side, bytes) => side === 1 && (bytes[0] & 0xF0) === PONG;       // page 1's answer is lost
        w.second(60);
        expect('a lost answer gives page 0 no sample, and the other page its own', [w.a.rtt(1), w.b.rtt(0)], [20, 40]);
        w.lose = () => false;
        w.second(60);
        w.second(60);
        expect('the next ones count, page 1 over its last five', [w.a.rtt(1), w.b.rtt(0)], [40, 52]);
        w.a.close();
        w.b.close();
    }
    {   // one page stops answering: the other one's number goes after four seconds, while its own goes on
        const w = await duo();
        w.second(40);
        w.lose = (side, bytes) => side === 1 && (bytes[0] & 0xF0) === PONG;       // page 1 pings but does not answer
        for (let i = 0; i < 5; i++) w.second(40);
        expect('a page that is not answered has no number after four seconds, and keeps the other\'s', [w.a.rtt(1), w.b.rtt(0)], [-1, 40]);
        w.a.close();
        w.b.close();
    }

    console.log((failed ? 'FAILED ' : 'ok ') + checks + ' checks, ' + failed + ' failed');
    process.exit(failed ? 1 : 0);
})();
