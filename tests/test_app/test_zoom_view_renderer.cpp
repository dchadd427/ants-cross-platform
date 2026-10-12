// Zoom view, part 1: the renderer (the world pass against the direct pass, the zoom out pass, every level, the lattice, the fog, the state of the pass).
// (See test_zoom_view.cpp for what the whole program checks.)
#include "zoom_view_fixtures.hpp"

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

namespace {

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

}  // namespace

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

namespace {

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

}  // namespace

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
                    } else {      // between 1 and 2 the filter is the nearest pixel (no linear smoothing), below 1 it is linear
                        const bool crisp = z > 1.0f;  const BestFit want = best_fit(p, view, spec, cx, cy, z, crisp, 2.0);
                        const BestFit other = best_fit(p, view, spec, cx, cy, z, !crisp, 2.0);
                        check(std::fabs(want.sx) <= 1.0 && std::fabs(want.sy) <= 1.0, here + "it is in its place, to a pixel: the specified picture fits best moved by (" + num(want.sx) + ", " + num(want.sy) + ") screen pixels");
                        check(want.error <= 10.0, here + "the picture is the specified one: mean error " + num(want.error) + " of 255 at its best fit");
                        check(want.error + (crisp ? 0.5 : 1.0) <= other.error, here + (crisp ? "it is crisp: closer to the nearest pixel's picture than to the smoothed one (" : "it is smooth: closer to the specified picture than to the nearest pixel's (") + num(want.error) + " against " + num(other.error) + ")");
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

namespace {

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

}  // namespace

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
