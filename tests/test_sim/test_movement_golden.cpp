// Golden tests for the frame-exact port of the original 1998 ant movement (src/ants_sim/movement_system.cpp).
//
// Every expected time, step count and position below comes from an independent reference model of the
// disassembled Ants.exe locomotion (animation stepper FUN_0102b997, SetAction FUN_0101ad02, WalkStep
// FUN_0101b8cb, StopAt FUN_01021664) fed with the ants.chd Table-4 frames; see
// docs/GAME_REVERSE_ENGINEERING.md, "Movement ground truth". Times are milliseconds after the path
// manager delivered the path; the ant starts idle, facing south, at tile (5, 5) (pixel (176, 176)).
// Coordinates are TileCoord{x = column, y = row}.

#include "ants_sim/sim_engine.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_assets/lvl_parser.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using namespace ants::sim;
namespace mv = ants::sim::movement;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

#define TEST_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [MOVEMENT GOLDEN SUITE] " << name << "\n" \
              << "=======================================================\n"

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(66) << name << " ... " << std::flush;
    int prev_fails = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev_fails) {
        std::cout << "PASS\n";
    }
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );

#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

template <typename T>
long long printable(const T& v) {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<long long>(static_cast<std::underlying_type_t<T>>(v));
    } else {
        return static_cast<long long>(v);
    }
}

