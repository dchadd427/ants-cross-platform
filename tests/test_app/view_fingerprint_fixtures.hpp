// The fixtures of the view fingerprint tests (test_view_fingerprint.cpp holds main(); the cases are in test_view_fingerprint_hud.cpp, test_view_fingerprint_pointer.cpp, test_view_fingerprint_pixels.cpp and test_view_fingerprint_wide.cpp). Inline or in an anonymous namespace.
#pragma once

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "ants_app/application.hpp"
#include "ants_app/edge_scroll.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/options_screen.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/start_menu.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#if defined(__GNUC__)
#define VF_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define VF_PRINTF(a, b)
#endif

using namespace ants;
using namespace ants::app;

// =====================================================================================================================================================
// Hashing, goldens, reporting
// =====================================================================================================================================================

/// FNV-1a 64 over bytes; every number is fed as its little-endian bytes, every string as its length and its bytes
class Fnv64 {
public:
    void byte(uint8_t b) noexcept {
        h_ ^= b;
        h_ *= 1099511628211ULL;
    }
    void u32(uint32_t v) noexcept {
        for (unsigned i = 0; i < 4; ++i) byte(static_cast<uint8_t>((v >> (8u * i)) & 0xFFu));
    }
    void i32(int32_t v) noexcept { u32(static_cast<uint32_t>(v)); }
    void flag(bool v) noexcept { byte(v ? 1 : 0); }
    void str(const std::string& s) noexcept {
        u32(static_cast<uint32_t>(s.size()));
        for (char c : s) byte(static_cast<uint8_t>(c));
    }
    void bytes(const uint8_t* p, size_t n) noexcept {
        for (size_t i = 0; i < n; ++i) byte(p[i]);
    }
    uint64_t value() const noexcept { return h_; }

private:
    uint64_t h_{14695981039346656037ULL};
};

/// One fingerprint: the hash and what it counts (draw calls for a recorded frame, samples for a pointer or pixel sweep)
struct Measure {
    uint64_t hash{0};
    uint64_t count{0};
};

struct Golden {
    const char* name;
    uint64_t hash;
    uint64_t count;
};

const Golden* find_golden(const std::string& name);       // defined below the table at the end of the file
inline int g_checks = 0;
inline int g_failures = 0;
inline bool g_print = false;                       // --print
inline bool g_keep_logs = false;                   // --dump: recorders keep a readable log
inline std::string g_dump_name;                    // --dump NAME
inline std::string g_only_prefix;                  // --only PREFIX
inline std::string g_save_dir;                     // --save DIR
inline std::vector<std::pair<std::string, Measure>> g_measured;
inline std::vector<std::string> g_seen;            // names that were compared (for the unused-golden check)

inline std::string hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%016llx", static_cast<unsigned long long>(v));
    return buf;
}

// =====================================================================================================================================================
// The recording renderer
// =====================================================================================================================================================

/// An IRenderer that records EVERY mutating call with ALL its parameters. Position, size, clip, flip, color (alpha included), text, font size and the
/// image bytes are in the hash; the HUD team is a call of its own (it recolors the sprites that follow). get_text_width / get_text_height are
/// not recorded but deterministic: the interface's 6 px per character, and the cell height of the font size (what the real renderer reports).
class FrameRecorder : public IRenderer {
public:
    enum Op : uint8_t { OpSprite = 1, OpNamed, OpFill, OpRect, OpText, OpTeam, OpClip, OpClearClip, OpImage, OpRegion, OpOrigin, OpCount };

    explicit FrameRecorder(bool keep_log = false) : keep_log_(keep_log) {}

    void draw_sprite(uint32_t sprite_id, int32_t x, int32_t y, bool mirrored = false) override {
        begin(OpSprite);
        hash_.u32(sprite_id);
        hash_.i32(x);
        hash_.i32(y);
        hash_.flag(mirrored);
        logf("sprite id=%u (%d,%d)%s", sprite_id, x, y, mirrored ? " mirrored" : "");
    }
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool mirrored = false) override {
        begin(OpNamed);
        hash_.str(name);
        hash_.i32(x);
        hash_.i32(y);
        hash_.flag(mirrored);
        logf("named \"%s\" (%d,%d)%s", name.c_str(), x, y, mirrored ? " mirrored" : "");
    }
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override {
        begin(OpFill);
        rect(x, y, w, h, c);
        logf("fill (%d,%d) %dx%d rgba(%u,%u,%u,%u)", x, y, w, h, c.r, c.g, c.b, c.a);
    }
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override {
        begin(OpRect);
        rect(x, y, w, h, c);
        logf("frame (%d,%d) %dx%d rgba(%u,%u,%u,%u)", x, y, w, h, c.r, c.g, c.b, c.a);
    }
    /// The 4 argument form is the 12 px text: the real renderer forwards it to the sized form, so both are one operation
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA c) override { text_call(text, x, y, c, FontSize::Px12); }
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) override { text_call(text, x, y, c, size); }
    int32_t get_text_height(FontSize size = FontSize::Px12) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t team) override {
        begin(OpTeam);
        hash_.byte(team);
        logf("hud team %u", team);
    }
    void set_clip_rect(int32_t x, int32_t y, int32_t w, int32_t h) override {
        begin(OpClip);
        hash_.i32(x);
        hash_.i32(y);
        hash_.i32(w);
        hash_.i32(h);
        logf("clip (%d,%d) %dx%d", x, y, w, h);
    }
    void clear_clip_rect() override {
        begin(OpClearClip);
        logf("clip off");
    }
    /// A part of a sprite stretched to a rectangle (the wide frame's pieces): the sprite and both rectangles are in the hash (milestone M3; no classic scene draws one)
    void draw_sprite_region(uint32_t sprite_id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh) override {
        begin(OpRegion);
        hash_.u32(sprite_id);
        hash_.i32(x);
        hash_.i32(y);
        hash_.i32(w);
        hash_.i32(h);
        hash_.i32(sx);
        hash_.i32(sy);
        hash_.i32(sw);
        hash_.i32(sh);
        logf("region id=%u (%d,%d) %dx%d from (%d,%d) %dx%d", sprite_id, x, y, w, h, sx, sy, sw, sh);
    }
    /// The origin of a window of the original (a page, a dialog) in a bigger picture: a CHANGE of the origin is a call; setting the origin that is set already is not (the classic scenes
    /// set (0, 0) around their dialogs, which changes nothing and must not change a classic number)
    void set_origin(int32_t x, int32_t y) override {
        if (x == origin_x_ && y == origin_y_) return;
        origin_x_ = x;
        origin_y_ = y;
        begin(OpOrigin);
        hash_.i32(x);
        hash_.i32(y);
        logf("origin (%d,%d)", x, y);
    }
    void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* rgba) override {
        begin(OpImage);
        hash_.i32(x);
        hash_.i32(y);
        hash_.i32(w);
        hash_.i32(h);
        Fnv64 image;
        const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
        image.bytes(rgba, n);
        hash_.u32(static_cast<uint32_t>(image.value() & 0xFFFFFFFFu));
        hash_.u32(static_cast<uint32_t>(image.value() >> 32));
        logf("image (%d,%d) %dx%d bytes-hash %s", x, y, w, h, hex64(image.value()).c_str());
    }

    Measure result() const noexcept { return Measure{hash_.value(), calls_}; }
    uint64_t calls() const noexcept { return calls_; }
    uint64_t calls_of(Op op) const noexcept { return op_count_[op]; }
    const std::vector<std::string>& log() const noexcept { return log_; }

    /// One recorded text (the text boxes are needed to mask TrueType pixels)
    struct TextCall {
        std::string text;
        int32_t x, y;
        FontSize size;
    };
    const std::vector<TextCall>& texts() const noexcept { return texts_; }

