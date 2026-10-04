// Canvas layout (milestone M2 of the widescreen work): the picture that the window shows and how it sits in the window (include/ants_app/canvas_layout.hpp), the option that asks for a
// 16:9 picture (`--aspect`, the settings key `aspect`), the window that opens for it, fullscreen (Alt+Enter) and the screenshot of a canvas that is not 4:3.
//   * the model: the game draws into SDL's logical canvas, 640 x 480 (the original's, the default) or 960 x 540 (16:9), which SDL scales into the window by the largest scale that
//     fits, centred, with bars: `CanvasLayout::fit` is that arithmetic (a table of window sizes: 1920 x 1080 is 2x exactly, 2560 x 1440 2.667x, 2880 x 1800 3x with bars above and
//     below ...), checked against SDL itself; the window that opens is the largest scale in steps of 0.5 of the canvas that fits the display's usable area (at least 1x), also for a
//     game that starts in fullscreen (the window that Alt+Enter gives back);
//   * the application: `--aspect 16:9` / `4:3` and the key `aspect` (refusals say "only 16:9 and 4:3 for now"), the classic picture in the wide canvas (the quick help's two columns pixel for pixel
//     what the 4:3 application draws, moved by (160, 30); every screen is composed for the whole canvas: test_wide_pages; the plate in the canvas's corner), the pointer over the bars (it is
//     the picture's nearest edge pixel, the map scrolls), Alt+Enter, the screenshot.
// Usage: test_canvas_layout. Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "ants_app/application.hpp"
#include "ants_app/canvas_layout.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/window_layout.hpp"
#include "ants_assets/asset_archive.hpp"
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
        std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

std::string str(const LayoutRect& r) {
    std::ostringstream o;
    o << "(" << r.x << ", " << r.y << ", " << r.w << " x " << r.h << ")";
    return o.str();
}

void check_rect(const LayoutRect& got, const LayoutRect& want, const std::string& what) {
    check(got == want, what + ": " + str(got) + " should be " + str(want));
}

class QuietStdout {
public:
    QuietStdout() : old_(std::cout.rdbuf(sink_.rdbuf())) {}
    ~QuietStdout() { std::cout.rdbuf(old_); }
    QuietStdout(const QuietStdout&) = delete;
    QuietStdout& operator=(const QuietStdout&) = delete;

private:
    std::ostringstream sink_;
    std::streambuf* old_;
};

/// A path of this run (never the player's own settings)
std::filesystem::path temp_path(const std::string& name) {
#if defined(_WIN32)
    const long pid = static_cast<long>(_getpid());
#else
    const long pid = static_cast<long>(getpid());
#endif
    return std::filesystem::temp_directory_path() / (name + "_" + std::to_string(pid));
}

void ensure_sdl() {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) SDL_Init(SDL_INIT_VIDEO);
}

// =====================================================================================================================================================
// The model
// =====================================================================================================================================================

struct FitCase {
    int32_t out_w, out_h;
    double scale;
    bool whole;
    LayoutRect viewport;
    const char* note;
};

void check_fit(const CanvasLayout& canvas, const FitCase& c) {
    const CanvasFit f = canvas.fit(c.out_w, c.out_h);
    const std::string at = std::to_string(canvas.width) + " x " + std::to_string(canvas.height) + " in " + std::to_string(c.out_w) + " x " + std::to_string(c.out_h) + " (" + c.note + "): ";
    check(std::fabs(f.scale - c.scale) < 0.0005, at + "the scale is " + std::to_string(c.scale) + ", not " + std::to_string(f.scale));
    check(f.whole_scale() == c.whole, at + (c.whole ? "every canvas pixel is a whole square" : "the scale is fractional"));
    check_rect(f.viewport, c.viewport, at + "the picture");
    check(f.bar_left() == c.viewport.x && f.bar_top() == c.viewport.y && f.bar_right() == c.out_w - c.viewport.right() && f.bar_bottom() == c.out_h - c.viewport.bottom(), at + "the bars are what is left");
}

void test_fit_table() {
    group("fit", "a canvas is scaled into a window by the largest scale that fits, centred: the table of window sizes");
    const CanvasLayout wide = CanvasLayout::of(Aspect::Wide16x9);
    const CanvasLayout classic = CanvasLayout::of(Aspect::Classic4x3);
    check(wide.width == 960 && wide.height == 540 && classic.width == 640 && classic.height == 480, "the canvases of the two aspects are 960 x 540 and 640 x 480");
    check(CanvasLayout{} == classic && canvas_width_of(Aspect::Wide16x9) == 960 && canvas_height_of(Aspect::Wide16x9) == 540 && canvas_width_of(Aspect::Classic4x3) == 640 && canvas_height_of(Aspect::Classic4x3) == 480,
          "the default canvas is the original's");
    // 16:9 (960 x 540)
    const FitCase wide_cases[] = {
        {1920, 1080, 2.0, true, {0, 0, 1920, 1080}, "1080p: 2x exactly"},
        {3840, 2160, 4.0, true, {0, 0, 3840, 2160}, "4K: 4x"},
        {2560, 1440, 2.6667, false, {0, 0, 2560, 1440}, "1440p: fractional"},
        {1280, 720, 1.3333, false, {0, 0, 1280, 720}, "720p: fractional"},
        {2880, 1800, 3.0, true, {0, 90, 2880, 1620}, "a 16:10 Retina screen: 3x with bars above and below"},
        {1366, 768, 1.4222, false, {0, 0, 1365, 768}, "a 1366 x 768 laptop: one column of bar"},
        {1440, 900, 1.5, false, {0, 45, 1440, 810}, "16:10: bars above and below"},
        {1920, 1200, 2.0, true, {0, 60, 1920, 1080}, "16:10 at 1200 rows: 2x with bars of 60 rows"},
        {2560, 1080, 2.0, true, {320, 0, 1920, 1080}, "21:9: 2x with bars of 320 columns at the sides"},
        {960, 540, 1.0, true, {0, 0, 960, 540}, "the canvas itself"},
        {1280, 960, 1.3333, false, {0, 120, 1280, 720}, "a 4:3 window: bars above and below"},
        {1001, 777, 1.0427, false, {0, 107, 1001, 563}, "an odd window: the picture's height rounds down (563.06)"},
        {854, 480, 0.8889, false, {0, 0, 853, 480}, "a window smaller than the canvas (a column of bar)"},
        {800, 600, 0.8333, false, {0, 75, 800, 450}, "800 x 600: smaller than the canvas, bars above and below"},
        {640, 480, 0.6667, false, {0, 60, 640, 360}, "640 x 480: the original's window size, half the canvas's height in bars"},
        {600, 400, 0.625, false, {0, 31, 600, 337}, "600 x 400: smaller than 640 x 480"},
    };
    for (const FitCase& c : wide_cases) check_fit(wide, c);
    // 4:3 (640 x 480): the original's picture, as it has always been scaled
    const FitCase classic_cases[] = {
        {640, 480, 1.0, true, {0, 0, 640, 480}, "the canvas itself"},
        {800, 600, 1.25, false, {0, 0, 800, 600}, "800 x 600"},
        {1280, 960, 2.0, true, {0, 0, 1280, 960}, "the default window: 2x"},
        {1920, 1080, 2.25, false, {240, 0, 1440, 1080}, "1080p: 2.25x with bars at the sides (uneven pixels)"},
        {2560, 1440, 3.0, true, {320, 0, 1920, 1440}, "1440p: 3x with bars of 320 columns"},
        {3840, 2160, 4.5, false, {480, 0, 2880, 2160}, "4K: 4.5x"},
        {600, 400, 0.8333, false, {33, 0, 533, 400}, "a window smaller than the canvas keeps the fit"},
        {1366, 768, 1.6, false, {171, 0, 1024, 768}, "1366 x 768: 1.6x with bars at the sides"},
        {1440, 900, 1.875, false, {120, 0, 1200, 900}, "1440 x 900"},
        {2880, 1800, 3.75, false, {240, 0, 2400, 1800}, "a 2880 x 1800 Retina screen"},
        {1920, 1200, 2.5, false, {160, 0, 1600, 1200}, "16:10: 2.5x"},
    };
    for (const FitCase& c : classic_cases) check_fit(classic, c);

    // an odd bar gives its extra pixel to the right / bottom
    const CanvasFit odd = wide.fit(1921, 1081);
    check(odd.viewport.x + odd.viewport.w + odd.bar_right() == 1921 && odd.viewport.y + odd.viewport.h + odd.bar_bottom() == 1081, "1921 x 1081: the picture and the bars are the window");
    check(odd.bar_left() <= odd.bar_right() && odd.bar_top() <= odd.bar_bottom() && odd.bar_right() - odd.bar_left() <= 1 && odd.bar_bottom() - odd.bar_top() <= 1, "... an odd bar's extra pixel is on the right and at the bottom");
    // nothing to show in a window of no size
    check(wide.fit(0, 100).viewport.w == 0 && wide.fit(100, 0).scale == 0.0 && CanvasLayout{0, 0}.fit(100, 100).viewport.w == 0, "a window or a canvas of no size has no picture");
    // other canvases fit as well (the shape of a monitor is a later option)
    check_fit(CanvasLayout{1280, 720}, FitCase{2560, 1440, 2.0, true, {0, 0, 2560, 1440}, "1280 x 720 canvas"});
    check_fit(CanvasLayout{854, 480}, FitCase{1708, 960, 2.0, true, {0, 0, 1708, 960}, "854 x 480 canvas"});
}

void test_fit_invariants() {
    group("fit-rules", "for every window size: the picture is in the window, centred, fills one dimension, is scaled by the smaller ratio, and is a whole scale exactly when the limiting side is a multiple");
    int checked = 0;
    bool in_window = true, centred = true, fills = true, ratio = true, whole_rule = true;
    for (const CanvasLayout& canvas : {CanvasLayout::of(Aspect::Wide16x9), CanvasLayout::of(Aspect::Classic4x3)}) {
        for (int32_t w = 320; w <= 4000; w += 97) {
            for (int32_t h = 200; h <= 2400; h += 83) {
                const CanvasFit f = canvas.fit(w, h);
                ++checked;
                in_window = in_window && f.viewport.x >= 0 && f.viewport.y >= 0 && f.viewport.right() <= w && f.viewport.bottom() <= h;
                centred = centred && std::abs(f.bar_left() - f.bar_right()) <= 1 && std::abs(f.bar_top() - f.bar_bottom()) <= 1;
                fills = fills && (f.viewport.w == w || f.viewport.h == h);
                const double r = std::min(static_cast<double>(w) / canvas.width, static_cast<double>(h) / canvas.height);
                ratio = ratio && std::fabs(f.scale - r) < 0.0002 * r + 0.0001;
                // whole exactly when the limiting side is a multiple of the canvas's side (ratio of the aspects within SDL's tolerance excepted: those windows are near the canvas's shape)
                const bool limiting_is_width = static_cast<double>(w) / canvas.width <= static_cast<double>(h) / canvas.height;
                const bool multiple = limiting_is_width ? (w % canvas.width == 0) : (h % canvas.height == 0);
                const bool near_shape = std::fabs(static_cast<float>(canvas.width) / static_cast<float>(canvas.height) - static_cast<float>(w) / static_cast<float>(h)) < 0.0001f;
                if (!near_shape) whole_rule = whole_rule && (f.whole_scale() == multiple);
            }
        }
    }
    check(checked > 2000, "two thousand window sizes were tried (" + std::to_string(checked) + ")");
    check(in_window, "the picture is inside the window");
    check(centred, "the bars of both sides differ by at most a pixel");
    check(fills, "the picture fills the window's width or its height");
    check(ratio, "the scale is the smaller of window / canvas in width and height");
    check(whole_rule, "the scale is a whole number exactly when the limiting side is a multiple of the canvas's");
}

