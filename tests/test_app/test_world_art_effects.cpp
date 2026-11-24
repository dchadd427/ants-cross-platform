// The fire wall, the bombs and the explosion of the world art (include/ants_app/renderer.hpp, src/ants_app/renderer_world_effects.cpp, renderer_terrain.cpp; docs/WORLD_ART.md, "Fire, bombs and the explosion"):
// the fire and the explosion are light (see-through, added, over a warm glow on the ground), the bombs are new pictures of the original's sprites.
//   [fire]       a wall's light lies in the y-sorted list at the bottom of its cell: an ant in the cell is seen through it, an ant south of it covers it; a glow warms the ground and never darkens it; Fog of War still
//                hides a wall that it hid; Classic draws the original's wall
//   [bomb]       the four teams' bombs are new pictures in the original's place and size, all different, and Fog of War still hides them
//   [explosion]  the ten frames are the original's place and times, with a glow on the ground that never darkens it, and Fog of War still hides them
// Usage: test_world_art_effects. Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "zoom_scene.hpp"

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
constexpr int32_t kCol = 23;                                    // a cell of gravel on ISLANDS with room round it
constexpr int32_t kRow = 26;

sim::AntSnapshot standing_ant(const assets::AssetArchive& arc, uint32_t id, uint8_t player, int type, int32_t px, int32_t py) {
    sim::AntSnapshot a;
    a.id = id;
    a.player_id = player;
    a.type = static_cast<sim::AntType>(type);
    a.px = px;
    a.py = py;
    a.tile_x = px / 32;
    a.tile_y = py / 32;
    a.hp = 10;
    a.max_hp = 10;
    a.state = sim::UnitState::Idle;
    const int32_t clip = zoomtest::idle_clip(arc, type, 3);
    a.loco_clip = clip < 0 ? uint16_t{0x7FFE} : static_cast<uint16_t>(clip);
    a.loco_frame = 0;
    a.loco_mirrored = false;
    a.loco_left_ms = 100;
    return a;
}

/// A rig on ISLANDS with the world art (and nothing else of it: no dense mud, no transitions) and the camera round the cell
struct Rig {
    Rig(const assets::AssetArchive& archive, const world_art::Pack& pack, int32_t os = 1) : rig(archive, "ISLANDS", kW, kH, 0, os), arc(archive), scale(os) {
        rig.renderer.set_world_art(&pack);
        rig.renderer.set_dense_mud(false);
        rig.renderer.set_transitions(false);
        rig.renderer.set_dense_mud_budget_ms(-1);
        rig.look(kCol * 32 - 120, kRow * 32 - 100);
        view = rig.layout.view();
    }
    /// The picture with what the world now holds: `ants` as given, `effects` as given, the fog as set
    zoomtest::Picture frame() {
        rig.world = rig.engine.get_world_state();
        rig.world.ants = ants;
        rig.world.effects = effects;
        rig.world.fog_of_war_enabled = fog;
        if (fog) {
            rig.world.fog_revealed.assign(static_cast<size_t>(rig.world.width) * rig.world.height, 0);
            for (const auto& cell : revealed) rig.world.fog_revealed[static_cast<size_t>(cell.second) * rig.world.width + static_cast<size_t>(cell.first)] = 1;
        }
        return rig.shoot();
    }
    /// The device pixel of a world point
    int32_t px(int32_t world_x) { return (view.x + world_x - static_cast<int32_t>(rig.cam().x)) * scale; }
    int32_t py(int32_t world_y) { return (view.y + world_y - static_cast<int32_t>(rig.cam().y)) * scale; }
    zoomtest::PixelRig rig;
    const assets::AssetArchive& arc;
    int32_t scale;
    LayoutRect view{};
    std::vector<sim::AntSnapshot> ants;
    std::vector<sim::VisualEffect> effects;
    bool fog{false};
    std::vector<std::pair<int32_t, int32_t>> revealed;
};

