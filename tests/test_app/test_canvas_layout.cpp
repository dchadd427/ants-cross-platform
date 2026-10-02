// Canvas layout (milestone M2 of the widescreen work): the picture that the window shows and how it sits in the window (include/ants_app/canvas_layout.hpp), the option that asks for a
// 16:9 picture (`--aspect`, the settings key `aspect`), the window that opens for it, fullscreen (Alt+Enter) and the screenshot of a canvas that is not 4:3.
//   * the model: the game draws into SDL's logical canvas, 640 x 480 (the original's, the default) or 960 x 540 (16:9), which SDL scales into the window by the largest scale that
//     fits, centred, with bars: `CanvasLayout::fit` is that arithmetic (a table of window sizes: 1920 x 1080 is 2x exactly, 2560 x 1440 2.667x, 2880 x 1800 3x with bars above and
//     below ...), checked against SDL itself; the window that opens is the largest whole-number multiple of the canvas that fits the display;
//   * the application: `--aspect 16:9` / `4:3` and the key `aspect` (refusals say "only 16:9 and 4:3 for now"), the classic picture centred in the wide canvas (pixel for pixel
//     what the 4:3 application draws, the plate in the canvas's corner), the pointer over the bars (it is the picture's nearest edge pixel, the map scrolls), Alt+Enter, the screenshot.
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

