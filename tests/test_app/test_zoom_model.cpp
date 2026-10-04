// The zoom of the map view, the model (milestone M4 of the widescreen work, many levels since the batch after v0.2.0): include/ants_app/view_zoom.hpp, the camera that carries the zoom
// (ViewportCamera in renderer.hpp) and the edge scroll's functions in screen pixels (edge_scroll.hpp). No window, no renderer: pure numbers, with every expectation written out here
// independently of the header (in whole numbers where the zoom is a power of two: a world coordinate is held in half pixels, so that the arithmetic of the zoom 2 is exact; in long
// doubles and by the definitions where it is not).
//   * the series of levels (2^(k/4), four to a doubling), the world that a view shows, the grid of the camera's origin, the conversions between the screen and the world at every level;
//   * THE LIMIT of the zoom-out (the map's width or height just fills the view) and which levels a view over a map offers (the six shipped maps, a 16 x 16 map, a 12 x 12 one and a
//     narrow one, the wide and the classic view, a texture that is too small for the map), the nearest level, the next level in a direction, the level a match starts with (no
//     limit of the kind of match: a match of the network offers the same levels);
//   * the camera after a zoom that keeps the world point under the pointer: at hundreds of pointer positions, from cameras all over every map, with the clamps (the origin on its
//     grid, inside the map, a small map centred);
//   * the camera itself: its conversions, its clamp, center_on, the scroll in screen pixels;
//   * the plan of the renderer's world pass: what part of the map the target covers, how often it is halved, where the last level lands, at every level and origin;
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
        static const int cap = std::getenv("ANTS_TEST_FAILURES") != nullptr ? std::atoi(std::getenv("ANTS_TEST_FAILURES")) : 60;      // (how many failures are printed; the count is always complete)
        if (g_failures <= cap) std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
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

/// The levels whose arithmetic is whole numbers in half pixels: powers of two (the zoom 2, 1, 0.5: exact in a float, the grid of the origin is 0.5, 1 and 2 world pixels)
constexpr std::array<float, 3> kExact = {0.5f, 1.0f, 2.0f};
/// The levels of the series that are not powers of two, and 0.25 (the pyramid's first level): written out as numbers, not computed by the header
const std::vector<float> kInBetween = {1.6817929f, 1.4142135f, 1.1892071f, 0.8408964f, 0.7071068f, 0.5946036f, 0.4204482f, 0.3535534f, 0.2973018f};
const std::vector<float> kEvery = {2.0f, 1.6817929f, 1.4142135f, 1.1892071f, 1.0f, 0.8408964f, 0.7071068f, 0.5946036f, 0.5f, 0.4204482f, 0.3535534f, 0.2973018f, 0.25f};

/// 2^(k/4) the plain way (the header builds it from a table of roots and an exponent)
float ref_series(int k) { return static_cast<float>(std::pow(2.0, static_cast<double>(k) / 4.0)); }
/// the world pixels that len screen pixels show at the zoom z, rounded up, in long doubles
int32_t ref_visible(int32_t len, float z) { return static_cast<int32_t>(std::ceil(static_cast<long double>(len) / static_cast<long double>(z))); }

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

/// ceil and floor in long doubles of a quotient, for the zooms that are not exact: false when the quotient is so near a whole number that two correct roundings may differ (the sample is skipped)
bool away_from_whole(long double v) {
    const long double frac = v - std::floor(v);
    return frac > 1e-9L && frac < 1.0L - 1e-9L;
}

