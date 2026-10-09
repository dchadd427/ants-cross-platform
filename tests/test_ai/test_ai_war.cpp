// The war batch (docs/BOTS.md, "The war batch"): the owner's report of a match of four bots on SMALL, "they just don't fight, they just eat ... they need to start more fights with each other, fire at
// their opponent, and try to kill their fire ant if they try to come for them ... the bomber doesn't place any bombs currently, but he should be bombing up the food so they can't eat it". Hand-made
// worlds (b41_helpers.hpp) hold the four rules; whole matches show what they add up to.
//
//   AI24.1  the plans: every level fights once it has nothing to harvest, Medium and Hard also fire the gate of the best opponent in and mine, the weaker the level the later and the more careful; a
//           plan made by hand has none of it
//   AI24.2  the mines: a Bomber of the economy lays them at a pile that an enemy works (and nowhere else, never at home), the plan without them lays none, a pile the bot reaches first is no
//           target, an ant that a task of a higher rank holds is not taken, the mines wait for an ant with nothing to harvest when the plan says so
//   AI24.3  the assault: the ants that have nothing to harvest go for an enemy ant once the plan's time has come, not before, not while the economy needs them, not into a stronger force
//   AI24.4  the raider hunt: an enemy Fire or Bomber Ant near the own hill is hunted without walls on the ring, a worker is not, one farther than the plan's radius is not
//   AI24.5  whole matches on SMALL: the shipped plans fight (attack orders, kills, bombs, fire) where the plans without the war batch do nothing of it, and the banked food of a Hard bot against
//           bots without it stays within a tenth
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

// The plan of a level with the war batch taken out (what the bots played before it)
LevelPlan without_war(Level level) {
    LevelPlan p = plan_for(level);
    without_war_batch(p);
    return p;
}

// The mines of team `team` that stand on the field now
std::vector<TileCoord> mines_of(const sim::SimulationEngine& sim, const MapInfo& map, uint8_t seat, uint8_t team) {
    const BotView view = BotView::build(sim, seat, &map);
    std::vector<TileCoord> out;
    for (const BombView& b : view.bombs()) {
        if (b.owner == team) out.push_back(b.tile);
    }
    return out;
}

}  // namespace