#define ASSERT_EQ(a, b) \
    do { \
        ++g_assert_count; \
        const auto lhs_ = (a); \
        const auto rhs_ = (b); \
        if (!(lhs_ == rhs_)) { \
            std::cout << "FAILED!\n    Assertion failed: " #a " == " #b " (" << printable(lhs_) << " vs " \
                      << printable(rhs_) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

namespace {

using TerrainFn = std::function<uint8_t(int32_t x, int32_t y)>;

uint8_t all_grass(int32_t, int32_t) { return mv::kTerrainGrass; }

// What the locomotion trace says about one ant's walk, relative to the path delivery.
struct WalkSummary {
    bool delivered{false};
    bool failed{false};
    uint32_t t_delivered{0};
    int32_t first{-1};      // ms from delivery to the first pixel move
    int32_t arrival{-1};    // ms from delivery to the last pixel move (the exact landing on the goal centre)
    int32_t steps{0};       // number of pixel moves
    int32_t fx{0};          // final pixel position
    int32_t fy{0};
};

WalkSummary summarize(const SimulationEngine& sim, uint32_t ant_id) {
    WalkSummary s;
    int32_t lx = 0;
    int32_t ly = 0;
    for (const auto& ev : sim.locomotion_trace()) {
        if (ev.ant_id != ant_id) continue;
        if (ev.kind == LocoTraceEvent::Kind::PathFailed) {
            s.failed = true;
            continue;
        }
        if (ev.kind == LocoTraceEvent::Kind::PathDelivered) {
            if (!s.delivered) {
                s.delivered = true;
                s.t_delivered = ev.time_ms;
                lx = ev.px;
                ly = ev.py;
            }
            continue;
        }
        if (!s.delivered) continue;
        if (ev.px != lx || ev.py != ly) {
            ++s.steps;
            const int32_t rel = static_cast<int32_t>(ev.time_ms - s.t_delivered);
            if (s.first < 0) s.first = rel;
            s.arrival = rel;
            lx = ev.px;
            ly = ev.py;
        }
    }
    s.fx = lx;
    s.fy = ly;
    return s;
}

void paint_terrain(SimulationEngine& sim, const TerrainFn& terrain) {
    for (int32_t y = 0; y < static_cast<int32_t>(sim.grid().height()); ++y) {
        for (int32_t x = 0; x < static_cast<int32_t>(sim.grid().width()); ++x) {
            sim.grid_mut().set_terrain_class(x, y, terrain(x, y));
        }
    }
}

struct GoldenCase {
    const char* name;
    AntType type;
    TileCoord goal;
    TerrainFn terrain;
    bool carrying;
    int32_t first;
    int32_t arrival;
    int32_t steps;
    int32_t fx;
    int32_t fy;
};

// Runs one golden walk and returns the summary plus the ant's final state.
WalkSummary run_golden(const GoldenCase& gc, bool* ended_idle) {
    SimulationEngine sim;
    sim.init_test_world(20, 20, 7, 600000);
    paint_terrain(sim, gc.terrain);
    const uint32_t id = sim.spawn_unit(0, gc.type, TileCoord{5, 5});
    AntUnit& a = sim.get_unit(id);
    a.facing = Direction::South;
    if (gc.carrying) a.pick_up_food(1, 25);
    sim.set_locomotion_trace_enabled(true);
    sim.issue_move_order(id, gc.goal);
    for (int t = 0; t < 200; ++t) sim.tick();
    if (ended_idle) {
        *ended_idle = a.waypoints.empty() && a.loco_action == AntUnit::kActionIdle && !a.pause_active &&
                      a.pixel_x == gc.goal.x * 32 + 16 && a.pixel_y == gc.goal.y * 32 + 16 &&
                      a.orig_order == AntUnit::kOrderNone;
    }
    return summarize(sim, id);
}

} // namespace

// ============================================================================
// Suite 1: Golden locomotion timings
// ============================================================================
static void run_suite_1_golden() {
    TEST_SUITE("Suite 1: Golden Locomotion Timings (reference model of Ants.exe)");

    const TerrainFn sand = [](int32_t, int32_t) -> uint8_t { return mv::kTerrainSand; };
    const TerrainFn dirt = [](int32_t, int32_t) -> uint8_t { return mv::kTerrainDirt; };
    const TerrainFn mud = [](int32_t, int32_t) -> uint8_t { return mv::kTerrainMud; };
    const TerrainFn water = [](int32_t, int32_t) -> uint8_t { return mv::kTerrainWater; };
    const TerrainFn mud_from_col7 = [](int32_t x, int32_t) -> uint8_t {
        return x >= 7 ? mv::kTerrainMud : mv::kTerrainGrass;
    };
    const TerrainFn water_from_col6 = [](int32_t x, int32_t) -> uint8_t {
        return x >= 6 ? mv::kTerrainWater : mv::kTerrainGrass;
    };
    const TerrainFn grass_from_col6 = [](int32_t x, int32_t) -> uint8_t {
        return x >= 6 ? mv::kTerrainGrass : mv::kTerrainWater;
    };

    const std::vector<GoldenCase> cases = {
        {"1.1 Worker, grass, 1 tile E", AntType::Worker, {6, 5}, all_grass, false, 250, 600, 8, 208, 176},
        {"1.2 Worker, grass, 1 tile W", AntType::Worker, {4, 5}, all_grass, false, 250, 600, 8, 144, 176},
        {"1.3 Worker, grass, 3 tiles E", AntType::Worker, {8, 5}, all_grass, false, 250, 1400, 24, 272, 176},
        {"1.4 Worker, grass, 1 tile SE", AntType::Worker, {6, 6}, all_grass, false, 250, 700, 10, 208, 208},
        {"1.5 Worker, grass, 3 tiles SE", AntType::Worker, {8, 8}, all_grass, false, 250, 1700, 30, 272, 272},
        {"1.6 Worker, grass, 1 tile NW (mirrored animation)", AntType::Worker, {4, 4}, all_grass, false, 250, 700, 10, 144, 144},
        {"1.7 Worker, sand, 3 tiles E", AntType::Worker, {8, 5}, sand, false, 230, 1150, 24, 272, 176},
        {"1.8 Worker, dirt, 3 tiles E", AntType::Worker, {8, 5}, dirt, false, 270, 1650, 24, 272, 176},
        {"1.9 Worker, mud, 1 tile E", AntType::Worker, {6, 5}, mud, false, 270, 1110, 15, 208, 176},
        {"1.10 Worker, mud, 1 tile SE", AntType::Worker, {6, 6}, mud, false, 270, 1410, 20, 208, 208},
        {"1.11 Worker carrying food, grass, 3 tiles E", AntType::Worker, {8, 5}, all_grass, true, 250, 1400, 24, 272, 176},
        {"1.12 Thief, grass, 3 tiles E (same pace as a worker)", AntType::Thief, {8, 5}, all_grass, false, 200, 1350, 24, 272, 176},
        {"1.13 Bomber, grass, 3 tiles E", AntType::Bomber, {8, 5}, all_grass, false, 200, 1350, 24, 272, 176},
        {"1.14 Fire ant, grass, 3 tiles E", AntType::Fire, {8, 5}, all_grass, false, 200, 1350, 24, 272, 176},
        {"1.15 Combat ant, grass, 3 tiles E", AntType::Combat, {8, 5}, all_grass, false, 225, 1375, 24, 272, 176},
        {"1.16 Swimmer, grass, 3 tiles E", AntType::Swimmer, {8, 5}, all_grass, false, 250, 1400, 24, 272, 176},
        {"1.17 Worker, E then SE (turn at a tile centre)", AntType::Worker, {7, 6}, all_grass, false, 250, 1150, 18, 240, 208},
        {"1.18 Worker, grass then mud from column 7, 3 tiles E", AntType::Worker, {8, 5}, mud_from_col7, false, 250, 2180, 34, 272, 176},
        {"1.19 Swimmer, water, 1 tile E", AntType::Swimmer, {6, 5}, water, false, 120, 480, 10, 208, 176},
        {"1.20 Swimmer, water, 1 tile SE", AntType::Swimmer, {6, 6}, water, false, 120, 680, 15, 208, 208},
        {"1.21 Swimmer, grass to water (dive), 2 tiles E", AntType::Swimmer, {7, 5}, water_from_col6, false, 510, 1690, 14, 240, 176},
        {"1.22 Swimmer, water to grass (climb), 2 tiles E", AntType::Swimmer, {7, 5}, grass_from_col6, false, 160, 970, 14, 240, 176},
    };

    for (const auto& gc : cases) {
        TEST_CASE(gc.name) {
            bool ended_idle = false;
            const WalkSummary s = run_golden(gc, &ended_idle);
            ASSERT_TRUE(s.delivered);
            ASSERT_EQ(s.first, gc.first);
            ASSERT_EQ(s.arrival, gc.arrival);
            ASSERT_EQ(s.steps, gc.steps);
            ASSERT_EQ(s.fx, gc.fx);
            ASSERT_EQ(s.fy, gc.fy);
            ASSERT_TRUE(ended_idle);
        } TEST_END();
    }
}

// ============================================================================
// Suite 2: Orders, path latency and the re-order snap
// ============================================================================
static void run_suite_2_orders() {
    TEST_SUITE("Suite 2: Orders, Path Latency and Re-Order Snap");

    TEST_CASE("2.1 Path arrives at the first PATHMGR run; walking starts 250 ms later") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        sim.get_unit(id).facing = Direction::South;
        sim.set_locomotion_trace_enabled(true);
        sim.issue_move_order(id, TileCoord{8, 5});
        // The order itself only snaps and idles the ant and queues a request (no path yet).
        ASSERT_TRUE(sim.get_unit(id).waypoints.empty());
        ASSERT_TRUE(sim.has_pending_path(id));
        ASSERT_EQ(sim.get_unit(id).state, UnitState::Walking);
        sim.tick();
        ASSERT_FALSE(sim.has_pending_path(id));
        ASSERT_EQ(sim.get_unit(id).waypoints.size(), static_cast<size_t>(4));   // start ... goal
        for (int t = 0; t < 60; ++t) sim.tick();
        const WalkSummary s = summarize(sim, id);
        ASSERT_EQ(s.t_delivered, 50u);                    // end of the first 50 ms tick
        ASSERT_EQ(s.first, 250);
        ASSERT_EQ(s.arrival, 1400);
        ASSERT_EQ(sim.get_unit(id).state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("2.2 One path per 50 ms per team: the second ant's path comes one run later") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        const uint32_t b = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 8});
        const uint32_t c = sim.spawn_unit(1, AntType::Worker, TileCoord{5, 11});
        sim.set_locomotion_trace_enabled(true);
        sim.issue_move_order(a, TileCoord{9, 5});
        sim.issue_move_order(b, TileCoord{9, 8});
        sim.issue_move_order(c, TileCoord{9, 11});
        for (int t = 0; t < 5; ++t) sim.tick();
        ASSERT_EQ(summarize(sim, a).t_delivered, 50u);
        ASSERT_EQ(summarize(sim, b).t_delivered, 100u);
        ASSERT_EQ(summarize(sim, c).t_delivered, 50u);   // another team has its own path manager
    } TEST_END();

    TEST_CASE("2.3 Re-order in the second half of a stride snaps FORWARD to the tile centre") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        AntUnit& a = sim.get_unit(id);
        sim.issue_move_order(id, TileCoord{9, 5});
        int guard = 0;
        while (!(a.pixel_x > 192 && a.pixel_x < 208) && guard++ < 100) sim.tick();
        ASSERT_TRUE(a.pixel_x > 192 && a.pixel_x < 208);   // already inside tile 6, short of its centre
        sim.issue_move_order(id, TileCoord{9, 5});
        ASSERT_EQ(a.pixel_x, 208);                           // FUN_0101fc50 snaps to the pixel tile's centre
        ASSERT_EQ(a.pixel_y, 176);
        ASSERT_TRUE(a.waypoints.empty());
        ASSERT_EQ(a.loco_action, AntUnit::kActionIdle);
        for (int t = 0; t < 60; ++t) sim.tick();
        ASSERT_EQ(a.pixel_x, 9 * 32 + 16);
    } TEST_END();

    TEST_CASE("2.4 Re-order in the first half of a stride snaps BACK to the tile centre") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        AntUnit& a = sim.get_unit(id);
        sim.issue_move_order(id, TileCoord{9, 5});
        int guard = 0;
        while (!(a.pixel_x > 176 && a.pixel_x < 192) && guard++ < 100) sim.tick();
        ASSERT_TRUE(a.pixel_x > 176 && a.pixel_x < 192);   // still inside tile 5, past its centre
        sim.issue_move_order(id, TileCoord{9, 5});
        ASSERT_EQ(a.pixel_x, 176);
        ASSERT_TRUE(a.waypoints.empty());
    } TEST_END();

    TEST_CASE("2.5 Ordering an ant to its own tile: 1-tile path, idle at the centre") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        AntUnit& a = sim.get_unit(id);
        sim.set_locomotion_trace_enabled(true);
        sim.issue_move_order(id, TileCoord{5, 5});
        sim.tick();
        ASSERT_EQ(a.waypoints.size(), static_cast<size_t>(1));
        for (int t = 0; t < 10; ++t) sim.tick();
        ASSERT_TRUE(a.waypoints.empty());
        ASSERT_EQ(a.state, UnitState::Idle);
        ASSERT_EQ(a.pixel_x, 176);
        ASSERT_EQ(summarize(sim, id).steps, 0);
    } TEST_END();

    TEST_CASE("2.6 A group ordered to one tile spreads over free tiles (team-mate claims, ring scan)") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{3, 5});
        const uint32_t b = sim.spawn_unit(0, AntType::Worker, TileCoord{3, 6});
        const uint32_t c = sim.spawn_unit(0, AntType::Worker, TileCoord{3, 7});
        sim.issue_move_order(a, TileCoord{10, 6});
        sim.issue_move_order(b, TileCoord{10, 6});
        sim.issue_move_order(c, TileCoord{10, 6});
        // The first order claims the clicked tile; later ones find it claimed (FUN_0101f780 flag 2) and take
        // the first free tile of the FUN_010202e7 ring scan: west column first, top to bottom.
        ASSERT_TRUE(sim.get_unit(a).orig_order_tile == (TileCoord{10, 6}));
        ASSERT_TRUE(sim.get_unit(b).orig_order_tile == (TileCoord{9, 5}));
        ASSERT_TRUE(sim.get_unit(c).orig_order_tile == (TileCoord{9, 6}));
        for (int t = 0; t < 120; ++t) sim.tick();
        const TileCoord pa = sim.get_unit(a).pos;
        const TileCoord pb = sim.get_unit(b).pos;
        const TileCoord pc = sim.get_unit(c).pos;
        ASSERT_TRUE(pa == (TileCoord{10, 6}));
        ASSERT_TRUE(pb != pa && pc != pa && pb != pc);
        ASSERT_TRUE(pc == (TileCoord{9, 6}));
        // B arrives at (9, 5) while A is walking across it: a taken final tile stops the ant where it is
        // (TryEnterTile -> StopSync, 0x101caf4), one tile short.
        ASSERT_TRUE(pb == (TileCoord{8, 6}));
        for (uint32_t id : {a, b, c}) {
            ASSERT_TRUE(sim.get_unit(id).waypoints.empty());
            ASSERT_EQ(sim.get_unit(id).state, UnitState::Idle);
        }
    } TEST_END();
}

