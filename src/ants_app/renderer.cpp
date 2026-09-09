#include "ants_app/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstring>
#include <cstdio>

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

void ViewportCamera::pan(float dx, float dy, float dt, uint32_t map_w, uint32_t map_h) {
    x += dx * scroll_speed * dt;
    y += dy * scroll_speed * dt;
    clamp_to_bounds(map_w, map_h);
}

void ViewportCamera::center_on(int32_t world_px, int32_t world_py, uint32_t map_w, uint32_t map_h) {
    x = static_cast<float>(world_px - viewport_w / 2);
    y = static_cast<float>(world_py - viewport_h / 2);
    clamp_to_bounds(map_w, map_h);
}

void ViewportCamera::clamp_to_bounds(uint32_t map_w, uint32_t map_h) {
    float max_x = std::max(0.0f, static_cast<float>(static_cast<int32_t>(map_w * TILE_SIZE) - viewport_w));
    float max_y = std::max(0.0f, static_cast<float>(static_cast<int32_t>(map_h * TILE_SIZE) - viewport_h));
    x = std::clamp(x, 0.0f, max_x);
    y = std::clamp(y, 0.0f, max_y);
    world_x = static_cast<int32_t>(x);
    world_y = static_cast<int32_t>(y);
}

bool ViewportCamera::world_to_screen(int32_t wx, int32_t wy, int32_t& sx, int32_t& sy) const noexcept {
    sx = PLAYFIELD_X + (wx - static_cast<int32_t>(x));
    sy = PLAYFIELD_Y + (wy - static_cast<int32_t>(y));
    return (sx >= PLAYFIELD_X - TILE_SIZE && sx <= PLAYFIELD_X + PLAYFIELD_W &&
            sy >= PLAYFIELD_Y - TILE_SIZE && sy <= PLAYFIELD_Y + PLAYFIELD_H);
}

bool ViewportCamera::screen_to_world(int32_t sx, int32_t sy, int32_t& wx, int32_t& wy) const noexcept {
    if (sx < PLAYFIELD_X || sx >= PLAYFIELD_X + PLAYFIELD_W ||
        sy < PLAYFIELD_Y || sy >= PLAYFIELD_Y + PLAYFIELD_H) {
        return false;
    }
    wx = static_cast<int32_t>(x) + (sx - PLAYFIELD_X);
    wy = static_cast<int32_t>(y) + (sy - PLAYFIELD_Y);
    return true;
}

bool ViewportCamera::is_tile_visible(int32_t tx, int32_t ty) const noexcept {
    int32_t px = tx * TILE_SIZE;
    int32_t py = ty * TILE_SIZE;
    return (px + TILE_SIZE >= static_cast<int32_t>(x) && px <= static_cast<int32_t>(x) + PLAYFIELD_W &&
            py + TILE_SIZE >= static_cast<int32_t>(y) && py <= static_cast<int32_t>(y) + PLAYFIELD_H);
}

// ============================================================================
// TextureCache Implementation
// ============================================================================

TextureCache::TextureCache(SDL_Renderer* renderer, const ants::assets::AssetArchive& archive)
    : renderer_(renderer), archive_(archive) {
    for (size_t i = 0; i < archive_.sprite_count(); ++i) {
        const auto& name = archive_.get_sprite(static_cast<uint32_t>(i)).name;
        if (name == "2bomb.bmp") base_bomb_sprite_id_ = static_cast<int32_t>(i);
        else if (name == "2bombgrn.bmp") team_bomb_sprite_ids_[0] = static_cast<int32_t>(i);
        else if (name == "2bombred.bmp") team_bomb_sprite_ids_[1] = static_cast<int32_t>(i);
        else if (name == "2bombblu.bmp") team_bomb_sprite_ids_[2] = static_cast<int32_t>(i);
        else if (name == "2bombblk.bmp") team_bomb_sprite_ids_[3] = static_cast<int32_t>(i);
    }
}

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

bool TextureCache::is_base_bomb_sprite(uint32_t sprite_id) const noexcept {
    return base_bomb_sprite_id_ >= 0 && sprite_id == static_cast<uint32_t>(base_bomb_sprite_id_);
}

uint32_t TextureCache::get_team_bomb_sprite_index(uint8_t team_id) const noexcept {
    if (team_id < 4 && team_bomb_sprite_ids_[team_id] >= 0) {
        return static_cast<uint32_t>(team_bomb_sprite_ids_[team_id]);
    }
    return base_bomb_sprite_id_ >= 0 ? static_cast<uint32_t>(base_bomb_sprite_id_) : 0;
}

SDL_Texture* TextureCache::get_sprite_texture(uint32_t sprite_id, bool mirrored, uint8_t team_id) {
    uint32_t eff_sprite_id = sprite_id;
    if (team_id < 4 && is_base_bomb_sprite(sprite_id)) {
        eff_sprite_id = get_team_bomb_sprite_index(team_id);
    }
    if (!renderer_ || eff_sprite_id >= archive_.sprite_count()) return nullptr;

    uint64_t key = (static_cast<uint64_t>(eff_sprite_id) << 4) |
                   (static_cast<uint64_t>(team_id & 0x07) << 1) |
                   (mirrored ? 1 : 0);
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        return it->second;
    }

    const auto& sp = mirrored ? archive_.get_mirrored_sprite(eff_sprite_id)
                              : archive_.get_sprite(eff_sprite_id);
    if (sp.width == 0 || sp.height == 0) return nullptr;

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

    // Convert 8-bit paletted sprite to 32-bit RGBA
    auto pal = archive_.get_palette();
    if (team_id < 4) {
        // Authentic Ants sprite palette remap:
        // Team 0 (Green): indices 80..99 -> base 140..159 (+60 shift)
        // Team 1 (Red):   indices 80..99 -> base 120..139 (+40 shift)
        // Team 2 (Blue):  indices 80..99 -> base 100..119 (+20 shift)
        // Team 3 (Black): indices 80..99 -> base 80..99 (base black palette)
        size_t offset = 0;
        if (team_id == 0) offset = 60;
        else if (team_id == 1) offset = 40;
        else if (team_id == 2) offset = 20;
        else if (team_id == 3) offset = 0;

        if (offset > 0) {
            const auto& base_pal = archive_.get_palette();
            for (size_t i = 80; i <= 99; ++i) {
                pal[i] = base_pal[i + offset];
            }
        }
    }

    // Authentic HUD palette remap for indices 1..31 (Ants.exe 0x100EA70):
    if (team_id == 0) {
        for (size_t i = 1; i <= 31; ++i) pal[i] = AUTHENTIC_GREEN_HUD[i - 1];
    } else if (team_id == 1) {
        for (size_t i = 1; i <= 31; ++i) pal[i] = AUTHENTIC_RED_HUD[i - 1];
    } else if (team_id == 2) {
        for (size_t i = 1; i <= 31; ++i) pal[i] = AUTHENTIC_BLUE_HUD[i - 1];
    } else if (team_id == 3) {
        for (size_t i = 1; i <= 31; ++i) pal[i] = AUTHENTIC_BLACK_HUD[i - 1];
    }

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

SDL_Texture* TextureCache::get_named_sprite_texture(const std::string& name, bool mirrored, uint8_t team_id) {
    int32_t sid = archive_.find_sprite_id(name);
    if (sid < 0) {
        sid = archive_.find_sprite_id(name + ".bmp");
    }
    if (sid >= 0) {
        return get_sprite_texture(static_cast<uint32_t>(sid), mirrored, team_id);
    }
    return nullptr;
}

// ============================================================================
// Renderer Implementation
// ============================================================================

