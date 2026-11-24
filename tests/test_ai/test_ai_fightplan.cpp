// AI60: a fight with a plan (docs/BOT_CONTEST.md, "A fight with a plan"; plan.fight_plan, off in every shipped plan): in hand-made worlds with the real engine and the bot controller, and in whole matches.
//
//   AI60.1  an edge: two own workers and an enemy worker that stands idle three tiles from them: the own side has four times its strength and strikes (the enemy ends at 1 hit point and walks home); with the rule off nobody strikes
//   AI60.2  no edge: one own worker and one enemy worker (equal strength, 100 percent of 150 asked): nobody is sent, the engagement is counted and skipped
//   AI60.3  the weakest first: an enemy worker with 3 hit points and one with 10 stand beside each other, three own ants near: the blows land on the weak one first; with fp_focus off on the nearest
//   AI60.4  the hurt are pulled back: an own worker with 3 hit points and an enemy Combat Ant that strikes it: it walks into its hill and is whole again; with fp_pull off it dies
//   AI60.5  a bridge is held, not chased: an enemy ant that stands on the bridge tile is not struck by ants that would have to walk onto the one tile way; with fp_bridge_hold off they walk onto it
//   AI60.6  a carrier is guarded: an own worker with food, an enemy ant three tiles from it and two own ants six tiles from the enemy: with fp_guard the two strike the enemy; without it nobody does
//   AI60.8  fp_spare_only: three free ants and one that a task holds against one enemy: the free ones strike and the held one is left where it stands; without the switch the held one strikes too
//   AI60.7  the plans that ship have the rule off, and whole matches with it on replay from their commands alone (four Hard bots on TREASURE and on ISLANDS)
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/fight_plan.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "contest_world.hpp"

namespace {

using namespace contest_world;

LevelPlan plan_fight(const std::function<void(LevelPlan&)>& tweak = {}) {
    LevelPlan p = peace_plan(Level::Hard);
    p.fight_plan = true;
    if (tweak) tweak(p);
    return p;
}

}  // namespace

