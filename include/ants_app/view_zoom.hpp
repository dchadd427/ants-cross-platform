#pragma once

// The zoom of the map view (widescreen work, milestone M4: the mouse wheel).
//
// The map view is a rectangle of the picture (ScreenLayout::view(): 442 x 440 in the original's picture, 762 x 500 in the 16:9 one); the world it shows is a different thing: at the zoom z
// one world pixel is z screen pixels, so the view shows view / z world pixels. There are three levels, 0.5 (the world is drawn half size and smoothed, twice as much of it is seen), 1
// (the original's own picture) and 2 (every world pixel is a crisp 2 x 2 square, half as much is seen). This header is the model of that and nothing else (pure, no SDL): the levels,
// which of them a map and a kind of match offers, the camera after a zoom that keeps the world point under the pointer where it is, the clamps, the scroll model in screen pixels,
// the accumulation of a wheel's precise deltas and the text of the settings key. The renderer draws it, the HUD converts the pointer with it, the application gives it its input
// (renderer.hpp ViewportCamera, hud.cpp, application.cpp).
//
// The camera's origin (the world point at the view's top left corner) lies on a grid of ONE SCREEN PIXEL at the zoom: 2 world pixels at 0.5, 1 at 1 and half a world pixel at 2. So a world
// pixel is never drawn across a screen pixel boundary, and the scroll model of the original (whole screen pixels, edge_scroll.hpp) works unchanged in screen pixels at every zoom.
// At the zoom 1 the grid is the whole world pixel, which is what the camera always was.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace ants::app {
namespace zoom {

inline constexpr float kOut = 0.5f;
inline constexpr float kNormal = 1.0f;
inline constexpr float kIn = 2.0f;
inline constexpr size_t kLevelCount = 3;
/// The levels, from the most of the world to the least (every one is a power of two, so every number here is exact in a float and a double)
inline constexpr std::array<float, kLevelCount> kLevels{kOut, kNormal, kIn};

/// The index of a level in kLevels, or -1 for a number that is not a level
constexpr int level_index(float z) noexcept {
    for (size_t i = 0; i < kLevelCount; ++i) {
        if (kLevels[i] == z) return static_cast<int>(i);
    }
    return -1;
}
constexpr bool is_level(float z) noexcept { return level_index(z) >= 0; }

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

/// The world pixel under a screen pixel that is `offset` screen pixels from the view's corner, when the view's origin is `origin` (world pixels). At the zoom 1 with a whole
/// origin this is origin + offset, as it always was.
inline int32_t world_at(double origin, float z, int32_t offset) noexcept { return static_cast<int32_t>(std::floor(origin + static_cast<double>(offset) / static_cast<double>(z))); }
/// The world coordinate of the EDGE of a screen pixel (the right / bottom edge of a rubber band) rounded up: every world pixel that the screen pixels cover is inside
inline int32_t world_edge_up(double origin, float z, int32_t offset) noexcept { return static_cast<int32_t>(std::ceil(origin + static_cast<double>(offset) / static_cast<double>(z))); }

/// What a kind of match allows. min_zoom is 0.5 in a local game and a game with bots, and 1 in a match of the network (a zoom-out shows more of the map than the other players
/// see: not fair; zooming in is always fair).
struct Limits {
    float min_zoom{kOut};
    float max_zoom{kIn};

