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
#include <set>
#include <string>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <algorithm>

using namespace ants::sim;

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_assert_count = 0;

#define STRESS_TEST(name) \
    do { \
        ++g_tests_run; \
        std::cout << "  [STRESS TEST] " << std::left << std::setw(60) << name << " ... " << std::flush; \
        bool _ok = true;

#define STRESS_END() \
        if (_ok) { \
            ++g_tests_passed; \
            std::cout << "PASS\n"; \
        } else { \
            ++g_tests_failed; \
            std::cout << "FAILED!\n"; \
        } \
    } while (0);

#define S_ASSERT(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "\n    ASSERTION FAILED: " #cond << " at line " << __LINE__ << "\n    "; \
            _ok = false; \
        } \
    } while (0)

#define S_ASSERT_EQ(a, b) S_ASSERT((a) == (b))
#define S_ASSERT_NE(a, b) S_ASSERT((a) != (b))
#define S_ASSERT_LT(a, b) S_ASSERT((a) < (b))
#define S_ASSERT_LE(a, b) S_ASSERT((a) <= (b))
#define S_ASSERT_GT(a, b) S_ASSERT((a) > (b))
#define S_ASSERT_GE(a, b) S_ASSERT((a) >= (b))
#define S_ASSERT_TRUE(a)  S_ASSERT((a))
#define S_ASSERT_FALSE(a) S_ASSERT(!(a))

