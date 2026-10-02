// Tests of what the player feels of the network (ants_net/latency.hpp): the round trip to the host from Ping / Pong timings, the delay of the player's own commands from
// the send to the tick that applies them, the means over the last answers / commands, and both measured in whole sessions over simulated links (a host and a guest with
// 40 ms in each direction: the delay must come out as the way there + the wait for the next 50 ms turn + the way back + the runner's jitter buffer, one turn of 50 ms on a
// steady link). And what the player sees when the game waits: "stalled for" is the time since a tick was due and had no turn, so the "Waiting for the other players..."
// message goes when the turns come again; and what a stall, a bunch of turns, a hitch or a hidden window leaves behind in the runner (nothing: the buffer is rebuilt, the
// queue is run down).
#include "ants_net/latency.hpp"
#include "ants_net/lobby.hpp"
#include "ants_net/loopback.hpp"
#include "ants_net/protocol.hpp"
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

namespace {

Command order(uint8_t seat, uint32_t ant, int16_t x, int16_t y) {
    Command c;
    c.type = CommandType::GroupMove;
    c.issuer = seat;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = {ant};
    return c;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// A host (seat 0) and one guest (seat 1) over a simulated link, stepped one millisecond at a time. Both simulations hold the same small world with one worker per player.
// ---------------------------------------------------------------------------------------------------------------------------------

struct Duel {
    LoopbackNetwork net{5};
    sim::SimulationEngine host_sim;
    sim::SimulationEngine guest_sim;
    uint32_t host_ant{0};
    uint32_t guest_ant{0};
    std::unique_ptr<HostSession> host;
    std::unique_ptr<ClientSession> guest;
    Connection* host_end{nullptr};
    Connection* guest_end{nullptr};
    uint32_t now{0};

    static void build(sim::SimulationEngine& sim, uint32_t& ant0, uint32_t& ant1) {
        sim.init_test_world(60, 60, 1, 720000);
        sim.grid_mut().set_anthill(0, TileCoord{4, 4});
        sim.grid_mut().set_anthill(1, TileCoord{50, 50});
        ant0 = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 10});
        ant1 = sim.spawn_unit(1, AntType::Worker, TileCoord{51, 56});
    }

    explicit Duel(LoopbackNetwork::Link link) {
        uint32_t unused_a = 0;
        uint32_t unused_b = 0;
        build(host_sim, host_ant, guest_ant);
        build(guest_sim, unused_a, unused_b);
        auto ends = net.connect(link);
        host_end = ends.first;
        guest_end = ends.second;
        HostSession::Config hc;
        hc.host_player = 0;
        host = std::make_unique<HostSession>(host_sim, hc);
        host->add_client(1, host_end);
        ClientSession::Config cc;
        cc.player = 1;
        guest = std::make_unique<ClientSession>(guest_sim, cc);
        guest->set_connection(guest_end);
        host->start(0);
        guest->start(0);
    }

    // Runs to the time `t`; `each_step` is called at the start of every step with the new time (before the sessions are updated: the way the application handles its input
    // before the frame's clock is taken)
    void run_to(uint32_t t, const std::function<void(uint32_t)>& each_step = {}) {
        while (now < t) {
            ++now;
            net.set_time(now);
            if (each_step) each_step(now);
            host->update(now);
            guest->update(now);
        }
    }
};

// A host (seat 0) and two guests (seats 1 and 2) over links of 30 ms each way with a little jitter. Guest 2 can be frozen (its frame loop does not run: a window that the
// browser stops drawing, a hitch): after a few seconds the host (which has a seat: a game on the local network) stops sealing turns (the frozen guest's acknowledgements
// are 60 turns behind), and guest 1 waits.
struct Trio {
    LoopbackNetwork net{9};
    sim::SimulationEngine host_sim;
    sim::SimulationEngine a_sim;
    sim::SimulationEngine b_sim;
    std::unique_ptr<HostSession> host;
    std::unique_ptr<ClientSession> a;
    std::unique_ptr<ClientSession> b;
    uint32_t now{0};

    static void build(sim::SimulationEngine& sim) {
        sim.init_test_world(60, 60, 1, 720000);
        sim.grid_mut().set_anthill(0, TileCoord{4, 4});
        sim.grid_mut().set_anthill(1, TileCoord{50, 50});
        sim.grid_mut().set_anthill(2, TileCoord{4, 50});
        sim.spawn_unit(0, AntType::Worker, TileCoord{5, 10});
        sim.spawn_unit(1, AntType::Worker, TileCoord{51, 56});
        sim.spawn_unit(2, AntType::Worker, TileCoord{5, 56});
    }

    Trio() {
        build(host_sim);
        build(a_sim);
        build(b_sim);
        HostSession::Config hc;
        hc.host_player = 0;
        host = std::make_unique<HostSession>(host_sim, hc);
        ClientSession::Config ca;
        ca.player = 1;
        ClientSession::Config cb;
        cb.player = 2;
        a = std::make_unique<ClientSession>(a_sim, ca);
        b = std::make_unique<ClientSession>(b_sim, cb);
        auto ea = net.connect({30, 5});
        auto eb = net.connect({30, 5});
        host->add_client(1, ea.first);
        host->add_client(2, eb.first);
        a->set_connection(ea.second);
        b->set_connection(eb.second);
        host->start(0);
        a->start(0);
        b->start(0);
    }

    // Runs to `t` one millisecond at a time; guest 2's frame loop does not run while `b_frozen(now)`; `each_step` sees every step after the sessions were updated
    void run_to(uint32_t t, const std::function<bool(uint32_t)>& b_frozen, const std::function<void(uint32_t)>& each_step = {}) {
        while (now < t) {
            ++now;
            net.set_time(now);
            host->update(now);
            a->update(now);
            if (!b_frozen(now)) b->update(now);
            if (each_step) each_step(now);
        }
    }
};


// ---------------------------------------------------------------------------------------------------------------------------------
// A runner fed with a synthetic stream of turns and a frame loop, to see what a stall, a bunch of turns, a hitch or a hidden window leave behind. The host seals a turn every
// 50 ms (a pause leaves a hole, and sealing goes on at the normal rate); each turn takes `latency` plus a pseudo-random 0 .. jitter (and `spike_ms` on one turn in
// `spike_every`) to arrive, in order; a frozen link holds everything until it thaws (what comes after comes in a bunch); the frames are 16.67 ms apart (a hitch blocks the
// loop, a hidden window draws one frame a second); a frame hands the runner its real time up to `max_dt` (the application: a second).
// ---------------------------------------------------------------------------------------------------------------------------------

