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

    // Authentic 1998 5-direction sequence (Ants.exe FUN_0101d8ed):
    // 1. Primary vector away from bomb
    // 2. +45° adjacent
    // 3. -45° adjacent
    // 4. +90° perpendicular
    // 5. -90° perpendicular
    // Strictly evaluates forward/perpendicular angles; NEVER wraps 180° through the bomb!
    uint32_t cand_dirs[5] = {
        initial_dir,
        (initial_dir + 1) % 8,
        (initial_dir + 7) % 8,
        (initial_dir + 2) % 8,
        (initial_dir + 6) % 8
    };

    int32_t ant_tx = (victim.pixel_x >= 0) ? (victim.pixel_x / 32) : ((victim.pixel_x - 31) / 32);
    int32_t ant_ty = (victim.pixel_y >= 0) ? (victim.pixel_y / 32) : ((victim.pixel_y - 31) / 32);

    bool dest_found = false;
    uint32_t chosen_dir = initial_dir;
    int32_t chosen_tx = ant_tx;
    int32_t chosen_ty = ant_ty;

    if (grid != nullptr) {
        for (uint32_t d : cand_dirs) {
            // In 1998 Ants.exe (FUN_0101d8ed & FUN_0101d9f7), flight projects 4 tiles from the ant,
            // which is exactly 5 tiles from the bomb counting the bomb itself!
            int32_t cand_tx = ant_tx + DIR_DX[d] * 4;
            int32_t cand_ty = ant_ty + DIR_DY[d] * 4;

            if (!grid->in_bounds(cand_tx, cand_ty)) continue;
            const auto& cell = grid->get_cell(static_cast<uint32_t>(cand_tx), static_cast<uint32_t>(cand_ty));
            if (cell.is_obstacle() || cell.is_food || grid->has_food_at(TileCoord{cand_tx, cand_ty})) continue;
            if (grid->is_anthill_reserved_spot(TileCoord{cand_tx, cand_ty})) continue;
            if (grid->has_powerup_at(TileCoord{cand_tx, cand_ty})) continue;

            chosen_dir = d;
            chosen_tx = cand_tx;
            chosen_ty = cand_ty;
            dest_found = true;
            break;
        }
    } else {
        chosen_dir = initial_dir;
        chosen_tx = ant_tx + DIR_DX[initial_dir] * 4;
        chosen_ty = ant_ty + DIR_DY[initial_dir] * 4;
        dest_found = true;
    }

    if (dest_found) {
        flight.start_px = victim.pixel_x;
        flight.start_py = victim.pixel_y;
        flight.target_px = chosen_tx * 32 + 16;
        flight.target_py = chosen_ty * 32 + 16;
        flight.total_distance_px = 128; // Always full 4 tiles (128px) from ant = 5 tiles counting bomb
        flight.flight_dir = static_cast<Direction>(chosen_dir);
        flight.destination_resolved = true;
        flight.total_ticks = 10;
        flight.apex_height_px = 36;
    } else {
        // All 5 directions are blocked: the ant cannot fly, safely anchors in place on its tile in dazed state
        flight.start_px = victim.pixel_x;
        flight.start_py = victim.pixel_y;
        flight.target_px = victim.pixel_x;
        flight.target_py = victim.pixel_y;
        flight.total_distance_px = 0;
        flight.flight_dir = static_cast<Direction>(initial_dir);
        flight.destination_resolved = true;
        flight.total_ticks = 1;
        flight.apex_height_px = 0;
    }

    victim.state = UnitState::Knockback;
    victim.state_timer = 0;
    victim.anim_tick = 0;
    victim.anim_subitem = 0;

    // Authentic 1998 Ants.exe (Ants.exe.c:24460):
    // The ant's logical coordinates are immediately anchored at the destination tile:
    victim.set_tile_pos(chosen_tx, chosen_ty);
    victim.set_pixel_pos(flight.target_px, flight.target_py);
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

        unit->set_pixel_pos(f.target_px, f.target_py);
        unit->altitude_z = 0;
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