Renderer::Renderer() = default;

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(SDL_Window* window,
                    const ants::assets::AssetArchive& archive,
                    bool integer_scale) {
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

    // Nearest neighbor scaling ensures retro pixel art stays sharp and crisp when scaled
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    integer_scale_ = integer_scale;
    SDL_RenderSetLogicalSize(renderer_, CANVAS_WIDTH, CANVAS_HEIGHT);
    SDL_RenderSetIntegerScale(renderer_, (integer_scale_ && !is_fullscreen_) ? SDL_TRUE : SDL_FALSE);

    texture_cache_ = std::make_unique<TextureCache>(renderer_, archive);

#ifdef ANTS_ENABLE_SDL_TTF
    if (TTF_Init() == 0) {
        ttf_initialized_ = true;
        const std::vector<std::string> font_candidates = {
            "Original-Ants/Franklin Gothic Medium.ttf",
            "Original-Ants/framd.ttf",
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Supplemental/Trebuchet MS.ttf",
            "/System/Library/Fonts/Geneva.ttf",
            "/Library/Fonts/Arial.ttf",
            "C:\\Windows\\Fonts\\framd.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf"
        };
        for (const auto& path : font_candidates) {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (f) {
                std::fclose(f);
                font_small_ = TTF_OpenFont(path.c_str(), 18);
                font_medium_ = TTF_OpenFont(path.c_str(), 22);
                font_large_ = TTF_OpenFont(path.c_str(), 26);
                if (font_small_) {
                    std::cout << "[Renderer] High-quality TrueType font loaded: " << path << std::endl;
                    break;
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
    if (font_small_) { TTF_CloseFont(font_small_); font_small_ = nullptr; }
    if (font_medium_) { TTF_CloseFont(font_medium_); font_medium_ = nullptr; }
    if (font_large_) { TTF_CloseFont(font_large_); font_large_ = nullptr; }
    if (ttf_initialized_) {
        TTF_Quit();
        ttf_initialized_ = false;
    }
#endif

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

void Renderer::set_fullscreen(bool fullscreen) {
    is_fullscreen_ = fullscreen;
    if (!renderer_) return;
    SDL_RenderSetLogicalSize(renderer_, CANVAS_WIDTH, CANVAS_HEIGHT);
    if (is_fullscreen_) {
        // Fullscreen: fit vertically to monitor height, preserve 4:3 aspect ratio with pillarboxing (no horizontal stretching)
        SDL_RenderSetIntegerScale(renderer_, SDL_FALSE);
    } else {
        if (integer_scale_) {
            SDL_RenderSetIntegerScale(renderer_, SDL_TRUE);
        } else {
            SDL_RenderSetIntegerScale(renderer_, SDL_FALSE);
        }
    }
}

void Renderer::set_level(const ants::assets::LevelData& level) {
    map_width_ = level.width;
    map_height_ = level.height;
    camera_.clamp_to_bounds(map_width_, map_height_);

    // Pre-resolve tile dictionary strings to sprite IDs and Table 4 (dx, dy) anchor offsets
    tile_sprite_ids_.assign(level.tile_dictionary.size(), -1);
    tile_offsets_.assign(level.tile_dictionary.size(), {0, 0});
    tile_anim_frames_.assign(level.tile_dictionary.size(), {});

    for (size_t i = 0; i < level.tile_dictionary.size(); ++i) {
        const std::string& name = level.tile_dictionary[i];
        if (name.empty() || name == ".") continue;

        // Suppress start marker tiles (editor markers with 'A')
        if (name == "BSTART" || name == "USTART" || name == "RSTART" || name == "GSTART" ||
            name == "bstart" || name == "ustart" || name == "rstart" || name == "gstart") {
            tile_sprite_ids_[i] = -1;
            continue;
        }

        // Map anthills to authentic 128x128 sprites
        if (name == "BLACKHILL" || name == "blackhill") {
            tile_sprite_ids_[i] = archive_->find_sprite_id("bkhill.bmp");
            continue;
        }
        if (name == "BLUEHILL" || name == "bluehill") {
            tile_sprite_ids_[i] = archive_->find_sprite_id("blhill.bmp");
            continue;
        }
        if (name == "REDHILL" || name == "redhill") {
            tile_sprite_ids_[i] = archive_->find_sprite_id("rhill.bmp");
            continue;
        }
        if (name == "GREENHILL" || name == "greenhill") {
            tile_sprite_ids_[i] = archive_->find_sprite_id("ghill.bmp");
            continue;
        }

        std::string lower_name = name;
        for (char& ch : lower_name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        int32_t sid = -1;
        int32_t off_x = 0, off_y = 0;

        // 1. Table 4 animation resolution (handles grass1..4, grassmed1..4, rocks, food, center items)
        const auto* anim = archive_->find_animation(name);
        if (!anim) anim = archive_->find_animation(lower_name);
        if (anim && !anim->subitems.empty() && !anim->subitems[0].frames.empty()) {
            sid = static_cast<int32_t>(anim->subitems[0].frames[0].sprite_index);
            off_x = anim->subitems[0].frames[0].dx;
            off_y = anim->subitems[0].frames[0].dy;
        }

        // 2. Direct or .bmp lookup
        if (sid < 0) sid = archive_->find_sprite_id(name);
        if (sid < 0) sid = archive_->find_sprite_id(name + ".bmp");

        // 3. Numerical variant resolution (e.g. M01b -> m01b2.bmp)
        if (sid < 0) sid = archive_->find_sprite_id(lower_name + "2.bmp");
        if (sid < 0) sid = archive_->find_sprite_id(lower_name + "2");

        // 4. Strip suffix after underscore (e.g. m01d_a -> M01d2.bmp)
        if (sid < 0 && lower_name.find('_') != std::string::npos) {
            std::string prefix = lower_name.substr(0, lower_name.find('_'));
            sid = archive_->find_sprite_id(prefix);
            if (sid < 0) sid = archive_->find_sprite_id(prefix + ".bmp");
            if (sid < 0) sid = archive_->find_sprite_id(prefix + "2.bmp");
            if (sid < 0) sid = archive_->find_sprite_id(prefix + "2");
        }

        // 5. Base category fallback for mud, grass, water
        if (sid < 0) {
            if (lower_name.rfind("m01", 0) == 0 || lower_name.rfind("mw", 0) == 0 || lower_name.rfind("wm", 0) == 0) {
                sid = archive_->find_sprite_id("M01a.bmp");
            } else if (lower_name.rfind("g01", 0) == 0 || lower_name.rfind("gm", 0) == 0 || lower_name.rfind("mg", 0) == 0) {
                sid = archive_->find_sprite_id("g01a.bmp");
            } else if (lower_name.rfind("w01", 0) == 0) {
                sid = archive_->find_sprite_id("w01a.bmp");
            }
        }
        tile_sprite_ids_[i] = sid;
        tile_offsets_[i] = {off_x, off_y};

        // 6. Resolve animated frame sequences for water and mud
        if (sid >= 0) {
            std::vector<int32_t> frames;
            frames.push_back(sid);
            if (lower_name.rfind("w01", 0) == 0 || lower_name.rfind("wm", 0) == 0 || lower_name.rfind("mw", 0) == 0) {
                // Water tiles cycle 4 animation frames (e.g. w01a, w01a2, w01a3, w01a4)
                std::string base = (lower_name.find('.') != std::string::npos) ? lower_name.substr(0, lower_name.find('.')) : lower_name;
                for (int f = 2; f <= 4; ++f) {
                    int32_t fsid = archive_->find_sprite_id(base + std::to_string(f) + ".bmp");
                    if (fsid >= 0) frames.push_back(fsid);
                }
            } else if (lower_name.rfind("m01b", 0) == 0 || lower_name.rfind("m01c", 0) == 0) {
                // Mud bubbling sequence (2, 4, 6, 8, 10)
                std::string base = lower_name.substr(0, 4);
                static const int seq[] = { 2, 4, 6, 8, 10 };
                for (int num : seq) {
                    int32_t fsid = archive_->find_sprite_id(base + std::to_string(num) + ".bmp");
                    if (fsid >= 0 && fsid != sid) frames.push_back(fsid);
                }
            } else if (lower_name.rfind("m01d", 0) == 0) {
                // Mud bubbling sequence (2, 4, 6, 8, 10, 11, 12, 13)
                static const int seq[] = { 2, 4, 6, 8, 10, 11, 12, 13 };
                for (int num : seq) {
                    int32_t fsid = archive_->find_sprite_id("m01d" + std::to_string(num) + ".bmp");
                    if (fsid >= 0 && fsid != sid) frames.push_back(fsid);
                }
            }
            if (frames.size() > 1) {
                tile_anim_frames_[i] = std::move(frames);
            }
        }
    }

    // Identify 4x4 Anthill base bounding origins from Layer 2
    for (size_t t = 0; t < 4; ++t) {
        anthill_bases_[t] = { -1, -1 };
    }
    has_anthill_bases_ = false;

    for (uint32_t y = 0; y < level.height; ++y) {
        for (uint32_t x = 0; x < level.width; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            if (c2.tile_index < level.tile_dictionary.size()) {
                const std::string& tname = level.tile_dictionary[c2.tile_index];
                int team = -1;
                if (tname == "GREENHILL" || tname == "greenhill") team = 0;
                else if (tname == "REDHILL" || tname == "redhill") team = 1;
                else if (tname == "BLUEHILL" || tname == "bluehill") team = 2;
                else if (tname == "BLACKHILL" || tname == "blackhill") team = 3;

                if (team >= 0) {
                    size_t st = static_cast<size_t>(team);
                    if (anthill_bases_[st].x < 0 || static_cast<int32_t>(x) < anthill_bases_[st].x) {
                        anthill_bases_[st].x = static_cast<int32_t>(x);
                    }
                    if (anthill_bases_[st].y < 0 || static_cast<int32_t>(y) < anthill_bases_[st].y) {
                        anthill_bases_[st].y = static_cast<int32_t>(y);
                    }
                    has_anthill_bases_ = true;
                }
            }
        }
    }

    // Instantiate all Layer 2 objects via authentic anchor flags ((flags & 1) != 0)
    static_decor_objects_.clear();
    for (uint32_t y = 0; y < level.height; ++y) {
        for (uint32_t x = 0; x < level.width; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            if (c2.tile_index >= level.tile_dictionary.size()) continue;
            if (c2.is_empty() || c2.tile_index == 0xFFFF || c2.tile_index == 0x7FFE) continue;

            const std::string& tname = level.tile_dictionary[c2.tile_index];
            if (tname.empty() || tname == ".") continue;

            std::string low = tname;
            for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (low.find("hill") != std::string::npos || low.find("start") != std::string::npos) {
                continue;
            }
            if (low.rfind("pu_", 0) == 0 || low.rfind("pu", 0) == 0) {
                continue; // Dynamic powerups on ground: rendered exclusively via cell.has_powerup()
            }

            // Only anchor tiles ((flags & 1) != 0) define object instances
            if ((c2.flags & 1) == 0) continue;

            int32_t sid = (c2.tile_index < tile_sprite_ids_.size()) ? tile_sprite_ids_[c2.tile_index] : -1;
            if (sid < 0) continue;
            const auto& sp = archive_->get_sprite(static_cast<uint32_t>(sid));
            if (sp.width == 0 || sp.height == 0) continue;
            if (sp.width == 128 && sp.height == 128) continue; // Anthills handled separately

            int32_t off_x = (c2.tile_index < tile_offsets_.size()) ? tile_offsets_[c2.tile_index].first : 0;
            int32_t off_y = (c2.tile_index < tile_offsets_.size()) ? tile_offsets_[c2.tile_index].second : 0;

            StaticMapObject obj{};
            obj.world_x = static_cast<int32_t>(x * TILE_SIZE) + off_x;
            obj.world_y = static_cast<int32_t>(y * TILE_SIZE) + off_y;
            obj.sprite_id = sid;
            obj.width = static_cast<int32_t>(sp.width);
            obj.height = static_cast<int32_t>(sp.height);
            obj.anchor_x = static_cast<uint16_t>(x);
            obj.anchor_y = static_cast<uint16_t>(y);

            bool is_food = (low.rfind("fd", 0) == 0 || low.rfind("food", 0) == 0);
            obj.is_food = is_food;
            if (is_food) {
                // Find local cells belonging to this food clump (within radius 4 of anchor)
                int32_t min_x = std::max(0, static_cast<int32_t>(x) - 4);
                int32_t max_x = std::min(static_cast<int32_t>(level.width) - 1, static_cast<int32_t>(x) + 4);
                int32_t min_y = std::max(0, static_cast<int32_t>(y) - 4);
                int32_t max_y = std::min(static_cast<int32_t>(level.height) - 1, static_cast<int32_t>(y) + 4);
                for (int32_t fy = min_y; fy <= max_y; ++fy) {
                    for (int32_t fx = min_x; fx <= max_x; ++fx) {
                        const auto& fc = level.get_cell_layer2(static_cast<uint32_t>(fx), static_cast<uint32_t>(fy));
                        if (fc.tile_index == c2.tile_index && (c2.properties == 0 || fc.properties == c2.properties)) {
                            obj.food_tiles.push_back({static_cast<uint16_t>(fx), static_cast<uint16_t>(fy)});
                        }
                    }
                }
                if (obj.food_tiles.empty()) {
                    obj.food_tiles.push_back({static_cast<uint16_t>(x), static_cast<uint16_t>(y)});
                }
            }

            static_decor_objects_.push_back(std::move(obj));
        }
    }

    // 3rd Layer Canopy Decor from LevelData Block 1 (items with team_id == 255: clovers, flowers, tree tops, etc.)
    layer3_canopy_objects_.clear();
    for (const auto& sp_item : level.anthill_spawns) {
        if (sp_item.team_id == 255 && sp_item.tile_id < level.tile_dictionary.size()) {
            const std::string& tname = level.tile_dictionary[sp_item.tile_id];
            const auto* anim = archive_->find_animation(tname);
            if (!anim) {
                std::string low = tname;
                for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                anim = archive_->find_animation(low);
            }
            if (anim && !anim->subitems.empty() && !anim->subitems[0].frames.empty()) {
                for (const auto& fr : anim->subitems[0].frames) {
                    const auto& sp = archive_->get_sprite(fr.sprite_index);
                    if (sp.width > 0 && sp.height > 0) {
                        StaticMapObject obj{};
                        obj.world_x = static_cast<int32_t>(sp_item.x * TILE_SIZE + TILE_SIZE / 2) + fr.dx;
                        obj.world_y = static_cast<int32_t>(sp_item.y * TILE_SIZE + TILE_SIZE / 2) + fr.dy;
                        obj.sprite_id = static_cast<int32_t>(fr.sprite_index);
                        obj.width = static_cast<int32_t>(sp.width);
                        obj.height = static_cast<int32_t>(sp.height);
                        obj.is_food = false;
                        layer3_canopy_objects_.push_back(std::move(obj));
                    }
                }
            } else {
                int32_t sid = (sp_item.tile_id < tile_sprite_ids_.size()) ? tile_sprite_ids_[sp_item.tile_id] : -1;
                if (sid >= 0) {
                    const auto& sp = archive_->get_sprite(static_cast<uint32_t>(sid));
                    if (sp.width > 0 && sp.height > 0) {
                        int32_t off_x = (sp_item.tile_id < tile_offsets_.size()) ? tile_offsets_[sp_item.tile_id].first : 0;
                        int32_t off_y = (sp_item.tile_id < tile_offsets_.size()) ? tile_offsets_[sp_item.tile_id].second : 0;

                        StaticMapObject obj{};
                        obj.world_x = static_cast<int32_t>(sp_item.x * TILE_SIZE + TILE_SIZE / 2) + off_x;
                        obj.world_y = static_cast<int32_t>(sp_item.y * TILE_SIZE + TILE_SIZE / 2) + off_y;
                        obj.sprite_id = sid;
                        obj.width = static_cast<int32_t>(sp.width);
                        obj.height = static_cast<int32_t>(sp.height);
                        obj.is_food = false;
                        layer3_canopy_objects_.push_back(std::move(obj));
                    }
                }
            }
        }
    }
}

void Renderer::begin_frame() {
    if (!renderer_) return;
    anim_tick_++;
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255); // Black letterbox / background
    SDL_RenderClear(renderer_);
}

void Renderer::render_world(const ants::sim::WorldState& world,
                            const ants::sim::Grid& grid,
                            int32_t selected_unit_id,
                            const std::vector<uint32_t>& selected_unit_ids,
                            bool show_all_health_bars,
                            bool show_tile_grid,
                            int32_t mouse_x,
                            int32_t mouse_y,
                            int32_t selected_base_team_id) {
    if (!renderer_) return;
    render_queue_.clear();

    // 1. Clip exclusively to playfield
    SDL_Rect clip_rect = { PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H };
    SDL_RenderSetClipRect(renderer_, &clip_rect);

    // 2. Layer 1 Terrain
    render_terrain_layer1(grid);

    // 3. Layer 2 Structures / Interactive Objects
    render_terrain_layer2_structures(grid);

    // 3.5 Anthill Selection Brackets (if a base is selected)
    if (selected_base_team_id >= 0) {
        int32_t base_sx = -1000, base_sy = -1000;
        if (has_anthill_bases_ && selected_base_team_id < 4) {
            size_t b_idx = static_cast<size_t>(selected_base_team_id);
            if (anthill_bases_[b_idx].x >= 0 && anthill_bases_[b_idx].y >= 0) {
                camera_.world_to_screen(anthill_bases_[b_idx].x * TILE_SIZE,
                                        anthill_bases_[b_idx].y * TILE_SIZE, base_sx, base_sy);
            }
        } else {
            for (const auto& a : grid.anthills()) {
                if (static_cast<int32_t>(a.team_id) == selected_base_team_id) {
                    camera_.world_to_screen((static_cast<int32_t>(a.x) - 1) * TILE_SIZE,
                                            (static_cast<int32_t>(a.y) - 1) * TILE_SIZE, base_sx, base_sy);
                    break;
                }
            }
        }
        if (base_sx >= -128 && base_sx <= PLAYFIELD_W + 128 && base_sy >= -128 && base_sy <= PLAYFIELD_H + 128) {
            draw_anthill_selection_brackets(base_sx, base_sy, 128, 128);
        }
    }

    // 4. Ant Units (Depth-Sorted)
    render_ant_units(world, selected_unit_id, selected_unit_ids, show_all_health_bars);

    // 4.2 Visual Effects (e.g. bomb explosion bombex)
    render_visual_effects(world);

    // 4.5 Layer 3 Canopy Overhang (rendered after ants so ants walk beneath foliage)
    render_terrain_layer3_canopy();

    // 5. Tile Grid Overlay (if enabled)
    if (show_tile_grid) {
        render_tile_grid(grid, mouse_x, mouse_y);
    }

    // 6. Unset clipping for full-canvas chrome
    SDL_RenderSetClipRect(renderer_, nullptr);
}

void Renderer::render_terrain_layer1(const ants::sim::Grid& grid) {
    int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE);
    int32_t end_col   = std::min(static_cast<int32_t>(grid.width()) - 1,
                                 (static_cast<int32_t>(camera_.x) + PLAYFIELD_W + 31) / TILE_SIZE);
    int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE);
    int32_t end_row   = std::min(static_cast<int32_t>(grid.height()) - 1,
                                 (static_cast<int32_t>(camera_.y) + PLAYFIELD_H + 31) / TILE_SIZE);

    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            const auto& cell = grid.get_cell(static_cast<uint32_t>(c), static_cast<uint32_t>(r));
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(c * TILE_SIZE, r * TILE_SIZE, sx, sy);

            SDL_Rect dst = { sx, sy, TILE_SIZE, TILE_SIZE };
            int32_t sid = (cell.terrain_id < tile_sprite_ids_.size()) ? tile_sprite_ids_[cell.terrain_id] : -1;
            if (cell.terrain_id < tile_anim_frames_.size() && !tile_anim_frames_[cell.terrain_id].empty()) {
                const auto& frames = tile_anim_frames_[cell.terrain_id];
                if (cell.is_mud) {
                    // Mud bubbles: idle on flat mud (frames[0]) for ~96 ticks (~1.6s @ 60Hz), then pop over 24 ticks (400ms)
                    uint32_t phase = (static_cast<uint32_t>(c) * 17 + static_cast<uint32_t>(r) * 31);
                    uint32_t local_tick = (anim_tick_ + phase) % 120;
                    if (local_tick < 96 || frames.size() <= 1) {
                        sid = frames[0];
                    } else {
                        size_t pop_idx = (local_tick - 96) * (frames.size() - 1) / 24;
                        sid = frames[1 + std::min(pop_idx, frames.size() - 2)];
                    }
                } else {
                    // Water ripples: cycle every 9 frames (150ms)
                    size_t frame_idx = (anim_tick_ / 9) % frames.size();
                    sid = frames[frame_idx];
                }
            }

            if (sid >= 0) {
                SDL_Texture* tex = texture_cache_->get_sprite_texture(static_cast<uint32_t>(sid));
                if (tex) {
                    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                    continue;
                }
            }

            // Fallback colors for terrain category
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

void Renderer::render_terrain_layer2_structures(const ants::sim::Grid& grid) {
    int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE);
    int32_t end_col   = std::min(static_cast<int32_t>(grid.width()) - 1,
                                 (static_cast<int32_t>(camera_.x) + PLAYFIELD_W + 31) / TILE_SIZE);
    int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE);
    int32_t end_row   = std::min(static_cast<int32_t>(grid.height()) - 1,
                                 (static_cast<int32_t>(camera_.y) + PLAYFIELD_H + 31) / TILE_SIZE);

    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            const auto& cell = grid.get_cell(static_cast<uint32_t>(c), static_cast<uint32_t>(r));
            if (cell.is_empty_overlay()) continue;

            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(c * TILE_SIZE, r * TILE_SIZE, sx, sy);
            SDL_Rect dst = { sx, sy, TILE_SIZE, TILE_SIZE };

            // 1. Bridges (Stages 1..4 and 4b, authentic Table 4 bounding boxes and sprite dimensions)
            if ((cell.interactive_id >= ants::sim::TILE_BRIDGE1 &&
                 cell.interactive_id <= ants::sim::TILE_BRIDGE4) ||
                cell.interactive_id == ants::sim::TILE_BRIDGE4B) {
                const char* bname = "bridge4a.bmp";
                int dx = 0, dy = 0, bw = 32, bh = 32;
                switch (cell.interactive_id) {
                    case ants::sim::TILE_BRIDGE1:
                        bname = "bridge1.bmp"; dx = 8; dy = 9; bw = 16; bh = 16; break;
                    case ants::sim::TILE_BRIDGE2:
                        bname = "bridge2.bmp"; dx = 7; dy = 7; bw = 19; bh = 17; break;
                    case ants::sim::TILE_BRIDGE3:
                        bname = "bridge3.bmp"; dx = 3; dy = 5; bw = 27; bh = 24; break;
                    case ants::sim::TILE_BRIDGE4:
                        bname = "bridge4a.bmp"; dx = 0; dy = 0; bw = 32; bh = 32; break;
                    case ants::sim::TILE_BRIDGE4B:
                        bname = "bridge4b.bmp"; dx = 2; dy = 1; bw = 29; bh = 30; break;
                    default: break;
                }
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture(bname);
                if (tex) {
                    SDL_Rect bridge_dst = { sx + dx, sy + dy, bw, bh };
                    SDL_RenderCopy(renderer_, tex, nullptr, &bridge_dst);
                }
                continue;
            }

            // 2. Fire Walls (Table 4 Anim 134 wallup04: 5 frames at 120ms each)
            if (cell.has_fire()) {
                static const struct {
                    const char* name;
                    int dx;
                    int dy;
                    int w;
                    int h;
                } fire_frames[5] = {
                    { "9fire01.bmp", 1, 0, 29, 33 },
                    { "9fire02.bmp", 1, 2, 30, 31 },
                    { "9fire03.bmp", 0, 1, 30, 32 },
                    { "9fire04.bmp", 1, 2, 30, 31 },
                    { "9fire05.bmp", 0, 1, 31, 32 }
                };

                uint32_t phase = static_cast<uint32_t>(c) * 2u + static_cast<uint32_t>(r) * 3u;
                size_t frame_idx = ((anim_tick_ / 7u) + phase) % 5u;
                const auto& ff = fire_frames[frame_idx];
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture(ff.name);
                if (tex) {
                    SDL_Rect fire_dst = { sx + ff.dx, sy + ff.dy, ff.w, ff.h };
                    SDL_RenderCopy(renderer_, tex, nullptr, &fire_dst);
                }
                continue;
            }

            // 3. Bombs (Table 4 Anim 129..132: 2 frames at 100ms each, team 0=Green, 1=Red, 2=Blue, 3=Black)
            if (cell.has_bomb()) {
                static const char* const bomb_frames[4][2] = {
                    { "1bombgrn.bmp", "2bombgrn.bmp" },
                    { "1bombred.bmp", "2bombred.bmp" },
                    { "1bombblu.bmp", "2bombblu.bmp" },
                    { "1bombblk.bmp", "2bombblk.bmp" }
                };
                uint8_t owner = cell.interactive_owner % 4u;
                uint32_t phase = static_cast<uint32_t>(c) * 3u + static_cast<uint32_t>(r) * 5u;
                size_t b_frame = ((anim_tick_ / 6u) + phase) % 2u;
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture(bomb_frames[owner][b_frame]);
                if (tex) {
                    SDL_Rect bomb_dst = { sx + 10, sy + 4, 12, 24 };
                    SDL_RenderCopy(renderer_, tex, nullptr, &bomb_dst);
                }
                continue;
            }

            // 4. Dropped Lunchbox
            if (cell.has_lunchbox()) {
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture("lunchicon.bmp");
                if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                continue;
            }

            // 5. Power-up Potions on Ground
            if (cell.has_powerup()) {
                const char* pu_name = nullptr;
                switch (cell.powerup_type) {
                    case 1: pu_name = "pubomb.bmp"; break;
                    case 2: pu_name = "pufire.bmp"; break;
                    case 3: pu_name = "puthief01.bmp"; break;
                    case 4: pu_name = "pucomb.bmp"; break;
                    case 5: pu_name = "puswim01.bmp"; break;
                    default: break;
                }
                if (pu_name) {
                    SDL_Texture* tex = texture_cache_->get_named_sprite_texture(pu_name);
                    if (tex) {
                        int pw = 24, ph = 24;
                        SDL_QueryTexture(tex, nullptr, nullptr, &pw, &ph);
                        SDL_Rect pu_dst = { sx + (32 - pw) / 2, sy + (32 - ph) / 2, pw, ph };
                        SDL_RenderCopy(renderer_, tex, nullptr, &pu_dst);
                        continue;
                    }
                }
            }
        }
    }

    // 5. Static Decorative Overlays & Multi-Tile Food Objects (Rendered once per unique anchor instance)
    for (const auto& obj : static_decor_objects_) {
        int32_t active_sid = obj.sprite_id;
        int32_t obj_x = obj.world_x;
        int32_t obj_y = obj.world_y;
        int32_t obj_w = obj.width;
        int32_t obj_h = obj.height;

        // If it is food, check if any of its footprint cells still has food
        if (obj.is_food) {
            bool has_any_food = false;
            uint16_t cur_tile = ants::assets::LVL_EMPTY_TILE;
            for (const auto& tile : obj.food_tiles) {
                if (grid.in_bounds(tile.first, tile.second)) {
                    const auto& cell = grid.get_cell(tile.first, tile.second);
                    if (cell.has_food()) {
                        has_any_food = true;
                        cur_tile = cell.interactive_id;
                        break;
                    }
                }
            }
            if (!has_any_food) continue; // All food in this item has been gathered

            // If the food stage changed, dynamically update active sprite and offsets
            if (cur_tile != ants::assets::LVL_EMPTY_TILE && cur_tile < tile_sprite_ids_.size()) {
                int32_t sid = tile_sprite_ids_[cur_tile];
                if (sid >= 0) {
                    active_sid = sid;
                    const auto& sp = archive_->get_sprite(static_cast<uint32_t>(sid));
                    int32_t off_x = (cur_tile < tile_offsets_.size()) ? tile_offsets_[cur_tile].first : 0;
                    int32_t off_y = (cur_tile < tile_offsets_.size()) ? tile_offsets_[cur_tile].second : 0;
                    obj_x = static_cast<int32_t>(obj.anchor_x * TILE_SIZE) + off_x;
                    obj_y = static_cast<int32_t>(obj.anchor_y * TILE_SIZE) + off_y;
                    obj_w = static_cast<int32_t>(sp.width);
                    obj_h = static_cast<int32_t>(sp.height);
                }
            }
        }

        int32_t right = obj_x + obj_w;
        int32_t bottom = obj_y + obj_h;
        if (right < camera_.world_x || obj_x > camera_.world_x + camera_.viewport_w ||
            bottom < camera_.world_y || obj_y > camera_.world_y + camera_.viewport_h) {
            continue;
        }

        int32_t sx = PLAYFIELD_X + (obj_x - camera_.world_x);
        int32_t sy = PLAYFIELD_Y + (obj_y - camera_.world_y);
        SDL_Texture* tex = texture_cache_->get_sprite_texture(static_cast<uint32_t>(active_sid));
        if (tex) {
            SDL_Rect decor_dst = { sx, sy, obj_w, obj_h };
            SDL_RenderCopy(renderer_, tex, nullptr, &decor_dst);
        }
    }

    // Anthill Bases (Authentic 128x128 4x4 bases: ghill, rhill, blhill, bkhill)
    static const char* hill_sprites[4] = { "ghill.bmp", "rhill.bmp", "blhill.bmp", "bkhill.bmp" };
    if (has_anthill_bases_) {
        for (size_t t = 0; t < 4; ++t) {
            if (anthill_bases_[t].x >= 0 && anthill_bases_[t].y >= 0) {
                int32_t sx = 0, sy = 0;
                camera_.world_to_screen(anthill_bases_[t].x * TILE_SIZE, anthill_bases_[t].y * TILE_SIZE, sx, sy);
                SDL_Rect dst = { sx, sy, 128, 128 };
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture(hill_sprites[t]);
                if (tex) {
                    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                }
            }
        }
    } else {
        // Fallback for custom test grids using grid.anthills()
        for (const auto& a : grid.anthills()) {
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen((static_cast<int32_t>(a.x) - 1) * TILE_SIZE,
                                    (static_cast<int32_t>(a.y) - 1) * TILE_SIZE, sx, sy);
            SDL_Rect dst = { sx, sy, 128, 128 };
            SDL_Texture* tex = texture_cache_->get_named_sprite_texture(hill_sprites[a.team_id % 4]);
            if (tex) {
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            }
        }
    }
}

