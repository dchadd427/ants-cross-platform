#pragma once

// The results screen in the new look, laid out for any size of picture (docs/VIEW_AND_HUD.md "The new look of the results screen"; the HUD art's pieces for it: tools/hud_art/results_art.py).
//
// The page is drawn in UNITS, and a unit is a whole number `k` of device pixels: the largest k that leaves at least 360 units on the picture's shorter side (1 at 640 x 480, 2 at 1280 x 720, 3 at 1920 x 1080 and
// on a phone, 4 at 2560 x 1440), as the countdown card does (countdown_card.hpp); a landscape picture that would be narrower than 520 units takes the next smaller unit (the 4:3 canvas at 1.5 device pixels to a
// canvas pixel is 960 x 720 units at 1, not 480 x 360 at 2). The clay panel fills the whole picture (nine pieces serve every size) and the content is one column of at most 760 units, centered.
//
// Three pages, all plain numbers and no renderer (a test runs them at any size):
//   the podium   Victory! / Defeat, the winner's well (the ants at double size, the score large, the other numbers and the damage as tiles), the others ranked in a well of rows with a bar of their
//                score, and, when the match's numbers were counted, the honors and the Play of the game under them and the link "All stats" to the second page over them. A picture 520 units wide or more is
//                laid out in two columns; a narrower one (a phone held upright: the shape table has landscape shapes only, so the game cannot make one yet) stacks everything in one, with Leave Game as a big
//                button at the bottom. After a lobby room's match Leave Game is two buttons, "Back to the room" and "Back to Ants & Uncles" (the same place: two big buttons one above the other on a narrow page)
//   waiting      "Game Over" and one line, for the quarter second before the scores are in
//   the stats    "Match stats": a row for each player and the numbers of the second page in groups, and the link "Back to results" where the first page has "All stats"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_app/screen_layout.hpp"

