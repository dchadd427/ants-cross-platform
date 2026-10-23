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
    // the names of two colors exchanged, and of the other two shifted (a permuted dictionary)
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

// A cut of the picture: the rectangle of `cells` x `cells` map cells whose top left cell is (col, row)
Image crop_cells(Image& im, int col, int row, int cells) {
    Image out(cells * 32, cells * 32);
    for (int y = 0; y < out.h; ++y) std::memcpy(out.at(0, y), im.at(col * 32, row * 32 + y), static_cast<size_t>(out.w) * 4u);
    return out;
}

// ---- Food objects of block 2 are drawn from the live cells, whatever layer 2 of the file holds at their anchor ----------------------------------------------------------------------------------
// The original puts the stage tile of every food object on its anchor with SetTile (FUN_01008ca0) and draws what the cells hold; the grid does the same. The renderer once made the
// piles' static objects from the FILE's layer 2 only, so a pile whose anchor cell the file leaves empty or gives to a bomb or another object was in the world and on the minimap and not in the view.
void test_food_without_file_cell(Renderer& r) {
    std::printf("[food] a food object of block 2 is drawn at its anchor whatever layer 2 of the file holds there (empty, a bomb, another object)\n");
    // Bombz Away: 895 bombs on the floor, one pile (15, 15) in the middle that replaced a bomb at load
    assets::LevelData bombz;
    if (!bombz.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/Bombz Away.lvl")) { check(false, "load Bombz Away"); return; }
    check(bombz.food_schedules.size() == 1 && bombz.food_schedules[0].x == 15 && bombz.food_schedules[0].y == 15, "Bombz Away has one food object, at (15, 15)");
    const uint16_t centre = bombz.get_cell_layer2(15, 15).tile_index;
    check(sim::is_original_bomb_tile(centre), "layer 2 of the file holds a bomb on the pile's anchor (" + std::to_string(centre) + ")");
    const uint16_t stage = bombz.food_schedules[0].variants[0].tile_id;
    check((sim::movement::tile_flags_of(stage) & sim::movement::kTileFlagFood) != 0, "the first stage tile (" + std::to_string(stage) + ") is a food id");
    Image with = picture_of(r, bombz);
    Image c_with = crop_cells(with, 13, 13, 5);
    // the same map with no pile and the bomb of the anchor taken out: the floor of bombs around an empty cell
    assets::LevelData bare = bombz;
    bare.food_schedules.clear();
    auto& anchor_cell = bare.layer2_interactive[static_cast<size_t>(15) * bare.width + 15];
    anchor_cell = assets::MapCell{};
    Image bare_full = picture_of(r, bare);
    Image c_bare = crop_cells(bare_full, 13, 13, 5);
    check(image_diff(c_with, c_bare) > 100, "Bombz Away: the pile is in the picture of the middle (" + std::to_string(image_diff(c_with, c_bare)) + " pixels differ from the same floor without it)");
    // the pile as an editor that writes it into the file would have it: the same picture
    assets::LevelData in_file = bombz;
    auto& fc = in_file.layer2_interactive[static_cast<size_t>(15) * in_file.width + 15];
    fc.tile_index = stage;
    fc.flags = 1;
    fc.properties = static_cast<uint16_t>((15 << 8) | 15);
    if (in_file.tile_dictionary.size() <= stage) in_file.tile_dictionary.resize(static_cast<size_t>(stage) + 1, ".");
    Image file_full = picture_of(r, in_file);
    Image c_file = crop_cells(file_full, 13, 13, 5);
    check(image_diff(c_with, c_file) == 0, "Bombz Away: the middle looks as it does with the pile written into layer 2 of the file (" + std::to_string(image_diff(c_with, c_file)) + " pixels differ)");

    // TINY: every pile of the file taken out of layer 2 (the cells with a food id): the picture is the same, the piles come from block 2 alone
    const assets::LevelData tiny_level = tiny();
    Image reference = picture_of(r, tiny_level);
    assets::LevelData block2_only = tiny_level;
    size_t removed = 0;
    for (auto& c : block2_only.layer2_interactive) {
        if (!c.is_empty() && c.tile_index != 0x7FFE && (sim::movement::tile_flags_of(c.tile_index) & sim::movement::kTileFlagFood) != 0) { c = assets::MapCell{}; ++removed; }
    }
    check(removed >= 14, "TINY holds the cells of its 14 piles in layer 2 (" + std::to_string(removed) + " cells)");
    Image b2 = picture_of(r, block2_only);
    check(image_diff(reference, b2) == 0, "TINY: the piles of block 2 are drawn with layer 2 of the file empty there (" + std::to_string(image_diff(reference, b2)) + " pixels differ)");

    // an ordinary object (a pile of toy jacks, id 336) on an anchor in the file: SetTile clears it for the pile, so the picture is the same and shows no jacks
    assets::LevelData over_object = block2_only;
    const auto& first = over_object.food_schedules[0];
    auto& oc = over_object.layer2_interactive[static_cast<size_t>(first.y) * over_object.width + first.x];
    oc.tile_index = 336;
    oc.flags = 1;
    oc.properties = static_cast<uint16_t>((first.x << 8) | first.y);
    if (over_object.tile_dictionary.size() <= 336) over_object.tile_dictionary.resize(337, ".");
    over_object.tile_dictionary[336] = "jacks1";
    check((sim::movement::tile_flags_of(336) & sim::movement::kTileFlagFood) == 0, "336 is no food id");
    Image b3 = picture_of(r, over_object);
    check(image_diff(reference, b3) == 0, "TINY: an object of the file on the anchor of a pile is replaced by the pile in the picture (" + std::to_string(image_diff(reference, b3)) + " pixels differ)");

    // an anchor outside the grid (real: KiM Trez (64, 14) on 60 columns): nothing is placed, and the object that the index row * width + col would hit on another row stays
    {
        assets::LevelData jacks = tiny_level;
        auto& jc = jacks.layer2_interactive[static_cast<size_t>(6) * jacks.width + 3];
        jc.tile_index = 336;
        jc.flags = 1;
        jc.properties = static_cast<uint16_t>((3 << 8) | 6);
        if (jacks.tile_dictionary.size() <= 336) jacks.tile_dictionary.resize(337, ".");
        jacks.tile_dictionary[336] = "jacks1";
        Image with_jacks = picture_of(r, jacks);
        check(image_diff(reference, with_jacks) > 20, "the jacks at (3, 6) are in the picture (" + std::to_string(image_diff(reference, with_jacks)) + " pixels)");
        assets::LevelData outside = jacks;
        assets::FoodSchedule fs;
        fs.y = 5;
        fs.x = static_cast<uint16_t>(outside.width + 3);                        // one row down and three columns in, for the index of a row of the width
        fs.initial_delay = 10;
        fs.respawn_interval = 30;
        fs.variants.push_back(assets::FoodItemVariant{10, 343});
        fs.variants.push_back(assets::FoodItemVariant{0, assets::LVL_EMPTY_TILE});
        outside.food_schedules.push_back(fs);
        Image o = picture_of(r, outside);
        check(image_diff(with_jacks, o) == 0, "a food object outside the grid draws nothing and replaces no object of the file (" + std::to_string(image_diff(with_jacks, o)) + " pixels differ)");
    }
}

// The picture of a level with a grid that the caller has changed
Image picture_with_grid(Renderer& r, const assets::LevelData& level, const sim::Grid& grid) {
    r.set_level(level);
    r.pin_animation_clock(0);
    Image im = capture_map(r, grid, level.width, level.height);
    r.unpin_animation_clock();
    return im;
}

// The pile is at the cell the block names (row, column) and nowhere else, and it follows its stages until it is gone
void test_food_cell_and_stages(Renderer& r) {
    std::printf("[food] a pile is drawn at exactly its row and column; its stages follow the units taken\n");
    assets::LevelData trouble;
    if (!trouble.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/Trouble.lvl")) { check(false, "load Trouble"); return; }
    // the pile at row 15, column 2 (not on the diagonal: a swap of the two would put it at row 2, column 15)
    size_t which = trouble.food_schedules.size();
    for (size_t i = 0; i < trouble.food_schedules.size(); ++i) if (trouble.food_schedules[i].y == 15 && trouble.food_schedules[i].x == 2) which = i;
    check(which < trouble.food_schedules.size(), "Trouble has a pile at row 15, column 2");
    if (which >= trouble.food_schedules.size()) return;
    Image with = picture_of(r, trouble);
    assets::LevelData without = trouble;
    without.food_schedules.erase(without.food_schedules.begin() + static_cast<std::ptrdiff_t>(which));
    Image bare = picture_of(r, without);
    int x0 = with.w, y0 = with.h, x1 = -1, y1 = -1;
    for (int y = 0; y < with.h; ++y) {
        for (int x = 0; x < with.w; ++x) {
            if (std::memcmp(with.at(x, y), bare.at(x, y), 4) == 0) continue;
            x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
        }
    }
    check(x1 >= 0, "the pile is in the picture");
    const int cx = 2 * 32 + 16, cy = 15 * 32 + 16;
    check(x1 >= 0 && x0 <= cx && x1 >= cx && y0 <= cy && y1 >= cy && (x1 - x0) < 96 && (y1 - y0) < 96,
          "the pile covers the middle of the cell at row 15, column 2 (" + std::to_string(x0) + ".." + std::to_string(x1) + ", " + std::to_string(y0) + ".." + std::to_string(y1) + ")");

    // TINY's gum pile at (15, 6) has four stages (272 .. 275): taken down by 10, 10, 5 and 5 units, the picture is the same as with the pile in the file's layer 2, and each stage differs
    const assets::LevelData file_level = tiny();
    assets::LevelData block2_only = file_level;
    for (auto& c : block2_only.layer2_interactive) {
        if (!c.is_empty() && c.tile_index != 0x7FFE && (sim::movement::tile_flags_of(c.tile_index) & sim::movement::kTileFlagFood) != 0) c = assets::MapCell{};
    }
    sim::Grid from_file, from_block2;
    check(from_file.init_from_level(file_level) && from_block2.init_from_level(block2_only), "init");
    const int32_t obj = from_block2.food_object_first_at(sim::TileCoord{6, 15});
    check(obj >= 0 && from_file.food_object_first_at(sim::TileCoord{6, 15}) == obj, "the gum pile is the same object in both");
    Image previous = picture_with_grid(r, block2_only, from_block2);
    Image first_file = picture_with_grid(r, file_level, from_file);
    check(image_diff(previous, first_file) == 0, "stage 1: the same picture");
    const uint16_t takes[4] = {10, 10, 5, 5};
    for (int k = 0; k < 4; ++k) {
        for (sim::Grid* g : {&from_block2, &from_file}) {
            bool changed = false;
            g->take_food(obj, takes[k], changed);
            const sim::FoodObject& o = g->food_objects()[static_cast<size_t>(obj)];
            if (changed) g->set_food_tile(sim::TileCoord{o.col, o.row}, o.stage_tile());       // what the engine does after a bite that changes the stage
        }
        Image a = picture_with_grid(r, block2_only, from_block2);
        Image b = picture_with_grid(r, file_level, from_file);
        check(image_diff(a, b) == 0, "stage " + std::to_string(k + 2) + ": the same picture as with the pile in the file");
        check(image_diff(previous, a) > 0, "stage " + std::to_string(k + 2) + ": the picture changed (" + std::to_string(image_diff(previous, a)) + " pixels)");
        previous = a;
    }
    assets::LevelData no_gum = block2_only;
    no_gum.food_schedules.erase(no_gum.food_schedules.begin() + 12);
    Image gone = picture_of(r, no_gum);
    check(image_diff(previous, gone) == 0, "the pile that is eaten is gone from the picture");
}

// ---- A fire wall of the file is a tile id (134): drawn from the cells, and gone from the screen when a fire ant has put it out ------------------------------------------------------------------
// The map maker writes the entry `wallup04` for the permanent fire wall it places. The renderer once made a static object of every layer-2 cell whose entry had a name, the fire wall
// included, and drew it for ever: the Fire ant's put-out cleared the cell and the fire stayed on the screen (the owner's report). A power-up and a bomb were already left to the cells.
void test_fire_wall_by_id(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[fire wall] a fire wall of the file is drawn from its cell, whatever its entry says, and leaves nothing on the screen when it is put out\n");
    assets::LevelData base = tiny();
    const int32_t g = arc.find_animation_id("g01a");
    for (auto& c : base.layer1_terrain) { c.tile_index = static_cast<uint16_t>(g); c.flags = 0; c.properties = 0; }
    if (base.tile_dictionary.size() <= static_cast<size_t>(g)) base.tile_dictionary.resize(static_cast<size_t>(g) + 1, ".");
    base.tile_dictionary[static_cast<size_t>(g)] = "g01a";
    for (auto& c : base.layer2_interactive) { c.tile_index = assets::LVL_EMPTY_TILE; c.flags = 0; c.properties = 0; }
    base.anthill_spawns.clear();
    base.food_schedules.clear();
    base.waypoints.clear();
    const int cols[3] = {3, 6, 9};
    const int row = 2;
    Image empty;
    {
        sim::Grid gn;
        check(gn.init_from_level(base), "init (no fire walls)");
        empty = picture_with_grid(r, base, gn);
    }
    Image dotted;
    for (const char* naming : {".", "wallup04"}) {
        assets::LevelData with = base;
        if (with.tile_dictionary.size() <= 134) with.tile_dictionary.resize(135, ".");
        with.tile_dictionary[134] = naming;
        for (const int c : cols) {
            auto& cell = with.layer2_interactive[static_cast<size_t>(row) * with.width + static_cast<size_t>(c)];
            cell.tile_index = 134;
            cell.flags = 1;
            cell.properties = static_cast<uint16_t>((c << 8) | row);          // the cell is its own anchor, as the map maker writes it
        }
        sim::Grid ga;
        check(ga.init_from_level(with), "init (fire walls of the file)");
        for (const int c : cols) check(ga.has_fire_at(sim::TileCoord{c, row}), "the fire wall of the file is in the grid");
        Image alive = picture_with_grid(r, with, ga);
        for (const int c : cols) check(crop_cells(alive, c, row, 1).px != crop_cells(empty, c, row, 1).px, std::string("the fire wall at column ") + std::to_string(c) + " is drawn (entry " + naming + ")");
        // the same pixels whatever the entry is called
        if (std::string(naming) == ".") dotted = alive;
        else check(image_diff(dotted, alive) == 0, "a fire wall named wallup04 looks as one named \".\" (" + std::to_string(image_diff(dotted, alive)) + " pixels differ)");
        // put out one by one: each leaves the picture of its cell as it was without the fire, nothing of it stays
        for (size_t k = 0; k < 3; ++k) {
            ga.clear_firewall(static_cast<uint32_t>(cols[k]), static_cast<uint32_t>(row));
            Image now = picture_with_grid(r, with, ga);
            for (size_t j = 0; j < 3; ++j) {
                const bool out = j <= k;
                const bool same_as_empty = crop_cells(now, cols[j], row, 1).px == crop_cells(empty, cols[j], row, 1).px;
                check(same_as_empty == out, std::string("entry ") + naming + ": after " + std::to_string(k + 1) + " put out, the cell at column " + std::to_string(cols[j]) + (out ? " is bare (no ghost)" : " still burns"));
            }
        }
        Image all_out = picture_with_grid(r, with, ga);
        check(image_diff(all_out, empty) == 0, std::string("entry ") + naming + ": every fire wall put out, the picture is the bare one (" + std::to_string(image_diff(all_out, empty)) + " pixels differ)");
    }
    // the same through the engine: a Fire ant puts a fire wall of the file out and the world picture is the bare one
    assets::LevelData level = base;
    level.tile_dictionary.resize(std::max<size_t>(level.tile_dictionary.size(), 135), ".");
    level.tile_dictionary[134] = "wallup04";
    {
        auto& cell = level.layer2_interactive[static_cast<size_t>(row) * level.width + 6];
        cell.tile_index = 134;
        cell.flags = 1;
        cell.properties = static_cast<uint16_t>((6 << 8) | row);
    }
    level.anthill_spawns.clear();
    sim::SimulationEngine eng;
    eng.init(level, 7, 0x03);
    check(eng.has_fire_at(sim::TileCoord{6, row}), "engine: the fire wall of the file burns");
    const uint32_t ant = eng.spawn_unit(0, sim::AntType::Fire, sim::TileCoord{6, row - 1});
    sim::AntOrder o{};
    o.ant_id = ant;
    o.type = sim::OrderType::IgniteFire;
    o.target_x = 6;
    o.target_y = row;
    eng.issue_order(o);
    for (int t = 0; t < 100 && eng.has_fire_at(sim::TileCoord{6, row}); ++t) eng.tick();
    check(!eng.has_fire_at(sim::TileCoord{6, row}), "engine: the Fire ant put the fire wall of the file out");
    Image after = picture_with_grid(r, level, eng.grid());
    assets::LevelData bare = base;
    sim::Grid gb;
    check(gb.init_from_level(bare), "init (bare)");
    Image bare_pic = picture_with_grid(r, base, gb);
    check(crop_cells(after, 6, row, 1).px == crop_cells(bare_pic, 6, row, 1).px, "engine: nothing of the fire is left on the screen after the put-out");
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
    test_food_without_file_cell(r);
    test_food_cell_and_stages(r);
    test_fire_wall_by_id(r, arc);

    r.shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::printf("\nloader fidelity (render): %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
