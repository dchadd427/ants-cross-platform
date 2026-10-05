// The fights, the fire and the standing of the standard bot (contest batch, AI20.2 - AI20.9): the owner's report "the hard bots don't really fight unless they're defending; the losing player should be a
// little more aggressive", "they need to be aware of the health of other players ... KDR", "the ant was 4 HP and it just didn't kill it", the three bug reports on the fire play, and what followed
// from them (docs/BOTS.md, "Fights of its own", "Fire play", "Behind the leader and the endgame"). Hand-made worlds (b41_helpers.hpp), quick enough for suite 2.20.
//
//   AI20.2  a skirmish of the bot's own (flag, off in every shipped plan): the stronger force attacks a carrier or a worker at a pile without a blow first
//   AI20.3  health-aware fighting: the target that dies soonest, an attacker one blow from the kill stays, an own ant that would die first is relieved, a fight is kept up only while a kill is in
//           reach, a survivor that walks home is not chased
//   AI20.4  hunting the kill, the owner's case: a wounded enemy within reach of a Combat Ant and a worker is attacked at once (no attack was ordered before), by the Combat Ant alone; two workers
//           cannot kill it and do not try; the kills are made
//   AI20.5  the hunt: the plan of the blows as a table, the choice (the one that dies soonest), the scope by level, kept until done, the stronger force, the last ants and the carriers
//   AI20.6  the fire-in is safe: no lone Fire Ant against a team that can put the fire out, escorts, the give-up
//   AI20.7  the defence against being fired in: the fighters go after the Fire Ant, the walls are put out when it is safe
//   AI20.8  no task orders a special order at a tile that an ant stands on
//   AI20.9  behind the leader and the endgame: the pressure and its tiers, the strike, Easy's raids, the last minute
#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "contest_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;
using namespace contest;

