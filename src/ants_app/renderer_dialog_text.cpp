// The sentences of the new windows (renderer.hpp, dialog_text_ready): the HUD art's face, Libre Baskerville, drawn like the rest of the text (opened at twice the size, halved into the canvas by
// the linear filter) with a pale edge one pixel below and right of the letters, as if they were pressed into the clay. A text with a letter that this face lacks (a nickname in Cyrillic: the face has Latin only)
// is drawn in the game's own face (face_font), whole, with no pale edge, and measured in it.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

#include "ants_app/renderer.hpp"

namespace ants::app {

#ifdef ANTS_ENABLE_SDL_TTF

namespace {
constexpr int kMaxDialogLines = 96;          // the pictures of lines kept (a window shows a handful; the sentences with names change now and then)

// Whether a face draws every letter of a text (UTF-8). The windows' face has Latin only, and a nickname may be Cyrillic: a letter that a face lacks is drawn as its box, which is no letter.
// A letter beyond the first 65,536 or a text that is no UTF-8 counts as lacking.
bool face_draws_all(TTF_Font* font, const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        size_t n = 0;
        if (lead < 0x80) { cp = lead; n = 1; }
        else if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1Fu; n = 2; }
        else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0Fu; n = 3; }
        else return false;
        if (i + n > text.size()) return false;
        for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
        if (cp >= 0x20 && TTF_GlyphIsProvided(font, static_cast<Uint16>(cp)) == 0) return false;
        i += n;
    }
    return true;
}
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
    for (auto& pair : face_cache_) {
        if (pair.second.texture) SDL_DestroyTexture(pair.second.texture);
    }
    face_cache_.clear();
    for (auto& pair : face_fonts_) {
        if (pair.second) TTF_CloseFont(pair.second);
    }
    face_fonts_.clear();
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
    if (font != nullptr && !face_draws_all(font, text) && face_font(em_tenths) != nullptr) return face_text_width(text, em_tenths);       // (a name in letters that the windows' face lacks: the game's face, which has them)
    int w = 0, h = 0;
    if (font == nullptr || TTF_SizeUTF8(font, text.c_str(), &w, &h) != 0) return 0;
    return (w + 1) / 2;
}

void Renderer::draw_dialog_text(const std::string& text, int32_t x, int32_t baseline, ants::assets::ColorRGBA color, int32_t em_tenths) {
    if (!renderer_ || text.empty()) return;
    TTF_Font* font = dialog_font(em_tenths);
    if (font == nullptr) return;
    if (!face_draws_all(font, text) && face_font(em_tenths) != nullptr) { draw_face_text(text, x, baseline, color, em_tenths); return; }       // (as dialog_text_width)
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

// The game's own face at any size (renderer.hpp, face_text_ready): the face of draw_text opened at twice the size and halved into the canvas, as the dialog's face is, without its pale edge
TTF_Font* Renderer::face_font(int32_t em_tenths) const {
    if (!ttf_initialized_ || ttf_path_.empty() || em_tenths <= 0) return nullptr;
    const auto it = face_fonts_.find(em_tenths);
    if (it != face_fonts_.end()) return it->second;
    TTF_Font* font = TTF_OpenFont(ttf_path_.c_str(), std::max(2, static_cast<int>(std::lround(em_tenths * 2.0 / 10.0))));
    face_fonts_[em_tenths] = font;
    return font;
}

bool Renderer::face_text_ready() const {
    return face_font(200) != nullptr;
}

int32_t Renderer::face_text_width(const std::string& text, int32_t em_tenths) const {
    if (text.empty()) return 0;
    TTF_Font* font = face_font(em_tenths);
    int w = 0, h = 0;
    if (font == nullptr || TTF_SizeUTF8(font, text.c_str(), &w, &h) != 0) return 0;
    return (w + 1) / 2;
}

void Renderer::draw_face_text(const std::string& text, int32_t x, int32_t baseline, ants::assets::ColorRGBA color, int32_t em_tenths) {
    if (!renderer_ || text.empty()) return;
    TTF_Font* font = face_font(em_tenths);
    if (font == nullptr) return;
    const std::string key = text + '\x1f' + std::to_string(em_tenths) + ':' + std::to_string((static_cast<uint32_t>(color.r) << 24) | (static_cast<uint32_t>(color.g) << 16) | (static_cast<uint32_t>(color.b) << 8));
    auto it = face_cache_.find(key);
    if (it == face_cache_.end()) {
        if (face_cache_.size() >= static_cast<size_t>(kMaxDialogLines)) {
            for (auto& pair : face_cache_) {
                if (pair.second.texture) SDL_DestroyTexture(pair.second.texture);
            }
            face_cache_.clear();
        }
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), SDL_Color{color.r, color.g, color.b, 255});
        if (surf == nullptr) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
        const int32_t lw = (surf->w + 1) / 2;
        const int32_t lh = (surf->h + 1) / 2;
        SDL_FreeSurface(surf);
        if (tex == nullptr) return;
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        it = face_cache_.emplace(key, CachedTextEntry{tex, lw, lh, 0}).first;
    }
    const int32_t top = baseline - (TTF_FontAscent(font) + 1) / 2;
    const SDL_Rect dst = placed(x, top, it->second.width, it->second.height);
    SDL_SetTextureAlphaMod(it->second.texture, scaled_alpha(color.a));
    SDL_RenderCopy(renderer_, it->second.texture, nullptr, &dst);
}

#else

void Renderer::set_dialog_font_path(const std::string& path) { dialog_path_ = path; }
bool Renderer::dialog_text_ready() const { return false; }
int32_t Renderer::dialog_text_width(const std::string&, int32_t) const { return 0; }
void Renderer::draw_dialog_text(const std::string&, int32_t, int32_t, ants::assets::ColorRGBA, int32_t) {}
bool Renderer::face_text_ready() const { return false; }
int32_t Renderer::face_text_width(const std::string&, int32_t) const { return 0; }
void Renderer::draw_face_text(const std::string&, int32_t, int32_t, ants::assets::ColorRGBA, int32_t) {}

#endif

}  // namespace ants::app
