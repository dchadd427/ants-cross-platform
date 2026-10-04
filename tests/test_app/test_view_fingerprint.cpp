// View fingerprint: the safety net of the widescreen / zoom / touch work (milestone M0). It pins what the game DRAWS and what the pointer DOES at the classic
// 640 x 480 picture, as a few hundred 64-bit numbers, so that the later milestones (a parametric screen layout, a wide default picture, a wider HUD, zoom, touch)
// can prove that the classic picture stays exactly what it is. M0 itself changes no production code and no other test.
//
// WHAT IS FINGERPRINTED
//   1. Draw calls (names "hud.*", "screen.*"): HUD::render for 97 scenes (every local colour, the start dialog, selections of every ant type, the hill, the chat with
//      entries and typing, the minimap with all four teams, at every corner of the map and on maps of other sizes, fog of war, allied / dropped / absent score boxes,
//      a crowd, the quit / options / quick-help / alliance dialogs, hovered and pressed buttons, the rubber band) and the setup, network room, guest and results
//      screens. A recording IRenderer takes EVERY mutating call with ALL its parameters in final screen coordinates (sprite ids, named sprites, rectangles,
//      frames, fills, texts with position, size and colour, the HUD team, clips, the image bytes of the minimap); the fingerprint is FNV-1a 64 over a canonical
//      little-endian serialisation (no pointers, no padding, no floats, no locale) plus the number of calls.
//   2. The pointer (names "ptr.*", "view.camera.*"): for EVERY pixel of the 640 x 480 screen the step of the edge scroll (the 60 x 60 map at the default rate from a
//      camera away from the borders and at each of the eight borders and corners; other rates and map sizes; a ring outside the screen), the cursor that
//      HUD::evaluate_cursor chooses (34 selection states and cameras: idle, own ants, an enemy under inspection, hills, latched pedestals, fog, a dialog, a rubber band; every
//      cursor of the original is met), the minimap's hit test, world point and scroll, the playfield / chat classification, the start view, the camera's limits and its two
//      conversions, what a press and a release do (which control reacts, what is selected, which command is issued, what is heard; every zone found to the exact pixel) in 17 HUD states
//      and on the dialogs, the setup and results screens, and the application's own pointer gate.
//   3. Pixels (names "px.*"): what the real software renderer puts on the 640 x 480 canvas: Renderer::render_world at the corners and edges of two maps and at
//      the middle of the others (ants of every type, colour and facing, effects, score bubbles, fog, selection markers, hit point digits, a crowd, all straddling the
//      borders of the playfield), the single-frame cursors at every edge of the screen, and whole frames of an Application (the loading screen, the setup screen, the
//      quick help, the match screen at the corners of the map). TrueType text is never part of a pixel hash (its pixels depend on the machine's font library): the
//      boxes where the classic picture has text and the frame-rate plate are masked, with fixed rectangles. The numbers are the same on every machine (checked on
//      macOS / clang and on Linux / gcc).
//   4. A self-check: the hash of a recorded frame changes when one parameter of one recorded call changes, when two calls are swapped, when one call is dropped
//      (47 parameters), a masked pixel does not change a pixel hash and any other one does, and the pointer hashes move with the rate, the camera and a rectangle.
// The scenes are built by hand (a plain sim::WorldState, a sim::Grid, engine worlds made with the test hooks) from fixed formulas; nothing here reads the wall
// clock (the HUD's clock is injected, the renderer's animation clock is pinned), a random device, a file other than ants.chd and the original maps, or the
// network. The program runs in about two seconds of CPU time.
//
//   5. The wide match screen (milestone M3; names "*.wide.*", 226 more fingerprints with golden numbers made at the commit that introduced it, the classic ones above did not move):
//      the same families for the 16:9 picture of 960 x 540: HUD::render draw calls of 82 scenes (the frame in 14 pieces and 18 parts, the panel moved by (320, 0) and (320, 60), the
//      score slots of 2, 3, 4 teams, the pages of the original centred over the clay and the dialogs over the middle of the map view as origins, maps smaller than the view), the pointer
//      at every pixel of the 960 x 540 picture (edge strips from nine cameras and on small maps, the zones, the minimap, the cursor in 13 states, refined click sweeps), the camera of
//      the 762 x 500 view (a map smaller than the view is centred) and the pixels of the real renderer on the 960 x 540 canvas (six maps, nine cameras of two of them, two synthetic small
//      maps, and whole Application frames: the setup screen, the quick help and the results, each composed for the whole canvas (their wide pages: the quick help and the results were centred pages of the original until then), the match screen and its windows).
//
// WHAT IS NOT COVERED (the blind spots; the places that hard-code 640 / 480 / 442 / 440 / the margins with a covered / not covered note: docs/audit/M0_notes.md)
//   - the glyph pixels of TrueType text (the CALLS that draw it are fingerprinted: string, place, size, colour; the text boxes are masked in the pixel hashes);
//   - the frame-rate counter, its sparkline and the version text (Application::render_frame: their width depends on the font and the version changes every release);
//   - the network overlay (Application::render_net_overlay: it needs a network match and sockets);
//   - the four animated cursors (Move, Target, Attack, Food: Renderer::render_software_cursor follows the wall clock);
//   - the pedestal glow of the HUD (it follows SDL_GetTicks) and the tile grid overlay (a debug aid with TrueType text);
//   - how SDL scales the 640 x 480 canvas into a window (every hash is of the logical canvas read back at scale 1: fractional scales, bars in a full screen), the
//     save_screenshot output, sound and music, the web build.
//
// USAGE
//   test_view_fingerprint               runs everything and compares with the golden numbers below; a difference is reported as
//                                       "FAIL [scenario]: expected hash 0x... / N, actual hash 0x... / N" (N = draw calls, or samples for a sweep)
//   test_view_fingerprint --print       prints the golden table for regeneration. USE IT DELIBERATELY AND ONLY WHEN THE PICTURE (OR THE POINTER) IS MEANT TO
//                                       CHANGE (a milestone that changes the classic picture on purpose); NEVER TO SILENCE A FAILURE: a failure that you did not
//                                       cause is a regression of the classic 640 x 480 view and is to be fixed in the code, not in this file.
//   test_view_fingerprint --list        lists the scenarios
//   test_view_fingerprint --dump NAME   prints the recorded draw calls of a "hud.*" or "screen.*" scenario (to see what moved when its hash changed)
//   test_view_fingerprint --save DIR    writes the canvas of every "px.*" scenario as DIR/<name>.bmp (masked pixels magenta), to look at what a pixel hash is about
//   test_view_fingerprint --only PREFIX runs only the scenarios whose name starts with PREFIX (a developer's shortcut: the comparison for unused golden numbers is
//                                       skipped)
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

using namespace ants;
using namespace ants::app;

#if defined(__GNUC__)
#define VF_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define VF_PRINTF(a, b)
#endif

namespace {

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

int g_checks = 0;
int g_failures = 0;
bool g_print = false;                       // --print
bool g_keep_logs = false;                   // --dump: recorders keep a readable log
std::string g_dump_name;                    // --dump NAME
std::string g_only_prefix;                  // --only PREFIX
std::string g_save_dir;                     // --save DIR
std::vector<std::pair<std::string, Measure>> g_measured;
std::vector<std::string> g_seen;            // names that were compared (for the unused-golden check)

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what.c_str());
    }
}

std::string hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%016llx", static_cast<unsigned long long>(v));
    return buf;
}

bool wanted(const std::string& name) { return g_only_prefix.empty() || name.rfind(g_only_prefix, 0) == 0; }
/// Is any scenario of the group `prefix` wanted (a rig that is expensive to build is only built for a wanted group)
bool wanted_group(const std::string& prefix) { return g_only_prefix.empty() || prefix.rfind(g_only_prefix, 0) == 0 || g_only_prefix.rfind(prefix, 0) == 0; }

/// Compares a measure with its golden number (or collects it for --print)
void record(const std::string& name, const Measure& m) {
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

// =====================================================================================================================================================
// The recording renderer
// =====================================================================================================================================================

/// An IRenderer that records EVERY mutating call with ALL its parameters. Position, size, clip, flip, colour (alpha included), text, font size and the
/// image bytes are in the hash; the HUD team is a call of its own (it recolours the sprites that follow). get_text_width / get_text_height are
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

/// Records a finished frame (and prints its log for --dump)
void record_frame(const std::string& name, const FrameRecorder& rec) {
    if (!g_dump_name.empty() && name == g_dump_name) {
        std::printf("== %s: %llu calls, hash %s\n", name.c_str(), static_cast<unsigned long long>(rec.calls()), hex64(rec.result().hash).c_str());
        for (size_t i = 0; i < rec.log().size(); ++i) std::printf("%5zu  %s\n", i, rec.log()[i].c_str());
    }
    record(name, rec.result());
}

}  // namespace

// =====================================================================================================================================================
// The scenes: a hand-built world, the HUD, the camera
// =====================================================================================================================================================

