#include "e2e_framework.hpp"
#include "e2e_model.hpp"

using namespace e2e;

// =========================================================================
// TIER 4: REAL-WORLD WORKLOAD SCENARIOS
// =========================================================================

// Scenario 1: Complete 12-Minute Match Flow with Simulation Freeze, Winner/Loser Audio, and Scorecard
E2E_TIER4_TEST(Tier4_Scenarios, complete_12_minute_match_lifecycle) {
    SimulationModel sim(60, 60, 1995);
    sim.set_anthill(0, {10, 10});
    sim.set_anthill(1, {50, 50});
    sim.set_anthill(2, {10, 50});
    sim.set_anthill(3, {50, 10});

    ASSERT_EQ(sim.match_time_ms(), 720000u); // 12:00
    ASSERT_FALSE(sim.is_frozen());

    for (uint8_t p = 0; p < 4; ++p) {
        Vec2i base = sim.get_anthill(p);
        sim.spawn_ant(p, AntType::Worker, base);
    }

    sim.get_player_stats(0).score += 250;
    ASSERT_TRUE(sim.hatch_ant(0, AntType::Combat));
    ASSERT_EQ(sim.get_player_stats(0).score, 50);
    ASSERT_EQ(sim.get_player_stats(0).new_hatched, 1u);

    uint32_t c0 = sim.spawn_ant(0, AntType::Combat, {30, 30});
    uint32_t w1 = sim.spawn_ant(1, AntType::Worker, {30, 31});
    sim.find_unit(w1)->hp = 2;
    ASSERT_TRUE(sim.execute_melee_attack(c0, w1));
    ASSERT_EQ(static_cast<uint8_t>(sim.find_unit(w1)->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_EQ(sim.get_player_stats(0).enemy_killed, 1u);
    ASSERT_EQ(sim.get_player_stats(1).friendly_lost, 1u);

    sim.get_player_stats(0).score += 300;
    sim.get_player_stats(1).score = 150;
    sim.get_player_stats(2).score = 100;
    sim.get_player_stats(3).score = 50;

    sim.clear_audio_events();
    sim.set_match_time_ms(50);
    sim.tick();

    ASSERT_TRUE(sim.is_frozen());
    ASSERT_EQ(sim.match_time_ms(), 0u);

    bool p0_got_winner_sound = false;
    bool p1_got_loser_sound = false;
    bool p2_got_loser_sound = false;
    bool p3_got_loser_sound = false;

    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 56 && ev.target_player == 0) p0_got_winner_sound = true;
        if (ev.sound_id == 41 && ev.target_player == 1) p1_got_loser_sound = true;
        if (ev.sound_id == 41 && ev.target_player == 2) p2_got_loser_sound = true;
        if (ev.sound_id == 41 && ev.target_player == 3) p3_got_loser_sound = true;
    }

    ASSERT_TRUE(p0_got_winner_sound);
    ASSERT_TRUE(p1_got_loser_sound);
    ASSERT_TRUE(p2_got_loser_sound);
    ASSERT_TRUE(p3_got_loser_sound);

    auto& s0 = sim.get_player_stats(0);
    ASSERT_EQ(s0.score, 350);
    ASSERT_EQ(s0.friendly_lost, 0u);
    ASSERT_EQ(s0.enemy_killed, 1u);
    ASSERT_EQ(s0.new_hatched, 1u);

    auto& s1 = sim.get_player_stats(1);
    ASSERT_EQ(s1.score, 150);
    ASSERT_EQ(s1.friendly_lost, 1u);
    ASSERT_EQ(s1.enemy_killed, 0u);
    ASSERT_EQ(s1.new_hatched, 0u);
}

