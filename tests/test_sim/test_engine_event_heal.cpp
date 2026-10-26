// The heap of the next timed events of the ants (src/ants_sim/event_queue.cpp) is kept right by every writer in the simulation, and by two nets under them that a product
// build has and no match may ever need: the pass over every ant at the start of a tick (event_catch_up) and the selection of the next event, which puts right an entry that the
// ant does not bear out. This program runs THE PRODUCT BUILD (ants_sim, no shadow check: test_engine_lookups links the shadow build, where the nets stop the program at the
// first thing they have to put right) and makes them work: the fields of the events of ants are written from OUTSIDE the simulation, between two ticks, through get_unit()
// (a test hook does that; no production code does), with no word to the heap.
//
// The reference is an engine given the same writes and then the same word that a writer would have given (the events of every ant set again from its fields before the tick):
// its heap is right by construction, and the shadow build has proved that a heap that follows the fields chooses what the scans choose (test_engine_lookups). The two engines play
// the same match, the state hash is compared at every tick, and so are the heaps (what is pending, and when). Without the nets the engine that is not told plays such a write
// differently from the scans: an event that became pending is not in the heap, an event that is not pending any more is run.
//
//   ./test_engine_event_heal
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"
#include "../../src/ants_sim/sim_engine_impl.hpp"
#include "scripted_players.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include "../common/test_assert.hpp"

using namespace ants::sim;

namespace ants::sim {
// What SimulationEngine names as its friend: the engine's inside, for the tests of this program
struct SimulationEngineTestAccess {
    static SimulationEngineImpl& impl(SimulationEngine& e) { return *e.impl_; }
};
}  // namespace ants::sim

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (g_test_failures == prev) std::cout << "PASS (" << std::fixed << std::setprecision(1) << s << " s)\n";
}

namespace {

SimulationEngineImpl& inside(SimulationEngine& e) { return SimulationEngineTestAccess::impl(e); }

// "slot:K@time" for an event (K: B burn, C combat timer, L animation frame, P pause), "none" for no event
std::string event_text(bool found, const AntEventPick& p) {
    if (!found) return "none";
    return std::to_string(p.slot) + ":" + "BCLP"[static_cast<size_t>(p.kind) & 3u] + "@" + std::to_string(p.time);
}

// An engine with no map and ants made by hand: the heap and the scan are asked the same question
struct HandWorld {
    SimulationEngineImpl impl;
    movement::MotionClip multi;
    HandWorld() {
        impl.grid_.init_empty(8, 8);
        impl.movement_reset();
        for (size_t i = 0; i < movement::clip_count(); ++i) {
            const movement::MotionClip c = movement::clip_at(i);
            if (c.valid() && c.count > 1) {
                multi = c;
                break;
            }
        }
    }
    AntUnit& add(uint32_t id) {
        impl.ants_.push_back(std::make_unique<AntUnit>(id, TeamId::Black, AntType::Worker, 0, 0));
        return *impl.ants_.back();
    }
    // The next event at or before t_end by the heap and by the scan: the two answers, "heap X scan Y" when they differ
    std::string next(uint32_t t_end) {
        AntEventPick p, s;
        const bool found = impl.next_ant_event(t_end, p);
        const bool scan_found = impl.next_ant_event_scan(t_end, s);
        const std::string by_heap = event_text(found, p), by_scan = event_text(scan_found, s);
        return by_heap == by_scan && (!found || p.ant == s.ant) ? by_heap : "heap " + by_heap + " but scan " + by_scan;
    }
};

// The match that the two engines play: four teams on a small world, hatching, fire, bombs, deaths, orders of every kind; the same for both, from the same numbers
struct World {
    uint32_t seed, width, per_team, intensity;
    uint32_t ids{0};
};

void build(SimulationEngine& e, World& w) {
    ants_test::Lcg r(w.seed);
    const uint32_t width = w.width;
    e.init_test_world(width, width, w.seed);
    e.set_hatch_delay_ticks(30 + r.below(60));
    const int32_t mid = static_cast<int32_t>(width) / 2;
    e.set_anthill(0, TileCoord{mid - 2, 6});
    e.set_anthill(1, TileCoord{mid - 2, static_cast<int32_t>(width) - 10});
    e.set_anthill(2, TileCoord{6, mid - 2});
    e.set_anthill(3, TileCoord{static_cast<int32_t>(width) - 10, mid - 2});
    for (int k = 0; k < 3; ++k) {                                         // ponds
        const int32_t px = 10 + static_cast<int32_t>(r.below(width - 24)), py = 10 + static_cast<int32_t>(r.below(width - 24));
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 5; ++x) e.set_terrain(px + x, py + y, 2);
        }
    }
    for (int i = 0; i < 12; ++i) e.grid_mut().place_powerup(8 + static_cast<int32_t>(r.below(width - 16)), 8 + static_cast<int32_t>(r.below(width - 16)), static_cast<uint8_t>(i % 5));
    for (int i = 0; i < 25; ++i) e.grid_mut().place_bomb(8 + r.below(width - 16), 8 + r.below(width - 16), static_cast<uint8_t>(r.below(4)));
    for (int i = 0; i < 6; ++i) e.set_fire_at(TileCoord{8 + static_cast<int32_t>(r.below(width - 16)), 8 + static_cast<int32_t>(r.below(width - 16))}, 600 + r.below(1500));
    static const AntType kinds[] = {AntType::Worker, AntType::Combat, AntType::Bomber, AntType::Fire, AntType::Thief, AntType::Swimmer};
    uint32_t n = 0;
    for (uint8_t p = 0; p < 4; ++p) {
        for (uint32_t i = 0; i < w.per_team; ++i, ++n) {
            e.spawn_unit(p, kinds[r.below(6)], TileCoord{8 + static_cast<int32_t>(r.below(width - 16)), 8 + static_cast<int32_t>(r.below(width - 16))}, true);
        }
    }
    w.ids = n;
}

