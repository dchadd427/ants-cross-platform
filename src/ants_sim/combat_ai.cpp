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
    user_moving_ = true;
}

bool CombatAIController::is_valid_target(const AntUnit& candidate,
                                         const AntUnit& owner,
                                         const MatchStatsManager& stats) const noexcept {
    if (candidate.id == owner.id) return false;
    if (candidate.pos == owner.pos) return false; // Same tile collision is handled by collision scuffle system
    if (!candidate.is_alive() || candidate.death_status != DeathStatus::Alive) return false;
    if (candidate.team == owner.team) return false;
    if (stats.are_allies(candidate.player_id, owner.player_id)) return false;
    if (candidate.is_underground() || candidate.state == UnitState::EnteringBase || candidate.state == UnitState::Infiltrating) return false;
    if (candidate.state == UnitState::Knockback || candidate.state == UnitState::Drowning) return false;
    if (candidate.is_in_scuffle || candidate.state == UnitState::Bounce) return false;
    if (candidate.on_powerup) return false;
    if (candidate.type == AntType::Swimmer && candidate.in_water) return false;
    if (candidate.is_invulnerable()) return false;

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

void CombatAIController::update(SimulationEngine& engine,
                                const std::vector<AntUnit*>& all_units,
                                const Grid& grid,
                                const MatchStatsManager& stats,
                                std::vector<AudioEvent>& audio_out,
                                uint32_t random_seed) {
    if (owner_.type != AntType::Combat || !owner_.is_alive()) return;
    if (owner_.is_stunned() || owner_.state == UnitState::Flinch || owner_.state == UnitState::Knockback ||
        owner_.state == UnitState::Bounce || owner_.is_in_scuffle || owner_.state == UnitState::Attacking) return;

    if (owner_.state == UnitState::GuardIdle || owner_.state == UnitState::Idle) {
        if (user_moving_) {
            user_moving_ = false;
            commit_guard_anchor();
        }
    }

    if (user_moving_) return; // Respect human user move commands

    switch (guard_state_) {
        case CombatGuardState::Idle:
            update_guard_idle(engine, all_units, grid, stats, audio_out, random_seed);
            break;
        case CombatGuardState::Intercepting:
            update_intercepting(engine, all_units, grid, stats, audio_out, random_seed);
            break;
        case CombatGuardState::Returning:
            update_returning(engine, all_units, grid, stats, audio_out, random_seed);
            break;
    }
}

void CombatAIController::update_guard_idle(SimulationEngine& engine,
                                           const std::vector<AntUnit*>& all_units,
                                           const Grid& grid,
                                           const MatchStatsManager& stats,
                                           std::vector<AudioEvent>& audio_out,
                                           uint32_t random_seed) {
    AntUnit* target = find_best_target(all_units, stats);
    if (target) {
        target_unit_id_ = target->id;
        guard_state_ = CombatGuardState::Intercepting;
        owner_.state = UnitState::Intercepting;
        update_intercepting(engine, all_units, grid, stats, audio_out, random_seed);
    }
}

void CombatAIController::update_intercepting(SimulationEngine& engine,
                                             const std::vector<AntUnit*>& all_units,
                                             const Grid& grid,
                                             const MatchStatsManager& stats,
                                             [[maybe_unused]] std::vector<AudioEvent>& audio_out,
                                             [[maybe_unused]] uint32_t random_seed) {
    if (!target_unit_id_) {
        guard_state_ = CombatGuardState::Returning;
        owner_.state = UnitState::ReturningToPost;
        if (owner_.pos != owner_.guard_anchor) {
            engine.issue_move_order(owner_.id, owner_.guard_anchor);
            owner_.state = UnitState::ReturningToPost;
        }
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
        if (owner_.pos != owner_.guard_anchor) {
            engine.issue_move_order(owner_.id, owner_.guard_anchor);
            owner_.state = UnitState::ReturningToPost;
        }
        return;
    }

    int32_t dist_to_anchor = owner_.guard_anchor.chebyshev_dist(target->pos);
    if (dist_to_anchor > PURSUIT_DISENGAGE_RADIUS) {
        target_unit_id_ = std::nullopt;
        guard_state_ = CombatGuardState::Returning;
        owner_.state = UnitState::ReturningToPost;
        if (owner_.pos != owner_.guard_anchor) {
            engine.issue_move_order(owner_.id, owner_.guard_anchor);
            owner_.state = UnitState::ReturningToPost;
        }
        return;
    }

    int32_t dist = owner_.pos.chebyshev_dist(target->pos);
    if (dist <= 1) {
        if (owner_.attack_cooldown_ticks == 0) {
            owner_.clear_path();
            owner_.state = UnitState::GuardIdle;
            engine.execute_melee_attack(owner_.id, target->id);
            target_unit_id_ = std::nullopt;
            guard_state_ = CombatGuardState::Returning;
        } else {
            owner_.clear_path();
            owner_.facing = ants::assets::vector_to_direction(target->pos.x - owner_.pos.x, target->pos.y - owner_.pos.y);
            owner_.state = UnitState::GuardIdle;
        }
        return;
    }

    // dist > 1: pathfind to target adjacent neighbor
    if ((owner_.state != UnitState::Walking && owner_.state != UnitState::Intercepting) || owner_.waypoints.empty() ||
        owner_.final_dest.chebyshev_dist(target->pos) > 1) {
        TileCoord best_neighbor = target->pos;
        int32_t best_dist = 999999;
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                TileCoord cand{target->pos.x + dx, target->pos.y + dy};
                if (grid.in_bounds(cand) && grid.get_cell(cand).is_passable()) {
                    int32_t dist_cand = owner_.pos.euclidean_dist_sq(cand);
                    if (dist_cand < best_dist) {
                        best_dist = dist_cand;
                        best_neighbor = cand;
                    }
                }
            }
        }
        if (best_dist < 999999 && best_neighbor != target->pos) {
            engine.issue_move_order(owner_.id, best_neighbor);
            owner_.state = UnitState::Intercepting;
        }
    }
}

void CombatAIController::update_returning(SimulationEngine& engine,
                                          const std::vector<AntUnit*>& all_units,
                                          const Grid& grid,
                                          const MatchStatsManager& stats,
                                          std::vector<AudioEvent>& audio_out,
                                          uint32_t random_seed) {
    if (owner_.pos == owner_.guard_anchor && owner_.state != UnitState::Walking && owner_.state != UnitState::ReturningToPost) {
        guard_state_ = CombatGuardState::Idle;
        owner_.state = UnitState::GuardIdle;
        return;
    }

    // Check for new intruders while returning
    AntUnit* new_target = find_best_target(all_units, stats);
    if (new_target) {
        target_unit_id_ = new_target->id;
        guard_state_ = CombatGuardState::Intercepting;
        owner_.state = UnitState::Intercepting;
        update_intercepting(engine, all_units, grid, stats, audio_out, random_seed);
        return;
    }

    if ((owner_.state != UnitState::Walking && owner_.state != UnitState::ReturningToPost) || owner_.waypoints.empty()) {
        if (owner_.pos != owner_.guard_anchor) {
            engine.issue_move_order(owner_.id, owner_.guard_anchor);
            owner_.state = UnitState::ReturningToPost;
        } else {
            guard_state_ = CombatGuardState::Idle;
            owner_.state = UnitState::GuardIdle;
        }
    }
}

} // namespace ants::sim
