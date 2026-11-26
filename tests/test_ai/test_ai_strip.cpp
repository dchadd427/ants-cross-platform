// AI61: the contest at a flower strip (docs/BOT_CONTEST.md, "The flower strip"; plan.strip_fight, on in the plan of Hard): in hand-made worlds on SMALL (the flower at (37, 20) has open grass round it) with the real
// engine and the bot controller, and in whole matches on ISLANDS.
//
//   AI61.1  an edge: three own ants wait at the flower and one enemy ant waits there: they strike it (every blow throws it a tile away: it is driven off the strip, hurt); with the rule off nobody strikes
//   AI61.2  no edge: one own ant waits at the flower, three enemy ants wait there: nobody strikes and the own ant stays where it waits (the rule never leaves a strip)
//   AI61.3  help from the posts near: one own ant waits at the flower, two enemy ants wait there, two own ants stand eight tiles away: with strip_help_reach 12 they join (an edge, three against two) and strike; with 3 they do not
//   AI61.4  holding: with no enemy ant at the strip the own ants stay where they wait and no order is given
//   AI61.5  the plan of Hard has the rule on, the plans of Easy and Medium and a plan made by hand have it off; whole matches on ISLANDS with it on replay from their commands alone
//   AI61.6  the weakest first: two enemy ants wait at the flower, the one nearer to the drop tile whole and the other with 3 hit points: the first blow lands on the weak one
//   AI61.7  an enemy ant that stands on a power-up is immune and is left alone (with the same enemy ant beside the power-up the own ants strike it)
//   AI61.8  who counts on the own side: Swimmers and ants with fewer than 5 hit points do not fight and do not add to the strength (three of them beside one whole ant make no edge against a whole enemy ant; three whole ants do)
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_ai/strip_task.hpp"
#include "b41_helpers.hpp"
#include "contest_world.hpp"

namespace {

using namespace contest_world;

LevelPlan plan_strip(const std::function<void(LevelPlan&)>& tweak = {}) {
    LevelPlan p = peace_plan(Level::Hard);
    p.strip_fight = true;
    p.islands = false;                                                                           // (SMALL has water: the island play of the level has nothing to do in these worlds)
    p.island_flower_link = false;
    p.flower_watch = false;
    if (tweak) tweak(p);
    return p;
}

// An own ant that waits at a strip as the swarm leaves it (the economy would send an idle ant to a pile): the ledger gives it to the swarm
uint32_t post(MapWorld& w, TileCoord at) {
    const uint32_t id = w.sim.spawn_unit(0, sim::AntType::Worker, at);
    w.bot().ledger_for_labs().claim(id, StandardBot::kSwarm);
    return id;
}

}  // namespace

