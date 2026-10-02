#pragma once

#include <cstdint>

#include "ants_app/screen_layout.hpp"

namespace ants::app {

/// The frame-rate counter, its sparkline and the version text in the bottom right corner (AGENTS.md invariant 7: the version stands next to the counter in every
/// state of the game). A remake-only overlay: the original draws nothing there. Its plate (with its one pixel frame) begins at row FPS_OVERLAY_TOP and ends at
/// the last row of the screen, so it never covers the START! button of the setup screen or of the quick help (their pressed pictures end at row 466) and the
/// 12 pixel digits fit above row 480.
inline constexpr int32_t FPS_OVERLAY_SPARK_Y = 468;                       // the first row of the sparkline and of the 12 px text
inline constexpr int32_t FPS_OVERLAY_SPARK_H = 11;
inline constexpr int32_t FPS_OVERLAY_TOP = FPS_OVERLAY_SPARK_Y - 1;       // the first row of the plate's frame
inline constexpr int32_t FPS_OVERLAY_BOTTOM = FPS_OVERLAY_SPARK_Y + FPS_OVERLAY_SPARK_H + 1;    // one past the plate's last row

/// Where the corner of the overlay is: the corner of the CANVAS (the window's picture, whatever the game draws in it), so that the version stands next to the frame rate in the
/// bottom right corner of the viewport in every layout. For the original's 640 x 480 screen these are the FPS_OVERLAY_ constants and the right edge 632 (eight pixels in).
struct CornerPlate {
    int32_t right_edge{632};        // where the frame rate text ends (the corner's margin)
    int32_t spark_y{FPS_OVERLAY_SPARK_Y};     // the first row of the sparkline and of the 12 px text
    int32_t top{FPS_OVERLAY_TOP};             // the first row of the plate's frame
    int32_t bottom{FPS_OVERLAY_BOTTOM};       // one past the plate's last row (the canvas's height)

    static constexpr int32_t kMargin = 8;     // the corner's margin on the right
    static constexpr CornerPlate for_canvas(int32_t canvas_w, int32_t canvas_h) noexcept {
        CornerPlate plate;
        plate.right_edge = canvas_w - kMargin;
        plate.spark_y = canvas_h - (ScreenLayout::kClassicHeight - FPS_OVERLAY_SPARK_Y);
        plate.top = plate.spark_y - 1;
        plate.bottom = plate.spark_y + FPS_OVERLAY_SPARK_H + 1;
        return plate;
    }
    static constexpr CornerPlate classic() noexcept { return for_canvas(ScreenLayout::kClassicWidth, ScreenLayout::kClassicHeight); }
    constexpr bool operator==(const CornerPlate& o) const noexcept {
        return right_edge == o.right_edge && spark_y == o.spark_y && top == o.top && bottom == o.bottom;
    }
};

/// Where the three parts of the corner's row stand, from the right: the frame rate text ends at the plate's right edge, the sparkline is `kRowGap` left of it and the version `kRowGap` left of the
/// sparkline (the row of Application::render_frame; the network's ping and delay stand left of the version: latency_corner.hpp). `fps_w` / `ver_w` are the widths of the two texts,
/// `spark_w` x `spark_h` the sparkline's size, `text_h` the height of the 12 px text.
struct CornerRow {
    static constexpr int32_t kRowGap = 6;
    int32_t fps_x{0};                 // the frame rate text's left edge
    int32_t spark_x{0};               // the sparkline's left edge
    int32_t spark_y{0};               // the sparkline's and the texts' first row
    int32_t text_y{0};                // the first row of the 12 px texts (centred on the sparkline)
    int32_t version_x{0};             // the version text's left edge

    static constexpr CornerRow of(const CornerPlate& plate, int32_t fps_w, int32_t ver_w, int32_t spark_w, int32_t spark_h, int32_t text_h) noexcept {
        CornerRow row;
        row.fps_x = plate.right_edge - fps_w;
        row.spark_x = row.fps_x - spark_w - kRowGap;
        row.spark_y = plate.spark_y;
        row.text_y = row.spark_y + (spark_h - text_h) / 2;
        row.version_x = row.spark_x - ver_w - kRowGap;
        return row;
    }
};

} // namespace ants::app