namespace {

// The HUD's millisecond clock is injected: pedestals, the caret of the chat box, the options screen and the dialogs read it
uint32_t g_now_ms = 0;
uint32_t test_clock() { return g_now_ms; }

/// The metrics of the text that the HUD measures its chat and labels with (the interface's 6 px per character; the recorder is never hashed here)
FrameRecorder g_metrics;

uint32_t lcg(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

sim::TileCell& cell_of(sim::WorldState& w, uint32_t x, uint32_t y) { return w.cells[static_cast<size_t>(y) * w.width + x]; }

constexpr uint32_t kMap = 60;                      // the map of the scenes is 60 x 60 tiles = 1920 x 1920 px: the view reaches (1478, 1480) at most
constexpr int32_t kMaxCamX = static_cast<int32_t>(kMap) * 32 - PLAYFIELD_W;
constexpr int32_t kMaxCamY = static_cast<int32_t>(kMap) * 32 - PLAYFIELD_H;

/// The anchors of the four hills (green, red, blue, black = the remake's players 0 .. 3) on a 60 x 60 map
constexpr int32_t kHill[4][2] = {{9, 6}, {46, 6}, {9, 46}, {46, 46}};

/// A deterministic world: terrain bands (water, mud, gravel, slate and the grass between), rocks, food piles, a lunchbox, a fire wall, a bomb, power-ups,
/// plants, the four hills and some ants of every team. Everything from fixed formulas.
sim::WorldState make_world(const assets::AssetArchive& arc, uint32_t w = kMap, uint32_t h = kMap) {
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
    for (uint32_t i = 0; i < w * h / 40u; ++i) {                             // rocks (animation 293, colour 51) on walkable ground
        const uint32_t x = lcg(seed) % w;
        const uint32_t y = lcg(seed) % h;
        sim::TileCell& c = cell_of(world, x, y);
        if (c.terrain_type == sim::TERRAIN_WALKABLE) c.interactive_id = 293;
    }
    auto put = [&](uint32_t x, uint32_t y, uint16_t id) {
        if (x < w && y < h) cell_of(world, x, y).interactive_id = id;
    };
    put(20, 35, 253);                                                         // food piles (fdburgr1, colour 171)
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

void add_ant(sim::WorldState& world, uint32_t id, uint8_t player, sim::AntType type, int32_t tx, int32_t ty, int32_t ox = 16, int32_t oy = 16, bool holding = false) {
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
void add_default_ants(sim::WorldState& world) {
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

}  // namespace

// =====================================================================================================================================================
// HUD::render scenarios (recording renderer)
// =====================================================================================================================================================

namespace {

void hud_scenarios(const assets::AssetArchive& arc) {
    using T = sim::AntType;

    // The start of a match: the "Get ready" dialog over the whole HUD, for each local colour (the shell art, the sprites' colour and the backing fill depend on it)
    for (uint8_t p = 0; p < 4; ++p) {
        Scene s(arc, p);
        s.hud.start_match_modal();
        s.look(100, 80);
        s.shoot("hud.start.modal.p" + std::to_string(p));
    }
    {
        Scene s(arc, 0);
        s.hud.start_match_modal();
        s.tick(30);                                              // 1.5 s into the dialog: the portrait is at another frame
        s.shoot("hud.start.modal.1500ms");
        s.tick(70);                                              // 5.0 s: the modal has closed itself
        s.shoot("hud.start.modal.closed_by_timer");
    }
    // The same with the dialog closed: the idle HUD of each colour (welcome status, the "Game started" news flash, the score boxes)
    for (uint8_t p = 0; p < 4; ++p) {
        Scene s(arc, p);
        s.shoot("hud.start.idle.p" + std::to_string(p));
    }
    {
        Scene s(arc, 0);
        s.hud.start_match_modal();
        s.hud.dismiss_match_start_modal();
        s.shoot("hud.start.idle.dismissed");
    }
    {
        Scene s(arc, 0);                                         // the match clock: 12:00, 10:59, 1:01, 0:00 (the tens of minutes are skipped when 0)
        const uint32_t clocks[4] = {12u * 60000u, 10u * 60000u + 59000u, 61000u, 0u};
        for (uint32_t i = 0; i < 4; ++i) {
            s.world.match_time_remaining_ms = clocks[i];
            s.shoot("hud.clock." + std::to_string(clocks[i]));
        }
    }

    // Selections: one own ant of every type (status text of the type, the move pedestal, the ability pedestal, the Stop button)
    {
        const char* names[7] = {"worker", "worker2", "bomber", "fire", "thief", "combat", "swimmer"};
        for (uint32_t i = 0; i < 7; ++i) {
            Scene s(arc, 0);
            s.hud.select_ant(1 + i);
            s.tick(0);
            s.shoot(std::string("hud.sel.one.") + names[i]);
        }
    }
    {
        Scene s(arc, 0);                                         // several own ants: the group text, no ability pedestal
        s.hud.set_selected_ant_ids({1, 2, 3, 4, 5});
        s.tick(0);
        s.shoot("hud.sel.group");
    }
    {
        Scene s(arc, 0);                                         // every selected ant carries food: the lunchbox indicator
        for (auto& a : s.world.ants) if (a.player_id == 0) a.is_holding = true;
        s.hud.set_selected_ant_ids({1, 2});
        s.tick(0);
        s.shoot("hud.sel.group.carrying");
    }
    {
        Scene s(arc, 0);                                         // an enemy ant under inspection (the status line stays empty)
        s.hud.select_ant(12);
        s.tick(0);
        s.shoot("hud.sel.enemy");
    }
    {
        Scene s(arc, 0);                                         // the own hill: eggs, the hatch pedestal, the Stop button
        s.hud.select_base(0);
        s.tick(0);
        s.shoot("hud.sel.hill.own");
        Scene noeggs(arc, 0);
        noeggs.world.player_eggs[0] = 0;
        noeggs.hud.select_base(0);
        noeggs.tick(0);
        noeggs.shoot("hud.sel.hill.own.noeggs");
        Scene few(arc, 0);
        few.world.player_eggs[0] = 3;
        few.hud.select_base(0);
        few.tick(0);
        few.shoot("hud.sel.hill.own.3eggs");
        Scene enemy(arc, 0);                                     // another player's hill: the ally pedestal
        enemy.hud.select_base(1);
        enemy.tick(0);
        enemy.shoot("hud.sel.hill.enemy");
    }
    {
        Scene s(arc, 2);                                         // the same selections for another colour (blue)
        s.hud.select_ant(21);
        s.tick(0);
        s.shoot("hud.sel.one.p2");
    }
    {
        // the pedestals while they rise (the chains of Ants.exe FUN_01028491 play in real time): a few moments of the move pedestal and the ability pedestal
        const uint32_t moments[5] = {100, 250, 450, 700, 1200};
        for (uint32_t i = 0; i < 5; ++i) {
            Scene s(arc, 0);
            s.hud.select_ant(3);                                 // the bomber: both pedestals rise
            s.tick(0);
            s.shoot("hud.pedestal.rising." + std::to_string(moments[i]), moments[i]);
        }
    }

    // The chat: entries of every colour (wrapped bodies, a news flash), the text being typed with the caret on and off, the log scrolled, the buttons
    auto fill_chat = [](Scene& s) {
        s.hud.add_chat_entry("Alice", "Hello everybody, this is a rather long message that has to wrap over several lines of the log window.", false, 3);
        s.hud.add_chat_entry("Bob", "Team only: attack the red hill together!", true, 2);
        s.hud.add_chat_entry("Carol", "ok", false, 1);
        s.hud.add_news_flash(65000, "Black dropped out of the game!");
    };
    {
        Scene s(arc, 0);
        fill_chat(s);
        s.hud.set_chat_input("Typing a message to all of you");
        s.shoot("hud.chat.typing.caret_on", 10000);              // (10000 - 0) / 150 is even: the caret shows
        s.shoot("hud.chat.typing.caret_off", 10100);             // 10100 / 150 is odd: it does not
    }
    {
        Scene s(arc, 0);                                         // a text that is longer than the box: its end shows
        s.hud.set_chat_input("This message is much too long to fit into the one line box of the chat input field at the bottom");
        s.shoot("hud.chat.typing.long");
    }
    {
        Scene s(arc, 0);                                         // many entries: the log follows the newest (the window has moved)
        for (int i = 0; i < 14; ++i) s.hud.add_chat_entry("Player" + std::to_string(i), "message number " + std::to_string(i) + " of the long conversation", i % 3 == 0, i % 4);
        for (int i = 0; i < 60; ++i) {
            g_now_ms += 50;
            s.hud.update(s.world, 1);
        }
        s.shoot("hud.chat.scrolled", 9000);
    }
    {
        Scene s(arc, 0);                                         // an ally exists: the [Team] button is there; hovered / pressed art of the two buttons
        s.world.player_alliances = {1, 0, 255, 255};
        fill_chat(s);
        s.tick(0);                                               // refreshes the "is on team" flag
        s.shoot("hud.chat.team.buttons");
        s.move(555, 455);                                        // over [All] (532, 443, 44 x 24)
        s.shoot("hud.chat.team.hover_all");
        s.move(600, 455);                                        // over [Team] (579, 443, 46 x 24)
        s.shoot("hud.chat.team.hover_team");
        s.press(600, 455);
        s.shoot("hud.chat.team.pressed_team");
    }
    {
        Scene s(arc, 0);                                         // chat switched off in the options: the input box is covered, no buttons
        s.hud.options().chat = false;
        fill_chat(s);
        s.shoot("hud.chat.off");
    }

    // The minimap: ants of every team, plants, food, power-ups, a fire wall, rocks, the hills, the view frame at the corners and edges of the map
    {
        Scene s(arc, 0);
        s.shoot("hud.minimap.teams");
    }
    {
        const struct { const char* name; int32_t x, y; } cams[] = {
            {"tl", 0, 0}, {"tr", kMaxCamX, 0}, {"bl", 0, kMaxCamY}, {"br", kMaxCamX, kMaxCamY}, {"top", 700, 0}, {"left", 0, 700},
            {"mid", 700, 700}, {"beyond", kMaxCamX + 21, kMaxCamY + 33}};
        for (const auto& c : cams) {
            Scene s(arc, 0);
            s.look(c.x, c.y);
            s.shoot(std::string("hud.minimap.frame.") + c.name);
        }
    }
    {
        // other map sizes: the minimap's scale and the view frame's size depend on them (31 x 31 = TINY, 40 x 40 = SMALL, 14 x 14 and 80 x 50)
        const struct { uint32_t w, h; } sizes[] = {{31, 31}, {40, 40}, {14, 14}, {80, 50}};
        for (const auto& sz : sizes) {
            Scene s(arc, 0, sz.w, sz.h);
            s.look(static_cast<int32_t>(sz.w) * 32 - PLAYFIELD_W, static_cast<int32_t>(sz.h) * 32 - PLAYFIELD_H);
            if (s.camera.world_x < 0) s.look(0, 0);
            s.shoot("hud.minimap.size." + std::to_string(sz.w) + "x" + std::to_string(sz.h));
        }
    }
    {
        Scene s(arc, 0);                                         // no map at all (before the first snapshot): a black minimap box
        s.world = sim::WorldState{};
        s.shoot("hud.minimap.no_world");
    }

    // Fog of war: unexplored cells use the fog colours, ants and objects in the fog are hidden on the minimap
    {
        Scene s(arc, 0);
        s.world.fog_of_war_enabled = true;
        s.world.fog_revealed.assign(static_cast<size_t>(kMap) * kMap, 0);
        for (uint32_t y = 0; y < kMap; ++y) {
            for (uint32_t x = 0; x < kMap; ++x) {
                const int32_t dx = static_cast<int32_t>(x) - 14;
                const int32_t dy = static_cast<int32_t>(y) - 12;
                if (dx * dx + dy * dy < 120 || (x >= 40 && y >= 40 && (x + y) % 3 == 0)) s.world.fog_revealed[static_cast<size_t>(y) * kMap + x] = 1;
            }
        }
        s.hud.select_ant(1);
        s.tick(0);
        s.shoot("hud.fog.on");
    }

    // The score boxes: allied boxes (half and half, the summed score), a dropped team, an absent team, scores over six digits, names, 15 characters at most
    {
        Scene s(arc, 0);
        s.world.player_alliances = {1, 0, 255, 255};
        s.world.player_scores = {500, 500, 450, 100};
        s.shoot("hud.scores.allied");
        Scene t(arc, 2);                                         // the same from the point of view of the third team: two allied boxes in the bottom slots
        t.world.player_alliances = {1, 0, 255, 255};
        t.world.player_scores = {500, 500, 450, 100};
        t.shoot("hud.scores.allied.local_p2");
        Scene u(arc, 0);
        u.world.player_alliances = {255, 255, 3, 2};
        u.world.player_scores = {10, 20, 530, 530};
        u.shoot("hud.scores.allied.bottom_pair");
    }
    {
        Scene s(arc, 0);
        s.world.dropped_mask = 0x04;                             // team 2 dropped out: its box is covered
        s.shoot("hud.scores.dropped");
        Scene t(arc, 0);
        t.hud.set_roster_mask(0x0B);                             // team 2 never was in the match: no label, its box is covered
        t.shoot("hud.scores.absent");
        Scene u(arc, 1);
        u.hud.set_roster_mask(0x03);                             // a two player match, the local player is team 1
        u.shoot("hud.scores.two_players");
    }
    {
        Scene s(arc, 0);
        s.world.player_scores = {1234567, 999999, 0, 7};
        s.shoot("hud.scores.big");
    }
    {
        Scene s(arc, 0);
        s.hud.set_player_name("Local Player With A Long Name");
        s.hud.set_team_names({"", "Redmond", "A Very Long Name Indeed", "Dave"});
        s.shoot("hud.scores.names");
    }

    // A crowd: the minimap with 240 dots, the selection of a big group
    {
        Scene s(arc, 0);
        s.world.ants.clear();
        uint32_t seed = 777u;
        for (uint32_t i = 0; i < 240; ++i) {
            const int32_t tx = static_cast<int32_t>(lcg(seed) % 58u) + 1;
            const int32_t ty = static_cast<int32_t>(lcg(seed) % 58u) + 1;
            const int32_t ox = static_cast<int32_t>(lcg(seed) % 32u);                  // (one statement each: the order in which the arguments of a call are evaluated is the compiler's)
            const int32_t oy = static_cast<int32_t>(lcg(seed) % 32u);
            add_ant(s.world, 1000 + i, static_cast<uint8_t>(i % 4), static_cast<T>(i % 6), tx, ty, ox, oy, i % 5 == 0);
        }
        std::vector<uint32_t> own;
        for (const auto& a : s.world.ants) if (a.player_id == 0) own.push_back(a.id);
        s.hud.set_selected_ant_ids(own);
        s.look(300, 300);
        s.tick(0);
        s.shoot("hud.antheavy");
    }

    // Dialogs: the quit dialog (resting, hover Yes, pressed No), the options screen, the in-match quick help, the three alliance dialogs
    {
        Scene s(arc, 0);
        s.hud.open_quit_dialog();
        s.shoot("hud.quit.open");
        s.move(200, 270);                                        // Yes (180, 260, 49 x 24)
        s.shoot("hud.quit.hover_yes");
        s.move(300, 270);
        s.press(300, 270);                                       // No (292, 260)
        s.shoot("hud.quit.pressed_no");
    }
    {
        Scene s(arc, 0);
        s.hud.open_options();
        s.shoot("hud.options.open");
        s.shoot("hud.options.open.caret_off", 10150);
        Scene t(arc, 0);
        t.hud.options().sound_volume = 30;
        t.hud.options().music_volume = 80;
        t.hud.options().scroll_speed = 99;
        t.hud.options().chat = false;
        t.hud.options().quick_help = false;
        t.hud.options().quick_chat[0] = "Hello!";
        t.hud.options().quick_chat[2] = "A very long quick chat text that does not fit into its edit field of 141 pixels";
        t.hud.open_options();
        t.shoot("hud.options.changed");
        t.move(375, 435);                                        // hover Return (351, 425, 98 x 26)
        t.shoot("hud.options.hover_return");
    }
    {
        Scene s(arc, 0);
        s.hud.open_quick_help();
        s.shoot("hud.quickhelp.open");
        s.move(540, 445);                                        // the Return button (529, 437, 98 x 26)
        s.shoot("hud.quickhelp.hover_return");
        s.press(540, 445);
        s.shoot("hud.quickhelp.pressed_return");
    }
    {
        Scene s(arc, 1);                                         // an invitation from team 0 to the local team 1
        s.world.pending_invite_from[1] = 0;
        s.tick(0);
        s.shoot("hud.dialog.invitation");
        s.move(160, 270);                                        // Accept (152, 260, 80 x 24)
        s.shoot("hud.dialog.invitation.hover_accept");
        s.press(160, 270);
        s.shoot("hud.dialog.invitation.pressed_accept");
        Scene w(arc, 0);                                         // the proposer waits for the answer of team 1
        w.world.pending_invite_from[1] = 0;
        w.tick(0);
        w.shoot("hud.dialog.waiting");
        Scene b(arc, 0);                                         // the confirmation before an alliance is broken (an engine owns the alliance)
        b.sim.form_alliance(0, 2);
        b.hud.request_team_up(b.sim, 1);
        b.shoot("hud.dialog.breakconfirm");
        Scene r(arc, 1);                                         // an invitation that replaces the invitee's present team: string 2
        r.world.player_alliances = {255, 3, 255, 1};
        r.world.pending_invite_from[1] = 0;
        r.tick(0);
        r.shoot("hud.dialog.invitation.replaces_team");
    }

    // The top bar's buttons (the Help, Options, Quit art: resting art is part of the shell, hovering draws the small label, pressing replaces the art)
    {
        const struct { const char* name; int32_t x; } buttons[] = {{"help", 490}, {"options", 550}, {"quit", 600}};
        for (const auto& b : buttons) {
            Scene s(arc, 0);
            s.move(b.x, 18);
            s.shoot(std::string("hud.topbar.hover_") + b.name);
            s.press(b.x, 18);
            s.shoot(std::string("hud.topbar.pressed_") + b.name);
        }
    }

    // The rubber band over the map, the status line with a flash, an unknown pointer position
    {
        Scene s(arc, 0);
        s.press(120, 100);
        s.move(330, 260);
        s.shoot("hud.marquee.band");
        Scene t(arc, 0);
        t.press(300, 300);                                       // a stationary press is a 2 x 2 dot
        t.shoot("hud.marquee.dot");
        Scene u(arc, 0);
        u.press(120, 100);
        u.move(700, 700);                                        // the band is kept one pixel inside the view
        u.shoot("hud.marquee.clamped");
        Scene v(arc, 0);
        v.press(120, 100);
        v.move(2, 2);                                            // ... and one pixel inside the view's top left corner
        v.shoot("hud.marquee.clamped_low");
    }
    {
        Scene s(arc, 0);
        s.hud.post_status("Flash: the text is hidden on the odd steps", true);
        s.hud.update(s.world, 1);
        s.shoot("hud.status.flash.odd");
        s.hud.update(s.world, 1);
        s.shoot("hud.status.flash.even");
        s.hud.post_status("A status text that is much too long to fit into the status box of 139 pixels width");
        s.shoot("hud.status.long");
    }
}

}  // namespace

// =====================================================================================================================================================
// The full-screen screens (recording renderer): the setup screen, the network room and guest screens, the results screen
// =====================================================================================================================================================

namespace {

/// Records what `draw` puts on a fresh recorder
void frame(const std::string& name, const std::function<void(FrameRecorder&)>& draw) {
    if (!wanted(name)) return;
    FrameRecorder rec(g_keep_logs);
    draw(rec);
    record_frame(name, rec);
}

/// The setup screen over the six original maps (the file names, headers and sizes of the Maps folder: sorted by the byte order of the names)
MapSelectScreen make_setup_screen() {
    MapSelectScreen screen;
    screen.init(std::string(ORIGINAL_ASSETS_DIR) + "/Maps");
    return screen;
}

void click(MapSelectScreen& screen, int32_t x, int32_t y) {
    screen.handle_mouse_motion(x, y);
    screen.handle_mouse_down(x, y, 1);
    screen.handle_mouse_up(x, y, 1);
}

void screen_scenarios(const assets::AssetArchive& arc) {
    using MS = MapSelectScreen;

    // The setup screen of a local game
    {
        MS screen = make_setup_screen();
        frame("screen.setup.before_refresh", [&](FrameRecorder& r) { screen.render(r, arc); });                  // the labels, portraits and thumbs appear 500 ms after the screen is created
        screen.update(0.5f);
        frame("screen.setup.default", [&](FrameRecorder& r) { screen.render(r, arc); });                         // a fresh screen: TREASURE.LVL is highlighted (the one deliberate deviation: the original highlights the first entry)
        screen.set_selected_index(0);                                                                            // the series below walks the list from its first entry, as it always did
        frame("screen.setup.map0", [&](FrameRecorder& r) { screen.render(r, arc); });
        for (size_t i = 1; i < screen.get_maps().size(); ++i) {                                                  // every map of the list: its name, description, size and minutes
            screen.handle_key_down(SDLK_DOWN);
            frame("screen.setup.map" + std::to_string(i), [&](FrameRecorder& r) { screen.render(r, arc); });
        }
        screen.set_selected_index(2);
        click(screen, MS::BTN_FOW_ON_X + 5, MS::BTN_FOW_ON_Y + 5);
        frame("screen.setup.fog_on", [&](FrameRecorder& r) { screen.render(r, arc); });
        screen.handle_mouse_motion(MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        frame("screen.setup.hover_start", [&](FrameRecorder& r) { screen.render(r, arc); });
        screen.handle_mouse_down(MS::BTN_START_X + 5, MS::BTN_START_Y + 5, 1);
        frame("screen.setup.pressed_start", [&](FrameRecorder& r) { screen.render(r, arc); });
    }
    {
        MS screen = make_setup_screen();
        screen.update(0.5f);
        screen.handle_mouse_down(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3, 1);
        frame("screen.setup.pressed_up", [&](FrameRecorder& r) { screen.render(r, arc); });
        screen.handle_mouse_up(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3, 1);
        screen.handle_mouse_down(MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3, 1);
        frame("screen.setup.pressed_down", [&](FrameRecorder& r) { screen.render(r, arc); });
        screen.handle_mouse_up(MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3, 1);
        screen.handle_mouse_motion(MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
        frame("screen.setup.hover_leave", [&](FrameRecorder& r) { screen.render(r, arc); });
        screen.lock();
        frame("screen.setup.locked", [&](FrameRecorder& r) { screen.render(r, arc); });
    }
    {
        MS screen = make_setup_screen();                                                                         // another player's colour and name
        screen.set_player_name("Alice");
        screen.set_player_team(2);
        screen.update(0.5f);
        frame("screen.setup.team2_alice", [&](FrameRecorder& r) { screen.render(r, arc); });
        MS notice = make_setup_screen();                                                                         // a refusal on the status line of the local screen
        MS::RoomView local;
        local.status = "The host left the room.";
        notice.set_room(local);
        notice.update(0.5f);
        frame("screen.setup.notice", [&](FrameRecorder& r) { notice.render(r, arc); });
    }

    // The network room (a host with seats and thumbs) and the guest screen
    {
        MS screen = make_setup_screen();
        screen.update(0.5f);
        MS::RoomView view;
        view.networked = true;
        view.is_host = true;
        view.my_seat = 0;
        view.seats[0] = {true, "Alice", MS::Thumb::Good};
        view.seats[1] = {true, "Bob", MS::Thumb::Ok};
        view.seats[3] = {true, "A Very Long Player Name", MS::Thumb::Unknown};                                   // seat 2 is empty
        view.status = "Press START when all players' thumbs have appeared.";
        screen.set_room(view);
        frame("screen.room.host", [&](FrameRecorder& r) { screen.render(r, arc); });
        const struct { MS::Thumb thumb; const char* name; } qualities[] = {{MS::Thumb::Good, "good"}, {MS::Thumb::Ok, "ok"}, {MS::Thumb::Bad, "bad"}, {MS::Thumb::Unknown, "unknown"}};
        for (const auto& q : qualities) {
            MS::RoomView v = view;
            v.seats[1].thumb = q.thumb;
            screen.set_room(v);
            frame(std::string("screen.room.host.thumb_") + q.name, [&](FrameRecorder& r) { screen.render(r, arc); });
        }
        MS::RoomView full = view;
        full.seats[2] = {true, "Dave", MS::Thumb::Bad};
        full.seats[3].name = "Eve";
        full.status = "Waiting for Dave: the connection is bad.";
        screen.set_room(full);
        frame("screen.room.host.four_players", [&](FrameRecorder& r) { screen.render(r, arc); });

        MS::RoomView guest = view;                                                                               // the guest's screen (nh_start): no Up / Down / START / Fog buttons
        guest.is_host = false;
        guest.my_seat = 1;
        guest.map_file = screen.get_maps().empty() ? std::string() : screen.get_maps()[1].filename;
        MS guest_screen = make_setup_screen();
        guest_screen.update(0.5f);
        guest_screen.set_room(guest);
        guest_screen.follow_host_choice(guest.map_file, true);
        frame("screen.room.guest", [&](FrameRecorder& r) { guest_screen.render(r, arc); });
        guest_screen.follow_host_choice(guest.map_file, false);
        frame("screen.room.guest.fog_off", [&](FrameRecorder& r) { guest_screen.render(r, arc); });
        guest_screen.handle_mouse_motion(MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
        frame("screen.room.guest.hover_leave", [&](FrameRecorder& r) { guest_screen.render(r, arc); });
        MS::RoomView lost = guest;                                                                               // the guest after the host's connection is lost: the notice only
        lost.seats = {};
        lost.status = "The connection to the host was lost.";
        guest_screen.set_room(lost);
        frame("screen.room.guest.connection_lost", [&](FrameRecorder& r) { guest_screen.render(r, arc); });
        guest_screen.set_room(guest);
        guest_screen.update(0.0f);
        MS::RoomView server = guest;                                                                             // a room on a dedicated server: everybody is a guest, seat 3
        server.my_seat = 3;
        server.status = "Waiting for the host to start the game.";
        guest_screen.set_room(server);
        frame("screen.room.guest.server_seat3", [&](FrameRecorder& r) { guest_screen.render(r, arc); });
    }

    // The 16:9 setup screen of a room with its chat box (online rooms, protocol 11): the draw calls of the leader's screen (a server's room: the Online variant, the fill footer "Empty seats at START:" /
    // "Medium bots") and of a guest's (the Guest variant) with the lines of a conversation (a notice among them, in the status text's size), the typed line and the caret on and off, and with
    // an empty box; the text's places, sizes and colours are what these pin (the pixel fingerprints below mask the TrueType text). The map list holds the six original maps; ISLANDS is the room's.
    {
        const auto room_screen = [&](bool leader) {
            MS screen = make_setup_screen();
            screen.set_wide_layout(true);
            screen.update(0.5f);
            MS::RoomView view;
            view.networked = true;
            view.is_host = false;
            view.leader = leader;
            view.my_seat = leader ? 0 : 1;
            view.seats[0] = {true, leader ? "Ana" : "Lea", MS::Thumb::Good};
            view.seats[1] = {true, leader ? "Ben" : "Ben", leader ? MS::Thumb::Ok : MS::Thumb::Good};
            view.map_file = "ISLANDS.LVL";
            view.status = leader ? "Press START: the empty seats get Medium bots." : "Waiting for the host to start the game...";
            screen.set_room(view);
            return screen;
        };
        const auto conversation = [](bool typed, bool caret) {
            MS::ChatPanel panel;
            panel.visible = true;
            panel.lines = {{"Fog of War is on, so START seats no bots.", true}, {"Ben: hi, which map?", false}, {"Ana: TINY first, then ISLANDS", false}, {"Ben: ok, ready when you are", false}};
            if (typed) panel.typed = "ready in a minute";
            panel.caret = caret;
            return panel;
        };
        MS leader = room_screen(true);
        MS::ChatPanel empty_box;
        empty_box.visible = true;
        leader.set_chat_panel(empty_box);
        leader.set_fill_footer("Empty seats at START:", "Medium bots");
        frame("screen.wide.room.leader_empty", [&](FrameRecorder& r) { leader.render(r, arc); });
        leader.set_chat_panel(conversation(false, false));
        frame("screen.wide.room.leader_lines", [&](FrameRecorder& r) { leader.render(r, arc); });
        leader.set_chat_panel(conversation(true, true));
        frame("screen.wide.room.leader_typed_caret", [&](FrameRecorder& r) { leader.render(r, arc); });
        leader.set_chat_panel(conversation(true, false));
        frame("screen.wide.room.leader_typed", [&](FrameRecorder& r) { leader.render(r, arc); });
        MS guest = room_screen(false);
        guest.set_chat_panel(empty_box);
        frame("screen.wide.room.guest_empty", [&](FrameRecorder& r) { guest.render(r, arc); });
        guest.set_chat_panel(conversation(true, true));
        guest.set_fill_footer("Empty seats at START:", "Medium bots");                                        // (a guest has no such choice: nothing is drawn for it)
        frame("screen.wide.room.guest_typed_caret", [&](FrameRecorder& r) { guest.render(r, arc); });
    }
    {
        MS fresh = make_setup_screen();                                                                          // a room before the refresh: labels, portraits and thumbs are not shown yet
        MS::RoomView view;
        view.networked = true;
        view.is_host = true;
        view.seats[0] = {true, "Host", MS::Thumb::Good};
        fresh.set_room(view);
        frame("screen.room.before_refresh", [&](FrameRecorder& r) { fresh.render(r, arc); });
    }

    // The results screen: the waiting phase, the rows of 2 and of 4 teams (an alliance in one row), the Leave button, the animated portraits
    auto make_result = [](uint8_t present, bool allied) {
        sim::MatchResult result;
        result.is_over = true;
        result.present_mask = present;
        if (allied) result.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
        result.stats[0].score = 300;  result.stats[0].friendly_lost = 1;  result.stats[0].enemy_killed = 2;  result.stats[0].new_hatched = 3;
        result.stats[1].score = 200;  result.stats[1].friendly_lost = 10; result.stats[1].enemy_killed = 20; result.stats[1].new_hatched = 30;
        result.stats[2].score = 450;  result.stats[2].friendly_lost = 4;  result.stats[2].enemy_killed = 5;  result.stats[2].new_hatched = 6;
        result.stats[3].score = 100;  result.stats[3].friendly_lost = 1234; result.stats[3].enemy_killed = 0; result.stats[3].new_hatched = 0;
        result.decide_winners();
        return result;
    };
    {
        ScorecardModal card;                                                                                     // two teams
        card.set_player_names({"Alice", "Bob", "", ""});
        card.set_shown_teams(0x03);
        card.show(make_result(0x03, false), 0);
        frame("screen.results.2teams.waiting", [&](FrameRecorder& r) { card.render(r, arc); });
        card.update(0.25f);
        frame("screen.results.2teams.rows", [&](FrameRecorder& r) { card.render(r, arc); });
        card.update(0.6f);
        frame("screen.results.2teams.rows.later", [&](FrameRecorder& r) { card.render(r, arc); });
        card.handle_mouse_motion(550, 20);                                                                       // the Leave button (525, 12, 99 x 22)
        frame("screen.results.2teams.hover_leave", [&](FrameRecorder& r) { card.render(r, arc); });
        card.handle_mouse_down(550, 20);
        frame("screen.results.2teams.pressed_leave", [&](FrameRecorder& r) { card.render(r, arc); });
    }
    {
        ScorecardModal card;                                                                                     // four teams, 0 and 1 allied, the local team is the winner's ally
        card.set_player_names({"Alice", "Bob", "Carol", "Dave"});
        card.show(make_result(0x0F, true), 0);
        frame("screen.results.4teams.waiting", [&](FrameRecorder& r) { card.render(r, arc); });
        card.update(0.25f);
        frame("screen.results.4teams.rows", [&](FrameRecorder& r) { card.render(r, arc); });
        ScorecardModal loser;                                                                                    // the same for team 3 (the loser's cue and order)
        loser.set_player_names({"Alice", "Bob", "Carol", "Dave"});
        loser.show(make_result(0x0F, true), 3);
        loser.update(0.4f);
        frame("screen.results.4teams.rows.local3", [&](FrameRecorder& r) { loser.render(r, arc); });
        ScorecardModal plain;                                                                                    // four teams without alliance, default names, a 35 character name
        plain.set_player_names({"A Name Of Exactly Thirty-Five Chars", "", "", ""});
        sim::MatchResult res = make_result(0x0F, false);
        res.quitter = 1;
        res.decide_winners();
        plain.show(res, 2);
        plain.update(0.9f);
        frame("screen.results.4teams.quitter", [&](FrameRecorder& r) { plain.render(r, arc); });
        ScorecardModal three;                                                                                    // a team that is not in the match has no row
        three.set_player_names({"", "", "", ""});
        three.show(make_result(0x0D, false), 0);
        three.update(0.3f);
        frame("screen.results.3teams.gap", [&](FrameRecorder& r) { three.render(r, arc); });
    }
}

// The desktop start menu (include/ants_app/start_menu.hpp; remake only, so these fingerprints are new: none of the classic screens above changes): every panel, a selection, the
// bots, the fields, a failure, the room's code
void menu_scenarios(const assets::AssetArchive& arc) {
    const auto menu_frame = [&](const std::string& name, const StartMenu& menu) { frame(name, [&](FrameRecorder& r) { render_start_menu(r, arc, menu); }); };
    const auto make = [](uint8_t own_seat) {
        StartMenu menu;
        MenuSettings settings;
        settings.name = "Player";
        menu.set_own_seat(own_seat);
        menu.set_settings(settings);
        menu.set_server(ServerAddress{});
        menu.show_main();
        return menu;
    };
    // (a key as a person presses it: the panel has been up for longer than StartMenu::kSettleMs, within which Enter and Space do nothing)
    const auto key = [](StartMenu& m, SDL_Keycode k) {
        if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_ESCAPE) m.update(static_cast<float>(StartMenu::kSettleMs + 10) / 1000.0f);
        m.on_key(k, 0, false);
    };
    {
        StartMenu m = make(0);
        menu_frame("screen.menu.main", m);
        key(m, SDLK_DOWN);
        menu_frame("screen.menu.main.second_selected", m);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);
        menu_frame("screen.menu.main.quit_selected", m);
        m.show_main("The connection to the other players was lost.");
        menu_frame("screen.menu.main.notice", m);
    }
    {
        StartMenu m = make(0);
        key(m, SDLK_RETURN);
        menu_frame("screen.menu.single.empty", m);
        key(m, SDLK_DOWN);                                                                                       // the first row is selected on arrival: Black, then Blue
        key(m, SDLK_DOWN);
        key(m, SDLK_RIGHT);
        key(m, SDLK_UP);
        key(m, SDLK_RIGHT);
        key(m, SDLK_RIGHT);
        menu_frame("screen.menu.single.two_bots", m);
        StartMenu own = make(2);
        key(own, SDLK_RETURN);
        key(own, SDLK_DOWN);
        key(own, SDLK_DOWN);
        key(own, SDLK_RIGHT);
        key(own, SDLK_RIGHT);
        key(own, SDLK_RIGHT);
        menu_frame("screen.menu.single.own_seat_2", own);
    }
    {
        StartMenu m = make(0);
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        menu_frame("screen.menu.join.empty", m);
        m.on_text("demo-small-2p-x7k2");
        menu_frame("screen.menu.join.typed", m);
        m.update(0.15f);
        menu_frame("screen.menu.join.caret_off", m);
        key(m, SDLK_UP);
        m.on_text("Bot (Hard)");
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        menu_frame("screen.menu.join.name_refused", m);
        m.on_text("Maya");
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        menu_frame("screen.menu.connecting", m);
        m.connection_failed("There is no room with the code demo-small-2p-x7k2 on beta.playants.org:4001. Check the code (capital letters matter).");
        menu_frame("screen.menu.join.error", m);
    }
    {
        StartMenu m = make(0);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        menu_frame("screen.menu.host.default", m);
        key(m, SDLK_RIGHT);
        key(m, SDLK_RIGHT);
        key(m, SDLK_DOWN);
        key(m, SDLK_RIGHT);
        menu_frame("screen.menu.host.map_and_players", m);
        m.show_room("demo-medium-2p-k3n7pq", 1, 2);
        menu_frame("screen.menu.room.code", m);
        m.set_room_players(2, 2);
        menu_frame("screen.menu.room.full", m);
        m.show_room(std::string(32, 'W'), 1, 4);
        menu_frame("screen.menu.room.longest_code", m);
    }
    {   // the empty seats at START (network protocol 11): the choice on the Host panel, and what the room's panel says START will do
        StartMenu m = make(0);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);                                                                                       // map, players, then the empty seats
        key(m, SDLK_RIGHT);
        key(m, SDLK_RIGHT);
        menu_frame("screen.menu.host.fill_medium", m);
        m.show_room("demo-small-4p-b7x2qk", 1, 4);
        menu_frame("screen.menu.room.fill_medium", m);
    }
}

}  // namespace

// =====================================================================================================================================================
// The pointer: every pixel of the 640 x 480 screen
// =====================================================================================================================================================

namespace {

constexpr int32_t kW = 640;                       // the classic screen (a name of its own: edge_scroll.hpp has kScreenW / kScreenH)
constexpr int32_t kH = 480;
constexpr uint64_t kPixels = static_cast<uint64_t>(kW) * static_cast<uint64_t>(kH);

/// Calls `per_pixel(hash, x, y)` for every pixel, rows top to bottom
template <class F>
Measure sweep(F&& per_pixel) {
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
std::array<Cam, 9> nine_cams(int32_t tiles_w, int32_t tiles_h) {
    const int32_t mx = std::max(0, tiles_w * 32 - PLAYFIELD_W);
    const int32_t my = std::max(0, tiles_h * 32 - PLAYFIELD_H);
    const int32_t cx = mx >= 2 ? mx / 2 : 0;
    const int32_t cy = my >= 2 ? my / 2 : 0;
    return {{{"tl", 0, 0}, {"t", cx, 0}, {"tr", mx, 0}, {"l", 0, cy}, {"mid", cx, cy}, {"r", mx, cy}, {"bl", 0, my}, {"b", cx, my}, {"br", mx, my}}};
}

ViewportCamera camera_at(int32_t x, int32_t y) {
    ViewportCamera cam;
    cam.x = static_cast<float>(x);
    cam.y = static_cast<float>(y);
    cam.world_x = x;
    cam.world_y = y;
    return cam;
}

void hash_scroll(Fnv64& h, const EdgeScroll& e) {
    h.i32(e.dir);
    h.i32(e.dx);
    h.i32(e.dy);
}

/// The edge-scroll step at every pixel, for the given cameras on a map of `tiles_w` x `tiles_h` tiles
Measure edge_sweep(int32_t tiles_w, int32_t tiles_h, int32_t rate, const std::vector<Cam>& cams) {
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

std::vector<Cam> cams_of(int32_t tiles_w, int32_t tiles_h) {
    const auto nine = nine_cams(tiles_w, tiles_h);
    return std::vector<Cam>(nine.begin(), nine.end());
}

std::string map_name(int32_t w, int32_t h) { return std::to_string(w) + "x" + std::to_string(h); }

void edge_scenarios() {
    // The 60 x 60 map at the default scroll rate: one case for every camera (away from the borders and at each of the eight borders and corners)
    for (const Cam& c : nine_cams(60, 60)) {
        const std::string name = "ptr.edge.60x60.rate50.cam_" + std::string(c.name);
        if (wanted(name)) record(name, edge_sweep(60, 60, 50, {c}));
    }
    // The other rates (the half extent is rate + 10) and other map sizes, over all nine cameras at once
    const struct { int32_t w, h, rate; } more[] = {{60, 60, 0}, {60, 60, 99}, {31, 31, 50}, {40, 40, 25}, {14, 14, 50}, {80, 50, 75}, {13, 9, 50}};
    for (const auto& m : more) {
        const std::string name = "ptr.edge." + map_name(m.w, m.h) + ".rate" + std::to_string(m.rate) + ".all9";
        if (wanted(name)) record(name, edge_sweep(m.w, m.h, m.rate, cams_of(m.w, m.h)));
    }
    // The pointer outside the screen (a window with bars around the picture reports positions beyond it): a ring of 16 pixels around the 640 x 480 pixels
    {
        const std::string name = "ptr.edge.60x60.rate50.outside_ring.mid";
        if (wanted(name)) {
            Fnv64 h;
            uint64_t n = 0;
            for (int32_t y = -16; y < kH + 16; ++y) {
                for (int32_t x = -16; x < kW + 16; ++x) {
                    if (x >= 0 && x < kW && y >= 0 && y < kH) continue;
                    hash_scroll(h, edge_scroll_step(x, y, 50, 739, 740, 60, 60));
                    ++n;
                }
            }
            record(name, Measure{h.value(), n});
        }
    }
}

// ---- the minimap: where the pointer is, what world point it is, how the view follows it; and the start view of a match

void minimap_scenarios() {
    record("ptr.minimap.rect", sweep([](Fnv64& h, int32_t x, int32_t y) { h.flag(HUD::in_minimap_rect(x, y)); }));
    record("ptr.class.map_rect", sweep([](Fnv64& h, int32_t x, int32_t y) { h.flag(HUD::in_map_rect(x, y)); }));
    {
        HUD hud;
        record("ptr.class.chat_view", sweep([&](Fnv64& h, int32_t x, int32_t y) { h.flag(hud.in_chat_view(x, y)); }));
    }
    // the world point under the pointer (no clamp) for several map sizes
    const struct { int32_t w, h; } sizes[] = {{60, 60}, {31, 31}, {40, 40}, {14, 14}, {80, 50}};
    for (const auto& s : sizes) {
        const std::string name = "ptr.minimap.point." + map_name(s.w, s.h);
        if (!wanted(name)) continue;
        record(name, sweep([&](Fnv64& h, int32_t x, int32_t y) {
                   int32_t wx = 0;
                   int32_t wy = 0;
                   minimap_point(x, y, s.w, s.h, wx, wy);
                   h.i32(wx);
                   h.i32(wy);
               }));
    }
    // one input tick with the button held on the minimap: the step that the view takes towards the point under the pointer, from every camera
    for (const auto& s : {std::pair<int32_t, int32_t>{60, 60}, std::pair<int32_t, int32_t>{31, 31}, std::pair<int32_t, int32_t>{14, 14}}) {
        const std::string name = "ptr.minimap.scroll." + map_name(s.first, s.second) + ".all9";
        if (!wanted(name)) continue;
        Fnv64 h;
        uint64_t n = 0;
        for (const Cam& c : nine_cams(s.first, s.second)) {
            for (int32_t y = 0; y < kH; ++y) {
                for (int32_t x = 0; x < kW; ++x) {
                    hash_scroll(h, minimap_scroll_step(x, y, c.x, c.y, s.first, s.second));
                    ++n;
                }
            }
        }
        record(name, Measure{h.value(), n});
    }
    // the start view of a match: the view that shows the square around the hill's anchor tile, for every anchor of several maps
    {
        const std::string name = "ptr.start_view.origins";
        if (wanted(name)) {
            Fnv64 h;
            uint64_t n = 0;
            for (const auto& s : sizes) {
                for (int32_t ty = 0; ty < s.h; ++ty) {
                    for (int32_t tx = 0; tx < s.w; ++tx) {
                        int32_t ox = 0;
                        int32_t oy = 0;
                        start_view_origin(tx, ty, s.w, s.h, ox, oy);
                        h.i32(ox);
                        h.i32(oy);
                        ++n;
                    }
                }
            }
            record(name, Measure{h.value(), n});
        }
    }
}

// ---- the camera: limits, centring, the two conversions

void camera_scenarios() {
    const struct { uint32_t w, h; } maps[] = {{60, 60}, {31, 31}, {14, 14}, {13, 9}, {80, 50}};
    if (wanted("view.camera.clamp")) {
        Fnv64 h;
        uint64_t n = 0;
        const float values[] = {-500.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 7.0f, 100.25f, 441.0f, 442.0f, 443.0f, 1000.0f, 1477.5f, 1478.0f, 1479.0f, 1600.0f, 5000.0f};
        for (const auto& m : maps) {
            for (float vx : values) {
                for (float vy : values) {
                    ViewportCamera cam;
                    cam.x = vx;
                    cam.y = vy;
                    cam.clamp_to_bounds(m.w, m.h);
                    h.i32(static_cast<int32_t>(cam.x * 4.0f));
                    h.i32(static_cast<int32_t>(cam.y * 4.0f));
                    h.i32(cam.world_x);
                    h.i32(cam.world_y);
                    ++n;
                }
            }
        }
        record("view.camera.clamp", Measure{h.value(), n});
    }
    if (wanted("view.camera.center_on")) {
        Fnv64 h;
        uint64_t n = 0;
        for (const auto& m : maps) {
            for (int32_t wy = -64; wy < static_cast<int32_t>(m.h) * 32 + 64; wy += 37) {
                for (int32_t wx = -64; wx < static_cast<int32_t>(m.w) * 32 + 64; wx += 41) {
                    ViewportCamera cam;
                    cam.center_on(wx, wy, m.w, m.h);
                    h.i32(cam.world_x);
                    h.i32(cam.world_y);
                    ++n;
                }
            }
        }
        record("view.camera.center_on", Measure{h.value(), n});
    }
    if (wanted("view.camera.scroll_pixels")) {
        Fnv64 h;
        uint64_t n = 0;
        for (const auto& m : maps) {
            ViewportCamera cam;
            for (int32_t i = 0; i < 400; ++i) {                      // a deterministic walk that hits every border of the map
                const int32_t dx = ((i * 7) % 61) - 30;
                const int32_t dy = ((i * 11) % 53) - 26;
                cam.scroll_pixels(dx * (1 + i / 50), dy * (1 + i / 50), m.w, m.h);
                h.i32(cam.world_x);
                h.i32(cam.world_y);
                ++n;
            }
        }
        record("view.camera.scroll_pixels", Measure{h.value(), n});
    }
    // screen -> world for every pixel, from several cameras; world -> screen for a lattice of world points
    for (const Cam& c : {Cam{"origin", 0, 0}, Cam{"mid", 739, 740}, Cam{"far", 1478, 1480}}) {
        const std::string name = std::string("view.camera.screen_to_world.") + c.name;
        if (wanted(name)) {
            const ViewportCamera cam = camera_at(c.x, c.y);
            record(name, sweep([&](Fnv64& h, int32_t x, int32_t y) {
                       int32_t wx = -1;
                       int32_t wy = -1;
                       const bool ok = cam.screen_to_world(x, y, wx, wy);
                       h.flag(ok);
                       if (ok) {
                           h.i32(wx);
                           h.i32(wy);
                       }
                   }));
        }
        const std::string name2 = std::string("view.camera.world_to_screen.") + c.name;
        if (wanted(name2)) {
            const ViewportCamera cam = camera_at(c.x, c.y);
            Fnv64 h;
            uint64_t n = 0;
            for (int32_t wy = c.y - 80; wy < c.y + PLAYFIELD_H + 80; wy += 9) {
                for (int32_t wx = c.x - 80; wx < c.x + PLAYFIELD_W + 80; wx += 9) {
                    int32_t sx = 0;
                    int32_t sy = 0;
                    const bool in = cam.world_to_screen(wx, wy, sx, sy);
                    h.flag(in);
                    h.i32(sx);
                    h.i32(sy);
                    ++n;
                }
            }
            record(name2, Measure{h.value(), n});
        }
    }
}

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

/// How many pixels got each cursor, over all the cursor sweeps (the scenes must reach every cursor of the original)
std::array<uint64_t, 16> g_cursor_seen{};

Measure cursor_sweep(PointerScene& ps, HUD& hud, const ViewportCamera& cam) {
    const sim::WorldState& world = ps.world();
    const sim::Grid& grid = ps.sim.grid();
    return sweep([&](Fnv64& h, int32_t x, int32_t y) {
        const CursorType c = hud.evaluate_cursor(x, y, world, grid, cam);
        ++g_cursor_seen[static_cast<size_t>(c)];
        h.byte(static_cast<uint8_t>(c));
    });
}

void cursor_scenarios() {
    const std::vector<CursorState> states = {
        {"idle", false, [](HUD&, PointerScene&, ViewportCamera&) {}},
        {"own_worker", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.worker); }},
        {"own_group", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.set_selected_ant_ids({p.worker, p.worker2}); }},
        {"own_bomber", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.bomber); }},
        {"own_thief", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.thief); }},
        {"own_combat", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.combat); }},
        {"bomber_latched", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.bomber);
             h.handle_mouse_down(560, 180, SDL_BUTTON_LEFT, p.sim, cam);            // the ability pedestal (slot 2) latches
             h.handle_mouse_up(560, 180, SDL_BUTTON_LEFT, p.sim, cam);
         }},
        {"worker_move_latched", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(500, 170, SDL_BUTTON_LEFT, p.sim, cam);            // the move pedestal (slot 1) latches
             h.handle_mouse_up(500, 170, SDL_BUTTON_LEFT, p.sim, cam);
         }},
        {"enemy_inspected", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.foe); }},
        {"hill_own", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.select_base(0); }},
        {"hill_enemy", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.select_base(1); }},
        {"fog_idle", true, [](HUD&, PointerScene&, ViewportCamera&) {}},
        {"fog_own_worker", true, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.worker); }},
        {"options_open", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.open_options(); }},
        {"band_dragging", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(100, 100, SDL_BUTTON_LEFT, p.sim, cam);
             h.handle_mouse_motion(300, 260, p.sim, cam);                           // a rubber band wider than 4 px
         }},
        {"button_captured", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(500, 18, SDL_BUTTON_LEFT, p.sim, cam);             // the Help button holds the pointer: the edge strips do not run
         }},
    };
    // every state from the middle camera (the objects are in view) ...
    for (const CursorState& s : states) {
        const std::string name = std::string("ptr.cursor.") + s.name + ".cam_mid400";
        if (!wanted(name)) continue;
        PointerScene ps(s.fog);
        HUD hud;
        hud.set_ticks_function(&test_clock);
        g_now_ms = 0;
        hud.init(0);
        hud.set_sim_query(&ps.sim);
        ViewportCamera cam = camera_at(400, 400);
        s.setup(hud, ps, cam);
        record(name, cursor_sweep(ps, hud, cam));
    }
    // ... and the plain selection from the corners and edges of the map (the scroll arrows depend on where the view can still move)
    for (const char* state : {"idle", "own_worker"}) {
        for (const Cam& c : nine_cams(60, 60)) {
            const std::string name = std::string("ptr.cursor.") + state + ".cam_" + c.name;
            if (!wanted(name)) continue;
            PointerScene ps;
            HUD hud;
            hud.set_ticks_function(&test_clock);
            g_now_ms = 0;
            hud.init(0);
            hud.set_sim_query(&ps.sim);
            if (std::string(state) == "own_worker") hud.select_ant(ps.worker);
            const ViewportCamera cam = camera_at(c.x, c.y);
            record(name, cursor_sweep(ps, hud, cam));
        }
    }
    if (g_only_prefix.empty()) {                                    // the scenes reach every cursor of the original (a sweep that never meets a cursor pins nothing about it)
        const struct { CursorType type; const char* name; } kinds[] = {{CursorType::Normal, "arrow"}, {CursorType::Select, "select"}, {CursorType::Move, "move"}, {CursorType::Target, "target"},
                                                                      {CursorType::Attack, "attack"}, {CursorType::ScrollN, "scroll N"}, {CursorType::ScrollNE, "scroll NE"},
                                                                      {CursorType::ScrollE, "scroll E"}, {CursorType::ScrollSE, "scroll SE"}, {CursorType::ScrollS, "scroll S"},
                                                                      {CursorType::ScrollSW, "scroll SW"}, {CursorType::ScrollW, "scroll W"}, {CursorType::ScrollNW, "scroll NW"},
                                                                      {CursorType::Food, "food"}};
        for (const auto& k : kinds) check(g_cursor_seen[static_cast<size_t>(k.type)] > 0, std::string("the cursor sweeps meet the ") + k.name + " cursor");
    }
}

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
Measure refined_sweep(const Probe& probe, int32_t stride, int32_t width = kW, int32_t height = kH) {
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

void click_scenarios() {
    struct State {
        const char* name;
        std::function<void(HUD&, PointerScene&)> setup;
        int32_t stride;
        uint8_t button;
        std::function<void(PointerScene&)> scene;               // changes the engine's world before the HUD looks at it (the invitations of the alliance dialogs)
        const char* expects;                                    // what some click must have done: c command, q quit, o option changed, d dialog answered, s selection, l latch, m view scrolled, p pointer captured, x a dialog closed
    };
    auto no_scene = [](PointerScene&) {};
    const std::vector<State> states = {
        {"idle", [](HUD&, PointerScene&) {}, 4, SDL_BUTTON_LEFT, no_scene, "mp"},
        {"own_worker", [](HUD& h, PointerScene& p) { h.select_ant(p.worker); }, 4, SDL_BUTTON_LEFT, no_scene, "csplmp"},
        {"own_bomber", [](HUD& h, PointerScene& p) { h.select_ant(p.bomber); }, 4, SDL_BUTTON_LEFT, no_scene, "csl"},
        {"own_group", [](HUD& h, PointerScene& p) { h.set_selected_ant_ids({p.worker, p.worker2, p.thief}); }, 4, SDL_BUTTON_LEFT, no_scene, "cs"},
        {"hill_own", [](HUD& h, PointerScene&) { h.select_base(0); }, 4, SDL_BUTTON_LEFT, no_scene, "cs"},
        {"hill_enemy", [](HUD& h, PointerScene&) { h.select_base(1); }, 4, SDL_BUTTON_LEFT, no_scene, "s"},
        // four hills and the alliance of the scene: the ally pedestal of an enemy hill asks before the team is broken
        {"hill_enemy.ally_break", [](HUD& h, PointerScene&) { h.select_base(1); }, 4, SDL_BUTTON_LEFT,
         [](PointerScene& p) { p.sim.grid_mut().set_anthill(2, sim::TileCoord{40, 40}); p.sim.grid_mut().set_anthill(3, sim::TileCoord{5, 45}); p.sim.tick(); }, "sd"},
        // no alliance and four hills: the ally pedestal of an enemy hill offers a team at once
        {"hill_enemy.ally_offer", [](HUD& h, PointerScene&) { h.select_base(1); }, 4, SDL_BUTTON_LEFT,
         [](PointerScene& p) { p.sim.break_alliance(0); p.sim.grid_mut().set_anthill(2, sim::TileCoord{40, 40}); p.sim.grid_mut().set_anthill(3, sim::TileCoord{5, 45}); p.sim.tick(); }, "cs"},
        {"own_worker.right_button", [](HUD& h, PointerScene& p) { h.select_ant(p.worker); }, 4, SDL_BUTTON_RIGHT, no_scene, "c"},
        {"own_bomber.right_button", [](HUD& h, PointerScene& p) { h.select_ant(p.bomber); }, 4, SDL_BUTTON_RIGHT, no_scene, "c"},
        // the dialogs and screens that take every click: where their buttons are
        {"dialog.quit", [](HUD& h, PointerScene&) { h.open_quit_dialog(); }, 4, SDL_BUTTON_LEFT, no_scene, "qx"},
        {"dialog.quickhelp", [](HUD& h, PointerScene&) { h.open_quick_help(); }, 4, SDL_BUTTON_LEFT, no_scene, "x"},
        {"dialog.options", [](HUD& h, PointerScene&) { h.open_options(); }, 4, SDL_BUTTON_LEFT, no_scene, "ox"},
        {"dialog.start_modal", [](HUD& h, PointerScene&) { h.start_match_modal(); }, 4, SDL_BUTTON_LEFT, no_scene, ""},
        {"dialog.invitation", [](HUD&, PointerScene&) {}, 4, SDL_BUTTON_LEFT, [](PointerScene& p) { p.sim.propose_alliance(1, 0); p.sim.tick(); }, "cd"},
        {"dialog.waiting", [](HUD&, PointerScene&) {}, 4, SDL_BUTTON_LEFT, [](PointerScene& p) { p.sim.propose_alliance(0, 1); p.sim.tick(); }, "cd"},
        {"dialog.breakconfirm", [](HUD& h, PointerScene& p) { h.request_team_up(p.sim, 1); }, 4, SDL_BUTTON_LEFT, no_scene, "cd"},
    };
    for (const State& s : states) {
        const std::string name = std::string("ptr.click.") + s.name;
        if (!wanted(name)) continue;
        PointerScene ps;
        s.scene(ps);
        ClickTally tally;
        const ClickProbe probe{ps, s.setup, s.button, &tally};
        const Measure m = refined_sweep(probe, s.stride);
        const uint64_t n = m.count;
        record(name, m);
        // the scenario does what it says: some click orders, answers, closes, latches, scrolls ... (a fingerprint of clicks that do nothing would pin nothing)
        for (const char* e = s.expects; *e != '\0'; ++e) {
            uint64_t seen = 0;
            const char* what = "";
            switch (*e) {
                case 'c': seen = tally.commands; what = "issues a command"; break;
                case 'q': seen = tally.quit; what = "quits"; break;
                case 'o': seen = tally.options; what = "changes an option"; break;
                case 'd': seen = tally.dialog; what = "opens or answers an alliance dialog"; break;
                case 's': seen = tally.selection; what = "keeps a selection"; break;
                case 'l': seen = tally.latched; what = "latches a pedestal"; break;
                case 'm': seen = tally.scrolled; what = "scrolls the view"; break;
                case 'p': seen = tally.captured; what = "captures the pointer"; break;
                case 'x': seen = tally.closed; what = "closes the dialog"; break;
                default: break;
            }
            check(seen > 0, std::string("click scenario ") + s.name + ": some click " + what);
        }
        check(tally.clicks == n, std::string("click scenario ") + s.name + ": every click was probed");
    }
}

/// The full-screen screens' buttons: the setup screen (a local host, a room's host, a room's guest) and the results screen, at every second pixel: which button is hovered or
/// pressed, which callback runs, what changes
void screen_click_scenarios() {
    using MS = MapSelectScreen;
    struct Variant {
        const char* name;
        bool networked;
        bool host;
    };
    for (const Variant& v : {Variant{"local", false, true}, Variant{"room_host", true, true}, Variant{"room_guest", true, false}}) {
        const std::string name = std::string("ptr.screen.setup.") + v.name;
        if (!wanted(name)) continue;
        MS base = make_setup_screen();
        base.update(0.5f);
        if (v.networked) {
            MS::RoomView view;
            view.networked = true;
            view.is_host = v.host;
            view.my_seat = v.host ? uint8_t{0} : uint8_t{1};
            view.seats[0] = {true, "Alice", MS::Thumb::Good};
            view.seats[1] = {true, "Bob", MS::Thumb::Ok};
            base.set_room(view);
        }
        const auto probe = [&](Fnv64& h, int32_t x, int32_t y) {
            {
                MS screen = base;
                int started = 0;
                int left = 0;
                int maps = 0;
                int fogs = 0;
                std::vector<uint32_t> sounds;
                screen.set_on_start([&](const std::string&) { ++started; });
                screen.set_on_quit([&]() { ++left; });
                screen.set_on_map_changed([&](const std::string&) { ++maps; });
                screen.set_on_fog_changed([&](bool) { ++fogs; });
                screen.set_on_play_sfx([&](uint32_t id) { sounds.push_back(id); });
                screen.handle_mouse_motion(x, y);
                h.flag(screen.up_button().hovered());
                h.flag(screen.down_button().hovered());
                h.flag(screen.start_button().hovered());
                screen.handle_mouse_down(x, y, 1);
                h.flag(screen.up_button().pressed());
                h.flag(screen.down_button().pressed());
                h.flag(screen.start_button().pressed());
                screen.handle_mouse_up(x, y, 1);
                h.i32(started);
                h.i32(left);
                h.i32(maps);
                h.i32(fogs);
                h.i32(screen.get_selected_index());
                h.flag(screen.is_fog_of_war_enabled());
                h.flag(screen.is_locked());
                h.u32(static_cast<uint32_t>(sounds.size()));
                for (uint32_t id : sounds) h.u32(id);
            }
        };
        record(name, refined_sweep(probe, 4));
    }
    for (const bool rows : {false, true}) {
        const std::string name = std::string("ptr.screen.results.") + (rows ? "rows" : "waiting");
        if (!wanted(name)) continue;
        sim::MatchResult result;
        result.is_over = true;
        result.stats[0].score = 300;
        result.stats[1].score = 200;
        result.decide_winners();
        const auto probe = [&](Fnv64& h, int32_t x, int32_t y) {
            {
                ScorecardModal card;
                int left = 0;
                int replayed = 0;
                std::vector<uint32_t> sounds;
                card.set_on_quit([&]() { ++left; });
                card.set_on_replay([&]() { ++replayed; });
                card.set_on_play_sfx([&](uint32_t id) { sounds.push_back(id); });
                card.show(result, 0);
                if (rows) card.update(0.25f);
                card.handle_mouse_motion(x, y);
                h.flag(card.is_quit_hovered());
                h.flag(card.handle_mouse_down(x, y));
                h.flag(card.is_quit_pressed());
                h.flag(card.handle_mouse_up(x, y));
                h.i32(left);
                h.i32(replayed);
                h.u32(static_cast<uint32_t>(sounds.size()));
                for (uint32_t id : sounds) h.u32(id);
            }
        };
        record(name, refined_sweep(probe, 4));
    }
}

}  // namespace

