#pragma once

// The results screen in the new look, laid out for any size of picture (docs/VIEW_AND_HUD.md "Results Screen"; the HUD art's pieces for it: tools/hud_art/results_art.py).
//
// The page is drawn in UNITS, and a unit is a whole number `k` of device pixels: the largest k that leaves at least 360 units on the picture's shorter side (1 at 640 x 480, 2 at 1280 x 720, 3 at 1920 x 1080 and
// on a phone, 4 at 2560 x 1440), as the countdown card does (countdown_card.hpp). The clay panel fills the whole picture (nine pieces serve every size), and the content is one column of at most 760
// units, centered. A picture 520 units wide or more gets a table (the number columns right aligned under their headings); a narrower one gets a card for each player, with the numbers under the name and
// a big Leave Game button at the bottom (when the height holds the cards: four teams in a picture 360 units high get a compact table instead). Which pictures are that narrow: no 16:9 or wider canvas (they
// are at least 640 units wide, with 360 on the shorter side), and a phone held upright has no canvas of its own yet (the shape table has landscape shapes only: it gets the 16:9 picture, a table); today
// it is the 4:3 canvas when one of its pixels is 1.5 or 3 device pixels (640 x 480 in a window of 960 x 720, or on a pixel ratio of 3): 480 x 360 units. The layout is made for any shape all the same,
// the canvases of a taller shape included. When the height is short (four teams, the 187 page's headline), "Your Score" becomes a small heading in the row of the column headings and the rows get
// lower. Everything here is whole units and no renderer: a test runs it at any size.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "ants_app/screen_layout.hpp"

namespace ants::app::scorecard_layout {

inline constexpr int32_t kMinShortSide = 360;       // units on the shorter side of the picture
inline constexpr int32_t kNarrowBelow = 520;        // a picture narrower than this (in units) stacks the numbers under the names
inline constexpr int32_t kMaxContent = 760;         // the widest column of content, in units
inline constexpr size_t kMaxRows = 4;

/// The HUD art's pieces of the screen, by name (tools/hud_art/results_art.py draws them): a set that lacks one of them has no new results screen (window_art::has_results_page)
inline constexpr std::array<const char*, 40> kSprites = {
    "sc_title", "sc_score_38", "sc_score_33", "sc_score_28", "sc_score_s", "sc_winner", "sc_others",
    "sc_h_score", "sc_h_friendly", "sc_h_enemy", "sc_h_new", "sc_s_lost", "sc_s_killed", "sc_s_hatched", "sc_h_kills", "sc_h_antslost", "sc_h_antsleft",
    "sc_well_tl", "sc_well_t", "sc_well_tr", "sc_well_l", "sc_well_r", "sc_well_bl", "sc_well_b", "sc_well_br",
    "sc_leave_up", "sc_leave_hover", "sc_leave_down",
    "sc_btn_l_up", "sc_btn_m_up", "sc_btn_r_up", "sc_btn_l_hover", "sc_btn_m_hover", "sc_btn_r_hover", "sc_btn_l_down", "sc_btn_m_down", "sc_btn_r_down",
    "sc_leave_word_up", "sc_leave_word_hover", "sc_leave_word_down"};

/// The sizes the art draws the pieces at, in units (results_art.py): a well's corner and the run of its edge, the big button's end and run, the small button's face (+ 3 across, + 4 down for its cast shadow)
inline constexpr int32_t kWellCorner = 12;
inline constexpr int32_t kWellRun = 128;
inline constexpr int32_t kWellRunY = 32;
inline constexpr int32_t kLeaveFaceW = 99;           // the corner button's face, and its picture (the face and its cast shadow: 3 across, 4 down)
inline constexpr int32_t kLeaveFaceH = 22;
inline constexpr int32_t kLeaveW = kLeaveFaceW + 3;
inline constexpr int32_t kLeaveH = kLeaveFaceH + 4;
inline constexpr int32_t kBigEnd = 12;
inline constexpr int32_t kBigRun = 128;
inline constexpr int32_t kBigH = 44;

/// How the page sits on the canvas: `k` device pixels to a unit, `f` canvas pixels to a unit (k over how many device pixels a canvas pixel is), the HUD art's sheet whose pixels are whole device pixels at that
/// scale (1, 2 or 3: a unit of 4 draws the 3x sheet scaled, as the countdown card does), and the canvas in units (the page is all of it)
struct Scale {
    int32_t k{1};
    double f{1.0};
    int32_t sheet{1};
    int32_t w{0};
    int32_t h{0};
    int32_t canvas_w{0};
    int32_t canvas_h{0};

