#pragma once

// The picture of a map that the wide setup screen shows (setup_layout.hpp). It is the map as the game draws it: render_map_preview_world sets the level up as for a new match (the same
// SimulationEngine and level loader, all four start positions, so that every hill shows), has the game's own renderer draw the whole world once into an offscreen image (1 world pixel per
// pixel: Renderer::render_world_image: terrain, water, grass and plants, the hills in their team colours, food, power-ups, rocks and every other object; no HUD, no fog, no cursor, and no
// ants: see below) and reduces it to the preview box with an exact area filter (box_downscale: every output pixel is the average of the area of the world image that it covers, weighted by
// how much of each source pixel it covers, averaged in linear light, not in the gamma-encoded values). The aspect of the map is kept: a map that is not square is centred in the box.
//
// The ants are left out: the starting ants (three at each hill's mouth, at the first tick) are 30 pixel sprites that become 4 to 6 pixel blobs in the box; they hide the paths and the
// ground beside the hills, and the picture is of the MAP, not of a match.
//
// When the offscreen image cannot be made (a renderer without render targets, a level that is not a grid, a start marker outside the grid) the preview is the older minimap-colour picture
// (render_map_preview_in_box: the selected map's .LVL drawn with the colours of the match screen's minimap, minimap_tables.hpp: one flat colour per terrain class and the colour of every
// object by its tile id; where the picture has room for two pixels of every cell it is drawn at a whole scale per cell, a map too big for that is sampled, nearest neighbour, into the box).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"

namespace ants::app {

class IRenderer;
struct WorldImage;

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
    bool rendered{false};        // made from the game's own drawing of the world (render_map_preview_world); false: the minimap-colour picture
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

/// The size of the picture of a world in a box of inner x inner pixels that keeps the map's proportion: the longer side of the map is exactly `inner` pixels, the shorter one is rounded down
/// (at least 1). A map of cols x rows cells is as big as its world pixels (cols * 32 x rows * 32) in proportion, so the cells are enough. An empty size for an empty map or a box without room.
/// width / height only: scale 0, not sampled.
MapPreviewSize map_preview_fit(uint32_t cols, uint32_t rows, int32_t inner) noexcept;

/// The exact area (box) filter: the RGBA8 image `src` of src_w x src_h pixels reduced (or enlarged) to dst_w x dst_h. An output pixel covers a rectangle of the source whose sides are
/// src_w / dst_w and src_h / dst_h source pixels; its colour is the average of that rectangle, every source pixel weighted by the area of it that the rectangle covers (whole pixels
/// inside, fractions at the edges), and the average is taken in LINEAR light: every colour channel is converted from sRGB to linear (a 16 bit table), summed in integers with the exact
/// areas (the same result on every compiler and machine: no floating point), divided by the area and converted back to the nearest sRGB byte. A flat area keeps its colour exactly.
/// Alpha of the result is 255. Returns dst_w * dst_h * 4 bytes, empty when a size is not positive, the source is null or a side is over 65535.
std::vector<uint8_t> box_downscale(const uint8_t* src, int32_t src_w, int32_t src_h, int32_t dst_w, int32_t dst_h);

/// The preview of a rendered world image of a map of cols x rows cells with `players` hills in a box of inner x inner pixels: map_preview_fit and box_downscale
MapPreview downscale_world_image(const WorldImage& image, uint32_t cols, uint32_t rows, uint32_t players, int32_t inner);

/// The preview that the game draws: `level` is set up as for a new match (a SimulationEngine started with every team that the level can be played by: all four for a level that is
/// playable by four), `renderer` draws its whole world offscreen (IRenderer::render_world_image) and the image is reduced to the box (downscale_world_image). An invalid MapPreview, and the
/// reason in `why` when it is given, when that cannot be done (the renderer cannot draw offscreen, the level cannot be played by any team). `max_target_side` is passed on to the renderer
/// (0: what the device allows). Nothing of a running match is touched: the engine is a new one and the renderer puts back what it had.
MapPreview render_map_preview_world(IRenderer& renderer, const assets::LevelData& level, int32_t inner, std::string* why = nullptr, int32_t max_target_side = 0);

/// The line under the picture: "60 x 60 cells · 4 players" (the middle dot is U+00B7; "1 player"; no player count for a map without a hill)
std::string map_preview_caption(const MapPreview& preview);

}  // namespace ants::app
