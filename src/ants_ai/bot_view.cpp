#include "ants_ai/bot_view.hpp"

#include <algorithm>

namespace ants::ai {

BotView BotView::build(const sim::SimulationEngine& sim, uint8_t seat) {
    BotView v;
    v.seat_ = seat < sim::MAX_PLAYERS ? seat : uint8_t{0};
    const sim::WorldState& ws = sim.get_world_state();
    v.tick_ = sim.current_tick();
    v.ticks_left_ = ws.match_time_remaining_ms / sim::TICK_MS;
    // The scores are what the score boxes show (FUN_01021e36): the team's score plus its ally's, and a box draws 0 for a negative number. Not the individual score:
    // a person cannot see how the sum is made up.
    const auto shown = [&](uint8_t team) { return std::max<int32_t>(0, ws.player_scores[team]); };
    v.score_ = shown(v.seat_);
    v.ally_ = ws.player_alliances[v.seat_];
    v.invite_from_ = ws.pending_invite_from[v.seat_];
    const uint8_t roster = sim.roster_mask();
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        TeamRow& row = v.rows_[t];
        row.present = ((roster >> t) & 1u) != 0;
        row.dropped = ((ws.dropped_mask >> t) & 1u) != 0;
        row.score = shown(t);
        row.ally = ws.player_alliances[t];
    }
    for (const sim::AntSnapshot& a : ws.ants) {
        if (a.hp == 0 || a.state == sim::UnitState::Dead || a.state == sim::UnitState::Drowning) continue;      // gone: nobody sees it as an ant any more
        AntView av;
        av.id = a.id;
        av.team = a.player_id;
        av.type = a.type;
        av.tile = sim::TileCoord{a.tile_x, a.tile_y};
        av.state = a.state;
        av.holding = a.is_holding;
        if (a.player_id == v.seat_) {
            av.hp = static_cast<uint8_t>(a.hp > 255u ? 255u : a.hp);
            av.carried_points = a.carried_points;
            v.mine_.push_back(av);
        } else {
            v.others_.push_back(av);
        }
    }
    return v;
}

}  // namespace ants::ai
