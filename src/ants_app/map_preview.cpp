#include "ants_app/map_preview.hpp"

#include <algorithm>
#include <memory>

#include "ants_app/minimap_tables.hpp"
#include "ants_app/renderer.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

namespace {

// sRGB -> linear light, 16 bit: round(65535 * s2l(v / 255)) with s2l(c) = c / 12.92 for c <= 0.04045, ((c + 0.055) / 1.055)^2.4 above (IEC 61966-2-1). The tables are made once, from doubles, and
// written out, so that the filter has no floating point in it: the same bytes on every compiler and machine.
constexpr uint16_t kSrgbToLinear[256] = {
    0, 20, 40, 60, 80, 99, 119, 139, 159, 179, 199, 219, 241, 264, 288, 313,
    340, 367, 396, 427, 458, 491, 526, 562, 599, 637, 677, 718, 761, 805, 851, 898,
    947, 997, 1048, 1101, 1156, 1212, 1270, 1330, 1391, 1453, 1517, 1583, 1651, 1720, 1790, 1863,
    1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504, 2592, 2681, 2773, 2866, 2961, 3058, 3157, 3258,
    3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129, 4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124,
    5257, 5392, 5530, 5669, 5810, 5953, 6099, 6246, 6395, 6547, 6700, 6856, 7014, 7174, 7335, 7500,
    7666, 7834, 8004, 8177, 8352, 8528, 8708, 8889, 9072, 9258, 9445, 9635, 9828, 10022, 10219, 10417,
    10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090, 12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909,
    14146, 14387, 14629, 14874, 15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
    18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481, 21787, 22096, 22407, 22721,
    23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325, 25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094,
    28452, 28813, 29176, 29542, 29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
    34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138, 39572, 40009, 40449, 40891,
    41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534, 45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359,
    48850, 49344, 49841, 50341, 50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
    57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221, 63795, 64372, 64952, 65535,
};

// the linear values (16 bit) at which the sRGB byte k becomes k + 1: round(65535 * s2l((k + 0.5) / 255)), k = 0 .. 254. A linear value v is the sRGB byte that counts the thresholds <= v:
// the byte whose sRGB value is nearest (rounding in the encoded values, as every converter does), and kSrgbToLinear[b] maps back to b for every b (a flat colour stays exact)
constexpr uint16_t kLinearThreshold[255] = {
    10, 30, 50, 70, 90, 109, 129, 149, 169, 189, 209, 230, 252, 276, 300, 326,
    353, 382, 411, 442, 475, 508, 543, 580, 618, 657, 697, 739, 783, 828, 874, 922,
    971, 1022, 1075, 1129, 1184, 1241, 1300, 1360, 1422, 1485, 1550, 1617, 1685, 1755, 1826, 1900,
    1975, 2051, 2130, 2210, 2292, 2375, 2460, 2547, 2636, 2727, 2819, 2914, 3010, 3107, 3207, 3309,
    3412, 3517, 3624, 3733, 3844, 3957, 4071, 4188, 4306, 4427, 4549, 4673, 4800, 4928, 5058, 5190,
    5325, 5461, 5599, 5739, 5881, 6026, 6172, 6320, 6471, 6623, 6778, 6935, 7093, 7254, 7417, 7582,
    7750, 7919, 8090, 8264, 8440, 8618, 8798, 8980, 9165, 9351, 9540, 9731, 9925, 10120, 10318, 10518,
    10720, 10924, 11131, 11340, 11551, 11765, 11981, 12199, 12419, 12642, 12867, 13094, 13324, 13556, 13790, 14027,
    14266, 14508, 14751, 14998, 15246, 15497, 15750, 16006, 16264, 16525, 16788, 17053, 17321, 17591, 17864, 18139,
    18416, 18696, 18979, 19264, 19551, 19841, 20134, 20429, 20726, 21026, 21329, 21634, 21941, 22251, 22564, 22879,
    23197, 23517, 23840, 24165, 24493, 24824, 25157, 25493, 25831, 26172, 26516, 26862, 27211, 27562, 27916, 28273,
    28632, 28994, 29359, 29726, 30096, 30469, 30844, 31222, 31603, 31986, 32372, 32761, 33153, 33547, 33944, 34344,
    34746, 35151, 35559, 35970, 36383, 36799, 37218, 37640, 38064, 38492, 38922, 39354, 39790, 40228, 40670, 41114,
    41560, 42010, 42463, 42918, 43376, 43837, 44301, 44768, 45237, 45709, 46185, 46663, 47144, 47628, 48114, 48604,
    49097, 49592, 50091, 50592, 51096, 51603, 52113, 52626, 53142, 53661, 54183, 54707, 55235, 55766, 56299, 56836,
    57375, 57918, 58463, 59012, 59563, 60118, 60675, 61235, 61799, 62365, 62935, 63507, 64083, 64661, 65243,
};

/// linear (16 bit) -> sRGB byte, a table of 65536 made from the thresholds
const std::array<uint8_t, 65536>& linear_to_srgb_table() {
    static const std::array<uint8_t, 65536> table = [] {
        std::array<uint8_t, 65536> t{};
        size_t k = 0;
        for (size_t v = 0; v < t.size(); ++v) {
            while (k < 255 && v >= kLinearThreshold[k]) ++k;
            t[v] = static_cast<uint8_t>(k);
        }
        return t;
    }();
    return table;
}

/// The source pixels that one output pixel of an axis covers, and the area of each in units of 1 / dst of a source pixel (so the weights of an output pixel add up to `src` exactly)
struct AxisSpan {
    int32_t first{0};
    int32_t count{0};
    size_t offset{0};            // into the shared weights
};

void build_axis(int32_t src, int32_t dst, std::vector<AxisSpan>& spans, std::vector<uint32_t>& weights) {
    spans.resize(static_cast<size_t>(dst));
    weights.clear();
    for (int64_t i = 0; i < dst; ++i) {
        const int64_t lo = i * src;                        // the output pixel covers [lo, hi) in units of 1 / dst source pixel
        const int64_t hi = lo + src;
        const int64_t j0 = lo / dst;
        const int64_t j1 = (hi - 1) / dst;
        AxisSpan& span = spans[static_cast<size_t>(i)];
        span.first = static_cast<int32_t>(j0);
        span.count = static_cast<int32_t>(j1 - j0 + 1);
        span.offset = weights.size();
        for (int64_t j = j0; j <= j1; ++j) weights.push_back(static_cast<uint32_t>(std::min((j + 1) * dst, hi) - std::max(j * dst, lo)));
    }
}

}  // namespace

