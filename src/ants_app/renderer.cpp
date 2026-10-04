#include "ants_app/renderer.hpp"
#include "ants_app/canvas_layout.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstdio>
#include <cctype>

namespace ants::app {

// ============================================================================
// ============================================================================
// Built-in Proportional 5x7 ASCII Bitmap Font (ASCII 32 ' ' through 126 '~')
// Authentic compact font matching Windows Small Fonts / MS Sans Serif 6pt
// ============================================================================
struct Glyph5x7 {
    uint8_t width;
    uint8_t rows[7];
};

static const Glyph5x7 FONT_5X7[95] = {
    {2, {0x00,0x00,0x00,0x00,0x00,0x00,0x00}}, // 32 ' '
    {1, {0x80,0x80,0x80,0x80,0x00,0x00,0x80}}, // 33 '!'
    {3, {0xA0,0xA0,0x40,0x00,0x00,0x00,0x00}}, // 34 '"'
    {5, {0x50,0xF8,0x50,0x50,0xF8,0x50,0x00}}, // 35 '#'
    {5, {0x20,0x78,0xA0,0x70,0x28,0xF0,0x20}}, // 36 '$'
    {5, {0xC8,0xD0,0x20,0x40,0x98,0x98,0x00}}, // 37 '%'
    {5, {0x60,0x90,0x60,0xA8,0x90,0x68,0x00}}, // 38 '&'
    {1, {0x80,0x80,0x00,0x00,0x00,0x00,0x00}}, // 39 '\''
    {2, {0x40,0x80,0x80,0x80,0x80,0x80,0x40}}, // 40 '('
    {2, {0x80,0x40,0x40,0x40,0x40,0x40,0x80}}, // 41 ')'
    {3, {0x00,0xA0,0x40,0xA0,0x00,0x00,0x00}}, // 42 '*'
    {3, {0x00,0x40,0xE0,0x40,0x00,0x00,0x00}}, // 43 '+'
    {2, {0x00,0x00,0x00,0x00,0x00,0x80,0x40}}, // 44 ','
    {3, {0x00,0x00,0x00,0xE0,0x00,0x00,0x00}}, // 45 '-'
    {1, {0x00,0x00,0x00,0x00,0x00,0x00,0x80}}, // 46 '.'
    {4, {0x10,0x20,0x20,0x40,0x40,0x80,0x80}}, // 47 '/'
    {4, {0x60,0x90,0x90,0x90,0x90,0x90,0x60}}, // 48 '0'
    {3, {0x40,0xC0,0x40,0x40,0x40,0x40,0xE0}}, // 49 '1'
    {4, {0x60,0x90,0x10,0x20,0x40,0x80,0xF0}}, // 50 '2'
    {4, {0x60,0x90,0x10,0x60,0x10,0x90,0x60}}, // 51 '3'
    {4, {0x20,0x60,0xA0,0xA0,0xF0,0x20,0x20}}, // 52 '4'
    {4, {0xF0,0x80,0xE0,0x10,0x10,0x90,0x60}}, // 53 '5'
    {4, {0x60,0x80,0x80,0xE0,0x90,0x90,0x60}}, // 54 '6'
    {4, {0xF0,0x10,0x20,0x20,0x40,0x40,0x40}}, // 55 '7'
    {4, {0x60,0x90,0x90,0x60,0x90,0x90,0x60}}, // 56 '8'
    {4, {0x60,0x90,0x90,0x70,0x10,0x10,0x60}}, // 57 '9'
    {1, {0x00,0x80,0x00,0x00,0x80,0x00,0x00}}, // 58 ':'
    {2, {0x00,0x80,0x00,0x00,0x80,0x80,0x40}}, // 59 ';'
    {3, {0x20,0x40,0x80,0x40,0x20,0x00,0x00}}, // 60 '<'
    {3, {0x00,0xE0,0x00,0xE0,0x00,0x00,0x00}}, // 61 '='
    {3, {0x80,0x40,0x20,0x40,0x80,0x00,0x00}}, // 62 '>'
    {4, {0x60,0x90,0x10,0x20,0x20,0x00,0x20}}, // 63 '?'
    {5, {0x70,0x88,0xA8,0xA8,0x88,0x70,0x00}}, // 64 '@'
    {4, {0x60,0x90,0x90,0xF0,0x90,0x90,0x90}}, // 65 'A'
    {4, {0xE0,0x90,0x90,0xE0,0x90,0x90,0xE0}}, // 66 'B'
    {4, {0x60,0x90,0x80,0x80,0x80,0x90,0x60}}, // 67 'C'
    {4, {0xE0,0x90,0x90,0x90,0x90,0x90,0xE0}}, // 68 'D'
    {4, {0xF0,0x80,0x80,0xE0,0x80,0x80,0xF0}}, // 69 'E'
    {4, {0xF0,0x80,0x80,0xE0,0x80,0x80,0x80}}, // 70 'F'
    {4, {0x60,0x90,0x80,0xB0,0x90,0x90,0x60}}, // 71 'G'
    {4, {0x90,0x90,0x90,0xF0,0x90,0x90,0x90}}, // 72 'H'
    {3, {0xE0,0x40,0x40,0x40,0x40,0x40,0xE0}}, // 73 'I'
    {4, {0x10,0x10,0x10,0x10,0x10,0x90,0x60}}, // 74 'J'
    {4, {0x90,0xA0,0xC0,0xC0,0xA0,0x90,0x90}}, // 75 'K'
    {4, {0x80,0x80,0x80,0x80,0x80,0x80,0xF0}}, // 76 'L'
    {5, {0x88,0xD8,0xA8,0x88,0x88,0x88,0x88}}, // 77 'M'
    {4, {0x90,0xD0,0xD0,0xB0,0xB0,0x90,0x90}}, // 78 'N'
    {4, {0x60,0x90,0x90,0x90,0x90,0x90,0x60}}, // 79 'O'
    {4, {0xE0,0x90,0x90,0xE0,0x80,0x80,0x80}}, // 80 'P'
    {4, {0x60,0x90,0x90,0x90,0xB0,0x90,0x50}}, // 81 'Q'
    {4, {0xE0,0x90,0x90,0xE0,0xA0,0x90,0x90}}, // 82 'R'
    {4, {0x60,0x90,0x80,0x60,0x10,0x90,0x60}}, // 83 'S'
    {5, {0xF8,0x20,0x20,0x20,0x20,0x20,0x20}}, // 84 'T'
    {4, {0x90,0x90,0x90,0x90,0x90,0x90,0x60}}, // 85 'U'
    {5, {0x88,0x88,0x88,0x88,0x50,0x50,0x20}}, // 86 'V'
    {5, {0x88,0x88,0x88,0xA8,0xA8,0xD8,0x88}}, // 87 'W'
    {5, {0x88,0x50,0x20,0x20,0x50,0x88,0x88}}, // 88 'X'
    {5, {0x88,0x88,0x50,0x20,0x20,0x20,0x20}}, // 89 'Y'
    {4, {0xF0,0x10,0x20,0x40,0x80,0x80,0xF0}}, // 90 'Z'
    {2, {0xC0,0x80,0x80,0x80,0x80,0x80,0xC0}}, // 91 '['
    {3, {0x80,0x80,0x40,0x40,0x20,0x20,0x20}}, // 92 '\\'
    {2, {0xC0,0x40,0x40,0x40,0x40,0x40,0xC0}}, // 93 ']'
    {3, {0x40,0xA0,0x00,0x00,0x00,0x00,0x00}}, // 94 '^'
    {4, {0x00,0x00,0x00,0x00,0x00,0x00,0xF0}}, // 95 '_'
    {2, {0x80,0x40,0x00,0x00,0x00,0x00,0x00}}, // 96 '`'
    {4, {0x00,0x00,0x60,0x10,0x70,0x90,0x70}}, // 97 'a'
    {4, {0x80,0x80,0xE0,0x90,0x90,0x90,0xE0}}, // 98 'b'
    {4, {0x00,0x00,0x60,0x90,0x80,0x90,0x60}}, // 99 'c'
    {4, {0x10,0x10,0x70,0x90,0x90,0x90,0x70}}, // 100 'd'
    {4, {0x00,0x00,0x60,0x90,0xF0,0x80,0x70}}, // 101 'e'
    {3, {0x40,0xA0,0x80,0xE0,0x80,0x80,0x80}}, // 102 'f'
    {4, {0x00,0x00,0x70,0x90,0x70,0x10,0x60}}, // 103 'g'
    {4, {0x80,0x80,0xE0,0x90,0x90,0x90,0x90}}, // 104 'h'
    {1, {0x80,0x00,0x80,0x80,0x80,0x80,0x80}}, // 105 'i'
    {2, {0x40,0x00,0x40,0x40,0x40,0x80,0x80}}, // 106 'j'
    {4, {0x80,0x80,0x90,0xA0,0xC0,0xA0,0x90}}, // 107 'k'
    {1, {0x80,0x80,0x80,0x80,0x80,0x80,0x80}}, // 108 'l'
    {5, {0x00,0x00,0xD0,0xA8,0xA8,0x88,0x88}}, // 109 'm'
    {4, {0x00,0x00,0xE0,0x90,0x90,0x90,0x90}}, // 110 'n'
    {4, {0x00,0x00,0x60,0x90,0x90,0x90,0x60}}, // 111 'o'
    {4, {0x00,0x00,0xE0,0x90,0x90,0xE0,0x80}}, // 112 'p'
    {4, {0x00,0x00,0x70,0x90,0x90,0x70,0x10}}, // 113 'q'
    {4, {0x00,0x00,0xA0,0xC0,0x80,0x80,0x80}}, // 114 'r'
    {3, {0x00,0x00,0x60,0x80,0x60,0x20,0xC0}}, // 115 's'
    {3, {0x40,0x40,0xE0,0x40,0x40,0x40,0x20}}, // 116 't'
    {4, {0x00,0x00,0x90,0x90,0x90,0x90,0x70}}, // 117 'u'
    {3, {0x00,0x00,0xA0,0xA0,0xA0,0x40,0x40}}, // 118 'v'
    {5, {0x00,0x00,0x88,0xA8,0xA8,0xA8,0x50}}, // 119 'w'
    {3, {0x00,0x00,0xA0,0x40,0x40,0xA0,0xA0}}, // 120 'x'
    {4, {0x00,0x00,0x90,0x90,0x70,0x10,0x60}}, // 121 'y'
    {3, {0x00,0x00,0xE0,0x40,0x80,0xE0,0xE0}}, // 122 'z'
    {3, {0x20,0x40,0x40,0x80,0x40,0x40,0x20}}, // 123 '{'
    {1, {0x80,0x80,0x80,0x80,0x80,0x80,0x80}}, // 124 '|'
    {3, {0x80,0x40,0x40,0x20,0x40,0x40,0x80}}, // 125 '}'
    {4, {0x50,0xA0,0x00,0x00,0x00,0x00,0x00}}  // 126 '~'
};

// ============================================================================
// ViewportCamera Implementation
// ============================================================================

void ViewportCamera::center_on(int32_t world_px, int32_t world_py, uint32_t map_w, uint32_t map_h) {
    if (zoom == zoom::kNormal) {
        x = static_cast<float>(world_px - viewport_w / 2);
        y = static_cast<float>(world_py - viewport_h / 2);
    } else {                                                   // the middle of what the view shows at this zoom
        x = static_cast<float>(static_cast<double>(world_px) - zoom::visible_exact(viewport_w, zoom) / 2.0);
        y = static_cast<float>(static_cast<double>(world_py) - zoom::visible_exact(viewport_h, zoom) / 2.0);
    }
    clamp_to_bounds(map_w, map_h);
}

// The view never shows beyond the map: the origin stays in [0, map - the world that the view shows]. A map that is smaller than that on an axis has no such range (a 16 x 16 map is 512
// pixels wide, the 16:9 view is 762): the camera is fixed on that axis, at the origin that centres the map in the view when `centre_small_maps` (a negative one: the world pixel 0 is right
// of the view's left edge; what the map does not cover stays the colour that the frame starts with, black) and at 0 otherwise. At the zoom 1 this is the camera's own arithmetic as it always
// was; at another zoom the origin is put on the grid of one screen pixel (view_zoom.hpp) and held by the same rule over the world that the view shows then.
void ViewportCamera::clamp_to_bounds(uint32_t map_w, uint32_t map_h) {
    const int32_t map_px_w = static_cast<int32_t>(map_w * TILE_SIZE);
    const int32_t map_px_h = static_cast<int32_t>(map_h * TILE_SIZE);
    if (zoom == zoom::kNormal) {
        if (map_px_w > viewport_w) x = std::clamp(x, 0.0f, static_cast<float>(map_px_w - viewport_w));
        else x = centre_small_maps ? static_cast<float>((map_px_w - viewport_w) / 2) : 0.0f;
        if (map_px_h > viewport_h) y = std::clamp(y, 0.0f, static_cast<float>(map_px_h - viewport_h));
        else y = centre_small_maps ? static_cast<float>((map_px_h - viewport_h) / 2) : 0.0f;
        world_x = static_cast<int32_t>(x);
        world_y = static_cast<int32_t>(y);
        return;
    }
    x = static_cast<float>(zoom::clamp_origin(zoom::snap(static_cast<double>(x), zoom), zoom, viewport_w, map_px_w, centre_small_maps));
    y = static_cast<float>(zoom::clamp_origin(zoom::snap(static_cast<double>(y), zoom), zoom, viewport_h, map_px_h, centre_small_maps));
    world_x = static_cast<int32_t>(std::floor(x));
    world_y = static_cast<int32_t>(std::floor(y));
}

void ViewportCamera::set_zoom(float level, int32_t anchor_dx, int32_t anchor_dy, uint32_t map_w, uint32_t map_h) {
    if (!(level >= zoom::kSmallest && level <= zoom::kIn)) return;          // (also a NaN)
    zoom::Camera from;
    from.x = static_cast<double>(zoom == zoom::kNormal ? static_cast<float>(world_x) : x);        // (at the zoom 1 the origin that the input code reads is world_x)
    from.y = static_cast<double>(zoom == zoom::kNormal ? static_cast<float>(world_y) : y);
    from.zoom = zoom;
    const zoom::Camera to = zoom::zoomed(from, level, anchor_dx, anchor_dy, viewport_w, viewport_h, static_cast<int64_t>(map_w) * TILE_SIZE, static_cast<int64_t>(map_h) * TILE_SIZE, centre_small_maps);
    zoom = level;
    x = static_cast<float>(to.x);
    y = static_cast<float>(to.y);
    world_x = static_cast<int32_t>(std::floor(x));
    world_y = static_cast<int32_t>(std::floor(y));
}

bool ViewportCamera::world_to_screen(int32_t wx, int32_t wy, int32_t& sx, int32_t& sy) const noexcept {
    if (zoom == zoom::kNormal) {
        sx = view_x + (wx - static_cast<int32_t>(x));
        sy = view_y + (wy - static_cast<int32_t>(y));
        return (sx >= view_x - TILE_SIZE && sx <= view_x + viewport_w &&
                sy >= view_y - TILE_SIZE && sy <= view_y + viewport_h);
    }
    const double z = static_cast<double>(zoom);
    sx = view_x + static_cast<int32_t>(std::floor((static_cast<double>(wx) - static_cast<double>(x)) * z));
    sy = view_y + static_cast<int32_t>(std::floor((static_cast<double>(wy) - static_cast<double>(y)) * z));
    const int32_t margin = static_cast<int32_t>(static_cast<double>(TILE_SIZE) * z);
    return (sx >= view_x - margin && sx <= view_x + viewport_w &&
            sy >= view_y - margin && sy <= view_y + viewport_h);
}

bool ViewportCamera::screen_to_world(int32_t sx, int32_t sy, int32_t& wx, int32_t& wy) const noexcept {
    if (sx < view_x || sx >= view_x + viewport_w ||
        sy < view_y || sy >= view_y + viewport_h) {
        return false;
    }
    if (zoom == zoom::kNormal) {
        wx = static_cast<int32_t>(x) + (sx - view_x);
        wy = static_cast<int32_t>(y) + (sy - view_y);
    } else {
        wx = zoom::world_at(static_cast<double>(x), zoom, sx - view_x);
        wy = zoom::world_at(static_cast<double>(y), zoom, sy - view_y);
    }
    return true;
}


// ============================================================================
// TextureCache Implementation
// ============================================================================

// Authentic Ants HUD palette tables from Ants.exe (31 entries for indices 1..31, VA 0x100EA70)
// Team 0 (Green in remake, Team 3 in Ants.exe VA 0x10023E0)
static const ants::assets::ColorRGBA AUTHENTIC_GREEN_HUD[31] = {
    {251, 251, 255, 255}, {191, 239, 227, 255}, {115, 191, 155, 255}, { 59, 151, 111, 255},
    { 51, 143, 103, 255}, { 39, 135,  95, 255}, { 31, 127,  87, 255}, { 23, 119,  79, 255},
    { 23, 115,  79, 255}, { 43, 107,  79, 255}, { 43, 104,  95, 255}, { 43,  99,  97, 255},
    { 19,  99,  75, 255}, { 47, 107,  75, 255}, { 43,  95,  67, 255}, { 43,  87,  63, 255},
    { 23,  83,  63, 255}, { 27,  75,  63, 255}, { 19,  71,  47, 255}, { 27,  67,  43, 255},
    { 23,  59,  39, 255}, { 19,  55,  47, 255}, { 19,  43,  27, 255}, { 11,  19,   7, 255},
    {  0,   0,   0, 255}, {243, 219,  55, 255}, {211, 183,   0, 255}, {183, 155,   0, 255},
    {151, 119,   0, 255}, {119,  87,   7, 255}, { 71,  63,   7, 255}
};

// Team 1 (Red in remake, Team 2 in Ants.exe VA 0x1002360)
static const ants::assets::ColorRGBA AUTHENTIC_RED_HUD[31] = {
    {251, 251, 255, 255}, {247, 175, 239, 255}, {227, 111, 163, 255}, {211,  83, 139, 255},
    {195,  67, 115, 255}, {195,  39, 123, 255}, {171,  63, 111, 255}, {171,  35, 119, 255},
    {175,  31, 115, 255}, {163,  31, 103, 255}, {143,  35,  99, 255}, {147,  35,  83, 255},
    {139,  35,  75, 255}, {131,  35,  71, 255}, {111,  43,  75, 255}, {107,  39,  71, 255},
    {107,  35,  55, 255}, { 87,  39,  67, 255}, { 87,  31,  47, 255}, { 67,  43,  43, 255},
    { 71,  27,  39, 255}, { 63,  27,  35, 255}, { 39,  15,  23, 255}, { 19,   7,  11, 255},
    {  0,   0,   0, 255}, {243, 219,  55, 255}, { 21, 183,   0, 255}, {183, 155,   0, 255},
    {151, 119,   0, 255}, {119,  87,   7, 255}, { 71,  63,   7, 255}
};

// Team 2 (Blue in remake, Team 1 in Ants.exe VA 0x10022E0)
static const ants::assets::ColorRGBA AUTHENTIC_BLUE_HUD[31] = {
    {251, 251, 255, 255}, {179, 191, 235, 255}, {115, 121, 219, 255}, { 79, 135, 199, 255},
    { 75, 123, 183, 255}, { 79, 115, 175, 255}, { 63, 103, 179, 255}, { 75, 103, 159, 255},
    { 55,  95, 171, 255}, { 47,  99, 163, 255}, { 51,  87, 163, 255}, { 47,  91, 155, 255},
    { 43,  95, 143, 255}, { 39,  91, 139, 255}, { 47,  71, 147, 255}, { 43,  67, 123, 255},
    { 39,  55, 107, 255}, { 31,  55,  87, 255}, { 31,  39,  79, 255}, { 27,  35,  71, 255},
    { 19,  39,  59, 255}, { 23,  27,  55, 255}, { 15,  15,  35, 255}, {  7,   7,  15, 255},
    {  0,   0,   0, 255}, {243, 219,  55, 255}, { 21, 183,   0, 255}, {183, 155,   0, 255},
    {151, 119,   0, 255}, {119,  87,   7, 255}, { 71,  63,   7, 255}
};

// Team 3 (Black in remake, Team 0 in Ants.exe VA 0x1002260)
static const ants::assets::ColorRGBA AUTHENTIC_BLACK_HUD[31] = {
    {251, 251, 255, 255}, {195, 191, 199, 255}, {146, 135, 147, 255}, {119, 127, 131, 255},
    {111, 107, 115, 255}, {115,  91, 119, 255}, {103,  91, 111, 255}, { 91,  95, 107, 255},
    { 99,  87, 103, 255}, { 91,  87,  99, 255}, { 87,  87,  91, 255}, { 79,  87,  87, 255},
    { 83,  79,  87, 255}, { 83,  67,  83, 255}, { 79,  63,  83, 255}, { 67,  55,  83, 255},
    { 63,  63,  67, 255}, { 67,  51,  71, 255}, { 59,  39,  67, 255}, { 59,  31,  63, 255},
    { 51,  35,  55, 255}, { 47,  31,  51, 255}, { 31,  19,  31, 255}, { 15,   7,  15, 255},
    {  0,   0,   0, 255}, {243, 219,  55, 255}, { 21, 183,   0, 255}, {183, 155,   0, 255},
    {151, 119,   0, 255}, {119,  87,   7, 255}, { 71,  63,   7, 255}
};

std::array<ants::assets::ColorRGBA, 256> TextureCache::compose_palette(const std::array<ants::assets::ColorRGBA, 256>& base,
                                                                       const std::string& image_name,
                                                                       uint8_t team_id) {
    auto pal = base;
    if (team_id >= ANT_COLOUR_BASE && team_id < ANT_COLOUR_BASE + 4) {
        // Original ant blit (Ants.exe FUN_0102cfef, remap enabled by FUN_0101b802): the colour offset is added to
        // every non-transparent pixel index; the callback at 0x101b7eb exempts image names starting with a digit.
        // Offsets by remake player id 0..3 (green, red, blue, black) = {60, 40, 20, 0}.
        static constexpr uint8_t kOffset[4] = { 60, 40, 20, 0 };
        const bool exempt = !image_name.empty() && std::isdigit(static_cast<unsigned char>(image_name[0])) != 0;
        const uint8_t offset = kOffset[team_id - ANT_COLOUR_BASE];
        if (!exempt && offset != 0) {
            for (size_t i = 0; i < 256; ++i) {
                if (i == ants::assets::CHD_COLOR_KEY_INDEX) continue;
                pal[i] = base[(i + offset) & 0xFF];
            }
        }
        return pal;
    }
    if (team_id < 4) {
        // Legacy HUD path. Ramp indices 80..99 map to the team ramp (Green +60, Red +40, Blue +20, Black +0).
        static constexpr size_t kRampOffset[4] = { 60, 40, 20, 0 };
        const size_t offset = kRampOffset[team_id];
        if (offset > 0) {
            for (size_t i = 80; i <= 99; ++i) pal[i] = base[i + offset];
        }
        // Authentic HUD palette remap for indices 1..31 (Ants.exe 0x100EA70):
        const ants::assets::ColorRGBA* hud = (team_id == 0) ? AUTHENTIC_GREEN_HUD
                                           : (team_id == 1) ? AUTHENTIC_RED_HUD
                                           : (team_id == 2) ? AUTHENTIC_BLUE_HUD
                                                            : AUTHENTIC_BLACK_HUD;
        for (size_t i = 1; i <= 31; ++i) pal[i] = hud[i - 1];
    }
    return pal;
}

TextureCache::TextureCache(SDL_Renderer* renderer, const ants::assets::AssetArchive& archive)
    : renderer_(renderer), archive_(archive) {}

TextureCache::~TextureCache() {
    clear();
}

void TextureCache::clear() {
    for (auto& pair : textures_) {
        if (pair.second) {
            SDL_DestroyTexture(pair.second);
        }
    }
    textures_.clear();
}

SDL_Texture* TextureCache::get_sprite_texture(uint32_t sprite_id, bool mirrored, uint8_t team_id) {
    // The bomb in a bomber's hands is the neutral maroon 2bomb.bmp for every team (its digit name exempts it from the
    // colour shift); only the planted bomb tiles have team art.
    const uint32_t eff_sprite_id = sprite_id;
    if (!renderer_ || eff_sprite_id >= archive_.sprite_count()) return nullptr;

    uint64_t key = (static_cast<uint64_t>(eff_sprite_id) << 9) |
                   (static_cast<uint64_t>(team_id) << 1) |
                   (mirrored ? 1 : 0);
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        return it->second;
    }

