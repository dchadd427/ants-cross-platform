#pragma once

// What a bot sees of the world: a read-only COPY of what a player of its seat can know (docs/BOTS.md, "Fairness in detail"). Built from a const
// SimulationEngine, so a bot cannot change the game through it.
//
//   what it holds    the clock, the scores of every team (as the score boxes show them: 0 for a team that is not in the match or has dropped out), the alliance state,
//                    the seat's own egg stock and whether an egg is incubating, the seat's own ants (everything), every other team's ants (what is on the screen: an ant
//                    that has been ordered but still stands in its idle clip, because its path is not delivered or it waits for a blocker, is idle), and the food piles
//   what it hides    other teams' eggs and incubation, other teams' hit points and carried points, every ant's order and target, the engaged flag; and it never
//                    reads the news or audio queues (they belong to the screen: reading them would empty the HUD's queues)
//   copies           everything it keeps is a COPY: the engine's get_world_state() reference is a cache that is rewritten after any tick or command, so a view
//                    stays what it was when it was built, however the engine moves on
//   borrows          grid(), predict_ack() and has_pending_path() read the engine itself, so they are valid only while the bot thinks (the controller hands
//                    think() a view that has them; a COPY of a view does not: its grid is empty and its helpers answer "nothing")
//
// Known deviations from "exactly what the screen shows" (accepted, harmless, listed in docs/BOTS.md "Fairness in detail" next to the egg-tray and pile-unit ones):
//   * the exact egg stock and the exact units of a pile (a person sees a tray of at most nine eggs and counts the bites);
//   * grid() is the engine's own grid: the owner and the remaining life (timer_ticks, 3,600 ticks counting down from the moment of lighting or building) of fire walls and
//     bridges are on it although the screen draws every fire wall and every bridge alike. A bot that watches continuously would know the same by noting when it first saw the
//     wall, and who lit a wall is the fire ant next to it; the cost of hiding it (a copy of the grid per look) is not worth it.
//
// With Fog of War on every ant of every team would still be listed (WorldState does not hide them), which is why a bot together with fog is refused
// (check_setup, the lobby, NetGame, the controller): the view is only fair with fog off.

#include <array>
#include <cstdint>
#include <vector>

#include "ants_ai/map_info.hpp"
#include "ants_sim/ant_unit.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::ai {

struct AntView {
    uint32_t id{0};
    uint8_t team{0};
    /// What the sprite shows (what a person sees): the ant's own type, and for an ant that never took a power-up the level's default type (BotView::default_ant_type(),
    /// LVL block 3), which is Worker on every shipped map
    sim::AntType type{sim::AntType::Worker};
    sim::TileCoord tile{};
    uint8_t hp{0};                       // own ants only (a player sees the bar of its own selection); another team's ant: 0
    /// An own ant: the engine's label. Another team's ant: what the screen draws, so a label that says "walking" while the ant still stands in its idle clip (an order given a moment
    /// ago whose path the path manager has not delivered yet, or a pause behind a blocker) reads as idle: who has been ordered is not on any screen.
    sim::UnitState state{sim::UnitState::Idle};
    bool holding{false};                 // the sprite carries a crumb: visible on every ant
    int32_t carried_points{0};           // own ants only

    /// FUN_0101ff5a (the engine's can_take_user_order: action 0, 1 or 3, ant not engaged or frozen): the states of an ant that accepts an order of a player. A HINT: the engine also
    /// refuses an ant that is engaged or frozen, which the view does not show. Idle, standing guard (Combat Ant), swimming at rest, walking (also a swimmer diving into the water or
    /// climbing out of it: orig_action_of maps both to the walking action) and stunned.
    bool takes_orders() const noexcept {
        return state == sim::UnitState::Idle || state == sim::UnitState::GuardIdle || state == sim::UnitState::Walking || state == sim::UnitState::Swimming ||
               state == sim::UnitState::DivingInWater || state == sim::UnitState::ExitingWater || state == sim::UnitState::Stunned;
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
    /// The number its score box shows: its score plus its ally's, never below 0. The box of a team that is not in the match or has dropped out is covered and shows nothing
    /// (hud.cpp render_score_team), so the row says 0 for such a team: its score keeps changing in the engine (a raid on its hill) but not on any screen.
    int32_t score{0};
    uint8_t ally{sim::ALLIANCE_NONE};    // the team it is allied with, ALLIANCE_NONE when it plays alone
};

/// The points a lunchbox is reported to be worth. A lunchbox holds what the ant that died carried (a bite of 10 to 50 points, or stolen loot), but the screen draws one lunchbox
/// picture for all of them, so a view tells every bot this figure instead of the real value (a hidden number of the dead ant).
inline constexpr uint16_t kLunchboxNominalValue = 30;

/// A food pile (or a lunchbox: a pile of one unit) with units left. A person sees the pile's picture; the view gives the exact units (a documented, harmless deviation: a
/// person counts the bites, and this is the one place to coarsen it if that is ever wanted).
struct PileView {
    uint32_t index{0};                   // its place in the engine's table (Grid::food_objects()): stable for the whole match, the table only grows; two objects may share an anchor
    sim::TileCoord anchor{};             // the pile's tile (column, row)
    uint16_t remaining{0};               // units left (always more than 0 in a view)
    uint16_t value{0};                   // points per unit; for a lunchbox kLunchboxNominalValue, whatever the dead ant carried
    bool lunchbox{false};                // dropped by a dying ant: one unit
};

class BotView {
public:
    /// Copies what `seat` may know from `sim`. `map` (optional) is the analysis of the match, handed through to the bot: it must outlive every use of the view (the controller owns it).
    static BotView build(const sim::SimulationEngine& sim, uint8_t seat, const MapInfo* map = nullptr);