// What happens to the match at tick t besides its ticks: orders, hatching, deaths, fire, blasts, and a drop-out (a pure function of the world and t)
void happenings(SimulationEngine& e, const World& w, uint32_t t) {
    ants_test::Lcg r(w.seed * 1000003u + t * 7919u + 17u);
    if (t % 3 == 0) {
        const uint32_t n = 1 + r.below(w.intensity);
        for (uint32_t k = 0; k < n; ++k) {
            Command cmd;
            const uint32_t kind = r.below(10);
            cmd.type = kind < 5 ? CommandType::GroupMove : kind < 7 ? CommandType::GroupAttack : kind < 9 ? CommandType::GroupSpecial : CommandType::Stop;
            cmd.issuer = static_cast<uint8_t>(r.below(4));
            cmd.tile_x = static_cast<int16_t>(4 + r.below(w.width - 8));
            cmd.tile_y = static_cast<int16_t>(4 + r.below(w.width - 8));
            const uint32_t m = 1 + r.below(12);
            for (uint32_t j = 0; j < m; ++j) cmd.ants.push_back(1 + r.below(w.ids));
            e.apply_command(cmd);
        }
    }
    if (t % 17 == 5) {
        for (uint8_t p = 0; p < 4; ++p) e.try_hatch(p, AntType::Worker, true);
    }
    if (t % 41 == 7) e.kill_unit(1 + r.below(w.ids));
    if (t % 29 == 3) e.set_fire_at(TileCoord{8 + static_cast<int32_t>(r.below(w.width - 16)), 8 + static_cast<int32_t>(r.below(w.width - 16))}, 200 + r.below(300));
    if (t % 53 == 11) e.blast_tile_for_test(TileCoord{8 + static_cast<int32_t>(r.below(w.width - 16)), 8 + static_cast<int32_t>(r.below(w.width - 16))});
    if (t % 97 == 50) {
        for (uint8_t p = 0; p < 4; ++p) e.set_player_score(p, 400);
    }
    if (t == 380) e.drop_player(2);
}

// What a test hook may do between two ticks, with no word to the heap (the same writes for both engines): the label of an ant (loco_sync deals with it), and the fields that
// decide the events, which only the start-of-tick pass and the selection of the next event can put right
struct Writes {
    uint64_t label{0}, pause{0}, burn{0}, combat{0}, action{0}, frame{0};
};

void foreign_writes(SimulationEngine& e, const World& w, uint32_t t, Writes& count, bool tally) {
    ants_test::Lcg r(w.seed * 7777u + t * 104729u + 5u);
    const uint32_t now = static_cast<uint32_t>(e.current_tick()) * 50u;
    auto ant = [&]() -> AntUnit& { return e.get_unit(1 + r.below(w.ids)); };
    if (t % 7 == 2) {
        static const uint8_t labels[] = {0, 1, 2, 4, 5, 7, 8, 10, 12, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28};
        ant().state = static_cast<UnitState>(labels[r.below(sizeof labels)]);
        if (tally) ++count.label;
    }
    if (t % 11 == 4) {
        AntUnit& u = ant();
        u.pause_active = !u.pause_active;
        u.pause_fire_ms = now + 50u * (1 + r.below(8));
        if (tally) ++count.pause;
    }
    if (t % 13 == 5) {
        ant().burn_end_ms = now + 50u * (1 + r.below(8));
        if (tally) ++count.burn;
    }
    if (t % 9 == 1) {
        ant().combevt_due_ms = r.below(3) == 0 ? 0u : now + 50u * (1 + r.below(8));
        if (tally) ++count.combat;
    }
    if (t % 17 == 6) {
        ant().loco_action = r.below(3) == 0 ? AntUnit::kActionNone : AntUnit::kActionIdle;
        if (tally) ++count.action;
    }
    if (t % 19 == 3) {
        AntUnit& u = ant();
        if (u.loco.cursor != 0) u.loco.next_ms = now + 50u * (1 + r.below(8));
        if (tally) ++count.frame;
    }
}

