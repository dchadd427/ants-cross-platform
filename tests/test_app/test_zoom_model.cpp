// The zoom of the map view, the model (milestone M4 of the widescreen work): include/ants_app/view_zoom.hpp, the camera that carries the zoom (ViewportCamera in renderer.hpp) and the
// edge scroll's functions in screen pixels (edge_scroll.hpp). No window, no renderer: pure numbers, with every expectation written out here independently of the header (in whole
// numbers: a world coordinate is held in half pixels, so that the arithmetic of the zoom 2 is exact).
//   * the levels, the world that a view shows, the grid of the camera's origin, the conversions between the screen and the world;
//   * which levels a view over a map offers (the six shipped maps, a 16 x 16 map, a 12 x 12 one and a narrow one, the wide and the classic view), the next level in a direction, the
//     level a match starts with, the fairness limits (no zoom-out in a match of the network);
//   * the camera after a zoom that keeps the world point under the pointer: at hundreds of pointer positions, from cameras all over every map, with the clamps (the origin on its
//     grid, inside the map, a small map centred);
//   * the camera itself: its conversions, its clamp, center_on, the scroll in screen pixels;
//   * the edge scroll in screen pixels: the same distance on the screen at every zoom, at every pixel of every strip; the minimap centres the world that is seen; the start view of
//     every map keeps the hill in view at every zoom;
//   * the wheel: the notches and the precise deltas, the natural-scrolling flip, the accumulation, the pauses and the reversals;
//   * the settings key.
// Usage: test_zoom_model. Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ants_app/edge_scroll.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/view_zoom.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        if (g_failures <= 60) std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