    const auto& sp = mirrored ? archive_.get_mirrored_sprite(eff_sprite_id)
                              : archive_.get_sprite(eff_sprite_id);
    if (sp.width == 0 || sp.height == 0) return nullptr;


    // Convert 8-bit paletted sprite to 32-bit RGBA under the requested colour mode
    const auto pal = compose_palette(archive_.get_palette(), sp.name, team_id);

    std::vector<uint8_t> rgba = sp.to_rgba32(pal);

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        rgba.data(),
        static_cast<int>(sp.width),
        static_cast<int>(sp.height),
        32,
        static_cast<int>(sp.width * 4),
        SDL_PIXELFORMAT_RGBA32
    );
    if (!surf) return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_FreeSurface(surf);

    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        textures_[key] = tex;
    }
    return tex;
}

// ============================================================================
// Renderer Implementation
// ============================================================================

Renderer::Renderer() = default;

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(SDL_Window* window,
                    const ants::assets::AssetArchive& archive) {
    archive_ = &archive;

    renderer_ = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer_) {
        // Fallback for headless or software mode
        renderer_ = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer_) {
            std::cerr << "[Renderer] Failed to create SDL Renderer: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    SDL_RendererInfo info{};
    software_ = SDL_GetRendererInfo(renderer_, &info) == 0 && (info.flags & SDL_RENDERER_SOFTWARE) != 0;

    // Nearest neighbor scaling ensures retro pixel art stays sharp and crisp when scaled
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    SDL_RenderSetLogicalSize(renderer_, canvas_w_, canvas_h_);      // the picture is scaled into the window by SDL: the largest scale that fits, centred, bars where the shapes differ

    texture_cache_ = std::make_unique<TextureCache>(renderer_, archive);

#ifdef ANTS_ENABLE_SDL_TTF
    if (TTF_Init() == 0) {
        ttf_initialized_ = true;
        // The original's labels use "Franklin Gothic Medium" (Ants.exe FUN_0102b05f). That font is commercial and was never part of the game (it came
        // with the operating system), so it cannot be shipped here: it is used when a copy is found (next to the game, or in the Windows fonts
        // folder). The bundled face is Libre Franklin Medium (SIL Open Font License, Original-Ants/LibreFranklin-OFL.txt), a free interpretation of
        // the same Franklin Gothic; the rest are system fonts for a build without it. Every size is opened by its cell height (see below), so the
        // layout is right with any of them.
        const std::vector<std::string> font_candidates = {
            "Original-Ants/Franklin Gothic Medium.ttf",
            "Original-Ants/framd.ttf",
            "C:\\Windows\\Fonts\\framd.ttf",
            "Original-Ants/LibreFranklin-Medium.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\Arial.ttf",
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/Library/Fonts/Arial.ttf",
            "/System/Library/Fonts/Supplemental/Trebuchet MS.ttf",
            "/System/Library/Fonts/Geneva.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf"
        };
        for (const auto& path : font_candidates) {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (f) {
                std::fclose(f);
                // How tall is the font's cell (ascent + descent) in em? The original's labels are sized by that cell height (GDI lfHeight), so every
                // FontSize is opened at the point size whose cell height, after the 2x supersampling, is the label's height.
                TTF_Font* probe = TTF_OpenFont(path.c_str(), 200);
                if (probe == nullptr) continue;
                const double cell_ratio = static_cast<double>(TTF_FontAscent(probe) - TTF_FontDescent(probe)) / 200.0;
                TTF_CloseFont(probe);
                bool ok = cell_ratio > 0.5;
                static constexpr FontSize kSizes[kFontSizes] = {FontSize::Px12, FontSize::Px14, FontSize::Px18, FontSize::Px20, FontSize::Px24, FontSize::Px35};
                for (size_t i = 0; ok && i < kFontSizes; ++i) {
                    fonts_[i] = TTF_OpenFont(path.c_str(), ttf_point_size(font_cell_height(kSizes[i]), cell_ratio));
                    ok = fonts_[i] != nullptr;
                }
                if (ok) {
                    std::cout << "[Renderer] High-quality TrueType font loaded: " << path << std::endl;
                    break;
                }
                for (auto& font : fonts_) {
                    if (font) TTF_CloseFont(font);
                    font = nullptr;
                }
            }
        }
    }
#endif

    return true;
}