private:
    void begin(Op op) {
        hash_.byte(static_cast<uint8_t>(op));
        ++calls_;
        ++op_count_[op];
    }
    void rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) {
        hash_.i32(x);
        hash_.i32(y);
        hash_.i32(w);
        hash_.i32(h);
        hash_.byte(c.r);
        hash_.byte(c.g);
        hash_.byte(c.b);
        hash_.byte(c.a);
    }
    void text_call(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) {
        begin(OpText);
        hash_.str(text);
        hash_.i32(x);
        hash_.i32(y);
        hash_.byte(static_cast<uint8_t>(size));
        hash_.byte(c.r);
        hash_.byte(c.g);
        hash_.byte(c.b);
        hash_.byte(c.a);
        texts_.push_back({text, x, y, size});
        logf("text \"%s\" (%d,%d) %dpx rgba(%u,%u,%u,%u)", text.c_str(), x, y, static_cast<int>(font_cell_height(size)), c.r, c.g, c.b, c.a);
    }
    void logf(const char* fmt, ...) VF_PRINTF(2, 3) {
        if (!keep_log_) return;
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        log_.emplace_back(buf);
    }

    bool keep_log_;
    Fnv64 hash_;
    int32_t origin_x_{0};
    int32_t origin_y_{0};
    uint64_t calls_{0};
    std::array<uint64_t, OpCount> op_count_{};
    std::vector<std::string> log_;
    std::vector<TextCall> texts_;
};

// =====================================================================================================================================================
// The scenes: a hand-built world, the HUD, the camera
// =====================================================================================================================================================


// The HUD's millisecond clock is injected: pedestals, the caret of the chat box, the options screen and the dialogs read it
inline uint32_t g_now_ms = 0;

/// The metrics of the text that the HUD measures its chat and labels with (the interface's 6 px per character; the recorder is never hashed here)
inline FrameRecorder g_metrics;

namespace {

inline void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what.c_str());
    }
}

inline bool wanted(const std::string& name) { return g_only_prefix.empty() || name.rfind(g_only_prefix, 0) == 0; }

/// Is any scenario of the group `prefix` wanted (a rig that is expensive to build is only built for a wanted group)
inline bool wanted_group(const std::string& prefix) { return g_only_prefix.empty() || prefix.rfind(g_only_prefix, 0) == 0 || g_only_prefix.rfind(prefix, 0) == 0; }

/// Compares a measure with its golden number (or collects it for --print)
inline void record(const std::string& name, const Measure& m) {
    if (!wanted(name)) return;
    g_measured.emplace_back(name, m);
    g_seen.push_back(name);
    if (g_print) return;
    ++g_checks;
    const Golden* g = find_golden(name);
    if (g == nullptr) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: no golden number (actual hash %s, %llu)\n", name.c_str(), hex64(m.hash).c_str(), static_cast<unsigned long long>(m.count));
        return;
    }
    if (g->hash != m.hash || g->count != m.count) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: expected hash %s / %llu, actual hash %s / %llu\n", name.c_str(), hex64(g->hash).c_str(),
                     static_cast<unsigned long long>(g->count), hex64(m.hash).c_str(), static_cast<unsigned long long>(m.count));
    }
}

/// Records a finished frame (and prints its log for --dump)
inline void record_frame(const std::string& name, const FrameRecorder& rec) {
    if (!g_dump_name.empty() && name == g_dump_name) {
        std::printf("== %s: %llu calls, hash %s\n", name.c_str(), static_cast<unsigned long long>(rec.calls()), hex64(rec.result().hash).c_str());
        for (size_t i = 0; i < rec.log().size(); ++i) std::printf("%5zu  %s\n", i, rec.log()[i].c_str());
    }
    record(name, rec.result());
}

inline uint32_t test_clock() { return g_now_ms; }

