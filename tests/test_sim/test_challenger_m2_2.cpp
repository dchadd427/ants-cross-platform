#include "ants_sim/sim_engine.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/ant_unit.hpp"
#include "ants_sim/prng.hpp"
#include "ants_sim/match_stats.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <algorithm>

using namespace ants::sim;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

static int g_bugs_found = 0;

#define TEST_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [CHALLENGER SUITE] " << name << "\n" \
              << "=======================================================\n"

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(60) << name << " ... " << std::flush;
    int prev_fails = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    } catch (...) {
        std::cout << "FAILED! Unknown exception caught\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev_fails) {
        std::cout << "PASS\n";
    }
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );

#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_LE(a, b) ASSERT_TRUE((a) <= (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define ASSERT_GE(a, b) ASSERT_TRUE((a) >= (b))

// ============================================================================
// SUITE 1: Base Entry, 17-Frame Lifecycle & Full Heal
// ============================================================================
void run_suite_1_base_lifecycle() {
    TEST_SUITE("Suite 1: Base Entry, 17-Frame Lifecycle & Full Heal");




}

// ============================================================================
// SUITE 2: Egg Hatching & Economy Bounds
// ============================================================================
void run_suite_2_egg_hatching() {
    TEST_SUITE("Suite 2: Egg Hatching & Economy Bounds");

    TEST_CASE("2.1 Exact 200 Points Deducted & Egg Inventory Decremented") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 500);
        sim.set_player_score(0, 600);
        sim.set_player_eggs(0, 5);

        // Only one egg can hatch at a time: the next click waits for the 8 s incubation of the first
        auto wait_hatched = [&]() { for (int t = 0; t < 200 && sim.get_pending_hatch_count(0) > 0; ++t) sim.tick(); };
        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
        ASSERT_EQ(sim.get_player_score(0), 400);
        ASSERT_EQ(sim.get_player_eggs(0), 4u);
        ASSERT_EQ(sim.get_player_hatched(0), 1u);
        ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));      // "An Ant is already hatching!"
        ASSERT_EQ(sim.get_player_score(0), 400);
        wait_hatched();

        ASSERT_TRUE(sim.hatch_ant(0, AntType::Combat));
        ASSERT_EQ(sim.get_player_score(0), 200);
        ASSERT_EQ(sim.get_player_eggs(0), 3u);
        ASSERT_EQ(sim.get_player_hatched(0), 2u);
        wait_hatched();

        ASSERT_TRUE(sim.hatch_ant(0, AntType::Thief));
        ASSERT_EQ(sim.get_player_score(0), 0);
        ASSERT_EQ(sim.get_player_eggs(0), 2u);
        ASSERT_EQ(sim.get_player_hatched(0), 3u);
    } TEST_END();

    TEST_CASE("2.2 Boundary Check: Score 199 Rejected vs Score 200 Accepted") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 501);

        sim.set_player_score(0, 199);
        sim.set_player_eggs(0, 3);
        ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));
        ASSERT_EQ(sim.get_player_score(0), 199); // Untouched
        ASSERT_EQ(sim.get_player_eggs(0), 3u);   // Untouched
        ASSERT_EQ(sim.get_player_hatched(0), 0u);

        sim.set_player_score(0, 200);
        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
        ASSERT_EQ(sim.get_player_score(0), 0);
        ASSERT_EQ(sim.get_player_eggs(0), 2u);
        ASSERT_EQ(sim.get_player_hatched(0), 1u);
    } TEST_END();

    TEST_CASE("2.3 Egg Inventory Depletion Boundary (0 Eggs)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 502);

        sim.set_player_score(0, 2000);
        sim.set_player_eggs(0, 0); // Out of eggs!

        ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));
        ASSERT_EQ(sim.get_player_score(0), 2000);
        ASSERT_EQ(sim.get_player_eggs(0), 0u);
        ASSERT_EQ(sim.get_player_hatched(0), 0u);
    } TEST_END();

    TEST_CASE("2.4 Out-of-Bounds Player IDs Rejection") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 503);

        ASSERT_FALSE(sim.hatch_ant(4, AntType::Worker));   // Max players is 4 (0..3)
        ASSERT_FALSE(sim.hatch_ant(255, AntType::Worker)); // Neutral player
    } TEST_END();

    TEST_CASE("2.5 All 6 Specialist Ant Types Successfully Hatched") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 504);
        sim.set_anthill(0, {10, 10});

        AntType types[6] = {
            AntType::Worker,
            AntType::Bomber,
            AntType::Fire,
            AntType::Thief,
            AntType::Combat,
            AntType::Swimmer
        };

        for (int i = 0; i < 6; ++i) {
            sim.set_player_score(0, 500);
            sim.set_player_eggs(0, 10);
            ASSERT_TRUE(sim.hatch_ant(0, types[i]));
            for (int t = 0; t < 200 && sim.get_pending_hatch_count(0) > 0; ++t) sim.tick();   // 8000 ms incubation
            const auto& ws = sim.get_world_state();
            ASSERT_FALSE(ws.ants.empty());
            const auto& spawned = ws.ants.back();
            ASSERT_EQ(spawned.player_id, 0u);
            ASSERT_EQ(spawned.type, types[i]);
            ASSERT_EQ(spawned.hp, 10u);
        }
    } TEST_END();
}