/// SDL itself: a hidden window of the size with a software renderer and the logical size; the corners of the canvas in window pixels
void test_fit_against_sdl() {
    group("fit-sdl", "CanvasLayout::fit is SDL's own arithmetic: the corners of the canvas in a real window of the size are the model's");
    ensure_sdl();
    const QuietStdout quiet;
    int compared = 0;
    int differing = 0;
    for (const CanvasLayout& canvas : {CanvasLayout::of(Aspect::Wide16x9), CanvasLayout::of(Aspect::Classic4x3)}) {
        for (const std::pair<int32_t, int32_t>& size : {std::pair<int32_t, int32_t>{1920, 1080}, {3840, 2160}, {2560, 1440}, {1280, 720}, {2880, 1800}, {1366, 768}, {1440, 900}, {1920, 1200},
                                                         {2560, 1080}, {960, 540}, {1280, 960}, {854, 480}, {640, 480}, {800, 600}, {600, 400}, {1001, 777}}) {
            SDL_Window* win = SDL_CreateWindow("canvas-fit", 0, 0, size.first, size.second, SDL_WINDOW_HIDDEN);
            if (win == nullptr) continue;
            SDL_Renderer* sr = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
            if (sr != nullptr) {
                SDL_RenderSetLogicalSize(sr, canvas.width, canvas.height);
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                SDL_RenderLogicalToWindow(sr, 0.0f, 0.0f, &x0, &y0);
                SDL_RenderLogicalToWindow(sr, static_cast<float>(canvas.width), static_cast<float>(canvas.height), &x1, &y1);
                const CanvasFit f = canvas.fit(size.first, size.second);
                ++compared;
                const bool same = std::abs(x0 - f.viewport.x) <= 1 && std::abs(y0 - f.viewport.y) <= 1 && std::abs(x1 - f.viewport.right()) <= 1 && std::abs(y1 - f.viewport.bottom()) <= 1;
                if (!same) {
                    ++differing;
                    std::fprintf(stderr, "  canvas %d x %d in %d x %d: SDL has (%d, %d) - (%d, %d), the model %s\n", canvas.width, canvas.height, size.first, size.second, x0, y0, x1, y1, str(f.viewport).c_str());
                }
                SDL_DestroyRenderer(sr);
            }
            SDL_DestroyWindow(win);
        }
    }
    check(compared >= 30, "SDL laid out " + std::to_string(compared) + " windows");
    check(differing == 0, "the model agrees with SDL's own layout in every window (within a pixel of rounding)");
}

void test_centred_picture() {
    group("centred", "the classic picture centred in a canvas");
    const CanvasLayout wide = CanvasLayout::of(Aspect::Wide16x9);
    check_rect(wide.centred(640, 480), LayoutRect{160, 30, 640, 480}, "640 x 480 in 960 x 540");
    check_rect(CanvasLayout::of(Aspect::Classic4x3).centred(640, 480), LayoutRect{0, 0, 640, 480}, "640 x 480 in 640 x 480");
    check_rect(wide.centred(960, 540), LayoutRect{0, 0, 960, 540}, "960 x 540 in 960 x 540 (a wide picture fills its canvas)");
    check_rect((CanvasLayout{1280, 720}).centred(640, 480), LayoutRect{320, 120, 640, 480}, "640 x 480 in 1280 x 720");
    check_rect(wide.rect(), LayoutRect{0, 0, 960, 540}, "the canvas's own rectangle");
}

// =====================================================================================================================================================
// The window that opens
// =====================================================================================================================================================

void test_window_sizes() {
    group("window", "the window of a canvas: the largest scale in steps of 0.5 that fits the display's usable area (at least 1x), centred; the grid's cells of the canvas's shape");
    // (rewritten with the review fixes of M3: the first window was the largest WHOLE multiple of the canvas, which left a typical laptop at 1x, 960 x 540 points on a 1800 x 1130 area;
    // the owner decided: steps of 0.5, which on a Retina display (two pixels to a point) is 3x in pixels at 1.5x in points, crisp)
    auto open = [](int32_t area_w, int32_t area_h, int32_t top = 0, int32_t canvas_w = 960, int32_t canvas_h = 540) {
        return default_canvas_window(WindowRect{0, 0, area_w, area_h}, canvas_w, canvas_h, top, 0, 0, 0);
    };
    // the table of display areas (usable bounds in points), with and without a 28 point title bar: the same scale
    struct Row { int32_t w, h; int32_t scale_halves; const char* what; };
    const Row table[] = {{1800, 1130, 3, "1800 x 1130 (a MacBook Pro 14 in its default mode): width limited, 1.875 -> 1.5x"},
                         {1440, 875, 3, "1440 x 875 (a MacBook Air 13): 1.5x"},
                         {1920, 1040, 3, "1920 x 1040 (a 1080p monitor with a task bar): height limited, 1.92 -> 1.5x"},
                         {2560, 1400, 5, "2560 x 1400 (a 1440p monitor): 2.5x"},
                         {3840, 2100, 7, "3840 x 2100 (a 4K monitor): height limited, 3.9 -> 3.5x"},
                         {1366, 728, 2, "1366 x 728 (a small laptop): 1x (width 1.42, height 1.35)"}};
    for (const Row& r : table) {
        for (const int32_t top : {0, 28}) {
            const WindowRect got = open(r.w, r.h, top);
            const int32_t want_w = 480 * r.scale_halves, want_h = 270 * r.scale_halves;
            check(got.w == want_w && got.h == want_h, std::string(r.what) + (top ? " (28 pt title bar)" : "") + ": " + std::to_string(got.w) + " x " + std::to_string(got.h) + ", wanted " + std::to_string(want_w) + " x " + std::to_string(want_h));
            check(got.x >= 0 && got.y >= top && got.x + got.w <= r.w && got.y + got.h <= r.h, std::string("... and it fits the area (") + std::to_string(r.w) + " x " + std::to_string(r.h) + ")");
        }
    }
    // the width limits (the review's survivor: the width's share ignored or counted one too many) and the height limits, one pixel either side of a step
    check(open(1920, 4000).w == 1920 && open(1919, 4000).w == 1440 && open(1440, 4000).w == 1440 && open(1439, 4000).w == 960, "the width alone decides: 1920 -> 2x, 1919 -> 1.5x, 1440 -> 1.5x, 1439 -> 1x");
    check(open(4000, 1080).h == 1080 && open(4000, 1079).h == 810 && open(4000, 810).h == 810 && open(4000, 809).h == 540, "the height alone decides: 1080 -> 2x, 1079 -> 1.5x, 810 -> 1.5x, 809 -> 1x");
    check(open(1920, 1080).w == 1920 && open(1920, 1080).h == 1080 && open(3840, 2160).w == 3840 && open(3840, 2160).h == 2160, "1920 x 1080 and 3840 x 2160 areas: 2x and 4x exactly");
    check(open(2400, 1350).w == 2400 && open(2400, 1349).w == 1920 && open(2399, 1350).w == 1920, "2.5x needs 2400 x 1350: one point less in either direction gives 2x");
    check(open(1920, 1050).w == 1440 && open(1920, 1050).x == 240 && open(1920, 1050).y == 120, "a 1920 x 1050 area: 1.5x, 1440 x 810, centred at (240, 120)");
    check(open(800, 450).w == 960 && open(800, 450).h == 540, "an area smaller than the canvas: still 1x");
    check(open(-5, -5).w == 960 && open(0, 0).h == 540, "an area of no size: 1x");
    // every size is whole canvas pixels in halves: a multiple of 480 x 270 (an even number of half steps is a whole scale)
    {
        bool shape = true;
        for (int32_t w = 900; w <= 4000; w += 37) {
            for (int32_t h = 500; h <= 2200; h += 41) {
                const WindowRect r = open(w, h);
                shape = shape && r.w % 480 == 0 && r.h % 270 == 0 && r.w / 480 == r.h / 270 && r.w >= 960;
            }
        }
        check(shape, "every window of a few thousand areas is the canvas at a multiple of 0.5 (480 x 270 steps), 16:9, at least 1x");
    }
    // the 4:3 canvas (640 x 480) in halves too, and a canvas of another size
    check(open(1800, 1130, 28, 640, 480).w == 1280 && open(1800, 1130, 28, 640, 480).h == 960, "(the 4:3 canvas in a 1800 x 1130 area with a title bar: 2x of 640 x 480, the height limits it)");
    check(open(2560, 1415, 28).w == 2400 && open(2560, 1415, 28).h == 1350, "a 2560 x 1415 area with a 28 px title bar: 2.5x (the height gives 2.57)");
    check(open(1512, 944, 28).w == 1440 && open(1512, 944, 28).h == 810, "a laptop's 1512 x 944 area: 1.5x");
    // centred in the area (the decoration on top)
    const WindowRect r = default_canvas_window(WindowRect{100, 50, 1920, 1100}, 960, 540, 28, 0, 0, 0);
    check(r.w == 1440 && r.h == 810, "a 1920 x 1100 area with a 28 px title bar: 1.5x (1072 rows are left: 1.99)");
    check(r.x == 100 + (1920 - 1440) / 2 && r.y == 50 + 28 + (1072 - 810) / 2, "... centred in what the decoration leaves: x 340, y 209");
    const WindowRect big = default_canvas_window(WindowRect{0, 0, 3000, 2000}, 640, 480, 0, 0, 0, 0);
    check(big.w == 2560 && big.h == 1920 && big.x == 220 && big.y == 40, "a 640 x 480 canvas in 3000 x 2000: 4x, centred at (220, 40)");
    // borders on all sides
    const WindowRect b = default_canvas_window(WindowRect{0, 0, 1000, 600}, 320, 180, 20, 10, 30, 10);
    check(b.w == 960 && b.h == 540 && b.x == 10 + (980 - 960) / 2 && b.y == 20 + (550 - 540) / 2, "the left, right and bottom decoration count too: 3x of 320 x 180 in 980 x 550");

    // the grid: cells of the canvas's shape (4:3 unless the aspect is told)
    const WindowRect area{0, 0, 1920, 1080};
    const WindowRect classic_cell = grid_cell_window(area, 2, 2, 0, 28, 0, 0, 0);
    const WindowRect classic_cell_explicit = grid_cell_window(area, 2, 2, 0, 28, 0, 0, 0, 4, 3);
    check(classic_cell == classic_cell_explicit && classic_cell.w == 682 && classic_cell.h == 511, "the original's cells are 4:3: a 960 x 540 cell with a 28 px title bar holds 682 x 511");
    const WindowRect wide_cell = grid_cell_window(area, 2, 2, 0, 28, 0, 0, 0, 960, 540);
    check(wide_cell.w == 910 && wide_cell.h == 511, "16:9 cells: the same cell holds 910 x 511 (the height decides)");
    const WindowRect wide_cell3 = grid_cell_window(area, 2, 2, 3, 28, 0, 0, 0, 16, 9);
    check(wide_cell3.x == 960 + (960 - 910) / 2 && wide_cell3.y == 540 + 28 + (512 - 511) / 2, "the cell number 3 of the 16:9 grid is the bottom right one, centred in the cell");
    const WindowRect wide_flat = grid_cell_window(WindowRect{0, 0, 2000, 400}, 2, 1, 0, 0, 0, 0, 0, 16, 9);
    check(wide_flat.w == 711 && wide_flat.h == 399, "a flat cell is as high as it is: 16:9 of 400 rows is 711 x 399");
}

