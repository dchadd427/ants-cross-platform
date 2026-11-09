// The group batch (docs/BOT_PEOPLE.md, "Step 3: fight as a group"; include/ants_ai/group_fight.hpp): with it on, the ants within 20 tiles of the target are called to a fight that answers a blow (the defence batch called
// those within 10). It is built and OFF at every level, so the cases turn it on by hand. Hand-made worlds (b41_helpers.hpp), quick enough for suite 2.20.
//
//   AI38.1  the number: the reach with the batch off (the defence batch's, capped by the leash) and on (the group's, nothing caps it); the shipped plans of every level pin the batch off; taking it out puts a plan back
//   AI38.2  who is called: an ant 20 tiles from the target joins a Hard bot's fight, one 21 tiles away does not; with the batch off 10 and 11 tiles; Medium with the defence rules on by hand: 14 and 15 against 8 and 9
//   AI38.3  how long: the share of the enemy's strength that keeps a fight up is the defence batch's, with the group on or off (three ants against six enemy workers: 24 kept and 25 called off at 60 percent, 29 and 30 at 50)
//   AI38.4  the whole path: three enemy workers strike one ant; the two workers 15 tiles away are called and the fight stands; without the batch it is called off at the look and nobody is ordered
//   AI38.5  the shipped bots of every level play as before: the same counters and state hash on a fixed scene whatever the number of the batch says; the game type 187 plays the brawl, the batch changes nothing there
//   AI38.6  the edges: the walk check across a channel still holds for the ants of the far ring, a carrier is called from no distance, the struck ant still answers first
//   AI38.7  the rules of the far ring: an ant with 3 hit points is not called from afar either, an ant that a job holds is called from afar when it is the only one, and a free ant 19 tiles away is called
//           before an ant 3 tiles away that a job holds
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/defence.hpp"
#include "ants_ai/group_fight.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

// The plan of a level with nothing but the fights that answer a blow in play (no hunt of the wounded, no assault, no mines, no kill batch). `group` false: the batch is taken out as well. At Medium, where the
// defence batch is built and off, `defence` turns its three rules on by hand
LevelPlan group_plan(Level level, bool group = true, bool defence = true) {
    LevelPlan p = peace_plan(level);
    without_kill_batch(p);
    p.hunt = false;
    p.catchup = false;
    p.endgame = false;
    if (defence) p.defend_all_types = p.victim_answers = p.defend_by_strength = true;
    p.group_fight = group;
    return p;
}

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

// The ants that were ever defenders of the fight against `enemy` while a rig ran
struct Seen {
    std::set<uint32_t> ever;
};

void watch_fight(Rig& rig, uint32_t enemy, Seen& seen) {
    for (const uint32_t d : rig.as<StandardBot>().fight().defenders_of(enemy)) seen.ever.insert(d);
}

// A struck ant, one enemy worker that stands still 2 tiles from it, and one ant at `dx`, `dy` from the enemy (the target of the fight): the blow is the struck ant's hit points (as AI35.3 does: a hit
// that the view shows, with no hit clip). Two ants and no egg are under the reserve, so there is a bystander far from everything and an egg. Returns the ants that the fight called, over 60 ticks
struct Called {
    bool joined{false};
    bool ordered{false};
};

// The ant that may be called: its hit points and the job that holds it (kNoTask: it is free; a job's claim is made again after every tick, as AI35.1 does)
struct Who {
    uint8_t hp{10};
    TaskId job{kNoTask};
};

Called call_from(const LevelPlan& plan, Level level, int dx, int dy, uint32_t seed, Who who = Who{}) {
    sim::SimulationEngine sim;
    empty_field(sim, seed);
    sim.set_player_eggs(0, 5);
    const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
    const uint32_t recruit = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22 + dx, 18 + dy});
    sim.get_unit(recruit).hp = who.hp;
    sim.spawn_unit(0, sim::AntType::Worker, TileCoord{57, 57});
    const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
    Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
    rig.run(12);
    sim.get_unit(victim).hp = 9;
    const uint64_t blow = sim.current_tick();
    Seen seen;
    for (int t = 0; t < 60; ++t) {
        if (who.job != kNoTask) rig.as<StandardBot>().ledger_for_labs().claim(recruit, who.job);
        rig.tick();
        watch_fight(rig, enemy, seen);
    }
    Called out;
    out.joined = seen.ever.count(recruit) != 0;
    for (const Command* c : proposed_since(rig, CommandType::GroupAttack, blow)) out.ordered = out.ordered || names(*c, recruit);
    return out;
}

}  // namespace

