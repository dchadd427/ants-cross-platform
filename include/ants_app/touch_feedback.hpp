#pragma once

// The feedback of the touch controls: while a finger may become a hold (touch_control.hpp) a ring closes around it, so that the right click is seen coming, and a pulse goes out where a
// hold has fired. The geometry is pure (rectangles of the picture's pixels: a ring is rows of them, one pixel high) and is drawn with the renderer's rectangles over the match and under
// the pointer, only while there is a mark to draw: nothing else of the picture changes. The sizes are tied to the slop, which is a size on the glass, so the ring is as big on
// a phone as on a tablet: just outside of a fingertip.

#include <cstdint>
#include <optional>
#include <vector>

#include "ants_app/touch_control.hpp"
#include "ants_assets/chd_parser.hpp"

namespace ants::app {

class IRenderer;

namespace touch {

/// The ring closes from kRingStartSlops to kRingEndSlops slops of radius (8 CSS pixels each: a fingertip is some 30 CSS pixels in radius, so the ring ends just outside of it and starts
/// well away); in picture pixels never under the minimum or over the maximum (a slop that is a guess must not make a speck or a screen-sized ring)
inline constexpr double kRingEndSlops = 5.0;
inline constexpr double kRingStartSlops = 8.0;
inline constexpr double kRingEndMin = 14.0;
inline constexpr double kRingEndMax = 100.0;
inline constexpr double kRingStartGap = 10.0;      // the ring starts at least this much (picture pixels) wider than it ends
inline constexpr double kRingStartMax = 160.0;
/// The ring's line: 0.4 slop, 3 to 8 picture pixels, in gold with a dark pixel along both edges (it is seen on grass, sand and rock alike)
inline constexpr double kRingStrokeSlops = 0.4;
inline constexpr double kRingStrokeMin = 3.0;
inline constexpr double kRingStrokeMax = 8.0;
/// The pulse goes out to this many times the ring's end radius while it fades
inline constexpr double kPulseGrowth = 0.8;
static_assert(kRingEndMax + kRingStartGap < kRingStartMax, "the start of the ring is wider than its end for every slop");

double ring_end_radius(double slop) noexcept;
double ring_start_radius(double slop) noexcept;
double ring_stroke(double slop) noexcept;

}  // namespace touch

/// A rectangle to fill, in picture pixels
struct TouchPaint {
    int32_t x{0};
    int32_t y{0};
    int32_t w{0};
    int32_t h{0};
    ants::assets::ColorRGBA color{};
};

/// The ring that closes around a finger (progress 0: wide and faint; 1: at its end radius and bright) and the pulse after a hold (progress 0: at the ring's end radius, bright;
/// 1: grown and gone), for a slop in picture pixels. A mark that is not a position and a progress gives nothing; a progress outside 0 .. 1 is the nearest end.
std::vector<TouchPaint> touch_ring_paint(const TouchControl::Mark& mark, double slop);
std::vector<TouchPaint> touch_pulse_paint(const TouchControl::Mark& mark, double slop);

/// The rectangles of a disc's ring: the pixels whose centre lies from `inner` (included) to `outer` (not included) away from (cx, cy), as the rows' spans (a radius over 2048 is 2048, a position
/// is held to a million). For the tests and the two above.
void touch_annulus(double cx, double cy, double outer, double inner, ants::assets::ColorRGBA color, std::vector<TouchPaint>& out);

/// Draws what the model has to show (nothing when both are empty)
void draw_touch_feedback(IRenderer& renderer, const std::optional<TouchControl::Mark>& ring, const std::optional<TouchControl::Mark>& pulse, double slop);

}  // namespace ants::app