// =====================================================================================================================================================
// --aspect and the settings key
// =====================================================================================================================================================

void test_aspect_parsing() {
    group("aspect", "the text of --aspect and of the key: 16:9 and 4:3, nothing else");
    Aspect a = Aspect::Classic4x3;
    std::string why;
    check(parse_aspect("16:9", a, why) && a == Aspect::Wide16x9 && why.empty(), "16:9");
    check(parse_aspect("4:3", a, why) && a == Aspect::Classic4x3, "4:3");
    check(std::string(aspect_name(Aspect::Wide16x9)) == "16:9" && std::string(aspect_name(Aspect::Classic4x3)) == "4:3", "the names are the values");
    for (const char* bad : {"21:9", "16:10", "", "abc", "16", "16:9 ", " 16:9", "16x9", "16/9", "0:0", "4:3:2", "4:3 ", "16:09", "1.78", "wide", "-16:9"}) {
        Aspect keep = Aspect::Wide16x9;
        std::string reason;
        const bool ok = parse_aspect(bad, keep, reason);
        check(!ok && keep == Aspect::Wide16x9 && reason.find("only 16:9 and 4:3 for now") != std::string::npos && reason.find(std::string("\"") + bad + "\"") != std::string::npos,
              std::string("\"") + bad + "\" is refused with the message and leaves the value alone");
    }
}

ApplicationConfig parse(std::initializer_list<const char*> args) {
    std::vector<std::string> store(args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& s : store) argv.push_back(s.data());
    return Application::parse_arguments(static_cast<int>(argv.size()), argv.data());
}

void test_aspect_option() {
    group("option", "--aspect on the command line");
    ApplicationConfig c = parse({"ants"});
    // (M3 rewrote this: the default of a game started from the command line is the platform's, 16:9 on a desktop, not the original's 4:3 any more: the owner's priority, 2026-10-02)
    check(c.aspect == kPlatformDefaultAspect && !c.aspect_given && c.startup_error.empty(), "without the option the aspect is the platform's default (16:9 on a desktop) and not given");
    c = parse({"ants", "--aspect", "16:9"});
    check(c.aspect == Aspect::Wide16x9 && c.aspect_given && c.startup_error.empty(), "--aspect 16:9");
    c = parse({"ants", "--headless", "--aspect", "4:3", "--map", "x.lvl"});
    check(c.aspect == Aspect::Classic4x3 && c.aspect_given && c.startup_error.empty(), "--aspect 4:3 is given (it beats the settings' key) and is the classic canvas");
    c = parse({"ants", "--aspect", "21:9"});
    check(c.startup_error.find("--aspect") != std::string::npos && c.startup_error.find("21:9") != std::string::npos && c.startup_error.find("only 16:9 and 4:3 for now") != std::string::npos &&
              !c.aspect_given && c.aspect == kPlatformDefaultAspect,
          "--aspect 21:9 is refused: \"" + c.startup_error + "\"");
    c = parse({"ants", "--aspect"});
    check(c.startup_error.find("--aspect needs 16:9 or 4:3") != std::string::npos, "--aspect without a value is refused");
    c = parse({"ants", "--aspect", "16:10", "--aspect", "16:9"});
    check(c.startup_error.find("16:10") != std::string::npos && c.aspect == Aspect::Wide16x9 && c.aspect_given, "the first refusal stays the one that is reported");
    c = parse({"ants", "--window-size", "1280,720", "--aspect", "16:9", "--fullscreen"});
    check(c.aspect == Aspect::Wide16x9 && c.has_window_size && c.fullscreen && c.startup_error.empty(), "the other options are not disturbed");

    // a refused command line stops the game before anything opens
    {
        const QuietStdout quiet;
        Application app;
        ApplicationConfig bad = parse({"ants", "--headless", "--aspect", "21:9"});
        check(!app.init(bad), "init() refuses to start with a refused --aspect");
    }
}

void test_window_size_option() {
    group("window-size", "--window-size takes WxH or W,H (at least 320x240); anything else is refused with a message and the game does not start");
    for (const char* good : {"1280,720", "1280x720", "1280X720", "320,240", "320x240", "100000,100000", "960x540"}) {
        const ApplicationConfig c = parse({"ants", "--window-size", good});
        const std::string text = good;
        const size_t sep = text.find_first_of(",xX");
        check(c.startup_error.empty() && c.has_window_size && c.window_w == std::atoi(text.substr(0, sep).c_str()) && c.window_h == std::atoi(text.substr(sep + 1).c_str()), std::string("--window-size ") + good + " is taken: " + std::to_string(c.window_w) + " x " + std::to_string(c.window_h));
    }
    for (const char* bad : {"1280", "1280x", "x720", ",720", "1280,", "1280;720", "1280 720", "1280x720x2", "1280,720,1", "1280x,720", "a,b", "axb", "-1280,720", "1280,-720", "+1280,720", "1280.5x720", " 1280x720", "1280x720 ", "319,240", "320,239",
                            "319x480", "640x239", "0x0", "100001,720", "1280,100001", "1e3x720", "0x500", "99999999999x1", ""}) {
        const ApplicationConfig c = parse({"ants", "--window-size", bad});
        check(!c.has_window_size && c.startup_error.find("--window-size") != std::string::npos && c.startup_error.find(std::string("\"") + bad + "\"") != std::string::npos,
              std::string("--window-size \"") + bad + "\" is refused with a message: \"" + c.startup_error + "\"");
    }
    ApplicationConfig c = parse({"ants", "--window-size"});
    check(!c.has_window_size && c.startup_error.find("--window-size needs") != std::string::npos, "--window-size without a value is refused");
    c = parse({"ants", "--window-size", "10x10", "--window-size", "1280x720"});
    check(!c.has_window_size || c.window_w == 1280, "(a later good one still counts for the window)");
    check(c.startup_error.find("10x10") != std::string::npos, "the first refusal stays the one that is reported");
    c = parse({"ants", "--aspect", "21:9", "--window-size", "bad"});
    check(c.startup_error.find("--aspect") != std::string::npos, "an earlier refusal of another option stays the one that is reported");
    // the parser alone
    int32_t w = 5, h = 6;
    std::string why;
    check(parse_window_size("800x600", w, h, why) && w == 800 && h == 600 && why.empty(), "parse_window_size: 800x600");
    check(parse_window_size("800,600", w, h, why) && w == 800 && h == 600, "... 800,600");
    w = 5;
    h = 6;
    check(!parse_window_size("800x6", w, h, why) && w == 5 && h == 6 && why.find("at least 320x240") != std::string::npos, "a window that is too small is refused with the minimum, and leaves the values alone");
    check(!parse_window_size("800", w, h, why) && why.find("WIDTHxHEIGHT or WIDTH,HEIGHT") != std::string::npos && w == 5 && h == 6, "a text with no separator says what is expected");
    // a refused command line stops the game before anything opens
    {
        const QuietStdout quiet;
        Application app;
        ApplicationConfig bad = parse({"ants", "--headless", "--window-size", "1280:720"});
        check(!app.init(bad), "init() refuses to start with a refused --window-size");
    }
}

// =====================================================================================================================================================
// The application
// =====================================================================================================================================================

SDL_Window* find_window() {
    for (uint32_t i = 1; i < 256; ++i) {
        if (SDL_Window* w = SDL_GetWindowFromID(i)) return w;
    }
    return nullptr;
}

/// An application on the dummy video driver (headless: a hidden window) with the settings in a file of this run
struct AppFixture {
    AppFixture(const std::string& settings_text, ApplicationConfig cfg, bool match = true) : settings(temp_path("ants_canvas_settings").replace_extension(".ini")) {
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
        if (!settings_text.empty()) {
            std::ofstream out(settings);
            out << settings_text;
        }
        cfg.headless = true;
        cfg.start_in_map_select = !match;
        cfg.settings_path = settings.string();
        QuietStdout quiet;
        ok = app.init(cfg);
        if (ok && match) ok = app.start_game("Original-Ants/Maps/SMALL.LVL");
        if (ok && match) app.hud().update(app.sim().get_world_state(), 100);
        window = find_window();
    }
    ~AppFixture() {
        app.shutdown();
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
    }
    AppFixture(const AppFixture&) = delete;
    AppFixture& operator=(const AppFixture&) = delete;

    void window_size(int32_t& w, int32_t& h) const {
        int ww = 0, wh = 0;
        if (window != nullptr) SDL_GetWindowSize(window, &ww, &wh);
        w = ww;
        h = wh;
    }
    // a motion at the pixel (cx, cy) of the canvas (its middle), through the application's own event loop
    void motion_at_canvas(int cx, int cy) {
        int wx = 0, wy = 0;
        SDL_RenderLogicalToWindow(app.renderer().get_sdl_renderer(), static_cast<float>(cx) + 0.5f, static_cast<float>(cy) + 0.5f, &wx, &wy);
        SDL_Event e{};
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = SDL_GetWindowID(window);
        e.motion.x = wx;
        e.motion.y = wy;
        SDL_PushEvent(&e);
    }
    void deliver() { app.run_frame_with_delta(0.001f); }          // (a headless application stops after 10 frames: the tests deliver fewer)
    void scroll(int ticks) {
        for (int i = 0; i < ticks; ++i) app.handle_camera_panning(0.020f);
    }

    std::filesystem::path settings;
    Application app;
    SDL_Window* window{nullptr};
    bool ok{false};
};

ApplicationConfig config_of(Aspect aspect, bool given, int32_t w = 0, int32_t h = 0) {
    ApplicationConfig cfg;
    cfg.aspect = aspect;
    cfg.aspect_given = given;
    if (w > 0) {
        cfg.has_window_size = true;
        cfg.window_w = w;
        cfg.window_h = h;
    }
    return cfg;
}

