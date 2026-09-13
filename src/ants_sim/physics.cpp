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
                                    uint16_t random_val,
                                    const Grid* grid) {
    BallisticFlight flight{};
    flight.unit_id = victim.id;
    flight.start_px = (source == DamageSource::BombBlast) ? from_px : victim.pixel_x;
    flight.start_py = (source == DamageSource::BombBlast) ? from_py : victim.pixel_y;
    victim.set_pixel_pos(flight.start_px, flight.start_py);
    flight.origin_source = source;
    flight.current_tick = 0;
    flight.total_ticks = 10;
    flight.apex_height_px = 36;

    int32_t range = max_tiles - min_tiles + 1;
    int32_t tiles = min_tiles + (range > 1 ? static_cast<int32_t>(random_val % static_cast<uint16_t>(range)) : 0);
    if (tiles <= 0) tiles = 4;

    static constexpr int32_t DIR_DX[8] = { 0,  1, 1, 1, 0, -1, -1, -1 };
    static constexpr int32_t DIR_DY[8] = {-1, -1, 0, 1, 1,  1,  0, -1 };

    int32_t start_tx = (flight.start_px >= 0) ? (flight.start_px / 32) : ((flight.start_px - 31) / 32);
    int32_t start_ty = (flight.start_py >= 0) ? (flight.start_py / 32) : ((flight.start_py - 31) / 32);

    uint32_t initial_dir = 0;
    if (source == DamageSource::BombBlast) {
        // Authentic 1998 Ants.exe FUN_0101df5d: recoil direction is opposite approach vector: (facing + 4) % 8
        initial_dir = (static_cast<uint32_t>(victim.facing) + 4) % 8;
    } else {
        // Combat Ant punch: recoil away from attacker
        int32_t dx = victim.pixel_x - from_px;
        int32_t dy = victim.pixel_y - from_py;
        if (dx == 0 && dy == 0) dx = 1;
        int32_t dir_x = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
        int32_t dir_y = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);
        initial_dir = static_cast<uint32_t>(ants::assets::vector_to_direction(dir_x, dir_y)) % 8;
    }

    bool dest_found = false;
    if (grid != nullptr) {
        for (uint32_t step = 0; step < 8; ++step) {
            uint32_t cand_dir = (initial_dir + step) % 8;
            int32_t cand_tx = start_tx + DIR_DX[cand_dir] * tiles;
            int32_t cand_ty = start_ty + DIR_DY[cand_dir] * tiles;

            if (!grid->in_bounds(cand_tx, cand_ty)) continue;
            // Impassable terrain / rocks cannot be landed on (water IS allowed: not counting water)
            const auto& cell = grid->get_cell(static_cast<uint32_t>(cand_tx), static_cast<uint32_t>(cand_ty));
            if (cell.terrain_type == TERRAIN_OBSTACLE || cell.is_obstacle_overlay) continue;
            // Anthill base cannot be landed on
            if (grid->is_anthill_reserved_spot(TileCoord{cand_tx, cand_ty})) continue;
            // Powerup tiles CANNOT be landed on (triggers directional rotation in 1998 Ants.exe FUN_0101df5d)
            if (grid->has_powerup_at(TileCoord{cand_tx, cand_ty})) continue;
            // Note: Fire wall tiles ARE valid landing spots (authentic to 1998 strategic play)

            flight.target_px = cand_tx * 32 + 16;
            flight.target_py = cand_ty * 32 + 16;
            flight.total_distance_px = tiles * 32;
            flight.flight_dir = static_cast<Direction>(cand_dir);
            flight.destination_resolved = true;
            dest_found = true;
            break;
        }
    }

    if (!dest_found) {
        int32_t dir_x = DIR_DX[initial_dir];
        int32_t dir_y = DIR_DY[initial_dir];
        flight.target_px = flight.start_px + dir_x * (tiles * 32);
        flight.target_py = flight.start_py + dir_y * (tiles * 32);
        flight.total_distance_px = tiles * 32;
        flight.flight_dir = static_cast<Direction>(initial_dir);
        flight.destination_resolved = (grid != nullptr);
    }

    victim.state = UnitState::Knockback;
    victim.anim_tick = 0;
    victim.anim_subitem = 0;
    audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_A, victim.pixel_x, victim.pixel_y, 1, 255});

    cancel_flight(victim.id);
    active_flights_.push_back(flight);
}

