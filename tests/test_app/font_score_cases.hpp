// Cases of test_font_numbers.cpp (included there, after its check()): the team scores and the match clock in the numbers' face (Libre Franklin Black; docs/VIEW_AND_HUD.md "Numbers in the game's font"),
// in the places that draw them besides the score pop-ups:
//   * the HUD's score boxes (the local team's in the top bar, the others' in the bottom strip): the number goes to Renderer::draw_number_text, right aligned in the box, white, 10 pixels tall, with no outline,
//     and no digit sprite is drawn for it; a team that is absent or has dropped out shows no number; without the face the old digit sprites are drawn;
//   * the match clock of the top bar: every character in the cell of the sprite that it replaces (centred, so that the clock does not move when a digit changes), plain, inside the clock's black box;
//     without the face the digit sprites are drawn;
//   * the results page: the four number columns of every row (the three of the 187 page): the label's color, with the thick outline, from the column's left edge; without the face the old squeezed label.
// And that a number fits its box at every output size (1:1, a 1280 x 800 window, a phone at 2.1x and a 3x screen): what is drawn stays inside the box, squeezed when it is wider (the draw_text_squeezed rule),
// and that the HUD's numbers have no outline pixel (nothing darker than the box behind them) where the results page's have.
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
        NumberAlign align{NumberAlign::Left};
        assets::ColorRGBA color{};
        bool outlined{false};
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
    bool draw_number_text(const std::string& text, int32_t x, int32_t y, int32_t box_w, int32_t ink_h, NumberAlign align, assets::ColorRGBA color, bool outlined) override {
        if (numbers_ok) numbers.push_back(Num{text, x, y, box_w, ink_h, align, color, outlined});
        return numbers_ok;
    }
    /// the numbers that a team's score box got (right aligned); the clock's characters are centred
    std::vector<Num> scores() const {
        std::vector<Num> out;
        for (const Num& n : numbers) {
            if (n.align == NumberAlign::Right) out.push_back(n);
        }
        return out;
    }
    std::vector<Num> clock() const {
        std::vector<Num> out;
        for (const Num& n : numbers) {
            if (n.align == NumberAlign::Centre) out.push_back(n);
        }
        return out;
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
        w.match_time_remaining_ms = 600000;                        // (10:00: the clock has five characters, four digit sprites and the colon without the face)
        const int kClockSprites = 5;
        Spy with(true);
        hud.render(with, arc, w, t.cam);
        Spy without(false);
        hud.render(without, arc, w, t.cam);
        // three teams exist: three boxes with their numbers; Black (team 3) is absent
        check(with.scores().size() == 3, at + "three teams: three numbers (" + std::to_string(with.scores().size()) + ")");
        const char* want[3] = {"1250", "37", "999999"};
        const ScoreSlot slots[3] = {layout.score_slot(0), layout.score_slot(1), layout.score_slot(2)};      // (team 0 is the local team: the top bar; teams 1 and 2: the first two bottom slots)
        const std::vector<Spy::Num> score_numbers = with.scores();
        for (size_t i = 0; i < score_numbers.size() && i < 3; ++i) {
            const Spy::Num* n = nullptr;
            for (const Spy::Num& c : score_numbers) {
                if (c.text == want[i]) n = &c;
            }
            check(n != nullptr, at + std::string("the score ") + want[i] + " goes to the numbers' face");
            if (n == nullptr) continue;
            check(n->align == NumberAlign::Right && n->color.r == 255 && n->color.g == 255 && n->color.b == 255 && n->h == 10, at + std::string(want[i]) + ": right aligned, white, 10 pixels tall");
            check(!n->outlined, at + std::string(want[i]) + ": with no outline");
            bool in_a_box = false;
            for (const ScoreSlot& s : slots) {
                in_a_box = in_a_box || (n->x == s.box_left + 52 && n->y == s.top + 2 && n->w == 51 && n->x - n->w >= s.box_left && n->x <= s.box_left + 54);
            }
            check(in_a_box, at + std::string(want[i]) + ": its right edge and width keep it inside its 54 pixel box (x " + std::to_string(n->x) + ", width " + std::to_string(n->w) + ")");
        }
        // the old digit sprites are not drawn for them: 4 + 2 + 6 digits fewer sprites, and the clock's
        check(without.sprites - with.sprites == 12 + kClockSprites, at + "no digit sprite is drawn for a number that the face drew (" + std::to_string(without.sprites - with.sprites) + " fewer)");
        check(without.numbers.empty(), at + "without the face the old sprites are drawn (and the call is refused politely)");
        // a team that has dropped out shows no number
        w.dropped_mask = 0x02;
        Spy dropped(true);
        hud.render(dropped, arc, w, t.cam);
        bool shows_37 = false;
        for (const Spy::Num& c : dropped.scores()) shows_37 = shows_37 || c.text == "37";
        check(dropped.scores().size() == 2 && !shows_37, at + "a team that dropped out: its box is covered and shows no number");
        // the score does not leave 0..999999
        w.dropped_mask = 0;
        w.player_scores = {-4, 1234567, 0, 0};
        Spy clamp(true);
        hud.render(clamp, arc, w, t.cam);
        bool zero = false, six = false;
        for (const Spy::Num& c : clamp.scores()) {
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
                placed = placed && in_a_column && n.align == NumberAlign::Left && n.h == 12;
                colored = colored && n.color.r == 239 && n.color.g == 231 && n.color.b == 223;
            }
            check(placed, at + "each starts at its column's left edge, no wider than the column, 12 pixels of ink");
            check(colored, at + "in the label's color");
            bool outlined = true;
            for (const Spy::Num& n : with.numbers) outlined = outlined && n.outlined;
            check(outlined, at + "and with the thick outline (the results page keeps it)");
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
    // `x` is the box's right edge (Right), left edge (Left) or middle (Centre), `outlined` the thick outline (the results page) or none (the HUD)
    struct Case { std::string text; int x, y, w, h; NumberAlign align; bool outlined; std::string name; };
    constexpr NumberAlign R = NumberAlign::Right, L = NumberAlign::Left, C = NumberAlign::Centre;
    // the HUD's box: right edge 52 into a 54 pixel box, 51 wide; the results column: 49 wide, from its left edge; the clock's cells: 10 wide (the colon's 7), 10 tall, at y 6, centred
    std::vector<Case> cases = {{"999999", 402 + 52, 100, 51, 10, R, false, "HUD box, 999999"}, {"1250", 402 + 52, 100, 51, 10, R, false, "HUD box, 1250"}, {"0", 402 + 52, 100, 51, 10, R, false, "HUD box, 0"},
                               {"98765", 402 + 52, 100, 51, 10, R, false, "HUD box, 98765"},
                               {"999999", 485 + 1, 100, 48, 12, L, true, "results column 1, 999999"}, {"1250", 485 + 1, 100, 48, 12, L, true, "results column 1, 1250"},
                               {"99999", 534 + 1, 100, 46, 12, L, true, "results column 2, 99999"}, {"99999", 555 + 1, 100, 48, 12, L, true, "results column 3, 99999"}, {"9999", 576 + 1, 100, 47, 12, L, true, "results column 4, 9999"},
                               {"7", 534 + 1, 100, 46, 12, L, true, "results column 2, 7"},
                               {"99", 534 + 1, 100, 18, 12, L, true, "classic page column 2 (19 wide), 99"}, {"999", 555 + 1, 100, 20, 12, L, true, "classic page column 3 (21 wide), 999"}, {"99", 576 + 1, 100, 19, 12, L, true, "classic page column 4 (20 wide), 99"}};
    for (int d = 0; d < 10; ++d) {
        const std::string digit = std::to_string(d);
        cases.push_back({digit, 80 + 5, 100, 10, 10, C, false, "clock cell " + digit + " (minutes)"});
        cases.push_back({digit, 107 + 5, 100, 10, 10, C, false, "clock cell " + digit + " (last)"});
    }
    cases.push_back({":", 90 + 3, 100, 7, 10, C, false, "clock cell, the colon"});
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
            const bool drawn = rig.renderer.draw_number_text(c.text, c.x, c.y, c.w, c.h, c.align, assets::ColorRGBA{255, 255, 255, 255}, c.outlined);
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
            const double box_l = pox + (c.align == R ? c.x - c.w : (c.align == C ? c.x - c.w / 2.0 : c.x)), box_r = box_l + c.w;
            check(left >= box_l - 0.8 && right <= box_r + 0.8, at + "inside the box's width (" + std::to_string(left) + " to " + std::to_string(right) + " of " + std::to_string(box_l) + " to " + std::to_string(box_r) + ")");
            check(top >= py + c.y - 3.5 && bottom <= py + c.y + c.h + 3.5, at + "and its height (" + std::to_string(top) + " to " + std::to_string(bottom) + ")");
            const double digits = static_cast<double>(c.text.size());
            const double floor_w = c.outlined ? 5.0 * digits : (digits > 1.0 ? 4.0 * digits : 0.0);          // (legible: at least 5 canvas pixels a digit, the outline included, 4 without it; three digits in the classic page's 19 to 21 pixels keep it)
            check(right - left >= floor_w, at + "and it is not squeezed away: at least " + std::to_string(c.outlined ? 5 : 4) + " pixels a digit (" + std::to_string(right - left) + " wide, at least " + std::to_string(floor_w) + ")");
            if (!c.outlined && c.text != ":") {         // (plain glyphs: as tall as asked, on the middle of the line; the colon is not a digit's height)
                check(std::fabs((bottom - top) - c.h) <= 1.5, at + "plain glyphs are as tall as asked (" + std::to_string(bottom - top) + " of " + std::to_string(c.h) + ")");
                check(std::fabs((top + bottom) / 2.0 - (py + c.y + c.h / 2.0)) <= 1.2, at + "and centred on the line");
            }
        }
    }
}

