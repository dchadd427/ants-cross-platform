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
#include <functional>
#include <algorithm>
#include <string>
#include <vector>

#include "ants_app/renderer.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/effect_specs.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/movement_tables.hpp"

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
                // A mirrored part is written one pixel right of its origin (Ants.exe 0x102d12c)
                const int px = ox + part.dx + (mirrored ? 1 : 0) + static_cast<int>(x);
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

// The layer-2 pass under fog (Ants.exe FUN_01008089 mode 2, docs 5.33.5 and 5.57), as a model of the original's rules. One view at a time (the original draws per view:
// the rows from top / 32 - 3 to bottom / 32 + 3 + 1 and the columns likewise, clipped to the map), row-major over the cells of that range:
//   * an anchor cell (layer-2 word bit 0) draws its object at its own cell, except that an unexplored anchor hides a power-up, a bomb, food and a fire wall (0x1007202,
//     0x1008bc6, 0x10071dd, 0x1008bb7);
//   * any other cell with an object draws it at its ANCHOR (the cell's id at the anchor's position, bytes 2 and 3 of the cell) when the cell is explored and the anchor is not.
using Explored = std::function<bool(int, int)>;
bool model_hidden_class(uint16_t id) {
    return (sim::movement::tile_flags_of(id) & (sim::movement::kTileFlagFood | sim::movement::kTileFlagPowerUp)) != 0 || (id >= 129 && id <= 132) || id == 134;
}
Image model_map_fog(const assets::AssetArchive& arc, const assets::LevelData& level, uint64_t t_ms, const Explored& explored) {
    const int W = static_cast<int>(level.width), H = static_cast<int>(level.height);
    Image out(W * 32, H * 32);
    for (size_t i = 0; i < out.px.size(); i += 4) { out.px[i] = 255; out.px[i + 1] = 0; out.px[i + 2] = 255; out.px[i + 3] = 255; }
    const auto& pal = arc.get_palette();
    std::vector<int> xs, ys;
    for (int x = 0;; x += 384) { xs.push_back(std::min(x, out.w - PLAYFIELD_W)); if (x + PLAYFIELD_W >= out.w) break; }
    for (int y = 0;; y += 384) { ys.push_back(std::min(y, out.h - PLAYFIELD_H)); if (y + PLAYFIELD_H >= out.h) break; }
    for (int cam_y : ys) {
        for (int cam_x : xs) {
            Image im(out.w, out.h);
            for (size_t i = 0; i < im.px.size(); i += 4) { im.px[i] = 255; im.px[i + 1] = 0; im.px[i + 2] = 255; im.px[i + 3] = 255; }
            auto draw_id = [&](uint16_t id, int ox, int oy) {
                if (id >= arc.animation_count()) return;
                const auto& seq = arc.get_animation(id);
                if (seq.subitems.empty()) return;
                model_draw_frame(im, arc, seq.subitems[model_frame(seq, t_ms)], ox, oy, false, pal);
            };
            for (int y = std::max(0, cam_y / 32); y <= std::min(H - 1, (cam_y + PLAYFIELD_H) / 32); ++y) {
                for (int x = std::max(0, cam_x / 32); x <= std::min(W - 1, (cam_x + PLAYFIELD_W) / 32); ++x) {
                    draw_id(level.get_cell_layer1(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).tile_index, x * 32, y * 32);
                }
            }
            const int row_begin = std::max(0, cam_y / 32 - 3), row_end = std::min(H, (cam_y + PLAYFIELD_H) / 32 + 3 + 1);
            const int col_begin = std::max(0, cam_x / 32 - 3), col_end = std::min(W, (cam_x + PLAYFIELD_W) / 32 + 3 + 1);
            for (int r = row_begin; r < row_end; ++r) {
                for (int c = col_begin; c < col_end; ++c) {
                    const auto& c2 = level.get_cell_layer2(static_cast<uint32_t>(c), static_cast<uint32_t>(r));
                    if (c2.is_empty() || c2.tile_index == 0xFFFF || c2.tile_index >= level.tile_dictionary.size()) continue;
                    std::string low = level.tile_dictionary[c2.tile_index];
                    for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (low.empty() || low == "." || low.find("start") != std::string::npos) continue;
                    if ((c2.flags & 1) != 0) {
                        if (!explored(c, r) && model_hidden_class(c2.tile_index)) continue;
                        draw_id(c2.tile_index, c * 32, r * 32);
                    } else if (explored(c, r)) {
                        const int ay = c2.properties & 0xff, ax = (c2.properties >> 8) & 0xff;
                        if (!explored(ax, ay)) draw_id(c2.tile_index, ax * 32, ay * 32);
                    }
                }
            }
            for (int y = 0; y < PLAYFIELD_H; ++y) {
                for (int x = 0; x < PLAYFIELD_W; ++x) {
                    const int wx = cam_x + x, wy = cam_y + y;
                    if (wx < 0 || wy < 0 || wx >= out.w || wy >= out.h) continue;
                    std::memcpy(out.at(wx, wy), im.at(wx, wy), 4);
                }
            }
        }
    }
    return out;
}

uint32_t fog_mix(uint32_t v) { v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; v ^= v >> 16; return v; }

void test_fog_objects(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[fog objects] the layer-2 pass under fog: anchors hide food / power-ups / bombs / fire while unexplored, explored body cells redraw an object whose anchor is unexplored\n");
    static const char* maps[] = { "TREASURE", "ISLANDS", "GAUNTLET", "MEDIUM", "SMALL", "TINY" };
    for (const char* name : maps) {
        assets::LevelData level;
        const std::string path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL";
        if (!level.load_from_file(path)) { check(false, "load " + path); continue; }
        sim::Grid grid;
        grid.init_from_level(level);
        r.set_level(level);
        const int W = static_cast<int>(level.width), H = static_cast<int>(level.height);
        for (int pattern = 0; pattern < 9; ++pattern) {
            // patterns 0 .. 7: blocks of 2 x 2 cells explored by a hash (about half of them, the share growing with the pattern), pattern 8: the upper half of the map explored
            auto is_explored = [&](int x, int y) {
                if (x < 0 || y < 0 || x >= W || y >= H) return false;
                if (pattern == 8) return y < H / 2;
                return (fog_mix(static_cast<uint32_t>((x / 2) * 7919 + (y / 2) * 104729 + pattern * 31)) % 100u) < 30u + static_cast<uint32_t>(pattern) * 6u;
            };
            sim::WorldState world;
            world.width = level.width;
            world.height = level.height;
            world.fog_of_war_enabled = true;
            world.fog_revealed.assign(static_cast<size_t>(W) * static_cast<size_t>(H), 0);
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) world.fog_revealed[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)] = is_explored(x, y) ? 1 : 0;
            r.pin_animation_clock(0);
            const Image got = capture_map(r, grid, level.width, level.height, &world);
            const Image ref = model_map_fog(arc, level, 0, is_explored);
            const DiffStats st = diff_images(got, ref);
            std::printf("[fog objects] %-9s pattern %d  differing cells %ld, pixels %ld\n", name, pattern, st.cells, st.pixels);
            check(st.pixels == 0, std::string(name) + " fog pattern " + std::to_string(pattern) + ": " + std::to_string(st.cells) + " cells differ (first at " +
                                      std::to_string(st.first_cx) + "," + std::to_string(st.first_cy) + ")");
            r.unpin_animation_clock();
        }
    }
}

