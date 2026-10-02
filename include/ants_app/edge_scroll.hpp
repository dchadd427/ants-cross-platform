#pragma once

// Scrolling of the map view as Ants.exe does it. The input task runs every 50 ms (INPUT, AddTask(0, 0x32)); each run looks at the pointer:
// FUN_01026aa3 tests the eight edge strips (a 12 px band gives the scroll arrow cursor, but only when the view can move that way),
// FUN_01027251 turns the pointer into a target point of the map, FUN_01027197 clips a square around it to the map and FUN_0102fff8
// (ScrollToShow, margin and step 0, immediate) moves the view just far enough to show that square. The step therefore depends on the
// exact pointer pixel and on the scroll rate (0..99), and a strip's "hot zone" along the edge also scrolls the other axis.
// The minimap (FUN_01009850) does the same with a square of the view's size around the point under the pointer.
// (docs/GAME_REVERSE_ENGINEERING.md 5.43)
//
// Every function exists twice: one for the original's 640 x 480 screen (the signatures that were always here) and one that takes a ScreenLayout (screen_layout.hpp), which
// gives the picture's size, the view's rectangle and the minimap's: the strips run along the edges of the picture, the quiet area is its middle, the view and the square around
// the pointer have the view's size. The classic overloads are the layout versions with ScreenLayout::classic() and give exactly what they always gave.
//
// THE ZOOM (view_zoom.hpp). Every number of the original's model is in screen pixels (the 12 and 5 px strips, the step of "scroll rate + 10" around the target point, the square of half
// the view around a point of the minimap), so at a zoom the same model runs in screen pixels with the origin and the map scaled by the zoom: the `_px` functions take the view's origin
// and the map's size in SCREEN pixels at the zoom (world pixels times the zoom; the camera gives them, ViewportCamera::origin_screen_x) and return a step in screen pixels, which is the
// same distance on the screen at every zoom (the camera moves by step / zoom world pixels). At the zoom 1 they are the functions that were always here, whose `tiles` forms call them.

#include <algorithm>
#include <cstdint>

#include "ants_app/screen_layout.hpp"
#include "ants_app/view_zoom.hpp"

namespace ants::app {

inline constexpr int32_t kOrigViewW = ScreenLayout::kClassicViewW;     // the view rect (16, 21) - (458, 461)
inline constexpr int32_t kOrigViewH = ScreenLayout::kClassicViewH;
inline constexpr int32_t kScreenW = ScreenLayout::kClassicWidth;       // the root screen
inline constexpr int32_t kScreenH = ScreenLayout::kClassicHeight;

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

inline constexpr int32_t kBandWidth = 12;     // the scroll arrow strips (0x104b3a0)
inline constexpr int32_t kInnerWidth = 5;     // the strips that scroll (0x104b3f0)
inline constexpr int32_t kQuietMargin = 13;   // the pre-test of 0x1026b02: inside this margin of the screen nothing scrolls

// The strips of a screen of sw x sh pixels, in the order N, NE, E, SE, S, SW, W, NW: 12 px bands and 5 px inner strips. (For 640 x 480 they are the original's
// {12, 0, 628, 12}, {628, 0, 640, 12}, {628, 12, 640, 468}, {628, 468, 640, 480}, {12, 468, 628, 480}, {0, 468, 12, 480}, {0, 12, 12, 468}, {0, 0, 12, 12} and
// {5, 0, 635, 5}, {635, 0, 640, 5}, {635, 5, 640, 475}, {635, 475, 640, 480}, {5, 475, 635, 480}, {0, 475, 5, 480}, {0, 5, 5, 475}, {0, 0, 5, 5}.)
inline Rect strip(int32_t i, int32_t sw, int32_t sh, int32_t t) {
    switch (i) {
        case 0: return Rect{t, 0, sw - t, t};
        case 1: return Rect{sw - t, 0, sw, t};
        case 2: return Rect{sw - t, t, sw, sh - t};
        case 3: return Rect{sw - t, sh - t, sw, sh};
        case 4: return Rect{t, sh - t, sw - t, sh};
        case 5: return Rect{0, sh - t, t, sh};
        case 6: return Rect{0, t, t, sh - t};
        default: return Rect{0, 0, t, t};
    }
}
inline Rect band(int32_t i, int32_t sw, int32_t sh) { return strip(i, sw, sh, kBandWidth); }
inline Rect inner(int32_t i, int32_t sw, int32_t sh) { return strip(i, sw, sh, kInnerWidth); }

// FUN_0102fff8: the offset that makes the rect visible in the view (ox, oy, ox + view_w, oy + view_h)
inline void scroll_to_show(int32_t l, int32_t t, int32_t r, int32_t b, int32_t ox, int32_t oy, int32_t& dx, int32_t& dy, int32_t view_w, int32_t view_h) {
    const int32_t vis_r = ox + view_w;
    const int32_t vis_b = oy + view_h;
    dx = 0;
    dy = 0;
    if (r > vis_r) dx = r - vis_r;
    else if (l < ox) dx = l - ox;
    if (t < oy) dy = t - oy;
    else if (b > vis_b) dy = b - vis_b;
}

// ... of the original's 442 x 440 view
inline void scroll_to_show(int32_t l, int32_t t, int32_t r, int32_t b, int32_t ox, int32_t oy, int32_t& dx, int32_t& dy) {
    scroll_to_show(l, t, r, b, ox, oy, dx, dy, kOrigViewW, kOrigViewH);
}

}  // namespace detail

