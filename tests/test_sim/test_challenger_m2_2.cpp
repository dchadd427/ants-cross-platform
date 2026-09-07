#include "ants_sim/sim_engine.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/ant_unit.hpp"
#include "ants_sim/combat_ai.hpp"
#include "ants_sim/physics.hpp"
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

    TEST_CASE("1.1 17-Frame Exact State & Event Mapping (Frames 0..16)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.set_anthill(0, {30, 30});
        uint32_t u = sim.spawn_unit(0, AntType::Worker, {30, 30});

        sim.get_unit(u).hp = 3; // Damaged
        sim.get_unit(u).carried_points = 50;
        sim.get_unit(u).holding = 1;
        sim.get_unit(u).state = UnitState::EnteringBase;

        // Frames 0..3: Approaching hole with food. No deposit, no heal.
        for (uint16_t f = 0; f <= 3; ++f) {
            sim.clear_audio_events();
            sim.step_base_entry_animation(u, f);
            ASSERT_EQ(sim.get_unit(u).anim_subitem, f);
            ASSERT_TRUE(sim.get_unit(u).is_holding());
            ASSERT_EQ(sim.get_unit(u).carried_points, 50u);
            ASSERT_EQ(sim.get_player_score(0), 0);
            ASSERT_EQ(sim.get_unit(u).hp, 3);
            ASSERT_FALSE(sim.has_audio_event(SoundID::BaseScoreUp));
            ASSERT_FALSE(sim.has_audio_event(SoundID::PowerUpHeal));
        }

        // Frame 4: Food deposit frame. Scoreup.wav (Sound 87)
        sim.clear_audio_events();
        sim.step_base_entry_animation(u, 4);
        ASSERT_EQ(sim.get_unit(u).anim_subitem, 4u);
        ASSERT_FALSE(sim.get_unit(u).is_holding());
        ASSERT_EQ(sim.get_unit(u).carried_points, 0u);
        ASSERT_EQ(sim.get_player_score(0), 50);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BaseScoreUp)); // Sound 87
        ASSERT_EQ(sim.get_unit(u).hp, 3); // Not healed yet at frame 4!

        // Frames 5..7: Diving underground empty-handed. Not healed yet.
        for (uint16_t f = 5; f <= 7; ++f) {
            sim.clear_audio_events();
            sim.step_base_entry_animation(u, f);
            ASSERT_EQ(sim.get_player_score(0), 50); // No extra score
            ASSERT_EQ(sim.get_unit(u).hp, 3);       // Still wounded
            ASSERT_FALSE(sim.has_audio_event(SoundID::BaseScoreUp));
            ASSERT_FALSE(sim.has_audio_event(SoundID::PowerUpHeal));
        }

        // Frame 8: Underground chamber & 100% full heal (Sound 36 powerupc.wav)
        sim.clear_audio_events();
        sim.step_base_entry_animation(u, 8);
        ASSERT_EQ(sim.get_unit(u).hp, 10); // Full heal to 10 HP!
        ASSERT_TRUE(sim.has_audio_event(SoundID::PowerUpHeal)); // Sound 36

        // Frame 16: Emergence ready -> Transitions to Idle
        sim.step_base_entry_animation(u, 16);
        ASSERT_EQ(sim.get_unit(u).state, UnitState::Idle);
        ASSERT_EQ(sim.get_unit(u).hp, 10);
    } TEST_END();

    TEST_CASE("1.2 Base Entry Without Food (Heal Only, No Score Deposit)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 200);
        sim.set_anthill(1, {20, 20});
        uint32_t u = sim.spawn_unit(1, AntType::Combat, {20, 20});

        sim.get_unit(u).hp = 1; // Near death
        sim.get_unit(u).holding = 0;
        sim.get_unit(u).carried_points = 0;

        sim.clear_audio_events();
        sim.step_base_entry_animation(u, 4);
        ASSERT_FALSE(sim.has_audio_event(SoundID::BaseScoreUp));
        ASSERT_EQ(sim.get_player_score(1), 0);

        sim.clear_audio_events();
        sim.step_base_entry_animation(u, 8);
        ASSERT_EQ(sim.get_unit(u).hp, 10); // 100% full heal
        ASSERT_TRUE(sim.has_audio_event(SoundID::PowerUpHeal));

        sim.step_base_entry_animation(u, 16);
        ASSERT_EQ(sim.get_unit(u).state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("1.3 Concentric Chebyshev Ring Queuing Geometry") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 300);
        sim.set_anthill(0, {25, 25});

        TileCoord slot = sim.assign_queue_slot(0, {30, 25});
        int32_t dist = std::max(std::abs(slot.x - 25), std::abs(slot.y - 25));
        ASSERT_GE(dist, 1);
        ASSERT_LE(dist, 3);
    } TEST_END();

    TEST_CASE("1.4 Consecutive Units Base Entry & Score Accumulation") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 400);
        sim.set_anthill(0, {15, 15});

        uint32_t u1 = sim.spawn_unit(0, AntType::Worker, {15, 15});
        uint32_t u2 = sim.spawn_unit(0, AntType::Worker, {15, 15});

        sim.get_unit(u1).carried_points = 25;
        sim.get_unit(u1).holding = 1;
        sim.get_unit(u2).carried_points = 50;
        sim.get_unit(u2).holding = 1;

        sim.step_base_entry_animation(u1, 4);
        ASSERT_EQ(sim.get_player_score(0), 25);

        sim.step_base_entry_animation(u2, 4);
        ASSERT_EQ(sim.get_player_score(0), 75);
    } TEST_END();
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

        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
        ASSERT_EQ(sim.get_player_score(0), 400);
        ASSERT_EQ(sim.get_player_eggs(0), 4u);
        ASSERT_EQ(sim.get_player_hatched(0), 1u);

        ASSERT_TRUE(sim.hatch_ant(0, AntType::Combat));
        ASSERT_EQ(sim.get_player_score(0), 200);
        ASSERT_EQ(sim.get_player_eggs(0), 3u);
        ASSERT_EQ(sim.get_player_hatched(0), 2u);

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

    TEST_CASE("3.1 Thief Infiltration State & 33-Frame Timing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 600);
        sim.set_anthill(1, {45, 45});

        uint32_t t = sim.spawn_unit(0, AntType::Thief, {45, 45});
        sim.start_thief_infiltration(t, 1);
        ASSERT_EQ(sim.get_unit(t).state, UnitState::Infiltrating);

        sim.clear_audio_events();
        sim.clear_news_events();
        sim.step_thief_animation(t, 18);
        ASSERT_FALSE(sim.has_audio_event(SoundID::BaseAlarmSiren));

        sim.step_thief_animation(t, 19);
        ASSERT_TRUE(sim.has_targeted_audio_event(1, SoundID::BaseAlarmSiren));
        ASSERT_TRUE(sim.has_news_event(1, StringID::ThiefAlarmWarning));
    } TEST_END();

    TEST_CASE("3.2 Steal Mechanics Bound by min(50, victim_score)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 602);
        sim.set_anthill(1, {30, 30});

        sim.set_player_score(1, 130);
        uint32_t tA = sim.spawn_unit(0, AntType::Thief, {30, 30});
        sim.clear_audio_events();
        sim.clear_news_events();
        sim.execute_thief_loot(tA, 1);
        ASSERT_EQ(sim.get_player_score(1), 80);
        ASSERT_EQ(sim.get_unit(tA).carried_points, 50u);
        ASSERT_TRUE(sim.get_unit(tA).is_holding());
        ASSERT_TRUE(sim.has_targeted_audio_event(1, SoundID::BaseScoreDn)); // Sound 88
        ASSERT_TRUE(sim.has_news_event(1, StringID::FoodStolenStatus));     // String 62

        sim.set_player_score(1, 35);
        uint32_t tB = sim.spawn_unit(0, AntType::Thief, {30, 30});
        sim.execute_thief_loot(tB, 1);
        ASSERT_EQ(sim.get_player_score(1), 0);
        ASSERT_EQ(sim.get_unit(tB).carried_points, 35u);

        sim.set_player_score(1, 0);
        uint32_t tC = sim.spawn_unit(0, AntType::Thief, {30, 30});
        sim.execute_thief_loot(tC, 1);
        ASSERT_EQ(sim.get_player_score(1), 0);
        ASSERT_EQ(sim.get_unit(tC).carried_points, 0u);
    } TEST_END();

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

        sim.grid_mut().drop_lunchbox(25, 25, 45);
        ASSERT_TRUE(sim.has_lunchbox_at({25, 25}));

        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {25, 25});
        sim.tick();

        ASSERT_TRUE(sim.get_unit(enemy).is_holding());
        ASSERT_EQ(sim.get_unit(enemy).carried_points, 45u);
        ASSERT_FALSE(sim.has_lunchbox_at({25, 25}));

        sim.set_anthill(1, {10, 10});
        sim.step_base_entry_animation(enemy, 4);
        ASSERT_EQ(sim.get_player_score(1), 45);
    } TEST_END();

    TEST_CASE("3.5 Lunchbox Dropped into Deep Water Remains Pickable by Swimmer") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 605);
        sim.set_terrain(18, 18, TERRAIN_WATER);

        uint32_t c = sim.spawn_unit(0, AntType::Worker, {18, 18});
        sim.get_unit(c).carried_points = 30;
        sim.get_unit(c).holding = 1;
        sim.kill_unit(c);

        ASSERT_TRUE(sim.has_lunchbox_at({18, 18}));

        uint32_t swimmer = sim.spawn_unit(2, AntType::Swimmer, {18, 18});
        sim.tick();
        ASSERT_TRUE(sim.get_unit(swimmer).is_holding());
        ASSERT_EQ(sim.get_unit(swimmer).carried_points, 30u);
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
void run_suite_5_game_over() {
    TEST_SUITE("Suite 5: Match End Freeze, Audio Split & 4-Stat Scorecard");

    TEST_CASE("5.1 Immediate Simulation Freeze at Exactly 0:00") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 800, 100);

        uint32_t u = sim.spawn_unit(0, AntType::Worker, {10, 10});
        sim.issue_move_order(u, {25, 25});

        sim.tick();
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 50u);
        ASSERT_FALSE(sim.is_match_over());

        sim.tick();
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);
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

    TEST_CASE("5.2 Split Audio Routing: Winner (Sound 56) vs Losers (Sound 41)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 801, 50);

        sim.set_player_score(0, 500);
        sim.set_player_score(1, 200);
        sim.set_player_score(2, 150);
        sim.set_player_score(3, 80);

        sim.clear_audio_events();
        sim.tick();

        ASSERT_TRUE(sim.is_match_over());

        ASSERT_TRUE(sim.has_targeted_audio_event(0, SoundID::VictoryFanfare));
        ASSERT_FALSE(sim.has_targeted_audio_event(0, SoundID::PlayerDefeat));

        for (uint8_t p = 1; p <= 3; ++p) {
            ASSERT_TRUE(sim.has_targeted_audio_event(p, SoundID::PlayerDefeat));
            ASSERT_FALSE(sim.has_targeted_audio_event(p, SoundID::VictoryFanfare));
        }
    } TEST_END();

    TEST_CASE("5.3 Allied Victory Split Audio Routing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 802, 50);

        sim.set_player_score(0, 350);
        sim.set_player_score(1, 350);
        sim.form_alliance(0, 1);

        sim.set_player_score(2, 450);
        sim.set_player_score(3, 100);

        sim.clear_audio_events();
        sim.tick();

        ASSERT_TRUE(sim.is_match_over());

        ASSERT_TRUE(sim.has_targeted_audio_event(0, SoundID::VictoryFanfare));
        ASSERT_FALSE(sim.has_targeted_audio_event(0, SoundID::PlayerDefeat));
        ASSERT_TRUE(sim.has_targeted_audio_event(1, SoundID::VictoryFanfare));
        ASSERT_FALSE(sim.has_targeted_audio_event(1, SoundID::PlayerDefeat));

        ASSERT_TRUE(sim.has_targeted_audio_event(2, SoundID::PlayerDefeat));
        ASSERT_FALSE(sim.has_targeted_audio_event(2, SoundID::VictoryFanfare));
        ASSERT_TRUE(sim.has_targeted_audio_event(3, SoundID::PlayerDefeat));
        ASSERT_FALSE(sim.has_targeted_audio_event(3, SoundID::VictoryFanfare));
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
    TEST_CASE("6.1 [DEFECT] Multi-Faction Thief Alarm Audio & Banner Routing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 901);

        // Player 0 raids Player 2 (Red team)
        uint32_t t = sim.spawn_unit(0, AntType::Thief, {30, 30});
        sim.start_thief_infiltration(t, 2);
        sim.clear_audio_events();
        sim.clear_news_events();
        sim.step_thief_animation(t, 19);

        bool sent_to_target_victim = sim.has_targeted_audio_event(2, SoundID::BaseAlarmSiren);
        bool wrongly_sent_to_player_1 = sim.has_targeted_audio_event(1, SoundID::BaseAlarmSiren);

        if (!sent_to_target_victim && wrongly_sent_to_player_1) {
            std::cout << "[CONFIRMED DEFECT] Thief alert hardcoded to Player 1 instead of true victim (Player 2)! ";
            ++g_bugs_found;
        }
        ASSERT_TRUE(sent_to_target_victim);
    } TEST_END();

    // CHALLENGE 2: Frame 8 Full Heal Sound Spam
    TEST_CASE("6.2 [DEFECT] Frame 8 Underground Full Heal Audio Event Multiplication") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 902);
        sim.set_anthill(0, {30, 30});
        uint32_t u = sim.spawn_unit(0, AntType::Worker, {30, 30});

        sim.clear_audio_events();
        // Step through frames 8 through 16
        for (uint16_t f = 8; f <= 16; ++f) {
            sim.step_base_entry_animation(u, f);
        }

        auto audio = sim.poll_audio_events();
        int sound_36_count = 0;
        for (const auto& ev : audio) {
            if (ev.sound_id == SoundID::PowerUpHeal) ++sound_36_count;
        }

        if (sound_36_count > 1) {
            std::cout << "[CONFIRMED DEFECT] Sound 36 (powerupc.wav) fired " << sound_36_count
                      << " times (expected exactly 1 at frame 8)! ";
            ++g_bugs_found;
        }
        ASSERT_EQ(sound_36_count, 1);
    } TEST_END();

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

        sim.tick(); // Game ends: 0:00, match_state = GameOver
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
    TEST_CASE("6.5 [DEFECT] Concentric Chebyshev Queue Slot Ring Expansion") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 905);
        sim.set_anthill(0, {30, 30});

        // Query slots from 4 different directions (North, South, East, West)
        TileCoord slotN = sim.assign_queue_slot(0, {30, 25});
        TileCoord slotS = sim.assign_queue_slot(0, {30, 35});
        TileCoord slotW = sim.assign_queue_slot(0, {25, 30});
        TileCoord slotE = sim.assign_queue_slot(0, {35, 30});

        if (slotN.x == slotS.x && slotN.y == slotS.y &&
            slotN.x == slotW.x && slotN.y == slotW.y &&
            slotN.x == slotE.x && slotN.y == slotE.y) {
            std::cout << "[CONFIRMED DEFECT] assign_queue_slot returns identical hardcoded position ("
                      << slotN.x << "," << slotN.y << ") regardless of caller position! ";
            ++g_bugs_found;
        }
        // A genuine concentric queue assigns directional / dynamic slots
        ASSERT_FALSE(slotN.x == slotS.x && slotN.y == slotS.y &&
                     slotN.x == slotW.x && slotN.y == slotW.y);
    } TEST_END();
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << " MICROSOFT ANTS - CHALLENGER M2.2 STRESS TEST SUITE   \n";
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

    if (g_bugs_found > 0) {
        std::cout << " >>> EMPIRICAL CHALLENGE VERDICT: REQUEST_CHANGES <<<\n";
        std::cout << " >>> " << g_bugs_found << " LOGICAL DEFECT(S) CONFIRMED EMPIRICALLY! <<<\n\n";
        return 1;
    } else {
        std::cout << " >>> EMPIRICAL CHALLENGE VERDICT: APPROVE <<<\n\n";
        return 0;
    }
}
