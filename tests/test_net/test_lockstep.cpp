// Tests of the lock-step network core (docs/NETWORK_PORT.md): the wire protocol, the in-memory network, the host's turn sequencer, the client's
// turn runner, and whole matches of a host and three clients over links with latency and jitter, including a desync, a stalled peer, a dropped
// peer and a hostile peer.
#include "ants_net/loopback.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/sequencer.hpp"
#include "ants_net/session.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace ants;
using namespace ants::net;
using ants::sim::AntType;
using ants::sim::Command;
using ants::sim::CommandType;
using ants::sim::TileCoord;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))

namespace {

// A Hello with a version and a name (the other fields at their defaults: the message has grown with the protocol, an aggregate initializer would miss fields)
HelloMsg hello_of(uint16_t version, const char* name) {
    HelloMsg h;
    h.version = version;
    h.name = name;
    return h;
}


struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

Command cmd(CommandType type, uint8_t issuer, uint8_t other = 255, int16_t x = 0, int16_t y = 0, std::vector<uint32_t> ants = {}) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.other_player = other;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = std::move(ants);
    return c;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The scenario: four players on a 60 x 60 map, every ant type, a river with a bridge, a lunchbox and a bomb (the same world on every machine)
// ---------------------------------------------------------------------------------------------------------------------------------

struct Ids {
    std::vector<uint32_t> ants[sim::MAX_PLAYERS];
};

Ids build_world(sim::SimulationEngine& sim, uint32_t seed) {
    Ids ids;
    sim.init_test_world(60, 60, seed, 720000);
    const TileCoord hills[sim::MAX_PLAYERS] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) sim.grid_mut().set_anthill(p, hills[p]);
    for (int32_t y = 0; y < 60; ++y) sim.grid_mut().set_terrain(30, y, sim::TERRAIN_WATER);
    for (int step = 0; step < 4; ++step) sim.grid_mut().advance_bridge(30, 30, 0);
    sim.grid_mut().drop_lunchbox(20, 20, 25);
    sim.grid_mut().place_bomb(25, 25, 1);
    const AntType types[6] = {AntType::Worker, AntType::Bomber, AntType::Fire, AntType::Thief, AntType::Combat, AntType::Swimmer};
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        for (int i = 0; i < 6; ++i) ids.ants[p].push_back(sim.spawn_unit(p, types[i], TileCoord{hills[p].x + 1 + i, hills[p].y + 6}));
    }
    return ids;
}

// What player p does at time t (ms): every 400 ms + p * 100 something random with its own ants
bool script(const Ids& ids, uint32_t seed, uint32_t t, uint8_t p, Command& out) {
    if (t % 400 != p * 100u) return false;
    Lcg rng(seed * 7919u + t * 104729u + p * 31u);
    const uint32_t kind = rng.below(10);
    std::vector<uint32_t> pick;
    for (uint32_t id : ids.ants[p]) {
        if (rng.below(3) != 0) pick.push_back(id);
    }
    if (pick.empty()) pick.push_back(ids.ants[p][0]);
    const int16_t x = static_cast<int16_t>(rng.below(60));
    const int16_t y = static_cast<int16_t>(rng.below(60));
    if (kind < 4) out = cmd(CommandType::GroupMove, p, 255, x, y, pick);
    else if (kind < 6) out = cmd(CommandType::GroupAttack, p, 255, x, y, pick);
    else if (kind < 8) out = cmd(CommandType::GroupSpecial, p, 255, x, y, pick);
    else if (kind == 8) out = cmd(CommandType::Stop, p, 255, 0, 0, pick);
    else out = cmd(CommandType::Hatch, p);
    return true;
}

// A match of a host (player 0) and clients (players 1 .. players-1) over a loopback network
struct Match {
    LoopbackNetwork net;
    std::vector<std::unique_ptr<sim::SimulationEngine>> sims;
    Ids ids;
    std::unique_ptr<HostSession> host;
    std::vector<std::unique_ptr<ClientSession>> clients;   // index = player - 1
    std::vector<Connection*> host_ends;                    // host_ends[player]
    std::vector<Connection*> client_ends;
    uint32_t now{0};
    uint32_t seed{1};
    uint8_t players{4};

    Match(uint32_t seed_, uint8_t players_, LoopbackNetwork::Link link, HostSession::Config hc = {}, ClientSession::Config cc = {})
        : net(seed_ * 13u), seed(seed_), players(players_) {
        for (uint8_t p = 0; p < players; ++p) {
            sims.push_back(std::make_unique<sim::SimulationEngine>());
            ids = build_world(*sims.back(), seed);
        }
        hc.host_player = 0;
        host = std::make_unique<HostSession>(*sims[0], hc);
        host_ends.assign(players, nullptr);
        client_ends.assign(players, nullptr);
        for (uint8_t p = 1; p < players; ++p) {
            auto ends = net.connect(link);
            host_ends[p] = ends.first;
            client_ends[p] = ends.second;
            host->add_client(p, ends.first);
            ClientSession::Config c = cc;
            c.player = p;
            clients.push_back(std::make_unique<ClientSession>(*sims[p], c));
            clients.back()->set_connection(ends.second);
        }
        host->start(0);
        for (auto& c : clients) c->start(0);
    }

    // Advances virtual time by `ms` in 10 ms steps; players issue their scripted commands when `scripted`
    void run(uint32_t ms, bool scripted = true, const std::function<void(uint32_t)>& each_step = {}) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            net.set_time(now);
            if (scripted) {
                Command c;
                if (script(ids, seed, now, 0, c)) host->submit_local(c);
                for (uint8_t p = 1; p < players; ++p) {
                    if (script(ids, seed, now, p, c)) clients[p - 1]->submit(c);
                }
            }
            if (each_step) each_step(now);
            host->update(now);
            for (auto& c : clients) c->update(now);
        }
    }
    // Stops the host from sealing and lets everything in flight arrive and execute (no new commands): afterwards all machines are at the same turn
    void settle(uint32_t ms = 3000) {
        host->freeze();
        run(ms, false);
    }
    bool all_equal() const {
        for (size_t i = 1; i < sims.size(); ++i) {
            if (sims[i]->state_hash() != sims[0]->state_hash()) return false;
        }
        return true;
    }
};

// A dedicated server's match: the host has NO seat (kNoSeat) and runs the match on its own engine as the referee; every seat 0 .. players - 1 is a client.
struct ServerMatch {
    LoopbackNetwork net;
    sim::SimulationEngine referee;
    std::vector<std::unique_ptr<sim::SimulationEngine>> sims;       // index = seat
    Ids ids;
    std::unique_ptr<HostSession> host;
    std::vector<std::unique_ptr<ClientSession>> clients;            // index = seat
    std::vector<Connection*> host_ends;
    std::vector<Connection*> client_ends;
    uint32_t now{0};
    uint32_t seed{1};
    uint8_t players{4};