// =====================================================================================================================================================
// What the real software renderer draws: the playfield at the corners and edges of the map (pixel hashes)
// =====================================================================================================================================================

namespace {

/// The pixels of the 640 x 480 canvas (RGBA); `mask` rectangles (x0, y0, x1, y1, half open) are cleared first
struct Canvas {
    std::vector<uint8_t> px = std::vector<uint8_t>(static_cast<size_t>(kW) * static_cast<size_t>(kH) * 4u, 0);
    bool ok{false};
};

Canvas read_canvas(SDL_Renderer* sr) {
    Canvas c;
    c.ok = SDL_RenderReadPixels(sr, nullptr, SDL_PIXELFORMAT_RGBA32, c.px.data(), kW * 4) == 0;
    if (!c.ok) std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
    return c;
}

struct MaskRect {
    int32_t x0, y0, x1, y1;
};

/// The hash of the canvas with the masked rectangles cleared (they are painted magenta in the files that --save writes)
Measure canvas_measure(Canvas& c, const std::vector<MaskRect>& mask = {}) {
    for (const MaskRect& m : mask) {
        for (int32_t y = std::max(0, m.y0); y < std::min(kH, m.y1); ++y) {
            for (int32_t x = std::max(0, m.x0); x < std::min(kW, m.x1); ++x) std::memset(&c.px[(static_cast<size_t>(y) * kW + static_cast<size_t>(x)) * 4u], 0, 4);
        }
    }
    Fnv64 h;
    h.bytes(c.px.data(), c.px.size());
    return Measure{h.value(), kPixels};
}

/// --save DIR: writes the canvas as DIR/<name>.bmp (opaque; the masked pixels magenta), to look at what a changed hash is about
void save_canvas(const std::string& name, const Canvas& c) {
    std::vector<uint8_t> shown = c.px;
    for (size_t i = 0; i < shown.size(); i += 4) {
        if (shown[i + 3] == 0) {
            shown[i] = 255;
            shown[i + 1] = 0;
            shown[i + 2] = 255;
        }
        shown[i + 3] = 255;
    }
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(shown.data(), kW, kH, 32, kW * 4, SDL_PIXELFORMAT_RGBA32);
    if (s == nullptr) return;
    if (SDL_SaveBMP(s, (g_save_dir + "/" + name + ".bmp").c_str()) != 0) std::fprintf(stderr, "cannot write %s/%s.bmp\n", g_save_dir.c_str(), name.c_str());
    SDL_FreeSurface(s);
}

/// Hashes (masked) and records one canvas
void record_canvas(const std::string& name, Canvas c, const std::vector<MaskRect>& mask = {}) {
    if (!wanted(name)) return;
    const Measure m = canvas_measure(c, mask);
    if (!g_save_dir.empty()) save_canvas(name, c);
    record(name, m);
}

/// The idle clip of every ant type (ag, ab, af, at, ac, as + "st" + direction digit + "01"): the snapshots of these scenes name their clip, so that what they show does not depend on
/// the engine's animation logic
int32_t idle_clip(const assets::AssetArchive& arc, int type, int dir_digit) {
    static const char kLetters[6] = {'g', 'b', 'f', 't', 'c', 's'};
    const std::string name = std::string("a") + kLetters[type % 6] + "st" + std::to_string(dir_digit) + "01";
    return arc.find_animation_id(name);
}

/// Ants of every type and colour, of every facing and mirrored clip, spread over the view of the camera (cx, cy) and 80 px beyond its edges, so that sprites straddle
/// every border of the playfield
void populate_view(sim::WorldState& world, const assets::AssetArchive& arc, int32_t cx, int32_t cy, int count, uint32_t seed, int32_t view_w = PLAYFIELD_W, int32_t view_h = PLAYFIELD_H) {
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
void add_effects(sim::WorldState& world, int32_t cx, int32_t cy, int32_t view_w = PLAYFIELD_W, int32_t view_h = PLAYFIELD_H) {
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

/// A real renderer over a hidden 640 x 480 window (the software renderer, the dummy video driver) with a map loaded, a pinned animation clock and a world of its own
struct RendererRig {
    RendererRig(const assets::AssetArchive& archive, const std::string& map) : arc(archive) {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);                                   // (an Application that ended before has quit SDL)
        win = SDL_CreateWindow("view-fingerprint", 0, 0, kW, kH, SDL_WINDOW_HIDDEN);
        if (win == nullptr) return;
        if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + map + ".LVL")) return;
        engine.init(level, 1337);
        if (!renderer.init(win, archive)) return;
        renderer.set_level(level);
        renderer.pin_animation_clock(1500);
        renderer.set_hud_team(0);
        world = engine.get_world_state();
        world.ants.clear();
        ok = true;
    }
    ~RendererRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    RendererRig(const RendererRig&) = delete;
    RendererRig& operator=(const RendererRig&) = delete;

    void look(int32_t x, int32_t y) {
        renderer.camera().x = static_cast<float>(x);
        renderer.camera().y = static_cast<float>(y);
        renderer.camera().clamp_to_bounds(level.width(), level.height());
    }
    int32_t cam_x() { return renderer.camera().world_x; }
    int32_t cam_y() { return renderer.camera().world_y; }

    /// One frame of the world pass (the HUD is not drawn: the canvas outside the playfield stays black), recorded as a pixel hash
    void shoot(const std::string& name, int32_t selected_id = -1, const std::vector<uint32_t>& selected_ids = {}, int32_t base_team = -1, bool hp = false) {
        if (!wanted(name)) return;
        renderer.set_show_hp(hp);
        renderer.begin_frame();
        renderer.render_world(world, engine.grid(), selected_id, selected_ids, false, false, -1, -1, base_team, 0.0f);
        record_canvas(name, read_canvas(renderer.get_sdl_renderer()));
    }

    const assets::AssetArchive& arc;
    SDL_Window* win{nullptr};
    assets::LevelData level;
    sim::SimulationEngine engine;
    Renderer renderer;
    sim::WorldState world;
    bool ok{false};
};

