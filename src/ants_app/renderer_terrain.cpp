// The terrain of the renderer: the template animation of a cell, the layers of the map (ground, objects, food, bombs, fire walls, bridges, power-ups, anthills), the dropping flowers, the fog of war and the
// effects of the world. The camera, the world pass at a zoom and the frame are in renderer.cpp, the ants and the markers over them in renderer_units.cpp.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>

#include "ants_app/renderer.hpp"

namespace ants::app {

// ============================================================================
// Terrain layer 1
// ============================================================================

void Renderer::render_map_layers(const ants::sim::Grid& grid, const ants::sim::WorldState* world) {
    if (!renderer_) return;
    const float zoom_kept = camera_.zoom;
    const bool target = (zoomed() || force_world_target_) && begin_world_target(static_cast<int64_t>(grid.width()) * TILE_SIZE, static_cast<int64_t>(grid.height()) * TILE_SIZE);
    if (!target && zoomed()) camera_.zoom = zoom::kNormal;       // (as in render_world: the zoom 1 picture rather than a broken one)
    const LayoutRect covered = map_view_rect(grid.width(), grid.height());
    const SDL_Rect clip_rect = placed(covered.x, covered.y, covered.w, covered.h);
    SDL_RenderSetClipRect(renderer_, &clip_rect);
    render_terrain_layer1(grid);
    render_terrain_layer2_structures(grid, world);
    restore_clip();
    if (target) {
        end_world_target();
        restore_clip();
    }
    camera_.zoom = zoom_kept;
}

void Renderer::render_terrain_layer1(const ants::sim::Grid& grid) {
    const LayoutRect view = world_view();
    int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE);
    int32_t end_col   = std::min(static_cast<int32_t>(grid.width()) - 1,
                                 (static_cast<int32_t>(camera_.x) + view.w + 31) / TILE_SIZE);
    int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE);
    int32_t end_row   = std::min(static_cast<int32_t>(grid.height()) - 1,
                                 (static_cast<int32_t>(camera_.y) + view.h + 31) / TILE_SIZE);

    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            const auto& cell = grid.get_cell(static_cast<uint32_t>(c), static_cast<uint32_t>(r));
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(c * TILE_SIZE, r * TILE_SIZE, sx, sy);

            if (level_set_) {
                // The cell value is the id of the tile's Table-4 animation. Every id has ONE template started at map
                // load, so all cells of an id show the same frame at the same time (Ants.exe FUN_0102c1fc /
                // FUN_0102b997). Ids without a template draw nothing, as in the original.
                const int32_t id = (cell.terrain_id < tile_anim_id_.size()) ? tile_anim_id_[cell.terrain_id] : -1;
                if (id >= 0) draw_template_screen(id, sx, sy);
                continue;
            }

            // Synthetic grids without a loaded level: flat colors per terrain category
            const SDL_Rect dst = placed(sx, sy, TILE_SIZE, TILE_SIZE);
            if (cell.terrain_type == ants::sim::TERRAIN_WATER) {
                SDL_SetRenderDrawColor(renderer_, 23, 71, 151, 255); // Navy Water
            } else if (cell.terrain_type == ants::sim::TERRAIN_OBSTACLE) {
                SDL_SetRenderDrawColor(renderer_, 47, 51, 63, 255);  // Dark Rock
            } else if (cell.is_mud) {
                SDL_SetRenderDrawColor(renderer_, 65, 65, 65, 255);  // Dark Mud
            } else {
                SDL_SetRenderDrawColor(renderer_, 135, 120, 110, 255); // Tan Gravel Ground
            }
            SDL_RenderFillRect(renderer_, &dst);
        }
    }
}

// ============================================================================
// Terrain layer 2 (objects, food, bombs, fire walls, bridges, power-ups, anthills)
// ============================================================================

int32_t Renderer::food_stage_anim(uint16_t tile) const {
    if (tile < tile_anim_id_.size() && tile_anim_id_[tile] >= 0) return tile_anim_id_[tile];
    return (archive_ && tile < archive_->animation_count()) ? static_cast<int32_t>(tile) : -1;
}