inline uint32_t lcg(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

inline sim::TileCell& cell_of(sim::WorldState& w, uint32_t x, uint32_t y) { return w.cells[static_cast<size_t>(y) * w.width + x]; }
constexpr uint32_t kMap = 60;                      // the map of the scenes is 60 x 60 tiles = 1920 x 1920 px: the view reaches (1478, 1480) at most

/// The anchors of the four hills (green, red, blue, black = the remake's players 0 .. 3) on a 60 x 60 map
constexpr int32_t kHill[4][2] = {{9, 6}, {46, 6}, {9, 46}, {46, 46}};

/// A deterministic world: terrain bands (water, mud, gravel, slate and the grass between), rocks, food piles, a lunchbox, a fire wall, a bomb, power-ups,
/// plants, the four hills and some ants of every team. Everything from fixed formulas.
inline sim::WorldState make_world(const assets::AssetArchive& arc, uint32_t w = kMap, uint32_t h = kMap) {
    sim::WorldState world;
    world.width = w;
    world.height = h;
    world.cells.assign(static_cast<size_t>(w) * h, sim::TileCell{});
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            sim::TileCell& c = cell_of(world, x, y);
            const uint32_t col = x * 60 / w;
            const uint32_t row = y * 60 / h;
            if (col < 8) {
                c.terrain_type = sim::TERRAIN_WATER;
                c.surface_type = sim::SurfaceType::Water;
            } else if (col >= 20 && col < 30 && row < 30) {
                c.surface_type = sim::SurfaceType::Mud;
                c.is_mud = true;
            } else if (col >= 40 && col < 50) {
                c.surface_type = sim::SurfaceType::Gravel;
            } else if (row >= 52) {
                c.surface_type = sim::SurfaceType::Slate;
            }
        }
    }
    uint32_t seed = 12345u;
    for (uint32_t i = 0; i < w * h / 40u; ++i) {                             // rocks (animation 293, color 51) on walkable ground
        const uint32_t x = lcg(seed) % w;
        const uint32_t y = lcg(seed) % h;
        sim::TileCell& c = cell_of(world, x, y);
        if (c.terrain_type == sim::TERRAIN_WALKABLE) c.interactive_id = 293;
    }
    auto put = [&](uint32_t x, uint32_t y, uint16_t id) {
        if (x < w && y < h) cell_of(world, x, y).interactive_id = id;
    };
    put(20, 35, 253);                                                         // food piles (fdburgr1, color 171)
    put(22, 36, 260);
    put(16, 20, 267);
    put(24, 24, sim::TILE_LUNCHBOX);
    put(30, 12, sim::TILE_FIREWALL);
    put(31, 33, sim::BOMB_RED);
    put(33, 20, sim::PU_BOMBER);
    for (int t = 0; t < 4; ++t) {                                             // the hills: the tile of the colony's hill (GREENHILL .. BLACKHILL) over 4 x 4 cells
        const int32_t id = arc.find_animation_id(t == 0 ? "GREENHILL" : t == 1 ? "REDHILL" : t == 2 ? "BLUEHILL" : "BLACKHILL");
        assets::AnthillSpawn spawn;
        spawn.tile_id = static_cast<uint16_t>(id < 0 ? 0 : id);
        spawn.x = static_cast<uint16_t>(kHill[t][0] * static_cast<int32_t>(w) / 60);
        spawn.y = static_cast<uint16_t>(kHill[t][1] * static_cast<int32_t>(h) / 60);
        spawn.team_id = static_cast<uint8_t>(t);
        world.anthills.push_back(spawn);
        for (uint32_t dy = 0; dy < 4; ++dy) {
            for (uint32_t dx = 0; dx < 4; ++dx) put(spawn.x + dx, spawn.y + dy, static_cast<uint16_t>(id < 0 ? 0 : id));
        }
    }
    for (uint16_t i = 0; i < 12; ++i) world.plants.push_back(sim::MapPlant{static_cast<uint16_t>(323 + i % 4), static_cast<uint16_t>(18 + i * 3), static_cast<uint16_t>(40 + (i * 7) % 15)});
    world.match_time_remaining_ms = 9u * 60000u + 5000u;                       // 9:05
    world.player_alliances = {255, 255, 255, 255};
    world.player_scores = {120, 340, 75, 910};
    world.player_eggs = {10, 10, 10, 10};
    return world;
}

inline void add_ant(sim::WorldState& world, uint32_t id, uint8_t player, sim::AntType type, int32_t tx, int32_t ty, int32_t ox = 16, int32_t oy = 16, bool holding = false) {
    sim::AntSnapshot a;
    a.id = id;
    a.player_id = player;
    a.type = type;
    a.tile_x = tx;
    a.tile_y = ty;
    a.px = tx * 32 + ox;
    a.py = ty * 32 + oy;
    a.is_holding = holding;
    a.hp = 10;
    a.max_hp = 10;
    world.ants.push_back(a);
}

/// A few ants of every team: green (the local team) ids 1 .. 7 are worker, worker, bomber, fire, thief, combat, swimmer; red 11 .. 14; blue 21 .. 23; black 31 .. 35
inline void add_default_ants(sim::WorldState& world) {
    using T = sim::AntType;
    const T kinds[7] = {T::Worker, T::Worker, T::Bomber, T::Fire, T::Thief, T::Combat, T::Swimmer};
    for (uint32_t i = 0; i < 7; ++i) add_ant(world, 1 + i, 0, kinds[i], 12 + static_cast<int32_t>(i % 4) * 2, 10 + static_cast<int32_t>(i / 4) * 2);
    for (uint32_t i = 0; i < 4; ++i) add_ant(world, 11 + i, 1, kinds[i], 44 + static_cast<int32_t>(i) * 2, 11);
    for (uint32_t i = 0; i < 3; ++i) add_ant(world, 21 + i, 2, kinds[i], 12 + static_cast<int32_t>(i) * 2, 44, 10 + static_cast<int32_t>(i) * 3, 20);
    for (uint32_t i = 0; i < 5; ++i) add_ant(world, 31 + i, 3, kinds[i], 44 + static_cast<int32_t>(i), 44 + static_cast<int32_t>(i % 2) * 2);
}

