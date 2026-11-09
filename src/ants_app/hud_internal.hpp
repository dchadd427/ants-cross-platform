#pragma once

// What hud.cpp and hud_alliance.cpp both use. It is not part of the library's interface.

#include "ants_app/renderer.hpp"
#include "ants_assets/asset_archive.hpp"

namespace ants::app::hud_detail {

// The frame of the quit dialog and of the alliance dialogs: the 20 parts of Table-4 animation std_dialg, origin (100, 100), no dim layer
inline void draw_std_dialog(IRenderer& renderer, const assets::AssetArchive& assets) {
    const int32_t dx = 100;
    const int32_t dy = 100;
    const auto* anim = assets.find_animation("std_dialg");
    if (anim && !anim->subitems.empty()) {
        const auto& frames = anim->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0; ) {
            const auto& fr = frames[i];
            renderer.draw_sprite(fr.sprite_index, dx + fr.dx, dy + fr.dy);
        }
    } else {
        renderer.fill_rect(dx, dy, 320, 224, assets::ColorRGBA{219, 75, 19, 255});
    }
}

} // namespace ants::app::hud_detail
