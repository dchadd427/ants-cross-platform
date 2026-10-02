// HUD layout tests: the real HUD drives a recording IRenderer, and the emitted draw calls are checked against the
// coordinates and rules of the original (Ants.exe) - no pixels, no SDL video needed.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ants_app/fps_overlay.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/options_screen.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"

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

uint32_t g_now_ms = 0;
uint32_t test_clock() { return g_now_ms; }

// Renders the HUD twice, at t = 0 (pedestal chains start) and 10 s later (everything settled); returns the second frame
void render_settled(HUD& hud, const assets::AssetArchive& arc, const sim::WorldState& world, RecordingRenderer& out) {
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
Off glyph_offset(const assets::AssetArchive& arc, const std::string& anim) {
    const auto* seq = arc.find_animation(anim);
    if (!seq || seq->subitems.empty() || seq->subitems[0].frames.empty()) return {0, 0};
    return {seq->subitems[0].frames[0].dx, seq->subitems[0].frames[0].dy};
}

void run_top_bar(const assets::AssetArchive& arc, uint32_t remaining_ms, int32_t local_score,
                 RecordingRenderer& rr, uint8_t local_player = 0) {
    sim::WorldState world;
    world.match_time_remaining_ms = remaining_ms;
    world.player_scores[local_player] = local_score;
    HUD hud;
    hud.init(local_player);
    ViewportCamera camera;
    hud.render(rr, arc, world, camera);
}

void test_clock_digits(const assets::AssetArchive& arc) {
    std::printf("[hud] match clock uses the digit sprites at the original positions\n");
    struct Case { uint32_t ms; int tens_min; int min; int tens_sec; int sec; };
    static const Case cases[] = {
        { 12u * 60000u, 1, 2, 0, 0 },
        { 9u * 60000u + 5000u, -1, 9, 0, 5 },        // tens of minutes skipped when 0
        { 10u * 60000u + 59000u, 1, 0, 5, 9 },
        { 61000u, -1, 1, 0, 1 },
        { 0u, -1, 0, 0, 0 },
    };
    for (const auto& c : cases) {
        RecordingRenderer rr(arc);
        run_top_bar(arc, c.ms, 0, rr);
        auto expect_digit = [&](int digit, int x, const char* what) {
            const Off o = glyph_offset(arc, "dig" + std::to_string(digit));
            check(rr.has_sprite_at("dig" + std::to_string(digit) + ".bmp", x + o.dx, 6 + o.dy),
                  std::string("clock ") + std::to_string(c.ms) + " ms: " + what + " digit " + std::to_string(digit));
        };
        if (c.tens_min >= 0) expect_digit(c.tens_min, 70, "tens of minutes");
        else {
            // nothing is drawn in the tens-of-minutes slot
            bool any = false;
            for (const auto& s : rr.sprites) if (s.y < 10 && s.x >= 66 && s.x < 78 && s.name.rfind("dig", 0) == 0) any = true;
            check(!any, "tens of minutes slot empty");
        }
        expect_digit(c.min, 80, "minutes");
        const Off colon = glyph_offset(arc, "digc");
        check(rr.has_sprite_at("digc.bmp", 90 + colon.dx, 6 + colon.dy), "clock colon at x=90");
        expect_digit(c.tens_sec, 97, "tens of seconds");
        expect_digit(c.sec, 107, "seconds");
    }
}

void test_score_digits(const assets::AssetArchive& arc) {
    std::printf("[hud] scores use the digit sprites in a right-aligned 6-slot field\n");
    struct Case { int32_t score; };
    static const int32_t scores[] = { 0, 7, 200, 1250, 45000, 999999, 1234567 };
    for (int32_t score : scores) {
        RecordingRenderer rr(arc);
        run_top_bar(arc, 60000, score, rr, 0);
        // local score box (402,4): digits at (401, 6), 9 px slots
        const int32_t v = std::min<int32_t>(score, 999999);
        std::string s = std::to_string(v);
        const int first_slot = 6 - static_cast<int>(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            const std::string name = std::string("dig") + s[i];
            const Off o = glyph_offset(arc, name);
            const int32_t x = 401 + 9 * (first_slot + static_cast<int>(i)) + o.dx;
            check(rr.has_sprite_at(name + ".bmp", x, 6 + o.dy),
                  "score " + std::to_string(score) + " digit " + std::string(1, s[i]) + " at x=" + std::to_string(x));
        }
        // the box is filled with the team colour first
        bool filled = false;
        for (const auto& f : rr.fills) if (f.x == 402 && f.y == 4 && f.w == 54 && f.h == 14) filled = true;
        check(filled, "local score box filled");
    }
}

// The score boxes (Ants.exe FUN_0100dbe2 0x100e1f0 - 0x100e222 for the slots, FUN_01021e36 for the boxes; docs 5.53): the slot follows the team index, absent and dropped teams
// are covered with scorcovr, an allied team's box is half its colour and half its ally's with the two scores added
void test_score_boxes(const assets::AssetArchive& arc) {
    std::printf("[hud] score boxes: slots by team index, scorcovr for absent and dropped teams, allied boxes split with the summed score\n");
    const assets::ColorRGBA colour[4] = {{7, 67, 47, 255}, {119, 0, 0, 255}, {43, 39, 107, 255}, {39, 39, 59, 255}};
    auto fill_at = [](const RecordingRenderer& rr, int32_t x, int32_t y, int32_t w, const assets::ColorRGBA& c) {
        for (const auto& f : rr.fills) {
            if (f.x == x && f.y == y && f.w == w && f.h == 14 && f.color.r == c.r && f.color.g == c.g && f.color.b == c.b) return true;
        }
        return false;
    };
    auto digits_at = [&](const RecordingRenderer& rr, int32_t x, int32_t y, int32_t value) {
        const std::string text = std::to_string(value);
        const int first_slot = 6 - static_cast<int>(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            const std::string name = std::string("dig") + text[i];
            const Off o = glyph_offset(arc, name);
            if (!rr.has_sprite_at(name + ".bmp", x + 9 * (first_slot + static_cast<int>(i)) + o.dx, y + o.dy)) return false;
        }
        return true;
    };
    auto render = [&](HUD& hud, const sim::WorldState& world, RecordingRenderer& rr) {
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
    };
    auto has_text = [](const RecordingRenderer& rr, const std::string& t) {
        for (const auto& x : rr.texts) if (x.text == t) return true;
        return false;
    };

    // four teams, no alliances, local team 0: the top bar's box and the three bottom slots in index order; nothing is covered
    {
        sim::WorldState world;
        world.player_alliances = {255, 255, 255, 255};
        world.player_scores = {10, 20, 30, 40};
        HUD hud;
        hud.init(0);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(fill_at(rr, 402, 4, 54, colour[0]) && digits_at(rr, 401, 6, 10), "local team 0: its box in the top bar with its score");
        check(fill_at(rr, 105, 464, 54, colour[1]) && digits_at(rr, 104, 466, 20), "team 1: bottom slot 0");
        check(fill_at(rr, 254, 464, 54, colour[2]) && digits_at(rr, 253, 466, 30), "team 2: bottom slot 1");
        check(fill_at(rr, 402, 464, 54, colour[3]) && digits_at(rr, 401, 466, 40), "team 3: bottom slot 2");
        check(rr.named("scorcovr.bmp").empty(), "nothing is covered");
    }
    // the local team is 1: the others keep index order (0, 2, 3 in slots 0, 1, 2)
    {
        sim::WorldState world;
        world.player_alliances = {255, 255, 255, 255};
        world.player_scores = {10, 20, 30, 40};
        HUD hud;
        hud.init(1);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(fill_at(rr, 402, 4, 54, colour[1]) && fill_at(rr, 105, 464, 54, colour[0]) && fill_at(rr, 254, 464, 54, colour[2]) && fill_at(rr, 402, 464, 54, colour[3]),
              "local team 1: team 0, 2 and 3 in the bottom slots 0, 1, 2");
    }
    // teams 0 and 2 play: team 1's slot (slot 0) and team 3's (slot 2) are covered, team 2 keeps slot 1 (the slots do not close up)
    {
        sim::WorldState world;
        world.player_alliances = {255, 255, 255, 255};
        world.player_scores = {10, 20, 30, 40};
        HUD hud;
        hud.init(0);
        hud.set_roster_mask(0x05);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(rr.has_sprite_at("scorcovr.bmp", 103, 463), "team 1 is absent: its box (105, 464) is covered at (103, 463) by scorcovr");
        check(fill_at(rr, 254, 464, 54, colour[2]) && digits_at(rr, 253, 466, 30), "team 2 is in slot 1 (254, 464), not in the first slot");
        check(rr.has_sprite_at("scorcovr.bmp", 400, 463), "team 3 is absent: its box (402, 464) is covered at (400, 463)");
        check(!fill_at(rr, 105, 464, 54, colour[2]) && !fill_at(rr, 105, 464, 54, colour[1]), "nothing is filled in the covered slots");
        check(!has_text(rr, "Red:") && !has_text(rr, "Black:") && has_text(rr, "Blue:"), "only the teams that exist have a label");
        const auto cover = arc.find_sprite("scorcovr.bmp");
        check(cover != nullptr && cover->width == 58 && cover->height == 17, "scorcovr is 58 x 17");
    }
    // a team that drops out: its box is covered, its label stays
    {
        sim::WorldState world;
        world.player_alliances = {255, 255, 255, 255};
        world.player_scores = {10, 20, 30, 40};
        world.dropped_mask = 0x02;
        HUD hud;
        hud.init(0);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(rr.has_sprite_at("scorcovr.bmp", 103, 463) && !fill_at(rr, 105, 464, 54, colour[1]), "team 1 has dropped out: its box is covered, its score is gone");
        check(has_text(rr, "Red:"), "its label stays");
        check(fill_at(rr, 254, 464, 54, colour[2]), "the others keep their slots");
    }
    // teams 0 and 1 are allied: each box is half its colour and half its ally's (the right half from left + 26) and shows the two scores added. The engine's
    // player_scores are the sums already (display scores): the box draws its team's entry once. (An earlier box added the ally's entry again: 1500.)
    {
        sim::WorldState world;
        world.player_alliances = {1, 0, 255, 255};
        world.player_scores = {750, 750, 30, 40};
        HUD hud;
        hud.init(0);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(fill_at(rr, 402, 4, 54, colour[0]) && fill_at(rr, 428, 4, 28, colour[1]), "the local box: green, and red from x = 428");
        check(digits_at(rr, 401, 6, 750), "it shows 300 + 450");
        check(fill_at(rr, 105, 464, 54, colour[1]) && fill_at(rr, 131, 464, 28, colour[0]), "the ally's box: red, and green from x = 131");
        check(digits_at(rr, 104, 466, 750), "it shows the same sum");
        check(!fill_at(rr, 254, 464, 28, colour[0]) && !fill_at(rr, 254 + 26, 464, 28, colour[0]), "a team without an ally has a single colour");
        check(!digits_at(rr, 401, 6, 1500) && !digits_at(rr, 104, 466, 1500), "the sum is not added twice");
    }
    // the same with the real engine: two allied teams with 300 and 450 points, the boxes show 750, and a team that plays alone shows its own score
    {
        sim::SimulationEngine sim;
        sim.init_test_world(40, 40, 1, 60000);
        sim.set_player_score(0, 300);
        sim.set_player_score(1, 450);
        sim.set_player_score(2, 30);
        sim.form_alliance(0, 1);
        const sim::WorldState& world = sim.get_world_state();
        HUD hud;
        hud.init(0);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(digits_at(rr, 401, 6, 750), "the engine's allied boxes: the local box shows 300 + 450 = 750");
        check(digits_at(rr, 104, 466, 750), "and the ally's box shows the same 750");
        check(digits_at(rr, 253, 466, 30), "a team that plays alone shows its own 30");
    }
}

// Home anthill panel: egg tray = animation egg<N> (N = min(eggs, 9)) with absolute part coordinates, hatch pedestal only
// while eggs remain, Stop button from butcanu / butcand.
void test_home_panel(const assets::AssetArchive& arc) {
    std::printf("[hud] home anthill panel: egg tray, hatch pedestal and Stop button\n");
    static const uint32_t egg_counts[] = { 0, 1, 2, 5, 9, 12 };
    for (uint32_t eggs : egg_counts) {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        world.player_eggs[0] = eggs;
        HUD hud;
        hud.init(0);
        hud.select_base(0);
        render_settled(hud, arc, world, rr);

        const uint32_t n = std::min<uint32_t>(eggs, 9u);
        if (n > 0) {
            const auto* seq = arc.find_animation("egg" + std::to_string(n));
            check(seq != nullptr, "egg" + std::to_string(n) + " animation exists");
            if (seq) {
                for (const auto& part : seq->subitems[0].frames) {
                    check(rr.has_sprite_at("egg.bmp", part.dx, part.dy),
                          std::to_string(eggs) + " eggs: egg drawn at " + std::to_string(part.dx) + "," + std::to_string(part.dy));
                }
                check(rr.named("egg.bmp").size() == seq->subitems[0].frames.size(),
                      std::to_string(eggs) + " eggs: exactly " + std::to_string(n) + " egg sprites");
            }
            // hatch pedestal buteggu: base (477,157), icon (490,165), label (483,140)
            check(rr.has_sprite_at("butup.bmp", 477, 157), "hatch pedestal base at (477,157)");
            check(rr.has_sprite_at("buthatup.bmp", 490, 165), "hatch icon at (490,165)");
            check(rr.has_sprite_at("labhatch.bmp", 483, 140), "hatch label at (483,140)");
        } else {
            check(rr.named("egg.bmp").empty(), "no eggs: no egg sprites");
            check(rr.named("buthatup.bmp").empty(), "no eggs: no hatch pedestal");
            check(rr.named("labhatch.bmp").empty(), "no eggs: no hatch label");
        }
        // Stop button: label (595,180), button (595,198)
        check(rr.has_sprite_at("labcan.bmp", 595, 180), "Stop label at (595,180)");
        check(rr.has_sprite_at("butcanu.bmp", 595, 198), "Stop button at (595,198)");
    }
}

// Ally pedestal of a selected enemy base: animation butalyu / butalyd at absolute coordinates. Slot 1 of another player's hill holds it only
// with more than two players and while its owner is not allied with the local player (FUN_01027f07 mode 2).
void test_ally_pedestal(const assets::AssetArchive& arc) {
    std::printf("[hud] ally pedestal position\n");
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        world.anthills.resize(3);
        for (size_t i = 0; i < 3; ++i) world.anthills[i].team_id = static_cast<uint8_t>(i);
        HUD hud;
        hud.init(0);
        hud.select_base(1);
        render_settled(hud, arc, world, rr);
        check(rr.has_sprite_at("butup.bmp", 477, 157), "ally pedestal base at (477,157)");
        check(rr.has_sprite_at("butdipu.bmp", 485, 164), "ally icon at (485,164)");
        check(rr.has_sprite_at("labdib.bmp", 477, 142), "ally label at (477,142)");
    }
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        world.anthills.resize(2);                                  // two players: no ally pedestal
        for (size_t i = 0; i < 2; ++i) world.anthills[i].team_id = static_cast<uint8_t>(i);
        HUD hud;
        hud.init(0);
        hud.select_base(1);
        render_settled(hud, arc, world, rr);
        check(rr.named("butdipu.bmp").empty() && rr.named("labdib.bmp").empty(), "two players: no ally pedestal");
    }
    // the players that dropped out do not count (FUN_0100c58a counts the teams whose +0x64 is clear) and a dropped local team has none (0x1028188)
    const auto ally_icon_shown = [&](size_t hills, uint8_t dropped_mask) {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        world.anthills.resize(hills);
        for (size_t i = 0; i < hills; ++i) world.anthills[i].team_id = static_cast<uint8_t>(i);
        world.dropped_mask = dropped_mask;
        HUD hud;
        hud.init(0);
        hud.select_base(1);
        render_settled(hud, arc, world, rr);
        return !rr.named("butdipu.bmp").empty();
    };
    check(ally_icon_shown(4, 0x00), "four live players: the ally pedestal");
    check(ally_icon_shown(4, 0x08), "four hills, one team dropped: three live players, the ally pedestal");
    check(!ally_icon_shown(4, 0x0C), "four hills, two teams dropped: two live players, no ally pedestal");
    check(!ally_icon_shown(3, 0x04), "three hills, one team dropped: two live players, no ally pedestal");
    check(!ally_icon_shown(4, 0x01), "the local team has dropped out itself: no ally pedestal");
}

// The rubber band (FUN_0102653f): a 1 px frame in (255, 0, 0) around the press point and the pointer, the pointer kept 1 px inside the view
// (x in [17, 457], y in [22, 460]), a direction without extent widened by 1 px on both sides, none while a pedestal is latched.
void test_rubber_band(const assets::AssetArchive& arc) {
    std::printf("[hud] rubber band\n");
    sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 1, 60000);
    const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{10, 10});
    sim.tick();
    ViewportCamera cam;
    cam.world_x = 200;
    cam.world_y = 200;
    auto band = [&](HUD& hud, int32_t px, int32_t py, int32_t mx, int32_t my) {
        hud.handle_mouse_down(px, py, SDL_BUTTON_LEFT, sim, cam);
        hud.handle_mouse_motion(mx, my, sim, cam);
        RecordingRenderer rr(arc);
        hud.render(rr, arc, sim.get_world_state(), cam);
        std::vector<FillDraw> red;
        for (const auto& r : rr.rects) if (r.color.r == 255 && r.color.g == 0 && r.color.b == 0) red.push_back(r);
        hud.handle_mouse_up(mx, my, SDL_BUTTON_LEFT, sim, cam);
        return red;
    };
    {
        HUD hud;
        hud.init(0);
        auto r = band(hud, 100, 100, 150, 140);
        check(r.size() == 1 && r[0].x == 100 && r[0].y == 100 && r[0].w == 50 && r[0].h == 40, "the band is the bounding box of the press and the pointer");
        r = band(hud, 150, 140, 100, 100);
        check(r.size() == 1 && r[0].x == 100 && r[0].y == 100 && r[0].w == 50 && r[0].h == 40, "dragging up and to the left gives the same box");
        r = band(hud, 100, 100, 100, 100);
        check(r.size() == 1 && r[0].x == 99 && r[0].y == 99 && r[0].w == 2 && r[0].h == 2, "a stationary press is a 2 x 2 dot");
        r = band(hud, 100, 100, 160, 100);
        check(r.size() == 1 && r[0].x == 100 && r[0].y == 99 && r[0].w == 60 && r[0].h == 2, "a zero height is widened by 1 px on both sides");
        r = band(hud, 300, 300, 500, 500);
        check(r.size() == 1 && r[0].x == 300 && r[0].y == 300 && r[0].w == 157 && r[0].h == 160, "the pointer is kept inside the view: (457, 460)");
        r = band(hud, 300, 300, 0, 0);
        check(r.size() == 1 && r[0].x == 17 && r[0].y == 22 && r[0].w == 283 && r[0].h == 278, "and at the top left: (17, 22)");
        check(r.size() == 1 && r[0].color.a == 255, "opaque");
    }
    {
        HUD hud;
        hud.init(0);
        hud.select_ant(ant);
        sim.tick();
        hud.handle_mouse_down(500, 170, SDL_BUTTON_LEFT, sim, cam);     // the move pedestal: latched, so no band exists
        hud.handle_mouse_up(500, 170, SDL_BUTTON_LEFT, sim, cam);
        check(hud.is_move_latched(), "the move pedestal is latched");
        auto r = band(hud, 100, 100, 150, 140);
        check(r.empty(), "no band while a pedestal is latched");
    }
}

// Full-screen HUD screens: composites drawn from the original animations, no invented dimming layers.
void click(MapSelectScreen& screen, int32_t x, int32_t y);

// The map view of the original: the rectangle (16, 21) - (458, 461) of the view window (Ants.exe 0x100a32b), 442 x 440; the camera's origin is the screen pixel (16, 21)
static_assert(PLAYFIELD_X == 16 && PLAYFIELD_Y == 21 && PLAYFIELD_W == 442 && PLAYFIELD_H == 440, "the map view is 442 x 440 at (16, 21)");
static_assert(HUD::PLAYFIELD_X == 16 && HUD::PLAYFIELD_Y == 21 && HUD::MAP_RIGHT - HUD::MAP_LEFT == PLAYFIELD_W && HUD::MAP_BOTTOM - HUD::MAP_TOP == PLAYFIELD_H,
              "and the HUD's view rectangle is the same one");

