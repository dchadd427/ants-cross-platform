#pragma once

// The picture of a map that the wide setup screen shows (setup_layout.hpp): the selected map's .LVL drawn with the colours of the match screen's minimap (minimap_tables.hpp),
// one flat colour per terrain class (gravel, slate, water, mud, dirt) and the colour of every object by its tile id from the minimap's table: the four hills in their team colours,
// water, food and power-ups, bridges, toys and rocks, and a dot for every plant the table gives a size. Where the picture has room for two pixels of every cell it is drawn at a
// whole scale per cell (a cell is a square of scale x scale pixels: nothing is stretched unevenly); a map too big for that is sampled, nearest neighbour, into the box.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"

namespace ants::app {

/// How big the picture of a map of cols x rows cells is in a box of inner x inner pixels
struct MapPreviewSize {
    int32_t width{0};
    int32_t height{0};
    int32_t scale{0};            // pixels per cell when `sampled` is false, 0 otherwise
    bool sampled{false};         // nearest neighbour sampling of a map that is too big for two pixels per cell
    constexpr bool operator==(const MapPreviewSize& o) const noexcept { return width == o.width && height == o.height && scale == o.scale && sampled == o.sampled; }
};

/// The size of the picture: scale = inner / (the longer side in cells); from 2 up it is a whole scale (width = cols * scale, height = rows * scale), below that the map is sampled
/// so that its longer side is exactly `inner` pixels and the shorter one keeps the map's proportion (rounded down, at least 1). An empty size for an empty map or a box without room.
MapPreviewSize map_preview_size(uint32_t cols, uint32_t rows, int32_t inner) noexcept;

/// The picture: tightly packed RGBA8 (alpha 255), width x height pixels
struct MapPreview {
    int32_t width{0};
    int32_t height{0};
    int32_t scale{0};
    bool sampled{false};
    uint32_t cols{0};            // the map's size in cells
    uint32_t rows{0};
    uint32_t players{0};         // the hills that the map has (what the playing teams are)
    std::vector<uint8_t> rgba;
    bool valid() const noexcept { return width > 0 && height > 0 && rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u; }
};

/// The picture of a level at `size`: pixel (px, py) shows the cell (px * cols / width, py * rows / height), so a whole scale and a sampled picture are the same rule. `palette` is the
/// archive's (colour indices of the minimap's table). The level is read as the game reads it (sim::Grid::init_from_level).
MapPreview render_map_preview(const assets::LevelData& level, const std::array<assets::ColorRGBA, 256>& palette, const MapPreviewSize& size);
/// ... at a whole scale per cell (scale >= 1)
MapPreview render_map_preview(const assets::LevelData& level, const std::array<assets::ColorRGBA, 256>& palette, int32_t scale);
/// ... in a box of inner x inner pixels (map_preview_size)
MapPreview render_map_preview_in_box(const assets::LevelData& level, const std::array<assets::ColorRGBA, 256>& palette, int32_t inner);

/// The line under the picture: "60 x 60 cells · 4 players" (the middle dot is U+00B7; "1 player"; no player count for a map without a hill)
std::string map_preview_caption(const MapPreview& preview);

}  // namespace ants::app