    ServerMatch(uint32_t seed_, uint8_t players_, LoopbackNetwork::Link link, HostSession::Config hc = {}, ClientSession::Config cc = {})
        : net(seed_ * 17u), seed(seed_), players(players_) {
        ids = build_world(referee, seed);
        hc.host_player = kNoSeat;
        host = std::make_unique<HostSession>(referee, hc);
        for (uint8_t p = 0; p < players; ++p) {
            sims.push_back(std::make_unique<sim::SimulationEngine>());
            build_world(*sims.back(), seed);
            auto ends = net.connect(link);
            host_ends.push_back(ends.first);
            client_ends.push_back(ends.second);
            host->add_client(p, ends.first);
            ClientSession::Config c = cc;
            c.player = p;
            c.host = kNoSeat;
            c.migration = false;
            clients.push_back(std::make_unique<ClientSession>(*sims[p], c));
            clients.back()->set_connection(ends.second);
        }
        host->start(0);
        for (auto& c : clients) c->start(0);
    }
    void run(uint32_t ms, bool scripted = true, const std::function<void(uint32_t)>& each_step = {}) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            net.set_time(now);
            if (scripted) {
                Command c;
                for (uint8_t p = 0; p < players; ++p) {
                    if (script(ids, seed, now, p, c)) clients[p]->submit(c);
                }
            }
            if (each_step) each_step(now);
            host->update(now);
            for (auto& c : clients) c->update(now);
        }
    }
    void settle(uint32_t ms = 3000) {
        host->freeze();
        run(ms, false);
    }
    bool all_equal() const {
        for (const auto& s : sims) {
            if (s->state_hash() != referee.state_hash()) return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------

// True when any decoder of the protocol accepts the bytes
bool any_decodes(const std::vector<uint8_t>& b) {
    HelloMsg m1;
    WelcomeMsg m2;
    RejectMsg m3;
    CommandMsg m4;
    TurnMsg m5;
    AckMsg m6;
    HashMsg m7;
    DesyncMsg m8;
    ChatMsg m9;
    RoomMsg m10;
    StartMsg m11;
    LoadedMsg m12;
    CancelMsg m13;
    ProposeMsg m14;
    AcceptMsg m15;
    RefuseMsg m16;
    ResumeMsg m17;
    RequestMsg m18;
    PeerHelloMsg m19;
    PingMsg m20;
    StartRequestMsg m21;
    return decode(b, m1) || decode(b, m2) || decode(b, m3) || decode(b, m4) || decode(b, m5) || decode(b, m6) || decode(b, m7) || decode(b, m8) ||
           decode(b, m9) || decode(b, m10) || decode(b, m11) || decode(b, m12) || decode(b, m13) || decode(b, m14) || decode(b, m15) ||
           decode(b, m16) || decode(b, m17) || decode(b, m18) || decode(b, m19) || decode_ping(b.data(), b.size(), m20) || decode(b, m21);
}

// A dedicated server's room as the Room message shows it (protocol 7): three guests (no Host slot), the first of them the leader
RoomMsg server_room_of(uint8_t leader) {
    RoomMsg r;
    r.slots[0] = {SlotState::Client, "Ann", 30};
    r.slots[1] = {SlotState::Client, "Bob", kRttUnknown};
    r.slots[3] = {SlotState::Client, "Cat", 1500};
    r.map_name = "SMALL.LVL";
    r.you = 1;
    r.leader = leader;
    return r;
}

void run_protocol_tests() {
    TEST_CASE("N2.1 Protocol: Every Message Round-Trips And Trailing Or Missing Bytes Are Rejected") {
        HelloMsg hello;
        hello.name = "Queen Ant";
        HelloMsg h2;
        ASSERT_TRUE(decode(encode(hello), h2) && h2.name == "Queen Ant" && h2.version == kProtocolVersion);
        WelcomeMsg w;
        w.player = 2;
        w.players = 4;
        WelcomeMsg w2;
        ASSERT_TRUE(decode(encode(w), w2) && w2.player == 2 && w2.players == 4);
        RejectMsg r;
        r.reason = RejectReason::Full;
        RejectMsg r2;
        ASSERT_TRUE(decode(encode(r), r2) && r2.reason == RejectReason::Full);
        CommandMsg cm;
        cm.command = cmd(CommandType::GroupAttack, 3, 255, 12, 34, {5, 6, 7});
        CommandMsg cm2;
        ASSERT_TRUE(decode(encode(cm), cm2) && cm2.command == cm.command);
        TurnMsg t;
        t.turn = 123456;
        t.commands = {cmd(CommandType::GroupMove, 0, 255, 1, 2, {1}), cmd(CommandType::Hatch, 1), cmd(CommandType::AllianceInvite, 2, 3)};
        TurnMsg t2;
        ASSERT_TRUE(decode(encode(t), t2) && t2.turn == 123456 && t2.commands == t.commands);
        TurnMsg empty;
        empty.turn = 7;
        ASSERT_TRUE(decode(encode(empty), t2) && t2.turn == 7 && t2.commands.empty());
        AckMsg a;
        a.turn = 99;
        AckMsg a2;
        ASSERT_TRUE(decode(encode(a), a2) && a2.turn == 99);
        HashMsg hm;
        hm.turn = 19;
        hm.hash = {1, 2, 3, 4, 5, 6, 7, 8};
        HashMsg hm2;
        ASSERT_TRUE(decode(encode(hm), hm2) && hm2.turn == 19 && hm2.hash.total == 1 && hm2.hash.droppers == 8);
        DesyncMsg d;
        d.turn = 29;
        d.player = 2;
        d.host = {1, 1, 1, 1, 1, 1, 1, 1};
        d.peer = {2, 2, 2, 2, 2, 2, 2, 2};
        DesyncMsg d2;
        ASSERT_TRUE(decode(encode(d), d2) && d2.player == 2 && d2.peer.total == 2);
        ChatMsg c;
        c.sender = 1;
        c.team = true;
        c.text = "Attack the north hill!";
        ChatMsg c2;
        ASSERT_TRUE(decode(encode(c), c2) && c2.team && c2.text == c.text && c2.sender == 1);
        PingMsg p;
        p.nonce = 5;
        p.sent_ms = 1000;
        PingMsg p2;
        ASSERT_TRUE(decode_ping(encode_ping(p).data(), 9, p2) && p2.nonce == 5 && p2.sent_ms == 1000);
        ASSERT_EQ(peek_type(encode_pong(p)), MsgType::Pong);
        // the messages of host migration and the mesh
        ProposeMsg pr;
        pr.epoch = 3;
        pr.candidate = 2;
        ProposeMsg pr2;
        ASSERT_TRUE(decode(encode(pr), pr2) && pr2.epoch == 3 && pr2.candidate == 2);
        AcceptMsg ac;
        ac.epoch = 1;
        ac.next_receive = 500;
        ac.next_execute = 498;
        AcceptMsg ac2;
        ASSERT_TRUE(decode(encode(ac), ac2) && ac2.epoch == 1 && ac2.next_receive == 500 && ac2.next_execute == 498);
        RefuseMsg rf;
        rf.epoch = 2;
        rf.lowest = 1;
        RefuseMsg rf2;
        ASSERT_TRUE(decode(encode(rf), rf2) && rf2.epoch == 2 && rf2.lowest == 1);
        ResumeMsg rs;
        rs.epoch = 1;
        rs.host = 2;
        rs.resume_turn = 4242;
        ResumeMsg rs2;
        ASSERT_TRUE(decode(encode(rs), rs2) && rs2.epoch == 1 && rs2.host == 2 && rs2.resume_turn == 4242);
        RequestMsg rq;
        rq.from_turn = 77;
        RequestMsg rq2;
        ASSERT_TRUE(decode(encode(rq), rq2) && rq2.from_turn == 77);
        PeerHelloMsg ph;
        ph.seat = 3;
        PeerHelloMsg ph2;
        ASSERT_TRUE(decode(encode(ph), ph2) && ph2.seat == 3);
        // the port a guest accepts the other guests on travels with its Hello; the host hands every guest the others' endpoints in Start
        HelloMsg hello_port;
        hello_port.name = "Ann";
        hello_port.listen_port = 41234;
        HelloMsg hello_port2;
        ASSERT_TRUE(decode(encode(hello_port), hello_port2) && hello_port2.listen_port == 41234 && hello_port2.name == "Ann");
        StartMsg start;
        start.map_name = "TREASURE.LVL";
        start.roster = 0x0D;
        start.endpoints[2] = Endpoint{"192.0.2.22", 5001};
        start.endpoints[3] = Endpoint{"fe80::1%en0", 5002};
        StartMsg start2;
        ASSERT_TRUE(decode(encode(start), start2) && start2.endpoints[2] == start.endpoints[2] && start2.endpoints[3] == start.endpoints[3] &&
                    start2.endpoints[0].address.empty() && start2.endpoints[1].port == 0);
        // protocol 7: the room names its leader, and the leader's request to start is a message of its own
        RoomMsg led = server_room_of(3);
        RoomMsg led2;
        ASSERT_TRUE(decode(encode(led), led2) && led2.leader == 3 && led2.you == 1 && led2.map_name == "SMALL.LVL" && led2.slots[3].name == "Cat" && led2.slots[2].state == SlotState::Empty);
        StartRequestMsg sr;
        ASSERT_TRUE(decode(encode(sr), sr) && peek_type(encode(sr)) == MsgType::StartRequest);
        // every valid message is rejected with a byte too many and with any byte missing
        const std::vector<std::vector<uint8_t>> all = {encode(hello), encode(w),  encode(r),  encode(cm), encode(t),  encode(a),  encode(hm),
                                                       encode(d),     encode(c),  encode(pr), encode(ac), encode(rf), encode(rs), encode(rq),
                                                       encode(ph),    encode(hello_port), encode(start), encode(led), encode(sr), encode(server_room_of(255))};
        for (const auto& m : all) {
            std::vector<uint8_t> longer = m;
            longer.push_back(0);
            for (size_t cut = 0; cut < m.size(); ++cut) {
                std::vector<uint8_t> shorter(m.begin(), m.begin() + static_cast<std::ptrdiff_t>(cut));
                ASSERT_FALSE(any_decodes(shorter));
            }
            ASSERT_FALSE(any_decodes(longer));
        }
    } TEST_END();

    TEST_CASE("N2.2 Protocol: Out-Of-Range Fields Are Rejected (players, counts, reasons, names, chat)") {
        WelcomeMsg w;
        auto bytes = encode(WelcomeMsg{2, 4});
        bytes[1] = 4;                                  // player 4 does not exist
        ASSERT_FALSE(decode(bytes, w));
        bytes = encode(WelcomeMsg{2, 4});
        bytes[2] = 1;                                  // a room needs two players at least
        ASSERT_FALSE(decode(bytes, w));
        bytes[2] = 5;
        ASSERT_FALSE(decode(bytes, w));
        RejectMsg r;
        bytes = encode(RejectMsg{RejectReason::Full});
        bytes[1] = 0;
        ASSERT_FALSE(decode(bytes, r));
        bytes[1] = 7;                                                    // the reasons are 1 .. 6 (6 = NoSuchRoom, protocol 6)
        ASSERT_FALSE(decode(bytes, r));
        bytes[1] = 6;
        ASSERT_TRUE(decode(bytes, r) && r.reason == RejectReason::NoSuchRoom);
        DesyncMsg d;
        DesyncMsg good;
        good.player = 1;
        bytes = encode(good);
        bytes[5] = 4;                                  // the peer's player number
        ASSERT_FALSE(decode(bytes, d));
        // a turn that claims more commands than fit, or than the limit
        TurnMsg t;
        t.turn = 1;
        bytes = encode(t);
        bytes[5] = 0xFF;                               // count = 65280 + 0 ...
        bytes[6] = 0xFF;
        ASSERT_FALSE(decode(bytes, t));
        bytes = encode(t);
        bytes[5] = 1;                                  // one command promised, none present
        ASSERT_FALSE(decode(bytes, t));
        // chat: non-printable bytes and an overlong text
        ChatMsg c;
        bytes = encode(ChatMsg{1, false, "hi"});
        bytes[4] = 1;                                  // control character in the text
        ASSERT_FALSE(decode(bytes, c));
        bytes = encode(ChatMsg{1, false, "hi"});
        bytes[2] = 2;                                  // team flag must be 0 or 1
        ASSERT_FALSE(decode(bytes, c));
        ChatMsg longtext;
        longtext.text = std::string(300, 'x');
        ChatMsg back;
        ASSERT_TRUE(decode(encode(longtext), back) && back.text.size() == kMaxChatChars);   // clipped by the encoder
        // a name with a control character
        HelloMsg h;
        bytes = encode(hello_of(kProtocolVersion, "Bob"));
        bytes[4] = 7;
        ASSERT_FALSE(decode(bytes, h));
        // host migration: seats out of range, an Accept that executed more than it received, endpoints that are no addresses
        ProposeMsg pr;
        bytes = encode(ProposeMsg{1, 2});
        bytes[2] = 4;
        ASSERT_FALSE(decode(bytes, pr));
        RefuseMsg rf;
        bytes = encode(RefuseMsg{1, 2});
        bytes[2] = 4;
        ASSERT_FALSE(decode(bytes, rf));
        ResumeMsg rs;
        bytes = encode(ResumeMsg{1, 2, 10});
        bytes[2] = 4;
        ASSERT_FALSE(decode(bytes, rs));
        PeerHelloMsg ph;
        bytes = encode(PeerHelloMsg{2});
        bytes[1] = 4;
        ASSERT_FALSE(decode(bytes, ph));
        AcceptMsg ac;
        AcceptMsg bad_accept;
        bad_accept.next_receive = 10;
        bad_accept.next_execute = 11;
        ASSERT_FALSE(decode(encode(bad_accept), ac));
        StartMsg start;
        start.map_name = "TREASURE.LVL";
        start.roster = 0x03;
        StartMsg start_back;
        start.endpoints[1].address = "bad address";
        ASSERT_FALSE(decode(encode(start), start_back));
        start.endpoints[1].address = std::string(65, '1');                    // too long to travel: the encoder sends none
        ASSERT_TRUE(decode(encode(start), start_back) && start_back.endpoints[1].address.empty());
        start.endpoints[1].address = "198.51.100.7";
        ASSERT_TRUE(decode(encode(start), start_back) && start_back.endpoints[1].address == "198.51.100.7");
        // unknown types (24 was one until protocol 7 gave it to StartRequest)
        for (uint8_t type : std::vector<uint8_t>{0, 25, 100, 255}) {
            const std::vector<uint8_t> m = {type, 0, 0, 0, 0};
            ASSERT_EQ(peek_type(m), MsgType::None);
        }
        ASSERT_EQ(peek_type(std::vector<uint8_t>{24}), MsgType::StartRequest);
        ASSERT_EQ(static_cast<int>(MsgType::Last), 24);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{}), MsgType::None);
    } TEST_END();

    TEST_CASE("N2.3 Protocol: 400000 Random And Mutated Messages Never Crash And Only Give Messages That Encode Back To The Same Bytes") {
        Lcg rng(99);
        TurnMsg turn;
        turn.turn = 1000;
        turn.commands = {cmd(CommandType::GroupMove, 1, 255, 3, 4, {1, 2}), cmd(CommandType::Stop, 2, 255, 0, 0, {9}), cmd(CommandType::Hatch, 0)};
        HashMsg hash;
        hash.turn = 10;
        hash.hash = {1, 2, 3, 4, 5, 6, 7, 8};
        const std::vector<std::vector<uint8_t>> seeds = {encode(turn), encode(hash), encode(CommandMsg{cmd(CommandType::GroupAttack, 2, 255, 5, 5, {1})}),
                                                         encode(ChatMsg{1, true, "hello"}), encode(hello_of(1, "Ann")), encode(WelcomeMsg{1, 4}),
                                                         encode(ProposeMsg{1, 2}), encode(AcceptMsg{1, 100, 98}), encode(RefuseMsg{1, 1}),
                                                         encode(ResumeMsg{1, 2, 500}), encode(RequestMsg{40}), encode(PeerHelloMsg{3}),
                                                         encode(server_room_of(0)), encode(server_room_of(255)), encode(StartRequestMsg{})};
        size_t accepted = 0;
        for (int i = 0; i < 400000; ++i) {
            std::vector<uint8_t> buf;
            if (i % 3 == 0) {
                buf.resize(rng.below(80));
                for (auto& x : buf) x = static_cast<uint8_t>(rng.below(256));
                if (!buf.empty()) buf[0] = static_cast<uint8_t>(1 + rng.below(24));    // a plausible type byte
            } else {
                buf = seeds[rng.below(static_cast<uint32_t>(seeds.size()))];
                for (uint32_t m = 1 + rng.below(3); m > 0; --m) buf[rng.below(static_cast<uint32_t>(buf.size()))] = static_cast<uint8_t>(rng.below(256));
                if (rng.below(5) == 0) buf.resize(rng.below(static_cast<uint32_t>(buf.size()) + 1));
            }
            TurnMsg t;
            HashMsg h;
            CommandMsg c;
            ChatMsg ch;
            HelloMsg he;
            WelcomeMsg we;
            ProposeMsg pr;
            AcceptMsg ac;
            RefuseMsg rf;
            ResumeMsg rs;
            RequestMsg rq;
            PeerHelloMsg ph;
            RoomMsg room;
            StartRequestMsg sreq;
            if (decode(buf, t)) {
                ++accepted;
                ASSERT_TRUE(encode(t) == buf);
                ASSERT_TRUE(t.commands.size() <= kMaxTurnCommands);
            }
            if (decode(buf, h)) { ++accepted; ASSERT_TRUE(encode(h) == buf); }
            if (decode(buf, c)) { ++accepted; ASSERT_TRUE(encode(c) == buf); }
            if (decode(buf, ch)) { ++accepted; ASSERT_TRUE(encode(ch) == buf); }
            if (decode(buf, he)) { ++accepted; ASSERT_TRUE(encode(he) == buf); }
            if (decode(buf, we)) { ++accepted; ASSERT_TRUE(encode(we) == buf); }
            if (decode(buf, pr)) { ++accepted; ASSERT_TRUE(encode(pr) == buf); }
            if (decode(buf, ac)) { ++accepted; ASSERT_TRUE(encode(ac) == buf); }
            if (decode(buf, rf)) { ++accepted; ASSERT_TRUE(encode(rf) == buf); }
            if (decode(buf, rs)) { ++accepted; ASSERT_TRUE(encode(rs) == buf); }
            if (decode(buf, rq)) { ++accepted; ASSERT_TRUE(encode(rq) == buf); }
            if (decode(buf, ph)) { ++accepted; ASSERT_TRUE(encode(ph) == buf); }
            if (decode(buf, room)) {                   // a Room that gets through names a leader that is a guest's seat (or nobody) and encodes back to the same bytes
                ++accepted;
                ASSERT_TRUE(encode(room) == buf);
                ASSERT_TRUE(room.leader == kNoLeader || (room.leader < sim::MAX_PLAYERS && room.slots[room.leader].state == SlotState::Client));
            }
            if (decode(buf, sreq)) { ++accepted; ASSERT_TRUE(encode(sreq) == buf && buf.size() == 1); }
        }
        ASSERT_TRUE(accepted > 5000);                  // the mutations of valid messages do get through
    } TEST_END();

    TEST_CASE("N2.3b Protocol 7: The Room Names Its Leader (Every Value; Only The Seat Of A Guest Or Nobody), StartRequest Is One Byte And Nothing Else, The Layout Of Protocol 6 Is Refused") {
        ASSERT_EQ(kProtocolVersion, 7);                                    // the Room message grew a byte and a message type was added: a client of protocol 6 cannot play with it
        ASSERT_EQ(kNoLeader, 255);
        // every value of the leader: nobody, and each seat that a guest holds; the byte is the last of the message, the receiver's own seat ("you") the one before it
        for (const uint8_t leader : {uint8_t{255}, uint8_t{0}, uint8_t{1}, uint8_t{3}}) {
            RoomMsg r = server_room_of(leader);
            for (const uint8_t you : {uint8_t{255}, uint8_t{0}, uint8_t{1}, uint8_t{3}}) {                     // (the receiver may or may not be the leader)
                r.you = you;
                const std::vector<uint8_t> bytes = encode(r);
                ASSERT_EQ(bytes[bytes.size() - 1], leader);
                ASSERT_EQ(bytes[bytes.size() - 2], you);
                RoomMsg back;
                ASSERT_TRUE(decode(bytes, back) && back.leader == leader && back.you == you);
                ASSERT_TRUE(encode(back) == bytes);
            }
        }
        {   // a room that names nobody: the default, and what a LAN host's room says
            RoomMsg none;
            RoomMsg back;
            ASSERT_EQ(none.leader, 255);
            ASSERT_TRUE(decode(encode(none), back) && back.leader == kNoLeader);
            RoomMsg lan = none;                                            // a host that holds a seat: no leader
            lan.slots[0] = {SlotState::Host, "Queen", 0};
            lan.slots[1] = {SlotState::Client, "Bob", 20};
            ASSERT_TRUE(decode(encode(lan), back) && back.leader == kNoLeader && back.slots[0].state == SlotState::Host);
        }
        // what a leader may be: the seat of a person who joined as a guest. Anything else is no message.
        const std::vector<uint8_t> base = encode(server_room_of(255));
        RoomMsg out;
        for (unsigned value = 0; value < 256; ++value) {                   // every byte value in the leader's place
            std::vector<uint8_t> bytes = base;
            bytes.back() = static_cast<uint8_t>(value);
            const bool should = value == 255 || value == 0 || value == 1 || value == 3;      // seats 0, 1 and 3 hold guests, seat 2 is empty
            ASSERT_EQ(decode(bytes, out), should);
        }
        {   // a leader on an empty seat, on a bot, on the host
            RoomMsg r = server_room_of(0);
            r.leader = 2;
            ASSERT_FALSE(decode(encode(r), out));                          // empty
            r.slots[2] = {SlotState::Bot, "Bot (Medium)", 0};
            ASSERT_FALSE(decode(encode(r), out));                          // a computer player never leads
            r.slots[2] = {SlotState::Host, "Queen", 0};
            ASSERT_FALSE(decode(encode(r), out));                          // nor does a host that holds a seat
            r.slots[2] = {SlotState::Client, "Dan", 10};
            ASSERT_TRUE(decode(encode(r), out) && out.leader == 2);        // a guest does
        }
        {   // the layout of protocol 6 (no leader byte) is no Room message of this protocol, and neither is one byte too many
            std::vector<uint8_t> v6 = base;
            v6.pop_back();
            ASSERT_FALSE(decode(v6, out));
            std::vector<uint8_t> longer = base;
            longer.push_back(255);
            ASSERT_FALSE(decode(longer, out));
        }
        // StartRequest: exactly the type byte
        const std::vector<uint8_t> request = encode(StartRequestMsg{});
        ASSERT_TRUE(request.size() == 1 && request[0] == 24);
        StartRequestMsg sr;
        ASSERT_TRUE(decode(request, sr));
        ASSERT_FALSE(decode(std::vector<uint8_t>{}, sr));
        ASSERT_FALSE(decode(nullptr, 1, sr));
        for (size_t extra : {size_t{1}, size_t{2}, size_t{9}, size_t{200}, kMaxMessageBytes - 1}) {    // a payload of any size makes it garbage
            std::vector<uint8_t> payload = request;
            payload.resize(1 + extra, 0x5A);
            ASSERT_FALSE(decode(payload, sr));
        }
        for (unsigned type = 0; type < 256; ++type) {                      // no other type byte is a StartRequest
            if (type == 24) continue;
            ASSERT_FALSE(decode(std::vector<uint8_t>{static_cast<uint8_t>(type)}, sr));
        }
        // and the neighbours: no other decoder takes it, and it takes no other message
        ASSERT_FALSE(any_decodes(std::vector<uint8_t>{24, 0}) || any_decodes(std::vector<uint8_t>{}));
        for (const auto& m : {encode(server_room_of(0)), encode(hello_of(kProtocolVersion, "A")), encode_begin(), encode_leave(), encode(WelcomeMsg{1, 4})}) {
            ASSERT_FALSE(decode(m, sr));
        }
        // 20,000 messages made of the leader's neighbours: bytes of the Room that were changed, or cut, or lengthened never give a leader that is not a guest's seat
        Lcg rng(7);
        size_t with_leader = 0;
        for (int i = 0; i < 20000; ++i) {
            std::vector<uint8_t> bytes = encode(server_room_of(static_cast<uint8_t>(i % 2 == 0 ? 0 : 255)));
            const uint32_t pick = rng.below(12);                                                                 // the leader's byte: mostly a seat number or nobody, sometimes any byte
            bytes[bytes.size() - 1] = pick < 8 ? static_cast<uint8_t>(pick) : (pick == 8 ? uint8_t{255} : static_cast<uint8_t>(rng.below(256)));
            if (rng.below(3) == 0) bytes[bytes.size() - 2] = static_cast<uint8_t>(rng.below(256));               // and the receiver's own seat
            if (rng.below(7) == 0) bytes.resize(rng.below(static_cast<uint32_t>(bytes.size()) + 2));
            RoomMsg room;
            if (decode(bytes, room)) {
                with_leader += room.leader == kNoLeader ? 0u : 1u;
                ASSERT_TRUE(room.leader == kNoLeader || room.slots[room.leader].state == SlotState::Client);
                ASSERT_TRUE(room.you == 255 || room.you < sim::MAX_PLAYERS);
                ASSERT_TRUE(encode(room) == bytes);
            }
        }
        ASSERT_TRUE(with_leader > 1000);
    } TEST_END();
}