void test_screens(const assets::AssetArchive& arc) {
    std::printf("[hud] options, quit, quick help and start screens\n");
    // Options: op_screen composite only (its own dither), no extra dither grid or card fill
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.open_options();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        size_t dither200 = 0;
        for (const auto& s : rr.sprites) if (s.name == "dith200.bmp") ++dither200;
        const auto* op = arc.find_animation("op_screen");
        size_t expect = 0;
        if (op) for (const auto& part : op->subitems[0].frames) if (arc.get_sprite(part.sprite_index).name == "dith200.bmp") ++expect;
        check(dither200 == expect, "options: only the composite's own dith200 tiles (" + std::to_string(dither200) + " of " + std::to_string(expect) + ")");
        bool card_fill = false;
        for (const auto& f : rr.fills) if (f.x == 18 && f.y == 20 && f.w == 442 && f.h == 440) card_fill = true;
        check(!card_fill, "options: no opaque card fill");
    }
    // Quick help: qh_screen composite (qh2 at (267,10), qh1 at (10,9)) + qh_return1, no dim fill
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.open_quick_help();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        check(rr.has_sprite_at("qh2.bmp", 267, 10), "quick help: qh2 at (267,10)");
        check(rr.has_sprite_at("qh1.bmp", 10, 9), "quick help: qh1 at (10,9)");
        check(rr.has_sprite_at("breturn1.bmp", 529, 437), "quick help: return button at (529,437)");
        bool dim = false;
        for (const auto& f : rr.fills) if (f.w == 640 && f.h == 480) dim = true;
        check(!dim, "quick help: no full-screen dim");
    }
    // Quit dialog: origin (100,100), yes (180,260), no (292,260), no dim
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.open_quit_dialog();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        check(rr.has_sprite_at("yes1.bmp", 180, 260), "quit: yes at (180,260)");
        check(rr.has_sprite_at("no1.bmp", 292, 260), "quit: no at (292,260)");
        bool dim = false;
        for (const auto& f : rr.fills) if (f.w == 640 && f.h == 480) dim = true;
        check(!dim, "quit: no full-screen dim");
        // the std_dialg frame starts at the origin: dfram1 part (0,0) => (100,100)
        check(rr.has_sprite_at("dfram1.bmp", 100, 100), "quit: dialog frame at origin (100,100)");
    }
    // Match start modal: origin (100,100), label in (130,110,240,160), portrait anchored at (245,250)
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.start_match_modal();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        check(rr.has_sprite_at("dfram1.bmp", 100, 100), "modal: dialog frame at origin (100,100)");
        bool portrait = false;
        const auto* ant = arc.find_animation("agst301");
        if (ant) for (const auto& part : ant->subitems[0].frames)
            if (rr.has_sprite_at("agst301.bmp", 245 + part.dx, 250 + part.dy)) portrait = true;
        check(portrait, "modal: worker portrait anchored at (245,250)");
    }
}

// Pedestal transition chains: every row of the original's transition table (Ants.exe FUN_01028491, table 0x1002b68)
struct ChainRow { const char* cur; int cur_mode; const char* next; int next_mode; int remove; const char* chain; };
const ChainRow kChainRows[] = {
    {"hidden", 1, "move", 1, 0, "trnbmovu|butmovu"},
    {"hidden", 1, "move", 2, 0, "trnbmovu|butmovu"},
    {"hidden", 1, "bomb", 1, 0, "trnbbomu|butbomu"},
    {"hidden", 1, "bomb", 2, 0, "trnbbomu|butbomu"},
    {"hidden", 1, "thief", 1, 0, "trnbthfu|butthfu"},
    {"hidden", 1, "thief", 2, 0, "trnbthfu|butthfu"},
    {"hidden", 1, "egg", 1, 0, "trnbeggu|buteggu"},
    {"hidden", 1, "egg", 2, 0, "trnbeggu|buteggu"},
    {"move", 1, "hidden", 1, 1, "trnbmovd"},
    {"move", 1, "hidden", 2, 1, "trnbmovd"},
    {"move", 1, "move", 2, 0, "butmov2d|butmovd"},
    {"move", 1, "bomb", 1, 0, "trnamovd|trnabomu|butbomu"},
    {"move", 1, "thief", 1, 0, "trnamovd|trnathfu|butthfu"},
    {"move", 1, "egg", 1, 0, "trnamovd|trnaeggu|buteggu"},
    {"move", 2, "hidden", 1, 1, "trnbmovd"},
    {"move", 2, "hidden", 2, 1, "trnbmovd"},
    {"move", 2, "move", 1, 0, "butmovu"},
    {"move", 2, "bomb", 1, 0, "trnamovd|trnabomu|butbomu"},
    {"move", 2, "thief", 1, 0, "trnamovd|trnathfu|butthfu"},
    {"move", 2, "egg", 1, 0, "trnamovd|trnaeggu|buteggu"},
    {"bomb", 1, "hidden", 1, 1, "trnbbomd"},
    {"bomb", 1, "hidden", 2, 1, "trnbbomd"},
    {"bomb", 1, "move", 1, 0, "trnabomd|trnamovu|butmovu"},
    {"bomb", 1, "bomb", 2, 0, "butbom2d|butbomd"},
    {"bomb", 1, "thief", 1, 0, "trnabomd|trnathfu|butthfu"},
    {"bomb", 1, "egg", 1, 0, "trnabomd|trnaeggu|buteggu"},
    {"bomb", 2, "hidden", 1, 1, "trnbbomd"},
    {"bomb", 2, "hidden", 2, 1, "trnbbomd"},
    {"bomb", 2, "move", 1, 0, "trnabomd|trnamovu|butmovu"},
    {"bomb", 2, "bomb", 1, 0, "butbomu"},
    {"bomb", 2, "thief", 1, 0, "trnabomd|trnathfu|butthfu"},
    {"bomb", 2, "egg", 1, 0, "trnabomd|trnaeggu|buteggu"},
    {"thief", 1, "hidden", 1, 1, "trnbthfd"},
    {"thief", 1, "hidden", 2, 1, "trnbthfd"},
    {"thief", 1, "move", 1, 0, "trnathfd|trnamovu|butmovu"},
    {"thief", 1, "bomb", 1, 0, "trnathfd|trnabomu|butbomu"},
    {"thief", 1, "thief", 2, 0, "butthf2d|butthfd"},
    {"thief", 1, "egg", 1, 0, "trnathfd|trnaeggu|buteggu"},
    {"thief", 2, "hidden", 1, 1, "trnbthfd"},
    {"thief", 2, "hidden", 2, 1, "trnbthfd"},
    {"thief", 2, "move", 1, 0, "trnathfd|trnamovu|butmovu"},
    {"thief", 2, "bomb", 1, 0, "trnathfd|trnabomu|butbomu"},
    {"thief", 2, "thief", 1, 0, "butthfu"},
    {"thief", 2, "egg", 1, 0, "trnathfd|trnaeggu|buteggu"},
    {"egg", 1, "hidden", 1, 1, "trnbeggd"},
    {"egg", 1, "hidden", 2, 1, "trnbeggd"},
    {"egg", 1, "move", 1, 0, "trnaeggd|trnamovu|butmovu"},
    {"egg", 1, "bomb", 1, 0, "trnaeggd|trnabomu|butbomu"},
    {"egg", 1, "thief", 1, 0, "trnaeggd|trnathfu|butthfu"},
    {"egg", 1, "egg", 2, 0, "butegg2d|buteggd"},
    {"egg", 2, "hidden", 1, 1, "trnbeggd"},
    {"egg", 2, "hidden", 2, 1, "trnbeggd"},
    {"egg", 2, "move", 1, 0, "trnaeggd|trnamovu|butmovu"},
    {"egg", 2, "bomb", 1, 0, "trnaeggd|trnabomu|butbomu"},
    {"egg", 2, "thief", 1, 0, "trnaeggd|trnathfu|butthfu"},
    {"egg", 2, "egg", 1, 0, "buteggu"},
};

PedestalKind kind_from_name(const std::string& n) {
    if (n == "move") return PedestalKind::Move;
    if (n == "bomb") return PedestalKind::Bomb;
    if (n == "attack") return PedestalKind::Attack;
    if (n == "fire") return PedestalKind::Fire;
    if (n == "thief") return PedestalKind::Thief;
    if (n == "ally") return PedestalKind::Ally;
    if (n == "swim") return PedestalKind::Swim;
    if (n == "egg") return PedestalKind::Egg;
    return PedestalKind::Hidden;
}

void test_pedestal_chains(const assets::AssetArchive& arc) {
    std::printf("[hud] pedestal transition chains equal the original's table\n");
    for (const auto& row : kChainRows) {
        const PedestalChain c = pedestal_chain(kind_from_name(row.cur), row.cur_mode, kind_from_name(row.next), row.next_mode);
        std::string got;
        for (size_t i = 0; i < c.animations.size(); ++i) got += (i ? "|" : "") + c.animations[i];
        check(got == row.chain, std::string(row.cur) + "," + std::to_string(row.cur_mode) + " -> " + row.next + "," +
                                std::to_string(row.next_mode) + ": " + got + " expected " + row.chain);
        check(c.remove_after_last == (row.remove != 0), std::string(row.cur) + " -> " + row.next + " removal flag");
        for (const auto& name : c.animations) check(arc.find_animation(name) != nullptr, "animation exists: " + name);
    }
    // Every kind has all seven animations
    static const PedestalKind kinds[] = { PedestalKind::Move, PedestalKind::Bomb, PedestalKind::Attack, PedestalKind::Fire,
                                          PedestalKind::Thief, PedestalKind::Ally, PedestalKind::Swim, PedestalKind::Egg };
    for (PedestalKind k : kinds) {
        for (const std::string& name : { pedestal_up_anim(k), pedestal_down_anim(k), pedestal_press_anim(k), pedestal_swap_out_anim(k),
                                         pedestal_swap_in_anim(k), pedestal_sink_anim(k), pedestal_rise_anim(k) }) {
            check(arc.find_animation(name) != nullptr, "pedestal animation exists: " + name);
        }
    }
}

// The hatch pedestal rises with trnbeggu (9 x 60 ms) and then rests on buteggu; deselecting sinks it with trnbeggd
void test_pedestal_timeline(const assets::AssetArchive& arc) {
    std::printf("[hud] pedestal rise and sink play in real time\n");
    sim::WorldState world;
    world.player_eggs[0] = 3;
    HUD hud;
    hud.init(0);
    hud.select_base(0);
    hud.set_ticks_function(&test_clock);
    ViewportCamera camera;
    const auto* rise = arc.find_animation("trnbeggu");
    check(rise != nullptr && rise->subitems.size() == 9, "trnbeggu has 9 frames");
    if (!rise) return;
    for (uint32_t t : { 0u, 59u, 60u, 300u, 539u }) {
        g_now_ms = t;
        RecordingRenderer rr(arc);
        hud.render(rr, arc, world, camera);
        const size_t frame = std::min<size_t>(t / 60, 8);
        for (const auto& part : rise->subitems[frame].frames) {
            check(rr.has_sprite_at(arc.get_sprite(part.sprite_index).name, part.dx, part.dy),
                  "rise frame " + std::to_string(frame) + " at t=" + std::to_string(t));
        }
        check(rr.named("buteggu.bmp").empty() && rr.named("buthatup.bmp").empty(), "resting art not shown while rising");
    }
    g_now_ms = 540;
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, world, camera);
        check(rr.has_sprite_at("buthatup.bmp", 490, 165), "resting hatch pedestal at 540 ms");
    }
    // deselect: the pedestal sinks (trnbeggd, 9 x 60 ms) and then disappears
    hud.select_base(-1);
    g_now_ms = 1000;
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, world, camera);   // sink starts
        const auto* sink = arc.find_animation("trnbeggd");
        check(sink != nullptr, "trnbeggd exists");
        if (sink) for (const auto& part : sink->subitems[0].frames)
            check(rr.has_sprite_at(arc.get_sprite(part.sprite_index).name, part.dx, part.dy), "sink frame 0");
        if (std::getenv("HUD_DEBUG")) {
            for (const auto& sp : rr.sprites) if (sp.x >= 470 && sp.x < 600 && sp.y >= 130 && sp.y < 240) std::fprintf(stderr, "  drawn %s at %d,%d\n", sp.name.c_str(), sp.x, sp.y);
            if (sink) for (const auto& part : sink->subitems[0].frames) std::fprintf(stderr, "  expect %s at %d,%d\n", arc.get_sprite(part.sprite_index).name.c_str(), part.dx, part.dy);
        }
    }
    g_now_ms = 1000 + 540;
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, world, camera);
        check(rr.named("buthatup.bmp").empty() && rr.named("butup.bmp").empty(), "pedestal gone after the sink");
    }
}

// Minimap (Ants.exe FUN_01009596): a 119x91 image at (480,35) with the class speckle colours, the object table and the fog table.
// The dots of the ants and plants (FUN_01009988) and the view frame are drawn over it as rectangles (test_minimap_dots).
void test_minimap(const assets::AssetArchive& arc) {
    std::printf("[hud] minimap image\n");
    const auto& pal = arc.get_palette();
    auto rgb_of = [&](int idx) { return std::array<uint8_t, 3>{ pal[static_cast<size_t>(idx)].r, pal[static_cast<size_t>(idx)].g, pal[static_cast<size_t>(idx)].b }; };
    auto pixel = [&](const RecordingRenderer::Image& im, int x, int y) {
        const uint8_t* p = &im.rgba[(static_cast<size_t>(y) * static_cast<size_t>(im.w) + static_cast<size_t>(x)) * 4u];
        return std::array<uint8_t, 3>{ p[0], p[1], p[2] };
    };

    sim::WorldState world;
    world.width = 60; world.height = 60;
    world.cells.assign(3600, sim::TileCell{});
    // west third: water (class 2), middle: mud (class 3), east third: dirt (class 4); rest gravel (class 0)
    for (uint32_t y = 0; y < 60; ++y) for (uint32_t x = 0; x < 60; ++x) {
        auto& c = world.cells[y * 60 + x];
        if (x < 10) { c.terrain_type = sim::TERRAIN_WATER; c.surface_type = sim::SurfaceType::Water; }
        else if (x >= 20 && x < 30) { c.surface_type = sim::SurfaceType::Mud; c.is_mud = true; }
        else if (x >= 40 && x < 50) { c.surface_type = sim::SurfaceType::Gravel; }
    }
    // an anthill tile of the black colony (animation 245, colour 239), the original's bomb id 129 at (15,40), the remake's own planted bomb (id 100 + team) at (16,40), a
    // rock (animation 293) at (12,20)
    world.cells[30 * 60 + 15].interactive_id = 245;
    world.cells[40 * 60 + 15].interactive_id = 129;
    world.cells[40 * 60 + 16].interactive_id = sim::BOMB_GREEN;
    world.cells[20 * 60 + 12].interactive_id = 293;

    RecordingRenderer rr(arc);
    HUD hud;
    hud.init(0);
    ViewportCamera camera;
    hud.render(rr, arc, world, camera);
    check(rr.images.size() == 1, "minimap drawn as one image");
    if (rr.images.empty()) return;
    const auto& im = rr.images[0];
    check(im.x == 480 && im.y == 35 && im.w == 119 && im.h == 91, "minimap rect (480,35) 119x91");

    auto in_set = [&](std::array<uint8_t, 3> c, std::initializer_list<int> idxs) {
        for (int i : idxs) if (rgb_of(i) == c) return true;
        return false;
    };
    // pixel (x,y) -> cell (x*60/119, y*60/91); sample interior pixels of each band
    bool water_ok = true, gravel_ok = true, mud_ok = true, dirt_ok = true;
    for (int y = 2; y < 30; ++y) {
        for (int x = 2; x < 17; ++x) if (!in_set(pixel(im, x, y), {37})) water_ok = false;          // cells 1..8
        for (int x = 22; x < 38; ++x) if (!in_set(pixel(im, x, y), {251, 201, 249})) gravel_ok = false; // cells 11..19
        for (int x = 42; x < 58; ++x) if (!in_set(pixel(im, x, y), {77})) mud_ok = false;           // cells 21..29
        for (int x = 82; x < 98; ++x) if (!in_set(pixel(im, x, y), {231, 232, 233})) dirt_ok = false; // cells 41..49
    }
    check(water_ok, "water cells use class colour 37");
    check(gravel_ok, "gravel cells use the class-0 speckle colours 251/201/249");
    check(mud_ok, "mud cells use class colour 77");
    check(dirt_ok, "dirt cells use the class-4 speckle colours 231/232/233");

    // the painter gives every pixel of a cell the colour of the object in it: the hill (pixel (30,46) is cell (15,30)) and the rock of tile (12,20) (pixels 24 .. 25 x 31)
    check(pixel(im, 30, 46) == rgb_of(239), "hill drawn with colour 239 at its cell");
    check(pixel(im, 24, 31) == rgb_of(51) && pixel(im, 25, 31) == rgb_of(51), "a rock (animation 293) shows colour 51 on its cell");
    // a bomb shows the terrain: the original's ids 129 .. 132 and the remake's own (100 .. 103) alike (pixels (30,61) and (32,61) are cells (15,40) and (16,40))
    check(in_set(pixel(im, 30, 61), {251, 201, 249}), "the bomb of id 129 is not drawn on the minimap");
    check(in_set(pixel(im, 32, 61), {251, 201, 249}), "a bomb planted by the remake is not drawn on the minimap");
    // no dot per object cell any more: the hill's cell next to it (16,30) shows the terrain
    check(in_set(pixel(im, 32, 46), {251, 201, 249}), "no dot around an object: the next cell shows the terrain");

    // a tile id that the table has no record for has colour 0 (the table is cleared first); the placeholder of a bomb that is being planted (0xa0) is one
    world.cells[5 * 60 + 40].interactive_id = sim::TILE_RESERVED;
    // a fire wall (134, colour 250) and a power-up dropped by the remake (0x8000 | type 4, the pu_comb tile 62, colour 171)
    world.cells[5 * 60 + 45].interactive_id = sim::TILE_FIREWALL;
    world.cells[5 * 60 + 47].interactive_id = static_cast<uint16_t>(0x8000u | 4u);
    world.cells[5 * 60 + 47].is_powerup = true;
    world.cells[5 * 60 + 47].powerup_type = 4;
    RecordingRenderer ro(arc);
    hud.render(ro, arc, world, camera);
    const auto& oim = ro.images[0];
    check(pixel(oim, 80, 8) == rgb_of(0), "a tile id without a record in the colour table shows palette colour 0");
    check(pixel(oim, 90, 8) == rgb_of(250), "a fire wall shows colour 250");
    check(pixel(oim, 94, 8) == rgb_of(171), "a dropped power-up shows the colour of its tile (171)");

    // Fog: unexplored cells use the fog colours by class
    world.fog_of_war_enabled = true;
    world.fog_revealed.assign(3600, 0);
    for (uint32_t y = 0; y < 60; ++y) for (uint32_t x = 20; x < 30; ++x) world.fog_revealed[y * 60 + x] = 1;   // reveal the mud band
    RecordingRenderer rf(arc);
    hud.render(rf, arc, world, camera);
    const auto& fim = rf.images[0];
    check(pixel(fim, 5, 20) == rgb_of(225), "unexplored water shows fog colour 225");
    check(pixel(fim, 30, 20) == rgb_of(244), "unexplored gravel shows fog colour 244");
    check(pixel(fim, 50, 20) == rgb_of(77), "explored mud keeps its colour");
    check(pixel(fim, 90, 20) == rgb_of(236), "unexplored dirt (class 4) shows fog colour 236");
    // in the fog only what gets removed or given out falls back to the terrain (power-ups, food, fire walls); every other object keeps its colour, the hills of the enemy too
    check(pixel(fim, 30, 46) == rgb_of(239), "a hill keeps its colour in the fog");
    check(pixel(fim, 24, 31) == rgb_of(51), "a rock keeps its colour in the fog");
    check(pixel(fim, 80, 8) == rgb_of(0), "an unlisted tile id shows colour 0 in the fog as well");
    check(pixel(fim, 90, 8) == rgb_of(236), "an unexplored fire wall shows the fog colour (of its class, dirt: 236)");
    check(pixel(fim, 94, 8) == rgb_of(236), "an unexplored power-up shows the fog colour");
    // food (animation 253, colour 171) in the fog: hidden while its cell is unexplored, shown once it is explored
    world.cells[15 * 60 + 15].interactive_id = 253;
    world.cells[15 * 60 + 25].interactive_id = 253;                       // in the revealed mud band
    RecordingRenderer rfood(arc);
    hud.render(rfood, arc, world, camera);
    const auto& food = rfood.images[0];
    check(pixel(food, 30, 23) == rgb_of(244), "unexplored food shows the fog colour (pixel (30,23) is cell (15,15))");
    check(pixel(food, 50, 23) == rgb_of(171), "explored food shows its colour 171");
    // a bridge is an object like the rock: it keeps its colour (77) in the fog
    world.cells[15 * 60 + 35].interactive_id = sim::TILE_BRIDGE2;
    RecordingRenderer rbridge(arc);
    hud.render(rbridge, arc, world, camera);
    check(pixel(rbridge.images[0], 70, 23) == rgb_of(77), "a bridge shows colour 77 in the fog");
}

