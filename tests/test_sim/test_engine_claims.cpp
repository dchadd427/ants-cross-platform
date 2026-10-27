// The claim lists (src/ants_sim/order_claims.cpp): the ants that claim a tile, by tile, answer "does a team-mate claim this tile?" (can_enter, rule R6) without a scan of every ant,
// and must give the scan's answer in every state, so that no match changes by a single bit (docs/ARCHITECTURE.md, "Lookups in the tick loop").
//
// This program links ants_sim_shadow, like test_engine_lookups: every question in a tick is also run through the scan (every call up to 2,048 ants, every 16th above) and stops the program
// on a difference, and every list is checked against the ants' orders at the end of a tick and after a copy. The matches here are played by two engines at once, one on the scans alone
// (LinearOnly) and one on the lists, with the state hash (all eight parts) compared at every tick; the worlds that call each writer of an order by hand are in this file, the crowds
// with every kind of order are those of test_engine_lookups (it asserts that the lists answered there).
//
//   ./test_engine_claims      all the tests (seconds)
#include "ants_sim/command.hpp"
#include "ants_sim/lookup_shadow.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#include "engine_twin.hpp"
#include "../../src/ants_sim/sim_engine_impl.hpp"

#include <chrono>
#include <iomanip>
#include "../common/test_assert.hpp"

using namespace ants::sim;
using namespace ants::sim::twin_test;

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

// A world of 12 x 12 tiles with no engine around it: ants are made, ordered and removed by the functions that the simulation uses (add_ant, set_order_kind, set_order_tile,
// clear_path_of, remove_ant, go_to), and the lists are read from the world itself
struct ClaimWorld {
    SimulationEngineImpl impl;
    ClaimWorld() { reset(12, 12); }
    void reset(uint32_t w, uint32_t h) {
        impl.ants_.clear();
        impl.grid_.init_empty(w, h);
        impl.movement_reset();
        impl.next_ant_id_ = 1;
    }
    AntUnit& spawn(uint8_t player, int32_t tx, int32_t ty) {
        AntUnit* a = impl.add_ant(std::make_unique<AntUnit>(impl.next_ant_id_++, static_cast<TeamId>(player & 3u), AntType::Worker, tx, ty));
        a->player_id = player;
        impl.set_position(*a, tx * 32 + 16, ty * 32 + 16);
        return *a;
    }
    void order(AntUnit& a, uint8_t kind, TileCoord tile) {
        impl.set_order_kind(a, kind);
        impl.set_order_tile(a, tile);
    }
    // The slots in the list of a tile, read from the lists (sorted)
    std::vector<uint32_t> list_of(int32_t tx, int32_t ty) {
        std::vector<uint32_t> out;
        const size_t cell = impl.occ_cell_index(TileCoord{tx, ty});
        if (cell == SimulationEngineImpl::kNoCell || cell >= impl.claim_ants_.cells()) return out;
        for (uint32_t s = impl.claim_ants_.first(cell); s != TileAntIndex::kNone; s = impl.claim_ants_.next(s)) out.push_back(s);
        std::sort(out.begin(), out.end());
        return out;
    }
    uint32_t slot(const AntUnit& a) const { return impl.ant_slot(a); }
    // The question in a tick (the lists answer, the shadow build compares with the scan) and outside it (the scan)
    bool asks(const AntUnit& a, int32_t tx, int32_t ty, bool in_tick) {
        impl.occ_in_tick_ = in_tick;
        const bool answer = impl.claimed_by_mate(a, TileCoord{tx, ty});
        impl.occ_in_tick_ = false;
        return answer;
    }
};

// Every ant against every tile of the grid and the ring around it, in a tick and outside one: the lists' answer is the scan's (the shadow build stops on a difference, here it is
// also compared by hand), and the lists are the ants' orders
std::string claim_disagreements(ClaimWorld& w, uint32_t salt) {
    for (size_t i = salt % 3u; i < w.impl.ants_.size(); i += 3) {
        const AntUnit& a = *w.impl.ants_[i];
        for (int32_t y = -2; y <= static_cast<int32_t>(w.impl.grid_.height()) + 1; ++y) {
            for (int32_t x = -2; x <= static_cast<int32_t>(w.impl.grid_.width()) + 1; ++x) {
                const bool scan = w.impl.claimed_by_mate_scan(a, TileCoord{x, y});
                if (w.asks(a, x, y, true) != scan || w.asks(a, x, y, false) != scan) return "ant " + std::to_string(a.id) + " asks about (" + std::to_string(x) + ", " + std::to_string(y) + ")";
            }
        }
    }
    w.impl.claims_verify();
    return "";
}

