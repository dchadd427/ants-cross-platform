// The grass and the plants of the world art (include/ants_app/world_art.hpp, src/ants_app/renderer_world_art.cpp; docs/WORLD_ART.md, "Grass and plants"): their new pictures replace the original's sprites by
// number, on a page of sheets of their own, so that a map without plants never makes that page's textures.
//   [set]        every sprite that the animations of the grass, the clovers, the flowers, the leaves, the sticks, the mushroom, the plant and the ivy use has a picture on the second page, of the original
//                sprite's size, and the page holds no other sprite
//   [draw]       the real renderer's plant is the page's picture in the original's place, at 1x, 2x and 3x (its opaque pixels are the picture's, its clear ones show the ground), and nothing outside the
//                plants' own rectangles changes against the ground alone
//   [pages]      a frame without a plant makes no texture of the second page; a plant makes the one of the output's scale; a device that cannot take the 3x sheet of the page gives the 2x one, and a page
//                whose 2x sheet is cut gives the 1x one, while the ground keeps its own sheets
//   [classic]    without the art, the plants are the original's, byte for byte; the picture of a whole map (a thumbnail) is the original's whatever the setting is
//   [animation]  the animated flower is drawn from the page in every one of its seven frames, which differ from one another
// Usage: test_world_art_plants. Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/png_decode.hpp"
#include "ants_app/world_art.hpp"

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

std::string pack_folder() { return std::string(ANTS_SOURCE_DIR) + "/assets/world_art"; }

constexpr int32_t kW = 960;
constexpr int32_t kH = 540;
constexpr int32_t kPlantPage = 1;

/// The animations of the grass and the plants, by the names of the original's table
const std::vector<std::string> kPlantNames = {"grass1", "grass2", "grass3", "grass4", "grassmed1", "grassmed2", "grassmed3", "grassmed4", "grassbig1", "grassbig2", "clover1", "clover2", "clover3", "clover4",
                                              "clovers", "plant1", "flower1", "flower1a", "flower1b", "flower2a", "flower2b", "flower3a", "flower3b", "flower3c", "dflower1", "leaf1", "leaf2", "leaf3",
                                              "leaf4", "stick1", "stick2a", "mush2a", "ivyrt", "ivylft", "ivyfront"};

struct Plants {
    std::set<uint32_t> anims;                                    // the animations' ids
    std::set<uint32_t> sprites;                                  // the sprites that they use
};

Plants plants_of(const assets::AssetArchive& arc) {
    Plants out;
    for (uint32_t id = 0; id < arc.animation_count(); ++id) {
        const auto& anim = arc.get_animation(id);
        if (std::find(kPlantNames.begin(), kPlantNames.end(), anim.name) == kPlantNames.end()) continue;
        out.anims.insert(id);
        for (const auto& sub : anim.subitems) {
            for (const auto& f : sub.frames) out.sprites.insert(f.sprite_index);
        }
    }
    return out;
}

struct Box {
    int32_t x0, y0, x1, y1;                                      // [x0, x1) x [y0, y1)
};