namespace ants::app::scorecard_layout {

inline constexpr int32_t kMinShortSide = 360;       // units on the shorter side of the picture
inline constexpr int32_t kNarrowBelow = 520;        // a picture narrower than this (in units) stacks the page in one column
inline constexpr int32_t kNarrowMinHeight = 560;    // ... and then needs this many units of height to hold the column and the two big buttons of a room's match (a window of a 4:3 canvas under 520 x 560 units keeps the original's page)
inline constexpr int32_t kMaxContent = 760;         // the widest column of content, in units
inline constexpr size_t kMaxRows = 4;

/// The HUD art's pieces of the screen, by name (tools/hud_art/results_art.py draws them): a set that lacks one of them has no new results screen (window_art::has_results_page)
inline constexpr std::array<const char*, 79> kSprites = {
    "sc_victory_26", "sc_victory_22", "sc_defeat_26", "sc_defeat_22", "sc_gameover_36", "sc_gameover_26", "sc_matchstats_15", "sc_others", "sc_g_damage", "sc_g_food",
    "sc_g_special", "sc_s_done", "sc_s_taken", "sc_s_trips", "sc_s_points", "sc_s_bombs", "sc_s_fires", "sc_s_raids", "sc_s_loot", "sc_k_winner", "sc_k_colony",
    "sc_k_play", "sc_k_mvp", "sc_k_blood", "sc_k_thief", "sc_k_stand", "sc_well_tl", "sc_well_t", "sc_well_tr", "sc_well_l", "sc_well_r", "sc_well_bl", "sc_well_b",
    "sc_well_br", "sc_leave_up", "sc_leave_hover", "sc_leave_down", "sc_watch_up", "sc_watch_hover", "sc_watch_down", "sc_btn_l_up", "sc_btn_m_up", "sc_btn_r_up",
    "sc_btn_l_hover", "sc_btn_m_hover", "sc_btn_r_hover", "sc_btn_l_down", "sc_btn_m_down", "sc_btn_r_down", "sc_leave_word_up", "sc_leave_word_hover",
    "sc_leave_word_down", "sc_sm_l_up", "sc_sm_m_up", "sc_sm_r_up", "sc_sm_l_hover", "sc_sm_m_hover", "sc_sm_r_hover", "sc_sm_l_down", "sc_sm_m_down", "sc_sm_r_down",
    "sc_sw_allstats_up", "sc_sw_allstats_hover", "sc_sw_allstats_down", "sc_sw_back_up", "sc_sw_back_hover", "sc_sw_back_down", "sc_sw_room_up", "sc_sw_room_hover",
    "sc_sw_room_down", "sc_sw_hub_up", "sc_sw_hub_hover", "sc_sw_hub_down", "sc_bw_room_up", "sc_bw_room_hover", "sc_bw_room_down", "sc_bw_hub_up", "sc_bw_hub_hover",
    "sc_bw_hub_down"
};

/// The sizes the art draws the pieces at, in units (results_art.py): a well's corner and the run of its edge, the ends and the run of the buttons that are as wide as the screen asks (face 40 high: the big
/// buttons; face 22 high: the wide screen's buttons and the link to the other page; with the cast shadow 3 across and 4 down), the buttons that are whole pictures (Leave Game and Watch: the face and its cast shadow)
inline constexpr int32_t kWellCorner = 12;
inline constexpr int32_t kWellRun = 128;
inline constexpr int32_t kWellRunY = 32;
inline constexpr int32_t kLeaveFaceW = 99;
inline constexpr int32_t kLeaveFaceH = 22;
inline constexpr int32_t kLeaveW = kLeaveFaceW + 3;
inline constexpr int32_t kLeaveH = kLeaveFaceH + 4;
inline constexpr int32_t kWatchFaceW = 58;
inline constexpr int32_t kWatchFaceH = 20;
inline constexpr int32_t kWatchW = kWatchFaceW + 3;
inline constexpr int32_t kWatchH = kWatchFaceH + 4;
inline constexpr int32_t kBigEnd = 12;
inline constexpr int32_t kBigRun = 128;
inline constexpr int32_t kBigFaceH = 40;
inline constexpr int32_t kBigH = kBigFaceH + 4;
inline constexpr int32_t kSmallFaceH = 22;          // a small button of the wide screen and the link to the other page
inline constexpr int32_t kSmallH = kSmallFaceH + 4;
/// The faces of the small buttons, in units: the word that each carries (results_art.py: 59, 99, 106 and 132 wide) with 12 of air on either side
inline constexpr int32_t kAllStatsFaceW = 84;       // "All stats ▸"
inline constexpr int32_t kBackFaceW = 124;          // "◂ Back to results"
inline constexpr int32_t kRoomFaceW = 130;          // "Back to the room"
inline constexpr int32_t kHubFaceW = 156;           // "Back to Ants & Uncles"
/// The ant's picture, as the page counts on it (the game's own sprites: the width and height of the first frame's box)
inline constexpr int32_t kAntW = 25;
inline constexpr int32_t kAntH = 42;
inline constexpr int32_t kAntHeadH = 24;            // the part of an ant that a ranked row shows

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
    /// The page can be drawn: the picture holds at least kMinShortSide units on its shorter side, and is two columns wide or tall enough for one (a window of fewer device pixels than that keeps the original's page)
    bool usable() const noexcept { return w > 0 && h > 0 && (w < h ? w : h) >= kMinShortSide && (w >= kNarrowBelow || h >= kNarrowMinHeight); }
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
    while (s.k > 1 && canvas_w >= canvas_h && std::floor(static_cast<double>(canvas_w) * o / static_cast<double>(s.k) + 1e-3) < static_cast<double>(kNarrowBelow)) --s.k;     // (a landscape picture is not narrow)
    s.f = static_cast<double>(s.k) / o;
    s.sheet = s.k > 3 ? 3 : s.k;
    s.w = static_cast<int32_t>(std::floor(static_cast<double>(canvas_w) / s.f + 1e-3));
    s.h = static_cast<int32_t>(std::floor(static_cast<double>(canvas_h) / s.f + 1e-3));
    return s;
}

