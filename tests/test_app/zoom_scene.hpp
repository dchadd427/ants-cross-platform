#pragma once

// The scenes that the zoom's test programs share (milestone M4 of the widescreen work): a real renderer over a hidden window with a map and a world of ants, a HUD over an engine world, an
// application that starts in its match, and the little oracles that tell what a pixel of the view should show at a zoom. test_zoom_view.cpp checks behaviour against independent
// computations; test_zoom_fingerprint.cpp pins the pictures and the pointer as golden numbers.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/view_zoom.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

namespace zoomtest {

using namespace ants;
using namespace ants::app;

inline std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

inline uint32_t lcg(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
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


// =====================================================================================================================================================
// Renderer: pictures
// =====================================================================================================================================================

struct Picture {
    int32_t w{0};
    int32_t h{0};
    std::vector<uint8_t> px;                                            // RGBA
    const uint8_t* at(int32_t x, int32_t y) const { return &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u]; }
};

/// The idle clip of every ant type (ag, ab, af, at, ac, as + "st" + direction digit + "01")
inline int32_t idle_clip(const assets::AssetArchive& arc, int type, int dir_digit) {
    static const char kLetters[6] = {'g', 'b', 'f', 't', 'c', 's'};
    const std::string name = std::string("a") + kLetters[type % 6] + "st" + std::to_string(dir_digit) + "01";
    return arc.find_animation_id(name);
}

/// Ants of every type, colour and facing spread over the world window (x0, y0, w, h) and 80 px beyond its edges, so that sprites straddle every border
inline void populate(sim::WorldState& world, const assets::AssetArchive& arc, int32_t x0, int32_t y0, int32_t w, int32_t h, int count, uint32_t seed) {
    static const int kDirs[5] = {3, 7, 2, 8, 9};
    const int32_t map_w = static_cast<int32_t>(world.width) * 32;
    const int32_t map_h = static_cast<int32_t>(world.height) * 32;
    for (int i = 0; i < count; ++i) {
        const int type = i % 6;
        const int dir = kDirs[(i / 6) % 5];
        const bool mirrored = (dir == 2 || dir == 8 || dir == 9) && (i % 3 == 0);
        const int32_t px = std::clamp(x0 - 80 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(w + 160)), 4, map_w - 4);
        const int32_t py = std::clamp(y0 - 80 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(h + 160)), 4, map_h - 4);
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

/// Effects and score bubbles around the edges of the world window
inline void add_effects(sim::WorldState& world, int32_t x0, int32_t y0, int32_t w, int32_t h) {
    const struct { const char* name; uint32_t duration; } kinds[4] = {{"bombex", 680}, {"sputter", 830}, {"dsplash", 460}, {"battle", 270}};
    const int32_t at[8][2] = {{-10, 200}, {w - 20, 150}, {200, -10}, {250, h - 30}, {30, 30}, {w - 40, 40}, {60, h - 50}, {w - 70, h - 60}};
    for (int i = 0; i < 8; ++i) {
        sim::VisualEffect e;
        e.anim_name = kinds[i % 4].name;
        e.px = std::max(0, x0 + at[i][0]);
        e.py = std::max(0, y0 + at[i][1]);
        e.elapsed_ms = 100u + 60u * static_cast<uint32_t>(i % 3);
        e.duration_ms = kinds[i % 4].duration;
        e.frame = static_cast<uint16_t>(e.elapsed_ms / 50u);
        e.total_frames = static_cast<uint16_t>(e.duration_ms / 50u);
        world.effects.push_back(e);
    }
    sim::ScoreBubble gain;
    gain.x = std::max(0, x0 + 120);
    gain.y = std::max(0, y0 + 120);
    gain.amount = 150;
    gain.elapsed_ms = 100;
    sim::ScoreBubble loss = gain;
    loss.x = std::max(0, x0 - 6);
    loss.y = std::max(0, y0 + 300);
    loss.amount = -35;
    world.score_bubbles.push_back(gain);
    world.score_bubbles.push_back(loss);
}

inline void reveal_some(sim::WorldState& world) {
    world.fog_of_war_enabled = true;
    world.fog_revealed.assign(static_cast<size_t>(world.width) * world.height, 0);
    uint32_t seed = 6u;
    for (uint32_t y = 0; y < world.height; ++y) {
        for (uint32_t x = 0; x < world.width; ++x) {
            const int32_t dx = static_cast<int32_t>(x) - 24;
            const int32_t dy = static_cast<int32_t>(y) - 22;
            const int32_t ex = static_cast<int32_t>(x) - 30;
            const int32_t ey = static_cast<int32_t>(y) - 29;
            const bool open = dx * dx + dy * dy < 30 || ex * ex + ey * ey < 12 || lcg(seed) % 11u == 0;
            world.fog_revealed[static_cast<size_t>(y) * world.width + x] = open ? 1 : 0;
        }
    }
}

/// A real renderer over a hidden window of the canvas's size (the software renderer, the dummy video driver) with a map loaded, a pinned animation clock and a world of its own
struct PixelRig {
    PixelRig(const assets::AssetArchive& archive, const std::string& map, int32_t canvas_w, int32_t canvas_h, uint32_t synthetic_tiles = 0) : arc(archive), cw(canvas_w), ch(canvas_h) {
        const QuietStdout quiet;
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        SDL_Init(SDL_INIT_VIDEO);
        win = SDL_CreateWindow("zoom-view", 0, 0, cw, ch, SDL_WINDOW_HIDDEN);
        if (win == nullptr || !renderer.init(win, archive)) return;
        renderer.set_canvas_size(cw, ch);
        layout = ScreenLayout::with_size(cw, ch);
        renderer.set_layout(layout);
        renderer.pin_animation_clock(1500);
        renderer.set_hud_team(0);
        if (synthetic_tiles > 0) {
            engine.init_test_world(synthetic_tiles, synthetic_tiles, 1, 600000);
            tiles_w = tiles_h = synthetic_tiles;
        } else {
            if (!level.load_from_file(maps_dir() + map + ".LVL")) return;
            engine.init(level, 1337);
            renderer.set_level(level);
            tiles_w = level.width();
            tiles_h = level.height();
        }
        world = engine.get_world_state();
        world.ants.clear();
        ok = true;
    }
    ~PixelRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    PixelRig(const PixelRig&) = delete;
    PixelRig& operator=(const PixelRig&) = delete;

