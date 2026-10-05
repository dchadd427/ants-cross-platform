#pragma once

// The fixture of the island tests (AI15, AI16): a match on a shipped map with a standard bot on every seat, driven tick by tick through a BotController that forwards to the engine, and a watch
// on what drowns: an ant that starts to drown on a tile that was a finished bridge a tick before has had its bridge collapse under it (the guard's failure), any other drowning is a flight,
// a blow or a swim into water that is not the bridges' doing.

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/island_tasks.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_ai/tactics.hpp"

namespace island_test {

using namespace ai_test;
using ants::ai::IslandTask;
using ants::ai::LevelPlan;
using ants::ai::StandardBot;

struct Match {
    ants::sim::SimulationEngine sim;
    std::unique_ptr<RecordingSink> sink;
    std::unique_ptr<BotController> ctl;
    uint8_t roster{0};
    std::vector<uint8_t> prev_bridge;                    // the finished bridge tiles of the tick before
    std::set<uint32_t> drowning;                         // the ants that have been seen to drown
    int collapsed[4] = {0, 0, 0, 0};                     // per team: ants that started to drown on a tile that was a finished bridge a tick before
    int other_drowned[4] = {0, 0, 0, 0};                 // per team: ants that drowned elsewhere
    uint32_t swimmers[4] = {0, 0, 0, 0};                 // the ids of the Swimmers that were given

    /// `swimmer_seats`: the seats that get a Swimmer on the first tile of their queue row (what a bomb flight or a flower would bring; the bot's own way to one is tested apart)
    /// `plan_of`: when set, the plan of the bot of the seat (the tests that need a bot without its builder, or with the guard off); else the level's
    /// `prepare`: when set, what the test does to the engine before the bots are seated (a pile of its own: the analysis of the map takes it in)
    void init(const std::string& map, uint32_t seed, uint8_t seats, Level level, uint8_t swimmer_seats, const std::function<void(LevelPlan&)>& tweak = {},
              const std::function<void(ants::sim::SimulationEngine&)>& prepare = {}) {
        roster = seats;
        start_match(sim, map, seed, seats);
        if (prepare) prepare(sim);
        const ants::ai::MapInfo probe(sim);
        for (uint8_t t = 0; t < 4; ++t) {
            if (((swimmer_seats >> t) & 1u) == 0 || !probe.hill(t).present || probe.hill(t).starts.empty()) continue;
            swimmers[t] = sim.spawn_unit(t, ants::sim::AntType::Swimmer, probe.hill(t).starts[0]);
        }
        sink = std::make_unique<RecordingSink>(sim, true);
        ctl = std::make_unique<BotController>(sim, seed);
        std::string why;
        for (uint8_t t = 0; t < 4; ++t) {
            if (((seats >> t) & 1u) == 0) continue;
            BotSpec spec;
            spec.seat = t;
            spec.kind = "standard";
            spec.level = level;
            LevelPlan plan = ants::ai::plan_for(level);
            if (tweak) tweak(plan);
            if (!ctl->add(spec, std::make_unique<StandardBot>(plan), *sink, why)) std::cout << "  (cannot seat " << int(t) << ": " << why << ")\n";
        }
        prev_bridge.assign(sim.grid().cells().size(), 0);
    }

    void tick() {
        sim.tick();
        sim.clear_news_events();
        sim.clear_audio_events();
        ctl->on_tick(sim);
        const ants::sim::Grid& grid = sim.grid();
        for (const auto& a : sim.get_world_state().ants) {
            if (a.state != ants::sim::UnitState::Drowning || !drowning.insert(a.id).second) continue;
            const size_t idx = static_cast<size_t>(a.tile_y) * grid.width() + static_cast<size_t>(a.tile_x);
            if (a.player_id < 4) (prev_bridge[idx] != 0 ? collapsed : other_drowned)[a.player_id] += 1;
        }
        for (size_t i = 0; i < prev_bridge.size(); ++i) prev_bridge[i] = grid.cells()[i].has_completed_bridge() ? 1 : 0;
    }

    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) tick();
    }

    StandardBot* bot(uint8_t seat) { return dynamic_cast<StandardBot*>(ctl->bot(seat)); }
    const IslandTask& island(uint8_t seat) { return bot(seat)->islands(); }
    int total_collapsed() const { return collapsed[0] + collapsed[1] + collapsed[2] + collapsed[3]; }
};

}  // namespace island_test
