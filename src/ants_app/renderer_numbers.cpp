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

std::string Renderer::choose_number_face(const std::string& text_face, const std::string& black_file) {
    if (text_face.empty()) return std::string();
#ifdef ANTS_ENABLE_SDL_TTF
    if (TTF_WasInit() != 0) {
        TTF_Font* probe = TTF_OpenFont(black_file.c_str(), 24);
        if (probe != nullptr) {
            TTF_CloseFont(probe);
            return black_file;
        }
    }
#else
    (void)black_file;
#endif
    return text_face;
}

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

// The faces that the numbers are made with (opened once per size), closed
void Renderer::drop_number_fonts() {
#ifdef ANTS_ENABLE_SDL_TTF
    for (DigitFont& d : digit_fonts_) {
        if (d.fill) TTF_CloseFont(static_cast<TTF_Font*>(d.fill));
        if (d.edge) TTF_CloseFont(static_cast<TTF_Font*>(d.edge));
    }
#endif
    digit_fonts_.clear();
}

// Everything: the pictures and the faces (a new face file, the end of the renderer)
void Renderer::clear_number_pictures() {
    for (NumberPicture& p : number_pictures_) {
        if (p.texture) SDL_DestroyTexture(p.texture);
    }
    number_pictures_.clear();
    drop_number_fonts();
}

// The picture cache is full: the oldest quarter (the least recently used) goes, the other three quarters stay, and so do the faces: a fight with many numbers of many sizes would otherwise
// throw every picture away and open every face again (twice a size) in the frames that follow, and a frame is only a few milliseconds long.
void Renderer::evict_number_pictures() {
    const size_t drop = std::max<size_t>(1, number_pictures_.size() / 4);
    std::vector<uint64_t> ages;
    ages.reserve(number_pictures_.size());
    for (const NumberPicture& p : number_pictures_) ages.push_back(p.used);
    std::nth_element(ages.begin(), ages.begin() + static_cast<std::ptrdiff_t>(drop - 1), ages.end());
    const uint64_t cutoff = ages[drop - 1];
    std::vector<NumberPicture> keep;
    keep.reserve(number_pictures_.size());
    size_t dropped = 0;
    for (NumberPicture& p : number_pictures_) {
        if (p.used <= cutoff && dropped < drop) {
            if (p.texture) SDL_DestroyTexture(p.texture);
            ++dropped;
        } else {
            keep.push_back(std::move(p));
        }
    }
    number_pictures_.swap(keep);
}

// The faces are at their bound: the oldest quarter goes (their pictures stay: a picture does not need its face once it is made)
void Renderer::evict_number_fonts() {
    const size_t drop = std::max<size_t>(1, digit_fonts_.size() / 4);
    std::vector<uint64_t> ages;
    ages.reserve(digit_fonts_.size());
    for (const DigitFont& d : digit_fonts_) ages.push_back(d.used);
    std::nth_element(ages.begin(), ages.begin() + static_cast<std::ptrdiff_t>(drop - 1), ages.end());
    const uint64_t cutoff = ages[drop - 1];
    std::vector<DigitFont> keep;
    keep.reserve(digit_fonts_.size());
    size_t dropped = 0;
    for (DigitFont& d : digit_fonts_) {
        if (d.used <= cutoff && dropped < drop) {
#ifdef ANTS_ENABLE_SDL_TTF
            if (d.fill) TTF_CloseFont(static_cast<TTF_Font*>(d.fill));
            if (d.edge) TTF_CloseFont(static_cast<TTF_Font*>(d.edge));
#endif
            ++dropped;
        } else {
            keep.push_back(d);
        }
    }
    digit_fonts_.swap(keep);
}

