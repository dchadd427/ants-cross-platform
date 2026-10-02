// Tests of the lock-step network core (docs/NETWORK_PORT.md): the wire protocol, the in-memory network, the host's turn sequencer, the client's
// turn runner, and whole matches of a host and three clients over links with latency and jitter, including a desync, a stalled peer, a dropped
// peer and a hostile peer.
#include "ants_net/loopback.hpp"
#include "ants_net/netgame.hpp"
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
#include <type_traits>
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

// The sessions hand the runner hooks that capture `this` (the delay meters): a copy or a move would leave them pointing at a session that is gone, so neither exists
static_assert(!std::is_copy_constructible<HostSession>::value && !std::is_copy_assignable<HostSession>::value, "a HostSession is never copied");
static_assert(!std::is_move_constructible<HostSession>::value && !std::is_move_assignable<HostSession>::value, "a HostSession is never moved");
static_assert(!std::is_copy_constructible<ClientSession>::value && !std::is_copy_assignable<ClientSession>::value, "a ClientSession is never copied");
static_assert(!std::is_move_constructible<ClientSession>::value && !std::is_move_assignable<ClientSession>::value, "a ClientSession is never moved");

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

// A connection that notes when each message is taken from it: what a host's flood control sees of a client, message by message. `hold` makes it a link whose way to the
// host is stuck: what the client sends waits in the pipe, and arrives together when it is let go.
class ArrivalLog final : public Connection {
public:
    ArrivalLog(Connection* inner, const uint32_t* clock) : inner_(inner), clock_(clock) {}
    bool send(const std::vector<uint8_t>& m) override { return inner_->send(m); }
    bool poll(std::vector<uint8_t>& m) override {
        if (hold) return false;
        if (!inner_->poll(m)) return false;
        times.push_back(*clock_);
        return true;
    }
    State state() const override { return inner_->state(); }
    void close() override { inner_->close(); }
    /// The most messages taken within any `window_ms` (a message at the start of the window counts, one at its end does not), counting from `from_ms` on
    size_t peak(uint32_t window_ms, uint32_t from_ms = 0) const {
        size_t best = 0;
        size_t lo = 0;
        for (size_t hi = 0; hi < times.size(); ++hi) {
            if (times[hi] < from_ms) {
                lo = hi + 1;
                continue;
            }
            while (times[hi] - times[lo] >= window_ms) ++lo;
            best = std::max(best, hi - lo + 1);
        }
        return best;
    }
    bool hold{false};
    std::vector<uint32_t> times;

private:
    Connection* inner_;
    const uint32_t* clock_;
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
    uint8_t frozen_mask{0};                                         // bit s: seat s's window does not run (no frames, no commands, no acks): a process that hangs, a laptop that sleeps
    uint32_t clock_lag[sim::MAX_PLAYERS]{};                         // the application's network clock never advances more than a second per frame: what a frozen window lost
    uint32_t last_frame[sim::MAX_PLAYERS]{};                        // of the real time, and when its last frame ran
    std::vector<std::unique_ptr<ArrivalLog>> arrivals;              // with log_arrivals: when the host took each message of seat s (index = seat)

    ServerMatch(uint32_t seed_, uint8_t players_, LoopbackNetwork::Link link, HostSession::Config hc = {}, ClientSession::Config cc = {}, bool log_arrivals = false)
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
            Connection* host_side = ends.first;
            if (log_arrivals) {
                arrivals.push_back(std::make_unique<ArrivalLog>(ends.first, &now));
                host_side = arrivals.back().get();
            }
            host->add_client(p, host_side);
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
                    if ((frozen_mask & (1u << p)) == 0 && script(ids, seed, now, p, c)) clients[p]->submit(c);
                }
            }
            if (each_step) each_step(now);
            host->update(now);
            for (uint8_t p = 0; p < players; ++p) {
                if ((frozen_mask & (1u << p)) != 0) continue;
                if (now - last_frame[p] > 1000 && last_frame[p] != 0) clock_lag[p] += now - last_frame[p] - 1000;   // a frame hands the network at most a second of what passed
                last_frame[p] = now;
                clients[p]->update(now - clock_lag[p]);
            }
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

// A connection that counts the messages that are taken from it: what a host polls in one update
class CountingConnection final : public Connection {
public:
    explicit CountingConnection(Connection* inner) : inner_(inner) {}
    bool send(const std::vector<uint8_t>& m) override { return inner_->send(m); }
    bool poll(std::vector<uint8_t>& m) override {
        if (!inner_->poll(m)) return false;
        ++taken;
        return true;
    }
    State state() const override { return inner_->state(); }
    void close() override { inner_->close(); }
    uint32_t taken{0};

private:
    Connection* inner_;
};

