#pragma once

// A job for the ant that nothing has planned for (docs/BOT_IDLE.md, "Jobs for the ants that nothing holds"; plan.idle_jobs, every level). The watched match: a third of every bot's ant time was spent by
// ants that no task held and that stood where they were (Swimmers after the last pile, Bombers after their mines, a worker that no walk leads anywhere), and in the last 2 minutes 40 seconds, with the
// food gone, the bots were 93 to 97 percent idle while a Thief took 13 steals at their hills. An ant that stands idle for `idle_after` ticks, holds no food and no task holds, gets one of two jobs:
//   Heal   it has at most `idle_heal_hp` hit points and a walk leads to the own hill as the map is now: it is clicked onto the entrance (the hill's door restores every hit point), so that the ants that
//          the fights and the raids take are whole. Every level; the engine sends an ant home by itself only at one hit point
//   Hold   the food is gone, or an enemy Thief has been seen this match: up to `idle_posts` of the idle ants that a walk leads to the own thief hole stand on the tiles in front of it that no
//          wall, bomb or ant holds, and shut it (a raid order whose three tiles are held by standing ants ends in "Can't go there.", whoever gave it, and the raid task of a bot leaves such a
//          hole alone; the Thief of a person is told so at once). The own ants never use those tiles (the entrance is on the other side of the mound), and the points that a hill holds when the food is
//          gone can only be lost to a Thief. The holders are the tasks' to take again at any look (rank 2); a hurt one goes home first; Swimmers stay out of it while the bot has `endraid_keep_swimmers`
//          or fewer (they dig the bridges), Fire Ants (the keeper of the walls) and Thieves always
// Everything it reads is on the view; it sends plain moves. The map's walks as it is now (the flood that tells which ants a walk leads home) are looked at once in 100 ticks: the cost of the task on a
// server that runs many bots is the cost of that flood.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class IdleJobsTask final : public Task {
public:
    IdleJobsTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "idle-jobs"; }
    void step(TaskContext& context) override;
    void finish(AntLedger& ledger) override;

    // ---- for the tests and the reports ----
    uint32_t heals() const noexcept { return heals_; }
    uint32_t holds() const noexcept { return holds_; }
    /// The ants that hold a post now
    size_t holders() const noexcept { return holders_.size(); }

private:
    struct Idle {
        sim::TileCoord tile{-1, -1};
        uint64_t since{0};
    };
    struct Post {
        sim::TileCoord tile{-1, -1};
        uint64_t ordered{0};
    };
    void release_all(TaskContext& context);

    /// The walks to the own hill as the map is now (a flood of the whole grid: made at most once in kJoinedTicks, it changes slowly)
    static constexpr uint64_t kJoinedTicks = 100;
    MapInfo::NowField joined_;
    uint64_t joined_tick_{0};
    bool joined_made_{false};

    Tactics& tactics_;
    std::map<uint32_t, Idle> idle_;          // the own ants that stand idle and empty-handed with no task: where and since when
    std::map<uint32_t, uint64_t> healing_;   // ant -> the tick it was sent home
    std::map<uint32_t, Post> holders_;       // ant -> the post it holds
    uint32_t heals_{0};
    uint32_t holds_{0};
};

}  // namespace ants::ai
