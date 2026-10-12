// The fixtures of the zoom view tests (test_zoom_view.cpp holds main(); the cases are in test_zoom_view_renderer.cpp, test_zoom_view_hud.cpp and test_zoom_view_app.cpp). Inline or in an anonymous namespace.
#pragma once

#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "ants_ai/bot.hpp"
#include "zoom_scene.hpp"
#include "ants_app/application.hpp"
#include "ants_app/config_store.hpp"
#include "ants_app/edge_scroll.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/setup_layout.hpp"
#include "ants_app/view_zoom.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;
using namespace zoomtest;

inline int g_checks = 0;
inline int g_failures = 0;
inline const char* g_group = "";

namespace {

inline void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        if (g_failures <= 80) std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

inline void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

[[maybe_unused]] std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

}  // namespace

// The tests, run by main() in test_zoom_view.cpp
void test_world_pass(const assets::AssetArchive& arc);
void test_zoom_out_pass(const assets::AssetArchive& arc);
void test_level_pictures(const assets::AssetArchive& arc);
void test_lattice_placement(const assets::AssetArchive& arc);
void test_fog_levels(const assets::AssetArchive& arc);
void test_click_centre(const assets::AssetArchive& arc);
void test_pass_state(const assets::AssetArchive& arc);
void test_hud_cursor(const assets::AssetArchive& arc);
void test_hud_orders(const assets::AssetArchive& arc);
void test_hud_rubber_band(const assets::AssetArchive& arc);
void test_hud_minimap_and_scroll(const assets::AssetArchive& arc);
void test_hud_radar_frame(const assets::AssetArchive& arc);
void test_hud_gating(const assets::AssetArchive& arc);
void test_hud_ctrl_n(const assets::AssetArchive& arc);
void test_app_wheel();
void test_app_middle_button();
void test_app_zoom_api();
void test_app_ants();
void test_app_fairness_local();
void test_app_setup_screen();
void test_app_settings();
void test_app_start_view_and_listener();
void test_local_determinism(const assets::AssetArchive&);
void test_network_match();
void test_app_guards();
void test_app_wheel_garbage();