// ---- the match clock: the HUD's calls (a recording renderer) ----
// The cells of the sprites that the characters replace (the original's: the digits 10 pixels apart at x 70, 80, 97 and 107, the colon's 7 wide at 90) and their middles
struct ClockCell { int cx, w; };
const ClockCell kClockCells[5] = {{75, 10}, {85, 10}, {93, 7}, {102, 10}, {112, 10}};

std::string clock_text(uint32_t mm, uint32_t ss) {
    char b[16];
    std::snprintf(b, sizeof b, "%u:%02u", static_cast<unsigned>(mm), static_cast<unsigned>(ss));
    return b;
}

Spy hud_with_clock(const assets::AssetArchive& arc, const ScreenLayout& layout, uint32_t mm, uint32_t ss, bool numbers_ok) {
    Table t;
    HUD hud;
    hud.set_ticks_function(&clock_fn);
    hud.init(0);
    hud.set_layout(layout);
    hud.set_roster_mask(0x01);
    t.cam.set_view(layout.view());
    sim::WorldState w = t.sim.get_world_state();
    for (uint8_t p = 0; p < 4; ++p) w.player_alliances[p] = p;
    w.dropped_mask = 0;
    w.match_time_remaining_ms = (mm * 60u + ss) * 1000u;
    Spy spy(numbers_ok);
    hud.render(spy, arc, w, t.cam);
    return spy;
}

