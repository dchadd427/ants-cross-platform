#pragma once

// The canvas (widescreen work, milestone M2): the picture that the window shows, and how it sits in the window.
//
// The game draws into SDL's LOGICAL canvas (SDL_RenderSetLogicalSize): a size of its own that the picture's shape gives, 960 x 540 (16:9, the default of a game that is started from the command line), 960 x 600 (16:10, for
// laptops and 16:10 monitors), 960 x 640 (3:2), 1200 x 540 (20:9, phones held sideways), 1260 x 540 (21:9, ultrawide monitors), 1920 x 540 (32:9) or 640 x 480 (the original's, `--aspect 4:3`), which SDL scales into the window
// by the largest scale that fits, centred, with bars where the shapes differ (nearest-neighbour filtering). The shape can be changed while a match runs (Application::set_aspect). The scale is a whole number when
// the window is a multiple of the canvas (1920 x 1080 shows 960 x 540 at 2x, 3840 x 2160 at 4x) and fractional otherwise (2560 x 1440 at 2.667x, 1280 x 720 at 1.333x); a window of
// another shape has bars (2880 x 1800 shows 960 x 540 at 3x with bars of 90 rows above and below). Everybody who asks for the same aspect sees exactly the same world area, so
// no room has to cap it, and the wider shapes show more of the map, as 16:9 does against 4:3 (the map views are in docs/VIEW_AND_HUD.md). A CanvasLayout is any size, not only these.
//
// THE SHAPE TABLE: `kAspectTable` below is the one list of the shapes (name, words, canvas). The game reads it everywhere (the canvas, `--aspect`, the settings key, the Esc menu's picture row, the
// page's `ants_set_aspect`); the pages read the same rows from web/shapes.js, and tests/scripts/test_web_shapes.py fails when the two tables differ.
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

/// The shapes of picture that can be asked for (`--aspect`, the settings key `aspect`, the page's selector). Every canvas but the original's is at least 960 x 540 (the pages exist only for 640 x 480 and 960 x 540 and more).
enum class Aspect : uint8_t {
    Classic4x3,     // the original's 640 x 480 (`--aspect 4:3`; the aspect of a config that is made by hand, as the tests do: the default of a game is kPlatformDefaultAspect, 16:9)
    Wide16x9,       // 960 x 540
    Wide16x10,      // 960 x 600: the same width as 16:9 and 60 rows more, for a laptop or a 16:10 monitor
    Ultra21x9,      // 1260 x 540: the same height as 16:9 and 300 columns more, for an ultrawide monitor
    Wide3x2,        // 960 x 640: the same width as 16:9 and 100 rows more, for tablets and Surface-style laptops
    Wide20x9,       // 1200 x 540: the same height as 16:9 and 240 columns more, for a phone held sideways
    Super32x9       // 1920 x 540: the same height as 16:9 and twice its width, for a super-ultrawide monitor
};

/// One row of the shape table: the shape, its text (`--aspect`, the settings key, the page's `?aspect=` and the key `ants.aspect.v2`), the words that a menu or a selector shows, and its canvas in pixels.
struct AspectRow {
    Aspect aspect;
    const char* name;      // "4:3", "3:2", "16:10", "16:9", "20:9", "21:9", "32:9"
    const char* label;     // the words of the selectors and of the Esc menu: "Classic 4:3" for the original's picture, else the name
    int32_t width;
    int32_t height;
};

/// THE TABLE of the shapes, from the narrowest to the widest (the order of the page's selector and the place that `ants_set_aspect` takes). A canvas is 960 wide at least and 540 high at least, except the original's own.
/// A new shape is a row here, a value of Aspect, and a row of web/shapes.js (tests/scripts/test_web_shapes.py keeps the two tables equal).
inline constexpr std::array<AspectRow, 7> kAspectTable = {{
    {Aspect::Classic4x3, "4:3", "Classic 4:3", 640, 480},
    {Aspect::Wide3x2, "3:2", "3:2", 960, 640},
    {Aspect::Wide16x10, "16:10", "16:10", 960, 600},
    {Aspect::Wide16x9, "16:9", "16:9", 960, 540},
    {Aspect::Wide20x9, "20:9", "20:9", 1200, 540},
    {Aspect::Ultra21x9, "21:9", "21:9", 1260, 540},
    {Aspect::Super32x9, "32:9", "32:9", 1920, 540},
}};

