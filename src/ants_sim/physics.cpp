#include "ants_sim/physics.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"
#include <algorithm>
#include <cmath>

namespace ants::sim {

void PhysicsEngine::apply_knockback(AntUnit& victim,
                                    int32_t from_px,
                                    int32_t from_py,
                                    int32_t min_tiles,
                                    int32_t max_tiles,
                                    DamageSource source,
                                    std::vector<AudioEvent>& audio_out,
                                    uint16_t random_val) {
    BallisticFlight flight{};
    flight.unit_id = victim.id;
    flight.start_px = victim.pixel_x;
    flight.start_py = victim.pixel_y;
    flight.origin_source = source;
    flight.current_tick = 0;
    flight.total_ticks = 10;
    flight.apex_height_px = 36;

    int32_t dx = victim.pixel_x - from_px;
    int32_t dy = victim.pixel_y - from_py;
    if (dx == 0 && dy == 0) {
        dx = 1;
    }

    int32_t range = max_tiles - min_tiles + 1;
    int32_t tiles = min_tiles + (range > 1 ? static_cast<int32_t>(random_val % static_cast<uint16_t>(range)) : 0);
    int32_t dist_px = tiles * 32;

    int32_t dir_x = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
    int32_t dir_y = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);

    flight.target_px = flight.start_px + dir_x * dist_px;
    flight.target_py = flight.start_py + dir_y * dist_px;
    flight.total_distance_px = dist_px;
    flight.flight_dir = ants::assets::vector_to_direction(dir_x, dir_y);

    victim.state = UnitState::Knockback;
    // Keep victim facing orientation towards blast epicenter
    victim.anim_tick = 0;
    victim.anim_subitem = 0;
    audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_A, victim.pixel_x, victim.pixel_y, 1, 255});

    cancel_flight(victim.id);
    active_flights_.push_back(flight);
}

