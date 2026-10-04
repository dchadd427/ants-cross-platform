// The zoom of the map view in the renderer, the HUD and the application (milestone M4 of the widescreen work; many levels since the batch after v0.2.0). The model (the levels, the limit, anchoring,
// the camera, the edge scroll in screen pixels, the plan of the world pass) is test_zoom_model; this program is what is drawn and what is done with it:
//   * RENDERER: the world pass at a zoom is checked against the direct pass, which draws what it always drew. At the zoom 1 the offscreen target (forced through a test hook) gives the
//     direct picture pixel for pixel; at 2 the view is the nearest-neighbour 2 x 2 enlargement of the direct picture of the same origin (and, from a half-pixel origin, the same shifted by
//     one screen pixel); at 0.5 it is the 2 x 2 average of the direct picture of a view twice as large (0.25: the 4 x 4 average). Group "levels": EVERY level of a map (the series 2^(k/4)
//     from 2 to the map's limit) against the picture that the design specifies, built from the zoom 1 picture of the whole map: the exact levels pixel for pixel, the others as the smooth
//     picture (the software renderer is a pixel off at most), never a gap in the view, a scroll that only moves the picture, frames of the levels in any order. Group "place": where the
//     software renderer puts the picture of a level (the smallest whole-pixel rectangle that holds the exact one), and that the pass paints the margins around a small map itself. Group "fog":
//     with Fog of War on every level draws what the zoom 1 draws (the fog over every unexplored tile, nothing that it hides), at every level of GAUNTLET and TINY. Ants of
//     every type, effects, score bubbles, selection markers, the click marker, the fog of war, the hill's brackets, and the small maps (centred, black around them) all pass through it. The
//     hit point digits are one size at every zoom; nothing is drawn outside the view.
//   * HUD: the cursor, the clicks and orders (also at the first and the last pixel of the view), the rubber band, the minimap and the edge scroll follow the zoom, each against an
//     independent computation; a dialog, a captured press and the panels do not zoom.
//   * APPLICATION: the wheel (up zooms in, down out, one level a notch, natural scrolling, the precise deltas, over the map only, not during a drag or with a dialog open), the middle
//     button, the anchoring at the pointer, the same levels in a local game and in a match of the network (no limit of the kind of match), the settings key, --zoom, the start view and the
//     sound listener at every zoom, a network match in which the two machines have different zooms and stay identical, and the setup screen after a zoomed match (the map preview is the
//     zoom 1 preview, the wheel does nothing there).
// Usage: test_zoom_view. Exit code 0 when every check passes.
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

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        if (g_failures <= 80) std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

[[maybe_unused]] std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

void test_world_pass(const assets::AssetArchive& arc) {
    group("pass", "the world pass through the offscreen target against the direct pass");
    for (const Shape& s : kShapes) {
        for (const char* map : {"GAUNTLET", "TINY"}) {
            PixelRig rig(arc, map, s.cw, s.ch);
            check(rig.ok, std::string("the rig for ") + map + " in the " + s.name + " picture is up");
            if (!rig.ok) continue;
            const LayoutRect view = rig.layout.view();
            const int32_t map_w = static_cast<int32_t>(rig.tiles_w) * 32;
            const int32_t map_h = static_cast<int32_t>(rig.tiles_h) * 32;
            const std::string where = std::string(map) + " " + s.name + ": ";
            // a world with everything in it
            const int32_t ox = std::max(0, std::min(300, map_w - view.w));
            const int32_t oy = std::max(0, std::min(260, map_h - view.h));
            populate(rig.world, arc, ox, oy, view.w, view.h, 160, 4242u);
            add_effects(rig.world, ox, oy, view.w, view.h);
            rig.renderer.spawn_transient_effect("xmarks", ox + 200, oy + 150);
            std::vector<uint32_t> ids;
            for (uint32_t i = 0; i < 160; i += 5) ids.push_back(5000u + i);

            // 1. zoom 1: the target gives the direct picture pixel for pixel (a forced pass, and one with fog of war, hit point digits, the hill's brackets)
            for (const bool fog : {false, true}) {
                for (const bool hp : {false, true}) {
                    if (fog) reveal_some(rig.world);
                    else rig.world.fog_of_war_enabled = false;
                    rig.look(ox, oy);
                    rig.renderer.set_force_world_target(false);
                    const uint64_t before = rig.renderer.world_target_passes();
                    const Picture direct = rig.shoot(static_cast<int32_t>(ids.front()), ids, 0, hp);
                    const uint64_t after_direct = rig.renderer.world_target_passes();
                    rig.renderer.set_force_world_target(true);
                    const Picture target = rig.shoot(static_cast<int32_t>(ids.front()), ids, 0, hp);
                    const uint64_t after_target = rig.renderer.world_target_passes();
                    rig.renderer.set_force_world_target(false);
                    {
                        // (the scene has ants and they are drawn: the pictures that follow compare something)
                        const auto kept = rig.world.ants;
                        rig.world.ants.clear();
                        const Picture empty = rig.shoot(static_cast<int32_t>(ids.front()), ids, 0, hp);
                        rig.world.ants = kept;
                        check(differ(direct, empty, view) > 1000, where + "the ants of the scene are drawn at the zoom 1: " + std::to_string(differ(direct, empty, view)) + " pixels differ from the view without them");
                    }
                    check(after_direct == before, where + "the world pass at the zoom 1 does not use the offscreen target (it draws straight into the view)");
                    check(after_target == after_direct + 1, where + "the forced pass went through the offscreen target once: " + std::to_string(after_target - after_direct) + " passes");
                    check(differ(direct, target, view) == 0, where + "at the zoom 1 the offscreen target gives the direct picture, pixel for pixel" + (fog ? " (fog)" : "") + (hp ? " (hit points)" : ""));
                    check(outside_not_black(target, view) == 0, where + "nothing is drawn outside the view through the target");
                    check(differ(direct, target, LayoutRect{0, 0, direct.w, direct.h}) == 0, where + "the whole canvas is the same");
                }
            }
            rig.world.fog_of_war_enabled = false;
            check(rig.renderer.zoomed() == false, where + "the renderer at the zoom 1 is not zoomed");

            // 2. zoom 2: the view is the 2 x 2 enlargement of the direct picture of the same origin; from a half-pixel origin it is shifted by one screen pixel
            for (const bool fog : {false, true}) {
                if (fog) reveal_some(rig.world);
                else rig.world.fog_of_war_enabled = false;
                for (const auto& phase : {std::pair<bool, bool>{false, false}, std::pair<bool, bool>{true, false}, std::pair<bool, bool>{false, true}, std::pair<bool, bool>{true, true}}) {
                    rig.look(ox, oy, 1.0f);
                    const Picture direct = rig.shoot(static_cast<int32_t>(ids.front()), ids, 0, false);
                    rig.look(ox + (phase.first ? 0.5 : 0.0), oy + (phase.second ? 0.5 : 0.0), 2.0f);
                    check(rig.cam().x == static_cast<float>(ox + (phase.first ? 0.5 : 0.0)) && rig.cam().y == static_cast<float>(oy + (phase.second ? 0.5 : 0.0)), where + "the camera took the half-pixel origin");
                    const uint64_t passes = rig.renderer.world_target_passes();
                    const Picture zoomed = rig.shoot(static_cast<int32_t>(ids.front()), ids, 0, false);
                    check(rig.renderer.world_target_passes() == passes + 1, where + "the zoom 2 world went through the offscreen target once");
                    check(rig.renderer.zoomed(), where + "the renderer is zoomed at 2");
                    const Picture want = enlarge2(direct, view, phase.first, phase.second);
                    const int bad = differ(zoomed, want, view);
                    check(bad == 0, where + "at the zoom 2 the view is the nearest 2 x 2 enlargement of the zoom 1 picture" + (phase.first ? ", half a pixel right" : "") + (phase.second ? ", half a pixel down" : "") + (fog ? " (fog)" : "") +
                                        ": " + std::to_string(bad) + " pixels differ");
                    check(outside_not_black(zoomed, view) == 0, where + "nothing is drawn outside the view at the zoom 2");
                }
            }
            rig.world.fog_of_war_enabled = false;

            // 3. a renderer that cannot make the offscreen target draws the zoom 1 picture of the same origin (not a broken one) and keeps the camera's zoom for the next frame
            for (const float z : {0.5f, 2.0f}) {
                rig.look(ox, oy, z);
                rig.cam().zoom = 1.0f;                       // (the zoom 1 picture of the origin that the zoom has put the camera at: only the zoom field is changed)
                const Picture direct = rig.shoot(static_cast<int32_t>(ids.front()), ids, 0, false);
                rig.cam().zoom = z;
                rig.renderer.set_fail_world_target(true);
                const uint64_t passes = rig.renderer.world_target_passes();
                const Picture fallback = rig.shoot(static_cast<int32_t>(ids.front()), ids, 0, false);
                rig.renderer.set_fail_world_target(false);
                check(rig.renderer.world_target_passes() == passes, where + std::string("no target, no pass at ") + zoom::level_name(z));
                check(differ(direct, fallback, view) == 0 && outside_not_black(fallback, view) == 0, where + std::string("without a target the zoom ") + zoom::level_name(z) + " frame is the zoom 1 picture of the same origin");
                check(rig.cam().zoom == z, where + "and the camera keeps its zoom for the next frame");
                const Picture again = rig.shoot(static_cast<int32_t>(ids.front()), ids, 0, false);
                check(rig.renderer.world_target_passes() == passes + 1 && differ(again, fallback, view) > 0, where + std::string("the next frame (the target can be made again) is the zoom ") + zoom::level_name(z) + " picture");
            }
        }
    }
}

/// The same scene in two rigs: ants, effects, a click marker, selected ants; the world window is (x0, y0, w, h)
void fill_scene(PixelRig& rig, const assets::AssetArchive& arc, int32_t x0, int32_t y0, int32_t w, int32_t h, std::vector<uint32_t>& ids) {
    populate(rig.world, arc, x0, y0, w, h, 200, 777u);
    add_effects(rig.world, x0, y0, w, h);
    rig.renderer.spawn_transient_effect("xmarks", x0 + 200, y0 + 150);
    ids.clear();
    for (uint32_t i = 0; i < 200; i += 5) ids.push_back(5000u + i);
}

/// the largest difference of a channel and the number of pixels that differ, between two pictures inside the view
struct Deviation {
    int max{0};
    int count{0};
    double mean{0.0};
};
Deviation deviation(const Picture& a, const Picture& b, const LayoutRect& view) {
    Deviation d;
    long total = 0;
    long n = 0;
    for (int32_t y = view.y; y < view.bottom(); ++y) {
        for (int32_t x = view.x; x < view.right(); ++x) {
            int worst = 0;
            for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(a.at(x, y)[c] - b.at(x, y)[c]));
            d.max = std::max(d.max, worst);
            if (worst > 0) ++d.count;
            total += worst;
            ++n;
        }
    }
    d.mean = n > 0 ? static_cast<double>(total) / static_cast<double>(n) : 0.0;
    return d;
}

void test_zoom_out_pass(const assets::AssetArchive& arc) {
    group("out", "the zoom 0.5: the view is the 2 x 2 average of the direct picture of a view twice as large");
    for (const Shape& s : kShapes) {
        const LayoutRect view = ScreenLayout::with_size(s.cw, s.ch).view();
        const Shape big_shape = shape_for_view(2 * view.w, 2 * view.h);
        for (const char* map : {"GAUNTLET", "TINY"}) {
            PixelRig small(arc, map, s.cw, s.ch);
            PixelRig big(arc, map, big_shape.cw, big_shape.ch);
            check(small.ok && big.ok, std::string("the rigs for ") + map + " in the " + s.name + " picture are up");
            if (!small.ok || !big.ok) continue;
            const LayoutRect big_view = big.layout.view();
            check(big_view.w == 2 * view.w && big_view.h == 2 * view.h, "the big layout's view is twice the view");
            const int32_t ox = 60;
            const int32_t oy = 40;
            std::vector<uint32_t> ids;
            std::vector<uint32_t> ids2;
            fill_scene(small, arc, ox, oy, 2 * view.w, 2 * view.h, ids);
            fill_scene(big, arc, ox, oy, 2 * view.w, 2 * view.h, ids2);
            for (const bool fog : {false, true}) {
                if (fog) {
                    reveal_some(small.world);
                    reveal_some(big.world);
                } else {
                    small.world.fog_of_war_enabled = false;
                    big.world.fog_of_war_enabled = false;
                }
                big.look(ox, oy, 1.0f);
                const Picture direct = big.shoot(static_cast<int32_t>(ids.front()), ids, 0, false);
                small.look(ox, oy, 0.5f);
                const Picture zoomed = small.shoot(static_cast<int32_t>(ids.front()), ids, 0, false);
                const Picture want = average2(direct, big_view, zoomed.w, zoomed.h, view);
                const Deviation d = deviation(zoomed, want, view);
                const std::string where = std::string(map) + " " + s.name + (fog ? " (fog)" : "") + ": ";
                check(d.max <= 1, where + "the view at the zoom 0.5 is the 2 x 2 average of the zoom 1 picture of a view twice as large, to the rounding of one level: max deviation " + std::to_string(d.max));
                // it is smoothed, not thinned out: the nearest pixel of every 2 x 2 block (what the nearest filter gives) is far from it in many places
                Picture thinned = want;
                for (int32_t y = view.y; y < view.bottom(); ++y) {
                    for (int32_t x = view.x; x < view.right(); ++x) std::memcpy(&thinned.px[(static_cast<size_t>(y) * static_cast<size_t>(thinned.w) + static_cast<size_t>(x)) * 4u], direct.at(big_view.x + 2 * (x - view.x), big_view.y + 2 * (y - view.y)), 4);
                }
                check(differ(zoomed, thinned, view, 8) > 2000, where + "the zoom 0.5 is smoothed: the thinned-out picture (every other pixel) differs from it in many places");
                check(outside_not_black(zoomed, view) == 0, where + "nothing is drawn outside the view at the zoom 0.5");
                if (std::string(map) == "TINY" && s.cw == kWideW) {
                    // a map smaller than the world that the view shows (992 px in 1524 x 1000): centred, black around it
                    check(small.cam().x == -266.0f && small.cam().y == -4.0f, where + "the 992 x 992 map is centred in the 1524 x 1000 world of the zoom 0.5: origin (-266, -4)");
                    bool left_black = true;
                    bool right_black = true;
                    bool middle_lit = false;
                    for (int32_t y = view.y; y < view.bottom(); ++y) {
                        for (int32_t x = view.x; x < view.right(); ++x) {
                            const uint8_t* p = zoomed.at(x, y);
                            const bool black = p[0] == 0 && p[1] == 0 && p[2] == 0;
                            if (x - view.x < 133 && !black) left_black = false;
                            if (x - view.x >= 133 + 496 && !black) right_black = false;
                            if (x - view.x >= 133 && x - view.x < 133 + 496 && y - view.y >= 2 && y - view.y < 2 + 496 && !black) middle_lit = true;
                        }
                    }
                    check(left_black && right_black && middle_lit, where + "the margins at the sides of the centred map are black and the map is drawn in between");
                }
            }
        }
    }
}


// =====================================================================================================================================================
// Renderer: the picture at every level
// =====================================================================================================================================================

// The pictures of the levels are checked against the picture that the design specifies, built here from the zoom 1 picture of the same world with plain arithmetic and none of the renderer's:
// the direct picture is halved as often as the level needs (every halving the exact average of 2 x 2 pixels, from a corner that is a multiple of the block) and sampled bilinearly at the world
// point under the centre of every screen pixel. The software renderer (the one of these tests) puts the last copy on whole pixels, so a level between the exact ones is checked as a picture: its
// error against the specified one is small, a whole pixel off or the nearest pixel instead of the smooth one is worse; the levels 2, 1, 0.5 and 0.25 are exact (to SDL's rounding of an average).
struct Img {
    int32_t w{0};
    int32_t h{0};
    std::vector<float> c;                                        // r, g, b of every pixel
    float at(int32_t x, int32_t y, int ch) const {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return c[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3u + static_cast<size_t>(ch)];
    }
};

Img img_of(const Picture& p, const LayoutRect& v) {
    Img out;
    out.w = v.w;
    out.h = v.h;
    out.c.resize(static_cast<size_t>(v.w) * static_cast<size_t>(v.h) * 3u);
    for (int32_t y = 0; y < v.h; ++y) {
        for (int32_t x = 0; x < v.w; ++x) {
            for (int ch = 0; ch < 3; ++ch) out.c[(static_cast<size_t>(y) * static_cast<size_t>(v.w) + static_cast<size_t>(x)) * 3u + static_cast<size_t>(ch)] = p.at(v.x + x, v.y + y)[ch];
        }
    }
    return out;
}

/// The exact average of 2 x 2 pixels
Img halved(const Img& a) {
    Img out;
    out.w = a.w / 2;
    out.h = a.h / 2;
    out.c.resize(static_cast<size_t>(out.w) * static_cast<size_t>(out.h) * 3u);
    for (int32_t y = 0; y < out.h; ++y) {
        for (int32_t x = 0; x < out.w; ++x) {
            for (int ch = 0; ch < 3; ++ch) {
                out.c[(static_cast<size_t>(y) * static_cast<size_t>(out.w) + static_cast<size_t>(x)) * 3u + static_cast<size_t>(ch)] =
                    (a.at(2 * x, 2 * y, ch) + a.at(2 * x + 1, 2 * y, ch) + a.at(2 * x, 2 * y + 1, ch) + a.at(2 * x + 1, 2 * y + 1, ch)) * 0.25f;
            }
        }
    }
    return out;
}

/// The bilinear sample at the position (tx, ty) of the image (pixel k covers [k, k + 1), its centre is k + 0.5; the edge pixels are repeated outside)
double sampled(const Img& a, double tx, double ty, int ch) {
    const double u = tx - 0.5;
    const double v = ty - 0.5;
    const int32_t i = static_cast<int32_t>(std::floor(u));
    const int32_t j = static_cast<int32_t>(std::floor(v));
    const double fx = u - static_cast<double>(i);
    const double fy = v - static_cast<double>(j);
    const double top = static_cast<double>(a.at(i, j, ch)) * (1.0 - fx) + static_cast<double>(a.at(i + 1, j, ch)) * fx;
    const double bottom = static_cast<double>(a.at(i, j + 1, ch)) * (1.0 - fx) + static_cast<double>(a.at(i + 1, j + 1, ch)) * fx;
    return top * (1.0 - fy) + bottom * fy;
}

/// The specified picture of a level from the direct picture of the whole map (its pixel k is the world pixel k): the direct picture halved `depth` times, as the level needs it
struct Spec {
    std::vector<Img> by_depth;
    int depth{0};
    double value(double wx, double wy, int ch, bool point) const {
        if (point) return static_cast<double>(by_depth[0].at(static_cast<int32_t>(std::floor(wx)), static_cast<int32_t>(std::floor(wy)), ch));
        const double t = static_cast<double>(1 << depth);
        return sampled(by_depth[static_cast<size_t>(depth)], wx / t, wy / t, ch);
    }
};

Spec spec_for(const Img& direct, float z) {
    Spec s;
    double scale = static_cast<double>(z);
    while (scale < 0.5 && s.depth < 8) {
        scale *= 2.0;
        ++s.depth;
    }
    s.by_depth.push_back(direct);
    for (int k = 1; k <= s.depth; ++k) s.by_depth.push_back(halved(s.by_depth.back()));
    return s;
}

/// The mean error of a picture's view against the specified picture of the camera (cx, cy) at the zoom z, with the specified picture moved by (sx, sy) screen pixels (every third pixel is
/// looked at); `point`: against the nearest pixel of the direct picture instead (what a nearest enlargement, or a thinning out, shows)
double picture_error(const Picture& p, const LayoutRect& view, const Spec& spec, double cx, double cy, float z, double sx, double sy, bool point) {
    double total = 0.0;
    long n = 0;
    for (int32_t j = 4; j < view.h - 4; j += 3) {
        for (int32_t i = 4; i < view.w - 4; i += 3) {
            const double wx = cx + (static_cast<double>(i) + 0.5 + sx) / static_cast<double>(z);
            const double wy = cy + (static_cast<double>(j) + 0.5 + sy) / static_cast<double>(z);
            for (int ch = 0; ch < 3; ++ch) {
                total += std::fabs(spec.value(wx, wy, ch, point) - static_cast<double>(p.at(view.x + i, view.y + j)[ch]));
                ++n;
            }
        }
    }
    return total / static_cast<double>(n);
}

/// The smallest mean error over the shifts of the specified picture by whole and half screen pixels up to `reach` (the software renderer puts the last copy on whole pixels: its picture may
/// be up to a pixel off), and the shift that gives it
struct BestFit {
    double error{1.0e9};
    double sx{0.0};
    double sy{0.0};
};
BestFit best_fit(const Picture& p, const LayoutRect& view, const Spec& spec, double cx, double cy, float z, bool point, double reach) {
    BestFit best;
    for (double sy = -reach; sy <= reach + 1e-9; sy += 0.5) {
        for (double sx = -reach; sx <= reach + 1e-9; sx += 0.5) {
            const double e = picture_error(p, view, spec, cx, cy, z, sx, sy, point);
            if (e < best.error) best = BestFit{e, sx, sy};
        }
    }
    return best;
}

/// The largest error of any pixel of the view (the exact levels)
double picture_max_error(const Picture& p, const LayoutRect& view, const Spec& spec, double cx, double cy, float z, bool point) {
    double worst = 0.0;
    for (int32_t j = 0; j < view.h; ++j) {
        for (int32_t i = 0; i < view.w; ++i) {
            const double wx = cx + (static_cast<double>(i) + 0.5) / static_cast<double>(z);
            const double wy = cy + (static_cast<double>(j) + 0.5) / static_cast<double>(z);
            for (int ch = 0; ch < 3; ++ch) worst = std::max(worst, std::fabs(spec.value(wx, wy, ch, point) - static_cast<double>(p.at(view.x + i, view.y + j)[ch])));
        }
    }
    return worst;
}

/// Is there a row or a column of the view that is entirely black (a gap that the map does not fill)?
int black_lines(const Picture& p, const LayoutRect& view) {
    int lines = 0;
    for (int32_t y = view.y; y < view.bottom(); ++y) {
        bool black = true;
        for (int32_t x = view.x; x < view.right() && black; ++x) black = p.at(x, y)[0] == 0 && p.at(x, y)[1] == 0 && p.at(x, y)[2] == 0;
        if (black) ++lines;
    }
    for (int32_t x = view.x; x < view.right(); ++x) {
        bool black = true;
        for (int32_t y = view.y; y < view.bottom() && black; ++y) black = p.at(x, y)[0] == 0 && p.at(x, y)[1] == 0 && p.at(x, y)[2] == 0;
        if (black) ++lines;
    }
    return lines;
}

bool is_power_of_two_level(float z) {
    const double l = std::log2(static_cast<double>(z));
    return std::fabs(l - std::round(l)) < 1e-9;
}