// The dots and the frame (FUN_01009988, FUN_01009899, 0x1009ae2 - 0x1009b6f): drawn over the image in screen coordinates, in list order (plants, then ants).
void test_minimap_dots(const assets::AssetArchive& arc) {
    std::printf("[hud] minimap dots, plants and the view frame\n");
    const auto& pal = arc.get_palette();
    auto colour_of = [&](int idx) { return assets::ColorRGBA{ pal[static_cast<size_t>(idx)].r, pal[static_cast<size_t>(idx)].g, pal[static_cast<size_t>(idx)].b, 255 }; };
    auto same = [](const assets::ColorRGBA& a, const assets::ColorRGBA& b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
    auto find_fill = [&](const RecordingRenderer& rr, int x, int y, int w, int h, int palette_index) {
        for (const auto& f : rr.fills) if (f.x == x && f.y == y && f.w == w && f.h == h && same(f.color, colour_of(palette_index))) return true;
        return false;
    };
    auto world_of = [](uint32_t w, uint32_t h) {
        sim::WorldState world;
        world.width = w; world.height = h;
        world.cells.assign(static_cast<size_t>(w) * h, sim::TileCell{});
        return world;
    };
    auto ant_at = [](uint32_t id, uint8_t team, int32_t px, int32_t py) {
        sim::AntSnapshot a{};
        a.id = id; a.player_id = team; a.hp = 10; a.px = px; a.py = py; a.tile_x = px / 32; a.tile_y = py / 32;
        return a;
    };
    ViewportCamera camera;

    // An ant is a rectangle at its PIXEL position, not snapped to its tile: size max(trunc(32 / scale), 2) = 2 x 2 on a 60 x 60 map, centred (left = x - 1, top = y - 1,
    // right = x + 1, bottom = y + 1). Ant (990, 500): image (61, 23) -> screen (480 + 60, 35 + 22)
    {
        sim::WorldState world = world_of(60, 60);
        world.ants.push_back(ant_at(1, 0, 990, 500));                      // image (990 * 119 / 1920, 500 * 91 / 1920) = (61, 23)
        world.ants.push_back(ant_at(2, 3, 1000, 500));                     // ten world pixels further: still the same image pixel (61, 23)
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        check(find_fill(rr, 540, 57, 2, 2, 47), "a green ant is a 2x2 dot of colour 47 at its pixel position");
        check(find_fill(rr, 540, 57, 2, 2, 239), "the black ant's dot (colour 239) is drawn at the same place, over the green one");
        // the dots come after the image and before the frame
        check(rr.images.size() == 1 && rr.rects.size() == 1, "one image, one frame");
    }
    // a dying ant (no hit points left, or drowning) keeps its dot until it is removed
    {
        sim::WorldState world = world_of(60, 60);
        sim::AntSnapshot dying = ant_at(1, 1, 640, 640);
        dying.hp = 0;
        sim::AntSnapshot drowning = ant_at(2, 2, 800, 640);
        drowning.is_drowning = true;
        world.ants.push_back(dying);
        world.ants.push_back(drowning);
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        // (640, 640) -> image (39, 30); (800, 640) -> (49, 30)
        check(find_fill(rr, 480 + 38, 35 + 29, 2, 2, 158), "an ant without hit points keeps its dot (red, colour 158)");
        check(find_fill(rr, 480 + 48, 35 + 29, 2, 2, 211), "a drowning ant keeps its dot (blue, colour 211)");
    }
    // The size grows with the scale: on a 31 x 31 map 32 px are trunc(119 / 31) = 3 wide and trunc(91 / 31) = 2 high; the top edge is taken from half of the WIDTH
    // (y - 1, where half of the height would also give y - 1). Ant at the tile centre (15, 15) = (496, 496): image (496 * 119 / 992, 496 * 91 / 992) = (59, 45)
    {
        sim::WorldState world = world_of(31, 31);
        world.ants.push_back(ant_at(1, 0, 496, 496));
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        check(find_fill(rr, 480 + 58, 35 + 44, 3, 2, 47), "on a 31x31 map the dot is 3x2: left x - 1, top y - 1, right x + 2, bottom y + 1");
    }
    // the quirk of the top edge: a 16 x 16 map has 32 px = trunc(119 / 16) = 7 wide and trunc(91 / 16) = 5 high; the top edge is y - 3 (half of 7), the bottom y + 3 (5 - 2)
    {
        sim::WorldState world = world_of(16, 16);
        world.ants.push_back(ant_at(1, 0, 256, 256));                      // image (256 * 119 / 512, 256 * 91 / 512) = (59, 45)
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        check(find_fill(rr, 480 + 59 - 3, 35 + 45 - 3, 7, 6, 47), "on a 16x16 map the top edge is taken from half the width: a 7x6 rectangle (y - 3 .. y + 3)");
    }

    // Plants (Block 1 flowers and clovers, 0x100e3b0): a dot of the colour and size flag of the tile's record (flower1 421: colour 51, two cells) at the centre of the cell,
    // 3 x 3 on a 60 x 60 map. Plant (20, 10): centre (656, 336) -> image (40, 15) -> left 39, top 14. Drawn before the ants, in the fog as well. A plant whose tile has no
    // record, or a size flag of 0, has no dot.
    {
        sim::WorldState world = world_of(60, 60);
        world.plants.push_back(sim::MapPlant{421, 20, 10});
        world.plants.push_back(sim::MapPlant{422, 30, 30});               // no record: no dot
        world.fog_of_war_enabled = true;
        world.fog_revealed.assign(3600, 0);
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        check(find_fill(rr, 480 + 39, 35 + 14, 3, 3, 51), "a flower plant is a 3x3 dot of colour 51 at the centre of its cell, even in the fog");
        int plant_dots = 0;
        for (const auto& f : rr.fills) if (f.w == 3 && f.h == 3 && f.x >= 480 && f.x < 599 && f.y >= 35 && f.y < 126) ++plant_dots;
        check(plant_dots == 1, "only the plant with a record has a dot");
    }

    // Fog: an own ant and an ally's always show, any other ant only on an explored cell (under the ant's pixel position)
    {
        sim::WorldState world = world_of(60, 60);
        world.fog_of_war_enabled = true;
        world.fog_revealed.assign(3600, 0);
        world.player_alliances[0] = 2;                                      // team 0 and team 2 are allies
        world.player_alliances[2] = 0;
        world.ants.push_back(ant_at(1, 0, 100, 100));                       // own, unexplored
        world.ants.push_back(ant_at(2, 2, 300, 100));                       // ally, unexplored
        world.ants.push_back(ant_at(3, 1, 500, 100));                       // enemy, unexplored
        world.ants.push_back(ant_at(4, 3, 700, 100));                       // enemy, explored (tile (21, 3))
        world.fog_revealed[3 * 60 + 21] = 1;
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        // the dots: (100, 100) -> image (6, 4); (300, 100) -> (18, 4); (500, 100) -> (30, 4); (700, 100) -> (43, 4)
        check(find_fill(rr, 480 + 5, 35 + 3, 2, 2, 47), "in fog the own ant shows");
        check(find_fill(rr, 480 + 17, 35 + 3, 2, 2, 211), "in fog an ally's ant shows");
        check(!find_fill(rr, 480 + 29, 35 + 3, 2, 2, 158), "in fog an enemy on an unexplored cell does not show");
        check(find_fill(rr, 480 + 42, 35 + 3, 2, 2, 239), "in fog an enemy on an explored cell shows");
        // seen from the ally's side, the first ant is the ally
        RecordingRenderer rr2(arc);
        HUD hud2;
        hud2.init(2);
        hud2.render(rr2, arc, world, camera);
        check(find_fill(rr2, 480 + 5, 35 + 3, 2, 2, 47), "team 2 sees its ally's ant (team 0)");
    }

    // The view frame: size (trunc(442 / scale) + 1, trunc(440 / scale) + 1) = 28 x 21 on a 60 x 60 map, at the view's origin divided by the scale, in (251, 251, 255); moved
    // inside the image when it would end beyond the right or bottom edge; it is the last thing drawn
    {
        sim::WorldState world = world_of(60, 60);
        auto frame_of = [&](float cx, float cy) {
            RecordingRenderer rr(arc);
            HUD hud;
            hud.init(0);
            ViewportCamera cam;
            cam.x = cx; cam.y = cy;
            hud.render(rr, arc, world, cam);
            struct R { int x, y, w, h; bool colour; size_t count; } r{0, 0, 0, 0, false, rr.rects.size()};
            if (!rr.rects.empty()) {
                const auto& f = rr.rects[0];
                r = {f.x, f.y, f.w, f.h, f.color.r == 251 && f.color.g == 251 && f.color.b == 255, rr.rects.size()};
            }
            return r;
        };
        auto f0 = frame_of(0.0f, 0.0f);
        check(f0.count == 1 && f0.x == 480 && f0.y == 35 && f0.w == 28 && f0.h == 21 && f0.colour, "frame at the origin: (480, 35) 28x21 in (251, 251, 255)");
        auto mid = frame_of(960.0f, 960.0f);                                // (960 * 119 / 1920, 960 * 91 / 1920) = (59, 45)
        check(mid.x == 480 + 59 && mid.y == 35 + 45 && mid.w == 28 && mid.h == 21, "the frame follows the view's origin, truncated");
        auto some = frame_of(1400.0f, 100.0f);                              // x: 1400 * 119 / 1920 = 86 -> right edge 480 + 86 + 28 = 594 < 599: not moved
        check(some.x == 480 + 86 && some.y == 35 + 4, "a frame that fits stays where it is");
        auto far = frame_of(1478.0f, 1480.0f);                              // x: 91 -> 480 + 91 + 28 = 599 reaches the edge: right = 599, left = 571; y: 70 -> bottom 126 -> top 105
        check(far.x == 571 && far.y == 105 && far.w == 28 && far.h == 21, "at the far corner the frame ends on the last pixel of the image (571, 105)");
        auto beyond = frame_of(1600.0f, 1600.0f);                           // (an over-scrolled view is still moved inside the image)
        check(beyond.x == 571 && beyond.y == 105 && beyond.w == 28 && beyond.h == 21, "a frame beyond the edge is moved inside the image");
    }
    // the frame of a small map: the view is larger than a 31 x 31 map's share: trunc(442 * 119 / 992) + 1 = 54 and trunc(440 * 91 / 992) + 1 = 41
    {
        sim::WorldState world = world_of(31, 31);
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        check(rr.rects.size() == 1 && rr.rects[0].w == 54 && rr.rects[0].h == 41, "on a 31x31 map the frame is 54x41");
    }
}

// The chat log window (Ants.exe 0x1011e78 and its line class 0x10123e2, docs 5.56): entries of two labels stacked one pixel apart, the window that follows the newest entry
// (CHATAPPD), the drag that shows earlier lines while the button is held (0x1012015 / 0x1012096 / 0x101228a) and its auto-repeat (CHATSCRL).
namespace {
uint32_t g_chat_clock = 0;
uint32_t chat_clock() { return g_chat_clock; }

// A renderer with proportional letters: 'i', 'l' and ' ' are 3 px wide, everything else 7 px
class NarrowRenderer : public IRenderer {
public:
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string&, int32_t, int32_t, assets::ColorRGBA) override {}
    int32_t get_text_width(const std::string& text, FontSize = FontSize::Px12) const override {
        int32_t w = 0;
        for (char c : text) w += (c == 'i' || c == 'l' || c == ' ') ? 3 : 7;
        return w;
    }
    void set_hud_team(uint8_t) override {}
};
}  // namespace

void test_chat_log(const assets::AssetArchive& arc) {
    std::printf("[chat] the log window: one pixel between entries, pixel wrapping, the follow task, the drag and its auto-repeat, the clip\n");
    sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 1);
    ViewportCamera camera;
    auto text_at = [](const RecordingRenderer& rr, const std::string& text) -> const RecordingRenderer::Text* {
        for (const auto& t : rr.texts) if (t.text == text) return &t;
        return nullptr;
    };

    HUD hud;
    hud.init(0);
    hud.set_ticks_function(&chat_clock);
    g_chat_clock = 1000;
    // the start message: a 12 px header and a body of two 12 px lines (36 px); the log ends one pixel below
    check(hud.chat_content_end() == 37 && hud.chat_follow_pos() == 0 && hud.chat_follow_target() == 0, "the start message fills 36 px and the log ends at 37");
    hud.add_chat_entry("Ann", "hi", false, 2);                   // 24 px: top 37, bottom 61
    check(hud.chat_content_end() == 62, "an entry starts one pixel below the one before: 37 + 24 + 1 = 62");
    check(hud.chat_follow_target() == 0, "a log that fits in the 101 px view does not move the window");

    // what is drawn: headers at the view's left edge, bodies 10 px right of it, the log from y = 299, inside a clip of the view's size
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, sim.get_world_state(), camera);
        const auto* news = text_at(rr, "[0:00] News Flash:");
        const auto* ann = text_at(rr, "Ann:");
        const auto* hi = text_at(rr, "hi");
        check(news && news->x == 482 && news->y == 299, "the first header is at (482, 299)");
        check(ann && ann->x == 482 && ann->y == 299 + 37, "the next header is 37 px below it (a 36 px entry and one pixel)");
        check(hi && hi->x == 492 && hi->y == 299 + 37 + 12, "a body is 10 px right of its header and one header line below it");
        check(rr.clips.size() == 1 && rr.clips[0] == std::array<int32_t, 4>{482, 299, 138, 101} && rr.clip_clears == 1 && !rr.clip_on, "the log is clipped to its 138 x 101 view");
    }

    // the third entry ends at 112 - 0 > 101: the window has to follow. Its target puts the new bottom on the view's last row (bottom 111 - 101 = 10); the task is due at once
    hud.add_chat_entry("Bob", "hello", false, 3);                // top 62, bottom 86, end 87
    check(hud.chat_follow_target() == 0, "87 px still fit");
    hud.add_chat_entry("Cy", "there", false, 1);                 // top 87, bottom 111, end 112
    check(hud.chat_content_end() == 112 && hud.chat_follow_target() == 10 && hud.chat_follow_pos() == 0, "the log ends at 112: the window's target is bottom - 101 = 10");
    hud.update(sim.get_world_state(), 1);                        // the task's first pass: 5 px
    check(hud.chat_follow_pos() == 5, "the follow task moves the window 5 px at its first pass");
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_follow_pos() == 5, "...and waits 50 ms for the next");
    g_chat_clock += 49;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_follow_pos() == 5, "49 ms are not enough");
    g_chat_clock += 1;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_follow_pos() == 10, "the next pass at 50 ms reaches the target 10");
    g_chat_clock += 50;
    hud.update(sim.get_world_state(), 1);                        // the pass that finds the target reached ends the task
    g_chat_clock += 50;
    hud.add_chat_entry("Di", "x", false, 0);                      // top 112, bottom 136, end 137: target 35; the task was gone, so this starts it again (window at its target)
    check(hud.chat_follow_target() == 35 && hud.chat_follow_pos() == 10, "a new entry moves the target");
    hud.add_chat_entry("Ed", "y", false, 0);                      // top 137, bottom 161, end 162: target 60; the task is running (the window is away from its target): only the target moves
    check(hud.chat_follow_target() == 60, "the target follows the newest bottom");
    for (int pass = 0; pass < 40; ++pass) { g_chat_clock += 50; hud.update(sim.get_world_state(), 1); }
    check(hud.chat_follow_pos() == 60, "the window arrives at 60, 5 px per 50 ms pass and never beyond");

    // the window is drawn from its position: the first entry is out of the view, the partly visible ones are drawn (the clip cuts them), Cy's body ends at the view's last row
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, sim.get_world_state(), camera);
        check(text_at(rr, "[0:00] News Flash:") == nullptr, "an entry whose bottom is above the window is not drawn (36 <= 60)");
        const auto* ann = text_at(rr, "Ann:");
        check(ann && ann->y == 299 + 37 - 60, "Ann's header is drawn above the view, at y = 299 + 37 - 60 (only its last rows would show: entry bottom 61 > 60)");
        const auto* ed = text_at(rr, "Ed:");
        check(ed && ed->y == 299 + 137 - 60, "the newest header at 299 + 137 - 60 = 376");
        const auto* y_body = text_at(rr, "y");
        check(y_body && y_body->y == 299 + 137 - 60 + 12 && y_body->y + 12 == 299 + 101, "its body ends on the last row of the view (y + 12 = 400)");
    }

    // The drag (0x1012015): a left press inside the view starts it from the window's position; a press outside or with the right button does nothing
    const int32_t follow = hud.chat_follow_pos();
    hud.handle_mouse_down(470, 350, SDL_BUTTON_LEFT, sim, camera);
    check(!hud.chat_dragging(), "a press left of the view starts nothing");
    hud.handle_mouse_down(625, 350, SDL_BUTTON_LEFT, sim, camera);
    check(!hud.chat_dragging(), "a press right of the view starts nothing");
    hud.handle_mouse_down(500, 405, SDL_BUTTON_LEFT, sim, camera);
    check(!hud.chat_dragging(), "a press below the view (the divider bar) starts nothing");
    hud.handle_mouse_down(500, 350, SDL_BUTTON_RIGHT, sim, camera);
    check(!hud.chat_dragging(), "the right button does not drag the log");
    hud.handle_mouse_up(500, 350, SDL_BUTTON_RIGHT, sim, camera);
    hud.handle_mouse_down(500, 350, SDL_BUTTON_LEFT, sim, camera);
    check(hud.chat_dragging() && hud.chat_drag_offset() == follow && hud.chat_view_offset() == follow, "a press inside the view starts the drag at the window's position");
    // a move inside the view scrolls by the distance that the pointer went UP; the window is clamped to [0, end - 101] = [0, 61]
    hud.handle_mouse_motion(500, 380, sim, camera);                // 30 px down: earlier lines
    check(hud.chat_drag_offset() == follow - 30, "30 px down shows 30 px earlier");
    hud.handle_mouse_motion(500, 300, sim, camera);                // 80 px up
    check(hud.chat_drag_offset() == 61, "80 px up is clamped at the end of the log minus the view (162 - 101 = 61)");
    hud.handle_mouse_motion(500, 398, sim, camera);                // 98 px down
    check(hud.chat_drag_offset() == 0, "and at 0 at the top");
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, sim.get_world_state(), camera);
        const auto* news = text_at(rr, "[0:00] News Flash:");
        check(news && news->y == 299, "while dragging the log is drawn from the dragged position (here the top)");
    }
    // outside the view a move scrolls nothing; CHATSCRL (100 ms) scrolls 15 px per pass, up while the pointer is below the view and down while it is above it
    hud.handle_mouse_motion(500, 420, sim, camera);                // below the view
    check(hud.chat_drag_offset() == 0, "a move outside the view does not scroll");
    hud.handle_mouse_motion(500, 250, sim, camera);                // above the view
    g_chat_clock += 100;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 15, "above the view: +15 at a pass of CHATSCRL");
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 15, "the next pass is 100 ms later");
    g_chat_clock += 100;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 30, "+15 again");
    hud.handle_mouse_motion(500, 420, sim, camera);                // below the view
    g_chat_clock += 100;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 15, "below the view: -15");
    g_chat_clock += 100;
    hud.update(sim.get_world_state(), 1);
    g_chat_clock += 100;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 0, "...down to the top and not beyond");
    // the release ends it: the log shows the window that follows the newest entry again, and nothing scrolls any more
    hud.handle_mouse_motion(500, 250, sim, camera);
    hud.handle_mouse_up(500, 250, SDL_BUTTON_LEFT, sim, camera);
    check(!hud.chat_dragging() && hud.chat_view_offset() == hud.chat_follow_pos(), "the release snaps the log back to the newest entries");
    g_chat_clock += 500;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 0 && hud.chat_view_offset() == hud.chat_follow_pos(), "no task scrolls after the release");

    // the window keeps following while it is dragged: the dragged view is not touched
    hud.handle_mouse_down(500, 350, SDL_BUTTON_LEFT, sim, camera);
    const int32_t dragged = hud.chat_drag_offset();
    hud.add_chat_entry("Flo", "z", false, 2);
    check(hud.chat_drag_offset() == dragged && hud.chat_follow_target() == hud.chat_content_end() - 1 - 101, "a new entry moves the target, not the dragged position");
    hud.handle_mouse_up(500, 350, SDL_BUTTON_LEFT, sim, camera);

    // a dialog takes the press: with the options screen open the log is not dragged
    hud.open_options();
    hud.handle_mouse_down(500, 350, SDL_BUTTON_LEFT, sim, camera);
    check(!hud.chat_dragging(), "a modal window takes every press");
    hud.handle_mouse_up(500, 350, SDL_BUTTON_LEFT, sim, camera);
    hud.close_options();

    // a header that is wider than its 138 px shows its end (the label's tail flag): 6 px per character here, 23 characters fit
    {
        HUD h2;
        h2.init(0);
        h2.add_chat_entry(std::string(30, 'A'), "x", false, 3);
        RecordingRenderer rr(arc);
        h2.render(rr, arc, sim.get_world_state(), camera);
        bool found = false;
        for (const auto& t : rr.texts) if (t.text.size() == 23 && t.text.back() == ':' && t.text[0] == 'A') found = t.x == 482 + 138 - 23 * 6;
        check(found, "a header wider than 138 px is drawn right aligned with its front cut off");
    }

    // the body wraps by PIXELS with the measure that the HUD was given (the original measures a label with GDI when it is made): 'i' is 3 px, so eight "iiii" words (117 px) fit
    {
        NarrowRenderer narrow;
        HUD h3;
        h3.init(0);
        h3.set_text_metrics(&narrow);
        const size_t before = h3.get_chat_log().size();
        h3.add_chat_entry("Ann", "iiii iiii iiii iiii iiii iiii iiii iiii iiii iiii", false, 2);
        const auto& log = h3.get_chat_log();
        check(log.size() == before + 3 && log[before] == "Ann:" && log[before + 1] == "iiii iiii iiii iiii iiii iiii iiii iiii" && log[before + 2] == "iiii iiii",
              "with proportional letters a line holds eight words (39 characters), not 20");
        h3.set_text_metrics(nullptr);                           // the entries are measured again with 6 px per character
        check(h3.get_chat_log().size() > before + 3, "a new measure lays every entry out again");
    }

    // the transcript the original writes to chat.txt at the end of the program (0x10122d4): "date @ time", a blank line, "header body" per entry. The start of the original's own file:
    {
        HUD h4;
        h4.init(0);
        check(h4.chat_transcript("04/17/98 @ 18:19:56") == "04/17/98 @ 18:19:56\n\n[0:00] News Flash: Game started! Go get that food!\n", "the transcript starts like the original's chat.txt");
        h4.add_chat_entry("Ann", "hello there", true, 2);
        check(h4.chat_transcript("d @ t") == "d @ t\n\n[0:00] News Flash: Game started! Go get that food!\nAnn (To Teammate): hello there\n", "every entry is one line: header, a space, the body");
    }
}