/// The HUD of one local player over a hand-built world and a camera, with an injected clock
class Scene {
public:
    explicit Scene(const assets::AssetArchive& archive, uint8_t local_player = 0, uint32_t w = kMap, uint32_t h = kMap, const ScreenLayout* layout = nullptr) : arc(archive), local(local_player) {
        g_now_ms = 0;
        hud.set_ticks_function(&test_clock);                     // before init: the chat box's caret starts from this clock
        hud.init(local_player);
        if (layout != nullptr) {                                 // a picture other than the original's (milestone M3): the HUD and the camera take its view
            hud.set_layout(*layout);
            camera.set_view(layout->view());
            camera.centre_small_maps = !layout->is_classic();
        }
        hud.set_text_metrics(&g_metrics);
        world = make_world(archive, w, h);
        add_default_ants(world);
        sim.init_test_world(w, h, 1, 600000);                    // only for the handlers that take an engine (the grid is the map's size)
        look(0, 0);
    }

    void look(int32_t x, int32_t y) {
        camera.x = static_cast<float>(x);
        camera.y = static_cast<float>(y);
        camera.world_x = x;
        camera.world_y = y;
    }
    void tick(uint32_t ticks = 0) { hud.update(world, ticks); }
    /// A pointer event that the match screen receives (the camera is at (0, 0) unless the scene moved it)
    void move(int32_t x, int32_t y) { hud.handle_mouse_motion(x, y, sim, camera); }
    void press(int32_t x, int32_t y) { hud.handle_mouse_down(x, y, SDL_BUTTON_LEFT, sim, camera); }
    void release(int32_t x, int32_t y) { hud.handle_mouse_up(x, y, SDL_BUTTON_LEFT, sim, camera); }

    /// Renders the HUD at t = 0 (the pedestals' chains start: once per scene) and again at `at_ms` (the clock only goes forward within a scene); the last
    /// frame is the fingerprint
    void shoot(const std::string& name, uint32_t at_ms = 10000) {
        if (!wanted(name)) return;
        if (!started_) {
            started_ = true;
            FrameRecorder first;
            g_now_ms = 0;
            hud.render(first, arc, world, camera);
        }
        g_now_ms = at_ms;
        FrameRecorder rec(g_keep_logs);
        hud.render(rec, arc, world, camera);
        record_frame(name, rec);
    }

    const assets::AssetArchive& arc;
    bool started_{false};
    uint8_t local;
    sim::WorldState world;
    HUD hud;
    ViewportCamera camera;
    sim::SimulationEngine sim;
};

// =====================================================================================================================================================
// The full-screen screens (recording renderer): the setup screen, the network room and guest screens, the results screen
// =====================================================================================================================================================


/// Records what `draw` puts on a fresh recorder
inline void frame(const std::string& name, const std::function<void(FrameRecorder&)>& draw) {
    if (!wanted(name)) return;
    FrameRecorder rec(g_keep_logs);
    draw(rec);
    record_frame(name, rec);
}

/// The setup screen over the six original maps (the file names, headers and sizes of the Maps folder: sorted by the byte order of the names)
inline MapSelectScreen make_setup_screen() {
    MapSelectScreen screen;
    screen.init(std::string(ORIGINAL_ASSETS_DIR) + "/Maps");
    return screen;
}

// =====================================================================================================================================================
// The pointer: every pixel of the 640 x 480 screen
// =====================================================================================================================================================


constexpr int32_t kW = 640;                       // the classic screen (a name of its own: edge_scroll.hpp has kScreenW / kScreenH)

constexpr int32_t kH = 480;
constexpr uint64_t kPixels = static_cast<uint64_t>(kW) * static_cast<uint64_t>(kH);

/// Calls `per_pixel(hash, x, y)` for every pixel, rows top to bottom
template <class F>
inline Measure sweep(F&& per_pixel) {
    Fnv64 h;
    for (int32_t y = 0; y < kH; ++y) {
        for (int32_t x = 0; x < kW; ++x) per_pixel(h, x, y);
    }
    return Measure{h.value(), kPixels};
}

struct Cam {
    const char* name;
    int32_t x;
    int32_t y;
};

/// Nine view origins on a map of `tiles_w` x `tiles_h` tiles: away from the borders (mid) and at each of the four corners and four edges
inline std::array<Cam, 9> nine_cams(int32_t tiles_w, int32_t tiles_h) {
    const int32_t mx = std::max(0, tiles_w * 32 - PLAYFIELD_W);
    const int32_t my = std::max(0, tiles_h * 32 - PLAYFIELD_H);
    const int32_t cx = mx >= 2 ? mx / 2 : 0;
    const int32_t cy = my >= 2 ? my / 2 : 0;
    return {{{"tl", 0, 0}, {"t", cx, 0}, {"tr", mx, 0}, {"l", 0, cy}, {"mid", cx, cy}, {"r", mx, cy}, {"bl", 0, my}, {"b", cx, my}, {"br", mx, my}}};
}

inline ViewportCamera camera_at(int32_t x, int32_t y) {
    ViewportCamera cam;
    cam.x = static_cast<float>(x);
    cam.y = static_cast<float>(y);
    cam.world_x = x;
    cam.world_y = y;
    return cam;
}

inline void hash_scroll(Fnv64& h, const EdgeScroll& e) {
    h.i32(e.dir);
    h.i32(e.dx);
    h.i32(e.dy);
}

