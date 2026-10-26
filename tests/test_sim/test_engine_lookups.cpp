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
//   ./test_engine_lookups --big    4 x 700 and 4 x 1,500 ants (packed in the corners, spread over the map, piled on shared tiles), 600 ticks (300 for the 1,500s; minutes: the engine that scans is the old, slow one)
//   ./test_engine_lookups --all    both
//   ./test_engine_lookups --go-to  only the tests of the go_to refresh (G1 - G4: the marks of the ants that are out of step)
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

// Counts the stale events that the two nets of the heap put right instead of stopping the program, while it lives (lookup_shadow.hpp): for the tests that write the fields of the
// events from outside on purpose
struct CountStale {
    CountStale() { lookup_shadow_count_stale_events() = true; }
    ~CountStale() { lookup_shadow_count_stale_events() = false; }
    CountStale(const CountStale&) = delete;
    CountStale& operator=(const CountStale&) = delete;
};

// What the shadow checks of the tile lists did since this was made: the questions of occ_scan and the blasts that were compared with their scans, the ants those blasts threw,
// and the lists that were rebuilt from the ants and compared (a test shows that it met piles of ants, not only that nothing differed)
struct Marks {
    uint64_t scans{lookup_shadow_scan_checks()}, blasts{lookup_shadow_blast_checks()}, blast_ants{lookup_shadow_blast_ants()}, lists{lookup_shadow_list_checks()};
    uint64_t events{lookup_shadow_event_checks()}, heaps{lookup_shadow_event_verifies()}, catch_ups{lookup_shadow_event_catch_ups()};
    uint64_t changes{lookup_shadow_event_catch_up_changes()}, stale{lookup_shadow_event_stale_entries()};
    uint64_t picks[4] = {lookup_shadow_event_picks(0), lookup_shadow_event_picks(1), lookup_shadow_event_picks(2), lookup_shadow_event_picks(3)};
    uint64_t scans_since() const { return lookup_shadow_scan_checks() - scans; }
    uint64_t blasts_since() const { return lookup_shadow_blast_checks() - blasts; }
    uint64_t blast_ants_since() const { return lookup_shadow_blast_ants() - blast_ants; }
    uint64_t events_since() const { return lookup_shadow_event_checks() - events; }
    uint64_t heaps_since() const { return lookup_shadow_event_verifies() - heaps; }
    uint64_t catch_ups_since() const { return lookup_shadow_event_catch_ups() - catch_ups; }
    uint64_t picks_since(size_t kind) const { return lookup_shadow_event_picks(kind) - picks[kind]; }
    uint64_t changes_since() const { return lookup_shadow_event_catch_up_changes() - changes; }   // ants whose events only the start-of-tick pass put right
    uint64_t stale_since() const { return lookup_shadow_event_stale_entries() - stale; }          // entries that a selection found the ant did not bear out
    std::string since() const {
        return std::to_string(scans_since()) + " tile questions, " + std::to_string(blasts_since()) + " blasts of " + std::to_string(blast_ants_since()) + " ants, " +
               std::to_string(lookup_shadow_list_checks() - lists) + " list checks, " + std::to_string(events_since()) + " event selections compared, events run: " + std::to_string(picks_since(0)) +
               " burn, " + std::to_string(picks_since(1)) + " combat timer, " + std::to_string(picks_since(2)) + " frame, " + std::to_string(picks_since(3)) + " pause, " +
               std::to_string(heaps_since()) + " heap checks, " + std::to_string(catch_ups_since()) +
               " ants looked at by the start-of-tick pass (it found nothing to do on any)";
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
        ASSERT_TRUE(victims.size() == 300 && std::is_sorted(victims.begin(), victims.end(), [](const AntUnit* x, const AntUnit* y) { return x->id < y->id; }));
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
                    a->set_pixel_pos(static_cast<int32_t>(below(300)), static_cast<int32_t>(below(300)));   // moved by something that does not register it (a knock-back, a landing): the refresh does
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
// The event heap (src/ants_sim/event_heap.hpp, event_queue.cpp): the next timed event of the ants without a scan of every ant
// ---------------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t kNever = 0xFFFFFFFFu;

// "slot:K@time" for an event (K: B burn, C combat timer, L animation frame, P pause), "none" for no event
std::string event_text(bool found, const AntEventPick& p) {
    if (!found) return "none";
    return std::to_string(p.slot) + ":" + "BCLP"[static_cast<size_t>(p.kind) & 3u] + "@" + std::to_string(p.time);
}

// The event that `top` gave is done: not pending any more
void drop(AntEventHeap& h, const AntEventPick& p) { h.set(p.slot, p.kind, false, 0); }

// An engine with no map at all and ants that are made by hand: the fields that decide the events are written the way the simulation writes them (a field, then event_refresh)
struct EventWorld {
    SimulationEngineImpl impl;
    movement::MotionClip multi, single;   // a clip of several frames, a clip of one frame
    EventWorld() {
        impl.grid_.init_empty(8, 8);
        impl.movement_reset();
        for (size_t i = 0; i < movement::clip_count(); ++i) {
            const movement::MotionClip c = movement::clip_at(i);
            if (c.valid() && c.count > 1 && !multi.valid()) multi = c;
            if (c.valid() && c.count == 1 && !single.valid()) single = c;
        }
    }
    // An ant in the next slot of the table, with this id (not necessarily slot + 1)
    AntUnit& add(uint32_t id) {
        impl.ants_.push_back(std::make_unique<AntUnit>(id, TeamId::Black, AntType::Worker, 0, 0));
        return *impl.ants_.back();
    }
    void loco(AntUnit& a, uint32_t next_ms) {
        a.loco_action = AntUnit::kActionWalk;
        a.loco.clip = multi;
        a.loco.cursor = 1;
        a.loco.next_ms = next_ms;
        impl.event_refresh(a);
    }
    void pause(AntUnit& a, uint32_t fire_ms) {
        a.loco_action = AntUnit::kActionWalk;
        a.pause_active = true;
        a.pause_fire_ms = fire_ms;
        impl.event_refresh(a);
    }
    void burn(AntUnit& a, uint32_t ms) {
        a.burn_end_ms = ms;
        impl.event_refresh(a);
    }
    void combevt(AntUnit& a, uint32_t ms) {
        a.combevt_due_ms = ms;
        impl.event_refresh(a);
    }
    // The next event at or before t_end by the heap (which the shadow build also compares with the scan), and by the scan alone: they must be the same
    std::string next(uint32_t t_end) {
        AntEventPick p, s;
        const bool found = impl.next_ant_event(t_end, p);
        const bool scan_found = impl.next_ant_event_scan(t_end, s);
        const std::string by_heap = event_text(found, p), by_scan = event_text(scan_found, s);
        if (by_heap != by_scan || (found && p.ant != s.ant)) return "heap " + by_heap + " but scan " + by_scan;
        return by_heap;
    }
    // Runs the next event the way movement_tick does: its handler changes the ant (here: `handler`), and the ant's events are set again by whoever changed its fields
    std::string run(uint32_t t_end, const std::function<void(AntUnit&, AntEventKind)>& handler) {
        AntEventPick p;
        const std::string text = next(t_end);
        if (!impl.next_ant_event(t_end, p)) return text;
        handler(*p.ant, p.kind);
        impl.event_refresh(*p.ant);
        return text;
    }
    // The handler of a finished event: its field goes back to "no event"
    static void finish(AntUnit& a, AntEventKind kind) {
        if (kind == AntEventKind::Burn) a.burn_end_ms = 0;
        else if (kind == AntEventKind::Combevt) a.combevt_due_ms = 0;
        else if (kind == AntEventKind::Pause) a.pause_active = false;
        else a.loco.cursor = 0;
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------
// go_to refreshes only the ants that are marked (occupancy.cpp, occ_refresh_for_order): the marks and the refresh, by hand and at random
// ---------------------------------------------------------------------------------------------------------------------------------

// A world of 12 x 12 tiles with no engine around it, in which ants are made, moved and removed by the functions the simulation uses (add_ant, set_position, occ_move, remove_ant),
// and orders are given by go_to. Two of them are played with the same operations: one as the simulation plays a tick (go_to refreshes the marked ants), one on the scans alone
// (go_to refreshes every ant, as before the marks); `same` says whether they are the same.
struct GoWorld {
    SimulationEngineImpl impl;
    GoWorld() { reset(12, 12); }
    void reset(uint32_t w, uint32_t h) {
        impl.ants_.clear();
        impl.grid_.init_empty(w, h);
        impl.movement_reset();
        impl.next_ant_id_ = 1;
        impl.grid_.set_anthill(0, TileCoord{0, 0});            // (a hill for each team: a retreating ant has a home to go to)
        impl.grid_.set_anthill(1, TileCoord{static_cast<int32_t>(w) - 4, 0});
        impl.grid_.set_anthill(2, TileCoord{0, static_cast<int32_t>(h) - 4});
        impl.grid_.set_anthill(3, TileCoord{static_cast<int32_t>(w) - 4, static_cast<int32_t>(h) - 4});
    }
    // The ant joins the table as the simulation adds one (add_ant); `registered`: it is put on its tile at once (set_position), as a hatched ant is
    AntUnit& spawn(uint8_t player, int32_t tx, int32_t ty, bool registered) {
        AntUnit* a = impl.add_ant(std::make_unique<AntUnit>(impl.next_ant_id_++, static_cast<TeamId>(player & 3u), AntType::Worker, tx, ty));
        a->player_id = player;
        if (registered) impl.set_position(*a, tx * 32 + 16, ty * 32 + 16);
        return *a;
    }
    bool marked(const AntUnit& a) const { return impl.occ_marked(impl.ant_slot(a)); }
    bool anything_marked() const {
        for (const uint64_t w : impl.occ_dirty_) {
            if (w != 0) return true;
        }
        return false;
    }
    int32_t on(int32_t tx, int32_t ty) { const auto* c = impl.occ_cell(TileCoord{tx, ty}); return c ? c->ant : -2; }
    // What a go_to does first, as a tick plays it
    void refresh_in_tick() {
        const SimulationEngineImpl::OccTickScope in_tick(impl);
        impl.occ_refresh_for_order();
    }
};

// What the two worlds differ in ("" when nothing): the grid, the lists, and every field that the refresh or an order writes
std::string go_world_diff(GoWorld& x, GoWorld& y) {
    SimulationEngineImpl& a = x.impl;
    SimulationEngineImpl& b = y.impl;
    if (a.ants_.size() != b.ants_.size()) return "the tables differ in size";
    if (a.occ_.size() != b.occ_.size()) return "the grids differ in size";
    for (size_t i = 0; i < a.occ_.size(); ++i) {
        if (a.occ_[i].ant != b.occ_[i].ant || a.occ_[i].multi != b.occ_[i].multi) return "the cell " + std::to_string(i) + " differs";
    }
    if (!(a.tile_ants_ == b.tile_ants_)) return "the tile lists differ";
    for (size_t i = 0; i < a.ants_.size(); ++i) {
        const AntUnit& p = *a.ants_[i];
        const AntUnit& q = *b.ants_[i];
        const std::string who = "ant " + std::to_string(p.id) + ": ";
        if (p.occ_tile != q.occ_tile) return who + "occ_tile";
        if (p.pixel_x != q.pixel_x || p.pixel_y != q.pixel_y || p.removed != q.removed) return who + "position or removal";
        if (p.orig_order != q.orig_order || p.orig_order_tile != q.orig_order_tile || p.final_dest != q.final_dest) return who + "order";
        if (p.state != q.state || p.loco_action != q.loco_action || p.waypoints != q.waypoints || p.hp != q.hp) return who + "state or action";
    }
    return "";
}

void run_go_to_tests() {
    TEST_CASE("G1.1 go_to refresh, each writer by hand: an ant that joins the table, a step that registers the ant on another tile than its own, a move, a removal each leave the ant marked or not as the full refresh needs, and the refresh visits it") {
        GoWorld w;
        AntUnit& a = w.spawn(0, 3, 3, false);                       // joins the table: out of step until registered
        ASSERT_TRUE(w.marked(a) && a.occ_tile.x < 0);
        w.impl.occ_move(a, TileCoord{3, 3});                         // registered on its own tile: in step, the mark goes
        ASSERT_TRUE(!w.marked(a) && w.on(3, 3) == static_cast<int32_t>(a.id));
        w.impl.occ_move(a, TileCoord{4, 3});                         // the step callback registers the ant on the tile it steps to, before set_position moves it: out of step
        ASSERT_TRUE(w.marked(a));
        w.refresh_in_tick();                                         // go_to puts it back on its pixel tile
        ASSERT_TRUE(!w.marked(a) && a.occ_tile == (TileCoord{3, 3}) && w.on(4, 3) == -1 && w.on(3, 3) == static_cast<int32_t>(a.id));
        w.impl.set_position(a, 5 * 32 + 16, 6 * 32 + 16);            // a knock-back or landing: pixel position and registration together
        ASSERT_TRUE(!w.marked(a) && a.occ_tile == (TileCoord{5, 6}));
        w.impl.set_position(a, 5 * 32 + 30, 6 * 32 + 2);             // a step inside the tile: no registration change, still in step
        ASSERT_TRUE(!w.marked(a));
        w.impl.occ_move(a, TileCoord{-1, -1});                       // unregistered while alive (the first half of remove_ant): out of step
        ASSERT_TRUE(w.marked(a));
        w.impl.occ_move(a, TileCoord{5, 6});
        ASSERT_TRUE(!w.marked(a));
        w.impl.remove_ant(a);                                        // removal: unregistered, removed: in step, whatever the mark was
        ASSERT_TRUE(a.removed && a.occ_tile.x < 0 && !w.marked(a));
        AntUnit& b = w.spawn(1, 2, 2, true);                         // a hatched ant: joins, is registered at once
        ASSERT_TRUE(!w.marked(b) && w.on(2, 2) == static_cast<int32_t>(b.id));
        w.impl.occ_move(b, TileCoord{-1, -1});
        w.impl.occ_refresh();                                        // the public refresh is the full one and leaves nothing marked
        ASSERT_TRUE(!w.anything_marked() && b.occ_tile == (TileCoord{2, 2}));
        w.impl.occ_dirty_verify();
        AntUnit& c = w.spawn(2, 7, 7, true);                         // marked, then put in step by hand (no writer notes it): the mark is stale until the full refresh clears it
        w.impl.occ_move(c, TileCoord{8, 8});
        ASSERT_TRUE(w.marked(c));
        c.occ_tile = TileCoord{7, 7};
        ASSERT_TRUE(w.marked(c) && w.impl.occ_in_step(c));
        w.impl.occ_refresh();
        ASSERT_TRUE(!w.anything_marked());
    } TEST_END();

    TEST_CASE("G1.2 go_to refresh: the marked ants are visited in the order of the table, one after the other (two ants put back on one tile: the first of the table is the tile's ant, the second makes it a pile)") {
        GoWorld w;
        AntUnit& a = w.spawn(0, 3, 3, true);
        AntUnit& b = w.spawn(0, 3, 3, true);                         // both on tile (3, 3): a is the ant of the cell, b makes it a pile
        ASSERT_TRUE(w.on(3, 3) == static_cast<int32_t>(a.id) && w.impl.occ_cell(TileCoord{3, 3})->multi);
        w.impl.occ_move(b, TileCoord{7, 7});                         // both registered elsewhere, each alone, then (b first) ...
        w.impl.occ_move(a, TileCoord{8, 8});
        ASSERT_TRUE(w.marked(a) && w.marked(b) && !w.impl.occ_cell(TileCoord{3, 3})->multi && w.on(3, 3) == -1);
        w.refresh_in_tick();                                         // a is visited first: it takes the empty cell, b joins it
        ASSERT_TRUE(w.on(3, 3) == static_cast<int32_t>(a.id) && w.impl.occ_cell(TileCoord{3, 3})->multi);
        ASSERT_TRUE(!w.anything_marked());
    } TEST_END();

    TEST_CASE("G1.3 go_to refresh: every marked ant is visited, in whatever word of the marks it is (slots 0, 63, 64, 130, 400), and an ant that is not marked is left alone") {
        GoWorld w;
        std::vector<AntUnit*> ants;
        for (int i = 0; i < 401; ++i) ants.push_back(&w.spawn(static_cast<uint8_t>(i % 4), 1 + i % 10, 1 + (i / 10) % 10, true));
        w.refresh_in_tick();                                         // (nothing is marked)
        ASSERT_TRUE(!w.anything_marked());
        const size_t out[] = {0, 63, 64, 130, 400};
        for (const size_t i : out) w.impl.occ_move(*ants[i], TileCoord{-1, -1});
        for (const size_t i : out) ASSERT_TRUE(w.marked(*ants[i]));
        ASSERT_TRUE(!w.marked(*ants[1]) && !w.marked(*ants[65]));
        const uint64_t visits = lookup_shadow_go_to_dirty_ants();
        w.refresh_in_tick();
        for (const size_t i : out) ASSERT_TRUE(ants[i]->occ_tile == (TileCoord{ants[i]->pixel_x / 32, ants[i]->pixel_y / 32}) && !w.marked(*ants[i]));
        ASSERT_TRUE(lookup_shadow_go_to_dirty_ants() - visits == 5);  // five visits, not 401
        w.impl.occ_dirty_verify();
    } TEST_END();

    TEST_CASE("G1.4 go_to refresh, death in the middle of an order: an ant that is removed while its victim retreats (the victim's go_to puts the dying ant back on its tile, then it is removed and still registered) is put right by the next go_to, as the full refresh does") {
        auto play = [](bool marks) {
            GoWorld w;
            std::string log;
            AntUnit& dying = w.spawn(0, 2, 2, true);
            AntUnit& victim = w.spawn(1, 3, 2, true);
            AntUnit& other = w.spawn(2, 9, 9, true);
            victim.hp = 1;                                           // a 1 hp survivor goes home ahead of the queue: low_hp_check calls go_to
            dying.pending_victim = victim.id;                        // the dying ant's pending hit is delivered by its removal
            dying.pending_range = 1;
            dying.pending_dir = 8;                                   // (cornered: no flight, the victim stuns or retreats)
            dying.pending_tile = TileCoord{3, 2};
            const SimulationEngineImpl::OccTickScope in_tick(w.impl);
            w.impl.occ_in_tick_ = marks;                             // (false: the same play with the full refresh in every go_to)
            w.impl.remove_ant(dying);
            const bool registered_after_removal = dying.removed && dying.occ_tile.x >= 0;   // the quirk: the victim's go_to registered the dying ant, which was then removed
            log += registered_after_removal ? "registered;" : "unregistered;";
            w.impl.go_to(other, TileCoord{1, 1}, false, false);      // the next order refreshes: the removed ant is unregistered
            log += dying.occ_tile.x < 0 ? "gone;" : "still;";
            log += w.marked(dying) ? "marked" : "clear";
            return log + "|" + std::to_string(w.on(2, 2));
        };
        const std::string by_marks = play(true);
        const std::string by_full = play(false);
        ASSERT_TRUE(by_marks == by_full);
        ASSERT_TRUE(by_marks.rfind("registered;gone;clear|-1", 0) == 0);   // the quirk is real (the full refresh has it too), and the next order undoes it
    } TEST_END();

    TEST_CASE("G1.5 go_to refresh outside a tick is the full refresh: an ant moved or removed by hand (no writer noted it) is put right by the next order, a tick's scope ends with the tick, and a copy keeps the marks") {
        GoWorld w;
        AntUnit& a = w.spawn(0, 3, 3, true);
        AntUnit& b = w.spawn(1, 6, 6, true);
        a.set_pixel_pos(8 * 32 + 16, 8 * 32 + 16);                   // moved by hand: no writer, no mark
        b.removed = true;
        ASSERT_TRUE(!w.marked(a) && !w.marked(b) && !w.impl.occ_in_tick_);
        w.impl.occ_refresh_for_order();                              // not in a tick: every ant is looked at
        ASSERT_TRUE(a.occ_tile == (TileCoord{8, 8}) && b.occ_tile.x < 0);
        // a copy of an engine in the middle of a refresh's premise: the marks are copied with the ants
        AntUnit& c = w.spawn(0, 1, 1, true);
        w.impl.occ_move(c, TileCoord{2, 2});
        ASSERT_TRUE(w.marked(c));
        SimulationEngineImpl copy(w.impl);
        ASSERT_TRUE(copy.occ_dirty_ == w.impl.occ_dirty_ && copy.occ_marked(copy.ant_slot(*copy.ants_[2])));
        {
            const SimulationEngineImpl::OccTickScope in_tick(copy);
            copy.occ_refresh_for_order();
        }
        ASSERT_TRUE(copy.ants_[2]->occ_tile == (TileCoord{1, 1}) && !copy.occ_marked(2) && w.impl.occ_marked(2));   // the copy's refresh did not touch the original
        SimulationEngineImpl assigned;
        assigned = w.impl;
        ASSERT_TRUE(assigned.occ_dirty_ == w.impl.occ_dirty_);
        // a new match: no marks, no scope
        w.impl.occ_in_tick_ = true;
        w.impl.ants_.clear();
        w.impl.movement_reset();
        ASSERT_TRUE(!w.anything_marked() && w.impl.occ_dirty_.empty() && !w.impl.occ_in_tick_);
    } TEST_END();

    TEST_CASE("G2.1 go_to refresh at random: two worlds play the same random operations (ants joining, set_position, steps onto another tile, removals with a retreating victim, orders, refreshes, copies, restarts, grids of another size, writes by hand outside a tick), one refreshes the marked ants in a tick, the other every ant: the grid, the lists and every ant are the same after every operation") {
        uint32_t s = 20261026;
        auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
        GoWorld marks, full;
        std::vector<SimulationEngineImpl> spares_m, spares_f;
        uint64_t go_tos = 0, with_marks = 0;
        const uint64_t compares0 = lookup_shadow_go_to_compares();
        auto both = [&](const std::function<void(GoWorld&)>& op) {
            op(marks);
            LinearOnly scans;
            op(full);
        };
        for (int step = 0; step < 6000; ++step) {
            const uint32_t op = below(100);
            const bool in_tick = below(10) != 0;                     // now and then outside a tick
            const uint32_t pick = below(1000);
            static const uint16_t kHp[] = {1, 1, 5, 10};
            const uint16_t hp = kHp[below(4)];
            const uint32_t size_w = 8 + below(7), size_h = 8 + below(7);
            const uint32_t x = below(16), y = below(16), px = below(12 * 32 + 80), py = below(12 * 32 + 80);
            auto ant_of = [&](GoWorld& w) -> AntUnit* { return w.impl.ants_.empty() ? nullptr : w.impl.ants_[pick % w.impl.ants_.size()].get(); };
            both([&](GoWorld& w) { w.impl.occ_in_tick_ = in_tick; });
            if (op < 12 && marks.impl.ants_.size() < 150) {
                const uint8_t team = static_cast<uint8_t>(below(4));
                const bool reg = below(2) == 0;
                both([&](GoWorld& w) { w.spawn(team, static_cast<int32_t>(x % 12), static_cast<int32_t>(y % 12), reg); });
            } else if (op < 30) {
                both([&](GoWorld& w) { if (AntUnit* a = ant_of(w)) { if (!a->removed) w.impl.set_position(*a, static_cast<int32_t>(px) - 40, static_cast<int32_t>(py) - 40); } });
            } else if (op < 48) {
                both([&](GoWorld& w) { if (AntUnit* a = ant_of(w)) { w.impl.occ_move(*a, TileCoord{static_cast<int32_t>(x) - 2, static_cast<int32_t>(y) - 2}); } });
            } else if (op < 54) {
                both([&](GoWorld& w) {
                    AntUnit* a = ant_of(w);
                    if (!a || a->removed) return;
                    if (w.impl.ants_.size() > 1) {                   // its pending hit goes to another ant, which may have 1 hp (then it retreats by go_to)
                        AntUnit& v = *w.impl.ants_[(pick / 7u) % w.impl.ants_.size()];
                        if (&v != a && !v.removed) {
                            v.hp = hp;
                            a->pending_victim = v.id;
                            a->pending_dir = 8;
                            a->pending_tile = TileCoord{v.pixel_x / 32, v.pixel_y / 32};
                        }
                    }
                    w.impl.remove_ant(*a);
                });
            } else if (op < 78) {
                ++go_tos;
                both([&](GoWorld& w) {
                    AntUnit* a = ant_of(w);
                    if (!a || a->removed) return;
                    w.impl.go_to(*a, TileCoord{static_cast<int32_t>(x % 12), static_cast<int32_t>(y % 12)}, false, false);
                });
            } else if (op < 84) {
                both([&](GoWorld& w) { w.impl.occ_refresh_for_order(); });
            } else if (op < 87) {
                both([&](GoWorld& w) { w.impl.occ_refresh(); });
            } else if (op < 91) {
                both([&](GoWorld& w) { if (AntUnit* a = ant_of(w)) { a->set_pixel_pos(static_cast<int32_t>(px), static_cast<int32_t>(py)); } });   // by hand: no writer is told (outside a tick the next refresh is the full one)
                both([&](GoWorld& w) { w.impl.occ_in_tick_ = false; w.impl.occ_refresh_for_order(); });
            } else if (op < 93) {
                spares_m.push_back(marks.impl);
                {
                    LinearOnly scans;
                    spares_f.push_back(full.impl);
                }
            } else if (op < 95 && !spares_m.empty()) {
                const size_t k = pick % spares_m.size();
                marks.impl = spares_m[k];
                full.impl = spares_f[k];
            } else if (op < 96 && below(3) == 0) {
                both([&](GoWorld& w) { w.reset(size_w, size_h); });
            } else if (op < 97 && below(2) == 0) {
                both([&](GoWorld& w) { w.impl.grid_.init_empty(pick % 2 == 0 ? 13 : 12, 12); });   // another size: the next refresh starts the grid again
            } else if (spares_m.size() > 5) {
                spares_m.erase(spares_m.begin());
                spares_f.erase(spares_f.begin());
            }
            if (marks.anything_marked()) ++with_marks;
            const std::string why = go_world_diff(marks, full);
            if (!why.empty()) std::cout << "\n        step " << step << ": " << why << "\n";
            ASSERT_TRUE(why.empty());
        }
        std::cout << "\n        " << go_tos << " orders, " << (lookup_shadow_go_to_compares() - compares0) << " refreshes of marked ants compared with the full refresh, " << with_marks << " moments with marks   ... ";
        ASSERT_TRUE(lookup_shadow_go_to_compares() - compares0 > 200);
        ASSERT_TRUE(with_marks > 500);
    } TEST_END();

    TEST_CASE("G2.2 go_to refresh at the scale of the mass resume: 2,400 ants on 60 x 60 tiles, 2,000 orders in a tick, each after a few steps onto another tile or a removal that leave marked ants, in two worlds (marks, and the full refresh): every refresh of marked ants is compared with the full refresh and the worlds are the same after every order") {
        uint32_t s = 0x6007u;
        auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
        GoWorld marks, full;
        {
            marks.reset(60, 60);
            LinearOnly scans;
            full.reset(60, 60);
        }
        std::vector<uint8_t> used(60 * 60, 0);
        for (int i = 0; i < 2400; ++i) {                              // distinct tiles, four teams mixed
            uint32_t x, y;
            do { x = below(60); y = below(60); } while (used[y * 60 + x] != 0);
            used[y * 60 + x] = 1;
            const uint8_t team = static_cast<uint8_t>(i % 4);
            marks.spawn(team, static_cast<int32_t>(x), static_cast<int32_t>(y), true);
            LinearOnly scans;
            full.spawn(team, static_cast<int32_t>(x), static_cast<int32_t>(y), true);
        }
        marks.impl.occ_in_tick_ = true;                               // one tick: the orders of a mass resume follow one another
        const uint64_t compares0 = lookup_shadow_go_to_compares(), visits0 = lookup_shadow_go_to_dirty_ants();
        uint64_t compared_orders = 0;
        for (int step = 0; step < 2000; ++step) {
            const uint32_t steps = below(4);                          // 0 - 3 ants stepped onto another tile or removed since the last order
            std::vector<std::pair<uint32_t, TileCoord>> moves;
            for (uint32_t k = 0; k < steps; ++k) moves.emplace_back(below(2400), TileCoord{static_cast<int32_t>(below(60)), static_cast<int32_t>(below(60))});
            const bool kill = below(20) == 0;
            const uint32_t victim = below(2400), who = below(2400);
            const TileCoord goal{static_cast<int32_t>(below(60)), static_cast<int32_t>(below(60))};
            auto play = [&](GoWorld& w) {
                for (const auto& m : moves) {
                    AntUnit& a = *w.impl.ants_[m.first];
                    if (!a.removed) w.impl.occ_move(a, m.second);
                }
                if (kill && !w.impl.ants_[victim]->removed) w.impl.remove_ant(*w.impl.ants_[victim]);
                AntUnit& a = *w.impl.ants_[who];
                if (!a.removed) w.impl.go_to(a, goal, false, false);
            };
            const uint64_t before = lookup_shadow_go_to_compares();
            play(marks);
            if (lookup_shadow_go_to_compares() != before) ++compared_orders;
            {
                LinearOnly scans;
                play(full);
            }
            const std::string why = go_world_diff(marks, full);
            if (!why.empty()) std::cout << "\n        order " << step << ": " << why << "\n";
            ASSERT_TRUE(why.empty());
        }
        std::cout << "\n        " << compared_orders << " of 2,000 orders refreshed marked ants and were compared with the full refresh (" << (lookup_shadow_go_to_dirty_ants() - visits0) << " marked ants visited)   ... ";
        ASSERT_TRUE(lookup_shadow_go_to_compares() - compares0 > 1000);
        ASSERT_TRUE(lookup_shadow_go_to_dirty_ants() - visits0 > 1500);
    } TEST_END();
}

void run_event_tests() {
    TEST_CASE("L4.1 Event heap alone: the order is (time, place, kind), an event whose time moves earlier or later, an event that is not pending any more or is done, a version that changes, the heap made again (answers worked out by hand)") {
        AntEventHeap h;
        AntEventPick p;
        ASSERT_TRUE(!h.top(p) && h.pending() == 0 && h.entries() == 0);
        // seven events: two of one ant and one of another at the same time, an earlier one of a later ant, and a late one
        h.set(5, AntEventKind::Loco, true, 100);
        h.set(2, AntEventKind::Pause, true, 100);
        h.set(2, AntEventKind::Burn, true, 100);
        h.set(2, AntEventKind::Combevt, true, 100);
        h.set(7, AntEventKind::Burn, true, 90);
        h.set(0, AntEventKind::Loco, true, 200);
        h.set(3, AntEventKind::Loco, true, 0);                       // (an ant whose time is 0: a time like any other)
        ASSERT_TRUE(h.pending() == 7 && h.entries() == 7);
        const std::vector<std::string> by_hand = {"3:L@0", "7:B@90", "2:B@100", "2:C@100", "2:P@100", "5:L@100", "0:L@200"};   // (time, slot, kind)
        {
            AntEventHeap c = h;                                       // a copy gives the same order, and takes nothing from the original
            for (const std::string& want : by_hand) {
                ASSERT_TRUE(c.top(p));
                ASSERT_TRUE(event_text(true, p) == want);
                drop(c, p);
            }
            ASSERT_TRUE(!c.top(p) && c.pending() == 0);
            ASSERT_TRUE(h.pending() == 7 && h.consistent());
        }
        // the time of an event moves earlier: the new entry is first and the old one is dead; later: the old one is dead and the event is behind the others; away: gone
        h.set(0, AntEventKind::Loco, true, 50);
        ASSERT_TRUE(h.top(p) && event_text(true, p) == "3:L@0");
        h.set(3, AntEventKind::Loco, true, 300);                     // later
        ASSERT_TRUE(h.top(p) && event_text(true, p) == "0:L@50");
        h.set(0, AntEventKind::Loco, false, 0);                      // not pending any more
        ASSERT_TRUE(h.top(p) && event_text(true, p) == "7:B@90");
        h.set(7, AntEventKind::Burn, true, 95);                      // later, still before the ties at 100
        ASSERT_TRUE(h.top(p) && event_text(true, p) == "7:B@95");
        h.set(7, AntEventKind::Burn, true, 100);                     // later still: the tie at 100 goes to the lower place
        ASSERT_TRUE(h.top(p) && event_text(true, p) == "2:B@100");
        // the same time again changes nothing (no second entry), the old time again after another time is one live entry
        const size_t entries = h.entries();
        h.set(7, AntEventKind::Burn, true, 100);
        ASSERT_TRUE(h.entries() == entries);
        h.set(5, AntEventKind::Loco, true, 60);
        h.set(5, AntEventKind::Loco, true, 100);                     // 100, 60, 100: the entry of the first 100 and of the second are two, one of them dead
        ASSERT_TRUE(h.pending() == 6 && h.consistent());
        ASSERT_TRUE(h.top(p) && event_text(true, p) == "2:B@100");
        // an event that is done: it is out, it is pending again only when it is set; set at the same time it is the next one again
        ASSERT_TRUE(h.top(p));
        drop(h, p);
        ASSERT_TRUE(h.top(p) && event_text(true, p) == "2:C@100" && h.pending() == 5);
        h.set(2, AntEventKind::Burn, true, 100);
        ASSERT_TRUE(h.top(p) && event_text(true, p) == "2:B@100" && h.pending() == 6 && h.consistent());
        // a long run of moves of one event leaves one live entry: the heap is made again from the records when the dead entries outnumber the live ones, and nothing is lost
        for (uint32_t t = 1000; t < 21000; ++t) h.set(9, AntEventKind::Loco, true, t);
        ASSERT_TRUE(h.pending() == 7 && h.entries() <= 4u * 7u + 4096u + 1u && h.consistent());
        ASSERT_TRUE(h.top(p) && event_text(true, p) == "2:B@100");
        std::vector<std::string> order;
        while (h.top(p)) {
            order.push_back(event_text(true, p));
            drop(h, p);
        }
        ASSERT_TRUE(order == (std::vector<std::string>{"2:B@100", "2:C@100", "2:P@100", "5:L@100", "7:B@100", "3:L@300", "9:L@20999"}));
        // a copy, an assignment over a heap that holds other events, a move (the source is left empty and usable) and an assignment into the moved-from one
        AntEventHeap a, b;
        a.set(1, AntEventKind::Loco, true, 10);
        a.set(4, AntEventKind::Pause, true, 5);
        b.set(2, AntEventKind::Burn, true, 1);
        AntEventHeap copy(a);
        b = a;
        ASSERT_TRUE(copy.consistent() && b.consistent() && copy.pending() == 2 && b.pending() == 2);
        ASSERT_TRUE(b.top(p) && event_text(true, p) == "4:P@5");
        AntEventHeap moved(std::move(a));
        ASSERT_TRUE(moved.pending() == 2 && moved.top(p) && event_text(true, p) == "4:P@5");
        a = copy;                                                    // into the moved-from heap
        ASSERT_TRUE(a.pending() == 2 && a.consistent() && a.top(p) && event_text(true, p) == "4:P@5");
        a.reset();
        ASSERT_TRUE(!a.top(p) && a.pending() == 0 && a.slots() == 0);
    } TEST_END();

    TEST_CASE("L4.2 Next ant event: the earliest by time, then by place in the table (not by id), then by kind; at or before the end of the tick only (answers worked out by hand)") {
        EventWorld w;
        AntUnit& s0 = w.add(10);                                     // ids that are not the place in the table: 10, 5, 99, 1, 7, 8
        AntUnit& s1 = w.add(5);
        AntUnit& s2 = w.add(99);
        AntUnit& s3 = w.add(1);
        AntUnit& s4 = w.add(7);
        AntUnit& s5 = w.add(8);
        ASSERT_TRUE(w.next(1000) == "none");
        w.combevt(s0, 100);
        w.loco(s1, 100);
        w.pause(s1, 100);
        w.burn(s2, 100);
        w.loco(s3, 100);
        w.loco(s4, 60);
        w.burn(s5, 130);
        ASSERT_TRUE(w.next(59) == "none");                           // nothing is due before 60
        ASSERT_TRUE(w.next(60) == "4:L@60");
        ASSERT_TRUE(w.next(99) == "4:L@60");
        // run them all: by time, then the place (slot 0 before 1 before 2 before 3, though ant 1 is in slot 3), then the kind (the animation frame before the pause)
        std::vector<std::string> ran;
        for (int guard = 0; guard < 20; ++guard) {
            const std::string text = w.run(150, EventWorld::finish);
            if (text == "none") break;
            ran.push_back(text);
        }
        ASSERT_TRUE(ran == (std::vector<std::string>{"4:L@60", "0:C@100", "1:L@100", "1:P@100", "2:B@100", "3:L@100", "5:B@130"}));
        ASSERT_TRUE(w.next(kNever - 1) == "none");
        // an event after the end of the tick stays; the tick that reaches it takes it
        w.loco(s2, 151);
        ASSERT_TRUE(w.next(150) == "none" && w.next(151) == "2:L@151");
        // handler that leaves the event as it was: the event is next again (the scan would take it again); one that moves it later: it goes behind the others
        w.loco(s1, 200);
        w.loco(s3, 200);
        ASSERT_TRUE(w.run(300, [](AntUnit&, AntEventKind) {}) == "2:L@151");                              // (nothing changed: the same event is next)
        ASSERT_TRUE(w.next(300) == "2:L@151");
        ASSERT_TRUE(w.run(300, [](AntUnit& a, AntEventKind) { a.loco.next_ms = 250; }) == "2:L@151");   // later than the others
        ASSERT_TRUE(w.next(300) == "1:L@200");
        ASSERT_TRUE(w.run(300, [](AntUnit& a, AntEventKind) { a.loco.next_ms = 150; }) == "1:L@200");   // earlier than the others
        ASSERT_TRUE(w.next(300) == "1:L@150");
        w.loco(s0, 150);                                              // a tie of two ants at 150: the lower place first
        ASSERT_TRUE(w.next(300) == "0:L@150");
    } TEST_END();

    TEST_CASE("L4.3 Next ant event: what makes an event pending, one condition at a time (a removed ant keeps its animation and pause, a clip of one frame or a cursor of 0 has no frame due, an event at the largest time never falls due)") {
        EventWorld w;
        AntUnit& a = w.add(1);
        auto none = [&w]() { return w.next(kNever - 1) == "none" && w.next(kNever) == "none"; };
        // the animation frame: the action, a valid clip, a clip of more than one frame, a cursor that is not 0, a time that is not the largest number
        w.loco(a, 40);
        ASSERT_TRUE(w.next(40) == "0:L@40");
        a.loco_action = AntUnit::kActionNone;                          // no action: no frame, and no pause either
        w.impl.event_refresh(a);
        ASSERT_TRUE(none());
        a.loco_action = AntUnit::kActionIdle;                          // action 0 is an action
        w.impl.event_refresh(a);
        ASSERT_TRUE(w.next(40) == "0:L@40");
        a.loco.cursor = 0;
        w.impl.event_refresh(a);
        ASSERT_TRUE(none());
        a.loco.cursor = 2;
        a.loco.clip = w.single;
        w.impl.event_refresh(a);
        ASSERT_TRUE(none());
        a.loco.clip = movement::MotionClip{};
        w.impl.event_refresh(a);
        ASSERT_TRUE(none());
        a.loco.clip = movement::MotionClip{};                          // two frames but no frame list: not a valid clip
        a.loco.clip.count = 2;
        w.impl.event_refresh(a);
        ASSERT_TRUE(none());
        a.loco.clip = w.multi;
        a.loco.next_ms = kNever;
        w.impl.event_refresh(a);
        ASSERT_TRUE(none());
        a.loco.next_ms = kNever - 1;
        w.impl.event_refresh(a);
        ASSERT_TRUE(w.next(kNever - 1) == "0:L@4294967294" && w.next(kNever - 2) == "none");
        a.loco.next_ms = 0;                                            // the time 0 is a time, for the animation (the burn and the combat timer use 0 for "none")
        w.impl.event_refresh(a);
        ASSERT_TRUE(w.next(0) == "0:L@0");
        w.loco(a, 40);
        // the pause
        a.loco.cursor = 0;
        w.pause(a, 70);
        ASSERT_TRUE(w.next(70) == "0:P@70" && w.next(69) == "none");
        a.pause_active = false;
        w.impl.event_refresh(a);
        ASSERT_TRUE(none());
        w.pause(a, kNever);
        ASSERT_TRUE(none());
        w.pause(a, 0);                                                 // a pause fires at 0 too
        ASSERT_TRUE(w.next(0) == "0:P@0");
        a.loco_action = AntUnit::kActionNone;
        w.impl.event_refresh(a);
        ASSERT_TRUE(none());
        a.pause_active = false;
        // the burn overlay and the combat timer: not 0, not the largest number, and the ant not removed
        w.burn(a, 55);
        w.combevt(a, 65);
        ASSERT_TRUE(w.next(54) == "none" && w.next(55) == "0:B@55" && w.next(1000) == "0:B@55");
        w.burn(a, 0);
        ASSERT_TRUE(w.next(1000) == "0:C@65");
        w.combevt(a, 0);
        ASSERT_TRUE(none());
        w.burn(a, kNever);
        w.combevt(a, kNever);
        ASSERT_TRUE(none());
        w.burn(a, 55);
        w.combevt(a, 65);
        a.removed = true;                                              // removed: the burn and the combat timer are gone ...
        w.impl.event_refresh(a);
        ASSERT_TRUE(none());
        w.loco(a, 40);                                                 // ... the animation frame and the pause are not looked at for removed (the scan did not)
        w.pause(a, 30);
        ASSERT_TRUE(w.next(100) == "0:P@30");
        a.removed = false;
        w.impl.event_refresh(a);
        ASSERT_TRUE(w.next(100) == "0:P@30");                          // (30 is the earliest of the four)
        // an ant that is not in the table has no event; an ant in the table that was copied keeps no link to the heap of the original
        AntUnit stray(77, TeamId::Black, AntType::Worker, 0, 0);
        stray.burn_end_ms = 1;
        w.impl.event_refresh(stray);
        ASSERT_TRUE(w.next(100) == "0:P@30");
    } TEST_END();

    TEST_CASE("L4.4 Event heap: after every change of a field, copy, assignment, move, restart and growth of the table the heap equals the ants' fields and the next event the scan's (random operations, ties, ids that are not the place, holes)") {
        uint32_t s = 20260;
        auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
        auto time_of = [&below]() { const uint32_t r = below(20); return r == 0 ? kNever : r == 1 ? 0u : below(7) * 50u; };   // few times: many ties
        EventWorld w;
        std::vector<SimulationEngineImpl> spares;
        uint64_t compared = 0, ties = 0;
        uint32_t next_id = 1;
        auto add_ant = [&]() {
            const uint32_t id = below(3) == 0 ? 1000u + below(100000) : next_id++;      // ids of every kind (some are not the place in the table)
            w.add(id);
        };
        for (int i = 0; i < 12; ++i) add_ant();
        auto any_ant = [&]() -> AntUnit* {
            for (uint32_t tries = 0; tries < 8; ++tries) {
                const uint32_t i = below(static_cast<uint32_t>(w.impl.ants_.size() + 1));
                if (i < w.impl.ants_.size() && w.impl.ants_[i]) return w.impl.ants_[i].get();
            }
            return nullptr;
        };
        for (int step = 0; step < 6000; ++step) {
            const uint32_t op = below(100);
            if (op < 40) {                                               // a field changes, as in the simulation: the field, then event_refresh
                if (AntUnit* a = any_ant()) {
                    switch (below(5)) {
                        case 0: a->burn_end_ms = time_of(); break;
                        case 1: a->combevt_due_ms = time_of(); break;
                        case 2:
                            a->loco_action = below(4) == 0 ? AntUnit::kActionNone : (below(2) == 0 ? AntUnit::kActionWalk : AntUnit::kActionIdle);
                            a->loco.clip = below(5) == 0 ? movement::MotionClip{} : (below(4) == 0 ? w.single : w.multi);
                            a->loco.cursor = static_cast<uint16_t>(below(3));
                            a->loco.next_ms = time_of();
                            break;
                        case 3:
                            a->pause_active = below(3) != 0;
                            a->pause_fire_ms = time_of();
                            if (below(3) == 0) a->loco_action = AntUnit::kActionNone;
                            break;
                        default:
                            a->removed = below(2) == 0;
                            if (a->removed && below(2) == 0) a->loco_action = AntUnit::kActionNone;
                            break;
                    }
                    w.impl.event_refresh(*a);
                }
            } else if (op < 70) {                                        // the tick's loop: the next event runs, its handler changes the ant, the ant is set again
                const uint32_t t_end = below(8) == 0 ? kNever : below(7) * 50u;
                AntEventPick p;
                if (w.impl.next_ant_event(t_end, p)) {
                    AntUnit& a = *p.ant;
                    switch (below(4)) {
                        case 0: EventWorld::finish(a, p.kind); break;
                        case 1: if (p.kind == AntEventKind::Loco) a.loco.next_ms += below(3) * 50u; else if (p.kind == AntEventKind::Pause) a.pause_fire_ms += 50; else EventWorld::finish(a, p.kind); break;
                        case 2: if (p.kind == AntEventKind::Loco && a.loco.next_ms >= 50) a.loco.next_ms -= 50; break;
                        default: break;                                  // the event is left as it was: it is next again
                    }
                    w.impl.event_refresh(a);
                }
            } else if (op < 78) {
                if (w.impl.ants_.size() < 50) {
                    if (below(8) == 0) w.impl.ants_.push_back(AntPtr());   // a hole
                    else add_ant();
                }
            } else if (op < 83) {
                spares.push_back(w.impl);                                // copy construction: the copy answers as the original
                SimulationEngineImpl& c = spares.back();
                c.event_heap_verify();
                const uint32_t t_end = below(7) * 50u;
                AntEventPick pa, pc;
                const bool fa = w.impl.next_ant_event(t_end, pa), fc = c.next_ant_event(t_end, pc);
                ASSERT_TRUE(fa == fc && (!fa || (pa.slot == pc.slot && pa.time == pc.time && pa.kind == pc.kind && pc.ant == c.ants_[pc.slot].get())));
            } else if (op < 87 && !spares.empty()) {
                w.impl = spares[below(static_cast<uint32_t>(spares.size()))];   // copy assignment over an engine that has ants and events of its own
            } else if (op < 90) {
                SimulationEngineImpl moved(std::move(w.impl));          // move, and back
                w.impl = std::move(moved);
            } else if (op < 92 && !spares.empty()) {
                SimulationEngineImpl source(spares.back());             // an engine that was moved from, assigned to: it is a whole engine again
                SimulationEngineImpl taker(std::move(source));
                source = spares[below(static_cast<uint32_t>(spares.size()))];
                source.event_heap_verify();
                w.impl = std::move(source);
                (void)taker;
            } else if (op < 93 && below(2) == 0) {
                w.impl.ants_.clear();                                    // a new match: no ants, the heap starts again
                w.impl.movement_reset();
                next_id = 1;
                for (int i = 0; i < 4; ++i) add_ant();
            } else if (spares.size() > 6) {
                spares.erase(spares.begin());
            }
            w.impl.event_heap_verify();                                   // every ant's four events against the heap, and the heap's own order
            for (const uint32_t t_end : {0u, 100u, 175u, 300u, kNever - 1, kNever}) {
                const std::string text = w.next(t_end);                   // (the heap against the scan, and the pick itself)
                ASSERT_TRUE(text.compare(0, 4, "heap") != 0);
                ++compared;
            }
            if (step % 7 == 0) {
                size_t at_100 = 0;
                for (const auto& up : w.impl.ants_) at_100 += (up && ((up->burn_end_ms == 100 && !up->removed) || up->pause_fire_ms == 100)) ? 1u : 0u;
                ties += at_100 > 1 ? 1u : 0u;
            }
        }
        for (SimulationEngineImpl& sp : spares) sp.event_heap_verify();
        std::cout << "\n        " << compared << " selections compared with the scan, " << ties << " moments with ties at 100   ... ";
        ASSERT_TRUE(compared > 30000);
        ASSERT_TRUE(ties > 20);
    } TEST_END();

    TEST_CASE("L4.6 Event heap: every function that writes the fields of the events leaves the heap as the fields say, called by itself on an ant that has events (animation play, release, pause start, cancel and fire, an action, the combat timer, the burn overlay, a retreat, a removal)") {
        // a clip that is played over a pending animation frame: an invalid clip starts nothing, so nothing falls due any more
        {
            EventWorld w;
            AntUnit& a = w.add(1);
            w.loco(a, 40);
            w.impl.loco_play(a, movement::MotionClip{}, 4);
            w.impl.event_heap_verify();
            ASSERT_TRUE(w.next(kNever - 1) == "none");
            w.impl.loco_play(a, w.multi, 4);                             // a valid one: its start step runs, the next frame falls due
            w.impl.event_heap_verify();
            ASSERT_TRUE(w.next(kNever - 1) != "none");
        }
        // released: no action, no pause, so no frame and no pause
        {
            EventWorld w;
            AntUnit& a = w.add(1);
            w.loco(a, 40);
            w.pause(a, 30);
            ASSERT_TRUE(w.next(100) == "0:P@30");
            w.impl.loco_release(a);
            w.impl.event_heap_verify();
            ASSERT_TRUE(w.next(kNever - 1) == "none");
        }
        // a pause starts (the ant has an action; it ends 300 ms later), is cancelled, starts again and fires
        {
            EventWorld w;
            AntUnit& a = w.add(1);
            w.loco(a, 500);
            w.impl.now_ms_ = 100;
            w.impl.start_pause(a);
            w.impl.event_heap_verify();
            ASSERT_TRUE(w.next(1000) == "0:P@400");
            w.impl.cancel_pause(a);
            w.impl.event_heap_verify();
            ASSERT_TRUE(w.next(1000) == "0:L@500");
            w.impl.start_pause(a);
            a.pause_saved_action = AntUnit::kActionIdle;
            w.impl.pause_fire(a);                                        // the pause is over: the ant plays its idle clip again
            w.impl.event_heap_verify();
            ASSERT_TRUE(!a.pause_active && w.next(1000).find(":P@") == std::string::npos);
            w.impl.set_action(a, AntUnit::kActionWalk, 2, -1, -1, false);   // an action
            w.impl.event_heap_verify();
            w.impl.set_action(a, 0x30, 2, -1, -1, false);                // (an action that the switch does not know sets the field and plays nothing)
            w.impl.event_heap_verify();
        }
        // the combat timer
        {
            EventWorld w;
            AntUnit& b = w.add(2);
            w.impl.now_ms_ = 1000;
            w.impl.start_combat_timer(b, 250);
            w.impl.event_heap_verify();
            ASSERT_TRUE(w.next(1249) == "none" && w.next(1250) == "0:C@1250");
            w.impl.cancel_combat_timer(b);
            w.impl.event_heap_verify();
            ASSERT_TRUE(w.next(kNever - 1) == "none");
        }
        // the burn overlay starts, and ends (an ant that is dying when its overlay ends: nothing after the end sets its events again)
        {
            EventWorld w;
            AntUnit& b = w.add(2);
            w.impl.anim_clock_ms_ = 2000;
            w.impl.burn_overlay_start(b);
            w.impl.event_heap_verify();
            ASSERT_TRUE(b.burn_end_ms > 2000 && w.next(kNever - 1) == "0:B@" + std::to_string(b.burn_end_ms));
            b.hp = 0;
            b.loco_action = AntUnit::kActionDeath;
            w.impl.event_refresh(b);
            w.impl.burn_overlay_end(b);
            w.impl.event_heap_verify();
            ASSERT_TRUE(b.burn_end_ms == 0 && w.next(kNever - 1) == "none");
        }
        // a retreat: a 1 hp ant that has no action (what a released ant has) is put on action 0 by low_hp_check, and its leftover clip is an animation again
        {
            EventWorld w;
            AntUnit& c = w.add(3);
            w.loco(c, 40);
            c.loco_action = AntUnit::kActionNone;
            w.impl.event_refresh(c);
            ASSERT_TRUE(w.next(100) == "none");
            c.hp = 1;
            ASSERT_TRUE(w.impl.low_hp_check(c));
            w.impl.event_heap_verify();
            ASSERT_TRUE(c.loco_action == AntUnit::kActionIdle && w.next(100) == "0:L@40");
        }
        // a pause that ends on an ant whose old action cannot be left (a death that is over: finish_death does nothing for a removed ant): set_action returns before it writes
        // anything, and pause_fire has cleared pause_active all the same
        {
            EventWorld w;
            AntUnit& a = w.add(1);
            w.pause(a, 30);
            a.removed = true;
            a.loco_action = AntUnit::kActionDeath;
            a.pause_saved_action = AntUnit::kActionIdle;
            w.impl.event_refresh(a);
            ASSERT_TRUE(w.next(100) == "0:P@30");
            w.impl.pause_fire(a);
            w.impl.event_heap_verify();
            ASSERT_TRUE(!a.pause_active && w.next(kNever - 1).find(":P@") == std::string::npos);
        }
        // a removal: every event of the ant is gone
        {
            EventWorld w;
            AntUnit& d = w.add(4);
            w.loco(d, 10);
            w.pause(d, 20);
            w.burn(d, 30);
            w.combevt(d, 40);
            ASSERT_TRUE(w.next(100) == "0:L@10");
            w.impl.remove_ant(d);
            w.impl.event_heap_verify();
            ASSERT_TRUE(d.removed && w.next(kNever - 1) == "none");
        }
    } TEST_END();

    TEST_CASE("L4.7 Scans alone: a thread switched to the scans (LinearOnly) takes the next event from the scan, whatever the heap says (a heap that is wrong on purpose), and keeps the heap as the writers leave it") {
        EventWorld w;
        AntUnit& a = w.add(1);
        w.add(2);
        w.loco(a, 60);
        w.impl.event_heap_.set(0, AntEventKind::Loco, true, 5);          // the heap says 5; the ant says 60
        w.impl.event_heap_.set(1, AntEventKind::Burn, true, 7);          // and an event of an ant that has none
        {
            LinearOnly scans;
            AntEventPick p;
            ASSERT_TRUE(w.impl.next_ant_event(100, p) && p.slot == 0 && p.time == 60 && p.kind == AntEventKind::Loco);
            ASSERT_TRUE(!w.impl.next_ant_event(59, p));
            a.loco.cursor = 0;                                           // a writer in the scan engine: the heap is kept in step (the check and the copies need it)
            w.impl.event_refresh(a);
            ASSERT_TRUE(!w.impl.next_ant_event(100, p));
        }
        w.impl.event_heap_ = AntEventHeap();                             // the engine of the scans never reads it: it is empty here, and the answer is still the scan's
        w.loco(a, 80);
        {
            LinearOnly scans;
            w.impl.event_heap_ = AntEventHeap();
            AntEventPick p;
            ASSERT_TRUE(w.impl.next_ant_event(100, p) && p.time == 80);
        }
    } TEST_END();

    TEST_CASE("L4.5 Engine copy: a copy of an engine (construction, assignment, an assignment into a moved-from engine) has the heap of its source, plays on to the same state hash, and a restart empties the heap") {
        SimulationEngine a;
        a.init_test_world(60, 60, 5);
        std::vector<uint32_t> ids;
        for (int i = 0; i < 40; ++i) ids.push_back(a.spawn_unit(static_cast<uint8_t>(i % 4), i % 7 == 0 ? AntType::Combat : AntType::Worker, TileCoord{10 + i % 20, 10 + i / 20 * 3}, false));
        Command c;
        c.type = CommandType::GroupMove;
        c.issuer = 0;
        c.tile_x = 40;
        c.tile_y = 40;
        for (size_t i = 0; i < 20; i += 4) c.ants.push_back(ids[i]);
        (void)a.apply_command(c);
        for (int t = 0; t < 40; ++t) a.tick();
        const uint64_t verifies0 = lookup_shadow_event_verifies();
        SimulationEngine copy(a);                                        // (the shadow build checks the copy's heap against its ants inside the copy)
        ASSERT_TRUE(lookup_shadow_event_verifies() == verifies0 + 1);
        SimulationEngine assigned;
        assigned = a;
        ASSERT_TRUE(lookup_shadow_event_verifies() == verifies0 + 2);   // (and so is an assignment)
        SimulationEngine moved(std::move(a));
        ASSERT_TRUE(lookup_shadow_event_verifies() == verifies0 + 2);   // (a move changes no ant: nothing to check)
        a = copy;                                                        // an assignment into the engine that was moved from
        ASSERT_TRUE(lookup_shadow_event_verifies() == verifies0 + 3);
        for (int t = 0; t < 120; ++t) {
            a.tick();
            copy.tick();
            assigned.tick();
            moved.tick();
            ASSERT_TRUE(a.state_hash() == copy.state_hash() && copy.state_hash() == assigned.state_hash() && assigned.state_hash() == moved.state_hash());
        }
        // restart: the same match again from the start on an engine that played one equals a fresh engine's
        moved.init_test_world(60, 60, 5);
        SimulationEngine fresh;
        fresh.init_test_world(60, 60, 5);
        ASSERT_TRUE(moved.state_hash() == fresh.state_hash());
        for (int i = 0; i < 6; ++i) {
            moved.spawn_unit(static_cast<uint8_t>(i % 4), AntType::Worker, TileCoord{10 + i, 10}, false);
            fresh.spawn_unit(static_cast<uint8_t>(i % 4), AntType::Worker, TileCoord{10 + i, 10}, false);
        }
        for (int t = 0; t < 30; ++t) {
            moved.tick();
            fresh.tick();
            ASSERT_TRUE(moved.state_hash() == fresh.state_hash());
        }
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
            {
                LinearOnly scans;   // (the twin is set up by the scans alone too, so that it shares nothing with the lists)
                t.old_engine.init(level, ++seed, 0x0F);
            }
            t.engine.init(level, seed, 0x0F);
            uint64_t checks = 0;
            const Marks marks;
            const std::string why = play(t, &level, 0x0F, ticks, nullptr, &checks);
            std::cout << "\n        " << file << ": " << t.ticks << " ticks, " << checks << " lookups checked, " << t.pending_seen << " queued requests seen, " << marks.since() << "   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(checks > 1000);
            ASSERT_TRUE(marks.changes_since() == 0 && marks.stale_since() == 0);   // no ant needed the start-of-tick pass, no selection met a stale entry
        }
    } TEST_END();

    TEST_CASE("L2.2 Big Maps Of The Library (read only), the same, 600 ticks: many ants, fights, long searches") {
        uint32_t seed = 70;
        const std::string dir = std::string(ANTS_SOURCE_DIR) + "/Community-Maps";
        for (const char* file : {"WAR OF ALL ants.lvl", "GrassHopper.lvl", "ARMIES OF FATE.lvl", "AnTsCaStLe.lvl", "Jungle.lvl", "OCEAN.LVL", "4cornersn3.lvl"}) {
            const auto& level = library_map(dir, file);
            Twin t;
            {
                LinearOnly scans;   // (the twin is set up by the scans alone too, so that it shares nothing with the lists)
                t.old_engine.init(level, ++seed, 0x0F);
            }
            t.engine.init(level, seed, 0x0F);
            uint64_t checks = 0;
            const Marks marks;
            const std::string why = play(t, &level, 0x0F, ticks, nullptr, &checks);
            std::cout << "\n        " << file << ": " << t.ticks << " ticks, " << checks << " lookups checked, " << t.pending_seen << " queued requests seen, " << marks.since() << "   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(checks > 1000);
            ASSERT_TRUE(marks.changes_since() == 0 && marks.stale_since() == 0);   // no ant needed the start-of-tick pass, no selection met a stale entry
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
    std::vector<uint32_t> pit;                        // the ants beside the bombs (40, team i % 4), beside the power-ups (8) and the river (8 swimmers), then the combat ants and workers of the melees
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
    for (int32_t y = mid - 12; y <= mid - 10; ++y) {
        for (int32_t x = mid - 10; x <= mid + 6; ++x) e.set_terrain(x, y, 2);  // a river that the swimmers bridge
    }
    for (int32_t i = 0; i < 8; ++i) e.grid_mut().place_powerup(mid - 16 + 3 * i, mid + 8, static_cast<uint8_t>(i % 5));   // power-ups on a row below the middle
    for (int32_t i = 0; i < 40; ++i) {                                          // a row of bombs, each with an ant two tiles below it: sent onto its bomb, 20 % of the bombs are duds (the ant burns)
        const uint8_t p = static_cast<uint8_t>(i % 4);
        e.grid_mut().place_bomb(static_cast<uint32_t>(mid - 20 + i), static_cast<uint32_t>(mid + 14), static_cast<uint8_t>((p + 1) % 4));
        crowd.pit.push_back(e.spawn_unit(p, AntType::Worker, TileCoord{mid - 20 + i, mid + 16}, false));
    }
    for (int32_t i = 0; i < 8; ++i) {                                           // workers two tiles from the power-ups, sent onto them
        crowd.pit.push_back(e.spawn_unit(static_cast<uint8_t>(i % 4), AntType::Worker, TileCoord{mid - 16 + 3 * i, mid + 10}, false));
    }
    for (int32_t i = 0; i < 8; ++i) {                                           // swimmers on the bank of the river, sent to dig into it (a bridge)
        crowd.pit.push_back(e.spawn_unit(static_cast<uint8_t>(i % 4), AntType::Swimmer, TileCoord{mid - 8 + 2 * i, mid - 9}, false));
    }
    for (int32_t k = 0; k < 4; ++k) {                                           // melees: a combat ant of team 0 beside a worker of team 1 (the combat ant strikes by its own reflex, the combat timer)
        crowd.pit.push_back(e.spawn_unit(0, AntType::Combat, TileCoord{mid - 30 + 4 * k, mid - 24}, false));
        crowd.pit.push_back(e.spawn_unit(1, AntType::Worker, TileCoord{mid - 28 + 4 * k, mid - 24}, false));
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

// The loco actions that the typed ants and the ants of the pit were seen in (so that a test shows that power-ups were taken and bridges dug)
struct Seen {
    std::set<uint8_t> actions;
};
Seen g_seen;

// What happens besides the orders of crowd_script (the same calls in both engines): a fire wall across the middle of the world at tick 30 and bombs beside it at tick 31 (ants
// that walk or are thrown onto them, two ants of a team sent to each bomb at tick 33: some bombs are duds, the ant burns), the typed ants sent to the middle at tick 5, the
// swimmers ordered onto the river at ticks 25 and 125 (a bridge, then it is torn down) and, the typed ants of the crowd, at 60 and 160, the workers of the pit onto the power-ups at tick 20, special orders at tick 220, an
// egg hatched for each team every 45 ticks, a third of the ants of team 1 killed at tick 200 (a mass death) and team 2 leaving the match at tick 270 (all its ants die)
Script hazard_script(const Crowd& crowd, uint32_t width) {
    const Script orders = crowd_script(crowd, width);
    return [crowd, width, orders](Twin& t, uint32_t tick, std::vector<Command>& out) {
        orders(t, tick, out);
        const int32_t mid = static_cast<int32_t>(width / 2);
        std::vector<uint32_t> watched = crowd.pit;
        for (uint8_t p = 0; p < 4; ++p) watched.insert(watched.end(), crowd.special[p].begin(), crowd.special[p].end());
        for (const uint32_t id : watched) {
            const AntUnit& u = t.engine.get_unit(id);
            if (u.is_alive()) g_seen.actions.insert(u.loco_action);
        }
        auto some = [&](uint8_t p, const std::vector<uint32_t>& ids, size_t from, size_t count, CommandType type, int32_t x, int32_t y) {
            Command c;
            c.type = type;
            c.issuer = p;
            c.tile_x = static_cast<int16_t>(x);
            c.tile_y = static_cast<int16_t>(y);
            for (size_t j = from; j < from + count && j < ids.size(); ++j) c.ants.push_back(ids[j]);
            if (!c.ants.empty()) out.push_back(c);
        };
        if (tick == 15) {
            for (size_t i = 0; i < 40 && i < crowd.pit.size(); ++i) {
                some(static_cast<uint8_t>(i % 4), crowd.pit, i, 1, CommandType::GroupMove, mid - 20 + static_cast<int32_t>(i), mid + 14);
            }
        }
        if (tick == 20) {
            for (size_t i = 0; i < 8 && 40 + i < crowd.pit.size(); ++i) {
                some(static_cast<uint8_t>(i % 4), crowd.pit, 40 + i, 1, CommandType::GroupMove, mid - 16 + 3 * static_cast<int32_t>(i), mid + 8);
            }
        }
        if (tick == 33) {
            for (int32_t j = 0; j < 15; ++j) some(static_cast<uint8_t>(j % 4), crowd.ids[static_cast<size_t>(j % 4)], static_cast<size_t>(2 * (j / 4)), 2, CommandType::GroupMove, mid - 14 + 2 * j, mid - 4);
        }
        if (tick == 25 || tick == 125) {
            for (size_t i = 0; i < 8 && 48 + i < crowd.pit.size(); ++i) {
                some(static_cast<uint8_t>(i % 4), crowd.pit, 48 + i, 1, CommandType::GroupSpecial, mid - 8 + 2 * static_cast<int32_t>(i), mid - 10);
            }
        }
        if (tick == 60 || tick == 160) {
            for (uint8_t p = 0; p < 4; ++p) {
                const std::vector<uint32_t>& sp = crowd.special[p];
                if (sp.size() >= 50) some(p, sp, 40, 10, CommandType::GroupSpecial, mid - 8 + 4 * p, mid - 11);
            }
        }
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
        if (tick == 270) t.both([](SimulationEngine& e) { e.drop_player(2); });
    };
}

std::string play_crowd(uint32_t width, uint32_t per_team, Layout layout, uint32_t ticks, uint64_t* checks, uint64_t* pending_seen) {
    g_seen = Seen{};
    Twin t;
    t.engine.init_test_world(width, width, 9);
    Crowd crowd = populate(t.engine, width, per_team, layout);
    add_hazards(t.engine, width, crowd);
    Crowd same;
    {
        LinearOnly scans;   // (the twin is set up by the scans alone too)
        t.old_engine.init_test_world(width, width, 9);
        same = populate(t.old_engine, width, per_team, layout);
        add_hazards(t.old_engine, width, same);
    }
    if (same.ids != crowd.ids || same.special != crowd.special || same.pit != crowd.pit) return "the two worlds hold different ants";
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
        cases.push_back({"4 x 1500 spread, 150 x 150", 150, 1500, Layout::Spread, 300});
        cases.push_back({"4 x 1500 in piles of 24, 150 x 150", 150, 1500, Layout::Piled, 300});
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
            ASSERT_TRUE(marks.changes_since() == 0 && marks.stale_since() == 0);               // no ant needed the start-of-tick pass, no selection met a stale entry
            ASSERT_TRUE(marks.catch_ups_since() > 1000);                                        // the start-of-tick pass ran over the ants, and (it stops the program otherwise) found nothing to put right
            ASSERT_TRUE(marks.events_since() > 1000 && marks.heaps_since() >= n);                // the heap chose events, and was checked against the ants at every tick
            for (size_t kind = 0; kind < 4; ++kind) ASSERT_TRUE(marks.picks_since(kind) > 0);    // events of every kind fell due: a burn overlay, a combat timer, a frame, a pause
            ASSERT_TRUE(g_seen.actions.count(AntUnit::kActionGetPow) != 0);                     // a power-up was taken
            ASSERT_TRUE(g_seen.actions.count(AntUnit::kActionBridgeBuild) != 0);                // a swimmer dug a bridge
        }
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Writes from outside: what no writer of the heap follows, and the two nets of a product build that put it right
// ---------------------------------------------------------------------------------------------------------------------------------

// A test hook can write the fields of an ant between two ticks (get_unit); the heap does not hear of it. In a match the nets must never find such a thing (the shadow build stops
// the program, and the tests above count 0 after every match); here the writes are made on purpose, with the nets set to count (CountStale), and the engine must play on as the
// scans do (the twin, the hash at every tick).
void run_foreign_write_tests() {
    const uint32_t kFar = 3600000u;   // (a time that no test match reaches)
    TEST_CASE("L4.8 Writes of the event fields from outside, between ticks: each one is counted by the start-of-tick pass (once in each engine of the twin), none is counted when nothing is written, and the hash equals the scans' at every tick") {
        Twin t;
        t.engine.init_test_world(90, 90, 9);
        const Crowd crowd = populate(t.engine, 90, 12, Layout::Spread);
        {
            LinearOnly scans;
            t.old_engine.init_test_world(90, 90, 9);
            populate(t.old_engine, 90, 12, Layout::Spread);
        }
        const CountStale counting;
        const Marks marks;
        auto tick = [&](uint32_t n) -> int64_t {                         // n ticks; the ants whose events the pass had to put right in them (both engines); -1: the hashes differ
            const uint64_t before = marks.changes_since();
            for (uint32_t i = 0; i < n; ++i) {
                if (!t.tick()) return -1;
            }
            return static_cast<int64_t>(marks.changes_since() - before);
        };
        auto poke = [&](uint32_t id, const std::function<void(AntUnit&)>& write) {
            t.both([&](SimulationEngine& e) { write(e.get_unit(id)); });
        };
        auto now = [&] { return static_cast<uint32_t>(t.engine.current_tick()) * 50u; };
        const uint32_t a = crowd.ids[0][0], b = crowd.ids[1][0], c = crowd.ids[2][0], d = crowd.ids[3][0], e = crowd.ids[0][1], f = crowd.ids[3][1];
        ASSERT_TRUE(tick(3) == 0);                                       // quiet ticks: nothing to put right
        poke(a, [&](AntUnit& u) { u.combevt_due_ms = kFar; });          // an event that was not pending is
        ASSERT_TRUE(tick(1) == 2);
        poke(a, [&](AntUnit& u) { u.combevt_due_ms = kFar + 1000u; });  // an event whose time moved
        ASSERT_TRUE(tick(1) == 2);
        poke(a, [&](AntUnit& u) { u.combevt_due_ms = 0; });             // an event that is not pending any more
        ASSERT_TRUE(tick(1) == 2);
        poke(b, [&](AntUnit& u) { u.burn_end_ms = kFar; });
        ASSERT_TRUE(tick(1) == 2);
        poke(b, [&](AntUnit& u) { u.burn_end_ms = 0; });
        ASSERT_TRUE(tick(1) == 2);
        ASSERT_TRUE(t.engine.get_unit(c).loco.clip.valid() && t.engine.get_unit(c).loco.cursor != 0 && t.engine.get_unit(c).loco.clip.count > 1 && t.engine.get_unit(c).loco_action != AntUnit::kActionNone);
        poke(c, [&](AntUnit& u) { u.loco.next_ms = kFar; });            // the next frame of the animation moved
        ASSERT_TRUE(tick(1) == 2);
        ASSERT_TRUE(tick(20) == 0);                                      // nothing written, nothing found (the counting does not make the pass see more)
        poke(d, [&](AntUnit& u) { u.burn_end_ms = now() + 100u; });     // events that fall due at once: the engine plays them as the scans do
        poke(e, [&](AntUnit& u) { u.combevt_due_ms = now() + 100u; });
        ASSERT_TRUE(tick(8) == 4);
        Command go;                                                      // an ant on its way, and a pause that another system starts under it
        go.type = CommandType::GroupMove;
        go.issuer = 3;
        go.tile_x = 80;
        go.tile_y = 20;
        go.ants.push_back(f);
        ASSERT_TRUE(t.apply(go));
        ASSERT_TRUE(tick(4) == 0);
        poke(f, [&](AntUnit& u) { u.pause_active = true; u.pause_fire_ms = now() + 100u; });
        ASSERT_TRUE(tick(6) == 2);
        ASSERT_TRUE(tick(30) == 0);
        ASSERT_TRUE(marks.stale_since() == 0);                           // (the selections met no stale entry: the pass had put everything right before them)
    } TEST_END();

    TEST_CASE("L4.9 A selection that meets an entry the ant does not bear out (a field written with no event_refresh): counted, the ant put right, and the answer is the scan's") {
        EventWorld w;
        AntUnit& a = w.add(1);
        w.add(2);
        w.impl.ants_.push_back(AntPtr());                              // a hole in the table
        w.loco(a, 60);
        w.burn(a, 30);
        const CountStale counting;
        const Marks marks;
        a.burn_end_ms = 45;                                              // the heap says 30
        ASSERT_TRUE(w.next(100) == "0:B@45" && marks.stale_since() == 1);
        a.burn_end_ms = 0;                                               // the heap says 45
        ASSERT_TRUE(w.next(100) == "0:L@60" && marks.stale_since() == 2);
        w.impl.event_heap_.set(1, AntEventKind::Combevt, true, 10);      // an event of an ant that has none
        ASSERT_TRUE(w.next(100) == "0:L@60" && marks.stale_since() == 3);
        w.impl.event_heap_.set(2, AntEventKind::Pause, true, 10);        // an event of a hole of the table
        ASSERT_TRUE(w.next(100) == "0:L@60" && marks.stale_since() == 4);
        w.impl.event_heap_.set(9, AntEventKind::Loco, true, 10);         // an event of a place that is not in the table
        ASSERT_TRUE(w.next(100) == "0:L@60" && marks.stale_since() == 5);
        ASSERT_TRUE(w.next(100) == "0:L@60" && marks.stale_since() == 5);   // (the heap is right now: nothing more is found)
        w.impl.event_heap_verify();
    } TEST_END();

    TEST_CASE("L3.3 Foreign writes of the state of ants (another system changes the label of an ant under the locomotion model's feet: loco_sync releases it, plays it idle or stops it), in a match with orders, bombs, fire, hatching and deaths: the hashes equal at every tick, and no net was needed") {
        const uint32_t width = 90;
        const uint32_t ticks = ANTS_LOOKUP_TEST_SANITIZED ? 300u : 600u;
        g_seen = Seen{};
        Twin t;
        t.engine.init_test_world(width, width, 9);
        Crowd crowd = populate(t.engine, width, 40, Layout::Spread);
        add_hazards(t.engine, width, crowd);
        {
            LinearOnly scans;
            t.old_engine.init_test_world(width, width, 9);
            Crowd same = populate(t.old_engine, width, 40, Layout::Spread);
            add_hazards(t.old_engine, width, same);
            ASSERT_TRUE(same.ids == crowd.ids && same.pit == crowd.pit);
        }
        std::vector<uint32_t> all;
        for (const auto& team : crowd.ids) all.insert(all.end(), team.begin(), team.end());
        all.insert(all.end(), crowd.pit.begin(), crowd.pit.end());
        const Script orders = hazard_script(crowd, width);
        static const UnitState kStates[] = {UnitState::Idle, UnitState::Walking, UnitState::Attacking, UnitState::Flinch, UnitState::Knockback, UnitState::Stunned, UnitState::EnteringBase,
                                            UnitState::Drowning, UnitState::GuardIdle, UnitState::Swimming, UnitState::Infiltrating, UnitState::CantGo};
        uint32_t seed = 0x51A7E5u;
        auto below = [&seed](uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; };
        uint64_t pokes = 0, released = 0, stopped = 0;
        std::vector<std::pair<uint32_t, uint8_t>> pending;                // the ants poked at the last tick, with the action they had
        const Script script = [&](Twin& tw, uint32_t tick, std::vector<Command>& out) {
            for (const auto& p : pending) {                               // what the poke of the last tick did to the ant
                const AntUnit& u = tw.engine.get_unit(p.first);
                if (u.is_alive() && u.loco_action == AntUnit::kActionNone && (p.second == AntUnit::kActionIdle || p.second == AntUnit::kActionWalk)) ++released;
                if (u.is_alive() && p.second == AntUnit::kActionWalk && u.loco_action == AntUnit::kActionIdle) ++stopped;
            }
            pending.clear();
            orders(tw, tick, out);
            if (tick % 3 != 1) return;
            const uint32_t id = all[below(static_cast<uint32_t>(all.size()))];
            const UnitState to = kStates[below(static_cast<uint32_t>(sizeof kStates / sizeof kStates[0]))];
            const AntUnit& u = tw.engine.get_unit(id);
            if (!u.is_alive()) return;
            pending.emplace_back(id, u.loco_action);
            ++pokes;
            tw.both([&](SimulationEngine& e) { e.get_unit(id).state = to; });
        };
        const Marks marks;
        uint64_t checks = 0;
        const std::string why = play(t, nullptr, 0x0F, ticks, script, &checks);
        std::cout << "\n        " << t.ticks << " ticks, " << pokes << " states written, " << released << " ants released, " << stopped << " walkers stopped, " << marks.since() << "   ... ";
        if (!why.empty()) std::cout << "\n        " << why << "\n";
        ASSERT_TRUE(why.empty());
        ASSERT_TRUE(pokes >= ticks / 8 && released > 5);                        // loco_sync took its release path on ants that were walking or standing
        ASSERT_TRUE(marks.changes_since() == 0 && marks.stale_since() == 0);   // every one of those writes was followed by its own refresh: the nets had nothing to do
        ASSERT_TRUE(marks.events_since() > 1000 && marks.heaps_since() >= ticks);
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// go_to refreshes the marked ants in a tick: whole matches, the mass resume of combat ants, writes from outside
// ---------------------------------------------------------------------------------------------------------------------------------

// What the shadow check of the go_to refresh did since this was made (calls in a tick, checks, refreshes that visited marked ants and were compared with the full refresh,
// the marked ants visited)
struct GoMarks {
    uint64_t calls{lookup_shadow_go_to_calls()}, checks{lookup_shadow_go_to_checks()}, compares{lookup_shadow_go_to_compares()}, visits{lookup_shadow_go_to_dirty_ants()};
    uint64_t calls_since() const { return lookup_shadow_go_to_calls() - calls; }
    uint64_t checks_since() const { return lookup_shadow_go_to_checks() - checks; }
    uint64_t compares_since() const { return lookup_shadow_go_to_compares() - compares; }
    uint64_t visits_since() const { return lookup_shadow_go_to_dirty_ants() - visits; }
    std::string since() const {
        return std::to_string(calls_since()) + " orders in a tick, " + std::to_string(checks_since()) + " checked, " + std::to_string(compares_since()) + " compared with the full refresh (" + std::to_string(visits_since()) + " marked ants visited)";
    }
};

// `per_team` Combat ants of each team on distinct tiles of the middle of a width x width world (the start markers of a big player's map: every ant has enemies next door; at
// 70 % of the tiles the combat ants end their reflex fights in the same ticks and each resumes its order in one tick)
std::vector<std::vector<uint32_t>> populate_combat(SimulationEngine& e, uint32_t width, uint32_t per_team, uint32_t percent) {
    std::vector<std::vector<uint32_t>> ids(4);
    uint32_t s = 4242;
    auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
    const uint32_t lo = 12, span = width - 24;
    std::vector<uint8_t> used(static_cast<size_t>(width) * width, 0);
    (void)percent;
    for (uint8_t p = 0; p < 4; ++p) {
        for (uint32_t i = 0; i < per_team; ++i) {
            for (;;) {
                const uint32_t x = lo + below(span);
                const uint32_t y = lo + below(span);
                if (used[static_cast<size_t>(y) * width + x] != 0) continue;
                used[static_cast<size_t>(y) * width + x] = 1;
                ids[p].push_back(e.spawn_unit(p, AntType::Combat, TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)}, true));
                break;
            }
        }
    }
    return ids;
}

void run_go_to_match_tests(bool big) {
    struct Case {
        const char* name;
        uint32_t width, per_team;
        uint32_t ticks;
        bool ordered;       // every ant sent across the map at tick 3 (in groups of 32)
        uint64_t min_calls; // go_to calls in a tick that the case must have made (the mass resume)
    };
    std::vector<Case> cases;
    if (!big) {
        cases.push_back({"4 x 150 Combat ants, 56 x 56, standing", 56, 150, 400, false, 80});
        cases.push_back({"4 x 150 Combat ants, 56 x 56, ordered across the map", 56, 150, 400, true, 80});
    } else {
        cases.push_back({"4 x 1,500 Combat ants, 120 x 120, standing", 120, 1500, 120, false, 1000});
        cases.push_back({"4 x 1,500 Combat ants, 120 x 120, ordered across the map", 120, 1500, 120, true, 1000});
    }
    TEST_CASE(big ? "G3.2 The Mass Resume Of 4 x 1,500 Combat Ants: the hashes equal at every tick" : "G3.1 Combat Ants That End Their Reflex Fights Together And Each Resume Their Order In One Tick (small, dense worlds), copies of the engine in the middle: the hashes equal at every tick") {
        for (const Case& c : cases) {
            Twin t;
            t.engine.init_test_world(c.width, c.width, 9);
            const auto ids = populate_combat(t.engine, c.width, c.per_team, 70);
            {
                LinearOnly scans;
                t.old_engine.init_test_world(c.width, c.width, 9);
                ASSERT_TRUE(populate_combat(t.old_engine, c.width, c.per_team, 70) == ids);
            }
            const Script orders = [&](Twin&, uint32_t tick, std::vector<Command>& out) {
                if (!c.ordered || tick != 3) return;
                for (uint8_t p = 0; p < 4; ++p) {
                    for (size_t i = 0; i < ids[p].size(); i += kMaxCommandAnts) {
                        Command cmd;
                        cmd.type = CommandType::GroupMove;
                        cmd.issuer = p;
                        cmd.tile_x = static_cast<int16_t>((p & 1) != 0 ? 6 : c.width - 7);
                        cmd.tile_y = static_cast<int16_t>((p & 2) != 0 ? 6 : c.width - 7);
                        for (size_t j = i; j < std::min(ids[p].size(), i + kMaxCommandAnts); ++j) cmd.ants.push_back(ids[p][j]);
                        out.push_back(cmd);
                    }
                }
            };
            const GoMarks marks;
            const uint32_t n = ANTS_LOOKUP_TEST_SANITIZED ? c.ticks / 2 : c.ticks;
            uint64_t most_in_one_tick = 0, calls_before = lookup_shadow_go_to_calls();
            std::string why;
            Twin& tw = t;
            std::vector<Command> cmds;
            for (uint32_t i = 0; i < n && why.empty(); ++i) {
                cmds.clear();
                orders(tw, i, cmds);
                for (const Command& cmd : cmds) {
                    if (!tw.apply(cmd)) why = tw.failure;
                }
                if (!why.empty()) break;
                if (!tw.tick()) why = tw.failure;
                most_in_one_tick = std::max(most_in_one_tick, lookup_shadow_go_to_calls() - calls_before);
                calls_before = lookup_shadow_go_to_calls();
                t.engine.clear_audio_events();
                t.old_engine.clear_audio_events();
                t.engine.clear_news_events();
                t.old_engine.clear_news_events();
                if (i == n / 3) t.engine = SimulationEngine(t.engine);                 // copy construction, then move
                if (i == 2 * n / 3) {
                    SimulationEngine other(t.engine);
                    t.engine = other;                                                  // copy assignment
                    t.old_engine = SimulationEngine(t.old_engine);
                }
            }
            std::cout << "\n        " << c.name << ": " << n << " ticks, " << most_in_one_tick << " orders in the busiest tick, " << marks.since() << "   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(most_in_one_tick >= c.min_calls);                              // the mass resume happened: many ants ended their fight and ordered themselves in one tick
            ASSERT_TRUE(marks.checks_since() > 0);
        }
    } TEST_END();

    TEST_CASE("G4.1 Writes of an ant's position and removal from outside, between ticks, each followed by an order with no tick in between: the order's refresh is the full one, so the state hash equals the scans' right after the command and at every tick") {
        Twin t;
        t.engine.init_test_world(60, 60, 9);
        const Crowd crowd = populate(t.engine, 60, 24, Layout::Spread);
        {
            LinearOnly scans;
            t.old_engine.init_test_world(60, 60, 9);
            populate(t.old_engine, 60, 24, Layout::Spread);
        }
        const CountStale counting;                                     // (a removal written by hand is a write of the events' fields too: the nets of the heap count it)
        uint32_t s = 0xB00B5u;
        auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
        uint64_t pokes = 0;
        for (int round = 0; round < 16; ++round) {
            for (int k = 0; k < 6; ++k) ASSERT_TRUE(t.tick());
            std::vector<uint32_t> poked;
            for (int k = 0; k < 5; ++k) {
                const uint8_t team = static_cast<uint8_t>(below(4));
                const uint32_t id = crowd.ids[team][below(static_cast<uint32_t>(crowd.ids[team].size()))];
                const int32_t px = static_cast<int32_t>(below(50 * 32)) + 64, py = static_cast<int32_t>(below(50 * 32)) + 64;
                const bool kill = k == 4 && round % 2 == 1;
                t.both([&](SimulationEngine& e) {
                    AntUnit& u = e.get_unit(id);
                    if (kill) u.removed = true; else u.set_pixel_pos(px, py);
                });
                poked.push_back(id);
                ++pokes;
            }
            Command c;                                                 // an order for other ants, then Stop for a poked one: the refresh of each order sees the writes
            c.type = CommandType::GroupMove;
            c.issuer = 0;
            c.tile_x = 30;
            c.tile_y = static_cast<int16_t>(10 + round);
            c.ants.push_back(crowd.ids[0][static_cast<size_t>(round)]);
            ASSERT_TRUE(t.apply(c));
            ASSERT_TRUE(t.old_engine.state_hash() == t.engine.state_hash());
            Command stop;
            stop.type = CommandType::Stop;
            stop.issuer = 1;
            stop.ants.push_back(crowd.ids[1][static_cast<size_t>(round)]);
            ASSERT_TRUE(t.apply(stop));
            ASSERT_TRUE(t.old_engine.state_hash() == t.engine.state_hash());
            for (int k = 0; k < 4; ++k) {                              // writes by hand and then a tick at once: the tick starts with the full refresh
                const uint32_t id = crowd.ids[static_cast<size_t>(below(4))][below(24)];
                const int32_t px = static_cast<int32_t>(below(50 * 32)) + 64, py = static_cast<int32_t>(below(50 * 32)) + 64;
                t.both([&](SimulationEngine& e) { e.get_unit(id).set_pixel_pos(px, py); });
                ++pokes;
            }
            ASSERT_TRUE(t.tick());
        }
        ASSERT_TRUE(t.tick());
        std::cout << "\n        " << pokes << " writes by hand   ... ";
    } TEST_END();

    TEST_CASE("G5.1 find_unit(0) is no ant: nullptr at once in a table of any size, never a fall back to the scan, and the other ids are as they were") {
        GoWorld w;
        const uint64_t fallbacks = lookup_shadow_fallbacks();
        for (int i = 0; i < 300; ++i) w.spawn(static_cast<uint8_t>(i % 4), 1 + i % 10, 1 + (i / 10) % 10, true);
        for (int i = 0; i < 500; ++i) ASSERT_TRUE(w.impl.find_unit(0) == nullptr);
        ASSERT_TRUE(w.impl.find_unit(1) == w.impl.ants_[0].get() && w.impl.find_unit(300) == w.impl.ants_[299].get() && w.impl.find_unit(301) == nullptr);
        ASSERT_TRUE(lookup_shadow_fallbacks() == fallbacks + 1);       // (only the id that was never handed out: 301)
        SimulationEngine e;
        e.init_test_world(30, 30, 9);
        e.spawn_unit(0, AntType::Worker, TileCoord{5, 5}, false);
        bool threw = false;
        try { (void)e.get_unit(0); } catch (const std::runtime_error&) { threw = true; }
        ASSERT_TRUE(threw);                                            // (get_unit of no ant says so, as before)
    } TEST_END();
}

// A new match on an engine that played one (a room starts the next match on the same engine)
void run_reinit_tests() {
    TEST_CASE("L2.3 init and init_test_world on an engine that was used: the ant ids restart at 1, the state is a fresh engine's, and the lookups go on answering") {
        const std::string dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
        const auto& level = library_map(dir, "TINY.LVL");
        Twin t;
        {
            LinearOnly scans;   // (what the old engine is given here, it is given by the scans alone)
            t.old_engine.init(level, 91, 0x0F);
        }
        t.engine.init(level, 91, 0x0F);
        ASSERT_TRUE(play(t, &level, 0x0F, 120, nullptr).empty());
        for (int i = 0; i < 6; ++i) {                                // more ants than a fresh match has: the id counter is well past its start
            uint32_t id_old;
            {
                LinearOnly scans;
                id_old = t.old_engine.spawn_unit(0, AntType::Worker, TileCoord{8 + i, 8}, false);
            }
            ASSERT_TRUE(id_old == t.engine.spawn_unit(0, AntType::Worker, TileCoord{8 + i, 8}, false));
        }
        // a map again: the engine that was used equals one that was never used, and its first new ant has the id that the fresh one's has
        SimulationEngine fresh;
        fresh.init(level, 92, 0x0F);
        {
            LinearOnly scans;
            t.old_engine.init(level, 92, 0x0F);
        }
        t.engine.init(level, 92, 0x0F);
        ASSERT_TRUE(t.engine.state_hash() == fresh.state_hash() && t.old_engine.state_hash() == fresh.state_hash());
        const uint32_t first_id = fresh.spawn_unit(1, AntType::Worker, TileCoord{9, 9}, false);
        ASSERT_TRUE(first_id == t.engine.spawn_unit(1, AntType::Worker, TileCoord{9, 9}, false));
        {
            LinearOnly scans;
            ASSERT_TRUE(first_id == t.old_engine.spawn_unit(1, AntType::Worker, TileCoord{9, 9}, false));
        }
        ASSERT_TRUE(t.engine.state_hash() == fresh.state_hash());
        ASSERT_TRUE(play(t, &level, 0x0F, 120, nullptr).empty());   // (find_unit never falls back to the scan: the slot of an id is its id - 1)
        // a test world on the engine that was used
        SimulationEngine fresh_world;
        fresh_world.init_test_world(90, 90, 9);
        {
            LinearOnly scans;
            t.old_engine.init_test_world(90, 90, 9);
        }
        t.engine.init_test_world(90, 90, 9);
        ASSERT_TRUE(t.engine.state_hash() == fresh_world.state_hash() && t.old_engine.state_hash() == fresh_world.state_hash());
        const Crowd crowd = populate(t.engine, 90, 12, Layout::Spread);
        {
            LinearOnly scans;
            ASSERT_TRUE(populate(t.old_engine, 90, 12, Layout::Spread).ids == crowd.ids);
        }
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
    bool go_to_only = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--big") == 0) {
            quick = false;
            big = true;
        } else if (std::strcmp(argv[i], "--go-to") == 0) {
            go_to_only = true;
        } else if (std::strcmp(argv[i], "--all") == 0) {
            quick = true;
            big = true;
        } else {
            std::cout << "usage: test_engine_lookups [--big | --all | --go-to]\n";
            return 2;
        }
    }
    std::cout << "\n=======================================================\n [ENGINE LOOKUPS SUITE] the indexes equal the scans they replaced (" << (quick ? "quick" : "") << (quick && big ? " + " : "")
              << (big ? "big" : "") << ")\n=======================================================\n";
    if (go_to_only) {
        run_go_to_tests();
        run_go_to_match_tests(false);
        quick = false;
    }
    if (quick) {
        run_lookup_tests();
        run_go_to_tests();
        run_map_tests();
        run_crowd_tests(false);
        run_event_tests();
        run_foreign_write_tests();
        run_reinit_tests();
        run_go_to_match_tests(false);
    }
    if (big) {
        run_crowd_tests(true);
        run_go_to_match_tests(true);
    }
    std::cout << "\n=======================================================\n"
              << " ENGINE LOOKUPS TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
