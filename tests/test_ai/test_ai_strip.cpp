// AI61: the contest at a flower strip (docs/BOT_CONTEST.md, "The flower strip"; plan.strip_fight, on in the plan of Hard): in hand-made worlds on SMALL (the flower at (37, 20) has open grass round it) with the real
// engine and the bot controller, and in whole matches on ISLANDS.
//
//   AI61.1  an edge: three own ants wait at the flower and one enemy ant waits there: they strike it (every blow throws it a tile away: it is driven off the strip, hurt); with the rule off nobody strikes
//   AI61.2  no edge: one own ant waits at the flower, three enemy ants wait there: the own ant leaves (walks home) and the strip is shut to the bot's swarm for plan.strip_close_ticks; with the rule off it stays
//   AI61.3  help from the posts near: one own ant waits at the flower, two enemy ants wait there, two own ants stand eight tiles away: with strip_help_reach 12 they join (an edge, three against two) and strike; with it 3 the post is alone and leaves
//   AI61.4  holding: with no enemy ant at the strip the own ants stay where they wait and no order is given
//   AI61.5  the plans that ship have the rule off, and whole matches on ISLANDS with it on replay from their commands alone
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
    p.strip_leave_percent = 80;                                                                  // (the plan that ships never leaves; the tests of the leave play it)
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

    TEST_CASE("AI61.2 No Edge: One Own Ant Waits At The Flower, Three Enemy Ants Wait There: The Own Ant Leaves (It Walks Home) And The Strip Is Shut To The Bot's Swarm For plan.strip_close_ticks; With The Rule Off It Stays") {
        for (const bool rule : {true, false}) {
            MapWorld w(plan_strip([rule](LevelPlan& p) { p.strip_fight = rule; }), "SMALL");
            const TileCoord drop = w.drop_near(37);
            const uint32_t ant = post(w, TileCoord{drop.x - 2, drop.y});
            for (int i = 0; i < 3; ++i) w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{drop.x + 1, drop.y - 1 + i});
            w.run(400);
            const sim::AntSnapshot* a = snapshot_of(w.sim, ant);
            ASSERT_TRUE(a != nullptr);
            const int32_t away = std::max(std::abs(a->tile_x - drop.x), std::abs(a->tile_y - drop.y));
            if (rule) {
                ASSERT_TRUE(away > 6);
                ASSERT_TRUE(w.bot().strip().leaves() >= 1u);
                ASSERT_TRUE(w.bot().tactics().strip_closed(drop, w.sim.current_tick()));                   // (shut for 600 ticks from the leave)
                ASSERT_FALSE(w.bot().tactics().strip_closed(drop, w.sim.current_tick() + 700));
            } else {
                ASSERT_TRUE(away <= 3);
                ASSERT_EQ(w.bot().strip().leaves(), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI61.3 Help From The Posts Near: One Own Ant Waits At The Flower, Two Enemy Ants Wait There, Two Own Ants Stand Eight Tiles Away: With A Reach Of 12 They Join (Three Against Two) And Strike; With A Reach Of 3 The Post Is Alone And Leaves") {
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
                ASSERT_TRUE(w.bot().strip().leaves() >= 1u);
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

    TEST_CASE("AI61.5 The Plan Of Hard Has The Rule On (And Never Leaves), The Plans Of Easy And Medium And A Plan Made By Hand Have It Off, `idlebatch=0` Takes It Out; Whole Matches On ISLANDS Replay From Their Commands Alone, And A Hard Bot Of The Plan That Ships Strikes At A Strip") {
        ASSERT_TRUE(plan_for(Level::Hard).strip_fight);
        ASSERT_EQ(plan_for(Level::Hard).strip_leave_percent, 0u);
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
}
