#include "ants_app/renderer.hpp"
#include "ants_app/canvas_layout.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstdio>
#include <cctype>

namespace ants::app {

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
// of the view's left edge; what the map does not cover stays the color that the frame starts with, black) and at 0 otherwise. At the zoom 1 this is the camera's own arithmetic as it always
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
    if (team_id >= ANT_COLOR_BASE && team_id < ANT_COLOR_BASE + 4) {
        // Original ant blit (Ants.exe FUN_0102cfef, remap enabled by FUN_0101b802): the color offset is added to
        // every non-transparent pixel index; the callback at 0x101b7eb exempts image names starting with a digit.
        // Offsets by remake player id 0..3 (green, red, blue, black) = {60, 40, 20, 0}.
        static constexpr uint8_t kOffset[4] = { 60, 40, 20, 0 };
        const bool exempt = !image_name.empty() && std::isdigit(static_cast<unsigned char>(image_name[0])) != 0;
        const uint8_t offset = kOffset[team_id - ANT_COLOR_BASE];
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
    // color shift); only the planted bomb tiles have team art.
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


    // Convert 8-bit paletted sprite to 32-bit RGBA under the requested color mode
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
                static constexpr FontSize kSizes[kFontSizes] = {FontSize::Px12, FontSize::Px14, FontSize::Px18, FontSize::Px20, FontSize::Px24, FontSize::Px35, FontSize::Px96, FontSize::Px27};
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
    for (ChangePicture& pic : change_pictures_) {
        if (pic.texture) SDL_DestroyTexture(pic.texture);
    }
    change_pictures_.clear();
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

        // Editor start markers (the ids 152 .. 155) are never drawn in game
        const std::string low = lower(name);
        if (ants::assets::start_marker_team_of_id(static_cast<uint16_t>(i)) >= 0) continue;

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
    for (size_t t = 0; t < 4; ++t) {
        anthill_bases_[t] = { -1, -1 };
        hill_anchor_cell_[t] = { -1, -1 };
    }
    has_anthill_bases_ = false;
    for (uint32_t y = 0; y < level.height; ++y) {
        for (uint32_t x = 0; x < level.width; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            const int team = ants::assets::hill_team_of_id(c2.tile_index);     // a hill is its tile id (245 .. 248), whatever the dictionary calls the entry
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
            // cell.has_powerup() (below), the same sprite of the same kind on every map; a file bomb (129 .. 132) likewise, via cell.has_bomb() (docs/reverse_engineering/map_file_bombs.md)
            if (ants::sim::movement::is_powerup_tile(c2.tile_index) || ants::sim::is_original_bomb_tile(c2.tile_index)) continue;
            // A fire wall (134) is likewise drawn exclusively through cell.has_fire() (below): the map maker names the entry `wallup04`, and a static object made from that name would stay on the
            // screen for ever after a fire ant put the fire out (docs/reverse_engineering/map_loader_fidelity.md, 7)
            if (c2.tile_index == ants::sim::TILE_FIREWALL) continue;
            // anthills are drawn from their own templates, start markers never; both are tile ids (245 .. 248, 152 .. 155), whatever the dictionary calls them
            if (ants::assets::hill_team_of_id(c2.tile_index) >= 0 || ants::assets::start_marker_team_of_id(c2.tile_index) >= 0) continue;
            if (c2.tile_index >= level.tile_dictionary.size()) continue;

            const std::string& tname = level.tile_dictionary[c2.tile_index];
            if (tname.empty() || tname == ".") continue;

            const std::string low = lower(tname);
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

            obj.is_food = (ants::sim::movement::tile_flags_of(c2.tile_index) & ants::sim::movement::kTileFlagFood) != 0;      // food is the tile flag 0x02 of the id (FUN_010071dd), not a name

            object_index_by_cell_[static_cast<size_t>(y) * level.width + x] = static_cast<int32_t>(static_decor_objects_.size());
            static_decor_objects_.push_back(std::move(obj));
        }
    }

    // Every food object of block 2 (FUN_01008ca0) puts its stage tile on its anchor with SetTile, whatever the file's layer 2 holds there (Grid::init_from_level does the same), and the
    // original draws what the cells hold: so each has a static object at its anchor, drawn from the live cells (draw_static_object), in place of a file object there (SetTile cleared it).
    for (const auto& fs : level.food_schedules) {
        if (fs.x >= level.width || fs.y >= level.height) continue;
        int32_t& slot = object_index_by_cell_[static_cast<size_t>(fs.y) * level.width + fs.x];
        if (slot < 0) { slot = static_cast<int32_t>(static_decor_objects_.size()); static_decor_objects_.emplace_back(); }
        static_decor_objects_[static_cast<size_t>(slot)] = StaticMapObject{-1, fs.x, fs.y, true};   // anim_id -1: the anchor's stage tile names the animation
    }

    // Object-list sprites (Block 1 entries with team 255: clovers, flowers, tree tops ...). The original puts them in
    // the y-sorted sprite list at the cell centre with sort key row*32+16 (Ants.exe 0x100e383..0x100e448).
    object_list_sprites_.clear();
    for (const auto& sp_item : level.anthill_spawns) {
        if (sp_item.team_id != 255) continue;
        // A plant (the clovers 404 .. 408, the flowers 410 .. 416, 420 and 421, ...) is its tile id, which indexes its animation directly, whatever the dictionary calls the entry
        // ("." in some community maps): the original's name pass makes the graphics of these ids load whatever the dictionary says (the table at 0x1001ba8), so the flowers of a
        // dropper are always there to see. Every other record of block 1 is ignored by the original (FUN_0100dbb0 makes world objects of the records with the tile flag 0x10 or 0x20
        // only): it is not drawn, whatever the dictionary calls its entry.
        if (!ants::sim::movement::is_plant_object_tile(sp_item.tile_id) || sp_item.tile_id >= anim_count) continue;
        const int32_t anim_id = static_cast<int32_t>(sp_item.tile_id);
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
    int32_t side = texture_side_override_;
    if (side <= 0) {
        SDL_RendererInfo info{};
        if (SDL_GetRendererInfo(renderer_, &info) != 0) return 0;
        side = std::min(info.max_texture_width, info.max_texture_height);
    }
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
    const zoom::Pass plan = zoom::plan(static_cast<double>(camera_.x), static_cast<double>(camera_.y), camera_.zoom, view.w, view.h, map_w_px, map_h_px, zoom::kSmoothUpscale);
    if (plan.w <= 0 || plan.h <= 0) return false;                 // (nothing of the map is in the view: the zoom 1 picture of the same origin, which is that as well)
    const bool direct_zoom_out = !software_ && camera_.zoom < zoom::kNormal;
    const int needed_depth = direct_zoom_out ? 0 : plan.depth;
    for (size_t k = static_cast<size_t>(needed_depth) + 1; k < pass_levels_.size(); ++k) destroy_texture(pass_levels_[k]);
    pass_levels_.resize(static_cast<size_t>(needed_depth) + 1);
    for (int k = 0; k <= needed_depth; ++k) {
        if (!ensure_texture(pass_levels_[static_cast<size_t>(k)], plan.cap_w >> k, plan.cap_h >> k, true)) return false;
    }
    if (!direct_zoom_out && !ensure_texture(lattice_, view.w + kLatticeMargin, view.h + kLatticeMargin, true)) return false;
    if (SDL_SetRenderTarget(renderer_, pass_levels_[0].texture) != 0) { note_world_target_error(SDL_GetError()); return false; }
    // The pass's own state: what is replaced is kept in pass_ and put back by end_world_target
    pass_ = TargetPass{camera_, picture_, picture_inset_, origin_};
    plan_ = plan;
    camera_.zoom = zoom::kNormal;                              // (a camera of the zoom 1 whose view is the target: the world code's numbers are texels)
    camera_.x = static_cast<float>(plan.x0); camera_.y = static_cast<float>(plan.y0);
    camera_.world_x = plan.x0; camera_.world_y = plan.y0;
    camera_.set_view(LayoutRect{0, 0, plan.w, plan.h});
    target_view_ = LayoutRect{0, 0, plan.w, plan.h};
    picture_ = target_view_; picture_inset_ = false; origin_ = LayoutPoint{};
    in_world_target_ = true; ++world_target_passes_;
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);           // (the black that the frame starts with: what the map does not cover)
    SDL_RenderClear(renderer_);
    return true;
}

// The pass ends. On a hardware window below zoom 1, the 1x world texture is blitted directly to the window viewport with nearest-neighbor sampling so high-DPI / fullscreen windows keep full native resolution without intermediate 762x500 down-res or linear blur. Otherwise the world is halved as often as the plan says, scaled into the lattice of whole screen pixels, and cut out into the view.
void Renderer::end_world_target() {
    if (!in_world_target_) return;
    in_world_target_ = false;
    camera_ = pass_.camera; picture_ = pass_.picture; picture_inset_ = pass_.picture_inset; origin_ = pass_.origin;
    const zoom::Pass& p = plan_;
    const LayoutRect view = layout_.view();
    if (!software_ && camera_.zoom < zoom::kNormal) {
        SDL_SetRenderTarget(renderer_, nullptr);
        const SDL_Rect clip = placed(view.x, view.y, view.w, view.h);
        SDL_RenderSetClipRect(renderer_, &clip);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer_, &clip);
        const double z = p.scale / static_cast<double>(1 << p.depth);
        const SDL_Rect src{0, 0, p.w, p.h};
        const SDL_FRect dst{static_cast<float>(clip.x + p.left), static_cast<float>(clip.y + p.top), static_cast<float>(p.w * z), static_cast<float>(p.h * z)};
        SDL_SetTextureScaleMode(pass_levels_[0].texture, SDL_ScaleModeNearest);
        SDL_RenderCopyF(renderer_, pass_levels_[0].texture, &src, &dst);
        return;
    }
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
    health_marks_.clear(), deferred_digits_.clear();

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
    if (!target) draw_health_marks();                 // (a zoom 1 pass: over everything of the world; at a zoom they come after the copy)

