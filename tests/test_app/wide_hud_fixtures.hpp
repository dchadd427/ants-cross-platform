// The fixtures of the wide match screen tests (test_wide_hud.cpp holds main(); the cases are in test_wide_hud_frame.cpp, test_wide_hud_hud.cpp and test_wide_hud_view.cpp). Inline or in an anonymous namespace.
#pragma once

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "ants_app/application.hpp"
#include "ants_app/canvas_layout.hpp"
#include "ants_app/edge_scroll.hpp"
#include "ants_app/fps_overlay.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/latency_corner.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/shell_layout.hpp"
#include "ants_app/version.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;

inline int g_checks = 0;
inline int g_failures = 0;
inline const char* g_group = "";
inline uint32_t g_clock = 0;

namespace {

inline void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

inline void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

inline std::string str(const LayoutRect& r) {
    std::ostringstream o;
    o << "(" << r.x << ", " << r.y << ", " << r.w << " x " << r.h << ")";
    return o.str();
}

inline void check_rect(const LayoutRect& got, const LayoutRect& want, const std::string& what) {
    check(got == want, what + ": " + str(got) + " should be " + str(want));
}

inline void ensure_sdl() {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) SDL_Init(SDL_INIT_VIDEO);
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
// A picture of palette indices: what the art composes to
// =====================================================================================================================================================

struct Pic {
    int32_t w{0};
    int32_t h{0};
    std::vector<int16_t> idx;                       // the palette index of every pixel, -1 where nothing was drawn
    Pic(int32_t width, int32_t height) : w(width), h(height), idx(static_cast<size_t>(width) * static_cast<size_t>(height), -1) {}
    int16_t at(int32_t x, int32_t y) const { return idx[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
    void put(int32_t x, int32_t y, int16_t v) {
        if (x >= 0 && y >= 0 && x < w && y < h) idx[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = v;
    }
};

/// The pixels of the rectangle `src` of a sprite stretched to the rectangle `dst` of the picture, nearest neighbour (a source one pixel wide is that column repeated)
inline void blit_span(Pic& pic, const assets::Sprite& sprite, const LayoutRect& dst, const LayoutRect& src) {
    for (int32_t y = 0; y < dst.h; ++y) {
        for (int32_t x = 0; x < dst.w; ++x) {
            const int32_t sx = src.x + x * src.w / dst.w;
            const int32_t sy = src.y + y * src.h / dst.h;
            const uint8_t v = sprite.get_pixel(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy));
            if (v != assets::CHD_COLOR_KEY_INDEX) pic.put(dst.x + x, dst.y + y, v);
        }
    }
}

/// The frame composed from the production model: the pieces of `uishell`, the last stored first, each as the spans of shell_spans
inline Pic compose_from_spans(const assets::AssetArchive& arc, const ScreenLayout& layout) {
    Pic pic(layout.width, layout.height);
    const auto* seq = arc.find_animation("uishell");
    if (seq == nullptr || seq->subitems.empty()) return pic;
    const auto& frames = seq->subitems[0].frames;
    for (size_t k = frames.size(); k-- > 0;) {
        const auto& part = frames[k];
        const assets::Sprite& sprite = arc.get_sprite(part.sprite_index);
        const ShellSpans spans = shell_spans(shell_rule(sprite.name), part.dx, part.dy, static_cast<int32_t>(sprite.width), static_cast<int32_t>(sprite.height), layout);
        for (size_t i = 0; i < spans.count; ++i) blit_span(pic, sprite, spans.span[i].dst, spans.span[i].src);
    }
    return pic;
}

// =====================================================================================================================================================
// 4. The real renderer draws the spans exactly
// =====================================================================================================================================================

/// What the renderer shows of a palette index of a piece: the HUD's color of the local team 0 (TextureCache::compose_palette is the renderer's own rule)
inline std::array<uint8_t, 4> rgb_of(const assets::AssetArchive& arc, const std::string& sprite_name, int16_t index) {
    const auto palette = TextureCache::compose_palette(arc.get_palette(), sprite_name, 0);
    const assets::ColorRGBA c = palette[static_cast<size_t>(index)];
    return {c.r, c.g, c.b, 255};
}

struct RendererRig {
    RendererRig(const assets::AssetArchive& archive, int32_t w, int32_t h) : arc(archive), width(w), height(h) {
        const QuietStdout quiet;
        ensure_sdl();
        win = SDL_CreateWindow("wide-hud", 0, 0, w, h, SDL_WINDOW_HIDDEN);
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

    const assets::AssetArchive& arc;
    int32_t width;
    int32_t height;
    SDL_Window* win{nullptr};
    Renderer renderer;
    bool ok{false};
};

// =====================================================================================================================================================
// 5. The HUD at 960 x 540
// =====================================================================================================================================================

/// Records the HUD's calls in order, with the origin that was set when each was made
class Spy : public IRenderer {
public:
    enum class Kind : uint8_t { Sprite, Region, Named, Fill, Rect, Text, Clip, ClearClip, Origin, Image, Team };
    struct Ev {
        Kind kind{Kind::Sprite};
        uint32_t id{0};                       // the sprite's id (Sprite, Region)
        std::string name;                     // the sprite's name, the text
        int32_t x{0}, y{0}, w{0}, h{0};       // as given
        int32_t sx{0}, sy{0}, sw{0}, sh{0};   // Region
        assets::ColorRGBA color{};
        int32_t ox{0}, oy{0};                 // the origin that was set
        FontSize size{FontSize::Px12};
    };

    explicit Spy(const assets::AssetArchive& archive) : arc_(archive) {}

    void draw_sprite(uint32_t id, int32_t x, int32_t y, bool) override {
        Ev e = base(Kind::Sprite);
        e.id = id;
        e.name = arc_.get_sprite(id).name;
        e.x = x;
        e.y = y;
        events.push_back(e);
    }
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool) override {
        Ev e = base(Kind::Named);
        e.name = name;
        e.x = x;
        e.y = y;
        events.push_back(e);
    }
    void draw_sprite_region(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh) override {
        Ev e = base(Kind::Region);
        e.id = id;
        e.name = arc_.get_sprite(id).name;
        e.x = x; e.y = y; e.w = w; e.h = h;
        e.sx = sx; e.sy = sy; e.sw = sw; e.sh = sh;
        events.push_back(e);
    }
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override {
        Ev e = base(Kind::Fill);
        e.x = x; e.y = y; e.w = w; e.h = h; e.color = c;
        events.push_back(e);
    }
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override {
        Ev e = base(Kind::Rect);
        e.x = x; e.y = y; e.w = w; e.h = h; e.color = c;
        events.push_back(e);
    }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c) override { text(s, x, y, c, FontSize::Px12); }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) override { text(s, x, y, c, size); }
    int32_t get_text_height(FontSize size = FontSize::Px12) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t) override { events.push_back(base(Kind::Team)); }
    void set_clip_rect(int32_t x, int32_t y, int32_t w, int32_t h) override {
        Ev e = base(Kind::Clip);
        e.x = x; e.y = y; e.w = w; e.h = h;
        events.push_back(e);
    }
    void clear_clip_rect() override { events.push_back(base(Kind::ClearClip)); }
    void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t*) override {
        Ev e = base(Kind::Image);
        e.x = x; e.y = y; e.w = w; e.h = h;
        events.push_back(e);
    }
    void set_origin(int32_t x, int32_t y) override {
        Ev e = base(Kind::Origin);
        e.x = x;
        e.y = y;
        events.push_back(e);
        ox_ = x;
        oy_ = y;
    }

    /// A sprite drawn with this image name at this place (as given: the origin is not added)
    bool has_sprite_at(const std::string& name, int32_t x, int32_t y) const {
        for (const Ev& e : events) {
            if (e.kind == Kind::Sprite && e.name == name && e.x == x && e.y == y) return true;
        }
        return false;
    }
    size_t count(Kind k) const {
        size_t n = 0;
        for (const Ev& e : events) n += e.kind == k ? 1 : 0;
        return n;
    }
    std::vector<Ev> of(Kind k) const {
        std::vector<Ev> out;
        for (const Ev& e : events) {
            if (e.kind == k) out.push_back(e);
        }
        return out;
    }

    std::vector<Ev> events;