// ============================================================================
// Suite 3: Tile blocking (occupancy grid, TryEnterTile, ANTPAUSE)
// ============================================================================
static void run_suite_3_blocking() {
    TEST_SUITE("Suite 3: Tile Blocking, ANTPAUSE Waits and Re-Planning");

    TEST_CASE("3.1 Stationary team-mate on the path: re-plan with a bump (anim 0xDC, sound 47)") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        AntUnit& a = sim.get_unit(id);
        sim.issue_move_order(id, TileCoord{9, 5});
        sim.tick();                                            // path delivered: straight east
        ASSERT_EQ(a.waypoints.size(), static_cast<size_t>(5));
        const uint32_t blocker = sim.spawn_unit(0, AntType::Worker, TileCoord{7, 5});
        sim.clear_audio_events();
        bool bumped = false;
        bool entered_blocked_tile = false;
        for (int t = 0; t < 120; ++t) {
            sim.tick();
            for (const auto& ev : sim.poll_audio_events()) {
                if (ev.sound_id == SoundID::Bump && ev.world_x == 7 * 32 + 16 && ev.world_y == 5 * 32 + 16) bumped = true;
            }
            if (a.occ_tile == (TileCoord{7, 5})) entered_blocked_tile = true;
        }
        ASSERT_TRUE(bumped);
        ASSERT_FALSE(entered_blocked_tile);
        ASSERT_TRUE(a.pos == (TileCoord{9, 5}));
        ASSERT_EQ(a.pixel_x, 9 * 32 + 16);
        ASSERT_TRUE(sim.get_unit(blocker).pos == (TileCoord{7, 5}));   // the idle ant is never pushed
    } TEST_END();

    TEST_CASE("3.2 Destination taken while walking: stop at the tile before it, no bump") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        AntUnit& a = sim.get_unit(id);
        sim.issue_move_order(id, TileCoord{7, 5});
        sim.tick();
        sim.spawn_unit(0, AntType::Worker, TileCoord{7, 5});
        sim.clear_audio_events();
        bool bumped = false;
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            for (const auto& ev : sim.poll_audio_events()) {
                if (ev.sound_id == SoundID::Bump) bumped = true;
            }
        }
        ASSERT_FALSE(bumped);
        ASSERT_TRUE(a.pos == (TileCoord{6, 5}));
        ASSERT_EQ(a.pixel_x, 6 * 32 + 16);
        ASSERT_TRUE(a.waypoints.empty());
        ASSERT_EQ(a.state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("3.3 Walking ant in the way: wait exactly 300 ms, then walk on (no bump)") {
        // Walls force both ants through tile (7, 5): A walks east along row 5, B south along column 7.
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        for (int32_t y = 1; y <= 10; ++y) {
            if (y == 5) continue;
            sim.grid_mut().set_terrain(6, y, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(8, y, TERRAIN_OBSTACLE);
        }
        const uint32_t b = sim.spawn_unit(0, AntType::Worker, TileCoord{7, 2});
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{4, 5});
        sim.get_unit(a).facing = Direction::South;
        sim.get_unit(b).facing = Direction::South;
        sim.set_locomotion_trace_enabled(true);
        sim.issue_move_order(b, TileCoord{7, 9});
        for (int t = 0; t < 4; ++t) sim.tick();
        sim.issue_move_order(a, TileCoord{12, 5});
        sim.clear_audio_events();
        bool bumped = false;
        bool shared_tile = false;
        for (int t = 0; t < 160; ++t) {
            sim.tick();
            for (const auto& ev : sim.poll_audio_events()) {
                if (ev.sound_id == SoundID::Bump) bumped = true;
            }
            if (sim.get_unit(a).occ_tile == sim.get_unit(b).occ_tile) shared_tile = true;
        }
        ASSERT_FALSE(bumped);
        ASSERT_FALSE(shared_tile);
        ASSERT_TRUE(sim.get_unit(a).pos == (TileCoord{12, 5}));
        ASSERT_TRUE(sim.get_unit(b).pos == (TileCoord{7, 9}));
        // B's path came at 50 ms and A's at 250 ms. A reaches the boundary of (7, 5) at 1450 ms while B is on
        // it (B is there from 1250 to 1650 ms): A goes idle and waits; ANTPAUSE fires 300 ms later and
        // restarts the walk animation.
        uint32_t pause_at = 0;
        uint32_t resume_at = 0;
        uint8_t last_action = 0xFF;
        for (const auto& ev : sim.locomotion_trace()) {
            if (ev.ant_id != a || ev.kind != LocoTraceEvent::Kind::Step) continue;
            if (last_action == AntUnit::kActionWalk && ev.action == AntUnit::kActionIdle && ev.time_ms < 3000 && pause_at == 0) {
                pause_at = ev.time_ms;
            } else if (pause_at != 0 && resume_at == 0 && ev.action == AntUnit::kActionWalk) {
                resume_at = ev.time_ms;
            }
            last_action = ev.action;
        }
        ASSERT_EQ(pause_at, 1450u);
        ASSERT_EQ(resume_at, 1750u);
    } TEST_END();

    TEST_CASE("3.4 The occupancy tile switches exactly at the 32-pixel boundary") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        AntUnit& a = sim.get_unit(id);
        sim.issue_move_order(id, TileCoord{9, 5});
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            ASSERT_TRUE(a.occ_tile == (TileCoord{a.pixel_x / 32, a.pixel_y / 32}));
        }
        ASSERT_TRUE(a.occ_tile == (TileCoord{9, 5}));
    } TEST_END();

    TEST_CASE("3.5 Enemy standing on the path: re-plan around it") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        AntUnit& a = sim.get_unit(id);
        sim.issue_move_order(id, TileCoord{9, 5});
        sim.tick();
        sim.spawn_unit(1, AntType::Worker, TileCoord{7, 5});
        bool entered = false;
        for (int t = 0; t < 120; ++t) {
            sim.tick();
            if (a.occ_tile == (TileCoord{7, 5})) entered = true;
        }
        ASSERT_FALSE(entered);
        ASSERT_TRUE(a.pos == (TileCoord{9, 5}));
    } TEST_END();
}