void Renderer::shutdown() {
#ifdef ANTS_ENABLE_SDL_TTF
    for (auto& pair : text_cache_) {
        if (pair.second.texture) {
            SDL_DestroyTexture(pair.second.texture);
        }
    }
    text_cache_.clear();
    for (auto& font : fonts_) {
        if (font) TTF_CloseFont(font);
        font = nullptr;
    }
    if (ttf_initialized_) {
        TTF_Quit();
        ttf_initialized_ = false;
    }
#endif

    if (rgba_texture_) {
        SDL_DestroyTexture(rgba_texture_);
        rgba_texture_ = nullptr;
    }
    for (PassTexture& t : pass_levels_) destroy_texture(t);
    pass_levels_.clear();
    destroy_texture(lattice_);
    for (auto& glyph : fixed_glyphs_) {
        if (glyph) SDL_DestroyTexture(glyph);
        glyph = nullptr;
    }
    if (texture_cache_) {
        texture_cache_->clear();
        texture_cache_.reset();
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    archive_ = nullptr;
}

void Renderer::set_canvas_size(int32_t w, int32_t h) {
    canvas_w_ = std::max(w, 1);
    canvas_h_ = std::max(h, 1);
    picture_ = LayoutRect{0, 0, canvas_w_, canvas_h_};         // the picture is the whole canvas until it is told otherwise
    picture_inset_ = false;
    refit_canvas();
    restore_clip();
}

void Renderer::set_picture(const LayoutRect& picture) {
    picture_ = picture;
    picture_inset_ = picture_ != LayoutRect{0, 0, canvas_w_, canvas_h_};
    restore_clip();
}

// What "no clip" means: the whole canvas when the picture is the canvas, the picture's rectangle when it sits inside a bigger canvas (the bars around it stay as they are)
void Renderer::restore_clip() {
    if (!renderer_) return;
    if (picture_inset_) {
        const SDL_Rect clip = {picture_.x, picture_.y, picture_.w, picture_.h};
        SDL_RenderSetClipRect(renderer_, &clip);
    } else {
        SDL_RenderSetClipRect(renderer_, nullptr);
    }
}

void Renderer::refit_canvas() {
    if (!renderer_) return;
    SDL_RenderSetLogicalSize(renderer_, canvas_w_, canvas_h_);
}

void Renderer::set_layout(const ScreenLayout& layout) {
    layout_ = layout;
    camera_.set_view(layout_.view());
    camera_.centre_small_maps = !layout_.is_classic();                                                   // (the wide view is bigger than a small map: the map is centred in it)
    if (map_width_ > 0 && map_height_ > 0) camera_.clamp_to_bounds(map_width_, map_height_);       // a bigger view may not show more than the map has
}

void Renderer::set_level(const ants::assets::LevelData& level) {
    map_width_ = level.width;
    map_height_ = level.height;
    camera_.clamp_to_bounds(map_width_, map_height_);

    auto lower = [](std::string s) {
        for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };

    // Per-animation caches (template clocks and culling bounds)
    const size_t anim_count = archive_->animation_count();
    anim_bounds_.assign(anim_count, AnimBounds{});
    anim_frame_stamp_.assign(anim_count, 0xFFFFFFFFu);
    anim_frame_cache_.assign(anim_count, 0u);

    // The LVL tile dictionary is positional: entry i is "." or exactly the name of Table-4 animation i, and the
    // original stores the animation id itself in every cell (Ants.exe FUN_0100674e, mode 0). A cell's tile value
    // therefore indexes its animation directly; names are only checked as a safeguard for foreign maps.
    tile_anim_id_.assign(level.tile_dictionary.size(), -1);
    for (size_t i = 0; i < level.tile_dictionary.size(); ++i) {
        const std::string& name = level.tile_dictionary[i];
        if (name.empty() || name == ".") continue;

        // Editor start markers are never drawn in game
        const std::string low = lower(name);
        if (low == "bstart" || low == "ustart" || low == "rstart" || low == "gstart") continue;

        int32_t id = -1;
        if (i < anim_count && lower(archive_->get_animation(static_cast<uint32_t>(i)).name) == low) {
            id = static_cast<int32_t>(i);
        } else {
            id = archive_->find_animation_id(name);
            if (id < 0) id = archive_->find_animation_id(low);
        }
        tile_anim_id_[i] = id;
    }

    // Animations of runtime layer-2 items (all cells of an id share one template clock)
    anim_id_plus_ = archive_->find_animation_id("plus");
    anim_id_minus_ = archive_->find_animation_id("minus");
    for (int d = 0; d < 10; ++d) {
        anim_id_digit_[d] = archive_->find_animation_id("dig" + std::to_string(d));
    }
    anim_id_fire_ = archive_->find_animation_id("wallup04");
    anim_id_lunchbox_ = archive_->find_animation_id("lunchbox");
    anim_id_bomb_[0] = archive_->find_animation_id("greenbomb");
    anim_id_bomb_[1] = archive_->find_animation_id("redbomb");
    anim_id_bomb_[2] = archive_->find_animation_id("bluebomb");
    anim_id_bomb_[3] = archive_->find_animation_id("blackbomb");
    anim_id_powerup_[0] = -1;
    anim_id_powerup_[1] = archive_->find_animation_id("pu_bomb");
    anim_id_powerup_[2] = archive_->find_animation_id("pu_mason");
    anim_id_powerup_[3] = archive_->find_animation_id("pu_thief");
    anim_id_powerup_[4] = archive_->find_animation_id("pu_comb");
    anim_id_powerup_[5] = archive_->find_animation_id("pu_swim");
    anim_id_hill_[0] = archive_->find_animation_id("GREENHILL");
    anim_id_hill_[1] = archive_->find_animation_id("REDHILL");
    anim_id_hill_[2] = archive_->find_animation_id("BLUEHILL");
    anim_id_hill_[3] = archive_->find_animation_id("BLACKHILL");

    // Identify 4x4 anthill footprints and their layer-2 anchor cells
    auto hill_team_of = [&](const std::string& tname) -> int {
        const std::string low = lower(tname);
        if (low == "greenhill") return 0;
        if (low == "redhill") return 1;
        if (low == "bluehill") return 2;
        if (low == "blackhill") return 3;
        return -1;
    };
    for (size_t t = 0; t < 4; ++t) {
        anthill_bases_[t] = { -1, -1 };
        hill_anchor_cell_[t] = { -1, -1 };
    }
    has_anthill_bases_ = false;
    for (uint32_t y = 0; y < level.height; ++y) {
        for (uint32_t x = 0; x < level.width; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            if (c2.tile_index >= level.tile_dictionary.size()) continue;
            const int team = hill_team_of(level.tile_dictionary[c2.tile_index]);
            if (team < 0) continue;
            const size_t st = static_cast<size_t>(team);
            if (anthill_bases_[st].x < 0 || static_cast<int32_t>(x) < anthill_bases_[st].x) {
                anthill_bases_[st].x = static_cast<int32_t>(x);
            }
            if (anthill_bases_[st].y < 0 || static_cast<int32_t>(y) < anthill_bases_[st].y) {
                anthill_bases_[st].y = static_cast<int32_t>(y);
            }
            has_anthill_bases_ = true;
            if ((c2.flags & 1) != 0) {
                hill_anchor_cell_[st] = { static_cast<int>(x), static_cast<int>(y) };
            }
        }
    }
    for (size_t t = 0; t < 4; ++t) {
        // Custom maps without an anchor flag: the anchor is the second cell of the 4x4 footprint
        if (anthill_bases_[t].x >= 0 && hill_anchor_cell_[t].x < 0) {
            hill_anchor_cell_[t] = { anthill_bases_[t].x + 1, anthill_bases_[t].y + 1 };
        }
    }

    // Instantiate all layer-2 objects via their anchor flag ((flags & 1) != 0)
    static_decor_objects_.clear();
    object_index_by_cell_.assign(static_cast<size_t>(level.width) * level.height, -1);
    for (uint32_t y = 0; y < level.height; ++y) {
        for (uint32_t x = 0; x < level.width; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            if (c2.is_empty() || c2.tile_index == 0xFFFF || c2.tile_index == 0x7FFE) continue;
            // A power-up is its tile id (62 .. 66), whatever the dictionary calls the entry ("." in many community maps): the ground power-up is drawn exclusively through
            // cell.has_powerup() (below), the same sprite of the same kind on every map
            if (ants::sim::movement::is_powerup_tile(c2.tile_index)) continue;
            if (c2.tile_index >= level.tile_dictionary.size()) continue;

            const std::string& tname = level.tile_dictionary[c2.tile_index];
            if (tname.empty() || tname == ".") continue;

            const std::string low = lower(tname);
            if (low.find("hill") != std::string::npos || low.find("start") != std::string::npos) {
                continue; // anthills are drawn from their own templates, start markers never
            }
            if (low.rfind("pu_", 0) == 0 || low.rfind("pu", 0) == 0) {
                continue; // Dynamic powerups on ground: rendered exclusively via cell.has_powerup()
            }

            // Only anchor tiles ((flags & 1) != 0) define object instances
            if ((c2.flags & 1) == 0) continue;

            const int32_t anim_id = tile_anim_id_[c2.tile_index];
            if (anim_id < 0) continue;

            StaticMapObject obj{};
            obj.anim_id = anim_id;
            obj.anchor_x = static_cast<uint16_t>(x);
            obj.anchor_y = static_cast<uint16_t>(y);

            obj.is_food = (low.rfind("fd", 0) == 0 || low.rfind("food", 0) == 0);

            object_index_by_cell_[static_cast<size_t>(y) * level.width + x] = static_cast<int32_t>(static_decor_objects_.size());
            static_decor_objects_.push_back(std::move(obj));
        }
    }

    // Object-list sprites (Block 1 entries with team 255: clovers, flowers, tree tops ...). The original puts them in
    // the y-sorted sprite list at the cell centre with sort key row*32+16 (Ants.exe 0x100e383..0x100e448).
    object_list_sprites_.clear();
    for (const auto& sp_item : level.anthill_spawns) {
        if (sp_item.team_id != 255) continue;
        // A plant (the clovers 404 .. 408, the flowers 410 .. 416, 420 and 421, ...) is its tile id, which indexes its animation directly, whatever the dictionary calls the entry
        // ("." in some community maps): the original's name pass makes the graphics of these ids load whatever the dictionary says (the table at 0x1001ba8), so the flowers of a
        // dropper are always there to see. Any other record keeps the dictionary's name for the animation.
        int32_t anim_id = -1;
        if (ants::sim::movement::is_plant_object_tile(sp_item.tile_id)) {
            if (sp_item.tile_id < anim_count) anim_id = static_cast<int32_t>(sp_item.tile_id);
        } else if (sp_item.tile_id < level.tile_dictionary.size()) {
            anim_id = tile_anim_id_[sp_item.tile_id];
        }
        if (anim_id < 0) continue;
        ObjectListSprite spr{};
        spr.anim_id = anim_id;
        spr.px = static_cast<int32_t>(sp_item.x) * TILE_SIZE + TILE_SIZE / 2;
        spr.py = static_cast<int32_t>(sp_item.y) * TILE_SIZE + TILE_SIZE / 2;
        object_list_sprites_.push_back(spr);
    }
    std::stable_sort(object_list_sprites_.begin(), object_list_sprites_.end(),
                     [](const ObjectListSprite& a, const ObjectListSprite& b) { return a.py < b.py; });

    level_set_ = true;
    map_epoch_ms_ = SDL_GetTicks();
    template_now_ms_ = 0;
}

void Renderer::begin_frame() {
    if (!renderer_) return;
    ++frame_counter_;
    template_now_ms_ = (anim_clock_pin_ms_ >= 0) ? static_cast<uint32_t>(anim_clock_pin_ms_)
                                                 : SDL_GetTicks() - map_epoch_ms_;
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255); // Black letterbox / background
    SDL_RenderClear(renderer_);
    origin_ = LayoutPoint{};                         // (a frame starts in the picture's own numbers, whatever window the last one left open)
    restore_clip();                                  // (the picture, when it is smaller than the canvas: nothing is drawn beyond it)
}