void Renderer::render_terrain_layer3_canopy() {
    for (const auto& obj : layer3_canopy_objects_) {
        int32_t right = obj.world_x + obj.width;
        int32_t bottom = obj.world_y + obj.height;
        if (right < camera_.world_x || obj.world_x > camera_.world_x + camera_.viewport_w ||
            bottom < camera_.world_y || obj.world_y > camera_.world_y + camera_.viewport_h) {
            continue;
        }

        int32_t sx = PLAYFIELD_X + (obj.world_x - camera_.world_x);
        int32_t sy = PLAYFIELD_Y + (obj.world_y - camera_.world_y);
        SDL_Texture* tex = texture_cache_->get_sprite_texture(static_cast<uint32_t>(obj.sprite_id));
        if (tex) {
            SDL_Rect dst = { sx, sy, obj.width, obj.height };
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        }
    }
}

void Renderer::render_visual_effects(const ants::sim::WorldState& world) {
    if (!archive_ || !texture_cache_) return;
    for (const auto& eff : world.effects) {
        int32_t sx = 0, sy = 0;
        if (!camera_.world_to_screen(eff.px, eff.py, sx, sy)) continue;
        const auto* anim = archive_->find_animation(eff.anim_name);
        if (!anim && !eff.anim_name.empty()) {
            std::string low = eff.anim_name;
            for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            anim = archive_->find_animation(low);
        }
        if (anim && !anim->subitems.empty()) {
            size_t sub_idx = eff.frame % anim->subitems.size();
            const auto& sub = anim->subitems[sub_idx];
            for (const auto& f : sub.frames) {
                SDL_Texture* tex = texture_cache_->get_sprite_texture(f.sprite_index);
                if (!tex) continue;
                const auto& sp = archive_->get_sprite(f.sprite_index);
                SDL_Rect dst = { sx + f.dx, sy + f.dy, static_cast<int>(sp.width), static_cast<int>(sp.height) };
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            }
        }
    }
}

