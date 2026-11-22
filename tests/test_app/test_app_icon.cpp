// The desktop game's icon (include/ants_app/app_icon.hpp): the picture is the original game's icon, whole-number zooms of it are exact, and the game gives its window the icon.
//   [picture]  the size, the empty zooms, the transparent corners, the colors (the original's 16-color palette, a few cells of known colors), every zoom as the 1-pixel picture with each cell enlarged
//   [window]   a window of SDL's dummy video driver takes the icon (and no window is refused); the application that makes a window gives it the icon, headless or not
// Usage: test_app_icon. Exit code 0 when every check passes.
#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "ants_app/app_icon.hpp"
#include "ants_app/application.hpp"

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

using Pixel = std::tuple<int, int, int, int>;

Pixel pixel_at(const std::vector<uint8_t>& rgba, int side, int x, int y) {
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(side) + static_cast<size_t>(x)) * 4;
    return Pixel{rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
}

void picture_checks() {
    group("picture", "the original's icon: 32 x 32 cells of its 16-color palette, transparent around the ant, every zoom a whole-number enlargement");
    const int n = app_icon::kCells;
    const std::vector<uint8_t> one = app_icon::rgba(1);
    check(one.size() == static_cast<size_t>(n * n * 4), "the icon at 1 pixel a cell is 32 x 32 x 4 bytes");
    check(app_icon::rgba(0).empty() && app_icon::rgba(-3).empty() && app_icon::rgba(app_icon::kMaxZoom + 1).empty(), "a zoom outside 1 .. the largest gives no picture");
    check(!app_icon::rgba(app_icon::kMaxZoom).empty(), "the largest zoom gives a picture");

    std::set<Pixel> colors;
    int opaque = 0;
    bool whole = true;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const Pixel p = pixel_at(one, n, x, y);
            const int a = std::get<3>(p);
            whole = whole && (a == 0 || a == 255);
            if (a == 255) {
                ++opaque;
                colors.insert(p);
            }
        }
    }
    check(whole, "every cell is fully transparent or fully opaque (no blended edge)");
    check(opaque > 0 && opaque < n * n, "the icon has an ant and has empty space around it");
    check(std::get<3>(pixel_at(one, n, 0, 0)) == 0 && std::get<3>(pixel_at(one, n, n - 1, 0)) == 0 && std::get<3>(pixel_at(one, n, 0, n - 1)) == 0 &&
              std::get<3>(pixel_at(one, n, n - 1, n - 1)) == 0, "the four corners are transparent, as in the original");
    check(colors.size() <= 16, "no more than the 16 colors of the original's palette");
    bool in_palette = true;
    for (const Pixel& p : colors) {
        for (int c : {std::get<0>(p), std::get<1>(p), std::get<2>(p)}) in_palette = in_palette && (c == 0 || c == 128 || c == 255);
    }
    check(in_palette, "every color is made of the palette's levels 0, 128 and 255");
    // a few cells of the original (column, row): the red body, a black outline cell, the white highlight, a lime green, the cyan, the yellow
    check(pixel_at(one, n, 14, 0) == Pixel{255, 0, 0, 255}, "the red of the body (14, 0)");
    check(pixel_at(one, n, 17, 1) == Pixel{0, 0, 0, 255}, "the black outline (17, 1)");
    check(pixel_at(one, n, 21, 4) == Pixel{255, 255, 255, 255}, "the white highlight (21, 4)");
    check(pixel_at(one, n, 20, 3) == Pixel{0, 255, 0, 255}, "the lime green (20, 3)");
    check(pixel_at(one, n, 16, 9) == Pixel{0, 255, 255, 255}, "the cyan (16, 9)");
    check(pixel_at(one, n, 23, 14) == Pixel{255, 255, 0, 255}, "the yellow (23, 14)");

    for (int zoom : {2, 3, 16}) {
        const std::vector<uint8_t> big = app_icon::rgba(zoom);
        const int side = n * zoom;
        bool exact = big.size() == static_cast<size_t>(side * side * 4);
        for (int y = 0; exact && y < side; ++y) {
            for (int x = 0; exact && x < side; ++x) exact = pixel_at(big, side, x, y) == pixel_at(one, n, x / zoom, y / zoom);
        }
        check(exact, "the picture at " + std::to_string(zoom) + " pixels a cell is the 1-pixel picture with every cell enlarged");
    }
    check(app_icon::window_zoom() >= 2 && app_icon::window_zoom() <= app_icon::kMaxZoom && !app_icon::rgba(app_icon::window_zoom()).empty(),
          "the picture that goes to the window system is a whole-number zoom of at least 64 pixels");
}

std::filesystem::path settings_of_this_run() {
#if defined(_WIN32)
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(getpid());
#endif
    return std::filesystem::temp_directory_path() / ("ants_icon_settings_" + std::to_string(pid) + ".ini");
}

void window_checks() {
    group("window", "a window takes the icon; the application gives its window the icon, with a screen or without");
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL starts with the dummy video driver");
    SDL_Window* window = SDL_CreateWindow("icon", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
    check(window != nullptr, "a window is made");
    check(app_icon::set_window_icon(window), "the window takes the icon");
    check(!app_icon::set_window_icon(nullptr), "no window gets no icon");
    if (window != nullptr) SDL_DestroyWindow(window);
    SDL_Quit();

    const std::filesystem::path settings = settings_of_this_run();
    for (bool headless : {true, false}) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");                  // (SDL_Quit clears the hints)
        SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
        ApplicationConfig cfg;
        cfg.headless = headless;
        cfg.skip_intro = true;
        cfg.settings_path = settings.string();
        Application app;
        const bool started = app.init(cfg);
        check(started, std::string("the application starts ") + (headless ? "headless" : "with a window"));
        if (started) check(app.window_icon_set(), std::string("the application gave its window the icon, ") + (headless ? "headless" : "with a window"));
        app.shutdown();
    }
    std::error_code ignored;
    std::filesystem::remove(settings, ignored);
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    picture_checks();
    window_checks();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