void test_application_aspects() {
    group("app", "the application's canvas, picture and window follow the aspect");
    {   // the default: the original's 640 x 480
        AppFixture f("", ApplicationConfig{});
        check(f.ok, "the default application starts");
        if (f.ok) {
            int32_t ww = 0, wh = 0;
            f.window_size(ww, wh);
            int lw = 0, lh = 0;
            SDL_RenderGetLogicalSize(f.app.renderer().get_sdl_renderer(), &lw, &lh);
            check(f.app.aspect() == Aspect::Classic4x3 && f.app.canvas() == CanvasLayout{640, 480} && lw == 640 && lh == 480, "default: the aspect is 4:3 and SDL's logical size 640 x 480");
            check_rect(f.app.picture(), LayoutRect{0, 0, 640, 480}, "default: the picture is the whole canvas");
            check_rect(f.app.renderer().picture(), LayoutRect{0, 0, 640, 480}, "default: the renderer's picture too");
            check(ww == 1280 && wh == 960, "default: the window opens 1280 x 960, as it always did");
        }
    }
    {   // --aspect 4:3 is the same
        AppFixture f("", config_of(Aspect::Classic4x3, true));
        check(f.ok && f.app.aspect() == Aspect::Classic4x3 && f.app.canvas() == CanvasLayout{640, 480}, "--aspect 4:3 is the classic canvas");
    }
    {   // --aspect 16:9
        AppFixture f("", config_of(Aspect::Wide16x9, true, 1280, 720));
        check(f.ok, "--aspect 16:9 starts");
        if (f.ok) {
            int32_t ww = 0, wh = 0;
            f.window_size(ww, wh);
            int lw = 0, lh = 0;
            SDL_RenderGetLogicalSize(f.app.renderer().get_sdl_renderer(), &lw, &lh);
            check(f.app.aspect() == Aspect::Wide16x9 && f.app.canvas() == CanvasLayout{960, 540} && lw == 960 && lh == 540, "16:9: SDL's logical size is 960 x 540");
            // (M3 rewrote these three: in M2 the match screen was still the classic picture, centred at (160, 30); it is the wide frame now and fills the canvas; the original's pages are
            // recomposed for the whole canvas too: test_wide_pages)
            check(f.app.layout() == ScreenLayout::with_size(960, 540) && f.app.renderer().layout() == f.app.layout() && f.app.hud().layout() == f.app.layout(), "16:9 (M3): the match screen is the wide 960 x 540 picture");
            check_rect(f.app.picture(), LayoutRect{0, 0, 960, 540}, "16:9: the match is the whole canvas");
            check_rect(f.app.renderer().picture(), LayoutRect{0, 0, 960, 540}, "16:9: the renderer has it too");
            check(ww == 1280 && wh == 720, "16:9: --window-size wins");
        }
    }
    {   // the window that opens without --window-size: the largest scale in steps of 0.5 of the canvas that fits the display's usable area
        AppFixture f("", config_of(Aspect::Wide16x9, true));
        check(f.ok, "16:9 without a window size starts");
        if (f.ok) {
            int32_t ww = 0, wh = 0;
            f.window_size(ww, wh);
            SDL_Rect area{0, 0, 0, 0};
            const bool have_area = SDL_GetDisplayUsableBounds(0, &area) == 0 || SDL_GetDisplayBounds(0, &area) == 0;
            const WindowRect expected = default_canvas_window(WindowRect{area.x, area.y, area.w, area.h}, 960, 540, 0, 0, 0, 0);
            check(have_area && ww == expected.w && wh == expected.h, "16:9: the window is the largest multiple of 480 x 270 in the display's area (" + std::to_string(ww) + " x " + std::to_string(wh) + ")");
            check(ww % 480 == 0 && wh % 270 == 0 && ww / 480 == wh / 270 && ww >= 960, "... a step of 0.5, at least 1x");
        }
    }
    {   // the window is CREATED in the shape of the picture it will show: it used to be created 1280 x 960 (4:3) and made 16:9 before the first frame, so that a 4:3 window could flash on screen at
        // the start. The first size (what SDL_CreateWindow got) has the canvas's aspect, 16:9 by default and 4:3 with --aspect 4:3, and it is the size that the window has afterwards: no resize followed.
        for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
            AppFixture f("", config_of(aspect, true), false);
            check(f.ok, std::string("a ") + aspect_name(aspect) + " application starts (the window's first size)");
            if (!f.ok) continue;
            const WindowRect first = f.app.window_created_rect();
            int32_t ww = 0, wh = 0;
            f.window_size(ww, wh);
            check(first.w > 0 && first.h > 0 && first.w * canvas_height_of(aspect) == first.h * canvas_width_of(aspect) && first.w >= canvas_width_of(aspect),
                  std::string(aspect_name(aspect)) + ": the window is created in the shape of the picture, " + std::to_string(first.w) + " x " + std::to_string(first.h) + " (at least 1x of the canvas)");
            check(ww == first.w && wh == first.h, std::string(aspect_name(aspect)) + ": it is the size that the window has now, nothing resized it");
            if (aspect == Aspect::Classic4x3) check(first.w == 1280 && first.h == 960, "4:3: the original's picture at the 2x integer scale, as it always was");
        }
        {   // --window-size is the size that is asked for, from the first moment
            AppFixture f("", config_of(Aspect::Wide16x9, true, 1280, 720), false);
            check(f.ok && f.app.window_created_rect().w == 1280 && f.app.window_created_rect().h == 720, "--window-size: created at the size that was asked for");
        }
        {   // fullscreen is as it always was: created at the config's size, sized by apply_window_layout once the window exists
            ApplicationConfig full = config_of(Aspect::Wide16x9, true);
            full.fullscreen = true;
            AppFixture f("", full, false);
            check(f.ok && f.app.window_created_rect().w == 1280 && f.app.window_created_rect().h == 960, "--fullscreen: created at the config's size, as it always was");
            ApplicationConfig grid_full = config_of(Aspect::Wide16x9, true);
            grid_full.grid_cols = 2;
            grid_full.grid_rows = 2;
            grid_full.fullscreen = true;
            AppFixture gf("", grid_full, false);
            check(gf.ok && gf.app.window_created_rect().w == 1280 && gf.app.window_created_rect().h == 960, "--grid with --fullscreen: created at the config's size too (the grid has no window in a fullscreen game)");
        }
        {   // the cells of the start scripts' grid (--grid 2x2 --cell N, no --aspect: the default 16:9) are CREATED at their cell's rectangle: the largest one of the picture's shape that fits the cell,
            // at its place. The four windows of start_game.sh used to be created at the config's 1280 x 960 (4:3) and cut to the cell once they existed, so that a 4:3 window was there at the
            // start (and stayed where a window system does not apply the cut at once). The title bar of a window that does not exist yet is assumed to be 28 rows, as apply_window_layout does where
            // it cannot be told (the dummy video driver of this test: the same rectangle before and after); the real borders only trim it afterwards, the shape stays.
            for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
                for (int32_t cell = 0; cell < 4; ++cell) {
                    const std::string what = std::string(aspect_name(aspect)) + ", cell " + std::to_string(cell);
                    ApplicationConfig grid = config_of(aspect, true);
                    grid.grid_cols = 2;
                    grid.grid_rows = 2;
                    grid.grid_cell = cell;
                    AppFixture g("", grid, false);
                    check(g.ok, what + ": a window in the grid starts");
                    if (!g.ok) continue;
                    SDL_Rect area{0, 0, 0, 0};                                                           // (asked while the application is up: that is when SDL's video is)
                    const bool have_area = SDL_GetDisplayUsableBounds(0, &area) == 0 || SDL_GetDisplayBounds(0, &area) == 0;
                    check(have_area && area.w > 0 && area.h > 0, what + ": a display to lay the grid on");
                    const WindowRect first = g.app.window_created_rect();
                    const WindowRect expected = grid_cell_window(WindowRect{area.x, area.y, area.w, area.h}, 2, 2, cell, 28, 0, 0, 0, canvas_width_of(aspect), canvas_height_of(aspect));
                    check(first.w == expected.w && first.h == expected.h, what + ": created at the size of its cell's rectangle, " + std::to_string(first.w) + " x " + std::to_string(first.h));
                    check(first.x == expected.x && first.y == expected.y, what + ": created at the place of its cell, not centred (" + std::to_string(first.x) + ", " + std::to_string(first.y) + ")");
                    check(first.w >= 320 && std::abs(first.h * canvas_width_of(aspect) - first.w * canvas_height_of(aspect)) <= canvas_width_of(aspect), what + ": created in the shape of the picture, never the config's 4:3 (1280 x 960)");
                    int32_t ww = 0, wh = 0;
                    g.window_size(ww, wh);
                    check(ww == first.w && wh == first.h, what + ": it is the size the window has now: nothing had to cut it");
                }
            }
            ApplicationConfig sized = config_of(Aspect::Wide16x9, true, 1280, 720);                      // the cell wins over --window-size, here as in apply_window_layout
            sized.grid_cols = 2;
            sized.grid_rows = 2;
            sized.grid_cell = 3;
            AppFixture gs("", sized, false);
            SDL_Rect area3{0, 0, 0, 0};
            const bool have_area3 = gs.ok && (SDL_GetDisplayUsableBounds(0, &area3) == 0 || SDL_GetDisplayBounds(0, &area3) == 0);
            const WindowRect cell3 = grid_cell_window(WindowRect{area3.x, area3.y, area3.w, area3.h}, 2, 2, 3, 28, 0, 0, 0, 960, 540);
            check(have_area3 && gs.app.window_created_rect() == cell3, "--grid with --window-size: the cell decides, from the first moment");
        }
    }
    {   // the cells of the start scripts' grid are of the canvas's shape: 16:9 for --aspect 16:9, 4:3 as ever
        for (const Aspect aspect : {Aspect::Classic4x3, Aspect::Wide16x9}) {
            ApplicationConfig cfg = config_of(aspect, true);
            cfg.grid_cols = 2;
            cfg.grid_rows = 2;
            cfg.grid_cell = 0;
            AppFixture f("", cfg);
            check(f.ok, std::string("a ") + aspect_name(aspect) + " application in a grid starts");
            if (f.ok) {
                int32_t ww = 0, wh = 0;
                f.window_size(ww, wh);
                SDL_Rect area{0, 0, 0, 0};
                const bool have_area = SDL_GetDisplayUsableBounds(0, &area) == 0 || SDL_GetDisplayBounds(0, &area) == 0;
                int top = 0, left = 0, bottom = 0, right = 0;
                if (SDL_GetWindowBordersSize(f.window, &top, &left, &bottom, &right) != 0) {
                    top = 28;
                    left = bottom = right = 0;
                }
                const WindowRect expected = grid_cell_window(WindowRect{area.x, area.y, area.w, area.h}, 2, 2, 0, top, left, bottom, right, canvas_width_of(aspect), canvas_height_of(aspect));
                check(have_area && ww == expected.w && wh == expected.h, std::string(aspect_name(aspect)) + ": the grid's window is the cell's largest " + aspect_name(aspect) + " rectangle (" + std::to_string(ww) + " x " + std::to_string(wh) + ")");
                if (aspect == Aspect::Wide16x9) check(std::abs(wh * 16 - ww * 9) <= 16 && ww >= 320, "16:9: the window is 16:9");
                else check(std::abs(wh * 4 - ww * 3) <= 4, "4:3: the window is 4:3");
            }
        }
    }
    {   // a game that starts in fullscreen (the canvas fills the monitor as far as it fits) has the canvas's default window for the way back (Alt+Enter), not the config's 1280 x 960 of the
        // original's shape (the headless window is not really fullscreen, so its size is the one that is stored)
        ApplicationConfig cfg = config_of(Aspect::Wide16x9, true);
        cfg.fullscreen = true;
        {
            AppFixture f("", cfg);
            check(f.ok && f.app.aspect() == Aspect::Wide16x9 && f.app.canvas() == CanvasLayout{960, 540}, "16:9 with --fullscreen: the same canvas");
            if (f.ok) {
                int32_t ww = 0, wh = 0;
                f.window_size(ww, wh);
                SDL_Rect area{0, 0, 0, 0};
                const bool have_area = SDL_GetDisplayUsableBounds(0, &area) == 0 || SDL_GetDisplayBounds(0, &area) == 0;
                const WindowRect expected = default_canvas_window(WindowRect{area.x, area.y, area.w, area.h}, 960, 540, 0, 0, 0, 0);
                check(have_area && ww == expected.w && wh == expected.h && ww != 1280 && std::abs(wh * 16 - ww * 9) <= 16,
                      "16:9 with --fullscreen: the window to go back to is the canvas's default (" + std::to_string(ww) + " x " + std::to_string(wh) + "), 16:9, not 1280 x 960");
            }
        }
        {   // --window-size still wins
            ApplicationConfig sized = config_of(Aspect::Wide16x9, true, 1024, 576);
            sized.fullscreen = true;
            AppFixture g("", sized);
            int32_t gw = 0, gh = 0;
            if (g.ok) g.window_size(gw, gh);
            check(g.ok && gw == 1024 && gh == 576, "16:9 with --fullscreen and --window-size: the size that was asked for (" + std::to_string(gw) + " x " + std::to_string(gh) + ")");
        }
        {   // the original's own aspect keeps the config's size
            ApplicationConfig classic = config_of(Aspect::Classic4x3, true);
            classic.fullscreen = true;
            AppFixture c("", classic);
            int32_t cw = 0, ch = 0;
            if (c.ok) c.window_size(cw, ch);
            check(c.ok && cw == 1280 && ch == 960, "4:3 with --fullscreen: the config's 1280 x 960, as it always was (" + std::to_string(cw) + " x " + std::to_string(ch) + ")");
        }
    }
}