void test_level_pictures(const assets::AssetArchive& arc) {
    group("levels", "the picture at every level of a map: the exact levels (2, 1, 0.5, 0.25) pixel for pixel, the others as the smooth picture of the zoom 1 world; never a gap; a scroll only moves it");
    for (const Shape& s : kShapes) {
        const LayoutRect view = ScreenLayout::with_size(s.cw, s.ch).view();
        for (const char* map : {"GAUNTLET", "TINY"}) {
            PixelRig rig(arc, map, s.cw, s.ch);
            check(rig.ok, std::string("the rig for ") + map + " in the " + s.name + " picture is up");
            if (!rig.ok) continue;
            const int32_t map_w = static_cast<int32_t>(rig.tiles_w) * 32;
            const int32_t map_h = static_cast<int32_t>(rig.tiles_h) * 32;
            const std::string where = std::string(map) + " " + s.name + ": ";
            zoom::Fit fit;
            fit.view_w = view.w;
            fit.view_h = view.h;
            fit.map_w = map_w;
            fit.map_h = map_h;
            const std::vector<float> levels = zoom::levels(fit, zoom::Limits::any());
            check(levels.size() >= 5 && levels.front() == 2.0f, where + "the map offers its levels");

            // the world: ants of every type spread over the whole map, effects and score bubbles in a few windows, a click marker
            populate(rig.world, arc, 0, 0, map_w, map_h, map_w * map_h / 2400, 31337u);
            for (const auto& at : {std::pair<int32_t, int32_t>{0, 0}, {map_w / 2 - 300, map_h / 2 - 250}, {map_w - 700, map_h - 600}}) add_effects(rig.world, std::max(0, at.first), std::max(0, at.second), 640, 480);
            rig.renderer.spawn_transient_effect("xmarks", map_w / 2, map_h / 2);

            // the direct picture of the whole map (one frame of a rig whose view is the map)
            const Shape whole_shape = shape_for_view(map_w, map_h);
            PixelRig whole(arc, map, whole_shape.cw, whole_shape.ch);
            check(whole.ok, where + "the rig for the whole map is up");
            if (!whole.ok) continue;
            whole.world = rig.world;
            whole.renderer.spawn_transient_effect("xmarks", map_w / 2, map_h / 2);
            whole.look(0.0, 0.0, 1.0f);
            const LayoutRect whole_view = whole.layout.view();
            check(whole_view.w == map_w && whole_view.h == map_h && whole.cam().x == 0.0f && whole.cam().y == 0.0f, where + "the whole rig shows the whole map from its corner");
            const Picture direct = whole.shoot(-1, {}, 0, false);
            const Img direct_img = img_of(direct, whole_view);

            for (const float z : levels) {
                const Spec spec = spec_for(direct_img, z);
                const std::string at = where + "at " + zoom::level_name(z) + ": ";
                const struct { const char* name; double x; double y; } cameras[3] = {{"top left", 0.0, 0.0}, {"middle", map_w / 2.0 - 300.0, map_h / 2.0 - 200.0}, {"bottom right", 1.0e9, 1.0e9}};
                for (const auto& cam : cameras) {
                    rig.look(cam.x, cam.y, z);
                    const double cx = static_cast<double>(rig.cam().x);
                    const double cy = static_cast<double>(rig.cam().y);
                    const uint64_t passes = rig.renderer.world_target_passes();
                    const Picture p = rig.shoot(-1, {}, 0, false);
                    const std::string here = at + cam.name + ": ";
                    check(rig.renderer.world_target_passes() == passes + (z == 1.0f ? 0u : 1u), here + "the world went through the offscreen target once (not at all at the zoom 1)");
                    check(!rig.renderer.world_target_failed(), here + "the pass did not fail");
                    check(outside_not_black(p, view) == 0, here + "nothing is drawn outside the view");
                    check(black_lines(p, view) == 0, here + "the map fills the view: no row and no column of it is black");
                    if (z == 2.0f || z == 1.0f || z == 0.5f || z == 0.25f) {
                        const double tolerance = z >= 1.0f ? 0.0 : (z == 0.5f ? 1.0 : 2.0);
                        const double worst = picture_max_error(p, view, spec, cx, cy, z, z >= 1.0f);
                        check(worst <= tolerance, here + "the exact level: no pixel is more than " + num(tolerance) + " levels from the specified picture, worst " + num(worst));
                    } else {
                        const BestFit smooth = best_fit(p, view, spec, cx, cy, z, false, 2.0);
                        const BestFit thin = best_fit(p, view, spec, cx, cy, z, true, 2.0);
                        check(std::fabs(smooth.sx) <= 1.0 && std::fabs(smooth.sy) <= 1.0, here + "it is in its place, to a pixel: the specified picture fits best moved by (" + num(smooth.sx) + ", " + num(smooth.sy) + ") screen pixels");
                        check(smooth.error <= 10.0, here + "the picture is the specified one: mean error " + num(smooth.error) + " of 255 at its best fit");
                        check(smooth.error + 1.0 <= thin.error, here + "it is smooth: closer to the specified picture than to the nearest pixel's (" + num(smooth.error) + " against " + num(thin.error) + ")");
                    }
                }
            }

            // a scroll by whole screen pixels only moves the picture (the exact levels: every pixel; the camera is on the lattice of screen pixels)
            for (const float z : levels) {
                if (!is_power_of_two_level(z) || z == 1.0f) continue;
                const double g = zoom::grid(z);
                rig.look(8.0 * g + 40.0, 8.0 * g + 40.0, z);
                const double ox = static_cast<double>(rig.cam().x);
                const double oy = static_cast<double>(rig.cam().y);
                const Picture base = rig.shoot(-1, {}, 0, false);
                for (const int k : {1, 2, 3}) {
                    rig.look(ox + static_cast<double>(k) * g, oy + static_cast<double>(k) * g, z);
                    check(std::fabs(static_cast<double>(rig.cam().x) - (ox + static_cast<double>(k) * g)) < 1e-9 && std::fabs(static_cast<double>(rig.cam().y) - (oy + static_cast<double>(k) * g)) < 1e-9,
                          where + "at " + zoom::level_name(z) + " the camera moves by " + std::to_string(k) + " screen pixels");
                    const Picture moved = rig.shoot(-1, {}, 0, false);
                    int bad = 0;
                    for (int32_t y = view.y; y < view.bottom() - k; ++y) {
                        for (int32_t x = view.x; x < view.right() - k; ++x) {
                            if (std::memcmp(moved.at(x, y), base.at(x + k, y + k), 3) != 0) ++bad;
                        }
                    }
                    check(bad == 0, where + "at " + zoom::level_name(z) + " a scroll by " + std::to_string(k) + " screen pixels (right and down) only moves the picture: " + std::to_string(bad) + " pixels are not those of the picture " + std::to_string(k) + " pixels on");
                }
            }

            // the frames of the levels in any order give the same pictures (the textures are reused and made again as the size changes)
            {
                std::vector<float> order = levels;
                std::reverse(order.begin(), order.end());
                order.insert(order.end(), levels.begin(), levels.end());
                std::vector<uint64_t> first(levels.size(), 0);
                bool same = true;
                for (size_t round = 0; round < 2; ++round) {
                    for (size_t i = 0; i < levels.size(); ++i) {
                        const float z = order[round * levels.size() + i];
                        rig.look(map_w / 2.0 - 250.0, map_h / 2.0 - 180.0, z);
                        const Picture p = rig.shoot(-1, {}, 0, false);
                        uint64_t h = 1469598103934665603ULL;
                        for (const uint8_t b : p.px) h = (h ^ b) * 1099511628211ULL;
                        const size_t slot = static_cast<size_t>(std::find(levels.begin(), levels.end(), z) - levels.begin());
                        if (round == 0) first[slot] = h;
                        else if (first[slot] != h) same = false;
                    }
                }
                check(same, where + "a level's frame is the same picture when it comes again after the frames of the other levels");
            }
        }
    }
}


// =====================================================================================================================================================
// Renderer: where the software renderer puts the picture of a level, and what is left of the view around a small map
// =====================================================================================================================================================

// SDL's software renderer cannot place a copy at a fraction of a pixel, so the pass scales the last level into the smallest rectangle of whole lattice pixels that holds the exact one:
// [floor(left), ceil(left + width)) on each axis, `left` and `width` being the plan's (a renderer that places a rectangle at a fraction puts it at the exact place). On a map that the view shows
// whole (TINY at 0.5 and below: the picture's four edges are inside the view) these are the columns and rows of the view that are not black, so the rule is seen directly: at the levels with
// a fraction in their numbers as well as at the exact ones (a whole place and size). And the pass paints the whole view itself: what the view held before (here a loud red) is gone, the
// margins around the small map are black.
void test_lattice_placement(const assets::AssetArchive& arc) {
    group("place", "the picture of a level is the whole-pixel rectangle that holds the exact one (the software renderer); the margins around a small map are painted black by the pass");
    PixelRig rig(arc, "TINY", kWideW, kWideH);
    check(rig.ok, "the rig for TINY in the wide picture is up");
    if (!rig.ok) return;
    const LayoutRect view = rig.layout.view();
    const int32_t map_px = static_cast<int32_t>(rig.tiles_w) * 32;
    int fractional = 0;
    for (int k = -4; k >= -11; --k) {                                       // 0.5 (an exact level), 0.42, 0.35, 0.30, 0.25 (exact), ... 0.15, 0.125 (exact): the 992 px map is smaller than the view
        const float z = zoom::series(k);
        const std::string at = "TINY at " + zoom::level_name(z) + ": ";
        rig.look(0.0, 0.0, z);                                                // (the small map is centred: the camera's origin is negative)
        const zoom::Pass p = zoom::plan(static_cast<double>(rig.cam().x), static_cast<double>(rig.cam().y), z, view.w, view.h, map_px, map_px);
        const double width = static_cast<double>(p.w) * static_cast<double>(z);
        const double height = static_cast<double>(p.h) * static_cast<double>(z);
        if (p.left != std::floor(p.left) || p.top != std::floor(p.top) || width != std::floor(width) || height != std::floor(height)) ++fractional;
        const Picture pic = rig.shoot(-1, {}, 0, false);
        int32_t c0 = view.w;
        int32_t c1 = -1;
        int32_t r0 = view.h;
        int32_t r1 = -1;
        for (int32_t y = 0; y < view.h; ++y) {
            for (int32_t x = 0; x < view.w; ++x) {
                const uint8_t* q = pic.at(view.x + x, view.y + y);
                if (q[0] == 0 && q[1] == 0 && q[2] == 0) continue;
                c0 = std::min(c0, x);
                c1 = std::max(c1, x);
                r0 = std::min(r0, y);
                r1 = std::max(r1, y);
            }
        }
        const int32_t want_c0 = static_cast<int32_t>(std::floor(p.left));
        const int32_t want_c1 = static_cast<int32_t>(std::ceil(p.left + width));
        const int32_t want_r0 = static_cast<int32_t>(std::floor(p.top));
        const int32_t want_r1 = static_cast<int32_t>(std::ceil(p.top + height));
        check(c0 == want_c0 && c1 + 1 == want_c1 && r0 == want_r0 && r1 + 1 == want_r1,
              at + "the picture covers the columns " + std::to_string(c0) + " .. " + std::to_string(c1) + " and the rows " + std::to_string(r0) + " .. " + std::to_string(r1) + " of the view, the plan says [" + std::to_string(want_c0) + ", " +
                  std::to_string(want_c1) + ") x [" + std::to_string(want_r0) + ", " + std::to_string(want_r1) + ")");
        // the pass paints the whole view: the same picture when the view held something else before
        rig.renderer.set_show_hp(false);
        rig.renderer.begin_frame();
        rig.renderer.fill_rect(view.x, view.y, view.w, view.h, ants::assets::ColorRGBA{200, 40, 40, 255});
        rig.renderer.render_world(rig.world, rig.engine.grid(), -1, {}, false, false, -1, -1, 0, 0.0f);
        const Picture over_red = rig.read();
        check(differ(over_red, pic, view) == 0, at + "the margins around the small map are black whatever the view held before: " + std::to_string(differ(over_red, pic, view)) + " pixels differ");
    }
    check(fractional >= 4, "the levels checked include ones with a fraction in the place or the size of the picture: " + std::to_string(fractional));
}


// =====================================================================================================================================================
// Renderer: the fog of war at every level
// =====================================================================================================================================================

// With Fog of War on, every level has to draw what the zoom 1 draws: the fog over every unexplored tile and nothing that the fog hides (every level goes through the one world pass; a level that
// skipped the fog or a gate would show what the zoom 1 cannot). For every level of GAUNTLET and TINY, both pictures, four cameras: a scene that is unexplored except a window at the view's top left,
// with every class of thing on unexplored tiles and two that are always drawn. R1 = everything, R2 = without the hidden things, R3 = nothing but the fog, R4 = nothing and no fog, R5 = only the hidden
// things and no fog: R1 = R2 (pixel for pixel), the two always drawn are in R2 and not in R3, the hidden things are in R5 and not in R4 (so R1 = R2 is no empty check), and R3 against R4 is the fog itself,
// block by block (R1 = R2 alone could not notice a fog that is missing: the gates hide the things with or without it).

/// An ant standing at (px, py), drawn from its idle clip (populate() puts them at random places; this one where it is told to)
sim::AntSnapshot fog_ant(const assets::AssetArchive& arc, uint32_t id, uint8_t player, int type, int32_t px, int32_t py) {
    static const int kDirs[5] = {3, 7, 2, 8, 9};
    const int dir = kDirs[id % 5];
    sim::AntSnapshot a;
    a.id = id;
    a.player_id = player;
    a.type = static_cast<sim::AntType>(type % 6);
    a.px = px;
    a.py = py;
    a.tile_x = px / 32;
    a.tile_y = py / 32;
    a.hp = static_cast<uint16_t>(2 + id % 8);
    a.max_hp = 10;
    a.state = sim::UnitState::Idle;
    const int32_t clip = idle_clip(arc, type % 6, dir);
    a.loco_clip = clip < 0 ? uint16_t{0x7FFE} : static_cast<uint16_t>(clip);
    a.loco_frame = 0;
    a.loco_mirrored = (dir == 2 || dir == 8 || dir == 9) && id % 3 == 0;
    a.loco_left_ms = 100;
    return a;
}

/// One scene of the group: the explored window, the things in it and outside it, and what is needed to take them in and out of the engine's map
struct FogScene {
    enum Kind { kBomb = 0, kPowerup = 1, kLunchbox = 2, kFire = 3, kAnt = 4, kEffect = 5, kDropper = 6, kPile = 7, kKinds = 8 };
    struct Item {                                  // a thing that lives in a cell of the map
        int32_t tx{0};
        int32_t ty{0};
        int kind{kBomb};
        sim::TileCell saved;                       // the cell as it was
    };
    struct Pile {                                  // a pile of food that the map has: the cells that it covers (their anchor is its anchor) as they were
        bool hidden{false};                        // the anchor is on an unexplored tile and no cell of it is explored: the fog hides it (a cell that is explored draws it, as in the original)
        std::vector<std::pair<int32_t, int32_t>> cells;
        std::vector<sim::TileCell> saved;
    };
    int32_t tx0{0}, ty0{0}, tx1{0}, ty1{0};       // the explored window: the tiles [tx0, tx1) x [ty0, ty1)
    std::vector<sim::AntSnapshot> hidden_ants;     // enemy ants on unexplored tiles
    std::vector<sim::AntSnapshot> shown_ants;      // an enemy ant on an explored tile and an own ant on an unexplored one
    std::vector<sim::VisualEffect> effects;        // on unexplored tiles
    std::vector<sim::FlowerDropperSnapshot> droppers;
    std::vector<Item> items;
    std::vector<Pile> piles;
    int placed[kKinds] = {0, 0, 0, 0, 0, 0, 0, 0};
    int piles_in_view{0};                          // hidden piles whose anchor is in the world that the camera shows (the art of a pile reaches 64 pixels past its anchor)
};

/// A thing of the map on or off: on puts it into its cell, off puts the cell back as it was
void fog_put(sim::Grid& grid, const FogScene::Item& it, bool on) {
    const uint32_t x = static_cast<uint32_t>(it.tx);
    const uint32_t y = static_cast<uint32_t>(it.ty);
    if (!on) {
        grid.get_cell_mut(x, y) = it.saved;
        return;
    }
    switch (it.kind) {
        case FogScene::kBomb: grid.place_bomb(x, y, static_cast<uint8_t>(1 + (x + y) % 3)); break;
        case FogScene::kPowerup: grid.place_powerup(it.tx, it.ty, static_cast<uint8_t>(1 + (x + y) % 5)); break;
        case FogScene::kLunchbox: grid.set_layer2(x, y, sim::TILE_LUNCHBOX, 255); break;
        default: grid.set_fire_at(sim::TileCoord{it.tx, it.ty}, 3600); break;
    }
}

bool fog_placed(const sim::Grid& grid, const FogScene::Item& it) {
    const sim::TileCell& c = grid.get_cell(static_cast<uint32_t>(it.tx), static_cast<uint32_t>(it.ty));
    switch (it.kind) {
        case FogScene::kBomb: return c.has_bomb();
        case FogScene::kPowerup: return c.has_powerup();
        case FogScene::kLunchbox: return c.has_lunchbox();
        default: return c.has_fire();
    }
}

