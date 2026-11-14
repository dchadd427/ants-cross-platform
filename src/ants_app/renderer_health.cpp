// The ant health marks, drawn (the rule, the bar's color and sizes: include/ants_app/health_marks.hpp; docs/VIEW_AND_HUD.md, "Ant health marks"). Drawing only: they read the hit points that the
// snapshot of every client already has, nothing is sent and nothing of the simulation, a recording or a hash is touched.
//
// Show health as: Bar. Every ant that the world pass draws (so never one that fog of war hides) is offered to note_health_mark while the pass collects them; the bars that are worn are kept as world
// positions and drawn once the world is on the screen (draw_health_marks), over everything of the world, so that a bar is never hidden by a neighbor and, at a zoom, is drawn on the canvas at its own size.
//
// Damage and heal numbers (health_changes.hpp: the rule; docs/VIEW_AND_HUD.md, "Damage and heal numbers"): draw_health_changes, called by draw_health_marks after the bars and the deferred digits, so that they stand over every
// part of the world, the selection ears and the health marks, and under the panel, the HUD, the dialogs and the cursor. Drawing only: the tracker that holds them reads the snapshot, nothing is sent.
//
// Show health as: Number. The original's own Ctrl+L numbers (docs/GAME_REVERSE_ENGINEERING.md, "The health number"): right after an ant is drawn (draw_ant_number, called by the ant's draw), its hit points
// in white, in the fixed system font (8 x 15 cells, not antialiased), the top left of the text at the sprite's position, so the digits hang to the lower right of it. No outline, no color, on a desktop; on a canvas shown small (a phone) they are scaled and outlined (health_marks.hpp, digit_scale).

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_app/health_changes.hpp"
#include "ants_app/health_marks.hpp"
#include "ants_app/renderer.hpp"