// The heaps of two engines say the same: for every ant and every kind, pending or not, and when. Returns the number of places where they do not.
size_t heaps_differ(SimulationEngine& a, SimulationEngine& b) {
    SimulationEngineImpl& x = inside(a);
    SimulationEngineImpl& y = inside(b);
    size_t differ = 0;
    const size_t n = std::max(x.ants_.size(), y.ants_.size());
    for (size_t slot = 0; slot < n; ++slot) {
        for (size_t k = 0; k < kAntEventKinds; ++k) {
            uint32_t dx = 0, dy = 0;
            const bool px = x.event_heap_.pending_at(static_cast<uint32_t>(slot), static_cast<AntEventKind>(k), dx);
            const bool py = y.event_heap_.pending_at(static_cast<uint32_t>(slot), static_cast<AntEventKind>(k), dy);
            if (px != py || (px && dx != dy)) ++differ;
        }
    }
    return differ;
}

// Plays `ticks` ticks with two engines. Both are given the same orders and the same foreign writes; the second is then told (every ant's events set again from its fields), the
// first is not. Returns "" or what differs; `stale` is the number of places where the heap of the first was wrong before its tick, `Writes` what was written.
std::string play_with_writes(World w, uint32_t ticks, uint32_t first_write, uint64_t& stale, Writes& writes) {
    SimulationEngine heals, told;
    World w2 = w;
    build(heals, w);
    build(told, w2);
    for (uint32_t t = 0; t < ticks; ++t) {
        happenings(heals, w, t);
        happenings(told, w, t);
        if (t >= first_write) {
            foreign_writes(heals, w, t, writes, true);
            foreign_writes(told, w, t, writes, false);
            SimulationEngineImpl& im = inside(told);
            for (const auto& up : im.ants_) {
                if (up) im.event_refresh(*up);
            }
            stale += heaps_differ(heals, told);
        }
        heals.tick();
        told.tick();
        if (heals.state_hash() != told.state_hash()) return "tick " + std::to_string(t) + ": the state hashes differ";
        if (heaps_differ(heals, told) != 0) return "tick " + std::to_string(t) + ": the heaps differ after the tick";
    }
    return "";
}

void run_tests() {
    TEST_CASE("H1 A selection that meets an entry the ant does not bear out (a field written with no event_refresh) puts the ant right and answers as the scan does") {
        HandWorld w;
        AntUnit& a = w.add(1);
        w.add(2);
        w.impl.ants_.push_back(AntPtr());                                    // a hole in the table
        a.loco_action = AntUnit::kActionWalk;
        a.loco.clip = w.multi;
        a.loco.cursor = 1;
        a.loco.next_ms = 60;
        w.impl.event_refresh(a);
        a.burn_end_ms = 30;
        w.impl.event_refresh(a);
        ASSERT_TRUE(w.multi.valid() && w.next(100) == "0:B@30");
        a.burn_end_ms = 45;                                                  // the heap says 30
        ASSERT_TRUE(w.next(100) == "0:B@45");
        a.burn_end_ms = 0;                                                   // the heap says 45
        ASSERT_TRUE(w.next(100) == "0:L@60");
        w.impl.event_heap_.set(1, AntEventKind::Combevt, true, 10);          // an event of an ant that has none
        ASSERT_TRUE(w.next(100) == "0:L@60");
        w.impl.event_heap_.set(2, AntEventKind::Pause, true, 10);            // of a hole of the table
        ASSERT_TRUE(w.next(100) == "0:L@60");
        w.impl.event_heap_.set(9, AntEventKind::Loco, true, 10);             // of a place that is not in the table
        ASSERT_TRUE(w.next(100) == "0:L@60");
    } TEST_END();

    TEST_CASE("H2 Writes of the fields of events and of the labels of ants from outside, between ticks, in four worlds with orders, fire, bombs, hatching and deaths: the engine that is not told plays as the engine that is, the hash and the heaps equal at every tick") {
        struct Case {
            World world;
            uint32_t ticks, first_write;
        };
        const Case cases[] = {{{901, 64, 80, 6}, 500, 5}, {{902, 56, 120, 8}, 500, 20}, {{903, 90, 60, 12}, 450, 0}, {{904, 40, 30, 4}, 600, 3}};
        for (const Case& c : cases) {
            uint64_t stale = 0;
            Writes writes;
            const std::string why = play_with_writes(c.world, c.ticks, c.first_write, stale, writes);
            std::cout << "\n        seed " << c.world.seed << ": " << c.ticks << " ticks, writes of labels " << writes.label << ", pauses " << writes.pause << ", burns " << writes.burn << ", combat timers "
                      << writes.combat << ", actions " << writes.action << ", frames " << writes.frame << "; places where the heap was wrong before its tick: " << stale << "   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(writes.pause > 10 && writes.burn > 10 && writes.combat > 10 && writes.action > 10 && writes.frame > 5 && writes.label > 10);
            ASSERT_TRUE(stale > 20);                                         // the writes did leave the heap wrong: the nets had something to put right
        }
    } TEST_END();
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [ENGINE EVENT HEAP, PRODUCT BUILD] writes from outside are put right by the nets of the heap\n=======================================================\n";
    run_tests();
    std::cout << "\n=======================================================\n"
              << " ENGINE EVENT HEAP TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