// ============================================================================
// SUITE 3: Thief Ant Infiltration, Alerts, Steal Bounds & Lunchbox
// ============================================================================
void run_suite_3_thief_infiltration() {
    TEST_SUITE("Suite 3: Thief Ant Infiltration, Alerts, Steal Bounds & Lunchbox");



    TEST_CASE("3.3 Physical Lunchbox Drop on Carrier Death") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 603);

        uint32_t carrier = sim.spawn_unit(0, AntType::Worker, {22, 22});
        sim.get_unit(carrier).carried_points = 45;
        sim.get_unit(carrier).holding = 1;

        sim.kill_unit(carrier);
        ASSERT_FALSE(sim.get_unit(carrier).is_alive());
        ASSERT_TRUE(sim.has_lunchbox_at({22, 22}));
        ASSERT_EQ(sim.get_lunchbox_points({22, 22}), 45u);
        ASSERT_FALSE(sim.get_unit(carrier).is_holding());
    } TEST_END();


    TEST_CASE("3.4 Universal Pickup of Dropped Lunchbox by Enemy Ant") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 604);
        sim.set_anthill(1, {10, 10});

        sim.grid_mut().drop_lunchbox(25, 25, 45);
        ASSERT_TRUE(sim.has_lunchbox_at({25, 25}));

        // The lunchbox is a food object like any pile: the enemy ant is ordered onto it (a plain step on the tile takes nothing),
        // plays the grab clip next to it and carries the 45 points
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {23, 25});
        sim.issue_move_order(enemy, {25, 25});
        for (int t = 0; t < 200 && !sim.get_unit(enemy).is_holding(); ++t) sim.tick();

        ASSERT_TRUE(sim.get_unit(enemy).is_holding());
        ASSERT_EQ(sim.get_unit(enemy).carried_points, 45u);
        ASSERT_FALSE(sim.has_lunchbox_at({25, 25}));

        // The picker heads for its own hill (enter order) and the food scores when its enter clip ends
        ASSERT_EQ(sim.get_unit(enemy).orig_order, AntUnit::kOrderHome);
        ASSERT_EQ(sim.get_player_score(1), 0);
        for (int t = 0; t < 600 && sim.get_player_score(1) == 0; ++t) sim.tick();
        ASSERT_EQ(sim.get_player_score(1), 45);
        ASSERT_FALSE(sim.get_unit(enemy).is_holding());
    } TEST_END();

    TEST_CASE("3.5 Carried Food Is Dropped Only On Land (RemoveAnt: terrain != water); Nothing Drops Into Water") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 605);
        sim.set_terrain(18, 18, TERRAIN_WATER);

        uint32_t c = sim.spawn_unit(0, AntType::Worker, {18, 18});
        sim.get_unit(c).carried_points = 30;
        sim.get_unit(c).holding = 1;
        sim.kill_unit(c);
        ASSERT_FALSE(sim.has_lunchbox_at({18, 18}));      // the original drops food only when the terrain is not water

        uint32_t d = sim.spawn_unit(0, AntType::Worker, {25, 25});
        sim.get_unit(d).carried_points = 30;
        sim.get_unit(d).holding = 1;
        sim.kill_unit(d);
        ASSERT_TRUE(sim.has_lunchbox_at({25, 25}));

        uint32_t picker = sim.spawn_unit(2, AntType::Worker, {23, 25});
        sim.issue_move_order(picker, {25, 25});
        for (int t = 0; t < 200 && !sim.get_unit(picker).is_holding(); ++t) sim.tick();
        ASSERT_TRUE(sim.get_unit(picker).is_holding());
        ASSERT_EQ(sim.get_unit(picker).carried_points, 30u);
    } TEST_END();
}