namespace ants::app {

void Renderer::set_health_marks(health::Setting setting, double picture_px_per_css, bool scale_digits) {
    health_ = setting;
    health_thick_ = health::thick_for(picture_px_per_css);
    digit_scale_ = scale_digits ? health::digit_scale(picture_px_per_css) : 1;
    digit_outline_ = scale_digits && health::digit_outlined(picture_px_per_css);
}

void Renderer::set_health_changes(health::ChangeTracker* tracker, int64_t now_ms, double picture_px_per_css, bool scale_digits) {
    changes_ = tracker;
    changes_now_ms_ = now_ms;
    changes_thick_ = health::thick_for(picture_px_per_css);            // (the place does not depend on whether the bars are shown: the bar that would stand there is the one of this size)
    changes_scale_ = scale_digits ? std::min(health::kChangeScaleMax, health::digit_scale(picture_px_per_css)) : 1;
}

// (the sprite's own, predicted position: the bar follows the ant the way its ears do)
void Renderer::note_health_mark(const ants::sim::AntSnapshot& ant, int32_t sprite_x, int32_t sprite_y) {
    if (health_.style != health::Style::Bar || !health::shown(health_.when, ant.hp, ant.max_hp, ant.frozen)) return;
    health_marks_.push_back(HealthMark{sprite_x, sprite_y, static_cast<int32_t>(ant.hp), static_cast<int32_t>(ant.max_hp)});
}

void Renderer::draw_health_marks() {
    draw_deferred_digits();
    draw_bars();
    draw_health_changes();
}

void Renderer::draw_bars() {
    if (!renderer_ || health_marks_.empty()) {
        health_marks_.clear();
        return;
    }
    const double zoom = static_cast<double>(camera_.zoom);
    const LayoutRect view = layout_.view();
    const health::Rect screen{view.x, view.y, view.w, view.h};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (const HealthMark& m : health_marks_) {
        int32_t sx = 0;
        int32_t sy = 0;
        camera_.world_to_screen(m.x, m.y, sx, sy);               // (its answer, whether the ant itself is in view, is not asked: the bar stands above it)
        const health::Bar b = health::bar(sx, sy, m.hp, m.max_hp, zoom, health_thick_);
        if (!health::overlaps(b.framed ? b.outline : b.track, screen)) continue;
        if (b.framed) {
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 230);
            const SDL_Rect o = placed(b.outline.x, b.outline.y, b.outline.w, b.outline.h);
            SDL_RenderFillRect(renderer_, &o);
        }
        SDL_SetRenderDrawColor(renderer_, 40, 40, 40, b.framed ? 255 : 170);
        const SDL_Rect t = placed(b.track.x, b.track.y, b.track.w, b.track.h);
        SDL_RenderFillRect(renderer_, &t);
        SDL_SetRenderDrawColor(renderer_, b.color.r, b.color.g, b.color.b, 255);
        const SDL_Rect f = placed(b.fill.x, b.fill.y, b.fill.w, b.fill.h);
        SDL_RenderFillRect(renderer_, &f);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    health_marks_.clear();
}

// FUN_0101b802 draws the number after the ant (the display loop skips a frozen ant), at the sprite's position (the predicted one: the sprite moves when a frame ends). At the zoom 1 it is drawn here,
// in the ant's place in the draw order; at a zoom it is text, not world art, so it waits for the copy (draw_deferred_digits) and keeps its one size.
void Renderer::draw_ant_number(const ants::sim::AntSnapshot& ant, int32_t sprite_x, int32_t sprite_y) {
    if (health_.style != health::Style::Number || !health::shown_number(health_.when, ant.hp, ant.max_hp, ant.frozen)) return;
    if (in_world_target_ && pass_.camera.zoom != zoom::kNormal) {
        deferred_digits_.push_back(DeferredDigits{std::to_string(ant.hp), sprite_x, sprite_y});
    } else {
        const LayoutRect view = world_view();
        draw_fixed_text(std::to_string(ant.hp), view.x + (sprite_x - static_cast<int32_t>(camera_.x)), view.y + (sprite_y - static_cast<int32_t>(camera_.y)), ants::assets::ColorRGBA{255, 255, 255, 255}, digit_scale_, digit_outline_);
    }
}

// What a zoomed pass leaves for the screen: the numbers (one size at every zoom), top left at the screen position of the sprite
void Renderer::draw_deferred_digits() {
    const LayoutRect view = layout_.view();
    const double z = static_cast<double>(camera_.zoom);
    for (const DeferredDigits& d : deferred_digits_) {
        const int32_t sx = view.x + static_cast<int32_t>(std::floor((static_cast<double>(d.wx) - static_cast<double>(camera_.x)) * z));
        const int32_t sy = view.y + static_cast<int32_t>(std::floor((static_cast<double>(d.wy) - static_cast<double>(camera_.y)) * z));
        draw_fixed_text(d.text, sx, sy, ants::assets::ColorRGBA{255, 255, 255, 255}, digit_scale_, digit_outline_);
    }
    deferred_digits_.clear();
}

// The original's health number: TextOutA with the stock SYSTEM_FIXED_FONT, the raster "Fixedsys": every character is a cell of 8 x 15 pixels, the
// glyphs are not antialiased. The Windows font itself is not available here, so the digits are drawn by hand in its style: 7 x 10 pixels with strokes of
// one pixel, top at row 2 of the cell (the font's ascent is 12).
static const char* const kFixedGlyphs[12][10] = {
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
    {".......", ".......", ".......", "...X...", "...X...", ".XXXXX.", "...X...", "...X...", ".......", "......."},   // + (new art, the same hand and weight as the minus; used by the heal numbers only)
};

void Renderer::draw_fixed_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color, int32_t scale, bool outline) {
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
        const int32_t g = (c >= '0' && c <= '9') ? c - '0' : (c == '-' ? 10 : (c == '+' ? 11 : -1));
        if (g >= 0 && fixed_glyphs_[static_cast<size_t>(g)] != nullptr) {
            SDL_Texture* glyph = fixed_glyphs_[static_cast<size_t>(g)];
            if (outline) {                                     // (a phone only: the glyph in black, one scaled pixel round it in the eight directions, then the glyph over it)
                SDL_SetTextureColorMod(glyph, 0, 0, 0);
                SDL_SetTextureAlphaMod(glyph, 255);
                for (int32_t oy = -1; oy <= 1; ++oy) {
                    for (int32_t ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) continue;
                        const SDL_Rect od = placed(cur_x + ox * scale, y + oy * scale, kFixedCellW * scale, kFixedCellH * scale);
                        SDL_RenderCopy(renderer_, glyph, nullptr, &od);
                    }
                }
            }
            SDL_SetTextureColorMod(glyph, color.r, color.g, color.b);
            SDL_SetTextureAlphaMod(glyph, color.a);
            const SDL_Rect dst = placed(cur_x, y, kFixedCellW * scale, kFixedCellH * scale);
            SDL_RenderCopy(renderer_, glyph, nullptr, &dst);
        }
        cur_x += kFixedCellW * scale;                                  // a fixed font: every character takes a cell, drawn or not
    }
}

