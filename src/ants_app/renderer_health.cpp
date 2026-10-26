// The ant health marks, drawn (the rule, the colors and the sizes: include/ants_app/health_marks.hpp; docs/VIEW_AND_HUD.md, "Ant health marks"). Drawing only: they read the hit points that the
// snapshot of every client already has, nothing is sent and nothing of the simulation, a recording or a hash is touched.
//
// Every ant that the world pass draws (so never one that fog of war hides) is offered to note_health_mark while the pass collects them; the marks that are worn are kept as world positions and
// drawn once the world is on the screen (draw_health_marks), over everything of the world, so that a mark is never hidden by a neighbor and, at a zoom, is drawn on the canvas at its own size.

#include <string>

#include "ants_app/health_marks.hpp"
#include "ants_app/renderer.hpp"

namespace ants::app {

void Renderer::set_health_marks(health::Setting setting, double picture_px_per_css) {
    health_ = setting;
    health_thick_ = health::thick_for(picture_px_per_css);
}

// (the sprite's own, predicted position: the mark follows the ant the way its ears do)
void Renderer::note_health_mark(const ants::sim::AntSnapshot& ant, int32_t sprite_x, int32_t sprite_y) {
    if (!health::shown(health_.when, ant.hp, ant.max_hp, ant.frozen)) return;
    health_marks_.push_back(HealthMark{sprite_x, sprite_y, static_cast<int32_t>(ant.hp), static_cast<int32_t>(ant.max_hp)});
}

void Renderer::draw_health_marks() {
    if (!renderer_ || health_marks_.empty()) {
        health_marks_.clear();
        return;
    }
    const double zoom = static_cast<double>(camera_.zoom);
    const bool numbers = health_.style == health::Style::Number;
    if (numbers && !health::number_fits(zoom)) {                 // (wider than a tile: left out; the bars stay at every zoom)
        health_marks_.clear();
        return;
    }
    const LayoutRect view = layout_.view();
    const health::Rect screen{view.x, view.y, view.w, view.h};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (const HealthMark& m : health_marks_) {
        int32_t sx = 0;
        int32_t sy = 0;
        camera_.world_to_screen(m.x, m.y, sx, sy);               // (its answer, whether the ant itself is in view, is not asked: the mark stands above it)
        if (numbers) {
            const health::Digits d = health::number(sx, sy, m.hp, zoom);
            if (!health::overlaps(d.covers, screen)) continue;
            const std::string text = std::to_string(m.hp);
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) draw_fixed_text(text, d.x + dx, d.y + dy, ants::assets::ColorRGBA{0, 0, 0, 255});
                }
            }
            draw_fixed_text(text, d.x, d.y, ants::assets::ColorRGBA{d.color.r, d.color.g, d.color.b, 255});
        } else {
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
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    health_marks_.clear();
}

}  // namespace ants::app