void world_pixel_scenarios(const assets::AssetArchive& arc) {
    // The playfield at the corners and edges of two maps (a 60 x 60 map and TINY, which is smaller than two views): ants of every type, colour and facing, effects and
    // score bubbles, all straddling the borders of the playfield; the canvas outside the playfield must stay black
    for (const char* map : {"MEDIUM", "TINY"}) {
        const int32_t tiles = std::string(map) == "TINY" ? 31 : 60;
        for (const Cam& c : nine_cams(tiles, tiles)) {
            const std::string name = std::string("px.world.") + map + ".cam_" + c.name;
            if (!wanted(name)) continue;
            RendererRig rig(arc, map);
            check(rig.ok, "the renderer rig for " + std::string(map) + " is up");
            if (!rig.ok) continue;
            rig.look(c.x, c.y);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 90, 4242u + static_cast<uint32_t>(c.x));
            add_effects(rig.world, rig.cam_x(), rig.cam_y());
            rig.shoot(name);
        }
    }
    // The other maps of the original (their terrain, objects and food): the corner and the middle
    for (const char* map : {"GAUNTLET", "ISLANDS", "SMALL", "TREASURE"}) {
        const int32_t tiles = std::string(map) == "SMALL" ? 40 : 60;
        for (const Cam& c : {nine_cams(tiles, tiles)[0], nine_cams(tiles, tiles)[4]}) {
            const std::string name = std::string("px.world.") + map + ".cam_" + c.name;
            if (!wanted(name)) continue;
            RendererRig rig(arc, map);
            check(rig.ok, "the renderer rig for " + std::string(map) + " is up");
            if (!rig.ok) continue;
            rig.look(c.x, c.y);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 40, 17u + static_cast<uint32_t>(c.x));
            rig.shoot(name);
        }
    }
    // Odd origins: a view one pixel off a border, a view at a tile boundary and half a tile off it
    for (const auto& o : {std::pair<int32_t, int32_t>{1, 1}, std::pair<int32_t, int32_t>{32, 64}, std::pair<int32_t, int32_t>{17, 49}, std::pair<int32_t, int32_t>{1477, 1479},
                          std::pair<int32_t, int32_t>{733, 1}}) {
        const std::string name = "px.world.MEDIUM.origin_" + std::to_string(o.first) + "_" + std::to_string(o.second);
        if (!wanted(name)) continue;
        RendererRig rig(arc, "MEDIUM");
        if (!rig.ok) continue;
        rig.look(o.first, o.second);
        populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 60, 99u + static_cast<uint32_t>(o.first));
        rig.shoot(name);
    }
    // Selection markers (the ears of every selected ant), the hill's brackets, the hit point digits, a crowd, the fog of war, the click marker
    if (wanted_group("px.world.MEDIUM.selected")) {
        RendererRig rig(arc, "MEDIUM");
        if (rig.ok) {
            rig.look(600, 600);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 60, 31337u);
            std::vector<uint32_t> ids;
            for (uint32_t i = 0; i < 60; i += 4) ids.push_back(5000u + i);
            rig.shoot("px.world.MEDIUM.selected", static_cast<int32_t>(ids.front()), ids);
            rig.shoot("px.world.MEDIUM.selected_hp", static_cast<int32_t>(ids.front()), ids, -1, true);
        }
    }
    if (wanted_group("px.world.MEDIUM.hill_selected")) {
        RendererRig rig(arc, "MEDIUM");
        if (rig.ok) {
            int32_t hx = 0;
            int32_t hy = 0;
            for (const auto& hill : rig.engine.grid().anthills()) {
                if (hill.team_id == 0) { hx = hill.x * 32; hy = hill.y * 32; break; }
            }
            rig.look(hx - 200, hy - 200);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 40, 777u);
            rig.shoot("px.world.MEDIUM.hill_selected", -1, {}, 0);
        }
    }
    if (wanted_group("px.world.MEDIUM.crowd")) {
        RendererRig rig(arc, "MEDIUM");
        if (rig.ok) {
            rig.look(300, 900);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 400, 5u);
            rig.shoot("px.world.MEDIUM.crowd");
        }
    }
    if (wanted_group("px.world.MEDIUM.fog")) {
        RendererRig rig(arc, "MEDIUM");
        if (rig.ok) {
            rig.look(500, 500);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 80, 2024u);
            rig.world.fog_of_war_enabled = true;
            rig.world.fog_revealed.assign(static_cast<size_t>(rig.world.width) * rig.world.height, 0);
            uint32_t seed = 6u;
            for (uint32_t y = 0; y < rig.world.height; ++y) {
                for (uint32_t x = 0; x < rig.world.width; ++x) {
                    const int32_t dx = static_cast<int32_t>(x) - 24;
                    const int32_t dy = static_cast<int32_t>(y) - 22;
                    const int32_t ex = static_cast<int32_t>(x) - 30;
                    const int32_t ey = static_cast<int32_t>(y) - 29;
                    const bool open = dx * dx + dy * dy < 30 || ex * ex + ey * ey < 12 || lcg(seed) % 11u == 0;
                    rig.world.fog_revealed[static_cast<size_t>(y) * rig.world.width + x] = open ? 1 : 0;
                }
            }
            rig.shoot("px.world.MEDIUM.fog");
            rig.look(0, 0);
            rig.shoot("px.world.MEDIUM.fog.corner_tl");
        }
    }
    if (wanted_group("px.world.MEDIUM.click_marker")) {
        RendererRig rig(arc, "MEDIUM");
        if (rig.ok) {
            rig.look(400, 400);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 20, 8u);
            rig.renderer.spawn_transient_effect("xmarks", 400 + 200, 400 + 150);
            rig.shoot("px.world.MEDIUM.click_marker");
        }
    }
    // The cursors that have one frame (the animated ones follow the wall clock): the hot spot and the clip at every edge of the screen
    {
        const struct { CursorType type; const char* name; } kinds[] = {{CursorType::Normal, "normal"}, {CursorType::Select, "select"}, {CursorType::ScrollN, "n"},
                                                                      {CursorType::ScrollNE, "ne"}, {CursorType::ScrollE, "e"}, {CursorType::ScrollSE, "se"},
                                                                      {CursorType::ScrollS, "s"}, {CursorType::ScrollSW, "sw"}, {CursorType::ScrollW, "w"},
                                                                      {CursorType::ScrollNW, "nw"}};
        if (wanted_group("px.cursor")) {
            RendererRig rig(arc, "TINY");
            if (rig.ok) {
                for (const auto& k : kinds) {
                    rig.renderer.begin_frame();
                    const int32_t spots[8][2] = {{320, 240}, {0, 0}, {639, 0}, {0, 479}, {639, 479}, {1, 240}, {320, 478}, {638, 100}};
                    for (const auto& s : spots) rig.renderer.render_software_cursor(k.type, s[0], s[1]);
                    record_canvas(std::string("px.cursor.") + k.name, read_canvas(rig.renderer.get_sdl_renderer()));
                }
            }
        }
    }
}

}  // namespace

// =====================================================================================================================================================
// What a whole Application frame puts on the canvas (a headless Application: the real renderer, the real HUD, the real screens)
// =====================================================================================================================================================

namespace {

/// TrueType text is the one thing whose pixels depend on the font library of the machine, so the places where the classic picture has text are masked (fixed rectangles,
/// the same on every machine: a text can never reach beyond the box of its label). Everything else is hashed. The rectangles are half open (x0, y0, x1, y1).
/// The frame-rate counter and the version text (the bottom right corner, right aligned at x = 632: their width depends on the font and the version changes every release)
const MaskRect kMaskPlate{470, 466, 640, 480};
/// The setup screen's labels: the map name (36, 312) 179 px, its description (36, 380) 293 px, the prompt (36, 447) 293 px, the four players' names (415, 95 + 50 i) 120 px
const std::vector<MaskRect> kMaskSetup = {{32, 308, 222, 352}, {32, 376, 336, 440}, {32, 443, 336, 480}, {411, 93, 539, 119}, {411, 143, 539, 169}, {411, 193, 539, 219}, {411, 243, 539, 269}};
/// The match screen's labels: the status line (481, 254) 139 px, the chat log (482, 299) 138 x 101, the chat input (481, 424) 139 px, the score labels (the local one (312..399, 4), the
/// bottom ones (5..101, 163..251, 312..399) x 464)
const std::vector<MaskRect> kMaskMatch = {{479, 252, 622, 268}, {480, 297, 622, 402}, {479, 422, 622, 438}, {310, 2, 402, 20}, {3, 462, 103, 480}, {161, 462, 253, 480}, {310, 462, 401, 480}};
/// The dialogs of the match screen: the "Get ready" dialog's two labels
const std::vector<MaskRect> kMaskStartModal = {{128, 108, 372, 272}, {128, 288, 372, 314}};
/// The quit dialog's prompt
const std::vector<MaskRect> kMaskQuit = {{128, 178, 392, 342}};

std::vector<MaskRect> masks(std::initializer_list<const std::vector<MaskRect>*> lists) {
    std::vector<MaskRect> all{kMaskPlate};
    for (const auto* l : lists) all.insert(all.end(), l->begin(), l->end());
    return all;
}

ApplicationConfig app_config() {
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

SDL_WindowEvent window_event(uint8_t what) {
    SDL_WindowEvent we{};
    we.type = SDL_WINDOWEVENT;
    we.event = what;
    return we;
}

/// An Application with the animation clock of its renderer and the clock of its HUD pinned. A headless one starts on the setup screen; one with a window
/// (SDL's dummy video and audio drivers: nothing is shown or heard) starts on the loading screen, which a headless application skips. The settings go
/// to a place that does not exist: neither the owner's stored options are read nor any file is written.
struct AppRig {
    explicit AppRig(bool window = false) {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg = app_config();
        cfg.headless = !window;
        if (window) cfg.settings_path = "view_fingerprint_no_such_folder/settings.ini";
        ok = app.init(cfg);
        if (!ok) return;
        app.renderer().pin_animation_clock(1500);
        app.hud().set_ticks_function(&test_clock);
        g_now_ms = 0;
        app.handle_window_event(window_event(SDL_WINDOWEVENT_LEAVE));      // the pointer is outside: no game cursor (the setup of a frame can bring it back)
    }
    void pointer_at(int32_t x, int32_t y) {
        app.handle_window_event(window_event(SDL_WINDOWEVENT_ENTER));
        app.note_pointer(x, y);
    }
    std::string map_path(const char* name) const { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL"; }
    /// A match on a map: the clocks are pinned again (the match's HUD has been reset) and the camera is where the start view put it
    bool start_match(const char* map) {
        if (!app.start_game(map_path(map))) return false;
        app.hud().reset();                                        // the chat box's caret starts from the pinned clock
        g_now_ms = 0;
        app.hud().start_match_modal();
        return true;
    }
    void shot(const std::string& name, const std::vector<MaskRect>& mask) {
        if (!wanted(name)) return;
        app.render_frame();
        record_canvas(name, read_canvas(app.renderer().get_sdl_renderer()), mask);
    }
    void look(int32_t x, int32_t y) {
        ViewportCamera& cam = app.renderer().camera();
        cam.x = static_cast<float>(x);
        cam.y = static_cast<float>(y);
        cam.clamp_to_bounds(app.sim().grid().width(), app.sim().grid().height());
    }
    Application app;
    bool ok{false};
};

void app_pixel_scenarios() {
    // The loading screen (an application with a window: the progress bar grows with the 50 ms ticks of the loading state)
    if (wanted_group("px.app.loading")) {
        AppRig rig(true);
        check(rig.ok, "the application with a window is up");
        if (rig.ok) {
            check(rig.app.state() == AppState::Loading, "an application with a window starts on the loading screen");
            const char* video = SDL_GetCurrentVideoDriver();
            const char* audio = SDL_GetCurrentAudioDriver();
            check(video != nullptr && std::string(video) == "dummy" && audio != nullptr && std::string(audio) == "dummy", "the window and the sound of this application are SDL's dummy drivers: nothing is shown or heard");
            rig.shot("px.app.loading.start", masks({}));
            for (int tick = 0; tick < 12; ++tick) rig.app.update_simulation(0.05f);
            rig.shot("px.app.loading.tick12", masks({}));
            for (int tick = 12; tick < 25; ++tick) rig.app.update_simulation(0.05f);
            rig.shot("px.app.loading.tick25", masks({}));
            rig.pointer_at(200, 300);                                                // the arrow over the loading screen
            rig.shot("px.app.loading.cursor", masks({}));
        }
    }
    // The setup screen: before its refresh (no labels), after it, with the START button hovered
    if (wanted_group("px.app.setup")) {
        AppRig rig;
        check(rig.ok, "the headless application is up");
        if (rig.ok) {
            check(rig.app.state() == AppState::MapSelect, "a headless application starts on the setup screen");
            rig.shot("px.app.setup.before_refresh", masks({}));
            rig.app.map_select().update(0.5f);
            rig.shot("px.app.setup.refreshed", masks({&kMaskSetup}));
            rig.app.map_select().handle_mouse_motion(MapSelectScreen::BTN_START_X + 5, MapSelectScreen::BTN_START_Y + 5);
            rig.shot("px.app.setup.hover_start", masks({&kMaskSetup}));
            rig.pointer_at(300, 200);                                                // the game's cursor on the setup screen (the arrow)
            rig.shot("px.app.setup.cursor", masks({&kMaskSetup}));
        }
    }
    // The quick help: START! resting, hovered, pressed
    if (wanted_group("px.app.quickhelp")) {
        AppRig rig;
        if (rig.ok) {
            rig.app.finish_loading();
            check(rig.app.state() == AppState::QuickHelp, "the quick help follows the loading screen");
            rig.shot("px.app.quickhelp.rest", masks({}));
            rig.app.quick_help_move(540, 445);
            rig.shot("px.app.quickhelp.hover_start", masks({}));
            rig.app.quick_help_press(540, 445);
            rig.shot("px.app.quickhelp.pressed_start", masks({}));
            rig.app.quick_help_release(700, 700);                                    // released off the button: cancelled
            rig.pointer_at(100, 100);
            rig.shot("px.app.quickhelp.cursor", masks({}));
        }
    }
    // The match screen: the "Get ready" dialog, then the HUD over the world at the start view and at the corners and edges of the map
    if (wanted_group("px.app.match")) {
        AppRig rig;
        if (rig.ok && rig.start_match("MEDIUM")) {
            check(rig.app.state() == AppState::Playing, "start_game puts the application into the match screen");
            rig.shot("px.app.match.modal", masks({&kMaskMatch, &kMaskStartModal}));
            rig.app.hud().dismiss_match_start_modal();
            rig.shot("px.app.match.start_view", masks({&kMaskMatch}));
            const int32_t tiles = static_cast<int32_t>(rig.app.sim().grid().width());
            for (const Cam& c : nine_cams(tiles, tiles)) {
                rig.look(c.x, c.y);
                rig.shot(std::string("px.app.match.cam_") + c.name, masks({&kMaskMatch}));
            }
            rig.look(700, 700);
            rig.app.hud().open_quit_dialog();
            rig.shot("px.app.match.quit_dialog", masks({&kMaskMatch, &kMaskQuit}));
            rig.app.hud().close_quit_dialog();
            rig.pointer_at(300, 300);                                                // the arrow over the map
            rig.shot("px.app.match.cursor_map", masks({&kMaskMatch}));
            rig.pointer_at(10, 240);                                                 // a scroll arrow (the map can scroll to the west from here)
            rig.shot("px.app.match.cursor_edge", masks({&kMaskMatch}));
        }
    }
}

/// The application's own pointer gate (it hands the pointer to the HUD's input task only while it is inside the 640 x 480 screen and inside the window): the step of the view
/// after one 50 ms input task, for every pointer position of the screen and eight pixels beyond it, from three cameras
void app_pointer_scenarios() {
    if (!wanted_group("ptr.app")) return;
    AppRig rig;
    if (!rig.ok || !rig.start_match("MEDIUM")) {
        check(false, "the application for the pointer sweeps is up");
        return;
    }
    rig.app.hud().dismiss_match_start_modal();
    const int32_t tiles = static_cast<int32_t>(rig.app.sim().grid().width());
    ViewportCamera& cam = rig.app.renderer().camera();
    const auto nine = nine_cams(tiles, tiles);
    for (const Cam& c : {nine[4], nine[0], nine[8]}) {
        const std::string name = std::string("ptr.app.edge_gate.cam_") + c.name;
        if (!wanted(name)) continue;
        Fnv64 h;
        uint64_t n = 0;
        uint64_t scrolled_inside = 0;
        for (int32_t y = -8; y < kH + 8; ++y) {
            for (int32_t x = -8; x < kW + 8; ++x) {
                cam.x = static_cast<float>(c.x);
                cam.y = static_cast<float>(c.y);
                cam.clamp_to_bounds(static_cast<uint32_t>(tiles), static_cast<uint32_t>(tiles));
                SDL_MouseMotionEvent ev{};
                ev.type = SDL_MOUSEMOTION;
                ev.x = x;
                ev.y = y;
                rig.app.handle_mouse_motion(ev);
                rig.app.handle_camera_panning(0.05f);
                h.i32(cam.world_x - c.x);
                h.i32(cam.world_y - c.y);
                if ((cam.world_x != c.x || cam.world_y != c.y) && x >= 0 && x < kW && y >= 0 && y < kH) ++scrolled_inside;
                ++n;
            }
        }
        check(scrolled_inside > 0, "the pointer sweep from camera " + std::string(c.name) + " scrolls the view somewhere");
        record(name, Measure{h.value(), n});
    }
    // a pointer that has left the window does not scroll the map, wherever it was last seen
    if (wanted("ptr.app.edge_gate.left_window")) {
        Fnv64 h;
        uint64_t n = 0;
        for (int32_t x = 0; x < kW; x += 3) {
            cam.x = static_cast<float>(nine[4].x);
            cam.y = static_cast<float>(nine[4].y);
            cam.clamp_to_bounds(static_cast<uint32_t>(tiles), static_cast<uint32_t>(tiles));
            SDL_MouseMotionEvent ev{};
            ev.type = SDL_MOUSEMOTION;
            ev.x = x;
            ev.y = 2;
            rig.app.handle_mouse_motion(ev);
            rig.app.handle_window_event(window_event(SDL_WINDOWEVENT_LEAVE));
            rig.app.handle_camera_panning(0.05f);
            h.i32(cam.world_x - nine[4].x);
            h.i32(cam.world_y - nine[4].y);
            h.flag(rig.app.pointer_outside());
            ++n;
        }
        record("ptr.app.edge_gate.left_window", Measure{h.value(), n});
    }
}

}  // namespace

// =====================================================================================================================================================
// The wide match screen (milestone M3): the same safety net for the 16:9 picture of 960 x 540 (golden numbers made at the commit that introduced it). Names "*.wide.*".
// The classic numbers above are untouched; these pin the wide frame (14 pieces, 6 of them stretched), the HUD's panel moved by (320, 0) and (320, 60), the windows of the original
// (pages centred over a clay margin, dialogs over the middle of the map view), the pointer over the 960 x 540 picture, the 762 x 500 view and the maps that are smaller than it.
// =====================================================================================================================================================

namespace {

const ScreenLayout kWide = ScreenLayout::with_size(960, 540);
constexpr int32_t kWW = 960;                                  // the wide screen
constexpr int32_t kWH = 540;
constexpr uint64_t kWPixels = static_cast<uint64_t>(kWW) * static_cast<uint64_t>(kWH);
constexpr int32_t kWViewW = 762;                              // its map view
constexpr int32_t kWViewH = 500;

template <class F>
Measure sweep_wide(F&& per_pixel) {
    Fnv64 h;
    for (int32_t y = 0; y < kWH; ++y) {
        for (int32_t x = 0; x < kWW; ++x) per_pixel(h, x, y);
    }
    return Measure{h.value(), kWPixels};
}

/// The nine view origins on a map of tiles_w x tiles_h tiles in the 762 x 500 view: away from the borders, at the four corners and the four edges; on an axis where the map is
/// smaller than the view there is one origin, the centred one (negative)
std::array<Cam, 9> wide_nine_cams(int32_t tiles_w, int32_t tiles_h) {
    const int32_t mx = tiles_w * 32 - kWViewW;
    const int32_t my = tiles_h * 32 - kWViewH;
    const int32_t x0 = mx > 0 ? 0 : mx / 2, x1 = mx > 0 ? mx / 2 : mx / 2, x2 = mx > 0 ? mx : mx / 2;
    const int32_t y0 = my > 0 ? 0 : my / 2, y1 = my > 0 ? my / 2 : my / 2, y2 = my > 0 ? my : my / 2;
    return {{{"tl", x0, y0}, {"t", x1, y0}, {"tr", x2, y0}, {"l", x0, y1}, {"mid", x1, y1}, {"r", x2, y1}, {"bl", x0, y2}, {"b", x1, y2}, {"br", x2, y2}}};
}

ViewportCamera wide_camera_at(int32_t x, int32_t y) {
    ViewportCamera cam = camera_at(x, y);
    cam.set_view(kWide.view());
    cam.centre_small_maps = true;
    return cam;
}

// ---- the HUD's draw calls at 960 x 540

void hud_wide_scenarios(const assets::AssetArchive& arc) {
    constexpr int32_t kMaxX = static_cast<int32_t>(kMap) * 32 - kWViewW;
    constexpr int32_t kMaxY = static_cast<int32_t>(kMap) * 32 - kWViewH;
    const auto make = [&](uint8_t local = 0, uint32_t w = kMap, uint32_t h = kMap) { return std::make_unique<Scene>(arc, local, w, h, &kWide); };

    for (uint8_t p = 0; p < 4; ++p) {                                           // the "Get ready" dialog over the wide frame, for each local colour
        auto s = make(p);
        s->hud.start_match_modal();
        s->look(100, 80);
        s->shoot("hud.wide.start.modal.p" + std::to_string(p));
    }
    for (uint8_t p = 0; p < 4; ++p) {
        auto s = make(p);
        s->shoot("hud.wide.start.idle.p" + std::to_string(p));
    }
    {
        const char* names[7] = {"worker", "worker2", "bomber", "fire", "thief", "combat", "swimmer"};
        for (uint32_t i = 0; i < 7; ++i) {
            auto s = make(0);
            s->hud.select_ant(1 + i);
            s->tick(0);
            s->shoot(std::string("hud.wide.sel.one.") + names[i]);
        }
    }
    {
        auto s = make(0);
        s->hud.set_selected_ant_ids({1, 2, 3, 4, 5});
        s->tick(0);
        s->shoot("hud.wide.sel.group");
        auto c = make(0);
        for (auto& a : c->world.ants) if (a.player_id == 0) a.is_holding = true;
        c->hud.set_selected_ant_ids({1, 2});
        c->tick(0);
        c->shoot("hud.wide.sel.group.carrying");
        auto e = make(0);
        e->hud.select_ant(12);
        e->tick(0);
        e->shoot("hud.wide.sel.enemy");
        auto own = make(0);
        own->hud.select_base(0);
        own->tick(0);
        own->shoot("hud.wide.sel.hill.own");
        auto few = make(0);
        few->world.player_eggs[0] = 3;
        few->hud.select_base(0);
        few->tick(0);
        few->shoot("hud.wide.sel.hill.own.3eggs");
        auto enemy = make(0);
        enemy->hud.select_base(1);
        enemy->tick(0);
        enemy->shoot("hud.wide.sel.hill.enemy");
        auto blue = make(2);
        blue->hud.select_ant(21);
        blue->tick(0);
        blue->shoot("hud.wide.sel.one.p2");
    }
    {   // the pedestals rising
        const uint32_t moments[3] = {100, 450, 1200};
        for (uint32_t i = 0; i < 3; ++i) {
            auto s = make(0);
            s->hud.select_ant(3);
            s->tick(0);
            s->shoot("hud.wide.pedestal.rising." + std::to_string(moments[i]), moments[i]);
        }
    }
    // the chat: the log takes the extra height (161 px), the input box is 60 px lower
    auto fill_chat = [](Scene& s) {
        s.hud.add_chat_entry("Alice", "Hello everybody, this is a rather long message that has to wrap over several lines of the log window.", false, 3);
        s.hud.add_chat_entry("Bob", "Team only: attack the red hill together!", true, 2);
        s.hud.add_chat_entry("Carol", "ok", false, 1);
        s.hud.add_news_flash(65000, "Black dropped out of the game!");
    };
    {
        auto s = make(0);
        fill_chat(*s);
        s->hud.set_chat_input("Typing a message to all of you");
        s->shoot("hud.wide.chat.typing.caret_on", 10000);
        s->shoot("hud.wide.chat.typing.caret_off", 10100);
        auto l = make(0);
        l->hud.set_chat_input("This message is much too long to fit into the one line box of the chat input field at the bottom");
        l->shoot("hud.wide.chat.typing.long");
        auto m = make(0);                                        // many entries: the taller log shows more of them
        for (int i = 0; i < 14; ++i) m->hud.add_chat_entry("Player" + std::to_string(i), "message number " + std::to_string(i) + " of the long conversation", i % 3 == 0, i % 4);
        for (int i = 0; i < 60; ++i) {
            g_now_ms += 50;
            m->hud.update(m->world, 1);
        }
        m->shoot("hud.wide.chat.scrolled", 9000);
        auto t = make(0);                                        // an ally: [Team] exists; hovered and pressed buttons ([All] (532, 443, 44 x 24) moved by (320, 60))
        t->world.player_alliances = {1, 0, 255, 255};
        fill_chat(*t);
        t->tick(0);
        t->shoot("hud.wide.chat.team.buttons");
        t->move(555 + 320, 455 + 60);
        t->shoot("hud.wide.chat.team.hover_all");
        t->move(600 + 320, 455 + 60);
        t->press(600 + 320, 455 + 60);
        t->shoot("hud.wide.chat.team.pressed_team");
        auto off = make(0);
        off->hud.options().chat = false;
        fill_chat(*off);
        off->shoot("hud.wide.chat.off");
    }
    // the minimap and its view frame (762 x 500 of the map) at the corners and edges; maps of other sizes, the small ones centred
    {
        auto s = make(0);
        s->shoot("hud.wide.minimap.teams");
        const struct { const char* name; int32_t x, y; } cams[] = {{"tl", 0, 0}, {"tr", kMaxX, 0}, {"bl", 0, kMaxY}, {"br", kMaxX, kMaxY}, {"top", 700, 0}, {"left", 0, 700}, {"mid", 700, 700}};
        for (const auto& c : cams) {
            auto t = make(0);
            t->look(c.x, c.y);
            t->shoot(std::string("hud.wide.minimap.frame.") + c.name);
        }
        const struct { uint32_t w, h; } sizes[] = {{31, 31}, {40, 40}, {14, 14}, {16, 16}, {12, 12}, {80, 50}, {13, 9}};
        for (const auto& sz : sizes) {
            auto t = make(0, sz.w, sz.h);
            t->camera.x = 100000.0f;                                // (the far corner, held by the camera's rule: a small axis is centred)
            t->camera.y = 100000.0f;
            t->camera.clamp_to_bounds(sz.w, sz.h);
            t->shoot("hud.wide.minimap.size." + std::to_string(sz.w) + "x" + std::to_string(sz.h));
        }
    }
    {
        auto s = make(0);                                        // fog
        s->world.fog_of_war_enabled = true;
        s->world.fog_revealed.assign(static_cast<size_t>(kMap) * kMap, 0);
        for (uint32_t y = 0; y < kMap; ++y) {
            for (uint32_t x = 0; x < kMap; ++x) {
                const int32_t dx = static_cast<int32_t>(x) - 14;
                const int32_t dy = static_cast<int32_t>(y) - 12;
                if (dx * dx + dy * dy < 120 || (x >= 40 && y >= 40 && (x + y) % 3 == 0)) s->world.fog_revealed[static_cast<size_t>(y) * kMap + x] = 1;
            }
        }
        s->hud.select_ant(1);
        s->tick(0);
        s->shoot("hud.wide.fog.on");
    }
    // the score boxes: slots by the team's order, whatever the number of teams
    {
        auto s = make(0);
        s->world.player_alliances = {1, 0, 255, 255};
        s->world.player_scores = {500, 500, 450, 100};
        s->shoot("hud.wide.scores.allied");
        auto b = make(0);
        b->world.player_alliances = {255, 255, 3, 2};
        b->world.player_scores = {10, 20, 530, 530};
        b->shoot("hud.wide.scores.allied.bottom_pair");
        auto d = make(0);
        d->world.dropped_mask = 0x04;
        d->shoot("hud.wide.scores.dropped");
        for (const uint8_t mask : std::array<uint8_t, 5>{0x03, 0x07, 0x0B, 0x0F, 0x0D}) {
            auto t = make(mask == 0x0D ? 1 : 0);
            t->hud.set_roster_mask(mask);
            t->shoot("hud.wide.scores.roster." + std::to_string(static_cast<unsigned>(mask)));
        }
        auto u = make(3);
        u->hud.set_roster_mask(0x09);
        u->shoot("hud.wide.scores.two_players.black_local");
        auto n = make(0);
        n->hud.set_player_name("Local Player With A Long Name");
        n->hud.set_team_names({"", "Redmond", "A Very Long Name Indeed", "Dave"});
        n->shoot("hud.wide.scores.names");
    }
    // the windows of the original: the dialogs over the middle of the map view (origin (137, 59)), the pages centred over the clay (origin (160, 30))
    {
        auto s = make(0);
        s->hud.open_quit_dialog();
        s->shoot("hud.wide.quit.open");
        s->move(200 + 137, 270 + 59);
        s->shoot("hud.wide.quit.hover_yes");
        s->move(300 + 137, 270 + 59);
        s->press(300 + 137, 270 + 59);
        s->shoot("hud.wide.quit.pressed_no");
    }
    {
        auto s = make(0);
        s->hud.open_options();
        s->shoot("hud.wide.options.open");
        auto t = make(0);
        t->hud.options().sound_volume = 30;
        t->hud.options().music_volume = 80;
        t->hud.options().scroll_speed = 99;
        t->hud.options().chat = false;
        t->hud.options().quick_chat[0] = "Hello!";
        t->hud.open_options();
        t->shoot("hud.wide.options.changed");
        t->move(375 + 160, 435 + 30);
        t->shoot("hud.wide.options.hover_return");
    }
    {
        auto s = make(0);
        s->hud.open_quick_help();
        s->shoot("hud.wide.quickhelp.open");
        s->move(540 + 160, 445 + 30);
        s->shoot("hud.wide.quickhelp.hover_return");
        s->press(540 + 160, 445 + 30);
        s->shoot("hud.wide.quickhelp.pressed_return");
    }
    {
        auto s = make(1);
        s->world.pending_invite_from[1] = 0;
        s->tick(0);
        s->shoot("hud.wide.dialog.invitation");
        s->move(160 + 137, 270 + 59);
        s->shoot("hud.wide.dialog.invitation.hover_accept");
        auto w = make(0);
        w->world.pending_invite_from[1] = 0;
        w->tick(0);
        w->shoot("hud.wide.dialog.waiting");
        auto b = make(0);
        b->sim.form_alliance(0, 2);
        b->hud.request_team_up(b->sim, 1);
        b->shoot("hud.wide.dialog.breakconfirm");
    }
    {   // the top bar's buttons, moved by (320, 0)
        const struct { const char* name; int32_t x; } buttons[] = {{"help", 490 + 320}, {"options", 550 + 320}, {"quit", 600 + 320}};
        for (const auto& b : buttons) {
            auto s = make(0);
            s->move(b.x, 18);
            s->shoot(std::string("hud.wide.topbar.hover_") + b.name);
            s->press(b.x, 18);
            s->shoot(std::string("hud.wide.topbar.pressed_") + b.name);
        }
    }
    {   // the rubber band over the wide map view
        auto s = make(0);
        s->press(120, 100);
        s->move(330, 260);
        s->shoot("hud.wide.marquee.band");
        auto t = make(0);
        t->press(120, 100);
        t->move(900, 700);                                       // held one pixel inside the view: it ends at (777, 520)
        t->shoot("hud.wide.marquee.clamped");
        auto u = make(0);
        u->press(600, 300);
        u->move(2, 2);
        u->shoot("hud.wide.marquee.clamped_low");
    }
    {
        auto s = make(0);
        s->hud.post_status("A status text that is much too long to fit into the status box of 139 pixels width");
        s->shoot("hud.wide.status.long");
    }
}

// ---- the pointer over the 960 x 540 picture

/// The edge-scroll step at every pixel of the 960 x 540 picture, from the given view origins on a map of tiles_w x tiles_h tiles
Measure wide_edge_sweep(int32_t tiles_w, int32_t tiles_h, int32_t rate, const std::vector<Cam>& cams) {
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

void ptr_wide_scenarios() {
    // the zones of the picture: the map view, the minimap, the chat log
    {
        HUD hud;
        hud.set_layout(kWide);
        record("ptr.wide.class.map_view", sweep_wide([&](Fnv64& h, int32_t x, int32_t y) { h.flag(hud.over_map(x, y)); }));
        record("ptr.wide.class.minimap", sweep_wide([&](Fnv64& h, int32_t x, int32_t y) { h.flag(hud.over_minimap(x, y)); }));
        record("ptr.wide.class.chat_view", sweep_wide([&](Fnv64& h, int32_t x, int32_t y) { h.flag(hud.in_chat_view(x, y)); }));
    }
    // the edge strips: every pixel of the picture from nine cameras of the 60 x 60 map, and the other maps (the small ones from their one, centred, camera)
    for (const Cam& c : wide_nine_cams(60, 60)) {
        const std::string name = "ptr.wide.edge.60x60.rate50.cam_" + std::string(c.name);
        if (wanted(name)) record(name, wide_edge_sweep(60, 60, 50, {c}));
    }
    const struct { int32_t w, h, rate; } more[] = {{60, 60, 0}, {60, 60, 99}, {31, 31, 50}, {40, 40, 25}, {14, 14, 50}, {16, 16, 50}, {12, 12, 50}, {80, 50, 75}, {13, 9, 50}};
    for (const auto& m : more) {
        const std::string name = "ptr.wide.edge." + map_name(m.w, m.h) + ".rate" + std::to_string(m.rate) + ".all9";
        if (!wanted(name)) continue;
        const auto nine = wide_nine_cams(m.w, m.h);
        record(name, wide_edge_sweep(m.w, m.h, m.rate, std::vector<Cam>(nine.begin(), nine.end())));
    }
    {   // the pointer outside the picture: a ring of 16 pixels around it
        const std::string name = "ptr.wide.edge.60x60.rate50.outside_ring.mid";
        if (wanted(name)) {
            Fnv64 h;
            uint64_t n = 0;
            for (int32_t y = -16; y < kWH + 16; ++y) {
                for (int32_t x = -16; x < kWW + 16; ++x) {
                    if (x >= 0 && x < kWW && y >= 0 && y < kWH) continue;
                    hash_scroll(h, edge_scroll_step(x, y, 50, 600, 700, 60, 60, kWide));
                    ++n;
                }
            }
            record(name, Measure{h.value(), n});
        }
    }
    // the minimap: the world point under every pixel, the view's step towards it from nine cameras
    const struct { int32_t w, h; } sizes[] = {{60, 60}, {31, 31}, {40, 40}, {14, 14}, {16, 16}, {80, 50}};
    for (const auto& s : sizes) {
        const std::string name = "ptr.wide.minimap.point." + map_name(s.w, s.h);
        if (!wanted(name)) continue;
        record(name, sweep_wide([&](Fnv64& h, int32_t x, int32_t y) {
                   int32_t wx = 0, wy = 0;
                   minimap_point(x, y, s.w, s.h, wx, wy, kWide);
                   h.i32(wx);
                   h.i32(wy);
               }));
    }
    for (const auto& s : {std::pair<int32_t, int32_t>{60, 60}, std::pair<int32_t, int32_t>{31, 31}, std::pair<int32_t, int32_t>{14, 14}, std::pair<int32_t, int32_t>{16, 16}}) {
        const std::string name = "ptr.wide.minimap.scroll." + map_name(s.first, s.second) + ".all9";
        if (!wanted(name)) continue;
        Fnv64 h;
        uint64_t n = 0;
        for (const Cam& c : wide_nine_cams(s.first, s.second)) {
            for (int32_t y = 0; y < kWH; ++y) {
                for (int32_t x = 0; x < kWW; ++x) {
                    hash_scroll(h, minimap_scroll_step(x, y, c.x, c.y, s.first, s.second, kWide));
                    ++n;
                }
            }
        }
        record(name, Measure{h.value(), n});
    }
    {   // the start view of every anchor tile of six maps in the 762 x 500 view
        const std::string name = "ptr.wide.start_view.origins";
        if (wanted(name)) {
            Fnv64 h;
            uint64_t n = 0;
            for (const auto& s : {std::pair<int32_t, int32_t>{60, 60}, std::pair<int32_t, int32_t>{31, 31}, std::pair<int32_t, int32_t>{40, 40}, std::pair<int32_t, int32_t>{14, 14},
                                  std::pair<int32_t, int32_t>{16, 16}, std::pair<int32_t, int32_t>{80, 50}}) {
                for (int32_t ty = 0; ty < s.second; ++ty) {
                    for (int32_t tx = 0; tx < s.first; ++tx) {
                        int32_t ox = 0, oy = 0;
                        start_view_origin(tx, ty, s.first, s.second, ox, oy, kWide);
                        h.i32(ox);
                        h.i32(oy);
                        ++n;
                    }
                }
            }
            record(name, Measure{h.value(), n});
        }
    }
}

void camera_wide_scenarios() {
    const struct { uint32_t w, h; } maps[] = {{60, 60}, {31, 31}, {14, 14}, {16, 16}, {12, 12}, {13, 9}, {80, 50}};
    for (const bool centre : {true, false}) {
        const std::string suffix = centre ? "" : ".corner";
        if (wanted("view.wide.camera.clamp" + suffix)) {
            Fnv64 h;
            uint64_t n = 0;
            const float values[] = {-500.0f, -1.0f, 0.0f, 0.5f, 7.0f, 100.25f, 441.0f, 761.0f, 762.0f, 763.0f, 1000.0f, 1157.5f, 1158.0f, 1159.0f, 1420.0f, 1600.0f, 5000.0f};
            for (const auto& m : maps) {
                for (float vx : values) {
                    for (float vy : values) {
                        ViewportCamera cam = wide_camera_at(0, 0);
                        cam.centre_small_maps = centre;
                        cam.x = vx;
                        cam.y = vy;
                        cam.clamp_to_bounds(m.w, m.h);
                        h.i32(static_cast<int32_t>(cam.x * 4.0f));
                        h.i32(static_cast<int32_t>(cam.y * 4.0f));
                        h.i32(cam.world_x);
                        h.i32(cam.world_y);
                        ++n;
                    }
                }
            }
            record("view.wide.camera.clamp" + suffix, Measure{h.value(), n});
        }
        if (wanted("view.wide.camera.center_on" + suffix)) {
            Fnv64 h;
            uint64_t n = 0;
            for (const auto& m : maps) {
                for (int32_t wy = -64; wy < static_cast<int32_t>(m.h) * 32 + 64; wy += 37) {
                    for (int32_t wx = -64; wx < static_cast<int32_t>(m.w) * 32 + 64; wx += 41) {
                        ViewportCamera cam = wide_camera_at(0, 0);
                        cam.centre_small_maps = centre;
                        cam.center_on(wx, wy, m.w, m.h);
                        h.i32(cam.world_x);
                        h.i32(cam.world_y);
                        ++n;
                    }
                }
            }
            record("view.wide.camera.center_on" + suffix, Measure{h.value(), n});
        }
        if (wanted("view.wide.camera.scroll_pixels" + suffix)) {
            Fnv64 h;
            uint64_t n = 0;
            for (const auto& m : maps) {
                ViewportCamera cam = wide_camera_at(0, 0);
                cam.centre_small_maps = centre;
                for (int32_t i = 0; i < 400; ++i) {
                    const int32_t dx = ((i * 7) % 61) - 30;
                    const int32_t dy = ((i * 11) % 53) - 26;
                    cam.scroll_pixels(dx * (1 + i / 50), dy * (1 + i / 50), m.w, m.h);
                    h.i32(cam.world_x);
                    h.i32(cam.world_y);
                    ++n;
                }
            }
            record("view.wide.camera.scroll_pixels" + suffix, Measure{h.value(), n});
        }
    }
    // screen -> world and world -> screen of a centred camera (a 16 x 16 map: the camera is at (-125, 0)) and of a camera in the middle of a big map
    for (const Cam& c : {Cam{"origin", 0, 0}, Cam{"mid", 700, 700}, Cam{"far", 1158, 1420}, Cam{"small16", -125, 6}}) {
        const std::string name = std::string("view.wide.camera.screen_to_world.") + c.name;
        if (wanted(name)) {
            const ViewportCamera cam = wide_camera_at(c.x, c.y);
            record(name, sweep_wide([&](Fnv64& h, int32_t x, int32_t y) {
                       int32_t wx = -1, wy = -1;
                       const bool ok = cam.screen_to_world(x, y, wx, wy);
                       h.flag(ok);
                       if (ok) {
                           h.i32(wx);
                           h.i32(wy);
                       }
                   }));
        }
        const std::string name2 = std::string("view.wide.camera.world_to_screen.") + c.name;
        if (wanted(name2)) {
            const ViewportCamera cam = wide_camera_at(c.x, c.y);
            Fnv64 h;
            uint64_t n = 0;
            for (int32_t wy = c.y - 80; wy < c.y + kWViewH + 80; wy += 9) {
                for (int32_t wx = c.x - 80; wx < c.x + kWViewW + 80; wx += 9) {
                    int32_t sx = 0, sy = 0;
                    const bool in = cam.world_to_screen(wx, wy, sx, sy);
                    h.flag(in);
                    h.i32(sx);
                    h.i32(sy);
                    ++n;
                }
            }
            record(name2, Measure{h.value(), n});
        }
    }
}

/// The cursor at every pixel of the wide picture for a fixed state of the selection (the scene of the classic sweeps, seen from (400, 400) in the 762 x 500 view)
Measure wide_cursor_sweep(PointerScene& ps, HUD& hud, const ViewportCamera& cam) {
    const sim::WorldState& world = ps.world();
    const sim::Grid& grid = ps.sim.grid();
    return sweep_wide([&](Fnv64& h, int32_t x, int32_t y) { h.byte(static_cast<uint8_t>(hud.evaluate_cursor(x, y, world, grid, cam))); });
}

void cursor_wide_scenarios() {
    const std::vector<CursorState> states = {
        {"idle", false, [](HUD&, PointerScene&, ViewportCamera&) {}},
        {"own_worker", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.worker); }},
        {"own_bomber", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.bomber); }},
        {"own_thief", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.thief); }},
        {"bomber_latched", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.bomber);
             h.handle_mouse_down(560 + 320, 180, SDL_BUTTON_LEFT, p.sim, cam);      // the ability pedestal (slot 2) latches, moved by dx
             h.handle_mouse_up(560 + 320, 180, SDL_BUTTON_LEFT, p.sim, cam);
         }},
        {"worker_move_latched", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(500 + 320, 170, SDL_BUTTON_LEFT, p.sim, cam);
             h.handle_mouse_up(500 + 320, 170, SDL_BUTTON_LEFT, p.sim, cam);
         }},
        {"enemy_inspected", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.foe); }},
        {"hill_own", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.select_base(0); }},
        {"fog_own_worker", true, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.worker); }},
        {"options_open", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.open_options(); }},
        {"quit_open", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.open_quit_dialog(); }},
        {"band_dragging", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(100, 100, SDL_BUTTON_LEFT, p.sim, cam);
             h.handle_mouse_motion(300, 260, p.sim, cam);
         }},
        {"button_captured", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(500 + 320, 18, SDL_BUTTON_LEFT, p.sim, cam);
         }},
    };
    for (const CursorState& s : states) {
        const std::string name = std::string("ptr.wide.cursor.") + s.name + ".cam_mid400";
        if (!wanted(name)) continue;
        PointerScene ps(s.fog);
        HUD hud;
        hud.set_ticks_function(&test_clock);
        g_now_ms = 0;
        hud.init(0);
        hud.set_layout(kWide);
        hud.set_sim_query(&ps.sim);
        ViewportCamera cam = wide_camera_at(400, 400);
        s.setup(hud, ps, cam);
        record(name, wide_cursor_sweep(ps, hud, cam));
    }
    for (const char* state : {"idle", "own_worker"}) {
        for (const Cam& c : wide_nine_cams(60, 60)) {
            const std::string name = std::string("ptr.wide.cursor.") + state + ".cam_" + c.name;
            if (!wanted(name)) continue;
            PointerScene ps;
            HUD hud;
            hud.set_ticks_function(&test_clock);
            g_now_ms = 0;
            hud.init(0);
            hud.set_layout(kWide);
            hud.set_sim_query(&ps.sim);
            if (std::string(state) == "own_worker") hud.select_ant(ps.worker);
            record(name, wide_cursor_sweep(ps, hud, wide_camera_at(c.x, c.y)));
        }
    }
}