// ============================================================================
// 1. Concentric Chebyshev Queuing Deep Stress
// ============================================================================
void test_concentric_chebyshev_deep() {
    std::cout << "\n=======================================================\n"
              << " 1. Concentric Chebyshev Queuing Deep Stress\n"
              << "=======================================================\n";

    // 1.1 Approach directions N, S, E, W, NE, NW, SE, SW
    STRESS_TEST("1.1 Directional Approach Ring 1 Slot Differentiation") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1001);
        sim.set_anthill(0, {30, 30});

        TileCoord slotN  = sim.assign_queue_slot(0, {30, 20});
        sim.clear_reserved_queue_slots();
        TileCoord slotS  = sim.assign_queue_slot(0, {30, 40});
        sim.clear_reserved_queue_slots();
        TileCoord slotE  = sim.assign_queue_slot(0, {40, 30});
        sim.clear_reserved_queue_slots();
        TileCoord slotW  = sim.assign_queue_slot(0, {20, 30});
        sim.clear_reserved_queue_slots();

        TileCoord slotNE = sim.assign_queue_slot(0, {40, 20});
        sim.clear_reserved_queue_slots();
        TileCoord slotNW = sim.assign_queue_slot(0, {20, 20});
        sim.clear_reserved_queue_slots();
        TileCoord slotSE = sim.assign_queue_slot(0, {40, 40});
        sim.clear_reserved_queue_slots();
        TileCoord slotSW = sim.assign_queue_slot(0, {20, 40});
        sim.clear_reserved_queue_slots();

        // Check each has Chebyshev distance 1 from {30, 30}
        auto chebyshev_dist = [](TileCoord a, TileCoord b) {
            return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
        };

        S_ASSERT_EQ(chebyshev_dist(slotN, {30, 30}), 1);
        S_ASSERT_EQ(chebyshev_dist(slotS, {30, 30}), 1);
        S_ASSERT_EQ(chebyshev_dist(slotE, {30, 30}), 1);
        S_ASSERT_EQ(chebyshev_dist(slotW, {30, 30}), 1);
        S_ASSERT_EQ(chebyshev_dist(slotNE, {30, 30}), 1);
        S_ASSERT_EQ(chebyshev_dist(slotNW, {30, 30}), 1);
        S_ASSERT_EQ(chebyshev_dist(slotSE, {30, 30}), 1);
        S_ASSERT_EQ(chebyshev_dist(slotSW, {30, 30}), 1);

        // Directional alignment checks
        S_ASSERT_LT(slotN.y, 30); // North slot is above anthill
        S_ASSERT_GT(slotS.y, 30); // South slot is below anthill
        S_ASSERT_GT(slotE.x, 30); // East slot is right of anthill
        S_ASSERT_LT(slotW.x, 30); // West slot is left of anthill

        // Pairwise uniqueness among cardinal directions
        S_ASSERT_NE(slotN, slotS);
        S_ASSERT_NE(slotE, slotW);
        S_ASSERT_NE(slotN, slotE);
        S_ASSERT_NE(slotS, slotW);
    } STRESS_END();

    // 1.2 Ring 1 Saturation (8 slots) and Ring 2 Expansion
    STRESS_TEST("1.2 Ring 1 Full Saturation (8 units) & Expansion to Ring 2") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1002);
        sim.set_anthill(0, {30, 30});

        auto chebyshev_dist = [](TileCoord a, TileCoord b) {
            return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
        };

        std::set<std::pair<int32_t, int32_t>> assigned_slots;

        // Allocate 8 slots: all must be in Ring 1 and all distinct
        for (int i = 0; i < 8; ++i) {
            // Approach from various directions
            TileCoord approach{30 + (i % 3 - 1) * 10, 30 + (i / 3 - 1) * 10};
            if (approach == TileCoord{30, 30}) approach = {30, 20};
            TileCoord slot = sim.assign_queue_slot(0, approach);

            S_ASSERT_EQ(chebyshev_dist(slot, {30, 30}), 1);
            S_ASSERT_TRUE(sim.is_queue_slot_reserved(slot));
            auto insert_res = assigned_slots.insert({slot.x, slot.y});
            S_ASSERT_TRUE(insert_res.second); // Must be strictly unique!
        }
        S_ASSERT_EQ(assigned_slots.size(), 8u);

        // Now allocate 9th slot: Ring 1 is completely saturated!
        // It MUST expand to Ring 2 (Chebyshev distance 2)
        TileCoord slot9 = sim.assign_queue_slot(0, {30, 20});
        S_ASSERT_EQ(chebyshev_dist(slot9, {30, 30}), 2);
        S_ASSERT_TRUE(sim.is_queue_slot_reserved(slot9));
        assigned_slots.insert({slot9.x, slot9.y});
        S_ASSERT_EQ(assigned_slots.size(), 9u);

        // Allocate remaining 15 slots in Ring 2 (total 16 slots in Ring 2)
        for (int i = 10; i <= 24; ++i) {
            TileCoord slot_r2 = sim.assign_queue_slot(0, {30, 15});
            S_ASSERT_EQ(chebyshev_dist(slot_r2, {30, 30}), 2);
            S_ASSERT_TRUE(sim.is_queue_slot_reserved(slot_r2));
            auto res = assigned_slots.insert({slot_r2.x, slot_r2.y});
            S_ASSERT_TRUE(res.second);
        }
        S_ASSERT_EQ(assigned_slots.size(), 24u); // 8 on Ring 1 + 16 on Ring 2

        // Slot 25 must expand to Ring 3 (Chebyshev distance 3)
        TileCoord slot25 = sim.assign_queue_slot(0, {30, 10});
        S_ASSERT_EQ(chebyshev_dist(slot25, {30, 30}), 3);
    } STRESS_END();

    // 1.3 Obstacle Avoidance on Queue Rings
    STRESS_TEST("1.3 Obstacle Avoidance (Obstacles on Queue Ring)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1003);
        sim.set_anthill(0, {30, 30});

        auto chebyshev_dist = [](TileCoord a, TileCoord b) {
            return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
        };

        // Surround anthill with obstacles on 7 of the 8 Ring 1 positions, leaving only (31, 30) passable
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                if (dx == 1 && dy == 0) continue; // Keep {31, 30} passable
                sim.set_terrain(30 + dx, 30 + dy, TERRAIN_OBSTACLE);
            }
        }

        // Even approaching from North {30, 20}, {30, 29} is OBSTACLE, so it must pick {31, 30}
        TileCoord slot1 = sim.assign_queue_slot(0, {30, 20});
        S_ASSERT_EQ(slot1, (TileCoord{31, 30}));
        S_ASSERT_EQ(chebyshev_dist(slot1, {30, 30}), 1);

        // Next request from any direction has NO passable tiles left on Ring 1!
        // It must cleanly expand to Ring 2 without crashing or assigning an obstacle
        TileCoord slot2 = sim.assign_queue_slot(0, {30, 20});
        S_ASSERT_EQ(chebyshev_dist(slot2, {30, 30}), 2);
        const auto& cell = sim.grid().get_cell(static_cast<uint32_t>(slot2.x), static_cast<uint32_t>(slot2.y));
        S_ASSERT_TRUE(cell.is_passable(false, false));
    } STRESS_END();

    // 1.4 Reservation and Release Lifecycle
    STRESS_TEST("1.4 Reservation Release and Reassignment Lifecycle") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1004);
        sim.set_anthill(0, {30, 30});

        TileCoord slotA = sim.assign_queue_slot(0, {30, 20});
        S_ASSERT_TRUE(sim.is_queue_slot_reserved(slotA));

        // Release slotA
        sim.release_queue_slot(slotA);
        S_ASSERT_FALSE(sim.is_queue_slot_reserved(slotA));

        // Assigning again from same direction should reclaim slotA
        TileCoord slotB = sim.assign_queue_slot(0, {30, 20});
        S_ASSERT_EQ(slotA, slotB);
        S_ASSERT_TRUE(sim.is_queue_slot_reserved(slotB));
    } STRESS_END();
}

