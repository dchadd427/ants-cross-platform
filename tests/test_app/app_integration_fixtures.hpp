// The fixtures of the application integration tests (test_app_integration.cpp holds main(); the suites are in the test_app_integration_*.cpp files beside it).
// Inline or in an anonymous namespace.
#pragma once
#include <exception>
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
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
#include "ants_app/local_teams.hpp"
#include "ants_app/version.hpp"
#include "ants_test_paths.hpp"
#include "../common/test_assert.hpp"

using namespace ants::app;
using namespace ants::sim;
using namespace ants::assets;

// ============================================================================
// Test Harness Assertions & Runner
// ============================================================================

inline int g_test_count = 0;
inline int g_test_failures = 0;
inline int g_assert_count = 0;

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



#undef ASSERT_NEAR
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(std::abs((a) - (b)) <= (eps))

// Original 1998 movement (Ants.exe): an order only snaps and idles the ant and queues a path request; the
// path manager (PATHMGR) delivers one path per 50 ms tick per team, and walking starts after that.
// Ticks until none of the given ants has a queued request (at most max_ticks ticks).
inline void tick_until_paths_delivered(SimulationEngine& sim, std::initializer_list<uint32_t> ids, int max_ticks = 20) {
    for (int t = 0; t < max_ticks; ++t) {
        bool pending = false;
        for (uint32_t id : ids) pending = pending || sim.has_pending_path(id);
        if (!pending) return;
        sim.tick();
    }
}

// Ticks until pred() holds (checked before every tick); returns whether it held within max_ticks ticks.
inline bool tick_until(SimulationEngine& sim, const std::function<bool()>& pred, int max_ticks) {
    for (int t = 0; t < max_ticks; ++t) {
        if (pred()) return true;
        sim.tick();
    }
    return pred();
}

// Runs the simulation for `ms` milliseconds (50 ms ticks).
inline void run_ms(SimulationEngine& sim, int ms) {
    for (int t = 0; t < ms / 50; ++t) sim.tick();
}

// The match ends at the first run of the CHECKGO task (every 200 ms) that finds the clock below 0 (Ants.exe 0x1024839), 0 - 200 ms
// after the clock shows 0:00. Ticks until the match is over (at most max_ticks); returns the ticks used, -1 when it never ended.
inline int run_until_over(SimulationEngine& sim, int max_ticks = 12) {
    for (int i = 0; i < max_ticks; ++i) {
        if (sim.is_match_over()) return i;
        sim.tick();
    }
    return sim.is_match_over() ? max_ticks : -1;
}

// Ticks until pred() holds (checked before every tick); returns the elapsed milliseconds, -1 if it did not hold within max_ms.
inline int wait_ms(SimulationEngine& sim, int max_ms, const std::function<bool()>& pred) {
    for (int t = 0; t <= max_ms / 50; ++t) {
        if (pred()) return t * 50;
        sim.tick();
    }
    return -1;
}

// The ant is playing the getpow clip (action 4): it has just taken a power-up.
inline bool picking_up(const AntUnit& a) { return a.loco_action == AntUnit::kActionGetPow; }

// An ant that stands on a power-up takes it by a player order onto its own tile (a one-tile path that ends on the tile);
// the original has no idle pick-up. Returns once the getpow clip plays; run_ms(sim, 900) lets it finish.
inline bool take_powerup_here(SimulationEngine& sim, uint32_t ant_id) {
    sim.issue_move_order(ant_id, sim.get_unit(ant_id).pos);
    return wait_ms(sim, 2000, [&]() { return picking_up(sim.get_unit(ant_id)); }) >= 0;
}