void click_wide_scenarios() {
    struct State {
        const char* name;
        std::function<void(HUD&, PointerScene&)> setup;
        uint8_t button;
        const char* expects;
    };
    const std::vector<State> states = {
        {"idle", [](HUD&, PointerScene&) {}, SDL_BUTTON_LEFT, "mp"},
        {"own_worker", [](HUD& h, PointerScene& p) { h.select_ant(p.worker); }, SDL_BUTTON_LEFT, "csplmp"},
        {"own_bomber", [](HUD& h, PointerScene& p) { h.select_ant(p.bomber); }, SDL_BUTTON_LEFT, "csl"},
        {"hill_own", [](HUD& h, PointerScene&) { h.select_base(0); }, SDL_BUTTON_LEFT, "cs"},
        {"own_worker.right_button", [](HUD& h, PointerScene& p) { h.select_ant(p.worker); }, SDL_BUTTON_RIGHT, "c"},
        {"dialog.quit", [](HUD& h, PointerScene&) { h.open_quit_dialog(); }, SDL_BUTTON_LEFT, "qx"},
        {"dialog.quickhelp", [](HUD& h, PointerScene&) { h.open_quick_help(); }, SDL_BUTTON_LEFT, "x"},
        {"dialog.options", [](HUD& h, PointerScene&) { h.open_options(); }, SDL_BUTTON_LEFT, "ox"},
        {"dialog.start_modal", [](HUD& h, PointerScene&) { h.start_match_modal(); }, SDL_BUTTON_LEFT, ""},
    };
    for (const State& s : states) {
        const std::string name = std::string("ptr.wide.click.") + s.name;
        if (!wanted(name)) continue;
        PointerScene ps;
        ClickTally tally;
        ClickProbe probe{ps, s.setup, s.button, &tally};
        probe.layout = &kWide;
        const Measure m = refined_sweep(probe, 4, kWW, kWH);
        record(name, m);
        for (const char* e = s.expects; *e != '\0'; ++e) {
            uint64_t seen = 0;
            const char* what = "";
            switch (*e) {
                case 'c': seen = tally.commands; what = "issues a command"; break;
                case 'q': seen = tally.quit; what = "quits"; break;
                case 'o': seen = tally.options; what = "changes an option"; break;
                case 's': seen = tally.selection; what = "keeps a selection"; break;
                case 'l': seen = tally.latched; what = "latches a pedestal"; break;
                case 'm': seen = tally.scrolled; what = "scrolls the view"; break;
                case 'p': seen = tally.captured; what = "captures the pointer"; break;
                case 'x': seen = tally.closed; what = "closes the dialog"; break;
                default: break;
            }
            check(seen > 0, std::string("wide click scenario ") + s.name + ": some click " + what);
        }
        check(tally.clicks == m.count, std::string("wide click scenario ") + s.name + ": every click was probed");
    }
}

// ---- what the real renderer draws on the 960 x 540 canvas

struct WideCanvas {
    std::vector<uint8_t> px = std::vector<uint8_t>(static_cast<size_t>(kWW) * static_cast<size_t>(kWH) * 4u, 0);
};

WideCanvas read_wide_canvas(SDL_Renderer* sr) {
    WideCanvas c;
    if (SDL_RenderReadPixels(sr, nullptr, SDL_PIXELFORMAT_RGBA32, c.px.data(), kWW * 4) != 0) std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
    return c;
}

void record_wide_canvas(const std::string& name, WideCanvas c, const std::vector<MaskRect>& mask = {}) {
    if (!wanted(name)) return;
    for (const MaskRect& m : mask) {
        for (int32_t y = std::max(0, m.y0); y < std::min(kWH, m.y1); ++y) {
            for (int32_t x = std::max(0, m.x0); x < std::min(kWW, m.x1); ++x) std::memset(&c.px[(static_cast<size_t>(y) * kWW + static_cast<size_t>(x)) * 4u], 0, 4);
        }
    }
    Fnv64 h;
    h.bytes(c.px.data(), c.px.size());
    if (!g_save_dir.empty()) {
        std::vector<uint8_t> shown = c.px;
        for (size_t i = 0; i < shown.size(); i += 4) {
            if (shown[i + 3] == 0) {
                shown[i] = 255;
                shown[i + 1] = 0;
                shown[i + 2] = 255;
            }
            shown[i + 3] = 255;
        }
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(shown.data(), kWW, kWH, 32, kWW * 4, SDL_PIXELFORMAT_RGBA32);
        if (s != nullptr) {
            SDL_SaveBMP(s, (g_save_dir + "/" + name + ".bmp").c_str());
            SDL_FreeSurface(s);
        }
    }
    record(name, Measure{h.value(), kWPixels});
}

/// A real renderer over a hidden 960 x 540 window with the wide layout, a map loaded (or none: a synthetic world), a pinned clock
struct WideRendererRig {
    WideRendererRig(const assets::AssetArchive& archive, const std::string& map, uint32_t synthetic_tiles = 0) : arc(archive) {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        win = SDL_CreateWindow("view-fingerprint-wide", 0, 0, kWW, kWH, SDL_WINDOW_HIDDEN);
        if (win == nullptr || !renderer.init(win, archive)) return;
        renderer.set_canvas_size(kWW, kWH);
        renderer.set_layout(kWide);
        renderer.pin_animation_clock(1500);
        renderer.set_hud_team(0);
        if (synthetic_tiles > 0) {
            engine.init_test_world(synthetic_tiles, synthetic_tiles, 1, 600000);
        } else {
            if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + map + ".LVL")) return;
            engine.init(level, 1337);
            renderer.set_level(level);
        }
        world = engine.get_world_state();
        world.ants.clear();
        ok = true;
    }
    ~WideRendererRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    WideRendererRig(const WideRendererRig&) = delete;
    WideRendererRig& operator=(const WideRendererRig&) = delete;
    void look(int32_t x, int32_t y, uint32_t tiles_w, uint32_t tiles_h) {
        renderer.camera().x = static_cast<float>(x);
        renderer.camera().y = static_cast<float>(y);
        renderer.camera().clamp_to_bounds(tiles_w, tiles_h);
    }
    int32_t cam_x() { return renderer.camera().world_x; }
    int32_t cam_y() { return renderer.camera().world_y; }
    void shoot(const std::string& name, const std::vector<MaskRect>& mask = {}) {
        if (!wanted(name)) return;
        renderer.begin_frame();
        renderer.render_world(world, engine.grid(), -1, {}, false, false, -1, -1, -1, 0.0f);
        record_wide_canvas(name, read_wide_canvas(renderer.get_sdl_renderer()), mask);
    }
    const assets::AssetArchive& arc;
    SDL_Window* win{nullptr};
    assets::LevelData level;
    sim::SimulationEngine engine;
    Renderer renderer;
    sim::WorldState world;
    bool ok{false};
};