void Renderer::draw_static_object(const StaticMapObject& obj, const ants::sim::Grid& grid,
                                  const ants::sim::WorldState* world) {
    int32_t anim_id = obj.anim_id;
    if (obj.is_food) {
        // Food shows the template of its current stage tile at the same anchor (SetTile keeps the anchor): the anchor cell holds it while any is left.
        if (!grid.in_bounds(obj.anchor_x, obj.anchor_y)) return;
        const auto& anchor = grid.get_cell(obj.anchor_x, obj.anchor_y);
        if (!anchor.has_food()) return; // All food in this item has been gathered

        // Fog: food is one of the four classes that an unexplored ANCHOR hides (FUN_01008089 0x10071dd); the explored cells of its footprint draw it from their own turn
        if (world && world->fog_of_war_enabled && !world->is_tile_revealed(obj.anchor_x, obj.anchor_y)) return;

        const int32_t stage = food_stage_anim(anchor.interactive_id);
        if (stage >= 0) anim_id = stage;
    }
    if (anim_id < 0) return;
    draw_template_world(anim_id, static_cast<int32_t>(obj.anchor_x) * TILE_SIZE, static_cast<int32_t>(obj.anchor_y) * TILE_SIZE);
}

void Renderer::draw_object_from_body_cell(const ants::sim::Grid& grid, const ants::sim::WorldState& world, int32_t col, int32_t row) {
    const auto& cell = grid.get_cell(static_cast<uint32_t>(col), static_cast<uint32_t>(row));
    if (cell.is_empty_overlay()) return;
    const int32_t ax = cell.anchor_x, ay = cell.anchor_y;
    if (ax < 0 || ay < 0 || !grid.in_bounds(ax, ay) || (ax == col && ay == row)) return;       // an anchor, or a cell without anchor bytes, draws itself
    const size_t index = static_cast<size_t>(row) * map_width_ + static_cast<size_t>(col);
    if (index < object_index_by_cell_.size() && object_index_by_cell_[index] >= 0) return;    // the anchor of a static object whose bytes do not name itself (the editor leaves 0, 0)
    if (!world.is_tile_revealed(col, row) || world.is_tile_revealed(ax, ay)) return;
    const size_t anchor_index = static_cast<size_t>(ay) * map_width_ + static_cast<size_t>(ax);
    if (anchor_index < object_index_by_cell_.size() && object_index_by_cell_[anchor_index] >= 0) {
        const StaticMapObject& obj = static_decor_objects_[static_cast<size_t>(object_index_by_cell_[anchor_index])];
        int32_t anim_id = obj.anim_id;
        if (obj.is_food) {                                                                       // the tile that this cell of the current stage shows
            const int32_t stage = food_stage_anim(cell.interactive_id);
            if (stage >= 0) anim_id = stage;
        }
        if (anim_id >= 0) draw_template_world(anim_id, ax * TILE_SIZE, ay * TILE_SIZE);
        return;
    }
    if (has_anthill_bases_) {                                                                    // a hill: the colony whose anchor it is
        for (size_t t = 0; t < 4; ++t) {
            if (hill_anchor_cell_[t].x == ax && hill_anchor_cell_[t].y == ay) {
                draw_template_world(anim_id_hill_[t], ax * TILE_SIZE, ay * TILE_SIZE);
                return;
            }
        }
    }
}

