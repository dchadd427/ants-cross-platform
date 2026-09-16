#include "ants_sim/physics.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace ants::sim {

void PhysicsEngine::apply_knockback(AntUnit& victim,
                                    int32_t from_px,
                                    int32_t from_py,
                                    int32_t min_tiles,
                                    int32_t max_tiles,
                                    DamageSource source,
                                    std::vector<AudioEvent>& audio_out,
                                    uint16_t random_val,
                                    const Grid* grid,
                                    int32_t incoming_dx,
                                    int32_t incoming_dy) {
    (void)min_tiles;
    (void)max_tiles;
    (void)random_val;

    BallisticFlight flight{};
    flight.unit_id = victim.id;
    flight.origin_source = source;
    flight.current_tick = 0;

    static constexpr int32_t DIR_DX[8] = { 0,  1, 1, 1, 0, -1, -1, -1 };
    static constexpr int32_t DIR_DY[8] = {-1, -1, 0, 1, 1,  1,  0, -1 };

    uint32_t initial_dir = 0;
    if (source == DamageSource::BombBlast) {
        int32_t b_dx = victim.pixel_x - from_px;
        int32_t b_dy = victim.pixel_y - from_py;
        if (b_dx == 0 && b_dy == 0) {
            if (incoming_dx != 0 || incoming_dy != 0) {
                b_dx = incoming_dx;
                b_dy = incoming_dy;
            } else {
                int32_t opp_dir = (static_cast<int32_t>(victim.facing) + 4) % 8;
                b_dx = DIR_DX[opp_dir];
                b_dy = DIR_DY[opp_dir];
            }
        }
        int32_t dir_x = (b_dx > 0) ? 1 : ((b_dx < 0) ? -1 : 0);
        int32_t dir_y = (b_dy > 0) ? 1 : ((b_dy < 0) ? -1 : 0);
        initial_dir = static_cast<uint32_t>(ants::assets::vector_to_direction(dir_x, dir_y)) % 8;
    } else {
        // Combat Ant punch: recoil away from attacker
        int32_t dx = victim.pixel_x - from_px;
        int32_t dy = victim.pixel_y - from_py;
        if (dx == 0 && dy == 0) dx = 1;
        int32_t dir_x = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
        int32_t dir_y = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);
        initial_dir = static_cast<uint32_t>(ants::assets::vector_to_direction(dir_x, dir_y)) % 8;
    }

    // Outward deflection sequence around the blast/impact origin:
    // 1. Primary vector away from source
    // 2. +45° adjacent
    // 3. -45° adjacent
    // 4. +90° perpendicular
    // 5. -90° perpendicular
    // 6. +135° angle
    // 7. -135° angle
    // 8. 180° opposite (if all forward/perpendicular vectors obstructed)
    uint32_t cand_dirs[8] = {
        initial_dir,
        (initial_dir + 1) % 8,
        (initial_dir + 7) % 8,
        (initial_dir + 2) % 8,
        (initial_dir + 6) % 8,
        (initial_dir + 3) % 8,
        (initial_dir + 5) % 8,
        (initial_dir + 4) % 8
    };

    int32_t ant_tx = (victim.pixel_x >= 0) ? (victim.pixel_x / 32) : ((victim.pixel_x - 31) / 32);
    int32_t ant_ty = (victim.pixel_y >= 0) ? (victim.pixel_y / 32) : ((victim.pixel_y - 31) / 32);

    int32_t origin_tx = (source == DamageSource::BombBlast) ?
        ((from_px >= 0) ? (from_px / 32) : ((from_px - 31) / 32)) : ant_tx;
    int32_t origin_ty = (source == DamageSource::BombBlast) ?
        ((from_py >= 0) ? (from_py / 32) : ((from_py - 31) / 32)) : ant_ty;

    bool dest_found = false;
    uint32_t chosen_dir = initial_dir;
    int32_t chosen_tx = origin_tx;
    int32_t chosen_ty = origin_ty;
    int32_t dist_tiles = (source == DamageSource::BombBlast) ? 4 : std::max(1, max_tiles);

    if (grid != nullptr) {
        for (int32_t try_dist = dist_tiles; try_dist >= 1 && !dest_found; --try_dist) {
            for (uint32_t d : cand_dirs) {
                int32_t cand_tx = origin_tx + DIR_DX[d] * try_dist;
                int32_t cand_ty = origin_ty + DIR_DY[d] * try_dist;

                if (!grid->in_bounds(cand_tx, cand_ty)) continue;
                const auto& cell = grid->get_cell(static_cast<uint32_t>(cand_tx), static_cast<uint32_t>(cand_ty));
                if (cell.is_obstacle() || cell.is_food || grid->has_food_at(TileCoord{cand_tx, cand_ty})) continue;
                if (grid->is_anthill_reserved_spot(TileCoord{cand_tx, cand_ty})) continue;
                if (grid->has_powerup_at(TileCoord{cand_tx, cand_ty})) continue;

                // Never land inside any anthill footprint (4x4) or queue slots (bx - 1, by..by+3)
                bool in_base_or_queue = false;
                for (const auto& ah : grid->anthills()) {
                    int32_t bx = static_cast<int32_t>(ah.x);
                    int32_t by = static_cast<int32_t>(ah.y);
                    if (cand_tx >= bx && cand_tx < bx + 4 && cand_ty >= by && cand_ty < by + 4) {
                        in_base_or_queue = true;
                        break;
                    }
                    if (cand_tx == bx - 1 && cand_ty >= by && cand_ty <= by + 3) {
                        in_base_or_queue = true;
                        break;
                    }
                }
                if (in_base_or_queue) continue;

                // Check intermediate obstacle clearance:
                // Ballistic knockback clears small terrain barriers (obstacle count <= 2 tiles)
                int32_t step_x = DIR_DX[d];
                int32_t step_y = DIR_DY[d];
                int32_t obst_count = 0;
                bool obst_too_large = false;
                for (int32_t s = 1; s < try_dist; ++s) {
                    int32_t mx = origin_tx + step_x * s;
                    int32_t my = origin_ty + step_y * s;
                    if (!grid->in_bounds(mx, my)) { obst_too_large = true; break; }
                    const auto& mcell = grid->get_cell(static_cast<uint32_t>(mx), static_cast<uint32_t>(my));
                    if (mcell.is_obstacle() || mcell.is_obstacle_overlay) {
                        obst_count++;
                        if (obst_count > 2) {
                            obst_too_large = true;
                            break;
                        }
                    }
                }
                if (obst_too_large) continue;

                chosen_dir = d;
                chosen_tx = cand_tx;
                chosen_ty = cand_ty;
                dist_tiles = try_dist;
                dest_found = true;
                break;
            }
        }
        if (!dest_found) {
            chosen_tx = origin_tx;
            chosen_ty = origin_ty;
            dist_tiles = 0;
            dest_found = true;
        }
    } else {
        chosen_dir = initial_dir;
        chosen_tx = origin_tx + DIR_DX[initial_dir] * dist_tiles;
        chosen_ty = origin_ty + DIR_DY[initial_dir] * dist_tiles;
        dest_found = true;
    }

    if (dest_found) {
        flight.start_px = (victim.pixel_x != 0 || victim.pixel_y != 0) ? victim.pixel_x : (origin_tx * 32 + 16);
        flight.start_py = (victim.pixel_x != 0 || victim.pixel_y != 0) ? victim.pixel_y : (origin_ty * 32 + 16);
        flight.target_px = chosen_tx * 32 + 16;
        flight.target_py = chosen_ty * 32 + 16;
        flight.total_distance_px = dist_tiles * 32;
        flight.flight_dir = static_cast<Direction>(chosen_dir);
        flight.destination_resolved = true;
        flight.total_ticks = 10;
        flight.apex_height_px = 36;
        if (source == DamageSource::BombBlast) {
            victim.facing = static_cast<Direction>(chosen_dir);
        }
    } else {
        // All directions blocked: ant remains anchored in place in dazed state
        flight.start_px = origin_tx * 32 + 16;
        flight.start_py = origin_ty * 32 + 16;
        flight.target_px = origin_tx * 32 + 16;
        flight.target_py = origin_ty * 32 + 16;
        flight.total_distance_px = 0;
        flight.flight_dir = static_cast<Direction>(initial_dir);
        flight.destination_resolved = true;
        flight.total_ticks = 1;
        flight.apex_height_px = 0;
        if (source == DamageSource::BombBlast) {
            victim.facing = static_cast<Direction>(initial_dir);
        }
    }

    victim.state = UnitState::Knockback;
    victim.state_timer = 0;
    victim.anim_tick = 0;
    victim.anim_subitem = 0;

    // Authentic 1998 Ants.exe (Ants.exe.c:24460 / FUN_01021a6f):
    // The ant's logical coordinates are registered at the landing destination.
    // Bomb blasts anchor pixel coordinates at destination because Table 4 aggb301
    // possesses an intrinsic -128px displacement in its bytecode.
    // Combat punches interpolate smoothly from the ant's starting tile while
    // maintaining logical destination tile occupancy.
    victim.set_tile_pos(chosen_tx, chosen_ty);
    if (source == DamageSource::BombBlast) {
        victim.set_pixel_pos(flight.target_px, flight.target_py);
    } else {
        victim.pixel_x = flight.start_px;
        victim.pixel_y = flight.start_py;
        victim.fx_x = flight.start_px << 16;
        victim.fx_y = flight.start_py << 16;
    }
    victim.altitude_z = 0;

    audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_A, victim.pixel_x, victim.pixel_y, 1, 255});

    cancel_flight(victim.id);
    active_flights_.push_back(flight);
}