/// The edge-scroll step at every pixel, for the given cameras on a map of `tiles_w` x `tiles_h` tiles
inline Measure edge_sweep(int32_t tiles_w, int32_t tiles_h, int32_t rate, const std::vector<Cam>& cams) {
    Fnv64 h;
    uint64_t n = 0;
    for (const Cam& c : cams) {
        for (int32_t y = 0; y < kH; ++y) {
            for (int32_t x = 0; x < kW; ++x) {
                hash_scroll(h, edge_scroll_step(x, y, rate, c.x, c.y, tiles_w, tiles_h));
                ++n;
            }
        }
    }
    return Measure{h.value(), n};
}

inline std::string map_name(int32_t w, int32_t h) { return std::to_string(w) + "x" + std::to_string(h); }

// ---- the cursor: what HUD::evaluate_cursor chooses at every pixel for a fixed state of the selection and the world

/// An engine world: own hill (team 0) at (21, 21), the enemy hill (team 1) at (14, 22), own ants, an enemy, an ally (team 2), enemies on both hills, a lunchbox, a pile of
/// crackers and an enemy bomb, all in the 14 x 14 tiles that the view shows from the origin (400, 400)
struct PointerScene {
    sim::SimulationEngine sim;
    uint32_t worker{0}, worker2{0}, bomber{0}, thief{0}, combat{0};
    uint32_t foe{0}, ally{0}, foe_on_hill{0}, mine_on_hill{0}, foe_on_own_hill{0};

    explicit PointerScene(bool fog = false) {
        using sim::AntType;
        using sim::TileCoord;
        sim.init_test_world(60, 60, 1, 600000);
        sim::Grid& g = sim.grid_mut();
        g.set_anthill(0, TileCoord{21, 21});
        g.set_anthill(1, TileCoord{14, 22});
        worker = sim.spawn_unit(0, AntType::Worker, TileCoord{14, 14});
        worker2 = sim.spawn_unit(0, AntType::Worker, TileCoord{16, 14});
        bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{18, 14});
        thief = sim.spawn_unit(0, AntType::Thief, TileCoord{14, 17});
        combat = sim.spawn_unit(0, AntType::Combat, TileCoord{13, 12});
        foe = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 14});
        ally = sim.spawn_unit(2, AntType::Worker, TileCoord{22, 14});
        foe_on_hill = sim.spawn_unit(1, AntType::Worker, TileCoord{15, 23});
        mine_on_hill = sim.spawn_unit(0, AntType::Worker, TileCoord{23, 22});
        foe_on_own_hill = sim.spawn_unit(1, AntType::Worker, TileCoord{22, 23});
        g.drop_lunchbox(14, 19, 25);
        sim::FoodObject crackers;
        crackers.row = 19;
        crackers.col = 18;
        crackers.units = 4;
        crackers.value = 25;
        crackers.remaining = 4;
        crackers.thresholds = {4, 3, 2, 1};
        crackers.stage_tiles = {369, 370, 371, 372};
        g.add_food_object(std::move(crackers));
        g.place_bomb(20, 18, 1);
        sim.form_alliance(0, 2);
        if (fog) sim.set_fog_of_war_enabled(true);
        sim.tick();                                                  // the snapshot of the world is rebuilt at the tick
    }
    const sim::WorldState& world() const { return sim.get_world_state(); }
};

struct CursorState {
    const char* name;
    bool fog;
    std::function<void(HUD&, PointerScene&, ViewportCamera&)> setup;
};

// ---- the click: what a press and a release at every pixel do (which control reacts, what is selected, what is ordered, what is heard)

/// Takes the commands of the HUD and does not carry them out, so that the engine's world is the same for every click
class RecordingSink : public sim::CommandSink {
public:
    sim::CommandResult submit(const sim::Command& command) override {
        commands.push_back(command);
        sim::CommandResult r;
        r.status = sim::CommandResult::Status::Applied;
        r.ants_ordered = static_cast<uint32_t>(command.ants.size());
        r.needing_order = static_cast<uint32_t>(command.ants.size());
        r.ack_ant = command.ants.empty() ? 0u : command.ants.front();
        return r;
    }
    std::vector<sim::Command> commands;
};

/// One click on a fresh HUD of a fixed selection state; everything that the click changes goes into the hash
/// A sweep of a probe (`probe(hash, x, y)` fills the hash with what a click at the pixel did) over every `stride`-th pixel, refined: wherever two neighbouring samples differ,
/// the pixels between them are probed as well, so that the edge of every zone is found to the exact pixel (a zone that is one pixel wider or narrower changes the hash)
/// at a fraction of the cost of probing every pixel. The count of the measure is the number of probes.
template <class Probe>
inline Measure refined_sweep(const Probe& probe, int32_t stride, int32_t width = kW, int32_t height = kH) {
    const int32_t nx = (width + stride - 1) / stride;
    const int32_t ny = (height + stride - 1) / stride;
    const auto signature = [&](int32_t x, int32_t y) {
        Fnv64 s;
        probe(s, x, y);
        return s.value();
    };
    std::vector<uint64_t> sig(static_cast<size_t>(nx) * static_cast<size_t>(ny));
    Fnv64 h;
    uint64_t n = 0;
    const auto fold = [&](int32_t x, int32_t y, uint64_t s) {
        h.i32(x);
        h.i32(y);
        h.u32(static_cast<uint32_t>(s & 0xFFFFFFFFu));
        h.u32(static_cast<uint32_t>(s >> 32));
        ++n;
    };
    for (int32_t j = 0; j < ny; ++j) {
        for (int32_t i = 0; i < nx; ++i) {
            const uint64_t s = signature(i * stride, j * stride);
            sig[static_cast<size_t>(j) * static_cast<size_t>(nx) + static_cast<size_t>(i)] = s;
            fold(i * stride, j * stride, s);
        }
    }
    for (int32_t j = 0; j < ny; ++j) {
        for (int32_t i = 0; i < nx; ++i) {
            const auto at = [&](int32_t ii, int32_t jj) { return sig[static_cast<size_t>(jj) * static_cast<size_t>(nx) + static_cast<size_t>(ii)]; };
            const int32_t x = i * stride;
            const int32_t y = j * stride;
            const uint64_t s = at(i, j);
            const bool right = i + 1 < nx && at(i + 1, j) != s;
            const bool down = j + 1 < ny && at(i, j + 1) != s;
            const bool diagonal = i + 1 < nx && j + 1 < ny && at(i + 1, j + 1) != s;
            for (int32_t d = 1; d < stride; ++d) {
                if (right && x + d < width) fold(x + d, y, signature(x + d, y));
                if (down && y + d < height) fold(x, y + d, signature(x, y + d));
                if (diagonal && x + d < width && y + d < height) fold(x + d, y + d, signature(x + d, y + d));
            }
        }
    }
    return Measure{h.value(), n};
}