void Renderer::render_terrain_layer2_structures(const ants::sim::Grid& grid, const ants::sim::WorldState* world) {
    // One row-major pass over anchor cells (FUN_01008089 with mode 2): the rows from top / 32 - 3 up to (bottom / 32 + 3 + 1, exclusive) of the view rectangle, the columns
    // likewise, clipped to the map. An object whose anchor lies further out is not drawn even when its art reaches into the view (docs 5.54); each draw is rect-culled.
    constexpr int32_t kMargin = 3;
    const LayoutRect view = world_view();
    const int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE - kMargin);
    const int32_t end_col   = std::min(static_cast<int32_t>(grid.width()) - 1,
                                       (static_cast<int32_t>(camera_.x) + view.w) / TILE_SIZE + kMargin);
    const int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE - kMargin);
    const int32_t end_row   = std::min(static_cast<int32_t>(grid.height()) - 1,
                                       (static_cast<int32_t>(camera_.y) + view.h) / TILE_SIZE + kMargin);

    const bool fog = world && world->fog_of_war_enabled;
    auto explored = [&](int32_t c, int32_t r) { return !fog || world->is_tile_revealed(c, r); };

    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            // 1. Static object anchored on this cell
            if (level_set_ && !object_index_by_cell_.empty()) {
                const size_t idx = static_cast<size_t>(r) * map_width_ + static_cast<size_t>(c);
                if (idx < object_index_by_cell_.size() && object_index_by_cell_[idx] >= 0) {
                    draw_static_object(static_decor_objects_[static_cast<size_t>(object_index_by_cell_[idx])], grid, world);
                }
            }

            // 2. Anthills anchored on this cell (animated, always drawn: the fog pass covers unexplored ground)
            if (has_anthill_bases_) {
                for (size_t t = 0; t < 4; ++t) {
                    if (hill_anchor_cell_[t].x == c && hill_anchor_cell_[t].y == r) {
                        draw_template_world(anim_id_hill_[t], c * TILE_SIZE, r * TILE_SIZE);
                    }
                }
            }

            // 2b. A cell of an object that is explored while the object's anchor is not: the object is drawn from here (FUN_01008089's second path)
            if (fog && level_set_) draw_object_from_body_cell(grid, *world, c, r);

            // 3. Runtime item on this cell
            const auto& cell = grid.get_cell(static_cast<uint32_t>(c), static_cast<uint32_t>(r));
            if (cell.is_empty_overlay()) continue;
            const int32_t wx = c * TILE_SIZE;
            const int32_t wy = r * TILE_SIZE;

            // Bridges: not part of the original's fog-hidden set
            if ((cell.interactive_id >= ants::sim::TILE_BRIDGE1 && cell.interactive_id <= ants::sim::TILE_BRIDGE4) ||
                cell.interactive_id == ants::sim::TILE_BRIDGE4B) {
                draw_template_world(cell.interactive_id, wx, wy);
                continue;
            }

            // Fire walls, bombs, food, power-ups are hidden while their anchor tile is unexplored
            if (cell.has_fire()) {
                if (explored(c, r)) draw_template_world(anim_id_fire_, wx, wy);
                continue;
            }
            if (cell.has_bomb()) {
                if (explored(c, r)) draw_template_world(anim_id_bomb_[(cell.interactive_id - ants::sim::BOMB_BLACK) % 4u], wx, wy);
                continue;
            }
            if (cell.has_lunchbox()) {
                if (explored(c, r)) draw_template_world(anim_id_lunchbox_, wx, wy);
                continue;
            }
            if (cell.has_powerup()) {
                if (explored(c, r) && cell.powerup_type >= 1 && cell.powerup_type <= 5) {
                    draw_template_world(anim_id_powerup_[cell.powerup_type], wx, wy);
                }
            }
        }
    }

    // Fallback for custom test grids that only carry grid.anthills() (no layer-2 hill anchors)
    if (!has_anthill_bases_) {
        for (const auto& a : grid.anthills()) {
            if (fog && a.team_id != hud_team_id_ &&
                !world->is_tile_revealed(static_cast<int32_t>(a.x), static_cast<int32_t>(a.y))) {
                continue;
            }
            draw_template_world(anim_id_hill_[a.team_id % 4],
                                static_cast<int32_t>(a.x) * TILE_SIZE, static_cast<int32_t>(a.y) * TILE_SIZE);
        }
    }
}

void Renderer::collect_object_list_sprites() {
    if (!archive_ || !texture_cache_) return;
    for (const auto& spr : object_list_sprites_) {
        const AnimBounds& b = anim_bounds(spr.anim_id);
        if (!b.valid) continue;
        const int32_t cam_x = static_cast<int32_t>(camera_.x);
        const int32_t cam_y = static_cast<int32_t>(camera_.y);
        const LayoutRect view = world_view();
        if (spr.px + b.x1 <= cam_x || spr.px + b.x0 >= cam_x + view.w ||
            spr.py + b.y1 <= cam_y || spr.py + b.y0 >= cam_y + view.h) {
            continue;
        }
        RenderItem item{};
        item.sort_y = spr.py;   // original sort key = row*32+16 (cell centre)
        const int32_t anim_id = spr.anim_id, px = spr.px, py = spr.py;
        item.draw_func = [this, anim_id, px, py](SDL_Renderer*, TextureCache&) {
            this->draw_template_world(anim_id, px, py);
        };
        render_queue_.push_back(std::move(item));
    }
}