/// The scene for the camera (cx, cy) at the zoom z: the fog is set in the rig's world, the things are chosen (not yet put anywhere)
FogScene fog_scene(PixelRig& rig, const assets::AssetArchive& arc, double cx, double cy, float z, const LayoutRect& view) {
    FogScene sc;
    sim::WorldState& world = rig.world;
    sim::Grid& grid = rig.engine.grid_mut();
    const int32_t tiles_w = static_cast<int32_t>(rig.tiles_w);
    const int32_t tiles_h = static_cast<int32_t>(rig.tiles_h);
    const double vis_w = zoom::visible_exact(view.w, z);
    const double vis_h = zoom::visible_exact(view.h, z);
    // the explored window: the tiles over the top left part of the world that is seen (about 30 % of it on each axis, at least 2 tiles), so that most of the view is deep in the fog
    const auto span = [](double origin, double vis, int32_t tiles, int32_t& t0, int32_t& t1) {
        t0 = static_cast<int32_t>(std::floor((origin + vis * 0.12) / 32.0));
        t1 = static_cast<int32_t>(std::ceil((origin + vis * 0.42) / 32.0));
        t1 = std::min(std::max(t1, t0 + 2), tiles);
        t0 = std::max(0, std::min(t0, t1 - 2));
    };
    span(cx, vis_w, tiles_w, sc.tx0, sc.tx1);
    span(cy, vis_h, tiles_h, sc.ty0, sc.ty1);
    world.fog_of_war_enabled = true;
    world.fog_revealed.assign(static_cast<size_t>(tiles_w) * static_cast<size_t>(tiles_h), 0);
    for (int32_t ty = sc.ty0; ty < sc.ty1; ++ty) {
        for (int32_t tx = sc.tx0; tx < sc.tx1; ++tx) world.fog_revealed[static_cast<size_t>(ty) * static_cast<size_t>(tiles_w) + static_cast<size_t>(tx)] = 1;
    }
    const auto explored = [&](int32_t tx, int32_t ty) { return tx >= sc.tx0 && tx < sc.tx1 && ty >= sc.ty0 && ty < sc.ty1; };
    const auto in_map = [&](int32_t tx, int32_t ty) { return tx >= 0 && ty >= 0 && tx < tiles_w && ty < tiles_h; };

    // the food that the map has: hidden when its anchor and every cell of it are unexplored
    for (const sim::FoodObject& food : grid.food_objects()) {
        const int32_t col = static_cast<int32_t>(food.col);
        const int32_t row = static_cast<int32_t>(food.row);
        FogScene::Pile pile;
        bool any_explored = explored(col, row);
        for (int32_t dy = -4; dy <= 4; ++dy) {
            for (int32_t dx = -4; dx <= 4; ++dx) {
                if (!in_map(col + dx, row + dy)) continue;
                const sim::TileCell& c = grid.get_cell(static_cast<uint32_t>(col + dx), static_cast<uint32_t>(row + dy));
                if (c.anchor_x != col || c.anchor_y != row || !c.is_food) continue;
                pile.cells.emplace_back(col + dx, row + dy);
                pile.saved.push_back(c);
                any_explored = any_explored || explored(col + dx, row + dy);
            }
        }
        if (pile.cells.empty()) continue;
        pile.hidden = !any_explored;
        sc.placed[FogScene::kPile] += pile.hidden ? 1 : 0;
        if (pile.hidden && col * 32 > cx - 64.0 && col * 32 < cx + vis_w + 64.0 && row * 32 > cy - 64.0 && row * 32 < cy + vis_h + 64.0) ++sc.piles_in_view;
        sc.piles.push_back(std::move(pile));
    }

    // the enemy ant that is drawn: on the middle tile of the explored window
    uint32_t id = 7000;
    sc.shown_ants.push_back(fog_ant(arc, id, 1, static_cast<int>(id), ((sc.tx0 + sc.tx1 - 1) / 2) * 32 + 16, ((sc.ty0 + sc.ty1 - 1) / 2) * 32 + 16));
    ++id;
    // a 5 x 5 lattice over the world that is seen: the last point holds the own ant, the others the hidden things, class after class (every thing on the nearest unexplored tile that is free)
    std::vector<std::pair<int32_t, int32_t>> taken;
    const FogScene::Kind order[8] = {FogScene::kAnt, FogScene::kAnt, FogScene::kBomb, FogScene::kPowerup, FogScene::kEffect, FogScene::kLunchbox, FogScene::kFire, FogScene::kDropper};
    int next = 0;
    for (int j = 0; j < 5; ++j) {
        for (int i = 0; i < 5; ++i) {
            const int32_t wx = std::clamp(static_cast<int32_t>(cx + (0.1 + 0.2 * i) * vis_w), 16, tiles_w * 32 - 16);
            const int32_t wy = std::clamp(static_cast<int32_t>(cy + (0.1 + 0.2 * j) * vis_h), 16, tiles_h * 32 - 16);
            const bool own = i == 4 && j == 4;
            const FogScene::Kind kind = own ? FogScene::kAnt : order[next % 8];
            if (!own) ++next;
            const int32_t home_x = wx / 32;
            const int32_t home_y = wy / 32;
            bool done = false;
            for (int ring = 0; ring <= 8 && !done; ++ring) {
                for (int32_t dy = -ring; dy <= ring && !done; ++dy) {
                    for (int32_t dx = -ring; dx <= ring && !done; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                        const int32_t tx = home_x + dx;
                        const int32_t ty = home_y + dy;
                        if (!in_map(tx, ty) || explored(tx, ty)) continue;
                        if (std::find(taken.begin(), taken.end(), std::make_pair(tx, ty)) != taken.end()) continue;
                        if (kind == FogScene::kAnt) {
                            sim::AntSnapshot ant = fog_ant(arc, id, own ? uint8_t{0} : static_cast<uint8_t>(1 + id % 3), static_cast<int>(id), tx * 32 + 16, ty * 32 + 16);
                            if (!own && sc.hidden_ants.size() % 3 == 2) {                  // (every third hidden ant is a dud bomb's, burning: the overlay of its flame is what the fog has to hide)
                                ant.burn_elapsed_ms = 150;
                                ant.frozen = true;
                                ant.state = sim::UnitState::Burn;
                            }
                            (own ? sc.shown_ants : sc.hidden_ants).push_back(ant);
                            ++id;
                        } else if (kind == FogScene::kEffect) {
                            sim::VisualEffect e;
                            e.anim_name = next % 2 == 0 ? "bombex" : "sputter";
                            e.duration_ms = next % 2 == 0 ? 680u : 830u;
                            e.px = tx * 32;
                            e.py = ty * 32;
                            e.y_key = ty * 32;
                            e.elapsed_ms = 150;
                            e.frame = 3;
                            e.total_frames = static_cast<uint16_t>(e.duration_ms / 50u);
                            e.fog_gated = true;
                            sc.effects.push_back(e);
                        } else if (kind == FogScene::kDropper) {
                            sim::FlowerDropperSnapshot fd;
                            fd.x = tx * 32 - 64;
                            fd.y = ty * 32 - 64;
                            fd.drop_x = tx;
                            fd.drop_y = ty;
                            fd.is_dropping = true;
                            fd.drop_elapsed_ms = 150;
                            fd.powerup_type = static_cast<uint8_t>(next % 5);
                            sc.droppers.push_back(fd);
                        } else {                                                       // a thing of the map: only on a cell that holds nothing else (a bomb is refused next to a hill)
                            const sim::TileCell& cell = grid.get_cell(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                            if (!cell.is_empty_overlay() || cell.is_food || cell.is_powerup || cell.is_obstacle() || cell.terrain_type != sim::TERRAIN_WALKABLE || cell.anchor_x >= 0) continue;
                            FogScene::Item item;
                            item.tx = tx;
                            item.ty = ty;
                            item.kind = kind;
                            item.saved = cell;
                            fog_put(grid, item, true);
                            const bool ok = fog_placed(grid, item);
                            fog_put(grid, item, false);
                            if (!ok) continue;
                            sc.items.push_back(item);
                        }
                        taken.emplace_back(tx, ty);
                        ++sc.placed[kind];
                        done = true;
                    }
                }
            }
        }
    }
    return sc;
}

/// The scene put into the rig: the hidden things, the things that are always drawn, the fog
void fog_set(PixelRig& rig, const FogScene& sc, bool hidden, bool shown, bool fog) {
    sim::Grid& grid = rig.engine.grid_mut();
    for (const FogScene::Item& it : sc.items) fog_put(grid, it, hidden);
    for (const FogScene::Pile& p : sc.piles) {
        const bool on = p.hidden ? hidden : shown;
        for (size_t k = 0; k < p.cells.size(); ++k) {
            sim::TileCell& c = grid.get_cell_mut(static_cast<uint32_t>(p.cells[k].first), static_cast<uint32_t>(p.cells[k].second));
            if (on) {
                c = p.saved[k];
            } else {                                                                    // (the pile is gone: its cells are empty)
                c.interactive_id = sim::TILE_EMPTY;
                c.is_food = false;
            }
        }
    }
    rig.world.ants.clear();
    if (hidden) rig.world.ants.insert(rig.world.ants.end(), sc.hidden_ants.begin(), sc.hidden_ants.end());
    if (shown) rig.world.ants.insert(rig.world.ants.end(), sc.shown_ants.begin(), sc.shown_ants.end());
    rig.world.effects = hidden ? sc.effects : std::vector<sim::VisualEffect>();
    rig.world.flower_droppers = hidden ? sc.droppers : std::vector<sim::FlowerDropperSnapshot>();
    rig.world.fog_of_war_enabled = fog;
}

/// The map of the rig as it was before the scene (every cell that the scene changed put back), and a world without the scene's ants, effects and droppers
void fog_reset(PixelRig& rig, const FogScene& sc) {
    sim::Grid& grid = rig.engine.grid_mut();
    for (const FogScene::Item& it : sc.items) fog_put(grid, it, false);
    for (const FogScene::Pile& p : sc.piles) {
        for (size_t k = 0; k < p.cells.size(); ++k) grid.get_cell_mut(static_cast<uint32_t>(p.cells[k].first), static_cast<uint32_t>(p.cells[k].second)) = p.saved[k];
    }
    rig.world.ants.clear();
    rig.world.effects.clear();
    rig.world.flower_droppers.clear();
}

/// The pixels of `area` where two pictures differ (any channel), and the box that holds them
struct DiffBox {
    int count{0};
    int32_t x0{1 << 30}, y0{1 << 30}, x1{-1}, y1{-1};
};
DiffBox diff_box(const Picture& a, const Picture& b, const LayoutRect& area) {
    DiffBox d;
    for (int32_t y = std::max(0, area.y); y < std::min(area.bottom(), a.h); ++y) {
        for (int32_t x = std::max(0, area.x); x < std::min(area.right(), a.w); ++x) {
            if (std::memcmp(a.at(x, y), b.at(x, y), 3) == 0) continue;
            ++d.count;
            d.x0 = std::min(d.x0, x);
            d.y0 = std::min(d.y0, y);
            d.x1 = std::max(d.x1, x);
            d.y1 = std::max(d.y1, y);
        }
    }
    return d;
}

/// The fog against the same world without it, block by block: a block over explored ground (with the margin that the level's filters look at) is the same in both (16 x 16 screen pixels); one
/// deep in the fog (its tiles and their neighbours unexplored: the dither covers every other pixel) keeps about half of what it showed (a tile of the world, at least 16 screen pixels).
struct FogBlocks {
    int deep_fog{0};
    int deep_clear{0};
    int bad_fog{0};
    int bad_clear{0};
    double lowest{9.0};
    double highest{0.0};
};
FogBlocks fog_blocks(const Picture& fogged, const Picture& clear, const LayoutRect& view, double cx, double cy, float z, const sim::WorldState& world) {
    FogBlocks r;
    const int32_t tw = static_cast<int32_t>(world.width);
    const int32_t th = static_cast<int32_t>(world.height);
    const auto explored = [&](int32_t tx, int32_t ty) { return tx >= 0 && ty >= 0 && tx < tw && ty < th && world.fog_revealed[static_cast<size_t>(ty) * world.width + static_cast<size_t>(tx)] != 0; };
    const auto deep = [&](int32_t tx, int32_t ty) { return !explored(tx, ty) && !explored(tx + 1, ty) && !explored(tx - 1, ty) && !explored(tx, ty + 1) && !explored(tx, ty - 1); };
    const double zz = static_cast<double>(z);
    const double support = z >= 1.0f ? 3.0 : 12.0;                     // world pixels around a block that its pixels depend on: a halving block (4 texels at most), the filter, the lattice
    for (const bool clear_blocks : {true, false}) {
        const int32_t block = clear_blocks ? 16 : std::max<int32_t>(16, static_cast<int32_t>(std::lround(32.0 * zz)));
        for (int32_t j0 = 0; j0 + block <= view.h; j0 += block) {
            for (int32_t i0 = 0; i0 + block <= view.w; i0 += block) {
                const double x_lo = cx + static_cast<double>(i0) / zz - support;
                const double x_hi = cx + static_cast<double>(i0 + block) / zz + support;
                const double y_lo = cy + static_cast<double>(j0) / zz - support;
                const double y_hi = cy + static_cast<double>(j0 + block) / zz + support;
                if (x_lo < 0.0 || y_lo < 0.0 || x_hi > static_cast<double>(tw) * 32.0 || y_hi > static_cast<double>(th) * 32.0) continue;
                bool all_clear = true;
                bool all_deep = true;
                for (int32_t ty = static_cast<int32_t>(std::floor(y_lo / 32.0)); ty <= static_cast<int32_t>(std::floor((y_hi - 1e-6) / 32.0)); ++ty) {
                    for (int32_t tx = static_cast<int32_t>(std::floor(x_lo / 32.0)); tx <= static_cast<int32_t>(std::floor((x_hi - 1e-6) / 32.0)); ++tx) {
                        all_clear = all_clear && explored(tx, ty);
                        all_deep = all_deep && deep(tx, ty);
                    }
                }
                if (clear_blocks ? !all_clear : !all_deep) continue;
                long sum_fog = 0;
                long sum_clear = 0;
                bool same = true;
                for (int32_t j = 0; j < block; ++j) {
                    for (int32_t i = 0; i < block; ++i) {
                        const uint8_t* p = fogged.at(view.x + i0 + i, view.y + j0 + j);
                        const uint8_t* q = clear.at(view.x + i0 + i, view.y + j0 + j);
                        sum_fog += p[0] + p[1] + p[2];
                        sum_clear += q[0] + q[1] + q[2];
                        if (std::memcmp(p, q, 3) != 0) same = false;
                    }
                }
                if (clear_blocks) {
                    ++r.deep_clear;
                    if (!same) ++r.bad_clear;
                } else if (sum_clear >= static_cast<long>(block) * block * 3 * 20) {      // (a block of black terrain says nothing about a fog over it)
                    const double ratio = static_cast<double>(sum_fog) / static_cast<double>(sum_clear);
                    ++r.deep_fog;
                    r.lowest = std::min(r.lowest, ratio);
                    r.highest = std::max(r.highest, ratio);
                    if (ratio < 0.3 || ratio > 0.7) ++r.bad_fog;
                }
            }
        }
    }
    return r;
}

void test_fog_levels(const assets::AssetArchive& arc) {
    group("fog", "with Fog of War on, every level draws what the zoom 1 draws: nothing that the fog hides, and the fog over every unexplored tile of the view");
    int scenes = 0;
    int hidden_piles = 0;                                              // piles of food that the map has, hidden and in view, over the scenes
    for (const Shape& s : kShapes) {
        const LayoutRect view = ScreenLayout::with_size(s.cw, s.ch).view();
        for (const char* map : {"GAUNTLET", "TINY"}) {
            PixelRig rig(arc, map, s.cw, s.ch);
            check(rig.ok, std::string("the rig for ") + map + " in the " + s.name + " picture is up");
            if (!rig.ok) continue;
            const int32_t map_w = static_cast<int32_t>(rig.tiles_w) * 32;
            const int32_t map_h = static_cast<int32_t>(rig.tiles_h) * 32;
            zoom::Fit fit;
            fit.view_w = view.w;
            fit.view_h = view.h;
            fit.map_w = map_w;
            fit.map_h = map_h;
            const std::vector<float> levels = zoom::levels(fit, zoom::Limits::any());
            check(levels.size() >= 7 && levels.front() == 2.0f, std::string(map) + " in the " + s.name + " picture offers its levels: " + std::to_string(levels.size()));
            const LayoutRect canvas{0, 0, s.cw, s.ch};
            double food_x = map_w / 2.0;                              // the pile of food that is nearest to the middle of the map (a camera that is held at the map's edge would put it in the explored window)
            double food_y = map_h / 2.0;
            double nearest = 1.0e18;
            for (const sim::FoodObject& food : rig.engine.grid().food_objects()) {
                const double dx = static_cast<double>(food.col) * 32.0 + 16.0 - map_w / 2.0;
                const double dy = static_cast<double>(food.row) * 32.0 + 16.0 - map_h / 2.0;
                if (dx * dx + dy * dy < nearest) {
                    nearest = dx * dx + dy * dy;
                    food_x = static_cast<double>(food.col) * 32.0 + 16.0;
                    food_y = static_cast<double>(food.row) * 32.0 + 16.0;
                }
            }
            const struct { const char* name; double x; double y; } cameras[4] = {{"top left", 0.0, 0.0}, {"middle", map_w / 2.0 - 300.0, map_h / 2.0 - 200.0}, {"bottom right", 1.0e9, 1.0e9}, {"on the food", 0.0, 0.0}};
            for (const float z : levels) {
                for (const auto& cam : cameras) {
                    const std::string at = std::string(map) + " " + s.name + " at " + zoom::level_name(z) + ", " + cam.name + ": ";
                    if (std::strcmp(cam.name, "on the food") == 0) rig.look(food_x - 0.75 * zoom::visible_exact(view.w, z), food_y - 0.75 * zoom::visible_exact(view.h, z), z);       // (the pile three quarters across and down the view)
                    else rig.look(cam.x, cam.y, z);
                    const double cx = static_cast<double>(rig.cam().x);
                    const double cy = static_cast<double>(rig.cam().y);
                    const FogScene sc = fog_scene(rig, arc, cx, cy, z, view);
                    ++scenes;
                    hidden_piles += sc.piles_in_view;
                    if (std::strcmp(cam.name, "on the food") == 0) check(sc.piles_in_view >= 1, at + "a hidden pile of food is in view: " + std::to_string(sc.piles_in_view));
                    bool all_classes = sc.shown_ants.size() == 2;                // (the enemy ant on the explored window and the own ant outside it)
                    for (int k = 0; k < FogScene::kKinds; ++k) all_classes = all_classes && (sc.placed[k] > 0 || k == FogScene::kPile);       // (a view may have no hidden food of the map's)
                    check(all_classes, at + "the scene has a thing of every class (an ant, a bomb, a power-up, an effect, a lunchbox, a fire wall, a falling power-up) and the two that are always drawn");

                    const uint64_t passes = rig.renderer.world_target_passes();
                    fog_set(rig, sc, true, true, true);
                    const Picture r1 = rig.shoot(-1, {}, -1, true);
                    fog_set(rig, sc, false, true, true);
                    const Picture r2 = rig.shoot(-1, {}, -1, true);
                    fog_set(rig, sc, false, false, true);
                    const Picture r3 = rig.shoot(-1, {}, -1, true);
                    fog_set(rig, sc, false, false, false);
                    const Picture r4 = rig.shoot(-1, {}, -1, true);
                    fog_set(rig, sc, true, false, false);
                    const Picture r5 = rig.shoot(-1, {}, -1, true);
                    fog_reset(rig, sc);                                      // (the cells of the map as they were: the next scene starts from them)
                    // the pictures are those of the world pass through the offscreen target (none at the zoom 1): a level that fell back to the zoom 1 picture would prove nothing
                    check(rig.renderer.world_target_passes() == passes + (z == 1.0f ? 0u : 5u) && !rig.renderer.world_target_failed(), at + "the five pictures went through the offscreen target (none at the zoom 1) and the pass did not fail");

                    // 1. what the fog hides is not drawn: the picture with the hidden things is the picture without them
                    const DiffBox hidden = diff_box(r1, r2, canvas);
                    check(hidden.count == 0, at + "the hidden things are not drawn: " + std::to_string(hidden.count) + " pixels differ, in the box (" + std::to_string(hidden.x0) + ", " + std::to_string(hidden.y0) + ") - (" + std::to_string(hidden.x1) + ", " +
                                                 std::to_string(hidden.y1) + ")");
                    // 2. what is always drawn is: the enemy ant on an explored tile and the own ant on an unexplored one make a difference to R2 where they stand
                    for (const sim::AntSnapshot& a : sc.shown_ants) {
                        const double zz = static_cast<double>(z);
                        const double sx = view.x + (static_cast<double>(a.px) - cx) * zz;
                        const double sy = view.y + (static_cast<double>(a.py) - cy) * zz;
                        const LayoutRect box{static_cast<int32_t>(std::floor(sx - 24.0 * zz)) - 2, static_cast<int32_t>(std::floor(sy - 36.0 * zz)) - 2, static_cast<int32_t>(std::ceil(48.0 * zz)) + 5, static_cast<int32_t>(std::ceil(60.0 * zz)) + 5};
                        const DiffBox shown = diff_box(r2, r3, box);
                        check(shown.count >= 3, at + std::string(a.player_id == 0 ? "the own ant on an unexplored tile" : "the enemy ant on an explored tile") + " is drawn: " + std::to_string(shown.count) + " pixels of its box differ from the picture without it");
                    }
                    // 3. the hidden things are there to be hidden: with the fog off they alone make a picture (R5 against R4)
                    const DiffBox there = diff_box(r5, r4, view);
                    check(there.count >= 50, at + "without the fog the hidden things are drawn (so the first check is not an empty one): " + std::to_string(there.count) + " pixels");
                    // 4. the fog is the fog: the same over explored ground, half of it deep in the fog, in every block of the view
                    const FogBlocks blocks = fog_blocks(r3, r4, view, cx, cy, z, rig.world);
                    check(blocks.bad_fog == 0, at + "deep in the fog about half of what is there is left: " + std::to_string(blocks.bad_fog) + " of " + std::to_string(blocks.deep_fog) + " blocks keep a share outside [0.3, 0.7], from " + num(blocks.lowest) + " to " + num(blocks.highest));
                    check(blocks.bad_clear == 0, at + "over explored ground the fog changes nothing: " + std::to_string(blocks.bad_clear) + " of " + std::to_string(blocks.deep_clear) + " blocks differ");
                    check(blocks.deep_fog >= 4 && blocks.deep_clear >= 3, at + "there is fog and there is explored ground to compare: " + std::to_string(blocks.deep_fog) + " blocks deep in the fog (4), " + std::to_string(blocks.deep_clear) + " over explored ground (3)");
                    check(outside_not_black(r1, view) == 0, at + "nothing is drawn outside the view");
                }
            }
        }
    }
    check(scenes >= 160, "the scenes: " + std::to_string(scenes));
    check(hidden_piles >= 60, "food that the map has was hidden in the views of the scenes: " + std::to_string(hidden_piles) + " piles");
}


// =====================================================================================================================================================
// Renderer: the state around the pass, the hit point digits, the tile grid, a picture inside the canvas
// =====================================================================================================================================================

void test_pass_state(const assets::AssetArchive& arc) {
    group("state", "the pass leaves the renderer as it found it; the hit point digits are one size; the tile grid and the static layers follow the zoom; a picture inside the canvas");
    PixelRig rig(arc, "GAUNTLET", kWideW, kWideH);
    check(rig.ok, "the wide rig is up");
    if (!rig.ok) return;
    const LayoutRect view = rig.layout.view();
    std::vector<uint32_t> ids;
    fill_scene(rig, arc, 300, 260, view.w, view.h, ids);

    // the camera, the picture, the origin and the layout come back as they were, and the chrome after the world is drawn in the canvas's numbers, unclipped
    for (const float z : {0.5f, 1.0f, 2.0f}) {
        rig.look(300.5, 260.0, z);
        const ViewportCamera before = rig.cam();
        const LayoutRect picture = rig.renderer.picture();
        const ScreenLayout layout = rig.renderer.layout();
        rig.renderer.begin_frame();
        rig.renderer.set_origin(3, 4);                               // (a window of the original's own screen is open when the world is drawn: the pass puts its own origin away and gives this one back)
        rig.renderer.render_world(rig.world, rig.engine.grid(), -1, {}, false, false, -1, -1, -1, 0.0f);
        const ViewportCamera after = rig.cam();
        check(after.x == before.x && after.y == before.y && after.world_x == before.world_x && after.world_y == before.world_y && after.zoom == before.zoom && after.view_x == before.view_x &&
                  after.view_y == before.view_y && after.viewport_w == before.viewport_w && after.viewport_h == before.viewport_h && after.centre_small_maps == before.centre_small_maps,
              std::string("the camera is as it was after a frame at ") + zoom::level_name(z));
        check(rig.renderer.picture() == picture && rig.renderer.layout() == layout && rig.renderer.origin() == LayoutPoint{3, 4}, std::string("the picture, the layout and the origin are as they were at ") + zoom::level_name(z));
        rig.renderer.set_origin(0, 0);
        check(rig.renderer.zoomed() == (z != 1.0f), std::string("zoomed() follows the camera's zoom at ") + zoom::level_name(z));
        // chrome in the canvas's own numbers, not clipped to the view, not moved
        rig.renderer.fill_rect(5, 5, 10, 10, ants::assets::ColorRGBA{250, 0, 0, 255});
        rig.renderer.fill_rect(850, 300, 20, 10, ants::assets::ColorRGBA{0, 250, 0, 255});
        const Picture pic = rig.read();
        check(pic.at(5, 5)[0] == 250 && pic.at(14, 14)[0] == 250 && pic.at(4, 5)[0] != 250 && pic.at(15, 15)[0] != 250, std::string("a fill after the world is at its own place at ") + zoom::level_name(z));
        check(pic.at(850, 300)[1] == 250 && pic.at(869, 309)[1] == 250 && !view.contains(850, 300), std::string("... also outside the view (no clip is left over) at ") + zoom::level_name(z));
    }

    // frames in any order give the same pictures (the targets are reused and made again as the zoom changes)
    {
        std::vector<Picture> first;
        const float order[7] = {1.0f, 2.0f, 0.5f, 1.0f, 0.5f, 2.0f, 1.0f};
        std::vector<Picture> got;
        for (const float z : order) {
            rig.look(300.0, 260.0, z);
            got.push_back(rig.shoot(static_cast<int32_t>(ids.front()), ids, 0, true));
        }
        check(differ(got[0], got[3], view) == 0 && differ(got[0], got[6], view) == 0, "a zoom 1 frame after zoom frames is the first zoom 1 frame again");
        check(differ(got[1], got[5], view) == 0 && differ(got[2], got[4], view) == 0, "a zoom frame is the same picture when it comes again");
        check(differ(got[0], got[1], view) > 1000 && differ(got[0], got[2], view) > 1000 && differ(got[1], got[2], view) > 1000, "the three zooms are three different pictures");
    }

    // the hit point digits are text: one size at every zoom, at the sprite's screen position
    for (const Shape& shape : kShapes) {
        PixelRig hp_rig(arc, "GAUNTLET", shape.cw, shape.ch);
        if (!hp_rig.ok) continue;
        const LayoutRect v = hp_rig.layout.view();
        sim::AntSnapshot a;
        a.id = 9000;
        a.player_id = 0;
        a.type = sim::AntType::Worker;
        a.px = 700;
        a.py = 650;
        a.tile_x = 700 / 32;
        a.tile_y = 650 / 32;
        a.hp = 10;
        a.max_hp = 10;
        a.state = sim::UnitState::Idle;
        a.loco_clip = 0x7FFE;                                         // no sprite: only the digits of the health number are drawn
        hp_rig.world.ants.push_back(a);
        // the digits "10" as a reference: drawn alone on a black frame at the zoom 1 (the fixed font's 8 x 15 cells)
        hp_rig.renderer.begin_frame();
        hp_rig.renderer.draw_fixed_text("10", 100, 100, ants::assets::ColorRGBA{255, 255, 255, 255});
        const Picture ref = hp_rig.read();
        int ref_count = 0;
        int ref_x0 = 1 << 30, ref_y0 = 1 << 30, ref_x1 = -1, ref_y1 = -1;
        for (int32_t y = 0; y < ref.h; ++y) {
            for (int32_t x = 0; x < ref.w; ++x) {
                if (ref.at(x, y)[0] == 255 && ref.at(x, y)[1] == 255 && ref.at(x, y)[2] == 255) {
                    ++ref_count;
                    ref_x0 = std::min(ref_x0, x);
                    ref_y0 = std::min(ref_y0, y);
                    ref_x1 = std::max(ref_x1, x);
                    ref_y1 = std::max(ref_y1, y);
                }
            }
        }
        check(ref_count > 30 && ref_x1 - ref_x0 == 13 && ref_y1 - ref_y0 == 9, std::string(shape.name) + ": the reference digits are 14 x 10 pixels of white (two 8 x 15 cells of the fixed font)");
        for (const float z : {0.5f, 1.0f, 2.0f}) {
            const double ox = z == 2.0f ? 640.0 : (z == 1.0f ? 500.0 : 300.0);
            const double oy = z == 2.0f ? 560.0 : (z == 1.0f ? 400.0 : 100.0);
            hp_rig.look(ox, oy, z);
            const int64_t o2x = static_cast<int64_t>(std::llround(hp_rig.cam().x * 2.0));
            const int64_t o2y = static_cast<int64_t>(std::llround(hp_rig.cam().y * 2.0));
            const Picture with = hp_rig.shoot(-1, {}, -1, true);
            const Picture without = hp_rig.shoot(-1, {}, -1, false);
            // the screen position of the sprite's position (700, 650): the view's corner plus the world offset times the zoom
            const int hpp = z == 0.5f ? 4 : z == 1.0f ? 2 : 1;                        // half pixels per screen pixel
            const int32_t sx = v.x + static_cast<int32_t>(fdiv_half(700 * 2 - o2x, hpp));
            const int32_t sy = v.y + static_cast<int32_t>(fdiv_half(650 * 2 - o2y, hpp));
            int count = 0;
            int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
            bool same_as_text = true;
            for (int32_t y = 0; y < with.h; ++y) {
                for (int32_t x = 0; x < with.w; ++x) {
                    const bool changed = std::memcmp(with.at(x, y), without.at(x, y), 3) != 0;
                    if (changed) {
                        ++count;
                        x0 = std::min(x0, x);
                        y0 = std::min(y0, y);
                        x1 = std::max(x1, x);
                        y1 = std::max(y1, y);
                        // the same pixel of the reference text, moved from (100, 100) to the sprite's screen position
                        const int32_t rx = x - sx + 100;
                        const int32_t ry = y - sy + 100;
                        const bool white = rx >= 0 && ry >= 0 && rx < ref.w && ry < ref.h && ref.at(rx, ry)[0] == 255 && ref.at(rx, ry)[1] == 255 && ref.at(rx, ry)[2] == 255;
                        if (!white || with.at(x, y)[0] != 255) same_as_text = false;
                    }
                }
            }
            const std::string at = std::string(shape.name) + " at " + zoom::level_name(z) + ": ";
            check(count == ref_count && same_as_text, at + "the digits are the fixed font's, the same " + std::to_string(ref_count) + " white pixels at every zoom (" + std::to_string(count) + ")");
            check(x0 == sx + (ref_x0 - 100) && y0 == sy + (ref_y0 - 100) && x1 - x0 == ref_x1 - ref_x0 && y1 - y0 == ref_y1 - ref_y0,
                  at + "at the screen position of the sprite, one size: top left (" + std::to_string(x0) + ", " + std::to_string(y0) + ") expected (" + std::to_string(sx + ref_x0 - 100) + ", " + std::to_string(sy + ref_y0 - 100) + ")");
        }
    }

    // the tile grid overlay (a debug aid, with text) is drawn on the screen at the zoom, inside the view; the zoom 1 pass through the target gives the direct picture
    {
        PixelRig grid_rig(arc, "GAUNTLET", kWideW, kWideH);
        if (grid_rig.ok) {
            const LayoutRect v = grid_rig.layout.view();
            auto shoot_grid = [&](bool grid) {
                grid_rig.renderer.begin_frame();
                grid_rig.renderer.render_world(grid_rig.world, grid_rig.engine.grid(), -1, {}, false, grid, v.x + 200, v.y + 150, -1, 0.0f);
                return grid_rig.read();
            };
            grid_rig.look(300.0, 260.0, 1.0f);
            grid_rig.renderer.set_force_world_target(false);
            const Picture direct = shoot_grid(true);
            grid_rig.renderer.set_force_world_target(true);
            const Picture target = shoot_grid(true);
            grid_rig.renderer.set_force_world_target(false);
            check(differ(direct, target, LayoutRect{0, 0, direct.w, direct.h}) == 0, "the tile grid overlay of the zoom 1 is the same through the offscreen target");
            for (const float z : {0.5f, 2.0f}) {
                grid_rig.look(300.0, 260.0, z);
                const Picture plain = shoot_grid(false);
                const Picture with_grid = shoot_grid(true);
                check(differ(plain, with_grid, v) > 1500 && outside_not_black(with_grid, v) == 0, std::string("the tile grid overlay draws lines and a badge in the view at ") + zoom::level_name(z) + ", nothing outside it");
            }
        }
    }

    // the static layers alone (render_map_layers): the zoom 2 is the enlargement of the zoom 1 here too
    {
        PixelRig layers(arc, "GAUNTLET", kWideW, kWideH);
        if (layers.ok) {
            const LayoutRect v = layers.layout.view();
            layers.look(300.0, 260.0, 1.0f);
            layers.renderer.begin_frame();
            layers.renderer.render_map_layers(layers.engine.grid(), &layers.world);
            const Picture direct = layers.read();
            layers.look(300.0, 260.0, 2.0f);
            layers.renderer.begin_frame();
            layers.renderer.render_map_layers(layers.engine.grid(), &layers.world);
            const Picture zoomed = layers.read();
            check(differ(zoomed, enlarge2(direct, v, false, false), v) == 0 && outside_not_black(zoomed, v) == 0, "render_map_layers at the zoom 2 is the enlargement of the zoom 1 picture");
            check(layers.cam().zoom == 2.0f, "render_map_layers leaves the camera at the zoom 2");
            // ... and without a target it draws the zoom 1 picture and keeps the zoom, as the world pass does
            layers.renderer.set_fail_world_target(true);
            layers.renderer.begin_frame();
            layers.renderer.render_map_layers(layers.engine.grid(), &layers.world);
            layers.renderer.set_fail_world_target(false);
            const Picture fallback = layers.read();
            check(differ(fallback, direct, v) == 0 && layers.cam().zoom == 2.0f, "render_map_layers without a target draws the zoom 1 picture and keeps the camera's zoom");
        }
    }

    // a picture inside a bigger canvas (the classic picture in the 16:9 canvas): the zoomed world lands in the picture's view, and nothing outside it
    {
        PixelRig inset(arc, "GAUNTLET", kWideW, kWideH);
        if (inset.ok) {
            inset.renderer.set_layout(ScreenLayout::classic());
            inset.renderer.set_picture(LayoutRect{160, 30, 640, 480});
            inset.layout = ScreenLayout::classic();
            const LayoutRect v{160 + 16, 30 + 21, 442, 440};
            std::vector<uint32_t> ids2;
            fill_scene(inset, arc, 300, 260, 442, 440, ids2);
            inset.look(300.0, 260.0, 1.0f);
            const Picture direct = inset.shoot(static_cast<int32_t>(ids2.front()), ids2, -1, false);
            inset.look(300.5, 260.0, 2.0f);
            const Picture zoomed = inset.shoot(static_cast<int32_t>(ids2.front()), ids2, -1, false);
            check(differ(zoomed, enlarge2(direct, v, true, false), v) == 0 && outside_not_black(zoomed, v) == 0, "a classic picture centred in a 16:9 canvas: the zoom 2 is the enlargement inside the picture's view, nothing outside it");
            check(inset.renderer.picture() == LayoutRect({160, 30, 640, 480}), "... and the picture is as it was");
        }
    }

    // the target is cleared at every pass: what the map does not cover is black, not what an earlier frame left there (the same target is used for another map)
    {
        PixelRig dirty(arc, "GAUNTLET", kWideW, kWideH);
        if (dirty.ok) {
            const LayoutRect v = dirty.layout.view();
            dirty.look(300.0, 260.0, 0.5f);
            const Picture first = dirty.shoot();
            check(differ(first, first, v) == 0 && first.at(v.x + 5, v.y + 5)[0] + first.at(v.x + 5, v.y + 5)[1] + first.at(v.x + 5, v.y + 5)[2] > 0, "(a zoom 0.5 frame of GAUNTLET fills the view with terrain)");
            check(dirty.switch_map("TINY"), "(TINY is loaded into the same renderer)");
            dirty.look(0.0, 0.0, 0.5f);
            const Picture second = dirty.shoot();
            bool black_sides = true;
            for (int32_t y = v.y; y < v.bottom(); ++y) {
                for (int32_t x = v.x; x < v.x + 133; ++x) black_sides = black_sides && second.at(x, y)[0] == 0 && second.at(x, y)[1] == 0 && second.at(x, y)[2] == 0;
                for (int32_t x = v.x + 133 + 496; x < v.right(); ++x) black_sides = black_sides && second.at(x, y)[0] == 0 && second.at(x, y)[1] == 0 && second.at(x, y)[2] == 0;
            }
            check(black_sides, "the black at the sides of a centred map is not what the last frame of another map left in the target");
        }
    }

    // map_view_rect outside a pass at a zoom: the part of the view that the map covers, on the screen
    {
        PixelRig big(arc, "GAUNTLET", kWideW, kWideH);
        PixelRig small(arc, "TINY", kWideW, kWideH);
        if (big.ok && small.ok) {
            const LayoutRect v = big.layout.view();
            for (const float z : {0.5f, 1.0f, 2.0f}) {
                big.look(300.0, 260.0, z);
                check(big.renderer.map_view_rect(60, 60) == v, std::string("a map that covers the view: the whole view at ") + zoom::level_name(z));
                big.look(1.0e9, 1.0e9, z);
                check(big.renderer.map_view_rect(60, 60) == v, std::string("... also from the far corner at ") + zoom::level_name(z));
            }
            small.look(0.0, 0.0, 0.5f);
            const LayoutRect r = small.renderer.map_view_rect(31, 31);
            check(r == LayoutRect({v.x + 133, v.y + 2, 496, 496}), "TINY at 0.5 covers 496 x 496 screen pixels, centred in the view: (" + std::to_string(r.x) + ", " + std::to_string(r.y) + ", " + std::to_string(r.w) + " x " + std::to_string(r.h) + ")");
            small.look(0.0, 0.0, 1.0f);
            const LayoutRect r1 = small.renderer.map_view_rect(31, 31);
            check(r1 == LayoutRect({v.x + 0, v.y + 0, 762, 500}), "TINY at 1 is larger than the view in x and y (992 px): the whole view");
            small.look(0.0, 0.0, 2.0f);
            check(small.renderer.map_view_rect(31, 31) == v, "TINY at 2: the whole view");
        }
    }

    // the tile grid overlay is on the screen at the zoom: its outlines are the tiles scaled by it (a tile of 32 world pixels is 16 screen pixels at 0.5 and 64 at 2)
    {
        PixelRig grid_rig(arc, "GAUNTLET", kWideW, kWideH);
        if (grid_rig.ok) {
            const LayoutRect v = grid_rig.layout.view();
            auto shoot_grid = [&](bool grid) {
                grid_rig.renderer.begin_frame();
                grid_rig.renderer.render_world(grid_rig.world, grid_rig.engine.grid(), -1, {}, false, grid, v.x + 700, v.y + 450, -1, 0.0f);       // (the pointer is far from the rows tested)
                return grid_rig.read();
            };
            for (const float z : {0.5f, 2.0f}) {
                grid_rig.look(320.0, 256.0, z);                          // (a tile boundary: the grid's lines are on whole screen pixels)
                const Picture plain = shoot_grid(false);
                const Picture lined = shoot_grid(true);
                const int32_t ts = static_cast<int32_t>(32.0f * z);
                // a row 8 pixels below a tile's top, far from its label: the outlines' pixels are the tile's left and right edge columns
                const int32_t row = v.y + (z == 2.0f ? 2 * 64 + 8 : 3 * 16 + 8);
                int lines_right = 0;
                int lines_wrong = 0;
                for (int32_t x = v.x + 2; x < v.right() - 2 - ts; ++x) {
                    const bool changed = std::memcmp(plain.at(x, row), lined.at(x, row), 3) != 0;
                    const bool on_edge = ((x - v.x) % ts == 0) || ((x - v.x) % ts == ts - 1);
                    if (changed && on_edge) ++lines_right;
                    if (changed != on_edge) ++lines_wrong;
                }
                check(lines_right > 8 && lines_wrong == 0, std::string("at ") + zoom::level_name(z) + " the grid's outlines are at every " + std::to_string(ts) + "th screen pixel and only there: " + std::to_string(lines_right) + " lines, " + std::to_string(lines_wrong) + " other changes");
            }
        }
    }
}

// =====================================================================================================================================================
// HUD: the pointer, the orders, the rubber band, the minimap, the edge scroll and the radar's frame at a zoom
// =====================================================================================================================================================

void test_hud_cursor(const assets::AssetArchive& arc) {
    group("cursor", "the cursor under the pointer follows the world at every zoom, pixel for pixel");
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide);
        const LayoutRect view = rig.layout.view();
        for (const float z : zooms_for(wide)) {
            rig.hud.select_ant(rig.mine);
            const sim::AntSnapshot& foe = rig.ant(rig.foe);
            const sim::AntSnapshot& mine = rig.ant(rig.mine);
            rig.camera.zoom = z;
            rig.camera.center_on(foe.px - 80, foe.py, 60, 60);
            // the sweep: the foe's hit box and its surroundings, and the own worker's
            int attack = 0;
            int select = 0;
            int move = 0;
            int wrong = 0;
            for (int32_t sy = view.y + 10; sy < view.bottom() - 10; sy += 1) {
                for (int32_t sx = view.x + 10; sx < view.right() - 10; sx += (wide ? 2 : 1)) {
                    const int32_t wx = world_under_f(rig.camera.x, z, sx - view.x);
                    const int32_t wy = world_under_f(rig.camera.y, z, sy - view.y);
                    bool in_foe = false;                                                     // (every ant of the world: a level that shows the whole map has the far ants in view too)
                    bool in_mine = false;
                    for (const auto& a : rig.world().ants) {
                        const bool in = wx >= a.px - 20 && wx < a.px + 20 && wy >= a.py - 32 && wy < a.py + 16;
                        if (a.player_id == mine.player_id) in_mine = in_mine || in;
                        else in_foe = in_foe || in;
                    }
                    const CursorType want = in_foe ? CursorType::Attack : in_mine ? CursorType::Select : CursorType::Move;
                    const CursorType got = rig.hud.evaluate_cursor(sx, sy, rig.world(), rig.sim.grid(), rig.camera);
                    if (got != want && wrong == 0) std::fprintf(stderr, "  (first wrong cursor: screen (%d, %d) world (%d, %d) got %d want %d, camera (%g, %g) zoom %g, foe at (%d, %d), mine at (%d, %d))\n", sx, sy, wx, wy, static_cast<int>(got), static_cast<int>(want),
                                                                static_cast<double>(rig.camera.x), static_cast<double>(rig.camera.y), static_cast<double>(z), foe.px, foe.py, mine.px, mine.py);
                    if (got != want) ++wrong;
                    attack += got == CursorType::Attack;
                    select += got == CursorType::Select;
                    move += got == CursorType::Move;
                }
            }
            const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": ";
            check(wrong == 0, at + "every pixel's cursor is the one of the world under it: " + std::to_string(wrong) + " wrong");
            const double box = 40.0 * 48.0 * static_cast<double>(z) * static_cast<double>(z) * 0.7 / (wide ? 2.0 : 1.0);           // (most of a hit box's pixels: the sweep's step is 2 across in the wide view)
            check(attack > box && select > box && move > 1000, at + "the sweep meets the attack, select and move cursors (" + std::to_string(attack) + ", " + std::to_string(select) + ", " + std::to_string(move) + ")");
            // the hit box in screen pixels is 40 x 48 world pixels times the zoom
            if (z == 0.5f || z == 1.0f || z == 2.0f) check(attack == static_cast<int>(std::lround(40.0 * z)) * static_cast<int>(std::lround(48.0 * z)) / (wide ? 2 : 1) || wide, at + "the attack area is the foe's hit box scaled by the zoom: " + std::to_string(attack));
            else check(wide || std::fabs(attack - 40.0 * 48.0 * static_cast<double>(z) * z) <= (40.0 + 48.0) * z + 2.0, at + "the attack area is the foe's hit box scaled by the zoom, to a pixel's edge: " + std::to_string(attack));
        }
    }
}

