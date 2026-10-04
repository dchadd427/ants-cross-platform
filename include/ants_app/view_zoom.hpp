#pragma once

// The zoom of the map view (widescreen work, milestone M4: the mouse wheel; many levels since the batch after v0.2.0).
//
// The map view is a rectangle of the picture (ScreenLayout::view(): 442 x 440 in the original's picture, 762 x 500 in the 16:9 one); the world it shows is a different thing: at the zoom z
// one world pixel is z screen pixels, so the view shows view / z world pixels. THE LEVELS are the series 2^(k/4), four to a doubling, from 2 (every world pixel a crisp 2 x 2 square, the
// most that the view zooms in) down to the map's own limit: 2, 1.68, 1.41, 1.19, 1 (the original's picture, drawn exactly as it always was), 0.84, 0.71, 0.59, 0.5, 0.42, 0.35, 0.30, 0.25 ...
// THE LIMIT of the zoom-out is the map's: the view never shows anything outside the map, so the smallest zoom is max(view_w / map_w, view_h / map_h) (the map's width or its height just fills
// the view, whichever comes first; a map that the view already covers at 1 has no zoom-out) and that exact number is the last level. It depends on the view's shape: on TREASURE it is 0.230 in the
// original's 4:3 picture (the whole map) and 0.397 in the 16:9 one (two thirds of it). 2, 1, 0.5, 0.25 ... are exact in a float: their
// pictures are exact (nearest, or the average of 2 x 2 world pixels at every halving). This header is the model of all that and nothing else (pure, no SDL): the levels, the nearest and the
// next one, the camera after a zoom that keeps the world point under the pointer where it is, the clamps, the scroll model in screen pixels, the plan of the renderer's world pass, the
// accumulation of a wheel's precise deltas and the text of the settings key. The renderer draws it, the HUD converts the pointer with it, the application gives it its input
// (renderer.hpp ViewportCamera, hud.cpp, application.cpp).
//
// The camera's origin (the world point at the view's top left corner) lies on a grid of ONE SCREEN PIXEL at the zoom: 1 / z world pixels (2 at 0.5, 1 at 1, half a pixel at 2, 0.71 at 1.41).
// So the picture is a function of the lattice of screen pixels at the zoom (the pixel m of the lattice shows the world at m / z), whatever the camera's origin is: a scroll by whole screen
// pixels moves the picture and never changes it (no shimmer), and the scroll model of the original (whole screen pixels, edge_scroll.hpp) works unchanged in screen pixels at every zoom.
// At the zoom 1 the grid is the whole world pixel, which is what the camera always was.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace ants::app {
namespace zoom {

inline constexpr float kNormal = 1.0f;
/// The most that the view zooms in: a world pixel is a crisp 2 x 2 square
inline constexpr float kIn = 2.0f;
/// The levels are the series 2^(k/4): four to a doubling
inline constexpr int kStepsPerDoubling = 4;
/// The smallest zoom that the settings key and --zoom accept (the map's own limit is never smaller than this: a 128 tile map over the original's picture needs 0.11)
inline constexpr float kSmallest = 0.05f;
/// A level of the series that lies closer than this ratio above the map's limit is left out (the step to the limit would be smaller than a quarter of a step)
inline constexpr double kMinStepRatio = 1.044;
/// The levels that a screen shows between 1 and 2 are drawn smoothed (the linear filter) unless a test says otherwise; 2 itself is always the crisp 2 x 2 square
inline constexpr bool kSmoothUpscale = true;

/// The k-th level of the series 2^(k/4): k = 4 is 2, 0 is 1, -4 is 0.5. Every power of two is exact in a float and a double (a power of two times a root is one rounding).
inline float series(int k) noexcept {
    static constexpr double kRoot[kStepsPerDoubling] = {1.0, 1.189207115002721, 1.414213562373095, 1.681792830507429};
    int q = k / kStepsPerDoubling;
    int r = k % kStepsPerDoubling;
    if (r < 0) {
        r += kStepsPerDoubling;
        --q;
    }
    return static_cast<float>(std::ldexp(kRoot[r], q));
}

/// The world pixels that `screen_len` screen pixels show at the zoom `z` (rounded up: a world pixel that is shown in part is a shown one): 2 x at 0.5, 1 x at 1, half at 2
inline int32_t visible(int32_t screen_len, float z) noexcept { return static_cast<int32_t>(std::ceil(static_cast<double>(screen_len) / static_cast<double>(z))); }
/// The same without rounding
inline double visible_exact(int32_t screen_len, float z) noexcept { return static_cast<double>(screen_len) / static_cast<double>(z); }

/// The grid of the camera's origin, in world pixels: one screen pixel at the zoom
inline double grid(float z) noexcept { return 1.0 / static_cast<double>(z); }
/// A world coordinate rounded (half up) to the grid of the zoom; a coordinate on the grid is returned as it is
inline double snap(double v, float z) noexcept {
    const double g = grid(z);
    return std::floor(v / g + 0.5) * g;
}
/// ... and truncated toward zero (the way the original's integer division centres a small map)
inline double snap_toward_zero(double v, float z) noexcept {
    const double g = grid(z);
    return std::trunc(v / g) * g;
}
/// ... and rounded down (a point of the grid that is not beyond `v`; a coordinate a hair below a point of the grid, the rounding of a division, counts as on it)
inline double snap_down(double v, float z) noexcept {
    const double g = grid(z);
    return std::floor(v / g + 1e-9) * g;
}
/// The map's size in WHOLE screen pixels at the zoom: the origin of the camera ends where the view's last pixel meets the map's last whole pixel, so this is what the scroll model works with
/// (a map of 1920 world pixels is 1612 screen pixels and 0.6 at the zoom 0.84: the 0.6 is never in a view that is on the lattice)
inline int32_t map_screen(int64_t map_px, float z) noexcept { return static_cast<int32_t>(std::floor(static_cast<double>(map_px) * static_cast<double>(z) + 1e-9)); }

/// The world pixel under the CENTRE of a screen pixel that is `offset` screen pixels from the view's corner, when the view's origin is `origin` (world pixels): floor(origin + (offset + 0.5) / z).
/// A click means the middle of the pixel, so it picks the tile that covers most of the pixel (below 1 and at 1.19 / 1.41 / 1.68 a pixel straddles tile edges; its left edge would pick the
/// neighbour that covers less). At the zoom 1 with a whole origin this is origin + offset, as it always was, and at 2 the tile of every pixel is the same as that of its left edge.
/// A rubber band's rectangle takes this at both of its edges.
inline int32_t world_at(double origin, float z, int32_t offset) noexcept { return static_cast<int32_t>(std::floor(origin + (static_cast<double>(offset) + 0.5) / static_cast<double>(z))); }

/// What a kind of match or a renderer allows. There is no limit of the kind of match: every player of a match runs the same game and has the same levels, and the zoom is the player's own view
/// (before the batch after v0.2.0 a match of the network offered no zoom-out). A renderer that cannot make the offscreen target of a zoom can draw the original's picture only.
struct Limits {
    float min_zoom{0.0f};        // 0: no limit of its own (the map's limit decides)
    float max_zoom{kIn};