// The part of the map view that the map covers. A map that is smaller than the view on an axis is centred in it with black around (the camera's origin is negative there): what hangs
// over the map's edge (an ant, an effect, a tall sprite of an object at the edge) is cut at the edge and does not draw onto the black. For every map that is at least as big as the view
// (all of the original's, in its own picture) this is the view itself. At a zoom the map's rectangle on the screen is the map scaled by it (inside the world pass the camera has the zoom 1
// and the view is the target: the rectangle is in texels).
LayoutRect Renderer::map_view_rect(uint32_t map_w, uint32_t map_h) const {
    const LayoutRect view = world_view();
    if (map_w == 0 || map_h == 0) return view;
    int32_t left = 0;
    int32_t top = 0;
    int32_t map_screen_w = 0;
    int32_t map_screen_h = 0;
    if (camera_.zoom == zoom::kNormal) {
        left = view.x - static_cast<int32_t>(camera_.x);                          // where the world pixel (0, 0) is on the screen (ViewportCamera::world_to_screen)
        top = view.y - static_cast<int32_t>(camera_.y);
        map_screen_w = static_cast<int32_t>(map_w) * TILE_SIZE;
        map_screen_h = static_cast<int32_t>(map_h) * TILE_SIZE;
    } else {
        const double z = static_cast<double>(camera_.zoom);
        left = view.x + static_cast<int32_t>(std::floor(-static_cast<double>(camera_.x) * z));
        top = view.y + static_cast<int32_t>(std::floor(-static_cast<double>(camera_.y) * z));
        map_screen_w = static_cast<int32_t>(std::lround(static_cast<double>(map_w) * TILE_SIZE * z));
        map_screen_h = static_cast<int32_t>(std::lround(static_cast<double>(map_h) * TILE_SIZE * z));
    }
    const int32_t x0 = std::max(view.x, left);
    const int32_t y0 = std::max(view.y, top);
    const int32_t x1 = std::min(view.right(), left + map_screen_w);
    const int32_t y1 = std::min(view.bottom(), top + map_screen_h);
    return LayoutRect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

// ----------------------------------------------------------------------------
// The world pass at a zoom: the world is drawn at one texel per world pixel into an offscreen target, which is then copied into the view
// ----------------------------------------------------------------------------

// A texture of the pass of tw x th texels: the one that there is when it is of that size, else a new one. A failure is kept in world_target_error_ and reported once (while it lasts, the
// Application takes the camera to the zoom 1 at its next frame and no pass asks again, see world_target_failed()); `report` false is the retry's (the failure is already reported).
bool Renderer::ensure_texture(PassTexture& t, int32_t tw, int32_t th, bool report) {
    if (t.texture != nullptr && t.w == tw && t.h == th) return true;
    destroy_texture(t);
    SDL_Texture* tex = (fail_world_target_creation_ || tw <= 0 || th <= 0) ? nullptr : SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
    if (tex == nullptr) {
        if (report) note_world_target_error(fail_world_target_creation_ ? "the test refuses the texture" : SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
    t.texture = tex;
    t.w = tw;
    t.h = th;
    return true;
}

void Renderer::destroy_texture(PassTexture& t) noexcept {
    if (t.texture != nullptr) SDL_DestroyTexture(t.texture);
    t = PassTexture{};
}

void Renderer::note_world_target_error(const char* what) {
    if (!world_target_error_.empty()) return;                    // (one report for one failure)
    world_target_error_ = (what != nullptr && what[0] != '\0') ? what : "unknown error";
    std::cerr << "[Renderer] Cannot make the offscreen target of the zoom: " << world_target_error_ << std::endl;
}

int32_t Renderer::max_world_extent() const noexcept {
    if (!renderer_) return 0;
    SDL_RendererInfo info{};
    if (SDL_GetRendererInfo(renderer_, &info) != 0) return 0;
    const int32_t side = std::min(info.max_texture_width, info.max_texture_height);
    return side > 0 ? std::max(0, side - kPassMargin) : 0;
}

// The way back from a real failure: the picture is the zoom 1 and no pass asks for the target, so the renderer asks now and then, with the largest target that a zoom needs (the whole map, the
// limit of the zoom-out; twice the view when no level is set) and the switch to it as the drawing target. A try that fails again is the failure that is already reported: no second report.
void Renderer::retry_world_target() {
    if (!renderer_ || in_world_target_ || world_target_error_.empty()) return;
    if (++world_target_retry_frames_ < kWorldTargetRetryFrames) return;
    world_target_retry_frames_ = 0;
    const LayoutRect view = layout_.view();
    const int32_t tw = std::max(2 * view.w, static_cast<int32_t>(map_width_) * TILE_SIZE);
    const int32_t th = std::max(2 * view.h, static_cast<int32_t>(map_height_) * TILE_SIZE);
    if (pass_levels_.empty()) pass_levels_.resize(1);
    if (!ensure_texture(pass_levels_[0], tw, th, false)) return;
    if (SDL_SetRenderTarget(renderer_, pass_levels_[0].texture) != 0) return;
    SDL_SetRenderTarget(renderer_, nullptr);
    world_target_error_.clear();                                  // it works again: the zoom levels come back, and a new failure is a new report
}

// The pass begins: the plan (view_zoom.hpp plan: what part of the map the target covers, how often it is halved, where the last level lands), the textures for it, and the renderer in the state
// of the world code: a camera of the zoom 1 over the target, the target as the picture.
bool Renderer::begin_world_target(int64_t map_w_px, int64_t map_h_px) {
    if (!renderer_ || in_world_target_ || fail_world_target_) return false;
    if (!world_target_error_.empty()) return false;               // (a failure that lasts: no new attempt at every frame; retry_world_target is the way back)
    const LayoutRect view = layout_.view();
    const zoom::Pass plan = zoom::plan(static_cast<double>(camera_.x), static_cast<double>(camera_.y), camera_.zoom, view.w, view.h, map_w_px, map_h_px, smooth_upscale_);
    if (plan.w <= 0 || plan.h <= 0) return false;                 // (nothing of the map is in the view: the zoom 1 picture of the same origin, which is that as well)
    if (pass_levels_.size() > static_cast<size_t>(plan.depth) + 1) {
        for (size_t k = static_cast<size_t>(plan.depth) + 1; k < pass_levels_.size(); ++k) destroy_texture(pass_levels_[k]);      // (the halvings of an earlier zoom: not kept)
    }
    pass_levels_.resize(static_cast<size_t>(plan.depth) + 1);
    for (int k = 0; k <= plan.depth; ++k) {
        if (!ensure_texture(pass_levels_[static_cast<size_t>(k)], plan.cap_w >> k, plan.cap_h >> k, true)) return false;
    }
    if (!ensure_texture(lattice_, view.w + kLatticeMargin, view.h + kLatticeMargin, true)) return false;
    if (SDL_SetRenderTarget(renderer_, pass_levels_[0].texture) != 0) {
        note_world_target_error(SDL_GetError());
        return false;
    }
    // The pass's own state: what is replaced is kept in pass_ and put back by end_world_target
    pass_.camera = camera_;
    pass_.picture = picture_;
    pass_.picture_inset = picture_inset_;
    pass_.origin = origin_;
    plan_ = plan;
    camera_.zoom = zoom::kNormal;                              // (a camera of the zoom 1 whose view is the target: the world code's numbers are texels)
    camera_.x = static_cast<float>(plan.x0);
    camera_.y = static_cast<float>(plan.y0);
    camera_.world_x = plan.x0;
    camera_.world_y = plan.y0;
    camera_.set_view(LayoutRect{0, 0, plan.w, plan.h});
    target_view_ = LayoutRect{0, 0, plan.w, plan.h};
    picture_ = target_view_;
    picture_inset_ = false;
    origin_ = LayoutPoint{};
    in_world_target_ = true;
    ++world_target_passes_;
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);           // (the black that the frame starts with: what the map does not cover)
    SDL_RenderClear(renderer_);
    return true;
}

// The pass ends. The world is in the first texture: it is halved as often as the plan says (a copy to half the size with the linear filter is the exact average of 2 x 2 texels), the last level is
// scaled by the plan's scale into the lattice of whole screen pixels at the fractional place that the plan gives (nearest at the zoom 2 and the zoom 1, else the linear filter), and the view is
// cut out of that, one screen pixel for one pixel: whatever the camera's origin is, the picture is the lattice's, so a scroll by whole screen pixels moves it and never changes it. A scaled copy
// that a clip cuts is cut by SDL's software renderer with a rounding of its source (and it truncates a fractional rectangle), which is why the view is cut out of a texture that holds the
// whole scaled picture, by an unscaled copy; a renderer that places a rectangle at a fraction of a pixel (every one but the software renderer) puts the last level at its exact place.
void Renderer::end_world_target() {
    if (!in_world_target_) return;
    in_world_target_ = false;
    camera_ = pass_.camera;
    picture_ = pass_.picture;
    picture_inset_ = pass_.picture_inset;
    origin_ = pass_.origin;
    const zoom::Pass& p = plan_;
    const LayoutRect view = layout_.view();
    int32_t lw = p.w;
    int32_t lh = p.h;
    for (int k = 1; k <= p.depth; ++k) {
        SDL_Texture* from = pass_levels_[static_cast<size_t>(k) - 1].texture;
        SDL_Texture* to = pass_levels_[static_cast<size_t>(k)].texture;
        const SDL_Rect src{0, 0, lw, lh};
        lw >>= 1;
        lh >>= 1;
        const SDL_Rect dst{0, 0, lw, lh};
        SDL_SetTextureScaleMode(from, SDL_ScaleModeLinear);
        SDL_SetRenderTarget(renderer_, to);
        SDL_RenderCopy(renderer_, from, &src, &dst);
    }
    SDL_Texture* last = pass_levels_[static_cast<size_t>(p.depth)].texture;
    SDL_SetTextureScaleMode(last, p.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    // where the last level lands, in the lattice of the screen pixels from the view's corner: its first texel at (left, top), fw x fh pixels
    const double fw = static_cast<double>(lw) * p.scale;
    const double fh = static_cast<double>(lh) * p.scale;
    const int64_t m0x = static_cast<int64_t>(std::floor(p.left));
    const int64_t m0y = static_cast<int64_t>(std::floor(p.top));
    const double fx = p.left - static_cast<double>(m0x);
    const double fy = p.top - static_cast<double>(m0y);
    const int32_t sw = static_cast<int32_t>(std::ceil(fx + fw));
    const int32_t sh = static_cast<int32_t>(std::ceil(fy + fh));
    SDL_SetRenderTarget(renderer_, lattice_.texture);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    const SDL_Rect src{0, 0, lw, lh};
    if (software_) {
        const SDL_Rect whole{0, 0, sw, sh};
        SDL_RenderCopy(renderer_, last, &src, &whole);
    } else {
        const SDL_FRect exact{static_cast<float>(fx), static_cast<float>(fy), static_cast<float>(fw), static_cast<float>(fh)};
        SDL_RenderCopyF(renderer_, last, &src, &exact);
    }
    SDL_SetRenderTarget(renderer_, nullptr);
    const SDL_Rect clip = placed(view.x, view.y, view.w, view.h);
    SDL_RenderSetClipRect(renderer_, &clip);
    const int32_t vx0 = static_cast<int32_t>(std::max<int64_t>(0, m0x));
    const int32_t vy0 = static_cast<int32_t>(std::max<int64_t>(0, m0y));
    const int32_t vx1 = static_cast<int32_t>(std::min<int64_t>(view.w, m0x + sw));
    const int32_t vy1 = static_cast<int32_t>(std::min<int64_t>(view.h, m0y + sh));
    if (vx0 > 0 || vy0 > 0 || vx1 < view.w || vy1 < view.h) {                      // (the picture does not cover the view: a small map; the rest is black)
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer_, &clip);
    }
    if (vx1 > vx0 && vy1 > vy0) {
        const SDL_Rect cut{static_cast<int32_t>(vx0 - m0x), static_cast<int32_t>(vy0 - m0y), vx1 - vx0, vy1 - vy0};
        const SDL_Rect dst = placed(view.x + vx0, view.y + vy0, vx1 - vx0, vy1 - vy0);
        SDL_SetTextureScaleMode(lattice_.texture, SDL_ScaleModeNearest);
        SDL_RenderCopy(renderer_, lattice_.texture, &cut, &dst);
    }
}

// What the pass leaves for the screen: the hit point digits (one size at every zoom: the numbers of Ctrl+L are text, not world art), at the screen position of the sprite's position
void Renderer::draw_deferred_digits() {
    const LayoutRect view = layout_.view();
    const double z = static_cast<double>(camera_.zoom);
    for (const DeferredDigits& d : deferred_digits_) {
        const int32_t sx = view.x + static_cast<int32_t>(std::floor((static_cast<double>(d.wx) - static_cast<double>(camera_.x)) * z));
        const int32_t sy = view.y + static_cast<int32_t>(std::floor((static_cast<double>(d.wy) - static_cast<double>(camera_.y)) * z));
        draw_fixed_text(d.text, sx, sy, ants::assets::ColorRGBA{255, 255, 255, 255});
    }
    deferred_digits_.clear();
}

void Renderer::render_world(const ants::sim::WorldState& world,
                            const ants::sim::Grid& grid,
                            int32_t selected_unit_id,
                            const std::vector<uint32_t>& selected_unit_ids,
                            bool /*show_all_health_bars: the original has no in-world health bar*/,
                            bool show_tile_grid,
                            int32_t mouse_x,
                            int32_t mouse_y,
                            int32_t selected_base_team_id,
                            float sub_tick_time) {
    if (!renderer_) return;
    sub_tick_ms_ = static_cast<uint32_t>(std::clamp(sub_tick_time, 0.0f, 0.0499f) * 1000.0f);
    render_queue_.clear();
    overlay_queue_.clear();
    deferred_digits_.clear();

    // At a zoom the world is drawn into the offscreen target (begin_world_target); at the zoom 1 straight into the view, exactly as it always was. A renderer that cannot make the target
    // (none that SDL has: every one supports targets) draws the zoom 1 picture of the same origin instead of a broken one.
    const float zoom_kept = camera_.zoom;
    const bool target = (zoomed() || force_world_target_) && begin_world_target(static_cast<int64_t>(grid.width()) * TILE_SIZE, static_cast<int64_t>(grid.height()) * TILE_SIZE);
    if (!target && zoomed()) camera_.zoom = zoom::kNormal;

    // 1. Clip exclusively to the part of the playfield that the map covers (the whole view, unless the map is smaller than it)
    const LayoutRect covered = map_view_rect(grid.width(), grid.height());
    const SDL_Rect clip_rect = placed(covered.x, covered.y, covered.w, covered.h);
    SDL_RenderSetClipRect(renderer_, &clip_rect);

    // 2. Layer 1 Terrain
    render_terrain_layer1(grid);

    // 3. Layer 2 Structures / Interactive Objects
    render_terrain_layer2_structures(grid, &world);

    // 4. One y-sorted sprite list (original FUN_010088e7, sort key = sprite y, ties keep insertion order):
    //    object-list plants (key row*32+16), ants, effects (key row*32), food droppers, the hill selection brackets
    //    and the click marker are all drawn from the same queue.
    collect_object_list_sprites();
    collect_ant_units(world);
    collect_visual_effects(world);
    collect_flower_droppers(world);
    draw_sorted_queue();

    // 4.8 Authentic Fog of War autotiling overlay
    if (world.fog_of_war_enabled) {
        render_fog_of_war(world);
    }

    // 4.9 View-container children, drawn after the whole map sprite in creation order (newest on top): selection
    //     markers, the hill marker, the click marker and the score bubbles
    collect_burn_overlays(world);
    collect_selection_markers(world, selected_unit_id, selected_unit_ids);
    collect_hill_brackets(grid, selected_base_team_id);
    collect_transient_sprites();
    collect_score_bubbles(world);
    draw_overlay_queue();

    // 5. Tile Grid Overlay (if enabled): on the screen, after the copy of a zoomed world
    if (show_tile_grid && !target) {
        render_tile_grid(grid, mouse_x, mouse_y);
    }

    // 6. Unset clipping for full-picture chrome
    restore_clip();

    if (target) {
        end_world_target();                       // the copy into the view (clipped to it)
        draw_deferred_digits();
        if (show_tile_grid) render_tile_grid(grid, mouse_x, mouse_y);
        restore_clip();
    }
    camera_.zoom = zoom_kept;
}

// ============================================================================
// Template animation helpers (terrain, layer-2 objects, object-list sprites)
// ============================================================================

void Renderer::draw_frame_parts(const ants::assets::AnimationSubItem& sub, int32_t sx, int32_t sy, bool mirrored,
                                uint8_t colour) {
    if (!archive_ || !texture_cache_ || !renderer_) return;
    // A mirrored part is written one pixel to the right of its own origin (Ants.exe part blitter 0x102cfef, mirrored
    // inner loop at 0x102d12c: the row starts at dest + width and runs backwards), which makes the reflection exact
    // about the anchor column: dx' = -dx - width, columns dx'+1 .. dx'+width.
    const int32_t mirror_shift = mirrored ? 1 : 0;
    // Original order (see frame_part_draw_order): the last stored part is drawn first, the first stored part on top.
    for (size_t k = sub.frames.size(); k-- > 0;) {
        const auto& f = sub.frames[k];
        const uint32_t sp_idx = f.sprite_index;
        SDL_Texture* tex = texture_cache_->get_sprite_texture(sp_idx, mirrored, colour);
        if (!tex) continue;
        const auto& sp = mirrored ? archive_->get_mirrored_sprite(sp_idx) : archive_->get_sprite(sp_idx);
        const SDL_Rect dst = placed(sx + f.dx + mirror_shift, sy + f.dy, static_cast<int>(sp.width), static_cast<int>(sp.height));
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    }
}

size_t Renderer::template_frame_index(int32_t anim_id) {
    if (!archive_ || anim_id < 0 || static_cast<size_t>(anim_id) >= anim_frame_cache_.size()) return 0;
    const size_t i = static_cast<size_t>(anim_id);
    if (anim_frame_stamp_[i] != frame_counter_) {
        const auto& seq = archive_->get_animation(static_cast<uint32_t>(anim_id));
        anim_frame_cache_[i] = static_cast<uint16_t>(seq.subitems.size() > 1 ? get_anim_subitem_by_time(seq, template_now_ms_) : 0);
        anim_frame_stamp_[i] = frame_counter_;
    }
    return anim_frame_cache_[i];
}

void Renderer::draw_template_screen(int32_t anim_id, int32_t sx, int32_t sy, uint8_t colour) {
    if (!archive_ || anim_id < 0 || static_cast<size_t>(anim_id) >= archive_->animation_count()) return;
    const auto& seq = archive_->get_animation(static_cast<uint32_t>(anim_id));
    if (seq.subitems.empty()) return;
    const size_t fi = std::min(template_frame_index(anim_id), seq.subitems.size() - 1);
    draw_frame_parts(seq.subitems[fi], sx, sy, false, colour);
}

const AnimBounds& Renderer::anim_bounds(int32_t anim_id) {
    static const AnimBounds kNone{};
    if (!archive_ || anim_id < 0 || static_cast<size_t>(anim_id) >= anim_bounds_.size()) return kNone;
    AnimBounds& b = anim_bounds_[static_cast<size_t>(anim_id)];
    if (b.valid) return b;
    const auto& seq = archive_->get_animation(static_cast<uint32_t>(anim_id));
    bool any = false;
    for (const auto& sub : seq.subitems) {
        for (const auto& f : sub.frames) {
            const auto& sp = archive_->get_sprite(f.sprite_index);
            const int32_t x0 = f.dx, y0 = f.dy;
            const int32_t x1 = f.dx + static_cast<int32_t>(sp.width), y1 = f.dy + static_cast<int32_t>(sp.height);
            if (!any) { b.x0 = x0; b.y0 = y0; b.x1 = x1; b.y1 = y1; any = true; }
            else { b.x0 = std::min(b.x0, x0); b.y0 = std::min(b.y0, y0); b.x1 = std::max(b.x1, x1); b.y1 = std::max(b.y1, y1); }
        }
    }
    b.valid = any;
    return b;
}

void Renderer::draw_template_world(int32_t anim_id, int32_t world_x, int32_t world_y, uint8_t colour) {
    const AnimBounds& b = anim_bounds(anim_id);
    if (!b.valid) return;
    const int32_t cam_x = static_cast<int32_t>(camera_.x);
    const int32_t cam_y = static_cast<int32_t>(camera_.y);
    const LayoutRect view = world_view();
    if (world_x + b.x1 <= cam_x || world_x + b.x0 >= cam_x + view.w ||
        world_y + b.y1 <= cam_y || world_y + b.y0 >= cam_y + view.h) {
        return;
    }
    draw_template_screen(anim_id, view.x + (world_x - cam_x), view.y + (world_y - cam_y), colour);
}

// ============================================================================
// Terrain layer 1
// ============================================================================

void Renderer::render_map_layers(const ants::sim::Grid& grid, const ants::sim::WorldState* world) {
    if (!renderer_) return;
    const float zoom_kept = camera_.zoom;
    const bool target = (zoomed() || force_world_target_) && begin_world_target(static_cast<int64_t>(grid.width()) * TILE_SIZE, static_cast<int64_t>(grid.height()) * TILE_SIZE);
    if (!target && zoomed()) camera_.zoom = zoom::kNormal;       // (as in render_world: the zoom 1 picture rather than a broken one)
    const LayoutRect covered = map_view_rect(grid.width(), grid.height());
    const SDL_Rect clip_rect = placed(covered.x, covered.y, covered.w, covered.h);
    SDL_RenderSetClipRect(renderer_, &clip_rect);
    render_terrain_layer1(grid);
    render_terrain_layer2_structures(grid, world);
    restore_clip();
    if (target) {
        end_world_target();
        restore_clip();
    }
    camera_.zoom = zoom_kept;
}

void Renderer::render_terrain_layer1(const ants::sim::Grid& grid) {
    const LayoutRect view = world_view();
    int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE);
    int32_t end_col   = std::min(static_cast<int32_t>(grid.width()) - 1,
                                 (static_cast<int32_t>(camera_.x) + view.w + 31) / TILE_SIZE);
    int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE);
    int32_t end_row   = std::min(static_cast<int32_t>(grid.height()) - 1,
                                 (static_cast<int32_t>(camera_.y) + view.h + 31) / TILE_SIZE);

    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            const auto& cell = grid.get_cell(static_cast<uint32_t>(c), static_cast<uint32_t>(r));
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(c * TILE_SIZE, r * TILE_SIZE, sx, sy);

            if (level_set_) {
                // The cell value is the id of the tile's Table-4 animation. Every id has ONE template started at map
                // load, so all cells of an id show the same frame at the same time (Ants.exe FUN_0102c1fc /
                // FUN_0102b997). Ids without a template draw nothing, as in the original.
                const int32_t id = (cell.terrain_id < tile_anim_id_.size()) ? tile_anim_id_[cell.terrain_id] : -1;
                if (id >= 0) draw_template_screen(id, sx, sy);
                continue;
            }

            // Synthetic grids without a loaded level: flat colours per terrain category
            const SDL_Rect dst = placed(sx, sy, TILE_SIZE, TILE_SIZE);
            if (cell.terrain_type == ants::sim::TERRAIN_WATER) {
                SDL_SetRenderDrawColor(renderer_, 23, 71, 151, 255); // Navy Water
            } else if (cell.terrain_type == ants::sim::TERRAIN_OBSTACLE) {
                SDL_SetRenderDrawColor(renderer_, 47, 51, 63, 255);  // Dark Rock
            } else if (cell.is_mud) {
                SDL_SetRenderDrawColor(renderer_, 65, 65, 65, 255);  // Dark Mud
            } else {
                SDL_SetRenderDrawColor(renderer_, 135, 120, 110, 255); // Tan Gravel Ground
            }
            SDL_RenderFillRect(renderer_, &dst);
        }
    }
}

// ============================================================================
// Terrain layer 2 (objects, food, bombs, fire walls, bridges, power-ups, anthills)
// ============================================================================

