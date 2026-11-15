// The numbers of the game in its TrueType font (the face that draw_text uses): the hit point numbers (Ctrl+L), the damage and heal numbers and the score numbers. White (the score: the digit
// sprites' own color), a one pixel outline in near black, and drawn at the OUTPUT's resolution: the canvas is a picture that SDL's logical size scales into the window, so a glyph made at the
// canvas's size would be scaled (soft on a phone, where one canvas pixel is two or three device pixels). Here SDL's logical size is switched off for the moment that the numbers are copied
// (begin_native / end_native), the glyphs are made at the size that the output shows them (the face opened at the size whose digit is the wanted number of device pixels tall) and every number
// is placed with the canvas's own arithmetic (CanvasLayout::fit, the same that the screenshot uses). Drawing only; the old drawing stays behind Renderer::set_classic_numbers.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_app/canvas_layout.hpp"
#include "ants_app/health_changes.hpp"
#include "ants_app/health_marks.hpp"
#include "ants_app/renderer.hpp"

namespace ants::app {

Renderer::NativeFrame Renderer::begin_native() {
    NativeFrame f;
    if (!renderer_ || in_world_target_) return f;
    int out_w = 0;
    int out_h = 0;
    SDL_GetRendererOutputSize(renderer_, &out_w, &out_h);
    if (out_w <= 0 || out_h <= 0) return f;
    const CanvasFit fit = CanvasLayout{canvas_w_, canvas_h_}.fit(out_w, out_h);
    if (fit.scale <= 0.0) return f;
    f.scale = fit.scale;
    f.x = fit.viewport.x;
    f.y = fit.viewport.y;
    f.clipped = SDL_RenderIsClipEnabled(renderer_) == SDL_TRUE;
    SDL_RenderGetClipRect(renderer_, &f.clip);
    SDL_RenderSetLogicalSize(renderer_, 0, 0);
    if (f.clipped) {                                                   // (the clip is in the canvas's pixels: the output's pixels now)
        const int32_t x0 = f.x + static_cast<int32_t>(std::lround(static_cast<double>(f.clip.x) * f.scale));
        const int32_t y0 = f.y + static_cast<int32_t>(std::lround(static_cast<double>(f.clip.y) * f.scale));
        const int32_t x1 = f.x + static_cast<int32_t>(std::lround(static_cast<double>(f.clip.x + f.clip.w) * f.scale));
        const int32_t y1 = f.y + static_cast<int32_t>(std::lround(static_cast<double>(f.clip.y + f.clip.h) * f.scale));
        const SDL_Rect c{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
        SDL_RenderSetClipRect(renderer_, &c);
    } else {
        const SDL_Rect c{f.x, f.y, static_cast<int32_t>(std::lround(static_cast<double>(canvas_w_) * f.scale)), static_cast<int32_t>(std::lround(static_cast<double>(canvas_h_) * f.scale))};
        SDL_RenderSetClipRect(renderer_, &c);
    }
    f.on = true;
    return f;
}

void Renderer::end_native(const NativeFrame& f) {
    if (!f.on || !renderer_) return;
    SDL_RenderSetLogicalSize(renderer_, canvas_w_, canvas_h_);
    SDL_RenderSetClipRect(renderer_, f.clipped ? &f.clip : nullptr);
}

void Renderer::clear_number_pictures() {
    for (NumberPicture& p : number_pictures_) {
        if (p.texture) SDL_DestroyTexture(p.texture);
    }
    number_pictures_.clear();
#ifdef ANTS_ENABLE_SDL_TTF
    for (DigitFont& d : digit_fonts_) {
        if (d.fill) TTF_CloseFont(static_cast<TTF_Font*>(d.fill));
        if (d.edge) TTF_CloseFont(static_cast<TTF_Font*>(d.edge));
    }
#endif
    digit_fonts_.clear();
}

#ifdef ANTS_ENABLE_SDL_TTF
namespace {

// The text as the font is asked for it: the minus sign is the font's own U+2212 where it has one (the hyphen is too short beside a plus)
std::string font_text(const std::string& text, TTF_Font* font) {
    std::string out;
    const bool has_minus = TTF_GlyphIsProvided(font, 0x2212) != 0;
    for (char c : text) {
        if (c == '-' && has_minus) out += "\xE2\x88\x92";
        else out += c;
    }
    return out;
}

uint16_t first_code(const std::string& text) {
    if (!text.empty() && text[0] == '-') return 0x2212;
    return text.empty() ? uint16_t{'0'} : static_cast<uint16_t>(static_cast<unsigned char>(text[0]));
}

}  // namespace
#endif

const Renderer::NumberPicture* Renderer::number_picture(const std::string& text, int32_t digit_px) {
#ifdef ANTS_ENABLE_SDL_TTF
    if (!renderer_ || text.empty() || ttf_path_.empty() || digit_px < 2) return nullptr;
    for (const NumberPicture& p : number_pictures_) {
        if (p.digit_px == digit_px && p.text == text) return p.texture != nullptr ? &p : nullptr;
    }
    if (number_pictures_.size() >= 256) clear_number_pictures();      // (a bound, not a working size: the texts of one match are a few dozen)
    const DigitFont* df = nullptr;
    for (const DigitFont& d : digit_fonts_) {
        if (d.digit_px == digit_px) df = &d;
    }
    if (df == nullptr) {
        if (digit_fonts_.size() >= 12) {
            clear_number_pictures();
        }
        // the point size whose digit is `digit_px` tall (measured on a probe: the glyph's height grows with the size)
        TTF_Font* probe = TTF_OpenFont(ttf_path_.c_str(), 200);
        if (probe == nullptr) return nullptr;
        int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
        TTF_GlyphMetrics(probe, '0', &minx, &maxx, &miny, &maxy, &adv);
        TTF_CloseFont(probe);
        const double per_point = static_cast<double>(maxy - miny) / 200.0;
        if (per_point <= 0.05) return nullptr;
        const int32_t point = std::max(4, static_cast<int32_t>(std::lround(static_cast<double>(digit_px) / per_point)));
        DigitFont d;
        d.digit_px = digit_px;
        d.outline = std::max(1, static_cast<int32_t>(std::lround(static_cast<double>(digit_px) / 10.0)));     // (one canvas pixel: a digit is 10 of them tall)
        TTF_Font* fill = TTF_OpenFont(ttf_path_.c_str(), point);
        TTF_Font* edge = TTF_OpenFont(ttf_path_.c_str(), point);
        if (fill == nullptr || edge == nullptr) {
            if (fill) TTF_CloseFont(fill);
            if (edge) TTF_CloseFont(edge);
            return nullptr;
        }
        TTF_SetFontOutline(edge, d.outline);
        d.fill = fill;
        d.edge = edge;
        digit_fonts_.push_back(d);
        df = &digit_fonts_.back();
    }
    TTF_Font* fill = static_cast<TTF_Font*>(df->fill);
    TTF_Font* edge = static_cast<TTF_Font*>(df->edge);
    const std::string utf8 = font_text(text, fill);
    const SDL_Color white{255, 255, 255, 255};
    const SDL_Color black{0, 0, 0, 255};
    SDL_Surface* fs = TTF_RenderUTF8_Blended(fill, utf8.c_str(), white);
    SDL_Surface* es = TTF_RenderUTF8_Blended(edge, utf8.c_str(), black);
    SDL_Surface* out = nullptr;
    NumberPicture pic;
    pic.text = text;
    pic.digit_px = digit_px;
    if (fs != nullptr && es != nullptr && es->w >= fs->w && es->h >= fs->h) {
        out = SDL_CreateRGBSurfaceWithFormat(0, es->w, es->h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (out != nullptr) {
            SDL_FillRect(out, nullptr, 0);
            SDL_Surface* e32 = SDL_ConvertSurfaceFormat(es, SDL_PIXELFORMAT_ARGB8888, 0);
            if (e32 != nullptr) {
                SDL_LockSurface(e32);
                for (int32_t y = 0; y < e32->h; ++y) {                  // (the outline's near black, about 85 %, as the old numbers' outline)
                    uint32_t* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(e32->pixels) + static_cast<size_t>(y) * static_cast<size_t>(e32->pitch));
                    for (int32_t x = 0; x < e32->w; ++x) {
                        const uint32_t a = (row[x] >> 24) * health::kOutlineAlpha / 255u;
                        row[x] = a << 24;
                    }
                }
                SDL_UnlockSurface(e32);
                SDL_SetSurfaceBlendMode(e32, SDL_BLENDMODE_NONE);
                SDL_BlitSurface(e32, nullptr, out, nullptr);
                SDL_FreeSurface(e32);
            }
            SDL_SetSurfaceBlendMode(fs, SDL_BLENDMODE_BLEND);
            SDL_Rect at{(es->w - fs->w) / 2, (es->h - fs->h) / 2, 0, 0};
            SDL_BlitSurface(fs, nullptr, out, &at);
            int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
            TTF_GlyphMetrics(fill, '0', &minx, &maxx, &miny, &maxy, &adv);
            int fminx = 0, fmaxx = 0, fminy = 0, fmaxy = 0, fadv = 0;
            TTF_GlyphMetrics(fill, first_code(text), &fminx, &fmaxx, &fminy, &fmaxy, &fadv);
            int lminx = 0, lmaxx = 0, lminy = 0, lmaxy = 0, ladv = 0;
            TTF_GlyphMetrics(fill, text.back() == '-' ? uint16_t{0x2212} : static_cast<uint16_t>(static_cast<unsigned char>(text.back())), &lminx, &lmaxx, &lminy, &lmaxy, &ladv);
            pic.w = out->w;
            pic.h = out->h;
            pic.ink_x = at.x + std::max(0, fminx);
            pic.ink_y = at.y + TTF_FontAscent(fill) - maxy;                        // (the digits' top: the '0' of this face)
            pic.ink_w = std::max(1, fs->w - std::max(0, fminx) - std::max(0, ladv - lmaxx));
            pic.texture = SDL_CreateTextureFromSurface(renderer_, out);
            if (pic.texture != nullptr) {
                SDL_SetTextureBlendMode(pic.texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(pic.texture, SDL_ScaleModeNearest);      // (one texel to one output pixel: nothing to filter)
            }
        }
    }
    if (out) SDL_FreeSurface(out);
    if (fs) SDL_FreeSurface(fs);
    if (es) SDL_FreeSurface(es);
    number_pictures_.push_back(pic);
    return pic.texture != nullptr ? &number_pictures_.back() : nullptr;
#else
    (void)text;
    (void)digit_px;
    return nullptr;
#endif
}

void Renderer::draw_number_ink(const NativeFrame& f, const NumberPicture& pic, double x, double y, bool centre_x, bool centre_y, health::Color color, int32_t alpha) {
    int32_t dx = f.x + static_cast<int32_t>(std::lround(x * f.scale));
    int32_t dy = f.y + static_cast<int32_t>(std::lround(y * f.scale));
    if (centre_x) dx -= pic.ink_w / 2;
    if (centre_y) dy -= pic.digit_px / 2;
    const SDL_Rect dst{dx - pic.ink_x, dy - pic.ink_y, pic.w, pic.h};
    SDL_SetTextureColorMod(pic.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(pic.texture, static_cast<uint8_t>(std::clamp(alpha, 0, 255)));
    SDL_RenderCopy(renderer_, pic.texture, nullptr, &dst);
}

// The hit point numbers: the top left of the old cell at the sprite's position, the digits starting 2 pixels under it; the digits as tall as the old ones (10 canvas pixels, times the phone's scale)
void Renderer::draw_font_digits(const LayoutRect& view, double zoom) {
    if (deferred_digits_.empty()) return;
    const NativeFrame f = begin_native();
    if (f.on) {
        const int32_t px = std::max(1, digit_scale_);
        const int32_t digit_px = std::max(2, static_cast<int32_t>(std::lround(10.0 * static_cast<double>(px) * f.scale)));
        for (const DeferredDigits& d : deferred_digits_) {
            const int32_t sx = view.x + static_cast<int32_t>(std::floor((static_cast<double>(d.wx) - static_cast<double>(camera_.x)) * zoom));
            const int32_t sy = view.y + static_cast<int32_t>(std::floor((static_cast<double>(d.wy) - static_cast<double>(camera_.y)) * zoom));
            const NumberPicture* pic = number_picture(d.text, digit_px);
            if (pic == nullptr) continue;
            const SDL_Rect at = placed(sx, sy + 2 * px, 0, 0);
            draw_number_ink(f, *pic, at.x, at.y, false, false, health::Color{255, 255, 255}, 255);
        }
        end_native(f);
    }
    deferred_digits_.clear();
}

// The damage and heal numbers: where the old ones stood (centred on the ant's body, rising), the digits as tall as the old ones (10 canvas pixels)
void Renderer::draw_font_changes() {
    const NativeFrame f = begin_native();
    if (!f.on) return;
    const double zoom = static_cast<double>(camera_.zoom);
    const LayoutRect view = layout_.view();
    const health::Rect screen{view.x, view.y, view.w, view.h};
    const int32_t digit_px = std::max(2, static_cast<int32_t>(std::lround(10.0 * f.scale)));
    for (size_t i = 0; i < changes_->count(); ++i) {
        const health::Number& num = changes_->at(i);
        const int32_t age = static_cast<int32_t>(changes_now_ms_ - num.start_ms);
        const int32_t alpha = health::alpha_at(age);
        if (alpha <= 0 || num.hidden) continue;
        const std::string text = std::string(num.heal ? "+" : "-") + std::to_string(std::min<int32_t>(num.amount, 9999));
        const NumberPicture* pic = number_picture(text, digit_px);
        if (pic == nullptr) continue;
        int32_t sx = 0;
        int32_t sy = 0;
        camera_.world_to_screen(num.wx, num.wy, sx, sy);
        const int32_t centre = sy - static_cast<int32_t>(std::lround(static_cast<double>(health::kBodyCentreAbove) * zoom));
        const int32_t rise = health::rise_at(age) + static_cast<int32_t>(num.level) * health::kLineHeight;
        const int32_t width = static_cast<int32_t>(text.size()) * health::kGlyphAdvance + 2;
        if (!health::overlaps(health::Rect{sx - width / 2, centre - rise - 6, width, 12}, screen)) continue;
        const SDL_Rect at = placed(sx, centre - rise, 0, 0);
        draw_number_ink(f, *pic, at.x, at.y, true, true, num.heal ? health::kHealColor : health::kDamageColor, alpha);
    }
    end_native(f);
}

// The score numbers: the original's field (a slot of 9 world pixels per digit, the sign in the slot of the first digit that is not a zero), each glyph centred in the slot that its sprite
// is centred in, as tall as the digit sprite, and (a world art) scaled by the zoom.
void Renderer::draw_deferred_scores() {
    if (deferred_scores_.empty()) return;
    const NativeFrame f = begin_native();
    if (f.on) {
        const double zoom = static_cast<double>(camera_.zoom);
        const AnimBounds zero = anim_bounds(anim_id_digit_[0]);
        const int32_t sprite_h = zero.valid ? std::max(1, zero.y1 - zero.y0) : 10;
        const int32_t digit_px = std::max(2, static_cast<int32_t>(std::lround(static_cast<double>(sprite_h) * zoom * f.scale)));
        for (const ScoreDigit& s : deferred_scores_) {
            int32_t value = std::clamp(s.amount < 0 ? -s.amount : s.amount, 0, 999999);
            int32_t divisor = 100000;
            bool leading = true;
            bool sign_pending = true;
            int32_t x = s.wx;
            const auto put = [&](int32_t anim_id, const std::string& glyph, health::Color color) {
                const AnimBounds b = anim_bounds(anim_id);
                if (!b.valid) return;
                const NumberPicture* pic = number_picture(glyph, digit_px);
                if (pic == nullptr) return;
                int32_t sx = 0;
                int32_t sy = 0;
                camera_.world_to_screen(x + (b.x0 + b.x1) / 2, s.wy + (b.y0 + b.y1) / 2, sx, sy);
                const SDL_Rect at = placed(sx, sy, 0, 0);
                draw_number_ink(f, *pic, at.x, at.y, true, true, color, 255);
            };
            while (divisor > 0) {
                const int32_t digit = value / divisor;
                value %= divisor;
                if (digit != 0 || divisor == 1) leading = false;
                if (!leading) {
                    if (sign_pending) {
                        put(s.amount > 0 ? anim_id_plus_ : anim_id_minus_, s.amount > 0 ? "+" : "-", health::Color{255, 255, 255});
                        x += 9;
                        sign_pending = false;
                    }
                    put(anim_id_digit_[digit], std::to_string(digit), health::Color{255, 255, 255});
                }
                divisor /= 10;
                x += 9;
            }
        }
        end_native(f);
    }
    deferred_scores_.clear();
}

}  // namespace ants::app
