#pragma once

// How the frame of the match screen grows (widescreen work, milestone M3).
//
// The frame around the map view is the original's animation `uishell`: 14 pieces of art (x0y0 the top bar, x0y22 the left strip, x17y461 the bottom strip, x458y22 / x458y35 / x599y35 /
// x480y126 / x480y266 / x480y400 / x480y466 / x521y254 the right panel, and the three flat text fields wstatus / wchat / wtype) at absolute places of the 640 x 480 screen. In a bigger
// picture (ScreenLayout::with_size, 960 x 540) every piece is classified by what it is anchored to and, where the piece itself has to be bigger, by ONE line of it that is repeated:
//
//   * Right: the piece moves right by dx = W - 640 (the right panel is pinned to the right edge);
//   * Bottom: it moves down by dy = H - 480 (the bottom strip, the bottom of the panel);
//   * Cut column c: the piece is widened by dx by repeating its column c (the part to the right of the cut moves right with the edge);
//   * Cut row r: the piece is made dy taller by repeating its row r (the part below the cut moves down).
//
// The owner's idea (2026-10-01): "I sliced out one pixel and then just repeated that one pixel", and the art allows it: the green texture has only a vertical gradient, so the pieces hold long
// runs of identical rows and columns. The cuts are inside those runs and never inside a decoration, a score box or a clock box (measured on the art of ants.chd, and checked again by
// tests/test_app/test_wide_hud.cpp, group "cuts"):
//
//   x0y0 (top bar)         column 140: the plain green between the clock box and the name; columns 138 .. 143 are identical (the black clock box and the score box are never stretched)
//   x17y461 (bottom strip) column 15: the plain band left of the first score box; columns 13 .. 17 are identical. NEVER inside columns 458 .. 482: those are the right panel's own fill
//   x0y22 (left strip)     row 300: below the horizontal rule (it stays aligned with the chat header); rows 294 .. 314 are identical
//   x458y35, wchat, x521y254 (the strip between the map and the panel, the chat box, the right edge strip): row 322, 59 and 103, which are ONE canvas row, y = 357 of the classic picture,
//                          where all three are plain between their ant decorations (the owner's choice, 2026-10-02: "further down on the chat there's a spot that can repeat cleanly"). The
//                          rows are not identical there (the strips are a dithered gradient: neighbouring rows differ by at most 5 pixels), the chat box is flat
//
// With dx = dy = 0 (the classic picture) a piece is one span that is the whole piece at its own place: exactly what the original draws. No SDL here.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "ants_app/screen_layout.hpp"