int32_t Renderer::food_stage_anim(uint16_t tile) const {
    if (tile < tile_anim_id_.size() && tile_anim_id_[tile] >= 0) return tile_anim_id_[tile];
    return (archive_ && tile < archive_->animation_count()) ? static_cast<int32_t>(tile) : -1;
}

void Renderer::draw_static_object(const StaticMapObject& obj, const ants::sim::Grid& grid,
                                  const ants::sim::WorldState* world) {
    int32_t anim_id = obj.anim_id;
    if (obj.is_food) {
        // Food shows the template of its current stage tile at the same anchor (SetTile keeps the anchor): the anchor cell holds it while any is left.
        if (!grid.in_bounds(obj.anchor_x, obj.anchor_y)) return;
        const auto& anchor = grid.get_cell(obj.anchor_x, obj.anchor_y);
        if (!anchor.has_food()) return; // All food in this item has been gathered

        // Fog: food is one of the four classes that an unexplored ANCHOR hides (FUN_01008089 0x10071dd); the explored cells of its footprint draw it from their own turn
        if (world && world->fog_of_war_enabled && !world->is_tile_revealed(obj.anchor_x, obj.anchor_y)) return;

        const int32_t stage = food_stage_anim(anchor.interactive_id);
        if (stage >= 0) anim_id = stage;
    }
    if (anim_id < 0) return;
    draw_template_world(anim_id, static_cast<int32_t>(obj.anchor_x) * TILE_SIZE, static_cast<int32_t>(obj.anchor_y) * TILE_SIZE);
}

void Renderer::draw_object_from_body_cell(const ants::sim::Grid& grid, const ants::sim::WorldState& world, int32_t col, int32_t row) {
    const auto& cell = grid.get_cell(static_cast<uint32_t>(col), static_cast<uint32_t>(row));
    if (cell.is_empty_overlay()) return;
    const int32_t ax = cell.anchor_x, ay = cell.anchor_y;
    if (ax < 0 || ay < 0 || !grid.in_bounds(ax, ay) || (ax == col && ay == row)) return;       // an anchor, or a cell without anchor bytes, draws itself
    const size_t index = static_cast<size_t>(row) * map_width_ + static_cast<size_t>(col);
    if (index < object_index_by_cell_.size() && object_index_by_cell_[index] >= 0) return;    // the anchor of a static object whose bytes do not name itself (the editor leaves 0, 0)
    if (!world.is_tile_revealed(col, row) || world.is_tile_revealed(ax, ay)) return;
    const size_t anchor_index = static_cast<size_t>(ay) * map_width_ + static_cast<size_t>(ax);
    if (anchor_index < object_index_by_cell_.size() && object_index_by_cell_[anchor_index] >= 0) {
        const StaticMapObject& obj = static_decor_objects_[static_cast<size_t>(object_index_by_cell_[anchor_index])];
        int32_t anim_id = obj.anim_id;
        if (obj.is_food) {                                                                       // the tile that this cell of the current stage shows
            const int32_t stage = food_stage_anim(cell.interactive_id);
            if (stage >= 0) anim_id = stage;
        }
        if (anim_id >= 0) draw_template_world(anim_id, ax * TILE_SIZE, ay * TILE_SIZE);
        return;
    }
    if (has_anthill_bases_) {                                                                    // a hill: the colony whose anchor it is
        for (size_t t = 0; t < 4; ++t) {
            if (hill_anchor_cell_[t].x == ax && hill_anchor_cell_[t].y == ay) {
                draw_template_world(anim_id_hill_[t], ax * TILE_SIZE, ay * TILE_SIZE);
                return;
            }
        }
    }
}

void Renderer::render_terrain_layer2_structures(const ants::sim::Grid& grid, const ants::sim::WorldState* world) {
    // One row-major pass over anchor cells (FUN_01008089 with mode 2): the rows from top / 32 - 3 up to (bottom / 32 + 3 + 1, exclusive) of the view rectangle, the columns
    // likewise, clipped to the map. An object whose anchor lies further out is not drawn even when its art reaches into the view (docs 5.54); each draw is rect-culled.
    constexpr int32_t kMargin = 3;
    const LayoutRect view = world_view();
    const int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE - kMargin);
    const int32_t end_col   = std::min(static_cast<int32_t>(grid.width()) - 1,
                                       (static_cast<int32_t>(camera_.x) + view.w) / TILE_SIZE + kMargin);
    const int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE - kMargin);
    const int32_t end_row   = std::min(static_cast<int32_t>(grid.height()) - 1,
                                       (static_cast<int32_t>(camera_.y) + view.h) / TILE_SIZE + kMargin);

    const bool fog = world && world->fog_of_war_enabled;
    auto explored = [&](int32_t c, int32_t r) { return !fog || world->is_tile_revealed(c, r); };

    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            // 1. Static object anchored on this cell
            if (level_set_ && !object_index_by_cell_.empty()) {
                const size_t idx = static_cast<size_t>(r) * map_width_ + static_cast<size_t>(c);
                if (idx < object_index_by_cell_.size() && object_index_by_cell_[idx] >= 0) {
                    draw_static_object(static_decor_objects_[static_cast<size_t>(object_index_by_cell_[idx])], grid, world);
                }
            }

            // 2. Anthills anchored on this cell (animated, always drawn: the fog pass covers unexplored ground)
            if (has_anthill_bases_) {
                for (size_t t = 0; t < 4; ++t) {
                    if (hill_anchor_cell_[t].x == c && hill_anchor_cell_[t].y == r) {
                        draw_template_world(anim_id_hill_[t], c * TILE_SIZE, r * TILE_SIZE);
                    }
                }
            }

            // 2b. A cell of an object that is explored while the object's anchor is not: the object is drawn from here (FUN_01008089's second path)
            if (fog && level_set_) draw_object_from_body_cell(grid, *world, c, r);

            // 3. Runtime item on this cell
            const auto& cell = grid.get_cell(static_cast<uint32_t>(c), static_cast<uint32_t>(r));
            if (cell.is_empty_overlay()) continue;
            const int32_t wx = c * TILE_SIZE;
            const int32_t wy = r * TILE_SIZE;

            // Bridges: not part of the original's fog-hidden set
            if ((cell.interactive_id >= ants::sim::TILE_BRIDGE1 && cell.interactive_id <= ants::sim::TILE_BRIDGE4) ||
                cell.interactive_id == ants::sim::TILE_BRIDGE4B) {
                draw_template_world(cell.interactive_id, wx, wy);
                continue;
            }

            // Fire walls, bombs, food, power-ups are hidden while their anchor tile is unexplored
            if (cell.has_fire()) {
                if (explored(c, r)) draw_template_world(anim_id_fire_, wx, wy);
                continue;
            }
            if (cell.has_bomb()) {
                if (explored(c, r)) draw_template_world(anim_id_bomb_[cell.interactive_owner % 4u], wx, wy);
                continue;
            }
            if (cell.has_lunchbox()) {
                if (explored(c, r)) draw_template_world(anim_id_lunchbox_, wx, wy);
                continue;
            }
            if (cell.has_powerup()) {
                if (explored(c, r) && cell.powerup_type >= 1 && cell.powerup_type <= 5) {
                    draw_template_world(anim_id_powerup_[cell.powerup_type], wx, wy);
                }
            }
        }
    }

    // Fallback for custom test grids that only carry grid.anthills() (no layer-2 hill anchors)
    if (!has_anthill_bases_) {
        for (const auto& a : grid.anthills()) {
            if (fog && a.team_id != hud_team_id_ &&
                !world->is_tile_revealed(static_cast<int32_t>(a.x), static_cast<int32_t>(a.y))) {
                continue;
            }
            draw_template_world(anim_id_hill_[a.team_id % 4],
                                static_cast<int32_t>(a.x) * TILE_SIZE, static_cast<int32_t>(a.y) * TILE_SIZE);
        }
    }
}

void Renderer::collect_object_list_sprites() {
    if (!archive_ || !texture_cache_) return;
    for (const auto& spr : object_list_sprites_) {
        const AnimBounds& b = anim_bounds(spr.anim_id);
        if (!b.valid) continue;
        const int32_t cam_x = static_cast<int32_t>(camera_.x);
        const int32_t cam_y = static_cast<int32_t>(camera_.y);
        const LayoutRect view = world_view();
        if (spr.px + b.x1 <= cam_x || spr.px + b.x0 >= cam_x + view.w ||
            spr.py + b.y1 <= cam_y || spr.py + b.y0 >= cam_y + view.h) {
            continue;
        }
        RenderItem item{};
        item.sort_y = spr.py;   // original sort key = row*32+16 (cell centre)
        const int32_t anim_id = spr.anim_id, px = spr.px, py = spr.py;
        item.draw_func = [this, anim_id, px, py](SDL_Renderer*, TextureCache&) {
            this->draw_template_world(anim_id, px, py);
        };
        render_queue_.push_back(std::move(item));
    }
}

// Non-looping frame of an animation `elapsed_ms` after it started (holds the last frame); -1 once it is over.
static int32_t effect_frame_at(const ants::assets::AnimationSequence& seq, uint32_t elapsed_ms) {
    uint32_t end = 0;
    for (size_t i = 0; i < seq.subitems.size(); ++i) {
        end += seq.subitems[i].val3;
        if (elapsed_ms < end) return static_cast<int32_t>(i);
    }
    return -1;
}

void Renderer::collect_flower_droppers(const ants::sim::WorldState& world) {
    if (!archive_ || !texture_cache_) return;

    for (const auto& fd : world.flower_droppers) {
        // Falling power-up droplet (the flower itself is an object-list sprite). Effect sprite: anchored at the drop
        // tile's top-left, sort key row*32, hidden while that tile is unexplored (Ants.exe FUN_0100fdd8).
        if (!fd.is_dropping) continue;
        if (world.fog_of_war_enabled && !world.is_tile_revealed(fd.drop_x, fd.drop_y)) continue;
        const char* anim_name = "FD_COMB";
        switch (fd.powerup_type) {
            case 0: anim_name = "FD_BOMB"; break;
            case 1: anim_name = "FD_COMB"; break;
            case 2: anim_name = "FD_THIEF"; break;
            case 3: anim_name = "FD_SWIM"; break;
            case 4: anim_name = "FD_FIRE"; break;
            default: break;
        }
        const auto* drop_anim = archive_->find_animation(anim_name);
        if (!drop_anim || drop_anim->subitems.empty()) continue;
        int32_t frame = effect_frame_at(*drop_anim, fd.drop_elapsed_ms + sub_tick_ms_);
        if (frame < 0) frame = static_cast<int32_t>(drop_anim->subitems.size()) - 1;
        const int32_t wx = fd.drop_x * TILE_SIZE;
        const int32_t wy = fd.drop_y * TILE_SIZE;
        RenderItem item{};
        item.sort_y = wy;
        item.draw_func = [this, drop_anim, frame, wx, wy](SDL_Renderer*, TextureCache&) {
            const LayoutRect view = world_view();
            const int32_t sx = view.x + (wx - static_cast<int32_t>(camera_.x));
            const int32_t sy = view.y + (wy - static_cast<int32_t>(camera_.y));
            this->draw_frame_parts(drop_anim->subitems[static_cast<size_t>(frame)], sx, sy);
        };
        render_queue_.push_back(std::move(item));
    }
}

void Renderer::render_fog_of_war(const ants::sim::WorldState& world) {
    if (!world.fog_of_war_enabled || world.fog_revealed.empty() || !archive_ || !texture_cache_) {
        return;
    }

    const LayoutRect view = world_view();
    int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE);
    int32_t end_col   = std::min(static_cast<int32_t>(world.width) - 1,
                                 (static_cast<int32_t>(camera_.x) + view.w + 31) / TILE_SIZE);
    int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE);
    int32_t end_row   = std::min(static_cast<int32_t>(world.height) - 1,
                                 (static_cast<int32_t>(camera_.y) + view.h + 31) / TILE_SIZE);

    // Authentic 1998 Ants.exe dither anim lookup table (Ants.exe VA 0x1001a7a & 0x10087c0..0x10087da)
    // Formula: idx = ((c0 * 2 + c1) * 2 + 2 + c2) * 2 + c3
    // c0 = North revealed (1/0), c1 = East revealed, c2 = South revealed, c3 = West revealed
    // Sprites: dither0.bmp (352) through dither15.bmp (367)
    static constexpr uint16_t FOG_DITHER_SPRITE_LUT[20] = {
        0,   0,   0,   0,   // 0..3: unused
        365, // idx  4: N=0 E=0 S=0 W=0 -> dither13.bmp
        363, // idx  5: N=0 E=0 S=0 W=1 -> dither11.bmp
        362, // idx  6: N=0 E=0 S=1 W=0 -> dither10.bmp
        359, // idx  7: N=0 E=0 S=1 W=1 -> dither7.bmp
        364, // idx  8: N=0 E=1 S=0 W=0 -> dither12.bmp
        367, // idx  9: N=0 E=1 S=0 W=1 -> dither15.bmp
        358, // idx 10: N=0 E=1 S=1 W=0 -> dither6.bmp
        353, // idx 11: N=0 E=1 S=1 W=1 -> dither1.bmp
        361, // idx 12: N=1 E=0 S=0 W=0 -> dither9.bmp
        360, // idx 13: N=1 E=0 S=0 W=1 -> dither8.bmp
        366, // idx 14: N=1 E=0 S=1 W=0 -> dither14.bmp
        354, // idx 15: N=1 E=0 S=1 W=1 -> dither2.bmp
        357, // idx 16: N=1 E=1 S=0 W=0 -> dither5.bmp
        356, // idx 17: N=1 E=1 S=0 W=1 -> dither4.bmp
        355, // idx 18: N=1 E=1 S=1 W=0 -> dither3.bmp
        352  // idx 19: N=1 E=1 S=1 W=1 -> dither0.bmp
    };

    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            if (world.is_tile_revealed(c, r)) {
                continue; // Revealed tiles are not covered by fog
            }

            auto is_fog = [&](int32_t x, int32_t y) -> int32_t {
                if (x < 0 || x >= static_cast<int32_t>(world.width) ||
                    y < 0 || y >= static_cast<int32_t>(world.height)) {
                    return 1; // Out-of-bounds tiles are unrevealed fog
                }
                return world.is_tile_revealed(x, y) ? 0 : 1; // 1 = Fog, 0 = Revealed
            };

            int32_t c0 = is_fog(c, r - 1); // North
            int32_t c1 = is_fog(c + 1, r); // East
            int32_t c2 = is_fog(c, r + 1); // South
            int32_t c3 = is_fog(c - 1, r); // West

            int32_t idx = ((c0 * 2 + c1) * 2 + 2 + c2) * 2 + c3;
            if (idx < 4 || idx > 19) continue;

            uint32_t sprite_id = FOG_DITHER_SPRITE_LUT[idx];
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(c * TILE_SIZE, r * TILE_SIZE, sx, sy);
            draw_sprite(sprite_id, sx, sy);
        }
    }
}

