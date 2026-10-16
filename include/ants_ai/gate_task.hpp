#pragma once

// GateTask: works the gate goal (gate_goal.hpp) with an ant of the right kind (docs/reverse_engineering/bomb_gates.md, "Gate goals"). Medium and Hard, on a walled map only
// (Tactics::gate_map: the analysis of the start finds no pile that the hill reaches): a bot whose food is walled off by fire walls, bombs or rock gets the ant that the way past needs first, and does it.
//
//   plan       every `look_every` ticks while nothing is under way (and when the fire walls and bombs on the map changed): GateInfo::analyse of the grid as it is, then plan_gate_goal
//   fetch      the ant of the goal's kind: an own ant of that type (a Fire ant, a Bomber) is taken from the lower ranks, else the task WANTS one (Tactics::wants) and the power-up task fetches it
//   extinguish the Fire ant is ordered onto the fire cell (a special order: it walks next to it and puts it out); done when the cell is no longer fire
//   hop        the Bomber plants on the bomb tile T (a special order), steps to the tile P in front of it, and is clicked onto the bomb (the own bomb is no obstacle for an order onto it): it is thrown
//              by the real rule. A bomb that was a dud (the ant burns on the spot, the bomb is gone) is planted again while the ant has the hit points for the chain (2 per bomb and one over);
//              the ant that lands where the chain ends is done, and for a power-up it is clicked onto it (a flight takes nothing; a move that ends on the tile does). The tile of the bomb and the
//              tile in front of it must be free of ants: an own ant without a job that stands on one is sent aside (and held until the goal is over), an ant of another team is waited for
//              (a special order onto a tile with an ant is refused by the controller and the engine alike)
//   time       a step with no progress for `step_ticks` (90 s) gives up the attempt; so does an attempt that is not done after `attempt_ticks` (4 minutes); `retries` (2) more attempts follow, then the task
//              is exhausted and the bot plays as if there were no gates
//
// Everything is read from the BotView; nothing is a promise: an ant that died, a bomb that is gone, a wall that went out by itself are seen at the next look.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/gate_goal.hpp"
#include "ants_ai/gate_info.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class GateGoalTask final : public Task {
public:
    struct Params {
        uint32_t step_ticks{1800};           // no progress for this long: the attempt is given up (90 s)
        uint32_t attempt_ticks{4800};        // an attempt not done after this long is given up (4 minutes)
        uint32_t retries{2};                 // attempts after the first one that was given up
        uint32_t hop_bombs{3};               // the most bombs of a hop chain
        uint32_t min_ticks_left{2400};       // no attempt starts with less than this left on the clock
        uint32_t look_every{200};            // the goal is chosen again this often while nothing is under way
        uint32_t order_gap{120};             // no second order to one ant within this many ticks (the bot's stall detector counts equal orders)
        uint32_t pause_ticks{100};           // the pause after an attempt that was given up
        uint32_t revivals_max{2};            // times a task that ran out of attempts starts afresh because a source of its ant appeared (a new Bomber or Fire ant, a power-up of the kind)
        uint32_t goals_max{8};               // goals done in a match (a map that keeps making gates is left alone after this many)
    };
    enum class Phase : uint8_t { Idle, Fetch, Act, Exhausted };

    GateGoalTask(TaskId id, Tactics& tactics) : GateGoalTask(id, tactics, Params{}) {}
    GateGoalTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "gate_goal"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;
    void finish(AntLedger& ledger) override;
    /// The bot goes to the plain economy (the stall detector's fallback): the ant is given back and the attempt dropped, without counting as one given up. A task that was exhausted stays so.
    void cancel(AntLedger& ledger);
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    // ---- for the tests and the reports ----
    Phase phase() const noexcept { return phase_; }
    const GateGoal& goal() const noexcept { return goal_; }
    uint32_t ant() const noexcept { return ant_; }
    uint32_t attempts() const noexcept { return attempts_; }
    uint32_t given_up() const noexcept { return given_up_; }
    uint32_t done() const noexcept { return done_; }
    uint32_t plants() const noexcept { return plants_; }
    uint32_t hops_ordered() const noexcept { return hops_ordered_; }
    uint32_t duds() const noexcept { return duds_; }
    uint32_t extinguishes() const noexcept { return extinguishes_; }
    uint32_t defuses() const noexcept { return defuses_; }
    uint32_t picks() const noexcept { return picks_; }
    uint32_t revivals() const noexcept { return revivals_; }
    uint64_t first_goal_tick() const noexcept { return first_goal_; }
    /// Why the last attempt was given up ("step", "attempt", "clock", "ant", "stranded", "hurt"); empty before one was
    const char* last_failure() const noexcept { return why_; }
    bool exhausted() const noexcept { return phase_ == Phase::Exhausted; }

private:
    void plan(TaskContext& context);
    void fetch(TaskContext& context);
    void act(TaskContext& context);
    void act_extinguish(TaskContext& context, const AntView& ant);
    void act_defuse(TaskContext& context, const AntView& ant);
    void act_hop(TaskContext& context, const AntView& ant);
    void succeed(TaskContext& context);
    void fail(TaskContext& context, const char* why);
    bool may_order(uint64_t now) const noexcept { return last_order_ == 0 || now >= last_order_ + params_.order_gap; }
    bool tile_blocked(TaskContext& context, sim::TileCoord tile);
    /// (LevelPlan::reach_aware) Whether a walk of the ant gets to `target` now: onto the power-up on it, or, for the hill's entrance, to the hill (the map as it is now, the seat's own and its ally's
    /// bombs closed). A hop chain can end anywhere (a dud, a landing on a bomb that throws the ant on): an order that no walk can carry out is not given, the attempt is given up and planned again
    bool walk_reaches(const TaskContext& context, const AntView& ant, sim::TileCoord target) const;
    uint64_t signature(const TaskContext& context) const;
    uint64_t sources(const TaskContext& context) const;

    Tactics& tactics_;
    Params params_;
    Phase phase_{Phase::Idle};
    GateGoal goal_;
    GateInfo gates_;                         // the analysis the goal was planned from
    uint32_t ant_{0};
    uint64_t attempt_start_{0};
    uint64_t progress_{0};                   // the look at which the attempt last made progress
    uint64_t last_order_{0};
    uint64_t next_plan_{0};
    uint64_t signature_{0};
    bool signature_known_{false};
    uint64_t pause_until_{0};
    uint32_t attempts_{0};
    uint32_t failures_{0};                   // attempts given up (more than `retries` of them end the task)
    uint32_t given_up_{0};
    uint32_t done_{0};
    uint32_t plants_{0};
    uint32_t hops_ordered_{0};
    uint32_t duds_{0};
    uint32_t extinguishes_{0};
    uint32_t defuses_{0};
    uint32_t picks_{0};
    uint64_t first_goal_{0};
    // watched state of the ant, to tell progress and duds
    sim::TileCoord seen_tile_{-1, -1};
    uint8_t seen_hp_{0};
    bool seen_bomb_{false};
    bool landed_{false};
    sim::TileCoord island_target_{-1, -1};
    BombHop return_hop_{};
    const char* why_{""};
    bool thrown_{false};                     // a click onto the bomb was given and the throw has not been accounted for yet
    bool finished_{false};                   // goals_max goals are done: nothing more is tried
    uint64_t exhausted_sources_{0};          // the sources (typed ants and power-ups of the hill's side) when the attempts ran out
    uint32_t revivals_{0};                   // times the task started afresh because a source appeared (at most `revivals_max`)
};

}  // namespace ants::ai
