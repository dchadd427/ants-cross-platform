// Tests of the lock-step network core (docs/NETWORK_PORT.md): the wire protocol, the in-memory network, the host's turn sequencer, the client's
// turn runner, and whole matches of a host and three clients over links with latency and jitter, including a desync, a stalled peer, a dropped
// peer and a hostile peer.
#include "ants_net/lobby.hpp"
#include "ants_net/loopback.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/sequencer.hpp"
#include "ants_net/session.hpp"
#include "ants_net/attendance.hpp"
#include "ants_net/turnlog.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
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
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; the suite as run_tests.sh runs it has no filter)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
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

Ids build_world(sim::SimulationEngine& sim, uint32_t seed, uint32_t match_ms = 720000) {
    Ids ids;
    sim.init_test_world(60, 60, seed, match_ms);
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
    PresenceMsg m23;
    VoteMsg m24;
    CatchUpMsg m25;
    TurnBatchMsg m26;
    CaughtUpMsg m27;
    return decode(b, m1) || decode(b, m2) || decode(b, m3) || decode(b, m4) || decode(b, m5) || decode(b, m6) || decode(b, m7) || decode(b, m8) ||
           decode(b, m9) || decode(b, m10) || decode(b, m11) || decode(b, m12) || decode(b, m13) || decode(b, m14) || decode(b, m15) ||
           decode(b, m16) || decode(b, m17) || decode(b, m18) || decode(b, m19) || decode_ping(b.data(), b.size(), m20) || decode(b, m21) || decode(b, m22) ||
           decode(b, m23) || decode(b, m24) || decode(b, m25) || decode(b, m26) || decode(b, m27);      // (protocol 10: Presence, Vote, CatchUp, TurnBatch, CaughtUp)
}

// A key that is not zero, and different for every `salt`
SeatKey key_with(uint8_t salt) {
    SeatKey k{};
    for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(salt * 31u + i * 7u + 1u);
    return k;
}

// What a Hello of protocols 6 to 9 looked like (they shared the layout: protocol 9 changed the rules of the match, not a message): no key, no turns. Protocol 10 adds both.
std::vector<uint8_t> old_layout_hello(uint16_t version, const std::string& name, const std::string& room = std::string(), const std::string& token = std::string()) {
    std::vector<uint8_t> out = {static_cast<uint8_t>(MsgType::Hello), static_cast<uint8_t>(version & 0xFF), static_cast<uint8_t>(version >> 8), static_cast<uint8_t>(name.size())};
    const auto put = [&out](const std::string& s) {                  // (byte by byte: GCC 12 reads a range insert into a vector this small as an overread and the project builds with -Werror)
        for (const char c : s) out.push_back(static_cast<uint8_t>(c));
    };
    put(name);
    out.push_back(0x34);                                              // listen_port 0x1234
    out.push_back(0x12);
    out.push_back(255);                                               // want_seat
    out.push_back(static_cast<uint8_t>(room.size()));
    put(room);
    out.push_back(static_cast<uint8_t>(token.size()));
    put(token);
    return out;
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

// ---------------------------------------------------------------------------------------------------------------------------------
// Reconnect (protocol 10): a dedicated server's match that HOLDS the seat of a player whose connection is lost
// ---------------------------------------------------------------------------------------------------------------------------------

// A connection on the host's side that can be told to say nothing (`silent`: what comes from the client is not delivered: a half-open link, a power cut), to drop what it
// is told to (`swallow_acks`: the client's acknowledgements and hashes never arrive: it talks, but the host cannot see it run) and that counts what it sends (the bytes that a
// stream puts on the link)
class HostTap final : public Connection {
public:
    HostTap(Connection* inner, const uint32_t* clock) : inner_(inner), clock_(clock) {}
    bool send(const std::vector<uint8_t>& m) override {
        sent_bytes += m.size();
        ++sent_messages;
        ++sent_by_type[static_cast<size_t>(peek_type(m)) % sent_by_type.size()];
        if (fail_send_of_turn != UINT32_MAX && peek_type(m) == MsgType::Turn) {      // the link breaks in the very send of this turn
            TurnMsg t;
            if (decode(m, t) && t.turn == fail_send_of_turn) {
                inner_->close();
                return false;
            }
        }
        return inner_->send(m);
    }
    bool poll(std::vector<uint8_t>& m) override {
        if (silent) return false;
        while (inner_->poll(m)) {
            const MsgType t = peek_type(m);
            if (swallow_acks && (t == MsgType::TurnAck || t == MsgType::Hash)) {
                if (t == MsgType::TurnAck && let_one_ack_through_every_ms != 0 && static_cast<uint32_t>(*clock_ - last_ack_through_) >= let_one_ack_through_every_ms) {
                    last_ack_through_ = *clock_;                                   // one acknowledgement in a while: it makes progress, slowly
                    return true;
                }
                continue;
            }
            return true;
        }
        return false;
    }
    State state() const override { return inner_->state(); }
    void close() override { inner_->close(); }
    size_t sent_of(MsgType t) const { return sent_by_type[static_cast<size_t>(t) % sent_by_type.size()]; }
    bool silent{false};
    bool swallow_acks{false};
    uint32_t let_one_ack_through_every_ms{0};
    uint32_t fail_send_of_turn{UINT32_MAX};                         // the send of this turn fails (and closes the link): a connection that breaks while the host broadcasts
    size_t sent_bytes{0};
    size_t sent_messages{0};
    std::array<size_t, 32> sent_by_type{};

private:
    Connection* inner_;
    const uint32_t* clock_;
    uint32_t last_ack_through_{0};
};

// The bytes that a host has put on a link and the machine at the other end has not taken yet: what a WebSocket's output buffer holds (it fails the connection at 1 MB). Both ends
// of a link share one of these; the host's end fails the link when the backlog passes `limit`.
struct LinkAudit {
    size_t sent{0};
    size_t taken{0};
    size_t peak{0};
    size_t limit{1024 * 1024};
    bool failed{false};
};
class AuditedEnd final : public Connection {
public:
    AuditedEnd(Connection* inner, LinkAudit* audit, bool host_side) : inner_(inner), audit_(audit), host_side_(host_side) {}
    bool send(const std::vector<uint8_t>& m) override {
        if (audit_->failed) return false;
        if (host_side_) {
            audit_->sent += m.size();
            audit_->peak = std::max(audit_->peak, audit_->sent - audit_->taken);
            if (audit_->sent - audit_->taken > audit_->limit) {                    // the backlog passed what the transport takes: the link fails
                audit_->failed = true;
                inner_->close();
                return false;
            }
        }
        return inner_->send(m);
    }
    bool poll(std::vector<uint8_t>& m) override {
        if (!inner_->poll(m)) return false;
        if (!host_side_) audit_->taken += m.size();
        return true;
    }
    State state() const override { return audit_->failed ? State::Failed : inner_->state(); }
    void close() override { inner_->close(); }

private:
    Connection* inner_;
    LinkAudit* audit_;
    bool host_side_;
};

// The options of a HoldMatch (an aggregate with defaults: members are set one by one, designated initializers are not C++17)
struct HoldOptions {
    LoopbackNetwork::Link link{30, 10};
    HostSession::Config host;                   // hold_seats is switched on by the rig unless `hold` is false
    ClientSession::Config client;               // reconnect, key and hello are filled in by the rig
    bool hold{true};                            // false: the same rig with a host that does not hold seats (today's rules)
    bool reconnect{true};                       // the clients' sessions reconnect
    uint32_t origin{0};                         // the clock at the start of the match
    uint8_t bot_mask{0};                        // seats that are bots: no connection, never absent
    bool keys{true};                            // the host knows the keys of the seats
    bool rejoin_start{true};                    // the host has a Start message to give a machine that comes back with nothing (the room always has)
    uint32_t seed{1};
    uint32_t match_ms{720000};                  // the match's clock (build_world): the match ends when it runs out (12 minutes by default)
};

// A dedicated server's match in which a lost seat is held: ServerMatch with hold_seats on, the keys of the seats, a DOOR that takes a Hello with a key to
// HostSession::accept_rejoin (what the room manager does), the machines' OWN reconnect (a new link when the session asks for one: what NetGame does in release B), and a
// machine that is RELOADED (a new engine and the real ClientLobby, then a session that starts by catching up: what a reloaded page does).
struct HoldMatch {
    LoopbackNetwork net;
    sim::SimulationEngine referee;
    std::vector<std::unique_ptr<sim::SimulationEngine>> sims;       // index = seat (null for a bot's seat)
    Ids ids;
    std::unique_ptr<HostSession> host;
    std::vector<std::unique_ptr<ClientSession>> clients;            // index = seat; null while a machine is being reloaded (and for a bot's seat)
    std::vector<std::unique_ptr<ClientLobby>> lobbies;              // a machine that is being reloaded plays the lobby first
    std::vector<std::unique_ptr<HostTap>> taps;                     // the host's end of each machine's FIRST link
    std::vector<Connection*> client_ends;                           // the link that a machine uses now (cut() cuts it)
    std::array<SeatKey, sim::MAX_PLAYERS> keys{};
    std::vector<Connection*> doorway;                               // the host's ends of new links that have not said Hello yet
    std::vector<std::pair<Connection*, Connection*>> new_links;     // every link made after the start: (the host's end, the machine's end)
    LoopbackNetwork::Link link;
    HostSession::Config host_cfg;
    ClientSession::Config client_cfg;
    uint32_t now{0};
    uint32_t origin{0};                                             // the clock at the start: the scripted commands depend on the time since then, not on the clock
    uint32_t match_ms{720000};
    uint32_t seed{1};
    uint8_t seats{3};
    uint8_t bot_mask{0};
    uint8_t frozen_mask{0};                                         // bit s: seat s's machine does not run (no frames, no commands, no acks, no pings)
    bool auto_reconnect[sim::MAX_PLAYERS]{true, true, true, true};  // whether a machine opens a new link when its session asks for one
    uint32_t clock_lag[sim::MAX_PLAYERS]{};
    uint32_t last_frame[sim::MAX_PLAYERS]{};
    std::function<void(sim::SimulationEngine&)> tamper_reloaded;    // runs on the engine of a machine that is reloaded (to make its state differ)
    std::vector<ChatMsg> chats[sim::MAX_PLAYERS];                   // what each machine's session reported as chat
    std::map<Connection*, Connection*> raw_of;                      // an audited end -> the loopback end behind it (the network cuts only its own)
    bool audit_new_links{false};
    size_t audit_limit{1024 * 1024};
    std::vector<std::unique_ptr<LinkAudit>> audits;
    std::vector<std::unique_ptr<AuditedEnd>> audited;
    uint32_t doors_answered{0};                                     // the Hellos that the door took to the session
    uint32_t refusals{0};                                           // ... and the ones that the session refused

    explicit HoldMatch(uint8_t seats_, const HoldOptions& o = HoldOptions{})
        : net(o.seed * 19u), link(o.link), host_cfg(o.host), client_cfg(o.client), now(o.origin), origin(o.origin), match_ms(o.match_ms), seed(o.seed), seats(seats_), bot_mask(o.bot_mask) {
        ids = build_world(referee, seed, match_ms);
        host_cfg.host_player = kNoSeat;
        host_cfg.hold_seats = o.hold;
        host = std::make_unique<HostSession>(referee, host_cfg);
        StartMsg start;
        start.seed = seed;
        start.map_name = "TEST.LVL";
        start.roster = static_cast<uint8_t>((1u << seats) - 1u);
        for (uint8_t p = 0; p < seats; ++p) {
            if ((bot_mask & (1u << p)) != 0) {
                start.names[p] = "Bot (Test)";
                continue;
            }
            start.names[p] = "Seat " + std::to_string(p);
            keys[p] = o.keys ? key_with(static_cast<uint8_t>(p + 1)) : SeatKey{};
        }
        if (o.keys) host->set_seat_keys(keys);
        if (o.rejoin_start) host->set_rejoin_start(start);
        sims.resize(seats);
        clients.resize(seats);
        lobbies.resize(seats);
        client_ends.assign(seats, nullptr);
        taps.resize(seats);
        for (uint8_t p = 0; p < seats; ++p) {
            if ((bot_mask & (1u << p)) != 0) {
                host->add_bot_seat(p);
                continue;
            }
            sims[p] = std::make_unique<sim::SimulationEngine>();
            build_world(*sims[p], seed, match_ms);
            auto ends = net.connect(link);
            client_ends[p] = ends.second;
            taps[p] = std::make_unique<HostTap>(ends.first, &now);
            host->add_client(p, taps[p].get());
            ClientSession::Config c = client_cfg;
            c.player = p;
            c.host = kNoSeat;
            c.migration = false;
            c.reconnect = o.reconnect;
            c.key = keys[p];
            c.hello.name = start.names[p];
            clients[p] = std::make_unique<ClientSession>(*sims[p], c);
            clients[p]->set_connection(ends.second);
            clients[p]->set_on_chat([this, p](const ChatMsg& m) { chats[p].push_back(m); });
        }
        host->start(now);
        for (auto& c : clients) {
            if (c) c->start(now);
        }
    }

    bool human(uint8_t p) const { return p < seats && (bot_mask & (1u << p)) == 0; }
    uint32_t clock_of(uint8_t p) const { return now - clock_lag[p]; }

    // The door: the first message of a new link is a Hello; one with a key goes to the session (the room's door does this, room_manager.cpp)
    void poll_door() {
        for (size_t i = 0; i < doorway.size();) {
            Connection* c = doorway[i];
            std::vector<uint8_t> msg;
            if (c->poll(msg)) {
                HelloMsg h;
                if (peek_type(msg) == MsgType::Hello && decode(msg, h)) {
                    ++doors_answered;
                    if (!host->accept_rejoin(c, h, now)) ++refusals;
                } else {
                    c->close();
                }
                doorway.erase(doorway.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            if (!c->is_open()) {
                doorway.erase(doorway.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }
    }
    // A new link between a machine and the door (with audit_new_links: both ends watch the bytes in flight, see LinkAudit)
    std::pair<Connection*, Connection*> open_link(LoopbackNetwork::Link l) {
        auto ends = net.connect(l);
        if (audit_new_links) {
            audits.push_back(std::make_unique<LinkAudit>());
            audits.back()->limit = audit_limit;
            audited.push_back(std::make_unique<AuditedEnd>(ends.first, audits.back().get(), true));
            audited.push_back(std::make_unique<AuditedEnd>(ends.second, audits.back().get(), false));
            raw_of[audited[audited.size() - 2].get()] = ends.first;
            raw_of[audited.back().get()] = ends.second;
            ends = {audited[audited.size() - 2].get(), audited.back().get()};
        }
        doorway.push_back(ends.first);
        new_links.push_back(ends);
        return ends;
    }
    std::pair<Connection*, Connection*> open_link() { return open_link(link); }

    // The machine of `seat` is replaced by one that has nothing but the key: a new engine, the lobby, and then a session that starts by catching up (a reloaded page)
    void reload(uint8_t seat, const SeatKey& key) {
        cut_link(client_ends[seat]);                                       // the old page is gone
        clients[seat].reset();
        lobbies[seat].reset();
        sims[seat] = std::make_unique<sim::SimulationEngine>();
        build_world(*sims[seat], seed, match_ms);
        if (tamper_reloaded) tamper_reloaded(*sims[seat]);
        auto ends = open_link();
        client_ends[seat] = ends.second;
        ClientLobby::Config lc;
        lc.name = "Seat " + std::to_string(seat);
        lc.key = key;
        lobbies[seat] = std::make_unique<ClientLobby>(ends.second, lc);
        chats[seat].clear();
    }
    void reload(uint8_t seat) { reload(seat, keys[seat]); }
    // The link of a machine is cut (both ends see it closed); the machine reconnects at once when its session wants to and `auto_reconnect` allows
    void cut(uint8_t seat) { cut_link(client_ends[seat]); }
    void cut_link(Connection* end) {
        if (end == nullptr) return;
        const auto it = raw_of.find(end);
        net.cut(it == raw_of.end() ? end : it->second);
    }

    void step_machines() {
        for (uint8_t p = 0; p < seats; ++p) {
            if (!human(p)) continue;
            if ((frozen_mask & (1u << p)) != 0) continue;
            if (lobbies[p]) {                                              // a reloaded machine: the lobby, then the session
                ClientLobby& l = *lobbies[p];
                l.update(now);
                for (const ClientLobby::Event& ev : l.take_events()) {
                    if (ev.type == ClientLobby::Event::Type::StartRequested) {
                        l.report_loaded(true);
                    } else if (ev.type == ClientLobby::Event::Type::Begun) {
                        ClientSession::Config c = client_cfg;
                        c.player = l.my_seat();
                        c.host = kNoSeat;
                        c.migration = false;
                        c.reconnect = true;
                        c.key = l.key();
                        c.hello.name = "Seat " + std::to_string(p);
                        c.rejoin = true;
                        clients[p] = std::make_unique<ClientSession>(*sims[p], c);
                        clients[p]->set_connection(client_ends[p]);
                        clients[p]->set_on_chat([this, p](const ChatMsg& m) { chats[p].push_back(m); });
                        clients[p]->start(clock_of(p));                         // (the clock that this machine's session is updated with: a machine that lags behind has its own)
                        last_frame[p] = 0;                                      // (a page that was reloaded has had no frame yet: the time in the lobby is no stop of this session's frames, which would put its clock back)
                    } else if (ev.type == ClientLobby::Event::Type::Rejected) {
                        rejected_lobby[p] = l.reject_reason();
                    }
                }
                if (clients[p]) lobbies[p].reset();
            }
            if (!clients[p]) continue;
            if (last_frame[p] != 0 && now - last_frame[p] > 1000) clock_lag[p] += now - last_frame[p] - 1000;      // a frame after a stop hands the network a second at the most
            last_frame[p] = now;
            const uint32_t t = clock_of(p);
            if (auto_reconnect[p] && clients[p]->wants_connection(t)) {
                auto ends = open_link();
                client_ends[p] = ends.second;
                clients[p]->attach(ends.second, t);
            }
            clients[p]->update(t);
        }
    }
    RejectReason rejected_lobby[sim::MAX_PLAYERS]{RejectReason::BadRequest, RejectReason::BadRequest, RejectReason::BadRequest, RejectReason::BadRequest};

    // Advances virtual time by `ms` in 10 ms steps; the machines that follow the match issue their scripted commands when `scripted`
    void run(uint32_t ms, bool scripted = true, const std::function<void(uint32_t)>& each_step = {}) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
            now += 10;
            net.set_time(now);
            if (scripted) {
                Command c;
                for (uint8_t p = 0; p < seats; ++p) {
                    if (human(p) && clients[p] && (frozen_mask & (1u << p)) == 0 && script(ids, seed, now - origin, p, c)) clients[p]->submit(c);
                }
            }
            if (each_step) each_step(now);
            poll_door();
            host->update(now);
            step_machines();
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            run(10);
        }
        return cond();
    }
    void settle(uint32_t ms = 3000) {
        host->freeze();
        run(ms, false);
    }
    // The referee and every machine that follows the match have the same state
    bool all_equal() const {
        for (uint8_t p = 0; p < seats; ++p) {
            if (!human(p) || !clients[p] || clients[p]->mode() != ClientSession::Mode::Normal) continue;
            if (sims[p]->state_hash() != referee.state_hash()) return false;
        }
        return true;
    }
    uint32_t sealed() const { return host->turns_sealed(); }
};

// A link that is still being made when it is handed over (a TCP connection that opens without blocking): Connecting until `open_at`, then it is the link behind it
class OpensLater final : public Connection {
public:
    OpensLater(Connection* inner, const uint32_t* clock, uint32_t open_at) : inner_(inner), clock_(clock), open_at_(open_at) {}
    bool send(const std::vector<uint8_t>& m) override { return opened() && inner_->send(m); }
    bool poll(std::vector<uint8_t>& m) override { return opened() && inner_->poll(m); }
    State state() const override { return opened() ? inner_->state() : State::Connecting; }
    void close() override { inner_->close(); }

private:
    bool opened() const { return static_cast<int32_t>(*clock_ - open_at_) >= 0; }
    Connection* inner_;
    const uint32_t* clock_;
    uint32_t open_at_;
};

// A ClientSession on its own, a scripted server at the other end of its links: what it does with every message and every state of its link. The scripted server hands out turns
// without commands, so it knows the state hash that a machine must report after any number of them (hash_after).
struct LoneSession {
    struct Wire {
        Connection* srv{nullptr};                                   // the scripted server's end
        Connection* cli{nullptr};                                   // the machine's end
        std::vector<std::vector<uint8_t>> heard;                    // everything that the server read on this link
        bool pong{false};                                           // the scripted server lives: it answers every ping
        void pump() {
            std::vector<uint8_t> m;
            while (srv->poll(m)) {
                heard.push_back(m);
                PingMsg p;
                if (pong && peek_type(m) == MsgType::Ping && decode_ping(m.data(), m.size(), p) && srv->is_open()) srv->send(encode_pong(p));
            }
        }
        size_t count(MsgType t) const {
            size_t n = 0;
            for (const auto& m : heard) n += peek_type(m) == t ? 1u : 0u;
            return n;
        }
    };
    LoopbackNetwork net{9};
    sim::SimulationEngine sim;
    std::unique_ptr<ClientSession> s;
    std::vector<std::unique_ptr<Wire>> wires;
    uint32_t now{5000};
    SeatKey key;
    uint32_t turns_given{0};                                        // the turns the scripted server has handed out (empty ones), numbered from 0

    explicit LoneSession(ClientSession::Config c = {}, bool reconnect = true) {
        build_world(sim, 1);
        key = key_with(7);
        c.player = 1;
        c.host = kNoSeat;
        c.migration = false;
        c.reconnect = reconnect;
        c.key = key;
        c.hello.name = "Lone";
        c.hello.room = "R-1";
        c.hello.token = "tok";
        s = std::make_unique<ClientSession>(sim, c);
        Wire& w = open();
        s->set_connection(w.cli);
        s->start(now);
    }
    Wire& open() {
        auto ends = net.connect({10, 0});
        wires.push_back(std::make_unique<Wire>());
        wires.back()->srv = ends.first;
        wires.back()->cli = ends.second;
        return *wires.back();
    }
    Wire& first() { return *wires.front(); }
    Wire& last() { return *wires.back(); }
    void step(uint32_t ms = 10) {
        for (uint32_t t = 0; t < ms; t += 10) {
            now += 10;
            net.set_time(now);
            s->update(now);
            for (auto& w : wires) w->pump();
        }
    }
    // A wake-up of a page that was not woken for `real_ms` of real time, as the application does it: the clock of the session moves by at most `cap_ms`, the session reads what waited
    // (and the scripted server answers what it hears), and the rest of the gap is handed over afterwards; when it touched a stamp the session is judged at once, with the same clock
    void wake(uint32_t real_ms, uint32_t cap_ms = 1000) {
        const uint32_t advance = std::min(real_ms, cap_ms);
        now += advance;
        net.set_time(now);
        s->update(now);
        for (auto& w : wires) w->pump();
        if (real_ms > advance && s->note_gap(real_ms - advance)) {
            s->update(now);
            for (auto& w : wires) w->pump();
        }
    }
    // The scripted server hands out `n` turns without commands, one message each, on the wire
    void give_turns(Wire& w, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            TurnMsg t;
            t.turn = turns_given++;
            w.srv->send(encode(t));
        }
    }
    // What the state of a machine that has run `turns` empty turns is
    sim::StateHash hash_after(uint32_t turns) {
        sim::SimulationEngine fresh;
        build_world(fresh, 1);
        for (uint32_t i = 0; i < turns; ++i) fresh.tick();
        return fresh.state_hash();
    }
    // The machine loses its link (the network cuts it) and its session notices on its next update
    void lose_link() {
        net.cut(wires.back()->cli);
        step(20);
    }
    // The owner makes a new link when the session asks, and hands it over
    Wire& attach_new() {
        Wire& w = open();
        s->attach(w.cli, now);
        return w;
    }
    WelcomeMsg rejoin_welcome() const {
        WelcomeMsg w;
        w.player = 1;
        w.players = 3;
        w.key = key;
        w.flags = kWelcomeRejoin;
        return w;
    }
};

void run_protocol_tests() {
    TEST_CASE("N2.1 Protocol: Every Message Round-Trips And Trailing Or Missing Bytes Are Rejected") {
        ASSERT_EQ(kProtocolVersion, 13);                                 // 7: the room leader's START; 8: turns of 50 ms, one tick each, the adaptive buffer and the Lag message (type 25); 9: the community-map rules; 10: keys, presence, votes and the catch-up stream (types 26 - 30); 11: the leader's START carries a fill level, chat in the waiting room; 12: the match clock waits for the start dialog (the first turn is sealed kMatchStartDelayMs after the match began, a dialog ends with the first turn that executes: no message changed); 13: a level for each seat of the fill and the teams (StartRequest is seven bytes, Start ends with the two team bytes)
        ASSERT_TRUE(kTurnMs == 50 && kTicksPerTurn == 1 && kTurnsPerSecond == 20 && kHashEveryTurns == 20);     // a hash every 20 ticks, one second, as before
        ASSERT_TRUE(turns_for_ms(0) == 0 && turns_for_ms(1) == 1 && turns_for_ms(50) == 1 && turns_for_ms(51) == 2 && turns_for_ms(3000) == 60);
        ASSERT_EQ(static_cast<int>(MsgType::Last), static_cast<int>(MsgType::CaughtUp));
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
        // protocol 10: a Hello and a Welcome that carry a key (the five new messages are N2.40's)
        HelloMsg keyed;
        keyed.name = "Ann";
        keyed.room = "ROOM-7";
        keyed.key = key_with(1);
        keyed.have_turns = 1234;
        HelloMsg keyed2;
        ASSERT_TRUE(decode(encode(keyed), keyed2) && keyed2.key == key_with(1) && keyed2.have_turns == 1234 && keyed2.room == "ROOM-7");
        WelcomeMsg welcome_key;
        welcome_key.player = 3;
        welcome_key.players = 4;
        welcome_key.key = key_with(2);
        welcome_key.flags = kWelcomeRejoin;
        WelcomeMsg welcome_key2;
        ASSERT_TRUE(decode(encode(welcome_key), welcome_key2) && welcome_key2.key == key_with(2) && welcome_key2.flags == kWelcomeRejoin && welcome_key2.player == 3);
        // every valid message is rejected with a byte too many and with any byte missing
        const std::vector<std::vector<uint8_t>> all = {encode(hello), encode(w),  encode(r),  encode(cm), encode(t),  encode(a),  encode(hm),
                                                       encode(d),     encode(c),  encode(pr), encode(ac), encode(rf), encode(rs), encode(rq),
                                                       encode(ph),    encode(hello_port), encode(start), encode(lag), encode(led), encode(sr), encode(server_room_of(255)),
                                                       encode(keyed), encode(welcome_key)};
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
        bytes[1] = 10;                                                   // the reasons are 1 .. 9 (6 = NoSuchRoom, protocol 6; 7 - 9 = Dropped, RejoinFailed, Superseded, protocol 10)
        ASSERT_FALSE(decode(bytes, r));
        bytes[1] = 255;
        ASSERT_FALSE(decode(bytes, r));
        bytes[1] = 6;
        ASSERT_TRUE(decode(bytes, r) && r.reason == RejectReason::NoSuchRoom);
        bytes[1] = 9;
        ASSERT_TRUE(decode(bytes, r) && r.reason == RejectReason::Superseded);
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
        // unknown types (24 was one until protocol 7 gave it to StartRequest, 25 until protocol 8 gave it to Lag, and 26 - 30 until protocol 10 gave them to Presence, Vote, CatchUp, TurnBatch, CaughtUp)
        for (uint8_t type : std::vector<uint8_t>{0, 31, 100, 255}) {
            const std::vector<uint8_t> m = {type, 0, 0, 0, 0};
            ASSERT_EQ(peek_type(m), MsgType::None);
        }
        ASSERT_EQ(peek_type(std::vector<uint8_t>{24}), MsgType::StartRequest);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{25}), MsgType::Lag);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{26}), MsgType::Presence);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{30}), MsgType::CaughtUp);
        ASSERT_EQ(static_cast<int>(MsgType::Last), 30);
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
        HelloMsg keyed;                                                // the messages of protocol 10 as seeds (N2.40 has the rules one by one; this is the net under them)
        keyed.name = "Ann";
        keyed.room = "ROOM-7";
        keyed.token = "tok";
        keyed.key = key_with(3);
        keyed.have_turns = 700;
        const std::vector<uint8_t> seed_hello = encode(keyed);
        WelcomeMsg welcome;
        welcome.player = 2;
        welcome.players = 4;
        welcome.key = key_with(4);
        welcome.flags = kWelcomeRejoin;
        const std::vector<uint8_t> seed_welcome = encode(welcome);
        PresenceMsg presence;
        presence.missing = {{1, PresenceMsg::State::Absent, 42, 0}, {3, PresenceMsg::State::CatchingUp, 31, 63}};
        presence.vote_seat = 1;
        presence.votes_continue = 1;
        presence.voters = 2;
        presence.your_vote = 2;
        presence.cap_s = 1700;
        const std::vector<uint8_t> seed_presence = encode(presence);
        StartRequestMsg request13;                                     // protocol 13: a level for each seat and a pair of seats as a team (N2.96 has the rules one by one)
        request13.fill = {FillLevel::None, FillLevel::Easy, FillLevel::Medium, FillLevel::Hard};
        request13.set_teams(sim::StartTeams{true, 1, 2});
        const std::vector<uint8_t> seed_request13 = encode(request13);
        StartMsg start13;
        start13.map_name = "TREASURE.LVL";
        start13.roster = 0x0F;
        start13.names = {"A", "B", "C", "D"};
        start13.set_teams(sim::StartTeams{true, 0, 3});
        const std::vector<uint8_t> seed_start13 = encode(start13);
        TurnBatchMsg batch;
        batch.first_turn = 500;
        batch.turns.resize(3);
        batch.turns[0].commands = {cmd(CommandType::GroupMove, 1, 255, 3, 4, {1, 2}), cmd(CommandType::Hatch, 2)};
        batch.turns[2].commands = {cmd(CommandType::Stop, 0, 255, 0, 0, {9})};
        for (uint32_t i = 0; i < 3; ++i) batch.turns[i].turn = 500 + i;
        const std::vector<uint8_t> seed_batch = encode(batch);
        const std::vector<std::vector<uint8_t>> seeds = {encode(turn), encode(hash), encode(CommandMsg{cmd(CommandType::GroupAttack, 2, 255, 5, 5, {1})}),
                                                         encode(ChatMsg{1, true, "hello"}), encode(hello_of(1, "Ann")), encode(WelcomeMsg{1, 4}),
                                                         encode(ProposeMsg{1, 2}), encode(AcceptMsg{1, 100, 98}), encode(RefuseMsg{1, 1}),
                                                         encode(ResumeMsg{1, 2, 500}), encode(RequestMsg{40}), encode(PeerHelloMsg{3}),
                                                         encode(server_room_of(0)), encode(server_room_of(255)), encode(StartRequestMsg{}), encode(LagMsg{2, 7000}), seed_hello,
                                                         seed_welcome, encode(RejectMsg{RejectReason::Superseded}), seed_presence, encode(VoteMsg{2, true}), encode(CatchUpMsg{100, 4000}),
                                                         seed_batch, encode(CaughtUpMsg{4000, {1, 2, 3, 4, 5, 6, 7, 8}}), seed_request13, seed_start13};
        size_t accepted = 0;
        for (int i = 0; i < 400000; ++i) {
            std::vector<uint8_t> buf;
            if (i % 3 == 0) {
                buf.resize(rng.below(80));
                for (auto& x : buf) x = static_cast<uint8_t>(rng.below(256));
                if (!buf.empty()) buf[0] = static_cast<uint8_t>(1 + rng.below(30));    // a plausible type byte
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
            StartMsg smsg;
            LagMsg lg;
            RejectMsg rj;
            PresenceMsg pres;
            VoteMsg vt;
            CatchUpMsg cu;
            TurnBatchMsg tb;
            CaughtUpMsg cg;
            if (decode(buf, lg)) { ++accepted; ASSERT_TRUE(encode(lg) == buf); }
            if (decode(buf, rj)) { ++accepted; ASSERT_TRUE(encode(rj) == buf); }
            if (decode(buf, pres)) {                   // a Presence that gets through is one that an honest server could have said, and encodes back to the same bytes
                ++accepted;
                ASSERT_TRUE(encode(pres) == buf);
                ASSERT_TRUE(pres.missing.size() <= sim::MAX_PLAYERS && pres.votes_continue <= pres.voters && pres.voters <= sim::MAX_PLAYERS && pres.your_vote <= 2);
                ASSERT_TRUE(pres.vote_seat == 255 || (pres.vote_seat < sim::MAX_PLAYERS && !pres.missing.empty()));
            }
            if (decode(buf, vt)) { ++accepted; ASSERT_TRUE(encode(vt) == buf && buf.size() == 3); }
            if (decode(buf, cu)) { ++accepted; ASSERT_TRUE(encode(cu) == buf && cu.first_turn <= cu.total_turns); }
            if (decode(buf, tb)) {
                ++accepted;
                ASSERT_TRUE(encode(tb) == buf);
                ASSERT_TRUE(!tb.turns.empty() && tb.turns.size() <= kMaxBatchTurns && buf.size() <= kMaxMessageBytes);
                for (size_t k = 0; k < tb.turns.size(); ++k) ASSERT_TRUE(tb.turns[k].turn == tb.first_turn + k && tb.turns[k].commands.size() <= kMaxTurnCommands);
            }
            if (decode(buf, cg)) { ++accepted; ASSERT_TRUE(encode(cg) == buf && buf.size() == 69); }
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
            if (decode(buf, sreq)) {                   // (protocol 13: the type, a level for each seat, two team bytes: none, or two different seats)
                ++accepted;
                ASSERT_TRUE(encode(sreq) == buf && buf.size() == 7);
                for (const FillLevel level : sreq.fill) ASSERT_TRUE(static_cast<uint8_t>(level) <= kFillLevelLast);
                ASSERT_TRUE((sreq.team_a == kNoTeam && sreq.team_b == kNoTeam) || (sreq.team_a < sim::MAX_PLAYERS && sreq.team_b < sim::MAX_PLAYERS && sreq.team_a != sreq.team_b));
            }
            if (decode(buf, smsg)) {                   // a Start that gets through has teams that its own roster can make, and encodes back to the same bytes
                ++accepted;
                ASSERT_TRUE(encode(smsg) == buf);
                ASSERT_TRUE((smsg.team_a == kNoTeam && smsg.team_b == kNoTeam) || sim::plan_start_teams(smsg.teams(), smsg.roster).why.empty());
            }
        }
        ASSERT_TRUE(accepted > 5000);                  // the mutations of valid messages do get through
    } TEST_END();

    TEST_CASE("N2.3b Protocol 7: The Room Names Its Leader (Every Value; Only The Seat Of A Guest Or Nobody), StartRequest Is The Type, A Fill Level 0 .. 3 For Each Seat And The Two Team Bytes (Seven Bytes Since Protocol 13, Two Before, One Before That) And Nothing Else, The Layout Of Protocol 6 Is Refused") {
        ASSERT_TRUE(kProtocolVersion >= 7);                                // protocol 7 grew the Room message by a byte and added a message type (8 to 11 keep both): a client of protocol 6 cannot play with it
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
        // StartRequest (protocol 13): exactly the type, a fill level 0 .. 3 for each of the four seats and the two team bytes (N2.96 has every rule of it)
        const std::vector<uint8_t> request = encode(StartRequestMsg{});
        ASSERT_TRUE(request == (std::vector<uint8_t>{24, 0, 0, 0, 0, 255, 255}));
        StartRequestMsg sr;
        ASSERT_TRUE(decode(request, sr) && sr.fill == StartRequestMsg{}.fill && sr.team_a == kNoTeam && sr.team_b == kNoTeam);
        for (uint8_t level = 0; level <= kFillLevelLast; ++level) {
            const std::vector<uint8_t> m = {24, level, level, level, level, 255, 255};
            ASSERT_TRUE(decode(m, sr) && sr.fill == StartRequestMsg::all(static_cast<FillLevel>(level)).fill && encode(sr) == m);
        }
        ASSERT_FALSE(decode(std::vector<uint8_t>{24}, sr));                // protocol 7's single byte is no StartRequest any more
        ASSERT_FALSE(decode(std::vector<uint8_t>{24, 0}, sr));             // nor is protocol 11's two bytes
        ASSERT_FALSE(decode(std::vector<uint8_t>{}, sr));
        ASSERT_FALSE(decode(nullptr, 1, sr));
        for (unsigned level = kFillLevelLast + 1u; level < 256u; ++level) ASSERT_FALSE(decode(std::vector<uint8_t>{24, static_cast<uint8_t>(level), 0, 0, 0, 255, 255}, sr));      // a level above 3
        for (size_t extra : {size_t{1}, size_t{2}, size_t{9}, size_t{200}, kMaxMessageBytes - 7}) {    // a payload of any size beyond the two team bytes makes it garbage
            std::vector<uint8_t> payload = request;
            payload.resize(7 + extra, 0x01);
            ASSERT_FALSE(decode(payload, sr));
        }
        for (unsigned type = 0; type < 256; ++type) {                      // no other type byte is a StartRequest
            if (type == 24) continue;
            ASSERT_FALSE(decode(std::vector<uint8_t>{static_cast<uint8_t>(type), 0, 0, 0, 0, 255, 255}, sr));
        }
        // and the neighbours: no other decoder takes it, and it takes no other message
        ASSERT_FALSE(any_decodes(std::vector<uint8_t>{24}) || any_decodes(std::vector<uint8_t>{24, 4}) || any_decodes(std::vector<uint8_t>{}));
        ASSERT_TRUE(any_decodes(request) && peek_type(request) == MsgType::StartRequest);                         // (and the one decoder that takes it is the StartRequest's own)
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

    TEST_CASE("N2.40 Protocol 10: Keys In Hello And Welcome, Three More Rejections, Presence (With The Resume Countdown), Vote, CatchUp, TurnBatch And CaughtUp: Numbers, Layouts Byte By Byte, Every Truncation, Every Range Rule, The Batch Encoders Agree") {
        // ---- the numbers ----
        ASSERT_EQ(kProtocolVersion, 13);                                 // (the layouts of protocol 10 below are still the layouts of protocols 11 to 13: 11 and 13 changed the StartRequest (13 the Start too), 12 no message)
        ASSERT_TRUE(static_cast<int>(MsgType::Presence) == 26 && static_cast<int>(MsgType::Vote) == 27 && static_cast<int>(MsgType::CatchUp) == 28 &&
                    static_cast<int>(MsgType::TurnBatch) == 29 && static_cast<int>(MsgType::CaughtUp) == 30 && static_cast<int>(MsgType::Last) == 30);
        ASSERT_TRUE(static_cast<int>(RejectReason::Dropped) == 7 && static_cast<int>(RejectReason::RejoinFailed) == 8 && static_cast<int>(RejectReason::Superseded) == 9);
        ASSERT_TRUE(kKeyBytes == 16 && kMaxBatchTurns == 4096 && kBatchBytes == 48 * 1024 && kBatchHeaderBytes == 7 && kWelcomeRejoin == 1 && kCapSecondsMore == 0xFFFF);
        for (const MsgType t : {MsgType::Presence, MsgType::Vote, MsgType::CatchUp, MsgType::TurnBatch, MsgType::CaughtUp}) {
            ASSERT_EQ(peek_type(std::vector<uint8_t>{static_cast<uint8_t>(t)}), t);
        }
        ASSERT_EQ(peek_type(std::vector<uint8_t>{31}), MsgType::None);

        // ---- the keys: zero is no key, a key matches only itself, and never "no key" ----
        {
            SeatKey zero{};
            ASSERT_TRUE(key_is_zero(zero));
            ASSERT_FALSE(key_matches(zero, zero));                           // "no key" matches nothing, not even "no key"
            ASSERT_FALSE(key_matches(zero, key_with(1)));
            ASSERT_FALSE(key_matches(key_with(1), zero));
            ASSERT_TRUE(key_matches(key_with(1), key_with(1)));
            ASSERT_FALSE(key_matches(key_with(1), key_with(2)));
            for (size_t i = 0; i < kKeyBytes; ++i) {                         // a key that differs in ANY one byte, in any bit, is another key
                for (unsigned bit = 0; bit < 8; ++bit) {
                    SeatKey other = key_with(1);
                    other[i] = static_cast<uint8_t>(other[i] ^ (1u << bit));
                    ASSERT_FALSE(key_matches(key_with(1), other));
                    ASSERT_FALSE(key_is_zero(other));
                }
                SeatKey one_byte{};
                one_byte[i] = 1;
                ASSERT_FALSE(key_is_zero(one_byte));                         // a key with one byte set, wherever it is, is a key
            }
        }

        // ---- Hello: the layout (v6 fields, then the key and the turns), the rules ----
        {
            HelloMsg h;
            h.name = "Ann";
            h.listen_port = 0x1234;
            h.want_seat = 2;
            h.room = "ROOM-7";
            h.token = "tok";
            h.key = key_with(5);
            h.have_turns = 0x01020304;
            const std::vector<uint8_t> bytes = encode(h);
            ASSERT_EQ(bytes.size(), size_t{1 + 2 + 4 + 2 + 1 + 7 + 4 + 16 + 4});     // type, version, "Ann", port, seat, "ROOM-7", "tok", key, turns
            ASSERT_TRUE(bytes[0] == 1 && bytes[1] == (kProtocolVersion & 0xFF) && bytes[2] == (kProtocolVersion >> 8));      // (the version is whatever this protocol is: 10 here until protocol 11)
            const SeatKey key5 = key_with(5);
            ASSERT_TRUE(std::equal(key5.begin(), key5.end(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() - 20)));       // the key: 16 bytes
            ASSERT_TRUE(bytes[bytes.size() - 4] == 4 && bytes[bytes.size() - 3] == 3 && bytes[bytes.size() - 2] == 2 && bytes[bytes.size() - 1] == 1);   // the turns, little endian
            HelloMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.version == kProtocolVersion && back.name == "Ann" && back.listen_port == 0x1234 && back.want_seat == 2 && back.room == "ROOM-7" && back.token == "tok" &&
                        back.key == key_with(5) && back.have_turns == 0x01020304);
            ASSERT_TRUE(encode(back) == bytes);
            // a new player: no key, no turns; the largest count with a key; no key with turns is no Hello (a count belongs to a key)
            HelloMsg fresh;
            ASSERT_TRUE(decode(encode(fresh), back) && key_is_zero(back.key) && back.have_turns == 0);
            h.have_turns = 0xFFFFFFFFu;
            ASSERT_TRUE(decode(encode(h), back) && back.have_turns == 0xFFFFFFFFu);
            h.have_turns = 0;
            ASSERT_TRUE(decode(encode(h), back) && back.key == key_with(5) && back.have_turns == 0);        // a key and nothing yet: starts from nothing
            HelloMsg orphan;
            orphan.have_turns = 1;
            ASSERT_FALSE(decode(encode(orphan), back));
            // a key that is cut short: every length between the end of the token and the end of the message
            const std::vector<uint8_t> core = encode(h);
            for (size_t keep = core.size() - 20; keep < core.size(); ++keep) {
                const std::vector<uint8_t> shorter(core.begin(), core.begin() + static_cast<std::ptrdiff_t>(keep));
                ASSERT_FALSE(decode(shorter, back));
                ASSERT_FALSE(any_decodes(shorter));
            }
            std::vector<uint8_t> longer = core;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
            // the prefix of every version's Hello reads the same: a host answers "version mismatch" without reading what follows
            ASSERT_TRUE(decode_hello_prefix(core.data(), core.size(), back) && back.version == kProtocolVersion && back.name == "Ann");
            for (const uint16_t version : {uint16_t{6}, uint16_t{7}, uint16_t{8}, uint16_t{9}}) {         // the old layout (protocols 6 to 9 had it): no key, no turns
                const std::vector<uint8_t> old = old_layout_hello(version, "Old", "ROOM-7", "tok");
                ASSERT_FALSE(decode(old, back));                                             // it is not a Hello of this protocol ...
                ASSERT_TRUE(decode_hello_prefix(old.data(), old.size(), back) && back.version == version && back.name == "Old");    // ... but its version and name are read
                ASSERT_FALSE(any_decodes(old));
            }
        }

        // ---- Welcome: the layout, the flags, the rules ----
        {
            WelcomeMsg w;
            w.player = 2;
            w.players = 4;
            w.key = key_with(6);
            w.flags = kWelcomeRejoin;
            std::vector<uint8_t> bytes = encode(w);
            ASSERT_EQ(bytes.size(), size_t{20});                                             // type, player, players, key, flags
            ASSERT_TRUE(bytes[0] == 2 && bytes[1] == 2 && bytes[2] == 4 && bytes[19] == 1);
            const SeatKey key6 = key_with(6);
            ASSERT_TRUE(std::equal(key6.begin(), key6.end(), bytes.begin() + 3));
            WelcomeMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.player == 2 && back.players == 4 && back.key == key_with(6) && back.flags == kWelcomeRejoin);
            ASSERT_TRUE(encode(back) == bytes);
            WelcomeMsg lan;                                                                  // a LAN or direct host: no key, no flags
            lan.player = 1;
            lan.players = 4;
            ASSERT_TRUE(decode(encode(lan), back) && key_is_zero(back.key) && back.flags == 0);
            w.flags = 0;                                                                     // a seat in the room: a key and no rejoin
            ASSERT_TRUE(decode(encode(w), back) && back.key == key_with(6) && back.flags == 0);
            for (unsigned flags = 2; flags < 256; ++flags) {                                 // the flags are 0 or 1
                bytes = encode(w);
                bytes[19] = static_cast<uint8_t>(flags);
                ASSERT_FALSE(decode(bytes, back));
            }
            lan.flags = kWelcomeRejoin;                                                      // a rejoin is of a seat that has a key
            ASSERT_FALSE(decode(encode(lan), back));
            // the old layout (type, player, players) and a cut key
            ASSERT_FALSE(decode(std::vector<uint8_t>{2, 1, 4}, back));
            bytes = encode(w);
            for (size_t keep = 3; keep < bytes.size(); ++keep) ASSERT_FALSE(any_decodes(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep))));
            bytes.push_back(1);
            ASSERT_FALSE(decode(bytes, back));
        }

        // ---- Reject: the three new reasons ----
        for (unsigned reason = 0; reason < 256; ++reason) {
            RejectMsg rj;
            const std::vector<uint8_t> bytes = {static_cast<uint8_t>(MsgType::Reject), static_cast<uint8_t>(reason)};
            ASSERT_EQ(decode(bytes, rj), reason >= 1 && reason <= 9);
            if (reason >= 1 && reason <= 9) {
                ASSERT_TRUE(static_cast<unsigned>(rj.reason) == reason && encode(rj) == bytes);
            }
        }

        // ---- Presence ----
        {
            PresenceMsg base;
            base.missing = {{1, PresenceMsg::State::Absent, 42, 0}, {3, PresenceMsg::State::CatchingUp, 31, 63}};
            base.vote_seat = 1;
            base.votes_continue = 1;
            base.voters = 2;
            base.your_vote = 2;
            base.cap_s = 1700;
            const std::vector<uint8_t> bytes = encode(base);
            ASSERT_EQ(bytes.size(), size_t{1 + 1 + 2 * 5 + 4 + 2 + 1});
            ASSERT_TRUE(bytes[0] == 26 && bytes[1] == 2);
            ASSERT_TRUE(bytes[2] == 1 && bytes[3] == 1 && bytes[4] == 42 && bytes[5] == 0 && bytes[6] == 0);                    // seat 1, absent, waited 42 s (u16), no progress
            ASSERT_TRUE(bytes[7] == 3 && bytes[8] == 2 && bytes[9] == 31 && bytes[10] == 0 && bytes[11] == 63);                // seat 3, catching up, 31 s, 63 %
            ASSERT_TRUE(bytes[12] == 1 && bytes[13] == 1 && bytes[14] == 2 && bytes[15] == 2 && bytes[16] == (1700 & 0xFF) && bytes[17] == (1700 >> 8));
            ASSERT_TRUE(bytes[18] == 0);                                                                                       // no countdown
            PresenceMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.missing.size() == 2 && back.missing[0].seat == 1 && back.missing[0].waited_s == 42 && back.missing[1].state == PresenceMsg::State::CatchingUp &&
                        back.missing[1].progress == 63 && back.vote_seat == 1 && back.votes_continue == 1 && back.voters == 2 && back.your_vote == 2 && back.cap_s == 1700);
            ASSERT_TRUE(encode(back) == bytes);
            // the u16 fields use both bytes: a wait of 0xFFFF s (it saturates there) and one of 0x1234, a cap of 0xABCD
            PresenceMsg wide;
            wide.missing = {{2, PresenceMsg::State::Absent, 0xFFFF, 0}, {0, PresenceMsg::State::CatchingUp, 0x1234, 100}};
            wide.vote_seat = 2;
            wide.votes_continue = 3;
            wide.voters = 3;
            wide.your_vote = 1;
            wide.cap_s = 0xABCD;
            ASSERT_TRUE(decode(encode(wide), back) && back.missing[0].waited_s == 0xFFFF && back.missing[1].waited_s == 0x1234 && back.cap_s == 0xABCD && encode(back) == encode(wide));
            // the match runs: nobody missing, no vote, any number of voters; the cap is told all the same
            PresenceMsg runs;
            runs.voters = 3;
            runs.cap_s = 0;
            ASSERT_TRUE(decode(encode(runs), back) && back.missing.empty() && back.vote_seat == 255 && back.voters == 3 && back.cap_s == 0);
            runs.cap_s = 0xFFFF;
            ASSERT_TRUE(decode(encode(runs), back) && back.cap_s == 0xFFFF);
            ASSERT_EQ(encode(PresenceMsg{}).size(), size_t{1 + 1 + 4 + 2 + 1});
            // every truncation and a byte too many
            for (size_t keep = 0; keep < bytes.size(); ++keep) {
                const std::vector<uint8_t> shorter(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep));
                ASSERT_FALSE(decode(shorter, back));
                ASSERT_FALSE(any_decodes(shorter));
            }
            std::vector<uint8_t> longer = bytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
            // every range rule, one at a time, on the base message
            auto with = [&](size_t at, uint8_t value) {
                std::vector<uint8_t> b = bytes;
                b[at] = value;
                return decode(b, back);
            };
            ASSERT_FALSE(with(1, 5));                                    // more than four missing
            ASSERT_FALSE(with(1, 4));                                    // four promised, two there
            ASSERT_FALSE(with(1, 3));                                    // three promised: the vote's bytes are read as the third entry, and then a byte is short
            ASSERT_FALSE(with(2, 4));                                    // a seat that does not exist (the entry's seat, and the vote is about it)
            ASSERT_FALSE(with(7, 1));                                    // a seat twice
            ASSERT_FALSE(with(3, 0));                                    // a state that is neither absent nor catching up
            ASSERT_FALSE(with(3, 3));
            ASSERT_FALSE(with(8, 0));
            ASSERT_FALSE(with(8, 3));
            ASSERT_FALSE(with(6, 1));                                    // an absent seat has no progress
            ASSERT_TRUE(with(11, 100) && back.missing[1].progress == 100);
            ASSERT_FALSE(with(11, 101));                                 // a progress above 100
            ASSERT_FALSE(with(11, 255));
            ASSERT_TRUE(with(9, 42) && back.missing[1].waited_s == 42);  // equal waits are in order (a tie)
            ASSERT_FALSE(with(9, 43));                                   // the second seat waited longer than the first: not longest away first
            ASSERT_TRUE(with(4, 255) && back.missing[0].waited_s == 255);// (a longer first wait is in order: the second is 31)
            ASSERT_TRUE(with(12, 3) && back.vote_seat == 3);             // the vote may be about a seat that is catching up, or one that is not missing at all (a seat that FLAPS is put to the vote in any state) ...
            ASSERT_TRUE(with(12, 0) && with(12, 2) && back.vote_seat == 2);
            ASSERT_FALSE(with(12, 4));                                   // ... but about a seat that exists
            ASSERT_FALSE(with(12, 254));
            ASSERT_FALSE(with(13, 3));                                   // more votes (3) than voters (2)
            ASSERT_TRUE(with(13, 2));
            ASSERT_FALSE(with(14, 5));                                   // five voters
            ASSERT_FALSE(with(14, 255));
            ASSERT_TRUE(with(14, 1) && back.voters == 1);                // one vote of one voter is all of them
            ASSERT_FALSE(with(14, 0));                                   // one vote and no voter
            ASSERT_FALSE(with(15, 3));                                   // a choice of the receiver that is not 0, 1 or 2
            ASSERT_TRUE(with(15, 0) && with(15, 1) && with(15, 2));
            ASSERT_TRUE(with(16, 0) && back.cap_s == (1700 & 0xFF00));   // the cap, a u16 that nothing constrains
            ASSERT_TRUE(with(17, 0) && back.cap_s == (1700 & 0x00FF));
            ASSERT_TRUE(with(17, 0xFF) && back.cap_s == (0xFF00 | (1700 & 0xFF)));
            ASSERT_TRUE(with(18, 0) && back.resume_s == 0);              // the countdown of the resume: none while a seat is missing (the match is paused, nothing counts down)
            ASSERT_FALSE(with(18, 1));
            ASSERT_FALSE(with(18, 60));
            ASSERT_FALSE(with(18, 255));
            // no vote: nobody has voted, and the receiver has no choice
            PresenceMsg no_vote = base;
            no_vote.vote_seat = 255;
            no_vote.votes_continue = 0;
            no_vote.your_vote = 0;
            ASSERT_TRUE(decode(encode(no_vote), back) && back.vote_seat == 255);
            no_vote.votes_continue = 1;
            ASSERT_FALSE(decode(encode(no_vote), back));
            no_vote.votes_continue = 0;
            no_vote.your_vote = 1;
            ASSERT_FALSE(decode(encode(no_vote), back));
            // the vote may be about any seat of the match (a seat that flaps is the subject whatever its state), listed or not; only a seat that does not exist is refused
            PresenceMsg catching_first;
            catching_first.missing = {{3, PresenceMsg::State::CatchingUp, 90, 10}, {1, PresenceMsg::State::Absent, 60, 0}, {0, PresenceMsg::State::Absent, 40, 0}};
            catching_first.voters = 1;
            for (const uint8_t seat : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}}) {
                catching_first.vote_seat = seat;
                ASSERT_TRUE(decode(encode(catching_first), back) && back.vote_seat == seat);
            }
            catching_first.vote_seat = 4;
            ASSERT_FALSE(decode(encode(catching_first), back));
            // the resume countdown: 1 .. 60 seconds, only while nobody is missing; the vote may go on during it (a seat that flaps and is back)
            PresenceMsg count;
            count.voters = 3;
            count.cap_s = 1234;
            for (unsigned secs = 0; secs <= 60; ++secs) {
                count.resume_s = static_cast<uint8_t>(secs);
                const std::vector<uint8_t> b = encode(count);
                ASSERT_TRUE(b.size() == 1 + 1 + 4 + 2 + 1 && b.back() == secs);
                ASSERT_TRUE(decode(b, back) && back.resume_s == secs && encode(back) == b);
            }
            for (unsigned secs = 61; secs < 256; ++secs) {
                count.resume_s = static_cast<uint8_t>(secs);
                ASSERT_FALSE(decode(encode(count), back));                          // more than a minute is not a countdown that a server makes
            }
            count.resume_s = 10;
            count.vote_seat = 2;
            count.votes_continue = 1;
            count.voters = 2;
            ASSERT_TRUE(decode(encode(count), back) && back.resume_s == 10 && back.vote_seat == 2);
            count.missing = {{1, PresenceMsg::State::Absent, 5, 0}};                    // somebody is missing again: no countdown runs
            ASSERT_FALSE(decode(encode(count), back));
            count.missing = {{1, PresenceMsg::State::CatchingUp, 5, 50}};
            ASSERT_FALSE(decode(encode(count), back));
            count.resume_s = 0;
            ASSERT_TRUE(decode(encode(count), back) && back.resume_s == 0);
            // four missing (everybody), all catching up
            PresenceMsg all_four;
            for (uint8_t s = 0; s < 4; ++s) all_four.missing.push_back({s, PresenceMsg::State::CatchingUp, static_cast<uint16_t>(100 - s), static_cast<uint8_t>(s * 10)});
            ASSERT_TRUE(decode(encode(all_four), back) && back.missing.size() == 4);
            all_four.vote_seat = 0;                                      // (a seat that flaps may be the subject of a vote in any state: the decoder takes it)
            ASSERT_TRUE(decode(encode(all_four), back) && back.vote_seat == 0);
            // the encoder never writes more than four entries
            PresenceMsg crowd = all_four;
            crowd.vote_seat = 255;
            crowd.missing.push_back({0, PresenceMsg::State::Absent, 1, 0});
            ASSERT_EQ(encode(crowd)[1], 4);
        }

        // ---- Vote ----
        {
            for (uint8_t seat = 0; seat < 4; ++seat) {
                for (const bool cont : {false, true}) {
                    VoteMsg v;
                    v.seat = seat;
                    v.continue_without = cont;
                    const std::vector<uint8_t> bytes = encode(v);
                    ASSERT_TRUE(bytes.size() == 3 && bytes[0] == 27 && bytes[1] == seat && bytes[2] == (cont ? 1 : 0));
                    VoteMsg back;
                    ASSERT_TRUE(decode(bytes, back) && back.seat == seat && back.continue_without == cont);
                }
            }
            VoteMsg back;
            ASSERT_FALSE(decode(std::vector<uint8_t>{27, 4, 0}, back));       // a seat that does not exist
            ASSERT_FALSE(decode(std::vector<uint8_t>{27, 255, 0}, back));
            ASSERT_FALSE(decode(std::vector<uint8_t>{27, 0, 2}, back));       // a choice that is neither
            ASSERT_FALSE(decode(std::vector<uint8_t>{27, 0}, back));          // short, long
            ASSERT_FALSE(decode(std::vector<uint8_t>{27}, back));
            ASSERT_FALSE(decode(std::vector<uint8_t>{27, 0, 1, 0}, back));
            ASSERT_FALSE(decode(std::vector<uint8_t>{26, 0, 1}, back));       // another type
            ASSERT_FALSE(any_decodes(std::vector<uint8_t>{27, 0}) || any_decodes(std::vector<uint8_t>{27, 0, 1, 0}) || any_decodes(std::vector<uint8_t>{27, 4, 0}));
        }

        // ---- CatchUp ----
        {
            CatchUpMsg c;
            c.first_turn = 0x01020304;
            c.total_turns = 0x05060708;
            const std::vector<uint8_t> bytes = encode(c);
            ASSERT_TRUE(bytes.size() == 9 && bytes[0] == 28 && bytes[1] == 4 && bytes[4] == 1 && bytes[5] == 8 && bytes[8] == 5);
            CatchUpMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.first_turn == 0x01020304 && back.total_turns == 0x05060708);
            c.first_turn = c.total_turns;                                      // nothing to stream: the client has everything
            ASSERT_TRUE(decode(encode(c), back) && back.first_turn == back.total_turns);
            c.first_turn = 0;
            c.total_turns = 0;
            ASSERT_TRUE(decode(encode(c), back));
            c.first_turn = 0xFFFFFFFFu;
            c.total_turns = 0xFFFFFFFFu;
            ASSERT_TRUE(decode(encode(c), back));
            c.first_turn = 6;                                                  // the stream cannot start after its end
            c.total_turns = 5;
            ASSERT_FALSE(decode(encode(c), back));
            c.first_turn = 0xFFFFFFFFu;
            c.total_turns = 0;
            ASSERT_FALSE(decode(encode(c), back));
            for (size_t keep = 0; keep < bytes.size(); ++keep) ASSERT_FALSE(any_decodes(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep))));
            std::vector<uint8_t> longer = bytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
        }

        // ---- CaughtUp ----
        {
            CaughtUpMsg c;
            c.turns = 36000;
            c.hash = {0x1111111111111111ull, 2, 3, 4, 5, 6, 7, 0x8888888888888888ull};
            const std::vector<uint8_t> bytes = encode(c);
            ASSERT_TRUE(bytes.size() == 1 + 4 + 64 && bytes[0] == 30 && bytes[1] == (36000 & 0xFF) && bytes[2] == ((36000 >> 8) & 0xFF) && bytes[5] == 0x11 && bytes[bytes.size() - 1] == 0x88);
            CaughtUpMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.turns == 36000 && back.hash == c.hash);
            ASSERT_TRUE(encode(back) == bytes);
            for (size_t keep = 0; keep < bytes.size(); ++keep) {
                const std::vector<uint8_t> shorter(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep));
                ASSERT_FALSE(decode(shorter, back));
                ASSERT_FALSE(any_decodes(shorter));
            }
            std::vector<uint8_t> longer = bytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
        }

        // ---- TurnBatch ----
        {
            auto pack = [](const std::vector<TurnMsg>& turns) {                // the packing by hand: u16 count, the commands (not TurnLog's: that is N2.41's to prove the same)
                std::vector<uint8_t> out;
                for (const TurnMsg& t : turns) {
                    out.push_back(static_cast<uint8_t>(t.commands.size() & 0xFF));
                    out.push_back(static_cast<uint8_t>(t.commands.size() >> 8));
                    for (const Command& c : t.commands) sim::encode(c, out);
                }
                return out;
            };
            TurnBatchMsg b;
            b.first_turn = 0x00010203;
            b.turns.resize(4);
            b.turns[0].commands = {cmd(CommandType::GroupMove, 0, 255, 1, 2, {1}), cmd(CommandType::Hatch, 1), cmd(CommandType::AllianceInvite, 2, 3)};
            b.turns[1].commands = {};
            std::vector<uint32_t> thirty_two;
            for (uint32_t i = 0; i < 32; ++i) thirty_two.push_back(1000 + i);
            b.turns[2].commands = {cmd(CommandType::GroupAttack, 3, 255, 59, 59, thirty_two)};
            b.turns[3].commands = {cmd(CommandType::Stop, 1, 255, 0, 0, {7, 8}), cmd(CommandType::Drop, 2)};
            for (uint32_t i = 0; i < 4; ++i) b.turns[i].turn = b.first_turn + i;
            const std::vector<uint8_t> bytes = encode(b);
            const std::vector<uint8_t> packed = pack(b.turns);
            ASSERT_EQ(bytes.size(), kBatchHeaderBytes + packed.size());
            ASSERT_TRUE(bytes[0] == 29 && bytes[1] == 3 && bytes[2] == 2 && bytes[3] == 1 && bytes[4] == 0 && bytes[5] == 4 && bytes[6] == 0);
            ASSERT_TRUE(std::equal(packed.begin(), packed.end(), bytes.begin() + static_cast<std::ptrdiff_t>(kBatchHeaderBytes)));
            TurnBatchMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.first_turn == b.first_turn && back.turns.size() == 4);
            for (uint32_t i = 0; i < 4; ++i) ASSERT_TRUE(back.turns[i].turn == b.first_turn + i && back.turns[i].commands == b.turns[i].commands);
            ASSERT_TRUE(encode(back) == bytes);
            // the two encoders agree: from the turns, and from the turns packed already
            ASSERT_TRUE(encode_turn_batch_packed(b.first_turn, 4, packed.data(), packed.size()) == bytes);
            ASSERT_TRUE(encode_turn_batch_packed(0, 0, packed.data(), packed.size()).empty());            // no turn: no message
            ASSERT_TRUE(encode_turn_batch_packed(0, static_cast<uint32_t>(kMaxBatchTurns) + 1, packed.data(), packed.size()).empty());
            ASSERT_TRUE(encode_turn_batch_packed(5, 1, nullptr, 0).size() == kBatchHeaderBytes);          // (a count that its bytes do not match is the caller's: this is no message that decodes)
            ASSERT_FALSE(decode(encode_turn_batch_packed(5, 1, nullptr, 0), back));
            Lcg rng(40);
            for (int round = 0; round < 200; ++round) {                        // random batches: both ways round give the same bytes, and decoding gives the turns back
                TurnBatchMsg r;
                r.first_turn = rng.below(1000000);
                const uint32_t count = 1 + rng.below(40);
                r.turns.resize(count);
                for (uint32_t i = 0; i < count; ++i) {
                    r.turns[i].turn = r.first_turn + i;
                    const uint32_t n = rng.below(4) == 0 ? rng.below(6) : 0;
                    for (uint32_t k = 0; k < n; ++k) {
                        const uint8_t issuer = static_cast<uint8_t>(rng.below(4));
                        if (rng.below(3) == 0) {
                            r.turns[i].commands.push_back(cmd(CommandType::Hatch, issuer));
                        } else {
                            std::vector<uint32_t> ants;
                            for (uint32_t a = 1 + rng.below(32); a > 0; --a) ants.push_back(rng.below(100000));
                            r.turns[i].commands.push_back(cmd(CommandType::GroupMove, issuer, 255, static_cast<int16_t>(rng.below(60)), static_cast<int16_t>(rng.below(60)), ants));
                        }
                    }
                }
                const std::vector<uint8_t> a = encode(r);
                const std::vector<uint8_t> p = pack(r.turns);
                ASSERT_TRUE(encode_turn_batch_packed(r.first_turn, count, p.data(), p.size()) == a);
                TurnBatchMsg again;
                ASSERT_TRUE(decode(a, again) && again.first_turn == r.first_turn && again.turns.size() == count);
                for (uint32_t i = 0; i < count; ++i) ASSERT_TRUE(again.turns[i].turn == r.turns[i].turn && again.turns[i].commands == r.turns[i].commands);
            }
            // every truncation (a turn that ends inside the buffer, a count that promises more, a half command) and a byte too many
            for (size_t keep = 0; keep < bytes.size(); ++keep) {
                const std::vector<uint8_t> shorter(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep));
                ASSERT_FALSE(decode(shorter, back));
                ASSERT_FALSE(any_decodes(shorter));
            }
            std::vector<uint8_t> longer = bytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
            longer = bytes;
            longer.insert(longer.end(), {0, 0});                               // the bytes of another (empty) turn that the count does not promise
            ASSERT_FALSE(decode(longer, back));
            // the count: 0 and 4097 are refused; 4096 (empty turns: 2 bytes each) is the largest
            {
                std::vector<uint8_t> empty_turns = {29, 0, 0, 0, 0, 0, 0};
                ASSERT_FALSE(decode(empty_turns, back));                       // count 0
                TurnBatchMsg full;
                full.first_turn = 7;
                full.turns.resize(kMaxBatchTurns);
                const std::vector<uint8_t> big = encode(full);
                ASSERT_EQ(big.size(), kBatchHeaderBytes + 2 * kMaxBatchTurns);
                ASSERT_TRUE(decode(big, back) && back.turns.size() == kMaxBatchTurns && back.turns.back().turn == 7 + kMaxBatchTurns - 1);
                std::vector<uint8_t> too_many = big;                           // count 4097 and the 2 bytes of the extra turn that it promises
                too_many[5] = static_cast<uint8_t>((kMaxBatchTurns + 1) & 0xFF);
                too_many[6] = static_cast<uint8_t>((kMaxBatchTurns + 1) >> 8);
                too_many.insert(too_many.end(), {0, 0});
                ASSERT_FALSE(decode(too_many, back));
                full.turns.resize(kMaxBatchTurns + 5);                         // the encoder cuts what the decoder would refuse
                ASSERT_EQ(encode(full).size(), big.size());
            }
            // the commands of a turn: at most kMaxTurnCommands (512), written as a count that is checked before any command is read
            {
                TurnBatchMsg five_twelve;
                five_twelve.turns.resize(1);
                for (size_t i = 0; i < kMaxTurnCommands; ++i) five_twelve.turns[0].commands.push_back(cmd(CommandType::Hatch, static_cast<uint8_t>(i % 4)));
                const std::vector<uint8_t> ok = encode(five_twelve);
                ASSERT_TRUE(decode(ok, back) && back.turns[0].commands.size() == kMaxTurnCommands);
                std::vector<uint8_t> over = ok;                                // 513: the count and the command that it promises
                over[kBatchHeaderBytes] = static_cast<uint8_t>((kMaxTurnCommands + 1) & 0xFF);
                over[kBatchHeaderBytes + 1] = static_cast<uint8_t>((kMaxTurnCommands + 1) >> 8);
                sim::encode(cmd(CommandType::Hatch, 0), over);
                ASSERT_FALSE(decode(over, back));
                five_twelve.turns[0].commands.push_back(cmd(CommandType::Hatch, 0));    // (the encoder cuts at 512)
                ASSERT_EQ(encode(five_twelve).size(), ok.size());
                std::vector<uint8_t> lie = ok;                                 // a count of 65535 and a few bytes
                lie[kBatchHeaderBytes] = 0xFF;
                lie[kBatchHeaderBytes + 1] = 0xFF;
                ASSERT_FALSE(decode(lie, back));
            }
            // a command that is no command (unknown type, 33 ants, an ant list on a Hatch) inside a batch
            {
                std::vector<uint8_t> bad = {29, 1, 0, 0, 0, 1, 0, 1, 0};       // one turn, one command
                const std::vector<uint8_t> good_command = {static_cast<uint8_t>(CommandType::Hatch), 0, 255, 0, 0, 0, 0, 0};
                std::vector<uint8_t> v = bad;
                v.insert(v.end(), good_command.begin(), good_command.end());
                ASSERT_TRUE(decode(v, back));
                v[kBatchHeaderBytes + 2] = 0;                                  // type None
                ASSERT_FALSE(decode(v, back));
                v[kBatchHeaderBytes + 2] = 99;                                 // unknown type
                ASSERT_FALSE(decode(v, back));
                v[kBatchHeaderBytes + 2] = static_cast<uint8_t>(CommandType::Hatch);
                v[kBatchHeaderBytes + 2 + 7] = 1;                              // a Hatch with an ant list
                v.insert(v.end(), {1, 0, 0, 0});
                ASSERT_FALSE(decode(v, back));
                std::vector<uint8_t> w = bad;
                w.insert(w.end(), {static_cast<uint8_t>(CommandType::GroupMove), 0, 255, 0, 0, 0, 0, 33});     // 33 ants
                w.insert(w.end(), 33 * 4, 0);
                ASSERT_FALSE(decode(w, back));
            }
            // the turn numbers must not wrap: first_turn + count - 1 is at most 0xFFFFFFFF
            {
                TurnBatchMsg edge;
                edge.first_turn = 0xFFFFFFFFu - 2;
                edge.turns.resize(3);
                ASSERT_TRUE(decode(encode(edge), back) && back.turns[2].turn == 0xFFFFFFFFu);
                edge.first_turn = 0xFFFFFFFFu - 1;                             // three turns from here would end past the last number
                ASSERT_FALSE(decode(encode(edge), back));
                edge.first_turn = 0xFFFFFFFFu;
                edge.turns.resize(1);
                ASSERT_TRUE(decode(encode(edge), back) && back.turns[0].turn == 0xFFFFFFFFu);
                edge.turns.resize(2);
                ASSERT_FALSE(decode(encode(edge), back));
            }
            // the size of the message: the largest that fits (65,535 bytes: a batch is 7 + an even number) is taken, one more is not
            {
                TurnBatchMsg fill;
                size_t size = kBatchHeaderBytes;
                uint32_t number = 0;
                std::vector<uint32_t> thirty_two_ants;
                for (uint32_t i = 0; i < 32; ++i) thirty_two_ants.push_back(i);
                const size_t big_turn = 2 + sim::kCommandHeaderBytes + 4 * 32;
                while (kMaxMessageBytes - 1 - size >= big_turn + 10) {
                    TurnMsg t;
                    t.turn = number++;
                    t.commands = {cmd(CommandType::GroupMove, 0, 255, 1, 1, thirty_two_ants)};
                    fill.turns.push_back(std::move(t));
                    size += big_turn;
                }
                // the rest, an even number of bytes, in turns of 2 + 8 h bytes: j turns with the last one holding the Hatch commands
                const size_t rest = kMaxMessageBytes - 1 - size;
                ASSERT_EQ(rest % 2, size_t{0});
                size_t j = 0;
                for (size_t k = 1; k <= 4 && j == 0; ++k) {
                    if (rest >= 2 * k && (rest - 2 * k) % 8 == 0) j = k;
                }
                ASSERT_TRUE(j != 0);
                for (size_t k = 0; k < j; ++k) {
                    TurnMsg t;
                    t.turn = number++;
                    if (k + 1 == j) {
                        for (size_t h = 0; h < (rest - 2 * j) / 8; ++h) t.commands.push_back(cmd(CommandType::Hatch, 0));
                    }
                    fill.turns.push_back(std::move(t));
                }
                const std::vector<uint8_t> largest = encode(fill);
                ASSERT_EQ(largest.size(), kMaxMessageBytes - 1);
                ASSERT_TRUE(decode(largest, back) && back.turns.size() == fill.turns.size());
                fill.turns.push_back(TurnMsg{});                               // two more bytes: 65,537
                const std::vector<uint8_t> too_big = encode(fill);
                ASSERT_EQ(too_big.size(), kMaxMessageBytes + 1);
                ASSERT_FALSE(decode(too_big, back));
                ASSERT_FALSE(any_decodes(too_big));
            }
        }
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
// Coming back (protocol 10): the turn log, the attendance rules, the runner's fast-forward (docs/NETWORK_PORT.md "Reconnect")
// ---------------------------------------------------------------------------------------------------------------------------------

// A match as a server logs it: `count` turns of 50 ms with about 4 commands a second over the four players (what people give: orders of 1 - 24 ants, Stops, a Hatch, now and then an
// invitation), in canonical order with the issuers stamped. `per_hundred_turns` is the share of turns that carry a command.
std::vector<TurnMsg> real_looking_turns(uint32_t count, uint32_t seed, uint32_t per_hundred_turns = 20) {
    Lcg rng(seed);
    std::vector<TurnMsg> turns(count);
    for (uint32_t t = 0; t < count; ++t) {
        turns[t].turn = t;
        uint32_t n = rng.below(100) < per_hundred_turns ? 1u : 0u;
        if (n == 1 && rng.below(10) == 0) n = 2 + rng.below(2);
        for (uint32_t k = 0; k < n; ++k) {
            const uint8_t issuer = static_cast<uint8_t>(rng.below(4));
            const uint32_t kind = rng.below(100);
            std::vector<uint32_t> ants;
            if (kind < 90) {
                for (uint32_t a = 1 + rng.below(24); a > 0; --a) ants.push_back(1000u * issuer + rng.below(60));
            }
            const int16_t x = static_cast<int16_t>(rng.below(60));
            const int16_t y = static_cast<int16_t>(rng.below(60));
            if (kind < 55) turns[t].commands.push_back(cmd(CommandType::GroupMove, issuer, 255, x, y, ants));
            else if (kind < 70) turns[t].commands.push_back(cmd(CommandType::GroupAttack, issuer, 255, x, y, ants));
            else if (kind < 80) turns[t].commands.push_back(cmd(CommandType::GroupSpecial, issuer, 255, x, y, ants));
            else if (kind < 90) turns[t].commands.push_back(cmd(CommandType::Stop, issuer, 255, 0, 0, ants));
            else if (kind < 96) turns[t].commands.push_back(cmd(CommandType::Hatch, issuer));
            else turns[t].commands.push_back(cmd(CommandType::AllianceInvite, issuer, static_cast<uint8_t>((issuer + 1) % 4)));
        }
        sim::canonical_order(turns[t].commands);
    }
    return turns;
}

// The packing of a turn by hand (u16 count, the commands in their wire form): what the log must keep, written down once more without the log
std::vector<uint8_t> pack_turn(const TurnMsg& t) {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(t.commands.size() & 0xFF));
    out.push_back(static_cast<uint8_t>(t.commands.size() >> 8));
    for (const Command& c : t.commands) sim::encode(c, out);
    return out;
}

// A turn that holds `commands` commands of `ants` ants each (a hostile client's turn is 64 of 32 for each of four players)
TurnMsg heavy_turn(uint32_t number, size_t commands, size_t ants) {
    TurnMsg t;
    t.turn = number;
    std::vector<uint32_t> list;
    for (size_t i = 0; i < ants; ++i) list.push_back(static_cast<uint32_t>(i));
    for (size_t i = 0; i < commands; ++i) t.commands.push_back(cmd(CommandType::GroupMove, static_cast<uint8_t>(i % 4), 255, 1, 1, list));
    return t;
}

// True when the turns of the batch are the turns `from` .. of `all`, number and commands
bool batch_is(const std::vector<TurnMsg>& all, size_t from, const TurnBatchMsg& b) {
    if (b.first_turn != from || b.turns.empty() || from + b.turns.size() > all.size()) return false;
    for (size_t i = 0; i < b.turns.size(); ++i) {
        if (b.turns[i].turn != all[from + i].turn || b.turns[i].commands != all[from + i].commands) return false;
    }
    return true;
}

void run_reconnect_core_tests() {
    TEST_CASE("N2.41 Turn Log: 36,000 Real-Looking Turns (30 Minutes Of 50 ms) Are Kept Packed And Read Back In Batches Of Every Size, Identical To What Was Sealed; The Limit Stops It At The Right Turn And For Good") {
        const std::vector<TurnMsg> all = real_looking_turns(36000, 7);
        std::vector<size_t> sizes(all.size());                            // what each turn packs to, and the sum (the blob), worked out without the log
        size_t blob = 0;
        std::vector<uint8_t> everything;
        for (size_t t = 0; t < all.size(); ++t) {
            const std::vector<uint8_t> p = pack_turn(all[t]);
            sizes[t] = p.size();
            blob += p.size();
            everything.insert(everything.end(), p.begin(), p.end());
        }
        TurnLog log;
        ASSERT_TRUE(log.max_bytes() == TurnLog::kDefaultMaxBytes && TurnLog::kDefaultMaxBytes == 16u * 1024u * 1024u && log.usable() && log.turns() == 0 && log.bytes() == 0);
        for (const TurnMsg& t : all) ASSERT_TRUE(log.append(t));
        ASSERT_TRUE(log.usable());
        ASSERT_EQ(log.turns(), 36000u);
        ASSERT_EQ(log.bytes(), blob + 36000u * 4u);                       // the packed turns and a 4-byte offset for each
        ASSERT_TRUE(log.bytes() > 36000u * 6u && log.bytes() < 700u * 1024u);   // 30 minutes of busy play (4 - 5 orders a second, up to 24 ants): 651,372 bytes, 144,000 of them the index; nobody giving an order at all is 216,000. Far under the limit
        ASSERT_EQ(log.bytes_between(0, log.turns()), blob);
        // everything in one read is the packing by hand, byte for byte
        {
            std::vector<uint8_t> out;
            ASSERT_EQ(log.read(0, 36000, SIZE_MAX, out), 36000u);
            ASSERT_TRUE(out == everything);
        }
        // the stream as the host sends it (4096 turns, 48 KB): every batch decodes to the turns that were sealed, every message fits
        {
            uint32_t next = 0;
            size_t batches = 0;
            while (next < log.turns()) {
                std::vector<uint8_t> packed;
                const uint32_t n = log.read(next, static_cast<uint32_t>(kMaxBatchTurns), kBatchBytes, packed);
                ASSERT_TRUE(n >= 1 && n <= kMaxBatchTurns);
                const std::vector<uint8_t> message = encode_turn_batch_packed(next, n, packed.data(), packed.size());
                ASSERT_TRUE(!message.empty() && message.size() <= kMaxMessageBytes && packed.size() <= kBatchBytes);
                TurnBatchMsg b;
                ASSERT_TRUE(decode(message, b) && b.turns.size() == n && batch_is(all, next, b));
                TurnBatchMsg built;                                       // and the batch made from the decoded turns is the same message
                built.first_turn = next;
                built.turns = b.turns;
                ASSERT_TRUE(encode(built) == message);
                next += n;
                ++batches;
            }
            ASSERT_EQ(next, 36000u);
            ASSERT_TRUE(batches >= 9 && batches <= 60);                   // 36,000 turns / 4096 at the least; a few hundred KB / 48 KB
        }
        // batches of every size (the turn limit, no byte limit): the count is the size asked for until the end, and everything comes back
        for (const uint32_t size : {1u, 2u, 3u, 5u, 7u, 16u, 63u, 64u, 65u, 100u, 255u, 256u, 1000u, 4095u, 4096u}) {
            uint32_t next = 0;
            while (next < 36000u) {
                std::vector<uint8_t> packed;
                const uint32_t n = log.read(next, size, SIZE_MAX, packed);
                ASSERT_EQ(n, std::min(size, 36000u - next));
                ASSERT_EQ(packed.size(), log.bytes_between(next, next + n));
                if (next % 997 == 0 || next + n == 36000u) {              // (decoded for a sample of them: all would be 15 x 36000 turns of decoding)
                    TurnBatchMsg b;
                    ASSERT_TRUE(decode(encode_turn_batch_packed(next, n, packed.data(), packed.size()), b) && batch_is(all, next, b));
                }
                next += n;
            }
        }
        // a limit in bytes: at least one turn, then as many as fit; a turn that is alone over the limit is a batch of its own
        for (const size_t limit : {size_t{1}, size_t{10}, size_t{100}, size_t{1000}, size_t{4096}, kBatchBytes}) {
            uint32_t next = 0;
            while (next < 36000u) {
                std::vector<uint8_t> packed;
                const uint32_t n = log.read(next, 4096, limit, packed);
                ASSERT_TRUE(n >= 1 && packed.size() == log.bytes_between(next, next + n));
                ASSERT_TRUE(n == 1 || packed.size() <= limit);            // more than one turn only when they fit
                if (next + n < 36000u) {                                  // ... and the next turn would not have fitted (or 4096 are in)
                    ASSERT_TRUE(n == 4096 || packed.size() + sizes[next + n] > limit);
                }
                next += n;
            }
        }
        // random reads from anywhere against the sealed turns
        {
            Lcg rng(11);
            for (int i = 0; i < 400; ++i) {
                const uint32_t from = rng.below(36000);
                const uint32_t max_turns = 1 + rng.below(100);
                const size_t limit = 1 + rng.below(5000);
                std::vector<uint8_t> packed;
                const uint32_t n = log.read(from, max_turns, limit, packed);
                ASSERT_TRUE(n >= 1 && n <= max_turns && from + n <= 36000u);
                TurnBatchMsg b;
                ASSERT_TRUE(decode(encode_turn_batch_packed(from, n, packed.data(), packed.size()), b) && batch_is(all, from, b));
            }
        }
        // bytes_between is the sum of the turns' sizes, for any range; ranges that are empty, reversed or past the end count for nothing
        {
            Lcg rng(12);
            for (int i = 0; i < 400; ++i) {
                const uint32_t a = rng.below(36000);
                const uint32_t b = a + rng.below(36000 - a + 1);
                size_t sum = 0;
                for (uint32_t t = a; t < b; ++t) sum += sizes[t];
                ASSERT_EQ(log.bytes_between(a, b), sum);
            }
            ASSERT_EQ(log.bytes_between(5, 5), size_t{0});
            ASSERT_EQ(log.bytes_between(9, 5), size_t{0});
            ASSERT_EQ(log.bytes_between(36000, 99999), size_t{0});
            ASSERT_EQ(log.bytes_between(35999, 99999), sizes[35999]);     // (the end is cut at the turns that there are)
            ASSERT_EQ(log.bytes_between(0, 0xFFFFFFFFu), blob);
            std::vector<uint8_t> out = {1, 2, 3};
            ASSERT_EQ(log.read(36000, 10, 100, out), 0u);                 // nothing at or past the end, nothing for zero turns, and `out` is only ever appended to
            ASSERT_EQ(log.read(0xFFFFFFFFu, 10, 100, out), 0u);
            ASSERT_EQ(log.read(0, 0, 100, out), 0u);
            ASSERT_TRUE(out == (std::vector<uint8_t>{1, 2, 3}));
            ASSERT_EQ(log.read(0, 1, 100, out), 1u);
            ASSERT_TRUE(out.size() == 3 + sizes[0] && out[0] == 1 && out[3] == everything[0]);
        }
        // an empty log reads nothing and costs nothing
        {
            TurnLog empty;
            std::vector<uint8_t> out;
            ASSERT_TRUE(empty.read(0, 10, 100, out) == 0 && out.empty() && empty.bytes() == 0 && empty.bytes_between(0, 10) == 0 && empty.usable() && empty.turns() == 0);
        }
        // a turn that is alone bigger than the byte limit still goes (a batch holds at least one turn)
        {
            TurnLog big;
            ASSERT_TRUE(big.append(heavy_turn(0, 200, 32)));
            TurnMsg small;
            small.turn = 1;
            ASSERT_TRUE(big.append(small));
            ASSERT_TRUE(big.append(heavy_turn(2, 1, 1)));
            std::vector<uint8_t> out;
            ASSERT_EQ(big.read(0, 10, 100, out), 1u);
            ASSERT_EQ(out.size(), 2 + 200 * (sim::kCommandHeaderBytes + 4 * 32));
            out.clear();
            ASSERT_EQ(big.read(1, 10, 100, out), 2u);                     // the two small turns go together
            out.clear();
            ASSERT_EQ(big.read(0, 10, 2 + 200 * (sim::kCommandHeaderBytes + 4 * 32) + 1, out), 1u);    // one byte more than the big turn: the empty turn after it (2 bytes) does not
            out.clear();
            ASSERT_EQ(big.read(0, 10, 2 + 200 * (sim::kCommandHeaderBytes + 4 * 32) + 2, out), 2u);    // two bytes more: it does
        }
        // the limit: a hostile client's log (64 commands of 32 ants in every turn: 8706 bytes and 4 of index) at 1 MiB stops at the turn that would pass it, and stays stopped
        {
            const size_t limit = 1024 * 1024;
            TurnLog hostile(limit);
            ASSERT_EQ(hostile.max_bytes(), limit);
            const size_t each = 2 + 64 * (sim::kCommandHeaderBytes + 4 * 32) + 4;
            const uint32_t fit = static_cast<uint32_t>(limit / each);     // 120
            uint32_t made = 0;
            while (made < 5000 && hostile.append(heavy_turn(made, 64, 32))) {
                ++made;
                ASSERT_TRUE(hostile.bytes() <= limit);
            }
            ASSERT_EQ(made, fit);                                         // exactly the turns that fit
            ASSERT_FALSE(hostile.usable());                               // a log with a hole serves no replay ...
            ASSERT_TRUE(hostile.turns() == 0 && hostile.bytes() == 0 && hostile.capacity_bytes() == 0);   // ... and a log that is dead holds nothing: what it had is freed the moment it died
            ASSERT_FALSE(hostile.append(heavy_turn(fit, 64, 32)));
            TurnMsg tiny;
            tiny.turn = fit;
            ASSERT_FALSE(hostile.append(tiny));                           // not even a small turn: it stays dead, and nothing is stored or allocated
            ASSERT_TRUE(hostile.turns() == 0 && hostile.bytes() == 0 && hostile.capacity_bytes() == 0 && !hostile.usable());
            std::vector<uint8_t> out;                                     // nothing can be read from it, nothing streamed
            ASSERT_EQ(hostile.read(0, 4096, SIZE_MAX, out), 0u);
            ASSERT_TRUE(out.empty() && hostile.bytes_between(0, 100) == 0);
        }
        // the limit to the byte: three empty turns cost 3 x (2 + 4) = 18
        for (const size_t limit : {size_t{0}, size_t{5}, size_t{6}, size_t{11}, size_t{12}, size_t{17}, size_t{18}, size_t{19}, size_t{23}, size_t{24}}) {
            TurnLog tight(limit);
            uint32_t made = 0;
            TurnMsg empty;
            while (made < 100) {
                empty.turn = made;
                if (!tight.append(empty)) break;
                ++made;
            }
            ASSERT_EQ(made, static_cast<uint32_t>(limit / 6));
            ASSERT_FALSE(tight.usable());                                 // (the loop ends when an append is refused: the log is dead and holds nothing)
            ASSERT_TRUE(tight.bytes() == 0 && tight.turns() == 0);
            // (what it held while it lived: the same turns again, one fewer than refused)
            TurnLog again(limit);
            for (uint32_t t = 0; t < made; ++t) {
                empty.turn = t;
                ASSERT_TRUE(again.append(empty) && again.bytes() == size_t{t + 1} * 6 && again.bytes() <= limit);
            }
        }
        // whatever the limit, never more than 0xFFFFFFF0 bytes (the offsets are 32 bits)
        ASSERT_EQ(TurnLog(SIZE_MAX).max_bytes(), static_cast<size_t>(0xFFFFFFF0u));
        ASSERT_EQ(TurnLog(1000).max_bytes(), size_t{1000});
        ASSERT_TRUE(TurnLog::kHardMaxBytes == 0xFFFFFFF0u);
        // a hole or a repeat is the end of the log: the turn is not the next one
        {
            TurnLog holey;
            TurnMsg t5;
            t5.turn = 5;
            ASSERT_FALSE(holey.append(t5));
            ASSERT_FALSE(holey.usable());
            TurnMsg t0;
            ASSERT_FALSE(holey.append(t0));                               // stopped for good
            TurnLog twice;
            ASSERT_TRUE(twice.append(t0));
            ASSERT_FALSE(twice.append(t0));
            ASSERT_TRUE(!twice.usable() && twice.turns() == 0 && twice.bytes() == 0);      // (dead: it freed the turn that it had)
        }
        // a turn that no batch could carry: the largest turn that does is 65,526 bytes packed (7 bytes of batch header and 65,529 are what a message may hold, and a turn is 2 + a multiple of 4)
        {
            auto turn_of = [](uint32_t number, size_t last_ants) {         // 481 commands of 32 ants, and one of `last_ants`
                TurnMsg t = heavy_turn(number, 481, 32);
                std::vector<uint32_t> list;
                for (size_t i = 0; i < last_ants; ++i) list.push_back(static_cast<uint32_t>(i));
                t.commands.push_back(cmd(CommandType::GroupMove, 0, 255, 1, 1, list));
                return t;
            };
            TurnLog fits;
            ASSERT_TRUE(fits.append(turn_of(0, 25)));                      // 2 + 481 x 136 + 108 = 65,526
            ASSERT_EQ(fits.bytes(), size_t{65526 + 4});
            std::vector<uint8_t> packed;
            ASSERT_EQ(fits.read(0, 10, 1, packed), 1u);
            TurnBatchMsg b;
            ASSERT_TRUE(decode(encode_turn_batch_packed(0, 1, packed.data(), packed.size()), b) && b.turns.size() == 1 && b.turns[0].commands.size() == 482);
            TurnLog too_big;
            ASSERT_FALSE(too_big.append(turn_of(0, 26)));                  // 65,530: no message could hold it
            ASSERT_TRUE(!too_big.usable() && too_big.turns() == 0 && too_big.bytes() == 0);
        }
        // more commands than a turn may hold are cut, as encode(TurnMsg) cuts them
        {
            TurnMsg many;
            for (size_t i = 0; i < kMaxTurnCommands + 88; ++i) many.commands.push_back(cmd(CommandType::Hatch, static_cast<uint8_t>(i % 4)));
            TurnLog cut;
            ASSERT_TRUE(cut.append(many));
            std::vector<uint8_t> packed;
            ASSERT_EQ(cut.read(0, 1, 1, packed), 1u);
            TurnBatchMsg b;
            ASSERT_TRUE(decode(encode_turn_batch_packed(0, 1, packed.data(), packed.size()), b) && b.turns[0].commands.size() == kMaxTurnCommands);
            ASSERT_EQ(packed.size(), 2 + kMaxTurnCommands * sim::kCommandHeaderBytes);
        }
    } TEST_END();

    TEST_CASE("N2.42 Attendance: Every Transition Of The Table, The Impossible Ones Change Nothing, Away Time Adds Up Over Episodes (Each Loss Counts At Least 5 s), The Pause Counts Each Moment Once (And At Least 5 s For Every Pause, For The Cap)") {
        using S = Attendance::State;
        const uint32_t t0 = 100000;
        // a snapshot of everything that can be seen: a call that must be a no-op may not change any of it
        auto snapshot = [](const Attendance& a, uint32_t now) {
            std::vector<uint32_t> v;
            for (uint8_t s = 0; s < 4; ++s) {
                v.push_back(static_cast<uint32_t>(a.state(s)));
                v.push_back(a.away_ms(s, now));
                v.push_back(a.percent(s));
                v.push_back(a.votes_for_continue(s));
            }
            v.insert(v.end(), {a.paused() ? 1u : 0u, a.pause_ms(now), a.connected_humans(), a.vote_subject(now), a.drops_by_vote(), a.drops_by_cap(), a.rejoins(), a.cap_s(now)});
            return v;
        };
        // ---- the table: which event is possible from which state, and where it leads ----
        struct Row {
            S from;
            bool lost, returning, progress, caught_up, failed, dropped;
        };
        const Row table[] = {
            {S::Present, true, true, false, false, false, true},
            {S::Absent, false, true, false, false, false, true},
            {S::CatchingUp, false, true, true, true, true, true},
            {S::Dropped, false, false, false, false, false, false},
            {S::Empty, false, false, false, false, false, false},
        };
        for (const Row& row : table) {
            for (int event = 0; event < 6; ++event) {
                Attendance a;
                a.seat_humans(0b0001 | (row.from == S::Empty ? 0 : 0b0010) | 0b1000, t0);        // seats 0, 3 and (unless the row is Empty) 1 are people; seat 2 is no person
                if (row.from == S::Absent) ASSERT_TRUE(a.lost(1, t0 + 1000));
                if (row.from == S::CatchingUp) ASSERT_TRUE(a.lost(1, t0 + 1000) && a.returning(1, t0 + 2000));
                if (row.from == S::Dropped) ASSERT_TRUE(a.dropped(1, t0 + 1000));
                ASSERT_EQ(a.state(1), row.from);
                const uint32_t now = t0 + 3000;
                const std::vector<uint32_t> before = snapshot(a, now);
                bool result = false;
                bool expect = false;
                S after = row.from;
                switch (event) {
                    case 0: result = a.lost(1, now); expect = row.lost; after = S::Absent; break;
                    case 1: result = a.returning(1, now); expect = row.returning; after = S::CatchingUp; break;
                    case 2: result = a.progress(1, 50, now); expect = row.progress; after = S::CatchingUp; break;
                    case 3: result = a.caught_up(1, now); expect = row.caught_up; after = S::Present; break;
                    case 4: result = a.catch_up_failed(1, now); expect = row.failed; after = S::Absent; break;
                    default: result = a.dropped(1, now); expect = row.dropped; after = S::Dropped; break;
                }
                ASSERT_EQ(result, expect);
                ASSERT_EQ(a.state(1), expect ? after : row.from);
                if (!expect) ASSERT_TRUE(snapshot(a, now) == before);                              // an event that does not fit changes nothing at all
            }
        }
        {   // seats that are no seats, and calls with seats out of range
            Attendance a;
            a.seat_humans(0b1011, t0);
            const std::vector<uint32_t> before = snapshot(a, t0 + 10);
            ASSERT_FALSE(a.lost(2, t0) || a.lost(4, t0) || a.lost(255, t0) || a.returning(2, t0) || a.returning(255, t0) || a.progress(2, 5, t0) || a.progress(7, 5, t0) ||
                         a.caught_up(2, t0) || a.caught_up(9, t0) || a.catch_up_failed(2, t0) || a.dropped(2, t0) || a.dropped(4, t0) || a.vote(0, 1, true, t0) || a.vote(9, 1, true, t0) || a.vote(0, 9, true, t0));
            ASSERT_TRUE(snapshot(a, t0 + 10) == before);
            ASSERT_TRUE(a.state(2) == S::Empty && a.state(4) == S::Empty && a.state(255) == S::Empty);
            ASSERT_TRUE(a.away_ms(2, t0 + 1000) == 0 && a.away_ms(200, t0 + 1000) == 0 && a.percent(200) == 0 && a.votes_for_continue(200) == 0);
            ASSERT_EQ(a.connected_humans(), 3);
            ASSERT_TRUE(a.state(0) == S::Present && a.state(1) == S::Present && a.state(3) == S::Present);
            ASSERT_EQ(a.config().vote_after_ms, kVoteAfterMs);
            ASSERT_TRUE(kVoteAfterMs == 30000 && kCatchUpStallMs == 20000 && kMinAbsenceMs == 5000 && kMaxPauseMs == 1800000);
        }
        // ---- one seat's episodes: loss, return, progress, catching up, three times; the total, the minimum, the pause ----
        {
            Attendance a;
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(!a.paused() && a.pause_ms(t0 + 9999) == 0 && a.away_ms(1, t0 + 9999) == 0 && a.cap_s(t0) == 1800);
            // episode 1: lost at +1 s, back at +6 s, caught up at +8 s: 7 s away
            ASSERT_TRUE(a.lost(1, t0 + 1000));
            ASSERT_TRUE(a.paused());
            ASSERT_EQ(a.away_ms(1, t0 + 1000), 0u);
            ASSERT_EQ(a.away_ms(1, t0 + 4000), 3000u);
            ASSERT_EQ(a.pause_ms(t0 + 4000), 5000u);                                 // (a pause counts at least 5 s toward the cap, from the moment it begins: 3 s were real)
            ASSERT_EQ(a.held_ms(t0 + 4000), 3000u);                                  // (the time that the match was held counts what it really was)
            ASSERT_FALSE(a.lost(1, t0 + 4500));                                      // already lost: the absence does not begin again
            ASSERT_EQ(a.away_ms(1, t0 + 5000), 4000u);
            ASSERT_EQ(a.connected_humans(), 2);
            ASSERT_TRUE(a.returning(1, t0 + 6000));
            ASSERT_EQ(a.state(1), S::CatchingUp);
            ASSERT_EQ(a.away_ms(1, t0 + 7000), 6000u);                               // the away time goes on from the loss
            ASSERT_TRUE(a.paused());
            ASSERT_FALSE(a.progress(1, 0, t0 + 7000));                               // 0 is not more than 0
            ASSERT_TRUE(a.progress(1, 10, t0 + 7100));
            ASSERT_EQ(a.percent(1), 10);
            ASSERT_FALSE(a.progress(1, 10, t0 + 7200));                              // the same again, or less, is no progress
            ASSERT_FALSE(a.progress(1, 9, t0 + 7300));
            ASSERT_EQ(a.percent(1), 10);
            ASSERT_TRUE(a.progress(1, 250, t0 + 7400));                              // above 100 is 100
            ASSERT_EQ(a.percent(1), 100);
            ASSERT_EQ(a.percent(0), 0);
            ASSERT_TRUE(a.caught_up(1, t0 + 8000));
            ASSERT_EQ(a.state(1), S::Present);
            ASSERT_EQ(a.percent(1), 0);
            ASSERT_FALSE(a.paused());
            ASSERT_EQ(a.rejoins(), 1u);
            ASSERT_EQ(a.away_ms(1, t0 + 8000), 7000u);
            ASSERT_EQ(a.away_ms(1, t0 + 99999), 7000u);                              // (an absence that is over does not grow)
            ASSERT_EQ(a.pause_ms(t0 + 8000), 7000u);
            ASSERT_EQ(a.pause_ms(t0 + 99999), 7000u);                                // nor does the pause
            ASSERT_EQ(a.connected_humans(), 3);
            // episode 2: a blip of 300 ms: it counts 5 s toward the seat's total, and 5 s toward the pause (so that a connection that flaps reaches the cap too)
            ASSERT_TRUE(a.lost(1, t0 + 20000));
            ASSERT_TRUE(a.returning(1, t0 + 20100));
            ASSERT_EQ(a.away_ms(1, t0 + 20300), 7300u);                              // (while it goes on, the real time: 7 s and 300 ms)
            ASSERT_TRUE(a.caught_up(1, t0 + 20300));
            ASSERT_EQ(a.away_ms(1, t0 + 20300), 12000u);                             // 7 s + the minimum of 5 s
            ASSERT_EQ(a.pause_ms(t0 + 20300), 7000u + 5000u);                        // the pause was 300 ms: the minimum counts for the cap, as for the vote
            ASSERT_EQ(a.held_ms(t0 + 20300), 7300u);                                 // (and what the match really waited: 7 s and 300 ms)
            // episode 3: 9 s, over the minimum
            ASSERT_TRUE(a.lost(1, t0 + 30000));
            ASSERT_EQ(a.away_ms(1, t0 + 30100), 12100u);
            ASSERT_TRUE(a.returning(1, t0 + 38000));
            ASSERT_TRUE(a.caught_up(1, t0 + 39000));
            ASSERT_EQ(a.away_ms(1, t0 + 39000), 21000u);                             // 12 s + 9 s
            ASSERT_EQ(a.pause_ms(t0 + 39000), 12000u + 9000u);
            ASSERT_EQ(a.held_ms(t0 + 39000), 7300u + 9000u);
            ASSERT_EQ(a.rejoins(), 3u);
            ASSERT_EQ(a.away_ms(0, t0 + 39000), 0u);                                 // the others have not been away
            ASSERT_EQ(a.cap_s(t0 + 39000), static_cast<uint16_t>((1800000u - 21000u + 999u) / 1000u));
        }
        {   // a catch-up that fails: the seat is Absent again and its time kept running from the loss; the pause goes on
            Attendance a;
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.lost(1, t0 + 1000) && a.returning(1, t0 + 11000));
            ASSERT_TRUE(a.progress(1, 30, t0 + 12000));
            ASSERT_TRUE(a.catch_up_failed(1, t0 + 16000));
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_EQ(a.percent(1), 0);
            ASSERT_EQ(a.away_ms(1, t0 + 17000), 16000u);
            ASSERT_TRUE(a.paused());
            ASSERT_EQ(a.pause_ms(t0 + 17000), 16000u);
            ASSERT_TRUE(a.returning(1, t0 + 18000));                                 // a second try (after the first failed): the percent starts again
            ASSERT_TRUE(a.progress(1, 5, t0 + 18100));                               // (5 is more than the 0 of the new try, less than the 30 of the old)
            ASSERT_TRUE(a.returning(1, t0 + 19000));                                 // a third Hello while the second try goes on: the try goes on as it was (REWRITTEN: it used to start again from 0 with a new stall clock, which let a key holder keep the pause alive for ever)
            ASSERT_EQ(a.percent(1), 5);
            ASSERT_EQ(a.away_ms(1, t0 + 20000), 19000u);
            ASSERT_TRUE(a.caught_up(1, t0 + 21000));
            ASSERT_EQ(a.away_ms(1, t0 + 21000), 20000u);
        }
        {   // a seat whose old link was not known to be dead: the new link takes it over, the seat is away from that moment on
            Attendance a;
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.returning(1, t0 + 5000));
            ASSERT_EQ(a.state(1), S::CatchingUp);
            ASSERT_TRUE(a.paused());
            ASSERT_EQ(a.away_ms(1, t0 + 5000), 0u);
            ASSERT_EQ(a.away_ms(1, t0 + 7000), 2000u);                               // not since the start of the match
            ASSERT_EQ(a.pause_ms(t0 + 7000), 5000u);                                 // (2 s were real: a pause counts at least 5 s toward the cap)
            ASSERT_EQ(a.held_ms(t0 + 7000), 2000u);
            ASSERT_TRUE(a.caught_up(1, t0 + 7000));
            ASSERT_EQ(a.away_ms(1, t0 + 7000), 5000u);                               // a loss counts at least 5 s
            ASSERT_EQ(a.pause_ms(t0 + 7000), 5000u);
            ASSERT_EQ(a.held_ms(t0 + 7000), 2000u);
        }
        // ---- dropped is final, from every state, and ends the absence ----
        for (const int from : {0, 1, 2}) {
            Attendance a;
            a.seat_humans(0b0111, t0);
            if (from >= 1) ASSERT_TRUE(a.lost(1, t0 + 1000));
            if (from >= 2) ASSERT_TRUE(a.returning(1, t0 + 2000));
            ASSERT_TRUE(a.dropped(1, t0 + 10000));
            ASSERT_EQ(a.state(1), S::Dropped);
            ASSERT_FALSE(a.paused());                                                // nobody else is away
            ASSERT_EQ(a.connected_humans(), 2);
            const uint32_t away = a.away_ms(1, t0 + 10000);
            ASSERT_EQ(away, from == 0 ? 0u : 9000u);
            ASSERT_EQ(a.away_ms(1, t0 + 500000), away);                              // frozen
            ASSERT_EQ(a.pause_ms(t0 + 500000), from == 0 ? 0u : 9000u);
            ASSERT_FALSE(a.lost(1, t0 + 20000) || a.returning(1, t0 + 20000) || a.progress(1, 99, t0 + 20000) || a.caught_up(1, t0 + 20000) || a.catch_up_failed(1, t0 + 20000) || a.dropped(1, t0 + 20000));
            ASSERT_FALSE(a.vote(1, 2, true, t0 + 20000));                            // a dropped seat does not vote
            ASSERT_EQ(a.state(1), S::Dropped);
            ASSERT_EQ(a.drops_by_vote(), 0u);                                        // (a seat that left is no drop by vote or by the cap)
            ASSERT_EQ(a.drops_by_cap(), 0u);
            ASSERT_TRUE(a.update(t0 + 60000).empty());
        }
        // ---- the pause counts every moment once, however many seats are away ----
        {
            Attendance a;
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0));                                              // seat 1 from 0 to 10 s, seat 2 from 4 s to 12 s: the pause is 0 to 12 s
            ASSERT_TRUE(a.lost(2, t0 + 4000));
            ASSERT_EQ(a.pause_ms(t0 + 6000), 6000u);
            ASSERT_TRUE(a.returning(1, t0 + 8000));
            ASSERT_TRUE(a.caught_up(1, t0 + 10000));
            ASSERT_TRUE(a.paused());
            ASSERT_EQ(a.pause_ms(t0 + 11000), 11000u);                               // (seat 2 is still away)
            ASSERT_TRUE(a.returning(2, t0 + 11500));
            ASSERT_EQ(a.pause_ms(t0 + 11800), 11800u);                               // catching up is pause too
            ASSERT_TRUE(a.caught_up(2, t0 + 12000));
            ASSERT_FALSE(a.paused());
            ASSERT_EQ(a.pause_ms(t0 + 12000), 12000u);
            ASSERT_EQ(a.pause_ms(t0 + 80000), 12000u);
            ASSERT_EQ(a.away_ms(1, t0 + 80000), 10000u);
            ASSERT_EQ(a.away_ms(2, t0 + 80000), 8000u);
            ASSERT_EQ(a.rejoins(), 2u);
            // a blip of a second: 5 s for the seat, and 5 s for the cap (what the match really waited was 1 s)
            ASSERT_TRUE(a.lost(2, t0 + 100000) && a.returning(2, t0 + 100500) && a.caught_up(2, t0 + 101000));
            ASSERT_EQ(a.away_ms(2, t0 + 101000), 13000u);
            ASSERT_EQ(a.pause_ms(t0 + 101000), 12000u + 5000u);
            ASSERT_EQ(a.held_ms(t0 + 101000), 12000u + 1000u);
        }
        {   // the match begins again: everything that was before is forgotten
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(1, t0 + 100) && a.returning(1, t0 + 200) && a.caught_up(1, t0 + 300) && a.lost(2, t0 + 400) && a.dropped(3, t0 + 500));
            a.seat_humans(0b0110, t0 + 1000);
            ASSERT_TRUE(a.state(0) == S::Empty && a.state(1) == S::Present && a.state(2) == S::Present && a.state(3) == S::Empty);
            ASSERT_TRUE(!a.paused() && a.pause_ms(t0 + 1000) == 0 && a.away_ms(1, t0 + 1000) == 0 && a.rejoins() == 0 && a.connected_humans() == 2);
        }
    } TEST_END();

    TEST_CASE("N2.43 Attendance: The Vote Opens At 30 s In All, One Seat At A Time (The One Away Longest, Never One That Is Catching Up); More Than Half Of The Connected Players Win It (1, 2 And 3 Connected, Every Number Of Votes); A Lost Voter Leaves The Count") {
        using S = Attendance::State;
        const uint32_t t0 = 5000000;
        // ---- the vote opens when the seat's total reaches 30 s: not a millisecond before, also over several absences ----
        {
            Attendance a;
            a.seat_humans(0b1011, t0);
            ASSERT_TRUE(a.lost(3, t0));
            ASSERT_EQ(a.vote_subject(t0 + 29999), 255);
            ASSERT_FALSE(a.vote(0, 3, true, t0 + 29999));                            // no vote is open: the choice is ignored
            ASSERT_EQ(a.presence_for(0, t0 + 29999).vote_seat, 255);
            ASSERT_EQ(a.vote_subject(t0 + 30000), 3);
            ASSERT_EQ(a.presence_for(0, t0 + 30000).vote_seat, 3);
            ASSERT_TRUE(a.update(t0 + 29999).empty() && a.update(t0 + 30000).empty());        // (no vote was cast)
            Attendance b;                                                            // 7 s, a return, then 23 s more: the total is 30 s at the same instant
            b.seat_humans(0b1011, t0);
            ASSERT_TRUE(b.lost(3, t0) && b.returning(3, t0 + 6000) && b.caught_up(3, t0 + 7000) && b.lost(3, t0 + 20000));
            ASSERT_EQ(b.away_ms(3, t0 + 42999), 29999u);
            ASSERT_EQ(b.vote_subject(t0 + 42999), 255);
            ASSERT_EQ(b.vote_subject(t0 + 43000), 3);
            Attendance::Config many;                                                 // (a seat is taken back three times a minute by default: this one is cut every 3 s, so the rule is raised to its most)
            many.rejoin_attempts = Attendance::kMaxRejoinAttempts;
            Attendance c(many);                                                      // six losses of a second each count 5 s each: the vote opens at 30 s of loss in all ...
            c.seat_humans(0b1011, t0);
            for (int i = 0; i < 6; ++i) {
                const uint32_t at = t0 + static_cast<uint32_t>(i) * 3000u;
                ASSERT_TRUE(c.lost(3, at) && c.returning(3, at + 500) && c.caught_up(3, at + 1000));
            }
            ASSERT_EQ(c.away_ms(3, t0 + 20000), 30000u);                             // six blips of a second are 30 s
            ASSERT_TRUE(c.lost(3, t0 + 20000));
            ASSERT_EQ(c.vote_subject(t0 + 20000), 3);                                // ... so the next loss is put to the vote at once
        }
        // ---- the rule: 2 * votes > connected; 1, 2 and 3 connected players (a match has four seats: one that is missing leaves at most three to vote), every subset that chooses Continue ----
        for (uint8_t connected = 1; connected <= 3; ++connected) {
            uint8_t mask = 0b1000;                                                   // seat 3 is the one that is missing; seats 0 .. connected - 1 are connected
            for (uint8_t s = 0; s < connected; ++s) mask = static_cast<uint8_t>(mask | (1u << s));
            for (unsigned chose = 0; chose < (1u << connected); ++chose) {           // the voters that choose Continue (bit v); the others choose Keep waiting
                for (const bool others_abstain : {false, true}) {
                    Attendance a;
                    a.seat_humans(mask, t0);
                    ASSERT_TRUE(a.lost(3, t0));
                    const uint32_t now = t0 + 31000;
                    unsigned votes = 0;
                    for (uint8_t v = 0; v < connected; ++v) {
                        const bool cont = (chose & (1u << v)) != 0;
                        if (!cont && others_abstain) continue;
                        ASSERT_TRUE(a.vote(v, 3, cont, now));
                        votes += cont ? 1u : 0u;
                    }
                    ASSERT_EQ(a.votes_for_continue(3), votes);
                    const PresenceMsg p = a.presence_for(0, now);
                    ASSERT_TRUE(p.vote_seat == 3 && p.votes_continue == votes && p.voters == connected);
                    const bool wins = 2 * votes > connected;                         // 1 of 1; 2 of 2; 2 of 3 (not 1 of 2: that is half)
                    const std::vector<uint8_t> dropped = a.update(now);
                    ASSERT_EQ(dropped.size(), wins ? size_t{1} : size_t{0});
                    ASSERT_EQ(a.state(3), wins ? S::Dropped : S::Absent);
                    ASSERT_EQ(a.drops_by_vote(), wins ? 1u : 0u);
                    ASSERT_EQ(a.drops_by_cap(), 0u);
                    if (wins) ASSERT_TRUE(dropped[0] == 3 && !a.paused());
                    else ASSERT_TRUE(a.paused() && a.vote_subject(now) == 3);
                }
            }
        }
        // the rule itself, for every number of connected players that the arithmetic knows (a match has four seats, so a missing one leaves three at most; 4 shows the rule is general)
        for (uint8_t connected = 0; connected <= 4; ++connected) {
            for (uint8_t votes = 0; votes <= connected; ++votes) {
                ASSERT_EQ(Attendance::vote_won(votes, connected), connected > 0 && votes * 2 > connected);
            }
        }
        ASSERT_TRUE(Attendance::vote_won(1, 1) && Attendance::vote_won(2, 2) && Attendance::vote_won(2, 3) && Attendance::vote_won(3, 4));        // 1 of 1, 2 of 2, 2 of 3, 3 of 4
        ASSERT_FALSE(Attendance::vote_won(0, 1) || Attendance::vote_won(1, 2) || Attendance::vote_won(1, 3) || Attendance::vote_won(2, 4) || Attendance::vote_won(0, 0));    // half is not more than half
        ASSERT_FALSE(Attendance::vote_won(1, 0) || Attendance::vote_won(4, 0));                                                                                       // nobody connected: never
        {   // nobody connected can never win (and one seat of one is all of them)
            Attendance a;
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.lost(0, t0 + 1000));                      // both away: nobody is there to vote
            ASSERT_EQ(a.connected_humans(), 0);
            ASSERT_TRUE(a.update(t0 + 600000).empty());                              // (the cap is 30 minutes)
            ASSERT_EQ(a.vote_subject(t0 + 600000), 1);
            ASSERT_FALSE(a.vote(0, 1, true, t0 + 600000));                           // an absent seat does not vote
        }
        // ---- a choice can be changed; a seat that does not vote is as one that wants to keep waiting ----
        {
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(3, t0));
            const uint32_t now = t0 + 30000;
            ASSERT_TRUE(a.vote(0, 3, true, now) && a.vote(1, 3, true, now));        // two of three: a majority ...
            ASSERT_TRUE(a.vote(1, 3, false, now));                                   // ... until the second thinks again
            ASSERT_EQ(a.votes_for_continue(3), 1u);
            ASSERT_TRUE(a.presence_for(1, now).your_vote == 1 && a.presence_for(0, now).your_vote == 2 && a.presence_for(2, now).your_vote == 0);
            ASSERT_TRUE(a.update(now).empty());
            ASSERT_TRUE(a.vote(1, 3, true, now));
            ASSERT_TRUE(a.vote(2, 3, false, now + 1));
            ASSERT_EQ(a.presence_for(0, now + 1).votes_continue, 2);
            const std::vector<uint8_t> dropped = a.update(now + 1);                  // 2 of 3 chose Continue; the third chose to keep waiting
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 3);
        }
        // ---- a voter that is lost leaves the count at once, and its choice does not come back with it ----
        {
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(3, t0));
            const uint32_t now = t0 + 30000;
            ASSERT_TRUE(a.vote(0, 3, true, now) && a.vote(1, 3, true, now));        // 2 of 3: it would win at the next update
            ASSERT_TRUE(a.lost(1, now + 10));                                        // ... but one of the two voters is lost first: 1 of 2 is only half
            ASSERT_EQ(a.connected_humans(), 2);
            ASSERT_EQ(a.votes_for_continue(3), 1u);
            ASSERT_EQ(a.presence_for(0, now + 10).voters, 2);
            ASSERT_TRUE(a.update(now + 10).empty());
            ASSERT_TRUE(a.returning(1, now + 20000) && a.caught_up(1, now + 21000));
            ASSERT_EQ(a.connected_humans(), 3);
            ASSERT_EQ(a.votes_for_continue(3), 1u);                                  // its old choice is gone: it must choose again
            ASSERT_EQ(a.presence_for(1, now + 21000).your_vote, 0);
            ASSERT_TRUE(a.update(now + 21000).empty());
            ASSERT_TRUE(a.vote(1, 3, true, now + 21000));
            ASSERT_EQ(a.update(now + 21000).size(), 1u);
            // the same with a voter that is dropped (it left): its choice leaves the count
            Attendance d;
            d.seat_humans(0b1111, t0);
            ASSERT_TRUE(d.lost(3, t0) && d.vote(0, 3, true, now) && d.vote(1, 3, true, now) && d.dropped(1, now + 5));
            ASSERT_EQ(d.votes_for_continue(3), 1u);
            ASSERT_TRUE(d.update(now + 5).empty());                                  // 1 of the 2 that are left
        }
        // ---- votes that do not count: wrong subject, a voter that is not connected, a seat that is no seat; and no violation (only false) ----
        {
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(2, t0) && a.lost(3, t0 + 10000));
            const uint32_t now = t0 + 31000;                                         // seat 2 has been away 31 s, seat 3 21 s: the vote is about 2
            ASSERT_EQ(a.vote_subject(now), 2);
            ASSERT_FALSE(a.vote(0, 3, true, now));                                   // about seat 3: it is not the subject
            ASSERT_FALSE(a.vote(0, 1, true, now));                                   // about a seat that is present
            ASSERT_FALSE(a.vote(0, 0, true, now));
            ASSERT_FALSE(a.vote(2, 2, true, now));                                   // from the seat that is missing
            ASSERT_FALSE(a.vote(3, 2, true, now));                                   // from another seat that is missing
            ASSERT_FALSE(a.vote(4, 2, true, now));                                   // from a seat that does not exist
            ASSERT_FALSE(a.vote(0, 4, true, now));
            ASSERT_FALSE(a.vote(255, 255, true, now));
            ASSERT_EQ(a.votes_for_continue(2), 0u);
            ASSERT_EQ(a.votes_for_continue(3), 0u);
            ASSERT_TRUE(a.returning(3, now) && a.vote_subject(now) == 2);            // a seat that is catching up cannot vote either
            ASSERT_FALSE(a.vote(3, 2, true, now));
            ASSERT_TRUE(a.vote(0, 2, true, now));
        }
        // ---- one vote at a time: the seat that has been away longest; of a tie the lowest seat; a seat that is catching up is never the subject; votes about a seat are gone when it changes state ----
        {
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(2, t0) && a.lost(3, t0));                             // lost together: a tie
            ASSERT_EQ(a.vote_subject(t0 + 40000), 2);
            const PresenceMsg tie = a.presence_for(0, t0 + 40000);
            ASSERT_EQ(tie.missing.size(), 2u);
            ASSERT_TRUE(tie.missing[0].seat == 2 && tie.missing[1].seat == 3 && tie.missing[0].waited_s == 40 && tie.missing[1].waited_s == 40);       // (a tie: the lowest seat first)
            Attendance b;
            b.seat_humans(0b1111, t0);
            ASSERT_TRUE(b.lost(3, t0) && b.lost(1, t0 + 5000));                      // 3 has been away longer than 1: 3, whatever the numbers of the seats
            ASSERT_EQ(b.vote_subject(t0 + 36000), 3);
            ASSERT_EQ(b.vote_subject(t0 + 29999), 255);
            ASSERT_EQ(b.presence_for(0, t0 + 36000).missing.size(), 2u);
            ASSERT_EQ(b.presence_for(0, t0 + 36000).missing[0].seat, 3);              // longest away first
            // seat 3 (away 60 s) comes back and is catching up: not the subject any more, though it has been away longest; seat 1 (55 s) is
            Attendance c;
            c.seat_humans(0b1111, t0);
            ASSERT_TRUE(c.lost(3, t0) && c.lost(1, t0 + 5000));
            const uint32_t now = t0 + 60000;
            ASSERT_EQ(c.vote_subject(now), 3);
            ASSERT_TRUE(c.vote(0, 3, true, now));
            ASSERT_TRUE(c.returning(3, now));
            ASSERT_EQ(c.vote_subject(now), 1);
            ASSERT_EQ(c.presence_for(0, now).vote_seat, 1);
            ASSERT_EQ(c.presence_for(0, now).missing[0].seat, 3);                    // (still first in the list: away longest) and not the vote's seat
            ASSERT_EQ(c.votes_for_continue(3), 0u);                                  // the votes about it are gone: it is back
            ASSERT_TRUE(c.vote(0, 1, true, now));
            ASSERT_EQ(c.votes_for_continue(1), 1u);
            ASSERT_TRUE(c.catch_up_failed(3, now + 1000));                           // it fails: absent again, with the longest total: the subject again, with no votes
            ASSERT_EQ(c.vote_subject(now + 1000), 3);
            ASSERT_EQ(c.votes_for_continue(3), 0u);
            ASSERT_FALSE(c.vote(0, 1, true, now + 1000));                            // the vote is about 3 now: a choice about 1 is ignored
            ASSERT_EQ(c.votes_for_continue(1), 1u);                                  // (what was chosen about 1 stays with 1 until it changes state)
            // the votes of one subject are gone when it returns, whether it catches up or not
            ASSERT_TRUE(c.vote(0, 3, true, now + 1000));
            ASSERT_TRUE(c.returning(3, now + 2000) && c.catch_up_failed(3, now + 3000));
            ASSERT_EQ(c.votes_for_continue(3), 0u);
        }
        // ---- one vote at a time, through update(): the first seat is dropped by its vote, the second starts with no votes ----
        {
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.lost(2, t0 + 2000));
            const uint32_t now = t0 + 40000;
            ASSERT_EQ(a.vote_subject(now), 1);
            ASSERT_TRUE(a.vote(0, 1, true, now) && a.vote(3, 1, true, now));        // 2 of the 2 that are connected
            std::vector<uint8_t> dropped = a.update(now);
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1);                     // seat 1 only: seat 2 was not asked yet
            ASSERT_EQ(a.state(2), S::Absent);
            ASSERT_EQ(a.vote_subject(now), 2);                                       // now it is
            ASSERT_EQ(a.votes_for_continue(2), 0u);
            ASSERT_TRUE(a.paused());                                                 // (seat 2 is still away: the match still waits)
            ASSERT_TRUE(a.update(now + 100).empty());
            ASSERT_TRUE(a.vote(0, 2, true, now + 200) && a.vote(3, 2, true, now + 200));
            dropped = a.update(now + 200);
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 2);
            ASSERT_FALSE(a.paused());
            ASSERT_EQ(a.drops_by_vote(), 2u);
        }
    } TEST_END();

    TEST_CASE("N2.44 Attendance: A Catch-Up That Shows No Progress For 20 s Is Let Go; No Seat Is Dropped For Its Own Time, The Match's Total Pause Is Capped (Every Seat That Is Not Present Is Dropped At The Cap, Absent Or Catching Up, Whatever Progress It Shows) And cap_s Counts Down; The Same Sequence Across The Wrap Of The Clock; Presence Is Always Something The Decoder Takes") {
        using S = Attendance::State;
        // ---- the stall: 20 s without progress (a higher percent) and the seat is absent again ----
        {
            const uint32_t t0 = 1000000;
            Attendance a;
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 5000));
            ASSERT_TRUE(a.update(t0 + 24999).empty() && a.state(1) == S::CatchingUp);        // 19,999 ms since the return
            ASSERT_TRUE(a.progress(1, 10, t0 + 15000));                                       // a sign of life at 10 s into it: the clock starts again
            ASSERT_TRUE(a.update(t0 + 34999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_FALSE(a.progress(1, 10, t0 + 30000));                                      // the same percent again is no progress: the clock goes on from 15 s
            ASSERT_TRUE(a.update(t0 + 34999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_TRUE(a.update(t0 + 35000).empty());                                        // 20,000 ms: let go (not dropped: it is Absent again, with its time)
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_TRUE(a.paused());
            ASSERT_EQ(a.away_ms(1, t0 + 35000), 35000u);
            ASSERT_EQ(a.percent(1), 0);
            ASSERT_TRUE(a.returning(1, t0 + 36000) && a.progress(1, 1, t0 + 40000));         // it may try again, and each try has its own 20 s
            ASSERT_TRUE(a.update(t0 + 59999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_TRUE(a.update(t0 + 60000).empty() && a.state(1) == S::Absent);
            // a seat that is only Absent has no stall clock
            ASSERT_TRUE(a.update(t0 + 900000).empty() && a.state(1) == S::Absent);
        }
        // ---- no seat is dropped for its own time ----
        {
            const uint32_t t0 = 1000000;
            Attendance::Config cfg;
            cfg.max_pause_ms = 24u * 3600u * 1000u;                                           // (the longest that a room may be set to)
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0));
            ASSERT_EQ(a.cap_s(t0), static_cast<uint16_t>(0xFFFF));                            // (a day is more than a u16 of seconds: it is told as "more")
            for (uint32_t hours = 1; hours <= 20; hours += 3) {
                ASSERT_TRUE(a.update(t0 + hours * 3600u * 1000u).empty());                    // ten hours, nineteen: still held
                ASSERT_EQ(a.state(1), S::Absent);
            }
            ASSERT_TRUE(a.update(t0 + 24u * 3600u * 1000u - 1).empty());
            const std::vector<uint8_t> at_cap = a.update(t0 + 24u * 3600u * 1000u);          // only the cap ends it
            ASSERT_TRUE(at_cap.size() == 1 && at_cap[0] == 1);
            ASSERT_EQ(a.cap_s(t0 + 24u * 3600u * 1000u), 0);
        }
        // ---- the cap: the total of the match's pauses; every Absent seat goes with it, whatever its own time ----
        {
            const uint32_t t0 = 3000000;
            Attendance::Config cfg;
            cfg.max_pause_ms = 60000;
            {   // one seat: dropped exactly when the pauses reach 60 s
                Attendance a(cfg);
                a.seat_humans(0b0111, t0);
                ASSERT_TRUE(a.lost(1, t0 + 5000));
                ASSERT_TRUE(a.update(t0 + 64999).empty() && a.state(1) == S::Absent);
                ASSERT_EQ(a.cap_left_ms(t0 + 64999), 1u);
                const std::vector<uint8_t> dropped = a.update(t0 + 65000);
                ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1 && a.state(1) == S::Dropped);
                ASSERT_TRUE(a.cap_reached(t0 + 65000) && a.cap_left_ms(t0 + 65000) == 0 && a.cap_s(t0 + 65000) == 0);
                ASSERT_EQ(a.drops_by_cap(), 1u);
                ASSERT_EQ(a.drops_by_vote(), 0u);
                ASSERT_FALSE(a.paused());
                ASSERT_EQ(a.pause_ms(t0 + 70000), 60000u);
                // the budget is gone: the next loss is dropped by the next update, however brief
                ASSERT_TRUE(a.lost(2, t0 + 100000));
                const std::vector<uint8_t> at_once = a.update(t0 + 100000);
                ASSERT_TRUE(at_once.size() == 1 && at_once[0] == 2 && a.state(2) == S::Dropped);
                ASSERT_EQ(a.drops_by_cap(), 2u);
            }
            {   // two seats: both go at the cap, the one that was away 10 s as the one that was away 60 s (no per-seat time)
                Attendance a(cfg);
                a.seat_humans(0b0111, t0);
                ASSERT_TRUE(a.lost(1, t0) && a.lost(2, t0 + 50000));
                const std::vector<uint8_t> dropped = a.update(t0 + 60000);
                ASSERT_TRUE(dropped.size() == 2 && dropped[0] == 1 && dropped[1] == 2);
                ASSERT_EQ(a.away_ms(2, t0 + 60000), 10000u);
                ASSERT_EQ(a.drops_by_cap(), 2u);
            }
            {   // the pauses add up over the match: 20 s + 30 s, and the third pause is dropped when it reaches the 10 s that are left
                Attendance a(cfg);
                a.seat_humans(0b0111, t0);
                ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 19000) && a.caught_up(1, t0 + 20000));
                ASSERT_EQ(a.cap_left_ms(t0 + 50000), 40000u);
                ASSERT_TRUE(a.lost(2, t0 + 100000) && a.returning(2, t0 + 129000) && a.caught_up(2, t0 + 130000));
                ASSERT_EQ(a.pause_ms(t0 + 200000), 50000u);
                ASSERT_EQ(a.cap_left_ms(t0 + 200000), 10000u);
                ASSERT_TRUE(a.lost(1, t0 + 300000));
                ASSERT_TRUE(a.update(t0 + 309999).empty() && a.state(1) == S::Absent);
                const std::vector<uint8_t> dropped = a.update(t0 + 310000);
                ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1);
            }
            {   // the cap is ABSOLUTE: a seat that is catching up and makes progress is dropped at the cap like every seat that is not present (REWRITTEN: it used to keep its chance for as long as it
                // made progress, and a key holder that said Hello again and again, each time with a little progress, held a room paused for ever)
                Attendance a(cfg);
                a.seat_humans(0b0111, t0);
                ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 30000));
                ASSERT_TRUE(a.progress(1, 5, t0 + 40000) && a.progress(1, 6, t0 + 55000));
                ASSERT_TRUE(a.update(t0 + 59999).empty() && a.state(1) == S::CatchingUp);     // 1 ms before the cap: it is getting on
                ASSERT_FALSE(a.cap_reached(t0 + 59999));
                const std::vector<uint8_t> dropped = a.update(t0 + 60000);                   // the cap: dropped, whatever progress it shows
                ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1 && a.state(1) == S::Dropped);
                ASSERT_TRUE(a.cap_reached(t0 + 60000) && a.cap_s(t0 + 60000) == 0);
                ASSERT_EQ(a.drops_by_cap(), 1u);
                ASSERT_FALSE(a.paused());
                // a seat that catches up in time is back; a later loss, which counts 5 s at least, finds the cap reached and is dropped at once
                Attendance b(cfg);
                b.seat_humans(0b0111, t0);
                ASSERT_TRUE(b.lost(1, t0) && b.returning(1, t0 + 30000) && b.progress(1, 50, t0 + 50000));
                ASSERT_TRUE(b.update(t0 + 55000).empty() && b.progress(1, 99, t0 + 55000) && b.caught_up(1, t0 + 56000));
                ASSERT_TRUE(b.state(1) == S::Present && !b.paused());
                ASSERT_EQ(b.pause_ms(t0 + 56000), 56000u);
                ASSERT_TRUE(b.lost(2, t0 + 70000));                                           // (56 s + at least 5 s: the budget of 60 s is gone)
                const std::vector<uint8_t> next = b.update(t0 + 70000);
                ASSERT_TRUE(next.size() == 1 && next[0] == 2);
                // a catch-up that was not getting on at all is let go before the cap can wait for it: 20 s without progress
                Attendance c(cfg);
                c.seat_humans(0b0111, t0);
                ASSERT_TRUE(c.lost(1, t0) && c.returning(1, t0 + 30000));
                ASSERT_TRUE(c.update(t0 + 49999).empty() && c.state(1) == S::CatchingUp);
                const std::vector<uint8_t> stalled = c.update(t0 + 50000);                    // 20 s of no progress: Absent again; the pause is 50 s, the cap 60 s: held
                ASSERT_TRUE(stalled.empty() && c.state(1) == S::Absent);
            }
            {   // the cap and a vote at the same moment: the vote's seat is the vote's, the rest are the cap's
                Attendance a(cfg);
                a.seat_humans(0b1111, t0);
                ASSERT_TRUE(a.lost(1, t0) && a.lost(2, t0 + 1000));
                ASSERT_TRUE(a.vote(0, 1, true, t0 + 59000) && a.vote(3, 1, true, t0 + 59000));          // the two that are connected
                ASSERT_FALSE(a.vote(2, 1, true, t0 + 59000));                                            // (seat 2 is away: it cannot vote)
                const std::vector<uint8_t> dropped = a.update(t0 + 60000);
                ASSERT_TRUE(dropped.size() == 2 && dropped[0] == 1 && dropped[1] == 2);
                ASSERT_TRUE(a.drops_by_vote() == 1 && a.drops_by_cap() == 1);
            }
        }
        // ---- cap_s: seconds that are left, rounded up, so 0 means the cap is reached; 0xFFFF for more than a u16 holds ----
        {
            const uint32_t t0 = 777;
            Attendance::Config cfg;
            cfg.max_pause_ms = 60000;
            Attendance a(cfg);
            a.seat_humans(0b0011, t0);
            ASSERT_EQ(a.cap_s(t0), 60);                                                       // not paused: the whole budget
            ASSERT_EQ(a.cap_s(t0 + 99999), 60);                                               // (and it does not run down while nobody is away)
            ASSERT_TRUE(a.lost(1, t0 + 5000));
            ASSERT_EQ(a.cap_s(t0 + 5000), 55);                                                // (a pause counts at least 5 s, from its first moment)
            ASSERT_EQ(a.cap_s(t0 + 5001), 55);
            ASSERT_EQ(a.cap_s(t0 + 10000), 55);                                               // 5 s really: the minimum and the pause agree
            ASSERT_EQ(a.cap_s(t0 + 10001), 55);                                               // 54,999 ms: still "55" (rounded up)
            ASSERT_EQ(a.cap_s(t0 + 11000), 54);
            ASSERT_EQ(a.cap_s(t0 + 11001), 54);
            ASSERT_EQ(a.cap_s(t0 + 34000), 31);
            ASSERT_EQ(a.cap_s(t0 + 64000), 1);
            ASSERT_EQ(a.cap_s(t0 + 64001), 1);
            ASSERT_EQ(a.cap_s(t0 + 64999), 1);
            ASSERT_EQ(a.cap_s(t0 + 65000), 0);
            ASSERT_EQ(a.cap_s(t0 + 99999), 0);
            Attendance::Config long_cfg;
            long_cfg.max_pause_ms = 86400u * 1000u;
            Attendance b(long_cfg);
            b.seat_humans(0b0011, t0);
            ASSERT_EQ(b.cap_s(t0), 0xFFFF);
            ASSERT_TRUE(b.lost(1, t0));
            ASSERT_EQ(b.cap_s(t0 + 20000000), 0xFFFF);                                        // 86,400 - 20,000 s = 66,400 s: more than a u16
            ASSERT_EQ(b.cap_s(t0 + 21000000), static_cast<uint16_t>(65400));                  // 65,400 s
            ASSERT_EQ(b.cap_s(t0 + 20865000), static_cast<uint16_t>(65535));                  // 65,535 s left: exactly the largest
            ASSERT_EQ(b.cap_s(t0 + 20864999), 0xFFFF);                                        // 65,535.001 s
            ASSERT_EQ(b.cap_s(t0 + 20865001), static_cast<uint16_t>(65535));                  // 65,534.999 s rounds up to 65,535
            ASSERT_EQ(b.cap_s(t0 + 20866000), static_cast<uint16_t>(65534));
            // the wait that Presence tells saturates at 0xFFFF seconds too (a pause of a day: the cap of such a room is 24 hours)
            ASSERT_EQ(b.presence_for(0, t0 + 65534999).missing[0].waited_s, 65534);
            ASSERT_EQ(b.presence_for(0, t0 + 65535000).missing[0].waited_s, 0xFFFF);
            ASSERT_EQ(b.presence_for(0, t0 + 70000000).missing[0].waited_s, 0xFFFF);
            ASSERT_EQ(b.presence_for(0, t0 + 70000000).cap_s, 16400);                       // (86,400 s of cap less the 70,000 s that have gone)
            Attendance::Config zero_cfg;                                                      // a room with no pause budget at all: the first update drops any seat that is away
            zero_cfg.max_pause_ms = 0;
            Attendance z(zero_cfg);
            z.seat_humans(0b0011, t0);
            ASSERT_EQ(z.cap_s(t0), 0);
            ASSERT_TRUE(z.lost(1, t0 + 10));
            ASSERT_EQ(z.update(t0 + 10).size(), 1u);
        }
        // ---- the same sequence, started at different points of the 32-bit clock (8 s before the wrap, at the wrap, over the signed half), makes the same decisions at the same offsets ----
        {
            auto scenario = [](uint32_t t0) {
                std::vector<int64_t> trace;
                auto at = [&](uint32_t ms) { return t0 + ms; };
                Attendance::Config cfg;
                cfg.max_pause_ms = 120000;
                Attendance a(cfg);
                a.seat_humans(0b1111, at(0));
                auto note = [&](uint32_t ms) {                                                // what can be seen at an offset
                    trace.push_back(ms);
                    for (uint8_t s = 0; s < 4; ++s) {
                        trace.push_back(static_cast<int64_t>(a.state(s)));
                        trace.push_back(a.away_ms(s, at(ms)));
                    }
                    trace.push_back(a.pause_ms(at(ms)));
                    trace.push_back(a.cap_s(at(ms)));
                    trace.push_back(a.vote_subject(at(ms)));
                    trace.push_back(a.connected_humans());
                    const PresenceMsg p = a.presence_for(0, at(ms));
                    trace.push_back(p.vote_seat);
                    trace.push_back(p.votes_continue);
                    trace.push_back(static_cast<int64_t>(p.missing.size()));
                    for (const auto& e : p.missing) {
                        trace.push_back(e.seat);
                        trace.push_back(e.waited_s);
                        trace.push_back(e.progress);
                    }
                };
                note(0);
                trace.push_back(a.lost(3, at(2000)));
                note(2000);
                trace.push_back(a.lost(2, at(9000)));
                trace.push_back(a.returning(2, at(11000)));
                trace.push_back(a.progress(2, 40, at(12000)));
                note(15000);
                trace.push_back(a.caught_up(2, at(17000)));
                note(17000);
                note(31999);                                                                  // seat 3 has been away 29,999 ms
                note(32000);                                                                  // 30,000: the vote opens
                trace.push_back(a.vote(0, 3, true, at(33000)));
                trace.push_back(a.vote(1, 3, false, at(33000)));
                note(33000);
                for (uint32_t ms = 34000; ms <= 70000; ms += 6000) {
                    for (uint8_t seat : a.update(at(ms))) trace.push_back(1000 + seat);
                    note(ms);
                }
                trace.push_back(a.vote(2, 3, true, at(70500)));
                for (uint8_t seat : a.update(at(70600))) trace.push_back(2000 + seat);       // 2 of 3 voted Continue: seat 3 is dropped
                note(70600);
                trace.push_back(a.lost(1, at(80000)));
                trace.push_back(a.returning(1, at(81000)));
                for (uint32_t ms = 82000; ms <= 135000; ms += 1000) {                         // seat 1 never makes progress: let go at 20 s, then the cap (120 s of pause in all) drops it
                    for (uint8_t seat : a.update(at(ms))) trace.push_back(3000 + seat);
                }
                note(135000);
                trace.push_back(a.drops_by_vote());
                trace.push_back(a.drops_by_cap());
                trace.push_back(a.rejoins());
                return trace;
            };
            const std::vector<int64_t> reference = scenario(1000000);
            ASSERT_TRUE(reference.size() > 100);
            for (const uint32_t start : {0u, 0xFFFFE000u, 0xFFFFFFFFu, 0xFFFFFFFFu - 40000u, 0x7FFFF000u, 0x80000000u, 0x80000000u - 1u, 0xFFFFFFFFu - 100000u}) {
                ASSERT_TRUE(scenario(start) == reference);
            }
            bool saw_drop_by_vote = false;
            bool saw_drop_by_cap = false;
            for (const int64_t v : reference) {
                saw_drop_by_vote = saw_drop_by_vote || v == 2003;
                saw_drop_by_cap = saw_drop_by_cap || v == 3001;
            }
            ASSERT_TRUE(saw_drop_by_vote && saw_drop_by_cap);                                 // (the scenario does run through a vote and through the cap)
        }
        // ---- Presence is whatever the state is, and always a message that the decoder takes: a random walk over every event ----
        {
            Lcg rng(44);
            for (const uint32_t start : {500000u, 0xFFFFF000u}) {
                Attendance::Config cfg;
                cfg.max_pause_ms = 300000;
                Attendance a(cfg);
                a.seat_humans(0b1111, start);
                uint32_t now = start;
                std::vector<bool> was_dropped(4, false);
                uint32_t last_pause = 0;
                size_t valid_votes = 0;
                size_t with_vote = 0;
                size_t with_missing = 0;
                size_t vote_drops = 0;
                size_t cap_drops = 0;
                for (int step = 0; step < 8000; ++step) {
                    now += rng.below(10) == 0 ? 40000u : rng.below(3000);
                    const uint8_t seat = static_cast<uint8_t>(rng.below(4));
                    switch (rng.below(10)) {
                        case 0: a.lost(seat, now); break;
                        case 1: a.returning(seat, now); break;
                        case 2: a.progress(seat, static_cast<uint8_t>(rng.below(120)), now); break;
                        case 3: a.caught_up(seat, now); break;
                        case 4: a.catch_up_failed(seat, now); break;
                        case 5: if (rng.below(8) == 0) a.dropped(seat, now); break;
                        case 6:
                        case 7: valid_votes += a.vote(seat, rng.below(2) == 0 ? a.vote_subject(now) : static_cast<uint8_t>(rng.below(4)), rng.below(3) != 0, now) ? 1u : 0u; break;
                        default: a.update(now); break;
                    }
                    for (uint8_t s = 0; s < 4; ++s) {
                        ASSERT_FALSE(was_dropped[s] && a.state(s) != S::Dropped);                                       // a seat that is dropped stays so ...
                        was_dropped[s] = a.state(s) == S::Dropped;
                    }
                    ASSERT_TRUE(a.pause_ms(now) >= last_pause);                                                          // ... and the pause never runs back
                    last_pause = a.pause_ms(now);
                    bool everybody_gone = true;
                    for (uint8_t s = 0; s < 4; ++s) everybody_gone = everybody_gone && a.state(s) == S::Dropped;
                    if (everybody_gone) {                                                                                // (a new match begins, with the same attendance)
                        vote_drops += a.drops_by_vote();
                        cap_drops += a.drops_by_cap();
                        a.seat_humans(0b1111, now);
                        was_dropped.assign(4, false);
                        last_pause = 0;
                    }
                    for (const uint8_t viewer : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{255}}) {
                        const PresenceMsg m = a.presence_for(viewer, now);
                        const std::vector<uint8_t> bytes = encode(m);
                        PresenceMsg back;
                        ASSERT_TRUE(decode(bytes, back));                                                                // a Presence of the real state is always one that the decoder takes ...
                        ASSERT_TRUE(encode(back) == bytes);
                        ASSERT_EQ(back.missing.empty(), !a.paused());                                                    // ... and it says what the state says
                        ASSERT_EQ(back.voters, a.connected_humans());
                        ASSERT_EQ(back.cap_s, a.cap_s(now));
                        ASSERT_EQ(back.vote_seat, a.vote_subject(now));
                        if (back.vote_seat != 255) {
                            ASSERT_EQ(back.votes_continue, a.votes_for_continue(back.vote_seat));
                            ASSERT_TRUE((a.state(back.vote_seat) == S::Absent && a.away_ms(back.vote_seat, now) >= cfg.vote_after_ms) || a.flapping(back.vote_seat, now));      // (away long enough, or a seat that flaps)
                            ASSERT_TRUE(viewer == 255 ? back.your_vote == 0 : (a.state(viewer) == S::Present || back.your_vote == 0));
                        }
                        for (const auto& e : back.missing) {
                            ASSERT_TRUE(a.state(e.seat) == S::Absent || a.state(e.seat) == S::CatchingUp);
                            ASSERT_EQ(e.waited_s, std::min<uint32_t>(a.away_ms(e.seat, now) / 1000u, 0xFFFFu));
                            ASSERT_EQ(e.progress, a.percent(e.seat));
                        }
                    }
                    const PresenceMsg m = a.presence_for(0, now);
                    with_vote += m.vote_seat != 255 ? 1u : 0u;
                    with_missing += m.missing.empty() ? 0u : 1u;
                }
                vote_drops += a.drops_by_vote();
                cap_drops += a.drops_by_cap();
                ASSERT_TRUE(valid_votes > 40 && with_vote > 1500 && with_missing > 3000 && vote_drops > 3 && cap_drops > 20);      // (the walk does reach votes, pauses and drops of both kinds)
            }
        }
    } TEST_END();

    TEST_CASE("N2.45 Lock-Step Runner: fast_forward Runs Queued Turns At Once Without Any Hook, In The Order Of update(): The Same State After Every Turn As The Paced Runner Over 600 Turns Of The Four-Player Match, In Slices Of Every Kind; Nothing Queued Is Nothing Done; Paced Play Goes On From There") {
        const uint32_t seed = 1;
        sim::SimulationEngine proto;
        const Ids ids = build_world(proto, seed);
        // 600 turns of the four-player match: the scripted orders of every player, and three turns that only the right ORDER can survive: an invitation and its acceptance in one turn
        // (the acceptance needs the invitation: canonical order is by issuer), a Hatch of every player, and the drop-out of seat 3
        std::vector<TurnMsg> turns(600);
        for (uint32_t k = 0; k < 600; ++k) {
            turns[k].turn = k;
            for (uint8_t p = 0; p < 4; ++p) {
                Command c;
                if (script(ids, seed, k * kTurnMs, p, c)) turns[k].commands.push_back(c);
            }
        }
        turns[100].commands.push_back(cmd(CommandType::AllianceAccept, 2, 1));
        turns[100].commands.push_back(cmd(CommandType::AllianceInvite, 1, 2));
        for (uint8_t p = 0; p < 4; ++p) turns[150].commands.push_back(cmd(CommandType::Hatch, p));
        turns[400].commands.push_back(cmd(CommandType::Drop, 3));
        for (TurnMsg& t : turns) sim::canonical_order(t.commands);
        size_t commands_in_all = 0;
        for (const TurnMsg& t : turns) commands_in_all += t.commands.size();
        ASSERT_TRUE(commands_in_all > 300);
        // the paced runner's state after every turn (one turn runs per update, a turn behind the one that was fed)
        std::vector<sim::StateHash> after(600);
        {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            size_t done = 0;
            for (uint32_t k = 0; k < 640; ++k) {
                if (k < 600) ASSERT_TRUE(runner.on_turn(turns[k]));
                for (const LockstepRunner::Executed& e : runner.update(kTurnMs)) {
                    ASSERT_EQ(e.turn, done);
                    after[done++] = sim.state_hash();
                }
            }
            ASSERT_EQ(done, size_t{600});
        }
        for (size_t k = 1; k < 600; ++k) ASSERT_TRUE(after[k] != after[k - 1]);       // (the match changes every turn: a tick that was skipped shows)
        {   // the scenario is order sensitive: turn 100 applied backwards gives another state (so the equality below proves the order, and a runner that applied it so would be caught)
            sim::SimulationEngine good;
            sim::SimulationEngine bad;
            build_world(good, seed);
            build_world(bad, seed);
            for (uint32_t k = 0; k < 100; ++k) {
                for (const Command& c : turns[k].commands) { good.apply_command(c); bad.apply_command(c); }
                good.tick();
                bad.tick();
            }
            ASSERT_TRUE(good.state_hash() == bad.state_hash());
            for (const Command& c : turns[100].commands) good.apply_command(c);
            for (auto it = turns[100].commands.rbegin(); it != turns[100].commands.rend(); ++it) bad.apply_command(*it);
            ASSERT_TRUE(good.state_hash() != bad.state_hash());
        }
        // ---- one turn at a time: the state after each is the paced runner's ----
        {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            size_t hooks = 0;
            runner.set_on_tick([&]() { ++hooks; });
            runner.set_on_command([&](const Command&, const sim::CommandResult&) { ++hooks; });
            runner.set_on_applied([&](const Command&) { ++hooks; });
            for (uint32_t k = 0; k < 600; ++k) {
                ASSERT_TRUE(runner.on_catch_up_turn(turns[k]));
                ASSERT_EQ(runner.queued(), size_t{1});
                ASSERT_EQ(runner.fast_forward(1), 1u);
                ASSERT_TRUE(sim.state_hash() == after[k]);                              // the same state after EVERY turn
                ASSERT_EQ(runner.next_turn_to_execute(), k + 1);
                ASSERT_EQ(runner.next_turn_expected(), k + 1);
                ASSERT_EQ(sim.current_tick(), uint64_t{k} + 1);
                ASSERT_TRUE(runner.at_boundary());
            }
            ASSERT_EQ(hooks, size_t{0});                                                // ... and no hook was called: nothing was drawn, no sound, no news
            ASSERT_EQ(runner.queued(), size_t{0});
        }
        // ---- slices of every kind: the state at each boundary of a slice is the paced runner's after that many turns ----
        for (const uint32_t pattern : {0u, 1u, 2u, 3u, 4u}) {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            size_t hooks = 0;
            runner.set_on_tick([&]() { ++hooks; });
            runner.set_on_command([&](const Command&, const sim::CommandResult&) { ++hooks; });
            runner.set_on_applied([&](const Command&) { ++hooks; });
            Lcg rng(100 + pattern);
            uint32_t fed = 0;
            uint32_t executed = 0;
            size_t calls = 0;
            while (executed < 600) {
                // turns come in bunches of the size of a batch (or of one), while the slices are of whatever size the frame had time for
                const uint32_t bunch = pattern == 0 ? 1u : (pattern == 1 ? 600u : 1u + rng.below(300));
                for (uint32_t i = 0; i < bunch && fed < 600; ++i) ASSERT_TRUE(runner.on_catch_up_turn(turns[fed++]));
                while (runner.queued() > 0) {
                    const uint32_t slice = pattern == 3 ? 7u : (pattern == 4 ? 1u + rng.below(100) : (pattern == 0 ? 1u : 64u));
                    const uint32_t ran = runner.fast_forward(slice);
                    ASSERT_TRUE(ran >= 1 && ran <= slice);
                    executed += ran;
                    ++calls;
                    ASSERT_EQ(runner.next_turn_to_execute(), executed);
                    ASSERT_TRUE(sim.state_hash() == after[executed - 1]);
                }
            }
            ASSERT_EQ(executed, 600u);
            ASSERT_EQ(hooks, size_t{0});
            ASSERT_TRUE(calls >= 10);
            ASSERT_EQ(sim.current_tick(), uint64_t{600});
        }
        // ---- max_ticks is respected, 0 does nothing, an empty queue is nothing done (and the runner is left as it was) ----
        {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            ASSERT_EQ(runner.fast_forward(100), 0u);                                     // nothing queued
            ASSERT_EQ(runner.next_turn_to_execute(), 0u);
            ASSERT_EQ(sim.current_tick(), uint64_t{0});
            for (uint32_t k = 0; k < 10; ++k) ASSERT_TRUE(runner.on_catch_up_turn(turns[k]));
            ASSERT_EQ(runner.fast_forward(0), 0u);                                       // asked for no ticks
            ASSERT_TRUE(runner.queued() == 10 && sim.current_tick() == 0);
            ASSERT_EQ(runner.fast_forward(5), 5u);
            ASSERT_TRUE(runner.queued() == 5 && sim.current_tick() == 5 && runner.next_turn_to_execute() == 5);
            ASSERT_TRUE(sim.state_hash() == after[4]);
            ASSERT_EQ(runner.fast_forward(1000), 5u);                                    // what is there, no more
            ASSERT_TRUE(runner.queued() == 0 && sim.current_tick() == 10 && sim.state_hash() == after[9]);
            ASSERT_EQ(runner.fast_forward(1000), 0u);
            ASSERT_EQ(sim.current_tick(), uint64_t{10});
            // a runner that is in the middle of paced play and has nothing queued is not touched by a fast-forward of nothing: it is still started, with the buffer that it had
            sim::SimulationEngine live;
            build_world(live, seed);
            LockstepRunner paced(live);
            for (uint32_t k = 0; k < 30; ++k) {
                ASSERT_TRUE(paced.on_turn(turns[k]));
                paced.update(kTurnMs);
            }
            const uint32_t buffer = paced.buffer_turns();
            const uint32_t expected = paced.next_turn_expected();
            const uint32_t executed_before = paced.next_turn_to_execute();
            ASSERT_TRUE(paced.queued() >= 1);
            const size_t queued = paced.queued();
            ASSERT_EQ(paced.fast_forward(0), 0u);
            ASSERT_TRUE(paced.queued() == queued && paced.buffer_turns() == buffer && paced.next_turn_expected() == expected && paced.next_turn_to_execute() == executed_before);
            paced.update(kTurnMs);                                                       // the last turn that was fed runs: the queue is empty, the runner is in paced play
            ASSERT_EQ(paced.queued(), size_t{0});
            const bool rebuilding = paced.rebuilding();
            const uint32_t stalled_for = paced.stalled_ms();
            const uint32_t at_turn = paced.next_turn_to_execute();
            ASSERT_EQ(paced.fast_forward(10), 0u);                                       // nothing queued is nothing done: not even the pacing is touched
            ASSERT_TRUE(paced.buffer_turns() == buffer && paced.rebuilding() == rebuilding && paced.stalled_ms() == stalled_for && paced.next_turn_to_execute() == at_turn);
            ASSERT_EQ(live.current_tick(), uint64_t{at_turn});
        }
        // ---- the numbering and the order of arrival: catch-up turns and live turns are one sequence, with the same refusals ----
        {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            ASSERT_FALSE(runner.on_catch_up_turn(turns[1]));                              // must start at 0
            ASSERT_TRUE(runner.on_catch_up_turn(turns[0]));
            ASSERT_FALSE(runner.on_catch_up_turn(turns[0]));                              // no repeat
            ASSERT_FALSE(runner.on_catch_up_turn(turns[2]));                              // no gap
            ASSERT_TRUE(runner.on_turn(turns[1]));                                        // a live turn follows a catch-up turn ...
            ASSERT_TRUE(runner.on_catch_up_turn(turns[2]));                               // ... and the other way round
            ASSERT_FALSE(runner.on_turn(turns[2]));
            TurnMsg many;
            many.turn = 3;
            for (size_t i = 0; i < kMaxTurnCommands + 1; ++i) many.commands.push_back(cmd(CommandType::Hatch, 0));
            ASSERT_FALSE(runner.on_catch_up_turn(many));                                  // more commands than a turn may hold
            ASSERT_FALSE(runner.on_turn(many));
            ASSERT_EQ(runner.next_turn_expected(), 3u);
            ASSERT_TRUE(runner.logged_turn(0) != nullptr && runner.logged_turn(2) != nullptr && runner.logged_turn(2)->commands == turns[2].commands);      // (a machine that caught up can serve what it was given)
            ASSERT_EQ(runner.fast_forward(100), 3u);
            ASSERT_TRUE(sim.state_hash() == after[2]);
        }
        // ---- paced play goes on from a fast-forward: the next live turns are collected (target + 1 of them), then run in order, with every hook, to the paced runner's final state ----
        {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            size_t ticks = 0;
            size_t applied = 0;
            size_t verdicts = 0;
            bool boundary_inside_a_hook = false;
            runner.set_on_tick([&]() { ++ticks; boundary_inside_a_hook = boundary_inside_a_hook || runner.at_boundary(); });
            runner.set_on_command([&](const Command&, const sim::CommandResult&) { ++verdicts; boundary_inside_a_hook = boundary_inside_a_hook || runner.at_boundary(); });
            runner.set_on_applied([&](const Command&) { ++applied; boundary_inside_a_hook = boundary_inside_a_hook || runner.at_boundary(); });
            for (uint32_t k = 0; k < 300; ++k) ASSERT_TRUE(runner.on_catch_up_turn(turns[k]));
            ASSERT_EQ(runner.fast_forward(300), 300u);
            ASSERT_TRUE(ticks == 0 && applied == 0 && verdicts == 0);
            ASSERT_TRUE(sim.state_hash() == after[299]);
            ASSERT_FALSE(runner.stalled());                                               // (no "waiting for the other players": the runner is not waiting, it has not started)
            ASSERT_FALSE(runner.rebuilding());
            ASSERT_EQ(runner.buffer_turns(), 1u);
            ASSERT_TRUE(runner.update(50).empty());                                       // nothing is queued: nothing runs, and nothing is a stall
            ASSERT_FALSE(runner.stalled());
            ASSERT_TRUE(runner.on_turn(turns[300]));
            ASSERT_TRUE(runner.update(50).empty());                                       // one live turn is not enough: the buffer (target 1 + the turn that runs) is collected first
            ASSERT_TRUE(runner.on_turn(turns[301]));
            std::vector<LockstepRunner::Executed> ran = runner.update(0);
            ASSERT_TRUE(ran.size() == 1 && ran[0].turn == 300);                           // ... and the first live turn runs at once, numbered as it is
            ASSERT_TRUE(sim.state_hash() == after[300]);
            size_t commands_applied = 0;
            for (uint32_t k = 301; k < 600; ++k) {
                if (k + 1 < 600) ASSERT_TRUE(runner.on_turn(turns[k + 1]));
                ran = runner.update(kTurnMs);
                ASSERT_TRUE(ran.size() == 1 && ran[0].turn == k);
                ASSERT_TRUE(sim.state_hash() == after[k]);
            }
            ran = runner.update(kTurnMs);
            ASSERT_TRUE(ran.empty() && runner.next_turn_to_execute() == 600);
            for (uint32_t k = 300; k < 600; ++k) commands_applied += turns[k].commands.size();
            ASSERT_EQ(ticks, size_t{300});                                                // the hooks are called for the live turns, one tick each ...
            ASSERT_TRUE(applied == commands_applied && verdicts == commands_applied);     // ... and for every command of them
            ASSERT_FALSE(boundary_inside_a_hook);                                         // (inside a hook a turn is half done: not at a boundary)
            ASSERT_TRUE(runner.at_boundary());
            ASSERT_TRUE(sim.state_hash() == after[599]);
        }
    } TEST_END();

    TEST_CASE("N2.46 Lock-Step Runner: The Catch-Up Tells The Jitter Buffer Nothing, And Leaves The Runner As At The Start Of A Match (The Buffer At Its Steady Value, No Turn Read, The Next Live Turns Collected First): A Link That Was Rough Before The Pause Does Not Make The Buffer Grow After It") {
        // a turn's commands are not what this is about: empty turns, and a world that nothing happens in
        auto empty_turn = [](uint32_t n) {
            TurnMsg t;
            t.turn = n;
            return t;
        };
        // drives live turns into a runner: turn k (k0 .. k0 + count - 1) is sealed at base + (k - k0) * 50 ms and arrives `delay(k)` later (never before an earlier turn: the link is ordered); every
        // 10 ms the runner has its frame. `buffer_max` is the largest buffer that was asked for.
        struct Drive {
            uint32_t buffer_max{0};
            uint32_t buffer_at_end{0};
            size_t executed{0};
        };
        auto drive = [&](LockstepRunner& runner, uint32_t k0, uint32_t count, const std::function<uint32_t(uint32_t)>& delay) {
            Drive out;
            std::vector<uint32_t> arrival(count);
            uint32_t last = 0;
            for (uint32_t i = 0; i < count; ++i) {
                last = std::max(last, i * kTurnMs + delay(i));
                arrival[i] = last;
            }
            uint32_t next = 0;
            for (uint32_t now = 10; now <= (count + 30) * kTurnMs; now += 10) {
                while (next < count && arrival[next] <= now) {
                    runner.on_turn(empty_turn(k0 + next));
                    ++next;
                }
                out.executed += runner.update(10).size();
                out.buffer_max = std::max(out.buffer_max, runner.buffer_turns());
            }
            out.buffer_at_end = runner.buffer_turns();
            return out;
        };
        // ---- the catch-up's turns are not arrivals: even when the runner is driven in between, the jitter buffer reads nothing from them ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            ASSERT_TRUE(runner.update(10).empty());                                       // (the first update of a runner reads what waited for it and counts none of it: so one frame goes by first)
            for (uint32_t k = 0; k < 1000; ++k) ASSERT_TRUE(runner.on_catch_up_turn(empty_turn(k)));
            runner.update(50);                                                            // a frame that finds a thousand turns that came all at once: for live turns that is a bunch of 1000 and wants the largest buffer
            ASSERT_EQ(runner.jitter().lateness_ms(), 0u);
            ASSERT_EQ(runner.buffer_turns(), 1u);
            ASSERT_EQ(runner.jitter().wanted(), 1u);
            // the same turns as live ones: the buffer reads them (the control: this is what the catch-up must not do)
            sim::SimulationEngine other;
            build_world(other, 1);
            LockstepRunner live(other);
            ASSERT_TRUE(live.update(10).empty());
            for (uint32_t k = 0; k < 1000; ++k) ASSERT_TRUE(live.on_turn(empty_turn(k)));
            live.update(50);
            ASSERT_TRUE(live.jitter().lateness_ms() > 1000 && live.buffer_turns() == 4);
        }
        // ---- a long catch-up (36,000 turns, in the batches that the server sends) and then the live match: nothing was learned, nothing is wrong ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1, 2 * 3600 * 1000);                                         // (a match of two hours: the engine ends a match of 12 minutes, which has 14,400 ticks)
            LockstepRunner runner(sim);
            uint32_t next = 0;
            while (next < 36000) {
                for (uint32_t i = 0; i < 4096 && next < 36000; ++i) ASSERT_TRUE(runner.on_catch_up_turn(empty_turn(next++)));
                while (runner.queued() > 0) runner.fast_forward(500);
            }
            ASSERT_EQ(runner.next_turn_to_execute(), 36000u);
            ASSERT_EQ(sim.current_tick(), uint64_t{36000});
            ASSERT_EQ(runner.jitter().lateness_ms(), 0u);
            ASSERT_EQ(runner.buffer_turns(), 1u);
            ASSERT_FALSE(runner.stalled());
            const Drive live = drive(runner, 36000, 400, [](uint32_t) { return 0u; });      // the live match goes on over a steady link
            ASSERT_EQ(live.buffer_max, 1u);                                               // the buffer never asked for more than one turn
            ASSERT_EQ(live.buffer_at_end, 1u);
            ASSERT_EQ(live.executed, size_t{400});                                        // every turn ran (the last one when its tick was due)
            ASSERT_EQ(runner.next_turn_to_execute(), 36000u + 400u);
        }
        // ---- a link that was rough before the pause: the buffer had grown; the pause, the catch-up and a steady link afterwards leave it at its steady value ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            Lcg rng(46);
            const Drive rough = drive(runner, 0, 600, [&](uint32_t) { return rng.below(130); });   // 0 - 130 ms of jitter: a buffer of three or four turns
            ASSERT_TRUE(rough.buffer_max >= 3);
            const uint32_t learned = runner.buffer_turns();
            ASSERT_TRUE(learned >= 2);
            const uint32_t executed_before = runner.next_turn_to_execute();
            for (int i = 0; i < 100; ++i) runner.update(50);                              // the pause: 5 s with no turn at all (the connection was lost and the match waits)
            ASSERT_TRUE(runner.stalled());
            const uint32_t next = runner.next_turn_expected();
            ASSERT_TRUE(runner.on_turn(empty_turn(next)));                                // (a live turn that came in the frame that the catch-up began in: nobody has read it yet)
            for (uint32_t k = next + 1; k < next + 300; ++k) ASSERT_TRUE(runner.on_catch_up_turn(empty_turn(k)));      // the turns that this machine missed come as a catch-up
            ASSERT_EQ(runner.fast_forward(1000), 300u);
            ASSERT_EQ(runner.next_turn_to_execute(), next + 300);
            ASSERT_TRUE(runner.next_turn_to_execute() > executed_before);
            ASSERT_EQ(runner.buffer_turns(), 1u);                                         // the buffer starts again from its steady value ...
            ASSERT_EQ(runner.jitter().lateness_ms(), 0u);                                 // ... with nothing read: what was read describes a link and a clock that are gone
            ASSERT_FALSE(runner.stalled());
            ASSERT_FALSE(runner.rebuilding());
            const Drive steady = drive(runner, next + 300, 400, [](uint32_t) { return 0u; });      // the match goes on, over a link that is steady now
            ASSERT_EQ(steady.buffer_max, 1u);                                             // the stale measurements did not come back, the clock that jumped did not look like a late link
            ASSERT_EQ(steady.buffer_at_end, 1u);
            ASSERT_EQ(steady.executed, size_t{400});
            // and a link that IS rough after the catch-up is learned afresh: it grows as it would at the start of a match
            const Drive rough_again = drive(runner, next + 300 + 400, 600, [&](uint32_t) { return rng.below(130); });
            ASSERT_TRUE(rough_again.buffer_max >= 3);
        }
        // ---- live turns that waited behind the catch-up (a window that was not drawn for a second after it) come in a bunch: that is not the link's lateness either ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            for (uint32_t k = 0; k < 300; ++k) ASSERT_TRUE(runner.on_catch_up_turn(empty_turn(k)));
            ASSERT_EQ(runner.fast_forward(1000), 300u);
            for (uint32_t k = 300; k < 340; ++k) ASSERT_TRUE(runner.on_turn(empty_turn(k)));      // two seconds of the live match, all there when the first frame comes
            runner.update(10);
            ASSERT_EQ(runner.jitter().lateness_ms(), 0u);                                 // read at once, but they were not late: they waited for the runner
            ASSERT_EQ(runner.buffer_turns(), 1u);
            for (int i = 0; i < 400 && runner.queued() > 0; ++i) runner.update(10);        // and the queue is run down at up to four times normal speed, as for any bunch
            ASSERT_EQ(runner.queued(), size_t{0});
            ASSERT_EQ(runner.next_turn_to_execute(), 340u);
            ASSERT_EQ(runner.buffer_turns(), 1u);
        }
        // ---- a runner that had waited a moment for a turn (80 ms: a stall that one more turn of buffer would have bridged) when a small catch-up came: the wait is over, so the live turns that come
        // after it do not grow the buffer as a late turn would ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            for (uint32_t k = 0; k < 60; ++k) {
                ASSERT_TRUE(runner.on_turn(empty_turn(k)));
                runner.update(kTurnMs);
            }
            runner.update(kTurnMs);                                                       // the last turn that was queued runs
            ASSERT_EQ(runner.queued(), size_t{0});
            for (int i = 0; i < 8; ++i) runner.update(10);                                // 80 ms with no turn although one is due
            ASSERT_TRUE(runner.stalled() && runner.stalled_ms() <= 100);
            const uint32_t next = runner.next_turn_expected();
            ASSERT_TRUE(runner.on_catch_up_turn(empty_turn(next)) && runner.on_catch_up_turn(empty_turn(next + 1)));
            ASSERT_EQ(runner.fast_forward(10), 2u);
            ASSERT_FALSE(runner.stalled());                                               // not waiting for anything now
            const Drive live = drive(runner, next + 2, 200, [](uint32_t) { return 0u; });
            ASSERT_EQ(live.buffer_max, 1u);                                               // (the end of the stall was never counted as a late turn)
            ASSERT_EQ(live.executed, size_t{200});
        }
        // ---- a fast-forward that runs nothing does not reset what the runner has learned ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            Lcg rng(47);
            drive(runner, 0, 600, [&](uint32_t) { return rng.below(130); });
            while (runner.queued() > 0) runner.update(50);                               // (the turns of the run that wait in the buffer run: nothing is queued now)
            const uint32_t learned = runner.buffer_turns();
            const uint32_t lateness = runner.jitter().lateness_ms();
            ASSERT_TRUE(learned >= 2 && lateness > 0);
            ASSERT_EQ(runner.fast_forward(100), 0u);                                      // nothing queued: nothing done, and what the runner has learned stays learned
            ASSERT_EQ(runner.fast_forward(100), 0u);
            ASSERT_TRUE(runner.buffer_turns() == learned && runner.jitter().lateness_ms() == lateness);
        }
    } TEST_END();
    // ---- the fixes of the review of release A: the budgets of a key holder, flapping, the resume countdown (N2.75 - N2.79) -------------------------------------------------------

    TEST_CASE("N2.75 Attendance: The Catch-Up Budget Of An Absence: A Hello Of An Attempt That Is In Progress Does Not Start Its Stall Clock, Percent Or Time Again; The Time Of All The Attempts Of One Absence Is Added Up; Used Up, The Catch-Up Fails At That Moment (The Seat Is Absent: The Vote And The Cap Apply) And A Hello Is Refused; A New Absence Has It All Again") {
        using S = Attendance::State;
        const uint32_t t0 = 400000;
        Attendance::Config cfg;
        cfg.max_catch_up_ms = 60000;
        cfg.max_pause_ms = 600000;
        cfg.rejoin_attempts = Attendance::kMaxRejoinAttempts;               // (this test is about time, not about the number of Hellos: N2.76)
        ASSERT_TRUE(Attendance::Config{}.max_catch_up_ms == 300000 && kMaxCatchUpMs == 300000);          // the default: five minutes
        {   // a Hello of the attempt that goes on changes nothing of it
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 1000));
            ASSERT_TRUE(a.progress(1, 10, t0 + 5000));
            ASSERT_TRUE(a.returning(1, t0 + 12000));                         // 12 s into the attempt it says Hello again
            ASSERT_EQ(a.state(1), S::CatchingUp);
            ASSERT_EQ(a.percent(1), 10);                                      // (it used to start at 0 again)
            ASSERT_EQ(a.catching_up_ms(1, t0 + 12000), 11000u);               // the attempt began at 1 s: its time goes on
            ASSERT_TRUE(a.returning(1, t0 + 24000));                          // and again: the stall clock is that of the progress at 5 s, whatever Hellos come
            ASSERT_TRUE(a.update(t0 + 24999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_TRUE(a.update(t0 + 25000).empty());
            ASSERT_EQ(a.state(1), S::Absent);                                 // 20 s after the last progress
            ASSERT_EQ(a.catching_up_ms(1, t0 + 25000), 24000u);               // the attempt took 24 s of the 60
        }
        {   // the attempts of one absence add up, and a seat that makes progress is let go at the budget all the same
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0));
            ASSERT_EQ(a.catching_up_ms(1, t0 + 100000), 0u);                  // an absent seat is not catching up
            ASSERT_TRUE(a.returning(1, t0 + 10000));                          // attempt 1: it never progresses, the stall ends it after 20 s
            ASSERT_TRUE(a.update(t0 + 30000).empty() && a.state(1) == S::Absent);
            ASSERT_EQ(a.catching_up_ms(1, t0 + 31000), 20000u);
            ASSERT_TRUE(a.returning(1, t0 + 40000));                          // attempt 2: it progresses every 10 s, so the stall never ends it
            for (uint32_t k = 1; k <= 3; ++k) ASSERT_TRUE(a.progress(1, static_cast<uint8_t>(k * 10), t0 + 40000 + k * 10000));     // 50, 60 and 70 s
            ASSERT_EQ(a.catching_up_ms(1, t0 + 79999), 20000u + 39999u);
            ASSERT_TRUE(a.update(t0 + 79999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_EQ(a.rejoin_check(1, t0 + 79999), Attendance::Rejoin::Allowed);          // 1 ms is left
            ASSERT_EQ(a.rejoin_check(1, t0 + 80000), Attendance::Rejoin::CatchUpSpent);     // 60 s: none
            ASSERT_TRUE(a.update(t0 + 80000).empty());                        // let go at the budget, 10 s after its last progress
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_EQ(a.catch_up_expired(), 1u);
            ASSERT_EQ(a.percent(1), 0);
            ASSERT_EQ(a.rejoins_refused(), 0u);
            ASSERT_FALSE(a.returning(1, t0 + 81000));                         // a Hello is refused, nothing changes, it is counted
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_EQ(a.rejoins_refused(), 1u);
            ASSERT_EQ(a.catching_up_ms(1, t0 + 500000), 60000u);              // (it stays what it was: an absent seat does not catch up)
            ASSERT_FALSE(a.returning(1, t0 + 500000));
            ASSERT_EQ(a.catch_up_expired(), 1u);                              // (and a refusal is not an expiry)
            ASSERT_EQ(a.vote_subject(t0 + 81000), 1);                         // the vote opens: it has been away 81 s, and nothing keeps it catching up
            ASSERT_TRUE(a.vote(0, 1, true, t0 + 81000) && a.vote(2, 1, true, t0 + 81000));
            ASSERT_EQ(a.update(t0 + 81000).size(), 1u);
            ASSERT_EQ(a.drops_by_vote(), 1u);
        }
        {   // the cap applies to a seat whose budget is spent (no vote is cast: the cap, 120 s, takes it)
            Attendance::Config cap = cfg;
            cap.max_pause_ms = 120000;
            Attendance a(cap);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 1000));
            for (uint32_t k = 1; k <= 5; ++k) ASSERT_TRUE(a.progress(1, static_cast<uint8_t>(k), t0 + 1000 + k * 10000));
            ASSERT_TRUE(a.update(t0 + 60999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_TRUE(a.update(t0 + 61000).empty() && a.state(1) == S::Absent);
            ASSERT_FALSE(a.returning(1, t0 + 62000));
            ASSERT_TRUE(a.update(t0 + 119999).empty());
            const std::vector<uint8_t> dropped = a.update(t0 + 120000);
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1 && a.drops_by_cap() == 1u);
        }
        {   // the budget is of one absence: catching up ends it (the next absence has all of it), and so does a seat that was Present (a second window)
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 1000));
            ASSERT_TRUE(a.progress(1, 50, t0 + 20000) && a.progress(1, 60, t0 + 40000));
            ASSERT_TRUE(a.update(t0 + 50000).empty() && a.caught_up(1, t0 + 50000));      // 49 s of the 60 were spent
            ASSERT_EQ(a.catching_up_ms(1, t0 + 50000), 0u);
            ASSERT_TRUE(a.lost(1, t0 + 100000) && a.returning(1, t0 + 101000));
            ASSERT_EQ(a.catching_up_ms(1, t0 + 130000), 29000u);
            ASSERT_TRUE(a.progress(1, 5, t0 + 110000) && a.progress(1, 6, t0 + 120000) && a.progress(1, 7, t0 + 130000));
            ASSERT_TRUE(a.update(t0 + 130000).empty() && a.state(1) == S::CatchingUp);    // (49 s would have been 20 s over)
            ASSERT_TRUE(a.caught_up(1, t0 + 131000));
            // a window that takes over a seat whose old link was alive starts an absence with all of the budget
            ASSERT_TRUE(a.returning(1, t0 + 200000));
            ASSERT_EQ(a.catching_up_ms(1, t0 + 210000), 10000u);
        }
        {   // an attempt that failed and one that got through: the time of the first is given back with the seat (caught up), and what the next absence may spend is all of it again
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 1000));
            ASSERT_TRUE(a.update(t0 + 21000).empty() && a.state(1) == S::Absent);        // 20 s without progress
            ASSERT_EQ(a.catching_up_ms(1, t0 + 25000), 20000u);
            ASSERT_TRUE(a.returning(1, t0 + 30000) && a.progress(1, 90, t0 + 35000) && a.caught_up(1, t0 + 36000));
            ASSERT_EQ(a.catching_up_ms(1, t0 + 36000), 0u);                               // back: nothing of the absence is held against the seat
            ASSERT_TRUE(a.lost(1, t0 + 100000) && a.returning(1, t0 + 101000));
            ASSERT_EQ(a.catching_up_ms(1, t0 + 130000), 29000u);                          // (the next absence counts from 0, not from the 26 s of the last)
            ASSERT_EQ(a.rejoin_check(1, t0 + 130000), Attendance::Rejoin::Allowed);
        }
    } TEST_END();

    TEST_CASE("N2.76 Attendance: The Rejoin Budget Of A Seat: At Most Three Hellos Are Accepted In A Minute (The Window Slides To The Millisecond), And At Most Three Times The Log's Size Is Streamed In Ten Minutes, At Least 1 MiB; A Refused Hello Changes Nothing, Counts Nothing Against The Seat And Is Counted; The Streams Are Charged As They Are Sent") {
        using S = Attendance::State;
        const uint32_t t0 = 900000;
        {   // three Hellos a minute
            Attendance a;
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(Attendance::Config{}.rejoin_attempts == 3 && Attendance::Config{}.rejoin_window_ms == 60000 && kRejoinAttempts == 3 && kRejoinWindowMs == 60000);
            ASSERT_TRUE(a.lost(1, t0));
            ASSERT_TRUE(a.returning(1, t0 + 1000));                                       // 1
            ASSERT_TRUE(a.returning(1, t0 + 11000));                                      // 2
            ASSERT_TRUE(a.returning(1, t0 + 21000));                                      // 3
            ASSERT_EQ(a.rejoin_check(1, t0 + 31000), Attendance::Rejoin::TooManyAttempts);
            const uint8_t percent = a.percent(1);
            ASSERT_TRUE(a.progress(1, 7, t0 + 31000));
            ASSERT_FALSE(a.returning(1, t0 + 31000));                                     // the fourth is refused: nothing changes
            ASSERT_TRUE(a.state(1) == S::CatchingUp && a.percent(1) == 7 && percent == 0);
            ASSERT_EQ(a.rejoins_refused(), 1u);
            ASSERT_FALSE(a.returning(1, t0 + 45000));                                     // (and a refused Hello does not count: the window is that of the three that were accepted)
            ASSERT_FALSE(a.returning(1, t0 + 60999));                                     // the first one is 59,999 ms old: it still counts
            ASSERT_EQ(a.rejoins_refused(), 3u);
            ASSERT_TRUE(a.returning(1, t0 + 61000));                                      // 60,000 ms: it is out of the minute
            ASSERT_EQ(a.rejoin_check(1, t0 + 61000), Attendance::Rejoin::TooManyAttempts);
            ASSERT_EQ(a.rejoin_check(1, t0 + 71000), Attendance::Rejoin::Allowed);        // (the second leaves at 71 s)
            ASSERT_EQ(a.rejoins_refused(), 3u);
            // a seat that is dropped, or no seat, is not "refused": nothing is held for it
            ASSERT_TRUE(a.dropped(1, t0 + 62000));
            ASSERT_FALSE(a.returning(1, t0 + 300000));
            ASSERT_EQ(a.rejoin_check(1, t0 + 300000), Attendance::Rejoin::NotHeld);
            ASSERT_EQ(a.rejoin_check(3, t0), Attendance::Rejoin::NotHeld);               // (seat 3 is no seat of this match)
            ASSERT_EQ(a.rejoins_refused(), 3u);
            // each seat has its own budget
            ASSERT_TRUE(a.lost(0, t0 + 65000) && a.returning(0, t0 + 65100));
        }
        {   // the configured number: one a minute, eight a minute (the most), and a request for more or for none is clamped
            Attendance::Config one;
            one.rejoin_attempts = 1;
            Attendance a(one);
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 100));
            ASSERT_FALSE(a.returning(1, t0 + 200));
            ASSERT_TRUE(a.returning(1, t0 + 60100));
            Attendance::Config lots;
            lots.rejoin_attempts = 1000;
            ASSERT_EQ(Attendance(lots).config().rejoin_attempts, Attendance::kMaxRejoinAttempts);
            lots.rejoin_attempts = 0;
            ASSERT_EQ(Attendance(lots).config().rejoin_attempts, 1u);
            Attendance::Config flap;
            flap.flap_losses = 1;
            ASSERT_EQ(Attendance(flap).config().flap_losses, 2u);
            flap.flap_losses = 99;
            ASSERT_EQ(Attendance(flap).config().flap_losses, Attendance::kMaxFlapLosses);
        }
        {   // bytes: three times the log's size in ten minutes (here with no floor)
            Attendance::Config cfg;
            cfg.stream_floor_bytes = 0;
            cfg.rejoin_attempts = Attendance::kMaxRejoinAttempts;
            cfg.max_pause_ms = 3600000;
            cfg.max_catch_up_ms = 3600000;
            Attendance a(cfg);
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.lost(1, t0));
            ASSERT_EQ(a.streamed_bytes(), uint64_t{0});
            const size_t log = 1000;
            const size_t planned = 900;                                                    // (the packed turns are less than the log: the index is not streamed)
            ASSERT_TRUE(a.returning(1, t0 + 1000, planned, log));
            a.charge_stream(1, t0 + 1100, 500);                                            // the stream is charged as it is sent
            a.charge_stream(1, t0 + 1200, 400);
            ASSERT_EQ(a.streamed_bytes(), uint64_t{900});
            ASSERT_TRUE(a.catch_up_failed(1, t0 + 2000) && a.returning(1, t0 + 100000, planned, log));        // the second: 900 + 900
            a.charge_stream(1, t0 + 100100, 900);
            ASSERT_TRUE(a.catch_up_failed(1, t0 + 101000) && a.returning(1, t0 + 200000, planned, log));      // the third: 1800 + 900 = 2700 of 3000
            a.charge_stream(1, t0 + 200100, 900);
            ASSERT_TRUE(a.catch_up_failed(1, t0 + 201000));
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000, planned, log), Attendance::Rejoin::TooMuchStreamed);     // the fourth: 2700 + 900 = 3600 of 3000
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000, 300, log), Attendance::Rejoin::Allowed);                 // a smaller one still fits: 3000 of 3000 ...
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000, 301, log), Attendance::Rejoin::TooMuchStreamed);         // ... and not a byte more
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000), Attendance::Rejoin::Allowed);                           // (no size given: the bytes are not asked)
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000, planned, 0), Attendance::Rejoin::Allowed);
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000, 0, log), Attendance::Rejoin::Allowed);
            ASSERT_FALSE(a.returning(1, t0 + 250000, planned, log));
            ASSERT_EQ(a.rejoins_refused(), 1u);
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_EQ(a.streamed_bytes(), uint64_t{2700});                                                    // (a refusal streams nothing)
            // the window of ten minutes slides: the first stream (at 1.1 - 1.2 s) is out of it at 601.1 s
            ASSERT_EQ(a.rejoin_check(1, t0 + 601099, planned, log), Attendance::Rejoin::TooMuchStreamed);
            ASSERT_EQ(a.rejoin_check(1, t0 + 601100, planned, log), Attendance::Rejoin::Allowed);             // 1800 + 900
            ASSERT_EQ(a.rejoin_check(1, t0 + 700099, planned, log), Attendance::Rejoin::Allowed);
            ASSERT_EQ(a.rejoin_check(1, t0 + 700100, planned, log), Attendance::Rejoin::Allowed);
            ASSERT_TRUE(a.returning(1, t0 + 700100, planned, log));
            ASSERT_EQ(a.streamed_bytes(), uint64_t{2700});
        }
        {   // the floor: a log of a few KB is no reason to refuse a fifth reload; the budget is the larger of three logs and 1 MiB
            Attendance::Config cfg;
            cfg.rejoin_attempts = Attendance::kMaxRejoinAttempts;
            cfg.max_catch_up_ms = 3600000;
            cfg.max_pause_ms = 3600000;
            ASSERT_TRUE(cfg.stream_factor == 3 && cfg.stream_floor_bytes == (size_t{1} << 20) && cfg.stream_window_ms == 600000 && kStreamFactor == 3 && kStreamWindowMs == 600000);
            Attendance a(cfg);
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.lost(1, t0));
            uint32_t at = t0 + 1000;
            for (int i = 0; i < 10; ++i) {                                                 // ten streams of 100 KB of a log of 100 KB: 1 MB of the 1 MiB
                ASSERT_TRUE(a.returning(1, at, 100000, 100000));
                a.charge_stream(1, at + 10, 100000);
                ASSERT_TRUE(a.catch_up_failed(1, at + 20));
                at += 8000;                                                                // (eight Hellos a minute are allowed here: this spacing is within it)
            }
            ASSERT_EQ(a.rejoin_check(1, at, 100000, 100000), Attendance::Rejoin::TooMuchStreamed);   // the eleventh: 1.1 MB of 1,048,576
            ASSERT_EQ(a.rejoin_check(1, at, 48576, 100000), Attendance::Rejoin::Allowed);
            ASSERT_EQ(a.rejoin_check(1, at, 48577, 100000), Attendance::Rejoin::TooMuchStreamed);
            // a log of 2 MB: three times that
            Attendance b(cfg);
            b.seat_humans(0b0011, t0);
            ASSERT_TRUE(b.lost(1, t0));
            const size_t big = 2u << 20;
            for (int i = 0; i < 3; ++i) {
                ASSERT_TRUE(b.returning(1, t0 + 1000 + 20000u * static_cast<uint32_t>(i), big - 100, big));
                b.charge_stream(1, t0 + 1100 + 20000u * static_cast<uint32_t>(i), big - 100);
                ASSERT_TRUE(b.catch_up_failed(1, t0 + 1200 + 20000u * static_cast<uint32_t>(i)));
            }
            ASSERT_EQ(b.rejoin_check(1, t0 + 61000, big - 100, big), Attendance::Rejoin::TooMuchStreamed);
        }
        {   // the same sequence from other points of the 32-bit clock gives the same answers (the windows are differences)
            for (const uint32_t start : {0u, 0xFFFFFF00u, 0xFFFF0000u, 0x80000000u - 500u}) {
                Attendance::Config cfg;
                cfg.stream_floor_bytes = 0;
                cfg.max_catch_up_ms = 3600000;                                               // (the attempt is left open for ten minutes: not the subject here)
                cfg.max_pause_ms = 3600000;
                Attendance a(cfg);
                a.seat_humans(0b0011, start);
                ASSERT_TRUE(a.lost(1, start));
                ASSERT_TRUE(a.returning(1, start + 1000, 900, 1000));
                a.charge_stream(1, start + 1100, 2800);
                ASSERT_TRUE(a.returning(1, start + 2000));
                ASSERT_TRUE(a.returning(1, start + 3000));
                ASSERT_EQ(a.rejoin_check(1, start + 4000), Attendance::Rejoin::TooManyAttempts);
                ASSERT_EQ(a.rejoin_check(1, start + 61000 - 1), Attendance::Rejoin::TooManyAttempts);
                ASSERT_EQ(a.rejoin_check(1, start + 61000), Attendance::Rejoin::Allowed);
                ASSERT_EQ(a.rejoin_check(1, start + 61000, 900, 1000), Attendance::Rejoin::TooMuchStreamed);     // 2800 + 900 > 3000
                ASSERT_EQ(a.rejoin_check(1, start + 601100 - 1, 900, 1000), Attendance::Rejoin::TooMuchStreamed);
                ASSERT_EQ(a.rejoin_check(1, start + 601100, 900, 1000), Attendance::Rejoin::Allowed);
            }
        }
    } TEST_END();

    TEST_CASE("N2.77 Attendance: A Seat That Flaps: Lost Three Times Within A Minute (A Second Window That Takes The Seat Over Counts As A Loss) It Is The Subject Of The Vote At Once, In Any State (Absent, Catching Up, Or Back); Its Votes Stay Across Its Returns And Win Or Close; It Does Not Vote On Itself; Every Pause Counts At Least 5 s Toward The Cap, So That Flapping Reaches It") {
        using S = Attendance::State;
        const uint32_t t0 = 3000000;
        Attendance::Config cfg;
        cfg.rejoin_attempts = Attendance::kMaxRejoinAttempts;
        cfg.max_pause_ms = 3600000;
        ASSERT_TRUE(Attendance::Config{}.flap_losses == 3 && Attendance::Config{}.flap_window_ms == 60000 && kFlapLosses == 3 && kFlapWindowMs == 60000);
        // ---- three losses in a minute; two are none; three in more than a minute are none ----
        {
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 500) && a.caught_up(1, t0 + 1000));
            ASSERT_TRUE(a.lost(1, t0 + 20000));
            ASSERT_FALSE(a.flapping(1, t0 + 20000));                                      // two
            ASSERT_EQ(a.vote_subject(t0 + 20000), 255);
            ASSERT_TRUE(a.returning(1, t0 + 20500) && a.caught_up(1, t0 + 21000));
            ASSERT_TRUE(a.lost(1, t0 + 40000));                                           // the third, 40 s after the first
            ASSERT_TRUE(a.flapping(1, t0 + 40000));
            ASSERT_TRUE(a.away_ms(1, t0 + 40000) < 30000u);                               // (it is nowhere near 30 s away in all: the flap alone puts it to the vote)
            ASSERT_EQ(a.vote_subject(t0 + 40000), 1);
            ASSERT_EQ(a.presence_for(0, t0 + 40000).vote_seat, 1);
            Attendance b(cfg);
            b.seat_humans(0b0111, t0);
            ASSERT_TRUE(b.lost(1, t0) && b.returning(1, t0 + 500) && b.caught_up(1, t0 + 1000) && b.lost(1, t0 + 30000) && b.returning(1, t0 + 30500) && b.caught_up(1, t0 + 31000));
            ASSERT_TRUE(b.lost(1, t0 + 60001));                                           // the third is 60,001 ms after the first
            ASSERT_FALSE(b.flapping(1, t0 + 60001));
            ASSERT_EQ(b.vote_subject(t0 + 60001), 255);
            Attendance c(cfg);
            c.seat_humans(0b0111, t0);
            ASSERT_TRUE(c.lost(1, t0) && c.returning(1, t0 + 500) && c.caught_up(1, t0 + 1000) && c.lost(1, t0 + 30000) && c.returning(1, t0 + 30500) && c.caught_up(1, t0 + 31000));
            ASSERT_TRUE(c.lost(1, t0 + 60000));                                           // ... and 60,000 ms: within the minute
            ASSERT_TRUE(c.flapping(1, t0 + 60000));
        }
        // ---- the vote stays open across the returns of the seat, in every state, with its votes ----
        {
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            for (uint32_t i = 0; i < 3; ++i) {
                const uint32_t at = t0 + i * 10000;
                ASSERT_TRUE(a.lost(1, at));
                if (i < 2) ASSERT_TRUE(a.returning(1, at + 500) && a.caught_up(1, at + 1000));
            }
            const uint32_t now = t0 + 20000;                                              // the third loss: Absent
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_EQ(a.vote_subject(now), 1);
            ASSERT_TRUE(a.vote(0, 1, true, now));
            ASSERT_EQ(a.votes_for_continue(1), 1u);
            ASSERT_TRUE(a.returning(1, now + 500));                                       // catching up: still the subject, and its votes stay
            ASSERT_EQ(a.state(1), S::CatchingUp);
            ASSERT_EQ(a.vote_subject(now + 500), 1);
            ASSERT_EQ(a.votes_for_continue(1), 1u);
            ASSERT_EQ(a.presence_for(2, now + 500).vote_seat, 1);
            ASSERT_EQ(a.presence_for(2, now + 500).votes_continue, 1);
            ASSERT_TRUE(a.caught_up(1, now + 1000));                                      // back and Present: still the subject, with the vote that was cast
            ASSERT_EQ(a.state(1), S::Present);
            ASSERT_EQ(a.vote_subject(now + 1000), 1);
            ASSERT_EQ(a.votes_for_continue(1), 1u);
            const PresenceMsg p = a.presence_for(2, now + 1000);
            ASSERT_TRUE(p.missing.empty() && p.vote_seat == 1 && p.votes_continue == 1 && p.voters == 2 && p.your_vote == 0);   // (the voters are the others: seat 1 does not vote on itself)
            ASSERT_EQ(a.presence_for(0, now + 1000).your_vote, 2);                        // seat 0's own choice is still its own
            ASSERT_FALSE(a.vote(1, 1, true, now + 1000));                                 // a seat does not vote on its own case
            ASSERT_EQ(a.connected_humans(), 3);
            ASSERT_EQ(a.connected_humans(1), 2);
            std::vector<uint8_t> dropped = a.update(now + 1000);                          // 1 of 2: not more than half
            ASSERT_TRUE(dropped.empty() && a.state(1) == S::Present);
            ASSERT_TRUE(a.vote(2, 1, true, now + 1500));                                  // 2 of 2: the seat is dropped though it is connected
            dropped = a.update(now + 1500);
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1);
            ASSERT_EQ(a.state(1), S::Dropped);
            ASSERT_EQ(a.drops_by_vote(), 1u);
            ASSERT_FALSE(a.paused());
            ASSERT_EQ(a.vote_subject(now + 1500), 255);
            ASSERT_EQ(a.votes_for_continue(1), 0u);
        }
        // ---- the vote closes by itself after a minute without a loss, with the votes that were cast; a vote of the other kind (away long enough) is not closed by it ----
        {
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            for (uint32_t i = 0; i < 3; ++i) {
                const uint32_t at = t0 + i * 10000;
                ASSERT_TRUE(a.lost(1, at) && a.returning(1, at + 500) && a.caught_up(1, at + 1000));
            }
            ASSERT_TRUE(a.flapping(1, t0 + 20000 + 59999));
            ASSERT_TRUE(a.vote(0, 1, true, t0 + 30000));
            ASSERT_EQ(a.vote_subject(t0 + 20000 + 59999), 1);
            ASSERT_TRUE(a.update(t0 + 20000 + 59999).empty());
            ASSERT_FALSE(a.flapping(1, t0 + 20000 + 60000));                              // 60 s after the last loss
            ASSERT_EQ(a.vote_subject(t0 + 20000 + 60000), 255);
            ASSERT_TRUE(a.update(t0 + 20000 + 60000).empty());
            ASSERT_EQ(a.votes_for_continue(1), 0u);                                       // the vote that was cast is gone with the vote
            ASSERT_EQ(a.presence_for(0, t0 + 100000).vote_seat, 255);
            // a seat that flapped and is away for long is a subject for that too: the votes stay when the flap runs out
            Attendance b(cfg);
            b.seat_humans(0b0111, t0);
            ASSERT_TRUE(b.lost(1, t0) && b.returning(1, t0 + 500) && b.caught_up(1, t0 + 1000) && b.lost(1, t0 + 5000) && b.returning(1, t0 + 5500) && b.caught_up(1, t0 + 6000) && b.lost(1, t0 + 10000));
            ASSERT_TRUE(b.vote(0, 1, true, t0 + 11000));
            ASSERT_TRUE(b.update(t0 + 80000).empty());                                    // the flap is over (70 s), the seat has been away 70 s: the vote goes on
            ASSERT_FALSE(b.flapping(1, t0 + 80000));
            ASSERT_EQ(b.vote_subject(t0 + 80000), 1);
            ASSERT_EQ(b.votes_for_continue(1), 1u);
        }
        // ---- a second window that takes over a seat that is still Present is a loss, like a cut link ----
        {
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 500) && a.caught_up(1, t0 + 1000));
            ASSERT_TRUE(a.returning(1, t0 + 10000));                                      // a Hello for a seat whose link the host thinks is alive
            ASSERT_FALSE(a.flapping(1, t0 + 10000));
            ASSERT_TRUE(a.caught_up(1, t0 + 10500) && a.returning(1, t0 + 20000));        // ... a third time within the minute
            ASSERT_TRUE(a.flapping(1, t0 + 20000));
            ASSERT_EQ(a.vote_subject(t0 + 20000), 1);
        }
        // ---- the cap: every pause counts at least 5 s, so a connection that flaps reaches it long before it has cost that much time ----
        {
            Attendance::Config cap = cfg;
            cap.max_pause_ms = 40000;
            Attendance a(cap);
            a.seat_humans(0b0111, t0);
            for (uint32_t i = 0; i < 8; ++i) {
                const uint32_t at = t0 + i * 1500;
                ASSERT_FALSE(a.cap_reached(at));
                ASSERT_TRUE(a.update(at).empty());
                ASSERT_TRUE(a.lost(1, at) && a.returning(1, at + 400) && a.caught_up(1, at + 800));
            }
            ASSERT_EQ(a.held_ms(t0 + 12000), 8u * 800u);                                  // the match really waited 6.4 s ...
            ASSERT_EQ(a.pause_ms(t0 + 12000), 8u * 5000u);                                // ... and the cap counted 40 s
            ASSERT_TRUE(a.cap_reached(t0 + 12000) && a.cap_s(t0 + 12000) == 0);
            ASSERT_TRUE(a.lost(1, t0 + 12000));                                           // the next loss finds no budget: dropped at once
            const std::vector<uint8_t> dropped = a.update(t0 + 12000);
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1 && a.drops_by_cap() == 1u);
        }
        // ---- the same from other points of the clock ----
        for (const uint32_t start : {0u, 0xFFFFFC00u, 0xFFFF0000u, 0x7FFFF000u}) {
            Attendance a(cfg);
            a.seat_humans(0b0111, start);
            for (uint32_t i = 0; i < 3; ++i) ASSERT_TRUE(a.lost(1, start + i * 20000) && (i == 2 || (a.returning(1, start + i * 20000 + 500) && a.caught_up(1, start + i * 20000 + 1000))));
            ASSERT_TRUE(a.flapping(1, start + 40000) && a.vote_subject(start + 40000) == 1);
            ASSERT_TRUE(a.flapping(1, start + 99999) && !a.flapping(1, start + 100000));
        }
    } TEST_END();

    TEST_CASE("N2.78 Attendance: The Resume Countdown: After A Pause Of 3 s Or More (A Seat Back And Verified, Or Dropped By A Vote Or The Cap) The Match Is Held For The Countdown, Which Is No Pause (Not The Cap's, Not Any Catch-Up's, But The Match's Own Held Time); A Shorter Pause Resumes At Once; A Loss Cancels It And One Follows That Pause; 0 Is Off") {
        using S = Attendance::State;
        const uint32_t t0 = 5000000;
        Attendance::Config cfg;
        cfg.resume_countdown_ms = 10000;
        cfg.rejoin_attempts = Attendance::kMaxRejoinAttempts;
        ASSERT_TRUE(Attendance::Config{}.resume_countdown_ms == 0 && kResumeCountdownMs == 10000 && kResumeMinPauseMs == 3000 && Attendance::Config{}.resume_min_pause_ms == 3000);   // (the library's default is none, a room's is ten seconds)
        {   // a pause of 20 s ends with a seat that is back: ten seconds of countdown
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_FALSE(a.counting_down(t0) || a.paused());
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 15000));
            ASSERT_FALSE(a.counting_down(t0 + 16000));                                    // (it is a pause: no countdown yet)
            ASSERT_TRUE(a.caught_up(1, t0 + 20000));
            ASSERT_FALSE(a.paused());
            ASSERT_TRUE(a.counting_down(t0 + 20000));
            ASSERT_EQ(a.resume_s(t0 + 20000), 10);
            ASSERT_EQ(a.resume_s(t0 + 20001), 10);                                        // 9,999 ms: rounded up
            ASSERT_EQ(a.resume_s(t0 + 20999), 10);                                        // 9,001 ms: still ten
            ASSERT_EQ(a.resume_s(t0 + 21000), 9);                                         // 9,000 ms: nine
            ASSERT_EQ(a.resume_s(t0 + 21001), 9);
            ASSERT_EQ(a.resume_s(t0 + 29001), 1);
            ASSERT_EQ(a.resume_s(t0 + 29999), 1);
            ASSERT_TRUE(a.counting_down(t0 + 29999));
            ASSERT_FALSE(a.counting_down(t0 + 30000));                                    // ten seconds to the millisecond
            ASSERT_EQ(a.resume_s(t0 + 30000), 0);
            ASSERT_EQ(a.resume_s(t0 + 99999), 0);
            // it is no pause: the cap and the seat's away time do not count it, the match's held time does
            ASSERT_EQ(a.pause_ms(t0 + 20000), 20000u);
            ASSERT_EQ(a.pause_ms(t0 + 25000), 20000u);
            ASSERT_EQ(a.pause_ms(t0 + 99999), 20000u);
            ASSERT_EQ(a.cap_left_ms(t0 + 25000), cfg.max_pause_ms - 20000u);
            ASSERT_EQ(a.away_ms(1, t0 + 99999), 20000u);
            ASSERT_EQ(a.held_ms(t0 + 20000), 20000u);
            ASSERT_EQ(a.held_ms(t0 + 25000), 25000u);
            ASSERT_EQ(a.held_ms(t0 + 30000), 30000u);
            ASSERT_EQ(a.held_ms(t0 + 99999), 30000u);                                     // (the time that the match did not run: 20 s paused, 10 s counted down)
            // the presence says it, to everybody, with nobody missing
            const PresenceMsg p = a.presence_for(0, t0 + 23500);
            ASSERT_TRUE(p.missing.empty() && p.resume_s == 7 && p.cap_s == a.cap_s(t0 + 23500));
            PresenceMsg back;
            ASSERT_TRUE(decode(encode(p), back) && back.resume_s == 7);
            ASSERT_EQ(a.presence_for(255, t0 + 31000).resume_s, 0);
        }
        {   // a pause shorter than 3 s is a blip: no countdown (2,999 ms none, 3,000 ms one)
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 1000) && a.caught_up(1, t0 + 2999));
            ASSERT_FALSE(a.counting_down(t0 + 2999) || a.counting_down(t0 + 3000) || a.paused());
            ASSERT_EQ(a.held_ms(t0 + 3000), 2999u);
            ASSERT_TRUE(a.lost(1, t0 + 10000) && a.returning(1, t0 + 10500) && a.caught_up(1, t0 + 13000));
            ASSERT_TRUE(a.counting_down(t0 + 13000));
            ASSERT_EQ(a.resume_s(t0 + 13000), 10);
            // the pause of two seats is one pause: 5 s here, though neither seat was away 3 s on its own after the other came back
            Attendance b(cfg);
            b.seat_humans(0b0111, t0);
            ASSERT_TRUE(b.lost(1, t0) && b.lost(2, t0 + 2000) && b.returning(1, t0 + 2500) && b.caught_up(1, t0 + 3000));
            ASSERT_FALSE(b.counting_down(t0 + 3000));                                     // seat 2 is away: the pause goes on
            ASSERT_TRUE(b.returning(2, t0 + 4000) && b.caught_up(2, t0 + 5000));
            ASSERT_TRUE(b.counting_down(t0 + 5000));
        }
        {   // a loss during the countdown cancels it (the pause rules apply); a countdown follows that pause, however short it is
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 4000) && a.caught_up(1, t0 + 5000));
            ASSERT_TRUE(a.counting_down(t0 + 8000));
            ASSERT_TRUE(a.lost(2, t0 + 8000));                                            // 3 s into the countdown
            ASSERT_TRUE(a.paused());
            ASSERT_FALSE(a.counting_down(t0 + 8000) || a.counting_down(t0 + 9000));
            ASSERT_EQ(a.resume_s(t0 + 9000), 0);
            ASSERT_EQ(a.held_ms(t0 + 9000), 5000u + 3000u + 1000u);                       // 5 s paused, 3 s counted, 1 s paused
            ASSERT_TRUE(a.returning(2, t0 + 8200) && a.caught_up(2, t0 + 8400));          // the new pause was 400 ms: shorter than 3 s ...
            ASSERT_TRUE(a.counting_down(t0 + 8400));                                      // ... and the countdown comes all the same: 10 s from now
            ASSERT_EQ(a.resume_s(t0 + 8400), 10);
            ASSERT_FALSE(a.counting_down(t0 + 18400));
            ASSERT_EQ(a.pause_ms(t0 + 18400), 5000u + 5000u);                             // (400 ms counted 5 s: the pauses count at least 5 s for the cap)
            ASSERT_EQ(a.held_ms(t0 + 18400), 5000u + 3000u + 400u + 10000u);
            // and the countdown after the second pause does not owe a third
            ASSERT_FALSE(a.counting_down(t0 + 40000));
            ASSERT_TRUE(a.lost(2, t0 + 40000) && a.returning(2, t0 + 40100) && a.caught_up(2, t0 + 40200));
            ASSERT_FALSE(a.counting_down(t0 + 40200));                                    // a plain blip again
        }
        {   // a seat that returns and is lost again within the countdown: a loss in the very ms of the countdown's end is the same as one after it
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 4000) && a.caught_up(1, t0 + 5000));
            ASSERT_TRUE(a.lost(2, t0 + 15000));                                           // the countdown is over (10 s): this is an ordinary loss
            ASSERT_TRUE(a.returning(2, t0 + 15100) && a.caught_up(2, t0 + 15200));
            ASSERT_FALSE(a.counting_down(t0 + 15200));
        }
        {   // the countdown after a vote that was won, and after the cap
            Attendance a(cfg);
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(3, t0));
            ASSERT_TRUE(a.vote(0, 3, true, t0 + 31000) && a.vote(1, 3, true, t0 + 31000));
            ASSERT_EQ(a.update(t0 + 31000).size(), 1u);
            ASSERT_FALSE(a.paused());
            ASSERT_TRUE(a.counting_down(t0 + 31000));                                     // the match goes on after ten seconds, without seat 3
            ASSERT_EQ(a.resume_s(t0 + 31000), 10);
            ASSERT_EQ(a.pause_ms(t0 + 41000), 31000u);
            ASSERT_FALSE(a.counting_down(t0 + 41000));
            Attendance::Config capped = cfg;
            capped.max_pause_ms = 60000;
            Attendance b(capped);
            b.seat_humans(0b0111, t0);
            ASSERT_TRUE(b.lost(2, t0));
            ASSERT_EQ(b.update(t0 + 60000).size(), 1u);
            ASSERT_FALSE(b.paused());
            ASSERT_TRUE(b.counting_down(t0 + 60000));
            ASSERT_EQ(b.pause_ms(t0 + 65000), 60000u);                                    // (the cap is spent: the countdown is not on it)
            ASSERT_EQ(b.held_ms(t0 + 65000), 65000u);
            // a pause that ends with a seat that LEFT (a Leave while the other one is away) is a pause that ended too
            Attendance c(cfg);
            c.seat_humans(0b0111, t0);
            ASSERT_TRUE(c.lost(1, t0) && c.dropped(1, t0 + 8000));
            ASSERT_TRUE(c.counting_down(t0 + 8000));
        }
        {   // 0 is off; a short or a long setting is what it says; the match begins again: nothing is left
            Attendance::Config off = cfg;
            off.resume_countdown_ms = 0;
            Attendance a(off);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 5000) && a.caught_up(1, t0 + 20000));
            ASSERT_FALSE(a.counting_down(t0 + 20000) || a.paused());
            ASSERT_EQ(a.resume_s(t0 + 20000), 0);
            ASSERT_EQ(a.held_ms(t0 + 20000), 20000u);
            Attendance::Config sixty = cfg;
            sixty.resume_countdown_ms = 60000;
            Attendance b(sixty);
            b.seat_humans(0b0111, t0);
            ASSERT_TRUE(b.lost(1, t0) && b.returning(1, t0 + 5000) && b.caught_up(1, t0 + 20000));
            ASSERT_EQ(b.resume_s(t0 + 20000), 60);
            ASSERT_EQ(b.resume_s(t0 + 79001), 1);
            ASSERT_FALSE(b.counting_down(t0 + 80000));
            Attendance c(cfg);
            c.seat_humans(0b0111, t0);
            ASSERT_TRUE(c.lost(1, t0) && c.returning(1, t0 + 5000) && c.caught_up(1, t0 + 20000));
            c.seat_humans(0b0111, t0 + 22000);
            ASSERT_FALSE(c.counting_down(t0 + 22000) || c.paused());
            ASSERT_EQ(c.held_ms(t0 + 22500), 0u);
            ASSERT_TRUE(c.state(1) == S::Present);
        }
        {   // the same from other points of the clock
            for (const uint32_t start : {0u, 0xFFFFF000u, 0xFFFFFFFFu, 0x80000000u - 1u}) {
                Attendance a(cfg);
                a.seat_humans(0b0111, start);
                ASSERT_TRUE(a.lost(1, start) && a.returning(1, start + 4000) && a.caught_up(1, start + 5000));
                ASSERT_TRUE(a.counting_down(start + 14999) && a.resume_s(start + 14999) == 1 && !a.counting_down(start + 15000));
                ASSERT_EQ(a.held_ms(start + 20000), 15000u);
            }
        }
    } TEST_END();
    TEST_CASE("N2.79 Lock-Step Runner: A Match That The Server Holds (Presence: A Pause, The Countdown After It) Is No Stall Of The Link: The Turns In Hand Run, Then The Runner Waits Without A Stall (Nothing Is Told To The Jitter Buffer, No \"Waiting For The Other Players\", Pauses Of 40 ms To 30 s) And Collects The Buffer Again; What Was Read Before The Pause Is Forgotten When It Ends, The Buffer Itself Is Not Reset") {
        auto empty_turn = [](uint32_t n) {
            TurnMsg t;
            t.turn = n;
            return t;
        };
        // One continuous stream on a runner of its own: turn k is sealed at k * 50 ms (and arrives `delay(k)` later, never before an earlier turn), every 10 ms the runner has its frame; no turn
        // is sealed for `pause_ms` after turn `pause_after_turn`; when `held` the runner is told (set_held) when the pause begins and when it ends. The start of the match and the end of the
        // stream (nothing follows the last turn: that is a stall, and no business of this) are not looked at.
        struct Run {
            uint32_t buffer_max{0};
            uint32_t buffer_at_end{0};
            uint32_t stalled_most_ms{0};
            bool stalled_ever{false};
            bool stalled_in_pause{false};                                             // the runner was stalled at a moment inside the pause (after the turns in hand had run)
            bool rebuilding_at_release{false};
            size_t executed{0};
            uint32_t jitter_ms_after{0};
        };
        const auto stream = [&](LockstepRunner& runner, uint32_t turns, uint32_t pause_after_turn, uint32_t pause_ms, bool held, const std::function<uint32_t(uint32_t)>& delay) {
            Run out;
            std::vector<std::pair<uint32_t, uint32_t>> arrivals;                      // (arrival, turn)
            uint32_t last = 0;
            uint32_t seal = 0;
            for (uint32_t i = 0; i < turns; ++i) {
                if (i == pause_after_turn) seal += pause_ms;                          // nothing is sealed for the pause
                last = std::max(last, seal + delay(i));
                arrivals.push_back({last, i});
                seal += kTurnMs;
            }
            const uint32_t pause_begin = pause_after_turn * kTurnMs;
            const uint32_t pause_end = pause_begin + pause_ms;
            size_t next = 0;
            bool announced = false;
            bool released = false;
            for (uint32_t now = 10; now <= seal + 400; now += 10) {
                if (held && !announced && now >= pause_begin) {                       // the Presence reaches the machine right behind the last turn that was sealed
                    runner.set_held(true);
                    announced = true;
                }
                if (held && announced && !released && now >= pause_end) {              // ... and the one that ends the pause right before the first turn after it
                    out.rebuilding_at_release = runner.rebuilding();
                    runner.set_held(false);
                    released = true;
                }
                while (next < arrivals.size() && arrivals[next].first <= now) {
                    runner.on_turn(empty_turn(arrivals[next].second));
                    ++next;
                }
                out.executed += runner.update(10).size();
                out.buffer_max = std::max(out.buffer_max, runner.buffer_turns());
                if (now > pause_begin + 150 && now < pause_end && runner.stalled()) out.stalled_in_pause = true;
                if (now < 500 || now > seal - kTurnMs) continue;
                out.stalled_most_ms = std::max(out.stalled_most_ms, runner.stalled_ms());
                out.stalled_ever = out.stalled_ever || runner.stalled();
            }
            out.buffer_at_end = runner.buffer_turns();
            out.jitter_ms_after = runner.jitter().lateness_ms();
            return out;
        };
        const auto none = [](uint32_t) { return 0u; };
        // ---- the control: a pause that the runner does not know of is a stall, and a short one grows the buffer (this is what the others' windows did) ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1, 2 * 3600 * 1000);
            LockstepRunner steady(sim);
            const Run quiet = stream(steady, 800, 100000, 0, false, none);             // no pause: a steady link keeps one turn
            ASSERT_TRUE(quiet.buffer_max == 1 && !quiet.stalled_ever);
            sim::SimulationEngine other;
            build_world(other, 1, 2 * 3600 * 1000);
            LockstepRunner runner(other);
            const Run cut = stream(runner, 800, 600, 130, false, none);                // 30 s of a steady link and a pause of 130 ms
            ASSERT_TRUE(cut.stalled_ever);
            ASSERT_TRUE(cut.stalled_in_pause || cut.buffer_max >= 2);
            ASSERT_TRUE(cut.buffer_max >= 2);                                          // the buffer grew for a pause that was nobody's link
        }
        // ---- held: the same pauses, the runner told: no stall, no growth, whatever the length of the pause ----
        for (const uint32_t pause : {40u, 130u, 210u, 600u, 1200u, 5000u, 30000u}) {
            sim::SimulationEngine sim;
            build_world(sim, 1, 2 * 3600 * 1000);
            LockstepRunner runner(sim);
            const Run cut = stream(runner, 800, 600, pause, true, none);
            ASSERT_FALSE(cut.stalled_ever || cut.stalled_in_pause);                   // no "Waiting for the other players..." ever, not even in a pause of half a minute
            ASSERT_EQ(cut.stalled_most_ms, 0u);
            ASSERT_EQ(cut.buffer_max, 1u);                                            // the jitter buffer was told nothing: it is where it was
            ASSERT_EQ(cut.buffer_at_end, 1u);
            ASSERT_TRUE(cut.rebuilding_at_release || pause <= 130);                   // (a pause that the buffer's turns outlast never ran dry: nothing to collect)
            ASSERT_EQ(cut.executed, size_t{800});                                     // every turn ran
            ASSERT_EQ(runner.next_turn_to_execute(), 800u);
            ASSERT_FALSE(runner.held());
            ASSERT_EQ(cut.jitter_ms_after, 0u);                                       // a pause is not lateness: the later turns were measured against each other
        }
        // ---- a rough link keeps what it learned across a pause (the pause forgets the samples, not the buffer) ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1, 2 * 3600 * 1000);
            LockstepRunner runner(sim);
            Lcg rng(79);
            const Run cut = stream(runner, 900, 600, 5000, true, [&](uint32_t) { return rng.below(130); });
            ASSERT_FALSE(cut.stalled_in_pause);                                       // (the link is rough: it stalls now and then, as it should; the pause is no stall)
            ASSERT_TRUE(cut.buffer_max >= 3);                                         // a link of 0 - 130 ms of jitter needs three or four turns ...
            ASSERT_TRUE(cut.buffer_at_end >= 3 && cut.buffer_at_end <= 4);            // ... and still does after the pause: it was not reset to one by it
            ASSERT_EQ(cut.executed, size_t{900});
        }
        // ---- the details: turns that were queued before the Presence run; a turn that arrives while the match is held runs; held twice or released twice changes nothing ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            ASSERT_FALSE(runner.held());
            runner.set_held(false);                                                    // nothing to release
            ASSERT_FALSE(runner.held());
            for (uint32_t k = 0; k < 5; ++k) ASSERT_TRUE(runner.on_turn(empty_turn(k)));
            ASSERT_EQ(runner.update(0).size(), 1u);                                    // it starts
            runner.set_held(true);
            runner.set_held(true);
            ASSERT_TRUE(runner.held());
            size_t ran = 1;
            for (int i = 0; i < 30; ++i) ran += runner.update(10).size();              // 300 ms: the four turns that were queued run (the server sealed them)
            ASSERT_EQ(ran, size_t{5});
            for (int i = 0; i < 400; ++i) runner.update(50);                           // 20 s of nothing: not a stall, not a message
            ASSERT_FALSE(runner.stalled());
            ASSERT_EQ(runner.stalled_ms(), 0u);
            ASSERT_EQ(runner.buffer_turns(), 1u);
            ASSERT_TRUE(runner.rebuilding());                                          // collecting the buffer for the turns that follow
            ASSERT_TRUE(runner.on_turn(empty_turn(5)));                                // (a turn that was in flight: it is not run before the buffer is full again, as after a start)
            ASSERT_TRUE(runner.update(50).empty());
            ASSERT_TRUE(runner.on_turn(empty_turn(6)));
            ASSERT_EQ(runner.update(50).size(), 1u);
            runner.set_held(false);
            runner.set_held(false);
            ASSERT_FALSE(runner.held());
            ASSERT_FALSE(runner.stalled());
            ASSERT_EQ(runner.buffer_turns(), 1u);
        }
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

    TEST_CASE("S2.5 Dedicated Server: A StartRequest That Reaches A Running Match (The Leader's Second Click Crossed The Start) Is Ignored And Costs Nothing (Up To The 16 That A Person Could Send, S2.6 Has The Flood); With A Payload Beyond The Fill Level It Is Garbage; A Host That Holds A Seat Has No Leader And Counts It As A Violation") {
        {
            ServerMatch m(1, 3, {20, 5});
            m.run(1000);
            for (uint32_t i = 0; i < kIgnoredStartRequestsAllowed; ++i) m.client_ends[0]->send(encode(StartRequestMsg{}));          // sixteen late clicks: more than the eight violations that throw a client out
            m.run(500);
            ASSERT_TRUE(m.host->client_present(0));
            ASSERT_EQ(m.host->violations(0), 0u);
            ASSERT_EQ(m.host->ignored_start_requests(), kIgnoredStartRequestsAllowed);
            m.client_ends[1]->send({static_cast<uint8_t>(MsgType::StartRequest), 0, 1});            // with a payload beyond the fill level: garbage like any other
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
            if (kind == "chat") ASSERT_EQ(chats, kChatBurst);                                                              // the chat budget's burst was relayed (S2.10), not a second's worth of messages, not 3000 lines
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

    TEST_CASE("N2.17 Chat: A Line For All Is Relayed To Everybody With The Sender Stamped By The Connection (A Line For The Team Is Another Rule Since The Release That Filters Team Chat: N2.90 - N2.92)") {
        Match m(1, 3, {20, 0});
        std::vector<ChatMsg> host_seen;
        std::vector<ChatMsg> c1_seen;
        std::vector<ChatMsg> c2_seen;
        m.host->set_on_chat([&](const ChatMsg& c) { host_seen.push_back(c); });
        m.clients[0]->set_on_chat([&](const ChatMsg& c) { c1_seen.push_back(c); });
        m.clients[1]->set_on_chat([&](const ChatMsg& c) { c2_seen.push_back(c); });
        m.run(500);
        ASSERT_TRUE(m.clients[0]->chat("hello all", false));
        m.host->chat_local("host here", false);                          // (this line used to be a team line: a team line goes to the ally only now, N2.92)
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
            ASSERT_TRUE(b->sender == 0 && !b->team);
        }
        m.host->chat_local("host team", true);                          // the host has no ally: nobody but itself hears a line for its team
        m.run(500, false);
        ASSERT_EQ(host_seen.size(), 3u);
        ASSERT_TRUE(c1_seen.size() == 2u && c2_seen.size() == 2u);
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
        m.hosts[1]->chat_local("the host of the rest", false);
        m.hosts[1]->chat_local("team of the new host", true);                     // (no alliance: a line for the team stays with its sender, N2.90)
        m.run(1000);
        for (uint8_t s = 1; s < 4; ++s) {
            bool a = false, b = false, t = false;
            for (const ChatMsg& c : m.chats[s]) {
                if (c.text == "still here") a = c.sender == 3 && !c.team;
                if (c.text == "the host of the rest") b = c.sender == 1 && !c.team;
                if (c.text == "team of the new host") t = true;
            }
            ASSERT_TRUE(a && b);
            ASSERT_EQ(t, s == 1);
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

// ---------------------------------------------------------------------------------------------------------------------------------
// Reconnect, release A: the sessions (protocol 10). A dedicated server's match that holds the seat of a player whose connection is lost. N2.47 - N2.7x.
// ---------------------------------------------------------------------------------------------------------------------------------

void run_reconnect_session_tests() {
    TEST_CASE("N2.47 Hold: A Lost Link Pauses The Match For Everybody Within One Pass (Nothing Is Sealed For 20 s, The Others Stall And Are Told Who Is Missing And Since When, The Held Seat Gates Nothing), The Player Comes Back With The Turns It Has, The Match Resumes And All Three Machines End Identical") {
        HoldMatch m(3);
        m.run(6000);
        ASSERT_FALSE(m.host->paused());
        for (auto& c : m.clients) ASSERT_TRUE(c->presence().missing.empty() && !c->paused());
        ASSERT_TRUE(m.sealed() >= 115);
        m.auto_reconnect[1] = false;                                            // seat 1's machine does not look for a new link yet
        m.cut(1);
        const uint32_t cut_at = m.now;
        uint32_t paused_after_ms = 0;
        uint32_t told_after_ms = 0;
        m.run(500, true, [&](uint32_t now) {
            if (paused_after_ms == 0 && m.host->paused()) paused_after_ms = now - cut_at;
            if (told_after_ms == 0 && m.clients[0]->paused() && m.clients[2]->paused()) told_after_ms = now - cut_at;
        });
        ASSERT_TRUE(paused_after_ms > 0 && paused_after_ms <= 20);               // one pass of the host (10 ms steps)
        ASSERT_TRUE(told_after_ms > 0 && told_after_ms <= 120);                  // ... and the one-way trip of the Presence (30 - 40 ms)
        ASSERT_TRUE(m.host->seat_held(1) && !m.host->client_present(1));
        ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Absent);
        ASSERT_EQ(m.host->behind_ms(1), 0u);                                     // inactive in the sequencer: it gates nothing and its acknowledgements no longer count
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
        const uint32_t sealed_at_pause = m.sealed();
        const uint64_t ticks_at_pause = m.sims[0]->current_tick();
        for (uint8_t seat : {uint8_t{0}, uint8_t{2}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_EQ(p.missing.size(), size_t{1});
            ASSERT_TRUE(p.missing[0].seat == 1 && p.missing[0].state == PresenceMsg::State::Absent && p.missing[0].progress == 0);
            ASSERT_TRUE(p.vote_seat == 255 && p.voters == 2 && p.cap_s <= 1800 && p.cap_s >= 1795);
        }
        m.run(20000);
        ASSERT_EQ(m.sealed(), sealed_at_pause);                                  // nothing is sealed for 20 s ...
        ASSERT_TRUE(m.sims[0]->current_tick() <= ticks_at_pause + 3 && m.sims[2]->current_tick() <= ticks_at_pause + 3);       // ... so nobody plays: the clients wait at a turn boundary
        ASSERT_TRUE(m.clients[0]->runner().held() && m.clients[2]->runner().held());          // REWRITTEN: the others' runners used to be STALLED here (which told their jitter buffers about a pause); they know the match is held (Presence): they wait, with no stall
        ASSERT_FALSE(m.clients[0]->runner().stalled() || m.clients[2]->runner().stalled());
        const PresenceMsg& later = m.clients[0]->presence();
        ASSERT_TRUE(later.missing[0].waited_s >= 19 && later.missing[0].waited_s <= 22);        // since when: the seat has been away for 20 s and more
        ASSERT_TRUE(later.cap_s < 1795);                                                         // the cap counts down
        ASSERT_EQ(m.host->violations(0), 0u);                                    // the clicks that they went on giving (the script) cost nothing: refused here, discarded there
        ASSERT_EQ(m.host->violations(2), 0u);
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
        ASSERT_FALSE(m.clients[1]->lost());                                      // the machine of seat 1 waits for its owner to give it a link
        ASSERT_EQ(static_cast<int>(m.clients[1]->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));
        // the machine comes back in memory: it says Hello with the key and the turns it has, is given the rest, and the match goes on
        m.auto_reconnect[1] = true;
        const uint32_t back_at = m.now;
        uint32_t resumed_after_ms = 0;
        m.run(3000, true, [&](uint32_t now) {
            if (resumed_after_ms == 0 && !m.host->paused()) resumed_after_ms = now - back_at;
        });
        ASSERT_TRUE(resumed_after_ms > 0 && resumed_after_ms <= 600);            // a round trip for the Hello and the Welcome, the stream, the hash: about 0.3 s over 30 - 40 ms links
        std::cout << "\n      [reconnect] a link cut: the host paused " << paused_after_ms << " ms later, the others knew " << told_after_ms << " ms later; the machine came back in memory and the match resumed "
                  << resumed_after_ms << " ms after its new link was made (links of 30 - 40 ms one way)" << std::flush;
        const uint32_t sealed_at_resume = m.sealed();
        m.run(2000);
        ASSERT_TRUE(m.sealed() - sealed_at_resume >= 38 && m.sealed() - sealed_at_resume <= 42);      // the schedule slid during the pause: no burst of 400 turns, 20 a second
        ASSERT_EQ(m.host->attendance().rejoins(), 1u);
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        ASSERT_TRUE(m.host->client_present(1) && !m.host->seat_held(1));
        ASSERT_EQ(static_cast<int>(m.clients[1]->mode()), static_cast<int>(ClientSession::Mode::Normal));
        ASSERT_TRUE(m.sealed() > sealed_at_pause + 40);                          // sealing went on
        for (auto& c : m.clients) ASSERT_TRUE(c->presence().missing.empty() && !c->paused());      // everybody was told that the match runs
        m.run(10000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
        for (auto& s : m.sims) ASSERT_EQ(s->current_tick(), m.referee.current_tick());
        ASSERT_TRUE(m.all_equal());                                              // the three machines and the referee: one state
        ASSERT_TRUE(m.clients[0]->runner().buffer_turns() == 1 && m.clients[2]->runner().buffer_turns() == 1);      // a pause is no lateness of the link: the others' jitter buffers did not grow
        ASSERT_TRUE(m.clients[1]->runner().buffer_turns() == 1);                 // and the returning machine's did not (the catch-up feeds it nothing)
    } TEST_END();

    TEST_CASE("N2.48 Hold: A Machine That Starts From Nothing (A Reloaded Page: A New Engine, The Lobby, Then The Match) Is Given More Than 300 Turns And Verified, Twice, The Second Time With A Slowed Catch-Up That Lasts Past 30 s Of Away Time: The Seat Is Catching Up, So No Vote Is Opened For It And It Is Not Dropped While Its Acknowledgements Move") {
        {
            HoldMatch m(3);
            m.run(40000);                                                        // 800 turns: more than the 600 that every machine keeps
            ASSERT_TRUE(m.sealed() >= 790);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(2000);
            ASSERT_TRUE(m.host->paused() && m.host->attendance().state(2) == Attendance::State::Absent);
            const uint32_t total = m.sealed();
            const uint32_t reload_at = m.now;
            m.reload(2);
            bool saw_catching_up = false;
            ASSERT_TRUE(m.until([&]() {
                saw_catching_up = saw_catching_up || m.host->attendance().state(2) == Attendance::State::CatchingUp;
                return !m.host->paused() && m.clients[2] != nullptr && m.clients[2]->mode() == ClientSession::Mode::Normal;       // (the machine goes on when the server's word reaches it)
            }, 20000));
            ASSERT_TRUE(saw_catching_up);
            std::cout << "\n      [reconnect] a machine that starts from nothing was given " << total << " turns (" << m.host->log().bytes() << " bytes of log) and was back " << m.now - reload_at
                      << " ms after its page opened (links of 30 - 40 ms one way; the work itself is measured in docs/NETWORK_PORT.md)" << std::flush;
            ASSERT_EQ(m.host->attendance().rejoins(), 1u);
            ASSERT_TRUE(m.clients[2]->runner().next_turn_to_execute() >= total);
            m.run(5000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
            ASSERT_TRUE(m.all_equal());
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Present);
        }
        {
            HoldOptions o;
            o.client.catch_up_ticks = 1;                                         // one turn per update (10 ms): 100 turns a second
            HoldMatch m(3, o);
            m.run(170000);                                                       // 3400 turns: 34 s of catch-up at that speed
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(1000);
            m.reload(1);
            bool opened_a_vote = false;
            bool dropped = false;
            uint8_t last_percent = 0;
            bool percent_grew = false;
            uint32_t catching_for_ms = 0;
            m.run(60000, true, [&](uint32_t) {
                if (m.host->attendance().state(1) == Attendance::State::CatchingUp) catching_for_ms += 10;
                for (uint8_t seat : {uint8_t{0}, uint8_t{2}}) opened_a_vote = opened_a_vote || m.clients[seat]->presence().vote_seat != 255;
                dropped = dropped || m.host->attendance().state(1) == Attendance::State::Dropped;
                const uint8_t pc = m.host->attendance().percent(1);
                if (pc > last_percent) percent_grew = true;
                last_percent = std::max(last_percent, pc);
            });
            ASSERT_TRUE(catching_for_ms >= 30000);                               // it was catching up for longer than the vote's 30 s: away for 35 s in all
            ASSERT_FALSE(opened_a_vote);                                         // a seat that is back is never put to the vote
            ASSERT_FALSE(dropped);
            ASSERT_TRUE(percent_grew && last_percent >= 90);
            ASSERT_FALSE(m.host->paused());                                      // it got through
            ASSERT_EQ(m.host->attendance().rejoins(), 1u);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.49 Hold: The Vote Opens After 30 s Away And Not Before; More Than Half Of The Connected Players Win It (Two Of Two Here: One Of Two Is Not Enough, A Changed Choice Counts); The Drop Is Sealed In One Turn And Executes At The Same Tick On Every Machine, The Match Goes On, The Key Of The Dropped Seat Is Told \"Dropped\"") {
        HoldMatch m(3);
        m.run(5000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        const uint32_t cut_at = m.now;
        m.run(28500);
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) ASSERT_TRUE(m.clients[seat]->paused() && m.clients[seat]->presence().vote_seat == 255);       // 28.5 s: no vote yet
        uint32_t vote_opened_after_ms = 0;
        m.run(3500, true, [&](uint32_t now) {
            if (vote_opened_after_ms == 0 && m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2) vote_opened_after_ms = now - cut_at;
        });
        ASSERT_TRUE(vote_opened_after_ms >= 30000 && vote_opened_after_ms <= 31600);        // 30 s of away time, and the second for the Presence to arrive
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_TRUE(p.vote_seat == 2 && p.voters == 2 && p.votes_continue == 0 && p.your_vote == 0);
        }
        ASSERT_TRUE(m.clients[0]->vote(2, true));                                    // seat 0: continue without seat 2. One of two connected players is not more than half
        m.run(1500);
        ASSERT_FALSE(m.sims[0]->is_player_dropped(2));
        ASSERT_TRUE(m.host->paused());
        ASSERT_TRUE(m.clients[0]->presence().votes_continue == 1 && m.clients[0]->presence().your_vote == 2);        // everybody is told the count, each its own choice
        ASSERT_TRUE(m.clients[1]->presence().votes_continue == 1 && m.clients[1]->presence().your_vote == 0);
        ASSERT_TRUE(m.clients[1]->vote(2, false));                                   // seat 1: keep waiting
        m.run(1500);
        ASSERT_TRUE(m.host->paused() && m.clients[1]->presence().your_vote == 1 && m.clients[1]->presence().votes_continue == 1);
        ASSERT_TRUE(m.clients[0]->vote(1, true));                                    // (a vote about a seat that is not the subject: ignored by the host, no offence)
        m.run(300);
        ASSERT_EQ(m.host->violations(0), 0u);
        ASSERT_TRUE(m.host->paused());
        // seat 1 changes its mind: two of two connected players want to continue
        const uint32_t sealed_before = m.sealed();
        ASSERT_TRUE(m.clients[1]->vote(2, true));
        // the tick at which each machine executes the Drop: the same
        uint64_t drop_tick[3] = {0, 0, 0};
        m.run(3000, true, [&](uint32_t) {
            if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(2)) drop_tick[0] = m.sims[0]->current_tick();
            if (drop_tick[1] == 0 && m.sims[1]->is_player_dropped(2)) drop_tick[1] = m.sims[1]->current_tick();
            if (drop_tick[2] == 0 && m.referee.is_player_dropped(2)) drop_tick[2] = m.referee.current_tick();
        });
        ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[1] != 0 && drop_tick[2] != 0);
        ASSERT_TRUE(drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);           // the same tick on every machine, the referee included
        ASSERT_EQ(m.host->attendance().drops_by_vote(), 1u);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
        ASSERT_FALSE(m.host->paused());                                              // the pause is over: the match goes on without seat 2
        ASSERT_TRUE(m.sealed() > sealed_before);
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) ASSERT_TRUE(!m.clients[seat]->paused() && m.clients[seat]->presence().missing.empty());
        m.run(5000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
            ASSERT_TRUE(m.sims[seat]->is_player_dropped(2));
            ASSERT_EQ(m.sims[seat]->current_tick(), m.referee.current_tick());
        }
        ASSERT_TRUE(m.all_equal());                                                  // the Drop was applied at the same tick: the states are one
        // the machine of the dropped seat comes by with its key: the seat is gone ("Sorry, you have been dropped": its session ends with that reason)
        m.auto_reconnect[2] = true;
        m.run(5000);
        ASSERT_TRUE(m.clients[2]->lost() && m.clients[2]->rejected() && m.clients[2]->reject_reason() == RejectReason::Dropped);
        ASSERT_FALSE(m.clients[2]->wants_connection(m.now));                         // and it makes no more attempts
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
    } TEST_END();

    TEST_CASE("N2.50 Hold: The Match's Total Pause Is Capped (Here 20 s) And The Cap Is Absolute: At The Cap Every Seat That Is Not Present Is Dropped At The Same Tick Everywhere, A Seat That Is Catching Up And Progressing Too, And The Match Goes On, cap_s Counts Down For The Players; One That Stops Is Let Go After 20 s And Then Dropped At Once; A Seat That Was Dropped Is Told So By Its Key") {
        {
            HoldOptions o;
            o.host.attendance.max_pause_ms = 20000;
            HoldMatch m(3, o);
            m.run(5000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            const uint32_t cut_at = m.now;
            m.run(5000);
            ASSERT_TRUE(m.host->paused());
            ASSERT_TRUE(m.clients[0]->presence().cap_s >= 14 && m.clients[0]->presence().cap_s <= 16);      // about 15 s are left (Presence is a second old at the most): it says so
            ASSERT_TRUE(m.clients[1]->presence().cap_s >= 14 && m.clients[1]->presence().cap_s <= 16);
            m.run(14500);                                                                                  // 19.5 s into the pause
            ASSERT_TRUE(m.host->paused() && m.host->attendance().state(2) == Attendance::State::Absent);
            ASSERT_TRUE(m.clients[0]->presence().cap_s <= 1);
            uint32_t resumed_after_ms = 0;
            uint64_t drop_tick[3] = {0, 0, 0};
            m.run(3000, true, [&](uint32_t now) {
                if (resumed_after_ms == 0 && !m.host->paused()) resumed_after_ms = now - cut_at;
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(2)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[1]->is_player_dropped(2)) drop_tick[1] = m.sims[1]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(2)) drop_tick[2] = m.referee.current_tick();
            });
            ASSERT_TRUE(resumed_after_ms >= 20000 && resumed_after_ms <= 20100);                           // at the cap, to the pass
            ASSERT_EQ(m.host->attendance().drops_by_cap(), 1u);
            ASSERT_EQ(m.host->attendance().drops_by_vote(), 0u);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);
            m.run(3000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
            m.auto_reconnect[2] = true;                                                                    // its machine comes by: the seat is gone
            m.run(4000);
            ASSERT_TRUE(m.clients[2]->lost() && m.clients[2]->rejected() && m.clients[2]->reject_reason() == RejectReason::Dropped);
        }
        {   // the cap is ABSOLUTE: a seat that is catching up and progresses is dropped at the cap with the other seat that is not present (REWRITTEN: it used to keep its chance while it progressed)
            HoldOptions o;
            o.host.attendance.max_pause_ms = 20000;
            o.client.catch_up_ticks = 1;                                                                   // 100 turns a second
            HoldMatch m(4, o);
            m.run(150000);                                                                                 // 3000 turns: a catch-up of 30 s
            m.auto_reconnect[1] = false;
            m.auto_reconnect[2] = false;
            m.cut(1);
            m.cut(2);
            m.run(500);
            m.reload(1);                                                                                   // seat 1 comes back (slowly), seat 2 does not
            m.run(15000);
            ASSERT_EQ(m.host->attendance().state(1), Attendance::State::CatchingUp);                       // on its way, and getting on: 15 s into a catch-up of 30 s
            ASSERT_TRUE(m.host->attendance().percent(1) > 20 && m.host->attendance().percent(1) < 80);
            uint64_t drop_tick[3] = {0, 0, 0};
            const auto watch = [&](uint32_t) {
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(1)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[3]->is_player_dropped(1)) drop_tick[1] = m.sims[3]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(1)) drop_tick[2] = m.referee.current_tick();
            };
            ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 8000));                              // the cap: 20 s of pause in all
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);                          // the absent seat went at the cap ...
            ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);                          // ... and so did the one that was on its way, whatever progress it showed
            ASSERT_EQ(m.host->attendance().drops_by_cap(), 2u);
            ASSERT_EQ(m.host->attendance().rejoins(), 0u);
            ASSERT_EQ(m.host->rejoiners(), size_t{0});
            m.run(3000, true, watch);                                                                      // (the Drops are sealed in the first turn after the pause)
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.sims[0]->is_player_dropped(1) && m.sims[0]->is_player_dropped(2) && m.sims[3]->is_player_dropped(1) && m.sims[3]->is_player_dropped(2));
            ASSERT_TRUE(m.referee.is_player_dropped(1) && m.referee.is_player_dropped(2));
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);   // the same tick everywhere
            ASSERT_TRUE(m.all_equal());
            ASSERT_TRUE(m.clients[1] == nullptr || m.clients[1]->lost() || m.clients[1]->mode() != ClientSession::Mode::Normal);      // (the machine that was coming back is told that it is out)
        }
        {   // a catch-up that stops: let go after 20 s without progress, and then the cap takes the seat (the match has no pause left)
            HoldOptions o;
            o.host.attendance.max_pause_ms = 30000;
            o.client.catch_up_ticks = 1;
            HoldMatch m(3, o);
            m.run(150000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(500);
            m.reload(2);
            m.run(2000);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::CatchingUp);
            m.frozen_mask = 1u << 2;                                                                       // the machine hangs in the middle of its catch-up
            m.run(19000);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::CatchingUp);                       // 19 s without progress: still its chance
            m.run(2500);
            ASSERT_NE(static_cast<int>(m.host->attendance().state(2)), static_cast<int>(Attendance::State::CatchingUp));   // 20 s: let go
            m.run(10000);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);                          // the cap (30 s of pause) has passed meanwhile: dropped
            ASSERT_FALSE(m.host->paused());
        }
    } TEST_END();

    TEST_CASE("N2.51 Hold: Two Seats Gone: The Vote Is About The One That Has Been Away Longest, The Other Keeps The Match Waiting (Its Own Vote Starts Empty When It Is The Subject); The Seats Are Dropped One After The Other And The Match Goes On With The Two That Are Left") {
        HoldMatch m(4);
        m.run(4000);
        m.auto_reconnect[1] = false;
        m.auto_reconnect[2] = false;
        m.cut(1);
        const uint32_t cut1 = m.now;
        m.run(8000);
        m.cut(2);                                                                       // seat 2 follows 8 s later
        m.run(cut1 + 31500 - m.now);                                                    // 31.5 s after seat 1 was lost: it is away 30 s and more, seat 2 only 23.5 s
        for (uint8_t seat : {uint8_t{0}, uint8_t{3}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_EQ(p.missing.size(), size_t{2});
            ASSERT_TRUE(p.missing[0].seat == 1 && p.missing[1].seat == 2);               // longest away first
            ASSERT_TRUE(p.missing[0].waited_s >= 30 && p.missing[1].waited_s >= 22 && p.missing[1].waited_s <= 24);
            ASSERT_TRUE(p.vote_seat == 1 && p.voters == 2);
        }
        m.run(8000);                                                                    // seat 2 has been away 30 s too: the subject is still seat 1
        ASSERT_TRUE(m.clients[0]->presence().vote_seat == 1 && m.clients[0]->presence().missing[1].waited_s >= 30);
        ASSERT_TRUE(m.clients[0]->vote(1, true));
        ASSERT_TRUE(m.clients[3]->vote(1, true));                                       // two of two connected players: seat 1 is dropped
        m.run(1500);
        ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);
        ASSERT_TRUE(m.host->paused());                                                  // seat 2 is still away: the match waits on
        for (uint8_t seat : {uint8_t{0}, uint8_t{3}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_EQ(p.missing.size(), size_t{1});
            ASSERT_TRUE(p.missing[0].seat == 2 && p.vote_seat == 2);                     // the next subject, at once (it has been away more than 30 s) ...
            ASSERT_TRUE(p.votes_continue == 0 && p.your_vote == 0);                      // ... and its vote starts empty
        }
        ASSERT_FALSE(m.sims[0]->is_player_dropped(1));                                  // (the Drop of seat 1 waits for the end of the pause: nothing is sealed)
        ASSERT_TRUE(m.clients[0]->vote(2, true));
        ASSERT_TRUE(m.clients[3]->vote(2, true));
        ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 3000));
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
        ASSERT_EQ(m.host->attendance().drops_by_vote(), 2u);
        m.run(5000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (uint8_t seat : {uint8_t{0}, uint8_t{3}}) {                                 // both Drops travelled in the first turn after the pause: the same tick on both machines
            ASSERT_TRUE(m.sims[seat]->is_player_dropped(1) && m.sims[seat]->is_player_dropped(2));
            ASSERT_EQ(m.sims[seat]->current_tick(), m.referee.current_tick());
        }
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.52 Hold: A Wrong Key, No Key And A Key With Too Many Turns Are Refused (The First Two With \"The Match Has Started\": Nothing Is Revealed); A Second Window With The Key Of A Seat Whose Link Is Alive Takes It Over And The First Is Told It Was Superseded, A Newer Attempt Of A Seat Closes The Earlier One; Nobody Else Is Disturbed") {
        HoldMatch m(3);
        m.run(5000);
        const auto raw = [&](const SeatKey& key, uint32_t have) {
            auto ends = m.open_link();
            HelloMsg h;
            h.name = "Mallory";
            h.key = key;
            h.have_turns = have;
            ends.second->send(encode(h));
            return ends.second;
        };
        const auto answer = [&](Connection* c) {                                         // the Reject that the machine got, MsgType::None when nothing yet
            std::vector<uint8_t> msg;
            RejectMsg r;
            while (c->poll(msg)) {
                if (peek_type(msg) == MsgType::Reject && decode(msg, r)) return static_cast<int>(r.reason);
            }
            return 0;
        };
        Connection* wrong = raw(key_with(99), 0);
        Connection* none = raw(SeatKey{}, 0);
        Connection* too_many = raw(m.keys[1], m.sealed() + 500);
        m.run(300);
        ASSERT_EQ(answer(wrong), static_cast<int>(RejectReason::MatchRunning));
        ASSERT_EQ(answer(none), static_cast<int>(RejectReason::MatchRunning));
        ASSERT_EQ(answer(too_many), static_cast<int>(RejectReason::BadRequest));
        ASSERT_EQ(m.refusals, 3u);
        ASSERT_FALSE(m.host->paused());                                                  // nobody was disturbed: no seat is held, nobody is coming back
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        for (uint8_t p = 0; p < 3; ++p) ASSERT_TRUE(m.host->client_present(p) && m.host->attendance().state(p) == Attendance::State::Present);
        // a key that fits two seats' worth of nothing: the key of seat 1 with its first byte changed
        SeatKey almost = m.keys[1];
        almost[0] = static_cast<uint8_t>(almost[0] ^ 1);
        Connection* close_call = raw(almost, 0);
        m.run(300);
        ASSERT_EQ(answer(close_call), static_cast<int>(RejectReason::MatchRunning));
        ASSERT_FALSE(m.host->paused());
        // a second window with the key of seat 1, whose own link is alive (a duplicated tab, a Wi-Fi switch that the host has not noticed)
        Connection* second = raw(m.keys[1], m.sealed());
        m.run(400);
        ASSERT_TRUE(m.host->paused());                                                   // the seat is away from now: the match waits for the new window
        ASSERT_EQ(m.host->attendance().state(1), Attendance::State::CatchingUp);
        ASSERT_EQ(m.host->rejoiners(), size_t{1});
        ASSERT_FALSE(m.host->client_present(1));
        ASSERT_TRUE(m.clients[1]->lost() && m.clients[1]->rejected() && m.clients[1]->reject_reason() == RejectReason::Superseded);      // the first window was told, and stops trying
        ASSERT_FALSE(m.clients[1]->wants_connection(m.now));
        std::vector<uint8_t> msg;
        bool welcome = false, catch_up = false;
        while (second->poll(msg)) {
            WelcomeMsg w;
            CatchUpMsg c;
            if (peek_type(msg) == MsgType::Welcome && decode(msg, w)) welcome = (w.flags & kWelcomeRejoin) != 0 && w.player == 1 && w.key == m.keys[1];
            if (peek_type(msg) == MsgType::CatchUp && decode(msg, c)) catch_up = c.first_turn == c.total_turns;
        }
        ASSERT_TRUE(welcome && catch_up);                                                // the new window got its seat, its key and the (empty) stream
        // a newer attempt of the same seat closes the earlier one, and tells it
        Connection* third = raw(m.keys[1], m.sealed());
        m.run(300);
        ASSERT_EQ(answer(second), static_cast<int>(RejectReason::Superseded));
        ASSERT_FALSE(second->is_open());
        ASSERT_EQ(m.host->rejoiners(), size_t{1});
        ASSERT_TRUE(third->is_open());
        ASSERT_EQ(m.host->violations(0), 0u);
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
        {   // a session that has no Start message to give cannot take in a machine that has nothing (it could not tell it how to load the match): RejoinFailed, and the seat stays away; a
            // machine that has its turns needs no Start, and comes back as ever
            HoldOptions no_start;
            no_start.rejoin_start = false;
            HoldMatch n(3, no_start);
            n.run(4000);
            n.auto_reconnect[2] = false;
            n.cut(2);
            n.run(500);
            ASSERT_TRUE(n.host->paused());
            n.reload(2);
            n.run(1000);
            ASSERT_EQ(static_cast<int>(n.rejected_lobby[2]), static_cast<int>(RejectReason::RejoinFailed));
            ASSERT_EQ(n.host->attendance().state(2), Attendance::State::Absent);
            ASSERT_EQ(n.host->rejoiners(), size_t{0});
            ASSERT_TRUE(n.host->paused());
            HoldMatch k(3, no_start);
            k.run(4000);
            k.cut(1);
            ASSERT_TRUE(k.until([&]() { return !k.host->paused() && k.clients[1]->mode() == ClientSession::Mode::Normal; }, 8000));
            ASSERT_EQ(k.host->attendance().state(1), Attendance::State::Present);
        }
    } TEST_END();

    TEST_CASE("N2.53 Hold: A Machine Whose State Differs After The Catch-Up Does Not Get Its Seat Back: It Alone Is Told (Desync), The Room Does Not Fail, The Others Are Not Disturbed And The Seat Stays Away; A Good Machine With The Same Key Gets It Afterwards") {
        HoldMatch m(3);
        m.run(8000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        m.tamper_reloaded = [&m](sim::SimulationEngine& s) { s.get_unit(m.ids.ants[2][1]).hp = 3; };      // a hit point that no turn explains
        m.reload(2);
        m.run(3000);
        ASSERT_TRUE(m.host->desyncs().empty());                                          // the referee's record of the match has no desync: the room does not fail
        ASSERT_FALSE(m.host->frozen());
        ASSERT_TRUE(m.host->paused());
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);             // the seat stays away
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        ASSERT_TRUE(m.clients[2] != nullptr && m.clients[2]->desynced() && m.clients[2]->lost() && m.clients[2]->reject_reason() == RejectReason::RejoinFailed);
        ASSERT_EQ(m.clients[2]->desync().player, 2);
        ASSERT_TRUE(m.clients[2]->desync().host != m.clients[2]->desync().peer);
        ASSERT_FALSE(m.clients[0]->desynced() || m.clients[1]->desynced());              // nobody else was told
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(1));
        m.tamper_reloaded = nullptr;
        m.reload(2);                                                                     // a good machine, the same key
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2] != nullptr && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 10000));
        m.run(5000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.54 Hold: A Log That Is Full Falls Back To Dropping At Once (No Pause, The Key Is Told \"Dropped\"); Until It Is Full A Lost Seat Is Held") {
        HoldOptions o;
        o.host.max_log_bytes = 3000;
        HoldMatch m(3, o);
        m.run(2000);
        ASSERT_TRUE(m.host->log().usable());
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        ASSERT_TRUE(m.host->paused() && m.host->seat_held(2));                           // the log has room: held
        m.auto_reconnect[2] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 6000));
        m.run(40000);
        ASSERT_FALSE(m.host->log().usable());                                            // the 3000 bytes were used up (the index alone is 80 bytes a second)
        ASSERT_TRUE(m.host->log().bytes() == 0 && m.host->log().turns() == 0 && m.host->log().capacity_bytes() == 0);     // (REWRITTEN: a log that is full used to keep its 3000 bytes for the rest of the match; it is DEAD, it freed them the moment it died)
        m.auto_reconnect[1] = false;
        m.cut(1);
        uint64_t drop_tick[3] = {0, 0, 0};
        bool paused_ever = false;
        m.run(3000, true, [&](uint32_t) {
            paused_ever = paused_ever || m.host->paused();
            if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(1)) drop_tick[0] = m.sims[0]->current_tick();
            if (drop_tick[1] == 0 && m.sims[2]->is_player_dropped(1)) drop_tick[1] = m.sims[2]->current_tick();
            if (drop_tick[2] == 0 && m.referee.is_player_dropped(1)) drop_tick[2] = m.referee.current_tick();
        });
        ASSERT_FALSE(paused_ever);                                                       // dropped at once, as before there was a way back
        ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);
        ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);
        m.auto_reconnect[1] = true;
        m.run(4000);
        ASSERT_TRUE(m.clients[1]->lost() && m.clients[1]->reject_reason() == RejectReason::Dropped);       // its key (which survived the drop) is told so
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
        {   // a log that is not usable cannot give anybody the match: a second window with the key of a seat whose link is alive is refused (the first window is NOT replaced)
            auto ends = m.open_link();
            HelloMsg h;
            h.name = "Second";
            h.key = m.keys[2];
            h.have_turns = m.sealed();
            ends.second->send(encode(h));
            m.run(300);
            std::vector<uint8_t> msg;
            RejectMsg r;
            int reason = 0;
            while (ends.second->poll(msg)) {
                if (peek_type(msg) == MsgType::Reject && decode(msg, r)) reason = static_cast<int>(r.reason);
            }
            ASSERT_EQ(reason, static_cast<int>(RejectReason::RejoinFailed));
            ASSERT_TRUE(m.host->client_present(2) && m.host->rejoiners() == 0 && !m.host->paused());
            ASSERT_EQ(static_cast<int>(m.clients[2]->mode()), static_cast<int>(ClientSession::Mode::Normal));     // the window that plays was not told that it was replaced
        }
        {   // the log is the match, so a turn is appended BEFORE it is sent: a link that fails in the very send of the turn that does not fit the log is dropped at once (a seat that was
            // held with a log that had just become useless could never be given the match: it would wait for the cap)
            const uint32_t turn = 120;
            size_t before_bytes = 0;
            {
                HoldMatch probe(3);
                probe.run(10000, true, [&](uint32_t) {
                    if (before_bytes == 0 && probe.host->log().turns() == turn) before_bytes = probe.host->log().bytes();
                });
            }
            ASSERT_TRUE(before_bytes > 0);
            HoldOptions tight;
            tight.host.max_log_bytes = before_bytes + 1;                                 // turn 120 (at least 6 bytes) does not fit
            HoldMatch t(3, tight);
            t.taps[2]->fail_send_of_turn = turn;
            bool paused_at_all = false;
            t.run(8000, true, [&](uint32_t) { paused_at_all = paused_at_all || t.host->paused(); });
            ASSERT_FALSE(t.host->log().usable());
            ASSERT_TRUE(t.host->log().turns() == 0 && t.host->log().bytes() == 0);          // (dead: nothing is kept)
            ASSERT_FALSE(paused_at_all);
            ASSERT_EQ(t.host->attendance().state(2), Attendance::State::Dropped);
        }
        {   // a seat that has no key cannot be held: its lost link is a drop at once
            HoldOptions keyless;
            keyless.keys = false;
            HoldMatch k(3, keyless);
            k.run(3000);
            k.cut(1);
            bool keyless_paused = false;
            k.run(2000, true, [&](uint32_t) { keyless_paused = keyless_paused || k.host->paused(); });
            ASSERT_FALSE(keyless_paused);
            ASSERT_EQ(k.host->attendance().state(1), Attendance::State::Dropped);
            ASSERT_TRUE(k.clients[1]->lost() && !k.clients[1]->reconnecting());
        }
    } TEST_END();

    TEST_CASE("N2.55 Hold: Commands During A Pause Are Discarded Without A Violation (400 Of Them Do Not Cost A Player Its Seat), A Command That Cannot Be Decoded Still Does; ClientSession::submit Refuses While A Seat Is Missing And Works Again When The Match Runs") {
        HoldMatch m(3);
        m.run(4000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        ASSERT_TRUE(m.host->paused());
        Command c = cmd(CommandType::GroupMove, 0, 255, 10, 10, {m.ids.ants[0][0], m.ids.ants[0][1]});
        ASSERT_FALSE(m.clients[0]->submit(c));                                           // refused at the client: a seat is missing
        for (int i = 0; i < 400; ++i) {                                                  // ... and a client that sends them all the same (a page that did not know yet) is not punished
            CommandMsg msg;
            msg.command = c;
            m.client_ends[0]->send(encode(msg));
            if (i % 40 == 39) m.run(10);
        }
        m.run(500);
        ASSERT_EQ(m.host->violations(0), 0u);
        ASSERT_TRUE(m.host->client_present(0));
        const uint32_t sealed_in_pause = m.sealed();
        m.client_ends[0]->send(std::vector<uint8_t>{static_cast<uint8_t>(MsgType::Command), 1, 2, 3});     // not a command
        m.run(100);
        ASSERT_EQ(m.host->violations(0), 1u);                                            // a malformed message is an offence in a pause as at any time
        // the match runs again: a command is carried out, and is counted by the sequencer's limit like any
        m.auto_reconnect[2] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal && !m.clients[0]->paused(); }, 8000));
        ASSERT_TRUE(m.clients[0]->submit(c));
        m.run(3000);
        ASSERT_TRUE(m.sealed() > sealed_in_pause);
        ASSERT_EQ(m.host->violations(0), 1u);
        m.settle();
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.56 Hold: Leave Is Final: A Player That Quits While Another Seat Is Away Is Dropped At Once (Its Drop Waits For The End Of The Pause, The Same Tick Everywhere); A Player That Quits While It Is Coming Back Is Dropped And Its Key Is Told So") {
        {
            HoldMatch m(3);
            m.run(4000);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(1000);
            ASSERT_TRUE(m.host->paused());
            m.clients[2]->leave();                                                       // seat 2 quits during the pause
            m.run(500);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);        // final at once: not held, though its key is valid
            ASSERT_FALSE(m.host->client_present(2) || m.host->seat_held(2));
            ASSERT_TRUE(m.host->paused());                                               // seat 1 is still away
            ASSERT_FALSE(m.sims[0]->is_player_dropped(2));                               // nothing is sealed: the Drop waits
            m.auto_reconnect[1] = true;
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[1]->mode() == ClientSession::Mode::Normal; }, 8000));
            uint64_t drop_tick[3] = {0, 0, 0};
            m.run(3000, true, [&](uint32_t) {
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(2)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[1]->is_player_dropped(2)) drop_tick[1] = m.sims[1]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(2)) drop_tick[2] = m.referee.current_tick();
            });
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[1]->state_hash() == m.referee.state_hash());
        }
        {
            HoldOptions o;
            o.client.catch_up_ticks = 1;
            HoldMatch m(3, o);
            m.run(80000);                                                                // 1600 turns: a catch-up of 16 s
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(500);
            m.reload(2);
            m.run(3000);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::CatchingUp);
            m.clients[2]->leave();                                                       // it quits on its way back
            m.run(500);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
            ASSERT_EQ(m.host->rejoiners(), size_t{0});
            ASSERT_FALSE(m.host->paused());                                              // nobody else is away: the match goes on, the Drop is sealed
            m.run(3000);
            ASSERT_TRUE(m.sims[0]->is_player_dropped(2) && m.sims[1]->is_player_dropped(2));
            const SeatKey key = m.keys[2];
            m.reload(2, key);                                                            // a new page with the same key is told that the seat is gone
            m.run(2000);
            ASSERT_EQ(m.rejected_lobby[2], RejectReason::Dropped);
            m.settle();
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[1]->state_hash() == m.referee.state_hash());
        }
    } TEST_END();

    TEST_CASE("N2.57 Hold: Absences Add Up: A Seat That Was Away For 20.2 s, Came Back And Is Lost Again Is Put To The Vote 9.4 s Into The Second Absence (30 s In All), Not 30 s; Every Loss Counts At Least 5 s, And A Connection That Is Lost Three Times In A Minute Is Put To The Vote At Its Third Loss") {
        {
            HoldMatch m(3);
            m.run(4000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(20200);
            ASSERT_EQ(m.clients[0]->presence().vote_seat, 255);
            m.auto_reconnect[2] = true;                                                  // it comes back after 20.2 s
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 5000));
            const uint32_t first_absence = m.host->attendance().away_ms(2, m.now);
            ASSERT_TRUE(first_absence >= 20200 && first_absence <= 21500);              // the 20.2 s and the way back
            m.run(3000);
            m.auto_reconnect[2] = false;
            m.cut(2);                                                                    // lost again
            const uint32_t second_cut = m.now;
            const uint32_t missing = 30000 - m.host->attendance().away_ms(2, m.now);     // what the seat still needs to be away to be put to the vote
            m.run(missing - 1500);
            ASSERT_EQ(m.clients[0]->presence().vote_seat, 255);                          // not yet (the second absence alone is 8 s)
            m.run(3500);
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);
            ASSERT_TRUE(m.now - second_cut < 12000);                                     // about 9 s into the second absence: 30 s in all
        }
        {   // REWRITTEN (it was: six short absences of a second, each counting 5 s, put to the vote at the seventh loss): a connection that is lost three times within a minute FLAPS, and the vote
            // about it opens at its third loss, though it is back each time (the 5 s of every loss are still counted: N2.42, N2.43)
            HoldMatch m(3);
            m.run(4000);
            for (int i = 1; i <= 3; ++i) {
                m.cut(2);
                m.run(100);
                ASSERT_TRUE(m.host->paused());
                ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 5000));
                ASSERT_TRUE(m.host->attendance().away_ms(2, m.now) >= static_cast<uint32_t>(5000 * i));      // however short, a loss counts 5 s
                if (i < 3) ASSERT_EQ(m.clients[0]->presence().vote_seat, 255);                              // (two losses are no flapping)
                m.run(300);
            }
            ASSERT_TRUE(m.host->attendance().flapping(2, m.now));
            m.run(1500);
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);   // the vote about a seat that is back
            ASSERT_TRUE(m.host->client_present(2) && !m.host->paused());
        }
    } TEST_END();

    TEST_CASE("N2.58 Hold: Two Machines That Start From Nothing At Once Are Both Given The Match; It Resumes When The Last Of Them Is Back (And Not Before), Every Machine Ends Identical") {
        HoldMatch m(4);
        m.run(30000);
        m.auto_reconnect[1] = false;
        m.auto_reconnect[2] = false;
        m.cut(1);
        m.cut(2);
        m.run(1000);
        ASSERT_TRUE(m.host->paused() && m.host->attendance().state(1) == Attendance::State::Absent && m.host->attendance().state(2) == Attendance::State::Absent);
        m.reload(1);
        m.reload(2);
        bool both_catching_up = false;
        bool inconsistent = false;
        const auto back = [&](uint8_t seat) { return m.host->attendance().state(seat) == Attendance::State::Present; };
        ASSERT_TRUE(m.until([&]() {
            both_catching_up = both_catching_up || (m.host->rejoiners() == 2 && m.host->attendance().state(1) == Attendance::State::CatchingUp && m.host->attendance().state(2) == Attendance::State::CatchingUp);
            if (!m.host->paused() && !(back(1) && back(2))) inconsistent = true;           // the match runs while a seat is still on its way
            return !m.host->paused() && m.clients[1] && m.clients[2] && m.clients[1]->mode() == ClientSession::Mode::Normal && m.clients[2]->mode() == ClientSession::Mode::Normal;
        }, 20000));
        ASSERT_TRUE(both_catching_up);
        ASSERT_FALSE(inconsistent);
        ASSERT_EQ(m.host->attendance().rejoins(), 2u);
        m.run(5000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.59 Hold: A Catch-Up That Stops Is Let Go After 20 s Without Progress (The Seat Is Absent Again, Its Time Kept Running, The Others May Vote), And The Machine Gets Through On Its Second Try, From The Turns It Already Has") {
        HoldOptions o;
        o.client.catch_up_ticks = 1;
        HoldMatch m(3, o);
        m.run(150000);                                                                   // 3000 turns: a catch-up of 30 s at one turn per 10 ms
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        m.reload(2);
        m.run(8000);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::CatchingUp);
        const uint32_t executed_before = m.clients[2]->runner().next_turn_to_execute();
        ASSERT_TRUE(executed_before > 500 && executed_before < 2000);
        m.frozen_mask = 1u << 2;                                                         // it hangs
        const uint32_t frozen_at = m.now;
        uint32_t let_go_after_ms = 0;
        m.run(25000, true, [&](uint32_t now) {
            if (let_go_after_ms == 0 && m.host->attendance().state(2) == Attendance::State::Absent) let_go_after_ms = now - frozen_at;
        });
        ASSERT_TRUE(let_go_after_ms >= 19900 && let_go_after_ms <= 21500);              // 20 s without progress
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        ASSERT_TRUE(m.host->paused());
        ASSERT_TRUE(m.host->attendance().away_ms(2, m.now) >= 30000);                    // its time kept running: the others may vote now ...
        ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[0]->presence().missing[0].state == PresenceMsg::State::Absent);
        m.frozen_mask = 0;                                                               // ... but it is back: its session sees the closed link, asks again, and is given what it lacks
        m.auto_reconnect[2] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 40000));
        ASSERT_EQ(m.host->attendance().rejoins(), 1u);
        ASSERT_TRUE(m.clients[2]->runner().next_turn_to_execute() >= executed_before);
        m.run(3000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.60 Hold: What Presence Says To Each Player: Who Is Missing And For How Long (Longest Away First), The Vote's Count For Everybody And Each Player's Own Choice For That Player Only, The Voters, The Seconds Left Until The Cap (Counting Down), A Seat That Is Catching Up With Its Progress And Never As The Subject") {
        HoldMatch m(4);
        m.run(4000);
        m.auto_reconnect[3] = false;
        m.cut(3);
        m.run(31500);
        for (uint8_t seat = 0; seat < 3; ++seat) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_TRUE(p.missing.size() == 1 && p.missing[0].seat == 3 && p.vote_seat == 3 && p.voters == 3 && p.votes_continue == 0 && p.your_vote == 0);
            ASSERT_TRUE(p.missing[0].waited_s >= 30 && p.missing[0].waited_s <= 32);
        }
        const uint16_t cap_before = m.clients[0]->presence().cap_s;
        ASSERT_TRUE(m.clients[0]->vote(3, true));                                        // seat 0: continue
        m.run(200);                                                                      // (the periodic Presence is half a second away): a vote is answered at once, not at the next second
        ASSERT_TRUE(m.clients[0]->presence().your_vote == 2 && m.clients[0]->presence().votes_continue == 1 && m.clients[2]->presence().votes_continue == 1);
        ASSERT_TRUE(m.clients[1]->vote(3, false));                                       // seat 1: keep waiting; seat 2 says nothing
        m.run(3300);
        ASSERT_TRUE(m.clients[0]->presence().your_vote == 2 && m.clients[0]->presence().votes_continue == 1);       // each is told its own choice, everybody the count
        ASSERT_TRUE(m.clients[1]->presence().your_vote == 1 && m.clients[1]->presence().votes_continue == 1);
        ASSERT_TRUE(m.clients[2]->presence().your_vote == 0 && m.clients[2]->presence().votes_continue == 1);
        ASSERT_TRUE(m.clients[0]->presence().voters == 3 && m.clients[2]->presence().voters == 3);
        ASSERT_TRUE(m.clients[0]->presence().cap_s + 2 <= cap_before);                   // the cap counts down: 3.5 s later, at least 2 s less
        ASSERT_TRUE(m.clients[0]->presence().missing[0].waited_s > 31);                  // and the time away goes on
        ASSERT_TRUE(m.host->paused());                                                   // one of three is not more than half
        // a second seat is lost: the list is longest away first, and the vote is still about seat 3
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(1500);
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_TRUE(p.missing.size() == 2 && p.missing[0].seat == 3 && p.missing[1].seat == 2 && p.missing[0].waited_s > p.missing[1].waited_s);
            ASSERT_TRUE(p.vote_seat == 3 && p.voters == 2);                              // seat 2 is lost: it is no voter and its choice (it made none) is gone
        }
        ASSERT_EQ(m.clients[0]->presence().votes_continue, 1);                           // seat 0's choice stays: one of two connected players
        // a seat that comes back is catching up: shown with its progress, never the subject
        m.reload(2);
        bool catching = false;
        m.run(3000, true, [&](uint32_t) {
            for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
                for (const PresenceMsg::Entry& e : m.clients[seat]->presence().missing) {
                    if (e.seat == 2 && e.state == PresenceMsg::State::CatchingUp) {
                        catching = true;
                        ASSERT_TRUE(m.clients[seat]->presence().vote_seat != 2);
                    }
                }
            }
        });
        ASSERT_TRUE(catching);
    } TEST_END();

    TEST_CASE("N2.61 Hold: Chat Works During A Pause Between The Players Who Are There (Relayed By The Host, The Sender Stamped); The Absent Player's Machine Cannot Send, And What Was Said While It Was Away Is Not Replayed To It") {
        HoldMatch m(3);
        m.run(4000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(1000);
        ASSERT_TRUE(m.host->paused());
        ASSERT_TRUE(m.clients[0]->chat("anybody home?", false));
        m.run(300);
        ASSERT_TRUE(m.clients[1]->chat("only us", false));
        m.run(300);
        ASSERT_FALSE(m.clients[2]->chat("(from the dark)", false));                      // no link: nothing can be said
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
            ASSERT_EQ(m.chats[seat].size(), size_t{2});
            ASSERT_TRUE(m.chats[seat][0].sender == 0 && m.chats[seat][0].text == "anybody home?" && !m.chats[seat][0].team);
            ASSERT_TRUE(m.chats[seat][1].sender == 1 && m.chats[seat][1].text == "only us" && !m.chats[seat][1].team);
        }
        ASSERT_TRUE(m.chats[2].empty());
        m.auto_reconnect[2] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 8000));
        m.run(500);
        ASSERT_TRUE(m.chats[2].empty());                                                 // the chat of the pause is not replayed
        ASSERT_TRUE(m.clients[2]->chat("back", false));                                  // it can talk again
        m.run(500);
        ASSERT_TRUE(m.chats[0].size() == 3 && m.chats[0][2].sender == 2 && m.chats[1].size() == 3 && m.chats[2].size() == 1);
        ASSERT_EQ(m.host->violations(0), 0u);
    } TEST_END();

    TEST_CASE("N2.62 Hold: A Bot's Seat Is Never Absent, Never A Voter And Not Counted As Connected (The Vote Is Won By More Than Half Of The People); Its Orders Wait While The Match Is Paused; Nothing Is Held For It") {
        HoldOptions o;
        o.bot_mask = 1u << 3;
        HoldMatch m(4, o);
        m.run(4000);
        ASSERT_EQ(m.host->attendance().state(3), Attendance::State::Empty);              // no state at all: it is not at the table
        ASSERT_EQ(m.host->attendance().connected_humans(), 3);
        Command c = cmd(CommandType::GroupMove, 3, 255, 12, 12, {m.ids.ants[3][0]});
        ASSERT_TRUE(m.host->submit_bot(3, c));                                           // while the match runs a bot gives its orders
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(1000);
        ASSERT_TRUE(m.host->paused());
        ASSERT_FALSE(m.host->submit_bot(3, c));                                          // ... and waits like everybody while it is paused
        ASSERT_EQ(m.host->attendance().connected_humans(), 2);
        m.run(30000);
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_TRUE(p.missing.size() == 1 && p.missing[0].seat == 2);                // the bot is not in the list
            ASSERT_TRUE(p.vote_seat == 2 && p.voters == 2);                              // two people vote, the bot does not count: one of them is not more than half
        }
        ASSERT_TRUE(m.clients[0]->vote(2, true));
        m.run(1500);
        ASSERT_TRUE(m.host->paused());
        ASSERT_TRUE(m.clients[1]->vote(2, true));                                        // two of two people
        ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 3000));
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
        ASSERT_EQ(m.host->attendance().state(3), Attendance::State::Empty);              // still nothing for the bot
        ASSERT_TRUE(m.host->submit_bot(3, c));                                           // the match runs again: the bot plays on
        m.run(5000);
        ASSERT_FALSE(m.referee.is_player_dropped(3));
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.63 Hold: A Hostile Log Of The Biggest Turns (Two Players Put 40 Commands Of 32 Ants Into Every Turn: 5 MB In 25 s) Is Streamed To A Machine That Starts From Nothing Without Failing Its Link (A Link That Fails At 1 MB Of Backlog, As A WebSocket Does): The Stream Is Paced By What The Machine Has Executed, In Bytes") {
        HoldOptions o;
        o.host.max_log_bytes = 8u * 1024u * 1024u;
        HoldMatch m(3, o);
        m.audit_new_links = true;
        std::vector<uint32_t> many;                                                      // 32 ants: the most that a command names
        for (uint32_t i = 0; i < 32; ++i) many.push_back(1000 + i);
        m.run(25000, false, [&](uint32_t) {
            for (uint8_t p : {uint8_t{0}, uint8_t{1}}) {
                for (int i = 0; i < 8; ++i) {                                            // 8 a step of 10 ms: 40 a turn, 800 a second: under the budget of a connection, under the 64 of a turn
                    CommandMsg msg;
                    msg.command = cmd(CommandType::GroupMove, p, 255, 5, 5, many);
                    m.client_ends[p]->send(encode(msg));
                }
            }
        });
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(1));            // (the flood budget was not met)
        ASSERT_TRUE(m.host->log().usable());
        const size_t log_bytes = m.host->log().bytes();
        ASSERT_TRUE(log_bytes > 3u * 1024u * 1024u && log_bytes < 8u * 1024u * 1024u);
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        ASSERT_TRUE(m.host->paused());
        m.reload(2);
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2] != nullptr && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 60000));
        ASSERT_EQ(m.audits.size(), size_t{1});
        ASSERT_FALSE(m.audits[0]->failed);                                               // the link never failed
        std::cout << "\n      [reconnect] a hostile log of " << log_bytes << " bytes (" << m.host->log().turns() << " turns) was streamed over a link that fails at 1 MB: " << m.audits[0]->sent
                  << " bytes sent, at most " << m.audits[0]->peak << " in flight at any moment" << std::flush;
        ASSERT_TRUE(m.audits[0]->sent > 3u * 1024u * 1024u);                             // the whole log went over it ...
        ASSERT_TRUE(m.audits[0]->peak < 400u * 1024u);                                   // ... never more than the window (256 KB) and a batch (48 KB) in flight, a third of what fails a WebSocket
        ASSERT_EQ(m.host->attendance().rejoins(), 1u);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.64 Client: A Reject Ends The Session For Good In Every Mode (Normal, Normal With The Close Behind It, Rejoining, CatchingUp), Whatever The Reason (1 - 9), With The Reason And No More Attempts; A Link That Only Closes Is Not The End; A Session That Does Not Reconnect Ignores A Reject As Before") {
        for (int r = 1; r <= 9; ++r) {
            const RejectReason reason = static_cast<RejectReason>(r);
            const auto ended = [&](LoneSession& L) {
                return L.s->lost() && L.s->rejected() && L.s->reject_reason() == reason && !L.s->wants_connection(L.now + 100000) && !L.s->submit(cmd(CommandType::Hatch, 1)) &&
                       !L.s->vote(0, true) && !L.s->chat("x", false) && L.s->mode() == ClientSession::Mode::Lost;
            };
            {   // Normal: the server says no on the live link
                LoneSession L;
                L.step(100);
                ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Normal));
                L.first().srv->send(encode(RejectMsg{reason}));
                L.step(100);
                ASSERT_TRUE(ended(L));
                ASSERT_FALSE(L.first().cli->is_open());                                 // its link was closed by the session
            }
            {   // Normal, the Reject and the close in the same breath (the host says "superseded" just before it closes the old link): the word is read before the close is judged
                LoneSession L;
                L.step(100);
                L.first().srv->send(encode(RejectMsg{reason}));
                L.first().srv->close();
                L.step(100);
                ASSERT_TRUE(ended(L));
            }
            {   // Rejoining: the link was lost, a new one says Hello, the answer is a Reject
                LoneSession L;
                L.step(100);
                L.lose_link();
                ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));
                ASSERT_TRUE(L.s->wants_connection(L.now));
                LoneSession::Wire& w = L.attach_new();
                L.step(100);
                ASSERT_EQ(w.count(MsgType::Hello), size_t{1});
                ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Rejoining));
                w.srv->send(encode(RejectMsg{reason}));
                L.step(100);
                ASSERT_TRUE(ended(L));
                ASSERT_FALSE(w.cli->is_open());
            }
            {   // CatchingUp: the Welcome and the stream's announcement came, then a Reject
                LoneSession L;
                L.step(100);
                L.lose_link();
                LoneSession::Wire& w = L.attach_new();
                L.step(100);
                w.srv->send(encode(L.rejoin_welcome()));
                w.srv->send(encode(CatchUpMsg{0, 100}));
                L.step(100);
                ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
                w.srv->send(encode(RejectMsg{reason}));
                L.step(100);
                ASSERT_TRUE(ended(L));
            }
        }
        {   // a link that only closes (no word from the server) is not the end: the machine is on its way back, and it was not "rejected"
            LoneSession L;
            L.step(100);
            L.lose_link();
            ASSERT_TRUE(!L.s->lost() && !L.s->rejected() && L.s->reconnecting() && L.s->wants_connection(L.now));
        }
        {   // a session that does not reconnect (a LAN guest, a room that holds no seats) ignores a Reject on its live link, as it always did
            LoneSession L(ClientSession::Config{}, false);
            L.step(100);
            L.first().srv->send(encode(RejectMsg{RejectReason::Superseded}));
            L.step(100);
            ASSERT_TRUE(!L.s->lost() && !L.s->rejected() && L.s->mode() == ClientSession::Mode::Normal);
            L.first().srv->close();                                                      // ... and a closed link is the end of its match as before
            L.step(100);
            ASSERT_TRUE(L.s->lost() && !L.s->rejected());
        }
        {   // a session that has no key has no way back: a lost link is the end, at once, without an attempt (reconnect is on, the key is zero)
            LoneSession L;
            L.step(100);
            sim::SimulationEngine other;
            build_world(other, 1);
            ClientSession::Config nk;
            nk.player = 1;
            nk.host = kNoSeat;
            nk.migration = false;
            nk.reconnect = true;
            ClientSession keyless(other, nk);
            auto ends = L.net.connect({10, 0});
            keyless.set_connection(ends.second);
            keyless.start(L.now);
            ends.first->close();
            L.net.set_time(L.now + 20);
            keyless.update(L.now + 20);
            ASSERT_TRUE(keyless.lost() && !keyless.reconnecting() && !keyless.wants_connection(L.now + 100000));
        }
    } TEST_END();

    TEST_CASE("N2.65 Client: The Way Back: A New Link Is Asked For At Once And Again Every 2 s (attach(nullptr): No Link Could Be Made), The Hello Goes Out When The Link OPENS (Not Before: A TCP Link Is Connecting For A While), Once, With The Key And The Turns The Machine Has; The Give-Up Time Counts From The First Loss Through Every Attempt; An Attempt With No Welcome Is Abandoned; Leaving Or Finishing Ends The Attempts") {
        {   // the Hello of a machine that comes back, and when it goes out
            LoneSession L;
            L.step(100);
            L.give_turns(L.first(), 10);
            L.step(600);
            ASSERT_EQ(L.s->runner().next_turn_expected(), 10u);
            L.lose_link();
            ASSERT_TRUE(L.s->wants_connection(L.now));
            auto ends = L.net.connect({10, 0});
            OpensLater later(ends.second, &L.now, L.now + 300);                         // the link opens in 300 ms
            L.wires.push_back(std::make_unique<LoneSession::Wire>());
            L.wires.back()->srv = ends.first;
            L.wires.back()->cli = &later;
            L.s->attach(&later, L.now);
            L.step(250);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Rejoining));
            ASSERT_EQ(L.last().count(MsgType::Hello), size_t{0});                        // nothing was written to a link that was not open
            L.step(150);
            ASSERT_EQ(L.last().count(MsgType::Hello), size_t{1});                        // it went out when the link opened ...
            L.step(300);
            ASSERT_EQ(L.last().count(MsgType::Hello), size_t{1});                        // ... once
            HelloMsg h;
            ASSERT_TRUE(decode(L.last().heard[0], h));
            ASSERT_TRUE(h.version == kProtocolVersion && h.key == L.key && h.have_turns == 10 && h.name == "Lone" && h.room == "R-1" && h.token == "tok");
        }
        {   // the Hello says how many turns the machine has RECEIVED (run or not), not how many it has run: the host's stream starts after the last one that the machine holds
            LoneSession L;
            L.step(100);
            L.give_turns(L.first(), 40);
            L.step(60);                                                                  // they arrive; the runner has run only some of them
            ASSERT_EQ(L.s->runner().next_turn_expected(), 40u);
            ASSERT_TRUE(L.s->runner().next_turn_to_execute() < 40u);
            L.lose_link();
            ASSERT_TRUE(L.s->runner().next_turn_to_execute() < 40u);
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            HelloMsg h;
            ASSERT_TRUE(!w.heard.empty() && decode(w.heard[0], h));
            ASSERT_EQ(h.have_turns, 40u);
        }
        {   // attempts: at once, then 2 s after the last one began; attach(nullptr) counts as an attempt that could not start
            LoneSession L;
            L.step(100);
            L.lose_link();
            const uint32_t lost_at = L.now;
            ASSERT_TRUE(L.s->wants_connection(L.now));
            ASSERT_EQ(L.s->lost_since_ms(), L.now - 10);                                 // (it noticed in its update of the step before the last)
            std::vector<uint32_t> times;
            for (int attempt = 0; attempt < 3; ++attempt) {
                while (!L.s->wants_connection(L.now)) L.step(10);
                times.push_back(L.now);
                if (attempt == 1) {
                    L.s->attach(nullptr, L.now);                                         // no link could be made
                } else {
                    auto ends = L.net.connect({10, 0});
                    L.net.cut(ends.second);                                              // a link that is refused at once
                    L.s->attach(ends.second, L.now);
                }
                ASSERT_FALSE(L.s->wants_connection(L.now));
                L.step(20);
            }
            ASSERT_TRUE(times[0] - lost_at <= 20);
            ASSERT_TRUE(times[1] - times[0] >= 1990 && times[1] - times[0] <= 2030);
            ASSERT_TRUE(times[2] - times[1] >= 1990 && times[2] - times[1] <= 2030);
            ASSERT_EQ(L.s->reconnect_attempts(), 3u);
            ASSERT_EQ(L.s->lost_since_ms(), lost_at - 10);                               // the first loss: it did not move
        }
        {   // the give-up time (here 7 s) counts from the first loss, through every attempt
            ClientSession::Config c;
            c.reconnect_give_up_ms = 7000;
            LoneSession L(c);
            L.step(100);
            L.lose_link();
            const uint32_t lost_at = L.now;
            while (!L.s->lost() && L.now - lost_at < 20000) {
                if (L.s->wants_connection(L.now)) {
                    auto ends = L.net.connect({10, 0});
                    L.net.cut(ends.second);
                    L.s->attach(ends.second, L.now);
                }
                L.step(10);
            }
            ASSERT_TRUE(L.s->lost() && !L.s->rejected());                                // no word from the server: just no way back
            ASSERT_TRUE(L.now - lost_at >= 7000 && L.now - lost_at <= 7100);
            ASSERT_TRUE(L.s->reconnect_attempts() >= 3 && L.s->reconnect_attempts() <= 5);
            ASSERT_FALSE(L.s->wants_connection(L.now + 100000));
        }
        {   // an attempt that gets no Welcome (here within 3 s) is abandoned: the link is closed, the next attempt is due at once (the attempt lasted more than 2 s)
            ClientSession::Config c;
            c.rejoin_timeout_ms = 3000;
            LoneSession L(c);
            L.step(100);
            L.lose_link();
            LoneSession::Wire& w = L.attach_new();                                       // the server never answers
            L.step(2900);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Rejoining));
            L.step(200);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));
            ASSERT_FALSE(w.cli->is_open());
            ASSERT_TRUE(L.s->wants_connection(L.now));
        }
        {   // a link lost in the middle of the catch-up: back to Reconnecting, the give-up clock keeps the first loss, the next Hello says how many turns the machine has by now
            LoneSession L;
            L.step(100);
            L.lose_link();
            const uint32_t first_loss = L.s->lost_since_ms();
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            w.srv->send(encode(L.rejoin_welcome()));
            w.srv->send(encode(CatchUpMsg{0, 300}));
            TurnBatchMsg b;
            b.first_turn = 0;
            for (uint32_t i = 0; i < 120; ++i) {
                TurnMsg t;
                t.turn = i;
                b.turns.push_back(t);
            }
            w.srv->send(encode(b));
            L.step(30);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
            L.net.cut(w.cli);                                                            // the link dies with 120 of the 300 turns delivered
            L.step(50);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));
            ASSERT_EQ(L.s->lost_since_ms(), first_loss);
            L.step(2100);
            ASSERT_TRUE(L.s->wants_connection(L.now));
            LoneSession::Wire& w2 = L.attach_new();
            L.step(100);
            HelloMsg h;
            ASSERT_EQ(w2.count(MsgType::Hello), size_t{1});
            ASSERT_TRUE(decode(w2.heard[0], h) && h.have_turns == 120 && h.key == L.key);
        }
        {   // leave() in each mode of the way back: no more attempts; the Leave goes out when there is a link
            for (int stage = 0; stage < 3; ++stage) {
                LoneSession L;
                L.step(100);
                L.lose_link();
                LoneSession::Wire* w = nullptr;
                if (stage >= 1) {
                    w = &L.attach_new();
                    L.step(100);
                }
                if (stage == 2) {
                    w->srv->send(encode(L.rejoin_welcome()));
                    w->srv->send(encode(CatchUpMsg{0, 50}));
                    L.step(50);
                    ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
                }
                L.s->leave();
                L.step(100);
                ASSERT_TRUE(L.s->lost() && !L.s->wants_connection(L.now + 100000));
                if (w != nullptr) ASSERT_EQ(w->count(MsgType::Leave), size_t{1});
            }
        }
        {   // finish(): the match is over, the server closing its links is no reason to come back
            LoneSession L;
            L.step(100);
            L.s->finish();
            L.first().srv->close();
            L.step(100);
            ASSERT_FALSE(L.s->lost() || L.s->reconnecting());
            ASSERT_FALSE(L.s->wants_connection(L.now + 100000));
        }
        {   // a machine that was on its way back when the match ended (the owner finished the session): no link that is handed over after that is taken, no Hello goes out
            LoneSession L;
            L.step(100);
            L.lose_link();
            ASSERT_TRUE(L.s->reconnecting());
            L.s->finish();
            ASSERT_FALSE(L.s->wants_connection(L.now + 100000));
            LoneSession::Wire& w = L.attach_new();
            L.step(200);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));      // the link was not taken
            ASSERT_EQ(w.count(MsgType::Hello), size_t{0});
            ASSERT_EQ(L.s->reconnect_attempts(), 0u);
        }
    } TEST_END();

    TEST_CASE("N2.66 Client: The Catch-Up Through A Scripted Server: The Stream Is Run At Once And Silently, Acknowledged At Every Higher Percent And When Its Queue Is Empty, CaughtUp Carries The State After Exactly The Announced Turns, The Machine Waits For The Server's Presence (Or A Live Turn) Before It Plays; Every Protocol Failure Ends It With \"Rejoin Failed\"; The Gating Of submit, chat And vote") {
        {   // the whole flow: 50 turns before the loss, 350 given now
            ClientSession::Config c;
            c.catch_up_ticks = 100;
            LoneSession L(c);
            L.step(100);
            L.give_turns(L.first(), 50);
            L.step(300);
            L.lose_link();
            ASSERT_EQ(L.s->runner().next_turn_expected(), 50u);
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            HelloMsg h;
            ASSERT_TRUE(decode(w.heard[0], h) && h.have_turns == 50);
            w.srv->send(encode(L.rejoin_welcome()));
            w.srv->send(encode(CatchUpMsg{50, 400}));
            for (uint32_t first = 50; first < 400; first += 100) {                       // four batches of at most 100 empty turns
                TurnBatchMsg b;
                b.first_turn = first;
                for (uint32_t t = first; t < std::min<uint32_t>(first + 100, 400); ++t) {
                    TurnMsg tm;
                    tm.turn = t;
                    b.turns.push_back(tm);
                }
                w.srv->send(encode(b));
            }
            L.step(40);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
            ASSERT_FALSE(L.s->submit(cmd(CommandType::Hatch, 1)));                       // nothing is given while it catches up
            ASSERT_FALSE(L.s->chat("x", false));
            ASSERT_FALSE(L.s->vote(0, true));
            L.step(200);
            std::vector<uint32_t> acks;
            bool caught_up = false;
            CaughtUpMsg claim;
            for (const auto& m : w.heard) {
                AckMsg a;
                if (peek_type(m) == MsgType::TurnAck && decode(m, a)) acks.push_back(a.turn);
                if (peek_type(m) == MsgType::CaughtUp) caught_up = decode(m, claim);
            }
            ASSERT_FALSE(acks.empty());
            for (size_t i = 1; i < acks.size(); ++i) ASSERT_TRUE(acks[i] > acks[i - 1]);   // every acknowledgement is further than the one before
            ASSERT_EQ(acks.back(), 399u);
            ASSERT_TRUE(caught_up && claim.turns == 400 && claim.hash == L.hash_after(400));        // the state after exactly those turns, and nothing was drawn on the way
            ASSERT_EQ(L.sim.current_tick(), 400u);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));      // it waits for the server's word
            ASSERT_EQ(L.s->catch_up_percent(), 100);
            ASSERT_EQ(w.count(MsgType::CaughtUp), size_t{1});
            ASSERT_FALSE(L.s->submit(cmd(CommandType::Hatch, 1)));
            PresenceMsg here;                                                            // the server compared the states: the match is paused for somebody else
            here.missing.push_back(PresenceMsg::Entry{2, PresenceMsg::State::Absent, 40, 0});
            here.voters = 1;
            w.srv->send(encode(here));
            L.step(60);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Normal));
            ASSERT_TRUE(L.s->paused() && L.s->presence().missing.size() == 1);
            ASSERT_FALSE(L.s->submit(cmd(CommandType::Hatch, 1)));                       // a seat is missing: nothing can be given, the host would discard it
            ASSERT_TRUE(L.s->chat("hello", false));                                      // chat goes on
            ASSERT_TRUE(L.s->vote(2, true));
            w.srv->send(encode(PresenceMsg{}));                                          // the match runs
            L.step(60);
            ASSERT_FALSE(L.s->paused());
            ASSERT_TRUE(L.s->submit(cmd(CommandType::Hatch, 1)));
        }
        {   // a live turn instead of a Presence confirms too
            LoneSession L;
            L.step(100);
            L.lose_link();
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            w.srv->send(encode(L.rejoin_welcome()));
            w.srv->send(encode(CatchUpMsg{0, 0}));
            L.step(60);
            ASSERT_EQ(w.count(MsgType::CaughtUp), size_t{1});
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
            TurnMsg t;
            t.turn = 0;
            w.srv->send(encode(t));
            L.step(60);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Normal));
            ASSERT_EQ(L.s->runner().next_turn_expected(), 1u);
        }
        {   // a server that thinks the machine has nothing (no turns when it said Hello) sends Start: the map is loaded, it says so once, and the stream follows
            LoneSession L;
            L.step(100);
            L.lose_link();
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            w.srv->send(encode(L.rejoin_welcome()));
            StartMsg start;
            start.map_name = "TEST.LVL";
            start.roster = 7;
            w.srv->send(encode(start));
            L.step(60);
            ASSERT_EQ(w.count(MsgType::Loaded), size_t{1});
            w.srv->send(encode_begin());
            w.srv->send(encode(CatchUpMsg{0, 0}));
            L.step(60);
            ASSERT_EQ(w.count(MsgType::Loaded), size_t{1});
            ASSERT_EQ(w.count(MsgType::CaughtUp), size_t{1});
        }
        {   // pings go on while it catches up (a link that is quiet for 10 s is a link that is gone)
            ClientSession::Config c;
            c.catch_up_ticks = 1;
            LoneSession L(c);
            L.step(100);
            L.lose_link();
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            w.srv->send(encode(L.rejoin_welcome()));
            w.srv->send(encode(CatchUpMsg{0, 1000}));
            TurnBatchMsg b;
            for (uint32_t t = 0; t < 1000; ++t) {
                TurnMsg tm;
                tm.turn = t;
                b.turns.push_back(tm);
            }
            w.srv->send(encode(b));
            L.step(3500);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
            ASSERT_TRUE(w.count(MsgType::Ping) >= 3);
            ASSERT_TRUE(L.s->catch_up_percent() >= 30 && L.s->catch_up_percent() <= 40);       // 350 of 1000 turns at 100 a second
            ASSERT_EQ(w.count(MsgType::CaughtUp), size_t{0});
            w.srv->send(encode(PresenceMsg{}));                                          // "the match runs", said to a machine that has not said CaughtUp: it is not back yet ...
            TurnMsg live;
            live.turn = 1000;
            w.srv->send(encode(live));                                                   // ... and a live turn does not bring it back either
            L.step(100);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
            ASSERT_TRUE(L.s->catch_up_percent() < 100);
        }
        {   // the protocol failures: each ends it, with "rejoin failed" (an honest server does none of them)
            enum Fail { NoRejoinFlag, CatchUpFromTheWrongTurn, BatchFromTheWrongTurn, BatchBeyondTheTotal, SecondCatchUp, BatchBeforeCatchUp, DesyncVerdict };
            for (int f = 0; f <= DesyncVerdict; ++f) {
                LoneSession L;
                L.step(100);
                L.give_turns(L.first(), 5);
                L.step(100);
                L.lose_link();
                LoneSession::Wire& w = L.attach_new();
                L.step(100);
                WelcomeMsg welcome = L.rejoin_welcome();
                if (f == NoRejoinFlag) welcome.flags = 0;
                w.srv->send(encode(welcome));
                const auto batch = [&](uint32_t first, uint32_t n) {
                    TurnBatchMsg b;
                    b.first_turn = first;
                    for (uint32_t t = first; t < first + n; ++t) {
                        TurnMsg tm;
                        tm.turn = t;
                        b.turns.push_back(tm);
                    }
                    return encode(b);
                };
                if (f == CatchUpFromTheWrongTurn) w.srv->send(encode(CatchUpMsg{3, 100}));
                else if (f == BatchBeforeCatchUp) w.srv->send(batch(5, 5));
                else w.srv->send(encode(CatchUpMsg{5, 100}));
                if (f == BatchFromTheWrongTurn) w.srv->send(batch(6, 5));
                if (f == BatchBeyondTheTotal) w.srv->send(batch(5, 200));
                if (f == SecondCatchUp) w.srv->send(encode(CatchUpMsg{5, 100}));
                if (f == DesyncVerdict) {
                    DesyncMsg d;
                    d.turn = 100;
                    d.player = 1;
                    w.srv->send(encode(d));
                }
                L.step(100);
                ASSERT_TRUE(L.s->lost() && L.s->rejected() && L.s->reject_reason() == RejectReason::RejoinFailed);
                ASSERT_EQ(L.s->desynced(), f == DesyncVerdict);
            }
        }
    } TEST_END();

    TEST_CASE("N2.67 Hold: Lag Is Not A Loss In A Room That Holds Seats: A Window That Stops For 5 s, And One That Stops For 9 s, Do Not Pause Anybody (The Others Are Told \"Bob Is Lagging\", It Catches Up At 4x); A Client That Keeps Talking But Whose Acknowledgements Never Arrive Is Told To The Others As Lagging And Dropped After 30 s Without Progress (Dropped For Good, No Pause); One That Is 60 s Behind Is Dropped Too: The Rules Of v0.0.94") {
        for (const uint32_t frozen_ms : {5000u, 9000u}) {
            HoldMatch m(3);
            m.run(3000);
            const uint32_t sealed_before = m.sealed();
            m.frozen_mask = 1u << 2;
            bool paused_ever = false;
            bool noticed = false;
            bool presence_ever = false;
            m.run(frozen_ms, true, [&](uint32_t) {
                paused_ever = paused_ever || m.host->paused();
                noticed = noticed || m.clients[0]->lagging_seat() == 2;
                presence_ever = presence_ever || m.clients[0]->paused() || m.clients[1]->paused();
            });
            ASSERT_FALSE(paused_ever || presence_ever);                                  // nobody is paused for a window that stopped
            ASSERT_TRUE(noticed);                                                        // they are told who lags (from 3 s behind)
            ASSERT_TRUE(m.sealed() - sealed_before >= frozen_ms / 50 - 2);               // the server kept sealing, every 50 ms
            ASSERT_TRUE(m.host->client_present(2) && !m.host->seat_held(2));
            m.frozen_mask = 0;
            m.run(10000, true, [&](uint32_t) { paused_ever = paused_ever || m.host->paused(); });
            ASSERT_FALSE(paused_ever);
            ASSERT_TRUE(m.clients[2]->mode() == ClientSession::Mode::Normal && m.host->client_present(2));
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // it talks (pings, a second apart) but its acknowledgements and hashes never reach the host: the host cannot see it run
            HoldMatch m(3);
            m.run(3000);
            m.taps[1]->swallow_acks = true;
            const uint32_t swallowed_at = m.now;
            bool paused_ever = false;
            bool noticed = false;
            uint32_t dropped_after_ms = 0;
            m.run(40000, true, [&](uint32_t now) {
                paused_ever = paused_ever || m.host->paused();
                noticed = noticed || m.clients[0]->lagging_seat() == 1;
                if (dropped_after_ms == 0 && m.host->attendance().state(1) == Attendance::State::Dropped) dropped_after_ms = now - swallowed_at;
            });
            ASSERT_FALSE(paused_ever);                                                   // it is connected: that is lag, never a pause
            ASSERT_TRUE(noticed);
            ASSERT_TRUE(dropped_after_ms >= 29900 && dropped_after_ms <= 31000);         // 30 s without progress, then a drop (final: the seat is not held)
            ASSERT_FALSE(m.host->seat_held(1));
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[2]->state_hash() == m.referee.state_hash());
            ASSERT_TRUE(m.sims[0]->is_player_dropped(1) && m.sims[2]->is_player_dropped(1));
            ASSERT_TRUE(m.clients[1]->lost() && m.clients[1]->reject_reason() == RejectReason::Dropped);        // its machine came back with the key and was told
        }
        {   // one acknowledgement every 25 s: a laggard that makes progress (never 30 s without one, and each one says how far it really is). Told to the others, never dropped, never a pause
            HoldMatch m(3);
            m.run(3000);
            m.taps[1]->swallow_acks = true;
            m.taps[1]->let_one_ack_through_every_ms = 25000;
            bool paused_ever = false;
            m.run(100000, true, [&](uint32_t) { paused_ever = paused_ever || m.host->paused(); });
            ASSERT_FALSE(paused_ever);
            ASSERT_TRUE(m.host->client_present(1) && !m.host->seat_held(1));
        }
        {   // 60 s behind with progress (an acknowledgement of one more turn every 29 s, and a ping every second): dropped at 60 s of it, in a room that holds seats, and never a pause
            LoopbackNetwork net{3};
            sim::SimulationEngine referee;
            build_world(referee, 1);
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            hc.hold_seats = true;
            HostSession host(referee, hc);
            std::array<SeatKey, sim::MAX_PLAYERS> keys{};
            keys[0] = key_with(1);
            keys[1] = key_with(2);
            host.set_seat_keys(keys);
            StartMsg st;
            st.map_name = "TEST.LVL";
            st.roster = 3;
            host.set_rejoin_start(st);
            auto e0 = net.connect({5, 0});
            auto e1 = net.connect({5, 0});
            host.add_client(0, e0.first);
            host.add_client(1, e1.first);
            host.start(0);
            uint32_t now = 0;
            uint32_t last_turn = 0;
            bool got_turn = false;
            uint32_t next_ack = 29000;
            uint32_t acked = 0;
            uint32_t dropped_at = 0;
            bool paused_ever = false;
            while (now < 80000 && dropped_at == 0) {
                now += 10;
                net.set_time(now);
                host.update(now);
                std::vector<uint8_t> msg;
                while (e0.second->poll(msg)) {
                    TurnMsg t;
                    if (peek_type(msg) == MsgType::Turn && decode(msg, t)) {
                        last_turn = t.turn;
                        got_turn = true;
                    }
                }
                while (e1.second->poll(msg)) {}
                if (got_turn) {
                    AckMsg a;
                    a.turn = last_turn;
                    e0.second->send(encode(a));
                }
                if (now + 10 >= next_ack) {
                    AckMsg a;
                    a.turn = ++acked;
                    e1.second->send(encode(a));
                    next_ack += 29000;
                }
                if (now % 1000 == 0) e1.second->send(encode_ping(PingMsg{now / 1000 + 1, now}));
                paused_ever = paused_ever || host.paused();
                if (!host.client_present(1)) dropped_at = now;
            }
            ASSERT_TRUE(dropped_at >= 59900 && dropped_at <= 60300);
            ASSERT_FALSE(paused_ever);
            ASSERT_EQ(host.attendance().state(1), Attendance::State::Dropped);
            ASSERT_TRUE(host.client_present(0));
        }
    } TEST_END();

    TEST_CASE("N2.68 Hold: Silence Is A Loss After 10 s (A Window That Hangs, A Link That Is Half Open And Delivers Nothing): The Match Pauses 10 s After The Last Word, The Others Read \"missing\" And Not \"lagging\"; 9 s Of Silence Is No Loss; The Machine That Comes Back Finds Its Link Closed And Gets Its Seat Back") {
        {
            HoldMatch m(3);
            m.run(4000);
            m.frozen_mask = 1u << 1;                                                     // the process hangs: it neither answers nor pings, its link stays open
            const uint32_t frozen_at = m.now;
            uint32_t paused_after_ms = 0;
            bool lag_notice = false;
            m.run(9500, true, [&](uint32_t now) {
                if (paused_after_ms == 0 && m.host->paused()) paused_after_ms = now - frozen_at;
                lag_notice = lag_notice || m.clients[0]->lagging_seat() == 1;
            });
            ASSERT_EQ(paused_after_ms, 0u);                                              // 9.5 s: not yet
            ASSERT_TRUE(lag_notice);                                                     // (for 6 s of it the others were told that it lags: it has stopped running turns)
            m.run(1000, true, [&](uint32_t now) {
                if (paused_after_ms == 0 && m.host->paused()) paused_after_ms = now - frozen_at;
            });
            ASSERT_TRUE(paused_after_ms >= 10000 && paused_after_ms <= 10100);          // 10 s after the last thing it said (the last ack or ping before it hung)
            m.run(300);
            ASSERT_TRUE(m.host->seat_held(1) && m.host->attendance().state(1) == Attendance::State::Absent);
            ASSERT_TRUE(m.clients[0]->presence().missing.size() == 1 && m.clients[0]->presence().missing[0].seat == 1);      // "missing", not "lagging":
            ASSERT_EQ(m.clients[0]->lagging_seat(), 255);                                 // the notice of lag ended with the loss
            m.run(5000);
            m.frozen_mask = 0;                                                           // the machine wakes up: its link was closed by the host, it asks for a new one
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[1]->mode() == ClientSession::Mode::Normal; }, 8000));
            m.run(3000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
            ASSERT_EQ(m.host->attendance().rejoins(), 1u);
        }
        {   // a half-open link: the host hears nothing, the machine hears everything and thinks that all is well
            HoldMatch m(3);
            m.run(4000);
            m.taps[2]->silent = true;
            const uint32_t at = m.now;
            uint32_t paused_after_ms = 0;
            m.run(9500, true, [&](uint32_t now) {
                if (paused_after_ms == 0 && m.host->paused()) paused_after_ms = now - at;
            });
            ASSERT_EQ(paused_after_ms, 0u);
            m.run(1000, true, [&](uint32_t now) {
                if (paused_after_ms == 0 && m.host->paused()) paused_after_ms = now - at;
            });
            ASSERT_TRUE(paused_after_ms >= 10000 && paused_after_ms <= 10100);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 8000));      // the host closed the dead link: the machine found out and came back
            m.run(2000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.69 Hold Off: A Host That Does Not Hold Seats Behaves Exactly As Before There Was A Way Back: A Lost Link Is A Drop At Once (No Pause, No Presence Ever, The Same Tick Everywhere); A Machine With A Key That Tries To Come Back Is Told \"The Match Has Started\", One Without A Key Is Lost At Once; A Hanging Window Is Lag, Not A Loss") {
        {
            HoldOptions o;
            o.hold = false;
            o.keys = false;
            HoldMatch m(3, o);
            m.run(5000);
            m.cut(1);
            bool paused_ever = false;
            uint64_t drop_tick[3] = {0, 0, 0};
            m.run(3000, true, [&](uint32_t) {
                paused_ever = paused_ever || m.host->paused();
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(1)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[2]->is_player_dropped(1)) drop_tick[1] = m.sims[2]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(1)) drop_tick[2] = m.referee.current_tick();
            });
            ASSERT_FALSE(paused_ever);
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);
            ASSERT_TRUE(m.clients[1]->lost() && !m.clients[1]->rejected() && !m.clients[1]->reconnecting());       // no key, no way back: the match is over for it at once
            ASSERT_EQ(m.refusals, 0u);
            const uint32_t before_vote = m.host->violations(0);
            m.client_ends[0]->send(encode(VoteMsg{1, true}));                            // a Vote belongs to a room that holds seats: for this host it is a message that hosts do not receive
            m.run(100);
            ASSERT_EQ(m.host->violations(0), before_vote + 1);
            m.frozen_mask = 1u << 2;                                                     // a hanging window is lag: no loss, no drop for 30 s
            m.run(15000);
            ASSERT_TRUE(m.host->client_present(2) && !m.host->paused());
            m.frozen_mask = 0;
            m.run(5000);
            m.settle();
            for (uint8_t p = 0; p < 3; ++p) ASSERT_EQ(m.taps[p]->sent_of(MsgType::Presence), size_t{0});         // nothing of the reconnect protocol was ever said
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[2]->state_hash() == m.referee.state_hash());
        }
        {   // the machine has a key (the room that it joined held seats; this host does not): it asks, and is told that the match has started
            HoldOptions o;
            o.hold = false;
            HoldMatch m(3, o);
            m.run(4000);
            m.cut(1);
            m.run(3000);
            ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);
            ASSERT_FALSE(m.host->paused());
            ASSERT_EQ(m.refusals, 1u);
            ASSERT_TRUE(m.clients[1]->lost() && m.clients[1]->rejected() && m.clients[1]->reject_reason() == RejectReason::MatchRunning);
            m.settle();
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[2]->state_hash() == m.referee.state_hash());
        }
    } TEST_END();

    TEST_CASE("N2.70 Hold: The Referee's Runner Can Hold Turns That Nobody Will Send (Fewer Than Its Buffer Asks For: At The Start Of A Match, And Right After A Resume): A Seat Lost In The First 50 ms, And One Lost Right After A Resume, Still Get Their Seats Back (The Referee Runs Its Queue For The Comparison)") {
        {   // lost before the referee's runner has started: one turn is sealed, the buffer asks for two
            HoldMatch m(3);
            m.run(30);
            ASSERT_TRUE(m.sealed() <= 1);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(100);
            ASSERT_TRUE(m.host->paused());
            ASSERT_TRUE(m.referee.current_tick() == 0 && m.host->runner().queued() <= 1);
            m.reload(1);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[1] != nullptr && m.clients[1]->mode() == ClientSession::Mode::Normal; }, 10000));
            m.run(8000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // lost again right after a resume: the referee's runner was restarted by the comparison, a turn or two are sealed and then it is paused again
            HoldMatch m(3);
            m.run(6000);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(500);
            m.reload(1);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 10000));
            m.auto_reconnect[2] = false;
            m.cut(2);                                                                    // about a tick after the match resumed
            m.run(200);
            ASSERT_TRUE(m.host->paused());
            m.reload(2);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[1] != nullptr && m.clients[2] != nullptr && m.clients[1]->mode() == ClientSession::Mode::Normal && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 10000));
            m.run(6000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_EQ(m.host->attendance().rejoins(), 2u);
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.71 Hold: A Returning Connection Is Held To The Rules Of A Client: Flooding (3000 Pings At Once), Garbage And A Wrong Number Of Turns End The Attempt, The Others Are Not Disturbed; Commands It Sends Before The Host Has Compared Its State Are No Offence; A Wrong Hash Is Answered With A Desync To It Alone; A Right Hash Gives It The Seat; A Map That Cannot Be Loaded Is Told \"Rejoin Failed\"; The Stream Is Paced In Bytes By What It Acknowledges, And An Acknowledgement Of What Was Never Sent Opens Nothing") {
        const auto drain = [](Connection* c) {
            std::vector<std::vector<uint8_t>> out;
            std::vector<uint8_t> msg;
            while (c->poll(msg)) out.push_back(msg);
            return out;
        };
        const auto has = [](const std::vector<std::vector<uint8_t>>& got, MsgType t) {
            for (const auto& m : got) {
                if (peek_type(m) == t) return true;
            }
            return false;
        };
        {
            HoldOptions many;                                                               // (a seat is taken back three times a minute: this block says Hello five times in a few seconds)
            many.host.attendance.rejoin_attempts = Attendance::kMaxRejoinAttempts;
            HoldMatch m(3, many);
            m.run(5000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(300);
            ASSERT_TRUE(m.host->paused());
            const auto rejoiner = [&](uint32_t have) {
                auto ends = m.open_link();
                HelloMsg h;
                h.name = "x";
                h.key = m.keys[2];
                h.have_turns = have;
                ends.second->send(encode(h));
                return ends.second;
            };
            {   // flooding: the budget of a connection is the budget of its seat (1000 a second)
                Connection* c = rejoiner(m.sealed());
                m.run(100);
                ASSERT_EQ(m.host->rejoiners(), size_t{1});
                for (uint32_t i = 0; i < 3000; ++i) c->send(encode_ping(PingMsg{i + 1, 0}));
                m.run(600);
                ASSERT_EQ(m.host->rejoiners(), size_t{0});
                ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);        // the attempt is over, the seat is held
                drain(c);                                                                   // (what the host said before it closed is still to be read: only then the machine sees the close)
                ASSERT_FALSE(c->is_open());
                ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(1) && m.host->violations(0) == 0 && m.host->violations(1) == 0);
            }
            {   // commands before the comparison are no offence (a client that has said CaughtUp may believe that it plays)
                Connection* c = rejoiner(m.sealed());
                m.run(100);
                for (int i = 0; i < 100; ++i) {
                    CommandMsg msg;
                    msg.command = cmd(CommandType::Hatch, 2);
                    c->send(encode(msg));
                    c->send(encode_ping(PingMsg{static_cast<uint32_t>(i) + 1, 0}));
                }
                m.run(200);
                ASSERT_EQ(m.host->rejoiners(), size_t{1});
                ASSERT_TRUE(c->is_open());
                ASSERT_TRUE(has(drain(c), MsgType::Pong));                                  // and its pings are answered
                for (int i = 0; i < 8; ++i) c->send(std::vector<uint8_t>{200, 1, 2});         // garbage: eight of them throw it out
                m.run(200);
                ASSERT_EQ(m.host->rejoiners(), size_t{0});
                ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);
            }
            {   // CaughtUp with another number of turns than were announced
                Connection* c = rejoiner(m.sealed());
                m.run(100);
                CaughtUpMsg bad;
                bad.turns = m.sealed() + 1;
                bad.hash = m.referee.state_hash();
                c->send(encode(bad));
                m.run(200);
                ASSERT_EQ(m.host->rejoiners(), size_t{0});
                ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);
            }
            {   // CaughtUp with a hash that is not the referee's: a Desync to it alone, the room does not fail, the seat stays away
                Connection* c = rejoiner(m.sealed());
                m.run(100);
                drain(c);
                CaughtUpMsg bad;
                bad.turns = m.sealed();
                bad.hash = sim::StateHash{1, 2, 3, 4, 5, 6, 7, 8};
                c->send(encode(bad));
                m.run(200);
                const auto got = drain(c);
                ASSERT_TRUE(has(got, MsgType::Desync));
                for (const auto& msg : got) {
                    DesyncMsg d;
                    if (peek_type(msg) == MsgType::Desync && decode(msg, d)) ASSERT_TRUE(d.player == 2 && d.host == m.referee.state_hash() && d.peer == bad.hash);
                }
                ASSERT_EQ(m.host->rejoiners(), size_t{0});
                ASSERT_TRUE(m.host->desyncs().empty() && !m.host->frozen());
                ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);
                ASSERT_FALSE(m.clients[0]->desynced() || m.clients[1]->desynced());
            }
            {   // the right hash (a client that executed everything): the connection is the seat's, the others hear that the match runs
                Connection* c = rejoiner(m.sealed());
                m.run(300);
                drain(c);
                CaughtUpMsg good;
                good.turns = m.sealed();
                good.hash = m.referee.state_hash();
                c->send(encode(good));
                m.run(300);
                ASSERT_FALSE(m.host->paused());
                ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Present);
                ASSERT_TRUE(m.host->client_present(2));
                ASSERT_TRUE(has(drain(c), MsgType::Presence));
                ASSERT_TRUE(!m.clients[0]->paused() && !m.clients[1]->paused());
            }
        }
        {   // a machine whose map cannot be loaded (Loaded not ok) is told that there is no way back for it
            HoldMatch m(3);
            m.run(4000);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(300);
            auto ends = m.open_link();
            HelloMsg h;
            h.key = m.keys[1];
            h.have_turns = 0;
            ends.second->send(encode(h));
            m.run(300);
            ASSERT_TRUE(has(drain(ends.second), MsgType::Start));
            ends.second->send(encode(LoadedMsg{false}));
            m.run(300);
            bool told = false;
            for (const auto& msg : drain(ends.second)) {
                RejectMsg r;
                if (peek_type(msg) == MsgType::Reject && decode(msg, r)) told = r.reason == RejectReason::RejoinFailed;
            }
            ASSERT_TRUE(told);
            ASSERT_EQ(m.host->rejoiners(), size_t{0});
            ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Absent);
        }
        {   // the stream is paced in bytes: a window of 64 KB, a batch of 48 KB at the most beyond it
            HoldOptions o;
            o.host.stream_window_bytes = 64 * 1024;
            HoldMatch m(3, o);
            std::vector<uint32_t> many;
            for (uint32_t i = 0; i < 32; ++i) many.push_back(1000 + i);
            m.run(8000, false, [&](uint32_t) {
                for (uint8_t p : {uint8_t{0}, uint8_t{1}}) {
                    for (int i = 0; i < 8; ++i) {
                        CommandMsg msg;
                        msg.command = cmd(CommandType::GroupMove, p, 255, 5, 5, many);
                        m.client_ends[p]->send(encode(msg));
                    }
                }
            });
            ASSERT_TRUE(m.host->log().bytes() > 1024u * 1024u);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(300);
            const uint32_t total = m.sealed();
            auto ends = m.open_link();
            Connection* c = ends.second;
            HelloMsg h;
            h.key = m.keys[2];
            h.have_turns = 0;
            c->send(encode(h));
            m.run(300);
            ASSERT_TRUE(has(drain(c), MsgType::Start));
            c->send(encode(LoadedMsg{true}));
            struct Got {
                size_t bytes{0};
                uint32_t turns{0};
                uint32_t last_turn{0};
                bool begin{false};
                bool catch_up{false};
            } got;
            const auto take = [&]() {
                for (const auto& msg : drain(c)) {
                    TurnBatchMsg b;
                    CatchUpMsg cu;
                    if (peek_type(msg) == MsgType::TurnBatch && decode(msg, b)) {
                        got.bytes += msg.size();
                        got.turns += static_cast<uint32_t>(b.turns.size());
                        got.last_turn = b.first_turn + static_cast<uint32_t>(b.turns.size()) - 1;
                    } else if (peek_type(msg) == MsgType::Begin) {
                        got.begin = true;
                    } else if (peek_type(msg) == MsgType::CatchUp && decode(msg, cu)) {
                        got.catch_up = cu.first_turn == 0 && cu.total_turns == total;
                    }
                }
            };
            m.run(400);
            take();
            ASSERT_TRUE(got.begin && got.catch_up);
            ASSERT_TRUE(got.bytes >= 64u * 1024u && got.bytes <= 64u * 1024u + kBatchBytes + 64u);      // the window, and the batch that crossed it
            ASSERT_TRUE(got.turns < total);
            const size_t before = got.bytes;
            c->send(encode(AckMsg{0xFFFFFFFEu}));                                           // it acknowledges turns that were never sent: the window opens to what was sent, not to the log
            m.run(400);
            take();
            ASSERT_TRUE(got.bytes - before <= 64u * 1024u + kBatchBytes + 64u);
            ASSERT_TRUE(got.turns < total);
            for (int guard = 0; guard < 2000 && got.turns < total; ++guard) {                // an honest client: acknowledges what it has run
                c->send(encode(AckMsg{got.last_turn}));
                m.run(20);
                take();
            }
            ASSERT_EQ(got.turns, total);
            ASSERT_TRUE(m.host->rejoiners() == 1 && m.host->attendance().state(2) == Attendance::State::CatchingUp);
            CaughtUpMsg good;
            good.turns = total;
            good.hash = m.referee.state_hash();
            c->send(encode(good));
            m.run(300);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Present);
            ASSERT_FALSE(m.host->paused());
        }
    } TEST_END();

    TEST_CASE("N2.72 Hold: The Whole Timeline Is The Same From Every Clock: A Match That Starts 8 s Before The Wrap Of The 32-Bit Clock, At Its Signed Half, And At 0 Pauses, Opens The Vote, Votes And Drops At Cap At The Same Moments (To 10 ms), Across The Wrap") {
        struct Timeline {
            uint32_t paused_after{0};
            uint32_t told_after{0};
            uint32_t vote_after{0};
            uint32_t dropped_after{0};
            uint32_t resumed_after{0};
            uint32_t sealed_at_cap{0};
            uint32_t waited_at_20s{0};
            uint16_t cap_at_20s{0};
        };
        const auto scenario = [&](uint32_t origin) {
            HoldOptions o;
            o.origin = origin;
            o.host.attendance.max_pause_ms = 45000;                                      // the cap (45 s) is after the vote (30 s)
            HoldMatch m(3, o);
            m.run(5000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            const uint32_t cut_at = m.now;
            Timeline t;
            m.run(20000, true, [&](uint32_t now) {
                if (t.paused_after == 0 && m.host->paused()) t.paused_after = now - cut_at;
                if (t.told_after == 0 && m.clients[0]->paused()) t.told_after = now - cut_at;
                if (t.vote_after == 0 && m.clients[0]->presence().vote_seat == 2) t.vote_after = now - cut_at;
            });
            t.waited_at_20s = m.clients[0]->presence().missing.empty() ? 0u : m.clients[0]->presence().missing[0].waited_s;
            t.cap_at_20s = m.clients[0]->presence().cap_s;
            m.run(30000, true, [&](uint32_t now) {
                if (t.vote_after == 0 && m.clients[0]->presence().vote_seat == 2) t.vote_after = now - cut_at;
                if (t.dropped_after == 0 && m.host->attendance().state(2) == Attendance::State::Dropped) {
                    t.dropped_after = now - cut_at;
                    t.sealed_at_cap = m.sealed();
                }
                if (t.dropped_after != 0 && t.resumed_after == 0 && !m.host->paused()) t.resumed_after = now - cut_at;
            });
            return t;
        };
        const Timeline base = scenario(0);
        ASSERT_TRUE(base.paused_after > 0 && base.paused_after <= 20 && base.told_after > 0 && base.told_after <= 120);
        ASSERT_TRUE(base.vote_after >= 30000 && base.vote_after <= 31200);
        ASSERT_TRUE(base.dropped_after >= 45000 && base.dropped_after <= 45100);
        ASSERT_TRUE(base.waited_at_20s >= 19 && base.waited_at_20s <= 21 && base.cap_at_20s >= 24 && base.cap_at_20s <= 26);
        for (const uint32_t origin : {0xFFFFE000u, 0x7FFFFE00u, 0x80000000u, 0xFFFFFC18u - 5000u}) {
            const Timeline t = scenario(origin);
            ASSERT_EQ(t.paused_after, base.paused_after);
            ASSERT_EQ(t.told_after, base.told_after);
            ASSERT_EQ(t.vote_after, base.vote_after);
            ASSERT_EQ(t.dropped_after, base.dropped_after);
            ASSERT_EQ(t.resumed_after, base.resumed_after);
            ASSERT_EQ(t.sealed_at_cap, base.sealed_at_cap);
            ASSERT_EQ(t.waited_at_20s, base.waited_at_20s);
            ASSERT_EQ(t.cap_at_20s, base.cap_at_20s);
        }
    } TEST_END();

    TEST_CASE("N2.73 Turn Log And The Server's Budget: Every Log Takes What It Has ALLOCATED (Its Capacity, Which Grows By Doubling And Is At Most Twice What It Stores) From A Budget That Many Logs Share; A Log That The Budget Cannot Grow Is DEAD: It Frees What It Held And Gives It Back At Once, And Serves Nothing; Destroying Or Releasing A Log Gives Everything Back Once") {
        const std::vector<TurnMsg> turns = real_looking_turns(2000, 11, 60);
        std::vector<size_t> cost(turns.size());                                          // what each turn takes: its packed bytes and its 4 bytes of index
        for (size_t i = 0; i < turns.size(); ++i) cost[i] = pack_turn(turns[i]).size() + sizeof(uint32_t);
        {   // accounting: the budget holds exactly what the logs have allocated, whoever appends, and that is never less than what they hold nor more than twice that and the first allocation
            LogBudget budget(1u << 30);
            TurnLog one(1u << 30, &budget);
            TurnLog two(1u << 30, &budget);
            for (size_t i = 0; i < 700; ++i) {
                ASSERT_TRUE(one.append(turns[i]));
                if (i % 3 == 0) ASSERT_TRUE(two.append(turns[i / 3]) || two.turns() != i / 3);        // (two takes a turn every third step, in order, from the front)
                ASSERT_EQ(budget.used(), uint64_t{one.capacity_bytes()} + uint64_t{two.capacity_bytes()});
                ASSERT_TRUE(one.capacity_bytes() >= one.bytes() && one.capacity_bytes() <= 2 * one.bytes() + TurnLog::kFirstBlobBytes + TurnLog::kFirstOffsets * sizeof(uint32_t));
                ASSERT_TRUE(two.capacity_bytes() >= two.bytes());
            }
            ASSERT_TRUE(budget.used() > 0 && budget.limit() == (1u << 30));
            ASSERT_TRUE(budget.used() > uint64_t{one.bytes()} + uint64_t{two.bytes()});          // (the capacity is what is charged: more than what the logs hold)
        }
        {   // the first allocation is 1.5 KiB: a budget of less keeps no log, a budget of exactly that keeps the 128 turns that its index holds (a turn without a command costs 6 bytes: 2 and 4)
            const size_t first = TurnLog::kFirstBlobBytes + TurnLog::kFirstOffsets * sizeof(uint32_t);
            ASSERT_EQ(first, size_t{1536});
            for (const size_t room : {first - 1, first, first + 1}) {
                LogBudget budget(room);
                TurnLog log(1u << 30, &budget);
                TurnMsg empty;
                uint32_t made = 0;
                while (made < 1000) {
                    empty.turn = made;
                    ASSERT_TRUE(budget.used() <= budget.limit());
                    if (!log.append(empty)) break;
                    ASSERT_EQ(budget.used(), uint64_t{log.capacity_bytes()});
                    ++made;
                }
                ASSERT_EQ(made, room < first ? 0u : 128u);                                // exactly the turns that the index holds; the next one needs a growth that the budget cannot give
                ASSERT_FALSE(log.usable());
                ASSERT_TRUE(log.turns() == 0 && log.bytes() == 0 && log.capacity_bytes() == 0);      // DEAD: it held made turns, and freed them
                ASSERT_EQ(budget.used(), uint64_t{0});                                    // ... and gave every byte back at the moment that it died
                ASSERT_FALSE(log.append(empty));
                std::vector<uint8_t> packed;
                ASSERT_EQ(log.read(0, 10, 1000, packed), 0u);
            }
        }
        {   // a budget that cannot double a log's vectors but can add a quarter keeps the log going a while longer, and then the log is dead and holds nothing
            LogBudget budget(2047);
            TurnLog log(1u << 30, &budget);
            TurnMsg empty;
            uint32_t made = 0;
            while (made < 1000) {
                empty.turn = made;
                if (!log.append(empty)) break;
                ASSERT_TRUE(budget.used() <= 2047);
                ++made;
            }
            ASSERT_TRUE(made > 128 && made < 256);                                        // past the first allocation (the index grew by a quarter), short of the doubling that 2048 would have held
            ASSERT_TRUE(!log.usable() && budget.used() == 0 && log.capacity_bytes() == 0);
        }
        {   // the edge of the budget for real turns: it never passes the budget, the log dies at the first turn that it cannot hold, and its bytes come back
            for (const uint64_t limit : {uint64_t{4000}, uint64_t{9000}, uint64_t{31000}}) {
                LogBudget budget(limit);
                TurnLog log(1u << 30, &budget);
                size_t made = 0;
                while (made < turns.size() && log.append(turns[made])) {
                    ASSERT_TRUE(budget.used() <= limit && budget.used() == log.capacity_bytes());
                    ++made;
                }
                ASSERT_TRUE(made > 0 && made < turns.size());
                size_t held = 0;
                for (size_t i = 0; i < made; ++i) held += cost[i];
                ASSERT_TRUE(held <= limit);                                               // what it held fitted the budget ...
                ASSERT_FALSE(log.usable());
                ASSERT_EQ(budget.used(), uint64_t{0});                                    // ... and it gave all of it back
                ASSERT_TRUE(log.turns() == 0 && log.bytes() == 0);
                ASSERT_FALSE(log.append(turns[made]));                                    // it stays dead
            }
        }
        {   // two logs share one budget: the one that comes later finds less, and the one that dies frees the room that the other may grow into
            uint64_t half = 0;
            for (size_t i = 0; i < 200; ++i) half += cost[i];
            LogBudget budget(half * 3);
            TurnLog first(1u << 30, &budget);
            TurnLog second(1u << 30, &budget);
            for (size_t i = 0; i < 200; ++i) ASSERT_TRUE(first.append(turns[i]));
            size_t got = 0;
            while (got < 2000 && second.append(turns[got])) ++got;
            ASSERT_TRUE(got > 50 && got < 2000);                                          // the second log got what was left
            ASSERT_FALSE(second.usable());
            ASSERT_TRUE(first.usable());
            ASSERT_TRUE(budget.used() <= budget.limit());
            ASSERT_EQ(budget.used(), uint64_t{first.capacity_bytes()});                   // (the dead one holds nothing)
            for (size_t i = 200; i < 400; ++i) ASSERT_TRUE(first.append(turns[i]));       // the first goes on in the room that the second gave back
        }
        {   // destruction and release give everything back, once
            LogBudget budget(1u << 30);
            {
                TurnLog a(1u << 30, &budget);
                TurnLog b(1u << 30, &budget);
                for (size_t i = 0; i < 300; ++i) {
                    ASSERT_TRUE(a.append(turns[i]));
                    ASSERT_TRUE(b.append(turns[i]));
                }
                const uint64_t both = budget.used();
                ASSERT_EQ(both, uint64_t{a.capacity_bytes()} + uint64_t{b.capacity_bytes()});
                a.release();
                ASSERT_EQ(budget.used(), uint64_t{b.capacity_bytes()});                   // a gave back all that it held
                ASSERT_TRUE(a.turns() == 0 && a.bytes() == 0 && a.capacity_bytes() == 0 && !a.usable());
                ASSERT_FALSE(a.append(turns[300]));                                       // a released log takes nothing
                ASSERT_EQ(budget.used(), uint64_t{b.capacity_bytes()});
                std::vector<uint8_t> packed;
                ASSERT_EQ(a.read(0, 10, 1000, packed), 0u);
                ASSERT_EQ(a.bytes_between(0, 10), size_t{0});
                a.release();                                                              // twice is harmless
                ASSERT_EQ(budget.used(), uint64_t{b.capacity_bytes()});
            }                                                                             // the destructors: a gives nothing more, b gives its bytes
            ASSERT_EQ(budget.used(), uint64_t{0});
        }
        {   // a log without a budget is what it was
            TurnLog plain(1u << 20);
            for (size_t i = 0; i < 100; ++i) ASSERT_TRUE(plain.append(turns[i]));
            ASSERT_TRUE(plain.capacity_bytes() >= plain.bytes());
            plain.release();
            ASSERT_TRUE(plain.turns() == 0 && plain.bytes() == 0 && !plain.usable());
        }
        {   // the log's own limit counts what it holds (its size), the budget what it has allocated: the log dies at its limit and gives its capacity back
            LogBudget budget(1u << 30);
            TurnLog log(5000, &budget);
            size_t made = 0;
            while (made < turns.size() && log.append(turns[made])) ++made;
            ASSERT_TRUE(made > 10 && made < turns.size());
            ASSERT_FALSE(log.usable());
            ASSERT_EQ(budget.used(), uint64_t{0});
        }
        LogBudget over(10);                                                               // a budget never gives more than it was given
        over.give(1000);
        ASSERT_EQ(over.used(), uint64_t{0});
        ASSERT_TRUE(over.take(10) && !over.take(1) && over.used() == 10);
        over.give(4);
        ASSERT_TRUE(over.take(4) && !over.take(1));
    } TEST_END();

    TEST_CASE("N2.74 Client: A Silent Server Is Silent In Real Time, And Only When It Was Asked (note_gap, The Rule Of A Page That The Browser Does Not Wake): The Gap That A Wake-Up Hands Over Counts Against A Server That Was Asked And Did Not Answer; One That Was Not Asked Yet Gets The Gap Only Up To 3 s Short Of The 10 s (2.9 s After The Question No, 3.1 s Yes); A Live Server That Answers Its Pings, Or Has Data Waiting, Is Never Condemned; The Way Back Counts Its Waits In Real Time Too: The Catch-Up, A Hello Without A Welcome, The Attempts, The Give-Up") {
        using Mode = ClientSession::Mode;
        const auto is = [](const LoneSession& L, Mode m) { return L.s->mode() == m; };
        {   // the control: a wake-up that hands over no gap (the clock moves by the second that the application allows) is no reason to say anything of the server
            LoneSession L;
            L.step(100);
            L.wake(1000);
            ASSERT_TRUE(is(L, Mode::Normal));
            ASSERT_FALSE(L.s->note_gap(0));                                              // nothing to count
        }
        {   // a server that is dead (it was asked: a ping went out before the page fell asleep) and a page that sleeps for a minute: the whole gap counts, at once, as on a machine whose clock kept time
            LoneSession L;
            L.step(100);
            ASSERT_EQ(L.first().count(MsgType::Ping), size_t{1});
            L.wake(60000);
            ASSERT_TRUE(is(L, Mode::Reconnecting));
            ASSERT_TRUE(L.s->reconnecting() && !L.s->lost() && !L.s->rejected());
            ASSERT_TRUE(L.s->wants_connection(L.now));                                    // and the way back begins
        }
        {   // a server that was heard just before the sleep and dies meanwhile has not been asked: the wake-up asks it (the ping goes out) and counts the gap only up to 3 s short of the 10 s
            const auto sleep_after_an_answer = [](LoneSession& L) {
                L.first().pong = true;
                L.step(100);                                                              // the first ping was answered and the answer heard
                L.first().pong = false;                                                   // the server dies
                L.wake(60000);                                                            // the page wakes after a minute: the question is asked now
            };
            {
                LoneSession L;
                sleep_after_an_answer(L);
                ASSERT_TRUE(is(L, Mode::Normal));                                         // not condemned for a silence that it had no chance to end
            }
            for (const uint32_t after_ms : {2900u, 3000u, 3001u, 3100u}) {                // the next wake-up, that long after the question: the answer is overdue, the whole gap counts
                LoneSession L;
                sleep_after_an_answer(L);
                L.wake(after_ms);
                ASSERT_EQ(is(L, Mode::Reconnecting), after_ms > 3000);                    // 7 s were counted at the first wake-up: 10 s of silence are more than 10 s from 3001 ms on
                ASSERT_EQ(is(L, Mode::Normal), after_ms <= 3000);
            }
            {   // wake-ups every 1.5 s (a page that is awake): the same moment, 3 s after the question
                LoneSession L;
                sleep_after_an_answer(L);
                int wakes = 0;
                while (is(L, Mode::Normal) && wakes < 20) {
                    L.wake(1500);
                    ++wakes;
                }
                ASSERT_TRUE(is(L, Mode::Reconnecting));
                ASSERT_EQ(wakes, 3);                                                      // 3 s after the question the silence is 10 s, 4.5 s after it is more
            }
        }
        {   // a live server that holds its turns says nothing but answers its pings (the answer is a message, which wakes the page): a wake-up a minute for three minutes never makes it silent
            LoneSession L;
            L.first().pong = true;
            L.step(100);
            for (int minute = 0; minute < 3; ++minute) {
                L.wake(60000);
                L.step(30);                                                               // the ping went out at the wake-up, the answer comes back
                ASSERT_TRUE(is(L, Mode::Normal));
            }
            ASSERT_TRUE(L.first().count(MsgType::Ping) >= 4);                             // it was asked every time
            L.first().pong = false;                                                       // ... until it dies: asked and not answered, the next wake-up has it
            L.wake(60000);
            L.step(30);
            ASSERT_TRUE(is(L, Mode::Normal));
            L.wake(60000);
            ASSERT_TRUE(is(L, Mode::Reconnecting));
        }
        {   // a live server with data waiting in the link: what waited was heard (it is read before the gap is counted), so nothing counts, and the silence after it starts from zero
            LoneSession L;
            L.step(100);
            L.give_turns(L.first(), 3);
            L.now += 1000;
            L.net.set_time(L.now);
            L.s->update(L.now);
            ASSERT_EQ(L.s->runner().next_turn_expected(), 3u);
            ASSERT_FALSE(L.s->note_gap(59000));                                           // heard in this very update
            L.step(10);
            ASSERT_TRUE(is(L, Mode::Normal));
            L.wake(3100);                                                                 // 3.1 s of real silence after the data: no reason to say anything
            ASSERT_TRUE(is(L, Mode::Normal));
            L.wake(6000);                                                                 // 9.1 s
            ASSERT_TRUE(is(L, Mode::Normal));
            L.wake(1500);                                                                 // 10.6 s: the question that went out after the data was never answered
            ASSERT_TRUE(is(L, Mode::Reconnecting));
        }
        {   // the machine that catches up is told the same way: its pings are the question, the stream's messages and the pongs are the answer
            ClientSession::Config c;
            c.catch_up_ticks = 1;                                                         // a slow machine: the stream below is never done
            for (const bool alive : {true, false}) {
                LoneSession L(c);
                L.step(100);
                L.lose_link();
                LoneSession::Wire& w = L.attach_new();
                w.pong = alive;
                L.step(100);
                w.srv->send(encode(L.rejoin_welcome()));
                w.srv->send(encode(CatchUpMsg{0, 100000}));
                L.step(100);
                ASSERT_TRUE(is(L, Mode::CatchingUp));
                for (int minute = 0; minute < 3 && is(L, Mode::CatchingUp); ++minute) {
                    L.wake(60000);
                    L.step(30);
                }
                ASSERT_EQ(is(L, Mode::CatchingUp), alive);                                // the one that is answered is never condemned
                ASSERT_EQ(is(L, Mode::Reconnecting), !alive);                             // the one that is not is condemned by the first wake-up: the attempt is over
                ASSERT_FALSE(L.s->lost());
            }
            {   // heard just before the sleep: the first wake-up asks, the second condemns
                LoneSession L(c);
                L.step(100);
                L.lose_link();
                LoneSession::Wire& w = L.attach_new();
                w.pong = true;
                L.step(100);
                w.srv->send(encode(L.rejoin_welcome()));
                w.srv->send(encode(CatchUpMsg{0, 100000}));
                L.step(100);
                w.pong = false;
                L.wake(60000);
                ASSERT_TRUE(is(L, Mode::CatchingUp));
                L.wake(5000);
                ASSERT_TRUE(is(L, Mode::Reconnecting));
            }
        }
        {   // a Hello that no Welcome has answered: the server was asked with it, the wait counts in full (an attempt is abandoned after 10 s, not after 10 wake-ups)
            LoneSession L;
            L.step(100);
            L.lose_link();
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            ASSERT_TRUE(is(L, Mode::Rejoining));
            L.wake(8800);                                                                 // 8.9 s since the Hello
            ASSERT_TRUE(is(L, Mode::Rejoining));
            L.wake(1300);                                                                 // 10.2 s
            ASSERT_TRUE(is(L, Mode::Reconnecting));
            ASSERT_FALSE(w.cli->is_open());
        }
        {   // no link: the attempts are 2 s apart in real time, and the give-up (here 2 minutes) counts from the first loss in real time
            ClientSession::Config c;
            c.reconnect_give_up_ms = 120000;
            LoneSession L(c);
            L.step(100);
            L.lose_link();
            ASSERT_TRUE(L.s->wants_connection(L.now));
            L.s->attach(nullptr, L.now);                                                  // no link could be made: the next attempt is due in 2 s
            ASSERT_FALSE(L.s->wants_connection(L.now));
            L.wake(60000);                                                                // the page was not woken for a minute
            ASSERT_TRUE(L.s->wants_connection(L.now));
            ASSERT_FALSE(L.s->lost());
            L.s->attach(nullptr, L.now);
            L.wake(40000);                                                                // 100 s since the loss
            ASSERT_TRUE(L.s->reconnecting() && !L.s->lost());
            L.wake(30000);                                                                // 130 s
            ASSERT_TRUE(L.s->lost() && !L.s->rejected() && !L.s->reconnecting());         // no word from the server, no way back
        }
        {   // the same in the middle of an attempt: a Hello that is waiting counts toward the give-up
            ClientSession::Config c;
            c.reconnect_give_up_ms = 120000;
            c.rejoin_timeout_ms = 1000000;                                                // (the attempt itself is not abandoned)
            LoneSession L(c);
            L.step(100);
            L.lose_link();
            L.attach_new();
            L.step(100);
            L.wake(100000);
            ASSERT_TRUE(is(L, Mode::Rejoining));
            L.wake(30000);
            ASSERT_TRUE(L.s->lost() && !L.s->rejected());
        }
        {   // the guards: a session that was not started, and one that is not waiting for a server any more, have nothing to count
            sim::SimulationEngine sim0;
            build_world(sim0, 1);
            ClientSession::Config c0;
            c0.player = 1;
            c0.host = kNoSeat;
            c0.migration = false;
            ClientSession fresh(sim0, c0);
            ASSERT_FALSE(fresh.note_gap(5000));
            LoneSession L;
            L.step(100);
            L.s->leave();
            L.step(100);
            ASSERT_TRUE(L.s->lost());
            ASSERT_FALSE(L.s->note_gap(60000));
        }
    } TEST_END();
    // ---- the fixes of the review of release A, through whole matches (N2.80 - N2.87) --------------------------------------------------------------------------------------------

    TEST_CASE("N2.80 Hold: A Key Holder Cannot Freeze A Room: A Hello Every 12 s, Each Time With A Little Progress So That No Stall Is Ever Seen, Holds The Match For The Catch-Up Budget Of The Absence (Here 40 s) And No Longer; Then The Seat Is Absent, Its Hellos Are Refused And Counted, The Vote Opens, And The Cap (Here 120 s) Drops It; The Others Play On To The End Identical And The Key Says \"Dropped\"") {
        HoldOptions o;
        o.host.attendance.max_catch_up_ms = 40000;
        o.host.attendance.max_pause_ms = 120000;
        HoldMatch m(3, o);
        m.run(6000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        const uint32_t cut_at = m.now;
        m.run(200);
        ASSERT_TRUE(m.host->paused() && m.host->attendance().state(2) == Attendance::State::Absent);
        const uint32_t sealed_at_pause = m.sealed();
        // the key holder: a raw client that says Hello for the seat on a new link every 12 s, and acknowledges a few more turns every 5 s
        std::vector<Connection*> links;
        uint32_t next_hello = m.now;
        uint32_t next_ack = m.now + 4000;
        uint32_t total = 0;
        uint32_t creeping = 0;
        const auto key_holder = [&](uint32_t now) {
            if (static_cast<int32_t>(now - next_hello) >= 0) {
                auto ends = m.open_link();
                HelloMsg h;
                h.name = "Eve";
                h.key = m.keys[2];
                h.have_turns = 1;
                ends.second->send(encode(h));
                links.push_back(ends.second);
                next_hello = now + 12000;
            }
            for (Connection* c : links) {
                std::vector<uint8_t> msg;
                while (c->poll(msg)) {
                    CatchUpMsg cu;
                    if (peek_type(msg) == MsgType::CatchUp && decode(msg, cu)) total = cu.total_turns;
                }
            }
            if (static_cast<int32_t>(now - next_ack) >= 0 && total > 100 && !links.empty() && links.back()->is_open()) {
                creeping = std::min(total, creeping + std::max<uint32_t>(total / 40, 2));
                AckMsg a;
                a.turn = creeping - 1;
                links.back()->send(encode(a));
                next_ack = now + 4000;
            }
        };
        m.run(cut_at + 39000 - m.now, true, key_holder);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::CatchingUp);       // 39 s: it has been catching up for 39 s of the 40, and has shown progress all along
        ASSERT_TRUE(m.host->paused() && m.host->attendance().percent(2) > 5);
        ASSERT_EQ(m.host->attendance().catch_up_expired(), 0u);
        m.run(3000, true, key_holder);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);            // 42 s: the budget is used up, whatever progress it made: let go
        ASSERT_EQ(m.host->attendance().catch_up_expired(), 1u);
        ASSERT_EQ(m.host->attendance().rejoins(), 0u);
        ASSERT_TRUE(m.host->attendance().rejoins_refused() >= 1);                       // (the Hello at 36 s was the fourth in a minute)
        ASSERT_TRUE(m.host->paused());
        m.run(2000, true, key_holder);
        ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);      // the others may vote on it: it is away (nothing keeps it catching up)
        ASSERT_TRUE(m.clients[0]->presence().missing.size() == 1 && m.clients[0]->presence().missing[0].state == PresenceMsg::State::Absent);
        uint32_t resumed_after_ms = 0;
        m.run(100000, true, [&](uint32_t now) {
            key_holder(now);
            if (resumed_after_ms == 0 && !m.host->paused()) resumed_after_ms = now - cut_at;
            if (resumed_after_ms == 0) {
                ASSERT_TRUE(m.sealed() == sealed_at_pause);                             // nothing is sealed for all that time
            }
        });
        ASSERT_TRUE(resumed_after_ms >= 120000 && resumed_after_ms <= 120400);           // the cap, to the pass (a key holder held the room for ever before)
        ASSERT_EQ(m.host->attendance().drops_by_cap(), 1u);
        ASSERT_EQ(m.host->attendance().drops_by_vote(), 0u);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
        ASSERT_EQ(m.host->attendance().rejoins(), 0u);
        ASSERT_TRUE(m.host->attendance().rejoins_refused() >= 6 && m.host->attendance().rejoins_refused() <= 8);       // the Hellos of 36 s to 108 s
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        m.run(3000);
        ASSERT_TRUE(m.sealed() > sealed_at_pause + 40);                                 // the others play on
        m.run(5000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.sims[0]->is_player_dropped(2) && m.sims[1]->is_player_dropped(2) && m.referee.is_player_dropped(2));
        ASSERT_TRUE(m.all_equal());
        m.auto_reconnect[2] = true;                                                     // the seat's own machine comes by: its key says "dropped"
        m.run(5000);
        ASSERT_TRUE(m.clients[2]->lost() && m.clients[2]->rejected() && m.clients[2]->reject_reason() == RejectReason::Dropped);
    } TEST_END();

    TEST_CASE("N2.81 Hold: A Key Holder Cannot Make The Server Stream Its Log Again And Again: With A Log Of More Than A Megabyte, 100 Hellos In 10 s Make Three Streams At Most (The Fourth Hello Of A Minute Is Refused), And With The Hellos Allowed The Bytes Stop It At Three Times The Log's Size; Every Refusal Is Counted And Streams Nothing") {
        const auto big_match = [](const HoldOptions& o) {
            auto m = std::make_unique<HoldMatch>(3, o);
            std::vector<uint32_t> many;
            for (uint32_t i = 0; i < 32; ++i) many.push_back(1000 + i);
            m->run(8000, false, [&](uint32_t) {                                       // two players put 16 commands of 32 ants into every 10 ms: a log that is bigger than a megabyte
                for (const uint8_t p : {uint8_t{0}, uint8_t{1}}) {
                    for (int i = 0; i < 8; ++i) {
                        CommandMsg msg;
                        msg.command = cmd(CommandType::GroupMove, p, 255, 5, 5, many);
                        m->client_ends[p]->send(encode(msg));
                    }
                }
            });
            return m;
        };
        // the key holder: Hello on a new link every `every_ms`, `hellos` times; whatever it is sent it acknowledges at once, so that the window of the stream never closes
        const auto hammer = [](HoldMatch& m, uint32_t hellos, uint32_t every_ms) {
            std::vector<Connection*> links;
            uint32_t sent = 0;
            uint32_t next = m.now;
            m.run(hellos * every_ms + 4000, false, [&](uint32_t now) {
                if (sent < hellos && static_cast<int32_t>(now - next) >= 0) {
                    auto ends = m.open_link();
                    HelloMsg h;
                    h.name = "Eve";
                    h.key = m.keys[2];
                    h.have_turns = 1;
                    ends.second->send(encode(h));
                    links.push_back(ends.second);
                    ++sent;
                    next = now + every_ms;
                }
                for (Connection* c : links) {
                    std::vector<uint8_t> msg;
                    while (c->poll(msg)) {
                        TurnBatchMsg b;
                        if (peek_type(msg) == MsgType::TurnBatch && decode(msg, b) && !b.turns.empty()) {
                            AckMsg a;
                            a.turn = b.first_turn + static_cast<uint32_t>(b.turns.size()) - 1;
                            c->send(encode(a));
                        }
                    }
                }
            });
            uint64_t streamed = 0;
            for (const auto& audit : m.audits) streamed += audit->sent;
            return streamed;
        };
        {   // 100 Hellos in 10 s (a Hello every 100 ms): three are accepted, the rest are refused
            HoldOptions o;
            auto mp = big_match(o);
            HoldMatch& m = *mp;
            const size_t log_bytes = m.host->log().bytes();
            ASSERT_TRUE(log_bytes > 1024u * 1024u);
            m.auto_reconnect[2] = false;
            m.audit_new_links = true;
            m.audit_limit = SIZE_MAX / 2;
            m.cut(2);
            m.run(300);
            ASSERT_TRUE(m.host->paused());
            const uint64_t streamed = hammer(m, 100, 100);
            ASSERT_TRUE(streamed <= 3 * uint64_t{log_bytes} + 65536);                  // at most three streams of the log (a hundred before: 150 MB in 10 s)
            ASSERT_TRUE(streamed >= uint64_t{log_bytes} * 9 / 10);                     // (the third was let to finish: its player did not say Hello again that was accepted)
            ASSERT_TRUE(m.host->attendance().streamed_bytes() <= 3 * uint64_t{log_bytes});
            ASSERT_TRUE(m.host->attendance().rejoins_refused() >= 90);                 // 97 Hellos refused, every one counted
            ASSERT_TRUE(m.refusals >= 90);                                             // (the door saw the refusals too)
            ASSERT_EQ(m.host->attendance().rejoins(), 0u);
            ASSERT_TRUE(m.host->rejoiners() <= 1);
            ASSERT_EQ(m.host->attendance().state(2) == Attendance::State::CatchingUp || m.host->attendance().state(2) == Attendance::State::Absent, true);
            ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(1));       // nobody else was disturbed
        }
        {   // the attempts raised to eight a minute and no floor: the bytes alone stop it, at the fourth stream (3 x the log's size streamed in ten minutes)
            HoldOptions o;
            o.host.attendance.rejoin_attempts = Attendance::kMaxRejoinAttempts;
            o.host.attendance.stream_floor_bytes = 0;
            o.host.attendance.max_catch_up_ms = 3600000;
            o.host.attendance.max_pause_ms = 3600000;
            auto mp = big_match(o);
            HoldMatch& m = *mp;
            const size_t log_bytes = m.host->log().bytes();
            ASSERT_TRUE(log_bytes > 1024u * 1024u);
            m.auto_reconnect[2] = false;
            m.audit_new_links = true;
            m.audit_limit = SIZE_MAX / 2;
            m.cut(2);
            m.run(300);
            const uint64_t streamed = hammer(m, 10, 2000);                              // ten Hellos, two seconds apart: each stream is finished before the next Hello comes
            ASSERT_TRUE(streamed <= 3 * uint64_t{log_bytes} + 65536);
            ASSERT_TRUE(streamed >= 2 * uint64_t{log_bytes});
            ASSERT_TRUE(m.host->attendance().streamed_bytes() <= 3 * uint64_t{log_bytes});
            ASSERT_TRUE(m.host->attendance().streamed_bytes() > 2 * uint64_t{log_bytes});
            ASSERT_EQ(m.host->attendance().rejoins_refused(), 7u);                      // the fourth to the tenth: the bytes (and not the eight Hellos a minute, which would have refused two)
        }
    } TEST_END();

    TEST_CASE("N2.82 Hold: A Short Pause Does Not Inflate The Others' Jitter Buffers: Three Cuts Of A Seat's Link (Each A Pause Of Under A Second, Links Of 5 ms) Leave The Buffers Of The Two Other Machines, And Of The One That Came Back, At One Turn For The Next 12 s; Nobody Is Told \"Waiting For The Other Players\"") {
        HoldOptions o;
        o.link.latency_ms = 5;
        o.link.jitter_ms = 2;
        HoldMatch m(3, o);
        m.run(8000);
        ASSERT_TRUE(m.clients[0]->runner().buffer_turns() == 1 && m.clients[2]->runner().buffer_turns() == 1 && m.clients[1]->runner().buffer_turns() == 1);
        for (int i = 0; i < 3; ++i) {
            const uint32_t sealed_before = m.sealed();
            m.cut(1);
            uint32_t paused_ms = 0;
            bool waiting = false;
            m.run(2000, true, [&](uint32_t) {
                if (m.host->paused()) paused_ms += 10;
                waiting = waiting || m.clients[0]->runner().stalled_ms() >= 1000 || m.clients[2]->runner().stalled_ms() >= 1000;
            });
            ASSERT_TRUE(paused_ms >= 30 && paused_ms <= 600);                           // a short pause (the review measured 130 - 210 ms over its links)
            ASSERT_TRUE(m.sealed() - sealed_before >= 25);                              // the match went on
            ASSERT_FALSE(waiting);
            ASSERT_EQ(m.clients[0]->runner().buffer_turns(), 1u);                       // (they used to be 2, then 4, and 150 ms of input delay for half a minute)
            ASSERT_EQ(m.clients[2]->runner().buffer_turns(), 1u);
            ASSERT_EQ(m.clients[1]->runner().buffer_turns(), 1u);
        }
        ASSERT_EQ(m.host->attendance().rejoins(), 3u);
        m.run(12000);
        ASSERT_TRUE(m.clients[0]->runner().buffer_turns() == 1 && m.clients[2]->runner().buffer_turns() == 1 && m.clients[1]->runner().buffer_turns() == 1);
        ASSERT_TRUE(m.clients[0]->runner().jitter().lateness_ms() <= 20 && m.clients[2]->runner().jitter().lateness_ms() <= 20);
        ASSERT_FALSE(m.clients[0]->runner().held() || m.clients[1]->runner().held() || m.clients[2]->runner().held());      // the Presence that ended the pause let go of the runners
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.83 Hold: A Connection That Flaps Cannot Slow The Match Down For Ever (Cut Every 300 ms): Three Losses In A Minute Put The Seat To The Vote At Once, Whatever Its State; Back And Present It Is Voted Out By The Two Others (Its Link Is Closed, The Drop Is The Same Tick Everywhere, The Machine Is Told \"Dropped\"); A Seat That Is Cut Again And Again Is Refused At Its Fourth Hello Of A Minute And The Vote And The Cap Take It; The Match Is At Full Speed Afterwards") {
        {   // three cuts, then the seat stays: it is back and Present, and the vote about it is open
            HoldMatch m(3);
            m.run(6000);
            uint32_t cuts = 0;
            uint32_t last_cut = 0;
            for (int i = 0; i < 3000 && cuts < 3; ++i) {
                m.run(10);
                if (m.clients[2]->mode() == ClientSession::Mode::Normal && m.host->client_present(2) && !m.host->paused() && m.now - last_cut >= 300) {
                    m.cut(2);
                    last_cut = m.now;
                    ++cuts;
                }
            }
            ASSERT_EQ(cuts, 3u);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal && m.host->client_present(2); }, 6000));
            m.run(1500);
            ASSERT_EQ(m.host->attendance().rejoins(), 3u);
            ASSERT_TRUE(m.host->attendance().flapping(2, m.now));
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Present);       // the seat is back and plays
            for (const uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
                const PresenceMsg& p = m.clients[seat]->presence();
                ASSERT_TRUE(p.missing.empty() && p.vote_seat == 2 && p.voters == 2 && p.votes_continue == 0);      // the vote about a seat that is not missing: the others are asked
            }
            ASSERT_TRUE(m.clients[2]->presence().vote_seat == 2 && m.clients[2]->presence().voters == 2);           // (it is told too: the vote is about it, the two others vote)
            ASSERT_FALSE(m.host->paused());
            ASSERT_TRUE(m.clients[0]->vote(2, true));                                    // one of two is not more than half ...
            m.run(1500);
            ASSERT_TRUE(m.host->client_present(2) && m.clients[0]->presence().votes_continue == 1 && m.clients[0]->presence().your_vote == 2);
            ASSERT_TRUE(m.clients[2]->vote(2, true));                                    // ... and the seat does not vote on its own case: its message is ignored (it crossed nothing: no offence)
            m.run(1500);
            ASSERT_EQ(m.host->violations(2), 0u);
            ASSERT_EQ(m.clients[0]->presence().votes_continue, 1);
            const uint32_t sealed_before = m.sealed();
            ASSERT_TRUE(m.clients[1]->vote(2, true));                                    // two of two: the seat is dropped though it is connected
            uint64_t drop_tick[3] = {0, 0, 0};
            m.run(3000, true, [&](uint32_t) {
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(2)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[1]->is_player_dropped(2)) drop_tick[1] = m.sims[1]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(2)) drop_tick[2] = m.referee.current_tick();
            });
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);      // the same tick everywhere
            ASSERT_EQ(m.host->attendance().drops_by_vote(), 1u);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
            ASSERT_FALSE(m.host->client_present(2));                                     // its connection was closed by the host ...
            ASSERT_EQ(m.host->behind_ms(2), 0u);                                         // ... its seat gates nothing
            ASSERT_TRUE(m.clients[2]->lost() && m.clients[2]->rejected() && m.clients[2]->reject_reason() == RejectReason::Dropped);        // ... and its machine knows why
            ASSERT_TRUE(m.sealed() > sealed_before + 40);
            for (const uint8_t seat : {uint8_t{0}, uint8_t{1}}) ASSERT_TRUE(m.clients[seat]->presence().vote_seat == 255 && m.clients[seat]->presence().missing.empty());
            // the match is at full speed: 20 turns a second
            const uint32_t at = m.sealed();
            m.run(5000);
            ASSERT_TRUE(m.sealed() - at >= 98 && m.sealed() - at <= 102);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // nobody votes: the vote about a seat that flapped is open for a minute after its last loss, then the players are told that it is closed (and the seat plays on)
            HoldMatch m(3);
            m.run(6000);
            uint32_t cuts = 0;
            uint32_t last_cut = 0;
            for (int i = 0; i < 3000 && cuts < 3; ++i) {
                m.run(10);
                if (m.clients[2]->mode() == ClientSession::Mode::Normal && m.host->client_present(2) && !m.host->paused() && m.now - last_cut >= 300) {
                    m.cut(2);
                    last_cut = m.now;
                    ++cuts;
                }
            }
            ASSERT_EQ(cuts, 3u);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 6000));
            const uint32_t last_loss = last_cut;
            m.run(2000);
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);
            m.run(last_loss + 55000 - m.now);                                            // 55 s after the last loss: still open
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);
            m.run(8000);                                                                 // more than a minute: closed, and the others were told
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 255 && m.clients[1]->presence().vote_seat == 255);
            ASSERT_FALSE(m.host->attendance().flapping(2, m.now));
            ASSERT_TRUE(m.host->client_present(2) && !m.host->paused() && m.host->attendance().state(2) == Attendance::State::Present);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // a seat that is cut again and again: the fourth Hello of a minute is refused, and its machine is told that there is no way back; the vote and the cap take the seat
            HoldOptions o;
            o.host.attendance.max_pause_ms = 60000;
            HoldMatch m(3, o);
            m.run(6000);
            const uint32_t sealed_before_cuts = m.sealed();
            const uint32_t t_start = m.now;
            uint32_t cuts = 0;
            uint32_t last_cut = 0;
            for (int i = 0; i < 6000 && !m.clients[2]->lost() && cuts < 12; ++i) {
                m.run(10);
                if (m.clients[2]->mode() == ClientSession::Mode::Normal && m.host->client_present(2) && !m.host->paused() && m.now - last_cut >= 300) {
                    m.cut(2);
                    last_cut = m.now;
                    ++cuts;
                }
            }
            ASSERT_TRUE(m.until([&]() { return m.clients[2]->lost(); }, 10000));
            ASSERT_EQ(cuts, 4u);                                                          // the fourth cut is the last one: its Hello is the fourth of a minute
            ASSERT_TRUE(m.clients[2]->rejected() && m.clients[2]->reject_reason() == RejectReason::RejoinFailed);
            ASSERT_EQ(m.host->attendance().rejoins(), 3u);
            ASSERT_EQ(m.host->attendance().rejoins_refused(), 1u);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);          // the seat is held: the vote and the cap apply
            ASSERT_TRUE(m.host->paused());
            ASSERT_TRUE(m.now - t_start < 20000);
            const uint32_t held_in_flapping = m.host->attendance().held_ms(m.now);
            ASSERT_TRUE(held_in_flapping < 12000);                                        // (the whole flapping cost the others a few seconds: it used to go on for as long as the machine kept it up)
            ASSERT_TRUE(m.sealed() > sealed_before_cuts + 10);                            // (the match ran between the cuts)
            m.run(2000);
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);      // the vote is open about it
            ASSERT_TRUE(m.clients[0]->vote(2, true) && m.clients[1]->vote(2, true));
            m.run(2000);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
            ASSERT_FALSE(m.host->paused());
            ASSERT_EQ(m.host->attendance().drops_by_vote(), 1u);
            m.run(8000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.84 Hold: The Resume Countdown (Owner's Request): After A Pause Of 3 s Or More The Match Stays Held For 10 s More Before The Next Turn Is Sealed (After A Return Or After A Vote Or The Cap Dropped The Seat): Presence Says The Seconds Left, Once A Second (10, 9, ... 1, 0); Chat Goes On, Commands Are Discarded Without A Violation; A Loss Cancels It And A Countdown Follows That Pause; A Blip Under 3 s And The Setting 0 Resume At Once; The Countdown Is Not The Cap's Time Nor A Catch-Up's; The Drops And The Resume Are The Same Tick On Every Machine") {
        HoldOptions o;
        o.host.attendance.resume_countdown_ms = 10000;
        {   // after a return: a pause of 6 s
            HoldMatch m(3, o);
            m.run(5000);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(6000);
            ASSERT_TRUE(m.host->paused());
            const uint32_t sealed_at_pause = m.sealed();
            const uint32_t cap_s_before = m.host->attendance().cap_s(m.now);
            m.auto_reconnect[1] = true;
            uint32_t back_at = 0;
            uint32_t resumed_at = 0;
            uint32_t pause_at_back = 0;
            std::vector<unsigned> seen;
            bool mid_checked = false;
            bool mid_submit = true;
            bool mid_paused = false;
            unsigned mid_left = 0;
            bool mid_held = false;
            m.run(18000, true, [&](uint32_t now) {
                if (back_at == 0 && m.host->attendance().state(1) == Attendance::State::Present) {
                    back_at = now;
                    pause_at_back = m.host->attendance().pause_ms(now);
                }
                if (back_at != 0 && resumed_at == 0 && m.sealed() > sealed_at_pause) resumed_at = now;
                if (back_at != 0 && resumed_at == 0 && now - back_at < 9900) {            // the countdown: nothing is sealed, the room is held for no seat's sake
                    ASSERT_EQ(m.sealed(), sealed_at_pause);
                    ASSERT_TRUE(m.host->paused() && !m.host->attendance().paused() && m.host->attendance().counting_down(now));
                }
                if (back_at != 0 && !mid_checked && now - back_at >= 4000) {              // 4 s into it
                    mid_checked = true;
                    mid_paused = m.clients[0]->paused() && m.clients[2]->paused();
                    mid_left = m.clients[0]->resume_seconds_left();
                    mid_held = m.clients[0]->runner().held() && m.clients[2]->runner().held() && m.clients[1]->runner().held();
                    mid_submit = m.clients[0]->submit(cmd(CommandType::Hatch, 0));       // the client refuses what the server would discard
                    ASSERT_TRUE(m.clients[0]->chat("anybody there", false));              // chat goes on
                    for (int i = 0; i < 20; ++i) {                                        // a client that does not know: the server discards, no offence
                        CommandMsg msg;
                        msg.command = cmd(CommandType::Hatch, 0);
                        m.client_ends[0]->send(encode(msg));
                    }
                }
                const unsigned s = m.clients[0]->presence().resume_s;
                if (seen.empty() ? s != 0 : s != seen.back()) seen.push_back(s);
            });
            ASSERT_TRUE(back_at != 0 && resumed_at != 0);
            ASSERT_TRUE(resumed_at - back_at >= 10000 && resumed_at - back_at <= 10040);   // ten seconds from the verified return to the first turn
            ASSERT_TRUE(seen == (std::vector<unsigned>{10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}));  // the seconds, once each, and the end
            ASSERT_TRUE(mid_paused && mid_left >= 5 && mid_left <= 7 && !mid_submit && mid_held);
            ASSERT_EQ(m.host->violations(0), 0u);                                         // the commands of the countdown cost nothing ...
            std::vector<uint8_t> packed;
            ASSERT_EQ(m.host->log().read(sealed_at_pause, 1, 100000, packed), 1u);
            TurnBatchMsg first;
            ASSERT_TRUE(decode(encode_turn_batch_packed(sealed_at_pause, 1, packed.data(), packed.size()), first) && first.turns[0].commands.size() < 5);     // ... and were not kept for the first turn after it
            ASSERT_TRUE(m.chats[2].size() >= 1 && m.chats[2].back().text == "anybody there" && m.chats[2].back().sender == 0);     // chat was relayed
            // it is no pause: the cap and the away times are what the pause left, the match's held time has the countdown in it
            ASSERT_EQ(m.host->attendance().pause_ms(m.now), pause_at_back);
            ASSERT_TRUE(m.host->attendance().cap_s(m.now) <= cap_s_before && m.host->attendance().cap_s(m.now) >= cap_s_before - 3);   // (the cap is where the pause left it: 1 - 2 s of the return)
            ASSERT_TRUE(m.host->attendance().held_ms(m.now) >= pause_at_back + 9990);
            ASSERT_TRUE(m.host->attendance().away_ms(1, m.now) <= pause_at_back);
            ASSERT_FALSE(m.clients[0]->paused() || m.clients[1]->paused() || m.clients[2]->paused());
            ASSERT_FALSE(m.clients[0]->runner().held() || m.clients[1]->runner().held() || m.clients[2]->runner().held());
            ASSERT_TRUE(m.clients[0]->submit(cmd(CommandType::Hatch, 0)));                // the match runs: the client may send again
            m.run(8000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
            for (auto& c : m.clients) ASSERT_EQ(c->runner().buffer_turns(), 1u);           // (a countdown is no stall either)
        }
        {   // a blip: the match resumes at once, no countdown is ever told
            HoldMatch m(3, o);
            m.run(5000);
            m.cut(1);
            std::vector<unsigned> seen;
            uint32_t back_at = 0;
            uint32_t resumed_at = 0;
            const uint32_t sealed_before = m.sealed();
            m.run(3000, true, [&](uint32_t now) {
                if (back_at == 0 && m.host->attendance().state(1) == Attendance::State::Present && m.host->attendance().rejoins() == 1) back_at = now;
                if (back_at != 0 && resumed_at == 0 && !m.host->paused()) resumed_at = now;
                if (m.clients[0]->presence().resume_s != 0) seen.push_back(m.clients[0]->presence().resume_s);
            });
            ASSERT_TRUE(back_at != 0 && resumed_at != 0 && resumed_at - back_at <= 10);
            ASSERT_TRUE(seen.empty());
            ASSERT_FALSE(m.host->attendance().counting_down(m.now));
            ASSERT_TRUE(m.sealed() > sealed_before + 30);
            ASSERT_TRUE(m.host->attendance().held_ms(m.now) < 2000);
        }
        {   // a loss during the countdown cancels it; a countdown follows that pause, however short it is
            HoldMatch m(3, o);
            m.run(5000);
            m.auto_reconnect[1] = false;
            m.auto_reconnect[2] = false;
            m.cut(1);
            m.run(6000);
            m.auto_reconnect[1] = true;
            ASSERT_TRUE(m.until([&]() { return m.host->attendance().state(1) == Attendance::State::Present; }, 6000));
            const uint32_t sealed_before = m.sealed();
            m.run(4000);                                                                  // 4 s into the countdown
            ASSERT_TRUE(m.host->paused() && m.host->attendance().counting_down(m.now));
            ASSERT_TRUE(m.clients[0]->resume_seconds_left() > 0);
            m.cut(2);
            m.run(250);
            ASSERT_TRUE(m.host->attendance().paused() && !m.host->attendance().counting_down(m.now));        // cancelled: seat 2 is missing
            ASSERT_EQ(m.sealed(), sealed_before);
            ASSERT_TRUE(m.clients[0]->presence().missing.size() == 1 && m.clients[0]->presence().missing[0].seat == 2 && m.clients[0]->presence().resume_s == 0);
            ASSERT_TRUE(m.clients[0]->resume_seconds_left() == 0 && m.clients[0]->paused());
            m.auto_reconnect[2] = true;
            ASSERT_TRUE(m.until([&]() { return m.host->attendance().state(2) == Attendance::State::Present; }, 6000));        // it is back within a second or two: a short pause ...
            const uint32_t back2 = m.now;
            ASSERT_TRUE(m.host->attendance().counting_down(m.now));                       // ... and the countdown comes: 10 s from this return
            m.run(9500);
            ASSERT_EQ(m.sealed(), sealed_before);
            m.run(800);
            ASSERT_TRUE(m.sealed() > sealed_before);
            ASSERT_TRUE(m.now - back2 >= 10000);
            ASSERT_FALSE(m.host->paused());
            m.run(6000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // the countdown is a setting: 0 resumes at once after a long pause (a room with it off), and so does the library's default
            HoldOptions off;
            ASSERT_EQ(off.host.attendance.resume_countdown_ms, 0u);
            off.host.attendance.resume_countdown_ms = 0;
            HoldMatch m(3, off);
            m.run(5000);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(6000);
            m.auto_reconnect[1] = true;
            uint32_t back_at = 0;
            uint32_t resumed_at = 0;
            bool any_countdown = false;
            m.run(3000, true, [&](uint32_t now) {
                if (back_at == 0 && m.host->attendance().state(1) == Attendance::State::Present) back_at = now;
                if (back_at != 0 && resumed_at == 0 && !m.host->paused()) resumed_at = now;
                any_countdown = any_countdown || m.clients[0]->presence().resume_s != 0;
            });
            ASSERT_TRUE(back_at != 0 && resumed_at != 0 && resumed_at - back_at <= 10 && !any_countdown);
        }
        {   // after a vote that dropped the last seat that was missing: ten seconds more, then the Drop is sealed and executes at the same tick everywhere
            HoldMatch m(3, o);
            m.run(5000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(31000);
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);
            const uint32_t sealed_at_pause = m.sealed();
            ASSERT_TRUE(m.clients[0]->vote(2, true) && m.clients[1]->vote(2, true));
            uint32_t dropped_at = 0;
            uint32_t resumed_at = 0;
            std::vector<unsigned> seen;
            uint64_t drop_tick[3] = {0, 0, 0};
            m.run(16000, true, [&](uint32_t now) {
                if (dropped_at == 0 && m.host->attendance().state(2) == Attendance::State::Dropped) dropped_at = now;
                if (dropped_at != 0 && resumed_at == 0 && m.sealed() > sealed_at_pause) resumed_at = now;
                if (dropped_at != 0 && resumed_at == 0) ASSERT_EQ(m.sealed(), sealed_at_pause);
                const unsigned s = m.clients[0]->presence().resume_s;
                if (seen.empty() ? s != 0 : s != seen.back()) seen.push_back(s);
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(2)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[1]->is_player_dropped(2)) drop_tick[1] = m.sims[1]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(2)) drop_tick[2] = m.referee.current_tick();
            });
            ASSERT_TRUE(dropped_at != 0 && resumed_at != 0);
            ASSERT_TRUE(resumed_at - dropped_at >= 10000 && resumed_at - dropped_at <= 10040);
            ASSERT_TRUE(seen.size() == 11 && seen.front() == 10 && seen.back() == 0);
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);      // the same tick on every machine, after the countdown
            ASSERT_EQ(m.host->attendance().drops_by_vote(), 1u);
            m.run(4000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // after the cap dropped the seat: ten seconds more; the cap's account is the pause only
            HoldOptions capped = o;
            capped.host.attendance.max_pause_ms = 20000;
            HoldMatch m(3, capped);
            m.run(5000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            const uint32_t cut_at = m.now;
            const uint32_t sealed_at_pause = m.sealed();
            uint32_t dropped_at = 0;
            uint32_t resumed_at = 0;
            m.run(34000, true, [&](uint32_t now) {
                if (dropped_at == 0 && m.host->attendance().state(2) == Attendance::State::Dropped) dropped_at = now;
                if (dropped_at != 0 && resumed_at == 0 && m.sealed() > sealed_at_pause) resumed_at = now;
            });
            ASSERT_TRUE(dropped_at - cut_at >= 20000 && dropped_at - cut_at <= 20300);    // the cap: 20 s of pause
            ASSERT_TRUE(resumed_at - dropped_at >= 10000 && resumed_at - dropped_at <= 10040);
            ASSERT_EQ(m.host->attendance().drops_by_cap(), 1u);
            ASSERT_TRUE(m.host->attendance().pause_ms(m.now) >= 20000 && m.host->attendance().pause_ms(m.now) <= 20300);        // the 10 s of countdown are not in it
            ASSERT_TRUE(m.host->attendance().held_ms(m.now) >= 30000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // the countdown is not the catch-up's time: a seat that catches up for 25 s of a budget of 30 s is not let go in the countdown that a slow stream's end begins
            HoldOptions slow = o;
            slow.host.attendance.max_catch_up_ms = 30000;
            slow.client.catch_up_ticks = 1;                                               // 100 turns a second
            HoldMatch m(3, slow);
            m.run(100000);                                                                // 2000 turns: a catch-up of 20 s
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(500);
            m.reload(2);
            ASSERT_TRUE(m.until([&]() { return m.host->attendance().state(2) == Attendance::State::Present; }, 28000));
            ASSERT_TRUE(m.host->attendance().catch_up_expired() == 0 && m.host->attendance().counting_down(m.now));
            m.run(11000);                                                                 // the countdown passes (it would have run the 30 s out, were it a catch-up)
            ASSERT_EQ(m.host->attendance().catch_up_expired(), 0u);
            ASSERT_FALSE(m.host->paused());
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Present);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.85 Hold: A Log That Is Dead Frees Its Memory At Once (Nobody Can Be Streaming From It: A Log Only Grows While The Match Runs, And A Seat Is Only Brought Back While It Is Paused): The Server's Budget Is Whole Again The Moment It Dies, And The Room Falls Back To Dropping A Lost Seat At Once; A Pause Never Changes What The Log Holds") {
        LogBudget budget(6000);
        HoldOptions o;
        o.host.log_budget = &budget;
        HoldMatch m(3, o);
        m.run(1000);
        ASSERT_TRUE(m.host->log().usable() && budget.used() > 0 && budget.used() == m.host->log().capacity_bytes());
        // a pause: nothing is appended, nothing is allocated, whatever the seat does meanwhile
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        ASSERT_TRUE(m.host->paused());
        const uint64_t held = budget.used();
        const uint32_t turns = m.host->log().turns();
        m.run(5000);
        ASSERT_TRUE(budget.used() == held && m.host->log().turns() == turns && m.host->log().usable());
        m.auto_reconnect[2] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 8000));
        // the match runs on until the budget is spent: the log is dead from that moment and has given everything back
        uint64_t most = 0;
        ASSERT_TRUE(m.until([&]() {
            most = std::max(most, budget.used());
            return !m.host->log().usable();
        }, 120000));
        ASSERT_TRUE(most > 3000 && most <= 6000);
        ASSERT_EQ(budget.used(), uint64_t{0});                                            // at once: not when the match ends
        ASSERT_TRUE(m.host->log().turns() == 0 && m.host->log().bytes() == 0 && m.host->log().capacity_bytes() == 0);
        m.run(2000);
        ASSERT_EQ(budget.used(), uint64_t{0});
        // a lost seat is dropped at once now (no pause, no way back)
        m.auto_reconnect[1] = false;
        m.cut(1);
        bool paused_ever = false;
        m.run(3000, true, [&](uint32_t) { paused_ever = paused_ever || m.host->paused(); });
        ASSERT_FALSE(paused_ever);
        ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.86 Client: How Long A Machine Tries To Come Back Follows The Room's Cap: The Last Cap That A Presence Named (From The Loss, Or From That Presence When A Seat Was Missing Then) And A Minute; 31 Minutes Before Any Presence; A Cap Of \"That Or More\" Is A Day; The Last Presence Wins") {
        const auto lose_and_wait = [](LoneSession& L) {
            L.lose_link();
            const uint32_t since = L.s->lost_since_ms();
            uint32_t waited = 0;
            while (!L.s->lost() && waited < 3 * 3600u * 1000u) {
                L.step(1000);
                waited += 1000;
            }
            return L.now - since;
        };
        const auto presence = [](LoneSession& L, uint16_t cap_s, bool missing) {
            PresenceMsg p;
            p.cap_s = cap_s;
            p.voters = 2;
            if (missing) p.missing = {{2, PresenceMsg::State::Absent, 20, 0}};
            L.first().srv->send(encode(p));
            L.step(100);
        };
        ASSERT_TRUE(kReconnectGiveUpMs == 31u * 60u * 1000u && kReconnectMarginMs == 60000u && kMaxCapSeconds == 86400u);
        {   // before any Presence: the default, 31 minutes (what a room's default cap and a minute make)
            LoneSession L;
            L.first().pong = true;
            L.step(100);
            L.lose_link();
            ASSERT_EQ(L.s->give_up_ms(), kReconnectGiveUpMs);
            ClientSession::Config c;
            c.reconnect_give_up_ms = 7000;
            LoneSession configured(c);
            configured.step(100);
            configured.lose_link();
            ASSERT_EQ(configured.s->give_up_ms(), 7000u);                                // (the configured time is the one that holds until a Presence names a cap)
        }
        {   // a Presence that names a cap of 100 s while nobody was missing: the cap runs from the loss: 100 s and a minute
            LoneSession L;
            L.first().pong = true;
            L.step(100);
            presence(L, 100, false);
            L.lose_link();
            ASSERT_EQ(L.s->give_up_ms(), 160000u);
            const uint32_t waited = lose_and_wait(L);
            ASSERT_TRUE(L.s->lost() && !L.s->rejected());
            ASSERT_TRUE(waited >= 160000 && waited <= 161100);
        }
        {   // a Presence of a pause that was on already, 30 s before the loss: the cap was running since: 100 s from that Presence and a minute, so 130 s from the loss
            LoneSession L;
            L.first().pong = true;
            L.step(100);
            presence(L, 100, true);
            L.step(30000);
            L.lose_link();
            const uint32_t gone = L.s->lost_since_ms();
            ASSERT_TRUE(L.s->give_up_ms() >= 129000 && L.s->give_up_ms() <= 130000);
            const uint32_t waited = lose_and_wait(L);
            ASSERT_TRUE(waited >= 129000 && waited <= 131100);
            (void)gone;
        }
        {   // the last Presence wins (a cap that ran down, a pause that ended); a cap that is spent leaves the margin
            LoneSession L;
            L.first().pong = true;
            L.step(100);
            presence(L, 100, false);
            presence(L, 50, false);
            L.lose_link();
            ASSERT_EQ(L.s->give_up_ms(), 110000u);
            LoneSession spent;
            spent.first().pong = true;
            spent.step(100);
            presence(spent, 0, false);
            spent.lose_link();
            ASSERT_EQ(spent.s->give_up_ms(), kReconnectMarginMs);
            LoneSession huge;
            huge.first().pong = true;
            huge.step(100);
            presence(huge, kCapSecondsMore, false);
            huge.lose_link();
            ASSERT_EQ(huge.s->give_up_ms(), 86400u * 1000u + 60000u);                    // 0xFFFF seconds says "that or more": a room's cap is at most a day
            LoneSession exact;
            exact.first().pong = true;
            exact.step(100);
            presence(exact, 1800, false);
            exact.lose_link();
            ASSERT_EQ(exact.s->give_up_ms(), 1860000u);                                  // the default cap of 30 minutes is the default give-up time
        }
    } TEST_END();

    TEST_CASE("N2.87 Hold: Presence Goes Out At Once For A Second Loss While The Match Is Paused (Not Up To A Second Later), And Once A Second And No Faster While Nothing Changes; A Vote That Opens Or Closes While The Match Runs Is Told Too") {
        HoldMatch m(3);
        m.run(3000);
        m.auto_reconnect[1] = false;
        m.auto_reconnect[2] = false;
        m.cut(1);
        m.run(300);                                                                       // paused; the last Presence went out at the loss, the next is due in 0.7 s
        ASSERT_TRUE(m.host->paused());
        const size_t before = m.taps[0]->sent_of(MsgType::Presence);
        ASSERT_TRUE(before >= 1);
        m.cut(2);
        m.run(30);                                                                        // the next pass or two
        ASSERT_TRUE(m.taps[0]->sent_of(MsgType::Presence) > before);                       // told in the same pass that saw the loss
        m.run(120);                                                                       // ... and it reaches the player
        ASSERT_EQ(m.clients[0]->presence().missing.size(), size_t{2});
        // nothing changes for ten seconds: a Presence a second, to every player that is there
        const size_t quiet_from = m.taps[0]->sent_of(MsgType::Presence);
        m.run(10000);
        const size_t quiet = m.taps[0]->sent_of(MsgType::Presence) - quiet_from;
        ASSERT_TRUE(quiet >= 9 && quiet <= 11);                                           // (a thousand when it went out on every pass)
        // a vote that opens by itself: told when the seat's away time reaches the vote's 30 s (within the second), and nothing more often than that
        ASSERT_TRUE(m.clients[0]->presence().vote_seat == 255);
        m.run(20000);
        ASSERT_TRUE(m.clients[0]->presence().vote_seat == 1);
    } TEST_END();
    TEST_CASE("N2.88 Hold (the rig): A Machine That Is Reloaded Over A Slow Link (More Than A Second In The Lobby) Has A Sane Clock: Its Session Is Started With The Machine's Own Clock And Its First Frame Does Not Take Back The Time That The Lobby Took, So It Is Not \"Silent\" And Does Not Give Up At Once; The Rig Of Every Other Test Reloads The Same Way") {
        HoldOptions o;
        o.link.latency_ms = 700;                                                        // four trips for the lobby alone: more than 2.8 s before the session exists
        o.link.jitter_ms = 0;
        HoldMatch m(3, o);
        m.run(8000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(3000);
        ASSERT_TRUE(m.host->paused());
        const uint32_t reload_at = m.now;
        m.clock_lag[2] = 4000;                                                          // this machine's own clock is behind (a window that was frozen earlier): the new session is started with that clock, not the real one
        m.reload(2);
        ASSERT_TRUE(m.until([&]() { return m.clients[2] != nullptr; }, 8000));
        ASSERT_TRUE(m.now - reload_at > 2000);                                          // the lobby took longer than a second: the old rig put the session's clock back by that much at its first frame
        ASSERT_EQ(m.clock_lag[2], 4000u);                                               // ... and the time in the lobby is no stop of the session's frames: the lag has not grown by it
        ASSERT_TRUE(m.until([&]() { return m.clients[2]->mode() == ClientSession::Mode::Normal && !m.host->paused(); }, 30000));
        ASSERT_FALSE(m.clients[2]->lost());
        ASSERT_EQ(m.host->attendance().rejoins(), 1u);
        m.run(8000);
        m.settle(6000);
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Protocol 11: the fill level of the leader's START, and chat in the waiting room
// ---------------------------------------------------------------------------------------------------------------------------------

void run_protocol11_tests() {
    TEST_CASE("N2.89 Protocol 11: StartRequest Carries A Fill Level (Every Level Round-Trips, Every Truncation, Trailing Byte And Level Above 3 Is Refused, The Names And Parsing Of The Levels), The Room's Notices Are Chat From Sender 255 (Round Trip, Limits), And 300000 Mutated StartRequests And Chat Lines Only Give Messages That Encode Back To The Same Bytes") {
        ASSERT_EQ(kProtocolVersion, 13);                                 // (the Chat of protocol 11 is the Chat of protocol 13; the StartRequest grew in 13: a level for each seat and the teams, N2.96 has its rules, here the one level of protocol 11 is the same level in every seat)
        ASSERT_TRUE(kFillLevelLast == 3 && kRoomSender == 255 && static_cast<int>(MsgType::StartRequest) == 24 && static_cast<int>(MsgType::Chat) == 9);
        // every level
        const std::pair<FillLevel, const char*> levels[] = {{FillLevel::None, "none"}, {FillLevel::Easy, "easy"}, {FillLevel::Medium, "medium"}, {FillLevel::Hard, "hard"}};
        for (uint8_t i = 0; i < 4; ++i) {
            const StartRequestMsg m = StartRequestMsg::all(levels[i].first);
            const std::vector<uint8_t> bytes = encode(m);
            ASSERT_TRUE(bytes == (std::vector<uint8_t>{24, i, i, i, i, 255, 255}));
            StartRequestMsg back = StartRequestMsg::all(FillLevel::Hard);
            ASSERT_TRUE(decode(bytes, back) && back.fill == m.fill);
            ASSERT_EQ(std::string(fill_level_name(levels[i].first)), std::string(levels[i].second));
            FillLevel parsed = FillLevel::Hard;
            ASSERT_TRUE(parse_fill_level(levels[i].second, parsed) && parsed == levels[i].first);
            std::string upper = levels[i].second;
            for (char& c : upper) c = static_cast<char>(c - 'a' + 'A');
            ASSERT_TRUE(parse_fill_level(upper, parsed) && parsed == levels[i].first);
            // every strict prefix, and every trailing byte, is refused
            for (size_t cut = 0; cut < bytes.size(); ++cut) {
                ASSERT_FALSE(decode(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)), back));
            }
            for (unsigned extra = 0; extra < 256; ++extra) {
                std::vector<uint8_t> longer = bytes;
                longer.push_back(static_cast<uint8_t>(extra));
                ASSERT_FALSE(decode(longer, back));
            }
        }
        ASSERT_TRUE(fill_bot_name(FillLevel::Easy) == "Bot (Easy)" && fill_bot_name(FillLevel::Medium) == "Bot (Medium)" && fill_bot_name(FillLevel::Hard) == "Bot (Hard)" && fill_bot_name(FillLevel::None).empty());
        ASSERT_TRUE(fill_level_title(FillLevel::Easy) == "Easy" && fill_level_title(FillLevel::Medium) == "Medium" && fill_level_title(FillLevel::Hard) == "Hard" && fill_level_title(FillLevel::None).empty());     // (what a person is told: "Empty seats will be Medium bots.")
        FillLevel keep = FillLevel::Easy;
        for (const char* bad : {"", "none ", " easy", "med", "mediumm", "hard1", "2", "harder", "NONE\\0"}) ASSERT_FALSE(parse_fill_level(bad, keep));
        ASSERT_EQ(keep, FillLevel::Easy);                                // a refusal leaves the value alone
        // the room's notice: a Chat message whose sender is 255; the limits of a Chat line are the match's
        ChatMsg notice;
        notice.sender = kRoomSender;
        notice.text = kNoticeFillFog;
        ChatMsg back;
        ASSERT_TRUE(decode(encode(notice), back) && back.sender == 255 && !back.team && back.text == kNoticeFillFog);
        ASSERT_TRUE(std::string(kNoticeFillFog).size() <= kMaxChatChars && std::string(kNoticeFillMap).size() <= kMaxChatChars);
        for (const char* text : {kNoticeFillFog, kNoticeFillMap}) {
            for (const char c : std::string(text)) ASSERT_TRUE(c >= 0x20 && c <= 0x7E);       // printable ASCII, as a chat line must be
        }
        ChatMsg full;
        full.sender = 2;
        full.text = std::string(kMaxChatChars, 'q');
        ASSERT_TRUE(decode(encode(full), back) && back.text.size() == kMaxChatChars);
        std::vector<uint8_t> raw = {9, 2, 0, 101};
        raw.insert(raw.end(), 101, 'q');
        ASSERT_FALSE(decode(raw, back));                                 // one character too many
        ASSERT_FALSE(decode(std::vector<uint8_t>{9, 2, 2, 1, 'a'}, back) || decode(std::vector<uint8_t>{9, 2, 0, 1, 0x1F}, back) || decode(std::vector<uint8_t>{9, 2, 0, 1, 0x7F}, back));
        // the fuzz: bytes made from valid StartRequests and chat lines, mutated, cut and lengthened, give a message only when it encodes back to exactly those bytes
        const std::vector<std::vector<uint8_t>> seeds = {encode(StartRequestMsg::all(FillLevel::Easy)), encode(StartRequestMsg::all(FillLevel::Hard)), encode(notice), encode(full),
                                                         encode(ChatMsg{1, false, "hello"}), encode(ChatMsg{3, true, ""}), encode(StartRequestMsg{}),
                                                         encode(StartRequestMsg{{FillLevel::None, FillLevel::Easy, FillLevel::Medium, FillLevel::Hard}, 1, 2})};
        Lcg rng(11);
        size_t accepted_requests = 0;
        size_t accepted_chat = 0;
        for (int i = 0; i < 300000; ++i) {
            std::vector<uint8_t> buf = seeds[rng.below(static_cast<uint32_t>(seeds.size()))];
            for (uint32_t k = 1 + rng.below(3); k > 0; --k) buf[rng.below(static_cast<uint32_t>(buf.size()))] = static_cast<uint8_t>(rng.below(256));
            const uint32_t shape = rng.below(6);
            if (shape == 0) buf.resize(rng.below(static_cast<uint32_t>(buf.size()) + 1));
            else if (shape == 1) buf.push_back(static_cast<uint8_t>(rng.below(256)));
            StartRequestMsg sr;
            ChatMsg chat;
            const bool is_request = decode(buf, sr);
            const bool is_chat = decode(buf, chat);
            ASSERT_FALSE(is_request && is_chat);                          // (they have different type bytes)
            if (is_request) {
                ++accepted_requests;
                ASSERT_TRUE(encode(sr) == buf && buf.size() == 7 && buf[0] == 24);
                for (const FillLevel level : sr.fill) ASSERT_TRUE(static_cast<uint8_t>(level) <= kFillLevelLast);
            }
            if (is_chat) {
                ++accepted_chat;
                ASSERT_TRUE(encode(chat) == buf && chat.text.size() <= kMaxChatChars);
                for (const char c : chat.text) ASSERT_TRUE(c >= 0x20 && c <= 0x7E);
            }
            if (!buf.empty() && (buf[0] == 24 || buf[0] == 9)) ASSERT_EQ(any_decodes(buf), is_request || is_chat);     // no other decoder takes either type
        }
        ASSERT_TRUE(accepted_requests > 300 && accepted_chat > 3000);               // (a message of seven bytes survives a mutation rarely; the ones that do are all the right ones)
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Team chat reaches only the allies (the relay of a host, by the host's own alliance table at the time of the relay)
// ---------------------------------------------------------------------------------------------------------------------------------

// A match of a host and `players` seats over a loopback network, built to see WHAT THE HOST PUTS ON EACH LINK: the host's end of every link is a HostTap (it counts the Chat messages that
// it sends), and a seat of `raw_mask` has no session at all: a raw client that never filters, never acknowledges and reads every message of its link (what a modified client would see).
// `lan` is a game on the local network (the host holds seat 0 and is a receiver like the others); without it the host is a dedicated server's referee and holds no seat.
struct TeamChatRig {
    LoopbackNetwork net;
    sim::SimulationEngine host_engine;                                  // the host's own engine: the table that the relay reads
    std::vector<std::unique_ptr<sim::SimulationEngine>> sims;           // by seat (null for the host's seat and for a raw seat)
    Ids ids;
    std::unique_ptr<HostSession> host;
    std::vector<std::unique_ptr<HostTap>> taps;                         // the host's end of the link of each seat (null for the host's own seat)
    std::vector<std::unique_ptr<ClientSession>> clients;                // by seat (null for the host's seat and for a raw seat)
    std::vector<Connection*> client_ends;
    std::vector<ChatMsg> heard[sim::MAX_PLAYERS];                       // what the session of a seat reported as chat
    std::vector<ChatMsg> raw[sim::MAX_PLAYERS];                         // every Chat message that arrived on the link of a raw seat (decoded from the bytes)
    std::vector<ChatMsg> host_heard;                                    // what the host's own callback was given
    uint32_t now{0};
    uint8_t players;
    bool lan;

    TeamChatRig(bool lan_, uint8_t players_, uint8_t raw_mask) : net(77), players(players_), lan(lan_) {
        ids = build_world(host_engine, 1);
        HostSession::Config hc;
        hc.host_player = lan ? uint8_t{0} : kNoSeat;
        host = std::make_unique<HostSession>(host_engine, hc);
        sims.resize(players);
        taps.resize(players);
        clients.resize(players);
        client_ends.assign(players, nullptr);
        for (uint8_t p = lan ? 1 : 0; p < players; ++p) {
            auto ends = net.connect({20, 0});
            taps[p] = std::make_unique<HostTap>(ends.first, &now);
            host->add_client(p, taps[p].get());
            client_ends[p] = ends.second;
            if ((raw_mask & (1u << p)) != 0) continue;
            sims[p] = std::make_unique<sim::SimulationEngine>();
            build_world(*sims[p], 1);
            ClientSession::Config c;
            c.player = p;
            c.host = lan ? uint8_t{0} : kNoSeat;
            c.migration = false;
            clients[p] = std::make_unique<ClientSession>(*sims[p], c);
            clients[p]->set_connection(ends.second);
            clients[p]->set_on_chat([this, p](const ChatMsg& m) { heard[p].push_back(m); });
        }
        host->set_on_chat([this](const ChatMsg& m) { host_heard.push_back(m); });
        host->start(0);
        for (auto& c : clients) {
            if (c) c->start(0);
        }
    }

    void step(uint32_t ms) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            net.set_time(now);
            host->update(now);
            for (uint8_t p = 0; p < players; ++p) {
                if (clients[p]) {
                    clients[p]->update(now);
                } else if (client_ends[p] != nullptr) {
                    std::vector<uint8_t> msg;
                    while (client_ends[p]->poll(msg)) {
                        ChatMsg c;
                        if (peek_type(msg) == MsgType::Chat && decode(msg, c)) raw[p].push_back(c);
                    }
                }
            }
        }
    }
    void order(uint8_t seat, const Command& c) {
        if (lan && seat == 0) host->submit_local(c);
        else clients[seat]->submit(c);
    }
    bool say(uint8_t seat, const std::string& text, bool team) {
        if (lan && seat == 0) {
            host->chat_local(text, team);
            return true;
        }
        return clients[seat]->chat(text, team);
    }
    // `a` invites `b`, `b` accepts: true when the host's table says that they are allies
    bool ally(uint8_t a, uint8_t b) {
        order(a, cmd(CommandType::AllianceInvite, a, b));
        step(500);
        order(b, cmd(CommandType::AllianceAccept, b, a));
        step(500);
        return host_engine.alliance_of(a) == b && host_engine.alliance_of(b) == a;
    }
    size_t on_wire(uint8_t seat) const { return taps[seat] ? taps[seat]->sent_of(MsgType::Chat) : 0; }     // the Chat messages that the host has sent to the seat
};

bool has_line(const std::vector<ChatMsg>& v, const std::string& text) {
    for (const ChatMsg& c : v) {
        if (c.text == text) return true;
    }
    return false;
}

void run_team_chat_tests() {
    TEST_CASE("N2.90 Team Chat At The Wire (A Dedicated Server's Host): A Line For The Team Goes To The Sender And Its Ally Only, By The Referee's Alliance Table; A Raw Client Of A Non-Ally That Reads Every Byte Of Its Link Gets Nothing; A Line For All Goes To Everybody; A Player Without An Ally Hears Only Itself; The Referee Logs Every Line") {
        TeamChatRig m(false, 4, 0x08);                                   // seats 0 and 1 will be allies, seat 2 is not, seat 3 is a raw client without a session
        ASSERT_TRUE(m.host->seatless());
        ASSERT_EQ(m.host_engine.alliance_of(0), sim::ALLIANCE_NONE);
        ASSERT_TRUE(m.ally(0, 1));
        ASSERT_TRUE(m.host_engine.alliance_of(2) == sim::ALLIANCE_NONE && m.host_engine.alliance_of(3) == sim::ALLIANCE_NONE);
        // the rule, as a function of the table
        ASSERT_TRUE(m.host->hears_chat(0, true, 0) && m.host->hears_chat(0, true, 1) && !m.host->hears_chat(0, true, 2) && !m.host->hears_chat(0, true, 3));
        ASSERT_TRUE(m.host->hears_chat(0, false, 2) && m.host->hears_chat(0, false, 3) && m.host->hears_chat(2, true, 2) && !m.host->hears_chat(2, true, 0));

        const size_t before_2 = m.on_wire(2);
        const size_t before_3 = m.on_wire(3);
        ASSERT_TRUE(m.say(0, "team A", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[0], "team A") && has_line(m.heard[1], "team A"));        // the sender (its own line comes back) and its ally
        ASSERT_FALSE(has_line(m.heard[2], "team A"));
        ASSERT_TRUE(m.raw[3].empty());                                                         // nothing at all arrived at the raw client
        ASSERT_EQ(m.on_wire(2), before_2);                                                     // and the host never sent it to either: counted on the host's end of the links
        ASSERT_EQ(m.on_wire(3), before_3);
        for (const ChatMsg& c : m.heard[1]) {
            if (c.text == "team A") ASSERT_TRUE(c.sender == 0 && c.team);                      // the line is what it was: the sender's seat (stamped by the connection) and the flag
        }

        ASSERT_TRUE(m.say(1, "team B", true));                                                 // the other way round
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[0], "team B") && has_line(m.heard[1], "team B"));
        ASSERT_FALSE(has_line(m.heard[2], "team B"));
        ASSERT_TRUE(m.raw[3].empty());

        ASSERT_TRUE(m.say(2, "lonely", true));                                                 // a player without an ally: its own screen only
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[2], "lonely"));
        ASSERT_FALSE(has_line(m.heard[0], "lonely") || has_line(m.heard[1], "lonely"));
        ASSERT_TRUE(m.raw[3].empty());

        ASSERT_TRUE(m.say(2, "to all", false));                                                // a line for all: everybody, the raw client too
        m.step(300);
        for (uint8_t p = 0; p < 3; ++p) ASSERT_TRUE(has_line(m.heard[p], "to all"));
        ASSERT_TRUE(m.raw[3].size() == 1 && m.raw[3][0].text == "to all" && m.raw[3][0].sender == 2 && !m.raw[3][0].team);
        // counted at the wire: seats 0 and 1 got three lines (team A, team B, to all), seat 2 two (lonely, to all), seat 3 one
        ASSERT_TRUE(m.on_wire(0) == 3 && m.on_wire(1) == 3 && m.on_wire(2) == before_2 + 2 && m.on_wire(3) == before_3 + 1);
        // the referee hears them all (its callback is its log, not a screen)
        ASSERT_EQ(m.host_heard.size(), size_t{4});
    } TEST_END();

    TEST_CASE("N2.91 Team Chat: An Alliance That Is Broken Stops The Delivery At Once (The Next Line After The Referee's Table Changed), A New Alliance Starts It At Once, A Line That Was Sent Before The Break Arrives") {
        TeamChatRig m(false, 4, 0x08);
        ASSERT_TRUE(m.ally(0, 1));
        ASSERT_TRUE(m.say(1, "before the break", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[0], "before the break") && !has_line(m.heard[2], "before the break"));
        const size_t at_0 = m.on_wire(0);
        // seat 0 leaves the team; the very step in which the referee's table changes is the last one in which the line could still go to it
        m.order(0, cmd(CommandType::AllianceBreak, 0));
        int steps = 0;
        while (m.host_engine.alliance_of(0) != sim::ALLIANCE_NONE && steps++ < 300) m.step(10);
        ASSERT_TRUE(steps < 300);
        ASSERT_TRUE(m.host_engine.alliance_of(1) == sim::ALLIANCE_NONE);                       // (an alliance ends for both)
        ASSERT_TRUE(m.say(1, "after the break", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[1], "after the break"));                                  // seat 1 hears itself
        ASSERT_FALSE(has_line(m.heard[0], "after the break"));
        ASSERT_EQ(m.on_wire(0), at_0);                                                         // not sent: the line is not on the host's end of seat 0's link
        ASSERT_TRUE(m.raw[3].empty());
        ASSERT_TRUE(m.say(0, "from the former ally", true));                                   // and the other direction
        m.step(300);
        ASSERT_FALSE(has_line(m.heard[1], "from the former ally"));
        // a new alliance with another seat, and the next line goes there and nowhere else
        ASSERT_TRUE(m.ally(0, 2));
        ASSERT_TRUE(m.say(0, "to the new ally", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[2], "to the new ally") && has_line(m.heard[0], "to the new ally"));
        ASSERT_FALSE(has_line(m.heard[1], "to the new ally"));
        ASSERT_TRUE(m.raw[3].empty());
    } TEST_END();

    TEST_CASE("N2.92 Team Chat: A Host On The Local Network (It Holds A Seat) Relays The Same Way And Is A Receiver Like The Guests: It Hears A Team Line Only When It Is The Sender Or The Sender's Ally; Lines For All Reach Everybody, The Raw Client Included") {
        TeamChatRig m(true, 4, 0x08);                                    // the host is seat 0, seats 1 and 2 are guests with sessions, seat 3 is a raw client
        ASSERT_FALSE(m.host->seatless());
        ASSERT_TRUE(m.ally(1, 2));                                       // two guests are allies; the host is not one of them
        ASSERT_TRUE(m.say(1, "guest team", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[1], "guest team") && has_line(m.heard[2], "guest team"));
        ASSERT_FALSE(has_line(m.host_heard, "guest team"));              // the host's own screen is not told either
        ASSERT_TRUE(m.raw[3].empty() && m.on_wire(3) == 0);
        ASSERT_TRUE(m.say(0, "host team", true));                        // the host has no ally: the line is its own screen's and nobody else's
        m.step(300);
        ASSERT_TRUE(has_line(m.host_heard, "host team"));
        ASSERT_FALSE(has_line(m.heard[1], "host team") || has_line(m.heard[2], "host team"));
        ASSERT_TRUE(m.raw[3].empty() && m.on_wire(3) == 0);
        ASSERT_TRUE(m.ally(0, 1));                                       // the host teams up with seat 1 (the pair of the guests ends)
        ASSERT_EQ(m.host_engine.alliance_of(2), sim::ALLIANCE_NONE);
        ASSERT_TRUE(m.say(0, "host team 2", true));
        ASSERT_TRUE(m.say(1, "guest team 2", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[1], "host team 2") && !has_line(m.heard[2], "host team 2"));
        ASSERT_TRUE(has_line(m.host_heard, "guest team 2") && has_line(m.heard[1], "guest team 2") && !has_line(m.heard[2], "guest team 2"));      // the host hears its ally
        ASSERT_TRUE(m.raw[3].empty() && m.on_wire(3) == 0);
        ASSERT_TRUE(m.say(2, "all of you", false));                      // a line for all: the host, the guests and the raw client
        ASSERT_TRUE(m.say(0, "host to all", false));
        m.step(300);
        ASSERT_TRUE(has_line(m.host_heard, "all of you") && has_line(m.heard[1], "all of you") && has_line(m.heard[2], "all of you"));
        ASSERT_TRUE(has_line(m.heard[1], "host to all") && has_line(m.heard[2], "host to all"));
        ASSERT_TRUE(m.raw[3].size() == 2 && m.on_wire(3) == 2);
    } TEST_END();

    TEST_CASE("N2.93 The Match's Chat Has A Budget Of Its Own (A Remake Protection: The Original Has No Limit On Chat): A Client Says A Burst Of 5 Lines And Then One A Second; A Line Beyond It Is Dropped (Nobody Hears It, Nothing Is Logged, No Offence At First); A Client That Goes On Beyond It For Long Is Thrown Out; Team Lines Count The Same; Each Client Has Its Own; The Host's Own Player Is Not A Connection And Has No Budget; An Honest Talker Is Never Touched") {
        {   // a burst of 8 from one client (seat 0): 5 reach the others (and itself), in order; the sender stays
            TeamChatRig m(false, 3, 0);
            for (int i = 0; i < 8; ++i) ASSERT_TRUE(m.say(0, "burst " + std::to_string(i), false));
            m.step(400);
            ASSERT_EQ(m.heard[1].size(), size_t{kChatBurst});
            ASSERT_EQ(m.heard[0].size(), size_t{kChatBurst});
            for (size_t i = 0; i < m.heard[1].size(); ++i) ASSERT_EQ(m.heard[1][i].text, "burst " + std::to_string(i));
            ASSERT_EQ(m.host_heard.size(), size_t{kChatBurst});                          // (the referee's callback hears what was relayed, not what was dropped)
            ASSERT_TRUE(m.host->client_present(0));
            ASSERT_EQ(m.host->violations(0), 0u);
            // a second later one more, not two; five quiet seconds fill the burst again
            m.step(1000);
            ASSERT_TRUE(m.say(0, "after a second", false) && m.say(0, "right behind it", false));
            m.step(300);
            ASSERT_EQ(m.heard[1].size(), size_t{kChatBurst + 1});
            ASSERT_EQ(m.heard[1].back().text, std::string("after a second"));
            m.step(6000);
            for (int i = 0; i < 6; ++i) ASSERT_TRUE(m.say(0, "again " + std::to_string(i), false));
            m.step(300);
            ASSERT_EQ(m.heard[1].size(), size_t{kChatBurst + 1 + kChatBurst});
        }
        {   // a team line takes from the same budget; each client has its own; nothing else of the match is touched
            TeamChatRig m(false, 3, 0);
            ASSERT_TRUE(m.ally(0, 1));
            for (int i = 0; i < 3; ++i) ASSERT_TRUE(m.say(0, "team " + std::to_string(i), true));
            for (int i = 0; i < 3; ++i) ASSERT_TRUE(m.say(0, "all " + std::to_string(i), false));
            for (int i = 0; i < 2; ++i) ASSERT_TRUE(m.say(2, "other " + std::to_string(i), false));
            m.step(400);
            size_t from_0 = 0, from_2 = 0;
            for (const ChatMsg& c : m.heard[1]) (c.sender == 0 ? from_0 : from_2) += 1;
            ASSERT_EQ(from_0, size_t{kChatBurst});                                       // 3 team + 3 all = 6 lines: the sixth is dropped
            ASSERT_EQ(from_2, size_t{2});                                                // seat 2 has its own burst
            ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
        }
        {   // an honest talker: a line every 1.2 s for a minute and a busy one, a line a second for two minutes: all heard, no offence
            TeamChatRig m(false, 3, 0);
            for (int i = 0; i < 50; ++i) {
                ASSERT_TRUE(m.say(0, "talk " + std::to_string(i), false));
                m.step(1200);
            }
            ASSERT_EQ(m.heard[1].size(), size_t{50});
            for (int i = 0; i < 120; ++i) {
                ASSERT_TRUE(m.say(2, "busy " + std::to_string(i), false));
                m.step(1000);
            }
            ASSERT_EQ(m.heard[1].size(), size_t{170});
            ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
            ASSERT_TRUE(m.host->violations(0) == 0u && m.host->violations(2) == 0u);
        }
        {   // two lines a second for a minute: half are dropped (the excess refills as fast as it is used), nobody is thrown out; four a second for long: the excess (20 lines) runs out after
            // about ten seconds, then each is a violation and eight throw the client out
            TeamChatRig m(false, 3, 0);
            for (int i = 0; i < 120; ++i) {
                ASSERT_TRUE(m.say(0, "two a second " + std::to_string(i), false));
                m.step(500);
            }
            ASSERT_TRUE(m.host->client_present(0));
            ASSERT_TRUE(m.heard[1].size() >= 60 && m.heard[1].size() <= 70);
            m.step(6000);
            const size_t before = m.heard[1].size();
            size_t sent = 0;
            while (m.host->client_present(0) && sent < 400) {
                ASSERT_TRUE(m.say(0, "four a second " + std::to_string(sent), false));
                ++sent;
                m.step(250);
            }
            ASSERT_FALSE(m.host->client_present(0));
            ASSERT_TRUE(sent >= 30 && sent <= 90);
            ASSERT_TRUE(m.heard[1].size() - before >= 10 && m.heard[1].size() - before <= 30);
            ASSERT_TRUE(m.host->client_present(1) && m.host->client_present(2));
        }
        {   // a flood is out within a pass; the other client's honest line is heard as ever
            TeamChatRig m(false, 3, 0);
            for (int i = 0; i < 900; ++i) m.client_ends[0]->send(encode(ChatMsg{0, false, "flood " + std::to_string(i)}));
            ASSERT_TRUE(m.say(1, "an honest line", false));
            m.step(300);
            ASSERT_FALSE(m.host->client_present(0));
            size_t honest = 0, spam = 0;
            for (const ChatMsg& c : m.heard[2]) (c.text == "an honest line" ? honest : spam) += 1;
            ASSERT_TRUE(honest == 1 && spam == kChatBurst);
        }
        {   // the host's own player (a game on the local network) is no connection: twenty lines at once all go out
            TeamChatRig m(true, 3, 0);
            for (int i = 0; i < 20; ++i) ASSERT_TRUE(m.say(0, "host line " + std::to_string(i), false));
            m.step(400);
            ASSERT_EQ(m.heard[1].size(), size_t{20});
            ASSERT_EQ(m.heard[2].size(), size_t{20});
            ASSERT_EQ(m.host_heard.size(), size_t{20});
        }
    } TEST_END();
}

}  // namespace

// ---- protocol 12: the start of a match (the first turn is sealed kMatchStartDelayMs after the match began: the "Get ready to play!" dialog of every machine) -------------------------------

namespace {

// The turns of a host's own runner that carry a command of `issuer` (it keeps the last 30 s of turns: the first seconds of a test are in it)
std::vector<uint32_t> turns_with(HostSession& host, uint8_t issuer) {
    std::vector<uint32_t> out;
    for (uint32_t t = 0; t < host.turns_sealed(); ++t) {
        const TurnMsg* turn = host.runner().logged_turn(t);
        if (turn == nullptr) continue;
        for (const Command& c : turn->commands) {
            if (c.issuer == issuer) out.push_back(t);
        }
    }
    return out;
}

// The turn that a command is sealed into when it reaches the host at `arrive_ms` on a host that sealed its first turn at `first_seal_ms` (a turn every 50 ms; the host reads what has arrived before it seals, so
// a command that arrives on the very pass of a seal is in that turn)
uint32_t turn_for_arrival(uint32_t arrive_ms, uint32_t first_seal_ms) { return (arrive_ms - first_seal_ms + kTurnMs - 1) / kTurnMs; }

}  // namespace

void run_protocol12_tests() {
    TEST_CASE("N2.94 Protocol 12, The Start Of A Match (A Host With A Seat): The Host Seals The First Turn Exactly start_delay_ms After start() And Nothing Before It; The Seconds Before Are No Stall, No Lag, No Pause And No Growth Of The Jitter Buffer; The First Turn Runs On Every Machine A Link's Delay And One Turn Of Buffer Later; The Match Is Then Identical Everywhere; With The Default Of 0 The First Turn Is Sealed At Once (The Rigs)") {
        ASSERT_EQ(kMatchStartDelayMs, 5000u);                                       // the dialog's 5 s (the original's task KWFO: Ants.exe 0x10254b0)
        ASSERT_EQ(kMatchStartDelayMs, sim::kMatchStartDialogMs);
        {   // the default is a host that seals at once, as every rig of this suite wants it
            Match m(3, 3, {40, 10});
            m.run(20, false);
            ASSERT_TRUE(m.host->turns_sealed() >= 1);
        }
        HostSession::Config hc;
        hc.start_delay_ms = kMatchStartDelayMs;
        Match m(5, 4, {40, 10}, hc);
        uint32_t first_seal = 0;
        uint32_t first_tick = 0;
        uint32_t first_tick_client = 0;
        while (m.now < 30000 && (first_seal == 0 || first_tick == 0 || first_tick_client == 0)) {
            m.run(10, false);
            if (first_seal == 0 && m.host->turns_sealed() >= 1) first_seal = m.now;
            if (first_tick == 0 && m.sims[0]->current_tick() >= 1) first_tick = m.now;
            if (first_tick_client == 0 && m.sims[3]->current_tick() >= 1) first_tick_client = m.now;
            if (first_seal == 0) {                                                  // the seconds before the first turn: nothing is sealed, run, waited for or reported
                ASSERT_EQ(m.host->turns_sealed(), 0u);
                ASSERT_TRUE(!m.host->paused() && !m.host->waiting() && m.host->lagging_mask() == 0 && m.host->laggard() == 255 && m.host->desyncs().empty());
                ASSERT_EQ(m.host->runner().next_turn_to_execute(), 0u);
                for (auto& c : m.clients) {
                    ASSERT_TRUE(c->mode() == ClientSession::Mode::Normal && c->connected() && !c->paused() && !c->catching_up());
                    ASSERT_TRUE(c->lagging_seat() == 255 && c->self_lag_behind_ms() == 0);
                    ASSERT_TRUE(c->runner().queued() == 0 && c->runner().next_turn_to_execute() == 0 && c->runner().next_turn_expected() == 0);
                    ASSERT_TRUE(c->runner().stalled_ms() == 0 && !c->runner().stalled() && !c->runner().rebuilding() && c->runner().buffer_turns() == 1);   // not a stall of the link
                }
                for (auto& sm : m.sims) ASSERT_EQ(sm->current_tick(), 0u);
            }
        }
        ASSERT_EQ(first_seal, kMatchStartDelayMs);                                  // sealed on the very pass that is 5 s after start()
        ASSERT_TRUE(first_tick >= kMatchStartDelayMs && first_tick <= kMatchStartDelayMs + 2 * kTurnMs);                       // the host's own runner begins with two turns in hand
        ASSERT_TRUE(first_tick_client >= kMatchStartDelayMs + 30 && first_tick_client <= kMatchStartDelayMs + 400);             // a client: the link's delay, the second turn, a few steps of jitter
        m.run(10000, false);
        for (auto& c : m.clients) ASSERT_TRUE(c->runner().buffer_turns() == 1 && !c->runner().stalled() && c->lagging_seat() == 255);   // the wait before the start left the buffer as it was
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
        ASSERT_TRUE(m.all_equal());
        ASSERT_TRUE(m.sims[0]->current_tick() > 150);
    } TEST_END();

    TEST_CASE("S2.15 Protocol 12, A Dedicated Server's Start: The Referee's First Turn Is Sealed Exactly start_delay_ms After start() And Nothing Before It, Nobody Is Behind, Announced As Lagging Or Paused In The Seconds Before It (A Room That Holds Seats Included); The Idle Rule Of The Lag Policy Does Not Count Those Seconds: A Seat That Never Acks Is Dropped Its Idle Time After The First Turn, Not After start()") {
        {   // the idle rule: seat 3's window never runs (no acks, no pings), the rule is 8 s (30 s by default: a shorter rule makes the difference visible)
            HostSession::Config hc;
            hc.start_delay_ms = kMatchStartDelayMs;
            hc.lag_drop_idle_ms = 8000;
            ServerMatch m(4, 4, {40, 10}, hc);
            m.frozen_mask = 1u << 3;
            uint32_t first_seal = 0;
            while (m.now < 5200 && first_seal == 0) {
                m.run(10, false);
                if (m.host->turns_sealed() >= 1) first_seal = m.now;
                if (first_seal == 0) {                                              // the quiet seconds: nothing is sealed, run or waited for
                    ASSERT_TRUE(m.host->turns_sealed() == 0 && m.referee.current_tick() == 0 && !m.host->paused() && m.host->lagging_mask() == 0);
                    ASSERT_TRUE(m.clients[0]->lagging_seat() == 255 && !m.clients[0]->catching_up() && !m.clients[0]->paused() && m.clients[0]->runner().stalled_ms() == 0);
                }
            }
            ASSERT_EQ(first_seal, kMatchStartDelayMs);
            m.run(7000, false);                                                     // 12 s after start(): the idle rule would have dropped the seat at 8 s if the quiet seconds counted
            ASSERT_TRUE(m.host->client_present(3));
            ASSERT_TRUE(m.clients[0]->lagging_seat() == 3);                         // (the others are told who lags once it is 3 s behind)
            m.run(2000, false);                                                     // 14 s: 9 s after the first turn, the idle time of 8 s is over
            ASSERT_FALSE(m.host->client_present(3));                                // dropped, the others play on
            for (auto& c : m.clients) ASSERT_FALSE(c->lost());
            m.run(1500, false);
            ASSERT_TRUE(m.sims[0]->is_player_dropped(3) && m.referee.is_player_dropped(3));
            ASSERT_TRUE(m.sims[0]->current_tick() > 100);
        }
        {   // a room that holds seats: the seconds before the first turn are no pause (nobody is missing, nothing is announced), and the first turn comes on time
            HostSession::Config hc;
            hc.start_delay_ms = kMatchStartDelayMs;
            hc.hold_seats = true;
            ServerMatch m(5, 3, {40, 10}, hc);
            uint32_t first_seal = 0;
            while (m.now < 5200 && first_seal == 0) {
                m.run(10, false);
                if (m.host->turns_sealed() >= 1) first_seal = m.now;
                if (first_seal == 0) {
                    ASSERT_TRUE(!m.host->paused() && !m.host->attendance().paused() && m.host->rejoiners() == 0);
                    for (auto& c : m.clients) ASSERT_TRUE(!c->paused() && c->presence().missing.empty() && c->presence().resume_s == 0 && c->mode() == ClientSession::Mode::Normal);
                }
            }
            ASSERT_EQ(first_seal, kMatchStartDelayMs);
            m.run(8000, false);
            ASSERT_TRUE(!m.host->paused() && m.referee.current_tick() > 100);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty() && m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.95 Protocol 12, A Command That Reaches The Host Before Its First Turn Is Sealed Is Discarded, Not Counted As A Violation (A Host With A Seat): A Guest's Orders At 100 ms (Seventy At Once), 4,900 ms And 4,950 ms Of The 5 s Wait Are In No Turn, One That Arrives 10 ms After The First Seal Is In Turn 1, An Honest Guest's First Order Is Not Delayed By The Gate, And With start_delay_ms 0 A Command Is Accepted At Once") {
        const uint32_t link = 40;                                                   // one way, no jitter: what is sent at t arrives at t + 40
        HostSession::Config hc;
        hc.start_delay_ms = kMatchStartDelayMs;
        Match m(7, 3, {link, 0}, hc);                                               // the host plays seat 0; seat 1 is the guest that orders in the wait (a modified client, or a rig: a client has no gate of its own), seat 2 is honest
        const auto order = [&](uint8_t seat, int16_t x) { return cmd(CommandType::GroupMove, seat, 255, x, 12, m.ids.ants[seat]); };
        bool all_sent = true;
        uint32_t sealed_before_the_seal = 99;
        bool honest_sent = false;
        uint32_t honest_sent_at = 0;
        m.run(5400, false, [&](uint32_t now) {
            if (now == 100) {
                for (int i = 0; i < 70; ++i) all_sent = m.clients[0]->submit(order(1, static_cast<int16_t>(10 + i % 5))) && all_sent;       // seventy at once: a turn takes 64, the rest would be carried into the next ones
            }
            if (now == 4900) all_sent = m.clients[0]->submit(order(1, 30)) && all_sent;
            if (now == 4950) all_sent = m.clients[0]->submit(order(1, 11)) && all_sent;                                                    // arrives at 4990, 10 ms before the first seal
            if (now == 4970) all_sent = m.clients[0]->submit(order(1, 31)) && all_sent;                                                    // arrives at 5010, 10 ms after it
            if (now == 4990) sealed_before_the_seal = m.host->turns_sealed();
            if (!honest_sent && m.sims[2]->current_tick() >= 1) {                                                                          // seat 2's earliest order: the frame after its dialog is gone (its first turn has run)
                honest_sent = m.clients[1]->submit(order(2, 20));
                honest_sent_at = now;
            }
        });
        ASSERT_TRUE(all_sent && honest_sent);
        ASSERT_EQ(sealed_before_the_seal, 0u);                                      // nothing was sealed until 5000 ms
        ASSERT_TRUE(m.host->turns_sealed() >= 8);
        ASSERT_TRUE(turns_with(*m.host, 1) == std::vector<uint32_t>{1});            // of seat 1's seventy-three orders only the one that arrived at 5010 ms is in a turn: turn 1, sealed at 5050 ms; the others are in no turn (not turn 0, and none was kept for later)
        for (uint32_t t : {0u, 1u, 2u}) {
            const TurnMsg* turn = m.host->runner().logged_turn(t);
            ASSERT_TRUE(turn != nullptr);
            for (const Command& c : turn->commands) ASSERT_TRUE(t == 1 && c.issuer == 1 && c.tile_x == 31);        // the late order and nothing else in the first three turns
        }
        for (uint8_t seat : {uint8_t{1}, uint8_t{2}}) ASSERT_TRUE(m.host->client_present(seat) && m.host->violations(seat) == 0u);      // a discarded command is no offence: the seat is not on its way out
        const std::vector<uint32_t> honest = turns_with(*m.host, 2);                // seat 2 sent nothing in the wait, and its first order is not held back by the gate
        ASSERT_TRUE(honest.size() == 1);
        ASSERT_EQ(honest[0], turn_for_arrival(honest_sent_at + link, kMatchStartDelayMs));      // the first turn after its arrival (turn 3 on a link of 40 ms)
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
        ASSERT_TRUE(m.all_equal());
        {   // the default delay of 0 (every rig of this suite): the first pass reads what has arrived before it seals turn 0, and the command is in turn 0 (the gate is not "before turn 0 was sealed" for a match that does not wait)
            Match z(8, 2, {0, 0});
            bool sent = false;
            z.run(30, false, [&](uint32_t now) {
                if (now == 10) sent = z.clients[0]->submit(cmd(CommandType::GroupMove, 1, 255, 10, 12, z.ids.ants[1]));
            });
            ASSERT_TRUE(sent);
            ASSERT_TRUE(turns_with(*z.host, 1) == std::vector<uint32_t>{0});
            ASSERT_EQ(z.host->violations(1), 0u);
        }
    } TEST_END();

    TEST_CASE("S2.16 Protocol 12, A Command That Reaches A Dedicated Server's Referee Before Its First Turn Is Sealed Is Discarded Without A Violation: A Raw Seat (A Modified Client) That Writes Seventy Orders At 100 ms And One That Arrives 100 ms Before The First Seal Has Them In No Turn, A Raw Order After The First Seal Is Accepted, And The Honest Seats' First Orders Are Sealed Into The First Turn After They Arrive (On Links Of 40, 120 And 300 ms): The Raw Seat Has No Opening Ahead Of Them") {
        for (const uint32_t link : {40u, 120u, 300u}) {
            HostSession::Config hc;
            hc.start_delay_ms = kMatchStartDelayMs;
            ServerMatch m(11, 3, {link, 0}, hc);                                    // seats 0 and 1 play honestly, seat 2 is a raw connection that writes CommandMsg itself
            const auto order = [&](uint8_t seat, int16_t x) { return cmd(CommandType::GroupMove, seat, 255, x, 12, m.ids.ants[seat]); };
            bool honest_sent[2] = {false, false};
            uint32_t honest_sent_at[2] = {0, 0};
            m.run(7000 + 2 * link, false, [&](uint32_t now) {
                if (now == 100) {
                    for (int i = 0; i < 70; ++i) m.client_ends[2]->send(encode(CommandMsg{order(2, static_cast<int16_t>(10 + i % 5))}));   // a scripted opening (seventy orders)
                }
                if (now == 4900 - link) m.client_ends[2]->send(encode(CommandMsg{order(2, 30)}));          // timed to arrive at 4,900 ms, 100 ms before the first seal (on any link)
                if (now == 5100) m.client_ends[2]->send(encode(CommandMsg{order(2, 31)}));       // after the first seal: an order like any other
                for (size_t p = 0; p < 2; ++p) {
                    if (!honest_sent[p] && m.sims[p]->current_tick() >= 1) {                 // an honest client's earliest order: the frame after its dialog is gone (its first turn has run)
                        honest_sent[p] = m.clients[p]->submit(order(static_cast<uint8_t>(p), 20));
                        honest_sent_at[p] = now;
                    }
                }
            });
            ASSERT_TRUE(honest_sent[0] && honest_sent[1]);
            ASSERT_TRUE(turns_with(*m.host, 2) == std::vector<uint32_t>{turn_for_arrival(5100 + link, kMatchStartDelayMs)});   // only the order after the first seal, in the first turn that follows its arrival
            for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
                const std::vector<uint32_t> turns = turns_with(*m.host, seat);
                ASSERT_TRUE(turns.size() == 1);
                ASSERT_EQ(turns[0], turn_for_arrival(honest_sent_at[seat] + link, kMatchStartDelayMs));      // not held back: the turn after its arrival
            }
            for (uint8_t seat : {uint8_t{0}, uint8_t{1}, uint8_t{2}}) ASSERT_TRUE(m.host->client_present(seat) && m.host->violations(seat) == 0u);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Restart records, the session's part (docs/NETWORK_PORT.md "Restart records"): the attendance of a match that was brought back after a restart of the server, the hooks that the record
// is written from, and a host that is restored from the turns of its predecessor
// ---------------------------------------------------------------------------------------------------------------------------------

// The start message of a HoldMatch (what a machine that starts from nothing is sent first)
StartMsg start_of_hold(const HoldMatch& m) {
    StartMsg start;
    start.seed = m.seed;
    start.map_name = "TEST.LVL";
    start.roster = static_cast<uint8_t>((1u << m.seats) - 1u);
    for (uint8_t p = 0; p < m.seats; ++p) start.names[p] = "Seat " + std::to_string(p);
    return start;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Protocol 13: a level for each seat of the leader's fill, and teams in the start data
// ---------------------------------------------------------------------------------------------------------------------------------

void run_protocol13_tests() {
    TEST_CASE("N2.96 Protocol 13, The Messages: StartRequest Is The Type, A Level For Each Seat (0 .. 3) And Two Team Bytes (None Both, Or Two Different Seats 0 .. 3), Seven Bytes And Nothing Else; Start Ends With The Same Two Bytes And Its Decoder Takes Only Teams That Its Own Roster Can Make; The Notice Fits A Line Of Chat") {
        ASSERT_EQ(kProtocolVersion, 13);
        ASSERT_TRUE(kNoTeam == 255 && kFillLevelLast == 3);
        StartRequestMsg out;
        // the layout, byte by byte: the type, the levels of seats 0 to 3, team_a, team_b
        StartRequestMsg m;
        m.fill = {FillLevel::None, FillLevel::Easy, FillLevel::Medium, FillLevel::Hard};
        m.team_a = 2;
        m.team_b = 0;
        ASSERT_TRUE(encode(m) == (std::vector<uint8_t>{24, 0, 1, 2, 3, 2, 0}));
        ASSERT_TRUE(decode(encode(m), out) && out.fill == m.fill && out.team_a == 2 && out.team_b == 0);
        ASSERT_TRUE(encode(StartRequestMsg{}) == (std::vector<uint8_t>{24, 0, 0, 0, 0, 255, 255}));
        ASSERT_TRUE(StartRequestMsg::all(FillLevel::Medium).fill == (std::array<FillLevel, 4>{FillLevel::Medium, FillLevel::Medium, FillLevel::Medium, FillLevel::Medium}));
        // each seat's own level: every level at every seat, the others none
        for (uint8_t seat = 0; seat < 4; ++seat) {
            for (uint8_t level = 0; level <= kFillLevelLast; ++level) {
                StartRequestMsg one;
                one.fill[seat] = static_cast<FillLevel>(level);
                std::vector<uint8_t> want = {24, 0, 0, 0, 0, 255, 255};
                want[1u + seat] = level;
                ASSERT_TRUE(encode(one) == want);
                StartRequestMsg back = StartRequestMsg::all(FillLevel::Hard);
                ASSERT_TRUE(decode(want, back) && back.fill == one.fill && back.team_a == kNoTeam && back.team_b == kNoTeam);
            }
        }
        // a level above 3 in any seat, whatever the other bytes: no message (and the target is left alone)
        for (size_t at = 1; at <= 4; ++at) {
            for (unsigned value = 0; value < 256; ++value) {
                std::vector<uint8_t> bytes = {24, 1, 2, 3, 0, 255, 255};
                bytes[at] = static_cast<uint8_t>(value);
                StartRequestMsg keep = StartRequestMsg::all(FillLevel::Hard);
                const bool accepted = decode(bytes, keep);
                ASSERT_EQ(accepted, value <= kFillLevelLast);
                if (!accepted) ASSERT_TRUE(keep.fill == StartRequestMsg::all(FillLevel::Hard).fill && keep.team_a == kNoTeam);
            }
        }
        // the team bytes: every pair of byte values. Free for all is 255 and 255; a team is two DIFFERENT seats of 0 .. 3; nothing else is a message
        size_t pairs = 0;
        for (unsigned a = 0; a < 256; ++a) {
            for (unsigned b = 0; b < 256; ++b) {
                const std::vector<uint8_t> bytes = {24, 0, 0, 0, 0, static_cast<uint8_t>(a), static_cast<uint8_t>(b)};
                StartRequestMsg keep;
                keep.team_a = 7;
                const bool should = (a == 255 && b == 255) || (a < 4 && b < 4 && a != b);
                const bool accepted = decode(bytes, keep);
                ASSERT_EQ(accepted, should);
                if (accepted) {
                    ASSERT_TRUE(keep.team_a == a && keep.team_b == b && encode(keep) == bytes);
                    ASSERT_EQ(keep.teams() == sim::StartTeams{}, a == 255);
                    pairs += a == 255 ? 0u : 1u;
                } else {
                    ASSERT_EQ(keep.team_a, 7);
                }
            }
        }
        ASSERT_EQ(pairs, size_t{12});                                                          // 4 x 3 ordered pairs (the first seat invites)
        // the teams of a message as the model's value, and back
        for (uint8_t a = 0; a < 4; ++a) {
            for (uint8_t b = 0; b < 4; ++b) {
                if (a == b) continue;
                StartRequestMsg req;
                req.set_teams(sim::StartTeams{true, a, b});
                ASSERT_TRUE(req.teams() == sim::StartTeams({true, a, b}) && req.team_a == a && req.team_b == b);
                StartMsg st;
                st.set_teams(sim::StartTeams{true, a, b});
                ASSERT_TRUE(st.teams() == sim::StartTeams({true, a, b}) && st.team_a == a && st.team_b == b);
            }
        }
        {
            StartRequestMsg req;
            req.set_teams(sim::StartTeams{true, 1, 2});
            req.set_teams(sim::StartTeams{});                                                  // free for all again: both bytes
            ASSERT_TRUE(req.team_a == kNoTeam && req.team_b == kNoTeam && !req.teams().set);
        }
        // every strict prefix and every extra byte of a message with teams and levels is refused; no other type byte takes it
        const std::vector<uint8_t> full = encode(m);
        for (size_t cut = 0; cut < full.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut)), out));
        for (unsigned extra = 0; extra < 256; ++extra) {
            std::vector<uint8_t> longer = full;
            longer.push_back(static_cast<uint8_t>(extra));
            ASSERT_FALSE(decode(longer, out));
        }
        for (unsigned type = 0; type < 256; ++type) {
            if (type == 24) continue;
            std::vector<uint8_t> other = full;
            other[0] = static_cast<uint8_t>(type);
            ASSERT_FALSE(decode(other, out));
        }
        ASSERT_FALSE(decode(std::vector<uint8_t>{24, 2}, out));                                // protocol 11's two bytes: one level for all
        ASSERT_FALSE(decode(std::vector<uint8_t>{24, 0, 0, 0, 0}, out));                       // the levels without the teams

        // ---- the Start message: the two team bytes at the end, and the decoder's rule ----
        const auto start_with = [](uint8_t roster, uint8_t a, uint8_t b) {
            StartMsg st;
            st.seed = 77;
            st.map_name = "TREASURE.LVL";
            st.map_hash = 0x0102030405060708ull;
            st.roster = roster;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                if ((roster >> seat) & 1u) st.names[seat] = std::string("P") + static_cast<char>('0' + seat);
            }
            st.team_a = a;
            st.team_b = b;
            return st;
        };
        {
            const StartMsg st = start_with(0x0F, 0, 3);
            const std::vector<uint8_t> bytes = encode(st);
            ASSERT_TRUE(bytes.size() >= 3 && bytes[bytes.size() - 2] == 0 && bytes[bytes.size() - 1] == 3);       // appended to the layout: the last two bytes
            StartMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.team_a == 0 && back.team_b == 3 && back.roster == 0x0F && back.seed == 77 && back.names[2] == "P2" && encode(back) == bytes);
            const std::vector<uint8_t> ffa = encode(start_with(0x0F, kNoTeam, kNoTeam));
            ASSERT_EQ(ffa.size(), bytes.size());
            ASSERT_TRUE(ffa[ffa.size() - 2] == 255 && ffa[ffa.size() - 1] == 255);
            ASSERT_TRUE(decode(ffa, back) && back.team_a == kNoTeam && back.team_b == kNoTeam && !back.teams().set);
            std::vector<uint8_t> old_layout = ffa;                                                                // protocol 12's Start has no team bytes: it is no Start
            old_layout.resize(old_layout.size() - 2);
            ASSERT_FALSE(decode(old_layout, back));
            std::vector<uint8_t> one_short = ffa;
            one_short.pop_back();
            ASSERT_FALSE(decode(one_short, back));
            std::vector<uint8_t> longer = ffa;
            longer.push_back(255);
            ASSERT_FALSE(decode(longer, back));
        }
        // every pair of bytes against every roster: the decoder takes it exactly when it is free for all, or the plan of the model can make it for that roster
        size_t accepted_teams = 0;
        size_t refused_teams = 0;
        for (unsigned roster = 0; roster < 16; ++roster) {
            if (__builtin_popcount(roster) < 2) continue;                                                         // (a Start of fewer than two players is refused for that reason)
            for (unsigned a = 0; a < 256; a += (a < 6 ? 1 : (a == 254 ? 1 : 249))) {                               // 0 .. 5, 255 (the values between are no different)
                for (unsigned b = 0; b < 256; b += (b < 6 ? 1 : (b == 254 ? 1 : 249))) {
                    const StartMsg st = start_with(static_cast<uint8_t>(roster), static_cast<uint8_t>(a), static_cast<uint8_t>(b));
                    StartMsg back;
                    const bool plain = a == 255 && b == 255;
                    const bool can = !plain && sim::plan_start_teams(sim::StartTeams{true, static_cast<uint8_t>(a), static_cast<uint8_t>(b)}, static_cast<uint8_t>(roster)).why.empty();
                    const bool accepted = decode(encode(st), back);
                    ASSERT_EQ(accepted, plain || can);
                    if (accepted) {
                        ASSERT_TRUE(back.team_a == a && back.team_b == b && encode(back) == encode(st));
                        accepted_teams += plain ? 0u : 1u;
                    } else {
                        ++refused_teams;
                    }
                }
            }
        }
        ASSERT_TRUE(accepted_teams > 30 && refused_teams > 100);
        // the cases that matter by name
        {
            StartMsg back;
            ASSERT_TRUE(decode(encode(start_with(0x0F, 0, 1)), back));                                              // four seats: the pair (and the other two, as a team)
            ASSERT_TRUE(decode(encode(start_with(0x07, 1, 2)), back));                                              // three seats: the pair, the third alone
            ASSERT_TRUE(decode(encode(start_with(0x0D, 3, 0)), back));                                              // seats 0, 2, 3 play: the pair 3 + 0, seat 2 alone
            ASSERT_FALSE(decode(encode(start_with(0x03, 0, 1)), back));                                             // two seats: the team would be the whole match (the original ends it at once)
            ASSERT_FALSE(decode(encode(start_with(0x07, 0, 3)), back));                                             // seat 3 does not play
            ASSERT_FALSE(decode(encode(start_with(0x0F, 2, 2)), back));                                             // one seat is no team
            ASSERT_FALSE(decode(encode(start_with(0x0F, 255, 1)), back));                                           // one of the bytes only
            ASSERT_FALSE(decode(encode(start_with(0x0F, 1, 255)), back));
            ASSERT_FALSE(decode(encode(start_with(0x0F, 4, 5)), back));                                             // seats that no match has
        }
        // ---- the notice: "No teams: " and the reason is one line of chat, whatever the teams and the seats ----
        ASSERT_EQ(std::string(kNoticeNoTeams), std::string("No teams: "));
        for (unsigned roster = 0; roster < 16; ++roster) {
            for (uint8_t a = 0; a < 4; ++a) {
                for (uint8_t b = 0; b < 4; ++b) {
                    const sim::StartTeamsPlan plan = sim::plan_start_teams(sim::StartTeams{true, a, b}, static_cast<uint8_t>(roster));
                    if (plan.why.empty()) continue;
                    const std::string text = std::string(kNoticeNoTeams) + plan.short_why;
                    ASSERT_TRUE(text.size() <= kMaxChatChars);
                    ChatMsg notice;
                    notice.sender = kRoomSender;
                    notice.text = text;
                    ChatMsg back;
                    ASSERT_TRUE(decode(encode(notice), back) && back.text == text);
                }
            }
        }
    } TEST_END();
}

void run_restart_tests() {
    TEST_CASE("N2.96 Attendance After A Restart Of The Server: Every Seat Of A Person Is Absent (Excused) From The Moment The Match Is Restored, The Seats That Were Dropped Stay Dropped; The Vote Opens After The Restart's Wait (90 s), Not A Lost Link's 30 s, And Nobody Can Win It While Nobody Is There; An Excused Absence Adds Nothing To The Away Time And Is No Loss For The Flapping Rule; The Pause Counts Toward The Cap And The Countdown Follows It; The Same Decisions From Four Clocks") {
        const uint32_t origins[] = {0u, 123456u, 0x7FFFF000u, 0xFFFFF000u};              // (the clock wraps after 49.7 days: a restart's clock starts at 0, a long-lived server's is anywhere)
        for (const uint32_t t0 : origins) {
            Attendance::Config c;
            c.vote_after_ms = 30000;
            c.restart_vote_after_ms = 90000;
            c.max_pause_ms = 400000;
            c.resume_countdown_ms = 10000;
            Attendance a(c);
            a.seat_restored(0x0F, 0x08, t0);                                              // four persons, seat 3 had been dropped before the restart
            ASSERT_TRUE(a.state(0) == Attendance::State::Absent && a.state(1) == Attendance::State::Absent && a.state(2) == Attendance::State::Absent);
            ASSERT_EQ(a.state(3), Attendance::State::Dropped);
            ASSERT_TRUE(a.paused() && !a.counting_down(t0));
            ASSERT_TRUE(a.drops_by_vote() == 0 && a.drops_by_cap() == 0 && a.rejoins() == 0 && a.rejoins_refused() == 0);          // (a seat dropped before the restart is no drop of this run)
            for (uint8_t seat = 0; seat < 3; ++seat) ASSERT_FALSE(a.flapping(seat, t0 + 1000));                                       // (no seat has lost its link: the restart is nobody's loss)
            // the vote: a restart's wait, not a lost link's
            ASSERT_EQ(a.vote_subject(t0 + 30000), 255);
            ASSERT_EQ(a.vote_subject(t0 + 89999), 255);
            ASSERT_EQ(a.vote_subject(t0 + 90000), 0);                                     // (of a tie the lowest seat)
            ASSERT_TRUE(a.update(t0 + 95000).empty());                                    // nobody is there to vote: more than half of nobody is not a majority
            ASSERT_EQ(a.connected_humans(0), 0);
            // a control: a seat whose link is LOST is put to the vote at 30 s
            Attendance control(c);
            control.seat_humans(0x07, t0);
            ASSERT_TRUE(control.lost(0, t0 + 1000));
            ASSERT_EQ(control.vote_subject(t0 + 1000 + 29999), 255);
            ASSERT_EQ(control.vote_subject(t0 + 1000 + 30000), 0);
            // the pause counts as any pause does, and the presence tells it
            const PresenceMsg p = a.presence_for(255, t0 + 30000);
            ASSERT_EQ(p.missing.size(), size_t{3});
            ASSERT_TRUE(p.missing[0].seat == 0 && p.missing[1].seat == 1 && p.missing[2].seat == 2 && p.missing[0].waited_s == 30 && p.missing[0].state == PresenceMsg::State::Absent);
            ASSERT_TRUE(p.vote_seat == 255 && p.voters == 0 && p.cap_s == 370 && p.resume_s == 0);
            PresenceMsg decoded;
            ASSERT_TRUE(decode(encode(p), decoded) && decoded.missing.size() == 3 && decoded.cap_s == 370);                           // (the message is one that the decoder accepts)
            ASSERT_EQ(a.pause_ms(t0 + 30000), 30000u);
            // a seat comes back: its absence adds nothing to its away time, and a loss that follows is judged as the seat's first (30 s, no flapping)
            ASSERT_TRUE(a.returning(0, t0 + 20000));
            ASSERT_EQ(a.state(0), Attendance::State::CatchingUp);
            ASSERT_TRUE(a.caught_up(0, t0 + 22000));
            ASSERT_EQ(a.state(0), Attendance::State::Present);
            ASSERT_EQ(a.away_ms(0, t0 + 25000), 0u);                                      // (an absence of 22 s that the seat was not to blame for)
            ASSERT_TRUE(a.paused());                                                      // (seats 1 and 2 are still away)
            ASSERT_TRUE(a.lost(0, t0 + 40000));
            ASSERT_FALSE(a.flapping(0, t0 + 40000));
            ASSERT_EQ(a.vote_subject(t0 + 69999), 255);                                   // 30 s after ITS loss, not 30 s of total away time (and the others wait for their 90 s)
            ASSERT_EQ(a.vote_subject(t0 + 70000), 0);
            ASSERT_EQ(a.away_ms(0, t0 + 70000), 30000u);
            ASSERT_EQ(a.vote_subject(t0 + 90000), 1);                                     // (at 90 s the restart's seats are due too, and they have been away longer than seat 0's 50 s: the longest total is the subject, of a tie the lowest seat)
            // the flapping rule: the restart is no loss, so three real losses are needed
            Attendance f(c);
            f.seat_restored(0x03, 0, t0);
            ASSERT_TRUE(f.returning(0, t0 + 5000) && f.caught_up(0, t0 + 6000));
            ASSERT_TRUE(f.lost(0, t0 + 10000));
            ASSERT_TRUE(f.returning(0, t0 + 11000) && f.caught_up(0, t0 + 12000));
            ASSERT_TRUE(f.lost(0, t0 + 14000));
            ASSERT_FALSE(f.flapping(0, t0 + 14500));                                      // two losses: if the restart counted as one this would be the third
            ASSERT_TRUE(f.returning(0, t0 + 15000) && f.caught_up(0, t0 + 16000));
            ASSERT_TRUE(f.lost(0, t0 + 18000));
            ASSERT_TRUE(f.flapping(0, t0 + 18500));                                       // the third real loss within a minute
            // the cap: the restart's pause counts toward the match's total pause, and at the cap every seat that is not present is dropped
            Attendance k(c);
            k.seat_restored(0x07, 0, t0);
            ASSERT_TRUE(k.update(t0 + 399999).empty());
            ASSERT_EQ(k.cap_left_ms(t0 + 399999), 1u);
            const std::vector<uint8_t> dropped = k.update(t0 + 400000);
            ASSERT_EQ(dropped.size(), size_t{3});
            ASSERT_TRUE(k.drops_by_cap() == 3 && k.drops_by_vote() == 0 && !k.paused());
            ASSERT_TRUE(k.state(0) == Attendance::State::Dropped && k.state(1) == Attendance::State::Dropped && k.state(2) == Attendance::State::Dropped);
            // the countdown that follows a pause of 3 s or more: when the last seat is back
            Attendance r(c);
            r.seat_restored(0x03, 0, t0);
            ASSERT_TRUE(r.returning(0, t0 + 5000) && r.caught_up(0, t0 + 6000));
            ASSERT_TRUE(r.paused());
            ASSERT_TRUE(r.returning(1, t0 + 7000) && r.caught_up(1, t0 + 8000));
            ASSERT_FALSE(r.paused());
            ASSERT_TRUE(r.counting_down(t0 + 8000) && r.resume_s(t0 + 8000) == 10);
            ASSERT_FALSE(r.counting_down(t0 + 18000));
            // a seat that was dropped before the restart cannot come back: its key is told "dropped", nothing is counted against it
            ASSERT_FALSE(a.returning(3, t0 + 1000));
            ASSERT_EQ(a.rejoins_refused(), 0u);
            ASSERT_FALSE(a.dropped(3, t0 + 2000));
            // nothing to hold: no persons, or only seats that were dropped: nobody is waited for
            Attendance none(c);
            none.seat_restored(0, 0, t0);
            ASSERT_FALSE(none.paused());
            Attendance gone(c);
            gone.seat_restored(0x03, 0x03, t0);
            ASSERT_FALSE(gone.paused());
            ASSERT_TRUE(gone.state(0) == Attendance::State::Dropped && gone.state(1) == Attendance::State::Dropped && gone.update(t0 + 1000000).empty());
        }
        // the restart's wait is a setting like the others: shorter than a lost link's it is still what it says (the room never sets less than its own: Room::build_session)
        Attendance::Config quick;
        quick.vote_after_ms = 30000;
        quick.restart_vote_after_ms = 5000;
        Attendance q(quick);
        q.seat_restored(0x03, 0, 1000);
        ASSERT_TRUE(q.vote_subject(5999) == 255 && q.vote_subject(6000) == 0);
        ASSERT_EQ(kRestartVoteAfterMs, 90000u);                                            // the default: 90 s
    } TEST_END();

    TEST_CASE("N2.97 A Host Restored From The Sealed Turns Of Its Predecessor: The Restored Engine Has The State Hash The Uninterrupted One Had At The Restored Tick And At Every Later Tick When The Same Turns Follow; The Log Holds The Same Bytes; Every Seat Is Absent And The Match Is Paused; The Three Machines (Still In Memory, Their Links Gone) Come Back Through The Door With Their Keys, Are Compared With The Referee At The Restored Tick And The Match Goes On To The Same State Everywhere") {
        HoldMatch m(3);
        std::vector<TurnMsg> turns;
        std::vector<std::pair<uint32_t, uint64_t>> checks;                              // the referee's own hash after every 20th turn, as the record would hold it
        std::map<uint64_t, uint64_t> tick_hash;                                         // the referee's state after every tick of the uninterrupted match
        m.host->set_on_seal([&](const TurnMsg& t) { turns.push_back(t); });
        m.host->set_on_referee_hash([&](uint32_t turn, const sim::StateHash& h) { checks.emplace_back(turn, h.total); });
        m.host->runner().set_on_tick([&]() { tick_hash[m.referee.current_tick()] = m.referee.state_hash().total; });
        m.run(45000);                                                                   // 45 s of play: 900 turns, every kind of command (the script)
        ASSERT_TRUE(turns.size() >= 880);
        for (size_t i = 0; i < turns.size(); ++i) ASSERT_EQ(turns[i].turn, static_cast<uint32_t>(i));       // (the hook saw every turn once, in order)
        ASSERT_TRUE(checks.size() >= 43);
        for (size_t i = 0; i < checks.size(); ++i) ASSERT_EQ(checks[i].first, static_cast<uint32_t>(i * kHashEveryTurns + kHashEveryTurns - 1));     // (turn 19, 39, 59, ...)
        std::vector<uint8_t> original_log;
        for (uint32_t at = 0; at < m.host->log().turns();) {
            const uint32_t got = m.host->log().read(at, 4096, 1u << 30, original_log);
            ASSERT_TRUE(got > 0);
            at += got;
        }
        ASSERT_EQ(m.host->log().turns(), static_cast<uint32_t>(turns.size()));
        const uint64_t executed = m.referee.current_tick();                             // the referee runs a turn or two behind the sealing
        ASSERT_TRUE(executed >= turns.size() - 4 && executed <= turns.size());
        ASSERT_TRUE(tick_hash.count(executed) == 1);
        // ---- the restored engine: the first `executed` turns, then one at a time ----------------------------------------------------------------------------------
        {
            sim::SimulationEngine b;
            build_world(b, m.seed, m.match_ms);
            HostSession hb(b, m.host_cfg);
            hb.set_seat_keys(m.keys);
            hb.set_rejoin_start(start_of_hold(m));
            for (uint64_t i = 0; i < executed; ++i) ASSERT_TRUE(hb.restore_turn(turns[static_cast<size_t>(i)]));
            ASSERT_EQ(hb.restored_turns(), static_cast<uint32_t>(executed));
            ASSERT_EQ(hb.runner().fast_forward(static_cast<uint32_t>(hb.runner().queued())), static_cast<uint32_t>(executed));
            ASSERT_TRUE(hb.runner().at_boundary() && hb.runner().queued() == 0 && hb.runner().next_turn_to_execute() == static_cast<uint32_t>(executed));
            ASSERT_EQ(b.current_tick(), executed);
            ASSERT_EQ(b.state_hash().total, tick_hash[executed]);                       // THE restored tick: the same state, hash for hash
            for (uint64_t i = executed; i < turns.size(); ++i) {                        // and every tick after it, when the same turns follow
                ASSERT_TRUE(hb.restore_turn(turns[static_cast<size_t>(i)]));
                ASSERT_EQ(hb.runner().fast_forward(1), 1u);
                const uint64_t tick = b.current_tick();
                ASSERT_EQ(tick, i + 1);
                if (tick_hash.count(tick) == 1) ASSERT_EQ(b.state_hash().total, tick_hash[tick]);
            }
            // the log is the match: the same bytes in the same order
            std::vector<uint8_t> restored_log;
            for (uint32_t at = 0; at < hb.log().turns();) {
                const uint32_t got = hb.log().read(at, 4096, 1u << 30, restored_log);
                ASSERT_TRUE(got > 0);
                at += got;
            }
            ASSERT_EQ(hb.log().turns(), m.host->log().turns());
            ASSERT_TRUE(restored_log == original_log);
            // the replay of the first run's checkpoints agrees as well (the turns of 20 are a boundary of the replay: the room verifies exactly this)
            {
                sim::SimulationEngine c;
                build_world(c, m.seed, m.match_ms);
                HostSession hc(c, m.host_cfg);
                size_t next_check = 0;
                for (const TurnMsg& t : turns) {
                    ASSERT_TRUE(hc.restore_turn(t));
                    if ((t.turn + 1) % kHashEveryTurns != 0) continue;
                    hc.runner().fast_forward(static_cast<uint32_t>(hc.runner().queued()));
                    ASSERT_TRUE(next_check < checks.size() && checks[next_check].first == t.turn);
                    ASSERT_EQ(c.state_hash().total, checks[next_check].second);
                    ++next_check;
                }
            }
            // begins the match: nothing is sealed, every seat is held
            hb.start_restored(5000, 0x07, 0x00);
            ASSERT_TRUE(hb.paused());
            ASSERT_EQ(hb.turns_sealed(), static_cast<uint32_t>(turns.size()));
            for (uint8_t seat = 0; seat < 3; ++seat) ASSERT_TRUE(hb.attendance().state(seat) == Attendance::State::Absent && hb.seat_held(seat) && !hb.client_present(seat));
            hb.update(6000);
            hb.update(60000);
            ASSERT_EQ(hb.turns_sealed(), static_cast<uint32_t>(turns.size()));          // paused: nothing is sealed, however long it waits
            ASSERT_TRUE(hb.desyncs().empty());
        }
        // ---- the machines come back: the same rig, its host replaced by one that was restored ---------------------------------------------------------------------
        for (uint8_t seat = 0; seat < 3; ++seat) {
            m.auto_reconnect[seat] = false;
            m.cut(seat);                                                                // the old server is gone: every link is cut
        }
        m.host.reset();
        const StartMsg start = start_of_hold(m);
        build_world(m.referee, m.seed, m.match_ms);                                     // a fresh engine, the same object
        m.host = std::make_unique<HostSession>(m.referee, m.host_cfg);
        m.host->set_seat_keys(m.keys);
        m.host->set_rejoin_start(start);
        for (size_t i = 0; i < turns.size(); ++i) {
            ASSERT_TRUE(m.host->restore_turn(turns[i]));
            if ((i + 1) % kHashEveryTurns == 0) m.host->runner().fast_forward(static_cast<uint32_t>(m.host->runner().queued()));
        }
        m.host->runner().fast_forward(static_cast<uint32_t>(m.host->runner().queued()));
        m.host->start_restored(m.now, 0x07, 0x00);
        const uint32_t sealed_at_restore = m.sealed();
        ASSERT_EQ(sealed_at_restore, static_cast<uint32_t>(turns.size()));
        m.run(2000, false);
        ASSERT_TRUE(m.host->paused() && m.sealed() == sealed_at_restore);               // the machines have no link yet (they are told to wait)
        for (uint8_t seat = 0; seat < 3; ++seat) ASSERT_EQ(static_cast<int>(m.clients[seat]->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));
        for (uint8_t seat = 0; seat < 3; ++seat) m.auto_reconnect[seat] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 20000));               // all three said Hello with their keys, were given what they lacked and agreed with the referee at the restored tick
        ASSERT_EQ(m.host->attendance().rejoins(), 3u);
        for (uint8_t seat = 0; seat < 3; ++seat) ASSERT_TRUE(m.host->client_present(seat) && m.host->attendance().state(seat) == Attendance::State::Present);
        m.run(15000);
        ASSERT_TRUE(m.sealed() > sealed_at_restore + 200);                              // the match goes on (the schedule did not burst)
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced() || c->lost());
        ASSERT_TRUE(m.all_equal());                                                     // the restored referee and the three machines: one state
        ASSERT_EQ(m.host->attendance().drops_by_cap() + m.host->attendance().drops_by_vote(), 0u);
    } TEST_END();

    TEST_CASE("N2.98 The Hooks That The Record Is Written From: set_on_seal Runs For Every Turn, In Order, Once, After The Turn Is In The Log And BEFORE Any Client Has Been Sent It (A Process That Dies At Any Moment Leaves No Client With A Turn That The Record Lacks); It Runs For A Host That Does Not Hold Seats Too; set_on_referee_hash Runs For Turns 19, 39, ... With The Referee's Own State After That Turn, And Only For A Host Without A Seat") {
        {   // a host that holds seats: the log has the turn, the clients have not been sent it yet
            HoldMatch m(3);
            std::vector<uint32_t> seen;
            uint32_t late_in_log = 0;
            uint32_t early_sends = 0;
            m.host->set_on_seal([&](const TurnMsg& t) {
                seen.push_back(t.turn);
                if (m.host->log().turns() != t.turn + 1) ++late_in_log;                  // the log has this turn already (the hook is called after the append) ...
                for (uint8_t p = 0; p < 3; ++p) {
                    if (m.taps[p] && m.taps[p]->sent_of(MsgType::Turn) > t.turn) ++early_sends;      // ... and the turn has not been sent to anybody (the taps count the Turn messages that they were given)
                }
            });
            std::map<uint64_t, uint64_t> tick_hash;
            std::vector<std::pair<uint32_t, uint64_t>> hashes;
            m.host->runner().set_on_tick([&]() { tick_hash[m.referee.current_tick()] = m.referee.state_hash().total; });
            m.host->set_on_referee_hash([&](uint32_t turn, const sim::StateHash& h) { hashes.emplace_back(turn, h.total); });
            m.run(30000);
            ASSERT_TRUE(seen.size() >= 580);
            for (size_t i = 0; i < seen.size(); ++i) ASSERT_EQ(seen[i], static_cast<uint32_t>(i));
            ASSERT_EQ(seen.size(), static_cast<size_t>(m.sealed()));                     // once for every sealed turn
            ASSERT_EQ(late_in_log, 0u);
            ASSERT_EQ(early_sends, 0u);
            ASSERT_TRUE(m.taps[0]->sent_of(MsgType::Turn) >= 580);                       // (the clients were sent the turns, of course: after the hook)
            ASSERT_TRUE(hashes.size() >= 28);
            for (size_t i = 0; i < hashes.size(); ++i) {
                ASSERT_EQ(hashes[i].first, static_cast<uint32_t>(i * kHashEveryTurns + kHashEveryTurns - 1));
                ASSERT_TRUE(tick_hash.count(hashes[i].first + 1) == 1);
                ASSERT_EQ(hashes[i].second, tick_hash[hashes[i].first + 1]);             // the state after that turn: the hash that the runner took when it ran it, not the engine's state at the time of the call
            }
        }
        {   // a host that does not hold seats: the seal hook still runs (the record is the room's business, not the log's), and it still has no log
            HoldOptions o;
            o.hold = false;
            HoldMatch m(3, o);
            uint32_t count = 0;
            m.host->set_on_seal([&](const TurnMsg& t) { count += t.turn == count ? 1u : 0u; });
            m.run(4000);
            ASSERT_TRUE(count >= 75 && count == m.sealed());
            ASSERT_EQ(m.host->log().turns(), 0u);
        }
        {   // a host with a seat reports its hashes through the sequencer like everybody and has no referee hook
            Match m(1, 3, {30, 10});
            uint32_t sealed = 0;
            uint32_t referee_calls = 0;
            m.host->set_on_seal([&](const TurnMsg& t) { sealed += t.turn == sealed ? 1u : 0u; });
            m.host->set_on_referee_hash([&](uint32_t, const sim::StateHash&) { ++referee_calls; });
            m.run(6000);
            ASSERT_TRUE(sealed >= 115 && sealed == m.host->turns_sealed());
            ASSERT_EQ(referee_calls, 0u);
        }
    } TEST_END();

    TEST_CASE("N2.99 What A Restored Host Refuses: A Session That Holds No Seats, A Turn That Is Not The Next, A Turn After start_restored, A Log That Cannot Hold The Match (Its Own Limit Or The Server's Budget); A Seat Without A Key Is Dropped At The Restart (Nobody Could Come Back To It) And Its Drop Is Sealed After The Pause; A Match Restored With No Turns (The Seconds Before Its First Turn) Waits For Its Players And Begins At Turn 0 Without Any Command Of The Pre-Start")  {
        {
            sim::SimulationEngine e;
            build_world(e, 1);
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            hc.hold_seats = false;
            HostSession h(e, hc);
            ASSERT_FALSE(h.restore_turn(TurnMsg{}));                                      // a room that holds no seats has no log to hold the match in
        }
        {
            sim::SimulationEngine e;
            build_world(e, 1);
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            hc.hold_seats = true;
            HostSession h(e, hc);
            std::array<SeatKey, sim::MAX_PLAYERS> keys{};
            keys[0] = key_with(1);
            keys[1] = key_with(2);
            h.set_seat_keys(keys);
            TurnMsg t0;
            t0.turn = 0;
            TurnMsg t1;
            t1.turn = 1;
            ASSERT_FALSE(h.restore_turn(t1));                                             // not the next one
            ASSERT_TRUE(h.restore_turn(t0));
            ASSERT_FALSE(h.restore_turn(t0));                                             // a repeat
            ASSERT_TRUE(h.restore_turn(t1));
            ASSERT_EQ(h.restored_turns(), 2u);
            h.runner().fast_forward(2);
            h.start_restored(1000, 0x03, 0x00);
            TurnMsg t2;
            t2.turn = 2;
            ASSERT_FALSE(h.restore_turn(t2));                                             // the match has begun
            h.start_restored(2000, 0x03, 0x00);                                           // (a second start changes nothing)
            ASSERT_TRUE(h.attendance().state(0) == Attendance::State::Absent && h.turns_sealed() == 2u);
        }
        {   // the log's own limit (its first allocation is 1.5 KB: a limit of 1 KB holds a few empty turns only) and the server's budget
            sim::SimulationEngine e;
            build_world(e, 1);
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            hc.hold_seats = true;
            hc.max_log_bytes = 1024;
            HostSession h(e, hc);
            uint32_t taken = 0;
            for (uint32_t i = 0; i < 400; ++i) {
                TurnMsg t;
                t.turn = i;
                if (!h.restore_turn(t)) break;
                ++taken;
            }
            ASSERT_TRUE(taken > 100 && taken < 400);                                      // refused when the log is full: the match does not fit
            LogBudget budget(1000);                                                       // a budget of less than the first allocation keeps no log at all
            sim::SimulationEngine e2;
            build_world(e2, 1);
            hc.max_log_bytes = TurnLog::kDefaultMaxBytes;
            hc.log_budget = &budget;
            HostSession h2(e2, hc);
            ASSERT_FALSE(h2.restore_turn(TurnMsg{}));
            ASSERT_EQ(budget.used(), 0u);
        }
        {   // a seat without a key (the maker failed for it): nobody can come back to it, so it is dropped, at the restart, and its Drop is sealed in the first turn after the pause
            HoldMatch m(3);
            std::vector<TurnMsg> turns;
            m.host->set_on_seal([&](const TurnMsg& t) { turns.push_back(t); });
            m.run(8000);
            for (uint8_t seat = 0; seat < 3; ++seat) {
                m.auto_reconnect[seat] = false;
                m.cut(seat);
            }
            m.host.reset();
            build_world(m.referee, m.seed, m.match_ms);
            m.host = std::make_unique<HostSession>(m.referee, m.host_cfg);
            std::array<SeatKey, sim::MAX_PLAYERS> keys = m.keys;
            keys[1] = SeatKey{};                                                          // seat 1 never got a key
            m.host->set_seat_keys(keys);
            m.host->set_rejoin_start(start_of_hold(m));
            for (const TurnMsg& t : turns) ASSERT_TRUE(m.host->restore_turn(t));
            m.host->runner().fast_forward(static_cast<uint32_t>(m.host->runner().queued()));
            m.host->start_restored(m.now, 0x07, 0x00);
            ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);         // (nobody could come back to it)
            ASSERT_TRUE(m.host->attendance().state(0) == Attendance::State::Absent && m.host->attendance().state(2) == Attendance::State::Absent);
            ASSERT_TRUE(m.host->paused());
            for (uint8_t seat : {uint8_t{0}, uint8_t{2}}) m.auto_reconnect[seat] = true;
            ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 20000));
            m.run(6000);
            m.settle();
            ASSERT_TRUE(m.referee.is_player_dropped(1) && !m.referee.is_player_dropped(0) && !m.referee.is_player_dropped(2));       // its Drop was sealed after the pause, on every machine
            ASSERT_TRUE(m.sims[0]->is_player_dropped(1) && m.sims[2]->is_player_dropped(1));
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[2]->state_hash() == m.referee.state_hash());
        }
        {   // no turns at all: the match had begun and the server stopped within the seconds before its first turn (a restored match waits for its players and begins at turn 0)
            HoldOptions o;
            o.host.start_delay_ms = kMatchStartDelayMs;
            HoldMatch m(3, o);
            m.run(2500, false);                                                           // 2.5 s into the 5 s of the dialog: nothing is sealed yet, nobody has given a command that counts
            ASSERT_EQ(m.sealed(), 0u);
            for (uint8_t seat = 0; seat < 3; ++seat) {
                m.auto_reconnect[seat] = false;
                m.cut(seat);
            }
            m.host.reset();
            build_world(m.referee, m.seed, m.match_ms);
            HostSession::Config hc = m.host_cfg;
            m.host = std::make_unique<HostSession>(m.referee, hc);
            m.host->set_seat_keys(m.keys);
            m.host->set_rejoin_start(start_of_hold(m));
            m.host->start_restored(m.now, 0x07, 0x00);                                    // (no restore_turn: the record held none)
            ASSERT_TRUE(m.host->paused() && m.host->turns_sealed() == 0u);
            m.run(3000, false);
            ASSERT_EQ(m.sealed(), 0u);
            for (uint8_t seat = 0; seat < 3; ++seat) m.auto_reconnect[seat] = true;
            ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 20000));              // every machine says Hello with its key (it has executed no turn: have_turns 0 is its count, it is given the match from nothing)
            m.run(4000, false);
            ASSERT_TRUE(m.sealed() > 0u);                                                 // the first turn is sealed when the pause ends, as it is after any pause
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();
}

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: lock-step core (protocol, sequencer, runner, sessions)\n"
                 "=======================================================\n";
    run_protocol_tests();
    run_network_tests();
    run_sequencer_tests();
    run_flood_budget_tests();
    run_runner_tests();
    run_reconnect_core_tests();
    run_match_tests();
    run_failure_tests();
    run_dropout_tests();
    run_migration_tests();
    run_reconnect_session_tests();
    run_protocol11_tests();
    run_team_chat_tests();
    run_protocol12_tests();
    run_protocol13_tests();
    run_restart_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
