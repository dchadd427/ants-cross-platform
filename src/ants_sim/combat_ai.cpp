#include "ants_sim/combat_ai.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_sim/sim_engine.hpp"
#include <algorithm>
#include <cmath>

namespace ants::sim {

CombatAIController::CombatAIController(AntUnit& owner)
    : owner_(owner)
    , anchor_tx_(owner.pos.x)
    , anchor_ty_(owner.pos.y)
{
    owner_.guard_anchor = {anchor_tx_, anchor_ty_};
}

void CombatAIController::commit_guard_anchor() noexcept {
    anchor_tx_ = owner_.pos.x;
    anchor_ty_ = owner_.pos.y;
    owner_.guard_anchor = owner_.pos;
}

void CombatAIController::set_guard_anchor(int32_t tx, int32_t ty) noexcept {
    anchor_tx_ = tx;
    anchor_ty_ = ty;
    owner_.guard_anchor = {tx, ty};
}

void CombatAIController::on_user_command_issued() noexcept {
    guard_state_ = CombatGuardState::Idle;
    target_unit_id_ = std::nullopt;
    strike_timer_ = 0;
}

bool CombatAIController::is_valid_target(const AntUnit& candidate,
                                         const AntUnit& owner,
                                         const MatchStatsManager& stats) const noexcept {
    if (candidate.id == owner.id) return false;
    if (!candidate.is_alive() || candidate.death_status != DeathStatus::Alive) return false;
    if (candidate.team == owner.team) return false;
    if (stats.are_allies(candidate.player_id, owner.player_id)) return false;
    if (candidate.is_underground() || candidate.state == UnitState::EnteringBase) return false;
    if (candidate.state == UnitState::Knockback || candidate.state == UnitState::Drowning) return false;
    if (candidate.is_in_scuffle || candidate.state == UnitState::Bounce) return false;
    if (candidate.on_powerup) return false;
    if (candidate.type == AntType::Swimmer && candidate.in_water) return false;

    return true;
}

AntUnit* CombatAIController::find_best_target(const std::vector<AntUnit*>& all_units,
                                              const MatchStatsManager& stats) const {
    AntUnit* best = nullptr;
    int32_t min_dist = AGGRO_RADIUS_CHEBYSHEV + 1;

    for (AntUnit* u : all_units) {
        if (!u || !is_valid_target(*u, owner_, stats)) continue;

        int32_t d = owner_.guard_anchor.chebyshev_dist(u->pos);
        if (d <= AGGRO_RADIUS_CHEBYSHEV && d < min_dist) {
            min_dist = d;
            best = u;
        }
    }
    return best;
}

void CombatAIController::update(const std::vector<AntUnit*>& all_units,
                                const Grid& grid,
                                const MatchStatsManager& stats,
                                std::vector<AudioEvent>& audio_out,
                                uint32_t random_seed) {
    if (owner_.type != AntType::Combat || !owner_.is_alive()) return;
    if (owner_.is_stunned() || owner_.state == UnitState::Flinch || owner_.state == UnitState::Knockback ||
        owner_.state == UnitState::Bounce || owner_.is_in_scuffle) return;
    if (owner_.state == UnitState::Walking) return; // Respect user movement commands

    if (owner_.state == UnitState::GuardIdle || owner_.state == UnitState::Idle) {
        guard_state_ = CombatGuardState::Idle;
        commit_guard_anchor();
        owner_.state = UnitState::GuardIdle;
    }

    switch (guard_state_) {
        case CombatGuardState::Idle:
            update_guard_idle(all_units, grid, stats, audio_out, random_seed);
            break;
        case CombatGuardState::Intercepting:
            update_intercepting(all_units, grid, stats, audio_out, random_seed);
            break;
        case CombatGuardState::Striking:
            update_striking(all_units, grid, audio_out, random_seed);
            break;
        case CombatGuardState::Returning:
            update_returning(grid);
            break;
    }
}

void CombatAIController::update_guard_idle(const std::vector<AntUnit*>& all_units,
                                           const Grid& grid,
                                           const MatchStatsManager& stats,
                                           std::vector<AudioEvent>& audio_out,
                                           uint32_t random_seed) {
    AntUnit* target = find_best_target(all_units, stats);
    if (target) {
        target_unit_id_ = target->id;
        int32_t dist = owner_.pos.chebyshev_dist(target->pos);
        if (dist <= 1) {
            // Already within melee range: strike immediately!
            guard_state_ = CombatGuardState::Striking;
            owner_.state = UnitState::Attacking;
            strike_timer_ = ATTACK_ANIM_TICKS;
            update_striking(all_units, grid, audio_out, random_seed);
        } else {
            guard_state_ = CombatGuardState::Intercepting;
            owner_.state = UnitState::Intercepting;
        }
    }
}

void CombatAIController::update_intercepting(const std::vector<AntUnit*>& all_units,
                                             const Grid& grid,
                                             const MatchStatsManager& stats,
                                             std::vector<AudioEvent>& audio_out,
                                             uint32_t random_seed) {
    if (!target_unit_id_) {
        guard_state_ = CombatGuardState::Returning;
        owner_.state = UnitState::ReturningToPost;
        return;
    }

    AntUnit* target = nullptr;
    for (AntUnit* u : all_units) {
        if (u && u->id == *target_unit_id_) {
            target = u;
            break;
        }
    }

    if (!target || !is_valid_target(*target, owner_, stats)) {
        target_unit_id_ = std::nullopt;
        guard_state_ = CombatGuardState::Returning;
        owner_.state = UnitState::ReturningToPost;
        return;
    }

    int32_t dist_to_anchor = owner_.guard_anchor.chebyshev_dist(target->pos);
    if (dist_to_anchor > PURSUIT_DISENGAGE_RADIUS) {
        target_unit_id_ = std::nullopt;
        guard_state_ = CombatGuardState::Returning;
        owner_.state = UnitState::ReturningToPost;
        return;
    }

    int32_t dist = owner_.pos.chebyshev_dist(target->pos);
    if (dist <= 1) {
        if (owner_.attack_cooldown_ticks == 0) {
            guard_state_ = CombatGuardState::Striking;
            owner_.state = UnitState::Attacking;
            strike_timer_ = ATTACK_ANIM_TICKS;
            update_striking(all_units, grid, audio_out, random_seed);
        } else {
            owner_.state = UnitState::GuardIdle;
        }
        return;
    }

    // Step 1 tile towards target
    int32_t step_dx = (target->pos.x > owner_.pos.x) ? 1 : ((target->pos.x < owner_.pos.x) ? -1 : 0);
    int32_t step_dy = (target->pos.y > owner_.pos.y) ? 1 : ((target->pos.y < owner_.pos.y) ? -1 : 0);

    TileCoord prev_pos = owner_.pos;
    TileCoord next_pos{owner_.pos.x + step_dx, owner_.pos.y + step_dy};
    if (next_pos != target->pos && grid.in_bounds(next_pos) && !grid.is_solid_obstacle(next_pos.x, next_pos.y)) {
        owner_.set_tile_pos(next_pos.x, next_pos.y);
    } else if (step_dx != 0 && TileCoord{owner_.pos.x + step_dx, owner_.pos.y} != target->pos &&
               grid.in_bounds(TileCoord{owner_.pos.x + step_dx, owner_.pos.y}) &&
               !grid.is_solid_obstacle(owner_.pos.x + step_dx, owner_.pos.y)) {
        owner_.set_tile_pos(owner_.pos.x + step_dx, owner_.pos.y);
    } else if (step_dy != 0 && TileCoord{owner_.pos.x, owner_.pos.y + step_dy} != target->pos &&
               grid.in_bounds(TileCoord{owner_.pos.x, owner_.pos.y + step_dy}) &&
               !grid.is_solid_obstacle(owner_.pos.x, owner_.pos.y + step_dy)) {
        owner_.set_tile_pos(owner_.pos.x, owner_.pos.y + step_dy);
    }

    if (owner_.pos != prev_pos) {
        owner_.facing = ants::assets::vector_to_direction(owner_.pos.x - prev_pos.x, owner_.pos.y - prev_pos.y);
        owner_.anim_tick++;
        owner_.anim_subitem = (owner_.anim_tick / 3);
    }

    if (owner_.pos.chebyshev_dist(target->pos) <= 1) {
        if (owner_.attack_cooldown_ticks == 0) {
            guard_state_ = CombatGuardState::Striking;
            owner_.state = UnitState::Attacking;
            strike_timer_ = ATTACK_ANIM_TICKS;
        } else {
            owner_.state = UnitState::GuardIdle;
        }
    }
}

void CombatAIController::update_striking(const std::vector<AntUnit*>& all_units,
                                         const Grid& grid,
                                         std::vector<AudioEvent>& audio_out,
                                         [[maybe_unused]] uint32_t random_seed) {
    if (target_unit_id_) {
        AntUnit* target = nullptr;
        for (AntUnit* u : all_units) {
            if (u && u->id == *target_unit_id_) {
                target = u;
                break;
            }
        }
        if (target && target->is_alive()) {
            if (owner_.pos.chebyshev_dist(target->pos) > 1 || owner_.attack_cooldown_ticks > 0) {
                target_unit_id_ = std::nullopt;
                guard_state_ = CombatGuardState::Returning;
                owner_.state = UnitState::ReturningToPost;
                return;
            }
            owner_.attack_cooldown_ticks = 12;
            audio_out.push_back(AudioEvent{SoundID::HeavyPunch, owner_.pixel_x, owner_.pixel_y, 1, 255});
            target->take_damage(2, DamageSource::CombatPunch, owner_.id);

            int32_t kdx = target->pos.x - owner_.pos.x;
            int32_t kdy = target->pos.y - owner_.pos.y;
            if (kdx == 0 && kdy == 0) {
                kdx = 1;
            }
            int32_t step_x = (kdx > 0) ? 1 : ((kdx < 0) ? -1 : 0);
            int32_t step_y = (kdy > 0) ? 1 : ((kdy < 0) ? -1 : 0);
            if (step_x == 0 && step_y == 0) {
                step_x = 1;
            }

            int32_t dist_tiles = 4 + (static_cast<int32_t>(random_seed) & 1);
            TileCoord land_pos = target->pos;
            for (int32_t s = 1; s <= dist_tiles; ++s) {
                TileCoord next{target->pos.x + step_x * s, target->pos.y + step_y * s};
                if (!grid.in_bounds(next) || grid.is_solid_obstacle(next.x, next.y)) {
                    break;
                }
                land_pos = next;
            }

            int32_t max_x = (grid.width() > 0) ? static_cast<int32_t>(grid.width() - 1) : 0;
            int32_t max_y = (grid.height() > 0) ? static_cast<int32_t>(grid.height() - 1) : 0;
            int32_t clamped_x = std::clamp(land_pos.x, 0, max_x);
            int32_t clamped_y = std::clamp(land_pos.y, 0, max_y);

            target->set_tile_pos(clamped_x, clamped_y);
            target->start_stun(AntUnit::STUN_TICKS);
            audio_out.push_back(AudioEvent{SoundID::StunRecover, target->pixel_x, target->pixel_y, 0, 255});
        }
    }

    target_unit_id_ = std::nullopt;
    guard_state_ = CombatGuardState::Returning;
    owner_.state = UnitState::ReturningToPost;
}

void CombatAIController::update_returning(const Grid& grid) {
    if (owner_.pos.x == anchor_tx_ && owner_.pos.y == anchor_ty_) {
        guard_state_ = CombatGuardState::Idle;
        owner_.state = UnitState::GuardIdle;
        return;
    }

    int32_t step_dx = (anchor_tx_ > owner_.pos.x) ? 1 : ((anchor_tx_ < owner_.pos.x) ? -1 : 0);
    int32_t step_dy = (anchor_ty_ > owner_.pos.y) ? 1 : ((anchor_ty_ < owner_.pos.y) ? -1 : 0);

    TileCoord prev_pos = owner_.pos;
    TileCoord next_pos{owner_.pos.x + step_dx, owner_.pos.y + step_dy};
    if (grid.in_bounds(next_pos) && !grid.is_solid_obstacle(next_pos.x, next_pos.y)) {
        owner_.set_tile_pos(next_pos.x, next_pos.y);
    } else if (step_dx != 0 && grid.in_bounds(TileCoord{owner_.pos.x + step_dx, owner_.pos.y}) &&
               !grid.is_solid_obstacle(owner_.pos.x + step_dx, owner_.pos.y)) {
        owner_.set_tile_pos(owner_.pos.x + step_dx, owner_.pos.y);
    } else if (step_dy != 0 && grid.in_bounds(TileCoord{owner_.pos.x, owner_.pos.y + step_dy}) &&
               !grid.is_solid_obstacle(owner_.pos.x, owner_.pos.y + step_dy)) {
        owner_.set_tile_pos(owner_.pos.x, owner_.pos.y + step_dy);
    }

    if (owner_.pos != prev_pos) {
        owner_.facing = ants::assets::vector_to_direction(owner_.pos.x - prev_pos.x, owner_.pos.y - prev_pos.y);
        owner_.anim_tick++;
        owner_.anim_subitem = (owner_.anim_tick / 3);
    }

    if (owner_.pos.x == anchor_tx_ && owner_.pos.y == anchor_ty_) {
        guard_state_ = CombatGuardState::Idle;
        owner_.state = UnitState::GuardIdle;
    }
}

} // namespace ants::sim
