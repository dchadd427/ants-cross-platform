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

// ---------------------------------------------------------------------------------------------------------------------------------

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
        // every valid message is rejected with a byte too many and with any byte missing
        const std::vector<std::vector<uint8_t>> all = {encode(hello), encode(w), encode(r), encode(cm), encode(t), encode(a), encode(hm), encode(d), encode(c)};
        for (const auto& m : all) {
            std::vector<uint8_t> longer = m;
            longer.push_back(0);
            for (size_t cut = 0; cut < m.size(); ++cut) {
                std::vector<uint8_t> shorter(m.begin(), m.begin() + static_cast<std::ptrdiff_t>(cut));
                HelloMsg x1; WelcomeMsg x2; RejectMsg x3; CommandMsg x4; TurnMsg x5; AckMsg x6; HashMsg x7; DesyncMsg x8; ChatMsg x9;
                const bool any = decode(shorter, x1) || decode(shorter, x2) || decode(shorter, x3) || decode(shorter, x4) || decode(shorter, x5) ||
                                 decode(shorter, x6) || decode(shorter, x7) || decode(shorter, x8) || decode(shorter, x9);
                ASSERT_FALSE(any);
            }
            HelloMsg x1; WelcomeMsg x2; RejectMsg x3; CommandMsg x4; TurnMsg x5; AckMsg x6; HashMsg x7; DesyncMsg x8; ChatMsg x9;
            const bool any = decode(longer, x1) || decode(longer, x2) || decode(longer, x3) || decode(longer, x4) || decode(longer, x5) ||
                             decode(longer, x6) || decode(longer, x7) || decode(longer, x8) || decode(longer, x9);
            ASSERT_FALSE(any);
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
        bytes[1] = 6;
        ASSERT_FALSE(decode(bytes, r));
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
        bytes = encode(HelloMsg{kProtocolVersion, "Bob"});
        bytes[4] = 7;
        ASSERT_FALSE(decode(bytes, h));
        // unknown types
        for (uint8_t type : std::vector<uint8_t>{0, 12, 100, 255}) {
            const std::vector<uint8_t> m = {type, 0, 0, 0, 0};
            ASSERT_EQ(peek_type(m), MsgType::None);
        }
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
                                                         encode(ChatMsg{1, true, "hello"}), encode(HelloMsg{1, "Ann"}), encode(WelcomeMsg{1, 4})};
        size_t accepted = 0;
        for (int i = 0; i < 400000; ++i) {
            std::vector<uint8_t> buf;
            if (i % 3 == 0) {
                buf.resize(rng.below(80));
                for (auto& x : buf) x = static_cast<uint8_t>(rng.below(256));
                if (!buf.empty()) buf[0] = static_cast<uint8_t>(1 + rng.below(11));    // a plausible type byte
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
        }
        ASSERT_TRUE(accepted > 5000);                  // the mutations of valid messages do get through
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
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