#define ASSERT_CLAIMS_OK(w, salt)                                                                            \
    do {                                                                                                     \
        const std::string why_ = claim_disagreements(w, salt);                                               \
        ASSERT_TRUE(why_.empty());                                                                           \
        if (!why_.empty()) std::cout << "\n        " << why_ << "\n";                                        \
    } while (0)

// What the shadow checks of the lists did since this was made
struct ClaimMarks {
    uint64_t calls{lookup_shadow_claim_calls()}, hits{lookup_shadow_claim_hits()}, checks{lookup_shadow_claim_checks()}, verifies{lookup_shadow_claim_verifies()};
    uint64_t catch_ups{lookup_shadow_claim_catch_ups()}, changes{lookup_shadow_claim_catch_up_changes()};
    uint64_t calls_since() const { return lookup_shadow_claim_calls() - calls; }
    uint64_t hits_since() const { return lookup_shadow_claim_hits() - hits; }
    uint64_t checks_since() const { return lookup_shadow_claim_checks() - checks; }
    uint64_t verifies_since() const { return lookup_shadow_claim_verifies() - verifies; }
    uint64_t catch_ups_since() const { return lookup_shadow_claim_catch_ups() - catch_ups; }
    uint64_t changes_since() const { return lookup_shadow_claim_catch_up_changes() - changes; }
};