void Renderer::collect_visual_effects(const ants::sim::WorldState& world) {
    if (!archive_ || !texture_cache_) return;
    for (const auto& eff : world.effects) {
        // Effect sprites of the original are hidden while their anchor tile is unexplored (vtable +0x3c, 0x101a40f)
        if (eff.fog_gated && world.fog_of_war_enabled && !world.is_tile_revealed(eff.px / TILE_SIZE, eff.py / TILE_SIZE)) continue;
        const auto* anim = archive_->find_animation(eff.anim_name);
        if (!anim && !eff.anim_name.empty()) {
            std::string low = eff.anim_name;
            for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            anim = archive_->find_animation(low);
        }
        if (!anim || anim->subitems.empty()) continue;

        size_t sub_idx = 0;
        if (eff.looping) {
            // The clip repeats until the simulation removes the effect (the dust ball of a foreign pile-up, Cloud54)
            uint32_t total = 0;
            for (const auto& sub : anim->subitems) total += sub.val3;
            const uint32_t t = (total > 0) ? (eff.elapsed_ms + sub_tick_ms_) % total : 0u;
            const int32_t f = effect_frame_at(*anim, t);
            sub_idx = (f < 0) ? anim->subitems.size() - 1 : static_cast<size_t>(f);
        } else {
            // Table-4 frame durations in real time: sim elapsed time plus the fraction of the current 50 ms tick
            const uint32_t elapsed_ms = eff.elapsed_ms + sub_tick_ms_;
            const uint32_t lifetime_ms = eff.duration_ms > 0 ? eff.duration_ms : static_cast<uint32_t>(eff.total_frames) * 50u;
            if (elapsed_ms >= lifetime_ms) continue; // the sprite ended at the end of its last frame
            const int32_t f = effect_frame_at(*anim, elapsed_ms);
            sub_idx = (f < 0) ? anim->subitems.size() - 1 : static_cast<size_t>(f);
        }
        RenderItem item{};
        item.sort_y = (eff.y_key != 0) ? eff.y_key : eff.py;
        const int32_t px = eff.px, py = eff.py;
        item.draw_func = [this, anim, sub_idx, px, py](SDL_Renderer*, TextureCache&) {
            const LayoutRect view = world_view();
            const int32_t sx = view.x + (px - static_cast<int32_t>(camera_.x));
            const int32_t sy = view.y + (py - static_cast<int32_t>(camera_.y));
            if (sx < view.x - 320 || sx > view.x + view.w + 320 ||
                sy < view.y - 320 || sy > view.y + view.h + 320) return;
            this->draw_frame_parts(anim->subitems[sub_idx], sx, sy);
        };
        render_queue_.push_back(std::move(item));
    }
}

void Renderer::spawn_transient_effect(const std::string& anim_name, int32_t px, int32_t py, bool is_screen_space) {
    if (anim_name == "xmarks") {
        // The click marker is a single object: a new click stops the previous marker (Ants.exe FUN_01010627)
        transient_effects_.erase(std::remove_if(transient_effects_.begin(), transient_effects_.end(),
                                                [](const TransientEffect& e) { return e.anim_name == "xmarks"; }),
                                 transient_effects_.end());
    }
    TransientEffect eff;
    eff.anim_name = anim_name;
    eff.px = px;
    eff.py = py;
    eff.elapsed_sec = 0.0f;
    eff.is_screen_space = is_screen_space;
    transient_effects_.push_back(std::move(eff));
}

void Renderer::update_transient_effects(float dt) {
    if (transient_effects_.empty() || !archive_) return;
    for (auto it = transient_effects_.begin(); it != transient_effects_.end();) {
        it->elapsed_sec += dt;
        const auto* anim = archive_->find_animation(it->anim_name);
        if (!anim && !it->anim_name.empty()) {
            std::string low = it->anim_name;
            for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            anim = archive_->find_animation(low);
        }
        if (!anim || anim->subitems.empty()) {
            it = transient_effects_.erase(it);
            continue;
        }
        float total_dur = 0.0f;
        for (const auto& sub : anim->subitems) {
            float sub_dur = (sub.val3 > 0) ? (static_cast<float>(sub.val3) / 1000.0f) : 0.060f;
            total_dur += sub_dur;
        }
        if (total_dur <= 0.0f) total_dur = 0.420f;
        if (it->elapsed_sec >= total_dur) {
            it = transient_effects_.erase(it);
        } else {
            ++it;
        }
    }
}

void Renderer::collect_transient_sprites() {
    if (!archive_ || !texture_cache_) return;
    for (const auto& eff : transient_effects_) {
        if (eff.is_screen_space) continue; // screen-space feedback is not part of the world sprite list
        const auto* anim = archive_->find_animation(eff.anim_name);
        if (!anim && !eff.anim_name.empty()) {
            std::string low = eff.anim_name;
            for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            anim = archive_->find_animation(low);
        }
        if (!anim || anim->subitems.empty()) continue;

        const uint32_t elapsed_ms = static_cast<uint32_t>(eff.elapsed_sec * 1000.0f);
        const int32_t f = effect_frame_at(*anim, elapsed_ms);
        const size_t sub_idx = (f < 0) ? anim->subitems.size() - 1 : static_cast<size_t>(f);
        OverlayItem item{};
        item.created_ms = overlay_now_ms() - static_cast<int64_t>(elapsed_ms);   // the click marker is a view child
        const int32_t px = eff.px, py = eff.py;
        item.draw = [this, anim, sub_idx, px, py]() {
            const LayoutRect view = world_view();
            const int32_t sx = view.x + (px - static_cast<int32_t>(camera_.x));
            const int32_t sy = view.y + (py - static_cast<int32_t>(camera_.y));
            this->draw_frame_parts(anim->subitems[sub_idx], sx, sy);
        };
        overlay_queue_.push_back(std::move(item));
    }
}

void Renderer::render_software_cursor(CursorType type, int32_t screen_x, int32_t screen_y, uint32_t anim_tick) {
    if (!archive_ || !texture_cache_ || !renderer_) return;
    (void)anim_tick;

    uint32_t anim_id = 41; // c_normal
    switch (type) {
        case CursorType::Normal:      anim_id = 41; break;
        case CursorType::Select:      anim_id = 42; break;
        case CursorType::Move:        anim_id = 44; break;
        case CursorType::ThiefTarget: anim_id = 43; break;
        case CursorType::Attack:      anim_id = 33; break;
        case CursorType::ScrollN:     anim_id = 51; break;
        case CursorType::ScrollNE:    anim_id = 46; break;
        case CursorType::ScrollE:     anim_id = 45; break;
        case CursorType::ScrollSE:    anim_id = 49; break;
        case CursorType::ScrollS:     anim_id = 48; break;
        case CursorType::ScrollSW:    anim_id = 52; break;
        case CursorType::ScrollW:     anim_id = 50; break;
        case CursorType::ScrollNW:    anim_id = 47; break;
        case CursorType::Food:        anim_id = 54; break;
        default:                      anim_id = 41; break;
    }

    if (anim_id >= archive_->animation_count()) return;
    const auto& anim = archive_->get_animation(anim_id);
    if (anim.subitems.empty()) return;

    size_t sub_idx = 0;
    if (anim.subitems.size() > 1) {
        uint32_t total_dur_ms = 0;
        for (const auto& sub : anim.subitems) {
            total_dur_ms += (sub.val3 > 0) ? sub.val3 : 100;
        }
        if (total_dur_ms == 0) total_dur_ms = 1000;
        uint32_t now_ms = SDL_GetTicks() % total_dur_ms;
        uint32_t accum = 0;
        for (size_t i = 0; i < anim.subitems.size(); ++i) {
            uint32_t dur = (anim.subitems[i].val3 > 0) ? anim.subitems[i].val3 : 100;
            if (now_ms < accum + dur) {
                sub_idx = i;
                break;
            }
            accum += dur;
            if (i + 1 == anim.subitems.size()) {
                sub_idx = i;
            }
        }
    }

    const auto& sub = anim.subitems[sub_idx];
    draw_frame_parts(sub, screen_x, screen_y);
}

// The stored (or mirrored) Table-4 animation of the clip the simulation plays on the ant; null while another system animates it.
const ants::assets::AnimationSequence* Renderer::ant_loco_sequence(const ants::sim::AntSnapshot& ant) const {
    if (!archive_ || ant.loco_clip == 0x7FFE || ant.loco_clip >= archive_->animation_count()) return nullptr;
    const auto& stored = archive_->get_animation(ant.loco_clip);
    if (!ant.loco_mirrored) return &stored;
    // Mirrored clips (SW, W, NW) use the archive's mirrored copies of the stored SE, E and NE animations.
    if (stored.name.size() > 3) {
        const char digit = stored.name[stored.name.size() - 3];
        const int mirrored_dir = (digit == '2') ? 5 : (digit == '9') ? 6 : (digit == '8') ? 7 : -1;
        if (mirrored_dir >= 0) {
            return archive_->get_directional_animation(stored.name.substr(0, stored.name.size() - 3),
                                                       static_cast<ants::assets::Direction>(mirrored_dir));
        }
    }
    return nullptr;
}

Renderer::AntClipPrediction Renderer::predict_ant_clip(const ants::sim::AntSnapshot& ant,
                                                       const ants::assets::AnimationSequence* seq) const {
    AntClipPrediction p;
    if (!seq || seq->subitems.empty()) return p;
    p.frame = std::min<size_t>(ant.loco_frame, seq->subitems.size() - 1);
    using S = ants::sim::UnitState;
    // The real-time player of the original steps every clip when a frame ends (AnimationStep 0x102b997 with timeGetTime) and the screen is redrawn on every scheduler pass
    // (REFRESH, period 0, 0x102c4d1), so whatever clip the ant plays, walk, idle, swim, dive, climb, harvest or an action, the frames that end before the next tick are shown
    // in advance. A frame's displacement applies when the frame ends. The last frame of a clip stays until the next tick, where the simulation's step callback decides what
    // comes next (the next tile, a snap to the tile centre, another clip); only an idle clip is known to loop.
    if (ant.state == S::Burn) return p;                                // the frozen ant under its burn overlay is not drawn
    const bool loops = (ant.state == S::Idle || ant.state == S::GuardIdle);
    int32_t left = static_cast<int32_t>(ant.loco_left_ms);
    int32_t t = static_cast<int32_t>(sub_tick_ms_);
    for (int guard = 0; guard < 64 && t >= left; ++guard) {
        if (p.frame + 1 < seq->subitems.size()) {
            t -= left;
            p.dx += seq->subitems[p.frame].val1;
            p.dy += seq->subitems[p.frame].val2;
            ++p.frame;
        } else if (loops) {
            t -= left;
            p.frame = 0;
        } else {
            break;
        }
        left = std::max<int32_t>(1, static_cast<int32_t>(seq->subitems[p.frame].val3));
    }
    return p;
}

void Renderer::draw_single_ant(const ants::sim::AntSnapshot& ant) {
    // The hill actions (enter, hatch, raid) are ordinary clips played on the tile centre the simulation puts the ant
    // on, so there is no special anchor: every ant is drawn where it stands.
    int32_t sx = 0, sy = 0;
    camera_.world_to_screen(ant.px, ant.py, sx, sy);
    // The original clips per sprite part (FUN_0102fb6d), never by the ant's anchor: the art of a walking ant reaches 35 px above
    // it and a blown ant's art (aggb / bomb flights) up to ~155 px away from the anchor that jumped 128 px. Only an ant whose art
    // cannot reach the playfield is skipped.
    constexpr int32_t kArtMargin = 160;
    const LayoutRect view = world_view();
    if (sx < view.x - kArtMargin || sx > view.x + view.w + kArtMargin ||
        sy < view.y - kArtMargin || sy > view.y + view.h + kArtMargin) return;

    // The original has neither a shadow nor a hop: a flight is the displacement baked into the aggb / aggh clips.
    const int32_t render_y = sy;

    // The simulation plays the exact ants.chd clip on every ant (idle, walk on each terrain, swim, dive, climb, can't-go and all the actions), so the frame to draw is that
    // clip's current frame. There is no interpolation: as in the 1998 game a sprite moves only when its animation frame ends; the real-time player of the original does that at
    // the moment a frame ends, not at 50 ms simulation ticks, so the frames that end before the next tick are shown in advance (predict_ant_clip). Mirrored clips (SW, W, NW)
    // use the archive's mirrored copies of the stored SE, E and NE animations.
    const ants::assets::AnimationSequence* loco_seq = ant_loco_sequence(ant);
    const AntClipPrediction pred = predict_ant_clip(ant, loco_seq);

    if (ant.frozen) {
        // The display loop (0x10088e7) skips a frozen ant (sprite slot +0x40 is +0xfc): the dud bomb's ?bu clip is a full-body
        // overlay, and drawing the idle ant beneath it would show a ghost body around the flames.
    } else if (loco_seq && !loco_seq->subitems.empty()) {
        // Draws one animation frame's parts at the ant in the original order (last stored part first, so the first stored part - e.g. a carried lunchbox in front of the body -
        // is on top) with the original ant colour rule.
        draw_frame_parts(loco_seq->subitems[pred.frame], sx + pred.dx, render_y + pred.dy, ant.loco_mirrored, ant_colour(ant.player_id));
    }

}

size_t Renderer::get_anim_subitem_by_time(const ants::assets::AnimationSequence& seq, uint32_t now_ms) {
    if (seq.subitems.empty()) return 0;
    uint32_t total_ms = 0;
    for (const auto& sub : seq.subitems) {
        total_ms += (sub.val3 > 0 ? sub.val3 : 100);
    }
    if (total_ms == 0) return 0;
    uint32_t t = now_ms % total_ms;
    uint32_t accum = 0;
    for (size_t i = 0; i < seq.subitems.size(); ++i) {
        accum += (seq.subitems[i].val3 > 0 ? seq.subitems[i].val3 : 100);
        if (t < accum) return i;
    }
    return 0;
}

void Renderer::draw_anthill_selection_brackets(int32_t cx, int32_t cy, uint32_t elapsed_ms) {
    if (archive_ && texture_cache_ && 59 < archive_->animation_count()) {
        const auto& ears_seq = archive_->get_animation(59); // hillears
        if (!ears_seq.subitems.empty()) {
            size_t sub_idx = get_anim_subitem_by_time(ears_seq, elapsed_ms);
            draw_frame_parts(ears_seq.subitems[sub_idx], cx, cy);
            return;
        }
    }

    // Fallback: draw geometry lines around the 128x128 footprint
    const int32_t w = 128, h = 128;
    const int32_t x = cx - w / 2 + picture_.x, y = cy - h / 2 + picture_.y;      // (drawn straight to SDL: the picture's corner is added here)
    // Fallback: draw geometry lines
    SDL_SetRenderDrawColor(renderer_, 50, 220, 50, 255);
    int32_t arm = 20;
    int32_t thick = 3;

    // Top-Left bracket
    for (int32_t t = 0; t < thick; ++t) {
        SDL_RenderDrawLine(renderer_, x + 2, y + t, x + arm, y + t);
        SDL_RenderDrawLine(renderer_, x + t, y + 2, x + t, y + arm);
    }
    SDL_RenderDrawLine(renderer_, x + 1, y + 1, x + 2, y);
    SDL_RenderDrawLine(renderer_, x, y + 2, x + 1, y + 1);

    // Top-Right bracket
    for (int32_t t = 0; t < thick; ++t) {
        SDL_RenderDrawLine(renderer_, x + w - arm, y + t, x + w - 3, y + t);
        SDL_RenderDrawLine(renderer_, x + w - 1 - t, y + 2, x + w - 1 - t, y + arm);
    }
    SDL_RenderDrawLine(renderer_, x + w - 3, y, x + w - 2, y + 1);
    SDL_RenderDrawLine(renderer_, x + w - 2, y + 1, x + w - 1, y + 2);

    // Bottom-Left bracket
    for (int32_t t = 0; t < thick; ++t) {
        SDL_RenderDrawLine(renderer_, x + 2, y + h - 1 - t, x + arm, y + h - 1 - t);
        SDL_RenderDrawLine(renderer_, x + t, y + h - arm, x + t, y + h - 3);
    }
    SDL_RenderDrawLine(renderer_, x, y + h - 3, x + 1, y + h - 2);
    SDL_RenderDrawLine(renderer_, x + 1, y + h - 2, x + 2, y + h - 1);

    // Bottom-Right bracket
    for (int32_t t = 0; t < thick; ++t) {
        SDL_RenderDrawLine(renderer_, x + w - arm, y + h - 1 - t, x + w - 3, y + h - 1 - t);
        SDL_RenderDrawLine(renderer_, x + w - 1 - t, y + h - arm, x + w - 1 - t, y + h - 3);
    }
    SDL_RenderDrawLine(renderer_, x + w - 1, y + h - 3, x + w - 2, y + h - 2);
    SDL_RenderDrawLine(renderer_, x + w - 2, y + h - 2, x + w - 3, y + h - 1);
}