void PhysicsEngine::tick(std::vector<AntUnit*>& all_units,
                         Grid& grid,
                         std::vector<AudioEvent>& audio_out,
                         PRNG& prng,
                         std::function<void(AntUnit&, TileCoord)> on_bomb_land) {
    static constexpr int32_t DIR_DX[8] = { 0,  1, 1, 1, 0, -1, -1, -1 };
    static constexpr int32_t DIR_DY[8] = {-1, -1, 0, 1, 1,  1,  0, -1 };

    struct CompletedLanding {
        AntUnit* unit;
        int32_t inc_dx;
        int32_t inc_dy;
        DamageSource origin_source;
    };
    std::vector<CompletedLanding> completed_landings;

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

        // On first tick, resolve the final destination tile if not already resolved
        if (!f.destination_resolved) {
            f.destination_resolved = true;

            int32_t start_tx = (f.start_px >= 0) ? (f.start_px / 32) : ((f.start_px - 31) / 32);
            int32_t start_ty = (f.start_py >= 0) ? (f.start_py / 32) : ((f.start_py - 31) / 32);
            uint32_t dir = static_cast<uint32_t>(f.flight_dir) % 8;
            int32_t dir_x = DIR_DX[dir];
            int32_t dir_y = DIR_DY[dir];
            int32_t nominal_tiles = std::max(std::abs(f.target_px - f.start_px),
                                             std::abs(f.target_py - f.start_py)) / 32;
            if (nominal_tiles <= 0) nominal_tiles = 4;

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
            completed_landings.push_back({unit, inc_dx, inc_dy, f.origin_source});
            it = active_flights_.erase(it);
            continue;
        }

        if (f.current_tick >= f.total_ticks) {
            unit->set_pixel_pos(f.target_px, f.target_py);
            unit->altitude_z = 0;
            completed_landings.push_back({unit, inc_dx, inc_dy, f.origin_source});
            it = active_flights_.erase(it);
            continue;
        }

        ++it;
    }

    // Resolve completed landings outside active_flights_ iteration loop
    for (const auto& landing : completed_landings) {
        if (!landing.unit) continue;
        resolve_landing(*landing.unit, grid, audio_out, prng, landing.inc_dx, landing.inc_dy, on_bomb_land, landing.origin_source);
    }
}