void run_claim_tests() {
    TEST_CASE("C1.1 Claim lists, each writer by hand: an order to a tile puts the ant in that tile's list, a new tile moves it, another kind of order, a removal and a reset of the path take it out (answers worked out by hand)") {
        ClaimWorld w;
        AntUnit& a = w.spawn(0, 2, 2);
        AntUnit& b = w.spawn(0, 3, 3);                               // a team-mate of a
        AntUnit& c = w.spawn(1, 4, 4);                               // another team
        ASSERT_TRUE(w.list_of(5, 5).empty() && !w.asks(b, 5, 5, true));
        w.impl.set_order_kind(a, AntUnit::kOrderMove);               // the kind alone, then the tile: only the pair claims
        w.impl.set_order_tile(a, TileCoord{5, 5});
        ASSERT_TRUE(w.list_of(5, 5) == std::vector<uint32_t>{w.slot(a)});
        ASSERT_TRUE(w.asks(b, 5, 5, true) && w.asks(b, 5, 5, false));   // a team-mate claims it
        ASSERT_TRUE(!w.asks(a, 5, 5, true) && !w.asks(a, 5, 5, false)); // not its own claim
        ASSERT_TRUE(!w.asks(c, 5, 5, true) && !w.asks(c, 5, 5, false)); // not another team's
        ASSERT_TRUE(!w.asks(b, 6, 6, true));
        w.impl.set_order_tile(a, TileCoord{6, 6});                   // a new tile: it moves
        ASSERT_TRUE(w.list_of(5, 5).empty() && w.list_of(6, 6) == std::vector<uint32_t>{w.slot(a)});
        w.impl.set_order_kind(a, AntUnit::kOrderHome);               // Home claims as Move does
        ASSERT_TRUE(w.list_of(6, 6) == std::vector<uint32_t>{w.slot(a)} && w.asks(b, 6, 6, true));
        w.impl.set_order_kind(a, AntUnit::kOrderAttack);             // any other kind does not
        ASSERT_TRUE(w.list_of(6, 6).empty() && !w.asks(b, 6, 6, true));
        w.impl.set_order_kind(a, AntUnit::kOrderMove);
        ASSERT_TRUE(w.list_of(6, 6) == std::vector<uint32_t>{w.slot(a)});
        w.impl.clear_path_of(a);                                     // the reset of the walk and of the order
        ASSERT_TRUE(w.list_of(6, 6).empty() && a.orig_order == AntUnit::kOrderNone);
        w.order(a, AntUnit::kOrderMove, TileCoord{7, 7});
        w.order(b, AntUnit::kOrderMove, TileCoord{7, 7});            // two claimants of one tile: each is a team-mate to the other
        ASSERT_TRUE(w.list_of(7, 7).size() == 2 && w.asks(a, 7, 7, true) && w.asks(b, 7, 7, true));
        w.impl.remove_ant(a);                                        // a removed ant claims nothing, whatever its order says
        ASSERT_TRUE(a.removed && a.orig_order == AntUnit::kOrderMove && w.list_of(7, 7) == std::vector<uint32_t>{w.slot(b)});
        ASSERT_TRUE(!w.asks(b, 7, 7, true) && w.asks(c, 7, 7, true) == false);
        AntUnit& d = w.spawn(1, 8, 8);
        w.order(d, AntUnit::kOrderMove, TileCoord{-1, -1});          // a tile off the grid has no list: the scan answers, in a tick too
        ASSERT_TRUE(w.asks(c, -1, -1, true) && w.asks(c, -1, -1, false) && !w.asks(d, -1, -1, true) && !w.asks(b, -1, -1, true));
        ASSERT_CLAIMS_OK(w, 1);
    } TEST_END();

    TEST_CASE("C1.2 Claim lists: a pile of claimants of every team on one tile, and many tiles, answer as the scan does for every ant (also an ant that is the only claimant of its team)") {
        ClaimWorld w;
        std::vector<AntUnit*> ants;
        for (int i = 0; i < 120; ++i) ants.push_back(&w.spawn(static_cast<uint8_t>(i % 4), 1 + i % 10, 1 + (i / 10) % 10));
        for (size_t i = 0; i < ants.size(); ++i) {
            if (i % 5 == 4) continue;                                // some ants claim nothing
            w.order(*ants[i], i % 7 == 0 ? AntUnit::kOrderHome : AntUnit::kOrderMove, i < 60 ? TileCoord{6, 6} : TileCoord{static_cast<int32_t>(i % 12), static_cast<int32_t>((i / 12) % 12)});
        }
        ASSERT_CLAIMS_OK(w, 2);
        ASSERT_TRUE(w.list_of(6, 6).size() >= 40);
        for (size_t i = 0; i < ants.size(); i += 2) w.impl.remove_ant(*ants[i]);
        ASSERT_CLAIMS_OK(w, 3);
        ASSERT_TRUE(w.impl.claim_ants_.cells() == w.impl.occ_.size());
    } TEST_END();

    TEST_CASE("C1.3 Claim lists and writes from outside: an order written by hand is not in the lists, the scan answers outside a tick, and the start-of-tick pass puts the lists right (counted, not stopped, when a test writes on purpose)") {
        ClaimWorld w;
        AntUnit& a = w.spawn(0, 2, 2);
        AntUnit& b = w.spawn(0, 3, 3);
        a.orig_order = AntUnit::kOrderMove;                          // no writer is told
        a.orig_order_tile = TileCoord{7, 7};
        ASSERT_TRUE(w.list_of(7, 7).empty());
        ASSERT_TRUE(w.asks(b, 7, 7, false));                         // outside a tick the scan sees it
        const CountStale counting;
        const ClaimMarks marks;
        {
            const SimulationEngineImpl::OccTickScope in_tick(w.impl);
            w.impl.claims_catch_up_all();                            // movement_tick starts so
            ASSERT_TRUE(marks.changes_since() == 1 && marks.catch_ups_since() == 2);
            ASSERT_TRUE(w.list_of(7, 7) == std::vector<uint32_t>{w.slot(a)});
            ASSERT_TRUE(w.impl.claimed_by_mate(b, TileCoord{7, 7}));
            w.impl.claims_catch_up_all();                            // nothing more to put right
            ASSERT_TRUE(marks.changes_since() == 1);
        }
        b.removed = true;                                            // a removal by hand, and a tile by hand
        a.orig_order_tile = TileCoord{8, 8};
        w.impl.claims_catch_up_all();
        ASSERT_TRUE(marks.changes_since() == 2 && w.list_of(7, 7).empty() && w.list_of(8, 8) == std::vector<uint32_t>{w.slot(a)});
        ASSERT_CLAIMS_OK(w, 4);
    } TEST_END();

    TEST_CASE("C1.4 Claim lists: copy, assignment, move, restart and a grid of another size keep them as the ants' orders say (the lists are copied with the ants, emptied by a new match, made again for another grid)") {
        ClaimWorld w;
        for (int i = 0; i < 40; ++i) {
            AntUnit& a = w.spawn(static_cast<uint8_t>(i % 4), 1 + i % 10, 1 + i / 10);
            w.order(a, AntUnit::kOrderMove, TileCoord{static_cast<int32_t>(i % 5), static_cast<int32_t>(i % 3)});
        }
        ASSERT_CLAIMS_OK(w, 5);
        SimulationEngineImpl copy(w.impl);
        copy.claims_verify();
        ASSERT_TRUE(copy.claim_ants_ == w.impl.claim_ants_ && copy.claim_cell_ == w.impl.claim_cell_);
        copy.set_order_tile(*copy.ants_[3], TileCoord{9, 9});        // the copy is its own: the original's lists do not change
        copy.claims_verify();
        w.impl.claims_verify();
        ASSERT_TRUE(!(copy.claim_ants_ == w.impl.claim_ants_));
        SimulationEngineImpl assigned;
        assigned = w.impl;
        assigned.claims_verify();
        SimulationEngineImpl moved(std::move(assigned));
        moved.claims_verify();
        w.impl.grid_.init_empty(13, 12);                             // another size: the next refresh starts the grid and the lists again
        w.impl.occ_refresh();
        w.impl.claims_verify();
        ASSERT_TRUE(w.impl.claim_ants_.cells() == 13u * 12u);
        w.order(*w.impl.ants_[0], AntUnit::kOrderHome, TileCoord{12, 11});
        ASSERT_CLAIMS_OK(w, 6);
        w.impl.ants_.clear();                                        // a new match
        w.impl.movement_reset();
        w.impl.claims_verify();
        ASSERT_TRUE(w.impl.claim_cell_.empty());
        bool any = false;
        for (size_t c = 0; c < w.impl.claim_ants_.cells(); ++c) any = any || w.impl.claim_ants_.first(c) != TileAntIndex::kNone;
        ASSERT_TRUE(!any);
    } TEST_END();

    TEST_CASE("C1.5 Claim lists, each function of the simulation that writes an order, called by itself on an ant that claims a tile (and on a removed one): the lists are the ants' orders afterwards, and every question is the scan's") {
        struct Case {
            const char* name;
            std::function<void(ClaimWorld&, AntUnit&)> call;
        };
        const TileCoord target{5, 4}, approach{4, 4};
        const std::vector<Case> cases = {
            {"end_walk_to_idle", [](ClaimWorld& w, AntUnit& a) { w.impl.end_walk_to_idle(a); }},
            {"start_plant", [&](ClaimWorld& w, AntUnit& a) { w.impl.start_plant(a, target, approach); }},
            {"start_defuse", [&](ClaimWorld& w, AntUnit& a) { w.impl.start_defuse(a, target, approach); }},
            {"start_ignite", [&](ClaimWorld& w, AntUnit& a) { w.impl.start_ignite(a, target, approach); }},
            {"start_extinguish", [&](ClaimWorld& w, AntUnit& a) { w.impl.grid_.place_firewall(5, 4, 0); w.impl.start_extinguish(a, target, approach); }},
            {"start_bridge_build", [&](ClaimWorld& w, AntUnit& a) { w.impl.start_bridge_build(a, target, approach); }},
            {"start_bridge_demolish", [&](ClaimWorld& w, AntUnit& a) { w.impl.grid_.set_layer2(5, 4, TILE_BRIDGE4, 0); w.impl.start_bridge_demolish(a, target, approach); }},
            {"enter_hill", [](ClaimWorld& w, AntUnit& a) { w.impl.enter_hill(a); }},
            {"start_raid", [](ClaimWorld& w, AntUnit& a) { w.impl.start_raid(a, 1, 5); }},
            {"start_harvest", [](ClaimWorld& w, AntUnit& a) { w.impl.start_harvest(a, TileCoord{3, 3}, 1); }},
            {"start_knock_flight", [](ClaimWorld& w, AntUnit& a) { w.impl.start_knock_flight(a, 2, 1, TileCoord{a.pixel_x / 32, a.pixel_y / 32}); }},
            {"stun_or_die", [](ClaimWorld& w, AntUnit& a) { w.impl.stun_or_die(a); }},
            {"stun_or_die of a 1 hp survivor", [](ClaimWorld& w, AntUnit& a) { a.hp = 1; w.impl.stun_or_die(a); }},
            {"blast_hit", [](ClaimWorld& w, AntUnit& a) { w.impl.blast_hit(a, TileCoord{2, 2}, TileCoord{3, 2}, 1, 0); }},
            {"drown", [](ClaimWorld& w, AntUnit& a) { w.impl.drown(a, TileCoord{2, 2}); }},
            {"bomb_victim", [](ClaimWorld& w, AntUnit& a) { w.impl.bomb_victim(a, TileCoord{2, 2}, TileCoord{3, 2}); }},
            {"attack_tile", [](ClaimWorld& w, AntUnit& a) { w.impl.attack_tile(a, TileCoord{3, 2}); }},
            {"attack_clip_end", [](ClaimWorld& w, AntUnit& a) { w.impl.attack_clip_end(a); }},
            {"stop_at", [](ClaimWorld& w, AntUnit& a) { w.impl.stop_at(a, TileCoord{2, 2}); }},
            {"stop_sync", [](ClaimWorld& w, AntUnit& a) { w.impl.stop_sync(a); }},
            {"enter_cant_go", [](ClaimWorld& w, AntUnit& a) { w.impl.enter_cant_go(a); }},
            {"set_action CantGo", [](ClaimWorld& w, AntUnit& a) { w.impl.set_action(a, AntUnit::kActionCantGo, 2, -1, -1, false); }},
            {"set_action Attack", [](ClaimWorld& w, AntUnit& a) { w.impl.set_action(a, AntUnit::kActionAttack, 2, -1, -1, false); }},
            {"path_complete of a walker that arrives", [](ClaimWorld& w, AntUnit& a) { SimulationEngineImpl::StepEvt e; w.impl.path_complete(a, e); }},
            {"path_complete of a walker that was heading for the ring of the hill", [](ClaimWorld& w, AntUnit& a) { a.home_state = 1; SimulationEngineImpl::StepEvt e; w.impl.path_complete(a, e); }},
            {"path_complete on a power-up", [](ClaimWorld& w, AntUnit& a) { w.impl.grid_.place_powerup(8, 8, 1); SimulationEngineImpl::StepEvt e; w.impl.path_complete(a, e); }},
            {"path_complete of a bomb order", [](ClaimWorld& w, AntUnit& a) { w.impl.set_order_kind(a, AntUnit::kOrderBomb); SimulationEngineImpl::StepEvt e; w.impl.path_complete(a, e); }},
            {"try_enter_tile blocked, not at the end of the path", [](ClaimWorld& w, AntUnit& a) { a.waypoints = {TileCoord{2, 2}, TileCoord{3, 2}, TileCoord{8, 8}}; a.current_waypoint_idx = 0; w.impl.try_enter_tile(a, TileCoord{3, 2}); }},
            {"try_enter_tile blocked at the end of the path", [](ClaimWorld& w, AntUnit& a) { a.waypoints = {TileCoord{2, 2}, TileCoord{3, 2}}; a.current_waypoint_idx = 0; w.impl.try_enter_tile(a, TileCoord{3, 2}); }},
            {"try_enter_tile blocked of an attack order", [](ClaimWorld& w, AntUnit& a) { w.impl.set_order_kind(a, AntUnit::kOrderAttack); a.waypoints = {TileCoord{2, 2}, TileCoord{3, 2}, TileCoord{8, 8}}; w.impl.try_enter_tile(a, TileCoord{3, 2}); }},
            {"apply_path", [](ClaimWorld& w, AntUnit& a) { w.impl.apply_path(a, std::vector<TileCoord>{TileCoord{2, 2}, TileCoord{3, 2}, TileCoord{9, 9}}); }},
            {"loco_sync of a walker stopped by another system", [](ClaimWorld& w, AntUnit& a) { a.loco_action = AntUnit::kActionWalk; a.state = UnitState::Idle; w.impl.loco_sync(a); }},
            {"loco_sync of a state of another system", [](ClaimWorld& w, AntUnit& a) { a.loco_action = AntUnit::kActionIdle; a.state = UnitState::Burn; w.impl.loco_sync(a); }},
            {"go_to the tile it stands on", [](ClaimWorld& w, AntUnit& a) { w.impl.go_to(a, TileCoord{2, 2}, false, false); }},
            {"go_to a far tile", [](ClaimWorld& w, AntUnit& a) { w.impl.go_to(a, TileCoord{10, 10}, false, false); }},
            {"go_to the entrance of the own hill", [](ClaimWorld& w, AntUnit& a) { w.impl.go_to(a, TileCoord{1, 1}, false, false); }},
            {"go_to a tile of an enemy hill", [](ClaimWorld& w, AntUnit& a) { w.impl.go_to(a, TileCoord{9, 1}, true, false); }},
            {"go_to the raid tile of an enemy hill (a thief)", [](ClaimWorld& w, AntUnit& a) { a.type = AntType::Thief; w.impl.go_to(a, TileCoord{9, 1}, true, false); }},
            {"go_to a tile with an ant of another team", [](ClaimWorld& w, AntUnit& a) { w.impl.go_to(a, TileCoord{3, 2}, true, false); }},
            {"go_to a tile with a team-mate (a special order)", [](ClaimWorld& w, AntUnit& a) { w.impl.go_to(a, TileCoord{3, 3}, true, true); }},
            {"go_to a bomb", [](ClaimWorld& w, AntUnit& a) { w.impl.grid_.place_bomb(6, 6, 1); w.impl.go_to(a, TileCoord{6, 6}, true, false); }},
            {"go_to food", [](ClaimWorld& w, AntUnit& a) { w.impl.go_to(a, TileCoord{7, 7}, true, false); }},
            {"go_to a power-up", [](ClaimWorld& w, AntUnit& a) { w.impl.grid_.place_powerup(6, 7, 1); w.impl.go_to(a, TileCoord{6, 7}, true, false); }},
        };
        uint64_t run = 0;
        for (const Case& c : cases) {
            for (int variant = 0; variant < 3; ++variant) {         // an ant that claims a tile, one that claims its own entrance with a Home order, and a removed one
                ClaimWorld w;
                w.impl.grid_.set_anthill(0, TileCoord{0, 0});
                w.impl.grid_.set_anthill(1, TileCoord{8, 0});
                AntUnit& a = w.spawn(0, 2, 2);
                AntUnit& b = w.spawn(0, 3, 3);                       // a team-mate that claims a tile too
                AntUnit& v = w.spawn(1, 3, 2);                       // another team beside it
                w.order(a, variant == 1 ? AntUnit::kOrderHome : AntUnit::kOrderMove, variant == 1 ? TileCoord{1, 1} : TileCoord{8, 8});
                w.order(b, AntUnit::kOrderMove, TileCoord{8, 8});
                w.order(v, AntUnit::kOrderMove, TileCoord{8, 8});
                if (variant == 2) w.impl.remove_ant(a);
                ASSERT_CLAIMS_OK(w, 7);
                const SimulationEngineImpl::OccTickScope in_tick(w.impl);
                c.call(w, a);
                const std::string why = claim_disagreements(w, static_cast<uint32_t>(run));
                if (!why.empty()) std::cout << "\n        " << c.name << " (variant " << variant << "): " << why << "\n";
                ASSERT_TRUE(why.empty());
                ++run;
            }
        }
    } TEST_END();

    TEST_CASE("C2.1 Claim lists at random: ants joining, set_position, orders of every kind to tiles on and off the grid, resets of the path, removals, orders through go_to, copies, restarts and grids of another size, writes by hand: after every operation the lists are the ants' orders and every question is the scan's") {
        uint32_t s = 20261027;
        auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
        ClaimWorld w;
        std::vector<SimulationEngineImpl> spares;
        const CountStale counting;
        const ClaimMarks marks;
        static const uint8_t kinds[] = {AntUnit::kOrderNone, AntUnit::kOrderMove, AntUnit::kOrderMove, AntUnit::kOrderHome, AntUnit::kOrderAttack, AntUnit::kOrderBomb, AntUnit::kOrderHarvest, 0x0C};
        uint64_t asked = 0;
        for (int step = 0; step < 4000; ++step) {
            const uint32_t op = below(100);
            const uint32_t pick = below(1000);
            const uint8_t kind = kinds[below(8)];
            const TileCoord tile{static_cast<int32_t>(below(16)) - 2, static_cast<int32_t>(below(16)) - 2};
            auto any_ant = [&]() -> AntUnit* { return w.impl.ants_.empty() ? nullptr : w.impl.ants_[pick % w.impl.ants_.size()].get(); };
            if (op < 10 && w.impl.ants_.size() < 90) {
                w.spawn(static_cast<uint8_t>(below(4)), static_cast<int32_t>(below(12)), static_cast<int32_t>(below(12)));
            } else if (op < 22) {
                if (AntUnit* a = any_ant()) { if (!a->removed) w.impl.set_position(*a, static_cast<int32_t>(below(12 * 32)), static_cast<int32_t>(below(12 * 32))); }
            } else if (op < 42) {
                if (AntUnit* a = any_ant()) w.impl.set_order_kind(*a, kind);
            } else if (op < 62) {
                if (AntUnit* a = any_ant()) w.impl.set_order_tile(*a, tile);
            } else if (op < 68) {
                if (AntUnit* a = any_ant()) w.impl.clear_path_of(*a);
            } else if (op < 73) {
                if (AntUnit* a = any_ant()) { if (!a->removed) w.impl.remove_ant(*a); }
            } else if (op < 82) {
                if (AntUnit* a = any_ant()) {                        // an order the way a player gives it: classify, adjust the goal (the claims are asked), queue the path
                    const SimulationEngineImpl::OccTickScope in_tick(w.impl);
                    if (!a->removed) w.impl.go_to(*a, TileCoord{tile.x + 2, tile.y + 2}, false, false);
                }
            } else if (op < 86) {
                spares.push_back(w.impl);
                spares.back().claims_verify();
            } else if (op < 89 && !spares.empty()) {
                w.impl = spares[pick % spares.size()];
            } else if (op < 90 && below(2) == 0) {
                w.reset(8 + below(7), 8 + below(7));
            } else if (op < 92 && below(2) == 0) {
                w.impl.grid_.init_empty(below(2) == 0 ? 13 : 12, 12);
                w.impl.occ_refresh();
            } else if (op < 96) {
                if (AntUnit* a = any_ant()) {                        // written by hand: the pass at the start of a tick puts it right
                    a->orig_order = kind;
                    a->orig_order_tile = tile;
                }
                w.impl.claims_catch_up_all();
            } else if (spares.size() > 5) {
                spares.erase(spares.begin());
            }
            const std::string why = claim_disagreements(w, static_cast<uint32_t>(step));
            if (!why.empty()) std::cout << "\n        step " << step << ": " << why << "\n";
            ASSERT_TRUE(why.empty());
            ++asked;
        }
        std::cout << "\n        " << asked << " operations, " << marks.calls_since() << " questions answered by the lists (" << marks.hits_since() << " yes), " << marks.checks_since() << " compared with the scan, " << marks.verifies_since() << " list checks   ... ";
        ASSERT_TRUE(marks.hits_since() > 2000 && marks.calls_since() - marks.hits_since() > 2000);   // both answers were met, often
    } TEST_END();

    TEST_CASE("C3.1 Orders written from outside between ticks, and commands given before the tick: the engine on the lists plays as the engine on the scans (the state hash equal right after each command and at every tick), the pass at the start of a tick puts the lists right, and nothing is put right when nothing is written") {
        Twin t;
        t.engine.init_test_world(60, 60, 9);
        const Crowd crowd = populate(t.engine, 60, 24, Layout::Spread);
        {
            LinearOnly scans;
            t.old_engine.init_test_world(60, 60, 9);
            populate(t.old_engine, 60, 24, Layout::Spread);
        }
        const CountStale counting;
        const ClaimMarks marks;
        uint32_t s = 0xC1A1u;
        auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
        static const uint8_t kinds[] = {AntUnit::kOrderMove, AntUnit::kOrderHome, AntUnit::kOrderNone, AntUnit::kOrderAttack};
        uint64_t pokes = 0;
        for (int round = 0; round < 16; ++round) {
            for (int k = 0; k < 6; ++k) ASSERT_TRUE(t.tick());
            const uint64_t before = marks.changes_since();
            for (int k = 0; k < 5; ++k) {
                const uint32_t id = crowd.ids[below(4)][below(24)];
                const uint8_t kind = kinds[below(4)];
                const TileCoord tile{static_cast<int32_t>(below(50)) + 4, static_cast<int32_t>(below(50)) + 4};
                t.both([&](SimulationEngine& e) { AntUnit& u = e.get_unit(id); u.orig_order = kind; u.orig_order_tile = tile; });
                ++pokes;
            }
            if (round % 2 == 0) {                                    // a command before the tick: outside a tick the claims are asked of the scan, so the pokes are seen as the old engine sees them
                Command c;
                c.type = CommandType::GroupMove;
                c.issuer = static_cast<uint8_t>(round % 4);
                c.tile_x = static_cast<int16_t>(20 + round);
                c.tile_y = 30;
                for (int k = 0; k < 6; ++k) c.ants.push_back(crowd.ids[static_cast<size_t>(round % 4)][static_cast<size_t>(k)]);
                ASSERT_TRUE(t.apply(c));
                ASSERT_TRUE(t.old_engine.state_hash() == t.engine.state_hash());
            }
            ASSERT_TRUE(t.tick());                                   // the pass puts the lists right; the hash is the scans'
            ASSERT_TRUE(marks.changes_since() > before);
        }
        const uint64_t after_pokes = marks.changes_since();
        for (int k = 0; k < 60; ++k) ASSERT_TRUE(t.tick());
        ASSERT_TRUE(marks.changes_since() == after_pokes);           // quiet ticks: nothing to put right
        std::cout << "\n        " << pokes << " orders written by hand, " << marks.changes_since() << " ants put right by the pass (both engines)   ... ";
    } TEST_END();

    TEST_CASE("C3.2 Crowds with every kind of order (orders across the map, stops, attacks, bombs, fire, bridges, raids, power-ups, hatching, deaths, a team leaving), engine copies in the middle: the lists answer, both answers are met, the hashes of the engine on the scans and of the engine on the lists are equal at every tick and the pass at the start of a tick puts nothing right") {
        for (const Layout layout : {Layout::Packed, Layout::Spread, Layout::Piled}) {
            const ClaimMarks marks;
            uint64_t checks = 0, pending = 0;
            const std::string why = play_crowd(90, layout == Layout::Spread ? 100 : 60, layout, 300u, &checks, &pending);
            std::cout << "\n        layout " << static_cast<int>(layout) << ": " << marks.calls_since() << " questions answered by the lists (" << marks.hits_since() << " yes), " << marks.checks_since() << " compared with the scan, "
                      << marks.verifies_since() << " list checks, " << marks.catch_ups_since() << " ants looked at by the start-of-tick pass   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(marks.calls_since() > 100 && marks.hits_since() > 0 && marks.calls_since() > marks.hits_since());
            ASSERT_TRUE(marks.checks_since() > 100 && marks.verifies_since() > 10);
            ASSERT_TRUE(marks.changes_since() == 0);                // no order was written without telling the lists
        }
    } TEST_END();

    TEST_CASE("C3.3 The six shipped maps, scripted players (orders of every kind, food carried home and the hill entered, fights, hatching), copies of the engine in the middle: the lists answer, both answers are met, the hashes of the engine on the scans and of the engine on the lists are equal at every tick and nothing is put right by the pass at the start of a tick") {
        uint32_t seed = 140;
        const std::string dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
        const uint32_t ticks = 900u;
        const ClaimMarks all_maps;
        for (const char* file : {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "ISLANDS.LVL", "GAUNTLET.LVL", "TREASURE.LVL"}) {
            const auto& level = library_map(dir, file);
            Twin t;
            {
                LinearOnly scans;
                t.old_engine.init(level, ++seed, 0x0F);
            }
            t.engine.init(level, seed, 0x0F);
            const ClaimMarks marks;
            const std::string why = play(t, &level, 0x0F, ticks, nullptr, nullptr);
            std::cout << "\n        " << file << ": " << t.ticks << " ticks, " << marks.calls_since() << " questions answered by the lists (" << marks.hits_since() << " yes), " << marks.checks_since() << " compared with the scan, "
                      << marks.verifies_since() << " list checks   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(marks.verifies_since() > 100 && marks.changes_since() == 0);
        }
        ASSERT_TRUE(all_maps.calls_since() > 300 && all_maps.hits_since() > 5 && all_maps.checks_since() == all_maps.calls_since());   // every question was compared (the maps start fewer than 2,048 ants)
    } TEST_END();
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [ENGINE CLAIMS SUITE] the claim lists equal the scan they replaced\n=======================================================\n";
    run_claim_tests();
    std::cout << "\n=======================================================\n"
              << " ENGINE CLAIMS TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