/// TrueType text is masked as in the classic frames: the boxes of the labels of the wide picture (fixed rectangles) and the plate in the canvas's corner
const MaskRect kWMaskPlate{790, 526, 960, 540};
/// The match screen's labels: the status line (801, 254) 139 px, the chat log (802, 299) 138 x 161, the chat input (801, 484) 139 px, the score labels (the local one at (632 .. 719, 4), the
/// bottom ones right aligned in [113, 209), [377, 465) and [632, 719) x 524: ScreenLayout::score_slot(1 .. 3) of the 960 x 540 picture, whose cuts move the classic slots (5 .. 101, 163 .. 251,
/// 312 .. 399) right by 108, 214 and 320). The masks of the two left ones used to lie at 323 .. 423 and 481 .. 573, beside their labels, so "Red:" and "Blue:" were hashed: SDL's alpha blit
/// rounds differently on x86-64 and on ARM, so the 22 fingerprints that show them failed on every x86-64 machine (the Linux and Windows jobs of .github/workflows/ci.yml found it)
const std::vector<MaskRect> kWMaskMatch = {{799, 252, 942, 268}, {800, 297, 942, 462}, {799, 482, 942, 498}, {630, 2, 722, 20}, {111, 522, 211, 540}, {375, 522, 467, 540}, {630, 522, 722, 540}};
/// The "Get ready" dialog's two labels and the quit dialog's prompt, moved by the dialog origin (137, 59)
const std::vector<MaskRect> kWMaskStartModal = {{265, 167, 509, 331}, {265, 347, 509, 373}};
const std::vector<MaskRect> kWMaskQuit = {{265, 237, 529, 401}};
/// The setup screen's labels in the wide screen (the setup screen of a 960 x 540 canvas is the whole canvas, no page: tests/test_app/test_wide_setup.cpp pins the screen in detail): the map's name,
/// its description and the prompt, the four players' names, the caption under the map preview. The preview itself is NOT masked: it is the game's own render of the map (map_preview.hpp), so the
/// three px.wide.app.setup.* fingerprints that show it (refreshed, hover_start, cursor) were regenerated when the preview changed from minimap colour blocks to that render (before_refresh has none)
const std::vector<MaskRect> kWMaskSetup = {{35, 370, 287, 392}, {35, 438, 401, 460}, {34, 505, 402, 529}, {733, 93, 857, 115}, {733, 143, 857, 165}, {733, 193, 857, 215}, {733, 243, 857, 265}, {349, 359, 657, 381}};
/// The options screen's edit fields and the results rows are text too: the whole page's text boxes are not known one by one, so the pages that carry TrueType text are masked as a
/// whole where it appears (the results' rows, the options' four quick-chat fields)
const std::vector<MaskRect> kWMaskResults = {{95, 262, 930, 290}, {95, 350, 930, 378}, {95, 400, 930, 428}};      // the three rows' text of the wide results page: the names (x 100) and the numbers (x 805 ...)
const std::vector<MaskRect> kWMaskOptions = {{250, 398, 396, 418}, {250, 430, 396, 450}, {460, 398, 606, 418}, {460, 430, 606, 450}};

std::vector<MaskRect> wide_masks(std::initializer_list<const std::vector<MaskRect>*> lists) {
    std::vector<MaskRect> all{kWMaskPlate};
    for (const auto* l : lists) all.insert(all.end(), l->begin(), l->end());
    return all;
}

void world_pixel_wide_scenarios(const assets::AssetArchive& arc) {
    // the playfield of 762 x 500 at the corners and edges of two maps (MEDIUM 60 x 60; TINY 31 x 31 = 992 px: narrower than 1.5 views), ants of every kind straddling the borders
    for (const char* map : {"MEDIUM", "TINY"}) {
        const int32_t tiles = std::string(map) == "TINY" ? 31 : 60;
        for (const Cam& c : wide_nine_cams(tiles, tiles)) {
            const std::string name = std::string("px.wide.world.") + map + ".cam_" + c.name;
            if (!wanted(name)) continue;
            WideRendererRig rig(arc, map);
            check(rig.ok, "the wide renderer rig for " + std::string(map) + " is up");
            if (!rig.ok) continue;
            rig.look(c.x, c.y, static_cast<uint32_t>(tiles), static_cast<uint32_t>(tiles));
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 140, 4242u + static_cast<uint32_t>(c.x), kWViewW, kWViewH);
            add_effects(rig.world, rig.cam_x(), rig.cam_y(), kWViewW, kWViewH);
            rig.shoot(name);
        }
    }
    for (const char* map : {"GAUNTLET", "ISLANDS", "SMALL", "TREASURE"}) {
        const int32_t tiles = std::string(map) == "SMALL" ? 40 : 60;
        for (const Cam& c : {wide_nine_cams(tiles, tiles)[0], wide_nine_cams(tiles, tiles)[4]}) {
            const std::string name = std::string("px.wide.world.") + map + ".cam_" + c.name;
            if (!wanted(name)) continue;
            WideRendererRig rig(arc, map);
            check(rig.ok, "the wide renderer rig for " + std::string(map) + " is up");
            if (!rig.ok) continue;
            rig.look(c.x, c.y, static_cast<uint32_t>(tiles), static_cast<uint32_t>(tiles));
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 60, 17u + static_cast<uint32_t>(c.x), kWViewW, kWViewH);
            rig.shoot(name);
        }
    }
    // the maps that are smaller than the view (a synthetic world: flat colours): centred, black around it; 16 x 16 is 12 px taller than the view, 12 x 12 is smaller on both axes
    for (const uint32_t tiles : {16u, 12u}) {
        const std::string name = "px.wide.world.small" + std::to_string(tiles);
        if (!wanted(name)) continue;
        WideRendererRig rig(arc, "", tiles);
        check(rig.ok, "the wide renderer rig for a " + std::to_string(tiles) + " x " + std::to_string(tiles) + " world is up");
        if (!rig.ok) continue;
        rig.look(100000, 100000, tiles, tiles);
        check(rig.cam_x() == (static_cast<int32_t>(tiles) * 32 - 762) / 2, "the " + std::to_string(tiles) + " x " + std::to_string(tiles) + " map is centred across the view");
        rig.shoot(name);
    }
}

// ---- whole application frames at 960 x 540: the pages and the match

struct WideAppRig {
    WideAppRig() {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg = app_config();
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        cfg.has_window_size = true;                              // the canvas and the window are the same size: one pixel of the canvas is one pixel read back
        cfg.window_w = kWW;
        cfg.window_h = kWH;
        cfg.window_width = kWW;
        cfg.window_height = kWH;
        ok = app.init(cfg);
        if (!ok) return;
        app.renderer().pin_animation_clock(1500);
        app.hud().set_ticks_function(&test_clock);
        g_now_ms = 0;
        app.handle_window_event(window_event(SDL_WINDOWEVENT_LEAVE));
    }
    void pointer_at(int32_t x, int32_t y) {
        app.handle_window_event(window_event(SDL_WINDOWEVENT_ENTER));
        app.note_pointer(x, y);
    }
    std::string map_path(const char* name) const { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL"; }
    bool start_match(const char* map) {
        if (!app.start_game(map_path(map))) return false;
        app.hud().reset();
        g_now_ms = 0;
        app.hud().start_match_modal();
        return true;
    }
    void shot(const std::string& name, const std::vector<MaskRect>& mask) {
        if (!wanted(name)) return;
        app.render_frame();
        record_wide_canvas(name, read_wide_canvas(app.renderer().get_sdl_renderer()), mask);
    }
    void look(int32_t x, int32_t y) {
        ViewportCamera& cam = app.renderer().camera();
        cam.x = static_cast<float>(x);
        cam.y = static_cast<float>(y);
        cam.clamp_to_bounds(app.sim().grid().width(), app.sim().grid().height());
    }
    Application app;
    bool ok{false};
};

void app_pixel_wide_scenarios() {
    // the setup screen of the 16:9 canvas (its wide version: the whole canvas) and the quick help (its wide page: the whole canvas too; the loading screen needs a window)
    if (wanted_group("px.wide.app.setup")) {
        WideAppRig rig;
        check(rig.ok, "the wide application is up");
        if (rig.ok) {
            check(rig.app.picture() == (LayoutRect{0, 0, 960, 540}), "the wide application's setup screen is the whole canvas (its own wide version, not a page)");
            rig.shot("px.wide.app.setup.before_refresh", wide_masks({}));
            rig.app.map_select().update(0.5f);
            rig.shot("px.wide.app.setup.refreshed", wide_masks({&kWMaskSetup}));
            rig.app.map_select().handle_mouse_motion(MapSelectScreen::BTN_START_X + 5 + 320, MapSelectScreen::BTN_START_Y + 5 + 60);        // START! of the wide screen
            rig.shot("px.wide.app.setup.hover_start", wide_masks({&kWMaskSetup}));
            rig.pointer_at(300, 200);
            rig.shot("px.wide.app.setup.cursor", wide_masks({&kWMaskSetup}));
        }
    }
    // the setup screen of a room in the 16:9 picture with its chat box: the art of the box (the black chat box, the input box, the label's place), the fill footer's place and the players' box; the
    // TrueType text (the chat's lines, the typed line, the label "Chat", the footer, the names) is masked, its draw calls are pinned by screen.wide.room.*
    if (wanted_group("px.wide.app.setup.room")) {
        for (const bool leader : {true, false}) {
            WideAppRig rig;
            if (!rig.ok) continue;
            MapSelectScreen::RoomView view;
            view.networked = true;
            view.is_host = false;
            view.leader = leader;
            view.my_seat = leader ? 0 : 1;
            view.seats[0] = {true, leader ? "Ana" : "Lea", MapSelectScreen::Thumb::Good};
            view.seats[1] = {true, "Ben", leader ? MapSelectScreen::Thumb::Ok : MapSelectScreen::Thumb::Good};
            view.map_file = "ISLANDS.LVL";
            view.status = leader ? "Press START: the empty seats get Medium bots." : "Waiting for the host to start the game...";
            rig.app.map_select().set_room(view);
            MapSelectScreen::ChatPanel panel;
            panel.visible = true;
            panel.lines = {{"Fog of War is on, so START seats no bots.", true}, {"Ben: hi, which map?", false}, {"Ana: TINY first, then ISLANDS", false}};
            panel.typed = "ready in a minute";
            panel.caret = true;
            rig.app.map_select().set_chat_panel(panel);
            if (leader) rig.app.map_select().set_fill_footer("Empty seats at START:", "Medium bots");
            rig.app.map_select().update(0.5f);
            const SetupLayout& layout = SetupLayout::of(leader ? SetupVariant::Online : SetupVariant::Guest);
            std::vector<MaskRect> text = kWMaskSetup;
            text.push_back({layout.chat.label_x - 2, layout.chat.label_y - 2, layout.chat.label_x + 46, layout.chat.label_y + 24});                 // "Chat"
            text.push_back({layout.chat.lines.x, layout.chat.lines.y, layout.chat.lines.x + layout.chat.lines.w, layout.chat.lines.y + layout.chat.lines.h});   // the lines
            text.push_back({layout.chat.input_text.x - 2, layout.chat.input_text.y - 2, layout.chat.input_text.x + layout.chat.input_text.w, layout.chat.input_text.y + 24});   // the typed line
            text.push_back({layout.footer_x - 2, layout.footer_y1 - 2, layout.footer_x + 200, layout.footer_y2 + 22});                                // the footer's two lines
            text.push_back({layout.caption_centre_x - 154, layout.caption_y - 2, layout.caption_centre_x + 154, layout.caption_y + 22});               // the caption under the map preview (the room layouts' own place: the single screen's is in kWMaskSetup)
            rig.shot(leader ? "px.wide.app.setup.room_leader" : "px.wide.app.setup.room_guest", wide_masks({&text}));
        }
    }
    if (wanted_group("px.wide.app.quickhelp")) {
        WideAppRig rig;
        if (rig.ok) {
            rig.app.finish_loading();
            check(rig.app.state() == AppState::QuickHelp, "the quick help follows the loading screen");
            rig.shot("px.wide.app.quickhelp.rest", wide_masks({}));
            rig.app.quick_help_move(895, 510);                                          // START! of the wide page (849, 497, 98 x 27: the bottom right corner)
            rig.shot("px.wide.app.quickhelp.hover_start", wide_masks({}));
        }
    }
    if (wanted_group("px.wide.app.results")) {
        WideAppRig rig;
        if (rig.ok && rig.start_match("SMALL")) {
            sim::MatchResult mr;
            mr.is_over = true;
            mr.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
            const int32_t scores[4] = {900, 600, 420, 100};
            for (size_t p = 0; p < 4; ++p) {
                mr.stats[p].score = scores[p];
                mr.stats[p].friendly_lost = static_cast<uint32_t>(5 + p);
                mr.stats[p].enemy_killed = static_cast<uint32_t>(18 - 4 * p);
                mr.stats[p].new_hatched = static_cast<uint32_t>(25 - 5 * p);
            }
            mr.decide_winners();
            rig.app.scorecard().show(mr, 0);
            rig.app.scorecard().update(0.25f);
            rig.shot("px.wide.app.results", wide_masks({&kWMaskResults}));
        }
    }
    // the match screen: the "Get ready" dialog, the start view of every map, the corners and edges of MEDIUM, the windows of the original
    if (wanted_group("px.wide.app.match")) {
        WideAppRig rig;
        if (rig.ok && rig.start_match("MEDIUM")) {
            check(rig.app.picture() == (LayoutRect{0, 0, 960, 540}), "the wide match is the whole canvas");
            rig.shot("px.wide.app.match.modal", wide_masks({&kWMaskMatch, &kWMaskStartModal}));
            rig.app.hud().dismiss_match_start_modal();
            rig.shot("px.wide.app.match.start_view", wide_masks({&kWMaskMatch}));
            const int32_t tiles = static_cast<int32_t>(rig.app.sim().grid().width());
            for (const Cam& c : wide_nine_cams(tiles, tiles)) {
                rig.look(c.x, c.y);
                rig.shot(std::string("px.wide.app.match.cam_") + c.name, wide_masks({&kWMaskMatch}));
            }
            rig.look(700, 700);
            rig.app.hud().open_quit_dialog();
            rig.shot("px.wide.app.match.quit_dialog", wide_masks({&kWMaskMatch, &kWMaskQuit}));
            rig.app.hud().close_quit_dialog();
            rig.app.hud().open_options();
            rig.shot("px.wide.app.match.options_page", wide_masks({&kWMaskMatch, &kWMaskOptions}));
            rig.app.hud().close_options();
            rig.app.hud().open_quick_help();
            rig.shot("px.wide.app.match.quickhelp_page", wide_masks({&kWMaskMatch}));
            rig.app.hud().close_quick_help();
            rig.pointer_at(300, 300);
            rig.shot("px.wide.app.match.cursor_map", wide_masks({&kWMaskMatch}));
            rig.pointer_at(10, 240);
            rig.shot("px.wide.app.match.cursor_edge", wide_masks({&kWMaskMatch}));
            rig.pointer_at(955, 270);                                 // the east strip at the right edge of the 960 wide picture
            rig.shot("px.wide.app.match.cursor_edge_east", wide_masks({&kWMaskMatch}));
        }
    }
    for (const char* map : {"TINY", "SMALL", "ISLANDS", "GAUNTLET", "TREASURE"}) {
        const std::string name = std::string("px.wide.app.start.") + map;
        if (!wanted(name)) continue;
        WideAppRig rig;
        if (rig.ok && rig.start_match(map)) {
            rig.app.hud().dismiss_match_start_modal();
            rig.shot(name, wide_masks({&kWMaskMatch}));
        }
    }
}

void wide_scenarios(const assets::AssetArchive& arc) {
    hud_wide_scenarios(arc);
    ptr_wide_scenarios();
    camera_wide_scenarios();
    cursor_wide_scenarios();
    click_wide_scenarios();
    world_pixel_wide_scenarios(arc);
    app_pixel_wide_scenarios();
}

}  // namespace

// =====================================================================================================================================================
// The self-check: the fingerprints can fail
// =====================================================================================================================================================

