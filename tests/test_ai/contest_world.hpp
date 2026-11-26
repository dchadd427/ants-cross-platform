#pragma once

// The worlds of the contest tests (AI59 to AI61, docs/BOT_CONTEST.md): one bot on seat 0 of the empty field (four hills, no start ants, a pocket of rocks that no walk reaches), driven by the real controller
// with a sink that applies at once, and the small questions that the tests ask of the engine.

#include <functional>
#include <memory>
#include <string>

#include "ai_test.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"

namespace contest_world {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

// A pocket of three by three tiles in a ring of rocks at (40 .. 42, 40 .. 42): no walk of the field reaches it, and a click on one of its tiles ends in "Can't go there." (the tile of the cancel)
inline void add_pocket(sim::SimulationEngine& sim) {
    for (int y = 39; y <= 43; ++y) {
        for (int x = 39; x <= 43; ++x) {
            if (x < 40 || x > 42 || y < 40 || y > 42) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
        }
    }
}

// One bot on seat 0 of the empty field (four hills, no start ants), driven by the real controller with a sink that applies at once
struct World {
    sim::SimulationEngine sim;
    std::unique_ptr<RecordingSink> sink;
    std::unique_ptr<BotController> ctl;
    explicit World(const LevelPlan& plan, const std::function<void(sim::SimulationEngine&)>& prepare = {}) {
        build_world(sim, 7, 0);
        add_pocket(sim);
        if (prepare) prepare(sim);
        sink = std::make_unique<RecordingSink>(sim, true);
        ctl = std::make_unique<BotController>(sim, 1);
        BotSpec spec;
        spec.seat = 0;
        spec.kind = "standard";
        spec.level = Level::Hard;
        std::string why;
        ctl->add(spec, std::make_unique<StandardBot>(plan), *sink, why);
    }
    void run(int ticks, const std::function<void()>& each = {}) {
        for (int i = 0; i < ticks; ++i) {
            sim.tick();
            ctl->on_tick(sim);
            sim.clear_news_events();
            sim.clear_audio_events();
            if (each) each();
        }
    }
    StandardBot& bot() { return *dynamic_cast<StandardBot*>(ctl->bot(0)); }
};

// One bot on seat 0 of a shipped map played by seats 0 and 1 (`roster` 0x03), with no ant on it but the ones a test puts there; the real controller, a sink that applies at once
struct MapWorld {
    sim::SimulationEngine sim;
    std::unique_ptr<RecordingSink> sink;
    std::unique_ptr<BotController> ctl;
    MapWorld(const LevelPlan& plan, const char* map, uint8_t roster = 0x03) {
        start_match(sim, map, 5, roster);
        std::vector<uint32_t> start_ants;
        for (const auto& a : sim.get_world_state().ants) start_ants.push_back(a.id);
        for (const uint32_t id : start_ants) sim.kill_unit(id);
        sink = std::make_unique<RecordingSink>(sim, true);
        ctl = std::make_unique<BotController>(sim, 1);
        BotSpec spec;
        spec.seat = 0;
        spec.kind = "standard";
        spec.level = Level::Hard;
        std::string why;
        ctl->add(spec, std::make_unique<StandardBot>(plan), *sink, why);
    }
    void run(int ticks, const std::function<void()>& each = {}) {
        for (int i = 0; i < ticks; ++i) {
            sim.tick();
            ctl->on_tick(sim);
            sim.clear_news_events();
            sim.clear_audio_events();
            if (each) each();
        }
    }
    StandardBot& bot() { return *dynamic_cast<StandardBot*>(ctl->bot(0)); }
    /// The drop tile of the flower that stands nearest to column `x`
    TileCoord drop_near(int x) {
        const MapInfo map(sim);
        const BotView view = BotView::build(sim, 0, &map);
        TileCoord best{-1, -1};
        for (const FlowerView& f : view.flowers()) {
            if (best.x < 0 || std::abs(f.drop.x - x) < std::abs(best.x - x)) best = f.drop;
        }
        return best;
    }
};

inline bool on_tile(const sim::SimulationEngine& sim, uint32_t ant, TileCoord t) {
    const sim::AntSnapshot* a = snapshot_of(sim, ant);
    return a != nullptr && a->tile_x == t.x && a->tile_y == t.y;
}

inline uint16_t hp_of(const sim::SimulationEngine& sim, uint32_t ant) {
    const sim::AntSnapshot* a = snapshot_of(sim, ant);
    return a == nullptr ? 0u : a->hp;
}

}  // namespace contest_world
