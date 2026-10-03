#pragma once

// The tasks of the standard bot (standard_bot.hpp), built on the task model of tasks.hpp: each claims ants through the AntLedger (a task of a higher rank may take an ant from one of a
// lower rank), puts orders into Orders, reads the result from the next view and ends when the reason is gone. They share the Tactics (the level's plan and the bot's memory).
//
//   rank 5  FightTask     strike back at an enemy that hit an own ant, and attack an enemy Thief that is on its way to the own hill
//   rank 4  WallTask      the Fire Ant that keeps three fire walls in front of the own thief hole
//   rank 3  PowerUpTask   one ant at a time is sent to take a power-up that the bot wants; RaidTask: a Thief raids the hill of the leading team
//   rank 2  GuardTask     a Combat Ant is parked where its reflex covers the hill
//   rank 1  HarvestTask   the economy (tasks.hpp, unchanged)
//
// Every order a task gives is a click that a person could make: a plain move, an attack on the tile of an enemy ant, a special order of one ant (BotController filters the rest).

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

/// Whether an attack order can reach the ant now: it is not on a power-up (an ant that stands on one cannot be attacked, and one that is about to take it will not be there in a few
/// ticks), not on a hill tile (the order would be a plain move there), not on water, not in the middle of a hit, a flight, the enter clip or a burn
bool attackable(const BotView& view, const AntView& enemy);

/// Whether an own ant may be sent to fight: it takes orders, holds nothing, is healthy and is a worker (of the level's default type) or a Combat Ant, and does not stand on a power-up
bool can_fight(const BotView& view, const AntView& ant);

// ---- rank 5: fights ---------------------------------------------------------------------------------------------------------------------------------------

/// Fights, one per enemy ant:
///   a blow      an own ant lost hit points (Memory::hits) and an enemy ant that can be attacked stands within two tiles of it: that ant is the target. The nearest healthy ants that
///               hold nothing (Combat Ants first), as many as the level says, are taken from whatever they were doing and ordered to attack it, and ordered again each time they are
///               idle after their blow (one attack order is one blow), while the target stays within the leash of the place of the first blow.
///   a thief     (Medium and Hard) an enemy Thief ant that holds no loot and is within reach of the own raid tile: attacked the same way until it is gone or has raided.
/// A fight ends when the target dies, leaves sight or the leash, stands on a power-up, or no blow has been seen for the level's linger time; an attack that was acknowledged and then
/// shows "can't go" within a few ticks tells that the target is out of reach (it stands on something that no order reaches): that ant is left alone for 900 ticks. The ants go back
/// to the pool when the fight ends.
class FightTask final : public Task {
public:
    FightTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "fight"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    size_t fights() const noexcept { return fights_.size(); }
    uint32_t fights_started() const noexcept { return fights_started_; }
    uint32_t attacks_ordered() const noexcept { return attacks_ordered_; }
    uint32_t shunned_count() const noexcept { return shunned_count_; }
    bool shunned(uint32_t enemy, uint64_t tick) const noexcept;
    /// The defenders of the fight against `enemy` (empty when there is none)
    std::vector<uint32_t> defenders_of(uint32_t enemy) const;

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Defender {
        uint64_t decided{0};
        uint64_t sent{kPending};             // the tick the last attack order LEFT (kPending: none, or it has not left yet); 0: never ordered
        bool ordered{false};
    };
    struct Fight {
        uint32_t target{0};
        bool thief{false};
        sim::TileCoord anchor{};             // the place of the first blow (the own hill for a thief): the leash is measured from here
        uint64_t started{0};
        uint64_t last_alarm{0};
        std::map<uint32_t, Defender> defenders;
    };

    void start_fights(TaskContext& context);
    void run_fight(TaskContext& context, Fight& fight, bool& end, std::vector<std::pair<sim::TileCoord, std::vector<uint32_t>>>& attacks);

    Tactics& tactics_;
    std::map<uint32_t, Fight> fights_;       // by the target's id
    std::map<uint32_t, uint64_t> shunned_;   // enemy ant -> the tick until which it is left alone (an attack on it showed "can't go")
    uint32_t fights_started_{0};
    uint32_t attacks_ordered_{0};
    uint32_t shunned_count_{0};
};

}  // namespace ants::ai
