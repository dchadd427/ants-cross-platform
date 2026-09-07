#include "e2e_framework.hpp"
#include "e2e_model.hpp"

using namespace e2e;

// =========================================================================
// TIER 3: CROSS-FEATURE PAIRWISE COMBINATIONS
// =========================================================================

// Scenario 1: Combat Heavy Punch knocking target into Firewall with ricochet and damage accumulation
E2E_TIER3_TEST(Tier3_Pairwise, combat_knockback_into_firewall_ricochet) {
    SimulationModel sim(60, 60);
    uint32_t fire_ant = sim.spawn_ant(0, AntType::Fire, {10, 14});
    sim.place_firewall(fire_ant, {10, 15});
    ASSERT_TRUE(sim.has_fire({10, 15}));

    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {10, 13});
    uint32_t enemy_w = sim.spawn_ant(1, AntType::Worker, {10, 14});

    sim.clear_audio_events();
    ASSERT_TRUE(sim.execute_melee_attack(combat, enemy_w));

    auto* victim = sim.find_unit(enemy_w);
    // 2 (combat punch) + 1 (fire contact) = 3 total damage -> HP = 7
    ASSERT_EQ(victim->hp, 7);
    ASSERT_NE(victim->pos.y, 15);
    ASSERT_TRUE(sim.has_fire({10, 15}));

    bool found_78 = false, found_64 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 78) found_78 = true;
        if (ev.sound_id == 64) found_64 = true;
    }
    ASSERT_TRUE(found_78);
    ASSERT_TRUE(found_64);
}

// Scenario 2: Bridge universal traversal and 180s collapse drowning non-swimmers while swimmer survives
E2E_TIER3_TEST(Tier3_Pairwise, bridge_universal_traversal_and_collapse_drowning) {
    SimulationModel sim(60, 60);
    sim.set_terrain(25, 25, 2); // Deep water

    uint32_t swimmer = sim.spawn_ant(0, AntType::Swimmer, {25, 24});
    ASSERT_TRUE(sim.build_bridge(swimmer, {25, 25}));
    ASSERT_TRUE(sim.has_bridge({25, 25}));

    uint32_t worker = sim.spawn_ant(0, AntType::Worker, {25, 24});
    uint32_t enemy_worker = sim.spawn_ant(1, AntType::Worker, {25, 26});
    sim.find_unit(worker)->pos = {25, 25};
    sim.find_unit(enemy_worker)->pos = {25, 25};
    sim.find_unit(swimmer)->pos = {25, 25};

    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(worker)->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(enemy_worker)->state), static_cast<uint8_t>(AntState::Dead));

    sim.clear_audio_events();
    for (int i = 0; i < 3600; ++i) sim.tick();

    ASSERT_FALSE(sim.has_bridge({25, 25}));
    ASSERT_EQ(static_cast<uint8_t>(sim.find_unit(worker)->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_EQ(static_cast<uint8_t>(sim.find_unit(enemy_worker)->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(swimmer)->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_EQ(sim.find_unit(swimmer)->hp, 10);

    bool found_71 = false, found_72 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 71) found_71 = true;
        if (ev.sound_id == 72) found_72 = true;
    }
    ASSERT_TRUE(found_71);
    ASSERT_TRUE(found_72);

    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 1u);
    ASSERT_EQ(sim.get_player_stats(1).friendly_lost, 1u);
}