/// How often a click did something of each kind (what a scenario says it exercises is checked against it)
struct ClickTally {
    uint64_t clicks{0}, commands{0}, quit{0}, options{0}, dialog{0}, selection{0}, latched{0}, scrolled{0}, captured{0}, closed{0};
};

struct ClickProbe {
    PointerScene& ps;
    const std::function<void(HUD&, PointerScene&)>& setup;
    uint8_t button;
    ClickTally* tally{nullptr};
    const ScreenLayout* layout{nullptr};          // a picture other than the original's (milestone M3)

    void operator()(Fnv64& h, int32_t x, int32_t y) const {
        HUD hud;
        hud.set_ticks_function(&test_clock);
        g_now_ms = 0;
        hud.init(0);
        if (layout != nullptr) hud.set_layout(*layout);
        hud.set_sim_query(&ps.sim);
        RecordingSink sink;
        hud.set_command_sink(&sink);
        std::vector<uint32_t> sounds;
        std::vector<std::pair<int32_t, int32_t>> markers;
        hud.set_on_play_sfx([&](uint32_t id) { sounds.push_back(id); });
        hud.set_on_spawn_click_marker([&](int32_t wx, int32_t wy) { markers.emplace_back(wx, wy); });
        int quit_calls = 0;
        hud.set_on_quit([&]() { ++quit_calls; });
        setup(hud, ps);
        hud.update(ps.world(), 0);
        const HUD::AllianceDialog dialog_before = hud.alliance_dialog();
        ViewportCamera cam = camera_at(400, 400);
        if (layout != nullptr) {
            cam.set_view(layout->view());
            cam.centre_small_maps = !layout->is_classic();
        }
        const bool down = hud.handle_mouse_down(x, y, button, ps.sim, cam);
        const bool captured = hud.is_input_captured();
        const bool scrolled = hud.input_tick(cam, 60, 60, x, y);                     // the 50 ms task that follows a press: edge strips, the minimap drag
        const bool up = hud.handle_mouse_up(x, y, button, ps.sim, cam);
        h.flag(down);
        h.flag(captured);
        h.flag(scrolled);
        h.flag(up);
        h.i32(cam.world_x);
        h.i32(cam.world_y);
        h.flag(hud.is_options_open());
        h.flag(hud.is_quick_help_open());
        h.flag(hud.is_quit_dialog_open());
        h.i32(quit_calls);
        h.byte(static_cast<uint8_t>(hud.alliance_dialog()));
        h.flag(hud.is_match_start_modal_active());
        h.i32(hud.options().sound_volume);
        h.i32(hud.options().music_volume);
        h.i32(hud.options().scroll_speed);
        h.flag(hud.options().chat);
        h.flag(hud.options().quick_help);
        h.flag(hud.is_move_latched());
        h.flag(hud.is_ability_latched());
        h.flag(hud.chat_dragging());
        h.i32(hud.get_selected_base_team_id());
        h.u32(static_cast<uint32_t>(hud.get_selected_ant_ids().size()));
        for (uint32_t id : hud.get_selected_ant_ids()) h.u32(id);
        h.u32(hud.get_selected_ant_id());
        h.byte(static_cast<uint8_t>(hud.panel_mode(ps.world())));
        for (int slot = 0; slot < 3; ++slot) h.flag(hud.is_pedestal_flashing(slot));
        h.str(hud.status_line().text());
        h.u32(static_cast<uint32_t>(sink.commands.size()));
        for (const sim::Command& c : sink.commands) {
            h.byte(static_cast<uint8_t>(c.type));
            h.byte(c.issuer);
            h.byte(c.other_player);
            h.i32(c.tile_x);
            h.i32(c.tile_y);
            h.u32(static_cast<uint32_t>(c.ants.size()));
            for (uint32_t id : c.ants) h.u32(id);
        }
        h.u32(static_cast<uint32_t>(sounds.size()));
        for (uint32_t s : sounds) h.u32(s);
        h.u32(static_cast<uint32_t>(markers.size()));
        for (const auto& m : markers) {                                        // where the marker went, relative to the clicked pixel: the classic map view puts the world point (camera + 0) at the screen pixel (16, 21)
            h.i32(m.first - (cam.world_x + (x - 16)));
            h.i32(m.second - (cam.world_y + (y - 21)));
        }
        if (tally != nullptr) {
            ++tally->clicks;
            tally->commands += sink.commands.empty() ? 0u : 1u;
            tally->quit += quit_calls > 0 ? 1u : 0u;
            tally->options += (hud.options().sound_volume != 100 || hud.options().music_volume != 65 || hud.options().scroll_speed != 50 || !hud.options().chat || !hud.options().quick_help) ? 1u : 0u;
            tally->dialog += hud.alliance_dialog() != dialog_before ? 1u : 0u;
            tally->selection += (hud.get_selected_base_team_id() >= 0 || !hud.get_selected_ant_ids().empty()) ? 1u : 0u;
            tally->latched += (hud.is_move_latched() || hud.is_ability_latched()) ? 1u : 0u;
            tally->scrolled += scrolled ? 1u : 0u;
            tally->captured += captured ? 1u : 0u;
            tally->closed += (!hud.is_options_open() && !hud.is_quick_help_open() && !hud.is_quit_dialog_open() && !hud.is_match_start_modal_active()) ? 1u : 0u;
        }
    }
};

