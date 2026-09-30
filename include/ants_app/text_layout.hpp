#pragma once

// Line breaking and drawing of the original's labels on top of an IRenderer (Ants.exe FUN_0102b0b5 and FUN_0102b36a). The original measures with GDI's
// GetTextExtentPoint32A and draws every line with DrawTextA into the label's box; here IRenderer::get_text_width and draw_text stand in for them.

#include <cstdint>
#include <string>
#include <vector>

#include "ants_app/renderer.hpp"

namespace ants::app {

/// The original's greedy word wrap (FUN_0102b0b5): from the start of a line the text is scanned character by character; the scan stops when the width of
/// the text so far reaches `width`, and the line ends at the last word end that was seen before (a line that holds no word end is cut one character
/// before the width was reached). Leading spaces of a line are skipped. A '\n' ends a line as well (the original's labels hold none).
std::vector<std::string> wrap_label_text(const IRenderer& renderer, const std::string& text, int32_t width, FontSize size);

/// A one-line label's text cut to its box: characters are dropped from the end while the text is wider than `width` (the original draws into a surface
/// of the label's size, so what does not fit is not seen); at least one character stays.
std::string fit_text(const IRenderer& renderer, std::string text, int32_t width, FontSize size);

/// A multi-line label as the original draws it (FUN_0102b36a): the text is wrapped at `width`, every line is drawn from the top of the box at (x, y), one
/// cell height below the line before, centred in the box when `center` (DT_CENTER) and left aligned otherwise. Returns the label's height.
int32_t draw_label(IRenderer& renderer, const std::string& text, int32_t x, int32_t y, int32_t width, ants::assets::ColorRGBA color, FontSize size,
                   bool center);

/// The one line of an edit field as the original's label draws it (FUN_0102b36a with FUN_01011c25, docs 5.51): the text is one line in a box `width` wide of which one
/// caret (the width of "_") is kept free on the right. A text that fits is drawn from the left; one that does not is drawn right aligned when `tail_aligned` (its end shows,
/// and the caret sits at the right end of the box), otherwise cut at the right edge. The caret is an underscore right behind the text. Both are drawn at (x, y) in `size`.
void draw_edit_line(IRenderer& renderer, const std::string& text, int32_t x, int32_t y, int32_t width, bool tail_aligned, bool caret_visible, ants::assets::ColorRGBA color,
                    FontSize size);

}  // namespace ants::app
