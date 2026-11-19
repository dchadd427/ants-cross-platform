#pragma once

// The rig of the prediction's tests (tests/test_net/test_prediction.cpp, test_prediction_hints.cpp): a server that is played by the test, a real runner and engines, and an oracle.
// Moved out of test_prediction.cpp unchanged so that the tests of the hints can use it too; every program that includes it defines its own counters here.

#include "ants_net/cue_router.hpp"
#include "ants_net/lockstep.hpp"
#include "ants_net/prediction.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"
#include "manual_clock.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "../common/test_assert.hpp"

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
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; run_tests.sh clears it)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(120) << name << " ... " << std::flush;
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

// A check function answers "" when all is well, else what is wrong (printed with the failure)
#define ASSERT_OK(expr) \
    do { \
        ++g_assert_count; \
        const std::string why_ = (expr); \
        if (!why_.empty()) { \
            std::cout << "FAILED!\n    " << why_ << "\n    at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

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

inline Command make_command(CommandType type, uint8_t issuer, uint8_t other = 255, int16_t x = 0, int16_t y = 0, std::vector<uint32_t> ants = {}) {
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
// The world: four players on a 60 x 60 map, every ant type, a river with a bridge, a lunch box and a bomb (the same on every engine)
// ---------------------------------------------------------------------------------------------------------------------------------

struct Ids {
    std::vector<uint32_t> ants[sim::MAX_PLAYERS];
};

inline Ids build_world(sim::SimulationEngine& sim, uint32_t seed, uint32_t match_ms = 720000, uint32_t extra_workers = 0, bool fog = false) {
    Ids ids;
    if (fog) sim.set_fog_of_war_enabled(true);                                       // (the viewer is seat 0: the player whose orders are predicted)
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
        sim.set_player_score(p, 800);                                           // (the points that make hatching possible)
        for (uint32_t i = 0; i < extra_workers; ++i) {                          // (a big selection: a block of workers of 10 columns beside the hill's ants)
            const int32_t y = hills[p].y < 30 ? hills[p].y + 8 + static_cast<int32_t>(i / 10) : hills[p].y - 8 - static_cast<int32_t>(i / 10);
            ids.ants[p].push_back(sim.spawn_unit(p, AntType::Worker, TileCoord{hills[p].x - 3 + static_cast<int32_t>(i % 10), y}));
        }
    }
    return ids;
}

// An order of `seat`: a group move, attack or special order, or Stop, on a random part of its ants (kinds that the prediction handles)
inline Command random_order(Lcg& rng, const Ids& ids, uint8_t seat) {
    std::vector<uint32_t> pick;
    for (uint32_t id : ids.ants[seat]) {
        if (rng.below(3) != 0) pick.push_back(id);
    }
    if (pick.empty()) pick.push_back(ids.ants[seat][0]);
    const int16_t x = static_cast<int16_t>(rng.below(60));
    const int16_t y = static_cast<int16_t>(rng.below(60));
    const uint32_t kind = rng.below(10);
    if (kind < 5) return make_command(CommandType::GroupMove, seat, 255, x, y, pick);
    if (kind < 7) return make_command(CommandType::GroupAttack, seat, 255, x, y, pick);
    if (kind < 9) return make_command(CommandType::GroupSpecial, seat, 255, x, y, pick);
    return make_command(CommandType::Stop, seat, 255, 0, 0, pick);
}

// A command of a kind that the prediction does not handle (it waits for the confirmation)
inline Command random_unpredicted(Lcg& rng, uint8_t seat) {
    const uint32_t kind = rng.below(4);
    if (kind == 0) return make_command(CommandType::Hatch, seat);
    const uint8_t other = static_cast<uint8_t>((seat + 1 + rng.below(3)) % sim::MAX_PLAYERS);
    if (kind == 1) return make_command(CommandType::AllianceInvite, seat, other);
    if (kind == 2) return make_command(CommandType::AllianceWithdraw, seat, other);
    return make_command(CommandType::AllianceBreak, seat);
}

// Work that costs at least `ns` of the thread's CPU time AND of wall time, for RP7.6, the one test on the real clocks (a block is charged the smaller of the two, and Windows' thread clock
// moves in steps of 15.6 ms). It waits as long as it takes (a thread that was not run reads nothing meanwhile) and gives up after 10 s only, so that a stuck clock fails the test.
inline void burn_cpu(uint64_t ns) {
    const uint64_t t0 = thread_cpu_ns();
    const auto wall0 = std::chrono::steady_clock::now();
    for (;;) {
        const uint64_t spent = thread_cpu_ns() - t0;
        const auto wall = std::chrono::steady_clock::now() - wall0;
        if (spent >= ns && wall >= std::chrono::nanoseconds(static_cast<std::chrono::nanoseconds::rep>(ns))) return;
        if (wall > std::chrono::seconds(10)) return;
    }
}

constexpr uint64_t kMs = 1000ull * 1000ull;

// ---------------------------------------------------------------------------------------------------------------------------------
// The rig
// ---------------------------------------------------------------------------------------------------------------------------------

struct Scenario {
    uint32_t seed{1};
    uint32_t buffer_turns{1};      // the client's jitter buffer, fixed (the turns it keeps in hand)
    uint32_t down_delay{1};        // steps from the seal of a turn to its arrival at the client
    uint32_t down_jitter{0};       // ... plus a random 0 .. this (the order of the turns is kept: a late turn holds the ones behind it)
    uint32_t uplink{1};            // an order of the player is sealed this many steps after it was given
    uint32_t lead_ms{0};           // the delay the prediction is told (0: the exact one for this scenario, (buffer + down_delay + uplink) ticks)
    bool predict{true};
    bool oracle_hashes{true};      // record the oracle's hash of every tick (the tests that compare with the oracle); off: only the engine runs (the property tests)
    uint32_t match_ms{720000};
    uint32_t extra_workers{0};     // workers added to every player beyond the six ants (the tests of a selection of more than 32 ants)
    bool fog{false};               // Fog of War: the views are also compared with the oracle's explored ground (it is no part of the state hash)
    uint8_t seat{0};               // the player whose orders are predicted (every test but the hints' order of a turn: seat 0)
    Prediction::Config prediction{rig_config()};
    // The rig's prediction has the lead that the scenario names and keeps it (the tests hold a wrong lead to account, and the exact one to exactness): it does not learn its lead and has no bias
    // (the tests of the learning and of the bias switch them on), and no budget (the tests run on busy machines: a stall of the test process is not the prediction's cost; the tests of the
    // budget set their own)
    static Prediction::Config rig_config() {
        Prediction::Config c;
        c.budget_ns = UINT64_MAX;
        c.learn_lead = false;
        c.lead_bias_ticks = 0;
        return c;
    }
};

struct View {
    uint32_t step{0};
    uint64_t display{0};
    std::vector<Command> applied;
    uint64_t hash{0};
    std::vector<uint8_t> fog;
    std::vector<uint8_t> confirmed_fog;
};

class Rig {
public:
    explicit Rig(const Scenario& sc)
        : sc_(sc), runner_(confirmed_, runner_config(sc)) {
        ids = build_world(confirmed_, sc.seed, sc.match_ms, sc.extra_workers, sc.fog);
        build_world(oracle_, sc.seed, sc.match_ms, sc.extra_workers, sc.fog);
        if (sc.predict) {
            Prediction::Config pc = sc.prediction;
            pc.seat = sc.seat;
            pred_ = std::make_unique<Prediction>(confirmed_, runner_, pc);
            pred_->set_expected_delay_ms(sc.lead_ms != 0 ? sc.lead_ms : (sc.buffer_turns + sc.down_delay + sc.uplink) * sim::TICK_MS);
            runner_.set_on_tick([this]() {
                pred_->on_tick();
                if (on_tick_extra_) on_tick_extra_();
            });
            runner_.set_on_turn([this](const TurnMsg& t) { pred_->on_turn(t); });
        }
        rng_ = Lcg(sc.seed * 977u + 5u);
    }

    static LockstepRunner::Config runner_config(const Scenario& sc) {
        LockstepRunner::Config rc;
        rc.jitter.min_turns = rc.jitter.max_turns = sc.buffer_turns;
        return rc;
    }

    // The player (seat 0) gives an order now: the prediction applies it at once and the server seals it `uplink` steps from now
    sim::CommandResult issue(const Command& c) {
        sim::CommandResult r;
        if (pred_) pred_->submit(c, r);
        inbox_[step_ + sc_.uplink].push_back(c);
        ++own_orders;
        return r;
    }
    // Another player's command (or one of the player's own that the prediction does not handle), sealed in the turn of step `at_step`
    void schedule(uint32_t at_step, const Command& c) { inbox_[at_step].push_back(c); }
    // The order is given to the prediction but the server never takes it (a refused command)
    sim::CommandResult issue_lost(const Command& c) {
        sim::CommandResult r;
        if (pred_) pred_->submit(c, r);
        return r;
    }
    // A HINT of another seat's order reaches the client at step `at_step`: before that step's turn arrives (`after_turns` false) or after it. `tick` is the tick the sender's screen applied it at, `seq` the sender's number
    // for it (a copy that is delivered twice has the same one)
    void schedule_hint(uint32_t at_step, const Command& c, uint32_t seq, uint64_t tick, bool after_turns = false) { hint_inbox_[at_step].push_back(HintSpec{c, seq, tick, after_turns}); }
    // Hold the link: no turn reaches the client before step `until` (they arrive in a burst then)
    void hold_link_until(uint32_t until) { hold_until_ = until; }
    // The orders that the player gives from now on are sealed this many steps after they were given (a link that got slower or faster)
    void set_uplink(uint32_t steps) { sc_.uplink = steps; }

    void step(bool frame = true) {
        // 1. the server seals the turn of this step; the oracle runs it at once
        TurnMsg t;
        t.turn = step_;
        t.commands = inbox_[step_];
        inbox_.erase(step_);
        std::stable_sort(t.commands.begin(), t.commands.end(), [](const Command& a, const Command& b) { return a.issuer < b.issuer; });     // (canonical order)
        sealed.push_back(t);
        pre.push_back(sc_.oracle_hashes ? oracle_.state_hash().total : 0u);
        if (sc_.fog) pre_fog.push_back(oracle_.get_world_state().fog_revealed);
        for (const Command& c : t.commands) oracle_.apply_command(c);
        post.push_back(sc_.oracle_hashes ? oracle_.state_hash().total : 0u);
        oracle_.tick();
        // 2. the link: the turn arrives after the delay (never before an earlier one)
        uint32_t at = step_ + sc_.down_delay + (sc_.down_jitter != 0 ? rng_.below(sc_.down_jitter + 1) : 0u);
        at = std::max({at, last_arrival_, hold_until_});
        last_arrival_ = at;
        flight_.push_back({at, step_});
        deliver_hints(false);
        while (!flight_.empty() && flight_.front().first <= step_) {
            runner_.on_turn(sealed[flight_.front().second]);
            flight_.pop_front();
        }
        deliver_hints(true);
        // 3. the client's runner runs a tick if one is due (50 ms of its clock)
        runner_.update(sim::TICK_MS);
        // 4. a frame: the screen asks for the engine that it shows (a step without one leaves a prediction that a turn has shown to be wrong as it is: stale)
        if (frame && pred_ && pred_->active()) {
            View v;
            v.step = step_;
            sim::SimulationEngine& e = pred_->engine();
            v.display = pred_->display_tick();
            v.applied = pred_->applied_at_display();
            const bool check_now = derived_every_ != 0 && step_ % derived_every_ == 0;
            v.hash = (sc_.oracle_hashes || check_now) ? e.state_hash().total : 0u;
            if (sc_.fog) {
                v.fog = e.get_world_state().fog_revealed;
                v.confirmed_fog = confirmed_.get_world_state().fog_revealed;
            }
            views.push_back(std::move(v));
            if (check_now) {
                ++derived_checks;
                if (pred_->derived_hash().total != v_last_hash()) ++derived_mismatches;
            }
        }
        ++step_;
    }
    void run(uint32_t steps) {
        for (uint32_t i = 0; i < steps; ++i) step();
    }
    // Quiet steps: nothing is ordered, the turns are empty
    void run_quiet(uint32_t steps) { run(steps); }

    // Compare the predicted engine with the derived one at every `every`-th frame (0: never)
    void check_derived(uint32_t every) { derived_every_ = every; }
    // Something to do after the prediction's tick, in the tick hook (the application's post_tick: it reads what the engines made)
    void set_on_tick_extra(std::function<void()> fn) { on_tick_extra_ = std::move(fn); }

    uint32_t step_no() const { return step_; }
    sim::SimulationEngine& confirmed() { return confirmed_; }
    sim::SimulationEngine& oracle() { return oracle_; }
    LockstepRunner& runner() { return runner_; }
    Prediction* prediction() { return pred_.get(); }

    // Every view recorded at or after `from_step` must be the oracle's state at its display tick (before any command of that tick when none was applied, after the turn's commands when the
    // turn's own commands were); "" when so, else the first view that is not.
    // The views at ticks that the server has not sealed yet (the prediction stands ahead of the stream by the lead) cannot be checked, and are skipped; `verified`, when given, gets the
    // number of views that were checked.
    std::string verify_views(uint32_t from_step = 0, size_t* verified = nullptr, bool compare_state = true) const {
        size_t checked = 0;
        for (const View& v : views) {
            if (v.step < from_step) continue;
            if (v.display >= pre.size()) continue;
            ++checked;
            if (verified != nullptr) *verified = checked;
            if (sc_.fog) {                                                               // the picture's explored ground is the confirmed engine's, which never goes beyond the oracle's at the tick that is shown
                const std::vector<uint8_t>& truth = pre_fog[v.display];
                for (size_t i = 0; i < v.fog.size() && i < truth.size(); ++i) {
                    if (v.fog[i] != 0 && truth[i] == 0) return "step " + std::to_string(v.step) + ": the predicted engine at tick " + std::to_string(v.display) + " has explored ground that the oracle's has not";
                }
                if (v.fog != v.confirmed_fog) return "step " + std::to_string(v.step) + ": the predicted engine's explored ground is not the confirmed engine's";
            }
            if (!compare_state) continue;                                               // (only the explored ground: a picture that other seats' unknown orders make differ from the oracle's)
            if (v.applied.empty()) {
                if (v.hash != pre[v.display]) return "step " + std::to_string(v.step) + ": the predicted engine at tick " + std::to_string(v.display) + " is not the oracle's";
            } else {
                if (!(v.applied == sealed[v.display].commands)) return "step " + std::to_string(v.step) + ": the commands applied at tick " + std::to_string(v.display) + " are not the turn's";
                if (v.hash != post[v.display]) return "step " + std::to_string(v.step) + ": the predicted engine at tick " + std::to_string(v.display) + " (after its commands) is not the oracle's";
            }
        }
        if (verified != nullptr) *verified = checked;
        return {};
    }

    Ids ids;
    std::vector<TurnMsg> sealed;
    std::vector<std::vector<uint8_t>> pre_fog;      // the oracle's explored ground before each tick (Scenario::fog)
    std::vector<uint64_t> pre;      // the oracle's hash before the commands of each tick
    std::vector<uint64_t> post;     // ... and after them (before the tick runs)
    std::vector<View> views;
    uint32_t own_orders{0};
    std::vector<Prediction::HintVerdict> hint_verdicts;       // what became of each hint that was delivered, in order
    uint32_t derived_checks{0};
    uint32_t derived_mismatches{0};

private:
    struct HintSpec {
        Command command;
        uint32_t seq{0};
        uint64_t tick{0};
        bool after_turns{false};
    };
    void deliver_hints(bool after_turns) {
        const auto it = hint_inbox_.find(step_);
        if (it == hint_inbox_.end() || !pred_) return;
        for (const HintSpec& h : it->second) {
            if (h.after_turns != after_turns) continue;
            hint_verdicts.push_back(pred_->hint(h.command, h.seq, h.tick));
        }
    }
    uint64_t v_last_hash() const { return views.back().hash; }

    Scenario sc_;
    sim::SimulationEngine confirmed_;
    sim::SimulationEngine oracle_;
    LockstepRunner runner_;
    std::unique_ptr<Prediction> pred_;
    std::map<uint32_t, std::vector<Command>> inbox_;
    std::map<uint32_t, std::vector<HintSpec>> hint_inbox_;
    std::deque<std::pair<uint32_t, uint32_t>> flight_;     // (arrival step, turn)
    uint32_t step_{0};
    uint32_t last_arrival_{0};
    uint32_t hold_until_{0};
    Lcg rng_{1};
    uint32_t derived_every_{0};
    std::function<void()> on_tick_extra_;
};

// verify_views, and at least `min_views` of them must have been checkable (a test that checks nothing proves nothing)
inline std::string verify_min(const Rig& rig, uint32_t from_step, size_t min_views) {
    size_t checked = 0;
    const std::string why = rig.verify_views(from_step, &checked);
    if (!why.empty()) return why;
    if (checked < min_views) return "only " + std::to_string(checked) + " views could be checked, " + std::to_string(min_views) + " were expected";
    return {};
}


}  // namespace
