// The stand batch (docs/BOTS.md, "The stand batch"): a report of a match on TREASURE, 2026-10-10: a combat ant was blocking where the regular ant could walk, and it did not know that it can move
// it; the fire ants need to fight each other: a fire ant is firing in a team, and that team has a fire ant that could just go and try to kill the other fire ant, or at least try to knock it off the fire
// and then get it with its other ants (stand your ground); a bot that is down by a few hundred points is very unlikely to win by just continuing to eat, so it could select all its ants and
// take them to the enemy base; that is why it is really important to put bombs around the own base: the enemy would have to go one or two ants at a time and path around the bombs.
//
//   AI25.1  the plans: what each level has of the batch, the switch that takes all of it out (`--tune stand=0`), a plan made by hand has none
//   AI25.2  the fire duel: the own Fire Ant strikes the enemy Fire Ant that stands on a fire wall of the ring (nobody else's blow reaches it there), the others wait; without the rule nobody goes
//   AI25.3  the draft: a Thief and a Bomber Ant of the bot (no worker, no Combat Ant at home) hunt the Fire Ant that fires the gate in; without the rule they stand by
//   AI25.4  the ramp unjam: an own ant without food that stands idle on the ramp is sent to the side of the doorstep when a carrier is on its way; not without the rule, not before its ticks
//   AI25.5  the mines of a loser: a bot behind lays its mines from the plan's war tier (Medium and Hard 2), not before tier 3 without the rule
//   AI25.6  the mines at home: a Bomber lays them five to nine tiles from the own hill, plan.mine_home_apart tiles apart, off the carriers' way, at most plan.mine_home; none without the plan, none before plan.mine_home_after, none while no enemy is seen to play
//   AI25.7  the rush: a bot far behind sends its typed ants without food at the leader's hill (gather, then assault; Workers only with plan.rush_workers), not before its time, not while it leads or with the plan off, and it ends
//   AI25.8  whole matches: nobody rushes on ISLANDS (no walk joins the hills); the shipped plans fire and mine on TREASURE and bank within a tenth of the bots without the batch
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "b41_helpers.hpp"
#include "contest_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;
using namespace contest;

namespace {

// The mines of team `team` that stand on the field now
std::vector<TileCoord> mines_at(const sim::SimulationEngine& sim, const MapInfo& map, uint8_t team) {
    const BotView view = BotView::build(sim, 0, &map);
    std::vector<TileCoord> out;
    for (const BombView& b : view.bombs()) {
        if (b.owner == team) out.push_back(b.tile);
    }
    return out;
}

// The hill of team 0 on the empty field and the eight tiles of the ring round its gate
struct Home {
    TileCoord origin{};
    std::array<TileCoord, 8> ring{};
    explicit Home(uint32_t seed) {
        sim::SimulationEngine sim;
        empty_field(sim, seed);
        const MapInfo map(sim);
        origin = map.hill(0).origin;
        ring = SabotageTask::ring_of(map.hill(0));
    }
};

}  // namespace