void test_hud_orders(const assets::AssetArchive& arc) {
    group("orders", "a click orders the tile under it, also at the first and the last pixel of the view; the click marker is at the world point");
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide);
        const LayoutRect view = rig.layout.view();
        for (const float z : zooms_for(wide)) {
            const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": ";
            struct Spot {
                double ox, oy;
                int32_t sx, sy;
                const char* what;
            };
            std::vector<Spot> spots;
            // the corners and the middle of the view from a camera in the middle of the map, then the corners of the map from the cameras that sit in them
            for (const auto& p : {std::pair<int32_t, int32_t>{view.x, view.y}, std::pair<int32_t, int32_t>{view.right() - 1, view.y}, std::pair<int32_t, int32_t>{view.x, view.bottom() - 1},
                                  std::pair<int32_t, int32_t>{view.right() - 1, view.bottom() - 1}, std::pair<int32_t, int32_t>{view.x + view.w / 2, view.y + view.h / 2}}) {
                spots.push_back(Spot{600.0, 700.0, p.first, p.second, "the view's edge pixels"});
                spots.push_back(Spot{0.0, 0.0, p.first, p.second, "from the map's top left corner"});
                spots.push_back(Spot{1.0e9, 1.0e9, p.first, p.second, "from the map's bottom right corner"});
            }
            for (int32_t sy = view.y + 3; sy < view.bottom(); sy += 61) {
                for (int32_t sx = view.x + 5; sx < view.right(); sx += 67) spots.push_back(Spot{700.25, 650.5, sx, sy, "a grid of pixels"});
            }
            int checked = 0;
            for (const Spot& sp : spots) {
                rig.look(sp.ox, sp.oy, z);
                rig.hud.select_ant(rig.mine);
                rig.sink.commands.clear();
                rig.markers.clear();
                const int32_t wx = world_under_f(rig.camera.x, z, sp.sx - view.x);
                const int32_t wy = world_under_f(rig.camera.y, z, sp.sy - view.y);
                // (a spot that has an ant under it is a select or an attack, not an order: the far ants and the two near ones are away from every spot of these cameras, except the corner
                // cameras: the ants of the corners are skipped)
                bool ant_there = false;
                for (const auto& a : rig.world().ants) {
                    ant_there = ant_there || (wx >= a.px - 20 - 32 && wx < a.px + 20 + 32 && wy >= a.py - 32 - 32 && wy < a.py + 16 + 32);
                }
                if (ant_there) continue;
                rig.click(sp.sx, sp.sy);
                ++checked;
                const std::string where = at + sp.what + " (" + std::to_string(sp.sx) + ", " + std::to_string(sp.sy) + ")";
                check(rig.sink.commands.size() == 1 && rig.sink.commands[0].type == sim::CommandType::GroupMove, where + ": one move order");
                if (rig.sink.commands.size() == 1) check(rig.sink.commands[0].tile_x == wx / 32 && rig.sink.commands[0].tile_y == wy / 32, where + ": to the tile of the world pixel under it: (" + std::to_string(wx / 32) + ", " + std::to_string(wy / 32) + ")");
                check(rig.markers.size() == 1 && rig.markers[0].first == wx && rig.markers[0].second == wy, where + ": the click marker is at the world pixel (" + std::to_string(wx) + ", " + std::to_string(wy) + ")");
                // the right button does the same (it acts at its release)
                rig.sink.commands.clear();
                rig.markers.clear();
                rig.click(sp.sx, sp.sy, SDL_BUTTON_RIGHT);
                check(rig.sink.commands.size() == 1 && rig.sink.commands[0].tile_x == wx / 32 && rig.sink.commands[0].tile_y == wy / 32, where + ": the right button orders the same tile");
                check(rig.markers.size() == 1 && rig.markers[0].first == wx && rig.markers[0].second == wy, where + ": and marks the same world pixel");
            }
            check(checked > 30, at + "the clicks that were made: " + std::to_string(checked));
            // an order at the foe: an attack at its tile (the pointer at an edge of its hit box)
            rig.look(0.0, 0.0, z);
            rig.camera.center_on(rig.ant(rig.foe).px, rig.ant(rig.foe).py, 60, 60);
            for (const uint8_t button : {static_cast<uint8_t>(SDL_BUTTON_LEFT), static_cast<uint8_t>(SDL_BUTTON_RIGHT)}) {
                const char* const which = button == SDL_BUTTON_LEFT ? "left" : "right";
                int attacks = 0;
                int expected = 0;
                for (int32_t sy = view.y + view.h / 2 - 60; sy < view.y + view.h / 2 + 60; ++sy) {
                    for (int32_t sx = view.x + view.w / 2 - 60; sx < view.x + view.w / 2 + 60; ++sx) {
                        const int32_t wx = world_under_f(rig.camera.x, z, sx - view.x);
                        const int32_t wy = world_under_f(rig.camera.y, z, sy - view.y);
                        const auto& foe = rig.ant(rig.foe);
                        const bool in = wx >= foe.px - 20 && wx < foe.px + 20 && wy >= foe.py - 32 && wy < foe.py + 16;
                        rig.hud.select_ant(rig.mine);
                        rig.sink.commands.clear();
                        rig.click(sx, sy, button);
                        const bool attacked = rig.sink.commands.size() == 1 && rig.sink.commands[0].type == sim::CommandType::GroupAttack;
                        attacks += attacked;
                        expected += in;
                        if (attacked != in) check(false, at + "an attack with the " + which + " button at (" + std::to_string(sx) + ", " + std::to_string(sy) + ") is the foe's hit box under the pointer");
                    }
                }
                check(attacks == expected && attacks > 40.0 * 48.0 * static_cast<double>(z) * static_cast<double>(z) * 0.7, at + "the " + which + " button orders an attack exactly where the foe's hit box is: " + std::to_string(attacks) + " of " + std::to_string(expected));
            }
        }
    }
}

void test_hud_rubber_band(const assets::AssetArchive& arc) {
    group("band", "the rubber band selects the ants whose hit box overlaps the world that it covers, at every zoom");
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide);
        const LayoutRect view = rig.layout.view();
        for (const float z : zooms_for(wide)) {
            const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": ";
            const sim::AntSnapshot& target = rig.ant(rig.mine);
            rig.camera.zoom = z;
            rig.camera.center_on(target.px, target.py, 60, 60);
            // the screen position of the ant's hit box: left edge world px - 20, right px + 20, top py - 32, bottom py + 16
            auto expected_selected = [&](int32_t left, int32_t top, int32_t right, int32_t bottom) {
                const int32_t wl = world_under_f(rig.camera.x, z, left - view.x);
                const int32_t wt = world_under_f(rig.camera.y, z, top - view.y);
                const int32_t wr = edge_under_f(rig.camera.x, z, right - view.x);
                const int32_t wb = edge_under_f(rig.camera.y, z, bottom - view.y);
                return std::max(wl, target.px - 20) < std::min(wr, target.px + 20) && std::max(wt, target.py - 32) < std::min(wb, target.py + 16);
            };
            int selected = 0;
            int wrong = 0;
            int tested = 0;
            // bands from a fixed corner far from the ant to the corner that moves across the ant's box on every side (and bands that start inside the box)
            struct Band {
                int32_t sx, sy;
            };
            const int32_t cx = view.x + view.w / 2;
            const int32_t cy = view.y + view.h / 2;
            const int32_t reach = static_cast<int32_t>(std::lround(60.0 * z)) + 8;
            for (const Band b : {Band{view.x + 12, view.y + 12}, Band{view.right() - 14, view.bottom() - 14}, Band{view.right() - 14, view.y + 12}, Band{view.x + 12, view.bottom() - 14}, Band{cx, cy}}) {
                for (int32_t ex = cx - reach; ex <= cx + reach; ex += 1) {
                    for (int32_t ey : {cy - reach, cy - reach / 2, cy, cy + reach / 2, cy + reach}) {
                        rig.hud.clear_selection();
                        rig.hud.handle_mouse_down(b.sx, b.sy, SDL_BUTTON_LEFT, rig.sim, rig.camera);
                        rig.hud.handle_mouse_motion(ex, ey, rig.sim, rig.camera);
                        rig.hud.handle_mouse_up(ex, ey, SDL_BUTTON_LEFT, rig.sim, rig.camera);
                        // the band: the pointer kept 1 px inside the view, a stationary press (no extent on an axis) a 2 x 2 dot
                        int32_t left = std::min(b.sx, std::clamp(ex, view.x + 1, view.right() - 1));
                        int32_t right = std::max(b.sx, std::clamp(ex, view.x + 1, view.right() - 1));
                        int32_t top = std::min(b.sy, std::clamp(ey, view.y + 1, view.bottom() - 1));
                        int32_t bottom = std::max(b.sy, std::clamp(ey, view.y + 1, view.bottom() - 1));
                        if (right == left) { --left; ++right; }
                        if (bottom == top) { --top; ++bottom; }
                        // (a band of 4 pixels or less in both directions is a click, not a band: skip those, the click is checked above)
                        if (right - left <= 4 && bottom - top <= 4) continue;
                        const bool want = expected_selected(left, top, right, bottom);
                        bool got = false;
                        for (uint32_t id : rig.hud.get_selected_ant_ids()) got = got || id == rig.mine;
                        ++tested;
                        selected += got;
                        if (got != want) ++wrong;
                    }
                }
            }
            check(wrong == 0, at + "the band selects the ant exactly when its world rectangle overlaps the hit box: " + std::to_string(wrong) + " wrong of " + std::to_string(tested));
            check(selected > 50 && selected < tested - 50, at + "the bands sweep across the box's edges (" + std::to_string(selected) + " of " + std::to_string(tested) + ")");
        }
    }
}

