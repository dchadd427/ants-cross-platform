// Tests of the client-side prediction of one's own orders (include/ants_net/prediction.hpp, docs/NETWORK_PORT.md "Prediction of one's own orders").
//
// The rig plays the SERVER: the test seals the turns and chooses when each one reaches the client and in which turn an order of the player is sealed, so that every timing that matters
// is a number in the test. The client is the real thing: a confirmed engine, a LockstepRunner with a fixed jitter buffer (one turn is one tick, one step of the rig is 50 ms), the
// prediction, and, next to them, an ORACLE: a second engine that executes every sealed turn the moment it is sealed, so that it always knows the truth that the client will reach later.
//
// What is checked:
//   - exactness: with the right lead and no other player's commands the predicted engine equals the oracle at every frame and is never rebuilt; with a jitter buffer deeper than the lead
//     it equals the oracle whatever the other players do (the turns in hand are run as certain input, there is no guess);
//   - the corrections: another player's command, an order of one's own sealed one tick later or earlier than it was applied, an order the server never sealed: how many rebuilds each
//     costs, and that the prediction is right again after them;
//   - the property that makes it safe: the predicted engine that was advanced tick by tick equals the engine that a rebuild would make now (derived_hash), at every frame, under random
//     jitter, stalls, bursts, commands of every kind and a lead that changes;
//   - the prediction never touches the confirmed engine, the turns or the hashes.
#include "ants_net/lockstep.hpp"
#include "ants_net/prediction.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
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

Command make_command(CommandType type, uint8_t issuer, uint8_t other = 255, int16_t x = 0, int16_t y = 0, std::vector<uint32_t> ants = {}) {
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
        sim.set_player_score(p, 800);                                           // (the points that make hatching possible)
    }
    return ids;
}

// An order of `seat`: a group move, attack or special order, or Stop, on a random part of its ants (kinds that the prediction handles)
Command random_order(Lcg& rng, const Ids& ids, uint8_t seat) {
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
Command random_unpredicted(Lcg& rng, uint8_t seat) {
    const uint32_t kind = rng.below(4);
    if (kind == 0) return make_command(CommandType::Hatch, seat);
    const uint8_t other = static_cast<uint8_t>((seat + 1 + rng.below(3)) % sim::MAX_PLAYERS);
    if (kind == 1) return make_command(CommandType::AllianceInvite, seat, other);
    if (kind == 2) return make_command(CommandType::AllianceWithdraw, seat, other);
    return make_command(CommandType::AllianceBreak, seat);
}

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
    Prediction::Config prediction{};
};

struct View {
    uint32_t step{0};
    uint64_t display{0};
    std::vector<Command> applied;
    uint64_t hash{0};
};