void run_stand_tests() {
    TEST_CASE("AI25.1 The Plans: Every Level Has The Fire Duel, Hard (The Level With A Gate) The Ramp Unjam; The Draft Of Every Ant Is Off (It Cost Food And Won Nothing The Duel Did Not); Medium And Hard Lay Mines From An Earlier War Tier And Round Their Hill, And Rush When Far Behind; Easy Mines And Rushes Nowhere; The Switch Takes All Of It Out; A Plan Made By Hand Has None")
    {
        const LevelPlan easy = plan_for(Level::Easy);
        const LevelPlan medium = plan_for(Level::Medium);
        const LevelPlan hard = plan_for(Level::Hard);
        for (const LevelPlan* p : {&easy, &medium, &hard}) {
            ASSERT_TRUE(p->fire_duel);
            ASSERT_FALSE(p->fire_draft);                                                                     // (the rule is there, the plans leave it off: docs/BOTS.md, "The stand batch")
            ASSERT_EQ(p->ramp_unjam_ticks, 40u);
        }
        ASSERT_TRUE(hard.ramp_unjam);                                                                        // (the gate is Hard's: GateTask never runs at the other levels)
        ASSERT_FALSE(easy.ramp_unjam || medium.ramp_unjam);
        ASSERT_FALSE(easy.rush);
        ASSERT_EQ(easy.mine_home, 0u);
        ASSERT_EQ(easy.behind_mine_gate, 0u);
        ASSERT_EQ(easy.behind_mine_tier, 4u);
        ASSERT_TRUE(hard.mine_per_pile >= 6 && hard.mine_apart == 1);                                        // (reported after play: the bomber only placed three bombs where it could bomb up a whole area)
        ASSERT_TRUE(hard.mine_home >= 8 && medium.mine_home >= 6);
        for (const LevelPlan* p : {&medium, &hard}) {
            ASSERT_TRUE(p->rush);
            ASSERT_TRUE(p->mine_home >= 2);
            ASSERT_TRUE(p->behind_mine_gate > p->mine_gate);
            ASSERT_TRUE(p->behind_mine_tier < p->behind_free_tier);                                          // (the mines come before the Combat Ants leave the food)
            ASSERT_TRUE(p->rush_deficit >= 200);
            ASSERT_TRUE(p->rush_after >= 3600);                                                              // (no rush while the economy is being built)
            ASSERT_TRUE(p->mine_home_after >= 2400);
        }
        ASSERT_TRUE(hard.behind_mine_tier <= medium.behind_mine_tier);                                       // (the weaker the level, the later)
        ASSERT_TRUE(hard.rush_tier <= medium.rush_tier);
        ASSERT_TRUE(hard.rush_deficit <= medium.rush_deficit);
        // the switch: all of it out, and the war batch's switch takes the stand batch with it
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            LevelPlan p = plan_for(level);
            without_stand_batch(p);
            ASSERT_FALSE(p.fire_duel || p.fire_draft || p.ramp_unjam || p.rush);
            ASSERT_EQ(p.mine_home, 0u);
            ASSERT_EQ(p.behind_mine_gate, 0u);
            ASSERT_EQ(p.behind_mine_tier, 4u);
            ASSERT_TRUE(p.mine_per_pile <= 3u);                                                              // (the field of mines at the piles goes with the batch: three were all there was)
            ASSERT_EQ(p.mine_apart, 2u);
            ASSERT_EQ(p.mine_home_apart, 3u);
            LevelPlan q = plan_for(level);
            without_war_batch(q);
            ASSERT_FALSE(q.fire_duel || q.fire_draft || q.ramp_unjam || q.rush);
            ASSERT_EQ(q.mine_home, 0u);
        }
        const LevelPlan by_hand;                                                                             // a plan made by hand (the tests') has none of it
        ASSERT_FALSE(by_hand.fire_duel || by_hand.fire_draft || by_hand.ramp_unjam || by_hand.rush);
        ASSERT_EQ(by_hand.mine_home, 0u);
        ASSERT_EQ(by_hand.behind_mine_gate, 0u);
        ASSERT_EQ(by_hand.behind_mine_tier, 4u);
    } TEST_END();

    TEST_CASE("AI25.2 The Fire Duel (Reported After Play: The Fire Ants Need To Fight Each Other, At Least To Knock The Enemy Off The Fire And Then Get It With The Other Ants): The Own Fire Ant Is Sent At The Enemy Fire Ant That Stands On A Wall Of The Ring, Unless It Keeps The Walls Of The Thief Hole Against A Thief, Nobody Else Is (No Blow Of Theirs Reaches It There), The Enemy Is Struck And Thrown Off; Without The Rule Nobody Goes")
    {
        const Home home(91);
        const auto build = [&](sim::SimulationEngine& sim, uint32_t& keeper, uint32_t& enemy) {
            empty_field(sim, 91);
            keeper = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{8, 10});
            for (int i = 0; i < 5; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9 + i, 12});
            for (int i = 0; i < 3; ++i) sim.grid_mut().place_firewall(static_cast<uint32_t>(home.ring[static_cast<size_t>(i)].x), static_cast<uint32_t>(home.ring[static_cast<size_t>(i)].y), 1);
            enemy = sim.spawn_unit(1, sim::AntType::Fire, home.ring[0]);                                       // on a lit tile of the ring
        };
        {   // (a) with the rule: a hunt begins, and while the enemy stands on the wall only the Fire Ant is ordered at it
            sim::SimulationEngine sim;
            uint32_t keeper = 0, enemy = 0;
            build(sim, keeper, enemy);
            LevelPlan plan = plan_for(Level::Hard);
            plan.fire_defence_hold = 0;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(40);
            ASSERT_EQ(rig.as<StandardBot>().fight().fire_hunts(), 1u);
            ASSERT_TRUE(rig.as<StandardBot>().fight().hunting(enemy));
            const auto attacks = attacks_of(rig);
            ASSERT_TRUE(!attacks.empty());
            for (const auto& e : attacks) {
                if (e.second.tile_x != home.ring[0].x || e.second.tile_y != home.ring[0].y) continue;           // (the orders at the wall)
                ASSERT_EQ(e.second.ants.size(), 1u);
                ASSERT_EQ(e.second.ants[0], keeper);
            }
            // the blows land: within 600 ticks the enemy is hurt, thrown off the wall or dead
            rig.run(560);
            const sim::AntSnapshot* snap = snapshot_of(sim, enemy);
            ASSERT_TRUE(snap == nullptr || snap->hp < 10 || snap->tile_x != home.ring[0].x || snap->tile_y != home.ring[0].y);
        }
        {   // (b) without the rule (the plan of before): the enemy on the wall is out of reach, nobody is ordered at it
            sim::SimulationEngine sim;
            uint32_t keeper = 0, enemy = 0;
            build(sim, keeper, enemy);
            LevelPlan plan = plan_for(Level::Hard);
            plan.fire_defence_hold = 0;
            plan.fire_duel = false;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(300);
            ASSERT_EQ(rig.as<StandardBot>().fight().fire_hunts(), 0u);
            for (const auto& e : attacks_of(rig)) ASSERT_TRUE(!(e.second.tile_x == home.ring[0].x && e.second.tile_y == home.ring[0].y));
        }
        {   // (d) a thief threatens (an enemy Thief is in sight): the Fire Ant that keeps the walls of the thief hole stays at them, the duel leaves it alone (the thieves raided 100 points a match
            //     more on TREASURE when it went); with plan.fire_duel_walls it goes
            for (const bool walls : {false, true}) {
                sim::SimulationEngine sim;
                uint32_t keeper = 0, enemy = 0;
                build(sim, keeper, enemy);
                sim.grid_mut().clear_firewall(static_cast<uint32_t>(home.ring[0].x), static_cast<uint32_t>(home.ring[0].y));            // (the fire comes later: the bot has found its keeper and seen the Thief by then)
                sim.grid_mut().clear_firewall(static_cast<uint32_t>(home.ring[1].x), static_cast<uint32_t>(home.ring[1].y));
                sim.grid_mut().clear_firewall(static_cast<uint32_t>(home.ring[2].x), static_cast<uint32_t>(home.ring[2].y));
                sim.kill_unit(enemy);
                sim.spawn_unit(1, sim::AntType::Thief, TileCoord{home.origin.x + 12, home.origin.y + 9});
                LevelPlan plan = plan_for(Level::Hard);
                plan.fire_defence_hold = 0;
                plan.fire_duel_walls = walls;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(120);
                ASSERT_TRUE(rig.as<StandardBot>().tactics().wall_demand);
                for (int i = 0; i < 3; ++i) sim.grid_mut().place_firewall(static_cast<uint32_t>(home.ring[static_cast<size_t>(i)].x), static_cast<uint32_t>(home.ring[static_cast<size_t>(i)].y), 1);
                sim.spawn_unit(1, sim::AntType::Fire, home.ring[0]);
                rig.run(120);
                ASSERT_EQ(rig.as<StandardBot>().fight().fire_hunts() >= 1u, walls);
                size_t at_wall = 0;
                for (const auto& e : attacks_of(rig)) at_wall += e.second.tile_x == home.ring[0].x && e.second.tile_y == home.ring[0].y ? 1u : 0u;
                ASSERT_EQ(at_wall >= 1u, walls);
            }
        }
        {   // (c) no Fire Ant of the bot: nobody can reach it on the wall, with the rule on or off
            sim::SimulationEngine sim;
            empty_field(sim, 91);
            for (int i = 0; i < 5; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9 + i, 12});
            for (int i = 0; i < 3; ++i) sim.grid_mut().place_firewall(static_cast<uint32_t>(home.ring[static_cast<size_t>(i)].x), static_cast<uint32_t>(home.ring[static_cast<size_t>(i)].y), 1);
            sim.spawn_unit(1, sim::AntType::Fire, home.ring[0]);
            LevelPlan plan = plan_for(Level::Hard);
            plan.fire_defence_hold = 0;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(300);
            for (const auto& e : attacks_of(rig)) ASSERT_TRUE(!(e.second.tile_x == home.ring[0].x && e.second.tile_y == home.ring[0].y));
        }
    } TEST_END();

    TEST_CASE("AI25.3 The Draft: With No Worker And No Combat Ant At Home The Thief And The Bomber Ant Are Sent At The Fire Ant That Fires The Gate In (Six Typed Ants Have Nobody Else To Send); Without The Rule They Stand By")
    {
        const Home home(92);
        const auto build = [&](sim::SimulationEngine& sim) {
            empty_field(sim, 92);
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Thief, TileCoord{8 + i, 10});          // (five ants of the kinds that the fighters are not: more than the reserve that stays out of every fight)
            for (int i = 0; i < 2; ++i) sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{8 + i, 11});
            for (int i = 0; i < 3; ++i) sim.grid_mut().place_firewall(static_cast<uint32_t>(home.ring[static_cast<size_t>(i)].x), static_cast<uint32_t>(home.ring[static_cast<size_t>(i)].y), 1);
            return sim.spawn_unit(1, sim::AntType::Fire, TileCoord{14, 6});                                     // off the wall, seven tiles from the gate
        };
        const auto typed_orders = [&](const Rig& rig, sim::SimulationEngine& sim) {
            size_t n = 0;
            for (const auto& e : attacks_of(rig)) {
                for (const uint32_t ant : e.second.ants) n += sim.get_unit(ant).type == sim::AntType::Thief || sim.get_unit(ant).type == sim::AntType::Bomber ? 1u : 0u;
            }
            return n;
        };
        LevelPlan plan = plan_for(Level::Hard);
        plan.war_bombers = plan.war_fires = plan.mine_per_pile = plan.mine_gate = plan.mine_home = 0;
        plan.sabotage = plan.raids = false;
        plan.fire_draft = true;                  // (the plans leave it off)
        {   // (a) with the draft: both are sent
            sim::SimulationEngine sim;
            build(sim);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(120);
            ASSERT_EQ(rig.as<StandardBot>().fight().fire_hunts(), 1u);
            ASSERT_TRUE(typed_orders(rig, sim) >= 1u);
        }
        {   // (b) without it nobody at home can strike: no hunt
            LevelPlan off = plan;
            off.fire_draft = false;
            sim::SimulationEngine sim;
            build(sim);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(off), 4, 4);
            rig.run(120);
            ASSERT_EQ(typed_orders(rig, sim), 0u);
        }
    } TEST_END();

    TEST_CASE("AI25.4 The Ramp Unjam (Reported After Play: A Combat Ant Was Blocking Where The Regular Ant Could Walk, And It Did Not Know That It Can Move): An Own Ant Without Food That Stands On The Ramp While A Carrier Waits Is Sent To The Side Of The Doorstep; Not Without The Rule, Not Before The Plan's Ticks, Not While Nobody Waits")
    {
        const Home home(93);
        const TileCoord ramp{home.origin.x + 1, home.origin.y};
        LevelPlan plan = plan_for(Level::Hard);
        plan.guards = false;
        plan.combat_harvests = false;
        plan.sabotage = plan.raids = plan.assault = plan.raider_hunt = false;
        plan.war_bombers = plan.war_fires = plan.mine_per_pile = plan.mine_gate = plan.mine_home = 0;
        const auto build = [&](sim::SimulationEngine& sim, bool carrier) {
            empty_field(sim, 93);
            const uint32_t combat = sim.spawn_unit(0, sim::AntType::Combat, ramp);
            if (carrier) carrier_at(sim, 0, TileCoord{home.origin.x + 1, home.origin.y - 5});
            return combat;
        };
        const auto position = [&](const sim::SimulationEngine& sim, uint32_t id) {
            const sim::AntSnapshot* s = snapshot_of(sim, id);
            return s == nullptr ? TileCoord{-1, -1} : TileCoord{s->tile_x, s->tile_y};
        };
        {   // (a) the Combat Ant stands on the ramp and a carrier waits at the doorstep: it is sent aside
            sim::SimulationEngine sim;
            const uint32_t combat = build(sim, true);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(600);
            ASSERT_TRUE(rig.as<StandardBot>().gate().ramp_unjams() >= 1u);
            ASSERT_TRUE(position(sim, combat) != ramp);
        }
        {   // (b) the plan without the rule: it stands where it was
            LevelPlan off = plan;
            off.ramp_unjam = false;
            sim::SimulationEngine sim;
            const uint32_t combat = build(sim, true);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(off), 4, 4);
            rig.run(600);
            ASSERT_EQ(rig.as<StandardBot>().gate().ramp_unjams(), 0u);
            ASSERT_TRUE(position(sim, combat) == ramp);
        }
        {   // (c) nobody waits (no carrier): the ramp is not in anybody's way
            sim::SimulationEngine sim;
            const uint32_t combat = build(sim, false);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(600);
            ASSERT_EQ(rig.as<StandardBot>().gate().ramp_unjams(), 0u);
            ASSERT_TRUE(position(sim, combat) == ramp);
        }
        {   // (d) not before the plan's ticks: a longer wait is no unjam yet
            LevelPlan late = plan;
            late.ramp_unjam_ticks = 5000;
            sim::SimulationEngine sim;
            const uint32_t combat = build(sim, true);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(late), 4, 4);
            rig.run(600);
            ASSERT_EQ(rig.as<StandardBot>().gate().ramp_unjams(), 0u);
            ASSERT_TRUE(position(sim, combat) == ramp);
        }
    } TEST_END();

    TEST_CASE("AI25.5 The Mines Of A Loser (Reported After Play: A Bot Waited Until The Last Two Minutes Though It Was Losing, And Should Have Started Putting A Bunch Of Bombs Down): A Bot 300 Points Behind Is At War Tier 1 And Lays Its Mines Then, Where Before The Free Rule Held Them Back Until Tier 3; A Bot That Is Level Lays None; The Gate Ring Gets More")
    {
        LevelPlan plan = plan_for(Level::Hard);
        plan.assault = plan.raider_hunt = plan.sabotage = false;
        plan.war_bombers = 1;
        plan.mine_per_pile = 2;
        plan.mine_gate = 0;
        plan.mine_home = 0;
        plan.rush = false;
        const LevelPlan shipped = plan;                                                                      // (the tier of the shipped plan: (a2))
        plan.behind_mine_tier = 1;
        // (the bot banks food as it plays; the scores are set again every 100 ticks, so that it stays 300 points behind)
        const auto play = [&](Rig& rig, sim::SimulationEngine& sim, int32_t own, int32_t leader, uint32_t ticks) {
            for (uint32_t t = 0; t < ticks; t += 100) {
                sim.set_player_score(0, own);
                sim.set_player_score(1, leader);
                rig.run(100);
            }
        };
        const auto build = [&](sim::SimulationEngine& sim, int32_t leader) {
            empty_field(sim, 94);
            add_pile(sim, 40, 10, 60, 25);                                                                   // 1,500 points of food on the field: 300 points behind is 20 percent of what can be earned
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{kFightHills[1].x - 3, kFightHills[1].y + 6});
            sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{14, 10});
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 12});
            sim.set_player_score(0, 100);
            sim.set_player_score(1, leader);
        };
        {   // (a) 300 points behind: war tier 1, and the mines are laid
            sim::SimulationEngine sim;
            build(sim, 400);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            play(rig, sim, 100, 400, 2400);
            ASSERT_EQ(rig.as<StandardBot>().tactics().standing.war, 1u);
            ASSERT_TRUE(rig.as<StandardBot>().mines().planted() >= 1u);
        }
        {   // (a2) the tier of the shipped Hard plan (2: tier 1 cost four bots 10 percent of their food on TREASURE): 300 points behind is tier 1, no mines; 600 behind is tier 2, mines
            for (const int32_t leader : {400, 700}) {
                sim::SimulationEngine sim;
                build(sim, leader);
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(shipped), 4, 4);
                play(rig, sim, 100, leader, 2400);
                ASSERT_EQ(rig.as<StandardBot>().tactics().standing.war, leader == 400 ? 1u : 2u);
                ASSERT_EQ(rig.as<StandardBot>().mines().planted() >= 1u, leader == 700);
            }
        }
        {   // (b) the plan of before (the mines wait for war tier 3 or for an ant with nothing to harvest): none
            LevelPlan off = plan;
            off.behind_mine_tier = 4;
            sim::SimulationEngine sim;
            build(sim, 400);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(off), 4, 4);
            play(rig, sim, 100, 400, 2400);
            ASSERT_EQ(rig.as<StandardBot>().tactics().standing.war, 1u);
            ASSERT_EQ(rig.as<StandardBot>().mines().planted(), 0u);
        }
        {   // (c) level: none
            sim::SimulationEngine sim;
            build(sim, 100);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            play(rig, sim, 100, 100, 2400);
            ASSERT_EQ(rig.as<StandardBot>().mines().planted(), 0u);
        }
        {   // (d) no pile to mine: the ring round the gate of the leader gets plan.behind_mine_gate mines at a time for a bot at the tier (plan.mine_gate otherwise: here none)
            LevelPlan gate_plan = plan;
            gate_plan.mine_per_pile = 0;
            gate_plan.mine_gate = 1;
            gate_plan.behind_mine_gate = 3;
            for (const bool behind : {false, true}) {
                sim::SimulationEngine sim;
                empty_field(sim, 95);
                sim.spawn_unit(1, sim::AntType::Worker, TileCoord{kFightHills[1].x - 3, kFightHills[1].y + 6});
                for (int i = 0; i < 2; ++i) sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{14 + i, 10});
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 12});
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan), 4, 4);
                play(rig, sim, 100, behind ? 1500 : 100, 4000);
                const MapInfo& map = rig.map();
                const auto ring = SabotageTask::ring_of(map.hill(1));
                size_t on_ring = 0;
                for (const TileCoord& t : mines_at(sim, map, 0)) {
                    for (const TileCoord& r : ring) on_ring += t == r ? 1u : 0u;
                }
                if (behind) {
                    ASSERT_TRUE(on_ring >= 2u);                                                              // (more than the one of mine_gate)
                    ASSERT_TRUE(on_ring <= 3u);
                } else {
                    ASSERT_TRUE(on_ring <= 1u);                                                              // (a bot that is level has plan.mine_gate of them, while an ant has nothing to harvest)
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI25.6 The Mines At Home (Reported After Play: It Is Really Important To Put Bombs Around The Own Base, So That The Enemy Has To Go One Or Two Ants At A Time And Path Around The Bombs): A Bomber Lays plan.mine_home Mines Five To Nine Tiles From The Own Hill, Three Tiles Apart, Never On The Doorstep Or At A Pile; None Without The Plan, None Before Its Tick")
    {
        LevelPlan plan = plan_for(Level::Hard);
        plan.assault = plan.raider_hunt = plan.sabotage = plan.rush = false;
        plan.mine_per_pile = plan.mine_gate = plan.behind_mine_gate = 0;
        plan.mine_home = 3;
        plan.mine_home_apart = 3;
        plan.mine_home_after = 0;
        const auto build = [&](sim::SimulationEngine& sim) {
            empty_field(sim, 96);
            const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{kFightHills[1].x - 3, kFightHills[1].y + 6});
            sim.apply_command(command_of(sim::CommandType::GroupMove, 1, {foe}, kFightHills[1].x - 3, kFightHills[1].y + 12));             // (it walks: the bot sees an enemy play)
            sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{14, 10});
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 12});
        };
        {   // (a) three mines, in the ring of five to nine tiles, three apart
            sim::SimulationEngine sim;
            build(sim);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(2400);
            const TileCoord origin = rig.map().hill(0).origin;
            const std::vector<TileCoord> mines = mines_at(sim, rig.map(), 0);
            ASSERT_TRUE(rig.as<StandardBot>().mines().planted() >= 1u);
            ASSERT_TRUE(!mines.empty() && mines.size() <= 3u);
            for (size_t i = 0; i < mines.size(); ++i) {
                ASSERT_TRUE(mines[i].chebyshev_dist(origin) >= 5 && mines[i].chebyshev_dist(origin) <= 9);
                for (size_t k = i + 1; k < mines.size(); ++k) ASSERT_TRUE(mines[i].chebyshev_dist(mines[k]) >= 3);
            }
        }
        {   // (a2) a field of them (the plans lay 8 and 12: the bomber only placed three bombs where it could bomb up a whole area), two apart as the plans have it: more than three, none closer than
            //      two tiles, none outside the ring, and the food is not touched (the Workers bank)
            LevelPlan field = plan;
            field.mine_home = 8;
            field.mine_home_apart = 2;
            sim::SimulationEngine sim;
            build(sim);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(field), 4, 4);
            rig.run(6000);
            const TileCoord origin = rig.map().hill(0).origin;
            const std::vector<TileCoord> mines = mines_at(sim, rig.map(), 0);
            ASSERT_TRUE(mines.size() > 3u && mines.size() <= 8u);
            for (size_t i = 0; i < mines.size(); ++i) {
                ASSERT_TRUE(mines[i].chebyshev_dist(origin) >= 5 && mines[i].chebyshev_dist(origin) <= 9);
                for (size_t k = i + 1; k < mines.size(); ++k) ASSERT_TRUE(mines[i].chebyshev_dist(mines[k]) >= 2);
            }
        }
        {   // (a3) a pile on the lane (fourteen tiles east of the hill): the carriers' way to it is no place for a mine (own bombs block their walks; on a map with food between the hills it is the
            //      enemy's lane too, and four bots that mined it lost 17 percent of their food); with plan.mine_home_off_route off the lane gets mines
            for (const bool off_route : {true, false}) {
                LevelPlan p = plan;
                p.mine_home = 6;
                p.mine_home_apart = 2;
                p.mine_home_off_route = off_route;
                sim::SimulationEngine sim;
                build(sim);
                add_pile(sim, 26, 10, 60, 25);
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
                rig.run(4000);
                const TileCoord origin = rig.map().hill(0).origin;
                size_t on_way = 0;
                for (const TileCoord& m : mines_at(sim, rig.map(), 0)) on_way += std::abs(m.y - origin.y) <= 2 && m.x > origin.x && m.x < 26 ? 1u : 0u;
                if (off_route) {
                    ASSERT_EQ(on_way, 0u);
                } else {
                    ASSERT_TRUE(on_way >= 1u);
                }
            }
        }
        {   // (b) the plan without them: none
            LevelPlan off = plan;
            off.mine_home = 0;
            sim::SimulationEngine sim;
            build(sim);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(off), 4, 4);
            rig.run(2400);
            ASSERT_EQ(rig.as<StandardBot>().mines().planted(), 0u);
        }
        {   // (c) not before the plan's tick
            LevelPlan late = plan;
            late.mine_home_after = 2000;
            sim::SimulationEngine sim;
            build(sim);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(late), 4, 4);
            rig.run(1900);
            ASSERT_EQ(rig.as<StandardBot>().mines().planted(), 0u);
        }
        {   // (d) no enemy plays (alone on the field): nobody comes, nothing is laid
            sim::SimulationEngine sim;
            empty_field(sim, 96);
            sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{14, 10});
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 12});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(1200);
            ASSERT_EQ(rig.as<StandardBot>().mines().planted(), 0u);                                          // (the hills of the others are on the field but nothing of theirs has moved: nobody is seen to play)
        }
    } TEST_END();

    TEST_CASE("AI25.7 The Rush (Reported After Play: A Bot Far Behind Could Select All Its Ants And Take Them To The Enemy Base): A Bot Far Behind Sends Every Ant Without Food At The Leader, Gathers Them On The Way And Then Orders Them At The Leader's Ants; Not Before Its Time, Not While It Leads, Not With The Plan Off, Not In The Last Ticks; It Ends")
    {
        LevelPlan plan = plan_for(Level::Hard);
        plan.assault = plan.raider_hunt = plan.sabotage = false;
        plan.war_bombers = plan.war_fires = plan.mine_per_pile = plan.mine_gate = plan.mine_home = 0;
        plan.rush_tier = 0;
        plan.rush_workers = true;                                                                            // (the plans send the typed ants only: (f))
        plan.rush_deficit = 200;
        plan.rush_after = 100;
        plan.rush_ticks = 2400;
        // six ants of the bot at home, the leader (team 1) has a worker at its gate and one in the field; the leader's box is `leader` points
        const auto build = [&](sim::SimulationEngine& sim, int32_t own, int32_t leader, uint32_t ticks = 14400) {
            empty_field(sim, 97, ticks);
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 12});
            sim.spawn_unit(0, sim::AntType::Combat, TileCoord{9, 14});
            sim.spawn_unit(0, sim::AntType::Thief, TileCoord{10, 14});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{kFightHills[1].x + 1, kFightHills[1].y - 4});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{kFightHills[1].x - 3, kFightHills[1].y - 6});
            sim.set_player_score(0, own);
            sim.set_player_score(1, leader);
        };
        {   // (a) 800 points behind: a rush begins with all the ants, gathers, assaults, and the ants are ordered at the leader's ants
            sim::SimulationEngine sim;
            build(sim, 100, 900);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(120);
            const RushTask& rush = rig.as<StandardBot>().rush();
            ASSERT_TRUE(rush.active());
            ASSERT_EQ(rush.rushes_started(), 1u);
            ASSERT_TRUE(rush.force() >= 5u);                                                                 // every ant without food (all six)
            ASSERT_EQ(rush.target(), 1);
            ASSERT_TRUE(rush.rally().x >= 0);
            ASSERT_FALSE(rush.assaulting());                                                                 // (they are on the way)
            rig.run(1500);
            ASSERT_TRUE(rig.as<StandardBot>().rush().assaulting() || !rig.as<StandardBot>().rush().active());
            ASSERT_TRUE(rig.as<StandardBot>().rush().attacks_ordered() >= 1u);
            ASSERT_TRUE(!attacks_of(rig).empty());
            // the ants stand near the leader's hill now (not at home): the rally tile is where his walk to it costs 400
            size_t near_foe = 0;
            for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                if (a.player_id == 0 && TileCoord{a.tile_x, a.tile_y}.chebyshev_dist(kFightHills[1]) <= 30) ++near_foe;
            }
            ASSERT_TRUE(near_foe >= 3u);
        }
        {   // (b) it ends by its clock (rush_ticks) and does not begin again before the pause is over
            sim::SimulationEngine sim;
            build(sim, 100, 900);
            LevelPlan p = plan;
            p.rush_ticks = 600;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
            rig.run(120);
            ASSERT_TRUE(rig.as<StandardBot>().rush().active());
            rig.run(900);
            ASSERT_FALSE(rig.as<StandardBot>().rush().active());
            ASSERT_EQ(rig.as<StandardBot>().rush().rushes_started(), 1u);
            ASSERT_TRUE(rig.as<StandardBot>().rush().rush_ticks() >= 400u && rig.as<StandardBot>().rush().rush_ticks() <= 800u);
        }
        {   // (c) a deficit under the plan's, a bot that leads, the plan without the rule, a time before rush_after: no rush
            struct Case { int32_t own; int32_t leader; bool rule; uint32_t after; };
            const Case cases[] = {{100, 250, true, 100}, {900, 100, true, 100}, {100, 900, false, 100}, {100, 900, true, 5000}};
            for (const Case& c : cases) {
                sim::SimulationEngine sim;
                build(sim, c.own, c.leader);
                LevelPlan p = plan;
                p.rush = c.rule;
                p.rush_after = c.after;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
                rig.run(600);
                ASSERT_FALSE(rig.as<StandardBot>().rush().active());
                ASSERT_EQ(rig.as<StandardBot>().rush().rushes_started(), 0u);
            }
        }
        {   // (d) in the last rush_min_left ticks nothing is begun
            sim::SimulationEngine sim;
            build(sim, 100, 900, 700);                                                                       // a match of 700 ticks: 400 are left at tick 300
            LevelPlan p = plan;
            p.rush_min_left = 800;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
            rig.run(400);
            ASSERT_EQ(rig.as<StandardBot>().rush().rushes_started(), 0u);
        }
        {   // (e) an ant with food is not called (it banks first); the rest go
            sim::SimulationEngine sim;
            build(sim, 100, 900);
            const uint32_t carrier = carrier_at(sim, 0, TileCoord{12, 12});
            LevelPlan p = plan;
            p.rush_after = 0;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(p), 4, 4);
            rig.run(12);
            ASSERT_TRUE(rig.as<StandardBot>().rush().active());
            ASSERT_TRUE(rig.as<StandardBot>().ledger().owner(carrier) != StandardBot::kRush);
        }
        {   // (f) the plans' rush (plan.rush_workers off: the Workers kill nothing and the rush cost 30 to 75 points a match with them): two typed ants and four Workers do not make a force; with
            //     five typed ants (the Thief is held by the raids) the rush is four of them and the Workers stay at their piles
            LevelPlan typed = plan;
            typed.rush_workers = false;
            {
                sim::SimulationEngine sim;
                build(sim, 100, 900);
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(typed), 4, 4);
                rig.run(200);
                ASSERT_EQ(rig.as<StandardBot>().rush().rushes_started(), 0u);
            }
            {
                sim::SimulationEngine sim;
                build(sim, 100, 900);
                sim.spawn_unit(0, sim::AntType::Combat, TileCoord{11, 14});
                sim.spawn_unit(0, sim::AntType::Combat, TileCoord{12, 14});
                sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{13, 14});
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(typed), 4, 4);
                rig.run(120);
                const RushTask& rush = rig.as<StandardBot>().rush();
                ASSERT_TRUE(rush.active());
                ASSERT_EQ(rush.force(), 4u);
            }
        }
    } TEST_END();

    TEST_CASE("AI25.8 Whole Matches: Nobody Rushes On ISLANDS Where No Walk Joins The Hills (Even With The Thresholds At Their Lowest); Two Hard Bots Of The Shipped Plan Fire And Mine On TREASURE, And Bank Within A Tenth Of Two Without The Batch")
    {
        const auto run = [&](const char* map, const std::vector<std::function<LevelPlan(const BotSpec&)>>& plans, uint32_t seed) {
            ArenaSpec spec;
            spec.level = &level_of(map);
            spec.seed = seed;
            spec.max_ticks = 0;
            for (uint8_t seat = 0; seat < plans.size(); ++seat) {
                BotSpec b;
                b.seat = static_cast<uint8_t>(seat * 2);
                b.level = Level::Hard;
                b.kind = "standard";
                b.style = Style::Random;
                spec.bots.push_back(b);
            }
            spec.factory = [&](const BotSpec& b) -> std::unique_ptr<Bot> {
                if (b.kind != "standard") return nullptr;
                return std::make_unique<StandardBot>(plans[b.seat / 2](b));
            };
            return play_match(spec);
        };
        {   // (a) ISLANDS: the hills are on islands; the rush has no way to walk and never starts, with the thresholds at their lowest
            const auto eager = [](const BotSpec& b) {
                LevelPlan p = plan_for(b.level);
                p.rush_tier = 0;
                p.rush_workers = true;
                p.rush_deficit = 1;
                p.rush_after = 0;
                p.rush_min_left = 0;
                return p;
            };
            const ArenaResult r = run("ISLANDS", {eager, eager}, 3);
            for (const ArenaSeatResult& s : r.seats) ASSERT_EQ(s.rushes, 0u);
        }
        {   // (b) TREASURE: over four matches the shipped Hard bots (the batch on) fire and mine, and bank within a tenth of the same bots without it
            const auto shipped = [](const BotSpec& b) { return plan_for(b.level); };
            const auto before = [](const BotSpec& b) {
                LevelPlan p = plan_for(b.level);
                without_stand_batch(p);
                return p;
            };
            int64_t with = 0, without = 0;
            uint32_t mines = 0;
            uint32_t hunts = 0;
            for (uint32_t seed = 1; seed <= 4; ++seed) {
                const ArenaResult a = run("TREASURE", {shipped, before}, seed);
                for (const ArenaSeatResult& s : a.seats) {
                    (s.spec.seat == 0 ? with : without) += s.banked;
                    if (s.spec.seat == 0) {
                        mines += s.mines_planted;
                        hunts += s.fire_hunts;
                    }
                }
                const ArenaResult b = run("TREASURE", {before, shipped}, seed);
                for (const ArenaSeatResult& s : b.seats) {
                    (s.spec.seat == 2 ? with : without) += s.banked;
                    if (s.spec.seat == 2) {
                        mines += s.mines_planted;
                        hunts += s.fire_hunts;
                    }
                }
            }
            ASSERT_TRUE(mines >= 4u);
            ASSERT_TRUE(hunts >= 1u);
            ASSERT_TRUE(with * 10 >= without * 9);
        }
    } TEST_END();
}
