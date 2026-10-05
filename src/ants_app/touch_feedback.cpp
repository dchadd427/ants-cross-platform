// The feedback of the touch controls (touch_feedback.hpp): the ring and the pulse as rectangles, and the call that draws them.
#include "ants_app/touch_feedback.hpp"

#include <algorithm>
#include <cmath>

#include "ants_app/renderer.hpp"

namespace ants::app {
namespace touch {

double ring_end_radius(double slop) noexcept {
    return std::clamp(kRingEndSlops * slop, kRingEndMin, kRingEndMax);
}

double ring_start_radius(double slop) noexcept {
    return std::clamp(kRingStartSlops * slop, ring_end_radius(slop) + kRingStartGap, kRingStartMax);
}

double ring_stroke(double slop) noexcept {
    return std::clamp(kRingStrokeSlops * slop, kRingStrokeMin, kRingStrokeMax);
}

}  // namespace touch

namespace {

using ants::assets::ColorRGBA;

constexpr double kLimit = 1.0e6;                                       // (positions are clamped: a rectangle's coordinates are 32 bits)
constexpr double kMaxRadius = 2048.0;                                  // (and so is a radius: a frame draws a few thousand rectangles at the most)

ColorRGBA gold(double alpha) { return ColorRGBA{255, 214, 64, static_cast<uint8_t>(std::clamp(std::lround(alpha), 0L, 255L))}; }
ColorRGBA edge(double alpha) { return ColorRGBA{24, 16, 0, static_cast<uint8_t>(std::clamp(std::lround(alpha * 0.75), 0L, 255L))}; }

bool usable(const TouchControl::Mark& mark, double slop) noexcept {
    return std::isfinite(mark.x) && std::isfinite(mark.y) && std::isfinite(mark.progress) && std::isfinite(slop);
}

/// The ring at radius `radius`: the gold line inside it, a dark pixel along both of its edges
void ring_at(const TouchControl::Mark& mark, double radius, double stroke, double alpha, std::vector<TouchPaint>& out) {
    const double inner = std::max(radius - stroke, 0.0);
    touch_annulus(mark.x, mark.y, radius, inner, gold(alpha), out);
    touch_annulus(mark.x, mark.y, radius + 1.0, radius, edge(alpha), out);
    if (inner > 0.0) touch_annulus(mark.x, mark.y, inner, std::max(inner - 1.0, 0.0), edge(alpha), out);
}

}  // namespace

void touch_annulus(double cx, double cy, double outer, double inner, ColorRGBA color, std::vector<TouchPaint>& out) {
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(outer) || !std::isfinite(inner)) return;
    cx = std::clamp(cx, -kLimit, kLimit);
    cy = std::clamp(cy, -kLimit, kLimit);
    outer = std::min(outer, kMaxRadius);
    inner = std::max(inner, 0.0);
    if (outer <= inner) return;
    const long first = static_cast<long>(std::floor(cy - outer));
    const long last = static_cast<long>(std::ceil(cy + outer));
    for (long y = first; y <= last; ++y) {
        const double dy = static_cast<double>(y) + 0.5 - cy;                        // (a pixel belongs to the ring by its centre)
        const double rest_outer = outer * outer - dy * dy;
        if (rest_outer <= 0.0) continue;
        const double half_outer = std::sqrt(rest_outer);
        const long x0 = static_cast<long>(std::floor(cx - half_outer - 0.5)) + 1;   // the first centre inside the outer circle
        const long x3 = static_cast<long>(std::ceil(cx + half_outer - 0.5));        // one past the last
        const double rest_inner = inner * inner - dy * dy;
        if (rest_inner <= 0.0) {                                                    // this row does not reach the hole: one span
            if (x3 > x0) out.push_back(TouchPaint{static_cast<int32_t>(x0), static_cast<int32_t>(y), static_cast<int32_t>(x3 - x0), 1, color});
            continue;
        }
        const double half_inner = std::sqrt(rest_inner);
        const long x1 = static_cast<long>(std::floor(cx - half_inner - 0.5)) + 1;   // one past the last centre outside the hole on the left
        const long x2 = static_cast<long>(std::ceil(cx + half_inner - 0.5));        // the first outside it on the right
        if (x1 > x0) out.push_back(TouchPaint{static_cast<int32_t>(x0), static_cast<int32_t>(y), static_cast<int32_t>(x1 - x0), 1, color});
        if (x3 > x2) out.push_back(TouchPaint{static_cast<int32_t>(x2), static_cast<int32_t>(y), static_cast<int32_t>(x3 - x2), 1, color});
    }
}

std::vector<TouchPaint> touch_ring_paint(const TouchControl::Mark& mark, double slop) {
    std::vector<TouchPaint> out;
    if (!usable(mark, slop)) return out;
    const double p = std::clamp(mark.progress, 0.0, 1.0);
    const double start = touch::ring_start_radius(slop);
    const double end = touch::ring_end_radius(slop);
    ring_at(mark, start + (end - start) * p, touch::ring_stroke(slop), 120.0 + 115.0 * p, out);
    return out;
}

std::vector<TouchPaint> touch_pulse_paint(const TouchControl::Mark& mark, double slop) {
    std::vector<TouchPaint> out;
    if (!usable(mark, slop)) return out;
    const double p = std::clamp(mark.progress, 0.0, 1.0);
    const double end = touch::ring_end_radius(slop);
    const double alpha = 235.0 * (1.0 - p) * (1.0 - p);
    if (alpha < 0.5) return out;                                                    // (a pulse that has faded to nothing is not drawn)
    ring_at(mark, end + end * touch::kPulseGrowth * p, touch::ring_stroke(slop), alpha, out);
    return out;
}

void draw_touch_feedback(IRenderer& renderer, const std::optional<TouchControl::Mark>& ring, const std::optional<TouchControl::Mark>& pulse, double slop) {
    if (pulse) {
        for (const TouchPaint& r : touch_pulse_paint(*pulse, slop)) renderer.fill_rect(r.x, r.y, r.w, r.h, r.color);
    }
    if (ring) {
        for (const TouchPaint& r : touch_ring_paint(*ring, slop)) renderer.fill_rect(r.x, r.y, r.w, r.h, r.color);
    }
}

}  // namespace ants::app