    /// Another map in the same renderer (the offscreen target of a zoom is reused)
    bool switch_map(const std::string& map) {
        assets::LevelData next;
        if (!next.load_from_file(maps_dir() + map + ".LVL")) return false;
        level = next;
        engine.init(level, 1337);
        renderer.set_level(level);
        tiles_w = level.width();
        tiles_h = level.height();
        world = engine.get_world_state();
        world.ants.clear();
        return true;
    }
    ViewportCamera& cam() { return renderer.camera(); }
    void look(double x, double y, float zoom_level = 1.0f) {
        cam().zoom = zoom_level;
        cam().set_origin(x, y, tiles_w, tiles_h);
    }
    /// One frame of the world pass (the HUD is not drawn: the canvas outside the view stays black)
    Picture shoot(int32_t selected_id = -1, const std::vector<uint32_t>& selected_ids = {}, int32_t base_team = -1, bool hp = false) {
        renderer.set_show_hp(hp);
        renderer.begin_frame();
        renderer.render_world(world, engine.grid(), selected_id, selected_ids, false, false, -1, -1, base_team, 0.0f);
        return read();
    }
    Picture read() {
        Picture p;
        p.w = cw;
        p.h = ch;
        p.px.assign(static_cast<size_t>(cw) * static_cast<size_t>(ch) * 4u, 0);
        if (SDL_RenderReadPixels(renderer.get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, p.px.data(), cw * 4) != 0) std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        return p;
    }

