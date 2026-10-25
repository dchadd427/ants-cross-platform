// Bombs that a map FILE holds (layer-2 ids 129 .. 132, docs/reverse_engineering/map_file_bombs.md), on the screen and on the minimap:
//   * the real Renderer draws a file bomb exactly as it draws the same bomb planted after the load (Grid::place_bomb): the same pixels, in the color of its id, once;
//   * the dictionary does not matter ("." as the editors leave it, or the tile's own name): a bomb that went off (or was defused) leaves nothing behind, not even the static object
//     that its dictionary name would have made;
//   * under Fog of War an unexplored file bomb is hidden like a planted one;
//   * the minimap and the map picture show the terrain under a bomb, as the original does (FUN_01009596 excludes 0x81 .. 0x84).
// New small file for the topic (software renderer, dummy video driver); its own check() function (it counts and goes on), not the shared assert macros.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ants_app/map_preview.hpp"
#include "ants_app/minimap_tables.hpp"
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

// The static map layers of the whole map, by tiling camera positions over the playfield (as tests/test_app/test_render_parity.cpp does)
Image capture_map(Renderer& r, const sim::Grid& grid, uint32_t mw, uint32_t mh, const sim::WorldState* world = nullptr) {
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
            r.render_map_layers(grid, world);
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

// Pixels of the 32 x 32 cell (col, row) that differ between two captures
long cell_diff(Image& a, Image& b, int col, int row) {
    long n = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (std::memcmp(a.at(col * 32 + x, row * 32 + y), b.at(col * 32 + x, row * 32 + y), 4) != 0) ++n;
    return n;
}
long image_diff(Image& a, Image& b) {
    long n = 0;
    for (size_t i = 0; i < a.px.size(); i += 4)
        if (std::memcmp(&a.px[i], &b.px[i], 4) != 0) ++n;
    return n;
}

struct BombTile {
    uint16_t id;
    uint8_t team;
    const char* name;
};
constexpr BombTile kBombs[4] = {{129, 1, "redbomb"}, {130, 0, "greenbomb"}, {131, 3, "blackbomb"}, {132, 2, "bluebomb"}};

// TINY on a flat floor (the layer-1 tile of the parity tests' dynamic items) with no objects: a level with a clear stage for the bombs
assets::LevelData flat_level(const assets::AssetArchive& arc) {
    assets::LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) { check(false, "load TINY"); return level; }
    const int32_t g = arc.find_animation_id("g01a");
    if (level.tile_dictionary.size() <= static_cast<size_t>(g)) level.tile_dictionary.resize(static_cast<size_t>(g) + 1, ".");
    level.tile_dictionary[static_cast<size_t>(g)] = "g01a";
    for (auto& c : level.layer1_terrain) { c.tile_index = static_cast<uint16_t>(g); c.flags = 0; c.properties = 0; }
    for (auto& c : level.layer2_interactive) { c.tile_index = assets::LVL_EMPTY_TILE; c.flags = 0; c.properties = 0; }
    level.anthill_spawns.clear();
    level.food_schedules.clear();
    level.waypoints.clear();
    return level;
}

void put_file_bombs(assets::LevelData& level, const char* naming) {
    for (int k = 0; k < 4; ++k) {
        auto& c = level.layer2_interactive[static_cast<size_t>(1) * level.width + static_cast<size_t>(3 * k)];
        c.tile_index = kBombs[k].id;
        c.flags = 1;
        c.properties = 0x0101;
        if (level.tile_dictionary.size() <= kBombs[k].id) level.tile_dictionary.resize(static_cast<size_t>(kBombs[k].id) + 1, ".");
        level.tile_dictionary[kBombs[k].id] = std::string(naming) == "own" ? std::string(kBombs[k].name) : std::string(".");
    }
}

void test_parity_with_planted_bombs(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[file bombs] drawn exactly as the bombs planted after the load\n");
    for (const char* naming : {"dot", "own"}) {
        assets::LevelData with = flat_level(arc);
        put_file_bombs(with, naming);
        assets::LevelData without = flat_level(arc);
        sim::Grid ga, gp, gn;
        check(ga.init_from_level(with) && gp.init_from_level(without) && gn.init_from_level(without), "init");
        for (int k = 0; k < 4; ++k) gp.place_bomb(static_cast<uint32_t>(3 * k), 1, kBombs[k].team);
        for (uint32_t t : {0u, 130u, 610u}) {
            r.set_level(with);
            r.pin_animation_clock(t);
            Image got = capture_map(r, ga, with.width, with.height);
            r.set_level(without);
            r.pin_animation_clock(t);
            Image planted = capture_map(r, gp, without.width, without.height);
            Image empty = capture_map(r, gn, without.width, without.height);
            check(image_diff(got, planted) == 0, std::string("file bombs (") + naming + ") draw the pixels of planted bombs at t=" + std::to_string(t));
            for (int k = 0; k < 4; ++k) check(cell_diff(got, empty, 3 * k, 1) > 50, "the bomb of color " + std::to_string(kBombs[k].team) + " is visible (" + naming + ")");
        }
        // the four colors are four different pictures (red, green, black, blue)
        r.set_level(with);
        r.pin_animation_clock(0);
        Image got = capture_map(r, ga, with.width, with.height);
        for (int a = 0; a < 4; ++a)
            for (int b = a + 1; b < 4; ++b) {
                long n = 0;
                for (int y = 0; y < 32; ++y)
                    for (int x = 0; x < 32; ++x)
                        if (std::memcmp(got.at(3 * a * 32 + x, 32 + y), got.at(3 * b * 32 + x, 32 + y), 4) != 0) ++n;
                check(n > 20, "two colors of file bombs differ");
            }
        r.unpin_animation_clock();
    }
}