bool overlap(const Box& a, const Box& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

/// One plant of a map: its animation and the world point that the animation is drawn from
struct Plant {
    uint32_t anim;
    int32_t wx, wy;
    Box box;                                                     // everything that it can draw, in world pixels (every frame of the animation)
};

Box animation_box(const assets::AssetArchive& arc, uint32_t anim, int32_t wx, int32_t wy) {
    Box b{1 << 30, 1 << 30, -(1 << 30), -(1 << 30)};
    for (const auto& sub : arc.get_animation(anim).subitems) {
        for (const auto& f : sub.frames) {
            const auto& sp = arc.get_sprite(f.sprite_index);
            b.x0 = std::min(b.x0, wx + f.dx);
            b.y0 = std::min(b.y0, wy + f.dy);
            b.x1 = std::max(b.x1, wx + f.dx + static_cast<int32_t>(sp.width));
            b.y1 = std::max(b.y1, wy + f.dy + static_cast<int32_t>(sp.height));
        }
    }
    return b;
}

/// Every plant of the map: the ones of its object list (drawn from the middle of their cell) and the grass that stands on a cell of the second layer (drawn from the cell's corner)
std::vector<Plant> plants_of_map(const assets::AssetArchive& arc, const assets::LevelData& level, const Plants& plants) {
    std::vector<Plant> out;
    for (const auto& item : level.anthill_spawns) {
        if (item.team_id != 255 || plants.anims.count(item.tile_id) == 0) continue;
        const int32_t wx = static_cast<int32_t>(item.x) * 32 + 16, wy = static_cast<int32_t>(item.y) * 32 + 16;
        out.push_back(Plant{item.tile_id, wx, wy, animation_box(arc, item.tile_id, wx, wy)});
    }
    for (uint32_t y = 0; y < level.height(); ++y) {
        for (uint32_t x = 0; x < level.width(); ++x) {
            const uint16_t id = level.get_cell_layer2(x, y).tile_index;
            if (id == assets::LVL_EMPTY_TILE || plants.anims.count(id) == 0) continue;
            const int32_t wx = static_cast<int32_t>(x) * 32, wy = static_cast<int32_t>(y) * 32;
            out.push_back(Plant{id, wx, wy, animation_box(arc, id, wx, wy)});
        }
    }
    return out;
}

bool same(const zoomtest::Picture& a, const zoomtest::Picture& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }

/// A copy of the shipped pack in a folder of its own whose sheet `file` is cut after its header (the first 80 bytes stay: the header is right, the body is not)
std::string pack_copy_cut(const std::string& tag, const std::string& file) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ants_world_art_plants_cut_" + tag + "_" + std::to_string(static_cast<long long>(SDL_GetTicks())));
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir, ignored);
    for (const auto& entry : fs::directory_iterator(pack_folder())) {
        const std::string name = entry.path().filename().string();
        if (name != file) {
            fs::copy_file(entry.path(), dir / name, fs::copy_options::overwrite_existing, ignored);
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        std::vector<char> head(80);
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        out.write(head.data(), in.gcount());
    }
    return dir.string();
}

