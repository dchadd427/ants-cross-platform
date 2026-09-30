#include "ants_assets/object_footprint.hpp"

#include <algorithm>

namespace ants::assets {

namespace {

struct Rect {
    int32_t left{0}, top{0}, right{0}, bottom{0};
};

// Win32 IntersectRect: an empty result is all zero.
Rect intersect(const Rect& a, const Rect& b) {
    Rect o{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    if (o.left >= o.right || o.top >= o.bottom) return Rect{};
    return o;
}

} // namespace

std::vector<FootprintOffset> compute_object_footprint(const AssetArchive& archive, uint32_t tile_id) {
    std::vector<FootprintOffset> out;
    if (tile_id >= archive.animation_count()) return out;
    const AnimationSequence& seq = archive.get_animation(tile_id);
    if (seq.subitems.empty()) return out;
    const AnimationSubItem& sub = seq.subitems.front();

    // The original's integer divisions truncate toward zero; a positive anchor keeps every coordinate positive, where that
    // is a floor, and the cells outside the map that a real anchor near an edge would drop are dropped by the caller.
    constexpr int32_t kAnchor = 100;
    const int32_t ox = kAnchor * 32;
    const Rect box{sub.box_left + ox, sub.box_top + ox, sub.box_right + ox, sub.box_bottom + ox};
    // FUN_010079e4: tile rectangle of a pixel rectangle (the right and bottom edges are inclusive pixels)
    const int32_t c0 = box.left / 32;
    const int32_t c1 = (box.right + 32) / 32;
    const int32_t r0 = box.top / 32;
    const int32_t r1 = (box.bottom + 32) / 32;

    if (sub.frames.empty()) {
        for (int32_t r = r0; r < r1; ++r)
            for (int32_t c = c0; c < c1; ++c) out.push_back(FootprintOffset{c - kAnchor, r - kAnchor});
        return out;
    }
    // The first part of the frame (the head of the part list, 0x10298fc): the part's image at the anchor plus its offset
    const AnimationFrame& part = sub.frames.front();
    const Sprite& sp = archive.get_sprite(part.sprite_index);
    const Rect part_rect{part.dx + ox, part.dy + ox, part.dx + static_cast<int32_t>(sp.width) + ox,
                         part.dy + static_cast<int32_t>(sp.height) + ox};
    for (int32_t r = r0; r < r1; ++r) {
        for (int32_t c = c0; c < c1; ++c) {
            const Rect cell{c * 32, r * 32, (c + 1) * 32, (r + 1) * 32};
            const Rect t = intersect(intersect(part_rect, cell), box);
            bool opaque = false;
            for (int32_t y = t.top; y < t.bottom && !opaque; ++y) {
                for (int32_t x = t.left; x < t.right; ++x) {
                    const int32_t sx = x - part_rect.left;
                    const int32_t sy = y - part_rect.top;
                    if (sx < 0 || sy < 0 || sx >= static_cast<int32_t>(sp.width) || sy >= static_cast<int32_t>(sp.height)) continue;
                    if (sp.pixels[static_cast<size_t>(sy) * sp.pitch + static_cast<size_t>(sx)] != CHD_COLOR_KEY_INDEX) {
                        opaque = true;
                        break;
                    }
                }
            }
            if (opaque) out.push_back(FootprintOffset{c - kAnchor, r - kAnchor});
        }
    }
    return out;
}

} // namespace ants::assets