void PhysicsEngine::resolve_landing(AntUnit& unit,
                                    Grid& grid,
                                    std::vector<AudioEvent>& audio_out,
                                    PRNG& prng,
                                    int32_t incoming_dx,
                                    int32_t incoming_dy,
                                    std::function<void(AntUnit&, TileCoord)> on_bomb_land,
                                    DamageSource source) {
    unit.altitude_z = 0;
    if (!grid.in_bounds(unit.pos.x, unit.pos.y)) {
        if (unit.hp == 0) unit.state = UnitState::Dead;
        return;
    }

    // 1. Bomb Landing (Authentic 1998 Chain Detonation with Dud Roll!)
    if (unit.is_alive() && grid.has_bomb_at(unit.pos)) {
        if (on_bomb_land) {
            on_bomb_land(unit, unit.pos);
            return;
        }
    }

    const auto& cell = grid.get_cell(unit.pos);

    // 2. Water Landing
    if (cell.terrain_type == TERRAIN_WATER && !cell.has_completed_bridge()) {
        resolve_water_entry(unit, audio_out);
        return;
    }

    // 3. Fire Landing: Ant lands directly on fire wall, takes fire damage, remains on fire tile!
    if (cell.has_fire()) {
        resolve_fire_contact(unit, grid, audio_out, prng, incoming_dx, incoming_dy, source);
        return;
    }

    // 4. Ground Landing
    if (unit.hp == 0) {
        unit.state = UnitState::Bounce;
        unit.state_timer = 10;
        unit.anim_tick = 0;
        unit.anim_subitem = 0;
        audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_B, unit.pixel_x, unit.pixel_y, 0, 255});
    } else if (source == DamageSource::BombBlast) {
        // Authentic 1998 Ants.exe case 0xe (Knockback completion):
        // Knockback sequence (a*gb, 12 ticks) concludes with the ant landing slide.
        // Upon landing completion, the ant transitions directly to Idle (0 additional stun).
        audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_B, unit.pixel_x, unit.pixel_y, 0, 255});
        unit.state = (unit.type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
        unit.anim_tick = 0;
        unit.anim_subitem = 0;
        unit.stun_ticks_remaining = 0;
    } else {
        // Melee / Combat punch knockback landing
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
                                         PRNG& /*prng*/,
                                         int32_t incoming_dx,
                                         int32_t incoming_dy,
                                         DamageSource source) {
    if (unit.type == AntType::Fire) {
        unit.altitude_z = 0;
        unit.state = (unit.type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
        unit.anim_tick = 0;
        unit.anim_subitem = 0;
        unit.stun_ticks_remaining = 0;
        unit.set_tile_pos(unit.pos.x, unit.pos.y);
        return;
    }

    int32_t start_px = unit.pixel_x;
    int32_t start_py = unit.pixel_y;
    int32_t dest_tx = unit.pos.x;
    int32_t dest_ty = unit.pos.y;
    int loop_guard = 0;

    while (grid.in_bounds(dest_tx, dest_ty) &&
           grid.get_cell(TileCoord{dest_tx, dest_ty}).has_fire() &&
           unit.is_alive() && loop_guard < 10) {
        loop_guard++;

        // Contact damage (+1 fire damage)
        unit.take_damage(1, DamageSource::FireBurn, 0);
        if (!unit.is_alive()) {
            unit.altitude_z = 0;
            return;
        }

        // Reflection trajectory: bounce back opposite of incoming movement
        int32_t b_dx = -incoming_dx;
        int32_t b_dy = -incoming_dy;
        if (b_dx == 0 && b_dy == 0) {
            b_dx = -1;
        }

        int32_t nx = dest_tx + b_dx;
        int32_t ny = dest_ty + b_dy;
        // If backward bounce is valid, not solid, and not another fire, bounce there
        if (grid.in_bounds(nx, ny) && !grid.is_solid_obstacle(nx, ny) && !grid.get_cell(TileCoord{nx, ny}).has_fire()) {
            dest_tx = nx;
            dest_ty = ny;
            incoming_dx = b_dx;
            incoming_dy = b_dy;
        } else {
            // Deflect forward / away from fire
            int32_t def_x = dest_tx - b_dx;
            int32_t def_y = dest_ty - b_dy;
            if (grid.in_bounds(def_x, def_y) && !grid.is_solid_obstacle(def_x, def_y)) {
                dest_tx = def_x;
                dest_ty = def_y;
                incoming_dx = -b_dx;
                incoming_dy = -b_dy;
            } else {
                // Try perpendicular deflection escape
                int32_t perp_x = dest_tx + b_dy;
                int32_t perp_y = dest_ty + b_dx;
                if (grid.in_bounds(perp_x, perp_y) && !grid.is_solid_obstacle(perp_x, perp_y)) {
                    dest_tx = perp_x;
                    dest_ty = perp_y;
                    incoming_dx = b_dy;
                    incoming_dy = b_dx;
                } else {
                    break;
                }
            }
        }
    }

    audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_A, start_px, start_py, 1, 255});
    unit.altitude_z = 0;
    unit.set_tile_pos(dest_tx, dest_ty);

    if (source == DamageSource::BombBlast) {
        unit.state = (unit.type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
        unit.anim_tick = 0;
        unit.anim_subitem = 0;
        unit.stun_ticks_remaining = 0;
    } else if (source == DamageSource::CombatPunch) {
        audio_out.push_back(AudioEvent{SOUND_STUN, unit.pixel_x, unit.pixel_y, 0, 255});
        unit.start_stun(STUN_RECOVERY_TICKS);
    } else {
        // Direct hazard contact (stepping or spawned on fire): animate bounce slide away from fire
        unit.push_start_px = start_px;
        unit.push_start_py = start_py;
        unit.push_dest_px = dest_tx * 32 + 16;
        unit.push_dest_py = dest_ty * 32 + 16;
        unit.push_ticks_total = 6;
        unit.push_tick_current = 0;
        unit.pixel_x = start_px;
        unit.pixel_y = start_py;
        unit.fx_x = start_px << 16;
        unit.fx_y = start_py << 16;

        unit.state = UnitState::Bounce;
        unit.anim_tick = 0;
        unit.anim_subitem = 0;
        unit.post_bounce_stun = false;
        unit.stun_ticks_remaining = 0;
    }
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
