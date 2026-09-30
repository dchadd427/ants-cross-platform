#pragma once

#include <cstdint>

#include "ants_app/renderer.hpp"
#include "ants_assets/asset_archive.hpp"

namespace ants::app {

/**
 * @brief Integer rectangle used for the hit areas of animation-driven controls.
 */
struct UIRect {
    int32_t x{0};
    int32_t y{0};
    int32_t w{0};
    int32_t h{0};

    bool contains(int32_t px, int32_t py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// The original's user-interface animations carry ABSOLUTE screen coordinates in their part offsets: the sprite object is
// placed at (0,0) and every part sits where the artist put it. These helpers read frame 0 of such an animation.

// Draws frame 0 of a named animation at an extra offset (x, y): every part at its own offset, last stored part first.
void draw_animation_frame0(IRenderer& renderer, const assets::AssetArchive& archive, const char* name,
                           int32_t x = 0, int32_t y = 0);

// Union of the sprite rectangles of frame 0 (an empty rectangle when the animation does not exist).
UIRect animation_bounds(const assets::AssetArchive& archive, const char* name);


} // namespace ants::app