void run_fightplan_tests() {
    TEST_CASE("AI60.1 An Edge: Two Own Workers And An Enemy Worker That Stands Idle Three Tiles From Them (Four Times Their Strength On One Side): The Own Side Strikes And The Enemy Ends On One Hit Point And Walks Home; With The Rule Off Nobody Strikes") {
        for (const bool rule : {true, false}) {
            World w(plan_fight([rule](LevelPlan& p) { p.fight_plan = rule; }));
            const uint32_t a = w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
            const uint32_t b = w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 32});
            const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{33, 31});
            uint16_t hp = 10;                                                                    // (the lowest the enemy was brought to: a worker at 1 hit point walks home, and the hill heals it)
            w.run(900, [&]() { hp = std::min(hp, hp_of(w.sim, enemy)); });
            ASSERT_TRUE(alive(w.sim, a) && alive(w.sim, b));
            if (rule) {
                ASSERT_TRUE(hp <= 2u);                                                           // (a worker's blows take it to one hit point, where the engine sends it home)
                ASSERT_TRUE(w.bot().fight_plan().fights() >= 1u);
                ASSERT_TRUE(w.bot().fight_plan().attacks() >= 1u);
            } else {
                ASSERT_EQ(hp, 10u);
                ASSERT_EQ(w.bot().fight_plan().attacks(), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI60.2 No Edge: One Own Worker And One Enemy Worker Three Tiles Apart Have The Same Strength (100 Percent Of The 150 Asked): Nobody Is Sent, The Place Is Counted And Skipped") {
        World w(plan_fight());
        w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
        const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{33, 30});
        w.run(600);
        ASSERT_EQ(hp_of(w.sim, enemy), 10u);
        ASSERT_EQ(w.bot().fight_plan().attacks(), 0u);
        ASSERT_TRUE(w.bot().fight_plan().engagements() >= 1u);
        ASSERT_TRUE(w.bot().fight_plan().skipped() >= 1u);
        ASSERT_EQ(w.bot().fight_plan().fights(), 0u);
    } TEST_END();

    TEST_CASE("AI60.3 The Weakest First: Two Enemy Workers Stand Beside Each Other, One With 10 Hit Points Nearer To The Own Ants And One With 3; Three Own Ants Strike The One With 3 First (Without fp_focus The Nearest, The Strong One)") {
        for (const bool focus : {true, false}) {
            World w(plan_fight([focus](LevelPlan& p) { p.fp_focus = focus; }));
            for (int i = 0; i < 3; ++i) w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{28, 29 + i * 2});
            const uint32_t strong = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{31, 31});
            const uint32_t weak = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{32, 33});
            w.sim.get_unit(weak).hp = 3;
            w.run(160);                                                                          // (the first blows land)
            const uint16_t hp_strong = hp_of(w.sim, strong);
            const uint16_t hp_weak = hp_of(w.sim, weak);
            if (focus) {
                ASSERT_TRUE(hp_weak < 3u);
                ASSERT_EQ(hp_strong, 10u);
            } else {
                ASSERT_TRUE(hp_strong < 10u);
            }
        }
    } TEST_END();

    TEST_CASE("AI60.4 The Hurt Are Pulled Back: An Own Worker With 3 Hit Points, An Enemy Combat Ant That Strikes It (No Edge): It Walks Into Its Hill And Is Whole Again, Never Struck; With fp_pull Off It Stands And Is Struck") {
        for (const bool pull : {true, false}) {
            World w(plan_fight([pull](LevelPlan& p) { p.fp_pull = pull; }));
            const uint32_t ant = w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14, 8});
            w.sim.get_unit(ant).hp = 3;
            const uint32_t e1 = w.sim.spawn_unit(1, sim::AntType::Combat, TileCoord{17, 8});
            uint16_t lowest = 10;                                                                // (the lowest it was brought to: a punch takes 2 hit points, and at 1 the engine sends an ant home by itself, where the hill heals it)
            bool healed = false;                                                                 // (the hill's door heals: 10 hit points are seen again)
            w.run(900, [&]() {
                keep_attacking(w.sim, 1, e1, ant);
                const uint16_t hp = hp_of(w.sim, ant);
                if (!healed) lowest = std::min(lowest, hp);                                      // (until it is whole again: it fights on afterwards)
                healed = healed || hp == 10u;
            });
            if (pull) {
                ASSERT_EQ(lowest, 3u);                                                           // it walked away at once: no punch ever landed
                ASSERT_TRUE(healed);
                ASSERT_TRUE(w.bot().fight_plan().pulls() >= 1u);
            } else {
                ASSERT_TRUE(lowest < 3u);                                                        // it stood and was struck (and then walked home by itself)
            }
        }
    } TEST_END();

    TEST_CASE("AI60.5 A Bridge Is Held, Not Chased: An Enemy Worker Stands On The First Tile Of A One-Tile Bridge, Three Own Ants On The Bank; Without fp_bridge_hold They Walk Onto The Bridge And Strike It, With It They Stand On The Shore") {
        for (const bool hold : {true, false}) {
            World w(plan_fight([hold](LevelPlan& p) { p.fp_bridge_hold = hold; }), [](sim::SimulationEngine& sim) {
                for (int x = 0; x < 60; ++x) sim.set_terrain(x, 27, sim::TERRAIN_WATER);
                sim.grid_mut().set_bridge_at(TileCoord{30, 27}, 4, 1000000);
            });
            for (int i = 0; i < 3; ++i) w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{29 + i, 24});
            const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 27});
            w.run(400);
            const uint16_t hp = hp_of(w.sim, enemy);
            if (hold) ASSERT_EQ(hp, 10u);
            else ASSERT_TRUE(hp < 10u);
        }
    } TEST_END();

    TEST_CASE("AI60.6 A Carrier Is Guarded: An Own Worker With Food, An Enemy Worker Three Tiles From It, Two Own Ants Six Tiles From The Enemy: With fp_guard The Two Strike The Enemy; Without It Nobody Does") {
        for (const bool guard : {true, false}) {
            World w(plan_fight([guard](LevelPlan& p) { p.fp_guard = guard; }));
            const uint32_t carrier = w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
            w.sim.get_unit(carrier).pick_up_food(1, 25);
            w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 31});
            w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 33});
            const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{33, 30});
            w.run(5);
            w.sim.apply_command(command_of(CommandType::Stop, 0, {carrier}, 0, 0));              // (the carrier stands: no walk home takes it away)
            w.run(500);
            if (guard) ASSERT_TRUE(hp_of(w.sim, enemy) < 10u);
            else ASSERT_EQ(hp_of(w.sim, enemy), 10u);
        }
    } TEST_END();

    TEST_CASE("AI60.7 The Plans That Ship Have The Rule Off, And Whole Matches With It On Replay From Their Commands Alone (Four Hard Bots On TREASURE And On ISLANDS)") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_FALSE(plan_for(level).fight_plan);
        const LevelPlan by_hand;
        ASSERT_FALSE(by_hand.fight_plan);
        for (const char* name : {"TREASURE", "ISLANDS"}) {
            assets::LevelData level;
            ASSERT_TRUE(level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL"));
            ArenaSpec spec;
            spec.level = &level;
            spec.seed = 3;
            spec.record = true;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            spec.factory = [](const BotSpec& b) {
                return std::unique_ptr<Bot>(std::make_unique<StandardBot>(b.level, Style::Random, [](LevelPlan& plan) { plan.fight_plan = true; }));
            };
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_TRUE(replay_commands(spec, r).ok);
            for (const ArenaSeatResult& seat : r.seats) {
                ASSERT_EQ(seat.stats.filtered, 0u);                                              // (nothing that a person could not click)
                ASSERT_EQ(seat.stats.rejected, 0u);
            }
        }
    } TEST_END();
    TEST_CASE("AI60.8 Only Ants That No Task Holds Are Sent With fp_spare_only: Three Free Ants And One That The Swarm Holds Stand Near An Enemy Worker; The Free Ants Strike And The Held One Stays Where It Stands; Without The Switch It Strikes Too") {
        for (const bool spare : {true, false}) {
            World w(plan_fight([spare](LevelPlan& p) { p.fp_spare_only = spare; }));
            uint32_t held = 0;
            for (int i = 0; i < 4; ++i) {
                const uint32_t id = w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{29 + (i % 2), 29 + (i / 2) * 2});
                if (i == 3) {
                    held = id;
                    w.bot().ledger_for_labs().claim(id, StandardBot::kSwarm);
                }
            }
            const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{33, 30});
            uint16_t lowest = 10;
            w.run(500, [&]() { lowest = std::min(lowest, hp_of(w.sim, enemy)); });
            ASSERT_TRUE(lowest < 10u);                                                           // (three free ants are three against one: an edge either way)
            if (spare) ASSERT_EQ(w.bot().ledger().owner(held), StandardBot::kSwarm);             // the held one was never taken
            else ASSERT_TRUE(w.bot().ledger().owner(held) != StandardBot::kSwarm);               // ... taken for the fight (it is free or the plan's again by now)
        }
    } TEST_END();
}