void run_strip_tests() {
    TEST_CASE("AI61.1 An Edge: Three Own Ants Wait At The Flower And One Enemy Ant Waits There (Nine Times Its Strength): They Strike It, Every Blow Throws It A Tile Away, And It Is Driven Off The Strip Hurt; With The Rule Off Nobody Strikes") {
        for (const bool rule : {true, false}) {
            MapWorld w(plan_strip([rule](LevelPlan& p) { p.strip_fight = rule; }), "SMALL");
            const TileCoord drop = w.drop_near(37);
            for (int i = 0; i < 3; ++i) post(w, TileCoord{drop.x - 2, drop.y - 1 + i});
            const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{drop.x + 1, drop.y});
            uint16_t lowest = 10;
            w.run(500, [&]() { lowest = std::min(lowest, hp_of(w.sim, enemy)); });
            if (rule) {
                const sim::AntSnapshot* e = snapshot_of(w.sim, enemy);                             // (every blow throws a worker one tile: it is driven off the strip, hurt, and the fight is over)
                ASSERT_TRUE(lowest <= 6u);
                ASSERT_TRUE(e != nullptr && std::max(std::abs(e->tile_x - drop.x), std::abs(e->tile_y - drop.y)) > 3);
                ASSERT_EQ(w.bot().strip().held(), 0u);                                               // (and the ants are the swarm's again)
                ASSERT_TRUE(w.bot().strip().strikes() >= 1u && w.bot().strip().attacks() >= 1u);
            } else {
                ASSERT_EQ(lowest, 10u);
                ASSERT_EQ(w.bot().strip().attacks(), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI61.2 No Edge: One Own Ant Waits At The Flower, Three Enemy Ants Wait There: Nobody Strikes (The Own Ant Alone Would Lose), No Enemy Ant Is Touched And The Own Ant Stays Where It Waits: The Rule Never Leaves A Strip") {
        MapWorld w(plan_strip(), "SMALL");
        const TileCoord drop = w.drop_near(37);
        const uint32_t ant = post(w, TileCoord{drop.x - 2, drop.y});
        std::vector<uint32_t> enemies;
        for (int i = 0; i < 3; ++i) enemies.push_back(w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{drop.x + 1, drop.y - 1 + i}));
        uint16_t lowest = 10;
        w.run(400, [&]() {
            for (const uint32_t e : enemies) lowest = std::min(lowest, hp_of(w.sim, e));
        });
        const sim::AntSnapshot* a = snapshot_of(w.sim, ant);
        ASSERT_TRUE(a != nullptr);
        ASSERT_TRUE(std::max(std::abs(a->tile_x - drop.x), std::abs(a->tile_y - drop.y)) <= 3);
        ASSERT_TRUE(w.bot().strip().contests() >= 1u);                                           // (the contest was looked at ...)
        ASSERT_EQ(w.bot().strip().strikes(), 0u);                                                // (... and no blow was given)
        ASSERT_EQ(w.bot().strip().attacks(), 0u);
        ASSERT_EQ(lowest, 10u);
    } TEST_END();

    TEST_CASE("AI61.3 Help From The Posts Near: One Own Ant Waits At The Flower, Two Enemy Ants Wait There, Two Own Ants Stand Eight Tiles Away: With A Reach Of 12 They Join (Three Against Two) And Strike; With A Reach Of 3 The Post Is Alone And Nobody Strikes") {
        for (const int reach : {12, 3}) {
            MapWorld w(plan_strip([reach](LevelPlan& p) { p.strip_help_reach = reach; }), "SMALL");
            const TileCoord drop = w.drop_near(37);
            post(w, TileCoord{drop.x - 2, drop.y});
            post(w, TileCoord{drop.x - 8, drop.y});                                              // (the posts at the other strip, as the swarm leaves them)
            post(w, TileCoord{drop.x - 8, drop.y + 2});
            const uint32_t e1 = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{drop.x + 1, drop.y - 1});
            const uint32_t e2 = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{drop.x + 1, drop.y + 1});
            uint16_t lowest = 20;
            w.run(500, [&]() { lowest = std::min<uint16_t>(lowest, static_cast<uint16_t>(hp_of(w.sim, e1) + hp_of(w.sim, e2))); });
            if (reach == 12) {
                ASSERT_TRUE(w.bot().strip().helped() >= 1u);
                ASSERT_TRUE(lowest < 20u);
            } else {
                ASSERT_EQ(w.bot().strip().helped(), 0u);
                ASSERT_EQ(lowest, 20u);
                ASSERT_EQ(w.bot().strip().strikes(), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI61.4 Holding: With No Enemy Ant At The Strip The Own Ants Stay Where They Wait And No Order Is Given") {
        MapWorld w(plan_strip(), "SMALL");
        const TileCoord drop = w.drop_near(37);
        std::vector<uint32_t> ants;
        for (int i = 0; i < 3; ++i) ants.push_back(post(w, TileCoord{drop.x - 2, drop.y - 1 + i}));
        w.run(300);
        for (size_t i = 0; i < ants.size(); ++i) {
            const sim::AntSnapshot* a = snapshot_of(w.sim, ants[i]);
            ASSERT_TRUE(a != nullptr && std::max(std::abs(a->tile_x - drop.x), std::abs(a->tile_y - drop.y)) <= 5);
        }
        ASSERT_EQ(w.bot().strip().attacks(), 0u);
        ASSERT_EQ(w.bot().strip().contests(), 0u);
    } TEST_END();

    TEST_CASE("AI61.5 The Plan Of Hard Has The Rule On, The Plans Of Easy And Medium And A Plan Made By Hand Have It Off, `idlebatch=0` Takes It Out; Whole Matches On ISLANDS Replay From Their Commands Alone, And A Hard Bot Of The Plan That Ships Strikes At A Strip") {
        ASSERT_TRUE(plan_for(Level::Hard).strip_fight);
        ASSERT_FALSE(plan_for(Level::Easy).strip_fight || plan_for(Level::Medium).strip_fight);
        LevelPlan before = plan_for(Level::Hard);
        without_idle_batch(before);
        ASSERT_FALSE(before.strip_fight);
        const LevelPlan by_hand;
        ASSERT_FALSE(by_hand.strip_fight);
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/ISLANDS.LVL"));
        for (const uint32_t seed : {3u, 4u}) {
            ArenaSpec spec;
            spec.level = &level;
            spec.seed = seed;
            spec.record = true;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            spec.factory = [](const BotSpec& b) {
                return std::unique_ptr<Bot>(std::make_unique<StandardBot>(b.level, Style::Random, [](LevelPlan& plan) { plan.strip_fight = true; }));
            };
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_TRUE(replay_commands(spec, r).ok);
            for (const ArenaSeatResult& seat : r.seats) {
                ASSERT_EQ(seat.stats.filtered, 0u);
                ASSERT_EQ(seat.stats.rejected, 0u);
            }
        }
        uint32_t strikes = 0;                                                                    // the bots of the plan that ships (no tuning at all): they strike at a strip
        for (const uint32_t seed : {3u, 4u}) {
            ArenaSpec spec;
            spec.level = &level;
            spec.seed = seed;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            for (const ArenaSeatResult& seat : r.seats) strikes += seat.fight_batch.strip_strikes;
        }
        ASSERT_TRUE(strikes >= 1u);
    } TEST_END();

    TEST_CASE("AI61.6 The Weakest First: Two Enemy Ants Wait At The Flower, The One Nearer To The Drop Tile Whole And The One Farther Away With 3 Hit Points, Three Own Ants Wait There: The First Blow Lands On The Weak One") {
        MapWorld w(plan_strip(), "SMALL");
        const TileCoord drop = w.drop_near(37);
        for (int i = 0; i < 3; ++i) post(w, TileCoord{drop.x - 2, drop.y - 1 + i});
        const uint32_t whole = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{drop.x, drop.y - 1});
        const uint32_t weak = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{drop.x, drop.y - 3});
        w.sim.get_unit(weak).hp = 3;
        bool weak_first = false;
        bool whole_first = false;
        w.run(300, [&]() {
            if (weak_first || whole_first) return;
            if (hp_of(w.sim, weak) < 3u) weak_first = true;                                      // (the same tick counts for the weak one)
            else if (hp_of(w.sim, whole) < 10u) whole_first = true;
        });
        ASSERT_TRUE(weak_first);
        ASSERT_FALSE(whole_first);
    } TEST_END();

    TEST_CASE("AI61.7 An Enemy Ant That Stands On A Power-Up Is Immune And Is Left Alone: Three Own Ants Wait At The Flower And One Enemy Ant Stands On A Power-Up Beside It, No Order Is Given And It Is Not Touched; With The Power-Up Two Tiles Away From The Same Enemy Ant The Own Ants Strike It") {
        for (const bool on_powerup : {true, false}) {
            MapWorld w(plan_strip(), "SMALL");
            const TileCoord drop = w.drop_near(37);
            for (int i = 0; i < 3; ++i) post(w, TileCoord{drop.x - 2, drop.y - 1 + i});
            const TileCoord spot{drop.x + 1, drop.y};
            w.sim.grid_mut().place_powerup(on_powerup ? spot.x : spot.x + 1, on_powerup ? spot.y : spot.y + 2, 5);          // (a Swimmer power-up: nothing that the plan wants)
            const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Worker, spot);
            uint16_t lowest = 10;
            w.run(300, [&]() { lowest = std::min(lowest, hp_of(w.sim, enemy)); });
            if (on_powerup) {
                ASSERT_EQ(w.bot().strip().contests(), 0u);
                ASSERT_EQ(w.bot().strip().attacks(), 0u);
                ASSERT_EQ(lowest, 10u);
            } else {
                ASSERT_TRUE(w.bot().strip().attacks() >= 1u);
                ASSERT_TRUE(lowest < 10u);
            }
        }
    } TEST_END();

    TEST_CASE("AI61.8 Who Counts On The Own Side: One Whole Own Ant Waits At The Flower Beside Three Swimmers, Or Beside Three Ants With 4 Hit Points, And One Whole Enemy Ant Waits There: No Edge, No Blow; Beside Three Whole Ants It Is An Edge And They Strike") {
        for (const int kind : {0, 1, 2}) {                                                       // 0 three Swimmers, 1 three ants with 4 hit points, 2 three whole ants
            MapWorld w(plan_strip(), "SMALL");
            const TileCoord drop = w.drop_near(37);
            post(w, TileCoord{drop.x - 2, drop.y});
            for (int i = 0; i < 3; ++i) {
                const TileCoord at{drop.x - 2, drop.y - 2 + 2 * i + (i == 1 ? 1 : 0)};
                if (kind == 0) {
                    w.sim.spawn_unit(0, sim::AntType::Swimmer, at);
                } else if (kind == 1) {
                    w.sim.get_unit(w.sim.spawn_unit(0, sim::AntType::Worker, at)).hp = 4;
                } else {
                    post(w, at);
                }
            }
            const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{drop.x + 1, drop.y});
            uint16_t lowest = 10;
            w.run(200, [&]() { lowest = std::min(lowest, hp_of(w.sim, enemy)); });                // (a Worker hatches at tick 240 and walks to the strip: one more whole ant is an edge)
            ASSERT_TRUE(w.bot().strip().contests() >= 1u);
            if (kind == 2) {
                ASSERT_TRUE(w.bot().strip().strikes() >= 1u);
                ASSERT_TRUE(lowest < 10u);
            } else {
                ASSERT_EQ(w.bot().strip().strikes(), 0u);
                ASSERT_EQ(w.bot().strip().attacks(), 0u);
                ASSERT_EQ(lowest, 10u);
            }
        }
    } TEST_END();
}
