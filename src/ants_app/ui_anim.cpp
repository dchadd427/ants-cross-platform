#include "ants_app/ui_anim.hpp"

#include <algorithm>

namespace ants::app {

void draw_animation_frame0(IRenderer& renderer, const assets::AssetArchive& archive, const char* name,
                           int32_t x, int32_t y) {
    const auto* seq = archive.find_animation(name);
    if (!seq || seq->subitems.empty()) return;
    const auto& sub = seq->subitems[0];
    for (size_t k = sub.frames.size(); k-- > 0;) {
        renderer.draw_sprite(sub.frames[k].sprite_index, x + sub.frames[k].dx, y + sub.frames[k].dy);
    }
}

UIRect animation_bounds(const assets::AssetArchive& archive, const char* name) {
    const auto* seq = archive.find_animation(name);
    if (!seq || seq->subitems.empty() || seq->subitems[0].frames.empty()) return {};
    int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    for (const auto& part : seq->subitems[0].frames) {
        const auto& sprite = archive.get_sprite(part.sprite_index);
        x0 = std::min(x0, part.dx);
        y0 = std::min(y0, part.dy);
        x1 = std::max(x1, part.dx + static_cast<int32_t>(sprite.width));
        y1 = std::max(y1, part.dy + static_cast<int32_t>(sprite.height));
    }
    return {x0, y0, x1 - x0, y1 - y0};
}


} // namespace ants::app
