#include "ants_app/wide_page.hpp"

#include <algorithm>
#include <array>

namespace ants::app {

namespace {

// The side strips of the frame (96 x 16 pieces stacked as whole pieces, five times 96 rows, then a short run from the piece's start and one jump at a row identical to its neighbour): 508 rows.
// (Found by the mock-up tool of the setup screen: a search for the fewest jumps between identical lines.)
constexpr LineSpan kFrameLeftSpans[] = {{0, 96, 96}, {0, 96, 96}, {0, 96, 96}, {0, 96, 96}, {0, 96, 96}, {0, 25, 25}, {45, 3, 3}};
constexpr LineSpan kFrameRightSpans[] = {{0, 96, 96}, {0, 96, 96}, {0, 96, 96}, {0, 96, 96}, {0, 96, 96}, {0, 23, 23}, {24, 5, 5}};

template <size_t N>
constexpr int32_t total_lines(const LineSpan (&spans)[N]) {
    int32_t sum = 0;
    for (size_t i = 0; i < N; ++i) sum += spans[i].dst_count;
    return sum;
}

#define ANTS_FRAME_STRIP(id, sprite, spans) PieceStrip{id, sprite, false, 0, 0, total_lines(spans), spans, sizeof(spans) / sizeof(spans[0])}

const std::array<PieceStrip, 2> kStrips = {{
    ANTS_FRAME_STRIP("frame.left", "dfram496.bmp", kFrameLeftSpans),
    ANTS_FRAME_STRIP("frame.right", "dfram596.bmp", kFrameRightSpans),
}};

#undef ANTS_FRAME_STRIP

}  // namespace

const PieceStrip* wide_page_strips(size_t& count) noexcept {
    count = kStrips.size();
    return kStrips.data();
}

const PieceStrip& find_strip(const PieceStrip* table, size_t count, const char* id) {
    for (size_t i = 0; i < count; ++i) {
        const char* a = table[i].id;
        const char* b = id;
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') return table[i];
    }
    return table[0];
}

void draw_strip(IRenderer& renderer, const ants::assets::AssetArchive& archive, const PieceStrip& s, int32_t x, int32_t y) {
    const int32_t id = archive.find_sprite_id(s.sprite);
    if (id < 0) return;
    const auto& sprite = archive.get_sprite(static_cast<uint32_t>(id));
    const int32_t cross = s.cross_count > 0 ? s.cross_count : (s.columns ? static_cast<int32_t>(sprite.height) : static_cast<int32_t>(sprite.width));
    int32_t pos = 0;
    for (size_t i = 0; i < s.span_count; ++i) {
        const LineSpan& span = s.spans[i];
        if (s.columns) renderer.draw_sprite_region(static_cast<uint32_t>(id), x + pos, y, span.dst_count, cross, span.src, s.cross_first, span.src_count, cross);
        else renderer.draw_sprite_region(static_cast<uint32_t>(id), x, y + pos, cross, span.dst_count, s.cross_first, span.src, cross, span.src_count);
        pos += span.dst_count;
    }
}

void draw_piece(IRenderer& renderer, const char* name, int32_t x, int32_t y) { renderer.draw_named_sprite(name, x, y); }

void draw_top_line(IRenderer& renderer, const ants::assets::AssetArchive& archive, int32_t x, int32_t y, int32_t length) {
    const int32_t id = archive.find_sprite_id("efram1100.bmp");
    if (id < 0 || length <= 0) return;
    renderer.draw_sprite_region(static_cast<uint32_t>(id), x, y, length, 4, 0, 0, 1, 4);
}

// The clay: dclay96 at its own pitch of 96 over the whole picture (the last row of tiles is cut at the picture's edge; the tile has isolated speckles only, so no join can show), or one flat field
void draw_wide_clay(IRenderer& renderer, const ants::assets::AssetArchive& archive, PageClay clay) {
    if (clay == PageClay::Flat) {
        renderer.fill_rect(0, 0, kWidePageWidth, kWidePageHeight, kFlatClay);
        return;
    }
    const int32_t id = archive.find_sprite_id("dclay96.bmp");
    if (id < 0) return;
    for (int32_t y = 0; y < kWidePageHeight; y += 96) {
        for (int32_t x = 0; x < kWidePageWidth; x += 96) {
            const int32_t w = std::min(96, kWidePageWidth - x);
            const int32_t h = std::min(96, kWidePageHeight - y);
            renderer.draw_sprite_region(static_cast<uint32_t>(id), x, y, w, h, 0, 0, w, h);
        }
    }
}

// The frame: the four corners, the top (nine pieces of 96 then four of 16), the bottom (four, two 16 pieces, three, two 16 pieces, two) and the two side strips
void draw_wide_frame(IRenderer& renderer, const ants::assets::AssetArchive& archive) {
    constexpr int32_t kW = kWidePageWidth;
    constexpr int32_t kH = kWidePageHeight;
    draw_piece(renderer, "dfram1.bmp", 0, 0);
    draw_piece(renderer, "dfram3.bmp", kW - 16, 0);
    draw_piece(renderer, "dfram6.bmp", 0, kH - 16);
    draw_piece(renderer, "dfram8.bmp", kW - 16, kH - 16);
    int32_t x = 16;
    for (int i = 0; i < 9; ++i) {
        draw_piece(renderer, "dfram296.bmp", x, 0);
        x += 96;
    }
    for (int i = 0; i < 4; ++i) {
        draw_piece(renderer, "dfram2.bmp", x, 0);
        x += 16;
    }
    x = 16;
    const int groups[3] = {4, 3, 2};
    for (int g = 0; g < 3; ++g) {
        for (int i = 0; i < groups[g]; ++i) {
            draw_piece(renderer, "dfram796.bmp", x, kH - 16);
            x += 96;
        }
        if (g < 2) {
            for (int i = 0; i < 2; ++i) {
                draw_piece(renderer, "dfram7.bmp", x, kH - 16);
                x += 16;
            }
        }
    }
    draw_strip(renderer, archive, find_strip(kStrips.data(), kStrips.size(), "frame.left"), 0, 16);
    draw_strip(renderer, archive, find_strip(kStrips.data(), kStrips.size(), "frame.right"), kW - 16, 16);
}

void draw_wide_background(IRenderer& renderer, const ants::assets::AssetArchive& archive, PageClay clay) {
    draw_wide_clay(renderer, archive, clay);
    draw_wide_frame(renderer, archive);
}

}  // namespace ants::app
