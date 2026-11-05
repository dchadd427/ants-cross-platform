// The fixtures of the Yard's tests (test_yard.cpp holds main(); the groups are in test_yard_model.cpp and test_yard_app.cpp). Inline or in an anonymous namespace.
#pragma once

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/yard_puppet.hpp"
#include "ants_app/yard_sink.hpp"
#include "ants_assets/lvl_core.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_yard/yard_types.hpp"

namespace yardtest {

using namespace ants;
using namespace ants::app;
using namespace zoomtest;

inline int g_checks = 0;
inline int g_failures = 0;
inline const char* g_group = "";

inline void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        static const int cap = std::getenv("ANTS_TEST_FAILURES") != nullptr ? std::atoi(std::getenv("ANTS_TEST_FAILURES")) : 60;
        if (g_failures <= cap) std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

inline void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

inline std::string text_of(const LayoutRect& r) { return "(" + std::to_string(r.x) + ", " + std::to_string(r.y) + ", " + std::to_string(r.w) + " x " + std::to_string(r.h) + ")"; }

inline void check_rect(const LayoutRect& got, const LayoutRect& want, const std::string& what) { check(got == want, what + ": " + text_of(got) + " should be " + text_of(want)); }

// =====================================================================================================================================================
// Records: ants of a batch, from the clips of the movement tables
// =====================================================================================================================================================

using sim::movement::ActionClip;
using sim::movement::MotionClip;

inline MotionClip idle_clip_of(int dir) { return sim::movement::idle_clip(0, static_cast<uint8_t>(dir), false); }
inline MotionClip walk_clip_of(int dir) { return sim::movement::walk_clip(0, 0, static_cast<uint8_t>(dir), false); }
inline MotionClip action_clip_of(ActionClip kind, int dir) { return sim::movement::action_clip(kind, 0, static_cast<uint8_t>(dir), false); }

/// One ant of a batch on a clip, at a frame of it (the milliseconds left are the frame's own)
inline yard::AntRecord make_ant(uint16_t id, uint8_t color, int px, int py, const MotionClip& clip, unsigned frame = 0) {
    yard::AntRecord r;
    r.id = id;
    r.color = color;
    r.px = static_cast<uint16_t>(px);
    r.py = static_cast<uint16_t>(py);
    r.clip = clip.chd_index;
    r.mirrored = clip.mirrored;
    r.frame = static_cast<uint8_t>(frame);
    r.left_ms = clip.duration(static_cast<uint16_t>(frame));
    r.next_clip = clip.chd_index;
    return r;
}

inline const sim::AntSnapshot* find_ant(const sim::WorldState& world, uint32_t unit_id) {
    for (const sim::AntSnapshot& a : world.ants) {
        if (a.id == unit_id) return &a;
    }
    return nullptr;
}

// =====================================================================================================================================================
// A renderer that records where everything is drawn
// =====================================================================================================================================================

/// Every call with its place (the sprite calls without a size): what the HUD draws and where. `kind`: s sprite, n named sprite, r sprite region, f fill, b frame, t text, i image.
class BoxRecorder : public IRenderer {
public:
    struct Call {
        char kind;
        int32_t x, y, w, h;
        std::string text;
    };
    void draw_sprite(uint32_t, int32_t x, int32_t y, bool) override { calls.push_back({'s', x, y, 0, 0, ""}); }
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool) override { calls.push_back({'n', x, y, 0, 0, name}); }
    void draw_sprite_region(uint32_t, int32_t x, int32_t y, int32_t w, int32_t h, int32_t, int32_t, int32_t, int32_t) override { calls.push_back({'r', x, y, w, h, ""}); }
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA) override { calls.push_back({'f', x, y, w, h, ""}); }
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA) override { calls.push_back({'b', x, y, w, h, ""}); }
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA) override { calls.push_back({'t', x, y, 0, 0, text}); }
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA, FontSize) override { calls.push_back({'t', x, y, 0, 0, text}); }
    void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t*) override { calls.push_back({'i', x, y, w, h, ""}); }
    void set_hud_team(uint8_t) override {}

    size_t count(char kind) const {
        size_t n = 0;
        for (const Call& c : calls) n += c.kind == kind ? 1u : 0u;
        return n;
    }
    std::vector<Call> calls;
};