void Renderer::draw_ant_shadow(int32_t anchor_sx, int32_t anchor_sy, int32_t altitude_z) {
    SDL_Texture* shadow_tex = texture_cache_->get_named_sprite_texture("shadow.bmp");
    if (!shadow_tex) return;

    SDL_Rect dst = { anchor_sx - 16, anchor_sy - 15, 32, 31 };
    uint8_t alpha = static_cast<uint8_t>(std::max(40, 220 - altitude_z * 4));
    SDL_SetTextureAlphaMod(shadow_tex, alpha);
    SDL_RenderCopy(renderer_, shadow_tex, nullptr, &dst);
    SDL_SetTextureAlphaMod(shadow_tex, 255);
}

void Renderer::draw_single_ant(const ants::sim::AntSnapshot& ant, bool is_selected, bool show_health_bar) {
    int32_t sx = 0, sy = 0;
    if (!camera_.world_to_screen(ant.px, ant.py, sx, sy)) return;

    // 1. Calculate Parabolic Elevation (Knockback Altitude)
    int32_t altitude_z = 0;
    if (ant.is_airborne) {
        int32_t t = static_cast<int32_t>(ant.anim_frame % 11);
        altitude_z = (4 * 36 * t * (10 - t)) / 100;
        draw_ant_shadow(sx, sy, altitude_z);
    }

    int32_t render_y = sy - altitude_z;

    // 2. Resolve Action Animation Prefix
    static const char* normal_prefixes[6]  = { "ag", "ab", "af", "at", "ac", "as" };
    static const char* holding_prefixes[6] = { "hg", "hb", "hf", "ht", "hc", "hs" };
    std::string prefix = ant.is_holding ? holding_prefixes[static_cast<size_t>(ant.type) % 6]
                                        : normal_prefixes[static_cast<size_t>(ant.type) % 6];
    std::string action = "st"; // Default Idle

    bool is_infiltrating = (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Infiltrating));
    bool is_entering_base = (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::EnteringBase));

    if (ant.is_underground) return; // Fully underground inside base hole, do not draw

    if (is_entering_base) {
        // Base entry animation for each ant type:
        // Worker:  agen301 / hgen301
        // Bomber:  aben301 / hben301
        // Fire:    afen301 / hfen301
        // Combat:  acen301 / hcen301
        // Swimmer: asen301 / hsen301
        // Thief:   aten301 / hten301
        static const char* type_letters[6] = { "g", "b", "f", "t", "c", "s" };
        char type_ch = type_letters[static_cast<size_t>(ant.type) % 6][0];
        bool carrying_food = (ant.is_holding || ant.had_food_at_base_entry);
        std::string anim_name = (carrying_food ? std::string("h") : std::string("a")) + type_ch + "en301";
        const auto* base_seq = archive_->find_animation(anim_name);
        if (!base_seq) base_seq = archive_->find_animation(carrying_food ? "hgen301" : "agen301");
        if (base_seq && !base_seq->subitems.empty()) {
            size_t sub_idx = std::min(static_cast<size_t>(ant.anim_frame), base_seq->subitems.size() - 1);
            const auto& sub = base_seq->subitems[sub_idx];
            auto frames = sub.frames;
            if (carrying_food && frames.size() > 1) {
                // Ensure lunchbox is rendered in the back (first), ant body in front (second)
                std::stable_sort(frames.begin(), frames.end(), [&](const auto& a, const auto& b) {
                    const auto& sp_a = archive_->get_sprite(a.sprite_index);
                    const auto& sp_b = archive_->get_sprite(b.sprite_index);
                    bool is_lb_a = (sp_a.name.find("lb") != std::string::npos);
                    bool is_lb_b = (sp_b.name.find("lb") != std::string::npos);
                    if (is_lb_a != is_lb_b) {
                        return is_lb_a;
                    }
                    return false;
                });
            }
            for (const auto& f : frames) {
                SDL_Texture* tex = texture_cache_->get_sprite_texture(f.sprite_index, false, static_cast<uint8_t>(ant.player_id));
                if (!tex) continue;
                const auto& sp = archive_->get_sprite(f.sprite_index);
                SDL_Rect dst = { sx + f.dx, render_y + f.dy, static_cast<int>(sp.width), static_cast<int>(sp.height) };
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            }
        }
        return;
    }

    if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Walking) ||
        ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Intercepting) ||
        ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::ReturningToPost)) {
        if (ant.is_swimming && !ant.is_holding) {
            action = "sw";
        } else if (ant.is_on_mud) {
            action = "wm";
        } else {
            action = "wg";
        }
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::BuildingBridge)) {
        action = ant.is_swimming ? "bbw" : "bbl";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::DemolishingBridge)) {
        action = ant.is_swimming ? "dbw" : "dbl";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::DivingInWater)) {
        action = "di";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::ExitingWater)) {
        action = "go";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Swimming)) {
        action = "tw";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Attacking) ||
               ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::HarvestingFood)) {
        action = "at";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Knockback)) {
        action = "gf";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Bounce)) {
        action = "gb";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Flinch)) {
        action = "gh";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Drowning)) {
        action = "dr";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::PlantingBomb)) {
        action = "sb";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::DefusingBomb)) {
        action = "db";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::PlacingFire)) {
        action = "sf";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::ExtinguishingFire)) {
        action = "xf";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::CantGo)) {
        action = "cg";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::QueuingBase)) {
        action = "st";
    } else {
        action = "st";
    }

    if (action == "gf" || action == "gb" || action == "gh" || action == "dr" ||
        action == "sb" || action == "db" || action == "sf" || action == "xf" ||
        action == "bbl" || action == "bbw" || action == "dbl" || action == "dbw" ||
        action == "di" || action == "go" ||
        (action == "at" && ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::HarvestingFood))) {
        prefix = normal_prefixes[static_cast<size_t>(ant.type) % 6];
    }

    ants::assets::Direction dir = static_cast<ants::assets::Direction>(ant.facing & 7);
    if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Drowning) ||
        ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::CantGo)) {
        dir = ants::assets::Direction::South;
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::BuildingBridge) ||
               ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::DemolishingBridge) ||
               ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::PlantingBomb) ||
               ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::DefusingBomb) ||
               ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::PlacingFire) ||
               ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::ExtinguishingFire)) {
        // These animations only exist for cardinal directions: North (7), South (3), East/West (9/5)
        if (dir == ants::assets::Direction::NorthEast || dir == ants::assets::Direction::NorthWest) {
            dir = ants::assets::Direction::North;
        } else if (dir == ants::assets::Direction::SouthEast || dir == ants::assets::Direction::SouthWest) {
            dir = ants::assets::Direction::South;
        }
    }

    if (ant.is_transforming) {
        // Authentic fidelity: ant body disappears during transformation while getpow animation plays
    } else if (is_infiltrating) {
        const auto* infil_seq = archive_->find_animation("atcr501");
        if (infil_seq && !infil_seq->subitems.empty()) {
            size_t sub_idx = ant.anim_frame % infil_seq->subitems.size();
            const auto& sub = infil_seq->subitems[sub_idx];
            for (const auto& f : sub.frames) {
                SDL_Texture* tex = texture_cache_->get_sprite_texture(f.sprite_index, false, static_cast<uint8_t>(ant.player_id));
                if (!tex) continue;
                const auto& sp = archive_->get_sprite(f.sprite_index);
                SDL_Rect dst = { sx + f.dx, render_y + f.dy, static_cast<int>(sp.width), static_cast<int>(sp.height) };
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            }
        }
    } else {
        const auto* seq = archive_->get_directional_animation(prefix + action, dir);
        if (seq && !seq->subitems.empty()) {
            size_t sub_idx = 0;
            if (action == "gf") {
                // Ballistic flight lasts 10 ticks; map tick to animation sequence frames
                sub_idx = (ant.anim_frame * seq->subitems.size()) / 10;
                if (sub_idx >= seq->subitems.size()) {
                    sub_idx = seq->subitems.size() - 1;
                }
            } else if (action == "gb") {
                // 1-Tile bounce lasts 4 ticks; map across the sequence frames
                sub_idx = (ant.anim_frame * seq->subitems.size()) / 4;
                if (sub_idx >= seq->subitems.size()) {
                    sub_idx = seq->subitems.size() - 1;
                }
            } else if (action == "sb") {
                // Planting bomb lasts 28 ticks; map across the sequence frames
                sub_idx = (ant.anim_frame * seq->subitems.size()) / 28;
                if (sub_idx >= seq->subitems.size()) {
                    sub_idx = seq->subitems.size() - 1;
                }
            } else {
                sub_idx = ant.anim_frame % seq->subitems.size();
            }
            const auto& sub = seq->subitems[sub_idx];

            auto frames = sub.frames;
            if (ant.is_holding && frames.size() > 1) {
                // Ensure lunchbox is rendered in the back (first), ant body in front (second)
                std::stable_sort(frames.begin(), frames.end(), [&](const auto& a, const auto& b) {
                    const auto& sp_a = archive_->get_sprite(a.sprite_index);
                    const auto& sp_b = archive_->get_sprite(b.sprite_index);
                    bool is_lb_a = (sp_a.name.find("lb") != std::string::npos);
                    bool is_lb_b = (sp_b.name.find("lb") != std::string::npos);
                    if (is_lb_a != is_lb_b) {
                        return is_lb_a;
                    }
                    return false;
                });
            }

            for (const auto& f : frames) {
                uint32_t sp_idx = f.sprite_index;
                if (ant.player_id < 4 && texture_cache_->is_base_bomb_sprite(sp_idx)) {
                    sp_idx = texture_cache_->get_team_bomb_sprite_index(static_cast<uint8_t>(ant.player_id));
                }
                bool mirrored = ants::assets::get_direction_mapping(dir).mirrored;
                SDL_Texture* tex = texture_cache_->get_sprite_texture(sp_idx, mirrored, static_cast<uint8_t>(ant.player_id));
                if (!tex) continue;

                const auto& sp = mirrored ? archive_->get_mirrored_sprite(sp_idx)
                                          : archive_->get_sprite(sp_idx);
                SDL_Rect dst = { sx + f.dx, render_y + f.dy, static_cast<int>(sp.width), static_cast<int>(sp.height) };
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            }
        } else {
            // Fallback: draw directional stand sprite
            std::string fallback_name = prefix + "st301.bmp";
            SDL_Texture* tex = texture_cache_->get_named_sprite_texture(fallback_name, false, static_cast<uint8_t>(ant.player_id));
            if (tex) {
                SDL_Rect dst = { sx - 16, render_y - 16, 32, 32 };
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            }
        }
    }

    // 2.5 Power-Up Transformation Animation (11 frames of getpow: pucov1..5.bmp)
    if (ant.is_transforming) {
        const auto* pow_seq = archive_->find_animation("getpow");
        if (pow_seq && !pow_seq->subitems.empty()) {
            size_t psub = ant.transform_anim_frame % pow_seq->subitems.size();
            const auto& sub = pow_seq->subitems[psub];
            for (const auto& f : sub.frames) {
                SDL_Texture* ptex = texture_cache_->get_sprite_texture(f.sprite_index, false, static_cast<uint8_t>(ant.player_id));
                if (ptex) {
                    const auto& sp = archive_->get_sprite(f.sprite_index);
                    SDL_Rect pdst = { sx + f.dx, render_y + f.dy, static_cast<int>(sp.width), static_cast<int>(sp.height) };
                    SDL_RenderCopy(renderer_, ptex, nullptr, &pdst);
                }
            }
        }
    }

    // 3. Selection Indicator (4 bright green corner brackets framing the unit)
    if (is_selected) {
        SDL_SetRenderDrawColor(renderer_, 50, 220, 50, 255);
        int32_t bx = sx - 15;
        int32_t by = render_y - 28;
        int32_t bw = 30;
        int32_t bh = 38;
        int32_t arm = 6;

        // Top-left
        SDL_RenderDrawLine(renderer_, bx, by, bx + arm, by);
        SDL_RenderDrawLine(renderer_, bx, by, bx, by + arm);

        // Top-right
        SDL_RenderDrawLine(renderer_, bx + bw, by, bx + bw - arm, by);
        SDL_RenderDrawLine(renderer_, bx + bw, by, bx + bw, by + arm);

        // Bottom-left
        SDL_RenderDrawLine(renderer_, bx, by + bh, bx + arm, by + bh);
        SDL_RenderDrawLine(renderer_, bx, by + bh, bx, by + bh - arm);

        // Bottom-right
        SDL_RenderDrawLine(renderer_, bx + bw, by + bh, bx + bw - arm, by + bh);
        SDL_RenderDrawLine(renderer_, bx + bw, by + bh, bx + bw, by + bh - arm);
    }

    // 4. Overhead Unit Health Bar (Framed bar with health-percentage coloring)
    if ((is_selected || show_health_bar) && ant.hp > 0 && !ant.is_drowning) {
        SDL_Rect bar_border = { sx - 13, render_y - 36, 26, 6 };
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer_, &bar_border);

        SDL_Rect bar_bg = { sx - 12, render_y - 35, 24, 4 };
        SDL_SetRenderDrawColor(renderer_, 40, 40, 40, 220);
        SDL_RenderFillRect(renderer_, &bar_bg);

        int hp_w = (ant.max_hp > 0) ? std::clamp((ant.hp * 24) / ant.max_hp, 0, 24) : 0;
        SDL_Rect bar_fg = { sx - 12, render_y - 35, hp_w, 4 };
        if (ant.hp > 6) SDL_SetRenderDrawColor(renderer_, 50, 220, 50, 255);
        else if (ant.hp > 3) SDL_SetRenderDrawColor(renderer_, 230, 200, 30, 255);
        else SDL_SetRenderDrawColor(renderer_, 230, 40, 40, 255);
        SDL_RenderFillRect(renderer_, &bar_fg);
    }
}

