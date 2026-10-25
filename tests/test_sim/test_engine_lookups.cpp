// The lookups that replaced linear scans in the simulation (SimulationEngineImpl::find_unit: the ant of id n sits in slot n - 1; PathManager::has_request_for: a flag per
// ant) must give the scan's answer in every state, so that no match changes by a single bit (the lock-step state hash is the proof of that, docs/ARCHITECTURE.md).
//
// This program links ants_sim_shadow: the same sources as ants_sim, compiled with ANTS_SIM_LOOKUP_SHADOW (include/ants_sim/lookup_shadow.hpp). In that build every call of
// a lookup also runs the scan and stops the program when the two answers differ; and a thread can be switched to the scans alone, which is the simulation as it was before
// the lookups. Every match below is played by TWO engines at once, one with the scans alone (the old lookups) and one with the lookups and the check on every call, given
// the same commands (a scripted play: hatching, fights, every kind of order, Stop, engine copies in the middle), and the state hash (all eight parts) is compared at EVERY tick.
//
//   ./test_engine_lookups          the quick tests (seconds): the lookups alone, the six shipped maps, big library maps, small packed and spread worlds
//   ./test_engine_lookups --big    4 x 700 and 4 x 1,500 ants (packed in the corners, and spread over the map), 600 ticks (400 for the 1,500s; minutes: the engine that scans is the old, slow one)
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
            const std::string why = play(t, &level, 0x0F, ticks, nullptr, &checks);
            std::cout << "\n        " << file << ": " << t.ticks << " ticks, " << checks << " lookups checked, " << t.pending_seen << " queued requests seen   ... ";
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
            const std::string why = play(t, &level, 0x0F, ticks, nullptr, &checks);
            std::cout << "\n        " << file << ": " << t.ticks << " ticks, " << checks << " lookups checked, " << t.pending_seen << " queued requests seen   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(checks > 1000);
        }
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Worlds with many ants, built here: `per_team` ants of each of the four teams
// ---------------------------------------------------------------------------------------------------------------------------------

enum class Layout { Packed, Spread };

struct Crowd {
    std::vector<std::vector<uint32_t>> ids{4};   // the ants of each team, in the order they were made
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

std::string play_crowd(uint32_t width, uint32_t per_team, Layout layout, uint32_t ticks, uint64_t* checks, uint64_t* pending_seen) {
    Twin t;
    t.old_engine.init_test_world(width, width, 9);
    t.engine.init_test_world(width, width, 9);
    Crowd crowd = populate(t.engine, width, per_team, layout);
    const Crowd same = populate(t.old_engine, width, per_team, layout);
    if (same.ids != crowd.ids) return "the two worlds hold different ants";
    const std::string why = play(t, nullptr, 0x0F, ticks, crowd_script(crowd, width), checks);
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
    } else {
        cases.push_back({"4 x 700 packed, 150 x 150", 150, 700, Layout::Packed, 0});
        cases.push_back({"4 x 700 spread, 150 x 150", 150, 700, Layout::Spread, 0});
        cases.push_back({"4 x 1500 packed, 150 x 150", 150, 1500, Layout::Packed, 400});
        cases.push_back({"4 x 1500 spread, 150 x 150", 150, 1500, Layout::Spread, 400});
    }
    TEST_CASE(big ? "L3.2 Packed And Spread Worlds Of 4 x 700 And 4 x 1,500 Ants, ordered across the map, stopped, re-ordered, attacked: the hashes equal at every tick" :
                    "L3.1 Small Packed And Spread Worlds, ordered across the map, stopped, re-ordered, attacked: the hashes equal at every tick, 600 ticks") {
        const uint32_t ticks = ANTS_LOOKUP_TEST_SANITIZED ? (big ? 120u : 300u) : 600u;
        for (const Case& c : cases) {
            uint64_t checks = 0, pending = 0;
            const uint32_t n = c.ticks != 0 && c.ticks < ticks ? c.ticks : ticks;   // (the 1,500 ants of a team cost the scan engine 100 ms a tick)
            const std::string why = play_crowd(c.width, c.per_team, c.layout, n, &checks, &pending);
            std::cout << "\n        " << c.name << ": " << n << " ticks, " << checks << " lookups checked, " << pending << " queued requests seen   ... ";
            if (!why.empty()) std::cout << "\n        " << why << "\n";
            ASSERT_TRUE(why.empty());
            ASSERT_TRUE(checks > 10000);
            ASSERT_TRUE(pending > 0);
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