// A food pile under fog reads its footprint from the live cells (the anchor bytes of its CURRENT stage), not from the cells that it had at the start of the match
// (docs 5.57): when its stage changes and cells drop out of the footprint, exploring such a cell no longer shows the food; exploring a cell of the new footprint does.
void test_food_fog_footprint(Renderer& r) {
    std::printf("[fog food] the footprint of a food pile under fog follows its stage\n");
    assets::LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TREASURE.LVL")) { check(false, "load TREASURE"); return; }
    sim::Grid grid;
    grid.init_from_level(level);
    r.set_level(level);
    const sim::TileCoord anchor{17, 16};                            // fdcola1 (19 cells), then 13, 7 and 3
    const int32_t object = grid.food_object_first_at(anchor);
    check(object >= 0, "TREASURE has the food pile fdcola at (17, 16)");
    if (object < 0) return;
    const uint16_t first_tile = grid.get_cell(anchor).interactive_id;
    std::vector<sim::TileCoord> old_cells;
    {
        const auto span = sim::movement::food_footprint(first_tile);
        for (size_t k = 0; k < span.count; ++k) old_cells.push_back(sim::TileCoord{anchor.x + span.cells[k].dcol, anchor.y + span.cells[k].drow});
    }
    // eat until the stage has changed twice: fdcola3 (7 cells)
    int changes = 0;
    for (int bite = 0; bite < 200 && changes < 2; ++bite) {
        bool changed = false;
        grid.take_food(object, 1, changed);
        if (changed) {
            ++changes;
            grid.set_food_tile(anchor, grid.food_objects()[static_cast<size_t>(object)].stage_tile());
        }
    }
    const uint16_t stage_tile = grid.get_cell(anchor).interactive_id;
    check(changes == 2 && stage_tile != first_tile && stage_tile != 0x7FFE, "the pile reached its third stage");
    std::vector<sim::TileCoord> live_cells, dropped_cells;
    for (const auto& t : old_cells) {
        const auto& cell = grid.get_cell(t);
        (cell.interactive_id == stage_tile && cell.anchor_x == anchor.x && cell.anchor_y == anchor.y ? live_cells : dropped_cells).push_back(t);
    }
    // a body cell of the live footprint and a cell that dropped out
    sim::TileCoord live_body{-1, -1}, dropped{-1, -1};
    for (const auto& t : live_cells) if (!(t.x == anchor.x && t.y == anchor.y)) { live_body = t; break; }
    if (!dropped_cells.empty()) dropped = dropped_cells.front();
    check(live_body.x >= 0 && dropped.x >= 0, "the new stage keeps body cells and has dropped some of the old ones");
    if (live_body.x < 0 || dropped.x < 0) return;

    auto render_with = [&](const std::vector<sim::TileCoord>& explored_cells) {
        sim::WorldState world;
        world.width = level.width;
        world.height = level.height;
        world.fog_of_war_enabled = true;
        world.fog_revealed.assign(static_cast<size_t>(level.width) * level.height, 0);
        for (const auto& t : explored_cells) world.fog_revealed[static_cast<size_t>(t.y) * level.width + static_cast<size_t>(t.x)] = 1;
        r.pin_animation_clock(0);
        r.camera().x = static_cast<float>(anchor.x * 32 - 200);
        r.camera().y = static_cast<float>(anchor.y * 32 - 200);
        r.camera().clamp_to_bounds(level.width, level.height);
        SDL_Renderer* sr = r.get_sdl_renderer();
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.render_map_layers(grid, &world);
        Image im = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);
        r.unpin_animation_clock();
        return im;
    };
    auto differ = [](const Image& a, const Image& b) { return std::memcmp(a.px.data(), b.px.data(), a.px.size()) != 0; };
    const Image nothing = render_with({});
    const Image body = render_with({live_body});
    const Image gone = render_with({dropped});
    const Image at_anchor = render_with({anchor});
    check(differ(nothing, body), "a cell of the live footprint that is explored shows the pile (the anchor is not)");
    check(!differ(nothing, gone), "a cell that dropped out of the footprint shows nothing");
    check(differ(nothing, at_anchor), "an explored anchor shows the pile");
    check(!differ(body, at_anchor), "from the body cell the pile looks as it does from its anchor");
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

// Mirrored directions (SW, W, NW) draw one pixel to the right of the plain flip: an exact reflection about the anchor.
void test_mirrored_draw(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[mirror] mirrored parts are an exact reflection about the anchor column\n");
    SDL_Renderer* sr = r.get_sdl_renderer();
    static const uint8_t offsets[4] = { 60, 40, 20, 0 };
    long compared = 0, bad = 0;
    for (const char* prefix : { "agst", "abwg", "hgst", "aggh", "abgb" }) {
        for (int d : { 5, 6, 7 }) {
            const auto* seq = arc.get_directional_animation(prefix, static_cast<assets::Direction>(d));
            if (!seq || seq->subitems.empty()) continue;
            // the unmirrored source direction: SE (2) for SW, E (9) for W, NE (8) for NW
            const auto* src = arc.get_directional_animation(prefix, static_cast<assets::Direction>(8 - d));
            const size_t frames = std::min<size_t>(seq->subitems.size(), 3);
            for (size_t f = 0; f < frames; ++f) {
                const uint8_t team = static_cast<uint8_t>(f % 4);
                auto pal = arc.get_palette();
                auto shifted = pal;
                for (size_t i = 0; i < 256; ++i) if (i != assets::CHD_COLOR_KEY_INDEX) shifted[i] = pal[(i + offsets[team]) & 0xFF];
                Image ref(200, 200);
                for (size_t i = 0; i < ref.px.size(); i += 4) { ref.px[i] = 255; ref.px[i + 1] = 0; ref.px[i + 2] = 255; ref.px[i + 3] = 255; }
                model_draw_frame(ref, arc, seq->subitems[f], 100, 100, true, shifted);
                SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
                SDL_RenderClear(sr);
                r.draw_animation_frame(seq->subitems[f], 100, 100, true, ant_colour(team));
                const Image got = read_region(sr, 0, 0, 200, 200);
                bool same = true;
                for (size_t i = 0; i < ref.px.size() && same; i += 4) same = std::memcmp(&got.px[i], &ref.px[i], 3) == 0;
                ++compared;
                if (!same) ++bad;
                // reflection: column c of the unmirrored frame (relative to the anchor) lands on column -c
                if (src && f < src->subitems.size() && src->subitems[f].frames.size() == 1) {
                    const auto& sp = src->subitems[f].frames[0];
                    const auto& base = arc.get_sprite(sp.sprite_index);
                    const auto& mp = seq->subitems[f].frames[0];
                    bool exact = true;
                    for (uint32_t y = 0; y < base.height && exact; ++y) {
                        for (uint32_t x = 0; x < base.width && exact; ++x) {
                            const int col = sp.dx + static_cast<int>(x);                      // unmirrored column
                            const int mcol = mp.dx + 1 + static_cast<int>(base.width - 1 - x);  // where it is drawn mirrored
                            if (mcol != -col) exact = false;
                        }
                    }
                    ++compared;
                    if (!exact) ++bad;
                }
            }
        }
    }
    check(compared > 20, "compared mirrored frames (" + std::to_string(compared) + ")");
    check(bad == 0, "mirrored frames that differ from the reflection model: " + std::to_string(bad));
}

// A food-carrying ant that attacks plays the plain a?at clip (the original has no carry variant): the simulation picks that clip (movement::action_clip ignores the food for an
// attack) and the renderer draws the clip that it is given, so the ant must not vanish.
void test_holding_attack(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[ants] a carrying ant that attacks is drawn with the plain attack clip\n");
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
    r.pin_animation_clock(0);
    SDL_Renderer* sr = r.get_sdl_renderer();
    static const char* prefixes[6] = { "ag", "ab", "af", "at", "ac", "as" };
    static const uint8_t offsets[4] = { 60, 40, 20, 0 };
    for (uint8_t type = 0; type < 6; ++type) {
        sim::WorldState ws;
        ws.width = level.width;
        ws.height = level.height;
        sim::AntSnapshot ant{};
        ant.id = 1; ant.player_id = 1; ant.type = static_cast<sim::AntType>(type);
        ant.px = 176; ant.py = 176; ant.tile_x = 5; ant.tile_y = 5;
        ant.facing = 2;                                   // east: clip digit 9
        ant.hp = 10; ant.max_hp = 10;
        ant.is_holding = true;
        ant.anim_state = static_cast<uint16_t>(sim::UnitState::Attacking);
        ant.state = sim::UnitState::Attacking;
        ant.anim_frame = 0;
        // the clip that the simulation plays for a carrying ant that attacks east
        const sim::movement::MotionClip clip = sim::movement::action_clip(sim::movement::ActionClip::Attack, type, 2, true);
        check(clip.valid() && arc.get_animation(clip.chd_index).name == std::string(prefixes[type]) + "at901", std::string("the simulation plays the plain ") + prefixes[type] + "at901 for a carrying ant");
        ant.loco_clip = clip.chd_index;
        ant.loco_frame = 0;
        ant.loco_left_ms = static_cast<uint16_t>(clip.duration(0));
        ws.ants.push_back(ant);
        r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.render_world(ws, grid, -1, {}, false, false, -1, -1, -1, 0.0f);
        const Image got = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);

        Image full = model_map(arc, level, 0);
        auto shifted = arc.get_palette();
        for (size_t i = 0; i < 256; ++i) if (i != assets::CHD_COLOR_KEY_INDEX) shifted[i] = arc.get_palette()[(i + offsets[ant.player_id]) & 0xFF];
        const auto* seq = arc.find_animation(std::string(prefixes[type]) + "at901");
        check(seq != nullptr, std::string("clip exists: ") + prefixes[type] + "at901");
        if (!seq) continue;
        model_draw_frame(full, arc, seq->subitems[0], 176, 176, false, shifted);
        bool same = true;
        for (int y = 0; y < PLAYFIELD_H && same; ++y)
            for (int x = 0; x < PLAYFIELD_W && same; ++x) same = std::memcmp(got.at(x, y), full.at(x, y), 3) == 0;
        check(same, std::string("carrying ") + prefixes[type] + " attack frame 0 matches the plain clip");
    }
    r.unpin_animation_clock();
}

