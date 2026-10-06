// Fights of the standard bot (B4-1, AI9.x): the last ants stay out of fights, hatching for a fight, the strike force of the bot that is behind, the wipe-out focus. Hand-made worlds
// (b41_helpers.hpp), quick enough for suite 2.20.
//
//   AI9.1   never be wiped out: no ant is sent to fight while the bot has no more than fight_reserve ants (one more without an egg)
//   AI9.2   hatching: only for a fight (a blow lately, enemy fighters at the hill, a strike), never for the economy; Medium and Hard; the points, an egg, nothing incubating, time
//   AI9.3   the strike: behind and winnable leads to a fight on the leader's carriers, behind but hopeless to none, ahead to none; Medium and Hard
//   AI9.4   the wipe-out focus (Hard only): an enemy that has lost most of its ants is hunted down by a far stronger force
//   AI9.5   the shipped plans: strikes, wipe-out focus and hatching are off at every level, and nothing else of the fight rules is
#include "ai_test.hpp"
#include "b41_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

size_t attack_orders(const Rig& rig) { return rig.proposed_count(CommandType::GroupAttack); }

// The plans of the levels with the fight features ON as designed (the strike and the hatching for Medium and Hard, the wipe-out focus for Hard): in the SHIPPED plans the three flags
// are off, because the tournaments measured that they cost score (docs/BOTS.md, AI9.5); the tests hold the features themselves
LevelPlan fight_plan(Level level, bool strikes = true, bool hatches = true, bool wipe = true) {
    LevelPlan p = plan_for(level);
    p.strikes = strikes && level != Level::Easy;
    p.hatches = hatches && level != Level::Easy;
    p.wipe_focus = wipe && level == Level::Hard;
    return p;
}

}  // namespace

