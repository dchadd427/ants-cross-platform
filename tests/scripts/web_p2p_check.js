// Runs the page's data channels (web/p2p.js, protocol 26, docs/P2P_HINTS.md) in node with a fake RTCPeerConnection, and checks the rule of the address and what the channels do:
//   * the rule of the address, for every pair of settings (this player allows direct or not, the other allows or not): only when BOTH allow is the connection made with every kind of candidate
//     (iceTransportPolicy "all", a STUN server); in every other case the policy is "relay", no STUN server is named, a candidate that is not a relay candidate is never sent and never accepted, a relay
//     candidate's related address is blanked on the way, and an SDP is sent with no candidate and no address in it;
//   * the browser's own choice is checked again: a channel of a pair that did not both allow direct whose chosen path is not the relay's is closed (and counted), a relay path is the mark "relay", a
//     direct one (both allowed) is "direct";
//   * the channels: the lower seat offers and the other answers, signals wait until the credential and the other player's readiness are known, a hint is sent three times and only on an open channel,
//     what arrives is handed up with the channel's seat and the one-byte heartbeat never is, a channel that fails is tried again by the initiator a few times, a channel that says nothing is no mark.
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
        pc.dc.receive([0]);                                      // the heartbeat
        expect('the heartbeat brings it back and is never given to the game', [r.c.link(1), r.c.takeHint()], [2, null]);
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
        expect('a message of one byte is a heartbeat', (a.dc.receive([0]), r.c.takeHint()), null);
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

    console.log((failed ? 'FAILED ' : 'ok ') + checks + ' checks, ' + failed + ' failed');
    process.exit(failed ? 1 : 0);
})();