void Renderer::collect_ant_units(const ants::sim::WorldState& world) {
    for (const auto& a : world.ants) {
        // The original shows the local player's and the allies' ants always (0x101aa0d); every other ant only on explored ground
        const bool allied = hud_team_id_ < world.player_alliances.size() && a.player_id < world.player_alliances.size() &&
                            world.player_alliances[hud_team_id_] == a.player_id && world.player_alliances[a.player_id] == hud_team_id_;
        if (world.fog_of_war_enabled && a.player_id != hud_team_id_ && !allied &&
            !world.is_tile_revealed(a.tile_x, a.tile_y)) {
            continue; // Concealed enemy ant under fog of war
        }
        RenderItem item{};
        // The sort key is the sprite's y: a thrown ant's key jumps with its position at the end of the first flight frame
        const AntClipPrediction pred = predict_ant_clip(a, ant_loco_sequence(a));
        item.sort_y = a.py + pred.dy;
        item.draw_func = [this, a, pred](SDL_Renderer*, TextureCache&) {
            this->draw_single_ant(a);
            // FUN_0101b802 (which the display loop skips for a frozen ant) with [4b14] != 0: sprintf("%d", hp), white, at the sprite position (the predicted one: the sprite
            // moves when a frame ends), in the fixed system font
            if (show_hp_ && !a.frozen) {
                if (in_world_target_ && pass_.camera.zoom != zoom::kNormal) {              // a zoomed world: the digits are text, not world art; they are drawn after the copy at one size (draw_deferred_digits)
                    deferred_digits_.push_back(DeferredDigits{std::to_string(a.hp), a.px + pred.dx, a.py + pred.dy});
                } else {
                    const LayoutRect view = world_view();
                    this->draw_fixed_text(std::to_string(a.hp), view.x + (a.px + pred.dx - static_cast<int32_t>(camera_.x)),
                                          view.y + (a.py + pred.dy - static_cast<int32_t>(camera_.y)), ants::assets::ColorRGBA{255, 255, 255, 255});
                }
            }
        };
        render_queue_.push_back(std::move(item));
    }
}

// Signed score number as the original draws it (Ants.exe FUN_01010452 with the sign flag): a 6-slot field of 9 px
// slots, leading zeros skipped but still advancing, the sign glyph in the slot of the first significant digit and
// every digit from there on shifted one slot to the right.
void Renderer::draw_score_number(int32_t amount, int32_t world_x, int32_t world_y) {
    int32_t value = std::clamp(amount < 0 ? -amount : amount, 0, 999999);
    int32_t divisor = 100000;
    bool leading = true;
    bool sign_pending = true;
    int32_t x = world_x;
    while (divisor > 0) {
        const int32_t digit = value / divisor;
        value %= divisor;
        if (digit != 0 || divisor == 1) leading = false;
        if (!leading) {
            if (sign_pending) {
                draw_template_world(amount > 0 ? anim_id_plus_ : anim_id_minus_, x, world_y);
                x += 9;
                sign_pending = false;
            }
            draw_template_world(anim_id_digit_[digit], x, world_y);
        }
        divisor /= 10;
        x += 9;
    }
}

void Renderer::collect_score_bubbles(const ants::sim::WorldState& world) {
    if (!archive_ || !texture_cache_) return;
    for (const auto& b : world.score_bubbles) {
        // 20 steps, one every 20 ms, 5 px each: up for a gain, down for a loss; the sprite ends after 400 ms
        const uint32_t t = b.elapsed_ms + sub_tick_ms_;
        if (t >= 400 || b.amount == 0) continue;
        const int32_t steps = static_cast<int32_t>(std::min<uint32_t>(20u, t / 20u + 1u));
        const int32_t y = b.y + (b.amount > 0 ? -5 : 5) * steps;
        OverlayItem item{};
        item.created_ms = overlay_now_ms() - static_cast<int64_t>(t);
        const int32_t amount = b.amount, x = b.x;
        item.draw = [this, amount, x, y]() { this->draw_score_number(amount, x, y); };
        overlay_queue_.push_back(std::move(item));
    }
}

void Renderer::collect_hill_brackets(const ants::sim::Grid& grid, int32_t selected_base_team_id) {
    if (selected_base_team_id < 0 || !archive_) {
        hill_marker_team_ = -1;
        return;
    }
    // The selection brackets ("hillears") sit at the top-left of tile (anchor row + 1, anchor col + 1), i.e. the centre
    // of the 4x4 hill footprint (Ants.exe FUN_01028b4c); they are sprites of the sorted list with that y as key.
    int32_t ax = -1, ay = -1;
    if (has_anthill_bases_ && selected_base_team_id < 4 &&
        anthill_bases_[static_cast<size_t>(selected_base_team_id)].x >= 0) {
        ax = anthill_bases_[static_cast<size_t>(selected_base_team_id)].x * TILE_SIZE + 2 * TILE_SIZE;
        ay = anthill_bases_[static_cast<size_t>(selected_base_team_id)].y * TILE_SIZE + 2 * TILE_SIZE;
    } else {
        for (const auto& a : grid.anthills()) {
            if (static_cast<int32_t>(a.team_id) == selected_base_team_id) {
                ax = static_cast<int32_t>(a.x) * TILE_SIZE + TILE_SIZE;
                ay = static_cast<int32_t>(a.y) * TILE_SIZE + TILE_SIZE;
                break;
            }
        }
    }
    if (ax < 0) return;
    if (hill_marker_team_ != selected_base_team_id) {   // a new marker (the template restarts on every selection)
        hill_marker_team_ = selected_base_team_id;
        hill_marker_start_ms_ = overlay_now_ms();
    }
    OverlayItem item{};
    item.created_ms = hill_marker_start_ms_;
    const int64_t start = hill_marker_start_ms_;
    item.draw = [this, ax, ay, start]() {
        const LayoutRect view = world_view();
        const int32_t sx = view.x + (ax - static_cast<int32_t>(camera_.x));
        const int32_t sy = view.y + (ay - static_cast<int32_t>(camera_.y));
        if (sx >= view.x - 128 && sx <= view.x + view.w + 128 &&
            sy >= view.y - 128 && sy <= view.y + view.h + 128) {
            this->draw_anthill_selection_brackets(sx, sy, static_cast<uint32_t>(overlay_now_ms() - start));
        }
    };
    overlay_queue_.push_back(std::move(item));
}

void Renderer::draw_sorted_queue() {
    // Stable sort: sprites with equal keys keep their insertion order (later added = drawn on top)
    std::stable_sort(render_queue_.begin(), render_queue_.end(), [](const RenderItem& a, const RenderItem& b) {
        return a.sort_y < b.sort_y;
    });
    for (auto& item : render_queue_) {
        item.draw_func(renderer_, *texture_cache_);
    }
}

int64_t Renderer::overlay_now_ms() const {
    return anim_clock_pin_ms_ >= 0 ? static_cast<int64_t>(anim_clock_pin_ms_) : static_cast<int64_t>(SDL_GetTicks());
}

// The dud bomb's burn overlay (FUN_01021c68, created by SetAction 0xa at 0x101af27 - 0x101af66): the ?bu clip is a sprite of its own, a CHILD OF THE VIEW CONTAINER
// (AddChild at 0x101af66), so it is drawn after the whole map sprite, over every ant, in creation order with the other children (docs 5.60); it sits on the cell centre of
// its ant and its visibility test (vtable slot +0x3c, 0x101a40f) is the explored test of that cell. The ant underneath is frozen and not drawn.
void Renderer::collect_burn_overlays(const ants::sim::WorldState& world) {
    if (!archive_ || !texture_cache_) return;
    static const char type_letters[6] = { 'g', 'b', 'f', 't', 'c', 's' };
    const int64_t now = overlay_now_ms();
    for (const auto& a : world.ants) {
        if (a.burn_elapsed_ms < 0) continue;
        if (world.fog_of_war_enabled && !world.is_tile_revealed(a.tile_x, a.tile_y)) continue;
        const std::string bu_name = std::string("a") + type_letters[static_cast<size_t>(a.type) % 6] + "bu301";
        const auto* bu_seq = archive_->find_animation(bu_name);
        if (!bu_seq || bu_seq->subitems.empty()) continue;
        OverlayItem item{};
        item.created_ms = now - a.burn_elapsed_ms;
        const int32_t px = a.px, py = a.py;
        const uint32_t elapsed = static_cast<uint32_t>(a.burn_elapsed_ms) + sub_tick_ms_;
        const uint8_t colour = ant_colour(a.player_id);
        item.draw = [this, bu_seq, px, py, elapsed, colour]() {
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(px, py, sx, sy);
            const LayoutRect view = world_view();
            if (sx < view.x - 160 || sx > view.x + view.w + 160 || sy < view.y - 160 || sy > view.y + view.h + 160) return;
            this->draw_frame_parts(bu_seq->subitems[get_anim_subitem_by_time(*bu_seq, elapsed)], sx, sy, false, colour);
        };
        overlay_queue_.push_back(std::move(item));
    }
}

// Selection markers (Ants.exe FUN_01010373 -> FUN_0101b52f): one base sprite per selected ant (own or inspected enemy),
// a copy of dogears (hp >= 9), yelears (hp 3..8) or redears (hp <= 2) placed at the ant's own position. It is a child of
// the view container, so it is drawn after the whole map sprite (over ants, foliage and the fog) in creation order, and
// it runs its own clock from the moment it is created; it is re-created on every health change.
void Renderer::collect_selection_markers(const ants::sim::WorldState& world, int32_t selected_unit_id,
                                         const std::vector<uint32_t>& selected_unit_ids) {
    std::vector<uint32_t> selected = selected_unit_ids;
    if (selected.empty() && selected_unit_id > 0) selected.push_back(static_cast<uint32_t>(selected_unit_id));
    for (auto it = ears_state_.begin(); it != ears_state_.end();) {
        it = (std::find(selected.begin(), selected.end(), it->first) == selected.end()) ? ears_state_.erase(it) : std::next(it);
    }
    if (!archive_ || !texture_cache_) return;
    const int64_t now = overlay_now_ms();
    for (uint32_t id : selected) {
        const ants::sim::AntSnapshot* ant = nullptr;
        for (const auto& a : world.ants) if (a.id == id) { ant = &a; break; }
        if (!ant) continue;
        // the marker is removed while a thief is raiding a hill (action 0xd)
        if (ant->anim_state == static_cast<uint16_t>(ants::sim::UnitState::Infiltrating)) continue;
        EarsState& st = ears_state_[id];
        if (st.start_ms == 0 || st.hp != ant->hp) {
            st.hp = ant->hp;
            st.start_ms = std::max<int64_t>(1, now);
        }
        const uint32_t ears_id = (ant->hp >= 9) ? 58u : (ant->hp <= 2) ? 61u : 60u;
        if (ears_id >= archive_->animation_count()) continue;
        const auto& seq = archive_->get_animation(ears_id);
        if (seq.subitems.empty()) continue;
        OverlayItem item{};
        item.created_ms = st.start_ms;
        const AntClipPrediction pred = predict_ant_clip(*ant, ant_loco_sequence(*ant));     // the marker follows the sprite, which steps when a frame ends
        const int32_t px = ant->px + pred.dx, py = ant->py + pred.dy;
        const int64_t start = st.start_ms;
        item.draw = [this, &seq, px, py, start]() {
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(px, py, sx, sy);
            const LayoutRect view = world_view();
            if (sx < view.x - 160 || sx > view.x + view.w + 160 ||
                sy < view.y - 160 || sy > view.y + view.h + 160) return;
            const size_t f = get_anim_subitem_by_time(seq, static_cast<uint32_t>(std::max<int64_t>(0, overlay_now_ms() - start)));
            this->draw_frame_parts(seq.subitems[f], sx, sy);
        };
        overlay_queue_.push_back(std::move(item));
    }
}

void Renderer::draw_overlay_queue() {
    std::stable_sort(overlay_queue_.begin(), overlay_queue_.end(),
                     [](const OverlayItem& a, const OverlayItem& b) { return a.created_ms < b.created_ms; });
    for (auto& item : overlay_queue_) item.draw();
}

void Renderer::render_tile_grid(const ants::sim::Grid& grid, int32_t mouse_x, int32_t mouse_y) {
    if (!renderer_) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // (a debug aid on the screen: at a zoom a tile is about `ts` screen pixels (its outline is cut at the lattice line of its own edges, so that the outlines meet at a fractional zoom) and the
    // view shows `vis_w` x `vis_h` world pixels; at the zoom 1 these are the numbers it always had)
    const LayoutRect view = world_view();
    const int32_t ts = static_cast<int32_t>(static_cast<double>(TILE_SIZE) * static_cast<double>(camera_.zoom));
    const auto tile_outline = [&](int32_t col, int32_t row, int32_t sx, int32_t sy, int32_t inset) {
        int32_t ex = 0, ey = 0;
        camera_.world_to_screen((col + 1) * TILE_SIZE, (row + 1) * TILE_SIZE, ex, ey);
        return placed(sx + inset, sy + inset, std::max(1, ex - sx - 2 * inset), std::max(1, ey - sy - 2 * inset));
    };
    const int32_t vis_w = zoom::visible(view.w, camera_.zoom);
    const int32_t vis_h = zoom::visible(view.h, camera_.zoom);
    int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE);
    int32_t end_col   = std::min(static_cast<int32_t>(grid.width()) - 1,
                                 (static_cast<int32_t>(camera_.x) + vis_w + 31) / TILE_SIZE);
    int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE);
    int32_t end_row   = std::min(static_cast<int32_t>(grid.height()) - 1,
                                 (static_cast<int32_t>(camera_.y) + vis_h + 31) / TILE_SIZE);

    // Determine hovered tile under mouse
    int32_t hover_tx = -1;
    int32_t hover_ty = -1;
    if (view.contains(mouse_x, mouse_y)) {
        int32_t world_x = camera_.world_x_at(mouse_x - view.x);
        int32_t world_y = camera_.world_y_at(mouse_y - view.y);
        hover_tx = std::clamp(world_x / TILE_SIZE, 0, static_cast<int32_t>(grid.width()) - 1);
        hover_ty = std::clamp(world_y / TILE_SIZE, 0, static_cast<int32_t>(grid.height()) - 1);
    } else {
        hover_tx = std::clamp(camera_.centre_world_x() / TILE_SIZE, 0, static_cast<int32_t>(grid.width()) - 1);
        hover_ty = std::clamp(camera_.centre_world_y() / TILE_SIZE, 0, static_cast<int32_t>(grid.height()) - 1);
    }

    // 1. Draw tile outline for each tile and coordinates in bottom-left of each tile
    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(c * TILE_SIZE, r * TILE_SIZE, sx, sy);

            // Subtle semi-transparent tile outline
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 75);
            const SDL_Rect tile_rect = tile_outline(c, r, sx, sy, 0);
            SDL_RenderDrawRect(renderer_, &tile_rect);

            // Coordinates inside bottom-left of tile (a tile of half size has no room for them)
            if (ts >= TILE_SIZE) {
                std::string c_str = std::to_string(c) + "," + std::to_string(r);
                draw_text(c_str, sx + 3, sy + ts - 8, ants::assets::ColorRGBA{0, 0, 0, 180});
                draw_text(c_str, sx + 2, sy + ts - 9, ants::assets::ColorRGBA{255, 255, 200, 220});
            }
        }
    }

    // 2. Active hovered tile highlight
    if (hover_tx >= 0 && hover_ty >= 0) {
        int32_t hsx = 0, hsy = 0;
        camera_.world_to_screen(hover_tx * TILE_SIZE, hover_ty * TILE_SIZE, hsx, hsy);
        SDL_SetRenderDrawColor(renderer_, 0, 255, 255, 255);
        const SDL_Rect h1 = tile_outline(hover_tx, hover_ty, hsx, hsy, 0);
        SDL_RenderDrawRect(renderer_, &h1);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 0, 220);
        const SDL_Rect h2 = tile_outline(hover_tx, hover_ty, hsx, hsy, 1);
        SDL_RenderDrawRect(renderer_, &h2);
    }

    // 3. Tile coordinates readout badge in the bottom left-hand corner of the playfield
    int32_t badge_x = view.x + 4;
    int32_t badge_y = view.y + view.h - 18;
    int32_t badge_w = 88;
    int32_t badge_h = 16;

    fill_rect(badge_x, badge_y, badge_w, badge_h, ants::assets::ColorRGBA{0, 0, 0, 210});
    draw_rect(badge_x, badge_y, badge_w, badge_h, ants::assets::ColorRGBA{0, 255, 255, 230});

    std::string badge_text = "X: " + std::to_string(hover_tx) + "  Y: " + std::to_string(hover_ty);
    draw_text(badge_text, badge_x + 6, badge_y + 4, ants::assets::ColorRGBA{255, 255, 255, 255});

    // 4. Units left in each food object (debug grid only), at the top-right of the anchor tile
    for (const auto& fo : grid.food_objects()) {
        if (fo.remaining == 0) continue;
        int32_t fsx = 0, fsy = 0;
        camera_.world_to_screen(static_cast<int32_t>(fo.col) * TILE_SIZE, static_cast<int32_t>(fo.row) * TILE_SIZE, fsx, fsy);
        if (fsx < -64 || fsx > view.w + 64 || fsy < -64 || fsy > view.h + 64) continue;

        std::string food_badge = std::to_string(fo.remaining);
        int32_t f_bw = static_cast<int32_t>(food_badge.length()) * 8 + 8;
        int32_t f_bh = 14;
        int32_t f_bx = fsx + ts - f_bw;
        int32_t f_by = fsy + 2;

        fill_rect(f_bx, f_by, f_bw, f_bh, ants::assets::ColorRGBA{0, 0, 0, 200});
        draw_rect(f_bx, f_by, f_bw, f_bh, ants::assets::ColorRGBA{255, 215, 0, 255});
        draw_text(food_badge, f_bx + 4, f_by + 2, ants::assets::ColorRGBA{255, 255, 100, 255});
    }
}


