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

        if (!unit || !unit->is_alive()) {
            it = active_flights_.erase(it);
            continue;
        }

        f.current_tick++;
        if (f.total_ticks == 0) f.total_ticks = 1;

        int32_t cur_px = f.start_px + ((f.target_px - f.start_px) * f.current_tick) / f.total_ticks;
        int32_t cur_py = f.start_py + ((f.target_py - f.start_py) * f.current_tick) / f.total_ticks;
        int32_t z = (4 * f.apex_height_px * f.current_tick * (f.total_ticks - f.current_tick)) /
                    (f.total_ticks * f.total_ticks);

        unit->set_pixel_pos(cur_px, cur_py);
        unit->altitude_z = z;

        int32_t tx = unit->pos.x;
        int32_t ty = unit->pos.y;
        int32_t inc_dx = (f.target_px > f.start_px) ? 1 : ((f.target_px < f.start_px) ? -1 : 0);
        int32_t inc_dy = (f.target_py > f.start_py) ? 1 : ((f.target_py < f.start_py) ? -1 : 0);

        // Check obstacle collision
        if (!grid.in_bounds(tx, ty) || grid.is_solid_obstacle(tx, ty)) {
            unit->altitude_z = 0;
            audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_A, cur_px, cur_py, 1, 255});
            resolve_landing(*unit, grid, audio_out, prng, inc_dx, inc_dy);
            it = active_flights_.erase(it);
            continue;
        }

        if (f.current_tick >= f.total_ticks) {
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
    audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_B, unit.pixel_x, unit.pixel_y, 0, 255});
    audio_out.push_back(AudioEvent{SOUND_STUN, unit.pixel_x, unit.pixel_y, 0, 255});
    unit.start_stun(STUN_RECOVERY_TICKS);
}

void PhysicsEngine::resolve_water_entry(AntUnit& unit,
                                        std::vector<AudioEvent>& audio_out) {
    if (unit.type == AntType::Swimmer) {
        unit.state = UnitState::Swimming;
        audio_out.push_back(AudioEvent{SOUND_SPLASH, unit.pixel_x, unit.pixel_y, 1, 255});
    } else {
        unit.start_drowning();
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