namespace shape_table_detail {
/// The shape's own row (the original's, for a value that no row has)
constexpr const AspectRow& row_of(Aspect aspect) noexcept {
    for (const AspectRow& shape : kAspectTable) {
        if (shape.aspect == aspect) return shape;
    }
    return kAspectTable[0];
}
constexpr std::array<Aspect, kAspectTable.size()> aspects_of_table() noexcept {
    std::array<Aspect, kAspectTable.size()> out{};
    for (size_t i = 0; i < kAspectTable.size(); ++i) out[i] = kAspectTable[i].aspect;
    return out;
}
/// Every value of Aspect has exactly one row, and the enum counts from 0 without a gap (so a value that is added and has no row is caught by whatever uses it first)
constexpr bool table_is_whole() noexcept {
    for (size_t i = 0; i < kAspectTable.size(); ++i) {
        if (static_cast<size_t>(kAspectTable[i].aspect) >= kAspectTable.size()) return false;
        for (size_t j = i + 1; j < kAspectTable.size(); ++j) {
            if (kAspectTable[i].aspect == kAspectTable[j].aspect) return false;
        }
        const bool classic = kAspectTable[i].width == 640 && kAspectTable[i].height == 480;
        if (!classic && (kAspectTable[i].width < 960 || kAspectTable[i].height < 540)) return false;      // (the pages exist for 640 x 480 and for 960 x 540 and more: CanvasLayout::page)
    }
    return true;
}
static_assert(table_is_whole(), "kAspectTable: one row for every Aspect, none twice, every canvas 640 x 480 or at least 960 x 540");
}  // namespace shape_table_detail

/// Every shape, from the narrowest to the widest (the order of the page's selector): the table's order
inline constexpr std::array<Aspect, kAspectTable.size()> kAllAspects = shape_table_detail::aspects_of_table();

/// The aspect of a game that is started from the command line when neither `--aspect` nor the settings say: 16:9 on a desktop (requested 2026-10-02: the default should be
/// 16x9) and in the web build, where the page always passes the shape it shows as `--aspect` (the address, else the player's stored pick, else on a first visit the shape nearest the player's
/// screen, web/shapes.js `firstVisit`), so this is only the fallback of a game that is started without one.
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

constexpr int32_t canvas_width_of(Aspect aspect) noexcept { return shape_table_detail::row_of(aspect).width; }
constexpr int32_t canvas_height_of(Aspect aspect) noexcept { return shape_table_detail::row_of(aspect).height; }
constexpr const char* aspect_name(Aspect aspect) noexcept { return shape_table_detail::row_of(aspect).name; }
/// The words that a selector and the Esc menu show for a shape ("Classic 4:3", "16:10", ...)
constexpr const char* aspect_label(Aspect aspect) noexcept { return shape_table_detail::row_of(aspect).label; }

/// The place of a shape in the table, from 0 (the narrowest), -1 for a value that no row has: the number that the page's `ants_set_aspect` takes (a number, because the page's exports take only numbers)
constexpr int aspect_place(Aspect aspect) noexcept {
    for (size_t i = 0; i < kAspectTable.size(); ++i) {
        if (kAspectTable[i].aspect == aspect) return static_cast<int>(i);
    }
    return -1;
}
/// The shape at a place of the table; false (and `out` untouched) for a number outside it
constexpr bool aspect_at_place(int place, Aspect& out) noexcept {
    if (place < 0 || static_cast<size_t>(place) >= kAspectTable.size()) return false;
    out = kAspectTable[static_cast<size_t>(place)].aspect;
    return true;
}

/// The names of every shape in a sentence, in table order: "4:3, 3:2, 16:10, 16:9, 20:9, 21:9 and 32:9" (`last` = " and ") or "... or 32:9" (" or "), for the messages that refuse a text
inline std::string aspect_names(const char* last) {
    std::string out;
    for (size_t i = 0; i < kAspectTable.size(); ++i) {
        if (i > 0) out += (i + 1 == kAspectTable.size()) ? last : ", ";
        out += kAspectTable[i].name;
    }
    return out;
}

/// The text of `--aspect`, of the settings key and of the page's `?aspect=`: the name of a row of kAspectTable and nothing else. False with a message that says so for anything else.
inline bool parse_aspect(const std::string& text, Aspect& out, std::string& why) {
    for (const AspectRow& shape : kAspectTable) {
        if (text == shape.name) {
            out = shape.aspect;
            return true;
        }
    }
    why = "\"" + text + "\": only " + aspect_names(" and ");
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
