#include "ants_sim/ant_unit.hpp"
#include <cmath>
#include <algorithm>

namespace ants::sim {

AntUnit::AntUnit(uint32_t unit_id, TeamId team_in, AntType type_in, int32_t start_tx, int32_t start_ty)
    : id(unit_id)
    , player_id(static_cast<uint8_t>(team_in))
    , team(team_in)
    , type(type_in)
    , state((type_in == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle)
    , death_status(DeathStatus::Alive)
    , hp(STARTING_HP)
    , max_hp(MAX_HP)
    , pos{start_tx, start_ty}
    , guard_anchor{start_tx, start_ty}
{
    set_tile_pos(start_tx, start_ty);
}

bool AntUnit::take_damage(uint16_t amount, DamageSource source, [[maybe_unused]] uint32_t attacker_id) noexcept {
    if (death_status != DeathStatus::Alive || hp == 0) {
        return false;
    }

    if (amount >= hp) {
        hp = 0;
        state = UnitState::Dead;
        switch (source) {
            case DamageSource::CombatPunch:
                death_status = DeathStatus::CombatKilled;
                break;
            case DamageSource::BombBlast:
                death_status = DeathStatus::BombKilled;
                break;
            case DamageSource::FireBurn:
                death_status = DeathStatus::FireKilled;
                break;
            default:
                death_status = DeathStatus::CombatKilled;
                break;
        }
        return true; // Lethal damage
    }

    hp = static_cast<uint16_t>(hp - amount);
    if (state != UnitState::Knockback && state != UnitState::Stunned && state != UnitState::Drowning) {
        start_flinch();
    }
    return false; // Survived
}

std::string AntUnit::get_sprite_prefix() const {
    const char base_prefix = (holding != 0) ? 'h' : 'a';
    char type_char = 'g';
    switch (type) {
        case AntType::Worker:  type_char = 'g'; break;
        case AntType::Bomber:  type_char = 'b'; break;
        case AntType::Fire:    type_char = 'f'; break;
        case AntType::Thief:   type_char = 't'; break;
        case AntType::Combat:  type_char = 'c'; break;
        case AntType::Swimmer: type_char = 's'; break;
    }
    return std::string{base_prefix, type_char};
}

void AntUnit::set_destination(int32_t target_tx, int32_t target_ty) {
    waypoints.clear();
    waypoints.push_back(TileCoord{target_tx, target_ty});
    current_waypoint_idx = 0;
    anim_tick = 0;
    anim_subitem = 0;
    state = UnitState::Walking;
}

void AntUnit::set_path(std::vector<TileCoord> path) {
    waypoints = std::move(path);
    current_waypoint_idx = 0;
    anim_tick = 0;
    anim_subitem = 0;
    state = UnitState::Walking;
}

void AntUnit::tick_movement(bool is_swimming, SurfaceType surface) {
    in_water = is_swimming;

    if (type == AntType::Swimmer) {
        if (state == UnitState::DivingInWater) {
            anim_tick++;
            anim_subitem = anim_tick;
            if (anim_subitem >= 16) {
                state = UnitState::Walking;
                anim_tick = 0;
                anim_subitem = 0;
            }
            return;
        }
        if (state == UnitState::ExitingWater) {
            anim_tick++;
            anim_subitem = anim_tick;
            if (anim_subitem >= 7) {
                state = UnitState::Walking;
                anim_tick = 0;
                anim_subitem = 0;
            }
            return;
        }

        // Detect entering or exiting water
        if (state == UnitState::Swimming) {
            was_in_water = true;
        }

        if (is_swimming && !was_in_water) {
            was_in_water = true;
            if (state == UnitState::Walking) {
                state = UnitState::DivingInWater;
                anim_tick = 0;
                anim_subitem = 0;
                return;
            } else if (state == UnitState::Idle) {
                state = UnitState::Swimming;
            }
        }
        if (!is_swimming && was_in_water) {
            was_in_water = false;
            if (state == UnitState::Walking) {
                state = UnitState::ExitingWater;
                anim_tick = 0;
                anim_subitem = 0;
                return;
            } else if (state == UnitState::Swimming) {
                state = UnitState::Idle;
            }
        }
    }

    if (state != UnitState::Walking || waypoints.empty()) {
        if (type == AntType::Swimmer && is_swimming && state == UnitState::Idle) {
            state = UnitState::Swimming;
            was_in_water = true;
        }
        return;
    }

    if (current_waypoint_idx >= waypoints.size()) {
        if (type == AntType::Swimmer && is_swimming) {
            state = UnitState::Swimming;
        } else {
            state = (type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
        }
        waypoints.clear();
        current_waypoint_idx = 0;
        anim_tick = 0;
        anim_subitem = 0;
        return;
    }

    const TileCoord dest = waypoints[current_waypoint_idx];
    const int32_t dest_px = dest.x * 32 + 16;
    const int32_t dest_py = dest.y * 32 + 16;

    const int32_t cur_px = fx_x >> 16;
    const int32_t cur_py = fx_y >> 16;

    const int32_t dx = dest_px - cur_px;
    const int32_t dy = dest_py - cur_py;

    // Check arrival at waypoint
    if (std::abs(dx) <= 2 && std::abs(dy) <= 2) {
        fx_x = dest_px << 16;
        fx_y = dest_py << 16;
        pixel_x = dest_px;
        pixel_y = dest_py;
        pos = dest;
        current_waypoint_idx++;
        if (current_waypoint_idx >= waypoints.size()) {
            if (type == AntType::Swimmer && is_swimming) {
                state = UnitState::Swimming;
            } else {
                state = (type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
            }
            waypoints.clear();
            current_waypoint_idx = 0;
            anim_tick = 0;
            anim_subitem = 0;
        }
        return;
    }

    // Set facing direction
    facing = ants::assets::vector_to_direction(dx, dy);

    // Advance walk cycle animation
    anim_tick++;
    anim_subitem = (anim_tick / 3);

    // Speed selection
    int32_t speed_fx = SPEED_STANDARD_FX;
    if (type == AntType::Thief) {
        speed_fx = SPEED_THIEF_FX;
    } else if (type == AntType::Swimmer && is_swimming) {
        speed_fx = SPEED_AQUATIC_FX;
    }

    // Apply surface speed multiplier (Slate > Gravel > Grass > Mud)
    if (!(type == AntType::Swimmer && is_swimming)) {
        speed_fx = FixedPointMath::mul(speed_fx, get_surface_speed_multiplier_fx(surface));
    }

    const bool is_diagonal = (dx != 0 && dy != 0);
    const int32_t step_fx = is_diagonal ? FixedPointMath::mul(speed_fx, DIAG_SCALE_FX) : speed_fx;

    if (dx > 0) fx_x += std::min(step_fx, dx << 16);
    else if (dx < 0) fx_x -= std::min(step_fx, (-dx) << 16);

    if (dy > 0) fx_y += std::min(step_fx, dy << 16);
    else if (dy < 0) fx_y -= std::min(step_fx, (-dy) << 16);

    is_on_mud = (surface == SurfaceType::Mud);

    sync_pixel_from_fx();
}

void AntUnit::tick_timers() noexcept {
    if (transform_timer > 0) {
        transform_timer--;
    }

    if (stun_ticks_remaining > 0) {
        stun_ticks_remaining--;
        if (stun_ticks_remaining == 0 && state == UnitState::Stunned) {
            state = (type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
            if (type == AntType::Combat) {
                guard_anchor = pos;
            }
        }
    }

    if (state_timer > 0) {
        state_timer--;
        if (state_timer == 0 && state == UnitState::Flinch) {
            state = (type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
        }
    }

    // Continuous idle standing animation cycle
    if (state == UnitState::Idle || state == UnitState::GuardIdle || state == UnitState::QueuingBase) {
        anim_tick++;
        anim_subitem = (anim_tick / 4);
    }
}

} // namespace ants::sim
