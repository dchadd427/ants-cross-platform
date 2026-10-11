#pragma once

// The results screen at 960 x 540 (widescreen work, after the setup screen; the mock-ups were approved).
//
// The original's results screen is a 640 x 480 page, the animation `re_screen` (149 pieces: the frame, the clay tiles, the banner "Game Results", "YOUR SCORE", the art of the column headers, the
// labels "Winner!" and "other players...", and the two black boxes that the rows stand in), the button Leave Game, and the rows that the program adds (the name, four numbers and the ants of each
// team or alliance; FUN_010155ac, docs 5.49). In a 960 x 540 picture the page is not a centred window any more: it is recomposed for the whole canvas from the original's own pieces:
//   * the clay and the frame are the wide pages' ground (wide_page.hpp): the tile clay (the page's art is tile clay) and the wide frame;
//   * what hangs from the top edge stays: the banner (centred: x 300) and the Leave Game button (the top right corner: 845, 12); what is anchored to the left stays at the original's x ("YOUR SCORE",
//     "Winner!", "other players...", the names and the ants); what is anchored to the right moves right by 320 (the art of the column headers, the four number columns, the boxes' right edges);
//     everything in the middle of the page moves down by 30 (the page's own margin below is 0 and becomes 30: the content is centred in the picture's height);
//   * both black boxes are 320 columns wider, built from the original's own pieces as the original builds them from tiles: the top line is one column of efram1100 repeated (all 100 of its columns are
//     identical), the bottom edge is efram4100 (five rows of dithered shadow, which has only two kinds of column) as a chain of runs whose every junction is between identical columns, the corners are
//     efram1c .. efram4c, the inside is one flat black (bg50x100 and efrbg100 are 100 % that colour) and the side pieces rel50 / rer50 are placed whole at the new edges (one for the winner's row, three for
//     the others'). At the original's own width the same construction reproduces the original's boxes exactly (tests/test_app/test_wide_pages.cpp);
//   * the numbers' columns: the original's labels are single-line labels (no wrap) 49 / 19 / 21 / 20 px wide at x 485 / 534 / 555 / 576, left aligned, whose surface clips what is wider; their face
//     (Franklin Gothic Medium at the height 18) has digits 8 px wide, so two digits take 16 of the 19 - 21 px. The bundled substitute face has wider digits (10 px on average): the numbers are drawn
//     squeezed to the original's 8 px a digit and clipped to the label's box (ScorecardModal::render, IRenderer::draw_text_squeezed). Before, "20", "4" and "10" ran into one another ("204 10").
//
// The classic picture is the original's page: its numbers are the layout's classic() rectangles, and nothing of the wide composition is used there.
// All rectangles are half-open (LayoutRect). The geometry is plain numbers; draw_results_art needs a renderer.

#include <array>
#include <cstddef>
#include <cstdint>

#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/wide_page.hpp"
#include "ants_assets/asset_archive.hpp"

