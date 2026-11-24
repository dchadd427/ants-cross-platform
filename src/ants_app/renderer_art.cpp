// The HUD art on the screen (hud_art.hpp; docs/VIEW_AND_HUD.md, "HUD art"): the sheet of the output's scale is a texture, and each piece is a copy of a part of it into its rectangle of the picture.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "ants_app/canvas_layout.hpp"
#include "ants_app/hud_art.hpp"
#include "ants_app/renderer.hpp"

namespace ants::app {

void Renderer::drop_art_textures() {
    for (SDL_Texture*& t : art_textures_) {
        if (t) SDL_DestroyTexture(t);
        t = nullptr;
    }
    art_failed_.fill(false);
    art_generation_ = 0;
}

size_t Renderer::art_textures() const noexcept {
    size_t n = 0;
    for (SDL_Texture* t : art_textures_) n += t != nullptr ? 1u : 0u;
    return n;
}

// How many device pixels one canvas pixel is: the window's output against the canvas, with the arithmetic of the screenshot (CanvasLayout::fit); 1 when there is no output to measure
int32_t Renderer::art_scale_for_output(double* out) const {
    double scale = 1.0;
    if (renderer_) {
        int out_w = 0;
        int out_h = 0;
        SDL_GetRendererOutputSize(renderer_, &out_w, &out_h);
        if (out_w > 0 && out_h > 0) {
            const CanvasFit fit = CanvasLayout{canvas_w_, canvas_h_}.fit(out_w, out_h);
            if (fit.scale > 0.0) scale = fit.scale;
        }
    }
    if (out != nullptr) *out = scale;
    return hud_art::Art::scale_for(scale);
}

double Renderer::output_scale() const {
    double scale = 1.0;
    art_scale_for_output(&scale);
    return scale;
}

int32_t Renderer::art_scale_now() const {
    return art_scale_for_output(nullptr);
}

// The texture of a sheet: read and decoded from its file once (the pixels are not kept: the card has them), made static with the alpha blend. Null when the file cannot be read or decoded, or the device cannot make the texture (nothing is drawn then: Application::check_hud_art gives the HUD the original's art)
SDL_Texture* Renderer::art_texture(const hud_art::Art& art, int32_t scale, int32_t sheet) {
    if (art_generation_ != art.generation()) {
        drop_art_textures();
        art_generation_ = art.generation();
    }
    const size_t at = static_cast<size_t>(scale - 1) + (sheet == 0 ? 0u : 3u);
    SDL_Texture*& slot = art_textures_[at];
    if (slot) return slot;
    if (art_failed_[at]) return nullptr;
    art_failed_[at] = true;                                                    // (until it works: a sheet that fails is not read again at every piece of every frame)
    const std::string& file = art.sheet_file(scale, sheet);
    DecodedImage image;
    if (!art.sheet_pixels(scale, image, sheet)) {
        std::cerr << "[Renderer] The HUD art's sheet " << file << " cannot be read" << std::endl;
        return nullptr;
    }
    if (art_texture_limit_ > 0 && (image.width > art_texture_limit_ || image.height > art_texture_limit_)) {
        std::cerr << "[Renderer] The HUD art's sheet " << file << " is larger than a texture of this device" << std::endl;
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
    if (!tex) {
        std::cerr << "[Renderer] The HUD art's sheet " << file << " cannot be made a texture: " << SDL_GetError() << std::endl;
        return nullptr;
    }
    art_failed_[at] = false;
    SDL_UpdateTexture(tex, nullptr, image.rgba.data(), image.width * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    slot = tex;
    return tex;
}

bool Renderer::preload_art(const hud_art::Art& art) {
    if (!renderer_ || !art.loaded()) return false;
    const int32_t scale = art_scale_for_output(nullptr);
    return art_texture(art, scale) != nullptr && (!art.has_second_sheet() || art_texture(art, scale, 1) != nullptr);
}

void Renderer::draw_art(const hud_art::Art& art, int32_t sprite, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh) {
    if (!renderer_ || !art.loaded() || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    const hud_art::Sprite* s = art.sprite_at(sprite);
    if (s == nullptr || sx < 0 || sy < 0 || sx + sw > s->w || sy + sh > s->h) return;
    double output = 1.0;
    int32_t scale = art_scale_for_output(&output);
    if (art_scale_override_ > 0) scale = art_scale_override_;                 // (the countdown card: the sheet of its own unit)
    SDL_Texture* tex = art_texture(art, scale, s->sheet);
    if (!tex) return;
    // The sheet is as fine as the output or finer: a copy at the same scale is exact (nearest), a scaled one is filtered so that it does not shimmer. With a sheet of its own choice the copy is
    // exact when a sheet pixel lands on one device pixel (the width that the piece is drawn at, in device pixels, is the width it has in the sheet)
    const bool exact = art_scale_override_ > 0 ? std::fabs(static_cast<double>(w) * output - static_cast<double>(sw) * static_cast<double>(scale)) < 0.02 * static_cast<double>(sw) * static_cast<double>(scale)
                                               : std::fabs(output - static_cast<double>(scale)) < 1e-6;
    SDL_SetTextureScaleMode(tex, exact ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    const SDL_Rect src = {(s->sheet_x + sx) * scale, (s->sheet_y + sy) * scale, sw * scale, sh * scale};
    const SDL_Rect dst = placed(x, y, w, h);
    copy_scaled(tex, &src, &dst);                                                              // (the sheet is shared: a see-through copy puts its opacity back)
}

}  // namespace ants::app
