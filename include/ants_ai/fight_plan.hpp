#pragma once

// A fight with a plan (docs/BOT_CONTEST.md, "A fight with a plan"; plan.fight_plan, off at every level: the arena shows no win; expires with v0.21.8). The standard bot answers a blow and hunts a kill (FightTask,
// OpportunityTask); nothing in it asks, before the first blow, whether the fight is worth having, and a fight that goes badly is fought to the end. This task looks at every place where an own ant and
// an enemy ant are within a few tiles of each other (an ENGAGEMENT: the contacts, grouped) and decides by the strength of the two sides there (Strength: the hit points times the blows of one round,
// the square law of the fights; ants that carry food do not fight):
//
//   an edge      the own ants that can fight within `fp_reach` tiles (fit, no load, not held by a task of a fight's rank or higher) have `fp_edge` percent of the enemy's strength: all of them strike the WEAKEST enemy
//                ant of the place (the fewest hit points: one that dies stops striking), one attack order each time an ant is idle again. No chase onto a bridge (a one-tile way): the ants hold the shore
//                (`fp_bridge_hold`), and a carrier that an enemy ant is near counts as a contact, so the ants near it guard it (`fp_guard`)
//   no edge      no ant of the place is sent into the fight by this task (the answer to a blow and the hunt are the other tasks' and keep their own rules); an ant with `fp_pull_hp` hit points or fewer that an
//                enemy ant is near is PULLED back: it walks into its hill, which heals it (and a power-up to stand on comes first when plan.power_stand has one: that task takes the ant)
//
// It reads the view alone (the ants' tiles, hit points, states, the bridges of the grid) and sends attack orders and plain moves. Counters for the reports: engagements, fights, pulls, attack orders.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class FightPlanTask final : public Task {
public:
    struct Params {
        uint32_t look_every{4};              // the task looks at this interval
        int32_t contact{3};                  // an own ant and an enemy ant within this many tiles are in contact
        int32_t cluster{6};                  // contacts within this many tiles of each other are one engagement
        int32_t engage{5};                   // the enemy ants within this many tiles of the engagement's center are its enemy side
        uint32_t pull_ticks{200};            // an ant that was pulled back is held this long (it walks home and heals)
        uint32_t linger_ticks{24};           // an ant that was sent to strike is held this long after the last look that wanted it
        uint32_t fit_hp{5};                  // an ant needs this many hit points to be sent into a fight
    };
    FightPlanTask(TaskId id, Tactics& tactics) : FightPlanTask(id, tactics, Params{}) {}
    FightPlanTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "fight-plan"; }
    void step(TaskContext& context) override;
    void finish(AntLedger& ledger) override;
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    // ---- for the tests and the reports ----
    uint32_t engagements() const noexcept { return engagements_; }       // the places that were looked at (each look counts each place)
    uint32_t fights() const noexcept { return fights_; }                 // ... where the own side had the edge and struck
    uint32_t skipped() const noexcept { return skipped_; }               // ... where it had none and no ant was sent
    uint32_t pulls() const noexcept { return pulls_; }                   // hurt ants that were pulled back
    uint32_t attacks() const noexcept { return attacks_; }               // attack orders given
    size_t held() const noexcept { return held_.size(); }

private:
    enum class Mode : uint8_t { Strike, Pull };
    struct Hold {
        Mode mode{Mode::Strike};
        uint64_t until{0};
    };
    Tactics& tactics_;
    Params params_;
    std::map<uint32_t, Hold> held_;
    uint64_t last_look_{0};
    uint32_t engagements_{0};
    uint32_t fights_{0};
    uint32_t skipped_{0};
    uint32_t pulls_{0};
    uint32_t attacks_{0};
};

}  // namespace ants::ai