// ============================================================================
// 2. Frame 8 Heal Sound Strictly Once Stress
// ============================================================================
void test_frame8_heal_sound_deep() {
    std::cout << "\n=======================================================\n"
              << " 2. Frame 8 Heal Sound Strictly Once Stress\n"
              << "=======================================================\n";

    STRESS_TEST("2.1 Stepping entering ant through frames 8 to 16 yields exactly 1 Sound 36") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 2001);
        sim.set_anthill(0, {30, 30});
        uint32_t u = sim.spawn_unit(0, AntType::Worker, {30, 30});

        sim.get_unit(u).hp = 2;
        sim.get_unit(u).state = UnitState::EnteringBase;

        sim.clear_audio_events();

        // Step through frames 8 to 16
        for (uint16_t f = 8; f <= 16; ++f) {
            sim.step_base_entry_animation(u, f);
        }

        auto audio = sim.poll_audio_events();
        int sound_36_count = 0;
        for (const auto& ev : audio) {
            if (ev.sound_id == SoundID::PowerUpHeal) {
                ++sound_36_count;
                S_ASSERT_EQ(ev.target_player, 0);
            }
        }
        S_ASSERT_EQ(sound_36_count, 1);
        S_ASSERT_EQ(sim.get_unit(u).hp, 10);
    } STRESS_END();

    STRESS_TEST("2.2 Frame-by-Frame Isolation across entire 0..16 lifecycle") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 2002);
        sim.set_anthill(0, {30, 30});
        uint32_t u = sim.spawn_unit(0, AntType::Combat, {30, 30});

        sim.get_unit(u).hp = 4;
        sim.get_unit(u).carried_points = 50;
        sim.get_unit(u).holding = 1;
        sim.get_unit(u).state = UnitState::EnteringBase;

        int total_heal_sounds = 0;
        int total_deposit_sounds = 0;

        for (uint16_t f = 0; f <= 16; ++f) {
            sim.step_base_entry_animation(u, f);
            auto events = sim.poll_audio_events();
            for (const auto& ev : events) {
                if (ev.sound_id == SoundID::PowerUpHeal) {
                    S_ASSERT_EQ(f, 8u); // Sound 36 must ONLY fire at frame 8
                    ++total_heal_sounds;
                }
                if (ev.sound_id == SoundID::BaseScoreUp) {
                    S_ASSERT_EQ(f, 4u); // Sound 87 must ONLY fire at frame 4
                    ++total_deposit_sounds;
                }
            }
        }

        S_ASSERT_EQ(total_heal_sounds, 1);
        S_ASSERT_EQ(total_deposit_sounds, 1);
        S_ASSERT_EQ(sim.get_unit(u).hp, 10);
        S_ASSERT_EQ(sim.get_unit(u).state, UnitState::Idle);
    } STRESS_END();

    STRESS_TEST("2.3 Multiple Ants Concurrently Stepping Entry Frames") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 2003);
        sim.set_anthill(0, {30, 30});

        uint32_t u1 = sim.spawn_unit(0, AntType::Worker, {30, 30});
        uint32_t u2 = sim.spawn_unit(0, AntType::Worker, {30, 30});

        sim.get_unit(u1).hp = 2;
        sim.get_unit(u2).hp = 3;

        sim.clear_audio_events();

        // u1 at frame 8 (should heal and emit Sound 36)
        sim.step_base_entry_animation(u1, 8);
        // u2 at frame 9 (should NOT emit Sound 36)
        sim.step_base_entry_animation(u2, 9);

        auto audio = sim.poll_audio_events();
        int sound_36_count = 0;
        for (const auto& ev : audio) {
            if (ev.sound_id == SoundID::PowerUpHeal) ++sound_36_count;
        }
        S_ASSERT_EQ(sound_36_count, 1);
        S_ASSERT_EQ(sim.get_unit(u1).hp, 10);
        S_ASSERT_EQ(sim.get_unit(u2).hp, 3); // u2 was stepped directly to 9, so frame 8 wasn't triggered
    } STRESS_END();
}

