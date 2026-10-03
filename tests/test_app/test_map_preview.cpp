// The map preview of the wide setup screen as the game's own picture of the map (include/ants_app/map_preview.hpp, Renderer::render_world_image in
// src/ants_app/renderer_world_image.cpp, MapSelectScreen::preview_of in src/ants_app/map_select_wide.cpp):
//   1. the filter: box_downscale against an independent oracle (a brute-force area average in doubles with std::pow, written here), flat colours exact, linear-light averaging (a black and
//      white checker is 188, not 128), fractional weights, a lone bright pixel, enlarging, degenerate arguments, and the real world image of a shipped map;
//   2. the fit: the longer side of the map fills the box, the shorter keeps the proportion; a NON-SQUARE level (a 60 x 40 piece of GAUNTLET, written as a real .LVL by a writer that is
//      checked by a round trip) is 300 x 200, centred on black on the real screen;
//   3. the render: the world image is the game's own drawing (the same pixels as Renderer::render_world puts on the match screen, window by window), the live renderer's level, camera,
//      layout and clip are put back, the picture does not depend on the clock, a world that does not fit one render target is drawn in tiles with the same pixels, a failing device fails;
//   4. the engine: the preview's simulation is a new one (a running match's state hash does not move), and a level that only some teams can play shows those teams;
//   5. the cache: one render per map (a map shown again is not drawn again; a map that is shown in another box is), the oldest of 24 are dropped;
//   6. the fallback: a renderer that cannot draw offscreen, or fails, gets the minimap-colour picture (exactly render_map_preview_in_box's) and ONE log line; a file that is not a map has no
//      picture and no render is tried;
//   7. the six shipped maps' previews pinned by digest with the software renderer (the digests are the same on the real Metal renderer: see the commit message).
// Usage: test_map_preview [--print] [--time]   (--print writes the digest table for regeneration, never to silence a failure; --time prints the time per map). Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
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

#include "ants_app/map_preview.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/setup_layout.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"

using namespace ants;
using namespace ants::app;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";
bool g_print = false;
bool g_time = false;

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

std::string hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
    return buf;
}

void ensure_sdl() {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) SDL_Init(SDL_INIT_VIDEO);
}

/// The renderer prints the font it found on std::cout
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

/// What goes to std::cerr while it lives (failures of the checks go through the C stream stderr, which this does not touch)
class CaptureCerr {
public:
    CaptureCerr() : old_(std::cerr.rdbuf(sink_.rdbuf())) {}
    ~CaptureCerr() { std::cerr.rdbuf(old_); }
    CaptureCerr(const CaptureCerr&) = delete;
    CaptureCerr& operator=(const CaptureCerr&) = delete;
    std::string text() const { return sink_.str(); }
    size_t lines() const {
        const std::string t = sink_.str();
        return static_cast<size_t>(std::count(t.begin(), t.end(), '\n'));
    }

private:
    std::ostringstream sink_;
    std::streambuf* old_;
};

/// FNV-1a 64 over the RGB bytes of RGBA8 pixels
uint64_t rgb_digest(const std::vector<uint8_t>& rgba) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        for (size_t k = 0; k < 3; ++k) {
            h ^= rgba[i + k];
            h *= 0x100000001b3ull;
        }
    }
    return h;
}

constexpr const char* kMapsDir = ORIGINAL_ASSETS_DIR "/Maps";
const char* const kShipped[] = {"GAUNTLET", "ISLANDS", "MEDIUM", "SMALL", "TINY", "TREASURE"};

assets::LevelData load_level(const std::string& name) {
    assets::LevelData level;
    level.load_from_file(std::string(kMapsDir) + "/" + name + ".LVL");
    return level;
}

/// The real renderer on the dummy video driver (SDL's software renderer: the same pixels on every machine)
struct RendererRig {
    RendererRig(const assets::AssetArchive& archive, int32_t w, int32_t h) : width(w), height(h) {
        const QuietStdout quiet;
        ensure_sdl();
        win = SDL_CreateWindow("map-preview", 0, 0, w, h, SDL_WINDOW_HIDDEN);
        if (win == nullptr || !renderer.init(win, archive)) return;
        renderer.set_canvas_size(w, h);
        ok = true;
    }
    ~RendererRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    RendererRig(const RendererRig&) = delete;
    RendererRig& operator=(const RendererRig&) = delete;

    std::vector<uint8_t> read() {
        std::vector<uint8_t> px(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
        SDL_RenderReadPixels(renderer.get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), width * 4);
        return px;
    }

    int32_t width;
    int32_t height;
    SDL_Window* win{nullptr};
    Renderer renderer;
    bool ok{false};
};

// =====================================================================================================================================================
// 1. The filter
// =====================================================================================================================================================

struct Img {
    int32_t w{0};
    int32_t h{0};
    std::vector<uint8_t> rgba;
};

Img random_image(int32_t w, int32_t h, uint32_t seed) {
    Img img;
    img.w = w;
    img.h = h;
    img.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    uint32_t s = seed;
    for (size_t i = 0; i < img.rgba.size(); ++i) {
        s = s * 1664525u + 1013904223u;
        img.rgba[i] = (i & 3u) == 3u ? uint8_t{255} : static_cast<uint8_t>(s >> 24);
    }
    return img;
}

double srgb_to_linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double linear_to_srgb(double l) { return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055; }

/// The oracle: for every output pixel the rectangle [x0, x1) x [y0, y1) of the source (in source pixels, real numbers), every source pixel weighted by the area of it inside the rectangle,
/// the channels averaged as LINEAR light (IEC 61966-2-1) and back, rounded to nearest. Brute force, doubles, std::pow: nothing shared with box_downscale.
std::vector<uint8_t> oracle_box(const Img& src, int32_t dw, int32_t dh) {
    std::array<double, 256> lin{};
    for (size_t v = 0; v < 256; ++v) lin[v] = srgb_to_linear(static_cast<double>(v) / 255.0);
    std::vector<uint8_t> out(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4u, 255);
    for (int32_t dy = 0; dy < dh; ++dy) {
        const double y0 = static_cast<double>(dy) * src.h / dh;
        const double y1 = static_cast<double>(dy + 1) * src.h / dh;
        for (int32_t dx = 0; dx < dw; ++dx) {
            const double x0 = static_cast<double>(dx) * src.w / dw;
            const double x1 = static_cast<double>(dx + 1) * src.w / dw;
            double acc[3] = {0, 0, 0};
            double area = 0;
            for (int32_t sy = static_cast<int32_t>(std::floor(y0)); sy < static_cast<int32_t>(std::ceil(y1)) && sy < src.h; ++sy) {
                const double oy = std::min(y1, sy + 1.0) - std::max(y0, static_cast<double>(sy));
                if (oy <= 0) continue;
                for (int32_t sx = static_cast<int32_t>(std::floor(x0)); sx < static_cast<int32_t>(std::ceil(x1)) && sx < src.w; ++sx) {
                    const double ox = std::min(x1, sx + 1.0) - std::max(x0, static_cast<double>(sx));
                    if (ox <= 0) continue;
                    const double w = ox * oy;
                    area += w;
                    const uint8_t* px = &src.rgba[(static_cast<size_t>(sy) * static_cast<size_t>(src.w) + static_cast<size_t>(sx)) * 4u];
                    for (size_t c = 0; c < 3; ++c) acc[c] += w * lin[px[c]];
                }
            }
            uint8_t* dst = &out[(static_cast<size_t>(dy) * static_cast<size_t>(dw) + static_cast<size_t>(dx)) * 4u];
            for (size_t c = 0; c < 3; ++c) dst[c] = static_cast<uint8_t>(std::clamp(std::floor(255.0 * linear_to_srgb(acc[c] / area) + 0.5), 0.0, 255.0));
        }
    }
    return out;
}

struct Diff {
    int max{0};
    size_t over_one{0};
    size_t exact{0};
    size_t total{0};
};

Diff compare_rgb(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    Diff d;
    if (a.size() != b.size()) {
        d.max = 1000;
        return d;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if ((i & 3u) == 3u) continue;
        const int diff = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        d.max = std::max(d.max, diff);
        d.over_one += diff > 1 ? 1u : 0u;
        d.exact += diff == 0 ? 1u : 0u;
        ++d.total;
    }
    return d;
}

