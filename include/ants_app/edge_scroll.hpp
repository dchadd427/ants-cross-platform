#pragma once

// Scrolling of the map view as Ants.exe does it. The input task runs every 50 ms (INPUT, AddTask(0, 0x32)); each run looks at the pointer:
// FUN_01026aa3 tests the eight edge strips (a 12 px band gives the scroll arrow cursor, but only when the view can move that way),
// FUN_01027251 turns the pointer into a target point of the map, FUN_01027197 clips a square around it to the map and FUN_0102fff8
// (ScrollToShow, margin and step 0, immediate) moves the view just far enough to show that square. The step therefore depends on the
// exact pointer pixel and on the scroll rate (0..99), and a strip's "hot zone" along the edge also scrolls the other axis.
// The minimap (FUN_01009850) does the same with a square of the view's size around the point under the pointer.
// (docs/GAME_REVERSE_ENGINEERING.md 5.43)

#include <algorithm>
#include <cstdint>

namespace ants::app {

inline constexpr int32_t kOrigViewW = 442;     // the view rect (16, 21) - (458, 461)
inline constexpr int32_t kOrigViewH = 440;
inline constexpr int32_t kScreenW = 640;       // the root screen
inline constexpr int32_t kScreenH = 480;

/// Result of one input tick: `dir` is the strip the pointer is in (0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW) or -1; the view moves by (dx, dy).
struct EdgeScroll {
    int32_t dir{-1};
    int32_t dx{0};
    int32_t dy{0};
};

namespace detail {

struct Rect {
    int32_t x0, y0, x1, y1;   // half-open
};

inline bool inside(const Rect& r, int32_t x, int32_t y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

// 12 px bands (0x104b3a0) and 5 px inner strips (0x104b3f0), in the order N, NE, E, SE, S, SW, W, NW
inline constexpr Rect kBand[8] = {{12, 0, 628, 12}, {628, 0, 640, 12}, {628, 12, 640, 468}, {628, 468, 640, 480},
                                  {12, 468, 628, 480}, {0, 468, 12, 480}, {0, 12, 12, 468}, {0, 0, 12, 12}};
inline constexpr Rect kInner[8] = {{5, 0, 635, 5}, {635, 0, 640, 5}, {635, 5, 640, 475}, {635, 475, 640, 480},
                                   {5, 475, 635, 480}, {0, 475, 5, 480}, {0, 5, 5, 475}, {0, 0, 5, 5}};

// FUN_0102fff8: the offset that makes the rect visible in the view (ox, oy, ox + 442, oy + 440)
inline void scroll_to_show(int32_t l, int32_t t, int32_t r, int32_t b, int32_t ox, int32_t oy, int32_t& dx, int32_t& dy) {
    const int32_t vis_r = ox + kOrigViewW;
    const int32_t vis_b = oy + kOrigViewH;
    dx = 0;
    dy = 0;
    if (r > vis_r) dx = r - vis_r;
    else if (l < ox) dx = l - ox;
    if (t < oy) dy = t - oy;
    else if (b > vis_b) dy = b - vis_b;
}

}  // namespace detail

/// One input tick of the edge scrolling. (mx, my) is the pointer on the 640 x 480 screen, `rate` the scroll setting 0..99, (ox, oy)
/// the view origin in map pixels and the map `map_tiles_w` x `map_tiles_h` tiles. A strip only counts when the view can move that way
/// (CanScroll 0x10270d2); only the 5 px inner strip of the same index scrolls.
inline EdgeScroll edge_scroll_step(int32_t mx, int32_t my, int32_t rate, int32_t ox, int32_t oy, int32_t map_tiles_w, int32_t map_tiles_h) {
    using namespace detail;
    const int32_t map_w = map_tiles_w * 32;
    const int32_t map_h = map_tiles_h * 32;
    const int32_t max_x = map_w - kOrigViewW;
    const int32_t max_y = map_h - kOrigViewH;
    if (mx >= 13 && mx < 627 && my >= 13 && my < 467) return EdgeScroll{};      // the pre-test of 0x1026b02: inside the quiet area
    const bool can[8] = {oy > 0, (ox < max_x) || (oy > 0), ox < max_x, (ox < max_x) || (oy < max_y),
                         oy < max_y, (ox > 0) || (oy < max_y), ox > 0, (ox > 0) || (oy > 0)};
    for (int32_t i = 0; i < 8; ++i) {
        if (!inside(kBand[i], mx, my) || !can[i]) continue;
        EdgeScroll out;
        out.dir = i;
        if (inside(kInner[i], mx, my)) {
            // FUN_01027251: the target point is the pointer scaled from the screen to the view, clamped to the map
            int32_t tx = ox + static_cast<int32_t>(mx * (static_cast<double>(kOrigViewW) / kScreenW));
            int32_t ty = oy + static_cast<int32_t>(my * (static_cast<double>(kOrigViewH) / kScreenH));
            tx = std::clamp(tx, 0, map_w);
            ty = std::clamp(ty, 0, map_h);
            const int32_t d = rate + 10;
            const int32_t l = std::max(tx - d, 0);
            const int32_t t = std::max(ty - d, 0);
            const int32_t r = std::min(tx + d, map_w);
            const int32_t b = std::min(ty + d, map_h);
            scroll_to_show(l, t, r, b, ox, oy, out.dx, out.dy);
        }
        return out;                                                              // the strips are disjoint
    }
    return EdgeScroll{};
}

/// The world point under the pointer on the minimap (rect (480, 35) - (599, 126), FUN_01009850): the offset in the rect times
/// map pixels / 119 and / 91, truncated, without a clamp.
inline void minimap_point(int32_t px, int32_t py, int32_t map_tiles_w, int32_t map_tiles_h, int32_t& wx, int32_t& wy) {
    const double scale_x = static_cast<double>(map_tiles_w * 32) / 119.0;
    const double scale_y = static_cast<double>(map_tiles_h * 32) / 91.0;
    wx = static_cast<int32_t>((px - 480) * scale_x);
    wy = static_cast<int32_t>((py - 35) * scale_y);
}

/// One input tick with the left button held on the minimap: the view scrolls to show the square (221, 220) around the point,
/// clipped to the map (ScrollToShow), so it ends up centred on the point and clamped at the map's edges.
inline EdgeScroll minimap_scroll_step(int32_t px, int32_t py, int32_t ox, int32_t oy, int32_t map_tiles_w, int32_t map_tiles_h) {
    int32_t wx = 0;
    int32_t wy = 0;
    minimap_point(px, py, map_tiles_w, map_tiles_h, wx, wy);
    const int32_t map_w = map_tiles_w * 32;
    const int32_t map_h = map_tiles_h * 32;
    const int32_t l = std::max(wx - 221, 0);
    const int32_t t = std::max(wy - 220, 0);
    const int32_t r = std::min(wx + 221, map_w);
    const int32_t b = std::min(wy + 220, map_h);
    EdgeScroll out;
    detail::scroll_to_show(l, t, r, b, ox, oy, out.dx, out.dy);
    return out;
}

/// The view at the start of a match (the end of the HUD's constructor, Ants.exe 0x100e458 - 0x100e4b1 into FUN_01027197): the fresh view, whose origin is (0, 0), scrolls just far enough
/// to show the square (ax - 160, ay - 160) - (ax + 192, ay + 192) around the pixel centre (ax, ay) = (32 tx + 16, 32 ty + 16) of the ANCHOR tile (tx, ty) of the local team's hill
/// (the tile that the level's layer 2 flags as the hill object's anchor: one tile in from the corner of the 4 x 4 footprint), clipped to the map. It does not centre the hill: the
/// view only moves right / down until the square's right / bottom edge is in view. Returns the origin.
inline void start_view_origin(int32_t anchor_tx, int32_t anchor_ty, int32_t map_tiles_w, int32_t map_tiles_h, int32_t& ox, int32_t& oy) {
    const int32_t ax = anchor_tx * 32 + 16;
    const int32_t ay = anchor_ty * 32 + 16;
    const int32_t l = std::max(ax - 160, 0);
    const int32_t t = std::max(ay - 160, 0);
    const int32_t r = std::min(ax + 192, map_tiles_w * 32);
    const int32_t b = std::min(ay + 192, map_tiles_h * 32);
    detail::scroll_to_show(l, t, r, b, 0, 0, ox, oy);
}

}  // namespace ants::app