bool same(const zoomtest::Picture& a, const zoomtest::Picture& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }
bool pixel_same(const zoomtest::Picture& a, const zoomtest::Picture& b, int32_t x, int32_t y) { return std::memcmp(a.at(x, y), b.at(x, y), 3) == 0; }
int distance(const uint8_t* a, const uint8_t* b) { return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]); }

struct Box {
    int32_t x0, y0, x1, y1;                                      // [x0, x1) x [y0, y1) in device pixels
};

/// The bounding box of the pixels that two pictures do not share (x1 < x0 when there are none), and how many
Box changed_box(const zoomtest::Picture& a, const zoomtest::Picture& b, size_t* count = nullptr) {
    Box box{a.w, a.h, -1, -1};
    size_t n = 0;
    for (int32_t y = 0; y < a.h; ++y) {
        for (int32_t x = 0; x < a.w; ++x) {
            if (pixel_same(a, b, x, y)) continue;
            ++n;
            box.x0 = std::min(box.x0, x);
            box.y0 = std::min(box.y0, y);
            box.x1 = std::max(box.x1, x + 1);
            box.y1 = std::max(box.y1, y + 1);
        }
    }
    if (count != nullptr) *count = n;
    return box;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [fire]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void test_fire(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("fire", "the fire wall as light");
    const int32_t x = kCol * 32, y = kRow * 32;
    Rig base(arc, pack);
    if (!base.rig.ok) {
        check(false, "the rig is up");
        return;
    }
    const zoomtest::Picture ground = base.frame();
    base.rig.engine.grid_mut().place_firewall(static_cast<uint32_t>(kCol), static_cast<uint32_t>(kRow), 0);
    const zoomtest::Picture wall = base.frame();
    size_t changed = 0;
    const Box box = changed_box(ground, wall, &changed);
    check(changed > 300, "a wall changes the picture: " + std::to_string(changed) + " pixels");
    // everything it changes lies in its glow (64 x 40 round (16, 25) of the cell) and its sprites (the cell and a pixel or two more)
    check(box.x0 >= base.px(x - 16) && box.x1 <= base.px(x + 48) && box.y0 >= base.py(y - 2) && box.y1 <= base.py(y + 46), "...all of it inside the glow and the sprites' box: x " + std::to_string(box.x0) + " .. " + std::to_string(box.x1) + ", y " + std::to_string(box.y0) + " .. " + std::to_string(box.y1));
    // the glow warms the ground and never darkens it: left and right of the cell, outside the sprites, every pixel is the ground's or lighter, and many are lighter
    size_t lighter = 0, darker = 0;
    for (int32_t py = base.py(y + 6); py < base.py(y + 44); ++py) {
        for (int32_t px = base.px(x - 15); px < base.px(x - 1); ++px) {
            const uint8_t* g = ground.at(px, py);
            const uint8_t* w = wall.at(px, py);
            for (int c = 0; c < 3; ++c) darker += w[c] < g[c] ? 1u : 0u;
            lighter += distance(g, w) > 0 ? 1u : 0u;
        }
    }
    check(lighter > 100 && darker == 0, "a glow warms the ground beside the wall and never darkens it: " + std::to_string(lighter) + " pixels lighter, " + std::to_string(darker) + " channels darker");

    // an ant in the wall's cell is seen through the flames (they lie over it, by their own opacity); one south of the cell covers the flames
    {
        Rig r(arc, pack);
        r.rig.engine.grid_mut().place_firewall(static_cast<uint32_t>(kCol), static_cast<uint32_t>(kRow), 0);
        r.ants = {standing_ant(arc, 9001, 0, 2, x + 16, y + 24)};
        const zoomtest::Picture with_both = r.frame();
        Rig ant_only(arc, pack);
        ant_only.ants = r.ants;
        const zoomtest::Picture ant = ant_only.frame();
        Rig fire_only(arc, pack);
        fire_only.rig.engine.grid_mut().place_firewall(static_cast<uint32_t>(kCol), static_cast<uint32_t>(kRow), 0);
        const zoomtest::Picture flame = fire_only.frame();
        size_t under = 0, over = 0, through = 0;
        for (int32_t py = 0; py < ground.h; ++py) {
            for (int32_t px = 0; px < ground.w; ++px) {
                if (pixel_same(ant, ground, px, py) || distance(flame.at(px, py), ground.at(px, py)) < 40) continue;      // an ant's pixel under a strong flame
                ++under;
                if (!pixel_same(with_both, ant, px, py)) ++over;
                if (distance(with_both.at(px, py), ant.at(px, py)) < distance(with_both.at(px, py), flame.at(px, py))) ++through;
            }
        }
        check(under > 60, "an ant in the cell has " + std::to_string(under) + " pixels under strong flames");
        check(over * 10 >= under * 7, "the flames lie over the ant (" + std::to_string(over) + " of " + std::to_string(under) + " pixels changed by them)");
        check(through * 5 >= under, "...and the ant is seen through them: " + std::to_string(through) + " of " + std::to_string(under) + " pixels are nearer the ant's color than the flame's");

        Rig south(arc, pack);
        south.rig.engine.grid_mut().place_firewall(static_cast<uint32_t>(kCol), static_cast<uint32_t>(kRow), 0);
        south.ants = {standing_ant(arc, 9002, 0, 0, x + 16, y + 32 + 10)};
        const zoomtest::Picture covered = south.frame();
        Rig south_only(arc, pack);
        south_only.ants = south.ants;
        const zoomtest::Picture ant_south = south_only.frame();
        size_t ant_pixels = 0, kept = 0;
        for (int32_t py = 0; py < ground.h; ++py) {
            for (int32_t px = 0; px < ground.w; ++px) {
                if (pixel_same(ant_south, ground, px, py)) continue;
                ++ant_pixels;
                kept += pixel_same(covered, ant_south, px, py) ? 1u : 0u;
            }
        }
        check(ant_pixels > 100 && kept == ant_pixels, "an ant south of the cell covers the wall: all " + std::to_string(ant_pixels) + " of its pixels are as they are without the wall (" + std::to_string(kept) + ")");
        check(!same(covered, ant_south), "...and the wall is there round it");
    }

    // Fog of War hides a wall whose cell is unexplored, exactly as it did
    {
        Rig fogged(arc, pack);
        fogged.rig.engine.grid_mut().place_firewall(static_cast<uint32_t>(kCol), static_cast<uint32_t>(kRow), 0);
        fogged.fog = true;
        const zoomtest::Picture hidden = fogged.frame();
        Rig none(arc, pack);
        none.fog = true;
        check(same(hidden, none.frame()), "a wall in an unexplored cell is not drawn (the picture is the one without it)");
        fogged.revealed.emplace_back(kCol, kRow);
        none.revealed.emplace_back(kCol, kRow);
        check(!same(fogged.frame(), none.frame()), "...and when its cell is explored it is");
    }

    // Classic draws the original's wall
    {
        Rig a(arc, pack);
        a.rig.engine.grid_mut().place_firewall(static_cast<uint32_t>(kCol), static_cast<uint32_t>(kRow), 0);
        a.rig.renderer.set_world_art(nullptr);
        const zoomtest::Picture classic = a.frame();
        zoomtest::PixelRig plain(arc, "ISLANDS", kW, kH);
        plain.engine.grid_mut().place_firewall(static_cast<uint32_t>(kCol), static_cast<uint32_t>(kRow), 0);
        plain.look(kCol * 32 - 120, kRow * 32 - 100);
        plain.world = plain.engine.get_world_state();
        plain.world.ants.clear();
        check(same(classic, plain.shoot()), "with Classic the wall is the original's, the pixels of a renderer that never had the art");
        check(!same(classic, wall), "...and the new wall is not");
    }

    // at a larger output the same: made from the sheet of its scale
    for (const int32_t os : {2, 3}) {
        Rig big(arc, pack, os);
        const zoomtest::Picture g = big.frame();
        big.rig.engine.grid_mut().place_firewall(static_cast<uint32_t>(kCol), static_cast<uint32_t>(kRow), 0);
        size_t n = 0;
        changed_box(g, big.frame(), &n);
        check(n > 300 * static_cast<size_t>(os * os) / 2, "a wall at " + std::to_string(os) + "x changes " + std::to_string(n) + " pixels");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [bomb]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void test_bomb(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("bomb", "the four teams' bombs");
    Rig art(arc, pack);
    if (!art.rig.ok) {
        check(false, "the rig is up");
        return;
    }
    const zoomtest::Picture ground = art.frame();
    for (uint32_t t = 0; t < 4; ++t) art.rig.engine.grid_mut().place_bomb(static_cast<uint32_t>(kCol) + t, static_cast<uint32_t>(kRow), static_cast<uint8_t>(t));
    const zoomtest::Picture with_art = art.frame();
    std::set<std::vector<uint8_t>> pictures;
    bool inside = true;
    for (int32_t t = 0; t < 4; ++t) {
        const int32_t cx = (kCol + t) * 32, cy = kRow * 32;
        std::vector<uint8_t> mine;
        size_t count = 0;
        for (int32_t py = art.py(cy); py < art.py(cy + 32); ++py) {
            for (int32_t px = art.px(cx); px < art.px(cx + 32); ++px) {
                for (int c = 0; c < 4; ++c) mine.push_back(with_art.at(px, py)[c]);
                if (!pixel_same(with_art, ground, px, py)) {
                    ++count;
                    if (px < art.px(cx + 9) || px >= art.px(cx + 21) || py < art.py(cy + 4) || py >= art.py(cy + 28)) inside = false;       // (the original's sprite: 12 x 24 at (9, 4) of the cell)
                }
            }
        }
        pictures.insert(mine);
        check(count > 40, "the bomb of team " + std::to_string(t) + " is drawn: " + std::to_string(count) + " pixels differ from the ground");
    }
    check(inside, "...in the place and the size of the original's sprite (12 x 24 at (9, 4) of its cell): nothing else changes");
    check(pictures.size() == 4, "the four teams' bombs are four different pictures");

    // Fog of War hides a bomb in an unexplored cell
    Rig fogged(arc, pack);
    fogged.rig.engine.grid_mut().place_bomb(static_cast<uint32_t>(kCol), static_cast<uint32_t>(kRow), 1);
    fogged.fog = true;
    Rig none(arc, pack);
    none.fog = true;
    check(same(fogged.frame(), none.frame()), "a bomb in an unexplored cell is not drawn");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [explosion]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

sim::VisualEffect blast(int32_t x, int32_t y, uint32_t ms, bool fog_gated = false) {
    sim::VisualEffect e;
    e.anim_name = "bombex";
    e.px = x;
    e.py = y;
    e.elapsed_ms = ms;
    e.duration_ms = 680;
    e.frame = static_cast<uint16_t>(ms / 50u);
    e.total_frames = 14;
    e.y_key = y;
    e.fog_gated = fog_gated;
    return e;
}

void test_explosion(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("explosion", "the bomb's explosion as light");
    const int32_t x = kCol * 32, y = kRow * 32;
    Rig art(arc, pack);
    if (!art.rig.ok) {
        check(false, "the rig is up");
        return;
    }
    const zoomtest::Picture ground = art.frame();
    // the ten frames of the original's animation: the sprite of each, where it stands and how long it is shown
    const auto& seq = arc.get_animation(static_cast<uint32_t>(arc.find_animation_id("bombex")));
    check(seq.subitems.size() == 10, "the original's explosion has ten frames");
    uint32_t start = 0;
    size_t moved_frames = 0;
    for (size_t f = 0; f < seq.subitems.size(); ++f) {
        const uint32_t mid = start + seq.subitems[f].val3 / 2;
        start += seq.subitems[f].val3;
        art.effects = {blast(x, y, mid)};
        const zoomtest::Picture shot = art.frame();
        size_t n = 0;
        const Box box = changed_box(ground, shot, &n);
        const auto& part = seq.subitems[f].frames[0];
        const auto& sprite = arc.get_sprite(part.sprite_index);
        const int32_t margin = 22;                                               // the glow reaches 21 pixels beyond the picture
        const bool placed = n > 0 && box.x0 >= art.px(x + part.dx - margin) && box.x1 <= art.px(x + part.dx + static_cast<int32_t>(sprite.width) + margin) && box.y0 >= art.py(y + part.dy - margin) && box.y1 <= art.py(y + part.dy + static_cast<int32_t>(sprite.height) + margin);
        moved_frames += placed ? 1u : 0u;
    }
    check(moved_frames == 10, "every one of the ten frames changes the picture inside the original's sprite and the glow round it, at the original's place: " + std::to_string(moved_frames));
    check(start == 680, "...in the original's 680 ms");
    art.effects = {blast(x, y, 700)};
    check(same(art.frame(), ground), "after its 680 ms the explosion is gone");

    // the glow warms the ground beside the picture and never darkens it
    {
        art.effects = {blast(x, y, 250)};
        const zoomtest::Picture shot = art.frame();
        const auto& part = seq.subitems[3].frames[0];                            // 200 .. 300 ms
        const auto& sprite = arc.get_sprite(part.sprite_index);
        size_t lighter = 0, darker = 0;
        for (int32_t py = art.py(y + part.dy - 20); py < art.py(y + part.dy + static_cast<int32_t>(sprite.height) + 20); ++py) {
            for (int32_t px = art.px(x + part.dx - 20); px < art.px(x + part.dx + static_cast<int32_t>(sprite.width) + 20); ++px) {
                const bool in_picture = px >= art.px(x + part.dx) && px < art.px(x + part.dx + static_cast<int32_t>(sprite.width)) && py >= art.py(y + part.dy) && py < art.py(y + part.dy + static_cast<int32_t>(sprite.height));
                if (in_picture) continue;
                for (int c = 0; c < 3; ++c) darker += shot.at(px, py)[c] < ground.at(px, py)[c] ? 1u : 0u;
                lighter += distance(shot.at(px, py), ground.at(px, py)) > 0 ? 1u : 0u;
            }
        }
        check(lighter > 100 && darker == 0, "the glow round the explosion warms the ground and never darkens it: " + std::to_string(lighter) + " pixels lighter, " + std::to_string(darker) + " channels darker");
    }

    // Fog of War hides an explosion whose cell is unexplored
    {
        Rig fogged(arc, pack);
        fogged.effects = {blast(x, y, 250, true)};
        fogged.fog = true;
        Rig none(arc, pack);
        none.fog = true;
        check(same(fogged.frame(), none.frame()), "an explosion in an unexplored cell is not drawn");
        fogged.revealed.emplace_back(kCol, kRow);
        none.revealed.emplace_back(kCol, kRow);
        check(!same(fogged.frame(), none.frame()), "...and when its cell is explored it is");
    }

    // Classic draws the original's explosion: no glow, nothing beyond the original's sprite
    {
        Rig classic(arc, pack);
        classic.rig.renderer.set_world_art(nullptr);
        const zoomtest::Picture clean = classic.frame();
        classic.effects = {blast(x, y, 250)};
        size_t n = 0;
        const Box box = changed_box(clean, classic.frame(), &n);
        const auto& part = seq.subitems[3].frames[0];
        const auto& sprite = arc.get_sprite(part.sprite_index);
        check(n > 0 && box.x0 >= classic.px(x + part.dx) && box.x1 <= classic.px(x + part.dx + static_cast<int32_t>(sprite.width)) && box.y0 >= classic.py(y + part.dy) && box.y1 <= classic.py(y + part.dy + static_cast<int32_t>(sprite.height)), "with Classic the explosion is the original's: nothing beyond its sprite");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;                                                  // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    assets::AssetArchive arc;
    world_art::Pack pack;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd") || !pack.load(pack_folder())) {
        std::fprintf(stderr, "cannot open ants.chd or the world art: %s\n", pack.what().c_str());
        return 2;
    }
    test_fire(arc, pack);
    test_bomb(arc, pack);
    test_explosion(arc, pack);
    std::printf("\nworld art effects: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
