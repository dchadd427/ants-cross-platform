#pragma once

// The one-bot lab of the flower link and bridge tests (AI44, AI54): a match on a shipped map with one Hard bot on a seat, the expedition, the miner, the mines and the rows on foot off (a test that
// needs one says so), driven tick by tick, with the ants that a test puts on the map the only ones there.

#include <functional>
#include <memory>

#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "island_fixture.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_sim/game_mode.hpp"

namespace flower_lab {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

struct Lab {
    sim::SimulationEngine sim;
    std::unique_ptr<RecordingSink> sink;
    std::unique_ptr<BotController> ctl;
    uint8_t seat_{0};

    /// `prepare`: what a test does to the engine before the bot is seated (a pile of its own, a bridge that stands: the analysis of the map takes it in); `mode`: the rules of the match
    explicit Lab(const std::function<void(LevelPlan&)>& tweak, const char* map = "ISLANDS", uint8_t roster = 0x01, uint8_t seat = 0, const std::function<void(sim::SimulationEngine&)>& prepare = {},
                 sim::GameMode mode = sim::GameMode::HighestScore) : seat_(seat) {
        sim.set_game_mode(mode);
        start_match(sim, map, 1, roster);
        if (prepare) prepare(sim);
        sink = std::make_unique<RecordingSink>(sim, true);
        ctl = std::make_unique<BotController>(sim, 1);
        LevelPlan plan = plan_for(Level::Hard);
        plan.island_expedition = false;
        plan.island_fly_miner = false;
        plan.island_mines = false;
        plan.island_builders = 0;
        plan.island_swarm_rows = 0;                                                             // (a test of the rows on foot says so)
        if (tweak) tweak(plan);
        BotSpec spec;
        spec.seat = seat;
        spec.kind = "standard";
        spec.level = Level::Hard;
        std::string why;
        ctl->add(spec, std::make_unique<StandardBot>(plan), *sink, why);
    }
    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            sim.tick();
            ctl->on_tick(sim);
            sim.clear_news_events();
            sim.clear_audio_events();
        }
    }
    StandardBot* bot() { return dynamic_cast<StandardBot*>(ctl->bot(seat_)); }
    const sim::AntSnapshot* ant(uint32_t id) const {
        for (const auto& a : sim.get_world_state().ants) {
            if (a.id == id) return &a;
        }
        return nullptr;
    }
    bool bridge_at(TileCoord t) const { return sim.grid().get_cell(t).has_completed_bridge(); }
};

}  // namespace flower_lab
