// The world art on the screen (world_art.hpp; docs/WORLD_ART.md): the sheet of the world pass's scale is a texture, and a new picture is a copy of its part of the sheet into the rectangle that the original's
// sprite would fill. Which picture of a plain ground tile a cell shows is chosen once for the map (world_art::ground_variants).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "ants_app/renderer.hpp"

namespace ants::app {

namespace {

// The ground tiles of the original: the animations 431 .. 669 (docs/chd_table4_animations.json), the plain tiles, their edges and the bubbling mud
constexpr uint32_t kFirstGroundTile = 431;
constexpr uint32_t kLastGroundTile = 669;

}  // namespace

void Renderer::drop_world_art_textures() {
    for (SDL_Texture*& t : world_art_textures_) {
        if (t) SDL_DestroyTexture(t);
        t = nullptr;
    }
    world_art_failed_.fill(false);
    world_art_generation_ = 0;
    drop_effect_textures();
}

void Renderer::set_world_art(const world_art::Pack* pack) {
    world_art_ = (pack != nullptr && pack->loaded()) ? pack : nullptr;
    if (world_art_ == nullptr || world_art_generation_ != world_art_->generation()) {
        drop_world_art_textures();
        drop_mud_caches();
        for (auto& pictures : mud_pictures_) pictures.clear();
        mud_pictures_state_.fill(0);
        drop_edge_caches();
        for (auto& pictures : edge_pictures_) {
            for (auto& picture : pictures) picture.clear();
        }
        edge_pictures_state_.fill(0);
    }
}

size_t Renderer::world_art_textures() const noexcept {
    size_t n = 0;
    for (SDL_Texture* t : world_art_textures_) n += t != nullptr ? 1u : 0u;
    return n;
}

uint32_t Renderer::world_art_sheets() const noexcept {
    uint32_t mask = 0;
    for (size_t k = 0; k < world_art_textures_.size(); ++k) mask |= world_art_textures_[k] != nullptr ? (1u << k) : 0u;
    return mask;
}

// A zoomed pass draws at the world's own pixels (one texel for each, scaled afterwards: begin_world_target), so it uses the 1x sheet; the zoom 1 picture is drawn straight into the output, at the scale of
// the output (the same rule as the HUD art's sheets). (Inside the pass SDL measures the target as the output, which can be larger than the canvas when the world is zoomed out: it must not choose.)
int32_t Renderer::world_art_scale_now() const {
    return in_world_target_ ? 1 : art_scale_for_output(nullptr);
}

// The texture of a sheet: read and decoded from its file once (the pixels are not kept), made static with the alpha blend. Null when the file cannot be read or decoded, or the device cannot make the
// texture (said once on stderr; the caller then tries a sheet of a smaller scale and in the end draws the original's sprite)
SDL_Texture* Renderer::world_art_texture(int32_t scale) {
    if (world_art_ == nullptr || scale < 1 || scale > 3) return nullptr;
    if (world_art_generation_ != world_art_->generation()) {
        drop_world_art_textures();
        world_art_generation_ = world_art_->generation();
    }
    const size_t k = static_cast<size_t>(scale - 1);
    if (world_art_textures_[k]) return world_art_textures_[k];
    if (world_art_failed_[k]) return nullptr;
    world_art_failed_[k] = true;                                                // (until it works: a sheet that fails is not read again at every picture of every frame)
    DecodedImage image;
    if (!world_art_->sheet_pixels(scale, image)) {
        std::cerr << "[Renderer] The world art's sheet " << world_art_->sheet_file(scale) << " cannot be read" << std::endl;
        return nullptr;
    }
    if (art_texture_limit_ > 0 && (image.width > art_texture_limit_ || image.height > art_texture_limit_)) {
        std::cerr << "[Renderer] The world art's sheet " << world_art_->sheet_file(scale) << " is larger than a texture of this device" << std::endl;
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
    if (!tex) {
        std::cerr << "[Renderer] The world art's sheet " << world_art_->sheet_file(scale) << " cannot be made a texture: " << SDL_GetError() << std::endl;
        return nullptr;
    }
    SDL_UpdateTexture(tex, nullptr, image.rgba.data(), image.width * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    world_art_failed_[k] = false;
    world_art_textures_[k] = tex;
    return tex;
}

// One world pixel is `scale` pixels of the sheet. A copy at the output's own scale is exact (nearest); a sheet that is finer than the output is filtered, so that it does not shimmer.
void Renderer::world_art_filter(SDL_Texture* tex, int32_t scale) {
    double output = 1.0;
    art_scale_for_output(&output);
    const double device = in_world_target_ ? 1.0 : output;
    SDL_SetTextureScaleMode(tex, std::fabs(device - static_cast<double>(scale)) < 1e-6 ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
}

// Draws the new picture of sprite `sprite_index` into the rectangle (x, y, w, h) of the picture, the one the original's sprite of that size fills; false when the set has none that fits (another size
// than the sprite's times the scale: the sprite is drawn as the original has it) or no sheet can be used
bool Renderer::draw_world_art_part(uint32_t sprite_index, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (world_art_ == nullptr || !renderer_) return false;
    const world_art::Picture* pic = world_art_->find(sprite_index, world_variant_);
    if (pic == nullptr || pic->w != w || pic->h != h) return false;
    int32_t scale = world_art_scale_now();
    SDL_Texture* tex = nullptr;
    for (; scale >= 1 && tex == nullptr; --scale) tex = world_art_texture(scale);        // (a sheet that cannot be had: the next smaller one)
    ++scale;
    if (tex == nullptr) return false;
    world_art_filter(tex, scale);
    const size_t k = static_cast<size_t>(scale - 1);
    const SDL_Rect src = {pic->x[k], pic->y[k], w * scale, h * scale};
    const SDL_Rect dst = placed(x, y, w, h);
    SDL_RenderCopy(renderer_, tex, &src, &dst);
    return true;
}

// What the choice of a ground tile's picture reads from a map: the kind of each ground tile (the first letter of its animation's name), whether it is a plain one, and whether an object stands on the cell
void Renderer::index_ground(const ants::assets::LevelData& level) {
    ground_map_ = world_art::GroundMap{};
    ground_variant_.clear();
    mud_depth_ = world_art::mud::Depth{};
    mud_depth_ready_ = false;
    drop_mud_caches();
    edge_map_ = world_art::edges::EdgeMap{};
    edge_has_.clear();
    edge_ready_ = false;
    drop_edge_caches();
    const int32_t w = static_cast<int32_t>(level.width());
    const int32_t h = static_cast<int32_t>(level.height());
    if (w <= 0 || h <= 0 || !archive_) return;
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    ground_map_.width = w;
    ground_map_.height = h;
    ground_map_.kind.assign(n, 0);
    ground_map_.plain.assign(n, 0);
    ground_map_.object.assign(n, 0);
    const size_t anim_count = archive_->animation_count();
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            const size_t at = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            const uint32_t id = level.get_cell_layer1(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).tile_index;
            if (id >= kFirstGroundTile && id <= kLastGroundTile && id < anim_count) {
                const std::string& name = archive_->get_animation(id).name;
                if (!name.empty()) ground_map_.kind[at] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
                ground_map_.plain[at] = (name.size() >= 3 && name[1] == '0' && name[2] == '1') ? 1 : 0;
            }
            ground_map_.object[at] = level.get_cell_layer2(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).tile_index != ants::assets::LVL_EMPTY_TILE ? 1 : 0;
        }
    }
}

void Renderer::ensure_ground_variants() {
    if (!ground_variant_.empty() || ground_map_.kind.empty()) return;
    ground_variant_ = world_art::ground_variants(ground_map_);
}

uint8_t Renderer::ground_variant_at(int32_t col, int32_t row) {
    ensure_ground_variants();
    if (col < 0 || row < 0 || col >= ground_map_.width || row >= ground_map_.height) return 0;
    const size_t at = static_cast<size_t>(row) * static_cast<size_t>(ground_map_.width) + static_cast<size_t>(col);
    return at < ground_variant_.size() ? ground_variant_[at] : uint8_t{0};
}

}  // namespace ants::app