void test_settings_key() {
    group("settings", "the key aspect of the settings file: the command line wins, a bad value is ignored");
    auto aspect_of = [](const std::string& settings_text, ApplicationConfig cfg = ApplicationConfig{}) {
        AppFixture f(settings_text, cfg, false);
        return f.ok ? f.app.aspect() : Aspect::Classic4x3;
    };
    check(aspect_of("aspect=16:9\n") == Aspect::Wide16x9, "aspect=16:9 in the settings gives the 16:9 canvas");
    check(aspect_of("aspect=4:3\n") == Aspect::Classic4x3, "aspect=4:3 gives the classic one");
    check(aspect_of("Scroll Speed=20\n") == Aspect::Classic4x3, "no key: 4:3");
    check(aspect_of("aspect=21:9\n") == Aspect::Classic4x3, "aspect=21:9 is ignored (4:3), the game starts");
    check(aspect_of("aspect=\n") == Aspect::Classic4x3, "an empty value is ignored");
    check(aspect_of("aspect=wide\n") == Aspect::Classic4x3, "a word is ignored");
    check(aspect_of("aspect=16:9\n", config_of(Aspect::Classic4x3, true)) == Aspect::Classic4x3, "--aspect 4:3 beats aspect=16:9");
    check(aspect_of("aspect=4:3\n", config_of(Aspect::Wide16x9, true)) == Aspect::Wide16x9, "--aspect 16:9 beats aspect=4:3");
    check(aspect_of("aspect=16:9\n", config_of(Aspect::Classic4x3, false)) == Aspect::Wide16x9, "a config that was not given from the command line does not count");
    // the settings file is read for the other options too (the key shares the file)
    AppFixture f("aspect=16:9\nScroll Speed=33\n", ApplicationConfig{}, false);
    check(f.ok && f.app.aspect() == Aspect::Wide16x9 && f.app.hud().get_scroll_speed() == 33, "the key shares the file with the original's options");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// pixels
// ---------------------------------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> read_canvas(Application& app, int32_t x, int32_t y, int32_t w, int32_t h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
    SDL_Rect r{x, y, w, h};
    SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), &r, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4);
    return px;
}

uint32_t fixed_clock() { return 1000; }

/// Pixels of the rectangle (rx, ry, rw, rh) of a frame, with the rectangle `mask` blanked (the plate of the frame rate: its numbers are not the same in two frames)
std::vector<uint8_t> masked(std::vector<uint8_t> px, int32_t w, int32_t mask_x, int32_t mask_y, int32_t mask_w, int32_t mask_h) {
    const int32_t h = static_cast<int32_t>(px.size() / 4u) / w;
    for (int32_t y = std::max(mask_y, 0); y < std::min(mask_y + mask_h, h); ++y) {
        for (int32_t x = std::max(mask_x, 0); x < std::min(mask_x + mask_w, w); ++x) {
            uint8_t* p = &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u];
            p[0] = p[1] = p[2] = p[3] = 0;
        }
    }
    return px;
}

void test_centred_pixels() {
    group("pixels", "the 16:9 application draws the classic picture, centred, pixel for pixel; the bars stay black; the plate is in the canvas's corner");
    // two applications: the 4:3 one in a 640 x 480 window (scale 1) and the 16:9 one in a 960 x 540 window (scale 1)
    AppFixture classic("", config_of(Aspect::Classic4x3, true, 640, 480));
    AppFixture wide("", config_of(Aspect::Wide16x9, true, 960, 540));
    check(classic.ok && wide.ok, "both applications run a match");
    if (!classic.ok || !wide.ok) return;
    for (AppFixture* f : {&classic, &wide}) {
        f->app.renderer().pin_animation_clock(1500);                                  // the terrain's animations follow the wall clock otherwise
        f->app.hud().set_ticks_function(&fixed_clock);                                // (the chat input's caret blinks with the clock: a new HUD state starts it at this time)
        f->app.hud().init(0);
    }
    classic.app.render_frame();
    wide.app.render_frame();
    // the match screen (M3 rewrote this: in M2 the 16:9 match was the classic frame, centred in the canvas, with black bars around it). The wide frame fills the canvas; what the original draws
    // that is not stretched (the top bar before its cut, the left strip down to its cut, the bottom strip before its cut) is the same pixels at the same place relative to its anchor.
    const std::vector<uint8_t> a = read_canvas(classic.app, 0, 0, 640, 480);
    const std::vector<uint8_t> b = read_canvas(wide.app, 0, 0, 960, 540);
    auto region_equal = [&](int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t w, int32_t h) {
        for (int32_t y = 0; y < h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                const size_t ia = (static_cast<size_t>(ay + y) * 640u + static_cast<size_t>(ax + x)) * 4u;
                const size_t ib = (static_cast<size_t>(by + y) * 960u + static_cast<size_t>(bx + x)) * 4u;
                if (std::memcmp(&a[ia], &b[ib], 4) != 0) return false;
            }
        }
        return true;
    };
    check(region_equal(0, 0, 0, 0, 140, 22) && region_equal(0, 22, 0, 22, 17, 300) && region_equal(17, 461, 17, 521, 15, 19), "the match screen of the 16:9 canvas: the unstretched art (top bar columns 0 - 139, left strip rows 22 - 321, bottom strip columns 0 - 14) is the 4:3 application's pixels");
    check(!region_equal(500, 100, 500, 100, 100, 100), "... and the rest is not: the right panel is moved (the 4:3 frame's panel is at x = 480, the wide one's map view reaches it)");
    // a frame sets the picture itself, whatever the renderer was left with
    wide.app.renderer().set_picture(LayoutRect{160, 30, 640, 480});
    wide.app.render_frame();
    check(masked(read_canvas(wide.app, 0, 0, 960, 540), 960, 640, 520, 320, 20) == masked(b, 960, 640, 520, 320, 20), "a frame of the 16:9 application draws the whole canvas even when the renderer's picture was changed since the last one");
    // the match fills the canvas: nothing is a black bar (the frame is there at the corners and the sides)
    auto black = [&](int32_t x, int32_t y, int32_t w, int32_t h) {
        const std::vector<uint8_t> p = read_canvas(wide.app, x, y, w, h);
        for (size_t i = 0; i + 3 < p.size(); i += 4) {
            if (p[i] != 0 || p[i + 1] != 0 || p[i + 2] != 0) return false;
        }
        return true;
    };
    check(!black(0, 0, 4, 4) && !black(956, 0, 4, 4) && !black(0, 536, 4, 4) && !black(956, 400, 4, 4), "the match screen: the frame fills the canvas to its corners and sides (no bar)");
    // the plate (the version next to the frame rate) is in the canvas's bottom right corner: rows 527 .. 539, and not in the picture's own corner
    auto bar_pixels = [&](Application& app, int32_t x, int32_t y, int32_t w, int32_t h) {
        const std::vector<uint8_t> p = read_canvas(app, x, y, w, h);
        int n = 0;
        for (size_t i = 0; i + 3 < p.size(); i += 4) n += (p[i + 1] >= 185 && p[i] < 100 && p[i + 2] < 120) ? 1 : 0;
        return n;
    };
    check(bar_pixels(wide.app, 834, 527, 118, 13) > 20, "16:9: the sparkline is in the canvas's bottom right corner (rows 527 .. 539)");
    check(bar_pixels(wide.app, 514, 467, 118, 13) == 0, "16:9: and not where the 4:3 picture's corner is (the original's place of the plate)");
    check(bar_pixels(classic.app, 514, 467, 118, 13) > 20, "4:3: it is in the picture's corner, as it was");
    // the quick help at the start: the 16:9 page is the whole canvas (the wide frame, flat clay: tests/test_app/test_wide_pages.cpp), and its two columns are the 4:3 page's own pixels, moved by
    // (160, 30) (this used to compare the whole page, centred, with the 4:3 application's: the frame around the columns is the wide frame now). START! is left out: it is anchored to the
    // bottom right corner of the wide page (+320, +60), not moved with the columns.
    AppFixture classic_setup("", config_of(Aspect::Classic4x3, true, 640, 480), false);
    AppFixture wide_setup("", config_of(Aspect::Wide16x9, true, 960, 540), false);
    check(classic_setup.ok && wide_setup.ok, "both applications show the quick help");
    if (classic_setup.ok && wide_setup.ok) {
        classic_setup.app.finish_loading();
        wide_setup.app.finish_loading();
        check(classic_setup.app.state() == AppState::QuickHelp && wide_setup.app.state() == AppState::QuickHelp, "the quick help is up in both");
        for (AppFixture* f : {&classic_setup, &wide_setup}) f->app.renderer().pin_animation_clock(1500);
        classic_setup.app.render_frame();
        wide_setup.app.render_frame();
        // the two columns of the 4:3 page: qh1 257 x 461 at (10, 9) and qh2 362 x 463 at (267, 10); qh2's slot of the START! button (x 521 .. 628, y 437 .. 472) is left out, the button is elsewhere in the
        // wide page and the art under it shows
        const auto columns = [&](Application& app, int32_t dx, int32_t dy, int32_t x, int32_t y, int32_t w, int32_t h) { return read_canvas(app, x + dx, y + dy, w, h); };
        const bool left = columns(classic_setup.app, 0, 0, 10, 9, 257, 461) == columns(wide_setup.app, 160, 30, 10, 9, 257, 461);
        const bool right_top = columns(classic_setup.app, 0, 0, 267, 10, 362, 427) == columns(wide_setup.app, 160, 30, 267, 10, 362, 427);
        // (the 4:3 application draws its plate, the version and the frame rate, in the picture's bottom right corner: x 514 .. 631, y 467 .. 479. The rows from 467 on are compared only well to the left of it,
        // so that the width of the version's text, which changes with a release, cannot decide this check: with v0.2.0 two pixels of its first letter reached x 519 .. 520 of row 472)
        const bool right_left = columns(classic_setup.app, 0, 0, 267, 437, 254, 30) == columns(wide_setup.app, 160, 30, 267, 437, 254, 30) &&
                                columns(classic_setup.app, 0, 0, 267, 467, 180, 6) == columns(wide_setup.app, 160, 30, 267, 467, 180, 6);
        check(left && right_top && right_left, "the quick help of the 16:9 canvas: the two columns are the 4:3 application's pixels, centred (moved by (160, 30))");
    }
}