void test_hud_minimap_and_scroll(const assets::AssetArchive& arc) {
    group("scroll", "the minimap centres the world that is seen; the edge scroll moves the same distance on the screen; the arrows show where the view can move");
    constexpr uint32_t kTiles = 125;                                  // a map of 4000 px: the view is far from its edges at every zoom, so no clamp bends the distances
    constexpr double kMapPx = 4000.0;
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide, kTiles);
        const LayoutRect view = rig.layout.view();
        const LayoutRect mini = rig.layout.minimap();
        // 1. the minimap: a press, the 50 ms ticks, the middle of the world that is seen is the point under the pointer
        for (const float z : zooms_for(wide, kTiles)) {
            if (z < 0.25f) continue;                                          // (the world seen at a smaller zoom is most of this 4000 px map: the model's own test (test_zoom_model) has the tiny levels on a bigger one)
            const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": ";
            int tested = 0;
            int centred = 0;
            for (int32_t my = mini.y + 3; my < mini.bottom(); my += 4) {
                for (int32_t mx = mini.x + 3; mx < mini.right(); mx += 4) {
                    rig.look(0.0, 0.0, z);
                    rig.hud.handle_mouse_down(mx, my, SDL_BUTTON_LEFT, rig.sim, rig.camera);
                    for (int i = 0; i < 4; ++i) rig.hud.input_tick(rig.camera, kTiles, kTiles, mx, my);
                    rig.hud.handle_mouse_up(mx, my, SDL_BUTTON_LEFT, rig.sim, rig.camera);
                    const double want_x = std::trunc((mx - mini.x) * (kMapPx / mini.w));
                    const double want_y = std::trunc((my - mini.y) * (kMapPx / mini.h));
                    const double half_w = view.w / static_cast<double>(z) / 2.0;
                    const double half_h = view.h / static_cast<double>(z) / 2.0;
                    if (want_x >= half_w && want_x <= kMapPx - half_w && want_y >= half_h && want_y <= kMapPx - half_h) {
                        ++tested;
                        const double cxw = rig.camera.x + half_w;
                        const double cyw = rig.camera.y + half_h;
                        if (std::fabs(cxw - want_x) <= 2.0 * zoom::grid(z) + 1e-9 && std::fabs(cyw - want_y) <= 2.0 * zoom::grid(z) + 1e-9) ++centred;           // (the minimap works in whole screen pixels: two of them)
                    }
                }
            }
            check(tested > (z < 0.5f ? 20 : 100) && centred == tested, at + "a press on the minimap centres the world that is seen on the point under the pointer: " + std::to_string(centred) + " of " + std::to_string(tested));
            // a right click on the minimap orders the point's tile, whatever the zoom
            rig.look(600.0, 600.0, z);
            rig.hud.select_ant(rig.mine);
            rig.sink.commands.clear();
            rig.click(mini.x + 50, mini.y + 40, SDL_BUTTON_RIGHT);
            check(rig.sink.commands.size() == 1 && rig.sink.commands[0].tile_x == static_cast<int16_t>(std::trunc(50.0 * (kMapPx / mini.w)) / 32) && rig.sink.commands[0].tile_y == static_cast<int16_t>(std::trunc(40.0 * (kMapPx / mini.h)) / 32),
                  at + "a right click on the minimap orders the tile of the minimap's point, the same at every zoom");
        }

        // 2. the edge scroll: the distance on the screen is the zoom 1 distance, from a camera far from the map's edges
        const int32_t pw = rig.layout.width;
        const int32_t ph = rig.layout.height;
        std::vector<std::pair<int32_t, int32_t>> ring;
        for (int32_t x = 0; x < pw; x += (x < 30 || x > pw - 30) ? 1 : 7) {
            for (int32_t y : {0, 1, 2, 3, 4, 6, 9, 11, ph - 12, ph - 10, ph - 7, ph - 5, ph - 4, ph - 3, ph - 2, ph - 1}) ring.emplace_back(x, y);
        }
        for (int32_t y = 0; y < ph; y += (y < 30 || y > ph - 30) ? 1 : 5) {
            for (int32_t x : {0, 1, 2, 3, 4, 6, 9, 11, pw - 12, pw - 10, pw - 7, pw - 5, pw - 4, pw - 3, pw - 2, pw - 1}) ring.emplace_back(x, y);
        }
        for (int32_t rate : {0, 50, 99}) {
            for (const float z : zooms_for(wide, kTiles)) {
                if (z < 0.4f) continue;                                       // (a step of up to 109 screen pixels from (700, 700) must not reach the far edge of the 4000 px map)
                const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ", rate " + std::to_string(rate) + ": ";
                int differences = 0;
                int moved = 0;
                for (const auto& p : ring) {
                    // (the options' scroll rate is the HUD's setting: an options state of its own is not needed, the default is 50 and `rate` is only used by the model; the test sets it
                    // through the stored option)
                    ConfigStore store;
                    store.set_int("Scroll Speed", rate);
                    rig.hud.options().load(store);
                    HudRig& r = rig;
                    r.look(700.0, 700.0, 1.0f);
                    const float bx = r.camera.x;
                    const float by = r.camera.y;
                    r.hud.input_tick(r.camera, kTiles, kTiles, p.first, p.second);
                    const double base_dx = r.camera.x - bx;
                    const double base_dy = r.camera.y - by;
                    r.look(700.0, 700.0, z);
                    const float zx = r.camera.x;
                    const float zy = r.camera.y;
                    r.hud.input_tick(r.camera, kTiles, kTiles, p.first, p.second);
                    const double dx = (r.camera.x - zx) * z;
                    const double dy = (r.camera.y - zy) * z;
                    if (std::fabs(dx - base_dx) > 3e-3 || std::fabs(dy - base_dy) > 3e-3) ++differences;                 // (the origin is a float: 13000 world pixels are a thousandth of a pixel)
                    if (base_dx != 0.0 || base_dy != 0.0) ++moved;
                }
                check(differences == 0, at + "the distance on the screen is the zoom 1 distance at every pixel of the picture's edge: " + std::to_string(differences) + " of " + std::to_string(ring.size()) + " differ");
                check(moved > 200, at + "the strips scroll (" + std::to_string(moved) + " pixels)");
            }
        }

        // 3. the scroll arrows show where the view can move (at the zoom the world that is seen is view / zoom)
        {
            const CursorType kArrows[8] = {CursorType::ScrollN, CursorType::ScrollNE, CursorType::ScrollE, CursorType::ScrollSE, CursorType::ScrollS, CursorType::ScrollSW, CursorType::ScrollW, CursorType::ScrollNW};
            const std::pair<int32_t, int32_t> at_dir[8] = {{pw / 2, 4}, {pw - 4, 4}, {pw - 4, ph / 2}, {pw - 4, ph - 4}, {pw / 2, ph - 4}, {4, ph - 4}, {4, ph / 2}, {4, 4}};
            for (const float z : zooms_for(wide, kTiles)) {
                // the origin in whole screen pixels and the map in whole screen pixels at the zoom: the view can move while it has a pixel of the map left on that side
                const double map_screen = std::floor(kMapPx * static_cast<double>(z) + 1e-9);
                for (const auto& cam_at : {std::pair<double, double>{0.0, 0.0}, std::pair<double, double>{1.0e9, 1.0e9}, std::pair<double, double>{0.0, 1.0e9}, std::pair<double, double>{1.0e9, 0.0}, std::pair<double, double>{500.0, 500.0}}) {
                    rig.look(cam_at.first, cam_at.second, z);
                    const double ox = rig.camera.x;
                    const double oy = rig.camera.y;
                    const double ox_s = std::round(ox * static_cast<double>(z));
                    const double oy_s = std::round(oy * static_cast<double>(z));
                    const bool west = ox_s > 0.0;
                    const bool east = ox_s + view.w < map_screen;
                    const bool north = oy_s > 0.0;
                    const bool south = oy_s + view.h < map_screen;
                    const bool can[8] = {north, east || north, east, east || south, south, west || south, west, west || north};
                    for (int d = 0; d < 8; ++d) {
                        rig.hud.clear_selection();
                        const CursorType got = rig.hud.evaluate_cursor(at_dir[d].first, at_dir[d].second, rig.world(), rig.sim.grid(), rig.camera);
                        check((got == kArrows[d]) == can[d], std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + " from the origin (" + num(ox) + ", " + num(oy) + "): the arrow " + std::to_string(d) +
                                                                  " shows exactly when the view can move that way (" + (can[d] ? "can" : "cannot") + ")");
                    }
                }
            }
        }
    }
}

void test_hud_radar_frame(const assets::AssetArchive& arc) {
    group("radar", "the minimap's frame is the world that is seen at the zoom");
    for (const bool wide : {false, true}) {
        HudRig rig(arc, wide);
        const LayoutRect view = rig.layout.view();
        const LayoutRect mini = rig.layout.minimap();
        for (const float z : zooms_for(wide)) {
            for (const auto& cam_at : {std::pair<double, double>{0.0, 0.0}, std::pair<double, double>{333.0, 479.0}, std::pair<double, double>{1.0e9, 1.0e9}, std::pair<double, double>{500.5, 700.5}}) {
                rig.look(cam_at.first, cam_at.second, z);
                RectRenderer rec;
                rig.hud.render(rec, rig.arc, rig.world(), rig.camera);
                const RectRenderer::Rect* frame = nullptr;
                for (const auto& r : rec.frames) {
                    if (r.colour.r == 251 && r.colour.g == 251 && r.colour.b == 255) frame = &r;
                }
                const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + " from (" + num(rig.camera.x) + ", " + num(rig.camera.y) + "): ";
                check(frame != nullptr, at + "the HUD draws the minimap's frame");
                if (frame == nullptr) continue;
                // the frame: the seen world scaled to the image, plus one; at the image's far edges it is moved inside; the origin scaled down
                const int64_t world = 1920;
                const int32_t seen_w = seen_f(view.w, z);
                const int32_t seen_h = seen_f(view.h, z);
                const int32_t frame_w = static_cast<int32_t>(seen_w * static_cast<int64_t>(mini.w) / world) + 1;
                const int32_t frame_h = static_cast<int32_t>(seen_h * static_cast<int64_t>(mini.h) / world) + 1;
                int32_t fl = mini.x + static_cast<int32_t>(static_cast<int32_t>(rig.camera.x) * static_cast<int64_t>(mini.w) / world);
                int32_t ft = mini.y + static_cast<int32_t>(static_cast<int32_t>(rig.camera.y) * static_cast<int64_t>(mini.h) / world);
                int32_t fr = fl + frame_w;
                int32_t fb = ft + frame_h;
                if (fr >= mini.x + mini.w) { fr = mini.x + mini.w; fl = fr - frame_w; }
                if (fb >= mini.y + mini.h) { fb = mini.y + mini.h; ft = fb - frame_h; }
                if (seen_w >= world) { fl = mini.x; fr = mini.x + mini.w; }                // the view shows the map's whole width (the zoom-out's limit): the frame is the image's whole width
                if (seen_h >= world) { ft = mini.y; fb = mini.y + mini.h; }
                check(frame->x == fl && frame->y == ft && frame->w == fr - fl && frame->h == fb - ft,
                      at + "the frame is (" + std::to_string(fl) + ", " + std::to_string(ft) + ", " + std::to_string(fr - fl) + " x " + std::to_string(fb - ft) + "), the HUD drew (" + std::to_string(frame->x) + ", " + std::to_string(frame->y) + ", " +
                          std::to_string(frame->w) + " x " + std::to_string(frame->h) + ")");
            }
        }
        // a map that the view shows whole on an axis: the frame is the image's whole width / height there
        {
            sim::SimulationEngine small;
            small.init_test_world(12, 12, 1, 600000);
            HUD hud;
            hud.init(0);
            hud.set_layout(rig.layout);
            ViewportCamera cam;
            cam.set_view(view);
            cam.centre_small_maps = wide;
            for (const float z : kZooms) {
                cam.zoom = z;
                cam.set_origin(0.0, 0.0, 12, 12);
                RectRenderer rec;
                hud.render(rec, rig.arc, small.get_world_state(), cam);
                const RectRenderer::Rect* frame = nullptr;
                for (const auto& r : rec.frames) {
                    if (r.colour.r == 251 && r.colour.g == 251 && r.colour.b == 255) frame = &r;
                }
                const bool whole = z <= 1.0f;                              // (a 384 px map is whole in 442 x 440 and 762 x 500, not in the 221 x 220 or 381 x 250 of the zoom 2)
                check(frame != nullptr && (whole ? (frame->x == mini.x && frame->y == mini.y && frame->w == mini.w && frame->h == mini.h) : (frame->w <= mini.w && frame->h < mini.h)),
                      std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + ": a 12 x 12 map that the view shows whole has the whole image as its frame, one that it does not has a part");
            }
        }
    }
}

void test_hud_gating(const assets::AssetArchive& arc) {
    group("gating", "the wheel and the middle button may zoom only when nothing holds the mouse and no dialog is open");
    HudRig rig(arc, true);
    check(rig.hud.view_zoom_allowed(), "a HUD with nothing open allows the zoom");
    {
        HudRig r(arc, true);
        r.hud.open_options();
        check(!r.hud.view_zoom_allowed(), "not while the options window is open");
    }
    {
        HudRig r(arc, true);
        r.hud.open_quick_help();
        check(!r.hud.view_zoom_allowed(), "not while the quick help is open");
    }
    {
        HudRig r(arc, true);
        r.hud.open_quit_dialog();
        check(!r.hud.view_zoom_allowed(), "not while the quit dialog is open");
    }
    {
        HudRig r(arc, true);
        r.hud.start_match_modal();
        check(!r.hud.view_zoom_allowed(), "not while the \"get ready\" dialog is open");
        r.hud.dismiss_match_start_modal();
        check(r.hud.view_zoom_allowed(), "... and again after it");
    }
    {
        HudRig r(arc, true);
        r.sim.form_alliance(0, 2);
        r.sim.tick();
        r.hud.request_team_up(r.sim, 1);                              // with an ally the offer asks first: "Doing this will break your team ..."
        check(r.hud.alliance_dialog() == HUD::AllianceDialog::BreakConfirm && !r.hud.view_zoom_allowed(), "not while an alliance dialog is open");
        r.hud.handle_key_down('n', r.sim, r.camera);                  // No
        check(r.hud.alliance_dialog() == HUD::AllianceDialog::None && r.hud.view_zoom_allowed(), "... and again after it is answered");
    }
    {   // a press on the map is a rubber band; on the minimap a drag; on a button a captured button; the right button on the map a capture; the chat log a drag
        HudRig r(arc, true);
        const LayoutRect view = r.layout.view();
        const LayoutRect mini = r.layout.minimap();
        r.hud.handle_mouse_down(view.x + 100, view.y + 100, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(!r.hud.view_zoom_allowed(), "not during a rubber band");
        r.hud.handle_mouse_up(view.x + 100, view.y + 100, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(r.hud.view_zoom_allowed(), "... but after it");
        r.hud.handle_mouse_down(mini.x + 20, mini.y + 20, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(!r.hud.view_zoom_allowed(), "not while the minimap is held");
        r.hud.handle_mouse_up(mini.x + 20, mini.y + 20, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(r.hud.view_zoom_allowed(), "... but after it");
        r.hud.handle_mouse_down(view.x + 100, view.y + 100, SDL_BUTTON_RIGHT, r.sim, r.camera);
        check(!r.hud.view_zoom_allowed(), "not while the right button is held on the map");
        r.hud.handle_mouse_up(view.x + 100, view.y + 100, SDL_BUTTON_RIGHT, r.sim, r.camera);
        check(r.hud.view_zoom_allowed(), "... but after it");
        const UIButton help = r.hud.help_button();
        r.hud.handle_mouse_down(help.x + 3, help.y + 3, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(!r.hud.view_zoom_allowed(), "not while a button is pressed");
        r.hud.handle_mouse_up(help.x + 3, help.y + 3, SDL_BUTTON_LEFT, r.sim, r.camera);
        r.hud.close_quick_help();
        check(r.hud.view_zoom_allowed(), "... but after it (the quick help that the click opened is closed)");
        r.hud.handle_mouse_down(r.layout.chat_view().x + 10, r.layout.chat_view().y + 10, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(!r.hud.view_zoom_allowed(), "not while the chat log is dragged");
        r.hud.handle_mouse_up(r.layout.chat_view().x + 10, r.layout.chat_view().y + 10, SDL_BUTTON_LEFT, r.sim, r.camera);
        check(r.hud.view_zoom_allowed(), "... but after it");
    }
}

void test_hud_ctrl_n(const assets::AssetArchive& arc) {
    group("ctrln", "Ctrl+N and Ctrl+P scroll just far enough to show the ant's square in the world that the view shows at the zoom");
    struct Origin {
        double x, y;
        bool half_pixel;                         // (the origin of the zoom 2 may lie half a world pixel inside a world pixel)
    };
    const Origin origins[] = {{1000.0, 1000.0, false}, {0.0, 0.0, false}, {1000.5, 1000.5, true}, {0.5, 100.5, true}, {1500.0, 40.0, false}};
    int moved_right = 0;
    int moved_down = 0;
    for (const bool wide : {false, true}) {
        for (const float z : zooms_for(wide)) {
            for (const Origin& org : origins) {
                if (org.half_pixel && z != 2.0f) continue;
                for (const char key : {'n', 'p'}) {
                    HudRig rig(arc, wide);
                    const LayoutRect view = rig.layout.view();
                    rig.look(org.x, org.y, z);
                    rig.hud.clear_selection();
                    const double ox = rig.camera.x;
                    const double oy = rig.camera.y;
                    // the ant that the key picks (the search starts at the last own ant with nothing selected: 'n' takes the first of the player's ants, 'p' the one before the last)
                    std::vector<sim::AntSnapshot> own;
                    for (const auto& a : rig.world().ants) {
                        if (a.player_id == 0) own.push_back(a);
                    }
                    const sim::AntSnapshot chosen = key == 'n' ? own.front() : own[own.size() - 2];
                    rig.hud.handle_key_down(key, rig.sim, rig.camera, KMOD_CTRL);
                    const int32_t vis_w = seen_f(view.w, z);
                    const int32_t vis_h = seen_f(view.h, z);
                    const int32_t l = std::max(chosen.px - 128, 0);
                    const int32_t t = std::max(chosen.py - 128, 0);
                    const int32_t r = std::min(chosen.px + 128, 1920);
                    const int32_t b = std::min(chosen.py + 128, 1920);
                    const int32_t oix = static_cast<int32_t>(std::floor(ox));
                    const int32_t oiy = static_cast<int32_t>(std::floor(oy));
                    int32_t dx = 0;
                    int32_t dy = 0;
                    if (r > oix + vis_w) dx = r - (oix + vis_w);
                    else if (l < oix) dx = l - oix;
                    if (t < oiy) dy = t - oiy;
                    else if (b > oiy + vis_h) dy = b - (oiy + vis_h);
                    moved_right += dx > 0 ? 1 : 0;
                    moved_down += dy > 0 ? 1 : 0;
                    // (the origin goes to the lattice of the zoom and is held in [0, the map less the world that is seen], whose far end is on the lattice too)
                    const auto held = [z](double v, int32_t len) {
                        const double zz = static_cast<double>(z);
                        return std::clamp(std::floor(v * zz + 0.5) / zz, 0.0, std::floor((1920.0 - len / zz) * zz + 1e-9) / zz);
                    };
                    const double want_x = held(ox + dx, view.w);
                    const double want_y = held(oy + dy, view.h);
                    const std::string at = std::string(wide ? "wide" : "classic") + " at " + zoom::level_name(z) + " from (" + num(org.x) + ", " + num(org.y) + ") with Ctrl+" + static_cast<char>(key - 32) + ": ";
                    check(std::fabs(rig.camera.x - static_cast<float>(want_x)) < 1e-3f && std::fabs(rig.camera.y - static_cast<float>(want_y)) < 1e-3f,
                          at + "the view moves from (" + num(ox) + ", " + num(oy) + ") to (" + num(want_x) + ", " + num(want_y) + "), it went to (" + num(rig.camera.x) + ", " + num(rig.camera.y) + ")");
                    check(rig.camera.world_x == static_cast<int32_t>(std::floor(rig.camera.x)) && rig.camera.world_y == static_cast<int32_t>(std::floor(rig.camera.y)), at + "the whole parts of the origin follow it");
                    // the square is in view when it fits in the world that is seen (from a half-pixel origin the left / top edge of the square, a margin of 128 pixels around the ant, may end
                    // half a world pixel = one screen pixel outside: the scroll counts from the whole part of the origin)
                    const bool exact_level = z == 0.5f || z == 1.0f || z == 2.0f;
                    const float slack = exact_level ? (org.half_pixel ? 0.5f : 0.0f) : 1.0f + static_cast<float>(0.5 * zoom::grid(z)) + 1e-3f;          // (an origin that is not on the whole pixel: the scroll counts from its whole part, a world pixel, and the lattice rounds it by half a screen pixel)
                    if (r - l <= vis_w && b - t <= vis_h) {
                        check(rig.camera.x <= static_cast<float>(l) + slack && rig.camera.x + static_cast<float>(vis_w) + slack >= static_cast<float>(r) && rig.camera.y <= static_cast<float>(t) + slack && rig.camera.y + static_cast<float>(vis_h) + slack >= static_cast<float>(b),
                              at + "the ant's square is in the world that the view shows");
                    }
                }
            }
        }
    }
    check(moved_right > 8 && moved_down > 8, "the cases include views that have to move right and down to show the square: " + std::to_string(moved_right) + " and " + std::to_string(moved_down));
}

// =====================================================================================================================================================
// Application: the wheel, the middle button, the zoom API, fairness, the settings key, the start view, the listener
// =====================================================================================================================================================

void test_app_wheel() {
    group("wheel", "the wheel zooms towards the pointer, over the map only, and never while a dialog is open or a press holds the mouse");
    for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
        const bool wide = aspect == Aspect::Wide16x9;
        const std::string pic = wide ? "wide: " : "classic: ";
        AppRig rig(aspect);
        check(rig.ok, pic + "the application is up");
        if (!rig.ok) continue;
        Application& app = rig.app;
        const LayoutRect view = app.layout().view();
        const LayoutRect mini = app.layout().minimap();
        check(app.state() == AppState::Playing && app.zoom() == 1.0f && app.remembered_zoom() == 1.0f, pic + "a match starts at the zoom 1");
        // the levels of GAUNTLET (1920 x 1920): from 2 down to the map's width in this view, 762 / 1920 = 0.397 in the wide one, 442 / 1920 = 0.230 in the classic one
        const std::vector<float> levels = app.zoom_levels();
        const std::vector<double> want_levels = wide ? std::vector<double>{2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.84090, 0.70711, 0.59460, 0.5, 0.42045, 0.396875}
                                                     : std::vector<double>{2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.84090, 0.70711, 0.59460, 0.5, 0.42045, 0.35355, 0.29730, 0.25, 0.230208};
        bool listed = levels.size() == want_levels.size();
        for (size_t i = 0; listed && i < levels.size(); ++i) listed = std::fabs(static_cast<double>(levels[i]) - want_levels[i]) < 5e-5;
        check(listed, pic + "a local game offers the series from 2 down to the map's limit on GAUNTLET: " + std::to_string(levels.size()) + " levels");
        if (!listed || levels.size() < 9) continue;
        const float in1 = levels[3];                                         // 1.19: one notch from 1
        const float out1 = levels[5];                                        // 0.84
        const float bottom = levels.back();
        const int32_t px = view.x + 284;
        const int32_t py = view.y + 183;
        app.note_pointer(px, py);
        const auto near_world = [](const std::pair<int32_t, int32_t>& a, const std::pair<int32_t, int32_t>& b, int32_t tolerance) { return std::abs(a.first - b.first) <= tolerance && std::abs(a.second - b.second) <= tolerance; };

        // up zooms in towards the pointer: the world point under it stays under it (to the rounding of the origin to the lattice of the new zoom: a world pixel)
        const auto before = rig.world_at(px, py);
        notch(app, +1);
        check(app.zoom() == in1, pic + "a notch away zooms in to the next level, 1.19");
        check(near_world(rig.world_at(px, py), before, 1), pic + "the world point under the pointer is the same after the zoom in, to a world pixel: (" + std::to_string(before.first) + ", " + std::to_string(before.second) + ")");
        check(app.remembered_zoom() == in1, pic + "the level is remembered");
        for (int i = 2; i >= 0; --i) {                                       // one level a notch, up to 2
            notch(app, +1);
            check(app.zoom() == levels[static_cast<size_t>(i)], pic + "a notch away is the next level: " + std::to_string(static_cast<double>(levels[static_cast<size_t>(i)])));
            check(near_world(rig.world_at(px, py), before, 1), pic + "... with the same world point under the pointer");
        }
        notch(app, +1);
        check(app.zoom() == 2.0f, pic + "there is no level above 2");
        for (size_t i = 1; i <= 4; ++i) {                                    // one level a notch, back down to 1
            notch(app, -1);
            check(app.zoom() == levels[i], pic + "a notch toward is the next level out: " + std::to_string(static_cast<double>(levels[i])));
        }
        check(app.zoom() == 1.0f && near_world(rig.world_at(px, py), before, 1), pic + "a notch per level back to 1, with the same world point under the pointer");
        notch(app, -1);
        check(app.zoom() == out1, pic + "another notch toward zooms out to 0.84");
        const auto after_out = rig.world_at(px, py);
        check(near_world(after_out, before, 1), pic + "the world point under the pointer stays (within a world pixel)");
        for (size_t i = 6; i < levels.size(); ++i) {                         // and on down to the map's limit
            notch(app, -1);
            check(app.zoom() == levels[i], pic + "a notch toward is the next level out: " + std::to_string(static_cast<double>(levels[i])));
        }
        check(app.zoom() == bottom, pic + "the last level is the map's limit: " + std::to_string(static_cast<double>(bottom)));
        {   // at the limit the view shows the map's whole width and nothing outside the map
            const ViewportCamera& cam = app.renderer().camera();
            check(cam.x >= 0.0f && cam.y >= 0.0f && cam.x + static_cast<float>(view.w) / bottom <= 1920.0f + 1e-3f && cam.y + static_cast<float>(view.h) / bottom <= 1920.0f + 1e-3f && cam.x == 0.0f, pic + "at the limit the camera shows the whole width of the map and nothing beyond it: (" + std::to_string(static_cast<double>(cam.x)) + ", " + std::to_string(static_cast<double>(cam.y)) + ")");
        }
        notch(app, -1);
        check(app.zoom() == bottom, pic + "there is no level below the map's limit");
        notch(app, +1);
        check(app.zoom() == levels[levels.size() - 2], pic + "a notch away from the limit is the level above it");
        for (size_t i = levels.size() - 2; i > 4; --i) notch(app, +1);
        check(app.zoom() == 1.0f, pic + "(back at 1)");

        // natural scrolling: SDL has inverted the numbers and says so (FLIPPED); the wheel rolled away still zooms in
        notch(app, +1, true);
        check(app.zoom() == in1, pic + "a flipped event of a wheel rolled away (its numbers are negative) zooms in");
        notch(app, -1, true);
        check(app.zoom() == 1.0f, pic + "a flipped event of a wheel rolled toward zooms out");
        // the same numbers without the flag are the other way round
        app.handle_mouse_wheel(wheel_event(-1, -1.0f, false));
        check(app.zoom() == out1, pic + "the same negative numbers, not flipped, zoom out");
        app.handle_mouse_wheel(wheel_event(-1, -1.0f, true));
        check(app.zoom() == 1.0f, pic + "... and flipped they zoom in");

        // a trackpad: the precise deltas add up to one step
        app.handle_mouse_wheel(wheel_event(0, 0.4f));
        app.handle_mouse_wheel(wheel_event(0, 0.4f));
        check(app.zoom() == 1.0f, pic + "0.4 + 0.4 is not a step");
        app.handle_mouse_wheel(wheel_event(0, 0.4f));
        check(app.zoom() == in1, pic + "0.4 + 0.4 + 0.4 is one step: in");
        app.handle_mouse_wheel(wheel_event(0, -0.3f));
        app.handle_mouse_wheel(wheel_event(0, -0.3f));
        app.handle_mouse_wheel(wheel_event(0, -0.3f));
        check(app.zoom() == in1, pic + "-0.9 is not a step yet (the left-over 0.2 of the last step was dropped by the change of direction)");
        app.handle_mouse_wheel(wheel_event(0, -0.3f));
        check(app.zoom() == 1.0f, pic + "-1.2 is one step: out");
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        app.handle_mouse_wheel(wheel_event(0, 0.6f, false, 2000));
        check(app.zoom() == 1.0f, pic + "a pause of two seconds forgets half a notch: 0.6, a pause, 0.6 is no step");

        // a mouse notch that the browser reports as 120, which the page counts at 100 a step: 1.2. One level a notch, however many come in a burst: the 0.2 is not kept (it made the fifth notch
        // of a burst two levels)
        for (int i = 0; i < 5; ++i) {
            app.handle_mouse_wheel(wheel_event(-1, -1.2f, false, 80));
            check(app.zoom() == levels[static_cast<size_t>(5 + i)], pic + "notch " + std::to_string(i + 1) + " of five of 120 within 400 ms is one level out: " + std::to_string(static_cast<double>(levels[static_cast<size_t>(5 + i)])));
        }
        for (int i = 0; i < 5; ++i) {
            app.handle_mouse_wheel(wheel_event(1, 1.2f, false, 80));
            check(app.zoom() == levels[static_cast<size_t>(8 - i)], pic + "notch " + std::to_string(i + 1) + " of five of 120 the other way is one level in: " + std::to_string(static_cast<double>(levels[static_cast<size_t>(8 - i)])));
        }
        check(app.zoom() == 1.0f, pic + "five of 120 out and five in are back at 1");

        // only over the map view
        const std::vector<std::pair<std::pair<int32_t, int32_t>, std::string>> outside = {
            {{mini.x + 10, mini.y + 10}, "the minimap"},
            {{view.right() + 30, view.y + 250}, "the right panel"},
            {{view.x + 100, 8}, "the top bar"},
            {{view.x + 100, view.bottom() + 8}, "the bottom strip"},
            {{view.x - 1, view.y + 100}, "the pixel left of the view"},
            {{view.right(), view.y + 100}, "the pixel right of the view"},
            {{view.x + 100, view.y - 1}, "the pixel above the view"},
            {{view.x + 100, view.bottom()}, "the pixel below the view"}};
        for (const auto& o : outside) {
            app.note_pointer(o.first.first, o.first.second);
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "the wheel over " + o.second + " does not zoom");
        }
        for (const auto& edge : {std::pair<int32_t, int32_t>{view.x, view.y}, std::pair<int32_t, int32_t>{view.right() - 1, view.bottom() - 1}, std::pair<int32_t, int32_t>{view.x, view.bottom() - 1}, std::pair<int32_t, int32_t>{view.right() - 1, view.y}}) {
            app.note_pointer(edge.first, edge.second);
            notch(app, +1);
            check(app.zoom() == in1, pic + "the wheel over the view's corner pixel (" + std::to_string(edge.first) + ", " + std::to_string(edge.second) + ") zooms");
            notch(app, -1);
            check(app.zoom() == 1.0f, pic + "... and back");
        }
        // what a panel or a dialog got is not half a notch for the map
        app.note_pointer(px, py);
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        app.note_pointer(mini.x + 10, mini.y + 10);
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        app.note_pointer(px, py);
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        check(app.zoom() == 1.0f, pic + "a wheel event that is ignored forgets the half notch before it: 0.6, (0.6 on the minimap), 0.6 is no step");

        // not while the pointer is out of the window
        app.handle_window_event([] { SDL_WindowEvent we{}; we.type = SDL_WINDOWEVENT; we.event = SDL_WINDOWEVENT_LEAVE; return we; }());
        notch(app, +1);
        check(app.zoom() == 1.0f, pic + "the wheel does nothing while the pointer is outside the window");
        app.handle_window_event([] { SDL_WindowEvent we{}; we.type = SDL_WINDOWEVENT; we.event = SDL_WINDOWEVENT_ENTER; return we; }());
        app.note_pointer(px, py);
        notch(app, +1);
        check(app.zoom() == in1, pic + "... and zooms again when it is back");
        notch(app, -1);

        // not while a press holds the mouse: the rubber band, the minimap, a button, the right button, the chat log
        {
            app.note_pointer(px, py);
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, px, py));
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom during a rubber band");
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, px, py));
            notch(app, +1);
            check(app.zoom() == in1, pic + "... and zoom after it");
            notch(app, -1);
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, mini.x + 20, mini.y + 20));
            app.note_pointer(px, py);
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom while the minimap is held");
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, mini.x + 20, mini.y + 20));
            app.note_pointer(px, py);
            app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONDOWN, px, py));
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom while the right button is held on the map");
            app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONUP, px, py));
            const UIButton help = app.hud().help_button();
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, help.x + help.w / 2, help.y + help.h / 2));
            check(app.hud().help_button().is_pressed, pic + "(the Help button is pressed)");
            app.note_pointer(px, py);
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom while a button is pressed");
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, help.x + help.w / 2, help.y + help.h / 2));
            app.hud().close_quick_help();
            app.note_pointer(px, py);                                       // (the release left the pointer on the button)
            notch(app, +1);
            check(app.zoom() == in1, pic + "... and zoom after it");
            notch(app, -1);
        }
        // not while a dialog or a page is open: quit, options, quick help, "get ready", an alliance question
        {
            app.note_pointer(px, py);
            app.hud().open_quit_dialog();
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom with the quit dialog open");
            app.hud().close_quit_dialog();
            app.hud().open_options();
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom with the options window open");
            app.hud().handle_key_down(SDLK_RETURN, app.sim(), app.renderer().camera());     // Enter closes it
            app.hud().open_quick_help();
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom with the quick help open");
            app.hud().close_quick_help();
            app.hud().start_match_modal();
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom with the \"get ready\" dialog open");
            app.hud().dismiss_match_start_modal();
            app.sim().form_alliance(0, 2);
            app.sim().tick();
            app.hud().request_team_up(app.sim(), 1);
            check(app.hud().alliance_dialog() == HUD::AllianceDialog::BreakConfirm, pic + "(an alliance question is open)");
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom with an alliance question open");
            app.hud().handle_key_down('n', app.sim(), app.renderer().camera());
            notch(app, +1);
            check(app.zoom() == in1, pic + "... and zoom when everything is closed again");
            notch(app, -1);
            // the results screen
            app.scorecard().show(app.sim().get_world_state().match_result, 0);
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom while the results are shown");
            app.scorecard().hide();
            notch(app, +1);
            check(app.zoom() == in1, pic + "... and zoom after");
            notch(app, -1);
        }
        check(app.zoom() == 1.0f, pic + "(the test's own bookkeeping: back at 1)");
    }

    // the way the window delivers it: an SDL_MOUSEWHEEL event in the queue and a frame
    {
        AppRig rig(Aspect::Wide16x9);
        check(rig.ok, "the application for the event queue is up");
        if (rig.ok) {
            Application& app = rig.app;
            app.note_pointer(300, 250);
            SDL_Event e{};
            e.type = SDL_MOUSEWHEEL;
            e.wheel = wheel_event(1, 1.0f);
            SDL_PushEvent(&e);
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == zoom::series(1), "a wheel event in the queue zooms the match in the next frame");
            SDL_Event m{};
            m.type = SDL_MOUSEBUTTONDOWN;
            m.button = button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, 300, 250);
            SDL_PushEvent(&m);
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == 1.0f, "a middle button press in the queue goes back to 1");
        }
    }
}