// The part of a digit sprite that shows: the pixels that are not the color key, over every frame of the animation (the sprite's own box is the cell, which has margins)
AnimBounds Renderer::anim_ink(int32_t anim_id) {
    const auto found = anim_ink_.find(anim_id);
    if (found != anim_ink_.end()) return found->second;
    AnimBounds b;
    if (archive_ && anim_id >= 0) {
        const auto& seq = archive_->get_animation(static_cast<uint32_t>(anim_id));
        for (const auto& sub : seq.subitems) {
            for (const auto& fr : sub.frames) {
                const auto& sp = archive_->get_sprite(fr.sprite_index);
                for (uint32_t y = 0; y < sp.height; ++y) {
                    for (uint32_t x = 0; x < sp.width; ++x) {
                        if (sp.get_pixel(x, y) == ants::assets::CHD_COLOR_KEY_INDEX) continue;
                        const int32_t px = fr.dx + static_cast<int32_t>(x);
                        const int32_t py = fr.dy + static_cast<int32_t>(y);
                        if (!b.valid) { b.x0 = px; b.y0 = py; b.x1 = px + 1; b.y1 = py + 1; b.valid = true; }
                        else { b.x0 = std::min(b.x0, px); b.y0 = std::min(b.y0, py); b.x1 = std::max(b.x1, px + 1); b.y1 = std::max(b.y1, py + 1); }
                    }
                }
            }
        }
    }
    anim_ink_[anim_id] = b;
    return b;
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

}  // namespace
#endif

// The picture of a text: the face's glyphs in white over an outline of `outline_px` output pixels in near black, the whole of what shows `height_px` tall (the glyphs of a digit are
// `height_px - 2 * outline_px` tall), the way the old drawing's ink was measured (its outline counted). Made once per (text, size, outline); the number of them is bounded.
const Renderer::NumberPicture* Renderer::number_picture(const std::string& text, int32_t height_px, int32_t outline_px) {
#ifdef ANTS_ENABLE_SDL_TTF
    if (!renderer_ || text.empty() || number_path_.empty() || height_px < 4) return nullptr;
    const int32_t o = std::max(1, outline_px);
    // `height_px` is what shows with the OLD outline (one scaled pixel, `base` here); the thicker outline grows outward from the glyphs, which keep the size that the old one left them
    // (the counters of 0 6 8 9 4 stay open). So what shows is `2 * (o - base)` taller and wider than asked, and the glyph is `height_px - 2 * base` tall.
    const int32_t base = std::max(1, o / health::kNumberOutlineFactor);
    const int32_t digit_px = std::max(2, height_px - 2 * base);
    for (NumberPicture& p : number_pictures_) {
        if (p.height_px == height_px && p.outline_px == o && p.text == text) {
            p.used = ++number_tick_;
            return p.texture != nullptr ? &p : nullptr;
        }
    }
    if (number_pictures_.size() >= kMaxNumberPictures) evict_number_pictures();      // (a bound, not a working size: the oldest quarter goes)
    if (font_per_point_ <= 0.0) {                      // (how tall a digit is per point of the face: measured once, on a probe)
        TTF_Font* probe = TTF_OpenFont(number_path_.c_str(), 200);
        if (probe == nullptr) return nullptr;
        int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
        TTF_GlyphMetrics(probe, '0', &minx, &maxx, &miny, &maxy, &adv);
        TTF_CloseFont(probe);
        font_per_point_ = static_cast<double>(maxy - miny) / 200.0;
        if (font_per_point_ <= 0.05) { font_per_point_ = 0.0; return nullptr; }
    }
    DigitFont* df = nullptr;
    for (DigitFont& d : digit_fonts_) {
        if (d.digit_px == digit_px && d.outline == o) df = &d;
    }
    if (df != nullptr) df->used = ++number_tick_;
    if (df == nullptr) {
        if (digit_fonts_.size() >= kMaxNumberFonts) evict_number_fonts();
        const int32_t point = std::max(4, static_cast<int32_t>(std::lround(static_cast<double>(digit_px) / font_per_point_)));
        TTF_Font* fill = TTF_OpenFont(number_path_.c_str(), point);
        TTF_Font* edge = TTF_OpenFont(number_path_.c_str(), point);
        number_font_opens_ += 2;
        if (fill == nullptr || edge == nullptr) {
            if (fill) TTF_CloseFont(fill);
            if (edge) TTF_CloseFont(edge);
            return nullptr;
        }
        TTF_SetFontOutline(edge, o);
        DigitFont d;
        d.digit_px = digit_px;
        d.outline = o;
        d.fill = fill;
        d.edge = edge;
        d.used = ++number_tick_;
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
    pic.height_px = height_px;
    pic.outline_px = o;
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
            // the box of what shows: the digits' height (the '0' of this face, from its top to its bottom, outline round it), the text's width (first glyph's left edge to the last one's right edge)
            int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
            TTF_GlyphMetrics(fill, '0', &minx, &maxx, &miny, &maxy, &adv);
            const auto code = [](char c) { return c == '-' ? uint16_t{0x2212} : static_cast<uint16_t>(static_cast<unsigned char>(c)); };
            int fminx = 0, fmaxx = 0, fminy = 0, fmaxy = 0, fadv = 0;
            TTF_GlyphMetrics(fill, code(text.front()), &fminx, &fmaxx, &fminy, &fmaxy, &fadv);
            int lminx = 0, lmaxx = 0, lminy = 0, lmaxy = 0, ladv = 0;
            TTF_GlyphMetrics(fill, code(text.back()), &lminx, &lmaxx, &lminy, &lmaxy, &ladv);
            const int32_t left = at.x + std::max(0, fminx) - o;
            const int32_t right = at.x + fs->w - std::max(0, ladv - lmaxx) + o;
            pic.box_x = std::max(0, left);
            pic.box_w = std::max(1, std::min(out->w, right) - pic.box_x);
            pic.box_y = std::max(0, at.y + TTF_FontAscent(fill) - maxy - o);
            pic.box_h = std::min(out->h - pic.box_y, (maxy - miny) + 2 * o);
            pic.w = out->w;
            pic.h = out->h;
            pic.texture = SDL_CreateTextureFromSurface(renderer_, out);
            if (pic.texture != nullptr) {
                SDL_SetTextureBlendMode(pic.texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(pic.texture, SDL_ScaleModeLinear);      // (one texel to one output pixel; a score digit's slight stretch is filtered)
            }
        }
    }
    if (out) SDL_FreeSurface(out);
    if (fs) SDL_FreeSurface(fs);
    if (es) SDL_FreeSurface(es);
    pic.used = ++number_tick_;
    number_pictures_.push_back(pic);
    return pic.texture != nullptr ? &number_pictures_.back() : nullptr;
#else
    (void)text;
    (void)height_px;
    (void)outline_px;
    return nullptr;
#endif
}

