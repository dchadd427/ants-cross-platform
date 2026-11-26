#pragma once

// The contest at a flower strip (docs/BOT_CONTEST.md, "The flower strip"; plan.strip_fight, on at Hard: the arena shows a win on ISLANDS). Where a flower drops, the bots' spare ants wait beside the drop
// tile for the power-up that falls (SwarmTask on ISLANDS, the watcher of the flowers on SMALL, MEDIUM and GAUNTLET), and ants of other teams wait there too: four teams' ants side by side under one flower, nobody
// touching anybody (a report after a match on ISLANDS: "they should fight for the flower"). This task makes the bot fight for it:
//
//   a contest   an own ant waits within `strip_radius` tiles of a flower's drop tile and an enemy ant stands idle there (drawn idle, hit points on the view, not on a power-up: that one is immune)
//   the edge    the own ants there and the own ants that can strike within `strip_help_reach` tiles (the posts at the other strips, an ant of the economy near) against the enemy ants within 5 tiles of
//               the drop tile, by the strength of the two sides (Strength: hit points times blows); with `strip_edge_percent` of the enemy's, all of them strike the enemy ant with the fewest hit points
//               (one attack order each time an ant is idle again)
//   holding     once no enemy ant waits there the own ants stay where they wait (they are the swarm's again); the task asks for nothing more. Without the edge nothing is done: leaving when losing
//               was built and measured, cost the flower power-ups and was taken out again (docs/BOT_CONTEST.md)
//
// It reads what the screen shows (ants, tiles, hit points, drawn states, the flowers) and sends attack orders and plain moves.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class StripTask final : public Task {
public:
    struct Params {
        uint32_t look_every{4};              // the task looks at this interval
        int32_t enemy_reach{5};              // the enemy ants within this many tiles of the drop tile are the other side
        uint32_t linger_ticks{24};           // an ant that was sent to strike is held this long after the last look that wanted it
        uint32_t fit_hp{5};                  // an ant needs this many hit points to strike
    };
    StripTask(TaskId id, Tactics& tactics) : StripTask(id, tactics, Params{}) {}
    StripTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "strip"; }
    void step(TaskContext& context) override;
    void finish(AntLedger& ledger) override;
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    // ---- for the tests and the reports ----
    uint32_t contests() const noexcept { return contests_; }       // looks at which an own ant and an enemy ant waited at a flower
    uint32_t strikes() const noexcept { return strikes_; }         // ... at which the own side had the edge and struck
    uint32_t helped() const noexcept { return helped_; }           // ants from beyond the strip that joined a strike
    uint32_t attacks() const noexcept { return attacks_; }         // attack orders given
    size_t held() const noexcept { return held_.size(); }

private:
    Tactics& tactics_;
    Params params_;
    std::map<uint32_t, uint64_t> held_;                          // the ants that strike now, by id: the tick until which the task holds each
    uint64_t last_look_{0};
    uint32_t contests_{0};
    uint32_t strikes_{0};
    uint32_t helped_{0};
    uint32_t attacks_{0};
};

}  // namespace ants::ai
