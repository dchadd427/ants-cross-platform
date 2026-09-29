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

    if (on_powerup && (source == DamageSource::MeleeStandard || source == DamageSource::CombatPunch)) {
        return false; // Standing on top of power-up: not attackable!
    }

    if (type == AntType::Swimmer && in_water && (source == DamageSource::MeleeStandard || source == DamageSource::CombatPunch)) {
        return false; // Swimmer in water: immune from being attacked underwater!
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
    if (hp == 1) retreat_pending = true;
    bool uninterruptible = (state == UnitState::PlacingFire || state == UnitState::PlantingBomb ||
                            state == UnitState::BuildingBridge || state == UnitState::DemolishingBridge);
    if (!uninterruptible && state != UnitState::Knockback && state != UnitState::Stunned && state != UnitState::Drowning) {
        start_flinch();
    }
    return false; // Survived
}

void AntUnit::tick_timers() noexcept {
    if (transform_timer > 0) {
        transform_timer--;
    }

    if (attack_cooldown_ticks > 0) {
        attack_cooldown_ticks--;
    }

    if (ability_cooldown_ticks > 0) {
        ability_cooldown_ticks--;
    }

    if (stun_ticks_remaining > 0) {
        stun_ticks_remaining--;
        if (state == UnitState::Stunned) {
            anim_tick++;
            anim_subitem = anim_tick;
        }
        if (stun_ticks_remaining == 0 && state == UnitState::Stunned) {
            state = (type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
            anim_tick = 0;
            anim_subitem = 0;
            if (type == AntType::Combat) {
                guard_anchor = pos;
            }
        }
    }

    if (state_timer > 0) {
        state_timer--;
        if (state_timer == 0) {
            if (state == UnitState::Burn) {
                if (post_bounce_stun) {
                    start_stun(50);
                } else {
                    state = (type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
                }
            } else if (state == UnitState::Flinch || state == UnitState::Bounce || state == UnitState::Attacking) {
                state = (type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
            }
        }
    }

    if (state == UnitState::Attacking || state == UnitState::Flinch || state == UnitState::Burn) {
        anim_tick++;
        anim_subitem = anim_tick;
        if (state == UnitState::Flinch && push_tick_current < push_ticks_total) {
            push_tick_current++;
            int32_t interp_x = push_start_px + ((push_dest_px - push_start_px) * static_cast<int32_t>(push_tick_current)) / static_cast<int32_t>(push_ticks_total);
            int32_t interp_y = push_start_py + ((push_dest_py - push_start_py) * static_cast<int32_t>(push_tick_current)) / static_cast<int32_t>(push_ticks_total);
            pixel_x = interp_x;
            pixel_y = interp_y;
            fx_x = pixel_x << 16;
            fx_y = pixel_y << 16;
        }
    } else if (is_in_scuffle) {
        if (scuffle_ticks > 0) {
            scuffle_ticks--;
        }
        if (scuffle_ticks == 0) {
            is_in_scuffle = false;
            if (state == UnitState::Bounce) {
                anim_tick = 0;
                anim_subitem = 0;
                push_tick_current = 0;
                pixel_x = push_start_px;
                pixel_y = push_start_py;
                fx_x = pixel_x << 16;
                fx_y = pixel_y << 16;
            } else {
                state = (type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
            }
        }
    } else if (state == UnitState::Bounce) {
        anim_tick++;
        anim_subitem = static_cast<uint16_t>(anim_tick);
        if (push_tick_current < push_ticks_total) {
            push_tick_current++;
            int32_t interp_x = push_start_px + ((push_dest_px - push_start_px) * static_cast<int32_t>(push_tick_current)) / static_cast<int32_t>(push_ticks_total);
            int32_t interp_y = push_start_py + ((push_dest_py - push_start_py) * static_cast<int32_t>(push_tick_current)) / static_cast<int32_t>(push_ticks_total);
            pixel_x = interp_x;
            pixel_y = interp_y;
            fx_x = pixel_x << 16;
            fx_y = pixel_y << 16;
        }
    }

    // Continuous idle standing animation cycle
    if (state == UnitState::Idle || state == UnitState::GuardIdle) {
        anim_tick++;
        anim_subitem = (anim_tick / 4);
    }
}

} // namespace ants::sim