std::vector<uint8_t> box_downscale(const uint8_t* src, int32_t src_w, int32_t src_h, int32_t dst_w, int32_t dst_h) {
    if (src == nullptr || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 || src_w > 65535 || src_h > 65535 || dst_w > 65535 || dst_h > 65535) return {};
    std::vector<AxisSpan> xs;
    std::vector<AxisSpan> ys;
    std::vector<uint32_t> xw;
    std::vector<uint32_t> yw;
    build_axis(src_w, dst_w, xs, xw);
    build_axis(src_h, dst_h, ys, yw);
    const size_t row_len = static_cast<size_t>(dst_w) * 3u;

    // pass 1, along x: for every source row the weighted sum of the linear values under each output column (at most src_w * 65535 < 2^32)
    std::vector<uint32_t> across(static_cast<size_t>(src_h) * row_len);
    for (int32_t y = 0; y < src_h; ++y) {
        const uint8_t* row = src + static_cast<size_t>(y) * static_cast<size_t>(src_w) * 4u;
        uint32_t* out_row = &across[static_cast<size_t>(y) * row_len];
        for (int32_t i = 0; i < dst_w; ++i) {
            const AxisSpan& span = xs[static_cast<size_t>(i)];
            const uint32_t* w = &xw[span.offset];
            uint32_t r = 0, g = 0, b = 0;
            const uint8_t* px = row + static_cast<size_t>(span.first) * 4u;
            for (int32_t j = 0; j < span.count; ++j, px += 4) {
                r += w[j] * kSrgbToLinear[px[0]];
                g += w[j] * kSrgbToLinear[px[1]];
                b += w[j] * kSrgbToLinear[px[2]];
            }
            out_row[static_cast<size_t>(i) * 3u] = r;
            out_row[static_cast<size_t>(i) * 3u + 1u] = g;
            out_row[static_cast<size_t>(i) * 3u + 2u] = b;
        }
    }

    // pass 2, along y: the weighted sum of those over the rows under each output row, divided by the whole area (src_w * src_h, rounded to nearest), back to sRGB
    const std::array<uint8_t, 65536>& to_srgb = linear_to_srgb_table();
    const uint64_t area = static_cast<uint64_t>(src_w) * static_cast<uint64_t>(src_h);
    std::vector<uint8_t> out(static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * 4u, 255);
    std::vector<uint64_t> sum(row_len);
    for (int32_t j = 0; j < dst_h; ++j) {
        const AxisSpan& span = ys[static_cast<size_t>(j)];
        std::fill(sum.begin(), sum.end(), uint64_t{0});
        for (int32_t k = 0; k < span.count; ++k) {
            const uint64_t w = yw[span.offset + static_cast<size_t>(k)];
            const uint32_t* in_row = &across[static_cast<size_t>(span.first + k) * row_len];
            for (size_t n = 0; n < row_len; ++n) sum[n] += w * in_row[n];
        }
        uint8_t* dst = &out[static_cast<size_t>(j) * static_cast<size_t>(dst_w) * 4u];
        for (int32_t i = 0; i < dst_w; ++i, dst += 4) {
            for (size_t c = 0; c < 3; ++c) {
                const uint64_t lin = (sum[static_cast<size_t>(i) * 3u + c] + area / 2) / area;
                dst[c] = to_srgb[static_cast<size_t>(std::min<uint64_t>(lin, 65535))];
            }
        }
    }
    return out;
}