void test_levels() {
    group("levels", "the series of levels, the world that a view shows, the grid and the conversions, at every level");
    // the series: 2^(k/4), four to a doubling, from 2 down
    check(zoom::kStepsPerDoubling == 4 && zoom::kNormal == 1.0f && zoom::kIn == 2.0f && zoom::kSmallest == 0.05f, "four levels to a doubling; 2 is the most; 1 is the original's picture; no setting below 0.05");
    for (int k = -48; k <= 8; ++k) {
        const float got = zoom::series(k);
        const double want = std::pow(2.0, static_cast<double>(k) / 4.0);
        check(std::fabs(static_cast<double>(got) / want - 1.0) < 1e-6 && got == ref_series(k), "series(" + std::to_string(k) + ") is 2^(k/4): " + num(static_cast<double>(got)));
        if (k % 4 == 0) check(static_cast<double>(got) == want, "a power of two is exact in a float: series(" + std::to_string(k) + ")");
        if (k < 8) check(std::fabs(static_cast<double>(zoom::series(k + 1)) / static_cast<double>(got) - std::pow(2.0, 0.25)) < 1e-6, "one step up is the fourth root of 2 times as much at k = " + std::to_string(k));
    }
    check(zoom::series(4) == 2.0f && zoom::series(0) == 1.0f && zoom::series(-4) == 0.5f && zoom::series(-8) == 0.25f && zoom::series(-12) == 0.125f, "2, 1, 0.5, 0.25 and 0.125 are the exact levels");
    {
        const double named[13] = {2.0, 1.68, 1.41, 1.19, 1.0, 0.84, 0.71, 0.59, 0.5, 0.42, 0.35, 0.30, 0.25};         // (as the owner's list names them)
        for (int i = 0; i < 13; ++i) check(std::fabs(static_cast<double>(zoom::series(4 - i)) - named[i]) < 0.005, "the level " + std::to_string(i) + " of the series is " + num(named[i]) + " to two decimals");
    }
    for (const float z : kEvery) check(std::fabs(static_cast<double>(z) / static_cast<double>(zoom::series(static_cast<int>(std::lround(std::log2(static_cast<double>(z)) * 4.0)))) - 1.0) < 1e-6,
                                       "the number " + num(static_cast<double>(z)) + " in this test is a level of the series");

    // the world that a view shows, and the grid of the camera's origin
    for (const ViewSize& v : kViews) {
        for (float z : kExact) {
            check(zoom::visible(v.w, z) == oracle_visible(v.w, z) && zoom::visible(v.h, z) == oracle_visible(v.h, z), std::string("the world that the ") + v.name + " view shows at " + zoom::level_name(z));
        }
        for (const float z : kEvery) {
            check(zoom::visible(v.w, z) == static_cast<int32_t>(std::ceil(static_cast<long double>(v.w) / static_cast<long double>(z))) &&
                      zoom::visible(v.h, z) == static_cast<int32_t>(std::ceil(static_cast<long double>(v.h) / static_cast<long double>(z))),
                  std::string("the world that the ") + v.name + " view shows at " + num(static_cast<double>(z)) + ": the view over the zoom, rounded up");
            check(std::fabs(zoom::visible_exact(v.w, z) * static_cast<double>(z) - v.w) < 1e-9, std::string("visible_exact times the zoom is the view at ") + num(static_cast<double>(z)));
        }
    }
    check(zoom::visible(442, 2.0f) == 221 && zoom::visible(440, 2.0f) == 220 && zoom::visible(442, 0.5f) == 884 && zoom::visible(440, 0.5f) == 880, "442 x 440 shows 221 x 220 at 2 and 884 x 880 at 0.5");
    check(zoom::visible(762, 2.0f) == 381 && zoom::visible(500, 2.0f) == 250 && zoom::visible(762, 0.5f) == 1524 && zoom::visible(500, 0.5f) == 1000, "762 x 500 shows 381 x 250 at 2 and 1524 x 1000 at 0.5");
    check(zoom::visible(762, 0.25f) == 3048 && zoom::visible(500, 0.25f) == 2000 && zoom::visible(762, ref_series(-2)) == 1078 && zoom::visible(500, ref_series(-2)) == 708, "762 x 500 shows 3048 x 2000 at 0.25 and 1078 x 708 at 0.71");
    check(zoom::visible(443, 2.0f) == 222 && zoom::visible(1, 2.0f) == 1 && zoom::visible(0, 2.0f) == 0 && zoom::visible(1, 0.5f) == 2, "an odd length rounds up at the zoom 2: a world pixel that is shown in part is shown");
    check(zoom::visible_exact(443, 2.0f) == 221.5 && zoom::visible_exact(762, 0.5f) == 1524.0 && zoom::visible_exact(762, 1.0f) == 762.0, "the exact world that a view shows");

    check(zoom::grid(0.5f) == 2.0 && zoom::grid(1.0f) == 1.0 && zoom::grid(2.0f) == 0.5 && zoom::grid(0.25f) == 4.0, "the grid of the origin is one screen pixel: 2, 1, half and 4 world pixels at 0.5, 1, 2 and 0.25");
    for (const float z : kEvery) {
        check(std::fabs(zoom::grid(z) * static_cast<double>(z) - 1.0) < 1e-12, std::string("the grid times the zoom is one screen pixel at ") + num(static_cast<double>(z)));
        for (int i = -200; i <= 200; ++i) {
            const double on_grid = i * zoom::grid(z);
            check(zoom::snap(on_grid, z) == on_grid, "a point on the grid stays: " + num(on_grid) + " at " + num(static_cast<double>(z)));
        }
        for (int i = 0; i < 400; ++i) {                                                   // the nearest point of the grid, half up, at any zoom
            const double v = -500.0 + (lcg() % 1000000) / 1000.0;
            const double got = zoom::snap(v, z);
            check(std::fabs(got - v) <= zoom::grid(z) / 2.0 + 1e-9 && std::fabs(got * static_cast<double>(z) - std::round(got * static_cast<double>(z))) < 1e-9,
                  "snap(" + num(v) + ") is the nearest point of the grid at " + num(static_cast<double>(z)) + ": " + num(got));
            const double toward = zoom::snap_toward_zero(v, z);
            check(std::fabs(toward) <= std::fabs(v) + 1e-9 && std::fabs(toward - v) < zoom::grid(z) + 1e-9, "snap_toward_zero(" + num(v) + ") is the next point of the grid toward 0 at " + num(static_cast<double>(z)));
        }
    }
    check(zoom::snap(4.25, 2.0f) == 4.5 && zoom::snap(4.24, 2.0f) == 4.0 && zoom::snap(4.5, 1.0f) == 5.0 && zoom::snap(4.49, 1.0f) == 4.0 && zoom::snap(-4.5, 1.0f) == -4.0 && zoom::snap(3.0, 0.5f) == 4.0 &&
              zoom::snap(2.99, 0.5f) == 2.0 && zoom::snap(1.0, 0.5f) == 2.0,
          "snap rounds half up to the grid: 4.25 at the zoom 2 is 4.5, 4.5 at 1 is 5, 3 at 0.5 is 4");
    check(zoom::snap_toward_zero(-125.9, 1.0f) == -125.0 && zoom::snap_toward_zero(125.9, 1.0f) == 125.0 && zoom::snap_toward_zero(-14.7, 2.0f) == -14.5 && zoom::snap_toward_zero(-505.0, 0.5f) == -504.0 &&
              zoom::snap_toward_zero(-0.9, 1.0f) == 0.0,
          "snap_toward_zero truncates to the grid, as the original's integer division does");
    check(zoom::snap_down(7.0, 1.0f) == 7.0 && zoom::snap_down(7.9, 1.0f) == 7.0 && zoom::snap_down(-0.5, 1.0f) == -1.0 && zoom::snap_down(9.0, 0.5f) == 8.0 && zoom::snap_down(8.0 - 1e-6, 0.5f) == 6.0 &&
              zoom::snap_down(8.0 - 1e-12, 0.5f) == 8.0 && zoom::snap_down(3.0 - 1e-12, 2.0f) == 3.0,
          "snap_down rounds down to the grid, and a coordinate a hair below a point of the grid (the rounding of a division) counts as on it");

    // the world pixel under a screen pixel
    for (float z : kExact) {
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
    // ... and at the levels that are not exact: the origin n / z (the lattice of screen pixels), the world point under a pixel's left edge is (n + offset) / z, in long doubles
    for (const float z : kEvery) {
        int tested = 0;
        for (int n = -60; n <= 3000; n += (n < 40 ? 1 : 29)) {
            const double origin = static_cast<double>(n) / static_cast<double>(z);
            for (int32_t d = -3; d <= 1600; d += (d < 12 ? 1 : 41)) {
                const long double exact = (static_cast<long double>(n) + d) / static_cast<long double>(z);
                const bool is_exact_level = z == 0.5f || z == 1.0f || z == 2.0f || z == 0.25f;                // (a power of two: the quotient is exact, there is no rounding to disagree about)
                if (!is_exact_level && !away_from_whole(exact)) continue;
                ++tested;
                check(zoom::world_at(origin, z, d) == static_cast<int32_t>(std::floor(exact)), "world_at lattice " + std::to_string(n) + " zoom " + num(static_cast<double>(z)) + " offset " + std::to_string(d));
                check(zoom::world_edge_up(origin, z, d) == static_cast<int32_t>(std::ceil(exact)), "world_edge_up lattice " + std::to_string(n) + " zoom " + num(static_cast<double>(z)) + " offset " + std::to_string(d));
            }
        }
        check(tested > 1000, "the sweep at " + num(static_cast<double>(z)) + " has samples: " + std::to_string(tested));
    }
    check(zoom::world_at(100.0, 1.0f, 17) == 117 && zoom::world_at(100.0, 2.0f, 17) == 108 && zoom::world_at(100.5, 2.0f, 17) == 109 && zoom::world_at(100.0, 0.5f, 17) == 134,
          "worked examples: at 1 the pixel 17 over is 117, at 2 it is 108, from 100.5 it is 109, at 0.5 it is 134");
    check(zoom::world_edge_up(100.0, 2.0f, 17) == 109 && zoom::world_edge_up(100.0, 1.0f, 17) == 117 && zoom::world_edge_up(100.0, 2.0f, 16) == 108, "the edge of a screen pixel rounds up at the zoom 2");
    check(zoom::world_at(100.0, ref_series(-2), 17) == 124 && zoom::world_at(100.0, ref_series(2), 17) == 112 && zoom::world_at(100.0, 0.25f, 17) == 168, "worked examples: from 100 the pixel 17 over is 124 at 0.71, 112 at 1.41 and 168 at 0.25");
}

// =====================================================================================================================================================
// The limit of the zoom-out and which levels are offered
// =====================================================================================================================================================

zoom::Fit fit_of(const MapSize& m, const ViewSize& v, int32_t max_world = 0) {
    zoom::Fit f;
    f.view_w = v.w;
    f.view_h = v.h;
    f.map_w = m.w;
    f.map_h = m.h;
    f.max_world = max_world;
    return f;
}

/// The exact limit of the zoom-out, in long doubles: the map's width or its height just fills the view (and the texture holds the world that is seen), never below 0.05, at most 1
long double ref_limit(const MapSize& m, const ViewSize& v, int32_t max_world, float min_zoom) {
    if (m.w <= 0 || m.h <= 0) return 1.0L;
    long double limit = std::max(static_cast<long double>(v.w) / m.w, static_cast<long double>(v.h) / m.h);
    if (max_world > 0) limit = std::max(limit, static_cast<long double>(std::max(v.w, v.h)) / max_world);
    limit = std::max(limit, 0.05L);
    limit = std::max(limit, static_cast<long double>(min_zoom));
    return std::min(limit, 1.0L);
}

/// The levels, written out another way: the candidates of the series from 2 (or the limits' top) down, those at least a quarter step above the limit, and the limit as a float that is not below it
std::vector<float> ref_levels(const MapSize& m, const ViewSize& v, float min_zoom, float max_zoom, int32_t max_world = 0) {
    std::vector<float> out;
    for (int k = 4; k >= 0; --k) {
        if (k == 0 || static_cast<double>(ref_series(k)) <= static_cast<double>(max_zoom) * (1.0 + 1e-6)) out.push_back(ref_series(k));
    }
    const long double limit = ref_limit(m, v, max_world, min_zoom);
    if (limit >= 1.0L) return out;
    for (int k = -1; k >= -60; --k) {
        if (static_cast<long double>(ref_series(k)) < limit * 1.044L) break;
        out.push_back(ref_series(k));
    }
    float f = static_cast<float>(limit);
    while (static_cast<long double>(f) < limit) f = std::nextafter(f, 2.0f);
    out.push_back(f);
    return out;
}

/// The levels of the series that a view over a map can have (the rest would show more than the map)
std::vector<float> levels_for(const MapSize& m, const ViewSize& v) {
    std::vector<float> out;
    const float limit = zoom::floor_zoom(fit_of(m, v));
    for (const float z : kEvery) {
        if (z >= limit) out.push_back(z);
    }
    return out;
}

std::string list_text(const std::vector<float>& list) {
    std::string out;
    for (const float l : list) out += (out.empty() ? "" : " ") + num(static_cast<double>(l));
    return out;
}

void test_offered_levels() {
    group("offered", "the limit of the zoom-out, which levels a view over a map offers, the nearest level, the next level in a direction, the level a match starts with");
    const zoom::Limits any = zoom::Limits::any();
    const zoom::Limits only_one = zoom::Limits::only_normal();
    check(any.min_zoom == 0.0f && any.max_zoom == 2.0f && only_one.min_zoom == 1.0f && only_one.max_zoom == 1.0f && any != only_one && any == zoom::Limits{} && only_one == zoom::Limits::only_normal(),
          "the limits: no limit of the kind of match (a match of the network has the levels of a local game), or the original's picture only (a renderer that cannot make the offscreen target)");

    const std::vector<MapSize> maps = all_maps();
    check(shipped_maps().size() == 6, "there are six shipped maps");

    // THE LIMIT: max(view_w / map_w, view_h / map_h), the smallest float that shows no more than the map, never above 1
    for (const MapSize& m : maps) {
        for (const ViewSize& v : kViews) {
            const std::string at = m.name + " (" + std::to_string(m.w) + " x " + std::to_string(m.h) + ") in the " + v.name + " view: ";
            const float f = zoom::floor_zoom(fit_of(m, v));
            const long double want = ref_limit(m, v, 0, 0.0f);
            check(f <= 1.0f && f >= zoom::kSmallest, at + "the limit is between 0.05 and 1: " + num(static_cast<double>(f)));
            check(static_cast<long double>(f) >= want && (f == 1.0f || static_cast<long double>(std::nextafter(f, 0.0f)) < want), at + "the limit is the smallest float that is not below max(view / map): " + num(static_cast<double>(f)));
            if (want < 1.0L) {
                check(static_cast<long double>(v.w) / f <= m.w && static_cast<long double>(v.h) / f <= m.h, at + "the world seen at the limit is not more than the map");
                // one axis is filled exactly: the map's width or its height is the view over the zoom
                const long double seen_w = static_cast<long double>(v.w) / f;
                const long double seen_h = static_cast<long double>(v.h) / f;
                check(std::fabs(seen_w - m.w) < 1e-3L * m.w || std::fabs(seen_h - m.h) < 1e-3L * m.h || f == zoom::kSmallest, at + "the map's width or its height just fills the view at the limit");
            } else {
                check(m.w <= v.w || m.h <= v.h || f == 1.0f, at + "a map that the view covers on an axis at 1 has no zoom-out");
            }
        }
    }
    // the numbers of the shipped maps: the width or the height decides, whichever is more
    {
        const MapSize treasure{"TREASURE", 1920, 1920};
        check(std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(treasure, kViews[1]))) - 762.0 / 1920.0) < 1e-6, "TREASURE in the wide view: the width decides, 762 / 1920 = 0.396875");
        check(std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(treasure, kViews[0]))) - 442.0 / 1920.0) < 1e-6, "TREASURE in the classic view: the width decides, 442 / 1920 = 0.230208");
        const MapSize tall{"tall", 600, 3000};
        check(zoom::floor_zoom(fit_of(tall, kViews[1])) == 1.0f, "a 600 x 3000 map in the wide view: 762 / 600 is above 1: no zoom-out");
        check(std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(tall, kViews[0]))) - 442.0 / 600.0) < 1e-6, "... in the classic view: the width decides, 442 / 600 = 0.7367");
        const MapSize long_map{"long", 3000, 700};
        check(std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(long_map, kViews[1]))) - 500.0 / 700.0) < 1e-6, "a 3000 x 700 map in the wide view: the height decides (500 / 700 = 0.714; the width would be 762 / 3000 = 0.254): the map's smaller side just fills the view");
        const MapSize deep{"deep", 800, 2000};
        check(std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(deep, kViews[0]))) - 442.0 / 800.0) < 1e-6 && std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(MapSize{"d", 1100, 3000}, kViews[0]))) - 442.0 / 1100.0) < 1e-6,
              "a map that is taller than wide has its width decide when the view is as tall as it is wide (the classic view)");
        const MapSize high{"high", 1500, 2400};
        check(std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(high, kViews[1]))) - 762.0 / 1500.0) < 1e-6, "1500 x 2400 in the wide view: the width, 762 / 1500 = 0.508 (the height would be 500 / 2400)");
        const MapSize wide_low{"wide_low", 1000, 2000};
        check(std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(wide_low, kViews[1]))) - 0.762) < 1e-6, "1000 x 2000: 762 / 1000 against 500 / 2000: the larger, 0.762");
        const MapSize flat{"flat", 4000, 520};
        check(std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(flat, kViews[1]))) - 500.0 / 520.0) < 1e-6, "a flat 4000 x 520 map: the height decides (500 / 520 = 0.9615 against 762 / 4000 = 0.19)");
    }
    // a texture that holds fewer world pixels than the map is wide: the limit rises so that the world seen fits it
    {
        const MapSize huge{"huge", 4096, 4096};
        check(std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(huge, kViews[1], 4096 - 64))) - 762.0 / 4032.0) < 1e-6, "a 4096 x 4096 map in a texture of 4096: the world seen is at most 4032 pixels (the margins of the pass): 762 / 4032 = 0.189");
        check(std::fabs(static_cast<double>(zoom::floor_zoom(fit_of(huge, kViews[0], 2048 - 64))) - 442.0 / 1984.0) < 1e-6, "... in a texture of 2048 and the classic view: 442 / 1984");
        check(zoom::floor_zoom(fit_of(MapSize{"t", 1920, 1920}, kViews[1], 8192)) == zoom::floor_zoom(fit_of(MapSize{"t", 1920, 1920}, kViews[1])), "a texture that is large enough changes nothing");
        const float f = zoom::floor_zoom(fit_of(huge, kViews[1], 4032));
        check(static_cast<long double>(762) / f <= 4032.0L, "the world seen at that limit fits the texture");
        const MapSize wild{"wild", 100000, 100000};
        check(zoom::floor_zoom(fit_of(wild, kViews[0])) == zoom::kSmallest, "a map that would need a zoom below 0.05 stops at 0.05");
    }
    // no map (none is loaded) and nonsense views: no zoom-out, never a crash
    check(zoom::floor_zoom(zoom::Fit{}) == 1.0f && zoom::floor_zoom(fit_of(MapSize{"none", 0, 0}, kViews[1])) == 1.0f && zoom::floor_zoom(zoom::Fit{762, 500, 1920, 0, 0}) == 1.0f, "no map, no zoom-out");

    // THE LEVELS of every map in every view: the list written out another way, exactly
    for (const MapSize& m : maps) {
        for (const ViewSize& v : kViews) {
            const std::string at = m.name + " (" + std::to_string(m.w) + " x " + std::to_string(m.h) + ") in the " + v.name + " view: ";
            for (const zoom::Limits& lim : {any, only_one, zoom::Limits{0.0f, 1.0f}, zoom::Limits{0.5f, 2.0f}, zoom::Limits{0.7f, 1.5f}}) {
                const std::vector<float> got = zoom::levels(fit_of(m, v), lim);
                const std::vector<float> want = ref_levels(m, v, lim.min_zoom, lim.max_zoom);
                check(got == want, at + "the levels with the limits " + num(static_cast<double>(lim.min_zoom)) + " .. " + num(static_cast<double>(lim.max_zoom)) + " are [" + list_text(want) + "], not [" + list_text(got) + "]");
                check(!got.empty() && std::find(got.begin(), got.end(), 1.0f) != got.end(), at + "the level 1 is always offered (it is the original's picture)");
                check(std::is_sorted(got.begin(), got.end(), [](float a, float b) { return a > b; }) && std::adjacent_find(got.begin(), got.end()) == got.end(), at + "the levels are strictly descending");
                for (size_t i = 1; i < got.size(); ++i) check(got[i - 1] / got[i] >= 1.04f, at + "no two levels are closer than a quarter step: " + num(static_cast<double>(got[i - 1])) + " " + num(static_cast<double>(got[i])));
                if (!got.empty()) check(got.front() <= 2.0f && got.back() >= zoom::kSmallest, at + "the levels are between 0.05 and 2");
            }
            const std::vector<float> got = zoom::levels(fit_of(m, v), any);
            check(got.size() >= 5 && got.front() == 2.0f && got[1] == ref_series(3) && got[2] == ref_series(2) && got[3] == ref_series(1) && got[4] == 1.0f, at + "from 2 down to 1: 2, 1.68, 1.41, 1.19, 1");
            // every level but the last is of the series, the last is the limit of the map (when it is below 1)
            const float limit = zoom::floor_zoom(fit_of(m, v));
            check(got.back() == (limit < 1.0f ? limit : 1.0f), at + "the last level is the limit of the map");
            for (size_t i = 0; i + 1 < got.size(); ++i) check(got[i] == ref_series(static_cast<int>(std::lround(std::log2(static_cast<double>(got[i])) * 4.0))), at + "the level " + num(static_cast<double>(got[i])) + " is of the series");
            // offered: exactly the levels of the list
            for (const float l : got) check(zoom::offered(l, fit_of(m, v), any), at + "offered: " + num(static_cast<double>(l)));
            for (const float bad : {0.0f, 0.75f, 1.5f, 3.0f, -1.0f, std::nextafter(1.0f, 2.0f), std::nextafter(2.0f, 3.0f), std::nextafter(1.0f, 0.0f)}) {
                check(!zoom::offered(bad, fit_of(m, v), any), at + "a number that is not a level is never offered: " + num(static_cast<double>(bad)));
            }
        }
    }
    // the worked lists (the numbers of the screenshots)
    {
        const auto near_list = [](const std::vector<float>& got, const std::vector<double>& want) {
            if (got.size() != want.size()) return false;
            for (size_t i = 0; i < got.size(); ++i) {
                if (std::fabs(static_cast<double>(got[i]) - want[i]) > 5e-5) return false;
            }
            return true;
        };
        const MapSize treasure{"TREASURE", 1920, 1920};
        const std::vector<float> wide = zoom::levels(fit_of(treasure, kViews[1]), any);
        check(near_list(wide, {2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.84090, 0.70711, 0.59460, 0.5, 0.42045, 0.396875}), "TREASURE, wide: 2, 1.68, 1.41, 1.19, 1, 0.84, 0.71, 0.59, 0.5, 0.42 and the limit 0.397 (0.35 is closer than a quarter step to it): " + list_text(wide));
        const std::vector<float> classic = zoom::levels(fit_of(treasure, kViews[0]), any);
        check(near_list(classic, {2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.84090, 0.70711, 0.59460, 0.5, 0.42045, 0.35355, 0.29730, 0.25, 0.230208}), "TREASURE, classic: ... 0.42, 0.35, 0.30, 0.25 and the limit 0.230: " + list_text(classic));
        const std::vector<float> tiny = zoom::levels(fit_of(MapSize{"TINY", 992, 992}, kViews[1]), any);
        check(near_list(tiny, {2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.84090, 0.768145}), "TINY, wide: 2 ... 0.84 and the limit 0.768: " + list_text(tiny));
        const std::vector<float> small = zoom::levels(fit_of(MapSize{"SMALL", 1280, 1280}, kViews[1]), any);
        check(near_list(small, {2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.84090, 0.70711, 0.595313}), "SMALL, wide: 2 ... 0.71 and the limit 0.595 (the level 0.5946 is closer than a quarter step to it and is left out): " + list_text(small));
        const std::vector<float> sixteen = zoom::levels(fit_of(MapSize{"16x16", 512, 512}, kViews[1]), any);
        check(near_list(sixteen, {2.0, 1.68179, 1.41421, 1.18921, 1.0}), "a 16 x 16 map in the wide view is covered at 1: no zoom-out");
        const std::vector<float> sixteen_classic = zoom::levels(fit_of(MapSize{"16x16", 512, 512}, kViews[0]), any);
        check(near_list(sixteen_classic, {2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.863281}), "... in the classic view the limit is 442 / 512 = 0.863: " + list_text(sixteen_classic));
        check(zoom::levels(fit_of(treasure, kViews[1]), only_one) == std::vector<float>({1.0f}) && zoom::levels(fit_of(treasure, kViews[0]), only_one) == std::vector<float>({1.0f}), "a renderer that cannot make the offscreen target is offered the level 1 only");
        const std::vector<float> no_in = zoom::levels(fit_of(treasure, kViews[1]), zoom::Limits{0.0f, 1.0f});
        check(no_in.front() == 1.0f && no_in.back() == zoom::floor_zoom(fit_of(treasure, kViews[1])), "limits 0 .. 1: no zoom in, the zoom-out as ever");
        const std::vector<float> half = zoom::levels(fit_of(treasure, kViews[1]), zoom::Limits{0.5f, 2.0f});
        check(half.back() == 0.5f && half.size() == 9 && half[7] == ref_series(-3), "limits 0.5 .. 2 on TREASURE, wide: the last level is 0.5 (it is not closer than a quarter step to the level above: 0.5946)");
        const std::vector<float> only_in = zoom::levels(fit_of(treasure, kViews[1]), zoom::Limits{2.0f, 2.0f});
        check(only_in == std::vector<float>({2.0f, 1.0f}) || only_in == std::vector<float>({1.0f}) || only_in.back() == 1.0f, "limits 2 .. 2: the level 1 is offered all the same, whatever the limits say");
    }

    // the nearest level: in ratio (a zoom is a factor)
    {
        const std::vector<float> list = zoom::levels(fit_of(MapSize{"TREASURE", 1920, 1920}, kViews[1]), any);
        check(zoom::nearest(1.0f, list) == 1.0f && zoom::nearest(2.0f, list) == 2.0f && zoom::nearest(0.5f, list) == 0.5f, "a level is its own nearest");
        for (const float l : list) check(zoom::nearest(l, list) == l, "the nearest of a level is itself: " + num(static_cast<double>(l)));
        check(zoom::nearest(0.7f, list) == ref_series(-2) && zoom::nearest(0.6f, list) == ref_series(-3) && zoom::nearest(1.5f, list) == ref_series(2) && zoom::nearest(1.6f, list) == ref_series(3), "0.7 is 0.71, 0.6 is 0.59, 1.5 is 1.41 and 1.6 is 1.68");
        check(zoom::nearest(5.0f, list) == 2.0f && zoom::nearest(100.0f, list) == 2.0f, "above 2 the nearest is 2");
        check(zoom::nearest(0.2f, list) == list.back() && zoom::nearest(0.01f, list) == list.back() && zoom::nearest(0.05f, list) == list.back(), "below the limit the nearest is the limit");
        check(zoom::nearest(0.0f, list) == 1.0f && zoom::nearest(-3.0f, list) == 1.0f && zoom::nearest(std::numeric_limits<float>::quiet_NaN(), list) == 1.0f && zoom::nearest(std::numeric_limits<float>::infinity(), list) == 1.0f,
              "a number that is no zoom (zero, negative, NaN, infinite) is 1");
        check(zoom::nearest(0.9f, list) == 0.8408964f && zoom::nearest(0.92f, list) == 1.0f, "0.9 is nearer 0.84 (a ratio 1.07) than 1 (1.11); 0.92 is nearer 1");
        check(zoom::nearest(1.3f, std::vector<float>{}) == 1.0f, "an empty list says 1");
        // ties (the geometric middle between two levels) go to the more zoomed in one
        const double middle = std::sqrt(static_cast<double>(list[4]) * static_cast<double>(list[5]));
        check(zoom::nearest(static_cast<float>(middle * (1.0 + 1e-6)), list) == list[4] && zoom::nearest(static_cast<float>(middle * (1.0 - 1e-6)), list) == list[5], "the geometric middle of 1 and 0.84 decides at the middle");
        // ... and an exact tie (1 is as far from 2 as from 0.5 in ratio) goes to the first level of the list, the more zoomed in one
        check(zoom::nearest(1.0f, std::vector<float>{2.0f, 0.5f}) == 2.0f && zoom::nearest(0.5f, std::vector<float>{1.0f, 0.25f}) == 1.0f, "an exact tie goes to the more zoomed in level: 1 between 2 and 0.5, 0.5 between 1 and 0.25");
    }

    // the next level in a direction
    for (const MapSize& m : maps) {
        for (const ViewSize& v : kViews) {
            const std::vector<float> list = zoom::levels(fit_of(m, v), any);
            const std::string at = m.name + " " + v.name + ": ";
            for (size_t i = 0; i < list.size(); ++i) {
                const float in = i == 0 ? list[0] : list[i - 1];
                const float out = i + 1 == list.size() ? list[i] : list[i + 1];
                check(zoom::step(list[i], +1, fit_of(m, v), any) == in, at + "in from " + num(static_cast<double>(list[i])) + " is " + num(static_cast<double>(in)));
                check(zoom::step(list[i], -1, fit_of(m, v), any) == out, at + "out from " + num(static_cast<double>(list[i])) + " is " + num(static_cast<double>(out)));
                check(zoom::step(list[i], 0, fit_of(m, v), any) == list[i], at + "no direction, no change");
                check(zoom::step(list[i], +100, fit_of(m, v), any) == in && zoom::step(list[i], -100, fit_of(m, v), any) == out, at + "the size of the direction does not matter");
            }
            // walking out from 2 reaches every level in turn and the limit at the end, walking in comes back
            float z = 2.0f;
            std::vector<float> walked{z};
            for (int i = 0; i < 100; ++i) {
                const float next = zoom::step(z, -1, fit_of(m, v), any);
                if (next == z) break;
                z = next;
                walked.push_back(z);
            }
            check(walked == list, at + "stepping out from 2 visits every level: [" + list_text(walked) + "]");
            for (int i = 0; i < 100; ++i) {
                const float next = zoom::step(z, +1, fit_of(m, v), any);
                if (next == z) break;
                z = next;
            }
            check(z == 2.0f, at + "stepping in from the limit comes back to 2");
            // a current zoom between two levels (a zoom that something else set): the next level beyond it in the direction
            if (list.size() >= 4) {
                const float between = static_cast<float>(std::sqrt(static_cast<double>(list[2]) * static_cast<double>(list[3])));
                check(zoom::step(between, +1, fit_of(m, v), any) == list[2] && zoom::step(between, -1, fit_of(m, v), any) == list[3], at + "from between two levels: in is the one above, out the one below");
            }
            check(zoom::step(10.0f, +1, fit_of(m, v), any) == 10.0f && zoom::step(10.0f, -1, fit_of(m, v), any) == 2.0f, at + "a current zoom above 2: in has nowhere to go, out is 2");
            check(zoom::step(0.001f, -1, fit_of(m, v), any) == 0.001f && zoom::step(0.001f, +1, fit_of(m, v), any) == list.back(), at + "a current zoom below the limit: out has nowhere to go, in is the limit");
            check(zoom::step(0.0f, +1, fit_of(m, v), any) == 0.0f && zoom::step(-1.0f, -1, fit_of(m, v), any) == -1.0f, at + "a current zoom that is none changes nothing");
        }
    }
    check(zoom::step(1.0f, +1, fit_of(MapSize{"t", 1920, 1920}, kViews[1]), only_one) == 1.0f && zoom::step(1.0f, -1, fit_of(MapSize{"t", 1920, 1920}, kViews[1]), only_one) == 1.0f, "step stays at 1 where nothing else is allowed");
    {   // a zoom that is a float step off a level (the rounding of a camera's number) counts as being at that level: the step goes past it, never back to it
        const zoom::Fit fit = fit_of(MapSize{"TREASURE", 1920, 1920}, kViews[1]);
        check(zoom::step(std::nextafter(1.0f, 2.0f), -1, fit, any) == ref_series(-1) && zoom::step(std::nextafter(1.0f, 0.0f), +1, fit, any) == ref_series(1) &&
                  zoom::step(std::nextafter(0.5f, 1.0f), -1, fit, any) == ref_series(-5) && zoom::step(std::nextafter(2.0f, 1.0f), +1, fit, any) == std::nextafter(2.0f, 1.0f),
              "a zoom a float step above 1 steps out to 0.84, one a float step below 1 steps in to 1.19, a float step above 0.5 steps out to 0.42, a float step below 2 has no level to step in to");
    }
    // a map that fits skips the zoom-out: from 1 there is nothing below
    check(zoom::step(1.0f, -1, fit_of(MapSize{"12x12", 384, 384}, kViews[1]), any) == 1.0f && zoom::step(2.0f, -1, fit_of(MapSize{"12x12", 384, 384}, kViews[1]), any) == ref_series(3), "a 12 x 12 map in the wide view: out stays at 1");
    check(zoom::step(0.5f, +1, fit_of(MapSize{"12x12", 384, 384}, kViews[1]), any) == 1.0f, "... and a level that was left over from another map goes in to 1");

    // the level a match starts with: the nearest of the levels that the map offers
    for (const MapSize& m : maps) {
        for (const ViewSize& v : kViews) {
            const std::vector<float> list = zoom::levels(fit_of(m, v), any);
            for (const float remembered : {0.05f, 0.1f, 0.2f, 0.3f, 0.35f, 0.397f, 0.45f, 0.5f, 0.55f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.2f, 1.5f, 1.9f, 2.0f}) {
                const float got = zoom::level_for_match(remembered, fit_of(m, v), any);
                check(got == zoom::nearest(remembered, list) && std::find(list.begin(), list.end(), got) != list.end(), m.name + " " + v.name + ": the match starts at a level that is offered, the nearest to " + num(static_cast<double>(remembered)) + ": " + num(static_cast<double>(got)));
            }
            check(zoom::level_for_match(0.0f, fit_of(m, v), any) == 1.0f && zoom::level_for_match(-2.0f, fit_of(m, v), any) == 1.0f && zoom::level_for_match(std::numeric_limits<float>::quiet_NaN(), fit_of(m, v), any) == 1.0f,
                  m.name + " " + v.name + ": a remembered number that is no zoom starts at 1");
            check(zoom::level_for_match(1.0f, fit_of(m, v), only_one) == 1.0f && zoom::level_for_match(0.5f, fit_of(m, v), only_one) == 1.0f && zoom::level_for_match(2.0f, fit_of(m, v), only_one) == 1.0f, m.name + " " + v.name + ": a renderer that draws the level 1 only starts every match at 1");
        }
    }
    check(zoom::level_for_match(0.5f, fit_of(MapSize{"TREASURE", 1920, 1920}, kViews[1]), any) == 0.5f && zoom::level_for_match(0.1f, fit_of(MapSize{"TREASURE", 1920, 1920}, kViews[1]), any) == zoom::floor_zoom(fit_of(MapSize{"TREASURE", 1920, 1920}, kViews[1])) &&
              zoom::level_for_match(0.5f, fit_of(MapSize{"TINY", 992, 992}, kViews[1]), any) == zoom::floor_zoom(fit_of(MapSize{"TINY", 992, 992}, kViews[1])) && zoom::level_for_match(2.0f, fit_of(MapSize{"TINY", 992, 992}, kViews[1]), any) == 2.0f,
          "a match on a map that the remembered level goes beyond starts at the map's limit; 0.5 is 0.5 where it is offered");
    check(zoom::level_for_match(0.3f, fit_of(MapSize{"12x12", 384, 384}, kViews[1]), any) == 1.0f, "a map that the view covers at 1 starts at 1, whatever was remembered");
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

