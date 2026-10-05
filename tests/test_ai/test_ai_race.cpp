// The race of the standard bot (contest batch, AI20.1): "they eat their food in a weird order ... the center food in the treasure map is contested on all four sides, so that is usually the starting
// food" (the owner) and what followed from it (docs/BOTS.md, "The race for contested food"). Hand-made worlds (b41_helpers.hpp) and TREASURE, quick enough for suite 2.20.
//
//   AI20.1  the ants that the gate cannot use go to the contested piles first (on TREASURE every level, a contested pile before a safe one of higher value, the gate keeps its own ants, a pile that
//           an enemy army holds is no race, a pile with two competitors stays one though an enemy is far nearer, the window, where nothing is contested and with fewer than six ants nothing changes)
#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "contest_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;
using namespace contest;

void run_race_tests() {
    TEST_CASE("AI20.1 The Race (Contested Food First): On TREASURE Every Seat At Every Level Sends Its Ants To The Pile In The Centre First (Easy Too); A Contested Pile Is Served Before A Richer Safe One; With The Gate Full Nobody Is Surplus And The Plain Order Stays; A Pile That An Enemy Army Holds Is No Race (Unless The Own Force Matches It); A Pile With Two Competitors Stays A Race Though One Enemy Is Far Nearer; After The Window (1200 Ticks), Where Nothing Is Contested And With Fewer Than Six Ants The Orders Are The Plain Ones")
    {
        sim::SimulationEngine probe;
        start_match(probe, "TREASURE", 7, 0x0F);
        const MapInfo pmap(probe);
        const size_t centre = treasure_centre(pmap);
        const size_t treasure_piles = probe.grid().food_objects().size();
        {   // (a) TREASURE (seed 7): the ants that the gate cannot use go to the centre first. Easy has no power-up trips, so every seat sends 3 to 4 of its 6 ants there and its first harvest order is
            //     the centre's (the owner: "they start eating the Cheerios"); Medium and Hard send three ants for power-ups first, and of the three left the centre gets the ones beyond what fills the
            //     gate: 1, 0, 3 and 1 ants at seats 0 to 3 (seat 1's three ants all work its own piles, whose trips are long); with the race off Easy sends none
            const size_t at_least[3][4] = {{3, 3, 3, 3}, {1, 0, 3, 1}, {1, 0, 3, 1}};
            for (size_t li = 0; li < 3; ++li) {
                const Level level = li == 0 ? Level::Easy : li == 1 ? Level::Medium : Level::Hard;
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    sim::SimulationEngine sim;
                    start_match(sim, "TREASURE", 7, 0x0F);
                    ASSERT_TRUE(plan_for(level).race);
                    Rig rig(sim, seat, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                    rig.run(60);
                    const std::vector<int> targets = harvest_targets(rig, sim, seat, treasure_piles);
                    ASSERT_TRUE(!targets.empty());
                    const size_t at_centre = ants_to_pile(rig, sim, treasure_piles, static_cast<int>(centre), 60);
                    ASSERT_TRUE(at_centre >= at_least[li][seat]);
                    if (at_least[li][seat] > 0) ASSERT_EQ(static_cast<size_t>(targets.front()), centre);
                }
            }
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 7, 0x0F);
            LevelPlan legacy = plan_for(Level::Easy);
            legacy.race = false;
            Rig rig(sim, 0, Level::Easy, std::make_unique<StandardBot>(legacy), 4, 4);
            rig.run(60);
            ASSERT_EQ(ants_to_pile(rig, sim, treasure_piles, static_cast<int>(centre), 60), 0u);              // (the previous order, kept selectable: Easy harvests near piles first)
            // seat 2's three ants of Medium and Hard are the race (the capped opening of v0.5.0 sends one at Medium and two at Hard, so the rows above are met by it only up to there)
            for (const Level level : {Level::Medium, Level::Hard}) {
                sim::SimulationEngine old_sim;
                start_match(old_sim, "TREASURE", 7, 0x0F);
                LevelPlan old_plan = plan_for(level);
                old_plan.race = false;
                Rig old_rig(old_sim, 2, level, std::make_unique<StandardBot>(old_plan), 4, 4);
                old_rig.run(60);
                ASSERT_TRUE(ants_to_pile(old_rig, old_sim, treasure_piles, static_cast<int>(centre), 60) < 3u);
            }
        }
        {   // (b) a contested pile before a richer safe one: the middle pile (15 a unit) before the near one (25 a unit); with the race off the richer near pile first; with at most three ants at a
            //     pile that is a race (race_ants) the rest harvest the near one
            for (const bool race : {true, false}) {
                sim::SimulationEngine sim;
                RaceWorld w;
                w.build(sim);
                LevelPlan plan = plan_for(Level::Hard);
                plan.race = race;
                plan.contest_opening_ants = 0;
                plan.race_ants = 3;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(60);
                const std::vector<int> targets = harvest_targets(rig, sim, 0, 2);
                ASSERT_TRUE(!targets.empty());
                ASSERT_EQ(targets.front(), race ? w.middle : w.near);
                ASSERT_EQ(ants_to_pile(rig, sim, 2, w.middle, 60), race ? 3u : 0u);
                ASSERT_EQ(ants_to_pile(rig, sim, 2, w.near, 60), race ? 3u : 6u);
            }
        }
        {   // (c) with the gate full nobody is surplus: a slack that the six ants cannot exceed leaves the plain order (the richer near pile first); the default (100 percent: what the gate can use) leaves the ants beyond it to the race
            sim::SimulationEngine sim;
            RaceWorld w;
            w.build(sim);
            LevelPlan plan = plan_for(Level::Hard);
            plan.contest_opening_ants = 0;
            plan.race_slack_percent = 100000;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(60);
            const std::vector<int> targets = harvest_targets(rig, sim, 0, 2);
            ASSERT_TRUE(!targets.empty());
            ASSERT_EQ(targets.front(), w.near);
            ASSERT_EQ(plan_for(Level::Hard).race_slack_percent, 100u);
        }
        {   // (d) a pile that an enemy army holds (a Combat Ant of an enemy within four tiles of it) is no race, unless the own force there matches it (two Combat Ants of ours stand at it)
            for (const int variant : {0, 1, 2}) {
                sim::SimulationEngine sim;
                RaceWorld w;
                w.build(sim);
                if (variant >= 1) sim.spawn_unit(1, sim::AntType::Combat, TileCoord{29, 29});
                if (variant == 2) {
                    sim.spawn_unit(0, sim::AntType::Combat, TileCoord{25, 25});
                    sim.spawn_unit(0, sim::AntType::Combat, TileCoord{25, 26});
                }
                LevelPlan plan = plan_for(Level::Hard);
                plan.contest_opening_ants = 0;
                plan.contest_opening_min_ants = 0;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(60);
                const std::vector<int> targets = harvest_targets(rig, sim, 0, 2);
                ASSERT_TRUE(!targets.empty());
                ASSERT_EQ(targets.front(), variant == 1 ? w.near : w.middle);
            }
        }
        {   // (e) a pile that two enemies reach as soon as the seat stays a race though a third enemy is far nearer (TREASURE seat 0: the middle pile with a contest_low of 77 percent: the enemy at
            //     238 against 314 is "there first", the two others are competitors); a pile with ONE competitor and an enemy far nearer is not (Hopeless: the side piles' business)
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 7, 0x0F);
            LevelPlan plan = plan_for(Level::Hard);
            plan.contest_low = 77;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(30);
            ASSERT_EQ(rig.as<StandardBot>().harvest().tier_of(static_cast<uint32_t>(centre)), static_cast<int>(PileClass::Multi));
            ASSERT_EQ(rig.as<StandardBot>().harvest().tier_of(1u), static_cast<int>(PileClass::Safe));         // (a pile with one competitor is no race: it reads as Safe)
            // raceone=1 (the experiment, off in every plan) makes a pile with one competitor a race too: seat 0 reads piles 1 and 6 as One, except that an enemy far nearer (contest_low 77: 238
            // against 314) is "there first": the pile is eaten before the ants arrive (Hopeless, no race, it reads as Safe) whatever one competitor there is
            for (const uint32_t low : {70u, 77u}) {
                sim::SimulationEngine one_sim;
                start_match(one_sim, "TREASURE", 7, 0x0F);
                LevelPlan one = plan_for(Level::Hard);
                one.race_one = true;
                one.contest_low = low;
                Rig one_rig(one_sim, 0, Level::Hard, std::make_unique<StandardBot>(one), 4, 4);
                one_rig.run(30);
                ASSERT_EQ(one_rig.as<StandardBot>().harvest().tier_of(static_cast<uint32_t>(centre)), static_cast<int>(PileClass::Multi));
                ASSERT_EQ(one_rig.as<StandardBot>().harvest().tier_of(1u), static_cast<int>(PileClass::One));
                ASSERT_EQ(one_rig.as<StandardBot>().harvest().tier_of(6u), static_cast<int>(low == 70u ? PileClass::One : PileClass::Safe));
            }
            ASSERT_FALSE(plan_for(Level::Hard).race_one);
        }
        {   // (f) the race lasts race_ticks from the start of the match (1200: the first minute): a look at tick 1 inside a window of 2 ticks races, a window of 1 tick has none (the plain order)
            for (const uint32_t window : {1u, 2u}) {
                sim::SimulationEngine sim;
                RaceWorld w;
                w.build(sim, 102, 6);
                LevelPlan plan = plan_for(Level::Hard);
                plan.contest_opening_ants = 0;
                plan.race_ticks = window;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(30);
                const std::vector<int> targets = harvest_targets(rig, sim, 0, 2);
                ASSERT_TRUE(!targets.empty());
                ASSERT_EQ(targets.front(), window == 1 ? w.near : w.middle);
            }
            ASSERT_EQ(plan_for(Level::Hard).race_ticks, 1200u);
        }
        {   // (g) where nothing is contested (one pile at the hill, the enemies far away) the race changes nothing: the same orders with and without it
            std::vector<std::vector<std::pair<uint64_t, Command>>> runs;
            for (const bool race : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 103);
                add_pile(sim, 12, 12, 40, 25);
                for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i % 4, 9 + i / 4});
                for (uint8_t t = 1; t < 4; ++t) sim.spawn_unit(t, sim::AntType::Worker, TileCoord{kFightHills[t].x + 3, kFightHills[t].y + 6});
                LevelPlan plan = plan_for(Level::Hard);
                plan.race = race;
                plan.contest_opening_ants = 0;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(120);
                runs.push_back(rig.proposed);
            }
            ASSERT_TRUE(runs[0] == runs[1]);
            ASSERT_TRUE(!runs[0].empty());
        }
        {   // (h) a team that cannot spare an ant (TINY starts with 3 ants, SMALL with 4) has no race: the first orders are those of a plan without it, every level and seat
            for (const char* map : {"TINY", "SMALL"}) {
                for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                    for (uint8_t seat = 0; seat < 4; ++seat) {
                        std::vector<std::vector<std::pair<uint64_t, Command>>> runs;
                        for (const bool race : {true, false}) {
                            sim::SimulationEngine sim;
                            start_match(sim, map, 7, 0x0F);
                            LevelPlan plan = plan_for(level);
                            plan.race = race;
                            Rig rig(sim, seat, level, std::make_unique<StandardBot>(plan), 4, 4);
                            rig.run(120);
                            runs.push_back(rig.proposed);
                        }
                        ASSERT_TRUE(runs[0] == runs[1]);
                        ASSERT_TRUE(!runs[0].empty());
                    }
                }
            }
        }
    } TEST_END();
}
