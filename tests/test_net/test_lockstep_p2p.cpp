// Protocol 26 (docs/P2P_HINTS.md): the messages of hints and channels, the relay's credential, the rule for what may be shown to whom, and what a server's session does with them.
//
// What is checked:
//   - the messages: every layout round-trips and every decoder refuses what an honest page never sends (a kind that is not predicted, a seat that is not one, trailing bytes, a credential with no address);
//   - the credential: coturn's rule (an expiry, a random label, base64(HMAC-SHA1)): good until the expiry and for nobody else, and the code of HMAC-SHA1 itself against RFC 2202;
//   - the rule of the address: a relay candidate may always be passed, anything that shows a player's own address only between two players who both allow direct connections;
//   - the session: who is given a credential, who is told who is ready, what is relayed and what is counted as a violation, and that a computer player's order is sent to every player the moment it is
//     given, at the tick of the turn that will carry it;
//   - what none of it can do: the turns, the record and the state hashes are the same with and without all this traffic.
#include <chrono>

#include "ants_net/p2p.hpp"
#include "ants_net/prediction.hpp"
#include "ants_net/turn.hpp"
#include "ants_net/ws.hpp"
#include "lockstep_fixtures.hpp"

namespace {

std::string hex_of(const std::array<uint8_t, 20>& d) {
    static const char kHex[] = "0123456789abcdef";
    std::string s;
    for (const uint8_t b : d) {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 15u]);
    }
    return s;
}

const char kRelayCandidate[] = "candidate:1 1 udp 41885439 203.0.113.9 49170 typ relay raddr 0.0.0.0 rport 0";
const char kHostCandidate[] = "candidate:2 1 udp 2122260223 192.168.1.20 54321 typ host";
const char kSrflxCandidate[] = "candidate:3 1 udp 1686052607 198.51.100.4 54321 typ srflx raddr 192.168.1.20 rport 54321";

// A host session of a server (no seat of its own) with some persons and some computer players, over a loopback network, with a relay of its own
struct P2pRig {
    LoopbackNetwork net;
    sim::SimulationEngine referee;
    std::vector<std::unique_ptr<sim::SimulationEngine>> sims;
    Ids ids;
    std::unique_ptr<HostSession> host;
    std::vector<std::unique_ptr<ClientSession>> clients;
    std::vector<Connection*> ends;                        // the clients' ends of the links (index = seat of a person)
    std::vector<std::vector<std::vector<uint8_t>>> got;   // what each person's session handed to its owner (set_on_p2p)
    std::vector<TurnMsg> sealed;
    uint32_t now{0};
    uint32_t unix{1700000000u};
    uint32_t random_calls{0};
    uint8_t persons{2};

    explicit P2pRig(uint8_t persons_ = 2, bool relay = true, uint8_t bots_from = 255, bool bot_hints = true) : net(7), persons(persons_) {
        ids = build_world(referee, 5);
        HostSession::Config hc;
        hc.host_player = kNoSeat;
        hc.bot_hints = bot_hints;
        if (relay) {
            auto turn = std::make_shared<TurnSettings>();
            turn->urls = {"stun:relay.example:3478", "turn:relay.example:3478?transport=udp"};
            turn->credential = make_turn_signer("test-secret");
            turn->ttl_s = 3600;
            hc.turn = turn;
        }
        hc.random_bytes = [this](uint8_t* out, size_t n) {
            ++random_calls;
            for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(random_calls * 37u + i * 11u);
            return true;
        };
        hc.unix_now = [this]() { return unix; };
        host = std::make_unique<HostSession>(referee, hc);
        for (uint8_t p = 0; p < 4; ++p) {
            if (p >= persons) {
                if (bots_from != 255 && p >= bots_from) host->add_bot_seat(p);
                continue;
            }
            sims.push_back(std::make_unique<sim::SimulationEngine>());
            build_world(*sims.back(), 5);
            auto link = net.connect({5, 0});
            ends.push_back(link.second);
            host->add_client(p, link.first);
            ClientSession::Config cc;
            cc.player = p;
            cc.host = kNoSeat;
            cc.migration = false;
            clients.push_back(std::make_unique<ClientSession>(*sims.back(), cc));
            clients.back()->set_connection(link.second);
            got.emplace_back();
        }
        for (size_t i = 0; i < clients.size(); ++i) clients[i]->set_on_p2p([this, i](const std::vector<uint8_t>& m) { got[i].push_back(m); });
        host->set_on_seal([this](const TurnMsg& t) { sealed.push_back(t); });
        host->start(0);
        for (auto& c : clients) c->start(0);
    }
    void run(uint32_t ms) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            net.set_time(now);
            host->update(now);
            for (auto& c : clients) c->update(now);
        }
    }
    void send(size_t person, const std::vector<uint8_t>& message) { ends[person]->send(message); }
    // the messages of a type that a person's session has been handed
    template <class Msg>
    std::vector<Msg> received(size_t person, MsgType type) const {
        std::vector<Msg> out;
        for (const auto& m : got[person]) {
            Msg decoded;
            if (peek_type(m) == type && decode(m, decoded)) out.push_back(decoded);
        }
        return out;
    }
    void say_config(size_t person, bool direct) {
        send(person, encode(P2pConfigMsg{direct}));
        run(200);
    }
    void say_signal(size_t person, uint8_t to, SignalKind kind, const std::string& text) {
        send(person, encode(SignalMsg{to, kind, text}));
        run(200);
    }
};