// A connection that hands the game one message every `period_ms` at the most (0: all that is there): a slow downlink. What is not handed over waits in the link, not in the game.
class SlowReader final : public Connection {
public:
    SlowReader(Connection* inner, const uint32_t* clock) : inner_(inner), clock_(clock) {}
    bool send(const std::vector<uint8_t>& m) override { return inner_->send(m); }
    bool poll(std::vector<uint8_t>& m) override {
        if (period_ms != 0 && *clock_ - last_ < period_ms) return false;
        if (!inner_->poll(m)) return false;
        last_ = *clock_;
        return true;
    }
    State state() const override { return inner_->state(); }
    void close() override { inner_->close(); }
    uint32_t period_ms{0};

private:
    Connection* inner_;
    const uint32_t* clock_;
    uint32_t last_{0};
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
    LagMsg m22;
    return decode(b, m1) || decode(b, m2) || decode(b, m3) || decode(b, m4) || decode(b, m5) || decode(b, m6) || decode(b, m7) || decode(b, m8) ||
           decode(b, m9) || decode(b, m10) || decode(b, m11) || decode(b, m12) || decode(b, m13) || decode(b, m14) || decode(b, m15) ||
           decode(b, m16) || decode(b, m17) || decode(b, m18) || decode(b, m19) || decode_ping(b.data(), b.size(), m20) || decode(b, m21) || decode(b, m22);
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
        ASSERT_EQ(kProtocolVersion, 8);                                  // 7: the room leader's START; 8: turns of 50 ms, one tick each, the adaptive buffer and the Lag message (type 25)
        ASSERT_TRUE(kTurnMs == 50 && kTicksPerTurn == 1 && kTurnsPerSecond == 20 && kHashEveryTurns == 20);     // a hash every 20 ticks, one second, as before
        ASSERT_TRUE(turns_for_ms(0) == 0 && turns_for_ms(1) == 1 && turns_for_ms(50) == 1 && turns_for_ms(51) == 2 && turns_for_ms(3000) == 60);
        ASSERT_EQ(static_cast<int>(MsgType::Last), static_cast<int>(MsgType::Lag));
        LagMsg lag;
        lag.seat = 2;
        lag.behind_ms = 12345;
        LagMsg lag2;
        ASSERT_TRUE(decode(encode(lag), lag2) && lag2.seat == 2 && lag2.behind_ms == 12345);
        ASSERT_EQ(peek_type(encode(lag)), MsgType::Lag);
        ASSERT_EQ(encode(lag).size(), size_t{6});                        // type, seat, u32 behind_ms
        lag.behind_ms = 0;                                               // "it is not lagging any more"
        ASSERT_TRUE(decode(encode(lag), lag2) && lag2.behind_ms == 0);
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
                                                       encode(ph),    encode(hello_port), encode(start), encode(lag), encode(led), encode(sr), encode(server_room_of(255))};
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
        // the Lag message names a seat of the match
        LagMsg bad_lag;
        LagMsg lag2;
        bad_lag.seat = 4;
        ASSERT_FALSE(decode(encode(bad_lag), lag2));
        bad_lag.seat = 255;
        ASSERT_FALSE(decode(encode(bad_lag), lag2));
        // unknown types (24 was one until protocol 7 gave it to StartRequest, and 25 until protocol 8 gave it to Lag)
        for (uint8_t type : std::vector<uint8_t>{0, 26, 100, 255}) {
            const std::vector<uint8_t> m = {type, 0, 0, 0, 0};
            ASSERT_EQ(peek_type(m), MsgType::None);
        }
        ASSERT_EQ(peek_type(std::vector<uint8_t>{24}), MsgType::StartRequest);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{25}), MsgType::Lag);
        ASSERT_EQ(static_cast<int>(MsgType::Last), 25);
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
                                                         encode(server_room_of(0)), encode(server_room_of(255)), encode(StartRequestMsg{}), encode(LagMsg{2, 7000})};
        size_t accepted = 0;
        for (int i = 0; i < 400000; ++i) {
            std::vector<uint8_t> buf;
            if (i % 3 == 0) {
                buf.resize(rng.below(80));
                for (auto& x : buf) x = static_cast<uint8_t>(rng.below(256));
                if (!buf.empty()) buf[0] = static_cast<uint8_t>(1 + rng.below(25));    // a plausible type byte
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
            LagMsg lg;
            if (decode(buf, lg)) { ++accepted; ASSERT_TRUE(encode(lg) == buf); }
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
        ASSERT_TRUE(kProtocolVersion >= 7);                                // protocol 7 grew the Room message by a byte and added a message type (8 keeps both): a client of protocol 6 cannot play with it
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
        // flooding: max_commands_per_turn per peer and turn; what does not fit waits for a later turn, up to max_carried_commands; a peer with more than that waiting is refused
        Sequencer::Config cfg;
        cfg.max_commands_per_turn = 3;
        cfg.max_carried_commands = 4;
        Sequencer small(cfg);
        small.set_active(1, true);
        int taken = 0;
        for (int i = 0; i < 10; ++i) taken += small.submit(1, cmd(CommandType::Hatch, 1)) ? 1 : 0;
        ASSERT_EQ(taken, 7);                                           // 3 for the turn and 4 that wait
        ASSERT_EQ(small.seal().commands.size(), 3u);
        ASSERT_EQ(small.carried(1), 1u);                               // (three of the four moved into the next turn, one still waits)
        ASSERT_TRUE(small.submit(1, cmd(CommandType::Hatch, 1)));      // and there is room to wait again
        ASSERT_EQ(small.seal().commands.size(), 3u);
    } TEST_END();

    TEST_CASE("N2.5b Sequencer: Commands Beyond The Turn's Quota Are Carried Into The Next Turns In The Order They Came (A Stuck Uplink That Is Let Go Delivers 76 Orders At Once: They Are Not A Flood), Nobody Else's Quota Is Touched, Only More Than Four Turns' Worth Waiting Is Refused") {
        Sequencer seq;                                                 // the defaults: 64 per turn, 256 waiting
        seq.set_active(0, true);
        seq.set_active(1, true);
        for (int i = 0; i < 76; ++i) ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupMove, 1, 255, static_cast<int16_t>(i), 0, {7})));   // order i goes to the tile x = i
        ASSERT_EQ(seq.queued(), 64u);
        ASSERT_EQ(seq.carried(1), 12u);
        ASSERT_TRUE(seq.submit(0, cmd(CommandType::Hatch, 0)));        // another player's command is not behind them
        ASSERT_EQ(seq.carried(0), 0u);
        const TurnMsg first = seq.seal();
        ASSERT_EQ(first.commands.size(), 65u);                         // 64 of player 1 and the one of player 0
        ASSERT_EQ(first.commands[0].issuer, 0);                        // (canonical order: by issuer)
        for (int i = 0; i < 64; ++i) {
            ASSERT_EQ(first.commands[static_cast<size_t>(i) + 1].issuer, 1);
            ASSERT_EQ(first.commands[static_cast<size_t>(i) + 1].tile_x, i);               // in the order they came
        }
        ASSERT_EQ(seq.carried(1), 0u);                                 // the other twelve are the first commands of the next turn
        ASSERT_EQ(seq.queued(), 12u);
        // a command that comes now goes behind the twelve that wait, not before them
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupMove, 1, 255, 100, 0, {7})));
        const TurnMsg second = seq.seal();
        ASSERT_EQ(second.commands.size(), 13u);
        for (int i = 0; i < 12; ++i) ASSERT_EQ(second.commands[static_cast<size_t>(i)].tile_x, 64 + i);
        ASSERT_EQ(second.commands[12].tile_x, 100);
        // a burst that fills the turn and the carry: 64 + 256 are taken, the rest refused; nobody's quota is shared
        Sequencer flood;
        flood.set_active(1, true);
        flood.set_active(2, true);
        int taken = 0;
        for (int i = 0; i < 400; ++i) taken += flood.submit(1, cmd(CommandType::Hatch, 1)) ? 1 : 0;
        ASSERT_EQ(taken, 64 + 256);
        ASSERT_EQ(flood.carried(1), 256u);
        ASSERT_TRUE(flood.submit(2, cmd(CommandType::Hatch, 2)));      // player 2 is not affected by player 1's flood
        size_t total = 0;
        uint32_t turns = 0;
        while (flood.carried(1) > 0 || flood.queued() > 0) {           // the 320 commands take five turns of 64, in order, none lost
            const TurnMsg t = flood.seal();
            total += t.commands.size();
            ++turns;
            ASSERT_TRUE(turns <= 5);
        }
        ASSERT_EQ(total, size_t{64 + 256 + 1});
        // a player that is gone has no commands waiting, and a host that resumes forgets them
        Sequencer gone;
        gone.set_active(1, true);
        for (int i = 0; i < 70; ++i) gone.submit(1, cmd(CommandType::Hatch, 1));
        ASSERT_EQ(gone.carried(1), 6u);
        gone.set_active(1, false);
        ASSERT_EQ(gone.carried(1), 0u);
        Sequencer moved;
        moved.set_active(1, true);
        for (int i = 0; i < 70; ++i) moved.submit(1, cmd(CommandType::Hatch, 1));
        moved.resume(500);
        ASSERT_TRUE(moved.carried(1) == 0 && moved.queued() == 0);
        ASSERT_FALSE(moved.submit(1, cmd(CommandType::Hatch, 1)));     // (nobody is active after a resume)
        ASSERT_EQ(moved.carried(1), 0u);
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

void run_flood_budget_tests() {
    TEST_CASE("N2.37 Flood Control: The Message Budget Is A Token Bucket (1000 A Second, A Burst Of 1000) That Is Exact To The Millisecond, Also Across The Wrap Of The 32-Bit Clock; Honest Rates Never Meet It And Floods Always Do") {
        {   // the burst, then nothing at the same instant; one millisecond gives one message back; a long pause fills it to the burst and no further
            MessageBudget b;
            for (uint32_t i = 0; i < kMessageBurst; ++i) ASSERT_TRUE(b.take(5000));
            ASSERT_FALSE(b.take(5000));
            ASSERT_FALSE(b.take(5000));
            ASSERT_TRUE(b.take(5001));
            ASSERT_FALSE(b.take(5001));
            ASSERT_TRUE(b.take(5002));
            ASSERT_FALSE(b.take(5002));
            for (uint32_t i = 0; i < kMessageBurst; ++i) ASSERT_TRUE(b.take(60000));
            ASSERT_FALSE(b.take(60000));
            ASSERT_FALSE(b.take(60000));
        }
        {   // the first message finds a full bucket whatever the clock says (a server that has just started, the signed half of the clock, the wrap), and 500 ms give back 500
            for (uint32_t start : {0u, 1u, 999u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFC00u, 0xFFFFFFFFu}) {
                MessageBudget b;
                for (uint32_t i = 0; i < kMessageBurst; ++i) ASSERT_TRUE(b.take(start));
                ASSERT_FALSE(b.take(start));
                const uint32_t later = start + 500u;                        // (wraps for the last two)
                for (uint32_t i = 0; i < 500; ++i) ASSERT_TRUE(b.take(later));
                ASSERT_FALSE(b.take(later));
            }
        }
        {   // other settings: a burst of 3, 10 a second (100 ms for one message; 99 ms are not enough)
            MessageBudget b;
            for (int i = 0; i < 3; ++i) ASSERT_TRUE(b.take(100, 3, 10));
            ASSERT_FALSE(b.take(100, 3, 10));
            ASSERT_FALSE(b.take(199, 3, 10));
            ASSERT_TRUE(b.take(200, 3, 10));
            ASSERT_FALSE(b.take(200, 3, 10));
        }
        {   // an hour at 60 (a person), 400, 900 and 1000 messages a second, spread evenly: never refused; at 1100 and 2000 a second the surplus is refused, nothing else
            for (uint32_t rate : {60u, 400u, 900u, 1000u, 1100u, 2000u}) {
                MessageBudget b;
                uint64_t refused = 0;
                uint32_t acc = 0;
                uint32_t now = 123456;
                for (uint32_t ms = 0; ms < 3600u * 1000u; ++ms) {
                    ++now;
                    acc += rate;
                    while (acc >= 1000) {
                        acc -= 1000;
                        if (!b.take(now)) ++refused;
                    }
                }
                if (rate <= kMessagesPerSecond) {
                    ASSERT_EQ(refused, 0u);
                } else {
                    const uint64_t offered = uint64_t{rate} * 3600u;
                    const uint64_t allowed = kMessageBurst + uint64_t{kMessagesPerSecond} * 3600u;
                    ASSERT_TRUE(refused + allowed >= offered - 2000u && refused + allowed <= offered + 2000u);
                }
            }
        }
        {   // three hours of silence give back no more than the burst
            MessageBudget b;
            for (uint32_t i = 0; i < kMessageBurst; ++i) ASSERT_TRUE(b.take(1000));
            const uint32_t later = 1000u + 3u * 3600u * 1000u;
            for (uint32_t i = 0; i < kMessageBurst; ++i) ASSERT_TRUE(b.take(later));
            ASSERT_FALSE(b.take(later));
        }
    } TEST_END();
}

