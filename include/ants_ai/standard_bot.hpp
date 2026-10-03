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
    explicit StandardBot(Level level) : tactics_{plan_for(level), {}, {}}, fight_(kFight, tactics_) {}
    const char* kind() const noexcept override { return "standard"; }
    void start(const BotContext& context) override;
    void think(const BotView& view, Orders& orders) override;
    void on_command(const sim::Command& command, Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    const HarvestTask& harvest() const noexcept { return harvest_; }
    const FightTask& fight() const noexcept { return fight_; }
    const Tactics& tactics() const noexcept { return tactics_; }
    const AntLedger& ledger() const noexcept { return ledger_; }
    uint32_t denials() const noexcept { return denials_; }

    /// The ids of the tasks (their rank is the order they take ants in: a higher rank takes from a lower one)
    static constexpr TaskId kHarvest = 1;
    static constexpr TaskId kFight = 2;

private:
    uint8_t seat_{0};
    Profile profile_{};
    const MapInfo* map_{nullptr};
    AntLedger ledger_;
    Tactics tactics_;
    HarvestTask harvest_{kHarvest};
    FightTask fight_;
    uint32_t denials_{0};
    uint64_t deny_after_{0};
};

}  // namespace ants::ai