void check_against_oracle(const Img& src, int32_t dw, int32_t dh, const std::string& what) {
    const std::vector<uint8_t> got = box_downscale(src.rgba.data(), src.w, src.h, dw, dh);
    const std::vector<uint8_t> want = oracle_box(src, dw, dh);
    const Diff d = compare_rgb(got, want);
    if (g_time) std::printf("    %-24s %4d x %4d -> %3d x %3d: largest difference %d, exact %zu of %zu\n", what.c_str(), src.w, src.h, dw, dh, d.max, d.exact, d.total);
    check(got.size() == static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4u && d.over_one == 0 && d.max <= 1 && d.exact * 100 >= d.total * 97,
          what + ": " + std::to_string(src.w) + " x " + std::to_string(src.h) + " -> " + std::to_string(dw) + " x " + std::to_string(dh) + " is the oracle's picture (largest difference " + std::to_string(d.max) +
              ", " + std::to_string(d.over_one) + " channels more than 1 off, " + std::to_string(d.exact) + " of " + std::to_string(d.total) + " exact)");
    // alpha of the result is opaque whatever the source says
    bool opaque = true;
    for (size_t i = 3; i < got.size(); i += 4) opaque = opaque && got[i] == 255;
    check(opaque, what + ": the result is opaque");
}

