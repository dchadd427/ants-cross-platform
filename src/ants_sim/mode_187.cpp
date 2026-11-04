// The rules of the game type 187 that need a function of their own (include/ants_sim/game_mode.hpp, docs/GAMEPLAY.md "187"):
// the closed hill, the end rule, and the line "<Name> (<Color>) is out of ants!" (the side that stands last is last_side.cpp's).
#include "ants_sim/game_strings.hpp"
#include "sim_engine_impl.hpp"

namespace ants::sim {

// The closed hill (docs/GAMEPLAY.md, "187"): no ant enters its hill. `hill_closed()` (sim_engine_impl.hpp) is the one question; the places that ask it are go_to (through hill_goal_refused below),
// low_hp_check, anthillq_run, predict_order_ack and is_special_target_valid. set_hill_open_187 is the one way to play a 187 match of the earlier rules (a recording of the rules number 6).
void SimulationEngine::set_hill_open_187(bool open) {
    impl_->hill_open_187_ = open;
    impl_->world_state_dirty_ = true;
}

bool SimulationEngine::own_hill_enterable() const noexcept {
    return !impl_->hill_closed();
}

// Whether the goal `t` of an order of `a` is a hill that the ant may not go to now (its own, or an enemy's raid goal of a thief), and if so the refusal: the way a goal that no walk reaches is refused
// (deliver_path), "Can't go there." and the can't-go clip, the ant stopped where it stands. An enemy hill clicked by a worker is stopped silently as it always was (go_to).
bool SimulationEngineImpl::hill_goal_refused(AntUnit& a, TileCoord t) {
    if (!hill_closed()) return false;
    for (const auto& ah : grid_.anthills()) {
        if (t.x < static_cast<int32_t>(ah.x) || t.x > static_cast<int32_t>(ah.x) + 3 || t.y < static_cast<int32_t>(ah.y) || t.y > static_cast<int32_t>(ah.y) + 3) continue;
        if (ah.team_id != a.player_id && a.type != AntType::Thief) return false;       // (go_to's own rule: only a thief goes to an enemy hill, and 187 now has no raid)
        stop_sync(a);
        enter_cant_go(a);
        post_news(a.player_id, strings::kCantGoThere);
        return true;
    }
    return false;
}

// The end-rule fixes (the rules number 11; set_end_fix_187 turns them off for a recording of 6, 7 or 10). A team stands while it has an ant, a hatch in progress or an egg that could still be hatched. An egg
// can be hatched when its team has a hill: the newborn appears at the hill's entrance (hatch_run spends the egg and makes no ant without one, and the free hatch of a last ant that is removed, combat_system.cpp,
// is the same hatch). That is the rule of the game type ("the match ends once nobody can hatch any more ants and they have no ants left; the last team standing wins") for the team that holds eggs and nothing else on the map (no hill, no ant): it
// is not a team at all. A team that has a hill keeps counting for its eggs as it always did. The test reads state only, so every machine decides the same.
bool SimulationEngineImpl::egg_can_hatch(uint8_t team) const {
    return stats_.get_egg_count(team) > 0 && team_entrance(team).x >= 0;
}

// The seats of the roster, or (187 with the fixes) the teams that began: two teams that really had an ant, an egg that could hatch or a hatch when the match started (started_mask_), as the original's game counts them (last_side.cpp).
uint32_t SimulationEngineImpl::teams_in_match() const {
    const uint8_t mask = end_fixed_187() ? static_cast<uint8_t>(started_mask_ & roster_mask_) : roster_mask_;
    uint32_t n = 0;
    for (uint8_t k = 0; k < MAX_PLAYERS; ++k) {
        if ((mask & (1u << k)) != 0) ++n;
    }
    return n;
}

void SimulationEngine::set_end_fix_187(bool on) {
    impl_->end_fix_187_ = on;
    impl_->world_state_dirty_ = true;
}

bool SimulationEngine::end_fix_187() const noexcept {
    return impl_->end_fix_187_;
}

bool SimulationEngineImpl::end_rule_187() const {
    const uint32_t in_match = teams_in_match();
    const uint32_t sides = alive_sides();
    if (sides == 0) return true;                                // nobody has anything left (the original's rule too)
    return in_match >= 2 && sides == 1;                         // the last side standing wins at once, whatever its score (a match of one team has no one to beat)
}

// 187: a team that has no ant, no egg and no hatch left is out of the fight for good (nothing brings an egg or an ant back), so it is said once, as a News Flash for every player, with the cue
// of a player that drops out (playerout.wav, as FUN_0100d03b plays it; tick() never gets here once the match is over). Every machine runs this at the same tick on the same state, and the line and the cue are
// presentation (the news and audio queues are not hashed), so the machines stay in step whatever they do with them. A team that dropped out has the line "%s dropped out of the game!" and gets none.
void SimulationEngineImpl::announce_out_of_ants_187() {
    for (uint8_t k = 0; k < MAX_PLAYERS; ++k) {
        const uint8_t bit = static_cast<uint8_t>(1u << k);
        if ((roster_mask_ & bit) == 0 || (out_of_ants_told_ & bit) != 0) continue;
        if (team_dropped(k)) {
            out_of_ants_told_ = static_cast<uint8_t>(out_of_ants_told_ | bit);
            continue;
        }
        if (team_alive(k)) continue;
        out_of_ants_told_ = static_cast<uint8_t>(out_of_ants_told_ | bit);
        post_news_flash(strings::kOutOfAnts, player_display_name(k), player_color_name(k));
        audio_queue_.push_back(AudioEvent{SoundID::PlayerDropOut, 0, 0, 1, 255});
    }
}

}  // namespace ants::sim