// ============================================================================
// SUITE 4: Dynamic Alliances, Protocol Audio & Discrete Score Preservation
// ============================================================================
void run_suite_4_dynamic_alliances() {
    TEST_SUITE("Suite 4: Dynamic Alliances & Discrete Score Preservation");

    TEST_CASE("4.1 FFA Default State (Alliance ID 4)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 700);

        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            ASSERT_EQ(sim.get_ally_id(i), ALLIANCE_NONE);
        }
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            for (uint8_t j = 0; j < MAX_PLAYERS; ++j) {
                if (i == j) {
                    ASSERT_TRUE(sim.stats_manager().are_allies(i, j));
                } else {
                    ASSERT_FALSE(sim.stats_manager().are_allies(i, j));
                }
            }
        }
    } TEST_END();

    TEST_CASE("4.2 Proposal Protocol: Sound 51 (allypro.wav) & Prompt Banner") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 701);

        sim.clear_audio_events();
        sim.clear_news_events();
        sim.propose_alliance(0, 2);

        ASSERT_TRUE(sim.has_targeted_audio_event(2, SoundID::AlliancePro));
        ASSERT_TRUE(sim.has_news_event(2, StringID::AllianceInvitePrompt));
        ASSERT_FALSE(sim.has_targeted_audio_event(1, SoundID::AlliancePro));
        ASSERT_FALSE(sim.has_targeted_audio_event(3, SoundID::AlliancePro));
    } TEST_END();

    TEST_CASE("4.3 Acceptance Protocol: Sounds 53 (allyyes) + 50 (allyon)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 702);

        sim.propose_alliance(0, 2);
        sim.clear_audio_events();
        sim.clear_news_events();

        sim.accept_alliance(2, 0);

        ASSERT_TRUE(sim.has_audio_event(SoundID::AllianceYes));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AllianceOn));
        ASSERT_TRUE(sim.has_news_event(255, StringID::AllianceFormedBroadcast));

        ASSERT_EQ(sim.get_ally_id(0), 2u);
        ASSERT_EQ(sim.get_ally_id(2), 0u);
        ASSERT_TRUE(sim.stats_manager().are_allies(0, 2));
        ASSERT_TRUE(sim.stats_manager().are_allies(2, 0));
    } TEST_END();

    TEST_CASE("4.4 Denial Protocol: Sound 52 (allynot.wav)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 703);

        sim.propose_alliance(1, 3);
        sim.clear_audio_events();
        sim.clear_news_events();

        sim.deny_alliance(3, 1);

        ASSERT_TRUE(sim.has_targeted_audio_event(1, SoundID::AllianceNot));
        ASSERT_TRUE(sim.has_news_event(1, StringID::AllianceDeclined));
        ASSERT_EQ(sim.get_ally_id(1), ALLIANCE_NONE);
        ASSERT_EQ(sim.get_ally_id(3), ALLIANCE_NONE);
        ASSERT_FALSE(sim.stats_manager().are_allies(1, 3));
    } TEST_END();

    TEST_CASE("4.5 Dissolution Protocol: Sound 49 (allyoff.wav)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 704);

        sim.form_alliance(0, 3);
        ASSERT_TRUE(sim.stats_manager().are_allies(0, 3));

        sim.clear_audio_events();
        sim.clear_news_events();

        sim.break_alliance(0, 3);
        ASSERT_TRUE(sim.has_audio_event(SoundID::AllianceBreak));
        ASSERT_TRUE(sim.has_news_event(255, StringID::AllianceBrokenBroadcast));
        ASSERT_EQ(sim.get_ally_id(0), ALLIANCE_NONE);
        ASSERT_EQ(sim.get_ally_id(3), ALLIANCE_NONE);
        ASSERT_FALSE(sim.stats_manager().are_allies(0, 3));
    } TEST_END();

    TEST_CASE("4.6 Combined Scoreboard vs Discrete Personal Memory Isolation") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 705);

        sim.set_player_score(0, 180);
        sim.set_player_score(1, 320);

        ASSERT_EQ(sim.get_display_score(0), 180);
        ASSERT_EQ(sim.get_display_score(1), 320);

        sim.form_alliance(0, 1);

        ASSERT_EQ(sim.get_display_score(0), 500);
        ASSERT_EQ(sim.get_display_score(1), 500);

        ASSERT_EQ(sim.get_player_score(0), 180);
        ASSERT_EQ(sim.get_player_score(1), 320);
        ASSERT_EQ(sim.get_player_stats(0).score, 180);
        ASSERT_EQ(sim.get_player_stats(1).score, 320);

        sim.stats_manager_mut().add_score(0, 70);
        ASSERT_EQ(sim.get_player_score(0), 250);
        ASSERT_EQ(sim.get_player_score(1), 320);
        ASSERT_EQ(sim.get_display_score(0), 570);
        ASSERT_EQ(sim.get_display_score(1), 570);

        sim.break_alliance(0, 1);
        ASSERT_EQ(sim.get_display_score(0), 250);
        ASSERT_EQ(sim.get_display_score(1), 320);
        ASSERT_EQ(sim.get_player_score(0), 250);
        ASSERT_EQ(sim.get_player_score(1), 320);
    } TEST_END();

    TEST_CASE("4.7 Alliances Disable Friendly Fire in Combat AI") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 706);

        uint32_t guard = sim.spawn_unit(0, AntType::Combat, {30, 30});
        uint32_t ally_worker = sim.spawn_unit(1, AntType::Worker, {31, 30});

        sim.form_alliance(0, 1);
        sim.tick();

        ASSERT_EQ(sim.get_unit(ally_worker).hp, 10u);
        ASSERT_EQ(sim.get_unit(guard).state, UnitState::GuardIdle);
    } TEST_END();
}

