#include "ants_app/results_layout.hpp"

#include <array>

#include "ants_app/scorecard.hpp"

namespace ants::app {

namespace {

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The bottom edges of the wide boxes: efram4100 (100 columns of five rows of dithered shadow; the piece has only two kinds of column: 56 columns equal to column 71 and 44 equal to column 50) as runs of
// columns. Every junction between runs that do not follow each other in the piece is between identical columns, and the last column shown is identical to the one that the original shows before the
// corner (tests/test_app/test_wide_pages.cpp checks each one against ants.chd). Found by the mock-up tool (a search for the fewest jumps between identical columns).
//   winner box: columns 28 .. 99, seven whole tiles (the wrap 99 | 0 is the piece's own join), 0 .. 50, then column 49 (column 50 equals column 48, so the pair 50 | 49 is one that the piece
//               holds; column 49 equals column 71): 824 columns
//   others box: columns 27 .. 99, seven tiles, 0 .. 53 (column 53 equals column 73): 827 columns
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr LineSpan kWinnerBottomSpans[] = {{28, 72, 72}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 51, 51}, {49, 1, 1}};   // 824 columns
constexpr LineSpan kOthersBottomSpans[] = {{27, 73, 73}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 54, 54}};             // 827 columns

template <size_t N>
constexpr int32_t total_lines(const LineSpan (&spans)[N]) {
    int32_t sum = 0;
    for (size_t i = 0; i < N; ++i) sum += spans[i].dst_count;
    return sum;
}

#define ANTS_RESULTS_STRIP(id, spans) PieceStrip{id, "efram4100.bmp", true, 0, 0, total_lines(spans), spans, sizeof(spans) / sizeof(spans[0])}

const std::array<PieceStrip, 2> kStrips = {{
    ANTS_RESULTS_STRIP("results.winner.bottom", kWinnerBottomSpans),
    ANTS_RESULTS_STRIP("results.others.bottom", kOthersBottomSpans),
}};

#undef ANTS_RESULTS_STRIP

// The original's boxes (the page's own coordinates): the winner's row and the others' rows
constexpr ResultsBox kWinnerBox{218, 272, 36, 572, 40, 600, 222, 272, 35, 598, 222, 1, 28, 71};
constexpr ResultsBox kOthersBox{306, 460, 35, 574, 40, 600, 310, 460, 34, 600, 310, 3, 27, 73};

ResultsLayout make_classic() {
    ResultsLayout l;
    l.is_wide = false;
    l.banner = {140, 0, 340, 34};
    l.your_score = {41, 55, 302, 127};
    l.headers = {342, 84, 259, 133};
    l.winner_label = {40, 195, 117, 19};
    l.others_label = {40, 280, 203, 25};
    l.winner_box = {35, 218, 569, 59};
    l.others_box = {34, 306, 572, 159};
    l.leave = {ScorecardModal::QUIT_BTN_X, ScorecardModal::QUIT_BTN_Y, ScorecardModal::QUIT_BTN_W, ScorecardModal::QUIT_BTN_H};
    l.leave_pressed = {ScorecardModal::QUIT_BTN_PRESSED.x, ScorecardModal::QUIT_BTN_PRESSED.y, ScorecardModal::QUIT_BTN_PRESSED.w, ScorecardModal::QUIT_BTN_PRESSED.h};
    l.waiting = {ScorecardModal::WAITING_X, ScorecardModal::WAITING_Y, ScorecardModal::WAITING_W, ScorecardModal::LABEL_H};
    l.first_row_y = ScorecardModal::FIRST_ROW_Y;
    l.other_rows_y = ScorecardModal::OTHER_ROWS_Y;
    l.row_pitch = ScorecardModal::ROW_PITCH;
    l.name_x = ScorecardModal::NAME_X;
    l.name_w = ScorecardModal::NAME_W;
    for (size_t c = 0; c < 4; ++c) {
        l.column_x[c] = ScorecardModal::COLUMN_X[c];
        l.column_w[c] = ScorecardModal::COLUMN_W[c];
    }
    l.portrait_alone_x = ScorecardModal::PORTRAIT_X_ALONE;
    l.portrait_pair_x = {ScorecardModal::PORTRAIT_X_PAIR[0], ScorecardModal::PORTRAIT_X_PAIR[1]};
    l.portrait_dy = ScorecardModal::PORTRAIT_Y_OFFSET;
    return l;
}