namespace ants::app {

/// One black box of the results page as the original builds it, in the original's page coordinates: the box is `top` .. `bottom` + 5 rows, the left corner piece is at `left_corner_x`, the right one at
/// `right_corner_x`; the black inside is the rectangle [fill_x0, fill_x1) x [fill_y0, fill_y1); the side pieces rel50 (6 x 50, left) and rer50 (right) stand at `left_side_x` / `right_side_x`, one
/// every 50 rows from `side_y`. The bottom edge's first visible column of efram4100 is `first_column`; the last visible one equals column `last_column` (what the original shows before the corner).
struct ResultsBox {
    int32_t top{0};
    int32_t bottom{0};
    int32_t left_corner_x{0};
    int32_t right_corner_x{0};
    int32_t fill_x0{0};
    int32_t fill_x1{0};
    int32_t fill_y0{0};
    int32_t fill_y1{0};
    int32_t left_side_x{0};
    int32_t right_side_x{0};
    int32_t side_y{0};
    int32_t side_count{0};
    int32_t first_column{0};
    int32_t last_column{0};
};

/// The two boxes of the original's page (the winner's row, the others' rows)
const ResultsBox& results_winner_box() noexcept;
const ResultsBox& results_others_box() noexcept;

/// Draws a box of the page, moved by (dx, dy) on its right side / down: its left edge stays, its right edge moves by `dx` (the box is `dx` columns wider than the original's), and every row by `dy`.
/// `bottom_edge` is the chain of runs of efram4100 that fills the bottom edge between the corners (box.right_corner_x + dx - (box.left_corner_x + 32) columns). dx = dy = 0 with the chain of the
/// original's width is the original's box.
void draw_results_box(IRenderer& renderer, const ants::assets::AssetArchive& archive, const ResultsBox& box, int32_t dx, int32_t dy, const PieceStrip& bottom_edge);

/// The two bottom-edge chains of the wide boxes (ids "results.winner.bottom" and "results.others.bottom"), for the tests of the seams. `count` is set to their number.
const PieceStrip* results_strips(size_t& count) noexcept;

/// The geometry of the results page: the classic one is the original's, the wide one is the recomposed page at 960 x 540. A rectangle of art is the sprite's rectangle; the outer rectangle of a box.
struct ResultsLayout {
    bool is_wide{false};
    int32_t dx{0};                               // what the right-anchored items move by (320 in the wide page)
    int32_t dy{0};                               // what the content in the middle moves down by (30)
    LayoutRect banner;                           // resbanr.bmp "Game Results"
    LayoutRect your_score;                       // yoscore.bmp
    LayoutRect headers;                          // newstats.bmp: the arrows over the four number columns
    LayoutRect winner_label;                     // winnr.bmp "Winner!"
    LayoutRect others_label;                     // otherp.bmp "other players..."
    LayoutRect winner_box;                       // the winner's black box (outer)
    LayoutRect others_box;                       // the others' black box (outer)
    LayoutRect leave;                            // Leave Game at rest and hovered (leave1, leave2); the hit test is the rectangle of the picture that shows
    LayoutRect leave_pressed;                    // Leave Game pressed (leave3)
    LayoutRect waiting;                          // "Waiting for scores..." (string 111)
    int32_t first_row_y{0};                      // the top row's labels (Y = 235 + dy)
    int32_t other_rows_y{0};                     // the others: Y(i) = row_pitch * i + other_rows_y
    int32_t row_pitch{0};
    int32_t name_x{0};
    int32_t name_w{0};
    std::array<int32_t, 4> column_x{};           // score, friendly lost, enemy killed, new hatched: the labels' left edges
    std::array<int32_t, 4> column_w{};
    int32_t portrait_alone_x{0};                 // an ant of a single team (x), and the two ants of an alliance
    std::array<int32_t, 2> portrait_pair_x{};
    int32_t portrait_dy{0};                      // the ants stand at (x, Y + portrait_dy)

    /// The row of position i (0 = the top row)
    constexpr int32_t row_y(size_t i) const noexcept { return i == 0 ? first_row_y : static_cast<int32_t>(i) * row_pitch + other_rows_y; }

    static const ResultsLayout& classic() noexcept;
    static const ResultsLayout& wide() noexcept;
    static const ResultsLayout& of(bool wide) noexcept { return wide ? ResultsLayout::wide() : ResultsLayout::classic(); }
};

/// The width that the original's face gives a digit at the height of the labels (hdmx of Franklin Gothic Medium at 14 ppem, which a cell height of 18 asks GDI for; docs 5.49): the numbers
/// are drawn squeezed to this many pixels a digit
inline constexpr int32_t kResultsDigitWidth = 8;

/// Draws the static art of the wide results page: the clay and the frame, the banner, "YOUR SCORE", the two boxes, the column headers and the two labels. The rows, the ants and the Leave Game
/// button are the screen's (they change). The classic page is the animation re_screen, drawn by the screen.
void draw_results_art(IRenderer& renderer, const ants::assets::AssetArchive& archive);

}  // namespace ants::app
