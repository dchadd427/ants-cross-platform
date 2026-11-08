// The defence batch (docs/BOT_PEOPLE.md, "Step 1: everyone hits back"; include/ants_ai/defence.hpp): a Hard bot answers a blow of an enemy ant with every ant that can strike, the struck ant first, as many
// as the enemy has near the target and one more, and keeps the fight up through the target's hit clips while its force is as strong as the enemy's. Medium and Easy are as they were. Hand-made worlds
// (b41_helpers.hpp), quick enough for suite 2.20.
//
//   AI35.1  a Thief, a Fire Ant and a Bomber (empty, 10 hit points) answer a blow on a neighbour, a job's ants only after the free ones, nobody from beyond the reach; without the batch they do not
//   AI35.2  who never answers: the wall keeper while a thief threatens, a Thief in its raid clip or about to raid, a carrier, an ant with 3 hit points, the island play's crew, the tasks of a rank that the fight cannot take from
//   AI35.3  the struck ant answers first: with Combat Ants all round, which the level's defenders would all be, it is still a defender and its order is in the first release
//   AI35.4  three enemy workers that strike one ant: four defenders (3 + 1); one: three; six weak ones: five (the cap)
//   AI35.5  a fight at equal strength is kept through the target's hit clip and ends in a kill; one below the enemy's is called off at the look, before an ant is taken; one that falls below ends at the next look
//   AI35.6  Medium and Easy as shipped are as before: the same counters and state hash whatever the numbers of the batch say; the shipped plans of every level pin the batch
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/defence.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

// The plan of a level with nothing but the fights that answer a blow in play: no hunt of the wounded, no assault, no mines, no kill batch. `batch` false: the defence batch is taken out as well
LevelPlan defence_plan(Level level, bool batch = true) {
    LevelPlan p = peace_plan(level);
    without_kill_batch(p);
    p.hunt = false;
    p.catchup = false;
    p.endgame = false;
    if (!batch) without_defence_batch(p);
    return p;
}

// The commands of one kind that the rig's bot proposed at or after `from`
std::vector<const Command*> proposed_since(const Rig& rig, CommandType type, uint64_t from) {
    std::vector<const Command*> out;
    for (const auto& e : rig.proposed) {
        if (e.first >= from && e.second.type == type) out.push_back(&e.second);
    }
    return out;
}

bool names(const Command& c, uint32_t ant) {
    for (const uint32_t a : c.ants) {
        if (a == ant) return true;
    }
    return false;
}

const AntView* view_of(const BotView& view, uint32_t id) {
    for (const AntView& a : view.mine()) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

// What a rig's fight task did: the ants that were ever defenders of the fight against `enemy` and the most that were at once
struct Seen {
    std::set<uint32_t> ever;
    size_t most{0};
};

void watch_fight(Rig& rig, uint32_t enemy, Seen& seen) {
    const std::vector<uint32_t> now = rig.as<StandardBot>().fight().defenders_of(enemy);
    for (const uint32_t d : now) seen.ever.insert(d);
    seen.most = std::max(seen.most, now.size());
}

}  // namespace

