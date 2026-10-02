#pragma once

// The worker bot (kind "worker", B3): harvest only. It is the economy of the later bots and the regression YARDSTICK that they are measured against: it is deliberately
// frozen once the standard bot (B4) exists, so a number that was measured against it stays comparable.
//
//   it harvests      every idle, empty-handed worker is sent to a pile (HarvestTask: one group move per pile starts the engine's self-running harvest loop)
//   it never hatches It is a fixed yardstick, and hatching does not pay on the shipped maps: a worker costs 200 points and takes 171 ticks (160 of incubation and the emerge clip)
//                    before it can take an order; one hatch helps a little on TINY seat 0 (+190 to +220) and loses on the other maps (docs/BOTS.md, "Measurements")
//   it never fights, never uses a power-up, never raids, never builds: those are the standard bot's (B4)
//   it declines     an invitation to team up is answered with a Deny: nobody else answers it (the simulation lets an invitation wait for ever) and an alliance of all live teams
//                   ends the match within a fifth of a second, so a bot that cannot judge it says no. A bot's Deny is what a person's click on "No" sends, and the controller
//                   lets it through (an answer needs the invitation it answers).
//   levels           Easy sends the ants to the nearest pile, at most 4 to a pile; Medium and Hard rank the piles by points per trip (value-aware) and take at most
//                    min(8, trip / 90 + 2) ants to a pile (the hill's cap and the profile's). The economy is nearly insensitive to reaction time and command rate (a few
//                    hundredths of a command per second are enough), so the levels differ in tactics, which the worker does not have; delay and rate are fairness limits only.
//
// Until the standard bot exists the kind "standard" is an alias of this bot (make_bot hands out a WorkerBot for both; kind() says "worker").

#include <cstdint>

#include "ants_ai/bot.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class WorkerBot final : public Bot {
public:
    const char* kind() const noexcept override { return "worker"; }
    void start(const BotContext& context) override;
    void think(const BotView& view, Orders& orders) override;
    void on_command(const sim::Command& command, Fate fate, uint64_t tick) override;

    /// For the tests and the reports
    const HarvestTask& harvest() const noexcept { return harvest_; }
    /// Invitations answered with a Deny (proposed; the controller decides what is released)
    uint32_t denials() const noexcept { return denials_; }

private:
    static constexpr TaskId kHarvest = 1;

    uint8_t seat_{0};
    Profile profile_{};
    const MapInfo* map_{nullptr};
    AntLedger ledger_;
    HarvestTask harvest_{kHarvest};
    uint32_t denials_{0};
    uint64_t deny_after_{0};                 // no second Deny before this tick: the first is on its way (or was just applied)
};

}  // namespace ants::ai