void PhysicsEngine::tick(std::vector<AntUnit*>& all_units,
                         Grid& grid,
                         std::vector<AudioEvent>& audio_out,
                         PRNG& prng) {
    for (auto it = active_flights_.begin(); it != active_flights_.end();) {
        BallisticFlight& f = *it;
        AntUnit* unit = nullptr;
        for (AntUnit* u : all_units) {
            if (u && u->id == f.unit_id) {
                unit = u;
                break;
            }
        }

        if (!unit || unit->state != UnitState::Knockback || !unit->is_alive()) {
            it = active_flights_.erase(it);
            continue;
        }

        // On first tick, resolve the final destination tile
        if (!f.destination_resolved) {
            f.destination_resolved = true;

            int32_t start_tx = (f.start_px >= 0) ? (f.start_px / 32) : ((f.start_px - 31) / 32);
            int32_t start_ty = (f.start_py >= 0) ? (f.start_py / 32) : ((f.start_py - 31) / 32);
            int32_t dir_x = (f.target_px > f.start_px) ? 1 : ((f.target_px < f.start_px) ? -1 : 0);
            int32_t dir_y = (f.target_py > f.start_py) ? 1 : ((f.target_py < f.start_py) ? -1 : 0);
            int32_t nominal_tiles = std::max(std::abs(f.target_px - f.start_px),
                                             std::abs(f.target_py - f.start_py)) / 32;
            if (nominal_tiles <= 0) {
                nominal_tiles = 4;
            }

            auto is_available = [&](int32_t tx, int32_t ty) -> bool {
                if (!grid.in_bounds(tx, ty)) return false;
                const auto& cell = grid.get_cell(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                return (cell.terrain_type != TERRAIN_OBSTACLE && !cell.is_obstacle_overlay);
            };

            int32_t nominal_tx = start_tx + dir_x * nominal_tiles;
            int32_t nominal_ty = start_ty + dir_y * nominal_tiles;

            if (is_available(nominal_tx, nominal_ty)) {
                // Available tile on the other side: intermediate terrain/water is flown over
                f.target_px = nominal_tx * 32 + 16;
                f.target_py = nominal_ty * 32 + 16;
            } else {
                // Nominal tile is not available (solid obstacle or out of bounds).
                // Trace backward along trajectory to find the last available tile before the obstacle.
                int32_t land_tx = start_tx;
                int32_t land_ty = start_ty;
                int32_t land_dist = 0;
                for (int32_t s = nominal_tiles - 1; s >= 0; --s) {
                    int32_t tx = start_tx + dir_x * s;
                    int32_t ty = start_ty + dir_y * s;
                    if (is_available(tx, ty)) {
                        land_tx = tx;
                        land_ty = ty;
                        land_dist = s;
                        break;
                    }
                }
                f.target_px = land_tx * 32 + 16;
                f.target_py = land_ty * 32 + 16;
                if (land_dist == 0) {
                    f.total_ticks = 1;
                    f.apex_height_px = 0;
                } else {
                    f.total_ticks = std::max<uint16_t>(2, static_cast<uint16_t>((land_dist * 10) / nominal_tiles));
                    f.apex_height_px = std::max(12, (36 * land_dist) / nominal_tiles);
                }
                audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_A, f.target_px, f.target_py, 1, 255});
            }
        }

        f.current_tick++;
        if (f.total_ticks == 0) f.total_ticks = 1;

        int32_t cur_px = f.start_px + ((f.target_px - f.start_px) * f.current_tick) / f.total_ticks;
        int32_t cur_py = f.start_py + ((f.target_py - f.start_py) * f.current_tick) / f.total_ticks;
        int32_t z = (4 * f.apex_height_px * f.current_tick * (f.total_ticks - f.current_tick)) /
                    (f.total_ticks * f.total_ticks);

        unit->set_pixel_pos(cur_px, cur_py);
        unit->altitude_z = z;
        unit->anim_tick = f.current_tick;
        unit->anim_subitem = f.current_tick;

        int32_t inc_dx = (f.target_px > f.start_px) ? 1 : ((f.target_px < f.start_px) ? -1 : 0);
        int32_t inc_dy = (f.target_py > f.start_py) ? 1 : ((f.target_py < f.start_py) ? -1 : 0);

        // Safety check for exiting map bounds
        if (!grid.in_bounds(unit->pos.x, unit->pos.y)) {
            unit->altitude_z = 0;
            resolve_landing(*unit, grid, audio_out, prng, inc_dx, inc_dy);
            it = active_flights_.erase(it);
            continue;
        }

        if (f.current_tick >= f.total_ticks) {
            unit->set_pixel_pos(f.target_px, f.target_py);
            unit->altitude_z = 0;
            resolve_landing(*unit, grid, audio_out, prng, inc_dx, inc_dy);
            it = active_flights_.erase(it);
            continue;
        }

        ++it;
    }
}

void PhysicsEngine::resolve_landing(AntUnit& unit,
                                    Grid& grid,
                                    std::vector<AudioEvent>& audio_out,
                                    PRNG& prng,
                                    int32_t incoming_dx,
                                    int32_t incoming_dy) {
    unit.altitude_z = 0;
    if (!grid.in_bounds(unit.pos.x, unit.pos.y)) {
        if (unit.hp == 0) unit.state = UnitState::Dead;
        return;
    }

    const auto& cell = grid.get_cell(unit.pos);

    // 1. Water Landing
    if (cell.terrain_type == TERRAIN_WATER && !cell.has_completed_bridge()) {
        resolve_water_entry(unit, audio_out);
        return;
    }

    // 2. Fire Landing
    if (cell.has_fire()) {
        resolve_fire_contact(unit, grid, audio_out, prng, incoming_dx, incoming_dy);
        return;
    }

    // 3. Ground Landing
    if (unit.hp == 0) {
        unit.state = UnitState::Bounce;
        unit.state_timer = 10;
        unit.anim_tick = 0;
        unit.anim_subitem = 0;
        audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_B, unit.pixel_x, unit.pixel_y, 0, 255});
    } else {
        audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_B, unit.pixel_x, unit.pixel_y, 0, 255});
        audio_out.push_back(AudioEvent{SOUND_STUN, unit.pixel_x, unit.pixel_y, 0, 255});
        unit.start_stun(STUN_RECOVERY_TICKS);
    }
}

