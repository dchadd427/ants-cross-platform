// The dense mud on the screen (world_art_mud.hpp; docs/WORLD_ART.md, "Dense mud"): a cell of a big mud field is drawn from a picture that has its looks laid over the world art's mud, made when the cell
// comes into view and kept. The looks of a cell (sampled twice for each world pixel) are made first, within a budget of work for each frame, and the picture of the cell as it is now (a frame of a bubble, the
// plain mud) is baked from them at the scale of the output. A cell that is not ready is drawn as the world art has it. Everything is a function of the map and the clock: the same on every machine.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include "ants_app/renderer.hpp"

namespace ants::app {

namespace {

constexpr uint32_t kBigBubbleStep = 10000;             // the number of a bigger bubble's picture: 10000 + the sprite (medium), 20000 + the sprite (large)

uint64_t picture_key(uint32_t number, uint8_t variant) { return (static_cast<uint64_t>(number) << 8) | variant; }

}  // namespace

void Renderer::drop_mud_caches() {
    for (auto& entry : mud_baked_) {
        if (entry.second.texture) SDL_DestroyTexture(entry.second.texture);
    }
    mud_baked_.clear();
    mud_looks_.clear();
    mud_baked_bytes_ = 0;
    mud_looks_bytes_ = 0;
}

// The depth of the mud (the distance in tiles from the nearest cell that is not plain mud), once for a map
void Renderer::ensure_mud_depth() {
    if (mud_depth_ready_) return;
    mud_depth_ready_ = true;
    const size_t n = ground_map_.kind.size();
    if (n == 0) return;
    std::vector<uint8_t> mud(n, 0);
    for (size_t i = 0; i < n; ++i) mud[i] = (ground_map_.kind[i] == 'm' && ground_map_.plain[i] != 0) ? 1 : 0;
    mud_depth_.build(ground_map_.width, ground_map_.height, mud);
}

double Renderer::mud_depth_at_cell(int32_t col, int32_t row) {
    ensure_mud_depth();
    if (col < 0 || row < 0 || col >= ground_map_.width || row >= ground_map_.height) return 0.0;
    const size_t at = static_cast<size_t>(row) * static_cast<size_t>(ground_map_.width) + static_cast<size_t>(col);
    if (ground_map_.kind[at] != 'm' || ground_map_.plain[at] == 0) return 0.0;      // (the depth means something in plain mud only)
    return mud_depth_.cell(col, row);
}

// The RGB pictures of the mud at a scale, cut from the sheet (which is decoded for it and let go): the plain mud, its bubble frames, and the bigger bubbles, every variant
bool Renderer::ensure_mud_pictures(int32_t scale) {
    const size_t k = static_cast<size_t>(scale - 1);
    if (mud_pictures_state_[k] != 0) return mud_pictures_state_[k] == 1;
    mud_pictures_state_[k] = 2;                                                  // (until it works: a sheet that fails is not read at every cell)
    DecodedImage sheet;
    if (world_art_ == nullptr || !world_art_->sheet_pixels(scale, sheet)) return false;
    for (const world_art::Picture& p : world_art_->pictures()) {
        const uint32_t base = p.number % kBigBubbleStep;
        const bool big = p.number >= kBigBubbleStep;
        if (big ? (base < world_art::mud::kBubbleFirst || base > world_art::mud::kBubbleLast) : !world_art::mud::is_mud_sprite(p.number)) continue;
        if (p.w != 32 || p.h != 32) continue;
        std::vector<uint8_t> rgb(static_cast<size_t>(32 * scale) * static_cast<size_t>(32 * scale) * 3u);
        for (int32_t y = 0; y < 32 * scale; ++y) {
            for (int32_t x = 0; x < 32 * scale; ++x) {
                const uint8_t* src = &sheet.rgba[(static_cast<size_t>(p.y[k] + y) * static_cast<size_t>(sheet.width) + static_cast<size_t>(p.x[k] + x)) * 4u];
                uint8_t* dst = &rgb[(static_cast<size_t>(y) * static_cast<size_t>(32 * scale) + static_cast<size_t>(x)) * 3u];
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
            }
        }
        mud_pictures_[k][picture_key(p.number, p.variant)] = std::move(rgb);
    }
    mud_pictures_state_[k] = 1;
    return true;
}

// The looks of a cell: kept, or made now when the frame has work left (at least one set of looks in every frame); null when they are not there yet
const world_art::mud::CellLooks* Renderer::mud_cell_looks(int32_t col, int32_t row) {
    const uint32_t cell = static_cast<uint32_t>(row) * static_cast<uint32_t>(ground_map_.width) + static_cast<uint32_t>(col);
    auto it = mud_looks_.find(cell);
    if (it != mud_looks_.end()) {
        it->second.used = mud_frame_;
        return &it->second.looks;
    }
    const bool unlimited = mud_budget_ms_ < 0;
    if (!unlimited && mud_made_ > 0 && mud_spent_us_ >= static_cast<int64_t>(mud_budget_ms_) * 1000) return nullptr;
    const uint64_t t0 = SDL_GetPerformanceCounter();
    MudLooks made;
    made.looks = world_art::mud::CellLooks::compute(mud_depth_, col, row);
    made.used = mud_frame_;
    mud_spent_us_ += static_cast<int64_t>((SDL_GetPerformanceCounter() - t0) * 1000000ull / SDL_GetPerformanceFrequency());
    ++mud_made_;
    mud_looks_bytes_ += made.looks.bytes() + sizeof(MudLooks);
    const auto inserted = mud_looks_.emplace(cell, std::move(made));
    if (mud_looks_bytes_ > mud_cache_limit_) evict_mud(false);
    return &inserted.first->second.looks;
}

// Lets go of what was used least recently (not in this frame) until what is kept is under a share of the limit
void Renderer::evict_mud(bool baked) {
    const size_t limit = mud_cache_limit_ - mud_cache_limit_ / 10;
    if (baked) {
        std::vector<std::pair<uint64_t, uint64_t>> order;
        for (const auto& e : mud_baked_) {
            if (e.second.used != mud_frame_) order.emplace_back(e.second.used, e.first);
        }
        std::sort(order.begin(), order.end());
        for (const auto& o : order) {
            if (mud_baked_bytes_ <= limit) break;
            const auto it = mud_baked_.find(o.second);
            if (it->second.texture) SDL_DestroyTexture(it->second.texture);
            mud_baked_bytes_ -= it->second.bytes;
            mud_baked_.erase(it);
        }
    } else {
        std::vector<std::pair<uint64_t, uint32_t>> order;
        for (const auto& e : mud_looks_) {
            if (e.second.used != mud_frame_) order.emplace_back(e.second.used, e.first);
        }
        std::sort(order.begin(), order.end());
        for (const auto& o : order) {
            if (mud_looks_bytes_ <= limit) break;
            const auto it = mud_looks_.find(o.second);
            mud_looks_bytes_ -= it->second.looks.bytes() + sizeof(MudLooks);
            mud_looks_.erase(it);
        }
    }
}

// Draws a cell of a big mud field with its looks, from the picture that its template shows now; false (the caller draws the world art's picture) when the cell is not of that kind, is shallow, its looks
// are not ready, or a sheet cannot be had
bool Renderer::draw_dense_mud_cell(int32_t col, int32_t row, int32_t anim_id, int32_t sx, int32_t sy) {
    using namespace world_art::mud;
    if (!dense_mud_ || world_art_ == nullptr || !archive_ || anim_id < 0 || static_cast<size_t>(anim_id) >= archive_->animation_count()) return false;
    ensure_mud_depth();
    if (mud_depth_.empty() || !mud_depth_.any_deep() || col < 0 || row < 0 || col >= ground_map_.width || row >= ground_map_.height) return false;
    const size_t at = static_cast<size_t>(row) * static_cast<size_t>(ground_map_.width) + static_cast<size_t>(col);
    if (ground_map_.kind[at] != 'm' || ground_map_.plain[at] == 0) return false;
    if (mud_depth_.deepest(col, row) + kWander < kFirstLook) return false;       // a bank, a small field: the approved picture
    const auto& seq = archive_->get_animation(static_cast<uint32_t>(anim_id));
    if (seq.subitems.empty()) return false;
    const auto& sub = seq.subitems[std::min(template_frame_index(anim_id), seq.subitems.size() - 1)];
    if (sub.frames.size() != 1 || sub.frames[0].dx != 0 || sub.frames[0].dy != 0 || !is_mud_sprite(sub.frames[0].sprite_index)) return false;
    const uint32_t native = sub.frames[0].sprite_index;
    const BubbleChoice choice = choose_bubble(col, row, native, template_now_ms_, mud_depth_.cell(col, row));
    const uint32_t number = choice.size == 0 ? choice.sprite : choice.sprite + static_cast<uint32_t>(choice.size) * kBigBubbleStep;
    int32_t scale = world_art_scale_now();
    while (scale >= 1 && world_art_texture(scale) == nullptr) --scale;             // (the sheet that the ground is drawn from: a device that cannot take the sheet has the original's mud as well)
    if (scale < 1) return false;
    const uint8_t variant = world_variant_;
    const uint64_t key = (static_cast<uint64_t>(at) << 32) | (static_cast<uint64_t>(number) << 8) | (static_cast<uint64_t>(variant) << 2) | static_cast<uint64_t>(scale - 1);
    auto baked = mud_baked_.find(key);
    if (baked == mud_baked_.end()) {
        if (!ensure_mud_pictures(scale)) return false;
        const auto& pictures = mud_pictures_[static_cast<size_t>(scale - 1)];
        const world_art::Picture* shown = world_art_->find(number, variant);
        const world_art::Picture* plain = world_art_->find(kPlainMud, variant);
        if (shown == nullptr || plain == nullptr) return false;
        const auto rgb = pictures.find(picture_key(shown->number, shown->variant));
        const auto base = pictures.find(picture_key(plain->number, plain->variant));
        if (rgb == pictures.end() || base == pictures.end()) return false;
        const CellLooks* looks = mud_cell_looks(col, row);
        if (looks == nullptr) return false;                                       // not made yet: the approved picture for now
        if (looks->identity() && number == native) return false;                  // nothing changes in this cell
        const std::vector<uint8_t> pixels = apply_looks(*looks, rgb->second.data(), base->second.data(), scale);
        const int32_t n = 32 * scale;
        SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, n, n);
        if (!tex) return false;
        SDL_UpdateTexture(tex, nullptr, pixels.data(), n * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
        MudBaked entry;
        entry.texture = tex;
        entry.bytes = pixels.size();
        entry.used = mud_frame_;
        mud_baked_bytes_ += entry.bytes;
        baked = mud_baked_.emplace(key, entry).first;
        if (mud_baked_bytes_ > mud_cache_limit_) evict_mud(true);
        baked = mud_baked_.find(key);
        if (baked == mud_baked_.end()) return false;
    }
    baked->second.used = mud_frame_;
    world_art_filter(baked->second.texture, scale);
    const SDL_Rect dst = placed(sx, sy, 32, 32);
    SDL_RenderCopy(renderer_, baked->second.texture, nullptr, &dst);
    return true;
}

}  // namespace ants::app