namespace {

/// The parameters of a fixed list of calls (one of every kind); the sensitivity check changes one of them by one at a time
constexpr size_t kParamCount = 47;
using Params = std::array<int32_t, kParamCount>;

const char* const kParamNames[kParamCount] = {
    "hud team", "sprite id", "sprite x", "sprite y", "sprite mirrored", "named sprite name", "named x", "named y", "named mirrored",
    "fill x", "fill y", "fill w", "fill h", "fill r", "fill g", "fill b", "fill a",
    "frame x", "frame y", "frame w", "frame h", "frame r", "frame g", "frame b", "frame a",
    "text string", "text x", "text y", "text size", "text r", "text g", "text b", "text a",
    "short text x", "short text y",
    "clip x", "clip y", "clip w", "clip h",
    "image x", "image y", "image w", "image h", "image byte",
    "second sprite id", "second sprite x", "second sprite y"};

Params base_params() {
    return Params{2,   2721, 10,  20, 0,  0,  30, 40, 0,            // team, sprite, named sprite
                  50,  60,   70,  14, 7,  67, 47, 255,              // fill
                  80,  90,   100, 20, 251, 251, 255, 255,           // frame
                  0,   110,  120, 14, 255, 255, 255, 255,           // text
                  130, 140,                                         // the 4 argument text
                  482, 299,  138, 101,                              // clip
                  480, 35,   2,   2,  17,                           // image
                  2722, 150, 160};                                  // a sprite inside the clip
}

/// Issues the calls with the given parameters (the order is fixed: the sensitivity check also swaps and drops)
void replay(FrameRecorder& r, const Params& p, bool swap_first_two = false, bool drop_last = false) {
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

Measure replayed(const Params& p, bool swap = false, bool drop = false) {
    FrameRecorder r;
    replay(r, p, swap, drop);
    return r.result();
}

void self_check_scenarios() {
    // the recorder: the same calls give the same hash; one parameter of one call changed by one gives another; a swap and a dropped call as well
    const Params base = base_params();
    const Measure reference = replayed(base);
    check(reference.count == 11, "the reference frame has 11 calls (it has " + std::to_string(reference.count) + ")");
    check(replayed(base).hash == reference.hash, "sensitivity: the same calls give the same hash");
    for (size_t i = 0; i < kParamCount; ++i) {
        Params changed = base;
        changed[i] += 1;
        const Measure m = replayed(changed);
        check(m.hash != reference.hash, std::string("sensitivity: the hash changes when '") + kParamNames[i] + "' changes by one");
        check(m.count == reference.count, std::string("sensitivity: '") + kParamNames[i] + "' does not change the number of calls");
    }
    check(replayed(base, true, false).hash != reference.hash, "sensitivity: the hash changes when two draw calls are swapped");
    {
        const Measure dropped = replayed(base, false, true);
        check(dropped.hash != reference.hash && dropped.count + 1 == reference.count, "sensitivity: the hash and the count change when one call is dropped");
    }
    check(FrameRecorder{}.result().count == 0, "sensitivity: an empty frame has no calls");
    {
        FrameRecorder a;                                                               // the 4 argument text is the 12 px text (one operation, as the real renderer treats it)
        FrameRecorder b;
        a.draw_text("t", 1, 2, assets::ColorRGBA{9, 9, 9, 255});
        b.draw_text("t", 1, 2, assets::ColorRGBA{9, 9, 9, 255}, FontSize::Px12);
        check(a.result().hash == b.result().hash, "the 4 argument text and the 12 px text are the same call");
        FrameRecorder c;
        c.draw_text("t", 1, 2, assets::ColorRGBA{9, 9, 9, 255}, FontSize::Px14);
        check(a.result().hash != c.result().hash, "sensitivity: the font size is part of a text call");
    }

    // a pixel hash: one pixel changes it, a masked pixel does not
    {
        Canvas a;
        Canvas b;
        for (size_t i = 0; i < a.px.size(); ++i) a.px[i] = b.px[i] = static_cast<uint8_t>(i * 7u);
        const std::vector<MaskRect> mask = {{10, 10, 20, 20}};
        const Measure ma = canvas_measure(a, mask);
        b.px[(static_cast<size_t>(15) * kW + 15u) * 4u + 1u] ^= 0x40;                       // inside the mask
        check(canvas_measure(b, mask).hash == ma.hash, "a pixel inside a masked rectangle does not change the hash");
        b.px[(static_cast<size_t>(9) * kW + 15u) * 4u + 2u] ^= 0x01;                        // one pixel above the mask
        check(canvas_measure(b, mask).hash != ma.hash, "sensitivity: one bit of one pixel outside the masks changes the hash");
    }

    // a pointer hash: a rate, a camera and a pixel's classification each change it
    {
        const Measure base_edge = edge_sweep(60, 60, 50, {Cam{"c", 400, 400}});
        check(edge_sweep(60, 60, 50, {Cam{"c", 400, 400}}).hash == base_edge.hash, "sensitivity: the same sweep gives the same hash");
        check(edge_sweep(60, 60, 51, {Cam{"c", 400, 400}}).hash != base_edge.hash, "sensitivity: the scroll rate changes the edge hash");
        check(edge_sweep(60, 60, 50, {Cam{"c", 0, 400}}).hash != base_edge.hash, "sensitivity: a camera at the west border changes the edge hash");
        check(edge_sweep(60, 60, 50, {Cam{"c", 0, 0}}).hash != base_edge.hash, "sensitivity: the camera at the corner changes the edge hash");
        const Measure rect = sweep([](Fnv64& h, int32_t x, int32_t y) { h.flag(HUD::in_map_rect(x, y)); });
        const Measure moved = sweep([](Fnv64& h, int32_t x, int32_t y) { h.flag(HUD::in_map_rect(x - 1, y)); });
        check(rect.hash != moved.hash, "sensitivity: a rectangle that moves by one pixel changes the classification hash");
    }
}

/// The wide family's own recorder calls (milestone M3): a part of a sprite and the origin of a window are calls with every parameter in the hash; setting the origin that is set is no call
void self_check_wide_scenarios() {
    const std::array<int32_t, 9> base{7, 100, 200, 50, 60, 3, 4, 1, 9};              // sprite id, x, y, w, h, sx, sy, sw, sh
    auto region_hash = [](const std::array<int32_t, 9>& p) {
        FrameRecorder rec;
        rec.draw_sprite_region(static_cast<uint32_t>(p[0]), p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
        return rec.result();
    };
    const Measure reference = region_hash(base);
    check(reference.count == 1 && region_hash(base).hash == reference.hash, "wide self-check: a region is one call, the same call gives the same hash");
    const char* names[9] = {"sprite id", "x", "y", "w", "h", "sx", "sy", "sw", "sh"};
    for (size_t i = 0; i < base.size(); ++i) {
        std::array<int32_t, 9> changed = base;
        changed[i] += 1;
        check(region_hash(changed).hash != reference.hash, std::string("wide self-check: the hash changes when the region's '") + names[i] + "' changes by one");
    }
    {
        FrameRecorder a, b, c, d;
        a.set_origin(137, 59);
        b.set_origin(137, 60);
        c.set_origin(138, 59);
        d.set_origin(137, 59);
        check(a.result().hash == d.result().hash && a.result().hash != b.result().hash && a.result().hash != c.result().hash && a.calls() == 1, "wide self-check: an origin is one call and both numbers are in the hash");
        FrameRecorder none;
        none.set_origin(0, 0);
        none.set_origin(0, 0);
        check(none.calls() == 0 && none.result().hash == FrameRecorder{}.result().hash, "wide self-check: the origin (0, 0) that is set already is no call (the classic scenes set it around their dialogs)");
        FrameRecorder moved;
        moved.set_origin(5, 6);
        moved.set_origin(5, 6);
        moved.set_origin(0, 0);
        check(moved.calls() == 2, "wide self-check: a change of the origin is a call, the same again is none, back to (0, 0) is one");
    }
    // the pointer and pixel families of the wide picture move with their inputs
    {
        const Measure a = wide_edge_sweep(60, 60, 50, {Cam{"c", 400, 400}});
        check(wide_edge_sweep(60, 60, 50, {Cam{"c", 400, 400}}).hash == a.hash, "wide self-check: the same sweep gives the same hash");
        check(wide_edge_sweep(60, 60, 51, {Cam{"c", 400, 400}}).hash != a.hash, "wide self-check: the scroll rate changes the wide edge hash");
        check(wide_edge_sweep(60, 60, 50, {Cam{"c", 0, 400}}).hash != a.hash, "wide self-check: a camera at the west border changes the wide edge hash");
        const Measure plain = sweep_wide([](Fnv64& h, int32_t x, int32_t y) { h.flag(kWide.view().contains(x, y)); });
        const Measure moved = sweep_wide([](Fnv64& h, int32_t x, int32_t y) { h.flag(kWide.view().contains(x - 1, y)); });
        check(plain.hash != moved.hash && plain.count == kWPixels, "wide self-check: a rectangle that moves by one pixel changes the wide classification hash");
    }
}

}  // namespace

// =====================================================================================================================================================
// The golden numbers, computed at the commit of v0.0.88 (the classic 640 x 480 view). Regenerate ONLY deliberately: see the notes at the top of the file.
// =====================================================================================================================================================

namespace {

// BEGIN GOLDEN TABLE
const Golden kGoldens[] = {
    {"hud.start.modal.p0", 0xc468a06a43ee26b0, 106},
    {"hud.start.modal.p1", 0x98e01a098d348967, 106},
    {"hud.start.modal.p2", 0x2f3e22b7fbf391bb, 106},
    {"hud.start.modal.p3", 0x79672a6a0f5c9a13, 106},
    {"hud.start.modal.1500ms", 0x71900accd71f78d9, 106},
    {"hud.start.modal.closed_by_timer", 0xfaf34595df510ce8, 79},
    {"hud.start.idle.p0", 0xaa58258cae06214c, 80},
    {"hud.start.idle.p1", 0x8dc6824bac18596a, 80},
    {"hud.start.idle.p2", 0x4e9c2b45813aeee2, 80},
    {"hud.start.idle.p3", 0xf3b3b3fa9e40f1f4, 80},
    {"hud.start.idle.dismissed", 0xaa58258cae06214c, 80},
    {"hud.clock.720000", 0xc7ce095780e7460b, 81},
    {"hud.clock.659000", 0x1fea9a9d9aa0282d, 81},
    {"hud.clock.61000", 0x398b96d597d5daca, 80},
    {"hud.clock.0", 0xddf27e8120defc20, 80},
    {"hud.sel.one.worker", 0x2381b6115afb2eae, 85},
    {"hud.sel.one.worker2", 0x2381b6115afb2eae, 85},
    {"hud.sel.one.bomber", 0x2a8dd8d8f7ea0aaa, 88},
    {"hud.sel.one.fire", 0x34837a980c65d4bd, 88},
    {"hud.sel.one.thief", 0x7bae27f834c87a3d, 88},
    {"hud.sel.one.combat", 0xe23e10fde1f6c280, 88},
    {"hud.sel.one.swimmer", 0x1bcfa9564814fa3b, 88},
    {"hud.sel.group", 0x4190e311b8d7b4b2, 85},
    {"hud.sel.group.carrying", 0xdff900c793c5d541, 86},
    {"hud.sel.enemy", 0xfaf34595df510ce8, 79},
    {"hud.sel.hill.own", 0x9033f768ea584d71, 93},
    {"hud.sel.hill.own.noeggs", 0x3afc3aca262c9d99, 81},
    {"hud.sel.hill.own.3eggs", 0xf2783dae07681798, 87},
    {"hud.sel.hill.enemy", 0x291219265740ff75, 82},
    {"hud.sel.one.p2", 0xccb6bb8fe4d8825c, 85},
    {"hud.pedestal.rising.100", 0x8ad26dce211191dd, 84},
    {"hud.pedestal.rising.250", 0x5295943b84e18c43, 83},
    {"hud.pedestal.rising.450", 0xf2ca0144839f8613, 85},
    {"hud.pedestal.rising.700", 0x2a8dd8d8f7ea0aaa, 88},
    {"hud.pedestal.rising.1200", 0x2a8dd8d8f7ea0aaa, 88},
    {"hud.chat.typing.caret_on", 0x4ee0cd89581d50ee, 88},
    {"hud.chat.typing.caret_off", 0xdb9ae1c45e60df34, 87},
    {"hud.chat.typing.long", 0xc3d816005e83e7b5, 81},
    {"hud.chat.scrolled", 0xbd5ed8436043ba9f, 89},
    {"hud.chat.team.buttons", 0x2cfcd57d89d234c4, 90},
    {"hud.chat.team.hover_all", 0x6ff996639ae4f193, 91},
    {"hud.chat.team.hover_team", 0x93c383ee3eb4e72a, 91},
    {"hud.chat.team.pressed_team", 0x0cdaa5486e023efe, 90},
    {"hud.chat.off", 0x35d52cf4800acfa1, 88},
    {"hud.minimap.teams", 0xaa58258cae06214c, 80},
    {"hud.minimap.frame.tl", 0xaa58258cae06214c, 80},
    {"hud.minimap.frame.tr", 0xaa4064c2fca28942, 80},
    {"hud.minimap.frame.bl", 0xbda799e3d9165f3a, 80},
    {"hud.minimap.frame.br", 0x9ae8f3a41ffd0284, 80},
    {"hud.minimap.frame.top", 0xef047400777a4532, 80},
    {"hud.minimap.frame.left", 0xd1dab31d39bd4e81, 80},
    {"hud.minimap.frame.mid", 0x041feb51be38a653, 80},
    {"hud.minimap.frame.beyond", 0x9ae8f3a41ffd0284, 80},
    {"hud.minimap.size.31x31", 0x413daec1d5321929, 80},
    {"hud.minimap.size.40x40", 0xf04fae819de9a865, 80},
    {"hud.minimap.size.14x14", 0xf0463ec0ec6324c2, 80},
    {"hud.minimap.size.80x50", 0xc59c1a49ceff9126, 80},
    {"hud.minimap.no_world", 0x8cc7b48707a931ec, 44},
    {"hud.fog.on", 0xbce789aa3cc6de13, 75},
    {"hud.scores.allied", 0xc4546ba30c7a3075, 83},
    {"hud.scores.allied.local_p2", 0xe019dec48388ea17, 83},
    {"hud.scores.allied.bottom_pair", 0x0423f0be9ddc30bc, 81},
    {"hud.scores.dropped", 0x876befe3137b386f, 78},
    {"hud.scores.absent", 0x5b81f3deae29e617, 77},
    {"hud.scores.two_players", 0x58c11df59bf8000d, 73},
    {"hud.scores.big", 0xe548db61f117131f, 83},
    {"hud.scores.names", 0x8f1b2c621e9951fc, 80},
    {"hud.antheavy", 0x318a4da814fd948b, 306},
    {"hud.quit.open", 0xb223ea350257db2f, 104},
    {"hud.quit.hover_yes", 0x3ab0dbbea79e786c, 104},
    {"hud.quit.pressed_no", 0x954d9d4e786333c1, 104},
    {"hud.options.open", 0x90929d9ea096b478, 304},
    {"hud.options.open.caret_off", 0x939680e725ee1154, 302},
    {"hud.options.changed", 0xd3d8ed43e3379edf, 305},
    {"hud.options.hover_return", 0x6b229c8602572af0, 305},
    {"hud.quickhelp.open", 0x9e37c41fa167033e, 114},
    {"hud.quickhelp.hover_return", 0x1154ad49a219c1fe, 115},
    {"hud.quickhelp.pressed_return", 0x7f1a8cd98154a559, 115},
    {"hud.dialog.invitation", 0x6d8ed2d84c285fee, 104},
    {"hud.dialog.invitation.hover_accept", 0x1b8abd43b3a57051, 104},
    {"hud.dialog.invitation.pressed_accept", 0x36f28bbd520e5db3, 104},
    {"hud.dialog.waiting", 0x2389d97621743512, 103},
    {"hud.dialog.breakconfirm", 0x7b6ac39fa22cf0bf, 104},
    {"hud.dialog.invitation.replaces_team", 0x657eb98e85cf1da3, 109},
    {"hud.topbar.hover_help", 0xbc0af5b7b52fea22, 82},
    {"hud.topbar.pressed_help", 0x7c36bcafa7bab12c, 81},
    {"hud.topbar.hover_options", 0x1f83ee6dea43bdcb, 82},
    {"hud.topbar.pressed_options", 0x090c0e6a8256f48f, 81},
    {"hud.topbar.hover_quit", 0xd2130c4d274dae7f, 82},
    {"hud.topbar.pressed_quit", 0xd3786463033e6b60, 81},
    {"hud.marquee.band", 0x83739c9b7d5c0eb4, 81},
    {"hud.marquee.dot", 0xf865c13ac053b5d6, 81},
    {"hud.marquee.clamped", 0xca5cc055f52bd08b, 81},
    {"hud.marquee.clamped_low", 0xf0c460abb382fdb4, 81},
    {"hud.status.flash.odd", 0xfaf34595df510ce8, 79},
    {"hud.status.flash.even", 0x1df336887628e5c6, 80},
    {"hud.status.long", 0xf9288f3ca266edd1, 80},
    {"screen.setup.before_refresh", 0x42e853aa8cdf3bf2, 135},
    {"screen.setup.default", 0xd4c2c2429fc06d95, 144},
    {"screen.setup.map0", 0x0ee6396e1b92b719, 144},
    {"screen.setup.map1", 0x2fd274d55b8c75b5, 144},
    {"screen.setup.map2", 0x1867fd88bdc42ec6, 144},
    {"screen.setup.map3", 0xeb3ac3dcf744b223, 144},
    {"screen.setup.map4", 0xa8e6b148ca2bd5e5, 144},
    {"screen.setup.map5", 0xd4c2c2429fc06d95, 144},
    {"screen.setup.fog_on", 0xb18ae62e5d7400b2, 144},
    {"screen.setup.hover_start", 0xe7dbf65917ac99f5, 144},
    {"screen.setup.pressed_start", 0x4d61f01332d97a95, 144},
    {"screen.setup.pressed_up", 0x2b78b15b3a13fcb5, 144},
    {"screen.setup.pressed_down", 0x7e2b7c2bcc68a8ed, 144},
    {"screen.setup.hover_leave", 0x7e7207f8142158e3, 144},
    {"screen.setup.locked", 0x7e7207f8142158e3, 144},
    {"screen.setup.team2_alice", 0x72e3b1d43258f4f7, 144},
    {"screen.setup.notice", 0x16053631afaeda43, 143},
    {"screen.room.host", 0x3c134aa968ce9108, 154},
    {"screen.room.host.thumb_good", 0x9d3bc23adab0a6df, 154},
    {"screen.room.host.thumb_ok", 0x3c134aa968ce9108, 154},
    {"screen.room.host.thumb_bad", 0x8f6c60cde6e12f55, 154},
    {"screen.room.host.thumb_unknown", 0x948ec4837938f846, 154},
    {"screen.room.host.four_players", 0xb80130a9e035a803, 158},
    {"screen.room.guest", 0x52301f71cb4cb15a, 146},
    {"screen.room.guest.fog_off", 0xef9c44da2487110a, 145},
    {"screen.room.guest.hover_leave", 0xa31f3e746517183c, 145},
    {"screen.room.guest.connection_lost", 0x0137bf09dbd127d6, 129},
    {"screen.room.guest.server_seat3", 0x46f213e744fbf523, 144},
    {"screen.wide.room.leader_empty", 0xb1d15745992ccdf1, 241},
    {"screen.wide.room.leader_lines", 0xa55867b74f9e2d48, 246},
    {"screen.wide.room.leader_typed_caret", 0x0973048a72fcf279, 248},
    {"screen.wide.room.leader_typed", 0xa34b68aaff170ca9, 247},
    {"screen.wide.room.guest_empty", 0x233a911b5a9ce3ed, 236},
    {"screen.wide.room.guest_typed_caret", 0x135fde3e34ea4d5d, 243},
    {"screen.room.before_refresh", 0x42e853aa8cdf3bf2, 135},
    {"screen.results.2teams.waiting", 0xae029099741ec334, 150},
    {"screen.results.2teams.rows", 0x6f89094de148c8c7, 182},
    {"screen.results.2teams.rows.later", 0xd007fb5670a1dc07, 182},
    {"screen.results.2teams.hover_leave", 0x32657987395aa25d, 182},
    {"screen.results.2teams.pressed_leave", 0xe4d9222bdb8e2d79, 182},
    {"screen.results.4teams.waiting", 0xae029099741ec334, 150},
    {"screen.results.4teams.rows", 0xa7ab576d931964ce, 201},
    {"screen.results.4teams.rows.local3", 0x7f2583f638f44226, 201},
    {"screen.results.4teams.quitter", 0x3042da4471779bf7, 214},
    {"screen.results.3teams.gap", 0xdc787da4ea6328d6, 198},
    {"ptr.edge.60x60.rate50.cam_tl", 0xf3b9a29a8192b052, 307200},
    {"ptr.edge.60x60.rate50.cam_t", 0x192ef46972c41e4a, 307200},
    {"ptr.edge.60x60.rate50.cam_tr", 0xa65f5dc2d2fb8e95, 307200},
    {"ptr.edge.60x60.rate50.cam_l", 0x44d95cd06c149201, 307200},
    {"ptr.edge.60x60.rate50.cam_mid", 0xb5aa479aa4b1eabd, 307200},
    {"ptr.edge.60x60.rate50.cam_r", 0x08f03e660ee22d99, 307200},
    {"ptr.edge.60x60.rate50.cam_bl", 0x493ae82616f8a316, 307200},
    {"ptr.edge.60x60.rate50.cam_b", 0x9b603d85dacfc0e2, 307200},
    {"ptr.edge.60x60.rate50.cam_br", 0x9a03779e9dc092d9, 307200},
    {"ptr.edge.60x60.rate0.all9", 0x28108b8a28bac4cd, 2764800},
    {"ptr.edge.60x60.rate99.all9", 0x70b85ef997a256dd, 2764800},
    {"ptr.edge.31x31.rate50.all9", 0x99d56a4a8c614c05, 2764800},
    {"ptr.edge.40x40.rate25.all9", 0xe86054b36acdae85, 2764800},
    {"ptr.edge.14x14.rate50.all9", 0xb8d7190b9a70d96d, 2764800},
    {"ptr.edge.80x50.rate75.all9", 0x732df304a733c98d, 2764800},
    {"ptr.edge.13x9.rate50.all9", 0xadb1227d3b21e325, 2764800},
    {"ptr.edge.60x60.rate50.outside_ring.mid", 0x388fc3bb96c96325, 36864},
    {"ptr.minimap.rect", 0x0b25e95b5dba108a, 307200},
    {"ptr.class.map_rect", 0x5118168d40602715, 307200},
    {"ptr.class.chat_view", 0x2c1e5ef1c71f181f, 307200},
    {"ptr.minimap.point.60x60", 0x7a4d020bb3b135c5, 307200},
    {"ptr.minimap.point.31x31", 0x70fb2306ad804525, 307200},
    {"ptr.minimap.point.40x40", 0x8a89cb2ff8b99071, 307200},
    {"ptr.minimap.point.14x14", 0x64d8598e20fc521d, 307200},
    {"ptr.minimap.point.80x50", 0x0c9d6d8b3686338d, 307200},
    {"ptr.minimap.scroll.60x60.all9", 0xf6cba4d215c4062d, 2764800},
    {"ptr.minimap.scroll.31x31.all9", 0x07f20a65afef290d, 2764800},
    {"ptr.minimap.scroll.14x14.all9", 0xb6e874de1d5dfbc5, 2764800},
    {"ptr.start_view.origins", 0x4838c15f5514941b, 10357},
    {"view.camera.clamp", 0x8bfb67b5df86cb33, 1445},
    {"view.camera.center_on", 0xd12c987530a61d84, 7178},
    {"view.camera.scroll_pixels", 0xff501334e20df5c2, 2000},
    {"view.camera.screen_to_world.origin", 0xa9f0c776211d3e2d, 307200},
    {"view.camera.world_to_screen.origin", 0x3eb17232d096191c, 4489},
    {"view.camera.screen_to_world.mid", 0x02626afcce7b4a75, 307200},
    {"view.camera.world_to_screen.mid", 0x3eb17232d096191c, 4489},
    {"view.camera.screen_to_world.far", 0xb57ce937e9d86e0d, 307200},
    {"view.camera.world_to_screen.far", 0x3eb17232d096191c, 4489},
    {"ptr.cursor.idle.cam_mid400", 0x6040d676d0686b85, 307200},
    {"ptr.cursor.own_worker.cam_mid400", 0x75cbe31e34598845, 307200},
    {"ptr.cursor.own_group.cam_mid400", 0x75cbe31e34598845, 307200},
    {"ptr.cursor.own_bomber.cam_mid400", 0xf41b03be4b263c45, 307200},
    {"ptr.cursor.own_thief.cam_mid400", 0x768356841f8c0445, 307200},
    {"ptr.cursor.own_combat.cam_mid400", 0x75cbe31e34598845, 307200},
    {"ptr.cursor.bomber_latched.cam_mid400", 0x8fa1a6f79882ad95, 307200},
    {"ptr.cursor.worker_move_latched.cam_mid400", 0x75cbe31e34598845, 307200},
    {"ptr.cursor.enemy_inspected.cam_mid400", 0x6040d676d0686b85, 307200},
    {"ptr.cursor.hill_own.cam_mid400", 0x6040d676d0686b85, 307200},
    {"ptr.cursor.hill_enemy.cam_mid400", 0x6040d676d0686b85, 307200},
    {"ptr.cursor.fog_idle.cam_mid400", 0x0855934140435385, 307200},
    {"ptr.cursor.fog_own_worker.cam_mid400", 0xe5b3fc8eb7ac9045, 307200},
    {"ptr.cursor.options_open.cam_mid400", 0x156ed4086987e325, 307200},
    {"ptr.cursor.band_dragging.cam_mid400", 0x156ed4086987e325, 307200},
    {"ptr.cursor.button_captured.cam_mid400", 0xce34ac41c08e0a85, 307200},
    {"ptr.cursor.idle.cam_tl", 0x65353ee3cf7ff411, 307200},
    {"ptr.cursor.idle.cam_t", 0xeb2f8aad825491e5, 307200},
    {"ptr.cursor.idle.cam_tr", 0x2fc636444d3fc3e5, 307200},
    {"ptr.cursor.idle.cam_l", 0x811930464ec06c45, 307200},
    {"ptr.cursor.idle.cam_mid", 0x5b89762dd00f7635, 307200},
    {"ptr.cursor.idle.cam_r", 0xcc8ed382e5663a45, 307200},
    {"ptr.cursor.idle.cam_bl", 0x82f3de948bb990a5, 307200},
    {"ptr.cursor.idle.cam_b", 0xb051a60fe68ad485, 307200},
    {"ptr.cursor.idle.cam_br", 0xb72d2f0a94d178e5, 307200},
    {"ptr.cursor.own_worker.cam_tl", 0xb466e978b0b5d681, 307200},
    {"ptr.cursor.own_worker.cam_t", 0x70f5b611f99c4d85, 307200},
    {"ptr.cursor.own_worker.cam_tr", 0xae6ee6a13fc06125, 307200},
    {"ptr.cursor.own_worker.cam_l", 0xf42cd6ca26c00985, 307200},
    {"ptr.cursor.own_worker.cam_mid", 0x94d40074049a40a5, 307200},
    {"ptr.cursor.own_worker.cam_r", 0x4b3783dfd7e6d785, 307200},
    {"ptr.cursor.own_worker.cam_bl", 0xf607851863b92de5, 307200},
    {"ptr.cursor.own_worker.cam_b", 0x8c72fa0482876b45, 307200},
    {"ptr.cursor.own_worker.cam_br", 0x35d5df6787521625, 307200},
    {"ptr.click.idle", 0x25e9631b11c72826, 41520},
    {"ptr.click.own_worker", 0x87ac9df1afb78f91, 55278},
    {"ptr.click.own_bomber", 0x84bb4fa3ddf53841, 55632},
    {"ptr.click.own_group", 0xe749c6285810c707, 55278},
    {"ptr.click.hill_own", 0xdea7ba8677946edf, 42006},
    {"ptr.click.hill_enemy", 0xa96821ebf1d04023, 41520},
    {"ptr.click.hill_enemy.ally_break", 0xe6ec16981d803e7d, 41874},
    {"ptr.click.hill_enemy.ally_offer", 0xd061cb77a8cdc43e, 41769},
    {"ptr.click.own_worker.right_button", 0x69a53e523e1501f4, 54291},
    {"ptr.click.own_bomber.right_button", 0x6396ebd7277a76e1, 54291},
    {"ptr.click.dialog.quit", 0x34aa0b29422c725a, 19644},
    {"ptr.click.dialog.quickhelp", 0x469e152ae536aee9, 19542},
    {"ptr.click.dialog.options", 0x953dcab625cd97a3, 25938},
    {"ptr.click.dialog.start_modal", 0x749e3efaba777625, 19200},
    {"ptr.click.dialog.invitation", 0xb9a52bf986228688, 19812},
    {"ptr.click.dialog.waiting", 0x56aa60a19a74acde, 19506},
    {"ptr.click.dialog.breakconfirm", 0xb95b9ad2953a85e4, 19644},
    {"ptr.screen.setup.local", 0x5c569fa1d2ea11d1, 20832},
    {"ptr.screen.setup.room_host", 0x858be58b30149cb6, 20832},
    {"ptr.screen.setup.room_guest", 0x48104980ac71541a, 19695},
    {"ptr.screen.results.waiting", 0x0d445389d7deca25, 19200},
    {"ptr.screen.results.rows", 0x215c4766e9f43c0a, 19695},
    {"px.world.MEDIUM.cam_tl", 0xfe286b53a530b787, 307200},
    {"px.world.MEDIUM.cam_t", 0x0f99ee9d6bd0c995, 307200},
    {"px.world.MEDIUM.cam_tr", 0x56b872ced0415aac, 307200},
    {"px.world.MEDIUM.cam_l", 0xd816170f3fa31b2e, 307200},
    {"px.world.MEDIUM.cam_mid", 0x6091d0ded2e718e8, 307200},
    {"px.world.MEDIUM.cam_r", 0x90b4a69baf901a2a, 307200},
    {"px.world.MEDIUM.cam_bl", 0x69746ce56092c13d, 307200},
    {"px.world.MEDIUM.cam_b", 0x815d61b64e050ce4, 307200},
    {"px.world.MEDIUM.cam_br", 0x9e40e11b2b5a6186, 307200},
    {"px.world.TINY.cam_tl", 0xbb382443938e689c, 307200},
    {"px.world.TINY.cam_t", 0x7b3c2a84a9d0c12d, 307200},
    {"px.world.TINY.cam_tr", 0x3cae5c2eb8fa9403, 307200},
    {"px.world.TINY.cam_l", 0xb604a9335535e7cd, 307200},
    {"px.world.TINY.cam_mid", 0x1aa74bff656f404e, 307200},
    {"px.world.TINY.cam_r", 0x8cccf1922c3a51d2, 307200},
    {"px.world.TINY.cam_bl", 0x21fbe3989f32daa2, 307200},
    {"px.world.TINY.cam_b", 0x3c734bf5ba16f868, 307200},
    {"px.world.TINY.cam_br", 0x07696b06f903f602, 307200},
    {"px.world.GAUNTLET.cam_tl", 0x844e4036876a8749, 307200},
    {"px.world.GAUNTLET.cam_mid", 0x9bb7cab3d7bfde0a, 307200},
    {"px.world.ISLANDS.cam_tl", 0xde4650ebc03b9d4a, 307200},
    {"px.world.ISLANDS.cam_mid", 0x8b9052d2a8f7fe17, 307200},
    {"px.world.SMALL.cam_tl", 0xca82937de3bbbdb5, 307200},
    {"px.world.SMALL.cam_mid", 0x35f7cd96d48a2f51, 307200},
    {"px.world.TREASURE.cam_tl", 0x50923948b5d19619, 307200},
    {"px.world.TREASURE.cam_mid", 0x2765945e6cba34e7, 307200},
    {"px.world.MEDIUM.origin_1_1", 0xbea317edc6ac0898, 307200},
    {"px.world.MEDIUM.origin_32_64", 0xd5fde999a640df4e, 307200},
    {"px.world.MEDIUM.origin_17_49", 0x265b449a1ea1fe71, 307200},
    {"px.world.MEDIUM.origin_1477_1479", 0x83195aeec913dc75, 307200},
    {"px.world.MEDIUM.origin_733_1", 0xa0360a8a2eba817f, 307200},
    {"px.world.MEDIUM.selected", 0x24d1c6b333ffe89f, 307200},
    {"px.world.MEDIUM.selected_hp", 0x0cc19ed7e1ebe2eb, 307200},
    {"px.world.MEDIUM.hill_selected", 0x43240c98f09c3a36, 307200},
    {"px.world.MEDIUM.crowd", 0x581384e8758e49ec, 307200},
    {"px.world.MEDIUM.fog", 0xe028297bde386974, 307200},
    {"px.world.MEDIUM.fog.corner_tl", 0x14005e3a5876b7d8, 307200},
    {"px.world.MEDIUM.click_marker", 0x14c3a2f828db7240, 307200},
    {"px.cursor.normal", 0xc8d42c5e91a8baf9, 307200},
    {"px.cursor.select", 0x5532c4ca983dceed, 307200},
    {"px.cursor.n", 0x0fb8ca1f83770200, 307200},
    {"px.cursor.ne", 0xebca3c1c95d05372, 307200},
    {"px.cursor.e", 0x01e6a6ca0ec90f90, 307200},
    {"px.cursor.se", 0xbe546741889273c9, 307200},
    {"px.cursor.s", 0x472431c6f70f149d, 307200},
    {"px.cursor.sw", 0xb902630ad3916a6c, 307200},
    {"px.cursor.w", 0x69a84740954105d3, 307200},
    {"px.cursor.nw", 0x05b9ca230fec312e, 307200},
    {"px.app.loading.start", 0x2a225b383fef7593, 307200},
    {"px.app.loading.tick12", 0xb2148702c4ad6bef, 307200},
    {"px.app.loading.tick25", 0x8875b5a2b0827923, 307200},
    {"px.app.loading.cursor", 0xe8897a816f1dd05a, 307200},
    {"px.app.setup.before_refresh", 0x4fcb4343bae50ef3, 307200},
    {"px.app.setup.refreshed", 0x5a8f68f4ab4a40de, 307200},
    {"px.app.setup.hover_start", 0x2b68054ae9dea39b, 307200},
    {"px.app.setup.cursor", 0x1498efbca958ffbc, 307200},
    {"px.app.quickhelp.rest", 0x8b011e0fb7cc3a9b, 307200},
    {"px.app.quickhelp.hover_start", 0x4d5a60c68a1aa0e7, 307200},
    {"px.app.quickhelp.pressed_start", 0x9938fed9d1959abf, 307200},
    {"px.app.quickhelp.cursor", 0xa8d009d12219564d, 307200},
    {"px.app.match.modal", 0x635a5ce25c7d86c6, 307200},
    {"px.app.match.start_view", 0x673ef86b26cd9338, 307200},
    {"px.app.match.cam_tl", 0xecbe581afae0ead5, 307200},
    {"px.app.match.cam_t", 0xbcc22ae1a17c9bf2, 307200},
    {"px.app.match.cam_tr", 0x94da8a0e285b54cf, 307200},
    {"px.app.match.cam_l", 0x3175a8b3493709d9, 307200},
    {"px.app.match.cam_mid", 0x289c495d11f8c6dd, 307200},
    {"px.app.match.cam_r", 0x577a083c9ecd70c1, 307200},
    {"px.app.match.cam_bl", 0x56ec07f221fc712e, 307200},
    {"px.app.match.cam_b", 0xbc40d07adb5e608f, 307200},
    {"px.app.match.cam_br", 0xde277b9a60fb497f, 307200},
    {"px.app.match.quit_dialog", 0xc6a4bf5cd49ed1bd, 307200},
    {"px.app.match.cursor_map", 0x06ce12a18534a0e5, 307200},
    {"px.app.match.cursor_edge", 0x6e50f3c14f5e43be, 307200},
    {"ptr.app.edge_gate.cam_mid", 0x1cd7067e7e69e285, 325376},
    {"ptr.app.edge_gate.cam_tl", 0x715bff47f7920212, 325376},
    {"ptr.app.edge_gate.cam_br", 0xb5c037bc6ed1d351, 325376},
    {"ptr.app.edge_gate.left_window", 0x06fca21281eeb67b, 214},
    // the desktop start menu (remake only; new: the classic screens above are unchanged): every panel, a selection, the bots, the fields, a failure, the room's code
    {"screen.menu.main", 0x3590c780bf8d6ce0, 146},
    {"screen.menu.main.second_selected", 0x7ec2b1c259beacce, 146},
    {"screen.menu.main.quit_selected", 0x647ac943406089ce, 146},
    {"screen.menu.main.notice", 0xe9d5e1ba26ad5f25, 156},
    {"screen.menu.single.empty", 0x985b15c8c2fc8ab2, 207},
    {"screen.menu.single.two_bots", 0x3799c5d3d81c9318, 207},
    {"screen.menu.single.own_seat_2", 0xb470829b76dc0289, 207},
    {"screen.menu.join.empty", 0xa3719b0cf65673ec, 148},
    {"screen.menu.join.typed", 0x97292172383f912c, 149},
    {"screen.menu.join.caret_off", 0xffc1932a3464e20d, 148},
    {"screen.menu.join.name_refused", 0x654e9cdadc7be96a, 160},
    {"screen.menu.connecting", 0xa95f45bb91fe2012, 114},
    {"screen.menu.join.error", 0x902ef842457399e8, 158},
    {"screen.menu.host.default", 0xd7650127449616a0, 210},
    {"screen.menu.host.map_and_players", 0x8240ae7480dd50b1, 210},
    {"screen.menu.room.code", 0xb4b079b2a4d20074, 150},
    {"screen.menu.room.full", 0x4b46383209d2e157, 150},
    {"screen.menu.room.longest_code", 0x02e344112faca6ac, 150},
    {"screen.menu.room.fill_medium", 0xc27d15c0f5fe2c7f, 150},
    {"screen.menu.host.fill_medium", 0xd1b4d8175d6af471, 210},
    // ---- the wide match screen (milestone M3, 960 x 540), computed at the commit that introduced it (the classic numbers above did not move); regenerate only deliberately, see the notes at the top.
    //      The review fixes of M3 moved 105 of these deliberately (all of the wide ones that draw the bottom strip or the options window and quick help, none of the classic): the score boxes are
    //      spread over the strip (seven draw calls for the strip instead of three, the boxes at x 213, 468 and 722), and the in-match options window and quick help are drawn over the map view
    //      with the HUD around them (no clay margin). The 121 that are pointer sweeps, cameras and world pixels did not move.
    {"hud.wide.start.modal.p0", 0x7203784daeef60da, 124},
    {"hud.wide.start.modal.p1", 0xc06d1005ad6f8c3f, 124},
    {"hud.wide.start.modal.p2", 0x907ff3981d20ca29, 124},
    {"hud.wide.start.modal.p3", 0x65031440507c3db2, 124},
    {"hud.wide.start.idle.p0", 0x7a90cfaad34abb92, 96},
    {"hud.wide.start.idle.p1", 0xc8021b2f8d847688, 96},
    {"hud.wide.start.idle.p2", 0xa7bc78f8a9249aec, 96},
    {"hud.wide.start.idle.p3", 0xa7d42ca526dbcfd1, 96},
    {"hud.wide.sel.one.worker", 0x2ce6536fbd8d3126, 101},
    {"hud.wide.sel.one.worker2", 0x2ce6536fbd8d3126, 101},
    {"hud.wide.sel.one.bomber", 0xf8c1bd03046f8ce5, 104},
    {"hud.wide.sel.one.fire", 0xcbe5d8e78fc2e932, 104},
    {"hud.wide.sel.one.thief", 0xec0a842d82e966c2, 104},
    {"hud.wide.sel.one.combat", 0xd1a39124e3195fcf, 104},
    {"hud.wide.sel.one.swimmer", 0xcbfd378073c49020, 104},
    {"hud.wide.sel.group", 0x4db4114addd05802, 101},
    {"hud.wide.sel.group.carrying", 0xb83c0a05cbd84876, 102},
    {"hud.wide.sel.enemy", 0x9a8b862299375224, 95},
    {"hud.wide.sel.hill.own", 0xfc8d102c83550430, 109},
    {"hud.wide.sel.hill.own.3eggs", 0x957049efc2b400ad, 103},
    {"hud.wide.sel.hill.enemy", 0xd2db89c618e4c53f, 98},
    {"hud.wide.sel.one.p2", 0x7970dae5c17d8e60, 101},
    {"hud.wide.pedestal.rising.100", 0xc8198e0c882bedf2, 100},
    {"hud.wide.pedestal.rising.450", 0x0b9cf1a9d9814a3b, 101},
    {"hud.wide.pedestal.rising.1200", 0xf8c1bd03046f8ce5, 104},
    {"hud.wide.chat.typing.caret_on", 0x39cd6ff0c76e0d51, 108},
    {"hud.wide.chat.typing.caret_off", 0xc60dfcd83b6c80a4, 107},
    {"hud.wide.chat.typing.long", 0xf36630de6418e2da, 97},
    {"hud.wide.chat.scrolled", 0x7423e8342968df8e, 109},
    {"hud.wide.chat.team.buttons", 0xfc84812fa8dce6c5, 110},
    {"hud.wide.chat.team.hover_all", 0xea6f1f12c30759fb, 111},
    {"hud.wide.chat.team.pressed_team", 0xea6af213d130546f, 110},
    {"hud.wide.chat.off", 0x0a89c59ae1694f5c, 108},
    {"hud.wide.minimap.teams", 0x7a90cfaad34abb92, 96},
    {"hud.wide.minimap.frame.tl", 0x7a90cfaad34abb92, 96},
    {"hud.wide.minimap.frame.tr", 0xda6ffa9e66b9f4b3, 96},
    {"hud.wide.minimap.frame.bl", 0xf7c9614e61d3192d, 96},
    {"hud.wide.minimap.frame.br", 0x5e6a412e286f0e00, 96},
    {"hud.wide.minimap.frame.top", 0x0bd2dbd53bf79197, 96},
    {"hud.wide.minimap.frame.left", 0x9e41bf081a796cff, 96},
    {"hud.wide.minimap.frame.mid", 0x2c4961de7223944a, 96},
    {"hud.wide.minimap.size.31x31", 0xa2109bf3a703a038, 96},
    {"hud.wide.minimap.size.40x40", 0x20920673c1e358b6, 96},
    {"hud.wide.minimap.size.14x14", 0x0b8a9ca561d3046f, 96},
    {"hud.wide.minimap.size.16x16", 0x64b5bef5c67ef882, 96},
    {"hud.wide.minimap.size.12x12", 0x61fa829aaa1607e0, 96},
    {"hud.wide.minimap.size.80x50", 0xbb8fa408b68b29bf, 96},
    {"hud.wide.minimap.size.13x9", 0xbd01aed8225c47c9, 96},
    {"hud.wide.fog.on", 0x38764f47ca34d524, 91},
    {"hud.wide.scores.allied", 0x72dd6a0a8709e622, 99},
    {"hud.wide.scores.allied.bottom_pair", 0xdbf1945e582315cc, 97},
    {"hud.wide.scores.dropped", 0x48e2c3dd817fb422, 94},
    {"hud.wide.scores.roster.3", 0xae0a7d7f95f2dbce, 89},
    {"hud.wide.scores.roster.7", 0x0ba6c360bbf542d0, 92},
    {"hud.wide.scores.roster.11", 0xfa761ddee88e3184, 93},
    {"hud.wide.scores.roster.15", 0x7a90cfaad34abb92, 96},
    {"hud.wide.scores.roster.13", 0xa645b62f069e2d3f, 92},
    {"hud.wide.scores.two_players.black_local", 0x6ab45d5480acddd8, 89},
    {"hud.wide.scores.names", 0xb1c936bda1772418, 96},
    {"hud.wide.quit.open", 0xdf73f3d91f954a2e, 122},
    {"hud.wide.quit.hover_yes", 0x3b9355f8d7566b1f, 122},
    {"hud.wide.quit.pressed_no", 0x9e0fbc84dc5a9ed4, 122},
    {"hud.wide.options.open", 0x79616d4bc655cc19, 365},
    {"hud.wide.options.changed", 0xdbc80485cf1d9692, 366},
    {"hud.wide.options.hover_return", 0xcd2418dca46b9e1f, 366},
    {"hud.wide.quickhelp.open", 0x436c19acc3031fd1, 134},
    {"hud.wide.quickhelp.hover_return", 0xa0bc1f1910c52526, 134},
    {"hud.wide.quickhelp.pressed_return", 0xf31e23101333c461, 134},
    {"hud.wide.dialog.invitation", 0x5125a9f3007cb230, 122},
    {"hud.wide.dialog.invitation.hover_accept", 0xe31fd09629a03e79, 122},
    {"hud.wide.dialog.waiting", 0xefc4d1fa1d476c3e, 121},
    {"hud.wide.dialog.breakconfirm", 0x4fcfda5742ebe96d, 122},
    {"hud.wide.topbar.hover_help", 0x91f7bd0685c650c8, 98},
    {"hud.wide.topbar.pressed_help", 0x1cdbe1a6828ff810, 97},
    {"hud.wide.topbar.hover_options", 0xc2d74e4d7be191f1, 98},
    {"hud.wide.topbar.pressed_options", 0xbf45bd918e9ae3a0, 97},
    {"hud.wide.topbar.hover_quit", 0x9b494bf20f5bce09, 98},
    {"hud.wide.topbar.pressed_quit", 0xe95e0a9f434d8283, 97},
    {"hud.wide.marquee.band", 0xcc991fc944b5fe6a, 97},
    {"hud.wide.marquee.clamped", 0xf593254f26da31f4, 97},
    {"hud.wide.marquee.clamped_low", 0xd0d89bae7b201b93, 97},
    {"hud.wide.status.long", 0xd1168ba03511ba31, 96},
    {"ptr.wide.class.map_view", 0x5a1dd554dd5daacd, 518400},
    {"ptr.wide.class.minimap", 0x412ac51fedbd6f8a, 518400},
    {"ptr.wide.class.chat_view", 0xd33a82eac3bc5ed7, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_tl", 0x6120e14a1274a015, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_t", 0x7845bacfec608f1f, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_tr", 0xd0a24e8428148eef, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_l", 0xcef20df609d2b663, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_mid", 0x89a379e25c82e2cd, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_r", 0x256d9d28c4c52373, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_bl", 0x01c9e9d3197b3293, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_b", 0x76375f7cfd86cacf, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_br", 0x038a2cf579102b41, 518400},
    {"ptr.wide.edge.60x60.rate0.all9", 0xfd5a1ca0dba5320d, 4665600},
    {"ptr.wide.edge.60x60.rate99.all9", 0x1ea2f90a915374fd, 4665600},
    {"ptr.wide.edge.31x31.rate50.all9", 0x8c4e8a6f60a65ee5, 4665600},
    {"ptr.wide.edge.40x40.rate25.all9", 0x47ccf33f6f94b285, 4665600},
    {"ptr.wide.edge.14x14.rate50.all9", 0x823aba4206d1b725, 4665600},
    {"ptr.wide.edge.16x16.rate50.all9", 0x9ef1663cfe94c0cd, 4665600},
    {"ptr.wide.edge.12x12.rate50.all9", 0x823aba4206d1b725, 4665600},
    {"ptr.wide.edge.80x50.rate75.all9", 0xde8fc67ee07f143d, 4665600},
    {"ptr.wide.edge.13x9.rate50.all9", 0x823aba4206d1b725, 4665600},
    {"ptr.wide.edge.60x60.rate50.outside_ring.mid", 0xa8728f11e3933925, 49024},
    {"ptr.wide.minimap.point.60x60", 0x56f0c5e5df3660f1, 518400},
    {"ptr.wide.minimap.point.31x31", 0xa9401273887159b5, 518400},
    {"ptr.wide.minimap.point.40x40", 0x3af9ace6fb459db1, 518400},
    {"ptr.wide.minimap.point.14x14", 0x5bda0cccc11bcc79, 518400},
    {"ptr.wide.minimap.point.16x16", 0x1d4f96be52e849e9, 518400},
    {"ptr.wide.minimap.point.80x50", 0x2f4b533746c5632d, 518400},
    {"ptr.wide.minimap.scroll.60x60.all9", 0x7f6bf7cee9748295, 4665600},
    {"ptr.wide.minimap.scroll.31x31.all9", 0x9c1e5777a849e54d, 4665600},
    {"ptr.wide.minimap.scroll.14x14.all9", 0x823aba4206d1b725, 4665600},
    {"ptr.wide.minimap.scroll.16x16.all9", 0xf7e6e1bc803b0425, 4665600},
    {"ptr.wide.start_view.origins", 0xaff608621198d0b4, 10613},
    {"view.wide.camera.clamp", 0x5a8385469ae3abc4, 2023},
    {"view.wide.camera.center_on", 0x9d5f1216d75aeef5, 7648},
    {"view.wide.camera.scroll_pixels", 0xc60723ba128ef67b, 2800},
    {"view.wide.camera.clamp.corner", 0x0693cd6ef68b2b7c, 2023},
    {"view.wide.camera.center_on.corner", 0xc8338d93cf5a2d35, 7648},
    {"view.wide.camera.scroll_pixels.corner", 0x75825320271573b3, 2800},
    {"view.wide.camera.screen_to_world.origin", 0x77e1d653db94b099, 518400},
    {"view.wide.camera.world_to_screen.origin", 0x0b317b1f5c708ef4, 7622},
    {"view.wide.camera.screen_to_world.mid", 0x3e1c574045e31501, 518400},
    {"view.wide.camera.world_to_screen.mid", 0x0b317b1f5c708ef4, 7622},
    {"view.wide.camera.screen_to_world.far", 0x105c85add91308d9, 518400},
    {"view.wide.camera.world_to_screen.far", 0x0b317b1f5c708ef4, 7622},
    {"view.wide.camera.screen_to_world.small16", 0x000e87d6179e245d, 518400},
    {"view.wide.camera.world_to_screen.small16", 0x0b317b1f5c708ef4, 7622},
    {"ptr.wide.cursor.idle.cam_mid400", 0xd8b30bda747f1305, 518400},
    {"ptr.wide.cursor.own_worker.cam_mid400", 0x431ae1bca23bf625, 518400},
    {"ptr.wide.cursor.own_bomber.cam_mid400", 0xc80b40b060c6aa25, 518400},
    {"ptr.wide.cursor.own_thief.cam_mid400", 0x4e65737ce1987225, 518400},
    {"ptr.wide.cursor.bomber_latched.cam_mid400", 0x34d4c425f3c590cd, 518400},
    {"ptr.wide.cursor.worker_move_latched.cam_mid400", 0x431ae1bca23bf625, 518400},
    {"ptr.wide.cursor.enemy_inspected.cam_mid400", 0xd8b30bda747f1305, 518400},
    {"ptr.wide.cursor.hill_own.cam_mid400", 0xd8b30bda747f1305, 518400},
    {"ptr.wide.cursor.fog_own_worker.cam_mid400", 0x4f69e73cb07afe25, 518400},
    {"ptr.wide.cursor.options_open.cam_mid400", 0x59af61e6ef65d725, 518400},
    {"ptr.wide.cursor.quit_open.cam_mid400", 0x59af61e6ef65d725, 518400},
    {"ptr.wide.cursor.band_dragging.cam_mid400", 0x59af61e6ef65d725, 518400},
    {"ptr.wide.cursor.button_captured.cam_mid400", 0xe7244f0d10442a25, 518400},
    {"ptr.wide.cursor.idle.cam_tl", 0x2f04b20490c0bd41, 518400},
    {"ptr.wide.cursor.idle.cam_t", 0x667ee8dabe9fcb45, 518400},
    {"ptr.wide.cursor.idle.cam_tr", 0x9d84544b5f12b5b5, 518400},
    {"ptr.wide.cursor.idle.cam_l", 0x82009ba1806fc619, 518400},
    {"ptr.wide.cursor.idle.cam_mid", 0x49c68f64404e1065, 518400},
    {"ptr.wide.cursor.idle.cam_r", 0x7320b165fdde6b15, 518400},
    {"ptr.wide.cursor.idle.cam_bl", 0x2308e686e9780175, 518400},
    {"ptr.wide.cursor.idle.cam_b", 0xa5e7f1faf8adbd05, 518400},
    {"ptr.wide.cursor.idle.cam_br", 0x6a30a3f35183dab5, 518400},
    {"ptr.wide.cursor.own_worker.cam_tl", 0xcd1732376a270051, 518400},
    {"ptr.wide.cursor.own_worker.cam_t", 0x1e05154b1615f945, 518400},
    {"ptr.wide.cursor.own_worker.cam_tr", 0x3b0d9a4009a6fe95, 518400},
    {"ptr.wide.cursor.own_worker.cam_l", 0x58d456a9d6624875, 518400},
    {"ptr.wide.cursor.own_worker.cam_mid", 0xd546aeee415dae85, 518400},
    {"ptr.wide.cursor.own_worker.cam_r", 0x10a9f75aa872b3f5, 518400},
    {"ptr.wide.cursor.own_worker.cam_bl", 0x11629c939ffc0a55, 518400},
    {"ptr.wide.cursor.own_worker.cam_b", 0x37e0b46b21df0a25, 518400},
    {"ptr.wide.cursor.own_worker.cam_br", 0x07b9e9e7fc182395, 518400},
    {"ptr.wide.click.idle", 0x141e464550b1cbd4, 58422},
    {"ptr.wide.click.own_worker", 0x70f92850bb3579a2, 89352},
    {"ptr.wide.click.own_bomber", 0xff1af76ddf37cbe9, 89706},
    {"ptr.wide.click.hill_own", 0x0cc7d92020d63abb, 58908},
    {"ptr.wide.click.own_worker.right_button", 0xda185c6d9bc471ee, 88590},
    {"ptr.wide.click.dialog.quit", 0xbc1fb4292cecb626, 32820},
    {"ptr.wide.click.dialog.quickhelp", 0x46d4abb250a3d044, 32754},
    {"ptr.wide.click.dialog.options", 0x3ddb1c261f808228, 39138},
    {"ptr.wide.click.dialog.start_modal", 0x99b29917ecef4c65, 32400},
    {"px.wide.world.MEDIUM.cam_tl", 0x95dc3673d504c028, 518400},
    {"px.wide.world.MEDIUM.cam_t", 0x66635d7a3a1c809e, 518400},
    {"px.wide.world.MEDIUM.cam_tr", 0xe0e0c8292973d82b, 518400},
    {"px.wide.world.MEDIUM.cam_l", 0x29e7dd3ad0a61612, 518400},
    {"px.wide.world.MEDIUM.cam_mid", 0x952eaa77934a2da3, 518400},
    {"px.wide.world.MEDIUM.cam_r", 0x7a120c104dfc74e0, 518400},
    {"px.wide.world.MEDIUM.cam_bl", 0x4e20f8b4530bca8f, 518400},
    {"px.wide.world.MEDIUM.cam_b", 0x3f2726530c6b9456, 518400},
    {"px.wide.world.MEDIUM.cam_br", 0xb52a74ecb39993d3, 518400},
    {"px.wide.world.TINY.cam_tl", 0xbc330da064f78288, 518400},
    {"px.wide.world.TINY.cam_t", 0xdd6c67bcc0006d25, 518400},
    {"px.wide.world.TINY.cam_tr", 0x2171c244a56e67de, 518400},
    {"px.wide.world.TINY.cam_l", 0xadf781277aa85eb1, 518400},
    {"px.wide.world.TINY.cam_mid", 0x9b962f23d4045bc6, 518400},
    {"px.wide.world.TINY.cam_r", 0x2d58241d85081213, 518400},
    {"px.wide.world.TINY.cam_bl", 0x82bc31e0e4b4b1e2, 518400},
    {"px.wide.world.TINY.cam_b", 0xd374c99d013b8958, 518400},
    {"px.wide.world.TINY.cam_br", 0xea93e816fb085f57, 518400},
    {"px.wide.world.GAUNTLET.cam_tl", 0xd699dba2e89e5476, 518400},
    {"px.wide.world.GAUNTLET.cam_mid", 0x903e0ba3fbac81c2, 518400},
    {"px.wide.world.ISLANDS.cam_tl", 0x85654509def1e63c, 518400},
    {"px.wide.world.ISLANDS.cam_mid", 0x15370608dcf36412, 518400},
    {"px.wide.world.SMALL.cam_tl", 0xc9ef1b48aa675ee9, 518400},
    {"px.wide.world.SMALL.cam_mid", 0x5c82fc366812687d, 518400},
    {"px.wide.world.TREASURE.cam_tl", 0x1fbd14a1af47079c, 518400},
    {"px.wide.world.TREASURE.cam_mid", 0x2edba5c122d49a2c, 518400},
    {"px.wide.world.small16", 0xda3d4b1c95658b25, 518400},
    {"px.wide.world.small12", 0xf388b740365b8b25, 518400},
    {"px.wide.app.setup.before_refresh", 0x59b5c5c52c351532, 518400},
    {"px.wide.app.setup.refreshed", 0xfe0a3b2c88226c05, 518400},
    {"px.wide.app.setup.hover_start", 0x946835c689ad3ad4, 518400},
    {"px.wide.app.setup.cursor", 0xba4230e31aad5a3c, 518400},
    {"px.wide.app.setup.room_leader", 0xa06a1b577f93ec06, 518400},
    {"px.wide.app.setup.room_guest", 0xf8ed8630e8ced504, 518400},
    {"px.wide.app.quickhelp.rest", 0x169fb94096dad790, 518400},
    {"px.wide.app.quickhelp.hover_start", 0x2fa88736e7f1b0af, 518400},
    {"px.wide.app.results", 0x8d6ae83a6750f4d7, 518400},
    {"px.wide.app.match.modal", 0x3ddb897f72c9d3d6, 518400},
    {"px.wide.app.match.start_view", 0x595d3def1a48bb8b, 518400},
    {"px.wide.app.match.cam_tl", 0xf19b355a5395c138, 518400},
    {"px.wide.app.match.cam_t", 0x3a8f693eab398c3b, 518400},
    {"px.wide.app.match.cam_tr", 0xdc84467c2cc25cdb, 518400},
    {"px.wide.app.match.cam_l", 0xca013b90f2f1e7ef, 518400},
    {"px.wide.app.match.cam_mid", 0x7d97859e8b29c4d7, 518400},
    {"px.wide.app.match.cam_r", 0xe4b044b9be8bd77d, 518400},
    {"px.wide.app.match.cam_bl", 0x35f62f16f716266a, 518400},
    {"px.wide.app.match.cam_b", 0xbb57cdb5c8e1e1d7, 518400},
    {"px.wide.app.match.cam_br", 0xb82828fd3358d899, 518400},
    {"px.wide.app.match.quit_dialog", 0xcb9a7f2c39318ead, 518400},
    {"px.wide.app.match.options_page", 0xe594c3d9822c0d2f, 518400},
    {"px.wide.app.match.quickhelp_page", 0xb19e23863dbf22c2, 518400},
    {"px.wide.app.match.cursor_map", 0x8c1e413493cf0e22, 518400},
    {"px.wide.app.match.cursor_edge", 0xa62d92275e38ffb9, 518400},
    {"px.wide.app.match.cursor_edge_east", 0xf806b33e9b8d9fd4, 518400},
    {"px.wide.app.start.TINY", 0x8c15e5245f01c313, 518400},
    {"px.wide.app.start.SMALL", 0xfe362f5f666236d8, 518400},
    {"px.wide.app.start.ISLANDS", 0x77dc81d9280a5b04, 518400},
    {"px.wide.app.start.GAUNTLET", 0x79e2b3de5175248b, 518400},
    {"px.wide.app.start.TREASURE", 0xffb65b08c16cda77, 518400},
};
// END GOLDEN TABLE

}  // namespace

namespace {

const Golden* find_golden(const std::string& name) {
    for (const Golden& g : kGoldens) {
        if (name == g.name) return &g;
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    bool list_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--print") g_print = true;
        else if (a == "--list") list_only = true;
        else if (a == "--dump" && i + 1 < argc) { g_dump_name = argv[++i]; g_keep_logs = true; }
        else if (a == "--only" && i + 1 < argc) g_only_prefix = argv[++i];
        else if (a == "--save" && i + 1 < argc) g_save_dir = argv[++i];
        else {
            std::fprintf(stderr, "usage: test_view_fingerprint [--print | --list | --dump NAME | --only PREFIX | --save DIR]\n");
            return 2;
        }
    }
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);                      // nothing is shown or heard
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }

    self_check_scenarios();
    self_check_wide_scenarios();
    hud_scenarios(arc);
    screen_scenarios(arc);
    menu_scenarios(arc);
    edge_scenarios();
    minimap_scenarios();
    camera_scenarios();
    cursor_scenarios();
    click_scenarios();
    screen_click_scenarios();
    world_pixel_scenarios(arc);
    app_pixel_scenarios();
    app_pointer_scenarios();
    wide_scenarios(arc);

    SDL_Quit();
    if (list_only) {
        for (const auto& m : g_measured) std::printf("%s\n", m.first.c_str());
        return 0;
    }
    if (g_print) {
        std::printf("    // %zu fingerprints\n", g_measured.size());
        for (const auto& m : g_measured) {
            std::printf("    {\"%s\", %s, %llu},\n", m.first.c_str(), hex64(m.second.hash).c_str(), static_cast<unsigned long long>(m.second.count));
        }
        return g_failures == 0 ? 0 : 1;
    }
    if (g_only_prefix.empty()) {
        for (const Golden& g : kGoldens) {
            ++g_checks;
            if (std::find(g_seen.begin(), g_seen.end(), g.name) == g_seen.end()) {
                ++g_failures;
                std::fprintf(stderr, "  FAIL [%s]: a golden number without a scenario\n", g.name);
            }
        }
    }
    std::printf("\nview fingerprint: %zu fingerprints, %d checks, %d failures\n", g_measured.size(), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
