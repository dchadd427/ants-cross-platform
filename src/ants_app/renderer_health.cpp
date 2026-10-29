// The ant health marks, drawn (the rule, the bar's color and sizes: include/ants_app/health_marks.hpp; docs/VIEW_AND_HUD.md, "Ant health marks"). Drawing only: they read the hit points that the
// snapshot of every client already has, nothing is sent and nothing of the simulation, a recording or a hash is touched.
//
// Show health as: Bar. Every ant that the world pass draws (so never one that fog of war hides) is offered to note_health_mark while the pass collects them; the bars that are worn are kept as world
// positions and drawn once the world is on the screen (draw_health_marks), over everything of the world, so that a bar is never hidden by a neighbor and, at a zoom, is drawn on the canvas at its own size.
//
// Show health as: Number. The original's own Ctrl+L numbers (docs/GAME_REVERSE_ENGINEERING.md, "The health number"): right after an ant is drawn (draw_ant_number, called by the ant's draw), its hit points
// in white, in the fixed system font (8 x 15 cells, not antialiased), the top left of the text at the sprite's position, so the digits hang to the lower right of it. No outline, no color, on a desktop; on a canvas shown small (a phone) they are scaled and outlined (health_marks.hpp, digit_scale).

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_app/health_marks.hpp"
#include "ants_app/renderer.hpp"

namespace ants::app {

void Renderer::set_health_marks(health::Setting setting, double picture_px_per_css, bool scale_digits) {
    health_ = setting;
    health_thick_ = health::thick_for(picture_px_per_css);
    digit_scale_ = scale_digits ? health::digit_scale(picture_px_per_css) : 1;
    digit_outline_ = scale_digits && health::digit_outlined(picture_px_per_css);
}

// (the sprite's own, predicted position: the bar follows the ant the way its ears do)
void Renderer::note_health_mark(const ants::sim::AntSnapshot& ant, int32_t sprite_x, int32_t sprite_y) {
    if (health_.style != health::Style::Bar || !health::shown(health_.when, ant.hp, ant.max_hp, ant.frozen)) return;
    health_marks_.push_back(HealthMark{sprite_x, sprite_y, static_cast<int32_t>(ant.hp), static_cast<int32_t>(ant.max_hp)});
}

void Renderer::draw_health_marks() {
    draw_deferred_digits();
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
static const char* const kFixedGlyphs[11][10] = {
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
        const int32_t g = (c >= '0' && c <= '9') ? c - '0' : (c == '-' ? 10 : -1);
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

}  // namespace ants::app