void test_clock(const assets::AssetArchive& arc) {
    std::printf("[clock] the match clock in the numbers' face\n");
    // the black box of the top bar's art: x 61 to 128, y 4 to 17 (every pixel of it is one color, and the pixels just outside are not)
    const int kBoxL = 61, kBoxR = 129, kBoxT = 4, kBoxB = 18;           // (right and bottom exclusive)
    const assets::Sprite* top = arc.find_sprite("x0y0.bmp");
    check(top != nullptr, "the top bar's art is there");
    if (top != nullptr) {
        const uint8_t inside = top->get_pixel(static_cast<uint32_t>(kBoxL + 1), static_cast<uint32_t>(kBoxT + 1));
        bool plain_box = true;
        for (int y = kBoxT; y < kBoxB; ++y) {
            for (int x = kBoxL; x < kBoxR; ++x) plain_box = plain_box && top->get_pixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y)) == inside;
        }
        check(plain_box, "the clock's box is one plain color from (61, 4) to (128, 17)");
        check(top->get_pixel(kBoxL - 1, 10) != inside && top->get_pixel(kBoxR, 10) != inside && top->get_pixel(90, kBoxT - 1) != inside && top->get_pixel(90, kBoxB) != inside, "...and it ends there on every side");
    }
    struct Time { uint32_t mm, ss; };
    const Time times[] = {{12, 7}, {9, 41}, {0, 5}, {10, 0}, {59, 59}, {90, 5}, {12, 34}, {11, 11}};
    std::vector<std::pair<std::string, std::vector<std::pair<int, int>>>> placed;        // (the time, the (x, width) of each of its characters)
    for (const ScreenLayout& layout : {ScreenLayout::classic(), ScreenLayout::with_size(960, 540), ScreenLayout::with_size(1260, 540)}) {
        const std::string at = std::string(layout.is_classic() ? "classic" : (layout.width == 960 ? "960 x 540" : "1260 x 540")) + ": ";
        for (const Time& tm : times) {
            const std::string want = clock_text(tm.mm, tm.ss);
            const Spy spy = hud_with_clock(arc, layout, tm.mm, tm.ss, true);
            const std::vector<Spy::Num> clock = spy.clock();
            // the characters: the tens of minutes only from 10:00, the minutes, the colon and the two digits of the seconds
            std::string chars;
            for (const Spy::Num& n : clock) chars += n.text;
            check(chars == want, at + want + ": drawn as the characters " + want + " (" + chars + ")");
            const size_t first = clock.size() == 5 ? 0 : 1;
            bool cells = clock.size() + first == 5, plain = true, inside_box = true;
            std::vector<std::pair<int, int>> xs;
            for (size_t i = 0; i < clock.size() && i + first < 5; ++i) {
                const Spy::Num& n = clock[i];
                const ClockCell& c = kClockCells[i + first];
                cells = cells && n.x == c.cx && n.w == c.w && n.y == 6 && n.h == 10 && n.text.size() == 1;
                plain = plain && !n.outlined && n.color.r == 255 && n.color.g == 255 && n.color.b == 255 && n.color.a == 255;
                inside_box = inside_box && n.x - n.w / 2 >= kBoxL && n.x + (n.w + 1) / 2 <= kBoxR && n.y >= kBoxT && n.y + n.h <= kBoxB;
                xs.push_back({n.x, n.w});
            }
            check(cells, at + want + ": each character is centred in the cell of the sprite it replaces, 10 pixels tall at y 6");
            check(plain, at + want + ": white and with no outline");
            check(inside_box, at + want + ": every cell is inside the clock's box");
            if (layout.width == 960) placed.push_back({want, xs});
            check(spy.sprites == hud_with_clock(arc, layout, 0, 0, true).sprites, at + want + ": no digit sprite is drawn for the clock");
        }
    }
    // a digit that changes does not move the others: the cell of a position is the same for every time (the face's digits are not all as wide)
    auto positions = [&](const std::string& time) {
        for (const auto& p : placed) {
            if (p.first == time) return p.second;
        }
        return std::vector<std::pair<int, int>>{};
    };
    check(!positions("12:34").empty() && positions("12:34") == positions("90:05") && positions("12:34") == positions("11:11"), "the cells of 12:34, 90:05 and 11:11 are the same (the narrow 1 stays in its cell)");
    const auto full = positions("12:07"), short_one = positions("9:41");
    check(full.size() == 5 && short_one.size() == 4 && std::equal(short_one.begin(), short_one.end(), full.begin() + 1), "9:41 is 12:07 without the tens of minutes: the other four stay where they were");
    // without the face: the digit sprites, as before
    {
        const ScreenLayout layout = ScreenLayout::with_size(960, 540);
        const Spy none = hud_with_clock(arc, layout, 12, 7, false);
        const Spy shorter = hud_with_clock(arc, layout, 9, 41, false);
        check(none.numbers.empty() && none.clock().empty(), "without the face no number is drawn");
        check(none.sprites - shorter.sprites == 1, "...and the digit sprites are: 12:07 has one more than 9:41 (" + std::to_string(none.sprites - shorter.sprites) + ")");
        const Spy with_face = hud_with_clock(arc, layout, 12, 7, true);
        check(none.sprites - with_face.sprites == 5 + 1, "...five of them for 12:07: four digits and the colon (and the one digit of the score 0: " + std::to_string(none.sprites - with_face.sprites) + ")");
    }
}

