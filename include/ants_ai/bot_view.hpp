#pragma once

// What a bot sees of the world: a read-only COPY of what a player of its seat can know (docs/BOTS.md, "Fairness in detail"). Built from a const
// SimulationEngine, so a bot cannot change the game through it. This is the minimal view of the plumbing milestone: the clock, the scores of every
// team, the alliance state, and the ants (everything about the seat's own ants; of the others only what is on the screen). Eggs, food piles, the map
// and the walking costs join in the next milestone.
//
// With Fog of War on every ant of every team would still be listed (WorldState does not hide them), which is why a bot together with fog is refused
// (check_setup, the lobby, NetGame, the controller): the view is only fair with fog off.

#include <array>
#include <cstdint>
#include <vector>

#include "ants_sim/ant_unit.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::ai {

struct AntView {
    uint32_t id{0};
    uint8_t team{0};
    sim::AntType type{sim::AntType::Worker};
    sim::TileCoord tile{};
    uint8_t hp{0};                       // own ants only (a player sees the bar of its own selection); another team's ant: 0
    sim::UnitState state{sim::UnitState::Idle};
    bool holding{false};                 // the sprite carries a crumb: visible on every ant
    int32_t carried_points{0};           // own ants only

    /// FUN_0101ff5a: only idle, walking, swimming and stunned ants accept an order of a player
    bool takes_orders() const noexcept {
        return state == sim::UnitState::Idle || state == sim::UnitState::GuardIdle || state == sim::UnitState::Walking ||
               state == sim::UnitState::Swimming || state == sim::UnitState::Stunned;
    }
    /// Standing still and ready for an order
    bool idle() const noexcept {
        return state == sim::UnitState::Idle || state == sim::UnitState::GuardIdle || state == sim::UnitState::Swimming;
    }
};

/// One team of the match as the score boxes and the alliance marks show it
struct TeamRow {
    bool present{false};                 // the team is in the roster of the match
    bool dropped{false};                 // it left or was dropped
    int32_t score{0};                    // the number its score box shows: its score plus its ally's, never below 0 (public: the score boxes show it)
    uint8_t ally{sim::ALLIANCE_NONE};    // the team it is allied with, ALLIANCE_NONE when it plays alone
};

class BotView {
public:
    /// Copies what `seat` may know from `sim`
    static BotView build(const sim::SimulationEngine& sim, uint8_t seat);

    uint64_t tick() const noexcept { return tick_; }
    /// Simulation ticks (50 ms) left on the match clock
    uint32_t ticks_left() const noexcept { return ticks_left_; }
    uint8_t seat() const noexcept { return seat_; }
    /// The number the seat's own score box shows (the team's score plus its ally's, never below 0)
    int32_t score() const noexcept { return score_; }
    uint8_t ally() const noexcept { return ally_; }
    /// The team whose invitation to team up waits for this seat's answer, 255 when none does
    uint8_t invite_from() const noexcept { return invite_from_; }
    const std::array<TeamRow, sim::MAX_PLAYERS>& rows() const noexcept { return rows_; }
    /// The seat's own living ants
    const std::vector<AntView>& mine() const noexcept { return mine_; }
    /// The living ants of every other team
    const std::vector<AntView>& others() const noexcept { return others_; }

private:
    uint64_t tick_{0};
    uint32_t ticks_left_{0};
    uint8_t seat_{0};
    int32_t score_{0};
    uint8_t ally_{sim::ALLIANCE_NONE};
    uint8_t invite_from_{255};
    std::array<TeamRow, sim::MAX_PLAYERS> rows_{};
    std::vector<AntView> mine_;
    std::vector<AntView> others_;
};

}  // namespace ants::ai