uint32_t g_seed = 1;
uint32_t lcg() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The oracles (whole numbers; a world coordinate in half pixels where it can be one)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// the world pixels that len screen pixels show (rounded up)
int32_t oracle_visible(int32_t len, float z) {
    if (z == 0.5f) return 2 * len;
    if (z == 2.0f) return (len + 1) / 2;
    return len;
}
/// half world pixels per screen pixel: 4 at 0.5, 2 at 1, 1 at 2 (this is also the grid of the origin in half pixels)
int oracle_half_per_pixel(float z) { return z == 0.5f ? 4 : z == 1.0f ? 2 : 1; }
/// floor division
int64_t fdiv(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
/// the world pixel under the screen pixel at `offset`, the origin given in HALF world pixels
int32_t oracle_world_at(int64_t origin2, float z, int32_t offset) { return static_cast<int32_t>(fdiv(origin2 + static_cast<int64_t>(offset) * oracle_half_per_pixel(z), 2)); }
/// a world point in half pixels under the screen pixel at `offset` (the pixel's own corner)
int64_t oracle_point2(int64_t origin2, float z, int32_t offset) { return origin2 + static_cast<int64_t>(offset) * oracle_half_per_pixel(z); }
/// the origin rounded (half up) to its grid
double oracle_snap(double v, float z) {
    const double g = 1.0 / z;
    return std::floor(v / g + 0.5) * g;
}

struct MapSize {
    std::string name;
    int32_t w;      // world pixels
    int32_t h;
};

std::vector<MapSize> shipped_maps() {
    std::vector<MapSize> maps;
    for (const auto& entry : std::filesystem::directory_iterator(std::string(ORIGINAL_ASSETS_DIR) + "/Maps")) {
        if (entry.path().extension() != ".LVL") continue;
        assets::LevelData level;
        if (!level.load_from_file(entry.path().string())) continue;
        maps.push_back(MapSize{entry.path().stem().string(), static_cast<int32_t>(level.width()) * 32, static_cast<int32_t>(level.height()) * 32});
    }
    std::sort(maps.begin(), maps.end(), [](const MapSize& a, const MapSize& b) { return a.name < b.name; });
    return maps;
}

std::vector<MapSize> all_maps() {
    std::vector<MapSize> maps = shipped_maps();
    maps.push_back(MapSize{"16x16", 512, 512});
    maps.push_back(MapSize{"12x12", 384, 384});
    maps.push_back(MapSize{"15x31", 480, 992});
    return maps;
}

struct ViewSize {
    const char* name;
    int32_t w;
    int32_t h;
};
const ViewSize kViews[2] = {{"classic", 442, 440}, {"wide", 762, 500}};

// =====================================================================================================================================================
// The levels and the conversions
// =====================================================================================================================================================

void test_levels() {
    group("levels", "the three levels, the world that a view shows, the grid and the conversions");
    check(zoom::kLevelCount == 3 && zoom::kLevels[0] == 0.5f && zoom::kLevels[1] == 1.0f && zoom::kLevels[2] == 2.0f, "the levels are 0.5, 1 and 2, from the most of the world to the least");
    check(zoom::kOut == 0.5f && zoom::kNormal == 1.0f && zoom::kIn == 2.0f, "the names of the levels");
    for (size_t i = 0; i < zoom::kLevelCount; ++i) check(zoom::level_index(zoom::kLevels[i]) == static_cast<int>(i) && zoom::is_level(zoom::kLevels[i]), "level " + std::to_string(i) + " is found at its index");
    for (float bad : {0.0f, 0.25f, 0.75f, 1.5f, 3.0f, -1.0f, 1.0001f, 0.4999f}) check(zoom::level_index(bad) == -1 && !zoom::is_level(bad), "no level at " + num(bad));

    for (const ViewSize& v : kViews) {
        for (float z : zoom::kLevels) {
            check(zoom::visible(v.w, z) == oracle_visible(v.w, z) && zoom::visible(v.h, z) == oracle_visible(v.h, z), std::string("the world that the ") + v.name + " view shows at " + zoom::level_name(z));
        }
    }
    check(zoom::visible(442, 2.0f) == 221 && zoom::visible(440, 2.0f) == 220 && zoom::visible(442, 0.5f) == 884 && zoom::visible(440, 0.5f) == 880, "442 x 440 shows 221 x 220 at 2 and 884 x 880 at 0.5");
    check(zoom::visible(762, 2.0f) == 381 && zoom::visible(500, 2.0f) == 250 && zoom::visible(762, 0.5f) == 1524 && zoom::visible(500, 0.5f) == 1000, "762 x 500 shows 381 x 250 at 2 and 1524 x 1000 at 0.5");
    check(zoom::visible(443, 2.0f) == 222 && zoom::visible(1, 2.0f) == 1 && zoom::visible(0, 2.0f) == 0 && zoom::visible(1, 0.5f) == 2, "an odd length rounds up at the zoom 2: a world pixel that is shown in part is shown");
    check(zoom::visible_exact(443, 2.0f) == 221.5 && zoom::visible_exact(762, 0.5f) == 1524.0 && zoom::visible_exact(762, 1.0f) == 762.0, "the exact world that a view shows");

    check(zoom::grid(0.5f) == 2.0 && zoom::grid(1.0f) == 1.0 && zoom::grid(2.0f) == 0.5, "the grid of the origin is one screen pixel: 2, 1 and half a world pixel");
    for (float z : zoom::kLevels) {
        for (int i = -200; i <= 200; ++i) {
            const double on_grid = i * zoom::grid(z);
            check(zoom::snap(on_grid, z) == on_grid, "a point on the grid stays: " + num(on_grid) + " at " + zoom::level_name(z));
        }
    }
    check(zoom::snap(4.25, 2.0f) == 4.5 && zoom::snap(4.24, 2.0f) == 4.0 && zoom::snap(4.5, 1.0f) == 5.0 && zoom::snap(4.49, 1.0f) == 4.0 && zoom::snap(-4.5, 1.0f) == -4.0 && zoom::snap(3.0, 0.5f) == 4.0 &&
              zoom::snap(2.99, 0.5f) == 2.0 && zoom::snap(1.0, 0.5f) == 2.0,
          "snap rounds half up to the grid: 4.25 at the zoom 2 is 4.5, 4.5 at 1 is 5, 3 at 0.5 is 4");
    check(zoom::snap_toward_zero(-125.9, 1.0f) == -125.0 && zoom::snap_toward_zero(125.9, 1.0f) == 125.0 && zoom::snap_toward_zero(-14.7, 2.0f) == -14.5 && zoom::snap_toward_zero(-505.0, 0.5f) == -504.0 &&
              zoom::snap_toward_zero(-0.9, 1.0f) == 0.0,
          "snap_toward_zero truncates to the grid, as the original's integer division does");

    // the world pixel under a screen pixel
    for (float z : zoom::kLevels) {
        const int step2 = oracle_half_per_pixel(z);                                  // the origin's grid in half pixels: 4, 2, 1
        for (int o2 = -40; o2 <= 40; o2 += step2) {
            const double origin = o2 / 2.0;
            for (int32_t d = -3; d <= 1600; d += (d < 12 ? 1 : 37)) {
                check(zoom::world_at(origin, z, d) == oracle_world_at(o2, z, d), "world_at origin " + num(origin) + " zoom " + zoom::level_name(z) + " offset " + std::to_string(d));
                const int64_t edge2 = oracle_point2(o2, z, d);
                check(zoom::world_edge_up(origin, z, d) == static_cast<int32_t>(fdiv(edge2 + 1, 2)), "world_edge_up origin " + num(origin) + " zoom " + zoom::level_name(z) + " offset " + std::to_string(d));
            }
        }
    }
    check(zoom::world_at(100.0, 1.0f, 17) == 117 && zoom::world_at(100.0, 2.0f, 17) == 108 && zoom::world_at(100.5, 2.0f, 17) == 109 && zoom::world_at(100.0, 0.5f, 17) == 134,
          "worked examples: at 1 the pixel 17 over is 117, at 2 it is 108, from 100.5 it is 109, at 0.5 it is 134");
    check(zoom::world_edge_up(100.0, 2.0f, 17) == 109 && zoom::world_edge_up(100.0, 1.0f, 17) == 117 && zoom::world_edge_up(100.0, 2.0f, 16) == 108, "the edge of a screen pixel rounds up at the zoom 2");
}

// =====================================================================================================================================================
// Which levels are offered
// =====================================================================================================================================================

void test_offered_levels() {
    group("offered", "which levels a view over a map offers, the next level in a direction, the level a match starts with, the fairness limits");
    const zoom::Limits any = zoom::Limits::any();
    const zoom::Limits none = zoom::Limits::no_zoom_out();
    check(any.min_zoom == 0.5f && any.max_zoom == 2.0f && none.min_zoom == 1.0f && none.max_zoom == 2.0f && any != none && any == zoom::Limits{} && none == zoom::Limits::no_zoom_out(),
          "the limits: a local game may go 0.5 .. 2, a match of the network 1 .. 2");

    // limits other than the two of the game: a view that may not zoom in, one that may do neither
    {
        const zoom::Limits no_in{0.5f, 1.0f};
        const zoom::Limits none_at_all{1.0f, 1.0f};
        check(!zoom::offered(2.0f, no_in, 762, 500, 1920, 1920) && zoom::offered(1.0f, no_in, 762, 500, 1920, 1920) && zoom::offered(0.5f, no_in, 762, 500, 1920, 1920), "limits 0.5 .. 1: no zoom in");
        check(!zoom::offered(2.0f, none_at_all, 762, 500, 1920, 1920) && !zoom::offered(0.5f, none_at_all, 762, 500, 1920, 1920) && zoom::offered(1.0f, none_at_all, 762, 500, 1920, 1920), "limits 1 .. 1: the level 1 only");
        // the level 1 is the original's own picture: offered under any limits, also limits that would exclude it
        check(zoom::offered(1.0f, zoom::Limits{2.0f, 2.0f}, 762, 500, 1920, 1920) && zoom::offered(1.0f, zoom::Limits{0.5f, 0.5f}, 762, 500, 1920, 1920) && zoom::level_for_match(1.0f, zoom::Limits{2.0f, 2.0f}, 762, 500, 1920, 1920) == 1.0f,
              "the level 1 is offered under every limits");
        check(zoom::step(1.0f, +1, no_in, 762, 500, 1920, 1920) == 1.0f && zoom::step(1.0f, -1, none_at_all, 762, 500, 1920, 1920) == 1.0f && zoom::step(1.0f, +1, none_at_all, 762, 500, 1920, 1920) == 1.0f, "step stays at 1 where nothing else is allowed");
        check(zoom::level_for_match(2.0f, no_in, 762, 500, 1920, 1920) == 1.0f && zoom::level_for_match(0.5f, none_at_all, 762, 500, 1920, 1920) == 1.0f, "a match starts at 1 when the remembered level is beyond the limits");
    }
    const std::vector<MapSize> maps = all_maps();
    check(shipped_maps().size() == 6, "there are six shipped maps");
    for (const MapSize& m : maps) {
        for (const ViewSize& v : kViews) {
            const std::string at = m.name + " (" + std::to_string(m.w) + " x " + std::to_string(m.h) + ") in the " + v.name + " view: ";
            // the level 1 is always offered; the level 2 when the limits allow it; the level 0.5 when they allow it AND the map does not already fit the view at 1
            const bool fits_at_one = m.w <= v.w && m.h <= v.h;
            for (const zoom::Limits& lim : {any, none}) {
                const std::string kind = lim == any ? "local: " : "network: ";
                check(zoom::offered(1.0f, lim, v.w, v.h, m.w, m.h), at + kind + "level 1 is always offered");
                check(zoom::offered(2.0f, lim, v.w, v.h, m.w, m.h), at + kind + "level 2 is offered");
                const bool want_out = lim.min_zoom <= 0.5f && !fits_at_one;
                check(zoom::offered(0.5f, lim, v.w, v.h, m.w, m.h) == want_out, at + kind + "level 0.5 is " + (want_out ? "offered" : "not offered"));
                for (float bad : {0.0f, 0.75f, 1.5f, 4.0f}) check(!zoom::offered(bad, lim, v.w, v.h, m.w, m.h), at + "a number that is not a level is never offered");
            }
        }
    }
    // the maps that decide it: the shipped ones are all larger than both views (60 x 60 and 40 x 40 tiles, TINY 31 x 31)
    for (const MapSize& m : shipped_maps()) {
        for (const ViewSize& v : kViews) check(zoom::offered(0.5f, any, v.w, v.h, m.w, m.h), m.name + ": a shipped map is larger than the " + v.name + " view: the zoom-out is offered in a local game");
    }
    check(zoom::offered(0.5f, any, 762, 500, 512, 512), "a 16 x 16 map is 12 px taller than the wide view: the zoom-out shows the rest of it");
    check(!zoom::offered(0.5f, any, 762, 500, 480, 480), "a 15 x 15 map fits the wide view at 1: nothing more to see at 0.5");
    check(zoom::offered(0.5f, any, 442, 440, 480, 480), "... but not the classic view");
    check(!zoom::offered(0.5f, any, 762, 500, 384, 384) && !zoom::offered(0.5f, any, 442, 440, 384, 384), "a 12 x 12 map fits both views at 1");
    check(zoom::offered(0.5f, any, 762, 500, 762, 501) && !zoom::offered(0.5f, any, 762, 500, 762, 500), "a map exactly as large as the view fits it; one pixel more does not (the edge of the rule)");
    check(zoom::offered(0.5f, any, 762, 500, 763, 500) && !zoom::offered(0.5f, any, 762, 500, 761, 499), "... on either axis");

    // the next level
    for (const MapSize& m : shipped_maps()) {
        for (const ViewSize& v : kViews) {
            for (const zoom::Limits& lim : {any, none}) {
                const float low = lim.min_zoom <= 0.5f ? 0.5f : 1.0f;
                check(zoom::step(1.0f, +1, lim, v.w, v.h, m.w, m.h) == 2.0f, m.name + ": in from 1 is 2");
                check(zoom::step(2.0f, +1, lim, v.w, v.h, m.w, m.h) == 2.0f, m.name + ": there is no level above 2");
                check(zoom::step(2.0f, -1, lim, v.w, v.h, m.w, m.h) == 1.0f, m.name + ": out from 2 is 1");
                check(zoom::step(1.0f, -1, lim, v.w, v.h, m.w, m.h) == low, m.name + ": out from 1 is 0.5 in a local game and 1 (nothing) in a match of the network");
                check(zoom::step(0.5f, -1, lim, v.w, v.h, m.w, m.h) == 0.5f, m.name + ": there is no level below 0.5");
                check(zoom::step(0.5f, +1, lim, v.w, v.h, m.w, m.h) == 1.0f, m.name + ": in from 0.5 is 1");
                check(zoom::step(1.0f, 0, lim, v.w, v.h, m.w, m.h) == 1.0f && zoom::step(2.0f, 0, lim, v.w, v.h, m.w, m.h) == 2.0f, m.name + ": no direction, no change");
                check(zoom::step(1.0f, +100, lim, v.w, v.h, m.w, m.h) == 2.0f && zoom::step(1.0f, -100, lim, v.w, v.h, m.w, m.h) == low, m.name + ": the size of the direction does not matter");
                check(zoom::step(0.7f, +1, lim, v.w, v.h, m.w, m.h) == 2.0f && zoom::step(0.7f, -1, lim, v.w, v.h, m.w, m.h) == low, m.name + ": a current level that is not one counts as 1");
            }
        }
    }
    // a map that fits skips the zoom-out: from 1 there is nothing below
    check(zoom::step(1.0f, -1, any, 762, 500, 384, 384) == 1.0f && zoom::step(2.0f, -1, any, 762, 500, 384, 384) == 1.0f, "a 12 x 12 map in the wide view: out stays at 1");
    check(zoom::step(0.5f, +1, any, 762, 500, 384, 384) == 1.0f, "... and a level that was left over from another map goes in to 1");

    // the level a match starts with
    for (const MapSize& m : maps) {
        for (const ViewSize& v : kViews) {
            for (const zoom::Limits& lim : {any, none}) {
                for (float remembered : {0.5f, 1.0f, 2.0f}) {
                    const float got = zoom::level_for_match(remembered, lim, v.w, v.h, m.w, m.h);
                    const float want = zoom::offered(remembered, lim, v.w, v.h, m.w, m.h) ? remembered : 1.0f;
                    check(got == want, m.name + ": the match starts at the remembered level when it is offered, else at 1 (" + zoom::level_name(remembered) + " -> " + zoom::level_name(got) + ")");
                }
                check(zoom::level_for_match(0.75f, lim, v.w, v.h, m.w, m.h) == 1.0f && zoom::level_for_match(0.0f, lim, v.w, v.h, m.w, m.h) == 1.0f, m.name + ": a remembered number that is no level starts at 1");
            }
        }
    }
    check(zoom::level_for_match(0.5f, any, 762, 500, 1920, 1920) == 0.5f && zoom::level_for_match(0.5f, none, 762, 500, 1920, 1920) == 1.0f && zoom::level_for_match(2.0f, none, 762, 500, 1920, 1920) == 2.0f,
          "a network match never starts zoomed out and may start zoomed in");
}

// =====================================================================================================================================================
// The camera after a zoom
// =====================================================================================================================================================

/// the clamp of the origin on an axis, written out in half pixels: the range [0, map - visible], a map that is not larger than what the view shows centred (truncated toward zero on the
/// grid) or at 0
int64_t oracle_clamp2(int64_t v2, float z, int32_t view_len, int32_t map_px, bool centre_small) {
    const int64_t hpp = oracle_half_per_pixel(z);
    const int64_t vis2 = static_cast<int64_t>(view_len) * hpp;                                       // 2 * (view_len / z) half pixels
    const int64_t map2 = static_cast<int64_t>(map_px) * 2;
    if (map2 > vis2) return std::clamp(v2, int64_t{0}, map2 - vis2);
    if (!centre_small) return 0;
    const int64_t c = (map2 - vis2) / 2;                                                           // toward zero
    return (c / hpp) * hpp;                                                                        // (the grid in half pixels is hpp)
}

void test_anchored_zoom() {
    group("anchor", "the camera after a zoom keeps the world point under the pointer, on its grid, inside the map");
    // a camera far from every edge of a big map. The origin lies on the grid of ONE SCREEN PIXEL of the new zoom, so the world point under the pointer stays where it is exactly when that is
    // a point of the grid, and within half a screen pixel otherwise (the error is the rounding of the origin to the grid): 1 -> 2 is always exact (a half-pixel grid holds every point),
    // 1 -> 0.5 is exact at an even offset (the grid is two world pixels) and 1 world pixel off at an odd one, 2 -> 1 from a half-pixel origin is half a world pixel off at an even offset
    constexpr int32_t kBig = 4000;
    for (const ViewSize& v : kViews) {
        int sampled = 0;
        for (int32_t ay = 0; ay < v.h; ay += 7) {
            for (int32_t ax = 0; ax < v.w; ax += 9) {
                const zoom::Camera one{1500.0, 1300.0, 1.0f};
                const zoom::Camera two = zoom::zoomed(one, 2.0f, ax, ay, v.w, v.h, kBig, kBig, true);
                const zoom::Camera half = zoom::zoomed(one, 0.5f, ax, ay, v.w, v.h, kBig, kBig, true);
                const int64_t before_x2 = oracle_point2(3000, 1.0f, ax);
                const int64_t before_y2 = oracle_point2(2600, 1.0f, ay);
                const std::string at = std::string(v.name) + " at (" + std::to_string(ax) + ", " + std::to_string(ay) + "): ";
                check(two.zoom == 2.0f && oracle_point2(static_cast<int64_t>(two.x * 2.0), 2.0f, ax) == before_x2 && oracle_point2(static_cast<int64_t>(two.y * 2.0), 2.0f, ay) == before_y2,
                      at + "1 -> 2 keeps the world point exactly");
                const int64_t half_err_x = std::llabs(oracle_point2(static_cast<int64_t>(half.x * 2.0), 0.5f, ax) - before_x2);
                const int64_t half_err_y = std::llabs(oracle_point2(static_cast<int64_t>(half.y * 2.0), 0.5f, ay) - before_y2);
                check(half.zoom == 0.5f && half_err_x == (ax % 2 == 0 ? 0 : 2) && half_err_y == (ay % 2 == 0 ? 0 : 2), at + "1 -> 0.5 keeps the world point exactly at an even offset, one world pixel off at an odd one: " + std::to_string(half_err_x) + ", " + std::to_string(half_err_y) + " half pixels");
                // back: the same anchor returns the origin (1 -> 2 -> 1 exactly; 1 -> 0.5 -> 1 exactly at an even offset, within a pixel otherwise)
                const zoom::Camera back2 = zoom::zoomed(two, 1.0f, ax, ay, v.w, v.h, kBig, kBig, true);
                const zoom::Camera back_half = zoom::zoomed(half, 1.0f, ax, ay, v.w, v.h, kBig, kBig, true);
                check(back2.x == one.x && back2.y == one.y && back2.zoom == 1.0f, at + "in and out at the same pixel returns the camera exactly");
                check(std::fabs(back_half.x - one.x) <= (ax % 2 == 0 ? 0.0 : 1.0) && std::fabs(back_half.y - one.y) <= (ay % 2 == 0 ? 0.0 : 1.0), at + "out and in returns the camera exactly at an even offset and within a pixel otherwise");
                // a half-pixel origin zooms out within half a world pixel of the point (exactly at an odd offset)
                const zoom::Camera odd{1500.5, 1300.5, 2.0f};
                const zoom::Camera odd_one = zoom::zoomed(odd, 1.0f, ax, ay, v.w, v.h, kBig, kBig, true);
                const int64_t odd_err_x = std::llabs(oracle_point2(static_cast<int64_t>(odd_one.x * 2.0), 1.0f, ax) - oracle_point2(3001, 2.0f, ax));
                const int64_t odd_err_y = std::llabs(oracle_point2(static_cast<int64_t>(odd_one.y * 2.0), 1.0f, ay) - oracle_point2(2601, 2.0f, ay));
                check(odd_err_x == (ax % 2 == 0 ? 1 : 0) && odd_err_y == (ay % 2 == 0 ? 1 : 0), at + "2 -> 1 from a half-pixel origin is half a world pixel off at an even offset (the origin lands between two pixels) and exact at an odd one: " + std::to_string(odd_err_x) + ", " + std::to_string(odd_err_y));
                ++sampled;
            }
        }
        check(sampled > 3000, std::string(v.name) + ": the sweep covers the whole view");
    }
    check(zoom::zoomed(zoom::Camera{10.0, 20.0, 1.0f}, 1.0f, 100, 100, 762, 500, 1920, 1920, true).x == 10.0, "the same level changes nothing");

    // every map, cameras all over it, every pair of levels, pointer positions of a sample: the result is on its grid, inside the map or centred, whatever the clamps did
    for (const MapSize& m : all_maps()) {
        for (const ViewSize& v : kViews) {
            for (bool centre : {true, false}) {
                for (float from : zoom::kLevels) {
                    for (float to : zoom::kLevels) {
                        for (int i = 0; i < 40; ++i) {
                            const double ox = oracle_snap(-100.0 + (lcg() % 10000) / 10000.0 * (m.w + 200.0), from);
                            const double oy = oracle_snap(-100.0 + (lcg() % 10000) / 10000.0 * (m.h + 200.0), from);
                            const int32_t ax = static_cast<int32_t>(lcg() % static_cast<uint32_t>(v.w));
                            const int32_t ay = static_cast<int32_t>(lcg() % static_cast<uint32_t>(v.h));
                            const zoom::Camera r = zoom::zoomed(zoom::Camera{ox, oy, from}, to, ax, ay, v.w, v.h, m.w, m.h, centre);
                            const std::string at = m.name + " " + v.name + " " + zoom::level_name(from) + " -> " + zoom::level_name(to) + (centre ? " centred" : " corner") + " from (" + num(ox) + ", " + num(oy) + ") at (" +
                                                   std::to_string(ax) + ", " + std::to_string(ay) + "): ";
                            const int64_t x2 = static_cast<int64_t>(std::llround(r.x * 2.0));
                            const int64_t y2 = static_cast<int64_t>(std::llround(r.y * 2.0));
                            check(r.zoom == to, at + "the zoom is the new level");
                            check(x2 == oracle_clamp2(x2, to, v.w, m.w, centre) && y2 == oracle_clamp2(y2, to, v.h, m.h, centre),
                                  at + "the origin is inside the map (or centred / at 0 for a map that the view shows whole): (" + num(r.x) + ", " + num(r.y) + ")");
                            check(r.x == oracle_snap(r.x, to) && r.y == oracle_snap(r.y, to), at + "the origin is on the grid of the new zoom");
                            // never shows more than the map: the visible world is inside [0, map] whenever the map is larger than it
                            const double vis_w = v.w / static_cast<double>(to);
                            const double vis_h = v.h / static_cast<double>(to);
                            if (m.w > vis_w) check(r.x >= 0.0 && r.x + vis_w <= m.w + 1e-9, at + "the visible world does not reach beyond the map in x");
                            if (m.h > vis_h) check(r.y >= 0.0 && r.y + vis_h <= m.h + 1e-9, at + "the visible world does not reach beyond the map in y");
                        }
                    }
                }
            }
        }
    }
    // the clamps at the corners of a map: an anchored zoom-out at the top left cannot move the view left or up, at the bottom right it pins the far edge
    {
        const zoom::Camera tl = zoom::zoomed(zoom::Camera{0.0, 0.0, 1.0f}, 0.5f, 700, 450, 762, 500, 1920, 1920, true);
        check(tl.x == 0.0 && tl.y == 0.0, "a zoom-out at the top left corner of the map stays at the corner (the map has no more to the left)");
        const zoom::Camera br = zoom::zoomed(zoom::Camera{1920.0 - 762.0, 1920.0 - 500.0, 1.0f}, 0.5f, 10, 10, 762, 500, 1920, 1920, true);
        check(br.x == 1920.0 - 1524.0 && br.y == 1920.0 - 1000.0, "a zoom-out at the bottom right corner pins the far edge: the view ends where the map ends (396, 920)");
        const zoom::Camera in = zoom::zoomed(zoom::Camera{0.0, 0.0, 1.0f}, 2.0f, 0, 0, 762, 500, 1920, 1920, true);
        check(in.x == 0.0 && in.y == 0.0, "a zoom in at the top left pixel keeps the corner");
        const zoom::Camera in_br = zoom::zoomed(zoom::Camera{1158.0, 1420.0, 1.0f}, 2.0f, 761, 499, 762, 500, 1920, 1920, true);
        check(in_br.x == 1538.5 && in_br.y == 1669.5, "a zoom in at the bottom right pixel keeps the world point under it, the last pixel of the map: (1538.5, 1669.5)");
    }
    // a small map is centred at every level, a 16 x 16 map zoomed in is a map larger than the view again
    {
        const zoom::Camera out = zoom::zoomed(zoom::Camera{-125.0, 0.0, 1.0f}, 0.5f, 300, 200, 762, 500, 512, 512, true);
        check(out.x == -506.0 && out.y == -244.0, "a 16 x 16 map zoomed out is centred in the view: x = (512 - 1524) / 2, y = (512 - 1000) / 2");
        const zoom::Camera in = zoom::zoomed(zoom::Camera{-125.0, 6.0, 1.0f}, 2.0f, 300, 200, 762, 500, 512, 512, true);
        check(in.x == 25.0 && in.y == 106.0, "a 16 x 16 map zoomed in at (300, 200) keeps the point 175, 206 under it: the origin is (25, 106)");
        const zoom::Camera corner = zoom::zoomed(zoom::Camera{0.0, 0.0, 1.0f}, 0.5f, 300, 200, 512, 512, 384, 384, false);
        check(corner.x == 0.0 && corner.y == 0.0, "without centring a small map is at the corner");
    }
}

// =====================================================================================================================================================
// The camera itself
// =====================================================================================================================================================

ViewportCamera camera_of(const ViewSize& v, int32_t view_x, int32_t view_y, float z, bool centre) {
    ViewportCamera c;
    c.set_view(LayoutRect{view_x, view_y, v.w, v.h});
    c.centre_small_maps = centre;
    c.zoom = z;
    return c;
}

void test_camera() {
    group("camera", "the camera that carries the zoom: the world it shows, its clamp, center_on, its conversions, the scroll in screen pixels");
    ViewportCamera def;
    check(def.zoom == 1.0f && def.visible_w() == 442 && def.visible_h() == 440, "a camera starts at the zoom 1 and shows the classic view's 442 x 440");
    for (const ViewSize& v : kViews) {
        for (float z : zoom::kLevels) {
            ViewportCamera c = camera_of(v, 16, 21, z, true);
            check(c.visible_w() == oracle_visible(v.w, z) && c.visible_h() == oracle_visible(v.h, z), std::string("visible_w / visible_h of the ") + v.name + " view at " + zoom::level_name(z));
        }
    }

    // the clamp: the origin on its grid, inside [0, map - visible]; at the zoom 1 the camera's own arithmetic (the origin is not rounded)
    for (const MapSize& m : all_maps()) {
        for (const ViewSize& v : kViews) {
            for (float z : zoom::kLevels) {
                for (bool centre : {true, false}) {
                    for (int i = 0; i < 12; ++i) {
                        ViewportCamera c = camera_of(v, 16, 21, z, centre);
                        c.x = static_cast<float>(-300.0 + (lcg() % 100000) / 100000.0 * (m.w + 600.0));
                        c.y = static_cast<float>(-300.0 + (lcg() % 100000) / 100000.0 * (m.h + 600.0));
                        const double ix = c.x;
                        const double iy = c.y;
                        c.clamp_to_bounds(static_cast<uint32_t>(m.w / 32), static_cast<uint32_t>(m.h / 32));
                        const std::string at = m.name + " " + v.name + " zoom " + zoom::level_name(z) + (centre ? " centred" : " corner") + " from (" + num(ix) + ", " + num(iy) + "): ";
                        if (z == 1.0f) {                           // the camera's own arithmetic: held in [0, map - view] (a map smaller than the view: the old centring), not rounded
                            const double want_x = m.w > v.w ? std::clamp(ix, 0.0, static_cast<double>(m.w - v.w)) : (centre ? static_cast<double>((m.w - v.w) / 2) : 0.0);
                            const double want_y = m.h > v.h ? std::clamp(iy, 0.0, static_cast<double>(m.h - v.h)) : (centre ? static_cast<double>((m.h - v.h) / 2) : 0.0);
                            check(c.x == static_cast<float>(want_x) && c.y == static_cast<float>(want_y), at + "the zoom 1 clamp is the original's: (" + num(c.x) + ", " + num(c.y) + ")");
                        } else {
                            const int64_t x2 = static_cast<int64_t>(std::llround(c.x * 2.0));
                            const int64_t y2 = static_cast<int64_t>(std::llround(c.y * 2.0));
                            const int64_t want_x2 = oracle_clamp2(static_cast<int64_t>(std::llround(oracle_snap(ix, z) * 2.0)), z, v.w, m.w, centre);
                            const int64_t want_y2 = oracle_clamp2(static_cast<int64_t>(std::llround(oracle_snap(iy, z) * 2.0)), z, v.h, m.h, centre);
                            check(x2 == want_x2 && y2 == want_y2, at + "the origin is snapped and held: (" + num(c.x) + ", " + num(c.y) + ")");
                        }
                        check(c.world_x == static_cast<int32_t>(std::floor(c.x)) && c.world_y == static_cast<int32_t>(std::floor(c.y)), at + "world_x / world_y are the whole parts");
                    }
                }
            }
        }
    }

    // center_on puts the point in the middle of the world that the view shows
    for (const ViewSize& v : kViews) {
        for (float z : zoom::kLevels) {
            ViewportCamera c = camera_of(v, 16, 21, z, true);
            c.center_on(960, 800, 60, 60);
            const double vis_w = v.w / static_cast<double>(z);
            const double vis_h = v.h / static_cast<double>(z);
            check(std::fabs(c.x + vis_w / 2.0 - 960.0) <= zoom::grid(z) && std::fabs(c.y + vis_h / 2.0 - 800.0) <= zoom::grid(z),
                  std::string("center_on puts the point in the middle at ") + zoom::level_name(z) + " in the " + v.name + " view: origin (" + num(c.x) + ", " + num(c.y) + ")");
            ViewportCamera corner = camera_of(v, 16, 21, z, true);
            corner.center_on(10, 10, 60, 60);
            check(corner.x == 0.0f && corner.y == 0.0f, std::string("center_on near the corner is held at the corner at ") + zoom::level_name(z));
            ViewportCamera far = camera_of(v, 16, 21, z, true);
            far.center_on(1919, 1919, 60, 60);
            check(far.x == static_cast<float>(1920.0 - vis_w) && far.y == static_cast<float>(1920.0 - vis_h), std::string("center_on at the far corner is held at the far edge at ") + zoom::level_name(z));
        }
    }
    {
        ViewportCamera c = camera_of(kViews[1], 16, 21, 1.0f, true);
        c.center_on(960, 960, 60, 60);
        check(c.world_x == 960 - 381 && c.world_y == 960 - 250, "the zoom 1 centre is the old one: (579, 710)");
        c.zoom = 2.0f;
        c.center_on(960, 960, 60, 60);
        check(c.x == 960.0f - 190.5f && c.y == 960.0f - 125.0f, "at 2 the 762 x 500 view shows 381 x 250 world pixels: the origin is (769.5, 835), a half-pixel origin");
        c.zoom = 0.5f;
        c.center_on(960, 960, 60, 60);
        check(c.x == 198.0f && c.y == 460.0f, "at 0.5 it shows 1524 x 1000: the origin is (198, 460)");
    }

    // the conversions between the screen and the world
    for (const ViewSize& v : kViews) {
        for (float z : zoom::kLevels) {
            ViewportCamera c = camera_of(v, 16, 21, z, true);
            c.set_origin(700.0, 650.5, 125, 125);                                   // (the grid takes the half pixel at 2 and rounds it elsewhere)
            const int64_t ox2 = static_cast<int64_t>(std::llround(c.x * 2.0));
            const int64_t oy2 = static_cast<int64_t>(std::llround(c.y * 2.0));
            for (int32_t sy = 21; sy < 21 + v.h; sy += 11) {
                for (int32_t sx = 16; sx < 16 + v.w; sx += 13) {
                    int32_t wx = -1;
                    int32_t wy = -1;
                    check(c.screen_to_world(sx, sy, wx, wy), "a pixel of the view converts");
                    check(wx == oracle_world_at(ox2, z, sx - 16) && wy == oracle_world_at(oy2, z, sy - 21),
                          std::string("screen_to_world at ") + zoom::level_name(z) + " (" + std::to_string(sx) + ", " + std::to_string(sy) + ") is (" + std::to_string(wx) + ", " + std::to_string(wy) + ")");
                    check(c.world_x_at(sx - 16) == wx && c.world_y_at(sy - 21) == wy, std::string("world_x_at / world_y_at agree with screen_to_world at ") + zoom::level_name(z));
                    // and back: the world pixel is drawn at the screen pixel that it covers
                    int32_t bx = 0;
                    int32_t by = 0;
                    c.world_to_screen(wx, wy, bx, by);
                    if (z == 0.5f) check(bx == sx && by == sy, "world_to_screen at 0.5 gives back the screen pixel");
                    else check(sx >= bx && sx < bx + (z == 2.0f ? 2 : 1) && sy >= by && sy < by + (z == 2.0f ? 2 : 1), std::string("world_to_screen of the pixel that screen_to_world found covers the screen pixel at ") + zoom::level_name(z));
                }
            }
            int32_t wx = 0;
            int32_t wy = 0;
            check(!c.screen_to_world(15, 100, wx, wy) && !c.screen_to_world(16 + v.w, 100, wx, wy) && !c.screen_to_world(100, 20, wx, wy) && !c.screen_to_world(100, 21 + v.h, wx, wy),
                  std::string("a pixel outside the view does not convert at ") + zoom::level_name(z));
        }
    }
    {   // the edge of a screen pixel, the origin in screen pixels, the middle of the view
        ViewportCamera c = camera_of(kViews[1], 16, 21, 2.0f, true);
        c.set_origin(700.5, 650.0, 125, 125);
        check(c.world_x_edge(10) == 706 && c.world_x_at(10) == 705 && c.world_x_edge(11) == 706 && c.world_x_at(11) == 706, "at 2 from 700.5: the pixel 10 covers 705.5 .. 706, its edge is 706");
        check(c.origin_screen_x() == 1401 && c.origin_screen_y() == 1300, "the origin in screen pixels at 2 is twice the world one");
        check(c.centre_world_x() == 891 && c.centre_world_y() == 775, "the middle of the view at 2: the origin plus half of 381 x 250");
        c.zoom = 1.0f;
        c.set_origin(700.0, 650.0, 125, 125);
        check(c.origin_screen_x() == 700 && c.centre_world_x() == 700 + 381 && c.centre_world_y() == 650 + 250, "at 1 the origin in screen pixels is the world one, the middle is the old sum");
        c.zoom = 0.5f;
        c.set_origin(700.0, 650.0, 125, 125);
        check(c.origin_screen_x() == 350 && c.origin_screen_y() == 325 && c.centre_world_x() == 700 + 762, "at 0.5 the origin in screen pixels is half the world one");
    }
    {   // world_to_screen's inside test follows the zoom: a tile that hangs in by less than its own size (at the zoom) is inside
        ViewportCamera c = camera_of(kViews[0], 16, 21, 2.0f, true);
        c.set_origin(100.0, 100.0, 125, 125);
        int32_t sx = 0;
        int32_t sy = 0;
        check(c.world_to_screen(100, 100, sx, sy) && sx == 16 && sy == 21, "the origin is at the view's corner");
        check(c.world_to_screen(100 - 32, 100, sx, sy) && sx == 16 - 64, "a tile left of the view by its width (64 screen pixels at 2) is the edge of the inside");
        check(!c.world_to_screen(100 - 33, 100, sx, sy), "one pixel more is outside");
        check(c.world_to_screen(100 + 221, 100, sx, sy) && sx == 16 + 442 && !c.world_to_screen(100 + 222, 100, sx, sy), "the right edge of the view is 221 world pixels from the origin at 2");
    }

    // the scroll in screen pixels moves the same distance on the screen at every zoom
    for (const ViewSize& v : kViews) {
        for (float z : zoom::kLevels) {
            ViewportCamera c = camera_of(v, 16, 21, z, true);
            c.set_origin(700.0, 650.0, 125, 125);
            const float x0 = c.x;
            const float y0 = c.y;
            c.scroll_screen(10, -6, 125, 125);
            check(std::fabs((c.x - x0) * z - 10.0f) < 1e-4f && std::fabs((c.y - y0) * z + 6.0f) < 1e-4f,
                  std::string("scroll_screen(10, -6) moves 10 and 6 screen pixels at ") + zoom::level_name(z) + " (" + num(c.x - x0) + ", " + num(c.y - y0) + " world pixels)");
            c.scroll_screen(9000, 9000, 125, 125);
            check(c.x == static_cast<float>(4000.0 - v.w / static_cast<double>(z)) && c.y == static_cast<float>(4000.0 - v.h / static_cast<double>(z)), std::string("a scroll beyond the map stops at its far edge at ") + zoom::level_name(z));
            c.scroll_screen(-19000, -19000, 125, 125);
            check(c.x == 0.0f && c.y == 0.0f, std::string("... and at the near edge at ") + zoom::level_name(z));
            ViewportCamera w = camera_of(v, 16, 21, z, true);
            w.set_origin(700.0, 650.0, 125, 125);
            w.scroll_pixels(10, -6, 125, 125);
            check(w.x == 710.0f && w.y == 644.0f, std::string("scroll_pixels moves world pixels (the original's whole-pixel scroll) at ") + zoom::level_name(z));
        }
    }

    // set_zoom on the camera
    {
        ViewportCamera c = camera_of(kViews[1], 16, 21, 1.0f, true);
        c.set_origin(700.0, 600.0, 60, 60);
        c.set_zoom(2.0f, 400, 250, 60, 60);
        check(c.zoom == 2.0f && c.x == 900.0f && c.y == 725.0f && c.world_x == 900 && c.world_y == 725, "set_zoom(2, (400, 250)) from (700, 600): the origin is (900, 725)");
        c.set_zoom(1.0f, 400, 250, 60, 60);
        check(c.zoom == 1.0f && c.x == 700.0f && c.y == 600.0f && c.world_x == 700, "... and back");
        c.set_zoom(0.75f, 400, 250, 60, 60);
        check(c.zoom == 1.0f && c.x == 700.0f, "a number that is not a level changes nothing");
        c.set_zoom(0.5f, 0, 0, 60, 60);
        check(c.zoom == 0.5f && c.x == 396.0f && c.y == 600.0f, "the pixel at the corner anchors the corner; the origin is held in the range [0, 396] of the zoom 0.5");
        c.set_zoom(1.0f, 761, 499, 60, 60);
        check(c.zoom == 1.0f && c.x == 1157.0f && c.y == 1099.0f && c.world_x == 1157, "from 0.5 back to 1 at the far pixel: the origin moves by the offset");
        ViewportCamera classic = camera_of(kViews[0], 16, 21, 1.0f, false);
        classic.set_zoom(2.0f, 10, 10, 4, 4);
        check(classic.zoom == 2.0f && classic.x == 0.0f && classic.y == 0.0f, "a 4 x 4 tile map (128 px) is smaller than the 221 x 220 world of the zoom 2 in the classic view: the origin is 0");
    }
}

// =====================================================================================================================================================
// The edge scroll and the minimap in screen pixels, the start view
// =====================================================================================================================================================

void test_scroll_model() {
    group("scroll", "the edge scroll moves the same distance on the screen at every zoom; the minimap centres the world that is seen; the start view keeps the hill in view");
    for (const ViewSize& v : kViews) {
        const ScreenLayout layout = v.w == 442 ? ScreenLayout::classic() : ScreenLayout::with_size(960, 540);
        check(layout.view().w == v.w && layout.view().h == v.h, std::string("the layout of the ") + v.name + " view");
        const int32_t map_tiles = 60;
        const int32_t map_px = map_tiles * 32;
        // the tiles form is the px form with the map's own size (the zoom 1)
        for (int32_t rate : {0, 50, 99}) {
            int mismatches = 0;
            for (int32_t y = 0; y < layout.height; y += 1) {
                for (int32_t x = 0; x < layout.width; x += 1) {
                    const EdgeScroll a = edge_scroll_step(x, y, rate, 300, 250, map_tiles, map_tiles, layout);
                    const EdgeScroll b = edge_scroll_step_px(x, y, rate, 300, 250, map_px, map_px, layout);
                    if (a.dir != b.dir || a.dx != b.dx || a.dy != b.dy) ++mismatches;
                }
            }
            check(mismatches == 0, std::string("the tiles form of the edge scroll is the px form for the map's own size, every pixel of the ") + v.name + " picture, rate " + std::to_string(rate));
        }
        // the same step on the screen at every zoom: the camera is far from the map's edges, so no clamp applies, and the step does not depend on where it is; every pixel of the picture
        for (int32_t rate : {0, 50, 99}) {
            for (const float z : zoom::kLevels) {
                int differences = 0;
                int scrolling = 0;
                const int32_t ox_s = static_cast<int32_t>(std::lround(1000.0 * z));
                const int32_t oy_s = static_cast<int32_t>(std::lround(900.0 * z));
                const int32_t map_s = static_cast<int32_t>(std::lround(4000.0 * z));
                for (int32_t y = 0; y < layout.height; ++y) {
                    for (int32_t x = 0; x < layout.width; ++x) {
                        const EdgeScroll at_one = edge_scroll_step_px(x, y, rate, 1000, 900, 4000, 4000, layout);
                        const EdgeScroll at_z = edge_scroll_step_px(x, y, rate, ox_s, oy_s, map_s, map_s, layout);
                        if (at_one.dir != at_z.dir || at_one.dx != at_z.dx || at_one.dy != at_z.dy) ++differences;
                        if (at_z.dx != 0 || at_z.dy != 0) ++scrolling;
                    }
                }
                check(differences == 0, std::string("the edge scroll step in screen pixels is the zoom 1 step at ") + zoom::level_name(z) + ", rate " + std::to_string(rate) + ", " + v.name + " picture: " + std::to_string(differences) + " pixels differ");
                check(scrolling > 100, std::string("the strips do scroll (") + std::to_string(scrolling) + " pixels)");
                // in world pixels the camera moves step / zoom: ViewportCamera::scroll_screen
                ViewportCamera cam = camera_of(v, layout.view().x, layout.view().y, z, true);
                cam.set_origin(1000.0, 900.0, 125, 125);
                const EdgeScroll s = edge_scroll_step_px(layout.width - 3, layout.height / 2, rate, cam.origin_screen_x(), cam.origin_screen_y(), static_cast<int32_t>(std::lround(4000.0 * z)), static_cast<int32_t>(std::lround(4000.0 * z)), layout);
                const float before = cam.x;
                cam.scroll_screen(s.dx, s.dy, 125, 125);
                check(s.dx > 0 && std::fabs((cam.x - before) * z - static_cast<float>(s.dx)) < 1e-3f, std::string("the camera moves by the step in screen pixels at ") + zoom::level_name(z) + ": " + std::to_string(s.dx));
            }
        }
        // a strip that cannot scroll shows no arrow: at the zoom 2 the camera at the origin cannot go west; at 0.5 a camera that sees the whole map cannot go anywhere
        {
            const EdgeScroll w = edge_scroll_step_px(2, layout.height / 2, 50, 0, 600, 8000, 8000, layout);
            check(w.dir == -1, "no arrow west of a view at the map's west edge (in screen pixels)");
            const EdgeScroll e = edge_scroll_step_px(layout.width - 2, layout.height / 2, 50, 0, 0, layout.view().w, layout.view().h, layout);
            check(e.dir == -1, "no arrow where the map (in screen pixels) is exactly the view");
            const EdgeScroll e2 = edge_scroll_step_px(layout.width - 2, layout.height / 2, 50, 0, 0, layout.view().w + 1, layout.view().h, layout);
            check(e2.dir == 2 && e2.dx == 1, "one screen pixel more map in x: the east arrow shows and the step is that one pixel");
        }
        // the minimap: from far away, the step centres the world that is seen on the point under the pointer, whatever the zoom is
        for (const float z : zoom::kLevels) {
            const LayoutRect mini = layout.minimap();
            int centred = 0;
            int tested = 0;
            for (int32_t py = mini.y; py < mini.bottom(); py += 3) {
                for (int32_t px = mini.x; px < mini.right(); px += 3) {
                    ViewportCamera cam = camera_of(v, layout.view().x, layout.view().y, z, true);
                    cam.set_origin(0.0, 0.0, 60, 60);                                    // from the corner, far from most points
                    for (int round = 0; round < 3; ++round) {
                        const EdgeScroll s = minimap_scroll_step_px(px, py, cam.origin_screen_x(), cam.origin_screen_y(), static_cast<int32_t>(std::lround(1920.0 * z)), static_cast<int32_t>(std::lround(1920.0 * z)), layout);
                        cam.scroll_screen(s.dx, s.dy, 60, 60);
                    }
                    // the point under the pointer: the offset in the minimap times map pixels / 119 (and / 91), in world pixels
                    const double want_x = std::trunc((px - mini.x) * (1920.0 / mini.w));
                    const double want_y = std::trunc((py - mini.y) * (1920.0 / mini.h));
                    const double centre_x = cam.x + (v.w / static_cast<double>(z)) / 2.0;
                    const double centre_y = cam.y + (v.h / static_cast<double>(z)) / 2.0;
                    // (at the map's edges the view cannot be centred: the point is then nearer the edge than half a view)
                    const double half_w = (v.w / static_cast<double>(z)) / 2.0;
                    const double half_h = (v.h / static_cast<double>(z)) / 2.0;
                    const bool free_x = want_x >= half_w && want_x <= 1920.0 - half_w;
                    const bool free_y = want_y >= half_h && want_y <= 1920.0 - half_h;
                    if (free_x) {
                        ++tested;
                        if (std::fabs(centre_x - want_x) <= zoom::grid(z) + 1e-9) ++centred;
                    }
                    if (free_y) {
                        ++tested;
                        if (std::fabs(centre_y - want_y) <= zoom::grid(z) + 1e-9) ++centred;
                    }
                }
            }
            check(tested > 500 && centred == tested, std::string("a press on the minimap centres the world that is seen on the point under the pointer at ") + zoom::level_name(z) + " in the " + v.name + " picture: " +
                                                       std::to_string(centred) + " of " + std::to_string(tested));
        }
        // minimap_point_px: the tiles form is the same for the map's own size
        {
            int bad = 0;
            for (int32_t py = 0; py < layout.height; py += 2) {
                for (int32_t px = 0; px < layout.width; px += 2) {
                    int32_t ax = 0, ay = 0, bx = 0, by = 0;
                    minimap_point(px, py, 60, 40, ax, ay, layout);
                    minimap_point_px(px, py, 60 * 32, 40 * 32, bx, by, layout);
                    if (ax != bx || ay != by) ++bad;
                }
            }
            check(bad == 0, "minimap_point is minimap_point_px for the map's own size");
            int32_t mx = 0, my = 0;
            minimap_point_px(layout.minimap().x + 10, layout.minimap().y + 20, 1000, 910, mx, my, layout);
            check(mx == static_cast<int32_t>(10 * (1000.0 / 119.0)) && my == static_cast<int32_t>(20 * (910.0 / 91.0)), "minimap_point_px scales the offset by the map over 119 x 91");
        }
    }

    // the start view: every map, every anchor tile, every zoom: the anchor's square is in view; the zoom 1 result is the old function's
    for (const ViewSize& v : kViews) {
        const ScreenLayout layout = v.w == 442 ? ScreenLayout::classic() : ScreenLayout::with_size(960, 540);
        for (const MapSize& m : all_maps()) {
            const int32_t tw = m.w / 32;
            const int32_t th = m.h / 32;
            int bad_old = 0;
            int bad_view = 0;
            int checked = 0;
            for (int32_t ty = 0; ty < th; ++ty) {
                for (int32_t tx = 0; tx < tw; ++tx) {
                    for (const float z : zoom::kLevels) {
                        int32_t ox = -1;
                        int32_t oy = -1;
                        start_view_origin(tx, ty, tw, th, ox, oy, layout, z);
                        // the original's rule written out: the fresh view at (0, 0) moves only right / down, just far enough to bring the right / bottom edge of the square (anchor + 192, at most
                        // the map) into the world that the view shows
                        const int32_t want_ox = std::max(0, std::min(tx * 32 + 16 + 192, m.w) - oracle_visible(v.w, z));
                        const int32_t want_oy = std::max(0, std::min(ty * 32 + 16 + 192, m.h) - oracle_visible(v.h, z));
                        if (ox != want_ox || oy != want_oy) ++bad_old;
                        if (z == 1.0f) {                                          // ... and the function without a zoom is the same
                            int32_t o2x = -2;
                            int32_t o2y = -2;
                            start_view_origin(tx, ty, tw, th, o2x, o2y, layout);
                            if (o2x != ox || o2y != oy) ++bad_old;
                        }
                        // the camera takes the origin the way show_start_view does
                        ViewportCamera cam = camera_of(v, layout.view().x, layout.view().y, z, true);
                        cam.x = 0.0f;
                        cam.y = 0.0f;
                        cam.set_origin(static_cast<double>(ox), static_cast<double>(oy), static_cast<uint32_t>(tw), static_cast<uint32_t>(th));
                        // the hill's anchor tile (and its centre) must be in the world that the view shows (when the map has one to show: a small map is centred and all of it is in view)
                        const double ax = tx * 32 + 16;
                        const double ay = ty * 32 + 16;
                        const double vis_w = v.w / static_cast<double>(z);
                        const double vis_h = v.h / static_cast<double>(z);
                        const bool in_x = ax >= cam.x && ax < cam.x + vis_w;
                        const bool in_y = ay >= cam.y && ay < cam.y + vis_h;
                        if (!in_x || !in_y) ++bad_view;
                        ++checked;
                    }
                }
            }
            check(bad_old == 0, m.name + " " + v.name + ": the start view is the original's rule over the world that the view shows, every anchor, every zoom (" + std::to_string(bad_old) + " differ)");
            check(bad_view == 0, m.name + " " + v.name + ": the anchor tile's centre is in view at every zoom, every anchor tile of the map (" + std::to_string(bad_view) + " of " + std::to_string(checked) + " are not)");
        }
        // the square around the anchor: shown whole when the view is large enough for it (the original shows the 352 x 352 square of the anchor +- 160 .. 192; at the zoom 0.5 it always fits)
        {
            int32_t ox = 0;
            int32_t oy = 0;
            start_view_origin(46, 46, 60, 60, ox, oy, layout, 0.5f);
            check(ox + zoom::visible(v.w, 0.5f) >= 46 * 32 + 16 + 192 && oy + zoom::visible(v.h, 0.5f) >= 46 * 32 + 16 + 192,
                  std::string("the start view at 0.5 shows the hill's whole square in the ") + v.name + " picture: origin (" + std::to_string(ox) + ", " + std::to_string(oy) + ")");
            start_view_origin(9, 6, 60, 60, ox, oy, layout, 2.0f);
            check(ox >= 0 && oy >= 0 && 9 * 32 + 16 >= ox && 9 * 32 + 16 < ox + zoom::visible(v.w, 2.0f) && 6 * 32 + 16 >= oy && 6 * 32 + 16 < oy + zoom::visible(v.h, 2.0f),
                  std::string("the start view at 2 keeps the anchor (304, 208) in view in the ") + v.name + " picture: origin (" + std::to_string(ox) + ", " + std::to_string(oy) + ")");
        }
    }
}

// =====================================================================================================================================================
// The wheel
// =====================================================================================================================================================

void test_wheel() {
    group("wheel", "the wheel's notches and precise deltas, the natural-scrolling flip, the accumulation");
    check(zoom::wheel_amount(1, 1.0f, false) == 1.0 && zoom::wheel_amount(-1, -1.0f, false) == -1.0, "a notch away is +1 and a notch toward is -1");
    check(zoom::wheel_amount(0, 0.4f, false) > 0.39 && zoom::wheel_amount(0, 0.4f, false) < 0.41 && zoom::wheel_amount(0, -0.25f, false) == -0.25, "a trackpad's fraction is its precise amount (the whole amount is 0)");
    check(zoom::wheel_amount(2, 0.0f, false) == 2.0 && zoom::wheel_amount(-3, 0.0f, false) == -3.0, "an old SDL gives no precise amount: the whole amount counts");
    check(zoom::wheel_amount(1, 1.5f, false) == 1.5, "when both are given the precise amount counts");
    check(zoom::wheel_amount(1, 1.0f, true) == -1.0 && zoom::wheel_amount(-2, -2.0f, true) == 2.0 && zoom::wheel_amount(0, 0.3f, true) < -0.29 && zoom::wheel_amount(3, 0.0f, true) == -3.0,
          "natural scrolling (SDL_MOUSEWHEEL_FLIPPED): the numbers are SDL's already inverted, they are multiplied by -1 to change them back, so the wheel rolled away zooms in whatever the system's setting is");
    check(zoom::wheel_amount(0, 0.0f, false) == 0.0 && zoom::wheel_amount(0, 0.0f, true) == 0.0, "no scroll is no amount");

    zoom::WheelAccumulator w;
    check(w.feed(1.0, 100) == 1 && w.feed(1.0, 200) == 1 && w.feed(-1.0, 300) == -1, "each notch is a step: away, away, toward");
    check(w.feed(0.0, 400) == 0 && w.pending() == 0.0, "no scroll: no step");
    w.reset();
    check(w.feed(0.4, 1000) == 0 && w.feed(0.4, 1016) == 0 && w.feed(0.4, 1032) == 1 && std::fabs(w.pending() - 0.2) < 1e-9, "0.4 + 0.4 + 0.4 is one step with 0.2 left");
    check(w.feed(0.4, 1048) == 0 && w.feed(0.4, 1064) == 1 && std::fabs(w.pending() - 0.0) < 1e-9 + 0.0, "... and 0.2 + 0.4 + 0.4 is the next");
    w.reset();
    check(w.feed(-0.6, 5000) == 0 && w.feed(-0.6, 5010) == -1 && std::fabs(w.pending() + 0.2) < 1e-9, "toward: -0.6 - 0.6 is one step out with -0.2 left");
    w.reset();
    check(w.feed(0.6, 7000) == 0 && w.feed(-0.6, 7010) == 0 && std::fabs(w.pending() + 0.6) < 1e-9, "a change of direction starts again from nothing: +0.6 then -0.6 is -0.6");
    check(w.feed(-0.6, 7020) == -1, "... and the next -0.6 completes the step");
    w.reset();
    check(w.feed(0.7, 9000) == 0 && w.feed(0.7, 9000 + zoom::WheelAccumulator::kStaleMs) == 1, "a pause of exactly the stale time still counts: 0.7 + 0.7 is a step");
    w.reset();
    check(w.feed(0.7, 9000) == 0 && w.feed(0.7, 9000 + zoom::WheelAccumulator::kStaleMs + 1) == 0 && std::fabs(w.pending() - 0.7) < 1e-9, "a longer pause forgets the left-over fraction: 0.7, a pause, 0.7 is no step");
    w.reset();
    check(w.feed(0.7, 4000000000u) == 0 && w.feed(0.7, 4000000100u) == 1, "the clock's large values are fine");
    w.reset();
    check(w.feed(5.0, 100) == 5 && w.feed(-3.0, 110) == -3 && w.feed(100.0, 120) == zoom::WheelAccumulator::kMaxSteps && w.feed(-100.0, 130) == -zoom::WheelAccumulator::kMaxSteps, "several notches in one event are that many steps, to a bound");
    w.reset();
    check(w.feed(0.5, 100) == 0 && w.pending() == 0.5, "half a notch is pending");
    w.reset();
    check(w.pending() == 0.0 && w.feed(0.5, 100) == 0, "reset forgets the left-over");
    // an event that is not a number (a broken driver, a synthetic event): no amount, no step, and nothing forgotten or poisoned (the review of v0.1.0: static_cast<int>(NaN) is undefined and a NaN in
    // the accumulator stays there for good)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        check(zoom::wheel_amount(0, nan, false) == 0.0 && zoom::wheel_amount(0, inf, false) == 0.0 && zoom::wheel_amount(0, -inf, false) == 0.0 && zoom::wheel_amount(0, nan, true) == 0.0,
              "a precise amount that is NaN or infinite is no amount (flipped or not)");
        check(zoom::wheel_amount(3, nan, false) == 0.0 && zoom::wheel_amount(-3, -inf, true) == 0.0, "... whatever the whole amount says: the event cannot be trusted");
        zoom::WheelAccumulator g;
        const double dnan = std::numeric_limits<double>::quiet_NaN();
        const double dinf = std::numeric_limits<double>::infinity();
        check(g.feed(0.4, 100) == 0 && g.feed(dnan, 110) == 0 && g.feed(dinf, 120) == 0 && g.feed(-dinf, 130) == 0 && std::fabs(g.pending() - 0.4) < 1e-12, "NaN and infinities add nothing and forget nothing: 0.4 is still pending");
        check(g.feed(0.7, 140) == 1 && std::fabs(g.pending() - 0.1) < 1e-9, "... and the next fraction completes the step: 0.4 + 0.7 is one step with 0.1 left");
        g.reset();
        check(g.feed(dnan, 200) == 0 && g.feed(0.5, 210) == 0 && g.feed(0.5, 220) == 1, "a NaN first, on a fresh accumulator: nothing happens, the next events are as if it had not been");
        // a NaN does not refresh the clock either: the left-over of 0.4 is forgotten after the pause as ever
        g.reset();
        check(g.feed(0.4, 1000) == 0 && g.feed(dnan, 1400) == 0 && g.feed(0.7, 1000 + zoom::WheelAccumulator::kStaleMs + 1) == 0 && std::fabs(g.pending() - 0.7) < 1e-9, "a NaN does not keep a fraction alive past the stale time");
        // an absurd amount is the most that one event can ask for: it never leaves a NaN or a huge remainder behind
        g.reset();
        check(g.feed(1.0e300, 100) == zoom::WheelAccumulator::kMaxSteps && g.pending() == 0.0 && g.feed(-1.0e300, 110) == -zoom::WheelAccumulator::kMaxSteps && g.pending() == 0.0, "an absurd amount is kMaxSteps and leaves nothing");
        check(g.feed(std::numeric_limits<double>::max(), 120) == zoom::WheelAccumulator::kMaxSteps && g.feed(-std::numeric_limits<double>::max(), 130) == -zoom::WheelAccumulator::kMaxSteps && g.feed(0.5, 140) == 0 && g.pending() == 0.5, "even the largest double: the accumulator still works afterwards");
    }
    // a long run of small deltas sums to the right number of steps
    w.reset();
    int steps = 0;
    for (int i = 0; i < 1600; ++i) steps += w.feed(0.0625, 10000u + static_cast<uint32_t>(i) * 10u);
    check(steps == 100, "1600 events of 1/16 (a trackpad's creep) make 100 steps");
}