/// One input tick of the edge scrolling. (mx, my) is the pointer on the picture, `rate` the scroll setting 0..99, (ox, oy) the view origin and map_w x map_h the map, both in SCREEN
/// pixels (the world's own pixels at the zoom 1). A strip only counts when the view can move that way (CanScroll 0x10270d2); only the 5 px inner strip of the same index scrolls.
inline EdgeScroll edge_scroll_step_px(int32_t mx, int32_t my, int32_t rate, int32_t ox, int32_t oy, int32_t map_w, int32_t map_h, const ScreenLayout& layout) {
    using namespace detail;
    const int32_t sw = layout.width;
    const int32_t sh = layout.height;
    const LayoutRect view = layout.view();
    const int32_t max_x = map_w - view.w;
    const int32_t max_y = map_h - view.h;
    if (mx >= kQuietMargin && mx < sw - kQuietMargin && my >= kQuietMargin && my < sh - kQuietMargin) return EdgeScroll{};      // the pre-test of 0x1026b02: inside the quiet area
    const bool can[8] = {oy > 0, (ox < max_x) || (oy > 0), ox < max_x, (ox < max_x) || (oy < max_y),
                         oy < max_y, (ox > 0) || (oy < max_y), ox > 0, (ox > 0) || (oy > 0)};
    for (int32_t i = 0; i < 8; ++i) {
        if (!inside(band(i, sw, sh), mx, my) || !can[i]) continue;
        EdgeScroll out;
        out.dir = i;
        if (inside(inner(i, sw, sh), mx, my)) {
            // FUN_01027251: the target point is the pointer scaled from the screen to the view, clamped to the map
            int32_t tx = ox + static_cast<int32_t>(mx * (static_cast<double>(view.w) / sw));
            int32_t ty = oy + static_cast<int32_t>(my * (static_cast<double>(view.h) / sh));
            tx = std::clamp(tx, 0, map_w);
            ty = std::clamp(ty, 0, map_h);
            const int32_t d = rate + 10;
            const int32_t l = std::max(tx - d, 0);
            const int32_t t = std::max(ty - d, 0);
            const int32_t r = std::min(tx + d, map_w);
            const int32_t b = std::min(ty + d, map_h);
            scroll_to_show(l, t, r, b, ox, oy, out.dx, out.dy, view.w, view.h);
        }
        return out;                                                              // the strips are disjoint
    }
    return EdgeScroll{};
}

/// ... for a map of map_tiles_w x map_tiles_h tiles at the zoom 1 (the view origin (ox, oy) in map pixels)
inline EdgeScroll edge_scroll_step(int32_t mx, int32_t my, int32_t rate, int32_t ox, int32_t oy, int32_t map_tiles_w, int32_t map_tiles_h, const ScreenLayout& layout) {
    return edge_scroll_step_px(mx, my, rate, ox, oy, map_tiles_w * 32, map_tiles_h * 32, layout);
}

/// ... on the original's 640 x 480 screen
inline EdgeScroll edge_scroll_step(int32_t mx, int32_t my, int32_t rate, int32_t ox, int32_t oy, int32_t map_tiles_w, int32_t map_tiles_h) {
    return edge_scroll_step(mx, my, rate, ox, oy, map_tiles_w, map_tiles_h, ScreenLayout::classic());
}

/// The point under the pointer on the minimap (the layout's minimap rect, (480, 35) - (599, 126) in the original, FUN_01009850): the offset in the rect times map pixels / 119 and / 91,
/// truncated, without a clamp. The map's size is in whatever pixels the caller works in (world pixels; screen pixels at a zoom).
inline void minimap_point_px(int32_t px, int32_t py, int32_t map_w, int32_t map_h, int32_t& wx, int32_t& wy, const ScreenLayout& layout) {
    const LayoutRect mini = layout.minimap();
    const double scale_x = static_cast<double>(map_w) / static_cast<double>(mini.w);
    const double scale_y = static_cast<double>(map_h) / static_cast<double>(mini.h);
    wx = static_cast<int32_t>((px - mini.x) * scale_x);
    wy = static_cast<int32_t>((py - mini.y) * scale_y);
}

/// The world point under the pointer on the minimap, for a map of map_tiles_w x map_tiles_h tiles
inline void minimap_point(int32_t px, int32_t py, int32_t map_tiles_w, int32_t map_tiles_h, int32_t& wx, int32_t& wy, const ScreenLayout& layout) {
    minimap_point_px(px, py, map_tiles_w * 32, map_tiles_h * 32, wx, wy, layout);
}