Command bot_walk(const Ids& ids, uint8_t seat, int16_t x, int16_t y) { return cmd(CommandType::GroupMove, seat, 255, x, y, ids.ants[seat]); }

void run_message_tests() {
    TEST_CASE("P25.1 The Messages Of Hints And Channels Round-Trip, And Every Decoder Refuses What An Honest Page Never Sends") {
        HintMsg hint;
        hint.seq = 9;
        hint.tick = 1234;
        hint.command = cmd(CommandType::GroupAttack, 2, 255, 12, 13, {5, 6, 7});
        HintMsg back;
        ASSERT_TRUE(decode(encode(hint), back));
        ASSERT_TRUE(back.seq == 9 && back.tick == 1234 && back.command == hint.command);
        std::vector<uint8_t> raw = encode(hint);
        for (const CommandType t : {CommandType::Hatch, CommandType::AllianceInvite, CommandType::Quit, CommandType::Drop, CommandType::None}) {      // (only what a prediction can use)
            HintMsg odd = hint;
            odd.command.type = t;
            odd.command.ants.clear();                                                // (a command of these kinds has no ants: it is a command that decodes, and still no hint)
            odd.command.tile_x = 0;
            odd.command.tile_y = 0;
            odd.command.other_player = 255;
            ASSERT_FALSE(decode(encode(odd), back));
        }
        HintMsg zero = hint;
        zero.seq = 0;
        ASSERT_FALSE(decode(encode(zero), back));                                    // a number starts at 1
        HintMsg far = hint;
        far.tick = kMaxHintTick + 1;
        ASSERT_FALSE(decode(encode(far), back));
        HintMsg seat = hint;
        seat.command.issuer = sim::MAX_PLAYERS;
        ASSERT_FALSE(decode(encode(seat), back));
        raw.push_back(0);
        ASSERT_FALSE(decode(raw, back));                                             // trailing bytes
        raw = encode(hint);
        raw.pop_back();
        ASSERT_FALSE(decode(raw, back));                                             // truncated

        P2pConfigMsg config;
        ASSERT_TRUE(decode(encode(P2pConfigMsg{true}), config) && config.direct);
        ASSERT_TRUE(decode(encode(P2pConfigMsg{false}), config) && !config.direct);
        ASSERT_FALSE(decode(std::vector<uint8_t>{static_cast<uint8_t>(MsgType::P2pConfig), 2}, config));
        ASSERT_FALSE(decode(std::vector<uint8_t>{static_cast<uint8_t>(MsgType::P2pConfig)}, config));

        IceConfigMsg ice;
        ice.expires = 1700003600u;
        ice.username = "1700003600:ab12";
        ice.credential = "c2VjcmV0";
        ice.urls = {"stun:relay.example:3478", "turn:relay.example:3478?transport=udp"};
        IceConfigMsg ice_back;
        ASSERT_TRUE(decode(encode(ice), ice_back) && ice_back.expires == ice.expires && ice_back.username == ice.username && ice_back.credential == ice.credential && ice_back.urls == ice.urls);
        ASSERT_TRUE(decode(encode(IceConfigMsg{}), ice_back) && ice_back.urls.empty());   // no relay: nothing else is told
        IceConfigMsg no_urls = ice;
        no_urls.urls.clear();
        ASSERT_FALSE(decode(encode(no_urls), ice_back));                             // a credential for nothing
        IceConfigMsg no_name = ice;
        no_name.username.clear();
        ASSERT_FALSE(decode(encode(no_name), ice_back));                             // a relay without a user
        IceConfigMsg blank = ice;
        blank.urls[0] = "stun:relay example";
        ASSERT_FALSE(decode(encode(blank), ice_back));
        IceConfigMsg many = ice;
        many.urls.assign(kMaxIceUrls + 1, "stun:relay.example:3478");
        std::vector<uint8_t> many_raw = encode(many);
        many_raw[1 + 4 + 1 + ice.username.size() + 1 + ice.credential.size()] = static_cast<uint8_t>(kMaxIceUrls + 1);      // (the count is cut by the encoder; say it by hand)
        ASSERT_FALSE(decode(many_raw, ice_back));

        P2pPeersMsg peers;
        ASSERT_TRUE(decode(encode(P2pPeersMsg{0x0B, 0x03}), peers) && peers.ready == 0x0B && peers.direct == 0x03);
        ASSERT_FALSE(decode(encode(P2pPeersMsg{0x01, 0x02}), peers));                // a seat that allows direct has said P2pConfig
        ASSERT_FALSE(decode(encode(P2pPeersMsg{0x10, 0x00}), peers));                // four seats

        SignalMsg signal;
        const std::string sdp = "v=0\r\no=- 1 2 IN IP4 0.0.0.0\r\ns=-\r\n";
        ASSERT_TRUE(decode(encode(SignalMsg{3, SignalKind::Offer, sdp}), signal) && signal.peer == 3 && signal.kind == SignalKind::Offer && signal.text == sdp);
        ASSERT_TRUE(decode(encode(SignalMsg{0, SignalKind::Bye, ""}), signal) && signal.text.empty());
        ASSERT_FALSE(decode(encode(SignalMsg{4, SignalKind::Offer, sdp}), signal));  // a seat that is not one
        std::vector<uint8_t> bad_kind = encode(SignalMsg{1, SignalKind::Offer, sdp});
        bad_kind[2] = 0;
        ASSERT_FALSE(decode(bad_kind, signal));
        bad_kind[2] = 5;
        ASSERT_FALSE(decode(bad_kind, signal));
        ASSERT_FALSE(decode(encode(SignalMsg{1, SignalKind::Offer, std::string("a\0b", 3)}), signal));       // no control character but the line ends
        ASSERT_FALSE(decode(encode(SignalMsg{1, SignalKind::Offer, std::string("caf\xC3\xA9")}), signal));   // printable ASCII only
        std::vector<uint8_t> lie = encode(SignalMsg{1, SignalKind::Offer, "abc"});
        lie[4] = 9;                                                                  // a length that is not the text's
        ASSERT_FALSE(decode(lie, signal));
        std::vector<uint8_t> big = encode(SignalMsg{1, SignalKind::Offer, std::string(kMaxSignalChars, 'a')});
        ASSERT_TRUE(decode(big, signal));                                            // the most that one message holds
        big.push_back('a');
        big[3] = static_cast<uint8_t>((kMaxSignalChars + 1) & 0xFF);
        big[4] = static_cast<uint8_t>((kMaxSignalChars + 1) >> 8);
        ASSERT_FALSE(decode(big, signal));
        ASSERT_TRUE(static_cast<int>(MsgType::Signal) == 40 && static_cast<int>(MsgType::Ready) == static_cast<int>(MsgType::Signal) + 1 && peek_type(encode(P2pConfigMsg{true})) == MsgType::P2pConfig);
    } TEST_END();
}