void test_pointer_over_bars() {
    group("pointer", "the pointer in a 16:9 canvas: the picture's own coordinates, and the bars around the picture count as its nearest edge pixel");
    // (M3 rewrote this group: in M2 the match was the 640 x 480 picture centred in the canvas, so a pointer over the canvas's margin was over a bar of the picture. The match is the whole canvas now;
    // the bars of a window are the ones of its shape: above and below in a window that is taller than 16:9 (16:10), left and right in one that is wider (21:9). The pages of the original
    // are centred with a margin; the pointer's clamp to the page is checked on the setup screen.)
    {
        AppFixture f("", config_of(Aspect::Wide16x9, true, 1920, 1200));       // 16:10: the canvas is 2x (1920 x 1080), bars of 60 rows above and below
        check(f.ok && f.window != nullptr, "the 16:9 application runs a match in a 16:10 window");
        if (!f.ok || f.window == nullptr) return;
        f.motion_at_canvas(100, 100);
        f.deliver();
        check(f.app.mouse_screen_x() == 100 && f.app.mouse_screen_y() == 100, "the canvas point (100, 100) is the picture's (100, 100): the match is the whole canvas");
        f.motion_at_canvas(0, 0);
        f.deliver();
        check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 0, "the picture's first pixel is (0, 0)");
        f.motion_at_canvas(959, 539);
        f.deliver();
        check(f.app.mouse_screen_x() == 959 && f.app.mouse_screen_y() == 539, "the picture's last pixel is (959, 539)");
        auto center = [&]() { f.app.renderer().camera().center_on(600, 600, f.app.sim().grid().width(), f.app.sim().grid().height()); };
        auto window_motion = [&](int wx, int wy) {
            SDL_Event e{};
            e.type = SDL_MOUSEMOTION;
            e.motion.windowID = SDL_GetWindowID(f.window);
            e.motion.x = wx;
            e.motion.y = wy;
            SDL_PushEvent(&e);
            f.deliver();
        };
        center();
        const int32_t y0 = f.app.renderer().camera().world_y;
        window_motion(960, 20);                                                // the bar above the picture, level with its middle
        check(f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 0 && !f.app.pointer_outside(), "over the top bar the pointer is on the picture's top edge, level with the pointer");
        f.scroll(12);
        check(f.app.renderer().camera().world_y < y0, "... and the map scrolls north");
        center();
        const int32_t y1 = f.app.renderer().camera().world_y;
        window_motion(960, 1190);                                              // the bar below
        check(f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 539, "over the bottom bar: the bottom edge");
        f.scroll(12);
        check(f.app.renderer().camera().world_y > y1, "... and the map scrolls south");
        // far beyond the window
        window_motion(-40000, 40000);
        check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 539, "far beyond the window's bottom left corner: the picture's bottom left pixel");
    }
    {
        AppFixture f("", config_of(Aspect::Wide16x9, true, 2560, 1080));       // 21:9: the canvas is 2x (1920 x 1080), bars of 320 columns left and right
        check(f.ok && f.window != nullptr, "the 16:9 application runs a match in a 21:9 window");
        if (!f.ok || f.window == nullptr) return;
        auto center = [&]() { f.app.renderer().camera().center_on(600, 600, f.app.sim().grid().width(), f.app.sim().grid().height()); };
        auto window_motion = [&](int wx, int wy) {
            SDL_Event e{};
            e.type = SDL_MOUSEMOTION;
            e.motion.windowID = SDL_GetWindowID(f.window);
            e.motion.x = wx;
            e.motion.y = wy;
            SDL_PushEvent(&e);
            f.deliver();
        };
        center();
        const int32_t x0 = f.app.renderer().camera().world_x;
        window_motion(40, 540);                                                // the left bar, level with the middle
        check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 270 && !f.app.pointer_outside(), "over the left bar the pointer is on the picture's left edge, level with the pointer");
        f.scroll(12);
        check(f.app.renderer().camera().world_x < x0, "... and the map scrolls west");
        center();
        const int32_t x1 = f.app.renderer().camera().world_x;
        window_motion(2520, 540);                                              // the right bar
        check(f.app.mouse_screen_x() == 959 && f.app.mouse_screen_y() == 270, "over the right bar: the right edge");
        f.scroll(12);
        check(f.app.renderer().camera().world_x > x1, "... and the map scrolls east");
    }
    {   // THE OWNER'S SCREENS (edge-pan: "if I go off the edge of the game, it no longer pans", on a 16:10 screen in fullscreen): a window of the shape of a 16:10 screen, 1440 x 900 (the picture
        // 1440 x 810, bars of 45 rows) and 1512 x 982 (a MacBook's, bars of 66 rows at a scale of 1.575): the pointer anywhere over a bar is on the picture's nearest edge pixel and is NOT gone, the
        // map scrolls up or down, a corner of the bars scrolls diagonally, and the picture's own first row and the bar's last row are the same pointer. A fullscreen window is such a window (the clamp does not
        // look at fullscreen): tests/test_app/test_app_integration.cpp 7.8f makes one. The windowed case is unchanged: a pointer that really left (SDL's LEAVE) is gone and scrolls nothing.
        for (const std::pair<int, int>& size : {std::pair<int, int>{1440, 900}, std::pair<int, int>{1512, 982}}) {
            AppFixture f("", config_of(Aspect::Wide16x9, true, size.first, size.second));
            check(f.ok && f.window != nullptr, "the 16:9 application runs a match in a " + std::to_string(size.first) + " x " + std::to_string(size.second) + " window");
            if (!f.ok || f.window == nullptr) return;
            int w = 0, h = 0;
            SDL_GetWindowSize(f.window, &w, &h);
            const CanvasFit fit = f.app.canvas().fit(w, h);
            check(w == size.first && h == size.second && fit.bar_top() > 0 && fit.bar_top() == fit.bar_bottom() && fit.bar_left() == 0, std::to_string(w) + " x " + std::to_string(h) + ": bars of " + std::to_string(fit.bar_top()) + " rows above and below, none at the sides");
            auto center = [&]() { f.app.renderer().camera().center_on(600, 600, f.app.sim().grid().width(), f.app.sim().grid().height()); };
            auto window_motion = [&](int wx, int wy) {
                SDL_Event e{};
                e.type = SDL_MOUSEMOTION;
                e.motion.windowID = SDL_GetWindowID(f.window);
                e.motion.x = wx;
                e.motion.y = wy;
                SDL_PushEvent(&e);
                f.deliver();
            };
            const auto& cam = f.app.renderer().camera();
            const std::string at = std::to_string(w) + " x " + std::to_string(h);
            center();
            const int32_t x0 = cam.world_x;
            const int32_t y0 = cam.world_y;
            window_motion(w / 2, fit.bar_top() / 2);                           // the middle of the top bar
            check(f.app.mouse_screen_y() == 0 && !f.app.pointer_outside() && std::abs(f.app.mouse_screen_x() - 480) <= 1, at + ": over the top bar the pointer is on the picture's top edge, level with the pointer");
            f.scroll(12);
            check(cam.world_y < y0 && cam.world_x == x0, at + ": ... and the map scrolls north, not sideways");
            window_motion(w / 2, fit.viewport.y - 1);                          // the bar's last row
            const int32_t bar_x = f.app.mouse_screen_x();
            window_motion(w / 2, fit.viewport.y);                              // the picture's own first row
            check(f.app.mouse_screen_y() == 0 && f.app.mouse_screen_x() == bar_x && !f.app.pointer_outside(), at + ": the bar's last row and the picture's first row are the same pointer");
            center();
            const int32_t y1 = cam.world_y;
            window_motion(w / 2, h - fit.bar_bottom() / 2);                    // the middle of the bottom bar
            check(f.app.mouse_screen_y() == 539 && !f.app.pointer_outside(), at + ": over the bottom bar the pointer is on the picture's bottom edge");
            f.scroll(12);
            check(cam.world_y > y1, at + ": ... and the map scrolls south");
            center();
            const int32_t cx = cam.world_x;
            const int32_t cy = cam.world_y;
            window_motion(0, 0);                                               // the window's top left corner: the corner of the bars
            check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 0 && !f.app.pointer_outside(), at + ": the corner of the bars is the picture's corner pixel");
            f.scroll(12);
            check(cam.world_x < cx && cam.world_y < cy, at + ": ... and the map scrolls diagonally, north west");
            center();
            const int32_t sx = cam.world_x;
            const int32_t sy = cam.world_y;
            window_motion(w - 1, h - 1);                                       // the bottom right corner
            check(f.app.mouse_screen_x() == 959 && f.app.mouse_screen_y() == 539, at + ": the bottom right corner of the window is the picture's last pixel");
            f.scroll(12);
            check(cam.world_x > sx && cam.world_y > sy, at + ": ... south east");
            // the windowed case is unchanged: the pointer that left is gone, and the view does not move
            center();
            const int32_t gx = cam.world_x;
            const int32_t gy = cam.world_y;
            window_motion(w / 2, 5);                                           // in the top bar again: scrolling
            SDL_Event leave{};
            leave.type = SDL_WINDOWEVENT;
            leave.window.windowID = SDL_GetWindowID(f.window);
            leave.window.event = SDL_WINDOWEVENT_LEAVE;
            SDL_PushEvent(&leave);
            f.deliver();
            check(f.app.pointer_outside(), at + ": a pointer that left the window (SDL's LEAVE) is gone");
            f.scroll(12);
            check(cam.world_x == gx && cam.world_y == gy, at + ": ... and nothing scrolls");
            window_motion(w / 2, h / 2);
            check(!f.app.pointer_outside(), at + ": it is back with its next motion");
        }
        {   // the original's 4:3 picture on the same screen: bars of 120 columns at the sides (1440 x 900), the pointer over them is on the picture's side edge
            AppFixture f("", config_of(Aspect::Classic4x3, true, 1440, 900));
            check(f.ok && f.window != nullptr, "the classic application runs a match in a 1440 x 900 window");
            if (!f.ok || f.window == nullptr) return;
            int w = 0, h = 0;
            SDL_GetWindowSize(f.window, &w, &h);
            const CanvasFit fit = f.app.canvas().fit(w, h);
            check(w == 1440 && h == 900 && fit.bar_left() == 120 && fit.bar_right() == 120 && fit.bar_top() == 0, "1440 x 900: the 4:3 picture is 1200 x 900 with bars of 120 columns at the sides");
            const auto& cam = f.app.renderer().camera();
            auto center = [&]() { f.app.renderer().camera().center_on(600, 600, f.app.sim().grid().width(), f.app.sim().grid().height()); };
            auto window_motion = [&](int wx, int wy) {
                SDL_Event e{};
                e.type = SDL_MOUSEMOTION;
                e.motion.windowID = SDL_GetWindowID(f.window);
                e.motion.x = wx;
                e.motion.y = wy;
                SDL_PushEvent(&e);
                f.deliver();
            };
            center();
            const int32_t x0 = cam.world_x;
            const int32_t y0 = cam.world_y;
            window_motion(40, 450);
            check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 240 && !f.app.pointer_outside(), "the left bar: the picture's left edge, level with the pointer");
            f.scroll(12);
            check(cam.world_x < x0 && cam.world_y == y0, "... the map scrolls west, not up or down");
            center();
            const int32_t x1 = cam.world_x;
            window_motion(1400, 450);
            check(f.app.mouse_screen_x() == 639 && f.app.mouse_screen_y() == 240, "the right bar: the right edge");
            f.scroll(12);
            check(cam.world_x > x1, "... east");
            center();
            const int32_t x2 = cam.world_x;
            const int32_t y2 = cam.world_y;
            window_motion(10, 1);                                              // a corner of the window: a bar on the left, the picture's top row
            check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 0, "the window's top left corner is the picture's corner");
            f.scroll(12);
            check(cam.world_x < x2 && cam.world_y < y2, "... the map scrolls diagonally");
        }
    }
    {   // a screen outside a match in the 16:9 canvas (the quick help): the whole canvas, so a canvas point is the picture's own (this block used to pin the clamp of a pointer over the margin
        // of a centred page to the page's nearest edge pixel: there is no page and no margin any more)
        AppFixture f("", config_of(Aspect::Wide16x9, true, 1920, 1080), false);
        check(f.ok && f.window != nullptr, "the 16:9 application shows the quick help");
        if (!f.ok || f.window == nullptr) return;
        f.app.finish_loading();
        f.deliver();
        check(f.app.state() == AppState::QuickHelp && f.app.picture() == (LayoutRect{0, 0, 960, 540}), "the quick help is the whole canvas");
        f.motion_at_canvas(260, 130);
        f.deliver();
        check(f.app.mouse_screen_x() == 260 && f.app.mouse_screen_y() == 130, "the canvas point (260, 130) is the picture's (260, 130)");
        f.motion_at_canvas(0, 0);
        f.deliver();
        check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 0, "the picture's first pixel is (0, 0)");
        f.motion_at_canvas(959, 539);
        f.deliver();
        check(f.app.mouse_screen_x() == 959 && f.app.mouse_screen_y() == 539, "the picture's last pixel is (959, 539)");
        f.motion_at_canvas(40, 270);
        f.deliver();
        check(f.app.mouse_screen_x() == 40 && f.app.mouse_screen_y() == 270 && !f.app.pointer_outside(), "left of the page's columns the pointer is where it is (on the wide frame's clay, not a margin)");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Alt+Enter