// ============================================================================
// 3. Post-Match Freeze on Egg Hatching Stress
// ============================================================================
void test_post_match_freeze_deep() {
    std::cout << "\n=======================================================\n"
              << " 3. Post-Match Freeze on Egg Hatching Stress\n"
              << "=======================================================\n";

    STRESS_TEST("3.1 Pre-GameOver (50ms) vs Post-GameOver (0ms) Boundary") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 3001, 100); // 100ms = 2 ticks

        sim.set_player_score(0, 1000);
        sim.set_player_eggs(0, 5);

        // Tick 1: 50 ms remaining
        sim.tick();
        S_ASSERT_EQ(sim.get_match_time_remaining_ms(), 50u);
        S_ASSERT_FALSE(sim.is_match_over());

        // Pre-gameover hatch succeeds
        bool h1 = sim.hatch_ant(0, AntType::Worker);
        S_ASSERT_TRUE(h1);
        S_ASSERT_EQ(sim.get_player_score(0), 800);
        S_ASSERT_EQ(sim.get_player_eggs(0), 4u);
        S_ASSERT_EQ(sim.get_player_hatched(0), 1u);

        // Tick 2: 0 ms remaining -> Match is over!
        sim.tick();
        S_ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);
        S_ASSERT_TRUE(sim.is_match_over());

        // Post-gameover hatch strictly FAILS
        size_t ant_count_before = sim.get_world_state().ants.size();
        bool h2 = sim.hatch_ant(0, AntType::Worker);
        S_ASSERT_FALSE(h2);
        S_ASSERT_EQ(sim.get_player_score(0), 800);
        S_ASSERT_EQ(sim.get_player_eggs(0), 4u);
        S_ASSERT_EQ(sim.get_player_hatched(0), 1u);
        S_ASSERT_EQ(sim.get_world_state().ants.size(), ant_count_before);
    } STRESS_END();

    STRESS_TEST("3.2 Comprehensive 4-Player 6-Ant-Type Post-Match Hatch Rejection") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 3002, 50);

        for (uint8_t p = 0; p < 4; ++p) {
            sim.set_player_score(p, 5000);
            sim.set_player_eggs(p, 20);
        }

        sim.tick(); // Game Over
        S_ASSERT_TRUE(sim.is_match_over());

        AntType all_types[6] = {
            AntType::Worker, AntType::Combat, AntType::Bomber,
            AntType::Fire, AntType::Thief, AntType::Swimmer
        };

        for (uint8_t p = 0; p < 4; ++p) {
            for (AntType t : all_types) {
                bool res = sim.hatch_ant(p, t);
                S_ASSERT_FALSE(res);
                S_ASSERT_EQ(sim.get_player_score(p), 5000);
                S_ASSERT_EQ(sim.get_player_eggs(p), 20u);
                S_ASSERT_EQ(sim.get_player_hatched(p), 0u);
            }
        }
    } STRESS_END();
}