void test_app_middle_button() {
    group("middle", "the middle button goes back to the zoom 1 towards the pointer, with the same rules as the wheel, and is nobody else's");
    for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
        const bool wide = aspect == Aspect::Wide16x9;
        const std::string pic = wide ? "wide: " : "classic: ";
        AppRig rig(aspect);
        if (!rig.ok) continue;
        Application& app = rig.app;
        const LayoutRect view = app.layout().view();
        const LayoutRect mini = app.layout().minimap();
        const int32_t px = view.x + 200;
        const int32_t py = view.y + 120;
        app.note_pointer(px, py);
        for (const float level : {2.0f, 0.5f}) {
            check(app.set_zoom(level, px, py), pic + "the level " + zoom::level_name(level) + " is set");
            const auto before = rig.world_at(px, py);
            app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, px, py));
            check(app.zoom() == 1.0f, pic + "a middle press goes back to 1 from " + zoom::level_name(level));
            const auto after = rig.world_at(px, py);
            check(std::abs(after.first - before.first) <= 1 && std::abs(after.second - before.second) <= 1, pic + "towards the pointer: the world point under it stays");
            check(app.remembered_zoom() == 1.0f, pic + "the level is remembered");
            app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONUP, px, py));
            check(app.zoom() == 1.0f, pic + "the release does nothing");
        }
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, px, py));
        check(app.zoom() == 1.0f, pic + "at 1 a middle press changes nothing");
        // not over a panel
        check(app.set_zoom(2.0f, px, py), pic + "(zoomed in again)");
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, mini.x + 10, mini.y + 10));
        check(app.zoom() == 2.0f, pic + "a middle press on the minimap does nothing");
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, view.right() + 20, view.y + 200));
        check(app.zoom() == 2.0f, pic + "... nor on the right panel");
        // not during a press that holds the mouse
        app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, px, py));
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, px, py));
        check(app.zoom() == 2.0f, pic + "... nor during a rubber band");
        app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, px, py));
        // with a dialog open
        app.hud().open_options();
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, px, py));
        check(app.zoom() == 2.0f, pic + "... nor with the options window open");
        app.hud().handle_key_down(SDLK_RETURN, app.sim(), app.renderer().camera());
        // the middle button never reaches the HUD: a middle release does not end a minimap drag (it used to clear every pressed state)
        app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, mini.x + 20, mini.y + 20));
        check(app.hud().is_input_captured(), pic + "(the minimap is held)");
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, mini.x + 20, mini.y + 20));
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONUP, mini.x + 20, mini.y + 20));
        check(app.hud().is_input_captured(), pic + "a middle press and release do not let go of the minimap");
        app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, mini.x + 20, mini.y + 20));
        check(!app.hud().is_input_captured(), pic + "(released)");
        // a middle press on the results screen is still nothing
        app.scorecard().show(app.sim().get_world_state().match_result, 0);
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, px, py));
        check(app.zoom() == 2.0f, pic + "a middle press on the results does nothing");
    }
}

void test_app_zoom_api() {
    group("api", "set_zoom, step_zoom and zoom_levels, the API of the touch work");
    AppRig rig(Aspect::Wide16x9);
    if (!rig.ok) return;
    Application& app = rig.app;
    const LayoutRect view = app.layout().view();
    const std::vector<float> levels = app.zoom_levels();                      // GAUNTLET in the wide view: 2 ... 1 ... 0.397
    check(levels.size() == 11 && levels.front() == 2.0f && levels[4] == 1.0f && levels.back() == zoom::floor_zoom(app.zoom_fit()), "the levels of a local game on GAUNTLET in the wide view: 11, from 2 down to the map's limit");
    check(!app.set_zoom(1.0f, 300, 200), "the level that is set already changes nothing (false)");
    for (const float bad : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        check(!app.set_zoom(bad, 300, 200) && app.zoom() == 1.0f, "a number that is no zoom is refused: " + num(static_cast<double>(bad)));
    }
    check(app.set_zoom(0.75f, 300, 200) && app.zoom() == zoom::series(-2), "a zoom that is not a level goes to the nearest level: 0.75 is 0.71");
    check(app.set_zoom(4.0f, 300, 200) && app.zoom() == 2.0f, "beyond the largest level it goes to 2");
    check(app.set_zoom(0.01f, 300, 200) && app.zoom() == levels.back(), "below the map's limit it goes to the limit (a pinch that goes too far is held there)");
    check(app.set_zoom(1.0f, 300, 200) && app.zoom() == 1.0f, "1 is a level like the others");
    check(app.set_zoom(2.0f, 300, 200) && app.zoom() == 2.0f, "a level of the list is taken (true)");
    check(app.set_zoom(0.5f, 300, 200) && app.zoom() == 0.5f, "from 2 straight to 0.5 (the API goes to any offered level)");
    for (size_t i = 8; i-- > 0;) check(app.step_zoom(+1, 300, 200) && app.zoom() == levels[i], "step_zoom(+1) goes up one level: " + num(static_cast<double>(levels[i])));
    check(!app.step_zoom(+1, 300, 200) && app.zoom() == 2.0f, "... and then says no at 2");
    for (size_t i = 1; i < levels.size(); ++i) check(app.step_zoom(-1, 300, 200) && app.zoom() == levels[i], "step_zoom(-1) goes down one level: " + num(static_cast<double>(levels[i])));
    check(!app.step_zoom(-1, 300, 200) && app.zoom() == levels.back(), "... and then says no at the map's limit");
    check(!app.step_zoom(0, 300, 200), "no direction, no step");
    // the anchor is a point of the picture: outside the view it is held to the view's nearest pixel
    check(app.set_zoom(1.0f, -500, -500) && app.set_zoom(2.0f, 5000, 5000) && app.zoom() == 2.0f, "an anchor outside the picture is held to the view");
    app.set_zoom(1.0f, view.x + 1, view.y + 1);
    {
        // ... to the view's own pixel: an anchor beyond an edge gives the camera that the edge pixel gives, and the anchor does matter inside the view
        const uint32_t tiles_w = app.sim().grid().width();
        const uint32_t tiles_h = app.sim().grid().height();
        const auto zoom_from_700 = [&](int32_t ax, int32_t ay) {
            app.set_zoom(1.0f, view.x + 1, view.y + 1);
            app.renderer().camera().set_origin(700.0, 700.0, tiles_w, tiles_h);
            app.set_zoom(2.0f, ax, ay);
            return std::pair<double, double>{app.renderer().camera().x, app.renderer().camera().y};
        };
        check(zoom_from_700(view.x - 100, view.y + 50) == zoom_from_700(view.x, view.y + 50), "an anchor left of the view is the view's left column");
        check(zoom_from_700(view.x + 50, view.y - 100) == zoom_from_700(view.x + 50, view.y), "an anchor above the view is the view's top row");
        check(zoom_from_700(view.right() + 200, view.y + 50) == zoom_from_700(view.right() - 1, view.y + 50), "an anchor right of the view is the view's last column");
        check(zoom_from_700(view.x + 50, view.bottom() + 200) == zoom_from_700(view.x + 50, view.bottom() - 1), "an anchor below the view is the view's last row");
        check(zoom_from_700(view.x, view.y).first != zoom_from_700(view.right() - 1, view.y).first && zoom_from_700(view.x, view.y).second != zoom_from_700(view.x, view.bottom() - 1).second, "(and the anchor decides where the camera goes)");
    }
    app.set_zoom(1.0f, view.x + 100, view.y + 100);
    app.renderer().camera().set_origin(700.0, 700.0, app.sim().grid().width(), app.sim().grid().height());
    const auto before = rig.world_at(view.x + 400, view.y + 250);
    app.set_zoom(2.0f, view.x + 400, view.y + 250);
    check(rig.world_at(view.x + 400, view.y + 250) == before, "the anchor of set_zoom is a point of the picture: its world point stays");
    check(app.zoom_levels() == levels, "the levels that are offered now are the same (a local game on a big map)");
    {   // the device's texture size: the world seen can be no more than a texture holds less the pass's margins, so a small texture raises the limit of the zoom-out (the software renderer says
        // no limit; the test hook says one)
        check(app.renderer().max_world_extent() == 0 && app.zoom_fit().max_world == 0, "the software renderer reports no texture limit: nothing is raised");
        app.renderer().set_texture_side_for_test(1100);
        const int32_t room = 1100 - 64;                                      // (the 64 texels that the pass keeps free of a texture: Renderer::kPassMargin)
        check(app.renderer().max_world_extent() == room && app.zoom_fit().max_world == room, "a device whose textures hold 1100 texels a side leaves a world of 1100 less the margins of the pass: " + std::to_string(room));
        const std::vector<float> small_device = app.zoom_levels();
        const float raised = zoom::floor_zoom(app.zoom_fit());
        check(std::fabs(static_cast<double>(raised) - 762.0 / static_cast<double>(room)) < 1e-6 && small_device.size() == 7 && small_device.front() == 2.0f && small_device[5] == zoom::series(-1) && small_device.back() == raised,
              "GAUNTLET in the wide view on that device: the limit rises to 762 / " + std::to_string(room) + ", the levels end there (2 ... 0.84, 0.7355): " + std::to_string(small_device.size()) + " levels");
        app.renderer().set_texture_side_for_test(0);
        check(app.renderer().max_world_extent() == 0 && app.zoom_levels() == levels, "the device's own answer is back with 0: no limit, the same levels");
    }
    // not in a screen that is not a match
    AppRig setup(Aspect::Wide16x9, 1.0f, false, std::string(), false);
    check(setup.ok && setup.app.state() == AppState::MapSelect, "the setup screen is up");
    if (setup.ok) {
        check(!setup.app.set_zoom(2.0f, 300, 200) && setup.app.zoom() == 1.0f, "set_zoom does nothing outside a match");
        check(!setup.app.view_zoom_allowed(300, 200), "and the wheel is not allowed there");
        notch(setup.app, +1);
        check(setup.app.zoom() == 1.0f, "a wheel event on the setup screen does nothing");
    }
}

// The setup screen after a match that was zoomed (a match leaves the renderer's camera at its zoom until the next match sets it): the screen's first frame makes the map preview with the
// live renderer, and it is the same picture at every zoom (pixel for pixel); the wheel and the middle button do nothing on the setup screen, with the preview or the list under the pointer
struct SetupAfterMatch {
    bool ok{false};
    float live_zoom{1.0f};                                     // the camera's zoom while the setup screen is up (the last match's)
    float remembered{1.0f};
    std::vector<uint8_t> preview;                              // the preview's inner square on the canvas, RGBA
    int colours{0};                                            // distinct colours in it (a picture of the map, not the "No preview" box)
    int32_t area_x{0};
    int32_t area_y{0};
};

SetupAfterMatch setup_after_zoomed_match(float level, const std::string& map, int32_t shown_index) {
    SetupAfterMatch out;
    AppRig rig(Aspect::Wide16x9);
    if (!rig.ok) return out;
    Application& app = rig.app;
    const LayoutRect view = app.layout().view();
    app.hud().dismiss_match_start_modal();
    app.note_pointer(view.x + 284, view.y + 183);
    if (level != 1.0f && !app.set_zoom(level, view.x + 284, view.y + 183)) return out;
    if (!app.start_game(maps_dir() + map + ".LVL")) return out;      // (a match that is started at the remembered level, the new match's own camera)
    app.hud().dismiss_match_start_modal();
    if (app.zoom() != level) return out;
    app.return_to_map_select();
    if (app.state() != AppState::MapSelect) return out;
    app.map_select().set_selected_index(shown_index);              // (the list's own selection: the map whose preview is made, not the match's)
    for (int i = 0; i < 6; ++i) app.run_frame_with_delta(0.1f);    // the setup screen's refresh task runs at 500 ms (a frame counts for at most 100 ms): the frame after it, with a map shown, makes the preview
    const Picture frame = read_canvas(app, kWideW, kWideH);
    const LayoutRect area = SetupLayout::of(SetupVariant::Single).preview_area();
    out.area_x = area.x;
    out.area_y = area.y;
    std::vector<uint32_t> seen;
    for (int32_t y = 0; y < area.h; ++y) {
        for (int32_t x = 0; x < area.w; ++x) {
            const uint8_t* p = frame.at(area.x + x, area.y + y);
            out.preview.insert(out.preview.end(), p, p + 4);
            const uint32_t key = static_cast<uint32_t>(p[0]) << 16 | static_cast<uint32_t>(p[1]) << 8 | p[2];
            if (std::find(seen.begin(), seen.end(), key) == seen.end()) seen.push_back(key);
        }
    }
    out.colours = static_cast<int>(seen.size());
    out.live_zoom = app.zoom();
    out.remembered = app.remembered_zoom();
    out.ok = true;
    return out;
}

void test_app_setup_screen() {
    group("setup", "the map preview is the same picture after a match at any zoom; the wheel and the middle button do nothing on the setup screen, the loading screen and the quick help");
    struct Pair {
        const char* match;                                    // the map of the match that is zoomed
        int32_t shown;                                        // the index of the map that the setup screen shows afterwards (the list's own order)
    };
    for (const Pair& pair : {Pair{"TINY", 1}, Pair{"GAUNTLET", 4}}) {
        const std::string name = std::string("match on ") + pair.match + ", list at " + std::to_string(pair.shown) + ": ";
        const SetupAfterMatch at1 = setup_after_zoomed_match(1.0f, pair.match, pair.shown);
        check(at1.ok && at1.preview.size() > 100000u && at1.colours > 30, name + "(setup) the preview after a match at the zoom 1 is a picture of the map (" + std::to_string(at1.colours) + " colours)");
        if (!at1.ok) continue;
        for (const float level : {zoom::series(-1), zoom::series(2), 2.0f}) {                  // (levels that both maps offer: TINY's own limit is 0.768 in this view)
            const std::string label = name + "after a match at the zoom " + zoom::level_name(level) + ": ";
            const SetupAfterMatch other = setup_after_zoomed_match(level, pair.match, pair.shown);
            check(other.ok && other.live_zoom == level && other.remembered == level, label + "the setup screen is up, the camera still has the match's zoom and the level is remembered");
            size_t different = 0;
            if (other.preview.size() == at1.preview.size()) {
                for (size_t i = 0; i < at1.preview.size(); ++i) different += at1.preview[i] != other.preview[i] ? 1u : 0u;
            } else {
                different = 1;
            }
            check(other.ok && other.preview.size() == at1.preview.size() && different == 0, label + "the preview is the zoom 1 preview, byte for byte (" + std::to_string(different) + " bytes differ)");
        }
    }
    // the wheel and the middle button on the setup screen of a zoomed match: nothing moves, nothing is remembered anew
    {
        AppRig rig(Aspect::Wide16x9);
        check(rig.ok, "(setup) the application is up");
        if (rig.ok) {
            Application& app = rig.app;
            const LayoutRect view = app.layout().view();
            app.hud().dismiss_match_start_modal();
            app.note_pointer(view.x + 284, view.y + 183);
            check(app.set_zoom(2.0f, view.x + 284, view.y + 183), "(setup) the match is zoomed to 2");
            app.return_to_map_select();
            app.render_frame();
            check(app.state() == AppState::MapSelect, "(setup) the setup screen is up");
            const ViewportCamera camera = app.renderer().camera();
            const LayoutRect preview = SetupLayout::of(SetupVariant::Single).preview_area();
            const LayoutRect list = SetupLayout::of(SetupVariant::Single).map_box;
            const std::vector<std::pair<std::pair<int32_t, int32_t>, std::string>> places = {
                {{preview.x + preview.w / 2, preview.y + preview.h / 2}, "the preview"},
                {{list.x + list.w / 2, list.y + list.h / 2}, "the map list"},
                {{view.x + 284, view.y + 183}, "where the match's map view was"},
                {{5, 5}, "the corner of the canvas"},
            };
            for (const auto& place : places) {
                const std::string label = "the setup screen, pointer over " + place.second + ": ";
                app.note_pointer(place.first.first, place.first.second);
                check(!app.view_zoom_allowed(place.first.first, place.first.second), label + "the wheel is not allowed");
                notch(app, +1);
                notch(app, -1);
                notch(app, -1);
                app.handle_mouse_wheel(wheel_event(0, 0.6f));
                app.handle_mouse_wheel(wheel_event(0, 0.6f));
                app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, place.first.first, place.first.second));
                app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONUP, place.first.first, place.first.second));
                const ViewportCamera& now = app.renderer().camera();
                check(app.zoom() == 2.0f && app.remembered_zoom() == 2.0f && now.x == camera.x && now.y == camera.y && now.zoom == camera.zoom, label + "the wheel and the middle button changed nothing (zoom, level, origin)");
                check(app.state() == AppState::MapSelect, label + "the setup screen is still up");
            }
            app.render_frame();
            check(app.zoom() == 2.0f, "(setup) a frame after all of it: the zoom is as it was");
        }
    }
    // the loading screen and the quick help: the same
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), false);
        check(rig.ok, "(setup) an application that starts on its loading screen is up");
        if (rig.ok) {
            Application& app = rig.app;
            const AppState first = app.state();
            app.note_pointer(300, 200);
            notch(app, +1);
            app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, 300, 200));
            check(app.zoom() == 1.0f && app.remembered_zoom() == 1.0f && !app.view_zoom_allowed(300, 200), "the first screen (" + std::to_string(static_cast<int>(first)) + "): the wheel and the middle button do nothing");
            app.finish_loading();
            const AppState second = app.state();
            notch(app, +1);
            app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, 300, 200));
            check(app.zoom() == 1.0f && app.remembered_zoom() == 1.0f && !app.view_zoom_allowed(300, 200), "the screen after loading (" + std::to_string(static_cast<int>(second)) + "): the wheel and the middle button do nothing");
            if (app.state() == AppState::QuickHelp) {
                app.quick_help_key(SDLK_RETURN);
                notch(app, +1);
                check(app.zoom() == 1.0f && app.remembered_zoom() == 1.0f, "the setup screen after the quick help: the wheel does nothing");
            }
        }
    }
}