// The wide page: the right items move by 320 (the headers, the number columns, the Leave Game button and the boxes' right edges), what is in the middle moves down by 30, the banner hangs from the
// top edge and is centred (x 300: the original's x 140 plus the page's 160), what is anchored left stays
ResultsLayout make_wide() {
    ResultsLayout l = make_classic();
    l.is_wide = true;
    l.dx = kWidePageDx;
    l.dy = 30;
    l.banner = {300, 0, 340, 34};
    l.your_score = l.your_score.moved(0, l.dy);
    l.headers = l.headers.moved(l.dx, l.dy);
    l.winner_label = l.winner_label.moved(0, l.dy);
    l.others_label = l.others_label.moved(0, l.dy);
    l.winner_box = LayoutRect{l.winner_box.x, l.winner_box.y + l.dy, l.winner_box.w + l.dx, l.winner_box.h};
    l.others_box = LayoutRect{l.others_box.x, l.others_box.y + l.dy, l.others_box.w + l.dx, l.others_box.h};
    l.leave = l.leave.moved(l.dx, 0);
    l.leave_pressed = l.leave_pressed.moved(l.dx, 0);
    l.waiting = l.waiting.moved(0, l.dy);
    l.first_row_y += l.dy;
    l.other_rows_y += l.dy;
    for (size_t c = 0; c < 4; ++c) l.column_x[c] += l.dx;
    return l;
}

}  // namespace

const ResultsBox& results_winner_box() noexcept { return kWinnerBox; }
const ResultsBox& results_others_box() noexcept { return kOthersBox; }

const PieceStrip* results_strips(size_t& count) noexcept {
    count = kStrips.size();
    return kStrips.data();
}

const ResultsLayout& ResultsLayout::classic() noexcept {
    static const ResultsLayout layout = make_classic();
    return layout;
}

const ResultsLayout& ResultsLayout::wide() noexcept {
    static const ResultsLayout layout = make_wide();
    return layout;
}

// A box as the original builds it, with the right edge `dx` columns further right and every row `dy` lower: the top line is one repeated column of efram1100, then the bottom edge between the
// corners, the four corners, the flat black inside and the side pieces on top of it
void draw_results_box(IRenderer& renderer, const ants::assets::AssetArchive& archive, const ResultsBox& box, int32_t dx, int32_t dy, const PieceStrip& bottom_edge) {
    draw_top_line(renderer, archive, box.fill_x0, box.top + dy, box.fill_x1 + dx - box.fill_x0);
    draw_strip(renderer, archive, bottom_edge, box.left_corner_x + 32, box.bottom + dy);
    draw_piece(renderer, "efram1c.bmp", box.left_corner_x, box.top + dy);
    draw_piece(renderer, "efram2c.bmp", box.right_corner_x + dx, box.top + dy);
    draw_piece(renderer, "efram3c.bmp", box.left_corner_x, box.bottom + dy);
    draw_piece(renderer, "efram4c.bmp", box.right_corner_x + dx, box.bottom + dy);
    renderer.fill_rect(box.fill_x0, box.fill_y0 + dy, box.fill_x1 + dx - box.fill_x0, box.fill_y1 - box.fill_y0, kBoxBlack);
    for (int32_t k = 0; k < box.side_count; ++k) {
        draw_piece(renderer, "rel50.bmp", box.left_side_x, box.side_y + 50 * k + dy);
        draw_piece(renderer, "rer50.bmp", box.right_side_x + dx, box.side_y + 50 * k + dy);
    }
}

void draw_results_art(IRenderer& renderer, const ants::assets::AssetArchive& archive) {
    const ResultsLayout& l = ResultsLayout::wide();
    draw_wide_background(renderer, archive, PageClay::Tiles);
    // the banner hangs from the top edge (it covers the frame there, as in the original)
    draw_piece(renderer, "resbanr.bmp", l.banner.x, l.banner.y);
    draw_piece(renderer, "yoscore.bmp", l.your_score.x, l.your_score.y);
    draw_results_box(renderer, archive, kWinnerBox, l.dx, l.dy, find_strip(kStrips.data(), kStrips.size(), "results.winner.bottom"));
    draw_piece(renderer, "newstats.bmp", l.headers.x, l.headers.y);
    draw_piece(renderer, "winnr.bmp", l.winner_label.x, l.winner_label.y);
    draw_piece(renderer, "otherp.bmp", l.others_label.x, l.others_label.y);
    draw_results_box(renderer, archive, kOthersBox, l.dx, l.dy, find_strip(kStrips.data(), kStrips.size(), "results.others.bottom"));
}

}  // namespace ants::app