// =====================================================================================================================================================
// What the real software renderer draws: the playfield at the corners and edges of the map (pixel hashes)
// =====================================================================================================================================================


/// The pixels of the 640 x 480 canvas (RGBA); `mask` rectangles (x0, y0, x1, y1, half open) are cleared first
struct Canvas {
    std::vector<uint8_t> px = std::vector<uint8_t>(static_cast<size_t>(kW) * static_cast<size_t>(kH) * 4u, 0);
    bool ok{false};
};

struct MaskRect {
    int32_t x0, y0, x1, y1;
};

/// The hash of the canvas with the masked rectangles cleared (they are painted magenta in the files that --save writes)
inline Measure canvas_measure(Canvas& c, const std::vector<MaskRect>& mask = {}) {
    for (const MaskRect& m : mask) {
        for (int32_t y = std::max(0, m.y0); y < std::min(kH, m.y1); ++y) {
            for (int32_t x = std::max(0, m.x0); x < std::min(kW, m.x1); ++x) std::memset(&c.px[(static_cast<size_t>(y) * kW + static_cast<size_t>(x)) * 4u], 0, 4);
        }
    }
    Fnv64 h;
    h.bytes(c.px.data(), c.px.size());
    return Measure{h.value(), kPixels};
}

/// The idle clip of every ant type (ag, ab, af, at, ac, as + "st" + direction digit + "01"): the snapshots of these scenes name their clip, so that what they show does not depend on
/// the engine's animation logic
inline int32_t idle_clip(const assets::AssetArchive& arc, int type, int dir_digit) {
    static const char kLetters[6] = {'g', 'b', 'f', 't', 'c', 's'};
    const std::string name = std::string("a") + kLetters[type % 6] + "st" + std::to_string(dir_digit) + "01";
    return arc.find_animation_id(name);
}

/// Ants of every type and color, of every facing and mirrored clip, spread over the view of the camera (cx, cy) and 80 px beyond its edges, so that sprites straddle
/// every border of the playfield
inline void populate_view(sim::WorldState& world, const assets::AssetArchive& arc, int32_t cx, int32_t cy, int count, uint32_t seed, int32_t view_w = PLAYFIELD_W, int32_t view_h = PLAYFIELD_H) {
    static const int kDirs[5] = {3, 7, 2, 8, 9};
    const int32_t map_w = static_cast<int32_t>(world.width) * 32;
    const int32_t map_h = static_cast<int32_t>(world.height) * 32;
    for (int i = 0; i < count; ++i) {
        const int type = i % 6;
        const int dir = kDirs[(i / 6) % 5];
        const bool mirrored = (dir == 2 || dir == 8 || dir == 9) && (i % 3 == 0);
        const int32_t px = std::clamp(cx - 80 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(view_w + 160)), 4, map_w - 4);
        const int32_t py = std::clamp(cy - 80 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(view_h + 160)), 4, map_h - 4);
        sim::AntSnapshot a;
        a.id = 5000u + static_cast<uint32_t>(i);
        a.player_id = static_cast<uint8_t>(i % 4);
        a.type = static_cast<sim::AntType>(type);
        a.px = px;
        a.py = py;
        a.tile_x = px / 32;
        a.tile_y = py / 32;
        a.hp = static_cast<uint16_t>(1 + i % 10);
        a.max_hp = 10;
        a.state = sim::UnitState::Idle;
        const int32_t clip = idle_clip(arc, type, dir);
        a.loco_clip = clip < 0 ? uint16_t{0x7FFE} : static_cast<uint16_t>(clip);
        const size_t frames = clip < 0 ? 1u : arc.get_animation(static_cast<uint32_t>(clip)).subitems.size();
        a.loco_frame = static_cast<uint16_t>(static_cast<size_t>(i) % std::max<size_t>(1, frames));
        a.loco_mirrored = mirrored;
        a.loco_left_ms = 100;
        world.ants.push_back(a);
    }
}

/// Effects and score bubbles around the edges of the view (the explosion, the sputter, the splash, the scuffle; a gain and a loss)
inline void add_effects(sim::WorldState& world, int32_t cx, int32_t cy, int32_t view_w = PLAYFIELD_W, int32_t view_h = PLAYFIELD_H) {
    const struct { const char* name; uint32_t duration; } kinds[4] = {{"bombex", 680}, {"sputter", 830}, {"dsplash", 460}, {"battle", 270}};
    const int32_t at[8][2] = {{-10, 200}, {view_w - 20, 150}, {200, -10}, {250, view_h - 30}, {30, 30}, {view_w - 40, 40}, {60, view_h - 50}, {view_w - 70, view_h - 60}};
    for (int i = 0; i < 8; ++i) {
        sim::VisualEffect e;
        e.anim_name = kinds[i % 4].name;
        e.px = std::max(0, cx + at[i][0]);
        e.py = std::max(0, cy + at[i][1]);
        e.elapsed_ms = 100u + 60u * static_cast<uint32_t>(i % 3);
        e.duration_ms = kinds[i % 4].duration;
        e.frame = static_cast<uint16_t>(e.elapsed_ms / 50u);
        e.total_frames = static_cast<uint16_t>(e.duration_ms / 50u);
        world.effects.push_back(e);
    }
    sim::ScoreBubble gain;
    gain.x = std::max(0, cx + 120);
    gain.y = std::max(0, cy + 120);
    gain.amount = 150;
    gain.elapsed_ms = 100;
    sim::ScoreBubble loss = gain;
    loss.x = std::max(0, cx - 6);
    loss.y = std::max(0, cy + 300);
    loss.amount = -35;
    world.score_bubbles.push_back(gain);
    world.score_bubbles.push_back(loss);
}