// ---- the HUD over the real renderer: the pixels the player sees ----
struct Output { int win_w, win_h, cw, ch; const char* name; };
const Output kOutputs[] = {{960, 540, 960, 540, "1:1"}, {1280, 800, 960, 600, "a 1280 x 800 window"}, {2532, 1170, 1200, 540, "a phone at 2.1x"}, {2880, 1620, 960, 540, "3x"}};

struct Shot {
    int w{0}, h{0};
    double scale{1.0};
    std::vector<uint8_t> px;
    const uint8_t* at(int x, int y) const { return &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u]; }
};

void set_output(RealRig& rig, const Output& o) {
    SDL_SetWindowSize(rig.win, o.win_w, o.win_h);
    rig.renderer.set_canvas_size(o.cw, o.ch);
    rig.renderer.set_layout(ScreenLayout::with_size(o.cw, o.ch));
}

/// The canvas as the window shows it (the area that the screenshot reads), RGBA
Shot read_canvas(RealRig& rig) {
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(rig.renderer.get_sdl_renderer(), &ow, &oh);
    const CanvasFit fit = CanvasLayout{rig.renderer.canvas_w(), rig.renderer.canvas_h()}.fit(ow, oh);
    Shot s;
    s.w = fit.viewport.w;
    s.h = fit.viewport.h;
    s.scale = fit.scale;
    s.px.assign(static_cast<size_t>(s.w) * static_cast<size_t>(s.h) * 4u, 0);
    SDL_Rect area{fit.viewport.x, fit.viewport.y, s.w, s.h};
    SDL_RenderReadPixels(rig.renderer.get_sdl_renderer(), &area, SDL_PIXELFORMAT_RGBA32, s.px.data(), s.w * 4);
    return s;
}

