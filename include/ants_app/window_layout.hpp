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

/// The value of --window-size: "W,H" or "WxH" (`x` or `X`), two whole numbers of at least kMinWindowWidth x kMinWindowHeight (the game is 640 x 480: a window never gets smaller than
/// half of it) and at most 100000. False with a message that says what is wrong for anything else; the message is meant to follow "--window-size ".
inline bool parse_window_size(const std::string& text, int32_t& w, int32_t& h, std::string& why) {
    const size_t sep = text.find_first_of(",xX");                                                   // (a second separator is no digit: the number test refuses it)
    if (sep == std::string::npos) {
        why = "\"" + text + "\": needs WIDTHxHEIGHT or WIDTH,HEIGHT (for example 1280x720)";
        return false;
    }
    const auto number = [](const std::string& s, int32_t& out) {
        if (s.empty() || s.size() > 6) return false;
        int64_t v = 0;
        for (const char c : s) {                                                                  // digits only: no sign, no space, no plus
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
        }
        if (v > 100000) return false;
        out = static_cast<int32_t>(v);
        return true;
    };
    int32_t first = 0;
    int32_t second = 0;
    if (!number(text.substr(0, sep), first) || !number(text.substr(sep + 1), second)) {
        why = "\"" + text + "\": needs WIDTHxHEIGHT or WIDTH,HEIGHT (for example 1280x720), two whole numbers of at most 100000";
        return false;
    }
    if (first < kMinWindowWidth || second < kMinWindowHeight) {
        why = "\"" + text + "\": at least " + std::to_string(kMinWindowWidth) + "x" + std::to_string(kMinWindowHeight);
        return false;
    }
    w = first;
    h = second;
    return true;
}

/// The client area of the window for cell `cell` (row by row, 0 = top left) of a cols x rows grid laid over `area` (the usable part of a display: without the
/// menu bar, the dock, the task bar). The decoration of the window (title bar and frame: `border_*`) has to fit into the cell as well; the client area is the
/// largest rectangle of the game's own proportions (4:3, or `aspect_w` : `aspect_h` for another canvas: 16:9; nothing is letterboxed) that does, centred in the cell,
/// and never smaller than kMinWindowWidth x Height. A cell number beyond the grid is the last cell.
inline WindowRect grid_cell_window(const WindowRect& area, int32_t cols, int32_t rows, int32_t cell, int32_t border_top, int32_t border_left,
                                   int32_t border_bottom, int32_t border_right, int32_t aspect_w = 4, int32_t aspect_h = 3) {
    cols = std::max<int32_t>(cols, 1);
    rows = std::max<int32_t>(rows, 1);
    cell = std::min<int32_t>(std::max<int32_t>(cell, 0), cols * rows - 1);
    const int32_t cell_w = area.w / cols;
    const int32_t cell_h = area.h / rows;
    const int32_t cell_x = area.x + (cell % cols) * cell_w;
    const int32_t cell_y = area.y + (cell / cols) * cell_h;
    const int32_t avail_w = std::max<int32_t>(cell_w - border_left - border_right, kMinWindowWidth);
    const int32_t avail_h = std::max<int32_t>(cell_h - border_top - border_bottom, kMinWindowHeight);
    int32_t w = std::min<int32_t>(avail_w, avail_h * aspect_w / aspect_h);
    w = std::max<int32_t>(w, kMinWindowWidth);
    const int32_t h = std::max<int32_t>(w * aspect_h / aspect_w, kMinWindowHeight);
    WindowRect r;
    r.w = w;
    r.h = h;
    r.x = cell_x + border_left + (avail_w - w) / 2;
    r.y = cell_y + border_top + (avail_h - h) / 2;
    return r;
}

/// The client area of the window that a game with a fixed canvas opens in: the LARGEST SCALE IN STEPS OF 0.5 (1, 1.5, 2, 2.5, ...) of the canvas (960 x 540 for 16:9) that fits `area` (the
/// usable part of a display) with the decoration of the window (`border_*`), and at least 1x even where nothing fits; centred in the area. Sizes are in the window system's points, so
/// on a Retina display (two pixels to a point) 1.5x is 3x in pixels: every canvas pixel a square of whole pixels. (A step of a whole number only left a typical laptop at 1x: a 1800 x 1130
/// area is width limited, 1.875 x.) On a display of one pixel to a point an odd number of half steps is a fractional scale, which the fixed canvas shows with SDL's nearest-neighbour
/// stretch (the seams of canvas_layout.hpp).
inline WindowRect default_canvas_window(const WindowRect& area, int32_t canvas_w, int32_t canvas_h, int32_t border_top, int32_t border_left, int32_t border_bottom,
                                        int32_t border_right) {
    canvas_w = std::max<int32_t>(canvas_w, 1);
    canvas_h = std::max<int32_t>(canvas_h, 1);
    const int32_t avail_w = area.w - border_left - border_right;
    const int32_t avail_h = area.h - border_top - border_bottom;
    const int32_t half_steps = std::max<int32_t>(std::min<int32_t>(avail_w * 2 / canvas_w, avail_h * 2 / canvas_h), 2);       // in halves of the canvas: 2 is 1x
    WindowRect r;
    r.w = canvas_w * half_steps / 2;
    r.h = canvas_h * half_steps / 2;
    r.x = area.x + border_left + (avail_w - r.w) / 2;
    r.y = area.y + border_top + (avail_h - r.h) / 2;
    return r;
}

}  // namespace ants::app