void test_filter(const assets::AssetArchive& arc) {
    group("filter", "box_downscale against an independent area-average oracle in linear light");
    // random pictures: fractional weights on both axes, whole factors, a factor of one, enlarging, one-pixel sides
    check_against_oracle(random_image(37, 29, 1), 11, 7, "random");
    check_against_oracle(random_image(100, 60, 2), 33, 20, "random");
    check_against_oracle(random_image(64, 64, 3), 16, 16, "random, whole factor");
    check_against_oracle(random_image(41, 23, 4), 41, 23, "random, same size");
    check_against_oracle(random_image(5, 4, 5), 13, 9, "random, enlarged");
    check_against_oracle(random_image(1, 1, 6), 7, 5, "one pixel enlarged");
    check_against_oracle(random_image(50, 1, 7), 7, 1, "one row");
    check_against_oracle(random_image(1, 50, 8), 1, 9, "one column");
    check_against_oracle(random_image(97, 89, 9), 96, 88, "almost the same size");
    // a picture of the size of a world (60 x 60 cells = 1920 pixels) into the box of the single player's screen: smooth ramps with noise
    {
        Img big;
        big.w = 1920;
        big.h = 1920;
        big.rgba.resize(static_cast<size_t>(big.w) * static_cast<size_t>(big.h) * 4u);
        uint32_t s = 77;
        for (int32_t y = 0; y < big.h; ++y) {
            for (int32_t x = 0; x < big.w; ++x) {
                s = s * 1664525u + 1013904223u;
                uint8_t* px = &big.rgba[(static_cast<size_t>(y) * static_cast<size_t>(big.w) + static_cast<size_t>(x)) * 4u];
                px[0] = static_cast<uint8_t>((static_cast<uint32_t>(x * 255 / big.w) + ((s >> 24) & 31u)) & 255u);
                px[1] = static_cast<uint8_t>((static_cast<uint32_t>(y * 255 / big.h) + ((s >> 19) & 31u)) & 255u);
                px[2] = static_cast<uint8_t>(((x ^ y) & 64) != 0 ? 230 : 20);
                px[3] = 255;
            }
        }
        check_against_oracle(big, 300, 300, "a world");
        check_against_oracle(big, 248, 248, "a world");
    }
    // the real world image of a shipped map
    {
        RendererRig rig(arc, 960, 540);
        check(rig.ok, "the renderer is up");
        if (rig.ok) {
            const assets::LevelData level = load_level("GAUNTLET");
            sim::SimulationEngine eng;
            eng.init(level, 1u, 0x0F);
            sim::WorldState world = eng.get_world_state();
            world.ants.clear();
            WorldImage image;
            std::string why;
            check(rig.renderer.render_world_image(level, world, eng.grid(), image, &why) && image.valid(), "the world image of GAUNTLET is made: " + why);
            Img src;
            src.w = image.width;
            src.h = image.height;
            src.rgba = image.rgba;
            check_against_oracle(src, 300, 300, "the world of GAUNTLET");
            check_against_oracle(src, 248, 248, "the world of GAUNTLET");
        }
    }
    // flat colours stay exactly what they are, every value of every channel, through fractional weights too
    {
        bool all = true;
        for (int v = 0; v < 256; ++v) {
            Img flat;
            flat.w = 13;
            flat.h = 11;
            flat.rgba.resize(static_cast<size_t>(flat.w) * static_cast<size_t>(flat.h) * 4u);
            for (size_t i = 0; i < flat.rgba.size(); i += 4) {
                flat.rgba[i] = static_cast<uint8_t>(v);
                flat.rgba[i + 1] = static_cast<uint8_t>(255 - v);
                flat.rgba[i + 2] = static_cast<uint8_t>((v * 7) & 255);
                flat.rgba[i + 3] = 255;
            }
            const std::vector<uint8_t> out = box_downscale(flat.rgba.data(), flat.w, flat.h, 5, 3);
            for (size_t i = 0; i < out.size(); i += 4) all = all && out[i] == flat.rgba[0] && out[i + 1] == flat.rgba[1] && out[i + 2] == flat.rgba[2] && out[i + 3] == 255;
        }
        check(all, "a flat colour keeps its exact value (all 256 values of the three channels, 13 x 11 -> 5 x 3)");
    }
    // the average is taken in LINEAR light: black and white in a checker is half the light, which is 188 as sRGB, not 128 (an average of the encoded values)
    {
        const uint8_t checker[16] = {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255};
        const std::vector<uint8_t> out = box_downscale(checker, 2, 2, 1, 1);
        check(out.size() == 4 && std::abs(static_cast<int>(out[0]) - 188) <= 1 && out[0] == out[1] && out[1] == out[2], "a black and white checker is half the light: " + std::to_string(out.empty() ? -1 : out[0]) + " (188, not the 128 of an average of the encoded values)");
        // the two sides of a gamma ramp: a quarter white is not 64 (encoded) but 137 (a quarter of the light)
        const uint8_t quarter[16] = {255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
        const std::vector<uint8_t> q = box_downscale(quarter, 2, 2, 1, 1);
        check(q.size() == 4 && std::abs(static_cast<int>(q[0]) - 137) <= 1, "one white pixel in four is a quarter of the light: " + std::to_string(q.empty() ? -1 : q[0]) + " (137)");
    }
    // fractional weights: 3 pixels -> 2 (black, white, black): each output covers one and a half pixels, a third of the light is white: the two outputs are equal, and as the oracle says
    {
        const uint8_t row[12] = {0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255};
        const std::vector<uint8_t> out = box_downscale(row, 3, 1, 2, 1);
        const double want = std::floor(255.0 * linear_to_srgb(1.0 / 3.0) + 0.5);
        check(out.size() == 8 && out[0] == out[4] && std::abs(static_cast<int>(out[0]) - static_cast<int>(want)) <= 1, "3 -> 2: both outputs are a third of the light, " + std::to_string(out.empty() ? -1 : out[0]) + " (" + std::to_string(static_cast<int>(want)) + ")");
    }
    // a lone bright pixel is neither lost (point sampling) nor smeared past its cell (bilinear aliasing): 16 x 16 black with one white pixel at (5, 9) -> 4 x 4: the output cell (1, 2) holds
    // a sixteenth of the light, every other cell none
    {
        Img one;
        one.w = 16;
        one.h = 16;
        one.rgba.assign(static_cast<size_t>(16 * 16 * 4), 0);
        for (size_t i = 3; i < one.rgba.size(); i += 4) one.rgba[i] = 255;
        const size_t at = (static_cast<size_t>(9) * 16 + 5) * 4u;
        one.rgba[at] = one.rgba[at + 1] = one.rgba[at + 2] = 255;
        const std::vector<uint8_t> out = box_downscale(one.rgba.data(), 16, 16, 4, 4);
        const double want = std::floor(255.0 * linear_to_srgb(1.0 / 16.0) + 0.5);
        bool others_dark = true;
        for (int cy = 0; cy < 4; ++cy) {
            for (int cx = 0; cx < 4; ++cx) {
                const uint8_t v = out[(static_cast<size_t>(cy) * 4 + static_cast<size_t>(cx)) * 4u];
                if (cx == 1 && cy == 2) check(std::abs(static_cast<int>(v) - static_cast<int>(want)) <= 1, "the lone white pixel is a sixteenth of the light in its output pixel: " + std::to_string(v) + " (" + std::to_string(static_cast<int>(want)) + ")");
                else others_dark = others_dark && v == 0;
            }
        }
        check(others_dark, "... and nothing of it reaches the other output pixels");
    }
    // the same picture twice is the same bytes; degenerate requests are refused (no crash, an empty result)
    {
        const Img img = random_image(61, 47, 11);
        check(box_downscale(img.rgba.data(), img.w, img.h, 19, 13) == box_downscale(img.rgba.data(), img.w, img.h, 19, 13), "the filter is deterministic");
        check(box_downscale(nullptr, 4, 4, 2, 2).empty() && box_downscale(img.rgba.data(), 0, 4, 2, 2).empty() && box_downscale(img.rgba.data(), 4, 0, 2, 2).empty() &&
                  box_downscale(img.rgba.data(), 4, 4, 0, 2).empty() && box_downscale(img.rgba.data(), 4, 4, 2, -1).empty() && box_downscale(img.rgba.data(), 70000, 4, 2, 2).empty(),
              "no source, no size, a side over 65535: an empty result");
        const std::vector<uint8_t> same = box_downscale(img.rgba.data(), img.w, img.h, img.w, img.h);
        bool identical = same.size() == img.rgba.size();
        for (size_t i = 0; identical && i < same.size(); ++i) identical = same[i] == img.rgba[i];
        check(identical, "the size it already has: the same picture (every byte, every channel value goes through the tables and back)");
    }
}

// =====================================================================================================================================================
// A level of another shape: a piece of a shipped map, written as a real .LVL file
// =====================================================================================================================================================

std::vector<uint8_t> serialize_level(const assets::LevelData& level) {
    std::vector<uint8_t> b;
    auto u16 = [&](uint32_t v) {
        b.push_back(static_cast<uint8_t>(v & 0xFF));
        b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    };
    auto u32 = [&](uint32_t v) {
        u16(v & 0xFFFF);
        u16(v >> 16);
    };
    u32(level.version);
    u32(level.game_mode);
    u16(level.default_minutes);
    for (size_t i = 0; i < 30; ++i) b.push_back(i < level.description.size() ? static_cast<uint8_t>(level.description[i]) : uint8_t{0});
    u16(static_cast<uint32_t>(level.tile_dictionary.size() - 1));
    for (const std::string& name : level.tile_dictionary) {
        for (size_t i = 0; i < 11; ++i) b.push_back(i < name.size() ? static_cast<uint8_t>(name[i]) : uint8_t{0});
    }
    u32(level.height());
    u32(level.width());
    for (const assets::MapCell& c : level.layer1_terrain) {
        u16(c.tile_index);
        u16(c.flags);
        u16(c.properties);
    }
    for (const assets::MapCell& c : level.layer2_interactive) {
        u16(c.tile_index);
        u16(c.flags);
        u16(c.properties);
    }
    u16(static_cast<uint32_t>(level.anthill_spawns.size()));
    for (const assets::AnthillSpawn& s : level.anthill_spawns) {
        u16(s.tile_id);
        u16(s.y);
        u16(s.x);
    }
    u16(static_cast<uint32_t>(level.food_schedules.size()));
    for (const assets::FoodSchedule& f : level.food_schedules) {
        u16(f.y);
        u16(f.x);
        u16(f.initial_delay);
        u16(f.respawn_interval);
        u16(static_cast<uint32_t>(f.variants.size()));
        for (const assets::FoodItemVariant& v : f.variants) {
            u16(v.weight);
            u16(v.tile_id);
        }
    }
    u16(level.ambient_flag);
    u16(level.ambient_tile_or_sound);
    u16(static_cast<uint32_t>(level.waypoints.size()));
    for (const assets::Waypoint& w : level.waypoints) {
        u16(w.y);
        u16(w.x);
        u32(w.flag);
        if (w.flag != 0) {
            u32(w.param);
            for (double p : w.probabilities) {
                uint64_t bits = 0;
                std::memcpy(&bits, &p, sizeof(bits));
                u32(static_cast<uint32_t>(bits & 0xFFFFFFFFu));
                u32(static_cast<uint32_t>(bits >> 32));
            }
        }
    }
    u16(level.boundary_param);
    return b;
}

/// The top left cols x rows cells of a level, with every record that lies inside them
assets::LevelData crop_level(const assets::LevelData& base, uint32_t cols, uint32_t rows) {
    assets::LevelData c;
    c.version = base.version;
    c.game_mode = base.game_mode;
    c.default_minutes = base.default_minutes;
    c.description = "a cut of " + base.description.substr(0, 12);
    c.tile_type_count = base.tile_type_count;
    c.tile_dictionary = base.tile_dictionary;
    c.width.val = cols;
    c.height.val = rows;
    c.layer1_terrain.assign(static_cast<size_t>(cols) * rows, assets::MapCell{});
    c.layer2_interactive.assign(static_cast<size_t>(cols) * rows, assets::MapCell{});
    for (uint32_t y = 0; y < rows; ++y) {
        for (uint32_t x = 0; x < cols; ++x) {
            c.layer1_terrain[static_cast<size_t>(y) * cols + x] = base.get_cell_layer1(x, y);
            c.layer2_interactive[static_cast<size_t>(y) * cols + x] = base.get_cell_layer2(x, y);
        }
    }
    for (const assets::AnthillSpawn& s : base.anthill_spawns) {
        if (s.x < cols && s.y < rows) c.anthill_spawns.push_back(s);
    }
    for (const assets::FoodSchedule& f : base.food_schedules) {
        if (f.x < cols && f.y < rows) c.food_schedules.push_back(f);
    }
    for (const assets::Waypoint& w : base.waypoints) {
        if (w.x < cols && w.y < rows) c.waypoints.push_back(w);
    }
    c.ambient_flag = base.ambient_flag;
    c.ambient_tile_or_sound = base.ambient_tile_or_sound;
    c.boundary_param = base.boundary_param;
    return c;
}

bool same_level(const assets::LevelData& a, const assets::LevelData& b) {
    if (a.width() != b.width() || a.height() != b.height() || a.layer1_terrain.size() != b.layer1_terrain.size() || a.layer2_interactive.size() != b.layer2_interactive.size()) return false;
    for (size_t i = 0; i < a.layer1_terrain.size(); ++i) {
        const assets::MapCell& p = a.layer1_terrain[i];
        const assets::MapCell& q = b.layer1_terrain[i];
        if (p.tile_index != q.tile_index || p.flags != q.flags || p.properties != q.properties) return false;
        const assets::MapCell& r = a.layer2_interactive[i];
        const assets::MapCell& s = b.layer2_interactive[i];
        if (r.tile_index != s.tile_index || r.flags != s.flags || r.properties != s.properties) return false;
    }
    if (a.anthill_spawns.size() != b.anthill_spawns.size() || a.food_schedules.size() != b.food_schedules.size() || a.waypoints.size() != b.waypoints.size()) return false;
    for (size_t i = 0; i < a.anthill_spawns.size(); ++i) {
        if (a.anthill_spawns[i].tile_id != b.anthill_spawns[i].tile_id || a.anthill_spawns[i].x != b.anthill_spawns[i].x || a.anthill_spawns[i].y != b.anthill_spawns[i].y) return false;
    }
    for (size_t i = 0; i < a.food_schedules.size(); ++i) {
        if (a.food_schedules[i].variants.size() != b.food_schedules[i].variants.size() || a.food_schedules[i].x != b.food_schedules[i].x) return false;
    }
    return a.tile_dictionary == b.tile_dictionary && a.boundary_param == b.boundary_param;
}

/// A scratch folder that is removed again
struct TempDir {
    TempDir() {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() / ("ants_map_preview_" + std::to_string(static_cast<unsigned long long>(SDL_GetPerformanceCounter())) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignore;
        std::filesystem::remove_all(path, ignore);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    std::filesystem::path path;
};

void write_file(const std::filesystem::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// =====================================================================================================================================================
// 2. The fit
// =====================================================================================================================================================

/// A renderer that draws nothing and remembers the picture that it was asked to blit; `world` makes it a renderer that can draw offscreen: a stand-in whose world image is a known synthetic
/// picture, or one that fails
class Stub : public IRenderer {
public:
    enum class World : uint8_t { None, Synthetic, Fail };
    explicit Stub(World w = World::None) : world(w) {}
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string& text, int32_t, int32_t, assets::ColorRGBA) override { texts.push_back(text); }
    void set_hud_team(uint8_t) override {}
    void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* rgba) override {
        images.push_back({x, y, w, h, std::vector<uint8_t>(rgba, rgba + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u)});
    }
    bool render_world_image(const assets::LevelData& level, const sim::WorldState& state, const sim::Grid& grid, WorldImage& out, std::string* why = nullptr, int32_t = 0) override {
        ++render_calls;
        last_ants = state.ants.size();
        last_cells = state.cells.size();
        last_anthills = grid.anthills().size();
        last_team_spawns = 0;
        for (const assets::AnthillSpawn& sp : level.anthill_spawns) last_team_spawns += sp.team_id < 4 ? 1u : 0u;
        if (world == World::None) return IRenderer::render_world_image(level, sim::WorldState{}, sim::Grid{}, out, why, 0);
        if (world == World::Fail) {
            if (why != nullptr) *why = "the stub fails";
            return false;
        }
        out = synthetic_world(level.width() * 32u, level.height() * 32u);
        return true;
    }

    /// A known picture of a world of w x h pixels
    static WorldImage synthetic_world(uint32_t w, uint32_t h) {
        WorldImage img;
        img.width = static_cast<int32_t>(w);
        img.height = static_cast<int32_t>(h);
        img.rgba.resize(static_cast<size_t>(w) * h * 4u);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t* px = &img.rgba[(static_cast<size_t>(y) * w + x) * 4u];
                px[0] = static_cast<uint8_t>(x * 255u / w);
                px[1] = static_cast<uint8_t>(y * 255u / h);
                px[2] = static_cast<uint8_t>(((x / 40u + y / 40u) & 1u) != 0 ? 200u : 40u);
                px[3] = 255;
            }
        }
        return img;
    }

    struct Image {
        int32_t x, y, w, h;
        std::vector<uint8_t> rgba;
    };
    World world;
    int render_calls{0};
    size_t last_ants{999999};              // what the last render_world_image was given: the ants of the world state, its cells, the hills of the grid, the start markers' records
    size_t last_cells{0};
    size_t last_anthills{0};
    size_t last_team_spawns{0};            // the start markers of the four teams in the level that it was given
    std::vector<Image> images;
    std::vector<std::string> texts;
};