// The static HUD shell is the animation `uishell` (14 parts) drawn last part first, nothing static is drawn twice, the
// chat cover appears only when chat is switched off, and the lunchbox indicator needs every selected ant to carry food.
void test_static_shell(const assets::AssetArchive& arc) {
    std::printf("[hud] static shell composite, chat cover and lunchbox indicator\n");
    sim::WorldState world;
    sim::AntSnapshot carrier{};
    carrier.id = 7; carrier.player_id = 0; carrier.hp = 10; carrier.tile_x = 5; carrier.tile_y = 5;
    carrier.px = 176; carrier.py = 176; carrier.is_holding = true;
    sim::AntSnapshot empty_handed = carrier;
    empty_handed.id = 8; empty_handed.tile_x = 6; empty_handed.px = 208; empty_handed.is_holding = false;
    world.ants.push_back(carrier);
    world.ants.push_back(empty_handed);

    const auto* shell = arc.find_animation("uishell");
    check(shell && !shell->subitems.empty() && shell->subitems[0].frames.size() == 14, "uishell has 14 parts");
    if (!shell || shell->subitems.empty()) return;
    {
        HUD hud;
        hud.init(0);
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        std::vector<size_t> draw_index;
        for (const auto& part : shell->subitems[0].frames) {
            const std::string name = arc.get_sprite(part.sprite_index).name;
            size_t count = 0;
            size_t at = SIZE_MAX;
            for (size_t i = 0; i < rr.sprites.size(); ++i) {
                if (rr.sprites[i].name != name) continue;
                ++count;
                if (rr.sprites[i].x == part.dx && rr.sprites[i].y == part.dy) at = i;
            }
            check(count == 1 && at != SIZE_MAX, "shell part " + name + " drawn once at (" + std::to_string(part.dx) + "," + std::to_string(part.dy) + ")");
            draw_index.push_back(at);
        }
        bool last_first = true;
        for (size_t k = 1; k < draw_index.size(); ++k) if (draw_index[k] >= draw_index[k - 1]) last_first = false;
        check(last_first, "shell parts are drawn last part first");
    }

    // Chat cover: three tiles, only after chat is switched off in the options
    const auto* cover = arc.find_animation("chatcovr");
    check(cover && !cover->subitems.empty() && cover->subitems[0].frames.size() == 3, "chatcovr has 3 parts");
    if (cover && !cover->subitems.empty()) {
        const std::string tile = arc.get_sprite(cover->subitems[0].frames[0].sprite_index).name;
        HUD hud;
        hud.init(0);
        RecordingRenderer with_chat(arc);
        render_settled(hud, arc, world, with_chat);
        check(with_chat.named(tile).empty(), "chat on: no chat cover");

        sim::SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        ViewportCamera camera;
        hud.open_options();
        hud.handle_mouse_down(160, 300, 1, sim, camera);   // the options' Chat OFF button spans x 151..200, y 289..313
        hud.handle_mouse_up(160, 300, 1, sim, camera);     // a switch acts at the release
        hud.close_options();
        check(!hud.is_chat_enabled(), "options chat OFF button switches chat off");
        RecordingRenderer no_chat(arc);
        render_settled(hud, arc, world, no_chat);
        check(no_chat.named(tile).size() == 3, "chat off: three cover tiles");
        check(no_chat.has_sprite_at(tile, 478, 421) && no_chat.has_sprite_at(tile, 478, 436) && no_chat.has_sprite_at(tile, 478, 445),
              "chat cover tiles at (478,421), (478,436), (478,445)");
    }

    // Lunchbox indicator UI_LBOX at (597,131)
    const auto* lbox = arc.find_animation("UI_LBOX");
    check(lbox && !lbox->subitems.empty() && !lbox->subitems[0].frames.empty(), "UI_LBOX exists");
    if (lbox && !lbox->subitems.empty() && !lbox->subitems[0].frames.empty()) {
        const std::string name = arc.get_sprite(lbox->subitems[0].frames[0].sprite_index).name;
        auto lunchbox_drawn = [&](std::vector<uint32_t> selection) {
            HUD hud;
            hud.init(0);
            hud.set_selected_ant_ids(std::move(selection));
            RecordingRenderer rr(arc);
            render_settled(hud, arc, world, rr);
            return rr.has_sprite_at(name, 597, 131);
        };
        check(lunchbox_drawn({7}), "lunchbox shown while the only selected ant carries food");
        check(!lunchbox_drawn({8}), "lunchbox hidden while the selected ant carries nothing");
        check(!lunchbox_drawn({7, 8}), "lunchbox hidden unless every selected ant carries food");
    }
}

// Cursor rules (Ants.exe 0x1026d6a, 0x1026d98): the map cursor only applies inside the map view (16,21)-(458,461) and
// stays the plain arrow while a selection box larger than 4 px is dragged.
void test_cursor_rules(const assets::AssetArchive&) {
    std::printf("[hud] cursor view rectangle and drag rule\n");
    sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 100, 60000);
    sim::WorldState world;
    world.width = 60;
    world.height = 60;
    sim::AntSnapshot enemy{};
    enemy.id = 9; enemy.player_id = 1; enemy.hp = 10; enemy.tile_x = 5; enemy.tile_y = 5; enemy.px = 176; enemy.py = 176;
    sim::AntSnapshot far_east = enemy;
    far_east.id = 10; far_east.tile_x = 13; far_east.px = 443;
    world.ants.push_back(enemy);
    world.ants.push_back(far_east);

    ViewportCamera camera;
    camera.viewport_w = 441;
    camera.viewport_h = 439;
    camera.world_x = 0;
    camera.world_y = 0;

    HUD hud;
    hud.init(0);
    check(hud.evaluate_cursor(193, 198, world, sim.grid(), camera) == CursorType::Select, "an enemy ant under the pointer shows the select cursor");
    check(hud.evaluate_cursor(460, 198, world, sim.grid(), camera) == CursorType::Normal, "no map cursor right of x = 458");

    // Drag box of 10 px: the pointer stays normal
    hud.handle_mouse_down(193, 198, 1, sim, camera);
    hud.handle_mouse_motion(203, 208, sim, camera);
    check(hud.evaluate_cursor(203, 208, world, sim.grid(), camera) == CursorType::Normal, "dragging a box wider than 4 px keeps the plain pointer");
    hud.handle_mouse_up(203, 208, 1, sim, camera);

    // Drag of 2 px: still the map cursor
    HUD small;
    small.init(0);
    small.handle_mouse_down(193, 198, 1, sim, camera);
    small.handle_mouse_motion(195, 199, sim, camera);
    check(small.evaluate_cursor(195, 199, world, sim.grid(), camera) == CursorType::Select, "a drag of 4 px or less keeps the map cursor");
}

// Buttons are drawn from the original's three-state animations with absolute coordinates: up, hover (a small "r"
// label over the up art) and pressed; the setup screen controls sit where those animations put them.
void test_button_states(const assets::AssetArchive& arc) {
    std::printf("[hud] button hover/pressed art, option controls, quit dialog and setup screen\n");
    sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 100, 60000);
    sim::WorldState world;
    ViewportCamera camera;

    // Top bar: hover draws buthelpr at (484,10) over buthelpu at (476,7); pressed draws buthelpd
    {
        HUD hud;
        hud.init(0);
        hud.handle_mouse_motion(490, 15, sim, camera);
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(rr.has_sprite_at("buthelpr.bmp", 484, 10) && rr.has_sprite_at("buthelpu.bmp", 476, 7), "help hover: r label at (484,10) over the up art");
        check(rr.named("buthelpd.bmp").empty(), "help hover: no pressed art");
        hud.handle_mouse_down(490, 15, 1, sim, camera);
        RecordingRenderer down(arc);
        render_settled(hud, arc, world, down);
        check(down.has_sprite_at("buthelpd.bmp", 476, 7), "help pressed: buthelpd at (476,7)");
        hud.handle_mouse_up(490, 15, 1, sim, camera);
    }
    {
        HUD hud;
        hud.init(0);
        hud.handle_mouse_motion(600, 15, sim, camera);
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(rr.has_sprite_at("butquitr.bmp", 588, 10) && rr.has_sprite_at("butquitu.bmp", 579, 7), "quit hover: r label at (588,10)");
        hud.handle_mouse_motion(540, 15, sim, camera);
        RecordingRenderer opt(arc);
        render_settled(hud, arc, world, opt);
        check(opt.has_sprite_at("butoptr.bmp", 528, 10) && opt.has_sprite_at("butoptu.bmp", 525, 7), "options hover: r label at (528,10)");
    }
    // Send-to buttons: hover label of All at (543,447)
    {
        HUD hud;
        hud.init(0);
        hud.handle_mouse_motion(550, 450, sim, camera);
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(rr.has_sprite_at("butallr.bmp", 543, 447) && rr.has_sprite_at("butallu.bmp", 532, 443), "All hover: r label at (543,447) over the up art at (532,443)");
    }

    // The Sound slider applies the option at the RELEASE and then plays the test voice (gantrdy, 14); the Music slider applies at the release; a press changes nothing and
    // dragging only moves the thumb (FUN_010115ca, FUN_0101161b, callback 0x1015058); the applied value is what the thumb stands for: (pos - 211) 100 / 185
    {
        HUD hud;
        hud.init(0);
        std::vector<int32_t> applied_sfx;
        std::vector<int32_t> applied_music;
        std::vector<uint32_t> played;
        hud.set_on_sfx_volume([&](int32_t v) { applied_sfx.push_back(v); });
        hud.set_on_music_volume([&](int32_t v) { applied_music.push_back(v); });
        hud.set_on_play_sfx([&](uint32_t s) { played.push_back(s); });
        hud.open_options();
        hud.handle_mouse_down(300, 185, 1, sim, camera);                      // press on the Sound slider
        check(hud.options_screen().slider(0).position() == 395 && hud.get_sound_volume() == 100, "sound slider: the press moves nothing");
        hud.handle_mouse_motion(330, 185, sim, camera);                       // drag
        hud.handle_mouse_motion(350, 185, sim, camera);
        check(hud.options_screen().slider(0).thumb_left() == 327, "sound slider: the thumb follows the pointer (x 350 - 23)");
        check(applied_sfx.empty() && played.empty(), "sound slider: nothing is applied and no voice plays while the thumb is dragged");
        hud.handle_mouse_up(350, 185, 1, sim, camera);
        check(applied_sfx.size() == 1 && applied_sfx[0] == 75, "sound slider: the option is applied once, at the release: (350 - 211) 100 / 185 = 75");
        check(played.size() == 1 && played[0] == sim::SoundID::GeneralReady, "sound slider: the test voice gantrdy (14) plays after it");
        played.clear();
        hud.handle_mouse_down(300, 222, 1, sim, camera);                      // the Music slider
        hud.handle_mouse_motion(260, 222, sim, camera);
        check(applied_music.empty(), "music slider: nothing is applied while dragging");
        hud.handle_mouse_up(260, 222, 1, sim, camera);
        check(applied_music.size() == 1 && applied_music[0] == 26 && played.empty(), "music slider: applied once at the release (26), no voice");
        check(applied_sfx.size() == 1, "and the sound option stays as it was");
        // every event is preceded by the move (FUN_0102737e calls FUN_0102653f first): the release moves the thumb to the pointer even when no motion came between, so a
        // plain click sets the value of the clicked position; the far end of the track is 99, the Sound Volume 100 of the start is out of reach
        HUD other;
        other.init(0);
        std::vector<int32_t> again;
        other.set_on_sfx_volume([&](int32_t v) { again.push_back(v); });
        other.open_options();
        other.handle_mouse_down(300, 185, 1, sim, camera);
        other.handle_mouse_up(300, 185, 1, sim, camera);
        check(again.size() == 1 && again[0] == 48, "a click on the Sound slider without a move sets the value of the clicked position: (300 - 211) 100 / 185 = 48");
        other.handle_mouse_down(418, 190, 1, sim, camera);
        other.handle_mouse_up(418, 190, 1, sim, camera);
        check(again.size() == 2 && again[1] == 99, "a click at the far end: 99");
    }

    // Option screen controls (op_screen): toggles, Return, slider thumbs at the original's defaults 100 / 65 / 50
    {
        HUD hud;
        hud.init(0);
        hud.open_options();
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(hud.is_chat_enabled() && hud.is_quick_help_enabled(), "options default: chat on, quick help on");
        check(hud.get_sound_volume() == 100 && hud.get_music_volume() == 65 && hud.get_scroll_speed() == 50, "options default: sound 100, music 65, scroll 50");
        check(rr.has_sprite_at("optond.bmp", 102, 289) && rr.has_sprite_at("dbutoffu.bmp", 151, 290), "chat on: ON down at (102,289), OFF up at (151,290)");
        check(rr.has_sprite_at("optond.bmp", 355, 289) && rr.has_sprite_at("dbutoffu.bmp", 404, 290), "quick help on: ON down at (355,289), OFF up at (404,290)");
        check(rr.has_sprite_at("breturn1.bmp", 351, 425), "Return button up art at (351,425)");
        check(rr.has_sprite_at("slidd.bmp", 372, 178) && rr.has_sprite_at("slidd.bmp", 309, 215) && rr.has_sprite_at("slidd.bmp", 281, 252),
              "slider thumbs at x = 188 + min(184, 185 v / 99) on rows 178 / 215 / 252");
        // hover over the OFF toggle of the chat pair shows its hover art (op_coffr: r label over the up art)
        hud.handle_mouse_motion(170, 300, sim, camera);
        RecordingRenderer hov(arc);
        render_settled(hud, arc, world, hov);
        check(hov.has_sprite_at("optoffr.bmp", 158, 292), "chat OFF hover: optoffr label at (158,292)");
        // Return pressed: breturn3 at (353,427)
        hud.handle_mouse_down(400, 440, 1, sim, camera);
        RecordingRenderer down(arc);
        render_settled(hud, arc, world, down);
        check(down.has_sprite_at("breturn3.bmp", 353, 427), "Return pressed: breturn3 at (353,427)");
        hud.handle_mouse_up(400, 440, 1, sim, camera);
        check(!hud.is_options_open(), "releasing Return closes the options");
        // Toggle hit rectangles are the union of the resting and pressed art; a switch acts at the release
        hud.open_options();
        auto click = [&](int32_t x, int32_t y) {
            hud.handle_mouse_down(x, y, 1, sim, camera);
            hud.handle_mouse_up(x, y, 1, sim, camera);
        };
        click(102, 289);
        click(150, 312);
        check(hud.is_chat_enabled(), "(150,312) is outside the chat OFF toggle");
        click(151, 289);
        check(!hud.is_chat_enabled(), "(151,289) is inside the chat OFF toggle");
        click(102, 289);
        check(hud.is_chat_enabled(), "(102,289) is inside the chat ON toggle");
        click(404, 289);
        check(!hud.is_quick_help_enabled(), "(404,289) is inside the quick help OFF toggle (the startup help is skipped)");
        click(355, 289);
        check(hud.is_quick_help_enabled(), "(355,289) is inside the quick help ON toggle");
        hud.handle_mouse_down(151, 289, 1, sim, camera);
        check(hud.is_chat_enabled(), "a press on a switch changes nothing");
        hud.handle_mouse_up(151, 289, 1, sim, camera);
        hud.close_options();
    }

    // The options screen's controls in every state (FUN_01011281: up, hover, down, down + hover) and its edit fields (FUN_010119a8, label FUN_0102b36a): 12 px text in
    // (239, 231, 223) at the field's corner, the caret "_" behind the text, blinking every 150 ms from the focus; a focused text that does not fit shows its end
    {
        auto drew = [&](const RecordingRenderer& rr, const char* anim) {
            const auto* seq = arc.find_animation(anim);
            if (!seq || seq->subitems.empty()) return false;
            for (const auto& part : seq->subitems[0].frames) {
                if (!rr.has_sprite_at(arc.get_sprite(part.sprite_index).name, part.dx, part.dy)) return false;
            }
            return true;
        };
        for (const char* name : {"op_conu", "op_conr", "op_cond", "op_condr", "op_coffu", "op_coffr", "op_coffd", "op_coffdr", "op_honu", "op_honr", "op_hond", "optondr", "op_hoffu",
                                 "op_hoffr", "op_hoffd", "op_hoffdr", "breturn1", "breturn2", "breturn3", "op_screen"}) {
            check(arc.find_animation(name) != nullptr, std::string("the options animation ") + name + " exists");
        }
        HUD hud;
        hud.init(0);
        hud.set_ticks_function(&test_clock);
        g_now_ms = 1000;
        hud.open_options();
        ViewportCamera cam;
        auto frame = [&](uint32_t now) {
            g_now_ms = now;
            RecordingRenderer rr(arc);
            hud.render(rr, arc, world, cam);
            return rr;
        };
        auto text_at = [&](const RecordingRenderer& rr, const std::string& text, int32_t x, int32_t y) {
            for (const auto& t : rr.texts) {
                if (t.text == text && t.x == x && t.y == y && t.size == FontSize::Px12 && t.colour.r == 239 && t.colour.g == 231 && t.colour.b == 223) return true;
            }
            return false;
        };
        RecordingRenderer f = frame(1000);
        check(text_at(f, "Now you are in for it!", 92, 370) && text_at(f, "Let me be!", 92, 402) && text_at(f, "Attack!", 302, 370) && text_at(f, "Do you want to ally?", 302, 402),
              "the four quick chats at the corners of their fields (92, 370), (92, 402), (302, 370), (302, 402) in 12 px (239, 231, 223)");
        check(text_at(f, "_", 92 + 22 * 6, 370), "the F9 field has the focus: the caret right behind its text");
        check(text_at(frame(1149), "_", 92 + 22 * 6, 370) && !text_at(frame(1150), "_", 92 + 22 * 6, 370) && !text_at(frame(1299), "_", 92 + 22 * 6, 370) &&
                  text_at(frame(1300), "_", 92 + 22 * 6, 370),
              "the caret shows 150 ms, is gone 150 ms, shows again (from the moment the screen opened)");
        // a press on the F10 field moves the focus and the caret
        hud.handle_mouse_down(150, 410, 1, sim, camera);
        RecordingRenderer g = frame(1400);
        check(text_at(g, "_", 92 + 10 * 6, 402) && !text_at(g, "_", 92 + 22 * 6, 370), "a press on F10: the caret is behind its text (the phase starts at the press: visible)");
        // typed text appears at once
        hud.handle_text_input(" ok");
        check(text_at(frame(1400), "Let me be! ok", 92, 402) && hud.get_quick_chat_key(1) == "Let me be! ok", "typed text shows in the field and is the quick chat");
        // a click on the window takes the focus: no caret at all
        hud.handle_mouse_down(300, 330, 1, sim, camera);
        RecordingRenderer h = frame(1400);
        bool any_caret = false;
        for (const auto& t : h.texts) if (t.text == "_" && t.colour.r == 239 && t.colour.g == 231 && t.colour.b == 223) any_caret = true;
        check(!any_caret, "no field has the focus: no caret");
        // a text that does not fit (135 px here: 22 characters of 6 px): focused it shows its end, unfocused its beginning
        hud.options().quick_chat[0] = "0123456789abcdefghijklmnopqrstuvwxyz";
        hud.close_options();
        hud.open_options();
        RecordingRenderer j = frame(2000);
        check(text_at(j, "efghijklmnopqrstuvwxyz", 92 + 135 - 132, 370) && text_at(j, "_", 92 + 135, 370), "focused and too long: the end shows, right aligned before the caret's room");
        hud.handle_mouse_down(300, 330, 1, sim, camera);
        RecordingRenderer k = frame(2000);
        check(text_at(k, "0123456789abcdefghijkl", 92, 370), "unfocused: the beginning shows, cut at the right edge of the field");
        hud.close_options();

        // the pictures of a switch in every state
        hud.open_options();
        hud.handle_mouse_motion(170, 300, sim, camera);                         // over Chat OFF (not latched)
        RecordingRenderer a1 = frame(3000);
        check(drew(a1, "op_coffr") && !drew(a1, "op_coffdr"), "hover over an unlatched switch: the hover picture");
        hud.handle_mouse_down(170, 300, 1, sim, camera);
        RecordingRenderer a2 = frame(3000);
        check(drew(a2, "op_coffd"), "pressed: the down picture");
        hud.handle_mouse_motion(172, 300, sim, camera);
        RecordingRenderer a3 = frame(3000);
        check(drew(a3, "op_coffdr"), "held down and moved inside: down + hover");
        hud.handle_mouse_up(172, 300, 1, sim, camera);
        hud.update(world, 1);                                                    // the 50 ms poll of the pointer
        RecordingRenderer a4 = frame(3000);
        check(drew(a4, "op_coffdr") && drew(a4, "op_conu"), "released: Chat OFF is latched (down + hover under the pointer), Chat ON is up");
        hud.handle_mouse_motion(300, 330, sim, camera);
        hud.update(world, 1);
        RecordingRenderer a5 = frame(3000);
        check(drew(a5, "op_coffd") && !drew(a5, "op_coffdr") && drew(a5, "op_conu"), "the pointer elsewhere: Chat OFF down, Chat ON up");
        hud.close_options();
    }

    // The chat input box is the edit control of FUN_0100dbe2 (docs 5.51): the rectangle (481, 424) - (620, 436), 12 px letters in (7, 11, 15), a caret that toggles every
    // 150 ms from the moment the screen was built, the end of a text that does not fit, nothing while the chat option is off
    {
        HUD hud;
        hud.set_ticks_function(&test_clock);
        g_now_ms = 5000;
        hud.init(0);                                              // the control gets the focus now: the caret origin is 5000
        ViewportCamera cam;
        auto frame = [&](uint32_t now) {
            g_now_ms = now;
            RecordingRenderer rr(arc);
            hud.render(rr, arc, world, cam);
            return rr;
        };
        auto text_at = [&](const RecordingRenderer& rr, const std::string& text, int32_t x, int32_t y) {
            for (const auto& t : rr.texts) {
                if (t.text == text && t.x == x && t.y == y && t.size == FontSize::Px12 && t.colour.r == 7 && t.colour.g == 11 && t.colour.b == 15) return true;
            }
            return false;
        };
        check(text_at(frame(5000), "_", 481, 424) && text_at(frame(5149), "_", 481, 424), "empty box: the caret at its corner (481, 424), visible for the first 150 ms");
        check(!text_at(frame(5150), "_", 481, 424) && !text_at(frame(5299), "_", 481, 424) && text_at(frame(5300), "_", 481, 424), "then gone for 150 ms, then visible again");
        hud.set_chat_input("hello");
        RecordingRenderer a = frame(5000);
        check(text_at(a, "hello", 481, 424) && text_at(a, "_", 481 + 5 * 6, 424), "the text at (481, 424) in (7, 11, 15), the caret right behind it");
        // a text wider than the box (139 px less the caret's 6 here: 22 characters of 6 px) shows its end, the caret at the right end of the box
        hud.set_chat_input("0123456789abcdefghijklmnopqrstuvwxyz");
        RecordingRenderer b = frame(5000);
        check(text_at(b, "efghijklmnopqrstuvwxyz", 481 + 133 - 132, 424) && text_at(b, "_", 481 + 133, 424), "a text that does not fit shows its end; the caret at the right end");
        // chat off: the box is hidden and covered
        hud.open_options();
        hud.handle_mouse_down(160, 300, 1, sim, cam);
        hud.handle_mouse_up(160, 300, 1, sim, cam);
        hud.close_options();
        RecordingRenderer c = frame(5000);
        bool any = false;
        for (const auto& t : c.texts) if (t.colour.r == 7 && t.colour.g == 11 && t.colour.b == 15 && (t.text == "_" || t.text.rfind("0123", 0) == 0 || t.text.find("efgh") != std::string::npos)) any = true;
        check(!any, "with the chat option off nothing of the input box is drawn");
    }

    // Quit dialog buttons: yes1 at (180,260); pressed yes3 has its part at (0,1) relative to the origin
    {
        HUD hud;
        hud.init(0);
        hud.open_quit_dialog();
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(rr.has_sprite_at("yes1.bmp", 180, 260) && rr.has_sprite_at("no1.bmp", 292, 260), "quit dialog: yes1 (180,260) and no1 (292,260)");
        hud.handle_mouse_down(200, 270, 1, sim, camera);
        RecordingRenderer down(arc);
        render_settled(hud, arc, world, down);
        check(down.has_sprite_at("yes3.bmp", 180, 261), "yes pressed: yes3 one pixel lower at (180,261)");
        hud.handle_mouse_up(700, 700, 1, sim, camera);
    }

    // Setup screen: every button has the rectangle of its resting / hovering picture and that of its pressed picture (the original's hit test is the rectangle of the
    // picture that shows, docs 5.52); the Fog of War pair keeps the union of its pictures; art at the animation coordinates
    {
        auto union_of = [&](const char* a, const char* b) {
            const UIRect ra = animation_bounds(arc, a);
            const UIRect rb = animation_bounds(arc, b);
            const int32_t x0 = std::min(ra.x, rb.x), y0 = std::min(ra.y, rb.y);
            const int32_t x1 = std::max(ra.x + ra.w, rb.x + rb.w), y1 = std::max(ra.y + ra.h, rb.y + rb.h);
            return UIRect{x0, y0, x1 - x0, y1 - y0};
        };
        auto same = [&](const UIRect& r, int32_t x, int32_t y, int32_t w, int32_t h) { return r.x == x && r.y == y && r.w == w && r.h == h; };
        auto same_pressed = [&](const UIRect& r, const ButtonRect& b) { return same(r, b.x, b.y, b.w, b.h); };
        using MS = MapSelectScreen;
        check(same(animation_bounds(arc, "up1"), MS::BTN_UP_X, MS::BTN_UP_Y, MS::BTN_UP_W, MS::BTN_UP_H) && same(animation_bounds(arc, "up2"), MS::BTN_UP_X, MS::BTN_UP_Y, MS::BTN_UP_W, MS::BTN_UP_H),
              "setup: up button rect = up1 = up2");
        check(same_pressed(animation_bounds(arc, "up3"), MS::BTN_UP_PRESSED), "setup: up button pressed rect = up3");
        check(same(animation_bounds(arc, "down1"), MS::BTN_DOWN_X, MS::BTN_DOWN_Y, MS::BTN_DOWN_W, MS::BTN_DOWN_H) && same(animation_bounds(arc, "down2"), MS::BTN_DOWN_X, MS::BTN_DOWN_Y, MS::BTN_DOWN_W, MS::BTN_DOWN_H),
              "setup: down button rect = down1 = down2");
        check(same_pressed(animation_bounds(arc, "down3"), MS::BTN_DOWN_PRESSED), "setup: down button pressed rect = down3");
        check(same(animation_bounds(arc, "start1"), MS::BTN_START_X, MS::BTN_START_Y, MS::BTN_START_W, MS::BTN_START_H) && same(animation_bounds(arc, "start2"), MS::BTN_START_X, MS::BTN_START_Y, MS::BTN_START_W, MS::BTN_START_H),
              "setup: start button rect = start1 = start2 = (526, 439, 98, 27)");
        check(same_pressed(animation_bounds(arc, "start3"), MS::BTN_START_PRESSED) && MS::BTN_START_PRESSED == ButtonRect{527, 443, 97, 24}, "setup: start button pressed rect = start3 = (527, 443, 97, 24)");
        check(same(animation_bounds(arc, "leave1"), MS::BTN_QUIT_X, MS::BTN_QUIT_Y, MS::BTN_QUIT_W, MS::BTN_QUIT_H) && same(animation_bounds(arc, "leave2"), MS::BTN_QUIT_X, MS::BTN_QUIT_Y, MS::BTN_QUIT_W, MS::BTN_QUIT_H),
              "setup: leave button rect = leave1 = leave2");
        check(same_pressed(animation_bounds(arc, "leave3"), MS::BTN_QUIT_PRESSED), "setup: leave button pressed rect = leave3");
        check(same(union_of("d_on1", "d_on3"), MS::BTN_FOW_ON_X, MS::BTN_FOW_ON_Y, MS::BTN_FOW_ON_W, MS::BTN_FOW_ON_H), "setup: Fog of War ON rect");
        check(same(union_of("d_off1", "d_off3"), MS::BTN_FOW_OFF_X, MS::BTN_FOW_OFF_Y, MS::BTN_FOW_OFF_W, MS::BTN_FOW_OFF_H), "setup: Fog of War OFF rect");

        MapSelectScreen screen;
        screen.init();
        RecordingRenderer early(arc);
        screen.render(early, arc);                                                  // before the refresh (500 ms): the buttons are there, nothing else
        check(early.has_sprite_at("bstart1.bmp", 526, 439) && early.texts.empty() && early.named("thumb1.bmp").empty(),
              "setup: before the first refresh (500 ms) the buttons show, the labels, the portrait and the thumb do not");
        screen.update(0.5f);
        RecordingRenderer rr(arc);
        screen.render(rr, arc);
        check(rr.has_sprite_at("bstart1.bmp", 526, 439), "setup: START up art at (526,439)");
        check(rr.has_sprite_at("up1.bmp", 226, 299) && rr.has_sprite_at("down1.bmp", 226, 323), "setup: up (226,299) and down (226,323)");
        check(rr.has_sprite_at("bleave1.bmp", 525, 12), "setup: leave at (525,12)");
        check(rr.has_sprite_at("dbutonu.bmp", 524, 373) && rr.has_sprite_at("optoffd.bmp", 574, 372), "setup: Fog of War off shows ON up (524,373) and OFF down (574,372)");
        check(rr.has_sprite_at("thumb1.bmp", 540, 95), "setup: thumbs-up at (540,95)");
        bool portrait_ok = false;
        for (const auto& sp : rr.sprites) {
            if (sp.name.rfind("agst30", 0) == 0 && sp.x >= 383 && sp.x <= 385 && sp.y >= 85 && sp.y <= 87) portrait_ok = true;
        }
        check(portrait_ok, "setup: portrait agst30x drawn from its origin (395,115) with the part offset");
        click(screen, MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2);
        RecordingRenderer on(arc);
        screen.render(on, arc);
        check(on.has_sprite_at("optond.bmp", 522, 372) && on.has_sprite_at("dbutoffu.bmp", 576, 373), "setup: Fog of War on shows ON down (522,372) and OFF up (576,373)");
        screen.handle_mouse_motion(MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        RecordingRenderer hov(arc);
        screen.render(hov, arc);
        check(hov.has_sprite_at("bstart2.bmp", 526, 439), "setup: START hover art");
        screen.handle_mouse_down(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3, 1);
        RecordingRenderer prs(arc);
        screen.render(prs, arc);
        check(prs.has_sprite_at("up3.bmp", 224, 301), "setup: up pressed art at (224,301)");
    }
}