// ============================================================================
// Suite 4: "Can't go there."
// ============================================================================
static void run_suite_4_cant_go() {
    TEST_SUITE("Suite 4: Can't Go There");

    TEST_CASE("4.1 Unreachable tile: message 0x3A, can't-go animation (360 ms, sound 63), then idle") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        for (int32_t y = 13; y <= 17; ++y) {
            for (int32_t x = 13; x <= 17; ++x) {
                if (x == 13 || x == 17 || y == 13 || y == 17) sim.grid_mut().set_terrain(x, y, TERRAIN_WATER);
            }
        }
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        AntUnit& a = sim.get_unit(id);
        sim.set_locomotion_trace_enabled(true);
        sim.clear_audio_events();
        sim.clear_news_events();
        sim.issue_move_order(id, TileCoord{15, 15});
        sim.tick();
        ASSERT_EQ(a.state, UnitState::CantGo);
        ASSERT_EQ(a.loco_action, AntUnit::kActionCantGo);
        ASSERT_TRUE(sim.has_news_event(0, 0x3A));
        ASSERT_TRUE(sim.has_audio_event(SoundID::CantGo));
        uint32_t t_fail = 0;
        uint32_t t_idle = 0;
        for (int t = 0; t < 20; ++t) sim.tick();
        for (const auto& ev : sim.locomotion_trace()) {
            if (ev.ant_id != id) continue;
            if (ev.kind == LocoTraceEvent::Kind::PathFailed) t_fail = ev.time_ms;
            if (t_fail != 0 && t_idle == 0 && ev.kind == LocoTraceEvent::Kind::Step &&
                ev.action == AntUnit::kActionIdle && ev.time_ms > t_fail) {
                t_idle = ev.time_ms;
            }
        }
        ASSERT_EQ(t_fail, 50u);
        ASSERT_EQ(t_idle - t_fail, 360u);                 // agcg301: 6 frames of 60 ms
        ASSERT_EQ(a.state, UnitState::Idle);
        ASSERT_TRUE(a.pos == (TileCoord{5, 5}));
    } TEST_END();

    TEST_CASE("4.2 An ant playing its can't-go animation ignores player orders (FUN_0101ff5a)") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        for (int32_t x = 12; x <= 14; ++x) {
            for (int32_t y = 12; y <= 14; ++y) {
                if (x != 13 || y != 13) sim.grid_mut().set_terrain(x, y, TERRAIN_WATER);
            }
        }
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        AntUnit& a = sim.get_unit(id);
        sim.issue_move_order(id, TileCoord{13, 13});
        sim.tick();
        ASSERT_EQ(a.state, UnitState::CantGo);
        AntOrder order;
        order.ant_id = id;
        order.type = OrderType::Move;
        order.target_x = 8;
        order.target_y = 5;
        sim.issue_order(order);
        ASSERT_FALSE(sim.has_pending_path(id));
        ASSERT_EQ(a.state, UnitState::CantGo);
    } TEST_END();
}

