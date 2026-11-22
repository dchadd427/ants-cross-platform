#pragma once

// The Thief that the engine keeps refusing (docs/BOT_UNSTUCK.md, "The click of a person who sees a Thief in the loop"; plan.free_thief, Hard). A Thief that raided a hole and holds the loot is sent home by the engine itself at the end
// of the raid clip; when no walk joins the raid tile to the own hill (the thief hole is walled in, a ring of fire round the gate) the walk is refused, the Thief plays the can't-go clip, and at its end the engine sends it home again:
// a loop that no order leaves, except at the one tick of each cycle (18 ticks, 900 ms) at which the path manager's answer has not been given yet (rules number 14 of the original's game, docs/GAME_REVERSE_ENGINEERING.md 5.40).
// What a person does who sees the Thief stand in the clip: he clicks it onto a tile where it can go, again and again, until it walks. The task does the same from what its looks show (the Thief's state, never the engine's
// queue): a Thief with loot that shows the clip at two looks over 40 ticks gets a VOLLEY, plain moves of one ant a few ticks apart (Orders::volley: 8 clicks, 3 and 4 ticks apart, a little more than one cycle), each paid for
// from the budget like any click, onto an ordinary tile of its walkable part (not a tile of a hill: the loop's goals are the hills' special tiles). A volley after which the Thief still shows the clip at a look is a miss, and
// the wait for the next one doubles (200 ticks, at most 800). The Thief then stands on an ordinary tile with its loot and nothing refuses it any more; when a walk home exists (the fire walls are out, the walls are down)
// the task clicks it onto the entrance, as the economy's rescue clicks a carrier that stands idle.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/tasks.hpp"

namespace ants::ai {

class FreeThiefTask final : public Task {
public:
    struct Params {
        uint32_t trap_looks{2};              // looks at which a Thief with loot showed the can't-go clip ...
        uint32_t trap_ticks{40};             // ... over at least this many ticks (two cycles of the loop: a refusal that a wall about to burn out gives is not a loop) before the volley
        std::vector<uint32_t> volley{0, 3, 7, 10, 14, 17, 21, 24};   // the volley: ticks after the first click (gaps of 3 and 4: about five clicks a second, 24 ticks, a cycle and a third)
        uint32_t judge_ticks{40};            // a volley is judged at the first look this long after it began: a Thief that showed the clip 30 ticks or more after the start was not freed
        uint32_t retry_ticks{200};           // the wait after a miss, doubled at every further miss ...
        uint32_t max_retry_ticks{800};       // ... up to this
        uint32_t home_cooldown_ticks{120};   // a free Thief is clicked onto the entrance no more often than this, the wait doubled at every click ...
        uint32_t max_volleys{6};             // ... and a Thief is given at most this many volleys and this many clicks home in a match (a Thief that is freed and loops again is not the task's for ever)
        uint32_t max_homes{6};
        uint32_t far_tiles{8};               // the task is for a Thief this many tiles or more from the own entrance (one at its own gate is the gate's: the stuck tally's 8 tiles)
        uint32_t tile_search{400};           // the tiles of its part that the search for a free tile looks at, nearest first
        uint32_t min_steps{2};               // the free tile is at least this many steps from the Thief
    };
    explicit FreeThiefTask(TaskId id) : FreeThiefTask(id, Params{}) {}
    FreeThiefTask(TaskId id, const Params& params) : Task(id), params_(params) {}
    const char* name() const noexcept override { return "free_thief"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    uint32_t volleys() const noexcept { return volleys_; }                // volleys given
    uint32_t clicks() const noexcept { return clicks_; }                  // clicks of them that left
    uint32_t freed() const noexcept { return freed_; }                    // volleys after which the Thief walked
    uint32_t misses() const noexcept { return misses_; }                  // volleys after which it still showed the clip
    uint32_t homeward() const noexcept { return homeward_; }              // clicks of a free Thief onto the entrance
    const Params& params() const noexcept { return params_; }
    void set_params(const Params& p) { params_ = p; }

private:
    static constexpr uint64_t kNone = ~uint64_t{0};
    struct Rec {
        uint32_t looks{0};                   // looks at which it showed the can't-go clip (since `streak`)
        uint64_t streak{0};                  // the tick of the first of them
        uint64_t last_cantgo{0};
        bool freed{false};                   // a volley took it off the tile: it is the task's until it banks
        uint64_t volley_at{kNone};           // the tick of the look that gave the volley that has not been judged yet
        uint64_t next_try{0};
        uint32_t misses{0};
        sim::TileCoord target{-1, -1};
        uint64_t home_after{0};
        uint32_t volleys_given{0};
        uint32_t homes_given{0};
    };
    sim::TileCoord free_tile(const TaskContext& context, const AntView& thief) const;

    Params params_;
    std::map<uint32_t, Rec> recs_;
    uint32_t volleys_{0};
    uint32_t clicks_{0};
    uint32_t freed_{0};
    uint32_t misses_{0};
    uint32_t homeward_{0};
};

}  // namespace ants::ai