void run_group_tests() {
    TEST_CASE("AI38.1 The Number (recruit_reach): With The Batch Off It Is The Defence Batch's (The Reach Capped By The Leash); On, It Is The Group's And Nothing Caps It; The Shipped Plans Of Every Level Pin The Batch Off (Hard 20 And Medium 14 When It Is Turned On); Taking The Batch Out Puts A Plan Back")
    {
        LevelPlan p;
        ASSERT_FALSE(p.group_fight);                                                                        // (a plan made by hand has none of it)
        ASSERT_EQ(p.group_reach, 20);
        ASSERT_EQ(recruit_reach(p), 10);                                                                    // (defend_reach 10, leash 10)
        p.defend_reach = 99;
        p.leash_tiles = 12;
        ASSERT_EQ(recruit_reach(p), 12);                                                                    // (the leash caps the defence batch's reach)
        p.leash_tiles = 7;
        p.defend_reach = 5;
        ASSERT_EQ(recruit_reach(p), 5);                                                                     // (and the reach caps itself)
        p.group_fight = true;
        p.group_reach = 33;
        ASSERT_EQ(recruit_reach(p), 33);                                                                    // (the group's reach is capped neither by the leash (7) nor by defend_reach (5))
        p.group_reach = 3;
        ASSERT_EQ(recruit_reach(p), 3);                                                                     // (and it may be shorter than the defence batch's: it is the group's number that counts)
        without_group_fight(p);
        ASSERT_FALSE(p.group_fight);
        ASSERT_EQ(recruit_reach(p), 5);
        ASSERT_EQ(p.group_reach, 3);                                                                        // (only the flag goes: the number stays for `--tune group=1`)
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan s = plan_for(level);
            ASSERT_FALSE(s.group_fight);                                                                    // (off at every level)
            ASSERT_EQ(s.group_reach, level == Level::Medium ? 14 : 20);
            ASSERT_EQ(recruit_reach(s), std::min(s.defend_reach, s.leash_tiles));                           // (so the shipped reach is the defence batch's)
            LevelPlan on = s;
            on.group_fight = true;
            ASSERT_EQ(recruit_reach(on), level == Level::Medium ? 14 : 20);
            without_group_fight(on);
            ASSERT_EQ(recruit_reach(on), recruit_reach(s));
        }
        ASSERT_TRUE(plan_for(Level::Hard).group_reach > plan_for(Level::Hard).leash_tiles);                // (Hard's leash is 12)
    } TEST_END();

    TEST_CASE("AI38.2 Who Is Called: An Ant 20 Tiles From The Target Joins The Fight Of A Hard Bot And Gets A Strike Order, One 21 Tiles Away Does Not; With The Batch Out The Line Is At 10 And 11 Tiles (The Defence Batch's Reach); Medium With The Defence Rules On By Hand Calls From 14 Tiles (Not 15) With The Batch And From 8 (Not 9) Without")
    {
        // Hard: the batch on, the line between 20 and 21 tiles (the target stands at (22, 18); the ant is straight south of it)
        const Called on20 = call_from(group_plan(Level::Hard), Level::Hard, 0, 20, 371);
        ASSERT_TRUE(on20.joined && on20.ordered);
        const Called on21 = call_from(group_plan(Level::Hard), Level::Hard, 0, 21, 372);
        ASSERT_FALSE(on21.joined || on21.ordered);
        const Called on20_diagonal = call_from(group_plan(Level::Hard), Level::Hard, -20, 20, 373);         // (Chebyshev distance: a diagonal counts as the longer side)
        ASSERT_TRUE(on20_diagonal.joined);
        const Called on21_diagonal = call_from(group_plan(Level::Hard), Level::Hard, -20, 21, 374);
        ASSERT_FALSE(on21_diagonal.joined);
        // the batch out: Hard's leash is 12 and its reach 10, so the line is at 10
        const Called off10 = call_from(group_plan(Level::Hard, false), Level::Hard, 0, 10, 375);
        ASSERT_TRUE(off10.joined && off10.ordered);
        const Called off11 = call_from(group_plan(Level::Hard, false), Level::Hard, 0, 11, 376);
        ASSERT_FALSE(off11.joined || off11.ordered);
        // the same ant 15 tiles away: called with the batch, not without it
        ASSERT_TRUE(call_from(group_plan(Level::Hard), Level::Hard, 0, 15, 377).joined);
        ASSERT_FALSE(call_from(group_plan(Level::Hard, false), Level::Hard, 0, 15, 378).joined);
        // Medium (the defence rules on by hand: its own numbers, 120 percent, 3 defenders at most, reach 8 and group reach 14)
        ASSERT_TRUE(call_from(group_plan(Level::Medium), Level::Medium, 0, 14, 379).joined);
        ASSERT_FALSE(call_from(group_plan(Level::Medium), Level::Medium, 0, 15, 380).joined);
        ASSERT_TRUE(call_from(group_plan(Level::Medium, false), Level::Medium, 0, 8, 381).joined);
        ASSERT_FALSE(call_from(group_plan(Level::Medium, false), Level::Medium, 0, 9, 382).joined);
        // the group's number is the one that counts, whatever the leash and the defence batch's reach say
        LevelPlan odd = group_plan(Level::Hard);
        odd.leash_tiles = 3;
        odd.defend_reach = 2;
        odd.group_reach = 17;
        ASSERT_TRUE(call_from(odd, Level::Hard, 0, 17, 383).joined);
        ASSERT_FALSE(call_from(odd, Level::Hard, 0, 18, 384).joined);
    } TEST_END();

    TEST_CASE("AI38.3 How Long: The Share Of The Enemy's Strength That Keeps A Fight Up Is The Defence Batch's Whether The Group Batch Is On Or Off: Three Ants (29 Hit Points) Against Six Enemy Workers Are Kept Up To 24 Hit Points Between Them And Called Off At 25 At 60 Percent, And Up To 29 And Called Off At 30 At 50 Percent")
    {
        const auto fight_of = [](const LevelPlan& plan, unsigned enemy_hp_sum, uint32_t seed, bool& kept, bool& ordered) {
            sim::SimulationEngine sim;
            empty_field(sim, seed);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 22});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 22});
            for (unsigned i = 0; i < 6; ++i) {
                const uint32_t e = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21 + static_cast<int32_t>(i % 3), 18 - static_cast<int32_t>(i / 3)});
                sim.get_unit(e).hp = static_cast<uint8_t>(i == 0 ? enemy_hp_sum - 20u : 4u);
            }
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(12);
            sim.get_unit(victim).hp = 9;
            const uint64_t blow = sim.current_tick();
            rig.run(12);
            const FightTask& fight = rig.as<StandardBot>().fight();
            ASSERT_TRUE(fight.fights_started() >= 1u);                                                      // (the look saw the blow)
            kept = fight.fights() == 1u;
            ordered = !proposed_since(rig, CommandType::GroupAttack, blow).empty();
            if (!kept) ASSERT_EQ(rig.as<StandardBot>().ledger().count(StandardBot::kFight), 0u);
        };
        ASSERT_EQ(plan_for(Level::Hard).defend_keep_percent, 60u);
        bool kept = false, ordered = false;
        uint32_t seed = 385;
        for (const bool group : {true, false}) {
            fight_of(group_plan(Level::Hard, group), 24, seed++, kept, ordered);
            ASSERT_TRUE(kept && ordered);
            fight_of(group_plan(Level::Hard, group), 25, seed++, kept, ordered);
            ASSERT_FALSE(kept || ordered);
            LevelPlan half = group_plan(Level::Hard, group);
            half.defend_keep_percent = 50;
            fight_of(half, 29, seed++, kept, ordered);                                                      // (87 against 29 x 6 = 174: half of it is 87, an even share is kept)
            ASSERT_TRUE(kept && ordered);
            fight_of(half, 30, seed++, kept, ordered);
            ASSERT_FALSE(kept || ordered);
        }
    } TEST_END();

    TEST_CASE("AI38.4 The Whole Path: Three Enemy Workers Strike One Ant; The Two Workers 15 Tiles Away Are Called, The Three Stand Against The Three (Strength 90 Each) And The Fight Is Kept; Without The Batch The Struck Ant Stands Alone At The Look (10 Against 90), The Fight Is Called Off And Nobody Is Ordered")
    {
        for (const bool group : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 392);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            const uint32_t far_a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 35});
            const uint32_t far_b = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 35});
            std::vector<uint32_t> enemies;
            for (int i = 0; i < 3; ++i) enemies.push_back(sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21 + i, 17}));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(group_plan(Level::Hard, group)), 4, 4);
            rig.run(3);
            Seen seen;
            size_t most = 0;
            for (int t = 0; t < 40; ++t) {
                for (const uint32_t e : enemies) keep_attacking(sim, 1, e, victim);
                rig.tick();
                for (const uint32_t e : enemies) {
                    watch_fight(rig, e, seen);
                    most = std::max(most, rig.as<StandardBot>().fight().defenders_of(e).size());
                }
            }
            ASSERT_TRUE(sim.get_unit(victim).hp < 10 || !alive(sim, victim));                              // (the premise: the ant was struck)
            size_t orders_for_far = 0;
            for (const Command* c : proposed_since(rig, CommandType::GroupAttack, 0)) orders_for_far += names(*c, far_a) || names(*c, far_b) ? 1u : 0u;
            if (group) {
                ASSERT_TRUE(seen.ever.count(far_a) != 0 && seen.ever.count(far_b) != 0);
                ASSERT_TRUE(orders_for_far >= 1u);
                ASSERT_TRUE(most >= 3u);
                ASSERT_TRUE(rig.as<StandardBot>().fight().fights() >= 1u);
            } else {
                ASSERT_TRUE(seen.ever.count(far_a) == 0 && seen.ever.count(far_b) == 0);
                ASSERT_EQ(orders_for_far, 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI38.5 The Shipped Bots Of Every Level Play As Before: The Same Counters, The Same Commands And The Same State Hash On A Fixed Scene As The Plan With The Batch Taken Out, Whatever The Number Of The Batch Says; With The Batch Turned On Hard Does Not; The Game Type 187 Plays The Brawl: With The Batch On Or Off Hard Gives The Same Commands And Hash")
    {
        struct Outcome {
            uint32_t started, orders, clip_waits, escapes, shunned;
            size_t commands;
            uint64_t hash;
            bool operator==(const Outcome& o) const {
                return started == o.started && orders == o.orders && clip_waits == o.clip_waits && escapes == o.escapes && shunned == o.shunned && commands == o.commands && hash == o.hash;
            }
        };
        const auto play = [](const LevelPlan& plan) {
            sim::SimulationEngine sim;
            empty_field(sim, 393);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            int32_t row = 21;
            for (const sim::AntType type : {sim::AntType::Thief, sim::AntType::Fire, sim::AntType::Bomber}) sim.spawn_unit(0, type, TileCoord{24, row++});
            for (int i = 0; i < 2; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20 + i, 24});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 36});                                       // (one ant in the far ring: 19 tiles from the enemy)
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
        for (const Level level : {Level::Medium, Level::Easy, Level::Hard}) {
            const Outcome shipped = play(group_plan(level, false, level == Level::Hard));
            LevelPlan odd = group_plan(level, false, level == Level::Hard);                                   // (the number of the batch turned to an extreme: with the flag off it changes nothing)
            odd.group_reach = 99;
            ASSERT_TRUE(shipped == play(odd));
            LevelPlan off = group_plan(level, false, level == Level::Hard);
            without_group_fight(off);
            ASSERT_TRUE(shipped == play(off));
            ASSERT_FALSE(plan_for(level).group_fight);
        }
        // the same scene at Hard answers differently with the batch turned on: the far ant joins
        ASSERT_FALSE(play(group_plan(Level::Hard)) == play(group_plan(Level::Hard, false)));
        // the game type 187 has no food game: the brawl plays, and the batch has nothing to change there
        const auto brawl = [](bool group) {
            sim::SimulationEngine sim;
            sim.set_game_mode(sim::GameMode::Kills187);
            empty_field(sim, 394);
            for (int i = 0; i < 5; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22 + i % 3, 20 + i / 3});
            for (int i = 0; i < 3; ++i) sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21 + i, 17});
            LevelPlan plan = plan_for(Level::Hard);
            plan.group_fight = group;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(400);
            return std::make_pair(rig.proposed.size(), sim.state_hash().total);
        };
        ASSERT_TRUE(brawl(true) == brawl(false));
    } TEST_END();

    TEST_CASE("AI38.6 The Edges: The Ants Of The Far Ring Are Called Only If A Walk Reaches The Target (A Channel Of Water Between Them: No); A Carrier Is No Defender At Any Distance, A Free Ant Of The Far Ring Is; The Struck Ant Still Answers First")
    {
        // (a) an ant 15 tiles away that no walk connects to the target: a channel of water across the whole field between them (the walk check of reach_fights, which stays on for the far ring)
        for (const bool wall : {false, true}) {
            sim::SimulationEngine sim;
            empty_field(sim, 395);
            if (wall) {
                for (int x = 0; x < 60; ++x) {
                    for (int y = 27; y <= 29; ++y) sim.set_terrain(x, y, sim::TERRAIN_WATER);
                }
            }
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            const uint32_t recruit = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 33});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{57, 57});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
            LevelPlan plan = group_plan(Level::Hard);
            plan.reach_fights = true;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(12);
            sim.get_unit(victim).hp = 9;
            Seen seen;
            for (int t = 0; t < 60; ++t) {
                rig.tick();
                watch_fight(rig, enemy, seen);
            }
            ASSERT_EQ(seen.ever.count(recruit) != 0, !wall);
        }
        // (b) a carrier and a free ant, both 15 tiles away: the free one comes, the carrier never does, and the struck ant answers first
        {
            sim::SimulationEngine sim;
            empty_field(sim, 396);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 33});
            sim.get_unit(carrier).carried_points = 20;
            sim.get_unit(carrier).holding = true;
            const uint32_t free_far = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 33});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{57, 57});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(group_plan(Level::Hard)), 4, 4);
            rig.run(12);
            sim.get_unit(victim).hp = 9;
            Seen seen;
            for (int t = 0; t < 60; ++t) {
                rig.tick();
                watch_fight(rig, enemy, seen);
            }
            ASSERT_TRUE(seen.ever.count(free_far) != 0);                                                     // (the free ant of the far ring comes)
            ASSERT_TRUE(seen.ever.count(carrier) == 0);                                                      // (the carrier never does)
            ASSERT_TRUE(seen.ever.count(victim) != 0);                                                       // (and the struck ant answers first, as before)
        }
        // (c) the reach is a rule of the fights that are sized by strength: with defend_by_strength off an ant 30 tiles away is still called (as it was), with the batch on or off
        for (const bool group : {true, false}) {
            LevelPlan plan = group_plan(Level::Hard, group);
            plan.defend_by_strength = false;
            ASSERT_TRUE(call_from(plan, Level::Hard, 0, 30, group ? 397 : 398).joined);
        }
    } TEST_END();

    TEST_CASE("AI38.7 The Rules Of The Far Ring: An Ant With 3 Hit Points Is Not Called From 15 Tiles (One With 4 Is); An Ant That A Job Holds Is Called From 15 Tiles When It Is The Only One (Not Without The Batch); A Free Ant 19 Tiles Away Is Called Before An Ant 3 Tiles Away That A Job Holds")
    {
        // (a) the hit points: the rule of can_defend holds for the far ring too
        ASSERT_FALSE(call_from(group_plan(Level::Hard), Level::Hard, 0, 15, 399, Who{3, kNoTask}).joined);
        ASSERT_TRUE(call_from(group_plan(Level::Hard), Level::Hard, 0, 15, 400, Who{4, kNoTask}).joined);
        // (b) an ant of a power-up trip (rank 3) 15 tiles away, the only ant that could come: called with the batch, out of reach without it
        ASSERT_TRUE(call_from(group_plan(Level::Hard), Level::Hard, 0, 15, 401, Who{10, StandardBot::kPowerUps}).joined);
        ASSERT_FALSE(call_from(group_plan(Level::Hard, false), Level::Hard, 0, 15, 402, Who{10, StandardBot::kPowerUps}).joined);
        // (c) one defender more than the struck ant is wanted (the level's defenders 2, no extra): a free ant 19 tiles away comes, the ant of the power-up trip 3 tiles away does not
        {
            sim::SimulationEngine sim;
            empty_field(sim, 403);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            const uint32_t job_ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 21});
            const uint32_t free_far = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 37});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{57, 57});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
            LevelPlan plan = group_plan(Level::Hard);
            plan.defenders = 2;
            plan.defend_extra = 0;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(12);
            sim.get_unit(victim).hp = 9;
            Seen seen;
            for (int t = 0; t < 60; ++t) {
                rig.as<StandardBot>().ledger_for_labs().claim(job_ant, StandardBot::kPowerUps);
                rig.tick();
                watch_fight(rig, enemy, seen);
            }
            ASSERT_TRUE(seen.ever.count(victim) != 0 && seen.ever.count(free_far) != 0);
            ASSERT_TRUE(seen.ever.count(job_ant) == 0);
            ASSERT_EQ(seen.ever.size(), size_t{2});
        }
    } TEST_END();
}
