// HUD layout tests: the real HUD drives a recording IRenderer, and the emitted draw calls are checked against the
// coordinates and rules of the original (Ants.exe) - no pixels, no SDL video needed.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ants_app/hud.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/ui_anim.hpp"
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
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA color) override {
        rects.push_back({x, y, w, h, color});
    }
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA) override {
        texts.push_back({text, x, y});
    }
    void set_hud_team(uint8_t team) override { hud_team = team; }
    void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* rgba) override {
        images.push_back({x, y, w, h, std::vector<uint8_t>(rgba, rgba + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u)});
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
    struct Text { std::string text; int32_t x; int32_t y; };
    std::vector<SpriteDraw> sprites;
    std::vector<FillDraw> fills;
    std::vector<FillDraw> rects;         // frames (draw_rect)
    std::vector<Text> texts;
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

// Minimap (Ants.exe FUN_01009596): a 119x91 image at (480,35) with the class speckle colours, the object table, the fog
// table and square team dots for ants.
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
    // an anthill tile of the black colony (animation 245, colour 239, size flag 2) and a bomb (id 129, hidden)
    world.cells[30 * 60 + 15].interactive_id = 245;
    world.cells[40 * 60 + 15].interactive_id = 129;
    // a green ant (player 0) at tile (55, 5)
    sim::AntSnapshot ant{};
    ant.id = 1; ant.player_id = 0; ant.hp = 10; ant.tile_x = 55; ant.tile_y = 5;
    world.ants.push_back(ant);

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

    // hill dot (colour 239, 2 cells => 4x3 px) at the cell centre of tile (15,30): px = 15.5*119/60 = 30, py = 30.5*91/60 = 46
    check(pixel(im, 30, 46) == rgb_of(239), "hill drawn with colour 239 at its cell");
    // the bomb cell (15,40) shows the terrain colour, not an object colour
    check(in_set(pixel(im, 30, 60), {251, 201, 249}), "bombs are not drawn on the minimap");
    // green ant dot: colour 47 at tile (55,5): px = 55.5*119/60 = 110, py = 5.5*91/60 = 8
    check(pixel(im, 110, 8) == rgb_of(47), "green ant dot colour 47");

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
        hud.handle_mouse_down(160, 300, 1, sim, camera);   // the options' Chat OFF button spans x 146..192, y 287..311
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

    // Option screen controls (op_screen): toggles, Return, slider thumbs at the original's defaults 100 / 65 / 50
    {
        HUD hud;
        hud.init(0);
        hud.open_options();
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(hud.is_chat_enabled() && hud.is_quick_help_enabled(), "options default: chat on, quick help on");
        check(hud.get_sfx_volume() == 1.0f && hud.get_music_volume() > 0.649f && hud.get_music_volume() < 0.651f && hud.get_scroll_rate() == 0.5f,
              "options default: sound 100, music 65, scroll 50");
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
        // Toggle hit rectangles are the union of the resting and pressed art
        hud.open_options();
        hud.handle_mouse_down(102, 289, 1, sim, camera);
        hud.handle_mouse_down(150, 312, 1, sim, camera);
        check(hud.is_chat_enabled(), "(150,312) is outside the chat OFF toggle");
        hud.handle_mouse_down(151, 289, 1, sim, camera);
        check(!hud.is_chat_enabled(), "(151,289) is inside the chat OFF toggle");
        hud.handle_mouse_down(102, 289, 1, sim, camera);
        check(hud.is_chat_enabled(), "(102,289) is inside the chat ON toggle");
        hud.handle_mouse_down(404, 289, 1, sim, camera);
        check(!hud.is_quick_help_enabled(), "(404,289) is inside the quick help OFF toggle (the startup help is skipped)");
        hud.handle_mouse_down(355, 289, 1, sim, camera);
        check(hud.is_quick_help_enabled(), "(355,289) is inside the quick help ON toggle");
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

    // Setup screen: constants are the union of the resting and pressed art; art at the animation coordinates
    {
        auto union_of = [&](const char* a, const char* b) {
            const UIRect ra = animation_bounds(arc, a);
            const UIRect rb = animation_bounds(arc, b);
            const int32_t x0 = std::min(ra.x, rb.x), y0 = std::min(ra.y, rb.y);
            const int32_t x1 = std::max(ra.x + ra.w, rb.x + rb.w), y1 = std::max(ra.y + ra.h, rb.y + rb.h);
            return UIRect{x0, y0, x1 - x0, y1 - y0};
        };
        auto same = [&](const UIRect& r, int32_t x, int32_t y, int32_t w, int32_t h) { return r.x == x && r.y == y && r.w == w && r.h == h; };
        using MS = MapSelectScreen;
        check(same(union_of("up1", "up3"), MS::BTN_UP_X, MS::BTN_UP_Y, MS::BTN_UP_W, MS::BTN_UP_H), "setup: up button rect = union of up1 / up3");
        check(same(union_of("down1", "down3"), MS::BTN_DOWN_X, MS::BTN_DOWN_Y, MS::BTN_DOWN_W, MS::BTN_DOWN_H), "setup: down button rect");
        check(same(union_of("start1", "start3"), MS::BTN_START_X, MS::BTN_START_Y, MS::BTN_START_W, MS::BTN_START_H), "setup: start button rect");
        check(same(union_of("leave1", "leave3"), MS::BTN_QUIT_X, MS::BTN_QUIT_Y, MS::BTN_QUIT_W, MS::BTN_QUIT_H), "setup: leave button rect");
        check(same(union_of("d_on1", "d_on3"), MS::BTN_FOW_ON_X, MS::BTN_FOW_ON_Y, MS::BTN_FOW_ON_W, MS::BTN_FOW_ON_H), "setup: Fog of War ON rect");
        check(same(union_of("d_off1", "d_off3"), MS::BTN_FOW_OFF_X, MS::BTN_FOW_OFF_Y, MS::BTN_FOW_OFF_W, MS::BTN_FOW_OFF_H), "setup: Fog of War OFF rect");

        MapSelectScreen screen;
        screen.init();
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
        screen.handle_mouse_down(MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2, 1);
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

// The setup screen as a network room: a row per occupied seat (portrait, name, the thumb of the connection quality), the status line, and the
// host-only controls (the map, the fog option and START belong to the host; every player can leave).
void test_room_screen(const assets::AssetArchive& arc) {
    std::printf("[room] names with a thumb per seat (netgood / netok / netbad / netunk), the status line, host-only controls\n");
    using MS = MapSelectScreen;
    MS screen;
    screen.init();
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
    check(text_at("Press START when all players' thumbs have appeared.", 38), "room: the status line is the original's prompt");
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
    screen.handle_mouse_down(MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3, 1);
    check(screen.get_selected_index() != before && maps_chosen.size() == 1, "room host: the down button changes the map and tells the room");
    check(maps_chosen[0] == screen.get_maps()[static_cast<size_t>(screen.get_selected_index())].filename, "room host: the room gets the file name of the map");
    screen.handle_mouse_down(MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2, 1);
    screen.handle_mouse_down(MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2, 1);           // the same choice again changes nothing
    check(fog_chosen.size() == 1 && fog_chosen[0], "room host: the fog option is reported once per change");
    screen.handle_mouse_down(MS::BTN_START_X + 5, MS::BTN_START_Y + 5, 1);
    check(started == 1, "room host: START starts");
    screen.handle_mouse_down(MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5, 1);
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
    screen.handle_mouse_down(MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3, 1);
    screen.handle_mouse_down(MS::W_MAP_X + 5, MS::W_MAP_Y + 5, 1);
    screen.handle_mouse_down(MS::BTN_FOW_OFF_X + 2, MS::BTN_FOW_OFF_Y + 2, 1);
    screen.handle_mouse_down(MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2, 1);
    screen.handle_mouse_down(MS::BTN_START_X + 5, MS::BTN_START_Y + 5, 1);
    screen.handle_key_down(SDLK_DOWN);
    screen.handle_key_down(SDLK_f);
    screen.handle_key_down(SDLK_RETURN);
    check(screen.get_selected_index() == idx && screen.is_fog_of_war_enabled() == fog_before, "room guest: the map and the fog option cannot be changed");
    check(started == 0 && maps_chosen.empty() && fog_chosen.empty(), "room guest: START does nothing and nothing is reported");
    screen.handle_mouse_down(MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5, 1);
    check(left == 1, "room guest: LEAVE quits");
    screen.handle_key_down(SDLK_ESCAPE);
    check(left == 2, "room guest: Esc leaves");
    check(screen.follow_host_choice("SMALL.LVL", true) && screen.get_maps()[static_cast<size_t>(screen.get_selected_index())].filename == "SMALL.LVL" &&
              screen.is_fog_of_war_enabled() && maps_chosen.empty() && fog_chosen.empty(),
          "room guest: follows the host's map and fog without telling anybody");
    check(!screen.follow_host_choice("NOSUCH.LVL", false), "room guest: an unknown map name is refused");

    // not networked: the local screen keeps its single row and its placeholders
    MS local;
    local.init();
    RecordingRenderer lr(arc);
    local.render(lr, arc);
    check(lr.has_sprite_at("thumb1.bmp", 540, 95) && lr.named("thumb2.bmp").empty(), "local setup screen: one row with the good thumb");
    int local_maps = 0;
    local.set_on_map_changed([&](const std::string&) { ++local_maps; });
    local.handle_mouse_down(MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3, 1);
    check(local_maps == 0, "local setup screen: no room to tell about a map change");
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
    test_rubber_band(arc);
    test_screens(arc);
    test_room_screen(arc);
    test_pedestal_chains(arc);
    test_pedestal_timeline(arc);
    test_minimap(arc);
    test_static_shell(arc);
    test_cursor_rules(arc);
    test_button_states(arc);
    std::printf("\nhud layout: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
