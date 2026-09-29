// HUD layout tests: the real HUD drives a recording IRenderer, and the emitted draw calls are checked against the
// coordinates and rules of the original (Ants.exe) - no pixels, no SDL video needed.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_assets/asset_archive.hpp"
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
        sprites.push_back({sprite_id, archive_.get_sprite(sprite_id).name, x, y});
    }
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool mirrored) override {
        int32_t id = archive_.find_sprite_id(name);
        if (id < 0) id = archive_.find_sprite_id(name + ".bmp");
        if (id >= 0) draw_sprite(static_cast<uint32_t>(id), x, y, mirrored);
    }
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA color) override {
        fills.push_back({x, y, w, h, color});
    }
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA) override {
        texts.push_back({text, x, y});
    }
    void set_hud_team(uint8_t team) override { hud_team = team; }

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

    struct Text { std::string text; int32_t x; int32_t y; };
    std::vector<SpriteDraw> sprites;
    std::vector<FillDraw> fills;
    std::vector<Text> texts;
    uint8_t hud_team{0};

private:
    const assets::AssetArchive& archive_;
};

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
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);

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

// Ally pedestal of a selected enemy base: animation butalyu / butalyd at absolute coordinates.
void test_ally_pedestal(const assets::AssetArchive& arc) {
    std::printf("[hud] ally pedestal position\n");
    RecordingRenderer rr(arc);
    sim::WorldState world;
    HUD hud;
    hud.init(0);
    hud.select_base(1);
    ViewportCamera camera;
    hud.render(rr, arc, world, camera);
    check(rr.has_sprite_at("butup.bmp", 477, 157), "ally pedestal base at (477,157)");
    check(rr.has_sprite_at("butdipu.bmp", 485, 164), "ally icon at (485,164)");
    check(rr.has_sprite_at("labdib.bmp", 477, 142), "ally label at (477,142)");
}

// Full-screen HUD screens: composites drawn from the original animations, no invented dimming layers.
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

} // namespace

int main() {
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot load ants.chd\n");
        return 2;
    }
    test_clock_digits(arc);
    test_score_digits(arc);
    test_home_panel(arc);
    test_ally_pedestal(arc);
    test_screens(arc);
    std::printf("\nhud layout: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