void test_fit(const assets::AssetArchive& arc) {
    group("fit", "the longer side of the map fills the box, the shorter keeps the proportion; a non-square level is centred on black");
    struct Case {
        uint32_t cols, rows;
        int32_t inner;
        int32_t w, h;
    };
    const Case cases[] = {
        {60, 60, 300, 300, 300}, {60, 40, 300, 300, 200}, {40, 60, 300, 200, 300}, {31, 31, 300, 300, 300}, {124, 100, 248, 248, 200}, {100, 81, 300, 300, 243},
        {1, 1, 300, 300, 300},   {200, 20, 300, 300, 30}, {16, 90, 300, 53, 300},  {100, 3, 300, 300, 9},    {250, 1, 248, 248, 1},     {60, 60, 248, 248, 248},
        {0, 5, 300, 0, 0},       {5, 0, 300, 0, 0},       {60, 60, 0, 0, 0},       {60, 60, -3, 0, 0},
    };
    for (const Case& c : cases) {
        const MapPreviewSize got = map_preview_fit(c.cols, c.rows, c.inner);
        check(got.width == c.w && got.height == c.h && got.scale == 0 && !got.sampled,
              std::to_string(c.cols) + " x " + std::to_string(c.rows) + " cells in a box of " + std::to_string(c.inner) + ": " + std::to_string(got.width) + " x " + std::to_string(got.height) + ", wanted " + std::to_string(c.w) + " x " + std::to_string(c.h));
    }
    // a non-square level, through the whole path (engine, the real renderer, the filter): its picture is 300 x 200
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer is up");
    if (!rig.ok) return;
    const assets::LevelData wide = crop_level(load_level("GAUNTLET"), 60, 40);
    // the writer is right: the level that it writes is the level that was written
    {
        const std::vector<uint8_t> bytes = serialize_level(wide);
        assets::LevelData back;
        check(back.load_from_memory(bytes.data(), bytes.size()) && same_level(wide, back), "the 60 x 40 piece of GAUNTLET is written as a .LVL file that loads as the same level (cells, hills, food, dictionary)");
        const assets::LevelData whole = load_level("GAUNTLET");
        const std::vector<uint8_t> wb = serialize_level(whole);
        assets::LevelData again;
        check(again.load_from_memory(wb.data(), wb.size()) && same_level(whole, again), "... and GAUNTLET itself survives a write and a read");
    }
    std::string why;
    const MapPreview p = render_map_preview_world(rig.renderer, wide, 300, &why);
    check(p.valid() && p.rendered && p.width == 300 && p.height == 200 && p.cols == 60 && p.rows == 40, "60 x 40 cells in the 300 box: a 300 x 200 picture of the game's own render " + why);
    check(p.players == 4, "all four hills are on it (players = " + std::to_string(p.players) + ")");
    const MapPreview tall = render_map_preview_world(rig.renderer, crop_level(load_level("GAUNTLET"), 40, 60), 248, &why);
    check(tall.valid() && tall.width == 165 && tall.height == 248, "40 x 60 cells in the 248 box: 165 x 248 (" + std::to_string(tall.width) + " x " + std::to_string(tall.height) + ")");
    // the real screen: the file is the only map of a folder; the picture is centred in the black box, the bands above and below it are black
    {
        TempDir dir;
        write_file(dir.path / "WIDE.LVL", serialize_level(wide));
        MapSelectScreen screen;
        screen.init(dir.path.string());
        screen.set_wide_layout(true);
        screen.set_player_name("Player");
        screen.enter();
        screen.update(1.66f);
        rig.renderer.begin_frame();
        screen.render(rig.renderer, arc);
        const std::vector<uint8_t> px = rig.read();
        const SetupLayout& layout = SetupLayout::of(SetupVariant::Single);
        const LayoutRect area = layout.preview_area();
        const LayoutRect pic = layout.preview_picture(p.width, p.height);
        check(pic.x == area.x && pic.y == area.y + 50 && pic.w == 300 && pic.h == 200, "the picture is centred in the box: 50 rows of black above and below");
        bool picture_same = true;
        for (int32_t y = 0; y < pic.h && picture_same; ++y) {
            for (int32_t x = 0; x < pic.w; ++x) {
                const size_t a = (static_cast<size_t>(pic.y + y) * 960u + static_cast<size_t>(pic.x + x)) * 4u;
                const size_t b = (static_cast<size_t>(y) * static_cast<size_t>(p.width) + static_cast<size_t>(x)) * 4u;
                if (px[a] != p.rgba[b] || px[a + 1] != p.rgba[b + 1] || px[a + 2] != p.rgba[b + 2]) {
                    picture_same = false;
                    break;
                }
            }
        }
        check(picture_same, "the screen shows the 300 x 200 picture pixel for pixel");
        // (the box is filled with the black of the original's frames, (7, 11, 15), not with 0, 0, 0)
        int non_black = 0;
        for (int32_t y = area.y; y < area.y + area.h; ++y) {
            for (int32_t x = area.x; x < area.x + area.w; ++x) {
                if (y >= pic.y && y < pic.y + pic.h) continue;
                const size_t a = (static_cast<size_t>(y) * 960u + static_cast<size_t>(x)) * 4u;
                non_black += (px[a] != 7 || px[a + 1] != 11 || px[a + 2] != 15) ? 1 : 0;
            }
        }
        check(non_black == 0, "the bands above and below the picture are the box's black (" + std::to_string(non_black) + " pixels are not)");
        check(screen.get_maps().size() == 1 && screen.get_maps()[0].width == 60 && screen.get_maps()[0].height == 40, "the screen lists the file as a 60 x 40 map");
    }
}