void Renderer::draw_anthill_selection_brackets(int32_t x, int32_t y, int32_t w, int32_t h) {
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

void Renderer::render_ant_units(const ants::sim::WorldState& world,
                                int32_t selected_unit_id,
                                const std::vector<uint32_t>& selected_unit_ids,
                                bool show_all_health_bars) {
    for (const auto& a : world.ants) {
        if (a.is_underground) continue;
        if (a.hp == 0 && !a.is_drowning &&
            a.anim_state != static_cast<uint16_t>(ants::sim::UnitState::Knockback) &&
            a.anim_state != static_cast<uint16_t>(ants::sim::UnitState::Bounce)) continue;

        bool is_sel = false;
        if (!selected_unit_ids.empty()) {
            for (uint32_t sid : selected_unit_ids) {
                if (sid == a.id) { is_sel = true; break; }
            }
        } else {
            is_sel = (a.id == static_cast<uint32_t>(selected_unit_id));
        }

        RenderItem item{};
        item.sort_y = a.py;
        item.draw_func = [this, a, is_sel, show_all_health_bars](SDL_Renderer*, TextureCache&) {
            this->draw_single_ant(a, is_sel, show_all_health_bars);
        };
        render_queue_.push_back(item);
    }

    // Y-Sorting (Background to Foreground)
    std::sort(render_queue_.begin(), render_queue_.end(), [](const RenderItem& a, const RenderItem& b) {
        return a.sort_y < b.sort_y;
    });

    for (auto& item : render_queue_) {
        item.draw_func(renderer_, *texture_cache_);
    }
}

void Renderer::render_tile_grid(const ants::sim::Grid& grid, int32_t mouse_x, int32_t mouse_y) {
    if (!renderer_) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE);
    int32_t end_col   = std::min(static_cast<int32_t>(grid.width()) - 1,
                                 (static_cast<int32_t>(camera_.x) + PLAYFIELD_W + 31) / TILE_SIZE);
    int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE);
    int32_t end_row   = std::min(static_cast<int32_t>(grid.height()) - 1,
                                 (static_cast<int32_t>(camera_.y) + PLAYFIELD_H + 31) / TILE_SIZE);

    // Determine hovered tile under mouse
    int32_t hover_tx = -1;
    int32_t hover_ty = -1;
    if (mouse_x >= PLAYFIELD_X && mouse_x < PLAYFIELD_X + PLAYFIELD_W &&
        mouse_y >= PLAYFIELD_Y && mouse_y < PLAYFIELD_Y + PLAYFIELD_H) {
        int32_t world_x = camera_.world_x + (mouse_x - PLAYFIELD_X);
        int32_t world_y = camera_.world_y + (mouse_y - PLAYFIELD_Y);
        hover_tx = std::clamp(world_x / TILE_SIZE, 0, static_cast<int32_t>(grid.width()) - 1);
        hover_ty = std::clamp(world_y / TILE_SIZE, 0, static_cast<int32_t>(grid.height()) - 1);
    } else {
        hover_tx = std::clamp((camera_.world_x + PLAYFIELD_W / 2) / TILE_SIZE, 0, static_cast<int32_t>(grid.width()) - 1);
        hover_ty = std::clamp((camera_.world_y + PLAYFIELD_H / 2) / TILE_SIZE, 0, static_cast<int32_t>(grid.height()) - 1);
    }

    // 1. Draw tile outline for each tile and coordinates in bottom-left of each tile
    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(c * TILE_SIZE, r * TILE_SIZE, sx, sy);

            // Subtle semi-transparent tile outline
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 75);
            SDL_Rect tile_rect = { sx, sy, TILE_SIZE, TILE_SIZE };
            SDL_RenderDrawRect(renderer_, &tile_rect);

            // Coordinates inside bottom-left of tile
            std::string c_str = std::to_string(c) + "," + std::to_string(r);
            draw_text(c_str, sx + 3, sy + 24, ants::assets::ColorRGBA{0, 0, 0, 180});
            draw_text(c_str, sx + 2, sy + 23, ants::assets::ColorRGBA{255, 255, 200, 220});
        }
    }

    // 2. Active hovered tile highlight
    if (hover_tx >= 0 && hover_ty >= 0) {
        int32_t hsx = 0, hsy = 0;
        camera_.world_to_screen(hover_tx * TILE_SIZE, hover_ty * TILE_SIZE, hsx, hsy);
        SDL_SetRenderDrawColor(renderer_, 0, 255, 255, 255);
        SDL_Rect h1 = { hsx, hsy, TILE_SIZE, TILE_SIZE };
        SDL_RenderDrawRect(renderer_, &h1);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 0, 220);
        SDL_Rect h2 = { hsx + 1, hsy + 1, TILE_SIZE - 2, TILE_SIZE - 2 };
        SDL_RenderDrawRect(renderer_, &h2);
    }

    // 3. Tile coordinates readout badge in the bottom left-hand corner of the playfield
    int32_t badge_x = PLAYFIELD_X + 4;
    int32_t badge_y = PLAYFIELD_Y + PLAYFIELD_H - 18;
    int32_t badge_w = 88;
    int32_t badge_h = 16;

    fill_rect(badge_x, badge_y, badge_w, badge_h, ants::assets::ColorRGBA{0, 0, 0, 210});
    draw_rect(badge_x, badge_y, badge_w, badge_h, ants::assets::ColorRGBA{0, 255, 255, 230});

    std::string badge_text = "X: " + std::to_string(hover_tx) + "  Y: " + std::to_string(hover_ty);
    draw_text(badge_text, badge_x + 6, badge_y + 4, ants::assets::ColorRGBA{255, 255, 255, 255});
}

