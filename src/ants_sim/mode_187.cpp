// The rules of the game type 187 that need a function of their own (include/ants_sim/game_mode.hpp, docs/GAMEPLAY.md "187"):
// the end rule, the side that stands last, and the line "<Name> (<Color>) is out of ants!".
#include "ants_sim/game_strings.hpp"
#include "sim_engine_impl.hpp"

namespace ants::sim {

bool SimulationEngineImpl::end_rule_187() const {
    uint32_t in_match = 0;
    for (uint8_t k = 0; k < MAX_PLAYERS; ++k) {
        if ((roster_mask_ & (1u << k)) != 0) ++in_match;
    }
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

uint8_t SimulationEngineImpl::last_standing_187() const {
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