// Scenario 3: Thief infiltration dive -> Sound 58 alarm + News flash -> 50 pt theft -> death lunchbox drop -> universal pickup
E2E_TIER3_TEST(Tier3_Pairwise, thief_infiltration_alarm_steal_death_drop_and_pickup) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {10, 10});
    sim.set_anthill(1, {40, 40});

    sim.get_player_stats(1).score = 150;
    sim.get_player_stats(0).score = 0;

    uint32_t thief = sim.spawn_ant(0, AntType::Thief, {40, 39});
    sim.clear_audio_events();
    sim.clear_news_events();
    ASSERT_TRUE(sim.thief_infiltrate(thief, 1));

    bool found_58 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 58 && ev.target_player == 1) found_58 = true;
    }
    ASSERT_TRUE(found_58);
    ASSERT_GT(sim.news_events().size(), 0u);
    ASSERT_EQ(sim.news_events()[0].target_player, 1u);

    ASSERT_EQ(sim.get_player_stats(1).score, 100);
    ASSERT_EQ(sim.find_unit(thief)->carried_points, 50u);
    ASSERT_TRUE(sim.find_unit(thief)->holding_lunchbox);

    sim.find_unit(thief)->pos = {30, 30};
    uint32_t interceptor = sim.spawn_ant(1, AntType::Worker, {30, 31});
    sim.find_unit(thief)->hp = 1;
    ASSERT_TRUE(sim.execute_melee_attack(interceptor, thief));
    ASSERT_EQ(static_cast<uint8_t>(sim.find_unit(thief)->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_TRUE(sim.has_lunchbox({30, 30}));

    uint32_t retriever = sim.spawn_ant(1, AntType::Worker, {30, 30});
    sim.find_unit(retriever)->holding_lunchbox = true;
    sim.find_unit(retriever)->carried_points = 50;

    sim.find_unit(retriever)->pos = {40, 40};
    sim.clear_audio_events();
    ASSERT_TRUE(sim.deposit_food(retriever));

    ASSERT_EQ(sim.get_player_stats(1).score, 150);
    bool found_87 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 87) found_87 = true;
    }
    ASSERT_TRUE(found_87);
}

// Scenario 4: Alliance lifecycle with shared scoreboard and discrete memory isolation
E2E_TIER3_TEST(Tier3_Pairwise, alliance_shared_scoreboard_and_discrete_memory) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 300;
    sim.get_player_stats(1).score = 200;

    sim.clear_audio_events();
    ASSERT_TRUE(sim.propose_alliance(0, 1));
    bool found_51 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 51 && ev.target_player == 1) found_51 = true;
    }
    ASSERT_TRUE(found_51);

    sim.clear_audio_events();
    ASSERT_TRUE(sim.accept_alliance(0, 1));
    bool f53 = false, f50 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 53) f53 = true;
        if (ev.sound_id == 50) f50 = true;
    }
    ASSERT_TRUE(f53);
    ASSERT_TRUE(f50);

    ASSERT_EQ(sim.get_team_score(0), 500);
    ASSERT_EQ(sim.get_team_score(1), 500);
    ASSERT_EQ(sim.get_player_stats(0).score, 300);
    ASSERT_EQ(sim.get_player_stats(1).score, 200);

    uint32_t a0 = sim.spawn_ant(0, AntType::Combat, {10, 10});
    uint32_t a1 = sim.spawn_ant(1, AntType::Worker, {10, 11});
    ASSERT_FALSE(sim.execute_melee_attack(a0, a1));
    ASSERT_EQ(sim.find_unit(a1)->hp, 10);

    sim.clear_audio_events();
    ASSERT_TRUE(sim.break_alliance(0, 1));
    bool found_49 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 49) found_49 = true;
    }
    ASSERT_TRUE(found_49);

    ASSERT_EQ(sim.get_team_score(0), 300);
    ASSERT_EQ(sim.get_team_score(1), 200);
}

// Scenario 5: Fire Ant extinguishing firewall under enemy pressure
E2E_TIER3_TEST(Tier3_Pairwise, fire_ant_extinguish_under_enemy_pressure) {
    SimulationModel sim(60, 60);
    uint32_t enemy_fire = sim.spawn_ant(1, AntType::Fire, {10, 10});
    sim.place_firewall(enemy_fire, {10, 9});
    ASSERT_TRUE(sim.has_fire({10, 9}));

    uint32_t friendly_fire = sim.spawn_ant(0, AntType::Fire, {10, 8});
    sim.clear_audio_events();
    ASSERT_TRUE(sim.extinguish_fire(friendly_fire, {10, 9}));
    ASSERT_FALSE(sim.has_fire({10, 9}));

    bool found_69 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 69) found_69 = true;
    }
    ASSERT_TRUE(found_69);
}