void PhysicsEngine::tick(std::vector<AntUnit*>& all_units,
                         Grid& grid,
                         std::vector<AudioEvent>& audio_out,
                         PRNG& prng,
                         std::function<void(AntUnit&, TileCoord, int32_t, int32_t)> on_bomb_land) {
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

        // Ensure destination is flagged resolved
        if (!f.destination_resolved) {
            f.destination_resolved = true;
        }

        f.current_tick++;
        if (f.total_ticks == 0) f.total_ticks = 1;

        if (f.origin_source == DamageSource::BombBlast) {
            unit->set_pixel_pos(f.target_px, f.target_py);
            unit->altitude_z = 0;
        } else {
            int32_t curr_px = f.start_px + ((f.target_px - f.start_px) * f.current_tick) / f.total_ticks;
            int32_t curr_py = f.start_py + ((f.target_py - f.start_py) * f.current_tick) / f.total_ticks;
            unit->pixel_x = curr_px;
            unit->pixel_y = curr_py;
            unit->fx_x = curr_px << 16;
            unit->fx_y = curr_py << 16;
            int32_t t = f.current_tick;
            unit->altitude_z = (4 * f.apex_height_px * t * (f.total_ticks - t)) / (f.total_ticks * f.total_ticks);
        }
        unit->anim_tick = f.current_tick;
        unit->anim_subitem = f.current_tick;

        int32_t inc_dx = (f.target_px > f.start_px) ? 1 : ((f.target_px < f.start_px) ? -1 : 0);
        int32_t inc_dy = (f.target_py > f.start_py) ? 1 : ((f.target_py < f.start_py) ? -1 : 0);

        // Safety check for exiting map bounds
        if (!grid.in_bounds(unit->pos.x, unit->pos.y)) {
            unit->set_pixel_pos(f.target_px, f.target_py);
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
                                    std::function<void(AntUnit&, TileCoord, int32_t, int32_t)> on_bomb_land,
                                    DamageSource source) {
    unit.altitude_z = 0;
    if (!grid.in_bounds(unit.pos.x, unit.pos.y)) {
        if (unit.hp == 0) unit.state = UnitState::Dead;
        return;
    }

    // 1. Bomb Landing (Authentic 1998 Chain Detonation with Dud Roll!)
    if (unit.is_alive() && grid.has_bomb_at(unit.pos)) {
        if (on_bomb_land) {
            on_bomb_land(unit, unit.pos, incoming_dx, incoming_dy);
            return;
        }
    }

    const auto& cell = grid.get_cell(unit.pos);

    // 2. Water Landing: Non-swimmer drowns unless landing on a completed bridge
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
    } else {
        // Living ant landing from knockback (bomb blast or punch):
        // Authentic 1998 Table 0x10045d8: Enters UnitState::Stunned playing *sd301
        // (spinning stars + dazed wobble) with Sound 65 (flythumpb.wav) and Sound 70 (stun.wav).
        audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_B, unit.pixel_x, unit.pixel_y, 0, 255});
        audio_out.push_back(AudioEvent{SOUND_STUN, unit.pixel_x, unit.pixel_y, 0, 255});
        unit.state = UnitState::Stunned;
        unit.anim_tick = 0;
        unit.anim_subitem = 0;
        unit.start_stun(STUN_RECOVERY_TICKS); // 50 ticks
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
                                         PRNG& prng,
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

    static constexpr int32_t DIR_DX[8] = { 0,  1, 1, 1, 0, -1, -1, -1 };
    static constexpr int32_t DIR_DY[8] = {-1, -1, 0, 1, 1,  1,  0, -1 };

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

        bool found_cand = false;
        uint32_t start_dir = 0;
        if (incoming_dx != 0 || incoming_dy != 0) {
            if (source == DamageSource::CombatPunch) {
                start_dir = static_cast<uint32_t>(ants::assets::vector_to_direction(-incoming_dx, -incoming_dy)) % 8;
            } else {
                start_dir = static_cast<uint32_t>(ants::assets::vector_to_direction(incoming_dx, incoming_dy)) % 8;
            }
        } else {
            start_dir = prng.rand() % 8;
        }
        for (uint32_t i = 0; i < 8; ++i) {
            uint32_t dir = (start_dir + i) % 8;
            int32_t nx = dest_tx + DIR_DX[dir];
            int32_t ny = dest_ty + DIR_DY[dir];
            if (grid.is_occupiable_non_wall(nx, ny)) {
                dest_tx = nx;
                dest_ty = ny;
                incoming_dx = DIR_DX[dir];
                incoming_dy = DIR_DY[dir];
                found_cand = true;
                break;
            }
        }

        if (!found_cand) {
            break;
        }
    }

    audio_out.push_back(AudioEvent{SOUND_FLY_THUMP_A, start_px, start_py, 1, 255});
    unit.altitude_z = 0;
    unit.set_tile_pos(dest_tx, dest_ty);

    if (source == DamageSource::CombatPunch) {
        audio_out.push_back(AudioEvent{SOUND_STUN, unit.pixel_x, unit.pixel_y, 0, 255});
        unit.start_stun(STUN_RECOVERY_TICKS);
    } else if (source == DamageSource::BombBlast) {
        unit.state = (unit.type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
        unit.anim_tick = 0;
        unit.anim_subitem = 0;
        unit.stun_ticks_remaining = 0;
    } else {
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
        unit.state_timer = 10;
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