// Non-looping frame of an animation `elapsed_ms` after it started (holds the last frame); -1 once it is over.
static int32_t effect_frame_at(const ants::assets::AnimationSequence& seq, uint32_t elapsed_ms) {
    uint32_t end = 0;
    for (size_t i = 0; i < seq.subitems.size(); ++i) {
        end += seq.subitems[i].val3;
        if (elapsed_ms < end) return static_cast<int32_t>(i);
    }
    return -1;
}

void Renderer::collect_flower_droppers(const ants::sim::WorldState& world) {
    if (!archive_ || !texture_cache_) return;

    for (const auto& fd : world.flower_droppers) {
        // Falling power-up droplet (the flower itself is an object-list sprite). Effect sprite: anchored at the drop
        // tile's top-left, sort key row*32, hidden while that tile is unexplored (Ants.exe FUN_0100fdd8).
        if (!fd.is_dropping) continue;
        if (world.fog_of_war_enabled && !world.is_tile_revealed(fd.drop_x, fd.drop_y)) continue;
        const char* anim_name = "FD_COMB";
        switch (fd.powerup_type) {
            case 0: anim_name = "FD_BOMB"; break;
            case 1: anim_name = "FD_COMB"; break;
            case 2: anim_name = "FD_THIEF"; break;
            case 3: anim_name = "FD_SWIM"; break;
            case 4: anim_name = "FD_FIRE"; break;
            default: break;
        }
        const auto* drop_anim = archive_->find_animation(anim_name);
        if (!drop_anim || drop_anim->subitems.empty()) continue;
        int32_t frame = effect_frame_at(*drop_anim, fd.drop_elapsed_ms + sub_tick_ms_);
        if (frame < 0) frame = static_cast<int32_t>(drop_anim->subitems.size()) - 1;
        const int32_t wx = fd.drop_x * TILE_SIZE;
        const int32_t wy = fd.drop_y * TILE_SIZE;
        RenderItem item{};
        item.sort_y = wy;
        item.draw_func = [this, drop_anim, frame, wx, wy](SDL_Renderer*, TextureCache&) {
            const LayoutRect view = world_view();
            const int32_t sx = view.x + (wx - static_cast<int32_t>(camera_.x));
            const int32_t sy = view.y + (wy - static_cast<int32_t>(camera_.y));
            this->draw_frame_parts(drop_anim->subitems[static_cast<size_t>(frame)], sx, sy);
        };
        render_queue_.push_back(std::move(item));
    }
}