/// The whole HUD (the four teams, the clock at mm:ss) drawn by the real renderer at an output size
Shot shoot_hud(RealRig& rig, const assets::AssetArchive& arc, const Output& o, const std::array<int32_t, 4>& scores, uint32_t mm, uint32_t ss) {
    const ScreenLayout layout = ScreenLayout::with_size(o.cw, o.ch);
    Table t;
    HUD hud;
    hud.set_ticks_function(&clock_fn);
    hud.init(0);
    hud.set_layout(layout);
    hud.set_team_names({"Alice", "Bob", "Carol", "Dave"});
    hud.set_roster_mask(0x0F);
    t.cam.set_view(layout.view());
    sim::WorldState w = t.sim.get_world_state();
    for (size_t i = 0; i < 4; ++i) w.player_scores[i] = scores[i];
    for (uint8_t p = 0; p < 4; ++p) w.player_alliances[p] = p;
    w.dropped_mask = 0;
    w.match_time_remaining_ms = (mm * 60u + ss) * 1000u;
    rig.renderer.begin_frame();
    hud.render(rig.renderer, arc, w, t.cam);
    return read_canvas(rig);
}

/// How many pixels round a drawing (80 canvas pixels left of `x` to 20 right of it, 90 to 125 down; the canvas's last output column is not covered by the fill at some scales) are darker than the grey behind it
int darker_than_grey(RealRig& rig, int grey, const std::string& text, int x, int w, NumberAlign align, bool outlined, int ch_height) {
    rig.renderer.begin_frame();
    rig.renderer.fill_rect(0, 0, rig.renderer.canvas_w(), rig.renderer.canvas_h(), assets::ColorRGBA{static_cast<uint8_t>(grey), static_cast<uint8_t>(grey), static_cast<uint8_t>(grey), 255});
    rig.renderer.draw_number_text(text, x, 100, w, ch_height, align, assets::ColorRGBA{255, 255, 255, 255}, outlined);
    const Shot s = read_canvas(rig);
    int n = 0;
    for (int y = static_cast<int>(90 * s.scale); y < std::min(s.h, static_cast<int>(125 * s.scale)); ++y) {
        for (int xx = static_cast<int>((x - 80) * s.scale); xx < std::min(s.w, static_cast<int>((x + 20) * s.scale)); ++xx) {
            const uint8_t* p = s.at(xx, y);
            if (p[0] + 4 < grey || p[1] + 4 < grey || p[2] + 4 < grey) ++n;
        }
    }
    return n;
}