    static constexpr Limits any() noexcept { return Limits{0.0f, kIn}; }
    /// Only the original's picture: what a renderer that cannot make the offscreen target of a zoom can draw (a zoom, in or out, is drawn through it)
    static constexpr Limits only_normal() noexcept { return Limits{kNormal, kNormal}; }
    constexpr bool operator==(const Limits& o) const noexcept { return min_zoom == o.min_zoom && max_zoom == o.max_zoom; }
    constexpr bool operator!=(const Limits& o) const noexcept { return !(*this == o); }
};

/// What decides the levels of a view over a map: the view's size in screen pixels, the map's in world pixels, and the most world pixels on one axis that the renderer's offscreen target
/// can hold (the device's texture size less the margins of the pass; 0: no limit). A map of size 0 (none is loaded) has no zoom-out.
struct Fit {
    int32_t view_w{0};
    int32_t view_h{0};
    int64_t map_w{0};
    int64_t map_h{0};
    int32_t max_world{0};
};

/// The smallest zoom that never shows anything outside the map: max(view_w / map_w, view_h / map_h), raised to what the target can hold, never below kSmallest and never above 1 (a map
/// that the view covers at 1 has no zoom-out). A float that is not below the exact number, so that the world seen at it is never more than the map.
inline float floor_zoom(const Fit& fit) noexcept {
    if (fit.map_w <= 0 || fit.map_h <= 0 || fit.view_w <= 0 || fit.view_h <= 0) return kNormal;
    double d = std::max(static_cast<double>(fit.view_w) / static_cast<double>(fit.map_w), static_cast<double>(fit.view_h) / static_cast<double>(fit.map_h));
    if (fit.max_world > 0) d = std::max(d, std::max(static_cast<double>(fit.view_w), static_cast<double>(fit.view_h)) / static_cast<double>(fit.max_world));
    if (!(d < 1.0)) return kNormal;
    float z = std::max(static_cast<float>(d), kSmallest);
    const double largest_view = static_cast<double>(std::max(fit.view_w, fit.view_h));
    for (int i = 0; i < 8 && (static_cast<double>(fit.view_w) / static_cast<double>(z) > static_cast<double>(fit.map_w) ||
                              static_cast<double>(fit.view_h) / static_cast<double>(z) > static_cast<double>(fit.map_h) ||
                              (fit.max_world > 0 && largest_view / static_cast<double>(z) > static_cast<double>(fit.max_world))); ++i) {
        z = std::nextafter(z, 2.0f);
    }
    return std::min(z, kNormal);
}

/// The levels that a view over a map offers, from the most zoomed in to the most zoomed out: the series from 2 (the limits' own top) down to 1, which is always there (it is the original's
/// picture), then the series below 1 as far as the map's limit allows, and the limit itself as the last level.
inline std::vector<float> levels(const Fit& fit, const Limits& limits) {
    std::vector<float> out;
    const float top = std::min(limits.max_zoom, kIn);
    for (int k = kStepsPerDoubling; k >= 0; --k) {
        const float z = series(k);
        if (k == 0 || z <= top * (1.0f + 1e-6f)) out.push_back(z);
    }
    const float bottom = std::max(floor_zoom(fit), limits.min_zoom);
    if (bottom >= kNormal) return out;
    for (int k = -1; k >= -48; --k) {
        const float z = series(k);
        if (static_cast<double>(z) < static_cast<double>(bottom) * kMinStepRatio) break;
        out.push_back(z);
    }
    out.push_back(bottom);
    return out;
}

/// The level of `list` that is nearest to `z` in ratio (a zoom is a factor: 0.7 is nearer to 0.71 than to 0.59); the first (the most zoomed in) when two are equally near
inline float nearest(float z, const std::vector<float>& list) noexcept {
    if (list.empty()) return kNormal;
    if (!std::isfinite(z) || z <= 0.0f) return kNormal;
    float best = list.front();
    double best_d = std::abs(std::log(static_cast<double>(z) / static_cast<double>(best)));
    for (const float l : list) {
        const double d = std::abs(std::log(static_cast<double>(z) / static_cast<double>(l)));
        if (d < best_d) {
            best = l;
            best_d = d;
        }
    }
    return best;
}

/// Does the view over the map offer exactly this level?
inline bool offered(float level, const Fit& fit, const Limits& limits) noexcept {
    const std::vector<float> list = levels(fit, limits);
    return std::find(list.begin(), list.end(), level) != list.end();
}

/// The next level in a direction (positive: zoom in, negative: zoom out) beyond `current`, which need not be a level, or `current` when there is none
inline float step(float current, int direction, const Fit& fit, const Limits& limits) noexcept {
    if (direction == 0 || !std::isfinite(current) || current <= 0.0f) return current;
    const std::vector<float> list = levels(fit, limits);            // (descending)
    if (direction > 0) {
        for (size_t i = list.size(); i-- > 0;) {
            if (list[i] > current * (1.0f + 1e-6f)) return list[i];
        }
    } else {
        for (const float l : list) {
            if (l < current * (1.0f - 1e-6f)) return l;
        }
    }
    return current;
}

/// The level that a match starts with, from the one that was remembered (or given with --zoom): the nearest level that the view over this map offers (1 for anything that is not a zoom)
inline float level_for_match(float remembered, const Fit& fit, const Limits& limits) noexcept {
    if (!std::isfinite(remembered) || remembered <= 0.0f) return kNormal;
    return nearest(remembered, levels(fit, limits));
}

/// The origin on one axis kept inside the map. A map larger than the world that the view shows has the range [0, map - visible], whose far end is rounded down to the grid (the camera is
/// always on the lattice of screen pixels at the zoom; at 0.5, 1 and 2 the end is on it already, and at the map's limit it is 0: the view shows the whole width); one that is not larger is
/// fixed: at the origin that centres it in the view (truncated toward zero, as the original's integer division does; the world pixel 0 is then right of the view's edge, what the map does not
/// cover stays black) when `centre_small`, else at 0.
inline double clamp_origin(double v, float z, int32_t view_len, int64_t map_px, bool centre_small) noexcept {
    const double vis = visible_exact(view_len, z);
    if (static_cast<double>(map_px) > vis) return std::clamp(v, 0.0, snap_down(static_cast<double>(map_px) - vis, z));
    return centre_small ? snap_toward_zero((static_cast<double>(map_px) - vis) / 2.0, z) : 0.0;
}

/// A camera: the world point at the view's top left corner (on the grid of its zoom) and the zoom
struct Camera {
    double x{0.0};
    double y{0.0};
    float zoom{kNormal};
};

/// The camera after the zoom changes from `from` to `to`, keeping the world point that is under the screen pixel (anchor_x, anchor_y) (offsets from the view's top left corner) under it:
/// the origin moves by anchor / from - anchor / to, is put on the grid of the new zoom and is held inside the map (clamp_origin) ... and the world point only stays under the pointer as far as
/// the clamps allow (at the edge of the map the view cannot go beyond it). View and map sizes: the view's size in screen pixels, the map's in world pixels.
inline Camera zoomed(const Camera& camera, float to, int32_t anchor_x, int32_t anchor_y, int32_t view_w, int32_t view_h, int64_t map_w_px, int64_t map_h_px, bool centre_small) noexcept {
    const double ax = static_cast<double>(anchor_x);
    const double ay = static_cast<double>(anchor_y);
    Camera out;
    out.zoom = to;
    out.x = camera.x + ax / static_cast<double>(camera.zoom) - ax / static_cast<double>(to);
    out.y = camera.y + ay / static_cast<double>(camera.zoom) - ay / static_cast<double>(to);
    out.x = clamp_origin(snap(out.x, to), to, view_w, map_w_px, centre_small);
    out.y = clamp_origin(snap(out.y, to), to, view_h, map_h_px, centre_small);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The plan of the world pass
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// How the renderer draws a world at a zoom other than 1 (renderer.cpp, begin_world_target / end_world_target): the world is drawn at ONE TEXEL PER WORLD PIXEL into an offscreen target
/// that covers what the view shows (and a margin of texels on each side, for the filter), halved `depth` times when the zoom is below 0.5 (every halving is the exact average of 2 x 2
/// texels, so a zoom-out far below 0.5 is a smooth average, not a sparse sample), and the last level is scaled by `scale` (in [0.5, 1) for a zoom below 1, the zoom itself above) into whole
/// screen pixels. The target starts at a world pixel that is a multiple of `align`, a power of two that is a multiple of 2^depth and, below 1, at least the world pixels of one lattice pixel
/// (1 / z): the halvings then group the same world pixels whatever the camera's origin is, and at the zooms that are powers of two (whose origins are multiples of 1 / z) the picture lands on
/// whole screen pixels, so that their pictures are exact (nearest at 2, the average of 2 x 2 or 4 x 4 world pixels below 1).
struct Pass {
    int depth{0};
    int32_t align{1};
    int32_t x0{0};                  // the world pixel of the target's first texel (a multiple of `align`, inside the map)
    int32_t y0{0};
    int32_t w{0};                   // the target's size in texels (multiples of `align`, inside the map); 0 when nothing of the map is in view
    int32_t h{0};
    int32_t cap_w{0};               // the size that a target needs for any origin at this zoom (what the renderer allocates: it changes with the zoom, not with the camera)
    int32_t cap_h{0};
    double scale{1.0};              // screen pixels per texel of the last level
    double left{0.0};               // where the last level's first texel lands, in screen pixels from the view's corner (fractional, usually negative)
    double top{0.0};
    bool smooth{false};             // the linear filter for the last copy; false: nearest (the zoom 2, and the copy of the zoom 1)
};

/// Does a zoom between 1 and 2 use the linear filter? 1 and 2 never do (they are exact)
inline bool smooth_at(float z, bool smooth_upscale) noexcept { return z != kNormal && z != kIn && (z < kNormal || smooth_upscale); }

/// The plan for the camera origin (ox, oy) (world pixels, on the grid of the zoom) at the zoom z, over a view of view_w x view_h screen pixels and a map of map_w x map_h world pixels
inline Pass plan(double ox, double oy, float z, int32_t view_w, int32_t view_h, int64_t map_w, int64_t map_h, bool smooth_upscale = kSmoothUpscale) noexcept {
    Pass p;
    const double zd = static_cast<double>(z);
    double s = zd;
    while (s < 0.5 && p.depth < 8) {
        s *= 2.0;
        ++p.depth;
    }
    int64_t u = int64_t{1};
    while (zd < 1.0 && static_cast<double>(u) * zd < 1.0 - 1e-12 && u < 64) u <<= 1;
    p.align = static_cast<int32_t>(u);
    const double vis_w = static_cast<double>(view_w) / zd;
    const double vis_h = static_cast<double>(view_h) / zd;
    const int64_t end_w = (map_w / u) * u;
    const int64_t end_h = (map_h / u) * u;
    const auto lo = [u](double o) { return static_cast<int64_t>(std::floor(o / static_cast<double>(u))) * u - u; };
    const auto hi = [u](double o, double vis) { return static_cast<int64_t>(std::ceil((o + vis) / static_cast<double>(u))) * u + u; };
    const int64_t x0 = std::max<int64_t>(0, lo(ox));
    const int64_t y0 = std::max<int64_t>(0, lo(oy));
    const int64_t x1 = std::min<int64_t>(end_w, hi(ox, vis_w));
    const int64_t y1 = std::min<int64_t>(end_h, hi(oy, vis_h));
    p.x0 = static_cast<int32_t>(x0);
    p.y0 = static_cast<int32_t>(y0);
    p.w = static_cast<int32_t>(std::max<int64_t>(0, x1 - x0));
    p.h = static_cast<int32_t>(std::max<int64_t>(0, y1 - y0));
    p.cap_w = static_cast<int32_t>(std::min<int64_t>(end_w, static_cast<int64_t>(std::ceil(vis_w / static_cast<double>(u))) * u + 4 * u));
    p.cap_h = static_cast<int32_t>(std::min<int64_t>(end_h, static_cast<int64_t>(std::ceil(vis_h / static_cast<double>(u))) * u + 4 * u));
    p.scale = s;
    p.left = (static_cast<double>(x0) - ox) * zd;
    p.top = (static_cast<double>(y0) - oy) * zd;
    p.smooth = smooth_at(z, smooth_upscale);
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The wheel
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// What one wheel event says, in notches (positive: the wheel is rolled away from the user: zoom in). SDL gives the whole notches (`y`) and, since 2.0.18, the exact amount
/// (`precise_y`, a trackpad's small fractions); an event whose precise amount is 0 but whose whole amount is not (an old SDL) is its whole amount. `flipped` is SDL's
/// SDL_MOUSEWHEEL_FLIPPED: the system's "natural scrolling" has already inverted the numbers, SDL says "multiply by -1 to change them back", and that is done here, so the wheel
/// rolled away zooms in whatever the system's setting is.
/// A precise amount that is not a number or is infinite (a broken driver or a synthetic event) says nothing: 0 (a conversion of NaN to an int is undefined, and a NaN in the accumulator would
/// stay there for good).
inline double wheel_amount(int32_t y, float precise_y, bool flipped) noexcept {
    if (!std::isfinite(precise_y)) return 0.0;
    const double amount = precise_y != 0.0f ? static_cast<double>(precise_y) : static_cast<double>(y);
    return flipped ? -amount : amount;
}

/// The wheel's deltas added up to whole steps. An event worth a step or more (a mouse notch: 1, or 1.2 when the browser's 120 is taken at the page's 100 a step) is the nearest whole number of
/// steps, at least one, and leaves no fraction (a kept 0.2 made the fifth notch of a burst two steps); events worth less than a step (a trackpad's) add up (0.4 + 0.4 + 0.4 is one step with
/// 0.2 left). A pause of more than kStaleMs forgets a left-over fraction and a change of direction starts again from nothing. A step is a change of one level.
class WheelAccumulator {
public:
    static constexpr uint32_t kStaleMs = 500;
    static constexpr int kMaxSteps = 8;           // (the most that one event can ask for: a burst of an absurd amount is not a dozen levels at once)

    /// Feeds one event (its amount in notches and its time in ms); returns the whole steps it completes: positive zooms in, negative zooms out
    int feed(double amount, uint32_t time_ms) noexcept {
        if (!std::isfinite(amount)) return 0;                                // (an event that is not a number is no event: nothing is added and nothing is forgotten)
        amount = std::clamp(amount, -static_cast<double>(kMaxSteps), static_cast<double>(kMaxSteps));        // (an absurd amount is the most that one event can ask for)
        if (have_ && static_cast<int64_t>(time_ms) - static_cast<int64_t>(last_ms_) > static_cast<int64_t>(kStaleMs)) acc_ = 0.0;
        have_ = true;
        last_ms_ = time_ms;
        if (std::fabs(amount) >= 1.0) {
            acc_ = 0.0;
            return static_cast<int>(std::lround(amount));                    // (at least one: 1.2 is 1, 1.6 is 2)
        }
        if (amount * acc_ < 0.0) acc_ = 0.0;
        acc_ += amount;
        const double whole = std::trunc(acc_);
        acc_ -= whole;
        return static_cast<int>(std::clamp(whole, -static_cast<double>(kMaxSteps), static_cast<double>(kMaxSteps)));
    }
    void reset() noexcept {
        acc_ = 0.0;
        have_ = false;
    }
    double pending() const noexcept { return acc_; }

private:
    double acc_{0.0};
    uint32_t last_ms_{0};
    bool have_{false};
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The settings key `zoom`
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// The text of a level as the settings file keeps it: three significant digits ("2", "1.41", "1", "0.841", "0.5", "0.397"): more than the series' steps need, so that reading it back
/// finds the same level (a level is chosen again as the nearest one that the view over the map offers)
inline std::string level_name(float z) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(z));
    return buf;
}

/// The text of the key `zoom` (and of `--zoom`): a number from kSmallest (0.05) to 2 in digits and one point (also .5, 0.50, 1.0 ...: nothing else); any such number is a zoom, and the
/// level that a match starts with is the nearest one that its view offers. False with a message for anything else.
inline bool parse_level(const std::string& text, float& out, std::string& why) {
    bool plain = !text.empty();
    int points = 0;
    int digits = 0;
    for (const char c : text) {
        if (c == '.') ++points;
        else if (c >= '0' && c <= '9') ++digits;
        else plain = false;
    }
    if (plain && points <= 1 && digits > 0) {
        char* end = nullptr;
        const double v = std::strtod(text.c_str(), &end);
        const float f = static_cast<float>(v);
        if (end != nullptr && *end == '\0' && f >= kSmallest && f <= kIn) {
            out = f;
            return true;
        }
    }
    why = "\"" + text + "\": a zoom from 0.05 to 2";
    return false;
}

}  // namespace zoom
}  // namespace ants::app
