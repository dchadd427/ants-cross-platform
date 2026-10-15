// The fixtures of the HUD layout tests (test_hud_layout.cpp holds main(); the cases are in test_hud_layout_panel.cpp, test_hud_layout_screens.cpp and test_hud_layout_overlays.cpp). Inline or in an anonymous namespace.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "ants_app/fps_overlay.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/latency_corner.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/net_overlay.hpp"
#include "ants_app/options_screen.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;

inline int g_checks = 0;
inline int g_failures = 0;
inline uint32_t g_now_ms = 0;

namespace {

inline void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what.c_str());
    }
}

struct SpriteDraw {
    uint32_t id;
    std::string name;
    int32_t x;
    int32_t y;
    uint8_t team{0};          // the HUD team that was set when the sprite was drawn
};

struct FillDraw {
    int32_t x, y, w, h;
    assets::ColorRGBA color;
};

// Records every draw call of the HUD.
class RecordingRenderer : public IRenderer {
public:
    explicit RecordingRenderer(const assets::AssetArchive& archive) : archive_(archive) {}

    void draw_sprite(uint32_t sprite_id, int32_t x, int32_t y, bool) override {
        sprites.push_back({sprite_id, archive_.get_sprite(sprite_id).name, x, y, hud_team});
    }
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool mirrored) override {
        int32_t id = archive_.find_sprite_id(name);
        if (id < 0) id = archive_.find_sprite_id(name + ".bmp");
        if (id >= 0) draw_sprite(static_cast<uint32_t>(id), x, y, mirrored);
    }
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA color) override {
        fills.push_back({x, y, w, h, color});
    }
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA color) override {
        rects.push_back({x, y, w, h, color});
    }
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA colour) override {
        texts.push_back({text, x, y, FontSize::Px12, colour});
    }
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA colour, FontSize size) override {
        texts.push_back({text, x, y, size, colour});
    }
    // the cell height is what the real renderer reports (the original's line distance); the width stays the interface's 6 px per character
    int32_t get_text_height(FontSize size = FontSize::Px12) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t team) override { hud_team = team; }
    void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* rgba) override {
        images.push_back({x, y, w, h, std::vector<uint8_t>(rgba, rgba + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u)});
    }
    void set_clip_rect(int32_t x, int32_t y, int32_t w, int32_t h) override {
        clips.push_back({x, y, w, h});
        clip_on = true;
        // what is drawn while a clip is set is marked: texts[] entries from here on belong to this clip
        clip_first_text.push_back(texts.size());
    }
    void clear_clip_rect() override {
        clip_on = false;
        ++clip_clears;
        clip_last_text.push_back(texts.size());
    }

    // All sprites drawn with this image name (e.g. "dig1.bmp")
    std::vector<SpriteDraw> named(const std::string& name) const {
        std::vector<SpriteDraw> out;
        for (const auto& s : sprites) if (s.name == name) out.push_back(s);
        return out;
    }
    bool has_sprite_at(const std::string& name, int32_t x, int32_t y) const {
        for (const auto& s : sprites) if (s.name == name && s.x == x && s.y == y) return true;
        return false;
    }

    struct Image { int32_t x, y, w, h; std::vector<uint8_t> rgba; };
    std::vector<Image> images;
    struct Text { std::string text; int32_t x; int32_t y; FontSize size; assets::ColorRGBA colour{}; };
    std::vector<SpriteDraw> sprites;
    std::vector<FillDraw> fills;
    std::vector<FillDraw> rects;         // frames (draw_rect)
    std::vector<Text> texts;
    std::vector<std::array<int32_t, 4>> clips;    // set_clip_rect calls (x, y, w, h)
    std::vector<size_t> clip_first_text;          // texts.size() when each clip was set ...
    std::vector<size_t> clip_last_text;           // ... and when it was cleared
    bool clip_on{false};
    int clip_clears{0};
    uint8_t hud_team{0};

private:
    const assets::AssetArchive& archive_;
};

inline uint32_t test_clock() { return g_now_ms; }

// Renders the HUD twice, at t = 0 (pedestal chains start) and 10 s later (everything settled); returns the second frame
inline void render_settled(HUD& hud, const assets::AssetArchive& arc, const sim::WorldState& world, RecordingRenderer& out) {
    ViewportCamera camera;
    hud.set_ticks_function(&test_clock);
    g_now_ms = 0;
    RecordingRenderer first(arc);
    hud.render(first, arc, world, camera);
    g_now_ms = 10000;
    hud.render(out, arc, world, camera);
}

// Table-4 part offset of frame 0 of a glyph animation
struct Off { int dx, dy; };

// Full-screen HUD screens: composites drawn from the original animations, no invented dimming layers.
void click(MapSelectScreen& screen, int32_t x, int32_t y);

// The map view of the original: the rectangle (16, 21) - (458, 461) of the view window (Ants.exe 0x100a32b), 442 x 440; the camera's origin is the screen pixel (16, 21)
static_assert(PLAYFIELD_X == 16 && PLAYFIELD_Y == 21 && PLAYFIELD_W == 442 && PLAYFIELD_H == 440, "the map view is 442 x 440 at (16, 21)");

static_assert(HUD::PLAYFIELD_X == 16 && HUD::PLAYFIELD_Y == 21 && HUD::MAP_RIGHT - HUD::MAP_LEFT == PLAYFIELD_W && HUD::MAP_BOTTOM - HUD::MAP_TOP == PLAYFIELD_H,
              "and the HUD's view rectangle is the same one");

// A click of the original's button class on the setup screen: the pointer moves onto the button, the left button goes down and up
inline void click(MapSelectScreen& screen, int32_t x, int32_t y) {
    screen.handle_mouse_motion(x, y);
    screen.handle_mouse_down(x, y, 1);
    screen.handle_mouse_up(x, y, 1);
}

}  // namespace

// The tests, run by main() in test_hud_layout.cpp
void test_clock_digits(const assets::AssetArchive& arc);
void test_score_digits(const assets::AssetArchive& arc);
void test_score_boxes(const assets::AssetArchive& arc);
void test_home_panel(const assets::AssetArchive& arc);
void test_ally_pedestal(const assets::AssetArchive& arc);
void test_rubber_band(const assets::AssetArchive& arc);
void test_screens(const assets::AssetArchive& arc);
void test_room_screen(const assets::AssetArchive& arc);
void test_leader_screen(const assets::AssetArchive& arc);
void test_held_keys_on_original_screens();
void test_button_zones(const assets::AssetArchive& arc);
void test_latency_corner(const assets::AssetArchive& arc);
void test_net_overlay();
void test_more_button_zones(const assets::AssetArchive& arc);
void test_pedestal_chains(const assets::AssetArchive& arc);
void test_pedestal_timeline(const assets::AssetArchive& arc);
void test_minimap(const assets::AssetArchive& arc);
void test_minimap_dots(const assets::AssetArchive& arc);
void test_chat_log(const assets::AssetArchive& arc);
void test_static_shell(const assets::AssetArchive& arc);
void test_cursor_rules(const assets::AssetArchive&);
void test_button_states(const assets::AssetArchive& arc);
void test_text_sizes(const assets::AssetArchive& arc);
void test_results_screen(const assets::AssetArchive& arc);
void test_label_wrap();
void test_countdown_card(const assets::AssetArchive& arc);
void test_alliance_dialog_layout(const assets::AssetArchive& arc);
void test_alliance_answers(const assets::AssetArchive&);