void Renderer::render_minimap(const ants::sim::WorldState& world,
                              const ants::sim::Grid& grid) {
    if (!renderer_ || map_width_ == 0 || map_height_ == 0) return;

    float sx_scale = static_cast<float>(MINIMAP_W) / static_cast<float>(map_width_);
    float sy_scale = static_cast<float>(MINIMAP_H) / static_cast<float>(map_height_);

    // 1. Terrain Pass (Sampled)
    for (uint32_t y = 0; y < map_height_; y += 2) {
        for (uint32_t x = 0; x < map_width_; x += 2) {
            const auto& cell = grid.get_cell(x, y);
            if (cell.terrain_type == ants::sim::TERRAIN_WATER) {
                SDL_SetRenderDrawColor(renderer_, 45, 90, 220, 255);
            } else if (cell.terrain_type == ants::sim::TERRAIN_OBSTACLE || cell.is_obstacle_overlay) {
                SDL_SetRenderDrawColor(renderer_, 47, 81, 48, 255);
            } else if (cell.is_mud) {
                SDL_SetRenderDrawColor(renderer_, 75, 55, 45, 255);
            } else {
                SDL_SetRenderDrawColor(renderer_, 155, 115, 108, 255);
            }
            SDL_Rect dot = {
                MINIMAP_X + static_cast<int>(x * sx_scale),
                MINIMAP_Y + static_cast<int>(y * sy_scale),
                std::max(1, static_cast<int>(sx_scale * 2.0f)),
                std::max(1, static_cast<int>(sy_scale * 2.0f))
            };
            SDL_RenderFillRect(renderer_, &dot);
        }
    }

    // 2. Anthills (Bases)
    for (const auto& hill : grid.anthills()) {
        static const SDL_Color base_colors[4] = {
            { 79, 87, 111, 255 },  // Team 0: Black
            { 119, 175, 239, 255 }, // Team 1: Blue
            { 251, 51, 91, 255 },   // Team 2: Red
            { 83, 147, 43, 255 }    // Team 3: Green
        };
        const auto& c = base_colors[hill.team_id % 4];
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        SDL_Rect bdot = {
            MINIMAP_X + static_cast<int>(hill.x * sx_scale) - 2,
            MINIMAP_Y + static_cast<int>(hill.y * sy_scale) - 2,
            5, 5
        };
        SDL_RenderFillRect(renderer_, &bdot);
    }

    // 3. Units Pass
    for (const auto& a : world.ants) {
        if (a.is_underground) continue;
        static const SDL_Color unit_colors[4] = {
            { 90, 240, 90, 255 },   // 0: Green
            { 255, 70, 70, 255 },   // 1: Red
            { 120, 180, 255, 255 }, // 2: Blue
            { 200, 200, 220, 255 }  // 3: Black
        };
        const auto& c = unit_colors[a.player_id % 4];
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        SDL_Rect udot = {
            MINIMAP_X + static_cast<int>((a.px / 32) * sx_scale),
            MINIMAP_Y + static_cast<int>((a.py / 32) * sy_scale),
            2, 2
        };
        SDL_RenderFillRect(renderer_, &udot);
    }

    // 4. Viewport Wireframe Box
    float map_w_px = static_cast<float>(map_width_ * TILE_SIZE);
    float map_h_px = static_cast<float>(map_height_ * TILE_SIZE);
    if (map_w_px > 0.0f && map_h_px > 0.0f) {
        SDL_Rect cam_box = {
            MINIMAP_X + static_cast<int>((camera_.x / map_w_px) * MINIMAP_W),
            MINIMAP_Y + static_cast<int>((camera_.y / map_h_px) * MINIMAP_H),
            std::max(4, static_cast<int>((PLAYFIELD_W / map_w_px) * MINIMAP_W)),
            std::max(4, static_cast<int>((PLAYFIELD_H / map_h_px) * MINIMAP_H))
        };
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer_, &cam_box);
    }
}

