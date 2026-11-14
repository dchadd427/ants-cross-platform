#pragma once

// The TURTLE of the bench: a TEST-ONLY bot that is never in the registry (make_bot does not know it; the arena and the tests hand it to a match through a factory). It is a seat that shuts
// itself in and never goes anywhere, and it exists to measure what the standard bot does about the hazards of a base that is none of its business (docs/BOT_FIGHT.md, "Hazards on another
// hill's ground"):
//
//   it takes the Bomber and Fire power-ups of its own side,
//   it keeps a fire wall on each of the three tiles in front of its own thief hole and lights one again when it is put out or burned out,
//   it lays bombs on the open ground five to nine tiles round its own hill, up to thirty (the standard bot's home mines, with the way of its own carriers no reason to spare a tile),
//   it harvests the piles that its hill reaches first, with every ant that has nothing else to do,
//   it never raids, never fights, never mines a pile, never puts out or defuses anything of anyone's, and never hatches.
//
// It is the plan of the standard bot at Hard with everything switched off that sends an ant out of the turtle's own part of the map, so the walls, the bombs and the economy are the real tasks
// of the real bot. It is a client like every bot: the BotView, Orders and the controller's door, the profile of its level.

#include <memory>

#include "ants_ai/bot.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_ai/tactics.hpp"

namespace ants::ai::bench {

/// The plan of the turtle: the standard plan of Hard with the tactics that leave home off and the mines round the hill on
inline LevelPlan turtle_plan() {
    LevelPlan p = plan_for(Level::Hard);
    p.counters = false;                  // it clears nothing of anyone's
    p.raids = p.takes_thief = p.takes_combat = p.steals = false;
    p.guards = p.intercepts = p.harass = p.sabotage = p.rush = p.strikes = p.wipe_focus = p.hatches = false;
    p.assault = p.hunt = p.catchup = p.endgame = p.skirmish = p.opportunity = p.behind_war = p.raider_hunt = p.water_hunt = p.vicinity_hunt = false;
    p.defenders = 0;
    p.mine_per_pile = 0;                 // no mine at a pile
    p.mine_gate = 0;
    p.war_fires = 0;
    p.war_bombers = 1;                   // the Bomber of its side is wanted from the start
    p.mine_home = 30;
    p.mine_home_after = 600;
    p.mine_home_apart = 1;
    p.home_on_route = true;              // a bomb may lie on the way of its own carriers
    p.home_first = true;
    p.home_route_lane = 400;
    p.secure_kinds = static_cast<uint8_t>((1u << static_cast<unsigned>(sim::AntType::Fire)) | (1u << static_cast<unsigned>(sim::AntType::Bomber)));
    return p;
}

class TurtleBot final : public Bot {
public:
    TurtleBot() : inner_(std::make_unique<StandardBot>(turtle_plan())) {}
    const char* kind() const noexcept override { return "turtle"; }
    void start(const BotContext& context) override { inner_->start(context); }
    void think(const BotView& view, Orders& orders) override { inner_->think(view, orders); }
    void on_command(const sim::Command& command, Fate fate, uint64_t tick) override { inner_->on_command(command, fate, tick); }
    const StandardBot& inner() const noexcept { return *inner_; }

private:
    std::unique_ptr<StandardBot> inner_;
};

}  // namespace ants::ai::bench