class Rig {
public:
    explicit Rig(const Scenario& sc)
        : sc_(sc), runner_(confirmed_, runner_config(sc)) {
        ids = build_world(confirmed_, sc.seed, sc.match_ms);
        build_world(oracle_, sc.seed, sc.match_ms);
        if (sc.predict) {
            Prediction::Config pc = sc.prediction;
            pc.seat = 0;
            pred_ = std::make_unique<Prediction>(confirmed_, runner_, pc);
            pred_->set_expected_delay_ms(sc.lead_ms != 0 ? sc.lead_ms : (sc.buffer_turns + sc.down_delay + sc.uplink) * sim::TICK_MS);
            runner_.set_on_tick([this]() { pred_->on_tick(); });
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
    // Hold the link: no turn reaches the client before step `until` (they arrive in a burst then)
    void hold_link_until(uint32_t until) { hold_until_ = until; }

    void step() {
        // 1. the server seals the turn of this step; the oracle runs it at once
        TurnMsg t;
        t.turn = step_;
        t.commands = inbox_[step_];
        inbox_.erase(step_);
        std::stable_sort(t.commands.begin(), t.commands.end(), [](const Command& a, const Command& b) { return a.issuer < b.issuer; });     // (canonical order)
        sealed.push_back(t);
        pre.push_back(sc_.oracle_hashes ? oracle_.state_hash().total : 0u);
        for (const Command& c : t.commands) oracle_.apply_command(c);
        post.push_back(sc_.oracle_hashes ? oracle_.state_hash().total : 0u);
        oracle_.tick();
        // 2. the link: the turn arrives after the delay (never before an earlier one)
        uint32_t at = step_ + sc_.down_delay + (sc_.down_jitter != 0 ? rng_.below(sc_.down_jitter + 1) : 0u);
        at = std::max({at, last_arrival_, hold_until_});
        last_arrival_ = at;
        flight_.push_back({at, step_});
        while (!flight_.empty() && flight_.front().first <= step_) {
            runner_.on_turn(sealed[flight_.front().second]);
            flight_.pop_front();
        }
        // 3. the client's runner runs a tick if one is due (50 ms of its clock)
        runner_.update(sim::TICK_MS);
        // 4. a frame: the screen asks for the engine that it shows
        if (pred_ && pred_->active()) {
            View v;
            v.step = step_;
            sim::SimulationEngine& e = pred_->engine();
            v.display = pred_->display_tick();
            v.applied = pred_->applied_at_display();
            const bool check_now = derived_every_ != 0 && step_ % derived_every_ == 0;
            v.hash = (sc_.oracle_hashes || check_now) ? e.state_hash().total : 0u;
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

    uint32_t step_no() const { return step_; }
    sim::SimulationEngine& confirmed() { return confirmed_; }
    sim::SimulationEngine& oracle() { return oracle_; }
    LockstepRunner& runner() { return runner_; }
    Prediction* prediction() { return pred_.get(); }

    // Every view recorded at or after `from_step` must be the oracle's state at its display tick (before any command of that tick when none was applied, after the turn's commands when the
    // turn's own commands were); "" when so, else the first view that is not.
    // The views at ticks that the server has not sealed yet (the prediction stands ahead of the stream by the lead) cannot be checked, and are skipped; `verified`, when given, gets the
    // number of views that were checked.
    std::string verify_views(uint32_t from_step = 0, size_t* verified = nullptr) const {
        size_t checked = 0;
        for (const View& v : views) {
            if (v.step < from_step) continue;
            if (v.display >= pre.size()) continue;
            ++checked;
            if (verified != nullptr) *verified = checked;
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
    std::vector<uint64_t> pre;      // the oracle's hash before the commands of each tick
    std::vector<uint64_t> post;     // ... and after them (before the tick runs)
    std::vector<View> views;
    uint32_t own_orders{0};
    uint32_t derived_checks{0};
    uint32_t derived_mismatches{0};

private:
    uint64_t v_last_hash() const { return views.back().hash; }

    Scenario sc_;
    sim::SimulationEngine confirmed_;
    sim::SimulationEngine oracle_;
    LockstepRunner runner_;
    std::unique_ptr<Prediction> pred_;
    std::map<uint32_t, std::vector<Command>> inbox_;
    std::deque<std::pair<uint32_t, uint32_t>> flight_;     // (arrival step, turn)
    uint32_t step_{0};
    uint32_t last_arrival_{0};
    uint32_t hold_until_{0};
    Lcg rng_{1};
    uint32_t derived_every_{0};
};

// verify_views, and at least `min_views` of them must have been checkable (a test that checks nothing proves nothing)
std::string verify_min(const Rig& rig, uint32_t from_step, size_t min_views) {
    size_t checked = 0;
    const std::string why = rig.verify_views(from_step, &checked);
    if (!why.empty()) return why;
    if (checked < min_views) return "only " + std::to_string(checked) + " views could be checked, " + std::to_string(min_views) + " were expected";
    return {};
}

// ---------------------------------------------------------------------------------------------------------------------------------

void run_basic_tests() {
    TEST_CASE("RP1.1 Which Commands Are Predicted: Group Moves, Special Orders, Attacks And Stop; Not Hatch, The Alliance Commands, Quit Or Drop") {
        ASSERT_TRUE(Prediction::predicts(CommandType::GroupMove));
        ASSERT_TRUE(Prediction::predicts(CommandType::GroupSpecial));
        ASSERT_TRUE(Prediction::predicts(CommandType::GroupAttack));
        ASSERT_TRUE(Prediction::predicts(CommandType::Stop));
        for (CommandType t : {CommandType::None, CommandType::Hatch, CommandType::AllianceInvite, CommandType::AllianceAccept, CommandType::AllianceDeny, CommandType::AllianceWithdraw,
                              CommandType::AllianceBreak, CommandType::Quit, CommandType::Drop}) {
            ASSERT_FALSE(Prediction::predicts(t));
        }
    } TEST_END();

    TEST_CASE("RP1.2 Nothing Is Predicted Before The First Tick Has Run (the \"Get ready\" dialog), And Every Order Is Left To The Caller") {
        Scenario sc;
        Rig rig(sc);
        Prediction& p = *rig.prediction();
        ASSERT_FALSE(p.active());
        sim::CommandResult r;
        r.status = sim::CommandResult::Status::RejectedMalformed;
        ASSERT_FALSE(p.submit(make_command(CommandType::GroupMove, 0, 255, 10, 10, {rig.ids.ants[0][0]}), r));
        ASSERT_TRUE(r.status == sim::CommandResult::Status::RejectedMalformed);          // untouched
        rig.run(1);                                                                         // turn 0 sealed and in flight: the runner has no turn yet
        ASSERT_FALSE(p.active());
        ASSERT_EQ(rig.confirmed().current_tick(), 0u);
        rig.run(4);                                                                         // the buffer is full, the first tick has run
        ASSERT_TRUE(rig.confirmed().current_tick() >= 1u);
        ASSERT_TRUE(p.active());
        ASSERT_EQ(p.stats().starts, 1u);
        ASSERT_TRUE(p.display_tick() > rig.confirmed().current_tick());
    } TEST_END();

    TEST_CASE("RP1.3 The Display Tick Is The Confirmed Tick Plus The Lead, Tick By Tick, And The Lead Follows The Delay: It Rises At Once, Falls Slowly And Is Bounded") {
        Scenario sc;
        sc.lead_ms = 150;                                                                   // 3 ticks
        Rig rig(sc);
        Prediction& p = *rig.prediction();
        rig.run(20);
        ASSERT_EQ(p.lead_ticks(), 3u);
        for (int i = 0; i < 30; ++i) {
            rig.step();
            ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + p.lead_ticks());
        }
        p.set_expected_delay_ms(300);                                                       // slower: the lead rises at the next tick
        rig.step();
        ASSERT_EQ(p.lead_ticks(), 6u);
        ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + 6);
        p.set_expected_delay_ms(100);                                                       // faster: nothing changes for 40 ticks, then one tick at a time
        for (int i = 0; i < 39; ++i) rig.step();
        ASSERT_EQ(p.lead_ticks(), 6u);
        const uint64_t held_at = p.display_tick();
        rig.step();
        ASSERT_EQ(p.lead_ticks(), 5u);
        ASSERT_EQ(p.display_tick(), held_at);                                               // (the lead falls by holding the engine for one tick: the confirmed tick moved on, the display tick did not)
        ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + 5);
        rig.step();
        ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + 5);
        ASSERT_EQ(p.display_tick(), held_at + 1);
        p.set_expected_delay_ms(60000);
        rig.step();
        ASSERT_EQ(p.lead_ticks(), 12u);                                                     // bounded above
        p.set_expected_delay_ms(0);
        for (int i = 0; i < 12 * 40 + 5; ++i) rig.step();
        ASSERT_EQ(p.lead_ticks(), 1u);                                                      // and below
        ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + 1);
    } TEST_END();

    TEST_CASE("RP1.4 submit(): The Engine's Own Verdict Comes Back (the ant that answers, the ants that needed the order), A Foreign Seat Or An Unpredicted Kind Is Refused, The Order Is Applied At The Display Tick") {
        Scenario sc;
        Rig rig(sc);
        rig.run(30);
        Prediction& p = *rig.prediction();
        ASSERT_TRUE(p.active());
        const uint32_t ant = rig.ids.ants[0][0];
        sim::CommandResult r;
        const Command move = make_command(CommandType::GroupMove, 0, 255, 40, 40, {ant});
        ASSERT_TRUE(p.submit(move, r));
        ASSERT_TRUE(r.status == sim::CommandResult::Status::Applied);
        ASSERT_EQ(r.ack_ant, ant);
        ASSERT_EQ(r.needing_order, 1u);
        ASSERT_EQ(p.pending_orders(), 1u);
        ASSERT_TRUE(p.applied_at_display().size() == 1 && p.applied_at_display()[0] == move);
        ASSERT_EQ(p.stats().commands_predicted, 1u);
        sim::CommandResult untouched;
        ASSERT_FALSE(p.submit(make_command(CommandType::GroupMove, 1, 255, 5, 5, {rig.ids.ants[1][0]}), untouched));      // another seat's: not its to predict
        ASSERT_FALSE(p.submit(make_command(CommandType::Hatch, 0), untouched));
        ASSERT_FALSE(p.submit(make_command(CommandType::AllianceInvite, 0, 1), untouched));
        ASSERT_FALSE(p.submit(make_command(CommandType::Quit, 0), untouched));
        ASSERT_EQ(p.pending_orders(), 1u);
        // an order that the engine refuses (a stranger's ant) is answered as the engine answers it, and still tracked (the server will seal it and the engine will refuse it there too)
        const Command stranger = make_command(CommandType::GroupMove, 0, 255, 8, 8, {rig.ids.ants[2][0]});
        ASSERT_TRUE(p.submit(stranger, r));
        ASSERT_TRUE(r.status == sim::CommandResult::Status::Ignored);
        ASSERT_EQ(r.ack_ant, 0u);
        ASSERT_EQ(p.pending_orders(), 2u);
    } TEST_END();
}

void run_exactness_tests() {
    TEST_CASE("RP2.1 Exact Timing, No Other Player: The Predicted Engine Equals The Oracle At Every Frame, Is Never Rebuilt, And The Order Stands At The Tick The Turn Gives It (3 buffers, 3 lags, 2 uplinks each, 300 steps)") {
        for (uint32_t buffer : {1u, 2u, 3u}) {
            for (uint32_t down : {0u, 1u, 2u}) {
                for (uint32_t up : {static_cast<uint32_t>(down), static_cast<uint32_t>((down + 2) % 3)}) {          // (six combinations of lag and uplink)
                    Scenario sc;
                    sc.seed = 10 + buffer * 7 + down * 3 + up;
                    sc.buffer_turns = buffer;
                    sc.down_delay = down;
                    sc.uplink = up;
                    Rig rig(sc);
                    Lcg rng(sc.seed);
                    rig.run(buffer + down + 8);
                    for (uint32_t s = 0; s < 180; ++s) {
                        if (s % 7 == 0) rig.issue(random_order(rng, rig.ids, 0));
                        rig.step();
                    }
                    rig.run_quiet(40);
                    ASSERT_EQ(rig.prediction()->stats().rebuilds, 0u);
                    ASSERT_EQ(rig.prediction()->stats().commands_predicted, rig.own_orders);
                    ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
                    ASSERT_OK(verify_min(rig, 0, 140));
                }
            }
        }
    } TEST_END();

    TEST_CASE("RP2.2 A Jitter Buffer Deeper Than The Lead Is Run As Certain Input: Whatever The Other Players Do (hatching and alliances too) The Predicted Engine Is The Oracle, With No Rebuild And No Guess") {
        for (uint32_t seed : {3u, 4u}) {
            Scenario sc;
            sc.seed = seed;
            sc.buffer_turns = 4;
            sc.down_delay = 1;
            sc.lead_ms = 50;                                                                 // one tick: the display tick stays inside the turns that are in hand
            Rig rig(sc);
            Lcg rng(seed * 31u);
            rig.run(12);
            for (uint32_t s = 0; s < 300; ++s) {
                if (s % 3 == 0) {
                    const uint8_t seat = static_cast<uint8_t>(1 + rng.below(3));
                    rig.schedule(rig.step_no() + 2, rng.below(3) == 0 ? random_unpredicted(rng, seat) : random_order(rng, rig.ids, seat));
                }
                if (s % 11 == 0) rig.schedule(rig.step_no() + 3, random_unpredicted(rng, 0));      // (the player's own hatch and alliance commands: not predicted)
                rig.step();
            }
            rig.run_quiet(30);
            ASSERT_EQ(rig.prediction()->stats().rebuilds, 0u);
            ASSERT_TRUE(rig.prediction()->stats().turns_checked > 250);
            ASSERT_OK(verify_min(rig, 0, 250));
        }
    } TEST_END();

    TEST_CASE("RP2.3 The Orders Of A Match Of Four Seats (every seat gives orders, the lead is exact for the player's own): the Player's Orders Never Make A Correction By Themselves, Another Seat's Do, One Rebuild For Each Turn That Has A Command") {
        Scenario sc;
        sc.seed = 21;
        sc.buffer_turns = 1;
        sc.down_delay = 1;
        sc.uplink = 1;
        Rig rig(sc);
        Lcg rng(21);
        rig.run(12);
        std::set<uint32_t> foreign_turns;
        for (uint32_t s = 0; s < 300; ++s) {
            if (s % 9 == 0) rig.issue(random_order(rng, rig.ids, 0));
            if (s % 13 == 4) {
                const uint8_t seat = static_cast<uint8_t>(1 + rng.below(3));
                const uint32_t at = rig.step_no() + 2;
                rig.schedule(at, random_order(rng, rig.ids, seat));
                foreign_turns.insert(at);
            }
            rig.step();
        }
        rig.run_quiet(40);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_TRUE(st.rebuilds_foreign >= 1);
        ASSERT_EQ(st.rebuilds_own_timing, 0u);
        ASSERT_TRUE(st.rebuilds <= foreign_turns.size());                                    // at most one rebuild for each turn that had another player's command (turns that come together share one)
        ASSERT_EQ(st.rebuilds, st.rebuilds_foreign + st.rebuilds_other + st.rebuilds_own_timing);
    } TEST_END();

    TEST_CASE("RP2.4 A Lead Shorter Than The Delay (set too low, or a buffer that grew after a burst): An Order Is Put Behind The Turns In Hand And Chased Turn By Turn Until Its Own (one rebuild for each turn that passes it, no more), Is Never Run Twice, And The Prediction Stays Derived State At Every Frame") {
        for (uint32_t seed : {81u, 82u}) {
            Scenario sc;
            sc.seed = seed;
            sc.buffer_turns = 3;
            sc.down_delay = 1;
            sc.uplink = 2;
            sc.lead_ms = 50;                                                                 // one tick: shorter than the three turns in hand, and than the way of the order
            Rig rig(sc);
            Lcg rng(seed);
            rig.check_derived(1);
            rig.run(12);
            uint32_t orders = 0;
            for (uint32_t s = 0; s < 150; ++s) {
                if (s % 12 == 0) {
                    rig.issue(random_order(rng, rig.ids, 0));
                    ++orders;
                }
                rig.step();
            }
            rig.run_quiet(30);
            const Prediction::Stats& st = rig.prediction()->stats();
            ASSERT_EQ(rig.derived_mismatches, 0u);                                           // at every frame: what was advanced tick by tick is what a rebuild would make
            ASSERT_TRUE(rig.derived_checks > 150);
            ASSERT_EQ(st.commands_predicted, orders);
            ASSERT_EQ(st.commands_lost, 0u);
            // The order is put at the first tick whose turn has not arrived, which the server sealed (without it) before the order was given; the turns up to its own (uplink 2 and the lag 1
            // later) pass it one by one, each costing one rebuild that moves it a tick on: the price of a lead that is too low, bounded by the turns that pass the order.
            ASSERT_EQ(st.rebuilds, 3ull * orders);
            ASSERT_EQ(st.rebuilds_foreign, 0u);
            ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
            ASSERT_OK(verify_min(rig, rig.step_no() - 20, 10));                              // and when the last of them has been sealed and run, the prediction is the oracle
        }
    } TEST_END();
}

void run_correction_tests() {
    TEST_CASE("RP3.1 A Command Of Another Player Costs One Rebuild When Its Turn Arrives, The Picture Is Right From That Frame On, And The Others' Ants Are The Ones That Move In The Correction") {
        Scenario sc;
        sc.seed = 31;
        sc.lead_ms = 400;                                                                   // the display stands 8 ticks ahead: the order's ants have walked for a few ticks when its turn comes
        sc.uplink = 6;                                                                      // (and the player's own order is exact for that lead: it is no correction)
        Rig rig(sc);
        Lcg rng(31);
        rig.prediction()->set_measure_corrections(true);
        rig.run(12);
        // the player's own ants walk all the time; seat 1 gives one order, sealed at step t
        rig.issue(make_command(CommandType::GroupMove, 0, 255, 50, 50, rig.ids.ants[0]));
        rig.run(6);
        const uint32_t t = rig.step_no() + 3;
        rig.schedule(t, make_command(CommandType::GroupMove, 1, 255, 20, 40, rig.ids.ants[1]));
        rig.run(20);
        rig.run_quiet(30);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_EQ(st.rebuilds, 1u);
        ASSERT_EQ(st.rebuilds_foreign, 1u);
        ASSERT_EQ(st.corrections, 1u);
        ASSERT_TRUE(st.corrections_visible == 1u && st.ants_moved >= 1u);                    // the foreign ants stood elsewhere after the correction
        ASSERT_TRUE(st.max_move_px > 0 && st.max_move_px <= 64);                             // by a few ticks of walking, never more
        ASSERT_OK(verify_min(rig, t + sc.down_delay + 1, 20));                                  // from the frame of the turn's arrival on, every frame is the oracle's
    } TEST_END();

    TEST_CASE("RP3.2 An Order Sealed At Another Tick Than The One Where It Was Applied: Earlier Costs One Rebuild, Later Costs Two (its tick passes without it, then its turn comes) Unless It Lands Where The First Miss Put It; Afterwards The Prediction Is Right") {
        // The lead is 5 ticks, the buffer 1 and the lag 1, so the order is applied 5 ticks ahead of the confirmed engine; an uplink of 3 steps seals it exactly there. A miss (its turn
        // arrives without it) puts it at the display tick of that moment, which is lead - buffer = 4 ticks after the tick that it missed.
        struct Case {
            int shift;                       // the tick of the turn minus the tick at which the prediction applied the order
            uint64_t rebuilds;
            uint64_t own_timing;             // ... of which the order's own turn found at another tick than the one it was applied at (the others: a turn that passed it without it)
        };
        for (const Case c : {Case{-3, 1, 1}, Case{-2, 1, 1}, Case{-1, 1, 1}, Case{0, 0, 0}, Case{1, 2, 1}, Case{2, 2, 1}, Case{3, 2, 1}, Case{4, 1, 0}, Case{5, 3, 1}}) {
            Scenario sc;
            sc.seed = 40 + static_cast<uint32_t>(c.shift + 5);
            sc.lead_ms = 250;
            sc.uplink = static_cast<uint32_t>(3 + c.shift);
            Rig rig(sc);
            rig.run(12);
            const uint32_t given = rig.step_no();
            rig.issue(make_command(CommandType::GroupMove, 0, 255, 44, 12, rig.ids.ants[0]));
            rig.run(50);
            rig.run_quiet(30);
            const Prediction::Stats& st = rig.prediction()->stats();
            ASSERT_EQ(st.rebuilds, c.rebuilds);
            ASSERT_EQ(st.rebuilds_own_timing, c.own_timing);
            ASSERT_EQ(st.rebuilds_other, c.rebuilds - c.own_timing);
            ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
            ASSERT_OK(verify_min(rig, given + sc.uplink + sc.down_delay + 8, 20));           // after its turn has arrived every frame is the oracle's
        }
    } TEST_END();

    TEST_CASE("RP3.3 An Order That The Server Never Seals Is Dropped After pending_timeout_ticks And The Picture Is Rebuilt Without It; Until Then Every Turn That Passes It Costs At Most One Rebuild") {
        Scenario sc;
        sc.seed = 51;
        sc.prediction.pending_timeout_ticks = 25;
        Rig rig(sc);
        rig.check_derived(1);                                                               // (at every frame: the frame in which the order is dropped is already rebuilt without it)
        rig.run(12);
        const sim::CommandResult r = rig.issue_lost(make_command(CommandType::GroupMove, 0, 255, 50, 50, {rig.ids.ants[0][0], rig.ids.ants[0][1]}));
        ASSERT_TRUE(r.status == sim::CommandResult::Status::Applied);
        const uint32_t given = rig.step_no();
        rig.run(10);
        ASSERT_EQ(rig.prediction()->pending_orders(), 1u);                                  // still waiting: nothing has sealed it and the timeout has not run out
        const uint64_t rebuilds_waiting = rig.prediction()->stats().rebuilds;
        ASSERT_TRUE(rebuilds_waiting >= 1);                                                 // (each turn that passed it without it was a miss: the prediction put it at the display tick again)
        ASSERT_TRUE(rebuilds_waiting <= 10);                                                // ... and never more than one rebuild for a turn
        rig.run(40);                                                                        // past the timeout
        ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
        ASSERT_EQ(rig.prediction()->stats().commands_lost, 1u);
        const uint64_t rebuilds_after = rig.prediction()->stats().rebuilds;
        rig.run_quiet(30);
        ASSERT_EQ(rig.prediction()->stats().rebuilds, rebuilds_after);                       // nothing more to correct
        ASSERT_OK(verify_min(rig, given + 25 + 8, 10));                                      // rebuilt without the order: the oracle's again
        ASSERT_EQ(rig.derived_mismatches, 0u);
        ASSERT_TRUE(rig.derived_checks > 60);
    } TEST_END();

    TEST_CASE("RP3.4 Two Orders Of The Player, The First Of Which The Server Never Seals: The Second Matches Its Turn, The First Is Counted Lost At Once, And The Picture Is Right Afterwards") {
        Scenario sc;
        sc.seed = 53;
        Rig rig(sc);
        rig.run(12);
        rig.issue_lost(make_command(CommandType::GroupMove, 0, 255, 50, 50, {rig.ids.ants[0][0]}));
        rig.run(1);
        rig.issue(make_command(CommandType::GroupMove, 0, 255, 12, 44, {rig.ids.ants[0][1]}));
        rig.run(30);
        rig.run_quiet(30);
        ASSERT_EQ(rig.prediction()->stats().commands_lost, 1u);
        ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
        ASSERT_OK(verify_min(rig, rig.step_no() - 40, 20));
    } TEST_END();
}

void run_property_tests() {
    TEST_CASE("RP4.1 The Predicted Engine Is Derived State: Advanced Tick By Tick It Equals The Engine That A Rebuild Would Make At Every Frame (24 random matches: jitter, bursts, every kind of command, a lead that changes)") {
        uint32_t checks = 0;
        for (uint32_t seed = 1; seed <= 24; ++seed) {
            Lcg rng(seed * 7919u);
            Scenario sc;
            sc.oracle_hashes = false;
            sc.seed = seed;
            sc.buffer_turns = 1 + rng.below(3);
            sc.down_delay = rng.below(3);
            sc.down_jitter = rng.below(4);
            sc.uplink = rng.below(4);
            sc.lead_ms = 50 * (1 + rng.below(6));
            Rig rig(sc);
            rig.check_derived(3);
            rig.run(sc.buffer_turns + sc.down_delay + 6);
            for (uint32_t s = 0; s < 120; ++s) {
                const uint32_t roll = rng.below(100);
                if (roll < 14) rig.issue(random_order(rng, rig.ids, 0));
                else if (roll < 18) rig.schedule(rig.step_no() + rng.below(5), random_unpredicted(rng, 0));      // own, not predicted
                else if (roll < 30) rig.schedule(rig.step_no() + rng.below(5), random_order(rng, rig.ids, static_cast<uint8_t>(1 + rng.below(3))));
                else if (roll < 34) rig.schedule(rig.step_no() + rng.below(5), random_unpredicted(rng, static_cast<uint8_t>(1 + rng.below(3))));
                if (s % 40 == 20) rig.prediction()->set_expected_delay_ms(50 * (1 + rng.below(8)));
                if (s % 50 == 30) rig.hold_link_until(rig.step_no() + 1 + rng.below(6));                 // a stall: the turns that are held arrive in a burst
                rig.step();
            }
            rig.run_quiet(20);
            checks += rig.derived_checks;
            ASSERT_EQ(rig.derived_mismatches, 0u);
            ASSERT_TRUE(rig.derived_checks > 30);
            // and the confirmed engine reached every turn: nothing was lost on the way
            ASSERT_TRUE(rig.confirmed().current_tick() > 100);
        }
        ASSERT_TRUE(checks > 700);
    } TEST_END();

    TEST_CASE("RP4.2 After Everything Has Settled The Prediction Is The Oracle: A Burst Of Random Commands From All Seats, Then Quiet Turns (corrections converge)") {
        for (uint32_t seed = 100; seed < 108; ++seed) {
            Lcg rng(seed);
            Scenario sc;
            sc.seed = seed;
            sc.buffer_turns = 1 + rng.below(2);
            sc.down_delay = rng.below(3);
            sc.down_jitter = rng.below(3);
            sc.uplink = rng.below(3);
            Rig rig(sc);
            rig.run(sc.buffer_turns + sc.down_delay + 6);
            for (uint32_t s = 0; s < 120; ++s) {
                if (s < 80 && rng.below(5) == 0) rig.issue(random_order(rng, rig.ids, 0));
                if (s < 80 && rng.below(6) == 0) rig.schedule(rig.step_no() + rng.below(4), random_order(rng, rig.ids, static_cast<uint8_t>(1 + rng.below(3))));
                rig.step();
            }
            rig.run_quiet(60);                                                                   // (quiet: empty turns; every order has been sealed and has arrived)
            ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
            ASSERT_FALSE(rig.prediction()->stale());
            ASSERT_OK(verify_min(rig, rig.step_no() - 20, 10));                                     // the last twenty frames: the oracle's, whatever happened before
        }
    } TEST_END();

    TEST_CASE("RP4.3 The Prediction Never Touches The Confirmed Engine, The Turns Or The Hashes: The Same Match With And Without It Has The Same Confirmed State At Every Tick And The Same Cues") {
        for (uint32_t seed : {61u, 62u}) {
            Scenario with;
            with.seed = seed;
            with.down_jitter = 2;
            Scenario without = with;
            without.predict = false;
            Rig a(with);
            Rig b(without);
            Lcg ra(seed);
            Lcg rb(seed);
            a.run(10);
            b.run(10);
            for (uint32_t s = 0; s < 200; ++s) {
                if (s % 6 == 0) {
                    const Command ca = random_order(ra, a.ids, 0);
                    const Command cb = random_order(rb, b.ids, 0);
                    a.issue(ca);
                    b.issue(cb);
                }
                if (s % 10 == 3) {
                    const Command fa = random_order(ra, a.ids, 2);
                    const Command fb = random_order(rb, b.ids, 2);
                    a.schedule(a.step_no() + 1, fa);
                    b.schedule(b.step_no() + 1, fb);
                }
                a.step();
                b.step();
                ASSERT_EQ(a.confirmed().current_tick(), b.confirmed().current_tick());
                ASSERT_TRUE(a.confirmed().state_hash() == b.confirmed().state_hash());
                ASSERT_EQ(a.runner().next_turn_to_execute(), b.runner().next_turn_to_execute());
                const auto audio_a = a.confirmed().poll_audio_events();
                const auto audio_b = b.confirmed().poll_audio_events();
                ASSERT_EQ(audio_a.size(), audio_b.size());
                ASSERT_EQ(a.confirmed().poll_news_events().size(), b.confirmed().poll_news_events().size());
            }
            ASSERT_TRUE(a.sealed.size() == b.sealed.size());
            for (size_t i = 0; i < a.sealed.size(); ++i) ASSERT_TRUE(a.sealed[i].commands == b.sealed[i].commands);
        }
    } TEST_END();
}

void run_state_tests() {
    TEST_CASE("RP5.1 Suspension: Suspended The Prediction Is Gone At Once (the confirmed engine is what is shown), Resumed It Begins At The Next Tick, And An Order In Flight Is Corrected When Its Turn Comes") {
        Scenario sc;
        sc.seed = 71;
        Rig rig(sc);
        rig.run(20);
        Prediction& p = *rig.prediction();
        ASSERT_TRUE(p.active());
        rig.issue(make_command(CommandType::GroupMove, 0, 255, 30, 30, {rig.ids.ants[0][0]}));
        ASSERT_EQ(p.pending_orders(), 1u);
        p.set_suspended(true);
        ASSERT_FALSE(p.active());
        ASSERT_EQ(p.pending_orders(), 0u);
        sim::CommandResult r;
        ASSERT_FALSE(p.submit(make_command(CommandType::GroupMove, 0, 255, 31, 31, {rig.ids.ants[0][1]}), r));
        const uint64_t starts = p.stats().starts;
        rig.run(10);
        ASSERT_FALSE(p.active());
        ASSERT_EQ(p.stats().starts, starts);
        p.set_suspended(false);
        ASSERT_FALSE(p.active());                                                           // (not before a tick has run)
        rig.run(1);
        ASSERT_TRUE(p.active());
        ASSERT_EQ(p.stats().starts, starts + 1);
        rig.run(40);
        rig.run_quiet(30);
        ASSERT_OK(verify_min(rig, rig.step_no() - 30, 10));
    } TEST_END();

    TEST_CASE("RP5.2 The End Of The Match: The Prediction Reaches It Before The Confirmed Engine, Nothing Hangs, And Both End Alike") {
        Scenario sc;
        sc.seed = 72;
        sc.match_ms = 8000;                                                                  // 160 ticks
        Rig rig(sc);
        Lcg rng(72);
        rig.run(10);
        for (uint32_t s = 0; s < 200; ++s) {
            if (s % 20 == 0) rig.issue(random_order(rng, rig.ids, 0));
            rig.step();
        }
        rig.run_quiet(20);
        ASSERT_TRUE(rig.confirmed().is_match_over());
        ASSERT_TRUE(rig.oracle().is_match_over());
        ASSERT_TRUE(rig.confirmed().state_hash() == rig.oracle().state_hash());
        ASSERT_TRUE(rig.prediction()->engine().is_match_over());
        ASSERT_TRUE(rig.prediction()->engine().get_world_state().match_result.is_over);
    } TEST_END();

    TEST_CASE("RP5.3 The Cues And News Of The Predicted Ticks Are Kept Once, With The Tick That Made Them, Bounded; Taking Them Empties The Buffer; A Rebuild Starts A New Generation") {
        Scenario sc;
        sc.seed = 73;
        sc.prediction.max_events = 40;
        Rig rig(sc);
        Lcg rng(73);
        rig.run(10);
        for (uint32_t s = 0; s < 120; ++s) {
            if (s % 5 == 0) rig.issue(random_order(rng, rig.ids, 0));
            rig.step();
        }
        const std::vector<Prediction::PredictedAudio> audio = rig.prediction()->take_audio();
        ASSERT_TRUE(!audio.empty());
        ASSERT_TRUE(audio.size() <= 40);                                                     // bounded
        for (size_t i = 1; i < audio.size(); ++i) ASSERT_TRUE(audio[i].tick >= audio[i - 1].tick || audio[i].generation != audio[i - 1].generation);
        ASSERT_TRUE(rig.prediction()->take_audio().empty());                                 // taking empties it
        const uint32_t generation = audio.back().generation;
        rig.schedule(rig.step_no() + 1, make_command(CommandType::GroupMove, 1, 255, 10, 40, rig.ids.ants[1]));
        rig.run(10);
        const std::vector<Prediction::PredictedAudio> later = rig.prediction()->take_audio();
        bool newer = false;
        for (const auto& e : later) newer = newer || e.generation > generation;
        ASSERT_TRUE(newer || later.empty());                                                 // (the rebuild's replay is a later generation)
        ASSERT_TRUE(rig.prediction()->stats().rebuilds >= 1);
    } TEST_END();

    TEST_CASE("RP5.4 A Rebuild Does Not Turn What The Confirmed Engine Has Queued Into Predicted Cues And News (the one-minute warning passed in both engines: the replay does not make it again, the copy must not carry it over)") {
        Scenario sc;
        sc.seed = 74;
        sc.match_ms = 62000;                                                                 // the one-minute warning is made by the tick at which the clock falls below 61 s: tick 21
        sc.lead_ms = 200;
        Rig rig(sc);
        rig.run(60);                                                                         // the confirmed engine is past tick 21, the predicted one too; nobody has taken the confirmed engine's cues
        Prediction& p = *rig.prediction();
        {
            sim::SimulationEngine probe(rig.confirmed());                                    // (a look at what the confirmed engine has queued: a copy is polled, not the engine)
            bool queued = false;
            for (const sim::AudioEvent& e : probe.poll_audio_events()) queued = queued || e.sound_id == sim::SoundID::OneMinute;
            ASSERT_TRUE(queued);
        }
        const auto count_audio = [](const std::vector<Prediction::PredictedAudio>& v) {
            size_t n = 0;
            for (const auto& e : v) n += e.event.sound_id == sim::SoundID::OneMinute ? 1u : 0u;
            return n;
        };
        const auto count_news = [](const std::vector<Prediction::PredictedNews>& v) {
            size_t n = 0;
            for (const auto& e : v) n += e.event.string_id == sim::strings::kOneMinute ? 1u : 0u;
            return n;
        };
        ASSERT_EQ(count_audio(p.take_audio()), 1u);                                          // the predicted engine made the warning once, when it ran tick 21 (ahead of the confirmed one) ...
        ASSERT_EQ(count_news(p.take_news()), 1u);
        const uint64_t rebuilds = p.stats().rebuilds;
        rig.schedule(rig.step_no() + 1, make_command(CommandType::GroupMove, 1, 255, 10, 40, rig.ids.ants[1]));       // another player's order: a rebuild, with the warning still queued in the confirmed engine
        rig.run(8);
        ASSERT_TRUE(p.stats().rebuilds > rebuilds);
        ASSERT_EQ(count_audio(p.take_audio()), 0u);                                          // ... and the rebuild's replay starts after it: no second warning, from the copy or anywhere
        ASSERT_EQ(count_news(p.take_news()), 0u);
    } TEST_END();

    TEST_CASE("RP5.5 A Tick That The Prediction Was Not Told Of (a catch-up that went round the hooks, a runner that was restarted) Makes It Stale: The Next Frame Is Rebuilt From The Confirmed Engine, Which Knows What Happened") {
        Scenario sc;
        sc.seed = 75;
        Rig rig(sc);
        rig.check_derived(1);
        Lcg rng(75);
        rig.run(20);
        rig.issue(random_order(rng, rig.ids, 0));
        rig.run(10);
        ASSERT_EQ(rig.derived_mismatches, 0u);
        const uint64_t rebuilds = rig.prediction()->stats().rebuilds;
        // two ticks of the confirmed engine that no hook announced, with a command of another player in them: the prediction knows nothing of either
        rig.confirmed().apply_command(make_command(CommandType::GroupMove, 1, 255, 12, 44, rig.ids.ants[1]));
        rig.confirmed().tick();
        rig.confirmed().tick();
        rig.step();
        ASSERT_EQ(rig.derived_mismatches, 0u);                                                // the frame after it is the confirmed engine's truth plus the lead, not the old picture
        ASSERT_TRUE(rig.prediction()->stats().rebuilds > rebuilds);
        rig.run(10);
        ASSERT_EQ(rig.derived_mismatches, 0u);
    } TEST_END();
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "\n=======================================================\n [PREDICTION SUITE] client-side prediction of one's own orders\n=======================================================\n";
    run_basic_tests();
    run_exactness_tests();
    run_correction_tests();
    run_property_tests();
    run_state_tests();
    std::cout << "\n=======================================================\n"
              << " PREDICTION TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