void Renderer::render_hud_chrome(const ants::sim::WorldState&, int32_t) {
    if (!renderer_) return;

    // Right panel base backing fill to ensure zero dark gaps between modular HUD tiles (Authentic index 10)
    static const SDL_Color hud_bg_colors[4] = {
        { 43, 104,  95, 255},  // Green (Player 0) - Authentic index 10
        {143,  35,  99, 255},  // Red   (Player 1) - Authentic index 10
        { 51,  87, 163, 255},  // Blue  (Player 2) - Authentic index 10
        { 87,  87,  91, 255}   // Black (Player 3) - Authentic index 10
    };
    const auto& bg_col = hud_bg_colors[hud_team_id_ % 4];
    SDL_SetRenderDrawColor(renderer_, bg_col.r, bg_col.g, bg_col.b, bg_col.a);
    SDL_Rect right_bg = { 480, 22, 160, 458 };
    SDL_RenderFillRect(renderer_, &right_bg);

    // Top Bar (640x22)
    SDL_Texture* top_tex = texture_cache_->get_named_sprite_texture("x0y0.bmp", false, hud_team_id_);
    if (top_tex) {
        SDL_Rect top_rect = { 0, 0, 640, 22 };
        SDL_RenderCopy(renderer_, top_tex, nullptr, &top_rect);
    }

    // Left Border (17x458)
    SDL_Texture* left_tex = texture_cache_->get_named_sprite_texture("x0y22.bmp", false, hud_team_id_);
    if (left_tex) {
        SDL_Rect left_rect = { 0, 22, 17, 458 };
        SDL_RenderCopy(renderer_, left_tex, nullptr, &left_rect);
    }

    // Right Divider (22x426)
    SDL_Texture* div_tex = texture_cache_->get_named_sprite_texture("x458y35.bmp", false, hud_team_id_);
    if (div_tex) {
        SDL_Rect div_rect = { 458, 35, 22, 426 };
        SDL_RenderCopy(renderer_, div_tex, nullptr, &div_rect);
    }

    // Bottom News Banner (623x19)
    SDL_Texture* bot_tex = texture_cache_->get_named_sprite_texture("x17y461.bmp", false, hud_team_id_);
    if (bot_tex) {
        SDL_Rect bot_rect = { 17, 461, 623, 19 };
        SDL_RenderCopy(renderer_, bot_tex, nullptr, &bot_rect);
    }

    // Selection Card Backing (160x128)
    SDL_Texture* card_tex = texture_cache_->get_named_sprite_texture("x480y126.bmp", false, hud_team_id_);
    if (card_tex) {
        SDL_Rect card_rect = { CARD_X, CARD_Y, CARD_W, CARD_H };
        SDL_RenderCopy(renderer_, card_tex, nullptr, &card_rect);
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
    SDL_Rect dst = { x, y, static_cast<int>(sp.width), static_cast<int>(sp.height) };
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
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
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(renderer_, &rect);
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    }
}

