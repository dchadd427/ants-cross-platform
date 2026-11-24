// AI58: the idle batch of the computer players (docs/BOT_IDLE.md): what their ants do when nothing is planned for them, in hand-made worlds with the real engine and in whole matches of the
// map that the rules were found on.
//
//   AI58.1  two ants face to face on a bridge: the engine leaves them standing for ever, the bot's head-on rule steps one back to land and the other crosses; with the rule off they stand
//   AI58.2  one free ant beside an enemy that stands still strikes it (and not when the enemy is the stronger, when an enemy Combat Ant stands near, with the rule off)
#include <algorithm>
#include <array>
#include <functional>
#include <cstdint>
#include <string>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

constexpr int kBridgeX = 30;                    // the bridge across the river: x 30, y 25 .. 29 (finished tiles), the river is the rows 25 to 29 across the whole field
constexpr int kRiverTop = 25;
constexpr int kRiverBottom = 29;

// The field of AI58.1: a river five tiles wide across the whole field and a bridge of five finished tiles over it (life for the whole test)
void river_with_bridge(sim::SimulationEngine& sim, uint32_t seed = 31) {
    empty_field(sim, seed);
    for (int y = kRiverTop; y <= kRiverBottom; ++y) {
        for (int x = 0; x < 60; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);
        sim.grid_mut().set_bridge_at(TileCoord{kBridgeX, y}, 4, 1000000);
    }
}

LevelPlan plan_headon(bool on) {
    LevelPlan p = peace_plan(Level::Hard);
    p.bridge_headon = on;
    return p;
}

// The plan of the hand-made worlds of the idle batch with exactly one rule on or off (the others as the level plays them: the world has nothing for them to do)
LevelPlan plan_of(Level level, const std::function<void(LevelPlan&)>& tweak) {
    LevelPlan p = peace_plan(level);
    p.idle_jobs = p.thief_answer = p.contact_strike = p.bridge_headon = false;
    tweak(p);
    return p;
}

uint16_t hp_of(const sim::SimulationEngine& sim, uint32_t id) {
    const sim::AntSnapshot* a = snapshot_of(sim, id);
    return a == nullptr ? 0u : a->hp;
}

}  // namespace

void run_idle_tests() {
    TEST_CASE("AI58.1 Two Ants Face To Face On A Bridge: One Walks North And One South Over A Five-Tile Bridge And Stand For Good; With The Head-On Rule On The One Nearer Land Steps Back And The Other Crosses (A Hard Bot)") {
        struct Out {
            bool north_crossed{false};
            bool south_crossed{false};
            uint32_t backoffs{0};
            uint32_t jams{0};
            uint32_t stood{0};                                                                   // ticks at which both stood on the bridge without moving
        };
        const auto play = [&](bool rule) {
            sim::SimulationEngine sim;
            river_with_bridge(sim);
            const uint32_t north = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{kBridgeX, kRiverTop - 1});     // on the north shore, bound south
            const uint32_t south = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{kBridgeX, kRiverBottom + 1});  // on the south shore, bound north
            sim.apply_command(command_of(CommandType::GroupMove, 0, {north}, kBridgeX, kRiverBottom + 4));
            sim.apply_command(command_of(CommandType::GroupMove, 0, {south}, kBridgeX, kRiverTop - 4));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_headon(rule)), 4, 4);
            // the owner of each ant (a task of its seat) clicks it again when it stands idle: the engine's meeting leaves both ants idle, which is what the watched match's economy and heal walk kept clicking
            const std::array<std::pair<uint32_t, TileCoord>, 2> goals = {{{north, TileCoord{kBridgeX, kRiverBottom + 4}}, {south, TileCoord{kBridgeX, kRiverTop - 4}}}};
            rig.after_sim_tick = [&]() {
                if (sim.current_tick() % 40 != 0) return;
                for (const auto& g : goals) {
                    const sim::AntSnapshot* a = snapshot_of(sim, g.first);
                    if (a != nullptr && a->state == sim::UnitState::Idle && !(a->tile_x == g.second.x && a->tile_y == g.second.y) && !sim.has_pending_path(g.first) && rig.as<StandardBot>().ledger().owner(g.first) == kNoTask) {
                        sim.apply_command(command_of(CommandType::GroupMove, 0, {g.first}, g.second.x, g.second.y));
                    }
                }
            };
            Out out;
            for (int t = 0; t < 1500; ++t) {
                rig.tick();
                const sim::AntSnapshot* n = snapshot_of(sim, north);
                const sim::AntSnapshot* s = snapshot_of(sim, south);
                if (n != nullptr && n->tile_y > kRiverBottom) out.north_crossed = true;
                if (s != nullptr && s->tile_y < kRiverTop) out.south_crossed = true;
                if (t >= 700 && n != nullptr && s != nullptr && n->tile_y >= kRiverTop && n->tile_y <= kRiverBottom && s->tile_y >= kRiverTop && s->tile_y <= kRiverBottom) ++out.stood;
            }
            out.backoffs = rig.as<StandardBot>().head_on().backoffs();
            out.jams = rig.as<StandardBot>().head_on().jams();
            return out;
        };
        const Out off = play(false);
        ASSERT_FALSE(off.north_crossed || off.south_crossed);                                    // the engine's rule: they meet in the middle and stand there
        ASSERT_TRUE(off.stood > 700);
        ASSERT_EQ(off.backoffs, 0u);
        const Out on = play(true);
        ASSERT_TRUE(on.backoffs >= 1u && on.jams >= 1u);
        ASSERT_TRUE(on.north_crossed || on.south_crossed);                                       // one of them crossed
        ASSERT_TRUE(on.stood < 200);                                                             // and the bridge was clear again soon
    } TEST_END();
    TEST_CASE("AI58.2 One Free Ant Beside An Enemy That Stands Still Strikes It: A Worker Next To An Idle Enemy Worker Hits It; Not When The Enemy Has More Hit Points, When An Enemy Combat Ant Stands Near, Or With The Rule Off (A Hard Bot)") {
        struct Out {
            uint16_t enemy_hp{0};
            uint32_t started{0};
        };
        const auto play = [&](bool rule, uint16_t own_hp, uint16_t enemy_hp, bool combat_near) {
            sim::SimulationEngine sim;
            empty_field(sim, 33);
            const uint32_t mine = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
            const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{31, 30});
            if (combat_near) sim.spawn_unit(1, sim::AntType::Combat, TileCoord{34, 30});
            sim.get_unit(mine).hp = own_hp;
            sim.get_unit(foe).hp = enemy_hp;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_of(Level::Hard, [&](LevelPlan& p) { p.contact_strike = rule; })), 4, 4);
            rig.run(500);
            Out out;
            out.enemy_hp = hp_of(sim, foe);
            out.started = rig.as<StandardBot>().contact().started();
            return out;
        };
        const Out on = play(true, 10, 10, false);
        ASSERT_TRUE(on.started >= 1u);
        ASSERT_TRUE(on.enemy_hp < 10u);                                                          // the blows landed
        const Out off = play(false, 10, 10, false);
        ASSERT_EQ(off.started, 0u);
        ASSERT_EQ(off.enemy_hp, 10u);
        const Out weaker = play(true, 7, 10, false);                                             // the own ant is the weaker: no blow
        ASSERT_EQ(weaker.started, 0u);
        ASSERT_EQ(weaker.enemy_hp, 10u);
        const Out combat = play(true, 10, 10, true);                                             // an enemy Combat Ant stands near: a person looks first
        ASSERT_EQ(combat.started, 0u);
    } TEST_END();

}