namespace ants::app {

/// How one piece of the frame follows a bigger picture
struct ShellRule {
    const char* sprite;     // the piece's name in the archive
    bool right;             // moves right by dx
    bool bottom;            // moves down by dy
    int32_t cut_col;        // -1, or the column that is repeated to widen the piece by dx
    int32_t cut_row;        // -1, or the row that is repeated to make the piece dy taller
};

/// One drawing step of a piece: the rectangle `src` of the sprite fills the rectangle `dst` of the picture. A source one pixel wide (high) is stretched over a destination wider
/// (taller) than it: that is the repeated line. `whole` is true when the span is the entire sprite at its own size (a plain copy).
struct ShellSpan {
    LayoutRect dst;
    LayoutRect src;
    bool whole{false};
};

/// The spans of a piece (at most three: before the cut, the repeated line, after the cut)
struct ShellSpans {
    std::array<ShellSpan, 3> span{};
    size_t count{0};
};

/// The 14 pieces of `uishell`, in the animation's own order (the order the original stores them; it draws the last first)
inline constexpr std::array<ShellRule, 14> kShellRules = {{
    {"x17y461.bmp", false, true, 15, -1},     // 0: the bottom strip: widened at its plain band, down by dy
    {"x0y0.bmp", false, false, 140, -1},      // 1: the top bar: widened at its plain green
    {"wstatus.bmp", true, false, -1, -1},     // 2: the status box: right
    {"wtype.bmp", true, true, -1, -1},        // 3: the chat input box: right and down
    {"wchat.bmp", true, false, -1, 59},       // 4: the chat log's box: right, taller at canvas row 357 (the chat log takes the extra height)
    {"x599y35.bmp", true, false, -1, -1},     // 5: the minimap's bezel: right
    {"x521y254.bmp", true, false, -1, 103},   // 6: the right edge strip: right, taller at canvas row 357
    {"x480y466.bmp", true, true, -1, -1},     // 7: the panel's bottom: right and down
    {"x480y400.bmp", true, true, -1, -1},     // 8: the ant relief under the chat log: right and down
    {"x480y266.bmp", true, false, -1, -1},    // 9: the chat header: right
    {"x480y126.bmp", true, false, -1, -1},    // 10: the status card: right
    {"x458y35.bmp", true, false, -1, 322},    // 11: the strip between the map and the panel: right, taller at canvas row 357
    {"x458y22.bmp", true, false, -1, -1},     // 12: the top of the panel: right
    {"x0y22.bmp", false, false, -1, 300},     // 13: the left strip: taller below its horizontal rule
}};

/// The rule of a piece by its sprite's name (null: not a piece of the frame; it is drawn where the animation puts it)
inline const ShellRule* shell_rule(const std::string& sprite_name) noexcept {
    for (const ShellRule& rule : kShellRules) {
        if (sprite_name == rule.sprite) return &rule;
    }
    return nullptr;
}

/// The spans of the piece `rule` that the original puts at (x, y) with the size w x h, in a picture with the layout's dx and dy. Without a null rule or extra size: one span, the whole
/// piece at (x, y).
inline ShellSpans shell_spans(const ShellRule* rule, int32_t x, int32_t y, int32_t w, int32_t h, const ScreenLayout& layout) noexcept {
    ShellSpans out;
    const int32_t ox = (rule != nullptr && rule->right) ? layout.dx() : 0;
    const int32_t oy = (rule != nullptr && rule->bottom) ? layout.dy() : 0;
    const int32_t extra_w = (rule != nullptr && rule->cut_col >= 0) ? layout.dx() : 0;
    const int32_t extra_h = (rule != nullptr && rule->cut_row >= 0) ? layout.dy() : 0;
    const auto add = [&out, w, h](const LayoutRect& dst, const LayoutRect& src) {
        if (dst.w <= 0 || dst.h <= 0 || src.w <= 0 || src.h <= 0) return;
        out.span[out.count++] = ShellSpan{dst, src, src.x == 0 && src.y == 0 && src.w == w && src.h == h && dst.w == w && dst.h == h};
    };
    if (extra_w > 0) {                                                // widened at column c: [0, c) | the column c repeated 1 + extra times | (c, w)
        const int32_t c = rule->cut_col;
        add(LayoutRect{x + ox, y + oy, c, h}, LayoutRect{0, 0, c, h});
        add(LayoutRect{x + ox + c, y + oy, 1 + extra_w, h}, LayoutRect{c, 0, 1, h});
        add(LayoutRect{x + ox + c + 1 + extra_w, y + oy, w - c - 1, h}, LayoutRect{c + 1, 0, w - c - 1, h});
    } else if (extra_h > 0) {                                         // made taller at row r
        const int32_t r = rule->cut_row;
        add(LayoutRect{x + ox, y + oy, w, r}, LayoutRect{0, 0, w, r});
        add(LayoutRect{x + ox, y + oy + r, w, 1 + extra_h}, LayoutRect{0, r, w, 1});
        add(LayoutRect{x + ox, y + oy + r + 1 + extra_h, w, h - r - 1}, LayoutRect{0, r + 1, w, h - r - 1});
    } else {
        add(LayoutRect{x + ox, y + oy, w, h}, LayoutRect{0, 0, w, h});
    }
    return out;
}

}  // namespace ants::app