// ---------------------------------------------------------------------------------------------------------------------------------------------------

void test_alt_enter() {
    group("fullscreen", "Alt+Enter toggles fullscreen (native builds); no screen sees the key; a held key toggles once");
    const std::filesystem::path settings = temp_path("ants_canvas_fs_settings").replace_extension(".ini");
    auto key_event = [](SDL_Window* w, Uint32 type, SDL_Keycode sym, Uint16 mod, bool repeat = false) {
        SDL_Event e{};
        e.type = type;
        e.key.windowID = SDL_GetWindowID(w);
        e.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
        e.key.repeat = repeat ? 1 : 0;
        e.key.keysym.sym = sym;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
        e.key.keysym.mod = mod;
        SDL_PushEvent(&e);
    };
    auto fullscreen_of = [](SDL_Window* w) { return (SDL_GetWindowFlags(w) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0; };
    auto open = [&](Application& app, Aspect aspect, bool match) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");                            // (SDL_Quit clears the hints: set before every start)
        SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
        ApplicationConfig cfg;
        cfg.headless = false;                                                  // a real window (of SDL's dummy driver) that has a fullscreen state
        cfg.skip_intro = true;
        cfg.aspect = aspect;
        cfg.aspect_given = true;
        cfg.start_in_map_select = !match;
        cfg.settings_path = settings.string();
        const QuietStdout quiet;
        return app.init(cfg);
    };
    {
        Application app;
        check(open(app, Aspect::Wide16x9, false), "a window of the dummy driver opens (the setup screen)");
        SDL_Window* window = find_window();
        check(window != nullptr, "it has a window");
        if (window != nullptr) {
            check(!fullscreen_of(window), "the window starts as a window");
            check(app.toggle_fullscreen() && fullscreen_of(window), "toggle_fullscreen() enters fullscreen and says so");
            check(!app.toggle_fullscreen() && !fullscreen_of(window), "toggle_fullscreen() again leaves it");
            // the key
            key_event(window, SDL_KEYDOWN, SDLK_RETURN, KMOD_LALT);
            app.run_frame_with_delta(0.001f);
            check(fullscreen_of(window), "Alt+Enter enters fullscreen");
            check(app.state() == AppState::MapSelect, "... and the setup screen did not see the Enter (it would have started the match)");
            key_event(window, SDL_KEYDOWN, SDLK_RETURN, KMOD_LALT);
            app.run_frame_with_delta(0.001f);
            check(!fullscreen_of(window), "Alt+Enter again leaves it");
            key_event(window, SDL_KEYDOWN, SDLK_KP_ENTER, KMOD_RALT);
            app.run_frame_with_delta(0.001f);
            check(fullscreen_of(window), "the keypad's Enter with the right Alt does the same");
            key_event(window, SDL_KEYDOWN, SDLK_RETURN, KMOD_LALT, true);       // a held key: the auto-repeat does nothing (one repeat, then three: an even number would toggle back)
            app.run_frame_with_delta(0.001f);
            check(fullscreen_of(window), "a held Alt+Enter toggles once: a repeat does nothing");
            for (int i = 0; i < 3; ++i) key_event(window, SDL_KEYDOWN, SDLK_RETURN, KMOD_LALT, true);
            app.run_frame_with_delta(0.001f);
            check(fullscreen_of(window), "... nor do three");
            key_event(window, SDL_KEYDOWN, SDLK_a, KMOD_LALT);
            key_event(window, SDL_KEYDOWN, SDLK_ESCAPE, KMOD_LALT);
            app.run_frame_with_delta(0.001f);
            check(fullscreen_of(window), "Alt with any other key does nothing");
            check(!app.toggle_fullscreen() && !fullscreen_of(window), "(back to a window)");
        }
        app.shutdown();
    }
    {   // Enter alone is the screen's: the setup screen starts the match
        Application app;
        check(open(app, Aspect::Classic4x3, false), "the classic application opens");
        SDL_Window* window = find_window();
        if (window != nullptr) {
            key_event(window, SDL_KEYDOWN, SDLK_RETURN, 0);
            app.run_frame_with_delta(0.001f);
            check(!fullscreen_of(window), "Enter without Alt does not change the window");
            check(app.state() == AppState::Playing, "... it is the setup screen's START: the match began");
        }
        app.shutdown();
    }
    {   // the chat box does not receive the Enter of Alt+Enter: a match with text typed
        Application app;
        check(open(app, Aspect::Classic4x3, true), "a match opens");
        SDL_Window* window = find_window();
        if (window != nullptr) {
            app.hud().update(app.sim().get_world_state(), 100);
            app.hud().set_chat_input("hello");
            key_event(window, SDL_KEYDOWN, SDLK_RETURN, KMOD_LALT);
            app.run_frame_with_delta(0.001f);
            check(fullscreen_of(window) && app.hud().get_chat_input() == "hello", "Alt+Enter in a match toggles fullscreen and does not send the chat text");
            key_event(window, SDL_KEYDOWN, SDLK_RETURN, 0);
            app.run_frame_with_delta(0.001f);
            check(app.hud().get_chat_input().empty(), "(Enter alone sends it)");
        }
        app.shutdown();
    }
    std::error_code ignore;
    std::filesystem::remove(settings, ignore);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// The screenshot
// ---------------------------------------------------------------------------------------------------------------------------------------------------

void test_screenshot(const assets::AssetArchive& arc) {
    group("screenshot", "the screenshot is the whole picture as the window shows it: right for a canvas whose shape is not the window's");
    ensure_sdl();
    const QuietStdout quiet;
    struct Case {
        int32_t canvas_w, canvas_h, win_w, win_h;
        int32_t shot_w, shot_h;
        const char* note;
    };
    const Case cases[] = {
        {960, 540, 1280, 960, 1280, 720, "a 16:9 canvas in a 4:3 window: the picture only, 1280 x 720 (bars above and below are not in it)"},
        {960, 540, 1280, 720, 1280, 720, "a 16:9 canvas in a 16:9 window"},
        {960, 540, 1920, 1200, 1920, 1080, "16:9 in a 16:10 window"},
        {960, 540, 960, 540, 960, 540, "the canvas itself"},
        {640, 480, 1280, 960, 1280, 960, "4:3 in its 2x window: the whole window, as it always was"},
        {640, 480, 1920, 1080, 1440, 1080, "4:3 in a 16:9 window: the picture, 1440 x 1080 (it used to be the picture at the top left of a 1920 x 1080 file)"},
        {640, 480, 640, 480, 640, 480, "4:3 at scale 1"},
    };
    for (const Case& c : cases) {
        SDL_Window* win = SDL_CreateWindow("screenshot", 0, 0, c.win_w, c.win_h, SDL_WINDOW_HIDDEN);
        Renderer r;
        const bool up = win != nullptr && r.init(win, arc);
        check(up, std::string("a renderer for ") + c.note);
        if (!up) {
            if (win != nullptr) SDL_DestroyWindow(win);
            continue;
        }
        r.set_canvas_size(c.canvas_w, c.canvas_h);
        r.begin_frame();
        // a red canvas with a green corner at the top left and a blue one at the bottom right
        r.fill_rect(0, 0, c.canvas_w, c.canvas_h, ants::assets::ColorRGBA{255, 0, 0, 255});
        r.fill_rect(0, 0, 8, 8, ants::assets::ColorRGBA{0, 255, 0, 255});
        r.fill_rect(c.canvas_w - 8, c.canvas_h - 8, 8, 8, ants::assets::ColorRGBA{0, 0, 255, 255});
        const std::filesystem::path file = temp_path("ants_canvas_shot").replace_extension(".bmp");
        const bool saved = r.save_screenshot(file.string());
        check(saved, std::string("the screenshot is saved: ") + c.note);
        SDL_Surface* shot = saved ? SDL_LoadBMP(file.string().c_str()) : nullptr;
        check(shot != nullptr, "... and it is a picture");
        if (shot != nullptr) {
            const std::string at = std::string(c.note) + ": ";
            check(shot->w == c.shot_w && shot->h == c.shot_h, at + "the file is " + std::to_string(c.shot_w) + " x " + std::to_string(c.shot_h) + ", not " + std::to_string(shot->w) + " x " + std::to_string(shot->h));
            SDL_Surface* conv = SDL_ConvertSurfaceFormat(shot, SDL_PIXELFORMAT_RGBA32, 0);
            if (conv != nullptr) {
                auto pixel = [&](int32_t x, int32_t y) {
                    const uint8_t* p = static_cast<const uint8_t*>(conv->pixels) + static_cast<size_t>(y) * static_cast<size_t>(conv->pitch) + static_cast<size_t>(x) * 4u;
                    return std::array<int, 3>{p[0], p[1], p[2]};
                };
                const int32_t w = conv->w;
                const int32_t h = conv->h;
                // (the corners are checked 2 pixels in: at a fractional scale the last row and column of a small block may round to the canvas's red)
                check(pixel(0, 0) == std::array<int, 3>{0, 255, 0} && pixel(2, 2) == std::array<int, 3>{0, 255, 0}, at + "the first pixels are the canvas's green corner");
                check(pixel(w - 3, h - 3) == std::array<int, 3>{0, 0, 255}, at + "the last pixels are the canvas's blue corner");
                check(pixel(w - 1, 0) == std::array<int, 3>{255, 0, 0} && pixel(0, h - 1) == std::array<int, 3>{255, 0, 0} && pixel(w - 1, h - 1)[0] + pixel(w - 1, h - 1)[2] > 0 &&
                          pixel(w / 2, h / 2) == std::array<int, 3>{255, 0, 0},
                      at + "the other corners and the middle are the canvas's red: no black bar and no empty part");
                SDL_FreeSurface(conv);
            }
            SDL_FreeSurface(shot);
        }
        std::error_code ignore;
        std::filesystem::remove(file, ignore);
        r.shutdown();
        SDL_DestroyWindow(win);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// The renderer's picture inside a canvas
// ---------------------------------------------------------------------------------------------------------------------------------------------------

void test_renderer_picture(const assets::AssetArchive& arc) {
    group("picture", "a picture inside a bigger canvas: everything the renderer draws lands at the picture's corner and nothing leaves the picture");
    ensure_sdl();
    const QuietStdout quiet;
    SDL_Window* win = SDL_CreateWindow("picture", 0, 0, 960, 540, SDL_WINDOW_HIDDEN);
    Renderer r;
    const bool up = win != nullptr && r.init(win, arc);
    check(up, "a renderer over a 960 x 540 window");
    if (!up) {
        if (win != nullptr) SDL_DestroyWindow(win);
        return;
    }
    r.set_canvas_size(960, 540);
    check_rect(r.picture(), LayoutRect{0, 0, 960, 540}, "the picture is the whole canvas after set_canvas_size");
    SDL_Renderer* sr = r.get_sdl_renderer();
    auto pixel = [&](int32_t x, int32_t y) {
        uint8_t px[4] = {0, 0, 0, 0};
        SDL_Rect rect{x, y, 1, 1};
        SDL_RenderReadPixels(sr, &rect, SDL_PIXELFORMAT_RGBA32, px, 4);
        return std::array<int, 3>{px[0], px[1], px[2]};
    };
    const std::array<int, 3> kBlack{0, 0, 0};
    const std::array<int, 3> kWhite{255, 255, 255};
    // no picture: a fill at (10, 10) is at (10, 10)
    r.begin_frame();
    r.fill_rect(10, 10, 20, 20, ants::assets::ColorRGBA{255, 255, 255, 255});
    check(pixel(10, 10) == kWhite && pixel(29, 29) == kWhite && pixel(30, 30) == kBlack, "without a picture a fill lands where it says");
    // the classic picture centred
    r.set_picture(LayoutRect{160, 30, 640, 480});
    r.begin_frame();
    r.fill_rect(10, 10, 20, 20, ants::assets::ColorRGBA{255, 255, 255, 255});
    check(pixel(170, 40) == kWhite && pixel(189, 59) == kWhite && pixel(190, 60) == kBlack && pixel(10, 10) == kBlack && pixel(29, 29) == kBlack, "a fill at (10, 10) of the picture lands at (170, 40)");
    r.draw_rect(0, 0, 640, 480, ants::assets::ColorRGBA{255, 255, 255, 255});
    check(pixel(160, 30) == kWhite && pixel(799, 509) == kWhite && pixel(159, 30) == kBlack && pixel(800, 100) == kBlack, "a frame round the picture is round the picture's rectangle");
    // nothing leaves the picture: a fill that is larger than the picture is cut at its edge, whatever the clip calls do
    r.begin_frame();
    r.fill_rect(-50, -50, 1000, 1000, ants::assets::ColorRGBA{255, 255, 255, 255});
    check(pixel(160, 30) == kWhite && pixel(799, 509) == kWhite && pixel(159, 100) == kBlack && pixel(800, 100) == kBlack && pixel(300, 29) == kBlack && pixel(300, 510) == kBlack, "a fill beyond the picture is cut at the picture's edge");
    r.set_clip_rect(100, 100, 50, 50);
    r.begin_frame();                                                         // (a new frame restores the picture's clip)
    r.fill_rect(0, 0, 640, 480, ants::assets::ColorRGBA{255, 255, 255, 255});
    check(pixel(160, 30) == kWhite && pixel(799, 509) == kWhite, "a frame begins with the picture's clip");
    r.set_clip_rect(100, 100, 50, 50);                                       // a clip is the picture's coordinates too
    r.begin_frame();
    r.set_clip_rect(100, 100, 50, 50);
    r.fill_rect(0, 0, 640, 480, ants::assets::ColorRGBA{255, 255, 255, 255});
    check(pixel(260, 130) == kWhite && pixel(309, 179) == kWhite && pixel(259, 130) == kBlack && pixel(310, 130) == kBlack && pixel(260, 129) == kBlack && pixel(260, 180) == kBlack, "set_clip_rect(100, 100, 50, 50) clips to the canvas rectangle at (260, 130)");
    r.clear_clip_rect();
    r.fill_rect(-50, -50, 1000, 1000, ants::assets::ColorRGBA{255, 255, 255, 255});
    check(pixel(160, 30) == kWhite && pixel(799, 509) == kWhite && pixel(159, 100) == kBlack && pixel(800, 100) == kBlack && pixel(300, 29) == kBlack && pixel(300, 510) == kBlack,
          "clear_clip_rect is the picture's clip again, not the canvas: a fill beyond the picture is cut at its edge");
    // text and sprites are placed too
    r.begin_frame();
    r.draw_named_sprite("logo.bmp", 25, 23);
    bool drawn = false;
    for (int32_t y = 30 + 23; y < 30 + 23 + 20 && !drawn; ++y) {
        for (int32_t x = 160 + 25; x < 160 + 25 + 40; ++x) {
            if (pixel(x, y) != kBlack) drawn = true;
        }
    }
    check(drawn, "a sprite at (25, 23) is drawn at (185, 53)");
    bool misplaced = false;
    for (int32_t y = 23; y < 43 && !misplaced; ++y) {
        for (int32_t x = 25; x < 65; ++x) {
            if (pixel(x, y) != kBlack) misplaced = true;
        }
    }
    check(!misplaced, "... and not at (25, 23) of the canvas");
    r.begin_frame();
    r.draw_text("Ants", 20, 20, ants::assets::ColorRGBA{255, 255, 255, 255}, FontSize::Px24);
    bool text_in_place = false;
    bool text_misplaced = false;
    for (int32_t y = 0; y < 100; ++y) {
        for (int32_t x = 0; x < 200; ++x) {
            if (pixel(x, y) == kBlack) continue;
            if (x >= 180 && y >= 50) text_in_place = true;
            else text_misplaced = true;
        }
    }
    check(text_in_place && !text_misplaced, "text at (20, 20) is drawn from (180, 50)");
    // the same text again comes from the cache of textures: placed the same way
    r.begin_frame();
    r.draw_text("Ants", 20, 20, ants::assets::ColorRGBA{255, 255, 255, 255}, FontSize::Px24);
    bool cached_in_place = false;
    bool cached_misplaced = false;
    for (int32_t y = 0; y < 100; ++y) {
        for (int32_t x = 0; x < 200; ++x) {
            if (pixel(x, y) == kBlack) continue;
            if (x >= 180 && y >= 50) cached_in_place = true;
            else cached_misplaced = true;
        }
    }
    check(cached_in_place && !cached_misplaced, "... and a text that is drawn again (the texture of the cache) is placed the same");
    // the hit point digits (the fixed 8 x 15 font, a texture of their own)
    r.begin_frame();
    r.draw_fixed_text("88", 20, 20, ants::assets::ColorRGBA{255, 255, 255, 255});
    bool digits_in_place = false;
    bool digits_misplaced = false;
    for (int32_t y = 0; y < 100; ++y) {
        for (int32_t x = 0; x < 200; ++x) {
            if (pixel(x, y) == kBlack) continue;
            if (x >= 180 && x < 180 + 16 && y >= 50 && y < 50 + 15) digits_in_place = true;
            else digits_misplaced = true;
        }
    }
    check(digits_in_place && !digits_misplaced, "the hit point digits at (20, 20) are in the cells from (180, 50)");
    // the image of the minimap (a streaming texture)
    r.begin_frame();
    std::vector<uint8_t> rgba(4u * 4u * 4u, 255);
    r.draw_rgba_image(100, 100, 4, 4, rgba.data());
    check(pixel(260, 130) == kWhite && pixel(263, 133) == kWhite && pixel(100, 100) == kBlack, "an RGBA image at (100, 100) is at (260, 130)");
    // back to the whole canvas for the plate
    r.set_picture(LayoutRect{0, 0, 960, 540});
    r.begin_frame();
    r.fill_rect(900, 500, 60, 40, ants::assets::ColorRGBA{255, 255, 255, 255});
    check(pixel(900, 500) == kWhite && pixel(959, 539) == kWhite, "with the picture set to the whole canvas a fill is at the canvas's own position (the plate)");
    // a new canvas resets the picture
    r.set_picture(LayoutRect{160, 30, 640, 480});
    r.set_canvas_size(960, 540);
    check_rect(r.picture(), LayoutRect{0, 0, 960, 540}, "set_canvas_size makes the picture the whole canvas again");
    r.shutdown();
    SDL_DestroyWindow(win);
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;                                     // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    ensure_sdl();
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }

    test_fit_table();
    test_fit_invariants();
    test_fit_against_sdl();
    test_centred_picture();
    test_window_sizes();
    test_aspect_parsing();
    test_aspect_option();
    test_window_size_option();
    test_renderer_picture(arc);
    test_screenshot(arc);
    test_application_aspects();
    test_settings_key();
    test_centred_pixels();
    test_pointer_over_bars();
    test_alt_enter();

    std::printf("\ncanvas layout: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
