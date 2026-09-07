#include "ants_app/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstring>

namespace ants::app {

// ============================================================================
// Built-in 8x8 ASCII Bitmap Font (ASCII 32 ' ' through 126 '~')
// ============================================================================
static const uint8_t FONT_8X8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // 33 '!'
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, // 34 '"'
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, // 35 '#'
    {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00}, // 36 '$'
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00}, // 37 '%'
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, // 38 '&'
    {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00}, // 39 '''
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // 40 '('
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // 41 ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // 42 '*'
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // 43 '+'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // 44 ','
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // 45 '-'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // 46 '.'
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, // 47 '/'
    {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00}, // 48 '0'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 49 '1'
    {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00}, // 50 '2'
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, // 51 '3'
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00}, // 52 '4'
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, // 53 '5'
    {0x7C,0xC6,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}, // 54 '6'
    {0xFE,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, // 55 '7'
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, // 56 '8'
    {0x7C,0xC6,0xC6,0x7E,0x06,0xC6,0x7C,0x00}, // 57 '9'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // 58 ':'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00}, // 59 ';'
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // 60 '<'
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, // 61 '='
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, // 62 '>'
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00}, // 63 '?'
    {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00}, // 64 '@'
    {0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0x00}, // 65 'A'
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, // 66 'B'
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, // 67 'C'
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, // 68 'D'
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, // 69 'E'
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00}, // 70 'F'
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00}, // 71 'G'
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 72 'H'
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}, // 73 'I'
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}, // 74 'J'
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, // 75 'K'
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00}, // 76 'L'
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00}, // 77 'M'
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, // 78 'N'
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 79 'O'
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, // 80 'P'
    {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06}, // 81 'Q'
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00}, // 82 'R'
    {0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00}, // 83 'S'
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 84 'T'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 85 'U'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 86 'V'
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, // 87 'W'
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, // 88 'X'
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00}, // 89 'Y'
    {0xFE,0xC6,0x0C,0x18,0x30,0x66,0xFE,0x00}, // 90 'Z'
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // 91 '['
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, // 92 '\'
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // 93 ']'
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // 94 '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 95 '_'
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, // 96 '`'
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}, // 97 'a'
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00}, // 98 'b'
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00}, // 99 'c'
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00}, // 100 'd'
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}, // 101 'e'
    {0x1C,0x36,0x30,0x78,0x30,0x30,0x78,0x00}, // 102 'f'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8}, // 103 'g'
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, // 104 'h'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 105 'i'
    {0x0C,0x00,0x1C,0x0C,0x0C,0xCC,0xCC,0x78}, // 106 'j'
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, // 107 'k'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 108 'l'
    {0x00,0x00,0xE6,0xFF,0xDB,0xC9,0xC9,0x00}, // 109 'm'
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00}, // 110 'n'
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}, // 111 'o'
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0}, // 112 'p'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E}, // 113 'q'
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00}, // 114 'r'
    {0x00,0x00,0x7C,0xC0,0x7C,0x06,0xFC,0x00}, // 115 's'
    {0x30,0x30,0x7C,0x30,0x30,0x34,0x18,0x00}, // 116 't'
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, // 117 'u'
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 118 'v'
    {0x00,0x00,0xC6,0xD6,0xD6,0xFE,0x6C,0x00}, // 119 'w'
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, // 120 'x'
    {0x00,0x00,0xC6,0xC6,0xCE,0x76,0x06,0xFC}, // 121 'y'
    {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00}, // 122 'z'
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, // 123 '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // 124 '|'
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, // 125 '}'
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}  // 126 '~'
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
    if (!renderer_ || sprite_id >= archive_.sprite_count()) return nullptr;

    uint64_t key = (static_cast<uint64_t>(sprite_id) << 4) |
                   (static_cast<uint64_t>(team_id & 0x07) << 1) |
                   (mirrored ? 1 : 0);
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        return it->second;
    }

    const auto& sp = mirrored ? archive_.get_mirrored_sprite(sprite_id)
                              : archive_.get_sprite(sprite_id);
    if (sp.width == 0 || sp.height == 0) return nullptr;

    // Convert 8-bit paletted sprite to 32-bit RGBA
    auto pal = archive_.get_palette();
    if (team_id > 0 && team_id < 4) {
        // Authentic Microsoft Ants palette remap:
        // Team 0 (Black): indices 80..99
        // Team 1 (Blue): indices 100..119 (+20 shift)
        // Team 2 (Red): indices 120..139 (+40 shift)
        // Team 3 (Green): indices 140..159 (+60 shift)
        const auto& base_pal = archive_.get_palette();
        const size_t offset = static_cast<size_t>(team_id) * 20;
        for (size_t i = 80; i <= 99; ++i) {
            pal[i] = base_pal[i + offset];
        }
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

    SDL_RenderSetLogicalSize(renderer_, CANVAS_WIDTH, CANVAS_HEIGHT);
    if (integer_scale) {
        SDL_RenderSetIntegerScale(renderer_, SDL_TRUE);
    }

    texture_cache_ = std::make_unique<TextureCache>(renderer_, archive);
    return true;
}