void test_no_ghost_after_the_bomb_is_gone(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[file bombs] a bomb that went off leaves nothing on the screen, whatever its dictionary entry says\n");
    for (const char* naming : {"dot", "own"}) {
        assets::LevelData with = flat_level(arc);
        put_file_bombs(with, naming);
        assets::LevelData without = flat_level(arc);
        sim::Grid ga, gn;
        ga.init_from_level(with);
        gn.init_from_level(without);
        for (int k = 0; k < 4; ++k) ga.clear_bomb(static_cast<uint32_t>(3 * k), 1);
        r.set_level(with);
        r.pin_animation_clock(0);
        Image got = capture_map(r, ga, with.width, with.height);
        r.set_level(without);
        r.pin_animation_clock(0);
        Image empty = capture_map(r, gn, without.width, without.height);
        check(image_diff(got, empty) == 0, std::string("no ghost of a gone file bomb (") + naming + ")");
        r.unpin_animation_clock();
    }
}

void test_fog_hides_unexplored_file_bombs(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[file bombs] under Fog of War an unexplored one is hidden like a planted one\n");
    assets::LevelData with = flat_level(arc);
    put_file_bombs(with, "dot");
    assets::LevelData without = flat_level(arc);
    sim::Grid ga, gp;
    ga.init_from_level(with);
    gp.init_from_level(without);
    for (int k = 0; k < 4; ++k) gp.place_bomb(static_cast<uint32_t>(3 * k), 1, kBombs[k].team);
    sim::WorldState world;
    world.fog_of_war_enabled = true;
    world.width = with.width;
    world.height = with.height;
    world.fog_revealed.assign(static_cast<size_t>(with.width) * with.height, 0);
    r.set_level(with);
    r.pin_animation_clock(0);
    Image a = capture_map(r, ga, with.width, with.height, &world);
    r.set_level(without);
    r.pin_animation_clock(0);
    Image p = capture_map(r, gp, without.width, without.height, &world);
    check(image_diff(a, p) == 0, "unexplored: file bombs and planted bombs look the same");
    r.unpin_animation_clock();
}

void test_minimap_shows_terrain(const assets::AssetArchive& arc) {
    std::printf("[file bombs] the minimap objects: a bomb is the original's 129 (terrain color)\n");
    assets::LevelData with = flat_level(arc);
    put_file_bombs(with, "dot");
    sim::Grid g;
    g.init_from_level(with);
    for (int k = 0; k < 4; ++k) {
        const sim::TileCell& cell = g.get_cell(static_cast<uint32_t>(3 * k), 1);
        check(cell.has_bomb(), "file bomb in the grid");
        const uint16_t id = minimap_object_id(cell);
        check(id == kMinimapBomb && minimap_is_bomb(id), "minimap_object_id of a file bomb is the original's bomb");
    }
}

void test_map_picture_shows_terrain(const assets::AssetArchive& arc) {
    std::printf("[file bombs] the map picture (setup screen, minimap colors): a bomb shows the terrain under it, as the original's minimap does\n");
    const auto& palette = arc.get_palette();
    // a flat level with four bombs against the same level without them
    assets::LevelData with = flat_level(arc);
    put_file_bombs(with, "dot");
    const assets::LevelData without = flat_level(arc);
    const MapPreview a = render_map_preview(with, palette, 4);
    const MapPreview b = render_map_preview(without, palette, 4);
    check(a.valid() && b.valid() && a.rgba == b.rgba, "flat level: the picture with file bombs is the picture without");
    // Bombz Away, a floor of 895 bombs, against the same map with every bomb cell emptied
    assets::LevelData bombz;
    if (!bombz.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/Bombz Away.lvl")) { check(false, "load Bombz Away"); return; }
    assets::LevelData emptied = bombz;
    size_t cells = 0;
    for (auto& c : emptied.layer2_interactive) {
        if (c.tile_index >= 129 && c.tile_index <= 132) { c.tile_index = assets::LVL_EMPTY_TILE; c.flags = 0; c.properties = 0; ++cells; }
    }
    check(cells == 896, "Bombz Away holds 896 bomb cells in its file");
    const MapPreview c1 = render_map_preview(bombz, palette, 4);
    const MapPreview c2 = render_map_preview(emptied, palette, 4);
    check(c1.valid() && c2.valid() && c1.width == c2.width && c1.height == c2.height, "Bombz Away: pictures");
    long differing = 0;
    for (size_t i = 0; i + 3 < c1.rgba.size() && i + 3 < c2.rgba.size(); i += 4)
        if (std::memcmp(&c1.rgba[i], &c2.rgba[i], 4) != 0) ++differing;
    // (the only cell that may differ is the food anchor in the middle that replaced a bomb in one of the two; none does: it is food in both)
    check(differing == 0, "Bombz Away: the picture is the same with the bomb cells emptied (" + std::to_string(differing) + " pixels differ)");
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
    SDL_Window* win = SDL_CreateWindow("file-bombs", 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT, SDL_WINDOW_HIDDEN);
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

    test_parity_with_planted_bombs(r, arc);
    test_no_ghost_after_the_bomb_is_gone(r, arc);
    test_fog_hides_unexplored_file_bombs(r, arc);
    test_minimap_shows_terrain(arc);
    test_map_picture_shows_terrain(arc);

    r.shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::printf("\nfile bombs (render): %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
