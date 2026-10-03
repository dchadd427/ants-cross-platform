#pragma once

// The standard bot (kind "standard", B4-1): the computer player that people meet. It is the economy of the worker bot (HarvestTask, unchanged) plus the tactics of its level:
//
//   every level   strikes back at an enemy that hits one of its ants (Easy with 1 ant, Medium 2, Hard 3: the nearest healthy ants that carry nothing), never attacks an ant that
//                 stands on a power-up, never clicks a power-up tile by accident, and keeps three fire walls in front of its thief hole when a thief threatens (Easy: an enemy Thief
//                 has been seen; Medium: one has been seen or an enemy that plays can reach a Thief power-up; Hard: as soon as an enemy plays, and the walls are renewed before they
//                 burn out): it takes a Fire power-up for that when one can be reached, the only power-up that Easy takes
//   Medium, Hard  also attack an enemy Thief on its way to the hill, take Combat and Thief power-ups, park a Combat Ant where its reflex covers the hill, and raid the hill of the
//                 leading team with a Thief that has points to take, is not shut by walls or bombs and can be reached
//   Hard          also fights in larger groups, Combat Ants first, harasses enemy carriers near contested piles when it pays, and does not raid a hill that an enemy Combat Ant guards
//
// It is a virtual client like every bot (project rule 8): it reads the world through the BotView, sends commands that a person could click, and has no knowledge that a person of its
// seat could not have. It never hatches and answers an invitation with one Deny, like the worker bot, which stays what it was: the frozen yardstick that this bot is measured against.

#include <cstdint>

#include "ants_ai/bot.hpp"
#include "ants_ai/standard_tasks.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class StandardBot final : public Bot {
public:
    explicit StandardBot(Level level) : StandardBot(plan_for(level)) {}
    /// A bot with a plan of its own (the tournaments' ablations and the tests): the registry's bot of a level is StandardBot(plan_for(level))
    explicit StandardBot(const LevelPlan& plan)
        : tactics_{plan, {}, {}, false, 0},
          harvest_(kHarvest, harvest_params(plan)),
          fight_(kFight, tactics_),
          aid_(kAid, tactics_),
          walls_(kWalls, tactics_),
          powerups_(kPowerUps, tactics_),
          bombs_(kBombs, tactics_),
          raids_(kRaids, tactics_),
          guard_(kGuard, tactics_),
          harass_(kHarass, tactics_) {}
    const char* kind() const noexcept override { return "standard"; }
    void start(const BotContext& context) override;
    void think(const BotView& view, Orders& orders) override;
    void on_command(const sim::Command& command, Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    const HarvestTask& harvest() const noexcept { return harvest_; }
    const FightTask& fight() const noexcept { return fight_; }
    const CarrierAidTask& aid() const noexcept { return aid_; }
    const WallTask& walls() const noexcept { return walls_; }
    const PowerUpTask& powerups() const noexcept { return powerups_; }
    const BombTask& bombs() const noexcept { return bombs_; }
    const RaidTask& raids() const noexcept { return raids_; }
    const GuardTask& guard() const noexcept { return guard_; }
    const HarassTask& harass() const noexcept { return harass_; }
    const Tactics& tactics() const noexcept { return tactics_; }
    const AntLedger& ledger() const noexcept { return ledger_; }
    uint32_t denials() const noexcept { return denials_; }

    /// The ids of the tasks (their rank is the order they take ants in: a higher rank takes from a lower one)
    static constexpr TaskId kHarvest = 1;
    static constexpr TaskId kFight = 2;
    static constexpr TaskId kAid = 3;
    static constexpr TaskId kWalls = 4;
    static constexpr TaskId kPowerUps = 5;
    static constexpr TaskId kRaids = 6;
    static constexpr TaskId kGuard = 7;
    static constexpr TaskId kHarass = 8;
    static constexpr TaskId kBombs = 9;

private:
    static HarvestTask::Params harvest_params(const LevelPlan& plan) {
        HarvestTask::Params p;
        if (plan.typed_harvest) p.extra_types = static_cast<uint8_t>((1u << static_cast<unsigned>(sim::AntType::Fire)) | (1u << static_cast<unsigned>(sim::AntType::Bomber)));
        if (plan.combat_harvests) p.extra_types = static_cast<uint8_t>(p.extra_types | (1u << static_cast<unsigned>(sim::AntType::Combat)));
        p.fire_aware = plan.fire_aware;
        p.contest_aware = plan.contest_aware;
        p.contest_low = plan.contest_low;
        p.contest_high = plan.contest_high;
        return p;
    }

    uint8_t seat_{0};
    Profile profile_{};
    const MapInfo* map_{nullptr};
    AntLedger ledger_;
    Tactics tactics_;
    HarvestTask harvest_;
    FightTask fight_;
    CarrierAidTask aid_;
    WallTask walls_;
    PowerUpTask powerups_;
    BombTask bombs_;
    RaidTask raids_;
    GuardTask guard_;
    HarassTask harass_;
    uint32_t denials_{0};
    uint64_t deny_after_{0};
};

}  // namespace ants::ai