// ============================================================================
// 4. Thief Infiltration Alarm Routing Deep Stress
// ============================================================================
void test_thief_alarm_routing_deep() {
    std::cout << "\n=======================================================\n"
              << " 4. Thief Infiltration Alarm Routing Deep Stress\n"
              << "=======================================================\n";

    STRESS_TEST("4.1 Targeting Victim Player 2 from Player 0") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 4001);
        sim.set_anthill(2, {40, 40});

        uint32_t t = sim.spawn_unit(0, AntType::Thief, {40, 40});
        sim.start_thief_infiltration(t, 2);

        sim.clear_audio_events();
        sim.clear_news_events();
        sim.step_thief_animation(t, 19);

        // Targeted audio check
        S_ASSERT_TRUE(sim.has_targeted_audio_event(2, SoundID::BaseAlarmSiren));
        S_ASSERT_FALSE(sim.has_targeted_audio_event(0, SoundID::BaseAlarmSiren));
        S_ASSERT_FALSE(sim.has_targeted_audio_event(1, SoundID::BaseAlarmSiren)); // NOT player 1!
        S_ASSERT_FALSE(sim.has_targeted_audio_event(3, SoundID::BaseAlarmSiren));

        // Targeted news banner check
        S_ASSERT_TRUE(sim.has_news_event(2, StringID::ThiefAlarmWarning));
        S_ASSERT_FALSE(sim.has_news_event(0, StringID::ThiefAlarmWarning));
        S_ASSERT_FALSE(sim.has_news_event(1, StringID::ThiefAlarmWarning)); // NOT player 1!
        S_ASSERT_FALSE(sim.has_news_event(3, StringID::ThiefAlarmWarning));
    } STRESS_END();

    STRESS_TEST("4.2 Targeting Victim Player 3 from Player 0") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 4002);
        sim.set_anthill(3, {50, 50});

        uint32_t t = sim.spawn_unit(0, AntType::Thief, {50, 50});
        sim.start_thief_infiltration(t, 3);

        sim.clear_audio_events();
        sim.clear_news_events();
        sim.step_thief_animation(t, 19);

        S_ASSERT_TRUE(sim.has_targeted_audio_event(3, SoundID::BaseAlarmSiren));
        S_ASSERT_FALSE(sim.has_targeted_audio_event(1, SoundID::BaseAlarmSiren));
        S_ASSERT_TRUE(sim.has_news_event(3, StringID::ThiefAlarmWarning));
        S_ASSERT_FALSE(sim.has_news_event(1, StringID::ThiefAlarmWarning));
    } STRESS_END();

    STRESS_TEST("4.3 Loot Execution Event Routing (Sound 88 & News String 62)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 4003);
        sim.set_player_score(2, 250);

        uint32_t t = sim.spawn_unit(1, AntType::Thief, {40, 40});
        sim.clear_audio_events();
        sim.clear_news_events();

        sim.execute_thief_loot(t, 2);

        S_ASSERT_EQ(sim.get_player_score(2), 200);
        S_ASSERT_TRUE(sim.has_targeted_audio_event(2, SoundID::BaseScoreDn));
        S_ASSERT_FALSE(sim.has_targeted_audio_event(1, SoundID::BaseScoreDn));
        S_ASSERT_TRUE(sim.has_news_event(2, StringID::FoodStolenStatus));
        S_ASSERT_FALSE(sim.has_news_event(1, StringID::FoodStolenStatus));
    } STRESS_END();
}