void Renderer::render_fog_of_war(const ants::sim::WorldState& world) {
    if (!world.fog_of_war_enabled || world.fog_revealed.empty() || !archive_ || !texture_cache_) {
        return;
    }

    const LayoutRect view = world_view();
    int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE);
    int32_t end_col   = std::min(static_cast<int32_t>(world.width) - 1,
                                 (static_cast<int32_t>(camera_.x) + view.w + 31) / TILE_SIZE);
    int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE);
    int32_t end_row   = std::min(static_cast<int32_t>(world.height) - 1,
                                 (static_cast<int32_t>(camera_.y) + view.h + 31) / TILE_SIZE);

    // Authentic 1998 Ants.exe dither anim lookup table (Ants.exe VA 0x1001a7a & 0x10087c0..0x10087da)
    // Formula: idx = ((c0 * 2 + c1) * 2 + 2 + c2) * 2 + c3
    // c0 = North revealed (1/0), c1 = East revealed, c2 = South revealed, c3 = West revealed
    // Sprites: dither0.bmp (352) through dither15.bmp (367)
    static constexpr uint16_t FOG_DITHER_SPRITE_LUT[20] = {
        0,   0,   0,   0,   // 0..3: unused
        365, // idx  4: N=0 E=0 S=0 W=0 -> dither13.bmp
        363, // idx  5: N=0 E=0 S=0 W=1 -> dither11.bmp
        362, // idx  6: N=0 E=0 S=1 W=0 -> dither10.bmp
        359, // idx  7: N=0 E=0 S=1 W=1 -> dither7.bmp
        364, // idx  8: N=0 E=1 S=0 W=0 -> dither12.bmp
        367, // idx  9: N=0 E=1 S=0 W=1 -> dither15.bmp
        358, // idx 10: N=0 E=1 S=1 W=0 -> dither6.bmp
        353, // idx 11: N=0 E=1 S=1 W=1 -> dither1.bmp
        361, // idx 12: N=1 E=0 S=0 W=0 -> dither9.bmp
        360, // idx 13: N=1 E=0 S=0 W=1 -> dither8.bmp
        366, // idx 14: N=1 E=0 S=1 W=0 -> dither14.bmp
        354, // idx 15: N=1 E=0 S=1 W=1 -> dither2.bmp
        357, // idx 16: N=1 E=1 S=0 W=0 -> dither5.bmp
        356, // idx 17: N=1 E=1 S=0 W=1 -> dither4.bmp
        355, // idx 18: N=1 E=1 S=1 W=0 -> dither3.bmp
        352  // idx 19: N=1 E=1 S=1 W=1 -> dither0.bmp
    };

    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            if (world.is_tile_revealed(c, r)) {
                continue; // Revealed tiles are not covered by fog
            }

            auto is_fog = [&](int32_t x, int32_t y) -> int32_t {
                if (x < 0 || x >= static_cast<int32_t>(world.width) ||
                    y < 0 || y >= static_cast<int32_t>(world.height)) {
                    return 1; // Out-of-bounds tiles are unrevealed fog
                }
                return world.is_tile_revealed(x, y) ? 0 : 1; // 1 = Fog, 0 = Revealed
            };

            int32_t c0 = is_fog(c, r - 1); // North
            int32_t c1 = is_fog(c + 1, r); // East
            int32_t c2 = is_fog(c, r + 1); // South
            int32_t c3 = is_fog(c - 1, r); // West

            int32_t idx = ((c0 * 2 + c1) * 2 + 2 + c2) * 2 + c3;
            if (idx < 4 || idx > 19) continue;

            uint32_t sprite_id = FOG_DITHER_SPRITE_LUT[idx];
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(c * TILE_SIZE, r * TILE_SIZE, sx, sy);
            draw_sprite(sprite_id, sx, sy);
        }
    }
}

void Renderer::collect_visual_effects(const ants::sim::WorldState& world) {
    if (!archive_ || !texture_cache_) return;
    for (const auto& eff : world.effects) {
        // Effect sprites of the original are hidden while their anchor tile is unexplored (vtable +0x3c, 0x101a40f)
        if (eff.fog_gated && world.fog_of_war_enabled && !world.is_tile_revealed(eff.px / TILE_SIZE, eff.py / TILE_SIZE)) continue;
        const auto* anim = archive_->find_animation(eff.anim_name);
        if (!anim && !eff.anim_name.empty()) {
            std::string low = eff.anim_name;
            for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            anim = archive_->find_animation(low);
        }
        if (!anim || anim->subitems.empty()) continue;

        size_t sub_idx = 0;
        if (eff.looping) {
            // The clip repeats until the simulation removes the effect (the dust ball of a foreign pile-up, Cloud54)
            uint32_t total = 0;
            for (const auto& sub : anim->subitems) total += sub.val3;
            const uint32_t t = (total > 0) ? (eff.elapsed_ms + sub_tick_ms_) % total : 0u;
            const int32_t f = effect_frame_at(*anim, t);
            sub_idx = (f < 0) ? anim->subitems.size() - 1 : static_cast<size_t>(f);
        } else {
            // Table-4 frame durations in real time: sim elapsed time plus the fraction of the current 50 ms tick
            const uint32_t elapsed_ms = eff.elapsed_ms + sub_tick_ms_;
            const uint32_t lifetime_ms = eff.duration_ms > 0 ? eff.duration_ms : static_cast<uint32_t>(eff.total_frames) * 50u;
            if (elapsed_ms >= lifetime_ms) continue; // the sprite ended at the end of its last frame
            const int32_t f = effect_frame_at(*anim, elapsed_ms);
            sub_idx = (f < 0) ? anim->subitems.size() - 1 : static_cast<size_t>(f);
        }
        RenderItem item{};
        item.sort_y = (eff.y_key != 0) ? eff.y_key : eff.py;
        const int32_t px = eff.px, py = eff.py;
        item.draw_func = [this, anim, sub_idx, px, py](SDL_Renderer*, TextureCache&) {
            const LayoutRect view = world_view();
            const int32_t sx = view.x + (px - static_cast<int32_t>(camera_.x));
            const int32_t sy = view.y + (py - static_cast<int32_t>(camera_.y));
            if (sx < view.x - 320 || sx > view.x + view.w + 320 ||
                sy < view.y - 320 || sy > view.y + view.h + 320) return;
            this->draw_frame_parts(anim->subitems[sub_idx], sx, sy);
        };
        render_queue_.push_back(std::move(item));
    }
}

