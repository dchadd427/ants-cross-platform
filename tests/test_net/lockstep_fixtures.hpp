// The fixtures that the parts of the lock-step test share (test_lockstep.cpp holds main(); the cases are in the test_lockstep_*.cpp files). Inline or in an anonymous namespace,
// so that every part is a self-contained translation unit.
#pragma once
#include "ants_net/lobby.hpp"
#include "ants_net/loopback.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/sequencer.hpp"
#include "ants_net/session.hpp"
#include "ants_net/attendance.hpp"
#include "ants_net/turnlog.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "../common/test_assert.hpp"

using namespace ants;
using namespace ants::net;
using ants::sim::AntType;
using ants::sim::Command;
using ants::sim::CommandType;
using ants::sim::TileCoord;

inline int g_test_count = 0;
inline int g_test_failures = 0;
inline int g_assert_count = 0;

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


// The sessions hand the runner hooks that capture `this` (the delay meters): a copy or a move would leave them pointing at a session that is gone, so neither exists
static_assert(!std::is_copy_constructible<HostSession>::value && !std::is_copy_assignable<HostSession>::value, "a HostSession is never copied");
static_assert(!std::is_move_constructible<HostSession>::value && !std::is_move_assignable<HostSession>::value, "a HostSession is never moved");
static_assert(!std::is_copy_constructible<ClientSession>::value && !std::is_copy_assignable<ClientSession>::value, "a ClientSession is never copied");
static_assert(!std::is_move_constructible<ClientSession>::value && !std::is_move_assignable<ClientSession>::value, "a ClientSession is never moved");

namespace {

// A Hello with a version and a name (the other fields at their defaults: the message has grown with the protocol, an aggregate initializer would miss fields)
inline HelloMsg hello_of(uint16_t version, const char* name) {
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

inline Command cmd(CommandType type, uint8_t issuer, uint8_t other = 255, int16_t x = 0, int16_t y = 0, std::vector<uint32_t> ants = {}) {
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

inline Ids build_world(sim::SimulationEngine& sim, uint32_t seed, uint32_t match_ms = 720000) {
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
inline bool script(const Ids& ids, uint32_t seed, uint32_t t, uint8_t p, Command& out) {
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
inline bool any_decodes(const std::vector<uint8_t>& b) {
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
    SeatMoveMsg m28;
    PlanMsg m29;
    NameMsg m30;
    RemoveMsg m31;
    return decode(b, m1) || decode(b, m2) || decode(b, m3) || decode(b, m4) || decode(b, m5) || decode(b, m6) || decode(b, m7) || decode(b, m8) ||
           decode(b, m9) || decode(b, m10) || decode(b, m11) || decode(b, m12) || decode(b, m13) || decode(b, m14) || decode(b, m15) ||
           decode(b, m16) || decode(b, m17) || decode(b, m18) || decode(b, m19) || decode_ping(b.data(), b.size(), m20) || decode(b, m21) || decode(b, m22) ||
           decode(b, m23) || decode(b, m24) || decode(b, m25) || decode(b, m26) || decode(b, m27) ||      // (protocol 10: Presence, Vote, CatchUp, TurnBatch, CaughtUp)
           decode(b, m28) || decode(b, m29) || decode(b, m30) || decode(b, m31);      // (protocol 14: SeatMove; 16: Plan, Name, Remove)
}

// A key that is not zero, and different for every `salt`
inline SeatKey key_with(uint8_t salt) {
    SeatKey k{};
    for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(salt * 31u + i * 7u + 1u);
    return k;
}

// What a Hello of protocols 6 to 9 looked like (they shared the layout: protocol 9 changed the rules of the match, not a message): no key, no turns. Protocol 10 adds both.
inline std::vector<uint8_t> old_layout_hello(uint16_t version, const std::string& name, const std::string& room = std::string(), const std::string& token = std::string()) {
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
inline RoomMsg server_room_of(uint8_t leader) {
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



// ---------------------------------------------------------------------------------------------------------------------------------
// Coming back (protocol 10): the turn log, the attendance rules, the runner's fast-forward (docs/NETWORK_PORT.md "Reconnect")
// ---------------------------------------------------------------------------------------------------------------------------------

// A match as a server logs it: `count` turns of 50 ms with about 4 commands a second over the four players (what people give: orders of 1 - 24 ants, Stops, a Hatch, now and then an
// invitation), in canonical order with the issuers stamped. `per_hundred_turns` is the share of turns that carry a command.
inline std::vector<TurnMsg> real_looking_turns(uint32_t count, uint32_t seed, uint32_t per_hundred_turns = 20) {
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
inline std::vector<uint8_t> pack_turn(const TurnMsg& t) {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(t.commands.size() & 0xFF));
    out.push_back(static_cast<uint8_t>(t.commands.size() >> 8));
    for (const Command& c : t.commands) sim::encode(c, out);
    return out;
}

// A turn that holds `commands` commands of `ants` ants each (a hostile client's turn is 64 of 32 for each of four players)
inline TurnMsg heavy_turn(uint32_t number, size_t commands, size_t ants) {
    TurnMsg t;
    t.turn = number;
    std::vector<uint32_t> list;
    for (size_t i = 0; i < ants; ++i) list.push_back(static_cast<uint32_t>(i));
    for (size_t i = 0; i < commands; ++i) t.commands.push_back(cmd(CommandType::GroupMove, static_cast<uint8_t>(i % 4), 255, 1, 1, list));
    return t;
}

// True when the turns of the batch are the turns `from` .. of `all`, number and commands
inline bool batch_is(const std::vector<TurnMsg>& all, size_t from, const TurnBatchMsg& b) {
    if (b.first_turn != from || b.turns.empty() || from + b.turns.size() > all.size()) return false;
    for (size_t i = 0; i < b.turns.size(); ++i) {
        if (b.turns[i].turn != all[from + i].turn || b.turns[i].commands != all[from + i].commands) return false;
    }
    return true;
}


}  // namespace

// The groups of cases, run by main() in test_lockstep.cpp in this order
void run_protocol_tests();
void run_network_tests();
void run_sequencer_tests();
void run_flood_budget_tests();
void run_runner_tests();
void run_reconnect_core_tests();
void run_match_tests();
void run_failure_tests();
void run_dropout_tests();
void run_migration_tests();
void run_reconnect_session_tests();
void run_protocol11_tests();
void run_team_chat_tests();
void run_protocol12_tests();
void run_protocol13_tests();
void run_protocol15_tests();
void run_protocol16_tests();
void run_protocol17_tests();
void run_protocol21_tests();
void run_restart_tests();
void run_reconnect_session_tests_2();
