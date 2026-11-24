// The automatic transitions on the screen (world_art_edges.hpp; docs/WORLD_ART.md, "Transitions"): the transition of a cell is a picture made when the cell comes into view (about 4 ms), kept in a bounded memory
// and laid over the cell's ground in the ground's own pass, so under every object, ant and fog. A cell that is not ready is drawn as the ground alone. Everything is a function of the map: the same on every machine.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include "ants_app/renderer.hpp"

namespace ants::app {

namespace {

// The plain tiles whose pictures the higher kind shows inside its edge: the plain mud, gravel, dirt and slate (sprite numbers of the original)
constexpr uint32_t kPlainSprite[world_art::edges::kKinds] = {0, 640, 726, 728, 812};

}  // namespace

void Renderer::drop_edge_caches() {
    for (auto& entry : edge_overlays_) {
        if (entry.second.texture) SDL_DestroyTexture(entry.second.texture);
    }
    edge_overlays_.clear();
    edge_bytes_ = 0;
}

size_t Renderer::transition_cells() const noexcept {
    size_t n = 0;
    for (const auto& e : edge_overlays_) n += e.second.texture != nullptr ? 1u : 0u;
    return n;
}

void Renderer::ensure_edge_map() {
    if (edge_ready_) return;
    edge_ready_ = true;
    edge_map_ = world_art::edges::edge_map_of(ground_map_);
    edge_has_.assign(edge_map_.kind.size(), 0);
    for (int32_t y = 0; y < edge_map_.height; ++y) {
        for (int32_t x = 0; x < edge_map_.width; ++x) edge_has_[static_cast<size_t>(y) * static_cast<size_t>(edge_map_.width) + static_cast<size_t>(x)] = world_art::edges::has_transition(edge_map_, x, y) ? 1 : 0;
    }
}

bool Renderer::cell_has_transition(int32_t col, int32_t row) {
    ensure_edge_map();
    if (col < 0 || row < 0 || col >= edge_map_.width || row >= edge_map_.height) return false;
    return edge_has_[static_cast<size_t>(row) * static_cast<size_t>(edge_map_.width) + static_cast<size_t>(col)] != 0;
}

// The RGB pictures of the plain tiles at a scale, cut from the sheet (which is decoded for it and let go)
bool Renderer::ensure_edge_pictures(int32_t scale) {
    const size_t k = static_cast<size_t>(scale - 1);
    if (edge_pictures_state_[k] != 0) return edge_pictures_state_[k] == 1;
    edge_pictures_state_[k] = 2;                                                  // (until it works: a sheet that fails is not read at every cell)
    DecodedImage sheet;
    if (world_art_ == nullptr || !world_art_->sheet_pixels(scale, sheet)) return false;
    for (size_t kind = 1; kind < world_art::edges::kKinds; ++kind) {
        const world_art::Picture* p = world_art_->find(kPlainSprite[kind], 0);
        if (p == nullptr || p->w != 32 || p->h != 32) return false;
        std::vector<uint8_t> rgb(static_cast<size_t>(32 * scale) * static_cast<size_t>(32 * scale) * 3u);
        for (int32_t y = 0; y < 32 * scale; ++y) {
            for (int32_t x = 0; x < 32 * scale; ++x) {
                const uint8_t* src = &sheet.rgba[(static_cast<size_t>(p->y[k] + y) * static_cast<size_t>(sheet.width) + static_cast<size_t>(p->x[k] + x)) * 4u];
                uint8_t* dst = &rgb[(static_cast<size_t>(y) * static_cast<size_t>(32 * scale) + static_cast<size_t>(x)) * 3u];
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
            }
        }
        edge_pictures_[k][kind] = std::move(rgb);
    }
    edge_pictures_state_[k] = 1;
    return true;
}

// Makes room for `bytes` more in the transitions' memory: lets go of what was used least recently, never what this frame used. False when that is not enough: the cell then has no transition
bool Renderer::make_room_for_edges(size_t bytes) {
    if (edge_bytes_ + bytes <= edge_cache_limit_) return true;
    std::vector<std::pair<uint64_t, uint64_t>> order;
    for (const auto& e : edge_overlays_) {
        if (e.second.used != mud_frame_ && e.second.bytes > 0) order.emplace_back(e.second.used, e.first);
    }
    std::sort(order.begin(), order.end());
    for (const auto& o : order) {
        if (edge_bytes_ + bytes <= edge_cache_limit_) break;
        const auto it = edge_overlays_.find(o.second);
        if (it->second.texture) SDL_DestroyTexture(it->second.texture);
        edge_bytes_ -= it->second.bytes;
        edge_overlays_.erase(it);
    }
    return edge_bytes_ + bytes <= edge_cache_limit_;
}

// Draws the transition of a cell over its ground, when it has one and it is ready
void Renderer::draw_transition(int32_t col, int32_t row, int32_t sx, int32_t sy) {
    if (!transitions_ || world_art_ == nullptr) return;
    ensure_edge_map();
    if (col < 0 || row < 0 || col >= edge_map_.width || row >= edge_map_.height) return;
    const size_t at = static_cast<size_t>(row) * static_cast<size_t>(edge_map_.width) + static_cast<size_t>(col);
    if (edge_has_[at] == 0) return;
    int32_t scale = world_art_scale_now();
    while (scale >= 1 && world_art_texture(scale) == nullptr) --scale;             // (the sheet that the ground is drawn from)
    if (scale < 1) return;
    scale = std::min(scale, edge_max_scale_);
    const uint64_t key = (static_cast<uint64_t>(at) << 8) | static_cast<uint64_t>(scale);
    auto it = edge_overlays_.find(key);
    if (it == edge_overlays_.end()) {
        if (!mud_work_left() || !ensure_edge_pictures(scale)) return;
        const size_t bytes = static_cast<size_t>(4) * static_cast<size_t>(32 * scale) * static_cast<size_t>(32 * scale);
        if (!make_room_for_edges(bytes)) return;                                    // (no room: no transition for this cell, the ground alone)
        const uint64_t t0 = SDL_GetPerformanceCounter();
        world_art::edges::EdgeTextures textures;
        for (size_t kind = 1; kind < world_art::edges::kKinds; ++kind) textures.rgb[kind] = edge_pictures_[static_cast<size_t>(scale - 1)][kind].data();
        const std::vector<uint8_t> pixels = world_art::edges::make_overlay(edge_map_, col, row, scale, textures);
        EdgeOverlay entry;
        entry.used = mud_frame_;
        if (!pixels.empty()) {
            const int32_t n = 32 * scale;
            SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, n, n);
            if (tex) {
                SDL_UpdateTexture(tex, nullptr, pixels.data(), n * 4);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                entry.texture = tex;
                entry.bytes = pixels.size();
                edge_bytes_ += entry.bytes;
            }
        }
        it = edge_overlays_.emplace(key, entry).first;
        mud_work_done(t0);
    }
    it->second.used = mud_frame_;
    if (it->second.texture == nullptr) return;
    world_art_filter(it->second.texture, scale);
    const SDL_Rect dst = placed(sx, sy, 32, 32);
    SDL_RenderCopy(renderer_, it->second.texture, nullptr, &dst);
}

}  // namespace ants::app
