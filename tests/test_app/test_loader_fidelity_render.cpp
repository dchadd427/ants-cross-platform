// The map loader as the original's (docs/reverse_engineering/map_loader_fidelity.md), on the screen: what the renderer draws of a map file follows the tile ids of the file, not the names of its
// dictionary. Companion of tests/test_sim/test_loader_fidelity.cpp (software renderer, dummy video driver); its own check() function (it counts and goes on), not the shared assert macros.
#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ants_app/renderer.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/grid.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what.c_str());
    }
}

struct Image {
    int w{0};
    int h{0};
    std::vector<uint8_t> px;   // RGBA8
    Image() = default;
    Image(int width, int height) : w(width), h(height), px(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u) {}
    uint8_t* at(int x, int y) { return &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u]; }
};

Image read_region(SDL_Renderer* sr, int x, int y, int w, int h) {
    Image im(w, h);
    SDL_Rect rd = {x, y, w, h};
    if (SDL_RenderReadPixels(sr, &rd, SDL_PIXELFORMAT_RGBA32, im.px.data(), w * 4) != 0) {
        std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        std::exit(2);
    }
    return im;
}

// The static map layers of the whole map, by tiling camera positions over the playfield (as tests/test_app/test_file_bombs_render.cpp does)
Image capture_map(Renderer& r, const sim::Grid& grid, uint32_t mw, uint32_t mh) {
    Image out(static_cast<int>(mw) * 32, static_cast<int>(mh) * 32);
    for (size_t i = 0; i < out.px.size(); i += 4) { out.px[i] = 255; out.px[i + 1] = 0; out.px[i + 2] = 255; out.px[i + 3] = 255; }
    std::vector<int> xs, ys;
    for (int x = 0;; x += 384) { xs.push_back(std::min(x, out.w - PLAYFIELD_W)); if (x + PLAYFIELD_W >= out.w) break; }
    for (int y = 0;; y += 384) { ys.push_back(std::min(y, out.h - PLAYFIELD_H)); if (y + PLAYFIELD_H >= out.h) break; }
    SDL_Renderer* sr = r.get_sdl_renderer();
    for (int cy : ys) {
        for (int cx : xs) {
            r.camera().x = static_cast<float>(cx);
            r.camera().y = static_cast<float>(cy);
            r.camera().clamp_to_bounds(mw, mh);
            SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
            SDL_RenderClear(sr);
            r.render_map_layers(grid, nullptr);
            Image tile = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);
            const int camx = static_cast<int>(r.camera().x);
            const int camy = static_cast<int>(r.camera().y);
            for (int y = 0; y < PLAYFIELD_H; ++y) {
                for (int x = 0; x < PLAYFIELD_W; ++x) {
                    const int wx = camx + x, wy = camy + y;
                    if (wx < 0 || wy < 0 || wx >= out.w || wy >= out.h) continue;
                    std::memcpy(out.at(wx, wy), tile.at(x, y), 4);
                }
            }
        }
    }
    return out;
}

long image_diff(Image& a, Image& b) {
    long n = 0;
    for (size_t i = 0; i < a.px.size() && i < b.px.size(); i += 4)
        if (std::memcmp(&a.px[i], &b.px[i], 4) != 0) ++n;
    return n;
}

assets::LevelData tiny() {
    assets::LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) check(false, "load TINY");
    return level;
}

Image picture_of(Renderer& r, const assets::LevelData& level) {
    sim::Grid g;
    check(g.init_from_level(level), "init");
    r.set_level(level);
    r.pin_animation_clock(0);
    Image im = capture_map(r, g, level.width, level.height);
    r.unpin_animation_clock();
    return im;
}

// ---- Fix 2: hills and start markers are tile ids -------------------------------------------------------------------------------------------------------------------------------------------
void test_hills_by_id(Renderer& r) {
    std::printf("[hills] the four hills are drawn from their tile ids (245 .. 248), whatever the dictionary calls those entries\n");
    const assets::LevelData original = tiny();
    Image reference = picture_of(r, original);
    // the names of the four hill entries damaged (the editor's "unnamed")
    assets::LevelData dotted = original;
    for (uint16_t id = 245; id <= 248; ++id) dotted.tile_dictionary[id] = ".";
    Image a = picture_of(r, dotted);
    check(image_diff(reference, a) == 0, "hill names \".\": the same picture");
    // the names of two colours exchanged, and of the other two shifted (a permuted dictionary)
    assets::LevelData permuted = original;
    std::swap(permuted.tile_dictionary[245], permuted.tile_dictionary[248]);
    std::swap(permuted.tile_dictionary[246], permuted.tile_dictionary[247]);
    Image b = picture_of(r, permuted);
    check(image_diff(reference, b) == 0, "hill names permuted: the same picture");
    // and the hills are really in it
    assets::LevelData none = original;
    for (auto& c : none.layer2_interactive) if (assets::hill_team_of_id(c.tile_index) >= 0) c = assets::MapCell{};
    Image bare = picture_of(r, none);
    check(image_diff(reference, bare) > 2000, "the hills are visible in the picture (" + std::to_string(image_diff(reference, bare)) + " pixels)");
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window* win = SDL_CreateWindow("loader-fidelity", 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT, SDL_WINDOW_HIDDEN);
    assets::AssetArchive arc;
    if (!win || !arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open window or ants.chd\n");
        return 2;
    }
    Renderer r;
    if (!r.init(win, arc)) {
        std::fprintf(stderr, "renderer init failed\n");
        return 2;
    }

    test_hills_by_id(r);

    r.shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::printf("\nloader fidelity (render): %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
