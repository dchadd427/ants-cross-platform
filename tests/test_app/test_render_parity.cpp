// Render parity tests: the real Renderer (software renderer, dummy video driver) against an independent CPU data model
// of the original's draw rules, all derived from Original-Ants/ants.chd and the six original maps:
//   * every animation frame's parts are drawn LAST STORED PART FIRST (Ants.exe FUN_0102b8d7),
//   * a map cell's tile value is the id of its Table-4 animation; all cells of an id share one template clock that
//     starts at map load (FUN_0100674e, FUN_0102c1fc, FUN_0102b997),
//   * layer-2 objects are drawn from their anchor cells in row-major order at the anchor top-left + part offset,
//   * ants are recoloured by adding the colour offset to every pixel index unless the image name starts with a digit.
#include <SDL.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "ants_app/renderer.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/grid.hpp"

using namespace ants;
using namespace ants::app;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

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
    std::vector<uint8_t> px; // RGBA8
    Image() = default;
    Image(int width, int height) : w(width), h(height), px(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u) {}
    uint8_t* at(int x, int y) { return &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u]; }
    const uint8_t* at(int x, int y) const { return &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u]; }
};

// ---------------------------------------------------------------------------------------------------------------------
// Reference model (independent of the renderer's implementation)
// ---------------------------------------------------------------------------------------------------------------------

// Frame of an animation at t ms since the template was started (looping; single-frame templates never advance).
size_t model_frame(const assets::AnimationSequence& seq, uint64_t t_ms) {
    if (seq.subitems.size() <= 1) return 0;
    uint64_t total = 0;
    for (const auto& s : seq.subitems) total += s.val3;
    if (total == 0) return 0;
    const uint64_t m = t_ms % total;
    uint64_t acc = 0;
    for (size_t i = 0; i < seq.subitems.size(); ++i) {
        acc += seq.subitems[i].val3;
        if (m < acc) return i;
    }
    return seq.subitems.size() - 1;
}