// A frozen ant (the dud bomb's ?bu overlay covers it) is not drawn at all: the display loop skips it (Ants.exe 0x10088e7),
// only the overlay clip is.
void test_frozen_ant(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[ants] a frozen ant is not drawn, only its ?bu overlay\n");
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
    r.pin_animation_clock(0);
    SDL_Renderer* sr = r.get_sdl_renderer();
    static const char letters[6] = { 'g', 'b', 'f', 't', 'c', 's' };
    static const uint8_t offsets[4] = { 60, 40, 20, 0 };
    for (uint8_t type = 0; type < 6; ++type) {
        sim::WorldState ws;
        ws.width = level.width;
        ws.height = level.height;
        sim::AntSnapshot ant{};
        ant.id = 1; ant.player_id = 1; ant.type = static_cast<sim::AntType>(type);
        ant.px = 176; ant.py = 176; ant.tile_x = 5; ant.tile_y = 5;
        ant.facing = 4;
        ant.hp = 10; ant.max_hp = 10;
        ant.anim_state = static_cast<uint16_t>(sim::UnitState::Idle);
        ant.state = sim::UnitState::Idle;
        ant.frozen = true;
        ant.burn_elapsed_ms = 0;
        ws.ants.push_back(ant);
        r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.render_world(ws, grid, -1, {}, false, false, -1, -1, -1, 0.0f);
        const Image got = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);

        Image full = model_map(arc, level, 0);
        auto shifted = arc.get_palette();
        for (size_t i = 0; i < 256; ++i) if (i != assets::CHD_COLOR_KEY_INDEX) shifted[i] = arc.get_palette()[(i + offsets[ant.player_id]) & 0xFF];
        const auto* seq = arc.find_animation(std::string("a") + letters[type] + "bu301");
        check(seq != nullptr, std::string("clip exists: a") + letters[type] + "bu301");
        if (!seq) continue;
        model_draw_frame(full, arc, seq->subitems[0], 176, 176, false, shifted);
        bool same = true;
        for (int y = 0; y < PLAYFIELD_H && same; ++y)
            for (int x = 0; x < PLAYFIELD_W && same; ++x) same = std::memcmp(got.at(x, y), full.at(x, y), 3) == 0;
        check(same, std::string("frozen ") + letters[type] + " ant: only the burn overlay frame 0 is drawn");
    }
    r.unpin_animation_clock();
}

// The burn overlay is a child of the view container (Ants.exe 0x101af66 AddChild, vtable slot +0x3c = the explored test of its cell): it is drawn after the whole sorted sprite list,
// over every ant, also over one that stands lower on the screen (the remake drew it inside the y-sorted pass, under such an ant). In fog an unexplored cell hides it.
void test_burn_overlay_layer(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[ants] the burn overlay is drawn over every ant (a view child), and only where the ground is explored\n");
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
    r.pin_animation_clock(0);
    SDL_Renderer* sr = r.get_sdl_renderer();
    const int32_t stand = arc.find_animation_id("agst301");
    const auto* bu = arc.find_animation("agbu301");
    const auto* stand_seq = arc.find_animation("agst301");
    check(stand >= 0 && bu && stand_seq, "clips exist: agst301, agbu301");
    if (stand < 0 || !bu || !stand_seq) { r.unpin_animation_clock(); return; }
    static const uint8_t offsets[4] = { 60, 40, 20, 0 };
    auto shifted_for = [&](uint8_t team) {
        auto pal = arc.get_palette();
        auto sh = pal;
        for (size_t i = 0; i < 256; ++i) if (i != assets::CHD_COLOR_KEY_INDEX) sh[i] = pal[(i + offsets[team]) & 0xFF];
        return sh;
    };
    auto render = [&](bool fog_unexplored, bool with_ants = true) {
        sim::WorldState ws;
        ws.width = level.width;
        ws.height = level.height;
        sim::AntSnapshot burning{};
        burning.id = 1; burning.player_id = 0; burning.type = sim::AntType::Worker;
        burning.px = 176; burning.py = 176; burning.tile_x = 5; burning.tile_y = 5; burning.facing = 4;
        burning.hp = 10; burning.max_hp = 10;
        burning.anim_state = static_cast<uint16_t>(sim::UnitState::Burn);
        burning.state = sim::UnitState::Burn;
        burning.frozen = true;
        burning.burn_elapsed_ms = 0;
        sim::AntSnapshot lower{};                                  // stands lower on the screen, so it is drawn after the burning ant in the sorted pass
        lower.id = 2; lower.player_id = 3; lower.type = sim::AntType::Worker;
        lower.px = 176; lower.py = 180; lower.tile_x = 5; lower.tile_y = 5; lower.facing = 4;
        lower.hp = 10; lower.max_hp = 10;
        lower.anim_state = static_cast<uint16_t>(sim::UnitState::Idle);
        lower.state = sim::UnitState::Idle;
        lower.loco_clip = static_cast<uint16_t>(stand); lower.loco_frame = 0; lower.loco_left_ms = 150;
        if (with_ants) {
            ws.ants.push_back(burning);
            ws.ants.push_back(lower);
        }
        if (fog_unexplored) {
            ws.fog_of_war_enabled = true;
            ws.fog_revealed.assign(static_cast<size_t>(level.width) * level.height, 0);
            ws.player_alliances[1] = 255;
        }
        r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.render_world(ws, grid, -1, {}, false, false, -1, -1, -1, 0.0f);
        return read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);
    };
    auto same_image = [&](const Image& a, const Image& b) {
        for (int y = 0; y < PLAYFIELD_H; ++y) for (int x = 0; x < PLAYFIELD_W; ++x) if (std::memcmp(a.at(x, y), b.at(x, y), 3) != 0) return false;
        return true;
    };
    // everything explored: the lower ant first, the burn overlay (player 0's colour) over it
    {
        const Image got = render(false);
        Image full = model_map(arc, level, 0);
        model_draw_frame(full, arc, stand_seq->subitems[0], 176, 180, false, shifted_for(3));
        model_draw_frame(full, arc, bu->subitems[0], 176, 176, false, shifted_for(0));
        check(same_image(got, full), "the burn overlay is drawn over the ant that stands lower");
    }
    // an unexplored cell: the overlay of the ant that is not the viewer's is not drawn (nor the ants themselves: the viewer is player 1 here)
    {
        r.set_hud_team(1);
        const Image got = render(true);
        const Image fog_only = render(true, false);
        r.set_hud_team(0);
        check(same_image(got, fog_only), "in an unexplored cell neither the burning ant's overlay nor the ants are drawn (only the fog)");
    }
    r.unpin_animation_clock();
}