    BotView() = default;
    /// A copy keeps what was copied and drops what was borrowed (see the top of this file)
    BotView(const BotView& other);
    BotView& operator=(const BotView& other);
    BotView(BotView&&) noexcept = default;
    BotView& operator=(BotView&&) noexcept = default;

    uint64_t tick() const noexcept { return tick_; }
    /// Simulation ticks (50 ms) left on the match clock
    uint32_t ticks_left() const noexcept { return ticks_left_; }
    uint8_t seat() const noexcept { return seat_; }
    /// The number the seat's own score box shows (the team's score plus its ally's, never below 0; 0 when the seat itself has dropped out: its box is covered)
    int32_t score() const noexcept { return score_; }
    uint8_t ally() const noexcept { return ally_; }
    /// The team whose invitation to team up waits for this seat's answer, 255 when none does
    uint8_t invite_from() const noexcept { return invite_from_; }
    /// The seat's OWN egg stock (the hatch pedestal and its tray show it: the tray draws at most nine eggs, the view gives the exact number); nobody else's is readable
    uint32_t eggs() const noexcept { return eggs_; }
    /// An egg of the seat is incubating (the pedestal shows it)
    bool hatching() const noexcept { return hatching_; }
    const std::array<TeamRow, sim::MAX_PLAYERS>& rows() const noexcept { return rows_; }
    /// The seat's own living ants, by id
    const std::vector<AntView>& mine() const noexcept { return mine_; }
    /// The living ants of every other team, by id
    const std::vector<AntView>& others() const noexcept { return others_; }
    /// The piles that still have units, by index. The index is the engine's table position, which is MapInfo::piles()'s index for the piles of the map at the start; a lunchbox
    /// (PileView::lunchbox) is added to the table during the match and has an index past the end of MapInfo::piles() (MapInfo::pile() is null for it; approach_now works for it)
    const std::vector<PileView>& piles() const noexcept { return piles_; }
    /// The type that every ant shows until it takes a power-up: Worker, except on a level whose block 3 names a default type (some community maps: all their workers are
    /// Combat, Thief, Bomber, Swimmer or Fire ants, as the sprites and the panel say from the first second). An ant whose AntView::type is this one has not (visibly) changed.
    sim::AntType default_ant_type() const noexcept { return default_ant_type_; }
    /// The analysis of the match, null when the view was built without one
    const MapInfo* map() const noexcept { return map_; }
    /// What the engine's dynamic walking rules need besides the grid for this seat's ants: its ally (own and allied bombs block) and the teams that dropped out (their queue rows are
    /// open). Pass it to MapInfo::approach_now / field_now / walkable.
    WalkContext walk_context() const noexcept;

    /// Whether this view still borrows the engine (true for the view that think() gets; false for a copy)
    bool has_grid() const noexcept { return grid_ != nullptr; }
    /// The map as it is now: terrain, solid objects, food, power-ups, bombs with their owner, fire walls with their timers, bridges. A reference to the engine's own grid, valid
    /// only while the bot thinks; the grid of a view without a borrow is an empty one (width 0). (The owner and the timer of fire walls and bridges are a documented deviation, see the top.)
    const sim::Grid& grid() const noexcept;
    /// The engine's own prediction (SimulationEngine::predict_order_ack) of the ant that would acknowledge the command if the seat gave it now (0 = none), and (optional) how many ants
    /// need the order. It changes nothing; only the seat's own ants count (the issuer is always the seat). 0 and 0 for a view without a borrow.
    /// It is the prediction a person's click gets (the "immediate feedback" of a lock-step client), NOT a copy of apply_command: it agrees with apply_command for every order a worker
    /// or a combat ant can be given on open ground, and it differs in exactly these two ways (pinned by test AI1.5b, so that the engine changing either of them is noticed):
    ///   (1) a special order (bomb, fire wall, bridge) of a Bomber, Fire ant or Swimmer onto a tile where a WALKING ant stands (own or foreign): predicted 0, because the prediction
    ///       asks the cursor's rule (is_special_target_valid, which refuses any occupant), while apply_command asks the order's rule (which refuses only an ant that stands still) and
    ///       the ant acknowledges;
    ///   (2) a goal that the engine's goal adjustment refuses, such as a water or rock tile with nothing enterable within four tiles of it, or an occupied approach: predicted as
    ///       acknowledged (the prediction never looks at the goal), but apply_command's answer is 0 with status Applied.
    /// So 0 is NOT a promise that the order will be refused (case 1) and non-zero is NOT a promise that it will be carried out (case 2): a bot checks the next view.
    uint32_t predict_ack(const sim::Command& command, uint32_t* needed = nullptr) const;
    /// Whether a path request of one of the seat's own ants is still queued in the path manager (an order given a moment ago has not been answered yet). False for an ant that
    /// is not the seat's: what another team's ants have been told is not known to a player.
    bool has_pending_path(uint32_t ant) const;

private:
    const sim::SimulationEngine* sim_{nullptr};      // borrowed (see above)
    const sim::Grid* grid_{nullptr};                 // borrowed
    const MapInfo* map_{nullptr};
    uint64_t tick_{0};
    uint32_t ticks_left_{0};
    uint8_t seat_{0};
    int32_t score_{0};
    uint8_t ally_{sim::ALLIANCE_NONE};
    uint8_t invite_from_{255};
    uint32_t eggs_{0};
    bool hatching_{false};
    sim::AntType default_ant_type_{sim::AntType::Worker};
    std::array<TeamRow, sim::MAX_PLAYERS> rows_{};
    std::vector<AntView> mine_;
    std::vector<AntView> others_;
    std::vector<PileView> piles_;
};

}  // namespace ants::ai
