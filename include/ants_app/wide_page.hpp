#pragma once

// The wide pages' common ground (widescreen work, after the setup screen): what every page of the original that is recomposed for the 16:9 picture of 960 x 540 shares.
//
// The original's pages are 640 x 480 pictures. In a 960 x 540 canvas the setup screen (setup_layout.hpp), the loading screen and the quick help at the start (page_layout.hpp), the results
// (results_layout.hpp) and the desktop start menu (start_menu.hpp) are not centred windows over a margin any more: each is composed for the whole canvas from the original's own art, and
// they all stand on the same ground, which is here:
//   * the clay: the tile dclay96 laid at its own pitch of 96 from (0, 0) (the last row of tiles is cut at the canvas's edge), or, for the pages whose own art carries flat clay (the loading screen,
//     the quick help), a flat field of the colour of that clay (kFlatClay: the tile is 97.3 % that colour with isolated speckles, which a flat field of bitmaps without speckles would show as
//     rectangles);
//   * the frame: the corners dfram1 / dfram3 / dfram6 / dfram8, the top (nine pieces of 96 and four of 16), the bottom (4, 2 x 16, 3, 2 x 16, 2 pieces of 96) and the two side strips, whole
//     pieces of 96 rows stacked and cut once, at a row that is identical to the piece's own neighbour;
//   * the primitives that every recomposed piece is made of: a strip of art drawn from runs of lines (LineSpan, PieceStrip, draw_strip), a piece drawn whole, the one repeated line of the
//     top of the original's black boxes.
// Every junction between runs that do not follow each other in a piece is between lines that are IDENTICAL (the pair of lines at the junction is one that the piece itself contains), and every
// line that is repeated lies in a run of identical lines, so that no seam can show and nothing is scaled: tests/test_app/test_wide_setup.cpp and test_wide_pages.cpp check each one against
// the art of ants.chd.
// The classic 640 x 480 canvas never sees any of this: a page is the original's own picture there.

#include <cstddef>
#include <cstdint>

#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_assets/asset_archive.hpp"

namespace ants::app {

/// A run of lines of a piece of art as it is drawn along one axis: the `src_count` lines from `src` fill `dst_count` lines of the picture. Equal counts are a plain copy of that run; one
/// source line (src_count 1) with a bigger dst_count is that one line repeated; a smaller dst_count is not used (a collapsed run is a plain copy of fewer lines: the run's other lines are
/// skipped). A strip is a list of spans; the lines of the picture are the spans' dst_counts in order.
struct LineSpan {
    int16_t src;
    int16_t src_count;
    int16_t dst_count;
};

/// One strip of art drawn from line spans: the piece `sprite`, along its columns (`columns`) or rows, over the lines [cross_first, cross_first + cross_count) of the other axis
/// (cross_count 0 = all of them); `length` is the number of lines of the picture that the spans make (the sum of their dst_counts).
struct PieceStrip {
    const char* id;
    const char* sprite;
    bool columns;
    int32_t cross_first;
    int32_t cross_count;
    int32_t length;
    const LineSpan* spans;
    size_t span_count;
};

/// The wide pages are made for a canvas of 960 x 540 and nothing else (any other canvas draws the original's own page)
inline constexpr int32_t kWidePageWidth = 960;
inline constexpr int32_t kWidePageHeight = 540;
constexpr bool wide_pages_supported(int32_t w, int32_t h) noexcept { return w == kWidePageWidth && h == kWidePageHeight; }

/// What a wide page's art moves by compared with the original's 640 x 480 page: the extra width, and the extra height
inline constexpr int32_t kWidePageDx = kWidePageWidth - ScreenLayout::kClassicWidth;       // 320
inline constexpr int32_t kWidePageDy = kWidePageHeight - ScreenLayout::kClassicHeight;     // 60

/// The colour of the flat clay (the original's own: the colour that the loading screen is filled with, (219, 75, 19), palette index 173)
inline constexpr ants::assets::ColorRGBA kFlatClay{219, 75, 19, 255};
/// The flat fill inside every black box of the original (the colour of the efram construction)
inline constexpr ants::assets::ColorRGBA kBoxBlack{7, 11, 15, 255};

enum class PageClay : uint8_t {
    Tiles,      // dclay96 laid at its pitch over the whole picture
    Flat        // one flat field (kFlatClay)
};

/// The clay of a wide page, the frame on top of it, or both (the background of every wide page)
void draw_wide_clay(IRenderer& renderer, const ants::assets::AssetArchive& archive, PageClay clay);
void draw_wide_frame(IRenderer& renderer, const ants::assets::AssetArchive& archive);
void draw_wide_background(IRenderer& renderer, const ants::assets::AssetArchive& archive, PageClay clay);

/// Every strip that the wide background draws from spans (the frame's two side strips), for the tests of the seams. `count` is set to their number.
const PieceStrip* wide_page_strips(size_t& count) noexcept;

/// The strip with this id in a table (the first one when there is none: a table is never asked for what it does not have)
const PieceStrip& find_strip(const PieceStrip* table, size_t count, const char* id);

/// Draws a strip at (x, y): the spans one after the other along the strip's axis
void draw_strip(IRenderer& renderer, const ants::assets::AssetArchive& archive, const PieceStrip& strip, int32_t x, int32_t y);

/// One piece of art drawn whole
void draw_piece(IRenderer& renderer, const char* name, int32_t x, int32_t y);

/// A line of one column of a piece repeated over a length: the top line of the black boxes (every column of efram1100 is the same)
void draw_top_line(IRenderer& renderer, const ants::assets::AssetArchive& archive, int32_t x, int32_t y, int32_t length);

}  // namespace ants::app