// A blown ant is culled by its ART, not by its anchor: the anchor jumps 128 px at the end of frame 0 of the aggb flight while
// the art trails behind it (aggb901 frame 1: 59..102 px from the anchor), so with the anchor just outside the right edge
// the parts that lie inside the playfield must still be drawn.
void test_blown_ant_edge(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[ants] a blown ant whose anchor is past the playfield edge still draws the art that is inside it\n");
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
    r.pin_animation_clock(0);
    SDL_Renderer* sr = r.get_sdl_renderer();
    const int32_t chd = arc.find_animation_id("aggb901");
    check(chd >= 0, "clip exists: aggb901");
    if (chd < 0) { r.unpin_animation_clock(); return; }
    auto render = [&](bool with_ant, int anchor_dx) {
        sim::WorldState ws;
        ws.width = level.width;
        ws.height = level.height;
        if (with_ant) {
            sim::AntSnapshot ant{};
            ant.id = 1; ant.player_id = 1; ant.type = sim::AntType::Worker;
            ant.px = static_cast<int32_t>(r.camera().x) + PLAYFIELD_W + anchor_dx;
            ant.py = static_cast<int32_t>(r.camera().y) + 200;
            ant.tile_x = ant.px / 32; ant.tile_y = ant.py / 32;
            ant.facing = 2;
            ant.hp = 10; ant.max_hp = 10;
            ant.anim_state = static_cast<uint16_t>(sim::UnitState::Knockback);
            ant.state = sim::UnitState::Knockback;
            ant.loco_clip = static_cast<uint16_t>(chd);
            ant.loco_mirrored = true;                     // thrown east: the art trails to the west of the jumped anchor
            ant.loco_frame = 1;                           // aggb901 frame 1: parts at dx +59..+102 (mirrored: -102..-59)
            ws.ants.push_back(ant);
        }
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.render_world(ws, grid, -1, {}, false, false, -1, -1, -1, 0.0f);
        return read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);
    };
    r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
    const Image bare = render(false, 0);
    for (int dx : {5, 20, 60}) {
        const Image got = render(true, dx);
        long diff = 0;
        for (int y = 0; y < PLAYFIELD_H; ++y)
            for (int x = 0; x < PLAYFIELD_W; ++x) if (std::memcmp(got.at(x, y), bare.at(x, y), 3) != 0) ++diff;
        check(diff > 0, "blown ant with its anchor " + std::to_string(dx) + " px past the right edge draws its art (" + std::to_string(diff) + " px)");
    }
    r.unpin_animation_clock();
}

// The real-time player of the original changes clip frames and positions when a frame ENDS; the simulation state is only
// known at 50 ms ticks, so the renderer shows the frames that end before the next tick in advance (sub-tick prediction):
// a thrown ant's 128 px jump at the end of frame 0 and the 60 / 80 / 120 ms frame lengths are not delayed to a tick.
void test_subtick_prediction(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[ants] sub-tick prediction of an action clip: the frames that end before the next tick are shown\n");
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
    r.pin_animation_clock(0);
    SDL_Renderer* sr = r.get_sdl_renderer();
    const int32_t chd = arc.find_animation_id("aggb901");            // worker thrown west: frame 0 lasts 100 ms and jumps -128 px
    check(chd >= 0, "clip exists: aggb901");
    if (chd < 0) { r.unpin_animation_clock(); return; }
    const auto& seq = arc.get_animation(static_cast<uint32_t>(chd));
    static const uint8_t offsets[4] = { 60, 40, 20, 0 };
    struct Case { uint16_t frame; uint16_t left_ms; float sub_s; size_t expect_frame; int expect_dx; };
    const Case cases[] = {
        {0, 30, 0.020f, 0, 0},          // the frame has not ended yet
        {0, 30, 0.030f, 1, -128},       // it ends exactly now: the jump and the next frame
        {0, 30, 0.049f, 1, -128},       // frame 1 (60 ms) is still on
        {1, 20, 0.045f, 2, 0},          // the jump was applied before (px is the landed anchor): frame 2 shows, no displacement
        {2, 10, 0.049f, 3, 0},          // 10 ms left, then frame 3 (60 ms)
        {4, 55, 0.049f, 4, 0},          // a frame that outlasts the tick
    };
    for (const Case& c : cases) {
        sim::WorldState ws;
        ws.width = level.width;
        ws.height = level.height;
        sim::AntSnapshot ant{};
        ant.id = 1; ant.player_id = 1; ant.type = sim::AntType::Worker;
        ant.px = 300; ant.py = 200; ant.tile_x = 9; ant.tile_y = 6;
        ant.facing = 2;
        ant.hp = 10; ant.max_hp = 10;
        ant.anim_state = static_cast<uint16_t>(sim::UnitState::Knockback);
        ant.state = sim::UnitState::Knockback;
        ant.loco_clip = static_cast<uint16_t>(chd);
        ant.loco_frame = c.frame;
        ant.loco_left_ms = c.left_ms;
        ws.ants.push_back(ant);
        r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.render_world(ws, grid, -1, {}, false, false, -1, -1, -1, c.sub_s);
        const Image got = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);

        Image full = model_map(arc, level, 0);
        auto shifted = arc.get_palette();
        for (size_t i = 0; i < 256; ++i) if (i != assets::CHD_COLOR_KEY_INDEX) shifted[i] = arc.get_palette()[(i + offsets[ant.player_id]) & 0xFF];
        model_draw_frame(full, arc, seq.subitems[c.expect_frame], ant.px + c.expect_dx, ant.py, false, shifted);
        bool same = true;
        for (int y = 0; y < PLAYFIELD_H && same; ++y)
            for (int x = 0; x < PLAYFIELD_W && same; ++x) same = std::memcmp(got.at(x, y), full.at(x, y), 3) == 0;
        check(same, "frame " + std::to_string(c.frame) + " with " + std::to_string(c.left_ms) + " ms left, +" + std::to_string(c.sub_s * 1000.0f) +
                    " ms: frame " + std::to_string(c.expect_frame) + " at dx " + std::to_string(c.expect_dx));
    }
    r.unpin_animation_clock();
}

