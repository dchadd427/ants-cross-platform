#pragma once

// The opportunistic kill (docs/BOT_FIGHT.md, rank 5 like a fight, and looked at BEFORE the fights and every chore): an enemy ant that ONE blow kills is struck from the tile that makes it so.
//
// A blow throws its victim from the attacker's tile through the victim's tile, one tile for a worker's blow and four for a Combat Ant's punch (knock.hpp), and an ant that lands in water
// drowns whatever its hit points, one that lands on a bomb loses two more, one that lands on a fire wall one more. So an enemy that stands next to water dies when the blow comes from the
// side away from the water, and lives when it comes from any other side; the engine takes the direction from the tile the attacker stands on when it strikes. An ordinary attack order lets the
// ant walk its own way and strike from wherever it arrives, which is why the hunts of FightTask (water_push_blows) only work when the ant happens to stand in line. This task walks the
// striker to the right tile first (a plain move), and strikes from there (an attack order on the neighbouring tile), once the model of the engine says that the blow kills:
//   Water  the victim is no Swimmer and the landing tile is water
//   Bomb / Fire  a bomb (or a fire wall, no Fire Ant) lies where it lands and the hit points that are left are not more than it takes
//   Blow   the victim has no more hit points than the blow takes
// Only an enemy that does not walk is a target (it stays where the striker will find it), unless an own ant already stands on the right tile (it strikes at once). The striker is the nearest ant
// that is free or holds a chore (the economy, the guard, the mines, the counters of bombs): a Fire Ant, a Bomber, a Thief, a Swimmer, a Combat Ant of a squad and the ants of a pick-up,
// a raid, the expedition or a fight are left alone. A job ends when the target is gone, moved, no longer dies of the blow, or after plan.opportunity_ticks.
//
// Everything it reads is on the view (the grid, the ants, their hit points and drawn states); it sends plain moves and attacks.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/knock.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

/// Whether an ant of `seat` that stands on `from` can walk to a tile next to `to` (the walker components of the start analysis; a hint: the bridge or the wall of the moment is not in it). An
/// attack order at an ant for which this is false is refused by the engine ("Can't go there."): the ant stands in its can't-go clip and nothing is struck.
bool walk_reaches(const MapInfo& map, uint8_t seat, sim::TileCoord from, sim::TileCoord to) noexcept;

class OpportunityTask final : public Task {
public:
    OpportunityTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "opportunity"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    /// Jobs begun, blows ordered, targets that were gone (dead or drowning) after a blow, jobs given up (the clock, a target that moved or no longer dies), and jobs of an ant that stood on the right tile already
    uint32_t started() const noexcept { return started_; }
    uint32_t blows() const noexcept { return blows_; }
    uint32_t kills() const noexcept { return kills_; }
    uint32_t gave_up() const noexcept { return gave_up_; }
    uint32_t ambushes() const noexcept { return ambushes_; }
    /// The jobs in hand now
    size_t jobs() const noexcept { return jobs_.size(); }
    /// The striker of the job against `target` (0: none)
    uint32_t striker_of(uint32_t target) const noexcept {
        for (const Job& j : jobs_) {
            if (j.target == target) return j.striker;
        }
        return 0;
    }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Job {
        uint32_t target{0};
        uint32_t striker{0};
        sim::TileCoord target_tile{};
        sim::TileCoord stand{};
        KillKind kind{KillKind::None};
        uint64_t decided{0};
        uint64_t move_sent{kPending};        // the tick the move order LEFT (kPending: not ordered, or not left yet)
        bool move_ordered{false};
        bool attack_ordered{false};
        uint64_t attack_sent{kPending};
        uint32_t blows{0};
    };
    struct Candidate {
        int32_t score{0};
        uint32_t target{0};
        uint32_t striker{0};
        sim::TileCoord target_tile{};
        sim::TileCoord stand{};
        KillKind kind{KillKind::None};
        bool at_stand{false};
    };
    bool shunned(uint32_t enemy, uint64_t tick) const noexcept;
    void run_jobs(TaskContext& context);
    void seek(TaskContext& context);
    void end_job(TaskContext& context, const Job& job);

    Tactics& tactics_;
    std::vector<Job> jobs_;
    std::map<uint32_t, uint64_t> shunned_;   // enemy ant -> the tick until which no job is begun against it
    std::map<int64_t, uint64_t> bad_stands_; // a tile that no striker reached in time -> the tick until which it is not used
    uint32_t started_{0};
    uint32_t blows_{0};
    uint32_t kills_{0};
    uint32_t gave_up_{0};
    uint32_t ambushes_{0};
};

}  // namespace ants::ai
