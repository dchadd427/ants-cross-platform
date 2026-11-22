#pragma once

// The Thief that the engine keeps refusing (docs/BOT_UNSTUCK.md, "The boundary click"; plan.free_thief). A Thief that raided a hole and holds the loot is sent home by the engine itself at the end of the raid clip;
// when no walk joins the raid tile to the own hill (the thief hole is walled in, a ring of fire round the gate) the walk is refused, the Thief plays the can't-go clip, and at its end the engine sends it home again: a loop that no order leaves, except
// at the one tick of each cycle (18 ticks, 900 ms) at which the path manager's answer has not been given yet (rules number 14 of the original's game, docs/GAME_REVERSE_ENGINEERING.md 5.40). The task sees the Thief show
// the can't-go clip at two looks over 40 ticks, picks an ordinary tile of its walkable part (not a tile of a hill: the loop's goals are the hills' special tiles), and proposes ONE boundary click for it (Orders::boundary_move):
// the controller releases it at that tick. The Thief then stands on an ordinary tile with its loot and nothing refuses it any more; when a walk home exists (the fire walls are out, the walls are down) the task clicks it
// onto the entrance, as the economy's rescue clicks a carrier that stands idle.
//
// The task reads only the view and the map's analysis and puts plain clicks into the orders: what a person does who sees a thief in the loop and clicks it away at the right moment.

#include <cstdint>
#include <map>

#include "ants_ai/tasks.hpp"

namespace ants::ai {

class FreeThiefTask final : public Task {
public:
    struct Params {
        uint32_t trap_looks{2};              // looks at which a Thief with loot showed the can't-go clip ...
        uint32_t trap_ticks{40};             // ... over at least this many ticks (two cycles of the loop: a refusal that a wall about to burn out gives is not a loop) before the click is proposed
        uint32_t retry_ticks{40};            // a click that never left (it expired, or a newer order took the ant) is proposed again this long after
        uint32_t max_retry_ticks{400};       // ... longer after every miss, up to this
        uint32_t home_cooldown_ticks{120};   // a free Thief is clicked onto the entrance no more often than this
        uint32_t tile_search{400};           // the tiles of its part that the search for a free tile looks at, nearest first
        uint32_t min_steps{2};               // the free tile is at least this many steps from the Thief
    };
    explicit FreeThiefTask(TaskId id) : FreeThiefTask(id, Params{}) {}
    FreeThiefTask(TaskId id, const Params& params) : Task(id), params_(params) {}
    const char* name() const noexcept override { return "free_thief"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    uint32_t proposed() const noexcept { return proposed_; }              // boundary clicks proposed
    uint32_t sent() const noexcept { return sent_; }                      // ... of them released
    uint32_t freed() const noexcept { return freed_; }                    // ... after which the Thief walked
    uint32_t homeward() const noexcept { return homeward_; }              // clicks of a free Thief onto the entrance
    uint32_t last_thief() const noexcept { return last_thief_; }          // the Thief of the latest boundary click and its tile
    sim::TileCoord last_target() const noexcept { return last_target_; }
    const Params& params() const noexcept { return params_; }
    void set_params(const Params& p) noexcept { params_ = p; }

private:
    static constexpr uint64_t kNone = ~uint64_t{0};
    struct Rec {
        uint32_t looks{0};                   // looks at which it showed the can't-go clip (since `streak`)
        uint64_t streak{0};                  // the tick of the first of them
        bool freed{false};                   // a boundary click of this task took it off the tile: it is the task's until it banks
        uint64_t last_cantgo{0};
        uint64_t armed_until{0};             // a boundary click is waiting until
        uint64_t sent{kNone};                // the tick at which a click left (and the Thief has not walked since)
        uint64_t next_try{0};
        uint32_t misses{0};
        sim::TileCoord target{-1, -1};
        uint64_t home_after{0};
    };
    sim::TileCoord free_tile(const TaskContext& context, const AntView& thief) const;

    Params params_;
    std::map<uint32_t, Rec> recs_;
    uint32_t proposed_{0};
    uint32_t sent_{0};
    uint32_t freed_{0};
    uint32_t homeward_{0};
    uint32_t last_thief_{0};
    sim::TileCoord last_target_{-1, -1};
};

}  // namespace ants::ai