private:
    Ev base(Kind k) const {
        Ev e;
        e.kind = k;
        e.ox = ox_;
        e.oy = oy_;
        return e;
    }
    void text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) {
        Ev e = base(Kind::Text);
        e.name = s;
        e.x = x;
        e.y = y;
        e.color = c;
        e.size = size;
        events.push_back(e);
    }
    const assets::AssetArchive& arc_;
    int32_t ox_{0}, oy_{0};
};

inline uint32_t clock_fn() { return g_clock; }

/// A HUD over a 60 x 60 engine world, with an injected clock (the pedestals' chains and the caret read it)
struct HudRig {
    explicit HudRig(const assets::AssetArchive& archive, const ScreenLayout& l, uint8_t local = 0, uint32_t map_tiles = 60) : arc(archive), layout(l) {
        g_clock = 0;
        sim.init_test_world(map_tiles, map_tiles, 1, 600000);
        hud.set_ticks_function(&clock_fn);
        hud.init(local);
        hud.set_layout(layout);
        hud.set_sim_query(&sim);
        cam.set_view(layout.view());
        cam.centre_small_maps = !layout.is_classic();
        cam.clamp_to_bounds(map_tiles, map_tiles);
    }
    /// A frame at time `at` (the pedestals' chains start at the first frame: render once at 0 and again later)
    Spy frame(uint32_t at = 10000) {
        if (!first_done) {
            first_done = true;
            Spy first(arc);
            g_clock = 0;
            hud.render(first, arc, world(), cam);
        }
        g_clock = at;
        Spy spy(arc);
        hud.render(spy, arc, world(), cam);
        return spy;
    }
    sim::WorldState world() const { return override_world ? *override_world : sim.get_world_state(); }
    void click(int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT) {
        hud.handle_mouse_down(x, y, button, sim, cam);
        hud.handle_mouse_up(x, y, button, sim, cam);
    }