using namespace ants;
using namespace ants::app;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

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
    group("window", "the window of a canvas: the largest whole-number multiple that fits the display's usable area (at least 1x), centred; the grid's cells of the canvas's shape");
    // 960 x 540
    auto open = [](int32_t area_w, int32_t area_h, int32_t top = 0, int32_t canvas_w = 960, int32_t canvas_h = 540) {
        return largest_canvas_window(WindowRect{0, 0, area_w, area_h}, canvas_w, canvas_h, top, 0, 0, 0);
    };
    check(open(1920, 1080) == WindowRect{0, 0, 1920, 1080}, "a 1920 x 1080 area: 2x, 1920 x 1080");
    check(open(1920, 1050) == WindowRect{480, 255, 960, 540}, "a 1920 x 1050 area (a task bar): 1x, only the height decides, centred");
    check(open(3840, 2160).w == 3840 && open(3840, 2160).h == 2160, "a 3840 x 2160 area: 4x");
    check(open(2560, 1415, 28).w == 1920 && open(2560, 1415, 28).h == 1080, "a 2560 x 1415 area with a 28 px title bar: 2x (the height gives 2.57)");
    check(open(1512, 944, 28).w == 960 && open(1512, 944, 28).h == 540, "a laptop's 1512 x 944 area: 1x");
    check(open(800, 450).w == 960 && open(800, 450).h == 540, "an area smaller than the canvas: still 1x");
    check(open(-5, -5).w == 960, "an area of no size: 1x");
    // centred in the area (the decoration on top)
    const WindowRect r = largest_canvas_window(WindowRect{100, 50, 1920, 1100}, 960, 540, 28, 0, 0, 0);
    check(r.w == 960 && r.h == 540, "a 1920 x 1100 area with a 28 px title bar: 1x (1072 rows are left)");
    check(r.x == 100 + (1920 - 960) / 2 && r.y == 50 + 28 + (1072 - 540) / 2, "... centred in what the decoration leaves: x 580, y 344");
    const WindowRect big = largest_canvas_window(WindowRect{0, 0, 3000, 2000}, 640, 480, 0, 0, 0, 0);
    check(big.w == 2560 && big.h == 1920 && big.x == 220 && big.y == 40, "a 640 x 480 canvas in 3000 x 2000: 4x, centred at (220, 40)");
    // borders on all sides
    const WindowRect b = largest_canvas_window(WindowRect{0, 0, 1000, 600}, 320, 180, 20, 10, 30, 10);
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
    check(c.aspect == Aspect::Classic4x3 && !c.aspect_given && c.startup_error.empty(), "without the option the aspect is 4:3 and not given");
    c = parse({"ants", "--aspect", "16:9"});
    check(c.aspect == Aspect::Wide16x9 && c.aspect_given && c.startup_error.empty(), "--aspect 16:9");
    c = parse({"ants", "--headless", "--aspect", "4:3", "--map", "x.lvl"});
    check(c.aspect == Aspect::Classic4x3 && c.aspect_given && c.startup_error.empty(), "--aspect 4:3 is given (it beats the settings' key) and is the classic canvas");
    c = parse({"ants", "--aspect", "21:9"});
    check(c.startup_error.find("--aspect") != std::string::npos && c.startup_error.find("21:9") != std::string::npos && c.startup_error.find("only 16:9 and 4:3 for now") != std::string::npos &&
              !c.aspect_given && c.aspect == Aspect::Classic4x3,
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
            check(f.app.layout().is_classic() && f.app.renderer().layout().is_classic() && f.app.hud().layout().is_classic(), "16:9 (M2): the match screen is still the classic picture");
            check_rect(f.app.picture(), LayoutRect{160, 30, 640, 480}, "16:9: the 640 x 480 picture is centred at (160, 30)");
            check_rect(f.app.renderer().picture(), LayoutRect{160, 30, 640, 480}, "16:9: the renderer has it too");
            check(ww == 1280 && wh == 720, "16:9: --window-size wins");
        }
    }
    {   // the window that opens without --window-size: the largest multiple of the canvas that fits the display's usable area
        AppFixture f("", config_of(Aspect::Wide16x9, true));
        check(f.ok, "16:9 without a window size starts");
        if (f.ok) {
            int32_t ww = 0, wh = 0;
            f.window_size(ww, wh);
            SDL_Rect area{0, 0, 0, 0};
            const bool have_area = SDL_GetDisplayUsableBounds(0, &area) == 0 || SDL_GetDisplayBounds(0, &area) == 0;
            const WindowRect expected = largest_canvas_window(WindowRect{area.x, area.y, area.w, area.h}, 960, 540, 0, 0, 0, 0);
            check(have_area && ww == expected.w && wh == expected.h, "16:9: the window is the largest multiple of 960 x 540 in the display's area (" + std::to_string(ww) + " x " + std::to_string(wh) + ")");
            check(ww % 960 == 0 && wh % 540 == 0 && ww / 960 == wh / 540 && ww >= 960, "... a whole-number multiple, at least 1x");
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
    {   // a fullscreen request keeps the default window (the canvas fills the monitor as far as it fits)
        ApplicationConfig cfg = config_of(Aspect::Wide16x9, true);
        cfg.fullscreen = true;
        AppFixture f("", cfg);
        check(f.ok && f.app.aspect() == Aspect::Wide16x9 && f.app.canvas() == CanvasLayout{960, 540}, "16:9 with --fullscreen: the same canvas");
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
    // the match screen: the classic frame (without its corner plate) is the wide frame's picture
    const std::vector<uint8_t> a = masked(read_canvas(classic.app, 0, 0, 640, 480), 640, 440, 464, 200, 16);
    const std::vector<uint8_t> b = masked(read_canvas(wide.app, 160, 30, 640, 480), 640, 440, 464, 200, 16);
    check(a == b, "the match screen of the 16:9 canvas is the 4:3 application's frame, centred at (160, 30)");
    // a frame sets the picture itself, whatever the renderer was left with
    wide.app.renderer().set_picture(LayoutRect{0, 0, 960, 540});
    wide.app.render_frame();
    check(masked(read_canvas(wide.app, 160, 30, 640, 480), 640, 440, 464, 200, 16) == b, "a frame of the 16:9 application draws the centred picture even when the renderer's picture was changed since the last one");
    // nothing else is drawn but the plate: the bars are black
    auto black = [&](int32_t x, int32_t y, int32_t w, int32_t h) {
        const std::vector<uint8_t> p = read_canvas(wide.app, x, y, w, h);
        for (size_t i = 0; i + 3 < p.size(); i += 4) {
            if (p[i] != 0 || p[i + 1] != 0 || p[i + 2] != 0) return false;
        }
        return true;
    };
    check(black(0, 0, 160, 540) && black(800, 0, 160, 510) && black(160, 0, 640, 30) && black(160, 510, 640, 8), "the bars around the picture are black (the plate is below)");
    // the plate (the version next to the frame rate) is in the canvas's bottom right corner: rows 527 .. 539, and not in the picture's own corner
    auto bar_pixels = [&](Application& app, int32_t x, int32_t y, int32_t w, int32_t h) {
        const std::vector<uint8_t> p = read_canvas(app, x, y, w, h);
        int n = 0;
        for (size_t i = 0; i + 3 < p.size(); i += 4) n += (p[i + 1] >= 185 && p[i] < 100 && p[i + 2] < 120) ? 1 : 0;
        return n;
    };
    check(bar_pixels(wide.app, 834, 527, 118, 13) > 20, "16:9: the sparkline is in the canvas's bottom right corner (rows 527 .. 539)");
    check(bar_pixels(wide.app, 160 + 514, 30 + 467, 118, 13) == 0, "16:9: and not in the picture's corner (the original's place of the plate)");
    check(bar_pixels(classic.app, 514, 467, 118, 13) > 20, "4:3: it is in the picture's corner, as it was");
    // the setup screen is a page: the same
    AppFixture classic_setup("", config_of(Aspect::Classic4x3, true, 640, 480), false);
    AppFixture wide_setup("", config_of(Aspect::Wide16x9, true, 960, 540), false);
    check(classic_setup.ok && wide_setup.ok, "both applications show the setup screen");
    if (classic_setup.ok && wide_setup.ok) {
        for (AppFixture* f : {&classic_setup, &wide_setup}) f->app.renderer().pin_animation_clock(1500);
        classic_setup.app.render_frame();
        wide_setup.app.render_frame();
        const std::vector<uint8_t> sa = masked(read_canvas(classic_setup.app, 0, 0, 640, 480), 640, 440, 464, 200, 16);
        const std::vector<uint8_t> sb = masked(read_canvas(wide_setup.app, 160, 30, 640, 480), 640, 440, 464, 200, 16);
        check(sa == sb, "the setup screen of the 16:9 canvas is the 4:3 application's, centred");
    }
}

void test_pointer_over_bars() {
    group("pointer", "the pointer in a 16:9 canvas: the picture's own coordinates, and the bars around the picture count as its nearest edge pixel");
    AppFixture f("", config_of(Aspect::Wide16x9, true, 1920, 1080));       // scale 2 exactly: a canvas pixel is 2 x 2 window pixels
    check(f.ok && f.window != nullptr, "the 16:9 application runs a match");
    if (!f.ok || f.window == nullptr) return;
    f.motion_at_canvas(160 + 100, 30 + 100);
    f.deliver();
    check(f.app.mouse_screen_x() == 100 && f.app.mouse_screen_y() == 100, "the canvas point (260, 130) is the picture's (100, 100)");
    f.motion_at_canvas(160, 30);
    f.deliver();
    check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 0, "the picture's first pixel is (0, 0)");
    f.motion_at_canvas(799, 509);
    f.deliver();
    check(f.app.mouse_screen_x() == 639 && f.app.mouse_screen_y() == 479, "the picture's last pixel is (639, 479)");
    // over a bar: the nearest edge pixel; the pointer is not gone and the map scrolls
    auto center = [&]() { f.app.renderer().camera().center_on(600, 600, f.app.sim().grid().width(), f.app.sim().grid().height()); };
    center();
    const int32_t x0 = f.app.renderer().camera().world_x;
    f.motion_at_canvas(40, 270);                                               // the left bar, level with the picture's middle
    f.deliver();
    check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 240 && !f.app.pointer_outside(), "over the left bar the pointer is on the picture's left edge, level with the pointer");
    f.scroll(12);
    check(f.app.renderer().camera().world_x < x0, "... and the map scrolls west");
    center();
    const int32_t x1 = f.app.renderer().camera().world_x;
    f.motion_at_canvas(930, 270);                                              // the right bar
    f.deliver();
    check(f.app.mouse_screen_x() == 639 && f.app.mouse_screen_y() == 240, "over the right bar: the right edge");
    f.scroll(12);
    check(f.app.renderer().camera().world_x > x1, "... and the map scrolls east");
    center();
    const int32_t y0 = f.app.renderer().camera().world_y;
    f.motion_at_canvas(480, 10);                                               // the top bar
    f.deliver();
    check(f.app.mouse_screen_x() == 320 && f.app.mouse_screen_y() == 0, "over the top bar: the top edge");
    f.scroll(12);
    check(f.app.renderer().camera().world_y < y0, "... and the map scrolls north");
    // far beyond the window
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.windowID = SDL_GetWindowID(f.window);
    e.motion.x = -40000;
    e.motion.y = 40000;
    SDL_PushEvent(&e);
    f.deliver();
    check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 479, "far beyond the window's bottom left corner: the picture's bottom left pixel");
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

int main() {
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
