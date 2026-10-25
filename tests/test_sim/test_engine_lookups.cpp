// The lookups that replaced linear scans in the simulation (SimulationEngineImpl::find_unit: the ant of id n sits in slot n - 1; PathManager::has_request_for: a flag per
// ant; occ_scan and the ants of a blast: the list of the ants on each tile, src/ants_sim/tile_ants.hpp) must give the scan's answer in every state, so that no match changes
// by a single bit (the lock-step state hash is the proof of that, docs/ARCHITECTURE.md).
//
// This program links ants_sim_shadow: the same sources as ants_sim, compiled with ANTS_SIM_LOOKUP_SHADOW (include/ants_sim/lookup_shadow.hpp). In that build every call of
// a lookup also runs the scan and stops the program when the two answers differ (every 64th call once there are more than 256 ants), every list is rebuilt from the ants and
// compared at the end of a tick (every 16th once big) and at every copy of an engine, and a thread can be switched to the scans alone, which is the simulation as it was
// before the lookups. Every match below is played by TWO engines at once, one with the scans alone (the old lookups) and one with the lookups and the check on every call,
// given the same commands (a scripted play: hatching, fights, every kind of order, Stop, bombs, fire, deaths, engine copies in the middle), and the state hash (all eight
// parts) is compared at EVERY tick.
//
//   ./test_engine_lookups          the quick tests (seconds): the lookups alone, the six shipped maps, big library maps, small packed and spread worlds
//   ./test_engine_lookups --big    4 x 700 and 4 x 1,500 ants (packed in the corners, spread over the map, piled on shared tiles), 600 ticks (400 for the 1,500s; minutes: the engine that scans is the old, slow one)
//   ./test_engine_lookups --all    both
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/lookup_shadow.hpp"
#include "ants_sim/path_planner.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#include "scripted_players.hpp"
#include "../../src/ants_sim/sim_engine_impl.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../common/test_assert.hpp"

#ifndef ANTS_SIM_LOOKUP_SHADOW
#error "test_engine_lookups links ants_sim_shadow: the simulation compiled with ANTS_SIM_LOOKUP_SHADOW"
#endif

using namespace ants::sim;

// An instrumented build (ASan / UBSan) runs five to ten times slower: the matches then play fewer ticks
#if defined(__SANITIZE_ADDRESS__)
#define ANTS_LOOKUP_TEST_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#define ANTS_LOOKUP_TEST_SANITIZED 1
#endif
#endif
#ifndef ANTS_LOOKUP_TEST_SANITIZED
#define ANTS_LOOKUP_TEST_SANITIZED 0
#endif

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

// Switches the calling thread to the scans alone while it lives
struct LinearOnly {
    LinearOnly() { lookup_shadow_linear_only() = true; }
    ~LinearOnly() { lookup_shadow_linear_only() = false; }
    LinearOnly(const LinearOnly&) = delete;
    LinearOnly& operator=(const LinearOnly&) = delete;
};

