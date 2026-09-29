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
{
    set_tile_pos(start_tx, start_ty);
}

void AntUnit::tick_timers() noexcept {
    if (transform_timer > 0) {
        transform_timer--;
    }

    if (ability_cooldown_ticks > 0) {
        ability_cooldown_ticks--;
    }

    // Continuous idle standing animation cycle
    if (state == UnitState::Idle || state == UnitState::GuardIdle) {
        anim_tick++;
        anim_subitem = (anim_tick / 4);
    }
}

} // namespace ants::sim