/// A named rectangle of what a page holds (its wells, buttons, cards and the boxes of its text), for the tests: nothing may leave the page or lie over another
struct Part {
    std::string name;
    LayoutRect rect;
};

// ---- the podium ------------------------------------------------------------------------------------------------------------------------------------------------

struct PodiumInput {
    int32_t w{0};                       // the page in units
    int32_t h{0};
    size_t rows{1};                     // one row for every team or alliance (the winner's first), 1 to 4
    int32_t winner_ants{1};             // the winner's ants (an alliance has two)
    std::array<int32_t, 3> other_ants{{1, 1, 1}};      // the ants of each of the others' rows
    bool report{false};                 // the match's numbers were counted: the link to the second page, the damage, the honors and the play
    bool play{false};                   // ... and there is a Play of the game to show
    bool is_187{false};                 // the 187 page: the tiles are Lost, Left (and Damage)
    int32_t title_w{0};                 // the width of the title's lettering (the sentence stands after it on a wide page)
    bool choice{false};                 // a room's match: "Back to the room" and "Back to Ants & Uncles" in place of Leave Game
};

/// The tile of a number under the winner's score
struct Tile {
    int32_t x{0};                       // its middle
};

/// One ranked row of the others' well
struct OtherRow {
    LayoutRect box;                     // the row (its hairline is at the top, for the second row on)
    int32_t rank_x{0};                  // the middle of the rank number, and its baseline
    int32_t rank_base{0};
    int32_t head_x{0};                  // where the first ant's head stands (the second is 27 further right), and the row's own top for the head
    int32_t head_y{0};
    int32_t name_x{0};                  // the name's left edge and the right edge that it and the score share (the score ends at score_right)
    int32_t name_base{0};
    int32_t name_right{0};
    int32_t score_right{0};
    int32_t score_base{0};
    LayoutRect bar;                     // the bar of the score
    int32_t small_base{0};              // the line of the little numbers under the bar
    int32_t pair_base[2]{0, 0};         // an alliance: its two names on two lines (the first ends in " &"), then the bar and the little numbers a little lower
    LayoutRect pair_bar;
    int32_t pair_small_base{0};
};

struct Podium {
    int32_t w{0};
    int32_t h{0};
    bool ok{false};
    bool narrow{false};
    bool report{false};
    LayoutRect content;                 // x, width

    bool link_shown{false};             // the link to the second page ("All stats"): there are counted numbers
    LayoutRect link;                    // its face (its hit rectangle)
    LayoutRect leave;                   // the face of Leave Game (its hit rectangle); in a room's match "Back to the room"
    LayoutRect hub;                     // a room's match: "Back to Ants & Uncles" (empty otherwise)
    bool choice{false};                 // a room's match: the two buttons stand where Leave Game stands
    bool leave_big{false};
    int32_t leave_word_base{0};
    int32_t hub_word_base{0};

    bool title_big{true};               // the title at 26 units to the capital (22 on a narrow page)
    int32_t title_x{0};                 // a wide page: the left edge of the title's letters; a narrow page: the middle
    bool title_centered{false};
    int32_t title_base{0};
    int32_t sentence_x{0};              // the left edge (wide) or the middle (narrow) of the sentence, its baseline and the room it has
    int32_t sentence_base{0};
    int32_t sentence_w{0};

    LayoutRect winner_well;             // the dark ground
    int32_t winner_label_x{0};
    int32_t winner_label_base{0};
    int32_t ants_x{0};                  // the first ant (the second is 52 further right), double size
    int32_t ants_y{0};
    int32_t name_x{0};                  // the name, the score and the tiles stand right of the ants
    int32_t name_base{0};
    int32_t name_pair_base[2]{0, 0};    // an alliance: its two names on two lines (the first ends in " &")
    int32_t name_w{0};
    int32_t score_base{0};
    std::array<Tile, 4> tile{};
    size_t tile_count{0};
    int32_t tile_base{0};
    int32_t tile_label_base{0};

