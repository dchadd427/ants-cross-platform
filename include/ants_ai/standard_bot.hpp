#pragma once

// The standard bot (kind "standard", B4-1): the computer player that people meet. It is the economy of the worker bot (HarvestTask) plus the tactics of its level and its style
// (docs/BOTS.md, "The standard bot"; the numbers of every level are in tactics.cpp, plan_for):
//
//   every level   strikes back at an enemy that hits one of its ants (Easy with 1 ant, Medium 2, Hard 3: the nearest healthy ants that carry nothing), sends a hit carrier home, never
//                 attacks an ant that stands on a power-up, never clicks a power-up tile by accident, never sends its last ants into a fight, puts out the enemy's fire walls and
//                 defuses (or sets off) its bombs where they are in the way of its economy, and keeps three fire walls in front of its thief hole when a thief threatens (Easy: an
//                 enemy Thief has been seen; Medium: one has been seen or an enemy that plays can reach a Thief power-up; Hard: as soon as an enemy plays, and the walls are renewed
//                 before they burn out): it takes a Fire power-up for that when one can be reached, the only power-up that Easy takes
//   Medium, Hard  take the Fire, Bomber and Thief power-ups of their own side of the map in the opening (the first moves go to power-ups, not to food), a Combat Ant once an enemy
//                 plays (it is a worker that fights: it harvests and its reflex punches what comes near; there is no guard post), and raid the hill of the leading team with a Thief
//                 that has points to take, is not shut by walls or bombs and can be reached (no level keeps away from a hill that an enemy Combat Ant stands near: measured, it is a worker
//                 that fights, not a guard); a Thief that harvests is taken from its loop for a raid. Medium sends one ant, Hard two, to the contested middle of the map at the start
//   Hard          also steals a second Thief (and a second Combat Ant once it is attacked) and guides every carrier at the hill's gate by hand (GateTask)
//   styles        a bot draws one per match from its own seat's generator (or the spec pins it) and plays it on top of its level: Aggressive adds the harassment squad (at Hard also the
//                 sabotage of the best opponent's gate with a stolen Fire Ant and the strike when behind), Economic the efficient order, Raider the Thief first, Defensive the walls early
//
// What the tournaments measured as a loss is OFF in every shipped plan, and stays in the code behind a flag for the next round: the Combat Ant on a guard post, the interception of
// thieves, hatching, the strict contest order, the strike and the wipe-out focus (the strike is part of the Hard Aggressive style), the ambush at a thief hole, the sabotage with a
// single Fire Ant, the harassment squad (but in the Aggressive style) (docs/BOTS.md, "Aggression").
//
// It is a virtual client like every bot (project rule 8): it reads the world through the BotView, sends commands that a person could click, and has no knowledge that a person of its
// seat could not have. It answers an invitation to team up by the accept rule (accepts_invitation), never invites and never breaks an alliance. The worker bot stays what it was: the
// frozen yardstick that this bot is measured against.

#include <cstdint>
#include <functional>

#include "ants_ai/bot.hpp"
#include "ants_ai/standard_tasks.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class StandardBot final : public Bot {
public:
    /// The bot of a level and a style (Style::Random: it draws one from its own seat's generator at start(), among the styles that the level allows): the registry's bot
    explicit StandardBot(Level level, Style style = Style::Random) : StandardBot(plan_for(level), level, style, true) {}
    /// A bot with a plan of its own (the tournaments' ablations and the tests): no style, the plan is used as it is
    explicit StandardBot(const LevelPlan& plan) : StandardBot(plan, plan.level, Style::Random, false) {}
    /// The tournaments' ablations on top of a style: `tune` is applied to the plan after the style and the variations have been made at start() (the arena only)
    StandardBot(Level level, Style style, std::function<void(LevelPlan&)> tune) : StandardBot(plan_for(level), level, style, true) { tune_ = std::move(tune); }

private:
    StandardBot(const LevelPlan& plan, Level level, Style style, bool styled)
        : level_(level),
          requested_style_(style),
          styled_(styled),
          tactics_(tactics_of(plan)),
          harvest_(kHarvest, harvest_params(plan)),
          fight_(kFight, tactics_),
          aid_(kAid, tactics_),
          walls_(kWalls, tactics_),
          powerups_(kPowerUps, tactics_),
          bombs_(kBombs, tactics_),
          raids_(kRaids, tactics_),
          guard_(kGuard, tactics_),
          strike_(kStrike, tactics_),
          hatch_(kHatch, tactics_),
          gate_(kGate, gate_params(plan)),
          harass_(kHarass, tactics_),
          sabotage_(kSabotage, tactics_) {}