// A food object as a Block-2 entry of an LVL describes it (Ants.exe FUN_01008ca0): `units` units of `value` points each, and the
// stage list {threshold, tile} (the pile shows the last stage whose threshold is >= the units left; {0, 0x7FFE} = gone).
// The stage tiles come from the archive's food tiles (crackers 369..372 cover 2x2 cells around their anchor). Returns the index.
inline int32_t place_pile(SimulationEngine& sim, int32_t col, int32_t row, uint16_t units, uint16_t value,
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
inline void make_water_island(SimulationEngine& sim, int32_t x, int32_t y) {
    for (int32_t iy = y - 1; iy <= y + 1; ++iy) {
        for (int32_t ix = x - 1; ix <= x + 1; ++ix) {
            if (ix != x || iy != y) sim.grid_mut().get_cell_mut(static_cast<uint32_t>(ix), static_cast<uint32_t>(iy)).terrain_type = TERRAIN_WATER;
        }
    }
}

// The ant crosses into the power-up's tile and gets the can't-go order while its walk has not landed yet: the pick-up is
// cancelled and the ant stands on the power-up (Ants.exe: any accepted order snaps the ant onto the centre of its tile).
inline bool stand_on_powerup(SimulationEngine& sim, uint32_t ant_id, TileCoord hat, TileCoord island) {
    sim.issue_move_order(ant_id, hat);
    if (wait_ms(sim, 4000, [&]() { return sim.get_unit(ant_id).pos == hat; }) < 0) return false;
    sim.issue_move_order(ant_id, island);
    run_ms(sim, 3000);
    const AntUnit& u = sim.get_unit(ant_id);
    return u.pos == hat && u.loco_action == AntUnit::kActionIdle && sim.grid().has_powerup_at(hat);
}

// A name in the temporary folder that belongs to this run of the suite alone: two runs at the same time share the folder (the process id tells them apart)
inline std::filesystem::path temp_path_of_this_run(const std::string& name) {
#if defined(_WIN32)
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(getpid());
#endif
    return std::filesystem::temp_directory_path() / (name + "_" + std::to_string(pid));
}

// TINY.LVL with its first start marker of a team (tile 154 green, 155 red, 153 blue, 152 black) moved to row 21512, written to a temporary file (a byte edit of a shipped map,
// as the loader tests make their synthetic maps): the file loads, a match with that team is not playable. Empty path when the shipped map cannot be read.
inline std::filesystem::path write_marker_outside_grid_map(uint16_t tile = 154) {
    std::ifstream in("Original-Ants/Maps/TINY.LVL", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 19000) return {};
    const size_t names = static_cast<size_t>(bytes[40] | (bytes[41] << 8)) + 1;
    const size_t dims = 42 + names * 11;
    const size_t cells = static_cast<size_t>(bytes[dims] | (bytes[dims + 1] << 8)) * static_cast<size_t>(bytes[dims + 4] | (bytes[dims + 5] << 8));
    const size_t records = dims + 8 + cells * 12 + 2;                     // block 1: a word count, then (tile, row, column) words
    const size_t count = static_cast<size_t>(bytes[records - 2] | (bytes[records - 1] << 8));
    size_t marker = 0;
    for (size_t i = 0; i < count && marker == 0; ++i) {
        const size_t at = records + i * 6;
        if ((bytes[at] | (bytes[at + 1] << 8)) == tile) marker = at;
    }
    if (marker == 0) return {};
    bytes[marker + 2] = static_cast<uint8_t>(21512 & 0xFF);
    bytes[marker + 3] = static_cast<uint8_t>(21512 >> 8);
    const std::filesystem::path path = temp_path_of_this_run("ants_unplayable_marker_" + std::to_string(tile)).replace_extension(".lvl");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good() ? path : std::filesystem::path{};
}

// Milliseconds from an ant's first path delivery to its last pixel move (the landing on the goal centre),
// read from the locomotion trace (SimulationEngine::set_locomotion_trace_enabled); -1 without a path.
inline int32_t walk_duration_ms(const SimulationEngine& sim, uint32_t ant_id) {
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

// The suites, run by main() in test_app_integration.cpp in this order
void run_suite_1_surface_compositing();
void run_suite_2_camera_transforms();
void run_suite_3_hud_and_radar();
void run_suite_4_audio_mixer();
void run_suite_5_midi_player();
void run_suite_6_scorecard_and_audio_routing();
void run_suite_7_input_controls();
void run_suite_8_unit_health_and_map_select();
void run_suite_9_gameplay_mechanics_and_options();
void run_suite_10_egg_economy_incubation_teamup_abilities();
void run_suite_11_anthill_queuing_and_priority();
void run_suite_12_unit_selection_and_occupied_tile_movement();
void run_suite_12_unit_selection_and_occupied_tile_movement_2();
void run_suite_12_unit_selection_and_occupied_tile_movement_3();
void run_suite_12_unit_selection_and_occupied_tile_movement_4();
void run_suite_13_extended_play();