void test_hud_pixels(const assets::AssetArchive& arc) {
    std::printf("[pixels] the HUD's numbers have no outline, the results page's have\n");
    RealRig rig(arc, 960, 540);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    check(rig.renderer.font_numbers(), "a face was found");
    const uint8_t bg[4][3] = {{7, 67, 47}, {119, 0, 0}, {43, 39, 107}, {39, 39, 59}};        // (the four boxes' colors, docs 5.53: the numbers' own background)
    for (const Output& o : kOutputs) {
        set_output(rig, o);
        const ScreenLayout layout = ScreenLayout::with_size(o.cw, o.ch);
        const Shot shot = shoot_hud(rig, arc, o, {98765, 4321, 999999, 17}, 12, 7);
        for (int team = 0; team < 4; ++team) {
            const ScoreSlot slot = layout.score_slot(static_cast<size_t>(team));                     // (team 0 is the local team: the top bar's; the others fill the bottom strip's three boxes in order)
            const int x0 = static_cast<int>(std::ceil((slot.box_left + 2) * shot.scale)) + 1, x1 = static_cast<int>(std::floor((slot.box_left + 52) * shot.scale)) - 1;
            const int y0 = static_cast<int>(std::ceil((slot.top + 1) * shot.scale)) + 1, y1 = static_cast<int>(std::floor((slot.top + 13) * shot.scale)) - 1;
            int darker = 0, white = 0;
            for (int y = y0; y < y1 && y < shot.h; ++y) {
                for (int x = x0; x < x1 && x < shot.w; ++x) {
                    const uint8_t* p = shot.at(x, y);
                    if (p[0] + 2 < bg[team][0] || p[1] + 2 < bg[team][1] || p[2] + 2 < bg[team][2]) ++darker;
                    if (p[0] > 200 && p[1] > 200 && p[2] > 200) ++white;
                }
            }
            const std::string at = std::string(o.name) + ", " + (team == 0 ? "the top bar's" : "a bottom strip's") + " score box (team " + std::to_string(team) + "): ";
            check(white > 20, at + "the number is drawn (" + std::to_string(white) + " white pixels)");
            check(darker == 0, at + "no outline: no pixel darker than the box behind the digits (" + std::to_string(darker) + ")");
        }
        // the same call with and without the outline on a grey: the thick outline of the results page is there, the HUD's is not
        const int plain = darker_than_grey(rig, 90, "98765", 200 + 52, 51, NumberAlign::Right, false, 10);
        const int edged = darker_than_grey(rig, 90, "98765", 200 + 52, 51, NumberAlign::Right, true, 12);
        check(plain == 0, std::string(o.name) + ": plain glyphs leave nothing darker than the grey behind them (" + std::to_string(plain) + ")");
        check(edged > 40, std::string(o.name) + ": with the outline the glyphs are surrounded by dark pixels (" + std::to_string(edged) + ")");
    }
    // the fallback face (no Black file: the Medium) has no outline either
    set_output(rig, kOutputs[1]);
    rig.renderer.set_number_font_path("Original-Ants/LibreFranklin-Medium.ttf");
    check(darker_than_grey(rig, 90, "98765", 252, 51, NumberAlign::Right, false, 10) == 0, "the Medium (no Black file): plain glyphs, no outline either");
    check(darker_than_grey(rig, 90, "98765", 252, 51, NumberAlign::Right, true, 12) > 40, "...and the results page's are outlined in it");
    // no face, or the classic numbers: nothing is drawn and the caller draws the digit sprites
    rig.renderer.set_number_font_path("Original-Ants/LibreFranklin-Black.ttf");
    rig.renderer.set_classic_numbers(true);
    check(!rig.renderer.draw_number_text("12", 75, 6, 10, 10, NumberAlign::Centre, assets::ColorRGBA{255, 255, 255, 255}, false) && !rig.renderer.draw_number_text("12", 75, 6, 10, 10, NumberAlign::Right, assets::ColorRGBA{255, 255, 255, 255}, false),
          "the classic numbers: a plain number is refused (the HUD draws its digit sprites)");
    rig.renderer.set_classic_numbers(false);
    rig.renderer.set_number_font_path("");
    check(!rig.renderer.draw_number_text("12", 75, 6, 10, 10, NumberAlign::Centre, assets::ColorRGBA{255, 255, 255, 255}, false), "no face at all: a plain number is refused");
}

