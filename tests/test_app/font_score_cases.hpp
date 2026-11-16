// Cases of test_font_numbers.cpp (included there, after its check()): the team scores in the numbers' face (Libre Franklin Black, white, the thick outline; docs/VIEW_AND_HUD.md "Numbers in the game's font"),
// in the two places that draw a team's score besides the score pop-ups:
//   * the HUD's score boxes (the local team's in the top bar, the others' in the bottom strip): the number goes to Renderer::draw_number_text, right aligned in the box, white, 10 pixels of ink,
//     and no digit sprite is drawn for it; a team that is absent or has dropped out shows no number; without the face the old digit sprites are drawn;
//   * the results page: the four number columns of every row (the three of the 187 page): the label's color, from the column's left edge; without the face the old squeezed label is drawn.
// And that a number fits its box at every output size (1:1, a 1280 x 800 window, a phone at 2.1x and a 3x screen): what is drawn stays inside the box, squeezed when it is wider (the draw_text_squeezed rule).
#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <sstream>

#include "ants_app/canvas_layout.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/results_layout.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/screen_layout.hpp"

namespace scorecases {

using namespace ants;
using namespace ants::app;

uint32_t g_clock = 0;
uint32_t clock_fn() { return g_clock; }

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

/// Records the number calls and counts the sprites and texts; `numbers_ok` is what draw_number_text answers
class Spy : public IRenderer {
public:
    struct Num {
        std::string text;
        int32_t x{0}, y{0}, w{0}, h{0};
        bool right{false};
        assets::ColorRGBA color{};
    };
    struct Txt {
        std::string text;
        bool squeezed{false};
        int32_t x{0}, y{0};
    };
    explicit Spy(bool ok) : numbers_ok(ok) {}
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override { ++sprites; }
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override { ++sprites; }
    void draw_sprite_region(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) override { ++sprites; }
    void fill_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA) override { texts.push_back(Txt{s, false, x, y}); }
    void draw_text_squeezed(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA, FontSize, int32_t) override { texts.push_back(Txt{s, true, x, y}); }
    int32_t get_text_width(const std::string& s, FontSize = FontSize::Px12) const override { return static_cast<int32_t>(s.size()) * 6; }
    int32_t get_text_height(FontSize size = FontSize::Px12) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t) override {}
    bool draw_number_text(const std::string& text, int32_t x, int32_t y, int32_t box_w, int32_t ink_h, bool right_align, assets::ColorRGBA color) override {
        if (numbers_ok) numbers.push_back(Num{text, x, y, box_w, ink_h, right_align, color});
        return numbers_ok;
    }
    bool has_text(const std::string& s) const {
        for (const Txt& t : texts) {
            if (t.text == s) return true;
        }
        return false;
    }
    bool numbers_ok;
    int sprites{0};
    std::vector<Num> numbers;
    std::vector<Txt> texts;
};

struct Table {
    sim::SimulationEngine sim;
    ViewportCamera cam;
    Table() {
        sim.init_test_world(60, 60, 5, 600000);
        const sim::TileCoord hills[4] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
        for (uint8_t p = 0; p < 4; ++p) sim.grid_mut().set_anthill(p, hills[p]);
    }
};