void Renderer::spawn_transient_effect(const std::string& anim_name, int32_t px, int32_t py, bool is_screen_space) {
    if (anim_name == "xmarks") {
        // The click marker is a single object: a new click stops the previous marker (Ants.exe FUN_01010627)
        transient_effects_.erase(std::remove_if(transient_effects_.begin(), transient_effects_.end(),
                                                [](const TransientEffect& e) { return e.anim_name == "xmarks"; }),
                                 transient_effects_.end());
    }
    TransientEffect eff;
    eff.anim_name = anim_name;
    eff.px = px;
    eff.py = py;
    eff.elapsed_sec = 0.0f;
    eff.is_screen_space = is_screen_space;
    transient_effects_.push_back(std::move(eff));
}

void Renderer::update_transient_effects(float dt) {
    if (transient_effects_.empty() || !archive_) return;
    for (auto it = transient_effects_.begin(); it != transient_effects_.end();) {
        it->elapsed_sec += dt;
        const auto* anim = archive_->find_animation(it->anim_name);
        if (!anim && !it->anim_name.empty()) {
            std::string low = it->anim_name;
            for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            anim = archive_->find_animation(low);
        }
        if (!anim || anim->subitems.empty()) {
            it = transient_effects_.erase(it);
            continue;
        }
        float total_dur = 0.0f;
        for (const auto& sub : anim->subitems) {
            float sub_dur = (sub.val3 > 0) ? (static_cast<float>(sub.val3) / 1000.0f) : 0.060f;
            total_dur += sub_dur;
        }
        if (total_dur <= 0.0f) total_dur = 0.420f;
        if (it->elapsed_sec >= total_dur) {
            it = transient_effects_.erase(it);
        } else {
            ++it;
        }
    }
}

void Renderer::collect_transient_sprites() {
    if (!archive_ || !texture_cache_) return;
    for (const auto& eff : transient_effects_) {
        if (eff.is_screen_space) continue; // screen-space feedback is not part of the world sprite list
        const auto* anim = archive_->find_animation(eff.anim_name);
        if (!anim && !eff.anim_name.empty()) {
            std::string low = eff.anim_name;
            for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            anim = archive_->find_animation(low);
        }
        if (!anim || anim->subitems.empty()) continue;

        const uint32_t elapsed_ms = static_cast<uint32_t>(eff.elapsed_sec * 1000.0f);
        const int32_t f = effect_frame_at(*anim, elapsed_ms);
        const size_t sub_idx = (f < 0) ? anim->subitems.size() - 1 : static_cast<size_t>(f);
        OverlayItem item{};
        item.created_ms = overlay_now_ms() - static_cast<int64_t>(elapsed_ms);   // the click marker is a view child
        const int32_t px = eff.px, py = eff.py;
        item.draw = [this, anim, sub_idx, px, py]() {
            const LayoutRect view = world_view();
            const int32_t sx = view.x + (px - static_cast<int32_t>(camera_.x));
            const int32_t sy = view.y + (py - static_cast<int32_t>(camera_.y));
            this->draw_frame_parts(anim->subitems[sub_idx], sx, sy);
        };
        overlay_queue_.push_back(std::move(item));
    }
}

} // namespace ants::app