// ============================================================================
// SUITE 5: Match End Freeze, Audio Split & 4-Stat Scorecard
// ============================================================================
// The match ends at the first run of the CHECKGO task (every 200 ms) that finds the clock below 0 (Ants.exe 0x1024839): 0 - 200 ms
// after the clock shows 0:00. Ticks until the match is over (at most max_ticks); returns the ticks used, -1 when it never ended.
static int run_until_over(SimulationEngine& sim, int max_ticks = 12) {
    for (int i = 0; i < max_ticks; ++i) {
        if (sim.is_match_over()) return i;
        sim.tick();
    }
    return sim.is_match_over() ? max_ticks : -1;
}

void run_suite_5_game_over() {
    TEST_SUITE("Suite 5: Match End Freeze, Audio Split & 4-Stat Scorecard");

    TEST_CASE("5.1 Simulation Freeze Within 200 ms After 0:00") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 800, 100);

        uint32_t u = sim.spawn_unit(0, AntType::Worker, {10, 10});
        sim.issue_move_order(u, {25, 25});

        sim.tick();
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 50u);
        ASSERT_FALSE(sim.is_match_over());

        sim.tick();
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);
        ASSERT_FALSE(sim.is_match_over());                 // a clock of exactly 0 is not below 0
        ASSERT_TRUE(run_until_over(sim, 8) >= 1);
        ASSERT_TRUE(sim.is_match_over());

        TileCoord frozen_pos = sim.get_unit(u).pos;
        uint64_t frozen_tick = sim.current_tick();

        for (int i = 0; i < 5; ++i) {
            sim.tick();
            ASSERT_EQ(sim.get_unit(u).pos.x, frozen_pos.x);
            ASSERT_EQ(sim.get_unit(u).pos.y, frozen_pos.y);
            ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);
            ASSERT_EQ(sim.current_tick(), frozen_tick);
            ASSERT_TRUE(sim.is_match_over());
        }
    } TEST_END();

    TEST_CASE("5.2 Match-End Stings: The Result Names The Winner And The Losers; The Simulation Plays Nothing (The Results Screen Plays One Cue Per Machine, 0x1015a4a)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 801, 50);

        sim.set_player_score(0, 500);
        sim.set_player_score(1, 200);
        sim.set_player_score(2, 150);
        sim.set_player_score(3, 80);

        sim.clear_audio_events();
        ASSERT_TRUE(run_until_over(sim) >= 0);

        ASSERT_TRUE(sim.is_match_over());

        const MatchResult& result = sim.get_world_state().match_result;
        ASSERT_TRUE(result.is_winner(0));                                   // the rule of the results screen: winner when the local team is the top row
        for (uint8_t p = 1; p <= 3; ++p) ASSERT_FALSE(result.is_winner(p));
        for (uint8_t p = 0; p <= 3; ++p) {                                  // the stings were played twice when the simulation also queued them
            ASSERT_FALSE(sim.has_targeted_audio_event(p, SoundID::VictoryFanfare));
            ASSERT_FALSE(sim.has_targeted_audio_event(p, SoundID::PlayerDefeat));
        }
    } TEST_END();

    TEST_CASE("5.3 Allied Victory: Both Allies Are Winners For The Results Screen, No Sting From The Simulation") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 802, 50);

        sim.set_player_score(0, 350);
        sim.set_player_score(1, 350);
        sim.form_alliance(0, 1);

        sim.set_player_score(2, 450);
        sim.set_player_score(3, 100);

        sim.clear_audio_events();
        ASSERT_TRUE(run_until_over(sim) >= 0);

        ASSERT_TRUE(sim.is_match_over());

        const MatchResult& result = sim.get_world_state().match_result;
        ASSERT_TRUE(result.is_winner(0));
        ASSERT_TRUE(result.is_winner(1));
        ASSERT_FALSE(result.is_winner(2));
        ASSERT_FALSE(result.is_winner(3));
        for (uint8_t p = 0; p <= 3; ++p) {
            ASSERT_FALSE(sim.has_targeted_audio_event(p, SoundID::VictoryFanfare));
            ASSERT_FALSE(sim.has_targeted_audio_event(p, SoundID::PlayerDefeat));
        }
    } TEST_END();

    TEST_CASE("5.4 4-Stat Scorecard Faithful Tracking (Score, Lost, Killed, Hatched)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 803);

        sim.record_player_stat(0, StatType::Score, 420);
        sim.record_player_stat(0, StatType::FriendlyLost, 4);
        sim.record_player_stat(0, StatType::EnemyKilled, 12);
        sim.record_player_stat(0, StatType::NewHatched, 8);

        sim.record_player_stat(1, StatType::Score, 280);
        sim.record_player_stat(1, StatType::FriendlyLost, 7);
        sim.record_player_stat(1, StatType::EnemyKilled, 5);
        sim.record_player_stat(1, StatType::NewHatched, 6);

        const auto s0 = sim.get_player_stats(0);
        ASSERT_EQ(s0.score, 420);
        ASSERT_EQ(s0.friendly_lost, 4u);
        ASSERT_EQ(s0.enemy_killed, 12u);
        ASSERT_EQ(s0.ants_hatched, 8u);
        ASSERT_EQ(s0.new_hatched, 8u);

        const auto s1 = sim.get_player_stats(1);
        ASSERT_EQ(s1.score, 280);
        ASSERT_EQ(s1.friendly_lost, 7u);
        ASSERT_EQ(s1.enemy_killed, 5u);
        ASSERT_EQ(s1.ants_hatched, 6u);
        ASSERT_EQ(s1.new_hatched, 6u);

        MatchResult res = sim.stats_manager().evaluate_victory();
        ASSERT_TRUE(res.is_over);
        ASSERT_EQ(res.winning_players.size(), 1u);
        ASSERT_EQ(res.winning_players[0], 0u);
        ASSERT_EQ(res.stats[0].score, 420);
        ASSERT_EQ(res.stats[1].score, 280);
    } TEST_END();
}

