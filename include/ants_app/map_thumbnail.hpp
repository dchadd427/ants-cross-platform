#pragma once

// The picture of a map that the game server keeps for the maps of players: the picture that the setup screen shows in its preview box, which is also what the
// library's pictures (tools/map_previews.py) are cut from, so that a submitted map looks like every other map: the game's own drawing of the map (render_map_preview_world) reduced to the box,
// centred on the box's dark floor. A map that cannot be drawn offscreen (too big for one image, or a renderer without render targets) gets what the setup screen shows for it, the picture in the
// colors of the match screen's minimap (render_map_preview_in_box). Made by a small program that links this library; nothing here needs a window.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"

namespace ants::app {

class IRenderer;

/// The side of the picture: the inner side of the setup screen's preview box (SetupLayout::preview_inner of the 960 x 540 screen), which the library's pictures have
constexpr int32_t kMapThumbnailSide = 300;
/// The floor of that box (the color round a map that is not square), as the library's pictures show it
constexpr uint8_t kMapThumbnailFloor[3] = {7, 11, 15};

struct MapThumbnail {
    int32_t width{0};                // kMapThumbnailSide, or 0 when nothing could be drawn
    int32_t height{0};
    bool engine{false};              // drawn by the game's own renderer; false: the minimap-color picture
    std::string why;                 // what the renderer said when it could not draw the map (empty when it did)
    std::vector<uint8_t> rgb;        // width x height pixels of 3 bytes
    bool valid() const noexcept { return width > 0 && height > 0 && rgb.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 3u; }
};

/// The picture of `level`. Invalid when the level has no grid that either drawing can read (the setup screen shows "No preview" then).
MapThumbnail make_map_thumbnail(IRenderer& renderer, const assets::LevelData& level, const std::array<assets::ColorRGBA, 256>& palette);

}  // namespace ants::app
