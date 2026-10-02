#pragma once

// The geometry of the picture that the game draws, as numbers (widescreen work, milestone M1).
//
// The original is a fixed 640 x 480 screen and the remake used to know that in a few hundred places: the map view (16, 21) - (458, 461), the minimap (480, 35),
// the right panel, the eight edge strips of the scroll, the pointer's limits, the corner of the frame-rate plate ... A ScreenLayout is the one place that says where
// those things are for a picture of a given size. `classic()` is the original's picture and every number of it is the original's (Ants.exe, docs/GAME_REVERSE_ENGINEERING.md
// 5.43, 5.44, 5.53, 5.56); `with_size(W, H)` is the same picture in a bigger canvas: what is anchored to the right edge of the original's screen (the right panel, the
// buttons of the top bar) moves right by dx = W - 640, what is anchored to the bottom edge (the bottom strip with its score boxes, the chat box) moves down by
// dy = H - 480, and the map view takes the rest (it grows by dx and dy). Nothing else moves. Everything that used to hard-code the classic picture reads the layout
// (renderer.cpp, hud.cpp, hud_input.cpp, edge_scroll.hpp, pointer_clamp.hpp, fps_overlay.hpp, application.cpp); with `classic()` they give what they always gave
// (tests/test_app/test_view_fingerprint.cpp pins that). No SDL here: the header is pure, so that the model can be tested without a window.
//
// All rectangles are half-open: the pixels x <= px < x + w and y <= py < y + h.

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ants::app {

/// A rectangle of the picture, half-open
struct LayoutRect {
    int32_t x{0};
    int32_t y{0};
    int32_t w{0};
    int32_t h{0};

    constexpr int32_t right() const noexcept { return x + w; }
    constexpr int32_t bottom() const noexcept { return y + h; }
    constexpr bool contains(int32_t px, int32_t py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h; }
    constexpr LayoutRect moved(int32_t dx, int32_t dy) const noexcept { return LayoutRect{x + dx, y + dy, w, h}; }
    constexpr bool operator==(const LayoutRect& o) const noexcept { return x == o.x && y == o.y && w == o.w && h == o.h; }
    constexpr bool operator!=(const LayoutRect& o) const noexcept { return !(*this == o); }
};

/// One slot of the score boxes: the label is right aligned in [label_left, label_right) and the box (54 x 14, FUN_01021e36) starts at box_left; `top` is the first row of both
/// (Ants.exe 0x10021b8 for the three bottom slots, 0x1002218 for the local team's slot of the top bar; FUN_0100dbe2 hands them out, docs 5.53)
struct ScoreSlot {
    int32_t label_left{0};
    int32_t label_right{0};
    int32_t top{0};
    int32_t box_left{0};

    constexpr bool operator==(const ScoreSlot& o) const noexcept {
        return label_left == o.label_left && label_right == o.label_right && top == o.top && box_left == o.box_left;
    }
    constexpr bool operator!=(const ScoreSlot& o) const noexcept { return !(*this == o); }
};

struct ScreenLayout {
    /// The original's screen
    static constexpr int32_t kClassicWidth = 640;
    static constexpr int32_t kClassicHeight = 480;
    /// The original's map view (Ants.exe 0x100a32b, 0x100dcbf): the world pixel (camera.x, camera.y) is at the screen pixel (16, 21), the view is 442 x 440
    static constexpr int32_t kClassicViewX = 16;
    static constexpr int32_t kClassicViewY = 21;
    static constexpr int32_t kClassicViewW = 442;
    static constexpr int32_t kClassicViewH = 440;
    /// The minimap's image (Ants.exe FUN_01009596): 119 x 91 at (480, 35)
    static constexpr int32_t kMinimapX = 480;
    static constexpr int32_t kMinimapY = 35;
    static constexpr int32_t kMinimapW = 119;
    static constexpr int32_t kMinimapH = 91;
    /// The chat log's view (docs 5.56): (482, 299) - (620, 400), 138 x 101
    static constexpr int32_t kChatViewX = 482;
    static constexpr int32_t kChatViewY = 299;
    static constexpr int32_t kChatViewW = 138;
    static constexpr int32_t kChatViewH = 101;
    /// The backing fill of the right panel (480, 22) 160 x 458: the plain colour under the panel's pieces
    static constexpr int32_t kPanelX = 480;
    static constexpr int32_t kPanelY = 22;
    static constexpr int32_t kPanelW = 160;
    static constexpr int32_t kPanelH = 458;
    /// The score slots of the original: slot 0 is the local team's (the top bar), slots 1 .. 3 are the other teams' (the bottom strip); a slot past the last repeats it
    static constexpr size_t kClassicScoreSlots = 4;

    /// The size of the picture: the original's 640 x 480, or a bigger canvas of the same screen
    int32_t width{kClassicWidth};
    int32_t height{kClassicHeight};
    /// The map view's top left corner: the original's (16, 21) in every layout of this model (only a test moves it, to see that a consumer reads it and does not assume it)
    int32_t view_x{kClassicViewX};
    int32_t view_y{kClassicViewY};

    /// The original's picture
    static constexpr ScreenLayout classic() noexcept { return ScreenLayout{}; }
    /// The picture of a canvas of w x h pixels (never smaller than the original's screen): the right-anchored parts move right by dx(), the bottom-anchored parts down by dy(),
    /// the map view grows by both
    static constexpr ScreenLayout with_size(int32_t w, int32_t h) noexcept {
        ScreenLayout layout;
        layout.width = std::max(w, kClassicWidth);
        layout.height = std::max(h, kClassicHeight);
        return layout;
    }

    constexpr bool is_classic() const noexcept { return *this == ScreenLayout{}; }
    constexpr bool operator==(const ScreenLayout& o) const noexcept { return width == o.width && height == o.height && view_x == o.view_x && view_y == o.view_y; }
    constexpr bool operator!=(const ScreenLayout& o) const noexcept { return !(*this == o); }

    /// What the right-anchored parts move by, and the bottom-anchored ones
    constexpr int32_t dx() const noexcept { return width - kClassicWidth; }
    constexpr int32_t dy() const noexcept { return height - kClassicHeight; }
    /// A classic x that is anchored to the right edge of the screen / a classic y that is anchored to the bottom edge
    constexpr int32_t right(int32_t classic_x) const noexcept { return classic_x + dx(); }
    constexpr int32_t bottom(int32_t classic_y) const noexcept { return classic_y + dy(); }

    /// The map view: it keeps its top left corner and takes what the panel and the bottom strip do not
    constexpr LayoutRect view() const noexcept { return LayoutRect{view_x, view_y, kClassicViewW + dx(), kClassicViewH + dy()}; }
    /// The minimap's image (the hit zone of the minimap too): right anchored
    constexpr LayoutRect minimap() const noexcept { return LayoutRect{right(kMinimapX), kMinimapY, kMinimapW, kMinimapH}; }
    /// The chat log's view: right anchored, and it takes the extra height
    constexpr LayoutRect chat_view() const noexcept { return LayoutRect{right(kChatViewX), kChatViewY, kChatViewW, kChatViewH + dy()}; }
    /// The panel's backing fill: right anchored, and as tall as the screen below the top bar
    constexpr LayoutRect panel_fill() const noexcept { return LayoutRect{right(kPanelX), kPanelY, kPanelW, kPanelH + dy()}; }

    /// The slot k of the score boxes: 0 is the local team's (the top bar; right anchored: it stays beside the buttons), 1 .. 3 are the bottom strip's (right anchored and bottom
    /// anchored: the boxes sit at the right end of the strip, next to the panel; the room to their left is where the boxes of further slots go). A k past the last slot of the
    /// original gives the last one. (The numbers are the original's, 0x1002218 and 0x10021b8.)
    constexpr ScoreSlot score_slot(size_t k) const noexcept {
        constexpr ScoreSlot local{312, 399, 4, 402};
        constexpr ScoreSlot bottom_slots[3] = {{5, 101, 464, 105}, {163, 251, 464, 254}, {312, 399, 464, 402}};
        if (k == 0) return ScoreSlot{right(local.label_left), right(local.label_right), local.top, right(local.box_left)};
        const ScoreSlot& s = bottom_slots[std::min<size_t>(k - 1, 2)];
        return ScoreSlot{right(s.label_left), right(s.label_right), bottom(s.top), right(s.box_left)};
    }
};

}  // namespace ants::app