// =====================================================================================================================================================
// 3. The render
// =====================================================================================================================================================

void test_render(const assets::AssetArchive& arc) {
    group("render", "the world image is the game's own drawing, the live renderer is left as it was, the clock does not matter, tiles equal the whole, a failing device fails");
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer is up");
    if (!rig.ok) return;

    for (const char* name : {"ISLANDS", "TINY"}) {
        const assets::LevelData level = load_level(name);
        sim::SimulationEngine eng;
        eng.init(level, 1u, 0x0F);
        sim::WorldState world = eng.get_world_state();
        world.ants.clear();
        WorldImage image;
        std::string why;
        const bool ok = rig.renderer.render_world_image(level, world, eng.grid(), image, &why);
        check(ok && image.valid() && image.width == static_cast<int32_t>(level.width()) * 32 && image.height == static_cast<int32_t>(level.height()) * 32, std::string(name) + ": the world image is cols x 32 by rows x 32 pixels " + why);
        if (!ok) continue;
        bool opaque = true;
        for (size_t i = 3; i < image.rgba.size(); i += 4) opaque = opaque && image.rgba[i] == 255;
        check(opaque, std::string(name) + ": it is opaque");

        // THE GAME'S OWN DRAWING: the same world put on the match screen (the 16:9 frame's map view, 762 x 500 at (16, 21)) by Renderer::set_level + render_world, window by window
        // (no ants, no fog, no HUD, no cursor: render_world alone), shows the very pixels of the image at the camera's position
        rig.renderer.set_layout(ScreenLayout::with_size(960, 540));
        rig.renderer.set_level(level);
        rig.renderer.pin_animation_clock(0);
        const LayoutRect view = ScreenLayout::with_size(960, 540).view();
        const int32_t maxx = std::max(0, image.width - view.w);
        const int32_t maxy = std::max(0, image.height - view.h);
        const int32_t spots[][2] = {{0, 0}, {maxx, maxy}, {maxx / 2, maxy / 3}, {std::min(maxx, 333), std::min(maxy, 71)}};
        size_t wrong = 0;
        size_t compared = 0;
        for (const auto& spot : spots) {
            rig.renderer.camera().x = static_cast<float>(spot[0]);
            rig.renderer.camera().y = static_cast<float>(spot[1]);
            rig.renderer.begin_frame();
            rig.renderer.render_world(world, eng.grid(), -1);
            const std::vector<uint8_t> screen = rig.read();
            const int32_t w = std::min(view.w, image.width - spot[0]);
            const int32_t h = std::min(view.h, image.height - spot[1]);
            for (int32_t y = 0; y < h; ++y) {
                for (int32_t x = 0; x < w; ++x) {
                    const size_t a = (static_cast<size_t>(view.y + y) * 960u + static_cast<size_t>(view.x + x)) * 4u;
                    const size_t b = (static_cast<size_t>(spot[1] + y) * static_cast<size_t>(image.width) + static_cast<size_t>(spot[0] + x)) * 4u;
                    ++compared;
                    wrong += (screen[a] != image.rgba[b] || screen[a + 1] != image.rgba[b + 1] || screen[a + 2] != image.rgba[b + 2]) ? 1u : 0u;
                }
            }
        }
        check(compared > 100000 && wrong == 0, std::string(name) + ": the image's pixels are the match screen's own pixels in four windows (" + std::to_string(wrong) + " of " + std::to_string(compared) + " differ)");
        rig.renderer.unpin_animation_clock();
    }

    // THE LIVE RENDERER IS LEFT AS IT WAS: a level is set and drawn, another level's image is made in the middle, the same frame is drawn again: the same pixels; the camera, the layout and the
    // picture are the same numbers
    {
        const assets::LevelData shown = load_level("TREASURE");
        const assets::LevelData other = load_level("GAUNTLET");
        sim::SimulationEngine live;
        live.init(shown, 5u, 0x0F);
        const sim::WorldState live_world = live.get_world_state();
        rig.renderer.set_layout(ScreenLayout::with_size(960, 540));
        rig.renderer.set_picture(LayoutRect{0, 0, 960, 540});
        rig.renderer.set_level(shown);
        rig.renderer.pin_animation_clock(0);
        rig.renderer.camera().x = 400.0f;
        rig.renderer.camera().y = 300.0f;
        rig.renderer.camera().clamp_to_bounds(shown.width(), shown.height());
        const ViewportCamera camera_before = rig.renderer.camera();
        const ScreenLayout layout_before = rig.renderer.layout();
        const LayoutRect picture_before = rig.renderer.picture();
        auto frame = [&]() {
            rig.renderer.begin_frame();
            rig.renderer.render_world(live_world, live.grid(), -1);
            return rgb_digest(rig.read());
        };
        const uint64_t before = frame();
        sim::SimulationEngine other_eng;
        other_eng.init(other, 1u, 0x0F);
        WorldImage image;
        check(rig.renderer.render_world_image(other, other_eng.get_world_state(), other_eng.grid(), image) && image.valid(), "another level's image is made in the middle");
        const uint64_t after = frame();
        check(before == after, "the shown level draws the same frame after another level's image was made: " + hex64(before) + " and " + hex64(after));
        const ViewportCamera& cam = rig.renderer.camera();
        check(cam.x == camera_before.x && cam.y == camera_before.y && cam.viewport_w == camera_before.viewport_w && cam.viewport_h == camera_before.viewport_h && cam.view_x == camera_before.view_x &&
                  cam.view_y == camera_before.view_y && cam.centre_small_maps == camera_before.centre_small_maps,
              "the camera is where it was");
        check(rig.renderer.layout() == layout_before && rig.renderer.picture() == picture_before, "the layout and the picture are as they were");
        // the live frame is not affected by the clip either: a frame drawn after is clipped like the one before (same digest above); and the level that the renderer shows is the shown one
        check(SDL_GetRenderTarget(rig.renderer.get_sdl_renderer()) == nullptr, "the window is the render target again");
        // the image of the shown level is the same whatever else was set before
        sim::SimulationEngine eng_shown;
        eng_shown.init(shown, 1u, 0x0F);
        sim::WorldState w1 = eng_shown.get_world_state();
        w1.ants.clear();
        WorldImage first;
        WorldImage second;
        rig.renderer.render_world_image(shown, w1, eng_shown.grid(), first);
        rig.renderer.set_level(other);
        rig.renderer.render_world_image(shown, w1, eng_shown.grid(), second);
        check(first.valid() && first.rgba == second.rgba, "the image does not depend on the level that the renderer had set");
        rig.renderer.unpin_animation_clock();
    }

    // THE SELECTION MARKERS' CLOCKS: the ears of a selected ant and the brackets of a selected hill each have a clock that starts when they are first drawn; drawing a frame with nothing selected
    // (which is what the preview's render does) resets them. The renderer puts them back: a frame drawn after the preview is the frame that would have been drawn without it
    {
        const assets::LevelData shown = load_level("TINY");
        sim::SimulationEngine live;
        live.init(shown, 3u, 0x0F);
        live.tick();
        const sim::WorldState live_world = live.get_world_state();
        const sim::AntSnapshot* mine = nullptr;
        for (const sim::AntSnapshot& a : live_world.ants) {
            if (mine == nullptr && a.player_id == 0) mine = &a;
        }
        check(mine != nullptr, "(setup) team 0 has an ant");
        if (mine != nullptr) {
            const uint32_t ant = mine->id;
            rig.renderer.set_layout(ScreenLayout::with_size(960, 540));
            rig.renderer.set_picture(LayoutRect{0, 0, 960, 540});
            rig.renderer.set_level(shown);
            rig.renderer.camera().center_on(mine->px, mine->py, shown.width(), shown.height());
            auto frame = [&](uint32_t ms) {
                rig.renderer.pin_animation_clock(ms);
                rig.renderer.begin_frame();
                rig.renderer.render_world(live_world, live.grid(), static_cast<int32_t>(ant), {ant}, false, false, -1, -1, 0);
                return rgb_digest(rig.read());
            };
            frame(0);                                    // the markers are created at the clock's 0
            const uint64_t plain = frame(1000);          // ... and drawn 1000 ms into their life
            frame(0);
            const uint64_t at_start = frame(0);
            check(plain != at_start, "(control) the markers' pictures change over their first second");
            // the same again with a preview made in between (no selection: the preview's frame resets the clocks if they are not put back)
            rig.renderer.set_level(shown);
            frame(0);
            sim::SimulationEngine other;
            other.init(load_level("GAUNTLET"), 1u, 0x0F);
            WorldImage image;
            check(rig.renderer.render_world_image(load_level("GAUNTLET"), other.get_world_state(), other.grid(), image), "a preview is made between the frames");
            const uint64_t after = frame(1000);
            check(after == plain, "the ears and the hill's brackets are 1000 ms into their life after the preview as before it: " + hex64(plain) + " and " + hex64(after));
            rig.renderer.unpin_animation_clock();
        }
    }

    // THE CLOCK DOES NOT MATTER: every template shows its first frame (the match's start): the same image whenever it is made, whatever the renderer's clock says
    {
        const assets::LevelData level = load_level("ISLANDS");
        sim::SimulationEngine eng;
        eng.init(level, 1u, 0x0F);
        sim::WorldState world = eng.get_world_state();
        world.ants.clear();
        WorldImage a;
        WorldImage b;
        rig.renderer.render_world_image(level, world, eng.grid(), a);
        rig.renderer.pin_animation_clock(7777);                // the live clock far from its start
        SDL_Delay(1300);
        rig.renderer.render_world_image(level, world, eng.grid(), b);
        rig.renderer.unpin_animation_clock();
        check(a.valid() && a.rgba == b.rgba, "the same image 1.3 seconds and a pinned clock later");
        // ... and it is the picture of the clock's start, not of some other moment: the same world drawn by the match screen with the clock pinned elsewhere differs (the water moves)
        rig.renderer.set_layout(ScreenLayout::with_size(960, 540));
        rig.renderer.set_level(level);
        rig.renderer.camera().x = 600.0f;
        rig.renderer.camera().y = 600.0f;
        auto digest_at = [&](uint32_t ms) {
            rig.renderer.pin_animation_clock(ms);
            rig.renderer.begin_frame();
            rig.renderer.render_world(world, eng.grid(), -1);
            return rgb_digest(rig.read());
        };
        check(digest_at(0) != digest_at(900), "(control) the match screen's picture changes with the template clock, so the equality above is a statement about the image");
        rig.renderer.unpin_animation_clock();
    }

    // TILES: a world that does not fit one render target is drawn in tiles of the device's size; the pixels are those of the whole
    {
        struct Tiled {
            const char* name;
            int32_t side;
        };
        for (const Tiled& t : {Tiled{"GAUNTLET", 1024}, Tiled{"SMALL", 1024}, Tiled{"TREASURE", 768}, Tiled{"TINY", 640}}) {
            const assets::LevelData level = load_level(t.name);
            sim::SimulationEngine eng;
            eng.init(level, 1u, 0x0F);
            sim::WorldState world = eng.get_world_state();
            world.ants.clear();
            WorldImage whole;
            WorldImage tiled;
            std::string why;
            const bool a = rig.renderer.render_world_image(level, world, eng.grid(), whole, &why);
            const bool b = rig.renderer.render_world_image(level, world, eng.grid(), tiled, &why, t.side);
            check(a && b && whole.valid() && tiled.valid() && whole.width == tiled.width && whole.height == tiled.height && whole.rgba == tiled.rgba,
                  std::string(t.name) + ": drawn in tiles (targets of at most " + std::to_string(t.side) + ") the image is the whole's, byte for byte " + why);
        }
        const assets::LevelData level = load_level("TINY");
        sim::SimulationEngine eng;
        eng.init(level, 1u, 0x0F);
        WorldImage out;
        std::string why;
        check(!rig.renderer.render_world_image(level, eng.get_world_state(), eng.grid(), out, &why, 500) && !out.valid() && !why.empty(),
              "a target too small to tile with (500) fails with a reason: " + why);
    }

    // A DEVICE THAT FAILS: a renderer that was never started, or an empty level, says no and why
    {
        Renderer cold;
        const assets::LevelData level = load_level("TINY");
        sim::SimulationEngine eng;
        eng.init(level, 1u, 0x0F);
        WorldImage out;
        std::string why;
        check(!cold.render_world_image(level, eng.get_world_state(), eng.grid(), out, &why) && !out.valid() && why.find("not up") != std::string::npos, "a renderer that is not up: no image, \"" + why + "\"");
        const assets::LevelData empty;
        why.clear();
        check(!rig.renderer.render_world_image(empty, eng.get_world_state(), eng.grid(), out, &why) && !why.empty(), "an empty level: no image, \"" + why + "\"");
        // a world of more than 2^25 pixels is not drawn (a 200 x 200 map is 6400 x 6400 pixels), and a level of an impossible size neither: both refused before anything is made
        assets::LevelData huge;
        huge.width.val = 200;
        huge.height.val = 200;
        why.clear();
        check(!rig.renderer.render_world_image(huge, eng.get_world_state(), eng.grid(), out, &why) && why.find("too big") != std::string::npos, "a 200 x 200 map is too big to be drawn as one image: \"" + why + "\"");
        huge.width.val = 5000;
        why.clear();
        check(!rig.renderer.render_world_image(huge, eng.get_world_state(), eng.grid(), out, &why) && !why.empty(), "a level 5000 cells wide has no usable size: \"" + why + "\"");
        // the default IRenderer (the test renderers) draws no images
        Stub none;
        why.clear();
        check(!none.render_world_image(level, eng.get_world_state(), eng.grid(), out, &why) && why.find("no offscreen") != std::string::npos, "an IRenderer without the ability says so: \"" + why + "\"");
    }
}