void run_war_tests() {
    TEST_CASE("AI24.1 The Plans: Every Level Fights Over What Is Left And Goes For The Enemy Ants When It Has Nothing To Harvest; Medium And Hard Fire The Gate Of The Best Opponent In And Mine, Hard From The Start And Most; Easy Asks For Better Odds And Starts Later; A Plan Made By Hand Has None Of It")
    {
        const LevelPlan easy = plan_for(Level::Easy);
        const LevelPlan medium = plan_for(Level::Medium);
        const LevelPlan hard = plan_for(Level::Hard);
        for (const LevelPlan* p : {&easy, &medium, &hard}) {
            ASSERT_TRUE(p->assault);
            ASSERT_TRUE(p->raider_hunt);
            ASSERT_TRUE(p->assault_force >= 2);
            ASSERT_FALSE(p->raider_piles);                                                                  // (a hunt at a pile costs the economy: measured, docs/BOTS.md)
        }
        // Easy: assault and the hunt at its own hill, nothing else
        ASSERT_FALSE(easy.sabotage);
        ASSERT_EQ(easy.war_fires, 0u);
        ASSERT_EQ(easy.war_bombers, 0u);
        ASSERT_EQ(easy.mine_per_pile, 0u);
        ASSERT_EQ(easy.mine_gate, 0u);
        // Medium: the fire-in in the last minutes (whether or not the enemy can put it out), mines at the gate of the best opponent while an ant has nothing to harvest
        ASSERT_TRUE(medium.sabotage);
        ASSERT_FALSE(medium.sabotage_safe);
        ASSERT_TRUE(medium.sabotage_after > hard.sabotage_after);
        ASSERT_TRUE(medium.war_fires >= 1);
        ASSERT_TRUE(medium.war_free_only);
        ASSERT_EQ(medium.mine_per_pile, 0u);
        ASSERT_TRUE(medium.mine_gate >= 1 && medium.war_bombers >= 1);
        // Hard: the fire-in from the start, mines at the piles an enemy works and at the gate
        ASSERT_TRUE(hard.sabotage);
        ASSERT_FALSE(hard.sabotage_safe);
        ASSERT_TRUE(hard.war_fires >= medium.war_fires);
        ASSERT_TRUE(hard.war_free_only);
        ASSERT_TRUE(hard.mine_per_pile >= 1 && hard.mine_gate >= medium.mine_gate && hard.war_bombers >= medium.war_bombers);
        // the weaker the level, the later it goes and the better the odds it asks for
        ASSERT_TRUE(easy.assault_after > medium.assault_after && medium.assault_after > hard.assault_after);
        ASSERT_TRUE(easy.assault_odds_percent > medium.assault_odds_percent && medium.assault_odds_percent > hard.assault_odds_percent);
        ASSERT_TRUE(easy.assault_force <= medium.assault_force && medium.assault_force <= hard.assault_force);
        ASSERT_TRUE(easy.raider_radius <= medium.raider_radius && medium.raider_radius <= hard.raider_radius);
        // a style keeps the rules (Hard's aggressive style had the fire-in before; the raider style has it now)
        for (const Style style : {Style::Aggressive, Style::Raider}) {
            BotRng rng(7);
            const LevelPlan styled = plan_for(Level::Hard, style, rng);
            ASSERT_TRUE(styled.assault && styled.sabotage && styled.raider_hunt && styled.mine_per_pile >= 1);
            ASSERT_FALSE(styled.sabotage_safe);
        }
        // a plan made by hand has none of it
        const LevelPlan hand;
        ASSERT_FALSE(hand.assault || hand.raider_hunt || hand.raider_piles || hand.war_free_only || hand.sabotage);
        ASSERT_EQ(hand.war_bombers + hand.war_fires + hand.mine_per_pile + hand.mine_gate + hand.raider_extra, 0u);
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan off = without_war(level);
            ASSERT_FALSE(off.assault || off.raider_hunt || off.sabotage);
            ASSERT_EQ(off.mine_per_pile + off.mine_gate + off.war_bombers + off.war_fires, 0u);
        }
    } TEST_END();

    TEST_CASE("AI24.2 The Mines: A Bomber Of The Economy Lays Them Round A Pile That An Enemy Works, Where Nobody Stands, Far From The Own Hill; None Without The Plan, None At A Pile The Bot Reaches First; An Ant Of A Task Of A Higher Rank Is Not Taken; With The Free Rule Only While An Ant Has Nothing To Harvest")
    {
        LevelPlan plan = plan_for(Level::Hard);
        plan.assault = false;
        plan.raider_hunt = false;
        plan.sabotage = false;
        plan.war_free_only = false;
        plan.war_bombers = 1;
        plan.mine_per_pile = 2;
        plan.mine_gate = 0;
        // a pile in front of the hill of team 1 (50, 4), far from the bot's own (4, 4); a Bomber of the bot walks about on the way, workers of the bot at home
        const auto build = [&](sim::SimulationEngine& sim, int32_t col, int32_t row) {
            empty_field(sim, 241);
            add_pile(sim, col, row, 60, 25);
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{kFightHills[1].x - 3, kFightHills[1].y + 6});
            const uint32_t bomber = sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{14, 10});
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 12});
            return bomber;
        };
        {   // (a) the mines stand round the pile, none at home, all the bot's own
            sim::SimulationEngine sim;
            const uint32_t bomber = build(sim, 40, 10);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(2400);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_TRUE(bot.mines().planted() >= 1);
            ASSERT_TRUE(sim.get_unit(bomber).type == sim::AntType::Bomber);
            const HillInfo& own = rig.map().hill(0);
            const auto mines = mines_of(sim, rig.map(), 0, 0);
            ASSERT_TRUE(!mines.empty());
            ASSERT_TRUE(mines.size() <= 2);                                                                 // (mine_per_pile at a time)
            for (const TileCoord& t : mines) {
                ASSERT_TRUE(t.chebyshev_dist(TileCoord{40, 10}) <= 6);                                      // round the pile
                ASSERT_TRUE(t.chebyshev_dist(own.origin) > 6);                                              // never at home
            }
        }
        {   // (b) the plan without mines: the Bomber lays none
            LevelPlan off = plan;
            off.mine_per_pile = 0;
            off.war_bombers = 0;
            sim::SimulationEngine sim;
            build(sim, 40, 10);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(off), 4, 4);
            rig.run(2400);
            ASSERT_EQ(rig.as<StandardBot>().mines().planted(), 0u);
            ASSERT_TRUE(mines_of(sim, rig.map(), 0, 0).empty());
        }
        {   // (c) a pile next to the bot's own hill is reached first by the bot: no mines
            sim::SimulationEngine sim;
            build(sim, 12, 12);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(2400);
            ASSERT_EQ(rig.as<StandardBot>().mines().planted(), 0u);
            ASSERT_TRUE(mines_of(sim, rig.map(), 0, 0).empty());
        }
        {   // (d) the Bomber is held by a task of a higher rank (the fight): the mines take no ant of the kind
            sim::SimulationEngine sim;
            const uint32_t bomber = build(sim, 40, 10);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(8);
            AntLedger& ledger = rig.as<StandardBot>().ledger_for_labs();
            ledger.set_rank(990, 9);
            ASSERT_TRUE(ledger.take(bomber, 990));
            rig.run(1200);
            ASSERT_EQ(rig.as<StandardBot>().mines().planted(), 0u);
            ASSERT_EQ(rig.as<StandardBot>().mines().jobs(), 0u);
        }
        {   // (e) the free rule: with the economy short of hands (three ants, a rich pile) the mines wait
            LevelPlan free_only = plan;
            free_only.war_free_only = true;
            sim::SimulationEngine sim;
            build(sim, 12, 12);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 13});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(free_only), 4, 4);
            rig.run(600);
            ASSERT_EQ(rig.as<StandardBot>().mines().planted(), 0u);
            ASSERT_EQ(rig.as<StandardBot>().mines().jobs(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI24.3 The Assault: Ants With Nothing To Harvest Go For An Enemy Ant When The Plan's Time Has Come, One Order Per Blow; Not Before, Not Without The Plan, Not While The Economy Needs Them, Not Into A Force That Is Far Stronger")
    {
        LevelPlan plan = plan_for(Level::Hard);
        plan.raider_hunt = false;
        plan.sabotage = false;
        plan.war_bombers = plan.war_fires = plan.mine_per_pile = plan.mine_gate = 0;
        plan.assault_after = 300;
        // six workers of the bot at home and no food on the field; an enemy worker of team 1 stands in the middle of the field, `escort` Combat Ants of team 1 next to it
        const auto build = [&](sim::SimulationEngine& sim, size_t escort, bool food) {
            empty_field(sim, 242);
            if (food) add_pile(sim, 12, 12, 200, 25);
            for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i % 3, 12 + i / 3});
            const uint32_t target = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{26, 14});
            for (size_t i = 0; i < escort; ++i) sim.spawn_unit(1, sim::AntType::Combat, TileCoord{27 + static_cast<int32_t>(i % 4), 15 + static_cast<int32_t>(i / 4)});
            return target;
        };
        {   // (a) nothing to harvest, the time has come: the ants go and strike
            sim::SimulationEngine sim;
            const uint32_t target = build(sim, 0, false);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(900);
            ASSERT_TRUE(rig.as<StandardBot>().fight().assaults_started() >= 1);
            ASSERT_TRUE(!attacks_of(rig).empty());
            ASSERT_TRUE(sim.get_unit(target).hp < 10 || !alive(sim, target));                               // the blows landed
        }
        {   // (b) before the plan's time nobody goes
            sim::SimulationEngine sim;
            build(sim, 0, false);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(240);
            ASSERT_EQ(rig.as<StandardBot>().fight().assaults_started(), 0u);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
        }
        {   // (c) the plan without the rule: nobody goes (a healthy ant is no hunt)
            LevelPlan off = plan;
            off.assault = false;
            sim::SimulationEngine sim;
            build(sim, 0, false);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(off), 4, 4);
            rig.run(900);
            ASSERT_EQ(rig.as<StandardBot>().fight().assaults_started(), 0u);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
        }
        {   // (d) food to harvest at the own hill: the ants are at work, nobody is free
            sim::SimulationEngine sim;
            build(sim, 0, true);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(900);
            ASSERT_EQ(rig.as<StandardBot>().fight().assaults_started(), 0u);
        }
        {   // (e) eight Combat Ants next to the target: a force far stronger than six workers, the plan's odds keep the ants at home
            sim::SimulationEngine sim;
            build(sim, 8, false);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(900);
            ASSERT_EQ(rig.as<StandardBot>().fight().assaults_started(), 0u);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
        }
    } TEST_END();

    TEST_CASE("AI24.4 The Raider Hunt: An Enemy Fire Ant Or Bomber Within The Plan's Radius Of The Own Hill Is Hunted Without Walls On The Ring, Not A Worker, Not One Farther Away, Not Without The Plan; The Hunt Of A Raider Is Not A Hunt Of The Walls' Fire Ant")
    {
        LevelPlan plan = plan_for(Level::Hard);
        plan.assault = false;
        plan.sabotage = false;
        plan.war_bombers = plan.war_fires = plan.mine_per_pile = plan.mine_gate = 0;
        plan.raider_radius = 6;
        const auto build = [&](sim::SimulationEngine& sim, sim::AntType kind, TileCoord at) {
            empty_field(sim, 243);
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{8, 10});
            for (int i = 0; i < 5; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9 + i, 12});
            return sim.spawn_unit(1, kind, at);
        };
        sim::SimulationEngine probe;
        empty_field(probe, 243);
        const MapInfo pmap(probe);
        const TileCoord origin = pmap.hill(0).origin;
        const TileCoord near_at{origin.x + 5, origin.y + 2};                                                // within 6 of the hill
        const TileCoord far_at{origin.x + 11, origin.y + 2};                                                // beyond 6
        for (const sim::AntType kind : {sim::AntType::Fire, sim::AntType::Bomber}) {                       // (a) both kinds are hunted, as a hunt of a raider
            sim::SimulationEngine sim;
            const uint32_t raider = build(sim, kind, near_at);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(60);
            ASSERT_EQ(rig.as<StandardBot>().fight().raider_hunts(), 1u);
            ASSERT_EQ(rig.as<StandardBot>().fight().fire_hunts(), 0u);
            ASSERT_TRUE(rig.as<StandardBot>().fight().hunting(raider));
            ASSERT_TRUE(!attacks_of(rig).empty());
        }
        {   // (b) a worker is no raider
            sim::SimulationEngine sim;
            build(sim, sim::AntType::Worker, near_at);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(120);
            ASSERT_EQ(rig.as<StandardBot>().fight().raider_hunts(), 0u);
        }
        {   // (c) beyond the radius nobody goes; with a wider one the Fire Ant is hunted
            sim::SimulationEngine sim;
            build(sim, sim::AntType::Fire, far_at);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(120);
            ASSERT_EQ(rig.as<StandardBot>().fight().raider_hunts(), 0u);
            LevelPlan wide = plan;
            wide.raider_radius = 14;
            sim::SimulationEngine sim2;
            build(sim2, sim::AntType::Fire, far_at);
            Rig rig2(sim2, 0, Level::Hard, std::make_unique<StandardBot>(wide), 4, 4);
            rig2.run(120);
            ASSERT_EQ(rig2.as<StandardBot>().fight().raider_hunts(), 1u);
        }
        {   // (d) the plan without the rule: no hunt (and no walls on the ring: the walls' hunt does not start either)
            LevelPlan off = plan;
            off.raider_hunt = false;
            sim::SimulationEngine sim;
            build(sim, sim::AntType::Fire, near_at);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(off), 4, 4);
            rig.run(120);
            ASSERT_EQ(rig.as<StandardBot>().fight().raider_hunts(), 0u);
            ASSERT_EQ(rig.as<StandardBot>().fight().fire_hunts(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI24.5 Whole Matches On SMALL: Four Bots Of The Shipped Plans Fight (Attack Orders, Kills, Fire) Where The Same Bots Without The War Batch Do Nothing Of It, At Every Level; A Hard Bot Of The Shipped Plan Banks Within A Tenth Of Hard Bots Without It")
    {
        const auto run = [&](Level level, bool war, uint32_t seed) {
            ArenaSpec spec;
            spec.level = &level_of("SMALL");
            spec.seed = seed;
            spec.max_ticks = 0;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.level = level;
                b.kind = "standard";
                b.style = Style::Random;
                spec.bots.push_back(b);
            }
            spec.factory = [&](const BotSpec& b) -> std::unique_ptr<Bot> {
                if (b.kind != "standard") return nullptr;
                return std::make_unique<StandardBot>(war ? plan_for(b.level) : without_war(b.level));
            };
            return play_match(spec);
        };
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            uint32_t attacks_on = 0, attacks_off = 0;
            uint32_t fires_on = 0;
            for (const uint32_t seed : {3u, 4u}) {
                for (const ArenaSeatResult& s : run(level, true, seed).seats) {
                    attacks_on += s.attack_orders;
                    fires_on += s.fires_lit;
                }
                for (const ArenaSeatResult& s : run(level, false, seed).seats) attacks_off += s.attack_orders;
            }
            ASSERT_TRUE(attacks_on >= 20);                                                                  // they fight
            ASSERT_TRUE(attacks_on > attacks_off * 3);                                                      // where the bots before did not
            if (level == Level::Hard) ASSERT_TRUE(fires_on >= 1);
        }
        {   // the banked food of two shipped Hard bots against two without the war batch, over six matches (a loss within a tenth: the measurements of docs/BOTS.md put it at a few percent)
            int64_t with = 0, without = 0;
            for (uint32_t seed = 1; seed <= 6; ++seed) {
                ArenaSpec spec;
                spec.level = &level_of("SMALL");
                spec.seed = seed;
                spec.max_ticks = 0;
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    BotSpec b;
                    b.seat = seat;
                    b.level = Level::Hard;
                    b.kind = "standard";
                    b.style = Style::Random;
                    spec.bots.push_back(b);
                }
                spec.factory = [&](const BotSpec& b) -> std::unique_ptr<Bot> {
                    if (b.kind != "standard") return nullptr;
                    return std::make_unique<StandardBot>(b.seat % 2 == 0 ? plan_for(b.level) : without_war(b.level));
                };
                const ArenaResult r = play_match(spec);
                for (const ArenaSeatResult& s : r.seats) (s.spec.seat % 2 == 0 ? with : without) += s.banked;
            }
            ASSERT_TRUE(with * 10 >= without * 9);
        }
    } TEST_END();
}