void test_app_fairness_local() {
    group("fair", "a local game and a game with bots offer the zoom-out (and so does a match of the network: the next group)");
    {
        AppRig rig(Aspect::Wide16x9);
        if (rig.ok) {
            check(rig.app.zoom_limits() == zoom::Limits::any() && rig.app.zoom_levels().front() == 2.0f && rig.app.zoom_levels().back() < 0.5f, "a local game: any level, down to the map's limit");
        }
    }
    {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg = base_config(Aspect::Wide16x9);
        ai::BotSpec spec;
        std::string why;
        check(ai::parse_bot_spec("1:easy", spec, why), "a bot spec parses");
        cfg.bots.push_back(spec);
        Application app;
        check(app.init(cfg) && !app.network_active() && app.bots() != nullptr, "a game with a bot is up and is not a game of the network");
        check(app.zoom_limits() == zoom::Limits::any() && app.zoom_levels().size() == 11 && app.zoom_levels().back() < 0.5f, "a game with bots offers the zoom-out, down to the map's limit");
        app.renderer().camera().set_origin(700.0, 700.0, app.sim().grid().width(), app.sim().grid().height());
        app.note_pointer(300, 200);
        notch(app, -1);
        check(app.zoom() == zoom::series(-1), "... and the wheel zooms out in it, one level a notch");
    }
}

// ---- the review of v0.1.0: the camera's zoom is always one that is allowed and drawn (no offscreen target, a network match), and a wheel event that is not a number ----
// The frames that a test runs with the program's stderr caught (the Application's and the Renderer's reports go there): returns what was written
std::string frames_with_stderr(Application& app, int frames) {
    std::ostringstream sink;
    std::streambuf* const old = std::cerr.rdbuf(sink.rdbuf());
    for (int i = 0; i < frames; ++i) app.run_frame_with_delta(0.016f);
    std::cerr.rdbuf(old);
    return sink.str();
}
int lines_with(const std::string& text, const std::string& needle) {
    int n = 0;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (line.find(needle) != std::string::npos) ++n;
    }
    return n;
}

void test_app_guards() {
    group("guard", "the zoom is always a level that is allowed and drawn: no offscreen target (the hook and a refused texture), a network match, and a wheel that is not a number");
    std::vector<float> all_levels;                                      // (the levels of MEDIUM in the wide view, taken from the first match below)
    const std::vector<float> only_one{1.0f};

    // 1. the target cannot be made (the hook: the pass finds it failing): the zoom drops to 1 at the next frame, only 1 is offered, one line says so, and a later success restores the levels
    for (const float start_zoom : {0.5f, 2.0f}) {
        const std::string at = "from " + num(static_cast<double>(start_zoom)) + ": ";
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
        check(rig.ok && start_ticked_match(rig.app, "MEDIUM"), at + "a match is up");
        if (!rig.ok) continue;
        Application& app = rig.app;
        const LayoutRect view = app.layout().view();
        app.note_pointer(view.x + 200, view.y + 150);
        if (all_levels.empty()) all_levels = app.zoom_levels();
        check(all_levels.size() == 11 && all_levels.front() == 2.0f && all_levels.back() < 0.5f, at + "(the levels of MEDIUM in the wide view: 11)");
        check(app.set_zoom(start_zoom, view.x + 200, view.y + 150), at + "the zoom is set");
        check(app.zoom() == start_zoom && app.zoom_levels() == all_levels, at + "the zoom is " + num(static_cast<double>(start_zoom)) + " and the whole series is offered");
        check(frames_with_stderr(app, 3).empty() && app.renderer().world_target_passes() > 0, at + "its frames go through the offscreen target and write nothing");
        const auto centre_before = rig.world_at(view.x + view.w / 2, view.y + view.h / 2);
        const uint64_t passes = app.renderer().world_target_passes();
        app.renderer().set_fail_world_target(true);
        check(app.renderer().world_target_failed() && app.zoom_levels() == only_one && app.zoom_limits() == zoom::Limits::only_normal(), at + "while the target fails only the level 1 is offered (at once)");
        const std::string said = frames_with_stderr(app, 60);
        check(app.zoom() == 1.0f, at + "one frame took the camera to the zoom 1 (the picture is the zoom 1: the camera, the clicks, the minimap and the scroll agree with it)");
        check(app.zoom_levels() == only_one, at + "the levels offered are {1}");
        check(app.remembered_zoom() == start_zoom, at + "what the player chose last stays remembered (the failure is not the player's choice)");
        check(lines_with(said, "") == 1 && lines_with(said, "the zoom is 1 until it can be") == 1, at + "60 frames wrote exactly ONE line: \"" + said + "\"");
        check(app.renderer().world_target_passes() == passes, at + "no pass tried the target while it fails");
        const auto centre_after = rig.world_at(view.x + view.w / 2, view.y + view.h / 2);
        check(std::abs(centre_after.first - centre_before.first) <= 2 && std::abs(centre_after.second - centre_before.second) <= 2,
              at + "the drop is anchored at the view's centre: the world point there stays (within the grid of the zoom): (" + std::to_string(centre_before.first) + ", " + std::to_string(centre_before.second) + ") -> (" + std::to_string(centre_after.first) + ", " + std::to_string(centre_after.second) + ")");
        // nothing zooms while it lasts, by wheel, middle button or API
        notch(app, -1);
        notch(app, +1);
        check(app.zoom() == 1.0f, at + "the wheel does nothing");
        check(!app.set_zoom(0.5f, 300, 200) && !app.set_zoom(2.0f, 300, 200) && !app.step_zoom(+1, 300, 200) && !app.step_zoom(-1, 300, 200) && app.zoom() == 1.0f, at + "set_zoom and step_zoom refuse every level but 1");
        // a match that starts now starts at 1 (the remembered level is not offered), and the level that was chosen is still remembered
        check(start_ticked_match(app, "MEDIUM") && app.zoom() == 1.0f && app.remembered_zoom() == start_zoom, at + "a match that starts while it fails starts at 1");
        // it works again: the levels come back at the next frame, and the player's level is offered once more
        app.renderer().set_fail_world_target(false);
        check(!app.renderer().world_target_failed() && app.zoom_levels() == all_levels, at + "when the failure is over the three levels are offered at once");
        check(frames_with_stderr(app, 3).empty(), at + "(and nothing is written)");
        app.note_pointer(view.x + 200, view.y + 150);
        notch(app, start_zoom < 1.0f ? -1 : +1);
        check(app.zoom() == (start_zoom < 1.0f ? zoom::series(-1) : zoom::series(1)), at + "the wheel zooms again, a level a notch");
        // a second failure is a new one: one line again
        app.renderer().set_fail_world_target(true);
        const std::string again = frames_with_stderr(app, 20);
        check(app.zoom() == 1.0f && lines_with(again, "") == 1, at + "a second failure drops the zoom again and writes one line more");
    }

    // 2. the failure of the texture itself (SDL_CreateTexture says no, here by the test hook): the renderer reports it once, the Application drops to 1 and says so once; the renderer tries again
    // now and then and, when it works, the levels are back; a try that fails again writes nothing
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
        check(rig.ok && start_ticked_match(rig.app, "MEDIUM"), "(refused texture) a match is up");
        if (rig.ok) {
            Application& app = rig.app;
            const LayoutRect view = app.layout().view();
            app.note_pointer(view.x + 200, view.y + 150);
            check(frames_with_stderr(app, 2).empty() && app.renderer().world_target_error().empty(), "(refused texture) at the zoom 1 no target is needed and nothing is wrong");
            all_levels = app.zoom_levels();
            app.renderer().set_fail_world_target_creation(true);
            notch(app, +1);
            check(app.zoom() == zoom::series(1), "(refused texture) the wheel zooms in: nothing is known to be wrong yet");
            const std::string first = frames_with_stderr(app, 2);
            check(app.renderer().world_target_failed() && !app.renderer().world_target_error().empty(), "(refused texture) the pass that could not make the target keeps the failure");
            check(app.zoom() == 1.0f && app.zoom_levels() == only_one, "(refused texture) and the next frame has the camera at 1 with only that level offered");
            check(lines_with(first, "[Renderer]") == 1 && lines_with(first, "[Application]") == 1 && lines_with(first, "") == 2, "(refused texture) one line of the renderer (what SDL said) and one of the application: \"" + first + "\"");
            const uint64_t passes = app.renderer().world_target_passes();
            const std::string long_run = frames_with_stderr(app, 2 * Renderer::kWorldTargetRetryFrames + 20);
            check(long_run.empty() && app.renderer().world_target_failed() && app.zoom() == 1.0f && app.renderer().world_target_passes() == passes,
                  "(refused texture) while it still fails the retries write nothing, the zoom stays 1 and no pass uses a target (\"" + long_run + "\")");
            app.renderer().set_fail_world_target_creation(false);
            const std::string recovered = frames_with_stderr(app, Renderer::kWorldTargetRetryFrames + 5);
            check(!app.renderer().world_target_failed() && app.renderer().world_target_error().empty() && recovered.empty(), "(refused texture) once the texture can be made the renderer finds out within its retry interval, silently");
            check(app.zoom_levels() == all_levels && app.zoom() == 1.0f, "(refused texture) the three levels are offered again (the camera stays at the 1 it was taken to)");
            const uint64_t before = app.renderer().world_target_passes();
            notch(app, +1);
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == zoom::series(1) && app.renderer().world_target_passes() > before, "(refused texture) the player zooms in again and the frame is drawn through the target");
        }
    }

    // 3. a camera that something put outside the allowed levels is taken back at the next frame: never below the map's limit (defence in depth: the wheel, the API and the match start
    // already keep to it), and the levels in between are left alone
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
        check(rig.ok && start_ticked_match(rig.app, "MEDIUM"), "(clamp) a local match is up");
        if (rig.ok) {
            Application& app = rig.app;
            const LayoutRect view = app.layout().view();
            app.renderer().camera().set_zoom(0.5f, view.w / 2, view.h / 2, app.sim().grid().width(), app.sim().grid().height());
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == 0.5f, "(clamp) in a local game a camera at 0.5 is left alone");
            const float limit = zoom::floor_zoom(app.zoom_fit());
            check(limit > 0.39f && limit < 0.4f, "(clamp) the limit of MEDIUM in the wide view is 762 / 1920");
            app.renderer().camera().set_zoom(0.25f, view.w / 2, view.h / 2, app.sim().grid().width(), app.sim().grid().height());
            check(app.zoom() == 0.25f, "(clamp) (forced) the camera is below the map's limit, where the view would show more than the map");
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == limit && app.renderer().camera().x >= 0.0f && app.renderer().camera().x + static_cast<float>(view.w) / limit <= 1920.0f + 1e-3f, "(clamp) the next frame takes it to the map's limit, and the view shows no more than the map");
            check(app.remembered_zoom() == 1.0f, "(clamp) what the player chose last (nothing: the camera was set by hand) is not touched by the correction");
            app.renderer().camera().set_zoom(0.7f, view.w / 2, view.h / 2, app.sim().grid().width(), app.sim().grid().height());
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == 0.7f, "(clamp) a zoom between two levels (not a level, but inside the limits) is left alone");
            app.renderer().camera().set_zoom(2.0f, view.w / 2, view.h / 2, app.sim().grid().width(), app.sim().grid().height());
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == 2.0f, "(clamp) 2 is the most, and is left alone");
        }
    }
}

// A wheel event whose precise amount is not a number or is infinite is no event: the accumulator keeps what it had, the zoom does not move (static_cast<int>(NaN) is undefined, and a NaN in the
// accumulator would make every later event of the session 0)
void test_app_wheel_garbage() {
    group("garbage", "a wheel event that is not a number is ignored and poisons nothing");
    AppRig rig(Aspect::Wide16x9);
    check(rig.ok && start_ticked_match(rig.app, "MEDIUM"), "a match is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    app.note_pointer(300, 200);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float v : {nan, inf, -inf, -nan}) {
        app.handle_mouse_wheel(wheel_event(0, v));
        check(app.zoom() == 1.0f, "a precise amount of " + num(static_cast<double>(v)) + " does not zoom");
    }
    app.handle_mouse_wheel(wheel_event(3, nan));
    check(app.zoom() == 1.0f, "a NaN with a whole amount of 3 does not zoom either (the event cannot be trusted)");
    g_clock_ms += 2000;
    const std::vector<float> levels = app.zoom_levels();                        // MEDIUM in the wide view: 11 levels, 2 down to 0.397
    check(levels.size() == 11, "(MEDIUM offers 11 levels in the wide view)");
    notch(app, +1);
    check(app.zoom() == zoom::series(1), "a normal notch afterwards still zooms (the accumulator is not poisoned)");
    notch(app, -1);
    check(app.zoom() == 1.0f, "and back");
    // a trackpad's fractions around a NaN: 0.4 + (NaN) + 0.7 is one step with 0.1 left; nothing was forgotten and nothing was added
    app.handle_mouse_wheel(wheel_event(0, 0.4f));
    app.handle_mouse_wheel(wheel_event(0, nan, false, 16));
    check(app.zoom() == 1.0f, "0.4 and a NaN: no step yet");
    app.handle_mouse_wheel(wheel_event(0, 0.7f));
    check(app.zoom() == zoom::series(1), "... and 0.7 completes the step: the 0.4 was not forgotten");
    // an absurd amount is one event's worth at most: the accumulator's bound, 8 levels (the series has 11 here)
    app.handle_mouse_wheel(wheel_event(0, -1.0e30f, false, 2000));
    check(app.zoom() == levels.back(), "an absurd amount toward zooms out as far as there are levels (at most 8 of them)");
    app.handle_mouse_wheel(wheel_event(0, 1.0e30f, false, 2000));
    check(app.zoom() == levels[levels.size() - 1 - static_cast<size_t>(zoom::WheelAccumulator::kMaxSteps)], "an absurd amount away zooms in by 8 levels, the most that one event can ask for");
}

void test_app_settings() {
    group("settings", "the key `zoom` of the settings file, and --zoom");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ants_zoom_view_test_" + std::to_string(static_cast<long>(SDL_GetTicks())) + "_" + std::to_string(reinterpret_cast<uintptr_t>(&g_clock_ms) % 100000));
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "settings.ini").string();
    auto read_file = [&]() {
        std::ifstream in(file);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, file);
        check(rig.ok && rig.app.zoom() == 1.0f && rig.app.remembered_zoom() == 1.0f, "no key: the zoom 1");
        check(read_file().find("zoom") == std::string::npos, "a start writes no zoom");
        rig.app.note_pointer(300, 200);
        notch(rig.app, +1);
        check(read_file().find("zoom=1.19") != std::string::npos, "the wheel writes the key at once, the level's name in three digits: zoom=1.19");
        notch(rig.app, -1);
        notch(rig.app, -1);
        check(read_file().find("zoom=0.841") != std::string::npos && read_file().find("zoom=1.19") == std::string::npos, "and the next change replaces it: zoom=0.841");
        check(rig.app.set_zoom(2.0f, 300, 200) && read_file().find("zoom=2") != std::string::npos, "(2 again)");
    }
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, file);
        check(rig.ok && rig.app.remembered_zoom() == 2.0f && rig.app.zoom() == 2.0f, "the next run starts at the remembered level, 2");
        check(rig.app.start_game(maps_dir() + "TINY.LVL") && rig.app.zoom() == 2.0f, "and so does a match that starts later");
    }
    {
        AppRig rig(Aspect::Wide16x9, 0.5f, true, file);
        check(rig.ok && rig.app.remembered_zoom() == 0.5f && rig.app.zoom() == 0.5f, "--zoom 0.5 wins over the key");
        check(read_file().find("zoom=2") != std::string::npos, "and it does not change the file");
    }
    {   // any zoom of the range is taken, and a match starts at the nearest level that its map offers in its view (GAUNTLET, wide: 2 ... 0.5, 0.42, 0.397)
        AppRig rig(Aspect::Wide16x9, 1.5f, true, file);
        check(rig.ok && rig.app.remembered_zoom() == 1.5f && rig.app.zoom() == zoom::series(2), "--zoom 1.5 is remembered as it is and the match starts at the nearest level, 1.41");
        AppRig fine(Aspect::Wide16x9, 0.6f, true, file);
        check(fine.ok && fine.app.remembered_zoom() == 0.6f && fine.app.zoom() == zoom::series(-3), "--zoom 0.6 starts at 0.59");
        AppRig low(Aspect::Wide16x9, 0.1f, true, file);
        check(low.ok && low.app.remembered_zoom() == 0.1f && low.app.zoom() == zoom::floor_zoom(low.app.zoom_fit()) && low.app.zoom() > 0.39f && low.app.zoom() < 0.4f, "--zoom 0.1 is beyond the map's limit: the match starts at the limit, 0.397");
        check(low.app.start_game(maps_dir() + "TINY.LVL") && low.app.zoom() == zoom::floor_zoom(low.app.zoom_fit()) && low.app.zoom() > 0.76f && low.app.zoom() < 0.77f && low.app.remembered_zoom() == 0.1f,
              "... and on TINY the limit is 0.768; what was asked for stays remembered");
        check(low.app.start_game(maps_dir() + "TREASURE.LVL") && low.app.zoom() > 0.39f && low.app.zoom() < 0.4f, "... and TREASURE's is 0.397 again");
        AppRig whole(Aspect::Wide16x9, 0.5f, true, file);
        check(whole.ok && whole.app.start_game(maps_dir() + "TINY.LVL") && whole.app.zoom() > 0.76f && whole.app.zoom() < 0.77f, "--zoom 0.5 on TINY (992 px: 0.5 would show more than the map) starts at the limit");
    }
    {   // a settings key is a zoom too: the same rule
        {
            std::ofstream out(file, std::ios::trunc);
            out << "zoom=0.35\n";
        }
        AppRig rig(Aspect::Wide16x9, 1.0f, false, file);
        check(rig.ok && rig.app.remembered_zoom() == 0.35f && rig.app.zoom() == zoom::floor_zoom(rig.app.zoom_fit()), "zoom=0.35 in the file: remembered as it is, the match starts at the map's limit (0.397)");
        AppRig classic(Aspect::Classic4x3, 1.0f, false, file);
        check(classic.ok && classic.app.zoom() == zoom::series(-6), "... and in the classic view, where 0.35 is a level, it starts there");
    }
    for (const char* bad : {"3", "abc", "", "0.04", "2.01", "-1", "0x1", "2 ", "0.5.0"}) {
        {
            std::ofstream out(file, std::ios::trunc);
            out << "zoom=" << bad << "\n";
        }
        std::ostringstream err;
        std::streambuf* old = std::cerr.rdbuf(err.rdbuf());
        AppRig rig(Aspect::Wide16x9, 1.0f, false, file);
        std::cerr.rdbuf(old);
        check(rig.ok && rig.app.remembered_zoom() == 1.0f && rig.app.zoom() == 1.0f, std::string("a settings file with zoom=") + bad + " is ignored, the game starts at 1");
        if (std::string(bad) != "") check(err.str().find("zoom") != std::string::npos, std::string("... and says so: ") + err.str());
    }
    {   // the command line
        auto parse = [](std::vector<std::string> args) {
            std::vector<char*> argv;
            args.insert(args.begin(), "ants");
            for (auto& a : args) argv.push_back(a.data());
            return Application::parse_arguments(static_cast<int>(argv.size()), argv.data());
        };
        ApplicationConfig none = parse({"--map", "x.LVL"});
        check(!none.zoom_given && none.zoom == 1.0f && none.startup_error.empty(), "no --zoom: not given, 1");
        for (const char* ok : {"0.5", "1", "2", "2.0", ".5", "1.5", "1.41", "0.397", "0.05", "0.6", "1.99"}) {
            ApplicationConfig c = parse({"--zoom", ok, "--map", "x.LVL"});
            check(c.zoom_given && c.zoom >= zoom::kSmallest && c.zoom <= zoom::kIn && c.startup_error.empty(), std::string("--zoom ") + ok + " is taken");
        }
        for (const char* bad : {"3", "x", "0", "0.04", "2.01", "-1", "1e0", "1,5"}) {
            ApplicationConfig c = parse({"--zoom", bad});
            check(!c.zoom_given && c.startup_error.find("--zoom") != std::string::npos && c.startup_error.find("a zoom from 0.05 to 2") != std::string::npos, std::string("--zoom ") + bad + " is refused: " + c.startup_error);
        }
        ApplicationConfig missing = parse({"--zoom"});
        check(!missing.zoom_given && missing.startup_error.find("--zoom needs") != std::string::npos, "--zoom without a value is refused: " + missing.startup_error);
        // a game that starts with a bad option does not start
        ApplicationConfig refused = parse({"--zoom", "3", "--headless"});
        Application app;
        std::ostringstream err;
        std::streambuf* old = std::cerr.rdbuf(err.rdbuf());
        const bool started = app.init(refused);
        std::cerr.rdbuf(old);
        check(!started, "a game with a refused --zoom does not start");
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void test_app_ants() {
    group("ants", "the ants of a real match are drawn at every zoom (after the first simulation ticks; the frames before the first tick are test_prestart_view.cpp's)");
    for (const Aspect aspect : {Aspect::Classic4x3, Aspect::Wide16x9}) {
        const bool wide = aspect == Aspect::Wide16x9;
        const int32_t cw = wide ? kWideW : kClassicW;
        const int32_t ch = wide ? kWideH : kClassicH;
        for (const char* map : {"GAUNTLET", "TINY"}) {
            const std::string at = std::string(wide ? "wide" : "classic") + " " + map + ": ";
            AppRig rig(aspect, 1.0f, true);
            check(rig.ok, at + "the application is up");
            if (!rig.ok) continue;
            Application& app = rig.app;
            app.renderer().pin_animation_clock(1500);
            check(start_ticked_match(app, map), at + "the match starts and ticks");
            check(app.sim().current_tick() >= 3, at + "the simulation has ticked: " + std::to_string(app.sim().current_tick()));
            bool own = false;
            for (const auto& a : app.sim().get_world_state().ants) own = own || a.player_id == 0;
            check(own, at + "the local player has ants");
            const LayoutRect view = app.layout().view();
            check(app.zoom() == 1.0f, at + "the match is at the zoom 1");
            // the camera is put where one of the local player's ants stands (a whole pixel origin on the grid of every zoom: even numbers), so that the ants are in the view at every zoom
            const sim::AntSnapshot* mine = nullptr;
            for (const auto& a : app.sim().get_world_state().ants) {
                if (a.player_id == 0 && mine == nullptr) mine = &a;
            }
            if (mine == nullptr) continue;
            const double ox = static_cast<double>(std::max(0, mine->px - 100) & ~1);
            const double oy = static_cast<double>(std::max(0, mine->py - 110) & ~1);
            ViewportCamera& cam = app.renderer().camera();
            const uint32_t tiles_w = app.sim().grid().width();
            const uint32_t tiles_h = app.sim().grid().height();
            const auto look = [&](float z) {
                cam.zoom = z;
                cam.set_origin(ox, oy, tiles_w, tiles_h);
            };

            // zoom 1: the sprites make a difference to the view that the same world without ants does not have
            look(1.0f);
            const double cx1 = static_cast<double>(cam.x);
            const double cy1 = static_cast<double>(cam.y);
            const Picture with1 = world_with_ants(app, cw, ch);
            const Picture without1 = world_without_ants(app, cw, ch);
            const int shown1 = differ(with1, without1, view);
            check(shown1 > 500, at + "at the zoom 1 the ants are drawn: " + std::to_string(shown1) + " pixels of the view differ from the same world without them");

            // zoom 2 from the same origin: the nearest enlargement of the zoom 1 picture, ants included, and the ants are in it
            look(2.0f);
            check(static_cast<double>(cam.x) == cx1 && static_cast<double>(cam.y) == cy1, at + "the same origin at the zoom 2");
            const Picture with2 = world_with_ants(app, cw, ch);
            const Picture without2 = world_without_ants(app, cw, ch);
            const int bad2 = differ(with2, enlarge2(with1, view, false, false), view);
            check(bad2 == 0, at + "at the zoom 2 the view is the nearest enlargement of the zoom 1 picture, ants included: " + std::to_string(bad2) + " pixels differ");
            const int shown2 = differ(with2, without2, view);
            check(shown2 > 500, at + "at the zoom 2 the ants are drawn: " + std::to_string(shown2));

            // zoom 0.5: the ants are in the picture
            look(0.5f);
            const Picture with05 = world_with_ants(app, cw, ch);
            const Picture without05 = world_without_ants(app, cw, ch);
            const int shown05 = differ(with05, without05, view);
            check(shown05 > 100, at + "at the zoom 0.5 the ants are drawn: " + std::to_string(shown05));
            app.renderer().set_show_hp(true);
        }
    }
}

void test_app_start_view_and_listener() {
    group("start", "the start view keeps the hill in view and the sound listener is the middle of the world that is seen, at every zoom, on every map and for every player");
    const char* maps[6] = {"GAUNTLET", "ISLANDS", "MEDIUM", "SMALL", "TINY", "TREASURE"};
    for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
        const bool wide = aspect == Aspect::Wide16x9;
        for (const float asked : {0.5f, 1.0f, 2.0f, 1.5f, 0.7f, 0.3f}) {
            AppRig rig(aspect, asked, true);
            check(rig.ok, "the application is up");
            if (!rig.ok) continue;
            Application& app = rig.app;
            const LayoutRect view = app.layout().view();
            for (const char* map : maps) {
                check(app.start_game(maps_dir() + map + ".LVL"), std::string("the match on ") + map + " starts");
                // a match starts at the nearest level that its map offers in this view to the one that was asked for (or remembered): 0.5 on TINY is its limit, 0.768
                const float z = app.zoom();
                const std::vector<float> offered = app.zoom_levels();
                check(z == zoom::nearest(asked, offered) && std::find(offered.begin(), offered.end(), z) != offered.end() && app.remembered_zoom() == asked,
                      std::string(wide ? "wide" : "classic") + " " + map + ": a match starts at the nearest level to " + num(static_cast<double>(asked)) + " that the map offers, " + zoom::level_name(z) + ", and the level that was asked for stays remembered");
                if (asked == 1.0f || asked == 2.0f) check(z == asked, std::string(wide ? "wide" : "classic") + " " + map + ": 1 and 2 are offered on every map");
                const int32_t map_w = static_cast<int32_t>(app.sim().grid().width()) * 32;
                const int32_t map_h = static_cast<int32_t>(app.sim().grid().height()) * 32;
                for (uint8_t player = 0; player < 4; ++player) {
                    app.set_local_player(player);
                    const auto* base = app.sim().grid().find_anthill(player);
                    if (base == nullptr) continue;
                    const int32_t ax = (base->x + 1) * 32 + 16;
                    const int32_t ay = (base->y + 1) * 32 + 16;
                    const ViewportCamera& cam = app.renderer().camera();
                    const double vis_w = view.w / static_cast<double>(z);
                    const double vis_h = view.h / static_cast<double>(z);
                    const std::string at = std::string(wide ? "wide" : "classic") + " " + map + " player " + std::to_string(player) + " at " + zoom::level_name(z) + ": ";
                    check(ax >= static_cast<double>(cam.x) && ax < static_cast<double>(cam.x) + vis_w && ay >= static_cast<double>(cam.y) && ay < static_cast<double>(cam.y) + vis_h, at + "the hill's anchor (" + std::to_string(ax) + ", " + std::to_string(ay) + ") is in view from the origin (" + num(cam.x) + ", " + num(cam.y) + ")");
                    // the original's rule over the world that is seen: just far enough right / down to show the square's far edge
                    const double want_x = wide && map_w <= vis_w ? (map_w - vis_w) / 2.0 : std::max(0, std::min(ax + 192, map_w) - zoom::visible(view.w, z));
                    const double want_y = wide && map_h <= vis_h ? (map_h - vis_h) / 2.0 : std::max(0, std::min(ay + 192, map_h) - zoom::visible(view.h, z));
                    // (rounded to the lattice of the zoom and held in the range whose far end is on the lattice: at the levels that are not powers of two the origin is not the whole number of the rule)
                    const auto lattice = [z](double v, double map_px, double vis) { return std::clamp(std::floor(v * static_cast<double>(z) + 0.5) / static_cast<double>(z), 0.0, std::floor((map_px - vis) * static_cast<double>(z) + 1e-9) / static_cast<double>(z)); };
                    if (map_w > vis_w && map_h > vis_h) check(std::fabs(cam.x - static_cast<float>(lattice(want_x, map_w, vis_w))) < 1e-3f && std::fabs(cam.y - static_cast<float>(lattice(want_y, map_h, vis_h))) < 1e-3f, at + "the origin is the original's start view over the world that is seen: (" + num(want_x) + ", " + num(want_y) + "), it is (" + num(cam.x) + ", " + num(cam.y) + ")");
                    // the listener: the middle of the world that is seen
                    app.update_simulation(0.0f);
                    const int32_t listen_x = static_cast<int32_t>(std::floor(cam.x + vis_w / 2.0));
                    const int32_t listen_y = static_cast<int32_t>(std::floor(cam.y + vis_h / 2.0));
                    check(app.audio_mixer().listener_x() == listen_x && app.audio_mixer().listener_y() == listen_y, at + "the sound listener is the middle of the world that is seen: (" + std::to_string(listen_x) + ", " + std::to_string(listen_y) + "), it is (" + std::to_string(app.audio_mixer().listener_x()) + ", " + std::to_string(app.audio_mixer().listener_y()) + ")");
                }
            }
        }
    }
    // the listener follows a zoom
    {
        AppRig rig(Aspect::Wide16x9);
        if (rig.ok) {
            Application& app = rig.app;
            app.note_pointer(300, 200);
            app.update_simulation(0.0f);
            const int32_t at_one = app.audio_mixer().listener_x();
            check(at_one == app.renderer().camera().world_x + 381, "at 1 the listener is the origin plus half the 762 px view");
            check(app.set_zoom(2.0f, 16, 21), "(zoomed in at the view's corner)");
            app.update_simulation(0.0f);
            check(app.audio_mixer().listener_x() == app.renderer().camera().world_x + 190 && app.audio_mixer().listener_x() != at_one, "at 2 the listener is the origin plus half the 381 world pixels that are seen: it moved");
        }
    }
}