void run_defence_tests() {
    TEST_CASE("AI35.1 A Thief, A Fire Ant And A Bomber (Empty, 10 Hit Points) Of A Hard Bot Answer A Blow On A Neighbour: Each Is A Defender And Gets A Strike Order; The Ants That A Job Holds Come After The Free Ones, Nobody Comes From Beyond The Reach; Without The Batch (The Old Rule) None Of Them Does")
    {
        for (const sim::AntType kind : {sim::AntType::Thief, sim::AntType::Fire, sim::AntType::Bomber}) {
            for (const bool batch : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 351);
                const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
                const uint32_t special = sim.spawn_unit(0, kind, TileCoord{24, 21});
                const uint32_t far_a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 44});
                const uint32_t far_b = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{45, 44});
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 17});
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(defence_plan(Level::Hard, batch)), 4, 4);
                rig.run(3);
                Seen seen;
                for (int t = 0; t < 160; ++t) {
                    keep_attacking(sim, 1, enemy, victim);
                    rig.tick();
                    watch_fight(rig, enemy, seen);
                }
                ASSERT_TRUE(sim.get_unit(victim).hp < 10 || !alive(sim, victim));                         // (the premise: the neighbour was struck)
                size_t orders_for_special = 0;
                for (const Command* c : proposed_since(rig, CommandType::GroupAttack, 0)) orders_for_special += names(*c, special) ? 1u : 0u;
                if (batch) {
                    ASSERT_TRUE(seen.ever.count(special) != 0);
                    ASSERT_TRUE(orders_for_special >= 1u);
                    ASSERT_TRUE(seen.ever.count(far_a) == 0 && seen.ever.count(far_b) == 0);               // (28 tiles away: beyond the reach of a defender)
                } else {
                    ASSERT_TRUE(seen.ever.count(special) == 0);
                    ASSERT_EQ(orders_for_special, 0u);
                }
            }
        }
        // the ants that a job holds are taken after the free ones: a Thief of the raid task stands nearest, two free workers a little further, and the fight takes the struck ant and the two workers
        {
            sim::SimulationEngine sim;
            empty_field(sim, 352);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{23, 19});
            const uint32_t free_a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 22});
            const uint32_t free_b = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 23});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 44});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 17});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(defence_plan(Level::Hard)), 4, 4);
            rig.run(3);
            ASSERT_TRUE(rig.as<StandardBot>().ledger_for_labs().claim(thief, StandardBot::kRaids));
            Seen seen;
            for (int t = 0; t < 160; ++t) {
                keep_attacking(sim, 1, enemy, victim);
                rig.tick();
                rig.as<StandardBot>().ledger_for_labs().claim(thief, StandardBot::kRaids);                  // (the raid task holds it whenever the fight lets go)
                watch_fight(rig, enemy, seen);
            }
            ASSERT_TRUE(seen.ever.count(victim) != 0 && seen.ever.count(free_a) != 0 && seen.ever.count(free_b) != 0);
            ASSERT_TRUE(seen.ever.count(thief) == 0);
        }
    } TEST_END();

    TEST_CASE("AI35.2 Who Never Answers (can_defend): The Wall Keeper While A Thief Threatens, A Thief In Its Raid Clip Or One Of The Raid Task Near The Raid Tile Of An Enemy Hill, A Carrier, An Ant With 3 Hit Points, The Island Play's Crew, An Ant Of A Task The Fights Cannot Take From, One That Stands On A Power-Up Or Is Stunned; The Ants Of A Pick-Up, A Wall, A Mine Or The Economy May Be Taken")
    {
        sim::SimulationEngine sim;
        empty_field(sim, 353);
        const uint32_t plain = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
        const uint32_t keeper = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{22, 20});
        const uint32_t raider = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{50, 9});                // 3 tiles from the raid tile of the hill of seat 1
        const uint32_t far_raider = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{30, 30});
        const uint32_t free_thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{51, 9});
        const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 20});
        sim.get_unit(carrier).holding = 1;
        sim.get_unit(carrier).carried_points = 20;
        const uint32_t hurt = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 20});
        sim.get_unit(hurt).hp = 3;
        const uint32_t crew = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{28, 20});
        const uint32_t digger = sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{30, 20});
        const uint32_t fighter = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{32, 20});            // (held by another fight)
        const uint32_t gate = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{34, 20});              // (held by a gate goal, rank 6)
        const uint32_t trip = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{36, 20});              // (a pick-up trip, rank 3)
        const uint32_t miner = sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{38, 20});             // (the mines, rank 4)
        const uint32_t walker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{40, 20});            // (the economy, rank 1)
        const uint32_t on_power_up = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{42, 20});
        sim.grid_mut().place_powerup(42, 20, 2);
        const uint32_t stunned = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 24});
        sim.spawn_unit(1, sim::AntType::Worker, TileCoord{50, 20});
        const MapInfo map(sim);
        const HillInfo& foe = map.hill(1);
        ASSERT_TRUE(foe.present);
        const BotView view = BotView::build(sim, 0, &map);
        ASSERT_TRUE(view_of(view, raider) != nullptr && view_of(view, raider)->tile.chebyshev_dist(foe.raid) <= 6);                  // (the premise: within 6 tiles of the raid tile)
        ASSERT_TRUE(view_of(view, far_raider) != nullptr && view_of(view, far_raider)->tile.chebyshev_dist(foe.raid) > 6);
        AntLedger ledger;
        ledger.set_rank(StandardBot::kFight, 5);
        ledger.set_rank(StandardBot::kRush, 5);
        ledger.set_rank(StandardBot::kGateGoals, 6);
        ledger.set_rank(StandardBot::kRaids, 3);
        ledger.set_rank(StandardBot::kPowerUps, 3);
        ledger.set_rank(StandardBot::kIslands, 3);
        ledger.set_rank(StandardBot::kExpedition, 3);
        ledger.set_rank(StandardBot::kMines, 4);
        ledger.set_rank(StandardBot::kHarvest, 1);
        ASSERT_TRUE(ledger.claim(raider, StandardBot::kRaids));
        ASSERT_TRUE(ledger.claim(far_raider, StandardBot::kRaids));
        ASSERT_TRUE(ledger.claim(crew, StandardBot::kExpedition));
        ASSERT_TRUE(ledger.claim(digger, StandardBot::kIslands));
        ASSERT_TRUE(ledger.claim(fighter, StandardBot::kFight));
        ASSERT_TRUE(ledger.claim(gate, StandardBot::kGateGoals));
        ASSERT_TRUE(ledger.claim(trip, StandardBot::kPowerUps));
        ASSERT_TRUE(ledger.claim(miner, StandardBot::kMines));
        ASSERT_TRUE(ledger.claim(walker, StandardBot::kHarvest));
        Tactics tactics;
        const auto can = [&](uint32_t id, bool struck = false) {
            const AntView* a = view_of(view, id);
            return a != nullptr && can_defend(view, *a, tactics, ledger, map, struck);
        };
        ASSERT_TRUE(can(plain));
        // the Fire Ant that keeps the walls: out of the fight while a thief threatens, as free as any other when none does
        ASSERT_TRUE(can(keeper));
        tactics.wall_keeper = keeper;
        ASSERT_TRUE(can(keeper));
        tactics.wall_demand = true;
        ASSERT_FALSE(can(keeper));
        ASSERT_TRUE(can(plain));
        // a Thief of the raid task near the raid tile of an enemy hill is about to raid; far from it, or not of the raid task, it may be taken; in its raid clip it takes no order at all
        ASSERT_FALSE(can(raider));
        ASSERT_TRUE(can(far_raider));
        ASSERT_TRUE(can(free_thief));
        {
            AntView clip = *view_of(view, free_thief);
            clip.state = sim::UnitState::Infiltrating;
            ASSERT_FALSE(clip.takes_orders());
            ASSERT_FALSE(can_defend(view, clip, tactics, ledger, map));
            ASSERT_FALSE(can_defend(view, clip, tactics, ledger, map, true));
        }
        // a carrier, an ant with 3 hit points (4 is enough), a stunned ant, one on a power-up
        ASSERT_FALSE(can(carrier));
        ASSERT_FALSE(can(hurt));
        {
            AntView four = *view_of(view, hurt);
            four.hp = 4;
            ASSERT_TRUE(can_defend(view, four, tactics, ledger, map));
            four.hp = 3;
            ASSERT_FALSE(can_defend(view, four, tactics, ledger, map));
        }
        ASSERT_FALSE(can(on_power_up));
        {
            AntView stun = *view_of(view, stunned);
            stun.state = sim::UnitState::Stunned;
            ASSERT_FALSE(can_defend(view, stun, tactics, ledger, map));
            ASSERT_FALSE(can_defend(view, stun, tactics, ledger, map, true));
        }
        // the hit clip: an ant in it takes no order, and may still be taken when it is the one that was struck
        {
            AntView clip = *view_of(view, plain);
            clip.state = sim::UnitState::Flinch;
            ASSERT_FALSE(can_defend(view, clip, tactics, ledger, map));
            ASSERT_TRUE(can_defend(view, clip, tactics, ledger, map, true));
            clip.state = sim::UnitState::Knockback;
            ASSERT_FALSE(can_defend(view, clip, tactics, ledger, map));
            ASSERT_TRUE(can_defend(view, clip, tactics, ledger, map, true));
        }
        // the island play's crew and diggers, another fight's ants, a gate goal's: never; a pick-up trip's, the mines' and the economy's: they may
        ASSERT_FALSE(can(crew));
        ASSERT_FALSE(can(digger));
        ASSERT_FALSE(can(fighter));
        ASSERT_FALSE(can(gate));
        ASSERT_TRUE(can(trip));
        ASSERT_TRUE(can(miner));
        ASSERT_TRUE(can(walker));
    } TEST_END();

    TEST_CASE("AI35.3 The Struck Ant Answers First: With Three Combat Ants Near It, Which The Level's Three Defenders Would All Be (A Combat Ant Comes First), The Struck Worker Is Still One Of The Defenders And Its Order Is In The First Release After The Blow; Without The Rule It Is Neither")
    {
        for (const bool rule : {true, false}) {
            // the three defenders of the level would all be Combat Ants (they come first); the struck ant is first all the same
            {
                sim::SimulationEngine sim;
                empty_field(sim, 355);
                const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Combat, TileCoord{22 + i, 25});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 44});
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
                LevelPlan plan = defence_plan(Level::Hard);
                plan.victim_answers = rule;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(12);
                sim.get_unit(victim).hp = 9;                                                              // (a blow that the view shows, with no hit clip: the worker takes orders)
                const uint64_t blow = sim.current_tick();
                Seen seen;
                for (int t = 0; t < 40; ++t) {
                    rig.tick();
                    watch_fight(rig, enemy, seen);
                }
                const std::vector<const Command*> first = proposed_since(rig, CommandType::GroupAttack, blow);
                ASSERT_FALSE(first.empty());
                ASSERT_EQ(names(*first.front(), victim), rule);
                ASSERT_EQ(seen.ever.count(victim) != 0, rule);
            }
        }
    } TEST_END();

    TEST_CASE("AI35.4 How Many Answer: Against Three Enemy Workers That Strike One Ant The Hard Bot Sends Four Defenders (The Level's Three And One More), Against One Three, Against Six That Are Down To 3 Hit Points Five (The Cap); Without The Batch Three In Every Case")
    {
        struct Case {
            size_t enemies;
            unsigned enemy_hp;
            size_t wanted;
        };
        for (const Case& k : {Case{1, 10, 3}, Case{3, 10, 4}, Case{6, 3, 5}}) {
            for (const bool batch : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 356);
                const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
                for (int i = 0; i < 9; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21 + i % 3, 22 + i / 3});
                std::vector<uint32_t> enemies;
                for (size_t i = 0; i < k.enemies; ++i) {
                    enemies.push_back(sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21 + static_cast<int32_t>(i % 3), 17 - static_cast<int32_t>(i / 3)}));
                    sim.get_unit(enemies.back()).hp = static_cast<uint8_t>(k.enemy_hp);
                }
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(defence_plan(Level::Hard, batch)), 4, 4);
                rig.run(3);
                size_t most = 0;
                for (int t = 0; t < 120; ++t) {
                    for (const uint32_t e : enemies) keep_attacking(sim, 1, e, victim);
                    rig.tick();
                    for (const uint32_t e : enemies) most = std::max(most, rig.as<StandardBot>().fight().defenders_of(e).size());
                }
                ASSERT_EQ(most, batch ? k.wanted : size_t{3});
            }
        }
    } TEST_END();

    TEST_CASE("AI35.5 How Long: A Fight At Equal Strength Is Kept Through The Target's Hit Clip And Ends In A Kill (Without The Batch It Ends At The First Clip, The Enemy Lives); One Below The Enemy's Is Called Off At The Look Before An Ant Is Taken Or Ordered; One That Falls Below Ends At The Next Look")
    {
        // (a) kept: three workers (the struck one at 9 hit points) against one enemy worker that does not strike: the force is the stronger, the clip does not end the fight, the enemy dies
        for (const bool batch : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 357);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 22});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 22});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(defence_plan(Level::Hard, batch)), 4, 4);
            rig.run(12);
            sim.get_unit(victim).hp = 9;
            rig.run(900);
            const FightTask& fight = rig.as<StandardBot>().fight();
            if (batch) {
                ASSERT_TRUE(fight.clip_waits() > 0u);
                ASSERT_FALSE(alive(sim, enemy));
                ASSERT_EQ(fight.fights(), 0u);
            } else {
                ASSERT_EQ(fight.clip_waits(), 0u);
                ASSERT_TRUE(alive(sim, enemy));
            }
        }
        // (b) the boundary: 29 hit points of three ants (the struck one at 9) against 29 of three enemy workers is kept, against 30 it is called off at the first look, with no order and no ant taken
        for (const unsigned enemy_hp_sum : {29u, 30u}) {
            sim::SimulationEngine sim;
            empty_field(sim, 358);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 22});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 22});
            const std::array<TileCoord, 3> spots = {TileCoord{22, 18}, TileCoord{23, 18}, TileCoord{21, 18}};
            for (size_t i = 0; i < 3; ++i) {
                const uint32_t e = sim.spawn_unit(1, sim::AntType::Worker, spots[i]);
                sim.get_unit(e).hp = static_cast<uint8_t>(i == 0 ? enemy_hp_sum - 20u : 10u);
            }
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(defence_plan(Level::Hard)), 4, 4);
            rig.run(12);
            sim.get_unit(victim).hp = 9;
            const uint64_t blow = sim.current_tick();
            rig.run(12);
            const FightTask& fight = rig.as<StandardBot>().fight();
            if (enemy_hp_sum == 29u) {
                ASSERT_EQ(fight.fights(), 1u);
                ASSERT_FALSE(proposed_since(rig, CommandType::GroupAttack, blow).empty());
            } else {
                ASSERT_EQ(fight.fights(), 0u);
                ASSERT_TRUE(fight.fights_started() >= 1u);                                                 // (it began: the look saw the blow)
                ASSERT_TRUE(proposed_since(rig, CommandType::GroupAttack, blow).empty());
                ASSERT_EQ(rig.as<StandardBot>().ledger().count(StandardBot::kFight), 0u);
            }
        }
        // (c) a force that falls below the enemy's during the fight ends it at the next look: three enemy workers join the one that the force was beating
        {
            sim::SimulationEngine sim;
            empty_field(sim, 359);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 22});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 22});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(defence_plan(Level::Hard)), 4, 4);
            rig.run(12);
            sim.get_unit(victim).hp = 9;
            rig.run(24);
            ASSERT_EQ(rig.as<StandardBot>().fight().fights(), 1u);
            for (const TileCoord t : {TileCoord{22, 17}, TileCoord{23, 17}, TileCoord{21, 17}}) sim.spawn_unit(1, sim::AntType::Worker, t);
            const uint64_t joined = sim.current_tick();
            rig.run(5);                                                                                     // (the look interval is 4)
            ASSERT_EQ(rig.as<StandardBot>().fight().fights(), 0u);
            rig.run(60);
            ASSERT_TRUE(proposed_since(rig, CommandType::GroupAttack, joined + 5).empty());
        }
    } TEST_END();

    TEST_CASE("AI35.6 Medium And Easy As Shipped Are As Before: The Same Counters, The Same Commands And The Same State Hash On A Fixed Scene (A Thief, A Fire Ant, A Bomber And Three Workers Against Three Enemy Workers) Whatever The Numbers Of The Batch Say, And With The Batch Taken Out; The Shipped Plans Of Every Level Pin The Batch")
    {
        for (const Level level : {Level::Medium, Level::Easy, Level::Hard}) {
            const LevelPlan p = plan_for(level);
            ASSERT_EQ(p.defend_all_types, level == Level::Hard);
            ASSERT_EQ(p.victim_answers, level == Level::Hard);
            ASSERT_EQ(p.defend_by_strength, level == Level::Hard);
            ASSERT_EQ(p.defend_radius, 4);
            ASSERT_EQ(p.defend_extra, 1u);
            ASSERT_EQ(p.defend_keep_percent, level == Level::Medium ? 120u : 100u);
            ASSERT_EQ(p.defend_max, level == Level::Medium ? 3u : 5u);
            ASSERT_EQ(p.defend_reach, level == Level::Medium ? 8 : 10);
            LevelPlan off = p;
            without_defence_batch(off);
            ASSERT_FALSE(off.defend_all_types || off.victim_answers || off.defend_by_strength);
            const LevelPlan none;                                                                           // (a plan made by hand has none of it)
            ASSERT_FALSE(none.defend_all_types || none.victim_answers || none.defend_by_strength);
        }
        struct Outcome {
            uint32_t started, orders, clip_waits, escapes, shunned;
            size_t commands;
            uint64_t hash;
            bool same_counters(const Outcome& o) const {
                return started == o.started && orders == o.orders && clip_waits == o.clip_waits && escapes == o.escapes && shunned == o.shunned && commands == o.commands;
            }
            bool operator==(const Outcome& o) const { return same_counters(o) && hash == o.hash; }
        };
        const auto play = [](const LevelPlan& plan) {
            sim::SimulationEngine sim;
            empty_field(sim, 360);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            int32_t row = 21;
            for (const sim::AntType type : {sim::AntType::Thief, sim::AntType::Fire, sim::AntType::Bomber}) sim.spawn_unit(0, type, TileCoord{24, row++});
            for (int i = 0; i < 2; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20 + i, 24});
            std::vector<uint32_t> enemies;
            for (int i = 0; i < 3; ++i) enemies.push_back(sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21 + i, 17}));
            Rig rig(sim, 0, plan.level, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(3);
            for (int t = 0; t < 500; ++t) {
                for (const uint32_t e : enemies) keep_attacking(sim, 1, e, victim);
                rig.tick();
            }
            const FightTask& f = rig.as<StandardBot>().fight();
            return Outcome{f.fights_started(), f.attacks_ordered(), f.clip_waits(), f.escapes(), f.shunned_count(), rig.proposed.size(), sim.state_hash().total};
        };
        for (const Level level : {Level::Medium, Level::Easy}) {
            const Outcome shipped = play(defence_plan(level));
            // the counters of the older rule on this scene (taken with the batch out of the plan; the matches of the bot before the batch end on the same state hashes): Medium 7 fights, 15 orders, no wait
            // through a hit clip, nobody who escapes or is shunned, 15 commands; Easy 6 fights and the rest the same. A change of the fight task that moves them on purpose moves these numbers
            const Outcome older{level == Level::Medium ? 7u : 6u, 15u, 0u, 0u, 0u, 15u, 0u};
            ASSERT_TRUE(shipped.same_counters(older));
            LevelPlan odd = defence_plan(level);                                                            // the numbers of the batch turned to extremes: with the flags off they change nothing
            odd.defend_keep_percent = 1000;
            odd.defend_radius = 1;
            odd.defend_extra = 9;
            odd.defend_max = 1;
            odd.defend_reach = 1;
            ASSERT_TRUE(shipped == play(odd));
            LevelPlan off = defence_plan(level);
            without_defence_batch(off);
            ASSERT_TRUE(shipped == play(off));
        }
        // and a force that is the stronger (three workers, the struck one hurt, against one enemy worker that does not strike): without the batch the fight ends at the first hit clip and the enemy lives, however the
        // numbers of the batch are set (the strength rule is the flag's, not the numbers')
        for (const Level level : {Level::Medium, Level::Easy}) {
            for (const int variant : {0, 1}) {
                LevelPlan plan = defence_plan(level);
                if (variant == 1) {
                    plan.defend_keep_percent = 1;
                    plan.defend_max = 9;
                    plan.defend_reach = 99;
                    plan.defend_extra = 0;
                }
                sim::SimulationEngine sim;
                empty_field(sim, 361);
                sim.set_player_eggs(0, 5);
                const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 22});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 22});
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(12);
                sim.get_unit(victim).hp = 9;
                rig.run(900);
                ASSERT_TRUE(rig.as<StandardBot>().fight().fights_started() >= 1u);
                ASSERT_EQ(rig.as<StandardBot>().fight().clip_waits(), 0u);
                ASSERT_TRUE(alive(sim, enemy));
            }
        }
        // the same scene at Hard answers differently: the batch is what changes it
        const Outcome with = play(defence_plan(Level::Hard));
        const Outcome without = play(defence_plan(Level::Hard, false));
        ASSERT_FALSE(with == without);
    } TEST_END();
}