void run_b41_fight_tests() {
    TEST_CASE("AI9.1 Never Be Wiped Out: With No More Than Two Ants (Three When No Egg Is Left) No Ant Is Sent To Fight Whoever Hits, With More The Level's Defenders Go; A Fight That Is On Ends When The Bot Is Down To Its Last Ants") {
        struct Case {
            size_t ants;
            uint32_t eggs;
            bool fights;
        };
        const Case cases[] = {
            {2, 5, false},    // the last two ants
            {3, 5, true},     // three ants with an egg to fall back on
            {3, 0, false},    // three ants and no egg: one more is kept out
            {4, 0, true},
            {7, 0, true},
        };
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            for (const Case& k : cases) {
                sim::SimulationEngine sim;
                empty_field(sim, 101);
                sim.set_player_eggs(0, k.eggs);
                std::vector<uint32_t> mine;
                for (size_t i = 0; i < k.ants; ++i) mine.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22 + static_cast<int32_t>(i) % 4, 20 + static_cast<int32_t>(i) / 4}));
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 24});
                LevelPlan plan = plan_for(level);
                plan.wipe_focus = false;
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(3);
                for (int t = 0; t < 240; ++t) {
                    keep_attacking(sim, 1, enemy, mine[0]);
                    rig.tick();
                }
                ASSERT_TRUE(sim.get_unit(mine[0]).hp < 10 || !alive(sim, mine[0]));                           // it was hit
                if (k.fights) ASSERT_TRUE(attack_orders(rig) > 0);
                else ASSERT_EQ(attack_orders(rig), 0u);
            }
        }
        // a fight that is on ends when the ants are gone: five ants, the enemy kills until two are left; no order after that
        {
            sim::SimulationEngine sim;
            empty_field(sim, 102);
            sim.set_player_eggs(0, 5);
            std::vector<uint32_t> mine;
            for (int i = 0; i < 5; ++i) mine.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22 + i % 4, 20 + i / 4}));
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 24});
            LevelPlan plan = plan_for(Level::Hard);
            plan.wipe_focus = false;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(3);
            for (int t = 0; t < 120; ++t) {
                keep_attacking(sim, 1, enemy, mine[0]);
                rig.tick();
            }
            ASSERT_TRUE(attack_orders(rig) > 0);
            sim.get_unit(mine[2]).hp = 0;                                                                     // three ants die: two are left
            sim.get_unit(mine[3]).hp = 0;
            sim.get_unit(mine[4]).hp = 0;
            rig.run(30);
            const size_t before = attack_orders(rig);
            for (int t = 0; t < 200; ++t) {
                keep_attacking(sim, 1, enemy, mine[0]);
                rig.tick();
            }
            ASSERT_EQ(attack_orders(rig), before);
        }
    } TEST_END();

    TEST_CASE("AI9.2 Hatching Only To Replace What A Fight Cost, Never For The Economy (Flag, Medium And Hard): With 1000 Points And Five Eggs Nothing Is Hatched While All Six Ants Live, However Often They Are Hit Less Than Three Times; After A Fight (Three Ants Hit Or One Lost) The Lost Ant Is Replaced: ONE Egg At A Time While The Bot Has Fewer Ants Than At The Start; Not Below 250 Points, Without An Egg, In The Last 600 Ticks, At Easy, Or When An Ally's Points Only Make The Box Look Richer; The Strike Adds One Ant")
    {
        const auto build = [&](sim::SimulationEngine& sim, int32_t score, uint32_t eggs, uint32_t ticks = 14400) {
            empty_field(sim, 111, ticks);
            sim.set_player_score(0, score);
            sim.set_player_eggs(0, eggs);
            std::vector<uint32_t> mine;
            for (int i = 0; i < 6; ++i) mine.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14 + i % 3, 12 + i / 3}));
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{44, 8});
            return mine;
        };
        for (const Level level : {Level::Medium, Level::Hard}) {
            // (a) no fight: nothing, however rich; hits on two ants (below the three of a fight) are no fight
            {
                sim::SimulationEngine sim;
                const auto mine = build(sim, 1000, 5);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(fight_plan(level)), 4, 4);
                rig.run(10);
                sim.get_unit(mine[0]).hp = 8;
                sim.get_unit(mine[1]).hp = 8;
                rig.run(1200);
                ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 0u);
                ASSERT_EQ(sim.get_player_eggs(0), 5u);
            }
            // (a2) three ants hit at once (a fight) but none lost: nothing to replace
            {
                sim::SimulationEngine sim;
                const auto mine = build(sim, 1000, 5);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(fight_plan(level)), 4, 4);
                rig.run(10);
                for (int i = 0; i < 3; ++i) sim.get_unit(mine[static_cast<size_t>(i)]).hp = 8;
                rig.run(400);
                ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 0u);
            }
            // (b) an ant is lost: one egg now, the newborn replaces it, no stream of eggs; a second loss later: a second egg
            {
                sim::SimulationEngine sim;
                const auto mine = build(sim, 1000, 5);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(fight_plan(level)), 4, 4);
                rig.run(10);
                sim.get_unit(mine[0]).hp = 0;
                rig.run(120);
                ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 1u);
                ASSERT_EQ(sim.get_player_eggs(0), 4u);
                ASSERT_EQ(sim.get_player_score(0), 800);
                rig.run(400);                                                                                    // the newborn is there: six ants again (the dead one is out of the view): no more
                ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 1u);
                ASSERT_EQ(sim.get_player_eggs(0), 4u);
                sim.get_unit(mine[1]).hp = 0;
                rig.run(150);
                ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 2u);
                ASSERT_EQ(sim.get_player_eggs(0), 3u);
            }
            // (b2) two ants lost at once: one egg at a time, the second click waits until the first newborn is out
            {
                sim::SimulationEngine sim;
                const auto mine = build(sim, 1000, 5);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(fight_plan(level)), 4, 4);
                rig.run(10);
                sim.get_unit(mine[0]).hp = 0;
                sim.get_unit(mine[1]).hp = 0;
                rig.run(120);
                ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 1u);
                rig.run(600);
                ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 2u);
            }
            // (c0) the margin of an ally is not asked of a bot that has none: 400 points are enough (250 asked; with an ally 650)
            {
                sim::SimulationEngine sim;
                const auto mine = build(sim, 400, 5);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(fight_plan(level)), 4, 4);
                rig.run(10);
                sim.get_unit(mine[0]).hp = 0;
                rig.run(200);
                ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 1u);
            }
            // (c) the conditions: 249 points are not enough, no egg, the last 600 ticks
            for (const int variant : {0, 1, 2}) {
                sim::SimulationEngine sim;
                const auto mine = build(sim, variant == 0 ? 249 : 1000, variant == 1 ? 0u : 5u, variant == 2 ? 700u : 14400u);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(fight_plan(level)), 4, 4);
                rig.run(100);                                                                                    // (the match of variant 2 has 600 ticks left)
                sim.get_unit(mine[0]).hp = 0;
                rig.run(200);
                ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 0u);
            }
            // (d) with an ally the box is the sum of both scores: 100 + 400 = 500 does not show that the click would be accepted (it needs 200 of the own score): 650 are asked, and a click that
            //     is refused anyway is not repeated for a while
            for (const int32_t ally_points : {400, 600}) {
                sim::SimulationEngine sim;
                const auto mine = build(sim, 100, 5);
                sim.set_player_score(1, ally_points);
                sim.form_alliance(0, 1);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(fight_plan(level)), 4, 4);
                rig.run(10);
                sim.get_unit(mine[0]).hp = 0;
                rig.run(400);
                ASSERT_EQ(rig.proposed_count(CommandType::Hatch), ally_points == 400 ? 0u : 1u);                 // (600: the click is proposed once, the engine refuses it, the bot waits)
            }
        }
        // Easy never hatches, and the shipped Medium and Hard plans do not either (the flag is off): a loss changes nothing
        for (const bool easy : {true, false}) {
            sim::SimulationEngine sim;
            const auto mine = build(sim, 1000, 5);
            LevelPlan plan = plan_for(easy ? Level::Easy : Level::Hard);
            if (easy) plan.hatches = false;
            Rig rig(sim, 0, easy ? Level::Easy : Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(10);
            sim.get_unit(mine[0]).hp = 0;
            rig.run(300);
            ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 0u);
        }
        // the strike adds one ant to the six: with the strike out the bot hatches one although none was lost
        {
            sim::SimulationEngine sim;
            empty_field(sim, 112);
            sim.set_player_score(0, 700);
            sim.set_player_score(1, 1500);
            sim.set_player_eggs(0, 5);
            for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i % 3, 9 + i / 3});
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Combat, TileCoord{14 + i, 10});
            for (int i = 0; i < 2; ++i) {
                const uint32_t id = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{47 + i, 7});
                sim.get_unit(id).pick_up_food(1, 25);
            }
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(fight_plan(Level::Medium)), 4, 4);
            rig.run(120);
            ASSERT_TRUE(rig.as<StandardBot>().strike().active());
            ASSERT_EQ(rig.proposed_count(CommandType::Hatch), 1u);                                                 // nine ants and one more (the egg that incubates counts: no second)
        }
    } TEST_END();

    TEST_CASE("AI9.3 The Strike (If You Are Losing, Force A Fight): Clearly Behind The Leader And Winnable (The Own Strength, A Combat Ant 2, At Least 150 Percent Of The Enemy's Near Its Hill) Sends A Force To Hunt The Leader's Carriers; Behind But Hopeless, Ahead, Not Behind Enough, Too Few Ants, The Last 900 Ticks, Or Easy Send None; The Force Is Called Off When The Bot Draws Level")
    {
        struct World {
            sim::SimulationEngine sim;
            std::vector<uint32_t> carriers;
            void build(int32_t mine, int32_t leader, size_t ants, bool hopeless, uint32_t ticks = 14400, int combats = 3) {
                empty_field(sim, 121, ticks);
                sim.set_player_score(0, mine);
                sim.set_player_score(1, leader);
                for (size_t i = 0; i < ants; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + static_cast<int32_t>(i) % 4, 9 + static_cast<int32_t>(i) / 4});
                for (int i = 0; i < combats; ++i) sim.spawn_unit(0, sim::AntType::Combat, TileCoord{14 + i, 10});
                for (int i = 0; i < 2; ++i) {                                                                // two carriers on their way to the hill of team 1 (at (50, 4), queue (51, 3))
                    const uint32_t id = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{47 + i, 7});
                    sim.get_unit(id).pick_up_food(1, 25);
                    carriers.push_back(id);
                }
                if (hopeless) {
                    for (int i = 0; i < 5; ++i) sim.spawn_unit(1, sim::AntType::Worker, TileCoord{46 + i, 5});
                    for (int i = 0; i < 2; ++i) sim.spawn_unit(1, sim::AntType::Combat, TileCoord{48 + i, 6});
                }
            }
        };
        for (const Level level : {Level::Medium, Level::Hard}) {
            // (a) behind (100 against 700) and winnable (the force of 3 or 4 against two carriers): the force goes, the orders name the carriers, the first ants are the Combat Ant and workers
            {
                World w;
                w.build(100, 700, 8, false);
                Rig rig(w.sim, 0, level, std::make_unique<StandardBot>(fight_plan(level, true, false, false)), 4, 4);
                rig.run(60);
                const StandardBot& bot = rig.as<StandardBot>();
                ASSERT_TRUE(bot.strike().active());
                ASSERT_EQ(bot.strike().target(), 1);
                ASSERT_EQ(bot.strike().force(), 3u);                                                            // the three Combat Ants (the force is made of Combat Ants: they do not harvest anyway)
                ASSERT_EQ(count_type(w.sim, 0, sim::AntType::Worker), 8u);
                for (const sim::AntSnapshot& a : w.sim.get_world_state().ants) {
                    if (a.player_id == 0 && a.raw_type == sim::AntType::Worker) ASSERT_TRUE(bot.ledger().owner(a.id) != StandardBot::kStrike);
                }
                ASSERT_TRUE(bot.tactics().standing.behind);
                size_t on_carriers = 0;
                for (const auto& e : rig.proposed) {
                    if (e.second.type != CommandType::GroupAttack) continue;
                    for (const uint32_t id : w.carriers) {
                        if (e.second.tile_x == w.sim.get_unit(id).pos.x && e.second.tile_y == w.sim.get_unit(id).pos.y) ++on_carriers;
                    }
                }
                ASSERT_TRUE(on_carriers >= 1);
                // the force is the nearest... the Combat Ant is in it
                const uint32_t combat = first_of_type(w.sim, 0, sim::AntType::Combat);
                ASSERT_EQ(bot.ledger().owner(combat), StandardBot::kStrike);
                // (g) it draws level: the force goes back to the pool at once
                w.sim.set_player_score(0, 900);
                rig.run(30);
                ASSERT_FALSE(bot.strike().active());
                ASSERT_EQ(bot.strike().force(), 0u);
                ASSERT_EQ(bot.ledger().count(StandardBot::kStrike), 0u);
            }
            // (a2) with strike_workers the workers join: Combat Ants first, then workers (Medium three, Hard four)
            {
                World w;
                w.build(100, 700, 8, false);
                LevelPlan plan = fight_plan(level, true, false, false);
                plan.strike_workers = true;
                Rig rig(w.sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(60);
                ASSERT_TRUE(rig.as<StandardBot>().strike().active());
                ASSERT_EQ(rig.as<StandardBot>().strike().force(), level == Level::Hard ? 4u : 3u);
            }
            // (b) behind but hopeless: seven more enemy ants (two of them Combat Ants) stand near the hill: 11 against our 4 or 5
            {
                World w;
                w.build(100, 700, 8, true);
                Rig rig(w.sim, 0, level, std::make_unique<StandardBot>(fight_plan(level, true, false, false)), 4, 4);
                rig.run(60);
                ASSERT_FALSE(rig.as<StandardBot>().strike().active());
                ASSERT_EQ(rig.as<StandardBot>().strike().force(), 0u);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
            }
            // (c) ahead, (d) not behind enough (the leader has 300, the bot 200: the margin of 150 is not there), (e) too few ants (four: the reserve of three leaves one), (f) the last 900 ticks
            for (const int variant : {0, 1, 2, 3}) {
                World w;
                if (variant == 0) w.build(800, 700, 8, false);
                if (variant == 1) w.build(200, 300, 8, false);
                if (variant == 2) w.build(100, 700, 1, false, 14400, 3);                                        // one worker and three Combat Ants: four ants, the reserve of three leaves one
                if (variant == 3) w.build(100, 700, 8, false, 800);
                Rig rig(w.sim, 0, level, std::make_unique<StandardBot>(fight_plan(level, true, false, false)), 4, 4);
                rig.run(60);
                ASSERT_FALSE(rig.as<StandardBot>().strike().active());
                ASSERT_EQ(rig.as<StandardBot>().strike().force(), 0u);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
            }
        }
        // Easy has no strike (and no Combat Ant fetched for one)
        {
            World w;
            w.build(100, 700, 8, false);
            Rig rig(w.sim, 0, Level::Easy, std::make_unique<StandardBot>(fight_plan(Level::Easy)), 4, 4);
            rig.run(60);
            ASSERT_FALSE(rig.as<StandardBot>().strike().active());
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
        }
    } TEST_END();

    TEST_CASE("AI9.4 The Wipe-Out Focus (Hard Only): An Enemy Team That Showed Six Ants And Shows Two Now Is Hunted Down (Every Ant Of It Is A Target, Not Only Carriers) By A Force That Is Three Times Stronger; Medium Does Not; A Team That Never Showed More Than Two Ants Is Not Hunted; A Force That Is Not Three Times Stronger Stays Home")
    {
        struct World {
            sim::SimulationEngine sim;
            std::vector<uint32_t> enemies;
            void build(size_t enemy_ants, size_t combats) {
                empty_field(sim, 131);
                for (size_t i = 0; i < combats; ++i) sim.spawn_unit(0, sim::AntType::Combat, TileCoord{14 + static_cast<int32_t>(i), 10});
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 9});
                for (size_t i = 0; i < enemy_ants; ++i) enemies.push_back(sim.spawn_unit(1, sim::AntType::Worker, TileCoord{44 + static_cast<int32_t>(i) % 3, 8 + static_cast<int32_t>(i) / 3}));
                sim.set_player_score(0, 400);
                sim.set_player_score(1, 400);
            }
        };
        for (const Level level : {Level::Medium, Level::Hard}) {
            for (const int variant : {0, 1, 2}) {                                                              // 0: lost four of six; 1: never more than two; 2: lost four but the force is weak (no Combat Ant)
                World w;
                w.build(variant == 1 ? 2u : 6u, variant == 2 ? 0u : 3u);
                Rig rig(w.sim, 0, level, std::make_unique<StandardBot>(fight_plan(level, false, false, true)), 4, 4);
                rig.run(12);                                                                                    // the bot sees six enemy ants (or two)
                if (variant != 1) {
                    for (size_t i = 2; i < 6; ++i) w.sim.get_unit(w.enemies[i]).hp = 0;                       // four of them die
                }
                rig.run(40);
                const StandardBot& bot = rig.as<StandardBot>();
                const bool wipes = level == Level::Hard && variant == 0;
                ASSERT_EQ(bot.strike().active(), wipes);
                ASSERT_EQ(bot.strike().wiping(), wipes);
                if (wipes) {
                    ASSERT_TRUE(bot.strike().force() >= 3);
                    size_t on_enemies = 0;
                    for (const auto& e : rig.proposed) {
                        if (e.second.type != CommandType::GroupAttack) continue;
                        for (size_t i = 0; i < 2; ++i) {
                            if (e.second.tile_x == w.sim.get_unit(w.enemies[i]).pos.x && e.second.tile_y == w.sim.get_unit(w.enemies[i]).pos.y) ++on_enemies;
                        }
                    }
                    ASSERT_TRUE(on_enemies >= 1);                                                              // the targets are the two ants that are left (idle, not carriers)
                } else {
                    ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI9.4b The Wipe-Out Focus Is Careful: Two Combat Ants Among The Six That Showed (16 Against The Force's 24: Not Three Times Stronger) And Four Left Of Eight (More Than Three) Are Not Hunted") {
        for (const int variant : {3, 4}) {
            sim::SimulationEngine sim;
            empty_field(sim, 131);
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Combat, TileCoord{14 + i, 10});
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 9});
            std::vector<uint32_t> enemies;
            const int shown = variant == 3 ? 6 : 8;
            for (int i = 0; i < shown; ++i) enemies.push_back(sim.spawn_unit(1, variant == 3 && i < 2 ? sim::AntType::Combat : sim::AntType::Worker, TileCoord{44 + i % 3, 8 + i / 3}));
            sim.set_player_score(0, 400);
            sim.set_player_score(1, 400);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(fight_plan(Level::Hard, false, false, true)), 4, 4);
            rig.run(12);
            for (int i = variant == 3 ? 2 : 4; i < shown; ++i) sim.get_unit(enemies[static_cast<size_t>(i)]).hp = 0;
            rig.run(40);
            ASSERT_FALSE(rig.as<StandardBot>().strike().wiping());
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
        }
    } TEST_END();

    TEST_CASE("AI9.5 The Shipped Plans: The Strike, The Wipe-Out Focus And Hatching Are Off At Every Level (The Tournaments Measured That Each Costs Score, docs/BOTS.md); The Last Ants Stay Out Of Fights At Every Level") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan p = plan_for(level);
            ASSERT_FALSE(p.strikes);
            ASSERT_FALSE(p.wipe_focus);
            ASSERT_FALSE(p.hatches);
            ASSERT_EQ(p.fight_reserve, 2u);
            ASSERT_EQ(p.bench_idle_ticks, 0u);                                                                    // (the tournaments' handicap is never part of a level)
        }
    } TEST_END();
}