// =====================================================================================================================================================
// 4. The engine of the preview
// =====================================================================================================================================================

void test_engine(const assets::AssetArchive& arc) {
    group("engine", "the preview's simulation is a new one; a level that only some teams can play shows those teams");
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer is up");
    if (!rig.ok) return;
    // a running match is not touched: its state hash, and every tick after, are what they would have been
    {
        const assets::LevelData level = load_level("ISLANDS");
        sim::SimulationEngine a;
        sim::SimulationEngine b;
        a.init(level, 42u, 0x0F);
        b.init(level, 42u, 0x0F);
        for (int i = 0; i < 40; ++i) {
            a.tick();
            b.tick();
        }
        const sim::StateHash before = a.state_hash();
        for (const char* name : kShipped) {
            const MapPreview p = render_map_preview_world(rig.renderer, load_level(name), 300);
            check(p.valid(), std::string(name) + ": a preview is made while the match runs");
        }
        check(a.state_hash() == before && a.state_hash() == b.state_hash(), "the running match's state hash is the one it had, and the one of a match that never saw a preview");
        for (int i = 0; i < 40; ++i) {
            a.tick();
            b.tick();
        }
        check(a.state_hash() == b.state_hash(), "... and so is every tick after");
    }
    // what the renderer is given: the world of a match that was set up for the level with every team (four hills, the cells of the level), and no ants (the engine of a match puts
    // its starting ants in the state at once, though they are not drawn before the first tick: the preview's picture is of the map, so the ants are taken out)
    {
        Stub stub(Stub::World::Synthetic);
        const assets::LevelData level = load_level("GAUNTLET");
        sim::SimulationEngine control;
        control.init(level, 1u, 0x0F);
        check(!control.get_world_state().ants.empty(), "(control) a started match has its starting ants in the state: " + std::to_string(control.get_world_state().ants.size()));
        const MapPreview p = render_map_preview_world(stub, level, 300);
        check(p.valid() && stub.render_calls == 1 && stub.last_ants == 0, "the renderer is given a world without ants (" + std::to_string(stub.last_ants) + ")");
        check(stub.last_cells == 3600 && stub.last_anthills == 4 && p.players == 4, "... of a match with all four hills on the level's 3600 cells (" + std::to_string(stub.last_anthills) + " hills)");
        const MapPreview tiny = render_map_preview_world(stub, load_level("TINY"), 248);
        check(tiny.valid() && stub.last_cells == 31u * 31u && stub.last_anthills == 4 && stub.last_ants == 0, "TINY: its 31 x 31 cells, four hills, no ants");
    }
    // a level that three teams can play: the fourth team's start marker is outside the grid (the original crashes there, the remake refuses the map for a roster with that team)
    {
        assets::LevelData level = load_level("GAUNTLET");
        check(level.validate(0x0F).playable, "GAUNTLET is playable by four");
        size_t moved = 0;
        for (assets::AnthillSpawn& s : level.anthill_spawns) {
            if (s.team_id == 3) {
                s.x = static_cast<uint16_t>(level.width() + 5);
                ++moved;
            }
        }
        check(moved > 0 && !level.validate(0x0F).playable && level.validate(0x07).playable, "with one team's marker outside the grid the level is playable by three");
        std::string why;
        const MapPreview p = render_map_preview_world(rig.renderer, level, 300, &why);
        check(p.valid() && p.rendered && p.players == 3, "its preview shows the three teams that can play it: " + std::to_string(p.players) + " hills " + why);
        // the renderer is given the level as a match for those three teams sets it up (no start markers, so no hill art, for the fourth: LevelData::for_roster)
        Stub stub(Stub::World::Synthetic);
        size_t all_spawns = 0;
        for (const assets::AnthillSpawn& sp : level.anthill_spawns) all_spawns += sp.team_id < 4 ? 1u : 0u;
        render_map_preview_world(stub, level, 300);
        check(stub.render_calls == 1 && stub.last_team_spawns > 0 && stub.last_team_spawns < all_spawns && stub.last_anthills == 3,
              "the renderer gets the level for the three teams (" + std::to_string(stub.last_team_spawns) + " of " + std::to_string(all_spawns) + " start markers, " + std::to_string(stub.last_anthills) + " hills)");
        // every team's marker outside the grid: nobody can play it, no preview of this kind, a reason
        for (assets::AnthillSpawn& s : level.anthill_spawns) {
            if (s.team_id < 4) s.x = static_cast<uint16_t>(level.width() + 5);
        }
        why.clear();
        const MapPreview none = render_map_preview_world(rig.renderer, level, 300, &why);
        check(!none.valid() && why.find("no team") != std::string::npos, "a level that no team can play has no such preview: \"" + why + "\"");
        // ... and the screen still has a picture for it: the minimap colours
        const MapPreview fallback = render_map_preview_in_box(level, arc.get_palette(), 300);
        check(fallback.valid() && !fallback.rendered, "the older preview can still be made of it");
    }
}