struct Rig {
    struct Span {
        uint32_t from;
        uint32_t to;        // for a hitch: the length
    };
    uint32_t latency{40};
    uint32_t jitter{0};
    std::vector<Span> host_pauses;
    std::vector<Span> freezes;
    std::vector<Span> hitches;        // {at, length}
    std::vector<Span> hidden;         // frames only once a second
    uint32_t spike_every{0};
    uint32_t spike_ms{0};
    uint32_t duration{40000};
    uint32_t max_dt{1000};
    uint32_t seed{1};
    LockstepRunner::Config runner{};

    struct Outcome {
        std::vector<uint32_t> ticks;      // the time of every tick
        std::vector<uint32_t> seal;       // per turn
        std::vector<uint32_t> arrive;
        std::vector<uint32_t> exec;       // the time its first tick ran (0: never)
        std::vector<size_t> queued;       // the queue after every frame
        // the mean over the turns that ran from `from` on of the time from the host's seal to the first tick: the delay that the runner adds to the link's
        double lag_from(uint32_t from) const {
            double sum = 0;
            size_t n = 0;
            for (size_t k = 0; k < exec.size(); ++k) {
                if (exec[k] == 0 || exec[k] < from) continue;
                sum += static_cast<double>(exec[k]) - static_cast<double>(seal[k]);
                ++n;
            }
            return n == 0 ? 0.0 : sum / static_cast<double>(n);
        }
        // the share of the ticks from `from` on that ran less than 20 ms after the one before: the game steps in pairs
        double back_to_back_from(uint32_t from) const {
            size_t pairs = 0;
            size_t n = 0;
            for (size_t i = 1; i < ticks.size(); ++i) {
                if (ticks[i] < from) continue;
                ++n;
                if (ticks[i] - ticks[i - 1] < 20) ++pairs;
            }
            return n == 0 ? 0.0 : static_cast<double>(pairs) / static_cast<double>(n);
        }
        // the longest time between two ticks, from `from` on
        uint32_t longest_gap_from(uint32_t from) const {
            uint32_t gap = 0;
            for (size_t i = 1; i < ticks.size(); ++i) {
                if (ticks[i] >= from) gap = std::max(gap, ticks[i] - ticks[i - 1]);
            }
            return gap;
        }
        // how many times more than `over` ms passed between two ticks, from `from` on
        size_t gaps_over(uint32_t over, uint32_t from) const {
            size_t n = 0;
            for (size_t i = 1; i < ticks.size(); ++i) {
                if (ticks[i] >= from && ticks[i] - ticks[i - 1] > over) ++n;
            }
            return n;
        }
    };

    Outcome run() const {
        Outcome out;
        uint32_t rng = seed * 2654435761u + 12345u;
        auto draw = [&](uint32_t n) {
            rng = rng * 1664525u + 1013904223u;
            return n == 0 ? 0u : (rng >> 8) % n;
        };
        auto in = [](const std::vector<Span>& spans, uint32_t t, Span* hit = nullptr) {
            for (const Span& sp : spans) {
                if (t >= sp.from && t < sp.to) {
                    if (hit != nullptr) *hit = sp;
                    return true;
                }
            }
            return false;
        };
        // the turns that reach the guest
        uint32_t next_seal = 0;
        uint32_t last_arrival = 0;
        while (next_seal < duration + 5000) {
            Span pause{};
            if (in(host_pauses, next_seal, &pause)) {
                next_seal = pause.to;                                     // sealing goes on at the normal rate when the pause is over
                continue;
            }
            uint32_t delay = latency + draw(jitter + 1);
            if (spike_every != 0 && draw(spike_every) == 0) delay += spike_ms;
            uint32_t at = std::max(next_seal + delay, last_arrival);       // reliable and ordered
            Span freeze{};
            if (in(freezes, at, &freeze)) at = freeze.to;
            last_arrival = at;
            out.seal.push_back(next_seal);
            out.arrive.push_back(at);
            next_seal += kTurnMs;
        }
        out.exec.assign(out.seal.size(), 0);
        sim::SimulationEngine sim;
        Trio::build(sim);
        LockstepRunner runner_(sim, runner);
        uint32_t now = 0;
        size_t ticks_run = 0;
        runner_.set_on_tick([&]() {
            out.ticks.push_back(now);
            if (ticks_run < out.exec.size()) out.exec[ticks_run] = now;        // a turn is a tick
            ++ticks_run;
        });
        size_t next_turn = 0;
        uint32_t last_frame = 0;
        double next_frame = 0.0;
        std::vector<Span> pending_hitches = hitches;
        for (now = 0; now < duration; ++now) {
            if (static_cast<double>(now) < next_frame) continue;
            bool blocked = false;
            for (auto it = pending_hitches.begin(); it != pending_hitches.end(); ++it) {
                if (now >= it->from) {
                    next_frame = static_cast<double>(it->from + it->to);          // the loop is busy until the hitch is over
                    pending_hitches.erase(it);
                    blocked = true;
                    break;
                }
            }
            if (blocked) continue;
            const uint32_t dt = std::min(now - last_frame, max_dt);
            last_frame = now;
            while (next_turn < out.arrive.size() && out.arrive[next_turn] <= now) {
                TurnMsg m;
                m.turn = static_cast<uint32_t>(next_turn);
                runner_.on_turn(m);
                ++next_turn;
            }
            runner_.update(dt);
            out.queued.push_back(runner_.queued());
            next_frame = in(hidden, now) ? static_cast<double>(now) + 1000.0 : next_frame + 16.6667;
            if (next_frame < static_cast<double>(now) + 1.0) next_frame = static_cast<double>(now) + 16.6667;
        }
        return out;
    }
};

// What the guest's delay must be for a command sent at `t` over a link of `one_way` ms each way: the command reaches the host at t + one_way and goes into the first turn
// that is sealed at or after that moment (the host seals at 0, 50, 100, ...); that turn is back at the guest one_way later. The guest's runner starts when the second turn
// has arrived (seal 50 + one_way) and executes one turn every 50 ms from then on: turn k at one_way + 50 + 50 k, i.e. 50 ms after the turn is there (one turn of buffer).
uint32_t expected_guest_delay(uint32_t t, uint32_t one_way) {
    const uint32_t arrives = t + one_way;
    const uint32_t sealed = (arrives + kTurnMs - 1u) / kTurnMs * kTurnMs;
    const uint32_t executed = one_way + kTurnMs + sealed;
    return executed - t;
}

