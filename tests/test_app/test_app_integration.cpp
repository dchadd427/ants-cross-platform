#include <exception>
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/effect_specs.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_app/audio_mixer.hpp"
#include "ants_app/midi_player.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/application.hpp"
#include "ants_app/version.hpp"
#include "ants_test_paths.hpp"

using namespace ants::app;
using namespace ants::sim;
using namespace ants::assets;

// ============================================================================
// Test Harness Assertions & Runner
// ============================================================================

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

#define TEST_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [SUITE] " << name << "\n" \
              << "=======================================================\n"

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(60) << name << " ... " << std::flush;
    int prev_fails = g_test_failures;
    // an exception that leaves a test case is a failure of that case that names itself (it used to end the whole program: std::terminate, exit code 0xC0000409 on Windows)
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED!\n    Uncaught exception: " << e.what() << "\n";
        ++g_test_failures;
    } catch (...) {
        std::cout << "FAILED!\n    Uncaught exception of an unknown type\n";
        ++g_test_failures;
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
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_LE(a, b) ASSERT_TRUE((a) <= (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define ASSERT_GE(a, b) ASSERT_TRUE((a) >= (b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(std::abs((a) - (b)) <= (eps))

// Original 1998 movement (Ants.exe): an order only snaps and idles the ant and queues a path request; the
// path manager (PATHMGR) delivers one path per 50 ms tick per team, and walking starts after that.
// Ticks until none of the given ants has a queued request (at most max_ticks ticks).
static void tick_until_paths_delivered(SimulationEngine& sim, std::initializer_list<uint32_t> ids, int max_ticks = 20) {
    for (int t = 0; t < max_ticks; ++t) {
        bool pending = false;
        for (uint32_t id : ids) pending = pending || sim.has_pending_path(id);
        if (!pending) return;
        sim.tick();
    }
}

// Ticks until pred() holds (checked before every tick); returns whether it held within max_ticks ticks.
static bool tick_until(SimulationEngine& sim, const std::function<bool()>& pred, int max_ticks) {
    for (int t = 0; t < max_ticks; ++t) {
        if (pred()) return true;
        sim.tick();
    }
    return pred();
}

// Runs the simulation for `ms` milliseconds (50 ms ticks).
static void run_ms(SimulationEngine& sim, int ms) {
    for (int t = 0; t < ms / 50; ++t) sim.tick();
}

// The match ends at the first run of the CHECKGO task (every 200 ms) that finds the clock below 0 (Ants.exe 0x1024839), 0 - 200 ms
// after the clock shows 0:00. Ticks until the match is over (at most max_ticks); returns the ticks used, -1 when it never ended.
static int run_until_over(SimulationEngine& sim, int max_ticks = 12) {
    for (int i = 0; i < max_ticks; ++i) {
        if (sim.is_match_over()) return i;
        sim.tick();
    }
    return sim.is_match_over() ? max_ticks : -1;
}

// Ticks until pred() holds (checked before every tick); returns the elapsed milliseconds, -1 if it did not hold within max_ms.
static int wait_ms(SimulationEngine& sim, int max_ms, const std::function<bool()>& pred) {
    for (int t = 0; t <= max_ms / 50; ++t) {
        if (pred()) return t * 50;
        sim.tick();
    }
    return -1;
}

// The ant is playing the getpow clip (action 4): it has just taken a power-up.
static bool picking_up(const AntUnit& a) { return a.loco_action == AntUnit::kActionGetPow; }

// An ant that stands on a power-up takes it by a player order onto its own tile (a one-tile path that ends on the tile);
// the original has no idle pick-up. Returns once the getpow clip plays; run_ms(sim, 900) lets it finish.
static bool take_powerup_here(SimulationEngine& sim, uint32_t ant_id) {
    sim.issue_move_order(ant_id, sim.get_unit(ant_id).pos);
    return wait_ms(sim, 2000, [&]() { return picking_up(sim.get_unit(ant_id)); }) >= 0;
}

// A food object as a Block-2 entry of an LVL describes it (Ants.exe FUN_01008ca0): `units` units of `value` points each, and the
// stage list {threshold, tile} (the pile shows the last stage whose threshold is >= the units left; {0, 0x7FFE} = gone).
// The stage tiles come from the archive's food tiles (crackers 369..372 cover 2x2 cells around their anchor). Returns the index.
static int32_t place_pile(SimulationEngine& sim, int32_t col, int32_t row, uint16_t units, uint16_t value,
                          const std::vector<std::pair<uint16_t, uint16_t>>& stages) {
    FoodObject o;
    o.row = static_cast<uint16_t>(row);
    o.col = static_cast<uint16_t>(col);
    o.units = units;
    o.value = value;
    o.remaining = units;
    for (const auto& st : stages) {
        o.thresholds.push_back(st.first);
        o.stage_tiles.push_back(st.second);
    }
    return sim.grid_mut().add_food_object(std::move(o));
}

// An unreachable one-tile island in a ring of water: a player order to it makes the path manager answer "Can't go there."
static void make_water_island(SimulationEngine& sim, int32_t x, int32_t y) {
    for (int32_t iy = y - 1; iy <= y + 1; ++iy) {
        for (int32_t ix = x - 1; ix <= x + 1; ++ix) {
            if (ix != x || iy != y) sim.grid_mut().get_cell_mut(static_cast<uint32_t>(ix), static_cast<uint32_t>(iy)).terrain_type = TERRAIN_WATER;
        }
    }
}

// The ant crosses into the power-up's tile and gets the can't-go order while its walk has not landed yet: the pick-up is
// cancelled and the ant stands on the power-up (Ants.exe: any accepted order snaps the ant onto the centre of its tile).
static bool stand_on_powerup(SimulationEngine& sim, uint32_t ant_id, TileCoord hat, TileCoord island) {
    sim.issue_move_order(ant_id, hat);
    if (wait_ms(sim, 4000, [&]() { return sim.get_unit(ant_id).pos == hat; }) < 0) return false;
    sim.issue_move_order(ant_id, island);
    run_ms(sim, 3000);
    const AntUnit& u = sim.get_unit(ant_id);
    return u.pos == hat && u.loco_action == AntUnit::kActionIdle && sim.grid().has_powerup_at(hat);
}

// A name in the temporary folder that belongs to this run of the suite alone: two runs at the same time share the folder (the process id tells them apart)
static std::filesystem::path temp_path_of_this_run(const std::string& name) {
#if defined(_WIN32)
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(getpid());
#endif
    return std::filesystem::temp_directory_path() / (name + "_" + std::to_string(pid));
}

// TINY.LVL with its first start marker of the green team (tile 154) moved to row 21512, written to a temporary file (a byte edit of a shipped map, as the loader
// tests make their synthetic maps): the file loads, a match with the green team is not playable. Empty path when the shipped map cannot be read.
static std::filesystem::path write_marker_outside_grid_map() {
    std::ifstream in("Original-Ants/Maps/TINY.LVL", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 19000) return {};
    const size_t names = static_cast<size_t>(bytes[40] | (bytes[41] << 8)) + 1;
    const size_t dims = 42 + names * 11;
    const size_t cells = static_cast<size_t>(bytes[dims] | (bytes[dims + 1] << 8)) * static_cast<size_t>(bytes[dims + 4] | (bytes[dims + 5] << 8));
    const size_t records = dims + 8 + cells * 12 + 2;                     // block 1: a word count, then (tile, row, column) words
    const size_t count = static_cast<size_t>(bytes[records - 2] | (bytes[records - 1] << 8));
    size_t green = 0;
    for (size_t i = 0; i < count && green == 0; ++i) {
        const size_t at = records + i * 6;
        if ((bytes[at] | (bytes[at + 1] << 8)) == 154) green = at;
    }
    if (green == 0) return {};
    bytes[green + 2] = static_cast<uint8_t>(21512 & 0xFF);
    bytes[green + 3] = static_cast<uint8_t>(21512 >> 8);
    const std::filesystem::path path = temp_path_of_this_run("ants_unplayable_marker").replace_extension(".lvl");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good() ? path : std::filesystem::path{};
}

// Milliseconds from an ant's first path delivery to its last pixel move (the landing on the goal centre),
// read from the locomotion trace (SimulationEngine::set_locomotion_trace_enabled); -1 without a path.
static int32_t walk_duration_ms(const SimulationEngine& sim, uint32_t ant_id) {
    bool delivered = false;
    uint32_t t0 = 0;
    int32_t lx = 0;
    int32_t ly = 0;
    int32_t last = -1;
    for (const auto& ev : sim.locomotion_trace()) {
        if (ev.ant_id != ant_id) continue;
        if (ev.kind == LocoTraceEvent::Kind::PathDelivered) {
            if (!delivered) {
                delivered = true;
                t0 = ev.time_ms;
                lx = ev.px;
                ly = ev.py;
            }
            continue;
        }
        if (!delivered || ev.kind != LocoTraceEvent::Kind::Step) continue;
        if (ev.px != lx || ev.py != ly) {
            last = static_cast<int32_t>(ev.time_ms - t0);
            lx = ev.px;
            ly = ev.py;
        }
    }
    return delivered ? last : -1;
}

// ============================================================================
// SUITE 1: 640x480 Software Surface & Compositing
// ============================================================================
void run_suite_1_surface_compositing() {
    TEST_SUITE("Suite 1: 640x480 Software Surface & Pixel Compositing");

    TEST_CASE("1.1 Virtual 640x480 Framebuffer Allocation & Clearing") {
        constexpr uint32_t VIRTUAL_WIDTH = 640;
        constexpr uint32_t VIRTUAL_HEIGHT = 480;
        std::vector<uint32_t> framebuffer(VIRTUAL_WIDTH * VIRTUAL_HEIGHT, 0);

        ASSERT_EQ(framebuffer.size(), 640u * 480u);

        // Fill with magenta color key (0xFF00FF)
        uint32_t magenta_key = 0xFFFF00FF;
        std::fill(framebuffer.begin(), framebuffer.end(), magenta_key);
        ASSERT_EQ(framebuffer[0], magenta_key);
        ASSERT_EQ(framebuffer[640 * 480 - 1], magenta_key);
    } TEST_END();

    TEST_CASE("1.2 HUD Frame Border Rectangles & Clipping") {
        constexpr uint32_t VIRTUAL_WIDTH = 640;
        constexpr uint32_t VIRTUAL_HEIGHT = 480;
        std::vector<uint32_t> fb(VIRTUAL_WIDTH * VIRTUAL_HEIGHT, 0);

        // Helper to draw filled rect with boundary clipping
        auto draw_rect = [&](int rx, int ry, int rw, int rh, uint32_t color) {
            for (int y = ry; y < ry + rh; ++y) {
                if (y < 0 || y >= static_cast<int>(VIRTUAL_HEIGHT)) continue;
                for (int x = rx; x < rx + rw; ++x) {
                    if (x < 0 || x >= static_cast<int>(VIRTUAL_WIDTH)) continue;
                    fb[static_cast<size_t>(y) * VIRTUAL_WIDTH + static_cast<size_t>(x)] = color;
                }
            }
        };

        // Draw top bar: (0, 0, 640, 22)
        draw_rect(0, 0, 640, 22, 0xFF111111);
        // Draw left border: (0, 22, 17, 439)
        draw_rect(0, 22, 17, 439, 0xFF222222);
        // Draw playfield divider: (458, 22, 22, 439)
        draw_rect(458, 22, 22, 439, 0xFF333333);
        // Draw bottom bar: (17, 461, 623, 19)
        draw_rect(17, 461, 623, 19, 0xFF444444);

        ASSERT_EQ(fb[0], 0xFF111111);                      // Top bar origin
        ASSERT_EQ(fb[22 * 640 + 0], 0xFF222222);           // Left border
        ASSERT_EQ(fb[22 * 640 + 458], 0xFF333333);         // Divider
        ASSERT_EQ(fb[461 * 640 + 17], 0xFF444444);         // Bottom bar
        ASSERT_EQ(fb[100 * 640 + 100], 0u);                // Inside playfield (untouched)

        // Test clipping resilience: outside coordinates should not crash
        draw_rect(-50, -50, 100, 100, 0xFF999999);
        draw_rect(630, 470, 50, 50, 0xFF888888);
        ASSERT_EQ(fb[0], 0xFF999999);
        ASSERT_EQ(fb[479 * 640 + 639], 0xFF888888);
    } TEST_END();

    TEST_CASE("1.3 Authentic 4:3 Integer Scaling Matrix") {
        auto compute_integer_scale = [](int win_w, int win_h) -> int {
            int scale_x = win_w / 640;
            int scale_y = win_h / 480;
            return std::max(1, std::min(scale_x, scale_y));
        };

        ASSERT_EQ(compute_integer_scale(640, 480), 1);
        ASSERT_EQ(compute_integer_scale(1280, 960), 2);
        ASSERT_EQ(compute_integer_scale(1920, 1080), 2); // 1080p -> max 2x integer scale with pillarbox
        ASSERT_EQ(compute_integer_scale(1920, 1440), 3); // 3x scale
        ASSERT_EQ(compute_integer_scale(2560, 1440), 3); // 1440p -> max 3x integer scale
        ASSERT_EQ(compute_integer_scale(3840, 2160), 4); // 4K -> 4x scale
    } TEST_END();
}

// ============================================================================
// SUITE 2: Camera Viewport, Panning & Coordinate Transforms
// ============================================================================
void run_suite_2_camera_transforms() {
    TEST_SUITE("Suite 2: Camera Viewport, Panning & Coordinate Transforms");

    TEST_CASE("2.1 World-to-Screen and Screen-to-World Inversion Invariant") {
        ViewportCamera cam;
        cam.x = 200.0f;
        cam.y = 350.0f;

        int32_t test_wx = 350;
        int32_t test_wy = 500;

        int32_t sx = 0, sy = 0;
        cam.world_to_screen(test_wx, test_wy, sx, sy);
        ASSERT_EQ(sx, 16 + (350 - 200)); // 166: the view's origin is the screen pixel (16, 21) (Ants.exe 0x100a32b)
        ASSERT_EQ(sy, 21 + (500 - 350)); // 171

        int32_t back_wx = 0, back_wy = 0;
        bool in_bounds = cam.screen_to_world(sx, sy, back_wx, back_wy);
        ASSERT_TRUE(in_bounds);
        ASSERT_EQ(back_wx, test_wx);
        ASSERT_EQ(back_wy, test_wy);
    } TEST_END();

    TEST_CASE("2.2 Camera Boundary Clamping Across Small/Medium/Large Maps") {
        ViewportCamera cam;

        // 31x31 Map (TINY.LVL) -> 992 x 992 pixels
        cam.x = -100.0f;
        cam.y = -50.0f;
        cam.clamp_to_bounds(31, 31);
        ASSERT_EQ(static_cast<int32_t>(cam.x), 0);
        ASSERT_EQ(static_cast<int32_t>(cam.y), 0);

        cam.x = 2000.0f;
        cam.y = 2000.0f;
        cam.clamp_to_bounds(31, 31);
        ASSERT_EQ(static_cast<int32_t>(cam.x), 31 * 32 - 442); // 992 - 442 = 550: the view is 442 x 440
        ASSERT_EQ(static_cast<int32_t>(cam.y), 31 * 32 - 440); // 992 - 440 = 552

        // 60x60 Map (TREASURE.LVL) -> 1920 x 1920 pixels
        cam.x = 5000.0f;
        cam.y = 5000.0f;
        cam.clamp_to_bounds(60, 60);
        ASSERT_EQ(static_cast<int32_t>(cam.x), 60 * 32 - 442); // 1920 - 442 = 1478
        ASSERT_EQ(static_cast<int32_t>(cam.y), 60 * 32 - 440); // 1920 - 440 = 1480
    } TEST_END();

    TEST_CASE("2.4 The Audio Listener Is The Centre Of The 442 x 440 View (Ants.exe 0x1030249: origin + 221 / + 220)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        app.renderer().camera().world_x = 100;
        app.renderer().camera().world_y = 200;
        app.update_simulation(0.05f);
        ASSERT_EQ(app.audio_mixer().listener_x(), 100 + 221);
        ASSERT_EQ(app.audio_mixer().listener_y(), 200 + 220);
    } TEST_END();

    TEST_CASE("2.3 Centering Viewport on Simulation Entity") {
        ViewportCamera cam;

        // Entity at tile (30, 30) -> center (30*32+16, 30*32+16) = (976, 976)
        cam.center_on(976, 976, 60, 60);
        ASSERT_EQ(static_cast<int32_t>(cam.x), 976 - 442 / 2); // 976 - 221 = 755
        ASSERT_EQ(static_cast<int32_t>(cam.y), 976 - 440 / 2); // 976 - 220 = 756

        int32_t sx = 0, sy = 0;
        cam.world_to_screen(976, 976, sx, sy);
        // Entity should project directly to center of playfield
        ASSERT_EQ(sx, 16 + 442 / 2);
        ASSERT_EQ(sy, 21 + 440 / 2);
    } TEST_END();
}

// ============================================================================
// SUITE 3: HUD Element Layout, Radar Dots & Selection Card
// ============================================================================
void run_suite_3_hud_and_radar() {
    TEST_SUITE("Suite 3: HUD Element Layout, Radar Dots & Selection Card");

    TEST_CASE("3.1 Authentic Radar Coordinate Projection & Dot Placement") {
        constexpr int32_t RADAR_X = 480;
        constexpr int32_t RADAR_Y = 22;
        constexpr int32_t RADAR_W = 160;
        constexpr int32_t RADAR_H = 104;

        constexpr int32_t MAP_TILES = 60;

        auto tile_to_radar = [&](int32_t tx, int32_t ty, int32_t& rx, int32_t& ry) {
            rx = RADAR_X + (tx * RADAR_W) / MAP_TILES;
            ry = RADAR_Y + (ty * RADAR_H) / MAP_TILES;
        };

        int32_t rx = 0, ry = 0;
        tile_to_radar(0, 0, rx, ry);
        ASSERT_EQ(rx, 480);
        ASSERT_EQ(ry, 22);

        tile_to_radar(30, 30, rx, ry);
        ASSERT_EQ(rx, 480 + 80); // 560
        ASSERT_EQ(ry, 22 + 52);  // 74

        tile_to_radar(59, 59, rx, ry);
        ASSERT_GE(rx, 480);
        ASSERT_LT(rx, 480 + 160);
        ASSERT_GE(ry, 22);
        ASSERT_LT(ry, 22 + 104);
    } TEST_END();

    TEST_CASE("3.2 Radar Team Color Fidelity") {
        // Authentic team palette definitions:
        // Team 0 (Green): RGB(83, 147, 43)
        // Team 1 (Red):   RGB(251, 51, 91)
        // Team 2 (Blue):  RGB(119, 175, 239)
        // Team 3 (Black): RGB(79, 87, 111)
        uint32_t team_colors[4] = {
            0xFF2B9353, // Team 0 (Green)
            0xFF5B33FB, // Team 1 (Red)
            0xFFEFAF77, // Team 2 (Blue)
            0xFF6F574F  // Team 3 (Black)
        };

        ASSERT_NE(team_colors[0], team_colors[1]);
        ASSERT_NE(team_colors[1], team_colors[2]);
        ASSERT_NE(team_colors[2], team_colors[3]);
    } TEST_END();

    TEST_CASE("3.3 Selection Card Health Bar HP Segments") {
        auto compute_hp_color = [](int hp) -> uint32_t {
            if (hp >= 8) return 0xFF00FF00;      // Green (8-10 HP)
            if (hp >= 4) return 0xFF00FFFF;      // Yellow (4-7 HP)
            return 0xFF0000FF;                  // Red (1-3 HP)
        };

        ASSERT_EQ(compute_hp_color(10), 0xFF00FF00); // Full HP -> Green
        ASSERT_EQ(compute_hp_color(8), 0xFF00FF00);
        ASSERT_EQ(compute_hp_color(7), 0xFF00FFFF);  // Damaged -> Yellow
        ASSERT_EQ(compute_hp_color(4), 0xFF00FFFF);
        ASSERT_EQ(compute_hp_color(3), 0xFF0000FF);  // Critical -> Red
        ASSERT_EQ(compute_hp_color(1), 0xFF0000FF);
    } TEST_END();

    TEST_CASE("3.4 Hatch Button Cost Deduction & Egg Stock Logic") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 5);

        // Attempt hatch with sufficient score
        bool success = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(success);
        ASSERT_EQ(sim.get_player_score(0), 300); // 500 - 200 = 300
        ASSERT_EQ(sim.get_player_eggs(0), 4u);

        // Deduct remaining score so score < 200
        sim.set_player_score(0, 150);
        bool fail_no_funds = sim.hatch_ant(0, AntType::Worker);
        ASSERT_FALSE(fail_no_funds);
        ASSERT_EQ(sim.get_player_score(0), 150);
        ASSERT_EQ(sim.get_player_eggs(0), 4u);
    } TEST_END();

    TEST_CASE("3.5 Authentic Team HUD Palettes & Score Background Colors") {
        // Ground-truth COLORREF values from Ants.exe VA 0x100DA90..0x100DAD0
        // COLORREF 0x00bbggrr -> RGBA
        constexpr ColorRGBA SCORE_BG[4] = {
            {  7,  67,  47, 255}, // Team 0 (Green): COLORREF 0x002F4307
            {119,   0,   0, 255}, // Team 1 (Red):   COLORREF 0x00000077
            { 43,  39, 107, 255}, // Team 2 (Blue):  COLORREF 0x006B272B
            { 39,  39,  59, 255}  // Team 3 (Black): COLORREF 0x003B2727
        };

        ASSERT_EQ(SCORE_BG[0].r, 7);
        ASSERT_EQ(SCORE_BG[0].g, 67);
        ASSERT_EQ(SCORE_BG[0].b, 47);

        ASSERT_EQ(SCORE_BG[1].r, 119);
        ASSERT_EQ(SCORE_BG[1].g, 0);
        ASSERT_EQ(SCORE_BG[1].b, 0);

        ASSERT_EQ(SCORE_BG[2].r, 43);
        ASSERT_EQ(SCORE_BG[2].g, 39);
        ASSERT_EQ(SCORE_BG[2].b, 107);

        ASSERT_EQ(SCORE_BG[3].r, 39);
        ASSERT_EQ(SCORE_BG[3].g, 39);
        ASSERT_EQ(SCORE_BG[3].b, 59);

        // Sidebar backing fill colors (Authentic Index 10 from Ants.exe VA 0x100EA70 tables)
        constexpr ColorRGBA SIDEBAR_BG[4] = {
            { 43, 104,  95, 255}, // Team 0 (Green)
            {143,  35,  99, 255}, // Team 1 (Red)
            { 51,  87, 163, 255}, // Team 2 (Blue)
            { 87,  87,  91, 255}  // Team 3 (Black)
        };

        ASSERT_EQ(SIDEBAR_BG[0].r, 43);
        ASSERT_EQ(SIDEBAR_BG[0].g, 104);
        ASSERT_EQ(SIDEBAR_BG[0].b, 95);

        ASSERT_EQ(SIDEBAR_BG[1].r, 143);
        ASSERT_EQ(SIDEBAR_BG[1].g, 35);
        ASSERT_EQ(SIDEBAR_BG[1].b, 99);

        ASSERT_EQ(SIDEBAR_BG[2].r, 51);
        ASSERT_EQ(SIDEBAR_BG[2].g, 87);
        ASSERT_EQ(SIDEBAR_BG[2].b, 163);

        ASSERT_EQ(SIDEBAR_BG[3].r, 87);
        ASSERT_EQ(SIDEBAR_BG[3].g, 87);
        ASSERT_EQ(SIDEBAR_BG[3].b, 91);

        // Score slot rects from Ants.exe VA 0x10021B8..0x1002230
        struct SlotRect {
            int32_t label_left, label_right;
            int32_t box_left, box_right;
            int32_t top, bottom;
        };

        SlotRect top_slot = { 312, 399, 402, 455, 4, 17 };
        ASSERT_EQ(top_slot.box_right - top_slot.box_left + 1, 54);
        ASSERT_EQ(top_slot.bottom - top_slot.top + 1, 14);
        ASSERT_EQ(top_slot.box_left - top_slot.label_right, 3);

        SlotRect bot_slots[3] = {
            {   5, 101, 105, 158, 464, 477 },
            { 163, 251, 254, 307, 464, 477 },
            { 312, 399, 402, 455, 464, 477 }
        };

        for (int i = 0; i < 3; ++i) {
            ASSERT_EQ(bot_slots[i].box_right - bot_slots[i].box_left + 1, 54);
            ASSERT_EQ(bot_slots[i].bottom - bot_slots[i].top + 1, 14);
            ASSERT_GE(bot_slots[i].box_left, bot_slots[i].label_right);
        }
    } TEST_END();
}

// ============================================================================
// SUITE 4: 32-Channel Audio Mixer & Spatial Audio
// ============================================================================
void run_suite_4_audio_mixer() {
    TEST_SUITE("Suite 4: 32-Channel Audio Mixer & Spatial Audio");

    AssetArchive archive;
    std::string chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
    bool loaded = archive.load_chd(chd_path);
    if (!loaded) {
        std::cout << "  SKIPPED: ants.chd not found at " << chd_path << "\n";
        return;
    }

    TEST_CASE("4.1 32-Channel Saturation Allocation") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        ASSERT_EQ(mixer.active_channel_count(), 0u);

        // Allocate 32 sounds
        for (uint32_t i = 0; i < 32; ++i) {
            int ch = mixer.play_sfx(0, 1.0f, 10, false);
            ASSERT_GE(ch, 0);
        }
        ASSERT_EQ(mixer.active_channel_count(), 32u);
    } TEST_END();

    TEST_CASE("4.2 Priority Channel Preemption (Sound 58 Preemption)") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        // Fill all 32 channels with priority 10
        for (uint32_t i = 0; i < 32; ++i) {
            mixer.play_sfx(0, 1.0f, 10, false);
        }
        ASSERT_EQ(mixer.active_channel_count(), 32u);

        // Play lower priority sound (priority 5) -> should be dropped (-1)
        int dropped = mixer.play_sfx(4, 1.0f, 5, false);
        ASSERT_EQ(dropped, -1);
        ASSERT_EQ(mixer.active_channel_count(), 32u);

        // Play critical alarm siren Sound 58 (priority 200) -> must preempt a channel
        int preempted = mixer.play_sfx(58, 1.0f, 200, false);
        ASSERT_GE(preempted, 0);
        ASSERT_LT(preempted, 32);
        ASSERT_TRUE(mixer.is_channel_active(preempted));
    } TEST_END();

    TEST_CASE("4.3 The Sound Law Of The Original: Chebyshev Percent Over 2500 px, Pan On The Far Channel Only, DirectSound Hundredths Of A dB (0x102e8e4, 0x102d803)") {
        // the integer arithmetic of the binary
        ASSERT_EQ(AudioMixer::distance_percent(0, 0), 100);
        ASSERT_EQ(AudioMixer::distance_percent(50, 0), 98);
        ASSERT_EQ(AudioMixer::distance_percent(221, 0), 92);                       // trunc(22100 / 2500) = 8
        ASSERT_EQ(AudioMixer::distance_percent(300, 400), 84);                     // Chebyshev: the larger of the two distances
        ASSERT_EQ(AudioMixer::distance_percent(0, -1000), 60);
        ASSERT_EQ(AudioMixer::distance_percent(800, 0), 68);
        ASSERT_EQ(AudioMixer::distance_percent(2500, 2500), 0);
        ASSERT_EQ(AudioMixer::pan_centibels(0), 0);
        ASSERT_EQ(AudioMixer::pan_centibels(50), 50);                              // 25 per step of 25 px
        ASSERT_EQ(AudioMixer::pan_centibels(221), 200);
        ASSERT_EQ(AudioMixer::pan_centibels(-221), -200);
        ASSERT_EQ(AudioMixer::pan_centibels(2500), 2500);
        ASSERT_EQ(AudioMixer::attenuation_centibels(100, 100), 0);
        ASSERT_EQ(AudioMixer::attenuation_centibels(100, 92), -200);
        ASSERT_EQ(AudioMixer::attenuation_centibels(100, 0), -2500);
        ASSERT_EQ(AudioMixer::attenuation_centibels(50, 100), -1250);              // the option at 50: -12.5 dB for every sound, positional or not
        ASSERT_EQ(AudioMixer::attenuation_centibels(75, 100), -625);
        ASSERT_EQ(AudioMixer::attenuation_centibels(1, 100), -2475);
        ASSERT_EQ(AudioMixer::attenuation_centibels(0, 100), -10000);              // muted
        ASSERT_NEAR(AudioMixer::gain_from_centibels(-200), 0.7943f, 0.0005f);
        ASSERT_NEAR(AudioMixer::gain_from_centibels(-2500), 0.0562f, 0.0005f);
        ASSERT_EQ(AudioMixer::gain_from_centibels(-10000), 0.0f);

        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);
        mixer.set_listener_position(500, 500);
        float vl = 0.0f, vr = 0.0f;

        mixer.calculate_spatial_pan(500, 500, 1.0f, vl, vr);                       // centre: no attenuation, no pan
        ASSERT_NEAR(vl, 1.0f, 0.001f);
        ASSERT_NEAR(vr, 1.0f, 0.001f);
        mixer.calculate_spatial_pan(721, 500, 1.0f, vl, vr);                       // 221 px to the right: -2 dB for both channels, the LEFT channel another -2 dB
        ASSERT_NEAR(vr, 0.794f, 0.001f);
        ASSERT_NEAR(vl, 0.631f, 0.001f);
        mixer.calculate_spatial_pan(279, 500, 1.0f, vl, vr);                       // the mirror image: the RIGHT channel is the far one
        ASSERT_NEAR(vl, 0.794f, 0.001f);
        ASSERT_NEAR(vr, 0.631f, 0.001f);
        mixer.calculate_spatial_pan(900, 500, 1.0f, vl, vr);                       // 400 px: -4 dB, far channel -8 dB
        ASSERT_NEAR(vr, 0.631f, 0.001f);
        ASSERT_NEAR(vl, 0.398f, 0.001f);
        mixer.calculate_spatial_pan(500, 1500, 1.0f, vl, vr);                      // 1000 px straight below: -10 dB, no pan: nothing is culled inside the map
        ASSERT_NEAR(vl, 0.316f, 0.001f);
        ASSERT_NEAR(vr, 0.316f, 0.001f);
        mixer.calculate_spatial_pan(1300, 500, 1.0f, vl, vr);                      // 800 px: -8 dB (the old law was silent here)
        ASSERT_NEAR(vr, 0.398f, 0.001f);
        ASSERT_NEAR(vl, 0.158f, 0.001f);
        mixer.calculate_spatial_pan(2500 + 500, 500, 1.0f, vl, vr);                // the edge of the radius: -25 dB, far channel -50 dB
        ASSERT_NEAR(vr, 0.0562f, 0.0005f);
        ASSERT_NEAR(vl, 0.0032f, 0.0005f);
    } TEST_END();

    TEST_CASE("4.3b The Sound Volume Option Enters Every Sound And Follows The Playing Ones; Positional Sounds Follow The Moving View (FUN_0102f777)") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);
        mixer.set_listener_position(500, 500);
        ASSERT_EQ(mixer.sound_volume(), 100);

        const int cue = mixer.play_sfx(SoundID::AntStop, 1.0f, 10, false);         // a cue: plain volume, only the option applies
        const int hit = mixer.play_spatial(SoundID::BombDetonate, 721, 500, 10);   // 221 px to the right of the view centre
        ASSERT_GE(cue, 0);
        ASSERT_GE(hit, 0);
        float l = 0.0f, r = 0.0f;
        ASSERT_TRUE(mixer.channel_volumes(cue, l, r));
        ASSERT_NEAR(l, 1.0f, 0.001f);
        ASSERT_NEAR(r, 1.0f, 0.001f);
        ASSERT_TRUE(mixer.channel_volumes(hit, l, r));
        ASSERT_NEAR(l, 0.631f, 0.001f);
        ASSERT_NEAR(r, 0.794f, 0.001f);

        mixer.set_sfx_volume(0.5f);                                                // the slider at 50: -12.5 dB for everything that plays, now
        ASSERT_EQ(mixer.sound_volume(), 50);
        ASSERT_TRUE(mixer.channel_volumes(cue, l, r));
        ASSERT_NEAR(l, 0.237f, 0.001f);
        ASSERT_NEAR(r, 0.237f, 0.001f);
        ASSERT_TRUE(mixer.channel_volumes(hit, l, r));                             // 25 * (50 * 92 / 100 - 100) = -1350: 0.211; the far channel another -2 dB
        ASSERT_NEAR(r, 0.2113f, 0.001f);
        ASSERT_NEAR(l, 0.1679f, 0.001f);

        mixer.set_listener_position(721, 500);                                     // the view moves: the source is at its centre
        ASSERT_TRUE(mixer.channel_volumes(hit, l, r));
        ASSERT_NEAR(l, 0.237f, 0.001f);
        ASSERT_NEAR(r, 0.237f, 0.001f);

        mixer.set_sfx_volume(0.0f);                                                // 0: muted, and nothing new occupies a channel
        ASSERT_TRUE(mixer.channel_volumes(cue, l, r));
        ASSERT_EQ(l, 0.0f);
        ASSERT_EQ(mixer.play_spatial(SoundID::BombDetonate, 721, 500, 10), -1);
    } TEST_END();

    TEST_CASE("4.3c Tracked Sounds: A Stop Event Cuts The Sounds Of Its Owner And Only Those (FUN_0102bdab)") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);
        mixer.set_listener_position(400, 400);
        AudioEvent a{SoundID::MeleeAttack, 400, 400, 1, 255};
        a.owner = 7;
        AudioEvent a2{SoundID::FoodHarvest, 400, 410, 1, 255};                   // a second sound of the same sprite (the duplicates of a clip are all tracked)
        a2.owner = 7;
        AudioEvent b{SoundID::MeleeAttack, 420, 400, 1, 255};
        b.owner = 8;
        AudioEvent cue{SoundID::AntStop, 0, 0, 1, 255};                          // a cue: nobody owns it
        mixer.ingest_simulation_events({a, a2, b, cue}, 0);
        ASSERT_EQ(mixer.active_channel_count(), 4u);

        AudioEvent stop;
        stop.sound_id = 0xFFFFFFFFu;
        stop.owner = 7;
        stop.stop = true;
        mixer.ingest_simulation_events({stop}, 0);
        ASSERT_EQ(mixer.active_channel_count(), 2u);                             // the sprite 7's two sounds are cut, the sprite 8's and the cue play on
        mixer.ingest_simulation_events({stop}, 0);                               // nothing of it plays any more: nothing changes
        ASSERT_EQ(mixer.active_channel_count(), 2u);
        stop.owner = 0;
        mixer.ingest_simulation_events({stop}, 0);                               // owner 0 is nobody
        ASSERT_EQ(mixer.active_channel_count(), 2u);
        // events in order: a sound that is started and stopped within one tick does not play; a sound after a stop does
        AudioEvent again = a;
        stop.owner = 7;
        mixer.ingest_simulation_events({a, stop, again}, 0);
        ASSERT_EQ(mixer.active_channel_count(), 3u);
        mixer.stop_owner(8);
        ASSERT_EQ(mixer.active_channel_count(), 2u);
    } TEST_END();

    TEST_CASE("4.4 Ingestion of Simulation Audio Events & Targeted Filtering") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        std::vector<AudioEvent> events;
        // Broadcast sound: VictoryFanfare
        events.push_back(AudioEvent{SoundID::VictoryFanfare, 0, 0, 2, 255});
        // Targeted alarm: targeted to victim player 1
        events.push_back(AudioEvent{SoundID::BaseAlarmSiren, 100, 100, 2, 1});

        // Player 0 (not the victim) should only receive the broadcast sound
        mixer.ingest_simulation_events(events, 0);
        ASSERT_EQ(mixer.active_channel_count(), 1u);
        mixer.stop_all();

        // Player 1 (the victim) should receive both sounds
        mixer.ingest_simulation_events(events, 1);
        ASSERT_EQ(mixer.active_channel_count(), 2u);
    } TEST_END();

    TEST_CASE("4.5 Software PCM Mixing & Non-Zero Dynamic Waveform Output") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        // Play bombexp.wav (Sound 4, 22050 Hz) and scoreup.wav (Sound 87, 11025 Hz)
        mixer.play_sfx(4, 0.8f, 10, false);
        mixer.play_sfx(87, 0.8f, 10, false);

        // Render 1024 frames of 44.1kHz stereo
        auto pcm = mixer.render_frames(1024);
        ASSERT_EQ(pcm.size(), 1024u * 2u);

        int non_zero_count = 0;
        int16_t max_sample = 0;
        for (int16_t s : pcm) {
            if (s != 0) ++non_zero_count;
            if (std::abs(s) > max_sample) max_sample = static_cast<int16_t>(std::abs(s));
        }

        ASSERT_GT(non_zero_count, 1000);
        ASSERT_GT(max_sample, 5000);
    } TEST_END();

    TEST_CASE("4.6 Native In-Engine MP3 Background Music Playback & Streaming Mixing") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        ASSERT_FALSE(mixer.is_music_playing());

        std::string intro_path = std::string(ORIGINAL_ASSETS_DIR) + "/INTRO.mp3";
        bool music_loaded = mixer.play_music(intro_path, true);
        ASSERT_TRUE(music_loaded);
        ASSERT_TRUE(mixer.is_music_playing());

        // Volume control
        mixer.set_music_volume(0.75f);
        ASSERT_NEAR(mixer.get_music_volume(), 0.75f, 0.01f);

        // Render frames with both SFX and MP3 music mixed
        mixer.play_sfx(4, 0.5f, 10, false); // bomb explosion
        auto pcm = mixer.render_frames(1024);
        ASSERT_EQ(pcm.size(), 1024u * 2u);

        int non_zero_count = 0;
        for (int16_t s : pcm) {
            if (s != 0) ++non_zero_count;
        }
        ASSERT_GT(non_zero_count, 500);

        // Pause and resume
        mixer.pause_music();
        ASSERT_FALSE(mixer.is_music_playing());
        mixer.resume_music();
        ASSERT_TRUE(mixer.is_music_playing());

        // Smooth fade out
        mixer.fade_out_music(0.5f);
        mixer.update_music(0.25f);
        ASSERT_TRUE(mixer.is_music_playing());
        mixer.update_music(0.30f); // Completed fade out
        ASSERT_FALSE(mixer.is_music_playing());

        // Stop music
        mixer.stop_music();
        ASSERT_FALSE(mixer.is_music_playing());
    } TEST_END();
}

// ============================================================================
// SUITE 5: AudioToolbox MIDI Lifecycle & Playback
// ============================================================================
void run_suite_5_midi_player() {
    TEST_SUITE("Suite 5: AudioToolbox MIDI Lifecycle & Playback");

    std::string midi_path = std::string(ORIGINAL_ASSETS_DIR) + "/INTRO.MID";

    TEST_CASE("5.1 Loading INTRO.MID & Sequence Properties") {
        MidiPlayer player;
        player.set_headless_mode(false); // Test native AudioToolbox loading
        bool ok = player.load_file(midi_path);
        ASSERT_TRUE(ok);
        ASSERT_TRUE(player.is_loaded());
        ASSERT_EQ(player.get_track_count(), 38u);
        ASSERT_GT(player.get_duration(), 90.0); // ~96.01 beats
    } TEST_END();

    TEST_CASE("5.2 MIDI Playback Lifecycle & Volume Fading") {
        MidiPlayer player;
        player.load_file(midi_path);

        ASSERT_FALSE(player.is_playing());
        player.play(true);
        ASSERT_TRUE(player.is_playing());
        ASSERT_EQ(player.state(), MidiState::Playing);

        player.pause();
        ASSERT_FALSE(player.is_playing());
        ASSERT_EQ(player.state(), MidiState::Paused);

        player.resume();
        ASSERT_TRUE(player.is_playing());

        // Volume control
        player.set_volume(0.6f);
        ASSERT_NEAR(player.get_volume(), 0.6f, 0.01f);

        // Fade out over 0.5s
        player.fade_out(0.5f);
        player.update(0.25f);
        ASSERT_LT(player.get_volume(), 0.6f);

        player.update(0.30f); // Complete fade out
        ASSERT_EQ(player.get_volume(), 0.0f);
        ASSERT_FALSE(player.is_playing());

        player.stop();
        ASSERT_EQ(player.state(), MidiState::Stopped);
    } TEST_END();
}

// ============================================================================
// SUITE 6: Scorecard Modal & Win/Loss Audio Routing
// ============================================================================
void run_suite_6_scorecard_and_audio_routing() {
    TEST_SUITE("Suite 6: Scorecard Modal & Win/Loss Audio Routing");

    TEST_CASE("6.1 Match End After 0:00 Simulation Freeze & Split Audio Routing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 999, 100); // 100 ms match (2 ticks to 0:00)

        sim.set_player_score(0, 350); // Winner
        sim.set_player_score(1, 150); // Loser

        // Tick to expiration: CHECKGO ends the match at its first run with a clock below 0 (within 200 ms after 0:00)
        sim.tick();
        sim.tick();
        ASSERT_FALSE(sim.is_match_over());
        ASSERT_TRUE(run_until_over(sim, 8) >= 1);

        ASSERT_TRUE(sim.is_match_over());
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);

        // The simulation queues no sting (it used to, and the results screen played its own on top: the sting was heard twice). The original plays ONE
        // cue per machine when the results rows are built (0x1015a4a); the screen's rule picks it: winner when the local team is the top row
        auto audio_events = sim.poll_audio_events();
        for (const auto& ev : audio_events) {
            ASSERT_TRUE(ev.sound_id != SoundID::VictoryFanfare);
            ASSERT_TRUE(ev.sound_id != SoundID::PlayerDefeat);
        }
        ScorecardModal winner_screen;
        winner_screen.show(sim.get_world_state().match_result, 0);
        ASSERT_EQ(winner_screen.get_audio_to_play(), 0u);                          // the cue plays when the rows are built, 250 ms after the screen opens
        winner_screen.update(0.25f);
        ASSERT_EQ(winner_screen.get_audio_to_play(), SoundID::VictoryFanfare);     // Winner hears Sound 56
        ScorecardModal loser_screen;
        loser_screen.show(sim.get_world_state().match_result, 1);
        loser_screen.update(0.25f);
        ASSERT_EQ(loser_screen.get_audio_to_play(), SoundID::PlayerDefeat);        // Loser hears Sound 42
    } TEST_END();

    TEST_CASE("6.2 Scorecard Modal 4 Discrete Tracked Statistics") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        sim.set_player_score(0, 420);
        sim.record_player_stat(0, StatType::FriendlyLost, 5);
        sim.record_player_stat(0, StatType::EnemyKilled, 14);
        sim.record_player_stat(0, StatType::NewHatched, 22);

        auto stats = sim.get_player_stats(0);
        ASSERT_EQ(sim.get_player_score(0), 420);        // Stat 1: Score
        ASSERT_EQ(stats.friendly_lost, 5u);            // Stat 2: Friendly Lost
        ASSERT_EQ(stats.enemy_killed, 14u);            // Stat 3: Enemy Killed
        ASSERT_EQ(stats.new_hatched, 22u);             // Stat 4: New Hatched

        // Verify ScorecardModal state matches
        ScorecardModal modal;
        modal.show(sim.get_world_state().match_result, 0);
        ASSERT_TRUE(modal.is_open());
        modal.update(0.25f);
        ASSERT_FALSE(modal.is_waiting());
        ASSERT_EQ(modal.get_audio_to_play(), SoundID::VictoryFanfare);
        ASSERT_EQ(modal.rows().size(), 4u);                                         // every team of the match has a row
        ASSERT_EQ(modal.rows()[0].first, 0);                                        // team 0 scored 420
        ASSERT_EQ(modal.rows()[0].numbers[0], "420");
        ASSERT_EQ(modal.rows()[0].numbers[1], "5");
        ASSERT_EQ(modal.rows()[0].numbers[2], "14");
        ASSERT_EQ(modal.rows()[0].numbers[3], "22");
        modal.clear_audio_to_play();
        ASSERT_EQ(modal.get_audio_to_play(), 0u);
        modal.hide();
        ASSERT_FALSE(modal.is_open());
    } TEST_END();

    TEST_CASE("6.3 Scorecard Modal Leave Game Closes Application / Triggers Quit") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        ScorecardModal modal;
        modal.show(sim.get_world_state().match_result, 0);
        ASSERT_TRUE(modal.is_open());

        bool quit_called = false;
        bool replay_called = false;
        modal.set_on_quit([&]() { quit_called = true; });
        modal.set_on_replay([&]() { replay_called = true; });

        // The Leave button is created with the rows (FUN_010155ac): while the screen waits for the scores it is not there
        modal.handle_mouse_motion(550, 20);
        ASSERT_FALSE(modal.is_quit_hovered());
        modal.handle_mouse_down(550, 20);
        modal.handle_mouse_up(550, 20);
        ASSERT_FALSE(quit_called);
        modal.update(0.25f);

        // Hover over Leave Game button at (525, 12)
        modal.handle_mouse_motion(550, 20);
        ASSERT_TRUE(modal.is_quit_hovered());

        // Mouse down on Leave Game button
        modal.handle_mouse_down(550, 20);
        ASSERT_TRUE(modal.is_quit_pressed());

        // Mouse up on Leave Game button
        modal.handle_mouse_up(550, 20);
        ASSERT_FALSE(modal.is_quit_pressed());
        ASSERT_TRUE(quit_called);
        ASSERT_FALSE(replay_called); // Must NOT trigger replay/restart

        // Verify with Application instance: clicking Leave Game on end screen calls quit()
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.is_running());

        // Trigger scorecard to show
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.match_running());                                   // (the page's selector of the picture asks before it restarts a running match: ants_match_running)
        app.scorecard().show(app.sim().get_world_state().match_result, 0);
        ASSERT_TRUE(app.scorecard().is_open());
        ASSERT_FALSE(app.match_running());                                  // the results are not a running match
        app.scorecard().update(0.25f);                                      // the rows (and the Leave button) appear after 250 ms

        // Click Leave Game button via Application input handler
        SDL_MouseButtonEvent down_ev{};
        down_ev.type = SDL_MOUSEBUTTONDOWN;
        down_ev.button = SDL_BUTTON_LEFT;
        down_ev.x = 550;
        down_ev.y = 20;
        app.handle_mouse_button(down_ev);

        SDL_MouseButtonEvent up_ev{};
        up_ev.type = SDL_MOUSEBUTTONUP;
        up_ev.button = SDL_BUTTON_LEFT;
        up_ev.x = 550;
        up_ev.y = 20;
        app.handle_mouse_button(up_ev);

        // Application must have received quit() and stopped running
        ASSERT_FALSE(app.is_running());
    } TEST_END();

    TEST_CASE("6.3b Results Leave: The Left Button Only (FUN_01011206); A Pointer That Rests On It Hovers As Soon As The Rows Are There (The INPUT Task's Poll, FUN_0102653f)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        app.note_pointer(550, 20);                                                // the pointer rests where the Leave button will be
        app.scorecard().show(app.sim().get_world_state().match_result, 0);
        app.update_results(0.1f);                                                 // waiting for the scores: no button yet
        ASSERT_FALSE(app.scorecard().is_quit_hovered());
        app.update_results(0.2f);                                                 // the rows, and the Leave button under the resting pointer
        ASSERT_TRUE(app.scorecard().is_quit_hovered());
        for (uint8_t b : {static_cast<uint8_t>(SDL_BUTTON_RIGHT), static_cast<uint8_t>(SDL_BUTTON_MIDDLE)}) {
            SDL_MouseButtonEvent ev{};
            ev.type = SDL_MOUSEBUTTONDOWN;
            ev.button = b;
            ev.x = 550;
            ev.y = 20;
            app.handle_mouse_button(ev);
            ASSERT_FALSE(app.scorecard().is_quit_pressed());                      // another button does not press it ...
            ev.type = SDL_MOUSEBUTTONUP;
            app.handle_mouse_button(ev);
            ASSERT_TRUE(app.is_running());                                        // ... and does not leave
        }
        SDL_MouseButtonEvent left{};
        left.type = SDL_MOUSEBUTTONDOWN;
        left.button = SDL_BUTTON_LEFT;
        left.x = 550;
        left.y = 20;
        app.handle_mouse_button(left);
        ASSERT_TRUE(app.scorecard().is_quit_pressed());
        left.type = SDL_MOUSEBUTTONUP;
        app.handle_mouse_button(left);
        ASSERT_FALSE(app.is_running());
    } TEST_END();

    TEST_CASE("6.4 Quit Dialog Yes With Exactly One Other Side Left Ends The Match (FUN_0101453f): No Exit, The Results Open With The Quitter Last") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.local_player_id(), 0);
        app.sim().drop_player(2);
        app.sim().drop_player(3);
        ASSERT_FALSE(app.sim().is_match_over());                             // two enemies are left: team 0 and team 1
        ASSERT_EQ(app.sim().other_sides(0), 1u);
        app.sim().set_player_score(0, 900);                                  // the quitter's score does not help: its row is the last
        app.sim().set_player_score(1, 50);
        SDL_KeyboardEvent q_ev{};
        q_ev.type = SDL_KEYDOWN;
        q_ev.keysym.sym = SDLK_q;
        q_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(q_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        SDL_KeyboardEvent y_ev{};
        y_ev.type = SDL_KEYDOWN;
        y_ev.keysym.sym = SDLK_y;
        app.handle_key_down(y_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());
        ASSERT_TRUE(app.is_running());                                       // the quit is the end of the match: the results come first
        ASSERT_TRUE(app.sim().is_match_over());
        ASSERT_EQ(app.sim().quitter(), 0);
        ASSERT_TRUE(app.scorecard().is_open());
        const auto rows = app.sim().get_world_state().match_result.rows(0);
        ASSERT_EQ(rows.size(), 2u);
        ASSERT_EQ(rows[0].first, 1);
        ASSERT_EQ(rows[1].first, 0);
        ASSERT_EQ(app.scorecard().get_audio_to_play(), 0u);                  // nothing is pending: the cue plays once, 250 ms after the results opened
    } TEST_END();

    TEST_CASE("6.5 Results Screen Keys (FUN_01015b17): Enter, C, Q And X Leave (Either Case) At Any Time, Even While It Waits; Esc And Every Other Key Do Nothing") {
        const SDL_Keycode leaving[] = {SDLK_RETURN, SDLK_KP_ENTER, SDLK_c, SDLK_q, SDLK_x};
        const SDL_Keycode staying[] = {SDLK_ESCAPE, SDLK_SPACE, SDLK_a, SDLK_y, SDLK_n, SDLK_l, SDLK_TAB, SDLK_F1, SDLK_BACKSPACE, SDLK_1, SDLK_UP};
        for (int rows_built = 0; rows_built < 2; ++rows_built) {
            for (SDL_Keycode sym : leaving) {
                for (uint16_t mod : {static_cast<uint16_t>(KMOD_NONE), static_cast<uint16_t>(KMOD_LSHIFT), static_cast<uint16_t>(KMOD_LCTRL)}) {
                    Application app;
                    ApplicationConfig cfg;
                    cfg.headless = true;
                    cfg.start_in_map_select = false;
                    ASSERT_TRUE(app.init(cfg));
                    app.scorecard().show(app.sim().get_world_state().match_result, 0);
                    if (rows_built != 0) app.scorecard().update(0.25f);
                    ASSERT_EQ(app.scorecard().is_waiting(), rows_built == 0);
                    SDL_KeyboardEvent key{};
                    key.type = SDL_KEYDOWN;
                    key.keysym.sym = sym;
                    key.keysym.mod = mod;
                    app.handle_key_down(key);
                    ASSERT_FALSE(app.is_running());                               // Leave = quit()
                }
            }
            for (SDL_Keycode sym : staying) {
                Application app;
                ApplicationConfig cfg;
                cfg.headless = true;
                cfg.start_in_map_select = false;
                ASSERT_TRUE(app.init(cfg));
                app.scorecard().show(app.sim().get_world_state().match_result, 0);
                if (rows_built != 0) app.scorecard().update(0.25f);
                SDL_KeyboardEvent key{};
                key.type = SDL_KEYDOWN;
                key.keysym.sym = sym;
                app.handle_key_down(key);
                ASSERT_TRUE(app.is_running());
                ASSERT_TRUE(app.scorecard().is_open());
                ASSERT_EQ(app.state(), AppState::Playing);                        // Esc does not send the player to the setup screen
            }
        }
    } TEST_END();

    TEST_CASE("6.6 The Results Cue Plays Once, 250 ms After The Screen Opens, When The Rows Appear (Not When The Match Ends)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        app.sim().drop_player(1);
        app.sim().drop_player(2);
        app.sim().drop_player(3);                                                // team 0 is alone: the drop-out decides the match
        ASSERT_TRUE(app.sim().is_match_over());
        app.audio_mixer().stop_all();
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 0u);
        app.update_results(0.016f);                                              // the frame that notices the end opens the screen
        ASSERT_TRUE(app.scorecard().is_open());
        ASSERT_TRUE(app.scorecard().is_waiting());
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 0u);                 // no cue yet
        for (int i = 0; i < 10; ++i) app.update_results(0.016f);                 // 176 ms in all
        ASSERT_TRUE(app.scorecard().is_waiting());
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 0u);
        for (int i = 0; i < 6; ++i) app.update_results(0.016f);                  // 272 ms
        ASSERT_FALSE(app.scorecard().is_waiting());
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 1u);                 // one cue: winner.wav
        for (int i = 0; i < 20; ++i) app.update_results(0.016f);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 1u);                 // still one: nothing plays it again
        ASSERT_EQ(app.scorecard().get_audio_to_play(), 0u);
    } TEST_END();
}

// ============================================================================
// SUITE 7: Input Scheme, Multi-Unit Marquee Selection & Right-Click Abilities
// ============================================================================

// Pointer events of a headless application's window, pushed through the application's own event loop (tests 7.8 - 7.8d). SDL maps a pushed pointer event of
// the window from window to picture coordinates as it maps a real one (the renderer's event watch), so the positions here are window positions; the headless
// window is 1280 x 960 (the picture at scale 2). One tiny frame delivers what is queued (a headless application stops after 10 frames, and a longer frame
// would run the match); the edge scrolling is then stepped as the INPUT task does, every 50 ms.
struct PointerRig {
    Application& app;
    SDL_Window* window{nullptr};
    uint32_t id{0};

    explicit PointerRig(Application& a) : app(a), window(SDL_GetWindowFromID(1)), id(window != nullptr ? SDL_GetWindowID(window) : 0) {}
    void motion(int x, int y, Uint32 state = 0, Uint32 which = 0) const {
        SDL_Event e{};
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = id;
        e.motion.which = which;
        e.motion.state = state;
        e.motion.x = x;
        e.motion.y = y;
        SDL_PushEvent(&e);
    }
    // a left button event; `window_id` 0 is an event of no window (SDL had no mouse focus); `clicks` is SDL's count of the click sequence (2: the second click of a double click)
    void button(Uint32 type, int x, int y, Uint32 which = 0, int64_t window_id = -1, Uint8 clicks = 1) const {
        SDL_Event e{};
        e.type = type;
        e.button.windowID = window_id < 0 ? id : static_cast<Uint32>(window_id);
        e.button.which = which;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = clicks;
        e.button.x = x;
        e.button.y = y;
        SDL_PushEvent(&e);
    }
    // a key event; `repeat` is the auto-repeat of a held key
    void key(Uint32 type, SDL_Keycode sym, bool repeat = false) const {
        SDL_Event e{};
        e.type = type;
        e.key.windowID = id;
        e.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
        e.key.repeat = repeat ? 1 : 0;
        e.key.keysym.sym = sym;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
        SDL_PushEvent(&e);
    }
    void window_event(Uint8 what) const {
        SDL_Event e{};
        e.type = SDL_WINDOWEVENT;
        e.window.windowID = id;
        e.window.event = what;
        SDL_PushEvent(&e);
    }
    void deliver() const { app.run_frame_with_delta(0.001f); }
    void scroll(int ticks_of_20_ms) const {
        for (int i = 0; i < ticks_of_20_ms; ++i) app.handle_camera_panning(0.020f);
    }
    void center() const { app.renderer().camera().center_on(600, 600, app.sim().grid().width(), app.sim().grid().height()); }   // every way open
    int32_t view_x() const { return app.renderer().camera().world_x; }
    int32_t view_y() const { return app.renderer().camera().world_y; }
};

void run_suite_7_input_controls() {
    TEST_SUITE("Suite 7: Input Scheme, Multi-Unit Marquee Selection & Right-Click Abilities");

    TEST_CASE("7.1 Single Click Friendly Selection vs Ground Click Move Order") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Click on ant at (5, 5) -> pixel (176, 176)
        int32_t sx = 176 + HUD::PLAYFIELD_X;
        int32_t sy = 176 + HUD::PLAYFIELD_Y;

        hud.handle_mouse_down(sx, sy, 1, sim, camera);
        hud.handle_mouse_up(sx, sy, 1, sim, camera);

        ASSERT_EQ(hud.get_selected_ant_id(), a1);
        ASSERT_TRUE(hud.is_ant_selected(a1));

        // Click ground at tile (8, 8) -> pixel (272, 272)
        int32_t gx = 272 + HUD::PLAYFIELD_X;
        int32_t gy = 272 + HUD::PLAYFIELD_Y;

        hud.handle_mouse_down(gx, gy, 1, sim, camera);
        hud.handle_mouse_up(gx, gy, 1, sim, camera);

        ASSERT_EQ(hud.get_selected_ant_id(), a1);
        const auto& u = sim.get_unit(a1);
        ASSERT_TRUE(!u.waypoints.empty() || u.state == UnitState::Walking);
    } TEST_END();

    TEST_CASE("7.2 Marquee Drag Box (>4px) Multi-Unit Friendly Selection") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        uint32_t a2 = sim.spawn_unit(0, AntType::Combat, TileCoord{6, 6});
        uint32_t a3 = sim.spawn_unit(0, AntType::Bomber, TileCoord{7, 7});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, TileCoord{6, 5}); // Enemy in box

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Drag marquee from (4*32, 4*32) to (8*32, 8*32)
        int32_t sx1 = 4 * 32 + HUD::PLAYFIELD_X;
        int32_t sy1 = 4 * 32 + HUD::PLAYFIELD_Y;
        int32_t sx2 = 8 * 32 + HUD::PLAYFIELD_X;
        int32_t sy2 = 8 * 32 + HUD::PLAYFIELD_Y;

        hud.handle_mouse_down(sx1, sy1, 1, sim, camera);
        hud.handle_mouse_motion(sx2, sy2, sim, camera);
        hud.handle_mouse_up(sx2, sy2, 1, sim, camera);

        const auto& sel = hud.get_selected_ant_ids();
        ASSERT_EQ(sel.size(), 3u);
        ASSERT_TRUE(hud.is_ant_selected(a1));
        ASSERT_TRUE(hud.is_ant_selected(a2));
        ASSERT_TRUE(hud.is_ant_selected(a3));
        ASSERT_FALSE(hud.is_ant_selected(enemy)); // Enemy not selected by marquee

        // 7.2b: Partial sprite intersection test (marquee grazes top of sprite without encompassing center px/py)
        // a1 at TileCoord{5, 5}: px = 176, py = 176, sprite top is ~150.
        // Drag marquee that only covers y from 100 to 160 (center y=176 is outside, but head is inside):
        hud.clear_selection();
        int32_t p_sx1 = 150 + HUD::PLAYFIELD_X;
        int32_t p_sy1 = 100 + HUD::PLAYFIELD_Y;
        int32_t p_sx2 = 200 + HUD::PLAYFIELD_X;
        int32_t p_sy2 = 160 + HUD::PLAYFIELD_Y; // Stops at 160, before ant center 176

        hud.handle_mouse_down(p_sx1, p_sy1, 1, sim, camera);
        hud.handle_mouse_motion(p_sx2, p_sy2, sim, camera);
        hud.handle_mouse_up(p_sx2, p_sy2, 1, sim, camera);

        ASSERT_TRUE(hud.is_ant_selected(a1)); // Any part of sprite inside marquee selects unit
    } TEST_END();

    TEST_CASE("7.3 Enemy Click with Friendly Selected Dispatches Attack Order") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t friendly = sim.spawn_unit(0, AntType::Combat, TileCoord{5, 5});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, TileCoord{6, 5});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);
        hud.select_ant(friendly);

        // Click enemy ant at (6, 5) -> pixel (208, 176)
        int32_t ex = 208 + HUD::PLAYFIELD_X;
        int32_t ey = 176 + HUD::PLAYFIELD_Y;

        hud.handle_mouse_down(ex, ey, 1, sim, camera);
        hud.handle_mouse_up(ex, ey, 1, sim, camera);

        // The click is a move order onto the enemy's tile that the classification turns into the attack order 3;
        // nothing is hit at the click, the punch lands when the step crosses the tile border
        ASSERT_EQ(sim.get_unit(friendly).orig_order, AntUnit::kOrderAttack);
        ASSERT_EQ(sim.get_unit(enemy).hp, 10u);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(enemy).hp < 10u; }) >= 0);
        ASSERT_EQ(sim.get_unit(enemy).hp, 8u);   // a combat ant punches for 2 hit points
    } TEST_END();

    TEST_CASE("7.4 Right Click Special Abilities (Bomber/Fire/Swimmer/Thief)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{5, 5});
        uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{7, 7});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // 1. Right click with Bomber -> PlantBomb at (6, 5) -> pixel (208, 176)
        hud.select_ant(bomber);
        int32_t bx = 208 + HUD::PLAYFIELD_X;
        int32_t by = 176 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(bx, by, 3, sim, camera);
        hud.handle_mouse_up(bx, by, 3, sim, camera);     // the right button gives its order at the release (FUN_01027b51)
        ASSERT_EQ(sim.get_unit(bomber).orig_order, AntUnit::kOrderPlant);
        // The plant clip plays (1360-1400 ms) and the bomb appears when it ends
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.has_bomb_at(TileCoord{6, 5}); }) >= 1000);

        // 2. Set water at (8, 7) and right click with Swimmer -> BuildBridge -> pixel (272, 240)
        sim.set_terrain(8, 7, 2); // 2 = TERRAIN_WATER
        hud.select_ant(swimmer);
        int32_t sx = 272 + HUD::PLAYFIELD_X;
        int32_t sy = 240 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(sx, sy, 3, sim, camera);
        hud.handle_mouse_up(sx, sy, 3, sim, camera);
        ASSERT_EQ(sim.get_unit(swimmer).orig_order, AntUnit::kOrderBridgeBuild);
        // stage 0x22 exists as soon as the swimmer starts to dig; three passes later the bridge is done
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.has_bridge_at(TileCoord{8, 7}); }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.grid().get_cell(TileCoord{8, 7}).has_completed_bridge(); }) >= 0);
    } TEST_END();

    TEST_CASE("7.5 Hotkeys: Ctrl+A Select All, Ctrl+N / Ctrl+P Cycle, Ctrl+H Home Hill, Esc Deselects, Ctrl+Q Quit Dialog") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        uint32_t a2 = sim.spawn_unit(0, AntType::Combat, TileCoord{15, 15});
        uint32_t a3 = sim.spawn_unit(0, AntType::Fire, TileCoord{25, 25});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Bare 'a' key without Ctrl does NOT trigger hotkey (does not select units)
        hud.handle_key_down('a', sim, camera, 0);
        ASSERT_EQ(hud.get_selected_ant_ids().size(), 0u);

        // 'Ctrl+A' key selects all friendly units
        hud.handle_key_down('a', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_ant_ids().size(), 3u);

        // 'Ctrl+N' key cycles to next ant
        hud.select_ant(a1);
        hud.handle_key_down('n', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_ant_id(), a2);

        hud.handle_key_down('n', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_ant_id(), a3);

        hud.handle_key_down('n', sim, camera, KMOD_CTRL); // Wrap around
        ASSERT_EQ(hud.get_selected_ant_id(), a1);

        // 'Ctrl+P' key cycles to previous ant
        hud.handle_key_down('p', sim, camera, KMOD_CTRL); // Wrap around backwards
        ASSERT_EQ(hud.get_selected_ant_id(), a3);

        // 'Ctrl+C' does nothing: the original has no such key (FUN_0102609a)
        hud.handle_key_down('c', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_ant_id(), a3);

        // 'Esc' deselects everything (FUN_01028c44(0)); it does not open the quit dialog
        hud.handle_key_down(27, sim, camera);
        ASSERT_EQ(hud.get_selected_ant_id(), 0u);
        ASSERT_TRUE(hud.get_selected_ant_ids().empty());
        ASSERT_FALSE(hud.is_quit_dialog_open());

        // 'Ctrl+H' selects the home hill (no hatch, no scrolling)
        hud.handle_key_down('h', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_base_team_id(), 0);

        // 'Ctrl+Q' starts the quit flow like the Quit button; in its dialog Enter does nothing, Esc answers No
        hud.handle_key_down('q', sim, camera, KMOD_CTRL);
        ASSERT_TRUE(hud.is_quit_dialog_open());
        hud.handle_key_down(SDLK_RETURN, sim, camera);
        ASSERT_TRUE(hud.is_quit_dialog_open());
        hud.handle_key_down(27, sim, camera);
        ASSERT_FALSE(hud.is_quit_dialog_open());
    } TEST_END();

    TEST_CASE("7.6 Camera Viewport Edge Panning & Minimap Jump Navigation") {
        ViewportCamera camera;
        camera.x = 100.0f;
        camera.y = 100.0f;
        camera.world_x = 100;
        camera.world_y = 100;
        camera.viewport_w = 442;
        camera.viewport_h = 440;

        // The view moves in whole pixel steps (the original's 50 ms input task) and stays inside the map
        camera.scroll_pixels(24, 24, 60, 60);
        ASSERT_EQ(camera.world_x, 124);
        ASSERT_EQ(camera.world_y, 124);
        camera.scroll_pixels(-500, -500, 60, 60);
        ASSERT_EQ(camera.world_x, 0);
        ASSERT_EQ(camera.world_y, 0);

        // Center on tile (30, 30) -> world px (960, 960)
        camera.center_on(960, 960, 60, 60);
        int32_t expected_x = 960 - 442 / 2;
        int32_t expected_y = 960 - 440 / 2;
        ASSERT_EQ(camera.world_x, expected_x);
        ASSERT_EQ(camera.world_y, expected_y);
    } TEST_END();

    TEST_CASE("7.6b A Map With A Start Marker Outside The Grid Is Not Started (Application::load_match Asks LevelData::validate For The Teams Of The Match; The Original Has No Behaviour For It)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);

        // the file loads, the match with all four teams is refused
        const std::filesystem::path unplayable = write_marker_outside_grid_map();
        ASSERT_FALSE(unplayable.empty());
        const bool started = app.start_game(unplayable.string());
        std::error_code ignore;
        std::filesystem::remove(unplayable, ignore);
        ASSERT_FALSE(started);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.match_running());                                  // the setup screen is not a running match

        // the shipped map starts as ever
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/TINY.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.match_running());
    } TEST_END();

    TEST_CASE("7.6c A Game That Starts Straight Into Its Match (--map) Refuses The Same Map: Application::init Asks LevelData::validate For All Four Teams; The Setup Screen Starts Without Asking (The Match Is Checked When It Starts)") {
        const std::filesystem::path unplayable = write_marker_outside_grid_map();
        ASSERT_FALSE(unplayable.empty());
        {
            Application direct;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.default_map_path = unplayable.string();
            ASSERT_FALSE(direct.init(cfg));
            direct.shutdown();
        }
        {
            Application direct;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
            ASSERT_TRUE(direct.init(cfg));
            ASSERT_EQ(direct.state(), AppState::Playing);
            direct.shutdown();
        }
        {
            Application setup;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            cfg.default_map_path = unplayable.string();
            ASSERT_TRUE(setup.init(cfg));
            ASSERT_EQ(setup.state(), AppState::MapSelect);
            ASSERT_FALSE(setup.start_game(unplayable.string()));
            ASSERT_EQ(setup.state(), AppState::MapSelect);
            setup.shutdown();
        }
        std::error_code ignore;
        std::filesystem::remove(unplayable, ignore);
    } TEST_END();

    TEST_CASE("7.7 Cursor Simulated in Screen Middle On Game Start (No Unwanted Edge Panning)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true; // Start in map select screen
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);

        // Pick map and start game
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);

        // Cursor simulated in middle of screen (320, 240) and has not yet moved
        ASSERT_EQ(app.mouse_screen_x(), 320);
        ASSERT_EQ(app.mouse_screen_y(), 240);
        ASSERT_FALSE(app.mouse_has_moved());

        // The "get ready" modal (at least 5 s) is a dialog: no edge scrolling while it is open (Ants.exe: a dialog gets all input)
        ASSERT_TRUE(app.hud().is_modal_open());
        app.hud().update(app.sim().get_world_state(), 100);
        ASSERT_FALSE(app.hud().is_modal_open());

        // Set camera away from boundaries using center_on so panning in any direction is unclamped
        app.renderer().camera().center_on(600, 600, app.sim().grid().width(), app.sim().grid().height());
        int32_t init_cam_x = app.renderer().camera().world_x;
        int32_t init_cam_y = app.renderer().camera().world_y;

        // Step camera updates without any mouse motion event
        for (int i = 0; i < 20; ++i) {
            app.handle_camera_panning(0.020f);
        }

        // Camera must NOT have panned (stays centered at 600, 600)
        ASSERT_EQ(app.renderer().camera().world_x, init_cam_x);
        ASSERT_EQ(app.renderer().camera().world_y, init_cam_y);

        // Keyboard keys (W, S, Up, Down, Left, Right) must NOT pan the camera at all
        const int32_t pan_test_keys[] = { SDLK_w, SDLK_s, SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, 'w', 's', 'a', 'd' };
        for (int32_t k : pan_test_keys) {
            SDL_KeyboardEvent key{};
            key.type = SDL_KEYDOWN;
            key.keysym.sym = k;
            app.handle_key_down(key);
            for (int i = 0; i < 5; ++i) {
                app.handle_camera_panning(0.020f);
            }
            ASSERT_EQ(app.renderer().camera().world_x, init_cam_x);
            ASSERT_EQ(app.renderer().camera().world_y, init_cam_y);
        }

        // Simulate actual mouse motion into the 12 px band of the top-left corner (x=10, y=10): the arrow shows, but only the 5 px
        // inner strip scrolls (Ants.exe FUN_01026aa3), so the view stays
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = 10;
        motion.y = 10;
        app.handle_mouse_motion(motion);

        ASSERT_TRUE(app.mouse_has_moved());
        ASSERT_EQ(app.mouse_screen_x(), 10);
        ASSERT_EQ(app.mouse_screen_y(), 10);
        for (int i = 0; i < 6; ++i) {
            app.handle_camera_panning(0.020f);
        }
        ASSERT_EQ(app.renderer().camera().world_x, init_cam_x);
        ASSERT_EQ(app.renderer().camera().world_y, init_cam_y);

        // In the 5 px inner strip (x=2, y=2) the view scrolls at the 50 ms rate of the original's input task (two ticks = 100 ms)
        motion.x = 2;
        motion.y = 2;
        app.handle_mouse_motion(motion);
        for (int i = 0; i < 6; ++i) {
            app.handle_camera_panning(0.020f);
        }
        ASSERT_LT(app.renderer().camera().world_x, init_cam_x);
        ASSERT_LT(app.renderer().camera().world_y, init_cam_y);
    } TEST_END();

    TEST_CASE("7.8 A Pointer Beyond The Picture (What SDL Reports Over A Black Bar Of A Wide Window) Is The Pointer On The Edge Pixel: The Map Scrolls And The Game's Cursor Is Drawn There (On A One-Monitor Machine The Original's Exclusive 640 x 480 Mode Keeps Its Pointer On The Picture)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);
        app.hud().update(app.sim().get_world_state(), 100);                        // the get-ready dialog is gone: the edges scroll
        ASSERT_FALSE(app.hud().is_modal_open());
        const PointerRig rig(app);
        ASSERT_TRUE(rig.window != nullptr);
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(rig.window, &win_w, &win_h);
        ASSERT_EQ(win_w, 1280);                                                    // the headless window: the picture at scale 2, no bars (SDL maps a window
        ASSERT_EQ(win_h, 960);                                                     // position to the picture: x / 2, y / 2)

        // far to the left of the picture (what SDL reports for a pointer over the left bar: a negative x): the view scrolls west, the cursor is drawn on x = 0
        rig.center();
        const int32_t x0 = rig.view_x();
        rig.motion(-4000, 480);
        rig.deliver();
        rig.scroll(12);
        ASSERT_EQ(app.mouse_screen_x(), 0);
        ASSERT_EQ(app.mouse_screen_y(), 240);
        ASSERT_FALSE(app.pointer_outside());                                       // (render_frame draws the game's cursor unless the pointer is outside)
        ASSERT_LT(rig.view_x(), x0);
        // far to the right: east, the cursor on x = 639
        rig.center();
        const int32_t x1 = rig.view_x();
        rig.motion(40000, 480);
        rig.deliver();
        rig.scroll(12);
        ASSERT_EQ(app.mouse_screen_x(), 639);
        ASSERT_FALSE(app.pointer_outside());
        ASSERT_GT(rig.view_x(), x1);
        // above and below the picture (a window that is taller than 4:3: bars at the top and bottom)
        rig.center();
        const int32_t y0 = rig.view_y();
        rig.motion(640, -4000);
        rig.deliver();
        rig.scroll(12);
        ASSERT_EQ(app.mouse_screen_y(), 0);
        ASSERT_FALSE(app.pointer_outside());
        ASSERT_LT(rig.view_y(), y0);
        rig.center();
        const int32_t y1 = rig.view_y();
        rig.motion(640, 40000);
        rig.deliver();
        rig.scroll(12);
        ASSERT_EQ(app.mouse_screen_y(), 479);
        ASSERT_FALSE(app.pointer_outside());
        ASSERT_GT(rig.view_y(), y1);
        // a button event beyond the picture lands on the edge too (here the press and the release are also beyond the window: see 7.8c for what the release does)
        rig.button(SDL_MOUSEBUTTONDOWN, -4000, 40000);
        rig.deliver();
        ASSERT_EQ(app.mouse_screen_x(), 0);
        ASSERT_EQ(app.mouse_screen_y(), 479);
        rig.button(SDL_MOUSEBUTTONUP, -4000, 40000);
        rig.deliver();
        ASSERT_EQ(app.mouse_screen_x(), 0);
        ASSERT_EQ(app.mouse_screen_y(), 479);
        // a pointer that is on the picture is left alone: window (300, 200) is picture (150, 100)
        rig.motion(300, 200);
        rig.deliver();
        ASSERT_EQ(app.mouse_screen_x(), 150);
        ASSERT_EQ(app.mouse_screen_y(), 100);
        ASSERT_FALSE(app.pointer_outside());
        app.shutdown();
    } TEST_END();

    TEST_CASE("7.8b The Setup Screen And The Quick Help Get The Same Clamped Pointer: The Clamp Is At The Top Of The Event Loop, Before Any Screen Takes The Event") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);
        const PointerRig rig(app);
        ASSERT_TRUE(rig.window != nullptr);
        for (int screen = 0; screen < 2; ++screen) {
            if (screen == 1) {
                app.finish_loading();                                              // the quick help (the stored option is on), as 9.14 reaches it
                ASSERT_EQ(app.state(), AppState::QuickHelp);
            }
            rig.motion(-4000, 40000);                                              // beyond the bottom left corner of the picture
            rig.deliver();
            ASSERT_EQ(app.mouse_screen_x(), 0);
            ASSERT_EQ(app.mouse_screen_y(), 479);
            rig.motion(300, 200);
            rig.deliver();
            ASSERT_EQ(app.mouse_screen_x(), 150);
            rig.button(SDL_MOUSEBUTTONDOWN, 40000, -4000);                         // beyond the top right corner
            rig.button(SDL_MOUSEBUTTONUP, 40000, -4000);
            rig.deliver();
            ASSERT_EQ(app.mouse_screen_x(), 639);
            ASSERT_EQ(app.mouse_screen_y(), 0);
            ASSERT_EQ(app.state(), screen == 0 ? AppState::MapSelect : AppState::QuickHelp);   // (the corner pixel is on no button)
        }
        app.shutdown();
    } TEST_END();

    TEST_CASE("7.8c A Button Released Outside The Window Ends The Pointer's Stay Although No LEAVE Comes (macOS After A Drag That Left The Window; The Web After Its LEAVE): The Map Does Not Scroll On Its Own. While The Button Is Held The Pointer Stays On The Edge; A Release Over A Black Bar Is A Release In The Window") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        app.hud().update(app.sim().get_world_state(), 100);
        ASSERT_FALSE(app.hud().is_modal_open());
        const PointerRig rig(app);
        ASSERT_TRUE(rig.window != nullptr);
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(rig.window, &win_w, &win_h);
        ASSERT_EQ(win_w, 1280);
        ASSERT_EQ(win_h, 960);

        // the press on the map, a drag out of the window to the right, the release out there; SDL on macOS sends no LEAVE after it (the capture of the drag
        // ends with the release)
        rig.motion(500, 400);                                                      // picture (250, 200): the map
        rig.button(SDL_MOUSEBUTTONDOWN, 500, 400);
        rig.motion(1000, 450, SDL_BUTTON_LMASK);
        rig.motion(1900, 480, SDL_BUTTON_LMASK);                                   // beyond the window's right edge: picture (950, 240)
        rig.deliver();
        ASSERT_EQ(app.mouse_screen_x(), 639);                                      // while the button is held the pointer stays on the edge pixel, as the
        ASSERT_EQ(app.mouse_screen_y(), 240);                                      // one-monitor original's pointer on its screen
        ASSERT_FALSE(app.pointer_outside());
        rig.button(SDL_MOUSEBUTTONUP, 1900, 480);
        rig.deliver();
        ASSERT_TRUE(app.pointer_outside());                                        // the pointer has gone: no cursor, no hover, no edge scroll
        ASSERT_EQ(app.mouse_screen_x(), 639);                                      // (its last position is kept: the pointer is one global)
        rig.center();
        const int32_t x0 = rig.view_x();
        const int32_t y0 = rig.view_y();
        rig.scroll(100);                                                           // 2 s of the 50 ms input task
        ASSERT_EQ(rig.view_x(), x0);
        ASSERT_EQ(rig.view_y(), y0);
        rig.motion(600, 400);                                                      // the pointer comes back: its first motion in the window ends it
        rig.deliver();
        ASSERT_FALSE(app.pointer_outside());
        ASSERT_EQ(app.mouse_screen_x(), 300);
        ASSERT_EQ(app.mouse_screen_y(), 200);

        // the web build: the pointer leaves the canvas while the button is held (a LEAVE: no capture there), the release comes afterwards and names no window
        // (SDL has no mouse focus then, so it did not map the position to the picture either)
        rig.motion(4, 240);                                                        // picture (2, 120): the west scroll strip
        rig.button(SDL_MOUSEBUTTONDOWN, 4, 240);
        rig.window_event(SDL_WINDOWEVENT_LEAVE);
        rig.button(SDL_MOUSEBUTTONUP, 4, 240, 0, 0);                               // window position (4, 240), windowID 0
        rig.deliver();
        ASSERT_TRUE(app.pointer_outside());
        rig.center();
        const int32_t x1 = rig.view_x();
        rig.scroll(100);
        ASSERT_EQ(rig.view_x(), x1);

        // a release over a black bar is a release in the window: the pointer stays there, on the edge pixel, and the map scrolls on
        SDL_SetWindowSize(rig.window, 1920, 960);                                  // a wide window: the picture 1280 x 960 in the middle, bars of 320 px
        rig.deliver();
        rig.motion(100, 480);                                                      // the left bar: picture (-110, 240), the pointer on (0, 240)
        rig.button(SDL_MOUSEBUTTONDOWN, 100, 480);
        rig.button(SDL_MOUSEBUTTONUP, 100, 480);
        rig.deliver();
        ASSERT_EQ(app.mouse_screen_x(), 0);
        ASSERT_EQ(app.mouse_screen_y(), 240);
        ASSERT_FALSE(app.pointer_outside());
        rig.center();
        const int32_t x2 = rig.view_x();
        rig.scroll(12);
        ASSERT_LT(rig.view_x(), x2);
        app.shutdown();
    } TEST_END();

    TEST_CASE("7.8d A Lifted Finger Leaves No Pointer Behind (Touch Arrives As Mouse Events With which = SDL_TOUCH_MOUSEID): A Tap On A Black Bar Or In The Edge Strip Does Not Keep The Map Scrolling; A Mouse Click In The Same Places Still Leaves The Pointer There") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        app.hud().update(app.sim().get_world_state(), 100);
        ASSERT_FALSE(app.hud().is_modal_open());
        const PointerRig rig(app);
        ASSERT_TRUE(rig.window != nullptr);
        SDL_SetWindowSize(rig.window, 1920, 960);                                  // a wide window (a phone in landscape): bars of 320 px on both sides
        rig.deliver();
        auto tap = [&](int x, int y, Uint32 which) {                               // what SDL sends for a tap: a motion, the press, the release
            rig.motion(x, y, 0, which);
            rig.button(SDL_MOUSEBUTTONDOWN, x, y, which);
            rig.button(SDL_MOUSEBUTTONUP, x, y, which);
            rig.deliver();
        };
        // the places: the left bar (window x 100: the pointer on picture x 0) and the picture's own 5 px strip (window x 324: picture x 2)
        for (const int x : {100, 324}) {
            const int32_t px = x == 100 ? 0 : 2;
            // a finger: once it is lifted the pointer is gone and the view stays (without that rule it ran to the map's edge until the next touch)
            tap(x, 480, SDL_TOUCH_MOUSEID);
            ASSERT_EQ(app.mouse_screen_x(), px);
            ASSERT_EQ(app.mouse_screen_y(), 240);
            ASSERT_TRUE(app.pointer_outside());
            rig.center();
            const int32_t t0 = rig.view_x();
            rig.scroll(100);                                                       // 2 s
            ASSERT_EQ(rig.view_x(), t0);
            // the mouse: its pointer is still there, so the view scrolls west as before
            tap(x, 480, 0);
            ASSERT_EQ(app.mouse_screen_x(), px);
            ASSERT_FALSE(app.pointer_outside());
            rig.center();
            const int32_t m0 = rig.view_x();
            rig.scroll(12);
            ASSERT_LT(rig.view_x(), m0);
        }
        app.shutdown();
    } TEST_END();

    TEST_CASE("7.8e The Pointer Grab: --fullscreen Asks For It, A Window Does Not; Entering And Leaving Fullscreen Ask And Let Go; SDL Holds The Pointer While The Window Has The Focus, Lets Go For Another Window, Takes It Back With The Focus, And Not For A Window That Left Fullscreen Meanwhile (A Real Window Of SDL's Dummy Video Driver, No Match)") {
        // A real window (headless = false) without a screen: SDL's dummy video driver. The settings go to a file of this run (never the player's own), no
        // match starts (so nothing is written at the end) and the sound goes to SDL's dummy audio driver. What the game asks for is SDL's grab flag; what SDL
        // makes of it (SDL_GetWindowMouseGrab) needs the input focus. SDL 2.32's dummy driver gives a window that is shown the focus; SDL 2.26 (Debian 12) and
        // 2.28 give none, so SDL holds nothing there: the requests are checked everywhere, the focus part only where the driver moves the focus.
        const std::filesystem::path settings = temp_path_of_this_run("ants_grab_settings").replace_extension(".ini");
        auto open = [&](Application& app, bool fullscreen) {
            SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");                            // (SDL_Quit clears the hints: set before every start)
            SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
            ApplicationConfig cfg;
            cfg.headless = false;
            cfg.skip_intro = true;                                                 // the setup screen at once
            cfg.fullscreen = fullscreen;
            cfg.settings_path = settings.string();
            return app.init(cfg);
        };
        auto frame = [](Application& app) { app.run_frame_with_delta(0.001f); };
        auto asked = [](SDL_Window* w) { return (SDL_GetWindowFlags(w) & SDL_WINDOW_MOUSE_GRABBED) != 0; };     // the game's request
        auto held = [](SDL_Window* w) { return SDL_GetWindowMouseGrab(w) == SDL_TRUE; };                      // SDL's grab
        auto focused = [](SDL_Window* w) { return (SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) != 0; };
        {
            Application app;
            ASSERT_TRUE(open(app, true));
            SDL_Window* window = SDL_GetWindowFromID(1);
            ASSERT_TRUE(window != nullptr);
            ASSERT_TRUE((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP);
            ASSERT_TRUE(asked(window));                                            // --fullscreen: asked for from the start
            ASSERT_EQ(held(window), focused(window));                              // ... and held while the window has the focus
            SDL_SetWindowFullscreen(window, 0);                                    // back to a window
            frame(app);
            ASSERT_FALSE(asked(window));
            ASSERT_FALSE(held(window));
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);        // and fullscreen again
            frame(app);
            ASSERT_TRUE(asked(window));
            ASSERT_EQ(held(window), focused(window));
            if (focused(window)) {                                                 // (a driver that moves the focus: SDL 2.32's dummy driver)
                auto focus_elsewhere = [&]() {
                    SDL_Window* other = SDL_CreateWindow("other", 0, 0, 64, 64, SDL_WINDOW_SHOWN);   // (the dummy driver gives a window that is shown the focus)
                    frame(app);
                    return other;
                };
                auto focus_back = [&](SDL_Window* other) {
                    SDL_DestroyWindow(other);
                    SDL_HideWindow(window);
                    SDL_ShowWindow(window);
                    frame(app);
                };
                // another window takes the focus: SDL lets go of the pointer, the request stands; the focus returns: SDL holds it again
                SDL_Window* other = focus_elsewhere();
                ASSERT_TRUE(other != nullptr);
                ASSERT_FALSE(focused(window));
                ASSERT_FALSE(held(window));
                ASSERT_TRUE(asked(window));
                focus_back(other);
                ASSERT_TRUE(focused(window));
                ASSERT_TRUE((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP);
                ASSERT_TRUE(held(window));
                // the window leaves fullscreen while another window has the focus: when the focus returns it is a plain window, and nothing holds the pointer
                other = focus_elsewhere();
                ASSERT_TRUE(other != nullptr);
                SDL_SetWindowFullscreen(window, 0);
                frame(app);
                ASSERT_FALSE(asked(window));
                focus_back(other);
                ASSERT_TRUE(focused(window));
                ASSERT_FALSE(held(window));
            } else {
                std::cout << "(7.8e: this SDL's dummy driver gives no window the focus: the focus part is not run) ";
            }
            app.shutdown();
        }
        {
            Application app;
            ASSERT_TRUE(open(app, false));
            SDL_Window* window = SDL_GetWindowFromID(1);
            ASSERT_TRUE(window != nullptr);
            ASSERT_FALSE(asked(window));                                           // a window: the pointer is free (four games on one screen, other programs)
            ASSERT_FALSE(held(window));
            frame(app);
            ASSERT_FALSE(asked(window));
            ASSERT_FALSE(held(window));
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
            frame(app);
            ASSERT_TRUE(asked(window));
            ASSERT_EQ(held(window), focused(window));
            app.shutdown();
        }
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
    } TEST_END();

    TEST_CASE("7.8f A FULLSCREEN Window Whose Shape Is Not The Picture's Has Bars Inside The Window (SDL Maps A Position Over A Bar To One Outside The Picture): The Pointer Over A Bar Is On The Picture's Nearest Edge Pixel, Corners Included, And Is Not Gone; A Window Of Another Shape (4:3 Picture In A Wide Window) Does The Same At The Sides (A Real Fullscreen Window Of SDL's Dummy Video Driver, The 16:9 Canvas, The Setup Screen)") {
        // The same clamp as 7.8 .. 7.8d, but in a window that is FULLSCREEN (SDL_WINDOW_FULLSCREEN_DESKTOP, which is how the owner's 16:10 screen shows the 16:9 picture: bars of 45 rows above and
        // below at 1440 x 900, of 66 at 1512 x 982): the clamp does not look at fullscreen, so the bars of a window and the bars of a fullscreen screen are the same rule. The dummy driver's
        // display is 4:3 (1024 x 768 with SDL 2.32), so a 16:9 canvas has bars above and below (96 rows) and the 4:3 canvas none; what the window really is decides the geometry (CanvasLayout::fit is
        // SDL's own arithmetic, pinned in test_canvas_layout), and a driver whose display happens to have the canvas's shape has no bars to test.
        const std::filesystem::path settings = temp_path_of_this_run("ants_bars_settings").replace_extension(".ini");
        auto open = [&](Application& app, Aspect aspect) {
            SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");                            // (SDL_Quit clears the hints: set before every start)
            SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
            ApplicationConfig cfg;
            cfg.headless = false;
            cfg.skip_intro = true;                                                 // the setup screen at once (no match: nothing is written at the end)
            cfg.fullscreen = true;
            cfg.aspect = aspect;
            cfg.aspect_given = true;
            cfg.settings_path = settings.string();
            return app.init(cfg);
        };
        {
            Application app;
            ASSERT_TRUE(open(app, Aspect::Wide16x9));
            SDL_Window* window = SDL_GetWindowFromID(1);
            ASSERT_TRUE(window != nullptr);
            ASSERT_TRUE((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP);
            app.run_frame_with_delta(0.001f);
            int w = 0;
            int h = 0;
            SDL_GetWindowSize(window, &w, &h);
            const CanvasFit fit = app.canvas().fit(w, h);
            ASSERT_EQ(app.picture(), (LayoutRect{0, 0, 960, 540}));               // (the wide setup screen is the whole canvas)
            if (fit.bar_top() >= 4 && fit.bar_bottom() >= 4) {
                auto at = [&](int wx, int wy) {
                    SDL_Event e{};
                    e.type = SDL_MOUSEMOTION;
                    e.motion.windowID = SDL_GetWindowID(window);
                    e.motion.x = wx;
                    e.motion.y = wy;
                    SDL_PushEvent(&e);
                    app.run_frame_with_delta(0.001f);
                };
                const int mid_x = w / 2;
                const int mid_picture = static_cast<int>(static_cast<double>(mid_x - fit.viewport.x) / fit.scale);
                at(mid_x, fit.bar_top() / 2);                                      // in the middle of the top bar, level with the window's middle
                ASSERT_EQ(app.mouse_screen_y(), 0);
                ASSERT_TRUE(std::abs(app.mouse_screen_x() - mid_picture) <= 1);
                ASSERT_FALSE(app.pointer_outside());
                at(mid_x, fit.out_h - fit.bar_bottom() / 2);                       // the bottom bar
                ASSERT_EQ(app.mouse_screen_y(), 539);
                ASSERT_TRUE(std::abs(app.mouse_screen_x() - mid_picture) <= 1);
                ASSERT_FALSE(app.pointer_outside());
                at(0, 1);                                                          // the window's top left corner, over the top bar: the picture's first pixel
                ASSERT_TRUE(app.mouse_screen_x() <= 1 && app.mouse_screen_y() == 0);
                at(w - 1, h - 1);                                                  // the window's bottom right corner: the picture's last pixel
                ASSERT_EQ(app.mouse_screen_x(), 959);
                ASSERT_EQ(app.mouse_screen_y(), 539);
                ASSERT_FALSE(app.pointer_outside());
            } else {
                std::cout << "(7.8f: this driver's display has the 16:9 canvas's shape: no bars to test) ";
            }
            app.shutdown();
        }
        {                                                                          // the other way: the original's 4:3 picture in a wide window has its bars at the sides
            Application app;
            ASSERT_TRUE(open(app, Aspect::Classic4x3));
            SDL_Window* window = SDL_GetWindowFromID(1);
            ASSERT_TRUE(window != nullptr);
            app.run_frame_with_delta(0.001f);
            SDL_SetWindowFullscreen(window, 0);                                    // a window of the shape of a 16:10 screen (what the way out of fullscreen can give, and the shape that fullscreen has there)
            SDL_SetWindowSize(window, 1440, 900);
            app.run_frame_with_delta(0.001f);
            int w = 0;
            int h = 0;
            SDL_GetWindowSize(window, &w, &h);
            const CanvasFit fit = app.canvas().fit(w, h);
            if (w == 1440 && h == 900 && fit.bar_left() > 4) {
                auto at = [&](int wx, int wy) {
                    SDL_Event e{};
                    e.type = SDL_MOUSEMOTION;
                    e.motion.windowID = SDL_GetWindowID(window);
                    e.motion.x = wx;
                    e.motion.y = wy;
                    SDL_PushEvent(&e);
                    app.run_frame_with_delta(0.001f);
                };
                ASSERT_EQ(fit.viewport.w, 1200);                                   // the picture 1200 x 900 (scale 1.875), bars of 120 columns at the sides
                ASSERT_EQ(fit.bar_left(), 120);
                at(40, 450);                                                       // the left bar, level with the middle
                ASSERT_EQ(app.mouse_screen_x(), 0);
                ASSERT_EQ(app.mouse_screen_y(), 240);
                ASSERT_FALSE(app.pointer_outside());
                at(1400, 450);                                                     // the right bar
                ASSERT_EQ(app.mouse_screen_x(), 639);
                ASSERT_EQ(app.mouse_screen_y(), 240);
                at(10, 899);                                                       // the bottom left corner of the window
                ASSERT_EQ(app.mouse_screen_x(), 0);
                ASSERT_EQ(app.mouse_screen_y(), 479);
            } else {
                std::cout << "(7.8f: this driver would not give the window a 16:10 shape: the pillarbox part is not run) ";
            }
            app.shutdown();
        }
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
    } TEST_END();
}

// ============================================================================
// SUITE 8: Map Selection Screen (the original has no in-world health bar)
// ============================================================================
void run_suite_8_unit_health_and_map_select() {
    TEST_SUITE("Suite 8: Unit Health Display & Map Selection Screen");

    TEST_CASE("8.2 Setup Screen: The List Is Every .lvl Of The Maps Folder In The Byte Order Of The File Names (GAUNTLET First); Up / Down Step And Wrap; Enter, S And s Start; Q, q, X And x Leave; Nothing Else Does Anything (FUN_01013de7, FUN_01014076)") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");

        const auto& maps = screen.get_maps();
        ASSERT_EQ(maps.size(), 6u);
        ASSERT_EQ(maps[0].filename, "GAUNTLET.LVL");                              // the first of the sorted names
        for (size_t i = 1; i < maps.size(); ++i) ASSERT_TRUE(maps[i - 1].filename < maps[i].filename);
        const char* order[] = {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"};
        for (size_t i = 0; i < maps.size(); ++i) ASSERT_EQ(maps[i].filename, order[i]);
        for (const auto& m : maps) {
            if (m.filename == "TREASURE.LVL") {
                ASSERT_EQ(m.width, 60u);
                ASSERT_EQ(m.height, 60u);
            }
        }

        // Initial selection: TREASURE.LVL, the last entry of this list (the one deliberate deviation of the screen, at the owner's request: the original highlights the first entry; 8.9 has it)
        ASSERT_EQ(screen.get_selected_index(), 5);
        ASSERT_EQ(maps[5].filename, "TREASURE.LVL");
        ASSERT_FALSE(screen.get_selected_map_path().empty());
        // The rest of this test walks the list from its first entry, as the original's screen starts
        screen.set_selected_index(0);
        ASSERT_EQ(screen.get_selected_index(), 0);

        // Down: next map, Up: previous map, both wrap round
        screen.handle_key_down(SDLK_DOWN);
        ASSERT_EQ(screen.get_selected_index(), 1);
        screen.handle_key_down(SDLK_UP);
        ASSERT_EQ(screen.get_selected_index(), 0);
        screen.handle_key_down(SDLK_UP);
        ASSERT_EQ(screen.get_selected_index(), static_cast<int32_t>(maps.size() - 1));
        screen.handle_key_down(SDLK_DOWN);
        ASSERT_EQ(screen.get_selected_index(), 0);

        std::string started_map;
        int starts = 0;
        int leaves = 0;
        screen.set_on_start([&](const std::string& path) { started_map = path; ++starts; });
        screen.set_on_quit([&]() { ++leaves; });

        // The keys that the original's handler does not know do nothing: digits, Left / Right, Space, F, D, Esc, Tab, letters
        const SDL_Keycode dead[] = {SDLK_1, SDLK_4, SDLK_6, SDLK_LEFT, SDLK_RIGHT, SDLK_SPACE, SDLK_f, SDLK_d, SDLK_ESCAPE, SDLK_TAB, SDLK_a, SDLK_y, SDLK_n, SDLK_F1, SDLK_BACKSPACE};
        for (SDL_Keycode k : dead) screen.handle_key_down(k);
        ASSERT_EQ(screen.get_selected_index(), 0);
        ASSERT_EQ(starts, 0);
        ASSERT_EQ(leaves, 0);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());

        // START: Enter, the keypad's Enter, S and s
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_key_down(SDLK_DOWN);                                          // SMALL.LVL
        for (SDL_Keycode k : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_s}) {
            started_map.clear();
            screen.handle_key_down(k);
            ASSERT_TRUE(started_map.find("SMALL.LVL") != std::string::npos);
        }
        ASSERT_EQ(starts, 3);
        // LEAVE: Q, q, X and x (SDL reports the letter's lower case key whatever the shift state)
        for (SDL_Keycode k : {SDLK_q, SDLK_x}) screen.handle_key_down(k);
        ASSERT_EQ(leaves, 2);
    } TEST_END();

    TEST_CASE("8.3 Setup Screen Buttons Are The Original's Button Class: The Press Captures (Pressed Art, Click Sound), The Release Acts, Leaving The Button Cancels For Good") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");
        screen.set_selected_index(0);                                               // (the screen starts on TREASURE.LVL, see 8.9; this test steps through the list from its first entry)
        std::vector<uint32_t> sounds;
        screen.set_on_play_sfx([&](uint32_t s) { sounds.push_back(s); });

        std::string launched_path;
        screen.set_on_start([&](const std::string& path) { launched_path = path; });

        const int32_t dx = MapSelectScreen::BTN_DOWN_X + 10;
        const int32_t dy = MapSelectScreen::BTN_DOWN_Y + 10;
        // the press alone changes nothing but plays the click of the pressed picture
        screen.handle_mouse_motion(dx, dy);
        screen.handle_mouse_down(dx, dy, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 0);
        ASSERT_EQ(sounds.size(), 1u);
        ASSERT_EQ(sounds.back(), SoundID::ButtonClick);
        // the release inside runs the action: the second map
        screen.handle_mouse_up(dx, dy, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 1);
        ASSERT_TRUE(launched_path.empty());                                         // stepping does not start

        // leaving the button while it is held cancels it for good: coming back before the release does not bring it back
        screen.handle_mouse_down(dx, dy, SDL_BUTTON_LEFT);
        screen.handle_mouse_motion(dx + 200, dy);
        screen.handle_mouse_motion(dx, dy);
        screen.handle_mouse_up(dx, dy, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 1);
        // a release elsewhere does nothing either
        screen.handle_mouse_down(dx, dy, SDL_BUTTON_LEFT);
        screen.handle_mouse_up(dx + 200, dy, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 1);
        // a press elsewhere and a release on the button: no capture, no action
        screen.handle_mouse_down(dx + 200, dy, SDL_BUTTON_LEFT);
        screen.handle_mouse_up(dx, dy, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 1);
        // the right button does nothing
        screen.handle_mouse_down(dx, dy, SDL_BUTTON_RIGHT);
        screen.handle_mouse_up(dx, dy, SDL_BUTTON_RIGHT);
        ASSERT_EQ(screen.get_selected_index(), 1);

        // START: press and release on the button
        const int32_t sx = MapSelectScreen::BTN_START_X + 20;
        const int32_t sy = MapSelectScreen::BTN_START_Y + 10;
        screen.handle_mouse_down(sx, sy, SDL_BUTTON_LEFT);
        ASSERT_TRUE(launched_path.empty());
        screen.handle_mouse_up(sx, sy, SDL_BUTTON_LEFT);
        ASSERT_TRUE(launched_path.find("ISLANDS.LVL") != std::string::npos);          // the second entry of the sorted list

        // the pressed art follows the state: hover, pressed, up again
        ASSERT_FALSE(screen.is_locked());
        screen.lock();                                                              // START ran in the application: the screen is locked (+0x130)
        ASSERT_TRUE(screen.is_locked());
        launched_path.clear();
        screen.handle_mouse_down(dx, dy, SDL_BUTTON_LEFT);
        screen.handle_mouse_up(dx, dy, SDL_BUTTON_LEFT);
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_mouse_down(sx, sy, SDL_BUTTON_LEFT);
        screen.handle_mouse_up(sx, sy, SDL_BUTTON_LEFT);
        screen.handle_key_down(SDLK_RETURN);
        ASSERT_EQ(screen.get_selected_index(), 1);                                  // nothing moves, nothing starts twice
        ASSERT_TRUE(launched_path.empty());
        screen.enter();                                                             // a new screen is unlocked
        ASSERT_FALSE(screen.is_locked());
    } TEST_END();

    TEST_CASE("8.5 Setup Screen: The Fog Of War Pair Is Silent, Off By Default And Acts At The Release; There Are No Other Click Targets (Map Box, Description Box, Thumbs, Drop)") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");
        const int32_t start_index = screen.get_selected_index();                    // (TREASURE.LVL, see 8.9: what matters here is that no click target below moves it)
        std::vector<uint32_t> sounds;
        screen.set_on_play_sfx([&](uint32_t s) { sounds.push_back(s); });
        int starts = 0;
        int leaves = 0;
        screen.set_on_start([&](const std::string&) { ++starts; });
        screen.set_on_quit([&]() { ++leaves; });

        // Default: Fog of War disabled
        ASSERT_FALSE(screen.is_fog_of_war_enabled());

        // "On": the press does nothing (and plays nothing: d_on3 is silent), the release switches it on
        const int32_t on_x = MapSelectScreen::BTN_FOW_ON_X + 5;
        const int32_t on_y = MapSelectScreen::BTN_FOW_ON_Y + 5;
        screen.handle_mouse_motion(on_x, on_y);
        screen.handle_mouse_down(on_x, on_y, SDL_BUTTON_LEFT);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());
        ASSERT_TRUE(sounds.empty());
        screen.handle_mouse_up(on_x, on_y, SDL_BUTTON_LEFT);
        ASSERT_TRUE(screen.is_fog_of_war_enabled());

        // "Off" likewise
        const int32_t off_x = MapSelectScreen::BTN_FOW_OFF_X + 5;
        const int32_t off_y = MapSelectScreen::BTN_FOW_OFF_Y + 5;
        screen.handle_mouse_motion(off_x, off_y);
        screen.handle_mouse_down(off_x, off_y, SDL_BUTTON_LEFT);
        ASSERT_TRUE(screen.is_fog_of_war_enabled());
        screen.handle_mouse_up(off_x, off_y, SDL_BUTTON_LEFT);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());
        ASSERT_TRUE(sounds.empty());

        // A press that leaves the button before the release changes nothing
        screen.handle_mouse_down(on_x, on_y, SDL_BUTTON_LEFT);
        screen.handle_mouse_motion(on_x, on_y + 100);
        screen.handle_mouse_up(on_x, on_y + 100, SDL_BUTTON_LEFT);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());

        // The places that used to do something in the remake (the map name box, the description box, the thumbs, the Drop button) are not buttons of the original
        const int32_t places[][2] = {{100, 325}, {100, 390}, {550, 105}, {550, 155}, {550, 205}, {590, 200}, {38, 380}};
        for (const auto& pt : places) {
            screen.handle_mouse_motion(pt[0], pt[1]);
            screen.handle_mouse_down(pt[0], pt[1], SDL_BUTTON_LEFT);
            screen.handle_mouse_up(pt[0], pt[1], SDL_BUTTON_LEFT);
        }
        ASSERT_EQ(screen.get_selected_index(), start_index);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());
        ASSERT_EQ(starts, 0);
        ASSERT_EQ(leaves, 0);
        ASSERT_TRUE(sounds.empty());
    } TEST_END();

    TEST_CASE("8.7 Setup Screen: Every Label, Portrait And Thumb Appears With The Refresh, 500 ms After The Screen Was Created (FUN_010133ef's first run)") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");
        ASSERT_FALSE(screen.refreshed());
        screen.update(0.499f);
        ASSERT_FALSE(screen.refreshed());
        screen.update(0.001f);
        ASSERT_TRUE(screen.refreshed());
        screen.enter();                                                             // a screen that is created again starts empty again
        ASSERT_FALSE(screen.refreshed());
        screen.update(0.5f);
        ASSERT_TRUE(screen.refreshed());
    } TEST_END();

    TEST_CASE("8.8 Setup Screen: The Map List Is Searched, Not Built In - Every .lvl / .LVL File Of The Folder (Spaces, Mixed Case), Nothing Else, In Byte Order; The Labels Come From The File's Header") {
        namespace fs = std::filesystem;
        struct TempDir {
            fs::path path;
            ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
        } dir;
        dir.path = temp_path_of_this_run("ants_map_list_test");
        std::error_code ec;
        fs::create_directories(dir.path / "inner", ec);
        const fs::path source = fs::path("Original-Ants/Maps/TINY.LVL");
        ASSERT_TRUE(fs::exists(source));
        for (const char* name : {"POPcOrN.lvl", "OCEAN.LVL", "Bombz Away.lvl", "zeta.Lvl", "Mid.LVL"}) {
            fs::copy_file(source, dir.path / name, fs::copy_options::overwrite_existing, ec);
            ASSERT_FALSE(static_cast<bool>(ec));
        }
        fs::copy_file(source, dir.path / "inner" / "Hidden.LVL", fs::copy_options::overwrite_existing, ec);     // a folder inside is not searched
        std::ofstream(dir.path / "readme.txt") << "not a map";
        std::ofstream(dir.path / "bad.lvl.bak") << "not a map either";

        MapSelectScreen screen;
        screen.init(dir.path.string());
        const auto& maps = screen.get_maps();
        const char* expected[] = {"Bombz Away.lvl", "Mid.LVL", "OCEAN.LVL", "POPcOrN.lvl", "zeta.Lvl"};        // the byte order of the names: capitals before small letters
        ASSERT_EQ(maps.size(), 5u);
        for (size_t i = 0; i < maps.size(); ++i) ASSERT_EQ(maps[i].filename, expected[i]);
        ASSERT_EQ(maps[0].display_name, "Bombz Away");
        ASSERT_EQ(maps[3].display_name, "POPcOrN");
        for (const auto& m : maps) {                                                  // everything shown about a map is the file's own header (these are copies of TINY)
            ASSERT_EQ(m.description, "Tiny map with no PowerUps");
            ASSERT_EQ(m.minutes, 6u);
            ASSERT_EQ(m.width, 31u);
            ASSERT_EQ(m.height, 31u);
            ASSERT_TRUE(fs::exists(m.full_path));
        }
        // Down steps through the list in that order and the start gets the chosen file
        std::string started;
        screen.set_on_start([&](const std::string& path) { started = path; });
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(started.find("POPcOrN.lvl") != std::string::npos);
        // a folder without maps has an empty list, and nothing starts
        MapSelectScreen empty;
        empty.init((dir.path / "inner" / "nothing").string());
        ASSERT_TRUE(empty.get_maps().empty());
        started.clear();
        empty.set_on_start([&](const std::string& path) { started = path; });
        empty.handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(started.empty());
        ASSERT_TRUE(empty.get_selected_map_path().empty());
    } TEST_END();

    TEST_CASE("8.9 Setup Screen: The List Highlights TREASURE.LVL When The Folder Holds It (Any Case), Else The First Entry; Only The Highlight Differs From The Original (The List, Its Order, Up / Down With The Wrap, START); A Room's Leader And Guest Show The Room's Map") {
        namespace fs = std::filesystem;
        struct TempDir {
            fs::path path;
            ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
        } dir;
        dir.path = temp_path_of_this_run("ants_map_default_test");
        std::error_code ec;
        fs::create_directories(dir.path, ec);
        const fs::path source = fs::path("Original-Ants/Maps/TINY.LVL");
        ASSERT_TRUE(fs::exists(source));
        const auto copies = [&](const std::string& folder_name, const std::vector<std::string>& names) {
            const fs::path folder = dir.path / folder_name;
            fs::create_directories(folder, ec);
            for (const std::string& name : names) fs::copy_file(source, folder / name, fs::copy_options::overwrite_existing, ec);
            return folder.string();
        };

        // 1. The six maps of the original: the list is the original's (byte order of the names, GAUNTLET first, TREASURE last) and TREASURE.LVL is the highlighted entry
        {
            MapSelectScreen screen;
            screen.init("Original-Ants/Maps");
            const auto& maps = screen.get_maps();
            const char* order[] = {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"};
            ASSERT_EQ(maps.size(), 6u);
            for (size_t i = 0; i < maps.size(); ++i) ASSERT_EQ(maps[i].filename, order[i]);
            ASSERT_EQ(std::string(MapSelectScreen::DEFAULT_MAP_FILE), "TREASURE.LVL");
            ASSERT_EQ(screen.get_selected_index(), 5);
            ASSERT_EQ(maps[static_cast<size_t>(screen.get_selected_index())].filename, "TREASURE.LVL");
            ASSERT_TRUE(screen.get_selected_map_path().find("TREASURE.LVL") != std::string::npos);
            // a player who only presses START plays it (a local screen: Enter starts)
            std::string started;
            screen.set_on_start([&](const std::string& path) { started = path; });
            screen.handle_key_down(SDLK_RETURN);
            ASSERT_TRUE(started.find("TREASURE.LVL") != std::string::npos);
            // everything else is the original's: Down from the last entry is the first (the wrap), Up goes back, nothing else moves the list
            screen.handle_key_down(SDLK_DOWN);
            ASSERT_EQ(screen.get_selected_index(), 0);
            screen.handle_key_down(SDLK_UP);
            ASSERT_EQ(screen.get_selected_index(), 5);
            screen.handle_key_down(SDLK_UP);
            ASSERT_EQ(screen.get_selected_index(), 4);                                // TINY.LVL: the entry in front of it
        }
        // 2. A folder without TREASURE.LVL: the first entry, as the original does (a name that only starts like it is not it)
        {
            MapSelectScreen screen;
            screen.init(copies("without", {"POPCORN.lvl", "OCEAN.LVL", "Mid.LVL", "TREASURE2.LVL", "TREASURE ISLAND.LVL"}));
            ASSERT_EQ(screen.get_maps().size(), 5u);
            ASSERT_EQ(screen.get_selected_index(), 0);
            ASSERT_EQ(screen.get_maps()[0].filename, "Mid.LVL");
        }
        // 3. A folder with its own spelling of the name: found whatever the case, highlighted where the list puts it (capitals sort before small letters: TREASURE2.LVL is in front of Treasure.lvl)
        {
            MapSelectScreen screen;
            screen.init(copies("mixed", {"ZULU.LVL", "Treasure.lvl", "TREASURE2.LVL", "ALPHA.LVL"}));
            ASSERT_EQ(screen.get_maps().size(), 4u);
            ASSERT_EQ(screen.get_maps()[2].filename, "Treasure.lvl");
            ASSERT_EQ(screen.get_selected_index(), 2);
            MapSelectScreen lower;
            lower.init(copies("lower", {"alpha.lvl", "treasure.lvl"}));
            ASSERT_EQ(lower.get_maps().size(), 2u);
            ASSERT_EQ(lower.get_selected_index(), 1);
        }
        // 4. No maps at all: nothing is highlighted and nothing starts (as before)
        {
            MapSelectScreen screen;
            screen.init((dir.path / "nothing").string());
            ASSERT_TRUE(screen.get_maps().empty());
            ASSERT_TRUE(screen.get_selected_map_path().empty());
        }
        // 5. A guest and the leader of a server's room show the room's map, never the highlight of the list: the application follows the room (follow_host_choice), and the leader's
        // Up / Down change nothing
        {
            MapSelectScreen guest;
            guest.init("Original-Ants/Maps");
            MapSelectScreen::RoomView view;
            view.networked = true;
            view.is_host = false;
            view.my_seat = 1;
            view.seats[0] = {true, "Ana", MapSelectScreen::Thumb::Good};
            view.seats[1] = {true, "Ben", MapSelectScreen::Thumb::Good};
            view.map_file = "SMALL.LVL";
            guest.set_room(view);
            ASSERT_TRUE(guest.is_guest());
            ASSERT_TRUE(guest.follow_host_choice("SMALL.LVL", false));
            ASSERT_EQ(guest.get_maps()[static_cast<size_t>(guest.get_selected_index())].filename, "SMALL.LVL");
            MapSelectScreen leader;
            leader.init("Original-Ants/Maps");
            view.leader = true;
            view.my_seat = 0;
            leader.set_room(view);
            ASSERT_TRUE(leader.leads_server_room());
            ASSERT_TRUE(leader.follow_host_choice("SMALL.LVL", false));
            const int32_t room_index = leader.get_selected_index();
            ASSERT_EQ(leader.get_maps()[static_cast<size_t>(room_index)].filename, "SMALL.LVL");
            leader.handle_key_down(SDLK_DOWN);
            leader.handle_key_down(SDLK_UP);
            leader.handle_key_down(SDLK_UP);
            ASSERT_EQ(leader.get_selected_index(), room_index);
            ASSERT_FALSE(leader.follow_host_choice("NOSUCH.LVL", false));              // a room on a map that this machine does not have: the highlight is left where it was
            ASSERT_EQ(leader.get_selected_index(), room_index);
        }
    } TEST_END();

    TEST_CASE("8.4 INTRO.MID Lifecycle & In-Game Music") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true; // Test map select lifecycle
        ASSERT_TRUE(app.init(cfg));

        // Starts in MapSelect with INTRO.MID active
        ASSERT_EQ(app.state(), AppState::MapSelect);

        // When starting game, state transitions to Playing and in-game MIDI track starts
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.midi_player().is_playing()); // In-game music shuffle is playing!

        // When returning to map select, MIDI reloads INTRO.MID and resumes looping
        app.return_to_map_select();
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.midi_player().is_playing());
    } TEST_END();

    TEST_CASE("8.6 The Tile Grid Overlay Is A Command Line Option Only: No Key Toggles It (the original has no such key)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        // Default: Tile grid overlay is OFF
        ASSERT_FALSE(app.is_tile_grid_visible());

        // Ctrl+T, Cmd+T and F3 do nothing
        SDL_KeyboardEvent key{};
        key.type = SDL_KEYDOWN;
        key.keysym.sym = SDLK_t;
        key.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key);
        key.keysym.mod = KMOD_LGUI;
        app.handle_key_down(key);
        key.keysym.sym = SDLK_F3;
        key.keysym.mod = 0;
        app.handle_key_down(key);
        ASSERT_FALSE(app.is_tile_grid_visible());

        // --show-grid turns it on
        Application with_grid;
        ApplicationConfig grid_cfg = cfg;
        grid_cfg.show_tile_grid = true;
        ASSERT_TRUE(with_grid.init(grid_cfg));
        ASSERT_TRUE(with_grid.is_tile_grid_visible());
    } TEST_END();

    TEST_CASE("8.7 Anthill Selection and Hatch Action") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& sim = app.sim();
        auto& hud = app.hud();

        // 1. Select Home Base (Player 0)
        hud.select_base(0);
        ASSERT_EQ(hud.get_selected_base_team_id(), 0);
        ASSERT_EQ(hud.get_selected_ant_id(), 0u);

        // 2. Set score for Player 0 to 500
        sim.set_player_score(0, 500);
        uint32_t initial_eggs = sim.get_player_eggs(0);
        size_t initial_ants = sim.get_world_state().ants.size();

        // 3. Click Hatch button at (500, 165)
        ViewportCamera cam;
        bool down_res = hud.handle_mouse_down(500, 165, SDL_BUTTON_LEFT, sim, cam);
        ASSERT_TRUE(down_res);
        hud.handle_mouse_up(500, 165, SDL_BUTTON_LEFT, sim, cam);

        // The click costs 200 points and one egg at once; the egg incubates for 8000 ms without any ant (HATCHTSK)
        ASSERT_EQ(sim.get_player_eggs(0), initial_eggs - 1);
        ASSERT_EQ(sim.get_player_score(0), 300);
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        ASSERT_EQ(sim.get_world_state().ants.size(), initial_ants);
        for (int t = 0; t < 165; ++t) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 0u);
        ASSERT_EQ(sim.get_world_state().ants.size(), initial_ants + 1);

        // 4. Click the Stop pedestal at (610, 195): it flashes, the mouse input is locked for 250 ms (5 ticks), then the selection is dropped
        bool stop_down = hud.handle_mouse_down(610, 195, SDL_BUTTON_LEFT, sim, cam);
        ASSERT_TRUE(stop_down);
        hud.handle_mouse_up(610, 195, SDL_BUTTON_LEFT, sim, cam);
        ASSERT_EQ(hud.get_selected_base_team_id(), 0);
        hud.update(sim.get_world_state(), 4);
        ASSERT_EQ(hud.get_selected_base_team_id(), 0);
        hud.update(sim.get_world_state(), 1);
        ASSERT_EQ(hud.get_selected_base_team_id(), -1);
    } TEST_END();

    TEST_CASE("8.8 Team Switching and the Start View (Ants.exe 0x100e458 - 0x100e4b1: Just Far Enough To Show The Square Around The Hill's Anchor Tile)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        cfg.default_map_path = "Original-Ants/Maps/GAUNTLET.LVL";                  // (a run without --map plays TREASURE.LVL now, see 8.9: the numbers below are GAUNTLET's, so it is named)
        ASSERT_TRUE(app.init(cfg));

        // Default starts as Player 0 (Green)
        ASSERT_EQ(app.local_player_id(), 0);

        // The start view is the original's (HUD constructor 0x100e458 - 0x100e4b1): the fresh view scrolls just far enough to show the square (-160, +192) around the
        // anchor tile of Team 0's hill, it does not centre it. On GAUNTLET (the first map of the original's list) green's anchor is (30, 8), so (726, 24)
        // (a centred view would be (772, 69)).
        const auto* base0 = app.sim().grid().find_anthill(0);
        ASSERT_TRUE(base0 != nullptr);
        ASSERT_EQ(base0->x + 1, 30);
        ASSERT_EQ(base0->y + 1, 8);
        const auto& cam = app.renderer().camera();
        ASSERT_EQ(cam.world_x, 726);
        ASSERT_EQ(cam.world_y, 24);
        int32_t expected_x = 726;
        int32_t expected_y = 24;

        // Ctrl+2 does nothing: the original has no key that switches the controlled team
        SDL_KeyboardEvent key_event{};
        key_event.type = SDL_KEYDOWN;
        key_event.keysym.sym = SDLK_2;
        key_event.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key_event);
        ASSERT_EQ(app.local_player_id(), 0);

        // Switch to Team 1 (Red) through the setter (what the command line's -pnum= does)
        app.set_local_player(1);
        ASSERT_EQ(app.local_player_id(), 1);
        const auto* base1 = app.sim().grid().find_anthill(1);
        ASSERT_TRUE(base1 != nullptr);
        ASSERT_EQ(base1->x + 1, 7);                    // red's anchor tile on GAUNTLET is (7, 30): the square (80, 816) - (432, 1168) needs the view 728 px down, none right
        ASSERT_EQ(base1->y + 1, 30);
        expected_x = 0;
        expected_y = 728;
        ASSERT_EQ(cam.world_x, expected_x);
        ASSERT_EQ(cam.world_y, expected_y);

        // Switch to Team 2 via direct setter
        app.set_local_player(2);
        ASSERT_EQ(app.local_player_id(), 2);
        const auto* base2 = app.sim().grid().find_anthill(2);
        ASSERT_TRUE(base2 != nullptr);
        ASSERT_EQ(base2->x + 1, 20);                   // blue's anchor tile is (20, 11): the square (496, 208) - (848, 560): 406 right, 120 down
        ASSERT_EQ(base2->y + 1, 11);
        expected_x = 406;
        expected_y = 120;
        ASSERT_EQ(cam.world_x, expected_x);
        ASSERT_EQ(cam.world_y, expected_y);

        // Ctrl+Tab does nothing either
        key_event.keysym.sym = SDLK_TAB;
        key_event.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key_event);
        ASSERT_EQ(app.local_player_id(), 2);
    } TEST_END();
}

// ============================================================================
// Suite 9: Gameplay Mechanics, Food Schedules, Hangman Pathing & Options Dialog
// ============================================================================
void run_suite_9_gameplay_mechanics_and_options() {
    std::cout << "\n=======================================================\n"
              << " [SUITE] Suite 9: Gameplay Mechanics, Hangman & Options\n"
              << "=======================================================\n";

    TEST_CASE("9.1 Food Objects of TREASURE.LVL: Harvest Takes One Unit Worth The Object's Points") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/TREASURE.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& sim_engine = app.sim();
        auto& grid = sim_engine.grid_mut();

        // Every Block 2 entry of the level is a food object that shows its first stage at its anchor
        const auto& objs = grid.food_objects();
        ASSERT_TRUE(!objs.empty());
        for (const FoodObject& o : objs) {
            ASSERT_EQ(o.remaining, o.units);
            ASSERT_TRUE(o.units > 0);
        }

        // A pile with a free tile close to it (not a cell of any pile): a worker put there harvests it
        int32_t obj = -1;
        TileCoord start{-1, -1};
        for (size_t i = 0; i < objs.size() && obj < 0; ++i) {
            const TileCoord anchor{objs[i].col, objs[i].row};
            int32_t best = 99;
            for (int32_t dy = -3; dy <= 3; ++dy) {
                for (int32_t dx = -3; dx <= 3; ++dx) {
                    const TileCoord t{anchor.x + dx, anchor.y + dy};
                    if (!grid.in_bounds(t) || grid.food_object_at_cell(t) >= 0) continue;
                    if (!grid.get_cell(t).is_passable() || grid.get_cell(t).has_food()) continue;
                    const int32_t d = std::max(std::abs(dx), std::abs(dy));
                    if (d < best) { best = d; start = t; }
                }
            }
            if (best < 99) obj = static_cast<int32_t>(i);
        }
        ASSERT_TRUE(obj >= 0);
        const FoodObject& pile = grid.food_objects()[static_cast<size_t>(obj)];
        const TileCoord anchor{pile.col, pile.row};
        const uint16_t units = pile.units;
        const uint16_t value = pile.value;
        ASSERT_TRUE(grid.food_object_at_cell(anchor) >= 0);

        // Order the worker onto the pile; it walks to the pile, plays the grab clip and carries the object's points
        uint32_t ant_id = sim_engine.spawn_unit(0, AntType::Worker, start);
        const auto& ant = sim_engine.get_unit(ant_id);
        ASSERT_FALSE(ant.is_holding());
        sim_engine.clear_news_events();
        sim_engine.issue_move_order(ant_id, anchor);
        ASSERT_TRUE(wait_ms(sim_engine, 20000, [&]() { return ant.is_holding(); }) >= 0);

        ASSERT_EQ(grid.food_objects()[static_cast<size_t>(obj)].remaining, units - 1);
        ASSERT_EQ(ant.carried_points, static_cast<uint32_t>(value));
        ASSERT_EQ(ant.harvest_origin, anchor);
        ASSERT_TRUE(sim_engine.has_news_event(0, 60));   // "Got Food!"
    } TEST_END();

    TEST_CASE("9.2 Anthill Hangman Pathing, Lifecycle & Sound 55") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/SMALL.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& sim_engine = app.sim();
        const auto* base = sim_engine.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x;
        int32_t by = base->y;

        // Spawn damaged worker carrying food on the waiting tile (bx - 1, by + 3)
        uint32_t ant_id = sim_engine.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        auto& ant = sim_engine.get_unit(ant_id);
        ant.hp = 4; // Damaged
        ant.pick_up_food(1, 25);
        ant.harvest_origin = TileCoord{15, 15};

        // An order onto the hill is the enter order (order 2): the ant walks to the entrance tile (bx+1, by+1)
        sim_engine.issue_move_order(ant_id, TileCoord{bx + 1, by + 1});
        ASSERT_EQ(ant.orig_order, AntUnit::kOrderHome);

        // Tick movement until the enter clip starts on the entrance tile
        for (int i = 0; i < 90 && ant.state != UnitState::EnteringBase; ++i) {
            sim_engine.tick();
        }
        ASSERT_EQ(static_cast<uint16_t>(ant.state), static_cast<uint16_t>(UnitState::EnteringBase));
        ASSERT_EQ(ant.pos, (TileCoord{bx + 1, by + 1}));

        // Food and health change only when the clip is cleaned up: still carrying and wounded inside the clip
        ASSERT_TRUE(ant.is_holding());
        ASSERT_EQ(ant.hp, 4u);

        // The clip ends: the food scores, the ant is healed to full health and sets out for the food source
        for (int i = 0; i < 200 && ant.state == UnitState::EnteringBase; ++i) {
            sim_engine.tick();
        }
        ASSERT_FALSE(ant.is_holding());
        ASSERT_EQ(ant.hp, ant.max_hp);
        ASSERT_EQ(sim_engine.get_player_score(0), 25);
    } TEST_END();

    TEST_CASE("9.3 Friendly Ant Pathing Around Stationary Ant (No Pushing)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/SMALL.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& sim_engine = app.sim();

        // Spawn stationary ant at (10, 8)
        uint32_t stationary_id = sim_engine.spawn_unit(0, AntType::Combat, TileCoord{10, 8});
        auto& stat_ant = sim_engine.get_unit(stationary_id);
        stat_ant.state = UnitState::Idle;

        // Spawn moving ant at (9, 8) heading to (11, 8)
        uint32_t mover_id = sim_engine.spawn_unit(0, AntType::Worker, TileCoord{9, 8});
        sim_engine.issue_move_order(mover_id, TileCoord{11, 8});

        // Tick simulation
        for (int i = 0; i < 40; ++i) {
            sim_engine.tick();
        }

        // Stationary ant stays anchored at (10, 8) and is NOT pushed
        const auto& mover = sim_engine.get_unit(mover_id);
        ASSERT_EQ(stat_ant.pos.x, 10);
        ASSERT_EQ(stat_ant.pos.y, 8);
        ASSERT_EQ(mover.pos.x, 11);
        ASSERT_EQ(mover.pos.y, 8);
    } TEST_END();

    TEST_CASE("9.4 Options Menu Navigation, Sliders and Return Button") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& hud = app.hud();
        auto& sim_engine = app.sim();
        ViewportCamera cam;

        // Open options dialog
        ASSERT_FALSE(hud.is_options_open());
        hud.open_options();
        ASSERT_TRUE(hud.is_options_open());

        // Drag sound volume slider (x=280, y=185)
        hud.handle_mouse_down(280, 185, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_motion(320, 185, sim_engine, cam);
        hud.handle_mouse_up(320, 185, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_EQ(hud.get_sound_volume(), 58);                       // the thumb stands for (320 - 211) 100 / 185
        ASSERT_EQ(app.audio_mixer().sound_volume(), 58);

        // Drag music volume slider (x=280, y=223)
        hud.handle_mouse_down(280, 223, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_motion(340, 223, sim_engine, cam);
        hud.handle_mouse_up(340, 223, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_EQ(hud.get_music_volume(), 69);                       // (340 - 211) 100 / 185
        ASSERT_NEAR(app.midi_player().get_volume(), 0.69f, 0.001f);

        // Click Return to Game button at (355, 427, bounds 345..455, 423..455)
        hud.handle_mouse_down(380, 435, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_up(380, 435, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_FALSE(hud.is_options_open());
    } TEST_END();

    TEST_CASE("9.5 Frame Rate Counter and Tile Grid Visibility") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        // FPS getter returns non-negative
        ASSERT_TRUE(app.get_current_fps() >= 0.0f);

        // The counter is the real rate, not the rate of a clamped frame: a window drawn four times a second reads 4 (every rate under ten frames a second read "10 FPS" when
        // the frame was cut at 100 ms for the local simulation), one drawn once a second reads 1; a frame counts for at most a second. (A headless application stops after ten
        // frames unless it is taking a screenshot far away: this one does.)
        Application timed;
        ApplicationConfig tcfg = cfg;
        tcfg.screenshot_path = "fps_counter_test.png";
        tcfg.screenshot_frames = 1000000;
        ASSERT_TRUE(timed.init(tcfg));
        for (int i = 0; i < 80; ++i) timed.run_frame_with_delta(0.25f);
        ASSERT_TRUE(timed.get_current_fps() > 3.5f && timed.get_current_fps() < 4.5f);
        for (int i = 0; i < 80; ++i) timed.run_frame_with_delta(1.0f);
        ASSERT_TRUE(timed.get_current_fps() > 0.9f && timed.get_current_fps() < 1.3f);
        for (int i = 0; i < 80; ++i) timed.run_frame_with_delta(5.0f);                   // (a frame of 5 s is counted as the second that the network gets of it)
        ASSERT_TRUE(timed.get_current_fps() > 0.9f && timed.get_current_fps() < 1.3f);
        for (int i = 0; i < 200; ++i) timed.run_frame_with_delta(1.0f / 60.0f);
        ASSERT_TRUE(timed.get_current_fps() > 58.0f && timed.get_current_fps() < 62.0f);

        // The tile grid is off unless --show-grid asks for it (test 8.6): no runtime toggle exists
        ASSERT_FALSE(app.is_tile_grid_visible());
    } TEST_END();

    TEST_CASE("9.6 Ant Type Movement and Ready Voice Confirmations") {
        // Verify authentic voice sound ID mapping across all 6 ant types
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Worker, 0), 17); // gantgo.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Worker, 1), 15); // gantcommand.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Swimmer, 0), 33); // brdggo.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Fire, 0), 23); // firego.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Combat, 0), 29); // combgo2.wav (cue index 20 of the original)
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Combat, 1), 28); // combgo1.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Bomber, 0), 37); // bombgo.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Thief, 0), 19); // theifgo.wav

        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Worker, 0), 13); // gantorders.wav for rand() % 3 == 0 ...
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Worker, 1), 14); // ... gantrdy.wav for the two other values
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Worker, 2), 14);
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Worker, 3), 13);
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Combat, 0), 27); // combrdy1.wav
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Combat, 1), 26); // combrdy2.wav

        ASSERT_EQ(ants::sim::get_attack_voice_sound(ants::sim::AntType::Worker, 0), 16); // gantattack.wav
        ASSERT_EQ(ants::sim::get_attack_voice_sound(ants::sim::AntType::Combat, 0), 59); // combat1.wav
        ASSERT_EQ(ants::sim::get_attack_voice_sound(ants::sim::AntType::Combat, 1), 60); // combat2.wav
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Worker), ants::sim::NoVoice);   // no special voice
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Combat), ants::sim::NoVoice);
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Bomber), 39); // bombdo.wav
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Swimmer), 35); // brdgdo.wav
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Fire), 25); // firedo.wav
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Thief), 21); // theifdo.wav

        // Test HUD order triggering sfx callback
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/TREASURE.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& hud = app.hud();
        auto& sim_engine = app.sim();

        uint32_t last_sfx = 0;
        uint32_t last_voice = 0;                                               // the last sound that is not the pedestal click (89)
        hud.set_on_play_sfx([&](uint32_t sid) {
            last_sfx = sid;
            if (sid != SoundID::NavButtonClick) last_voice = sid;
        });

        // Select all friendly ants
        hud.select_all_friendly(sim_engine.get_world_state());
        ASSERT_TRUE(last_sfx == 14 || last_sfx == 13); // gantrdy or gantorders

        // Issue move order via right click on playfield
        ViewportCamera cam;
        hud.handle_mouse_down(100, 100, SDL_BUTTON_RIGHT, sim_engine, cam);
        hud.handle_mouse_up(100, 100, SDL_BUTTON_RIGHT, sim_engine, cam);     // the order is given at the release
        ASSERT_TRUE(last_voice == 17 || last_voice == 15); // gantgo or gantcommand
        ASSERT_EQ(last_sfx, SoundID::NavButtonClick);                          // the flashing move pedestal clicks after the voice (BTNPUSH)
    } TEST_END();

    TEST_CASE("9.6b The Click Of A Pressed Button Is Cut At Its Release (the raised clip replaces the pressed one: StopTracked); A Cue Is Not") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/TREASURE.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        const size_t before = app.audio_mixer().active_channel_count();         // (the start voice of the match may be playing: nothing mixes without a device)
        SDL_MouseButtonEvent down{};
        down.type = SDL_MOUSEBUTTONDOWN;
        down.button = SDL_BUTTON_LEFT;
        down.x = 490;                                                            // the Help button of the top bar
        down.y = 15;
        app.handle_mouse_button(down);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), before + 1);        // buttonclick.wav (277 ms) of the pressed picture
        SDL_MouseButtonEvent up = down;
        up.type = SDL_MOUSEBUTTONUP;
        app.handle_mouse_button(up);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), before);            // released after a moment: the click stops with the pressed picture
        // the other UI sounds and the cues are not tied to a release
        app.audio_mixer().play_sfx(SoundID::AntStop, 1.0f, 255);
        app.handle_mouse_button(up);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), before + 1);
    } TEST_END();

    // ---- the music of the original: one MCI sequencer device (0x100e627 .. 0x100e8cc). The intro plays once; every piece that ends is followed by a random in-game piece (never the one
    // before), also on the setup screen; a match start, the activation of the program and the release of the music slider start a random piece; the deactivation of the program and the
    // end of a match close the device, and a closed device is not started by anything else ---------------------------------------------------------------------------------------
    TEST_CASE("9.8 Music: The Intro Plays Once, Then Random In-Game Pieces Follow, Never The Same Twice In A Row, Also On The Setup Screen (MM_MCINOTIFY 0x100e8cc)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        auto ends_with = [](const std::string& s, const char* tail) { const std::string t(tail); return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0; };
        auto& mixer = app.audio_mixer();
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(ends_with(mixer.music_filepath(), "INTRO.mp3"));
        ASSERT_FALSE(mixer.music_loops());                                       // `play AntsMidi from 0 notify`: once
        std::string previous;
        for (int i = 0; i < 40; ++i) {
            mixer.stop_music();                                                  // the piece ends (nothing advances a stream without an audio device)
            app.midi_player().stop();
            app.update_music(0.016f);
            ASSERT_TRUE(mixer.is_music_playing());
            const std::string now = mixer.music_filepath();
            ASSERT_TRUE(ends_with(now, "ANTS2A.mp3") || ends_with(now, "ANTS2B.mp3") || ends_with(now, "ANTSFUN3.mp3"));
            ASSERT_FALSE(mixer.music_loops());
            if (!previous.empty()) ASSERT_TRUE(now != previous);
            previous = now;
        }
        ASSERT_EQ(static_cast<int>(app.state()), static_cast<int>(AppState::MapSelect));   // the setup screen all the time
    } TEST_END();

    TEST_CASE("9.9 Music: Deactivating The Program Closes It, Activating It Starts A New Random Piece; A Closed Music Is Not Started By The Activation (0x100e875 .. 0x100e8bc)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        auto& mixer = app.audio_mixer();
        ASSERT_TRUE(mixer.is_music_playing());
        app.set_app_active(false);                                                // WM_ACTIVATEAPP(false): `close AntsMidi`
        ASSERT_FALSE(mixer.is_music_playing());
        app.update_music(0.016f);
        ASSERT_FALSE(mixer.is_music_playing());                                   // the closed device does not play on
        app.set_app_active(true);                                                 // a NEW random piece, not the intro again
        ASSERT_TRUE(mixer.is_music_playing());
        const std::string piece = mixer.music_filepath();
        ASSERT_TRUE(piece.find("INTRO") == std::string::npos);
        // a second deactivation / activation pair: again a new piece
        app.set_app_active(false);
        app.set_app_active(false);                                                // (a repeated event changes nothing)
        ASSERT_FALSE(mixer.is_music_playing());
        app.set_app_active(true);
        app.set_app_active(true);
        ASSERT_TRUE(mixer.is_music_playing());
    } TEST_END();

    TEST_CASE("9.9b Music With --audio-focus (Several Games On One Machine): A Window Without The Focus HOLDS Its Music, The Focus Continues The Same Piece; A Piece That Starts Meanwhile Starts Held") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.audio_follows_focus = true;
        ASSERT_TRUE(app.init(cfg));
        auto& mixer = app.audio_mixer();
        ASSERT_TRUE(mixer.is_music_playing());
        const std::string intro = mixer.music_filepath();
        ASSERT_TRUE(intro.find("INTRO") != std::string::npos);
        app.set_app_active(false);                                                // the focus goes to another game: its music is paused, not closed
        ASSERT_FALSE(mixer.is_music_playing());
        for (int i = 0; i < 20; ++i) app.update_music(0.016f);                    // a held piece has not ended: no new piece starts
        ASSERT_TRUE(mixer.music_filepath() == intro);
        app.set_app_active(true);                                                 // the focus is back: the SAME piece goes on (the original starts a new random one)
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath() == intro);
        // switching back and forth any number of times never changes the piece
        for (int i = 0; i < 10; ++i) {
            app.set_app_active(false);
            ASSERT_FALSE(mixer.is_music_playing());
            app.set_app_active(true);
            ASSERT_TRUE(mixer.is_music_playing());
            ASSERT_TRUE(mixer.music_filepath() == intro);
        }
        // a match that starts while the window is behind the others: its piece is there, but silent until the window gets the focus
        app.set_app_active(false);
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/TREASURE.LVL"));
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") == std::string::npos);
        const std::string piece = mixer.music_filepath();
        ASSERT_FALSE(mixer.is_music_playing());
        app.update_music(0.016f);
        ASSERT_TRUE(mixer.music_filepath() == piece);
        app.set_app_active(true);
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath() == piece);
        // the end of the piece still starts the next one (with the focus)
        mixer.stop_music();
        app.midi_player().stop();
        app.update_music(0.016f);
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath() != piece);
        // the setup screen's intro, started while the window is behind the others, is held too, and the focus plays it
        app.set_app_active(false);
        app.return_to_map_select();
        ASSERT_EQ(static_cast<int>(app.state()), static_cast<int>(AppState::MapSelect));
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);
        ASSERT_FALSE(mixer.is_music_playing());
        app.set_app_active(true);
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);
    } TEST_END();

    TEST_CASE("9.10 Music: The Release Of The Music Slider Starts A New Random Piece (0x100e714, 0x100e795); The Sound Slider Does Not") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        auto& mixer = app.audio_mixer();
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);
        ViewportCamera cam;
        app.hud().open_options();
        app.hud().handle_mouse_down(300, 185, 1, app.sim(), cam);                 // the Sound slider
        app.hud().handle_mouse_up(300, 185, 1, app.sim(), cam);
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);   // still the intro
        app.hud().handle_mouse_down(300, 222, 1, app.sim(), cam);                 // the Music slider
        app.hud().handle_mouse_motion(330, 222, app.sim(), cam);
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);   // dragging changes nothing
        app.hud().handle_mouse_up(330, 222, 1, app.sim(), cam);
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") == std::string::npos);   // the running piece was closed and a random in-game piece started
        ASSERT_EQ(app.hud().get_music_volume(), 64);                              // (330 - 211) 100 / 185
        ASSERT_NEAR(mixer.get_music_volume(), 0.64f, 0.001f);
    } TEST_END();

    TEST_CASE("9.11 Music: A Match Start Starts A Random Piece; The End Of The Match Closes The Music At Once And Nothing Starts It Again (0x1022714)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        auto& mixer = app.audio_mixer();
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);   // the setup screen plays the intro
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/TREASURE.LVL"));           // a match start: a random in-game piece replaces it
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") == std::string::npos);
        app.sim().init_test_world(60, 60, 5, 100);                                // a match of 100 ms
        for (int i = 0; i < 100 + 12 && !app.sim().is_match_over(); ++i) app.update_simulation(0.05f);      // (the first 100 steps are the "Get ready" dialog, in which the simulation waits)
        ASSERT_TRUE(app.sim().is_match_over());
        ASSERT_FALSE(mixer.is_music_playing());                                   // closed at once, not faded out over a second
        app.update_music(0.016f);
        ASSERT_FALSE(mixer.is_music_playing());                                   // nothing restarts it
        app.set_app_active(false);
        app.set_app_active(true);
        ASSERT_FALSE(mixer.is_music_playing());                                   // nor does the activation of the program: the device was already closed
    } TEST_END();

    TEST_CASE("9.12 Options: Every Setting Is Written When Its Callback Runs And The Next Start Reads It (FUN_0100c20c, FUN_0100a2c9)") {
        const std::filesystem::path dir = temp_path_of_this_run("ants_test_settings");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const std::string file = (dir / "settings.ini").string();
        ViewportCamera cam;
        {
            Application app;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.settings_path = file;
            ASSERT_TRUE(app.init(cfg));
            auto& hud = app.hud();
            ASSERT_EQ(hud.get_sound_volume(), 100);                   // nothing stored yet: the defaults
            ASSERT_EQ(hud.get_music_volume(), 65);
            ASSERT_EQ(hud.get_scroll_speed(), 50);
            hud.open_options();
            auto drag = [&](int32_t y, int32_t x_to) {
                hud.handle_mouse_down(300, y, SDL_BUTTON_LEFT, app.sim(), cam);
                hud.handle_mouse_motion(x_to, y, app.sim(), cam);
                hud.handle_mouse_up(x_to, y, SDL_BUTTON_LEFT, app.sim(), cam);
            };
            drag(185, 260);                                           // Sound Volume: (260 - 211) 100 / 185 = 26
            drag(222, 380);                                           // Music Volume: (380 - 211) 100 / 185 = 91
            drag(260, 240);                                           // Scroll Speed: (240 - 211) 100 / 185 = 15
            ASSERT_EQ(hud.get_sound_volume(), 26);
            ASSERT_EQ(hud.get_music_volume(), 91);
            ASSERT_EQ(hud.get_scroll_speed(), 15);
            ASSERT_EQ(app.audio_mixer().sound_volume(), 26);
            hud.handle_mouse_down(151, 289, SDL_BUTTON_LEFT, app.sim(), cam);            // Chat OFF
            hud.handle_mouse_up(151, 289, SDL_BUTTON_LEFT, app.sim(), cam);
            hud.handle_mouse_down(404, 289, SDL_BUTTON_LEFT, app.sim(), cam);            // Quick Help OFF
            hud.handle_mouse_up(404, 289, SDL_BUTTON_LEFT, app.sim(), cam);
            hud.handle_mouse_down(150, 410, SDL_BUTTON_LEFT, app.sim(), cam);            // the F10 field
            hud.handle_mouse_up(150, 410, SDL_BUTTON_LEFT, app.sim(), cam);
            hud.handle_text_input(" Now!");
            ASSERT_EQ(hud.get_quick_chat_key(1), "Let me be! Now!");
        }
        {
            std::ifstream in(file);
            ASSERT_TRUE(in.good());
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            ASSERT_TRUE(text.find("Sound Volume=26\n") != std::string::npos);
            ASSERT_TRUE(text.find("Music Volume=91\n") != std::string::npos);
            ASSERT_TRUE(text.find("Scroll Speed=15\n") != std::string::npos);
            ASSERT_TRUE(text.find("Participate In Chat=0\n") != std::string::npos);
            ASSERT_TRUE(text.find("Show Quick Help at Startup=0\n") != std::string::npos);
            ASSERT_TRUE(text.find("Quick Chat F10=Let me be! Now!\n") != std::string::npos);
            ASSERT_TRUE(text.find("Quick Chat F9") == std::string::npos);        // only what a callback wrote is stored
        }
        {
            Application app;                                          // the next start
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.settings_path = file;
            ASSERT_TRUE(app.init(cfg));
            auto& hud = app.hud();
            ASSERT_EQ(hud.get_sound_volume(), 26);
            ASSERT_EQ(hud.get_music_volume(), 91);
            ASSERT_EQ(hud.get_scroll_speed(), 15);
            ASSERT_FALSE(hud.is_chat_enabled());
            ASSERT_FALSE(hud.is_quick_help_enabled());
            ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it!");      // not stored: the default
            ASSERT_EQ(hud.get_quick_chat_key(1), "Let me be! Now!");
            ASSERT_EQ(app.audio_mixer().sound_volume(), 26);                     // and applied at the start
            ASSERT_NEAR(app.audio_mixer().get_music_volume(), 0.91f, 0.001f);
            ASSERT_NEAR(app.midi_player().get_volume(), 0.91f, 0.001f);
            hud.open_options();                                                  // the screen is built from the settings: pos = 211 + 185 v / 99, thumb at pos - 23
            ASSERT_EQ(hud.options_screen().slider(0).thumb_left(), 236);
            ASSERT_EQ(hud.options_screen().slider(1).thumb_left(), 358);
            ASSERT_EQ(hud.options_screen().slider(2).thumb_left(), 216);
            ASSERT_EQ(hud.options_screen().slider(0).value(), 25);               // the mapping is not its own inverse: 26 is placed where 25 is read back
            ASSERT_TRUE(hud.options_screen().chat_off().latched() && hud.options_screen().help_off().latched());
        }
        std::filesystem::remove_all(dir);
    } TEST_END();

    TEST_CASE("9.13 Options: A Headless Run Keeps Its Settings In Memory; An Invalid Stored Value Is The Default (FUN_0102950f)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.hud().options().quick_chat[2] == "Attack!");
        app.hud().open_options();
        app.hud().handle_key_down(SDLK_RETURN, app.sim(), app.renderer().camera());
        ASSERT_FALSE(app.hud().is_options_open());
        ASSERT_TRUE(ConfigStore::default_location().empty() || !std::filesystem::exists(std::filesystem::path(ConfigStore::default_location()).parent_path() / "headless_marker"));

        const std::filesystem::path dir = temp_path_of_this_run("ants_test_settings2");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const std::string file = (dir / "settings.ini").string();
        {
            std::ofstream out(file);
            out << "Sound Volume=100\nMusic Volume=abc\nScroll Speed=100\nParticipate In Chat=1\nShow Quick Help at Startup=\nQuick Chat F9=" << std::string(130, 'x') << "\n";
        }
        Application other;
        ApplicationConfig cfg2;
        cfg2.headless = true;
        cfg2.start_in_map_select = false;
        cfg2.settings_path = file;
        ASSERT_TRUE(other.init(cfg2));
        ASSERT_EQ(other.hud().get_sound_volume(), 100);      // 100 is not below the maximum 100
        ASSERT_EQ(other.hud().get_music_volume(), 65);       // not a number
        ASSERT_EQ(other.hud().get_scroll_speed(), 50);       // 100 again
        ASSERT_TRUE(other.hud().is_chat_enabled());          // 1 is not the one valid value (0) of a switch
        ASSERT_TRUE(other.hud().is_quick_help_enabled());    // empty
        ASSERT_EQ(other.hud().get_quick_chat_key(0).size(), 100u);
        std::filesystem::remove_all(dir);
    } TEST_END();

    TEST_CASE("9.14 Startup Quick Help: START! Is The Button Class, The Keys Are Enter Esc C X (FUN_010145d2, FUN_010147c2); The Stored Switch Skips It") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);
        auto show = [&]() { app.finish_loading(); };

        // the stored option is on: the loading screen leads to the quick help
        show();
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        // clicks elsewhere and the wrong keys do nothing
        app.quick_help_press(300, 200);
        app.quick_help_release(300, 200);
        for (SDL_Keycode k : {SDLK_SPACE, SDLK_m, SDLK_q, SDLK_s, SDLK_a, SDLK_TAB, SDLK_F1}) {
            app.quick_help_key(k);
            ASSERT_EQ(app.state(), AppState::QuickHelp);
        }
        // the button: captured at the press, acts at the release, leaving cancels for good
        app.quick_help_press(580, 450);
        ASSERT_TRUE(app.quick_help_start_button().pressed());
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        app.quick_help_release(580, 450);
        ASSERT_EQ(app.state(), AppState::MapSelect);

        show();
        app.quick_help_press(580, 450);
        app.quick_help_move(10, 10);
        app.quick_help_move(580, 450);
        app.quick_help_release(580, 450);
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        app.quick_help_press(580, 450);
        app.quick_help_release(300, 200);
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        // the hit test is the rectangle of the picture that shows (0x10112e9): a press must hit the up / hover picture [529, 627) x [437, 464), and every move until the
        // release must stay on the pressed picture [528, 625) x [438, 462): the click zone is the intersection [529, 625) x [438, 462) (docs 5.52)
        ASSERT_TRUE(app.quick_help_start_button().up_rect() == ButtonRect({529, 437, 98, 27}));
        ASSERT_TRUE(app.quick_help_start_button().pressed_rect() == ButtonRect({528, 438, 97, 24}));
        auto zone = [&](int32_t x, int32_t y) {
            show();
            app.quick_help_move(x, y);
            app.quick_help_press(x, y);
            app.quick_help_release(x, y);
            return app.state() == AppState::MapSelect;
        };
        ASSERT_TRUE(zone(529, 438));
        ASSERT_TRUE(zone(624, 461));
        ASSERT_TRUE(zone(580, 450));
        ASSERT_FALSE(zone(528, 450));                                          // the pressed picture only: no capture at all
        ASSERT_FALSE(zone(528, 437));
        ASSERT_FALSE(zone(529, 437));                                          // the up picture only: captured, the move that ends the input run cancels it
        ASSERT_FALSE(zone(625, 450));
        ASSERT_FALSE(zone(529, 462));
        ASSERT_FALSE(zone(626, 463));
        ASSERT_FALSE(zone(627, 450));
        show();
        app.quick_help_move(529, 437);
        app.quick_help_press(529, 437);
        ASSERT_FALSE(app.quick_help_start_button().pressed());                 // a press in a dead strip never shows the pressed picture
        // the keys Enter, Esc, C and X (either case) are the same callback
        for (SDL_Keycode k : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_ESCAPE, SDLK_c, SDLK_x}) {
            show();
            ASSERT_EQ(app.state(), AppState::QuickHelp);
            app.quick_help_key(k);
            ASSERT_EQ(app.state(), AppState::MapSelect);
        }

        // the stored switch "Show Quick Help at Startup" = 0 skips the screen
        app.hud().options().quick_help = false;
        show();
        ASSERT_EQ(app.state(), AppState::MapSelect);
    } TEST_END();

    TEST_CASE("9.14b Quick Help To Setup Screen: The Pointer Is One Global (The New START Is In Hover At Once, Nothing Is Reset); START Is Where The Original Puts It On Each Screen (0x10110a4, 0x102653f)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        // the setup screen's START: up / hover [526, 624) x [439, 466), pressed [527, 624) x [443, 467)
        ASSERT_TRUE(app.map_select().start_button().up_rect() == ButtonRect({526, 439, 98, 27}));
        ASSERT_TRUE(app.map_select().start_button().pressed_rect() == ButtonRect({527, 443, 97, 24}));
        // pointer on both buttons: the hand-over leaves the new START in the hover picture before any mouse move arrives
        app.finish_loading();
        app.quick_help_move(580, 450);
        app.quick_help_press(580, 450);
        app.quick_help_release(580, 450);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.map_select().start_button().hovered());
        ASSERT_FALSE(app.map_select().start_button().pressed());
        // pointer on the quick help START only (x = 628, 529 + ... is outside the setup START): the new START is up
        app.finish_loading();
        app.quick_help_move(625, 440);
        app.quick_help_press(625, 440);
        ASSERT_FALSE(app.quick_help_start_button().pressed());                 // (625, 440) is in the up picture only
        app.quick_help_move(530, 437);
        app.quick_help_press(530, 437);
        app.quick_help_release(530, 437);                                      // inside the up picture, outside the pressed one: nothing
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        app.quick_help_move(529, 462);
        ASSERT_TRUE(app.quick_help_start_button().hovered());
        app.quick_help_press(560, 460);
        app.quick_help_release(560, 460);                                      // y = 460 is inside both pictures: fires
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.map_select().start_button().hovered());                // (560, 460) is inside [526, 624) x [439, 466)
        // a pointer outside both buttons: up
        app.finish_loading();
        app.quick_help_move(300, 200);
        app.quick_help_key(SDLK_RETURN);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().start_button().hovered());
    } TEST_END();

    TEST_CASE("9.14c Quick Help To Setup Screen, The Original's Own Screens: The Second Click Of A Double Click On START! And A Held Enter Press The Local Game's START As Any Press Does (The Original Has No Double-Click Messages And Passes Key Repeats On); Releases That Began On The Quick Help Start Nothing") {
        {   // the second click of a double click (SDL: clicks 2), over both START buttons: a press on the new screen, the game starts (a local game is the original's screen, not guarded)
            Application app;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            ASSERT_TRUE(app.init(cfg));
            const PointerRig rig(app);
            ASSERT_TRUE(rig.window != nullptr);
            app.finish_loading();
            ASSERT_EQ(app.state(), AppState::QuickHelp);
            rig.button(SDL_MOUSEBUTTONDOWN, 1160, 900, 0, -1, 1);                  // picture (580, 450), the window is twice the picture
            rig.button(SDL_MOUSEBUTTONUP, 1160, 900, 0, -1, 1);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_EQ(app.mouse_screen_x(), 580);
            ASSERT_EQ(app.mouse_screen_y(), 450);
            ASSERT_FALSE(app.closing_click_pending());                              // nothing is held back on a local game's screen
            rig.button(SDL_MOUSEBUTTONDOWN, 1160, 900, 0, -1, 2);
            rig.button(SDL_MOUSEBUTTONUP, 1160, 900, 0, -1, 2);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::Playing);
            app.shutdown();
        }
        {   // a held Enter: the press closes the quick help, its repeat presses START (the original's key handler does not tell them apart)
            Application app;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            ASSERT_TRUE(app.init(cfg));
            const PointerRig rig(app);
            app.finish_loading();
            ASSERT_EQ(app.state(), AppState::QuickHelp);
            rig.key(SDL_KEYDOWN, SDLK_RETURN, false);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::MapSelect);
            rig.key(SDL_KEYDOWN, SDLK_RETURN, true);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::Playing);
            app.shutdown();
        }
        {   // a press that began on the quick help and is released after the quick help was closed by a key (the button held, Enter pressed): the release finds nothing pressed on the
            // new START, whatever the machine is, and does nothing; the quick help's own button is let go of
            Application app;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            ASSERT_TRUE(app.init(cfg));
            const PointerRig rig(app);
            app.finish_loading();
            rig.button(SDL_MOUSEBUTTONDOWN, 1160, 900);
            rig.deliver();
            ASSERT_TRUE(app.quick_help_start_button().pressed());
            rig.key(SDL_KEYDOWN, SDLK_RETURN, false);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_FALSE(app.quick_help_start_button().pressed());                  // (the window is gone: nothing stays pressed in it)
            ASSERT_FALSE(app.map_select().start_button().pressed());
            rig.button(SDL_MOUSEBUTTONUP, 1160, 900);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::MapSelect);                            // nothing started
            ASSERT_FALSE(app.map_select().start_button().pressed());
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("9.7 The Chat Box Is Always Active; Enter, [All] And [Team] Send; Hotkey Modifier Isolation") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/TREASURE.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& hud = app.hud();
        auto& sim_engine = app.sim();
        ViewportCamera cam;

        // 1. Verify that neither bare nor Ctrl hotkeys trigger any developer shortcut (there are none)
        SDL_KeyboardEvent key_ev{};
        key_ev.type = SDL_KEYDOWN;

        // Bare SDLK_t should not toggle tile grid
        bool initial_grid = app.is_tile_grid_visible();
        key_ev.keysym.sym = SDLK_t;
        key_ev.keysym.mod = 0; // No Ctrl!
        app.handle_key_down(key_ev);
        ASSERT_EQ(app.is_tile_grid_visible(), initial_grid);

        // With Ctrl Ctrl+T does not either: there is no such key in the original
        key_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key_ev);
        ASSERT_EQ(app.is_tile_grid_visible(), initial_grid);

        // 2. The chat box is always active: the text of the keys (SDL_TEXTINPUT) lands in it without any focus
        ASSERT_TRUE(hud.get_chat_input().empty());
        hud.handle_text_input("Rush the base!");
        ASSERT_EQ(hud.get_chat_input(), "Rush the base!");

        // Backspace removes a character
        key_ev.keysym.sym = SDLK_BACKSPACE;
        key_ev.keysym.mod = 0;
        app.handle_key_down(key_ev);
        ASSERT_EQ(hud.get_chat_input(), "Rush the base");

        // The key event of a printable key is not typed a second time (its text arrives as a text input event)
        key_ev.keysym.sym = SDLK_x;
        app.handle_key_down(key_ev);
        ASSERT_EQ(hud.get_chat_input(), "Rush the base");

        // Sound callback tracking
        uint32_t played_sound = 0;
        hud.set_on_play_sfx([&](uint32_t sid) {
            played_sound = sid;
        });

        // Enter sends the text (to everybody: the local player has no ally) and clears the box
        key_ev.keysym.sym = SDLK_RETURN;
        app.handle_key_down(key_ev);
        ASSERT_TRUE(hud.get_chat_input().empty());
        ASSERT_EQ(played_sound, 0u); // Chat sound effect disabled per user request

        // Verify message in chat log (wrapped across lines)
        const auto& log = hud.get_chat_log();
        ASSERT_FALSE(log.empty());
        bool found_msg = false;
        for (const auto& entry : log) {
            if (entry.find("Rush the") != std::string::npos) {
                found_msg = true;
                break;
            }
        }
        ASSERT_TRUE(found_msg);

        // 3. Word Wrapping Test for Long Chat Messages
        size_t log_size_before = log.size();
        hud.handle_text_input("This is a really long chat message that will exceed twenty-two characters per line");
        hud.send_chat_message();
        ASSERT_TRUE(hud.get_chat_input().empty());
        // Should have created multiple wrapped lines
        ASSERT_TRUE(log.size() >= log_size_before + 3);

        // 4. [All] and [Team] send the text at once; [Team] exists only while the local player has an ally
        ASSERT_FALSE(hud.is_on_team());
        hud.set_chat_input("Nobody hears this as a team message");
        size_t lines_before = log.size();
        hud.handle_mouse_down(590, 450, SDL_BUTTON_LEFT, sim_engine, cam);      // the Team button's place: no button without an ally
        hud.handle_mouse_up(590, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_EQ(log.size(), lines_before);
        ASSERT_EQ(hud.get_chat_input(), "Nobody hears this as a team message");

        // [All] sends when it is released on the button (a press that leaves the button and is released elsewhere sends nothing)
        hud.handle_mouse_down(545, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_motion(300, 300, sim_engine, cam);
        hud.handle_mouse_up(300, 300, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_EQ(log.size(), lines_before);
        hud.handle_mouse_down(545, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_motion(546, 451, sim_engine, cam);
        ASSERT_EQ(log.size(), lines_before);                                     // the callback runs at the release, not at the press
        hud.handle_mouse_up(546, 451, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_TRUE(log.size() > lines_before);
        ASSERT_TRUE(hud.get_chat_input().empty());

        // Now set on team
        hud.set_on_team(true);
        ASSERT_TRUE(hud.is_on_team());

        // [Team] (579..625, 443..467) sends to the team
        hud.set_player_name("QueenAnt");
        hud.set_chat_input("Teammates defend!");
        hud.handle_mouse_down(590, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_up(590, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        bool found_team_msg = false;
        for (const auto& entry : log) {
            if (entry.find("QueenAnt (To Teammate):") != std::string::npos) {
                found_team_msg = true;
                break;
            }
        }
        ASSERT_TRUE(found_team_msg);

        // Enter with an ally goes to the team as well
        hud.set_chat_input("Enter is a team message too");
        hud.handle_key_down(SDLK_RETURN, sim_engine, cam);
        bool found_enter_team = false;
        for (size_t li = 0; li + 1 < log.size(); ++li) {
            if (log[li] == "QueenAnt (To Teammate):" && log[li + 1].find("Enter is a team") == 0) found_enter_team = true;
        }
        ASSERT_TRUE(found_enter_team);

        // [All] (532..576, 443..467) sends to everybody even with an ally
        hud.set_chat_input("GG everyone");
        hud.handle_mouse_down(545, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_up(545, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        // an entry is a header line "QueenAnt:" (in the colour of the sender's team) and its body lines
        bool found_all_msg = false;
        for (size_t li = 0; li + 1 < log.size(); ++li) {
            if (log[li] == "QueenAnt:" && log[li + 1] == "GG everyone") {
                found_all_msg = true;
                ASSERT_TRUE(hud.get_chat_line_colour(li) <= 3);      // a team colour
                ASSERT_EQ(hud.get_chat_line_colour(li + 1), 5);      // the body colour (7, 11, 15)
                break;
            }
        }
        ASSERT_TRUE(found_all_msg);

        // 5. Escape is not a chat key: it deselects (FUN_01028c44) and leaves the typed text alone
        hud.set_chat_input("Unsent message");
        hud.select_ant(sim_engine.get_world_state().ants.front().id, false);
        key_ev.keysym.sym = SDLK_ESCAPE;
        app.handle_key_down(key_ev);
        ASSERT_EQ(hud.get_chat_input(), "Unsent message");
        ASSERT_EQ(hud.get_selected_ant_id(), 0u);
        ASSERT_FALSE(hud.is_quit_dialog_open());
    } TEST_END();
}

// ============================================================================
// SUITE 10: Egg Economy, Incubation Emergence, Team-Up & Smart Abilities
// ============================================================================
void run_suite_10_egg_economy_incubation_teamup_abilities() {
    TEST_SUITE("Suite 10: Egg Economy, Incubation Emergence, Team-Up & Smart Abilities");

    TEST_CASE("10.1 Per-Map Egg Stock & Boundary Param Mapping") {
        std::vector<std::pair<std::string, uint16_t>> expected_map_eggs = {
            {"TREASURE.LVL", 9},
            {"MEDIUM.LVL", 6},
            {"GAUNTLET.LVL", 6},
            {"ISLANDS.LVL", 4},
            {"TINY.LVL", 3},
            {"SMALL.LVL", 2}
        };

        for (const auto& [map_name, expected_eggs] : expected_map_eggs) {
            std::string path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + map_name;
            ants::assets::LevelData lvl;
            if (lvl.load_lvl(path)) {
                ASSERT_EQ(lvl.boundary_param, expected_eggs);
                ASSERT_EQ(lvl.initial_eggs(), expected_eggs);

                SimulationEngine sim;
                sim.init(lvl, 42);
                for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                    ASSERT_EQ(sim.get_player_eggs(p), expected_eggs);
                }
            }
        }
    } TEST_END();

    TEST_CASE("10.2 Egg Consumption & Zero Egg Prohibits Hatching") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.grid_mut().set_anthill(0, TileCoord{10, 10});
        sim.set_player_score(0, 600);
        sim.set_player_eggs(0, 2);

        ASSERT_EQ(sim.get_player_eggs(0), 2u);
        ASSERT_EQ(sim.get_player_score(0), 600);

        // 1st Hatch: 600 -> 400 pts, 2 -> 1 eggs
        bool h1 = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(h1);
        ASSERT_EQ(sim.get_player_score(0), 400);
        ASSERT_EQ(sim.get_player_eggs(0), 1u);

        // Only one egg incubates at a time: a click while one hatches costs nothing ("An Ant is already hatching!")
        ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));
        ASSERT_EQ(sim.get_player_score(0), 400);
        ASSERT_EQ(sim.get_player_eggs(0), 1u);
        for (int t = 0; t < 170 && sim.get_pending_hatch_count(0) > 0; ++t) sim.tick();   // 8000 ms incubation
        ASSERT_EQ(sim.get_pending_hatch_count(0), 0u);

        // 2nd Hatch: 400 -> 200 pts, 1 -> 0 eggs
        bool h2 = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(h2);
        ASSERT_EQ(sim.get_player_score(0), 200);
        ASSERT_EQ(sim.get_player_eggs(0), 0u);
        for (int t = 0; t < 170 && sim.get_pending_hatch_count(0) > 0; ++t) sim.tick();

        // 3rd Hatch: score is 200 (sufficient score), but 0 eggs available -> Must fail!
        bool h3 = sim.hatch_ant(0, AntType::Worker);
        ASSERT_FALSE(h3);
        ASSERT_EQ(sim.get_player_score(0), 200);
        ASSERT_EQ(sim.get_player_eggs(0), 0u);

        // Even with huge score (e.g. 10,000 pts), 0 eggs strictly prohibits hatching
        sim.set_player_score(0, 10000);
        bool h4 = sim.hatch_ant(0, AntType::Worker);
        ASSERT_FALSE(h4);
        ASSERT_EQ(sim.get_player_score(0), 10000);
        ASSERT_EQ(sim.get_player_eggs(0), 0u);
    } TEST_END();

    TEST_CASE("10.3 Hatch Incubation Delay, Hole Emergence & Idle Routing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.grid_mut().set_anthill(0, TileCoord{10, 10});
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 5);
        sim.set_hatch_delay_ticks(20); // 20 ticks delay for testing

        const auto* home = sim.grid().find_anthill(0);
        ASSERT_TRUE(home != nullptr);
        int32_t bx = home->x;
        int32_t by = home->y;

        bool hatched = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(hatched);

        // While the egg incubates there is no ant at all (the original creates the newborn when HATCHTSK runs)
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        for (const auto& a : sim.get_world_state().ants) ASSERT_TRUE(a.player_id != 0);

        // Tick 10 ticks: still incubating
        for (int i = 0; i < 10; ++i) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        for (const auto& a : sim.get_world_state().ants) ASSERT_TRUE(a.player_id != 0);

        // Tick 15 more ticks (total 25 ticks > 20 delay): the newborn appears on the entrance tile (bx+1, by+1)
        // playing the hatch clip
        for (int i = 0; i < 15; ++i) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 0u);
        uint32_t newborn_id = 0;
        for (const auto& a : sim.get_world_state().ants) {
            if (a.player_id == 0) newborn_id = a.id;
        }
        ASSERT_TRUE(newborn_id != 0);
        const auto& u_new = sim.get_unit(newborn_id);
        ASSERT_EQ(u_new.pos.x, bx + 1);
        ASSERT_EQ(u_new.pos.y, by + 1);
        ASSERT_EQ(u_new.state, UnitState::EnteringBase);
        ASSERT_TRUE(sim.has_audio_event(SoundID::ExitHill));

        // Advance simulation until the ant finishes the clip and walks to the idle spot tile42 (bx+4, by+4)
        for (int i = 0; i < 130; ++i) sim.tick();

        const auto& u_final = sim.get_unit(newborn_id);
        ASSERT_EQ(u_final.pos.x, bx + 4);
        ASSERT_EQ(u_final.pos.y, by + 4);
        ASSERT_EQ(u_final.state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("10.4 Right-Click Special Abilities Autonomous Approach") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{5, 5});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // 1. Bomber at (5, 5) orders bomb at distance (8, 5) -> pixel (8*32 + 16, 5*32 + 16) = (272, 176)
        hud.select_ant(bomber);
        int32_t bx = 272 + HUD::PLAYFIELD_X;
        int32_t by = 176 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(bx, by, 3, sim, camera);
        hud.handle_mouse_up(bx, by, 3, sim, camera);

        const auto& b_unit = sim.get_unit(bomber);
        ASSERT_EQ(b_unit.orig_order, AntUnit::kOrderPlant);
        ASSERT_EQ(b_unit.orig_special_tile, (TileCoord{8, 5}));

        // The bomber walks to a neighbour tile of the target (the nearest one: west of it), plants, and the bomb appears
        ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return sim.has_bomb_at(TileCoord{8, 5}); }) >= 0);
        ASSERT_EQ(b_unit.pos, (TileCoord{7, 5}));

        // 2. Swimmer at (6, 8). Set water at distance (10, 8). Right-click (10, 8) -> pixel (10*32 + 16, 8*32 + 16)
        uint32_t swimmer2 = sim.spawn_unit(0, AntType::Swimmer, TileCoord{6, 8});
        sim.set_terrain(10, 8, 2); // 2 = TERRAIN_WATER
        hud.select_ant(swimmer2);
        int32_t sx = (10 * 32 + 16) + HUD::PLAYFIELD_X;
        int32_t sy = (8 * 32 + 16) + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(sx, sy, 3, sim, camera);
        hud.handle_mouse_up(sx, sy, 3, sim, camera);

        const auto& s_unit = sim.get_unit(swimmer2);
        ASSERT_EQ(s_unit.orig_order, AntUnit::kOrderBridgeBuild);
        ASSERT_EQ(s_unit.orig_special_tile, (TileCoord{10, 8}));

        ASSERT_TRUE(wait_ms(sim, 10000, [&]() { return sim.grid().get_cell(TileCoord{10, 8}).has_completed_bridge(); }) >= 0);

        // 3. Swimmer right-clicking land tile -> standard Move order (no bridge built on land)
        hud.select_ant(swimmer2);
        int32_t mx = (6 * 32 + 16) + HUD::PLAYFIELD_X;
        int32_t my = (6 * 32 + 16) + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(mx, my, 3, sim, camera);
        hud.handle_mouse_up(mx, my, 3, sim, camera);
        const auto& s_unit2 = sim.get_unit(swimmer2);
        ASSERT_NE(s_unit2.orig_order, AntUnit::kOrderBridgeBuild);
        ASSERT_FALSE(sim.has_bridge_at(TileCoord{6, 6}));
    } TEST_END();

    TEST_CASE("10.5 Enemy Base Click & The Ally Pedestal (more than two players, not yet allied)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{2, 2});
        sim.grid_mut().set_anthill(1, TileCoord{8, 8});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0); // local player is 0

        const auto* enemy_base = sim.grid().find_anthill(1);
        ASSERT_TRUE(enemy_base != nullptr);
        int32_t ebx = enemy_base->x;
        int32_t eby = enemy_base->y;

        // Click on enemy base in world -> selects enemy base
        int32_t cx = (ebx * 32 + 16) + HUD::PLAYFIELD_X;
        int32_t cy = (eby * 32 + 16) + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(cx, cy, 1, sim, camera);
        hud.handle_mouse_up(cx, cy, 1, sim, camera);

        ASSERT_EQ(hud.get_selected_base_team_id(), 1);

        // Clicking Stop button position (605, 195) does not clear enemy base selection (stop button is absent)
        hud.handle_mouse_down(605, 195, 1, sim, camera);
        hud.handle_mouse_up(605, 195, 1, sim, camera);
        ASSERT_EQ(hud.get_selected_base_team_id(), 1);

        // With two players there is no ally pedestal (slot 1 of another player's hill exists only with more than two players)
        auto proposal_sound = [&]() {
            for (const auto& ev : sim.poll_audio_events()) {
                if (ev.sound_id == SoundID::AlliancePro) return true;
            }
            return false;
        };
        hud.handle_mouse_down(495, 165, 1, sim, camera);
        hud.handle_mouse_up(495, 165, 1, sim, camera);
        ASSERT_FALSE(proposal_sound());

        // A third player's hill makes the pedestal appear: the click proposes the alliance
        sim.grid_mut().set_anthill(2, TileCoord{14, 14});
        sim.tick();                                            // the world snapshot is rebuilt with the third hill
        hud.select_base(1);
        hud.handle_mouse_down(495, 165, 1, sim, camera);      // slot 1: (482, 152) - (525, 225)
        hud.handle_mouse_up(495, 165, 1, sim, camera);
        ASSERT_TRUE(proposal_sound());

        // Nobody answers: there is no computer opponent that accepts the invitation, it stays open
        for (int i = 0; i < 35; ++i) sim.tick();
        ASSERT_NE(sim.get_world_state().player_alliances[0], 1u);

        // A command that answers an invitation nobody made is refused; player 1's own accept makes the team
        Command bogus;
        bogus.type = CommandType::AllianceAccept;
        bogus.issuer = 2;
        bogus.other_player = 0;
        ASSERT_EQ(sim.apply_command(bogus).status, CommandResult::Status::RejectedNotAllowed);
        Command accept;
        accept.type = CommandType::AllianceAccept;
        accept.issuer = 1;
        accept.other_player = 0;
        ASSERT_TRUE(sim.apply_command(accept).accepted());
        const auto& ws_allied = sim.get_world_state();
        ASSERT_EQ(ws_allied.player_alliances[0], 1u);
        ASSERT_EQ(ws_allied.player_alliances[1], 0u);

        // Allied with the owner: the pedestal is gone, its place does nothing (the alliance is left through the ally attack dialog)
        hud.update(sim.get_world_state(), 1);
        hud.select_base(1);
        hud.handle_mouse_down(495, 165, 1, sim, camera);
        hud.handle_mouse_up(495, 165, 1, sim, camera);
        ASSERT_FALSE(proposal_sound());
        ASSERT_EQ(sim.get_world_state().player_alliances[0], 1u);
    } TEST_END();
}

// ============================================================================
// SUITE 11: Anthill Queuing & Priority Serialization
// ============================================================================
void run_suite_11_anthill_queuing_and_priority() {
    TEST_SUITE("Suite 11: Anthill Queuing & Priority Serialization");


    TEST_CASE("11.2 Serialized Base Entry & Priority Queue (Stand Off to Side & Wait)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x;
        int32_t by = base->y;

        // Spawn 3 workers carrying food next to the hill
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by});
        uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 1});
        uint32_t a3 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 2});

        sim.get_unit(a1).pick_up_food(1, 25);
        sim.get_unit(a2).pick_up_food(1, 25);
        sim.get_unit(a3).pick_up_food(1, 25);

        // Order all three onto the hill in order 1, 2, 3
        sim.join_base_queue(a1);
        sim.join_base_queue(a2);
        sim.join_base_queue(a3);

        // The first ant may take the free entrance; the entrance is claimed by it, so the other two were sent to the
        // waiting tiles in front of the hill (+0x68 = 1)
        ASSERT_FALSE(sim.is_ant_in_base_queue(a1));
        ASSERT_EQ(sim.get_unit(a1).orig_order, AntUnit::kOrderHome);
        ASSERT_TRUE(sim.is_ant_in_base_queue(a2));
        ASSERT_TRUE(sim.is_ant_in_base_queue(a3));

        // Run until all three deposited: the enter clips never overlap and the ants take the entrance one by one
        std::vector<uint32_t> entry_order;
        uint32_t last_active = 0;
        bool a1_entering_with_two_waiting = false;
        for (int i = 0; i < 1500 && sim.get_player_score(0) < 75; ++i) {
            sim.tick();
            int entering = 0;
            for (uint32_t id : {a1, a2, a3}) {
                if (sim.get_unit(id).state == UnitState::EnteringBase) ++entering;
            }
            ASSERT_TRUE(entering <= 1);
            const uint32_t active = sim.get_active_depositing_ant(0);
            if (active != 0 && active != last_active) {
                entry_order.push_back(active);
                if (entry_order.size() == 1 && sim.get_base_queue_size(0) == 2u) a1_entering_with_two_waiting = true;
            }
            last_active = active;
        }

        // The ant that had the free entrance went first while the other two stood queued on the waiting tiles
        ASSERT_EQ(entry_order.size(), 3u);
        ASSERT_EQ(entry_order[0], a1);
        ASSERT_TRUE(a1_entering_with_two_waiting);
        ASSERT_TRUE(entry_order[1] != entry_order[2] && entry_order[1] != a1 && entry_order[2] != a1);

        // Every ant scored its food when its clip ended: scoreup.wav (Sound 87) and a total of 75 points
        ASSERT_EQ(sim.get_player_score(0), 75);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BaseScoreUp));
        ASSERT_FALSE(sim.get_unit(a1).is_holding());
        ASSERT_FALSE(sim.get_unit(a2).is_holding());
        ASSERT_FALSE(sim.get_unit(a3).is_holding());
    } TEST_END();

    TEST_CASE("11.3 Escape Deselects Without A Quit Dialog; Ctrl+Q Opens It; Esc / N Answer No, Enter Does Nothing, Y Quits") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        ViewportCamera camera;
        HUD hud;
        hud.init(0);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        hud.select_ant(a1);
        ASSERT_FALSE(hud.get_selected_ant_ids().empty());
        ASSERT_FALSE(hud.is_quit_dialog_open());

        // Escape (27) deselects everything (FUN_01028c44(0)); there is no quit dialog on Esc
        hud.handle_key_down(27, sim, camera);
        ASSERT_TRUE(hud.get_selected_ant_ids().empty());
        ASSERT_FALSE(hud.is_quit_dialog_open());

        // Ctrl+Q runs the quit flow of the Quit button
        hud.handle_key_down('q', sim, camera, KMOD_CTRL);
        ASSERT_TRUE(hud.is_quit_dialog_open());

        // Enter does nothing in the quit dialog, Escape and N answer No
        hud.handle_key_down(SDLK_RETURN, sim, camera);
        ASSERT_TRUE(hud.is_quit_dialog_open());
        hud.handle_key_down(SDLK_ESCAPE, sim, camera);
        ASSERT_FALSE(hud.is_quit_dialog_open());
        hud.handle_key_down('q', sim, camera, KMOD_CTRL);
        hud.handle_key_down('n', sim, camera);
        ASSERT_FALSE(hud.is_quit_dialog_open());

        // Verify full Application::handle_key_down wiring
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.is_running());
        ASSERT_FALSE(app.hud().is_quit_dialog_open());

        SDL_KeyboardEvent esc_ev{};
        esc_ev.type = SDL_KEYDOWN;
        esc_ev.keysym.sym = SDLK_ESCAPE;

        // Escape in-game opens nothing
        app.handle_key_down(esc_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());

        // Ctrl+Q opens the quit dialog
        SDL_KeyboardEvent q_ev{};
        q_ev.type = SDL_KEYDOWN;
        q_ev.keysym.sym = SDLK_q;
        q_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(q_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open());

        // Pressing 'N' closes quit dialog (a printable key reaches the dialog although the chat box takes typed text)
        SDL_KeyboardEvent n_ev{};
        n_ev.type = SDL_KEYDOWN;
        n_ev.keysym.sym = SDLK_n;
        app.handle_key_down(n_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());

        // Ctrl+Q and then 'Y' quits the game
        app.handle_key_down(q_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        SDL_KeyboardEvent y_ev{};
        y_ev.type = SDL_KEYDOWN;
        y_ev.keysym.sym = SDLK_y;
        app.handle_key_down(y_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());
        ASSERT_FALSE(app.is_running());
    } TEST_END();

}

// ============================================================================
// Suite 12: Unit Selection & Occupied Tile Collision Handling
// ============================================================================
void run_suite_12_unit_selection_and_occupied_tile_movement() {
    TEST_SUITE("Suite 12: Unit Selection & Occupied Tile Collision Handling");

    TEST_CASE("12.1 Tile Click Unit Selection With Pre-Selected Unit") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        uint32_t a2 = sim.spawn_unit(0, AntType::Combat, TileCoord{8, 8});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Pre-select a1
        hud.select_ant(a1);
        ASSERT_EQ(hud.get_selected_ant_id(), a1);

        // Click within a2's tile (8, 8) -> center is pixel (8*32+16, 8*32+16) = (272, 272)
        // Test an off-center click at (272 + 10, 272 - 12) = (282, 260)
        int32_t click_x = 282 + HUD::PLAYFIELD_X;
        int32_t click_y = 260 + HUD::PLAYFIELD_Y;

        hud.handle_mouse_down(click_x, click_y, 1, sim, camera);
        hud.handle_mouse_up(click_x, click_y, 1, sim, camera);

        // a2 must be selected instead of moving a1 to a2's tile!
        ASSERT_EQ(hud.get_selected_ant_id(), a2);
        ASSERT_EQ(hud.get_selected_ant_ids().size(), 1u);
        ASSERT_EQ(hud.get_selected_ant_ids()[0], a2);

        // a1 must remain idle at its starting tile and not have been ordered to move
        ASSERT_EQ(sim.get_unit(a1).pos, (TileCoord{5, 5}));
        ASSERT_TRUE(sim.get_unit(a1).waypoints.empty());
    } TEST_END();

    TEST_CASE("12.2 Move Order to Occupied Friendly Tile (Stops Cleanly Without Bumping Loop)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Spawn stationary friendly ant at (15, 15)
        uint32_t stat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{15, 15});
        auto& stat_ant = sim.get_unit(stat_id);
        stat_ant.state = UnitState::Idle;

        // Spawn moving friendly ant at (15, 12)
        uint32_t mover_id = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 12});

        // Issue move order directly to the stationary ant's tile (15, 15)
        sim.issue_move_order(mover_id, TileCoord{15, 15});

        // Step simulation until mover ant arrives
        for (int i = 0; i < 60; ++i) {
            sim.tick();
        }

        const auto& mover = sim.get_unit(mover_id);

        // Stationary ant stays at (15, 15) and was not pushed
        ASSERT_EQ(stat_ant.pos.x, 15);
        ASSERT_EQ(stat_ant.pos.y, 15);

        // Mover ant stopped at an adjacent tile
        int32_t dx = std::abs(mover.pos.x - 15);
        int32_t dy = std::abs(mover.pos.y - 15);
        ASSERT_TRUE(dx <= 1 && dy <= 1);
        ASSERT_FALSE(mover.pos.x == 15 && mover.pos.y == 15);

        // Mover ant is idle, not walking or struggling
        ASSERT_EQ(mover.state, UnitState::Idle);
        ASSERT_TRUE(mover.waypoints.empty());

        // Audio queue must NOT contain FlingThumpA (Sound 64)
        ASSERT_FALSE(sim.has_audio_event(SoundID::FlingThumpA));

        // Step 30 more ticks: mover ant must remain stationary without bouncing
        TileCoord mover_settled_pos = mover.pos;
        for (int i = 0; i < 30; ++i) {
            sim.tick();
            ASSERT_EQ(sim.get_unit(mover_id).pos, mover_settled_pos);
            ASSERT_FALSE(sim.has_audio_event(SoundID::FlingThumpA));
        }
    } TEST_END();

    TEST_CASE("12.3 Move Order Onto an Occupied Tile Picks the First Free Ring Tile (FUN_010202e7)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Spawn stationary ant at (20, 20)
        uint32_t stat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        auto& stat_ant = sim.get_unit(stat_id);
        stat_ant.state = UnitState::Idle;

        // Spawn friendly ant at adjacent tile (20, 21)
        uint32_t adj_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 21});
        auto& adj_ant = sim.get_unit(adj_id);
        adj_ant.state = UnitState::Idle;

        // Original GoTo: the taken tile is replaced by the first enterable tile of the ring scan around it
        // (distance 1: west column top to bottom, then east column, north row, south row) -> (19, 19).
        sim.issue_move_order(adj_id, TileCoord{20, 20});
        ASSERT_EQ(adj_ant.final_dest, (TileCoord{19, 19}));
        ASSERT_TRUE(tick_until(sim, [&]() { return adj_ant.pos == TileCoord{19, 19} && adj_ant.waypoints.empty() &&
                                                  adj_ant.state == UnitState::Idle; }, 60));
        ASSERT_EQ(stat_ant.pos, (TileCoord{20, 20}));   // never pushed
        ASSERT_FALSE(sim.has_audio_event(SoundID::FlingThumpA));
    } TEST_END();

    TEST_CASE("12.4 Pile-Up Dispersal: Ants On One Tile Are Thrown Apart Without Damage And Rest On Tile Centres") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // A pile-up in the original: an ant lands on a tile that another ant stands on (WalkStep block C, Blast 0, 7)
        uint32_t a1_id = sim.spawn_unit(0, AntType::Worker, TileCoord{25, 25});
        uint32_t a2_id = sim.spawn_unit(1, AntType::Worker, TileCoord{26, 25});   // is hit and thrown to (27, 25)
        uint32_t a3_id = sim.spawn_unit(1, AntType::Worker, TileCoord{27, 25});   // stands on the landing tile
        sim.execute_melee_attack(a1_id, a2_id);

        const auto& a1 = sim.get_unit(a1_id);
        const auto& a2 = sim.get_unit(a2_id);
        const auto& a3 = sim.get_unit(a3_id);

        // Everything is thrown apart and comes to rest
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return a2.loco_action == AntUnit::kActionIdle &&
                                                       a3.loco_action == AntUnit::kActionIdle &&
                                                       a2.pos != a3.pos && a2.pos != a1.pos && a3.pos != a1.pos &&
                                                       a2.engaged == false; }) >= 0);
        ASSERT_EQ(a2.hp, 9u);   // the one blow
        ASSERT_EQ(a3.hp, 10u);  // the pile-up itself does no damage
        ASSERT_EQ(a1.hp, 10u);

        // All 3 ants rest exactly on tile centres (NEVER halfway between tiles)
        run_ms(sim, 500);
        ASSERT_EQ(a1.pixel_x, a1.pos.x * 32 + 16);
        ASSERT_EQ(a1.pixel_y, a1.pos.y * 32 + 16);
        ASSERT_EQ(a2.pixel_x, a2.pos.x * 32 + 16);
        ASSERT_EQ(a2.pixel_y, a2.pos.y * 32 + 16);
        ASSERT_EQ(a3.pixel_x, a3.pos.x * 32 + 16);
        ASSERT_EQ(a3.pixel_y, a3.pos.y * 32 + 16);
    } TEST_END();

    TEST_CASE("12.5 Multi-Ant Food Harvesting: The Group Order Sends Every Ant To The Pile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{30, 5});   // the ants with food walk home and free their tiles for the others

        // A pile of crackers (2x2 cells around its anchor (21, 21)): 8 units of 25 points
        const int32_t pile = place_pile(sim, 21, 21, 8, 25, {{8, 369}, {6, 370}, {4, 371}, {2, 372}, {0, 0x7FFE}});
        ASSERT_EQ(sim.grid().get_cell({20, 20}).interactive_id, 369u);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{20, 20}), pile);

        // Spawn 4 workers at (15, 20), (15, 21), (16, 20), (16, 21)
        const uint32_t w[4] = {sim.spawn_unit(0, AntType::Worker, TileCoord{15, 20}), sim.spawn_unit(0, AntType::Worker, TileCoord{15, 21}),
                               sim.spawn_unit(0, AntType::Worker, TileCoord{16, 20}), sim.spawn_unit(0, AntType::Worker, TileCoord{16, 21})};

        HUD hud;
        hud.init(0);
        hud.select_ant(w[0]);
        hud.set_selected_ant_ids({w[0], w[1], w[2], w[3]});

        // A move click on the pile (cursor mode 7): the ordinary group order, no slots around the pile
        hud.order_selected(sim, TileCoord{21, 21}, false, false);
        for (uint32_t id : w) {
            ASSERT_EQ(sim.get_unit(id).orig_order, AntUnit::kOrderHarvest);
            ASSERT_EQ(sim.get_unit(id).orig_order_tile, (TileCoord{21, 21}));
        }

        // Advance until every ant has carried food once: each ant plays its own grab clip and the cue of the clip
        bool held[4] = {false, false, false, false};
        int harvest_sound_count = 0;
        for (int t = 0; t < 600; ++t) {
            sim.tick();
            for (const auto& e : sim.poll_audio_events()) {
                if (e.sound_id == SoundID::FoodHarvest || e.sound_id == SoundID::FoodGrab) harvest_sound_count++;
            }
            for (size_t k = 0; k < 4; ++k) held[k] = held[k] || sim.get_unit(w[k]).is_holding();
            if (held[0] && held[1] && held[2] && held[3]) break;
        }
        for (size_t k = 0; k < 4; ++k) ASSERT_TRUE(held[k]);

        // Four bites were taken, one per ant, and the pile shows its 4-unit stage
        ASSERT_EQ(harvest_sound_count, 4);
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 4u);
        ASSERT_EQ(sim.grid().get_cell({21, 21}).interactive_id, 371u);
    } TEST_END();

    TEST_CASE("12.6 Swarm Movement Non-Bouncing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Spawn 8 worker ants closely grouped
        std::vector<uint32_t> swarm;
        for (int dy = 0; dy < 3; ++dy) {
            for (int dx = 0; dx < 3; ++dx) {
                if (dx == 1 && dy == 1) continue;
                swarm.push_back(sim.spawn_unit(0, AntType::Worker, TileCoord{10 + dx, 10 + dy}));
            }
        }
        ASSERT_EQ(swarm.size(), 8u);

        HUD hud;
        hud.init(0);
        hud.select_ant(swarm[0]);
        hud.set_selected_ant_ids(swarm);

        // Move group to (20, 20)
        hud.dispatch_move_order(20, 20, sim);

        // Step 120 ticks: moving ants should never bounce or wipe waypoints
        bool had_bounce = false;
        for (int t = 0; t < 120; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::FlingThumpA)) {
                had_bounce = true;
            }
        }

        // Must NOT have bounced moving ants
        ASSERT_FALSE(had_bounce);

        // All ants must have made substantial progress toward (20, 20)
        for (uint32_t aid : swarm) {
            const auto& ant = sim.get_unit(aid);
            int32_t d = ant.pos.chebyshev_dist(TileCoord{20, 20});
            ASSERT_TRUE(d <= 3);
        }
    } TEST_END();

    TEST_CASE("12.7 An Attack Order Walks Up To The Target: The Blow Comes When The Step Crosses The Border, One Blow Per Order") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Spawn Combat Ant at (10, 10) on Team 0
        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        // Spawn Enemy Worker at (15, 10) on Team 1 (Chebyshev dist = 5: outside the auto-engage radius)
        uint32_t target_id = sim.spawn_unit(1, AntType::Worker, TileCoord{15, 10});

        auto& attacker = sim.get_unit(combat_id);
        auto& target = sim.get_unit(target_id);
        int32_t init_hp = target.hp;

        // Ordering attack from distance (dist = 5)
        AntOrder atk_order{};
        atk_order.ant_id = combat_id;
        atk_order.type = OrderType::Attack;
        atk_order.target_x = 15;
        atk_order.target_y = 10;
        atk_order.target_entity_id = static_cast<int32_t>(target_id);
        sim.issue_order(atk_order);

        // Target must NOT have taken damage immediately (not adjacent)
        ASSERT_EQ(target.hp, init_hp);
        ASSERT_EQ(attacker.orig_order, AntUnit::kOrderAttack);

        // The target takes no damage until the attacker's step crosses into its tile
        ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return target.hp != init_hp; }) > 0);
        ASSERT_EQ(attacker.pos, (TileCoord{14, 10}));

        // The contact takes 2 hit points (a combat ant), the blow is a single one: no further strike
        ASSERT_EQ(target.hp, init_hp - 2);
        run_ms(sim, 5000);
        ASSERT_EQ(target.hp, init_hp - 2);
        ASSERT_EQ(attacker.orig_order, AntUnit::kOrderNone);
    } TEST_END();

    TEST_CASE("12.8 Bomber Ant Plant Bomb On Gravel") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Set gravel surface at (20, 20) with walkable terrain
        auto& cell = sim.grid_mut().get_cell_mut({20, 20});
        cell.surface_type = SurfaceType::Gravel;
        cell.terrain_type = TERRAIN_WALKABLE;
        cell.flags |= (FLAG_CAN_PLACE_BOMB | FLAG_CAN_PLACE_FIRE);

        ASSERT_TRUE(cell.can_place_bomb());

        // Spawn bomber ant at (20, 23) (3 tiles south)
        uint32_t bomber_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 23});

        HUD hud;
        hud.init(0);
        hud.select_ant(bomber_id);

        // The special order (a latched bomb pedestal makes a plantable tile a valid target) on the gravel tile (20, 20)
        hud.order_selected(sim, TileCoord{20, 20}, true, false);

        // Step simulation for 50 ticks until bomber approaches cardinal neighbor (20, 21) and drops bomb
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (sim.has_bomb_at({20, 20})) break;
        }

        // Bomb successfully planted on gravel tile!
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombPick));
    } TEST_END();

    TEST_CASE("12.9 The Chat Log: Initial Message, Pixel Wrapping, The Window Follows The Newest Entry; A Drag Peeks Back, The Wheel And PageUp Do Nothing") {
        HUD hud;
        hud.init(0);
        SimulationEngine sim_engine;
        sim_engine.init_test_world(60, 60, 1);
        ViewportCamera cam;

        // The start message is a News Flash entry: the header "[0:00] News Flash:" and its body, wrapped at 126 px (20 characters of 6 px here)
        const auto& log = hud.get_chat_log();
        ASSERT_TRUE(!log.empty());
        for (const auto& line : log) ASSERT_TRUE(line.length() <= 21);
        ASSERT_EQ(hud.chat_follow_pos(), 0);

        // Add 10 chat messages: the log outgrows the 101 px view and the window follows the newest entry (5 px per 50 ms pass)
        static uint32_t clock_now;
        clock_now = 1000;
        hud.set_ticks_function([]() { return clock_now; });
        for (int i = 0; i < 10; ++i) hud.add_chat_entry("Player", "Message number " + std::to_string(i));
        ASSERT_TRUE(hud.get_chat_log().size() >= 20);            // ten entries of a header and a body line each, plus the news flash
        ASSERT_TRUE(hud.chat_content_end() > HUD::kChatViewH);
        ASSERT_EQ(hud.chat_follow_pos(), 0);                     // nothing has moved yet
        ASSERT_EQ(hud.chat_follow_target(), hud.chat_content_end() - 1 - HUD::kChatViewH);

        hud.add_chat_entry("Player", "one more");
        const int32_t target = hud.chat_follow_target();
        for (int pass = 0; pass < 200 && hud.chat_follow_pos() != target; ++pass) {
            clock_now += 50;
            hud.update(sim_engine.get_world_state(), 1);
        }
        ASSERT_EQ(hud.chat_follow_pos(), target);

        // A drag inside the view: the log moves with the pointer (up = later lines) and snaps back at the release
        const int32_t follow = hud.chat_follow_pos();
        hud.handle_mouse_down(500, 350, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_TRUE(hud.chat_dragging());
        ASSERT_EQ(hud.chat_view_offset(), follow);
        hud.handle_mouse_motion(500, 380, sim_engine, cam);      // 30 px down: the earlier lines come into view
        ASSERT_EQ(hud.chat_view_offset(), follow - 30);
        hud.handle_mouse_up(500, 380, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_FALSE(hud.chat_dragging());
        ASSERT_EQ(hud.chat_view_offset(), follow);

        // PageUp does nothing (the original has no key for the log)
        hud.handle_key_down(SDLK_PAGEUP, sim_engine, cam, 0, false);
        ASSERT_EQ(hud.chat_view_offset(), follow);

        // Sending a chat message adds an entry; the window is not reset (it follows)
        hud.set_chat_input("Hello Colony!");
        hud.send_chat_message();
        ASSERT_EQ(hud.chat_follow_target(), hud.chat_content_end() - 1 - HUD::kChatViewH);
    } TEST_END();

    TEST_CASE("12.9b The Chat Transcript: The Program Writes The Chat Log To A Text File When It Ends (Ants.exe 0x10122d4: \"%s @ %s\\n\\n\" and \"%s %s\\n\" per entry)") {
        // the stamp is the C runtime's _strdate and _strtime: the original's own chat.txt starts "04/17/98 @ 18:19:56"
        std::tm when{};
        when.tm_year = 98; when.tm_mon = 3; when.tm_mday = 17; when.tm_hour = 18; when.tm_min = 19; when.tm_sec = 56; when.tm_isdst = -1;
        ASSERT_EQ(Application::transcript_stamp(std::mktime(&when)), "04/17/98 @ 18:19:56");

        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        app.hud().add_chat_entry("Ann", "Rush the base!", false, 2);
        app.hud().add_chat_entry("Bob", "on my way", true, 3);
        const std::filesystem::path dir = temp_path_of_this_run("ants_test_chat_transcript");
        std::filesystem::create_directories(dir);
        const std::filesystem::path file = dir / "chat.txt";
        std::filesystem::remove(file);
        ASSERT_TRUE(app.write_chat_transcript(file.string()));
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();                                      // (Windows cannot delete a file that is still open: the remove below threw there)
        // "date @ time", a blank line, then one line per entry: its header, a space, its body (the body is not wrapped in the file)
        const size_t blank = text.find("\n\n");
        ASSERT_TRUE(blank != std::string::npos && blank == 19 && text[2] == '/' && text[5] == '/' && text.substr(8, 3) == " @ ");
        ASSERT_EQ(text.substr(blank + 2), "[0:00] News Flash: Game started! Go get that food!\nAnn: Rush the base!\nBob (To Teammate): on my way\n");
        std::filesystem::remove(file);
        std::filesystem::remove(dir);
        // a headless run writes no file at its end (and neither does a run that never built a match screen)
        app.shutdown();
        ASSERT_FALSE(std::filesystem::exists(dir / "chat.txt"));
    } TEST_END();

    TEST_CASE("12.10 Bomb Size & Team Color Rendering") {
        // Authentic Table 4 animations (129-132) define bombs as 12x24 pixels centered at (sx + 10, sy + 4)
        constexpr int32_t kTileDim = 32;
        constexpr int32_t BOMB_WIDTH = 12;
        constexpr int32_t BOMB_HEIGHT = 24;
        constexpr int32_t OFFSET_X = 10;
        constexpr int32_t OFFSET_Y = 4;

        int32_t sx = 100;
        int32_t sy = 200;
        SDL_Rect bomb_dst = { sx + OFFSET_X, sy + OFFSET_Y, BOMB_WIDTH, BOMB_HEIGHT };

        ASSERT_EQ(bomb_dst.w, 12);
        ASSERT_EQ(bomb_dst.h, 24);
        ASSERT_EQ(bomb_dst.x, 110);
        ASSERT_EQ(bomb_dst.y, 204);
        ASSERT_EQ(OFFSET_X * 2 + BOMB_WIDTH, kTileDim); // Perfectly centered horizontally: (32 - 12) / 2 = 10
        ASSERT_EQ(OFFSET_Y + BOMB_HEIGHT + 4, kTileDim);

        // Verify team color mapping: 0=Green, 1=Red, 2=Blue, 3=Black
        static const char* bomb_names[4] = { "1bombgrn.bmp", "1bombred.bmp", "1bombblu.bmp", "1bombblk.bmp" };
        ASSERT_TRUE(std::string(bomb_names[0]) == "1bombgrn.bmp");
        ASSERT_TRUE(std::string(bomb_names[1]) == "1bombred.bmp");
        ASSERT_TRUE(std::string(bomb_names[2]) == "1bombblu.bmp");
        ASSERT_TRUE(std::string(bomb_names[3]) == "1bombblk.bmp");
    } TEST_END();

    TEST_CASE("12.11 Friendly Bomb Autonomous Pathfinding Avoidance") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place friendly bomb at (20, 20) owned by Team 0
        sim.grid_mut().place_bomb(20, 20, 0);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));

        // Spawn Team 0 unit at (20, 18)
        uint32_t friendly_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 18});

        // Issue move order across bomb to (20, 22)
        sim.issue_move_order(friendly_id, TileCoord{20, 22});
        tick_until_paths_delivered(sim, {friendly_id});

        const auto& unit = sim.get_unit(friendly_id);
        ASSERT_FALSE(unit.waypoints.empty());
        // Verify pathfinder routed around (20, 20)
        for (const auto& wp : unit.waypoints) {
            ASSERT_FALSE((wp == TileCoord{20, 20}));
        }

        // Tick simulation until reached destination
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            ASSERT_FALSE((sim.get_unit(friendly_id).pos == TileCoord{20, 20}));
            if (sim.get_unit(friendly_id).pos == TileCoord{20, 22}) {
                break;
            }
        }
        ASSERT_TRUE((sim.get_unit(friendly_id).pos == TileCoord{20, 22}));
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20})); // Friendly bomb remains unexploded

        // A player's move order onto a bomb of its own team ends on it (FUN_010202e7 gets flag 0x20 for every player order; only the path finder goes around):
        // the ant walks onto the bomb and sets it off, which is how a group gets off the island of ISLANDS
        sim.issue_move_order(friendly_id, TileCoord{20, 20});
        ASSERT_TRUE((sim.get_unit(friendly_id).final_dest == TileCoord{20, 20}));
        bool went_off = false;
        for (int t = 0; t < 100 && !went_off; ++t) {
            sim.tick();
            went_off = !sim.has_bomb_at(TileCoord{20, 20});
        }
        ASSERT_TRUE(went_off);
    } TEST_END();

    TEST_CASE("12.12 Enemy Bomb Proximity Detonation & 4-Space Knockback") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 101, 60000);

        // Place Team 0 bomb at (20, 20)
        sim.grid_mut().place_bomb(20, 20, 0);

        // Spawn Team 1 enemy ant at (18, 20)
        uint32_t enemy_id = sim.spawn_unit(1, AntType::Combat, TileCoord{18, 20});
        auto& enemy = sim.get_unit(enemy_id);
        enemy.hp = 5;
        enemy.max_hp = 5;

        // Move to (22, 20) through (20, 20)
        sim.issue_move_order(enemy_id, TileCoord{22, 20});

        bool detonated = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{20, 20})) {
                detonated = true;
                break;
            }
        }
        ASSERT_TRUE(detonated);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombDetonate));

        // Victim took 2 HP damage
        ASSERT_EQ(enemy.hp, 3);
        ASSERT_TRUE(enemy.state == UnitState::Knockback);

        // Step simulation through 10-tick ballistic knockback flight
        for (int t = 0; t < 15; ++t) {
            sim.tick();
            if (enemy.state != UnitState::Knockback) break;
        }

        // Enemy was moving East, so impulse propelled it 4 spaces West to (16, 20)
        ASSERT_EQ(enemy.pos.x, 16);
        ASSERT_EQ(enemy.pos.y, 20);
    } TEST_END();

    TEST_CASE("12.13 Swimmer Bridge Digging Animation & Multi-Stage Progression") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water tile at (20, 20)
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);

        // Swimmer ant at adjacent tile (19, 20)
        uint32_t swimmer_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{19, 20});
        auto& swimmer = sim.get_unit(swimmer_id);

        // Issue BuildBridge order to (20, 20)
        AntOrder order{};
        order.ant_id = swimmer_id;
        order.type = OrderType::BuildBridge;
        order.target_x = 20;
        order.target_y = 20;
        sim.issue_order(order);

        // The unit starts its dig clip on the tile it stands on, facing the bridge, once its one-tile path is delivered
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return swimmer.state == UnitState::BuildingBridge; }) >= 0);
        ASSERT_EQ(swimmer.facing, Direction::East);
        ASSERT_EQ(swimmer.pos, (TileCoord{19, 20}));
        ASSERT_EQ(sim.get_bridge_stage(TileCoord{20, 20}), 1);                     // stage 0x22 at once
        sim.clear_audio_events();

        // Every pass of the dig clip (480 ms on land, cue 81 at 240 ms) adds a stage; after three passes the bridge is done
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return swimmer.state != UnitState::BuildingBridge; }) >= 0);
        ASSERT_TRUE(sim.has_audio_event(81));
        ASSERT_FALSE(sim.has_audio_event(SoundID::ShovelWater));

        // Bridge is now fully constructed (stage 4)
        ASSERT_TRUE(sim.has_bridge_at(TileCoord{20, 20}));
        ASSERT_EQ(sim.get_bridge_stage(TileCoord{20, 20}), 4);
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{20, 20}).has_completed_bridge());

        // Swimmer completed digging and idles on its tile centre
        ASSERT_EQ(swimmer.state, UnitState::Idle);
        ASSERT_EQ(swimmer.pixel_x, 19 * 32 + 16);
        ASSERT_EQ(swimmer.pixel_y, 20 * 32 + 16);

        // Non-swimmer unit can now traverse the completed bridge
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Worker, TileCoord{20, 20}));
    } TEST_END();

    TEST_CASE("12.14 Authentic Bridge Sprite Geometry & Offsets") {
        // Verify authentic dimensions and offsets matching Table 4 animations 34..38
        // Stage 1 (34): bridge1.bmp 16x16 at dx=8, dy=9
        // Stage 2 (35): bridge2.bmp 19x17 at dx=7, dy=7
        // Stage 3 (36): bridge3.bmp 27x24 at dx=3, dy=5
        // Stage 4 (37): bridge4a.bmp 32x32 at dx=0, dy=0
        // Tile 38 (38): bridge4b.bmp 29x30 at dx=2, dy=1
        struct ExpectedBridge {
            uint16_t tile_id;
            const char* name;
            int dx, dy, w, h;
        };
        const ExpectedBridge expected[5] = {
            { TILE_BRIDGE1,  "bridge1.bmp",  8, 9, 16, 16 },
            { TILE_BRIDGE2,  "bridge2.bmp",  7, 7, 19, 17 },
            { TILE_BRIDGE3,  "bridge3.bmp",  3, 5, 27, 24 },
            { TILE_BRIDGE4,  "bridge4a.bmp", 0, 0, 32, 32 },
            { TILE_BRIDGE4B, "bridge4b.bmp", 2, 1, 29, 30 }
        };

        for (const auto& b : expected) {
            int dx = 0, dy = 0, bw = 32, bh = 32;
            switch (b.tile_id) {
                case TILE_BRIDGE1:  dx = 8; dy = 9; bw = 16; bh = 16; break;
                case TILE_BRIDGE2:  dx = 7; dy = 7; bw = 19; bh = 17; break;
                case TILE_BRIDGE3:  dx = 3; dy = 5; bw = 27; bh = 24; break;
                case TILE_BRIDGE4:  dx = 0; dy = 0; bw = 32; bh = 32; break;
                case TILE_BRIDGE4B: dx = 2; dy = 1; bw = 29; bh = 30; break;
                default: break;
            }
            ASSERT_EQ(dx, b.dx);
            ASSERT_EQ(dy, b.dy);
            ASSERT_EQ(bw, b.w);
            ASSERT_EQ(bh, b.h);
        }
    } TEST_END();

    TEST_CASE("12.15 Single Bomber Left-Click and Right-Click Defuse Friendly Bomb") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        ViewportCamera cam;
        cam.world_x = 20 * 32 - 100;
        cam.world_y = 20 * 32 - 100;
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim);       // the cursor asks the simulation whether the tile is a valid special target

        // Place friendly bomb at (20, 20)
        sim.grid_mut().place_bomb(20, 20, 0);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));

        // Spawn friendly bomber at (20, 19)
        uint32_t b_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 19});

        // 1. A single selected bomber (panel 3): a bomb tile is a valid special target (cursor 4), the left click is the special order
        hud.select_ant(b_id, false);
        ASSERT_EQ(hud.get_selected_ant_id(), b_id);
        ASSERT_FALSE(hud.is_multi_select());

        // Screen coords for (20, 20)
        int32_t screen_x = HUD::PLAYFIELD_X + (20 * 32 + 16) - cam.world_x;
        int32_t screen_y = HUD::PLAYFIELD_Y + (20 * 32 + 16) - cam.world_y;

        // Left-click on friendly bomb defuses bomb
        hud.handle_mouse_down(screen_x, screen_y, 1, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 1, sim, cam, 0);

        // The defuse clip plays (1100-1140 ms) and the bomb is removed when it ends
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(b_id).state == UnitState::DefusingBomb; }) >= 0);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !sim.has_bomb_at(TileCoord{20, 20}); }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.get_unit(b_id).state == UnitState::Idle; }) >= 0);

        // 2. Right-click also defuses
        sim.grid_mut().place_bomb(20, 20, 0);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));

        hud.handle_mouse_down(screen_x, screen_y, 3, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 3, sim, cam, 0);      // the right button gives its order at the release
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !sim.has_bomb_at(TileCoord{20, 20}); }) >= 0);
    } TEST_END();

    TEST_CASE("12.16 Non-Bomber Moves Directly Onto Friendly Bomb, Explodes & 4-Space Knockback") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 101, 60000);
        ViewportCamera cam;
        cam.world_x = 20 * 32 - 100;
        cam.world_y = 20 * 32 - 100;
        HUD hud;
        hud.init(0);

        // Place friendly bomb at (20, 20)
        sim.grid_mut().place_bomb(20, 20, 0);

        // Spawn friendly worker at (18, 20)
        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{18, 20});
        hud.select_ant(worker_id, false);

        int32_t screen_x = HUD::PLAYFIELD_X + (20 * 32 + 16) - cam.world_x;
        int32_t screen_y = HUD::PLAYFIELD_Y + (20 * 32 + 16) - cam.world_y;

        // Left-click on friendly bomb: instructs worker to hit the bomb!
        hud.handle_mouse_down(screen_x, screen_y, 1, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 1, sim, cam, 0);

        bool detonated = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{20, 20})) {
                detonated = true;
                break;
            }
        }
        ASSERT_TRUE(detonated);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombDetonate));
        auto& worker = sim.get_unit(worker_id);
        ASSERT_EQ(worker.hp, 8); // 10 - 2 = 8 HP
        ASSERT_TRUE(worker.state == UnitState::Knockback);

        // Step through knockback flight
        for (int t = 0; t < 15; ++t) {
            sim.tick();
            if (worker.state != UnitState::Knockback) break;
        }

        // Propelled 4 spaces West to (16, 20)
        ASSERT_EQ(worker.pos.x, 16);
        ASSERT_EQ(worker.pos.y, 20);
    } TEST_END();

    TEST_CASE("12.17 Several Selected Ants Move Onto A Friendly Bomb To Hit It (panel 4: cursor 3); The Order Of A Right Click Is A Move Too") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        ViewportCamera cam;
        cam.world_x = 20 * 32 - 100;
        cam.world_y = 20 * 32 - 100;
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim);

        // Case A: a bomber and a worker selected (panel 4, mixed types): the bomb tile is a plain move target, the bomber hits the bomb
        sim.grid_mut().place_bomb(20, 20, 0);
        uint32_t b1 = sim.spawn_unit(0, AntType::Bomber, TileCoord{18, 20});
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{16, 20});
        hud.set_selected_ant_ids({b1, w1});
        ASSERT_TRUE(hud.is_multi_select());

        int32_t screen_x = HUD::PLAYFIELD_X + (20 * 32 + 16) - cam.world_x;
        int32_t screen_y = HUD::PLAYFIELD_Y + (20 * 32 + 16) - cam.world_y;
        ASSERT_EQ(hud.evaluate_cursor(screen_x, screen_y, sim.get_world_state(), sim.grid(), cam), CursorType::Move);

        hud.handle_mouse_down(screen_x, screen_y, 1, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 1, sim, cam, 0);

        bool detonated = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{20, 20})) {
                detonated = true;
                break;
            }
        }
        ASSERT_TRUE(detonated);
        ASSERT_EQ(sim.get_unit(b1).hp, 8);

        // Case B: the same selection, a right click on a bomb: panel 4 gives a move, the bomber hits the bomb
        sim.grid_mut().place_bomb(30, 30, 0);
        uint32_t b2 = sim.spawn_unit(0, AntType::Bomber, TileCoord{28, 30});
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{28, 31});
        hud.set_selected_ant_ids({b2, w2});
        ASSERT_TRUE(hud.is_multi_select());

        cam.world_x = 30 * 32 - 100;
        cam.world_y = 30 * 32 - 100;
        int32_t screen_x2 = HUD::PLAYFIELD_X + (30 * 32 + 16) - cam.world_x;
        int32_t screen_y2 = HUD::PLAYFIELD_Y + (30 * 32 + 16) - cam.world_y;

        hud.handle_mouse_down(screen_x2, screen_y2, 3, sim, cam, 0); // Right click on bomb
        hud.handle_mouse_up(screen_x2, screen_y2, 3, sim, cam, 0);

        bool detonated2 = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{30, 30})) {
                detonated2 = true;
                break;
            }
        }
        ASSERT_TRUE(detonated2);
    } TEST_END();

    TEST_CASE("12.18 Swimmer Ant Snorkel Animation Advances in Water") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place water tile at (20, 20)
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);

        uint32_t swimmer_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{20, 20});
        auto& swimmer = sim.get_unit(swimmer_id);
        ASSERT_EQ(swimmer.state, UnitState::Swimming);
        ASSERT_TRUE(swimmer.in_water);

        uint16_t initial_tick = swimmer.anim_tick;
        uint16_t initial_subitem = swimmer.anim_subitem;

        for (int t = 0; t < 10; ++t) {
            sim.tick();
        }

        // Snorkel animation advances smoothly each tick (anim_tick and anim_subitem increment)
        ASSERT_EQ(swimmer.anim_tick, initial_tick + 10);
        ASSERT_EQ(swimmer.anim_subitem, initial_subitem + 10);
    } TEST_END();

    TEST_CASE("12.19 Single-Tile Bridge Diagonal Pathfinding Traversability") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Build a water canal around a single bridge at (20, 20)
        // Land at (19, 19) and (21, 21), water at (19, 20) and (20, 19)
        sim.grid_mut().set_terrain(19, 20, TERRAIN_WATER);
        sim.grid_mut().set_terrain(20, 19, TERRAIN_WATER);
        sim.grid_mut().set_terrain(20, 21, TERRAIN_WATER);
        sim.grid_mut().set_terrain(21, 20, TERRAIN_WATER);

        // Single bridge completed at (20, 20)
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);
        for (int s = 0; s < 4; ++s) {
            sim.grid_mut().advance_bridge(20, 20, 0);
        }
        ASSERT_TRUE(sim.grid().get_cell(20, 20).has_completed_bridge());

        // Worker on land at (19, 19) pathfinding diagonally across the bridge to (21, 21)
        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 19});
        sim.issue_move_order(worker_id, TileCoord{21, 21});
        tick_until_paths_delivered(sim, {worker_id});

        auto& worker = sim.get_unit(worker_id);
        ASSERT_FALSE(worker.waypoints.empty());

        // Step simulation until worker arrives at (21, 21)
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (worker.pos == TileCoord{21, 21}) break;
        }
        ASSERT_EQ(worker.pos.x, 21);
        ASSERT_EQ(worker.pos.y, 21);
    } TEST_END();

    TEST_CASE("12.20 Swimmer Ant Bridge Demolition Multi-Stage Removal") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Bridge at (20, 20)
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);
        for (int s = 0; s < 4; ++s) {
            sim.grid_mut().advance_bridge(20, 20, 0);
        }
        ASSERT_TRUE(sim.grid().get_cell(20, 20).has_completed_bridge());
        ASSERT_EQ(sim.get_bridge_stage(TileCoord{20, 20}), 4);

        // Swimmer at (19, 20)
        uint32_t swimmer_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{19, 20});

        ViewportCamera cam;
        cam.world_x = 20 * 32 - 100;
        cam.world_y = 20 * 32 - 100;
        HUD hud;
        hud.init(0);
        hud.select_ant(swimmer_id, false);

        int32_t screen_x = HUD::PLAYFIELD_X + (20 * 32 + 16) - cam.world_x;
        int32_t screen_y = HUD::PLAYFIELD_Y + (20 * 32 + 16) - cam.world_y;

        // Right-click on existing bridge dispatches DemolishBridge
        hud.handle_mouse_down(screen_x, screen_y, 3, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 3, sim, cam, 0);

        // Step simulation while swimmer demolishes the bridge through stages 3, 2, 1 to 0 (water)
        for (int t = 0; t < 150; ++t) {
            sim.tick();
            if (!sim.has_bridge_at(TileCoord{20, 20})) break;
        }

        // Bridge is completely demolished back to water
        ASSERT_FALSE(sim.has_bridge_at(TileCoord{20, 20}));
        ASSERT_FALSE(sim.grid().get_cell(20, 20).has_completed_bridge());
        ASSERT_FALSE(sim.grid().get_cell(20, 20).has_partial_bridge());
        ASSERT_TRUE(sim.grid().get_cell(20, 20).terrain_type == TERRAIN_WATER);

        // Worker can no longer traverse (water is impassable for worker)
        ASSERT_FALSE(sim.can_unit_traverse(AntType::Worker, TileCoord{20, 20}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.21: Power-Up Arrival Timing & Transformation Lifecycle
    // ------------------------------------------------------------------------
    TEST_CASE("12.21 Power-Up Arrival Timing & getpow Clip Lifecycle (taken when the walk lands on the tile, type at once, 840 ms clip)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place powerup at (13, 10) (Type 4 = Combat Ant)
        sim.grid_mut().place_powerup(13, 10, 4);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));

        // Worker ant at (10, 10)
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& ant = sim.get_unit(ant_id);
        ASSERT_EQ(ant.type, AntType::Worker);
        ASSERT_FALSE(picking_up(ant));

        // Order worker to move to power-up tile (13, 10)
        sim.issue_move_order(ant_id, TileCoord{13, 10});

        // While walking towards (13, 10) nothing is taken from a distance: at (12, 10) the power-up is still there
        bool saw_adjacent_not_triggered = false;
        while (ant.pos.x < 13) {
            sim.tick();
            if (ant.pos.x == 12) {
                saw_adjacent_not_triggered = true;
                ASSERT_FALSE(picking_up(ant));
                ASSERT_EQ(ant.type, AntType::Worker);
                ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));
            }
        }
        ASSERT_TRUE(saw_adjacent_not_triggered);

        // The ant has crossed into the tile but its walk has not landed: still nothing taken
        ASSERT_FALSE(picking_up(ant));
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));
        ASSERT_EQ(ant.type, AntType::Worker);

        // The landing frame takes the power-up in the very same tick: the type changes, the clip plays, the tile is empty
        while (ant.is_alive() && !picking_up(ant)) {
            sim.tick();
        }
        ASSERT_EQ(ant.pos, (TileCoord{13, 10}));
        ASSERT_EQ(ant.type, AntType::Combat);
        ASSERT_EQ(ant.hp, 10u);
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{13, 10}));
        ASSERT_TRUE(ant.waypoints.empty());

        // In the world snapshot the running clip is getpow (CHD animation 55) in the power-up state
        const auto& ws = sim.get_world_state();
        const AntSnapshot* snap = nullptr;
        for (const auto& a : ws.ants) {
            if (a.id == ant_id) { snap = &a; break; }
        }
        ASSERT_TRUE(snap != nullptr);
        ASSERT_EQ(snap->state, UnitState::PoweringUp);
        ASSERT_EQ(snap->loco_clip, 55);

        // The clip ends 840 ms after the snap (16 ticks: still playing, the 17th ends it)
        run_ms(sim, 800);
        ASSERT_TRUE(picking_up(ant));
        run_ms(sim, 100);
        ASSERT_FALSE(picking_up(ant));

        // Emerges idle as a Combat ant; the hit points stay at 10 (the maximum is 10 for every type)
        ASSERT_EQ(ant.type, AntType::Combat);
        ASSERT_EQ(ant.state, UnitState::GuardIdle);
        ASSERT_EQ(ant.max_hp, 10u);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.22: Power-Up Transformation Interruption via Impossible Order
    // ------------------------------------------------------------------------
    TEST_CASE("12.22 Uninterruptible Power-Up Pick-Up Parity (orders are ignored for the whole getpow clip)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place powerup at (10, 10) (Type 4 = Combat)
        sim.grid_mut().place_powerup(10, 10, 4);

        // Worker ant at (9, 10) walks onto it
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{9, 10});
        auto& ant = sim.get_unit(ant_id);
        sim.issue_move_order(ant_id, TileCoord{10, 10});
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return picking_up(ant); }) >= 0);
        ASSERT_EQ(ant.type, AntType::Combat);
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{10, 10}));

        // While the clip plays, the player attempts to move the ant away to (11, 10)
        AntOrder move_order;
        move_order.ant_id = ant_id;
        move_order.type = OrderType::Move;
        move_order.target_x = 11;
        move_order.target_y = 10;
        sim.issue_order(move_order);

        // Order is silently ignored: no snap, no path, no can't-go
        ASSERT_TRUE(picking_up(ant));
        ASSERT_EQ(ant.pos, (TileCoord{10, 10}));
        ASSERT_FALSE(sim.has_pending_path(ant_id));

        // Advance through the remaining getpow clip (16 ticks in all are still refused)
        run_ms(sim, 800);
        ASSERT_TRUE(picking_up(ant));
        sim.issue_order(move_order);
        ASSERT_FALSE(sim.has_pending_path(ant_id));
        run_ms(sim, 100);

        // Ant has completed the clip and is a Combat Ant
        ASSERT_FALSE(picking_up(ant));
        ASSERT_EQ(ant.type, AntType::Combat);
        ASSERT_EQ(ant.pos, (TileCoord{10, 10}));

        // Once the clip is over, issuing the move order lets the unit walk normally
        sim.issue_order(move_order);
        ASSERT_EQ(ant.state, UnitState::Walking);

        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (ant.pos == TileCoord{11, 10} && ant.state != UnitState::Walking) break;
        }
        ASSERT_EQ(ant.pos, (TileCoord{11, 10}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.23: Unattackability While Standing on Power-Up
    // ------------------------------------------------------------------------
    TEST_CASE("12.23 An Ant Standing On A Power-Up Is Immune From Attacks (no path can end on the solid power-up tile)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        make_water_island(sim, 30, 30);

        // Place powerup at (10, 10)
        sim.grid_mut().place_powerup(10, 10, 4);

        // Friendly worker walks onto (10, 10) and gets the can't-go order in the window: it stands on the power-up
        uint32_t friendly_id = sim.spawn_unit(0, AntType::Worker, TileCoord{9, 10});
        ASSERT_TRUE(stand_on_powerup(sim, friendly_id, TileCoord{10, 10}, TileCoord{30, 30}));

        auto& friendly = sim.get_unit(friendly_id);
        ASSERT_EQ(friendly.type, AntType::Worker);
        ASSERT_TRUE(sim.grid().has_powerup_at(friendly.pos));
        uint16_t initial_hp = friendly.hp;

        // Enemy Combat ant four tiles away
        uint32_t enemy_id = sim.spawn_unit(1, AntType::Combat, TileCoord{14, 10});
        auto& enemy = sim.get_unit(enemy_id);

        // The HUD accepts the click as an attack order (order 3), but no path can end on the power-up tile
        // (A* step cost and CanEnter block it): the path manager answers "Can't go there." and the ant plays can't go
        HUD hud;
        hud.init(1); // Player 1 (enemy team)
        hud.select_ant(enemy_id, false);
        hud.dispatch_attack_order(friendly_id, sim);
        ASSERT_EQ(enemy.orig_order, AntUnit::kOrderAttack);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.has_news_event(1, 0x3A); }) >= 0);   // "Can't go there."
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return enemy.loco_action == AntUnit::kActionCantGo; }) >= 0);
        run_ms(sim, 5000);
        ASSERT_EQ(friendly.hp, initial_hp);
        ASSERT_EQ(friendly.pos, (TileCoord{10, 10}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.24: Swimmer Bridge Protection During Digging and Demolishing
    // ------------------------------------------------------------------------
    TEST_CASE("12.24 Swimmer Bridge Protection During Digging and Demolishing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);

        // Swimmer at (19, 20)
        uint32_t swimmer_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{19, 20});
        auto& swimmer = sim.get_unit(swimmer_id);

        // Start bridge building step towards (20, 20)
        ASSERT_TRUE(sim.build_bridge_step(swimmer_id, TileCoord{20, 20}));
        ASSERT_EQ(swimmer.state, UnitState::BuildingBridge);

        // Try to interrupt swimmer with move order
        sim.issue_move_order(swimmer_id, TileCoord{18, 20});
        // State remains BuildingBridge (cannot be interrupted)
        ASSERT_EQ(swimmer.state, UnitState::BuildingBridge);

        // Try to interrupt via generic order
        AntOrder move_order;
        move_order.ant_id = swimmer_id;
        move_order.type = OrderType::Move;
        move_order.target_x = 18;
        move_order.target_y = 20;
        sim.issue_order(move_order);
        ASSERT_EQ(swimmer.state, UnitState::BuildingBridge);

        // Step simulation until bridge building completes
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (swimmer.state != UnitState::BuildingBridge) break;
        }
        ASSERT_NE(swimmer.state, UnitState::BuildingBridge);
        ASSERT_TRUE(sim.grid().get_cell(20, 20).has_completed_bridge());

        // Start demolish step
        ASSERT_TRUE(sim.demolish_bridge_step(swimmer_id, TileCoord{20, 20}));
        ASSERT_EQ(swimmer.state, UnitState::DemolishingBridge);

        // Try to interrupt demolish with move order
        sim.issue_move_order(swimmer_id, TileCoord{18, 20});
        ASSERT_EQ(swimmer.state, UnitState::DemolishingBridge);

        // Step simulation until demolish action finishes
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (swimmer.state != UnitState::DemolishingBridge) break;
        }
        ASSERT_NE(swimmer.state, UnitState::DemolishingBridge);
        ASSERT_TRUE(sim.grid().get_cell(20, 20).terrain_type == TERRAIN_WATER);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.25: Bridge Locomotion Speed Matches Mud Speed
    // ------------------------------------------------------------------------
    TEST_CASE("12.25 Bridge Locomotion Speed Matches Mud Speed (bridges are terrain class 3)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Three walled corridors (rows 10, 12, 14) so that every path is a straight line east:
        // row 10 grass, row 12 mud, row 14 completed bridges over water.
        for (int x = 8; x <= 18; ++x) {
            for (int y : {9, 11, 13, 15}) sim.grid_mut().set_terrain(x, y, TERRAIN_OBSTACLE);
        }
        for (int x = 10; x <= 16; ++x) {
            sim.grid_mut().get_cell_mut(TileCoord{x, 12}).surface_type = SurfaceType::Mud;
            sim.grid_mut().set_terrain(x, 14, TERRAIN_WATER);
            uint32_t ux = static_cast<uint32_t>(x);
            for (int s = 0; s < 4; ++s) {
                sim.grid_mut().advance_bridge(ux, 14, 0);
            }
            ASSERT_TRUE(sim.grid().get_cell(ux, 14).has_completed_bridge());
        }
        // A bridge piece makes its tile terrain class 3, mud (Ants.exe FUN_01008af7)
        ASSERT_EQ(sim.grid().terrain_class_at(TileCoord{12, 14}), sim.grid().terrain_class_at(TileCoord{12, 12}));

        uint32_t w_grass = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t w_mud = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 12});
        uint32_t w_bridge = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 14});
        sim.set_locomotion_trace_enabled(true);

        sim.issue_move_order(w_grass, TileCoord{15, 10});
        sim.issue_move_order(w_mud, TileCoord{15, 12});
        sim.issue_move_order(w_bridge, TileCoord{15, 14});
        for (int t = 1; t <= 300; ++t) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(w_grass).pos, (TileCoord{15, 10}));
        ASSERT_EQ(sim.get_unit(w_mud).pos, (TileCoord{15, 12}));
        ASSERT_EQ(sim.get_unit(w_bridge).pos, (TileCoord{15, 14}));

        // Walk times from path delivery to arrival (reference model of Ants.exe): 5 tiles of grass
        // (4 px per 50 ms) take 2200 ms, 5 tiles of mud (2 px per 60 ms) 4710 ms; bridges walk like mud.
        ASSERT_EQ(walk_duration_ms(sim, w_grass), 2200);
        ASSERT_EQ(walk_duration_ms(sim, w_mud), 4710);
        ASSERT_EQ(walk_duration_ms(sim, w_bridge), walk_duration_ms(sim, w_mud));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.26: Swimmers Immune From Being Attacked Underwater
    // ------------------------------------------------------------------------
    TEST_CASE("12.26 An Ant In Water Cannot Be Attacked And Nothing Attacks From Water (CanBeAttackedFrom)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water pond around (20, 20)
        for (int y = 19; y <= 21; ++y) {
            for (int x = 20; x <= 22; ++x) {
                sim.grid_mut().set_terrain(x, y, TERRAIN_WATER);
            }
        }

        // Friendly swimmer in water at (20, 20)
        uint32_t friendly_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{20, 20});
        auto& friendly = sim.get_unit(friendly_id);
        uint16_t initial_hp = friendly.hp;

        // Enemy swimmer in water at (21, 20) (adjacent in the water)
        uint32_t enemy_swimmer_id = sim.spawn_unit(1, AntType::Swimmer, TileCoord{21, 20});

        // Enemy Combat ant on land at (19, 20) (adjacent on the bank)
        uint32_t enemy_combat_id = sim.spawn_unit(1, AntType::Combat, TileCoord{19, 20});

        // 1. A melee from the land against the swimmer in the water is refused ("Can't go there.")
        sim.execute_melee_attack(enemy_combat_id, friendly_id);
        ASSERT_EQ(friendly.hp, initial_hp);
        ASSERT_EQ(sim.get_unit(enemy_combat_id).loco_action, AntUnit::kActionCantGo);

        // 2. A melee between two ants in the water is refused as well
        sim.execute_melee_attack(enemy_swimmer_id, friendly_id);
        ASSERT_EQ(friendly.hp, initial_hp);

        // 3. The auto-engage of the combat ant ignores the swimmer in the water for a long time
        for (int i = 0; i < 100; ++i) sim.tick();
        ASSERT_EQ(friendly.hp, initial_hp);

        // 4. When the swimmer stands on land it can be attacked normally
        SimulationEngine sim2;
        sim2.init_test_world(60, 60, 100, 60000);
        uint32_t swimmer_land = sim2.spawn_unit(0, AntType::Swimmer, TileCoord{18, 20});
        uint32_t combat_land = sim2.spawn_unit(1, AntType::Combat, TileCoord{19, 20});
        sim2.execute_melee_attack(combat_land, swimmer_land);
        ASSERT_EQ(sim2.get_unit(swimmer_land).hp, initial_hp - 2); // Takes damage on land!
    } TEST_END();

    TEST_CASE("12.27 Power-Up Obstacle Avoidance (Walks Around Power-Up Unless Directly Instructed)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place a Combat power-up at (13, 10)
        sim.grid_mut().place_powerup(13, 10, 4);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));

        // Spawn a Worker ant at (10, 10)
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& ant = sim.get_unit(ant_id);
        ASSERT_EQ(ant.type, AntType::Worker);

        // 1. Order ant to move PAST the powerup to (16, 10)
        sim.issue_move_order(ant_id, TileCoord{16, 10});

        // The path must NOT contain (13, 10) - it must route around the power-up tile
        for (const auto& wp : ant.waypoints) {
            ASSERT_FALSE(wp == (TileCoord{13, 10}));
        }

        // Step simulation until the ant reaches (16, 10)
        int steps = 0;
        while (ant.pos != (TileCoord{16, 10}) && steps++ < 200) {
            sim.tick();
            // At no point during travel should the ant ever be on (13, 10)
            ASSERT_FALSE(ant.pos == (TileCoord{13, 10}));
        }
        ASSERT_EQ(ant.pos, (TileCoord{16, 10}));

        // The powerup at (13, 10) must remain intact, and ant must NOT have transformed
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));
        ASSERT_EQ(ant.type, AntType::Worker);
        ASSERT_FALSE(picking_up(ant));

        // 2. Now specifically instruct the ant to walk onto the powerup at (13, 10)
        sim.issue_move_order(ant_id, TileCoord{13, 10});

        // This time, the destination IS the powerup, so the ant routes directly to it
        ASSERT_EQ(ant.final_dest, (TileCoord{13, 10}));

        // Step simulation until the ant lands on it and takes it
        steps = 0;
        while (!picking_up(ant) && steps++ < 150) {
            sim.tick();
        }

        ASSERT_EQ(ant.pos, (TileCoord{13, 10}));
        ASSERT_TRUE(picking_up(ant));
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{13, 10}));

        // Finish the 11-frame getpow clip (840 ms after the snap)
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(ant));
        ASSERT_EQ(ant.type, AntType::Combat); // Successfully transformed into Combat Ant!

        // 3. Place another power-up at (13, 12). Order ant from (13, 10) to move past it to (13, 14)
        sim.grid_mut().place_powerup(13, 12, 1); // Bomber powerup
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 12}));

        sim.issue_move_order(ant_id, TileCoord{13, 14});
        // Waypoints must avoid (13, 12)
        for (const auto& wp : ant.waypoints) {
            ASSERT_FALSE(wp == (TileCoord{13, 12}));
        }

        steps = 0;
        while (ant.pos != (TileCoord{13, 14}) && steps++ < 200) {
            sim.tick();
            ASSERT_FALSE(ant.pos == (TileCoord{13, 12}));
        }
        ASSERT_EQ(ant.pos, (TileCoord{13, 14}));
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 12})); // Second powerup also untouched!
        ASSERT_EQ(ant.type, AntType::Combat);
    } TEST_END();

    TEST_CASE("12.28 Enemy Ant Selection & Friendly Fire Command Rejection") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Player 0 (Blue) ants: b1 at (10, 10), b2 at (10, 11)
        uint32_t b1 = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        uint32_t b2 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 11});

        // Player 1 (Green) ant: g1 at (8, 8)
        uint32_t g1 = sim.spawn_unit(1, AntType::Combat, TileCoord{8, 8});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(1); // Local player is Green (Player 1)

        // 1. Green player clicks on Blue Ant 1 at (10, 10): the pick rectangle of the original (FUN_01026a39) is the sprite box
        //    around the ant (combat ant [x-32, x+26) x [y-46, y+16)); the ant below it (10, 11) has the box [y-32, y+16) and the
        //    last hit wins, so the click goes 12 px above the anchor (336, 324), where only b1's box is
        int32_t b1_click_x = 336 + HUD::PLAYFIELD_X;
        int32_t b1_click_y = 324 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(b1_click_x, b1_click_y, 1, sim, camera);
        hud.handle_mouse_up(b1_click_x, b1_click_y, 1, sim, camera);

        // Enemy ant b1 CAN now be selected for inspection if no friendly units selected!
        ASSERT_TRUE(hud.is_ant_selected(b1));
        ASSERT_EQ(hud.get_selected_ant_id(), b1);

        // But ground clicks must NOT move or command enemy ants!
        int32_t ground_x = 350 + HUD::PLAYFIELD_X;
        int32_t ground_y = 350 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(ground_x, ground_y, 1, sim, camera);
        hud.handle_mouse_up(ground_x, ground_y, 1, sim, camera);
        ASSERT_NE(sim.get_unit(b1).state, UnitState::Walking);

        // 2. Green player clicks on Blue Ant 2 at (10, 11) -> pixel (10*32+16, 11*32+16) = (336, 368)
        int32_t b2_click_x = 336 + HUD::PLAYFIELD_X;
        int32_t b2_click_y = 368 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(b2_click_x, b2_click_y, 1, sim, camera);
        hud.handle_mouse_up(b2_click_x, b2_click_y, 1, sim, camera);

        // b2 is now selected for inspection, neither unit has an attack order
        ASSERT_TRUE(hud.is_ant_selected(b2));
        ASSERT_FALSE(hud.is_ant_selected(b1));
        ASSERT_NE(sim.get_unit(b1).orig_order, AntUnit::kOrderAttack);
        ASSERT_NE(sim.get_unit(b2).orig_order, AntUnit::kOrderAttack);

        // 3. Right-clicking b2 while nothing or enemy is selected does not issue attack orders
        hud.handle_mouse_down(b2_click_x, b2_click_y, 3, sim, camera);
        ASSERT_NE(sim.get_unit(b1).orig_order, AntUnit::kOrderAttack);

        // 4. Directly testing HUD attack order dispatch protection:
        // Even if an enemy ant ID were artificially injected into selected_ant_ids:
        hud.set_selected_ant_ids({b1});
        hud.dispatch_attack_order(b2, sim);
        // HUD must filter out non-friendly units so b1 NEVER targets b2!
        ASSERT_NE(sim.get_unit(b1).orig_order, AntUnit::kOrderAttack);

        // 5. SimulationEngine layer protection:
        // Even if a malformed AntOrder with b1 attacking b2 is fed to sim.issue_order:
        AntOrder bad_order;
        bad_order.ant_id = b1;
        bad_order.type = OrderType::Attack;
        bad_order.target_entity_id = static_cast<int32_t>(b2);
        sim.issue_order(bad_order);
        ASSERT_NE(sim.get_unit(b1).orig_order, AntUnit::kOrderAttack);

        // 6. execute_melee_attack deals 0 damage to teammates:
        uint32_t b2_initial_hp = sim.get_unit(b2).hp;
        sim.execute_melee_attack(b1, b2);
        ASSERT_EQ(sim.get_unit(b2).hp, b2_initial_hp); // No damage dealt!

        // 7. Legitimate Green ant g1 selecting and attacking b1 works as expected:
        int32_t g1_click_x = 8 * 32 + 16 + HUD::PLAYFIELD_X;
        int32_t g1_click_y = 8 * 32 + 16 + HUD::PLAYFIELD_Y;
        hud.clear_selection();
        hud.handle_mouse_down(g1_click_x, g1_click_y, 1, sim, camera);
        hud.handle_mouse_up(g1_click_x, g1_click_y, 1, sim, camera);

        ASSERT_TRUE(hud.is_ant_selected(g1));
        ASSERT_EQ(hud.get_selected_ant_id(), g1);

        // Green ant g1 attacks enemy b1
        hud.handle_mouse_down(b1_click_x, b1_click_y, 1, sim, camera);
        hud.handle_mouse_up(b1_click_x, b1_click_y, 1, sim, camera);
        ASSERT_EQ(sim.get_unit(g1).orig_order, AntUnit::kOrderAttack);
    } TEST_END();

    TEST_CASE("12.29 Power-Up Drop On Pick-Up & Occupied Tile Avoidance / Disappearance") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        static constexpr std::array<TileCoord, 8> OFFSETS = {{{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}}};

        // 1. Worker Ant takes the Combat power-up -> nothing is dropped (a worker holds none)
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.grid_mut().place_powerup(10, 10, 4); // Combat powerup
        ASSERT_TRUE(take_powerup_here(sim, a1));
        ASSERT_EQ(sim.get_unit(a1).type, AntType::Combat);
        // Verify none of the 8 neighbors have any powerup
        for (const auto& off : OFFSETS) {
            ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{10 + off.x, 10 + off.y}));
        }
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(sim.get_unit(a1)));
        ASSERT_EQ(sim.get_unit(a1).type, AntType::Combat);

        // 2. Combat Ant takes a Bomber power-up -> drops the original Combat powerup (Type 4) on an adjacent free tile
        sim.grid_mut().place_powerup(10, 10, 1); // Bomber powerup
        ASSERT_TRUE(take_powerup_here(sim, a1));
        // Exactly 1 of the 8 neighbors receives the dropped Combat powerup
        int drop_count = 0;
        TileCoord dropped_tile{-1, -1};
        for (const auto& off : OFFSETS) {
            TileCoord adj{10 + off.x, 10 + off.y};
            if (sim.grid().has_powerup_at(adj)) {
                ++drop_count;
                dropped_tile = adj;
            }
        }
        ASSERT_EQ(drop_count, 1);
        ASSERT_EQ(sim.grid().get_powerup_type(dropped_tile), 4); // Dropped old Combat powerup
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(sim.get_unit(a1)));
        ASSERT_EQ(sim.get_unit(a1).type, AntType::Bomber);

        // 3. Occupied tile avoidance: place an ant on (10, 9), the Bomber takes a Swimmer power-up
        // Clean up any powerup around (10, 10) first to ensure known board state
        for (const auto& off : OFFSETS) {
            sim.grid_mut().clear_powerup(10 + off.x, 10 + off.y);
        }
        uint32_t blocker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 9});
        (void)blocker;
        sim.grid_mut().place_powerup(10, 10, 5); // Swimmer powerup
        ASSERT_TRUE(take_powerup_here(sim, a1));
        // (10, 9) is occupied by blocker ant -> MUST NOT drop at (10, 9)
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{10, 9}));
        // Exactly 1 unoccupied neighbor receives the dropped Bomber powerup (Type 1)
        drop_count = 0;
        dropped_tile = TileCoord{-1, -1};
        for (const auto& off : OFFSETS) {
            TileCoord adj{10 + off.x, 10 + off.y};
            if (sim.grid().has_powerup_at(adj)) {
                ++drop_count;
                dropped_tile = adj;
            }
        }
        ASSERT_EQ(drop_count, 1);
        ASSERT_NE(dropped_tile, (TileCoord{10, 9})); // Blocker was avoided!
        ASSERT_EQ(sim.grid().get_powerup_type(dropped_tile), 1); // Dropped old Bomber powerup
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(sim.get_unit(a1)));
        ASSERT_EQ(sim.get_unit(a1).type, AntType::Swimmer);

        // 4. Surrounded ant: all 8 neighbors blocked / occupied / water -> powerup permanently disappears
        uint32_t a2 = sim.spawn_unit(1, AntType::Combat, TileCoord{20, 20});
        sim.grid_mut().set_terrain(20, 19, TERRAIN_WATER);    // N: Water
        sim.grid_mut().set_terrain(21, 20, TERRAIN_OBSTACLE); // E: Obstacle
        sim.spawn_unit(1, AntType::Worker, TileCoord{20, 21}); // S: Ant
        sim.spawn_unit(1, AntType::Worker, TileCoord{19, 20}); // W: Ant
        sim.spawn_unit(1, AntType::Worker, TileCoord{21, 19}); // NE: Ant
        sim.spawn_unit(1, AntType::Worker, TileCoord{21, 21}); // SE: Ant
        sim.spawn_unit(1, AntType::Worker, TileCoord{19, 21}); // SW: Ant
        sim.spawn_unit(1, AntType::Worker, TileCoord{19, 19}); // NW: Ant

        sim.grid_mut().place_powerup(20, 20, 3); // Thief powerup
        sim.clear_audio_events();
        ASSERT_TRUE(take_powerup_here(sim, a2));
        ASSERT_TRUE(sim.has_audio_event(40));                  // the silent cue of the lost power-up
        // None of the 8 neighbors should receive a dropped powerup
        for (const auto& off : OFFSETS) {
            ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{20 + off.x, 20 + off.y}));
        }
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(sim.get_unit(a2)));
        ASSERT_EQ(sim.get_unit(a2).type, AntType::Thief);
        // Still no powerup on any of the 8 neighbors: permanently disappeared!
        for (const auto& off : OFFSETS) {
            ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{20 + off.x, 20 + off.y}));
        }

        // 5. Uninterruptible pick-up ignores move order and completes
        uint32_t a3 = sim.spawn_unit(0, AntType::Thief, TileCoord{30, 30});
        sim.grid_mut().place_powerup(30, 30, 1); // Bomber powerup
        sim.grid_mut().set_terrain(30, 31, TERRAIN_WATER); // S: Water (for impossible order)
        ASSERT_TRUE(take_powerup_here(sim, a3));
        auto& ant3 = sim.get_unit(a3);
        // Verify dropped Thief powerup (Type 3) landed on a valid neighbor
        dropped_tile = TileCoord{-1, -1};
        for (const auto& off : OFFSETS) {
            TileCoord adj{30 + off.x, 30 + off.y};
            if (sim.grid().has_powerup_at(adj)) dropped_tile = adj;
        }
        ASSERT_TRUE(dropped_tile.x >= 0 && dropped_tile.y >= 0);
        ASSERT_EQ(sim.grid().get_powerup_type(dropped_tile), 3);
        ASSERT_TRUE(dropped_tile != (TileCoord{30, 31}));      // water is never a drop tile

        // Move order issued during the active clip is silently ignored per Ants.exe
        sim.issue_move_order(a3, TileCoord{30, 32});
        ASSERT_TRUE(picking_up(ant3));
        ASSERT_FALSE(sim.has_pending_path(a3));

        // Completes into Bomber (the getpow clip is 840 ms from the snap)
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(ant3));
        ASSERT_EQ(ant3.type, AntType::Bomber);
        ASSERT_TRUE(sim.grid().has_powerup_at(dropped_tile));

        // 6. Multi-direction randomized landing verification:
        // Over multiple pick-ups with all 8 directions free, verify that the dropped powerup lands randomly across
        // multiple distinct directions, NOT always above (the original scan starts at a random row and column).
        std::set<std::pair<int32_t, int32_t>> chosen_directions;
        for (int iter = 0; iter < 24; ++iter) {
            TileCoord origin{5 + (iter % 6) * 8, 40 + (iter / 6) * 4};
            uint32_t tester = sim.spawn_unit(0, AntType::Combat, origin);
            sim.grid_mut().place_powerup(origin.x, origin.y, 1); // Bomber
            ASSERT_TRUE(take_powerup_here(sim, tester));
            int found = 0;
            for (const auto& off : OFFSETS) {
                TileCoord adj{origin.x + off.x, origin.y + off.y};
                if (sim.grid().has_powerup_at(adj)) {
                    chosen_directions.insert({off.x, off.y});
                    ++found;
                }
            }
            ASSERT_EQ(found, 1);
            run_ms(sim, 900);
        }
        // With 8 free directions, 24 trials should sample multiple distinct directions (>= 4)
        ASSERT_TRUE(chosen_directions.size() >= 4);
    } TEST_END();

    TEST_CASE("12.30 Blocked Path Can't Go Reaction, Ability Animations & Drowning Sequence") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Can't Go reaction. Clicking water sends a worker to the first enterable tile of the ring scan
        //    around the clicked tile (FUN_010202e7), without any can't-go; an unreachable land tile makes the
        //    path manager report "Can't go there." and the can't-go animation (action 0xB) plays.
        for (int32_t wx = 11; wx <= 15; ++wx) {
            for (int32_t wy = 8; wy <= 12; ++wy) {
                sim.grid_mut().set_terrain(wx, wy, TERRAIN_WATER);
            }
        }
        uint32_t w_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& w_ant = sim.get_unit(w_id);

        // Order into the water at (12, 10): distance 2 of the ring scan, west column, top first -> (10, 8)
        sim.issue_move_order(w_id, TileCoord{12, 10});
        ASSERT_NE(w_ant.state, UnitState::CantGo);
        ASSERT_EQ(w_ant.final_dest, (TileCoord{10, 8}));
        ASSERT_TRUE(tick_until(sim, [&]() { return w_ant.pos == TileCoord{10, 8} && w_ant.waypoints.empty() &&
                                                  w_ant.state == UnitState::Idle; }, 60));
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));

        // Island: land at (13, 10) in the middle of the water block cannot be reached
        sim.grid_mut().set_terrain(13, 10, TERRAIN_WALKABLE);
        sim.clear_audio_events();
        sim.issue_move_order(w_id, TileCoord{13, 10});
        ASSERT_TRUE(tick_until(sim, [&]() { return w_ant.state == UnitState::CantGo; }, 10));
        ASSERT_EQ(w_ant.pixel_x, 10 * 32 + 16);
        ASSERT_EQ(w_ant.pixel_y, 8 * 32 + 16);
        ASSERT_TRUE(w_ant.waypoints.empty());
        ASSERT_TRUE(sim.has_audio_event(SoundID::CantGo));   // frame 0 of the can't-go animation (sound 63)
        ASSERT_TRUE(sim.has_news_event(0, 0x3A));            // "Can't go there."

        // The worker's can't-go animation (agcg301) runs 6 frames x 60 ms = 360 ms, then the ant idles
        int cant_go_ticks = 0;
        while (w_ant.state == UnitState::CantGo && cant_go_ticks < 40) {
            sim.tick();
            ++cant_go_ticks;
        }
        ASSERT_EQ(w_ant.state, UnitState::Idle);
        ASSERT_EQ(cant_go_ticks, 8);

        // 2. Bomber ability clips (plant and defuse): the world changes when the clip ends
        uint32_t b_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 20});
        auto& b_ant = sim.get_unit(b_id);

        sim.plant_bomb(b_id, TileCoord{21, 20}, false);
        ASSERT_EQ(b_ant.state, UnitState::PlantingBomb);
        ASSERT_EQ(b_ant.facing, Direction::East);
        // Bomb is NOT placed on the grid tile yet while the clip is playing (only the invisible placeholder)
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{21, 20}));
        // absb east lasts 1400 ms; the bomb appears when it ends and the ant is idle again
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.has_bomb_at(TileCoord{21, 20}); }) >= 1350);
        ASSERT_EQ(b_ant.state, UnitState::Idle);

        // Defuse (abdb east, 1140 ms): the bomb is removed when the clip ends
        sim.defuse_bomb(b_id, TileCoord{21, 20}, false);
        ASSERT_EQ(b_ant.state, UnitState::DefusingBomb);
        ASSERT_EQ(b_ant.facing, Direction::East);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !sim.has_bomb_at(TileCoord{21, 20}); }) >= 1100);
        ASSERT_EQ(b_ant.state, UnitState::Idle);

        // 3. Fire Ant ability clips (ignite and extinguish)
        uint32_t f_id = sim.spawn_unit(0, AntType::Fire, TileCoord{25, 25});
        auto& f_ant = sim.get_unit(f_id);

        sim.ignite_fire(f_id, TileCoord{25, 26}, false);
        ASSERT_EQ(f_ant.state, UnitState::PlacingFire);
        ASSERT_EQ(f_ant.facing, Direction::South);
        // Fire is NOT placed on the grid tile yet while the clip is playing!
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{25, 26}).has_fire());

        // afsf south lasts 1760 ms; the fire wall appears when it ends
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.grid().get_cell(TileCoord{25, 26}).has_fire(); }) >= 1700);
        ASSERT_EQ(f_ant.state, UnitState::Idle);

        // Extinguish fire (afxf south, 1200 ms): the wall is put out when the clip ends
        sim.extinguish_fire(f_id, TileCoord{25, 26}, false);
        ASSERT_EQ(f_ant.state, UnitState::ExtinguishingFire);
        ASSERT_EQ(f_ant.facing, Direction::South);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !sim.grid().get_cell(TileCoord{25, 26}).has_fire(); }) >= 1150);
        ASSERT_EQ(f_ant.state, UnitState::Idle);

        // 4. Drowning: the ant plays the drowning clip (action 0xF, 2370 ms) on the tile centre and is removed at its end
        sim.grid_mut().set_terrain(30, 30, TERRAIN_WATER);
        uint32_t drown_id = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
        auto& drown_ant = sim.get_unit(drown_id);

        // Unit enters Drowning state, snapped to the tile centre; the hit points do not change
        ASSERT_EQ(drown_ant.state, UnitState::Drowning);
        ASSERT_EQ(drown_ant.pixel_x, 30 * 32 + 16);
        ASSERT_EQ(drown_ant.pixel_y, 30 * 32 + 16);
        ASSERT_EQ(drown_ant.hp, 10);

        // The clip lasts 2370 ms (about 47 ticks); the ant is removed when it ends
        for (int t = 0; t < 44; ++t) {
            ASSERT_EQ(drown_ant.state, UnitState::Drowning);
            sim.tick();
        }
        ASSERT_TRUE(wait_ms(sim, 500, [&]() { return !drown_ant.is_alive(); }) >= 0);
        ASSERT_EQ(drown_ant.state, UnitState::Dead);
        ASSERT_FALSE(drown_ant.is_alive());

        // 5. Bomb Detonation Visual Effect
        sim.grid_mut().place_bomb(35, 35, 0);
        uint32_t trigger_id = sim.spawn_unit(1, AntType::Combat, TileCoord{35, 36});
        sim.issue_move_order(trigger_id, TileCoord{35, 34});
        for (int t = 0; t < 20; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{35, 35})) break;
        }
        // WorldState includes bombex visual effect
        const auto& snap = sim.get_world_state();
        bool found_bombex = false;
        for (const auto& fx : snap.effects) {
            if (fx.anim_name == "bombex") {
                found_bombex = true;
                break;
            }
        }
        ASSERT_TRUE(found_bombex);
    } TEST_END();

    TEST_CASE("12.31 Base Entry Without Food & Silent Health Visit") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 720000);   // a long match: sound 55 is also the "1 minute left" cue
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* home = sim.grid().find_anthill(0);
        ASSERT_TRUE(home != nullptr);
        int32_t bx = home->x;
        int32_t by = home->y;

        // Spawn unit with full HP and 0 food near the ramp entrance
        uint32_t aid = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        const auto& u = sim.get_unit(aid);
        ASSERT_FALSE(u.is_holding());
        ASSERT_EQ(u.hp, u.max_hp);

        // Issue ReturnToBase order
        AntOrder order;
        order.ant_id = aid;
        order.type = OrderType::ReturnToBase;
        sim.issue_order(order);

        // Tick simulation until the ant climbs the ramp and enters the base hole
        bool entered_base = false;
        for (int i = 0; i < 80; ++i) {
            sim.tick();
            if (u.state == UnitState::EnteringBase) {
                entered_base = true;
                break;
            }
        }
        ASSERT_TRUE(entered_base);

        // Advance through base entry animation (16 ticks)
        for (int i = 0; i < 20; ++i) {
            sim.tick();
        }

        // Verify that NO SoundID::BaseEnter or score audio was played during empty base entry
        ASSERT_FALSE(sim.has_audio_event(SoundID::BaseEnter));
        ASSERT_FALSE(sim.has_audio_event(SoundID::BaseScoreUp));

        // Tick until ant emerges and walks off the ramp to idle spot (bx+4, by+4)
        for (int i = 0; i < 140; ++i) {
            sim.tick();
            if (u.state == UnitState::Idle && u.pos != TileCoord{bx + 1, by + 1}) {
                break;
            }
        }
        ASSERT_EQ(u.state, UnitState::Idle);
        ASSERT_EQ(u.pos, (TileCoord{bx + 4, by + 4}));
    } TEST_END();

    TEST_CASE("12.32 Move Order From Anthill Hole Steps Down Ramp Without CantGo") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* home = sim.grid().find_anthill(0);
        ASSERT_TRUE(home != nullptr);
        int32_t bx = home->x;
        int32_t by = home->y;

        // Place ant directly on the anthill entrance hole
        uint32_t aid = sim.spawn_unit(0, AntType::Worker, TileCoord{bx + 1, by + 1});
        const auto& u = sim.get_unit(aid);
        ASSERT_EQ(u.pos, (TileCoord{bx + 1, by + 1}));

        // Issue move order to open ground outside the anthill
        TileCoord target{bx - 5, by + 3};
        sim.issue_move_order(aid, target);

        // Must NOT trigger CantGo! Must transition to Walking
        ASSERT_NE(u.state, UnitState::CantGo);
        ASSERT_EQ(u.state, UnitState::Walking);

        // Step simulation until arrival and transition to Idle
        for (int i = 0; i < 150; ++i) {
            sim.tick();
            if (u.pos == target && u.state == UnitState::Idle) break;
        }
        ASSERT_EQ(u.pos, target);
        ASSERT_EQ(u.state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("12.33 Planted Bomb and Fire Wall Object Animation Frames") {
        // 1. Bomb frame alternation (Anim 129..132, 2 frames alternating every 100ms / 6 ticks)
        static const char* const bomb_frames[4][2] = {
            { "1bombgrn.bmp", "2bombgrn.bmp" },
            { "1bombred.bmp", "2bombred.bmp" },
            { "1bombblu.bmp", "2bombblu.bmp" },
            { "1bombblk.bmp", "2bombblk.bmp" }
        };
        for (uint8_t team = 0; team < 4; ++team) {
            size_t f0 = 0;
            size_t f1 = 1;
            ASSERT_TRUE(std::string(bomb_frames[team][f0]).rfind("1bomb", 0) == 0);
            ASSERT_TRUE(std::string(bomb_frames[team][f1]).rfind("2bomb", 0) == 0);
        }

        // 2. Fire Wall 5-frame sequence (Anim 134 wallup04)
        static const char* const fire_names[5] = {
            "9fire01.bmp", "9fire02.bmp", "9fire03.bmp", "9fire04.bmp", "9fire05.bmp"
        };
        for (size_t i = 0; i < 5; ++i) {
            std::string expected = "9fire0" + std::to_string(i + 1) + ".bmp";
            ASSERT_EQ(std::string(fire_names[i]), expected);
        }
    } TEST_END();

    TEST_CASE("12.34 Knockback Flies Over Intermediate Rock Obstacle to Available Ground Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place a solid obstacle rock at (22, 20): only the landing tile is tested, not the path of the flight
        sim.grid_mut().set_terrain(22, 20, TERRAIN_OBSTACLE);

        // Spawn worker at (20, 20) and apply 4-tile knockback East towards (24, 20)
        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        auto& worker = sim.get_unit(worker_id);

        sim.apply_knockback(worker_id, 19 * 32 + 16, 20 * 32 + 16, 4, 4);
        ASSERT_EQ(worker.state, UnitState::Knockback);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return worker.state != UnitState::Knockback; }) >= 0);

        // Flew over rock at (22, 20) and landed at ground tile (24, 20); a melee flight leaves no stun
        ASSERT_EQ(worker.pos.x, 24);
        ASSERT_EQ(worker.pos.y, 20);
        ASSERT_EQ(worker.state, UnitState::Idle);
        ASSERT_FALSE(worker.is_stunned());
    } TEST_END();

    TEST_CASE("12.35 Knockback Flies Over Intermediate Water to Available Ground Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water spans 3 spaces at (21, 20), (22, 20), (23, 20)
        sim.grid_mut().set_terrain(21, 20, TERRAIN_WATER);
        sim.grid_mut().set_terrain(22, 20, TERRAIN_WATER);
        sim.grid_mut().set_terrain(23, 20, TERRAIN_WATER);

        // Spawn worker at (20, 20) and knock East 4 tiles to ground at (24, 20)
        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        auto& worker = sim.get_unit(worker_id);

        sim.apply_knockback(worker_id, 19 * 32 + 16, 20 * 32 + 16, 4, 4);
        ASSERT_EQ(worker.state, UnitState::Knockback);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return worker.state != UnitState::Knockback; }) >= 0);

        // Airborne ant flew over water without drowning, landed safely on ground
        ASSERT_EQ(worker.pos.x, 24);
        ASSERT_EQ(worker.pos.y, 20);
        ASSERT_EQ(worker.state, UnitState::Idle);
        ASSERT_FALSE(worker.in_water);
        ASSERT_EQ(worker.death_status, DeathStatus::Alive);
    } TEST_END();

    TEST_CASE("12.36 Knockback Flies Over Intermediate Rock Obstacle into Water (Drowning)") {
        // A bomb victim is thrown 4 tiles opposite its facing (a dud, 20 %, does not throw it: try seeds for a blast)
        bool tested = false;
        for (uint32_t seed = 101; seed < 160 && !tested; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 60000);

            // Intermediate rock at (22, 20), water at landing tile (24, 20)
            sim.grid_mut().set_terrain(22, 20, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(24, 20, TERRAIN_WATER);

            // Plant enemy bomb at (20, 20)
            sim.grid_mut().place_bomb(20, 20, 1);

            // Spawn worker at (21, 20) and order movement West onto bomb at (20, 20)
            uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 20});
            auto& worker = sim.get_unit(worker_id);

            AntOrder move_order{};
            move_order.ant_id = worker_id;
            move_order.type = OrderType::Move;
            move_order.target_x = 20;
            move_order.target_y = 20;
            sim.issue_order(move_order);

            // Advance until detonation (the ant ends its path on the bomb tile)
            ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return !sim.has_bomb_at(TileCoord{20, 20}); }) >= 0);
            if (worker.knock_flag) continue;   // a dud
            tested = true;
            ASSERT_EQ(worker.state, UnitState::Knockback);

            // Step through the flight: it lands in the water and the ant drowns
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return worker.state == UnitState::Drowning; }) >= 0);

            // Flew over rock at (22, 20), landed at (24, 20) in water
            ASSERT_EQ(worker.pos.x, 24);
            ASSERT_EQ(worker.pos.y, 20);
            ASSERT_EQ(worker.hp, 8);   // the two hits of the bomb; the drowning takes none
            run_ms(sim, 250);
            ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
            ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !worker.is_alive(); }) >= 0);
            ASSERT_EQ(worker.death_status, DeathStatus::Drowned);
        }
        ASSERT_TRUE(tested);
    } TEST_END();

    TEST_CASE("12.37 Knockback Deflects To The Next Free Direction When The Landing Tile Is A Rock (dir, +1, -1, +2, -2)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Solid rock wall across spaces 2, 3, 4 at (22..24, 20)
        sim.grid_mut().set_terrain(22, 20, TERRAIN_OBSTACLE);
        sim.grid_mut().set_terrain(23, 20, TERRAIN_OBSTACLE);
        sim.grid_mut().set_terrain(24, 20, TERRAIN_OBSTACLE);

        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        auto& worker = sim.get_unit(worker_id);

        sim.apply_knockback(worker_id, 19 * 32 + 16, 20 * 32 + 16, 4, 4);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return worker.state != UnitState::Knockback; }) >= 0);

        // KnockDir: East (24, 20) is a rock, the next direction (+1, South-East) lands on the free tile (24, 24)
        ASSERT_EQ(worker.pos.x, 24);
        ASSERT_EQ(worker.pos.y, 24);
        ASSERT_EQ(worker.state, UnitState::Idle);
        ASSERT_TRUE(sim.has_audio_event(64)); // SOUND_FLY_THUMP_A
    } TEST_END();


    TEST_CASE("12.39 Dynamic Base Deposit Path Avoids Queued Ants, Bombs and Fire Walls") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x; // 20
        int32_t by = base->y; // 20

        // Spawn 3 workers carrying food on the waiting column in front of the hill
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 2});
        uint32_t a3 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 1});

        sim.get_unit(a1).pick_up_food(1, 25);
        sim.get_unit(a2).pick_up_food(1, 25);
        sim.get_unit(a3).pick_up_food(1, 25);

        // Order all three onto the hill: the first takes the entrance, the others wait for their turn
        sim.join_base_queue(a1);
        sim.join_base_queue(a2);
        sim.join_base_queue(a3);
        ASSERT_EQ(sim.get_unit(a1).orig_order, AntUnit::kOrderHome);

        // Ant 1 walks to the entrance and never steps on a tile another ant stands on
        auto all_on_distinct_tiles = [&]() {
            return sim.get_unit(a1).pos != sim.get_unit(a2).pos && sim.get_unit(a1).pos != sim.get_unit(a3).pos &&
                   sim.get_unit(a2).pos != sim.get_unit(a3).pos;
        };
        ASSERT_TRUE(all_on_distinct_tiles());
        for (int i = 0; i < 300 && sim.get_unit(a1).state != UnitState::EnteringBase; ++i) {
            sim.tick();
            ASSERT_TRUE(all_on_distinct_tiles());
        }
        ASSERT_EQ(sim.get_unit(a1).state, UnitState::EnteringBase);
        ASSERT_EQ(sim.get_unit(a1).pos, (TileCoord{bx + 1, by + 1}));

        // The ants that wait for their turn are sent in one by one (ANTHILLQ) and never share a tile with each other
        std::set<uint32_t> entered{a1};
        for (int i = 0; i < 1500 && sim.get_player_score(0) < 75; ++i) {
            sim.tick();
            ASSERT_TRUE(all_on_distinct_tiles());
            const uint32_t active = sim.get_active_depositing_ant(0);
            if (active != 0) entered.insert(active);
        }
        ASSERT_EQ(entered.size(), 3u);
        ASSERT_EQ(sim.get_player_score(0), 75);

        // A friendly bomb between a lone worker and the entrance is never stepped on: the worker re-plans
        uint32_t lone = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 4, by + 1});
        sim.get_unit(lone).pick_up_food(1, 25);
        sim.grid_mut().place_bomb(static_cast<uint32_t>(bx - 3), static_cast<uint32_t>(by + 1), 0);
        ASSERT_TRUE(sim.grid().has_bomb_at(TileCoord{bx - 3, by + 1}));
        sim.join_base_queue(lone);
        for (int i = 0; i < 400 && sim.get_unit(lone).state != UnitState::EnteringBase; ++i) {
            sim.tick();
            ASSERT_TRUE(sim.get_unit(lone).pos != (TileCoord{bx - 3, by + 1}));
        }
        ASSERT_EQ(sim.get_unit(lone).state, UnitState::EnteringBase);
        ASSERT_EQ(sim.get_unit(lone).pos, (TileCoord{bx + 1, by + 1}));
    } TEST_END();

    TEST_CASE("12.40 Ant Lunchbox Sprite Render Order Layering (Lunchbox in Back)") {
        AssetArchive archive;
        std::string chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
        ASSERT_TRUE(archive.load_chd(chd_path));

        // Test holding directional walk animations and base entry animations across all ant types
        std::vector<std::string> test_anims = {
            "hgwg301", "hgwg201", "hgwg701", "hgwg801", "hgwg901",
            "hgws201", "hgwd301", "hgwm901",
            "hgen301", "hben301", "hfen301", "hcen301", "hsen301", "hten301"
        };

        for (const auto& anim_name : test_anims) {
            const auto* seq = archive.find_animation(anim_name);
            ASSERT_TRUE(seq != nullptr);
            ASSERT_FALSE(seq->subitems.empty());

            for (const auto& sub : seq->subitems) {
                if (sub.frames.size() <= 1) continue;

                auto frames = sub.frames;
                std::stable_sort(frames.begin(), frames.end(), [&](const auto& a, const auto& b) {
                    const auto& sp_a = archive.get_sprite(a.sprite_index);
                    const auto& sp_b = archive.get_sprite(b.sprite_index);
                    bool is_lb_a = (sp_a.name.find("lb") != std::string::npos);
                    bool is_lb_b = (sp_b.name.find("lb") != std::string::npos);
                    if (is_lb_a != is_lb_b) {
                        return is_lb_a; // Lunchbox rendered first (in back)
                    }
                    return false;
                });

                // First frame must be a lunchbox sprite (rendered in the back)
                const auto& sp0 = archive.get_sprite(frames[0].sprite_index);
                ASSERT_TRUE(sp0.name.find("lb") != std::string::npos);

                // Last frame must be the ant body sprite (rendered in front)
                const auto& sp_back = archive.get_sprite(frames.back().sprite_index);
                ASSERT_TRUE(sp_back.name.find("lb") == std::string::npos);
            }
        }
    } TEST_END();

    TEST_CASE("12.41 Standard Attack 1-Tile Pushback & Flinch Animation on Land") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.execute_melee_attack(attacker, defender);

        const auto& def = sim.get_unit(defender);
        // The hit point is lost at the contact and the victim turns to the attacker; it stays until the strike frame
        ASSERT_EQ(def.hp, 9);
        ASSERT_EQ(def.facing, Direction::West);
        ASSERT_EQ(def.pos.x, 11);
        ASSERT_TRUE(def.engaged);

        // The strike frame of the attack clip (200 ms) throws it: the gh clip carries it one tile East
        ASSERT_EQ(wait_ms(sim, 1000, [&]() { return def.state == UnitState::Flinch; }), 200);
        ASSERT_EQ(def.facing, Direction::West); // Victim keeps facing the attacker West
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return def.pos.x == 12; }) >= 0);
        ASSERT_EQ(def.pos.y, 10);

        // The flinch clip (gh) lasts 790 ms for a worker; then the ant is idle again
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def.state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(def.pos.x, 12);
        ASSERT_FALSE(def.is_stunned());
    } TEST_END();

    TEST_CASE("12.42 Standard Attack 1-Tile Pushback into Water (Drowning Clip At The Landing, Removal At Its End)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water at (12, 10)
        sim.grid_mut().set_terrain(12, 10, TERRAIN_WATER);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.clear_audio_events();
        sim.execute_melee_attack(attacker, defender);

        const auto& def = sim.get_unit(defender);
        // The landing event of the gh clip (500 ms) finds the water: a non-swimmer starts the drowning clip
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def.state == UnitState::Drowning; }) >= 400);
        ASSERT_EQ(def.pos.x, 12);
        ASSERT_EQ(def.pos.y, 10);
        ASSERT_EQ(def.hp, 9);           // the drowning takes no hit points
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        run_ms(sim, 250);
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
        ASSERT_EQ(sim.stats_manager().get_player_stats(1).friendly_lost, 0u);

        // The ant is removed when the drowning clip ends: the scorecard counts the loss and the kill
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !def.is_alive(); }) >= 0);
        ASSERT_EQ(def.death_status, DeathStatus::Drowned);
        ASSERT_EQ(sim.stats_manager().get_player_stats(1).friendly_lost, 1u);
        ASSERT_EQ(sim.stats_manager().get_player_stats(0).enemy_killed, 1u);
    } TEST_END();

    TEST_CASE("12.43 Combat Ant 4-Tile Ballistic Fling & Type-Specific Animation for All 6 Ant Types") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        AssetArchive archive;
        std::string chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
        ASSERT_TRUE(archive.load_chd(chd_path));

        AntType types[6] = {AntType::Worker, AntType::Bomber, AntType::Fire,
                            AntType::Thief, AntType::Combat, AntType::Swimmer};
        const char* pfx[6] = {"aggf", "abgf", "afgf", "atgf", "acgf", "asgf"};

        for (size_t i = 0; i < 6; ++i) {
            AntType type = types[i];
            const auto* seq = archive.get_directional_animation(pfx[i], Direction::East);
            ASSERT_TRUE(seq != nullptr);
            ASSERT_FALSE(seq->subitems.empty());

            uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{10, static_cast<int32_t>(10 + i * 4)});
            uint32_t victim = sim.spawn_unit(1, type, TileCoord{11, static_cast<int32_t>(10 + i * 4)});

            sim.execute_melee_attack(combat, victim);

            // At the contact the victim only turns to the attacker; the strike frame throws it
            const auto& v = sim.get_unit(victim);
            ASSERT_EQ(v.facing, Direction::West);
            ASSERT_EQ(v.pos.x, 11);
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return v.state == UnitState::Knockback; }) >= 0);
            ASSERT_EQ(v.facing, Direction::West);

            // The gb clip carries it 4 tiles East in one jump; no stun follows a melee flight
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return v.state != UnitState::Knockback; }) >= 0);
            const auto& landed = sim.get_unit(victim);
            ASSERT_EQ(landed.pos.x, 15);
            ASSERT_NE(landed.state, UnitState::Stunned);
            ASSERT_FALSE(landed.is_stunned());
        }
    } TEST_END();

    TEST_CASE("12.44 Combat Ant 4-Tile Fling into Water (Drowning Clip At The Landing, Removal At Its End)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water at landing tile (15, 10)
        sim.grid_mut().set_terrain(15, 10, TERRAIN_WATER);

        uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.execute_melee_attack(combat, victim);
        const auto& landed = sim.get_unit(victim);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return landed.state == UnitState::Knockback; }) >= 0);

        // Advance until the flight lands in the water: the drowning clip starts, hit points stay (10 - 2)
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return landed.state == UnitState::Drowning; }) >= 0);
        ASSERT_EQ(landed.pos.x, 15);
        ASSERT_EQ(landed.pos.y, 10);
        ASSERT_EQ(landed.hp, 8);

        // The drowning clip ends with the removal of the ant
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !landed.is_alive(); }) >= 0);
        ASSERT_EQ(landed.death_status, DeathStatus::Drowned);
    } TEST_END();

    TEST_CASE("12.45 Swimmer Ant Pushback and Fling into Water Survives Without Drowning (splash, stun)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Standard attack pushback into water
        sim.grid_mut().set_terrain(12, 10, TERRAIN_WATER);
        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t swimmer = sim.spawn_unit(1, AntType::Swimmer, TileCoord{11, 10});

        sim.execute_melee_attack(attacker, swimmer);

        // A swimmer that lands in water splashes (dsplash) and is stunned, it does not drown
        const auto& sw1 = sim.get_unit(swimmer);
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sw1.state == UnitState::Stunned; }) >= 0);
        ASSERT_EQ(sw1.pos.x, 12);
        ASSERT_EQ(sw1.pos.y, 10);
        ASSERT_EQ(sw1.hp, 9);
        ASSERT_TRUE(sw1.in_water);
        ASSERT_EQ(sw1.death_status, DeathStatus::Alive);
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return sw1.state == UnitState::Swimming; }) >= 0);
        ASSERT_TRUE(sw1.is_alive());

        // 2. Combat Ant 4-tile fling into water
        sim.grid_mut().set_terrain(25, 20, TERRAIN_WATER);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        uint32_t swimmer2 = sim.spawn_unit(1, AntType::Swimmer, TileCoord{21, 20});

        sim.execute_melee_attack(combat, swimmer2);
        const auto& sw2 = sim.get_unit(swimmer2);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sw2.state == UnitState::Stunned; }) >= 0);
        ASSERT_EQ(sw2.pos.x, 25);
        ASSERT_EQ(sw2.pos.y, 20);
        // Swimmer landed in water without drowning!
        ASSERT_EQ(sw2.hp, 8); // 10 - 2 punch damage
        ASSERT_TRUE(sw2.in_water);
        ASSERT_EQ(sw2.death_status, DeathStatus::Alive);
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return sw2.state == UnitState::Swimming; }) >= 0);
    } TEST_END();

    TEST_CASE("12.46 Bomb Placement Disallowed on Mud Surface") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        // Set (11, 10) to mud
        sim.grid_mut().get_cell_mut({11, 10}).is_mud = true;
        sim.grid_mut().get_cell_mut({11, 10}).surface_type = SurfaceType::Mud;

        ASSERT_FALSE(sim.grid().get_cell({11, 10}).can_place_bomb());

        // Direct ability call fails on mud
        bool res = sim.plant_bomb(bomber, {11, 10}, false);
        ASSERT_FALSE(res);
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // Issue order fails on mud
        AntOrder order{};
        order.type = OrderType::PlantBomb;
        order.ant_id = bomber;
        order.target_x = 11;
        order.target_y = 10;
        sim.issue_order(order);
        ASSERT_NE(sim.get_unit(bomber).state, UnitState::PlantingBomb);
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // After the can't clip (the refusal of the order) the bomber accepts the next order again
        ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return sim.get_unit(bomber).state == UnitState::Idle; }) >= 0);

        // Placement succeeds on valid gravel at (10, 9)
        bool ok = sim.plant_bomb(bomber, {10, 9}, false);
        ASSERT_TRUE(ok);
        ASSERT_EQ(sim.get_unit(bomber).state, UnitState::PlantingBomb);
    } TEST_END();

    TEST_CASE("12.47 Food Pickup Adjacency Requirement & The Grab Clip") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // A pile of one unit (2x2 crackers around its anchor (15, 10): the cells (14..15, 9..10)), 25 points
        const int32_t pile = place_pile(sim, 15, 10, 1, 25, {{1, 372}, {0, 0x7FFE}});
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{14, 10}), pile);

        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        // Move worker to (13, 10), adjacent to the pile
        sim.issue_move_order(worker, TileCoord{13, 10});

        // Step simulation until arrival
        while (!sim.get_unit(worker).waypoints.empty() || sim.get_unit(worker).state == UnitState::Walking) {
            sim.tick();
            if (sim.get_unit(worker).pos.x < 13) {
                // Not adjacent/arrived yet, must not have collected food
                ASSERT_FALSE(sim.get_unit(worker).is_holding());
            }
        }

        // Worker reached (13, 10), adjacent to the pile: walking or standing on adjacent tiles must NOT harvest anything
        ASSERT_EQ(sim.get_unit(worker).pos.x, 13);
        ASSERT_EQ(sim.get_unit(worker).pos.y, 10);
        ASSERT_EQ(sim.get_unit(worker).state, UnitState::Idle);
        ASSERT_FALSE(sim.get_unit(worker).is_holding());
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 1u);

        // Now explicitly instruct worker to eat the food at (15, 10): it faces the food and plays the grab clip at once (the
        // pile is the very next tile of its path)
        sim.issue_move_order(worker, TileCoord{15, 10});
        ASSERT_TRUE(wait_ms(sim, 500, [&]() { return sim.get_unit(worker).loco_action == AntUnit::kActionHarvest; }) >= 0);
        ASSERT_EQ(sim.get_unit(worker).state, UnitState::HarvestingFood);
        ASSERT_EQ(sim.get_unit(worker).facing, Direction::East);
        ASSERT_FALSE(sim.get_unit(worker).is_holding());
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 1u);

        // The food is taken when the clip ends (the clip of the original lasts its frames' sum, here at most one tick more)
        const uint32_t clip_ms = ants::sim::movement::action_clip(ants::sim::movement::ActionClip::Harvest, ants::sim::movement::kAntWorker,
                                                                  static_cast<uint8_t>(Direction::East), false).total_duration_ms();
        ASSERT_TRUE(clip_ms >= 300 && clip_ms <= 500);
        const int took = wait_ms(sim, 2000, [&]() { return sim.get_unit(worker).is_holding(); });
        ASSERT_TRUE(took >= 0);
        ASSERT_TRUE(static_cast<uint32_t>(took) <= clip_ms + 100);
        const auto& w = sim.get_unit(worker);
        ASSERT_EQ(w.carried_food, 1);
        ASSERT_EQ(w.carried_points, 25u);
        ASSERT_EQ(w.state, UnitState::Idle);

        // The last unit is gone with its tiles
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 0u);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{14, 10}), -1);
        ASSERT_EQ(sim.grid().get_cell({15, 10}).interactive_id, TILE_EMPTY);
    } TEST_END();

    TEST_CASE("12.48 Bomber Carrying Food Placing Bomb Uses absb and Does Not Disappear") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        auto* b_unit = const_cast<AntUnit*>(&sim.get_unit(bomber));
        b_unit->pick_up_food(1, 25);
        ASSERT_TRUE(b_unit->is_holding());

        bool ok = sim.plant_bomb(bomber, {11, 10}, false);
        ASSERT_TRUE(ok);
        ASSERT_EQ(b_unit->state, UnitState::PlantingBomb);
        ASSERT_TRUE(b_unit->is_holding());
    } TEST_END();

    TEST_CASE("12.49 Bomb Placement Animation Runs Authentic 28 Ticks with Sound at Tick 14 and Placement at Tick 18") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        bool ok = sim.plant_bomb(bomber, {11, 10}, false);
        ASSERT_TRUE(ok);
        ASSERT_EQ(sim.get_unit(bomber).state, UnitState::PlantingBomb);

        // Advance 13 ticks: no bomb on grid yet
        for (int i = 0; i < 13; ++i) {
            sim.tick();
        }
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // Tick 14: Sound 90 (BombPick) triggered
        sim.tick();
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // Advance up to tick 27: still no bomb on grid (seamless placement prevents double bomb)
        for (int i = 0; i < 13; ++i) {
            sim.tick();
        }
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // Tick 28: Bomb placed seamlessly on grid upon animation completion!
        sim.tick();
        ASSERT_TRUE(sim.has_bomb_at({11, 10}));
        ASSERT_EQ(sim.get_unit(bomber).state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("12.50 Single Attack Order Executes One Strike And Ends (no pursuit, no second blow)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        AntOrder order{};
        order.type = OrderType::Attack;
        order.ant_id = attacker;
        order.target_entity_id = static_cast<int32_t>(defender);
        sim.issue_order(order);
        ASSERT_EQ(sim.get_unit(attacker).orig_order, AntUnit::kOrderAttack);

        // The blow: the step into the defender's tile is the contact
        const auto& atk = sim.get_unit(attacker);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(defender).hp < 10; }) >= 0);
        ASSERT_EQ(atk.loco_action, AntUnit::kActionAttack);
        ASSERT_EQ(sim.get_unit(defender).hp, 9);

        // The order ends with the blow: the attacker is idle, its order is cleared, and there is no second blow
        run_ms(sim, 5000);
        ASSERT_EQ(sim.get_unit(attacker).state, UnitState::Idle);
        ASSERT_EQ(sim.get_unit(attacker).orig_order, AntUnit::kOrderNone);
        ASSERT_EQ(sim.get_unit(defender).hp, 9);
    } TEST_END();

    TEST_CASE("12.51 The Victim Turns To The Attacker At The Contact And Is Pushed Along The Attack Vector") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        auto* def_unit = const_cast<AntUnit*>(&sim.get_unit(defender));
        def_unit->facing = Direction::North;

        sim.execute_melee_attack(attacker, defender);
        // Facing is forced towards attacker (West) at the contact
        ASSERT_EQ(def_unit->facing, Direction::West);
        // Pushed East to (12, 10) by the flight after the strike frame
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def_unit->pos.x == 12 && def_unit->state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(def_unit->pos.y, 10);
    } TEST_END();

    TEST_CASE("12.52 Lethal 0 HP Death: The Dying Ant Plays death1..death4 On Its Own Sprite (No Separate Effect) While Water Drowning Plays The Drown Clip") {
        // the ant's own clip is one of Table-4 animations 102 .. 105 (death1 .. death4); no effect of that name exists any more (the original plays the clip on the ant's sprite)
        auto has_death_effect = [](const SimulationEngine& s) {
            for (const auto& a : s.get_world_state().ants) {
                if (a.state == UnitState::Dead && a.loco_clip >= 102 && a.loco_clip <= 105) return true;
            }
            return false;
        };
        auto separate_effect = [](const SimulationEngine& s) {
            for (const auto& eff : s.get_world_state().effects) {
                if (eff.anim_name.rfind("death", 0) == 0) return true;
            }
            return false;
        };

        // 1. Lethal Melee Attack: hp is 0 at the contact, the victim is still thrown; the death clip starts at the end
        //    of the flight (LowHpCheck), the ant is removed when the clip ends
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
            auto* def = const_cast<AntUnit*>(&sim.get_unit(defender));
            def->hp = 1; // 1 HP, lethal against 1 HP melee strike

            sim.execute_melee_attack(attacker, defender);
            ASSERT_EQ(def->hp, 0);
            ASSERT_NE(def->state, UnitState::Dead);      // not dead yet: death is deferred
            ASSERT_FALSE(has_death_effect(sim));

            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return def->state == UnitState::Dead; }) >= 0);
            ASSERT_TRUE(def->is_alive());                // the death clip is playing
            ASSERT_TRUE(has_death_effect(sim));
            ASSERT_FALSE(separate_effect(sim));
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !def->is_alive(); }) >= 0);
        }

        // 2. Lethal Bomb Blast (whether the bomb is a dud or throws the ant): the ant dies after the burn or the flight
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
            auto* vic = const_cast<AntUnit*>(&sim.get_unit(victim));
            vic->hp = 2; // lethal against the 2 damage of a bomb blast
            sim.grid_mut().place_bomb(15, 15, 1); // Enemy team bomb

            sim.trigger_bomb_detonation(victim, TileCoord{15, 15});
            ASSERT_EQ(vic->hp, 0);

            ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return vic->state == UnitState::Dead; }) >= 0);
            ASSERT_TRUE(has_death_effect(sim));
        }

        // 3. Pushback into Water (Non-swimmer drowns, NO skull death animation)
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            sim.grid_mut().set_terrain(12, 10, TERRAIN_WATER);
            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

            sim.execute_melee_attack(attacker, victim);
            const auto& vic = sim.get_unit(victim);
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return vic.state == UnitState::Drowning; }) >= 0);
            ASSERT_EQ(vic.pos.x, 12);
            ASSERT_EQ(vic.pos.y, 10);
            ASSERT_FALSE(has_death_effect(sim));      // no death clip for drowning (the ant plays the drown clip)
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !vic.is_alive(); }) >= 0);
            ASSERT_EQ(vic.death_status, DeathStatus::Drowned);
            ASSERT_FALSE(has_death_effect(sim));
        }
    } TEST_END();

    // -------------------------------------------------------------------------
    // Test 12.53: Power-Up Transformation & Continuous Idle Animation Facing South
    // -------------------------------------------------------------------------
    TEST_CASE("12.53 Power-Up Pick-Up (getpow Clip) & Continuous Idle Animation Facing South & All Headings") {
        static constexpr std::array<std::pair<uint8_t, const char*>, 5> POWERUPS = {{
            {1, "Bomber"},
            {2, "Fire"},
            {3, "Thief"},
            {4, "Combat"},
            {5, "Swimmer"}
        }};

        // 1. Verify all 5 powerup types when approached moving SOUTH
        for (const auto& [pu_type, pu_name] : POWERUPS) {
            SimulationEngine sim;
            sim.init_test_world(30, 30, 100, 60000);
            sim.grid_mut().place_powerup(15, 15, pu_type);

            // Ant starts North of powerup at (15, 13), walks SOUTH to (15, 15)
            uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 13});
            sim.issue_move_order(ant_id, TileCoord{15, 15});

            // Step simulation until the walk lands on the power-up and the getpow clip starts
            while (sim.get_unit(ant_id).is_alive() && !picking_up(sim.get_unit(ant_id))) {
                sim.tick();
            }

            const auto& unit_trans = sim.get_unit(ant_id);
            ASSERT_TRUE(picking_up(unit_trans));
            ASSERT_EQ(unit_trans.pos.x, 15);
            ASSERT_EQ(unit_trans.pos.y, 15);
            ASSERT_EQ(static_cast<uint8_t>(unit_trans.facing), static_cast<uint8_t>(Direction::South));

            // The exact getpow frames (CHD animation 55, 70 ms each) play until the clip ends 840 ms after the snap
            uint16_t last_frame = 0;
            for (int t = 0; t < 16; ++t) {
                const auto& ws = sim.get_world_state();
                const AntSnapshot* snap = nullptr;
                for (const auto& a : ws.ants) {
                    if (a.id == ant_id) { snap = &a; break; }
                }
                ASSERT_TRUE(snap != nullptr);
                ASSERT_EQ(snap->state, UnitState::PoweringUp);
                ASSERT_EQ(snap->loco_clip, 55);
                ASSERT_TRUE(snap->loco_frame >= last_frame);
                last_frame = snap->loco_frame;
                sim.tick();
            }
            ASSERT_TRUE(last_frame >= 8);
            sim.tick();

            // The clip is over: the unit is idle as its new type (GuardIdle for Combat)
            const auto& unit_done = sim.get_unit(ant_id);
            ASSERT_FALSE(picking_up(unit_done));
            ASSERT_EQ(static_cast<uint8_t>(unit_done.type), pu_type);
            ASSERT_EQ(static_cast<uint8_t>(unit_done.facing), static_cast<uint8_t>(Direction::South));

            UnitState expected_state = (pu_type == 4) ? UnitState::GuardIdle : UnitState::Idle;
            ASSERT_EQ(static_cast<uint8_t>(unit_done.state), static_cast<uint8_t>(expected_state));

            // Step additional ticks and verify idle animation cycle is actively progressing (not static 0!)
            uint16_t initial_anim_frame = 0;
            {
                const auto& ws = sim.get_world_state();
                for (const auto& a : ws.ants) {
                    if (a.id == ant_id) { initial_anim_frame = a.anim_frame; break; }
                }
            }
            for (int t = 0; t < 20; ++t) {
                sim.tick();
            }
            uint16_t post_anim_frame = 0;
            {
                const auto& ws = sim.get_world_state();
                for (const auto& a : ws.ants) {
                    if (a.id == ant_id) { post_anim_frame = a.anim_frame; break; }
                }
            }
            ASSERT_TRUE(post_anim_frame > initial_anim_frame);
        }

        // 2. Verify all other 7 cardinal and diagonal approach headings
        static constexpr std::array<std::pair<int, int>, 7> OTHER_DIRS = {{
            {0, -1},  // North
            {1, 0},   // East
            {-1, 0},  // West
            {1, 1},   // SouthEast
            {-1, 1},  // SouthWest
            {1, -1},  // NorthEast
            {-1, -1}  // NorthWest
        }};

        for (const auto& [dx, dy] : OTHER_DIRS) {
            SimulationEngine sim;
            sim.init_test_world(30, 30, 100, 60000);
            sim.grid_mut().place_powerup(15, 15, 4); // Combat Ant powerup

            ants::sim::TileCoord start{15 - dx * 2, 15 - dy * 2};
            uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, start);
            sim.issue_move_order(ant_id, TileCoord{15, 15});

            for (int t = 0; t < 60; ++t) {
                sim.tick();
            }

            const auto& unit = sim.get_unit(ant_id);
            ASSERT_FALSE(picking_up(unit));
            ASSERT_EQ(unit.type, AntType::Combat);
            ASSERT_EQ(unit.state, UnitState::GuardIdle);
            ASSERT_TRUE(unit.anim_subitem > 0); // Actively animating
        }
    } TEST_END();

    TEST_CASE("12.54 Text Width Measurement, Multi-Line Splitting & Font Sizing Integrity") {
        ants::app::Renderer renderer;
        // Verify empty text width
        ASSERT_EQ(renderer.get_text_width(""), 0);

        // Verify non-empty text width is positive
        int32_t small_w = renderer.get_text_width("that food!", ants::app::FontSize::Px12);
        ASSERT_TRUE(small_w > 0);
        ASSERT_TRUE(renderer.get_text_height(ants::app::FontSize::Px12) > 0);

        // Verify proportional width scales monotonically with string length
        int32_t longer_w = renderer.get_text_width("that food! And extra text for width check.", ants::app::FontSize::Px12);
        ASSERT_TRUE(longer_w > small_w);

        // Verify HUD chat entry recording and scroll offset boundaries
        ants::app::HUD hud;
        hud.init(0);
        size_t initial_lines = hud.get_chat_log().size();
        hud.add_chat_entry("Player1", "Hello world!");
        hud.add_chat_entry("Player2", "that food!");
        ASSERT_EQ(hud.get_chat_log().size(), initial_lines + 4);   // a header line and a body line per entry
        ASSERT_EQ(hud.chat_follow_pos(), 0);                       // a log that is not higher than the view does not move
        ASSERT_EQ(hud.chat_content_end(), 37 + 2 * 25);            // the news flash (12 + 2 x 12 px) and two entries (12 + 12 px each), one pixel after each
    } TEST_END();

    TEST_CASE("12.55 Fire Ant Ignite Clip Timing (Wall At The End Of The 1760 ms Clip) & Immediate Zero-Cooldown Re-Cast") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t f_id = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 10});

        // Step 1: Initiate fire placement (non-instant, clip afsf: 1810 ms east)
        ASSERT_TRUE(sim.ignite_fire(f_id, TileCoord{11, 10}, false));
        ASSERT_EQ(sim.get_unit(f_id).state, UnitState::PlacingFire);

        // Nothing is on the grid for the whole clip except the invisible placeholder; the wall appears when it ends
        int elapsed = 0;
        while (!sim.grid().get_cell(TileCoord{11, 10}).has_fire() && elapsed < 4000) {
            ASSERT_EQ(sim.get_unit(f_id).state, UnitState::PlacingFire);
            sim.tick();
            elapsed += 50;
        }
        ASSERT_TRUE(elapsed >= 1750 && elapsed <= 1950);

        // The ant is idle at once: no cooldown exists in the original
        ASSERT_EQ(sim.get_unit(f_id).state, UnitState::Idle);

        // Immediate subsequent fire placement succeeds with zero post-animation cooldown
        ASSERT_TRUE(sim.ignite_fire(f_id, TileCoord{10, 11}, false));
        ASSERT_EQ(sim.get_unit(f_id).state, UnitState::PlacingFire);
    } TEST_END();

    TEST_CASE("12.56 Bomber Plant Clip: Orders Are Refused While It Plays, The Next Plant Can Start At Once When It Ends") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t b_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{15, 15});

        // Step 1: Initiate bomb planting (non-instant)
        ASSERT_TRUE(sim.plant_bomb(b_id, TileCoord{16, 15}, false));
        ASSERT_EQ(sim.get_unit(b_id).state, UnitState::PlantingBomb);

        // Half-way through the clip: another plant and a move order are refused (FUN_0101ff5a accepts only idle,
        // walking and stunned ants)
        for (int t = 0; t < 14; ++t) sim.tick();
        ASSERT_FALSE(sim.plant_bomb(b_id, TileCoord{15, 16}, false));
        sim.issue_move_order(b_id, TileCoord{18, 15});
        ASSERT_EQ(sim.get_unit(b_id).state, UnitState::PlantingBomb);
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{16, 15}));

        // The clip ends (1400 ms east): the bomb is there, the ant is idle and can plant again immediately
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.has_bomb_at(TileCoord{16, 15}); }) >= 0);
        ASSERT_EQ(sim.get_unit(b_id).state, UnitState::Idle);
        ASSERT_TRUE(sim.plant_bomb(b_id, TileCoord{15, 16}, false));
    } TEST_END();

    TEST_CASE("12.57 Bridge Demolition & Collapse Instant Drowning for Non-Swimmers") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);

        // Create completed bridges over water at {20, 20} and {22, 20}
        sim.set_terrain(20, 20, TERRAIN_WATER);
        sim.set_tile_flags(20, 20, 0x06);
        sim.grid_mut().set_bridge_at(TileCoord{20, 20}, 4, 3600);
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{20, 20}).has_completed_bridge());

        sim.set_terrain(22, 20, TERRAIN_WATER);
        sim.set_tile_flags(22, 20, 0x06);
        sim.grid_mut().set_bridge_at(TileCoord{22, 20}, 4, 3600);
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{22, 20}).has_completed_bridge());

        // Spawn a Worker ant on {20, 20} and Swimmer ant on {22, 20}
        uint32_t w_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        uint32_t s_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{22, 20});
        ASSERT_NE(sim.get_unit(w_id).state, UnitState::Drowning);
        ASSERT_NE(sim.get_unit(s_id).state, UnitState::Swimming);

        // Demolish/regress the bridges by 1 stage: units survive on decaying bridges
        sim.grid_mut().regress_bridge(20, 20);
        sim.grid_mut().regress_bridge(22, 20);
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{20, 20}).has_completed_bridge());
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{20, 20}).has_any_bridge());
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{22, 20}).has_completed_bridge());
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{22, 20}).has_any_bridge());
        sim.tick();
        ASSERT_NE(sim.get_unit(w_id).state, UnitState::Drowning);
        ASSERT_NE(sim.get_unit(s_id).state, UnitState::Swimming);

        // The bridge lifetime task ends both bridges (BridgeTimeout -> DestroyBridgeAt; it acts only on a completed bridge, 0x25): the tiles are water again
        sim.set_bridge_at(TileCoord{20, 20}, 4, 1);
        sim.set_bridge_at(TileCoord{22, 20}, 4, 1);
        sim.tick();
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{20, 20}).has_any_bridge());
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{22, 20}).has_any_bridge());

        // Now the Worker MUST start drowning (message 0x15), the Swimmer only splashes (dsplash) and swims on
        ASSERT_EQ(sim.get_unit(w_id).state, UnitState::Drowning);
        ASSERT_EQ(sim.get_unit(s_id).state, UnitState::Swimming);
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        run_ms(sim, 250);
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
    } TEST_END();

    TEST_CASE("12.58 The Chat Box Is Always Active: Ctrl Keys Are Not Typed, Clicks Do Not Focus Or Unfocus, The Option Covers It") {
        HUD hud;
        hud.init(0);
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        ViewportCamera camera;

        // Control key (e.g. Ctrl+A) MUST NOT be appended to chat input
        hud.handle_key_down('a', sim, camera, KMOD_CTRL);
        ASSERT_TRUE(hud.get_chat_input().empty());

        // Without Ctrl a printable key is typed at once: there is no focus to gain (FUN_0102609a: the chat edit is always active)
        hud.handle_key_down('a', sim, camera, 0);
        ASSERT_EQ(hud.get_chat_input(), "a");

        // Clicks on the field or anywhere else do not change the box
        hud.handle_mouse_down(200, 200, SDL_BUTTON_LEFT, sim, camera);
        hud.handle_mouse_up(200, 200, SDL_BUTTON_LEFT, sim, camera);
        hud.handle_mouse_down(500, 428, SDL_BUTTON_LEFT, sim, camera);
        hud.handle_mouse_up(500, 428, SDL_BUTTON_LEFT, sim, camera);
        ASSERT_EQ(hud.get_chat_input(), "a");

        // Switched off in the options ("Participate In Chat"): the box is covered and takes nothing
        hud.open_options();
        hud.handle_mouse_down(151, 289, SDL_BUTTON_LEFT, sim, camera);      // the OFF toggle of the chat option
        ASSERT_TRUE(hud.is_chat_enabled());                                 // a switch acts at the release
        hud.handle_mouse_up(151, 289, SDL_BUTTON_LEFT, sim, camera);
        hud.close_options();
        ASSERT_FALSE(hud.is_chat_enabled());
        hud.handle_key_down('b', sim, camera, 0);
        hud.handle_text_input("c");
        ASSERT_EQ(hud.get_chat_input(), "a");
    } TEST_END();

    TEST_CASE("12.59 Options Dialog F9-F12 Quick Chat Editing & In-Game Broadcast Keys") {
        HUD hud;
        hud.init(0);
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        ViewportCamera camera;

        // Verify clean default strings (no "$_")
        ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it!");
        ASSERT_EQ(hud.get_quick_chat_key(1), "Let me be!");
        ASSERT_EQ(hud.get_quick_chat_key(2), "Attack!");
        ASSERT_EQ(hud.get_quick_chat_key(3), "Do you want to ally?");

        // Open options dialog: the F9 field has the focus (FUN_0101487c focuses the first edit field)
        hud.open_options();
        ASSERT_TRUE(hud.is_options_open());
        ASSERT_TRUE(hud.options_screen().edit(0).focused());
        ASSERT_FALSE(hud.options_screen().edit(1).focused());

        // A press on the F9 field (92..233, 370..385) keeps the focus there
        hud.handle_mouse_down(120, 375, SDL_BUTTON_LEFT, sim, camera);
        hud.handle_mouse_up(120, 375, SDL_BUTTON_LEFT, sim, camera);
        ASSERT_TRUE(hud.options_screen().edit(0).focused());

        // Backspace to delete "!"
        hud.handle_key_down(SDLK_BACKSPACE, sim, camera);
        ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it");

        // Type characters
        hud.handle_text_input(" all!");
        ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it all!");

        // Esc does nothing; Return closes the dialog whatever has the focus (FUN_01014f12) and the text stays
        hud.handle_key_down(SDLK_ESCAPE, sim, camera);
        ASSERT_TRUE(hud.is_options_open());
        hud.handle_key_down(SDLK_RETURN, sim, camera);
        ASSERT_FALSE(hud.is_options_open());
        ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it all!");

        // Press F9 in gameplay: broadcasts quick chat 0
        size_t initial_log_size = hud.get_chat_log().size();
        hud.handle_key_down(SDLK_F9, sim, camera);
        ASSERT_TRUE(hud.get_chat_log().size() > initial_log_size);
        std::string recent_text;
        for (size_t li = initial_log_size; li < hud.get_chat_log().size(); ++li) {
            recent_text += hud.get_chat_log()[li] + " ";
        }
        ASSERT_TRUE(recent_text.find("Now you are in for it all!") != std::string::npos);
    } TEST_END();

    TEST_CASE("12.60 Bridge Expiry No-Spam CantGo and Movement Cancellation") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);

        // Water channel at column 25 separating land at x=24 and island at x=26
        for (int y = 0; y < 60; ++y) {
            sim.set_terrain(25, y, TERRAIN_WATER);
            sim.set_tile_flags(25, y, 0x06);
        }

        // Bridge at {25, 20}
        sim.grid_mut().set_bridge_at(TileCoord{25, 20}, 4, 3600);
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{25, 20}).has_completed_bridge());

        // Spawn a Worker ant at {20, 20} and order to island {30, 20}
        uint32_t w_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        sim.issue_move_order(w_id, TileCoord{30, 20});
        ASSERT_EQ(sim.get_unit(w_id).state, UnitState::Walking);
        tick_until_paths_delivered(sim, {w_id});
        ASSERT_FALSE(sim.get_unit(w_id).waypoints.empty());

        // Advance 5 ticks: ant moves towards bridge
        for (int i = 0; i < 5; ++i) sim.tick();
        ASSERT_EQ(sim.get_unit(w_id).state, UnitState::Walking);

        // Collapse the bridge
        sim.grid_mut().collapse_bridge(25, 20);
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{25, 20}).has_completed_bridge());

        // Clear audio events
        sim.clear_audio_events();

        // Advance simulation: at the water edge the walk is blocked (TryEnterTile), the re-plan finds no way
        // to the island ("Can't go there."), and the can't-go animation plays its sound ONCE.
        uint32_t cant_go_audio_count = 0;
        for (int i = 0; i < 60; ++i) {
            sim.tick();
            auto events = sim.poll_audio_events();
            for (const auto& ev : events) {
                if (ev.sound_id == SoundID::CantGo) {
                    cant_go_audio_count++;
                }
            }
        }

        // Must play CantGo audio event EXACTLY ONCE (no repeated spamming!)
        ASSERT_EQ(cant_go_audio_count, 1u);

        // Movement must be completely cancelled: ant is now Idle, not drowned, waypoints empty
        const auto& w_ant = sim.get_unit(w_id);
        ASSERT_TRUE(w_ant.state == UnitState::Idle || w_ant.state == UnitState::GuardIdle);
        ASSERT_TRUE(w_ant.waypoints.empty());
        ASSERT_EQ(w_ant.final_dest, w_ant.pos);
        ASSERT_NE(w_ant.state, UnitState::Drowning);
    } TEST_END();

    TEST_CASE("12.61 Authentic Cursors, Edge Panning, Click Markers & Selection Brackets") {
        // 1. Asset verification in ants.chd
        AssetArchive archive;
        ASSERT_TRUE(archive.load_chd("Original-Ants/ants.chd"));

        // Cursors
        ASSERT_TRUE(archive.animation_count() > 54);
        ASSERT_EQ(archive.get_animation(41).name, "c_normal");
        ASSERT_EQ(archive.get_animation(42).name, "c_select");
        ASSERT_EQ(archive.get_animation(44).name, "c_mov1");
        ASSERT_EQ(archive.get_animation(43).name, "c_targ1");
        ASSERT_EQ(archive.get_animation(33).name, "c_attack");
        ASSERT_EQ(archive.get_animation(54).name, "c_food");
        ASSERT_EQ(archive.get_animation(39).name, "c_cant");
        ASSERT_EQ(archive.get_animation(32).name, "xmarks");

        // Edge panning cursors
        ASSERT_EQ(archive.get_animation(51).name, "CUR_N");
        ASSERT_EQ(archive.get_animation(46).name, "CUR_NE");
        ASSERT_EQ(archive.get_animation(45).name, "CUR_E");
        ASSERT_EQ(archive.get_animation(49).name, "CUR_SE");
        ASSERT_EQ(archive.get_animation(48).name, "CUR_S");
        ASSERT_EQ(archive.get_animation(52).name, "CUR_SW");
        ASSERT_EQ(archive.get_animation(50).name, "CUR_W");
        ASSERT_EQ(archive.get_animation(47).name, "CUR_NW");

        // Selection brackets
        ASSERT_EQ(archive.get_animation(58).name, "dogears");
        ASSERT_EQ(archive.get_animation(60).name, "yelears");
        ASSERT_EQ(archive.get_animation(61).name, "redears");
        ASSERT_EQ(archive.get_animation(149).name, "c_dogears");
        ASSERT_EQ(archive.get_animation(150).name, "c_yelears");
        ASSERT_EQ(archive.get_animation(151).name, "c_redears");
        ASSERT_EQ(archive.get_animation(59).name, "hillears");

        // 2. HUD evaluate_cursor Logic
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        HUD hud;
        hud.init(0);

        ViewportCamera camera;
        camera.viewport_w = 480;
        camera.viewport_h = 440;
        camera.world_x = 320;
        camera.world_y = 320;

        // A. Overlays: Normal cursor
        hud.open_options();
        ASSERT_EQ(hud.evaluate_cursor(200, 200, sim.get_world_state(), sim.grid(), camera), CursorType::Normal);
        hud.close_options();

        // B. Edge Panning Bounds Check (0x1026b09..0x1026d11)
        ASSERT_EQ(hud.evaluate_cursor(5, 5, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollNW);
        ASSERT_EQ(hud.evaluate_cursor(320, 5, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollN);
        ASSERT_EQ(hud.evaluate_cursor(635, 5, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollNE);
        ASSERT_EQ(hud.evaluate_cursor(5, 240, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollW);
        ASSERT_EQ(hud.evaluate_cursor(635, 240, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollE);
        ASSERT_EQ(hud.evaluate_cursor(5, 475, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollSW);
        ASSERT_EQ(hud.evaluate_cursor(320, 475, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollS);
        ASSERT_EQ(hud.evaluate_cursor(635, 475, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollSE);

        // C. HUD Chrome: Normal cursor (between edge panning margins 13..467)
        ASSERT_EQ(hud.evaluate_cursor(500, 200, sim.get_world_state(), sim.grid(), camera), CursorType::Normal);
        ASSERT_EQ(hud.evaluate_cursor(200, 16, sim.get_world_state(), sim.grid(), camera), CursorType::Normal);
        ASSERT_EQ(hud.evaluate_cursor(200, 464, sim.get_world_state(), sim.grid(), camera), CursorType::Normal);

        // D. Playfield - No units selected
        int32_t screen_tx15 = (15 * 32 + 16) - 320 + PLAYFIELD_X;
        int32_t screen_ty15 = (15 * 32 + 16) - 320 + PLAYFIELD_Y;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx15, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Normal);

        uint32_t my_worker = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
        uint32_t enemy_ant = sim.spawn_unit(1, AntType::Worker, TileCoord{18, 15});
        (void)my_worker;
        (void)enemy_ant;

        // Hover over friendly or enemy ant with no units selected -> Select
        ASSERT_EQ(hud.evaluate_cursor(screen_tx15, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Select);
        int32_t screen_tx18 = (18 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx18, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Select);

        // E. Playfield - Friendly Unit Selected
        hud.select_ant(my_worker);
        ASSERT_TRUE(hud.get_selected_ant_id() == my_worker);

        // Friendly ant hover -> Select
        ASSERT_EQ(hud.evaluate_cursor(screen_tx15, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Select);
        // Enemy ant hover -> Attack!
        ASSERT_EQ(hud.evaluate_cursor(screen_tx18, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Attack);

        // Food tile hover -> Food (a cell of a food object)
        place_pile(sim, 16, 15, 1, 25, {{1, TILE_LUNCHBOX}, {0, 0x7FFE}});
        int32_t screen_tx16 = (16 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx16, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Food);

        // Obstacle tile hover -> Move (authentic 1998 behavior: move cursor shown across map)
        sim.grid_mut().set_terrain(17, 15, TERRAIN_OBSTACLE);
        int32_t screen_tx17 = (17 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx17, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Move);

        // Water tile hover with Worker selected -> Move
        sim.grid_mut().set_terrain(14, 15, TERRAIN_WATER);
        int32_t screen_tx14 = (14 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx14, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Move);

        // Water tile hover with Swimmer selected -> Move!
        uint32_t my_swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{15, 16});
        hud.select_ant(my_swimmer);
        ASSERT_EQ(hud.evaluate_cursor(screen_tx14, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Move);

        // F. Anthill Bases & Thief
        uint32_t my_thief = sim.spawn_unit(0, AntType::Thief, TileCoord{15, 17});
        hud.select_ant(my_thief);

        // 3. Click Marker Spawning Verification
        int32_t spawned_wx = -1, spawned_wy = -1;
        hud.set_on_spawn_click_marker([&](int32_t wx, int32_t wy) {
            spawned_wx = wx;
            spawned_wy = wy;
        });

        // Click ground to move: drag dx/dy <= 4 at screen (250, 250)
        hud.handle_mouse_down(250, 250, 1, sim, camera, 0);
        hud.handle_mouse_up(250, 250, 1, sim, camera, 0);
        ASSERT_EQ(spawned_wx, camera.world_x + (250 - PLAYFIELD_X));
        ASSERT_EQ(spawned_wy, camera.world_y + (250 - PLAYFIELD_Y));

        // 4. Renderer Transient Effects Lifecycle
        Renderer renderer;
        renderer.spawn_transient_effect("xmarks", 500, 500);
        renderer.update_transient_effects(0.200f);
        renderer.update_transient_effects(0.300f);
    } TEST_END();

    TEST_CASE("12.62 Closest Passable Water Shoreline Fallback, Ally Walk-Over Movement & Setup Screen Map Centering") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Water shoreline fallback: lake from x=13..20, y=5..15
        for (int32_t wx = 13; wx <= 20; ++wx) {
            for (int32_t wy = 5; wy <= 15; ++wy) {
                sim.grid_mut().set_terrain(wx, wy, TERRAIN_WATER);
            }
        }
        uint32_t w_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& w_ant = sim.get_unit(w_id);

        // Issue move order into middle of water at (16, 10): GoTo replaces the unwalkable tile by the first
        // enterable tile of its ring scan (FUN_010202e7; distance 4, west column x = 12, top first) -> (12, 6)
        sim.issue_move_order(w_id, TileCoord{16, 10});
        ASSERT_NE(w_ant.state, UnitState::CantGo);
        ASSERT_EQ(w_ant.final_dest, (TileCoord{12, 6}));

        // Advance simulation until ant reaches the shoreline at (12, 6)
        ASSERT_TRUE(tick_until(sim, [&]() { return w_ant.pos == TileCoord{12, 6} && w_ant.waypoints.empty() &&
                                                  w_ant.state == UnitState::Idle; }, 100));

        // Ordering it into the lake again resolves to the tile it already stands on: a one-tile path, no can't-go
        sim.clear_audio_events();
        sim.issue_move_order(w_id, TileCoord{16, 10});
        ASSERT_EQ(w_ant.final_dest, (TileCoord{12, 6}));
        for (int t = 0; t < 20; ++t) {
            sim.tick();
        }
        ASSERT_EQ(w_ant.state, UnitState::Idle);
        ASSERT_EQ(w_ant.pos, (TileCoord{12, 6}));
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));

        // 2. Ally Walk-Over Movement & Cursors
        uint32_t ally_id = sim.spawn_unit(1, AntType::Worker, TileCoord{8, 8});
        (void)ally_id;
        sim.form_alliance(0, 1);
        ASSERT_TRUE(sim.stats_manager().are_allies(0, 1));

        HUD hud;
        hud.init(0);
        ViewportCamera camera{0, 0};
        hud.select_ant(w_id, false);

        // Hover over an ally with an own ant selected: the cursor is the attack cursor (there is no alliance test in FUN_01026aa3); when the order is
        // given the original asks (FUN_0101ffab): "Doing this will break your team with ...  Continue?"
        int32_t ally_screen_x = PLAYFIELD_X + (8 * 32 + 16);
        int32_t ally_screen_y = PLAYFIELD_Y + (8 * 32 + 16);
        CursorType c_ally = hud.evaluate_cursor(ally_screen_x, ally_screen_y, sim.get_world_state(), sim.grid(), camera);
        ASSERT_EQ(c_ally, CursorType::Attack);

        // Click the ally's ant: nothing moves, the confirmation opens; No (N) drops the order and keeps the team
        hud.handle_mouse_down(ally_screen_x, ally_screen_y, 1, sim, camera, 0);
        hud.handle_mouse_up(ally_screen_x, ally_screen_y, 1, sim, camera, 0);
        ASSERT_EQ(w_ant.state, UnitState::Idle);
        ASSERT_EQ(hud.alliance_dialog(), HUD::AllianceDialog::BreakConfirm);
        ASSERT_TRUE(sim.stats_manager().are_allies(0, 1));
        hud.handle_key_down('n', sim, camera);
        ASSERT_EQ(hud.alliance_dialog(), HUD::AllianceDialog::None);
        ASSERT_TRUE(sim.stats_manager().are_allies(0, 1));
        ASSERT_EQ(w_ant.state, UnitState::Idle);
        // Again, and Yes (Y): the team is broken and the attack order is carried out
        hud.handle_mouse_down(ally_screen_x, ally_screen_y, 1, sim, camera, 0);
        hud.handle_mouse_up(ally_screen_x, ally_screen_y, 1, sim, camera, 0);
        ASSERT_EQ(hud.alliance_dialog(), HUD::AllianceDialog::BreakConfirm);
        hud.handle_key_down('y', sim, camera);
        ASSERT_EQ(hud.alliance_dialog(), HUD::AllianceDialog::None);
        ASSERT_FALSE(sim.stats_manager().are_allies(0, 1));
        ASSERT_EQ(w_ant.state, UnitState::Walking);                                       // on its way to attack

        // 3. Map Select Screen Vertical Centering Formula
        Renderer renderer;
        int32_t th_large = renderer.get_text_height(FontSize::Px18);      // the map name label is 18 px high (FUN_01012ce0)
        int32_t name_y = 307 + (29 - th_large) / 2;
        // Cavity top is 307, height is 29 (y=307..335). Text y must be strictly inside cavity with symmetric padding
        ASSERT_GE(name_y, 307);
        ASSERT_LE(name_y + th_large, 336);
        int32_t top_pad = name_y - 307;
        int32_t bot_pad = 336 - (name_y + th_large);
        ASSERT_LE(std::abs(top_pad - bot_pad), 1); // Symmetric within 1 pixel
    } TEST_END();

    TEST_CASE("12.63 Ant Carrying Food Instructed to Eat Food Walks to Food Then Returns to Base") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place anthill at (5, 5)
        sim.grid_mut().set_anthill(0, TileCoord{5, 5});

        // A morsel of one unit at (20, 10) (a single cell: the lunchbox art)
        const int32_t morsel = place_pile(sim, 20, 10, 1, 25, {{1, TILE_LUNCHBOX}, {0, 0x7FFE}});
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{20, 10}), morsel);

        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto* w = const_cast<AntUnit*>(&sim.get_unit(worker));
        w->pick_up_food(1, 25);
        ASSERT_TRUE(w->is_holding());
        ASSERT_EQ(w->carried_food, 1);

        // Instruct ant with food to move to the food tile
        sim.issue_move_order(worker, TileCoord{20, 10});

        // Ant must walk towards food first (does NOT immediately return to base): the order is the harvest order (5)
        ASSERT_EQ(w->state, UnitState::Walking);
        ASSERT_EQ(w->orig_order, AntUnit::kOrderHarvest);

        // Advance simulation until ant reaches food; during transit it still carries exactly 1 food
        tick_until_paths_delivered(sim, {worker});
        while (!w->waypoints.empty() && w->orig_order == AntUnit::kOrderHarvest) {
            sim.tick();
            ASSERT_EQ(w->carried_food, 1);
        }

        // Food was NOT consumed (still on the map) and ant carried_food is still 1 (no double bite)
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(morsel)].remaining, 1u);
        ASSERT_EQ(w->carried_food, 1);
        ASSERT_TRUE(sim.has_lunchbox_at(TileCoord{20, 10}));

        // The ant does not eat: it heads for its own entrance (5 + 1, 5 + 1) with the enter order (order 2), and the
        // original says "Can't - already have food." (string 17); the food it carries is still the one it took at its origin
        ASSERT_EQ(w->orig_order, AntUnit::kOrderHome);
        ASSERT_TRUE(sim.has_news_event(0, 17));
        ASSERT_EQ(w->harvest_origin, (TileCoord{20, 10}));
    } TEST_END();

    TEST_CASE("12.64 Diagonal Melee Attack Pushes Diagonally Along Strike Vector") {
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 11});

            auto* def = const_cast<AntUnit*>(&sim.get_unit(defender));
            def->facing = Direction::South;

            sim.execute_melee_attack(attacker, defender);

            // Defender was forced to face towards attacker at (10, 10) (North-West)
            ASSERT_EQ(def->facing, Direction::NorthWest);

            // Pushback is along diagonal strike vector (p_dx = +1, p_dy = +1) to (12, 12)
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def->pos == TileCoord{12, 12} && def->state == UnitState::Idle; }) >= 0);
        }

        // If the primary diagonal destination is obstructed by rock, KnockDir tries dir+1 (South) first
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            sim.grid_mut().set_terrain(12, 12, TERRAIN_OBSTACLE);

            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 11});
            auto* def = const_cast<AntUnit*>(&sim.get_unit(defender));
            sim.execute_melee_attack(attacker, defender);
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def->pos != TileCoord{11, 11} && def->state == UnitState::Idle; }) >= 0);
            bool cardinal_flank = (def->pos == TileCoord{12, 11} || def->pos == TileCoord{11, 12});
            ASSERT_TRUE(cardinal_flank);
            ASSERT_EQ(def->pos, (TileCoord{11, 12}));
        }
    } TEST_END();

    TEST_CASE("12.65 Melee Attack Pushback Deflects To The Next Direction When Obstructed by Rock Wall") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Attacker at (10, 10), defender at (11, 10)
        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        // Place solid obstacle at primary push destination (12, 10)
        sim.grid_mut().set_terrain(12, 10, TERRAIN_OBSTACLE);

        sim.execute_melee_attack(attacker, defender);

        const auto& def = sim.get_unit(defender);
        // Victim forced to face attacker (West)
        ASSERT_EQ(def.facing, Direction::West);

        // Because primary East (12, 10) is obstructed by the wall, KnockDir tries the direction +1 (South-East) next
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def.pos != TileCoord{11, 10} && def.state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(def.pos, (TileCoord{12, 11}));
    } TEST_END();

    TEST_CASE("12.66 Selection Bracket (*ears) Table 4 Duration Millisecond Time Mapping") {
        AssetArchive archive;
        bool loaded = archive.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd");
        ASSERT_TRUE(loaded);

        // 1. dogears (Anim 58) & c_dogears (Anim 149): 4 frames @ 250ms each (1000ms loop)
        for (uint32_t aid : {58u, 149u}) {
            const auto& seq = archive.get_animation(aid);
            ASSERT_EQ(seq.subitems.size(), 4u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 0), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 249), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 250), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 499), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 500), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 749), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 750), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 999), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 1000), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 1250), 1u);
        }

        // 2. hillears (Anim 59): 4 frames @ 200ms each (800ms loop)
        {
            const auto& seq = archive.get_animation(59);
            ASSERT_EQ(seq.subitems.size(), 4u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 0), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 199), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 200), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 399), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 400), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 599), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 600), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 799), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 800), 0u);
        }

        // 3. yelears (Anim 60) & c_yelears (Anim 150): 4 frames: 60ms, 125ms, 125ms, 125ms (435ms loop)
        for (uint32_t aid : {60u, 150u}) {
            const auto& seq = archive.get_animation(aid);
            ASSERT_EQ(seq.subitems.size(), 4u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 0), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 59), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 60), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 184), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 185), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 309), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 310), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 434), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 435), 0u);
        }

        // 4. redears (Anim 61) & c_redears (Anim 151): 4 frames @ 60ms each (240ms loop)
        for (uint32_t aid : {61u, 151u}) {
            const auto& seq = archive.get_animation(aid);
            ASSERT_EQ(seq.subitems.size(), 4u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 0), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 59), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 60), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 119), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 120), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 179), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 180), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 239), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 240), 0u);
        }
    } TEST_END();

    TEST_CASE("12.67 CHECKGO: The 200 ms Time-Warning Grid (1 Minute, 30 Seconds, 11 Countdown Steps) And The End Of The Match") {
        // Ants.exe 0x1024839: every 200 ms the clock is compared with a threshold (61000, then 31000, then 11000 falling by 1000 per
        // step); below it the next warning is given (cue and flashing text, stage by stage) and the match ends at the first run that
        // finds the clock below 0. A 6 minute map: the warnings fall on remaining = 60800, 30800, 10800, 9800 ... 800.
        SimulationEngine sim;
        const uint32_t limit = 360000;
        sim.init_test_world(60, 60, 100, limit);
        struct Warning { uint32_t remaining; uint32_t sound; uint16_t text; };
        std::vector<Warning> got;
        int32_t over_elapsed = -1;
        for (uint32_t i = 0; i < 8000 && over_elapsed < 0; ++i) {
            const uint32_t elapsed = i * 50;          // the poll of this tick sees the clock at limit - elapsed
            sim.tick();
            uint32_t sound = 0;
            uint16_t text = 0;
            for (const auto& a : sim.poll_audio_events()) {
                if (a.target_player == 255 && (a.sound_id == SoundID::OneMinute || a.sound_id == SoundID::ThirtySeconds || a.sound_id == SoundID::Countdown)) sound = a.sound_id;
            }
            for (const auto& n : sim.poll_news_events()) {
                if (n.string_id == 49 || n.string_id == 50 || n.string_id == 59) {
                    ASSERT_TRUE(n.blink);                        // all three are posted with the flash flag
                    ASSERT_EQ(n.target_player, 255);
                    text = n.string_id;
                    if (n.string_id == 49) ASSERT_EQ(n.message_text, "1 minute left in the game.");
                    if (n.string_id == 50) ASSERT_EQ(n.message_text, "30 seconds left in the game.");
                    if (n.string_id == 59) ASSERT_EQ(n.message_text, "10 seconds and counting...");
                }
            }
            if (sound != 0 || text != 0) got.push_back({limit - elapsed, sound, text});
            if (sim.is_match_over()) over_elapsed = static_cast<int32_t>(elapsed);
        }
        ASSERT_EQ(got.size(), size_t{13});
        ASSERT_EQ(got[0].remaining, 60800u);
        ASSERT_EQ(got[0].sound, SoundID::OneMinute);
        ASSERT_EQ(got[0].text, 49);
        ASSERT_EQ(got[1].remaining, 30800u);
        ASSERT_EQ(got[1].sound, SoundID::ThirtySeconds);
        ASSERT_EQ(got[1].text, 50);
        for (uint32_t k = 0; k < 11; ++k) {
            ASSERT_EQ(got[2 + k].remaining, 10800u - 1000u * k);
            ASSERT_EQ(got[2 + k].sound, SoundID::Countdown);
            ASSERT_EQ(got[2 + k].text, 59);
        }
        // the match ends at the first run with the clock below 0: 200 ms after 0:00 on this grid
        ASSERT_EQ(over_elapsed, static_cast<int32_t>(limit + 200));
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);
    } TEST_END();

    TEST_CASE("12.68 Match Defeat Triggers losers.wav (Sound 42) & Player Drop-Out Triggers playerout.wav (Sound 41)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 50);

        sim.set_player_score(0, 500);
        sim.set_player_score(1, 100);

        sim.clear_audio_events();
        ASSERT_TRUE(run_until_over(sim) >= 0); // Match game over (within 200 ms after 0:00)

        ASSERT_TRUE(sim.is_match_over());
        // The stings are the results screen's (one per machine, Sound 56 for a winner, Sound 42 = losers.wav for a loser), not the simulation's
        ASSERT_FALSE(sim.has_targeted_audio_event(0, SoundID::VictoryFanfare));
        ASSERT_FALSE(sim.has_targeted_audio_event(1, SoundID::PlayerDefeat));
        ScorecardModal loser_screen;
        loser_screen.show(sim.get_world_state().match_result, 1);
        loser_screen.update(0.25f);
        ASSERT_EQ(loser_screen.get_audio_to_play(), SoundID::PlayerDefeat);
        ASSERT_EQ(SoundID::PlayerDefeat, 42u);

        // Player dropout (FUN_0100d03b): a News Flash line "%s dropped out of the game!" in the chat log, and playerout.wav
        // (Sound 41) unless the game is over
        sim.clear_audio_events();
        sim.clear_news_events();
        sim.trigger_player_dropout(1, "Player 1");
        ASSERT_FALSE(sim.has_audio_event(SoundID::PlayerDropOut));         // the match is over: silent
        ASSERT_TRUE(sim.has_news_event(255, StringID::PlayerDropOut));
        SimulationEngine live;
        live.init_test_world(60, 60, 101, 720000);
        live.trigger_player_dropout(1, "Player 1");
        ASSERT_TRUE(live.has_audio_event(SoundID::PlayerDropOut));
        ASSERT_EQ(SoundID::PlayerDropOut, 41u);
        bool line = false;
        for (const auto& n : live.poll_news_events()) {
            if (n.string_id == StringID::PlayerDropOut && n.channel == NewsChannel::ChatLog && n.message_text == "Player 1 dropped out of the game!") line = true;
        }
        ASSERT_TRUE(line);
    } TEST_END();

    TEST_CASE("12.69 Hatched Ant Emergence Triggers exithill.wav (Sound 43)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        sim.set_anthill(0, TileCoord{10, 10});
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 10);
        sim.set_hatch_delay_ticks(4); // Short incubation for testing

        sim.clear_audio_events();
        bool hatched = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(hatched);

        // Advance incubation
        bool got_exithill = false;
        for (int i = 0; i < 20; ++i) {
            sim.tick();
            if (sim.has_audio_event(SoundID::ExitHill)) {
                got_exithill = true;
                break;
            }
        }
        ASSERT_TRUE(got_exithill);
        ASSERT_EQ(SoundID::ExitHill, 43u);
    } TEST_END();

    TEST_CASE("12.70 A Landing On An Occupied Tile Throws Both Ants Apart (Blast 0) With The Hit Clip Sounds") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        uint32_t a1 = sim.spawn_unit(1, AntType::Worker, TileCoord{21, 20});
        uint32_t a2 = sim.spawn_unit(1, AntType::Combat, TileCoord{22, 20});   // stands on the landing tile

        sim.clear_audio_events();
        sim.execute_melee_attack(attacker, a1);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(a1).loco_action == AntUnit::kActionIdle &&
                                                       sim.get_unit(a2).loco_action == AntUnit::kActionIdle &&
                                                       sim.get_unit(a1).pos != sim.get_unit(a2).pos &&
                                                       !sim.get_unit(a1).engaged; }) >= 0);
        ASSERT_TRUE(sim.has_audio_event(64));      // frame 0 of the gh clip of every thrown ant
        ASSERT_FALSE(sim.get_unit(a1).pos == sim.get_unit(a2).pos);
        ASSERT_EQ(sim.get_unit(a2).hp, 10);        // the pile-up does no damage
    } TEST_END();

    TEST_CASE("12.71 Authentic Map Duration & LVL Header Parsing") {
        std::string maps_dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
        MapSelectScreen screen;
        screen.init(maps_dir);

        const auto& maps = screen.get_maps();
        ASSERT_GE(maps.size(), 6u);

        std::vector<std::pair<std::string, uint32_t>> expected_mins = {
            {"TINY.LVL", 6},
            {"SMALL.LVL", 8},
            {"MEDIUM.LVL", 10},
            {"GAUNTLET.LVL", 10},
            {"ISLANDS.LVL", 12},
            {"TREASURE.LVL", 12}
        };

        for (const auto& [fname, exp_min] : expected_mins) {
            bool found = false;
            for (const auto& m : maps) {
                if (m.filename == fname) {
                    found = true;
                    ASSERT_EQ(m.minutes, exp_min);

                    // Verify SimulationEngine initializes remaining time to exact match minutes
                    ants::assets::LevelData lvl;
                    if (lvl.load_from_file(m.full_path)) {
                        SimulationEngine sim;
                        sim.init(lvl, 42);
                        ASSERT_EQ(sim.get_match_time_remaining_ms(), exp_min * 60 * 1000u);
                    }
                    break;
                }
            }
            ASSERT_TRUE(found);
        }
    } TEST_END();

    TEST_CASE("12.72 TINY Map Central Enclosure Diagonal Pathfinding Walkability") {
        std::string tiny_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL";
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_from_file(tiny_path));

        SimulationEngine sim;
        sim.init(lvl, 42);

        // Spawn ant outside the central obstacle ring
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        ASSERT_TRUE(ant_id > 0);

        // The 9 user-reported central tiles inside the ring
        std::vector<TileCoord> targets = {
            {16, 17}, {17, 16}, {14, 17}, {13, 16}, {14, 15},
            {13, 14}, {14, 13}, {15, 14}, {16, 13}
        };

        // 1. Direct movement from outside the enclosure to each of the 9 tiles individually
        for (const auto& target : targets) {
            // Reset ant to outside position
            sim.get_unit(ant_id).pos = TileCoord{10, 10};
            sim.get_unit(ant_id).pixel_x = 10 * 32 + 16;
            sim.get_unit(ant_id).pixel_y = 10 * 32 + 16;
            sim.get_unit(ant_id).fx_x = (10 * 32 + 16) << 16;
            sim.get_unit(ant_id).fx_y = (10 * 32 + 16) << 16;
            sim.get_unit(ant_id).clear_path();
            sim.get_unit(ant_id).state = UnitState::Idle;

            sim.issue_move_order(ant_id, target);
            tick_until_paths_delivered(sim, {ant_id});
            const auto& ant = sim.get_unit(ant_id);

            // Unit must not reject with CantGo, and destination must reach the target tile
            ASSERT_FALSE(ant.state == UnitState::CantGo);
            ASSERT_TRUE(!ant.waypoints.empty());
            ASSERT_EQ(ant.final_dest, target);

            // Run simulation ticks until the ant reaches the central target square
            uint32_t ticks = 0;
            while ((sim.get_unit(ant_id).pos != target || sim.get_unit(ant_id).state == UnitState::Walking) && ticks < 400) {
                sim.tick();
                ticks++;
            }
            ASSERT_EQ(sim.get_unit(ant_id).pos, target);
        }

        // 2. Sequential traversal between all 9 tiles inside the enclosure
        for (const auto& target : targets) {
            sim.issue_move_order(ant_id, target);
            tick_until_paths_delivered(sim, {ant_id});
            const auto& ant = sim.get_unit(ant_id);

            // Unit must not reject with CantGo, and destination must reach the target tile
            ASSERT_FALSE(ant.state == UnitState::CantGo);
            ASSERT_TRUE(!ant.waypoints.empty());
            ASSERT_EQ(ant.final_dest, target);

            // Run simulation ticks until the ant reaches the central target square
            uint32_t ticks = 0;
            while ((sim.get_unit(ant_id).pos != target || sim.get_unit(ant_id).state == UnitState::Walking) && ticks < 400) {
                sim.tick();
                ticks++;
            }
            ASSERT_EQ(sim.get_unit(ant_id).pos, target);
        }
    } TEST_END();

    TEST_CASE("12.33 Daisy Flower Power-Up Droppers and Food Avoidance") {
        SimulationEngine sim;
        std::string path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL";
        ants::assets::LevelData lvl;
        if (lvl.load_lvl(path)) {
            sim.init(lvl, 42);

            const auto& ws = sim.get_world_state();
            ASSERT_EQ(ws.flower_droppers.size(), 2u);
            bool has_left = false;
            bool has_right = false;
            for (const auto& fd : ws.flower_droppers) {
                if (fd.x == 2 && fd.y == 19 && fd.drop_x == 2 && fd.drop_y == 20) has_left = true;
                if (fd.x == 37 && fd.y == 19 && fd.drop_x == 37 && fd.drop_y == 20) has_right = true;
            }
            ASSERT_TRUE(has_left);
            ASSERT_TRUE(has_right);

            // Authentic 1998 Stem Obstacle Parity: Bottom stem at (2, 19) and (37, 19) is a solid obstacle
            ASSERT_FALSE(sim.grid().get_cell(2, 19).is_passable());
            ASSERT_TRUE(sim.grid().get_cell(2, 19).is_obstacle_overlay);
            ASSERT_FALSE(sim.grid().get_cell(37, 19).is_passable());
            ASSERT_TRUE(sim.grid().get_cell(37, 19).is_obstacle_overlay);

            // Ground tile directly in front where powerup lands (2, 20) and (37, 20) is fully passable
            ASSERT_TRUE(sim.grid().get_cell(2, 20).is_passable());
            ASSERT_TRUE(sim.grid().get_cell(37, 20).is_passable());

            // Fast forward 301 ticks: FDTASK polls every 3000 ms (+ its run time); the first poll stamps the record, the fifth one (5 x 3001 = 15005 ms) sees
            // more than wp.param == 15 seconds and posts the drop (v0.0.56; before, the timer was a per-tick countdown of exactly 15 s)
            for (int i = 0; i < 301; ++i) {
                sim.tick();
            }

            const auto& ws_dropping = sim.get_world_state();
            ASSERT_TRUE(ws_dropping.flower_droppers[0].is_dropping);

            // Complete the 16-tick drop animation
            for (int i = 0; i < 16; ++i) {
                sim.tick();
            }

            const auto& ws_dropped = sim.get_world_state();
            ASSERT_FALSE(ws_dropped.flower_droppers[0].is_dropping);
            // Powerup placed at target (open ground tile in front of the plant base: y + 1)
            int32_t target_drop_x = ws.flower_droppers[0].drop_x;
            int32_t target_drop_y = ws.flower_droppers[0].drop_y;
            ASSERT_EQ(target_drop_y, ws.flower_droppers[0].y + 1);
            const auto& cell = sim.grid().get_cell(TileCoord{target_drop_x, target_drop_y});
            ASSERT_TRUE(cell.has_powerup());
            // SMALL.LVL probabilities are [0.45, 0.0, 0.0, 0.1, 0.45] (Combat & Thief are 0%)
            ASSERT_NE(cell.powerup_type, 4); // Not Combat
            ASSERT_NE(cell.powerup_type, 3); // Not Thief

            // Advance to the second posting with the power-up still uncollected: the record is stamped again at the posting (15005 ms), so the next
            // posting is five polls later (30010 ms), not 15 s after the landing; it replaces the power-up lying there
            for (int i = 0; i < 285; ++i) {
                sim.tick();
            }
            const auto& ws_dropping2 = sim.get_world_state();
            ASSERT_TRUE(ws_dropping2.flower_droppers[0].is_dropping);

            // Complete the second 16-tick drop animation
            for (int i = 0; i < 16; ++i) {
                sim.tick();
            }
            const auto& ws_dropped2 = sim.get_world_state();
            ASSERT_FALSE(ws_dropped2.flower_droppers[0].is_dropping);
            const auto& cell2 = sim.grid().get_cell(TileCoord{target_drop_x, target_drop_y});
            ASSERT_TRUE(cell2.has_powerup());
            ASSERT_NE(cell2.powerup_type, 4);
            ASSERT_NE(cell2.powerup_type, 3);

            // Food avoidance: Intermediate food is treated as obstacle in pathfinding
            sim.grid_mut().get_cell_mut(10, 10).is_food = true;
            sim.grid_mut().get_cell_mut(10, 10).interactive_id = 301;
            ASSERT_TRUE(sim.grid().get_cell(10, 10).has_food());

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 9});
            sim.issue_move_order(a1, TileCoord{10, 11});
            // Unit should route around (10, 10) instead of passing through it
            for (const auto& wp : sim.get_unit(a1).waypoints) {
                ASSERT_FALSE(wp.x == 10 && wp.y == 10);
            }
        }

        // Verify GAUNTLET.LVL waypoints including top-left daisy flower at (3, 5) -> drop at (3, 6)
        std::string gauntlet_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/GAUNTLET.LVL";
        ants::assets::LevelData gauntlet_lvl;
        if (gauntlet_lvl.load_lvl(gauntlet_path)) {
            sim.init(gauntlet_lvl, 42);
            const auto& g_ws = sim.get_world_state();
            ASSERT_EQ(g_ws.flower_droppers.size(), 1u);
            bool has_flower = false;
            for (const auto& fd : g_ws.flower_droppers) {
                if (fd.x == 3 && fd.y == 5 && fd.drop_x == 3 && fd.drop_y == 6) has_flower = true;
            }
            ASSERT_TRUE(has_flower);
        }

        // Verify TREASURE.LVL: Center plants have waypoints with flag == 0 (no power-up droppers)
        // and corridor is traversable without spurious obstacles
        std::string treasure_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TREASURE.LVL";
        ants::assets::LevelData treasure_lvl;
        if (treasure_lvl.load_lvl(treasure_path)) {
            sim.init(treasure_lvl, 42);
            const auto& t_ws = sim.get_world_state();
            ASSERT_EQ(t_ws.flower_droppers.size(), 0u);
        }
    } TEST_END();

    TEST_CASE("12.74: A Pile-Up Is Thrown Apart With The gh Clip; Only Ants That Are Not The Viewer's Show The Dust Cloud (battle) And Sound 3, Briefly") {
        for (int viewer = 0; viewer < 2; ++viewer) {
            SimulationEngine sim;
            ants::assets::LevelData lvl;
            std::string lvl_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL";
            ASSERT_TRUE(lvl.load_lvl(lvl_path));
            sim.init(lvl, 42);
            sim.set_viewing_player_id(static_cast<uint8_t>(viewer));

            // The victim of a melee blow lands on a tile where an enemy ant stands (pile-up)
            uint32_t a1_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 15});
            uint32_t a2_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 15});
            uint32_t a3_id = sim.spawn_unit(1, AntType::Combat, TileCoord{12, 15});
            sim.execute_melee_attack(a1_id, a2_id);

            bool has_battle_effect = false;
            bool got_landing_thump = false;
            int battle_ticks = 0;
            for (int t = 0; t < 60; ++t) {
                sim.tick();
                bool now = false;
                for (const auto& eff : sim.get_world_state().effects) {
                    if (eff.anim_name == "battle") { has_battle_effect = true; now = true; ASSERT_TRUE(eff.looping); }
                }
                if (now) ++battle_ticks;
                if (sim.has_audio_event(SoundID::FlingThumpB)) got_landing_thump = true;
            }
            // Blast puts the dust ball (Cloud54) on the tile of a pile-up of ants that are not the viewer's own (the viewer is
            // player 1 in the second run, whose ants are the ones thrown apart: no cloud); it is gone within about a second
            ASSERT_EQ(has_battle_effect, viewer == 0);
            ASSERT_EQ(sim.has_audio_event(SoundID::CombatNetFairy), viewer == 0);
            ASSERT_TRUE(battle_ticks < 30);
            ASSERT_TRUE(sim.has_audio_event(SoundID::FlingThumpA));
            ASSERT_TRUE(got_landing_thump);
            if (viewer == 0) {                                                        // the cloud owns its sound (622 ms long, the clip loops every 270 ms) and takes it along when it is removed
                uint32_t cloud_owner = 0;
                std::vector<AudioEvent> timeline = sim.poll_audio_events();
                for (const auto& e : timeline) {
                    if (!e.stop && e.sound_id == SoundID::CombatNetFairy) cloud_owner = e.owner;
                }
                ASSERT_TRUE(cloud_owner != 0);
                bool cloud_stop = false;
                for (const auto& e : timeline) {
                    if (e.stop && e.owner == cloud_owner) cloud_stop = true;
                }
                ASSERT_TRUE(cloud_stop);
            }

            // Ensure the ants ended on different discrete tiles
            const auto& a2 = sim.get_unit(a2_id);
            const auto& a3 = sim.get_unit(a3_id);
            ASSERT_FALSE(a2.pos == a3.pos);
            ASSERT_FALSE(a2.pos == sim.get_unit(a1_id).pos);
        }
    } TEST_END();

    TEST_CASE("12.75: Original Click Sounds: buttonclick (0) on Buttons, navbuttonclick (89) on Pedestals Only, Silent Toggles") {
        // The pressed-state animations of the original carry the click: sound 0 (buttonclick.wav) for the top bar, the
        // send-to buttons, the quit dialog, the setup screen and the results screen; sound 89 (navbuttonclick.wav) only
        // in the pedestal press animations; the option toggles, breturn3, qh_return3 and qh_start3 are silent; the stop
        // button carries antstop (61) only.
        ASSERT_EQ(SoundID::NavButtonClick, 89u);
        ASSERT_EQ(SoundID::ButtonClick, 0u);

        // 1. HUD buttons
        {
            HUD hud;
            hud.init(0);
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            ViewportCamera camera{0, 0};

            std::vector<uint32_t> played_sounds;
            hud.set_on_play_sfx([&](uint32_t s) { played_sounds.push_back(s); });

            // Help button (476, 7, 46, 23)
            played_sounds.clear();
            hud.handle_mouse_down(485, 15, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);

            // Quick help overlay dismiss: qh_return3 is silent
            played_sounds.clear();
            hud.handle_mouse_down(100, 100, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_TRUE(played_sounds.empty());

            // Options button (525, 7, 52, 23)
            played_sounds.clear();
            hud.handle_mouse_down(540, 15, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);

            // Options screen: the Chat ON / OFF toggles and the Return button (breturn3) are silent
            played_sounds.clear();
            hud.handle_mouse_down(126, 300, SDL_BUTTON_LEFT, sim, camera, 0);    // chat ON toggle
            hud.handle_mouse_down(175, 300, SDL_BUTTON_LEFT, sim, camera, 0);    // chat OFF toggle
            hud.handle_mouse_down(400, 440, SDL_BUTTON_LEFT, sim, camera, 0);    // Return button (351..449, 425..451)
            ASSERT_TRUE(played_sounds.empty());
            hud.handle_mouse_up(400, 440, SDL_BUTTON_LEFT, sim, camera, 0);

            // Quit button (579, 7, 46, 23) -> opens quit dialog
            played_sounds.clear();
            hud.handle_mouse_down(590, 15, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);
            hud.handle_mouse_up(590, 15, SDL_BUTTON_LEFT, sim, camera, 0);

            // Quit dialog "No" button (292, 260, 49, 24): no3 carries sound 0
            played_sounds.clear();
            hud.handle_mouse_down(310, 270, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);
            hud.handle_mouse_up(310, 270, SDL_BUTTON_LEFT, sim, camera, 0);

            // Spawn and select friendly ant
            uint32_t a_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            hud.select_ant(a_id, false);

            // Move Pedestal (slot 1: (482, 152) - (525, 225)): the press animation butmov2d carries sound 89
            played_sounds.clear();
            hud.handle_mouse_down(500, 170, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // Stop button (595, 180, 32, 50): antstop (61) and no click
            played_sounds.clear();
            hud.handle_mouse_down(610, 190, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            bool saw_nav = false;
            bool saw_stop = false;
            for (auto s : played_sounds) {
                if (s == SoundID::NavButtonClick || s == SoundID::ButtonClick) saw_nav = true;
                if (s == SoundID::AntStop) saw_stop = true;
            }
            ASSERT_FALSE(saw_nav);
            ASSERT_TRUE(saw_stop);

            // The Stop pedestal locks the mouse for 250 ms (5 ticks, FUN_01028bdd); afterwards the buttons answer again
            hud.handle_mouse_up(610, 190, SDL_BUTTON_LEFT, sim, camera, 0);
            hud.update(sim.get_world_state(), 5);

            // Chat [All] button (532, 443, 44, 24): butalld carries sound 0
            played_sounds.clear();
            hud.handle_mouse_down(545, 450, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);
        }

        // 2. MapSelectScreen buttons: the click of a pressed picture plays at the PRESS (the action comes with the release); the fog pair is silent, and the places
        // of the invented targets (the Drop button) are no buttons
        {
            MapSelectScreen screen;
            std::vector<uint32_t> played_sounds;
            screen.set_on_play_sfx([&](uint32_t s) { played_sounds.push_back(s); });

            struct Case { int32_t x; int32_t y; bool silent; };
            const Case cases[] = {
                { MapSelectScreen::BTN_UP_X + 10, MapSelectScreen::BTN_UP_Y + 10, false },
                { MapSelectScreen::BTN_DOWN_X + 10, MapSelectScreen::BTN_DOWN_Y + 10, false },
                { MapSelectScreen::BTN_FOW_ON_X + 5, MapSelectScreen::BTN_FOW_ON_Y + 5, true },
                { MapSelectScreen::BTN_FOW_OFF_X + 5, MapSelectScreen::BTN_FOW_OFF_Y + 5, true },
                { 576 + 10, 192 + 10, true },                                       // where the remake's invented Drop button was
                { MapSelectScreen::BTN_QUIT_X + 10, MapSelectScreen::BTN_QUIT_Y + 10, false },
                { MapSelectScreen::BTN_START_X + 10, MapSelectScreen::BTN_START_Y + 10, false },
            };
            for (const auto& c : cases) {
                played_sounds.clear();
                screen.handle_mouse_motion(c.x, c.y);
                screen.handle_mouse_down(c.x, c.y, SDL_BUTTON_LEFT);
                if (c.silent) {
                    ASSERT_TRUE(played_sounds.empty());
                } else {
                    ASSERT_FALSE(played_sounds.empty());
                    ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);
                }
                screen.handle_mouse_up(c.x, c.y, SDL_BUTTON_LEFT);
            }
        }

        // 3. ScorecardModal leave button: leave3 carries sound 0
        {
            ScorecardModal modal;
            MatchResult mr{};
            mr.is_over = true;
            modal.show(mr, 0);
            modal.update(0.25f);                                            // the Leave button exists once the rows are built
            std::vector<uint32_t> played_sounds;
            modal.set_on_play_sfx([&](uint32_t s) { played_sounds.push_back(s); });

            played_sounds.clear();
            modal.handle_mouse_down(550, 20);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);
        }
    } TEST_END();

    TEST_CASE("12.76b Fog Of War Reveals Only From Ant Position Updates Of The Viewer And Its Teammate: No Hill Box, Nothing When A Team Forms (Ants.exe FUN_0101a93a -> FUN_01006af4)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        sim.set_fog_of_war_enabled(true);
        sim.set_viewing_player_id(0);
        const uint32_t mine = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        const uint32_t friend_ant = sim.spawn_unit(2, AntType::Worker, TileCoord{50, 10});
        sim.spawn_unit(1, AntType::Worker, TileCoord{30, 45});
        sim.tick();
        {
            const auto& w = sim.get_world_state();
            // the own ant reveals its 13 x 13 square; the own hill at (20, 20) reveals nothing by itself (it used to reveal 16 x 16 tiles around it)
            ASSERT_TRUE(w.is_tile_revealed(5, 5) && w.is_tile_revealed(11, 11) && w.is_tile_revealed(0, 0));
            ASSERT_FALSE(w.is_tile_revealed(12, 5));
            ASSERT_FALSE(w.is_tile_revealed(20, 20));
            ASSERT_FALSE(w.is_tile_revealed(22, 22));
            // another team's ant reveals nothing
            ASSERT_FALSE(w.is_tile_revealed(50, 10));
            ASSERT_FALSE(w.is_tile_revealed(30, 45));
        }
        // a team forms: the ally's ant stands where it stood, so nothing is revealed (the original reveals only when the ally's ant SetPos runs)
        sim.form_alliance(0, 2);
        sim.tick();
        {
            const auto& w = sim.get_world_state();
            ASSERT_FALSE(w.is_tile_revealed(50, 10));
            ASSERT_FALSE(w.is_tile_revealed(47, 10));
        }
        // it moves: its position update now reveals the square around its new tile (and only that one)
        sim.issue_move_order(friend_ant, TileCoord{50, 12});
        for (int i = 0; i < 60; ++i) sim.tick();
        {
            const auto& w = sim.get_world_state();
            ASSERT_TRUE(w.is_tile_revealed(50, 12) && w.is_tile_revealed(56, 18) && w.is_tile_revealed(44, 6));
            ASSERT_FALSE(w.is_tile_revealed(30, 45));
        }
        // the own ant walks: every tile change reveals a new square, what was revealed stays
        sim.issue_move_order(mine, TileCoord{9, 5});
        for (int i = 0; i < 80; ++i) sim.tick();
        {
            const auto& w = sim.get_world_state();
            ASSERT_TRUE(w.is_tile_revealed(0, 0) && w.is_tile_revealed(15, 5));
            ASSERT_FALSE(w.is_tile_revealed(17, 5));
        }
    } TEST_END();

    TEST_CASE("12.76 Authentic Fog of War Sight Radius 6 & Exploration Persistence") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_fog_of_war_enabled(true);
        ASSERT_TRUE(sim.is_fog_of_war_enabled());

        sim.set_viewing_player_id(0);
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
        ASSERT_GT(ant_id, 0u);

        sim.tick();
        const auto& world = sim.get_world_state();
        ASSERT_TRUE(world.fog_of_war_enabled);

        // Unit tile and radius 6 tiles must be revealed
        ASSERT_TRUE(world.is_tile_revealed(30, 30));
        ASSERT_TRUE(world.is_tile_revealed(30 + 6, 30));
        ASSERT_TRUE(world.is_tile_revealed(30 - 6, 30));
        ASSERT_TRUE(world.is_tile_revealed(30, 30 + 6));
        ASSERT_TRUE(world.is_tile_revealed(30, 30 - 6));

        // Beyond radius 6 must remain shrouded
        ASSERT_FALSE(world.is_tile_revealed(30 + 7, 30));
        ASSERT_FALSE(world.is_tile_revealed(30 - 7, 30));
        ASSERT_FALSE(world.is_tile_revealed(30, 30 + 7));
        ASSERT_FALSE(world.is_tile_revealed(30, 30 - 7));
        ASSERT_FALSE(world.is_tile_revealed(5, 5));

        // Move unit to (31, 30) - previous tiles must stay permanently revealed
        sim.issue_move_order(ant_id, TileCoord{31, 30});
        for (int i = 0; i < 15; ++i) sim.tick();
        const auto& world2 = sim.get_world_state();
        ASSERT_TRUE(world2.is_tile_revealed(30, 30)); // Permanent exploration
        ASSERT_TRUE(world2.is_tile_revealed(31, 30));

        // Authentic 1998 Fog of War Autotiling & Dither Mapping Verification (Ants.exe 0x1008760..0x10087da)
        // c = 1 if neighbor is Fog, 0 if neighbor is Revealed
        auto compute_idx = [](int32_t c0, int32_t c1, int32_t c2, int32_t c3) {
            return ((c0 * 2 + c1) * 2 + 2 + c2) * 2 + c3;
        };

        // Deep interior fog: all neighbors are fog (1, 1, 1, 1) -> idx 19 -> dither0.bmp (352, solid 50% stipple)
        ASSERT_EQ(compute_idx(1, 1, 1, 1), 19);

        // Boundary fog: North neighbor is revealed (0, 1, 1, 1) -> idx 11 -> dither1.bmp (353, top edge fade)
        ASSERT_EQ(compute_idx(0, 1, 1, 1), 11);

        // Isolated fog island: all neighbors are revealed (0, 0, 0, 0) -> idx 4 -> dither13.bmp (365, rounded island dot)
        ASSERT_EQ(compute_idx(0, 0, 0, 0), 4);
    } TEST_END();

    TEST_CASE("12.77 Hatching Emergence Isolation of exithill.wav") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_anthill(0, TileCoord{10, 10});
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 10);
        sim.set_hatch_delay_ticks(4);

        // Hatch egg with 200 points
        sim.clear_audio_events();
        bool hatched = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(hatched);

        // Advance simulation through incubation delay until newborn surfaces
        bool hatched_exit_sound = false;
        for (int t = 0; t < 20; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::ExitHill)) {
                hatched_exit_sound = true;
                break;
            }
        }
        ASSERT_TRUE(hatched_exit_sound);

        // Now move a unit to base to heal/eat
        sim.clear_audio_events();
        uint32_t a_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        auto* a = const_cast<AntUnit*>(&sim.get_unit(a_id));
        a->hp = 5; // Damaged to trigger healing dwell

        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        sim.join_base_queue(a_id);

        // Run until unit enters, dwells, heals, and exits
        bool regular_exit_sound = false;
        for (int t = 0; t < 120; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::ExitHill)) {
                regular_exit_sound = true;
            }
        }
        // Exiting after heal/eating must NOT play ExitHill
        ASSERT_FALSE(regular_exit_sound);
    } TEST_END();

    TEST_CASE("12.78 Ability Cooldown Initiation & Uninterruptible Placement") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);

        // 1. Fire Ant: the ignite clip is uninterruptible (afsf)
        uint32_t fire_id = sim.spawn_unit(0, AntType::Fire, TileCoord{20, 20});
        sim.set_tile_flags(21, 20, 0x06);

        sim.clear_audio_events();
        ASSERT_TRUE(sim.ignite_fire(fire_id, TileCoord{21, 20}, false));
        ASSERT_EQ(sim.get_unit(fire_id).state, UnitState::PlacingFire);

        // Order move while placing -> silently ignored without CantGo per Ants.exe (uninterruptible)
        sim.clear_audio_events();
        sim.issue_move_order(fire_id, TileCoord{22, 20});
        ASSERT_EQ(sim.get_unit(fire_id).state, UnitState::PlacingFire); // Uninterruptible
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));

        // 2. Bomber Ant: the plant clip is uninterruptible (absb)
        uint32_t bomb_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{25, 25});
        sim.set_tile_flags(26, 25, 0x06);

        sim.clear_audio_events();
        ASSERT_TRUE(sim.plant_bomb(bomb_id, TileCoord{26, 25}, false));
        ASSERT_EQ(sim.get_unit(bomb_id).state, UnitState::PlantingBomb);

        // Order move while placing -> silently ignored without CantGo per Ants.exe (uninterruptible)
        sim.clear_audio_events();
        sim.issue_move_order(bomb_id, TileCoord{27, 25});
        ASSERT_EQ(sim.get_unit(bomb_id).state, UnitState::PlantingBomb); // Uninterruptible
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));

        // 3. An enemy attack is not refused: the bomber loses its hit point at the contact, the placing is cancelled
        //    without any change of the world (nothing is planted) and the bomber is thrown like any other victim
        uint32_t enemy_id = sim.spawn_unit(1, AntType::Worker, TileCoord{24, 25});

        int32_t hp_before = sim.get_unit(bomb_id).hp;
        int32_t x_before = sim.get_unit(bomb_id).pos.x;

        sim.execute_melee_attack(enemy_id, bomb_id);
        ASSERT_LT(sim.get_unit(bomb_id).hp, hp_before); // Took damage
        ASSERT_NE(sim.get_unit(bomb_id).state, UnitState::PlantingBomb); // The placing is cancelled
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(bomb_id).pos.x != x_before; }) >= 0); // and it is thrown
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{26, 25}));
    } TEST_END();

    TEST_CASE("12.79 A Fire Wall Stands On Its Tile; An Audio Event Addressed To One Player Reaches Only That Player") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);

        // Ignite fire at (10, 10)
        uint32_t fire_ant_id = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 9});
        sim.set_tile_flags(10, 10, 0x06);
        ASSERT_TRUE(sim.ignite_fire(fire_ant_id, TileCoord{10, 10}, false));
        for (int t = 0; t < 40; ++t) sim.tick();
        ASSERT_TRUE(sim.grid().has_fire_at(TileCoord{10, 10}));

        AssetArchive archive;
        std::string chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
        archive.load_chd(chd_path);
        HUD hud;
        hud.init(0);

        // Targeted audio event routing in AudioMixer
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        std::vector<AudioEvent> events;
        // AlliancePro targeted to recipient player 1
        events.push_back(AudioEvent{SoundID::AlliancePro, 0, 0, 1, 1});

        // Player 0 should ignore targeted event
        mixer.ingest_simulation_events(events, 0);
        ASSERT_EQ(mixer.active_channel_count(), 0u);

        // Player 1 should receive targeted event
        mixer.ingest_simulation_events(events, 1);
        ASSERT_EQ(mixer.active_channel_count(), 1u);
    } TEST_END();

    TEST_CASE("12.98: An Attack Order Ends After The One Blow: No Pursuit, The Attacker Stays In Its Row") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        AntOrder atk_order{};
        atk_order.ant_id = attacker_id;
        atk_order.type = OrderType::Attack;
        atk_order.target_entity_id = static_cast<int32_t>(defender_id);
        sim.issue_order(atk_order);

        // The blow throws the defender one tile East to (12, 10)
        const auto& def = sim.get_unit(defender_id);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return def.state == UnitState::Flinch; }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return def.pos.x == 12; }) >= 0);
        ASSERT_EQ(def.pos.y, 10);

        // The attacker does not pursue: it stays on the same tile and in the same row
        run_ms(sim, 3000);
        const auto& atk = sim.get_unit(attacker_id);
        ASSERT_EQ(atk.pos.y, 10);
        ASSERT_EQ(atk.pos.x, 10);
        ASSERT_EQ(def.hp, 9);
    } TEST_END();

    TEST_CASE("12.99: Bomb Dud Burn Animation Cycle (?bu overlay, 1150 ms for a worker, then the stun)") {
        bool tested = false;
        for (uint32_t seed = 1; seed < 100 && !tested; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 60000);

            uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
            auto& unit = sim.get_unit(worker_id);
            sim.grid_mut().place_bomb(15, 15, 1);
            sim.trigger_bomb_detonation(worker_id, TileCoord{15, 15});
            if (!unit.knock_flag) continue;   // a real blast
            tested = true;

            // The dud freezes the ant under the burn overlay; the world snapshot tells the renderer how long it burns
            ASSERT_TRUE(unit.frozen);
            ASSERT_EQ(unit.hp, 8);

            // The overlay clip (11 frames, 1150 ms) plays; the ant is frozen all along
            run_ms(sim, 1100);
            ASSERT_TRUE(unit.frozen);
            run_ms(sim, 100);

            // The end of the burn frees the ant and stuns it (a3 stun clip)
            ASSERT_FALSE(unit.frozen);
            ASSERT_EQ(unit.state, UnitState::Stunned);
            ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return unit.state == UnitState::Idle; }) >= 0);
        }
        ASSERT_TRUE(tested);
    } TEST_END();

    TEST_CASE("12.100: 8-Directional Diagonal Attack Adjacency & Approach Routing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Attacker already diagonally adjacent at (10, 10) to defender at (11, 11)
        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 11});

        AntOrder atk_order{};
        atk_order.ant_id = attacker_id;
        atk_order.type = OrderType::Attack;
        atk_order.target_entity_id = static_cast<int32_t>(defender_id);
        sim.issue_order(atk_order);

        // The attack order walks one step onto the defender's tile: the contact makes the attacker face it
        const auto& atk = sim.get_unit(attacker_id);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return atk.state == UnitState::Attacking; }) >= 0);
        ASSERT_EQ(atk.facing, Direction::SouthEast);
        ASSERT_EQ(atk.pos, (TileCoord{10, 10}));
        ASSERT_EQ(sim.get_unit(defender_id).hp, 9);

        // 2. Attacker commanded to attack from a distance (8, 8): it walks up to a neighbour tile first
        SimulationEngine sim2;
        sim2.init_test_world(60, 60, 100, 60000);
        uint32_t defender2_id = sim2.spawn_unit(1, AntType::Worker, TileCoord{11, 11});
        uint32_t distant_atk_id = sim2.spawn_unit(0, AntType::Worker, TileCoord{8, 8});
        AntOrder dist_order{};
        dist_order.ant_id = distant_atk_id;
        dist_order.type = OrderType::Attack;
        dist_order.target_entity_id = static_cast<int32_t>(defender2_id);
        sim2.issue_order(dist_order);

        const auto& dist_unit = sim2.get_unit(distant_atk_id);
        ASSERT_TRUE(wait_ms(sim2, 3000, [&]() { return dist_unit.state == UnitState::Walking; }) >= 0);
        ASSERT_EQ(sim2.get_unit(defender2_id).hp, 10);   // nothing is hit while it walks
        ASSERT_TRUE(wait_ms(sim2, 6000, [&]() { return sim2.get_unit(defender2_id).hp < 10; }) >= 0);
        // The blow came from a tile next to the defender (Chebyshev distance 1)
        ASSERT_TRUE(dist_unit.pos.chebyshev_dist(TileCoord{11, 11}) == 1);
    } TEST_END();

    TEST_CASE("12.101: Complete Attack Animation Playback Across Full Duration (a?at clip, 6 frames)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.execute_melee_attack(attacker_id, defender_id);

        const auto& atk = sim.get_unit(attacker_id);
        ASSERT_EQ(atk.state, UnitState::Attacking);
        ASSERT_EQ(atk.loco_action, AntUnit::kActionAttack);
        ASSERT_EQ(atk.facing, Direction::East);

        // The attack clip of a worker (6 frames) plays; its length is the sum of the frame durations
        const auto clip = movement::action_clip(movement::ActionClip::Attack, static_cast<uint8_t>(AntType::Worker), 2, false);
        ASSERT_TRUE(clip.valid());
        ASSERT_EQ(clip.count, 6u);
        const int clip_ms = static_cast<int>(clip.total_duration_ms());

        // The attacker stays in the attack action for the whole clip (the first frame is booked twice), then idles
        const int done = wait_ms(sim, 3000, [&]() { return sim.get_unit(attacker_id).state != UnitState::Attacking; });
        ASSERT_TRUE(done >= clip_ms - 100);
        ASSERT_TRUE(done <= clip_ms + 150);
        ASSERT_EQ(sim.get_unit(attacker_id).state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("12.102: 1-Tile Pushback Slide Follows The gh Clip (24 px at 300 ms, 8 px at 400 ms after the contact)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.execute_melee_attack(attacker_id, defender_id);

        const auto& def = sim.get_unit(defender_id);
        // At the contact the victim stands still on its tile facing the attacker
        ASSERT_EQ(def.pixel_x, 11 * 32 + 16); // 368
        ASSERT_NE(def.state, UnitState::Flinch);

        // Strike frame (attack clip event 4, 200 ms): the gh clip starts, the ant has not moved yet
        for (int i = 0; i < 4; ++i) sim.tick();
        ASSERT_EQ(def.state, UnitState::Flinch);
        ASSERT_EQ(def.pixel_x, 368);

        // The clip moves the ant in two steps (frame displacements of the table): 24 px at 300 ms, 8 px at 400 ms
        sim.tick(); sim.tick();
        ASSERT_EQ(def.pixel_x, 392);
        sim.tick(); sim.tick();
        ASSERT_EQ(def.pixel_x, 400); // 12 * 32 + 16, the landing tile centre

        // The ant stays at 400 px while the remaining recovery frames play, then it is idle again
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return def.state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(def.pixel_x, 400);
    } TEST_END();

    TEST_CASE("12.103: TINY.LVL Tile (18, 17) Passability & 10-Tick Bounce Parity") {
        // Part A: Tile (18, 17) passability on authentic TINY.LVL (Layer 2 broken2 flat debris)
        std::string tiny_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL";
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_from_file(tiny_path));

        SimulationEngine sim;
        sim.init(lvl, 42);

        // The remake draws broken2 as flat debris (no obstacle overlay), but tile (18, 17) carries the layer-1
        // solid bit in TINY.LVL (it is part of the footprint of the broken2 object anchored at (18, 15)), and
        // the original's CanEnter / StepCost (FUN_0100cf0f) never let an ant onto it.
        ASSERT_TRUE(sim.grid().in_bounds(18, 17));
        const auto& cell = sim.grid().get_cell(18, 17);
        ASSERT_FALSE(cell.is_obstacle_overlay);
        ASSERT_TRUE(cell.is_passable());
        ASSERT_TRUE(sim.grid().is_solid_object(TileCoord{18, 17}));

        // Ordering an ant onto it from neighbour (18, 18) sends it to the first enterable tile of the ring scan
        uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{18, 18});
        ASSERT_TRUE(ant > 0);
        sim.issue_move_order(ant, TileCoord{18, 17});
        ASSERT_EQ(sim.get_unit(ant).final_dest, (TileCoord{17, 16}));
        bool entered = false;
        for (int i = 0; i < 60; ++i) {
            sim.tick();
            if (sim.get_unit(ant).pos == TileCoord{18, 17}) entered = true;
        }
        ASSERT_FALSE(entered);
        ASSERT_EQ(sim.get_unit(ant).pos, (TileCoord{17, 16}));

        // Part B: an ant that lands on an occupied tile causes a pile-up (Blast 0, 7): both ants are thrown to distinct free
        // neighbours with the gh clip; the dust ball (Cloud54) shows for ants that are not the viewer's own (here player 1's)
        SimulationEngine sim_pile;
        sim_pile.init_test_world(60, 60, 100, 60000);
        uint32_t hitter = sim_pile.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t ant1 = sim_pile.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        uint32_t ant2 = sim_pile.spawn_unit(1, AntType::Worker, TileCoord{12, 10});
        sim_pile.execute_melee_attack(hitter, ant1);

        bool found_battle = false;
        bool dispersed = false;
        for (int i = 0; i < 80; ++i) {
            sim_pile.tick();
            for (const auto& eff : sim_pile.get_world_state().effects) {
                if (eff.anim_name == "battle") found_battle = true;
            }
            const auto& u1 = sim_pile.get_unit(ant1);
            const auto& u2 = sim_pile.get_unit(ant2);
            if (u1.state == UnitState::Flinch && u2.state == UnitState::Flinch && !(u1.pos == u2.pos)) dispersed = true;
        }
        ASSERT_TRUE(found_battle);
        ASSERT_TRUE(dispersed);
        ASSERT_FALSE(sim_pile.get_unit(ant1).pos == sim_pile.get_unit(ant2).pos);
    } TEST_END();

    TEST_CASE("12.74b: The Dust Ball Belongs To The Pile-Up Block Alone: A Foreign Ant Thrown Onto A Fire Wall Hops And Loses 1 hp Without It (Ants.exe 0x101bbdd - 0x101bbf4: the fire wall block runs for the viewer's own ants only)") {
        auto battle_cloud_seen = [](SimulationEngine& sim, uint32_t ticks) {
            bool seen = false;
            for (uint32_t t = 0; t < ticks; ++t) {
                sim.tick();
                for (const auto& eff : sim.get_world_state().effects) if (eff.anim_name == "battle") seen = true;
            }
            return seen;
        };
        // a fire wall under player 1's ant, the viewer is player 0: Blast(1, owner) runs without the dust ball, the ant hops and loses 1 hp
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            sim.set_viewing_player_id(0);
            sim.grid_mut().place_firewall(15, 15, 1);
            const uint32_t foreign = sim.spawn_unit(1, AntType::Worker, {15, 15});
            sim.resolve_fire_contact(foreign, 1, 0);
            ASSERT_EQ(sim.get_unit(foreign).hp, 9);
            ASSERT_EQ(sim.get_unit(foreign).state, UnitState::Flinch);
            ASSERT_FALSE(battle_cloud_seen(sim, 60));
            ASSERT_FALSE(sim.has_audio_event(SoundID::CombatNetFairy));
        }
        // and the viewer's own ant makes none either
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            sim.set_viewing_player_id(0);
            sim.grid_mut().place_firewall(15, 15, 1);
            const uint32_t own = sim.spawn_unit(0, AntType::Worker, {15, 15});
            sim.resolve_fire_contact(own, 1, 0);
            ASSERT_FALSE(battle_cloud_seen(sim, 60));
        }
        // the pile-up block keeps it for ants that are not the viewer's own
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            sim.set_viewing_player_id(0);
            const uint32_t a = sim.spawn_unit(1, AntType::Worker, {30, 30});
            sim.spawn_unit(2, AntType::Worker, {30, 30});
            (void)a;
            sim.blast_tile_for_test(TileCoord{30, 30});
            bool seen = false;
            for (const auto& eff : sim.get_world_state().effects) if (eff.anim_name == "battle") seen = true;
            ASSERT_TRUE(seen || battle_cloud_seen(sim, 5));
        }
    } TEST_END();

    TEST_CASE("12.104: Pile-Up Dispersal Never Hides An Ant; Hazard Landings Of The Thrown Ants (water, bomb)") {
        // Part A: no ant is ever concealed (the original has no scuffle ball): all ants stay visible while thrown apart
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            uint32_t hitter = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 20});
            uint32_t a1 = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
            uint32_t a2 = sim.spawn_unit(1, AntType::Worker, TileCoord{21, 20});
            sim.execute_melee_attack(hitter, a1);

            for (int t = 0; t < 80; ++t) {
                sim.tick();
                // no ant is ever hidden: every living ant of the pile-up stays in the world snapshot
                const auto& ws = sim.get_world_state();
                int visible = 0;
                for (const auto& snap : ws.ants) {
                    if (snap.id == hitter || snap.id == a1 || snap.id == a2) ++visible;
                }
                ASSERT_EQ(visible, 3);
            }
            ASSERT_FALSE(sim.get_unit(a1).pos == sim.get_unit(a2).pos);
        }

        // Part B: Water Landing - the ant thrown by a pile-up into water: non-swimmer drowns, swimmer splashes
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Surround tile (30, 30) with rock obstacles except north tile (30, 29) which is water
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    TileCoord c{30 + dx, 30 + dy};
                    if (dx == 0 && dy == -1) {
                        sim.grid_mut().set_terrain(c.x, c.y, TERRAIN_WATER);
                    } else {
                        sim.grid_mut().set_terrain(c.x, c.y, TERRAIN_OBSTACLE);
                    }
                }
            }

            // Two ants on (30, 30): the blast (Blast 0) throws each into the only free tile: the water north of them
            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
            sim.blast_tile_for_test(TileCoord{30, 30});

            // The blast throws the ant onto its only free neighbour, the water tile north of it: the landing drowns it
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(a1).state == UnitState::Drowning; }) >= 0);
            ASSERT_EQ(sim.get_unit(a1).pos, (TileCoord{30, 29}));
            run_ms(sim, 250);
            ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
            ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        }

        {
            // A swimmer thrown into the water splashes and survives
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    TileCoord c{30 + dx, 30 + dy};
                    if (dx == 0 && dy == -1) {
                        sim.grid_mut().set_terrain(c.x, c.y, TERRAIN_WATER);
                    } else {
                        sim.grid_mut().set_terrain(c.x, c.y, TERRAIN_OBSTACLE);
                    }
                }
            }

            uint32_t a1 = sim.spawn_unit(0, AntType::Swimmer, TileCoord{30, 30});
            (void)a1;
            sim.blast_tile_for_test(TileCoord{30, 30});
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(a1).pos == TileCoord{30, 29} &&
                                                          sim.get_unit(a1).state == UnitState::Stunned; }) >= 0);
            ASSERT_TRUE(sim.get_unit(a1).in_water);
            ASSERT_TRUE(sim.get_unit(a1).hp > 0);
        }

        // Part C: Bomb Detonation on the landing tile of a thrown ant
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Surround tile (40, 40) with obstacles except east tile (41, 40) which has a bomb
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    TileCoord c{40 + dx, 40 + dy};
                    if (dx == 1 && dy == 0) {
                        sim.grid_mut().place_bomb(static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y), 1);
                    } else {
                        sim.grid_mut().set_terrain(c.x, c.y, TERRAIN_OBSTACLE);
                    }
                }
            }

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{40, 40});
            sim.blast_tile_for_test(TileCoord{40, 40});

            // The bomb goes off when the flight ends on it (the bomb block comes first at the landing)
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !sim.grid().has_bomb_at(TileCoord{41, 40}); }) >= 0);
            ASSERT_TRUE(sim.has_audio_event(SoundID::BombDetonate));
            // The ant that landed on the bomb took the 2 hits of the blast
            ASSERT_EQ(sim.get_unit(a1).hp, 8);
        }
    } TEST_END();

    TEST_CASE("12.105: Authentic Attack Audio Sequencing, Sound 75/78/79/83/57 From The Attack Clip & Flinch FlyThump Events") {
        // Part A: Worker Ant Melee Attack Sound Sequence: the sound of the attack clip (75) plays at its frame 1,
        // the victim's gh clip plays 64 when it starts (strike frame) and 65 at its landing frame
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
            uint32_t a2 = sim.spawn_unit(1, AntType::Worker, TileCoord{21, 20});
            // Ensure target has enough HP to flinch and survive
            sim.get_unit(a2).hp = 4;

            sim.execute_melee_attack(a1, a2); // Contact
            ASSERT_FALSE(sim.has_audio_event(SoundID::AttackAlt));   // the clip has not reached its sound frame
            ASSERT_FALSE(sim.has_audio_event(SoundID::FlingThumpA));

            // 1. The attack clip plays AttackAlt (Sound 75) at its second frame
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.has_audio_event(SoundID::AttackAlt); }) >= 0);

            // 2. Target enters Flinch and triggers FlingThumpA (Sound 64 / flythumpa.wav) at launch
            const auto& victim = sim.get_unit(a2);
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return victim.state == UnitState::Flinch; }) >= 0);
            ASSERT_TRUE(sim.has_audio_event(SoundID::FlingThumpA));

            // 3. The clip triggers FlingThumpB (Sound 65 / flythumpb.wav) at its landing frame (500 ms after the contact)
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.has_audio_event(SoundID::FlingThumpB); }) >= 0);
            ASSERT_EQ(sim.get_unit(a2).pos, (TileCoord{22, 20}));
        }

        // Part B: Swimmer and Thief Type-Specific Attack Sound Verification (sound of the attacker's clip)
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
            uint32_t target1 = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
            sim.get_unit(target1).hp = 4;
            sim.execute_melee_attack(swimmer, target1);
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.has_audio_event(SoundID::WaterAttack); }) >= 0); // Sound 79 (waterattack.wav)

            uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{15, 10});
            uint32_t target2 = sim.spawn_unit(1, AntType::Worker, TileCoord{16, 10});
            sim.get_unit(target2).hp = 4;
            sim.execute_melee_attack(thief, target2);
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.has_audio_event(SoundID::ThiefWhip); }) >= 0); // Sound 83 (theifwhip.wav)
        }

        // Part C: Combat Ant Heavy Punch Knockback: 78 in the punch clip, 64 / 65 in the gb flight, no stun after it
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
            uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{31, 30});
            sim.get_unit(victim).hp = 4;

            sim.execute_melee_attack(combat, victim); // Combat punch connects
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.has_audio_event(SoundID::HeavyPunch) &&
                                                          sim.has_audio_event(SoundID::FlingThumpA); }) >= 0);

            // The flight lands: FlingThumpB (65); the ant is thrown 4 tiles East and stands up again (no stun state)
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.has_audio_event(SoundID::FlingThumpB); }) >= 0);
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(victim).state == UnitState::Idle; }) >= 0);
            ASSERT_EQ(sim.get_unit(victim).pos, (TileCoord{35, 30}));
        }
    } TEST_END();

    TEST_CASE("12.106: Multi-Directional Attack Registration, Victim Facing & No Re-Collision Loop After The Blow") {
        // 1. Attack registration from all 8 directions against victim facing away:
        // When attacked, regardless of which direction victim was facing, victim takes damage and turns to face attacker!
        struct AttackDirectionTest {
            int32_t att_dx;
            int32_t att_dy;
            Direction victim_initial_facing;
            Direction expected_victim_turned_facing;
        };

        std::vector<AttackDirectionTest> dirs = {
            {-1,  0, Direction::North, Direction::West},       // Attacker to West, victim faces North -> turns West
            { 1,  0, Direction::North, Direction::East},       // Attacker to East, victim faces North -> turns East
            { 0, -1, Direction::South, Direction::North},      // Attacker to North, victim faces South -> turns North
            { 0,  1, Direction::North, Direction::South},      // Attacker to South, victim faces North -> turns South
            {-1, -1, Direction::South, Direction::NorthWest},  // Attacker NorthWest, victim faces South -> turns NorthWest
            { 1, -1, Direction::South, Direction::NorthEast},  // Attacker NorthEast, victim faces South -> turns NorthEast
            {-1,  1, Direction::North, Direction::SouthWest},  // Attacker SouthWest, victim faces North -> turns SouthWest
            { 1,  1, Direction::North, Direction::SouthEast},  // Attacker SouthEast, victim faces North -> turns SouthEast
        };

        for (const auto& d : dirs) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            TileCoord victim_pos{20, 20};
            TileCoord att_pos{20 + d.att_dx, 20 + d.att_dy};

            uint32_t att = sim.spawn_unit(0, AntType::Worker, att_pos);
            uint32_t vic = sim.spawn_unit(1, AntType::Combat, victim_pos);

            sim.get_unit(vic).facing = d.victim_initial_facing;
            uint32_t initial_hp = sim.get_unit(vic).hp;

            // Worker strikes victim from side / behind:
            sim.execute_melee_attack(att, vic);

            // Victim took damage despite facing away:
            ASSERT_LT(sim.get_unit(vic).hp, initial_hp);

            // Victim turned and is now facing directly towards the attacker:
            ASSERT_EQ(sim.get_unit(vic).facing, d.expected_victim_turned_facing);
        }

        // 2. Attack order from the side / rear: the attacker walks up, the contact comes with the step into the tile
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Friendly attacker at (18, 20), enemy stationary at (20, 20) facing South
            uint32_t att = sim.spawn_unit(0, AntType::Worker, TileCoord{18, 20});
            uint32_t vic = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
            sim.get_unit(vic).facing = Direction::South;

            AntOrder order;
            order.ant_id = att;
            order.type = OrderType::Attack;
            order.target_x = 20;
            order.target_y = 20;
            order.target_entity_id = static_cast<int32_t>(vic);
            sim.issue_order(order);

            uint32_t initial_vic_hp = sim.get_unit(vic).hp;
            bool hit_connected = false;
            for (int t = 0; t < 100; ++t) {
                sim.tick();
                const auto& att_u = sim.get_unit(att);
                if (att_u.state == UnitState::Walking) {
                    // While walking, no damage can be dealt mid-stride
                    ASSERT_EQ(sim.get_unit(vic).hp, initial_vic_hp);
                }
                if (sim.get_unit(vic).hp < initial_vic_hp) {
                    hit_connected = true;
                    // Strike connects only after completing the walk into the adjacent tile (19, 20)
                    ASSERT_EQ(att_u.pos, (TileCoord{19, 20}));
                    break;
                }
            }
            ASSERT_TRUE(hit_connected);
            // Victim turned West towards attacker
            ASSERT_EQ(sim.get_unit(vic).facing, Direction::West);
        }

        // 3. After a blow both ants settle: nothing collides again and no order stays behind
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{25, 25});
            uint32_t a2 = sim.spawn_unit(1, AntType::Worker, TileCoord{26, 25});
            sim.execute_melee_attack(a1, a2);

            // Both ants must settle within 4 s without any re-collision loop
            run_ms(sim, 4000);
            ASSERT_EQ(sim.get_unit(a1).state, UnitState::Idle);
            ASSERT_EQ(sim.get_unit(a2).state, UnitState::Idle);
            ASSERT_FALSE(sim.get_unit(a2).engaged);
            ASSERT_EQ(sim.get_unit(a1).orig_order, AntUnit::kOrderNone);
            ASSERT_EQ(sim.get_unit(a2).orig_order, AntUnit::kOrderNone);
            ASSERT_EQ(sim.get_unit(a2).hp, 9);   // only the one blow
        }
    } TEST_END();

    TEST_CASE("12.107: Lethal Food Drops & Grab Food Action Mapping") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});

        // (The healing time of the enter clip and the deposit at its end are golden-tested in test_hill_actions.)

        // 1. Lethal Melee Damage Drops Carried Lunchbox (at the removal of the ant, after its death clip):
        uint32_t attacker = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{31, 30});
        auto& v = sim.get_unit(victim);
        v.hp = 2; // Combat punch does 2 damage -> lethal
        v.pick_up_food(1, 50);

        ASSERT_FALSE(sim.grid().has_lunchbox_at(TileCoord{31, 30}));
        sim.execute_melee_attack(attacker, victim);

        // Death is deferred: hp is 0 at the contact, the victim is still thrown 4 tiles East and carries the food
        ASSERT_EQ(v.hp, 0u);
        ASSERT_TRUE(v.is_holding());
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return v.state == UnitState::Dead; }) >= 0);
        ASSERT_TRUE(v.is_holding());
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !v.is_alive(); }) >= 0);
        ASSERT_FALSE(v.is_holding());
        ASSERT_TRUE(sim.grid().has_lunchbox_at(TileCoord{35, 30}));
        ASSERT_EQ(sim.grid().get_lunchbox_points(TileCoord{35, 30}), 50u);

        // 2. Food Harvesting Sequence Action Verification
        uint32_t harvester = sim.spawn_unit(0, AntType::Worker, TileCoord{40, 40});
        auto& h = sim.get_unit(harvester);
        h.state = UnitState::HarvestingFood;
        h.anim_tick = 3;
        const auto& ws = sim.get_world_state();
        const AntSnapshot* snap = nullptr;
        for (const auto& a : ws.ants) {
            if (a.id == harvester) { snap = &a; break; }
        }
        ASSERT_TRUE(snap != nullptr);
        ASSERT_EQ(snap->anim_state, static_cast<uint16_t>(UnitState::HarvestingFood));
    } TEST_END();

    TEST_CASE("12.108: Version Format, Build Id & Fog of War Cursor Concealment Parity") {
        // 1. Verify the FORMAT of the version, not its value. The value lives in one place, the file VERSION (CMake generates ants_app/version.hpp from it) and moves for a
        // batch or a milestone only (docs/WORKFLOW.md), so a test that pinned "v0.1.0" failed at every release for no reason. What stays pinned: the text is
        // "vMAJOR.MINOR.PATCH" (digits only, no leading zeros of a longer number, nothing after the third number), the three numbers agree with it, and the build id names the build.
        {
            const std::string text(ants::VERSION_STRING);
            ASSERT_TRUE(text.size() >= 6);
            ASSERT_EQ(text[0], 'v');
            int numbers[3] = {0, 0, 0};
            int field = 0;
            bool digit_seen = false;
            bool format_ok = true;
            for (size_t i = 1; i < text.size(); ++i) {
                const char c = text[i];
                if (c >= '0' && c <= '9') {
                    if (digit_seen && numbers[field] == 0) format_ok = false;     // "01": a leading zero
                    numbers[field] = numbers[field] * 10 + (c - '0');
                    if (numbers[field] > 100000) format_ok = false;
                    digit_seen = true;
                } else if (c == '.' && digit_seen && field < 2) {
                    ++field;
                    digit_seen = false;
                } else {
                    format_ok = false;
                }
            }
            ASSERT_TRUE(format_ok);
            ASSERT_EQ(field, 2);
            ASSERT_TRUE(digit_seen);                                            // not "v1.2." and not "v1.2"
            ASSERT_EQ(numbers[0], ants::VERSION_MAJOR);
            ASSERT_EQ(numbers[1], ants::VERSION_MINOR);
            ASSERT_EQ(numbers[2], ants::VERSION_PATCH);
            ASSERT_TRUE(ants::VERSION_MAJOR >= 0 && ants::VERSION_MINOR >= 0 && ants::VERSION_PATCH >= 0);

            const std::string build(ants::BUILD_ID);                            // a short commit, the Docker build argument, or "unknown": never empty
            ASSERT_TRUE(!build.empty());
            ASSERT_TRUE(build.size() <= 40);
            for (const char c : build) {
                const bool allowed = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '_';
                ASSERT_TRUE(allowed);
            }
        }

        // 2. Setup simulation world with Fog of War enabled
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_fog_of_war_enabled(true);
        sim.set_viewing_player_id(0);

        // Spawn friendly unit at (10, 10)
        uint32_t friendly_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        // Spawn enemy unit deep in fog at (50, 50)
        uint32_t enemy_id = sim.spawn_unit(1, AntType::Worker, TileCoord{50, 50});
        (void)enemy_id;
        // Place food deep in fog at (52, 52)
        sim.grid_mut().drop_lunchbox(52, 52, 100);

        // Tick once to update fog reveals around (10, 10)
        sim.tick();
        const auto& ws = sim.get_world_state();
        ASSERT_TRUE(ws.fog_of_war_enabled);
        ASSERT_TRUE(ws.is_tile_revealed(10, 10));
        ASSERT_FALSE(ws.is_tile_revealed(50, 50));
        ASSERT_FALSE(ws.is_tile_revealed(52, 52));

        HUD hud;
        hud.init(0);
        ViewportCamera cam;
        cam.world_x = 0;
        cam.world_y = 0;

        // Position camera so (50, 50) and (52, 52) are cleanly on screen
        cam.world_x = 50 * 32 - 100;
        cam.world_y = 50 * 32 - 100;

        int32_t enemy_screen_x = HUD::PLAYFIELD_X + (50 * 32 + 16) - cam.world_x;
        int32_t enemy_screen_y = HUD::PLAYFIELD_Y + (50 * 32 + 16) - cam.world_y;

        int32_t food_screen_x = HUD::PLAYFIELD_X + (52 * 32 + 16) - cam.world_x;
        int32_t food_screen_y = HUD::PLAYFIELD_Y + (52 * 32 + 16) - cam.world_y;

        // Case A: Friendly unit is selected
        hud.select_ant(friendly_id, false);
        // Hovering over enemy in fog returns Move (c_mov1), NOT Attack (c_attack)!
        CursorType cur_enemy = hud.evaluate_cursor(enemy_screen_x, enemy_screen_y, ws, sim.grid(), cam);
        ASSERT_EQ(static_cast<int>(cur_enemy), static_cast<int>(CursorType::Move));

        // Hovering over food in fog returns Move (c_mov1), NOT Food (c_food)!
        CursorType cur_food = hud.evaluate_cursor(food_screen_x, food_screen_y, ws, sim.grid(), cam);
        ASSERT_EQ(static_cast<int>(cur_food), static_cast<int>(CursorType::Move));

        // Case B: No units selected
        hud.clear_selection();
        // Hovering over enemy in fog returns Normal (c_normal), NOT Select (c_select)!
        CursorType cur_no_sel = hud.evaluate_cursor(enemy_screen_x, enemy_screen_y, ws, sim.grid(), cam);
        ASSERT_EQ(static_cast<int>(cur_no_sel), static_cast<int>(CursorType::Normal));

        // Case C: When Fog of War is disabled, hovering over enemy returns Attack (when unit selected)
        sim.set_fog_of_war_enabled(false);
        hud.select_ant(friendly_id, false);
        const auto& ws_no_fog = sim.get_world_state();
        CursorType cur_revealed = hud.evaluate_cursor(enemy_screen_x, enemy_screen_y, ws_no_fog, sim.grid(), cam);
        ASSERT_EQ(static_cast<int>(cur_revealed), static_cast<int>(CursorType::Attack));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.109: Mud Animation Cancel ("Mud Humping") Speedup vs Passive Walking
    // ------------------------------------------------------------------------
    TEST_CASE("12.109 Authentic Mud \"Humping\": Re-Ordering Just After Each Tile Boundary Beats Passive Walking") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Mud terrain corridors on row 10 and row 14 from col 10 to 25 bordered by obstacles
        for (int x = 9; x <= 25; ++x) {
            sim.grid_mut().get_cell_mut(TileCoord{x, 9}).terrain_type = TERRAIN_OBSTACLE;
            sim.grid_mut().get_cell_mut(TileCoord{x, 11}).terrain_type = TERRAIN_OBSTACLE;
            sim.grid_mut().get_cell_mut(TileCoord{x, 13}).terrain_type = TERRAIN_OBSTACLE;
            sim.grid_mut().get_cell_mut(TileCoord{x, 15}).terrain_type = TERRAIN_OBSTACLE;
        }
        for (int x = 10; x <= 25; ++x) {
            sim.grid_mut().get_cell_mut(TileCoord{x, 10}).surface_type = SurfaceType::Mud;
            sim.grid_mut().get_cell_mut(TileCoord{x, 10}).is_mud = true;
            sim.grid_mut().get_cell_mut(TileCoord{x, 14}).surface_type = SurfaceType::Mud;
            sim.grid_mut().get_cell_mut(TileCoord{x, 14}).is_mud = true;
        }

        uint32_t w_passive = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t w_cancel = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 14});

        sim.issue_move_order(w_passive, TileCoord{20, 10});
        sim.issue_move_order(w_cancel, TileCoord{20, 14});

        int ticks_passive = 0;
        int ticks_cancel = 0;
        int forward_snaps = 0;

        for (int t = 1; t <= 600; ++t) {
            sim.tick();

            // The exploit: every order snaps the ant to the centre of the tile under it (GoTo, FUN_0101fc50).
            // Right after crossing into the next tile the ant is still up to 16 px short of that tile's centre,
            // so re-ordering then jumps it forward, which outweighs the restart of the walk on slow mud.
            AntUnit& c = sim.get_unit(w_cancel);
            if (ticks_cancel == 0 && c.loco_action == AntUnit::kActionWalk && (c.pixel_x % 32) < 16) {
                const int32_t before = c.pixel_x;
                sim.issue_move_order(w_cancel, TileCoord{20, 14});
                ASSERT_GT(c.pixel_x, before);
                ++forward_snaps;
            }

            if (ticks_passive == 0 && sim.get_unit(w_passive).pos == TileCoord{20, 10} &&
                sim.get_unit(w_passive).state != UnitState::Walking) {
                ticks_passive = t;
            }
            if (ticks_cancel == 0 && sim.get_unit(w_cancel).pos == TileCoord{20, 14} &&
                sim.get_unit(w_cancel).state != UnitState::Walking) {
                ticks_cancel = t;
            }

            if (ticks_passive > 0 && ticks_cancel > 0) break;
        }

        ASSERT_TRUE(ticks_passive > 0);
        ASSERT_TRUE(ticks_cancel > 0);
        ASSERT_TRUE(forward_snaps >= 5);
        // Mud humping completes faster than passive walking across mud
        ASSERT_TRUE(ticks_cancel < ticks_passive);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.110: Power-Up Standing Immunity to Bounce and Melee Attacks
    // ------------------------------------------------------------------------
    TEST_CASE("12.110 Power-Up Standing Immunity: Not Displaced By A Friendly Ant, Immune To Attack Orders And Auto-Engage") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place Bomber power-up at (15, 15); a friendly ant walks onto it and stands on it (the can't-go trick)
        sim.grid_mut().place_powerup(15, 15, static_cast<uint8_t>(AntType::Bomber));
        make_water_island(sim, 40, 40);
        uint32_t standing_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{14, 15});
        ASSERT_TRUE(stand_on_powerup(sim, standing_ant, TileCoord{15, 15}, TileCoord{40, 40}));
        ASSERT_TRUE(sim.grid().has_powerup_at(sim.get_unit(standing_ant).pos));

        // Second friendly ant orders a move onto (15, 15): the goal ring scan picks a free tile
        uint32_t incoming_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{16, 15});
        sim.issue_move_order(incoming_ant, TileCoord{15, 15});
        run_ms(sim, 2000);

        // Standing ant on power-up must NEVER be displaced
        ASSERT_EQ(sim.get_unit(standing_ant).pos, (TileCoord{15, 15}));

        // Enemy Combat ant next to it: the auto-engage walks up but its last step onto the solid power-up tile is refused
        // (CanEnter), and an attack order fails with "Can't go there."
        uint32_t enemy_combat = sim.spawn_unit(1, AntType::Combat, TileCoord{15, 17});
        uint16_t hp_before = sim.get_unit(standing_ant).hp;
        run_ms(sim, 6000);
        ASSERT_EQ(sim.get_unit(standing_ant).hp, hp_before);
        AntOrder o{};
        o.ant_id = enemy_combat;
        o.type = OrderType::Attack;
        o.target_entity_id = static_cast<int32_t>(standing_ant);
        sim.issue_order(o);
        run_ms(sim, 3000);
        ASSERT_EQ(sim.get_unit(standing_ant).hp, hp_before);
        ASSERT_EQ(sim.get_unit(standing_ant).pos, (TileCoord{15, 15}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.111: Occupied Destination Bumping (SoundID::Bump and Stopping Cleanly)
    // ------------------------------------------------------------------------
    TEST_CASE("12.111 Destination Taken While Walking: Stop One Tile Short Without a Bump (TryEnterTile)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Moving ant approaches towards destination (15, 15)
        uint32_t mover = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 15});
        sim.issue_move_order(mover, TileCoord{15, 15});
        tick_until_paths_delivered(sim, {mover});

        // Later, blocker occupies destination (15, 15)
        uint32_t blocker = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
        sim.get_unit(blocker).state = UnitState::Idle;

        bool bumped = false;
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::Bump)) {
                bumped = true;
            }
            if (sim.get_unit(mover).state == UnitState::Idle && sim.get_unit(mover).pos != TileCoord{12, 15}) {
                break;
            }
        }

        // A taken final tile makes the walker stop where it is (StopSync at 0x101caf4); only a re-plan around
        // an obstacle shows the bump effect. The mover waits on the tile next to its destination.
        ASSERT_FALSE(bumped);
        ASSERT_EQ(sim.get_unit(mover).state, UnitState::Idle);
        ASSERT_TRUE(sim.get_unit(mover).pos.chebyshev_dist(TileCoord{15, 15}) == 1);
        ASSERT_TRUE(sim.get_unit(mover).waypoints.empty());
        ASSERT_EQ(sim.get_unit(blocker).pos, (TileCoord{15, 15}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.112: Anthill 3-Tile Ability Block and Occupied Tile Ability Block
    // ------------------------------------------------------------------------
    TEST_CASE("12.112 Anthill 3-Tile Ability Block and Occupied Tile Ability Block") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* ah = sim.grid().find_anthill(0);
        ASSERT_TRUE(ah != nullptr);
        int32_t bx = ah->x;
        int32_t by = ah->y;

        // 1. The original's three tiles that refuse bombs and fire walls next to a hill (HillSpecial, 0x101d858 / 0x101d8a4: the player's
        //    tile pairs +0x36, +0x3a and +0x3e, stored as (row, col)): the three tiles ABOVE the mound, row by - 1, columns bx .. bx + 2
        for (int dx = 0; dx <= 2; ++dx) {
            TileCoord blocked_tile{bx + dx, by - 1};

            // Bomber ant adjacent to blocked tile cannot plant bomb on blocked tile
            uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{blocked_tile.x - 1, blocked_tile.y});
            ASSERT_FALSE(sim.plant_bomb(bomber, blocked_tile));
            ASSERT_FALSE(sim.plant_bomb(bomber, blocked_tile, false));

            // Fire ant adjacent to blocked tile cannot ignite fire on blocked tile
            uint32_t fire_ant = sim.spawn_unit(0, AntType::Fire, TileCoord{blocked_tile.x - 1, blocked_tile.y});
            ASSERT_FALSE(sim.ignite_fire(fire_ant, blocked_tile));
            ASSERT_FALSE(sim.ignite_fire(fire_ant, blocked_tile, false));
        }

        // The three tiles two columns to the left of the mound, (bx - 2, by - 1 .. by + 1), were refused by the remake until v0.0.53
        // (the same data read with rows and columns swapped); the original has no such rule: they are ordinary ground
        for (int dy = -1; dy <= 1; ++dy) {
            TileCoord open_tile{bx - 2, by + dy};
            uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{open_tile.x - 1, open_tile.y});
            ASSERT_TRUE(sim.plant_bomb(bomber, open_tile, false));
            uint32_t fire_ant = sim.spawn_unit(0, AntType::Fire, TileCoord{open_tile.x - 1, open_tile.y + 3});
            ASSERT_TRUE(sim.ignite_fire(fire_ant, TileCoord{open_tile.x, open_tile.y + 3}, false));
        }

        // 2. Tile occupied by any living ant cannot have a bomb or fire planted on it
        TileCoord occ{30, 30};
        uint32_t occupant = sim.spawn_unit(0, AntType::Worker, occ);
        sim.tick();
        ASSERT_TRUE(sim.has_living_ant_at(occ));

        uint32_t b2 = sim.spawn_unit(0, AntType::Bomber, TileCoord{29, 30});
        ASSERT_FALSE(sim.plant_bomb(b2, occ));
        ASSERT_FALSE(sim.plant_bomb(b2, occ, false));

        uint32_t f2 = sim.spawn_unit(0, AntType::Fire, TileCoord{29, 30});
        ASSERT_FALSE(sim.ignite_fire(f2, occ));
        ASSERT_FALSE(sim.ignite_fire(f2, occ, false));
        (void)occupant;
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.113: Base Queue Mound Ramp Concurrency Limit (Max 2 Ants)
    // ------------------------------------------------------------------------

    // ------------------------------------------------------------------------
    // 12.114: Water Melee Combat Isolation and Emergence Invulnerability
    // ------------------------------------------------------------------------
    TEST_CASE("12.114 Water Melee Combat Isolation and Hatch Clip Refusal") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Part A: an ant that plays the hatch clip cannot be attacked (the melee refusal list holds actions 2 and 0x14),
        // but there is no invulnerability after the clip (the original has none)
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 5);
        sim.set_hatch_delay_ticks(2);
        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
        uint32_t baby = 0;
        for (int t = 0; t < 10 && baby == 0; ++t) {
            sim.tick();
            for (const auto& a : sim.get_world_state().ants) {
                if (a.player_id == 0) baby = a.id;
            }
        }
        ASSERT_TRUE(baby != 0);
        ASSERT_EQ(sim.get_unit(baby).state, UnitState::EnteringBase);
        ASSERT_TRUE(sim.get_unit(baby).in_hill_action());

        // Enemy Combat ant tries to attack the newborn right after it appeared
        uint32_t enemy = sim.spawn_unit(1, AntType::Combat, TileCoord{21, 22});
        uint16_t baby_hp = sim.get_unit(baby).hp;
        sim.execute_melee_attack(enemy, baby);
        ASSERT_EQ(sim.get_unit(baby).hp, baby_hp);

        // Tick until the newborn has finished the clip
        for (int t = 0; t < 40 && sim.get_unit(baby).state == UnitState::EnteringBase; ++t) sim.tick();
        ASSERT_FALSE(sim.get_unit(baby).in_hill_action());

        // Right after the clip the ant can be hurt like any other
        const TileCoord bp = sim.get_unit(baby).pos;
        uint32_t enemy2 = sim.spawn_unit(1, AntType::Combat, TileCoord{bp.x + 1, bp.y});
        sim.execute_melee_attack(enemy2, baby);
        ASSERT_TRUE(sim.get_unit(baby).hp < baby_hp);

        // Part B: Water Melee Combat Isolation
        sim.grid_mut().set_terrain(10, 10, TERRAIN_WATER);
        uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
        sim.get_unit(swimmer).in_water = true;
        uint32_t land_ant = sim.spawn_unit(1, AntType::Combat, TileCoord{10, 11});

        // Land ant cannot melee attack water ant
        sim.execute_melee_attack(land_ant, swimmer);
        ASSERT_NE(sim.get_unit(land_ant).state, UnitState::Attacking);

        // Water ant cannot melee attack land ant
        sim.execute_melee_attack(swimmer, land_ant);
        ASSERT_NE(sim.get_unit(swimmer).state, UnitState::Attacking);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.115: Blocked Base Emergence 20-Tick Postponement (Ants.exe 0x1025100)
    // ------------------------------------------------------------------------
    TEST_CASE("12.115 Blocked Base Emergence Waits For The Entrance (HATCHTSK re-runs every slot)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* ah = sim.grid().find_anthill(0);
        ASSERT_TRUE(ah != nullptr);
        TileCoord hole{ah->x + 1, ah->y + 1}; // (21, 21)

        // 1. Place a stationary own ant directly on the hole
        uint32_t blocker = sim.spawn_unit(0, AntType::Worker, hole);
        sim.tick();
        ASSERT_TRUE(sim.has_living_ant_at(hole));

        // 2. Hatch a newborn ant
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 5);
        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));

        // 3. Tick through the 8000 ms incubation and some more: the hole is occupied, so no newborn appears
        for (int t = 0; t < 200; ++t) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        size_t own_ants = 0;
        for (const auto& a : sim.get_world_state().ants) {
            if (a.player_id == 0) ++own_ants;
        }
        ASSERT_EQ(own_ants, 1u);

        // 4. Move the blocker away from the hole: the newborn appears as soon as the entrance is free
        TileCoord target{ah->x - 5, ah->y + 3};
        sim.issue_move_order(blocker, target);
        for (int t = 0; t < 60 && sim.get_pending_hatch_count(0) > 0; ++t) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 0u);
        own_ants = 0;
        for (const auto& a : sim.get_world_state().ants) {
            if (a.player_id == 0) ++own_ants;
        }
        ASSERT_EQ(own_ants, 2u);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.116: A* Pathfinding Moving-Ally Passability (Ants.exe 0x10209cd)
    // ------------------------------------------------------------------------
    TEST_CASE("12.116 A* Pathfinding Moving-Ally Passability (Ants.exe 0x10209cd)") {
        SimulationEngine sim;
        sim.init_test_world(40, 40, 100, 60000);

        // Build a narrow 1-tile corridor at y=10 from x=5 to x=25
        // Flank with solid rocks above and below
        for (int x = 5; x <= 25; ++x) {
            sim.grid_mut().set_terrain(x, 9, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(x, 11, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(x, 10, TERRAIN_WALKABLE);
        }

        // Spawn friendly ant 1 marching east inside corridor
        uint32_t ant1 = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 10});
        sim.issue_move_order(ant1, TileCoord{22, 10});
        sim.tick();
        ASSERT_EQ(sim.get_unit(ant1).state, UnitState::Walking);

        // Spawn friendly ant 2 behind ant 1 inside corridor
        uint32_t ant2 = sim.spawn_unit(0, AntType::Worker, TileCoord{8, 10});
        ASSERT_EQ(sim.get_unit(ant2).state, UnitState::Idle);

        // Issue move order to ant 2 targeting (20, 10) ahead of ant 1
        sim.issue_move_order(ant2, TileCoord{20, 10});
        tick_until_paths_delivered(sim, {ant2});

        // Moving ally ant1 is NOT an impassable obstacle in A* routing (Ant::StepCost FUN_01020951: only a
        // team-mate that is waiting, or idle without a path and without an order target, costs 8000).
        // Ant 2 must successfully find a path directly through the corridor!
        ASSERT_EQ(sim.get_unit(ant2).state, UnitState::Walking);
        ASSERT_FALSE(sim.get_unit(ant2).waypoints.empty());
        ASSERT_EQ(sim.get_unit(ant2).final_dest, (TileCoord{20, 10}));
    } TEST_END();

    TEST_CASE("12.117 Thief Raid On A Hill Without Food: Idle And Empty-Handed On The Raid Tile, Still Attackable") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{5, 5});
        sim.grid_mut().set_anthill(1, TileCoord{20, 20});

        // Team 0 has a Thief next to the raid tile (bx + 3, by + 2) = (23, 22) of team 1's hill (0 food points)
        uint32_t thief_id = sim.spawn_unit(0, AntType::Thief, TileCoord{24, 22});
        const auto* enemy_base = sim.grid().find_anthill(1);
        ASSERT_TRUE(enemy_base != nullptr);
        ASSERT_EQ(sim.get_player_score(1), 0);

        // Order Thief to infiltrate Team 1's base
        AntOrder infil_order;
        infil_order.ant_id = thief_id;
        infil_order.type = OrderType::InfiltrateAnthill;
        infil_order.target_x = enemy_base->x + 1;
        infil_order.target_y = enemy_base->y + 1;
        infil_order.target_entity_id = 1;
        sim.issue_order(infil_order);

        // Step simulation until the thief arrives and plays the raid clip (atcr501) on the raid tile centre
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (sim.get_unit(thief_id).state == UnitState::Infiltrating) break;
        }
        ASSERT_EQ(sim.get_unit(thief_id).state, UnitState::Infiltrating);
        ASSERT_EQ(sim.get_unit(thief_id).pos, (TileCoord{enemy_base->x + 3, enemy_base->y + 2}));
        // (the message handler puts the thief on the tile centre, then the last walk step's snap is still added)
        ASSERT_TRUE(std::abs(sim.get_unit(thief_id).pixel_x - ((enemy_base->x + 3) * 32 + 16)) <= 8);
        ASSERT_EQ(sim.get_unit(thief_id).pixel_y, (enemy_base->y + 2) * 32 + 16);

        // The clip lasts 3510 ms (71 ticks)
        for (int t = 0; t < 75; ++t) {
            sim.tick();
        }

        // Empty steal: the victim had 0 food, so the thief got nothing (the loot flag is set nevertheless)
        const auto& thief = sim.get_unit(thief_id);
        ASSERT_EQ(thief.carried_points, 0);
        ASSERT_FALSE(thief.is_holding());
        ASSERT_TRUE(thief.is_thief_steal);

        // Authentic 1998 behavior: with nothing to carry the thief does NOT return to base; it stands idle on the raid tile
        ASSERT_EQ(thief.state, UnitState::Idle);
        ASSERT_EQ(thief.pixel_x, (enemy_base->x + 3) * 32 + 16);
        ASSERT_EQ(thief.pixel_y, (enemy_base->y + 2) * 32 + 16);

        // The raid tile is a cell of the hill mound: A* step cost and CanEnter refuse every hill cell to all ants but the
        // ones heading home, and the owner's click on it is classified as a "go home" order, so nobody can attack the thief
        // there (it is not attackable by melee; the other rules like CanBeAttackedFrom are never reached)
        uint32_t combat_id = sim.spawn_unit(1, AntType::Combat, TileCoord{enemy_base->x + 4, enemy_base->y + 2});
        AntOrder atk_order;
        atk_order.ant_id = combat_id;
        atk_order.type = OrderType::Attack;
        atk_order.target_entity_id = static_cast<int32_t>(thief_id);
        sim.issue_order(atk_order);
        run_ms(sim, 4000);
        ASSERT_EQ(sim.get_unit(thief_id).hp, 10);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.118: 5.0-Second Non-Dismissable Match Start Ready Modal (Ants.exe 0x1017127)
    // ------------------------------------------------------------------------
    TEST_CASE("12.118 5.0-Second Non-Dismissable Match Start Ready Modal (Ants.exe 0x1017127)") {
        HUD hud;
        hud.init(0);
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        ViewportCamera camera;

        // Starts inactive in headless tests until match launch
        ASSERT_FALSE(hud.is_match_start_modal_active());

        // Launch match modal
        hud.start_match_modal();
        ASSERT_TRUE(hud.is_match_start_modal_active());

        // Clicks on playfield are consumed / blocked (non-dismissable hold)
        bool consumed_lmb = hud.handle_mouse_down(200, 200, 1, sim, camera);
        ASSERT_TRUE(consumed_lmb);
        ASSERT_TRUE(hud.is_match_start_modal_active());

        // Keyboard inputs are consumed / blocked
        bool consumed_key = hud.handle_key_down(SDLK_SPACE, sim, camera);
        ASSERT_TRUE(consumed_key);
        ASSERT_TRUE(hud.is_match_start_modal_active());

        // Advance 99 steps of 50 ms (4.95 s: the application steps the HUD in real time while the dialog is up, the simulation waits for it): modal remains active
        WorldState dummy_world{};
        for (uint32_t t = 1; t < 100; ++t) {
            hud.update(dummy_world, 1);
            ASSERT_TRUE(hud.is_match_start_modal_active());
        }

        // At step 100 (exactly 5.0s elapsed: the first run of the original's timer task), modal auto-dismisses
        hud.update(dummy_world, 1);
        ASSERT_FALSE(hud.is_match_start_modal_active());

        // The dialog of a match of the network (start_match_modal(true)) does not close by itself, however long it waits for the first turn: only dismiss_match_start_modal() ends it
        hud.start_match_modal(true);
        ASSERT_TRUE(hud.is_match_start_modal_active());
        for (uint32_t t = 0; t < 1000; ++t) hud.update(dummy_world, 1);
        ASSERT_TRUE(hud.is_match_start_modal_active() && hud.match_start_modal_ticks() == 1000u);
        ASSERT_TRUE(hud.handle_mouse_down(200, 200, 1, sim, camera) && hud.is_match_start_modal_active());            // (and it takes the clicks as long as it is up)
        hud.dismiss_match_start_modal();
        ASSERT_FALSE(hud.is_match_start_modal_active());
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.118b: The Dialog Lasts Exactly sim::kMatchStartDialogMs (100 Steps Of 50 Ms Of HUD Time: The Simulation Waits For It, The Host's Start Delay Is The Same Number)
    // ------------------------------------------------------------------------
    TEST_CASE("12.118b The \"Get Ready\" Dialog Closes On The Very Step That sim::kMatchStartDialogMs Names (Open After 99 Updates, Closed After 100): The Number The Hosts Of A Match Of The Network Wait For Before They Seal The First Turn; A Click On An Own Ant Is Swallowed On Step 99 And Selects On Step 100") {
        ASSERT_EQ(ants::sim::kMatchStartDialogMs, 5000u);                                    // the original's task KWFO (Ants.exe 0x10254b0), first run 5000 ms after the dialog was made
        ASSERT_EQ(HUD::kMatchStartModalSteps * ants::sim::TICK_MS, ants::sim::kMatchStartDialogMs);
        ASSERT_EQ(HUD::kMatchStartModalSteps, 100u);
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        const uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;
        HUD hud;
        hud.init(0);
        hud.start_match_modal();
        WorldState world{};
        const int32_t sx = 176 + HUD::PLAYFIELD_X;                                           // the middle of the ant at tile (5, 5), as test 7.1 clicks it
        const int32_t sy = 176 + HUD::PLAYFIELD_Y;
        for (uint32_t t = 1; t < HUD::kMatchStartModalSteps; ++t) {                          // 99 updates: the dialog is up after each of them
            hud.update(world, 1);
            ASSERT_TRUE(hud.is_match_start_modal_active());
        }
        hud.handle_mouse_down(sx, sy, 1, sim, camera);                                        // the click of step 99 is swallowed whole
        hud.handle_mouse_up(sx, sy, 1, sim, camera);
        ASSERT_TRUE(hud.get_selected_ant_ids().empty() && hud.get_selected_ant_id() == 0u && !hud.is_ant_selected(ant));
        hud.update(world, 1);                                                                 // the 100th: the dialog is gone
        ASSERT_FALSE(hud.is_match_start_modal_active());
        hud.handle_mouse_down(sx, sy, 1, sim, camera);                                        // and the same click selects
        hud.handle_mouse_up(sx, sy, 1, sim, camera);
        ASSERT_EQ(hud.get_selected_ant_id(), ant);
        ASSERT_TRUE(hud.is_ant_selected(ant));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.119: Mutual Friendly Collision Bouncing (Neither Ant Static)
    // ------------------------------------------------------------------------
    TEST_CASE("12.119 A Friendly Ant Hit Into A Friendly Ant: Pile-Up Throws Both Apart (Neither Ant Static)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Friendly Ant 1 at (20, 20), Friendly Ant 2 at (21, 20)
        uint32_t f1 = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        uint32_t f2 = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 20});

        // Enemy Worker at (19, 20) strikes friendly ant 1 eastwards into friendly ant 2
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, TileCoord{19, 20});
        sim.clear_audio_events();
        sim.execute_melee_attack(enemy, f1);

        const auto& u1 = sim.get_unit(f1);
        const auto& u2 = sim.get_unit(f2);
        ASSERT_EQ(u1.hp, 9);

        // The gh flight of f1 ends its landing event (500 ms) on f2's tile (21, 20): both are thrown (Blast 0, 7)
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return u1.state == UnitState::Flinch && u2.state == UnitState::Flinch &&
                                                       u1.pos != u2.pos; }) >= 0);

        // Neither ant stands static: BOTH ants are thrown onto distinct tiles, nobody loses hit points to the pile-up
        ASSERT_EQ(u2.hp, 10);
        ASSERT_TRUE(sim.has_audio_event(SoundID::FlingThumpA));

        // ZERO battle dust cloud effects (no scuffle in the original)
        bool has_battle_cloud = false;
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            for (const auto& eff : sim.get_world_state().effects) {
                if (eff.anim_name == "battle") has_battle_cloud = true;
            }
        }
        ASSERT_FALSE(has_battle_cloud);
        ASSERT_FALSE(u1.pos == u2.pos);

        // No CombatNetFairy sound effect
        ASSERT_FALSE(sim.has_audio_event(SoundID::CombatNetFairy));
    } TEST_END();

    // 12.113: 2 HP Ant Struck into Friendly Ant Triggers Mutual Bounce Before 1 HP Retreat
    TEST_CASE("12.113: 2 HP Ant Struck into Friendly Ant: Both Are Thrown Apart, Then The 1 HP Ant Retreats Home") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.set_anthill(0, {30, 30});

        // Friendly Ant 1 at (20, 20) with 2 HP, Friendly Ant 2 at (21, 20) with 10 HP
        uint32_t f1 = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        uint32_t f2 = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 20});

        auto* u1_mut = const_cast<AntUnit*>(&sim.get_unit(f1));
        u1_mut->hp = 2;

        // Enemy Worker at (19, 20) strikes friendly ant 1 eastwards into friendly ant 2
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, TileCoord{19, 20});
        sim.clear_audio_events();
        sim.execute_melee_attack(enemy, f1);

        const auto& u1 = sim.get_unit(f1);
        const auto& u2 = sim.get_unit(f2);
        // Ant 1 took 1 damage: drops to 1 HP, but it does not run away before the hit is over
        ASSERT_EQ(u1.hp, 1);
        ASSERT_NE(u1.orig_order, AntUnit::kOrderHome);

        // Pile-up on f2's tile: both ants are thrown to distinct tiles
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return u1.state == UnitState::Flinch && u2.state == UnitState::Flinch &&
                                                       u1.pos != u2.pos; }) >= 0);
        ASSERT_FALSE(u1.pos == u2.pos);
        ASSERT_NE(u1.orig_order, AntUnit::kOrderHome);

        // Once the hit is over, Ant 1 with 1 HP is ordered home to heal (FUN_0101dded: the enter order)
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(f1).orig_order == AntUnit::kOrderHome; }) >= 0);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.120: Multi-Ant Food Harvesting & 1998 Duplication Exploit
    // ------------------------------------------------------------------------
    TEST_CASE("12.120 Multi-Ant Food Harvesting & 1998 Duplication Exploit") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // The last unit of a pile: 1 unit of 25 points (the crackers' last stage), anchor (21, 21)
        const int32_t pile = place_pile(sim, 21, 21, 1, 25, {{1, 372}, {0, 0x7FFE}});

        // Two worker ants on opposite sides of the pile
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 20});
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{22, 21});

        // Direct both ants to eat the food
        sim.issue_move_order(w1, TileCoord{21, 21});
        sim.issue_move_order(w2, TileCoord{21, 21});

        // Both ants play the grab clip at the same time: the arrival only looks whether a unit is left (one is)
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() {
            return sim.get_unit(w1).loco_action == AntUnit::kActionHarvest && sim.get_unit(w2).loco_action == AntUnit::kActionHarvest;
        }) >= 0);
        ASSERT_FALSE(sim.get_unit(w1).is_holding());
        ASSERT_FALSE(sim.get_unit(w2).is_holding());
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 1u);

        // At the end of the clips BOTH concurrent biting ants receive 25 points (duplication exploit!): the bite takes
        // min(1, units left) and gives the object's value without a second check
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(w1).is_holding() && sim.get_unit(w2).is_holding(); }) >= 0);
        const auto& u1 = sim.get_unit(w1);
        const auto& u2 = sim.get_unit(w2);
        ASSERT_EQ(u1.carried_food, 1);
        ASSERT_EQ(u1.carried_points, 25u);
        ASSERT_EQ(u2.carried_food, 1);
        ASSERT_EQ(u2.carried_points, 25u);

        // The pile is gone: no units, no tile on the map
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 0u);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{21, 21}), -1);
        ASSERT_EQ(sim.grid().get_cell({21, 21}).interactive_id, TILE_EMPTY);
        ASSERT_EQ(sim.grid().get_cell({20, 20}).interactive_id, TILE_EMPTY);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.121: Ground Lunchbox Multi-Ant Collection and Duplication Exploit
    // ------------------------------------------------------------------------
    TEST_CASE("12.121 Ground Lunchbox Multi-Ant Collection and Duplication Exploit") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place a dropped lunchbox at (15, 15) with 50 points
        sim.grid_mut().drop_lunchbox(15, 15, 50);
        ASSERT_TRUE(sim.has_lunchbox_at(TileCoord{15, 15}));

        // Two worker ants at (14, 15) and (16, 15)
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{14, 15});
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{16, 15});

        // Move both to the dropped lunchbox
        sim.issue_move_order(w1, TileCoord{15, 15});
        sim.issue_move_order(w2, TileCoord{15, 15});

        // Both ants play the grab clip (the lunchbox still has its unit at both arrivals) and take the 50 points each
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return sim.get_unit(w1).is_holding() && sim.get_unit(w2).is_holding(); }) >= 0);

        const auto& u1 = sim.get_unit(w1);
        const auto& u2 = sim.get_unit(w2);

        // Both ants received 50 points before lunchbox is cleared
        ASSERT_TRUE(u1.is_holding());
        ASSERT_EQ(u1.carried_points, 50u);
        ASSERT_TRUE(u2.is_holding());
        ASSERT_EQ(u2.carried_points, 50u);

        // Lunchbox is cleared
        ASSERT_FALSE(sim.has_lunchbox_at(TileCoord{15, 15}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.122: Combat Ant AI Smooth Locomotion, Cooldown Enforcement & Physics Knockback
    // ------------------------------------------------------------------------
    TEST_CASE("12.122 Combat Ant Auto-Engage: Approach, Punch, Knockback And Return To The Saved Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Friendly Combat Ant standing on (20, 20); it got no order for a long time
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});

        // Enemy Worker invades the scan radius at (22, 20)
        uint32_t intruder = sim.spawn_unit(1, AntType::Worker, TileCoord{22, 20});

        // The idle hook finds the intruder (auto-engage starts, the saved order is "stay on this tile")
        bool engaged = false;
        for (int i = 0; i < 60 && !engaged; ++i) {
            sim.tick();
            engaged = sim.get_unit(combat).auto_engage;
        }
        ASSERT_TRUE(engaged);
        ASSERT_EQ(sim.get_unit(combat).ae_target, (TileCoord{20, 20}));

        // The combat ant walks up to the intruder and punches it: 2 hp (TakeHit twice)
        for (int i = 0; i < 100 && sim.get_unit(intruder).hp == 10; ++i) sim.tick();
        ASSERT_EQ(sim.get_unit(intruder).hp, 8);
        ASSERT_EQ(sim.get_unit(combat).loco_action, AntUnit::kActionAttack);

        // Thrown four tiles away without any stun afterwards; the combat ant goes back to its saved tile
        for (int i = 0; i < 200; ++i) sim.tick();
        ASSERT_TRUE(sim.get_unit(intruder).pos.x >= 24);
        ASSERT_FALSE(sim.get_unit(intruder).is_stunned());
        ASSERT_FALSE(sim.get_unit(combat).auto_engage);
        ASSERT_EQ(sim.get_unit(combat).pos, (TileCoord{20, 20}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.123: Anthill Bottlecap Thief Infiltration and Mound Passability
    // ------------------------------------------------------------------------
    TEST_CASE("12.123 Anthill Bottlecap Thief Infiltration and Mound Passability") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Setup base 1 at (20, 20)
        sim.set_anthill(1, {20, 20});

        // Verify corridor tile (24, 21) allows firewall and bomb placement
        const auto& corridor_cell = sim.grid().get_cell(TileCoord{24, 21});
        ASSERT_TRUE(corridor_cell.is_passable());
        ASSERT_TRUE(corridor_cell.can_place_fire());
        ASSERT_TRUE(corridor_cell.can_place_bomb());

        // Spawn a Thief ant for team 0
        uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{18, 22});

        // 1. Non-flank approach: Thief standing on left side does not infiltrate
        sim.tick();
        ASSERT_NE(sim.get_unit(thief).state, UnitState::Infiltrating);

        // 2. Bottlecap approach: Move thief onto bottlecap entrance tile (23, 22)
        sim.issue_move_order(thief, TileCoord{23, 22});
        for (int i = 0; i < 200; ++i) {
            sim.tick();
            if (sim.get_unit(thief).state == UnitState::Infiltrating) break;
        }

        // Thief entering from bottlecap tile (23, 22) infiltrates successfully!
        ASSERT_EQ(sim.get_unit(thief).state, UnitState::Infiltrating);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.124: Bomb Explosion bombex, Bomb Dud a*bu Scorch & Landing Stun a*sd Recovery
    // ------------------------------------------------------------------------
    TEST_CASE("12.124 Bomb Explosion bombex, Bomb Dud a*bu Scorch & Landing Stun a*sd Recovery") {
        // A. Full explosion branch and dud branch: 2 hit points either way, then the stun clip, then idle
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 60000);

            // Plant bomb at (15, 15): the ant ends its path on it and sets it off
            sim.grid_mut().place_bomb(15, 15, 1);
            uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
            sim.trigger_bomb_detonation(victim, TileCoord{15, 15});

            const auto& u = sim.get_unit(victim);
            // Both dud and full blast deal 2 damage: HP drops from 10 to 8
            ASSERT_EQ(u.hp, 8);
            ASSERT_TRUE(u.state == UnitState::Knockback || u.state == UnitState::Burn);   // the blast flight, or (dud) the frozen ant under ?bu
            ASSERT_EQ(u.frozen, u.knock_flag);

            // The flight (or the burn) is followed by the stun clip; then the ant is idle
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return u.state == UnitState::Stunned; }) >= 0);
            ASSERT_TRUE(u.is_stunned());
            ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return u.state == UnitState::Idle; }) >= 0);
            ASSERT_FALSE(u.is_stunned());
        }

        // B. Dud specific sequence: frozen under the burn overlay for 1150 ms, stun clip afterwards, then idle
        {
            bool tested = false;
            for (uint32_t seed = 1; seed < 100 && !tested; ++seed) {
                SimulationEngine sim;
                sim.init_test_world(60, 60, seed, 60000);
                sim.grid_mut().place_bomb(20, 20, 1);
                uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
                sim.trigger_bomb_detonation(ant_id, TileCoord{20, 20});
                if (!sim.get_unit(ant_id).knock_flag) continue;
                tested = true;

                ASSERT_TRUE(sim.get_unit(ant_id).frozen);
                for (int i = 0; i < 22; ++i) {
                    sim.tick();
                    ASSERT_TRUE(sim.get_unit(ant_id).frozen);      // 1100 ms
                }
                sim.tick(); sim.tick();                             // 1200 ms: the overlay has ended
                ASSERT_FALSE(sim.get_unit(ant_id).frozen);
                ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::Stunned);
                ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return sim.get_unit(ant_id).state == UnitState::Idle; }) >= 0);
                ASSERT_FALSE(sim.get_unit(ant_id).is_stunned());
            }
            ASSERT_TRUE(tested);
        }
    } TEST_END();

    TEST_CASE("12.125: Bomb Cursor, HUD Pedestal Glow, Water CantGo, Red Marquee & Intro Flow") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        ViewportCamera camera;
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim);

        // 1. Bomber Ant cursor evaluation: target reticle CursorType::Target (3 / c_targ1)
        uint32_t bomber_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        hud.select_ant(bomber_id);
        const auto& ws = sim.get_world_state();

        // Place a bomb at (11, 10) (adjacent cardinal neighbor)
        bool bomb_planted = sim.plant_bomb(bomber_id, TileCoord{11, 10}, true /* instant */);
        ASSERT_TRUE(bomb_planted);

        // Hover over the bomb at (11, 10) -> CursorType::Target
        int32_t screen_bx = (11 * 32 + 16) - camera.world_x + HUD::PLAYFIELD_X;
        int32_t screen_by = (10 * 32 + 16) - camera.world_y + HUD::PLAYFIELD_Y;
        CursorType bomb_cursor = hud.evaluate_cursor(screen_bx, screen_by, ws, sim.grid(), camera);
        ASSERT_EQ(static_cast<uint8_t>(bomb_cursor), static_cast<uint8_t>(CursorType::Target));

        // Order bomb placement at (10, 11) -> the cursor is not locked while the bomber plants: plain ground is the move cursor again
        bool planting_started = sim.plant_bomb(bomber_id, TileCoord{10, 11}, false);
        ASSERT_TRUE(planting_started);
        ASSERT_EQ(sim.get_unit(bomber_id).state, UnitState::PlantingBomb);
        const auto& ws_planting = sim.get_world_state();
        CursorType planting_cursor = hud.evaluate_cursor(100, 100, ws_planting, sim.grid(), camera);
        ASSERT_EQ(static_cast<uint8_t>(planting_cursor), static_cast<uint8_t>(CursorType::Move));

        // Wait for planting to complete so bomber returns to Idle
        while (sim.get_unit(bomber_id).state == UnitState::PlantingBomb) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(bomber_id).state, UnitState::Idle);

        // 2. Water Bomb Order -> instant CantGo
        sim.set_terrain(10, 9, 2); // 2 = water
        sim.clear_audio_events();
        AntOrder water_bomb_order;
        water_bomb_order.ant_id = bomber_id;
        water_bomb_order.type = OrderType::PlantBomb;
        water_bomb_order.target_x = 10;
        water_bomb_order.target_y = 9;
        sim.issue_order(water_bomb_order);
        ASSERT_TRUE(sim.has_audio_event(SoundID::CantGo));

        // 3. Application Intro Flow: Loading -> QuickHelp -> MapSelect
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.skip_intro = false;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);

        // 4. Quick Help START! button (Anim 1335 / Sprite 289 at 529, 437)
        const auto* qh_start = app.assets().find_animation("qh_start1");
        ASSERT_TRUE(qh_start != nullptr);
        ASSERT_FALSE(qh_start->subitems.empty());
        ASSERT_FALSE(qh_start->subitems[0].frames.empty());
        ASSERT_EQ(qh_start->subitems[0].frames[0].sprite_index, 289u);
        ASSERT_EQ(qh_start->subitems[0].frames[0].dx, 529);
        ASSERT_EQ(qh_start->subitems[0].frames[0].dy, 437);

        // 5. Loading screen logo, credits, and masking strip sprites
        const auto* logo_sp = app.assets().find_sprite("logo.bmp");
        ASSERT_TRUE(logo_sp != nullptr);
        ASSERT_EQ(logo_sp->width, 593u);
        ASSERT_EQ(logo_sp->height, 270u);

        const auto* cred_sp = app.assets().find_sprite("credits.bmp");
        ASSERT_TRUE(cred_sp != nullptr);
        ASSERT_EQ(cred_sp->width, 573u);
        ASSERT_EQ(cred_sp->height, 172u);

        const auto* strip_sp = app.assets().find_sprite("strip.bmp");
        ASSERT_TRUE(strip_sp != nullptr);
        ASSERT_EQ(strip_sp->width, 478u);
        ASSERT_EQ(strip_sp->height, 31u);
    } TEST_END();

    TEST_CASE("12.126: Daisy Dropper Occupancy Preservation, Standing Fire Ability Pathing, Bomb Knockback Redirection, Chain Bombs & HUD Bomb Tile Parity") {
        // 1. Daisy Flower Dropper Occupancy Preservation
        SimulationEngine sim;
        std::string path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL";
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_lvl(path));
        sim.init(lvl, 42);

        // Flower dropper 0 drops onto (2, 20)
        TileCoord drop_pos{2, 20};
        // Place bomb at (2, 20)
        sim.grid_mut().place_bomb(2, 20, 0);
        ASSERT_TRUE(sim.grid().has_bomb_at(drop_pos));

        // Advance simulation 15 s: the polls find the tile occupied by the bomb (layer 2 must be empty or a power-up), so nothing is posted
        for (int t = 0; t < 300; ++t) {
            sim.tick();
        }
        ASSERT_TRUE(sim.grid().has_bomb_at(drop_pos));
        ASSERT_FALSE(sim.grid().has_powerup_at(drop_pos));

        // Now remove the bomb
        sim.grid_mut().clear_bomb(2, 20);
        // The stamp of the first poll is still the old one, so the next poll (every 3 s) posts and the drop lands 820 ms later
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (sim.grid().has_powerup_at(drop_pos)) break;
        }
        ASSERT_TRUE(sim.grid().has_powerup_at(drop_pos));

        // 2. Fire Ant Standing On Fire Ability Pathing: does not detour around terrain
        SimulationEngine sim2;
        sim2.init_test_world(60, 60, 42, 60000);
        sim2.set_fire_at({15, 15}, 3600);
        uint32_t fire_ant_id = sim2.spawn_unit(0, AntType::Fire, {15, 15});
        // Issue IgniteFire order at adjacent tile (14, 15)
        AntOrder fire_order{};
        fire_order.ant_id = fire_ant_id;
        fire_order.type = OrderType::IgniteFire;
        fire_order.target_x = 14;
        fire_order.target_y = 15;
        sim2.issue_order(fire_order);
        // Ant is already adjacent on (15, 15), so it should NOT path away to North; it stays on (15, 15)
        const auto& fire_ant = sim2.get_unit(fire_ant_id);
        ASSERT_EQ(fire_ant.pos.x, 15);
        ASSERT_EQ(fire_ant.pos.y, 15);

        // 3. Bomb Knockback 8-Way Redirection Around Power-Ups (FUN_0101df5d)
        // Bomb at (20, 20), worker approaches from (21, 20) walking West
        // Recoil starts East towards (24, 20).
        // If (24, 20) has a power-up, recoil MUST rotate clockwise away from (24, 20)
        sim2.grid_mut().place_powerup(24, 20, 1);
        sim2.grid_mut().place_bomb(20, 20, 1);
        uint32_t w1 = sim2.spawn_unit(0, AntType::Worker, {21, 20});
        AntOrder w1_order{};
        w1_order.ant_id = w1;
        w1_order.type = OrderType::Move;
        w1_order.target_x = 20;
        w1_order.target_y = 20;
        sim2.issue_order(w1_order);
        for (int t = 0; t < 100; ++t) {
            sim2.tick();
            if (!sim2.grid().has_bomb_at({20, 20})) break;
        }
        for (int t = 0; t < 15; ++t) {
            sim2.tick();
            if (sim2.get_unit(w1).state != UnitState::Knockback) break;
        }
        const auto& landed_w1 = sim2.get_unit(w1);
        // Did not land on (24, 20) because of powerup redirection!
        ASSERT_FALSE(landed_w1.pos.x == 24 && landed_w1.pos.y == 20);
        // Powerup at (24, 20) was preserved
        ASSERT_TRUE(sim2.grid().has_powerup_at({24, 20}));

        // 4. Bomb Knockback Landing on Fire Wall Allowed
        sim2.set_fire_at({34, 30}, 3600);
        sim2.grid_mut().place_bomb(30, 30, 1);
        uint32_t w2 = sim2.spawn_unit(0, AntType::Worker, {31, 30});
        AntOrder w2_order{};
        w2_order.ant_id = w2;
        w2_order.type = OrderType::Move;
        w2_order.target_x = 30;
        w2_order.target_y = 30;
        sim2.issue_order(w2_order);
        for (int t = 0; t < 100; ++t) {
            sim2.tick();
            if (!sim2.grid().has_bomb_at({30, 30})) break;
        }
        for (int t = 0; t < 15; ++t) {
            sim2.tick();
            if (sim2.get_unit(w2).state != UnitState::Knockback) break;
        }
        // Took bomb blast damage (2 HP) + fire damage (1 HP) = 3 HP damage (10 - 3 = 7 HP)
        ASSERT_EQ(sim2.get_unit(w2).hp, 7);

        // 5. Chain Bomb Detonation
        // Bomb at (40, 40) recoils East to (44, 40). Place another bomb at (44, 40).
        // Using seed 1 guarantees deterministic non-dud rolls (call 0: 41% >= 20%, call 2: 34% >= 20%).
        SimulationEngine sim_chain;
        sim_chain.init_test_world(60, 60, 1, 60000);
        sim_chain.grid_mut().place_bomb(40, 40, 1);
        sim_chain.grid_mut().place_bomb(44, 40, 1);
        uint32_t w3 = sim_chain.spawn_unit(0, AntType::Worker, {41, 40});
        AntOrder w3_order{};
        w3_order.ant_id = w3;
        w3_order.type = OrderType::Move;
        w3_order.target_x = 40;
        w3_order.target_y = 40;
        sim_chain.issue_order(w3_order);
        for (int t = 0; t < 100; ++t) {
            sim_chain.tick();
            if (!sim_chain.grid().has_bomb_at({40, 40})) break;
        }
        for (int t = 0; t < 30; ++t) {
            sim_chain.tick();
        }
        // Chain bomb at (44, 40) was triggered and cleared!
        ASSERT_FALSE(sim_chain.grid().has_bomb_at({44, 40}));

        // 6. HUD Bomb Cursor (FUN_01026f91: a bomb tile is a special target of a single selected bomber; Shift plays no part)
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim2);
        sim2.grid_mut().place_bomb(50, 50, 1);
        uint32_t bomber_id = sim2.spawn_unit(0, AntType::Bomber, {48, 50});
        uint32_t worker_id2 = sim2.spawn_unit(0, AntType::Worker, {48, 51});

        ViewportCamera cam;
        cam.center_on(50 * 32 + 16, 50 * 32 + 16);
        int32_t screen_bx = 0;
        int32_t screen_by = 0;
        cam.world_to_screen(50 * 32 + 16, 50 * 32 + 16, screen_bx, screen_by);

        // (a) Bomber selected (panel 3), shift NOT held -> Target reticle
        hud.select_ant(bomber_id, false);
        hud.set_shift_held(false);
        CursorType c1 = hud.evaluate_cursor(screen_bx, screen_by, sim2.get_world_state(), sim2.grid(), cam);
        ASSERT_EQ(c1, CursorType::Target);

        // (b) Bomber selected, shift HELD -> the same Target reticle (the original's cursor code never looks at Shift)
        hud.set_shift_held(true);
        CursorType c2 = hud.evaluate_cursor(screen_bx, screen_by, sim2.get_world_state(), sim2.grid(), cam);
        ASSERT_EQ(c2, CursorType::Target);

        // (c) Worker selected (non-bomber) -> regular Move cursor
        hud.select_ant(worker_id2, false);
        hud.set_shift_held(false);
        CursorType c3 = hud.evaluate_cursor(screen_bx, screen_by, sim2.get_world_state(), sim2.grid(), cam);
        ASSERT_EQ(c3, CursorType::Move);

        // (d) A bomber and a worker (panel 4, mixed types) -> regular Move cursor
        hud.set_selected_ant_ids({bomber_id, worker_id2});
        CursorType c4 = hud.evaluate_cursor(screen_bx, screen_by, sim2.get_world_state(), sim2.grid(), cam);
        ASSERT_EQ(c4, CursorType::Move);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.127: Fire Extinguish Timing & Sputter, Multi-Direction Placement Queueing, and Bomb Parity
    // ------------------------------------------------------------------------
    TEST_CASE("12.127 Fire Extinguish Timing, Silent Casting Rejection, Zero Cooldown, and Bomb Dud Parity") {
        // A. Fire Extinguish Timing: 1200 ms (South) or 1300 ms (North/East/West), cue 69 at 400 ms, the wall is put out (with
        //    the sputter puff) when the clip ends
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Place firewall at (20, 21)
            sim.set_fire_at({20, 21}, 3600);
            ASSERT_TRUE(sim.has_fire_at({20, 21}));

            // Fire Ant at (20, 20) facing Down (South) to extinguish
            uint32_t ant_id = sim.spawn_unit(0, AntType::Fire, TileCoord{20, 20});
            AntOrder o;
            o.type = OrderType::ExtinguishFire;
            o.ant_id = ant_id;
            o.target_x = 20;
            o.target_y = 21;
            sim.issue_order(o);

            // Ant enters ExtinguishingFire once its one-tile path is delivered
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.get_unit(ant_id).state == UnitState::ExtinguishingFire; }) >= 0);
            sim.clear_audio_events();

            // The firewall stays for the whole clip; the extinguisher cue 69 plays at 400 ms
            int elapsed = 0;
            while (sim.has_fire_at({20, 21}) && elapsed < 3000) {
                if (elapsed == 300) ASSERT_FALSE(sim.has_audio_event(static_cast<uint32_t>(SoundID::FireExtinguish)));
                if (elapsed == 500) ASSERT_TRUE(sim.has_audio_event(static_cast<uint32_t>(SoundID::FireExtinguish)));
                ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::ExtinguishingFire);
                sim.tick();
                elapsed += 50;
            }
            ASSERT_TRUE(elapsed >= 1150 && elapsed <= 1350);

            // Fire cleared and sputter effect spawned at top-left anchor (20 * 32, 21 * 32)
            ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::Idle);
            ASSERT_FALSE(sim.has_fire_at({20, 21}));
            bool found_sputter = false;
            for (const auto& fx : sim.get_world_state().effects) {
                if (fx.anim_name == "sputter" && fx.px == 20 * 32 && fx.py == 21 * 32 && fx.total_frames == 17) {
                    found_sputter = true;
                    break;
                }
            }
            ASSERT_TRUE(found_sputter);
        }

        // B. Silent Order Rejection During Active Action & Zero Post-Animation Cooldown
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Fire ant at (25, 25)
            uint32_t ant_id = sim.spawn_unit(0, AntType::Fire, TileCoord{25, 25});

            // Order 1: Ignite Fire North (25, 24)
            AntOrder o1;
            o1.type = OrderType::IgniteFire;
            o1.ant_id = ant_id;
            o1.target_x = 25;
            o1.target_y = 24;
            sim.issue_order(o1);

            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.get_unit(ant_id).state == UnitState::PlacingFire; }) >= 0);
            ASSERT_EQ(sim.get_unit(ant_id).facing, ants::assets::Direction::North);

            // While placing North, player tries to issue another order (East 26, 25)
            sim.clear_audio_events();
            AntOrder o2;
            o2.type = OrderType::IgniteFire;
            o2.ant_id = ant_id;
            o2.target_x = 26;
            o2.target_y = 25;
            sim.issue_order(o2);

            // Order is SILENTLY IGNORED: no CantGo sound, placement continues uninterrupted
            ASSERT_FALSE(sim.has_audio_event(static_cast<uint32_t>(SoundID::CantGo)));
            ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::PlacingFire);
            ASSERT_EQ(sim.get_unit(ant_id).facing, ants::assets::Direction::North);
            ASSERT_EQ(sim.get_unit(ant_id).orig_special_tile, (TileCoord{25, 24}));

            // Let the North placement finish (afsf: 1810 ms)
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.has_fire_at({25, 24}); }) >= 0);

            // First firewall at (25, 24) exists and ant is back in Idle
            ASSERT_TRUE(sim.has_fire_at({25, 24}));
            ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::Idle);

            // Immediately upon Idle, issuing East placement succeeds with ZERO cooldown delay!
            sim.issue_order(o2);
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.get_unit(ant_id).state == UnitState::PlacingFire; }) >= 0);
            ASSERT_EQ(sim.get_unit(ant_id).facing, ants::assets::Direction::East);

            // Finish East placement
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.has_fire_at({26, 25}); }) >= 0);
            ASSERT_TRUE(sim.has_fire_at({26, 25}));
            ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::Idle);
        }

        // C. Bomb Dud & Knockback Stun Parity: is_stunned() is true during the flight / burn and the stun clip, false afterwards
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 10, 60000);

            sim.grid_mut().place_bomb(10, 10, 1);
            uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            const auto& ant = sim.get_unit(ant_id);
            ASSERT_FALSE(ant.is_stunned());
            sim.trigger_bomb_detonation(ant_id, TileCoord{10, 10});

            // In flight or burn, ant is considered stunned (rejects orders)
            ASSERT_TRUE(ant.is_stunned());
            ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return ant.state == UnitState::Stunned; }) >= 0);
            ASSERT_TRUE(ant.is_stunned());

            // After the stun clip the unit is Idle with 0 stun ticks remaining
            ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return ant.state == UnitState::Idle; }) >= 0);
            ASSERT_FALSE(ant.is_stunned());
        }
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.128: Bomber Pathing Around Intervening Friendly Bombs
    // ------------------------------------------------------------------------
    TEST_CASE("12.128 Bomber Pathing Around Intervening Friendly Bombs") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place a friendly bomb at (11, 10) owned by Player 0
        sim.grid_mut().place_bomb(11, 10, 0);
        ASSERT_TRUE(sim.has_bomb_at({11, 10}));

        // Bomber Ant at (10, 10)
        uint32_t bomber_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        auto& bomber = sim.get_unit(bomber_id);
        ASSERT_EQ(bomber.pos, (TileCoord{10, 10}));

        // Issue order to plant a bomb at (12, 10)
        // (11, 10) already has a friendly bomb between the ant and target
        AntOrder o;
        o.type = OrderType::PlantBomb;
        o.ant_id = bomber_id;
        o.target_x = 12;
        o.target_y = 10;
        sim.issue_order(o);

        // The Bomber must NOT stall or pick (11, 10) as its candidate standing spot.
        // It must route around (11, 10) (e.g. via y=9 or y=11) to an open cardinal neighbor of (12, 10).
        ASSERT_TRUE(bomber.state == UnitState::Walking || bomber.state == UnitState::PlantingBomb);

        // Step simulation up to 100 ticks to allow navigation and bomb placement
        bool planted = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (sim.has_bomb_at({12, 10})) {
                planted = true;
                break;
            }
        }

        // Successfully placed the new bomb at (12, 10)!
        ASSERT_TRUE(planted);
        ASSERT_TRUE(sim.has_bomb_at({12, 10}));

        // The original friendly bomb at (11, 10) was never detonated or stepped on!
        ASSERT_TRUE(sim.has_bomb_at({11, 10}));
    } TEST_END();

    TEST_CASE("12.129 Chain Bomb Detonation Flight Decoupling, Firewall Knockback Landing & 22-Tick Defusal Parity") {
        // -------------------------------------------------------------------------
        // Part 1: Chain Bomb Knockback Secondary Flight Decoupling
        // An ant detonates Bomb 1 at (40, 40) and is launched into Bomb 2 at (44, 40).
        // Bomb 2 detonates, triggering a secondary ballistic flight that must NOT be
        // double-erased or leave the ant frozen in UnitState::Knockback.
        // -------------------------------------------------------------------------
        SimulationEngine sim_chain;
        sim_chain.init_test_world(60, 60, 1, 60000);
        sim_chain.grid_mut().place_bomb(40, 40, 1);
        sim_chain.grid_mut().place_bomb(44, 40, 1);

        uint32_t w_chain = sim_chain.spawn_unit(0, AntType::Worker, {41, 40});
        AntOrder move_to_bomb1{};
        move_to_bomb1.ant_id = w_chain;
        move_to_bomb1.type = OrderType::Move;
        move_to_bomb1.target_x = 40;
        move_to_bomb1.target_y = 40;
        sim_chain.issue_order(move_to_bomb1);

        // Step until Bomb 1 detonates
        for (int t = 0; t < 100; ++t) {
            sim_chain.tick();
            if (!sim_chain.grid().has_bomb_at({40, 40})) break;
        }
        ASSERT_FALSE(sim_chain.grid().has_bomb_at({40, 40}));

        // Advance simulation for chain detonation and secondary flight completion
        for (int t = 0; t < 40; ++t) {
            sim_chain.tick();
        }
        // Both bombs cleared!
        ASSERT_FALSE(sim_chain.grid().has_bomb_at({44, 40}));
        auto& chain_ant = sim_chain.get_unit(w_chain);
        // Ant survived (took 2 + 2 = 4 damage out of 10 HP = 6 HP remaining)
        ASSERT_TRUE(chain_ant.is_alive());
        ASSERT_EQ(chain_ant.hp, 6);
        // Ant successfully completed both flights and, after the stun of the second one, is idle again (NOT frozen in Knockback!)
        ASSERT_TRUE(wait_ms(sim_chain, 12000, [&]() { return chain_ant.state == UnitState::Idle; }) >= 0);
        ASSERT_FALSE(chain_ant.is_stunned());

        // Ant can immediately accept new move orders
        AntOrder move_after_chain{};
        move_after_chain.ant_id = w_chain;
        move_after_chain.type = OrderType::Move;
        move_after_chain.target_x = 45;
        move_after_chain.target_y = 40;
        sim_chain.issue_order(move_after_chain);
        ASSERT_EQ(chain_ant.state, UnitState::Walking);

        // -------------------------------------------------------------------------
        // Part 2: Bomb Knockback into Firewall (Non-Fire Ant & Fire Ant)
        // -------------------------------------------------------------------------
        SimulationEngine sim_fire;
        sim_fire.init_test_world(60, 60, 1, 60000);
        // Place bomb at (30, 30) and firewall at (34, 30)
        sim_fire.grid_mut().place_bomb(30, 30, 1);
        sim_fire.grid_mut().place_firewall(34, 30, 0);

        uint32_t w_fire = sim_fire.spawn_unit(0, AntType::Worker, {31, 30});
        AntOrder move_to_bomb_fire{};
        move_to_bomb_fire.ant_id = w_fire;
        move_to_bomb_fire.type = OrderType::Move;
        move_to_bomb_fire.target_x = 30;
        move_to_bomb_fire.target_y = 30;
        sim_fire.issue_order(move_to_bomb_fire);

        for (int t = 0; t < 100; ++t) {
            sim_fire.tick();
            if (!sim_fire.grid().has_bomb_at({30, 30})) break;
        }
        ASSERT_FALSE(sim_fire.grid().has_bomb_at({30, 30}));

        // Advance through the bomb flight, the landing on the fire wall (a second, one tile flight) and the recovery
        auto& fire_hit_ant = sim_fire.get_unit(w_fire);
        ASSERT_TRUE(wait_ms(sim_fire, 12000, [&]() { return fire_hit_ant.hp == 7 && fire_hit_ant.state == UnitState::Idle; }) >= 0);
        ASSERT_TRUE(fire_hit_ant.is_alive());
        // Worker took 2 blast + 1 fire burn damage = 7 HP remaining
        ASSERT_EQ(fire_hit_ant.hp, 7);
        // Ant returned to Idle (NOT frozen in Knockback!)
        ASSERT_EQ(fire_hit_ant.state, UnitState::Idle);
        ASSERT_FALSE(fire_hit_ant.is_stunned());

        // Worker can receive move orders (order to move away from firewall to safe ground)
        AntOrder move_after_fire{};
        move_after_fire.ant_id = w_fire;
        move_after_fire.type = OrderType::Move;
        move_after_fire.target_x = 25;
        move_after_fire.target_y = 30;
        sim_fire.issue_order(move_after_fire);
        ASSERT_EQ(fire_hit_ant.state, UnitState::Walking);

        // Fire Ant landing on fire wall from bomb knockback: takes 0 fire damage, does not freeze
        SimulationEngine sim_fire_ant;
        sim_fire_ant.init_test_world(60, 60, 1, 60000);
        sim_fire_ant.grid_mut().place_bomb(20, 20, 1);
        sim_fire_ant.grid_mut().place_firewall(24, 20, 0);

        uint32_t f_id = sim_fire_ant.spawn_unit(0, AntType::Fire, {21, 20});
        AntOrder move_fire_ant{};
        move_fire_ant.ant_id = f_id;
        move_fire_ant.type = OrderType::Move;
        move_fire_ant.target_x = 20;
        move_fire_ant.target_y = 20;
        sim_fire_ant.issue_order(move_fire_ant);

        for (int t = 0; t < 100; ++t) {
            sim_fire_ant.tick();
            if (!sim_fire_ant.grid().has_bomb_at({20, 20})) break;
        }
        ASSERT_FALSE(sim_fire_ant.grid().has_bomb_at({20, 20}));

        // The fire ant lands on the wall without any damage from it (it is stunned like after any bomb flight)
        auto& f_unit = sim_fire_ant.get_unit(f_id);
        ASSERT_TRUE(wait_ms(sim_fire_ant, 12000, [&]() { return f_unit.state == UnitState::Idle && f_unit.pos.x >= 24; }) >= 0);
        ASSERT_TRUE(f_unit.is_alive());
        // Fire Ant took only bomb damage (2 HP), immune to fire: 10 - 2 = 8 HP
        ASSERT_EQ(f_unit.hp, 8);
        ASSERT_EQ(f_unit.state, UnitState::Idle);
        ASSERT_FALSE(f_unit.is_stunned());

        // Fire Ant can immediately move after landing on fire
        AntOrder move_after_fire_ant{};
        move_after_fire_ant.ant_id = f_id;
        move_after_fire_ant.type = OrderType::Move;
        move_after_fire_ant.target_x = 25;
        move_after_fire_ant.target_y = 20;
        sim_fire_ant.issue_order(move_after_fire_ant);
        ASSERT_EQ(f_unit.state, UnitState::Walking);

        // -------------------------------------------------------------------------
        // Part 3: Bomb Defusal Authentic Clip Timing (abdb east: 1140 ms, cues 73 at 220 and 74 at 620 ms)
        // -------------------------------------------------------------------------
        SimulationEngine sim_defuse;
        sim_defuse.init_test_world(60, 60, 1, 60000);
        sim_defuse.grid_mut().place_bomb(11, 10, 1); // Enemy bomb
        uint32_t b_defuser = sim_defuse.spawn_unit(0, AntType::Bomber, {10, 10});
        auto& b_unit = sim_defuse.get_unit(b_defuser);

        sim_defuse.clear_audio_events();
        ASSERT_TRUE(sim_defuse.defuse_bomb(b_defuser, {11, 10}, false));
        ASSERT_EQ(b_unit.state, UnitState::DefusingBomb);
        ASSERT_EQ(b_unit.facing, Direction::East);

        // The bomb stays until the clip ends; the two cues come at 220 ms and 620 ms
        int defuse_elapsed = 0;
        while (sim_defuse.grid().has_bomb_at({11, 10}) && defuse_elapsed < 3000) {
            if (defuse_elapsed == 150) ASSERT_FALSE(sim_defuse.has_audio_event(SoundID::BombDefuseGrab));
            if (defuse_elapsed == 350) {
                ASSERT_TRUE(sim_defuse.has_audio_event(SoundID::BombDefuseGrab));       // Sound 73
                ASSERT_FALSE(sim_defuse.has_audio_event(SoundID::BombBodySquash));
            }
            ASSERT_EQ(b_unit.state, UnitState::DefusingBomb);
            sim_defuse.tick();
            defuse_elapsed += 50;
        }
        ASSERT_TRUE(defuse_elapsed >= 1100 && defuse_elapsed <= 1300);
        ASSERT_TRUE(sim_defuse.has_audio_event(SoundID::BombBodySquash));                // Sound 74
        ASSERT_EQ(sim_defuse.get_player_stats(0).bombs_defused, 1u);

        // The bomber is idle at once
        ASSERT_EQ(b_unit.state, UnitState::Idle);

        // Immediately order move without cooldown delay
        AntOrder move_defuser{};
        move_defuser.ant_id = b_defuser;
        move_defuser.type = OrderType::Move;
        move_defuser.target_x = 11;
        move_defuser.target_y = 10;
        sim_defuse.issue_order(move_defuser);
        ASSERT_EQ(b_unit.state, UnitState::Walking);
    } TEST_END();

    TEST_CASE("12.130 Airborne Trajectory Clearance Over Ground Ants & Fire Landing (Blast 1) / State Transitions") {
        // -------------------------------------------------------------------------
        // Part 1: Airborne Ballistic Trajectory Clearance Over Intermediate Ants
        // An ant sets off a bomb at (20, 20) while facing West. The blast throws it
        // East towards (24, 20) (4 tiles). Station friendly and enemy ants along the
        // flight path at (21, 20), (22, 20), and (23, 20). The airborne ant must fly
        // completely OVER them without colliding: only the landing tile is tested.
        // -------------------------------------------------------------------------
        {
            bool tested = false;
            for (uint32_t seed = 1; seed < 60 && !tested; ++seed) {
                SimulationEngine sim;
                sim.init_test_world(60, 60, seed, 60000);
                sim.grid_mut().place_bomb(20, 20, 1);

                // Ground ants stationed along trajectory
                uint32_t standing_friendly1 = sim.spawn_unit(0, AntType::Worker, {21, 20});
                uint32_t standing_enemy     = sim.spawn_unit(1, AntType::Worker, {22, 20});
                uint32_t standing_friendly2 = sim.spawn_unit(0, AntType::Worker, {23, 20});

                // Ant spawned directly at bomb tile facing West -> blast recoils East across tiles 21, 22, 23
                uint32_t flying_ant = sim.spawn_unit(0, AntType::Worker, {20, 20});
                sim.get_unit(flying_ant).facing = Direction::West;

                sim.trigger_bomb_detonation(flying_ant, {20, 20});
                if (sim.get_unit(flying_ant).knock_flag) continue;   // a dud burns the ant on the spot
                tested = true;
                ASSERT_FALSE(sim.grid().has_bomb_at({20, 20}));

                // Step through the airborne flight and into the stun that follows a bomb flight
                const auto& fa = sim.get_unit(flying_ant);
                ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return fa.state == UnitState::Stunned; }) >= 0);

                // Intermediate ants must remain completely undisturbed at their coordinates!
                const auto& sf1 = sim.get_unit(standing_friendly1);
                ASSERT_EQ(sf1.pos.x, 21);
                ASSERT_EQ(sf1.pos.y, 20);
                ASSERT_EQ(sf1.hp, 10);
                ASSERT_EQ(sf1.state, UnitState::Idle);

                const auto& se = sim.get_unit(standing_enemy);
                ASSERT_EQ(se.pos.x, 22);
                ASSERT_EQ(se.pos.y, 20);
                ASSERT_EQ(se.hp, 10);
                ASSERT_EQ(se.state, UnitState::Idle);

                const auto& sf2 = sim.get_unit(standing_friendly2);
                ASSERT_EQ(sf2.pos.x, 23);
                ASSERT_EQ(sf2.pos.y, 20);
                ASSERT_EQ(sf2.hp, 10);
                ASSERT_EQ(sf2.state, UnitState::Idle);

                // Flying ant has flown cleanly over all 3 ants and landed on tile 4 (24, 20)!
                ASSERT_TRUE(fa.is_alive());
                ASSERT_EQ(fa.pos.x, 24);
                ASSERT_EQ(fa.pos.y, 20);
                ASSERT_TRUE(fa.is_stunned());

                // Stunned ant can be ordered to walk immediately (Ants.exe FUN_01021494), cancelling stun
                AntOrder move_after_flight{};
                move_after_flight.ant_id = flying_ant;
                move_after_flight.type = OrderType::Move;
                move_after_flight.target_x = 25;
                move_after_flight.target_y = 20;
                sim.issue_order(move_after_flight);
                ASSERT_EQ(fa.state, UnitState::Walking);
                ASSERT_FALSE(fa.is_stunned());
            }
            ASSERT_TRUE(tested);
        }

        // -------------------------------------------------------------------------
        // Part 2: Bomb Blast Knockback Landing on Fire Wall
        // The landing on a fire wall costs 1 hit point and throws the ant one tile away
        // (Blast 1); the hit clip ends without a stun and the ant is idle.
        // -------------------------------------------------------------------------
        {
            bool tested = false;
            for (uint32_t seed = 1; seed < 60 && !tested; ++seed) {
                SimulationEngine sim;
                sim.init_test_world(60, 60, seed, 60000);
                sim.grid_mut().place_bomb(10, 10, 1);
                sim.grid_mut().place_firewall(14, 10, 0);

                uint32_t w_id = sim.spawn_unit(0, AntType::Worker, {11, 10});
                AntOrder o{};
                o.ant_id = w_id;
                o.type = OrderType::Move;
                o.target_x = 10;
                o.target_y = 10;
                sim.issue_order(o);

                ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return !sim.grid().has_bomb_at({10, 10}); }) >= 0);
                if (sim.get_unit(w_id).knock_flag) continue;   // a dud
                tested = true;

                // Step through the flight, the landing on the wall and the recovery
                const auto& u_landed = sim.get_unit(w_id);
                ASSERT_TRUE(wait_ms(sim, 12000, [&]() { return u_landed.hp == 7 && u_landed.state == UnitState::Idle; }) >= 0);
                ASSERT_TRUE(u_landed.is_alive());
                // 10 HP - 2 blast - 1 fire = 7 HP
                ASSERT_EQ(u_landed.hp, 7);
                // Ricocheted away from fire at (14, 10) to an adjacent tile
                ASSERT_TRUE(std::abs(u_landed.pos.x - 14) <= 1 && std::abs(u_landed.pos.y - 10) <= 1);
                ASSERT_FALSE(u_landed.pos.x == 14 && u_landed.pos.y == 10);
                ASSERT_EQ(u_landed.state, UnitState::Idle);
                ASSERT_FALSE(u_landed.is_stunned());

                // Can immediately walk
                AntOrder move_after{};
                move_after.ant_id = w_id;
                move_after.type = OrderType::Move;
                move_after.target_x = 5;
                move_after.target_y = 5;
                sim.issue_order(move_after);
                ASSERT_EQ(u_landed.state, UnitState::Walking);
            }
            ASSERT_TRUE(tested);
        }

        // -------------------------------------------------------------------------
        // Part 3: Combat Ant Punch Knockback Landing on Fire Wall
        // The victim takes the 2 hp of the punch and 1 hp from the wall, is thrown one tile away
        // and ends idle (a hit clip is not followed by a stun).
        // -------------------------------------------------------------------------
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            sim.grid_mut().place_firewall(34, 30, 0);

            // Puncher at (29, 30), Target at (30, 30), Firewall at (34, 30) (4 tiles distance)
            uint32_t combat_ant = sim.spawn_unit(0, AntType::Combat, {29, 30});
            uint32_t enemy_ant  = sim.spawn_unit(1, AntType::Worker, {30, 30});

            // Combat ant attacks enemy ant
            sim.execute_melee_attack(combat_ant, enemy_ant);

            // Step through the punch flight onto the firewall and the landing resolution
            const auto& u_after = sim.get_unit(enemy_ant);
            ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return u_after.hp == 7 && u_after.state == UnitState::Idle; }) >= 0);
            ASSERT_TRUE(u_after.is_alive());
            // 10 HP - 2 punch - 1 fire = 7 HP
            ASSERT_EQ(u_after.hp, 7);
            // Ricocheted away from fire at (34, 30) to an adjacent tile
            ASSERT_TRUE(std::abs(u_after.pos.x - 34) <= 1 && std::abs(u_after.pos.y - 30) <= 1);
            ASSERT_FALSE(u_after.pos.x == 34 && u_after.pos.y == 30);
            ASSERT_FALSE(u_after.is_stunned());
        }

        // -------------------------------------------------------------------------
        // Part 4: General Fire Contact (Stepping on Fire)
        // Non-fire ant takes 1 fire damage and is thrown one tile away with the hit clip
        // -------------------------------------------------------------------------
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            sim.grid_mut().place_firewall(15, 15, 0);

            // Non-fire ant on the fire tile
            uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, {15, 15});
            sim.resolve_fire_contact(ant_id, 1, 0);

            const auto& ant_flying = sim.get_unit(ant_id);
            // Takes 1 fire damage (10 - 1 = 9 HP)
            ASSERT_EQ(ant_flying.hp, 9);
            ASSERT_EQ(ant_flying.state, UnitState::Flinch);

            // Advance through the hit clip
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return ant_flying.state == UnitState::Idle; }) >= 0);
            ASSERT_FALSE(sim.grid().has_fire_at(ant_flying.pos));
        }
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.131: Anthill Mound Bounds & Access Invariants, 22-Tick Fire Burn & 8-Dir Random Bounces
    // ------------------------------------------------------------------------
    TEST_CASE("12.131 Anthill Mound Bounds & Access Invariants, 22-Tick Fire Burn & 8-Dir Random Bounces") {
        // Part 1: Anthill Mound Bounds & Access (4x4 Mound + Top Corridor)
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            // Setup Team 1 base at (36, 20) matching user screenshot
            sim.set_anthill(1, {36, 20});
            const auto& grid = sim.grid();

            // 1. Top approach corridor (36, 19), (37, 19), (38, 19)
            // Blocked from bombs and firewalls
            for (int32_t x = 36; x <= 38; ++x) {
                const auto& cell = grid.get_cell(TileCoord{x, 19});
                ASSERT_FALSE(cell.can_place_bomb());
                ASSERT_FALSE(cell.can_place_fire());
                // Team 1 ants can pass
                ASSERT_TRUE(cell.is_passable(false, false, false, 1, false));
                // Enemy ants (Team 0) can pass freely (corridor team lock removed for authentic base perimeter walkability)
                ASSERT_TRUE(cell.is_passable(false, false, false, 0, false));
            }

            // 2. Bottlecap tile (39, 22) (bx + 3, by + 2)
            const auto& bcap = grid.get_cell(TileCoord{39, 22});
            // Enemy thief (Team 0) can occupy
            ASSERT_TRUE(bcap.is_passable(false, false, true, 0, false));
            // Friendly thief (Team 1) cannot occupy
            ASSERT_FALSE(bcap.is_passable(false, false, true, 1, false));
            // Non-thief enemy (Team 0) cannot occupy
            ASSERT_FALSE(bcap.is_passable(false, false, false, 0, false));
            // Friendly non-thief (Team 1) cannot occupy
            ASSERT_FALSE(bcap.is_passable(false, false, false, 1, false));

            // 3. Ramp (37, 20) and Hole (37, 21)
            // Blocked from bombs and firewalls
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 20}).can_place_bomb());
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 20}).can_place_fire());
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 21}).can_place_bomb());
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 21}).can_place_fire());
            // Only passable when entering or leaving base
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 20}).is_passable(false, false, false, 1, false));
            ASSERT_TRUE(grid.get_cell(TileCoord{37, 20}).is_passable(false, false, false, 1, true));
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 21}).is_passable(false, false, false, 1, false));
            ASSERT_TRUE(grid.get_cell(TileCoord{37, 21}).is_passable(false, false, false, 1, true));

            // 4. Remaining 13 mound tiles must be impassable for all
            for (int32_t dy = 0; dy < 4; ++dy) {
                for (int32_t dx = 0; dx < 4; ++dx) {
                    if ((dx == 1 && dy == 0) || (dx == 1 && dy == 1) || (dx == 3 && dy == 2)) {
                        continue;
                    }
                    const auto& cell = grid.get_cell(TileCoord{36 + dx, 20 + dy});
                    ASSERT_FALSE(cell.is_passable(false, false, false, 1, false));
                    ASSERT_FALSE(cell.is_passable(false, false, true, 0, false));
                    ASSERT_TRUE(cell.is_obstacle());
                }
            }
        }

        // Part 2: Fire contact (Blast 1): 1 hp, the gh clip carries the ant one tile away, sounds 64 (frame 0) and 65
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            sim.grid_mut().place_firewall(10, 10, 0);

            uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            sim.resolve_fire_contact(ant, 1, 0);

            const auto& u0 = sim.get_unit(ant);
            ASSERT_EQ(u0.loco_action, AntUnit::kActionHit);
            ASSERT_EQ(u0.hp, 9);
            ASSERT_TRUE(sim.has_audio_event(64));
            bool heard_sound65 = false;
            for (int t = 0; t < 60; ++t) {
                sim.tick();
                if (sim.has_audio_event(65)) heard_sound65 = true;
            }
            ASSERT_TRUE(heard_sound65);

            const auto& u_done = sim.get_unit(ant);
            ASSERT_EQ(u_done.loco_action, AntUnit::kActionIdle);
            ASSERT_EQ(std::max(std::abs(u_done.pos.x - 10), std::abs(u_done.pos.y - 10)), 1);
            ASSERT_TRUE(sim.grid().has_fire_at(TileCoord{10, 10}));   // the fire wall stays
        }

        // Part 3: The thrown ant lands in a random one of the free directions across trials
        {
            std::set<std::pair<int32_t, int32_t>> distinct_landing_tiles;
            for (int trial = 0; trial < 40; ++trial) {
                SimulationEngine sim;
                sim.init_test_world(60, 60, 100 + static_cast<uint32_t>(trial * 31), 60000);
                sim.grid_mut().place_firewall(30, 30, 0);
                uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
                sim.resolve_fire_contact(ant, 1, 0);
                for (int t = 0; t < 60; ++t) sim.tick();
                const auto& u = sim.get_unit(ant);
                distinct_landing_tiles.insert({u.pos.x, u.pos.y});
            }
            // Over 40 randomized trials, the ant must be thrown in multiple directions, not just 1 fixed tile
            ASSERT_TRUE(distinct_landing_tiles.size() >= 4);
        }

        // Part 4: A landing on a bomb tile sets the bomb off (the flight of a hit ends on the bomb)
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            TileCoord bomb_pos{25, 25};
            TileCoord fire_pos{24, 26}; // Southwest of bomb (dx = -1, dy = +1)
            sim.grid_mut().place_bomb(static_cast<uint32_t>(bomb_pos.x), static_cast<uint32_t>(bomb_pos.y), 1);
            sim.grid_mut().place_firewall(static_cast<uint32_t>(fire_pos.x), static_cast<uint32_t>(fire_pos.y), 0);

            // Block all other neighbors of fire_pos except bomb_pos so the throw is forced towards the bomb (northeast)
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    if (dx == 0 && dy == 0) continue;
                    TileCoord cand{fire_pos.x + dx, fire_pos.y + dy};
                    if (cand != bomb_pos) {
                        sim.grid_mut().set_terrain(cand.x, cand.y, TERRAIN_OBSTACLE);
                    }
                }
            }

            uint32_t ant = sim.spawn_unit(0, AntType::Worker, fire_pos);
            sim.resolve_fire_contact(ant, 1, 0);
            ASSERT_EQ(sim.get_unit(ant).loco_action, AntUnit::kActionHit);
            for (int t = 0; t < 120; ++t) sim.tick();

            // The bomb went off when the flight ended on it (the ant lost 2 more hp)
            ASSERT_FALSE(sim.grid().has_bomb_at(bomb_pos));
            ASSERT_TRUE(sim.get_unit(ant).hp <= 7);
        }
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.63 Authentic 1998 Bomb Blast Flyback Starts Directly at Bomb Tile
    // ------------------------------------------------------------------------
    TEST_CASE("12.63 Authentic 1998 Bomb Blast Flyback Starts Directly at Bomb Tile") {
        bool tested_flight = false;
        for (uint32_t seed = 0; seed < 100 && !tested_flight; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 60000);
            TileCoord bomb_pos{20, 20};
            sim.grid_mut().place_bomb(static_cast<uint32_t>(bomb_pos.x), static_cast<uint32_t>(bomb_pos.y), 1);

            uint32_t ant = sim.spawn_unit(0, AntType::Worker, bomb_pos);
            auto& u = sim.get_unit(ant);
            u.facing = ants::assets::Direction::North;
            u.hp = 10;
            u.max_hp = 10;

            sim.trigger_bomb_detonation(ant, bomb_pos, 0, 0);

            const auto& u_post = sim.get_unit(ant);
            if (u_post.state != UnitState::Knockback || u_post.knock_flag) continue;   // a dud burns the ant on the spot
            tested_flight = true;

            // The gb clip starts ON the bomb tile centre (the blast puts the ant there) ...
            ASSERT_EQ(u_post.pixel_x, bomb_pos.x * 32 + 16);
            ASSERT_EQ(u_post.pixel_y, bomb_pos.y * 32 + 16);

            // ... and carries it 4 tiles opposite its facing (facing North: recoil South, dest_ty == 24) in one
            // 128 px jump of the clip (Table 4 frame 1 dy = +128) at about 300 ms
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return u_post.pixel_y != bomb_pos.y * 32 + 16; }) >= 0);
            ASSERT_EQ(u_post.pixel_x, bomb_pos.x * 32 + 16);
            ASSERT_EQ(u_post.pixel_y, (bomb_pos.y + 4) * 32 + 16);
            ASSERT_EQ(u_post.pos, (TileCoord{bomb_pos.x, bomb_pos.y + 4}));

            // The ant stays at the destination while the rest of the clip plays
            for (int t = 0; t < 10; ++t) {
                sim.tick();
                ASSERT_EQ(u_post.pixel_x, bomb_pos.x * 32 + 16);
                ASSERT_EQ(u_post.pixel_y, (bomb_pos.y + 4) * 32 + 16);
            }
        }
        ASSERT_TRUE(tested_flight);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.64 Thief Infiltration Bottlecap Row Alignment, Untargetability & In-Place Re-Trigger
    // ------------------------------------------------------------------------
    TEST_CASE("12.64 Thief Raid Clip Alignment, Re-Trigger By Order And Raid On Arrival") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_anthill(1, {10, 10});

        uint32_t thief_id = sim.spawn_unit(0, AntType::Thief, TileCoord{13, 12});
        auto& thief = sim.get_unit(thief_id);
        ASSERT_EQ(thief.type, AntType::Thief);

        // Step 1: Infiltrate enemy base with 0 enemy score (unsuccessful steal)
        sim.start_thief_infiltration(thief_id, 1);
        ASSERT_EQ(thief.state, UnitState::Infiltrating);

        // Advance ticks for the 3510 ms raid clip to complete
        for (int t = 0; t < 75; ++t) {
            sim.tick();
        }

        // On an empty steal the thief stands idle on the raid tile (bx + 3, by + 2) = (13, 12) at its centre
        const auto& u_idle = sim.get_unit(thief_id);
        ASSERT_EQ(u_idle.state, UnitState::Idle);
        ASSERT_EQ(u_idle.pos.x, 13);
        ASSERT_EQ(u_idle.pos.y, 12);
        ASSERT_EQ(u_idle.pixel_x, 13 * 32 + 16);
        ASSERT_EQ(u_idle.pixel_y, 12 * 32 + 16);

        // Step 2: In-place steal re-trigger while standing on the raid tile
        AntOrder inf_order;
        inf_order.ant_id = thief_id;
        inf_order.type = OrderType::InfiltrateAnthill;
        inf_order.target_entity_id = 1;
        inf_order.target_x = 13;
        inf_order.target_y = 12;
        sim.issue_order(inf_order);
        ASSERT_EQ(sim.get_unit(thief_id).state, UnitState::Infiltrating);

        // The raid clip refuses orders while it runs
        sim.issue_move_order(thief_id, TileCoord{14, 12});
        ASSERT_EQ(sim.get_unit(thief_id).state, UnitState::Infiltrating);
        for (int t = 0; t < 75; ++t) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(thief_id).state, UnitState::Idle);

        // Step 3: Moving away and back onto the enemy hill raids again upon arrival
        sim.issue_move_order(thief_id, TileCoord{14, 12});
        for (int t = 0; t < 40 && sim.get_unit(thief_id).pos != (TileCoord{14, 12}); ++t) sim.tick();
        ASSERT_EQ(sim.get_unit(thief_id).pos, (TileCoord{14, 12}));
        sim.issue_move_order(thief_id, TileCoord{13, 12});
        for (int t = 0; t < 40; ++t) {
            sim.tick();
            if (sim.get_unit(thief_id).state == UnitState::Infiltrating) break;
        }
        ASSERT_EQ(sim.get_unit(thief_id).state, UnitState::Infiltrating);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.65 Fire Contact Bounce Plays Ground Bounce Animation State
    // ------------------------------------------------------------------------
    TEST_CASE("12.65 Fire Contact Plays The Hit Clip (gh): One Hit Point, A One Tile Flight") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        TileCoord fire_pos{15, 15};
        sim.grid_mut().place_firewall(static_cast<uint32_t>(fire_pos.x), static_cast<uint32_t>(fire_pos.y), 0);

        uint32_t ant = sim.spawn_unit(0, AntType::Worker, fire_pos);
        sim.resolve_fire_contact(ant, 1, 0);

        const auto& u = sim.get_unit(ant);
        // Contact with fire is Blast(1): the ant plays the gh clip (action 0xE) and loses one hit point
        ASSERT_EQ(u.state, UnitState::Flinch);
        ASSERT_EQ(u.loco_action, AntUnit::kActionHit);
        ASSERT_EQ(u.hp, 9);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.66 Bomb Dud Stationary Position Lock
    // ------------------------------------------------------------------------
    TEST_CASE("12.66 Bomb Dud Stationary Position Lock") {
        bool found_dud = false;
        for (uint32_t seed = 0; seed < 100; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 60000);
            TileCoord bomb_pos{20, 20};
            sim.grid_mut().place_bomb(static_cast<uint32_t>(bomb_pos.x), static_cast<uint32_t>(bomb_pos.y), 1);
            uint32_t ant = sim.spawn_unit(0, AntType::Worker, bomb_pos);
            sim.get_unit(ant).hp = 10;
            sim.trigger_bomb_detonation(ant, bomb_pos, 0, 0);

            if (sim.get_unit(ant).state == UnitState::Burn) {
                found_dud = true;
                const auto& u = sim.get_unit(ant);
                // Ant is locked firmly at bomb tile without pushing or sliding
                ASSERT_EQ(u.pos.x, bomb_pos.x);
                ASSERT_EQ(u.pos.y, bomb_pos.y);
                ASSERT_EQ(u.pixel_x, bomb_pos.x * 32 + 16);
                ASSERT_EQ(u.pixel_y, bomb_pos.y * 32 + 16);

                // Advance several ticks: ant must remain firmly locked at bomb position
                for (int t = 0; t < 10; ++t) {
                    sim.tick();
                    const auto& u_burn = sim.get_unit(ant);
                    if (u_burn.state == UnitState::Burn) {
                        ASSERT_EQ(u_burn.pixel_x, bomb_pos.x * 32 + 16);
                        ASSERT_EQ(u_burn.pixel_y, bomb_pos.y * 32 + 16);
                    }
                }
                break;
            }
        }
        ASSERT_TRUE(found_dud);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.67 Mutual Ant-Ant Collision Bouncing
    // ------------------------------------------------------------------------
    TEST_CASE("12.67 Head-On Walkers Wait, Re-Plan and Pass Without Bouncing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        uint32_t ant1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t ant2 = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 10});

        sim.issue_move_order(ant1, TileCoord{15, 10});
        sim.issue_move_order(ant2, TileCoord{10, 10});
        // Each clicked tile holds the other ant, so GoTo takes the first free tile of the ring scan around it
        // (west column first, top first): ant 1 heads for (14, 9), ant 2 for (9, 9).
        ASSERT_EQ(sim.get_unit(ant1).final_dest, (TileCoord{14, 9}));
        ASSERT_EQ(sim.get_unit(ant2).final_dest, (TileCoord{9, 9}));

        // Walking ants never bounce off each other in the original: the occupancy grid stops them at the tile
        // boundary, they wait (ANTPAUSE, 300 ms) while the other ant is moving, and re-plan around it (bump)
        // once it stands still.
        bool bounced = false;
        bool shared_tile = false;
        bool waited = false;
        for (int t = 0; t < 200; ++t) {
            sim.tick();
            const auto& u1 = sim.get_unit(ant1);
            const auto& u2 = sim.get_unit(ant2);
            if (u1.state == UnitState::Knockback || u2.state == UnitState::Knockback ||
                u1.state == UnitState::Flinch || u2.state == UnitState::Flinch) {
                bounced = true;
            }
            if (u1.pause_active || u2.pause_active) waited = true;
            if (u1.occ_tile == u2.occ_tile) shared_tile = true;
            if (u1.pos == TileCoord{14, 9} && u2.pos == TileCoord{9, 9} &&
                u1.waypoints.empty() && u2.waypoints.empty()) {
                break;
            }
        }
        ASSERT_FALSE(bounced);
        ASSERT_FALSE(shared_tile);
        ASSERT_TRUE(waited);
        ASSERT_TRUE(sim.has_audio_event(SoundID::Bump));   // the re-plan around the waiting ant
        ASSERT_EQ(sim.get_unit(ant1).pos, (TileCoord{14, 9}));
        ASSERT_EQ(sim.get_unit(ant2).pos, (TileCoord{9, 9}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.68 Sidebar Pedestal Buttons Dimensions and Drop Shadows
    // ------------------------------------------------------------------------
    TEST_CASE("12.68 Sidebar Pedestal Buttons Dimensions and Drop Shadows") {
        HUD hud;
        hud.init(0);
        const auto& move_btn = hud.get_move_pedestal_button();
        const auto& abil_btn = hud.get_ability_pedestal_button();

        // The pedestal slots of FUN_01028d30 (half-open): slot 1 (482, 152) - (525, 225), slot 2 (539, 152) - (582, 225)
        ASSERT_EQ(move_btn.x, 482);
        ASSERT_EQ(move_btn.y, 152);
        ASSERT_EQ(move_btn.w, 43);
        ASSERT_EQ(move_btn.h, 73);

        ASSERT_EQ(abil_btn.x, 539);
        ASSERT_EQ(abil_btn.y, 152);
        ASSERT_EQ(abil_btn.w, 43);
        ASSERT_EQ(abil_btn.h, 73);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.132 Authentic 1998 Parity Suite: Stun Pacing, Bomb Deflection Facing, Placement Detonation & Combat AI Punch/Knockback/Water Kill
    // ------------------------------------------------------------------------
    TEST_CASE("12.132 Authentic 1998 Parity: Stun Pacing, Bomb Deflection, Placement Detonation & Combat AI") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        // 1. Bomb Placement Pre-Check & Placeholder While The Plant Clip Plays
        uint32_t occupied_ant = sim.spawn_unit(1, AntType::Worker, TileCoord{12, 10});
        uint32_t bomber1 = sim.spawn_unit(0, AntType::Bomber, TileCoord{11, 10});
        ASSERT_EQ(sim.get_unit(occupied_ant).pos, (TileCoord{12, 10}));
        // Bomber cannot initiate placement on a tile already occupied by an ant
        ASSERT_FALSE(sim.plant_bomb(bomber1, TileCoord{12, 10}));

        // Now initiate valid bomb placement on open tile (15, 15) with the plant clip (instant = false)
        uint32_t bomber2 = sim.spawn_unit(0, AntType::Bomber, TileCoord{14, 15});
        ASSERT_TRUE(sim.plant_bomb(bomber2, TileCoord{15, 15}, false));
        ASSERT_EQ(sim.get_unit(bomber2).state, UnitState::PlantingBomb);

        // The reserved tile is solid: a walker ordered onto it stops next to it and never sets the bomb off
        uint32_t walker = sim.spawn_unit(1, AntType::Worker, TileCoord{15, 17});
        sim.issue_move_order(walker, TileCoord{15, 15});
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.grid().has_bomb_at({15, 15}); }) >= 1300);
        run_ms(sim, 2000);
        const auto& w_ant = sim.get_unit(walker);
        ASSERT_EQ(w_ant.hp, 10);                                  // untouched: nobody stepped on the reserved tile
        ASSERT_TRUE(sim.grid().has_bomb_at({15, 15}));

        // A walker ordered onto the finished bomb sets it off: two hit points, a bomb flight (or the dud burn)
        sim.issue_move_order(walker, TileCoord{15, 15});
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return !sim.grid().has_bomb_at({15, 15}); }) >= 0);
        ASSERT_EQ(w_ant.hp, 8);                                   // Took 2 HP bomb blast damage
        ASSERT_TRUE(w_ant.state == UnitState::Knockback || w_ant.state == UnitState::Burn);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombDetonate));

        // 2. Bomb Knockback Deflection: the ant is thrown opposite its facing; a solid landing tile makes PickLanding
        //    try the next direction (d + 1)
        bool tested_deflection = false;
        for (uint32_t s = 1; s <= 40 && !tested_deflection; ++s) {
            sim.init_test_world(60, 60, s, 60000);
            sim.grid_mut().place_bomb(20, 20, 0);
            // Block the South landing tile (20, 24) with an obstacle
            sim.grid_mut().get_cell_mut(20, 24).terrain_type = ants::sim::TERRAIN_OBSTACLE;

            // Ant at (20, 20) steps on the bomb moving North
            uint32_t bomb_victim = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
            auto* bv_unit = const_cast<AntUnit*>(&sim.get_unit(bomb_victim));
            bv_unit->facing = Direction::North;

            // Trigger detonation
            sim.trigger_bomb_detonation(bomb_victim, TileCoord{20, 20}, 0, -1);
            const auto& bv_post = sim.get_unit(bomb_victim);
            if (bv_post.state == UnitState::Knockback && !bv_post.knock_flag) {
                tested_deflection = true;
                // Deflected from South because (20, 24) is blocked; the clip faces the direction of the deflected throw
                ASSERT_NE(bv_post.facing, Direction::North);
                ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return bv_post.pos != TileCoord{20, 20}; }) >= 0);
                // Destination is 4 tiles from bomb
                int32_t dist_from_bomb = std::max(std::abs(bv_post.pos.x - 20), std::abs(bv_post.pos.y - 20));
                ASSERT_EQ(dist_from_bomb, 4);
                ASSERT_NE(bv_post.pos, (TileCoord{20, 24})); // Blocked South was avoided
                ASSERT_EQ(bv_post.pos, (TileCoord{16, 24})); // the next direction: South-West
            }
        }
        ASSERT_TRUE(tested_deflection);

        // 3. Table 4 Stun Animation Duration & Pacing
        AssetArchive archive;
        ASSERT_TRUE(archive.load_chd("Original-Ants/ants.chd"));
        const auto* worker_sd = archive.find_animation("agsd301");
        ASSERT_TRUE(worker_sd != nullptr);
        ASSERT_EQ(worker_sd->subitems.size(), 25u);
        ASSERT_EQ(worker_sd->subitems[0].val3, 125u); // Authentic 125ms per subitem (8 FPS)

        const auto* bomber_sd = archive.find_animation("absd301");
        ASSERT_TRUE(bomber_sd != nullptr);
        ASSERT_EQ(bomber_sd->subitems[0].val3, 105u); // Authentic 105ms per subitem

        const auto* carrying_sd = archive.find_animation("hgsd301");
        ASSERT_TRUE(carrying_sd != nullptr);
        ASSERT_EQ(carrying_sd->subitems[0].val3, 125u); // Preserves 125ms duration for carrying variant

        // 4. Combat ant auto-engage punch (2 hp), knockback into water drowns the victim
        sim.init_test_world(60, 60, 42, 60000);
        // Make tile (24, 20) water
        sim.grid_mut().get_cell_mut(24, 20).terrain_type = ants::sim::TERRAIN_WATER;

        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{19, 20});

        // Enemy Worker at (20, 20)
        uint32_t victim_id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});

        // The enemy is adjacent: the idle hook starts the attack at once; the punch sound is frame 2 of the clip
        bool punch_sound = false;
        for (int t = 0; t < 40 && sim.get_unit(victim_id).hp == 10; ++t) {
            sim.tick();
        }
        for (int t = 0; t < 10; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::HeavyPunch)) punch_sound = true;
        }
        ASSERT_TRUE(punch_sound);
        ASSERT_EQ(sim.get_unit(combat_id).loco_action, AntUnit::kActionAttack);
        ASSERT_EQ(sim.get_unit(victim_id).hp, 8); // 2 HP punch damage

        // Punch directly into water test: Worker on (23, 20) punched East into water (24..30, 20)
        sim.init_test_world(60, 60, 42, 60000);
        for (int tx = 24; tx <= 30; ++tx) {
            sim.grid_mut().get_cell_mut(static_cast<uint32_t>(tx), 20).terrain_type = ants::sim::TERRAIN_WATER;
        }
        uint32_t puncher = sim.spawn_unit(0, AntType::Combat, TileCoord{22, 20});
        uint32_t water_victim = sim.spawn_unit(1, AntType::Worker, TileCoord{23, 20});
        sim.execute_melee_attack(puncher, water_victim);

        // Advance through the flight until the water landing
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            if (sim.get_unit(water_victim).state == UnitState::Drowning) break;
        }
        const auto& wv_landed = sim.get_unit(water_victim);
        ASSERT_EQ(wv_landed.state, UnitState::Drowning);
        for (int t = 0; t < 5; ++t) sim.tick();   // the drowning sound is frame 1 of the clip (100 ms)
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
        for (int t = 0; t < 120 && !sim.get_unit(water_victim).removed; ++t) sim.tick();
        ASSERT_TRUE(sim.get_unit(water_victim).removed);
        ASSERT_EQ(sim.get_unit(water_victim).death_status, DeathStatus::Drowned);
    } TEST_END();

    TEST_CASE("12.133 Combat Ant Walk Animation Completion Before Attack") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        // Combat Ant spawned at (15, 20), target enemy Worker at (18, 20): the scan finds it (ring 3)
        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{15, 20});
        uint32_t victim_id = sim.spawn_unit(1, AntType::Worker, TileCoord{18, 20});
        uint32_t initial_vic_hp = sim.get_unit(victim_id).hp;

        // Advance simulation: the combat ant walks towards (17, 20)
        bool hit_occurred = false;
        for (int t = 0; t < 120; ++t) {
            sim.tick();
            const auto& c_u = sim.get_unit(combat_id);
            if (c_u.state == UnitState::Walking) {
                // While walking mid-stride, no damage can be dealt
                ASSERT_EQ(sim.get_unit(victim_id).hp, initial_vic_hp);
            }
            if (sim.get_unit(victim_id).hp < initial_vic_hp) {
                hit_occurred = true;
                // Attack connected: Combat Ant must have finished walking into adjacent tile (17, 20)
                ASSERT_EQ(c_u.pos, (TileCoord{17, 20}));
                ASSERT_EQ(c_u.pixel_x, 17 * 32 + 16);
                ASSERT_EQ(c_u.pixel_y, 20 * 32 + 16);
                break;
            }
        }
        ASSERT_TRUE(hit_occurred);
    } TEST_END();

    TEST_CASE("12.134 Knockback Trajectory Origin: The gb Clip Jumps 128 px In One Frame Step From The Contact Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{19, 20});
        uint32_t victim_id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});

        int32_t expected_start_px = 20 * 32 + 16; // 656
        int32_t expected_start_py = 20 * 32 + 16; // 656
        int32_t expected_target_px = 24 * 32 + 16; // 784

        sim.execute_melee_attack(combat_id, victim_id);

        // At the contact the victim stands on its tile until the strike frame
        const auto& vic = sim.get_unit(victim_id);
        ASSERT_EQ(vic.pixel_x, expected_start_px);
        ASSERT_EQ(vic.pixel_y, expected_start_py);
        ASSERT_NE(vic.state, UnitState::Knockback);

        // Strike frame (200 ms): the flight clip starts on the original tile
        ASSERT_EQ(wait_ms(sim, 1000, [&]() { return vic.state == UnitState::Knockback; }), 200);
        ASSERT_EQ(vic.pixel_x, expected_start_px);
        ASSERT_EQ(vic.pixel_y, expected_start_py);

        // The clip then moves it by 128 px in one step (no smooth interpolation in the original)
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return vic.pixel_x != expected_start_px; }) >= 0);
        ASSERT_EQ(vic.pixel_x, expected_target_px);
        ASSERT_EQ(vic.pixel_y, expected_start_py);
        ASSERT_EQ(vic.state, UnitState::Knockback);

        // The rest of the clip plays on the landing tile; the flight ends without a stun
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return vic.state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(vic.pixel_x, expected_target_px);
        ASSERT_FALSE(vic.is_stunned());
    } TEST_END();

    TEST_CASE("12.135 Combat Ant Punches Ant Over Small Terrain Barrier to Open Ground") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        // Create 1-tile rock obstacle at (21, 20)
        sim.grid_mut().get_cell_mut(21, 20).terrain_type = ants::sim::TERRAIN_OBSTACLE;

        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{19, 20});
        uint32_t victim_id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});

        sim.execute_melee_attack(combat_id, victim_id);

        const auto& vic = sim.get_unit(victim_id);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return vic.state == UnitState::Knockback; }) >= 0);
        // Flight destination cleared the rock at (21, 20) and anchored at (24, 20)
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return vic.pos == TileCoord{24, 20}; }) >= 0);

        // Advance through the flight until the landing frames end
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return vic.state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(vic.pos, (TileCoord{24, 20}));
        ASSERT_EQ(vic.pixel_x, 24 * 32 + 16);
        ASSERT_FALSE(vic.is_stunned());
    } TEST_END();

    TEST_CASE("12.136 Combat Ant Punches Ant Over Small Terrain Barrier Into Water Drowning") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        // Create 1-tile rock obstacle at (21, 20)
        sim.grid_mut().get_cell_mut(21, 20).terrain_type = ants::sim::TERRAIN_OBSTACLE;
        // Create water tiles on the other side at (22..30, 20)
        for (int tx = 22; tx <= 30; ++tx) {
            sim.grid_mut().get_cell_mut(static_cast<uint32_t>(tx), 20).terrain_type = ants::sim::TERRAIN_WATER;
        }

        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{19, 20});
        uint32_t victim_id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});

        sim.execute_melee_attack(combat_id, victim_id);

        // Advance through the flight until the water landing
        const auto& vic_drowned = sim.get_unit(victim_id);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return vic_drowned.state == UnitState::Drowning; }) >= 0);
        ASSERT_EQ(vic_drowned.pos, (TileCoord{24, 20}));
        run_ms(sim, 250);
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !vic_drowned.is_alive(); }) >= 0);
        ASSERT_EQ(vic_drowned.death_status, DeathStatus::Drowned);
    } TEST_END();

    TEST_CASE("12.137 Non-Thief Friendly Unit Clicking Enemy Base Stops Without CantGo (GoTo, FUN_0101fc50)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_anthill(0, TileCoord{2, 2});
        sim.set_anthill(1, TileCoord{8, 8});

        ViewportCamera camera{};
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Spawn friendly worker at (5, 8)
        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 8});

        // Select worker
        hud.select_ant(worker);
        ASSERT_TRUE(hud.is_ant_selected(worker));

        // Click on enemy base at (8, 8)
        int32_t bx = (8 * 32 + 16) - camera.world_x + HUD::PLAYFIELD_X;
        int32_t by = (8 * 32 + 16) - camera.world_y + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(bx, by, 1, sim, camera);
        hud.handle_mouse_up(bx, by, 1, sim, camera);

        // Verify CantGo was NOT triggered
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));

        const auto& u = sim.get_unit(worker);
        // The original GoTo stops a non-thief that is ordered onto an enemy hill (StopSync) and requests no path
        ASSERT_FALSE(sim.has_pending_path(worker));
        ASSERT_TRUE(u.waypoints.empty());
        ASSERT_NE(u.state, UnitState::Infiltrating);

        for (int t = 0; t < 40; ++t) {
            sim.tick();
        }

        const auto& u_done = sim.get_unit(worker);
        ASSERT_EQ(u_done.state, UnitState::Idle);
        ASSERT_EQ(u_done.pos, (TileCoord{5, 8}));
        // Never inside the 4x4 enemy base footprint (8..11, 8..11)
        bool inside_enemy_base = (u_done.pos.x >= 8 && u_done.pos.x < 12 && u_done.pos.y >= 8 && u_done.pos.y < 12);
        ASSERT_FALSE(inside_enemy_base);
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));
    } TEST_END();

    TEST_CASE("12.138 Clicking Base Queue Slots Issues Standard Move Without Auto-Queueing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_anthill(0, TileCoord{5, 5});

        ViewportCamera camera{};
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Spawn friendly worker with 2 HP (damaged, max 10) at (2, 5)
        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{2, 5});
        sim.get_unit(worker).hp = 2;

        // Select worker
        hud.select_ant(worker);
        ASSERT_TRUE(hud.is_ant_selected(worker));

        // Click directly on queue slot (bx - 1, by + 3) -> (4, 8)
        int32_t qx = (4 * 32 + 16) - camera.world_x + HUD::PLAYFIELD_X;
        int32_t qy = (8 * 32 + 16) - camera.world_y + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(qx, qy, 1, sim, camera);
        hud.handle_mouse_up(qx, qy, 1, sim, camera);

        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (sim.get_unit(worker).state == UnitState::Idle && sim.get_unit(worker).pos == (TileCoord{4, 8})) break;
        }

        const auto& u = sim.get_unit(worker);
        ASSERT_EQ(u.pos, (TileCoord{4, 8}));
        ASSERT_EQ(u.state, UnitState::Idle);
        // Must NOT be queued into base queue!
        ASSERT_FALSE(sim.is_ant_in_base_queue(worker));
        ASSERT_NE(u.state, UnitState::EnteringBase);
    } TEST_END();

    TEST_CASE("12.139 Combat Ant Knockback Near Base Never Lands Inside Base Footprint or Queue Locations") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_anthill(0, TileCoord{20, 20});

        // Spawn Player 0 worker at (18, 21)
        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{18, 21});
        // Spawn Player 1 combat ant at (17, 21) facing east directly towards base
        uint32_t combat = sim.spawn_unit(1, AntType::Combat, TileCoord{17, 21});

        // Combat ant punches worker eastward towards anthill
        sim.execute_melee_attack(combat, worker);

        const auto& w_knocked = sim.get_unit(worker);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return w_knocked.state == UnitState::Knockback; }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return w_knocked.pos.x != 18 || w_knocked.pos.y != 21; }) >= 0);

        // Landing pos must NOT be inside 4x4 base footprint or queue slots
        int32_t lx = w_knocked.pos.x;
        int32_t ly = w_knocked.pos.y;
        bool in_base = (lx >= 20 && lx < 24 && ly >= 20 && ly < 24);
        bool in_queue = (lx == 19 && ly >= 20 && ly <= 23);
        ASSERT_FALSE(in_base);
        ASSERT_FALSE(in_queue);

        // Step through knockback flight
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            ASSERT_NE(sim.get_unit(worker).state, UnitState::EnteringBase);
        }

        const auto& w_landed = sim.get_unit(worker);
        int32_t fx = w_landed.pos.x;
        int32_t fy = w_landed.pos.y;
        bool landed_in_base = (fx >= 20 && fx < 24 && fy >= 20 && fy < 24);
        bool landed_in_queue = (fx == 19 && fy >= 20 && fy <= 23);
        ASSERT_FALSE(landed_in_base);
        ASSERT_FALSE(landed_in_queue);
        ASSERT_NE(w_landed.state, UnitState::EnteringBase);
    } TEST_END();

    TEST_CASE("12.140 A 1 HP Survivor Of A Hit Goes Home, A Thief With Loot Goes Home, Nobody Else Does") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_anthill(0, TileCoord{20, 20});
        sim.set_anthill(1, TileCoord{40, 40});

        // 1. A worker at 2 HP just standing there does not go home
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{25, 25});
        sim.get_unit(w1).hp = 2;
        for (int t = 0; t < 10; ++t) sim.tick();
        ASSERT_FALSE(sim.is_ant_in_base_queue(w1));
        ASSERT_EQ(sim.get_unit(w1).orig_order, AntUnit::kOrderNone);

        // 2. Hit down to 1 HP it goes home once the hit is over (LowHpCheck FUN_0101dded at the end of the flight)
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, TileCoord{26, 25});
        sim.execute_melee_attack(enemy, w1);
        ASSERT_EQ(sim.get_unit(w1).hp, 1u);
        ASSERT_NE(sim.get_unit(w1).orig_order, AntUnit::kOrderHome);      // not before the hit is over
        bool ordered_home = false;
        for (int t = 0; t < 80 && !ordered_home; ++t) {
            sim.tick();
            ordered_home = (sim.get_unit(w1).orig_order == AntUnit::kOrderHome);
        }
        ASSERT_TRUE(ordered_home);

        // 3. A thief that finished a raid with loot starts for its own entrance
        sim.set_player_score(1, 100);
        uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
        sim.start_thief_infiltration(thief, 1);
        bool thief_home = false;
        for (int t = 0; t < 100 && !thief_home; ++t) {
            sim.tick();
            // heading for the entrance, or for the waiting tile while the entrance is claimed by another ant
            thief_home = (sim.get_unit(thief).orig_order == AntUnit::kOrderHome || sim.is_ant_in_base_queue(thief));
        }
        ASSERT_TRUE(thief_home);
        ASSERT_TRUE(sim.get_unit(thief).is_thief_steal);
        ASSERT_TRUE(sim.get_unit(thief).is_holding());
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.141: ISLANDS.LVL Corner Power-Up Hats Skill-Based Walk-On / Walk-Off
    // ------------------------------------------------------------------------
    TEST_CASE("12.141: ISLANDS.LVL Corner Power-Up Hats Skill-Based Walk-On / Walk-Off (each next hat is ordered inside the window of the one before)") {
        auto build = [](SimulationEngine& sim) {
            sim.init_test_world(30, 30, 100, 60000);
            // The top-left corner of ISLANDS.LVL: a tunnel one tile wide whose only way is a row of hats, the tiles beside
            // the column (col 1, rows 2-4) blocked:
            // (0, 4) and (0, 3): Mason Hats (type 2), (0, 2) and (0, 1): Swimmer Hats (type 5)
            sim.grid_mut().set_terrain(1, 2, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(1, 3, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(1, 4, TERRAIN_OBSTACLE);
            sim.grid_mut().place_powerup(0, 4, 2);
            sim.grid_mut().place_powerup(0, 3, 2);
            sim.grid_mut().place_powerup(0, 2, 5);
            sim.grid_mut().place_powerup(0, 1, 5);
        };

        {   // Ordering the second hat before the first one has been crossed: no path (the first hat is solid), "Can't go there."
            SimulationEngine sim;
            build(sim);
            uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{0, 5});
            sim.issue_move_order(ant, {0, 3});
            ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return sim.has_news_event(0, 0x3A); }) >= 0);
            ASSERT_TRUE(sim.grid().has_powerup_at({0, 4}));
            ASSERT_TRUE(sim.grid().has_powerup_at({0, 3}));
            ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker);
        }

        SimulationEngine sim;
        build(sim);
        // Worker starts at (0, 5)
        uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{0, 5});
        auto crossed = [&](TileCoord t) {
            return wait_ms(sim, 3000, [&]() { return sim.get_unit(ant).pos == t; }) >= 0;
        };

        // Step 1: Order to (0, 4) and wait until the ant has crossed into its tile (the walk has not landed yet)
        sim.issue_move_order(ant, {0, 4});
        ASSERT_TRUE(crossed(TileCoord{0, 4}));
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker);
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 4}));

        // Step 2: Quick click inside the window! Redirect to (0, 3): the ant is snapped onto (0, 4), its walk is dropped
        sim.issue_move_order(ant, {0, 3});
        ASSERT_EQ(sim.get_unit(ant).pixel_y, 4 * 32 + 16);
        ASSERT_TRUE(sim.get_unit(ant).waypoints.empty());
        ASSERT_TRUE(crossed(TileCoord{0, 3}));
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker); // Still Worker!
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 4}));      // Hat at (0, 4) untouched!

        // Step 3: Quick click to (0, 2) (the target Swimmer hat) inside the next window
        sim.issue_move_order(ant, {0, 2});
        ASSERT_EQ(sim.get_unit(ant).pixel_y, 3 * 32 + 16);
        ASSERT_TRUE(crossed(TileCoord{0, 2}));
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker);
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 3}));      // Hat at (0, 3) untouched!

        // Step 4: Player now lets the ant land on (0, 2): the pick-up happens in the tick of the landing
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return picking_up(sim.get_unit(ant)); }) >= 0);
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Swimmer); // Transformed into Swimmer!
        ASSERT_FALSE(sim.grid().has_powerup_at({0, 2}));     // Hat at (0, 2) consumed!
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 1}));      // the next hat is still there
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 3}));
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 4}));

        // The clip ends 840 ms after the snap
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(sim.get_unit(ant)));
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Swimmer);
    } TEST_END();

    // -------------------------------------------------------------------------
    // Effects: original creators (Ants.exe FUN_01010008 call sites), anchors and lifetimes
    // -------------------------------------------------------------------------
    TEST_CASE("12.114 Bombex Explosion Sprite Exists For Every Detonation At The Bomb Tile Top-Left For 680 ms") {
        int duds = 0, blasts = 0;
        for (uint32_t seed = 1; seed <= 120; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 600000);
            uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
            sim.grid_mut().place_bomb(15, 15, 1);
            sim.trigger_bomb_detonation(victim, TileCoord{15, 15}); // the ant sets the bomb off (20 % dud)

            const auto& u = sim.get_unit(victim);
            if (u.knock_flag) ++duds; else ++blasts;

            const VisualEffect* fx = nullptr;
            const auto& effects = sim.get_world_state().effects;
            for (const auto& e : effects) if (e.anim_name == "bombex") { fx = &e; break; }
            ASSERT_TRUE(fx != nullptr);                       // dud and full blast alike
            ASSERT_EQ(fx->px, 15 * 32);                       // tile top-left, not the tile centre
            ASSERT_EQ(fx->py, 15 * 32);
            ASSERT_EQ(fx->y_key, 15 * 32);                    // sorts with row*32
            ASSERT_EQ(fx->duration_ms, 680u);                 // sum of the ten Table-4 frames
            ASSERT_TRUE(fx->fog_gated);
        }
        ASSERT_TRUE(duds > 0);
        ASSERT_TRUE(blasts > 0);

        // Lethal hit: the sprite still exists and lives until its last frame ends (650 ms yes, 700 ms no)
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 600000);
        uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
        const_cast<AntUnit*>(&sim.get_unit(victim))->hp = 2;
        sim.grid_mut().place_bomb(15, 15, 1);
        sim.trigger_bomb_detonation(victim, TileCoord{15, 15});
        auto has_bombex = [&]() {
            for (const auto& e : sim.get_world_state().effects) if (e.anim_name == "bombex") return true;
            return false;
        };
        ASSERT_TRUE(has_bombex());
        for (int t = 0; t < 13; ++t) sim.tick();
        ASSERT_TRUE(has_bombex());   // 650 ms
        sim.tick();
        ASSERT_FALSE(has_bombex());  // 700 ms >= 680 ms
    } TEST_END();

    TEST_CASE("12.115 Fire Wall Burnout Puff And The 180 Second Lifetime Task Arming Rule") {
        // Placed with 600 s left: the wall burns out after 180 s and leaves a sputter puff at the tile top-left
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 600000);
            uint32_t fire_ant = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 10});
            ASSERT_TRUE(sim.ignite_fire(fire_ant, TileCoord{10, 11}));
            ASSERT_EQ(sim.grid().get_cell(10, 11).timer_ticks, 3600u);
            int ticks = 0;
            while (sim.has_fire_at(TileCoord{10, 11}) && ticks < 4000) { sim.tick(); ++ticks; }
            ASSERT_EQ(ticks, 3600);
            const VisualEffect* fx = nullptr;
            const auto& effects = sim.get_world_state().effects;
            for (const auto& e : effects) if (e.anim_name == "sputter") { fx = &e; break; }
            ASSERT_TRUE(fx != nullptr);
            ASSERT_EQ(fx->px, 10 * 32);
            ASSERT_EQ(fx->py, 11 * 32);
            ASSERT_EQ(fx->duration_ms, 830u);
        }
        // Placed with 180 s or less left: the lifetime task is never created, the wall does not burn out
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 600000);
            sim.set_match_time_remaining_ms(170000);
            uint32_t fire_ant = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 10});
            ASSERT_TRUE(sim.ignite_fire(fire_ant, TileCoord{10, 11}));
            ASSERT_EQ(sim.grid().get_cell(10, 11).timer_ticks, 0u);
        }
    } TEST_END();

    TEST_CASE("12.116 Bridge Collapse Leaves A bsputter Puff And Swimmers Splash With dsplash At The Tile Top-Left") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 600000);
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);
        sim.set_bridge_at(TileCoord{20, 20}, 4, 3);
        uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{20, 20});
        (void)swimmer;
        for (int t = 0; t < 3; ++t) sim.tick(); // timer expires
        ASSERT_FALSE(sim.has_bridge_at(TileCoord{20, 20}));
        const VisualEffect* puff = nullptr;
        const VisualEffect* splash = nullptr;
        const auto& effects = sim.get_world_state().effects;
        for (const auto& e : effects) {
            if (e.anim_name == "bsputter") puff = &e;
            if (e.anim_name == "dsplash") splash = &e;
        }
        ASSERT_TRUE(puff != nullptr);
        ASSERT_EQ(puff->px, 20 * 32);
        ASSERT_EQ(puff->py, 20 * 32);
        ASSERT_EQ(puff->duration_ms, 1220u);
        ASSERT_TRUE(splash != nullptr);
        ASSERT_EQ(splash->px, 20 * 32);
        ASSERT_EQ(splash->py, 20 * 32);
        ASSERT_EQ(splash->duration_ms, 460u);
    } TEST_END();

    TEST_CASE("12.117 Death Animation Is One Of death1..death4, Played On The Ant's Own Sprite For Its Full Original Duration (The First Frame Is Booked Twice)") {
        std::set<uint16_t> seen;
        // the clips' frame sums and first frames (ants.chd): death1 920 ms / 100, death2 1000 / 100, death3 980 / 100, death4 600 / 60
        static const uint32_t kSum[4] = { 920, 1000, 980, 600 };
        static const uint32_t kFirst[4] = { 100, 100, 100, 60 };
        for (uint32_t seed = 1; seed <= 300; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 600000);
            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
            auto* def = const_cast<AntUnit*>(&sim.get_unit(defender));
            def->hp = 1;
            sim.execute_melee_attack(attacker, defender);
            // the death clip starts when the flight of the victim ends (deferred death), not at the contact
            const int dying = wait_ms(sim, 3000, [&]() { return def->state == UnitState::Dead; });
            ASSERT_TRUE(dying >= 0);
            uint16_t clip = 0;
            for (const auto& a : sim.get_world_state().ants) if (a.id == defender) clip = a.loco_clip;
            ASSERT_TRUE(clip >= 102 && clip <= 105);
            seen.insert(clip);
            for (const auto& e : sim.get_world_state().effects) ASSERT_TRUE(e.anim_name.rfind("death", 0) != 0);   // the clip is the ant's, there is no effect
            // the ant stays (alive and drawn) until the clip has ended: the frame sum plus the doubled first frame, to the 50 ms tick
            const int gone = wait_ms(sim, 2000, [&]() { return def->removed; });
            ASSERT_TRUE(gone >= 0);
            const uint32_t expect = kSum[clip - 102] + kFirst[clip - 102];
            ASSERT_TRUE(static_cast<uint32_t>(gone) + 50u >= expect && static_cast<uint32_t>(gone) <= expect + 50u);
        }
        ASSERT_EQ(seen.size(), 4u); // rand() % 4 reaches every death animation
    } TEST_END();

    TEST_CASE("12.121 Score Change Bubbles At The Home Tile (Hatch Cost And Theft), 400 ms") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 600000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        sim.set_player_score(0, 500);
        sim.set_player_score(1, 300);

        // Hatching costs 200 points: a "-200" bubble at the hill exit tile top-left and the scoredn cue
        sim.clear_audio_events();
        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
        sim.tick();
        ASSERT_TRUE(sim.has_audio_event(SoundID::BaseScoreDn));
        const auto& bubbles = sim.get_world_state().score_bubbles;
        ASSERT_EQ(bubbles.size(), 1u);
        ASSERT_EQ(bubbles[0].amount, -200);
        ASSERT_EQ(bubbles[0].x, 21 * 32);
        ASSERT_EQ(bubbles[0].y, 21 * 32);
        ASSERT_EQ(bubbles[0].elapsed_ms, 0u);

        // Theft: the loot moves when the 3510 ms raid clip ends; the victim's home tile then shows "-stolen"
        uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
        sim.start_thief_infiltration(thief, 1);
        bool found_theft = false;
        for (int t = 0; t < 90 && !found_theft; ++t) {
            sim.tick();
            for (const auto& b : sim.get_world_state().score_bubbles) {
                if (b.amount == -MAX_THIEF_STEAL && b.x == 41 * 32 && b.y == 41 * 32) found_theft = true;
            }
        }
        ASSERT_TRUE(found_theft);

        // A bubble lives 400 ms: still shown 350 ms after it appeared, gone at 400 ms
        for (int t = 0; t < 7; ++t) sim.tick();
        ASSERT_EQ(sim.get_world_state().score_bubbles.size(), 1u);
        sim.tick();
        ASSERT_EQ(sim.get_world_state().score_bubbles.size(), 0u);
    } TEST_END();

    TEST_CASE("12.118 Food Dropper Animation Timeline (9 Frames, 820 ms)") {
        static const uint8_t expected_by_tick[17] = { 0, 0, 1, 1, 2, 2, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 8 };
        for (uint32_t tick = 0; tick < 17; ++tick) {
            ASSERT_EQ(effect_spec::dropper_frame_at(tick * 50u), expected_by_tick[tick]);
        }
        uint32_t total = 0;
        for (uint32_t d : effect_spec::kDropperFrameMs) total += d;
        ASSERT_EQ(total, effect_spec::kDropperMs);
    } TEST_END();

    // -------------------------------------------------------------------------
    // Group attack order (Ants.exe FUN_010287b5 with the attack flag), armed attack mode, pick rectangles (FUN_01026904)
    // -------------------------------------------------------------------------
    TEST_CASE("12.142 A Repeated Attack Click Changes Nothing: Ants That Already Attack The Tile Are Skipped (contact time equals one click)") {
        auto contact_ms = [&](int click_every_ms) -> int {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            uint32_t att = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 20});
            uint32_t vic = sim.spawn_unit(1, AntType::Worker, TileCoord{16, 20});
            HUD hud;
            hud.init(0);
            hud.select_ant(att, false);
            hud.dispatch_attack_order(vic, sim);
            for (int t = 0; t < 400; ++t) {
                if (click_every_ms > 0 && t > 0 && (t * 50) % click_every_ms == 0) hud.dispatch_attack_order(vic, sim);
                sim.tick();
                if (sim.get_unit(vic).hp < 10) return (t + 1) * 50;
            }
            return -1;
        };
        const int single = contact_ms(0);
        ASSERT_TRUE(single > 1000 && single < 6000);
        for (int every : {100, 300, 600}) {
            const int spam = contact_ms(every);
            ASSERT_TRUE(spam > 0);
            ASSERT_TRUE(spam == single);   // the skipped clicks do not even touch the ant
        }
    } TEST_END();

    TEST_CASE("12.143 Group Attack: Nearest Ant Answers (once), A Second Click Skips Every Ant That Already Attacks That Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t far_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 20});
        uint32_t near_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{14, 20});
        uint32_t mid_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 22});
        uint32_t vic = sim.spawn_unit(1, AntType::Worker, TileCoord{18, 20});
        const std::vector<uint32_t> ids = {far_ant, near_ant, mid_ant};
        const uint32_t ack = sim.issue_group_attack_order(ids, TileCoord{18, 20});
        ASSERT_EQ(ack, near_ant);                                       // 16 x Chebyshev: near 4, mid 6, far 8
        for (uint32_t id : ids) {
            ASSERT_EQ(sim.get_unit(id).orig_order, AntUnit::kOrderAttack);
            ASSERT_EQ(sim.get_unit(id).orig_order_tile, (TileCoord{18, 20}));
            ASSERT_EQ(sim.get_unit(id).orig_target_ant, vic);
        }
        const uint32_t serial_far = sim.get_unit(far_ant).move_serial;
        run_ms(sim, 200);
        ASSERT_EQ(sim.issue_group_attack_order(ids, TileCoord{18, 20}), 0u);   // all skipped: no order, no voice
        ASSERT_EQ(sim.get_unit(far_ant).move_serial, serial_far);
        // An ant that is not attacking that tile is not skipped: it gets the order and can acknowledge
        uint32_t idle_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 18});
        ASSERT_EQ(sim.issue_group_attack_order({idle_ant}, TileCoord{18, 20}), idle_ant);
        // Move orders to the same tile are NOT skipped by an attack click (only order 3 is)
        uint32_t mover = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 26});
        sim.issue_move_order(mover, TileCoord{18, 26});
        ASSERT_EQ(sim.get_unit(mover).orig_order, AntUnit::kOrderMove);
        ASSERT_EQ(sim.issue_group_attack_order({mover}, TileCoord{18, 26}), mover);
    } TEST_END();

    TEST_CASE("12.144 A Click On An Enemy Ant Attacks It With The Selected Ants (Cursor Mode 5) And Moves When Nothing Is There") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t att = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 20});
        uint32_t vic = sim.spawn_unit(1, AntType::Worker, TileCoord{15, 20});
        sim.tick();
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim);
        hud.select_ant(att, false);
        ViewportCamera camera;
        camera.world_x = 400;
        camera.world_y = 500;
        // the victim's body (12 px above its anchor) is under the pointer: the cursor is the attack cursor
        const int32_t vx = 15 * 32 + 16 - camera.world_x + HUD::PLAYFIELD_X;
        const int32_t vy = 20 * 32 + 16 - 12 - camera.world_y + HUD::PLAYFIELD_Y;
        ASSERT_EQ(hud.evaluate_cursor(vx, vy, sim.get_world_state(), sim.grid(), camera), CursorType::Attack);
        hud.handle_mouse_down(vx, vy, SDL_BUTTON_LEFT, sim, camera);
        hud.handle_mouse_up(vx, vy, SDL_BUTTON_LEFT, sim, camera);
        ASSERT_EQ(sim.get_unit(att).orig_order, AntUnit::kOrderAttack);
        ASSERT_EQ(sim.get_unit(att).orig_target_ant, vic);
        ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return sim.get_unit(vic).hp < 10; }) >= 0);
        // click on empty ground: an ordinary move
        SimulationEngine sim2;
        sim2.init_test_world(60, 60, 100, 60000);
        uint32_t att2 = sim2.spawn_unit(0, AntType::Combat, TileCoord{10, 20});
        sim2.tick();
        HUD hud2;
        hud2.init(0);
        hud2.set_sim_query(&sim2);
        hud2.select_ant(att2, false);
        const int32_t gx = 20 * 32 + 16 - camera.world_x + HUD::PLAYFIELD_X;
        const int32_t gy = 20 * 32 + 16 - camera.world_y + HUD::PLAYFIELD_Y;
        hud2.handle_mouse_down(gx, gy, SDL_BUTTON_LEFT, sim2, camera);
        hud2.handle_mouse_up(gx, gy, SDL_BUTTON_LEFT, sim2, camera);
        ASSERT_EQ(sim2.get_unit(att2).orig_order, AntUnit::kOrderMove);
    } TEST_END();

    TEST_CASE("12.145 Ant Pick Rectangles Of The Original (FUN_01026a39): Worker [x-20, x+20) x [y-32, y+16), Combat [x-32, x+26) x [y-46, y+16), Last Hit Wins") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t w = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t c = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 10});
        sim.tick();
        HUD hud;
        hud.init(0);
        const auto& world = sim.get_world_state();
        auto pick = [&](int32_t x, int32_t y) -> uint32_t {
            const AntSnapshot* a = hud.pick_ant_at(world, x, y);
            return a ? a->id : 0u;
        };
        const int32_t wx = 10 * 32 + 16, wy = 10 * 32 + 16;
        ASSERT_EQ(pick(wx - 20, wy - 32), w);           // top-left corner: inside
        ASSERT_EQ(pick(wx + 19, wy + 15), w);           // bottom-right corner: inside
        ASSERT_EQ(pick(wx - 21, wy), 0u);
        ASSERT_EQ(pick(wx + 20, wy), 0u);               // right edge is exclusive
        ASSERT_EQ(pick(wx, wy - 33), 0u);
        ASSERT_EQ(pick(wx, wy + 16), 0u);               // bottom edge is exclusive
        const int32_t cx = 20 * 32 + 16, cy = 10 * 32 + 16;
        ASSERT_EQ(pick(cx - 32, cy - 46), c);
        ASSERT_EQ(pick(cx + 25, cy + 15), c);
        ASSERT_EQ(pick(cx - 33, cy), 0u);
        ASSERT_EQ(pick(cx + 26, cy), 0u);
        ASSERT_EQ(pick(cx, cy - 47), 0u);
        // a click on the head of a combat ant (37 px above the anchor) still hits it: 21.6 % of its pixels lay outside the old box
        ASSERT_EQ(pick(cx, cy - 40), c);
        // two ants one tile apart: the click inside both boxes goes to the last one scanned (the lower ant, drawn in front)
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 11});
        sim.tick();
        const auto& world2 = sim.get_world_state();
        const AntSnapshot* both = hud.pick_ant_at(world2, wx, wy + 8 - 32 + 26);   // y = 346: inside both boxes ([304,352) and [336,384))
        ASSERT_TRUE(both != nullptr);
        ASSERT_EQ(both->id, w2);
    } TEST_END();

    TEST_CASE("12.146 The ants that a level starts with never face North: SpawnAnt draws the direction as rand() % 7 + 1 (0x100ef18)") {
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_lvl(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/GAUNTLET.LVL"));
        size_t ants_seen = 0;
        for (uint32_t seed = 1; seed <= 20; ++seed) {
            SimulationEngine sim;
            sim.init(lvl, seed);
            for (const auto& a : sim.get_world_state().ants) {
                ++ants_seen;
                ASSERT_TRUE(a.facing != 0);                                          // Direction::North
            }
        }
        ASSERT_TRUE(ants_seen >= 20 * 4);                                            // every seed starts with at least one ant per team
    } TEST_END();
}


// ============================================================================
// Master Test Runner Main
// ============================================================================
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "=======================================================\n"
              << " ANTS REMAKE — HEADLESS INTEGRATION HARNESS\n"
              << "=======================================================\n";

    run_suite_1_surface_compositing();
    run_suite_2_camera_transforms();
    run_suite_3_hud_and_radar();
    run_suite_4_audio_mixer();
    run_suite_5_midi_player();
    run_suite_6_scorecard_and_audio_routing();
    run_suite_7_input_controls();
    run_suite_8_unit_health_and_map_select();
    run_suite_9_gameplay_mechanics_and_options();
    run_suite_10_egg_economy_incubation_teamup_abilities();
    run_suite_11_anthill_queuing_and_priority();
    run_suite_12_unit_selection_and_occupied_tile_movement();

    std::cout << "\n=======================================================\n"
              << " INTEGRATION TEST SUMMARY\n"
              << "=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Passed:           " << (g_test_count - g_test_failures) << "\n"
              << " Failed:           " << g_test_failures << "\n"
              << "=======================================================\n";

    return (g_test_failures == 0) ? 0 : 1;
}