// =====================================================================================================================================================
// 5. The cache, 6. the fallback
// =====================================================================================================================================================

void prepare(MapSelectScreen& screen, const std::filesystem::path& dir, bool wide = true) {
    screen.init(dir.string());
    screen.set_wide_layout(wide);
    screen.set_player_name("Player");
    screen.set_player_team(0);
    screen.enter();
    screen.update(1.66f);
}

void test_cache(const assets::AssetArchive& arc) {
    group("cache", "one render per map, the oldest of 24 dropped, another box is another picture");
    // a folder of 30 maps (copies of TINY, so that the paths differ and the render is cheap)
    TempDir dir;
    const std::vector<uint8_t> tiny = serialize_level(load_level("TINY"));
    for (int i = 0; i < 30; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "M%02d.LVL", i);
        write_file(dir.path / name, tiny);
    }
    MapSelectScreen screen;
    prepare(screen, dir.path);
    check(screen.get_maps().size() == 30, "30 maps are listed");
    Stub stub(Stub::World::Synthetic);
    auto show = [&](int index) {
        screen.set_selected_index(index);
        screen.render(stub, arc);
    };
    show(0);
    show(0);
    show(0);
    check(stub.render_calls == 1, "three frames of one map: one render (" + std::to_string(stub.render_calls) + ")");
    check(!stub.images.empty() && stub.images.back().w == 300 && stub.images.back().h == 300, "the picture is 300 x 300 in the single player's box");
    // it is the filter's picture of the synthetic world, exactly
    {
        const assets::LevelData level = load_level("TINY");
        const MapPreview want = downscale_world_image(Stub::synthetic_world(level.width() * 32u, level.height() * 32u), level.width(), level.height(), 4, 300);
        check(want.valid() && !stub.images.empty() && stub.images.back().rgba == want.rgba, "the screen blits box_downscale's picture of the world that the renderer gave");
    }
    show(1);
    show(0);
    show(1);
    show(0);
    check(stub.render_calls == 2, "map 1 is rendered once, map 0 is not rendered again when it is shown again (" + std::to_string(stub.render_calls) + " renders)");
    for (int i = 2; i < 30; ++i) show(i);
    check(stub.render_calls == 30, "30 maps: 30 renders (" + std::to_string(stub.render_calls) + ")");
    show(29);
    show(28);
    show(6);                                    // the last 24 (6 .. 29) are kept
    check(stub.render_calls == 30, "the last 24 maps are still kept");
    show(5);                                    // the 25th from the end is gone: the cache holds 24, not 25 (and not 23: map 6 above)
    check(stub.render_calls == 31, "map 5 was dropped (it is rendered again), the cache holds exactly 24: " + std::to_string(stub.render_calls));
    show(0);                                    // the oldest are gone
    check(stub.render_calls == 32, "so was map 0 (the oldest goes first: 5 took the place of 6)");
    show(29);
    check(stub.render_calls == 32, "(map 29 is still there)");
    // the same screen when the room makes it the online host's: another box (248): the same map is drawn again at that size, and both pictures are kept
    {
        MapSelectScreen online;
        prepare(online, dir.path);
        Stub s2(Stub::World::Synthetic);
        online.render(s2, arc);
        online.render(s2, arc);
        check(s2.render_calls == 1 && s2.images.back().w == 300, "the local game's screen: 300 x 300, once");
        MapSelectScreen::RoomView room;
        room.networked = true;
        room.is_host = true;
        room.leader = false;
        room.my_seat = 0;
        room.seats[0] = {true, "Ana", MapSelectScreen::Thumb::Good};
        online.set_room(room);
        online.update(0.0f);
        online.render(s2, arc);
        online.render(s2, arc);
        check(s2.render_calls == 2 && s2.images.back().w == 248 && s2.images.back().h == 248, "the same map as an online host's screen: drawn again, 248 x 248, once (" + std::to_string(s2.render_calls) + " renders)");
        online.set_room(MapSelectScreen::RoomView{});
        online.update(0.0f);
        online.render(s2, arc);
        online.set_selected_index(1);
        online.render(s2, arc);
        online.set_selected_index(0);
        online.render(s2, arc);
        check(s2.render_calls == 3 && s2.images.back().w == 300, "back to the local game: the 300 picture of the map that was kept, map 1 once more (" + std::to_string(s2.render_calls) + " renders)");
    }
}