void run_credential_tests() {
    TEST_CASE("P25.2 HMAC-SHA1 Is RFC 2202's, And A Credential Is coturn's: Good Until Its Expiry And For Nobody Else") {
        const std::string key1(20, '\x0b');
        ASSERT_EQ(hex_of(hmac_sha1(reinterpret_cast<const uint8_t*>(key1.data()), key1.size(), reinterpret_cast<const uint8_t*>("Hi There"), 8)), std::string("b617318655057264e28bc0b6fb378c8ef146be00"));
        const std::string key2 = "Jefe";
        const std::string msg2 = "what do ya want for nothing?";
        ASSERT_EQ(hex_of(hmac_sha1(reinterpret_cast<const uint8_t*>(key2.data()), key2.size(), reinterpret_cast<const uint8_t*>(msg2.data()), msg2.size())), std::string("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));
        const std::string key6(80, '\xaa');                                          // (a key longer than a block is hashed first)
        const std::string msg6 = "Test Using Larger Than Block-Size Key - Hash Key First";
        ASSERT_EQ(hex_of(hmac_sha1(reinterpret_cast<const uint8_t*>(key6.data()), key6.size(), reinterpret_cast<const uint8_t*>(msg6.data()), msg6.size())), std::string("aa4ae5e15272d00e95705637ce8a3b55ed402112"));

        const std::string secret = "a shared secret";
        const std::string name = turn_username(1700003600u, "0123456789abcdef");
        ASSERT_EQ(name, std::string("1700003600:0123456789abcdef"));
        ASSERT_EQ(turn_username_expiry(name), 1700003600u);
        const std::string credential = turn_credential(secret, name);
        ASSERT_EQ(credential.size(), 28u);                                           // 20 bytes of base64
        ASSERT_TRUE(turn_credential_valid(secret, name, credential, 1700000000u));
        ASSERT_TRUE(turn_credential_valid(secret, name, credential, 1700003599u));
        ASSERT_FALSE(turn_credential_valid(secret, name, credential, 1700003600u));          // expired (the relay refuses at the expiry second)
        ASSERT_FALSE(turn_credential_valid(secret, name, credential, 1800000000u));
        ASSERT_FALSE(turn_credential_valid("another secret", name, credential, 1700000000u));
        ASSERT_FALSE(turn_credential_valid(secret, turn_username(1700003601u, "0123456789abcdef"), credential, 1700000000u));      // a later expiry is another credential
        std::string flipped = credential;
        flipped[0] = flipped[0] == 'A' ? 'B' : 'A';
        ASSERT_FALSE(turn_credential_valid(secret, name, flipped, 1700000000u));
        ASSERT_FALSE(turn_credential_valid("", name, credential, 1700000000u));
        for (const char* odd : {"", ":label", "abc:label", "1700003600:", "1700003600", "99999999999:x"}) ASSERT_EQ(turn_username_expiry(odd), 0u);
        ASSERT_FALSE(static_cast<bool>(make_turn_signer("")));
        ASSERT_EQ(make_turn_signer(secret)(name), credential);
    } TEST_END();

    TEST_CASE("P25.11 The Relay's Port Is One Number: The Name's, The Environment's Or TURN's Own 3478, Never Two That Differ; The Addresses A Player Is Given Follow It") {
        const auto urls = [](const std::string& host, const std::string& port_env) -> std::vector<std::string> {
            const auto turn = make_turn_settings(host, port_env, "a shared secret");
            return turn ? turn->urls : std::vector<std::string>{};
        };
        const std::vector<std::string> standard = {"stun:turn.example.org:3478", "turn:turn.example.org:3478?transport=udp", "turn:turn.example.org:3478?transport=tcp"};
        const std::vector<std::string> other = {"stun:turn.example.org:3480", "turn:turn.example.org:3480?transport=udp", "turn:turn.example.org:3480?transport=tcp"};
        ASSERT_TRUE(urls("turn.example.org", "") == standard);                               // (no port said: TURN's own)
        ASSERT_TRUE(urls("turn.example.org:3478", "") == standard);
        ASSERT_TRUE(urls("turn.example.org:3480", "") == other);                             // (the name says it)
        ASSERT_TRUE(urls("turn.example.org", "3480") == other);                              // (the environment says it: the stack's ANTS_TURN_PORT)
        ASSERT_TRUE(urls("turn.example.org:3480", "3480") == other);                         // (both say it and agree)
        ASSERT_TRUE(urls("turn.example.org:3480", "03480") == other);                        // (the same number written another way)
        ASSERT_TRUE(urls("turn.example.org:3480", "3481").empty());                          // (two numbers: no relay, not a guess)
        ASSERT_TRUE(urls("turn.example.org", "3481:3482").empty() && urls("turn.example.org", "0").empty() && urls("turn.example.org", "65536").empty());
        ASSERT_TRUE(urls("turn.example.org", "-1").empty() && urls("turn.example.org", "34 80").empty() && urls("turn.example.org", "abc").empty() && urls("turn.example.org", "123456").empty());
        ASSERT_TRUE(urls("turn.example.org", "65535").size() == 3u && urls("turn.example.org:1", "").size() == 3u);
        for (const char* bad : {"", ":3478", "turn.example.org:", "turn.example.org:0", "turn.example.org:65536", "turn.example.org:x", "-turn.example.org", ".example.org", "turn example.org", "turn.example.org/x", "turn_example.org", "[::1]:3478"}) {
            ASSERT_TRUE(urls(bad, "").empty());
        }
        ASSERT_TRUE(urls(std::string(101, 'a'), "").empty());
        ASSERT_FALSE(static_cast<bool>(make_turn_settings("turn.example.org", "", "")));      // (no secret, no relay)
        const auto turn = make_turn_settings("turn.example.org", "3480", "a shared secret");
        ASSERT_TRUE(turn && turn->usable());
        const std::string name = turn_username(1700003600u, "0123456789abcdef");
        ASSERT_EQ(turn->credential(name), turn_credential("a shared secret", name));         // (the secret signs, and is nowhere in the addresses)
        for (const std::string& u : turn->urls) ASSERT_TRUE(u.find("a shared secret") == std::string::npos);
    } TEST_END();
}

void run_rule_tests() {
    TEST_CASE("P25.3 Only A Relay Candidate May Be Passed To A Player Who Has Not Allowed Direct Connections: Every Way Of Showing An Address Is Refused, Whatever Else The Text Holds") {
        const std::string relay = kRelayCandidate;
        const std::string host = kHostCandidate;
        const std::string srflx = kSrflxCandidate;
        ASSERT_FALSE(carries_non_relay_candidate(""));
        ASSERT_FALSE(carries_non_relay_candidate("0|"));                             // the end of the candidates
        ASSERT_FALSE(carries_non_relay_candidate("0|" + relay));
        ASSERT_FALSE(carries_non_relay_candidate("a=" + relay + "\r\n"));
        ASSERT_TRUE(carries_non_relay_candidate("0|" + host));
        ASSERT_TRUE(carries_non_relay_candidate("0|" + srflx));
        ASSERT_TRUE(carries_non_relay_candidate("0|candidate:4 1 udp 1 198.51.100.4 5 typ prflx"));
        ASSERT_TRUE(carries_non_relay_candidate("0|candidate:5 1 udp 1 198.51.100.4 5"));                      // no type: not known to be a relay one
        ASSERT_TRUE(carries_non_relay_candidate("0|candidate:6 1 udp 1 198.51.100.4 5 typ relayed"));          // (a word that only starts like it)
        ASSERT_TRUE(carries_non_relay_candidate("0|candidate:7 1 udp 1 203.0.113.9 5 typ relay raddr 192.168.1.20 rport 9"));   // the relay candidate names the player's own address
        ASSERT_FALSE(carries_non_relay_candidate("0|candidate:7 1 udp 1 203.0.113.9 5 typ relay raddr :: rport 0"));
        // one bad candidate among good ones, in any line
        ASSERT_TRUE(carries_non_relay_candidate("v=0\r\na=" + relay + "\r\na=" + host + "\r\na=" + relay + "\r\n"));
        ASSERT_TRUE(carries_non_relay_candidate("v=0\r\na=" + relay + "\r\na=" + relay + "\r\na=" + srflx));
        // an SDP: its connection lines show the address of the best candidate once it is known
        ASSERT_FALSE(carries_non_relay_candidate("v=0\r\nc=IN IP4 0.0.0.0\r\na=rtcp:9 IN IP4 0.0.0.0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"));
        ASSERT_FALSE(carries_non_relay_candidate("v=0\r\nc=IN IP6 ::\r\n"));
        ASSERT_TRUE(carries_non_relay_candidate("v=0\r\nc=IN IP4 198.51.100.4\r\n"));
        ASSERT_TRUE(carries_non_relay_candidate("v=0\r\nc=IN IP6 2001:db8::1\r\n"));
        ASSERT_TRUE(carries_non_relay_candidate("v=0\r\na=rtcp:9 IN IP4 198.51.100.4\r\n"));
        ASSERT_TRUE(carries_non_relay_candidate("v=0\nc=garbage\n"));
        // the rule: relay for anybody, an address only between two who allow it
        for (const bool a : {false, true}) {
            for (const bool b : {false, true}) {
                ASSERT_TRUE(signal_allowed(a, b, "0|" + relay));
                ASSERT_EQ(signal_allowed(a, b, "0|" + host), a && b);
                ASSERT_EQ(signal_allowed(a, b, "0|" + srflx), a && b);
                ASSERT_EQ(signal_allowed(a, b, "v=0\r\nc=IN IP4 198.51.100.4\r\n"), a && b);
            }
        }
    } TEST_END();
}

void run_session_tests() {
    TEST_CASE("P25.4 A Player Who Says P2pConfig Is Handed The Relay's Credential: Good For Three Hours, With A Random Label That Is Not A Name, A New One For Every Player And Every Ask") {
        P2pRig m(2);
        m.say_config(0, false);
        m.say_config(1, true);
        m.say_config(0, false);
        const std::vector<IceConfigMsg> a = m.received<IceConfigMsg>(0, MsgType::IceConfig);
        const std::vector<IceConfigMsg> b = m.received<IceConfigMsg>(1, MsgType::IceConfig);
        ASSERT_TRUE(a.size() == 2 && b.size() == 1);
        ASSERT_EQ(a[0].urls.size(), 2u);
        ASSERT_EQ(a[0].expires, m.unix + 3600u);
        ASSERT_EQ(turn_username_expiry(a[0].username), a[0].expires);
        ASSERT_TRUE(turn_credential_valid("test-secret", a[0].username, a[0].credential, m.unix));
        ASSERT_TRUE(turn_credential_valid("test-secret", b[0].username, b[0].credential, m.unix));
        ASSERT_FALSE(turn_credential_valid("test-secret", a[0].username, a[0].credential, m.unix + 3600u));        // expired when the time is up
        ASSERT_TRUE(a[0].username != b[0].username && a[0].username != a[1].username);
        const std::string label = a[0].username.substr(a[0].username.find(':') + 1);
        ASSERT_EQ(label.size(), 16u);
        for (const char c : label) ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));                  // (random bytes in hex: nothing that names a person, a seat or a room)
        ASSERT_EQ(m.host->violations(0), 0u);
    } TEST_END();

    TEST_CASE("P25.5 A Server With No Relay Answers P2pConfig With An Empty List: Nobody Is Ready, Nothing Is Relayed, A Signal Is A Violation, And The Match Goes On") {
        P2pRig m(2, false);
        m.say_config(0, true);
        m.say_config(1, true);
        const std::vector<IceConfigMsg> a = m.received<IceConfigMsg>(0, MsgType::IceConfig);
        ASSERT_TRUE(a.size() == 1 && a[0].urls.empty() && a[0].username.empty() && a[0].credential.empty());
        ASSERT_TRUE(m.received<P2pPeersMsg>(0, MsgType::P2pPeers).empty() && m.received<P2pPeersMsg>(1, MsgType::P2pPeers).empty());
        m.say_signal(0, 1, SignalKind::Offer, "v=0\r\n");
        ASSERT_TRUE(m.received<SignalMsg>(1, MsgType::Signal).empty());
        ASSERT_EQ(m.host->violations(0), 1u);
        // a relay that has no secret (an empty signer) is no relay either
        auto turn = std::make_shared<TurnSettings>();
        turn->urls = {"stun:relay.example:3478"};
        ASSERT_FALSE(turn->usable());
    } TEST_END();

    TEST_CASE("P25.6 A Pair Is Connected Directly Only When Both Allow It: An Address Is Passed Only Then, A Relay Candidate Always, And A Page That Sends An Address Anyway Is Counted And Not Passed On") {
        P2pRig m(2);
        m.say_config(0, false);
        m.say_config(1, false);
        m.say_signal(0, 1, SignalKind::Candidate, std::string("0|") + kRelayCandidate);
        ASSERT_EQ(m.received<SignalMsg>(1, MsgType::Signal).size(), 1u);             // a relay candidate always goes
        m.say_signal(0, 1, SignalKind::Candidate, std::string("0|") + kHostCandidate);
        ASSERT_EQ(m.received<SignalMsg>(1, MsgType::Signal).size(), 1u);             // neither allows: not passed
        ASSERT_EQ(m.host->violations(0), 1u);
        P2pRig one(2);
        one.say_config(0, true);
        one.say_config(1, false);
        one.say_signal(0, 1, SignalKind::Candidate, std::string("0|") + kSrflxCandidate);
        one.say_signal(1, 0, SignalKind::Candidate, std::string("0|") + kHostCandidate);
        ASSERT_TRUE(one.received<SignalMsg>(1, MsgType::Signal).empty() && one.received<SignalMsg>(0, MsgType::Signal).empty());   // one allows, the other does not: not passed either way
        ASSERT_TRUE(one.host->violations(0) == 1 && one.host->violations(1) == 1);
        P2pRig both(2);
        both.say_config(0, true);
        both.say_config(1, true);
        both.say_signal(0, 1, SignalKind::Candidate, std::string("0|") + kHostCandidate);
        both.say_signal(1, 0, SignalKind::Candidate, std::string("0|") + kSrflxCandidate);
        const std::vector<SignalMsg> to1 = both.received<SignalMsg>(1, MsgType::Signal);
        const std::vector<SignalMsg> to0 = both.received<SignalMsg>(0, MsgType::Signal);
        ASSERT_TRUE(to1.size() == 1 && to0.size() == 1);
        ASSERT_TRUE(to1[0].peer == 0 && to0[0].peer == 1);                           // the sender's seat is stamped by the connection: the page names the one it is for
        ASSERT_TRUE(to1[0].text == std::string("0|") + kHostCandidate && to1[0].kind == SignalKind::Candidate);
        ASSERT_EQ(both.host->violations(0) + both.host->violations(1), 0u);
        // an offer whose connection line shows an address is no different from a candidate
        P2pRig sdp(2);
        sdp.say_config(0, false);
        sdp.say_config(1, true);
        sdp.say_signal(0, 1, SignalKind::Offer, "v=0\r\nc=IN IP4 198.51.100.4\r\n");
        sdp.say_signal(0, 1, SignalKind::Offer, "v=0\r\nc=IN IP4 0.0.0.0\r\n");
        ASSERT_EQ(sdp.received<SignalMsg>(1, MsgType::Signal).size(), 1u);
    } TEST_END();

    TEST_CASE("P25.7 The Players Who Are Ready Are Told Who Is Ready And Who Allows Direct, When It Changes: A Player Who Arrives Or Leaves Is A Change, Others Are Told Nothing") {
        P2pRig m(3);
        m.say_config(0, false);
        std::vector<P2pPeersMsg> p0 = m.received<P2pPeersMsg>(0, MsgType::P2pPeers);
        ASSERT_TRUE(p0.size() == 1 && p0[0].ready == 0x01 && p0[0].direct == 0x00);
        ASSERT_TRUE(m.received<P2pPeersMsg>(1, MsgType::P2pPeers).empty() && m.received<P2pPeersMsg>(2, MsgType::P2pPeers).empty());       // (seat 1 and 2 have not asked: they are told nothing)
        m.say_config(2, true);
        p0 = m.received<P2pPeersMsg>(0, MsgType::P2pPeers);
        const std::vector<P2pPeersMsg> p2 = m.received<P2pPeersMsg>(2, MsgType::P2pPeers);
        ASSERT_TRUE(p0.size() == 2 && p0[1].ready == 0x05 && p0[1].direct == 0x04);
        ASSERT_TRUE(p2.size() == 1 && p2[0].ready == 0x05 && p2[0].direct == 0x04);
        m.run(1000);
        ASSERT_EQ(m.received<P2pPeersMsg>(0, MsgType::P2pPeers).size(), 2u);        // nothing changed: nothing is sent again
        m.say_config(2, false);                                                      // seat 2 takes its permission back
        p0 = m.received<P2pPeersMsg>(0, MsgType::P2pPeers);
        ASSERT_TRUE(p0.size() == 3 && p0[2].ready == 0x05 && p0[2].direct == 0x00);
        m.send(2, std::vector<uint8_t>{static_cast<uint8_t>(MsgType::Leave)});      // seat 2 leaves
        m.run(300);
        p0 = m.received<P2pPeersMsg>(0, MsgType::P2pPeers);
        ASSERT_TRUE(p0.size() == 4 && p0[3].ready == 0x01);
        // a signal for a player who is not here, or not ready, goes nowhere and costs nothing
        m.say_signal(0, 2, SignalKind::Offer, "v=0\r\n");
        m.say_signal(0, 1, SignalKind::Offer, "v=0\r\n");
        ASSERT_TRUE(m.received<SignalMsg>(1, MsgType::Signal).empty());
        ASSERT_EQ(m.host->violations(0), 0u);
    } TEST_END();

    TEST_CASE("P25.8 A Page That Floods The Server With P2pConfigs And Signals, Signals Itself, Or Sends Frames That Do Not Decode Is Counted And Thrown Out With The Others' Violations") {
        P2pRig m(2);
        for (int i = 0; i < 40 && m.host->violations(0) < 8; ++i) m.say_config(0, false);
        ASSERT_TRUE(m.host->violations(0) > 0);                                      // after 16, every ask is a violation (a credential is made for each)
        ASSERT_EQ(m.received<IceConfigMsg>(0, MsgType::IceConfig).size(), 16u);
        P2pRig s(2);
        s.say_config(0, false);
        s.say_config(1, false);
        s.say_signal(0, 0, SignalKind::Offer, "v=0\r\n");                            // for itself
        ASSERT_EQ(s.host->violations(0), 1u);
        s.send(0, std::vector<uint8_t>{static_cast<uint8_t>(MsgType::Signal), 9, 1, 0, 0});     // a seat that is not one
        s.send(0, std::vector<uint8_t>{static_cast<uint8_t>(MsgType::P2pConfig), 7});
        s.send(0, std::vector<uint8_t>{static_cast<uint8_t>(MsgType::IceConfig)});             // host-bound messages that only a server sends
        s.send(0, encode(P2pPeersMsg{1, 0}));
        s.run(300);
        ASSERT_EQ(s.host->violations(0), 5u);
        P2pRig f(2);
        f.say_config(0, false);
        f.say_config(1, false);
        for (int i = 0; i < 600; ++i) f.send(0, encode(SignalMsg{1, SignalKind::Candidate, std::string("0|") + kRelayCandidate}));
        f.run(3000);
        ASSERT_TRUE(f.host->violations(0) > 0 || !f.host->client_present(0));       // (the signals that are over the limit count)
        ASSERT_TRUE(f.received<SignalMsg>(1, MsgType::Signal).size() <= 512u);
    } TEST_END();
}