// The real-time player of the original steps a clip when a frame ends, not at 50 ms ticks (REFRESH redraws on every scheduler pass, AnimationStep 0x102b997 uses timeGetTime):
// the renderer shows the frames that end before the next tick for EVERY locomotion clip, not only for action clips. A walking clip moves the ant by the displacement of each frame that
// ended (its last frame stays: the step callback decides what comes next), an idle clip loops.
void test_subtick_locomotion(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[ants] sub-tick prediction of walking and idle clips: the frames that end before the next tick are shown\n");
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
    r.pin_animation_clock(0);
    SDL_Renderer* sr = r.get_sdl_renderer();
    static const uint8_t offsets[4] = { 60, 40, 20, 0 };
    struct Case { const char* clip; bool mirrored; sim::UnitState state; uint16_t frame; uint16_t left_ms; float sub_s; size_t expect_frame; int expect_dx; int expect_dy; };
    const Case cases[] = {
        // sand (agws301): 12 frames of 40 ms, 4 px south each
        {"agws301", false, sim::UnitState::Walking, 0, 10, 0.020f, 1, 0, 4},      // frame 0 ended 10 ms into the wait: frame 1 shows and its 4 px are applied
        {"agws301", false, sim::UnitState::Walking, 3, 5, 0.049f, 5, 0, 8},       // two frames end inside one tick
        {"agws301", false, sim::UnitState::Walking, 3, 30, 0.049f, 4, 0, 4},
        {"agws301", false, sim::UnitState::Walking, 3, 30, 0.020f, 3, 0, 0},      // nothing ends yet
        {"agws301", false, sim::UnitState::Walking, 11, 10, 0.040f, 11, 0, 0},    // the last frame stays
        // mud (agwm301: 60 ms, 2 px): a frame that outlasts the tick changes nothing
        {"agwm301", false, sim::UnitState::Walking, 2, 55, 0.049f, 2, 0, 0},
        {"agwm301", false, sim::UnitState::Walking, 2, 10, 0.049f, 3, 0, 2},
        // grass (agwg301: 50 ms, 4 px): one frame per tick
        {"agwg301", false, sim::UnitState::Walking, 5, 50, 0.049f, 5, 0, 0},
        {"agwg301", false, sim::UnitState::Walking, 5, 1, 0.049f, 6, 0, 4},
        // idle (agst301: 150 ms frames, frames 8 and 9 of 75 ms): the loop starts again after its last frame
        {"agst301", false, sim::UnitState::Idle, 11, 20, 0.040f, 0, 0, 0},
        {"agst301", false, sim::UnitState::Idle, 7, 10, 0.049f, 8, 0, 0},
        {"agst301", false, sim::UnitState::Idle, 8, 30, 0.049f, 9, 0, 0},
        // a mirrored clip (south-west is the stored south-east clip mirrored): the displacement is mirrored too
        {"agwg201", true, sim::UnitState::Walking, 2, 10, 0.030f, 3, -3, 3},
    };
    for (const Case& c : cases) {
        const int32_t chd = arc.find_animation_id(c.clip);
        check(chd >= 0, std::string("clip exists: ") + c.clip);
        if (chd < 0) continue;
        sim::WorldState ws;
        ws.width = level.width;
        ws.height = level.height;
        sim::AntSnapshot ant{};
        ant.id = 1; ant.player_id = 1; ant.type = sim::AntType::Worker;
        ant.px = 300; ant.py = 200; ant.tile_x = 9; ant.tile_y = 6;
        ant.facing = 4;
        ant.hp = 10; ant.max_hp = 10;
        ant.anim_state = static_cast<uint16_t>(c.state);
        ant.state = c.state;
        ant.loco_clip = static_cast<uint16_t>(chd);
        ant.loco_mirrored = c.mirrored;
        ant.loco_frame = c.frame;
        ant.loco_left_ms = c.left_ms;
        ws.ants.push_back(ant);
        r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.render_world(ws, grid, -1, {}, false, false, -1, -1, -1, c.sub_s);
        const Image got = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);

        // the model: the stored animation (or its mirrored copy) at the expected frame, displaced by the expected amount
        const assets::AnimationSequence* model_seq = &arc.get_animation(static_cast<uint32_t>(chd));
        if (c.mirrored) {
            const std::string stored = model_seq->name;
            const char digit = stored[stored.size() - 3];
            const int mirrored_dir = (digit == '2') ? 5 : (digit == '9') ? 6 : 7;
            model_seq = arc.get_directional_animation(stored.substr(0, stored.size() - 3), static_cast<assets::Direction>(mirrored_dir));
        }
        Image full = model_map(arc, level, 0);
        auto shifted = arc.get_palette();
        for (size_t i = 0; i < 256; ++i) if (i != assets::CHD_COLOR_KEY_INDEX) shifted[i] = arc.get_palette()[(i + offsets[ant.player_id]) & 0xFF];
        model_draw_frame(full, arc, model_seq->subitems[c.expect_frame], ant.px + c.expect_dx, ant.py + c.expect_dy, c.mirrored, shifted);
        bool same = true;
        for (int y = 0; y < PLAYFIELD_H && same; ++y)
            for (int x = 0; x < PLAYFIELD_W && same; ++x) same = std::memcmp(got.at(x, y), full.at(x, y), 3) == 0;
        check(same, std::string(c.clip) + (c.mirrored ? " (mirrored)" : "") + " frame " + std::to_string(c.frame) + " with " + std::to_string(c.left_ms) + " ms left, +" +
                    std::to_string(c.sub_s * 1000.0f) + " ms: frame " + std::to_string(c.expect_frame) + " at (" + std::to_string(c.expect_dx) + ", " + std::to_string(c.expect_dy) + ")");
    }
    r.unpin_animation_clock();
}

// The selection marker and the health number follow the sprite, which the real-time player moves when a frame ends: they use the predicted position as well (the audit's LA NEW-3
// found them at the tick's position, 50 ms behind a thrown ant). The display loop skips a frozen ant, its number included (FUN_0101b802 is what draws both).
void test_marker_and_digit_position(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[ears] the marker and the health number of a walking ant follow its predicted position\n");
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
    SDL_Renderer* sr = r.get_sdl_renderer();
    const int32_t walk = arc.find_animation_id("agws301");          // sand: 40 ms frames, 4 px south each
    const int32_t dogears = arc.find_animation_id("dogears");
    check(walk >= 0 && dogears >= 0, "clips exist: agws301, dogears");
    if (walk < 0 || dogears < 0) return;
    const auto& walk_seq = arc.get_animation(static_cast<uint32_t>(walk));
    const auto& ears_seq = arc.get_animation(static_cast<uint32_t>(dogears));
    auto snapshot = [&](bool frozen) {
        sim::AntSnapshot a{};
        a.id = 1; a.player_id = 0; a.type = sim::AntType::Worker;
        a.px = 300; a.py = 200; a.tile_x = 9; a.tile_y = 6; a.facing = 4; a.hp = 10; a.max_hp = 10;
        a.anim_state = static_cast<uint16_t>(sim::UnitState::Walking);
        a.state = sim::UnitState::Walking;
        a.loco_clip = static_cast<uint16_t>(walk); a.loco_frame = 0; a.loco_left_ms = 10;
        a.frozen = frozen;
        return a;
    };
    auto render = [&](const sim::AntSnapshot& ant, const std::vector<uint32_t>& selected, bool show_hp, float sub_s) {
        r.pin_animation_clock(0);
        r.set_show_hp(show_hp);
        sim::WorldState ws;
        ws.width = level.width;
        ws.height = level.height;
        ws.ants.push_back(ant);
        r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.render_world(ws, grid, -1, selected, false, false, -1, -1, -1, sub_s);
        r.set_show_hp(false);
        r.unpin_animation_clock();
        return read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);
    };
    static const uint8_t offsets[4] = { 60, 40, 20, 0 };
    auto shifted = arc.get_palette();
    for (size_t i = 0; i < 256; ++i) if (i != assets::CHD_COLOR_KEY_INDEX) shifted[i] = arc.get_palette()[(i + offsets[0]) & 0xFF];
    auto same_image = [&](const Image& a, const Image& b) {
        for (int y = 0; y < PLAYFIELD_H; ++y) for (int x = 0; x < PLAYFIELD_W; ++x) if (std::memcmp(a.at(x, y), b.at(x, y), 3) != 0) return false;
        return true;
    };
    // the marker: frame 0 of dogears at the predicted position (the ant has moved 4 px south at 20 ms, its frame 1 shows)
    {
        const Image got = render(snapshot(false), {1}, false, 0.020f);
        Image full = model_map(arc, level, 0);
        model_draw_frame(full, arc, walk_seq.subitems[1], 300, 204, false, shifted);
        model_draw_frame(full, arc, ears_seq.subitems[0], 300, 204, false, arc.get_palette());
        check(same_image(got, full), "the ears are drawn at the ant's predicted position (300, 204), not at its tick position (300, 200)");
    }
    // the number: the white pixels that it adds are those of the fixed font at the predicted position
    {
        const Image plain = render(snapshot(false), {}, false, 0.020f);
        const Image with = render(snapshot(false), {}, true, 0.020f);
        SDL_SetRenderDrawColor(sr, 0, 0, 0, 255);
        SDL_RenderClear(sr);
        r.draw_fixed_text("10", PLAYFIELD_X + 300, PLAYFIELD_Y + 204, assets::ColorRGBA{255, 255, 255, 255});
        const Image digits = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);
        long added = 0, expected = 0, wrong = 0;
        for (int y = 0; y < PLAYFIELD_H; ++y) {
            for (int x = 0; x < PLAYFIELD_W; ++x) {
                const bool is_new = std::memcmp(plain.at(x, y), with.at(x, y), 3) != 0;
                const bool white = digits.at(x, y)[0] == 255 && digits.at(x, y)[1] == 255 && digits.at(x, y)[2] == 255;
                if (is_new) ++added;
                if (white) ++expected;
                if (is_new != white) ++wrong;
            }
        }
        check(added > 0 && expected > 0, "the health number adds pixels");
        check(wrong == 0, "the health number is drawn at the predicted position (300, 204): " + std::to_string(wrong) + " pixels differ");
    }
    // a frozen ant is skipped by the display loop: no number either (only its burn overlay is drawn)
    {
        sim::AntSnapshot frozen = snapshot(true);
        frozen.burn_elapsed_ms = 0;
        const Image plain = render(frozen, {}, false, 0.0f);
        const Image with = render(frozen, {}, true, 0.0f);
        check(same_image(plain, with), "a frozen ant shows no health number");
    }
}

