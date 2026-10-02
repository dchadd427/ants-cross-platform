#pragma once

// The canvas (widescreen work, milestone M2): the picture that the window shows, and how it sits in the window.
//
// The game draws into SDL's LOGICAL canvas (SDL_RenderSetLogicalSize): a fixed size of its own, 640 x 480 (the original's, the default) or 960 x 540 (16:9, `--aspect 16:9`),
// which SDL scales into the window by the largest scale that fits, centred, with bars where the shapes differ (nearest-neighbour filtering). The scale is a whole number when
// the window is a multiple of the canvas (1920 x 1080 shows 960 x 540 at 2x, 3840 x 2160 at 4x) and fractional otherwise (2560 x 1440 at 2.667x, 1280 x 720 at 1.333x); a window of
// another shape has bars (2880 x 1800 shows 960 x 540 at 3x with bars of 90 rows above and below). Everybody who asks for the same aspect sees exactly the same world area, so
// no room has to cap it. Fitting the canvas to the shape of the monitor is a later option: a CanvasLayout is any size, not only these two.
//
// This header is pure (no SDL). `CanvasLayout::fit` is SDL's own arithmetic for a logical size (SDL_render.c, UpdateLogicalSize, the letterbox policy), so that the program can
// say where the picture is in the window without asking SDL (the screenshot, the window's first size) and the tests can compare it with SDL itself.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "ants_app/screen_layout.hpp"

namespace ants::app {

/// The shapes of picture that can be asked for (`--aspect`, the settings key `aspect`)
enum class Aspect : uint8_t {
    Classic4x3,     // the original's 640 x 480 (the default)
    Wide16x9        // 960 x 540
};

/// The aspect of a game that is started from the command line when neither `--aspect` nor the settings say: 16:9 on a desktop (the owner's priority, 2026-10-02: "The default should be
/// 16x9"), the original's 4:3 in the web build until its page shows 16:9 (milestone M5: this is the one line that flips the web). `Application::parse_arguments` puts it into the config;
/// an `ApplicationConfig` that is made by hand (the tests') stays at the original's 4:3.
#if defined(__EMSCRIPTEN__)
inline constexpr Aspect kPlatformDefaultAspect = Aspect::Classic4x3;
#else
inline constexpr Aspect kPlatformDefaultAspect = Aspect::Wide16x9;
#endif

inline constexpr int32_t kWideCanvasWidth = 960;
inline constexpr int32_t kWideCanvasHeight = 540;

constexpr int32_t canvas_width_of(Aspect aspect) noexcept { return aspect == Aspect::Wide16x9 ? kWideCanvasWidth : ScreenLayout::kClassicWidth; }
constexpr int32_t canvas_height_of(Aspect aspect) noexcept { return aspect == Aspect::Wide16x9 ? kWideCanvasHeight : ScreenLayout::kClassicHeight; }
inline const char* aspect_name(Aspect aspect) noexcept { return aspect == Aspect::Wide16x9 ? "16:9" : "4:3"; }

/// The text of `--aspect` and of the settings key: "16:9" or "4:3" and nothing else (no other shape exists yet). False with a message that says so for anything else.
inline bool parse_aspect(const std::string& text, Aspect& out, std::string& why) {
    if (text == "16:9") {
        out = Aspect::Wide16x9;
        return true;
    }
    if (text == "4:3") {
        out = Aspect::Classic4x3;
        return true;
    }
    why = "\"" + text + "\": only 16:9 and 4:3 for now";
    return false;
}

/// How a canvas is shown in a window of out_w x out_h pixels
struct CanvasFit {
    int32_t out_w{0};
    int32_t out_h{0};
    double scale{0.0};                // window pixels per canvas pixel
    LayoutRect viewport;              // the picture's rectangle in window pixels
    /// Every canvas pixel is a square of scale x scale window pixels
    bool whole_scale() const noexcept { return scale > 0.0 && std::floor(scale) == scale; }
    int32_t bar_left() const noexcept { return viewport.x; }
    int32_t bar_right() const noexcept { return out_w - viewport.right(); }
    int32_t bar_top() const noexcept { return viewport.y; }
    int32_t bar_bottom() const noexcept { return out_h - viewport.bottom(); }
};

/// The canvas of the picture: a size, any size
struct CanvasLayout {
    int32_t width{ScreenLayout::kClassicWidth};
    int32_t height{ScreenLayout::kClassicHeight};

    static constexpr CanvasLayout of(Aspect aspect) noexcept { return CanvasLayout{canvas_width_of(aspect), canvas_height_of(aspect)}; }
    constexpr bool operator==(const CanvasLayout& o) const noexcept { return width == o.width && height == o.height; }
    constexpr bool operator!=(const CanvasLayout& o) const noexcept { return !(*this == o); }
    constexpr LayoutRect rect() const noexcept { return LayoutRect{0, 0, width, height}; }

    /// Where a picture of w x h pixels sits when it is centred in the canvas: the original's 640 x 480 pages and the match screen in a 960 x 540 canvas are at (160, 30)
    constexpr LayoutRect centred(int32_t w, int32_t h) const noexcept { return LayoutRect{(width - w) / 2, (height - h) / 2, w, h}; }

    /// SDL's logical size, for a window of out_w x out_h pixels: the aspect ratios are compared with a tolerance of 0.0001 (equal: the canvas fills the window); the canvas is
    /// scaled by the window's width over the canvas's width when it is wider than the window (bars above and below) and by the heights when it is narrower (bars at the sides);
    /// the picture is centred, a bar of an odd number of pixels gives the extra pixel to the right / bottom. The arithmetic is SDL's (single precision floats and floor).
    CanvasFit fit(int32_t out_w, int32_t out_h) const noexcept {
        CanvasFit f;
        f.out_w = out_w;
        f.out_h = out_h;
        if (out_w <= 0 || out_h <= 0 || width <= 0 || height <= 0) return f;
        const float want = static_cast<float>(width) / static_cast<float>(height);
        const float real = static_cast<float>(out_w) / static_cast<float>(out_h);
        float scale = 1.0f;
        LayoutRect v;
        if (std::fabs(want - real) < 0.0001f) {
            scale = static_cast<float>(out_w) / static_cast<float>(width);
            v = LayoutRect{0, 0, out_w, out_h};
        } else if (want > real) {                       // a wider canvas than the window: letterbox (bars above and below)
            scale = static_cast<float>(out_w) / static_cast<float>(width);
            v.x = 0;
            v.w = out_w;
            v.h = static_cast<int32_t>(std::floor(static_cast<float>(height) * scale));
            v.y = (out_h - v.h) / 2;
        } else {                                        // a narrower canvas: pillarbox (bars at the sides)
            scale = static_cast<float>(out_h) / static_cast<float>(height);
            v.y = 0;
            v.h = out_h;
            v.w = static_cast<int32_t>(std::floor(static_cast<float>(width) * scale));
            v.x = (out_w - v.w) / 2;
        }
        f.scale = static_cast<double>(scale);
        f.viewport = v;
        return f;
    }
};

}  // namespace ants::app