void test_fallback(const assets::AssetArchive& arc) {
    group("fallback", "no offscreen render: the minimap-colour picture and one log line; an unreadable file: no picture, no render");
    TempDir dir;
    const std::vector<uint8_t> tiny = serialize_level(load_level("TINY"));
    write_file(dir.path / "A.LVL", tiny);
    write_file(dir.path / "B.LVL", serialize_level(crop_level(load_level("GAUNTLET"), 60, 40)));
    write_file(dir.path / "C.LVL", tiny);
    const assets::LevelData tiny_level = load_level("TINY");
    const MapPreview old_picture = render_map_preview_in_box(tiny_level, arc.get_palette(), 300);

    struct Mode {
        const char* name;
        Stub::World world;
        const char* reason;
    };
    for (const Mode& mode : {Mode{"a renderer that cannot draw offscreen", Stub::World::None, "no offscreen"}, Mode{"a renderer whose offscreen render fails", Stub::World::Fail, "the stub fails"}}) {
        MapSelectScreen screen;
        prepare(screen, dir.path);
        Stub stub(mode.world);
        CaptureCerr log;
        for (int index = 0; index < 3; ++index) {
            screen.set_selected_index(index);
            screen.render(stub, arc);
            screen.render(stub, arc);
        }
        check(stub.images.size() >= 3 && stub.render_calls == 3, std::string(mode.name) + ": a render was tried once per map (" + std::to_string(stub.render_calls) + ") and every map has a picture");
        // map A is TINY: the picture is exactly the minimap-colour preview of the older code
        check(!stub.images.empty() && stub.images.front().w == old_picture.width && stub.images.front().h == old_picture.height && stub.images.front().rgba == old_picture.rgba,
              std::string(mode.name) + ": the picture is render_map_preview_in_box's (279 x 279, a whole scale)");
        check(log.lines() == 1 && log.text().find("[MapSelect]") != std::string::npos && log.text().find(mode.reason) != std::string::npos,
              std::string(mode.name) + ": ONE line is logged, with the reason (" + std::to_string(log.lines()) + " lines: " + log.text() + ")");
        // the box is still the preview box; the caption is the map's
        std::string captions;
        for (const std::string& t : stub.texts) captions += t + "|";
        check(captions.find("31 x 31 cells") != std::string::npos && captions.find("60 x 40 cells") != std::string::npos, std::string(mode.name) + ": the captions are the maps' (31 x 31, 60 x 40)");
    }
    // a renderer that draws offscreen logs nothing
    {
        MapSelectScreen screen;
        prepare(screen, dir.path);
        Stub stub(Stub::World::Synthetic);
        CaptureCerr log;
        for (int index = 0; index < 3; ++index) {
            screen.set_selected_index(index);
            screen.render(stub, arc);
        }
        check(log.lines() == 0 && stub.render_calls == 3, "a renderer that draws offscreen: nothing is logged");
    }
    // a file that is not a map: no picture, no render, "No preview", and not tried again
    {
        TempDir bad;
        write_file(bad.path / "BAD.LVL", std::vector<uint8_t>{'n', 'o', 't', ' ', 'a', ' ', 'm', 'a', 'p'});
        MapSelectScreen screen;
        prepare(screen, bad.path);
        Stub stub(Stub::World::Synthetic);
        CaptureCerr log;
        screen.render(stub, arc);
        screen.render(stub, arc);
        std::string captions;
        for (const std::string& t : stub.texts) captions += t + "|";
        check(stub.render_calls == 0 && stub.images.empty() && captions.find("No preview") != std::string::npos && captions.find("Map cannot be read") != std::string::npos && log.lines() == 0,
              "a file that is not a map: the \"No preview\" box, no render tried, nothing logged");
    }
    // a level that no team can play: the renderer is not asked, the minimap colours are shown and the log says why
    {
        assets::LevelData level = load_level("TINY");
        for (assets::AnthillSpawn& s : level.anthill_spawns) {
            if (s.team_id < 4) s.y = static_cast<uint16_t>(level.height() + 3);
        }
        TempDir odd;
        write_file(odd.path / "ODD.LVL", serialize_level(level));
        MapSelectScreen screen;
        prepare(screen, odd.path);
        Stub stub(Stub::World::Synthetic);
        CaptureCerr log;
        screen.render(stub, arc);
        const MapPreview want = render_map_preview_in_box(level, arc.get_palette(), 300);
        check(stub.render_calls == 0 && !stub.images.empty() && want.valid() && stub.images.back().rgba == want.rgba && log.lines() == 1 && log.text().find("no team") != std::string::npos,
              "a level no team can play: the minimap colours, and the log says why (" + log.text() + ")");
    }
}

// =====================================================================================================================================================
// 7. The six shipped maps
// =====================================================================================================================================================

struct PinnedPreview {
    const char* map;
    int32_t inner;
    uint64_t digest;
    uint32_t players;
};

constexpr PinnedPreview kPinned[] = {
    {"GAUNTLET", 300, 0x135cc146fe92f86full, 4}, {"GAUNTLET", 248, 0xf5803c01d4d098faull, 4}, {"ISLANDS", 300, 0x8043ca1ffecf1652ull, 4}, {"ISLANDS", 248, 0xd6011b4e38e57829ull, 4},
    {"MEDIUM", 300, 0x278bcd324fdbaff0ull, 4},   {"MEDIUM", 248, 0x476998c4b82e227dull, 4},   {"SMALL", 300, 0xd9603eb1eadb45c6ull, 4},   {"SMALL", 248, 0xf1f02e24d87716c5ull, 4},
    {"TINY", 300, 0xffd6efe3615ca6dcull, 4},     {"TINY", 248, 0x404dc25216c5dc6aull, 4},     {"TREASURE", 300, 0x331245eb34fcc87eull, 4}, {"TREASURE", 248, 0x1902bd3deb095a00ull, 4},
};

void test_shipped(const assets::AssetArchive& arc) {
    group("shipped", "the previews of the six shipped maps are pinned (SDL's software renderer: its integer blits and the filter's integers are the same on every machine)");
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer is up");
    if (!rig.ok) return;
    if (g_print) std::printf("\nconstexpr PinnedPreview kPinned[] = {\n");
    for (const char* name : kShipped) {
        const assets::LevelData level = load_level(name);
        for (int32_t inner : {300, 248}) {
            std::string why;
            const auto t0 = std::chrono::steady_clock::now();
            const MapPreview p = render_map_preview_world(rig.renderer, level, inner, &why);
            const auto t1 = std::chrono::steady_clock::now();
            const uint64_t digest = rgb_digest(p.rgba);
            if (g_print) std::printf("    {\"%s\", %d, %s, %u},\n", name, inner, hex64(digest).c_str(), p.players);
            if (g_time) std::printf("    %-9s box %d: %.1f ms\n", name, inner, std::chrono::duration<double, std::milli>(t1 - t0).count());
            const PinnedPreview* pin = nullptr;
            for (const PinnedPreview& c : kPinned) {
                if (std::string(c.map) == name && c.inner == inner) pin = &c;
            }
            check(p.valid() && p.rendered && p.width == inner && p.height == inner && p.scale == 0 && !p.sampled, std::string(name) + " in " + std::to_string(inner) + ": a square picture that fills the box " + why);
            check(pin != nullptr && pin->digest == digest && pin->players == p.players, std::string(name) + " in " + std::to_string(inner) + ": the picture is the pinned one: " + hex64(digest) + (pin != nullptr ? ", wanted " + hex64(pin->digest) : ", no pin"));
            check(p.cols == level.width() && p.rows == level.height() && map_preview_caption(p).find(std::to_string(level.width()) + " x " + std::to_string(level.height()) + " cells") == 0,
                  std::string(name) + ": the caption names its size");
        }
    }
    if (g_print) std::printf("};\n");
    // the six are six different pictures, and the box sizes differ
    std::vector<uint64_t> seen;
    for (const PinnedPreview& c : kPinned) seen.push_back(c.digest);
    std::sort(seen.begin(), seen.end());
    check(std::adjacent_find(seen.begin(), seen.end()) == seen.end(), "twelve pinned previews, twelve different digests");
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--print") == 0) g_print = true;
        if (std::strcmp(argv[i], "--time") == 0) g_time = true;
    }
    ensure_sdl();
    assets::AssetArchive archive;
    if (!archive.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot load ants.chd\n");
        return 2;
    }
    test_filter(archive);
    test_fit(archive);
    test_render(archive);
    test_engine(archive);
    test_cache(archive);
    test_fallback(archive);
    test_shipped(archive);
    std::printf("\nmap preview: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