// What the shadow checks of the tile lists did since this was made: the questions of occ_scan and the blasts that were compared with their scans, the ants those blasts threw,
// and the lists that were rebuilt from the ants and compared (a test shows that it met piles of ants, not only that nothing differed)
struct Marks {
    uint64_t scans{lookup_shadow_scan_checks()}, blasts{lookup_shadow_blast_checks()}, blast_ants{lookup_shadow_blast_ants()}, lists{lookup_shadow_list_checks()};
    uint64_t scans_since() const { return lookup_shadow_scan_checks() - scans; }
    uint64_t blasts_since() const { return lookup_shadow_blast_checks() - blasts; }
    uint64_t blast_ants_since() const { return lookup_shadow_blast_ants() - blast_ants; }
    std::string since() const {
        return std::to_string(scans_since()) + " tile questions, " + std::to_string(blasts_since()) + " blasts of " + std::to_string(blast_ants_since()) + " ants, " +
               std::to_string(lookup_shadow_list_checks() - lists) + " list checks";
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------
// The lookups alone
// ---------------------------------------------------------------------------------------------------------------------------------

// What the queue holds, read from the queue itself (not from the scan that the shadow check compares with)
bool queued_in(const PathManager& m, uint32_t ant_id) {
    bool found = false;
    m.for_each_request([&](uint32_t id, TileCoord, TileCoord, bool, bool) { found = found || id == ant_id; });
    return found;
}

const uint32_t kProbeIds[] = {0, 1, 2, 3, 4, 5, 8, 13, 21, 34, 55, 89, 100, 1000, (1u << 20) - 1, 1u << 20, (1u << 20) + 5, (1u << 20) + 7, 0x7FFFFFFFu, 0xFFFFFFFEu, 0xFFFFFFFFu};

// The ids that the random operations request: a few small ones (they are queued again and again), and the ones around the limit of the flag array and the largest
const uint32_t kRequestIds[] = {1, 2, 3, 4, (1u << 20) - 1, 1u << 20, (1u << 20) + 5, 0xFFFFFFFFu, 0};

// The simple model of a path queue: the set of the ants that have a request (the queue holds at most one request of an ant, a second one replaces the first)
using IdModel = std::set<uint32_t>;

// The queue against the model: as many requests as ants in the model, no ant listed twice by for_each_request, and the same ants
std::string model_disagreements(const PathManager& m, const IdModel& model) {
    if (m.pending() != model.size()) return "pending() is " + std::to_string(m.pending()) + ", the model holds " + std::to_string(model.size()) + " ants";
    IdModel listed;
    std::string dup;
    m.for_each_request([&](uint32_t id, TileCoord, TileCoord, bool, bool) {
        if (!listed.insert(id).second && dup.empty()) dup = "for_each_request lists ant " + std::to_string(id) + " twice";
    });
    if (!dup.empty()) return dup;
    if (listed != model) return "the ants of the queue are not the ants of the model";
    return "";
}

// Every id that the tests use, and the ones around the limits, against what the queue holds
std::string queue_disagreements(const PathManager& m) {
    for (uint32_t id : kProbeIds) {
        if (m.has_request_for(id) != queued_in(m, id)) return "has_request_for(" + std::to_string(id) + ") says " + (m.has_request_for(id) ? "queued" : "not queued") + ", the queue says the opposite";
    }
    return "";
}

#define ASSERT_QUEUE_OK(mgr)                                                                                 \
    do {                                                                                                     \
        const std::string why_ = queue_disagreements(mgr);                                                   \
        ASSERT_TRUE(why_.empty());                                                                           \
        if (!why_.empty()) std::cout << "\n        " << why_ << "\n";                                        \
    } while (0)

#define ASSERT_MODEL_OK(mgr, model)                                                                          \
    do {                                                                                                     \
        const std::string why_ = model_disagreements(mgr, model);                                            \
        ASSERT_TRUE(why_.empty());                                                                           \
        if (!why_.empty()) std::cout << "\n        " << why_ << "\n";                                        \
    } while (0)

// ---------------------------------------------------------------------------------------------------------------------------------
// The tile lists alone
// ---------------------------------------------------------------------------------------------------------------------------------

// A world of 8 x 8 tiles with no engine around it: ants are put on tiles by hand (also off the grid), and the questions of the tile lists are asked of it directly
struct TileWorld {
    SimulationEngineImpl impl;
    TileWorld(uint32_t w = 8, uint32_t h = 8) {
        impl.grid_.init_empty(w, h);
        impl.movement_reset();
    }
    // An ant of team `player` (any number: 4 and more are in no team of the scan) in the table, registered on the tile (tx, ty), which may be off the grid
    AntUnit& add(uint8_t player, int32_t tx, int32_t ty, uint32_t id = 0) {
        const uint32_t use_id = id != 0 ? id : static_cast<uint32_t>(impl.ants_.size()) + 1u;
        auto u = std::make_unique<AntUnit>(use_id, static_cast<TeamId>(player & 3u), AntType::Worker, 0, 0);
        u->player_id = player;
        impl.ants_.push_back(std::move(u));
        AntUnit& a = *impl.ants_.back();
        put(a, tx, ty);
        return a;
    }
    void put(AntUnit& a, int32_t tx, int32_t ty) {
        a.set_pixel_pos(tx * 32 + 16, ty * 32 + 16);
        impl.occ_move(a, TileCoord{tx, ty});
    }
    // "No path and a stationary action" (the scan's rule for the default occupant) or the opposite
    static void make_still(AntUnit& a, bool still) {
        a.waypoints.clear();
        if (!still) a.waypoints.push_back(TileCoord{1, 1});
    }
};

// Every tile of the grid and the ring around it (off the grid: no list, the scan answers), asked with no excluded ant and with each of a few: occ_scan and the ants of a blast
// against their scans, and the lists against the ants. Returns "" or what differs.
std::string tile_disagreements(SimulationEngineImpl& impl, uint32_t salt) {
    std::vector<AntUnit*> excluded{nullptr};
    for (size_t i = salt % 7u; i < impl.ants_.size() && excluded.size() < 4; i += 1 + salt % 5u) excluded.push_back(impl.ants_[i].get());
    for (int32_t y = -2; y <= static_cast<int32_t>(impl.grid_.height()) + 1; ++y) {
        for (int32_t x = -2; x <= static_cast<int32_t>(impl.grid_.width()) + 1; ++x) {
            const TileCoord t{x, y};
            for (const AntUnit* ex : excluded) {
                uint16_t n_list = 99, n_scan = 98;
                AntUnit* const by_list = impl.occ_scan(t, ex, &n_list);
                AntUnit* const by_scan = impl.occ_scan_scan(t, ex, &n_scan);
                if (by_list != by_scan || n_list != n_scan) return "occ_scan differs on the tile (" + std::to_string(x) + ", " + std::to_string(y) + ")";
            }
            std::vector<AntUnit*> victims, scanned;
            impl.blast_victims(t, victims);
            impl.blast_victims_scan(t, scanned);
            if (victims != scanned) return "the ants of a blast differ on the tile (" + std::to_string(x) + ", " + std::to_string(y) + ")";
        }
    }
    impl.tile_ants_verify();
    return "";
}

#define ASSERT_TILES_OK(impl, salt)                                                                          \
    do {                                                                                                     \
        const std::string why_ = tile_disagreements(impl, salt);                                             \
        ASSERT_TRUE(why_.empty());                                                                           \
        if (!why_.empty()) std::cout << "\n        " << why_ << "\n";                                        \
    } while (0)

void run_lookup_tests() {
    TEST_CASE("L1.1 PathManager: has_request_for equals the queue, and the queue a simple model, after every request, run, replacement, clear, copy, assignment and move (random operations, ids of every kind)") {
        const uint16_t rows = 60, cols = 100;
        const PathManager::CostProvider cost = [](uint32_t, TileCoord from, TileCoord to) { return (from.x != to.x && from.y != to.y) ? 28u : 20u; };
        uint32_t s = 12345;
        auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
        struct Spare {
            PathManager mgr;
            IdModel model;
        };
        PathManager mgr;
        IdModel model;
        std::vector<Spare> spares;
        for (int step = 0; step < 4000; ++step) {
            const uint32_t op = below(100);
            if (op < 40) {                                           // a request: a fresh ant, or a second request of one that is queued (it replaces the first)
                const uint32_t id = kRequestIds[below(sizeof(kRequestIds) / sizeof(kRequestIds[0]))];
                mgr.request(id, TileCoord{static_cast<int32_t>(below(40)), static_cast<int32_t>(below(50))}, TileCoord{static_cast<int32_t>(60 + below(35)), static_cast<int32_t>(below(50))}, rows, cols);
                model.insert(id);
            } else if (op < 85) {
                for (uint32_t k = 0, n = 1 + below(3); k < n; ++k) {
                    const auto delivery = mgr.run(cost, static_cast<uint16_t>(5 + below(60)));
                    if (delivery) {
                        ASSERT_TRUE(model.erase(delivery->ant_id) == 1);   // a delivered ant had a request, and has none now
                    }
                }
            } else if (op < 88) {
                mgr.clear();
                model.clear();
            } else if (op < 92) {
                spares.push_back(Spare{mgr, model});                 // copy construction
                ASSERT_QUEUE_OK(spares.back().mgr);
                ASSERT_MODEL_OK(spares.back().mgr, spares.back().model);
            } else if (op < 95 && !spares.empty()) {
                const Spare& from = spares[below(static_cast<uint32_t>(spares.size()))];
                mgr = from.mgr;                                      // copy assignment (over a manager with requests of its own)
                model = from.model;
            } else if (op < 97) {
                PathManager moved(std::move(mgr));                   // move construction: the target holds the requests, the source none
                ASSERT_QUEUE_OK(moved);
                ASSERT_MODEL_OK(moved, model);
                ASSERT_QUEUE_OK(mgr);
                ASSERT_TRUE(mgr.pending() == 0);
                mgr = std::move(moved);                              // and back (move assignment)
                ASSERT_QUEUE_OK(moved);
                ASSERT_TRUE(moved.pending() == 0);
            } else if (spares.size() > 8) {
                spares.erase(spares.begin());
            }
            ASSERT_QUEUE_OK(mgr);
            ASSERT_MODEL_OK(mgr, model);
        }
        for (const Spare& sp : spares) {
            ASSERT_QUEUE_OK(sp.mgr);
            ASSERT_MODEL_OK(sp.mgr, sp.model);
        }
        PathManager gone_from;
        PathManager taker(std::move(gone_from));
        gone_from.request(7, TileCoord{1, 1}, TileCoord{5, 5}, rows, cols);   // a moved-from manager works again
        ASSERT_TRUE(gone_from.has_request_for(7) && !gone_from.has_request_for(8));
        ASSERT_QUEUE_OK(gone_from);
        ASSERT_QUEUE_OK(taker);
    } TEST_END();

    TEST_CASE("L1.1b PathManager: a copy of a moved-from manager (construction and assignment) is empty, and the manager that it was assigned to holds no flag of its old requests") {
        const uint16_t rows = 60, cols = 100;
        PathManager a;
        a.request(5, TileCoord{1, 1}, TileCoord{30, 30}, rows, cols);
        a.request(6, TileCoord{2, 2}, TileCoord{31, 31}, rows, cols);
        a.request((1u << 20) + 5, TileCoord{3, 3}, TileCoord{32, 32}, rows, cols);   // (an id without a flag byte: counted, found by a scan)
        PathManager src;
        PathManager sink(std::move(src));                           // src is moved-from: no pool, no searches
        a = src;                                                    // copy assignment from it
        ASSERT_TRUE(a.pending() == 0);
        ASSERT_TRUE(!a.has_request_for(5) && !a.has_request_for(6) && !a.has_request_for((1u << 20) + 5));
        ASSERT_QUEUE_OK(a);
        PathManager c(src);                                         // copy construction from it
        ASSERT_TRUE(c.pending() == 0 && !c.has_request_for(5));
        ASSERT_QUEUE_OK(c);
        a.request(5, TileCoord{1, 1}, TileCoord{30, 30}, rows, cols);   // and both work again
        c = a;
        ASSERT_TRUE(a.pending() == 1 && c.pending() == 1 && a.has_request_for(5) && c.has_request_for(5) && !c.has_request_for(6));
        ASSERT_QUEUE_OK(a);
        ASSERT_QUEUE_OK(c);
    } TEST_END();

    TEST_CASE("L1.2 find_unit: the slot answers for a table that is dense, and the scan for one that is not (holes, other ids, a moved-from slot, an id that was never handed out)") {
        SimulationEngineImpl impl;
        auto add = [&impl](uint32_t id) { impl.ants_.push_back(std::make_unique<AntUnit>(id, TeamId::Black, AntType::Worker, 1, 1)); };
        auto probe = [&impl]() {
            for (uint32_t id = 0; id < 14; ++id) {
                if (impl.find_unit(id) != impl.find_unit_scan(id)) return false;
            }
            return impl.find_unit(0xFFFFFFFFu) == nullptr;
        };
        ASSERT_TRUE(probe());                                        // an empty table
        for (uint32_t id = 1; id <= 6; ++id) add(id);                // dense, as the engine makes it
        ASSERT_TRUE(probe());
        ASSERT_TRUE(impl.find_unit(4) == impl.ants_[3].get() && impl.find_unit(7) == nullptr && impl.find_unit(0) == nullptr);
        impl.ants_.push_back(AntPtr());                              // a hole
        ASSERT_TRUE(probe());
        add(12);                                                     // an id that does not match its slot
        ASSERT_TRUE(probe());
        add(9);
        add(3);                                                      // a duplicate of an id: the scan finds the first one, so must the lookup
        ASSERT_TRUE(probe());
        ASSERT_TRUE(impl.find_unit(3) == impl.ants_[2].get());
        std::swap(impl.ants_[0], impl.ants_[4]);                     // the table reordered
        ASSERT_TRUE(probe());
        impl.ants_.erase(impl.ants_.begin() + 1);                    // an ant erased: every slot after it is out of step
        ASSERT_TRUE(probe());
        impl.ants_.clear();
        ASSERT_TRUE(probe());
    } TEST_END();

    TEST_CASE("L1.3 Tile lists: occ_scan visits the ants of a tile by team, then by place in the table, and the ants of a blast by place in the table (answers worked out by hand)") {
        TileWorld w;
        AntUnit& s0 = w.add(3, 2, 2);          // slot 0: team 3, standing still
        AntUnit& s1 = w.add(0, 2, 2);          // slot 1: team 0, walking
        AntUnit& s2 = w.add(1, 2, 2);          // slot 2: team 1, standing still
        AntUnit& s3 = w.add(0, 2, 2);          // slot 3: team 0, standing still
        AntUnit& s4 = w.add(2, 2, 2);          // slot 4: team 2, walking
        AntUnit& s5 = w.add(5, 2, 2);          // slot 5: a seat that is in none of the four teams: the scan never counts it
        AntUnit& s6 = w.add(0, 3, 3);          // slot 6: alone elsewhere
        TileWorld::make_still(s0, true);
        TileWorld::make_still(s1, false);
        TileWorld::make_still(s2, true);
        TileWorld::make_still(s3, true);
        TileWorld::make_still(s4, false);
        TileWorld::make_still(s5, true);
        TileWorld::make_still(s6, true);
        uint16_t n = 0;
        // order of visit: team 0: slot 1 (first), slot 3 (still: best); team 1: slot 2 (still: best); team 2: slot 4 (walking); team 3: slot 0 (still: best): the answer is slot 0
        ASSERT_TRUE(w.impl.occ_scan(TileCoord{2, 2}, nullptr, &n) == &s0 && n == 5);
        ASSERT_TRUE(w.impl.occ_scan(TileCoord{2, 2}, &s0, &n) == &s2 && n == 4);       // without it: team 1's slot 2 is the last that stands still
        ASSERT_TRUE(w.impl.occ_scan(TileCoord{2, 2}, &s2, &n) == &s0 && n == 4);
        TileWorld::make_still(s0, false);
        TileWorld::make_still(s2, false);
        ASSERT_TRUE(w.impl.occ_scan(TileCoord{2, 2}, nullptr, &n) == &s3 && n == 5);   // only slot 3 stands still
        TileWorld::make_still(s3, false);
        ASSERT_TRUE(w.impl.occ_scan(TileCoord{2, 2}, nullptr, &n) == &s1 && n == 5);   // none does: the first of the visit, team 0's slot 1 (not the first of the table)
        TileWorld::make_still(s1, true);
        ASSERT_TRUE(w.impl.occ_scan(TileCoord{2, 2}, nullptr, &n) == &s1 && n == 5);   // the first one is the only one that does: it stays
        ASSERT_TRUE(w.impl.occ_scan(TileCoord{3, 3}, &s6, &n) == nullptr && n == 0);
        ASSERT_TRUE(w.impl.occ_scan(TileCoord{3, 3}, nullptr, &n) == &s6 && n == 1);
        ASSERT_TRUE(w.impl.occ_scan(TileCoord{4, 4}, nullptr, &n) == nullptr && n == 0);
        // the blast takes every ant that is not removed, whatever its team, in the order of the table
        std::vector<AntUnit*> victims;
        w.impl.blast_victims(TileCoord{2, 2}, victims);
        ASSERT_TRUE((victims == std::vector<AntUnit*>{&s0, &s1, &s2, &s3, &s4, &s5}));
        s2.removed = true;                                           // (a removed ant that is still registered is not thrown; the scan still counts it)
        w.impl.blast_victims(TileCoord{2, 2}, victims);
        ASSERT_TRUE((victims == std::vector<AntUnit*>{&s0, &s1, &s3, &s4, &s5}));
        ASSERT_TRUE(w.impl.occ_scan(TileCoord{2, 2}, nullptr, &n) != nullptr && n == 5);
        w.put(s1, 5, 5);                                             // the slot of the first one leaves, arrives again: the order is the table's, not the arrival's
        w.put(s1, 2, 2);
        w.impl.blast_victims(TileCoord{2, 2}, victims);
        ASSERT_TRUE((victims == std::vector<AntUnit*>{&s0, &s1, &s3, &s4, &s5}));
        w.impl.blast_victims(TileCoord{6, 6}, victims);
        ASSERT_TRUE(victims.empty());
        AntUnit& twin_id = w.add(2, 6, 6, 2);                        // an ant that has the id of slot 1: its place in the table is found by looking, not by its id
        w.impl.blast_victims(TileCoord{6, 6}, victims);
        ASSERT_TRUE((victims == std::vector<AntUnit*>{&twin_id}));
        w.impl.blast_victims(TileCoord{2, 2}, victims);
        ASSERT_TRUE((victims == std::vector<AntUnit*>{&s0, &s1, &s3, &s4, &s5}));
        ASSERT_TILES_OK(w.impl, 1);
        for (uint32_t i = 0; i < 300; ++i) {                          // a pile of 300 ants of every team on one tile, some standing, some walking
            AntUnit& a = w.add(static_cast<uint8_t>(i * 7u % 4u), 4, 4);
            TileWorld::make_still(a, i % 3u == 0);
        }
        uint16_t pile = 0;
        w.impl.occ_scan(TileCoord{4, 4}, nullptr, &pile);
        ASSERT_TRUE(pile == 300);
        w.impl.blast_victims(TileCoord{4, 4}, victims);
        ASSERT_TRUE(victims.size() == 300 && std::is_sorted(victims.begin(), victims.end(), [&w](const AntUnit* x, const AntUnit* y) { return x->id < y->id; }));
        ASSERT_TILES_OK(w.impl, 2);
    } TEST_END();

    TEST_CASE("L1.4 Tile lists: every list equals the ants' occ_tile and every question its scan, after every arrival, move (also off the grid), death, change of path or action, refresh, copy, assignment, move, restart and grid of another size (random operations, piles of ants, a table that is not dense)") {
        uint32_t s = 20260125;
        auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
        TileWorld w;
        std::vector<SimulationEngineImpl> spares;
        auto any_ant = [&]() -> AntUnit* { return w.impl.ants_.empty() ? nullptr : w.impl.ants_[below(static_cast<uint32_t>(w.impl.ants_.size()))].get(); };
        auto tile_range = [&](int32_t& x, int32_t& y) {                // mostly on the grid, now and then off it (also with a negative row and an x that is not)
            x = static_cast<int32_t>(below(12)) - 2;
            y = static_cast<int32_t>(below(12)) - 2;
            if (below(3) != 0) { x = static_cast<int32_t>(below(4)) + 2; y = static_cast<int32_t>(below(4)) + 2; }   // (a small square, so that ants pile up)
        };
        const UnitState states[] = {UnitState::Idle, UnitState::Walking, UnitState::Attacking, UnitState::PlantingBomb, UnitState::Stunned, UnitState::CantGo};
        const uint8_t actions[] = {AntUnit::kActionNone, AntUnit::kActionNone, 0, 1, 5, 0x12};
        uint64_t big_piles = 0;
        for (int step = 0; step < 3000; ++step) {
            const uint32_t op = below(100);
            int32_t x = 0, y = 0;
            if (op < 14 && w.impl.ants_.size() < 90) {
                tile_range(x, y);
                const uint32_t odd_id = below(15) == 0 ? 1u + below(40) : 0u;   // now and then an id that is not its place (the id of another ant)
                w.add(static_cast<uint8_t>(below(9) == 0 ? 4 + below(4) : below(4)), x, y, odd_id);
            } else if (op < 50) {
                if (AntUnit* a = any_ant()) { tile_range(x, y); w.put(*a, x, y); }
            } else if (op < 54) {
                if (AntUnit* a = any_ant()) {                          // death: removed, then unregistered by the refresh (or at once by the move that death makes)
                    a->removed = true;
                    a->hp = 0;
                    a->state = UnitState::Dead;
                    if (below(2) == 0) w.impl.occ_refresh(); else w.impl.occ_move(*a, TileCoord{-1, -1});
                }
            } else if (op < 66) {
                if (AntUnit* a = any_ant()) TileWorld::make_still(*a, below(2) == 0);
            } else if (op < 74) {
                if (AntUnit* a = any_ant()) {
                    a->loco_action = actions[below(6)];
                    a->state = states[below(6)];
                }
            } else if (op < 80) {
                if (AntUnit* a = any_ant()) {
                    a->set_pixel_pos(static_cast<int32_t>(below(300)), static_cast<int32_t>(below(300)));   // moved by something that does not register it (a teleport): the refresh does
                }
                w.impl.occ_refresh();
            } else if (op < 85) {
                spares.push_back(w.impl);                              // copy construction
                ASSERT_TILES_OK(spares.back(), static_cast<uint32_t>(step));
            } else if (op < 89 && !spares.empty()) {
                w.impl = spares[below(static_cast<uint32_t>(spares.size()))];   // copy assignment over an engine with ants of its own
            } else if (op < 91) {
                SimulationEngineImpl moved(std::move(w.impl));        // move, and back
                w.impl = std::move(moved);
            } else if (op < 92 && below(2) == 0) {
                w.impl.ants_.clear();                                  // a new match (init): no ants, the lists and the grid start again
                w.impl.movement_reset();
            } else if (op < 94 && below(2) == 0) {
                w.impl.grid_.init_empty(below(2) == 0 ? 9 : 8, below(2) == 0 ? 7 : 8);   // another size: the next refresh starts the lists again, every ant unregistered first
                w.impl.occ_refresh();
            } else if (spares.size() > 6) {
                spares.erase(spares.begin());
            }
            ASSERT_TILES_OK(w.impl, static_cast<uint32_t>(step));
            for (uint32_t cy = 0; cy < w.impl.grid_.height(); ++cy) {
                for (uint32_t cx = 0; cx < w.impl.grid_.width(); ++cx) {
                    uint16_t n = 0;
                    w.impl.occ_scan(TileCoord{static_cast<int32_t>(cx), static_cast<int32_t>(cy)}, nullptr, &n);
                    if (n >= 4) ++big_piles;
                }
            }
        }
        for (const SimulationEngineImpl& sp : spares) {
            SimulationEngineImpl copy(sp);
            ASSERT_TILES_OK(copy, 5);
        }
        std::cout << "\n        " << big_piles << " tiles with 4 ants or more seen   ... ";
        ASSERT_TRUE(big_piles > 100);                                  // the test did meet piles of four ants and more
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Two engines, one match
// ---------------------------------------------------------------------------------------------------------------------------------

struct Twin {
    SimulationEngine old_engine;   // the scans alone
    SimulationEngine engine;       // the lookups, checked against the scans at every call
    uint64_t ticks{0};
    uint64_t pending_seen{0};      // ants with a queued path request at the sweeps (the flags had something to say)
    std::string failure;

    std::string describe(const StateHash& a, const StateHash& b) const {
        std::string s = "tick " + std::to_string(ticks) + ": the state hashes differ, in";
        const char* names[] = {"engine", "players", "grid", "food", "ants", "paths", "droppers"};
        const uint64_t av[] = {a.engine, a.players, a.grid, a.food, a.ants, a.paths, a.droppers};
        const uint64_t bv[] = {b.engine, b.players, b.grid, b.food, b.ants, b.paths, b.droppers};
        for (int i = 0; i < 7; ++i) {
            if (av[i] != bv[i]) s += std::string(" ") + names[i];
        }
        return s;
    }

    // A change that is not a command (a test hook: fire, bomb, kill, hatch, ...): made in both engines, in the old engine by the scans alone
    void both(const std::function<void(SimulationEngine&)>& change) {
        {
            LinearOnly scans;
            change(old_engine);
        }
        change(engine);
    }

    bool apply(const Command& c) {
        CommandResult ro;
        {
            LinearOnly scans;
            ro = old_engine.apply_command(c);
        }
        const CommandResult rn = engine.apply_command(c);
        if (ro.status != rn.status || ro.ack_ant != rn.ack_ant || ro.needing_order != rn.needing_order || ro.ants_ordered != rn.ants_ordered) {
            failure = "tick " + std::to_string(ticks) + ": a command is answered differently";
            return false;
        }
        return true;
    }

    // Every id that was ever handed out and a few that were not: the ants and the path queues (the checks run inside, the engines are compared here)
    bool sweep() {
        const uint64_t fallbacks = lookup_shadow_fallbacks();   // (the ids that were never handed out fall back on purpose; they are not counted)
        const bool ok = sweep_ids();
        lookup_shadow_fallbacks() = fallbacks;
        return ok;
    }
    bool sweep_ids() {
        const uint32_t last = static_cast<uint32_t>(engine.get_world_state().ants.size()) + 3u;
        for (uint32_t id = 0; id <= last; ++id) {
            bool pending_old, base_old;
            {
                LinearOnly scans;
                pending_old = old_engine.has_pending_path(id);
                base_old = old_engine.is_ant_in_base_queue(id);
            }
            if (pending_old != engine.has_pending_path(id) || base_old != engine.is_ant_in_base_queue(id)) {
                failure = "tick " + std::to_string(ticks) + ": the engines answer differently for ant " + std::to_string(id);
                return false;
            }
            if (pending_old) ++pending_seen;
        }
        return true;
    }

    bool tick() {
        {
            LinearOnly scans;
            old_engine.tick();
        }
        engine.tick();
        ++ticks;
        const StateHash a = old_engine.state_hash();
        const StateHash b = engine.state_hash();
        if (a != b || a.engine != b.engine || a.players != b.players || a.grid != b.grid || a.food != b.food || a.ants != b.ants || a.paths != b.paths || a.droppers != b.droppers) {
            failure = describe(a, b);
            return false;
        }
        return true;
    }
};

// What a match is given besides the scripted players: the commands of a tick, from the match's own state
using Script = std::function<void(Twin&, uint32_t tick, std::vector<Command>& out)>;

// Plays `ticks` ticks. The scripted players of the map sweep (tests/common/scripted_players.hpp) give their orders once a second, `extra` adds orders of the test; the engine is
// copied in the middle (a copy-constructed one, then an assigned one) because a copy has to rebuild the flags of its path queue. Returns "" or what went wrong.
std::string play(Twin& t, const ants::assets::LevelData* level, uint8_t roster, uint32_t ticks, const Script& extra, uint64_t* checks_out = nullptr) {
    std::unique_ptr<ants_test::ScriptedPlayers> players;
    if (level != nullptr) players = std::make_unique<ants_test::ScriptedPlayers>(*level, roster, 0x5EEDu, true);
    const uint64_t checks0 = lookup_shadow_checks();
    const uint64_t fallbacks0 = lookup_shadow_fallbacks();
    std::vector<Command> cmds;
    for (uint32_t i = 0; i < ticks; ++i) {
        cmds.clear();
        if (players && i % 20 == 0) {
            players->orders(t.engine, cmds);                           // (the engines are equal, so one tells what both see; it tops up the scores of the engine it is given)
            for (uint8_t p = 0; p < MAX_PLAYERS; ++p) t.old_engine.set_player_score(p, t.engine.get_player_score(p));
        }
        if (extra) extra(t, i, cmds);
        for (const Command& c : cmds) {
            if (!t.apply(c)) return t.failure;
        }
        if (!t.tick()) return t.failure;
        if (i % 20 == 0 && !t.sweep()) return t.failure;
        if (i == ticks / 3) t.engine = SimulationEngine(t.engine);                 // copy construction, then move
        if (i == 2 * ticks / 3) {
            SimulationEngine other(t.engine);
            t.engine = other;                                                      // copy assignment
            t.old_engine = SimulationEngine(t.old_engine);
        }
    }
    if (checks_out) *checks_out = lookup_shadow_checks() - checks0;
    if (lookup_shadow_fallbacks() != fallbacks0) return "find_unit fell back to the scan " + std::to_string(lookup_shadow_fallbacks() - fallbacks0) + " times: the slot of the id should always answer";
    return "";
}

const ants::assets::LevelData& library_map(const std::string& dir, const char* file) {
    static std::vector<std::pair<std::string, ants::assets::LevelData>> cache;
    const std::string path = dir + "/" + std::string(file);
    for (auto& e : cache) {
        if (e.first == path) return e.second;
    }
    ants::assets::LevelData lvl;
    if (!ants::assets::LVLParser::load_from_file(path, lvl, nullptr, ants::assets::LevelLimits::for_extended_maps())) {
        std::cout << "cannot load the map " << path << "\n";
        std::exit(2);
    }
    cache.emplace_back(path, std::move(lvl));
    return cache.back().second;
}

void run_map_tests() {
    const uint32_t ticks = ANTS_LOOKUP_TEST_SANITIZED ? 200u : 600u;
    TEST_CASE("L2.1 The Six Shipped Maps, scripted players, the state hash of the scan engine and of the lookup engine equal at every tick") {
        uint32_t seed = 40;
        const std::string dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
        for (const char* file : {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "ISLANDS.LVL", "GAUNTLET.LVL", "TREASURE.LVL"}) {
            const auto& level = library_map(dir, file);
            Twin t;
            t.old_engine.init(level, ++seed, 0x0F);
            t.engine.init(level, seed, 0x0F);
            uint64_t checks = 0;
            const Marks marks;
            const std::string why = play(t, &level, 0x0F, ticks, nullptr, &checks);
            std::cout << "\n        " << file << ": " << t.ticks << " ticks, " << checks << " lookups checked, " << t.pending_seen << " queued requests seen, " << marks.since() << "   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(checks > 1000);
        }
    } TEST_END();

    TEST_CASE("L2.2 Big Maps Of The Library (read only), the same, 600 ticks: many ants, fights, long searches") {
        uint32_t seed = 70;
        const std::string dir = std::string(ANTS_SOURCE_DIR) + "/Community-Maps";
        for (const char* file : {"WAR OF ALL ants.lvl", "GrassHopper.lvl", "ARMIES OF FATE.lvl", "AnTsCaStLe.lvl", "Jungle.lvl", "OCEAN.LVL", "4cornersn3.lvl"}) {
            const auto& level = library_map(dir, file);
            Twin t;
            t.old_engine.init(level, ++seed, 0x0F);
            t.engine.init(level, seed, 0x0F);
            uint64_t checks = 0;
            const Marks marks;
            const std::string why = play(t, &level, 0x0F, ticks, nullptr, &checks);
            std::cout << "\n        " << file << ": " << t.ticks << " ticks, " << checks << " lookups checked, " << t.pending_seen << " queued requests seen, " << marks.since() << "   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(checks > 1000);
        }
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Worlds with many ants, built here: `per_team` ants of each of the four teams
// ---------------------------------------------------------------------------------------------------------------------------------

enum class Layout { Packed, Spread, Piled };

struct Crowd {
    std::vector<std::vector<uint32_t>> ids{4};        // the ants of each team, in the order they were made
    std::vector<std::vector<uint32_t>> special{4};    // the typed ants that add_hazards made (bombers, fire ants, thieves, combat ants, swimmers), also in order
    uint32_t side{0};
};

// Packed: a solid square of ants in each corner of a width x width world. Spread: the four teams on distinct tiles over the whole middle of the map, mixed together (the
// start markers of a big player's map are like this: every ant has enemies next door).
Crowd populate(SimulationEngine& e, uint32_t width, uint32_t per_team, Layout layout) {
    Crowd crowd;
    uint32_t s = 4242;
    auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
    if (layout == Layout::Packed) {
        uint32_t side = 1;
        while (side * side < per_team) ++side;
        crowd.side = side;
        for (uint8_t p = 0; p < 4; ++p) {
            const int32_t ox = (p & 1) != 0 ? static_cast<int32_t>(width - 2 - side) : 2;
            const int32_t oy = (p & 2) != 0 ? static_cast<int32_t>(width - 2 - side) : 2;
            uint32_t n = 0;
            for (uint32_t y = 0; y < side && n < per_team; ++y) {
                for (uint32_t x = 0; x < side && n < per_team; ++x, ++n) {
                    crowd.ids[p].push_back(e.spawn_unit(p, AntType::Worker, TileCoord{ox + static_cast<int32_t>(x), oy + static_cast<int32_t>(y)}, true));
                }
            }
        }
    } else if (layout == Layout::Piled) {
        // piles: six ants of every team on the same tile, the tiles two apart over the middle of the map (the ants of a pile depart one by one, each from a tile that holds several)
        const uint32_t lo = 12, cols = (width - 24) / 2;
        for (uint8_t p = 0; p < 4; ++p) {
            for (uint32_t i = 0; i < per_team; ++i) {
                const uint32_t k = i / 6;
                crowd.ids[p].push_back(e.spawn_unit(p, AntType::Worker, TileCoord{static_cast<int32_t>(lo + 2 * (k % cols)), static_cast<int32_t>(lo + 2 * (k / cols))}, true));
            }
        }
    } else {
        const uint32_t lo = 12, span = width - 24;
        std::vector<uint8_t> used(static_cast<size_t>(width) * width, 0);
        for (uint8_t p = 0; p < 4; ++p) {
            for (uint32_t i = 0; i < per_team; ++i) {
                for (;;) {
                    const uint32_t x = lo + below(span);
                    const uint32_t y = lo + below(span);
                    if (used[static_cast<size_t>(y) * width + x] != 0) continue;
                    used[static_cast<size_t>(y) * width + x] = 1;
                    crowd.ids[p].push_back(e.spawn_unit(p, AntType::Worker, TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)}, true));
                    break;
                }
            }
        }
    }
    return crowd;
}

// Orders of a crowd, in groups of 32 ants (the most one command takes): all ants of every team across the world at tick 3, the first half of every team stopped at tick 100 (a
// request that is queued is replaced by the stop or by the next order), all of them sent to another tile at tick 130, and attack orders at the middle of the map at tick 260
Script crowd_script(const Crowd& crowd, uint32_t width) {
    return [crowd, width](Twin&, uint32_t tick, std::vector<Command>& out) {
        const int16_t far = static_cast<int16_t>(width - 5);
        auto groups = [&](uint8_t p, size_t from, size_t to, CommandType type, int16_t x, int16_t y) {
            const std::vector<uint32_t>& ids = crowd.ids[p];
            for (size_t i = from; i < to && i < ids.size(); i += kMaxCommandAnts) {
                Command c;
                c.type = type;
                c.issuer = p;
                c.tile_x = x;
                c.tile_y = y;
                for (size_t j = i; j < std::min({ids.size(), to, i + kMaxCommandAnts}); ++j) c.ants.push_back(ids[j]);
                out.push_back(c);
            }
        };
        for (uint8_t p = 0; p < 4; ++p) {
            const size_t n = crowd.ids[p].size();
            const int16_t ox = (p & 1) != 0 ? 4 : far;
            const int16_t oy = (p & 2) != 0 ? 4 : far;
            if (tick == 3) groups(p, 0, n, CommandType::GroupMove, ox, oy);
            if (tick == 100) groups(p, 0, n / 2, CommandType::Stop, 0, 0);
            if (tick == 130) groups(p, 0, n, CommandType::GroupMove, static_cast<int16_t>(width / 2 + p), static_cast<int16_t>(width / 2 - p));
            if (tick == 260) groups(p, n / 4, n / 2, CommandType::GroupAttack, static_cast<int16_t>(width / 2), static_cast<int16_t>(width / 2));
        }
    };
}

// The hazards of a world with a crowd: a hill for each team (the hatching of the newborn, 40 ticks), a pond that ants fall into, and in the middle of the world, where the
// crowds cross, ten ants of each typed kind for each team (bombers plant, fire ants light, thieves raid, combat ants fight back, swimmers build bridges and swim).
// The same calls are made in both engines, in the same order.
void add_hazards(SimulationEngine& e, uint32_t width, Crowd& crowd) {
    const int32_t mid = static_cast<int32_t>(width / 2);
    e.set_hatch_delay_ticks(40);
    e.set_anthill(0, TileCoord{mid - 2, 6});
    e.set_anthill(1, TileCoord{mid - 2, static_cast<int32_t>(width) - 10});
    e.set_anthill(2, TileCoord{6, mid - 2});
    e.set_anthill(3, TileCoord{static_cast<int32_t>(width) - 10, mid - 2});
    for (int32_t y = mid + 6; y < mid + 12; ++y) {
        for (int32_t x = mid + 6; x < mid + 12; ++x) e.set_terrain(x, y, 2);   // water
    }
    const AntType kinds[] = {AntType::Bomber, AntType::Fire, AntType::Thief, AntType::Combat, AntType::Swimmer};
    for (uint8_t p = 0; p < 4; ++p) {
        int32_t i = 0;
        for (const AntType kind : kinds) {
            for (int32_t k = 0; k < 10; ++k, ++i) {
                crowd.special[p].push_back(e.spawn_unit(p, kind, TileCoord{mid - 20 + i % 25, mid - 8 + 4 * p + i / 25}, false));
            }
        }
    }
}

// What happens besides the orders of crowd_script (the same calls in both engines): a fire wall across the middle of the world at tick 30 and bombs beside it at tick 31 (ants
// that walk or are thrown onto them), the typed ants sent to the middle at tick 5 and given special orders at tick 220, an egg hatched for each team every 45 ticks, a third
// of the ants of team 1 killed at tick 200 (a mass death) and team 2 leaving the match at tick 330 (all its ants die)
Script hazard_script(const Crowd& crowd, uint32_t width) {
    const Script orders = crowd_script(crowd, width);
    return [crowd, width, orders](Twin& t, uint32_t tick, std::vector<Command>& out) {
        orders(t, tick, out);
        const int32_t mid = static_cast<int32_t>(width / 2);
        auto special_group = [&](uint8_t p, CommandType type, int32_t x, int32_t y) {
            const std::vector<uint32_t>& ids = crowd.special[p];
            for (size_t i = 0; i < ids.size(); i += kMaxCommandAnts) {
                Command c;
                c.type = type;
                c.issuer = p;
                c.tile_x = static_cast<int16_t>(x);
                c.tile_y = static_cast<int16_t>(y);
                for (size_t j = i; j < std::min(ids.size(), i + kMaxCommandAnts); ++j) c.ants.push_back(ids[j]);
                out.push_back(c);
            }
        };
        if (tick % 45 == 10) {
            t.both([](SimulationEngine& e) {
                for (uint8_t p = 0; p < 4; ++p) e.try_hatch(p, AntType::Worker, true);
            });
        }
        if (tick == 5) {
            for (uint8_t p = 0; p < 4; ++p) special_group(p, CommandType::GroupMove, mid - 6 + 4 * p, mid + 2);
        }
        if (tick == 30) {
            t.both([mid](SimulationEngine& e) {
                for (int32_t x = mid - 14; x <= mid + 14; ++x) e.set_fire_at(TileCoord{x, mid}, 2000);
            });
        }
        if (tick == 31) {
            t.both([mid](SimulationEngine& e) {
                for (int32_t x = mid - 14; x <= mid + 14; x += 2) e.grid_mut().place_bomb(static_cast<uint32_t>(x), static_cast<uint32_t>(mid - 4), 0);
            });
        }
        if (tick == 200) {
            t.both([&crowd](SimulationEngine& e) {
                for (size_t i = 0; i < crowd.ids[1].size(); i += 3) e.kill_unit(crowd.ids[1][i]);
            });
        }
        if (tick == 220) {
            for (uint8_t p = 0; p < 4; ++p) special_group(p, CommandType::GroupSpecial, mid - 3 + 2 * p, mid - 4);
        }
        if (tick == 330) t.both([](SimulationEngine& e) { e.drop_player(2); });
    };
}

std::string play_crowd(uint32_t width, uint32_t per_team, Layout layout, uint32_t ticks, uint64_t* checks, uint64_t* pending_seen) {
    Twin t;
    t.old_engine.init_test_world(width, width, 9);
    t.engine.init_test_world(width, width, 9);
    Crowd crowd = populate(t.engine, width, per_team, layout);
    Crowd same = populate(t.old_engine, width, per_team, layout);
    add_hazards(t.engine, width, crowd);
    add_hazards(t.old_engine, width, same);
    if (same.ids != crowd.ids || same.special != crowd.special) return "the two worlds hold different ants";
    const std::string why = play(t, nullptr, 0x0F, ticks, hazard_script(crowd, width), checks);
    *pending_seen = t.pending_seen;
    return why;
}

void run_crowd_tests(bool big) {
    struct Case {
        const char* name;
        uint32_t width, per_team;
        Layout layout;
        uint32_t ticks;   // (0: the test's)
    };
    std::vector<Case> cases;
    if (!big) {
        cases.push_back({"4 x 60 packed, 90 x 90", 90, 60, Layout::Packed, 0});
        cases.push_back({"4 x 100 spread, 90 x 90", 90, 100, Layout::Spread, 0});
        cases.push_back({"4 x 60 in piles of 24, 90 x 90", 90, 60, Layout::Piled, 0});
    } else {
        cases.push_back({"4 x 700 packed, 150 x 150", 150, 700, Layout::Packed, 0});
        cases.push_back({"4 x 700 spread, 150 x 150", 150, 700, Layout::Spread, 0});
        cases.push_back({"4 x 700 in piles of 24, 150 x 150", 150, 700, Layout::Piled, 0});
        cases.push_back({"4 x 1500 spread, 150 x 150", 150, 1500, Layout::Spread, 400});
        cases.push_back({"4 x 1500 in piles of 24, 150 x 150", 150, 1500, Layout::Piled, 400});
    }
    TEST_CASE(big ? "L3.2 Packed, Spread And Piled Worlds Of 4 x 700 And 4 x 1,500 Ants, ordered across the map, stopped, re-ordered, attacked, with bombs, fire, deaths and hatching: the hashes equal at every tick" :
                    "L3.1 Small Packed, Spread And Piled Worlds, ordered across the map, stopped, re-ordered, attacked, with bombs, fire, deaths and hatching: the hashes equal at every tick, 600 ticks") {
        const uint32_t ticks = ANTS_LOOKUP_TEST_SANITIZED ? (big ? 120u : 300u) : 600u;
        for (const Case& c : cases) {
            uint64_t checks = 0, pending = 0;
            const uint32_t n = c.ticks != 0 && c.ticks < ticks ? c.ticks : ticks;
            const Marks marks;
            const auto t0 = std::chrono::steady_clock::now();
            const std::string why = play_crowd(c.width, c.per_team, c.layout, n, &checks, &pending);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::cout << "\n        " << c.name << ": " << n << " ticks in " << std::fixed << std::setprecision(0) << secs << " s, " << checks << " lookups checked, " << pending << " queued requests seen, " << marks.since() << "   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(checks > 10000);
            ASSERT_TRUE(pending > 0);
            ASSERT_TRUE(marks.scans_since() > 0);                       // the tile lists answered, and the checks compared them
            ASSERT_TRUE(marks.blasts_since() > 0);                      // piles of ants were thrown apart
            ASSERT_TRUE(marks.blast_ants_since() > marks.blasts_since());   // and some blast threw two ants or more
        }
    } TEST_END();
}

// A new match on an engine that played one (a room starts the next match on the same engine)
void run_reinit_tests() {
    TEST_CASE("L2.3 init and init_test_world on an engine that was used: the ant ids restart at 1, the state is a fresh engine's, and the lookups go on answering") {
        const std::string dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
        const auto& level = library_map(dir, "TINY.LVL");
        Twin t;
        t.old_engine.init(level, 91, 0x0F);
        t.engine.init(level, 91, 0x0F);
        ASSERT_TRUE(play(t, &level, 0x0F, 120, nullptr).empty());
        for (int i = 0; i < 6; ++i) {                                // more ants than a fresh match has: the id counter is well past its start
            ASSERT_TRUE(t.old_engine.spawn_unit(0, AntType::Worker, TileCoord{8 + i, 8}, false) == t.engine.spawn_unit(0, AntType::Worker, TileCoord{8 + i, 8}, false));
        }
        // a map again: the engine that was used equals one that was never used, and its first new ant has the id that the fresh one's has
        SimulationEngine fresh;
        fresh.init(level, 92, 0x0F);
        t.old_engine.init(level, 92, 0x0F);
        t.engine.init(level, 92, 0x0F);
        ASSERT_TRUE(t.engine.state_hash() == fresh.state_hash() && t.old_engine.state_hash() == fresh.state_hash());
        const uint32_t first_id = fresh.spawn_unit(1, AntType::Worker, TileCoord{9, 9}, false);
        ASSERT_TRUE(first_id == t.engine.spawn_unit(1, AntType::Worker, TileCoord{9, 9}, false));
        ASSERT_TRUE(first_id == t.old_engine.spawn_unit(1, AntType::Worker, TileCoord{9, 9}, false));
        ASSERT_TRUE(t.engine.state_hash() == fresh.state_hash());
        ASSERT_TRUE(play(t, &level, 0x0F, 120, nullptr).empty());   // (find_unit never falls back to the scan: the slot of an id is its id - 1)
        // a test world on the engine that was used
        SimulationEngine fresh_world;
        fresh_world.init_test_world(90, 90, 9);
        t.old_engine.init_test_world(90, 90, 9);
        t.engine.init_test_world(90, 90, 9);
        ASSERT_TRUE(t.engine.state_hash() == fresh_world.state_hash() && t.old_engine.state_hash() == fresh_world.state_hash());
        const Crowd crowd = populate(t.engine, 90, 12, Layout::Spread);
        ASSERT_TRUE(populate(t.old_engine, 90, 12, Layout::Spread).ids == crowd.ids);
        ASSERT_TRUE(populate(fresh_world, 90, 12, Layout::Spread).ids == crowd.ids);
        ASSERT_TRUE(crowd.ids[0][0] == 1 && crowd.ids[3].back() == 48);
        ASSERT_TRUE(t.engine.state_hash() == fresh_world.state_hash());
        ASSERT_TRUE(play(t, nullptr, 0x0F, 150, crowd_script(crowd, 90)).empty());
    } TEST_END();
}

}  // namespace

int main(int argc, char* argv[]) {
    bool quick = true;
    bool big = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--big") == 0) {
            quick = false;
            big = true;
        } else if (std::strcmp(argv[i], "--all") == 0) {
            quick = true;
            big = true;
        } else {
            std::cout << "usage: test_engine_lookups [--big | --all]\n";
            return 2;
        }
    }
    std::cout << "\n=======================================================\n [ENGINE LOOKUPS SUITE] the indexes equal the scans they replaced (" << (quick ? "quick" : "") << (quick && big ? " + " : "")
              << (big ? "big" : "") << ")\n=======================================================\n";
    if (quick) {
        run_lookup_tests();
        run_map_tests();
        run_crowd_tests(false);
        run_reinit_tests();
    }
    if (big) run_crowd_tests(true);
    std::cout << "\n=======================================================\n"
              << " ENGINE LOOKUPS TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