void test_local_determinism(const assets::AssetArchive&) {
    group("client", "the zoom is the player's own: the same orders at the same ticks give the same match at any zoom");
    // The same script of orders (to world points) on runs that differ in nothing but the zoom, the scrolling and the pointer (one run for every level of the series, 0.397 to 2): the clicks are
    // computed from the camera so that they hit the same world pixels, and the state hash of the simulation must be the same
    std::vector<sim::StateHash> hashes;
    std::vector<uint64_t> ticks;
    const std::vector<float> run_levels = zooms_for(true);
    for (const float z : run_levels) {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        AppRig rig(Aspect::Wide16x9, z, true);
        if (!rig.ok) continue;
        Application& app = rig.app;
        const LayoutRect view = app.layout().view();
        app.hud().dismiss_match_start_modal();
        const auto* hill = app.sim().grid().find_anthill(0);
        const int32_t hx = hill != nullptr ? hill->x * 32 + 64 : 400;
        const int32_t hy = hill != nullptr ? hill->y * 32 + 64 : 400;
        for (int step = 0; step < 400; ++step) {
            if (step % 40 == 0) {
                // select every ant, then order a move to a world point near the hill by clicking the pixel that shows it (the camera is brought to the hill first)
                app.hud().select_all_friendly(app.sim().get_world_state());
                const int32_t target_x = hx + ((step / 40) % 5) * 70 - 100;
                const int32_t target_y = hy + ((step / 40) % 3) * 90 - 60;
                ViewportCamera& cam = app.renderer().camera();
                cam.center_on(target_x, target_y, app.sim().grid().width(), app.sim().grid().height());
                // find a pixel of the view whose world pixel is in the target's tile (the tile decides the order)
                int32_t cx = -1;
                int32_t cy = -1;
                for (int32_t sy = view.y + 20; sy < view.bottom() - 20 && cx < 0; ++sy) {
                    for (int32_t sx = view.x + 20; sx < view.right() - 20; ++sx) {
                        if (world_under_f(cam.x, z, sx - view.x) / 32 == target_x / 32 && world_under_f(cam.y, z, sy - view.y) / 32 == target_y / 32) {
                            cx = sx;
                            cy = sy;
                            break;
                        }
                    }
                }
                check(cx >= 0, "the target tile of step " + std::to_string(step) + " is in view at the zoom " + zoom::level_name(z));
                if (cx >= 0) {
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONDOWN, cx, cy));
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONUP, cx, cy));
                }
            }
            if (step % 7 == 0) app.note_pointer(view.x + 100 + (step * 13) % 400, view.y + 80 + (step * 7) % 300);
            app.hud().input_tick(app.renderer().camera(), app.sim().grid().width(), app.sim().grid().height(), view.right() + 3, view.y + 100);     // a scroll-ish poke at the panel's edge
            app.update_simulation(0.05f);
        }
        hashes.push_back(app.sim().state_hash());
        ticks.push_back(app.sim().current_tick());
    }
    bool same_ticks = hashes.size() == run_levels.size() && ticks.size() == run_levels.size();
    bool same_hash = same_ticks;
    for (size_t i = 1; same_ticks && i < ticks.size(); ++i) {
        same_ticks = same_ticks && ticks[i] == ticks[0];
        same_hash = same_hash && hashes[i] == hashes[0];
    }
    check(same_ticks && ticks[0] >= 300, "the runs at every level ran the same number of ticks: " + std::to_string(ticks.empty() ? 0 : ticks[0]));
    check(same_hash, "the state hash of the simulation is the same at every level of the series: " + std::to_string(run_levels.size()) + " runs");
}

// =====================================================================================================================================================
// A match of the network: the same levels as a local game, and the view is the client's own
// =====================================================================================================================================================

void test_network_match() {
    group("net", "a match of the network offers the same levels as a local game (host or guest), and the view stays the client's own");
    // 1. a guest that remembered 0.5, on TINY (992 px: the limit of the zoom-out is 762 / 992 = 0.768 in the wide view)
    {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        Peer host;
        Application app;
        check(join_and_start(app, host, network_config(0.5f, true)), "the guest and the host are in the match on TINY");
        if (app.state() == AppState::Playing) {
            check(app.network_active() && !app.net()->is_host(), "it is a match of the network and this machine is a guest");
            const float limit = zoom::floor_zoom(app.zoom_fit());
            check(limit > 0.76f && limit < 0.77f, "the limit of the zoom-out on TINY in the wide view is 0.768: " + num(static_cast<double>(limit)));
            check(app.zoom() == limit && app.remembered_zoom() == 0.5f, "a network match starts at the nearest level to the remembered 0.5 that the map offers, its limit (and 0.5 stays remembered)");
            check(app.zoom_limits() == zoom::Limits::any(), "a match of the network has no limit of its own: the same levels as a local game");
            {   // (the levels that a local game has on TINY in this view, written out: another Application cannot be made here, the end of one closes SDL for the other)
                const std::vector<double> want = {2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.84090, 0.768145};
                const std::vector<float> got = app.zoom_levels();
                bool same = got.size() == want.size();
                for (size_t i = 0; same && i < got.size(); ++i) same = std::fabs(static_cast<double>(got[i]) - want[i]) < 5e-5;
                check(same, "the levels of the match of the network are the levels of a local game on TINY in this view: 2 ... 0.84, 0.768");
            }
            app.hud().dismiss_match_start_modal();
            // the camera is held to the map's limit at every frame, in a match of the network as anywhere, and the level that the player remembered is not touched
            {
                const LayoutRect gview = app.layout().view();
                app.renderer().camera().set_zoom(0.5f, gview.w / 2, gview.h / 2, app.sim().grid().width(), app.sim().grid().height());
                check(app.zoom() == 0.5f && app.network_active(), "(forced) the camera is at 0.5, below TINY's limit, in a match of the network");
                app.run_frame_with_delta(0.016f);
                check(app.zoom() == limit && app.remembered_zoom() == 0.5f, "the next frame takes the camera to the map's limit (the view would show more than the map)");
                app.renderer().camera().set_zoom(2.0f, gview.w / 2, gview.h / 2, app.sim().grid().width(), app.sim().grid().height());
                app.run_frame_with_delta(0.016f);
                check(app.zoom() == 2.0f, "a camera at 2 is left alone");
                app.renderer().camera().set_zoom(0.9f, gview.w / 2, gview.h / 2, app.sim().grid().width(), app.sim().grid().height());
                app.run_frame_with_delta(0.016f);
                check(app.zoom() == 0.9f, "a camera between two levels, inside the limits, is left alone");
                app.renderer().camera().set_zoom(limit, gview.w / 2, gview.h / 2, app.sim().grid().width(), app.sim().grid().height());
            }
            app.note_pointer(300, 200);
            notch(app, -1);
            check(app.zoom() == limit, "the wheel toward does not go below the map's limit");
            check(!app.step_zoom(-1, 300, 200) && app.zoom() == limit, "step_zoom(-1) says no at the limit");
            check(app.set_zoom(0.3f, 300, 200) == false && app.zoom() == limit, "set_zoom(0.3) goes to the nearest level, the limit, where it already is");
            app.handle_mouse_wheel(wheel_event(0, -0.6f));
            app.handle_mouse_wheel(wheel_event(0, -0.6f));
            check(app.zoom() == limit, "a trackpad's creep toward does not go below it either");
            check(app.remembered_zoom() == limit || app.remembered_zoom() == 0.5f, "(what is remembered is the player's own choice)");
            // the wheel away zooms in, a level a notch, and the way back goes to the limit and no further
            notch(app, +1);
            check(app.zoom() == zoom::series(-1), "the wheel away zooms in to the next level, 0.84");
            notch(app, -1);
            check(app.zoom() == limit, "and back to the limit");
            check(app.set_zoom(2.0f, 300, 200) && app.zoom() == 2.0f, "(2 again)");
            app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, 300, 200));
            check(app.zoom() == 1.0f, "the middle button goes back to 1 from 2");
            check(app.remembered_zoom() == 1.0f, "(the player's own choice 1 is what is remembered now)");
        }
        // the match is over for this machine: back to the setup screen
        app.return_to_map_select();
        check(!app.network_active(), "the network is gone");
        check(app.zoom_limits() == zoom::Limits::any(), "a local game offers any level too");
    }
    // 2. a guest on a bigger map: the whole series, the zoom-out included, down to the map's limit, as in a local game
    {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        Peer host;
        Application app;
        check(join_and_start(app, host, network_config(0.5f, true), "TREASURE"), "(the second guest is in the match on TREASURE)");
        if (app.state() == AppState::Playing) {
            check(app.network_active() && app.zoom() == 0.5f && app.remembered_zoom() == 0.5f, "a match of the network on TREASURE starts at the remembered 0.5: the zoom-out is offered in a match of the network");
            const std::vector<float> levels = app.zoom_levels();
            check(levels.size() == 11 && levels.front() == 2.0f && levels[8] == 0.5f && levels.back() == zoom::floor_zoom(app.zoom_fit()), "TREASURE offers 2 ... 0.5, 0.42 and its limit 0.397 to a guest");
            app.hud().dismiss_match_start_modal();
            app.note_pointer(300, 200);
            check(app.set_zoom(0.5f, 300, 200) == false, "(at 0.5 already)");
            notch(app, -1);
            check(app.zoom() == levels[9], "the wheel toward zooms out a level, in a match of the network");
            notch(app, -1);
            check(app.zoom() == levels.back(), "... and to the limit");
            notch(app, -1);
            check(app.zoom() == levels.back(), "... and no further");
            check(app.step_zoom(+1, 300, 200) && app.zoom() == levels[9], "step_zoom(+1) goes in a level");
            // a local game that follows keeps what the player chose last
            app.return_to_map_select();
            check(app.start_game(maps_dir() + "TREASURE.LVL") && app.zoom() == levels[9], "a local game after it starts at the level that was chosen last, 0.42");
        }
    }
    // 3. a host: the same levels; the view stays the client's own: a zoomed machine and a machine without a view play the same match
    {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg = network_config(0.5f, true);
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        Application app;
        check(app.init(cfg) && app.network_active() && app.net()->is_host(), "the host application is up");
        Peer bob;
        check(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"), "Bob joins the room");
        Duo duo{app, bob};
        check(duo.until([&]() { return app.net()->can_start(); }, 8000), "the room can start");
        app.map_select().handle_key_down(SDLK_RETURN);
        check(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000), "the match starts on both machines");
        if (app.state() == AppState::Playing) {
            const std::vector<float> host_levels = app.zoom_levels();
            check(host_levels == zoom::levels(app.zoom_fit(), zoom::Limits::any()) && host_levels.size() >= 7 && host_levels.back() < 0.8f, "a host is offered the whole series, the zoom-out included");
            check(app.zoom() == zoom::nearest(0.5f, host_levels) && app.remembered_zoom() == 0.5f, "a host starts at the nearest level to the remembered 0.5 that its map offers");
            const size_t start_index = static_cast<size_t>(std::find(host_levels.begin(), host_levels.end(), app.zoom()) - host_levels.begin());
            app.hud().dismiss_match_start_modal();
            const LayoutRect view = app.layout().view();
            app.note_pointer(view.x + 300, view.y + 200);
            notch(app, +1);
            check(start_index > 0 && start_index < host_levels.size() && app.zoom() == host_levels[start_index - 1], "the host zooms in a level");
            const auto* hill = app.sim().grid().find_anthill(0);
            const int32_t hx = hill != nullptr ? hill->x * 32 + 64 : 400;
            const int32_t hy = hill != nullptr ? hill->y * 32 + 64 : 400;
            // 40 seconds of play: the host orders through its HUD with clicks at the zoom 2 and scrolls; Bob orders through its NetGame
            uint32_t next = 0;
            int host_orders = 0;
            for (uint32_t t = 0; t < 40000; t += 10) {
                if (t >= next) {
                    next = t + 1000;
                    app.hud().select_all_friendly(app.sim().get_world_state());
                    ViewportCamera& cam = app.renderer().camera();
                    cam.center_on(hx + static_cast<int32_t>((t / 1000) % 7) * 40, hy + static_cast<int32_t>((t / 1000) % 5) * 30, app.sim().grid().width(), app.sim().grid().height());
                    const int32_t sx = view.x + view.w / 2 + static_cast<int32_t>((t / 1000) % 9) * 6 - 24;
                    const int32_t sy = view.y + view.h / 2 + static_cast<int32_t>((t / 1000) % 4) * 8 - 12;
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONDOWN, sx, sy));
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONUP, sx, sy));
                    host_orders += 1;
                    const auto theirs = ants_of(bob.sim, 1);
                    if (!theirs.empty()) {
                        sim::Command c;
                        c.type = sim::CommandType::GroupMove;
                        c.issuer = 1;
                        c.tile_x = static_cast<int16_t>((t / 10) % 31);
                        c.tile_y = static_cast<int16_t>((t / 20) % 31);
                        c.ants.push_back(theirs[(t / 1000) % theirs.size()]);
                        bob.net.submit(c);
                    }
                }
                if (t == 15000) notch(app, -1);                           // back to 1 in the middle of the match
                duo.step(10);
            }
            check(host_orders >= 39, "the host gave its orders");
            app.net()->freeze();                                          // (the host stops sealing turns: Bob catches up with the last one)
            duo.step(3000);
            check(app.sim().state_hash() == bob.sim.state_hash(), "the host (zoomed, scrolled, clicking) and Bob (no view) are in the same state");
            check(!app.net()->desynced() && app.sim().current_tick() == bob.sim.current_tick() && app.sim().current_tick() > 700, "no desync, the same tick: " + std::to_string(app.sim().current_tick()));
        }
    }
    // 4. a guest that zooms and scrolls: the same
    {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        Peer host;
        Application app;
        check(join_and_start(app, host, network_config(1.0f, false)), "(the zooming guest is in the match)");
        if (app.state() == AppState::Playing) {
            Duo duo{app, host};
            app.hud().dismiss_match_start_modal();
            const LayoutRect view = app.layout().view();
            app.note_pointer(view.x + 300, view.y + 200);
            notch(app, +1);
            uint32_t next = 0;
            for (uint32_t t = 0; t < 30000; t += 10) {
                if (t >= next) {
                    next = t + 900;
                    app.hud().select_all_friendly(app.sim().get_world_state());
                    const int32_t sx = view.x + 60 + static_cast<int32_t>((t / 900) % 11) * 50;
                    const int32_t sy = view.y + 40 + static_cast<int32_t>((t / 900) % 7) * 50;
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONDOWN, sx, sy));
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONUP, sx, sy));
                    app.hud().input_tick(app.renderer().camera(), app.sim().grid().width(), app.sim().grid().height(), view.right() - 2, view.y + 200);
                    const auto theirs = ants_of(host.sim, 0);
                    if (!theirs.empty()) {
                        sim::Command c;
                        c.type = sim::CommandType::GroupMove;
                        c.issuer = 0;
                        c.tile_x = static_cast<int16_t>((t / 10) % 31);
                        c.tile_y = static_cast<int16_t>((t / 25) % 31);
                        c.ants.push_back(theirs[(t / 900) % theirs.size()]);
                        host.net.submit(c);
                    }
                }
                duo.step(10);
            }
            host.net.freeze();
            duo.step(3000);
            check(app.sim().state_hash() == host.sim.state_hash(), "the zoomed guest and the host are in the same state");
            check(!app.net()->desynced() && app.sim().current_tick() == host.sim.current_tick() && app.sim().current_tick() > 500, "no desync, the same tick: " + std::to_string(app.sim().current_tick()));
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    // (SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one)
    std::string only;                                           // --only NAME: run the test NAME alone (pass, out, levels, place, fog, state, cursor, orders, band, scroll, radar, gating, ctrln, wheel, middle, api, ants, setup, fair, settings, start, client, net, guard, garbage)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            only = argv[++i];
        } else {
            std::fprintf(stderr, "usage: test_zoom_view [--only NAME]\n");
            return 2;
        }
    }
    const auto run = [&only](const char* name) { return only.empty() || only == name; };
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);                      // (the environment survives SDL_Quit, which the end of an application calls; a hint does not): nothing is shown or heard
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_Init(SDL_INIT_VIDEO);
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }
    if (run("pass")) test_world_pass(arc);
    if (run("out")) test_zoom_out_pass(arc);
    if (run("levels")) test_level_pictures(arc);
    if (run("place")) test_lattice_placement(arc);
    if (run("fog")) test_fog_levels(arc);
    if (run("state")) test_pass_state(arc);
    if (run("cursor")) test_hud_cursor(arc);
    if (run("orders")) test_hud_orders(arc);
    if (run("band")) test_hud_rubber_band(arc);
    if (run("scroll")) test_hud_minimap_and_scroll(arc);
    if (run("radar")) test_hud_radar_frame(arc);
    if (run("gating")) test_hud_gating(arc);
    if (run("ctrln")) test_hud_ctrl_n(arc);
    if (run("wheel")) test_app_wheel();
    if (run("middle")) test_app_middle_button();
    if (run("api")) test_app_zoom_api();
    if (run("ants")) test_app_ants();
    if (run("fair")) test_app_fairness_local();
    if (run("setup")) test_app_setup_screen();
    if (run("settings")) test_app_settings();
    if (run("start")) test_app_start_view_and_listener();
    if (run("client")) test_local_determinism(arc);
    if (run("net")) test_network_match();
    if (run("guard")) test_app_guards();
    if (run("garbage")) test_app_wheel_garbage();
    std::printf("\nzoom view: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