// ============================================================================
// Suite 5: Terrain classes and solid bits
// ============================================================================
static std::string locate_maps_dir() {
    const std::vector<std::string> candidates = {
#ifdef ORIGINAL_ASSETS_DIR
        std::string(ORIGINAL_ASSETS_DIR) + "/Maps",
#endif
        "Original-Ants/Maps", "../Original-Ants/Maps", "../../Original-Ants/Maps", "../../../Original-Ants/Maps",
    };
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p + "/SMALL.LVL")) return p;
    }
    return "";
}

static void run_suite_5_terrain() {
    TEST_SUITE("Suite 5: Terrain Classes and Solid Bits");

    TEST_CASE("5.1 A bridge piece at any stage makes the tile mud (FUN_01008af7)") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        sim.grid_mut().set_terrain(10, 10, TERRAIN_WATER);
        ASSERT_EQ(sim.grid().terrain_class_at(TileCoord{10, 10}), mv::kTerrainWater);
        sim.grid_mut().advance_bridge(10, 10, 0);
        ASSERT_EQ(sim.grid().terrain_class_at(TileCoord{10, 10}), mv::kTerrainMud);
        for (int s = 0; s < 3; ++s) sim.grid_mut().advance_bridge(10, 10, 0);
        ASSERT_TRUE(sim.grid().get_cell(10, 10).has_completed_bridge());
        ASSERT_EQ(sim.grid().terrain_class_at(TileCoord{10, 10}), mv::kTerrainMud);
    } TEST_END();

    TEST_CASE("5.2 Workers cross a bridge with the mud walk (2 px per 60 ms)") {
        SimulationEngine sim;
        sim.init_test_world(20, 20, 7, 600000);
        for (int32_t y = 0; y < 20; ++y) sim.grid_mut().set_terrain(7, y, TERRAIN_WATER);
        for (int s = 0; s < 4; ++s) sim.grid_mut().advance_bridge(7, 5, 0);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        sim.set_locomotion_trace_enabled(true);
        sim.issue_move_order(id, TileCoord{9, 5});
        for (int t = 0; t < 120; ++t) sim.tick();
        ASSERT_TRUE(sim.get_unit(id).pos == (TileCoord{9, 5}));
        int bridge_steps = 0;
        for (const auto& ev : sim.locomotion_trace()) {
            if (ev.ant_id != id || ev.kind != LocoTraceEvent::Kind::Step) continue;
            if (ev.px / 32 == 7 && ev.dx != 0 && ev.px != 7 * 32 + 16) {
                ASSERT_TRUE(ev.dx == 2 || ev.dx == 3 || ev.dx == 4);   // mud frames (+ boundary nudge / snap)
                ++bridge_steps;
            }
        }
        ASSERT_TRUE(bridge_steps >= 14);
    } TEST_END();

    TEST_CASE("5.3 Shipped maps: every tile's terrain class equals the original tile-info table") {
        const std::string dir = locate_maps_dir();
        if (dir.empty()) {
            std::cout << "(maps not found, skipped) ";
            return;
        }
        for (const char* name : {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"}) {
            ants::assets::LevelData lvl;
            ASSERT_TRUE(lvl.load_from_file(dir + "/" + name));
            SimulationEngine sim;
            sim.init(lvl, 1);
            for (uint32_t y = 0; y < lvl.height; ++y) {
                for (uint32_t x = 0; x < lvl.width; ++x) {
                    const uint8_t expected = mv::terrain_class_of_cell(lvl.get_cell_layer1(x, y).tile_index,
                                                                       lvl.get_cell_layer2(x, y).tile_index);
                    ASSERT_EQ(sim.grid().terrain_class_at(TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)}), expected);
                }
            }
            // Solid bits: row 0 always; hill entrance and the tile above it never.
            for (uint32_t x = 0; x < lvl.width; ++x) {
                ASSERT_TRUE(sim.grid().is_solid_object(TileCoord{static_cast<int32_t>(x), 0}));
            }
            for (const auto& ah : sim.grid().anthills()) {
                const int32_t ex = static_cast<int32_t>(ah.x) + 1;
                const int32_t ey = static_cast<int32_t>(ah.y) + 1;
                ASSERT_FALSE(sim.grid().is_solid_object(TileCoord{ex, ey}));
                if (ey - 1 > 0) ASSERT_FALSE(sim.grid().is_solid_object(TileCoord{ex, ey - 1}));
            }
            for (const auto& wp : lvl.waypoints) {
                ASSERT_TRUE(sim.grid().is_solid_object(TileCoord{static_cast<int32_t>(wp.x), static_cast<int32_t>(wp.y)}));
            }
        }
    } TEST_END();
}

int main() {
    std::cout << "=======================================================\n"
              << " ANTS - ORIGINAL MOVEMENT GOLDEN TEST SUITE\n"
              << "=======================================================\n";

    run_suite_1_golden();
    run_suite_2_orders();
    run_suite_3_blocking();
    run_suite_4_cant_go();
    run_suite_5_terrain();

    std::cout << "\n=======================================================\n"
              << " MOVEMENT GOLDEN TEST SUMMARY\n"
              << "=======================================================\n"
              << "  Total Test Cases: " << g_test_count << "\n"
              << "  Total Assertions: " << g_assert_count << "\n"
              << "  Failed Tests:     " << g_test_failures << "\n"
              << "=======================================================\n";

    if (g_test_failures == 0) {
        std::cout << " >>> ALL MOVEMENT GOLDEN TESTS PASSED CLEANLY <<< \n\n";
        return 0;
    }
    std::cout << " >>> " << g_test_failures << " TEST(S) FAILED! <<< \n\n";
    return 1;
}