// A dying ant plays death1 .. death4 on its own sprite (Ants.exe 0x101b3fd SetAction(0xC)): the renderer draws the ant through that clip, with the same sub-tick stepping as any
// other, and the ant keeps its hit-point number until it is removed (the number is drawn by the ant's own draw function). There is no separate effect any more.
void test_dying_ant(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[ants] a dying ant is drawn through its own death clip\n");
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
    r.pin_animation_clock(0);
    SDL_Renderer* sr = r.get_sdl_renderer();
    struct Case { const char* clip; uint16_t frame; uint16_t left_ms; float sub_s; };
    const Case cases[] = {
        {"death1", 3, 80, 0.030f},          // frame 3 (200 ms) is still on
        {"death1", 3, 20, 0.030f},          // it ends 20 ms into the wait: frame 4 shows
        {"death1", 10, 10, 0.049f},         // the last frame stays until the tick, where the ant is removed
        {"death4", 0, 60, 0.0f},
        {"death4", 1, 5, 0.049f},           // frame 1 ends after 5 ms, frame 2 (80 ms) outlasts the tick
    };
    auto render = [&](const Case& c, int32_t clip, bool show_hp) {
        r.set_show_hp(show_hp);
        sim::WorldState ws;
        ws.width = level.width;
        ws.height = level.height;
        sim::AntSnapshot ant{};
        ant.id = 1; ant.player_id = 3; ant.type = sim::AntType::Worker;
        ant.px = 300; ant.py = 200; ant.tile_x = 9; ant.tile_y = 6; ant.facing = 4;
        ant.hp = 0; ant.max_hp = 10;
        ant.anim_state = static_cast<uint16_t>(sim::UnitState::Dead);
        ant.state = sim::UnitState::Dead;
        ant.loco_clip = static_cast<uint16_t>(clip);
        ant.loco_frame = c.frame;
        ant.loco_left_ms = c.left_ms;
        ws.ants.push_back(ant);
        r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.render_world(ws, grid, -1, {}, false, false, -1, -1, -1, c.sub_s);
        r.set_show_hp(false);
        return read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);
    };
    for (const Case& c : cases) {
        const int32_t chd = arc.find_animation_id(c.clip);
        check(chd >= 0, std::string("clip exists: ") + c.clip);
        if (chd < 0) continue;
        const auto& seq = arc.get_animation(static_cast<uint32_t>(chd));
        // the frame that the real-time player shows, by an independent walk along the frame times
        int32_t left = c.left_ms;
        int32_t t = static_cast<int32_t>(c.sub_s * 1000.0f);
        size_t frame = c.frame;
        while (t >= left && frame + 1 < seq.subitems.size()) { t -= left; ++frame; left = static_cast<int32_t>(seq.subitems[frame].val3); }

        const Image plain = render(c, chd, false);
        Image full = model_map(arc, level, 0);
        model_draw_frame(full, arc, seq.subitems[frame], 300, 200, false, arc.get_palette());
        bool same = true;
        for (int y = 0; y < PLAYFIELD_H && same; ++y)
            for (int x = 0; x < PLAYFIELD_W && same; ++x) same = std::memcmp(plain.at(x, y), full.at(x, y), 3) == 0;
        check(same, std::string(c.clip) + " frame " + std::to_string(c.frame) + " with " + std::to_string(c.left_ms) + " ms left, +" + std::to_string(c.sub_s * 1000.0f) + " ms: frame " + std::to_string(frame));

        // a dying ant keeps its hit-point number ("0") until it is removed: the pixels that it adds are the fixed font's at the ant's position
        const Image with = render(c, chd, true);
        SDL_SetRenderDrawColor(sr, 0, 0, 0, 255);
        SDL_RenderClear(sr);
        r.draw_fixed_text("0", PLAYFIELD_X + 300, PLAYFIELD_Y + 200, assets::ColorRGBA{255, 255, 255, 255});
        const Image digit = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);
        long added = 0, wrong = 0;
        for (int y = 0; y < PLAYFIELD_H; ++y) {
            for (int x = 0; x < PLAYFIELD_W; ++x) {
                const bool is_new = std::memcmp(plain.at(x, y), with.at(x, y), 3) != 0;
                const bool white = digit.at(x, y)[0] == 255 && digit.at(x, y)[1] == 255 && digit.at(x, y)[2] == 255;
                if (is_new) ++added;
                if (is_new != white && !(white && std::memcmp(plain.at(x, y), digit.at(x, y), 3) == 0)) ++wrong;   // (a number pixel over a white pixel of the art changes nothing)
            }
        }
        check(added > 0 && wrong == 0, std::string(c.clip) + ": a dying ant still shows its hit-point number (" + std::to_string(wrong) + " wrong pixels)");
    }
    r.unpin_animation_clock();
}

// Selection markers are children of the view container: drawn after the whole map (over lower ants), thresholds dogears
// hp >= 9 / yelears / redears hp <= 2, each with its own clock that restarts when the health changes.
void test_selection_markers(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[ears] selection markers: view child order, hp thresholds, own restarting clock\n");
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
    SDL_Renderer* sr = r.get_sdl_renderer();
    const int32_t stand = arc.find_animation_id("agst301");
    const auto* stand_seq = arc.find_animation("agst301");
    if (stand < 0 || !stand_seq) { check(false, "agst301 exists"); return; }
    static const uint8_t offsets[4] = { 60, 40, 20, 0 };
    auto shifted_for = [&](uint8_t team) {
        auto pal = arc.get_palette();
        auto sh = pal;
        for (size_t i = 0; i < 256; ++i) if (i != assets::CHD_COLOR_KEY_INDEX) sh[i] = pal[(i + offsets[team]) & 0xFF];
        return sh;
    };
    auto make_ant = [&](uint32_t id, uint8_t player, int32_t py, uint16_t hp) {
        sim::AntSnapshot a{};
        a.id = id; a.player_id = player; a.type = sim::AntType::Worker; a.px = 176; a.py = py;
        a.tile_x = 5; a.tile_y = py / 32; a.facing = 4; a.hp = hp; a.max_hp = 10;
        a.loco_clip = static_cast<uint16_t>(stand); a.loco_frame = 0; a.loco_mirrored = false;
        a.loco_left_ms = 150;                                       // frame 0 of agst301 has just started (a snapshot never holds a frame whose end is due)
        a.anim_state = static_cast<uint16_t>(sim::UnitState::Idle);
        a.state = sim::UnitState::Idle;
        return a;
    };
    struct Step { uint32_t pin; uint16_t hp; uint32_t ears_id; uint32_t elapsed; };
    const Step steps[] = {
        {  0, 10, 58,   0 },    // created at selection: dogears frame 0
        { 300, 10, 58, 300 },   // 300 ms later: frame 1 of 4 x 250 ms
        { 600,  5, 60,   0 },   // health change: re-created, yelears restarts
        { 700,  5, 60, 100 },   // 100 ms into yelears (60, 125, 125, 125): second frame
        { 800,  2, 61,   0 },   // redears (hp <= 2) restarts
        { 900,  9, 58,   0 },   // hp 9 is still green
    };
    long bad = 0;
    std::string first_bad;
    for (const auto& st : steps) {
        r.pin_animation_clock(st.pin);
        sim::WorldState ws;
        ws.width = level.width;
        ws.height = level.height;
        ws.ants.push_back(make_ant(1, 0, 176, st.hp));
        ws.ants.push_back(make_ant(2, 1, 196, 10));    // lower on screen: y-sorted after the selected ant
        r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        r.render_world(ws, grid, -1, { 1 }, false, false, -1, -1, -1, 0.0f);
        const Image got = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);

        Image full = model_map(arc, level, 0);
        model_draw_frame(full, arc, stand_seq->subitems[0], 176, 176, false, shifted_for(0));
        model_draw_frame(full, arc, stand_seq->subitems[0], 176, 196, false, shifted_for(1));
        const auto& ears = arc.get_animation(st.ears_id);
        model_draw_frame(full, arc, ears.subitems[Renderer::get_anim_subitem_by_time(ears, st.elapsed)], 176, 176, false,
                         arc.get_palette());
        bool same = true;
        for (int y = 0; y < PLAYFIELD_H && same; ++y)
            for (int x = 0; x < PLAYFIELD_W && same; ++x) same = std::memcmp(got.at(x, y), full.at(x, y), 3) == 0;
        if (!same) { ++bad; if (first_bad.empty()) first_bad = "t=" + std::to_string(st.pin) + " hp=" + std::to_string(st.hp); }
    }
    check(bad == 0, "selection marker frames differing from the model: " + std::to_string(bad) + " (first: " + first_bad + ")");
    r.unpin_animation_clock();
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