// A picture copied by the box of what shows. (x, y) is its left and top edge in canvas pixels (the middle when centred); `width_px` > 0 stretches it to that many output pixels wide,
// about the same middle, so that a glyph takes the width of the sprite digit that it replaces.
void Renderer::draw_number_box(const NativeFrame& f, const NumberPicture& pic, double x, double y, bool centre_x, bool centre_y, health::Color color, int32_t alpha, int32_t width_px) {
    const double k = width_px > 0 ? static_cast<double>(width_px) / static_cast<double>(pic.box_w) : 1.0;
    const double sx = (k > 0.7 && k < 1.45) ? k : 1.0;       // (a stretch only where it stays slight)
    const int32_t box_w = static_cast<int32_t>(std::lround(static_cast<double>(pic.box_w) * sx));
    int32_t dx = f.x + static_cast<int32_t>(std::lround(x * f.scale));
    int32_t dy = f.y + static_cast<int32_t>(std::lround(y * f.scale));
    if (centre_x) dx -= box_w / 2;
    if (centre_y) dy -= pic.box_h / 2;
    const SDL_Rect dst{dx - static_cast<int32_t>(std::lround(static_cast<double>(pic.box_x) * sx)), dy - pic.box_y, static_cast<int32_t>(std::lround(static_cast<double>(pic.w) * sx)), pic.h};
    SDL_SetTextureColorMod(pic.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(pic.texture, static_cast<uint8_t>(std::clamp(alpha, 0, 255)));
    SDL_RenderCopy(renderer_, pic.texture, nullptr, &dst);
}

// The hit point numbers: where the old cell's ink stood (2 pixels under the sprite's position, and one scaled pixel out on the phone, where the old digits were outlined), as tall as the old ink
// (10 canvas pixels, outline included on the phone: (10 + 2) x the scale), so they do not look larger.
void Renderer::draw_font_digits(const LayoutRect& view, double zoom) {
    if (deferred_digits_.empty()) return;
    const NativeFrame f = begin_native();
    if (f.on) {
        const int32_t px = std::max(1, digit_scale_);
        const int32_t pad = digit_outline_ ? px : 0;                             // (the old outline's width, canvas pixels)
        const int32_t height_px = std::max(4, static_cast<int32_t>(std::lround(static_cast<double>(10 * px + 2 * pad) * f.scale)));
        const int32_t outline_px = health::number_outline_px(static_cast<double>(std::max(1, pad)) * f.scale);
        for (const DeferredDigits& d : deferred_digits_) {
            const int32_t sx = view.x + static_cast<int32_t>(std::floor((static_cast<double>(d.wx) - static_cast<double>(camera_.x)) * zoom));
            const int32_t sy = view.y + static_cast<int32_t>(std::floor((static_cast<double>(d.wy) - static_cast<double>(camera_.y)) * zoom));
            const NumberPicture* pic = number_picture(d.text, height_px, outline_px);
            if (pic == nullptr) continue;
            const SDL_Rect at = placed(sx - pad, sy + 2 * px - pad, 0, 0);
            draw_number_box(f, *pic, at.x, at.y, false, false, health::Color{255, 255, 255}, 255);
        }
        end_native(f);
    }
    deferred_digits_.clear();
}

// The damage and heal numbers: where the old ones stood (centred on the ant's body, rising), as tall as the old picture (10 canvas pixels and an outline: 12), and, with the age, smaller:
// health::scale_at, about the number's own middle, so that it stays over the ant. Every size is its own picture (one output pixel steps, a bounded cache): crisp at every size.
void Renderer::draw_font_changes() {
    const NativeFrame f = begin_native();
    if (!f.on) return;
    const double zoom = static_cast<double>(camera_.zoom);
    const LayoutRect view = layout_.view();
    const health::Rect screen{view.x, view.y, view.w, view.h};
    for (size_t i = 0; i < changes_->count(); ++i) {
        const health::Number& num = changes_->at(i);
        const int32_t age = static_cast<int32_t>(changes_now_ms_ - num.start_ms);
        const int32_t alpha = health::alpha_at(age);
        if (alpha <= 0 || num.hidden) continue;
        const double shrink = health::scale_at(age);
        const int32_t height_px = std::max(4, static_cast<int32_t>(std::lround(static_cast<double>(health::kGlyphHeight + 2) * f.scale * shrink)));
        const int32_t outline_px = health::number_outline_px(f.scale * shrink);
        const std::string text = std::string(num.heal ? "+" : "-") + std::to_string(std::min<int32_t>(num.amount, 9999));
        const NumberPicture* pic = number_picture(text, height_px, outline_px);
        if (pic == nullptr) continue;
        int32_t sx = 0;
        int32_t sy = 0;
        camera_.world_to_screen(num.wx, num.wy, sx, sy);
        const int32_t centre = sy - static_cast<int32_t>(std::lround(static_cast<double>(health::kBodyCentreAbove) * zoom));
        const int32_t rise = health::rise_at(age) + static_cast<int32_t>(num.level) * health::kLineHeight;
        const int32_t width = static_cast<int32_t>(text.size()) * health::kGlyphAdvance + 2;
        if (!health::overlaps(health::Rect{sx - width / 2, centre - rise - 6, width, 12}, screen)) continue;
        const SDL_Rect at = placed(sx, centre - rise, 0, 0);
        const int32_t old_ink = static_cast<int32_t>(text.size()) * health::kGlyphAdvance + 1;       // (the old glyphs' ink: 7 pixels a character and one more: "-2" is 15 wide)
        draw_number_box(f, *pic, at.x, at.y, true, true, num.heal ? health::kHealColor : health::kDamageColor, alpha, static_cast<int32_t>(std::lround(static_cast<double>(old_ink) * f.scale * shrink)));
    }
    end_native(f);
}

// The score numbers: the original's field (a slot of 9 world pixels per digit, the sign in the slot of the first digit that is not a zero). Each glyph is as tall as the digit sprite's INK
// (the pixels that show, outline included, not its cell), as wide as the sprite that it replaces (a slight stretch), and centred on that sprite's ink: so the number takes the old one's room.
// A world art: scaled by the zoom.
void Renderer::draw_deferred_scores() {
    if (deferred_scores_.empty()) return;
    const NativeFrame f = begin_native();
    if (f.on) {
        const double zoom = static_cast<double>(camera_.zoom);
        const AnimBounds zero = anim_ink(anim_id_digit_[0]);
        const int32_t ink_h = zero.valid ? std::max(1, zero.y1 - zero.y0) : 10;
        const int32_t height_px = std::max(4, static_cast<int32_t>(std::lround(static_cast<double>(ink_h) * zoom * f.scale)));
        const int32_t outline_px = health::number_outline_px(zoom * f.scale);
        for (const ScoreDigit& s : deferred_scores_) {
            int32_t value = std::clamp(s.amount < 0 ? -s.amount : s.amount, 0, 999999);
            int32_t divisor = 100000;
            bool leading = true;
            bool sign_pending = true;
            int32_t x = s.wx;
            const auto put = [&](int32_t anim_id, const std::string& glyph) {
                const AnimBounds b = anim_ink(anim_id);
                if (!b.valid) return;
                const NumberPicture* pic = number_picture(glyph, height_px, outline_px);
                if (pic == nullptr) return;
                int32_t sx = 0;
                int32_t sy = 0;
                camera_.world_to_screen(x + (b.x0 + b.x1) / 2, s.wy + (zero.valid ? (zero.y0 + zero.y1) / 2 : (b.y0 + b.y1) / 2), sx, sy);
                const SDL_Rect at = placed(sx, sy, 0, 0);
                const int32_t want_w = static_cast<int32_t>(std::lround(static_cast<double>(b.x1 - b.x0) * zoom * f.scale));
                draw_number_box(f, *pic, at.x, at.y, true, true, health::Color{255, 255, 255}, 255, want_w);
            };
            while (divisor > 0) {
                const int32_t digit = value / divisor;
                value %= divisor;
                if (digit != 0 || divisor == 1) leading = false;
                if (!leading) {
                    if (sign_pending) {
                        put(s.amount > 0 ? anim_id_plus_ : anim_id_minus_, s.amount > 0 ? "+" : "-");
                        x += 9;
                        sign_pending = false;
                    }
                    put(anim_id_digit_[digit], std::to_string(digit));
                }
                divisor /= 10;
                x += 9;
            }
        }
        end_native(f);
    }
    deferred_scores_.clear();
}

// A score number of the HUD's boxes or the results page: the numbers' face, white (or `color`), the thick outline. `ink_h` canvas pixels tall as the old drawing's ink was (the outline grows outward from it,
// about the middle), starting at `x` (or ending there), squeezed to `box_w` canvas pixels when wider (the draw_text_squeezed rule: the height stays).
bool Renderer::draw_number_text(const std::string& text, int32_t x, int32_t y, int32_t box_w, int32_t ink_h, bool right_align, ants::assets::ColorRGBA color) {
    if (text.empty() || !font_numbers() || ink_h < 2 || box_w < 2) return false;
    const NativeFrame f = begin_native();
    if (!f.on) return false;
    bool drawn = false;
    const int32_t height_px = std::max(4, static_cast<int32_t>(std::lround(static_cast<double>(ink_h) * f.scale)));
    const NumberPicture* pic = number_picture(text, height_px, health::number_outline_px(f.scale));
    if (pic != nullptr) {
        const SDL_Rect at = placed(x, y, 0, 0);
        const int32_t avail = std::max(1, static_cast<int32_t>(std::lround(static_cast<double>(box_w) * f.scale)));
        const double k = pic->box_w > avail ? static_cast<double>(avail) / static_cast<double>(pic->box_w) : 1.0;
        const int32_t shown_w = std::max(1, static_cast<int32_t>(std::lround(static_cast<double>(pic->box_w) * k)));
        const int32_t left = f.x + static_cast<int32_t>(std::lround(static_cast<double>(at.x) * f.scale)) - (right_align ? shown_w : 0);
        const int32_t mid = f.y + static_cast<int32_t>(std::lround((static_cast<double>(at.y) + static_cast<double>(ink_h) / 2.0) * f.scale));
        const SDL_Rect dst{left - static_cast<int32_t>(std::lround(static_cast<double>(pic->box_x) * k)), mid - pic->box_h / 2 - pic->box_y,
                           static_cast<int32_t>(std::lround(static_cast<double>(pic->w) * k)), pic->h};
        SDL_SetTextureColorMod(pic->texture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(pic->texture, color.a);
        SDL_RenderCopy(renderer_, pic->texture, nullptr, &dst);
        drawn = true;
    }
    end_native(f);
    return drawn;
}

}  // namespace ants::app