void PhysicsEngine::resolve_water_entry(AntUnit& unit,
                                        std::vector<AudioEvent>& audio_out) {
    if (unit.type == AntType::Swimmer) {
        unit.state = UnitState::Swimming;
        unit.in_water = true;
        unit.was_in_water = true;
        audio_out.push_back(AudioEvent{SOUND_SPLASH, unit.pixel_x, unit.pixel_y, 1, 255});
    } else {
        unit.start_drowning();
        unit.in_water = true;
        unit.was_in_water = true;
        unit.set_tile_pos(unit.pos.x, unit.pos.y);
        unit.facing = ants::assets::Direction::South;
        audio_out.push_back(AudioEvent{SOUND_SPLASH, unit.pixel_x, unit.pixel_y, 1, 255});
        audio_out.push_back(AudioEvent{SOUND_DROWN, unit.pixel_x, unit.pixel_y, 2, 255});
        unit.clear_inventory();
    }
}

void PhysicsEngine::resolve_fire_contact(AntUnit& unit,
                                         Grid& grid,
                                         std::vector<AudioEvent>& audio_out,
                                         [[maybe_unused]] PRNG& prng,
                                         int32_t incoming_dx,
                                         int32_t incoming_dy) {
    if (unit.type == AntType::Fire) {
        return;
    }

    int loop_guard = 0;
    while (grid.in_bounds(unit.pos.x, unit.pos.y) &&
           grid.get_cell(unit.pos).has_fire() &&
           unit.is_alive() && loop_guard < 10) {
        loop_guard++;

        // Contact damage (+1 fire damage)
        unit.take_damage(1, DamageSource::FireBurn, 0);
        if (!unit.is_alive()) {
            return;
        }

        // Reflection trajectory: bounce back opposite of incoming movement
        int32_t b_dx = -incoming_dx;
        int32_t b_dy = -incoming_dy;
        if (b_dx == 0 && b_dy == 0) {
            b_dx = -1;
        }

        int32_t nx = unit.pos.x + b_dx;
        int32_t ny = unit.pos.y + b_dy;
        // If backward bounce is valid, not solid, and not another fire, bounce there
        if (grid.in_bounds(nx, ny) && !grid.is_solid_obstacle(nx, ny) && !grid.get_cell(TileCoord{nx, ny}).has_fire()) {
            unit.set_tile_pos(nx, ny);
            incoming_dx = b_dx;
            incoming_dy = b_dy;
        } else {
            // Deflect forward / away from fire
            int32_t def_x = unit.pos.x - b_dx;
            int32_t def_y = unit.pos.y - b_dy;
            if (grid.in_bounds(def_x, def_y) && !grid.is_solid_obstacle(def_x, def_y)) {
                unit.set_tile_pos(def_x, def_y);
                incoming_dx = -b_dx;
                incoming_dy = -b_dy;
            } else {
                // Try perpendicular deflection escape
                int32_t perp_x = unit.pos.x + b_dy;
                int32_t perp_y = unit.pos.y + b_dx;
                if (grid.in_bounds(perp_x, perp_y) && !grid.is_solid_obstacle(perp_x, perp_y)) {
                    unit.set_tile_pos(perp_x, perp_y);
                    incoming_dx = b_dy;
                    incoming_dy = b_dx;
                } else {
                    break;
                }
            }
        }
    }

    audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_A, unit.pixel_x, unit.pixel_y, 1, 255});
    audio_out.push_back(AudioEvent{SOUND_STUN, unit.pixel_x, unit.pixel_y, 0, 255});
    unit.start_stun(STUN_RECOVERY_TICKS);
}

const BallisticFlight* PhysicsEngine::get_active_flight(uint32_t unit_id) const noexcept {
    for (const auto& f : active_flights_) {
        if (f.unit_id == unit_id) return &f;
    }
    return nullptr;
}

void PhysicsEngine::cancel_flight(uint32_t unit_id) noexcept {
    for (auto it = active_flights_.begin(); it != active_flights_.end(); ++it) {
        if (it->unit_id == unit_id) {
            active_flights_.erase(it);
            return;
        }
    }
}

} // namespace ants::sim