// Effect lifetimes hard-coded in the simulation must equal the Table-4 frame-duration sums.
void test_effect_specs(const assets::AssetArchive& arc) {
    std::printf("[effects] simulation effect lifetimes equal the Table-4 durations\n");
    auto total = [&](const char* name) -> uint32_t {
        const auto* seq = arc.find_animation(name);
        if (!seq) { check(false, std::string("animation exists: ") + name); return 0; }
        uint32_t t = 0;
        for (const auto& s : seq->subitems) t += s.val3;
        return t;
    };
    check(total("bombex") == sim::effect_spec::kBombexMs, "bombex lifetime");
    check(total("sputter") == sim::effect_spec::kSputterMs, "sputter lifetime");
    check(total("bsputter") == sim::effect_spec::kBsputterMs, "bsputter lifetime");
    check(total("dsplash") == sim::effect_spec::kDsplashMs, "dsplash lifetime");
    check(total("battle") == sim::effect_spec::kBattleMs, "battle cycle");
    static const char* death[4] = { "death1", "death2", "death3", "death4" };
    for (int i = 0; i < 4; ++i) check(total(death[i]) == sim::effect_spec::kDeathMs[i], std::string(death[i]) + " lifetime");
    static const char* droppers[5] = { "FD_BOMB", "FD_COMB", "FD_THIEF", "FD_SWIM", "FD_FIRE" };
    for (const char* name : droppers) {
        check(total(name) == sim::effect_spec::kDropperMs, std::string(name) + " lifetime");
        const auto* seq = arc.find_animation(name);
        if (!seq || seq->subitems.size() != 9) { check(false, std::string(name) + " has 9 frames"); continue; }
        for (size_t f = 0; f < 9; ++f) check(seq->subitems[f].val3 == sim::effect_spec::kDropperFrameMs[f], std::string(name) + " frame duration");
    }
}

// Effect sprites: real-time frame selection (sim elapsed ms + sub-tick), part order, tile-top-left anchoring and the
// end of the sprite at the end of its last frame. Reference = flat map + the model's frame at that instant.
void test_effect_rendering(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[effects] effect sprites are drawn at their real-time frame\n");
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
    r.pin_animation_clock(0);
    SDL_Renderer* sr = r.get_sdl_renderer();
    const auto& pal = arc.get_palette();

    struct Case { const char* name; uint32_t duration; };
    static const Case cases[] = { {"bombex", 680}, {"sputter", 830}, {"bsputter", 1220}, {"dsplash", 460},
                                  {"death1", 920}, {"death2", 1000}, {"death3", 980}, {"death4", 600} };
    long bad = 0, compared = 0;
    std::string first_bad;
    for (const auto& c : cases) {
        const auto* seq = arc.find_animation(c.name);
        if (!seq) { check(false, std::string("animation exists: ") + c.name); continue; }
        for (uint32_t elapsed = 0; elapsed <= c.duration + 50; elapsed += 50) {
            for (float sub : { 0.0f, 0.030f }) {
                sim::WorldState ws;
                ws.width = level.width;
                ws.height = level.height;
                sim::VisualEffect fx;
                fx.anim_name = c.name;
                fx.px = 96; fx.py = 64;                       // a tile top-left, or a pixel for death
                fx.duration_ms = c.duration;
                fx.total_frames = static_cast<uint16_t>((c.duration + 49) / 50);
                fx.elapsed_ms = elapsed;
                fx.frame = static_cast<uint16_t>(elapsed / 50);
                fx.y_key = fx.py;
                ws.effects.push_back(fx);

                r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
                SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
                SDL_RenderClear(sr);
                r.render_world(ws, grid, -1, {}, false, false, -1, -1, -1, sub);
                const Image got = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);

                Image full = model_map(arc, level, 0);
                const uint32_t sub_ms = static_cast<uint32_t>(std::clamp(sub, 0.0f, 0.0499f) * 1000.0f);
                const uint32_t t = elapsed + sub_ms;
                if (t < c.duration) {
                    uint32_t end = 0; size_t frame = seq->subitems.size() - 1;
                    for (size_t i = 0; i < seq->subitems.size(); ++i) { end += seq->subitems[i].val3; if (t < end) { frame = i; break; } }
                    model_draw_frame(full, arc, seq->subitems[frame], fx.px, fx.py, false, pal);
                }
                bool same = true;
                for (int y = 0; y < PLAYFIELD_H && same; ++y)
                    for (int x = 0; x < PLAYFIELD_W && same; ++x)
                        same = std::memcmp(got.at(x, y), full.at(x, y), 3) == 0;
                ++compared;
                if (!same) { ++bad; if (first_bad.empty()) first_bad = std::string(c.name) + " t=" + std::to_string(t); }
            }
        }
    }
    check(compared > 100, "compared effect frames (" + std::to_string(compared) + ")");
    check(bad == 0, "effect frames differing from the model: " + std::to_string(bad) + " (first: " + first_bad + ")");
    r.unpin_animation_clock();
}

// Score bubbles: signed number layout of Ants.exe FUN_01010452 (6 slots of 9 px, leading zeros skipped but advancing,
// the sign in the slot of the first significant digit, digits shifted one slot right) moving 5 px per 20 ms step.
void test_score_bubbles(Renderer& r, const assets::AssetArchive& arc) {
    std::printf("[score bubbles] signed numbers at the original layout\n");
    assets::LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) { check(false, "load TINY"); return; }
    const int32_t g = arc.find_animation_id("g01a");
    if (level.tile_dictionary.size() <= static_cast<size_t>(g)) level.tile_dictionary.resize(static_cast<size_t>(g) + 1, ".");
    level.tile_dictionary[static_cast<size_t>(g)] = "g01a";
    for (auto& c : level.layer1_terrain) { c.tile_index = static_cast<uint16_t>(g); c.flags = 0; c.properties = 0; }
    for (auto& c : level.layer2_interactive) { c.tile_index = assets::LVL_EMPTY_TILE; c.flags = 0; c.properties = 0; }
    level.anthill_spawns.clear(); level.food_schedules.clear(); level.waypoints.clear();
    sim::Grid grid;
    grid.init_from_level(level);
    r.set_level(level);
    r.pin_animation_clock(0);
    SDL_Renderer* sr = r.get_sdl_renderer();
    const auto& pal = arc.get_palette();

    auto draw_id = [&](Image& im, const char* name, int x, int y) {
        const auto* seq = arc.find_animation(name);
        if (seq && !seq->subitems.empty()) model_draw_frame(im, arc, seq->subitems[0], x, y, false, pal);
    };
    long bad = 0, compared = 0;
    std::string first_bad;
    for (int amount : { 200, -200, 50, -50, 7, -1000, 123456 }) {
        for (uint32_t elapsed : { 0u, 40u, 200u, 380u }) {
            sim::WorldState ws;
            ws.width = level.width; ws.height = level.height;
            ws.score_bubbles.push_back(sim::ScoreBubble{96, 160, amount, elapsed});
            r.camera().x = 0; r.camera().y = 0; r.camera().clamp_to_bounds(level.width, level.height);
            SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
            SDL_RenderClear(sr);
            r.render_world(ws, grid, -1);
            const Image got = read_region(sr, PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H);

            Image ref = model_map(arc, level, 0);
            const int steps = std::min<int>(20, static_cast<int>(elapsed / 20) + 1);
            const int y = 160 + (amount > 0 ? -5 : 5) * steps;
            int value = std::abs(amount);
            int divisor = 100000, x = 96;
            bool leading = true, sign_pending = true;
            while (divisor > 0) {
                const int digit = value / divisor;
                value %= divisor;
                if (digit != 0 || divisor == 1) leading = false;
                if (!leading) {
                    if (sign_pending) { draw_id(ref, amount > 0 ? "plus" : "minus", x, y); x += 9; sign_pending = false; }
                    draw_id(ref, (std::string("dig") + std::to_string(digit)).c_str(), x, y);
                }
                divisor /= 10;
                x += 9;
            }
            bool same = true;
            for (int py = 0; py < PLAYFIELD_H && same; ++py)
                for (int px = 0; px < PLAYFIELD_W && same; ++px)
                    same = std::memcmp(got.at(px, py), ref.at(px, py), 3) == 0;
            ++compared;
            if (!same) { ++bad; if (first_bad.empty()) first_bad = std::to_string(amount) + " at " + std::to_string(elapsed) + " ms"; }
        }
    }
    check(compared == 28, "compared bubble renders");
    check(bad == 0, "score bubble renders differing from the model: " + std::to_string(bad) + " (first: " + first_bad + ")");
    r.unpin_animation_clock();
}