// Scenario 6: Bomber squash defusal adjacent to idle Combat Ant
E2E_TIER3_TEST(Tier3_Pairwise, bomber_squash_defusal_near_combat_guard) {
    SimulationModel sim(60, 60);
    uint32_t enemy_bomber = sim.spawn_ant(1, AntType::Bomber, {15, 15});
    sim.plant_bomb(enemy_bomber, {15, 16});
    ASSERT_TRUE(sim.has_bomb({15, 16}));

    uint32_t friendly_bomber = sim.spawn_ant(0, AntType::Bomber, {15, 17});
    sim.clear_audio_events();
    ASSERT_TRUE(sim.defuse_bomb(friendly_bomber, {15, 16}));
    ASSERT_FALSE(sim.has_bomb({15, 16}));
    ASSERT_EQ(sim.find_unit(friendly_bomber)->hp, 10);
}

// Scenario 7: Combat punch launching victim directly into deep water with drowning
E2E_TIER3_TEST(Tier3_Pairwise, combat_punch_into_deep_water_drowning) {
    SimulationModel sim(60, 60);
    // Water at (10, 14), (10, 15), (10, 16)
    sim.set_terrain(10, 14, 2); // Deep water
    sim.set_terrain(10, 15, 2); // Deep water
    sim.set_terrain(10, 16, 2); // Deep water

    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {10, 10});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 11});

    sim.clear_audio_events();
    sim.execute_melee_attack(combat, target);

    // Victim knocked 4-5 tiles south into deep water and drowned!
    ASSERT_EQ(static_cast<uint8_t>(sim.find_unit(target)->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_EQ(sim.get_player_stats(1).friendly_lost, 1u);

    bool found_71 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 71) found_71 = true;
    }
    ASSERT_TRUE(found_71);
}

// Scenario 8: Simultaneous cross-thefts by opposing thieves
E2E_TIER3_TEST(Tier3_Pairwise, simultaneous_opposing_thieves_cross_raid) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {10, 10});
    sim.set_anthill(1, {50, 50});
    sim.get_player_stats(0).score = 200;
    sim.get_player_stats(1).score = 300;

    uint32_t t0 = sim.spawn_ant(0, AntType::Thief, {50, 49});
    uint32_t t1 = sim.spawn_ant(1, AntType::Thief, {10, 9});

    sim.clear_audio_events();
    ASSERT_TRUE(sim.thief_infiltrate(t0, 1));
    ASSERT_TRUE(sim.thief_infiltrate(t1, 0));

    // Both scores reduced by 50
    ASSERT_EQ(sim.get_player_stats(0).score, 150);
    ASSERT_EQ(sim.get_player_stats(1).score, 250);

    // Both carried points = 50
    ASSERT_EQ(sim.find_unit(t0)->carried_points, 50u);
    ASSERT_EQ(sim.find_unit(t1)->carried_points, 50u);
}

// Scenario 9: Alliance proposal during combat halts friendly fire immediately
E2E_TIER3_TEST(Tier3_Pairwise, alliance_proposal_halts_combat) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {20, 20});
    uint32_t w = sim.spawn_ant(1, AntType::Worker, {20, 21});

    // Before alliance, combat is valid
    ASSERT_TRUE(sim.execute_melee_attack(c, w));
    ASSERT_EQ(sim.find_unit(w)->hp, 8);

    // Alliance established
    ASSERT_TRUE(sim.accept_alliance(0, 1));

    // Attacks now blocked
    sim.find_unit(w)->pos = {20, 21};
    ASSERT_FALSE(sim.execute_melee_attack(c, w));
    ASSERT_EQ(sim.find_unit(w)->hp, 8);
}

// Scenario 10: Heavy pinball ricochet between two firewalls
E2E_TIER3_TEST(Tier3_Pairwise, pinball_ricochet_between_two_firewalls) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    sim.place_firewall(f, {10, 11});

    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 10});
    sim.apply_knockback(*sim.find_unit(target), 0, -1, 3);

    // Target sustained damage from fire contact
    ASSERT_LT(sim.find_unit(target)->hp, 10);
    // Both firewalls remain intact
    ASSERT_TRUE(sim.has_fire({10, 9}));
    ASSERT_TRUE(sim.has_fire({10, 11}));
}