/// ... on the original's screen
inline void minimap_point(int32_t px, int32_t py, int32_t map_tiles_w, int32_t map_tiles_h, int32_t& wx, int32_t& wy) {
    minimap_point(px, py, map_tiles_w, map_tiles_h, wx, wy, ScreenLayout::classic());
}

/// One input tick with the left button held on the minimap: the view scrolls to show the square (221, 220) (half the view's size each way) around the point,
/// clipped to the map (ScrollToShow), so it ends up centred on the point and clamped at the map's edges. The origin and the map are in screen pixels (see the top of this file):
/// the square is half the VIEW, so at a zoom the view ends up centred on the point whatever the zoom is.
inline EdgeScroll minimap_scroll_step_px(int32_t px, int32_t py, int32_t ox, int32_t oy, int32_t map_w, int32_t map_h, const ScreenLayout& layout) {
    int32_t wx = 0;
    int32_t wy = 0;
    minimap_point_px(px, py, map_w, map_h, wx, wy, layout);
    const LayoutRect view = layout.view();
    const int32_t half_w = view.w / 2;
    const int32_t half_h = view.h / 2;
    const int32_t l = std::max(wx - half_w, 0);
    const int32_t t = std::max(wy - half_h, 0);
    const int32_t r = std::min(wx + half_w, map_w);
    const int32_t b = std::min(wy + half_h, map_h);
    EdgeScroll out;
    detail::scroll_to_show(l, t, r, b, ox, oy, out.dx, out.dy, view.w, view.h);
    return out;
}

/// ... for a map of map_tiles_w x map_tiles_h tiles at the zoom 1
inline EdgeScroll minimap_scroll_step(int32_t px, int32_t py, int32_t ox, int32_t oy, int32_t map_tiles_w, int32_t map_tiles_h, const ScreenLayout& layout) {
    return minimap_scroll_step_px(px, py, ox, oy, map_tiles_w * 32, map_tiles_h * 32, layout);
}

/// ... on the original's screen
inline EdgeScroll minimap_scroll_step(int32_t px, int32_t py, int32_t ox, int32_t oy, int32_t map_tiles_w, int32_t map_tiles_h) {
    return minimap_scroll_step(px, py, ox, oy, map_tiles_w, map_tiles_h, ScreenLayout::classic());
}

/// The view at the start of a match (the end of the HUD's constructor, Ants.exe 0x100e458 - 0x100e4b1 into FUN_01027197): the fresh view, whose origin is (0, 0), scrolls just far enough
/// to show the square (ax - 160, ay - 160) - (ax + 192, ay + 192) around the pixel centre (ax, ay) = (32 tx + 16, 32 ty + 16) of the ANCHOR tile (tx, ty) of the local team's hill
/// (the tile that the level's layer 2 flags as the hill object's anchor: one tile in from the corner of the 4 x 4 footprint), clipped to the map. It does not centre the hill: the
/// view only moves right / down until the square's right / bottom edge is in view. Returns the origin (world pixels). The square is the same in every layout; only the world that the view
/// shows differs: the view's size over the zoom (`zoom`, view_zoom.hpp: the square is in world pixels and the hill stays in view at every zoom).
inline void start_view_origin(int32_t anchor_tx, int32_t anchor_ty, int32_t map_tiles_w, int32_t map_tiles_h, int32_t& ox, int32_t& oy, const ScreenLayout& layout, float zoom) {
    const int32_t ax = anchor_tx * 32 + 16;
    const int32_t ay = anchor_ty * 32 + 16;
    const int32_t l = std::max(ax - 160, 0);
    const int32_t t = std::max(ay - 160, 0);
    const int32_t r = std::min(ax + 192, map_tiles_w * 32);
    const int32_t b = std::min(ay + 192, map_tiles_h * 32);
    const LayoutRect view = layout.view();
    detail::scroll_to_show(l, t, r, b, 0, 0, ox, oy, zoom::visible(view.w, zoom), zoom::visible(view.h, zoom));
}

/// ... at the zoom 1
inline void start_view_origin(int32_t anchor_tx, int32_t anchor_ty, int32_t map_tiles_w, int32_t map_tiles_h, int32_t& ox, int32_t& oy, const ScreenLayout& layout) {
    start_view_origin(anchor_tx, anchor_ty, map_tiles_w, map_tiles_h, ox, oy, layout, zoom::kNormal);
}

/// ... on the original's screen
inline void start_view_origin(int32_t anchor_tx, int32_t anchor_ty, int32_t map_tiles_w, int32_t map_tiles_h, int32_t& ox, int32_t& oy) {
    start_view_origin(anchor_tx, anchor_ty, map_tiles_w, map_tiles_h, ox, oy, ScreenLayout::classic());
}

}  // namespace ants::app