void run_contest_tests() {
    TEST_CASE("AI20.2 A Skirmish Of The Bot's Own (Hard): With The Stronger Force Near A Carrier Of An Enemy It Attacks Without Waiting To Be Hit, One Order Per Blow, And The Carrier Is Hurt; Not With The Weaker Force (Enemy Fighters Near The Carrier), Not With One Ant, Not A Thief, Not An Ant That Stands On A Power-Up; A Plan Without The Flag Waits To Be Hit") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_FALSE(plan_for(level).skirmish);        // (built and measured as a loss: shipped OFF, docs/BOTS.md)
        LevelPlan plan = plan_for(Level::Hard);
        plan.skirmish = true;
        plan.health_aware = true;
        const auto build = [&](sim::SimulationEngine& sim, size_t own, size_t enemy_fighters) {
            empty_field(sim, 71);
            for (size_t i = 0; i < own; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21 + static_cast<int32_t>(i), 30});     // (6 to 8 tiles from the carrier: in reach, and beyond the 3 tiles of a Combat Ant's reflex)
            const uint32_t carrier = carrier_at(sim, 1, TileCoord{29, 30});
            for (size_t i = 0; i < enemy_fighters; ++i) sim.spawn_unit(1, sim::AntType::Combat, TileCoord{30 + static_cast<int32_t>(i), 32});
            return carrier;
        };
        {   // (a) four idle ants, a lone carrier six tiles away: the force goes (three of them), the orders name the carrier, and it is hurt
            sim::SimulationEngine sim;
            const uint32_t carrier = build(sim, 4, 0);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(300);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_TRUE(bot.fight().offence_started() >= 1);
            const auto attacks = attacks_of(rig);
            ASSERT_TRUE(!attacks.empty());
            ASSERT_TRUE(attacks.front().second.ants.size() >= 2 && attacks.front().second.ants.size() <= plan.skirmish_force);       // a force, not an ant
            ASSERT_TRUE(!alive(sim, carrier) || sim.get_unit(carrier).hp < 10);                                                      // the blows landed
            ASSERT_TRUE(attacks.size() >= 2);                                                                                        // one order is one blow: it is ordered again
        }
        {   // (b) three enemy Combat Ants stand next to the carrier: 3 x (10 x 2) hit points and damage against our four workers: not the stronger force, nobody goes
            sim::SimulationEngine sim;
            build(sim, 4, 3);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(200);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
            ASSERT_EQ(rig.as<StandardBot>().fight().offence_started(), 0u);
            LevelPlan bold = plan;
            bold.skirmish_odds_percent = 10;                                                                                         // a bot that takes any fight goes
            sim::SimulationEngine sim2;
            build(sim2, 4, 3);
            Rig rig2(sim2, 0, Level::Hard, std::make_unique<StandardBot>(bold), 4, 4);
            rig2.run(200);
            ASSERT_TRUE(rig2.proposed_count(CommandType::GroupAttack) >= 1);
        }
        {   // (c) one ant alone is no force
            sim::SimulationEngine sim;
            build(sim, 1, 0);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 10});                                                              // (a second ant far away: the reserve of the last ants is kept)
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{11, 10});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(200);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
        }
        {   // (d) a Thief with loot is the walls' and the raid's business, an ant on a power-up cannot be attacked at all
            sim::SimulationEngine sim;
            empty_field(sim, 71);
            sim.grid_mut().place_powerup(29, 30, 4);
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24 + i, 30});
            const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{29, 33});
            sim.get_unit(thief).pick_up_food(1, 25);
            const uint32_t on_powerup = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{29, 30});
            sim.get_unit(on_powerup).pick_up_food(1, 25);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(200);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
        }
        {   // (e) the plan without the flag waits to be hit; so do the levels whose plans have none
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                build(sim, 4, 0);
                LevelPlan off = plan_for(level);
                off.skirmish = false;
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(off), 4, 4);
                rig.run(200);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
            }
        }
        {   // (f) a worker at a pile is a target too (it is on the way of nobody's order but its own harvest)
            sim::SimulationEngine sim;
            empty_field(sim, 71);
            add_pile(sim, 30, 30, 40, 25);
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24 + i, 36});
            const uint32_t worker = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{31, 31});
            LevelPlan p = plan;
            p.contest_opening_ants = 0;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
            rig.run(120);
            const auto attacks = attacks_of(rig);
            ASSERT_TRUE(!attacks.empty());
            ASSERT_TRUE(!alive(sim, worker) || sim.get_unit(worker).hp < 10 || rig.as<StandardBot>().fight().offence_started() >= 1);
        }
    } TEST_END();

    TEST_CASE("AI20.3 Health-Aware Fighting (The Owner: \"If They're Attacking An Ant And They're Low Health, They Should Continue And Try To Kill The Ant\"): The Target That Dies Soonest First, A Wounded Target Is Finished (An Attacker One Blow From The Kill Stays Though It Is Hurt), An Own Ant That Would Die Before Its Target Is Relieved By Another; Without The Flag The Same Fights Are Fought By Distance")
    {
        LevelPlan plan = plan_for(Level::Hard);
        plan.skirmish = true;
        plan.health_aware = true;
        {   // (a) two carriers at about the same distance, the fresh one a little nearer: the wounded one (2 hit points) is attacked first with the flag, the fresh one without it
            for (const bool health : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 72);
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24 + i, 30});
                carrier_at(sim, 1, TileCoord{29, 28});
                const uint32_t hurt = carrier_at(sim, 2, TileCoord{29, 33});
                sim.get_unit(hurt).hp = 2;
                LevelPlan p = plan;
                p.health_aware = health;
                p.hunt = false;                                                                                                       // (the hunt of a kill reads the hit points by itself: AI20.4; this is the skirmish's choice of target)
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
                rig.run(40);
                const auto attacks = attacks_of(rig);
                ASSERT_TRUE(!attacks.empty());
                const Command& first = attacks.front().second;
                const bool on_hurt = first.tile_x >= 28 && first.tile_y >= 32;
                ASSERT_EQ(on_hurt, health);
            }
        }
        {   // (b) an attacker one blow from the kill stays though it is hurt: three fighters at a carrier, then (a blow later) one of them has 2 hit points (below the 3 that keeps an ant in a fight)
            //     and the carrier one; with the flag the ant finishes it, without it the ant leaves the fight
            for (const bool health : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 73);
                const uint32_t a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27, 30});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27, 31});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 30});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 31});                                                          // (the reserve of the last ants stays out of the fight)
                const uint32_t carrier = carrier_at(sim, 1, TileCoord{31, 30});
                LevelPlan p = plan;
                p.health_aware = health;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
                rig.run(12);
                ASSERT_TRUE(rig.as<StandardBot>().fight().offence_started() >= 1);
                ASSERT_EQ(rig.as<StandardBot>().ledger().owner(a), StandardBot::kFight);
                sim.get_unit(a).hp = 2;
                sim.get_unit(carrier).hp = 1;
                rig.run(8);
                const bool stays = rig.as<StandardBot>().ledger().owner(a) == StandardBot::kFight || !alive(sim, carrier);
                ASSERT_EQ(stays, health);
            }
        }
        {   // (c) an own ant that would die before its target is relieved: three fighters, one of them with 3 hit points while an enemy Combat Ant stands next to the target (a punch takes 2): with
            //     the flag it leaves the fight and a fresh ant that was not in it takes its place; without the flag it stays (the threshold is 3)
            for (const bool health : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 74);
                const uint32_t weak = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27, 30});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27, 31});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 30});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 31});
                carrier_at(sim, 1, TileCoord{31, 30});
                sim.spawn_unit(1, sim::AntType::Combat, TileCoord{33, 33});                                                          // a Combat Ant (alone: not too strong for the force)
                LevelPlan p = plan;
                p.skirmish_force = 3;
                p.health_aware = health;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
                rig.run(24);
                ASSERT_TRUE(rig.as<StandardBot>().fight().offence_started() >= 1);
                ASSERT_EQ(rig.as<StandardBot>().ledger().owner(weak), StandardBot::kFight);
                sim.get_unit(weak).hp = 3;                                                                                            // the ant is hurt in the fight
                rig.run(4);                                                                                                           // (the next look)
                const StandardBot& bot = rig.as<StandardBot>();
                ASSERT_EQ(bot.ledger().owner(weak) != StandardBot::kFight, health);                                                   // relieved (an ant with three hit points next to a Combat Ant)
                ASSERT_TRUE(bot.ledger().count(StandardBot::kFight) >= 3u);                                                           // and a fourth ant took its place
            }
        }
        {   // (d) a duel that cannot end in a kill ends with the first blow, as it did (workers against a fresh ant: the engine lets one blow land per hit clip and a worker's blow leaves an ant with one hit
            //     point, which walks home); a fight in which a kill is in reach (a Combat Ant among the defenders, an enemy of 4 hit points: two punches) waits out the hit clips of its target
            for (const bool combat : {false, true}) {
                sim::SimulationEngine sim;
                empty_field(sim, 75);
                const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27 + i % 2, 31 + i / 2});
                if (combat) sim.spawn_unit(0, sim::AntType::Combat, TileCoord{28, 29});
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{34, 30});
                sim.get_unit(enemy).hp = combat ? 4 : 10;
                LevelPlan p = plan;
                p.skirmish = false;
                p.hunt = false;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
                for (int t = 0; t < 300; ++t) {
                    keep_attacking(sim, 1, enemy, victim);
                    rig.tick();
                }
                ASSERT_TRUE(rig.as<StandardBot>().fight().fights_started() >= 1u);
                ASSERT_EQ(rig.as<StandardBot>().fight().clip_waits() > 0u, combat);
            }
        }
        {   // (e) a survivor with one hit point that walks home with a lead of two tiles is not chased: the fight is over and the ant is left alone for a while; one with more hit points is followed
            for (const uint16_t hp : {uint16_t{1}, uint16_t{5}}) {
                sim::SimulationEngine sim;
                empty_field(sim, 76);
                const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26 + i % 2, 30 + i / 2});
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{31, 30});
                sim.get_unit(enemy).hp = hp;
                LevelPlan p = plan;
                p.skirmish = false;
                p.hunt = false;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
                rig.run(2);
                sim.apply_command(command_of(CommandType::GroupMove, 1, {enemy}, 50, 30));                                                // it walks away
                sim.get_unit(victim).hp = 9;                                                                                              // a blow on the own ant: the fight begins
                rig.run(120);
                ASSERT_TRUE(rig.as<StandardBot>().fight().fights_started() >= 1u);
                ASSERT_EQ(rig.as<StandardBot>().fight().escapes() >= 1u, hp == 1);
            }
        }
    } TEST_END();

    TEST_CASE("AI20.4 Hunt The Kill (The Owner: \"I Saw Lots Of Opportunities Where It Could Have Killed An Ant, The Ant Was 4 HP And It Just Didn't Kill It\"): An Enemy With 4 Hit Points Within Reach Of A Combat Ant And An Idle Worker, Nothing Else Near, Is Attacked At Once, Every Level, By The Combat Ant Alone (Before The Fix No Attack Was Ordered: The Fights Began Only From A Blow On An Own Ant); Two Workers Cannot Kill It In The Engine (A Worker's Blow Leaves An Ant With One Hit Point, And It Walks Home) And Do Not Try; The Kills Are Made (4, 5, 6 Hit Points); A Fresh Enemy Of 10 Is Nobody's Hunt")
    {
        const auto world = [&](sim::SimulationEngine& sim, uint16_t enemy_hp, size_t workers, size_t combats, int32_t enemy_x = 30, uint32_t seed = 81) {
            empty_field(sim, seed);
            size_t n = 0;
            std::vector<uint32_t> ids;
            for (size_t i = 0; i < combats; ++i, ++n) ids.push_back(sim.spawn_unit(0, sim::AntType::Combat, TileCoord{27 + static_cast<int32_t>(n % 2), 30 + static_cast<int32_t>(n / 2)}));
            for (size_t i = 0; i < workers; ++i, ++n) ids.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27 + static_cast<int32_t>(n % 2), 30 + static_cast<int32_t>(n / 2)}));
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 8});                                  // (the last ants stay out of every fight: three more at home)
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{enemy_x, 30});
            sim.get_unit(enemy).hp = enemy_hp;
            return std::make_pair(enemy, ids);
        };
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            {   // (a) the owner's case: 4 hit points, a Combat Ant and an idle worker, nothing else near: the Combat Ant is ordered at the enemy (an even number of hit points is taken by punches alone), the
                //     worker waits; the same world with the rule off is today's: nothing is ordered
                sim::SimulationEngine sim;
                const auto w = world(sim, 4, 1, 1);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(120);
                const auto attacks = attacks_of(rig);
                ASSERT_TRUE(!attacks.empty());                                                                                        // an attack is ordered
                ASSERT_TRUE(attacks.front().second.ants.size() == 1 && attacks.front().second.ants[0] == w.second[0]);                // by the Combat Ant alone
                ASSERT_TRUE(attacks.front().second.tile_x == 30 && attacks.front().second.tile_y == 30);                              // at the enemy
                ASSERT_TRUE(sim.get_unit(w.first).hp < 4 || !alive(sim, w.first));                                                    // and it is hurt
                LevelPlan off = plan_for(level);
                off.hunt = false;
                off.skirmish = false;
                sim::SimulationEngine sim2;
                world(sim2, 4, 1, 1);
                Rig rig2(sim2, 0, level, std::make_unique<StandardBot>(off), 4, 4);
                rig2.run(120);
                ASSERT_EQ(rig2.proposed_count(CommandType::GroupAttack), 0u);
            }
            {   // (b) two idle workers cannot kill it (and three cannot kill 4 hit points either): no attack is ordered (the plan of the blows says no)
                for (const size_t workers : {size_t{2}, size_t{3}}) {
                    sim::SimulationEngine sim;
                    world(sim, 4, workers, 0);
                    Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                    rig.run(120);
                    ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
                }
            }
            {   // (c) a fresh enemy (10 hit points) is nobody's hunt, with whatever force (it stands four tiles from the Combat Ants: their reflex does not punch it)
                sim::SimulationEngine sim;
                world(sim, 10, 2, 2, 32);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(120);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
            }
        }
        {   // (c2) a plan that takes any number of blows (9) still leaves a fresh enemy to the fights that a blow begins: a hunt is of a WOUNDED ant
            sim::SimulationEngine sim;
            world(sim, 10, 2, 2, 32);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{34, 30});                           // (an own worker next to it puts it in the scope; the Combat Ants stand four tiles away: no reflex)
            LevelPlan plan = plan_for(Level::Hard);
            plan.hunt_blows = 9;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(120);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
            ASSERT_EQ(rig.as<StandardBot>().fight().hunts_started(), 0u);
        }
        {   // (d) the kills are made, with the orders of the hunt (the Combat Ant alone while the number of hit points is even, one worker once when it is odd): ants of 2, 3 and 4 hit points die with the
            //     plan as it ships (two blows); ants of 5 and 6 are nobody's hunt then, and die when the plan takes three blows
            for (const uint16_t hp : {uint16_t{2}, uint16_t{3}, uint16_t{4}, uint16_t{5}, uint16_t{6}}) {
                for (const bool three_blows : {false, true}) {
                    if (hp < 5 && three_blows) continue;
                    sim::SimulationEngine hunt;
                    const auto h = world(hunt, hp, 1, 1);
                    LevelPlan plan = plan_for(Level::Hard);
                    if (three_blows) plan.hunt_blows = 3;
                    Rig rig(hunt, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                    uint64_t guard = 0;
                    while (alive(hunt, h.first) && guard++ < 600) rig.run(1);
                    ASSERT_EQ(!alive(hunt, h.first), hp < 5 || three_blows);
                    ASSERT_EQ(rig.as<StandardBot>().fight().hunts_started(), hp < 5 || three_blows ? 1u : 0u);
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI20.5 The Choice, The Scope And The Release Of A Hunt: Of Two Killable Enemies The One That Dies Soonest (The More Wounded) Is Hunted First; A Hunt Is Kept Until The Target Is Dead (A Fresh Enemy That Steps In Is Not Attacked) And The Ants Are Free Afterwards; Not Against A Stronger Force Near The Target, Never With The Last Ants Or Ants That Carry; Per Level: The Blows That The Kill Takes (A Combat Ant Punches For 2, An Odd Number Of Hit Points Needs An Opener, Workers Alone Only Kill 2 Hit Points And Only Three Of Them) Against What A Plan Hunts (The Shipped Plans: Two Blows At Every Level); Easy Hunts What Stands Next To Its Ants, Medium Also What Is Within The Leash Of Its Hill Or At A Pile It Works, Hard Also The Carriers Of The Leader")
    {
        const auto world = [&](sim::SimulationEngine& sim, size_t workers, size_t combats, uint32_t seed = 82) {
            empty_field(sim, seed);
            size_t n = 0;
            std::vector<uint32_t> ids;
            for (size_t i = 0; i < combats; ++i, ++n) ids.push_back(sim.spawn_unit(0, sim::AntType::Combat, TileCoord{27 + static_cast<int32_t>(n % 2), 30 + static_cast<int32_t>(n / 2)}));
            for (size_t i = 0; i < workers; ++i, ++n) ids.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27 + static_cast<int32_t>(n % 2), 30 + static_cast<int32_t>(n / 2)}));
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 8});                                  // (the last ants stay out of every fight: three more at home)
            return ids;
        };
        const auto enemy_at = [&](sim::SimulationEngine& sim, int32_t x, int32_t y, uint16_t hp, uint8_t team = 1) {
            const uint32_t id = sim.spawn_unit(team, sim::AntType::Worker, TileCoord{x, y});
            sim.get_unit(id).hp = hp;
            return id;
        };
        {   // (a) two killable enemies (3 and 6 hit points) with the wounded one a little further away: the wounded one first (a worker opens: 3 is odd), the other is not touched meanwhile
            for (const Level level : {Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                const auto own = world(sim, 2, 1);
                enemy_at(sim, 29, 30, 6);
                const uint32_t hurt = enemy_at(sim, 31, 31, 3, 2);
                LevelPlan plan = plan_for(level);
                plan.hunt_blows = 3;                                                                                                   // (the 6 hit points are a kill of three blows: both are candidates)
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(40);
                const auto attacks = attacks_of(rig);
                ASSERT_TRUE(!attacks.empty());
                ASSERT_TRUE(attacks.front().second.tile_x == 31 && attacks.front().second.tile_y == 31);
                ASSERT_TRUE(attacks.front().second.ants.size() == 1 && attacks.front().second.ants[0] != own[0]);                     // (the opener: a worker, not the Combat Ant)
                ASSERT_TRUE(!alive(sim, hurt) || sim.get_unit(hurt).hp < 3);
            }
        }
        {   // (a2) the same number of blows (three, set by hand), the more wounded enemy (5 hit points) a tile further from the Combat Ant than the other (6), in the scope of an own worker next to it: the
            //      wounded one first (a tile of distance is worth less than a hit point)
            sim::SimulationEngine sim;
            world(sim, 2, 1);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{33, 31});
            enemy_at(sim, 31, 30, 6);
            enemy_at(sim, 32, 30, 5, 2);
            LevelPlan plan = plan_for(Level::Hard);
            plan.hunt_blows = 3;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(40);
            const auto attacks = attacks_of(rig);
            ASSERT_TRUE(!attacks.empty());
            ASSERT_TRUE(attacks.front().second.tile_x == 32 && attacks.front().second.tile_y == 30);
        }
        {   // (b) a kill that is made: the enemy dies, the ants are free again and the counters say one kill was available, hunted and made
            sim::SimulationEngine sim;
            world(sim, 1, 1);
            const uint32_t target = enemy_at(sim, 31, 30, 4);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
            uint64_t guard = 0;
            while (alive(sim, target) && guard++ < 600) rig.run(1);
            ASSERT_TRUE(!alive(sim, target));
            rig.run(60);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_EQ(bot.fight().fights(), 0u);                                                                                       // released: no fight is left
            ASSERT_EQ(bot.ledger().count(StandardBot::kFight), 0u);
            ASSERT_TRUE(bot.fight().hunt_available() >= 1u);
            ASSERT_EQ(bot.fight().hunts_started(), 1u);
            ASSERT_EQ(bot.fight().hunts_killed(), 1u);
        }
        {   // (b2) the hunt is kept on its target: three workers hunt an enemy of 2 hit points (the one kill that workers alone can make); a fresh enemy (10 hit points, more than anybody hunts) steps in next to
            //      the ants and is never attacked
            sim::SimulationEngine sim;
            world(sim, 3, 0);
            enemy_at(sim, 31, 30, 2);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
            rig.run(8);
            ASSERT_EQ(rig.as<StandardBot>().fight().hunts_started(), 1u);
            const uint32_t fresh = enemy_at(sim, 29, 32, 10, 2);
            rig.run(150);
            ASSERT_EQ(sim.get_unit(fresh).hp, 10u);
            ASSERT_EQ(rig.as<StandardBot>().fight().hunts_started(), 1u);
            for (const auto& e : attacks_of(rig)) ASSERT_TRUE(!(e.second.tile_x == 29 && e.second.tile_y == 32));
            ASSERT_TRUE(attacks_of(rig).size() >= 2);                                                                                  // (the target was struck more than once)
        }
        {   // (c) a stronger force near the target: three enemy Combat Ants stand three tiles from a 4-hit-point worker: no hunt (the same world without them hunts); with a plan that takes any odds it goes
            for (const int variant : {0, 1, 2}) {
                sim::SimulationEngine sim;
                world(sim, 1, 1);
                enemy_at(sim, 31, 30, 4);
                if (variant >= 1) {
                    for (int i = 0; i < 3; ++i) sim.spawn_unit(2, sim::AntType::Combat, TileCoord{34, 29 + i});
                }
                LevelPlan plan = plan_for(Level::Hard);
                if (variant == 2) plan.hunt_odds_percent = 1;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(60);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack) >= 1, variant != 1);
            }
        }
        {   // (d) the last ants and ants that carry: with two ants (the reserve) none is taken; ants that carry food are no fighters
            {
                sim::SimulationEngine sim;
                empty_field(sim, 83);
                sim.spawn_unit(0, sim::AntType::Combat, TileCoord{27, 30});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{28, 30});
                enemy_at(sim, 30, 30, 4);
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
                rig.run(120);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
                ASSERT_EQ(rig.as<StandardBot>().fight().hunts_started(), 0u);                                                           // (not even begun)
            }
            {
                sim::SimulationEngine sim;
                world(sim, 1, 1);
                for (const uint32_t ant : ants_of(sim, 0)) sim.get_unit(ant).pick_up_food(1, 25);                                      // (every ant of the field carries: nobody can fight)
                enemy_at(sim, 30, 30, 4);
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
                rig.run(120);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
                ASSERT_EQ(rig.as<StandardBot>().fight().hunts_started(), 0u);
            }
        }
        {   // (e0) the plan of the blows, as a table: (hit points, Combat Ants, other ants) -> a kill is possible, the blows it takes, whether an other ant opens
            struct Case { uint32_t hp; size_t combats; size_t others; bool possible; uint32_t blows; bool opener; };
            const Case cases[] = {{0, 1, 1, false, 0, false}, {1, 0, 0, false, 1, false}, {1, 0, 1, true, 1, false}, {1, 1, 0, true, 1, false}, {2, 0, 2, false, 0, false}, {2, 0, 3, true, 2, false},
                                  {3, 0, 9, false, 0, false}, {4, 0, 9, false, 0, false}, {2, 1, 0, true, 1, false}, {3, 1, 0, false, 0, false}, {3, 1, 1, true, 2, true}, {4, 1, 0, true, 2, false},
                                  {5, 2, 1, true, 3, true}, {6, 1, 1, true, 3, false}, {7, 1, 1, true, 4, true}, {8, 2, 0, true, 4, false}, {9, 1, 1, true, 5, true}, {10, 1, 1, true, 5, false}};
            for (const Case& k : cases) {
                const KillPlan plan = kill_plan(k.hp, k.combats, k.others);
                ASSERT_EQ(plan.possible, k.possible);
                if (k.possible) {
                    ASSERT_EQ(plan.blows, k.blows);
                    ASSERT_EQ(plan.opener, k.opener);
                }
            }
        }
        {   // (e) the blows that the kill takes against what a plan hunts (the thresholds that were tried, 2, 3 and 4 blows, set by hand: the shipped plans hunt two blows at every level, (e2)) and the force
            //     of the level (Easy 2 ants, Medium and Hard 3): a Combat Ant punches for 2 (an even
            //     number of hit points takes h / 2 punches, an odd one an opener and (h - 1) / 2 punches, and no opener means no plan), workers alone only kill an ant of 2 hit points, three of them
            struct Row { uint16_t hp; size_t workers; size_t combats; bool easy; bool medium; bool hard; };
            const Row rows[] = {{4, 1, 1, true, true, true}, {5, 1, 1, false, true, true}, {6, 1, 1, false, true, true}, {7, 1, 1, false, false, true}, {8, 1, 1, false, false, true},
                                {9, 1, 1, false, false, false}, {10, 1, 1, false, false, false}, {2, 3, 0, false, true, true}, {4, 3, 0, false, false, false}, {4, 2, 0, false, false, false},
                                {4, 0, 2, true, true, true}, {2, 0, 2, true, true, true}};
            for (const Row& row : rows) {
                const bool want[3] = {row.easy, row.medium, row.hard};
                for (size_t li = 0; li < 3; ++li) {
                    const Level level = li == 0 ? Level::Easy : li == 1 ? Level::Medium : Level::Hard;
                    sim::SimulationEngine sim;
                    world(sim, row.workers, row.combats);
                    enemy_at(sim, 30, 30, row.hp);
                    LevelPlan plan = plan_for(level);
                    plan.hunt_blows = li == 0 ? 2u : li == 1 ? 3u : 4u;
                    Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                    rig.run(60);
                    ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack) >= 1, want[li]);
                }
            }
        }
        {   // (e2) the shipped plans: two blows at every level, so a Combat Ant and a worker hunt an enemy of 4 hit points and nothing above it (5 or more hit points are three blows or more: a hunt that mostly
            //      chases an ant that walks on, docs/BOTS.md), at Easy too
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                ASSERT_EQ(plan_for(level).hunt_blows, 2u);
                for (const uint16_t hp : {uint16_t{4}, uint16_t{5}, uint16_t{6}, uint16_t{8}}) {
                    sim::SimulationEngine sim;
                    world(sim, 1, 1);
                    enemy_at(sim, 30, 30, hp);
                    Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                    rig.run(60);
                    ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack) >= 1, hp == 4);
                }
            }
        }
        {   // (f) the scope: an enemy worker of 4 hit points 6 and 7 tiles from a Combat Ant and a worker (beyond the three tiles of Easy's scope), within the leash of the own hill: Medium and Hard hunt it, Easy does not
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                empty_field(sim, 84);
                sim.spawn_unit(0, sim::AntType::Combat, TileCoord{5, 12});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6, 12});
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 2});
                enemy_at(sim, 12, 8, 4);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(60);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack) >= 1, level != Level::Easy);
            }
        }
        {   // (g) the carrier of the leading team far from the own hill: only Hard hunts it
            for (const Level level : {Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                empty_field(sim, 85);
                sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 30});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{31, 30});
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 2});
                const uint32_t carrier = carrier_at(sim, 1, TileCoord{37, 30});
                sim.get_unit(carrier).hp = 4;
                sim.set_player_score(1, 3000);
                LevelPlan plan = plan_for(level);
                plan.catchup = false;                                                                                                  // (no food on this field: 3000 points behind is tier 3, where the odds of a hunt are lowest: AI20.9)
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(60);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack) >= 1, level == Level::Hard);
            }
        }
        {   // (h) a worker at a pile that the own ants work (4 hit points, 5 tiles from them, within 4 of the pile): Medium and Hard hunt it, Easy does not
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                empty_field(sim, 86);
                add_pile(sim, 40, 40, 40, 25);
                sim.spawn_unit(0, sim::AntType::Combat, TileCoord{35, 38});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{36, 38});
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 2});
                enemy_at(sim, 42, 41, 4);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(60);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack) >= 1, level != Level::Easy);
            }
        }
    } TEST_END();

    TEST_CASE("AI20.6 The Fire-In Is Safe (The Owner: \"The Hard Bot Sent One Fire Ant To The Enemy Gate That Was Put Out At Once\"): A Lone Fire Ant Is Not Sent To A Hill Whose Team Has A Fire Ant In Sight Or A Fire Power-Up It Can Still Take; It Is Sent When Neither Is There; A Fire Ant With Two Combat Ants That Hold The Entrance Is Sent Though The Enemy Could Put The Fire Out; Walls Put Out Twice End The Attempt For A While")
    {
        LevelPlan plan = plan_for(Level::Hard);
        plan.sabotage = true;
        plan.sabotage_after = 20;
        plan.sabotage_spare_keeper = false;                                                                                           // (the lone Fire Ant is free to go: the keeper of the own walls is not spared in this plan)
        sim::SimulationEngine probe;
        empty_field(probe, 90);
        const MapInfo pmap(probe);
        const HillInfo& hill1 = pmap.hill(1);
        const std::array<TileCoord, 8> ring = SabotageTask::ring_of(hill1);
        const TileCoord station = hill1.origin.y >= 5 ? TileCoord{hill1.origin.x + 1, hill1.origin.y - 4} : TileCoord{hill1.origin.x + 1, hill1.origin.y + 6};
        const auto lit = [&](const sim::SimulationEngine& sim) {
            size_t n = 0;
            for (const TileCoord& t : ring) n += sim.grid().has_fire_at(t) ? 1u : 0u;
            return n;
        };
        // team 1 leads with 400 points; one Fire Ant (and `combats` Combat Ants) of the bot far from its gate; `enemy_fire`: a Fire Ant of team 1 in sight; `powerup`: a Fire power-up on the field
        struct World {
            uint32_t enemy_fire{0};
        };
        const auto build = [&](sim::SimulationEngine& sim, size_t combats, bool enemy_fire, bool powerup) {
            World w;
            empty_field(sim, 90);
            sim.set_player_score(1, 400);
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 30});
            for (size_t i = 0; i < combats; ++i) sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30 + static_cast<int32_t>(i), 33});
            if (enemy_fire) w.enemy_fire = sim.spawn_unit(1, sim::AntType::Fire, TileCoord{46, 14});
            if (powerup) sim.grid_mut().place_powerup(40, 20, 2);
            return w;
        };
        {   // (a) nothing can put the fire out: the lone Fire Ant lights the ring
            sim::SimulationEngine sim;
            build(sim, 0, false, false);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(1500);
            ASSERT_TRUE(rig.as<StandardBot>().sabotage().walls_ordered() >= 6);
            ASSERT_TRUE(lit(sim) >= 6);
            ASSERT_EQ(rig.as<StandardBot>().sabotage().refused_safe(), 0u);
        }
        {   // (b) an enemy Fire Ant in sight, or a Fire power-up that the enemy can still take: the lone Fire Ant stays at home (the looks that refused are counted), the ring stays dark
            for (const int variant : {0, 1}) {
                sim::SimulationEngine sim;
                build(sim, 0, variant == 0, variant == 1);
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(1500);
                ASSERT_EQ(rig.as<StandardBot>().sabotage().walls_ordered(), 0u);
                ASSERT_EQ(lit(sim), 0u);
                ASSERT_TRUE(rig.as<StandardBot>().sabotage().refused_safe() >= 1u);
            }
            LevelPlan unsafe = plan;
            unsafe.sabotage_safe = false;                                                                                             // (the rule off: the plan of v0.5.0 sends it)
            sim::SimulationEngine sim;
            build(sim, 0, true, false);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(unsafe), 4, 4);
            rig.run(1500);
            ASSERT_TRUE(lit(sim) >= 6);
        }
        {   // (c) the enemy could put the fire out, but two Combat Ants hold the entrance: they go to the gate first (the first wall waits for them) and the ring is lit; they stay a while afterwards
            for (const int variant : {0, 1}) {
                sim::SimulationEngine sim;
                build(sim, 2, variant == 0, variant == 1);
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                uint64_t guard = 0;
                while (rig.as<StandardBot>().sabotage().walls_ordered() < 1 && guard++ < 1500) rig.run(1);
                ASSERT_TRUE(rig.as<StandardBot>().sabotage().walls_ordered() >= 1);
                ASSERT_EQ(rig.as<StandardBot>().sabotage().escorts(), 2u);
                for (const sim::AntSnapshot& a : sim.get_world_state().ants) {                                                       // when the first wall is ordered both escorts stand at the entrance
                    if (a.player_id == 0 && a.raw_type == sim::AntType::Combat) ASSERT_TRUE(tc(a.tile_x, a.tile_y).chebyshev_dist(station) <= 3);
                }
                rig.run(900);
                ASSERT_TRUE(lit(sim) >= 6);
                rig.run(2000);                                                                                                       // the ring stands, the escorts held it for sabotage_escort_ticks: they are let go ...
                ASSERT_TRUE(lit(sim) >= 6);
                ASSERT_EQ(rig.as<StandardBot>().sabotage().escorts(), 0u);
                ASSERT_EQ(rig.as<StandardBot>().sabotage().escorts_called(), 2u);                                                    // ... and not called to the entrance again and again
            }
            // one Combat Ant is no force: no fire-in
            sim::SimulationEngine one;
            build(one, 1, true, false);
            Rig rig1(one, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig1.run(1500);
            ASSERT_EQ(lit(one), 0u);
            ASSERT_TRUE(rig1.as<StandardBot>().sabotage().refused_safe() >= 1u);
        }
        {   // (c2) no Fire Ant, nobody to escort: two Combat Ants of the bot stay where they are (nobody is called to an enemy gate for a fire-in that cannot be made)
            sim::SimulationEngine sim;
            empty_field(sim, 90);
            sim.set_player_score(1, 400);
            for (int i = 0; i < 2; ++i) sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30 + i, 33});
            sim.spawn_unit(1, sim::AntType::Fire, TileCoord{46, 14});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(600);
            ASSERT_EQ(rig.as<StandardBot>().sabotage().escorts(), 0u);
            ASSERT_EQ(rig.as<StandardBot>().sabotage().escorts_called(), 0u);
            for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                if (a.player_id == 0 && a.raw_type == sim::AntType::Combat) ASSERT_TRUE(tc(a.tile_x, a.tile_y).chebyshev_dist(station) > 10);
            }
        }
        {   // (d) when the enemy's Fire Ant is dead and no power-up is left to take, the lone Fire Ant goes: a Fire Ant that dies drops its power-up (the enemy can take it: no fire-in until it is gone)
            sim::SimulationEngine sim;
            const World w = build(sim, 0, true, false);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(600);
            ASSERT_EQ(rig.as<StandardBot>().sabotage().walls_ordered(), 0u);
            sim.kill_unit(w.enemy_fire);
            rig.run(600);
            ASSERT_EQ(rig.as<StandardBot>().sabotage().walls_ordered(), 0u);                                                           // (its drop is a Fire power-up on the field)
            size_t drops = 0;
            for (int32_t y = 0; y < 60; ++y) {
                for (int32_t x = 0; x < 60; ++x) {
                    if (sim.grid().has_powerup_at(TileCoord{x, y})) {
                        sim.grid_mut().clear_powerup(x, y);                                                                            // (somebody took it)
                        ++drops;
                    }
                }
            }
            ASSERT_EQ(drops, 1u);
            rig.run(1500);
            ASSERT_TRUE(lit(sim) >= 6);
        }
        {   // (e) walls that are put out twice end the attempt for a while: two of the lit walls are put out (as an enemy Fire Ant would), the team is left alone for sabotage_giveup_ticks and tried again after
            LevelPlan p = plan;
            p.sabotage_giveup_ticks = 400;
            sim::SimulationEngine sim;
            build(sim, 0, false, false);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
            uint64_t guard = 0;
            while (lit(sim) < 4 && guard++ < 1500) rig.run(1);
            ASSERT_TRUE(lit(sim) >= 4);
            rig.run(12);                                                                                                              // (the walls are seen burning)
            size_t cleared = 0;
            for (const TileCoord& t : ring) {
                if (cleared < 2 && sim.grid().has_fire_at(t)) {
                    sim.grid_mut().clear_firewall(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y));
                    ++cleared;
                }
            }
            ASSERT_EQ(cleared, 2u);
            rig.run(12);
            ASSERT_TRUE(rig.as<StandardBot>().sabotage().put_out() >= 2u);
            ASSERT_TRUE(rig.as<StandardBot>().sabotage().gave_up(1, sim.current_tick()));
            const uint32_t before = rig.as<StandardBot>().sabotage().walls_ordered();
            rig.run(300);
            ASSERT_EQ(rig.as<StandardBot>().sabotage().walls_ordered(), before);                                                      // left alone meanwhile
            rig.run(400);
            ASSERT_TRUE(rig.as<StandardBot>().sabotage().walls_ordered() > before);                                                   // and tried again afterwards
        }
    } TEST_END();

    TEST_CASE("AI20.7 A Bot Whose Entrance Is Fired In Goes After The Fire Ant (The Owner: \"The Other Team Just Extinguishes All Of The Fire, It Does Not Try To Kill The Fire Ant That Is Firing Them In\"): The Fighters Attack The Enemy Fire Ant Near The Ring Round Its Gate (A Wounded One First); The Walls Of The Ring Are Not Put Out While That Ant Is Near Or An Enemy Force That Is Stronger Than The Own Stands Near Them, Never From A Tile That An Ant Stands On; When The Fire Ant Is Dead The Keeper Puts Them Out")
    {
        sim::SimulationEngine probe;
        empty_field(probe, 91);
        const MapInfo pmap(probe);
        const HillInfo& hill0 = pmap.hill(0);
        const std::array<TileCoord, 8> ring = SabotageTask::ring_of(hill0);
        // the bot (seat 0) with its keeper (a Fire Ant) and five workers near its hill, three walls of the ring lit by team 1 and the enemy Fire Ant that lit them standing `fire_ant_at` tiles away
        struct World {
            uint32_t fire_ant{0};
            uint32_t keeper{0};
        };
        const auto build = [&](sim::SimulationEngine& sim, TileCoord fire_ant_at) {
            World w;
            empty_field(sim, 91);
            w.keeper = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{8, 10});
            for (int i = 0; i < 5; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9 + i, 12});
            for (int i = 0; i < 3; ++i) sim.grid_mut().place_firewall(static_cast<uint32_t>(ring[static_cast<size_t>(i)].x), static_cast<uint32_t>(ring[static_cast<size_t>(i)].y), 1);
            w.fire_ant = sim.spawn_unit(1, sim::AntType::Fire, fire_ant_at);
            return w;
        };
        const auto extinguish_orders = [&](const Rig& rig) {
            size_t n = 0;
            for (const auto& e : rig.proposed) {
                if (e.second.type != CommandType::GroupSpecial) continue;
                for (const TileCoord& t : ring) n += e.second.tile_x == t.x && e.second.tile_y == t.y ? 1u : 0u;
            }
            return n;
        };
        const auto lit = [&](const sim::SimulationEngine& sim) {
            size_t n = 0;
            for (const TileCoord& t : ring) n += sim.grid().has_fire_at(t) ? 1u : 0u;
            return n;
        };
        {   // (a) the Fire Ant stands 7 tiles from the gate: the fighters go after it (a hunt of its own kind), the walls are not put out meanwhile
            sim::SimulationEngine sim;
            const World w = build(sim, TileCoord{14, 6});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
            rig.run(60);
            ASSERT_EQ(rig.as<StandardBot>().fight().fire_hunts(), 1u);
            ASSERT_TRUE(rig.as<StandardBot>().fight().hunting(w.fire_ant));
            const auto attacks = attacks_of(rig);
            ASSERT_TRUE(!attacks.empty());
            ASSERT_TRUE(attacks.front().second.tile_x == 14 && attacks.front().second.tile_y == 6);
            ASSERT_EQ(extinguish_orders(rig), 0u);
            // the rule off (the plan of v0.5.0): no hunt, and the keeper puts the walls out at once
            LevelPlan off = plan_for(Level::Hard);
            off.fire_defence = false;
            sim::SimulationEngine sim2;
            build(sim2, TileCoord{14, 6});
            Rig rig2(sim2, 0, Level::Hard, std::make_unique<StandardBot>(off), 4, 4);
            rig2.run(300);
            ASSERT_EQ(rig2.as<StandardBot>().fight().fire_hunts(), 0u);
            ASSERT_TRUE(extinguish_orders(rig2) >= 1u);
        }
        {   // (b) the Fire Ant is dead: the keeper puts the walls out (and the fighters are free again)
            sim::SimulationEngine sim;
            const World w = build(sim, TileCoord{14, 6});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
            rig.run(60);
            ASSERT_EQ(extinguish_orders(rig), 0u);
            sim.kill_unit(w.fire_ant);
            for (int32_t y = 0; y < 60; ++y) {
                for (int32_t x = 0; x < 60; ++x) {
                    if (sim.grid().has_powerup_at(TileCoord{x, y})) sim.grid_mut().clear_powerup(x, y);                              // (the drop of the Fire Ant)
                }
            }
            rig.run(500);
            ASSERT_TRUE(extinguish_orders(rig) >= 1u);
            ASSERT_TRUE(lit(sim) < 3u);
            ASSERT_EQ(rig.as<StandardBot>().fight().fights(), 0u);
        }
        {   // (c) an enemy force that is stronger than the own stands near the walls (three Combat Ants five tiles from them, none of our ants near): the walls are not put out, with or without a Fire Ant;
            //     with the force gone they are
            sim::SimulationEngine sim;
            empty_field(sim, 91);
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{20, 20});
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22 + i, 22});
            for (int i = 0; i < 3; ++i) sim.grid_mut().place_firewall(static_cast<uint32_t>(ring[static_cast<size_t>(i)].x), static_cast<uint32_t>(ring[static_cast<size_t>(i)].y), 1);
            std::vector<uint32_t> force;
            for (int i = 0; i < 3; ++i) force.push_back(sim.spawn_unit(1, sim::AntType::Combat, TileCoord{ring[0].x + 5, ring[0].y + 1 + i}));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
            rig.run(200);
            ASSERT_EQ(extinguish_orders(rig), 0u);
            for (const uint32_t id : force) sim.kill_unit(id);
            rig.run(600);
            ASSERT_TRUE(extinguish_orders(rig) >= 1u);
        }
        {   // (d) the Fire Ant stands ON a lit tile of the ring: no order ever names that tile while it stands there (a click on an ant is no special order)
            sim::SimulationEngine sim;
            empty_field(sim, 91);
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{8, 10});
            for (int i = 0; i < 5; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9 + i, 12});
            sim.grid_mut().place_firewall(static_cast<uint32_t>(ring[0].x), static_cast<uint32_t>(ring[0].y), 1);
            sim.spawn_unit(1, sim::AntType::Fire, ring[0]);
            LevelPlan p = plan_for(Level::Hard);
            p.fire_defence = false;                                                                                                   // (the walls would be put out at once: the occupant is what holds the order back)
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
            rig.run(300);
            for (const auto& e : rig.proposed) ASSERT_TRUE(!(e.second.type == CommandType::GroupSpecial && e.second.tile_x == ring[0].x && e.second.tile_y == ring[0].y));
        }
    } TEST_END();

    TEST_CASE("AI20.8 No Task Orders A Special Order At A Tile That An Ant Stands On (The Owner: \"An Ant Standing On The Tile Cannot Be Targeted For Extinguish, Defuse, Plant, Ignite, Bridge Or Demolish\"): A Whole Run Of The Raid, The Sabotage And The Walls With An Ant (Enemy Or Own) On The Tile They Want Has No Order At An Occupied Tile; The Raid Takes A Free Tile Of The Mound, The Keeper Steps Aside; The Simulation Is Not Changed")
    {
        // runs `ticks` ticks of a rig and counts the special orders that the bot proposed at a tile with an ant on it at the moment of the look (the view of the look is the state after the tick)
        const auto run_checked = [&](Rig& rig, const sim::SimulationEngine& sim, uint64_t ticks) {
            size_t violations = 0;
            for (uint64_t i = 0; i < ticks; ++i) {
                const size_t before = rig.proposed.size();
                rig.tick();
                for (size_t k = before; k < rig.proposed.size(); ++k) {
                    const Command& c = rig.proposed[k].second;
                    if (c.type != CommandType::GroupSpecial) continue;
                    for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                        if (a.tile_x == c.tile_x && a.tile_y == c.tile_y && a.hp > 0 && a.state != sim::UnitState::Dead && a.state != sim::UnitState::Drowning) ++violations;
                    }
                }
            }
            return violations;
        };
        {   // (a) the raid: an enemy ant stands on the entrance of the hill that the thief is to raid: the order names another tile of the mound (any tile of it is a raid click), never the occupied one
            for (const bool occupied_entrance : {false, true}) {
                sim::SimulationEngine sim;
                empty_field(sim, 92);
                sim.set_player_score(1, 300);
                sim.spawn_unit(0, sim::AntType::Thief, TileCoord{30, 10});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 10});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{11, 10});
                const MapInfo map(sim);
                const HillInfo& hill1 = map.hill(1);
                if (occupied_entrance) sim.spawn_unit(2, sim::AntType::Worker, hill1.entrance);
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
                ASSERT_EQ(run_checked(rig, sim, 60), 0u);
                ASSERT_EQ(rig.as<StandardBot>().raids().raids_ordered(), 1u);
                size_t at_entrance = 0;
                size_t on_mound = 0;
                for (const auto& e : rig.proposed) {
                    if (e.second.type != CommandType::GroupSpecial) continue;
                    at_entrance += e.second.tile_x == hill1.entrance.x && e.second.tile_y == hill1.entrance.y ? 1u : 0u;
                    on_mound += e.second.tile_x >= hill1.origin.x && e.second.tile_x <= hill1.origin.x + 3 && e.second.tile_y >= hill1.origin.y && e.second.tile_y <= hill1.origin.y + 3 ? 1u : 0u;
                }
                ASSERT_TRUE(on_mound >= 1u);
                ASSERT_EQ(at_entrance >= 1u, !occupied_entrance);
            }
        }
        {   // (b) the sabotage: an enemy worker stands on a tile of the ring: it is never named, the seven others are lit
            sim::SimulationEngine sim;
            empty_field(sim, 93);
            sim.set_player_score(1, 400);
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 30});
            const MapInfo map(sim);
            const std::array<TileCoord, 8> ring = SabotageTask::ring_of(map.hill(1));
            sim.spawn_unit(1, sim::AntType::Worker, ring[0]);
            LevelPlan plan = plan_for(Level::Hard);
            plan.sabotage = true;
            plan.sabotage_after = 20;
            plan.sabotage_spare_keeper = false;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            ASSERT_EQ(run_checked(rig, sim, 1500), 0u);
            size_t lit = 0;
            for (const TileCoord& t : ring) lit += sim.grid().has_fire_at(t) ? 1u : 0u;
            ASSERT_TRUE(lit >= 6u && !sim.grid().has_fire_at(ring[0]));
        }
        {   // (c) the walls of the own thief hole: the keeper stands on the tile that is to be lit (a Thief of the enemy is in sight): it steps aside first, then all three walls stand
            sim::SimulationEngine sim;
            empty_field(sim, 94);
            const MapInfo map(sim);
            const std::array<TileCoord, 3> east = east_tiles(map.hill(0));
            sim.spawn_unit(0, sim::AntType::Fire, east[0]);
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 10});
            sim.spawn_unit(2, sim::AntType::Thief, TileCoord{30, 40});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
            ASSERT_EQ(run_checked(rig, sim, 700), 0u);
            ASSERT_EQ(walls_east(sim, kFightHills[0]), 3u);
        }
        {   // (d) the bombs: a bomb of the enemy near the own hill with an enemy ant on the tile next to it: the Bomber defuses the bomb (nothing stands on its tile) and never names an occupied tile
            sim::SimulationEngine sim;
            empty_field(sim, 95);
            sim.grid_mut().place_bomb(12, 12, 1);
            sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{9, 9});
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 14});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{13, 12});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
            ASSERT_EQ(run_checked(rig, sim, 600), 0u);
        }
    } TEST_END();

    TEST_CASE("AI20.9 Behind The Leader And The Endgame (The Owner: \"The Losing Player Should Be A Little More Aggressive\"): The Pressure Is The Deficit In Percent Of What Can Still Be Earned, Its Tiers Come Later The Weaker The Level (Hard 40, 80 And 130 Percent, Medium 1.5 Times, Easy 2.5 Times); The Old Margin Of 150 Points Never Fires In A Close Match, The Pressure Does; A Bot Behind Strikes At Tier 1 With Its Combat Ants, Workers Join Only Where The Plan Lets Them (Never In The Shipped Plans), Ahead It Does Not; Easy Raids From Tier 2; In The Last Minute The Leader Guards (No Offence, One Defender More) And The Bot Behind Is All-In (Tier 3, The Strike Does Not Stop)")
    {
        // a field with plenty of food (10,000 points: the time is what limits what can be earned), the scores and the time of the case
        const auto build = [&](sim::SimulationEngine& sim, int32_t mine, int32_t leader, uint32_t ticks, uint64_t at, uint32_t seed = 110) {
            empty_field(sim, seed, ticks);
            add_pile(sim, 30, 30, 400, 25);
            sim.set_player_score(0, mine);
            sim.set_player_score(1, leader);
            for (uint8_t t = 0; t < 4; ++t) sim.spawn_unit(t, sim::AntType::Worker, TileCoord{kFightHills[t].x + 3, kFightHills[t].y + 6});
            tick_all(sim, at);
        };
        const auto standing = [&](const sim::SimulationEngine& sim, Level level) {
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            return standing_of(plan_for(level), view, map);
        };
        {   // (a) the tiers: at tick 7200 of 14400 there are 7200 ticks left, 1152 points to earn; the deficit in percent of that, from the tiers of the level
            struct Row { int32_t deficit; uint8_t easy; uint8_t medium; uint8_t hard; };
            const Row rows[] = {{300, 0, 0, 0}, {600, 0, 0, 1}, {1000, 0, 1, 2}, {1600, 1, 2, 3}, {2500, 2, 3, 3}};
            for (const Row& row : rows) {
                sim::SimulationEngine sim;
                build(sim, 100, 100 + row.deficit, 14400, 7200);
                ASSERT_EQ(standing(sim, Level::Easy).tier, row.easy);
                ASSERT_EQ(standing(sim, Level::Medium).tier, row.medium);
                ASSERT_EQ(standing(sim, Level::Hard).tier, row.hard);
                ASSERT_EQ(standing(sim, Level::Hard).deficit, row.deficit);
            }
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {                  // the thresholds: the weaker the level, the later; 1.5 and 2.5 times Hard's
                const LevelPlan p = plan_for(level);
                ASSERT_TRUE(p.catchup && p.endgame);
                ASSERT_TRUE(p.catchup_tier1 < p.catchup_tier2 && p.catchup_tier2 < p.catchup_tier3);
            }
            ASSERT_TRUE(plan_for(Level::Hard).catchup_tier1 == 40u && plan_for(Level::Medium).catchup_tier1 == 60u && plan_for(Level::Easy).catchup_tier1 == 100u);
            // a leader below 300 points is no reason (nothing worth a trip), and ahead is no pressure
            sim::SimulationEngine poor;
            build(poor, 0, 250, 14400, 7200);
            ASSERT_EQ(standing(poor, Level::Hard).tier, 0u);
            sim::SimulationEngine ahead;
            build(ahead, 900, 500, 14400, 7200);
            ASSERT_EQ(standing(ahead, Level::Hard).tier, 0u);
            ASSERT_EQ(standing(ahead, Level::Hard).pressure, 0u);
            ASSERT_TRUE(standing(ahead, Level::Hard).ahead);
        }
        {   // (b) the old trigger against the pressure: late in a close match (leader 900, the bot 780, 1,400 ticks left: 224 points to earn) the margin of 150 points is not reached, 120 points are 53 percent
            sim::SimulationEngine sim;
            build(sim, 780, 900, 14400, 13000);
            const Standing st = standing(sim, Level::Hard);
            ASSERT_FALSE(st.behind);
            ASSERT_EQ(st.tier, 1u);
            ASSERT_TRUE(st.pressure >= 40u && st.pressure < 80u);
        }
        {   // (c) a bot behind strikes: Combat Ants of the bot near the leader's hill with its carriers near the queue; tier 1 at Hard and Medium (the leader is 800 points ahead at tick 7200), none at Easy
            //     (tier 0), none when the bot is not behind enough, none when the rule is off
            struct Case { Level level; int32_t deficit; bool catchup; bool want; };
            const Case cases[] = {{Level::Hard, 800, true, true}, {Level::Medium, 800, true, true}, {Level::Easy, 800, true, false}, {Level::Hard, 100, true, false}, {Level::Hard, 800, false, false}};
            for (const Case& k : cases) {
                sim::SimulationEngine sim;
                build(sim, 100, 100 + k.deficit, 14400, 0);
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Combat, TileCoord{44 + i, 14});
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 8});
                for (int i = 0; i < 2; ++i) carrier_at(sim, 1, TileCoord{46 + i, 8});
                tick_all(sim, 7200);
                sim.set_player_score(0, 100);
                sim.set_player_score(1, 100 + k.deficit);
                LevelPlan plan = plan_for(k.level);
                plan.catchup = k.catchup;
                plan.skirmish = false;                                                                                                 // (the skirmish of Hard would take the same ants for the same carriers: AI20.2)
                Rig rig(sim, 0, k.level, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(120);
                ASSERT_EQ(rig.as<StandardBot>().strike().strikes_started() >= 1u, k.want);
            }
        }
        {   // (d) workers join the strike only where the plan lets them (catchup_workers_tier): with 2 a bot with no Combat Ant strikes with its workers at tier 2 (the deficit of 1000 at tick 7200 at Hard)
            //     and not at tier 1 (600); the shipped plans never do (a worker kills nothing above two hit points: docs/BOTS.md), not even at tier 3 (2500)
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_EQ(plan_for(level).catchup_workers_tier, 4u);
            struct Case { int32_t deficit; bool workers_from_2; bool want; };
            const Case cases[] = {{600, true, false}, {1000, true, true}, {1000, false, false}, {2500, false, false}};
            for (const Case& k : cases) {
                sim::SimulationEngine sim;
                build(sim, 100, 100 + k.deficit, 14400, 0);
                for (int i = 0; i < 7; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44 + i % 3, 14 + i / 3});
                for (int i = 0; i < 2; ++i) carrier_at(sim, 1, TileCoord{46 + i, 8});
                tick_all(sim, 7200);
                sim.set_player_score(0, 100);
                sim.set_player_score(1, 100 + k.deficit);
                LevelPlan plan = plan_for(Level::Hard);
                plan.skirmish = false;
                if (k.workers_from_2) plan.catchup_workers_tier = 2;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(120);
                ASSERT_EQ(rig.as<StandardBot>().strike().strikes_started() >= 1u, k.want);
            }
        }
        {   // (e) Easy raids from tier 2 (a Thief of its own, the leader 2,500 points ahead at tick 7200: tier 2 at Easy) and not at tier 0
            for (const int32_t deficit : {300, 2500}) {
                sim::SimulationEngine sim;
                build(sim, 100, 100 + deficit, 14400, 0);
                sim.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
                tick_all(sim, 7200);
                sim.set_player_score(0, 100);
                sim.set_player_score(1, 100 + deficit);
                sim.apply_command(command_of(CommandType::GroupMove, 1, {ants_of(sim, 1)[0]}, 40, 40));                                 // (the leader plays: an ant of it is seen walking, which is when a bot wants a Thief)
                Rig rig(sim, 0, Level::Easy, std::make_unique<StandardBot>(plan_for(Level::Easy)), 4, 4);
                rig.run(60);
                ASSERT_EQ(rig.as<StandardBot>().raids().raids_ordered() >= 1u, deficit == 2500);
                ASSERT_EQ(rig.as<StandardBot>().tactics().wants[static_cast<size_t>(sim::AntType::Thief)], deficit == 2500 ? 1u : 0u);      // (and a Thief is wanted for them)
            }
        }
        {   // (f) the last minute (a match of 2,000 ticks, the last 1,200 begin at tick 800): the bot that leads guards, the one that is behind is at tier 3 and its strike does not stop (the old stop is
            //     the last 900 ticks). The leader's box is kept 60 points above the bot's (or 100 below it) all along, so that the standing does not change with the bot's own income
            for (const bool leads : {true, false}) {
                sim::SimulationEngine sim;
                build(sim, 300, 360, 2000, 0);
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Combat, TileCoord{44 + i, 14});
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 8});
                for (int i = 0; i < 2; ++i) carrier_at(sim, 1, TileCoord{46 + i, 8});
                LevelPlan plan = plan_for(Level::Hard);
                plan.skirmish = false;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                const auto run_to = [&](uint64_t tick) {
                    while (sim.current_tick() < tick) {
                        sim.set_player_score(1, std::max<int32_t>(sim.get_player_score(0) + (leads ? -100 : 60), 300));
                        rig.run(4);
                    }
                };
                run_to(700);                                                                           // 1,300 ticks left: not the last minute yet
                ASSERT_EQ(rig.as<StandardBot>().tactics().standing.guard, false);
                ASSERT_EQ(rig.as<StandardBot>().tactics().standing.tier, 0u);
                ASSERT_EQ(rig.as<StandardBot>().strike().strikes_started(), 0u);
                run_to(950);                                                                           // 1,050 ticks left: the last minute
                const Standing& st = rig.as<StandardBot>().tactics().standing;
                ASSERT_EQ(st.guard, leads);
                ASSERT_EQ(st.tier, leads ? 0u : 3u);
                ASSERT_EQ(rig.as<StandardBot>().tactics().guard_stance, leads);
                ASSERT_EQ(rig.as<StandardBot>().strike().strikes_started() >= 1u, !leads);
            }
        }
        {   // (g) the guard: a bot that leads in the last minute does not hunt the wounded enemy that it hunts earlier (a Combat Ant and a worker, an enemy of 4 hit points), and one defender more answers a blow
            for (const bool last_minute : {false, true}) {
                sim::SimulationEngine sim;
                build(sim, 500, 300, 2000, last_minute ? 900 : 0);
                sim.spawn_unit(0, sim::AntType::Combat, TileCoord{27, 30});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{28, 30});
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 8});
                const uint32_t enemy = sim.spawn_unit(2, sim::AntType::Worker, TileCoord{30, 30});
                sim.get_unit(enemy).hp = 4;
                sim.set_player_score(0, 500);
                sim.set_player_score(1, 300);
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
                rig.run(60);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack) >= 1, !last_minute);
                ASSERT_EQ(rig.as<StandardBot>().fight().hunts_started() >= 1u, !last_minute);                                          // (not even begun)
            }
        }
        {   // (h) one defender more answers a blow in the last minute of a leader (Hard: 4 against 3)
            for (const bool last_minute : {false, true}) {
                sim::SimulationEngine sim;
                build(sim, 500, 300, 2000, last_minute ? 900 : 0);
                std::vector<uint32_t> mine;
                for (int i = 0; i < 9; ++i) mine.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26 + i % 3, 29 + i / 3}));
                const uint32_t enemy = sim.spawn_unit(2, sim::AntType::Worker, TileCoord{31, 29});
                sim.set_player_score(0, 500);
                sim.set_player_score(1, 300);
                LevelPlan plan = plan_for(Level::Hard);
                plan.hunt = false;                                                                                                     // (the fight that a blow begins, nothing else)
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(3);
                for (int t = 0; t < 60; ++t) {
                    keep_attacking(sim, 2, enemy, mine[0]);
                    rig.tick();
                }
                ASSERT_TRUE(rig.as<StandardBot>().fight().fights_started() >= 1u);
                ASSERT_EQ(rig.as<StandardBot>().fight().defenders_of(enemy).size(), last_minute ? 4u : 3u);
            }
        }
    } TEST_END();
}
