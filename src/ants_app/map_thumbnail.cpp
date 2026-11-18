#include "ants_app/map_thumbnail.hpp"

#include "ants_app/map_preview.hpp"

namespace ants::app {

MapThumbnail make_map_thumbnail(IRenderer& renderer, const assets::LevelData& level, const std::array<assets::ColorRGBA, 256>& palette) {
    MapThumbnail out;
    MapPreview preview = render_map_preview_world(renderer, level, kMapThumbnailSide, &out.why);
    out.engine = preview.valid();
    if (!preview.valid()) preview = render_map_preview_in_box(level, palette, kMapThumbnailSide);          // (what the setup screen shows when the renderer cannot draw the map)
    if (!preview.valid() || preview.width > kMapThumbnailSide || preview.height > kMapThumbnailSide) {
        out.engine = false;
        return out;
    }
    out.width = kMapThumbnailSide;
    out.height = kMapThumbnailSide;
    out.rgb.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 3u);
    for (size_t i = 0; i < out.rgb.size(); i += 3) {
        out.rgb[i] = kMapThumbnailFloor[0];
        out.rgb[i + 1] = kMapThumbnailFloor[1];
        out.rgb[i + 2] = kMapThumbnailFloor[2];
    }
    const int32_t x0 = (kMapThumbnailSide - preview.width) / 2;                // (SetupLayout::preview_picture: centred, the rounding down)
    const int32_t y0 = (kMapThumbnailSide - preview.height) / 2;
    for (int32_t y = 0; y < preview.height; ++y) {
        for (int32_t x = 0; x < preview.width; ++x) {
            const uint8_t* src = &preview.rgba[(static_cast<size_t>(y) * static_cast<size_t>(preview.width) + static_cast<size_t>(x)) * 4u];
            uint8_t* dst = &out.rgb[(static_cast<size_t>(y0 + y) * static_cast<size_t>(out.width) + static_cast<size_t>(x0 + x)) * 3u];
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    }
    if (out.engine) out.why.clear();
    return out;
}

}  // namespace ants::app