    // 5. Tile Grid Overlay (if enabled): on the screen, after the copy of a zoomed world
    if (show_tile_grid && !target) {
        render_tile_grid(grid, mouse_x, mouse_y);
    }

    // 6. Unset clipping for full-picture chrome
    restore_clip();

    if (target) {
        end_world_target();                       // the copy into the view (clipped to it)
        draw_health_marks();
        if (show_tile_grid) render_tile_grid(grid, mouse_x, mouse_y);
        restore_clip();
    }
    camera_.zoom = zoom_kept;
}

// ============================================================================
// Template animation helpers (terrain, layer-2 objects, object-list sprites)
// ============================================================================

void Renderer::draw_frame_parts(const ants::assets::AnimationSubItem& sub, int32_t sx, int32_t sy, bool mirrored,
                                uint8_t color) {
    if (!archive_ || !texture_cache_ || !renderer_) return;
    // A mirrored part is written one pixel to the right of its own origin (Ants.exe part blitter 0x102cfef, mirrored
    // inner loop at 0x102d12c: the row starts at dest + width and runs backwards), which makes the reflection exact
    // about the anchor column: dx' = -dx - width, columns dx'+1 .. dx'+width.
    const int32_t mirror_shift = mirrored ? 1 : 0;
    // Original order (see frame_part_draw_order): the last stored part is drawn first, the first stored part on top.
    for (size_t k = sub.frames.size(); k-- > 0;) {
        const auto& f = sub.frames[k];
        const uint32_t sp_idx = f.sprite_index;
        SDL_Texture* tex = texture_cache_->get_sprite_texture(sp_idx, mirrored, color);
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

void Renderer::draw_template_screen(int32_t anim_id, int32_t sx, int32_t sy, uint8_t color) {
    if (!archive_ || anim_id < 0 || static_cast<size_t>(anim_id) >= archive_->animation_count()) return;
    const auto& seq = archive_->get_animation(static_cast<uint32_t>(anim_id));
    if (seq.subitems.empty()) return;
    const size_t fi = std::min(template_frame_index(anim_id), seq.subitems.size() - 1);
    draw_frame_parts(seq.subitems[fi], sx, sy, false, color);
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

void Renderer::draw_template_world(int32_t anim_id, int32_t world_x, int32_t world_y, uint8_t color) {
    const AnimBounds& b = anim_bounds(anim_id);
    if (!b.valid) return;
    const int32_t cam_x = static_cast<int32_t>(camera_.x);
    const int32_t cam_y = static_cast<int32_t>(camera_.y);
    const LayoutRect view = world_view();
    if (world_x + b.x1 <= cam_x || world_x + b.x0 >= cam_x + view.w ||
        world_y + b.y1 <= cam_y || world_y + b.y0 >= cam_y + view.h) {
        return;
    }
    draw_template_screen(anim_id, view.x + (world_x - cam_x), view.y + (world_y - cam_y), color);
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