    /// Where a unit edge falls on the canvas, in whole pixels: the last edge of the page is the canvas's own, so the panel covers all of it
    int32_t x(int32_t u) const noexcept { return u >= w ? canvas_w : static_cast<int32_t>(std::lround(static_cast<double>(u) * f)); }
    int32_t y(int32_t u) const noexcept { return u >= h ? canvas_h : static_cast<int32_t>(std::lround(static_cast<double>(u) * f)); }
    /// A rectangle in units as a rectangle of the canvas: its edges are rounded, so that neighbors share an edge
    LayoutRect rect(int32_t ux, int32_t uy, int32_t uw, int32_t uh) const noexcept {
        const int32_t x0 = x(ux), y0 = y(uy), x1 = x(ux + uw), y1 = y(uy + uh);
        return LayoutRect{x0, y0, x1 - x0, y1 - y0};
    }
    LayoutRect rect(const LayoutRect& u) const noexcept { return rect(u.x, u.y, u.w, u.h); }
    /// A size in units as the em of a line of text, in tenths of a canvas pixel
    int32_t em_tenths(double units) const noexcept { return static_cast<int32_t>(std::lround(units * f * 10.0)); }
    /// The page can be drawn: the picture holds at least kMinShortSide units on its shorter side (a window of fewer device pixels than that keeps the original's page)
    bool usable() const noexcept { return w > 0 && h > 0 && (w < h ? w : h) >= kMinShortSide; }
};

/// The scale for a canvas of canvas_w x canvas_h pixels that the output shows as `output` device pixels to a canvas pixel
inline Scale scale_for(int32_t canvas_w, int32_t canvas_h, double output) noexcept {
    Scale s;
    s.canvas_w = canvas_w;
    s.canvas_h = canvas_h;
    const double o = output > 0.0 ? output : 1.0;
    const double shorter = static_cast<double>(canvas_w < canvas_h ? canvas_w : canvas_h);
    s.k = static_cast<int32_t>(std::floor(shorter * o / static_cast<double>(kMinShortSide) + 1e-3));         // (the output scale is SDL's arithmetic in single precision: a hair under a whole number is that number)
    if (s.k < 1) s.k = 1;
    s.f = static_cast<double>(s.k) / o;
    s.sheet = s.k > 3 ? 3 : s.k;
    s.w = static_cast<int32_t>(std::floor(static_cast<double>(canvas_w) / s.f + 1e-3));
    s.h = static_cast<int32_t>(std::floor(static_cast<double>(canvas_h) / s.f + 1e-3));
    return s;
}

/// What the page shows
struct Input {
    int32_t w{0};                       // the page in units
    int32_t h{0};
    size_t rows{1};                     // one row for every team or alliance (the winner's first), 1 to 4
    int32_t columns{4};                 // the numbers of a row: 4 (score, friendly lost, enemy killed, new hatched), 3 on the 187 page (kills, ants lost, ants left)
    int32_t headline_lines{0};          // the 187 page's headline: 0 (none), 1 or 2 lines
};

/// The size of "Your Score": a big one of 38, 33 or 28 units to the capital, or the small heading (0)
enum class ScoreSize : uint8_t { Small, Big28, Big33, Big38 };

/// One row of a player: a well's inside
struct RowSlots {
    int32_t top{0};
    int32_t height{0};
    int32_t ants_left{0};               // the left edge of the first ant (the box of one ant stands in the middle of the slot of two)
    int32_t name_x{0};                  // the name's left edge, and the most it may take
    int32_t name_w{0};
    int32_t name_base{0};               // baselines of the name, the numbers and (a card) the little labels under them
    int32_t number_base{0};
    int32_t label_base{0};
    std::array<int32_t, 4> number_x{};  // a table: the right edge of each number; a card: the middle of it
    int32_t number_w{0};                // the most that a number may take (a table: to the left of number_x; a card: centered on it)
};

struct Layout {
    int32_t w{0};
    int32_t h{0};
    bool narrow{false};                 // a card for each player
    bool ok{false};                     // false: nothing to show (no row)
    int32_t unit_text{15};              // the em of the names and numbers, in units
    LayoutRect content;                 // the column: x, width

    int32_t title_cx{0};                // "Game Results": the middle and the baseline
    int32_t title_base{0};
    int32_t headline_cx{0};             // the 187 page's headline: the middle of its lines, the baseline of the first and the pitch
    int32_t headline_base{0};
    int32_t headline_pitch{16};
    int32_t headline_w{0};              // the width its lines may take

    ScoreSize score{ScoreSize::Small};
    int32_t score_x{0};                 // big: the left edge of the letters (a table) or the middle (a card); small: the middle
    int32_t score_base{0};
    bool score_centered{false};

    bool headings{false};               // a table has the column headings
    std::array<int32_t, 4> column_right{};
    int32_t heading_base{0};
    int32_t sub_base{0};

    int32_t label_x{0};
    int32_t winner_base{0};             // "Winner!"
    int32_t others_base{0};             // "other players..." (only with more than one row)
    LayoutRect winner_well;             // the dark ground of the winner's well, and the others' well (empty without other rows)
    LayoutRect others_well;
    std::array<RowSlots, kMaxRows> row{};
    size_t row_count{0};
    int32_t divider_x{0};               // the hairlines between the others' rows: at each row's top but the first
    int32_t divider_w{0};

    LayoutRect leave;                   // the face of Leave Game (its hit rectangle)
    bool leave_big{false};              // the card layout's wide button
    int32_t leave_word_base{0};         // the big button's word: the baseline (its middle is the face's)
    LayoutRect waiting;                 // where "Waiting for scores..." stands before the rows (a box; the sentence is centered in it)
};

Layout compute(const Input& in);

}  // namespace ants::app::scorecard_layout