// =====================================================================================================================================================
// The HUD over a Yard-sized world: an engine that never ticks, the puppet that rewrites it, the sink that takes the orders
// =====================================================================================================================================================

inline uint32_t fixed_clock() { return 123456u; }

struct YardHud {
    explicit YardHud(int32_t w = 960, int32_t h = 540, uint32_t tiles_w = 128, uint32_t tiles_h = 96) : layout(ScreenLayout::map_only(w, h)), width(tiles_w), height(tiles_h) {
        engine.init_test_world(tiles_w, tiles_h, 1, 600000);
        puppet = std::make_unique<YardPuppet>(engine);
        sink = std::make_unique<YardSink>(*puppet);
        hud.set_ticks_function(&fixed_clock);
        hud.set_layout(layout);
        hud.init(0, false);
        hud.options().chat = false;
        hud.set_command_sink(sink.get());
        hud.set_sim_query(&engine);
        camera.set_view(layout.view());
        camera.centre_small_maps = true;
        look(0, 0);
    }

    void look(int32_t x, int32_t y) {
        camera.x = static_cast<float>(x);
        camera.y = static_cast<float>(y);
        camera.world_x = x;
        camera.world_y = y;
        camera.clamp_to_bounds(width, height);
    }
    /// The batch is the world now (what the selection points at keeps its slot)
    size_t apply(const std::vector<yard::AntRecord>& records) {
        std::vector<uint32_t> held = hud.get_selected_ant_ids();
        if (hud.get_selected_ant_id() != 0) held.push_back(hud.get_selected_ant_id());
        return puppet->apply(records, held);
    }
    std::pair<int32_t, int32_t> screen_of(int32_t wx, int32_t wy) const {
        int32_t sx = 0;
        int32_t sy = 0;
        camera.world_to_screen(wx, wy, sx, sy);
        return {sx, sy};
    }
    void press(int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT) { hud.handle_mouse_down(x, y, button, engine, camera); }
    void release(int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT) { hud.handle_mouse_up(x, y, button, engine, camera); }
    void move(int32_t x, int32_t y) { hud.handle_mouse_motion(x, y, engine, camera); }
    void click(int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT) {
        move(x, y);
        press(x, y, button);
        release(x, y, button);
    }
    /// A drag box with the left button from one point to another
    void drag(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
        move(x0, y0);
        press(x0, y0);
        move((x0 + x1) / 2, (y0 + y1) / 2);
        move(x1, y1);
        release(x1, y1);
    }
    bool key(int32_t sym, uint16_t mod = 0) { return hud.handle_key_down(sym, engine, camera, mod, false); }
    const sim::WorldState& world() { return engine.get_world_state(); }

    ScreenLayout layout;
    uint32_t width;
    uint32_t height;
    sim::SimulationEngine engine;
    std::unique_ptr<YardPuppet> puppet;
    std::unique_ptr<YardSink> sink;
    HUD hud;
    ViewportCamera camera;
};

// =====================================================================================================================================================
// The application as the Yard: a headless game over a Yard-sized map
// =====================================================================================================================================================

/// The renderer's output as it is now, RGBA: the canvas times the window's whole scale (SDL scales the logical canvas into the window; at the scale 1 it is the canvas itself)
inline Picture read_output(Application& app) {
    Picture p;
    int w = 0;
    int h = 0;
    SDL_GetRendererOutputSize(app.renderer().get_sdl_renderer(), &w, &h);
    p.w = w;
    p.h = h;
    p.px.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
    if (SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, p.px.data(), w * 4) != 0) std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
    return p;
}

