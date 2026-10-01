#pragma once

#include <cstdint>

namespace ants::app {

/// The frame-rate counter, its sparkline and the version text in the bottom right corner (AGENTS.md invariant 7: the version stands next to the counter in every
/// state of the game). A remake-only overlay: the original draws nothing there. Its plate (with its one pixel frame) begins at row FPS_OVERLAY_TOP and ends at
/// the last row of the screen, so it never covers the START! button of the setup screen or of the quick help (their pressed pictures end at row 466) and the
/// 12 pixel digits fit above row 480.
inline constexpr int32_t FPS_OVERLAY_SPARK_Y = 468;                       // the first row of the sparkline and of the 12 px text
inline constexpr int32_t FPS_OVERLAY_SPARK_H = 11;
inline constexpr int32_t FPS_OVERLAY_TOP = FPS_OVERLAY_SPARK_Y - 1;       // the first row of the plate's frame
inline constexpr int32_t FPS_OVERLAY_BOTTOM = FPS_OVERLAY_SPARK_Y + FPS_OVERLAY_SPARK_H + 1;    // one past the plate's last row

} // namespace ants::app