/// The first rectangle of 15 x 9 cells of the map in which `want` holds of the plants that lie in it (their boxes meet the window), or none
bool find_window(zoomtest::PixelRig& rig, const std::vector<Plant>& all, int32_t& wx, int32_t& wy, bool want_plants) {
    const int32_t cols = 15, rows = 9;
    for (int32_t r = 0; r + rows <= static_cast<int32_t>(rig.tiles_h); r += 3) {
        for (int32_t c = 0; c + cols <= static_cast<int32_t>(rig.tiles_w); c += 3) {
            const Box window{c * 32 - 160, r * 32 - 160, (c + cols) * 32 + 160, (r + rows) * 32 + 160};            // (a plant's picture reaches past its cell)
            bool any = false;
            for (const Plant& p : all) any = any || overlap(p.box, window);
            if (any != want_plants) continue;
            wx = c * 32;
            wy = r * 32;
            return true;
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [set]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static void test_set(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("set", "every sprite of the grass and the plants has a picture on the second page, and no other sprite is there");
    const Plants plants = plants_of(arc);
    check(plants.anims.size() == kPlantNames.size(), "every animation of the list is in the original's table: " + std::to_string(plants.anims.size()) + " of " + std::to_string(kPlantNames.size()));
    size_t lacking = 0, unfit = 0, wrong_page = 0;
    for (const uint32_t s : plants.sprites) {
        const world_art::Picture* p = pack.find(s, 0);
        if (p == nullptr) {
            ++lacking;
            continue;
        }
        wrong_page += p->page != kPlantPage ? 1u : 0u;
        const auto& sp = arc.get_sprite(s);
        unfit += (p->w != static_cast<int32_t>(sp.width) || p->h != static_cast<int32_t>(sp.height)) ? 1u : 0u;
    }
    check(plants.sprites.size() == 46 && lacking == 0 && unfit == 0 && wrong_page == 0, "all " + std::to_string(plants.sprites.size()) + " sprites of the plants have a picture of the original sprite's size on the second page (lacking " + std::to_string(lacking) + ", unfit " + std::to_string(unfit) + ", on another page " + std::to_string(wrong_page) + ")");
    std::set<uint32_t> on_page;
    for (const world_art::Picture& p : pack.pictures()) {
        if (p.page == kPlantPage) on_page.insert(p.number);
    }
    check(on_page == plants.sprites, "the second page holds exactly those sprites: " + std::to_string(on_page.size()));
    check(pack.page_count() == 2 && pack.sheet_height(kPlantPage, 3) > 0 && pack.sheet_height(kPlantPage, 3) <= 4096 && pack.sheet_width(kPlantPage, 3) <= 4096, "the page has its own three sheets, none above 4096");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [draw]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// A plant of the object list put on a map before the renderer reads it: in the middle of a stretch of ground whose second layer holds nothing within five cells, so that no other object is drawn over it.
/// The world point that the animation is drawn from (the middle of the cell) is `wx`, `wy`.
static bool add_plant(zoomtest::PixelRig& rig, uint16_t tile_id, int32_t& wx, int32_t& wy) {
    const int32_t w = static_cast<int32_t>(rig.tiles_w), h = static_cast<int32_t>(rig.tiles_h);
    for (int32_t r = 6; r + 6 < h; ++r) {
        for (int32_t c = 6; c + 6 < w; ++c) {
            bool free_of_objects = true;
            for (int32_t j = -5; j <= 5 && free_of_objects; ++j) {
                for (int32_t i = -5; i <= 5; ++i) free_of_objects = free_of_objects && rig.level.get_cell_layer2(static_cast<uint32_t>(c + i), static_cast<uint32_t>(r + j)).tile_index == assets::LVL_EMPTY_TILE;
            }
            if (!free_of_objects) continue;
            assets::AnthillSpawn spawn;
            spawn.tile_id = tile_id;
            spawn.x = static_cast<uint16_t>(c);
            spawn.y = static_cast<uint16_t>(r);
            spawn.team_id = 255;
            rig.level.anthill_spawns.push_back(spawn);
            rig.renderer.set_level(rig.level);
            wx = c * 32 + 16;
            wy = r * 32 + 16;
            return true;
        }
    }
    return false;
}

/// The device pixel of a world point in a rig's picture
struct Mapping {
    explicit Mapping(zoomtest::PixelRig& rig) : view(rig.layout.view()), cam_x(static_cast<int32_t>(rig.cam().x)), cam_y(static_cast<int32_t>(rig.cam().y)), os(rig.os) {}
    int32_t x(int32_t world_x) const { return (view.x + world_x - cam_x) * os; }
    int32_t y(int32_t world_y) const { return (view.y + world_y - cam_y) * os; }
    LayoutRect view;
    int32_t cam_x, cam_y, os;
};

static void test_draw(const assets::AssetArchive& arc, const world_art::Pack& pack, const world_art::Pack& ground) {
    group("draw", "the real renderer's plant is the picture of the page in the original's place, and nothing else changes");
    const Plants plants = plants_of(arc);
    DecodedImage sheets[3];
    for (const int32_t s : world_art::kScales) {
        if (!pack.sheet_pixels(kPlantPage, s, sheets[s - 1])) {
            check(false, "the second page's sheet of scale " + std::to_string(s) + " is read");
            return;
        }
    }
    // one clover on TINY, alone: its picture is the frame's where it is opaque, and the ground's where it is clear
    constexpr uint16_t kClover = 405;                                                          // clover2: one sprite (574)
    for (const int32_t os : {1, 2, 3}) {
        zoomtest::PixelRig rig(arc, "TINY", kW, kH, 0, os);
        const std::string at = "a clover on TINY at " + std::to_string(os) + "x: ";
        int32_t wx = 0, wy = 0;
        if (!rig.ok || !add_plant(rig, kClover, wx, wy)) {
            check(false, at + "the scene is set up");
            continue;
        }
        rig.renderer.set_dense_mud(false);
        rig.renderer.set_transitions(false);
        rig.look(static_cast<double>(wx) - 240, static_cast<double>(wy) - 150);
        const auto& anim = arc.get_animation(kClover);
        const auto& frame = anim.subitems[0].frames[0];
        const world_art::Picture* pic = pack.find(frame.sprite_index, 0);
        if (anim.subitems.size() != 1 || anim.subitems[0].frames.size() != 1 || pic == nullptr) {
            check(false, at + "the clover is one sprite with a picture");
            continue;
        }
        rig.renderer.set_world_art(&ground);
        const zoomtest::Picture without = rig.shoot();
        rig.renderer.set_world_art(&pack);
        const zoomtest::Picture with = rig.shoot();
        rig.level.anthill_spawns.pop_back();                                                   // the same view without the clover: the ground that its clear pixels show
        rig.renderer.set_level(rig.level);
        const zoomtest::Picture bare = rig.shoot();
        const Mapping map(rig);
        const int32_t ox = map.x(wx + frame.dx), oy = map.y(wy + frame.dy);
        const size_t k = static_cast<size_t>(os - 1);
        size_t opaque = 0, opaque_same = 0, clear = 0, clear_ground = 0;
        for (int32_t y = 0; y < pic->h * os; ++y) {
            for (int32_t x = 0; x < pic->w * os; ++x) {
                const uint8_t* want = &sheets[k].rgba[(static_cast<size_t>(pic->y[k] + y) * static_cast<size_t>(sheets[k].width) + static_cast<size_t>(pic->x[k] + x)) * 4u];
                const int32_t fx = ox + x, fy = oy + y;
                if (fx < 0 || fy < 0 || fx >= with.w || fy >= with.h) continue;
                if (want[3] == 255) {
                    ++opaque;
                    opaque_same += std::memcmp(with.at(fx, fy), want, 3) == 0 ? 1u : 0u;
                } else if (want[3] == 0) {
                    ++clear;
                    clear_ground += std::memcmp(with.at(fx, fy), bare.at(fx, fy), 3) == 0 ? 1u : 0u;
                }
            }
        }
        check(opaque > 100 && opaque_same == opaque, at + "the opaque pixels of the picture are the frame's: " + std::to_string(opaque_same) + " of " + std::to_string(opaque));
        check(clear > 100 && clear_ground == clear, at + "its clear pixels show the ground (the view without the clover): " + std::to_string(clear_ground) + " of " + std::to_string(clear));
        size_t outside = 0;
        for (int32_t y = 0; y < with.h; ++y) {
            for (int32_t x = 0; x < with.w; ++x) {
                const bool inside = x >= ox && x < ox + pic->w * os && y >= oy && y < oy + pic->h * os;
                if (!inside && std::memcmp(with.at(x, y), without.at(x, y), 3) != 0) ++outside;
            }
        }
        check(outside == 0, at + "nothing outside the picture's rectangle changes against the ground alone: " + std::to_string(outside));
    }

    // a busy view of ISLANDS (grass, clovers, flowers): the frame differs from the ground alone only inside the plants' own rectangles, and in some of them
    zoomtest::PixelRig probe(arc, "ISLANDS", kW, kH, 0, 1);
    const std::vector<Plant> all = plants_of_map(arc, probe.level, plants);
    int32_t px = 0, py = 0;
    if (!find_window(probe, all, px, py, true)) {
        check(false, "a window with plants on ISLANDS");
        return;
    }
    for (const int32_t os : {1, 2, 3}) {
        zoomtest::PixelRig rig(arc, "ISLANDS", kW, kH, 0, os);
        const std::string at = "ISLANDS at " + std::to_string(os) + "x: ";
        rig.renderer.set_dense_mud(false);
        rig.renderer.set_transitions(false);
        rig.look(px, py);
        rig.renderer.set_world_art(&ground);
        const zoomtest::Picture without = rig.shoot();
        rig.renderer.set_world_art(&pack);
        const zoomtest::Picture with = rig.shoot();
        const Mapping map(rig);
        std::vector<Box> boxes;
        for (const Plant& p : all) boxes.push_back(Box{map.x(p.box.x0), map.y(p.box.y0), map.x(p.box.x1), map.y(p.box.y1)});
        size_t inside = 0, outside = 0;
        for (int32_t y = 0; y < with.h; ++y) {
            for (int32_t x = 0; x < with.w; ++x) {
                if (std::memcmp(with.at(x, y), without.at(x, y), 3) == 0) continue;
                bool in_a_plant = false;
                for (const Box& b : boxes) in_a_plant = in_a_plant || (x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1);
                (in_a_plant ? inside : outside) += 1;
            }
        }
        check(inside > 1000 && outside == 0, at + "the plants change the picture only inside their own rectangles: " + std::to_string(inside) + " pixels inside, " + std::to_string(outside) + " outside");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [pages]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t kPlantSheets = 7u << 3;                      // the bits of world_art_sheets() that are the second page's: 1x, 2x and 3x

static void test_pages(const assets::AssetArchive& arc, const world_art::Pack& pack, const world_art::Pack& ground) {
    group("pages", "the second page's textures are made only for a plant in view, at the output's scale, with the fallbacks of the first page");
    const Plants plants = plants_of(arc);
    zoomtest::PixelRig probe(arc, "ISLANDS", kW, kH, 0, 1);
    if (!probe.ok) {
        check(false, "the rig is up");
        return;
    }
    const std::vector<Plant> all = plants_of_map(arc, probe.level, plants);
    int32_t px = 0, py = 0, bx = 0, by = 0;
    const bool with_plants = find_window(probe, all, px, py, true);
    const bool without_plants = find_window(probe, all, bx, by, false);
    check(with_plants && !all.empty(), "ISLANDS has plants: " + std::to_string(all.size()));
    if (!with_plants) return;
    {
        zoomtest::PixelRig rig(arc, "ISLANDS", kW, kH, 0, 3);
        rig.renderer.set_dense_mud(false);
        rig.renderer.set_transitions(false);
        rig.renderer.set_world_art(&pack);
        if (without_plants) {
            rig.look(bx, by);
            (void)rig.shoot();
            check((rig.renderer.world_art_sheets() & kPlantSheets) == 0 && rig.renderer.world_art_sheets() != 0, "a view without a plant makes the ground's texture and none of the plants'");
        }
        rig.look(px, py);
        const zoomtest::Picture three = rig.shoot();
        check(rig.renderer.world_art_sheets() == ((1u << 2) | (1u << (3 + 2))), "a view with a plant, at 3x, makes the 3x sheet of each page and no other");
        rig.renderer.set_world_art(&ground);
        const zoomtest::Picture ground_only = rig.shoot();
        check(!same(three, ground_only), "...and the plants are what differs from the ground alone");
    }
    {
        zoomtest::PixelRig rig(arc, "ISLANDS", kW, kH, 0, 3);
        rig.renderer.set_dense_mud(false);
        rig.renderer.set_transitions(false);
        rig.renderer.set_world_art(&pack);
        rig.renderer.set_art_texture_limit(2600);                // the 3x sheets (3072 wide) are too wide for this device, the 2x ones (2048) are not
        rig.look(px, py);
        const zoomtest::Picture two = rig.shoot();
        check(rig.renderer.world_art_sheets() == ((1u << 1) | (1u << (3 + 1))), "a device that takes no texture of 3072 gets the 2x sheet of each page at the 3x output");
        rig.renderer.set_world_art(&ground);
        check(!same(two, rig.shoot()), "...and the plants are drawn");
    }
    {
        const world_art::Pack cut_pack = [&]() {
            world_art::Pack p;
            p.load(pack_copy_cut("cut_plants_2x", "plants_2x.png"));
            return p;
        }();
        check(cut_pack.loaded(), "a pack whose second page has a 2x sheet that is cut after its header is read");
        zoomtest::PixelRig rig(arc, "ISLANDS", kW, kH, 0, 2);
        rig.renderer.set_dense_mud(false);
        rig.renderer.set_transitions(false);
        rig.renderer.set_world_art(&cut_pack);
        rig.look(px, py);
        const zoomtest::QuietStdout quiet;
        const zoomtest::Picture drawn = rig.shoot();
        check(rig.renderer.world_art_sheets() == ((1u << 1) | (1u << 3)), "a 2x output draws the plants from the page's 1x sheet and the ground from its own 2x sheet");
        rig.renderer.set_world_art(&ground);
        check(!same(drawn, rig.shoot()), "...and the plants are still the new ones, not the original's");
        (void)rig.shoot();
        rig.renderer.set_world_art(&cut_pack);
        check(same(rig.shoot(), drawn), "...and again the same (a sheet that failed is not read at every frame)");
    }
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path(), ignored)) {
        if (entry.path().filename().string().rfind("ants_world_art_plants_cut_", 0) == 0) std::filesystem::remove_all(entry.path(), ignored);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [classic]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static void test_classic(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("classic", "without the art the plants are the original's, and a map's picture is the original's whatever the setting is");
    const Plants plants = plants_of(arc);
    zoomtest::PixelRig rig(arc, "ISLANDS", kW, kH, 0, 2);
    const std::vector<Plant> all = plants_of_map(arc, rig.level, plants);
    int32_t px = 0, py = 0;
    if (!find_window(rig, all, px, py, true)) {
        check(false, "a window with plants");
        return;
    }
    rig.look(px, py);
    const zoomtest::Picture classic = rig.shoot();
    rig.renderer.set_world_art(&pack);
    const zoomtest::Picture art = rig.shoot();
    check(!same(classic, art), "the art changes the picture of a view with plants");
    rig.renderer.set_world_art(nullptr);
    check(same(rig.shoot(), classic), "taking the art away gives the original's frame again, byte for byte");
    WorldImage plain_image, art_image;
    std::string why;
    check(rig.renderer.render_world_image(rig.level, rig.world, rig.engine.grid(), plain_image, &why), "the picture of the whole map without the art: " + why);
    rig.renderer.set_world_art(&pack);
    check(rig.renderer.render_world_image(rig.level, rig.world, rig.engine.grid(), art_image, &why), "...and with the art set: " + why);
    check(plain_image.width == art_image.width && plain_image.rgba == art_image.rgba, "the picture of a map is the same with the art set");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [animation]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static void test_animation(const assets::AssetArchive& arc, const world_art::Pack& pack, const world_art::Pack& ground) {
    group("animation", "the animated flower is drawn from the page in each of its seven frames, which differ");
    constexpr uint16_t kFlower = 420;                                                          // dflower1
    check(arc.get_animation(kFlower).name == "dflower1" && arc.get_animation(kFlower).subitems.size() == 7, "the animated flower has seven frames in the original's table");
    zoomtest::PixelRig rig(arc, "TINY", kW, kH, 0, 2);
    int32_t wx = 0, wy = 0;
    if (!rig.ok || !add_plant(rig, kFlower, wx, wy)) {
        check(false, "the flower is put on TINY");
        return;
    }
    rig.renderer.set_dense_mud(false);
    rig.renderer.set_transitions(false);
    rig.look(static_cast<double>(wx) - 240, static_cast<double>(wy) - 150);
    std::vector<zoomtest::Picture> frames, grounds;
    for (size_t i = 0; i < 7; ++i) {
        rig.renderer.pin_animation_clock(100u * static_cast<uint32_t>(i) + 10u);               // (seven frames of 100 ms each)
        rig.renderer.set_world_art(&pack);
        frames.push_back(rig.shoot());
        rig.renderer.set_world_art(&ground);
        grounds.push_back(rig.shoot());
    }
    size_t different_frames = 0, different_from_ground = 0;
    for (size_t i = 0; i < 7; ++i) {
        different_from_ground += same(frames[i], grounds[i]) ? 0u : 1u;
        for (size_t j = i + 1; j < 7; ++j) different_frames += same(frames[i], frames[j]) ? 0u : 1u;
    }
    check(different_from_ground == 7, "every frame of the flower differs from the ground alone: " + std::to_string(different_from_ground) + " of 7");
    check(different_frames >= 18, "...and the seven frames differ from one another: " + std::to_string(different_frames) + " of 21 pairs");
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;                                                  // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    assets::AssetArchive arc;
    world_art::Pack pack;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd") || !pack.load(pack_folder())) {
        std::fprintf(stderr, "cannot open ants.chd or the world art: %s\n", pack.what().c_str());
        return 2;
    }
    // the first page alone: the ground's pictures without the plants' (in a folder of its own that stays until the end: the sheets are decoded when a texture needs them)
    world_art::Pack ground;
    std::string ground_folder;
    {
        std::ifstream in(pack_folder() + "/world_art.txt", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        std::istringstream lines(text.str());
        std::string line, kept;
        while (std::getline(lines, line)) {
            std::istringstream words(line);
            std::string kind, first, second, third;
            words >> kind >> first >> second >> third;
            if ((kind == "sheet" && first != "0") || (kind == "sprite" && third != "0")) continue;
            kept += line + "\n";
        }
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / ("ants_world_art_plants_ground_" + std::to_string(static_cast<long long>(SDL_GetTicks())));
        std::error_code ignored;
        fs::create_directories(dir, ignored);
        {
            std::ofstream out(dir / "world_art.txt", std::ios::binary | std::ios::trunc);
            out << kept;
        }
        for (const char* name : {"sheet_1x.png", "sheet_2x.png", "sheet_3x.png"}) fs::copy_file(pack_folder() + "/" + name, dir / name, fs::copy_options::overwrite_existing, ignored);
        ground_folder = dir.string();
        if (!ground.load(ground_folder)) {
            std::fprintf(stderr, "cannot read the first page alone: %s\n", ground.what().c_str());
            return 2;
        }
    }
    test_set(arc, pack);
    test_draw(arc, pack, ground);
    test_pages(arc, pack, ground);
    test_classic(arc, pack);
    test_animation(arc, pack, ground);
    std::error_code removed;
    std::filesystem::remove_all(ground_folder, removed);
    std::printf("\nworld art plants: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