// Text sizes: the original's label fonts are GDI cell heights (12, 14, 18, 20, 24, 35, Ants.exe FUN_0102b05f); the renderer opens its font so that the cell
// of every FontSize is that many pixels tall, and the letters really are that big (not the three fixed tiers of 9 / 11 / 13 px em that it used before).
void test_text_sizes(Renderer& r) {
    std::printf("[text] font sizes are the original's cell heights\n");
    SDL_Renderer* sr = r.get_sdl_renderer();
    const FontSize sizes[] = {FontSize::Px12, FontSize::Px14, FontSize::Px18, FontSize::Px20, FontSize::Px24, FontSize::Px35};
    int32_t last_width = 0;
    int32_t ink12 = 0;
    for (FontSize s : sizes) {
        const int32_t h = font_cell_height(s);
        check(r.get_text_height(s) == h, "text height of size " + std::to_string(h));
        const int32_t w = r.get_text_width("Waiting for others...", s);
        check(w > last_width, "text width grows with size " + std::to_string(h));
        last_width = w;
        // the ink of a capital H is 55 - 72 % of the cell (the cap height of a text face is about 0.64 of its cell)
        SDL_SetRenderDrawColor(sr, 0, 0, 0, 255);
        SDL_RenderClear(sr);
        r.draw_text("H", 20, 20, assets::ColorRGBA{255, 255, 255, 255}, s);
        const Image im = read_region(sr, 0, 0, 100, 100);
        int top = 1000, bottom = -1;
        for (int y = 0; y < im.h; ++y) {
            for (int x = 0; x < im.w; ++x) {
                if (im.at(x, y)[0] > 128) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        }
        const int ink = bottom - top + 1;
        check(bottom >= 0 && ink * 100 >= h * 55 && ink * 100 <= h * 72 + 100, "a capital H of size " + std::to_string(h) + " is " + std::to_string(ink) + " px tall");
        if (h == 12) ink12 = ink;
        if (h == 35) check(ink * 12 >= ink12 * 35 * 90 / 100 && ink * 12 <= ink12 * 35 * 110 / 100, "the letters grow in proportion to the cell (12 -> 35)");
    }
    // the text of a multi-line string advances by the cell height
    const int32_t line = font_cell_height(FontSize::Px24);
    SDL_SetRenderDrawColor(sr, 0, 0, 0, 255);
    SDL_RenderClear(sr);
    r.draw_text("H\nH", 10, 10, assets::ColorRGBA{255, 255, 255, 255}, FontSize::Px24);
    const Image two = read_region(sr, 0, 0, 60, 120);
    int first_top = -1, second_top = -1;
    for (int y = 0; y < two.h; ++y) {
        bool row = false;
        for (int x = 0; x < two.w; ++x) row = row || two.at(x, y)[0] > 128;
        if (row && first_top < 0) first_top = y;
        if (row && first_top >= 0 && y >= first_top + line - 2 && second_top < 0) second_top = y;
    }
    check(first_top >= 0 && second_top - first_top >= line - 2 && second_top - first_top <= line + 2, "two lines of size 24 are one 24 px cell apart");
}

// The health number: the fixed 8 x 15 system font, white, not antialiased, top left at the position (Ants.exe FUN_0101b802 -> TextOutA with SYSTEM_FIXED_FONT)
void test_fixed_digits(Renderer& r) {
    std::printf("[hp] the health number in the fixed 8 x 15 system font\n");
    SDL_Renderer* sr = r.get_sdl_renderer();
    const int ox = 30, oy = 30;
    auto draw = [&](const std::string& text) {
        SDL_SetRenderDrawColor(sr, 0, 0, 0, 255);
        SDL_RenderClear(sr);
        r.draw_fixed_text(text, ox, oy, assets::ColorRGBA{255, 255, 255, 255});
        return read_region(sr, 0, 0, 120, 60);
    };
    auto lit = [&](const Image& im, int cell, int x_from, int x_to, int y_from, int y_to) {
        int n = 0;
        for (int y = y_from; y <= y_to; ++y) {
            for (int x = x_from; x <= x_to; ++x) n += im.at(ox + cell * 8 + x, oy + y)[0] > 128 ? 1 : 0;
        }
        return n;
    };
    int lit_of[10] = {};
    for (int d = 0; d < 10; ++d) {
        const Image im = draw(std::string(1, static_cast<char>('0' + d)));
        const int inside = lit(im, 0, 0, 6, 2, 11);
        lit_of[d] = inside;
        check(inside >= 9 && inside <= 45, "digit " + std::to_string(d) + " has " + std::to_string(inside) + " lit pixels in its 7 x 10 body");
        check(lit(im, 0, 0, 7, 0, 1) == 0 && lit(im, 0, 0, 7, 12, 14) == 0 && lit(im, 0, 7, 7, 0, 14) == 0, "digit " + std::to_string(d) + " stays inside rows 2 - 11 and columns 0 - 6");
        check(lit(im, 1, 0, 7, 0, 14) == 0 && lit(im, 2, 0, 7, 0, 14) == 0, "digit " + std::to_string(d) + " draws nothing in the next cells");
        bool pure = true;                                        // no antialiasing: every pixel is black or white
        for (int y = 0; y < im.h; ++y) {
            for (int x = 0; x < im.w; ++x) {
                const uint8_t* p = im.at(x, y);
                pure = pure && ((p[0] == 0 && p[1] == 0 && p[2] == 0) || (p[0] == 255 && p[1] == 255 && p[2] == 255));
            }
        }
        check(pure, "digit " + std::to_string(d) + " is drawn without antialiasing");
    }
    check(lit_of[8] > lit_of[1] && lit_of[0] > lit_of[1], "the glyphs differ (8 and 0 have more pixels than 1)");
    // a number takes one 8 px cell per character
    const Image hundred = draw("100");
    check(lit(hundred, 0, 0, 7, 0, 14) > 0 && lit(hundred, 1, 0, 7, 0, 14) > 0 && lit(hundred, 2, 0, 7, 0, 14) > 0 && lit(hundred, 3, 0, 7, 0, 14) == 0,
          "\"100\" fills three cells (24 px) and nothing beyond");
    check(lit(hundred, 1, 0, 7, 0, 14) == lit(hundred, 2, 0, 7, 0, 14), "the two zeros of \"100\" are the same glyph");
    // a minus sign is one row of pixels; a character without a glyph draws nothing but still takes its cell
    const Image minus = draw("-");
    check(lit(minus, 0, 0, 7, 0, 14) == 5, "the minus sign is five pixels wide");
    const Image skipped = draw("a1");
    check(lit(skipped, 0, 0, 7, 0, 14) == 0 && lit(skipped, 1, 0, 7, 0, 14) == lit_of[1], "a character without a glyph takes its cell and draws nothing");
    check(Renderer::kFixedCellW == 8 && Renderer::kFixedCellH == 15, "the fixed font's cell is 8 x 15");
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
    test_effect_specs(arc);
    test_ant_colour_rule(arc);
    test_part_order_all_frames(r, arc);
    test_ant_sprite_pixels(r, arc);
    test_mirrored_draw(r, arc);
    test_holding_attack(r, arc);
    test_frozen_ant(r, arc);
    test_burn_overlay_layer(r, arc);
    test_blown_ant_edge(r, arc);
    test_subtick_prediction(r, arc);
    test_subtick_locomotion(r, arc);
    test_selection_markers(r, arc);
    test_marker_and_digit_position(r, arc);
    test_dying_ant(r, arc);
    test_map_layers(r, arc);
    test_fog_objects(r, arc);
    test_food_fog_footprint(r);
    test_dynamic_items(r, arc);
    test_effect_rendering(r, arc);
    test_score_bubbles(r, arc);
    test_food_stages(r, arc);
    test_text_sizes(r);
    test_fixed_digits(r);

    r.shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::printf("\nrender parity: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