    static constexpr Limits any() noexcept { return Limits{kOut, kIn}; }
    static constexpr Limits no_zoom_out() noexcept { return Limits{kNormal, kIn}; }
    constexpr bool operator==(const Limits& o) const noexcept { return min_zoom == o.min_zoom && max_zoom == o.max_zoom; }
    constexpr bool operator!=(const Limits& o) const noexcept { return !(*this == o); }
};

/// Does a view of view_w x view_h screen pixels over a map of map_w_px x map_h_px world pixels offer this level? The level 1 always (it is the original's picture); a level above it
/// when the limits allow it; a level below it when the limits allow it AND the level above it leaves part of the map unseen (a zoom-out that adds nothing but black around a map
/// that is already entirely in the view is not offered: "the zoom-out never shows more than the map")
inline bool offered(float level, const Limits& limits, int32_t view_w, int32_t view_h, int64_t map_w_px, int64_t map_h_px) noexcept {
    const int idx = level_index(level);
    if (idx < 0) return false;
    if (level == kNormal) return true;
    if (level < limits.min_zoom || level > limits.max_zoom) return false;
    if (level > kNormal) return true;
    const float above = kLevels[static_cast<size_t>(idx) + 1];
    return static_cast<int64_t>(visible(view_w, above)) < map_w_px || static_cast<int64_t>(visible(view_h, above)) < map_h_px;
}

/// The next level in a direction (positive: zoom in, negative: zoom out) that is offered, or `current` when there is none. A `current` that is not a level counts as 1.
inline float step(float current, int direction, const Limits& limits, int32_t view_w, int32_t view_h, int64_t map_w_px, int64_t map_h_px) noexcept {
    if (direction == 0) return current;
    const int dir = direction > 0 ? 1 : -1;
    int idx = level_index(current);
    if (idx < 0) idx = level_index(kNormal);
    for (int i = idx + dir; i >= 0 && i < static_cast<int>(kLevelCount); i += dir) {
        if (offered(kLevels[static_cast<size_t>(i)], limits, view_w, view_h, map_w_px, map_h_px)) return kLevels[static_cast<size_t>(i)];
    }
    return is_level(current) ? current : kNormal;
}

/// The level that a match starts with, from the one that was remembered: that one when it is offered, else the level 1
inline float level_for_match(float remembered, const Limits& limits, int32_t view_w, int32_t view_h, int64_t map_w_px, int64_t map_h_px) noexcept {
    return offered(remembered, limits, view_w, view_h, map_w_px, map_h_px) ? remembered : kNormal;
}

/// The origin on one axis kept inside the map. A map larger than the world that the view shows has the range [0, map - visible]; one that is not larger is fixed: at the origin that
/// centres it in the view (truncated toward zero, as the original's integer division does; the world pixel 0 is then right of the view's edge, what the map does not cover stays
/// black) when `centre_small`, else at 0.
inline double clamp_origin(double v, float z, int32_t view_len, int64_t map_px, bool centre_small) noexcept {
    const double vis = visible_exact(view_len, z);
    if (static_cast<double>(map_px) > vis) return std::clamp(v, 0.0, static_cast<double>(map_px) - vis);
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
// The wheel
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// What one wheel event says, in notches (positive: the wheel is rolled away from the user: zoom in). SDL gives the whole notches (`y`) and, since 2.0.18, the exact amount
/// (`precise_y`, a trackpad's small fractions); an event whose precise amount is 0 but whose whole amount is not (an old SDL) is its whole amount. `flipped` is SDL's
/// SDL_MOUSEWHEEL_FLIPPED: the system's "natural scrolling" has already inverted the numbers, SDL says "multiply by -1 to change them back", and that is done here, so the wheel
/// rolled away zooms in whatever the system's setting is.
constexpr double wheel_amount(int32_t y, float precise_y, bool flipped) noexcept {
    const double amount = precise_y != 0.0f ? static_cast<double>(precise_y) : static_cast<double>(y);
    return flipped ? -amount : amount;
}

/// The wheel's deltas added up to whole steps: one notch is one step, a trackpad's fractions add up (0.4 + 0.4 + 0.4 is one step with 0.2 left); a pause of more than kStaleMs
/// forgets a left-over fraction and a change of direction starts again from nothing. A step is a change of one level.
class WheelAccumulator {
public:
    static constexpr uint32_t kStaleMs = 500;
    static constexpr int kMaxSteps = 8;           // (one event cannot ask for more: there are three levels)

    /// Feeds one event (its amount in notches and its time in ms); returns the whole steps it completes: positive zooms in, negative zooms out
    int feed(double amount, uint32_t time_ms) noexcept {
        if (have_ && static_cast<int64_t>(time_ms) - static_cast<int64_t>(last_ms_) > static_cast<int64_t>(kStaleMs)) acc_ = 0.0;
        have_ = true;
        last_ms_ = time_ms;
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

/// The text of a level as the settings file keeps it: "0.5", "1" or "2"
inline const char* level_name(float z) noexcept {
    if (z == kOut) return "0.5";
    if (z == kIn) return "2";
    return "1";
}

/// The text of the key `zoom` (and of `--zoom`): a number that is a level, "0.5", "1" or "2" (also written .5, 0.50, 1.0, 2.00 ...: digits and one point, nothing else), exactly; false with a
/// message for anything else
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
        if (end != nullptr && *end == '\0' && is_level(static_cast<float>(v)) && static_cast<double>(static_cast<float>(v)) == v) {
            out = static_cast<float>(v);
            return true;
        }
    }
    why = "\"" + text + "\": only 0.5, 1 and 2";
    return false;
}

}  // namespace zoom
}  // namespace ants::app