void test_hud(const assets::AssetArchive& arc) {
    std::printf("[hud] the score boxes\n");
    for (const ScreenLayout& layout : {ScreenLayout::classic(), ScreenLayout::with_size(960, 540)}) {
        const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
        Table t;
        HUD hud;
        hud.set_ticks_function(&clock_fn);
        hud.init(0);
        hud.set_layout(layout);
        hud.set_team_names({"Alice", "Bob", "Carol", "Dave"});
        hud.set_roster_mask(0x07);
        t.cam.set_view(layout.view());
        sim::WorldState w = t.sim.get_world_state();
        w.player_scores = {1250, 37, 999999, 5};
        for (uint8_t p = 0; p < 4; ++p) w.player_alliances[p] = p;
        w.dropped_mask = 0;
        Spy with(true);
        hud.render(with, arc, w, t.cam);
        Spy without(false);
        hud.render(without, arc, w, t.cam);
        // three teams exist: three boxes with their numbers; Black (team 3) is absent
        check(with.numbers.size() == 3, at + "three teams: three numbers (" + std::to_string(with.numbers.size()) + ")");
        const char* want[3] = {"1250", "37", "999999"};
        const ScoreSlot slots[3] = {layout.score_slot(0), layout.score_slot(1), layout.score_slot(2)};      // (team 0 is the local team: the top bar; teams 1 and 2: the first two bottom slots)
        for (size_t i = 0; i < with.numbers.size() && i < 3; ++i) {
            const Spy::Num* n = nullptr;
            for (const Spy::Num& c : with.numbers) {
                if (c.text == want[i]) n = &c;
            }
            check(n != nullptr, at + std::string("the score ") + want[i] + " goes to the numbers' face");
            if (n == nullptr) continue;
            check(n->right && n->color.r == 255 && n->color.g == 255 && n->color.b == 255 && n->h == 10, at + std::string(want[i]) + ": right aligned, white, 10 pixels of ink");
            bool in_a_box = false;
            for (const ScoreSlot& s : slots) {
                in_a_box = in_a_box || (n->x == s.box_left + 52 && n->y == s.top + 2 && n->w == 51 && n->x - n->w >= s.box_left && n->x <= s.box_left + 54);
            }
            check(in_a_box, at + std::string(want[i]) + ": its right edge and width keep it inside its 54 pixel box (x " + std::to_string(n->x) + ", width " + std::to_string(n->w) + ")");
        }
        // the old digit sprites are not drawn for them: 4 + 2 + 6 digits fewer sprites
        check(without.sprites - with.sprites == 12, at + "no digit sprite is drawn for a number that the face drew (" + std::to_string(without.sprites - with.sprites) + " fewer)");
        check(without.numbers.empty(), at + "without the face the old sprites are drawn (and the call is refused politely)");
        // a team that has dropped out shows no number
        w.dropped_mask = 0x02;
        Spy dropped(true);
        hud.render(dropped, arc, w, t.cam);
        bool shows_37 = false;
        for (const Spy::Num& c : dropped.numbers) shows_37 = shows_37 || c.text == "37";
        check(dropped.numbers.size() == 2 && !shows_37, at + "a team that dropped out: its box is covered and shows no number");
        // the score does not leave 0..999999
        w.dropped_mask = 0;
        w.player_scores = {-4, 1234567, 0, 0};
        Spy clamp(true);
        hud.render(clamp, arc, w, t.cam);
        bool zero = false, six = false;
        for (const Spy::Num& c : clamp.numbers) {
            zero = zero || c.text == "0";
            six = six || c.text == "999999";
        }
        check(zero && six, at + "the number is kept between 0 and 999999, as the old field was");
    }
}