MapPreviewSize map_preview_fit(uint32_t cols, uint32_t rows, int32_t inner) noexcept {
    MapPreviewSize size;
    if (cols == 0 || rows == 0 || inner <= 0) return size;
    const int64_t longest = std::max<int64_t>(cols, rows);
    size.width = static_cast<int32_t>(std::max<int64_t>(1, static_cast<int64_t>(cols) * inner / longest));
    size.height = static_cast<int32_t>(std::max<int64_t>(1, static_cast<int64_t>(rows) * inner / longest));
    return size;
}

MapPreview downscale_world_image(const WorldImage& image, uint32_t cols, uint32_t rows, uint32_t players, int32_t inner) {
    MapPreview out;
    const MapPreviewSize fit = map_preview_fit(cols, rows, inner);
    if (!image.valid() || fit.width <= 0 || fit.height <= 0) return out;
    out.rgba = box_downscale(image.rgba.data(), image.width, image.height, fit.width, fit.height);
    if (out.rgba.empty()) return out;
    out.width = fit.width;
    out.height = fit.height;
    out.cols = cols;
    out.rows = rows;
    out.players = players;
    out.rendered = true;
    return out;
}

namespace {

constexpr uint32_t kPreviewSeed = 1;

/// The teams that a preview of the level can show: all four when the level is playable by four (a match with every start position), else the teams it can be played by one at a time (a start
/// marker outside the grid is a crash in the original: the remake refuses such a map for a roster that has the team); 0 when none
uint8_t preview_roster(const assets::LevelData& level) {
    if (level.validate(0x0F).playable) return 0x0F;
    uint8_t roster = 0;
    for (uint8_t t = 0; t < 4; ++t) {
        if (level.validate(static_cast<uint8_t>(1u << t)).playable) roster = static_cast<uint8_t>(roster | (1u << t));
    }
    return roster;
}

}  // namespace

MapPreview render_map_preview_world(IRenderer& renderer, const assets::LevelData& level, int32_t inner, std::string* why, int32_t max_target_side) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr) *why = reason;
        return MapPreview{};
    };
    if (level.width() == 0 || level.height() == 0) return fail("the level is empty");
    const uint8_t roster = preview_roster(level);
    if (roster == 0) return fail("no team can play the level");
    // the level as load_match sets a match up: the teams that play, the engine started with them, the renderer given the same level (no hill art for a team without a player)
    const assets::LevelData shown = roster == 0x0F ? level : level.for_roster(roster);
    sim::SimulationEngine engine;
    engine.init(shown, kPreviewSeed, roster);
    sim::WorldState world = engine.get_world_state();
    world.ants.clear();                                                      // the picture is of the map (see the header)
    WorldImage image;
    if (!renderer.render_world_image(shown, world, engine.grid(), image, why, max_target_side)) return MapPreview{};
    return downscale_world_image(image, level.width(), level.height(), static_cast<uint32_t>(engine.grid().anthills().size()), inner);
}

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