// The picture of one number's text ("-12", "+7"): the glyphs of the fixed font side by side at a 7 pixel step (the glyph is 7 wide, so the cell's blank column is dropped), white and opaque, with a one pixel outline
// in near black (about 85 %) round the union of them, so that the outline is one even layer where two glyphs meet. Made once per text and tinted (the color) and faded (the opacity) when it is copied.
const Renderer::ChangePicture* Renderer::change_picture(const std::string& text) {
    for (const ChangePicture& p : change_pictures_) {
        if (p.text == text) return p.texture != nullptr ? &p : nullptr;
    }
    if (!renderer_ || text.empty()) return nullptr;
    if (change_pictures_.size() >= 64) {                               // (a handful of texts are ever seen: a bound, not a working size)
        for (ChangePicture& p : change_pictures_) {
            if (p.texture) SDL_DestroyTexture(p.texture);
        }
        change_pictures_.clear();
    }
    const int32_t n = static_cast<int32_t>(text.size());
    const int32_t w = n * health::kGlyphAdvance + 2;
    const int32_t h = health::kGlyphHeight + 2;
    std::vector<uint8_t> ink(static_cast<size_t>(w * h), 0u);
    for (int32_t i = 0; i < n; ++i) {
        const char c = text[static_cast<size_t>(i)];
        const int32_t g = (c >= '0' && c <= '9') ? c - '0' : (c == '-' ? 10 : (c == '+' ? 11 : -1));
        if (g < 0) continue;
        for (int32_t row = 0; row < health::kGlyphHeight; ++row) {
            for (int32_t col = 0; col < health::kGlyphAdvance; ++col) {
                if (kFixedGlyphs[g][row][col] == 'X') ink[static_cast<size_t>((row + 1) * w + 1 + i * health::kGlyphAdvance + col)] = 1u;
            }
        }
    }
    std::vector<uint32_t> pixels(static_cast<size_t>(w * h), 0u);
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            const size_t at = static_cast<size_t>(y * w + x);
            if (ink[at] != 0u) {
                pixels[at] = 0xFFFFFFFFu;                              // the glyph: white, tinted by the color
                continue;
            }
            bool near = false;
            for (int32_t dy = -1; dy <= 1 && !near; ++dy) {
                for (int32_t dx = -1; dx <= 1 && !near; ++dx) {
                    const int32_t xx = x + dx;
                    const int32_t yy = y + dy;
                    if (xx >= 0 && xx < w && yy >= 0 && yy < h && ink[static_cast<size_t>(yy * w + xx)] != 0u) near = true;
                }
            }
            if (near) pixels[at] = static_cast<uint32_t>(health::kOutlineAlpha) << 24;      // near black, black under any tint
        }
    }
    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (tex != nullptr) {
        SDL_UpdateTexture(tex, nullptr, pixels.data(), w * static_cast<int32_t>(sizeof(uint32_t)));
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    }
    change_pictures_.push_back(ChangePicture{text, tex, w, h});
    return tex != nullptr ? &change_pictures_.back() : nullptr;
}

// The numbers of the frame, over the ants: one size at every zoom (the health numbers' rule on a small canvas, to twice at most), centred on the ant's drawn sprite, the glyphs' bottom 2 pixels over where the health
// bar's top would be (31 world pixels over the sprite, scaled by the zoom), rising 8 pixels at an even pace and fading in the last part of their life. Clipped to the part of the view that is already the clip.
void Renderer::draw_health_changes() {
    if (changes_ == nullptr || !renderer_) return;
    changes_->expire(changes_now_ms_);
    if (changes_->count() == 0) return;
    const double zoom = static_cast<double>(camera_.zoom);
    const int32_t scale = changes_scale_;
    const LayoutRect view = layout_.view();
    const health::Rect screen{view.x, view.y, view.w, view.h};
    const SDL_bool was_clipped = SDL_RenderIsClipEnabled(renderer_);
    SDL_Rect kept{};
    if (was_clipped) SDL_RenderGetClipRect(renderer_, &kept);
    const SDL_Rect view_rect = placed(view.x, view.y, view.w, view.h);
    SDL_Rect clip = view_rect;
    if (was_clipped && !SDL_IntersectRect(&kept, &view_rect, &clip)) clip = SDL_Rect{0, 0, 0, 0};
    SDL_RenderSetClipRect(renderer_, &clip);
    for (size_t i = 0; i < changes_->count(); ++i) {
        const health::Number& num = changes_->at(i);
        const int32_t age = static_cast<int32_t>(changes_now_ms_ - num.start_ms);
        const int32_t alpha = health::alpha_at(age);
        if (alpha <= 0) continue;
        const ChangePicture* pic = change_picture(std::string(num.heal ? "+" : "-") + std::to_string(std::min<int32_t>(num.amount, 9999)));
        if (pic == nullptr) continue;
        int32_t sx = 0;
        int32_t sy = 0;
        camera_.world_to_screen(num.wx, num.wy, sx, sy);
        const int32_t bar_top = sy + static_cast<int32_t>(std::lround(static_cast<double>(-health::kHeadAbove) * zoom)) - health::bar_height(zoom, changes_thick_);
        const int32_t glyph_bottom = bar_top - health::kAboveBar * scale;
        const int32_t rise = health::rise_at(age) * scale + static_cast<int32_t>(num.level) * health::kLineHeight * scale;
        const int32_t top = glyph_bottom - health::kGlyphHeight * scale - scale - rise;       // (the outline's row is one more over the glyph)
        const int32_t left = sx - pic->w * scale / 2;
        if (!health::overlaps(health::Rect{left, top, pic->w * scale, pic->h * scale}, screen)) continue;
        const health::Color c = num.heal ? health::kHealColor : health::kDamageColor;
        SDL_SetTextureColorMod(pic->texture, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(pic->texture, static_cast<uint8_t>(alpha));
        const SDL_Rect dst = placed(left, top, pic->w * scale, pic->h * scale);
        SDL_RenderCopy(renderer_, pic->texture, nullptr, &dst);
    }
    if (was_clipped) SDL_RenderSetClipRect(renderer_, &kept);
    else SDL_RenderSetClipRect(renderer_, nullptr);
}

}  // namespace ants::app