// The host's own command: sealed at the first seal at or after t, and the host's runner (which also waits for two turns) executes turn k at 50 + 50 k
uint32_t expected_host_delay(uint32_t t) {
    const uint32_t sealed = (t + kTurnMs - 1u) / kTurnMs * kTurnMs;
    return sealed + kTurnMs - t;
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: ping and command delay (what the player feels)\n"
                 "=======================================================\n";

    TEST_CASE("N9.1 MeanWindow: Nothing Until The First Value, The Mean Rounds Half Up, The Oldest Value Leaves When The Window Is Full") {
        MeanWindow<3> w;
        ASSERT_TRUE(w.empty() && w.size() == 0 && w.mean() == 0 && w.last() == 0);
        w.add(10);
        ASSERT_TRUE(!w.empty() && w.size() == 1 && w.mean() == 10 && w.last() == 10);
        w.add(11);
        ASSERT_EQ(w.mean(), 11u);                                        // 10.5 rounds up
        ASSERT_EQ(w.last(), 11u);
        w.add(15);
        ASSERT_EQ(w.size(), 3u);
        ASSERT_EQ(w.mean(), 12u);                                        // (10 + 11 + 15) / 3 = 12
        w.add(30);                                                       // 10 leaves: (11 + 15 + 30) / 3 = 18.67
        ASSERT_EQ(w.size(), 3u);
        ASSERT_EQ(w.mean(), 19u);
        ASSERT_EQ(w.last(), 30u);
        w.add(0);                                                        // 11 leaves: (15 + 30 + 0) / 3 = 15
        w.add(0);                                                        // 15 leaves: (30 + 0 + 0) / 3 = 10
        ASSERT_EQ(w.mean(), 10u);
        MeanWindow<1> one;
        one.add(7);
        one.add(9);
        ASSERT_TRUE(one.mean() == 9 && one.size() == 1);
        MeanWindow<4> big;                                               // no overflow with large values (the sum is 64 bits)
        for (int i = 0; i < 4; ++i) big.add(0xFFFFFFFFu);
        ASSERT_EQ(big.mean(), 0xFFFFFFFFu);
    } TEST_END();

    TEST_CASE("N9.2 PingMeter: The Round Trip Is The Time Between The Ping And Its Pong, The Number Is The Mean Of The Last Five Answers") {
        PingMeter m;
        ASSERT_TRUE(!m.measured() && m.ping_ms() == 0 && m.last_ms() == 0 && m.answers() == 0);
        const PingMsg first = m.next(1000);
        ASSERT_TRUE(first.nonce == 1 && first.sent_ms == 1000);
        ASSERT_TRUE(m.on_pong(first, 1060));
        ASSERT_TRUE(m.measured() && m.ping_ms() == 60 && m.last_ms() == 60);
        // five more answers: 70, 80, 90, 100, 110; the last five are 70 .. 110 (mean 90), the 60 has left
        uint32_t t = 2000;
        for (uint32_t rtt : {70u, 80u, 90u, 100u, 110u}) {
            const PingMsg p = m.next(t);
            ASSERT_TRUE(m.on_pong(p, t + rtt));
            t += 1000;
        }
        ASSERT_EQ(m.answers(), PingMeter::kAnswersKept);
        ASSERT_EQ(m.ping_ms(), 90u);
        ASSERT_EQ(m.last_ms(), 110u);
        // the nonce counts up and is never 0
        ASSERT_EQ(m.next(t).nonce, 7u);
    } TEST_END();

    TEST_CASE("N9.3 PingMeter: An Answer That Is Not Believed Counts For Nothing (nonce 0, never sent, wrong time, twice, from the future, older than eight pings)") {
        PingMeter m;
        const PingMsg p1 = m.next(500);
        ASSERT_FALSE(m.on_pong(PingMsg{0, 500}, 560));                   // the guests' pings to each other carry the nonce 0
        ASSERT_FALSE(m.on_pong(PingMsg{2, 500}, 560));                   // a nonce that was never sent
        ASSERT_FALSE(m.on_pong(PingMsg{1, 501}, 560));                   // not the send time that was recorded: a made-up echo
        ASSERT_FALSE(m.on_pong(p1, 499));                                // before it left (the clock is the caller's, never run backwards)
        ASSERT_FALSE(m.measured());
        ASSERT_TRUE(m.on_pong(p1, 560));
        ASSERT_FALSE(m.on_pong(p1, 570));                                // the same echo twice counts once
        ASSERT_EQ(m.answers(), 1u);
        ASSERT_EQ(m.ping_ms(), 60u);
        // the ninth ping after one forgets it
        PingMeter n;
        const PingMsg old = n.next(0);
        for (uint32_t i = 1; i <= PingMeter::kInFlight; ++i) n.next(i * 1000);
        ASSERT_FALSE(n.on_pong(old, 5000));
        ASSERT_FALSE(n.measured());
        // the clock wraps (a server's clock is its uptime): the difference is still right
        PingMeter w;
        const PingMsg across = w.next(0xFFFFFFF0u);
        ASSERT_TRUE(w.on_pong(across, 0x00000030u));
        ASSERT_EQ(w.ping_ms(), 0x40u);
    } TEST_END();

    TEST_CASE("N9.4 PingMeter: A Slow Link Has Several Pings Out At Once (a round trip of 2.5 s with a ping every second): every answer counts, in the order it comes") {
        PingMeter m;
        const PingMsg a = m.next(0);
        const PingMsg b = m.next(1000);
        const PingMsg c = m.next(2000);
        ASSERT_TRUE(m.on_pong(a, 2500));
        ASSERT_EQ(m.ping_ms(), 2500u);
        ASSERT_TRUE(m.on_pong(b, 3500));
        ASSERT_TRUE(m.on_pong(c, 4500));
        ASSERT_EQ(m.answers(), 3u);
        ASSERT_EQ(m.ping_ms(), 2500u);
    } TEST_END();

    TEST_CASE("N9.5 CommandDelayMeter: The Delay Is The Time From The Send To The Tick That Applies The Command; Nothing Is Shown Before The First One") {
        CommandDelayMeter m(1);
        ASSERT_TRUE(!m.measured() && m.delay_ms() == 0 && m.samples() == 0 && m.pending() == 0);
        m.on_sent(order(1, 5, 10, 10));
        m.on_frame(1000);                                                // its send time is the clock of the next frame
        ASSERT_EQ(m.pending(), 1u);
        ASSERT_FALSE(m.measured());
        ASSERT_TRUE(m.on_applied(order(1, 5, 10, 10), 1230));
        ASSERT_TRUE(m.measured());
        ASSERT_EQ(m.delay_ms(), 230u);
        ASSERT_EQ(m.last_ms(), 230u);
        ASSERT_EQ(m.pending(), 0u);
        ASSERT_FALSE(m.on_applied(order(1, 5, 10, 10), 1300));            // applied twice: the second has no sent command behind it
        ASSERT_EQ(m.samples(), 1u);
    } TEST_END();

    TEST_CASE("N9.6 CommandDelayMeter: A Command Is Matched By Its Content And Its Issuer; Other Players' Commands And Other Content Are Ignored") {
        CommandDelayMeter m(2);
        Command stop;
        stop.type = CommandType::Stop;
        stop.ants = {7, 8};
        m.on_sent(stop);                                                 // (the issuer of what is sent is the own seat whatever the caller had in it)
        m.on_frame(100);
        Command other = stop;
        other.issuer = 3;                                                // another player's identical command
        ASSERT_FALSE(m.on_applied(other, 400));
        Command own = stop;
        own.issuer = 2;
        own.ants = {7, 9};                                               // other ants
        ASSERT_FALSE(m.on_applied(own, 400));
        own.ants = {7, 8};
        own.tile_x = 1;                                                  // another tile
        ASSERT_FALSE(m.on_applied(own, 400));
        own.tile_x = 0;
        ASSERT_EQ(m.pending(), 1u);
        ASSERT_TRUE(m.on_applied(own, 400));
        ASSERT_EQ(m.delay_ms(), 300u);
        CommandDelayMeter nobody(255);                                   // a host without a seat plays no commands: nothing is measured
        nobody.on_sent(stop);
        nobody.on_frame(0);
        ASSERT_EQ(nobody.pending(), 0u);
        stop.issuer = 255;
        ASSERT_FALSE(nobody.on_applied(stop, 100));
    } TEST_END();

    TEST_CASE("N9.7 CommandDelayMeter: A Command Sent Between Two Frames Went Out At The Next Frame; Commands Of One Frame Share Its Time") {
        CommandDelayMeter m(0);
        m.on_frame(1000);                                                // the frame
        m.on_sent(order(0, 1, 1, 1));                                    // input is handled after this clock was taken ...
        m.on_sent(order(0, 1, 2, 2));
        m.on_frame(1016);                                                // ... so they went out in the next frame
        m.on_frame(1033);                                                // (a later frame does not move the stamp)
        ASSERT_TRUE(m.on_applied(order(0, 1, 1, 1), 1216));
        ASSERT_TRUE(m.on_applied(order(0, 1, 2, 2), 1316));
        ASSERT_EQ(m.last_ms(), 300u);
        ASSERT_EQ(m.samples(), 2u);
        ASSERT_EQ(m.delay_ms(), 250u);                                   // (200 + 300) / 2
    } TEST_END();

    TEST_CASE("N9.8 CommandDelayMeter: Identical Commands Are Matched In The Order They Were Sent") {
        CommandDelayMeter m(1);
        m.on_sent(order(1, 4, 9, 9));
        m.on_frame(100);
        m.on_sent(order(1, 4, 9, 9));
        m.on_frame(180);
        ASSERT_TRUE(m.on_applied(order(1, 4, 9, 9), 350));                // the first: 250
        ASSERT_EQ(m.last_ms(), 250u);
        ASSERT_TRUE(m.on_applied(order(1, 4, 9, 9), 430));                // the second: 250
        ASSERT_EQ(m.last_ms(), 250u);
        ASSERT_EQ(m.pending(), 0u);
    } TEST_END();

    TEST_CASE("N9.9 CommandDelayMeter: A Command That The Host Refused Or A Host Change Lost Does Not Spoil The Next Ones (it leaves when a later command is applied, or after 20 s)") {
        CommandDelayMeter m(1);
        m.on_sent(order(1, 1, 1, 1));                                    // lost
        m.on_frame(100);
        m.on_sent(order(1, 2, 2, 2));
        m.on_frame(150);
        m.on_sent(order(1, 3, 3, 3));
        m.on_frame(200);
        ASSERT_TRUE(m.on_applied(order(1, 3, 3, 3), 480));                // the host keeps the order of one player's commands: 1 and 2 were lost
        ASSERT_EQ(m.last_ms(), 280u);
        ASSERT_EQ(m.pending(), 0u);
        ASSERT_FALSE(m.on_applied(order(1, 1, 1, 1), 500));               // (and a command that was dropped cannot be applied later)
        // after 20 s a command that never came is forgotten
        m.on_sent(order(1, 9, 9, 9));
        m.on_frame(1000);
        m.on_frame(1000 + CommandDelayMeter::kForgetAfterMs);
        ASSERT_EQ(m.pending(), 1u);
        m.on_frame(1000 + CommandDelayMeter::kForgetAfterMs + 1);
        ASSERT_EQ(m.pending(), 0u);
        // a flood of commands that are never answered keeps the list bounded
        for (uint32_t i = 0; i < 3 * CommandDelayMeter::kMaxPending; ++i) m.on_sent(order(1, 1, static_cast<int16_t>(i), 0));
        ASSERT_EQ(m.pending(), CommandDelayMeter::kMaxPending);
    } TEST_END();

    TEST_CASE("N9.10 CommandDelayMeter: The Number Is The Mean Over The Last Ten Commands") {
        CommandDelayMeter m(0);
        uint32_t t = 0;
        for (uint32_t i = 1; i <= 12; ++i) {                              // delays 100, 200, ... 1200
            m.on_sent(order(0, 1, static_cast<int16_t>(i), 0));
            m.on_frame(t);
            ASSERT_TRUE(m.on_applied(order(0, 1, static_cast<int16_t>(i), 0), t + i * 100));
            t += 5000;
        }
        ASSERT_EQ(m.samples(), CommandDelayMeter::kCommandsKept);
        ASSERT_EQ(m.delay_ms(), 750u);                                   // (300 + ... + 1200) / 10
        ASSERT_EQ(m.last_ms(), 1200u);
    } TEST_END();

    TEST_CASE("N9.11 Ping Over A Link Of 40 ms Each Way In A Match: 80 ms, From The First Answer On") {
        Duel d({40, 0});
        ASSERT_FALSE(d.guest->ping().measured());
        d.run_to(30);
        ASSERT_FALSE(d.guest->ping().measured());                         // the first answer cannot be here before 80 ms
        d.run_to(120);
        ASSERT_TRUE(d.guest->ping().measured());                          // the guest pings at once when the match begins
        ASSERT_EQ(d.guest->ping().last_ms(), 80u);
        d.run_to(6000);
        ASSERT_TRUE(d.guest->ping().answers() == PingMeter::kAnswersKept);
        ASSERT_EQ(d.guest->ping().ping_ms(), 80u);
        ASSERT_EQ(d.guest->rtt_ms(), 80u);
        ASSERT_FALSE(d.host->command_delay().measured());                 // (nobody sent a command)
        ASSERT_FALSE(d.guest->command_delay().measured());
    } TEST_END();

    TEST_CASE("N9.12 Command Delay Over A Link Of 40 ms Each Way: 40 + Seal Wait + 40 + One Turn Of Jitter Buffer, For Every Phase Of The 50 ms Turns") {
        Duel d({40, 0});
        d.run_to(1000);                                                   // the runner is in its steady state
        ASSERT_TRUE(d.guest->runner().queued() <= 2);
        // one command at every moment of the 50 ms turns (the phase is t mod 50; a command sent at phase 10 reaches the host exactly when it seals), 600 ms apart so that
        // each one is applied before the next goes out; twice round
        uint32_t lowest = 1000000;
        uint32_t highest = 0;
        for (uint32_t phase = 0; phase < 100; ++phase) {
            const uint32_t t = 1100 + 600u * (phase + 1) + phase;
            d.run_to(t - 1);
            const size_t before = d.guest->command_delay().samples();
            d.run_to(t, [&](uint32_t now) {
                if (now == t) d.guest->submit(order(1, d.guest_ant, 20, 20));
            });
            const uint32_t end = t + 500;
            while (d.now < end && d.guest->command_delay().pending() > 0) d.run_to(d.now + 1);
            ASSERT_EQ(d.guest->command_delay().pending(), 0u);
            ASSERT_TRUE(d.guest->command_delay().samples() > before || d.guest->command_delay().samples() == CommandDelayMeter::kCommandsKept);
            const uint32_t measured = d.guest->command_delay().last_ms();
            const uint32_t expected = expected_guest_delay(t, 40);
            ASSERT_EQ(measured, expected);                                // the clock is virtual and steps 1 ms: the model is exact for every phase
            lowest = std::min(lowest, measured);
            highest = std::max(highest, measured);
        }
        ASSERT_TRUE(lowest >= 128 && lowest <= 135);                      // 40 + 40 + 50 and the smallest seal wait
        ASSERT_TRUE(highest >= 172 && highest <= 180);                    // ... and the largest (almost a whole turn of waiting): 40 + 49 + 40 + 50
        ASSERT_TRUE(d.guest->command_delay().measured());
    } TEST_END();

    TEST_CASE("N9.13 Command Delay: The Mean Over Ten Commands Sent Every 130 ms Is The Mean Of The Model, About 155 ms On A 40 + 40 ms Link") {
        Duel d({40, 0});
        d.run_to(1000);
        uint32_t sent = 0;
        uint32_t sum_expected = 0;
        uint32_t next = 1003;
        d.run_to(1003 + 130 * 10 + 1500, [&](uint32_t now) {
            if (now == next && sent < 10) {
                d.guest->submit(order(1, d.guest_ant, static_cast<int16_t>(20 + sent), 20));
                sum_expected += expected_guest_delay(now, 40);
                ++sent;
                next += 130;
            }
        });
        ASSERT_EQ(sent, 10u);
        ASSERT_EQ(d.guest->command_delay().samples(), 10u);
        const uint32_t expected_mean = (sum_expected + 5u) / 10u;
        ASSERT_EQ(d.guest->command_delay().delay_ms(), expected_mean);
        ASSERT_TRUE(d.guest->command_delay().delay_ms() >= 130 && d.guest->command_delay().delay_ms() <= 180);
        ASSERT_EQ(d.guest->command_delay().pending(), 0u);
    } TEST_END();

    TEST_CASE("N9.14 The Host's Own Commands: Delay Is The Wait For The Next Seal Plus One Turn Of Buffer (50 - 100 ms), Ping Is None; Commands Of The Other Seat Are Not Counted") {
        Duel d({40, 0});
        d.run_to(1000);
        const uint32_t phases[] = {3, 0, 1, 49, 25};
        for (size_t i = 0; i < sizeof(phases) / sizeof(phases[0]); ++i) {
            const uint32_t t = 1100 + 500u * static_cast<uint32_t>(i) + phases[i];
            d.run_to(t - 1);
            const size_t before = d.host->command_delay().samples();
            d.run_to(t, [&](uint32_t now) {
                if (now == t) d.host->submit_local(order(0, d.host_ant, 21, 21));
            });
            const uint32_t end = t + 1000;
            while (d.now < end && d.host->command_delay().pending() > 0) d.run_to(d.now + 1);
            ASSERT_EQ(d.host->command_delay().pending(), 0u);
            ASSERT_TRUE(d.host->command_delay().samples() > before || d.host->command_delay().samples() == CommandDelayMeter::kCommandsKept);
            ASSERT_EQ(d.host->command_delay().last_ms(), expected_host_delay(t));
        }
        ASSERT_FALSE(d.guest->command_delay().measured());                // the host's commands are not the guest's
        d.guest->submit(order(1, d.guest_ant, 30, 30));
        d.run_to(d.now + 800);
        ASSERT_TRUE(d.guest->command_delay().measured());
        ASSERT_EQ(d.host->command_delay().samples(), 5u);                 // and the guest's are not the host's
    } TEST_END();

    TEST_CASE("N9.15 A Host Without A Seat (a dedicated server) Measures Nothing; A Guest Of It Measures Its Own Commands") {
        sim::SimulationEngine referee;
        sim::SimulationEngine guest_sim;
        uint32_t a = 0;
        uint32_t b = 0;
        Duel::build(referee, a, b);
        Duel::build(guest_sim, a, b);
        LoopbackNetwork net(3);
        auto ends = net.connect({20, 0});
        HostSession::Config hc;
        hc.host_player = kNoSeat;
        HostSession host(referee, hc);
        host.add_client(1, ends.first);
        ClientSession::Config cc;
        cc.player = 1;
        cc.host = kNoSeat;
        cc.migration = false;
        ClientSession guest(guest_sim, cc);
        guest.set_connection(ends.second);
        host.start(0);
        guest.start(0);
        uint32_t now = 0;
        for (; now < 1000;) {
            ++now;
            net.set_time(now);
            host.update(now);
            guest.update(now);
        }
        host.submit_local(order(0, 1, 5, 5));                             // ignored: it plays nobody
        ASSERT_TRUE(guest.submit(order(1, b, 25, 25)));
        for (const uint32_t end = now + 800; now < end;) {
            ++now;
            net.set_time(now);
            host.update(now);
            guest.update(now);
        }
        ASSERT_FALSE(host.command_delay().measured());
        ASSERT_TRUE(guest.command_delay().measured());
        ASSERT_EQ(guest.ping().ping_ms(), 40u);
    } TEST_END();

    TEST_CASE("N9.16 A Command That The Host Refuses (a system command from a guest) Is Never Applied And Does Not Spoil The Delay Of The Next One") {
        Duel d({40, 0});
        d.run_to(1000);
        Command drop;
        drop.type = CommandType::Drop;                                    // only the sequencer may make one: the host refuses it (and counts a violation)
        d.run_to(1100, [&](uint32_t now) {
            if (now == 1100) d.guest->submit(drop);
        });
        d.run_to(1103, [&](uint32_t now) {
            if (now == 1103) d.guest->submit(order(1, d.guest_ant, 22, 22));
        });
        d.run_to(1900);
        ASSERT_EQ(d.guest->command_delay().samples(), 1u);                // the refused command is not a measurement
        ASSERT_EQ(d.guest->command_delay().pending(), 0u);                // and it does not wait for ever behind the good one
        ASSERT_EQ(d.guest->command_delay().last_ms(), expected_guest_delay(1103, 40));
    } TEST_END();

    TEST_CASE("N9.17 A Link With Jitter (40 + 0 .. 30 ms each way): Ping And Delay Stay Inside What The Link And The Turns Allow, And The Runner Never Stalls") {
        Duel d({40, 30});
        uint32_t sent = 0;
        uint32_t next = 1000;
        bool stalled = false;
        d.run_to(1000 + 200 * 25 + 1500, [&](uint32_t now) {
            if (now >= 1000 && now == next && sent < 25) {
                d.guest->submit(order(1, d.guest_ant, static_cast<int16_t>(10 + sent), 12));
                ++sent;
                next += 200;
            }
            stalled = stalled || d.guest->runner().stalled();
        });
        ASSERT_EQ(sent, 25u);
        ASSERT_EQ(d.guest->command_delay().samples(), CommandDelayMeter::kCommandsKept);
        ASSERT_TRUE(d.guest->ping().measured());
        ASSERT_TRUE(d.guest->ping().ping_ms() >= 80 && d.guest->ping().ping_ms() <= 140);        // 2 x 40 plus two draws of 0 .. 30
        // the way there and back (80 - 140), the wait for the seal (0 - 49), one turn of buffer that the jitter of the first turns moves by up to 30 either way (20 - 80)
        ASSERT_TRUE(d.guest->command_delay().delay_ms() >= 100 && d.guest->command_delay().delay_ms() <= 280);
        ASSERT_FALSE(stalled);                                            // 30 ms of jitter is inside the buffer of one turn (50 ms)
    } TEST_END();

    TEST_CASE("N9.18 The Room: A Guest Pings The Host Once A Second From The Moment It Has A Seat, And Measures The Round Trip (25 ms each way: 50 ms)") {
        LoopbackNetwork net(11);
        HostLobby host;
        host.set_map("TINY.LVL");
        auto ends = net.connect({25, 0});
        ClientLobby::Config cc;
        cc.name = "Bob";
        ClientLobby guest(ends.second, cc);
        uint32_t now = 0;
        host.add_connection(ends.first, now);
        auto run_to = [&](uint32_t t) {
            while (now < t) {
                ++now;
                net.set_time(now);
                host.update(now);
                guest.update(now);
            }
        };
        run_to(20);
        ASSERT_FALSE(guest.ping().measured());                            // not seated yet: nobody answers a connection that has not said Hello
        run_to(400);
        ASSERT_TRUE(guest.phase() == ClientLobby::Phase::InRoom);
        ASSERT_TRUE(guest.ping().measured());
        ASSERT_EQ(guest.ping().last_ms(), 50u);
        run_to(4000);
        ASSERT_EQ(guest.ping().ping_ms(), 50u);
        ASSERT_TRUE(guest.ping().answers() >= 3);
        ASSERT_TRUE(host.measured(1));                                    // the host measures the guest for the thumbs the same way
        ASSERT_EQ(host.rtt_ms(1), 50u);
    } TEST_END();

    TEST_CASE("N9.19 Waiting: The Count Of The Wait Is The Time Since The Tick That Was Due, So The Message Goes When The Turns Come Again (a frozen window holds the turns of a host WITH a seat up; it did not go)") {
        // guest 2 is frozen from 2 s to 7 s: after 3 s of its acknowledgements missing the host (it has a seat: a game on the local network) stops sealing (60 turns), and
        // guest 1 gets no turns until guest 2 has caught up
        Trio t;
        const uint32_t freeze_from = 2000;
        const uint32_t freeze_to = 7000;
        auto b_frozen = [&](uint32_t now) { return now >= freeze_from && now < freeze_to; };
        uint32_t longest_wait = 0;                                        // the longest the guest has waited for a turn
        uint32_t waited_at_turn = 0;                                      // the turn that guest 1 had executed when its wait was longest
        uint32_t resumed_at = 0;                                          // the first time after that wait at which it executed another turn
        uint32_t worst_after_300 = 0;                                     // the longest wait from 300 ms after that on
        uint32_t seen_before = 0;
        t.run_to(20000, b_frozen, [&](uint32_t now) {
            const uint32_t waited = t.a->runner().stalled_ms();
            if (waited > longest_wait && resumed_at == 0) {
                longest_wait = waited;
                waited_at_turn = t.a->runner().next_turn_to_execute();
            }
            if (resumed_at == 0 && longest_wait >= 1000 && t.a->runner().next_turn_to_execute() > waited_at_turn) resumed_at = now;
            if (resumed_at != 0 && now >= resumed_at + 300) worst_after_300 = std::max(worst_after_300, waited);
            if (now < freeze_from) seen_before = std::max(seen_before, waited);
        });
        ASSERT_TRUE(seen_before < 100);                                   // before the freeze nothing waits (turns are due every 50 ms)
        ASSERT_TRUE(longest_wait >= 1000);                                // the wait was real: the message is right to show while it lasts ...
        ASSERT_TRUE(resumed_at != 0);                                     // ... and it ended: the guest ran turns again
        ASSERT_TRUE(resumed_at > freeze_from + 3000 && resumed_at < freeze_to + 3000);
        ASSERT_TRUE(worst_after_300 < 300);                               // ... and 300 ms after the turns flow again the count is low, and stays low (it grew for ever)
        ASSERT_TRUE(t.a->runner().stalled_ms() < 300);
        ASSERT_TRUE(t.b->runner().stalled_ms() < 300);                    // the frozen guest too: it has caught up and runs turn by turn
        // and the two windows are at the same moment of the match again: the one that waited rebuilt its buffer, the one that was frozen ran its backlog down (it once kept five
        // turns standing in its queue for the rest of the match: half a second behind the other, which the clock of the match shows as 6:41 against 6:40)
        const uint32_t turn_a = t.a->runner().next_turn_to_execute();
        const uint32_t turn_b = t.b->runner().next_turn_to_execute();
        ASSERT_TRUE((turn_a > turn_b ? turn_a - turn_b : turn_b - turn_a) <= 5);
        ASSERT_TRUE(t.b->runner().queued() <= 5);                         // (the standing queue of a buffer of one turn is two; the frozen window may hold a larger one for a stall's sake)
    } TEST_END();

    TEST_CASE("N9.20 Waiting: A Hitch Of One Second In One Window Does Not Make The Other One Wait (the host seals for three seconds before it waits), And Nobody Waits Afterwards") {
        Trio t;
        auto b_frozen = [&](uint32_t now) { return now >= 2000 && now < 3000; };
        uint32_t worst_a = 0;
        t.run_to(12000, b_frozen, [&](uint32_t) { worst_a = std::max(worst_a, t.a->runner().stalled_ms()); });
        ASSERT_TRUE(worst_a < 100);                                       // guest 1 never waited: the host kept sealing
        ASSERT_TRUE(t.b->runner().stalled_ms() < 300);
    } TEST_END();

    TEST_CASE("N9.21 Waiting: The Runner Counts From The Tick That Was Due, Not From The Accumulator Of Unspent Time (a one second gap, then turns every 50 ms with jitter)") {
        sim::SimulationEngine sim;
        Trio::build(sim);
        LockstepRunner runner(sim);
        uint32_t next_turn = 0;
        auto turn = [&]() {
            TurnMsg m;
            m.turn = next_turn++;
            return m;
        };
        ASSERT_EQ(runner.stalled_ms(), 0u);                               // not started: nothing to wait for
        ASSERT_TRUE(runner.on_turn(turn()));
        ASSERT_TRUE(runner.on_turn(turn()));
        runner.update(0);                                                 // starts, runs the first turn
        ASSERT_EQ(runner.stalled_ms(), 0u);
        runner.update(50);                                                // turn 1
        ASSERT_FALSE(runner.stalled());                                   // just ran: nothing is due that is missing
        uint32_t t = 0;                                                   // 16 ms frames from here
        for (; t < 1000; t += 16) runner.update(16);                       // no turn for a second: the runner waits
        ASSERT_TRUE(runner.stalled());
        ASSERT_TRUE(runner.stalled_ms() >= 900 && runner.stalled_ms() <= 1010);   // (from the tick that was due, 50 ms after the last one; the count went on while the buffer was collected again)
        // turns arrive again, one every 50 ms with a little jitter. The runner waits for the third one (the stall grew its buffer to two turns and it collects them again), then
        // runs: from then on a turn is queued whenever the next one is due, so nothing waits any more
        uint32_t jitter = 0;
        uint32_t due = t;
        uint32_t worst = 0;
        uint32_t frames_running = 0;
        for (uint32_t i = 0; i < 400; ++i) {
            due += kTurnMs;
            jitter = (jitter * 7 + 11) % 21;                              // 0 .. 20 ms
            for (; t < due + jitter; t += 16) {
                runner.update(16);
                if (i >= 8) {
                    worst = std::max(worst, runner.stalled_ms());
                    frames_running += runner.stalled() ? 0u : 1u;
                }
            }
            runner.on_turn(turn());
        }
        ASSERT_EQ(worst, 0u);                                             // the buffer absorbs the jitter: there is always a turn when the next one is due
        ASSERT_TRUE(frames_running > 1000);
        ASSERT_FALSE(runner.stalled_ms() >= 1000);
        ASSERT_TRUE(runner.queued() >= 1 && runner.queued() <= 4);        // the buffer: one turn on a steady link, two after the stall, and the turn that runs
    } TEST_END();

    TEST_CASE("N9.22 Runner: After A Stall That Used Up The Buffer It Is Rebuilt (the host stops for 2 s and goes on at the normal rate): One Turn Again, Ticks 50 ms Apart, Never In Pairs, And Nothing Stays Behind (a stall that long grows no buffer and the lateness it caused is forgotten)") {
        Rig rig;
        rig.host_pauses = {{10000, 12000}};
        rig.jitter = 5;
        rig.duration = 60000;
        const Rig::Outcome out = rig.run();
        // from 14 s on: the runner collected its turns again (before the rebuild it ran each turn the moment it came, with no buffer at all, and the ticks of a turn together:
        // ticks in pairs, every bit of jitter a stall). The stall of 2 s grew no buffer (no buffer bridges it) and the turns that were read before it were forgotten (they
        // describe a link that is gone: the host's clock slid by two seconds), so the delay is the link's and one turn, at once and for good
        ASSERT_TRUE(out.lag_from(14000) >= 40 + 50 - 20 && out.lag_from(14000) <= 40 + 50 + 40);
        ASSERT_EQ(out.back_to_back_from(14000), 0.0);                    // no two ticks together
        ASSERT_TRUE(out.longest_gap_from(14000) <= 67);
        ASSERT_EQ(out.gaps_over(70, 14000), 0u);
        ASSERT_TRUE(out.lag_from(35000) >= 40 + 50 - 20 && out.lag_from(35000) <= 40 + 50 + 40);
    } TEST_END();

    TEST_CASE("N9.23 Runner: With Jitter After That Stall The Game Never Stands Still For A Hundred Milliseconds Again (it used to, again and again, for want of a buffer)") {
        Rig rig;
        rig.host_pauses = {{10000, 12000}};
        rig.jitter = 60;
        const Rig::Outcome out = rig.run();
        ASSERT_EQ(out.gaps_over(100, 14000), 0u);
        ASSERT_TRUE(out.lag_from(14000) <= 40 + 60 + 150 + 40);          // the link, its jitter and at most three turns of buffer
    } TEST_END();

    TEST_CASE("N9.24 Runner: The Turns Of A Link That Froze For A Second Come In A Bunch; The Queue That Is Left Standing Is Run Down At Up To 4x (it kept five turns for the rest of the match), And The Buffer Shrinks Back Slowly") {
        Rig rig;
        rig.freezes = {{10000, 11000}};
        rig.jitter = 5;
        rig.duration = 60000;
        const Rig::Outcome out = rig.run();
        // the bunch of twenty turns is run down: within two seconds of the thaw what stands in the queue is the buffer (the stall and the late bunch asked for the largest: four
        // turns and the one that runs), not the bunch
        ASSERT_TRUE(out.lag_from(13000) <= 40 + 200 + 50 + 40 && out.lag_from(13000) >= 40 + 50 - 20);
        ASSERT_TRUE(out.queued[out.queued.size() * 14 / 60] <= 6);       // (the queue at 14 s)
        // and the extra buffer goes, a turn every ten seconds: from 45 s on the link's own delay and one turn
        ASSERT_TRUE(out.lag_from(45000) <= 40 + 50 + 40);
        ASSERT_TRUE(out.queued.back() <= 2);
        ASSERT_EQ(out.gaps_over(70, 13000), 0u);
    } TEST_END();

    TEST_CASE("N9.25 Runner: A Hitch Of 400 ms Of The Frame Loop Leaves Nothing Behind: The Frame That Follows It Runs Its Time's Ticks, The Buffer Stays, Nobody Waits, And The Hitch Is Not Mistaken For A Slow Link") {
        Rig rig;
        rig.hitches = {{10000, 400}, {20000, 400}, {30000, 300}};
        rig.jitter = 5;
        rig.max_dt = 1000;                                                // the application: a frame hands the network its real time, up to a second
        const Rig::Outcome out = rig.run();
        ASSERT_TRUE(out.lag_from(33000) <= 40 + 50 + 40);                 // what the hitches left once: a queue of four or five turns (and, until it was run down, a buffer that stayed too large)
        ASSERT_TRUE(out.queued.back() <= 2);
        ASSERT_TRUE(out.lag_from(12000) <= 40 + 50 + 60);                 // a second and a half after the first hitch it is already back at one turn
        ASSERT_EQ(out.gaps_over(100, 12000), 2u);                         // the hitches at 20 s and 30 s themselves (the frame loop stood still), nothing else: no wait, no stall
        ASSERT_TRUE(out.longest_gap_from(12000) <= 400 + 70);
        // the buffer rule did not take the hitches for lateness of the link: it is at its smallest at the end
        sim::SimulationEngine sim;
        Trio::build(sim);
        LockstepRunner runner(sim);
        uint32_t next = 0;
        uint32_t now = 0;
        for (; now < 5000; now += 17) {                                   // 5 s of a steady link, then a frame that stood for 400 ms
            while (static_cast<uint64_t>(next) * kTurnMs + 40 <= now) {
                TurnMsg m;
                m.turn = next++;
                runner.on_turn(m);
            }
            runner.update(17);
        }
        ASSERT_EQ(runner.buffer_turns(), 1u);
        now += 400;
        while (static_cast<uint64_t>(next) * kTurnMs + 40 <= now) {
            TurnMsg m;
            m.turn = next++;
            runner.on_turn(m);
        }
        runner.update(400);                                               // eight turns were waiting: their read time says how long the window slept, not how late the link was
        ASSERT_EQ(runner.buffer_turns(), 1u);
        ASSERT_FALSE(runner.rebuilding());
        ASSERT_TRUE(runner.queued() <= 3);                                // what is left is the buffer (and the turn that is just due)
    } TEST_END();

    TEST_CASE("N9.26 Runner: A Window That Is Not Drawn For 5 s (one frame a second, as a browser does for a hidden tab) Keeps Up With The Match While It Is Hidden, And Keeps No Extra Delay When It Is Shown") {
        Rig rig;
        rig.hidden = {{10000, 15000}};
        rig.jitter = 5;
        const Rig::Outcome out = rig.run();
        size_t ticks_hidden = 0;                                          // the game of the hidden window ran in real time: 100 ticks in 5 s (a frame of a second runs a second's ticks)
        for (const uint32_t at : out.ticks) ticks_hidden += (at >= 11000 && at < 15000) ? 1 : 0;
        ASSERT_TRUE(ticks_hidden >= 4 * 20 - 21 && ticks_hidden <= 4 * 20 + 21);
        ASSERT_TRUE(out.lag_from(25000) <= 40 + 50 + 40);
        ASSERT_TRUE(out.queued.back() <= 2);
    } TEST_END();

    TEST_CASE("N9.27 Runner: On Links Of Every Kind The Buffer Is As Small As The Link Allows: One Turn On A Steady Link, More Where The Jitter Needs It, And Once It Has Learned The Link The Game Does Not Stand Still For 100 ms; A Spike Is A Stall Of Its Own Length At The Most") {
        for (const uint32_t jitter : {0u, 20u, 60u, 120u}) {
            for (uint32_t seed = 1; seed <= 3; ++seed) {
                Rig rig;
                rig.jitter = jitter;
                rig.seed = seed;
                rig.duration = 40000;
                const Rig::Outcome out = rig.run();
                ASSERT_EQ(out.gaps_over(100, 15000), 0u);                // learned: no more stops
                ASSERT_TRUE(out.queued.back() <= 5);
                if (jitter == 0) {
                    ASSERT_TRUE(out.lag_from(5000) >= 40 + 50 - 10 && out.lag_from(5000) <= 40 + 50 + 30);    // steady: the link and one turn
                    ASSERT_EQ(out.gaps_over(60, 3000), 0u);
                } else {
                    ASSERT_TRUE(out.lag_from(15000) <= 40 + jitter + 200 + 40);                               // never more than the jitter and four turns
                }
            }
        }
        // a link that holds a turn up for 300 ms now and then (a lost packet: head-of-line blocking): the game stops for what the spike lasts beyond the buffer, no longer
        for (const uint32_t seed : {1u, 2u, 3u, 4u}) {
            Rig rig;
            rig.jitter = 20;
            rig.spike_every = 140;
            rig.spike_ms = 300;
            rig.seed = seed;
            const Rig::Outcome out = rig.run();
            ASSERT_TRUE(out.longest_gap_from(10000) <= 300 + 70);
            ASSERT_TRUE(out.lag_from(10000) <= 40 + 20 + 200 + 60);
        }
    } TEST_END();

    TEST_CASE("N9.28 Waiting: A Window That Is Hardly Drawn (one frame in 0.75 s) Keeps Up With The Match, So The Others' Games Do Not Slow Down To Its Speed (a host with a seat does not seal more than 60 turns ahead of the slowest)") {
        Trio t;
        // guest 2 draws one frame every 750 ms from 5 s to 25 s (a window in the background); every frame hands it the real time since the one before
        auto b_slow = [](uint32_t now) { return now >= 5000 && now < 25000 && now % 750 != 0; };
        uint32_t a_at_15 = 0;
        uint32_t a_at_25 = 0;
        uint32_t longest_wait_a = 0;
        t.run_to(30000, b_slow, [&](uint32_t now) {
            if (now == 15000) a_at_15 = t.a->runner().next_turn_to_execute();
            if (now == 25000) a_at_25 = t.a->runner().next_turn_to_execute();
            if (now >= 5000 && now < 25000) longest_wait_a = std::max(longest_wait_a, t.a->runner().stalled_ms());
        });
        ASSERT_TRUE(a_at_25 - a_at_15 >= 195);                            // the other game ran at (almost) full speed: about 200 turns in 10 s (before: about 90)
        ASSERT_TRUE(longest_wait_a < 400);                                // and never stood still for long
        const uint32_t turn_a = t.a->runner().next_turn_to_execute();
        const uint32_t turn_b = t.b->runner().next_turn_to_execute();
        ASSERT_TRUE((turn_a > turn_b ? turn_a - turn_b : turn_b - turn_a) <= 6);       // 5 s after it is drawn normally again, the window is back in step
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