void test_results(const assets::AssetArchive& arc) {
    std::printf("[results] the four number columns (the three of the 187 page)\n");
    for (const sim::GameMode mode : {sim::GameMode::HighestScore, sim::GameMode::Kills187}) {
        const bool is_187 = mode == sim::GameMode::Kills187;
        for (bool wide : {false, true}) {
            const std::string at = std::string(is_187 ? "187, " : "") + (wide ? "wide page: " : "classic page: ");
            sim::MatchResult mr{};
            mr.is_over = true;
            mr.present_mask = 0x0F;
            for (size_t p = 0; p < 4; ++p) {
                mr.ally[p] = sim::ALLIANCE_NONE;
                mr.stats[p].score = static_cast<int32_t>(1250 + 111 * p);
                mr.stats[p].friendly_lost = static_cast<uint32_t>(7 + p);
                mr.stats[p].enemy_killed = static_cast<uint32_t>(30 + p);
                mr.stats[p].new_hatched = 3;
                mr.ants_left[p] = static_cast<uint32_t>(2 + p);
            }
            mr.decide_winners();
            ScorecardModal card;
            card.set_wide_layout(wide);
            card.set_player_names({"Juniper", "Odile", "Marta", "Theo"});
            card.show(mr, 0, mode);
            card.update(0.3f);
            const ResultsLayout& l = ResultsLayout::of(wide);
            const size_t columns = is_187 ? 3 : 4;
            Spy with(true);
            card.render(with, arc);
            check(with.numbers.size() == 4 * columns, at + std::to_string(4 * columns) + " numbers in the numbers' face: four rows of " + std::to_string(columns) + " (" + std::to_string(with.numbers.size()) + ")");
            // every number is where its column is: from the column's left edge (+1), no wider than the column, 12 pixels of ink, left aligned, in the label's color
            bool placed = true, colored = true;
            for (const Spy::Num& n : with.numbers) {
                bool in_a_column = false;
                for (size_t c = 0; c < columns; ++c) {
                    const int32_t cx = is_187 ? l.numbers187_x[c] : l.column_x[c];
                    const int32_t cw = is_187 ? l.numbers187_w[c] : l.column_w[c];
                    in_a_column = in_a_column || (n.x == cx + 1 && n.w == cw - 1);
                }
                placed = placed && in_a_column && !n.right && n.h == 12;
                colored = colored && n.color.r == 239 && n.color.g == 231 && n.color.b == 223;
            }
            check(placed, at + "each starts at its column's left edge, no wider than the column, 12 pixels of ink");
            check(colored, at + "in the label's color");
            for (const char* t : {"1250", "1361", "7", "10", "3"}) {
                bool found = false;
                for (const Spy::Num& n : with.numbers) found = found || n.text == t;
                if (!is_187 || std::string(t) != "3") check(found, at + "the number " + t + " is drawn in the numbers' face");
            }
            check(!with.has_text("1250") && !with.has_text("7"), at + "and not as a label as well");
            check(with.has_text("Juniper") && with.has_text("Odile"), at + "the names stay labels");
            Spy without(false);
            card.render(without, arc);
            check(without.numbers.empty() && without.has_text("1250") && without.has_text("7") && without.has_text("1361"), at + "without the face every number is the old squeezed label");
        }
    }
}

// ---- the fit: a real renderer, a black canvas, one number, the box of what is drawn ----
struct RealRig {
    RealRig(const assets::AssetArchive& archive, int32_t w, int32_t h) : cw(w), ch(h) {
        const QuietStdout quiet;
        win = SDL_CreateWindow("score-numbers", 0, 0, w, h, SDL_WINDOW_HIDDEN);
        if (win == nullptr || !renderer.init(win, archive)) return;
        renderer.set_canvas_size(w, h);
        ok = true;
    }
    ~RealRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    RealRig(const RealRig&) = delete;
    RealRig& operator=(const RealRig&) = delete;
    int32_t cw, ch;
    SDL_Window* win{nullptr};
    Renderer renderer;
    bool ok{false};
};