/// The renderer and the application print what font they found on std::cout: that is not of interest here
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

inline ApplicationConfig app_config() {
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = true;
    cfg.lan_port = 0;
    cfg.window_width = kW;                                       // the logical size and the window are the same: one pixel of the canvas is one pixel read back
    cfg.window_height = kH;
    cfg.chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
    cfg.maps_dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
    cfg.midi_path = std::string(ORIGINAL_ASSETS_DIR) + "/INTRO.MID";
    cfg.random_seed = 1337;
    cfg.player_name = "Tester";                                  // not the system user
    return cfg;
}

inline SDL_WindowEvent window_event(uint8_t what) {
    SDL_WindowEvent we{};
    we.type = SDL_WINDOWEVENT;
    we.event = what;
    return we;
}

// =====================================================================================================================================================
// The wide match screen (milestone M3): the same safety net for the 16:9 picture of 960 x 540 (golden numbers made at the commit that introduced it). Names "*.wide.*".
// The classic numbers above are untouched; these pin the wide frame (14 pieces, 6 of them stretched), the HUD's panel moved by (320, 0) and (320, 60), the windows of the original
// (pages centred over a clay margin, dialogs over the middle of the map view), the pointer over the 960 x 540 picture, the 762 x 500 view and the maps that are smaller than it.
// =====================================================================================================================================================


const ScreenLayout kWide = ScreenLayout::with_size(960, 540);

constexpr int32_t kWW = 960;                                  // the wide screen
constexpr int32_t kWH = 540;
constexpr uint64_t kWPixels = static_cast<uint64_t>(kWW) * static_cast<uint64_t>(kWH);

template <class F>
inline Measure sweep_wide(F&& per_pixel) {
    Fnv64 h;
    for (int32_t y = 0; y < kWH; ++y) {
        for (int32_t x = 0; x < kWW; ++x) per_pixel(h, x, y);
    }
    return Measure{h.value(), kWPixels};
}

// ---- the pointer over the 960 x 540 picture

/// The edge-scroll step at every pixel of the 960 x 540 picture, from the given view origins on a map of tiles_w x tiles_h tiles
inline Measure wide_edge_sweep(int32_t tiles_w, int32_t tiles_h, int32_t rate, const std::vector<Cam>& cams) {
    Fnv64 h;
    uint64_t n = 0;
    for (const Cam& c : cams) {
        for (int32_t y = 0; y < kWH; ++y) {
            for (int32_t x = 0; x < kWW; ++x) {
                hash_scroll(h, edge_scroll_step(x, y, rate, c.x, c.y, tiles_w, tiles_h, kWide));
                ++n;
            }
        }
    }
    return Measure{h.value(), n};
}

// =====================================================================================================================================================
// The self-check: the fingerprints can fail
// =====================================================================================================================================================


/// The parameters of a fixed list of calls (one of every kind); the sensitivity check changes one of them by one at a time
constexpr size_t kParamCount = 47;

using Params = std::array<int32_t, kParamCount>;

/// Issues the calls with the given parameters (the order is fixed: the sensitivity check also swaps and drops)
inline void replay(FrameRecorder& r, const Params& p, bool swap_first_two = false, bool drop_last = false) {
    auto color = [&](size_t i) { return assets::ColorRGBA{static_cast<uint8_t>(p[i]), static_cast<uint8_t>(p[i + 1]), static_cast<uint8_t>(p[i + 2]), static_cast<uint8_t>(p[i + 3])}; };
    r.set_hud_team(static_cast<uint8_t>(p[0]));
    if (!swap_first_two) {
        r.draw_sprite(static_cast<uint32_t>(p[1]), p[2], p[3], p[4] != 0);
        r.draw_named_sprite(p[5] == 0 ? "scorcovr.bmp" : "scorcovr.bmq", p[6], p[7], p[8] != 0);
    } else {
        r.draw_named_sprite(p[5] == 0 ? "scorcovr.bmp" : "scorcovr.bmq", p[6], p[7], p[8] != 0);
        r.draw_sprite(static_cast<uint32_t>(p[1]), p[2], p[3], p[4] != 0);
    }
    r.fill_rect(p[9], p[10], p[11], p[12], color(13));
    r.draw_rect(p[17], p[18], p[19], p[20], color(21));
    r.draw_text(p[25] == 0 ? "Hello" : "Hellp", p[26], p[27], color(29), static_cast<FontSize>(p[28]));
    r.draw_text("x", p[33], p[34], assets::ColorRGBA{1, 2, 3, 4});
    r.set_clip_rect(p[35], p[36], p[37], p[38]);
    r.draw_sprite(static_cast<uint32_t>(p[44]), p[45], p[46], false);
    r.clear_clip_rect();
    uint8_t image[64];
    for (size_t i = 0; i < sizeof image; ++i) image[i] = static_cast<uint8_t>(i + 1u);
    image[15] = static_cast<uint8_t>(p[43]);
    if (!drop_last) r.draw_rgba_image(p[39], p[40], p[41], p[42], image);
}

inline Measure replayed(const Params& p, bool swap = false, bool drop = false) {
    FrameRecorder r;
    replay(r, p, swap, drop);
    return r.result();
}

}  // namespace

// The tests, run by main() in test_view_fingerprint.cpp
void self_check_scenarios();
void self_check_wide_scenarios();
void hud_scenarios(const assets::AssetArchive& arc);
void screen_scenarios(const assets::AssetArchive& arc);
void menu_scenarios(const assets::AssetArchive& arc);
void edge_scenarios();
void minimap_scenarios();
void camera_scenarios();
void cursor_scenarios();
void click_scenarios();
void screen_click_scenarios();
void world_pixel_scenarios(const assets::AssetArchive& arc);
void app_pixel_scenarios();
void app_pointer_scenarios();
void wide_scenarios(const assets::AssetArchive& arc);
