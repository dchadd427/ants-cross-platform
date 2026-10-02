#include "ants_app/map_preview.hpp"

#include <algorithm>

#include "ants_app/minimap_tables.hpp"
#include "ants_sim/grid.hpp"

namespace ants::app {

MapPreviewSize map_preview_size(uint32_t cols, uint32_t rows, int32_t inner) noexcept {
    MapPreviewSize size;
    if (cols == 0 || rows == 0 || inner <= 0) return size;
    const int64_t longest = std::max<int64_t>(cols, rows);
    const int64_t scale = inner / longest;
    if (scale >= 2) {
        size.width = static_cast<int32_t>(static_cast<int64_t>(cols) * scale);
        size.height = static_cast<int32_t>(static_cast<int64_t>(rows) * scale);
        size.scale = static_cast<int32_t>(scale);
        size.sampled = false;
        return size;
    }
    size.width = static_cast<int32_t>(std::max<int64_t>(1, static_cast<int64_t>(cols) * inner / longest));
    size.height = static_cast<int32_t>(std::max<int64_t>(1, static_cast<int64_t>(rows) * inner / longest));
    size.scale = 0;
    size.sampled = true;
    return size;
}

MapPreview render_map_preview(const assets::LevelData& level, const std::array<assets::ColorRGBA, 256>& palette, const MapPreviewSize& size) {
    MapPreview out;
    sim::Grid grid;
    if (size.width <= 0 || size.height <= 0 || !grid.init_from_level(level)) return out;
    const int64_t cols = level.width();
    const int64_t rows = level.height();
    if (cols <= 0 || rows <= 0) return out;
    out.width = size.width;
    out.height = size.height;
    out.scale = size.scale;
    out.sampled = size.sampled;
    out.cols = static_cast<uint32_t>(cols);
    out.rows = static_cast<uint32_t>(rows);
    out.players = static_cast<uint32_t>(grid.anthills().size());

    // the colour of every cell: an object of the minimap's table shows its colour (a tile id without a record: colour 0), every other cell the flat colour of its terrain class
    std::vector<assets::ColorRGBA> cell_colour(static_cast<size_t>(cols * rows));
    for (int64_t y = 0; y < rows; ++y) {
        for (int64_t x = 0; x < cols; ++x) {
            const sim::TileCell& cell = grid.get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
            const uint16_t id = cell.interactive_id;
            uint8_t index;
            if (id != sim::TILE_EMPTY && id != 0xFFFFu && !minimap_is_bomb(id)) {
                index = static_cast<uint8_t>(minimap_object_entry(id) & 0xffu);
            } else {
                const uint8_t cls = minimap_class(cell);
                index = kMinimapClassColours[cls < 6 ? cls : 5][0];
            }
            cell_colour[static_cast<size_t>(y * cols + x)] = palette[index];
        }
    }

    const int64_t w = size.width;
    const int64_t h = size.height;
    out.rgba.assign(static_cast<size_t>(w * h) * 4u, 255);
    for (int64_t py = 0; py < h; ++py) {
        const int64_t cy = py * rows / h;
        for (int64_t px = 0; px < w; ++px) {
            const int64_t cx = px * cols / w;
            const assets::ColorRGBA& c = cell_colour[static_cast<size_t>(cy * cols + cx)];
            uint8_t* dst = &out.rgba[static_cast<size_t>(py * w + px) * 4u];
            dst[0] = c.r;
            dst[1] = c.g;
            dst[2] = c.b;
        }
    }

    // the dots of the plants (flowers, clover): a square of `size flag` cells centred on the plant's cell, in the colour of the minimap's table (a size flag of 0 draws nothing)
    for (const auto& plant : grid.plants()) {
        const uint16_t entry = minimap_object_entry(plant.tile_id);
        const int64_t flag = entry >> 8;
        if (flag <= 0) continue;
        const assets::ColorRGBA& c = palette[entry & 0xffu];
        const int64_t dot_w = std::max<int64_t>(1, flag * w / cols);
        const int64_t dot_h = std::max<int64_t>(1, flag * h / rows);
        const int64_t x0 = static_cast<int64_t>(plant.x) * w / cols - ((flag - 1) * w / cols) / 2;
        const int64_t y0 = static_cast<int64_t>(plant.y) * h / rows - ((flag - 1) * h / rows) / 2;
        for (int64_t dy = 0; dy < dot_h; ++dy) {
            for (int64_t dx = 0; dx < dot_w; ++dx) {
                const int64_t X = x0 + dx;
                const int64_t Y = y0 + dy;
                if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                uint8_t* dst = &out.rgba[static_cast<size_t>(Y * w + X) * 4u];
                dst[0] = c.r;
                dst[1] = c.g;
                dst[2] = c.b;
            }
        }
    }
    return out;
}

MapPreview render_map_preview(const assets::LevelData& level, const std::array<assets::ColorRGBA, 256>& palette, int32_t scale) {
    if (scale < 1) return MapPreview{};
    MapPreviewSize size;
    size.width = static_cast<int32_t>(static_cast<int64_t>(level.width()) * scale);
    size.height = static_cast<int32_t>(static_cast<int64_t>(level.height()) * scale);
    size.scale = scale;
    size.sampled = false;
    return render_map_preview(level, palette, size);
}

MapPreview render_map_preview_in_box(const assets::LevelData& level, const std::array<assets::ColorRGBA, 256>& palette, int32_t inner) {
    return render_map_preview(level, palette, map_preview_size(level.width(), level.height(), inner));
}

std::string map_preview_caption(const MapPreview& preview) {
    std::string text = std::to_string(preview.cols) + " x " + std::to_string(preview.rows) + " cells";
    if (preview.players > 0) text += " \xC2\xB7 " + std::to_string(preview.players) + (preview.players == 1 ? " player" : " players");
    return text;
}

}  // namespace ants::app