void model_draw_frame(Image& im, const assets::AssetArchive& arc, const assets::AnimationSubItem& sub, int ox, int oy,
                      bool mirrored, const std::array<assets::ColorRGBA, 256>& pal) {
    for (size_t k = sub.frames.size(); k-- > 0;) { // last stored part first
        const auto& part = sub.frames[k];
        const auto& sp = mirrored ? arc.get_mirrored_sprite(part.sprite_index) : arc.get_sprite(part.sprite_index);
        for (uint32_t y = 0; y < sp.height; ++y) {
            for (uint32_t x = 0; x < sp.width; ++x) {
                const uint8_t v = sp.pixels[y * sp.pitch + x];
                if (v == assets::CHD_COLOR_KEY_INDEX) continue;
                const int px = ox + part.dx + static_cast<int>(x);
                const int py = oy + part.dy + static_cast<int>(y);
                if (px < 0 || py < 0 || px >= im.w || py >= im.h) continue;
                uint8_t* d = im.at(px, py);
                d[0] = pal[v].r; d[1] = pal[v].g; d[2] = pal[v].b; d[3] = 255;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Renderer capture helpers
// ---------------------------------------------------------------------------------------------------------------------

Image read_region(SDL_Renderer* sr, int x, int y, int w, int h) {
    Image im(w, h);
    SDL_Rect rd = { x, y, w, h };
    if (SDL_RenderReadPixels(sr, &rd, SDL_PIXELFORMAT_RGBA32, im.px.data(), w * 4) != 0) {
        std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        std::exit(2);
    }
    return im;
}

// Renders the static map layers of the whole map by tiling camera positions over the playfield.
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
            r.render_map_layers(grid);
            const Image tile = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);
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

// Reference for the static map layers at t ms after map load. `overrides` replaces the tile shown at a layer-2 anchor
// (runtime SetTile: food stages), `extras` adds runtime items (cell, animation id) that are drawn after the map objects.
struct CellItem { int col; int row; int32_t anim_id; };
Image model_map(const assets::AssetArchive& arc, const assets::LevelData& level, uint64_t t_ms,
                const std::vector<CellItem>& overrides = {}, const std::vector<CellItem>& extras = {}) {
    Image im(static_cast<int>(level.width) * 32, static_cast<int>(level.height) * 32);
    for (size_t i = 0; i < im.px.size(); i += 4) { im.px[i] = 255; im.px[i + 1] = 0; im.px[i + 2] = 255; im.px[i + 3] = 255; }
    const auto& pal = arc.get_palette();
    auto draw_id = [&](uint16_t id, int ox, int oy) {
        if (id >= arc.animation_count()) return;
        const auto& seq = arc.get_animation(id);
        if (seq.subitems.empty()) return;
        model_draw_frame(im, arc, seq.subitems[model_frame(seq, t_ms)], ox, oy, false, pal);
    };
    // Layer 1: the cell value is the animation id
    for (uint32_t y = 0; y < level.height; ++y)
        for (uint32_t x = 0; x < level.width; ++x)
            draw_id(level.get_cell_layer1(x, y).tile_index, static_cast<int>(x) * 32, static_cast<int>(y) * 32);
    // Layer 2: anchors in row-major order (start markers are editor art and never shown)
    for (uint32_t y = 0; y < level.height; ++y) {
        for (uint32_t x = 0; x < level.width; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            if (c2.is_empty() || c2.tile_index >= level.tile_dictionary.size()) continue;
            if ((c2.flags & 1) == 0) continue;
            std::string low = level.tile_dictionary[c2.tile_index];
            for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (low.empty() || low == "." || low.find("start") != std::string::npos) continue;
            uint16_t id = c2.tile_index;
            for (const auto& o : overrides) if (o.col == static_cast<int>(x) && o.row == static_cast<int>(y)) id = static_cast<uint16_t>(o.anim_id);
            draw_id(id, static_cast<int>(x) * 32, static_cast<int>(y) * 32);
        }
    }
    for (const auto& e : extras) draw_id(static_cast<uint16_t>(e.anim_id), e.col * 32, e.row * 32);
    return im;
}

struct DiffStats { long pixels{0}; long cells{0}; int first_cx{-1}; int first_cy{-1}; };

DiffStats diff_images(const Image& a, const Image& b) {
    DiffStats st;
    std::vector<uint8_t> cell_bad(static_cast<size_t>((a.w / 32) * (a.h / 32)), 0);
    for (int y = 0; y < a.h; ++y) {
        for (int x = 0; x < a.w; ++x) {
            if (std::memcmp(a.at(x, y), b.at(x, y), 3) != 0) {
                ++st.pixels;
                const size_t ci = static_cast<size_t>((y / 32) * (a.w / 32) + (x / 32));
                if (!cell_bad[ci]) {
                    cell_bad[ci] = 1;
                    ++st.cells;
                    if (st.first_cx < 0) { st.first_cx = x / 32; st.first_cy = y / 32; }
                }
            }
        }
    }
    return st;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------------------------------------

void test_part_order_helper() {
    std::printf("[part order] helper\n");
    const auto o3 = frame_part_draw_order(3);
    check(o3.size() == 3 && o3[0] == 2 && o3[1] == 1 && o3[2] == 0, "last stored part is drawn first");
    check(frame_part_draw_order(0).empty(), "empty frame");
    check(frame_part_draw_order(1) == std::vector<size_t>{0}, "single part");
}

void test_part_order_all_frames(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[part order] every multi-part frame of ants.chd composites in the original order\n");
    SDL_Renderer* sr = r.get_sdl_renderer();
    const auto& pal = arc.get_palette();
    constexpr int kW = 320, kH = 320, kOx = 160, kOy = 160;
    long frames = 0, bad = 0;
    std::string first_bad;
    for (size_t ai = 0; ai < arc.animation_count(); ++ai) {
        const auto& seq = arc.get_animation(static_cast<uint32_t>(ai));
        for (size_t fi = 0; fi < seq.subitems.size(); ++fi) {
            const auto& sub = seq.subitems[fi];
            if (sub.frames.size() < 2) continue;
            ++frames;
            Image ref(kW, kH);
            for (size_t i = 0; i < ref.px.size(); i += 4) { ref.px[i] = 255; ref.px[i + 1] = 0; ref.px[i + 2] = 255; ref.px[i + 3] = 255; }
            model_draw_frame(ref, arc, sub, kOx, kOy, false, pal);
            SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
            SDL_RenderClear(sr);
            r.draw_animation_frame(sub, kOx, kOy, false, TEAM_NONE);
            const Image got = read_region(sr, 0, 0, kW, kH);
            if (std::memcmp(got.px.data(), ref.px.data(), ref.px.size()) != 0) {
                // compare RGB only (alpha of the backbuffer is not part of the contract)
                bool same = true;
                for (size_t i = 0; i < ref.px.size() && same; i += 4) same = std::memcmp(&got.px[i], &ref.px[i], 3) == 0;
                if (!same) {
                    ++bad;
                    if (first_bad.empty()) first_bad = seq.name + " frame " + std::to_string(fi);
                }
            }
        }
    }
    check(frames > 2000, "found the multi-part frames (" + std::to_string(frames) + ")");
    check(bad == 0, "multi-part frames drawn in the wrong order: " + std::to_string(bad) + " (first: " + first_bad + ")");
}

void test_mirrored_part_order(const assets::AssetArchive& arc) {
    std::printf("[part order] mirrored directional animations keep the stored part order\n");
    long compared = 0, bad = 0;
    static const char* prefixes[] = { "hgst", "hgwg", "hbst", "hfst", "htst", "hcst", "hsst", "agst", "aggf" };
    for (const char* prefix : prefixes) {
        for (int d = 5; d <= 7; ++d) {
            const auto* mirrored = arc.get_directional_animation(prefix, static_cast<assets::Direction>(d));
            if (!mirrored) continue;
            // stored source directions 3 (SE), 2 (E), 1 (NE)
            const auto* src = arc.get_directional_animation(prefix, static_cast<assets::Direction>(8 - d));
            if (!src || src->subitems.size() != mirrored->subitems.size()) continue;
            for (size_t f = 0; f < src->subitems.size(); ++f) {
                const auto& a = src->subitems[f].frames;
                const auto& b = mirrored->subitems[f].frames;
                if (a.size() != b.size()) { ++bad; continue; }
                ++compared;
                for (size_t k = 0; k < a.size(); ++k) if (a[k].sprite_index != b[k].sprite_index) { ++bad; break; }
            }
        }
    }
    check(compared > 0, "compared mirrored frames");
    check(bad == 0, "mirrored frames with a different part order/count: " + std::to_string(bad));
}

void test_template_clock(const assets::AssetArchive& arc) {
    std::printf("[templates] original template cycles (Table-4 durations)\n");
    struct Row { const char* name; uint32_t cycle; };
    static const Row rows[] = {
        {"w01a", 600}, {"wm02a", 600}, {"MW02a", 600}, {"M01b", 1400}, {"M01c", 2400}, {"M01d", 2000},
        {"m01d_a", 1550}, {"m01d_b", 2550}, {"m01d_c", 2800}, {"m01d_d", 3250}, {"m01d_e", 3650},
        {"m01e", 3400}, {"m01e_a", 1600}, {"m01e_b", 2100}, {"m01e_c", 4100}, {"m01e_d", 4700}, {"m01e_e", 4500},
    };
    for (const auto& row : rows) {
        const auto* seq = arc.find_animation(row.name);
        check(seq != nullptr, std::string("animation exists: ") + row.name);
        if (!seq) continue;
        uint32_t total = 0;
        for (const auto& s : seq->subitems) total += s.val3;
        check(total == row.cycle, std::string(row.name) + " cycle " + std::to_string(total) + " expected " + std::to_string(row.cycle));
        check(Renderer::get_anim_subitem_by_time(*seq, 0) == 0, std::string(row.name) + " starts on frame 0");
        check(Renderer::get_anim_subitem_by_time(*seq, row.cycle) == 0, std::string(row.name) + " loops to frame 0");
    }
    // M01b: idle 1000 ms, pops 100,100,100,50,50
    const auto* b = arc.find_animation("M01b");
    if (b) {
        check(Renderer::get_anim_subitem_by_time(*b, 999) == 0, "M01b idle until 1000 ms");
        check(Renderer::get_anim_subitem_by_time(*b, 1000) == 1, "M01b first pop frame at 1000 ms");
        check(Renderer::get_anim_subitem_by_time(*b, 1299) == 3, "M01b frame 3 until 1300 ms");
        check(Renderer::get_anim_subitem_by_time(*b, 1300) == 4, "M01b frame 4 at 1300 ms");
        check(Renderer::get_anim_subitem_by_time(*b, 1349) == 4, "M01b frame 4 lasts 50 ms");
        check(Renderer::get_anim_subitem_by_time(*b, 1350) == 5, "M01b frame 5 at 1350 ms");
    }
}

void test_map_layers(Renderer& r, const assets::AssetArchive& arc) {
    static const char* maps[] = { "GAUNTLET", "ISLANDS", "MEDIUM", "SMALL", "TINY", "TREASURE" };
    // t = 0 (all idle), inside the M01b/M01d pop windows, and later inside other pop windows
    static const uint32_t times[] = { 0, 1050, 1150, 2350, 3100, 4000 };
    for (const char* name : maps) {
        assets::LevelData level;
        const std::string path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL";
        if (!level.load_from_file(path)) { check(false, "load " + path); continue; }
        sim::Grid grid;
        grid.init_from_level(level);
        r.set_level(level);
        for (uint32_t t : times) {
            r.pin_animation_clock(t);
            const Image got = capture_map(r, grid, level.width, level.height);
            const Image ref = model_map(arc, level, t);
            const DiffStats st = diff_images(got, ref);
            std::printf("[map layers] %-9s t=%4u ms  differing cells %ld, pixels %ld\n", name, t, st.cells, st.pixels);
            check(st.pixels == 0, std::string(name) + " t=" + std::to_string(t) + " ms: " + std::to_string(st.cells) +
                                      " cells differ (first at " + std::to_string(st.first_cx) + "," + std::to_string(st.first_cy) + ")");
        }
        r.unpin_animation_clock();
    }
}

void test_ant_colour_rule(const assets::AssetArchive& arc) {
    std::printf("[palette] original per-pixel ant colour rule and raw world palette\n");
    const auto& base = arc.get_palette();
    static const uint8_t offsets[4] = { 60, 40, 20, 0 };
    long checked = 0, bad_raw = 0, bad_ant = 0;
    for (size_t si = 0; si < arc.sprite_count(); ++si) {
        const std::string& name = arc.get_sprite(static_cast<uint32_t>(si)).name;
        const bool digit = !name.empty() && std::isdigit(static_cast<unsigned char>(name[0])) != 0;
        const auto raw = TextureCache::compose_palette(base, name, TEAM_NONE);
        for (size_t i = 0; i < 256; ++i) if (raw[i] != base[i]) { ++bad_raw; break; }
        for (uint8_t team = 0; team < 4; ++team) {
            const auto pal = TextureCache::compose_palette(base, name, ant_colour(team));
            ++checked;
            for (size_t i = 0; i < 256; ++i) {
                assets::ColorRGBA expect = base[i];
                if (!digit && i != assets::CHD_COLOR_KEY_INDEX) expect = base[(i + offsets[team]) & 0xFF];
                if (pal[i] != expect) { ++bad_ant; break; }
            }
        }
    }
    check(checked == static_cast<long>(arc.sprite_count()) * 4, "checked every sprite for every team");
    check(bad_raw == 0, "TEAM_NONE is the raw CHD palette (bad sprites: " + std::to_string(bad_raw) + ")");
    check(bad_ant == 0, "ant colour rule (bad sprite/team pairs: " + std::to_string(bad_ant) + ")");
    // transparent index is never recoloured; digit names are exempt
    const auto p = TextureCache::compose_palette(base, "agst301.bmp", ant_colour(0));
    check(p[assets::CHD_COLOR_KEY_INDEX].a == 0, "transparent key stays transparent");
    const auto d = TextureCache::compose_palette(base, "3lb0000.bmp", ant_colour(0));
    check(d[85] == base[85], "digit-named images are not recoloured");
}

void test_ant_sprite_pixels(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[palette] rendered ant frame uses the shifted palette\n");
    SDL_Renderer* sr = r.get_sdl_renderer();
    const auto* seq = arc.find_animation("agst301");
    check(seq != nullptr && !seq->subitems.empty(), "agst301 exists");
    if (!seq || seq->subitems.empty()) return;
    static const uint8_t offsets[4] = { 60, 40, 20, 0 };
    for (uint8_t team = 0; team < 4; ++team) {
        auto pal = arc.get_palette();
        auto shifted = pal;
        for (size_t i = 0; i < 256; ++i) if (i != assets::CHD_COLOR_KEY_INDEX) shifted[i] = pal[(i + offsets[team]) & 0xFF];
        Image ref(200, 200);
        for (size_t i = 0; i < ref.px.size(); i += 4) { ref.px[i] = 255; ref.px[i + 1] = 0; ref.px[i + 2] = 255; ref.px[i + 3] = 255; }
        model_draw_frame(ref, arc, seq->subitems[0], 100, 100, false, shifted);
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.draw_animation_frame(seq->subitems[0], 100, 100, false, ant_colour(team));
        const Image got = read_region(sr, 0, 0, 200, 200);
        bool same = true;
        for (size_t i = 0; i < ref.px.size() && same; i += 4) same = std::memcmp(&got.px[i], &ref.px[i], 3) == 0;
        check(same, "agst301 rendered with team colour " + std::to_string(team));
    }
}


// Runtime layer-2 items placed through the grid API on a flat gravel map: bombs, fire walls, bridge stages, lunchbox,
// power-ups. Every item shows its Table-4 template (part offsets included) and all cells of an id share one clock.
void test_dynamic_items(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[dynamic items] bombs, fire walls, bridges, lunchbox and power-ups\n");
    assets::LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) { check(false, "load TINY"); return; }
    const int32_t g = arc.find_animation_id("g01a");
    if (level.tile_dictionary.size() <= static_cast<size_t>(g)) level.tile_dictionary.resize(static_cast<size_t>(g) + 1, ".");
    level.tile_dictionary[static_cast<size_t>(g)] = "g01a";
    for (auto& c : level.layer1_terrain) { c.tile_index = static_cast<uint16_t>(g); c.flags = 0; c.properties = 0; }
    for (auto& c : level.layer2_interactive) { c.tile_index = assets::LVL_EMPTY_TILE; c.flags = 0; c.properties = 0; }
    level.anthill_spawns.clear();
    level.food_schedules.clear();
    level.waypoints.clear();

    sim::Grid grid;
    grid.init_from_level(level);
    r.set_level(level);

    std::vector<CellItem> items;
    static const char* bomb_names[4] = { "greenbomb", "redbomb", "bluebomb", "blackbomb" }; // owners 0..3
    for (int t = 0; t < 4; ++t) {
        grid.place_bomb(static_cast<uint32_t>(3 * t), 1, static_cast<uint8_t>(t));
        items.push_back({3 * t, 1, arc.find_animation_id(bomb_names[t])});
    }
    grid.place_firewall(12, 1, 0);
    items.push_back({12, 1, arc.find_animation_id("wallup04")});
    grid.place_firewall(3, 10, 1);
    items.push_back({3, 10, arc.find_animation_id("wallup04")});
    for (int s = 1; s <= 4; ++s) {
        grid.set_bridge_at(sim::TileCoord{3 * (s - 1), 4}, static_cast<uint8_t>(s), 3600);
        static const char* bridge_names[4] = { "bridge1", "bridge2", "bridge3", "bridge4" };
        items.push_back({3 * (s - 1), 4, arc.find_animation_id(bridge_names[s - 1])});
    }
    grid.get_cell_mut(12, 4).interactive_id = sim::TILE_BRIDGE4B;
    items.push_back({12, 4, arc.find_animation_id("bridge4b")});
    grid.drop_lunchbox(0, 7, 25);
    items.push_back({0, 7, arc.find_animation_id("lunchbox")});
    static const char* pu_names[6] = { "", "pu_bomb", "pu_mason", "pu_thief", "pu_comb", "pu_swim" };
    for (int p = 1; p <= 4; ++p) {
        grid.place_powerup(3 * p, 7, static_cast<uint8_t>(p));
        items.push_back({3 * p, 7, arc.find_animation_id(pu_names[p])});
    }
    grid.place_powerup(0, 10, 5);
    items.push_back({0, 10, arc.find_animation_id(pu_names[5])});

    // Row-major draw order among items = original layer-2 pass
    std::vector<CellItem> ordered = items;
    std::stable_sort(ordered.begin(), ordered.end(), [](const CellItem& a, const CellItem& b) {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });

    for (uint32_t t : { 0u, 90u, 130u, 250u, 610u }) {
        r.pin_animation_clock(t);
        const Image got = capture_map(r, grid, level.width, level.height);
        const Image ref = model_map(arc, level, t, {}, ordered);
        const DiffStats st = diff_images(got, ref);
        std::printf("[dynamic items] t=%3u ms  differing cells %ld, pixels %ld\n", t, st.cells, st.pixels);
        check(st.pixels == 0, "dynamic items t=" + std::to_string(t) + " ms: " + std::to_string(st.cells) + " cells differ");
    }
    r.unpin_animation_clock();
}

// Food stages: the anchor keeps its position and shows the template of the current stage tile (all parts).
void test_food_stages(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[food stages] fdpmeat stage tiles keep every part\n");
    assets::LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TREASURE.LVL")) { check(false, "load TREASURE"); return; }
    sim::Grid grid;
    grid.init_from_level(level);
    r.set_level(level);
    const int32_t meat1 = arc.find_animation_id("fdpmeat1");
    int ax = -1, ay = -1;
    for (uint32_t y = 0; y < level.height && ax < 0; ++y)
        for (uint32_t x = 0; x < level.width && ax < 0; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            if (!c2.is_empty() && (c2.flags & 1) && static_cast<int32_t>(c2.tile_index) == meat1) { ax = static_cast<int>(x); ay = static_cast<int>(y); }
        }
    check(ax >= 0, "TREASURE has a fdpmeat1 object");
    if (ax < 0) return;
    static const char* stages[3] = { "fdpmeat2", "fdpmeat3", "fdpmeat4" };
    for (const char* stage : stages) {
        const int32_t stage_id = arc.find_animation_id(stage);
        // The sim advances a food stage on every cell of the item's footprint (same tile and property word)
        const auto& anchor = level.get_cell_layer2(static_cast<uint32_t>(ax), static_cast<uint32_t>(ay));
        for (int fy = std::max(0, ay - 4); fy <= std::min(static_cast<int>(level.height) - 1, ay + 4); ++fy) {
            for (int fx = std::max(0, ax - 4); fx <= std::min(static_cast<int>(level.width) - 1, ax + 4); ++fx) {
                const auto& fc = level.get_cell_layer2(static_cast<uint32_t>(fx), static_cast<uint32_t>(fy));
                if (fc.tile_index == anchor.tile_index && (anchor.properties == 0 || fc.properties == anchor.properties)) {
                    grid.get_cell_mut(static_cast<uint32_t>(fx), static_cast<uint32_t>(fy)).interactive_id = static_cast<uint16_t>(stage_id);
                }
            }
        }
        r.pin_animation_clock(0);
        const Image got = capture_map(r, grid, level.width, level.height);
        const Image ref = model_map(arc, level, 0, {CellItem{ax, ay, stage_id}});
        const DiffStats st = diff_images(got, ref);
        check(st.pixels == 0, std::string("food stage ") + stage + ": " + std::to_string(st.cells) + " cells differ");
    }
    r.unpin_animation_clock();
}

} // namespace

int main() {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window* win = SDL_CreateWindow("render-parity", 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT, SDL_WINDOW_HIDDEN);
    assets::AssetArchive arc;
    if (!win || !arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open window or ants.chd\n");
        return 2;
    }
    Renderer r;
    if (!r.init(win, arc, false)) {
        std::fprintf(stderr, "renderer init failed\n");
        return 2;
    }

    test_part_order_helper();
    test_mirrored_part_order(arc);
    test_template_clock(arc);
    test_ant_colour_rule(arc);
    test_part_order_all_frames(r, arc);
    test_ant_sprite_pixels(r, arc);
    test_map_layers(r, arc);
    test_dynamic_items(r, arc);
    test_food_stages(r, arc);

    r.shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::printf("\nrender parity: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
