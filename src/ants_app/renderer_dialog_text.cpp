// The sentences of the new windows (renderer.hpp, dialog_text_ready): the HUD art's face, Libre Baskerville, drawn like the rest of the text (opened at twice the size, halved into the canvas by
// the linear filter) with a pale edge one pixel below and right of the letters, as if they were pressed into the clay.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

#include "ants_app/renderer.hpp"

namespace ants::app {

#ifdef ANTS_ENABLE_SDL_TTF

namespace {
constexpr int kMaxDialogLines = 96;          // the pictures of lines kept (a window shows a handful; the sentences with names change now and then)
}

void Renderer::set_dialog_font_path(const std::string& path) {
    if (path == dialog_path_) return;
    dialog_path_ = path;
    dialog_face_ok_ = -1;
    drop_dialog_fonts();
}

void Renderer::drop_dialog_fonts() {
    for (auto& pair : dialog_cache_) {
        if (pair.second.texture) SDL_DestroyTexture(pair.second.texture);
    }
    dialog_cache_.clear();
    for (auto& pair : dialog_fonts_) {
        if (pair.second) TTF_CloseFont(pair.second);
    }
    dialog_fonts_.clear();
}

TTF_Font* Renderer::dialog_font(int32_t em_tenths) const {
    if (!ttf_initialized_ || dialog_path_.empty() || dialog_face_ok_ == 0 || em_tenths <= 0) return nullptr;
    const auto it = dialog_fonts_.find(em_tenths);
    if (it != dialog_fonts_.end()) return it->second;
    TTF_Font* font = TTF_OpenFont(dialog_path_.c_str(), std::max(2, static_cast<int>(std::lround(em_tenths * 2.0 / 10.0))));
    if (font == nullptr) {
        if (dialog_face_ok_ < 0) {
            dialog_face_ok_ = 0;
            std::cerr << "[Renderer] The windows' face " << dialog_path_ << " cannot be opened: the original's text is drawn" << std::endl;
        }
        return nullptr;
    }
    dialog_face_ok_ = 1;
    dialog_fonts_[em_tenths] = font;
    return font;
}

bool Renderer::dialog_text_ready() const {
    return dialog_font(200) != nullptr;
}

int32_t Renderer::dialog_text_width(const std::string& text, int32_t em_tenths) const {
    if (text.empty()) return 0;
    TTF_Font* font = dialog_font(em_tenths);
    int w = 0, h = 0;
    if (font == nullptr || TTF_SizeUTF8(font, text.c_str(), &w, &h) != 0) return 0;
    return (w + 1) / 2;
}

void Renderer::draw_dialog_text(const std::string& text, int32_t x, int32_t baseline, ants::assets::ColorRGBA color, int32_t em_tenths) {
    if (!renderer_ || text.empty()) return;
    TTF_Font* font = dialog_font(em_tenths);
    if (font == nullptr) return;
    const int32_t top = baseline - (TTF_FontAscent(font) + 1) / 2;
    // the ink is drawn three times, a third of a pixel apart, which thickens the thin strokes of the face by about that much (the drawn lettering has a stroke of 0.03 em round it)
    const auto draw = [&](ants::assets::ColorRGBA c, float dx, float dy) {
        const std::string key = text + '\x1f' + std::to_string(em_tenths) + ':' + std::to_string((static_cast<uint32_t>(c.r) << 24) | (static_cast<uint32_t>(c.g) << 16) | (static_cast<uint32_t>(c.b) << 8) | c.a);
        auto it = dialog_cache_.find(key);
        if (it == dialog_cache_.end()) {
            if (dialog_cache_.size() >= static_cast<size_t>(kMaxDialogLines)) {
                for (auto& pair : dialog_cache_) {
                    if (pair.second.texture) SDL_DestroyTexture(pair.second.texture);
                }
                dialog_cache_.clear();
            }
            SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), SDL_Color{c.r, c.g, c.b, 255});
            if (surf == nullptr) return;
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            const int32_t lw = (surf->w + 1) / 2;
            const int32_t lh = (surf->h + 1) / 2;
            SDL_FreeSurface(surf);
            if (tex == nullptr) return;
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(tex, c.a);
            it = dialog_cache_.emplace(key, CachedTextEntry{tex, lw, lh, 0}).first;
        }
        const SDL_Rect at = placed(x, top, it->second.width, it->second.height);
        const SDL_FRect dst{static_cast<float>(at.x) + dx, static_cast<float>(at.y) + dy, static_cast<float>(at.w), static_cast<float>(at.h)};
        if (alpha_scale_ != 255) SDL_SetTextureAlphaMod(it->second.texture, scaled_alpha(c.a));      // (the countdown card, see-through)
        SDL_RenderCopyF(renderer_, it->second.texture, nullptr, &dst);
        if (alpha_scale_ != 255) SDL_SetTextureAlphaMod(it->second.texture, c.a);
    };
    draw(ants::assets::ColorRGBA{255, 196, 147, 46}, 1.0f, 1.0f);
    draw(color, 0.0f, 0.0f);
    draw(color, 0.35f, 0.0f);
    draw(color, 0.0f, 0.35f);
}

#else

void Renderer::set_dialog_font_path(const std::string& path) { dialog_path_ = path; }
bool Renderer::dialog_text_ready() const { return false; }
int32_t Renderer::dialog_text_width(const std::string&, int32_t) const { return 0; }
void Renderer::draw_dialog_text(const std::string&, int32_t, int32_t, ants::assets::ColorRGBA, int32_t) {}

#endif

}  // namespace ants::app