void run_network_tests() {
    TEST_CASE("N2.4 Loopback Network: Latency Is Respected, Delivery Is Ordered Even With Jitter, Closing Is Seen On Both Ends") {
        LoopbackNetwork net(5);
        auto ends = net.connect({50, 40});
        std::vector<uint8_t> m;
        std::vector<std::pair<uint8_t, uint32_t>> arrivals;         // (message, arrival time)
        std::vector<uint32_t> sent_at(100, 0);
        uint8_t sent = 0;
        for (uint32_t t = 0; t <= 1500; ++t) {
            net.set_time(t);
            if (t % 3 == 0 && sent < 100) {
                sent_at[sent] = t;
                ASSERT_TRUE(ends.first->send({sent}));
                ++sent;
            }
            while (ends.second->poll(m)) arrivals.emplace_back(m[0], t);
        }
        ASSERT_EQ(arrivals.size(), 100u);
        for (size_t i = 0; i < arrivals.size(); ++i) {
            ASSERT_EQ(arrivals[i].first, i);                                   // in order
            ASSERT_TRUE(arrivals[i].second >= sent_at[i] + 50);                // never before the latency
            ASSERT_TRUE(arrivals[i].second <= sent_at[i] + 50 + 40 + 40 * 33); // (a slow message may hold up later ones, but not for long)
        }
        // the other direction, and a message sent now is not there yet
        net.set_time(2000);
        ASSERT_TRUE(ends.second->send({42}));
        ASSERT_FALSE(ends.first->poll(m));
        net.set_time(2000 + 50 + 40);
        ASSERT_TRUE(ends.first->poll(m) && m[0] == 42);
        // close
        ends.first->close();
        ASSERT_EQ(ends.second->state(), Connection::State::Closed);
        ASSERT_FALSE(ends.second->send({1}));
        LoopbackNetwork net2;
        auto e2 = net2.connect({10, 0});
        net2.cut(e2.first, true);
        ASSERT_EQ(e2.second->state(), Connection::State::Failed);
    } TEST_END();
}

void run_sequencer_tests() {
    TEST_CASE("N2.5 Sequencer: The Connection, Not The Payload, Decides The Issuer; Canonical Order; Inactive Slots And Floods Are Refused") {
        Sequencer seq;
        seq.set_host_player(0);
        seq.set_active(0, true);
        seq.set_active(1, true);
        seq.set_active(2, true);
        // player 1 claims to be player 2
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupMove, 2, 255, 1, 1, {5})));
        ASSERT_TRUE(seq.submit(2, cmd(CommandType::Hatch, 2)));
        ASSERT_TRUE(seq.submit(0, cmd(CommandType::Stop, 0, 255, 0, 0, {1})));
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupAttack, 0, 255, 2, 2, {6})));
        ASSERT_FALSE(seq.submit(3, cmd(CommandType::Hatch, 3)));                       // slot 3 is not part of the match
        ASSERT_FALSE(seq.submit(4, cmd(CommandType::Hatch, 3)));
        ASSERT_FALSE(seq.submit(1, cmd(CommandType::None, 1)));
        const TurnMsg t = seq.seal();
        ASSERT_EQ(t.turn, 0u);
        ASSERT_EQ(t.commands.size(), 4u);
        ASSERT_EQ(t.commands[0].issuer, 0);
        ASSERT_EQ(t.commands[1].issuer, 1);
        ASSERT_EQ(t.commands[2].issuer, 1);
        ASSERT_EQ(t.commands[3].issuer, 2);
        ASSERT_EQ(t.commands[1].type, CommandType::GroupMove);       // player 1's commands keep their order
        ASSERT_EQ(t.commands[2].type, CommandType::GroupAttack);
        ASSERT_EQ(seq.seal().turn, 1u);                              // turns are numbered without gaps
        // flooding: only max_commands_per_turn per peer and turn
        Sequencer::Config cfg;
        cfg.max_commands_per_turn = 3;
        Sequencer small(cfg);
        small.set_active(1, true);
        int taken = 0;
        for (int i = 0; i < 10; ++i) taken += small.submit(1, cmd(CommandType::Hatch, 1)) ? 1 : 0;
        ASSERT_EQ(taken, 3);
        small.seal();
        ASSERT_TRUE(small.submit(1, cmd(CommandType::Hatch, 1)));      // the limit is per turn
    } TEST_END();

    TEST_CASE("N2.6 Sequencer: Flow Control Stalls Sealing While A Peer Lags, Acks Only Move Forward, Reports Are Pruned") {
        Sequencer::Config cfg;
        cfg.max_lag_turns = 5;
        Sequencer seq(cfg);
        seq.set_host_player(0);
        seq.set_active(0, true);
        seq.set_active(1, true);
        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(seq.can_seal());
            seq.seal();
        }
        ASSERT_TRUE(seq.can_seal());                                   // lag 5 is still allowed
        seq.seal();                                                    // lag 6
        ASSERT_FALSE(seq.can_seal());
        ASSERT_EQ(seq.laggard(), 0);                                   // nobody acked yet: player 0 is the furthest behind (first in order)
        seq.on_ack(0, 5);
        ASSERT_EQ(seq.laggard(), 1);
        seq.on_ack(1, 2);                                              // turn 2 executed: needs 3 next, lag 3
        ASSERT_TRUE(seq.can_seal());
        seq.on_ack(1, 0);                                              // an old ack changes nothing
        ASSERT_EQ(seq.acked(1), 3u);
        seq.on_ack(1, 1000);                                           // an ack for a turn that was never sealed is clamped
        ASSERT_EQ(seq.acked(1), seq.next_turn());
        // a peer that joins late is level with the sequencer
        seq.set_active(2, true);
        ASSERT_EQ(seq.acked(2), seq.next_turn());
    } TEST_END();

    TEST_CASE("N2.7 Sequencer: Hash Reports Are Compared With The Host's; A Mismatch Is Named, Equal Reports Are Silent, Order Does Not Matter") {
        Sequencer seq;
        seq.set_host_player(0);
        for (uint8_t p = 0; p < 3; ++p) seq.set_active(p, true);
        for (int i = 0; i < 40; ++i) seq.seal();                       // turns 0 .. 39 exist (a report for a turn that was never sealed is dropped)
        const sim::StateHash good{10, 1, 2, 3, 4, 5, 6, 7};
        sim::StateHash bad = good;
        bad.total = 11;
        bad.ants = 55;
        ASSERT_TRUE(seq.on_hash(1, 9, good).empty());                  // the host's report is not in yet
        ASSERT_TRUE(seq.on_hash(2, 9, bad).empty());
        const auto d = seq.on_hash(0, 9, good);                        // now it is: player 2 differs
        ASSERT_EQ(d.size(), 1u);
        ASSERT_EQ(d[0].player, 2);
        ASSERT_EQ(d[0].turn, 9u);
        ASSERT_TRUE(d[0].host == good && d[0].peer == bad);
        ASSERT_TRUE(d[0].peer.ants != d[0].host.ants && d[0].peer.grid == d[0].host.grid);   // which subsystem
        ASSERT_TRUE(seq.on_hash(0, 19, good).empty());                 // host first
        ASSERT_TRUE(seq.on_hash(1, 19, good).empty());
        ASSERT_EQ(seq.on_hash(2, 19, bad).size(), 1u);
        ASSERT_TRUE(seq.on_hash(3, 29, good).empty());                 // an inactive slot is ignored
    } TEST_END();
}

