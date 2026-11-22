/* The game page's data channels (protocol 26, docs/P2P_HINTS.md). They carry one thing: a player's orders to the other players ahead of their sealed turns (hints). The server stays the sealer of the
   turns; a channel that is down, slow or wrong costs nothing.

   THE RULE OF THE ADDRESS. Every channel goes through the operator's relay (iceTransportPolicy "relay": only relay candidates are gathered, sent and accepted), so that no player's address reaches
   another player. Two players are connected directly only when BOTH turned the setting on; only then may a candidate that is not a relay candidate travel between them. The page keeps the rule on
   the way out (a candidate or an SDP that shows an address is never sent to a peer that has not allowed direct), on the way in (never accepted from one) and again in what the browser chose (a
   channel whose chosen path is not the relay's, for a pair that did not both allow direct, is closed); the server keeps it too (ants_net/p2p.hpp, signal_allowed).

   THE ECHO. A channel also carries one-byte messages of its own, once a second each way: 0x00 the old heartbeat, 0x40|n a ping (n is 0 to 15), 0x80|n its answer, which a page sends at once. rtt(seat) is the
   mean of the last five round trips of an answer to the last ping this page sent, or -1: no answer for a while, no channel, or a page of the old kind (it drops a message of one byte and never answers).

   The game (C++, ants_net/wasm_rtc.cpp) calls open / peers / signal / sendHint / takeSignal / takeHint / link / rtt / close. The rules and the channels are one object made by AntsP2p.create(env) with the
   browser's parts in `env` (the tests give fakes); the rules alone are AntsP2p.rules. */