void Renderer::shutdown() {
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

void Renderer::set_level(const ants::assets::LevelData& level) {
    map_width_ = level.width;
    map_height_ = level.height;
    camera_.clamp_to_bounds(map_width_, map_height_);

    // Pre-resolve tile dictionary strings to sprite IDs
    tile_sprite_ids_.assign(level.tile_dictionary.size(), -1);
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

        // Map food items to authentic food morsel
        std::string lower_name = name;
        for (char& ch : lower_name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lower_name.rfind("fd", 0) == 0 || lower_name.rfind("food", 0) == 0) {
            int32_t sid = archive_->find_sprite_id(name + ".bmp");
            if (sid < 0) sid = archive_->find_sprite_id(name);
            if (sid < 0) {
                const auto* anim = archive_->find_animation(lower_name);
                if (!anim) anim = archive_->find_animation(name);
                if (anim && !anim->subitems.empty() && !anim->subitems[0].frames.empty()) {
                    sid = static_cast<int32_t>(anim->subitems[0].frames[0].sprite_index);
                }
            }
            if (sid < 0) {
                // Try base animation with '1' suffix e.g. fdburgr1, fdsuckr1, fdgumw1
                std::string stem = lower_name;
                while (!stem.empty() && std::isdigit(static_cast<unsigned char>(stem.back()))) {
                    stem.pop_back();
                }
                const auto* anim1 = archive_->find_animation(stem + "1");
                if (!anim1) anim1 = archive_->find_animation(stem);
                if (anim1 && !anim1->subitems.empty() && !anim1->subitems[0].frames.empty()) {
                    sid = static_cast<int32_t>(anim1->subitems[0].frames[0].sprite_index);
                }
            }
            if (sid < 0) {
                if (lower_name.find("pez") != std::string::npos) sid = archive_->find_sprite_id("pez1.bmp");
                else if (lower_name.find("pizz") != std::string::npos) sid = archive_->find_sprite_id("pizza.bmp");
                else if (lower_name.find("fish") != std::string::npos) sid = archive_->find_sprite_id("fish.bmp");
                else if (lower_name.find("chick") != std::string::npos) sid = archive_->find_sprite_id("chicken.bmp");
                else if (lower_name.find("pmeat") != std::string::npos) sid = archive_->find_sprite_id("meata.bmp");
                else if (lower_name.find("crak") != std::string::npos) sid = archive_->find_sprite_id("cracker.bmp");
                else if (lower_name.find("egg") != std::string::npos) sid = archive_->find_sprite_id("eggs.bmp");
                else if (lower_name.find("cornd") != std::string::npos) sid = archive_->find_sprite_id("corndog.bmp");
                else if (lower_name.find("twin") != std::string::npos) sid = archive_->find_sprite_id("twink.bmp");
                else if (lower_name.find("jelo") != std::string::npos) sid = archive_->find_sprite_id("jello1.bmp");
                else if (lower_name.find("frl") != std::string::npos) sid = archive_->find_sprite_id("fried.bmp");
            }
            if (sid < 0) sid = archive_->find_sprite_id("meatmov1.bmp");
            if (sid < 0) sid = archive_->find_sprite_id("foodse.bmp");
            tile_sprite_ids_[i] = sid;
            continue;
        }

        // 1. Direct or .bmp lookup (archive_->find_sprite_id is case-insensitive)
        int32_t sid = archive_->find_sprite_id(name);
        if (sid < 0) sid = archive_->find_sprite_id(name + ".bmp");

        // 2. Numerical variant resolution (e.g. M01b -> m01b2.bmp)
        if (sid < 0) sid = archive_->find_sprite_id(lower_name + "2.bmp");
        if (sid < 0) sid = archive_->find_sprite_id(lower_name + "2");

        // 3. Strip suffix after underscore (e.g. m01d_a -> M01d2.bmp)
        if (sid < 0 && lower_name.find('_') != std::string::npos) {
            std::string prefix = lower_name.substr(0, lower_name.find('_'));
            sid = archive_->find_sprite_id(prefix);
            if (sid < 0) sid = archive_->find_sprite_id(prefix + ".bmp");
            if (sid < 0) sid = archive_->find_sprite_id(prefix + "2.bmp");
            if (sid < 0) sid = archive_->find_sprite_id(prefix + "2");
        }

        // 4. Base category fallback for mud, grass, water
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

        // 5. Resolve animated frame sequences for water and mud
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
    for (int t = 0; t < 4; ++t) {
        anthill_bases_[t] = { -1, -1 };
    }
    has_anthill_bases_ = false;

    for (uint32_t y = 0; y < level.height; ++y) {
        for (uint32_t x = 0; x < level.width; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            if (c2.tile_index < level.tile_dictionary.size()) {
                const std::string& tname = level.tile_dictionary[c2.tile_index];
                int team = -1;
                if (tname == "BLACKHILL" || tname == "blackhill") team = 0;
                else if (tname == "BLUEHILL" || tname == "bluehill") team = 1;
                else if (tname == "REDHILL" || tname == "redhill") team = 2;
                else if (tname == "GREENHILL" || tname == "greenhill") team = 3;

                if (team >= 0) {
                    if (anthill_bases_[team].x < 0 || static_cast<int32_t>(x) < anthill_bases_[team].x) {
                        anthill_bases_[team].x = static_cast<int32_t>(x);
                    }
                    if (anthill_bases_[team].y < 0 || static_cast<int32_t>(y) < anthill_bases_[team].y) {
                        anthill_bases_[team].y = static_cast<int32_t>(y);
                    }
                    has_anthill_bases_ = true;
                }
            }
        }
    }

    // Deduplicate Layer 2 multi-tile decor objects into unique instances
    static_decor_objects_.clear();
    struct DecorGroup {
        int32_t min_x{999999};
        int32_t min_y{999999};
        uint16_t tile_index{0};
    };
    std::unordered_map<uint32_t, DecorGroup> groups;

    for (uint32_t y = 0; y < level.height; ++y) {
        for (uint32_t x = 0; x < level.width; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            if (c2.tile_index >= level.tile_dictionary.size()) continue;
            if (c2.is_empty() || c2.tile_index == 0xFFFF || c2.tile_index == 0x7FFE) continue;

            const std::string& tname = level.tile_dictionary[c2.tile_index];
            if (tname.empty() || tname == ".") continue;

            std::string low = tname;
            for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (low.find("hill") != std::string::npos || low.find("start") != std::string::npos ||
                low.rfind("fd", 0) == 0 || low.rfind("food", 0) == 0) {
                continue;
            }

            int32_t sid = (c2.tile_index < tile_sprite_ids_.size()) ? tile_sprite_ids_[c2.tile_index] : -1;
            if (sid < 0) continue;
            const auto& sp = archive_->get_sprite(static_cast<uint32_t>(sid));
            if (sp.width == 0 || sp.height == 0) continue;
            if (sp.width == 128 && sp.height == 128) continue; // Anthills handled separately

            if (c2.properties != 0) {
                uint32_t key = (static_cast<uint32_t>(c2.tile_index) << 16) | c2.properties;
                auto& grp = groups[key];
                grp.tile_index = c2.tile_index;
                grp.min_x = std::min(grp.min_x, static_cast<int32_t>(x));
                grp.min_y = std::min(grp.min_y, static_cast<int32_t>(y));
            } else {
                StaticMapObject obj{};
                obj.world_x = static_cast<int32_t>(x * TILE_SIZE);
                obj.world_y = static_cast<int32_t>(y * TILE_SIZE);
                obj.sprite_id = sid;
                obj.width = static_cast<int32_t>(sp.width);
                obj.height = static_cast<int32_t>(sp.height);
                static_decor_objects_.push_back(obj);
            }
        }
    }

    for (const auto& pair : groups) {
        const auto& grp = pair.second;
        int32_t sid = (grp.tile_index < tile_sprite_ids_.size()) ? tile_sprite_ids_[grp.tile_index] : -1;
        if (sid >= 0) {
            const auto& sp = archive_->get_sprite(static_cast<uint32_t>(sid));
            StaticMapObject obj{};
            obj.world_x = grp.min_x * TILE_SIZE;
            obj.world_y = grp.min_y * TILE_SIZE;
            obj.sprite_id = sid;
            obj.width = static_cast<int32_t>(sp.width);
            obj.height = static_cast<int32_t>(sp.height);
            static_decor_objects_.push_back(obj);
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
                            bool show_all_health_bars) {
    if (!renderer_) return;
    render_queue_.clear();

    // 1. Clip exclusively to playfield
    SDL_Rect clip_rect = { PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H };
    SDL_RenderSetClipRect(renderer_, &clip_rect);

    // 2. Layer 1 Terrain
    render_terrain_layer1(grid);

    // 3. Layer 2 Structures / Interactive Objects
    render_terrain_layer2_structures(grid);

    // 4. Ant Units (Depth-Sorted)
    render_ant_units(world, selected_unit_id, selected_unit_ids, show_all_health_bars);

    // 5. Unset clipping for full-canvas chrome
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
                size_t frame_idx = (anim_tick_ / 4) % frames.size();
                sid = frames[frame_idx];
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

            // 1. Bridges (Stages 1..4)
            if (cell.interactive_id >= ants::sim::TILE_BRIDGE1 &&
                cell.interactive_id <= ants::sim::TILE_BRIDGE4) {
                int stage = cell.interactive_id - ants::sim::TILE_BRIDGE1 + 1;
                std::string bname = (stage == 4) ? "bridge4a.bmp" : ("bridge" + std::to_string(stage) + ".bmp");
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture(bname);
                if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                continue;
            }

            // 2. Fire Walls
            if (cell.has_fire()) {
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture("9fire01.bmp");
                if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                continue;
            }

            // 3. Bombs
            if (cell.has_bomb()) {
                static const char* bomb_names[4] = { "1bombblk.bmp", "1bombblu.bmp", "1bombred.bmp", "1bombgrn.bmp" };
                uint8_t owner = cell.interactive_owner % 4;
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture(bomb_names[owner]);
                if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                continue;
            }

            // 4. Dropped Lunchbox
            if (cell.has_lunchbox()) {
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture("lunchicon.bmp");
                if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                continue;
            }

            // 5. Authentic Food Items
            if (cell.has_food()) {
                SDL_Texture* tex = nullptr;
                if (cell.interactive_id < tile_sprite_ids_.size() && tile_sprite_ids_[cell.interactive_id] >= 0) {
                    tex = texture_cache_->get_sprite_texture(static_cast<uint32_t>(tile_sprite_ids_[cell.interactive_id]));
                }
                if (!tex) {
                    tex = texture_cache_->get_named_sprite_texture("meatmov1.bmp");
                }
                if (!tex) {
                    tex = texture_cache_->get_named_sprite_texture("foodse.bmp");
                }
                if (tex) {
                    SDL_Rect food_dst = { sx + 2, sy + 2, 28, 28 };
                    SDL_RenderCopy(renderer_, tex, nullptr, &food_dst);
                }
                continue;
            }
        }
    }

    // 6. Static Decorative Overlays (Rendered once per unique instance with full bounds culling)
    for (const auto& obj : static_decor_objects_) {
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
            SDL_Rect decor_dst = { sx, sy, obj.width, obj.height };
            SDL_RenderCopy(renderer_, tex, nullptr, &decor_dst);
        }
    }

    // Anthill Bases (Authentic 128x128 4x4 bases: bkhill, blhill, rhill, ghill)
    static const char* hill_sprites[4] = { "bkhill.bmp", "blhill.bmp", "rhill.bmp", "ghill.bmp" };
    if (has_anthill_bases_) {
        for (int t = 0; t < 4; ++t) {
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

    // 2. Resolve Action Animation Prefix
    static const char* class_prefixes[6] = { "ag", "ab", "af", "at", "ac", "as" };
    std::string prefix = class_prefixes[static_cast<size_t>(ant.type) % 6];
    std::string action = "st"; // Default Idle

    if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Walking)) {
        action = ant.is_holding ? "ws" : (ant.is_swimming ? "sw" : "wg");
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Attacking)) {
        action = "at";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Knockback)) {
        action = "gf";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Bounce)) {
        action = "gb";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Drowning)) {
        action = "dr";
    }

    ants::assets::Direction dir = static_cast<ants::assets::Direction>(ant.facing & 7);
    const auto* seq = archive_->get_directional_animation(prefix + action, dir);

    int32_t render_y = sy - altitude_z;

    if (seq && !seq->subitems.empty()) {
        size_t sub_idx = ant.anim_frame % seq->subitems.size();
        const auto& sub = seq->subitems[sub_idx];

        for (const auto& f : sub.frames) {
            bool mirrored = ants::assets::get_direction_mapping(dir).mirrored;
            SDL_Texture* tex = texture_cache_->get_sprite_texture(f.sprite_index, mirrored, static_cast<uint8_t>(ant.player_id));
            if (!tex) continue;

            const auto& sp = mirrored ? archive_->get_mirrored_sprite(f.sprite_index)
                                      : archive_->get_sprite(f.sprite_index);
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

    // 3. Selection Indicator (Yellow Box encompassing the full ant height 40px, head at render_y - 28)
    if (is_selected) {
        SDL_SetRenderDrawColor(renderer_, 255, 255, 0, 255); // Yellow Selection Ring
        SDL_Rect sel_box = { sx - 14, render_y - 29, 28, 41 };
        SDL_RenderDrawRect(renderer_, &sel_box);
    }

    // 4. Overhead Health Bar (Drawn if selected OR if global unit health display is ON)
    if (is_selected || show_health_bar) {
        // Health Bar (10 HP standard, 24x4 px anchored at sx-12, render_y-35 above the ant's head)
        SDL_Rect bar_bg = { sx - 12, render_y - 35, 24, 4 };
        SDL_SetRenderDrawColor(renderer_, 40, 40, 40, 200);
        SDL_RenderFillRect(renderer_, &bar_bg);

        int hp_w = (ant.max_hp > 0) ? (ant.hp * 24) / ant.max_hp : 0;
        SDL_Rect bar_fg = { sx - 12, render_y - 35, hp_w, 4 };
        if (ant.hp > 6) SDL_SetRenderDrawColor(renderer_, 50, 220, 50, 255);
        else if (ant.hp > 3) SDL_SetRenderDrawColor(renderer_, 230, 200, 30, 255);
        else SDL_SetRenderDrawColor(renderer_, 230, 40, 40, 255);
        SDL_RenderFillRect(renderer_, &bar_fg);
    }
}

void Renderer::render_ant_units(const ants::sim::WorldState& world,
                                int32_t selected_unit_id,
                                const std::vector<uint32_t>& selected_unit_ids,
                                bool show_all_health_bars) {
    for (const auto& a : world.ants) {
        if (a.is_underground) continue;

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
                SDL_SetRenderDrawColor(renderer_, 25, 75, 150, 255);
            } else if (cell.terrain_type == ants::sim::TERRAIN_OBSTACLE) {
                SDL_SetRenderDrawColor(renderer_, 45, 42, 38, 255);
            } else if (cell.is_mud) {
                SDL_SetRenderDrawColor(renderer_, 65, 65, 65, 255);
            } else {
                SDL_SetRenderDrawColor(renderer_, 135, 120, 110, 255);
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
            { 200, 200, 220, 255 }, // Black team light dot
            { 120, 180, 255, 255 }, // Blue
            { 255, 70, 70, 255 },   // Red
            { 90, 240, 90, 255 }    // Green
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

    // Top Bar (640x22)
    SDL_Texture* top_tex = texture_cache_->get_named_sprite_texture("x0y0.bmp");
    if (top_tex) {
        SDL_Rect top_rect = { 0, 0, 640, 22 };
        SDL_RenderCopy(renderer_, top_tex, nullptr, &top_rect);
    }

    // Left Border (17x458)
    SDL_Texture* left_tex = texture_cache_->get_named_sprite_texture("x0y22.bmp");
    if (left_tex) {
        SDL_Rect left_rect = { 0, 22, 17, 458 };
        SDL_RenderCopy(renderer_, left_tex, nullptr, &left_rect);
    }

    // Right Divider (22x426)
    SDL_Texture* div_tex = texture_cache_->get_named_sprite_texture("x458y35.bmp");
    if (div_tex) {
        SDL_Rect div_rect = { 458, 35, 22, 426 };
        SDL_RenderCopy(renderer_, div_tex, nullptr, &div_rect);
    }

    // Bottom News Banner (623x19)
    SDL_Texture* bot_tex = texture_cache_->get_named_sprite_texture("x17y461.bmp");
    if (bot_tex) {
        SDL_Rect bot_rect = { 17, 461, 623, 19 };
        SDL_RenderCopy(renderer_, bot_tex, nullptr, &bot_rect);
    }

    // Selection Card Backing (160x128)
    SDL_Texture* card_tex = texture_cache_->get_named_sprite_texture("x480y126.bmp");
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
        SDL_RenderPresent(renderer_);
    }
}

// ============================================================================
// IRenderer Interface Implementations
// ============================================================================

void Renderer::draw_sprite(uint32_t sprite_id, int32_t x, int32_t y, bool mirrored) {
    if (!renderer_ || !texture_cache_ || !archive_) return;
    SDL_Texture* tex = texture_cache_->get_sprite_texture(sprite_id, mirrored);
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
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(renderer_, &rect);
}

void Renderer::draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) {
    if (!renderer_) return;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(renderer_, &rect);
}

void Renderer::draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color) {
    if (!renderer_) return;
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
        const uint8_t* glyph = FONT_8X8[glyph_idx];

        for (int row = 0; row < 8; ++row) {
            uint8_t row_bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                if (row_bits & (0x80 >> col)) {
                    SDL_RenderDrawPoint(renderer_, cur_x + col, cur_y + row);
                }
            }
        }
        cur_x += 8;
    }
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