void test_clock_pixels(const assets::AssetArchive& arc) {
    std::printf("[clock pixels] the clock stays inside its box at every output size\n");
    RealRig rig(arc, 960, 540);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    // four clocks that differ in every position (every digit 0 to 9 is among them): whatever one draws that another does not shows in the difference
    struct Time { uint32_t mm, ss; };
    const Time times[] = {{12, 34}, {90, 5}, {78, 46}, {56, 13}};
    for (const Output& o : kOutputs) {
        set_output(rig, o);
        std::vector<Shot> shots;
        for (const Time& tm : times) shots.push_back(shoot_hud(rig, arc, o, {1250, 37, 999999, 5}, tm.mm, tm.ss));
        int x0 = shots[0].w, x1 = -1, y0 = shots[0].h, y1 = -1;
        for (size_t a = 0; a < shots.size(); ++a) {
            for (size_t b = a + 1; b < shots.size(); ++b) {
                for (int y = 0; y < shots[a].h; ++y) {
                    for (int x = 0; x < shots[a].w; ++x) {
                        const uint8_t* p = shots[a].at(x, y);
                        const uint8_t* q = shots[b].at(x, y);
                        if (std::abs(p[0] - q[0]) > 2 || std::abs(p[1] - q[1]) > 2 || std::abs(p[2] - q[2]) > 2) {
                            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
                        }
                    }
                }
            }
        }
        const std::string at = std::string(o.name) + ": ";
        check(x1 >= x0, at + "the clock differs between times");
        if (x1 < x0) continue;
        const double s = shots[0].scale;
        const double left = x0 / s, right = (x1 + 1) / s, top = y0 / s, bottom = (y1 + 1) / s;
        check(left >= 61 - 1.0 && right <= 129 + 1.0 && top >= 4 - 1.0 && bottom <= 18 + 1.0,
              at + "everything the clock draws is inside its black box (x " + std::to_string(left) + " to " + std::to_string(right) + " of 61 to 129, y " + std::to_string(top) + " to " + std::to_string(bottom) + " of 4 to 18)");
        for (size_t i = 0; i < shots.size(); ++i) {
            int white = 0;
            for (int y = static_cast<int>(std::ceil(4 * s)); y < static_cast<int>(18 * s); ++y) {
                for (int x = static_cast<int>(std::ceil(61 * s)); x < static_cast<int>(129 * s); ++x) {
                    const uint8_t* p = shots[i].at(x, y);
                    if (p[0] > 200 && p[1] > 200 && p[2] > 200) ++white;
                }
            }
            check(white > 30, at + clock_text(times[i].mm, times[i].ss) + ": the clock is drawn in white (" + std::to_string(white) + " pixels)");
        }
    }
}

}  // namespace scorecases
