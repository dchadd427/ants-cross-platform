// AI21: what the contest batch and the island batch of the standard bot have to agree on (docs/BOTS.md, "Task ranks"): the order in which the tasks take ants, and that the fights never
// draw the crew of the expedition away. The island task and the controller of the contest are tested together in AI15.16 and AI18.3 (no special order names a tile with an ant on it).
#include <algorithm>
#include <set>

#include "ai_test.hpp"
#include "island_fixture.hpp"

namespace {

using namespace island_test;
using namespace ants;
using namespace ants::ai;

}  // namespace

void run_merge_tests() {
    TEST_CASE("AI21.1 The Ranks Of The Tasks, Pinned (A Higher Rank May Take An Ant From A Lower One): The Economy 1, The Guard And The Ferry 2, The Power-Ups, The Raids, The Sabotage, The Island Task And The Expedition 3, The Walls, The Bombs, The Strike And The Harassment 4, The Fights 5, At Every Level")
    {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            Match m;
            m.init("TINY", 1, 0x01, level, 0);
            m.run(5);
            const AntLedger& l = m.bot(0)->ledger();
            using B = StandardBot;
            const auto rank = [&](TaskId t) { return static_cast<unsigned>(l.rank(t)); };
            ASSERT_EQ(rank(B::kHarvest), 1u);
            ASSERT_EQ(rank(B::kGuard), 2u);
            ASSERT_EQ(rank(B::kFerry), 2u);
            ASSERT_EQ(rank(B::kPowerUps), 3u);
            ASSERT_EQ(rank(B::kRaids), 3u);
            ASSERT_EQ(rank(B::kSabotage), 3u);
            ASSERT_EQ(rank(B::kIslands), 3u);
            ASSERT_EQ(rank(B::kExpedition), 3u);
            ASSERT_EQ(rank(B::kWalls), 4u);
            ASSERT_EQ(rank(B::kBombs), 4u);
            ASSERT_EQ(rank(B::kStrike), 4u);
            ASSERT_EQ(rank(B::kHarass), 4u);
            ASSERT_EQ(rank(B::kFight), 5u);
        }
    } TEST_END();

    TEST_CASE("AI21.2 The Fights Never Take The Crew Of The Expedition (The Crew Is The Only Use Of The Hill's Ants On ISLANDS For Minutes, And A Team Without A Swimmer Scores Nothing): A Hard Bot (Seat 0, Seed 1) Whose Crew Is Hit By An Enemy Combat Ant That Stands Beside It Holds No Ant In A Fight For 400 Ticks, And The Crew Stays The Expedition's")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 1, 0x03, Level::Hard, 0);
        m.run(40);                                                                                              // (the crew is chosen at the first looks)
        std::set<uint32_t> crew;
        TileCoord beside{-1, -1};
        for (const auto& a : m.sim.get_world_state().ants) {
            if (a.player_id != 0 || m.bot(0)->ledger().owner(a.id) != StandardBot::kExpedition) continue;
            crew.insert(a.id);
            if (beside.x < 0 && a.type == sim::AntType::Worker) beside = TileCoord{a.tile_x + 1, a.tile_y};
        }
        ASSERT_TRUE(crew.size() >= 5 && beside.x >= 0);
        ASSERT_TRUE(m.sim.spawn_unit(1, sim::AntType::Combat, beside) != 0);                                    // (its reflex punches the first ant of another team within three tiles)
        size_t fight_held = 0;
        size_t crew_kept = crew.size();
        for (int t = 0; t < 400; ++t) {
            m.tick();
            fight_held = std::max(fight_held, m.bot(0)->ledger().count(StandardBot::kFight));
            size_t kept = 0;
            for (const uint32_t id : crew) kept += m.bot(0)->ledger().owner(id) == StandardBot::kExpedition ? 1u : 0u;
            crew_kept = std::min(crew_kept, kept);
        }
        size_t wounded = 0;
        for (const auto& a : m.sim.get_world_state().ants) wounded += a.player_id == 0 && a.hp < 10 ? 1u : 0u;
        ASSERT_TRUE(wounded >= 1);                                                                              // (the premise: the crew was hit, and the blows started a fight that found nobody it may take)
        ASSERT_TRUE(m.bot(0)->fight().fights_started() >= 1);
        ASSERT_EQ(fight_held, 0u);
        ASSERT_TRUE(crew_kept + 2 >= crew.size());                                                              // (an ant that took a token leaves the crew: two at most in 400 ticks)
    } TEST_END();
}