(function (root) {
    'use strict';
    var COPIES = 3, PING_MS = 1000, SILENT_MS = 4000, RTT_KEPT = 5, PING = 0x40, PONG = 0x80, GIVE_UP_MS = 12000, MAX_INBOX = 256, MAX_OUTBOX = 256, MAX_RETRIES = 3;
    var LINK = { none: 0, connecting: 1, relay: 2, direct: 3 }, KIND = { offer: 1, answer: 2, candidate: 3, bye: 4 };
    var SETTING = 'ants.direct';

    // ---- the rule of the address (pure) ----
    function typeOf(line) { var m = / typ ([a-z]+)/.exec(line); return m ? m[1] : ''; }
    function blankRelated(line) { return line.replace(/ raddr \S+/, ' raddr 0.0.0.0').replace(/ rport \d+/, ' rport 0'); }
    // an SDP with no candidate and no address: what the browser knows of its own addresses is never in it
    function scrubSdp(sdp) {
        return String(sdp).split(/\r?\n/).filter(function (l) { return !/^a=(candidate:|end-of-candidates)/.test(l); }).map(function (l) {
            return l.replace(/^(c=IN (IP[46]) ).*/, function (m, a, v) { return a + (v === 'IP4' ? '0.0.0.0' : '::'); })
                    .replace(/^(a=rtcp:\d+ IN (IP[46]) ).*/, function (m, a, v) { return a + (v === 'IP4' ? '0.0.0.0' : '::'); });
        }).join('\r\n');
    }
    // "index|candidate:..." as it travels. For a pair that did not both allow direct only a relay candidate (its related address blanked) passes, in both directions; the end of the candidates ("index|") always
    function passCandidate(pairDirect, text) {
        var bar = String(text).indexOf('|');
        if (bar < 1 || !/^\d{1,3}$/.test(text.slice(0, bar))) return null;
        var c = text.slice(bar + 1);
        if (c === '') return text;
        if (pairDirect) return text;
        if (c.indexOf('candidate:') !== 0 || typeOf(c) !== 'relay') return null;
        return text.slice(0, bar + 1) + blankRelated(c);
    }
    function serversFor(ice, pairDirect) {
        var stun = [], turn = [];
        ice.urls.forEach(function (u) { (/^stuns?:/.test(u) ? stun : turn).push(u); });
        var out = [];
        if (pairDirect && stun.length) out.push({ urls: stun });
        if (turn.length) out.push({ urls: turn, username: ice.username, credential: ice.credential });
        return out;
    }
    function directSetting(storage) { try { return (storage || root.localStorage).getItem(SETTING) === '1'; } catch (e) { return false; } }
    var rules = { typeOf: typeOf, scrubSdp: scrubSdp, passCandidate: passCandidate, serversFor: serversFor, directSetting: directSetting, SETTING: SETTING, LINK: LINK, KIND: KIND, COPIES: COPIES };

    // ---- the channels ----
    function create(env) {
        env = env || {};
        var PC = env.RTCPeerConnection || root.RTCPeerConnection, now = env.now || function () { return Date.now(); };
        var timer = env.setTimeout || function (f, ms) { return root.setTimeout(f, ms); }, untimer = env.clearTimeout || function (h) { root.clearTimeout(h); };
        var every = env.setInterval || function (f, ms) { return root.setInterval(f, ms); }, unevery = env.clearInterval || function (h) { root.clearInterval(h); };
        var me = -1, myDirect = false, ice = null, ready = 0, directMask = 0, peers = {}, queued = [], outbox = [], inbox = [], tick = null;
        var stats = { refusedIn: 0, refusedOut: 0, policyBreaks: 0, hintsOut: 0, hintsIn: 0, opened: 0 };

        function pairDirect(seat) { return myDirect && ((directMask >> seat) & 1) === 1 && ((directMask >> me) & 1) === 1; }
        function say(to, kind, text) { if (outbox.length < MAX_OUTBOX) outbox.push({ to: to, kind: kind, text: text }); }
        function drop(seat, tellPeer) {
            var p = peers[seat];
            if (!p) return;
            delete peers[seat];
            untimer(p.timer);
            try { if (p.dc) p.dc.close(); if (p.pc) p.pc.close(); } catch (e) { /* closed already */ }
            if (tellPeer) say(seat, KIND.bye, '');
        }
        function failed(p) {                               // a channel that did not come up, or was lost: the server's stream is all that is left, and the initiator tries again a few times
            if (peers[p.seat] !== p) return;
            var again = p.initiator && p.tries < MAX_RETRIES && ((ready >> p.seat) & 1) === 1, seat = p.seat, tries = p.tries;
            drop(seat, false);
            if (again) timer(function () { if (ice && !peers[seat] && ((ready >> seat) & 1) === 1) { begin(seat).tries = tries + 1; } }, 1000 * Math.pow(2, tries));
        }
        function judge(p) {                                // which way the browser chose, from its own statistics (no address is read: only the kind of the local candidate)
            if (!p.pc.getStats) return;
            p.pc.getStats().then(function (report) {
                var pairs = {}, cands = {}, chosen = null;
                report.forEach(function (r) {
                    if (r.type === 'transport' && r.selectedCandidatePairId) chosen = r.selectedCandidatePairId;
                    if (r.type === 'candidate-pair') pairs[r.id] = r;
                    if (r.type === 'local-candidate') cands[r.id] = r;
                });
                if (!chosen) { for (var id in pairs) if (pairs[id].state === 'succeeded' && (pairs[id].selected || pairs[id].nominated)) chosen = id; }
                var pr = chosen && pairs[chosen], lc = pr && cands[pr.localCandidateId];
                if (!lc || peers[p.seat] !== p) return;
                if (lc.candidateType === 'relay') p.link = LINK.relay;
                else if (p.direct) p.link = LINK.direct;
                else { stats.policyBreaks++; drop(p.seat, true); }       // a path that is not the relay's for a pair that did not both allow direct: it is closed
            }).catch(function () { /* no statistics: the mark waits */ });
        }
        function settle(p) {
            p.up = true;
            p.heard = now();
            stats.opened++;
            judge(p);
            timer(function () { if (peers[p.seat] === p && p.link === LINK.connecting) judge(p); }, 1500);
        }
        function echo(p, byte) {                           // a ping is answered at once; an answer counts only for the last ping sent
            var kind = byte & 0xF0, n = byte & 0x0F;
            if (kind === PING) { try { if (p.dc.readyState === 'open') p.dc.send(new Uint8Array([PONG | n])); } catch (e) { /* closing */ } }
            else if (kind === PONG && p.waiting && n === p.pingN) {
                var t = now(), ms = t - p.pingAt;
                p.waiting = false;
                if (ms < 0 || ms > SILENT_MS) return;
                p.rtts.push(ms);
                if (p.rtts.length > RTT_KEPT) p.rtts.shift();
                p.answered = t;
            }
        }
        function ping(p) {
            p.pingN = (p.pingN + 1) & 15;
            p.pingAt = now();
            p.waiting = true;
            try { p.dc.send(new Uint8Array([PING | p.pingN])); } catch (e) { /* closing */ }
        }
        function begin(seat) {
            var direct = pairDirect(seat), p = { seat: seat, direct: direct, initiator: me < seat, up: false, link: LINK.connecting, heard: 0, pending: [], remote: false, tries: 0, timer: null,
                pingN: 15, pingAt: 0, waiting: false, rtts: [], answered: 0 };
            peers[seat] = p;
            p.pc = new PC({ iceServers: serversFor(ice, direct), iceTransportPolicy: direct ? 'all' : 'relay' });
            p.dc = p.pc.createDataChannel('hints', { negotiated: true, id: 1, ordered: false, maxRetransmits: 0 });
            p.dc.binaryType = 'arraybuffer';
            p.dc.onopen = function () { settle(p); };
            p.dc.onmessage = function (e) {
                p.heard = now();
                var b = new Uint8Array(e.data);
                if (b.length === 1) echo(p, b[0]);
                if (b.length < 2 || inbox.length >= MAX_INBOX) return;       // (a message of one byte is the channel's own: the game never sees it)
                inbox.push({ from: seat, bytes: b });
                stats.hintsIn++;
            };
            p.pc.onicecandidate = function (e) {
                var text = e.candidate ? passCandidate(direct, (e.candidate.sdpMLineIndex || 0) + '|' + e.candidate.candidate) : '0|';
                if (text === null) { stats.refusedOut++; return; }
                say(seat, KIND.candidate, text);
            };
            p.pc.onconnectionstatechange = function () { if (p.pc.connectionState === 'failed' || p.pc.connectionState === 'closed') failed(p); };
            p.timer = timer(function () { if (!p.up) failed(p); }, GIVE_UP_MS);
            if (p.initiator) {
                p.pc.createOffer().then(function (o) { return p.pc.setLocalDescription(o); }).then(function () { say(seat, KIND.offer, scrubSdp(p.pc.localDescription.sdp)); }).catch(function () { failed(p); });
            }
            return p;
        }
        function accept(seat, kind, text) {
            var p = peers[seat];
            if (kind === KIND.bye) { drop(seat, false); return; }
            if (kind === KIND.offer) {
                if (me < seat) return;                      // (the lower seat offers; an offer to the lower seat is a mistake)
                drop(seat, false);
                p = begin(seat);
                p.pc.setRemoteDescription({ type: 'offer', sdp: scrubSdp(text) }).then(function () { return flush(p); }).then(function () { return p.pc.createAnswer(); })
                    .then(function (a) { return p.pc.setLocalDescription(a); }).then(function () { say(seat, KIND.answer, scrubSdp(p.pc.localDescription.sdp)); }).catch(function () { failed(p); });
            } else if (kind === KIND.answer) {
                if (!p || !p.initiator || p.remote) return;
                p.pc.setRemoteDescription({ type: 'answer', sdp: scrubSdp(text) }).then(function () { return flush(p); }).catch(function () { failed(p); });
            } else if (kind === KIND.candidate && p) {
                var c = passCandidate(p.direct, text);
                if (c === null) { stats.refusedIn++; return; }
                if (p.remote) add(p, c); else p.pending.push(c);
            }
        }
        function add(p, text) {
            var bar = text.indexOf('|'), c = text.slice(bar + 1);
            if (c === '') return;
            p.pc.addIceCandidate({ candidate: c, sdpMLineIndex: parseInt(text.slice(0, bar), 10) }).catch(function () { /* a candidate that does not fit: skipped */ });
        }
        function flush(p) { p.remote = true; p.pending.splice(0).forEach(function (t) { add(p, t); }); return Promise.resolve(); }
        function reconsider() {                              // who should have a channel now: every ready seat but this one, started by the lower seat
            if (!ice) return;
            for (var s = 0; s < 4; s++) {
                if (s === me) continue;
                var isReady = ((ready >> s) & 1) === 1;
                if (!isReady && peers[s]) drop(s, false);
                if (isReady && !peers[s] && me < s) begin(s);
            }
        }
        function drain() {                                  // a signal waits until this player has the relay's credential and the other player is known to be ready
            if (!ice) return;
            var wait = [];
            queued.splice(0).forEach(function (q) { if (((ready >> q.from) & 1) === 1 || q.kind === KIND.bye) accept(q.from, q.kind, q.text); else wait.push(q); });
            wait.forEach(function (q) { queued.push(q); });
        }
        var api = {
            open: function (seat, direct, config) {
                me = seat; myDirect = !!direct; ice = config;
                if (tick === null) tick = every(function () {              // the ping of each second (it is the channel's heartbeat too)
                    for (var s in peers) { var p = peers[s]; if (p.up && p.dc.readyState === 'open') ping(p); }
                }, PING_MS);
                reconsider();
                drain();
            },
            peers: function (readyMask, direct) { ready = readyMask; directMask = direct; reconsider(); drain(); },
            signal: function (from, kind, text) {
                if (queued.length < MAX_OUTBOX) queued.push({ from: from, kind: kind, text: text });
                drain();
            },
            sendHint: function (bytes) {
                var any = false;
                for (var s in peers) {
                    var p = peers[s];
                    if (!p.up || p.dc.readyState !== 'open') continue;
                    for (var i = 0; i < COPIES; i++) { try { p.dc.send(bytes); } catch (e) { break; } }
                    any = true;
                }
                if (any) stats.hintsOut++;
            },
            takeSignal: function () { return outbox.shift() || null; },
            takeHint: function () { return inbox.shift() || null; },
            link: function (seat) {
                var p = peers[seat];
                if (!p) return LINK.none;
                if (p.up && now() - p.heard > SILENT_MS) return LINK.connecting;         // a channel that has said nothing for a while is no mark
                return p.link;
            },
            rtt: function (seat) {                           // the mean of the last five round trips in ms, or -1: no channel, no answer for a while, or a page that does not answer
                var p = peers[seat], sum = 0, i;
                if (!p || !p.up || p.dc.readyState !== 'open' || !p.rtts.length || now() - p.answered > SILENT_MS) return -1;
                for (i = 0; i < p.rtts.length; i++) sum += p.rtts[i];
                return Math.round(sum / p.rtts.length);
            },
            close: function () { for (var s in peers) drop(+s, true); if (tick !== null) { unevery(tick); tick = null; } ice = null; me = -1; queued.length = 0; },
            stats: function () { return stats; }
        };
        return api;
    }

    root.AntsP2p = { create: create, rules: rules, directSetting: directSetting };
    if (typeof module !== 'undefined' && module.exports) module.exports = root.AntsP2p;
})(typeof window !== 'undefined' ? window : globalThis);
