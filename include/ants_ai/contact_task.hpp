#pragma once

// One free ant strikes an enemy that stands beside it (docs/BOT_IDLE.md, "A blow for the ant beside the enemy"; plan.contact_strike, every level): the offence of the standard bot (FightTask: hunt, skirmish,
// assault) is made of groups: an assault wants two free ants and a force that outweighs what answers near the target, the hunt wants a wounded enemy, the answer to a blow wants a blow. A single idle
// worker beside an unhurt enemy that has hit nobody has no rule, and the watched match showed it: a Worker of one bot stood 82 seconds beside a Bomber of the player's and 52 beside his Thief, and nothing
// struck (docs/BOT_IDLE.md). A person who sees an ant of another team stand still beside his idle ant hits it, when he is not the one to lose: an ant that is not ordered to fight does not fight back (only a
// Combat Ant's reflex does), so the blow costs nothing, and what it takes (a hit point a blow, a Thief's raid, a carrier's walk) is the enemy's.
//
// The task takes an own ant that is free (or holds a chore of rank 2 or less: the economy, the guard, the ferry), that can strike, is not a Fire Ant (the keeper of the walls) or a Thief (the loot), has the
// hit points (`min_hp`) and stands idle, and an enemy ant that stands within one tile of it and has stood still for `stand_ticks` (a Thief any time: it cancels no raid by standing), that can be hit
// (attackable: not on a power-up or a hill tile, not in a clip), is no Combat Ant, has no more hit points than the striker and no ally of the seat. It is not begun where an enemy Combat Ant stands within
// `crowd_tiles` or two enemy ants that were drawn fighting lately do (a person looks first), nor within `home_tiles` of the enemy's hill (its ants answer there), nor in the last minute of a bot that leads (the
// guard stance). One attack order is one blow: the striker is ordered again each time it is idle after its blow (Priority::Urgent, the follow-up lane), while the enemy stays beside it, at most
// `max_blows` blows and `job_ticks` ticks; a striker with less than `stop_hp` hit points leaves the job. At most `max_jobs` jobs at a time. Everything it reads is on the view; it sends attack orders.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class ContactTask final : public Task {
public:
    struct Params {
        uint32_t stand_ticks{40};            // an enemy that has not changed tile for this long is a target (a walker passes by; a Thief is a target at once)
        uint32_t blow_gap{8};                // an attack order is repeated no sooner than this after the last one, and only for a striker that stands idle after its blow
        uint32_t job_ticks{300};             // a job ends after this long
        uint32_t max_blows{10};
        uint32_t max_jobs{2};
        uint8_t min_hp{6};                   // the striker has at least this many hit points when a job begins ...
        uint8_t stop_hp{4};                  // ... and leaves it below this many
        int32_t crowd_tiles{5};              // no job where an enemy Combat Ant, or two enemy ants drawn fighting lately, stand within this many tiles of the striker
        int32_t home_tiles{6};               // no job within this many tiles of the enemy's own hill, unless the target is a Thief
    };
    ContactTask(TaskId id, Tactics& tactics) : ContactTask(id, tactics, Params{}) {}
    ContactTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "contact"; }
    void step(TaskContext& context) override;
    void finish(AntLedger& ledger) override;
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    // ---- for the tests and the reports ----
    uint32_t started() const noexcept { return started_; }
    uint32_t blows() const noexcept { return blows_; }
    size_t jobs() const noexcept { return jobs_.size(); }

private:
    struct Job {
        uint32_t target{0};
        uint64_t started{0};
        uint64_t last_blow{0};
        uint32_t blows{0};
    };
    struct Still {
        sim::TileCoord tile{-1, -1};
        uint64_t since{0};
    };
    void release_all(TaskContext& context);

    Tactics& tactics_;
    Params params_;
    std::map<uint32_t, Job> jobs_;           // by the striker
    std::map<uint32_t, Still> still_;        // the ants of other teams: the tile and since when
    uint32_t started_{0};
    uint32_t blows_{0};
};

}  // namespace ants::ai