public:
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
    const StrikeTask& strike() const noexcept { return strike_; }
    const HatchTask& hatch() const noexcept { return hatch_; }
    const GateTask& gate() const noexcept { return gate_; }
    const HarassTask& harass() const noexcept { return harass_; }
    const SabotageTask& sabotage() const noexcept { return sabotage_; }
    const Tactics& tactics() const noexcept { return tactics_; }
    /// The style that the bot plays (known once start() has run; Random for a bot with a plan of its own)
    Style style() const noexcept { return style_; }
    const AntLedger& ledger() const noexcept { return ledger_; }
    uint32_t denials() const noexcept { return denials_; }
    uint32_t accepts() const noexcept { return accepts_; }
    /// The accept rule: an invitation to team up is accepted unless it would unite all live teams (the match would end at once: the alliance of the last two live teams wins), or the bot
    /// already has an ally, or the inviter has one (accepting would break an alliance). A team counts as live when it plays, has not dropped out and has an ant in sight (the bot's own
    /// ants count for itself); the eggs of a team that has no ant cannot be seen: such a team counts as gone, which only makes the bot more careful. The bot never invites, never withdraws
    /// and never breaks an alliance.
    static bool accepts_invitation(const BotView& view, uint8_t from);

    /// The ids of the tasks (their rank is the order they take ants in: a higher rank takes from a lower one)
    static constexpr TaskId kHarvest = 1;
    static constexpr TaskId kFight = 2;
    static constexpr TaskId kAid = 3;
    static constexpr TaskId kWalls = 4;
    static constexpr TaskId kPowerUps = 5;
    static constexpr TaskId kRaids = 6;
    static constexpr TaskId kGuard = 7;
    static constexpr TaskId kStrike = 8;
    static constexpr TaskId kBombs = 9;
    static constexpr TaskId kHatch = 10;
    static constexpr TaskId kGate = 11;
    static constexpr TaskId kHarass = 12;
    static constexpr TaskId kSabotage = 13;

private:
    static Tactics tactics_of(const LevelPlan& plan) {
        Tactics t;
        t.plan = plan;
        return t;
    }
    static GateTask::Params gate_params(const LevelPlan& plan) {
        GateTask::Params p;
        p.latency_ticks = plan.gate_latency;
        p.max_staged = plan.gate_max_staged;
        p.predictive = plan.gate_predictive;
        return p;
    }
    static HarvestTask::Params harvest_params(const LevelPlan& plan) {
        HarvestTask::Params p;
        if (plan.typed_harvest) {
            p.extra_types = static_cast<uint8_t>((1u << static_cast<unsigned>(sim::AntType::Fire)) | (1u << static_cast<unsigned>(sim::AntType::Bomber)) | (1u << static_cast<unsigned>(sim::AntType::Thief)));
        }
        if (plan.combat_harvests) p.extra_types = static_cast<uint8_t>(p.extra_types | (1u << static_cast<unsigned>(sim::AntType::Combat)));
        p.fire_aware = plan.fire_aware;
        if (plan.gate) {
            p.rescue = false;                                    // the gate task owns every carrier
            p.gate_gap_ticks = plan.gate_gap_ticks;
        }
        p.contest_aware = plan.contest_aware;
        p.contest_low = plan.contest_low;
        p.contest_high = plan.contest_high;
        p.rank_by_remaining = plan.rank_by_remaining;
        p.contest_one_first = plan.contest_one_first;
        p.contest_reactive = plan.contest_reactive;
        p.contest_opening_ants = plan.contest_opening_ants;
        p.contest_opening_min_ants = plan.contest_opening_min_ants;
        return p;
    }

    Level level_{Level::Medium};
    Style requested_style_{Style::Random};
    Style style_{Style::Random};
    bool styled_{false};
    std::function<void(LevelPlan&)> tune_;
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
    StrikeTask strike_;
    HatchTask hatch_;
    GateTask gate_;
    HarassTask harass_;
    SabotageTask sabotage_;
    uint32_t denials_{0};
    uint32_t accepts_{0};
    uint64_t deny_after_{0};
};

}  // namespace ants::ai
