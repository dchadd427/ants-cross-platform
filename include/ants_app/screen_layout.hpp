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

/// A point of the picture (an offset: where a page or a dialog is moved to)
struct LayoutPoint {
    int32_t x{0};
    int32_t y{0};

    constexpr bool operator==(const LayoutPoint& o) const noexcept { return x == o.x && y == o.y; }
    constexpr bool operator!=(const LayoutPoint& o) const noexcept { return !(*this == o); }
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
    /// The score slots of the original: slot 0 is the local team's (the top bar), slots 1 .. 3 are the other teams' (the bottom strip)
    static constexpr size_t kClassicScoreSlots = 4;
    /// The bottom strip (x17y461) of a wider picture is widened at THREE plain cuts of its art (shell_layout.hpp: its piece columns 15, 188 and 346), one left of each score box, and the
    /// extra width dx is shared between them in thirds (the leftmost takes the remainder), so that the three boxes are spread evenly over the strip instead of sitting together at its right
    /// end (requested: expand between the scores so they are not all offset to the right). At 960 x 540 (dx = 320) the cuts add 108, 106 and 106 and the boxes sit at x 213, 468
    /// and 722. A box and its label are anchored to each other: they move by what the cuts left of the box add. A strip with more score slots (later) would get one more cut for each, with
    /// the extra width shared equally between them: the boxes stay evenly spaced.
    static constexpr size_t kBottomSlots = 3;
    /// How far the score row of the corner's texts is kept from the covers of the boxes (the original's corner row: the third box's cover ends at 458, the texts' limit is 460)
    static constexpr int32_t kScoreRowMargin = 2;
    /// The cover of a box (scorcovr, 58 x 17 at (box_left - 2, top - 1)) reaches this far right of the box's left edge
    static constexpr int32_t kScoreCoverReach = 56;
    /// The dialog frame of the original (the animation std_dialg: the quit dialog, the alliance dialogs, the "get ready" modal), (100, 100) 320 x 224, and the original's pages (640 x 480)
    static constexpr int32_t kDialogX = 100;
    static constexpr int32_t kDialogY = 100;
    static constexpr int32_t kDialogW = 320;
    static constexpr int32_t kDialogH = 224;

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

    /// What cut `index` (0 = the leftmost) of a piece with `cuts` cuts adds to the piece's width: dx shared in equal parts, the leftmost cut taking the remainder (no extra width, no share).
    /// The frame (shell_layout.hpp) and the score slots read this one rule.
    constexpr int32_t cut_share(size_t index, size_t cuts) const noexcept {
        if (cuts == 0 || index >= cuts) return 0;
        const int32_t each = dx() / static_cast<int32_t>(cuts);
        return index == 0 ? dx() - each * static_cast<int32_t>(cuts - 1) : each;
    }
    /// How far the cuts 0 .. index of a piece with `cuts` cuts move what is right of cut `index`
    constexpr int32_t cut_shift(size_t index, size_t cuts) const noexcept {
        int32_t sum = 0;
        for (size_t i = 0; i <= index && i < cuts; ++i) sum += cut_share(i, cuts);
        return sum;
    }

    /// The slot k of the score boxes: 0 is the local team's (the top bar; right anchored: it stays beside the buttons), 1 .. 3 are the original's bottom slots (bottom anchored, and spread over
    /// the strip: the box of bottom slot b moves right by what the strip's cuts left of it add, see kBottomSlots: all of dx for the last, a third less for the one before ...).
    /// A k past the last slot gives the last one. (The numbers of the original are 0x1002218 and 0x10021b8.)
    constexpr ScoreSlot score_slot(size_t k) const noexcept {
        constexpr ScoreSlot local{312, 399, 4, 402};
        constexpr ScoreSlot bottom_slots[kBottomSlots] = {{5, 101, 464, 105}, {163, 251, 464, 254}, {312, 399, 464, 402}};
        if (k == 0) return ScoreSlot{right(local.label_left), right(local.label_right), local.top, right(local.box_left)};
        const size_t b = std::min(k, kBottomSlots);                      // 1 .. 3
        const ScoreSlot& s = bottom_slots[b - 1];
        const int32_t shift = cut_shift(b - 1, kBottomSlots);
        return ScoreSlot{s.label_left + shift, s.label_right + shift, bottom(s.top), s.box_left + shift};
    }

    /// How many bottom slots there are (1 .. n): the original's three, in every picture
    constexpr size_t bottom_slot_count() const noexcept { return kBottomSlots; }

    /// Where the row of the score boxes ends on the bottom strip: the right edge of the last bottom slot's cover and the margin that the texts of the corner keep (the network's ping and
    /// delay stand right of it; the original's 640 x 480 picture: 460)
    constexpr int32_t score_row_right() const noexcept { return score_slot(kBottomSlots).box_left + kScoreCoverReach + kScoreRowMargin; }

    /// What a dialog of the original (the frame at (100, 100) 320 x 224) is moved by during a match: its centre goes to the centre of the map view. The original's own
    /// place is the classic picture's (no move), so a layout without extra size gives (0, 0).
    constexpr LayoutPoint modal_offset() const noexcept { return centred_in_view(kDialogX, kDialogY, kDialogW, kDialogH); }

    /// Where the windows of the original's own pages that open DURING a match are drawn in a bigger picture: over the map view, centred in it, the HUD staying visible around them (as in the
    /// original's picture, where the options window sits over the map view and the panel is still there). The options window is the original's 442 x 440 card over its map view; the quick
    /// help is the 640 x 480 page. The original's own picture has no move.
    constexpr LayoutPoint options_offset() const noexcept { return centred_in_view(kClassicViewX, kClassicViewY, kClassicViewW, kClassicViewH); }
    constexpr LayoutPoint quick_help_offset() const noexcept { return centred_in_view(0, 0, kClassicWidth, kClassicHeight); }

    /// The part of the options window's picture that the original does not dim (measured on ants.chd: op_screen's 25 pieces of checker dither are a ring around the card, the left strip at
    /// x -3 .. 16, the top one at y 0 .. 19, the bottom one from y 460, and the whole panel to the right of it from x 459, so the card is 442 x 440 at the window's own (17, 20)). The original's
    /// whole picture outside the card is dimmed; in a bigger picture the card is where options_offset() puts it and the rest of the picture is dimmed the same way (HUD::render_options_dim).
    static constexpr int32_t kOptionsCardX = 17;
    static constexpr int32_t kOptionsCardY = 20;
    static constexpr int32_t kOptionsCardW = 442;
    static constexpr int32_t kOptionsCardH = 440;
    constexpr LayoutRect options_card() const noexcept {
        const LayoutPoint o = options_offset();
        return LayoutRect{kOptionsCardX + o.x, kOptionsCardY + o.y, kOptionsCardW, kOptionsCardH};
    }

private:
    /// The offset that puts the window (x, y, w, h) of the original's pages at the centre of the map view; none for the original's own picture
    constexpr LayoutPoint centred_in_view(int32_t x, int32_t y, int32_t w, int32_t h) const noexcept {
        if (dx() == 0 && dy() == 0) return LayoutPoint{};
        const LayoutRect v = view();
        return LayoutPoint{v.x + v.w / 2 - (x + w / 2), v.y + v.h / 2 - (y + h / 2)};
    }
};

}  // namespace ants::app