void run_bot_hint_tests() {
    TEST_CASE("P25.9 A Computer Player's Order Is Sent To Every Player The Moment It Is Given, With The Tick Of The Turn That Will Carry It: The Turn Carries The Same Order There") {
        P2pRig m(2, true, 2);                                                        // seats 0 and 1 are persons, seats 2 and 3 are computer players
        m.run(500);
        const Command first = bot_walk(m.ids, 2, 20, 40);
        ASSERT_TRUE(m.host->submit_bot(2, first));
        const uint32_t expected_tick = m.host->turns_sealed();
        ASSERT_TRUE(m.host->submit_bot(3, bot_walk(m.ids, 3, 40, 20)));
        ASSERT_TRUE(m.host->submit_bot(2, cmd(CommandType::Stop, 2, 255, 0, 0, m.ids.ants[2])));
        ASSERT_TRUE(m.host->submit_bot(2, cmd(CommandType::Hatch, 2)));              // (a kind that is not predicted: no hint)
        m.run(500);
        for (const size_t person : {size_t{0}, size_t{1}}) {
            size_t on_the_wire = 0;
            for (const auto& raw : m.got[person]) on_the_wire += peek_type(raw) == MsgType::Hint ? 1u : 0u;
            ASSERT_EQ(on_the_wire, 3u);                                               // (the Hatch is not sent at all: a message that a decoder would refuse is no hint either)
            const std::vector<HintMsg> hints = m.received<HintMsg>(person, MsgType::Hint);
            ASSERT_EQ(hints.size(), 3u);
            ASSERT_TRUE(hints[0].command == first);
            ASSERT_EQ(hints[0].tick, expected_tick);
            ASSERT_TRUE(hints[0].seq == 1 && hints[1].seq == 1 && hints[2].seq == 2);        // (a number for each seat, from 1)
            ASSERT_EQ(hints[1].command.issuer, 3);
        }
        // the turn that the order was sealed into is the tick of its hint, and holds the order (the hint told the truth)
        bool found = false;
        for (const TurnMsg& t : m.sealed) {
            for (const Command& c : t.commands) {
                if (c == m.received<HintMsg>(0, MsgType::Hint)[0].command) {
                    found = true;
                    ASSERT_EQ(t.turn, expected_tick);
                }
            }
        }
        ASSERT_TRUE(found);
        // an order that the sequencer refuses (more than a seat may give in one turn) is no hint: only what it took is sent
        P2pRig flood(2, true, 2);
        flood.run(300);
        size_t taken = 0;
        for (int i = 0; i < 1500; ++i) taken += flood.host->submit_bot(2, bot_walk(flood.ids, 2, static_cast<int16_t>(10 + i % 40), 20)) ? 1u : 0u;
        flood.run(300);
        size_t sent = 0;
        for (const auto& raw : flood.got[0]) sent += peek_type(raw) == MsgType::Hint ? 1u : 0u;
        ASSERT_TRUE(taken > 64 && taken < 1500);
        ASSERT_EQ(sent, taken);
        // a server that is told not to sends none; a seat that is paused or a match that has not started has no hints either
        P2pRig quiet(2, true, 2, false);
        quiet.run(300);
        ASSERT_TRUE(quiet.host->submit_bot(2, bot_walk(quiet.ids, 2, 20, 40)));
        quiet.run(300);
        ASSERT_TRUE(quiet.received<HintMsg>(0, MsgType::Hint).empty());
    } TEST_END();

    TEST_CASE("P25.10 Hints, Credentials And Signals Change Nothing That The Server Seals Or The Machines Run: The Same Turns And The Same State Hashes With And Without Them") {
        const auto play = [](bool traffic, std::vector<TurnMsg>& turns, uint64_t& referee_hash, uint64_t& client_hash) {
            P2pRig m(2, true, 2, traffic);
            for (int step = 0; step < 60; ++step) {
                if (traffic && step == 5) {
                    m.send(0, encode(P2pConfigMsg{true}));
                    m.send(1, encode(P2pConfigMsg{true}));
                }
                if (traffic && step == 12) m.send(0, encode(SignalMsg{1, SignalKind::Offer, "v=0\r\nc=IN IP4 0.0.0.0\r\n"}));
                if (step % 7 == 0) {
                    m.host->submit_bot(2, bot_walk(m.ids, 2, static_cast<int16_t>(10 + step % 40), 22));
                    m.host->submit_bot(3, bot_walk(m.ids, 3, 22, static_cast<int16_t>(10 + step % 40)));
                }
                if (step % 9 == 0) m.clients[0]->submit(bot_walk(m.ids, 0, static_cast<int16_t>(12 + step % 30), 30));
                m.run(50);
            }
            m.host->freeze();
            m.run(2000);
            turns = m.sealed;
            referee_hash = m.referee.state_hash().total;
            client_hash = m.sims[0]->state_hash().total;
            ASSERT_EQ(client_hash, referee_hash);
            ASSERT_EQ(m.sims[1]->state_hash().total, referee_hash);
        };
        std::vector<TurnMsg> with_turns;
        std::vector<TurnMsg> without_turns;
        uint64_t with_ref = 0, with_client = 0, without_ref = 0, without_client = 0;
        play(true, with_turns, with_ref, with_client);
        play(false, without_turns, without_ref, without_client);
        ASSERT_TRUE(with_turns.size() > 40u);
        ASSERT_EQ(with_ref, without_ref);
        ASSERT_EQ(with_client, without_client);
        ASSERT_EQ(with_turns.size(), without_turns.size());
        for (size_t i = 0; i < with_turns.size(); ++i) ASSERT_TRUE(with_turns[i].turn == without_turns[i].turn && with_turns[i].commands == without_turns[i].commands);
    } TEST_END();
}

}  // namespace

void run_p2p_tests() {
    run_message_tests();
    run_credential_tests();
    run_rule_tests();
    run_session_tests();
    run_bot_hint_tests();
}