// =====================================================================================================================================================
// The settings key
// =====================================================================================================================================================

void test_settings_text() {
    group("settings", "the text of the key `zoom` and of --zoom");
    for (float z : zoom::kLevels) {
        float out = 99.0f;
        std::string why;
        check(zoom::parse_level(zoom::level_name(z), out, why) && out == z, std::string("level_name -> parse_level round trip: ") + zoom::level_name(z));
    }
    check(std::string(zoom::level_name(0.5f)) == "0.5" && std::string(zoom::level_name(1.0f)) == "1" && std::string(zoom::level_name(2.0f)) == "2", "the names are 0.5, 1 and 2");
    for (const char* ok : {"0.5", "0.50", ".5", "1", "1.0", "1.00", "2", "2.0", "2.000"}) {
        float out = 0.0f;
        std::string why;
        check(zoom::parse_level(ok, out, why) && zoom::is_level(out), std::string("\"") + ok + "\" is a level");
    }
    for (const char* bad : {"", " ", "0", "3", "1.5", "0.75", "-1", "+1", "1x", "x", "2 ", " 2", "0.5.", "1..0", ".", "1,0", "two", "nan", "inf", "0x1", "1e0", "20", "0.51", "0.5000001", "1.00000001", "2.0000001", "\t1"}) {
        float out = 7.0f;
        std::string why;
        check(!zoom::parse_level(bad, out, why) && out == 7.0f && why.find("only 0.5, 1 and 2") != std::string::npos && why.find(std::string("\"") + bad + "\"") == 0, std::string("\"") + bad + "\" is refused with a reason that names it");
    }
    {
        std::string why;
        float out = 0.0f;
        check(zoom::parse_level("1.", out, why) && out == 1.0f, "\"1.\" is the level 1");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;                                     // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    test_levels();
    test_offered_levels();
    test_anchored_zoom();
    test_camera();
    test_scroll_model();
    test_wheel();
    test_settings_text();
    std::printf("\nzoom model: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