// Scenario 2: 4-Player FFA with Dynamic Alliances and Shifting Frontlines
E2E_TIER4_TEST(Tier4_Scenarios, four_player_ffa_diplomacy_and_warfare) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 200;
    sim.get_player_stats(1).score = 180;
    sim.get_player_stats(2).score = 220;
    sim.get_player_stats(3).score = 150;

    ASSERT_TRUE(sim.propose_alliance(0, 1));
    ASSERT_TRUE(sim.accept_alliance(0, 1));

    ASSERT_EQ(sim.get_team_score(0), 380);
    ASSERT_EQ(sim.get_team_score(1), 380);
    ASSERT_GT(sim.get_team_score(0), sim.get_team_score(2));

    uint32_t t2_c = sim.spawn_ant(2, AntType::Combat, {20, 20});
    uint32_t t0_w = sim.spawn_ant(0, AntType::Worker, {20, 21});
    sim.find_unit(t0_w)->hp = 2;
    sim.execute_melee_attack(t2_c, t0_w);
    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 1u);
    ASSERT_EQ(sim.get_player_stats(2).enemy_killed, 1u);

    uint32_t t1_c = sim.spawn_ant(1, AntType::Combat, {20, 19});
    sim.find_unit(t2_c)->hp = 2;
    sim.execute_melee_attack(t1_c, t2_c);
    ASSERT_EQ(sim.get_player_stats(2).friendly_lost, 1u);
    ASSERT_EQ(sim.get_player_stats(1).enemy_killed, 1u);
}

// Scenario 3: Coordinated Multi-Base Thief Infiltration Heist
E2E_TIER4_TEST(Tier4_Scenarios, coordinated_multi_base_thief_heist) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {10, 10});
    sim.set_anthill(1, {50, 50});
    sim.get_player_stats(0).score = 200;
    sim.get_player_stats(1).score = 200;

    uint32_t t0 = sim.spawn_ant(0, AntType::Thief, {50, 49});
    uint32_t t1 = sim.spawn_ant(1, AntType::Thief, {10, 9});

    ASSERT_TRUE(sim.thief_infiltrate(t0, 1));
    ASSERT_TRUE(sim.thief_infiltrate(t1, 0));

    ASSERT_EQ(sim.get_player_stats(0).score, 150);
    ASSERT_EQ(sim.get_player_stats(1).score, 150);
    ASSERT_EQ(sim.find_unit(t0)->carried_points, 50u);
    ASSERT_EQ(sim.find_unit(t1)->carried_points, 50u);
}

// Scenario 4: Deep Water Island Bridge Assault and Retreat
E2E_TIER4_TEST(Tier4_Scenarios, deep_water_island_bridge_assault_and_retreat) {
    SimulationModel sim(60, 60);
    sim.set_terrain(20, 20, 2); // Deep water chokepoint

    uint32_t swimmer = sim.spawn_ant(0, AntType::Swimmer, {20, 19});
    ASSERT_TRUE(sim.build_bridge(swimmer, {20, 20}));
    ASSERT_TRUE(sim.has_bridge({20, 20}));

    uint32_t worker = sim.spawn_ant(0, AntType::Worker, {20, 20});
    // Worker retreats before collapse
    sim.find_unit(worker)->pos = {20, 19};

    // 180s elapses
    for (int i = 0; i < 3600; ++i) sim.tick();

    ASSERT_FALSE(sim.has_bridge({20, 20}));
    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(worker)->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 0u);
}

// Scenario 5: Full Game Determinism Under Identical Random Seeds
E2E_TIER4_TEST(Tier4_Scenarios, full_game_determinism_under_identical_seeds) {
    SimulationModel sim1(60, 60, 424242);
    SimulationModel sim2(60, 60, 424242);

    for (int i = 0; i < 500; ++i) {
        sim1.tick();
        sim2.tick();
        ASSERT_EQ(sim1.prng().raw_state(), sim2.prng().raw_state());
    }
}

// Scenario 6: Tactical Minefield Defense Operation
E2E_TIER4_TEST(Tier4_Scenarios, tactical_minefield_defense_operation) {
    SimulationModel sim(60, 60);
    uint32_t b = sim.spawn_ant(0, AntType::Bomber, {15, 15});
    sim.plant_bomb(b, {15, 16});
    ASSERT_TRUE(sim.has_bomb({15, 16}));

    // Enemy Bomber defuses mine safely
    uint32_t enemy_b = sim.spawn_ant(1, AntType::Bomber, {15, 17});
    ASSERT_TRUE(sim.defuse_bomb(enemy_b, {15, 16}));
    ASSERT_FALSE(sim.has_bomb({15, 16}));
    ASSERT_EQ(sim.find_unit(enemy_b)->hp, 10);
}
