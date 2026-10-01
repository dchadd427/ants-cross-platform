#include "ants_ai/bot_view.hpp"

#include <algorithm>

namespace ants::ai {

BotView::BotView(const BotView& other)
    : sim_(nullptr),
      grid_(nullptr),
      map_(other.map_),
      tick_(other.tick_),
      ticks_left_(other.ticks_left_),
      seat_(other.seat_),
      score_(other.score_),
      ally_(other.ally_),
      invite_from_(other.invite_from_),
      eggs_(other.eggs_),
      hatching_(other.hatching_),
      rows_(other.rows_),
      mine_(other.mine_),
      others_(other.others_),
      piles_(other.piles_) {}

BotView& BotView::operator=(const BotView& other) {
    if (this != &other) {
        BotView copy(other);
        *this = std::move(copy);
    }
    return *this;
}

const sim::Grid& BotView::grid() const noexcept {
    static const sim::Grid empty;
    return grid_ != nullptr ? *grid_ : empty;
}

uint32_t BotView::predict_ack(const sim::Command& command, uint32_t* needed) const {
    if (needed != nullptr) *needed = 0;
    if (sim_ == nullptr) return 0;
    sim::Command c = command;
    c.issuer = seat_;                                           // the seat's own ants only: the engine ignores the ants of other teams in a command
    return sim_->predict_order_ack(c, needed);
}

bool BotView::has_pending_path(uint32_t ant) const {
    if (sim_ == nullptr) return false;
    const auto it = std::lower_bound(mine_.begin(), mine_.end(), ant, [](const AntView& a, uint32_t id) { return a.id < id; });
    if (it == mine_.end() || it->id != ant) return false;       // not an ant of the seat (or gone): a player does not know what other teams' ants were told
    return sim_->has_pending_path(ant);
}

BotView BotView::build(const sim::SimulationEngine& sim, uint8_t seat, const MapInfo* map) {
    BotView v;
    v.sim_ = &sim;
    v.grid_ = &sim.grid();
    v.map_ = map;
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
    // The own egg stock and incubator only: the hatch pedestal of a person's HUD shows its own team's (hud.cpp), nobody else's is on any screen
    v.eggs_ = sim.get_player_eggs(v.seat_);
    v.hatching_ = sim.get_pending_hatch_count(v.seat_) != 0;
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
    const auto by_id = [](const AntView& x, const AntView& y) { return x.id < y.id; };
    if (!std::is_sorted(v.mine_.begin(), v.mine_.end(), by_id)) std::sort(v.mine_.begin(), v.mine_.end(), by_id);        // ids are unique: any sort gives one order
    if (!std::is_sorted(v.others_.begin(), v.others_.end(), by_id)) std::sort(v.others_.begin(), v.others_.end(), by_id);
    const std::vector<sim::FoodObject>& objects = sim.grid().food_objects();
    for (size_t i = 0; i < objects.size(); ++i) {
        const sim::FoodObject& o = objects[i];
        if (o.remaining == 0) continue;
        PileView p;
        p.index = static_cast<uint32_t>(i);
        p.anchor = sim::TileCoord{o.col, o.row};
        p.remaining = o.remaining;
        p.value = o.value;
        p.lunchbox = sim.grid().has_lunchbox_at(p.anchor);
        v.piles_.push_back(p);
    }
    return v;
}

}  // namespace ants::ai