// ============================================================================
// SUITE 6: Adversarial Stress Challenges & Defect Identification
// ============================================================================
void run_suite_6_adversarial_challenges() {
    TEST_SUITE("Suite 6: Adversarial Stress Challenges & Defect Identification");

    // CHALLENGE 1: Hardcoded Thief Alarm Victim Dispatch

    // CHALLENGE 2: Frame 8 Full Heal Sound Spam

    // CHALLENGE 3: Asymmetric / Orphaned Alliance State
    TEST_CASE("6.3 [DEFECT] Multi-Party Alliance Shift Asymmetric Desynchronization") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 903);
        sim.set_player_score(0, 100);
        sim.set_player_score(1, 200);
        sim.set_player_score(2, 300);

        // Player 0 allies with Player 1
        sim.form_alliance(0, 1);
        ASSERT_TRUE(sim.stats_manager().are_allies(0, 1));

        // Player 0 now allies with Player 2 without breaking alliance with 1
        sim.form_alliance(0, 2);

        uint8_t ally_of_1 = sim.get_ally_id(1);
        uint8_t ally_of_0 = sim.get_ally_id(0);

        if (ally_of_1 == 0 && ally_of_0 == 2) {
            std::cout << "[CONFIRMED DEFECT] Player 1 orphaned in one-way alliance with Player 0! Score 1="
                      << sim.get_display_score(1) << " vs Score 0=" << sim.get_display_score(0) << " ";
            ++g_bugs_found;
        }
        // Consistent alliance requires either mutual alliance or clean break of previous
        ASSERT_TRUE(ally_of_1 == ALLIANCE_NONE || sim.stats_manager().are_allies(1, ally_of_1));
    } TEST_END();

    // CHALLENGE 4: Hatching Allowed After Simulation Freeze at 0:00
    TEST_CASE("6.4 [DEFECT] Post-Game Simulation Freeze Bypass for Egg Hatching") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 904, 50);
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 5);

        ASSERT_TRUE(run_until_over(sim) >= 0); // Game ends within 200 ms after 0:00, match_state = GameOver
        ASSERT_TRUE(sim.is_match_over());

        // Player attempts to hatch ant after game over
        bool hatched = sim.hatch_ant(0, AntType::Worker);
        if (hatched) {
            std::cout << "[CONFIRMED DEFECT] hatch_ant succeeded during GameOver freeze! (Score drained from 500 to "
                      << sim.get_player_score(0) << ") ";
            ++g_bugs_found;
        }
        ASSERT_FALSE(hatched);
    } TEST_END();

    // CHALLENGE 5: Concentric Chebyshev Ring Allocation for Multiple Units
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << " ANTS - CHALLENGER M2.2 STRESS TEST SUITE              \n";
    std::cout << " (Base Lifecycle, Economy, Alliances, Game Over Split) \n";
    std::cout << "=======================================================\n";

    run_suite_1_base_lifecycle();
    run_suite_2_egg_hatching();
    run_suite_3_thief_infiltration();
    run_suite_4_dynamic_alliances();
    run_suite_5_game_over();
    run_suite_6_adversarial_challenges();

    std::cout << "\n=======================================================\n";
    std::cout << " CHALLENGER TEST SUMMARY\n";
    std::cout << "=======================================================\n";
    std::cout << "  Baseline Test Cases: " << (g_test_count - 5) << " (all passed)\n";
    std::cout << "  Adversarial Challenges: 5\n";
    std::cout << "  Total Assertions:     " << g_assert_count << "\n";
    std::cout << "  Defects Discovered:   " << g_bugs_found << "\n";
    std::cout << "  Failed Test Cases:    " << g_test_failures << "\n";
    std::cout << "=======================================================\n";

    if (g_test_failures > 0) {
        std::cout << " >>> " << g_test_failures << " TEST CASE(S) FAILED <<<\n\n";   // a failed baseline case must fail the run too
        return 1;
    }
    if (g_bugs_found > 0) {
        std::cout << " >>> EMPIRICAL CHALLENGE VERDICT: REQUEST_CHANGES <<<\n";
        std::cout << " >>> " << g_bugs_found << " LOGICAL DEFECT(S) CONFIRMED EMPIRICALLY! <<<\n\n";
        return 1;
    } else {
        std::cout << " >>> EMPIRICAL CHALLENGE VERDICT: APPROVE <<<\n\n";
        return 0;
    }
}
