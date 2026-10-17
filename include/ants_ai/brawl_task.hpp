#pragma once

// The brawl of game type 187 (docs/BOTS.md, "187: the brawl"; docs/GAMEPLAY.md, "187"). In 187 there is no food and no power-up, the kills are the score and the last side with ants
// wins, so a standard bot that sees the game mode plays this task INSTEAD of the food game's tasks: it keeps its profile (look interval, reaction time, command budget), its answer to an
// invitation to team up and its fairness, and spends every ant on the fight.
//
// What the engine allows (measured, docs/BOTS.md "Fights of its own"): one blow lands per hit clip however many ants strike; a worker's blow takes 1 hit point and throws the victim one
// tile away from the attacker, a Combat Ant's punch takes 2 and throws it four; an ant left with ONE hit point walks home and is rarely caught; an ant thrown into water drowns whatever its hit
// points. So the kills are of four kinds: a blow that throws the victim into water (or onto a bomb) from the side that the bot chooses, the last hit point of a wounded ant, a Combat Ant's
// punch on an even number of hit points, and a pack that corners a retreating ant. The task
//   * sends every ant that is hurt home to heal (the hill restores all hit points) and brings it back,
//   * takes the kills that stand in reach: it walks a healthy ant to the side of a victim from which one blow kills it, then strikes,
//   * otherwise attacks the nearest enemy ants in packs of a few, the wounded first, each pack on its own target (blows land in parallel on different victims),
//   * and when no enemy ant can be struck (every one is inside its hill) marches to the enemy hills and waits at their doors.
// It reads only the BotView (what a player sees), sends only clicks a person could make, uses integers and ordered containers: the same world gives the same orders.

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "ants_ai/brawl_params.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class BrawlTask final : public Task {
public:
    BrawlTask(TaskId id, const BrawlParams& params) : Task(id), params_(params) {}

    void set_params(const BrawlParams& params) noexcept { params_ = params; }
    const char* name() const noexcept override { return "brawl"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    uint32_t attacks_ordered() const noexcept { return attacks_ordered_; }
    uint32_t kill_stands_ordered() const noexcept { return kill_stands_ordered_; }
    uint32_t kill_blows_ordered() const noexcept { return kill_blows_ordered_; }
    uint32_t retreats_ordered() const noexcept { return retreats_ordered_; }
    uint32_t marches_ordered() const noexcept { return marches_ordered_; }

private:
    enum class Kind : uint8_t { None, Attack, Stage, Home, March };
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Job {
        Kind kind{Kind::None};
        uint32_t target{0};              // the enemy ant of an Attack or a Stage
        sim::TileCoord tile{};           // where the target stood when the order was decided / where a Stage, Home or March leads
        uint64_t decided{0};
        uint64_t sent{kPending};         // the tick the order left (kPending: not yet, or never)
        bool refused{false};             // the "can't go" that followed it was counted
    };
    /// The last group order that named the ants of one victim's pack
    struct Fight {
        uint64_t ordered{0};
        sim::TileCoord tile{-1, -1};     // the tile that order named (where the victim stood)
    };
    struct Refusal {
        uint32_t count{0};
        uint64_t last{0};
    };

    bool pending(const Job& job, const Profile& profile, uint64_t now) const noexcept;
    bool shunned(uint32_t ant, uint32_t target, uint64_t now) const noexcept;

    BrawlParams params_;
    std::map<uint32_t, Job> jobs_;       // by own ant
    std::map<uint32_t, Fight> fights_;   // by victim
    std::map<std::pair<uint32_t, uint32_t>, Refusal> refusals_;
    std::map<std::pair<uint32_t, uint32_t>, uint64_t> shun_;   // (own ant, enemy ant) -> until when the pair is left alone: an attack that showed "can't go" (the enemy stands where no walk joins them)
    std::vector<uint32_t> occupant_;     // the id of the ant on each tile (scratch of one look, cleared again at its end)
    uint32_t attacks_ordered_{0};
    uint32_t kill_stands_ordered_{0};
    uint32_t kill_blows_ordered_{0};
    uint32_t retreats_ordered_{0};
    uint32_t marches_ordered_{0};
};

}  // namespace ants::ai
