// AI58: the idle batch of the computer players (docs/BOT_IDLE.md): what their ants do when nothing is planned for them, in hand-made worlds with the real engine and in whole matches of the
// map that the rules were found on.
//
//   AI58.1  two ants face to face on a bridge: the engine leaves them standing for ever, the bot's head-on rule steps one back to land and the other crosses; with the rule off they stand
//   AI58.2  one free ant beside an enemy that stands still strikes it (and not when the enemy is the stronger, when an enemy Combat Ant stands near, with the rule off)
//   AI58.4  with the food gone the idle ants hold the three tiles in front of the thief hole, and a raid of a Thief of another team ends in the can't-go clip; with the rule off it takes its 50 points
//   AI58.5  a hurt ant that stands idle walks home and is whole again; with the rule off it stands
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

// The score of a team after a Thief of team 1 has been told to raid the hill of team 0 (its mound's entrance at (5, 5)), and the Thief
void send_raid(sim::SimulationEngine& sim, uint32_t thief) { sim.apply_command(command_of(CommandType::GroupSpecial, 1, {thief}, 5, 5)); }

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

    TEST_CASE("AI58.4 With The Food Gone The Idle Ants Hold The Three Tiles In Front Of The Thief Hole: A Raid Of Another Team's Thief Then Ends In The Can't-Go Clip And The Hill Keeps Its Points; With The Rule Off It Takes 50 (A Hard Bot, Six Idle Workers)") {
        struct Out {
            int32_t score{0};
            size_t holders{0};
            uint32_t holds{0};
        };
        const auto play = [&](bool rule, bool gone = true, bool fire_ant = false) {
            sim::SimulationEngine sim;
            empty_field(sim, 37);
            sim.set_player_score(0, 300);
            for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i % 3, 8 + i / 3});
            if (fire_ant) sim.spawn_unit(0, sim::AntType::Fire, TileCoord{12, 12});
            LevelPlan plan = plan_for(Level::Hard);                                              // (the food-gone batch on: the empty field is a match whose food is gone)
            without_war_batch(plan);
            if (!gone) without_gone_batch(plan);
            plan.thief_answer = false;
            plan.idle_jobs = rule;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(1500);                                                                       // the batch begins after 400 ticks, the ants have stood idle for 200 by then
            Out out;
            for (int dy = 1; dy <= 3; ++dy) {
                for (const sim::AntSnapshot& a : sim.get_world_state().ants) out.holders += (a.player_id == 0 && a.tile_x == 8 && a.tile_y == 4 + dy) ? 1u : 0u;
            }
            const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{19, 6});
            send_raid(sim, thief);
            rig.run(500);
            out.score = sim.get_player_score(0);
            out.holds = rig.as<StandardBot>().idle_jobs().holds();
            return out;
        };
        const Out off = play(false);
        ASSERT_EQ(off.holders, 0u);
        ASSERT_EQ(off.score, 250);
        const Out on = play(true);
        ASSERT_EQ(on.holders, 3u);                                                               // one ant on each tile
        ASSERT_EQ(on.score, 300);
        ASSERT_TRUE(on.holds >= 3u);
        const Out early = play(true, false);                                                     // (no food gone, no Thief seen before the one at 1,500: nobody holds a post before it)
        ASSERT_EQ(early.holders, 0u);
        const Out keeper = play(true, true, true);                                               // (a Fire Ant that can walk home keeps the walls, which a holder would stand in the way of)
        ASSERT_EQ(keeper.holders, 0u);
    } TEST_END();

    TEST_CASE("AI58.5 A Hurt Ant That Stands Idle Walks Home And Is Whole Again: A Worker With 4 Hit Points Far From The Hill Is At The Hill's Door And Has Its 10 Hit Points Back Within 1,500 Ticks; With The Rule Off It Stands (A Medium Bot)") {
        const auto play = [&](bool rule, uint16_t hp) {
            sim::SimulationEngine sim;
            empty_field(sim, 39);
            const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
            sim.get_unit(ant).hp = hp;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_of(Level::Medium, [&](LevelPlan& p) { p.idle_jobs = rule; })), 4, 4);
            rig.run(1500);
            const sim::AntSnapshot* a = snapshot_of(sim, ant);
            return std::make_pair(hp_of(sim, ant), a != nullptr ? TileCoord{a->tile_x, a->tile_y} : TileCoord{-1, -1});
        };
        ASSERT_EQ(play(false, 4).first, 4u);
        ASSERT_EQ(play(true, 4).first, 10u);
        ASSERT_TRUE(play(true, 10).second == (TileCoord{30, 30}));                               // a whole ant has nothing to heal: it stays where it is
    } TEST_END();
}
