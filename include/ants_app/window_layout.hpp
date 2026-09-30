#pragma once

// Where a window goes: the options of the start scripts, which lay four games out as a 2 x 2 grid on one screen (--grid 2x2 --cell N), and the explicit
// --window-pos / --window-size. Pure functions; the application applies the result with SDL (native builds).

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace ants::app {

struct WindowRect {
    int32_t x{0};
    int32_t y{0};
    int32_t w{0};
    int32_t h{0};
    bool operator==(const WindowRect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

inline constexpr int32_t kMinWindowWidth = 320;      // the game is 640 x 480: a window never gets smaller than half of it
inline constexpr int32_t kMinWindowHeight = 240;
inline constexpr int32_t kMaxGridSide = 4;

/// "CxR" = columns x rows, each 1 .. 4 (`x` or `X`): the value of --grid. False for anything else.
inline bool parse_grid(const std::string& text, int32_t& cols, int32_t& rows) {
    if (text.size() != 3 || (text[1] != 'x' && text[1] != 'X')) return false;
    if (text[0] < '1' || text[0] > '0' + kMaxGridSide || text[2] < '1' || text[2] > '0' + kMaxGridSide) return false;
    cols = text[0] - '0';
    rows = text[2] - '0';
    return true;
}

/// "A,B" = two whole numbers (negative ones allowed: a display left of or above the main one): the value of --window-pos and --window-size.
inline bool parse_pair(const std::string& text, int32_t& a, int32_t& b) {
    const size_t comma = text.find(',');
    if (comma == std::string::npos || comma == 0 || comma + 1 >= text.size()) return false;
    const auto number = [](const std::string& s, int32_t& out) {
        if (s.empty() || !(s[0] == '-' || (s[0] >= '0' && s[0] <= '9'))) return false;       // no spaces, no plus sign: strtol would accept both
        char* end = nullptr;
        const long v = std::strtol(s.c_str(), &end, 10);
        if (end == s.c_str() || *end != '\0' || v < -100000 || v > 100000) return false;
        out = static_cast<int32_t>(v);
        return true;
    };
    int32_t first = 0;
    int32_t second = 0;
    if (!number(text.substr(0, comma), first) || !number(text.substr(comma + 1), second)) return false;
    a = first;
    b = second;
    return true;
}

/// The client area of the window for cell `cell` (row by row, 0 = top left) of a cols x rows grid laid over `area` (the usable part of a display: without the
/// menu bar, the dock, the task bar). The decoration of the window (title bar and frame: `border_*`) has to fit into the cell as well; the client area is the
/// largest 4:3 rectangle (the game's own proportions, nothing is letterboxed) that does, centred in the cell, and never smaller than kMinWindowWidth x Height.
/// A cell number beyond the grid is the last cell.
inline WindowRect grid_cell_window(const WindowRect& area, int32_t cols, int32_t rows, int32_t cell, int32_t border_top, int32_t border_left,
                                   int32_t border_bottom, int32_t border_right) {
    cols = std::max<int32_t>(cols, 1);
    rows = std::max<int32_t>(rows, 1);
    cell = std::min<int32_t>(std::max<int32_t>(cell, 0), cols * rows - 1);
    const int32_t cell_w = area.w / cols;
    const int32_t cell_h = area.h / rows;
    const int32_t cell_x = area.x + (cell % cols) * cell_w;
    const int32_t cell_y = area.y + (cell / cols) * cell_h;
    const int32_t avail_w = std::max<int32_t>(cell_w - border_left - border_right, kMinWindowWidth);
    const int32_t avail_h = std::max<int32_t>(cell_h - border_top - border_bottom, kMinWindowHeight);
    int32_t w = std::min<int32_t>(avail_w, avail_h * 4 / 3);
    w = std::max<int32_t>(w, kMinWindowWidth);
    const int32_t h = std::max<int32_t>(w * 3 / 4, kMinWindowHeight);
    WindowRect r;
    r.w = w;
    r.h = h;
    r.x = cell_x + border_left + (avail_w - w) / 2;
    r.y = cell_y + border_top + (avail_h - h) / 2;
    return r;
}

}  // namespace ants::app