    const assets::AssetArchive& arc;
    ScreenLayout layout;
    sim::SimulationEngine sim;
    HUD hud;
    ViewportCamera cam;
    bool first_done{false};
    std::optional<sim::WorldState> override_world;
};

}  // namespace

// The tests, run by main() in test_wide_hud.cpp
void test_anchoring(const assets::AssetArchive& arc);
void test_cuts(const assets::AssetArchive& arc);
void test_composition(const assets::AssetArchive& arc);
void test_renderer_draws_spans(const assets::AssetArchive& arc);
void test_hud_draws_the_frame(const assets::AssetArchive& arc);
void test_score_slots(const assets::AssetArchive& arc);
void test_latency_clear_of_scores(const assets::AssetArchive& arc);
void test_dialogs_and_pages(const assets::AssetArchive& arc);
void test_window_controls_through_update(const assets::AssetArchive& arc);
void test_camera_clamps();
void test_edge_strips();
void test_start_view_and_minimap(const assets::AssetArchive& arc);
void test_small_map_pointer(const assets::AssetArchive& arc);
void test_small_map_pointer_rows(const assets::AssetArchive& arc);
void test_small_map_clip(const assets::AssetArchive& arc);
void test_picture_per_screen();
void test_small_levels_in_the_application();
void test_margin_and_pages_in_match();
void test_options_dim(const assets::AssetArchive& arc);
void test_default_and_options();
void test_pointer_edges_in_match();