void Renderer::draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) {
    if (!renderer_) return;
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(renderer_, &rect);
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    }
}

void Renderer::draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color) {
    draw_text(text, x, y, color, FontSize::Small);
}

void Renderer::draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color, FontSize size) {
    if (!renderer_ || text.empty()) return;

    // Handle multi-line strings
    if (text.find('\n') != std::string::npos) {
        int32_t cur_y = y;
        int32_t line_height = (size == FontSize::Large) ? 20 : ((size == FontSize::Medium) ? 16 : 14);
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
    TTF_Font* font = font_small_;
    if (size == FontSize::Medium && font_medium_) {
        font = font_medium_;
    } else if (size == FontSize::Large && font_large_) {
        font = font_large_;
    }

    if (font) {
        uint32_t c_u32 = (static_cast<uint32_t>(color.r) << 24) |
                         (static_cast<uint32_t>(color.g) << 16) |
                         (static_cast<uint32_t>(color.b) << 8)  |
                         static_cast<uint32_t>(color.a);
        TextCacheKey key{text, c_u32, static_cast<uint8_t>(size)};
        auto it = text_cache_.find(key);
        if (it != text_cache_.end()) {
            it->second.last_frame = text_frame_counter_;
            SDL_Rect dst{x, y, it->second.width, it->second.height};
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
                SDL_Rect dst{x, y, lw, lh};
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                SDL_FreeSurface(surf);
                return;
            }
            SDL_FreeSurface(surf);
        }
    }
#endif

    // Fallback: built-in 5x7 bitmap font
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);

    int32_t cur_x = x;
    int32_t cur_y = y;

    for (char c : text) {
        if (c == '\n') {
            cur_x = x;
            cur_y += 10;
            continue;
        }

        uint8_t glyph_idx = (c >= 32 && c <= 126) ? static_cast<uint8_t>(c - 32) : static_cast<uint8_t>('?' - 32);
        const auto& glyph = FONT_5X7[glyph_idx];

        for (int row = 0; row < 7; ++row) {
            uint8_t row_bits = glyph.rows[row];
            for (int col = 0; col < glyph.width; ++col) {
                if (row_bits & (0x80 >> col)) {
                    SDL_RenderDrawPoint(renderer_, cur_x + col, cur_y + row);
                }
            }
        }
        cur_x += glyph.width + 1;
    }
    if (color.a < 255) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    }
}

int32_t Renderer::get_text_width(const std::string& text, FontSize size) const {
    if (text.empty()) return 0;
#ifdef ANTS_ENABLE_SDL_TTF
    TTF_Font* font = font_small_;
    if (size == FontSize::Medium && font_medium_) {
        font = font_medium_;
    } else if (size == FontSize::Large && font_large_) {
        font = font_large_;
    }
    if (font) {
        int w = 0, h = 0;
        if (TTF_SizeUTF8(font, text.c_str(), &w, &h) == 0) {
            return (w + 1) / 2;
        }
    }
#else
    (void)size;
#endif
    return static_cast<int32_t>(text.size()) * 6;
}

int32_t Renderer::get_text_height(FontSize size) const {
#ifdef ANTS_ENABLE_SDL_TTF
    TTF_Font* font = font_small_;
    if (size == FontSize::Medium && font_medium_) {
        font = font_medium_;
    } else if (size == FontSize::Large && font_large_) {
        font = font_large_;
    }
    if (font) {
        int h = TTF_FontHeight(font);
        return (h + 1) / 2;
    }
#else
    (void)size;
#endif
    return 7;
}

bool Renderer::save_screenshot(const std::string& path) {
    if (!renderer_) return false;
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer_, &w, &h);
    if (w <= 0 || h <= 0) { w = CANVAS_WIDTH; h = CANVAS_HEIGHT; }

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return false;

    if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) != 0) {
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