/// The map of the Yard's size, 128 x 96 tiles of plain ground (an Extended map: no start marker and no hill), as a file of the test's own: the file is written once and never read as a map of
/// the library
inline std::string yard_map_file() {
    static const std::string path = [] {
        const std::filesystem::path file = std::filesystem::temp_directory_path() / "ants_yard_test_128x96.lvl";
        const assets::LevelLimits limits = assets::LevelLimits::for_extended_maps();
        const assets::LevelData level = assets::blank_extended_level(96, 128, assets::lvl_core::tile_names(), limits);
        const assets::LevelWriteResult written = assets::write_level(level, limits);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (written.ok) out.write(reinterpret_cast<const char*>(written.bytes.data()), static_cast<std::streamsize>(written.bytes.size()));
        return written.ok && out ? file.string() : std::string();
    }();
    return path;
}

struct YardRig {
    /// A game that starts as the Yard (ApplicationConfig::yard) in a canvas of w x h, on the map of `map_file` (the Yard-sized one when empty), with `settings` as its settings file (none: in memory)
    explicit YardRig(int32_t w = 960, int32_t h = 540, const std::string& settings = std::string(), const std::string& map_file = std::string()) {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg = base_config(Aspect::Wide16x9);
        cfg.default_map_path = map_file.empty() ? yard_map_file() : map_file;
        cfg.yard = true;
        cfg.has_window_size = true;
        cfg.window_w = w;
        cfg.window_h = h;
        cfg.settings_path = settings;
        cfg.screenshot_path = "never_written.png";                      // (a headless game stops after ten frames, but not while it waits for a screenshot that never comes)
        cfg.screenshot_frames = 1 << 30;
        ok = app.init(cfg);
        if (!ok) return;
        app.renderer().pin_animation_clock(5000);
        app.hud().set_ticks_function(&fixed_clock);
        app.handle_window_event(window_event(SDL_WINDOWEVENT_LEAVE));      // the pointer is outside: no game cursor and no edge scroll, until a test brings it in
    }

    static SDL_WindowEvent window_event(uint8_t what) {
        SDL_WindowEvent we{};
        we.type = SDL_WINDOWEVENT;
        we.event = what;
        return we;
    }
    void frames(int n, float dt = 0.05f) {
        for (int i = 0; i < n; ++i) app.run_frame_with_delta(dt);
    }
    void motion(int32_t x, int32_t y) {
        SDL_MouseMotionEvent m{};
        m.type = SDL_MOUSEMOTION;
        m.x = x;
        m.y = y;
        app.handle_mouse_motion(m);
    }
    void button(bool down, uint8_t which, int32_t x, int32_t y) { app.handle_mouse_button(button_event(which, down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP, x, y)); }
    void click(int32_t x, int32_t y, uint8_t which = SDL_BUTTON_LEFT) {
        motion(x, y);
        button(true, which, x, y);
        button(false, which, x, y);
    }
    void drag(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
        motion(x0, y0);
        button(true, SDL_BUTTON_LEFT, x0, y0);
        motion((x0 + x1) / 2, (y0 + y1) / 2);
        motion(x1, y1);
        button(false, SDL_BUTTON_LEFT, x1, y1);
    }
    void key(SDL_Keycode sym, uint16_t mod = 0) {
        SDL_KeyboardEvent k{};
        k.type = SDL_KEYDOWN;
        k.state = SDL_PRESSED;
        k.keysym.sym = sym;
        k.keysym.mod = mod;
        app.handle_key_down(k);
    }
    std::pair<int32_t, int32_t> screen_of(int32_t wx, int32_t wy) {
        int32_t sx = 0;
        int32_t sy = 0;
        app.renderer().camera().world_to_screen(wx, wy, sx, sy);
        return {sx, sy};
    }
    Picture frame() {
        app.render_frame();
        return read_output(app);
    }
    size_t apply(const std::vector<yard::AntRecord>& records, uint32_t tick = 1) {
        yard::Batch batch;
        batch.tick = tick;
        batch.ants = records;
        return app.yard_apply(batch);
    }

    Application app;
    bool ok{false};
};

}  // namespace yardtest