void run_runner_tests() {
    TEST_CASE("N2.8 Lock-Step Runner: Buffers Before Starting, Ticks Every 50 ms, Applies A Turn's Commands Before Its First Tick, Stalls And Catches Up") {
        sim::SimulationEngine sim;
        const Ids ids = build_world(sim, 1);
        LockstepRunner runner(sim);
        auto turn = [](uint32_t n, std::vector<Command> cmds = {}) {
            TurnMsg t;
            t.turn = n;
            t.commands = std::move(cmds);
            return t;
        };
        ASSERT_FALSE(runner.on_turn(turn(1)));                                       // turns must start at 0 and have no gaps
        ASSERT_TRUE(runner.on_turn(turn(0, {cmd(CommandType::GroupMove, 0, 255, 20, 30, {ids.ants[0][0]})})));
        ASSERT_TRUE(runner.update(1000).empty());                                    // one turn is not enough to start (buffer 2)
        ASSERT_EQ(sim.current_tick(), 0u);
        ASSERT_FALSE(runner.on_turn(turn(0)));                                       // a turn cannot repeat
        ASSERT_TRUE(runner.on_turn(turn(1)));
        auto done = runner.update(0);                                                // starting: the first tick is due at once
        ASSERT_TRUE(done.empty());
        ASSERT_EQ(sim.current_tick(), 1u);
        ASSERT_EQ(sim.get_unit(ids.ants[0][0]).orig_order, sim::AntUnit::kOrderMove);   // the command was applied before that tick
        done = runner.update(49);
        ASSERT_TRUE(done.empty());
        ASSERT_EQ(sim.current_tick(), 1u);
        done = runner.update(1);                                                     // 50 ms: the second tick completes turn 0
        ASSERT_EQ(done.size(), 1u);
        ASSERT_EQ(done[0].turn, 0u);
        ASSERT_FALSE(done[0].has_hash);
        ASSERT_EQ(sim.current_tick(), 2u);
        done = runner.update(100);                                                   // turn 1 (two ticks in 100 ms)
        ASSERT_EQ(done.size(), 1u);
        ASSERT_EQ(sim.current_tick(), 4u);
        // no turn queued: the runner stalls at the boundary
        done = runner.update(500);
        ASSERT_TRUE(done.empty());
        ASSERT_TRUE(runner.stalled());
        ASSERT_EQ(sim.current_tick(), 4u);
        for (uint32_t n = 2; n < 40; ++n) ASSERT_TRUE(runner.on_turn(turn(n)));
        // 38 turns queued: far more than the buffer, so it runs at double speed; the burst per update is bounded
        done = runner.update(100);
        ASSERT_TRUE(done.size() >= 2 && done.size() <= 4);
        size_t hashes = 0;
        for (int i = 0; i < 400 && runner.queued() > 0; ++i) {
            for (const auto& e : runner.update(16)) hashes += e.has_hash ? 1 : 0;
        }
        for (const auto& e : runner.update(200)) hashes += e.has_hash ? 1 : 0;      // the second tick of the last turn
        ASSERT_EQ(runner.next_turn_to_execute(), 40u);
        ASSERT_EQ(sim.current_tick(), 80u);
        ASSERT_TRUE(hashes >= 3);                                                    // turns 9, 19, 29 (and 39) report a hash
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------

void run_match_tests() {
    TEST_CASE("N2.9 Match: A Host And Three Clients Over 25 - 120 ms Links With Jitter Stay Bit-Identical For 90 Seconds Of Play") {
        for (uint32_t seed : {1u, 2u}) {
            Match m(seed, 4, {60, 60});
            m.run(90000);
            m.settle();
            if (!m.host->desyncs().empty()) std::cout << "\n    desync at turn " << m.host->desyncs()[0].turn << "\n";
            ASSERT_TRUE(m.host->desyncs().empty());
            for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
            ASSERT_TRUE(m.host->turns_sealed() > 850);                                 // about 900 turns of 100 ms
            for (auto& s : m.sims) ASSERT_EQ(s->current_tick(), m.sims[0]->current_tick());
            ASSERT_TRUE(m.all_equal());
            ASSERT_TRUE(m.sims[0]->current_tick() > 1700);
            // the commands of every player took effect: ants of every player are not where they started
            sim::SimulationEngine fresh;
            build_world(fresh, seed);
            for (uint8_t p = 0; p < 4; ++p) {
                int moved = 0;
                for (uint32_t id : m.ids.ants[p]) {
                    if (m.sims[0]->get_unit(id).pos != fresh.get_unit(id).pos) ++moved;
                }
                ASSERT_TRUE(moved >= 2);
            }
        }
    } TEST_END();

    TEST_CASE("N2.10 Match: Different Links (40 / 150 / 300 ms, heavy jitter) Still End In The Same State") {
        LoopbackNetwork net(11);
        std::vector<std::unique_ptr<sim::SimulationEngine>> sims;
        Ids ids;
        for (int i = 0; i < 4; ++i) {
            sims.push_back(std::make_unique<sim::SimulationEngine>());
            ids = build_world(*sims.back(), 3);
        }
        HostSession host(*sims[0], HostSession::Config{});
        std::vector<std::unique_ptr<ClientSession>> clients;
        const LoopbackNetwork::Link links[3] = {{40, 5}, {150, 100}, {300, 200}};
        for (uint8_t p = 1; p < 4; ++p) {
            auto ends = net.connect(links[p - 1]);
            host.add_client(p, ends.first);
            ClientSession::Config cc;
            cc.player = p;
            clients.push_back(std::make_unique<ClientSession>(*sims[p], cc));
            clients.back()->set_connection(ends.second);
        }
        host.start(0);
        for (auto& c : clients) c->start(0);
        uint32_t now = 0;
        for (; now < 60000; now += 10) {
            net.set_time(now);
            Command c;
            if (script(ids, 3, now, 0, c)) host.submit_local(c);
            for (uint8_t p = 1; p < 4; ++p) {
                if (script(ids, 3, now, p, c)) clients[p - 1]->submit(c);
            }
            host.update(now);
            for (auto& cl : clients) cl->update(now);
        }
        host.freeze();
        for (uint32_t end = now + 5000; now < end; now += 10) {
            net.set_time(now);
            host.update(now);
            for (auto& cl : clients) cl->update(now);
        }
        ASSERT_TRUE(host.desyncs().empty());
        for (int i = 1; i < 4; ++i) ASSERT_TRUE(sims[static_cast<size_t>(i)]->state_hash() == sims[0]->state_hash());
        ASSERT_TRUE(clients[2]->rtt_ms() >= 600 && clients[2]->rtt_ms() <= 1000);        // the 300 ms link measures its round trip
        ASSERT_TRUE(clients[0]->rtt_ms() >= 80 && clients[0]->rtt_ms() <= 120);
    } TEST_END();

    TEST_CASE("N2.11 Match: Executed Ticks Are Steady At 20 Hz On Every Machine (no bursts beyond the bound, no drift against the host's schedule)") {
        Match m(1, 3, {80, 40});
        std::vector<uint64_t> last(3, 0);
        uint64_t max_step = 0;
        m.run(20000, true, [&](uint32_t) {
            for (size_t i = 0; i < m.sims.size(); ++i) {
                const uint64_t t = m.sims[i]->current_tick();
                max_step = std::max<uint64_t>(max_step, t - last[i]);
                last[i] = t;
            }
        });
        ASSERT_TRUE(max_step <= 2);                                    // a 10 ms step never advances more than one tick per machine
        // after the start buffer every machine has run about (20 s - start delay) * 20 ticks
        for (auto& s : m.sims) ASSERT_TRUE(s->current_tick() > 20000 / 50 - 100);
        m.settle();
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.12 Match: A Client Cannot Speak For Another Player (the payload's issuer is ignored)") {
        Match m(1, 2, {30, 0});
        const uint32_t victim_ant = m.ids.ants[0][0];                  // the host's worker
        const uint32_t own_ant = m.ids.ants[1][0];
        // client 1 orders the host's ant and claims to be the host
        m.clients[0]->submit(cmd(CommandType::GroupMove, 0, 255, 20, 30, {victim_ant}));
        m.clients[0]->submit(cmd(CommandType::GroupMove, 1, 255, 21, 31, {own_ant}));
        m.run(1000, false);
        m.settle();
        ASSERT_EQ(m.sims[0]->get_unit(victim_ant).orig_order, sim::AntUnit::kOrderNone);
        ASSERT_EQ(m.sims[1]->get_unit(victim_ant).orig_order, sim::AntUnit::kOrderNone);
        ASSERT_EQ(m.sims[0]->get_unit(own_ant).orig_order_tile, (TileCoord{21, 31}));
        ASSERT_TRUE(m.all_equal());
    } TEST_END();
}

void run_failure_tests() {
    TEST_CASE("S2.1 Dedicated Server: A Host Without A Seat Referees Four Clients Over 25 - 120 ms Links; Every Machine, The Referee Included, Stays Bit-Identical For 60 Seconds") {
        for (uint32_t seed : {1u, 2u}) {
            ServerMatch m(seed, 4, {60, 60});
            ASSERT_TRUE(m.host->seatless());
            m.run(60000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
            ASSERT_TRUE(m.host->turns_sealed() > 550);
            ASSERT_TRUE(m.referee.current_tick() > 1100);
            for (auto& s : m.sims) ASSERT_EQ(s->current_tick(), m.referee.current_tick());
            ASSERT_TRUE(m.all_equal());                                              // the referee's own engine equals every client's
            sim::SimulationEngine fresh;
            build_world(fresh, seed);
            for (uint8_t p = 0; p < 4; ++p) {                                        // every client's commands took effect on the referee's engine
                int moved = 0;
                for (uint32_t id : m.ids.ants[p]) {
                    if (m.referee.get_unit(id).pos != fresh.get_unit(id).pos) ++moved;
                }
                ASSERT_TRUE(moved >= 2);
            }
        }
    } TEST_END();

    TEST_CASE("S2.2 Dedicated Server: The Referee Names A Diverging Client Within A Second, With The Subsystem; The Server Plays And Chats As Nobody") {
        ServerMatch m(1, 4, {40, 10});
        m.run(3000);
        ASSERT_TRUE(m.host->desyncs().empty());
        m.sims[3]->get_unit(m.ids.ants[3][1]).hp = 3;                                // a hit point that no command explains
        const uint32_t before = m.now;
        m.run(2000, false);
        ASSERT_FALSE(m.host->desyncs().empty());
        ASSERT_TRUE(m.now - before <= 2000);
        const DesyncMsg& d = m.host->desyncs()[0];
        ASSERT_EQ(d.player, 3);
        ASSERT_TRUE(d.peer.ants != d.host.ants);
        ASSERT_TRUE(d.peer.grid == d.host.grid && d.peer.players == d.host.players);
        for (auto& c : m.clients) ASSERT_TRUE(c->desynced());                        // everybody is told
        ASSERT_TRUE(m.host->frozen());
        for (const auto& x : m.host->desyncs()) ASSERT_EQ(x.player, 3);              // only client 3 is blamed (the referee agrees with the others)
        // a host without a seat has no commands and no chat of its own
        ServerMatch quiet(2, 2, {20, 5});
        quiet.run(500, false);
        Command c;
        c.type = CommandType::Stop;
        c.ants = {quiet.ids.ants[0][0]};
        quiet.host->submit_local(c);
        quiet.host->chat_local("nobody speaks", false);
        std::vector<uint8_t> msg;
        bool chat_seen = false;
        quiet.run(500, false);
        for (auto* end : quiet.client_ends) {
            while (end->poll(msg)) chat_seen = chat_seen || peek_type(msg) == MsgType::Chat;
        }
        ASSERT_FALSE(chat_seen);
    } TEST_END();

    TEST_CASE("S2.3 Dedicated Server: A Client Cannot Make The Report Maps Grow With Hashes For Turns That Were Never Sealed; A Report Of A Real Turn Is Compared Whichever Comes First") {
        // the sequencer alone: a host with and a host without a seat
        for (uint8_t host_seat : {uint8_t{0}, kNoSeat}) {
            Sequencer seq;
            seq.set_host_player(host_seat);
            seq.set_active(1, true);
            seq.set_active(2, true);
            if (host_seat < sim::MAX_PLAYERS) seq.set_active(host_seat, true);
            sim::StateHash bogus;
            for (uint32_t t = 100000; t < 100500; ++t) {
                ASSERT_TRUE(seq.on_hash(1, t, bogus).empty());
                ASSERT_TRUE(seq.on_referee_hash(t, bogus).empty());
            }
            ASSERT_EQ(seq.pending_reports(), size_t{0});                                  // nothing was sealed: nothing is kept
            for (int i = 0; i < 20; ++i) seq.seal();                                      // turns 0 .. 19 exist now
            sim::StateHash a;
            sim::StateHash b;
            b.total = 1;
            b.ants = 1;
            if (host_seat == kNoSeat) {
                ASSERT_TRUE(seq.on_hash(1, 10, a).empty());                               // the client is first: waits for the referee
                ASSERT_TRUE(seq.on_hash(2, 10, b).empty());
                const auto d = seq.on_referee_hash(10, a);                                // the referee agrees with client 1 and not with client 2
                ASSERT_EQ(d.size(), size_t{1});
                ASSERT_EQ(d[0].player, 2);
                ASSERT_TRUE(d[0].host == a && d[0].peer == b);
                const auto late = seq.on_hash(2, 10, b);                                  // a repeated report of the same turn is compared again
                ASSERT_EQ(late.size(), size_t{1});
                ASSERT_TRUE(seq.on_hash(1, 11, a).empty() && seq.on_referee_hash(11, a).empty());      // agreement: nothing
                ASSERT_EQ(seq.on_hash(2, 12, b).size() + seq.on_referee_hash(12, a).size(), size_t{1});   // the referee is first: the client is found as it reports
            }
        }
        // on the wire: a hostile client sends hashes for the far future; the match goes on unharmed
        ServerMatch m(1, 2, {10, 0});
        m.run(2000);
        HashMsg h;
        for (uint32_t t = 100000; t < 100200; ++t) {
            h.turn = t;
            m.client_ends[0]->send(encode(h));
        }
        m.run(1000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("S2.4 Dedicated Server: A Client Whose Server Goes Away Does Not Elect Itself The Host (No Migration): The Match Is Lost Here After The Silence") {
        ServerMatch m(1, 3, {20, 5});
        m.run(3000);
        ASSERT_EQ(static_cast<int>(m.clients[0]->mode()), static_cast<int>(ClientSession::Mode::Normal));
        m.host_ends[0]->close();                                                      // client 0's link to the server dies
        m.run(1500, false);
        ASSERT_TRUE(m.clients[0]->lost());                                            // it does not become a host of a one-player game
        ASSERT_FALSE(m.clients[0]->promoted());
        ASSERT_FALSE(m.clients[0]->electing());
        ASSERT_EQ(static_cast<int>(m.clients[1]->mode()), static_cast<int>(ClientSession::Mode::Normal));    // the others are not affected
        m.net.set_time(m.now);
        // the server drops the vanished client at the same tick for everybody
        m.run(1000, false);
        ASSERT_TRUE(m.referee.is_player_dropped(0));
        for (uint8_t p = 1; p < 3; ++p) ASSERT_TRUE(m.sims[p]->is_player_dropped(0));
    } TEST_END();

    TEST_CASE("S2.5 Dedicated Server: A StartRequest That Reaches A Running Match (The Leader's Second Click Crossed The Start) Is Ignored And Costs Nothing; With A Payload It Is Garbage; A Host That Holds A Seat Has No Leader And Counts It As A Violation") {
        {
            ServerMatch m(1, 3, {20, 5});
            m.run(1000);
            for (int i = 0; i < 50; ++i) m.client_ends[0]->send(encode(StartRequestMsg{}));          // fifty late clicks: far over the eight violations that throw a client out
            m.run(500);
            ASSERT_TRUE(m.host->client_present(0));
            ASSERT_EQ(m.host->violations(0), 0u);
            m.client_ends[1]->send({static_cast<uint8_t>(MsgType::StartRequest), 1});               // with a payload: garbage like any other
            m.run(300);
            ASSERT_EQ(m.host->violations(1), 1u);
            ASSERT_TRUE(m.host->client_present(1));
            m.settle();
            ASSERT_TRUE(m.all_equal());                                                              // nothing changed in the match
            ASSERT_TRUE(m.host->desyncs().empty());
        }
        {   // a host that plays a seat: nobody in its room leads, so the message is a violation like any other that a guest may not send
            Match m(1, 3, {20, 0});
            m.run(1000);
            m.client_ends[2]->send(encode(StartRequestMsg{}));
            m.run(300);
            ASSERT_EQ(m.host->violations(2), 1u);
            for (int i = 0; i < 10; ++i) m.client_ends[2]->send(encode(StartRequestMsg{}));
            m.run(300);
            ASSERT_FALSE(m.host->client_present(2));                                                 // thrown out after eight
            ASSERT_TRUE(m.host->client_present(1));
        }
    } TEST_END();

    TEST_CASE("N2.13 Desync: A Diverging Client Is Named Within A Second, With The Subsystem, And Sees It Itself") {
        Match m(1, 4, {40, 10});
        m.run(3000);
        ASSERT_TRUE(m.host->desyncs().empty());
        // corrupt client 2's ant state (a hit point that no command explains)
        m.sims[2]->get_unit(m.ids.ants[2][1]).hp = 3;
        const uint32_t before = m.now;
        m.run(2000, false);
        ASSERT_FALSE(m.host->desyncs().empty());
        ASSERT_TRUE(m.now - before <= 2000);
        const DesyncMsg& d = m.host->desyncs()[0];
        ASSERT_EQ(d.player, 2);
        ASSERT_TRUE(d.peer.ants != d.host.ants);
        ASSERT_TRUE(d.peer.grid == d.host.grid && d.peer.players == d.host.players);       // only the ants differ
        ASSERT_TRUE(m.clients[1]->desynced());
        ASSERT_EQ(m.clients[1]->desync().player, 2);
        ASSERT_TRUE(m.clients[0]->desynced());                                              // everybody is told
        ASSERT_TRUE(m.host->frozen());                                                      // and the match stops sealing turns
        for (const auto& x : m.host->desyncs()) ASSERT_EQ(x.player, 2);                     // only client 2 is blamed
    } TEST_END();

    TEST_CASE("N2.14 Flow Control: A Peer That Stops Executing Holds The Match Up After 3 s, Then The Game Continues When It Catches Up") {
        Match m(1, 3, {30, 0});
        m.run(2000);
        const uint32_t sealed_before_stall = m.host->turns_sealed();
        // client 2 freezes: it neither executes nor acknowledges (its updates stop)
        uint32_t now = m.now;
        for (uint32_t end = now + 6000; now < end; now += 10) {
            m.net.set_time(now);
            m.host->update(now);
            m.clients[0]->update(now);
        }
        m.now = now;
        ASSERT_TRUE(m.host->waiting());
        ASSERT_EQ(m.host->laggard(), 2);
        const uint32_t stalled_at = m.host->turns_sealed();
        ASSERT_TRUE(stalled_at > sealed_before_stall);
        ASSERT_TRUE(stalled_at <= sealed_before_stall + 30 + 45);                           // at most max lag + what was in flight
        // the freeze ends: it catches up and the match goes on
        m.run(8000, false);
        ASSERT_FALSE(m.host->waiting());
        ASSERT_TRUE(m.host->turns_sealed() > stalled_at + 30);
        m.settle();
        ASSERT_TRUE(m.all_equal());
        ASSERT_TRUE(m.host->desyncs().empty());
    } TEST_END();

    TEST_CASE("N2.15 Drop-Out: A Client Whose Connection Dies Is Reported Once And The Others Play On, Still Identical") {
        Match m(1, 4, {30, 5});
        std::vector<uint8_t> left;
        m.host->set_on_player_left([&](uint8_t p) { left.push_back(p); });
        m.run(3000);
        m.net.cut(m.client_ends[3], true);
        m.run(5000);
        ASSERT_EQ(left.size(), 1u);
        ASSERT_EQ(left[0], 3);
        ASSERT_FALSE(m.host->client_present(3));
        m.settle();
        for (size_t i = 1; i < 3; ++i) ASSERT_TRUE(m.sims[i]->state_hash() == m.sims[0]->state_hash());
        ASSERT_TRUE(m.host->desyncs().empty() || m.host->desyncs()[0].player == 3);        // only the cut peer can differ
        ASSERT_TRUE(m.host->turns_sealed() > 70);                                           // the game did not stop for it
    } TEST_END();

    TEST_CASE("N2.16 Hostile Client: Garbage, Host-Only Messages, Oversized Messages And Floods Get It Thrown Out; Nobody Else Notices") {
        Match m(1, 3, {20, 0});
        m.run(1000);
        Connection* evil = m.client_ends[2];
        evil->send({1, 2, 3});                                          // a Hello that is too short
        evil->send({static_cast<uint8_t>(MsgType::Turn), 0, 0, 0, 0, 0, 0});
        evil->send({255, 255, 255});
        evil->send(std::vector<uint8_t>(kMaxMessageBytes + 10, 4));      // oversized
        m.run(500, false);
        ASSERT_TRUE(m.host->violations(2) >= 4 || !m.host->client_present(2));
        for (int i = 0; i < 400; ++i) evil->send(encode(CommandMsg{cmd(CommandType::Hatch, 2)}));
        m.run(1000, false);
        ASSERT_FALSE(m.host->client_present(2));                        // thrown out
        ASSERT_FALSE(evil->is_open());
        m.settle();
        ASSERT_TRUE(m.sims[1]->state_hash() == m.sims[0]->state_hash());
        ASSERT_TRUE(m.host->client_present(1));
        ASSERT_TRUE(m.host->turns_sealed() > 20);                       // the game went on for everybody else
    } TEST_END();

    TEST_CASE("N2.17 Chat: Relayed To Everybody With The Sender Stamped By The Connection") {
        Match m(1, 3, {20, 0});
        std::vector<ChatMsg> host_seen;
        std::vector<ChatMsg> c1_seen;
        std::vector<ChatMsg> c2_seen;
        m.host->set_on_chat([&](const ChatMsg& c) { host_seen.push_back(c); });
        m.clients[0]->set_on_chat([&](const ChatMsg& c) { c1_seen.push_back(c); });
        m.clients[1]->set_on_chat([&](const ChatMsg& c) { c2_seen.push_back(c); });
        m.run(500);
        ASSERT_TRUE(m.clients[0]->chat("hello all", false));
        m.host->chat_local("host here", true);
        m.run(500, false);
        ASSERT_EQ(host_seen.size(), 2u);
        ASSERT_EQ(c2_seen.size(), 2u);
        ASSERT_EQ(c1_seen.size(), 2u);                                  // the sender gets its own text back through the host
        auto find = [](const std::vector<ChatMsg>& v, const std::string& text) -> const ChatMsg* {
            for (const auto& c : v) {
                if (c.text == text) return &c;
            }
            return nullptr;
        };
        for (const auto* seen : {&host_seen, &c1_seen, &c2_seen}) {
            const ChatMsg* a = find(*seen, "hello all");
            const ChatMsg* b = find(*seen, "host here");
            ASSERT_TRUE(a != nullptr && b != nullptr);
            ASSERT_TRUE(a->sender == 1 && !a->team);
            ASSERT_TRUE(b->sender == 0 && b->team);
        }
        // a client that forges the sender byte is stamped anyway
        ChatMsg forged;
        forged.sender = 0;
        forged.text = "I am the host";
        m.client_ends[2]->send(encode(forged));
        m.run(500, false);
        const ChatMsg* forged_seen = find(host_seen, "I am the host");
        ASSERT_TRUE(forged_seen != nullptr && forged_seen->sender == 2);
    } TEST_END();
}

void run_dropout_tests() {
    TEST_CASE("N2.18 Drop-Out Travels In The Turn Stream: Every Machine Drops The Team At The Same Tick, The Match Stays Identical") {
        Match m(1, 4, {30, 5});
        std::vector<uint8_t> left;
        m.host->set_on_player_left([&](uint8_t p) { left.push_back(p); });
        std::vector<uint64_t> host_ticks;
        std::vector<uint64_t> client_ticks;
        m.host->runner().set_on_command([&](const Command& c, const sim::CommandResult&) {
            if (c.type == CommandType::Drop) host_ticks.push_back(m.sims[0]->current_tick());
        });
        m.clients[0]->runner().set_on_command([&](const Command& c, const sim::CommandResult&) {
            if (c.type == CommandType::Drop) client_ticks.push_back(m.sims[1]->current_tick());
        });
        m.run(3000);
        for (uint8_t p = 0; p < 4; ++p) ASSERT_FALSE(m.sims[0]->is_player_dropped(p));
        m.net.cut(m.client_ends[3], true);                                  // player 3's machine dies
        m.run(5000);
        ASSERT_EQ(left.size(), 1u);
        ASSERT_EQ(left[0], 3);
        m.settle();
        ASSERT_EQ(host_ticks.size(), 1u);                                   // one Drop, applied once ...
        ASSERT_EQ(client_ticks.size(), 1u);
        ASSERT_EQ(host_ticks[0], client_ticks[0]);                          // ... at the same tick on the host and on a client
        for (size_t i = 0; i < 3; ++i) {                                    // the survivors all dropped team 3 and agree on everything
            ASSERT_TRUE(m.sims[i]->is_player_dropped(3));
            ASSERT_FALSE(m.sims[i]->is_player_dropped(0));
            ASSERT_FALSE(m.sims[i]->is_player_dropped(1));
            ASSERT_FALSE(m.sims[i]->is_player_dropped(2));
            ASSERT_TRUE(m.sims[i]->state_hash() == m.sims[0]->state_hash());
            size_t ants_of_3 = 0;
            for (const auto& a : m.sims[i]->get_world_state().ants) ants_of_3 += a.player_id == 3;
            ASSERT_EQ(ants_of_3, 0u);                                       // the team's ants died on every machine
        }
        ASSERT_TRUE(m.host->desyncs().empty() || m.host->desyncs()[0].player == 3);
        ASSERT_TRUE(m.host->turns_sealed() > 70);
    } TEST_END();

    TEST_CASE("N2.19 Drop-Out: A Client Cannot Send The System Command (it is refused and counted as a violation, nobody is dropped)") {
        Match m(1, 3, {20, 0});
        m.run(1000);
        ASSERT_EQ(m.host->violations(2), 0u);
        m.client_ends[2]->send(encode(CommandMsg{cmd(CommandType::Drop, 2)}));       // its own drop
        m.client_ends[2]->send(encode(CommandMsg{cmd(CommandType::Drop, 1)}));       // somebody else's
        m.run(500, false);
        ASSERT_EQ(m.host->violations(2), 2u);
        ASSERT_TRUE(m.host->client_present(1));
        ASSERT_TRUE(m.host->client_present(2));
        m.settle();
        for (size_t i = 0; i < m.sims.size(); ++i) {
            for (uint8_t p = 0; p < 4; ++p) ASSERT_FALSE(m.sims[i]->is_player_dropped(p));
        }
        ASSERT_TRUE(m.all_equal());
        // the sequencer refuses it for a client and accepts it as a system command
        Sequencer seq;
        seq.set_active(1, true);
        ASSERT_FALSE(seq.submit(1, cmd(CommandType::Drop, 1)));
        ASSERT_EQ(seq.queued(), 0u);
        seq.submit_system(cmd(CommandType::Drop, 1));
        ASSERT_EQ(seq.queued(), 1u);
        seq.submit_system(cmd(CommandType::Drop, 9));                                // no such player
        seq.submit_system(cmd(CommandType::None, 1));
        ASSERT_EQ(seq.queued(), 1u);
        const TurnMsg turn = seq.seal();
        ASSERT_EQ(turn.commands.size(), 1u);
        ASSERT_EQ(turn.commands[0].type, CommandType::Drop);
        ASSERT_EQ(turn.commands[0].issuer, 1);
    } TEST_END();

    TEST_CASE("N2.20 Drop-Out: A Client That Falls Silent Holds The Match Up (flow control) And Is Dropped After The Silence Timeout") {
        ASSERT_EQ(HostSession::Config{}.silence_timeout_ms, 60000u);                 // the original's 60 s
        HostSession::Config hc;
        hc.silence_timeout_ms = 8000;
        Match m(1, 3, {20, 0}, hc);
        std::vector<uint8_t> left;
        m.host->set_on_player_left([&](uint8_t p) { left.push_back(p); });
        auto run_without_client_2 = [&](uint32_t ms) {                                // client 2's process hangs: its connection stays open
            const uint32_t end = m.now + ms;
            while (m.now < end) {
                m.now += 10;
                m.net.set_time(m.now);
                m.host->update(m.now);
                m.clients[0]->update(m.now);
            }
        };
        m.run(2000);
        const uint32_t hang_at = m.now;
        run_without_client_2(6000);                                                  // 6 s: past the 3 s flow control, before the timeout
        ASSERT_TRUE(left.empty());
        ASSERT_TRUE(m.host->waiting());
        ASSERT_EQ(m.host->laggard(), 2);
        const uint32_t sealed_while_waiting = m.host->turns_sealed();
        run_without_client_2(1000);
        ASSERT_EQ(m.host->turns_sealed(), sealed_while_waiting);                     // sealing is stalled
        run_without_client_2(3000);                                                  // the 8 s of silence are over
        ASSERT_TRUE(m.now - hang_at > 8000);
        ASSERT_EQ(left.size(), 1u);
        ASSERT_EQ(left[0], 2);
        ASSERT_FALSE(m.host->client_present(2));
        ASSERT_FALSE(m.host->waiting());
        run_without_client_2(4000);
        ASSERT_TRUE(m.host->turns_sealed() > sealed_while_waiting + 20);             // the game goes on for the others
        m.host->freeze();
        run_without_client_2(3000);
        ASSERT_TRUE(m.sims[0]->is_player_dropped(2));
        ASSERT_TRUE(m.sims[1]->is_player_dropped(2));
        ASSERT_TRUE(m.sims[1]->state_hash() == m.sims[0]->state_hash());
    } TEST_END();

    TEST_CASE("N2.21 Runner Hooks: One Call Per Tick (two per turn) And One Per Command With The Engine's Verdict, In Order") {
        sim::SimulationEngine sim;
        const Ids ids = build_world(sim, 1);
        LockstepRunner runner(sim);
        std::vector<uint64_t> tick_times;
        std::vector<std::pair<CommandType, sim::CommandResult::Status>> commands;
        runner.set_on_tick([&]() { tick_times.push_back(sim.current_tick()); });
        runner.set_on_command([&](const Command& c, const sim::CommandResult& r) { commands.emplace_back(c.type, r.status); });
        auto turn = [](uint32_t n, std::vector<Command> cmds = {}) {
            TurnMsg t;
            t.turn = n;
            t.commands = std::move(cmds);
            return t;
        };
        runner.on_turn(turn(0, {cmd(CommandType::GroupMove, 0, 255, 20, 30, {ids.ants[0][0]}), cmd(CommandType::Hatch, 1),
                                cmd(CommandType::GroupMove, 4, 255, 1, 1, {1})}));                 // the last has no such player
        runner.on_turn(turn(1));
        runner.on_turn(turn(2, {cmd(CommandType::Stop, 0, 255, 0, 0, {ids.ants[0][0]})}));
        for (int i = 0; i < 40; ++i) runner.update(50);
        ASSERT_EQ(runner.next_turn_to_execute(), 3u);
        ASSERT_EQ(tick_times.size(), 6u);                                                           // two ticks per turn
        for (size_t i = 0; i < tick_times.size(); ++i) ASSERT_EQ(tick_times[i], i + 1);             // the hook runs after the tick
        ASSERT_EQ(commands.size(), 4u);
        ASSERT_EQ(commands[0].first, CommandType::GroupMove);
        ASSERT_EQ(commands[0].second, sim::CommandResult::Status::Applied);
        ASSERT_EQ(commands[1].first, CommandType::Hatch);
        ASSERT_EQ(commands[2].second, sim::CommandResult::Status::RejectedIssuer);
        ASSERT_EQ(commands[3].first, CommandType::Stop);
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// Host migration (docs/NETWORK_PORT.md): the guests are linked to each other as well, so the game outlives its host
// ---------------------------------------------------------------------------------------------------------------------------------

using LinkFn = std::function<LoopbackNetwork::Link(uint8_t, uint8_t)>;

LinkFn same_link(LoopbackNetwork::Link link) {
    return [link](uint8_t, uint8_t) { return link; };
}

// A match of `players` machines: seat 0 hosts, every guest is linked to the host and to every other guest
struct Mesh {
    LoopbackNetwork net;
    std::vector<std::unique_ptr<sim::SimulationEngine>> sims;
    Ids ids;
    std::vector<std::unique_ptr<HostSession>> hosts;         // by seat: the first host, and every guest that took over
    std::vector<std::unique_ptr<ClientSession>> clients;     // by seat; reset when the seat took over
    std::vector<std::vector<Connection*>> link;              // link[a][b]: the endpoint that seat a holds of its link to seat b
    std::vector<bool> alive;                                 // a machine that is not alive is no longer updated
    std::vector<std::pair<uint8_t, uint8_t>> left;           // (host seat, player) of every drop-out a host announced
    std::vector<std::vector<ChatMsg>> chats;                 // what each machine's chat callback saw
    uint32_t now{0};
    uint32_t seed{1};
    uint8_t players{4};
    int promotions{0};

    Mesh(uint32_t seed_, uint8_t players_, const LinkFn& link_of, ClientSession::Config cc = {}) : net(seed_ * 17u), seed(seed_), players(players_) {
        for (uint8_t p = 0; p < players; ++p) {
            sims.push_back(std::make_unique<sim::SimulationEngine>());
            ids = build_world(*sims.back(), seed);
        }
        hosts.resize(players);
        clients.resize(players);
        alive.assign(players, true);
        chats.resize(players);
        link.assign(players, std::vector<Connection*>(players, nullptr));
        hosts[0] = std::make_unique<HostSession>(*sims[0], HostSession::Config{});
        hosts[0]->set_on_player_left([this](uint8_t p) { left.emplace_back(0, p); });
        hosts[0]->set_on_chat([this](const ChatMsg& c) { chats[0].push_back(c); });
        for (uint8_t p = 1; p < players; ++p) {
            ClientSession::Config c = cc;
            c.player = p;
            c.host = 0;
            clients[p] = std::make_unique<ClientSession>(*sims[p], c);
            clients[p]->set_on_chat([this, p](const ChatMsg& m) { chats[p].push_back(m); });
            auto ends = net.connect(link_of(0, p));
            link[0][p] = ends.first;
            link[p][0] = ends.second;
            hosts[0]->add_client(p, ends.first);
            clients[p]->set_connection(ends.second);
        }
        for (uint8_t a = 1; a < players; ++a) {
            for (uint8_t b = static_cast<uint8_t>(a + 1); b < players; ++b) {
                auto ends = net.connect(link_of(a, b));
                link[a][b] = ends.first;
                link[b][a] = ends.second;
                clients[a]->set_peer(b, ends.first);
                clients[b]->set_peer(a, ends.second);
            }
        }
        hosts[0]->start(0);
        for (uint8_t p = 1; p < players; ++p) clients[p]->start(0);
    }

    // The machine of `seat` dies: its links are cut at once (abrupt), or it simply stops answering while its links stay open (a frozen process)
    void kill(uint8_t seat, bool abrupt = true) {
        alive[seat] = false;
        if (abrupt) isolate(seat);
    }
    // Every link of the seat is cut, but the machine goes on (a partition)
    void isolate(uint8_t seat) {
        for (uint8_t o = 0; o < players; ++o) {
            if (o != seat && link[seat][o] != nullptr) net.cut(link[seat][o]);
        }
    }
    void cut_link(uint8_t a, uint8_t b) { net.cut(link[a][b]); }

    void step_promotions() {
        for (uint8_t s = 0; s < players; ++s) {
            if (!alive[s] || !clients[s] || !clients[s]->promoted()) continue;
            hosts[s] = promote_to_host(*clients[s], *sims[s], HostSession::Config{}, now, [this, s](HostSession& h) {
                h.set_on_player_left([this, s](uint8_t p) { left.emplace_back(s, p); });
                h.set_on_chat([this, s](const ChatMsg& c) { chats[s].push_back(c); });
            });
            clients[s].reset();
            ++promotions;
        }
    }

    // Advances virtual time by `ms` in 10 ms steps; every machine that plays issues its scripted commands when `scripted`
    void run(uint32_t ms, bool scripted = true, const std::function<void(uint32_t)>& each_step = {}) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            net.set_time(now);
            for (uint8_t s = 0; s < players; ++s) {
                Command c;
                if (!alive[s] || !scripted || !script(ids, seed, now, s, c)) continue;
                if (hosts[s]) hosts[s]->submit_local(c);
                else if (clients[s]) clients[s]->submit(c);
            }
            if (each_step) each_step(now);
            for (uint8_t s = 0; s < players; ++s) {
                if (!alive[s]) continue;
                if (hosts[s]) hosts[s]->update(now);
                else if (clients[s]) clients[s]->update(now);
            }
            step_promotions();
        }
    }
    bool playing(uint8_t s) const { return alive[s] && (hosts[s] != nullptr || (clients[s] && !clients[s]->lost())); }
    void settle(uint32_t ms = 3000) {
        for (uint8_t s = 0; s < players; ++s) {
            if (alive[s] && hosts[s]) hosts[s]->freeze();
        }
        run(ms, false);
    }
    // Every machine that still plays holds the same state (the hosts' desync detectors stayed silent as well)
    bool survivors_equal(uint8_t except = 255) const {
        const sim::StateHash* first = nullptr;
        sim::StateHash hash;
        for (uint8_t s = 0; s < players; ++s) {
            if (!playing(s) || s == except) continue;
            if (hosts[s] && !hosts[s]->desyncs().empty()) return false;
            hash = sims[s]->state_hash();
            if (first == nullptr) {
                first = &hash;
                continue;
            }
            if (hash != *first) return false;
        }
        return true;
    }
    uint32_t tick(uint8_t s) const { return static_cast<uint32_t>(sims[s]->current_tick()); }
};

void run_migration_tests() {
    TEST_CASE("N2.22 Host Migration: The Host Dies, The Lowest Guest Takes Over, The Others Follow, Every Survivor Stays Identical And The Game Goes On") {
        std::vector<sim::StateHash> finals;
        for (int run = 0; run < 2; ++run) {                                  // twice: the whole thing is deterministic
            Mesh m(1, 4, same_link({30, 5}));
            std::vector<uint8_t> after;                                      // issuers of the commands seat 1's machine applies after the death
            bool killed = false;
            m.clients[1]->runner().set_on_command([&](const Command& c, const sim::CommandResult&) {
                if (killed && c.type != CommandType::Drop) after.push_back(c.issuer);
            });
            m.run(6000);
            ASSERT_EQ(m.promotions, 0);
            ASSERT_TRUE(m.hosts[0]->turns_sealed() > 50);
            const uint32_t kill_tick = m.tick(1);
            killed = true;
            m.kill(0);
            m.run(8000);
            ASSERT_EQ(m.promotions, 1);
            ASSERT_TRUE(m.hosts[1] != nullptr);
            ASSERT_TRUE(m.hosts[2] == nullptr && m.hosts[3] == nullptr);
            ASSERT_EQ(m.hosts[1]->epoch(), 1);
            ASSERT_EQ(m.hosts[1]->host_player(), 1);
            for (uint8_t s = 2; s < 4; ++s) {
                ASSERT_EQ(m.clients[s]->epoch(), 1);
                ASSERT_EQ(m.clients[s]->host_seat(), 1);
                ASSERT_EQ(m.clients[s]->mode(), ClientSession::Mode::Normal);
            }
            m.settle();
            ASSERT_TRUE(m.survivors_equal());
            for (uint8_t s = 1; s < 4; ++s) {
                ASSERT_TRUE(m.sims[s]->is_player_dropped(0));
                for (uint8_t p = 1; p < 4; ++p) ASSERT_FALSE(m.sims[s]->is_player_dropped(p));
            }
            ASSERT_TRUE(m.tick(1) > kill_tick + 150);                        // the game went on for several seconds after the host died
            ASSERT_EQ(std::count(after.begin(), after.end(), 0), 0);         // nothing more from the dead host ...
            for (uint8_t p = 1; p < 4; ++p) ASSERT_TRUE(std::count(after.begin(), after.end(), p) > 5);   // ... and everybody else still commands
            ASSERT_EQ(m.left.size(), 1u);                                    // the new host announced the drop-out of the old one, once
            ASSERT_TRUE(m.left[0].first == 1 && m.left[0].second == 0);
            finals.push_back(m.sims[1]->state_hash());
        }
        ASSERT_TRUE(finals[0] == finals[1]);
    } TEST_END();

    TEST_CASE("N2.23 Host Migration: A Host That Only Falls Silent (links stay open) Is Given Up After Ten Seconds") {
        Mesh m(2, 4, same_link({40, 10}));
        m.run(4000);
        m.kill(0, false);
        m.run(9000);
        ASSERT_EQ(m.promotions, 0);                                          // nine seconds are not enough
        for (uint8_t s = 1; s < 4; ++s) ASSERT_EQ(m.clients[s]->mode(), ClientSession::Mode::Normal);
        m.run(3000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[1] != nullptr);
        m.run(4000);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        for (uint8_t s = 1; s < 4; ++s) ASSERT_TRUE(m.sims[s]->is_player_dropped(0));
    } TEST_END();

    TEST_CASE("N2.24 Host Migration: Two Players Left (three in the match); Two Players Total: The Drop Of The Host Leaves The Guest Without A Live Enemy And Ends The Match At Once (0x100d172)") {
        {
            Mesh m(3, 3, same_link({25, 0}));
            m.run(3000);
            m.kill(0);
            m.run(6000);
            ASSERT_EQ(m.promotions, 1);
            ASSERT_TRUE(m.hosts[1] != nullptr);
            ASSERT_EQ(m.clients[2]->host_seat(), 1);
            m.settle();
            ASSERT_TRUE(m.survivors_equal());
            ASSERT_TRUE(m.sims[1]->is_player_dropped(0));
            ASSERT_TRUE(m.sims[2]->is_player_dropped(0));
            ASSERT_TRUE(m.sims[1]->is_player_dropped(3));                    // the seat of the roster that nobody plays leaves with the old host
            ASSERT_FALSE(m.sims[1]->is_match_over());                        // two enemies are left: the guests play on
            ASSERT_FALSE(m.sims[2]->is_match_over());
        }
        {
            Mesh m(4, 2, same_link({25, 0}));
            m.run(3000);
            m.kill(0);
            m.run(6000);
            ASSERT_EQ(m.promotions, 1);                                      // nobody to ask: the guest is the host at once
            ASSERT_TRUE(m.hosts[1] != nullptr);
            ASSERT_TRUE(m.sims[1]->is_player_dropped(0));
            ASSERT_FALSE(m.sims[1]->is_player_dropped(1));
            ASSERT_TRUE(m.sims[1]->is_match_over());                         // the win test of the drop-out: no team is left besides the guest
            ASSERT_EQ(m.sims[1]->quitter(), sim::NO_QUITTER);
            ASSERT_TRUE(m.sims[1]->get_world_state().match_result.is_winner(1));
        }
    } TEST_END();

    TEST_CASE("N2.25 Host Migration: The Host And The Next Seat Die Together; The Seats Above Them Elect Among Themselves") {
        Mesh m(5, 4, same_link({30, 5}));
        m.run(4000);
        m.kill(0);
        m.kill(1);
        m.run(8000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[2] != nullptr && m.hosts[1] == nullptr);
        ASSERT_EQ(m.hosts[2]->epoch(), 1);
        ASSERT_EQ(m.clients[3]->host_seat(), 2);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        for (uint8_t s = 2; s < 4; ++s) {
            ASSERT_TRUE(m.sims[s]->is_player_dropped(0));
            ASSERT_TRUE(m.sims[s]->is_player_dropped(1));
            ASSERT_FALSE(m.sims[s]->is_player_dropped(2));
            ASSERT_FALSE(m.sims[s]->is_player_dropped(3));
        }
    } TEST_END();

    TEST_CASE("N2.26 Host Migration: The Successor Dies While It Is Being Accepted; The Next One Takes Over Within A Blink") {
        Mesh m(6, 4, same_link({30, 0}));
        m.run(4000);
        m.kill(0);
        bool cut = false;
        m.run(6000, true, [&](uint32_t) {
            if (!cut && m.clients[2] && m.clients[2]->mode() == ClientSession::Mode::Following) {
                cut = true;                                                  // seat 2 has accepted seat 1: seat 1 dies before it can resume
                m.kill(1);
            }
        });
        ASSERT_TRUE(cut);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[2] != nullptr && m.hosts[1] == nullptr);
        ASSERT_EQ(m.clients[3]->host_seat(), 2);
        ASSERT_EQ(m.clients[3]->epoch(), 1);                                 // still the first host change: the election was only repeated
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        ASSERT_TRUE(m.sims[2]->is_player_dropped(0) && m.sims[2]->is_player_dropped(1));
        ASSERT_TRUE(m.sims[3]->is_player_dropped(0) && m.sims[3]->is_player_dropped(1));
    } TEST_END();

    TEST_CASE("N2.27 Host Migration: Guests That Are Behind Or Ahead Of The New Host Get Or Give The Missing Turns; Nobody Diverges") {
        for (int variant = 0; variant < 2; ++variant) {
            // variant 0: seat 1 (the successor) is on a slow link, so it is behind the others when the host dies: it fetches the turns from one of them
            // variant 1: seats 2 and 3 are on slow links: they are behind the successor, which sends them what they miss
            const LinkFn links = [variant](uint8_t a, uint8_t b) {
                if (a == 0) {
                    const bool slow = variant == 0 ? b == 1 : b >= 2;
                    return LoopbackNetwork::Link{slow ? 400u : 30u, 0u};
                }
                return LoopbackNetwork::Link{30u, 0u};
            };
            Mesh m(7 + static_cast<uint32_t>(variant), 4, links);
            m.run(5000);
            const uint32_t behind_1 = m.clients[1]->runner().next_turn_expected();
            const uint32_t at_2 = m.clients[2]->runner().next_turn_expected();
            ASSERT_TRUE(variant == 0 ? behind_1 + 2 < at_2 : at_2 + 2 < behind_1);   // the links really make them differ
            m.kill(0);
            m.run(8000);
            ASSERT_EQ(m.promotions, 1);
            ASSERT_TRUE(m.hosts[1] != nullptr);
            m.settle();
            ASSERT_TRUE(m.survivors_equal());
            for (uint8_t s = 1; s < 4; ++s) ASSERT_TRUE(m.sims[s]->is_player_dropped(0));
            ASSERT_EQ(m.sims[1]->current_tick(), m.sims[2]->current_tick());
            ASSERT_EQ(m.sims[1]->current_tick(), m.sims[3]->current_tick());
        }
    } TEST_END();

    TEST_CASE("N2.28 Host Migration: The Guest That Holds The Turns Dies While They Are Being Fetched; The Rest Goes On Without Them") {
        const LinkFn links = [](uint8_t a, uint8_t b) {
            if (a == 0) return LoopbackNetwork::Link{b == 2 ? 30u : 400u, 0u};   // only seat 2 hears the host promptly
            return LoopbackNetwork::Link{30u, 0u};
        };
        Mesh m(9, 4, links);
        m.run(5000);
        ASSERT_TRUE(m.clients[1]->runner().next_turn_expected() + 2 < m.clients[2]->runner().next_turn_expected());
        m.kill(0);
        bool cut = false;
        m.run(9000, true, [&](uint32_t) {
            if (!cut && m.clients[1] && m.clients[1]->mode() == ClientSession::Mode::Fetching) {
                cut = true;                                                  // seat 1 asks seat 2 for the turns it lacks: seat 2 dies first
                m.kill(2);
            }
        });
        ASSERT_TRUE(cut);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[1] != nullptr);
        ASSERT_EQ(m.clients[3]->host_seat(), 1);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        ASSERT_TRUE(m.sims[1]->is_player_dropped(0) && m.sims[1]->is_player_dropped(2));
        ASSERT_FALSE(m.sims[1]->is_player_dropped(3));
        ASSERT_TRUE(m.sims[3]->is_player_dropped(2));
    } TEST_END();

    TEST_CASE("N2.29 Host Migration: A Guest That Says Nothing Does Not Hold The Election Up; It Is Dropped With The Old Host") {
        Mesh m(10, 4, same_link({30, 5}));
        m.run(4000);
        m.kill(0);
        m.kill(3, false);                                                    // seat 3's machine freezes (its links stay open, it never answers)
        m.run(3000);
        ASSERT_EQ(m.promotions, 0);                                          // seat 1 waits three seconds for the answer of seat 3 ...
        m.run(3000);
        ASSERT_EQ(m.promotions, 1);                                          // ... and goes on without it
        ASSERT_TRUE(m.hosts[1] != nullptr);
        m.run(4000);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        ASSERT_TRUE(m.sims[1]->is_player_dropped(0) && m.sims[1]->is_player_dropped(3));
        ASSERT_TRUE(m.sims[2]->is_player_dropped(3));
        ASSERT_FALSE(m.sims[1]->is_player_dropped(2));
    } TEST_END();

    TEST_CASE("N2.30 Host Migration: A Host That Is Cut Off From Everybody Goes On Alone; The Guests Elect A New One And Ignore Its Past") {
        Mesh m(11, 4, same_link({30, 5}));
        m.run(4000);
        m.isolate(0);                                                        // a partition: the old host's machine is fine but nobody reaches it
        m.run(8000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[1] != nullptr);
        ASSERT_EQ(m.hosts[0]->epoch(), 0);
        ASSERT_EQ(m.hosts[1]->epoch(), 1);
        // the old host dropped everybody (its links were closed) and plays on by itself
        for (uint8_t p = 1; p < 4; ++p) ASSERT_TRUE(m.sims[0]->is_player_dropped(p));
        m.settle();
        ASSERT_TRUE(m.survivors_equal(0));                                   // the guests agree with each other
        ASSERT_TRUE(m.sims[1]->is_player_dropped(0));
    } TEST_END();

    TEST_CASE("N2.31 Host Migration: A Guest Whose Own Link To The Host Broke While The Host Lives For The Others Is Lost, Not A New Host") {
        Mesh m(12, 4, same_link({30, 0}));
        m.run(3000);
        m.cut_link(0, 3);                                                    // only seat 3 loses the host
        m.run(12000);
        ASSERT_EQ(m.promotions, 0);                                          // nobody took over: the host is alive for seats 1 and 2
        ASSERT_TRUE(m.clients[3]->lost());
        ASSERT_EQ(m.clients[1]->mode(), ClientSession::Mode::Normal);
        ASSERT_EQ(m.clients[2]->mode(), ClientSession::Mode::Normal);
        ASSERT_EQ(m.clients[1]->host_seat(), 0);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        for (uint8_t s = 0; s < 3; ++s) ASSERT_TRUE(m.sims[s]->is_player_dropped(3));   // the host dropped the one it lost
        ASSERT_FALSE(m.sims[0]->is_player_dropped(1) || m.sims[0]->is_player_dropped(2));
    } TEST_END();

    TEST_CASE("N2.32 Host Migration: Chat, Leaving And Commands All Work Through The New Host") {
        Mesh m(13, 4, same_link({30, 5}));
        m.run(3000);
        m.kill(0);
        m.run(4000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.clients[3]->chat("still here", false));
        m.hosts[1]->chat_local("the host of the rest", true);
        m.run(1000);
        for (uint8_t s = 1; s < 4; ++s) {
            bool a = false, b = false;
            for (const ChatMsg& c : m.chats[s]) {
                if (c.text == "still here") a = c.sender == 3 && !c.team;
                if (c.text == "the host of the rest") b = c.sender == 1 && c.team;
            }
            ASSERT_TRUE(a && b);
        }
        m.clients[2]->leave();                                               // seat 2 quits: the new host drops it
        m.run(2000);
        ASSERT_FALSE(m.hosts[1]->client_present(2));
        ASSERT_TRUE(m.hosts[1]->client_present(3));
        m.settle();
        ASSERT_TRUE(m.sims[1]->is_player_dropped(2));
        ASSERT_TRUE(m.sims[3]->is_player_dropped(2));
        ASSERT_TRUE(m.sims[1]->state_hash() == m.sims[3]->state_hash());
    } TEST_END();

    TEST_CASE("N2.33 Host Migration: The Host Changes Three Times In A Row; The Last Guest Ends Up Alone And Identical To What It Saw") {
        Mesh m(14, 4, same_link({30, 5}));
        m.run(4000);
        m.kill(0);
        m.run(5000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_EQ(m.hosts[1]->epoch(), 1);
        m.kill(1);
        m.run(6000);
        ASSERT_EQ(m.promotions, 2);
        ASSERT_TRUE(m.hosts[2] != nullptr);
        ASSERT_EQ(m.hosts[2]->epoch(), 2);
        ASSERT_EQ(m.clients[3]->host_seat(), 2);
        ASSERT_EQ(m.clients[3]->epoch(), 2);
        m.run(1000);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());                                    // seats 2 and 3, after two changes of host
        for (uint8_t s = 2; s < 4; ++s) ASSERT_TRUE(m.sims[s]->is_player_dropped(0) && m.sims[s]->is_player_dropped(1));
    } TEST_END();

    TEST_CASE("N2.34 Host Migration: The Session Ignores Proposals That Are Stale, Forged, From The Wrong Election Or Made While Its Host Lives") {
        LoopbackNetwork net(3);
        sim::SimulationEngine sim;
        build_world(sim, 1);
        ClientSession::Config cfg;
        cfg.player = 2;
        cfg.host = 0;
        ClientSession c(sim, cfg);
        auto host_link = net.connect({5, 0});
        auto peer1 = net.connect({5, 0});                                    // the link to seat 1
        auto peer3 = net.connect({5, 0});                                    // the link to seat 3
        c.set_connection(host_link.second);
        c.set_peer(1, peer1.second);
        c.set_peer(3, peer3.second);
        c.start(0);
        uint32_t now = 0;
        bool host_talks = true;
        auto step = [&](uint32_t ms) {
            for (uint32_t i = 0; i < ms; i += 10) {
                now += 10;
                net.set_time(now);
                if (host_talks && now % 500 == 0) host_link.first->send(encode_pong(PingMsg{1, 0}));   // a live host talks every half second
                c.update(now);
            }
        };
        auto drain = [](Connection* end) {
            std::vector<std::vector<uint8_t>> out;
            std::vector<uint8_t> m;
            while (end->poll(m)) out.push_back(m);
            return out;
        };
        auto only_pings = [](const std::vector<std::vector<uint8_t>>& msgs) {
            for (const auto& m : msgs) {
                if (peek_type(m) != MsgType::Ping) return false;
            }
            return true;
        };
        step(1000);
        drain(peer1.first);
        drain(peer3.first);
        // a proposal of the right election while the host lives for us: refused, naming the host
        peer1.first->send(encode(ProposeMsg{1, 1}));
        step(100);
        RefuseMsg refuse;
        bool refused = false;
        for (const auto& m : drain(peer1.first)) refused = refused || (peek_type(m) == MsgType::Refuse && decode(m, refuse));
        ASSERT_TRUE(refused && refuse.epoch == 1 && refuse.lowest == 0);
        ASSERT_EQ(c.mode(), ClientSession::Mode::Normal);
        // a wrong election, a candidate that is not the seat of the link, a Resume although the host lives: no answer, nothing changes
        peer1.first->send(encode(ProposeMsg{2, 1}));
        peer1.first->send(encode(ProposeMsg{0, 1}));
        peer1.first->send(encode(ProposeMsg{1, 3}));                         // seat 1's link claims to be seat 3 ...
        peer3.first->send(encode(ProposeMsg{1, 1}));                         // ... and seat 3's link claims to be seat 1
        peer1.first->send(encode(ResumeMsg{1, 1, 0}));
        step(100);
        ASSERT_TRUE(only_pings(drain(peer1.first)));
        ASSERT_TRUE(only_pings(drain(peer3.first)));
        ASSERT_EQ(c.mode(), ClientSession::Mode::Normal);
        ASSERT_EQ(c.host_seat(), 0);
        // the host falls silent (for longer than "alive", shorter than "gone"): the proposal of the lowest seat is accepted
        host_talks = false;
        step(2500);
        ASSERT_EQ(c.mode(), ClientSession::Mode::Normal);
        drain(peer1.first);
        peer1.first->send(encode(ProposeMsg{1, 1}));
        step(100);
        AcceptMsg accept;
        bool accepted = false;
        for (const auto& m : drain(peer1.first)) accepted = accepted || (peek_type(m) == MsgType::Accept && decode(m, accept));
        ASSERT_TRUE(accepted && accept.epoch == 1 && accept.next_receive == 0 && accept.next_execute == 0);
        ASSERT_EQ(c.mode(), ClientSession::Mode::Following);
        // one candidate per election: seat 3 is refused, naming the seat that has been accepted; its Resume does not count
        drain(peer3.first);
        peer3.first->send(encode(ProposeMsg{1, 3}));
        peer3.first->send(encode(ResumeMsg{1, 3, 0}));
        step(100);
        refused = false;
        for (const auto& m : drain(peer3.first)) refused = refused || (peek_type(m) == MsgType::Refuse && decode(m, refuse));
        ASSERT_TRUE(refused && refuse.lowest == 1);
        ASSERT_EQ(c.mode(), ClientSession::Mode::Following);
        // the accepted candidate's Resume ends the election: its link is the host link from now on
        peer1.first->send(encode(ResumeMsg{1, 1, 0}));
        step(100);
        ASSERT_EQ(c.mode(), ClientSession::Mode::Normal);
        ASSERT_EQ(c.host_seat(), 1);
        ASSERT_EQ(c.epoch(), 1);
        drain(peer1.first);
        ASSERT_TRUE(c.submit(cmd(CommandType::Hatch, 2)));
        step(50);
        bool command_seen = false;
        for (const auto& m : drain(peer1.first)) command_seen = command_seen || peek_type(m) == MsgType::Command;
        ASSERT_TRUE(command_seen);                                           // commands go to the new host now
    } TEST_END();

    TEST_CASE("N2.35 Host Migration: 20000 Garbage Messages On The Links Between Guests Change Nothing While The Host Lives") {
        Mesh m(15, 4, same_link({25, 0}));
        Lcg rng(4242);
        m.run(2000);
        for (int i = 0; i < 20000; ++i) {
            std::vector<uint8_t> buf(rng.below(30));
            for (auto& x : buf) x = static_cast<uint8_t>(rng.below(256));
            if (!buf.empty() && rng.below(2) == 0) buf[0] = static_cast<uint8_t>(18 + rng.below(6));   // a migration message type
            const uint8_t from = static_cast<uint8_t>(1 + rng.below(3));
            uint8_t to = static_cast<uint8_t>(1 + rng.below(3));
            if (to == from) to = static_cast<uint8_t>(from % 3 + 1);
            m.link[from][to]->send(buf);
            if (i % 200 == 0) m.run(100);
        }
        m.run(4000);
        ASSERT_EQ(m.promotions, 0);
        for (uint8_t s = 1; s < 4; ++s) ASSERT_EQ(m.clients[s]->mode(), ClientSession::Mode::Normal);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        for (uint8_t s = 0; s < 4; ++s) {
            for (uint8_t p = 0; p < 4; ++p) ASSERT_FALSE(m.sims[s]->is_player_dropped(p));
        }
    } TEST_END();

    TEST_CASE("N2.36 Turn Log: The Runner Remembers The Last 300 Turns, In Order; Older And Future Turns Are Not There; The Sequencer Resumes Where Told") {
        sim::SimulationEngine sim;
        build_world(sim, 1);
        LockstepRunner runner(sim);
        ASSERT_TRUE(runner.logged_turn(0) == nullptr);
        for (uint32_t t = 0; t < 350; ++t) {
            TurnMsg turn;
            turn.turn = t;
            turn.commands = {cmd(CommandType::Hatch, static_cast<uint8_t>(t % 4))};
            ASSERT_TRUE(runner.on_turn(turn));
        }
        ASSERT_TRUE(runner.logged_turn(49) == nullptr);                      // 350 received, the log holds 300: turns 50 ..349
        ASSERT_TRUE(runner.logged_turn(50) != nullptr && runner.logged_turn(50)->turn == 50);
        ASSERT_TRUE(runner.logged_turn(349) != nullptr && runner.logged_turn(349)->turn == 349);
        ASSERT_EQ(runner.logged_turn(349)->commands[0].issuer, 1);
        ASSERT_TRUE(runner.logged_turn(350) == nullptr);
        TurnMsg gap;
        gap.turn = 352;
        ASSERT_FALSE(runner.on_turn(gap));                                   // a gap is refused and not logged
        ASSERT_TRUE(runner.logged_turn(352) == nullptr);
        // the sequencer of a new host: continues at the turn it is told, forgets what was queued, knows how far the others got
        Sequencer seq;
        seq.set_active(0, true);
        seq.set_active(1, true);
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::Hatch, 1)));
        seq.seal();
        seq.seal();
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::Hatch, 1)));
        seq.on_hash(1, 9, sim::StateHash{1, 1, 1, 1, 1, 1, 1, 1});
        seq.resume(500);
        ASSERT_EQ(seq.next_turn(), 500u);
        ASSERT_EQ(seq.queued(), 0u);
        ASSERT_EQ(seq.acked(1), 0u);
        ASSERT_FALSE(seq.is_active(0) || seq.is_active(1));                  // the survivors are activated again by the new host
        seq.set_active(0, true);
        seq.set_active(1, true);
        ASSERT_EQ(seq.acked(1), 500u);                                       // level with the sequencer until told otherwise
        seq.set_acked(1, 480);
        ASSERT_EQ(seq.acked(1), 480u);
        seq.set_acked(1, 900);                                               // never beyond what was sealed
        ASSERT_EQ(seq.acked(1), 500u);
        seq.set_acked(1, 480);
        ASSERT_TRUE(seq.can_seal());                                         // 20 turns behind is within the flow-control bound ...
        for (uint32_t i = 0; i < 11; ++i) seq.seal();
        ASSERT_FALSE(seq.can_seal());                                        // ... 31 is beyond it: the peer is the laggard that holds the game up
        ASSERT_EQ(seq.laggard(), 1);
        ASSERT_EQ(seq.seal().turn, 511u);
    } TEST_END();
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: lock-step core (protocol, sequencer, runner, sessions)\n"
                 "=======================================================\n";
    run_protocol_tests();
    run_network_tests();
    run_sequencer_tests();
    run_runner_tests();
    run_match_tests();
    run_failure_tests();
    run_dropout_tests();
    run_migration_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