/// The same clamp in doubles, for any zoom: the range [0, map - visible]; a map that is not larger than what the view shows centred (truncated toward zero on the grid of the zoom) or at 0
double oracle_clamp_f(double v, float z, int32_t view_len, int32_t map_px, bool centre_small) {
    const double zz = static_cast<double>(z);
    const double vis = view_len / zz;
    if (map_px > vis) return std::clamp(v, 0.0, std::floor((map_px - vis) * zz + 1e-9) / zz);        // (the far end of the range is rounded down to the lattice of screen pixels)
    if (!centre_small) return 0.0;
    return std::trunc((map_px - vis) / 2.0 * zz) / zz;
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

    constexpr int32_t kHuge = 20000;
    // every pair of levels of the series, a camera far from every edge of a huge map (no clamp): the world point under the pointer stays under it within half a screen pixel of the new
    // zoom (the origin is rounded to the grid of one screen pixel), and the origin is on the grid
    for (const ViewSize& v : kViews) {
        for (const float from : kEvery) {
            for (const float to : kEvery) {
                if (from == to) continue;
                double worst = 0.0;
                int tested = 0;
                for (int32_t ay = 0; ay < v.h; ay += 31) {
                    for (int32_t ax = 0; ax < v.w; ax += 37) {
                        const double ox = zoom::snap(7000.3, from);
                        const double oy = zoom::snap(6500.9, from);
                        const zoom::Camera r = zoom::zoomed(zoom::Camera{ox, oy, from}, to, ax, ay, v.w, v.h, kHuge, kHuge, true);
                        const double before_x = ox + ax / static_cast<double>(from);
                        const double before_y = oy + ay / static_cast<double>(from);
                        const double after_x = r.x + ax / static_cast<double>(to);
                        const double after_y = r.y + ay / static_cast<double>(to);
                        worst = std::max({worst, std::fabs(after_x - before_x), std::fabs(after_y - before_y)});
                        check(r.x == oracle_snap(r.x, to) && r.y == oracle_snap(r.y, to) && r.zoom == to, std::string(v.name) + " " + num(static_cast<double>(from)) + " -> " + num(static_cast<double>(to)) + ": the origin is on the grid of the new zoom");
                        ++tested;
                    }
                }
                check(tested > 150 && worst <= 0.5 * zoom::grid(to) + 1e-9, std::string(v.name) + " " + num(static_cast<double>(from)) + " -> " + num(static_cast<double>(to)) + ": the point under the pointer moves at most half a screen pixel: " + num(worst) + " world pixels (half a pixel is " +
                                                                              num(0.5 * zoom::grid(to)) + ")");
            }
        }
    }
    // a round trip 1 -> z -> 1 at one pixel returns the camera within the rounding of the two grids: half a lattice pixel of z (the origin of z is rounded to it) and half a world pixel (the origin
    // of 1 is rounded to whole pixels)
    for (const ViewSize& v : kViews) {
        for (const float level : kInBetween) {
            for (int32_t ay = 0; ay < v.h; ay += 53) {
                for (int32_t ax = 0; ax < v.w; ax += 61) {
                    const zoom::Camera one{7500.0, 6300.0, 1.0f};
                    const zoom::Camera there = zoom::zoomed(one, level, ax, ay, v.w, v.h, kHuge, kHuge, true);
                    const zoom::Camera back = zoom::zoomed(there, 1.0f, ax, ay, v.w, v.h, kHuge, kHuge, true);
                    const double bound = 0.5 * zoom::grid(level) + 0.5 + 1e-9;
                    check(std::fabs(back.x - one.x) <= bound && std::fabs(back.y - one.y) <= bound && back.zoom == 1.0f,
                          std::string(v.name) + ": 1 -> " + num(static_cast<double>(level)) + " -> 1 at one pixel returns the camera within " + num(bound) + " world pixels: (" + num(back.x) + ", " + num(back.y) + ")");
                }
            }
        }
    }

    // every map, cameras all over it, every pair of levels, pointer positions of a sample: the result is on its grid, inside the map or centred, whatever the clamps did
    for (const MapSize& m : all_maps()) {
        for (const ViewSize& v : kViews) {
            for (bool centre : {true, false}) {
                for (const float from : levels_for(m, v)) {
                    for (const float to : levels_for(m, v)) {
                        for (int i = 0; i < 24; ++i) {
                            const double ox = oracle_snap(-100.0 + (lcg() % 10000) / 10000.0 * (m.w + 200.0), from);
                            const double oy = oracle_snap(-100.0 + (lcg() % 10000) / 10000.0 * (m.h + 200.0), from);
                            const int32_t ax = static_cast<int32_t>(lcg() % static_cast<uint32_t>(v.w));
                            const int32_t ay = static_cast<int32_t>(lcg() % static_cast<uint32_t>(v.h));
                            const zoom::Camera r = zoom::zoomed(zoom::Camera{ox, oy, from}, to, ax, ay, v.w, v.h, m.w, m.h, centre);
                            const std::string at = m.name + " " + v.name + " " + zoom::level_name(from) + " -> " + zoom::level_name(to) + (centre ? " centred" : " corner") + " from (" + num(ox) + ", " + num(oy) + ") at (" +
                                                   std::to_string(ax) + ", " + std::to_string(ay) + "): ";
                            check(r.zoom == to, at + "the zoom is the new level");
                            const bool exact_to = to == 0.5f || to == 1.0f || to == 2.0f;
                            if (exact_to) {
                                const int64_t x2 = static_cast<int64_t>(std::llround(r.x * 2.0));
                                const int64_t y2 = static_cast<int64_t>(std::llround(r.y * 2.0));
                                check(x2 == oracle_clamp2(x2, to, v.w, m.w, centre) && y2 == oracle_clamp2(y2, to, v.h, m.h, centre),
                                      at + "the origin is inside the map (or centred / at 0 for a map that the view shows whole): (" + num(r.x) + ", " + num(r.y) + ")");
                            } else {
                                check(std::fabs(r.x - oracle_clamp_f(r.x, to, v.w, m.w, centre)) < 1e-9 && std::fabs(r.y - oracle_clamp_f(r.y, to, v.h, m.h, centre)) < 1e-9,
                                      at + "the origin is inside the map (or centred / at 0 for a map that the view shows whole): (" + num(r.x) + ", " + num(r.y) + ")");
                            }
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
    // an anchored zoom to the limit of the map: the map's width fills the view (the origin cannot move along x), and the origin along the other axis stays inside the map
    for (const ViewSize& v : kViews) {
        const MapSize treasure{"TREASURE", 1920, 1920};
        const float limit = zoom::floor_zoom(fit_of(treasure, v));
        int tested = 0;
        for (const float from : kEvery) {
            for (int i = 0; i < 60; ++i) {
                const double ox = zoom::snap(static_cast<double>(lcg() % 1920), from);
                const double oy = zoom::snap(static_cast<double>(lcg() % 1920), from);
                const int32_t ax = static_cast<int32_t>(lcg() % static_cast<uint32_t>(v.w));
                const int32_t ay = static_cast<int32_t>(lcg() % static_cast<uint32_t>(v.h));
                const zoom::Camera r = zoom::zoomed(zoom::Camera{ox, oy, from}, limit, ax, ay, v.w, v.h, 1920, 1920, true);
                const double vis_w = v.w / static_cast<double>(limit);
                const double vis_h = v.h / static_cast<double>(limit);
                check(vis_w <= 1920.0 && vis_h <= 1920.0 && r.x >= 0.0 && r.y >= 0.0 && r.x + vis_w <= 1920.0 + 1e-9 && r.y + vis_h <= 1920.0 + 1e-9,
                      std::string(v.name) + ": at the limit the view shows no more than the map: origin (" + num(r.x) + ", " + num(r.y) + ") from " + num(static_cast<double>(from)));
                check(r.x == 0.0 && (v.w <= v.h || r.y == 0.0 || r.y > 0.0), std::string(v.name) + ": at the limit the origin along the map's whole width is 0");
                ++tested;
            }
        }
        check(tested == 60 * static_cast<int>(kEvery.size()), std::string(v.name) + ": the sweep ran");
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
        for (float z : kExact) {
            ViewportCamera c = camera_of(v, 16, 21, z, true);
            check(c.visible_w() == oracle_visible(v.w, z) && c.visible_h() == oracle_visible(v.h, z), std::string("visible_w / visible_h of the ") + v.name + " view at " + zoom::level_name(z));
        }
        for (const float z : kEvery) {
            ViewportCamera c = camera_of(v, 16, 21, z, true);
            check(c.visible_w() == ref_visible(v.w, z) && c.visible_h() == ref_visible(v.h, z), std::string("visible_w / visible_h of the ") + v.name + " view at " + num(static_cast<double>(z)));
        }
    }

    // the clamp: the origin on its grid, inside [0, map - visible]; at the zoom 1 the camera's own arithmetic (the origin is not rounded)
    for (const MapSize& m : all_maps()) {
        for (const ViewSize& v : kViews) {
            for (const float z : kEvery) {
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
                            if (z == 0.5f || z == 2.0f) {                   // (the exact levels: in whole half pixels)
                                const int64_t x2 = static_cast<int64_t>(std::llround(c.x * 2.0));
                                const int64_t y2 = static_cast<int64_t>(std::llround(c.y * 2.0));
                                const int64_t want_x2 = oracle_clamp2(static_cast<int64_t>(std::llround(oracle_snap(ix, z) * 2.0)), z, v.w, m.w, centre);
                                const int64_t want_y2 = oracle_clamp2(static_cast<int64_t>(std::llround(oracle_snap(iy, z) * 2.0)), z, v.h, m.h, centre);
                                check(x2 == want_x2 && y2 == want_y2, at + "the origin is snapped and held: (" + num(c.x) + ", " + num(c.y) + ")");
                            }
                            check(c.x == static_cast<float>(oracle_clamp_f(oracle_snap(ix, z), z, v.w, m.w, centre)) && c.y == static_cast<float>(oracle_clamp_f(oracle_snap(iy, z), z, v.h, m.h, centre)),
                                  at + "the origin is snapped to the lattice and held (the far end on the lattice too): (" + num(c.x) + ", " + num(c.y) + ")");
                        }
                        check(c.world_x == static_cast<int32_t>(std::floor(c.x)) && c.world_y == static_cast<int32_t>(std::floor(c.y)), at + "world_x / world_y are the whole parts");
                    }
                }
            }
        }
    }

    // center_on puts the point in the middle of the world that the view shows
    for (const ViewSize& v : kViews) {
        for (const float z : levels_for(MapSize{"60 x 60", 1920, 1920}, v)) {
            ViewportCamera c = camera_of(v, 16, 21, z, true);
            c.center_on(960, 960, 60, 60);
            const double vis_w = v.w / static_cast<double>(z);
            const double vis_h = v.h / static_cast<double>(z);
            check(std::fabs(c.x + vis_w / 2.0 - 960.0) <= zoom::grid(z) && std::fabs(c.y + vis_h / 2.0 - 960.0) <= zoom::grid(z),
                  std::string("center_on puts the point in the middle at ") + zoom::level_name(z) + " in the " + v.name + " view: origin (" + num(c.x) + ", " + num(c.y) + ")");
            ViewportCamera corner = camera_of(v, 16, 21, z, true);
            corner.center_on(10, 10, 60, 60);
            check(corner.x == 0.0f && corner.y == 0.0f, std::string("center_on near the corner is held at the corner at ") + zoom::level_name(z));
            ViewportCamera far = camera_of(v, 16, 21, z, true);
            far.center_on(1919, 1919, 60, 60);
            check(far.x == static_cast<float>(oracle_clamp_f(1e9, z, v.w, 1920, true)) && far.y == static_cast<float>(oracle_clamp_f(1e9, z, v.h, 1920, true)) && far.x + vis_w <= 1920.0 + 1e-9 && far.x + vis_w >= 1920.0 - zoom::grid(z) - 1e-6,
                  std::string("center_on at the far corner is held at the far end of the lattice, within a screen pixel of the map's edge at ") + num(static_cast<double>(z)));
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
        for (float z : kExact) {
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
    // ... at the levels between: the world pixel under a screen pixel's left edge is the floor of the exact point, in long doubles; the pixel that is found, drawn at the screen pixel that
    // world_to_screen gives, covers the point (the picture and the pointer agree), and the pair of exact conversions is the identity
    for (const ViewSize& v : kViews) {
        for (const float z : kEvery) {
            ViewportCamera c = camera_of(v, 16, 21, z, true);
            c.set_origin(700.3, 650.8, 125, 125);
            const long double zz = z;
            const long double ox = c.x;
            const long double oy = c.y;
            if (z != 1.0f) check(std::fabs(static_cast<double>(c.x) * static_cast<double>(z) - std::round(static_cast<double>(c.x) * static_cast<double>(z))) < 1e-3, std::string("the origin of the camera is on the lattice at ") + num(static_cast<double>(z)));
            int tested = 0;
            for (int32_t sy = 21; sy < 21 + v.h; sy += 7) {
                for (int32_t sx = 16; sx < 16 + v.w; sx += 11) {
                    const long double px = ox + (sx - 16) / zz;
                    const long double py = oy + (sy - 21) / zz;
                    const bool is_exact_level = z == 0.5f || z == 1.0f || z == 2.0f || z == 0.25f;
                    if (!is_exact_level && (!away_from_whole(px) || !away_from_whole(py))) continue;
                    int32_t wx = -1;
                    int32_t wy = -1;
                    check(c.screen_to_world(sx, sy, wx, wy) && wx == static_cast<int32_t>(std::floor(px)) && wy == static_cast<int32_t>(std::floor(py)),
                          std::string("screen_to_world at ") + num(static_cast<double>(z)) + " (" + std::to_string(sx) + ", " + std::to_string(sy) + ") is the floor of the exact point (" + std::to_string(wx) + ", " + std::to_string(wy) + ")");
                    check(c.world_x_at(sx - 16) == wx && c.world_y_at(sy - 21) == wy, std::string("world_x_at / world_y_at agree with screen_to_world at ") + num(static_cast<double>(z)));
                    int32_t bx = 0;
                    int32_t by = 0;
                    c.world_to_screen(wx, wy, bx, by);
                    // the world pixel's span on the screen [(w - o) z, (w + 1 - o) z) contains the left edge of the pixel (sx - 16): the pixel that was found is under it
                    check((wx - ox) * zz <= (sx - 16) + 1e-6L && (sx - 16) < (wx + 1 - ox) * zz + 1e-6L && (wy - oy) * zz <= (sy - 21) + 1e-6L && (sy - 21) < (wy + 1 - oy) * zz + 1e-6L,
                          std::string("the world pixel found under a screen pixel covers its left edge at ") + num(static_cast<double>(z)));
                    check(sx - 16 - (bx - 16) >= 0 && sx - 16 - (bx - 16) <= static_cast<int32_t>(std::ceil(z)) && sy - 21 - (by - 21) >= 0 && sy - 21 - (by - 21) <= static_cast<int32_t>(std::ceil(z)),
                          std::string("world_to_screen of that pixel is at most ceil(zoom) screen pixels from the one that found it, at ") + num(static_cast<double>(z)));
                    // the exact conversions are inverses of each other
                    const long double back = (px - ox) * zz;
                    check(std::fabs(static_cast<double>(back) - (sx - 16)) < 1e-9, "screen -> world -> screen is the identity for the exact conversions");
                    ++tested;
                }
            }
            check(tested > 1000, std::string("the sweep at ") + num(static_cast<double>(z)) + " has samples: " + std::to_string(tested));
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
        for (const float z : kEvery) {
            ViewportCamera c = camera_of(v, 16, 21, z, true);
            c.set_origin(700.0, 650.0, 125, 125);
            const float x0 = c.x;
            const float y0 = c.y;
            c.scroll_screen(10, -6, 125, 125);
            check(std::fabs((c.x - x0) * z - 10.0f) < 2e-3f && std::fabs((c.y - y0) * z + 6.0f) < 2e-3f,
                  std::string("scroll_screen(10, -6) moves 10 and 6 screen pixels at ") + zoom::level_name(z) + " (" + num(c.x - x0) + ", " + num(c.y - y0) + " world pixels)");
            c.scroll_screen(9000, 9000, 125, 125);
            check(c.x == static_cast<float>(oracle_clamp_f(1e9, z, v.w, 4000, true)) && c.y == static_cast<float>(oracle_clamp_f(1e9, z, v.h, 4000, true)) && c.x + v.w / static_cast<double>(z) <= 4000.0 + 1e-9 &&
                      c.x + v.w / static_cast<double>(z) > 4000.0 - zoom::grid(z) - 1e-6,
                  std::string("a scroll beyond the map stops at the far end of the lattice, within a screen pixel of its edge at ") + num(static_cast<double>(z)));
            c.scroll_screen(-19000, -19000, 125, 125);
            check(c.x == 0.0f && c.y == 0.0f, std::string("... and at the near edge at ") + zoom::level_name(z));
            ViewportCamera w = camera_of(v, 16, 21, z, true);
            w.set_origin(700.0, 650.0, 125, 125);
            const double start_x = w.x;
            const double start_y = w.y;
            w.scroll_pixels(10, -6, 125, 125);
            check(std::fabs(static_cast<double>(w.x) - (start_x + 10.0)) <= 0.5 * zoom::grid(z) + 1e-4 && std::fabs(static_cast<double>(w.y) - (start_y - 6.0)) <= 0.5 * zoom::grid(z) + 1e-4,
                  std::string("scroll_pixels moves world pixels (the original's whole-pixel scroll), to the lattice of the zoom at ") + num(static_cast<double>(z)));
            if (z == 0.5f || z == 1.0f || z == 2.0f) check(w.x == 710.0f && w.y == 644.0f, std::string("... exactly at ") + zoom::level_name(z));
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
        for (const float bad : {0.0f, -1.0f, 0.04f, 2.5f, 3.0f, 100.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            c.set_zoom(bad, 400, 250, 60, 60);
            check(c.zoom == 1.0f && c.x == 700.0f && c.y == 600.0f, "a number outside 0.05 .. 2 changes nothing: " + num(static_cast<double>(bad)));
        }
        {                                                                                                  // (the camera does not know the levels: any zoom of the range, the Application chooses among the levels)
            ViewportCamera free_zoom = c;
            free_zoom.set_zoom(0.75f, 400, 250, 60, 60);
            check(free_zoom.zoom == 0.75f && std::fabs(free_zoom.x - oracle_snap(free_zoom.x, 0.75f)) < 1e-3 && std::fabs(free_zoom.y - oracle_snap(free_zoom.y, 0.75f)) < 1e-3, "a zoom of the range that is not a level of the series is taken, the origin on its lattice");
            free_zoom.set_zoom(0.05f, 0, 0, 60, 60);
            check(free_zoom.zoom == 0.05f, "0.05 is the smallest zoom of the range");
            free_zoom.set_zoom(2.0f, 0, 0, 60, 60);
            check(free_zoom.zoom == 2.0f, "and 2 the largest");
        }
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
            for (const float z : kEvery) {
                int differences = 0;
                int scrolling = 0;
                const int32_t ox_s = static_cast<int32_t>(std::lround(4000.0 * z));
                const int32_t oy_s = static_cast<int32_t>(std::lround(3600.0 * z));
                const int32_t map_s = static_cast<int32_t>(std::lround(16000.0 * z));
                for (int32_t y = 0; y < layout.height; ++y) {
                    for (int32_t x = 0; x < layout.width; ++x) {
                        const EdgeScroll at_one = edge_scroll_step_px(x, y, rate, 4000, 3600, 16000, 16000, layout);
                        const EdgeScroll at_z = edge_scroll_step_px(x, y, rate, ox_s, oy_s, map_s, map_s, layout);
                        if (at_one.dir != at_z.dir || at_one.dx != at_z.dx || at_one.dy != at_z.dy) ++differences;
                        if (at_z.dx != 0 || at_z.dy != 0) ++scrolling;
                    }
                }
                check(differences == 0, std::string("the edge scroll step in screen pixels is the zoom 1 step at ") + zoom::level_name(z) + ", rate " + std::to_string(rate) + ", " + v.name + " picture: " + std::to_string(differences) + " pixels differ");
                check(scrolling > 100, std::string("the strips do scroll (") + std::to_string(scrolling) + " pixels)");
                // in world pixels the camera moves step / zoom: ViewportCamera::scroll_screen
                ViewportCamera cam = camera_of(v, layout.view().x, layout.view().y, z, true);
                cam.set_origin(4000.0, 3600.0, 500, 500);
                const EdgeScroll s = edge_scroll_step_px(layout.width - 3, layout.height / 2, rate, cam.origin_screen_x(), cam.origin_screen_y(), zoom::map_screen(16000, z), zoom::map_screen(16000, z), layout);
                const float before = cam.x;
                cam.scroll_screen(s.dx, s.dy, 500, 500);
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
        for (const float z : levels_for(MapSize{"60 x 60", 1920, 1920}, v)) {
            const LayoutRect mini = layout.minimap();
            int centred = 0;
            int tested = 0;
            for (int32_t py = mini.y; py < mini.bottom(); py += 3) {
                for (int32_t px = mini.x; px < mini.right(); px += 3) {
                    ViewportCamera cam = camera_of(v, layout.view().x, layout.view().y, z, true);
                    cam.set_origin(0.0, 0.0, 60, 60);                                    // from the corner, far from most points
                    for (int round = 0; round < 3; ++round) {
                        const EdgeScroll s = minimap_scroll_step_px(px, py, cam.origin_screen_x(), cam.origin_screen_y(), zoom::map_screen(1920, z), zoom::map_screen(1920, z), layout);
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
                        if (std::fabs(centre_x - want_x) <= 2.0 * zoom::grid(z) + 1e-9) ++centred;
                    }
                    if (free_y) {
                        ++tested;
                        if (std::fabs(centre_y - want_y) <= 2.0 * zoom::grid(z) + 1e-9) ++centred;
                    }
                }
            }
            check(tested > (z < 0.5f ? 40 : 500) && centred == tested, std::string("a press on the minimap centres the world that is seen on the point under the pointer (within two screen pixels: the minimap works in whole ones: the point, the map, the half view and the lattice each round) at ") + zoom::level_name(z) + " in the " + v.name + " picture: " +
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
                    for (const float z : kEvery) {
                        int32_t ox = -1;
                        int32_t oy = -1;
                        start_view_origin(tx, ty, tw, th, ox, oy, layout, z);
                        // the original's rule written out: the fresh view at (0, 0) moves only right / down, just far enough to bring the right / bottom edge of the square (anchor + 192, at most
                        // the map) into the world that the view shows
                        const int32_t want_ox = std::max(0, std::min(tx * 32 + 16 + 192, m.w) - ref_visible(v.w, z));
                        const int32_t want_oy = std::max(0, std::min(ty * 32 + 16 + 192, m.h) - ref_visible(v.h, z));
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
    // a mouse notch worth more than a step (the browser's 120 at the page's 100 a step is 1.2): the nearest whole number of steps, at least one, and nothing left over. The 0.2 of every notch
    // used to be kept, so that the fifth notch of a burst was two levels (1, 1, 1, 1, 2)
    for (const double sign : {1.0, -1.0}) {
        w.reset();
        int ones = 0;
        int total = 0;
        for (uint32_t i = 0; i < 5; ++i) {
            const int s = w.feed(sign * 1.2, 1000u + 80u * i);
            ones += s == static_cast<int>(sign) ? 1 : 0;
            total += s;
        }
        check(ones == 5 && total == 5 * static_cast<int>(sign) && w.pending() == 0.0, "five notches of 1.2 within 400 ms are five steps of one and nothing is left over (not 1, 1, 1, 1, 2)");
        w.reset();
        int run = 0;
        for (uint32_t i = 0; i < 1000; ++i) run += w.feed(sign * 1.2, 5000u + 10u * i);
        check(run == 1000 * static_cast<int>(sign), "a thousand notches of 1.2 are a thousand steps: no drift");
    }
    w.reset();
    check(w.feed(1.6, 100) == 2 && w.feed(-1.6, 110) == -2 && w.feed(2.4, 120) == 2 && w.feed(-2.4, 130) == -2 && w.feed(1.49, 140) == 1 && w.feed(7.6, 150) == 8 && w.feed(1.0, 160) == 1 && w.pending() == 0.0,
          "a notch worth a step or more is the nearest whole number of steps: 1.6 is 2, 2.4 is 2, 1.49 is 1, 1 is 1");
    w.reset();
    check(w.feed(0.4, 100) == 0 && std::fabs(w.pending() - 0.4) < 1e-12 && w.feed(1.2, 110) == 1 && w.pending() == 0.0 && w.feed(0.4, 120) == 0 && w.feed(0.4, 130) == 0 && std::fabs(w.pending() - 0.8) < 1e-12,
          "a notch clears a pending fraction: 0.4, 1.2 is one step with nothing left, and 0.4 + 0.4 after it is not a step");
    w.reset();
    check(w.feed(0.4, 100) == 0 && w.feed(1.0, 110) == 1 && w.pending() == 0.0 && w.feed(0.4, 120) == 0 && w.feed(-1.2, 130) == -1 && w.pending() == 0.0, "a notch of exactly 1 clears it too, and so does a notch the other way: no fraction survives a whole notch");
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
    // a level's name is three significant digits, and reading it back finds the same level (as the nearest level that a map offers)
    for (const float z : kEvery) {
        const std::string name = zoom::level_name(z);
        float out = 99.0f;
        std::string why;
        check(zoom::parse_level(name, out, why) && std::fabs(static_cast<double>(out) / static_cast<double>(z) - 1.0) < 0.005, std::string("level_name -> parse_level round trip: ") + name);
        check(name.size() <= 5, std::string("the name is short: ") + name);
    }
    check(zoom::level_name(0.5f) == "0.5" && zoom::level_name(1.0f) == "1" && zoom::level_name(2.0f) == "2" && zoom::level_name(0.25f) == "0.25", "the names of the exact levels are 0.5, 1, 2 and 0.25");
    check(zoom::level_name(ref_series(3)) == "1.68" && zoom::level_name(ref_series(2)) == "1.41" && zoom::level_name(ref_series(1)) == "1.19" && zoom::level_name(ref_series(-1)) == "0.841" && zoom::level_name(ref_series(-2)) == "0.707" &&
              zoom::level_name(ref_series(-3)) == "0.595" && zoom::level_name(ref_series(-5)) == "0.42" && zoom::level_name(ref_series(-6)) == "0.354" && zoom::level_name(ref_series(-7)) == "0.297" && zoom::level_name(0.396875f) == "0.397",
          "the names of the levels in between: 1.68, 1.41, 1.19, 0.841, 0.707, 0.595, 0.42, 0.354, 0.297 and the limit 0.397");
    // the name of every level of every map in every view reads back as that level (the nearest offered one)
    for (const MapSize& m : all_maps()) {
        for (const ViewSize& v : kViews) {
            const std::vector<float> list = zoom::levels(fit_of(m, v), zoom::Limits::any());
            for (const float l : list) {
                float out = 0.0f;
                std::string why;
                check(zoom::parse_level(zoom::level_name(l), out, why) && zoom::nearest(out, list) == l, m.name + " " + v.name + ": the name of " + num(static_cast<double>(l)) + " reads back as the level: " + zoom::level_name(l));
            }
        }
    }
    for (const char* ok : {"0.5", "0.50", ".5", "1", "1.0", "1.00", "2", "2.0", "2.000", "0.05", "0.050", ".05", "1.41", "0.707", "1.5", "0.75", "1.9999", "0.0500", "02", "0001.5", "1."}) {
        float out = 0.0f;
        std::string why;
        check(zoom::parse_level(ok, out, why) && out >= zoom::kSmallest && out <= zoom::kIn, std::string("\"") + ok + "\" is a zoom");
    }
    {
        float out = 0.0f;
        std::string why;
        check(zoom::parse_level("0.5", out, why) && out == 0.5f && zoom::parse_level("1.41", out, why) && out == 1.41f && zoom::parse_level("2", out, why) && out == 2.0f && zoom::parse_level("0.05", out, why) && out == 0.05f &&
                  zoom::parse_level("1.", out, why) && out == 1.0f,
              "the value is the number as a float");
    }
    for (const char* bad : {"", " ", "0", "0.0", "0.049", "0.0499999", "3", "2.0001", "2.5", "20", "-1", "-0.5", "+1", "1x", "x", "2 ", " 2", "0.5.", "1..0", ".", "1,0", "two", "nan", "inf", "0x1", "1e0", "\t1", "0.5 ", "1/2", "1e-1", "00000000000000000000003"}) {
        float out = 7.0f;
        std::string why;
        check(!zoom::parse_level(bad, out, why) && out == 7.0f && why.find("a zoom from 0.05 to 2") != std::string::npos && why.find(std::string("\"") + bad + "\"") == 0, std::string("\"") + bad + "\" is refused with a reason that names it");
    }
}

// =====================================================================================================================================================
// The plan of the world pass
// =====================================================================================================================================================

void test_plan() {
    group("plan", "the plan of the world pass at every level and origin: the part of the map the target covers, how often it is halved, where the last level lands");
    const std::vector<MapSize> maps = all_maps();
    int plans = 0;
    for (const MapSize& m : maps) {
        for (const ViewSize& v : kViews) {
            const std::vector<float> list = zoom::levels(fit_of(m, v), zoom::Limits::any());
            for (const float z : list) {
                for (int i = 0; i < 40; ++i) {
                    // a camera origin of the lattice of this zoom, anywhere on the map or a little beyond it (a small map is centred: its origin is negative)
                    const double raw_x = -300.0 + (lcg() % 100000) / 100000.0 * (m.w + 600.0);
                    const double raw_y = -300.0 + (lcg() % 100000) / 100000.0 * (m.h + 600.0);
                    ViewportCamera cam = camera_of(v, 16, 21, z, true);
                    cam.x = static_cast<float>(raw_x);
                    cam.y = static_cast<float>(raw_y);
                    cam.clamp_to_bounds(static_cast<uint32_t>(m.w / 32), static_cast<uint32_t>(m.h / 32));
                    const double ox = cam.x;
                    const double oy = cam.y;
                    const zoom::Pass p = zoom::plan(ox, oy, z, v.w, v.h, m.w, m.h);
                    ++plans;
                    const std::string at = m.name + " " + v.name + " zoom " + num(static_cast<double>(z)) + " origin (" + num(ox) + ", " + num(oy) + "): ";
                    const int64_t a = int64_t{1} << p.depth;                    // the halvings group a x a texels
                    const int64_t u = p.align;                                 // the target's alignment: a power of two, a multiple of a, and at least one lattice pixel (1 / z) below 1
                    check(u >= a && u % a == 0 && (u & (u - 1)) == 0 && (z >= 1.0f ? u == 1 : (static_cast<double>(u) * static_cast<double>(z) >= 1.0 - 1e-9 && static_cast<double>(u) * static_cast<double>(z) < 2.0)),
                          at + "the alignment is the smallest power of two that is at least 1 / z (1 from the zoom 1 up) and a multiple of 2^depth: " + std::to_string(u));
                    // the depth and the scale: below 0.5 the target is halved until the last level is between 0.5 and 1 of a screen pixel per texel; 0.5 .. 2 not at all
                    const double s = static_cast<double>(z) * static_cast<double>(a);
                    check(std::fabs(p.scale - s) < 1e-12, at + "the scale is the zoom times 2^depth");
                    if (z >= 0.5f) check(p.depth == 0, at + "from 0.5 up the target is not halved");
                    else check(p.depth >= 1 && s >= 0.5 && s < 1.0 + 1e-12, at + "below 0.5 the last level is 0.5 .. 1 screen pixels per texel: depth " + std::to_string(p.depth) + " scale " + num(s));
                    check(p.depth == (z >= 0.5f ? 0 : static_cast<int>(std::ceil(-std::log2(static_cast<double>(z)) - 1e-9)) - 1), at + "the depth is ceil(-log2 z) - 1 below 0.5");
                    // the target: inside the map, whole multiples of 2^depth, at least a texel of margin where the map has room
                    check(p.x0 >= 0 && p.y0 >= 0 && p.x0 % u == 0 && p.y0 % u == 0 && p.w % u == 0 && p.h % u == 0, at + "the target starts at a multiple of the alignment and has a size that is one");
                    check(p.x0 + p.w <= m.w && p.y0 + p.h <= m.h, at + "the target is inside the map");
                    check(p.w > 0 && p.h > 0, at + "something of the map is in view");
                    check(p.w <= p.cap_w && p.h <= p.cap_h && p.cap_w <= m.w && p.cap_h <= m.h, at + "the target fits the size that is made for this zoom: " + std::to_string(p.w) + " x " + std::to_string(p.h) + " in " + std::to_string(p.cap_w) + " x " + std::to_string(p.cap_h));
                    const double vis_w = v.w / static_cast<double>(z);
                    const double vis_h = v.h / static_cast<double>(z);
                    check(p.cap_w <= static_cast<int64_t>(std::ceil(vis_w / static_cast<double>(u))) * u + 4 * u && p.cap_h <= static_cast<int64_t>(std::ceil(vis_h / static_cast<double>(u))) * u + 4 * u, at + "the capacity is the world seen and four alignments");
                    check(p.cap_w % u == 0 && p.cap_h % u == 0, at + "the capacity is a multiple of the alignment (and of 2^depth): every halving is exact");
                    // it covers the world that the view shows (the part inside the map), with a texel of the last level of margin on each side where the map has room
                    const double want_x0 = std::max(0.0, std::floor(ox / static_cast<double>(u)) * static_cast<double>(u) - static_cast<double>(u));
                    check(p.x0 <= std::floor(std::max(0.0, ox)) && (p.x0 == 0 || p.x0 <= ox - static_cast<double>(a) + 1e-9) && std::fabs(p.x0 - want_x0) < 1e-9, at + "the target starts at least a texel before the origin, or at the map's edge");
                    const double view_right = ox + vis_w;
                    const double view_bottom = oy + vis_h;
                    check((p.x0 + p.w >= std::min<double>(m.w, std::ceil(view_right)) && (p.x0 + p.w >= view_right + static_cast<double>(a) - 1e-9 || p.x0 + p.w == (m.w / u) * u)) &&
                              (p.y0 + p.h >= std::min<double>(m.h, std::ceil(view_bottom)) && (p.y0 + p.h >= view_bottom + static_cast<double>(a) - 1e-9 || p.y0 + p.h == (m.h / u) * u)),
                          at + "the target ends at least a texel after the world that is seen, or at the map's edge");
                    // where the last level lands on the screen: the texel at x0 is at (x0 - ox) screen pixels from the origin at the zoom, and the picture covers the view
                    check(std::fabs(p.left - (p.x0 - ox) * static_cast<double>(z)) < 1e-9 && std::fabs(p.top - (p.y0 - oy) * static_cast<double>(z)) < 1e-9, at + "the last level lands at (x0 - ox) * z");
                    const double right_edge = p.left + (p.w / static_cast<double>(a)) * p.scale;      // = left + w * z
                    const double bottom_edge = p.top + (p.h / static_cast<double>(a)) * p.scale;
                    check(std::fabs(right_edge - (p.left + p.w * static_cast<double>(z))) < 1e-9, at + "the last level is w * z screen pixels wide");
                    const bool covers_x = (p.left <= 1e-9 || p.x0 == 0) && (right_edge >= v.w - 1e-6 || p.x0 + p.w == (m.w / u) * u);
                    const bool covers_y = (p.top <= 1e-9 || p.y0 == 0) && (bottom_edge >= v.h - 1e-6 || p.y0 + p.h == (m.h / u) * u);
                    check(covers_x && covers_y, at + "the picture covers the view, or ends where the map does");
                    // a map that is covered by the view on an axis: its edge is inside the view there (black around it), and where the map is larger the view is all map
                    if (m.w > vis_w + 1e-9 && ox >= 0.0 && ox + vis_w <= m.w + 1e-9) check(p.left <= 1e-9 && right_edge >= v.w - 1e-6, at + "a map larger than the view is drawn over the whole width of the view");
                    // at the zooms that are powers of two the picture lands on whole screen pixels (the origin is a multiple of 1 / z and so is the target's start): the pictures are exact
                    if (z == 0.5f || z == 0.25f || z == 0.125f || z == 2.0f) {            // (the zoom 1 of the direct path has any float origin in a test; the lattice is the whole pixel there)
                        const double lx = p.left;
                        const double ly = p.top;
                        check(lx == std::floor(lx) && ly == std::floor(ly), at + "at a power of two the last level lands on whole screen pixels: (" + num(lx) + ", " + num(ly) + ")");
                    }
                    // the filter: nearest at 1 and 2 (exact), linear elsewhere
                    check(p.smooth == (z != 1.0f && z != 2.0f), at + "the filter: linear except at 1 and 2");
                    if (z > 1.0f && z < 2.0f) check(!zoom::plan(ox, oy, z, v.w, v.h, m.w, m.h, false).smooth && zoom::plan(ox, oy, z, v.w, v.h, m.w, m.h, true).smooth, at + "between 1 and 2 the filter is the choice that is given");
                    if (z < 1.0f) check(zoom::plan(ox, oy, z, v.w, v.h, m.w, m.h, false).smooth, at + "below 1 it is linear whatever the choice for the zoom-in says");
                    // the lattice: moving the origin by whole screen pixels (1 / z world pixels) moves the picture by those whole pixels and nothing else: the world pixel X lands at (X - ox) * z
                    const double ox2 = ox + 3.0 * zoom::grid(z);
                    const zoom::Pass q = zoom::plan(ox2, oy, z, v.w, v.h, m.w, m.h);
                    for (const int32_t X : {p.x0, p.x0 + static_cast<int32_t>(a) * 7, p.x0 + static_cast<int32_t>(a) * 100}) {
                        if (X < q.x0 || X >= q.x0 + q.w) continue;
                        const double land_p = p.left + (X - p.x0) * static_cast<double>(z);
                        const double land_q = q.left + (X - q.x0) * static_cast<double>(z);
                        check(std::fabs((land_p - land_q) - 3.0) < 1e-8, at + "an origin 3 lattice pixels further moves every world pixel by exactly 3 screen pixels");
                    }
                }
            }
        }
    }
    check(plans > 4000, "the plan was checked " + std::to_string(plans) + " times");
    // worked examples
    {
        const zoom::Pass p = zoom::plan(500.0, 300.0, 1.0f, 762, 500, 1920, 1920);                  // (the zoom 1 forced through the target: whole pixels everywhere)
        check(p.depth == 0 && p.x0 == 499 && p.y0 == 299 && p.left == -1.0 && p.top == -1.0 && p.scale == 1.0 && !p.smooth && p.w == 764 && p.h == 502, "the zoom 1 from (500, 300): the target starts a pixel before, 764 x 502, lands at (-1, -1)");
        const zoom::Pass h = zoom::plan(500.0, 300.0, 0.5f, 762, 500, 1920, 1920);
        check(h.depth == 0 && h.align == 2 && h.x0 == 498 && h.y0 == 298 && h.w == 1422 && h.h == 1004 && h.scale == 0.5 && h.left == -1.0 && h.top == -1.0 && h.smooth,
              "the zoom 0.5 from (500, 300): the target starts two world pixels (a lattice pixel) before, to the map's edge in x, 1422 x 1004, 0.5 screen pixels per texel, landing on a whole pixel (-1, -1)");
        const zoom::Pass q = zoom::plan(500.0, 300.0, 0.25f, 442, 440, 1920, 1920);
        check(q.depth == 1 && q.align == 4 && q.x0 == 496 && q.y0 == 296 && q.w == 1424 && q.h == 1624 && q.scale == 0.5 && q.left == -1.0 && q.top == -1.0 && q.smooth,
              "the zoom 0.25 is one halving, then 0.5: the target starts 4 world pixels (a lattice pixel) before, and lands on a whole pixel (-1, -1)");
        const zoom::Pass g = zoom::plan(500.0 + 0.0, 300.0, 0.5946036f, 762, 500, 1920, 1920);
        check(g.depth == 0 && g.align == 2 && g.scale == static_cast<double>(0.5946036f) && g.smooth && g.left < 0.0 && g.left > -4.0, "the zoom 0.59: no halving, aligned at 2, linear, a margin before the view");
        const zoom::Pass d = zoom::plan(500.0, 300.0, 2.0f, 762, 500, 1920, 1920);
        check(d.depth == 0 && d.x0 == 499 && d.scale == 2.0 && !d.smooth && d.left == -2.0 && d.top == -2.0, "the zoom 2: nearest, the target a pixel before the origin (two screen pixels)");
        const zoom::Pass half = zoom::plan(500.5, 300.0, 2.0f, 762, 500, 1920, 1920);
        check(half.x0 == 499 && half.left == -3.0, "from a half-pixel origin at 2 the picture starts 3 screen pixels before the view");
        const zoom::Pass corner = zoom::plan(0.0, 0.0, 1.0f, 762, 500, 1920, 1920);
        check(corner.x0 == 0 && corner.y0 == 0 && corner.left == 0.0 && corner.top == 0.0, "at the corner of the map nothing is before it: the target starts at 0, at the view's corner");
        const zoom::Pass small = zoom::plan(-125.0, 0.0, 1.0f, 762, 500, 512, 512);
        check(small.x0 == 0 && small.w == 512 && small.left == 125.0 && small.y0 == 0, "a 512 map centred in the wide view at x = -125: the target is the whole map, landing 125 pixels into the view");
        const zoom::Pass lim = zoom::plan(0.0, 330.08, 0.396875f, 762, 500, 1920, 1920);
        check(lim.depth == 1 && lim.align == 4 && lim.x0 == 0 && lim.w == 1920 && lim.cap_w == 1920 && std::fabs(lim.scale - 0.79375) < 1e-6 && lim.left == 0.0, "TREASURE at its limit, wide: one halving, the whole width, 0.79 screen pixels per texel");
        const zoom::Pass empty = zoom::plan(5000.0, 5000.0, 1.0f, 762, 500, 1920, 1920);
        check(empty.w == 0 || empty.h == 0, "an origin far beyond the map: nothing of it is in view (the renderer then draws the zoom 1 picture)");
    }
    // a map whose size is not a multiple of the alignment (the maps of the game are whole 32 pixel tiles, but the plan does not rely on that): the target still ends at the last multiple of the
    // alignment inside the map, so that every halving is exact
    {
        const MapSize odd{"odd", 1001, 777};
        for (const float z : {0.5946036f, 0.5f, 0.3535534f, 0.25f, 0.2302f}) {
            const zoom::Pass p = zoom::plan(0.0, 0.0, z, 762, 500, odd.w, odd.h);
            const int64_t u = p.align;
            check(u >= 2 && p.x0 == 0 && p.y0 == 0 && p.w == (odd.w / u) * u && p.h == (odd.h / u) * u && p.cap_w == p.w && p.cap_h == p.h,
                  "a map of 1001 x 777 at " + num(static_cast<double>(z)) + ": the target is the map less the part after its last multiple of " + std::to_string(u) + ": " + std::to_string(p.w) + " x " + std::to_string(p.h));
        }
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
    test_plan();
    std::printf("\nzoom model: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
