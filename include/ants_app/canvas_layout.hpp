#pragma once

// The canvas (widescreen work, milestone M2): the picture that the window shows, and how it sits in the window.
//
// The game draws into SDL's LOGICAL canvas (SDL_RenderSetLogicalSize): a size of its own that the picture's shape gives, 960 x 540 (16:9, the default of a game that is started from the command line), 960 x 600 (16:10, for
// laptops and 16:10 monitors), 1260 x 540 (21:9, ultrawide monitors) or 640 x 480 (the original's, `--aspect 4:3`), which SDL scales into the window by the largest scale that fits, centred, with bars where the
// shapes differ (nearest-neighbour filtering). The shape can be changed while a match runs (Application::set_aspect). The scale is a whole number when
// the window is a multiple of the canvas (1920 x 1080 shows 960 x 540 at 2x, 3840 x 2160 at 4x) and fractional otherwise (2560 x 1440 at 2.667x, 1280 x 720 at 1.333x); a window of
// another shape has bars (2880 x 1800 shows 960 x 540 at 3x with bars of 90 rows above and below). Everybody who asks for the same aspect sees exactly the same world area, so
// no room has to cap it, and the wider shapes show more of the map, as 16:9 does against 4:3 (the map view is 442 x 440, 762 x 500, 762 x 560 and 1062 x 500). A CanvasLayout is any size, not only these four.
//
// This header is pure (no SDL). `CanvasLayout::fit` is SDL's own arithmetic for a logical size (SDL_render.c, UpdateLogicalSize, the letterbox policy), so that the program can
// say where the picture is in the window without asking SDL (the screenshot, the window's first size) and the tests can compare it with SDL itself.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "ants_app/screen_layout.hpp"

namespace ants::app {

/// The shapes of picture that can be asked for (`--aspect`, the settings key `aspect`, the page's selector). The canvas of each is 60 steps of its shape (960 x 540, 960 x 600, 1260 x 540) except the original's own.
enum class Aspect : uint8_t {
    Classic4x3,     // the original's 640 x 480 (`--aspect 4:3`; the aspect of a config that is made by hand, as the tests do: the default of a game is kPlatformDefaultAspect, 16:9)
    Wide16x9,       // 960 x 540
    Wide16x10,      // 960 x 600: the same width as 16:9 and 60 rows more, for a laptop or a 16:10 monitor
    Ultra21x9       // 1260 x 540: the same height as 16:9 and 300 columns more, for an ultrawide monitor
};

/// Every shape, from the narrowest to the widest (the order of the page's selector)
inline constexpr std::array<Aspect, 4> kAllAspects = {Aspect::Classic4x3, Aspect::Wide16x10, Aspect::Wide16x9, Aspect::Ultra21x9};

/// The aspect of a game that is started from the command line when neither `--aspect` nor the settings say: 16:9 on a desktop (requested 2026-10-02: the default should be
/// 16x9) and in the web build (milestone M5: the page's game box is 16:9; the page passes the shape it shows as `--aspect`, `?aspect=4:3` on its address asks for the classic one).
/// `Application::parse_arguments` puts it into the config; an `ApplicationConfig` that is made by hand (the tests') stays at the original's 4:3. The two have names of their own so that a
/// native test can pin the browser build's default too.
inline constexpr Aspect kWebDefaultAspect = Aspect::Wide16x9;
inline constexpr Aspect kDesktopDefaultAspect = Aspect::Wide16x9;
#if defined(__EMSCRIPTEN__)
inline constexpr Aspect kPlatformDefaultAspect = kWebDefaultAspect;
#else
inline constexpr Aspect kPlatformDefaultAspect = kDesktopDefaultAspect;
#endif

inline constexpr int32_t kWideCanvasWidth = 960;
inline constexpr int32_t kWideCanvasHeight = 540;

inline constexpr int32_t kLaptopCanvasHeight = 600;      // 16:10
inline constexpr int32_t kUltrawideCanvasWidth = 1260;   // 21:9

constexpr int32_t canvas_width_of(Aspect aspect) noexcept {
    switch (aspect) {
        case Aspect::Wide16x9:
        case Aspect::Wide16x10: return kWideCanvasWidth;
        case Aspect::Ultra21x9: return kUltrawideCanvasWidth;
        case Aspect::Classic4x3: break;
    }
    return ScreenLayout::kClassicWidth;
}
constexpr int32_t canvas_height_of(Aspect aspect) noexcept {
    switch (aspect) {
        case Aspect::Wide16x9:
        case Aspect::Ultra21x9: return kWideCanvasHeight;
        case Aspect::Wide16x10: return kLaptopCanvasHeight;
        case Aspect::Classic4x3: break;
    }
    return ScreenLayout::kClassicHeight;
}
inline const char* aspect_name(Aspect aspect) noexcept {
    switch (aspect) {
        case Aspect::Wide16x9: return "16:9";
        case Aspect::Wide16x10: return "16:10";
        case Aspect::Ultra21x9: return "21:9";
        case Aspect::Classic4x3: break;
    }
    return "4:3";
}

/// The text of `--aspect`, of the settings key and of the page's `?aspect=`: "4:3", "16:10", "16:9" or "21:9" and nothing else. False with a message that says so for anything else.
inline bool parse_aspect(const std::string& text, Aspect& out, std::string& why) {
    for (const Aspect aspect : kAllAspects) {
        if (text == aspect_name(aspect)) {
            out = aspect;
            return true;
        }
    }
    why = "\"" + text + "\": only 4:3, 16:10, 16:9 and 21:9";
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

    /// Where the PAGES stand: the setup screen and the room, the loading screen, the quick help, the results and the start menu are made for the canvases of 640 x 480 and of 960 x 540 and for no other
    /// (setup_layout.hpp, page_layout.hpp, results_layout.hpp). A canvas of 960 x 540 or more that is not that size (16:10: 960 x 600, 21:9: 1260 x 540) shows the 960 x 540 page centred in it, with the
    /// black of the canvas around it; the match screen is always the whole canvas. Every other canvas is its own page.
    constexpr LayoutRect page() const noexcept {
        const bool bigger = width >= kWideCanvasWidth && height >= kWideCanvasHeight && (width != kWideCanvasWidth || height != kWideCanvasHeight);
        return bigger ? centred(kWideCanvasWidth, kWideCanvasHeight) : rect();
    }

    /// Where a picture of w x h pixels sits when it is centred in the canvas (a 640 x 480 picture in a 960 x 540 canvas is at (160, 30): the match of a classic layout; every screen outside a match is the whole canvas)
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
