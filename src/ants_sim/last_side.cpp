// The last side standing (docs/GAMEPLAY.md, "The last side standing"): the end rule that the rules number 8 of the original's game adds to the original's CHECKGO. The original ends a match early
// only when every live team is the local team or its ally and that alliance has the strictly best score, so a tie for the top, or a score of 0, never wins and a match with no food on the map
// (or none left) played on to the clock with one side alive. Now, when exactly ONE side still has an ant, an egg or a hatch in progress and nothing is left that could score, the match ends and that side
// wins a tie. The match ends early because nothing can change the scores after that, so the results are what the clock would give: the highest score wins even with no ant left, and only a tie for the top (or all scores 0) goes to the side that is left (MatchResult::tie_standing). Game type 187 has an end rule of its own (mode_187.cpp) and never reads this one. set_last_side_rule(false) is the way to play a recording of the rules number 5.
#include "sim_engine_impl.hpp"

namespace ants::sim {

void SimulationEngine::set_last_side_rule(bool on) {
    impl_->last_side_rule_ = on;
    impl_->world_state_dirty_ = true;
}

bool SimulationEngine::last_side_rule() const noexcept {
    return impl_->last_side_ends();
}

// What could still change a score. Only food does: a pile (or a lunchbox, which is a pile of one unit) with units left that are worth points; an ant that carries food or loot (it deposits it, or drops it as a
// lunchbox when it dies); an ant in the middle of a bite (the pile loses the unit at the end of the clip, so the ant gets food from a pile that may already read empty). A unit worth 0 points scores
// nothing (the ant that takes it carries nothing). Power-ups and bombs never count: a power-up only changes an ant's type, a bomb and a fire only hurt, the flower droppers drop power-ups and nothing else,
// and no ant, egg or pile comes out of any of them. A live Thief counts while an enemy hill's team has a score to take (a raid is loot, cleanup_raid; with 187's closed hill there is none, and this rule is not 187's).
// What cannot be told cheaply counts too and plays on to the clock as it always did: a pile that no ant can reach, a carrier that cannot get home, a Thief that cannot reach the hill.
bool SimulationEngineImpl::scoring_food_left() const {
    for (const FoodObject& pile : grid_.food_objects()) {
        if (pile.remaining > 0 && pile.value > 0) return true;
    }
    for (const auto& a : ants_) {
        if (a == nullptr || a->removed) continue;
        if (a->is_holding() || orig_action_of(*a) == AntUnit::kActionHarvest) return true;
        if (type_of(*a) == AntType::Thief) {                                                    // a raid moves points between the thief's side and the hill it raids, dead team or not (cleanup_raid)
            for (const auto& hill : grid_.anthills()) {
                if (hill.team_id < MAX_PLAYERS && hill.team_id != a->player_id && !team_dropped(hill.team_id) && stats_.get_individual_score(hill.team_id) != 0) return true;
            }
        }
    }
    return false;
}

uint8_t SimulationEngineImpl::alive_mask() const {
    uint8_t mask = 0;
    for (uint8_t k = 0; k < MAX_PLAYERS; ++k) {
        if ((roster_mask_ & (1u << k)) != 0 && !team_dropped(k) && team_alive(k)) mask = static_cast<uint8_t>(mask | (1u << k));
    }
    return mask;
}

// "At least two teams in the match" means two teams that really began with an ant, an egg or a hatch (started_mask_), not two seats of the roster: a roster of four on a map with one hill and one team's ants is a
// match of one team, which CHECKGO never ended for lack of an enemy and this rule does not either. Nobody left at all is the original's rule (checkgo_ends_for).
bool SimulationEngineImpl::last_side_decides() const {
    uint32_t began = 0;
    for (uint8_t k = 0; k < MAX_PLAYERS; ++k) {
        if (((started_mask_ & roster_mask_) & (1u << k)) != 0) ++began;
    }
    return began >= 2 && alive_sides() == 1 && !scoring_food_left();
}

void SimulationEngineImpl::mark_side_left(MatchResult& result) const {
    if (game_mode_ == GameMode::Kills187) {
        result.standing = last_standing();
    } else if (last_side_ends() && last_side_decides()) {
        result.tie_standing = last_standing();
    } else {
        return;
    }
    result.decide_winners();
}

uint8_t SimulationEngineImpl::last_standing() const {
    uint32_t in_match = 0;
    for (uint8_t k = 0; k < MAX_PLAYERS; ++k) {
        if ((roster_mask_ & (1u << k)) != 0) ++in_match;
    }
    if (in_match < 2 || alive_sides() != 1) return PLAYER_NEUTRAL;
    for (uint8_t k = 0; k < MAX_PLAYERS; ++k) {
        if ((roster_mask_ & (1u << k)) != 0 && !team_dropped(k) && team_alive(k)) return k;
    }
    return PLAYER_NEUTRAL;
}

}  // namespace ants::sim