    const assets::AssetArchive& arc;
    int32_t cw;
    int32_t ch;
    SDL_Window* win{nullptr};
    assets::LevelData level;
    sim::SimulationEngine engine;
    Renderer renderer;
    ScreenLayout layout;
    sim::WorldState world;
    uint32_t tiles_w{0};
    uint32_t tiles_h{0};
    bool ok{false};
};

inline constexpr int32_t kWideW = 960;
inline constexpr int32_t kWideH = 540;
inline constexpr int32_t kClassicW = 640;
inline constexpr int32_t kClassicH = 480;

struct Shape {
    const char* name;
    int32_t cw;
    int32_t ch;
};
inline const Shape kShapes[2] = {{"classic", kClassicW, kClassicH}, {"wide", kWideW, kWideH}};

/// The canvas of a picture whose view is `view_w` x `view_h`: the view of ScreenLayout::with_size is (442 + W - 640) x (440 + H - 480)
inline Shape shape_for_view(int32_t view_w, int32_t view_h) { return Shape{"big", 640 + (view_w - 442), 480 + (view_h - 440)}; }

/// The number of pixels of the view rectangle that differ between two pictures by more than `tolerance` in a channel (the comparison is inside the view only)
inline int differ(const Picture& a, const Picture& b, const LayoutRect& view, int tolerance = 0) {
    int n = 0;
    for (int32_t y = view.y; y < view.bottom(); ++y) {
        for (int32_t x = view.x; x < view.right(); ++x) {
            const uint8_t* p = a.at(x, y);
            const uint8_t* q = b.at(x, y);
            if (std::abs(p[0] - q[0]) > tolerance || std::abs(p[1] - q[1]) > tolerance || std::abs(p[2] - q[2]) > tolerance) ++n;
        }
    }
    return n;
}

/// floor division of half pixels by half pixels per screen pixel: the screen offset (in whole pixels) of a world offset given in half pixels
inline int64_t fdiv_half(int64_t half_pixels, int64_t per_pixel) {
    // a world offset of `half_pixels` half pixels is `half_pixels / 2` world pixels = (half_pixels / 2) * zoom screen pixels = half_pixels / per_pixel  (per_pixel = 2 / zoom)
    return half_pixels >= 0 ? half_pixels / per_pixel : -((-half_pixels + per_pixel - 1) / per_pixel);
}

/// How many pixels outside the view are not black
inline int outside_not_black(const Picture& p, const LayoutRect& view) {
    int n = 0;
    for (int32_t y = 0; y < p.h; ++y) {
        for (int32_t x = 0; x < p.w; ++x) {
            if (view.contains(x, y)) continue;
            const uint8_t* q = p.at(x, y);
            if (q[0] != 0 || q[1] != 0 || q[2] != 0) ++n;
        }
    }
    return n;
}

/// The nearest 2 x 2 enlargement of the direct picture: what the view should show at the zoom 2 from the origin whose whole part the direct picture has; `half` when the origin of
/// the zoom 2 lies half a world pixel further (every screen column and row then shows the world pixel one half-step on)
inline Picture enlarge2(const Picture& direct, const LayoutRect& view, bool half_x, bool half_y) {
    Picture out;
    out.w = direct.w;
    out.h = direct.h;
    out.px.assign(direct.px.size(), 0);
    for (int32_t y = view.y; y < view.bottom(); ++y) {
        for (int32_t x = view.x; x < view.right(); ++x) {
            const int32_t sx = view.x + ((x - view.x) + (half_x ? 1 : 0)) / 2;
            const int32_t sy = view.y + ((y - view.y) + (half_y ? 1 : 0)) / 2;
            std::memcpy(&out.px[(static_cast<size_t>(y) * static_cast<size_t>(out.w) + static_cast<size_t>(x)) * 4u], direct.at(sx, sy), 4);
        }
    }
    return out;
}

/// The 2 x 2 average of a direct picture that is twice as large in both directions: what the view should show at the zoom 0.5. The direct picture's view is `big_view`.
inline Picture average2(const Picture& direct, const LayoutRect& big_view, int32_t out_w, int32_t out_h, const LayoutRect& view) {
    Picture out;
    out.w = out_w;
    out.h = out_h;
    out.px.assign(static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * 4u, 0);
    for (int32_t y = view.y; y < view.bottom(); ++y) {
        for (int32_t x = view.x; x < view.right(); ++x) {
            for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int32_t j = 0; j < 2; ++j) {
                    for (int32_t i = 0; i < 2; ++i) sum += direct.at(big_view.x + 2 * (x - view.x) + i, big_view.y + 2 * (y - view.y) + j)[c];
                }
                out.px[(static_cast<size_t>(y) * static_cast<size_t>(out_w) + static_cast<size_t>(x)) * 4u + static_cast<size_t>(c)] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

/// half world pixels per screen pixel: 4 at 0.5, 2 at 1, 1 at 2
inline int hpp_of(float z) { return z == 0.5f ? 4 : z == 1.0f ? 2 : 1; }
inline int64_t floor_div(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
/// the world pixel under the screen offset `offset` (from the view's corner), the origin in half pixels
inline int32_t world_under(int64_t origin2, float z, int32_t offset) { return static_cast<int32_t>(floor_div(origin2 + static_cast<int64_t>(offset) * hpp_of(z), 2)); }
/// the world coordinate of the edge of the screen pixel at `offset`, rounded up
inline int32_t edge_under(int64_t origin2, float z, int32_t offset) { return static_cast<int32_t>(floor_div(origin2 + static_cast<int64_t>(offset) * hpp_of(z) + 1, 2)); }
inline int64_t origin2_of(double v) { return static_cast<int64_t>(std::llround(v * 2.0)); }

/// Takes the commands of the HUD and does not carry them out
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

/// A recording renderer for the HUD's drawing: only the rectangles that are frames (draw_rect) are kept
class RectRenderer : public IRenderer {
public:
    struct Rect {
        int32_t x, y, w, h;
        ants::assets::ColorRGBA colour;
    };
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t, int32_t, int32_t, int32_t, ants::assets::ColorRGBA) override {}
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA colour) override { frames.push_back(Rect{x, y, w, h, colour}); }
    void draw_text(const std::string&, int32_t, int32_t, ants::assets::ColorRGBA) override {}
    void set_hud_team(uint8_t) override {}
    std::vector<Rect> frames;
};

/// A HUD over an engine world of 60 x 60 tiles (a hand-made world with a few ants), in the classic or the wide picture
struct HudRig {
    HudRig(const assets::AssetArchive& archive, bool wide_picture, uint32_t map_tiles = 60) : arc(archive), wide(wide_picture), tiles(map_tiles), layout(wide_picture ? ScreenLayout::with_size(960, 540) : ScreenLayout::classic()) {
        sim.init_test_world(map_tiles, map_tiles, 1, 600000);
        hud.init(0);
        hud.set_layout(layout);
        hud.set_sim_query(&sim);
        hud.set_command_sink(&sink);
        hud.set_on_spawn_click_marker([this](int32_t x, int32_t y) { markers.emplace_back(x, y); });
        camera.set_view(layout.view());
        camera.centre_small_maps = wide;
        using sim::AntType;
        using sim::TileCoord;
        mine = sim.spawn_unit(0, AntType::Worker, TileCoord{28, 30});
        foe = sim.spawn_unit(1, AntType::Worker, TileCoord{33, 30});
        far_a = sim.spawn_unit(0, AntType::Worker, TileCoord{3, 3});
        far_b = sim.spawn_unit(0, AntType::Worker, TileCoord{55, 55});
        sim.tick();
    }
    const sim::WorldState& world() const { return sim.get_world_state(); }
    void look(double x, double y, float z) {
        camera.zoom = z;
        camera.set_origin(x, y, tiles, tiles);
    }
    const sim::AntSnapshot& ant(uint32_t id) const {
        for (const auto& a : world().ants) {
            if (a.id == id) return a;
        }
        return world().ants.front();
    }
    void click(int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT) {
        hud.handle_mouse_down(x, y, button, sim, camera);
        hud.handle_mouse_up(x, y, button, sim, camera);
    }

    const assets::AssetArchive& arc;
    bool wide;
    uint32_t tiles;
    ScreenLayout layout;
    sim::SimulationEngine sim;
    HUD hud;
    ViewportCamera camera;
    RecordingSink sink;
    std::vector<std::pair<int32_t, int32_t>> markers;
    uint32_t mine{0}, foe{0}, far_a{0}, far_b{0};
};

inline constexpr std::array<float, 3> kZooms = {0.5f, 1.0f, 2.0f};

inline uint32_t g_clock_ms = 100000;
inline SDL_MouseWheelEvent wheel_event(int32_t y, float precise, bool flipped = false, uint32_t advance_ms = 16) {
    g_clock_ms += advance_ms;
    SDL_MouseWheelEvent e{};
    e.type = SDL_MOUSEWHEEL;
    e.timestamp = g_clock_ms;
    e.x = 0;
    e.y = y;
    e.preciseY = precise;
    e.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
    return e;
}
/// One notch (or `notches` of them) rolled away (positive) or toward the user, as a mouse wheel reports it
inline void notch(Application& app, int notches, bool flipped = false) {
    const int sign = notches > 0 ? 1 : -1;
    for (int i = 0; i < std::abs(notches); ++i) app.handle_mouse_wheel(wheel_event(flipped ? -sign : sign, flipped ? -static_cast<float>(sign) : static_cast<float>(sign), flipped));
}
inline SDL_MouseButtonEvent button_event(uint8_t button, uint32_t type, int32_t x, int32_t y) {
    SDL_MouseButtonEvent b{};
    b.type = type;
    b.button = button;
    b.x = x;
    b.y = y;
    b.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    b.clicks = 1;
    return b;
}

inline ApplicationConfig base_config(Aspect aspect) {
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = false;                                  // the game starts in its match
    cfg.lan_port = 0;
    cfg.chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
    cfg.maps_dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
    cfg.midi_path = std::string(ORIGINAL_ASSETS_DIR) + "/INTRO.MID";
    cfg.default_map_path = maps_dir() + "GAUNTLET.LVL";
    cfg.random_seed = 1337;
    cfg.player_name = "Tester";
    cfg.aspect = aspect;
    cfg.aspect_given = true;
    cfg.has_window_size = true;
    cfg.window_w = aspect == Aspect::Wide16x9 ? kWideW : kClassicW;
    cfg.window_h = aspect == Aspect::Wide16x9 ? kWideH : kClassicH;
    cfg.window_width = cfg.window_w;
    cfg.window_height = cfg.window_h;
    return cfg;
}

struct AppRig {
    /// `many_frames`: a headless application stops after ten frames; with this it is told to take a screenshot that never comes instead, so a test can run as many frames as it needs
    explicit AppRig(Aspect aspect = Aspect::Wide16x9, float zoom_level = 1.0f, bool given = false, const std::string& settings = std::string(), bool in_match = true, bool many_frames = false) {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg = base_config(aspect);
        cfg.zoom = zoom_level;
        cfg.zoom_given = given;
        cfg.settings_path = settings;
        cfg.start_in_map_select = !in_match;
        if (many_frames) {
            cfg.screenshot_path = "never_written.png";
            cfg.screenshot_frames = 1 << 30;
        }
        ok = app.init(cfg);
        if (ok && in_match) app.renderer().camera().set_origin(450.0, 500.0, app.sim().grid().width(), app.sim().grid().height());
    }
    int64_t ox2() { return origin2_of(app.renderer().camera().x); }
    int64_t oy2() { return origin2_of(app.renderer().camera().y); }
    /// the world pixel under a screen pixel of the picture, by the oracle
    std::pair<int32_t, int32_t> world_at(int32_t x, int32_t y) {
        const LayoutRect view = app.layout().view();
        const float z = app.zoom();
        return {world_under(ox2(), z, x - view.x), world_under(oy2(), z, y - view.y)};
    }
    Application app;
    bool ok{false};
};


/// The canvas of an application's renderer as it is now (RGBA)
inline Picture read_canvas(Application& app, int32_t w, int32_t h) {
    Picture p;
    p.w = w;
    p.h = h;
    p.px.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
    if (SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, p.px.data(), w * 4) != 0) std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
    return p;
}

/// A match of the application that is started and has TICKED: the start modal dismissed and `ticks` simulation ticks of 50 ms run. BEFORE the first tick an ant has no animation clip, so
/// no sprite: the frame shows the hit point digits of the ants (Ctrl+L) and nothing else of them. (`ants --screenshot` takes its picture in that state; so did the application frames
/// of suites 3.10 and 3.20 until the ants scenes were added.)
inline bool start_ticked_match(Application& app, const std::string& map, int ticks = 6) {
    if (!app.start_game(maps_dir() + map + ".LVL")) return false;
    app.hud().dismiss_match_start_modal();
    for (int i = 0; i < ticks; ++i) app.update_simulation(0.05f);
    return true;
}

/// One frame of the application's own world pass over a copy of its world without the ants (and without their digits), read back: what the picture would be if no ant were drawn
inline Picture world_without_ants(Application& app, int32_t w, int32_t h) {
    sim::WorldState world = app.sim().get_world_state();
    world.ants.clear();
    app.renderer().set_show_hp(false);
    app.renderer().begin_frame();
    app.renderer().render_world(world, app.sim().grid(), -1, {}, false, false, -1, -1, 0, 0.0f);
    return read_canvas(app, w, h);
}

/// ... and with the ants of the world, but also without their digits (so that only the sprites make the difference)
inline Picture world_with_ants(Application& app, int32_t w, int32_t h) {
    const sim::WorldState& world = app.sim().get_world_state();
    app.renderer().set_show_hp(false);
    app.renderer().begin_frame();
    app.renderer().render_world(world, app.sim().grid(), -1, {}, false, false, -1, -1, 0, 0.0f);
    return read_canvas(app, w, h);
}


}  // namespace zoomtest