void Renderer::end_frame() {
    if (renderer_) {
        if (!pending_screenshot_.empty()) {
            save_screenshot(pending_screenshot_);
            pending_screenshot_.clear();
        }
#ifdef ANTS_ENABLE_SDL_TTF
        ++text_frame_counter_;
        if (text_frame_counter_ % 180 == 0 && text_cache_.size() > 64) {
            for (auto it = text_cache_.begin(); it != text_cache_.end(); ) {
                if (text_frame_counter_ - it->second.last_frame > 180) {
                    if (it->second.texture) {
                        SDL_DestroyTexture(it->second.texture);
                    }
                    it = text_cache_.erase(it);
                } else {
                    ++it;
                }
            }
        }
#endif
        SDL_RenderPresent(renderer_);
    }
}

// ============================================================================
// IRenderer Interface Implementations
// ============================================================================

void Renderer::draw_sprite(uint32_t sprite_id, int32_t x, int32_t y, bool mirrored) {
    if (!renderer_ || !texture_cache_ || !archive_) return;
    SDL_Texture* tex = texture_cache_->get_sprite_texture(sprite_id, mirrored, hud_team_id_);
    if (!tex) return;

    const auto& sp = mirrored ? archive_->get_mirrored_sprite(sprite_id) : archive_->get_sprite(sprite_id);
    if (sprite_id == 2599 && y == 160) {
        // Sprite 2599 (butdef3a.bmp): 3 padding rows of HUD green background index 11.
        // Clip top 3 rows and align at y = 163 to perfectly match butdef1a/2a without vertical jump or flash.
        SDL_Rect src = { 0, 3, static_cast<int>(sp.width), static_cast<int>(sp.height) - 3 };
        const SDL_Rect dst = placed(x, 163, static_cast<int>(sp.width), static_cast<int>(sp.height) - 3);
        SDL_RenderCopy(renderer_, tex, &src, &dst);
        return;
    }
    const SDL_Rect dst = placed(x, y, static_cast<int>(sp.width), static_cast<int>(sp.height));
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
}

// A part of a sprite, scaled to a rectangle (IRenderer::draw_sprite_region): the frame of the match screen is drawn in parts, and a part that is one column or one row wide is
// repeated over the whole width or height of its rectangle. The scaling is SDL's with the "nearest" filter, so a repeated line is exactly that line.
void Renderer::draw_sprite_region(uint32_t sprite_id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh) {
    if (!renderer_ || !texture_cache_ || !archive_ || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    SDL_Texture* tex = texture_cache_->get_sprite_texture(sprite_id, false, hud_team_id_);
    if (!tex) return;
    const SDL_Rect src = {sx, sy, sw, sh};
    const SDL_Rect dst = placed(x, y, w, h);
    SDL_RenderCopy(renderer_, tex, &src, &dst);
}

void Renderer::draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* rgba) {
    if (!renderer_ || !rgba || w <= 0 || h <= 0) return;
    if (!rgba_texture_ || rgba_texture_w_ != w || rgba_texture_h_ != h) {
        if (rgba_texture_) SDL_DestroyTexture(rgba_texture_);
        rgba_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
        rgba_texture_w_ = w;
        rgba_texture_h_ = h;
    }
    if (!rgba_texture_) return;
    SDL_UpdateTexture(rgba_texture_, nullptr, rgba, w * 4);
    const SDL_Rect dst = placed(x, y, w, h);
    SDL_RenderCopy(renderer_, rgba_texture_, nullptr, &dst);
}

void Renderer::draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool mirrored) {
    if (!renderer_ || !texture_cache_ || !archive_) return;
    int32_t sid = archive_->find_sprite_id(name);
    if (sid < 0) sid = archive_->find_sprite_id(name + ".bmp");
    if (sid >= 0) {
        draw_sprite(static_cast<uint32_t>(sid), x, y, mirrored);
    }
}

void Renderer::fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) {
    if (!renderer_) return;
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    const SDL_Rect rect = placed(x, y, w, h);
    SDL_RenderFillRect(renderer_, &rect);
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    }
}

void Renderer::set_clip_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!renderer_) return;
    const SDL_Rect clip = placed(x, y, w, h);
    SDL_RenderSetClipRect(renderer_, &clip);
}

void Renderer::clear_clip_rect() {
    if (!renderer_) return;
    restore_clip();
}

void Renderer::draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) {
    if (!renderer_) return;
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    const SDL_Rect rect = placed(x, y, w, h);
    SDL_RenderDrawRect(renderer_, &rect);
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    }
}

void Renderer::draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color) {
    draw_text(text, x, y, color, FontSize::Px12);
}

size_t Renderer::font_index(FontSize size) noexcept {
    switch (size) {
        case FontSize::Px12: return 0;
        case FontSize::Px14: return 1;
        case FontSize::Px18: return 2;
        case FontSize::Px20: return 3;
        case FontSize::Px24: return 4;
        case FontSize::Px35: return 5;
    }
    return 0;
}

int32_t Renderer::ttf_point_size(int32_t cell_px, double cell_ratio) noexcept {
    // the glyphs are rendered at twice the size and halved into the canvas: a cell of `cell_px` pixels is 2 * cell_px pixels of font, and one em is
    // 1 / cell_ratio of the cell
    return std::max(2, static_cast<int32_t>(std::lround(2.0 * static_cast<double>(cell_px) / cell_ratio)));
}

void Renderer::draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color, FontSize size) {
    if (!renderer_ || text.empty()) return;

    // Handle multi-line strings: the lines are one cell height apart (what GetTextExtentPoint32A reports as cy in the original)
    if (text.find('\n') != std::string::npos) {
        int32_t cur_y = y;
        const int32_t line_height = font_cell_height(size);
        size_t start = 0;
        while (start < text.length()) {
            size_t next = text.find('\n', start);
            std::string line = (next == std::string::npos) ? text.substr(start) : text.substr(start, next - start);
            if (!line.empty()) {
                draw_text(line, x, cur_y, color, size);
            }
            cur_y += line_height;
            if (next == std::string::npos) break;
            start = next + 1;
        }
        return;
    }

#ifdef ANTS_ENABLE_SDL_TTF
    TTF_Font* font = font_for(size);

    if (font) {
        uint32_t c_u32 = (static_cast<uint32_t>(color.r) << 24) |
                         (static_cast<uint32_t>(color.g) << 16) |
                         (static_cast<uint32_t>(color.b) << 8)  |
                         static_cast<uint32_t>(color.a);
        TextCacheKey key{text, c_u32, static_cast<uint8_t>(size)};
        auto it = text_cache_.find(key);
        if (it != text_cache_.end()) {
            it->second.last_frame = text_frame_counter_;
            const SDL_Rect dst = placed(x, y, squeezed(it->second.width), it->second.height);
            SDL_RenderCopy(renderer_, it->second.texture, nullptr, &dst);
            return;
        }

        SDL_Color sc{color.r, color.g, color.b, color.a};
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), sc);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            if (tex) {
                SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
                // 2x ptsize mapped to logical canvas size:
                int32_t lw = (surf->w + 1) / 2;
                int32_t lh = (surf->h + 1) / 2;
                CachedTextEntry entry{tex, lw, lh, text_frame_counter_};
                text_cache_[key] = entry;
                const SDL_Rect dst = placed(x, y, squeezed(lw), lh);
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                SDL_FreeSurface(surf);
                return;
            }
            SDL_FreeSurface(surf);
        }
    }
#endif

    // Fallback: built-in 5x7 bitmap font, scaled up for the larger sizes
    const int32_t scale = std::max(1, font_cell_height(size) / 10);
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);

    int32_t cur_x = x;
    for (char c : text) {
        uint8_t glyph_idx = (c >= 32 && c <= 126) ? static_cast<uint8_t>(c - 32) : static_cast<uint8_t>('?' - 32);
        const auto& glyph = FONT_5X7[glyph_idx];

        for (int row = 0; row < 7; ++row) {
            uint8_t row_bits = glyph.rows[row];
            for (int col = 0; col < glyph.width; ++col) {
                if (row_bits & (0x80 >> col)) {
                    if (scale == 1) {
                        SDL_RenderDrawPoint(renderer_, cur_x + col + picture_.x + origin_.x, y + row + picture_.y + origin_.y);
                    } else {
                        const SDL_Rect dot = placed(cur_x + col * scale, y + row * scale, scale, scale);
                        SDL_RenderFillRect(renderer_, &dot);
                    }
                }
            }
        }
        cur_x += (glyph.width + 1) * scale;
    }
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    }
}

// The text squeezed horizontally to `max_width` when it is wider (the cached texture is the same: only the rectangle that it is copied into is narrower; the linear filter does the rest)
void Renderer::draw_text_squeezed(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color, FontSize size, int32_t max_width) {
    squeeze_width_ = max_width;
    draw_text(text, x, y, color, size);
    squeeze_width_ = 0;
}

int32_t Renderer::get_text_width(const std::string& text, FontSize size) const {
    if (text.empty()) return 0;
#ifdef ANTS_ENABLE_SDL_TTF
    TTF_Font* font = font_for(size);
    if (font) {
        int w = 0, h = 0;
        if (TTF_SizeUTF8(font, text.c_str(), &w, &h) == 0) {
            return (w + 1) / 2;
        }
    }
#endif
    return static_cast<int32_t>(text.size()) * 6 * std::max(1, font_cell_height(size) / 10);
}

int32_t Renderer::get_text_height(FontSize size) const {
    return font_cell_height(size);
}

// The original's health number: TextOutA with the stock SYSTEM_FIXED_FONT, the raster "Fixedsys": every character is a cell of 8 x 15 pixels, the
// glyphs are not antialiased. The Windows font itself is not available here, so the digits are drawn by hand in its style: 7 x 10 pixels with strokes of
// one pixel, top at row 2 of the cell (the font's ascent is 12).
static const char* const kFixedGlyphs[11][10] = {
    {"..XXX..", ".X...X.", "X.....X", "X.....X", "X.....X", "X.....X", "X.....X", "X.....X", ".X...X.", "..XXX.."},   // 0
    {"...X...", "..XX...", ".X.X...", "...X...", "...X...", "...X...", "...X...", "...X...", "...X...", ".XXXXX."},   // 1
    {".XXXXX.", "X.....X", "......X", "......X", ".....X.", "....X..", "...X...", "..X....", ".X.....", "XXXXXXX"},   // 2
    {".XXXXX.", "X.....X", "......X", "......X", "..XXXX.", "......X", "......X", "......X", "X.....X", ".XXXXX."},   // 3
    {"....XX.", "...X.X.", "..X..X.", ".X...X.", "X....X.", "XXXXXXX", ".....X.", ".....X.", ".....X.", ".....X."},   // 4
    {"XXXXXXX", "X......", "X......", "X......", "XXXXXX.", "......X", "......X", "......X", "X.....X", ".XXXXX."},   // 5
    {"..XXXX.", ".X.....", "X......", "X......", "XXXXXX.", "X.....X", "X.....X", "X.....X", "X.....X", ".XXXXX."},   // 6
    {"XXXXXXX", "......X", ".....X.", ".....X.", "....X..", "....X..", "...X...", "...X...", "..X....", "..X...."},   // 7
    {".XXXXX.", "X.....X", "X.....X", "X.....X", ".XXXXX.", "X.....X", "X.....X", "X.....X", "X.....X", ".XXXXX."},   // 8
    {".XXXXX.", "X.....X", "X.....X", "X.....X", "X.....X", ".XXXXXX", "......X", "......X", ".....X.", ".XXXX.."},   // 9
    {".......", ".......", ".......", ".......", ".......", ".XXXXX.", ".......", ".......", ".......", "......."},   // -
};

void Renderer::draw_fixed_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color) {
    if (!renderer_ || text.empty()) return;
    for (size_t g = 0; g < fixed_glyphs_.size(); ++g) {
        if (fixed_glyphs_[g] != nullptr) continue;
        std::vector<uint32_t> pixels(static_cast<size_t>(kFixedCellW * kFixedCellH), 0u);
        for (int32_t row = 0; row < 10; ++row) {
            for (int32_t col = 0; col < 7; ++col) {
                if (kFixedGlyphs[g][row][col] == 'X') pixels[static_cast<size_t>((row + 2) * kFixedCellW + col)] = 0xFFFFFFFFu;     // white, opaque
            }
        }
        SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, kFixedCellW, kFixedCellH);
        if (tex == nullptr) return;
        SDL_UpdateTexture(tex, nullptr, pixels.data(), kFixedCellW * static_cast<int32_t>(sizeof(uint32_t)));
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        fixed_glyphs_[g] = tex;
    }
    int32_t cur_x = x;
    for (char c : text) {
        const int32_t g = (c >= '0' && c <= '9') ? c - '0' : (c == '-' ? 10 : -1);
        if (g >= 0 && fixed_glyphs_[static_cast<size_t>(g)] != nullptr) {
            SDL_SetTextureColorMod(fixed_glyphs_[static_cast<size_t>(g)], color.r, color.g, color.b);
            SDL_SetTextureAlphaMod(fixed_glyphs_[static_cast<size_t>(g)], color.a);
            const SDL_Rect dst = placed(cur_x, y, kFixedCellW, kFixedCellH);
            SDL_RenderCopy(renderer_, fixed_glyphs_[static_cast<size_t>(g)], nullptr, &dst);
        }
        cur_x += kFixedCellW;                                  // a fixed font: every character takes a cell, drawn or not
    }
}

// The screenshot is the picture as the window shows it: the canvas scaled into the window by SDL's logical size (CanvasLayout::fit is that arithmetic), without the black bars
// around it. (It used to be the size of the whole window with the picture at its top left corner, which is right only when the window has the canvas's shape.)
bool Renderer::save_screenshot(const std::string& path) {
    if (!renderer_) return false;
    int out_w = 0, out_h = 0;
    SDL_GetRendererOutputSize(renderer_, &out_w, &out_h);
    if (out_w <= 0 || out_h <= 0) { out_w = canvas_w_; out_h = canvas_h_; }
    const CanvasFit fit = CanvasLayout{canvas_w_, canvas_h_}.fit(out_w, out_h);
    SDL_Rect area = { fit.viewport.x, fit.viewport.y, fit.viewport.w, fit.viewport.h };
    if (area.w <= 0 || area.h <= 0) return false;

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, area.w, area.h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return false;

    if (SDL_RenderReadPixels(renderer_, &area, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) != 0) {
        SDL_FreeSurface(surf);
        return false;
    }

    std::string bmp_path = path;
    bool convert_png = false;
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".png") {
        bmp_path = path.substr(0, path.size() - 4) + ".bmp";
        convert_png = true;
    }

    if (SDL_SaveBMP(surf, bmp_path.c_str()) != 0) {
        SDL_FreeSurface(surf);
        return false;
    }
    SDL_FreeSurface(surf);

    if (convert_png) {
        std::string cmd = "python3 -c \"from PIL import Image; Image.open('" + bmp_path + "').save('" + path + "')\" > /dev/null 2>&1 && rm -f \"" + bmp_path + "\"";
        int ret = system(cmd.c_str());
        (void)ret;
    }
    return true;
}

} // namespace ants::app
