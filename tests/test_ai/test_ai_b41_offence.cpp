// The offence that the tournaments measured as a loss (B4-1, AI12.x): the harassment squad, the sabotage of another team's gate and the ambush at a thief hole. They are switches of the plan
// that are OFF at every level (AI11.6 pins that); the tests hold the features themselves in hand-made worlds, so that the numbers of docs/BOTS.md ("Aggression") can be reproduced and a
// later change of the engine's economy can be answered with another measurement instead of another implementation.
//
//   AI12.1  the harassment squad: Combat Ants hunt the carriers of other teams (one order per blow, the best opponent's first), not an enemy that is too strong near the target, not an ant
//           that stands on a power-up; the shipped plans send nobody
//   AI12.2  the sabotage: the Fire Ant of the bot lights walls on the ring round the gate of the best opponent, and gives the ant back to the walls of the own thief hole at once
//   AI12.3  the ambush: a thief that finds every hole shut waits near the hole of the leading team and raids when it opens; the shipped plans wait for nobody
#include "ai_test.hpp"
#include "b41_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

void run_b41_offence_tests() {
    TEST_CASE("AI12.1 The Harassment Squad (Flag): The Combat Ant Attacks The Carrier Of An Enemy, One Order Per Blow, And The Enemy Is Hurt; An Enemy Fighter Near The Target Above The Squad's Strength Keeps It Away; A Carrier That Stands On A Power-Up Is No Target; The Shipped Plans Send Nobody")
    {
        const auto build = [&](sim::SimulationEngine& sim, size_t enemy_fighters) {
            empty_field(sim, 61);
            sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 30});
            const uint32_t carrier = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{36, 30});
            sim.get_unit(carrier).pick_up_food(1, 25);
            for (size_t i = 0; i < enemy_fighters; ++i) sim.spawn_unit(1, sim::AntType::Combat, TileCoord{37 + static_cast<int32_t>(i), 31});
            return carrier;
        };
        LevelPlan plan = plan_for(Level::Hard);
        plan.harass = true;
        {   // (a) a lone carrier: the Combat Ant goes and strikes, again and again
            sim::SimulationEngine sim;
            const uint32_t carrier = build(sim, 0);
            const uint32_t fighter = first_of_type(sim, 0, sim::AntType::Combat);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(400);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_TRUE(bot.harass().attacks_ordered() >= 2);                                            // one order is one blow
            ASSERT_EQ(bot.harass().squad(), 1u);
            ASSERT_TRUE(sim.get_unit(carrier).hp < 10);                                                  // the blows landed
            bool named = false;
            for (const auto& e : rig.proposed) {
                if (e.second.type == CommandType::GroupAttack) named = named || (e.second.ants.size() == 1 && e.second.ants[0] == fighter);
            }
            ASSERT_TRUE(named);
        }
        {   // (b) two enemy Combat Ants stand next to the carrier: 16 against 8, the squad does not go
            sim::SimulationEngine sim;
            build(sim, 2);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(200);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
            ASSERT_TRUE(rig.as<StandardBot>().harass().refused_odds() >= 1);
            LevelPlan odds = plan;
            odds.harass_odds_percent = 40;                                                               // a squad that is content with 40 percent goes
            sim::SimulationEngine sim2;
            build(sim2, 2);
            Rig rig2(sim2, 0, Level::Hard, std::make_unique<StandardBot>(odds), 4, 4);
            rig2.run(200);
            ASSERT_TRUE(rig2.proposed_count(CommandType::GroupAttack) >= 1);
        }
        {   // (c) the carrier stands on a power-up (nobody can attack it there)
            sim::SimulationEngine sim;
            empty_field(sim, 61);
            sim.grid_mut().place_powerup(36, 30, 4);
            sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 30});
            const uint32_t carrier = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{36, 30});
            sim.get_unit(carrier).pick_up_food(1, 25);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(200);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
        }
        {   // (d) the shipped plans: nobody is sent
            for (const Level level : {Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                const uint32_t carrier = build(sim, 0);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(400);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
                ASSERT_EQ(sim.get_unit(carrier).hp, 10);
            }
        }
    } TEST_END();

    TEST_CASE("AI12.2 The Sabotage (Flag): The Fire Ant Lights Walls On The Ring Round The Gate Of The Best Opponent (Eight Tiles, The Queue Row Sealed); It Is Taken Back At Once For The Walls Of The Own Thief Hole; No Team With Points Or No Fire Ant, No Walls; The Shipped Plans Light None")
    {
        LevelPlan plan = plan_for(Level::Hard);
        plan.sabotage = true;
        plan.sabotage_after = 20;
        sim::SimulationEngine probe;
        empty_field(probe, 62);
        const MapInfo pmap(probe);
        const HillInfo& hill1 = pmap.hill(1);
        const std::array<TileCoord, 8> ring = SabotageTask::ring_of(hill1);
        const auto lit = [&](const sim::SimulationEngine& sim) {
            size_t n = 0;
            for (const TileCoord& t : ring) n += sim.grid().has_fire_at(t) ? 1u : 0u;
            return n;
        };
        {   // the ring: the row two above the queue row and its two ends, as the notes say
            ASSERT_EQ(ring[0].y, hill1.origin.y - 2);
            size_t row = 0;
            for (const TileCoord& t : ring) row += t.y == hill1.origin.y - 2 ? 1u : 0u;
            ASSERT_EQ(row, 5u);
            for (const TileCoord& q : hill1.starts) ASSERT_TRUE(q.y == hill1.origin.y - 1);                // (the queue row is the row that the ring seals)
        }
        {   // (a) team 1 leads with 400 points: a Fire Ant that is not the keeper of the own walls lights the ring round its gate, the ring stands and the ant goes back to work; the lone Fire Ant of
            //     a bot is the keeper and stays out of it (the sabotage is for a second one, stolen)
            sim::SimulationEngine sim;
            empty_field(sim, 62);
            sim.set_player_score(1, 400);
            const uint32_t keeper = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 30});
            const uint32_t spare = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{32, 30});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(1500);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_EQ(bot.sabotage().target(), 1);
            ASSERT_TRUE(bot.sabotage().walls_ordered() >= 6);
            ASSERT_TRUE(lit(sim) >= 6);                                                                    // (a tile can be a rock or stood on: the ring holds what can be lit)
            ASSERT_TRUE(sim.get_unit(keeper).type == sim::AntType::Fire && sim.get_unit(spare).type == sim::AntType::Fire);
            ASSERT_EQ(bot.sabotage().working(), 0u);                                                       // the ring stands: the ant is free
            ASSERT_EQ(bot.tactics().wall_keeper, keeper);                                                  // the first Fire Ant keeps the own walls, the other one worked
            sim::SimulationEngine one;
            empty_field(one, 62);
            one.set_player_score(1, 400);
            one.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 30});
            Rig lone(one, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            lone.run(800);
            ASSERT_EQ(lone.as<StandardBot>().sabotage().walls_ordered(), 0u);                              // a lone Fire Ant is the keeper's
            ASSERT_EQ(lit(one), 0u);
            LevelPlan any = plan;
            any.sabotage_spare_keeper = false;                                                             // (the first version of the measurements: any Fire Ant that is free)
            sim::SimulationEngine two;
            empty_field(two, 62);
            two.set_player_score(1, 400);
            two.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 30});
            Rig lone2(two, 0, Level::Hard, std::make_unique<StandardBot>(any), 4, 4);
            lone2.run(1500);
            ASSERT_TRUE(lit(two) >= 6);
        }
        {   // (b) the own thief hole needs its walls (an enemy Thief is in sight): the walls take the ant from the sabotage
            sim::SimulationEngine sim;
            empty_field(sim, 62);
            sim.set_player_score(1, 400);
            const uint32_t fire = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 30});
            LevelPlan any = plan;
            any.sabotage_spare_keeper = false;                                                             // (one Fire Ant for both jobs: the walls of the own hole come first)
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(any), 4, 4);
            rig.run(120);
            ASSERT_EQ(rig.as<StandardBot>().sabotage().working(), 1u);
            sim.spawn_unit(2, sim::AntType::Thief, TileCoord{30, 40});                                      // a thief in sight: the walls of the own thief hole are wanted
            rig.run(600);
            ASSERT_EQ(walls_east(sim, kFightHills[0]), 3u);                                                // the own walls stand
            ASSERT_TRUE(sim.get_unit(fire).type == sim::AntType::Fire);
        }
        {   // (c) nobody has 100 points: nothing to sabotage; no Fire Ant: nothing either
            sim::SimulationEngine sim;
            empty_field(sim, 62);
            sim.set_player_score(1, 50);
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 30});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(400);
            ASSERT_EQ(rig.as<StandardBot>().sabotage().walls_ordered(), 0u);
            sim::SimulationEngine sim2;
            empty_field(sim2, 62);
            sim2.set_player_score(1, 400);
            sim2.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
            Rig rig2(sim2, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig2.run(400);
            ASSERT_EQ(rig2.as<StandardBot>().sabotage().walls_ordered(), 0u);
            ASSERT_EQ(lit(sim2), 0u);
        }
        {   // (d) the shipped plans light nothing at another gate
            for (const Level level : {Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                empty_field(sim, 62);
                sim.set_player_score(1, 400);
                sim.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 30});
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(800);
                ASSERT_EQ(lit(sim), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI12.3 The Ambush (Flag): A Thief That Finds Every Thief Hole Shut Waits Six Tiles East Of The Hole Of The Leading Team And Raids As Soon As The Hole Is Open; It Goes Back To The Economy After A While; One Thief Waits At A Time; The Shipped Plans Wait For Nobody")
    {
        LevelPlan plan = plan_for(Level::Hard);
        plan.ambush = true;
        plan.ambush_ticks = 900;
        sim::SimulationEngine probe;
        empty_field(probe, 63);
        const MapInfo pmap(probe);
        const HillInfo& hill = pmap.hill(2);                                                                  // team 2 leads: its hill is at (4, 50)
        const auto shut = [&](sim::SimulationEngine& sim) {
            for (const TileCoord& t : east_tiles(hill)) sim.set_fire_at(t, 3500);
        };
        {   // (a) the hole is shut: the thief walks to its waiting place and stands; the hole opens: it raids
            sim::SimulationEngine sim;
            empty_field(sim, 63);
            sim.set_player_score(2, 300);
            shut(sim);
            const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(500);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_EQ(bot.raids().raids_ordered(), 0u);                                                    // nothing to raid through three walls
            ASSERT_EQ(bot.raids().waiting(), 1u);
            const TileCoord at{sim.get_unit(thief).pos.x, sim.get_unit(thief).pos.y};
            ASSERT_TRUE(at.chebyshev_dist(TileCoord{hill.origin.x + 4 + plan.ambush_distance, hill.origin.y + 2}) <= 3);
            sim.grid_mut().clear_firewall(static_cast<uint32_t>(east_tiles(hill)[1].x), static_cast<uint32_t>(east_tiles(hill)[1].y));       // a wall burns out
            rig.run(20);
            ASSERT_TRUE(rig.as<StandardBot>().raids().raids_ordered() >= 1);
            ASSERT_EQ(rig.as<StandardBot>().raids().last_target(), 2);
        }
        {   // (b) nothing opens for 900 ticks: the thief gives up (and is not sent again at once)
            sim::SimulationEngine sim;
            empty_field(sim, 63);
            sim.set_player_score(2, 300);
            shut(sim);
            sim.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(1100);
            ASSERT_EQ(rig.as<StandardBot>().raids().waiting(), 0u);
        }
        {   // (c) two thieves: one waits, the other is left to the economy
            sim::SimulationEngine sim;
            empty_field(sim, 63);
            sim.set_player_score(2, 300);
            shut(sim);
            sim.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
            sim.spawn_unit(0, sim::AntType::Thief, TileCoord{22, 20});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(300);
            ASSERT_EQ(rig.as<StandardBot>().raids().waiting(), 1u);
        }
        {   // (d) the shipped plans: the thief stays with the economy
            for (const Level level : {Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                empty_field(sim, 63);
                sim.set_player_score(2, 300);
                shut(sim);
                sim.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(300);
                ASSERT_EQ(rig.as<StandardBot>().raids().waiting(), 0u);
            }
        }
    } TEST_END();
}