void test_fit(const assets::AssetArchive& arc) {
    std::printf("[fit] a number stays inside its box at every output size\n");
    RealRig rig(arc, 960, 540);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    check(rig.renderer.font_numbers(), "a face was found");
    struct Size { int w, h; const char* name; };
    const Size sizes[] = {{960, 540, "1:1"}, {1280, 720, "1280 x 720"}, {1280, 800, "1280 x 800"}, {2532, 1170, "a phone at 2.1x"}, {2880, 1620, "3x"}};
    struct Case { const char* text; int x, y, w, h; bool right; const char* name; };
    // the HUD's box: right edge 52 into a 54 pixel box, 51 wide; the results column: 49 wide, from its left edge
    const Case cases[] = {{"999999", 402 + 52, 100, 51, 10, true, "HUD box, 999999"}, {"1250", 402 + 52, 100, 51, 10, true, "HUD box, 1250"}, {"0", 402 + 52, 100, 51, 10, true, "HUD box, 0"},
                          {"999999", 485 + 1, 100, 48, 12, false, "results column 1, 999999"}, {"1250", 485 + 1, 100, 48, 12, false, "results column 1, 1250"},
                          {"99999", 534 + 1, 100, 46, 12, false, "results column 2, 99999"}, {"99999", 555 + 1, 100, 48, 12, false, "results column 3, 99999"}, {"9999", 576 + 1, 100, 47, 12, false, "results column 4, 9999"},
                          {"7", 534 + 1, 100, 46, 12, false, "results column 2, 7"},
                          {"99", 534 + 1, 100, 18, 12, false, "classic page column 2 (19 wide), 99"}, {"999", 555 + 1, 100, 20, 12, false, "classic page column 3 (21 wide), 999"}, {"99", 576 + 1, 100, 19, 12, false, "classic page column 4 (20 wide), 99"}};
    for (const Size& sz : sizes) {
        SDL_SetWindowSize(rig.win, sz.w, sz.h);
        rig.renderer.refit_canvas();
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(rig.renderer.get_sdl_renderer(), &ow, &oh);
        const CanvasFit fit = CanvasLayout{rig.renderer.canvas_w(), rig.renderer.canvas_h()}.fit(ow, oh);      // (the canvas takes the window's shape; the renderer's own arithmetic)
        const double scale = fit.scale;
        const int32_t vx = fit.viewport.x, vy = fit.viewport.y;
        for (const Case& c : cases) {
            rig.renderer.begin_frame();
            rig.renderer.fill_rect(0, 0, rig.renderer.canvas_w(), rig.renderer.canvas_h(), assets::ColorRGBA{0, 0, 0, 255});
            const bool drawn = rig.renderer.draw_number_text(c.text, c.x, c.y, c.w, c.h, c.right, assets::ColorRGBA{255, 255, 255, 255});
            const int vw = fit.viewport.w, vh = fit.viewport.h;                  // (what the screenshot reads: the canvas as the window shows it, without the bars)
            SDL_Rect area{vx, vy, vw, vh};
            std::vector<uint8_t> px(static_cast<size_t>(vw) * static_cast<size_t>(vh) * 4u, 0);
            SDL_RenderReadPixels(rig.renderer.get_sdl_renderer(), &area, SDL_PIXELFORMAT_RGBA32, px.data(), vw * 4);
            int x0 = vw, x1 = -1, y0 = vh, y1 = -1;
            for (int y = 0; y < vh; ++y) {
                for (int x = 0; x < vw; ++x) {
                    const uint8_t* p = &px[(static_cast<size_t>(y) * static_cast<size_t>(vw) + static_cast<size_t>(x)) * 4u];
                    if (p[0] > 40 || p[1] > 40 || p[2] > 40) {
                        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
                    }
                }
            }
            const std::string at = std::string(sz.name) + ", " + c.name + ": ";
            check(drawn && x1 >= x0, at + "drawn");
            if (x1 < x0) continue;
            const double left = x0 / scale, right = (x1 + 1) / scale, top = y0 / scale, bottom = (y1 + 1) / scale;
            const double pox = rig.renderer.picture().x, py = rig.renderer.picture().y;           // (the 4:3 or 16:9 picture inside a wider canvas: every position is moved by its corner)
            const double box_l = pox + (c.right ? c.x - c.w : c.x), box_r = pox + (c.right ? c.x : c.x + c.w);
            check(left >= box_l - 0.8 && right <= box_r + 0.8, at + "inside the box's width (" + std::to_string(left) + " to " + std::to_string(right) + " of " + std::to_string(box_l) + " to " + std::to_string(box_r) + ")");
            check(top >= py + c.y - 3.5 && bottom <= py + c.y + c.h + 3.5, at + "and its height (" + std::to_string(top) + " to " + std::to_string(bottom) + ")");
            const double floor_w = 5.0 * static_cast<double>(std::strlen(c.text));          // (legible: at least 5 canvas pixels a digit, the outline included; three digits in the classic page's 19 to 21 pixels keep it)
            check(right - left >= floor_w, at + "and it is not squeezed away: at least 5 pixels a digit (" + std::to_string(right - left) + " wide, at least " + std::to_string(floor_w) + ")");
        }
    }
}


}  // namespace scorecases