MapSelectScreen::RoomView view_of_host_for_art() {
    MapSelectScreen::RoomView v;
    v.networked = true;
    v.is_host = true;
    v.my_seat = 0;
    v.seats[0] = {true, "Host", MapSelectScreen::Thumb::Good};
    return v;
}

// A click of the original's button class on the setup screen: the pointer moves onto the button, the left button goes down and up
void click(MapSelectScreen& screen, int32_t x, int32_t y) {
    screen.handle_mouse_motion(x, y);
    screen.handle_mouse_down(x, y, 1);
    screen.handle_mouse_up(x, y, 1);
}

// The setup screen as a network room: a row per occupied seat (portrait, name, the thumb of the connection quality), the status line, and the
// host-only controls (the map, the fog option and START belong to the host; every player can leave).
void test_room_screen(const assets::AssetArchive& arc) {
    std::printf("[room] names with a thumb per seat (netgood / netok / netbad / netunk), the status line, host-only controls\n");
    using MS = MapSelectScreen;
    MS screen;
    screen.init();
    screen.update(0.5f);                                                            // the refresh has run: labels, portraits and thumbs are shown
    MS::RoomView view;
    view.networked = true;
    view.is_host = true;
    view.my_seat = 0;
    view.seats[0] = {true, "Alice", MS::Thumb::Good};
    view.seats[1] = {true, "Bob", MS::Thumb::Ok};
    view.seats[3] = {true, "A Very Long Player Name", MS::Thumb::Unknown};      // seat 2 is empty
    view.status = "Press START when all players' thumbs have appeared.";
    screen.set_room(view);
    RecordingRenderer rr(arc);
    screen.render(rr, arc);
    check(rr.has_sprite_at("thumb1.bmp", 540, 95), "room: seat 0 shows the good thumb at (540,95)");
    check(rr.has_sprite_at("thumb2.bmp", 540, 145), "room: seat 1 shows the ok thumb at (540,145)");
    bool row2 = false;
    for (const auto& sp : rr.sprites) row2 = row2 || (sp.x == 540 && sp.y == 195 && sp.name.rfind("thumb", 0) == 0);
    check(!row2, "room: an empty seat shows no thumb");
    check(rr.has_sprite_at("thumb4.bmp", 540, 245), "room: seat 3 shows the question mark at (540,245)");
    auto text_at = [&](const std::string& t, int32_t x) {
        for (const auto& tx : rr.texts) {
            if (tx.text == t && tx.x == x) return true;
        }
        return false;
    };
    check(text_at("Alice", 415) && text_at("Bob", 415), "room: the names are drawn at x = 415");
    check(text_at("A Very Long Pla", 415) || text_at("A Very Long Play", 415), "room: a long name is cut to fit before the thumb");
    {
        // the prompt is the label (36, 447) 293 wide with 14 px lines: it wraps like every label (the recorder measures 6 px per character)
        const auto lines = wrap_label_text(rr, "Press START when all players' thumbs have appeared.", MS::STATUS_W, FontSize::Px14);
        bool ok = lines.size() == 2;
        for (size_t i = 0; i < lines.size(); ++i) {
            bool found = false;
            for (const auto& tx : rr.texts) found = found || (tx.text == lines[i] && tx.x == 36 && tx.y == 447 + static_cast<int32_t>(i) * 14 && tx.size == FontSize::Px14);
            ok = ok && found;
        }
        check(ok, "room: the status line is the original's prompt, wrapped in its label at (36, 447)");
    }
    int portraits = 0;
    for (const auto& sp : rr.sprites) portraits += sp.name.rfind("agst30", 0) == 0 ? 1 : 0;
    check(portraits >= 3, "room: a portrait for each occupied seat");
    // every quality has its own thumb
    for (const auto& [quality, name] : {std::pair{MS::Thumb::Good, "thumb1.bmp"}, std::pair{MS::Thumb::Ok, "thumb2.bmp"},
                                         std::pair{MS::Thumb::Bad, "thumb3.bmp"}, std::pair{MS::Thumb::Unknown, "thumb4.bmp"}}) {
        MS::RoomView v = view;
        v.seats[1].thumb = quality;
        screen.set_room(v);
        RecordingRenderer r(arc);
        screen.render(r, arc);
        check(r.has_sprite_at(name, 540, 145), std::string("room: quality ") + name + " at (540,145)");
    }

    // host controls: the map and the fog option go to the room, START starts, LEAVE quits
    screen.set_room(view);
    std::vector<std::string> maps_chosen;
    std::vector<bool> fog_chosen;
    int started = 0;
    int left = 0;
    screen.set_on_map_changed([&](const std::string& f) { maps_chosen.push_back(f); });
    screen.set_on_fog_changed([&](bool on) { fog_chosen.push_back(on); });
    screen.set_on_start([&](const std::string&) { ++started; });
    screen.set_on_quit([&]() { ++left; });
    const int32_t before = screen.get_selected_index();
    click(screen, MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3);
    check(screen.get_selected_index() != before && maps_chosen.size() == 1, "room host: the down button changes the map and tells the room");
    check(maps_chosen[0] == screen.get_maps()[static_cast<size_t>(screen.get_selected_index())].filename, "room host: the room gets the file name of the map");
    click(screen, MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2);
    click(screen, MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2);                         // the same choice again changes nothing
    check(fog_chosen.size() == 1 && fog_chosen[0], "room host: the fog option is reported once per change");
    click(screen, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
    check(started == 1, "room host: START starts");
    click(screen, MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
    check(left == 1, "room host: LEAVE quits");

    // a guest cannot change anything but can leave; it follows the host without any callback
    MS::RoomView guest = view;
    guest.is_host = false;
    guest.my_seat = 1;
    screen.set_room(guest);
    maps_chosen.clear();
    fog_chosen.clear();
    started = 0;
    left = 0;
    const int32_t idx = screen.get_selected_index();
    const bool fog_before = screen.is_fog_of_war_enabled();
    click(screen, MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3);
    click(screen, MS::BTN_FOW_OFF_X + 2, MS::BTN_FOW_OFF_Y + 2);
    click(screen, MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2);
    click(screen, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
    screen.handle_key_down(SDLK_DOWN);
    screen.handle_key_down(SDLK_s);
    screen.handle_key_down(SDLK_RETURN);
    check(screen.get_selected_index() == idx && screen.is_fog_of_war_enabled() == fog_before, "room guest: the map and the fog option cannot be changed");
    check(started == 0 && maps_chosen.empty() && fog_chosen.empty(), "room guest: START does nothing and nothing is reported");
    click(screen, MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
    check(left == 1, "room guest: LEAVE quits");
    screen.handle_key_down(SDLK_ESCAPE);
    check(left == 1, "room guest: Esc does nothing");
    screen.handle_key_down(SDLK_q);
    screen.handle_key_down(SDLK_x);
    check(left == 3, "room guest: Q and X leave");
    check(screen.follow_host_choice("SMALL.LVL", true) && screen.get_maps()[static_cast<size_t>(screen.get_selected_index())].filename == "SMALL.LVL" &&
              screen.is_fog_of_war_enabled() && maps_chosen.empty() && fog_chosen.empty(),
          "room guest: follows the host's map and fog without telling anybody");
    check(!screen.follow_host_choice("NOSUCH.LVL", false), "room guest: an unknown map name is refused");

    // not networked: the local screen keeps its single row and its placeholders
    MS local;
    local.init();
    local.update(0.5f);
    RecordingRenderer lr(arc);
    local.render(lr, arc);
    check(lr.has_sprite_at("thumb1.bmp", 540, 95) && lr.named("thumb2.bmp").empty(), "local setup screen: one row with the good thumb");
    int local_maps = 0;
    local.set_on_map_changed([&](const std::string&) { ++local_maps; });
    click(local, MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3);
    check(local_maps == 0, "local setup screen: no room to tell about a map change");

    // ---- the rows (FUN_010133ef: row i is slot i of the machine's own table, the local machine is slot 0) ----------------------------------------------------------------------------------
    {
        auto view_of = [](bool host, uint8_t my, std::initializer_list<uint8_t> occupied) {
            MS::RoomView v;
            v.networked = true;
            v.is_host = host;
            v.my_seat = my;
            for (uint8_t s : occupied) v.seats[s] = {true, std::string("P") + std::to_string(s), MS::Thumb::Ok};
            return v;
        };
        using Rows = std::array<int8_t, 4>;
        check(MS::row_seats(view_of(true, 0, {0, 1, 3})) == Rows{0, 1, -1, 3}, "rows: a host shows the seats in their places, a seat that is not taken leaves its row blank");
        check(MS::row_seats(view_of(false, 1, {0, 1, 3})) == Rows{1, 0, 3, -1}, "rows: a guest (seat 1) shows itself first, then the host (seat 0), then the others, compact");
        check(MS::row_seats(view_of(false, 3, {0, 1, 2, 3})) == Rows{3, 0, 1, 2}, "rows: a guest in seat 3 of four: itself, then the seats 0, 1, 2");
        check(MS::row_seats(view_of(false, 2, {0, 2})) == Rows{2, 0, -1, -1}, "rows: a guest and the host: [me, host]");
        check(MS::row_seats(view_of(false, 255, {0, 1, 3})) == Rows{0, 1, 3, -1}, "rows: a guest without a seat shows the occupied seats in ascending order");
        check(MS::row_seats(view_of(false, 2, {0, 1})) == Rows{0, 1, -1, -1}, "rows: a guest whose own seat is not in the room (yet) shows the others");
        {
            MS::RoomView solo;                                                       // the local screen is not a room: the seats stay where they are
            solo.seats[0] = {true, "Me", MS::Thumb::Good};
            check(MS::row_seats(solo) == Rows{0, -1, -1, -1}, "rows: the local screen shows its one row");
        }
    }

    // ---- the GUEST screen (FUN_01014228): another composite, no Up / Down / START / Fog buttons, the fixed "Fog of War?" box, only Leave --------------------------------------------------
    {
        MS g;
        g.init();
        MS::RoomView v;
        v.networked = true;
        v.is_host = false;
        v.my_seat = 2;
        const char* const names[4] = {"Alice", "Bob", "Cara", "Dave"};
        for (size_t i = 0; i < 4; ++i) v.seats[i] = {true, names[i], MS::Thumb::Ok};
        v.seats[2].thumb = MS::Thumb::Bad;                                          // the local row is always the good thumb
        v.map_file = "SMALL.LVL";
        g.set_room(v);
        RecordingRenderer early(arc);
        g.render(early, arc);                                                       // before the first refresh: the art and Leave only
        check(early.has_sprite_at("nhbanr.bmp", 140, 0) && early.has_sprite_at("bleave1.bmp", 525, 12), "guest: before the refresh the guest art and Leave show");
        check(early.texts.empty() && early.named("fowyes.bmp").empty(), "guest: no label and no fog overlay before the first refresh");
        g.update(0.5f);
        RecordingRenderer gr(arc);
        g.render(gr, arc);
        check(gr.has_sprite_at("nhbanr.bmp", 140, 0) && gr.has_sprite_at("waiting.bmp", 20, 39) && gr.has_sprite_at("nhmap.bmp", 25, 278), "guest: the banner 'Game Set-Up', the 'WAITING FOR GAME TO START!' art and the 'Map' caption");
        check(gr.has_sprite_at("fowno.bmp", 529, 361) && gr.has_sprite_at("Q_mark.bmp", 511, 372), "guest: the fixed 'Fog of War?' box shows No");
        check(gr.has_sprite_at("bleave1.bmp", 525, 12), "guest: Leave is the one button");
        bool host_art = false;
        for (const char* n : {"hostbanr.bmp", "gamesetup.bmp", "pickmap.bmp", "up1.bmp", "down1.bmp", "bstart1.bmp", "dbutonu.bmp", "dbutoffu.bmp", "optoffd.bmp", "optond.bmp"}) host_art = host_art || !gr.named(n).empty();
        check(!host_art, "guest: none of the host's art or buttons (banner, 'GAME SET UP!', 'Pick a Map', Up, Down, START, Fog On / Off)");
        check(gr.named("fowyes.bmp").empty(), "guest: fog off = no 'Yes' over the box");
        // the rows: the guest itself first (the colour follows the player: seat 2), then the others in ascending seat order
        auto portrait_team = [&](int32_t row_y) -> int {
            for (const auto& sp : gr.sprites) {
                if (sp.name.rfind("agst30", 0) == 0 && sp.x >= 383 && sp.x <= 385 && sp.y >= 85 + row_y && sp.y <= 89 + row_y) return sp.team;      // the part offsets move with the frame
            }
            return -1;
        };
        check(portrait_team(0) == 2 && portrait_team(50) == 0 && portrait_team(100) == 1 && portrait_team(150) == 3, "guest: the portraits of the rows are seats 2, 0, 1, 3 (each in its own colour)");
        auto text_row = [&](const char* t, int32_t y) {
            for (const auto& tx : gr.texts) {
                if (tx.text == t && tx.x == 415 && tx.y == y) return true;
            }
            return false;
        };
        check(text_row("Cara", 95) && text_row("Alice", 145) && text_row("Bob", 195) && text_row("Dave", 245), "guest: the names are Cara, Alice, Bob, Dave from the top (its own name first)");
        check(gr.has_sprite_at("thumb1.bmp", 540, 95) && gr.has_sprite_at("thumb2.bmp", 540, 145) && gr.has_sprite_at("thumb2.bmp", 540, 195), "guest: the local row has the good thumb whatever its measured quality, the others their own");
        // the map labels: the host's choice, the description from the guest's own copy of the file
        bool name_label = false, info_label = false;
        for (const auto& tx : gr.texts) {
            if (tx.text == "SMALL" && tx.x == 36 && tx.y == 312) name_label = true;
            if (tx.x == 36 && tx.y == 380 && tx.text != "???") info_label = true;
        }
        check(name_label && info_label, "guest: the map name without its extension at (36, 312) and the description of its own copy at (36, 380)");
        // keys and mouse: Leave only
        int guest_left = 0;
        int guest_started = 0;
        g.set_on_quit([&]() { ++guest_left; });
        g.set_on_start([&](const std::string&) { ++guest_started; });
        g.handle_mouse_motion(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3);
        RecordingRenderer hov(arc);
        g.render(hov, arc);
        check(hov.named("up2.bmp").empty() && hov.named("up1.bmp").empty(), "guest: nothing to hover where the host's Up button is");
        click(g, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        check(!g.start_button().hovered() && !g.start_button().pressed() && !g.up_button().hovered() && !g.down_button().hovered(), "guest: the host's buttons have no pointer state after a click on their places");
        g.handle_key_down(SDLK_RETURN);
        g.handle_key_down(SDLK_s);
        g.handle_key_down(SDLK_UP);
        g.handle_key_down(SDLK_DOWN);
        g.handle_key_down(SDLK_ESCAPE);
        check(guest_started == 0 && guest_left == 0, "guest: START, Enter, S, the arrows and Esc do nothing");
        g.handle_key_down(SDLK_q);
        g.handle_key_down(SDLK_x);
        click(g, MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
        check(guest_left == 3, "guest: Q, X and the Leave button leave");
        // fog on: the 'Yes' overlay over the box; a map the guest does not have: the host's name and '???'
        check(g.follow_host_choice("SMALL.LVL", true), "guest: follows the host's fog choice");
        RecordingRenderer fog(arc);
        g.render(fog, arc);
        check(fog.has_sprite_at("fowyes.bmp", 534, 366), "guest: fog on shows 'Yes' at (534, 366)");
        {
            MS fresh;
            fresh.init();
            fresh.set_room(v);
            fresh.follow_host_choice("SMALL.LVL", true);
            RecordingRenderer before_refresh(arc);
            fresh.render(before_refresh, arc);                                              // no update yet: the 'Yes' overlay is put on by the first refresh, 500 ms after the screen
            check(before_refresh.named("fowyes.bmp").empty(), "guest: fog on, but the 'Yes' overlay waits for the first refresh");
        }
        v.map_file = "NOSUCH.LVL";
        g.set_room(v);
        RecordingRenderer missing(arc);
        g.render(missing, arc);
        bool shown_name = false, unknown = false;
        for (const auto& tx : missing.texts) {
            if (tx.text == "NOSUCH" && tx.x == 36 && tx.y == 312) shown_name = true;
            if (tx.text == "???" && tx.x == 36 && tx.y == 380) unknown = true;
        }
        check(shown_name && unknown, "guest: a map that the guest does not have: the host's name and the question marks for the description");
        v.map_file.clear();
        g.set_room(v);
        RecordingRenderer none(arc);
        g.render(none, arc);
        bool any_name = false, still_unknown = false;
        for (const auto& tx : none.texts) {
            if (tx.x == 36 && tx.y == 312) any_name = true;
            if (tx.text == "???" && tx.x == 36 && tx.y == 380) still_unknown = true;
        }
        check(!any_name && still_unknown, "guest: before the host's first message the name is empty and the description is the question marks");

        // the host's screen and the local screen keep the host art
        MS h;
        h.init();
        h.update(0.5f);
        MS::RoomView hv = view_of_host_for_art();
        h.set_room(hv);
        RecordingRenderer hr(arc);
        h.render(hr, arc);
        check(hr.has_sprite_at("hostbanr.bmp", 140, 0) && hr.has_sprite_at("bstart1.bmp", 526, 439) && hr.named("nhbanr.bmp").empty() && hr.named("fowyes.bmp").empty(), "host: the host screen keeps its art and its controls");
    }
}

// The LEADER's screen: the first player of a dedicated server's room (protocol 7) has the HOST's composite and buttons (st_screen) in a "server room" mode: the room's map and fog
// are shown and cannot be changed, START asks the server to start, Leave works; the rows are the guest's (itself first)
void test_leader_screen(const assets::AssetArchive& arc) {
    std::printf("[leader] the first player of a server's room: the host's screen with START, the room's setup shown and not changeable, the guest's rows\n");
    using MS = MapSelectScreen;
    {
        MS l;
        l.init();
        MS::RoomView v;
        v.networked = true;
        v.is_host = false;
        v.leader = true;
        v.my_seat = 2;
        const char* const names[4] = {"Alice", "Bob", "Cara", "Dave"};
        for (size_t i = 0; i < 4; ++i) v.seats[i] = {true, names[i], MS::Thumb::Ok};
        v.seats[2].thumb = MS::Thumb::Bad;                                          // the local row is always the good thumb
        v.map_file = "SMALL.LVL";
        v.status = "Press START when all players' thumbs have appeared.";
        l.set_room(v);
        check(!l.is_guest() && l.leads_server_room() && l.has_start_button() && !l.can_change_setup(), "leader: not a guest, leads a server's room, has START, cannot change the setup");
        {   // the same questions for every kind of screen
            MS local;
            check(!local.is_guest() && !local.leads_server_room() && local.has_start_button() && local.can_change_setup(), "local screen: START, the setup is its own");
            MS host;
            MS::RoomView hv = view_of_host_for_art();
            host.set_room(hv);
            check(!host.is_guest() && !host.leads_server_room() && host.has_start_button() && host.can_change_setup(), "host screen: START, the setup is its own");
            MS guest;
            MS::RoomView gv = v;
            gv.leader = false;
            guest.set_room(gv);
            check(guest.is_guest() && !guest.leads_server_room() && !guest.has_start_button() && !guest.can_change_setup(), "guest screen: no START, no setup");
            MS::RoomView both = v;
            both.is_host = true;                                                    // (a host that is also marked leader is a host: the leader flag is for guests of a server's room)
            guest.set_room(both);
            check(!guest.is_guest() && !guest.leads_server_room() && guest.has_start_button() && guest.can_change_setup(), "a host with the leader flag is still a host");
        }
        l.follow_host_choice("SMALL.LVL", true);                                    // what the application does for a leader, as it does for a guest
        RecordingRenderer early(arc);
        l.render(early, arc);                                                       // before the first refresh: the host's art and buttons, no label
        check(early.has_sprite_at("hostbanr.bmp", 140, 0) && early.has_sprite_at("bstart1.bmp", 526, 439) && early.has_sprite_at("bleave1.bmp", 525, 12), "leader: before the refresh the host's art and its buttons show");
        check(early.texts.empty() && early.named("thumb1.bmp").empty(), "leader: no label, no portrait and no thumb before the first refresh");
        l.update(0.5f);
        RecordingRenderer lr(arc);
        l.render(lr, arc);
        check(lr.has_sprite_at("hostbanr.bmp", 140, 0) && lr.has_sprite_at("bstart1.bmp", 526, 439) && lr.has_sprite_at("up1.bmp", 226, 299) && lr.has_sprite_at("down1.bmp", 226, 323) &&
                  lr.has_sprite_at("bleave1.bmp", 525, 12),
              "leader: the host's screen: banner, START, Up, Down and Leave");
        bool guest_art = false;
        for (const char* n : {"nhbanr.bmp", "waiting.bmp", "nhmap.bmp", "fowno.bmp", "Q_mark.bmp", "fowyes.bmp"}) guest_art = guest_art || !lr.named(n).empty();
        check(!guest_art, "leader: none of the guest's art (banner, 'WAITING FOR GAME TO START!', the fixed Fog box)");
        check(lr.has_sprite_at("optond.bmp", 522, 372) && lr.has_sprite_at("dbutoffu.bmp", 576, 373), "leader: the Fog buttons show the room's choice (on): ON down, OFF up");
        // the rows are the guest's: itself first (seat 2, the good thumb), then the others in ascending order
        {
            auto text_row = [&](const char* t, int32_t y) {
                for (const auto& tx : lr.texts) {
                    if (tx.text == t && tx.x == 415 && tx.y == y) return true;
                }
                return false;
            };
            check(text_row("Cara", 95) && text_row("Alice", 145) && text_row("Bob", 195) && text_row("Dave", 245), "leader: the names are Cara, Alice, Bob, Dave from the top (its own name first)");
            check(lr.has_sprite_at("thumb1.bmp", 540, 95) && lr.has_sprite_at("thumb2.bmp", 540, 145), "leader: the local row has the good thumb, the others their own");
            check(MS::row_seats(v) == std::array<int8_t, 4>{2, 0, 1, 3}, "leader: row_seats = itself first, then the others");
        }
        // the labels: the room's map (the file name of the room, the description from the leader's own copy), '???' when this machine has no such file
        {
            bool name_label = false, info_label = false, prompt = false;
            for (const auto& tx : lr.texts) {
                if (tx.text == "SMALL" && tx.x == 36 && tx.y == 312) name_label = true;
                if (tx.x == 36 && tx.y == 380 && tx.text != "???") info_label = true;
                if (tx.x == 36 && tx.y == 447 && tx.size == FontSize::Px14) prompt = true;
            }
            check(name_label && info_label, "leader: the room's map name at (36, 312) and the description of its own copy at (36, 380)");
            check(prompt, "leader: the prompt (the original's) at (36, 447)");
            MS::RoomView missing = v;
            missing.map_file = "NOSUCH.LVL";
            l.set_room(missing);
            RecordingRenderer mr(arc);
            l.render(mr, arc);
            bool shown_name = false, unknown = false;
            for (const auto& tx : mr.texts) {
                if (tx.text == "NOSUCH" && tx.x == 36 && tx.y == 312) shown_name = true;
                if (tx.text == "???" && tx.x == 36 && tx.y == 380) unknown = true;
            }
            check(shown_name && unknown, "leader: a map that this machine does not have: the room's name and the question marks (not another map of the list)");
            l.set_room(v);
        }
        // the controls: Up, Down, the wrap and Fog change nothing and tell nobody; START, Enter, the keypad's Enter, S and s ask; the room is the room's
        int asked = 0, started = 0, left = 0;
        std::vector<std::string> maps_chosen;
        std::vector<bool> fog_chosen;
        l.set_on_request_start([&]() { ++asked; });
        l.set_on_start([&](const std::string&) { ++started; });
        l.set_on_quit([&]() { ++left; });
        l.set_on_map_changed([&](const std::string& f) { maps_chosen.push_back(f); });
        l.set_on_fog_changed([&](bool on) { fog_chosen.push_back(on); });
        const int32_t idx = l.get_selected_index();
        click(l, MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3);
        click(l, MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3);
        click(l, MS::BTN_FOW_OFF_X + 2, MS::BTN_FOW_OFF_Y + 2);
        for (int i = 0; i < 14; ++i) {                                              // (more than the six maps: the wrap changes nothing either)
            l.handle_key_down(SDLK_DOWN);
            l.handle_key_down(SDLK_UP);
        }
        l.handle_key_down(SDLK_UP);
        l.set_selected_index(idx + 1);
        check(l.get_selected_index() == idx && l.is_fog_of_war_enabled(), "leader: Up, Down, the wrap and the Fog buttons change neither the map nor the fog option");
        check(maps_chosen.empty() && fog_chosen.empty() && asked == 0 && started == 0, "leader: and tell nobody; nothing starts");
        // the buttons answer the pointer as the host's do (the same pictures), they only do nothing
        l.handle_mouse_motion(MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        RecordingRenderer hov(arc);
        l.render(hov, arc);
        check(hov.has_sprite_at("bstart2.bmp", 526, 439), "leader: START shows its hover picture");
        l.handle_mouse_down(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3, 1);
        RecordingRenderer prs(arc);
        l.render(prs, arc);
        check(prs.has_sprite_at("up3.bmp", 224, 301), "leader: Up shows its pressed picture while it is held");
        l.handle_mouse_up(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3, 1);
        // START
        click(l, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        check(asked == 1 && started == 0, "leader: the START button asks the server (and does not start by itself)");
        l.handle_key_down(SDLK_RETURN);
        check(asked == 2, "leader: Enter asks");
        l.handle_key_down(SDLK_KP_ENTER);
        check(asked == 3, "leader: the keypad's Enter asks");
        l.handle_key_down(SDLK_s);
        check(asked == 4, "leader: S and s ask (the key is 's' whatever the shift)");
        l.handle_key_down(SDLK_ESCAPE);
        l.handle_key_down(SDLK_x + 1000);
        check(asked == 4 && started == 0 && left == 0, "leader: every other key does nothing (Esc included)");
        l.lock();                                                                   // the server's Start arrived: the screen is locked, nothing more to ask
        click(l, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        l.handle_key_down(SDLK_RETURN);
        l.handle_key_down(SDLK_s);
        check(asked == 4, "leader: a locked screen asks nobody (a second click on START while the match loads)");
        l.unlock();                                                                 // the start was cancelled: the leader may ask again
        l.handle_key_down(SDLK_s);
        check(asked == 5, "leader: after a cancelled start START asks again");
        // Leave
        click(l, MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
        l.handle_key_down(SDLK_q);
        l.handle_key_down(SDLK_x);
        check(left == 3, "leader: the Leave button, Q and X leave");
        l.lock();
        click(l, MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
        check(left == 4, "leader: Leave works on a locked screen too (every player can leave)");
        // a leader screen that nobody gave a request callback: START is harmless
        MS bare;
        bare.init();
        bare.set_room(v);
        bare.handle_key_down(SDLK_RETURN);
        click(bare, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        check(!bare.is_locked(), "leader: START without a callback does nothing at all");
    }
}

// The original's button class tests the rectangle of the picture that shows (docs 5.52): the START! buttons of the quick help and of the setup screen, the zones
// that follow from FUN_010112e9 / FUN_01011281 / FUN_01011206, and the plate that the remake draws in the corner
void test_button_zones(const assets::AssetArchive& arc) {
    std::printf("[zones] the START buttons: positions in each state, the hit rectangle of the picture that shows, dead strips, the corner plate\n");
    using MS = MapSelectScreen;
    auto rect = [&](const char* n) { const UIRect r = animation_bounds(arc, n); return ButtonRect{r.x, r.y, r.w, r.h}; };
    const ButtonRect qh_up{529, 437, 98, 27};
    const ButtonRect qh_down{528, 438, 97, 24};
    const ButtonRect st_up{526, 439, 98, 27};
    const ButtonRect st_down{527, 443, 97, 24};
    check(rect("qh_start1") == qh_up && rect("qh_start2") == qh_up && rect("qh_start3") == qh_down, "quick help START pictures: qh_start1 / 2 (529, 437) 98 x 27, qh_start3 (528, 438) 97 x 24");
    check(rect("start1") == st_up && rect("start2") == st_up && rect("start3") == st_down, "setup START pictures: start1 / 2 (526, 439) 98 x 27, start3 (527, 443) 97 x 24");
    // the shift between the two screens is authored in the CHD (both buttons are built at (0, 0), both windows are (0, 0, 640, 480)): the original moves it too
    check(st_up.x - qh_up.x == -3 && st_up.y - qh_up.y == 2 && st_down.x - qh_down.x == -1 && st_down.y - qh_down.y == 5,
          "the setup START sits (-3, +2) from the quick help START (up, hover) and (-1, +5) (pressed): as in the original");
    check(MS::BTN_START_X == st_up.x && MS::BTN_START_Y == st_up.y && MS::BTN_START_W == st_up.w && MS::BTN_START_H == st_up.h && MS::BTN_START_PRESSED == st_down, "the screen's constants are those pictures");

    // click zones: the press must hit the up / hover picture, every move until the release must stay on the pressed picture
    auto make = [](ButtonRect up, ButtonRect down) { return ScreenButton(up, down); };
    auto fires = [](ScreenButton b, int32_t x, int32_t y) {
        b.on_move(x, y);
        b.on_press(x, y);
        return b.on_release(x, y);
    };
    auto shows_pressed = [](ScreenButton b, int32_t x, int32_t y) {
        b.on_move(x, y);
        b.on_press(x, y);
        return b.pressed();
    };
    auto sounds = [](ScreenButton b, int32_t x, int32_t y) {      // the press that captured (the pressed animation starts, its sound plays)
        b.on_move(x, y);
        return b.on_press(x, y);
    };
    const ScreenButton qh = make(qh_up, qh_down);
    check(fires(qh, 529, 438) && fires(qh, 624, 461) && fires(qh, 580, 450), "quick help START: fires in [529, 625) x [438, 462)");
    check(!fires(qh, 528, 450) && !sounds(qh, 528, 450), "quick help START: x = 528 is on the pressed picture only: no capture, no sound");
    check(!fires(qh, 529, 437) && sounds(qh, 529, 437) && !shows_pressed(qh, 529, 437), "quick help START: y = 437 is on the up picture only: the press is cancelled at once (the pressed picture never shows)");
    check(!fires(qh, 625, 450) && !fires(qh, 529, 462) && !fires(qh, 626, 463) && !fires(qh, 627, 450), "quick help START: x = 625, 626, y = 462, 463 and x = 627 fire nothing");
    const ScreenButton st = make(st_up, st_down);
    check(fires(st, 527, 443) && fires(st, 623, 465) && fires(st, 580, 450), "setup START: fires in [527, 624) x [443, 466)");
    check(!fires(st, 623, 466) && !sounds(st, 623, 466), "setup START: y = 466 is on the pressed picture only: no capture");
    check(!fires(st, 526, 450) && !fires(st, 531, 440) && !fires(st, 527, 442) && !fires(st, 527, 439), "setup START: x = 526 and y = 439 .. 442 are on the up picture only: nothing fires");
    check(!fires(st, 624, 450), "setup START: x = 624 is outside both pictures");
    // the hover picture at rest: the up rectangle (the pointer on the pressed-only strip does not hover)
    {
        ScreenButton b = st;
        b.on_move(530, 440);
        check(b.hovered() && !b.pressed(), "a pointer on the up picture hovers");
        b.on_move(623, 466);
        check(!b.hovered(), "a pointer below the up picture does not");
        b.on_move(530, 450);
        b.on_press(530, 450);
        check(b.pressed() && b.hovered(), "a captured button is pressed and hovered");
        b.on_move(526, 450);                                     // x = 526 is on the up picture only: the pressed picture does not contain it any more: the capture is gone
        check(!b.pressed() && !b.hovered(), "a move onto the up-only strip while captured cancels the capture for good");
        b.on_move(560, 450);
        check(b.hovered() && !b.pressed(), "coming back only hovers");
    }
    // the corner plate (frame rate, sparkline, version) starts below the pressed START of the setup screen and ends at the last row
    check(FPS_OVERLAY_TOP >= st_down.y + st_down.h && FPS_OVERLAY_TOP >= qh_up.y + qh_up.h, "the corner plate starts below both START buttons, pressed or not");
    check(FPS_OVERLAY_BOTTOM <= 480 && FPS_OVERLAY_SPARK_Y + font_cell_height(FontSize::Px12) <= 480, "and the plate and the 12 px text end inside the 480 rows");
}

// The same rule for the other buttons of the class that the release converted: the setup screen's Up, Down and Leave, the results screen's Leave, the options
// screen's Return; and the host's screen after a cancelled start
void test_more_button_zones(const assets::AssetArchive& arc) {
    std::printf("[zones] Up, Down, Leave (setup and results), Return (options): click zones; unlock after a cancelled start\n");
    using MS = MapSelectScreen;
    // setup Up: up1 [226, 272) x [299, 321), up3 [224, 271) x [301, 322): the zone is [226, 271) x [301, 321)
    {
        MS screen;
        screen.init();
        const int32_t n = static_cast<int32_t>(screen.get_maps().size());
        auto steps = [&](int32_t x, int32_t y) {
            const int32_t before = screen.get_selected_index();
            click(screen, x, y);
            return screen.get_selected_index() != before;
        };
        check(n >= 2, "setup zones: the map list has two maps or more (the six of the original)");
        check(steps(230, 305) && steps(226, 301) && steps(270, 320), "setup Up: fires in [226, 271) x [301, 321)");
        check(!steps(225, 305), "setup Up: x = 225 is on the pressed picture only: no capture");
        check(!steps(230, 300) && !steps(271, 305), "setup Up: y = 300 and x = 271 are on the resting picture only: captured, then cancelled");
        // setup Down: down1 [226, 273) x [323, 343), down3 [225, 272) x [324, 345): the zone is [226, 272) x [324, 343)
        check(steps(230, 330) && steps(226, 324) && steps(271, 342), "setup Down: fires in [226, 272) x [324, 343)");
        check(!steps(225, 330) && !steps(272, 330) && !steps(230, 323) && !steps(230, 343), "setup Down: x = 225, 272 and y = 323, 343 fire nothing");
        // setup Leave: leave1 [525, 624) x [12, 34), leave3 [524, 622) x [14, 34): the zone is [525, 622) x [14, 34)
        int left = 0;
        screen.set_on_quit([&]() { ++left; });
        click(screen, 530, 20);
        click(screen, 525, 14);
        click(screen, 621, 33);
        check(left == 3, "setup Leave: fires in [525, 622) x [14, 34)");
        click(screen, 524, 20);
        click(screen, 530, 12);
        click(screen, 623, 20);
        click(screen, 530, 34);
        check(left == 3, "setup Leave: x = 524, y = 12, x = 623 and y = 34 fire nothing");
    }
    // results Leave: the same pictures
    {
        ScorecardModal card;
        sim::MatchResult mr;
        mr.is_over = true;
        card.show(mr, 0);
        card.update(0.25f);                                                                    // the rows, and with them the Leave button
        int left = 0;
        card.set_on_quit([&]() { ++left; });
        auto leave = [&](int32_t x, int32_t y) {
            card.handle_mouse_motion(x, y);
            card.handle_mouse_down(x, y);
            card.handle_mouse_up(x, y);
        };
        leave(530, 20);
        leave(525, 14);
        leave(621, 33);
        check(left == 3, "results Leave: fires in [525, 622) x [14, 34)");
        leave(524, 20);
        leave(530, 12);
        leave(623, 20);
        leave(530, 34);
        check(left == 3, "results Leave: the strips of one picture only fire nothing");
        card.handle_mouse_motion(530, 20);
        check(card.is_quit_hovered(), "results Leave: hover on the resting picture");
        card.handle_mouse_down(530, 20);
        card.handle_mouse_motion(623, 20);                                                     // x = 623 is on the resting picture only: the capture is gone for good
        card.handle_mouse_motion(530, 20);
        card.handle_mouse_up(530, 20);
        check(left == 3 && !card.is_quit_pressed(), "results Leave: leaving the pressed picture cancels the capture for good");
    }
    // options Return: breturn1 [351, 449) x [425, 451), breturn3 [353, 450) x [427, 451): the zone is [353, 449) x [427, 451)
    {
        ScreenButton ret(ButtonRect{OptionsScreen::RETURN_X, OptionsScreen::RETURN_Y, OptionsScreen::RETURN_W, OptionsScreen::RETURN_H}, OptionsScreen::RETURN_PRESSED);
        auto fires = [&](int32_t x, int32_t y) {
            ScreenButton b = ret;
            b.on_move(x, y);
            b.on_press(x, y);
            return b.on_release(x, y);
        };
        check(ret.up_rect() == ButtonRect({351, 425, 98, 26}) && ret.pressed_rect() == ButtonRect({353, 427, 97, 24}), "options Return: breturn1 / 2 and breturn3 rectangles");
        const UIRect r1 = animation_bounds(arc, "breturn1");
        const UIRect r3 = animation_bounds(arc, "breturn3");
        check(r1.x == 351 && r1.y == 425 && r1.w == 98 && r1.h == 26 && r3.x == 353 && r3.y == 427 && r3.w == 97 && r3.h == 24, "options Return: the rectangles are the CHD's");
        check(fires(353, 427) && fires(448, 450) && fires(400, 440), "options Return: fires in [353, 449) x [427, 451)");
        check(!fires(351, 440) && !fires(400, 425) && !fires(449, 440), "options Return: x = 351, y = 425 and x = 449 fire nothing");
    }
    // the host's screen after a cancelled start: unlock gives the controls back
    {
        MS screen;
        screen.init();
        screen.update(0.5f);
        const int32_t first = screen.get_selected_index();
        screen.lock();
        screen.handle_key_down(SDLK_DOWN);
        check(screen.is_locked() && screen.get_selected_index() == first, "locked (START ran): the map cannot be changed");
        screen.unlock();
        screen.handle_key_down(SDLK_DOWN);
        check(!screen.is_locked() && screen.get_selected_index() != first, "unlocked (the start was cancelled): the map can be chosen again");
    }
}

// The original's label fonts (Ants.exe label constructors 0x10116cb / 0x1011856, docs 5.14): every text uses its own cell height
void test_text_sizes(const assets::AssetArchive& arc) {
    std::printf("[text] every label uses the original's font height\n");
    auto find = [](const RecordingRenderer& rr, const std::string& text) -> const RecordingRenderer::Text* {
        for (const auto& t : rr.texts) {
            if (t.text == text) return &t;
        }
        return nullptr;
    };
    // the HUD: status line and chat 12, the score bars' labels 14 (right aligned ending at x = 399 for the local player)
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.post_status("Ready!");
        hud.add_chat_entry("Bob", "Hello there");
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        const auto* status = find(rr, "Ready!");
        check(status != nullptr && status->size == FontSize::Px12 && status->x == 481 && status->y == 254, "hud: the status line is 12 px high at (481, 254)");
        const auto* chat = find(rr, "Hello there");
        check(chat != nullptr && chat->size == FontSize::Px12, "hud: the chat log is 12 px high");
        const auto* header = find(rr, "Bob:");
        check(header != nullptr && header->size == FontSize::Px12 && chat != nullptr && chat->y == header->y + 12, "hud: chat lines are one 12 px cell apart");
        const auto* mine = find(rr, "Player:");                      // the HUD's default player name
        check(mine != nullptr && mine->size == FontSize::Px14 && mine->x + 7 * 6 == 399 && mine->y == 4, "hud: the own score label is 14 px high, right aligned to x = 399, at the top of its box");
    }
    // the quit dialog: one centred label of 24 px letters in (130, 180) 260 x 160
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.open_quit_dialog();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        const auto* prompt = find(rr, "Do you really want to quit?");
        check(prompt != nullptr && prompt->size == FontSize::Px24 && prompt->y == 180 && prompt->x == 130 + (260 - 27 * 6) / 2,
              "quit: the prompt is 24 px high, centred in its box, at y = 180");
    }
    // the start dialog: 35 px letters wrapped at 240 px, one cell (35 px) between the lines, "Waiting for others..." 24 px in (130, 290)
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.start_match_modal();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        // with 6 px per character a line of up to 39 characters fits in 240 px
        const std::string line1 = "Get ready to play!  You are the Green";
        const std::string line2 = "Ants.";
        const auto* l1 = find(rr, line1);
        const auto* l2 = find(rr, line2);
        check(l1 != nullptr && l1->size == FontSize::Px35 && l1->y == 110 && l1->x == 130 + (240 - 37 * 6) / 2, "start dialog: the first line is 35 px high, centred, at y = 110");
        check(l2 != nullptr && l2->size == FontSize::Px35 && l2->y == 110 + 35 && l2->x == 130 + (240 - 5 * 6) / 2, "start dialog: the second line follows one 35 px cell lower");
        const auto* footer = find(rr, "Waiting for others...");
        check(footer != nullptr && footer->size == FontSize::Px24 && footer->y == 290 && footer->x == 130 + (240 - 21 * 6) / 2, "start dialog: the footer is 24 px high at y = 290");
    }
    // the setup screen (after its refresh): map name and description 18 px at x = 36 (y 312 and 380), the prompt 14 px in its label (36, 447), the player's name 18 px
    // at the row's top; every label in (239, 231, 223)
    {
        MapSelectScreen screen;
        screen.init();
        screen.update(0.5f);
        RecordingRenderer rr(arc);
        screen.render(rr, arc);
        const auto& maps = screen.get_maps();
        const auto& cur = maps[static_cast<size_t>(screen.get_selected_index())];
        const auto* name = find(rr, cur.display_name);
        check(name != nullptr && name->size == FontSize::Px18 && name->x == 36 && name->y == 312, "setup: the map name is 18 px high at (36, 312) (the original's label top)");
        check(name != nullptr && name->colour.r == 239 && name->colour.g == 231 && name->colour.b == 223, "setup: the labels' colour is (239, 231, 223)");
        const auto* info = find(rr, cur.description + " (" + std::to_string(cur.minutes) + " min)");
        check(info != nullptr && info->size == FontSize::Px18 && info->x == 36 && info->y == 380, "setup: the map description is 18 px high at (36, 380)");
        const auto lines = wrap_label_text(rr, "Press START when all players' thumbs have appeared.", MapSelectScreen::STATUS_W, FontSize::Px14);
        const auto* first = lines.empty() ? nullptr : find(rr, lines[0]);
        check(first != nullptr && first->size == FontSize::Px14 && first->x == 36 && first->y == 447, "setup: the prompt is 14 px high in its label at (36, 447)");
        const auto* player = find(rr, "Player");
        check(player != nullptr && player->size == FontSize::Px18 && player->x == 415 && player->y == 95, "setup: the player's name is 18 px high at (415, 95)");
    }
    // the results screen: see test_results_screen
}

// The results screen (Ants.exe FUN_010153a1 constructor, FUN_010155ac rows and layout, FUN_01015136 row builder, docs 5.49)
void test_results_screen(const assets::AssetArchive& arc) {
    std::printf("[results] waiting phase, rows, positions, portraits, the cue, the Leave button\n");
    sim::MatchResult result;
    result.is_over = true;
    result.present_mask = 0x0F;
    result.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};                 // teams 0 and 1 are allied
    result.stats[0].score = 300; result.stats[0].friendly_lost = 1; result.stats[0].enemy_killed = 2; result.stats[0].new_hatched = 3;
    result.stats[1].score = 200; result.stats[1].friendly_lost = 10; result.stats[1].enemy_killed = 20; result.stats[1].new_hatched = 30;
    result.stats[2].score = 450; result.stats[2].friendly_lost = 4; result.stats[2].enemy_killed = 5; result.stats[2].new_hatched = 6;
    result.stats[3].score = 100;

    ScorecardModal card;
    card.set_player_names({"Alice", "Bob", "Carol", "Dave"});
    card.show(result, 2);
    check(card.is_open() && card.is_waiting(), "results: the screen opens in its waiting phase");
    check(card.get_audio_to_play() == 0, "results: no cue while it waits (one cue per machine, when the rows are built)");
    {
        RecordingRenderer rr(arc);
        card.render(rr, arc);
        check(rr.texts.size() == 1 && rr.texts[0].text == "Waiting for scores..." && rr.texts[0].x == 100 && rr.texts[0].y == 350 && rr.texts[0].size == FontSize::Px20,
              "results: the label \"Waiting for scores...\" is 20 px high at (100, 350)");
        check(!rr.texts.empty() && rr.texts[0].colour.r == 239 && rr.texts[0].colour.g == 231 && rr.texts[0].colour.b == 223, "results: its colour is (239, 231, 223)");
        bool leave = false, portrait = false;
        for (const auto& sp : rr.sprites) {
            leave = leave || sp.name.rfind("bleave", 0) == 0;
            portrait = portrait || sp.name.rfind("agst30", 0) == 0;
        }
        check(!leave, "results: there is no Leave button while it waits");
        check(!portrait, "results: there are no portraits while it waits");
        check(!card.handle_mouse_down(550, 20) || !card.is_quit_pressed(), "results: a press where the Leave button will be does nothing yet");
    }
    card.update(0.200f);
    check(card.is_waiting() && card.get_audio_to_play() == 0, "results: 200 ms later it still waits");
    card.update(0.049f);
    check(card.is_waiting(), "results: 249 ms: still waiting");
    card.update(0.001f);
    check(!card.is_waiting(), "results: after 250 ms the rows are built");
    check(card.get_audio_to_play() == sim::SoundID::PlayerDefeat, "results: the cue is chosen when the rows appear: team 2 is not in the top row (the loser cue)");
    {
        RecordingRenderer rr(arc);
        card.render(rr, arc);
        check(card.rows().size() == 3, "results: one row per alliance or single team: \"Alice & Bob\", Carol, Dave");
        auto text_at = [&](const std::string& text, int32_t x, int32_t y) {
            for (const auto& t : rr.texts) if (t.text == text && t.x == x && t.y == y && t.size == FontSize::Px18) return &t;
            return static_cast<const RecordingRenderer::Text*>(nullptr);
        };
        // the top row stands at Y = 235, the others at 50 i + 273
        const auto* n0 = text_at("Alice & Bob", 100, 235);
        const auto* n1 = text_at("Carol", 100, 323);
        const auto* n2 = text_at("Dave", 100, 373);
        check(n0 != nullptr && n1 != nullptr && n2 != nullptr, "results: the names stand at x = 100, y = 235, 323, 373 (18 px)");
        check(n0 != nullptr && n0->colour.r == 239 && n0->colour.g == 231 && n0->colour.b == 223, "results: the name colour is (239, 231, 223)");
        // the numbers are left aligned at 485 / 534 / 555 / 576 and add up the two members of an alliance
        check(text_at("500", 485, 235) != nullptr && text_at("11", 534, 235) != nullptr && text_at("22", 555, 235) != nullptr && text_at("33", 576, 235) != nullptr,
              "results: the alliance row adds up its members and the numbers start at x = 485, 534, 555, 576");
        check(text_at("450", 485, 323) != nullptr && text_at("4", 534, 323) != nullptr && text_at("5", 555, 323) != nullptr && text_at("6", 576, 323) != nullptr,
              "results: the second row (Carol)");
        check(text_at("100", 485, 373) != nullptr && text_at("0", 534, 373) != nullptr, "results: rows with nothing to show still have their numbers (no all-zero row is hidden)");
        // the counters are drawn on one line whatever their width (the substitute font does not fit two digits into the 19 px label)
        ScorecardModal wide;
        sim::MatchResult big = result;
        big.stats[2].friendly_lost = 1234;
        wide.show(big, 2);
        wide.update(0.3f);
        RecordingRenderer rw(arc);
        wide.render(rw, arc);
        bool one_line = false;
        for (const auto& t : rw.texts) one_line = one_line || (t.text == "1234" && t.x == 534 && t.y == 323 && t.size == FontSize::Px18);
        check(one_line, "results: a counter that is wider than its label stays on one line");
        // the ant portraits: one per team at (60, Y + 20), an alliance shows both at (45, Y + 20) and (75, Y + 20), each in its own colour, animated
        const auto* anim = arc.find_animation("agst301");
        check(anim != nullptr && !anim->subitems.empty(), "results: the portrait animation exists");
        if (anim != nullptr && !anim->subitems.empty()) {
            const auto& parts = anim->subitems[Renderer::get_anim_subitem_by_time(*anim, 250)].frames;
            auto portrait_at = [&](int32_t x, int32_t y, uint8_t team) {
                size_t found = 0;
                for (const auto& part : parts) {
                    for (const auto& sp : rr.sprites) {
                        if (sp.id == part.sprite_index && sp.x == x + part.dx && sp.y == y + part.dy && sp.team == team) { ++found; break; }
                    }
                }
                return found == parts.size();
            };
            check(portrait_at(45, 255, 0) && portrait_at(75, 255, 1), "results: the alliance row shows both ants at (45, 255) and (75, 255) in their colours");
            check(portrait_at(60, 343, 2), "results: a single team's ant stands at (60, Y + 20) in its colour");
            check(portrait_at(60, 393, 3), "results: the last row's ant");
            // the portrait moves with the screen's clock: later the clip shows another frame
            const size_t f_now = Renderer::get_anim_subitem_by_time(*anim, 250);
            const size_t f_later = Renderer::get_anim_subitem_by_time(*anim, 850);
            check(f_now != f_later, "results: frames 250 ms and 850 ms into the clip differ (a moving portrait is testable)");
            card.update(0.600f);
            RecordingRenderer later(arc);
            card.render(later, arc);
            const auto& parts_later = anim->subitems[f_later].frames;
            size_t found = 0;
            for (const auto& part : parts_later) {
                for (const auto& sp : later.sprites) {
                    if (sp.id == part.sprite_index && sp.x == 60 + part.dx && sp.y == 343 + part.dy && sp.team == 2) { ++found; break; }
                }
            }
            check(found == parts_later.size(), "results: 600 ms later the portrait shows the next frame of its clip");
        }
        // the Leave button exists now
        bool leave = false;
        for (const auto& sp : rr.sprites) leave = leave || sp.name.rfind("bleave", 0) == 0;
        check(leave, "results: the Leave button is there once the rows are");
        bool left = false;
        card.set_on_quit([&]() { left = true; });
        card.handle_mouse_down(550, 20);
        check(card.is_quit_pressed(), "results: a press on Leave presses it");
        card.handle_mouse_up(550, 20);
        check(left, "results: the release leaves");
    }
    // the cue for the other members: Bob's ally is the top row's first team (Alice), so Bob hears the winner cue
    {
        ScorecardModal bob;
        bob.show(result, 1);
        bob.update(0.3f);
        check(bob.get_audio_to_play() == sim::SoundID::VictoryFanfare, "results: the winner cue for the ally of the top row's first team");
        ScorecardModal alice;
        alice.show(result, 0);
        alice.update(0.3f);
        check(alice.get_audio_to_play() == sim::SoundID::VictoryFanfare, "results: and for the first team itself");
    }
    // teams that are not shown (a browser game has no other players) have no row; a team that dropped has none either
    {
        ScorecardModal only;
        only.set_shown_teams(0x05);
        only.show(result, 2);
        only.update(0.3f);
        check(only.rows().size() == 2 && only.rows()[0].first == 2 && only.rows()[1].first == 0, "results: only the shown teams have rows (Alice's alliance partner is not shown: a single row)");
        sim::MatchResult dropped = result;
        dropped.present_mask = 0x0D;                                               // team 1 dropped out
        ScorecardModal d;
        d.set_player_names({"Alice", "Bob", "Carol", "Dave"});
        d.show(dropped, 0);
        d.update(0.3f);
        check(d.rows().size() == 3 && d.rows()[0].name == "Carol" && d.rows()[1].name == "Alice" && d.rows()[2].name == "Dave", "results: a dropped team has no row");
    }
    // names: without a name the colour word; the local player's own name; at most 35 characters
    {
        ScorecardModal n;
        n.set_local_player_name("Me");
        n.show(result, 3);
        n.update(0.3f);
        bool me = false, word = false;
        for (const auto& r : n.rows()) {
            me = me || r.name == "Me";
            word = word || r.name == "Red" || r.name == "Blue" || r.name.find("Red") != std::string::npos;
        }
        check(me && word, "results: the local player's own name, the colour words for unnamed teams");
        ScorecardModal longname;
        longname.set_player_names({std::string(30, 'a'), std::string(30, 'b'), "C", "D"});
        longname.show(result, 0);
        longname.update(0.3f);
        check(longname.rows()[0].name.size() == 35, "results: a row name is at most 35 characters (the label's limit)");
    }
}

// The original's greedy word wrap (FUN_0102b0b5), checked on a renderer that measures 15 px per character
class MonoRenderer : public IRenderer {
public:
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA) override { drawn.push_back({text, x, y, FontSize::Px12}); }
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA, FontSize size) override { drawn.push_back({text, x, y, size}); }
    int32_t get_text_width(const std::string& text, FontSize = FontSize::Px12) const override { return static_cast<int32_t>(text.size()) * 15; }
    int32_t get_text_height(FontSize size = FontSize::Px12) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t) override {}
    struct Drawn { std::string text; int32_t x; int32_t y; FontSize size; };
    std::vector<Drawn> drawn;
};

void test_label_wrap() {
    std::printf("[text] the original's label wrap and drawing\n");
    MonoRenderer mono;
    auto lines = wrap_label_text(mono, "Get ready to play!  You are the Green Ants.", 240, FontSize::Px35);
    // 15 px per character: a prefix of 16 characters reaches 240 px; the line ends at the last word end before that
    check(lines.size() == 3 && lines[0] == "Get ready to" && lines[1] == "play!  You are" && lines[2] == "the Green Ants.", "wrap: the start dialog's text at 240 px, 15 px per character");
    // a word that is wider than the box is cut one character before the width is reached
    lines = wrap_label_text(mono, "Supercalifragilistic", 100, FontSize::Px18);
    check(lines.size() == 4 && lines[0] == "Superc" && lines[1] == "alifra" && lines[2] == "gilist" && lines[3] == "ic", "wrap: a long word is cut before the width");
    // leading spaces of a line are skipped, empty text has no line, a box without width keeps the text in one line
    lines = wrap_label_text(mono, "   hello", 300, FontSize::Px12);
    check(lines.size() == 1 && lines[0] == "hello", "wrap: leading spaces are skipped");
    check(wrap_label_text(mono, "", 300, FontSize::Px12).empty(), "wrap: no text, no line");
    lines = wrap_label_text(mono, "no room at all", 0, FontSize::Px12);
    check(lines.size() == 1 && lines[0] == "no room at all", "wrap: a box without width does not loop");
    lines = wrap_label_text(mono, "one\ntwo words here", 300, FontSize::Px12);
    check(lines.size() == 2 && lines[0] == "one" && lines[1] == "two words here", "wrap: a newline ends a line");
    // fit_text: characters are dropped from the end until the text fits
    check(fit_text(mono, "abcdefghij", 60, FontSize::Px12) == "abcd" && fit_text(mono, "abc", 60, FontSize::Px12) == "abc" && fit_text(mono, "x", 1, FontSize::Px12) == "x",
          "fit_text: cut to the width, one character stays");
    // draw_label: centred lines, one cell height apart, from the top of the box
    mono.drawn.clear();
    const int32_t h = draw_label(mono, "Get ready to play!  You are the Green Ants.", 130, 110, 240, assets::ColorRGBA{}, FontSize::Px35, true);
    check(h == 3 * 35 && mono.drawn.size() == 3, "draw_label: three lines of 35 px");
    check(mono.drawn.size() == 3 && mono.drawn[0].y == 110 && mono.drawn[1].y == 145 && mono.drawn[2].y == 180, "draw_label: the lines are 35 px apart from the top of the box");
    check(mono.drawn.size() == 3 && mono.drawn[0].x == 130 + (240 - 12 * 15) / 2 && mono.drawn[1].x == 130 + (240 - 14 * 15) / 2 && mono.drawn[2].x == 130 + (240 - 15 * 15) / 2,
          "draw_label: every line is centred in the box");
    mono.drawn.clear();
    draw_label(mono, "left text", 40, 50, 300, assets::ColorRGBA{}, FontSize::Px18, false);
    check(mono.drawn.size() == 1 && mono.drawn[0].x == 40 && mono.drawn[0].y == 50 && mono.drawn[0].size == FontSize::Px18, "draw_label: a left aligned label starts at the box's left edge");
}

// ------------------------------------------------------------------------------------------------------------------------------------------
// The alliance dialogs (Ants.exe FUN_01015b65 / FUN_010160e2 / FUN_01016438): what the original shows, where, and what each answer does
// ------------------------------------------------------------------------------------------------------------------------------------------

// Four teams with a hill each in a test world (the ally pedestal needs more than two hills), named Alice, Bob, Carol and Dave
struct AllianceTable {
    sim::SimulationEngine sim;
    ViewportCamera camera{0, 0};
    std::vector<uint32_t> sounds;
    AllianceTable() {
        sim.init_test_world(60, 60, 5, 600000);
        const sim::TileCoord hills[4] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
        for (uint8_t p = 0; p < 4; ++p) sim.grid_mut().set_anthill(p, hills[p]);
        const char* names[4] = {"Alice", "Bob", "Carol", "Dave"};
        for (uint8_t p = 0; p < 4; ++p) sim.set_player_name(p, names[p]);
    }
    void prepare(HUD& hud, uint8_t local) {
        hud.init(local);
        hud.set_team_names({"Alice", "Bob", "Carol", "Dave"});
        hud.set_on_play_sfx([this](uint32_t id) { sounds.push_back(id); });
    }
    void refresh(HUD& hud) { hud.update(sim.get_world_state(), 1); }
    static sim::Command command(sim::CommandType type, uint8_t issuer, uint8_t other = 255) {
        sim::Command c;
        c.type = type;
        c.issuer = issuer;
        c.other_player = other;
        return c;
    }
    bool news_has(const std::string& text) {
        bool found = false;
        for (const auto& n : sim.poll_news_events()) found = found || n.message_text.find(text) != std::string::npos;
        return found;
    }
};

// A sink that keeps the commands until they are applied, like the turn of a network match
struct DelayedSink : sim::CommandSink {
    std::vector<sim::Command> held;
    sim::CommandResult submit(const sim::Command& c) override {
        held.push_back(c);
        sim::CommandResult r;
        r.status = sim::CommandResult::Status::Applied;
        return r;
    }
};

void test_alliance_dialog_layout(const assets::AssetArchive& arc) {
    std::printf("[alliance] the question, the waiting dialog and the confirmation: text, buttons, hover and pressed art\n");
    using D = HUD::AllianceDialog;
    AllianceTable t;
    HUD hud;
    t.prepare(hud, 1);
    t.sim.apply_command(AllianceTable::command(sim::CommandType::AllianceInvite, 0, 1));          // Alice asks Bob
    t.refresh(hud);
    check(hud.alliance_dialog() == D::Invitation && hud.alliance_dialog_team() == 0 && hud.is_modal_open(), "question: the invitee sees the invitation of Alice");
    check(hud.alliance_dialog_text() == sim::strings::format(sim::strings::kInviteDialog, "Alice", "Green"), "question: string 1 with the name and the colour word");
    {
        RecordingRenderer rr(arc);
        sim::WorldState world = t.sim.get_world_state();
        hud.render(rr, arc, world, t.camera);
        check(rr.has_sprite_at("dfram1.bmp", 100, 100), "question: the dialog frame at the origin (100, 100)");
        check(rr.has_sprite_at("accpt1.bmp", 152, 260) && rr.has_sprite_at("decl1.bmp", 284, 260), "question: Accept at (152, 260) and Decline at (284, 260)");
        bool all24 = !rr.texts.empty();
        int64_t first_y = -1;
        int lines = 0;
        for (const auto& tx : rr.texts) {
            if (tx.size != FontSize::Px24) continue;
            ++lines;
            if (first_y < 0) first_y = tx.y;
            all24 = all24 && tx.y == first_y + (lines - 1) * 24 && tx.x >= 130 && tx.x + static_cast<int32_t>(tx.text.size()) * 6 <= 130 + 270;
        }
        check(lines >= 2 && first_y == 110 && all24, "question: the text is 24 px high, from y = 110, one cell per line, inside the 270 px box");
    }
    // hover and pressed art, the click sound at the press, the answer at the release
    hud.handle_mouse_motion(160, 270, t.sim, t.camera);
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, t.sim.get_world_state(), t.camera);
        check(rr.has_sprite_at("accpt2.bmp", 152, 260), "question: the hovered Accept shows accpt2");
    }
    t.sounds.clear();
    hud.handle_mouse_down(160, 270, 1, t.sim, t.camera);
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, t.sim.get_world_state(), t.camera);
        check(rr.has_sprite_at("accpt3.bmp", 152, 261), "question: the pressed Accept shows accpt3, one pixel lower (part offset (52, 161))");
        check(t.sounds.size() == 1 && t.sounds[0] == sim::SoundID::ButtonClick, "question: the click sound plays at the press");
    }
    hud.handle_mouse_up(400, 400, 1, t.sim, t.camera);                                            // released elsewhere: cancelled
    check(hud.alliance_dialog() == D::Invitation && !t.sim.stats_manager().are_allies(0, 1), "question: a release off the button does not answer");

    // the waiting dialog of the proposer
    HUD proposer;
    t.prepare(proposer, 0);
    t.refresh(proposer);
    check(proposer.alliance_dialog() == D::Waiting && proposer.alliance_dialog_team() == 1, "waiting: the proposer waits for Bob");
    check(proposer.alliance_dialog_text() == sim::strings::format(sim::strings::kWaitingForAnswer, "Bob", "Red"), "waiting: string 3 with the name and the colour word");
    {
        RecordingRenderer rr(arc);
        proposer.render(rr, arc, t.sim.get_world_state(), t.camera);
        check(rr.has_sprite_at("withd1.bmp", 220, 260), "waiting: Withdraw at (220, 260)");
        check(rr.named("accpt1.bmp").empty() && rr.named("decl1.bmp").empty(), "waiting: no Accept and Decline");
    }

    // the confirmation before a team is broken
    AllianceTable u;
    u.sim.form_alliance(0, 2);
    HUD leader;
    u.prepare(leader, 0);
    leader.request_team_up(u.sim, 1);                                                             // Alice (allied with Carol) offers Bob a team
    check(leader.alliance_dialog() == D::BreakConfirm && leader.alliance_dialog_team() == 2, "confirmation: asked about the team with Carol");
    check(leader.alliance_dialog_text() == sim::strings::format(sim::strings::kBreakTeamConfirm, "Carol", "Blue"), "confirmation: string 4 with the ally's name and colour word");
    {
        RecordingRenderer rr(arc);
        leader.render(rr, arc, u.sim.get_world_state(), u.camera);
        check(rr.has_sprite_at("yes1.bmp", 180, 260) && rr.has_sprite_at("no1.bmp", 292, 260), "confirmation: Yes at (180, 260) and No at (292, 260)");
        check(u.sim.get_world_state().pending_invite_from[1] == 255, "confirmation: nothing is offered before the answer");
    }
}

void test_alliance_answers(const assets::AssetArchive&) {
    std::printf("[alliance] every answer and what it sends: accept, decline, withdraw, confirm, the keys, dropped teams, dialogs over dialogs\n");
    using D = HUD::AllianceDialog;
    using CT = sim::CommandType;
    // Accept (A): the team is made, the texts and cues of the original, both dialogs close
    {
        AllianceTable t;
        HUD alice, bob;
        t.prepare(alice, 0);
        t.prepare(bob, 1);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(alice);
        t.refresh(bob);
        t.sim.clear_audio_events();
        bob.handle_key_down('a', t.sim, t.camera);
        check(bob.alliance_dialog() == D::None && t.sim.stats_manager().are_allies(0, 1), "accept: A answers yes, the team is made");
        check(t.sim.get_world_state().pending_invite_from[1] == 255, "accept: the offer is gone");
        check(t.news_has("Alice (Green) and Bob (Red) are a team now!"), "accept: the News Flash of string 39");
        t.refresh(alice);
        check(alice.alliance_dialog() == D::None, "accept: the proposer's waiting dialog closes");
        t.refresh(bob);
        check(bob.alliance_dialog() == D::None, "accept: the question does not come back");
    }
    // Decline: D, Esc and the button; the proposer hears "rejected"
    for (int way = 0; way < 3; ++way) {
        AllianceTable t;
        HUD alice, bob;
        t.prepare(alice, 0);
        t.prepare(bob, 1);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(alice);
        t.refresh(bob);
        if (way == 0) bob.handle_key_down('d', t.sim, t.camera);
        if (way == 1) bob.handle_key_down(SDLK_ESCAPE, t.sim, t.camera);
        if (way == 2) {
            bob.handle_mouse_down(300, 270, 1, t.sim, t.camera);
            bob.handle_mouse_up(300, 270, 1, t.sim, t.camera);
        }
        check(bob.alliance_dialog() == D::None && !t.sim.stats_manager().are_allies(0, 1), "decline (" + std::to_string(way) + "): no team");
        check(t.sim.get_world_state().pending_invite_from[1] == 255, "decline (" + std::to_string(way) + "): the offer is gone");
        check(t.news_has("Bob rejected teaming up"), "decline (" + std::to_string(way) + "): the proposer reads string 80");
        t.refresh(alice);
        check(alice.alliance_dialog() == D::None, "decline (" + std::to_string(way) + "): the waiting dialog closes");
    }
    // Withdraw: W and Esc take the offer back; the invitee's question closes and it reads "withdrew"
    for (int way = 0; way < 2; ++way) {
        AllianceTable t;
        HUD alice, bob;
        t.prepare(alice, 0);
        t.prepare(bob, 1);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(alice);
        t.refresh(bob);
        alice.handle_key_down(way == 0 ? SDLK_w : SDLK_ESCAPE, t.sim, t.camera);
        check(alice.alliance_dialog() == D::None && t.sim.get_world_state().pending_invite_from[1] == 255, "withdraw (" + std::to_string(way) + "): the offer is taken back");
        check(t.news_has("Alice withdrew offer to team up"), "withdraw (" + std::to_string(way) + "): the invitee reads string 82");
        t.refresh(bob);
        check(bob.alliance_dialog() == D::None, "withdraw (" + std::to_string(way) + "): the question closes");
    }
    // Keys that mean nothing change nothing, the dialog takes every key
    {
        AllianceTable t;
        HUD bob;
        t.prepare(bob, 1);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(bob);
        bool taken = true;
        for (int key : std::vector<int>{'x', 'y', 'n', 'w', static_cast<int>(SDLK_RETURN), static_cast<int>(SDLK_F1)}) taken = taken && bob.handle_key_down(key, t.sim, t.camera);
        check(taken && bob.alliance_dialog() == D::Invitation, "keys: the question only answers to A, D and Esc, and takes every key");
        bob.handle_key_down('q', t.sim, t.camera, KMOD_CTRL);                                    // not even Ctrl+Q opens the quit dialog over it
        check(!bob.is_quit_dialog_open(), "keys: no quit dialog over the question");
    }
    // A team is replaced: the question is string 2, Accept breaks the present team first (News Flash of string 40), then makes the new one
    {
        AllianceTable t;
        t.sim.form_alliance(0, 2);
        HUD alice;
        t.prepare(alice, 0);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 3, 0));                    // Dave asks Alice, who is allied with Carol
        t.refresh(alice);
        check(alice.alliance_dialog() == D::Invitation && alice.alliance_dialog_text() ==
                  sim::strings::format(sim::strings::kInviteBreakDialog, "Dave", "Black", "Carol", "Blue"), "replace: the question says that the team with Carol ends");
        t.sim.poll_news_events();
        alice.handle_key_down('a', t.sim, t.camera);
        check(t.sim.stats_manager().are_allies(0, 3) && !t.sim.stats_manager().are_allies(0, 2) && !t.sim.stats_manager().are_allies(2, 3), "replace: Alice and Dave are a team, Carol is free");
        check(t.news_has("are no longer a team!"), "replace: the old team's break is announced (string 40)");
    }
    // The confirmation: Yes breaks the team and sends the offer, No changes nothing; both the keys and the buttons
    for (int way = 0; way < 3; ++way) {
        AllianceTable t;
        t.sim.form_alliance(0, 2);
        HUD alice;
        t.prepare(alice, 0);
        alice.request_team_up(t.sim, 1);
        check(alice.alliance_dialog() == D::BreakConfirm, "confirmation (" + std::to_string(way) + "): asks first");
        if (way == 0) alice.handle_key_down('y', t.sim, t.camera);
        if (way == 1) alice.handle_key_down(SDLK_ESCAPE, t.sim, t.camera);
        if (way == 2) {
            alice.handle_mouse_down(190, 270, 1, t.sim, t.camera);
            alice.handle_mouse_up(190, 270, 1, t.sim, t.camera);
        }
        const bool yes = way != 1;
        check(alice.alliance_dialog() == D::None, "confirmation (" + std::to_string(way) + "): the dialog closes");
        check(t.sim.stats_manager().are_allies(0, 2) == !yes, "confirmation (" + std::to_string(way) + "): the team with Carol " + (yes ? "ends" : "stays"));
        check((t.sim.get_world_state().pending_invite_from[1] == 0) == yes, "confirmation (" + std::to_string(way) + "): the offer to Bob " + (yes ? "is out" : "is not sent"));
    }
    // Without an ally the offer goes out at once; the pedestal of an enemy hill does it (only with more than two players)
    {
        AllianceTable t;
        HUD alice;
        t.prepare(alice, 0);
        alice.select_base(1);
        alice.handle_mouse_down(500, 190, 1, t.sim, t.camera);
        alice.handle_mouse_up(500, 190, 1, t.sim, t.camera);
        check(t.sim.get_world_state().pending_invite_from[1] == 0, "pedestal: the ally pedestal of an enemy hill sends the offer at once");
        t.refresh(alice);
        check(alice.alliance_dialog() == D::Waiting, "pedestal: and the waiting dialog follows");
    }
    // A dropped team takes its offers and dialogs with it
    {
        AllianceTable t;
        HUD bob, alice;
        t.prepare(bob, 1);
        t.prepare(alice, 0);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(bob);
        t.refresh(alice);
        check(bob.alliance_dialog() == D::Invitation && alice.alliance_dialog() == D::Waiting, "drop: both dialogs are open");
        t.sim.apply_command(AllianceTable::command(CT::Drop, 0));
        t.refresh(bob);
        t.refresh(alice);
        check(bob.alliance_dialog() == D::None && t.sim.get_world_state().pending_invite_from[1] == 255, "drop: the question of a team that dropped closes");
    }
    // One dialog at a time: the question waits while another dialog is open
    {
        AllianceTable t;
        HUD bob;
        t.prepare(bob, 1);
        bob.open_quit_dialog();
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(bob);
        check(bob.alliance_dialog() == D::None && bob.is_quit_dialog_open(), "queue: no question over the quit dialog");
        bob.close_quit_dialog();
        t.refresh(bob);
        check(bob.alliance_dialog() == D::Invitation, "queue: the question comes when the quit dialog is closed");
    }
    // In a network match the answer reaches the simulation a few turns later: the question does not come back meanwhile
    {
        AllianceTable t;
        HUD bob;
        t.prepare(bob, 1);
        DelayedSink sink;
        bob.set_command_sink(&sink);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(bob);
        bob.handle_key_down('a', t.sim, t.camera);
        t.refresh(bob);
        check(bob.alliance_dialog() == D::None && sink.held.size() == 1 && sink.held[0].type == CT::AllianceAccept && sink.held[0].other_player == 0 &&
                  sink.held[0].issuer == 1, "network: Accept is handed to the turn manager with the proposer, and the question stays away");
        for (const auto& c : sink.held) t.sim.apply_command(c);                                   // the turn executes it
        t.refresh(bob);
        check(bob.alliance_dialog() == D::None && t.sim.stats_manager().are_allies(0, 1), "network: the team is made when the turn comes");
        // a new offer from somebody else opens a new question
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 2, 1));
        t.refresh(bob);
        check(bob.alliance_dialog() == D::Invitation && bob.alliance_dialog_team() == 2, "network: a later offer asks again");
    }
    // The commands of a confirmed attack on the ally: the team is broken first, then the order is given
    {
        AllianceTable t;
        t.sim.form_alliance(0, 1);
        const uint32_t mine = t.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{20, 20});
        t.sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{22, 20});
        HUD alice;
        t.prepare(alice, 0);
        DelayedSink sink;
        alice.set_command_sink(&sink);
        alice.select_ant(mine, false);
        alice.order_selected(t.sim, sim::TileCoord{22, 20}, false, true);
        check(alice.alliance_dialog() == D::BreakConfirm && sink.held.empty(), "attack on the ally: the order waits for the answer");
        alice.handle_key_down('y', t.sim, t.camera);
        check(sink.held.size() == 2 && sink.held[0].type == CT::AllianceBreak && sink.held[1].type == CT::GroupAttack && sink.held[1].tile_x == 22,
              "attack on the ally: Yes sends the break and then the attack");
    }
}

} // namespace

int main() {
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot load ants.chd\n");
        return 2;
    }
    test_clock_digits(arc);
    test_score_digits(arc);
    test_score_boxes(arc);
    test_home_panel(arc);
    test_ally_pedestal(arc);
    test_rubber_band(arc);
    test_screens(arc);
    test_room_screen(arc);
    test_leader_screen(arc);
    test_button_zones(arc);
    test_more_button_zones(arc);
    test_pedestal_chains(arc);
    test_pedestal_timeline(arc);
    test_minimap(arc);
    test_minimap_dots(arc);
    test_chat_log(arc);
    test_static_shell(arc);
    test_cursor_rules(arc);
    test_button_states(arc);
    test_text_sizes(arc);
    test_results_screen(arc);
    test_label_wrap();
    test_alliance_dialog_layout(arc);
    test_alliance_answers(arc);
    std::printf("\nhud layout: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