    int32_t others_label_x{0};
    int32_t others_label_base{0};
    LayoutRect others_well;
    std::array<OtherRow, 3> other{};
    size_t other_count{0};

    LayoutRect play;                    // the Play of the game's well, its picture, its text and Watch
    LayoutRect play_picture;
    int32_t play_text_x{0};
    int32_t play_label_base{0};
    int32_t play_line_base{0};          // the first line of the sentence; the next is `play_pitch` lower, then the time
    int32_t play_pitch{14};
    LayoutRect watch;                   // the face of Watch
    bool honors{false};
    std::array<LayoutRect, 3> honor{};  // the three wells
    int32_t honor_label_base{0};        // from the well's top
    int32_t honor_ant_y{0};
    int32_t honor_ant_h{kAntH};         // the ant of a card is drawn this high (a low card has a smaller ant)
    int32_t honor_name_base{0};
    int32_t honor_detail_base{0};

    std::vector<Part> parts;            // for the tests
};

/// The podium for a page of `in.w` x `in.h` units
Podium compute_podium(const PodiumInput& in);

// ---- the waiting page --------------------------------------------------------------------------------------------------------------------------------------------

struct Waiting {
    bool ok{false};
    bool narrow{false};
    bool title_big{true};               // 36 units to the capital (26 on a narrow page)
    int32_t title_cx{0};
    int32_t title_base{0};
    int32_t sentence_base{0};
    int32_t sentence_w{0};
    LayoutRect leave;
    LayoutRect hub;
    bool choice{false};
    bool leave_big{false};
    int32_t leave_word_base{0};
    int32_t hub_word_base{0};
    std::vector<Part> parts;
};
Waiting compute_waiting(int32_t w, int32_t h, bool choice = false);

// ---- the second page ----------------------------------------------------------------------------------------------------------------------------------------------

struct StatsInput {
    int32_t w{0};
    int32_t h{0};
    size_t seats{2};                    // a row for every player, 1 to 4
    bool is_187{false};                 // two groups of numbers (Damage, Special ants) in place of three
    bool choice{false};                 // a room's match (see PodiumInput)
};

struct StatsPage {
    bool ok{false};
    bool narrow{false};
    LayoutRect content;
    LayoutRect link;                    // "Back to results"
    LayoutRect leave;
    LayoutRect hub;
    bool choice{false};
    bool leave_big{false};
    int32_t leave_word_base{0};
    int32_t hub_word_base{0};
    int32_t title_cx{0};                // the middle of "Match Stats", and its baseline (a wide page)
    int32_t title_base{0};
    LayoutRect well;
    std::array<int32_t, 8> column_right{};        // the numbers' right edges (a wide page) or middles (a narrow page: the card's grid)
    size_t columns{0};
    std::array<int32_t, 3> group_cx{};            // the middle of each group's heading, its baseline, and the line under it (x0, x1 of each)
    std::array<LayoutRect, 3> group_line{};
    size_t groups{0};
    int32_t group_base{0};
    int32_t sub_base{0};
    struct Seat {
        LayoutRect box;
        int32_t ant_x{0};
        int32_t ant_y{0};
        int32_t name_x{0};
        int32_t name_base{0};
        int32_t name_w{0};
        std::array<int32_t, 8> number_x{};
        std::array<int32_t, 8> number_base{};
        std::array<int32_t, 8> label_base{};      // a narrow page: the little label under each number
    };
    std::array<Seat, 4> seat{};
    size_t seat_count{0};
    int32_t number_w{0};                // the most that a number may take
    std::vector<Part> parts;
};
StatsPage compute_stats(const StatsInput& in);

}  // namespace ants::app::scorecard_layout
