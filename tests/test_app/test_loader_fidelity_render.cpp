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
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"
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
Image capture_map(Renderer& r, const sim::Grid& grid, uint32_t mw, uint32_t mh, const sim::WorldState* entities = nullptr) {
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
            if (entities != nullptr) r.render_world(*entities, grid, -1);          // (everything: the object list of the map and the ants on it)
            else r.render_map_layers(grid, nullptr);
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

Image picture_of(Renderer& r, const assets::LevelData& level, bool entities = false) {
    r.set_level(level);
    r.pin_animation_clock(0);
    Image im;
    if (entities) {
        sim::SimulationEngine eng;
        eng.init(level, 3, 0x0F);
        im = capture_map(r, eng.grid(), level.width, level.height, &eng.get_world_state());
    } else {
        sim::Grid g;
        check(g.init_from_level(level), "init");
        im = capture_map(r, g, level.width, level.height);
    }
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

// ---- Fix 4: block 1 holds start markers and plants; every other record is ignored ------------------------------------------------------------------------------------------------------------
void test_ignored_block1_records(Renderer& r) {
    std::printf("[block 1] a record that the original ignores is not drawn, whatever its dictionary entry says; a plant is\n");
    const assets::LevelData original = tiny();
    // a name that draws something: the dictionary entry of an object that TINY has on its map
    std::string drawable;
    uint16_t drawable_id = 0;
    for (const auto& c : original.layer2_interactive) {
        if (c.tile_index == assets::LVL_EMPTY_TILE || c.tile_index >= original.tile_dictionary.size() || (c.flags & 1) == 0) continue;
        const std::string& n = original.tile_dictionary[c.tile_index];
        if (n.empty() || n == "." || assets::hill_team_of_id(c.tile_index) >= 0 || sim::movement::is_powerup_tile(c.tile_index)) continue;
        drawable = n;
        drawable_id = c.tile_index;
        break;
    }
    check(!drawable.empty(), "TINY has an object to take a name from");
    Image reference = picture_of(r, original, true);
    assets::LevelData with = original;
    for (const uint16_t id : {uint16_t{0}, uint16_t{148}, uint16_t{151}, uint16_t{296}}) {
        with.tile_dictionary[id] = drawable;
        assets::AnthillSpawn sp;
        sp.tile_id = id;
        sp.x = static_cast<uint16_t>(8 + id % 7);
        sp.y = static_cast<uint16_t>(14 + id % 5);
        sp.team_id = 255;
        with.anthill_spawns.push_back(sp);
    }
    Image a = picture_of(r, with, true);
    check(image_diff(reference, a) == 0, "records of the ids 0, 148, 151 and 296 named like an object of the map (" + drawable + " = " + std::to_string(drawable_id) + ") draw nothing");
    // a plant record is drawn, by its id, whatever its entry
    assets::LevelData plant = original;
    plant.tile_dictionary[410] = ".";
    assets::AnthillSpawn sp;
    sp.tile_id = 410;
    sp.x = 9;
    sp.y = 16;
    sp.team_id = 255;
    plant.anthill_spawns.push_back(sp);
    Image b = picture_of(r, plant, true);
    check(image_diff(reference, b) > 20, "a plant record (410, named \".\") is drawn (" + std::to_string(image_diff(reference, b)) + " pixels)");
}

// ---- Fix 5: food is the tile flag 0x02 -------------------------------------------------------------------------------------------------------------------------------------------------------
void test_food_by_flag(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[food] an object whose entry is called fd... but whose id is no food is an ordinary object and is drawn\n");
    assets::LevelData level = tiny();
    const int32_t g = arc.find_animation_id("g01a");
    for (auto& c : level.layer1_terrain) { c.tile_index = static_cast<uint16_t>(g); c.flags = 0; c.properties = 0; }
    if (level.tile_dictionary.size() <= static_cast<size_t>(g)) level.tile_dictionary.resize(static_cast<size_t>(g) + 1, ".");
    level.tile_dictionary[static_cast<size_t>(g)] = "g01a";
    for (auto& c : level.layer2_interactive) { c.tile_index = assets::LVL_EMPTY_TILE; c.flags = 0; c.properties = 0; }
    level.anthill_spawns.clear();
    level.food_schedules.clear();
    level.waypoints.clear();
    Image empty = picture_of(r, level);
    // id 344 is called fdpez2b1 and is not food (tile flag 0x02 clear): an ordinary object
    check((sim::movement::tile_flags_of(344) & sim::movement::kTileFlagFood) == 0, "344 is not a food id");
    assets::LevelData with = level;
    with.tile_dictionary[344] = "fdpez2b1";
    auto& c = with.layer2_interactive[static_cast<size_t>(10) * with.width + 10];
    c.tile_index = 344;
    c.flags = 1;
    c.properties = static_cast<uint16_t>((10 << 8) | 10);
    Image a = picture_of(r, with);
    check(image_diff(empty, a) > 50, "an fd... object that is no food is drawn (" + std::to_string(image_diff(empty, a)) + " pixels)");
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
    test_ignored_block1_records(r);
    test_food_by_flag(r, arc);

    r.shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::printf("\nloader fidelity (render): %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