// ============================================================================
// 5. Dynamic Alliance Dissociation & Score Isolation Deep Stress
// ============================================================================
void test_alliance_dissociation_deep() {
    std::cout << "\n=======================================================\n"
              << " 5. Dynamic Alliance Dissociation & Score Isolation Deep Stress\n"
              << "=======================================================\n";

    STRESS_TEST("5.1 Player 0 Allies with 1, then P0 Allies with 2: P1 Dissociated") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 5001);

        sim.set_player_score(0, 100);
        sim.set_player_score(1, 200);
        sim.set_player_score(2, 300);
        sim.set_player_score(3, 400);

        // P0 allies with P1
        sim.form_alliance(0, 1);
        S_ASSERT_TRUE(sim.stats_manager().are_allies(0, 1));
        S_ASSERT_TRUE(sim.stats_manager().are_allies(1, 0));
        S_ASSERT_EQ(sim.get_ally_id(0), 1u);
        S_ASSERT_EQ(sim.get_ally_id(1), 0u);
        S_ASSERT_EQ(sim.get_display_score(0), 300);
        S_ASSERT_EQ(sim.get_display_score(1), 300);

        // P0 now forms alliance with P2
        sim.form_alliance(0, 2);

        // Check new alliance
        S_ASSERT_TRUE(sim.stats_manager().are_allies(0, 2));
        S_ASSERT_TRUE(sim.stats_manager().are_allies(2, 0));
        S_ASSERT_EQ(sim.get_ally_id(0), 2u);
        S_ASSERT_EQ(sim.get_ally_id(2), 0u);

        // CRUCIAL: P1 former alliance must be clean broken
        S_ASSERT_EQ(sim.get_ally_id(1), ALLIANCE_NONE);
        S_ASSERT_FALSE(sim.stats_manager().are_allies(0, 1));
        S_ASSERT_FALSE(sim.stats_manager().are_allies(1, 0));
        S_ASSERT_FALSE(sim.stats_manager().are_allies(1, 2));

        // Display scores check (no inflation!)
        S_ASSERT_EQ(sim.get_display_score(1), 200); // Back to individual score
        S_ASSERT_EQ(sim.get_display_score(0), 400); // 100 + 300
        S_ASSERT_EQ(sim.get_display_score(2), 400); // 100 + 300
        S_ASSERT_EQ(sim.get_display_score(3), 400); // Individual score

        // Personal scores intact
        S_ASSERT_EQ(sim.get_player_score(0), 100);
        S_ASSERT_EQ(sim.get_player_score(1), 200);
        S_ASSERT_EQ(sim.get_player_score(2), 300);
        S_ASSERT_EQ(sim.get_player_score(3), 400);
    } STRESS_END();

    STRESS_TEST("5.2 Chained Alliance Shifts: P3 Steals P2 from P0") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 5002);

        sim.set_player_score(0, 100);
        sim.set_player_score(1, 200);
        sim.set_player_score(2, 300);
        sim.set_player_score(3, 400);

        sim.form_alliance(0, 2);
        S_ASSERT_TRUE(sim.stats_manager().are_allies(0, 2));

        // P3 allies with P2
        sim.form_alliance(3, 2);

        S_ASSERT_TRUE(sim.stats_manager().are_allies(3, 2));
        S_ASSERT_TRUE(sim.stats_manager().are_allies(2, 3));
        S_ASSERT_EQ(sim.get_ally_id(2), 3u);
        S_ASSERT_EQ(sim.get_ally_id(3), 2u);

        // P0 former alliance with P2 must be broken
        S_ASSERT_EQ(sim.get_ally_id(0), ALLIANCE_NONE);
        S_ASSERT_FALSE(sim.stats_manager().are_allies(0, 2));
        S_ASSERT_EQ(sim.get_display_score(0), 100);
        S_ASSERT_EQ(sim.get_display_score(2), 700); // 300 + 400
        S_ASSERT_EQ(sim.get_display_score(3), 700); // 300 + 400
    } STRESS_END();
}

int main() {
    std::cout << "\n=======================================================\n"
              << " DEEP EMPIRICAL STRESS SUITE (CHALLENGER 2)\n"
              << "=======================================================\n";

    test_concentric_chebyshev_deep();
    test_frame8_heal_sound_deep();
    test_post_match_freeze_deep();
    test_thief_alarm_routing_deep();
    test_alliance_dissociation_deep();

    std::cout << "\n=======================================================\n"
              << " DEEP STRESS SUITE RESULTS\n"
              << "=======================================================\n"
              << " Tests Run:        " << g_tests_run << "\n"
              << " Tests Passed:     " << g_tests_passed << "\n"
              << " Tests Failed:     " << g_tests_failed << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << "=======================================================\n";

    if (g_tests_failed > 0) {
        std::cout << " >>> VERDICT: FAILURES DETECTED! <<<\n";
        return 1;
    } else {
        std::cout << " >>> VERDICT: ALL DEEP STRESS TESTS PASSED! <<<\n";
        return 0;
    }
}