void run_runner_tests() {
    TEST_CASE("N2.8 Lock-Step Runner: Collects A Buffer Before Starting, Runs One Tick Per Turn Every 50 ms, Applies A Turn's Commands Before Its Tick, Waits Without Debt And Runs A Long Queue Down Faster") {
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
        ASSERT_TRUE(runner.update(1000).empty());                                    // one turn is not enough to start: the buffer is one turn behind the turn that runs
        ASSERT_EQ(sim.current_tick(), 0u);
        ASSERT_EQ(runner.buffer_turns(), 1u);                                        // (a steady link: one turn of buffer, 50 ms)
        ASSERT_FALSE(runner.on_turn(turn(0)));                                       // a turn cannot repeat
        ASSERT_TRUE(runner.on_turn(turn(1)));
        auto done = runner.update(0);                                                // starting: the first tick is due at once, and a turn is one tick
        ASSERT_EQ(done.size(), 1u);
        ASSERT_EQ(done[0].turn, 0u);
        ASSERT_FALSE(done[0].has_hash);
        ASSERT_EQ(sim.current_tick(), 1u);
        ASSERT_EQ(sim.get_unit(ids.ants[0][0]).orig_order, sim::AntUnit::kOrderMove);   // the command was applied before that tick
        done = runner.update(49);
        ASSERT_TRUE(done.empty());
        ASSERT_EQ(sim.current_tick(), 1u);
        done = runner.update(1);                                                     // 50 ms: the next turn
        ASSERT_EQ(done.size(), 1u);
        ASSERT_EQ(done[0].turn, 1u);
        ASSERT_EQ(sim.current_tick(), 2u);
        ASSERT_FALSE(runner.stalled());
        // no turn queued although one is due: the runner waits at the boundary, and the buffer that the wait used up is collected again (the buffer's size is decided when the
        // late turn comes: a wait of up to 100 ms grows it by a turn, a longer one does not)
        for (int i = 0; i < 6; ++i) ASSERT_TRUE(runner.update(16).empty());
        ASSERT_TRUE(runner.stalled());
        ASSERT_TRUE(runner.rebuilding());
        ASSERT_EQ(runner.buffer_turns(), 1u);
        ASSERT_EQ(sim.current_tick(), 2u);
        const uint32_t waited = runner.stalled_ms();
        ASSERT_TRUE(waited >= 16 && waited <= 96);
        for (int i = 0; i < 100; ++i) runner.update(16);                             // a long wait is counted from the tick that was due (the message shows at one second)
        ASSERT_TRUE(runner.stalled_ms() >= 1000);
        for (uint32_t n = 2; n < 40; ++n) ASSERT_TRUE(runner.on_turn(turn(n)));
        // 38 turns queued: far more than the buffer: the first runs at once, then the queue is run down at up to four times normal speed, never faster
        done = runner.update(16);
        ASSERT_EQ(done.size(), 1u);
        ASSERT_EQ(runner.buffer_turns(), 1u);                                        // a bunch of 38 turns after a wait of 1.7 s: the freeze's own lateness, not the link's (it is left out, jitter.hpp): the buffer stays at one turn (the wait itself, over 150 ms, did not grow it either)
        ASSERT_FALSE(runner.stalled());
        ASSERT_EQ(runner.stalled_ms(), 0u);
        ASSERT_EQ(runner.speed_x4(), 16u);                                           // 37 queued, 3 wanted: the limit, 4x
        size_t hashes = 0;
        size_t ticks_in_50 = 0;
        uint32_t frames = 0;
        for (int i = 0; i < 400 && runner.queued() > 0; ++i) {
            const auto ran = runner.update(10);
            ASSERT_TRUE(ran.size() <= 1);                                            // 10 ms at 4x: not even one tick per frame is exceeded by more than one
            for (const auto& e : ran) hashes += e.has_hash ? 1 : 0;
            ticks_in_50 += ran.size();
            if (++frames % 5 == 0) {
                ASSERT_TRUE(ticks_in_50 <= 4 + 1);                                   // 50 ms of frames: at most four ticks (and one for the rounding of the quarter steps)
                ticks_in_50 = 0;
            }
        }
        for (const auto& e : runner.update(200)) hashes += e.has_hash ? 1 : 0;
        ASSERT_EQ(runner.next_turn_to_execute(), 40u);
        ASSERT_EQ(sim.current_tick(), 40u);                                          // a turn is a tick
        ASSERT_EQ(hashes, 2u);                                                       // turns 19 and 39: a hash every 20 turns (one second)
        ASSERT_TRUE(runner.stalled() == false || runner.queued() == 0);
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
            ASSERT_TRUE(m.host->turns_sealed() > 1750);                                // about 1800 turns of 50 ms
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
            ASSERT_TRUE(m.host->turns_sealed() > 1150);
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

    TEST_CASE("S2.5 Dedicated Server: A StartRequest That Reaches A Running Match (The Leader's Second Click Crossed The Start) Is Ignored And Costs Nothing (Up To The 16 That A Person Could Send, S2.6 Has The Flood); With A Payload It Is Garbage; A Host That Holds A Seat Has No Leader And Counts It As A Violation") {
        {
            ServerMatch m(1, 3, {20, 5});
            m.run(1000);
            for (uint32_t i = 0; i < kIgnoredStartRequestsAllowed; ++i) m.client_ends[0]->send(encode(StartRequestMsg{}));          // sixteen late clicks: more than the eight violations that throw a client out
            m.run(500);
            ASSERT_TRUE(m.host->client_present(0));
            ASSERT_EQ(m.host->violations(0), 0u);
            ASSERT_EQ(m.host->ignored_start_requests(), kIgnoredStartRequestsAllowed);
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

    TEST_CASE("S2.6 Dedicated Server: Flood Control In The Running Match: A Client That Floods Valid Messages (Acknowledgements, Hashes Of Turns That Were Never Sealed, Pings, Chat Lines, Late StartRequests) Is Thrown Out After About A Second's Worth And Cannot Hurt The Others; At Most 256 Messages Of A Client Are Taken Per Update; Honest Rates Never Meet A Limit") {
        const auto message_of = [](const std::string& kind, uint32_t i) -> std::vector<uint8_t> {
            if (kind == "ack") return encode(AckMsg{0});
            if (kind == "hash") {
                HashMsg h;
                h.turn = 0xFFFFFFF0u;                                                  // a turn that was never sealed: valid, and nothing happens
                return encode(h);
            }
            if (kind == "ping") return encode_ping(PingMsg{i + 1, 0});
            if (kind == "chat") {
                ChatMsg c;
                c.text = "spam";
                return encode(c);
            }
            return encode(StartRequestMsg{});                                           // "startreq"
        };
        for (const std::string kind : {"ack", "hash", "ping", "chat", "startreq"}) {
            ServerMatch m(1, 3, {20, 5});
            uint32_t chats = 0;
            m.clients[1]->set_on_chat([&](const ChatMsg&) { ++chats; });
            m.run(1000);
            for (uint32_t i = 0; i < 3000; ++i) m.client_ends[0]->send(message_of(kind, i));
            m.run(1000, false);
            ASSERT_FALSE(m.host->client_present(0));                                    // thrown out
            ASSERT_EQ(m.host->violations(0), 8u);
            ASSERT_FALSE(m.client_ends[0]->is_open());
            if (kind == "startreq") ASSERT_EQ(m.host->ignored_start_requests(), kIgnoredStartRequestsAllowed + 8u);      // the 24th is the eighth violation: the rest is never looked at
            if (kind == "chat") ASSERT_TRUE(chats >= kMessageBurst && chats <= kMessageBurst + 300);                       // a second's worth was relayed, not 3000 lines
            ASSERT_TRUE(m.host->client_present(1) && m.host->client_present(2));         // nobody else notices
            m.settle();
            ASSERT_TRUE(m.sims[1]->state_hash() == m.referee.state_hash() && m.sims[2]->state_hash() == m.referee.state_hash());
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.host->turns_sealed() > 20);
        }
        {   // at most 256 messages of a client are taken per update; the rest waits for the next (the budget is made too big to matter here)
            LoopbackNetwork net{3};
            sim::SimulationEngine referee;
            build_world(referee, 1);
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            hc.message_burst = 1000000;
            hc.messages_per_second = 1000000;
            HostSession host(referee, hc);
            auto ends = net.connect({0, 0});
            CountingConnection counted(ends.first);
            host.add_client(0, &counted);
            host.start(0);
            for (uint32_t i = 1; i <= 1000; ++i) ends.second->send(encode_ping(PingMsg{i, 0}));
            net.set_time(10);
            counted.taken = 0;
            host.update(10);
            ASSERT_EQ(counted.taken, 256u);
            host.update(11);
            ASSERT_EQ(counted.taken, 512u);
            host.update(12);
            ASSERT_EQ(counted.taken, 768u);
            host.update(13);
            ASSERT_EQ(counted.taken, 1000u);                                            // (what was left)
            ASSERT_TRUE(host.client_present(0));
        }
        {   // honest rates: 800 a second for twenty seconds, and a burst of 900 after five quiet seconds: the client stays; 1500 a second: it is out within a few seconds
            ServerMatch m(1, 3, {20, 5});
            m.run(1000);
            uint32_t n = 0;
            for (int step = 0; step < 2000; ++step) {
                for (int i = 0; i < 8; ++i) m.client_ends[0]->send(encode_ping(PingMsg{++n, 0}));
                m.run(10);
            }
            ASSERT_TRUE(m.host->client_present(0));
            m.run(5000);
            for (int i = 0; i < 900; ++i) m.client_ends[0]->send(encode_ping(PingMsg{++n, 0}));
            m.run(1000);
            ASSERT_TRUE(m.host->client_present(0) && m.host->violations(0) == 0u);
            const uint32_t before = m.now;
            int steps = 0;
            for (; steps < 1000 && m.host->client_present(0); ++steps) {
                for (int i = 0; i < 15; ++i) m.client_ends[0]->send(encode_ping(PingMsg{++n, 0}));
                m.run(10);
            }
            ASSERT_FALSE(m.host->client_present(0));
            ASSERT_TRUE(m.now - before >= 1500 && m.now - before <= 3500);
            ASSERT_TRUE(m.host->client_present(1) && m.host->client_present(2));
        }
    } TEST_END();

    TEST_CASE("S2.7 Dedicated Server: A Client That Stops Executing For 5 s Does Not Slow The Others Down; They Are Told Who Lags; It Catches Up On Its Own At Up To 4x After It Is Back; Every State Hash Agrees") {
        ServerMatch m(1, 3, {30, 5});
        m.run(3000);
        ASSERT_EQ(m.host->lagging_mask(), 0);
        const uint64_t others_before = m.sims[0]->current_tick();
        const uint32_t sealed_before = m.host->turns_sealed();
        const uint32_t frozen_at = m.now;
        m.frozen_mask = 1u << 2;                                                      // seat 2's window stops: its process hangs, the link stays open
        bool noticed = false;
        uint32_t noticed_after_ms = 0;
        m.run(5000, true, [&](uint32_t now) {
            if (!noticed && m.clients[0]->lagging_seat() == 2) {
                noticed = true;
                noticed_after_ms = now - frozen_at;
            }
        });
        // the server never stopped: 5 s are 100 turns, and the others ran them (a tick of buffer behind the newest turn and the frame's rounding are all that is missing)
        ASSERT_TRUE(m.host->turns_sealed() - sealed_before >= 99);
        ASSERT_TRUE(m.sims[0]->current_tick() - others_before >= 95);
        ASSERT_TRUE(m.sims[1]->current_tick() - others_before >= 95);
        // ... and they were told, when it was 3 s behind (and not before), once a second, naming the seat and how far behind it is
        if (!noticed || noticed_after_ms < 2500 || noticed_after_ms > 4500) std::cout << "\n    noticed after " << noticed_after_ms << " ms\n";
        ASSERT_TRUE(noticed && noticed_after_ms >= 2500 && noticed_after_ms <= 4500);       // (3 s behind: the acks that had not come back yet when it froze count 0.2 s of that)
        ASSERT_EQ(m.clients[0]->lagging_seat(), 2);
        ASSERT_EQ(m.clients[1]->lagging_seat(), 2);
        ASSERT_TRUE(m.clients[0]->lagging_behind_ms() >= 3000 && m.clients[0]->lagging_behind_ms() <= 5500);
        ASSERT_EQ(m.host->lagging_mask(), 1u << 2);
        ASSERT_TRUE(m.host->client_present(2));                                       // 5 s: not dropped
        ASSERT_EQ(m.clients[2]->lagging_seat(), 255);                                 // (it is never told about itself)
        m.run(4000);                                                                  // it stays frozen for 4 s more (9 s in all: its first frame back runs a second at 4x, 80 ticks, and 100 turns are left)
        ASSERT_TRUE(m.host->client_present(2));
        // it is back: it finds 9 s of turns waiting, says so, runs them at up to 4 times normal speed (never faster) and is level again within a few seconds
        m.frozen_mask = 0;
        const uint32_t back_at = m.now;
        bool catching = false;
        uint32_t level_after_ms = 0;
        uint64_t last_tick = m.sims[2]->current_tick();
        uint32_t window_start = m.now;
        uint64_t window_ticks = 0;
        uint64_t fastest = 0;
        bool cleared = false;
        uint32_t cleared_after_ms = 0;
        m.run(8000, true, [&](uint32_t now) {
            catching = catching || m.clients[2]->catching_up();
            const uint64_t t = m.sims[2]->current_tick();
            window_ticks += t - last_tick;
            last_tick = t;
            if (now - window_start >= 50) {                                           // ticks per 50 ms of real time: 4x normal speed is 4
                if (window_start >= back_at + 100) fastest = std::max(fastest, window_ticks);     // (the first frame stood for a second and runs the second's ticks: a frame's time is owed)
                window_ticks = 0;
                window_start = now;
            }
            if (level_after_ms == 0 && m.clients[2]->runner().next_turn_to_execute() + 8 >= m.clients[0]->runner().next_turn_to_execute()) level_after_ms = now - back_at;
            if (!cleared && m.clients[0]->lagging_seat() == 255) {
                cleared = true;
                cleared_after_ms = now - back_at;
            }
        });
        ASSERT_TRUE(catching);                                                        // "Catching up..." showed on its screen
        ASSERT_FALSE(m.clients[2]->catching_up());                                    // ... and went
        ASSERT_TRUE(level_after_ms > 0 && level_after_ms <= 4000);                    // 100 turns behind, 3 gained per 50 ms: under 2 s of work, and the first frame
        ASSERT_TRUE(fastest <= 5);                                                    // four ticks in 50 ms (one more for the rounding of a frame that is not a divisor of 50)
        ASSERT_TRUE(fastest >= 3);                                                    // and it did run fast
        ASSERT_TRUE(cleared && cleared_after_ms <= level_after_ms + 1500);            // the notice of the others ended when it was within a second of the match
        ASSERT_EQ(m.host->lagging_mask(), 0);
        ASSERT_TRUE(m.host->client_present(2));
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
        for (auto& s : m.sims) ASSERT_EQ(s->current_tick(), m.referee.current_tick());
        ASSERT_TRUE(m.all_equal());                                                   // all three and the referee: the hash of every machine is the same
    } TEST_END();

    TEST_CASE("S2.8 Dedicated Server: The Lag Policy's Numbers: A Notice From 3 s Behind Once A Second, Cleared Within 1 s, A Drop At 60 s Behind Or After 30 s Without An Ack (Whichever Comes First), Never A Wait") {
        // The test plays the clients on raw links: client 0 acknowledges every turn at once; client 1 as the test decides.
        struct Rig {
            LoopbackNetwork net{3};
            sim::SimulationEngine referee;
            std::unique_ptr<HostSession> host;
            Connection* ends[2]{};
            Connection* host_ends[2]{};
            uint32_t now{0};
            uint32_t last_turn[2]{0, 0};
            bool got_turn[2]{false, false};
            std::vector<std::pair<uint32_t, LagMsg>> notices;       // what client 0 was told (the time and the message)
            std::vector<LagMsg> told_client_1;
            Rig() {
                build_world(referee, 1);
                HostSession::Config hc;
                hc.host_player = kNoSeat;
                host = std::make_unique<HostSession>(referee, hc);
                for (int i = 0; i < 2; ++i) {
                    auto e = net.connect({5, 0});
                    host_ends[i] = e.first;
                    ends[i] = e.second;
                    host->add_client(static_cast<uint8_t>(i), e.first);
                }
                host->start(0);
            }
            // one step of 10 ms; `ack1` is the turn that client 1 acknowledges now (or -1 for none)
            void step(int64_t ack1) {
                now += 10;
                net.set_time(now);
                host->update(now);
                for (int i = 0; i < 2; ++i) {
                    std::vector<uint8_t> msg;
                    while (ends[i]->poll(msg)) {
                        if (peek_type(msg) == MsgType::Turn) {
                            TurnMsg t;
                            if (decode(msg, t)) {
                                last_turn[i] = t.turn;
                                got_turn[i] = true;
                            }
                        } else if (peek_type(msg) == MsgType::Lag) {
                            LagMsg l;
                            if (decode(msg, l)) {
                                if (i == 0) notices.emplace_back(now, l);
                                else told_client_1.push_back(l);
                            }
                        }
                    }
                }
                if (got_turn[0]) {
                    AckMsg a;
                    a.turn = last_turn[0];
                    ends[0]->send(encode(a));
                }
                if (ack1 >= 0) {
                    AckMsg a;
                    a.turn = static_cast<uint32_t>(ack1);
                    ends[1]->send(encode(a));
                }
            }
        };
        {   // 1. a client that never acknowledges: told from 3 s, once a second, dropped at 30 s (the idle rule), the match never waits
            Rig r;
            uint32_t dropped_at = 0;
            uint32_t sealed_at_10s = 0;
            while (r.now < 40000 && dropped_at == 0) {
                r.step(-1);
                if (r.now == 10000) sealed_at_10s = r.host->turns_sealed();
                if (!r.host->client_present(1)) dropped_at = r.now;
            }
            ASSERT_TRUE(dropped_at >= 30000 && dropped_at <= 30100);                      // 30 s without an ack
            ASSERT_TRUE(sealed_at_10s >= 199);                                            // the server sealed all along: 200 turns in 10 s
            ASSERT_TRUE(r.host->turns_sealed() >= 590);
            for (int i = 0; i < 40; ++i) r.step(-1);                                      // (the last notice, that of the drop, is on its way)
            ASSERT_EQ(r.told_client_1.size() + 1, r.notices.size());                      // the lagger is told too (its own link may be the slow one: its backlog is on the way, not in its queue) ...
            for (const LagMsg& l : r.told_client_1) ASSERT_EQ(l.seat, 1);                 // ... about itself, once a second like the others, only the last one (the drop ends the notice for the others, and the lagger is gone) is not
            ASSERT_TRUE(r.notices.size() >= 27 && r.notices.size() <= 29);               // one a second from 3 s to 30 s ...
            if (r.notices.empty() || r.notices.front().first < 2900 || r.notices.front().first > 3100) std::cout << "\n    first notice at " << (r.notices.empty() ? 0u : r.notices.front().first) << ", " << r.notices.size() << " notices\n";
            ASSERT_TRUE(r.notices.front().first >= 2900 && r.notices.front().first <= 3100);   // 3 s behind: sixty turns sealed and none acknowledged
            ASSERT_EQ(r.notices.front().second.seat, 1);
            for (size_t i = 1; i < r.notices.size(); ++i) {
                const uint32_t gap = r.notices[i].first - r.notices[i - 1].first;
                if (r.notices[i].second.behind_ms == 0) continue;
                ASSERT_TRUE(gap >= 990 && gap <= 1020);
                ASSERT_TRUE(r.notices[i].second.behind_ms > r.notices[i - 1].second.behind_ms || r.notices[i - 1].second.behind_ms == 0);   // the number grows while it lasts
            }
            ASSERT_EQ(r.notices.back().second.behind_ms, 0u);                             // ... and the drop ends the notice
            ASSERT_EQ(r.host->lagging_mask(), 0);
            ASSERT_TRUE(r.referee.is_player_dropped(1));                                  // the drop travelled in the turn stream
            ASSERT_FALSE(r.referee.is_player_dropped(0));
        }
        {   // 2. a client that makes progress but is 61 s behind: dropped at 60 s of it (the idle rule is never reached: it acknowledges a turn every 29 s)
            Rig r;
            uint32_t dropped_at = 0;
            uint32_t next_ack = 29000;
            uint32_t acked = 0;
            while (r.now < 80000 && dropped_at == 0) {
                int64_t ack = -1;
                if (r.now + 10 >= next_ack) {
                    ack = ++acked;                                                         // one more turn executed: progress, 29 s after the last
                    next_ack += 29000;
                }
                r.step(ack);
                if (!r.host->client_present(1)) dropped_at = r.now;
            }
            ASSERT_TRUE(dropped_at >= 59900 && dropped_at <= 60200);                       // 60 s behind
        }
        {   // 3. hysteresis and clearing: 5 s behind (told), 2 s behind (neither new nor cleared), within 1 s (cleared once); nothing is repeated after that
            Rig r;
            int64_t ack = -1;
            uint32_t phase_end = 5500;
            std::vector<LagMsg> seen;
            while (r.now < 12000) {
                // phase 1 (until 5.5 s): no acks. phase 2: ack up to 2 s behind. phase 3: ack up to 0.3 s behind
                if (r.now >= phase_end && r.now < 9000) ack = r.host->turns_sealed() > 40 ? static_cast<int64_t>(r.host->turns_sealed()) - 40 : 0;       // 40 turns: 2 s
                else if (r.now >= 9000) ack = static_cast<int64_t>(r.last_turn[1]);
                r.step(ack);
                ack = -1;
            }
            ASSERT_TRUE(r.host->client_present(1));
            // told while it lasted from 3 s; not cleared by the 2 s phase; cleared once in the last phase
            size_t zero_notices = 0;
            for (const auto& n : r.notices) zero_notices += n.second.behind_ms == 0 ? 1 : 0;
            ASSERT_EQ(zero_notices, size_t{1});
            ASSERT_TRUE(r.notices.back().second.behind_ms == 0);
            ASSERT_TRUE(r.notices.back().first >= 9000 && r.notices.back().first <= 9200);
            ASSERT_EQ(r.host->lagging_mask(), 0);
        }
        {   // 4. a client that is quiet because there is nothing to run (the match is frozen) is not dropped for it
            Rig r;
            for (int i = 0; i < 300; ++i) r.step(-1);
            r.host->freeze();
            for (int i = 0; i < 4500; ++i) r.step(static_cast<int64_t>(r.last_turn[1]));  // 45 s: acks are all there is: behind 0
            ASSERT_TRUE(r.host->client_present(1));
        }
    } TEST_END();

    TEST_CASE("S2.9 Dedicated Server: A Player With A Round Trip Of 200 ms (And One With 200 - 400 ms) Is Never Called A Lagger, Never Catches Up, And Nobody Is Dropped") {
        for (const LoopbackNetwork::Link link : {LoopbackNetwork::Link{100, 0}, LoopbackNetwork::Link{100, 100}}) {
            ServerMatch m(5, 4, link);
            bool anybody_told = false;
            m.run(60000, true, [&](uint32_t) {
                if (m.host->lagging_mask() != 0) anybody_told = true;
                for (auto& c : m.clients) {
                    if (c->lagging_seat() != 255 || c->catching_up()) anybody_told = true;
                }
            });
            ASSERT_FALSE(anybody_told);
            for (uint8_t p = 0; p < 4; ++p) ASSERT_TRUE(m.host->client_present(p));
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("S2.10 Dedicated Server: The End Of A Match Over A Spiky Link: Every Guest Runs Every Turn The Referee Sealed And Reaches Its Final Tick (The Host Freezes When The Match Ends; A Guest That Stalled Near The End Waited For A Buffer That Could Not Come And Never Saw The End)") {
        // 2 % of the messages are held up by 250 ms (a resend), the ones behind them wait; sixty matches of two guests, each ends at another moment
        uint32_t guests = 0;
        uint32_t missed = 0;
        for (uint32_t match = 1; match <= 60; ++match) {
            ServerMatch m(match * 7919u + 3, 2, {30, 0, 250, 20});
            m.run(24000 + (match % 17) * 50);
            m.settle(6000);                                                           // the match ends: the host stops sealing, everything in flight arrives
            for (uint8_t p = 0; p < 2; ++p) {
                ++guests;
                if (m.clients[p]->runner().next_turn_to_execute() != m.host->turns_sealed() || m.sims[p]->current_tick() != m.referee.current_tick()) ++missed;
            }
            ASSERT_TRUE(m.host->desyncs().empty());
        }
        if (missed != 0) std::cout << "\n    " << missed << " of " << guests << " guests did not reach the final tick\n";
        ASSERT_EQ(missed, 0u);
    } TEST_END();

    TEST_CASE("S2.11 Dedicated Server: A Player Whose Own Link Is The Slow One Is Told That It Lags (Its Backlog Is On The Way, Not In Its Queue: \"Catching up...\" Had Nothing To Say And Nobody Told It Until It Was Dropped At 60 s); Everybody Else Is Told Too, And The Notice Ends When It Is Level") {
        ServerMatch m(1, 3, {30, 5});
        SlowReader slow(m.client_ends[2], &m.now);
        m.clients[2]->set_connection(&slow);
        m.run(3000);
        ASSERT_EQ(m.clients[2]->self_lag_behind_ms(), 0u);
        slow.period_ms = 150;                                                       // seat 2's downlink hands the game 7 messages a second against the 21 that the room sends
        bool told_self = false;
        bool catching = false;
        bool others_told = false;
        uint32_t told_at = 0;
        const uint32_t from = m.now;
        m.run(40000, true, [&](uint32_t now) {
            if (!told_self && m.clients[2]->self_lag_behind_ms() > 0) {
                told_self = true;
                told_at = now - from;
            }
            catching = catching || m.clients[2]->catching_up();
            others_told = others_told || (m.clients[0]->lagging_seat() == 2 && m.clients[1]->lagging_seat() == 2);
            ASSERT_TRUE(m.clients[2]->lagging_seat() == 255);                       // (the OTHER players: nobody else lags)
        });
        if (!told_self) std::cout << "\n    the laggard was never told\n";
        ASSERT_TRUE(told_self);                                                    // it is told, although nothing is queued in its game
        ASSERT_TRUE(told_at >= 3000 && told_at <= 30000);                          // (the notice comes behind the turns that wait in front of it)
        ASSERT_FALSE(catching);                                                    // "Catching up..." never had anything to say: the turns it misses are still in the link
        ASSERT_TRUE(others_told);
        ASSERT_TRUE(m.clients[2]->self_lag_behind_ms() >= 3000);
        ASSERT_TRUE(m.host->client_present(2));
        // the link is back: it reads what waited, catches up, and the notice ends for everybody
        slow.period_ms = 0;
        m.run(20000);
        ASSERT_EQ(m.clients[2]->self_lag_behind_ms(), 0u);
        ASSERT_EQ(m.clients[0]->lagging_seat(), 255);
        ASSERT_FALSE(m.clients[2]->catching_up());
        ASSERT_TRUE(m.clients[2]->runner().next_turn_to_execute() + 10 >= m.clients[0]->runner().next_turn_to_execute());
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("S2.12 Dedicated Server: A Player Who Was Dropped For Being Away Is Told Why When It Is Back (Its Link Was Closed While Half A Minute Of The Match Waited Unplayed); A Link That Fails By Itself, Or A Server That Goes Silent, Is Only A Lost Connection") {
        ASSERT_EQ(match_lost_text(ClientSession::LostReason::AwayTooLong), std::string("You were away too long and were dropped from the match."));
        ASSERT_EQ(match_lost_text(ClientSession::LostReason::Connection), std::string("The connection to the other players was lost."));
        ASSERT_EQ(match_lost_text(ClientSession::LostReason::None), std::string("The connection to the other players was lost."));
        {   // seat 2's process stops for 35 s: the server drops it at 30 s without an acknowledgement and closes the link; when the process runs again it reads the turns that waited, then the close
            ServerMatch m(1, 3, {30, 5});
            m.run(3000);
            m.frozen_mask = 1u << 2;
            m.run(35000);
            ASSERT_FALSE(m.host->client_present(2));
            ASSERT_TRUE(m.clients[0]->mode() == ClientSession::Mode::Normal && !m.clients[0]->lost());
            ASSERT_FALSE(m.clients[2]->lost());                                      // (it has not run: it does not know yet)
            m.frozen_mask = 0;
            m.run(1000);
            ASSERT_TRUE(m.clients[2]->lost());
            ASSERT_TRUE(m.clients[2]->lost_reason() == ClientSession::LostReason::AwayTooLong);
            ASSERT_TRUE(m.clients[2]->runner().backlog_ms() >= kAwayBacklogMs);
            ASSERT_FALSE(m.clients[0]->lost());                                      // the others play on
        }
        {   // a link that fails while the player plays: nothing waits in its queue
            ServerMatch m(1, 3, {30, 5});
            m.run(5000);
            m.net.cut(m.client_ends[1]);
            m.run(500);
            ASSERT_TRUE(m.clients[1]->lost());
            ASSERT_TRUE(m.clients[1]->lost_reason() == ClientSession::LostReason::Connection);
            ASSERT_TRUE(m.clients[1]->runner().backlog_ms() < kAwayBacklogMs);
        }
        {   // a server that says nothing for 10 s (the link stays open): lost, and not "dropped"
            ServerMatch m(1, 3, {30, 5});
            m.run(5000);
            uint32_t t = m.now;
            while (m.now < t + 12000) {
                m.now += 10;
                m.net.set_time(m.now);
                m.clients[1]->update(m.now);                                         // the host is not updated: no turns, no pongs
            }
            ASSERT_TRUE(m.clients[1]->lost());
            ASSERT_TRUE(m.clients[1]->lost_reason() == ClientSession::LostReason::Connection);
        }
        {   // a player that is behind by more than a minute is dropped by the other rule (60 s): the same message
            ServerMatch m(1, 3, {30, 5});
            m.run(3000);
            m.frozen_mask = 1u << 2;
            m.run(26000);
            ASSERT_TRUE(m.host->client_present(2));                                  // 26 s: still a lagger
            m.frozen_mask = 0;
            ASSERT_FALSE(m.clients[2]->lost());
        }
    } TEST_END();

    TEST_CASE("S2.13 Dedicated Server: A Stuck Uplink That Is Let Go After 5 s Delivers The 76 Orders Of A Player Who Gave 15 A Second: Nobody Is Dropped For It, Every Order Is Run In The Order It Was Given, And Every Machine Runs The Same Turns (The 64 A Turn Of The Sequencer Made 12 Of Them Violations: Eight Throw The Player Out)") {
        ServerMatch m(1, 3, {30, 5}, {}, {}, true);
        std::vector<int16_t> run_order;                                             // the orders of seat 1 as the referee executes them
        m.host->runner().set_on_command([&](const sim::Command& c, const sim::CommandResult&) {
            if (c.issuer == 1 && c.type == CommandType::GroupMove) run_order.push_back(c.tile_x);
        });
        m.run(10000, false);
        const uint32_t stuck_from = m.now;
        m.arrivals[1]->hold = true;                                                 // seat 1's way to the host is stuck: what it sends waits in the pipe
        int16_t next_order = 0;
        uint32_t next_at = m.now;
        m.run(5000, false, [&](uint32_t now) {
            if (now >= next_at) {
                next_at += 1000 / 15;                                               // 15 orders a second
                m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, next_order++, 0, {m.ids.ants[1][0]}));
            }
        });
        ASSERT_TRUE(next_order >= 74 && next_order <= 76);
        ASSERT_TRUE(m.now - stuck_from == 5000);
        m.arrivals[1]->hold = false;                                                // let go: everything arrives in one poll
        m.run(15000, false);
        m.settle();
        ASSERT_TRUE(m.host->client_present(1));                                    // not dropped
        ASSERT_EQ(m.host->violations(1), 0u);                                      // not a single one counted
        ASSERT_EQ(run_order.size(), static_cast<size_t>(next_order));              // every order was run ...
        for (size_t i = 0; i < run_order.size(); ++i) ASSERT_EQ(run_order[i], static_cast<int16_t>(i));       // ... in the order that it was given
        // the turns that carry them are the same on every machine: the referee's log and every client's log hold the same bytes
        const uint32_t sealed = m.host->turns_sealed();
        ASSERT_TRUE(sealed > 400);
        const uint32_t first_logged = sealed > LockstepRunner::kTurnLogTurns - 10 ? sealed - (static_cast<uint32_t>(LockstepRunner::kTurnLogTurns) - 10) : 0u;      // (the runners keep the last 600 turns)
        for (uint32_t t = first_logged; t < sealed; ++t) {
            const TurnMsg* host_turn = m.host->runner().logged_turn(t);
            ASSERT_TRUE(host_turn != nullptr);
            const std::vector<uint8_t> bytes = encode(*host_turn);
            for (auto& c : m.clients) {
                const TurnMsg* turn = c->runner().logged_turn(t);
                ASSERT_TRUE(turn != nullptr && encode(*turn) == bytes);
            }
        }
        // the 64 a turn of the sequencer shows in the turns: the turn that came first carried 64 of seat 1, the next the other twelve (or fewer, when the orders came in two bunches)
        size_t most = 0;
        for (uint32_t t = first_logged; t < sealed; ++t) {
            size_t n = 0;
            for (const sim::Command& c : m.host->runner().logged_turn(t)->commands) n += (c.issuer == 1 && c.type == CommandType::GroupMove) ? 1 : 0;
            most = std::max(most, n);
        }
        ASSERT_EQ(most, size_t{64});
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("S2.14 Dedicated Server: The Small Rules Of The Lag Policy That No Other Test Pinned: The Ack Of A Frame That Ran Several Turns Is For The Last, A Notice That Is Not Renewed Goes Stale (3 s; 10 s For The Notice About Oneself), Notices About Oneself Are Kept Apart, A Late Pass Seals At Most Ten Turns, The Server's Host Never Waits, And \"Catching Up...\" Begins At Sixty Turns") {
        {   // the ack of a frame that ran many turns is for the LAST of them (the host sees a machine that is level, not one that is as far behind as the frame began)
            ServerMatch m(1, 3, {30, 5});
            m.run(3000);
            const uint32_t last_clock = m.now;
            m.frozen_mask = 1u << 1;
            m.run(3000);                                                            // seat 1 stands still: 60 turns are waiting for it
            ASSERT_TRUE(m.host->behind_ms(1) >= 3000);
            m.frozen_mask = 0;
            m.now += 10;
            m.net.set_time(m.now);
            m.clients[1]->update(last_clock + 1000);                               // ONE frame: it runs the turns that it has (a second, at four times the speed: 80 ticks at the most) and acks once
            ASSERT_TRUE(m.clients[1]->runner().next_turn_to_execute() > 55);
            for (int i = 0; i < 20; ++i) {                                          // the host takes the ack (nobody else runs a frame of seat 1)
                m.now += 10;
                m.net.set_time(m.now);
                m.host->update(m.now);
            }
            if (m.host->behind_ms(1) > 500) std::cout << "\n    behind " << m.host->behind_ms(1) << " ms after one frame that ran " << m.clients[1]->runner().next_turn_to_execute() << " turns\n";
            ASSERT_TRUE(m.host->behind_ms(1) <= 500);
            // (and what the server's host says of such a player: it is the server's, so it never waits, and names nobody as the one that holds the game up)
            ASSERT_FALSE(m.host->waiting());
            ASSERT_EQ(m.host->laggard(), 255);
        }
        {   // the server's host: a player 5 s behind is announced, and the host neither waits nor names a laggard (HostSession::waiting / laggard are for a host with a seat)
            ServerMatch m(1, 3, {30, 5});
            m.run(3000);
            m.frozen_mask = 1u << 2;
            m.run(5000);
            ASSERT_EQ(m.host->lagging_mask(), 1u << 2);
            ASSERT_TRUE(m.host->behind_ms(2) >= 4000);
            ASSERT_FALSE(m.host->waiting());
            ASSERT_EQ(m.host->laggard(), 255);
        }
        {   // a notice that is not renewed goes stale after 3 s, one about the client itself after 10 s; a notice about itself never names the others' seat
            LoopbackNetwork net(1);
            sim::SimulationEngine sim;
            build_world(sim, 1);
            ClientSession::Config cc;
            cc.player = 0;
            cc.host = kNoSeat;
            cc.migration = false;
            ClientSession cs(sim, cc);
            auto ends = net.connect({1, 0});
            cs.set_connection(ends.second);
            cs.start(0);
            ASSERT_TRUE(ends.first->send(encode(LagMsg{2, 5000})));
            ASSERT_TRUE(ends.first->send(encode(LagMsg{0, 8000})));                   // about this client itself
            uint32_t now = 0;
            const auto step_to = [&](uint32_t t) {
                while (now < t) {
                    ++now;
                    net.set_time(now);
                    if (now % 2000 == 0) ends.first->send(encode(TurnMsg{now / 50, {}}));         // (keeps the host's silence from being noticed: any message is a sign of life)
                    cs.update(now);
                }
            };
            step_to(20);
            ASSERT_EQ(cs.lagging_seat(), 2);
            ASSERT_EQ(cs.lagging_behind_ms(), 5000u);
            ASSERT_EQ(cs.self_lag_behind_ms(), 8000u);
            step_to(2900);
            ASSERT_EQ(cs.lagging_seat(), 2);                                          // 2.9 s after it came
            step_to(3100);
            ASSERT_EQ(cs.lagging_seat(), 255);                                        // gone: not renewed
            ASSERT_EQ(cs.lagging_behind_ms(), 0u);
            ASSERT_EQ(cs.self_lag_behind_ms(), 8000u);                                // the one about itself lasts longer
            step_to(9900);
            ASSERT_EQ(cs.self_lag_behind_ms(), 8000u);
            step_to(10100);
            ASSERT_EQ(cs.self_lag_behind_ms(), 0u);
            // renewed and ended
            ASSERT_TRUE(ends.first->send(encode(LagMsg{0, 4000})));
            step_to(10200);
            ASSERT_EQ(cs.self_lag_behind_ms(), 4000u);
            ASSERT_TRUE(ends.first->send(encode(LagMsg{0, 0})));                       // "back within a second"
            step_to(10300);
            ASSERT_EQ(cs.self_lag_behind_ms(), 0u);
            ASSERT_EQ(cs.lagging_seat(), 255);
        }
        {   // a pass that comes late seals the turns it missed, ten at the most (half a second), and the schedule goes on in the passes that follow
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            sim::SimulationEngine sim;
            build_world(sim, 1);
            HostSession host(sim, hc);
            host.start(0);
            host.update(2000);                                                     // forty turns are due
            ASSERT_EQ(host.turns_sealed(), 10u);
            host.update(2000);
            ASSERT_EQ(host.turns_sealed(), 20u);
            host.update(2000);
            host.update(2000);
            host.update(2000);
            ASSERT_EQ(host.turns_sealed(), 41u);                                   // all the turns up to 2000 ms (0, 50, ... 2000), then the schedule is level
            host.update(2049);
            ASSERT_EQ(host.turns_sealed(), 41u);
            host.update(2050);
            ASSERT_EQ(host.turns_sealed(), 42u);
        }
        {   // "Catching up..." begins when 60 turns (3 s) of the match wait unplayed in the queue, not before
            for (const uint32_t queued : {60u, 61u}) {
                LoopbackNetwork net(1);
                sim::SimulationEngine sim;
                build_world(sim, 1);
                ClientSession::Config cc;
                cc.player = 0;
                cc.host = kNoSeat;
                cc.migration = false;
                ClientSession cs(sim, cc);
                auto ends = net.connect({1, 0});
                cs.set_connection(ends.second);
                cs.start(0);
                for (uint32_t t = 0; t < queued; ++t) ASSERT_TRUE(ends.first->send(encode(TurnMsg{t, {}})));
                net.set_time(10);
                cs.update(10);                                                      // the runner starts and runs one turn: queued - 1 stay
                ASSERT_EQ(cs.runner().backlog_ms(), (queued - 1) * 50);
                ASSERT_EQ(cs.catching_up(), queued - 1 >= 60);                      // 59 turns are 2950 ms, 60 are 3000
            }
        }
    } TEST_END();

    TEST_CASE("N2.38 Flood Control In A Match Whose Host Holds A Seat (A LAN Game): A Guest That Floods Valid Messages Is Thrown Out, The Host And The Other Guest Play On Bit-Identically") {
        for (const std::string kind : {"ack", "ping", "chat", "hash"}) {
            Match m(1, 3, {20, 0});
            m.run(1000);
            for (uint32_t i = 0; i < 3000; ++i) {
                if (kind == "ack") m.client_ends[2]->send(encode(AckMsg{0}));
                else if (kind == "ping") m.client_ends[2]->send(encode_ping(PingMsg{i + 1, 0}));
                else if (kind == "chat") {
                    ChatMsg c;
                    c.text = "spam";
                    m.client_ends[2]->send(encode(c));
                } else {
                    HashMsg h;
                    h.turn = 0xFFFFFFF0u;
                    m.client_ends[2]->send(encode(h));
                }
            }
            m.run(1000, false);
            ASSERT_FALSE(m.host->client_present(2));
            ASSERT_EQ(m.host->violations(2), 8u);
            ASSERT_TRUE(m.host->client_present(1));
            m.settle();
            ASSERT_TRUE(m.sims[1]->state_hash() == m.sims[0]->state_hash());
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.host->turns_sealed() > 20);
        }
    } TEST_END();

    TEST_CASE("N2.39 Flood Control With Turns Of 50 ms: What An Honest Client Sends Stays Far Below The Budget Of 1000 A Second (Steady, A Person Who Gives 20 Orders A Second, A Window That Catches Up At 4x After A Freeze, An Uplink That Delivers 25 s At Once)") {
        // The budget (flood.hpp) was set for turns of 100 ms. With turns of 50 ms a client acknowledges twice as often, so the claim "an honest client is far below it" is measured again:
        // every message that the host takes from a seat is noted with the time it was taken, and the most that any second holds is the client's peak. (The sequencer takes 64 orders
        // a turn from a seat, 1280 a second: a client at that rate is not a person and meets the budget first.)
        const size_t margin = kMessagesPerSecond / 5;                                  // the claim: five times below the budget at the least
        const auto report = [](const char* what, size_t peak) { std::cout << "\n      [flood budget] " << what << ": peak " << peak << " messages in one second (budget " << kMessagesPerSecond << ")"; };
        {   // 1. steady play over links of 60 ms with jitter: the order of 20 acknowledgements, a hash and a ping a second, and the scripted orders (2.5 a second)
            ServerMatch m(1, 4, {60, 30}, {}, {}, true);
            m.run(60000);
            size_t peak = 0;
            for (uint8_t p = 0; p < 4; ++p) {
                peak = std::max(peak, m.arrivals[p]->peak(1000));
                ASSERT_TRUE(m.host->client_present(p) && m.host->violations(p) == 0u);
            }
            report("steady play", peak);
            ASSERT_TRUE(peak >= 20 && peak <= margin);                                 // (20 turns a second are acknowledged: the test does measure something)
        }
        {   // 2. a person who gives 20 orders a second (more than a hand can) on top of that
            ServerMatch m(2, 4, {40, 20}, {}, {}, true);
            m.run(30000, true, [&](uint32_t t) {
                if (t % 50 != 0) return;
                for (uint8_t p = 0; p < 4; ++p) m.clients[p]->submit(cmd(CommandType::Stop, p, 255, 0, 0, {m.ids.ants[p][0]}));
            });
            size_t peak = 0;
            for (uint8_t p = 0; p < 4; ++p) {
                peak = std::max(peak, m.arrivals[p]->peak(1000));
                ASSERT_TRUE(m.host->client_present(p) && m.host->violations(p) == 0u);
            }
            report("20 orders a second", peak);
            ASSERT_TRUE(peak >= 40 && peak <= margin);
        }
        {   // 3. a window that did not run for 10 s and catches up at up to four times the speed: still one acknowledgement per turn that ran, at most
            ServerMatch m(3, 3, {30, 10}, {}, {}, true);
            m.run(5000);
            m.frozen_mask = 1u << 1;
            m.run(10000);
            m.frozen_mask = 0;
            const uint32_t back = m.now;
            m.run(15000);
            const size_t peak = m.arrivals[1]->peak(1000, back);
            report("catching up at 4x after a freeze of 10 s", peak);
            ASSERT_TRUE(peak >= 40 && peak <= margin);
            ASSERT_TRUE(m.host->client_present(1) && m.host->violations(1) == 0u);
            m.settle();
            ASSERT_TRUE(m.all_equal());                                                // it caught up: every machine ends at the same state
            ASSERT_TRUE(m.host->desyncs().empty());
        }
        {   // 4. an uplink that is stuck for 25 s (the longest that a server tolerates is 30 s without an acknowledgement) and delivers everything at once
            ServerMatch m(4, 3, {30, 10}, {}, {}, true);
            m.run(5000);
            m.arrivals[2]->hold = true;
            m.run(25000);
            ASSERT_TRUE(m.host->client_present(2));
            ASSERT_EQ(m.host->lagging_mask() & 4u, 4u);                                 // (the others were told, as the policy says)
            m.arrivals[2]->hold = false;
            const uint32_t release = m.now;
            m.run(5000);
            const size_t burst = m.arrivals[2]->peak(1000, release);
            report("an uplink that delivers 25 s at once", burst);
            ASSERT_TRUE(burst >= 400 && burst < kMessageBurst);                         // a real burst, and the burst of the budget holds it
            ASSERT_TRUE(m.host->client_present(2) && m.host->violations(2) == 0u);
            ASSERT_EQ(m.host->lagging_mask() & 4u, 0u);                                 // its acknowledgements are all there now: it is level again
            m.settle();
            ASSERT_TRUE(m.all_equal());
        }
        std::cout << "\n     ";                                                          // (the report lines above end the line: PASS gets one of its own)
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
        ASSERT_TRUE(stalled_at <= sealed_before_stall + 60 + 90);                           // at most max lag (3 s) + what was in flight
        // the freeze ends: it catches up and the match goes on
        m.run(8000, false);
        ASSERT_FALSE(m.host->waiting());
        ASSERT_TRUE(m.host->turns_sealed() > stalled_at + 60);
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
        ASSERT_TRUE(m.host->turns_sealed() > 140);                                          // the game did not stop for it
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
        ASSERT_TRUE(m.host->turns_sealed() > 40);                       // the game went on for everybody else
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
        ASSERT_TRUE(m.host->turns_sealed() > 140);
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
        ASSERT_TRUE(m.host->turns_sealed() > sealed_while_waiting + 40);             // the game goes on for the others
        m.host->freeze();
        run_without_client_2(3000);
        ASSERT_TRUE(m.sims[0]->is_player_dropped(2));
        ASSERT_TRUE(m.sims[1]->is_player_dropped(2));
        ASSERT_TRUE(m.sims[1]->state_hash() == m.sims[0]->state_hash());
    } TEST_END();

    TEST_CASE("N2.21 Runner Hooks: One Call Per Tick (one per turn) And One Per Command With The Engine's Verdict, In Order") {
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
        ASSERT_EQ(tick_times.size(), 3u);                                                           // one tick per turn
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
            ASSERT_TRUE(m.hosts[0]->turns_sealed() > 100);
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

    TEST_CASE("N2.36 Turn Log: The Runner Remembers The Last 600 Turns (30 s), In Order; Older And Future Turns Are Not There; The Sequencer Resumes Where Told") {
        sim::SimulationEngine sim;
        build_world(sim, 1);
        LockstepRunner runner(sim);
        ASSERT_TRUE(runner.logged_turn(0) == nullptr);
        ASSERT_EQ(LockstepRunner::kTurnLogTurns, size_t{600});               // 30 s of play at 20 turns a second (it was 300 turns of 100 ms)
        for (uint32_t t = 0; t < 650; ++t) {
            TurnMsg turn;
            turn.turn = t;
            turn.commands = {cmd(CommandType::Hatch, static_cast<uint8_t>(t % 4))};
            ASSERT_TRUE(runner.on_turn(turn));
        }
        ASSERT_TRUE(runner.logged_turn(49) == nullptr);                      // 650 received, the log holds 600: turns 50 ..649
        ASSERT_TRUE(runner.logged_turn(50) != nullptr && runner.logged_turn(50)->turn == 50);
        ASSERT_TRUE(runner.logged_turn(649) != nullptr && runner.logged_turn(649)->turn == 649);
        ASSERT_EQ(runner.logged_turn(649)->commands[0].issuer, 1);
        ASSERT_TRUE(runner.logged_turn(650) == nullptr);
        TurnMsg gap;
        gap.turn = 652;
        ASSERT_FALSE(runner.on_turn(gap));                                   // a gap is refused and not logged
        ASSERT_TRUE(runner.logged_turn(652) == nullptr);
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
        ASSERT_TRUE(seq.can_seal());                                         // 20 turns behind is within the flow-control bound (60 turns: 3 s) ...
        for (uint32_t i = 0; i < 41; ++i) seq.seal();
        ASSERT_FALSE(seq.can_seal());                                        // ... 61 is beyond it: the peer is the laggard that holds the game up
        ASSERT_EQ(seq.laggard(), 1);
        ASSERT_EQ(seq.seal().turn, 541u);
    } TEST_END();
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: lock-step core (protocol, sequencer, runner, sessions)\n"
                 "=======================================================\n";
    run_protocol_tests();
    run_network_tests();
    run_sequencer_tests();
    run_flood_budget_tests();
    run_runner_tests();
    run_match_tests();
    run_failure_tests();
    run_dropout_tests();
    run_migration_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
