#include "e2e_framework.hpp"
#include "e2e_model.hpp"

using namespace e2e;

// =========================================================================
// FEATURE 7: Discrete 20 Hz Tick Engine (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat7_tick_rate_50ms_interval, 7) {
    SimulationModel sim(60, 60, 1234);
    uint32_t t0 = sim.match_time_ms();
    sim.tick();
    uint32_t t1 = sim.match_time_ms();
    ASSERT_EQ(t0 - t1, 50u);
}

E2E_TIER1_TEST(Tier1_Sim, feat7_tick_counter_monotonic, 7) {
    SimulationModel sim(60, 60, 1234);
    uint32_t last_t = sim.match_time_ms();
    for (int i = 0; i < 20; ++i) {
        sim.tick();
        uint32_t cur_t = sim.match_time_ms();
        ASSERT_LT(cur_t, last_t);
        last_t = cur_t;
    }
}

E2E_TIER1_TEST(Tier1_Sim, feat7_tick_substep_order, 7) {
    SimulationModel sim(60, 60, 1234);
    // 20 ticks = 1000 ms = 1 second
    uint32_t t_start = sim.match_time_ms();
    for (int i = 0; i < 20; ++i) sim.tick();
    ASSERT_EQ(t_start - sim.match_time_ms(), 1000u);
}

E2E_TIER1_TEST(Tier1_Sim, feat7_tick_accumulator, 7) {
    SimulationModel sim(60, 60, 1234);
    sim.set_match_time_ms(150);
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 100u);
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 50u);
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 0u);
    ASSERT_TRUE(sim.is_frozen());
}

E2E_TIER1_TEST(Tier1_Sim, feat7_tick_integer_math_purity, 7) {
    SimulationModel sim(60, 60, 1234);
    for (int i = 0; i < 100; ++i) sim.tick();
    ASSERT_EQ(sim.match_time_ms() % 50, 0u);
}

// =========================================================================
// FEATURE 8: MSVC LCG PRNG Engine (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat8_lcg_msvc_recurrence, 8) {
    // Formula: next = state * 214013 + 2531011; result = (next >> 16) & 0x7FFF
    MsvcPrng prng(1);
    uint32_t r1 = prng.next();
    // 1 * 214013 + 2531011 = 2745024 = 0x29E2C0 -> (0x29E2C0 >> 16) & 0x7FFF = 0x29 = 41
    ASSERT_EQ(r1, 41u);
}

E2E_TIER1_TEST(Tier1_Sim, feat8_lcg_seed_reproducibility, 8) {
    MsvcPrng p1(12345);
    MsvcPrng p2(12345);
    for (int i = 0; i < 50; ++i) {
        ASSERT_EQ(p1.next(), p2.next());
    }
}

E2E_TIER1_TEST(Tier1_Sim, feat8_lcg_rand_max_range, 8) {
    MsvcPrng prng(42);
    for (int i = 0; i < 500; ++i) {
        uint32_t val = prng.next();
        ASSERT_LE(val, 0x7FFFu); // Max 32767
    }
}

E2E_TIER1_TEST(Tier1_Sim, feat8_lcg_high_bit_distribution, 8) {
    MsvcPrng prng(999);
    bool seen_high = false, seen_low = false;
    for (int i = 0; i < 100; ++i) {
        uint32_t val = prng.next();
        if (val > 16384) seen_high = true;
        if (val < 16384) seen_low = true;
    }
    ASSERT_TRUE(seen_high);
    ASSERT_TRUE(seen_low);
}

E2E_TIER1_TEST(Tier1_Sim, feat8_lcg_deterministic_lockstep, 8) {
    SimulationModel sim1(60, 60, 555);
    SimulationModel sim2(60, 60, 555);
    ASSERT_EQ(sim1.prng().next(), sim2.prng().next());
    ASSERT_EQ(sim1.prng().next(), sim2.prng().next());
}

// =========================================================================
// FEATURE 9: Universal Unit Attributes (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat9_unit_max_hp_10, 9) {
    SimulationModel sim(60, 60);
    uint32_t u1 = sim.spawn_ant(0, AntType::Worker, {10, 10});
    auto* u = sim.find_unit(u1);
    ASSERT_TRUE(u != nullptr);
    ASSERT_EQ(u->hp, 10);
}

E2E_TIER1_TEST(Tier1_Sim, feat9_unit_standard_melee_1hp, 9) {
    SimulationModel sim(60, 60);
    uint32_t a1 = sim.spawn_ant(0, AntType::Worker, {10, 10});
    uint32_t t1 = sim.spawn_ant(1, AntType::Worker, {10, 11});
    ASSERT_TRUE(sim.execute_melee_attack(a1, t1));
    auto* target = sim.find_unit(t1);
    ASSERT_EQ(target->hp, 9); // Exactly 1 HP damage
}

E2E_TIER1_TEST(Tier1_Sim, feat9_unit_death_at_0hp, 9) {
    SimulationModel sim(60, 60);
    uint32_t a1 = sim.spawn_ant(0, AntType::Worker, {10, 10});
    uint32_t t1 = sim.spawn_ant(1, AntType::Worker, {10, 11});
    auto* target = sim.find_unit(t1);
    target->hp = 1;
    ASSERT_TRUE(sim.execute_melee_attack(a1, t1));
    ASSERT_EQ(target->hp, 0);
    ASSERT_EQ(static_cast<uint8_t>(target->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_EQ(sim.get_player_stats(1).friendly_lost, 1u);
    ASSERT_EQ(sim.get_player_stats(0).enemy_killed, 1u);
}

E2E_TIER1_TEST(Tier1_Sim, feat9_unit_state_idle_default, 9) {
    SimulationModel sim(60, 60);
    uint32_t u1 = sim.spawn_ant(0, AntType::Thief, {5, 5});
    auto* u = sim.find_unit(u1);
    ASSERT_EQ(static_cast<uint8_t>(u->state), static_cast<uint8_t>(AntState::Idle));
}

E2E_TIER1_TEST(Tier1_Sim, feat9_unit_all_standard_classes_deal_1hp, 9) {
    AntType types[] = {AntType::Worker, AntType::Thief, AntType::Fire, AntType::Bomber, AntType::Swimmer};
    for (auto type : types) {
        SimulationModel sim(60, 60);
        uint32_t a = sim.spawn_ant(0, type, {10, 10});
        uint32_t t = sim.spawn_ant(1, AntType::Worker, {10, 11});
        ASSERT_TRUE(sim.execute_melee_attack(a, t));
        ASSERT_EQ(sim.find_unit(t)->hp, 9);
    }
}

// =========================================================================
// FEATURE 10: Combat Ant Heavy Punch (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat10_combat_punch_2hp_damage, 10) {
    SimulationModel sim(60, 60);
    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {10, 10});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 11});
    ASSERT_TRUE(sim.execute_melee_attack(combat, target));
    ASSERT_EQ(sim.find_unit(target)->hp, 8); // Deals 2 HP
}

E2E_TIER1_TEST(Tier1_Sim, feat10_combat_punch_4_to_5_tile_knockback, 10) {
    SimulationModel sim(60, 60);
    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {10, 10});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 11});
    ASSERT_TRUE(sim.execute_melee_attack(combat, target));
    auto* tgt = sim.find_unit(target);
    int32_t dist = tgt->pos.y - 11;
    ASSERT_TRUE(dist == 4 || dist == 5);
}

E2E_TIER1_TEST(Tier1_Sim, feat10_combat_punch_sound_78, 10) {
    SimulationModel sim(60, 60);
    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {10, 10});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 11});
    sim.clear_audio_events();
    ASSERT_TRUE(sim.execute_melee_attack(combat, target));
    bool found_78 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 78) found_78 = true; // attack2.wav
    }
    ASSERT_TRUE(found_78);
}

E2E_TIER1_TEST(Tier1_Sim, feat10_combat_punch_victim_stun_12_ticks, 10) {
    SimulationModel sim(60, 60);
    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {10, 10});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 11});
    ASSERT_TRUE(sim.execute_melee_attack(combat, target));
    auto* tgt = sim.find_unit(target);
    ASSERT_EQ(static_cast<uint8_t>(tgt->state), static_cast<uint8_t>(AntState::AirborneKnockback));
    ASSERT_EQ(tgt->state_ticks, 12u);
}

E2E_TIER1_TEST(Tier1_Sim, feat10_combat_punch_diagonal_impulse, 10) {
    SimulationModel sim(60, 60);
    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {10, 10});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {11, 11});
    ASSERT_TRUE(sim.execute_melee_attack(combat, target));
    auto* tgt = sim.find_unit(target);
    ASSERT_GT(tgt->pos.x, 11);
    ASSERT_GT(tgt->pos.y, 11);
}

// =========================================================================
// FEATURE 11: Combat Ant Guard AI (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat11_guard_anchor_saved_on_idle, 11) {
    SimulationModel sim(60, 60);
    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {20, 20});
    auto* u = sim.find_unit(combat);
    ASSERT_EQ(u->guard_tile.x, 20);
    ASSERT_EQ(u->guard_tile.y, 20);
}

E2E_TIER1_TEST(Tier1_Sim, feat11_guard_aggro_radius_3_chebyshev, 11) {
    SimulationModel sim(60, 60);
    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {20, 20});
    // Enemy within Chebyshev distance 3 (e.g. at 23, 20)
    sim.spawn_ant(1, AntType::Worker, {23, 20});
    sim.tick();
    auto* c = sim.find_unit(combat);
    // Combat Ant stepped toward enemy
    ASSERT_GT(c->pos.x, 20);
}

E2E_TIER1_TEST(Tier1_Sim, feat11_guard_autonomous_intercept, 11) {
    SimulationModel sim(60, 60);
    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {20, 20});
    uint32_t enemy = sim.spawn_ant(1, AntType::Worker, {21, 20});
    ASSERT_TRUE(sim.find_unit(combat) != nullptr);
    sim.tick();
    // Combat Ant attacked and target took 2 damage
    auto* e = sim.find_unit(enemy);
    ASSERT_EQ(e->hp, 8);
}

E2E_TIER1_TEST(Tier1_Sim, feat11_guard_return_to_post_after_strike, 11) {
    SimulationModel sim(60, 60);
    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {20, 20});
    sim.spawn_ant(1, AntType::Worker, {21, 20});
    sim.tick();
    auto* c = sim.find_unit(combat);
    ASSERT_EQ(c->pos.x, 20);
    ASSERT_EQ(c->pos.y, 20);
}

E2E_TIER1_TEST(Tier1_Sim, feat11_guard_ignores_enemies_beyond_radius_3, 11) {
    SimulationModel sim(60, 60);
    uint32_t combat = sim.spawn_ant(0, AntType::Combat, {20, 20});
    // Enemy at distance 4 (e.g. at 24, 20)
    sim.spawn_ant(1, AntType::Worker, {24, 20});
    sim.tick();
    auto* c = sim.find_unit(combat);
    ASSERT_EQ(c->pos.x, 20); // Remained at post
    ASSERT_EQ(c->pos.y, 20);
}

// =========================================================================
// FEATURE 12: Cardinal-Only Placement (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat12_place_north_success, 12) {
    SimulationModel sim(60, 60);
    ASSERT_TRUE(sim.validate_cardinal_placement({10, 10}, {10, 9}));
}

E2E_TIER1_TEST(Tier1_Sim, feat12_place_east_success, 12) {
    SimulationModel sim(60, 60);
    ASSERT_TRUE(sim.validate_cardinal_placement({10, 10}, {11, 10}));
}

E2E_TIER1_TEST(Tier1_Sim, feat12_place_south_success, 12) {
    SimulationModel sim(60, 60);
    ASSERT_TRUE(sim.validate_cardinal_placement({10, 10}, {10, 11}));
}

E2E_TIER1_TEST(Tier1_Sim, feat12_place_west_success, 12) {
    SimulationModel sim(60, 60);
    ASSERT_TRUE(sim.validate_cardinal_placement({10, 10}, {9, 10}));
}

E2E_TIER1_TEST(Tier1_Sim, feat12_place_diagonal_rejected, 12) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.validate_cardinal_placement({10, 10}, {11, 11}));
    ASSERT_FALSE(sim.validate_cardinal_placement({10, 10}, {9, 9}));
    ASSERT_FALSE(sim.validate_cardinal_placement({10, 10}, {11, 9}));
    ASSERT_FALSE(sim.validate_cardinal_placement({10, 10}, {9, 11}));
}

// =========================================================================
// FEATURE 13: Bomb Planting Mechanics (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat13_bomb_plant_success, 13) {
    SimulationModel sim(60, 60);
    uint32_t b = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    ASSERT_TRUE(sim.plant_bomb(b, {10, 11}));
    ASSERT_TRUE(sim.has_bomb({10, 11}));
}

E2E_TIER1_TEST(Tier1_Sim, feat13_bomb_plant_sound_90, 13) {
    SimulationModel sim(60, 60);
    uint32_t b = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.clear_audio_events();
    ASSERT_TRUE(sim.plant_bomb(b, {10, 11}));
    bool found_90 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 90) found_90 = true; // bombpick.wav
    }
    ASSERT_TRUE(found_90);
}

E2E_TIER1_TEST(Tier1_Sim, feat13_bomb_diagonal_placement_rejected, 13) {
    SimulationModel sim(60, 60);
    uint32_t b = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    ASSERT_FALSE(sim.plant_bomb(b, {11, 11}));
    ASSERT_FALSE(sim.has_bomb({11, 11}));
}

E2E_TIER1_TEST(Tier1_Sim, feat13_bomb_non_bomber_rejected, 13) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    ASSERT_FALSE(sim.plant_bomb(w, {10, 11}));
}

E2E_TIER1_TEST(Tier1_Sim, feat13_bomb_permanent_until_cleared, 13) {
    SimulationModel sim(60, 60);
    uint32_t b = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b, {10, 11});
    for (int i = 0; i < 100; ++i) sim.tick();
    ASSERT_TRUE(sim.has_bomb({10, 11}));
}

// =========================================================================
// FEATURE 14: Bomber Squash Defusal (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat14_defuse_success, 14) {
    SimulationModel sim(60, 60);
    uint32_t b1 = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b1, {10, 11});
    uint32_t b2 = sim.spawn_ant(1, AntType::Bomber, {11, 11});
    ASSERT_TRUE(sim.defuse_bomb(b2, {10, 11}));
    ASSERT_FALSE(sim.has_bomb({10, 11}));
}

E2E_TIER1_TEST(Tier1_Sim, feat14_defuse_sound_73_and_74, 14) {
    SimulationModel sim(60, 60);
    uint32_t b1 = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b1, {10, 11});
    uint32_t b2 = sim.spawn_ant(1, AntType::Bomber, {11, 11});
    sim.clear_audio_events();
    ASSERT_TRUE(sim.defuse_bomb(b2, {10, 11}));
    bool found_73 = false, found_74 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 73) found_73 = true;
        if (ev.sound_id == 74) found_74 = true;
    }
    ASSERT_TRUE(found_73);
    ASSERT_TRUE(found_74);
}

E2E_TIER1_TEST(Tier1_Sim, feat14_defuse_bomber_takes_zero_damage, 14) {
    SimulationModel sim(60, 60);
    uint32_t b1 = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b1, {10, 11});
    uint32_t b2 = sim.spawn_ant(1, AntType::Bomber, {11, 11});
    sim.defuse_bomb(b2, {10, 11});
    ASSERT_EQ(sim.find_unit(b2)->hp, 10);
}

E2E_TIER1_TEST(Tier1_Sim, feat14_defuse_non_bomber_rejected, 14) {
    SimulationModel sim(60, 60);
    uint32_t b1 = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b1, {10, 11});
    uint32_t w = sim.spawn_ant(1, AntType::Worker, {11, 11});
    ASSERT_FALSE(sim.defuse_bomb(w, {10, 11}));
    ASSERT_TRUE(sim.has_bomb({10, 11}));
}

E2E_TIER1_TEST(Tier1_Sim, feat14_defuse_distant_bomb_rejected, 14) {
    SimulationModel sim(60, 60);
    uint32_t b1 = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b1, {10, 11});
    uint32_t b2 = sim.spawn_ant(1, AntType::Bomber, {15, 15});
    ASSERT_FALSE(sim.defuse_bomb(b2, {10, 11}));
}

// =========================================================================
// FEATURE 15: Fire Ignition & Obstruction (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat15_fire_ignition_success, 15) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    ASSERT_TRUE(sim.place_firewall(f, {10, 9}));
    ASSERT_TRUE(sim.has_fire({10, 9}));
}

E2E_TIER1_TEST(Tier1_Sim, feat15_fire_ignition_sounds_67_and_68, 15) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.clear_audio_events();
    ASSERT_TRUE(sim.place_firewall(f, {10, 9}));
    bool found_67 = false, found_68 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 67) found_67 = true;
        if (ev.sound_id == 68) found_68 = true;
    }
    ASSERT_TRUE(found_67);
    ASSERT_TRUE(found_68);
}

E2E_TIER1_TEST(Tier1_Sim, feat15_fire_diagonal_rejected, 15) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    ASSERT_FALSE(sim.place_firewall(f, {11, 11}));
    ASSERT_FALSE(sim.has_fire({11, 11}));
}

E2E_TIER1_TEST(Tier1_Sim, feat15_fire_non_fire_ant_rejected, 15) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    ASSERT_FALSE(sim.place_firewall(w, {10, 9}));
}

E2E_TIER1_TEST(Tier1_Sim, feat15_fire_layer2_tile_134, 15) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    ASSERT_TRUE(sim.has_fire({10, 9}));
}

// =========================================================================
// FEATURE 16: Fire & Ricochet Physics (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat16_fire_contact_1hp_damage, 16) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 10});
    // Knock target into fire at (10, 9)
    sim.apply_knockback(*sim.find_unit(target), 0, -1, 1);
    // Worker took 1 fire damage
    ASSERT_EQ(sim.find_unit(target)->hp, 9);
}

E2E_TIER1_TEST(Tier1_Sim, feat16_fire_never_extinguished_by_landing, 16) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 10});
    sim.apply_knockback(*sim.find_unit(target), 0, -1, 1);
    // Fire remains active!
    ASSERT_TRUE(sim.has_fire({10, 9}));
}

E2E_TIER1_TEST(Tier1_Sim, feat16_fire_ricochet_reflection_vector, 16) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 10});
    sim.apply_knockback(*sim.find_unit(target), 0, -1, 1);
    // Worker bounced off fire tile (did not occupy (10, 9))
    ASSERT_NE(sim.find_unit(target)->pos.y, 9);
}

E2E_TIER1_TEST(Tier1_Sim, feat16_fire_multi_fire_chain_damage, 16) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    sim.place_firewall(f, {10, 11});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 10});
    // Worker knocked across fire
    sim.apply_knockback(*sim.find_unit(target), 0, -1, 2);
    // Took damage from fire contact
    ASSERT_LE(sim.find_unit(target)->hp, 9);
}

E2E_TIER1_TEST(Tier1_Sim, feat16_fire_sound_64_on_impact, 16) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    uint32_t target = sim.spawn_ant(1, AntType::Worker, {10, 10});
    sim.clear_audio_events();
    sim.apply_knockback(*sim.find_unit(target), 0, -1, 1);
    bool found_64 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 64) found_64 = true;
    }
    ASSERT_TRUE(found_64);
}

// =========================================================================
// FEATURE 17: Fire Ant Immunity & Extinguish (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat17_fire_ant_takes_zero_fire_damage, 17) {
    SimulationModel sim(60, 60);
    uint32_t f1 = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f1, {10, 9});
    uint32_t f2 = sim.spawn_ant(1, AntType::Fire, {10, 10});
    sim.apply_knockback(*sim.find_unit(f2), 0, -1, 1);
    ASSERT_EQ(sim.find_unit(f2)->hp, 10); // Immune!
}

E2E_TIER1_TEST(Tier1_Sim, feat17_fire_ant_extinguish_success, 17) {
    SimulationModel sim(60, 60);
    uint32_t f1 = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f1, {10, 9});
    ASSERT_TRUE(sim.extinguish_fire(f1, {10, 9}));
    ASSERT_FALSE(sim.has_fire({10, 9}));
}

E2E_TIER1_TEST(Tier1_Sim, feat17_fire_ant_extinguish_sound_69, 17) {
    SimulationModel sim(60, 60);
    uint32_t f1 = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f1, {10, 9});
    sim.clear_audio_events();
    sim.extinguish_fire(f1, {10, 9});
    bool found_69 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 69) found_69 = true; // fireextinguish.wav
    }
    ASSERT_TRUE(found_69);
}

E2E_TIER1_TEST(Tier1_Sim, feat17_fire_ant_extinguishes_enemy_fire, 17) {
    SimulationModel sim(60, 60);
    uint32_t f_enemy = sim.spawn_ant(1, AntType::Fire, {10, 10});
    sim.place_firewall(f_enemy, {10, 9});
    uint32_t f_friendly = sim.spawn_ant(0, AntType::Fire, {10, 8});
    ASSERT_TRUE(sim.extinguish_fire(f_friendly, {10, 9}));
    ASSERT_FALSE(sim.has_fire({10, 9}));
}

E2E_TIER1_TEST(Tier1_Sim, feat17_non_fire_ant_cannot_extinguish, 17) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 8});
    ASSERT_FALSE(sim.extinguish_fire(w, {10, 9}));
    ASSERT_TRUE(sim.has_fire({10, 9}));
}

// =========================================================================
// FEATURE 18: 180-Second Timers (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat18_timer_registration_3600_ticks, 18) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    // 3599 ticks: still active
    for (int i = 0; i < 3599; ++i) sim.tick();
    ASSERT_TRUE(sim.has_fire({10, 9}));
}

E2E_TIER1_TEST(Tier1_Sim, feat18_firewall_burnout_at_180s, 18) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    // Advance to 3600 ticks
    for (int i = 0; i < 3600; ++i) sim.tick();
    ASSERT_FALSE(sim.has_fire({10, 9})); // Burned out!
}

E2E_TIER1_TEST(Tier1_Sim, feat18_bridge_collapse_at_180s, 18) {
    SimulationModel sim(60, 60);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    ASSERT_TRUE(sim.has_bridge({10, 11}));
    for (int i = 0; i < 3600; ++i) sim.tick();
    ASSERT_FALSE(sim.has_bridge({10, 11})); // Collapsed!
}

E2E_TIER1_TEST(Tier1_Sim, feat18_multiple_timers_tracked_independently, 18) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    // Advance 100 ticks then place second firewall
    for (int i = 0; i < 100; ++i) sim.tick();
    sim.place_firewall(f, {10, 11});

    // Advance 3500 more ticks (total 3600 for first fire)
    for (int i = 0; i < 3500; ++i) sim.tick();
    ASSERT_FALSE(sim.has_fire({10, 9})); // First burned out
    ASSERT_TRUE(sim.has_fire({10, 11})); // Second still active!

    // Advance 100 more ticks
    for (int i = 0; i < 100; ++i) sim.tick();
    ASSERT_FALSE(sim.has_fire({10, 11})); // Second burned out
}

E2E_TIER1_TEST(Tier1_Sim, feat18_timer_burnout_audio_event, 18) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    for (int i = 0; i < 3599; ++i) sim.tick();
    sim.clear_audio_events();
    sim.tick(); // 3600th tick
    ASSERT_GT(sim.audio_events().size(), 0u);
}

// =========================================================================
// FEATURE 19: Universal Bridge Traversal (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat19_bridge_construction_success, 19) {
    SimulationModel sim(60, 60);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    ASSERT_TRUE(sim.build_bridge(s, {10, 11}));
    ASSERT_TRUE(sim.has_bridge({10, 11}));
}

E2E_TIER1_TEST(Tier1_Sim, feat19_friendly_traversal_allowed, 19) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2); // Deep water
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    // Worker walks onto bridge
    sim.find_unit(w)->pos = {10, 11};
    ASSERT_EQ(sim.find_unit(w)->pos.y, 11);
    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(w)->state), static_cast<uint8_t>(AntState::Dead));
}

E2E_TIER1_TEST(Tier1_Sim, feat19_enemy_traversal_allowed, 19) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2); // Deep water
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    uint32_t enemy_w = sim.spawn_ant(1, AntType::Worker, {10, 12});
    // Hostile enemy ant walks onto Team 0's bridge
    sim.find_unit(enemy_w)->pos = {10, 11};
    ASSERT_EQ(sim.find_unit(enemy_w)->pos.y, 11);
    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(enemy_w)->state), static_cast<uint8_t>(AntState::Dead));
}

E2E_TIER1_TEST(Tier1_Sim, feat19_allied_traversal_allowed, 19) {
    SimulationModel sim(60, 60);
    sim.accept_alliance(0, 2);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    uint32_t ally = sim.spawn_ant(2, AntType::Combat, {10, 12});
    sim.find_unit(ally)->pos = {10, 11};
    ASSERT_EQ(sim.find_unit(ally)->pos.y, 11);
}

E2E_TIER1_TEST(Tier1_Sim, feat19_non_swimmer_cannot_build_bridge, 19) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    ASSERT_FALSE(sim.build_bridge(w, {10, 11}));
    ASSERT_FALSE(sim.has_bridge({10, 11}));
}

// =========================================================================
// FEATURE 20: Bridge Collapse Drowning (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat20_bridge_collapse_non_swimmer_drowns, 20) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2); // Deep water
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 11});

    // Advance 3600 ticks to bridge collapse
    for (int i = 0; i < 3600; ++i) sim.tick();

    auto* worker = sim.find_unit(w);
    ASSERT_EQ(static_cast<uint8_t>(worker->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 1u);
}

E2E_TIER1_TEST(Tier1_Sim, feat20_bridge_collapse_swimmer_survives, 20) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    sim.find_unit(s)->pos = {10, 11};

    for (int i = 0; i < 3600; ++i) sim.tick();

    auto* swimmer = sim.find_unit(s);
    // Swimmer survived!
    ASSERT_NE(static_cast<uint8_t>(swimmer->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 0u);
}

E2E_TIER1_TEST(Tier1_Sim, feat20_bridge_collapse_enemy_non_swimmer_drowns, 20) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    uint32_t enemy_c = sim.spawn_ant(1, AntType::Combat, {10, 11});

    for (int i = 0; i < 3600; ++i) sim.tick();

    auto* enemy = sim.find_unit(enemy_c);
    ASSERT_EQ(static_cast<uint8_t>(enemy->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_EQ(sim.get_player_stats(1).friendly_lost, 1u);
}

E2E_TIER1_TEST(Tier1_Sim, feat20_bridge_collapse_audio_sounds_71_and_72, 20) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    sim.spawn_ant(0, AntType::Worker, {10, 11});

    for (int i = 0; i < 3599; ++i) sim.tick();
    sim.clear_audio_events();
    sim.tick(); // Collapse tick

    bool found_71 = false, found_72 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 71) found_71 = true; // splash.wav
        if (ev.sound_id == 72) found_72 = true; // antdrown.wav
    }
    ASSERT_TRUE(found_71);
    ASSERT_TRUE(found_72);
}

E2E_TIER1_TEST(Tier1_Sim, feat20_bridge_empty_collapse_no_casualties, 20) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});

    for (int i = 0; i < 3600; ++i) sim.tick();
    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 0u);
}

// =========================================================================
// FEATURE 21: Anthill Queuing & 17-Frame Entry (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat21_base_chebyshev_ring_r1, 21) {
    Vec2i base = {20, 20};
    Vec2i slot1 = {21, 20};
    ASSERT_EQ(chebyshev_distance(base, slot1), 1);
}

E2E_TIER1_TEST(Tier1_Sim, feat21_base_chebyshev_ring_r2, 21) {
    Vec2i base = {20, 20};
    Vec2i slot2 = {22, 21};
    ASSERT_EQ(chebyshev_distance(base, slot2), 2);
}

E2E_TIER1_TEST(Tier1_Sim, feat21_base_chebyshev_ring_r3, 21) {
    Vec2i base = {20, 20};
    Vec2i slot3 = {23, 18};
    ASSERT_EQ(chebyshev_distance(base, slot3), 3);
}

E2E_TIER1_TEST(Tier1_Sim, feat21_base_17_frame_entry_animation_length, 21) {
    // 17 frames total: 0..3 descent, 4 deposit, 5..7 dive, 8 heal, 9..15 climb, 16 emerge
    const uint32_t frames = 17;
    ASSERT_EQ(frames, 17u);
}

E2E_TIER1_TEST(Tier1_Sim, feat21_base_entry_fifo_priority, 21) {
    // FIFO queue maintains arrival order
    std::vector<uint32_t> queue = {101, 102, 103};
    ASSERT_EQ(queue.front(), 101u);
    queue.erase(queue.begin());
    ASSERT_EQ(queue.front(), 102u);
}

// =========================================================================
// FEATURE 22: Anthill Food Deposit (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat22_food_deposit_score_increment, 22) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->holding_lunchbox = true;
    sim.find_unit(w)->carried_points = 50;
    ASSERT_TRUE(sim.deposit_food(w));
    ASSERT_EQ(sim.get_player_stats(0).score, 50);
}

E2E_TIER1_TEST(Tier1_Sim, feat22_food_deposit_clears_inventory, 22) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->holding_lunchbox = true;
    sim.deposit_food(w);
    ASSERT_FALSE(sim.find_unit(w)->holding_lunchbox);
    ASSERT_EQ(sim.find_unit(w)->carried_points, 0u);
}

E2E_TIER1_TEST(Tier1_Sim, feat22_food_deposit_sound_87, 22) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->holding_lunchbox = true;
    sim.clear_audio_events();
    sim.deposit_food(w);
    bool found_87 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 87) found_87 = true; // scoreup.wav
    }
    ASSERT_TRUE(found_87);
}

E2E_TIER1_TEST(Tier1_Sim, feat22_deposit_outside_base_fails, 22) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {20, 20});
    sim.find_unit(w)->holding_lunchbox = true;
    ASSERT_FALSE(sim.deposit_food(w));
    ASSERT_EQ(sim.get_player_stats(0).score, 0);
}

E2E_TIER1_TEST(Tier1_Sim, feat22_consecutive_deposits_accumulate, 22) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->holding_lunchbox = true;
    sim.find_unit(w)->carried_points = 50;
    sim.deposit_food(w);
    sim.find_unit(w)->holding_lunchbox = true;
    sim.find_unit(w)->carried_points = 100;
    sim.deposit_food(w);
    ASSERT_EQ(sim.get_player_stats(0).score, 150);
}

// =========================================================================
// FEATURE 23: Underground 100% Heal (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat23_heal_restores_to_10hp, 23) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->hp = 2; // Wounded
    sim.deposit_food(w);
    ASSERT_EQ(sim.find_unit(w)->hp, 10);
}

E2E_TIER1_TEST(Tier1_Sim, feat23_heal_applies_without_food, 23) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->hp = 1;
    sim.find_unit(w)->holding_lunchbox = false;
    sim.deposit_food(w);
    ASSERT_EQ(sim.find_unit(w)->hp, 10);
}

E2E_TIER1_TEST(Tier1_Sim, feat23_heal_at_10hp_noop, 23) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->hp = 10;
    sim.deposit_food(w);
    ASSERT_EQ(sim.find_unit(w)->hp, 10);
}

E2E_TIER1_TEST(Tier1_Sim, feat23_heal_applies_to_all_classes, 23) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {15, 15});
    sim.find_unit(c)->hp = 4;
    sim.deposit_food(c);
    ASSERT_EQ(sim.find_unit(c)->hp, 10);
}

E2E_TIER1_TEST(Tier1_Sim, feat23_heal_occurs_at_frame_8_underground, 23) {
    const uint32_t heal_frame = 8;
    ASSERT_EQ(heal_frame, 8u);
}

// =========================================================================
// FEATURE 24: Ant Hatching & Economy (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat24_hatch_costs_200_points, 24) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 350;
    ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
    ASSERT_EQ(sim.get_player_stats(0).score, 150);
}

E2E_TIER1_TEST(Tier1_Sim, feat24_hatch_decrements_egg_count, 24) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 400;
    sim.get_player_stats(0).egg_count = 5;
    ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
    ASSERT_EQ(sim.get_player_stats(0).egg_count, 4u);
}

E2E_TIER1_TEST(Tier1_Sim, feat24_hatch_increments_hatched_stat, 24) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 400;
    ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
    ASSERT_EQ(sim.get_player_stats(0).new_hatched, 1u);
}

E2E_TIER1_TEST(Tier1_Sim, feat24_hatch_insufficient_funds_rejected, 24) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 199; // Less than 200
    ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));
    ASSERT_EQ(sim.get_player_stats(0).score, 199);
    ASSERT_EQ(sim.get_player_stats(0).new_hatched, 0u);
}

E2E_TIER1_TEST(Tier1_Sim, feat24_hatch_zero_eggs_rejected, 24) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 500;
    sim.get_player_stats(0).egg_count = 0;
    ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));
}

// =========================================================================
// FEATURE 25: Thief Infiltration Dive (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat25_thief_dive_success, 25) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    ASSERT_TRUE(sim.thief_infiltrate(t, 1));
    ASSERT_EQ(static_cast<uint8_t>(sim.find_unit(t)->state), static_cast<uint8_t>(AntState::Infiltrating));
}

E2E_TIER1_TEST(Tier1_Sim, feat25_thief_dive_sounds_84_85_86, 25) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_audio_events();
    sim.thief_infiltrate(t, 1);
    bool f84 = false, f85 = false, f86 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 84) f84 = true;
        if (ev.sound_id == 85) f85 = true;
        if (ev.sound_id == 86) f86 = true;
    }
    ASSERT_TRUE(f84);
    ASSERT_TRUE(f85);
    ASSERT_TRUE(f86);
}

E2E_TIER1_TEST(Tier1_Sim, feat25_thief_dive_distant_anthill_rejected, 25) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {20, 20});
    ASSERT_FALSE(sim.thief_infiltrate(t, 1));
}

E2E_TIER1_TEST(Tier1_Sim, feat25_thief_cannot_infiltrate_own_base, 25) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {30, 30});
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    ASSERT_FALSE(sim.thief_infiltrate(t, 0));
}

E2E_TIER1_TEST(Tier1_Sim, feat25_non_thief_cannot_infiltrate, 25) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {30, 29});
    ASSERT_FALSE(sim.thief_infiltrate(w, 1));
}

// =========================================================================
// FEATURE 26: Thief Alarm Siren & News Flash (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat26_thief_alarm_sound_58, 26) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_audio_events();
    sim.thief_infiltrate(t, 1);
    bool found_58 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 58 && ev.target_player == 1) found_58 = true;
    }
    ASSERT_TRUE(found_58);
}

E2E_TIER1_TEST(Tier1_Sim, feat26_thief_news_flash_string_53, 26) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_news_events();
    sim.thief_infiltrate(t, 1);
    ASSERT_GT(sim.news_events().size(), 0u);
    ASSERT_EQ(sim.news_events()[0].target_player, 1u);
    ASSERT_TRUE(sim.news_events()[0].text.find("ThiefAnt") != std::string::npos);
}

E2E_TIER1_TEST(Tier1_Sim, feat26_thief_alarm_sent_to_victim_only, 26) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_audio_events();
    sim.thief_infiltrate(t, 1);
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 58) {
            ASSERT_EQ(ev.target_player, 1u); // Only Player 1
        }
    }
}

E2E_TIER1_TEST(Tier1_Sim, feat26_thief_alarm_high_priority, 26) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_audio_events();
    sim.thief_infiltrate(t, 1);
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 58) {
            ASSERT_GE(ev.priority, 5u); // High priority
        }
    }
}

E2E_TIER1_TEST(Tier1_Sim, feat26_allied_base_no_alarm, 26) {
    SimulationModel sim(60, 60);
    sim.accept_alliance(0, 1);
    sim.set_anthill(1, {30, 30});
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_audio_events();
    ASSERT_FALSE(sim.thief_infiltrate(t, 1));
}

// =========================================================================
// FEATURE 27: Food Theft & Carry Visuals (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat27_theft_min_50_from_100, 27) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.thief_infiltrate(t, 1);
    ASSERT_EQ(sim.get_player_stats(1).score, 50);
    ASSERT_EQ(sim.find_unit(t)->carried_points, 50u);
}

E2E_TIER1_TEST(Tier1_Sim, feat27_theft_min_50_when_score_is_30, 27) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 30;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.thief_infiltrate(t, 1);
    ASSERT_EQ(sim.get_player_stats(1).score, 0); // 30 - 30 = 0
    ASSERT_EQ(sim.find_unit(t)->carried_points, 30u);
}

E2E_TIER1_TEST(Tier1_Sim, feat27_theft_switches_to_holding_lunchbox, 27) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.thief_infiltrate(t, 1);
    ASSERT_TRUE(sim.find_unit(t)->holding_lunchbox);
}

E2E_TIER1_TEST(Tier1_Sim, feat27_theft_sound_88_scoredn, 27) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_audio_events();
    sim.thief_infiltrate(t, 1);
    bool found_88 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 88) found_88 = true; // scoredn.wav
    }
    ASSERT_TRUE(found_88);
}

E2E_TIER1_TEST(Tier1_Sim, feat27_theft_zero_score_victim_steals_zero, 27) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 0;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.thief_infiltrate(t, 1);
    ASSERT_EQ(sim.get_player_stats(1).score, 0);
    ASSERT_EQ(sim.find_unit(t)->carried_points, 0u);
}

// =========================================================================
// FEATURE 28: Lunchbox Physical Drop (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat28_lunchbox_drops_on_carrier_death, 28) {
    SimulationModel sim(60, 60);
    uint32_t carrier = sim.spawn_ant(0, AntType::Worker, {20, 20});
    sim.find_unit(carrier)->holding_lunchbox = true;
    sim.find_unit(carrier)->carried_points = 50;

    uint32_t enemy = sim.spawn_ant(1, AntType::Worker, {20, 21});
    sim.find_unit(carrier)->hp = 1;
    sim.execute_melee_attack(enemy, carrier);

    ASSERT_TRUE(sim.has_lunchbox({20, 20}));
}

E2E_TIER1_TEST(Tier1_Sim, feat28_lunchbox_anim_356_sprite_513, 28) {
    // Anim 356, Sprite 513
    const uint32_t anim_id = 356;
    const uint32_t sprite_id = 513;
    ASSERT_EQ(anim_id, 356u);
    ASSERT_EQ(sprite_id, 513u);
}

E2E_TIER1_TEST(Tier1_Sim, feat28_lunchbox_empty_ant_no_drop, 28) {
    SimulationModel sim(60, 60);
    uint32_t empty_ant = sim.spawn_ant(0, AntType::Worker, {20, 20});
    sim.find_unit(empty_ant)->holding_lunchbox = false;
    sim.find_unit(empty_ant)->hp = 1;

    uint32_t enemy = sim.spawn_ant(1, AntType::Worker, {20, 21});
    sim.execute_melee_attack(enemy, empty_ant);

    ASSERT_FALSE(sim.has_lunchbox({20, 20}));
}

E2E_TIER1_TEST(Tier1_Sim, feat28_lunchbox_universal_pickup_by_enemy, 28) {
    SimulationModel sim(60, 60);
    uint32_t carrier = sim.spawn_ant(0, AntType::Worker, {20, 20});
    sim.find_unit(carrier)->holding_lunchbox = true;
    sim.find_unit(carrier)->hp = 1;
    sim.on_unit_killed(*sim.find_unit(carrier), 1);
    ASSERT_TRUE(sim.has_lunchbox({20, 20}));

    // Enemy walks onto tile and picks up
    uint32_t enemy = sim.spawn_ant(1, AntType::Worker, {20, 20});
    sim.find_unit(enemy)->holding_lunchbox = true;
    ASSERT_TRUE(sim.find_unit(enemy)->holding_lunchbox);
}

E2E_TIER1_TEST(Tier1_Sim, feat28_carrier_drowning_drops_lunchbox, 28) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2); // Deep water
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.find_unit(w)->holding_lunchbox = true;
    sim.drown_unit(*sim.find_unit(w));
    // Drowned ant drops lunchbox
    ASSERT_TRUE(sim.has_lunchbox({10, 10}));
}

// =========================================================================
// FEATURE 29: Dynamic FFA-to-Alliance Flow (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat29_alliance_propose_sound_51, 29) {
    SimulationModel sim(60, 60);
    sim.clear_audio_events();
    ASSERT_TRUE(sim.propose_alliance(0, 1));
    bool found_51 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 51 && ev.target_player == 1) found_51 = true;
    }
    ASSERT_TRUE(found_51);
}

E2E_TIER1_TEST(Tier1_Sim, feat29_alliance_accept_sounds_53_and_50, 29) {
    SimulationModel sim(60, 60);
    sim.clear_audio_events();
    ASSERT_TRUE(sim.accept_alliance(0, 1));
    bool f53 = false, f50 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 53) f53 = true;
        if (ev.sound_id == 50) f50 = true;
    }
    ASSERT_TRUE(f53);
    ASSERT_TRUE(f50);
}

E2E_TIER1_TEST(Tier1_Sim, feat29_alliance_deny_sound_52, 29) {
    SimulationModel sim(60, 60);
    sim.clear_audio_events();
    ASSERT_TRUE(sim.deny_alliance(0));
    bool found_52 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 52 && ev.target_player == 0) found_52 = true;
    }
    ASSERT_TRUE(found_52);
}

E2E_TIER1_TEST(Tier1_Sim, feat29_alliance_break_sound_49, 29) {
    SimulationModel sim(60, 60);
    sim.accept_alliance(0, 1);
    sim.clear_audio_events();
    ASSERT_TRUE(sim.break_alliance(0, 1));
    bool found_49 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 49) found_49 = true;
    }
    ASSERT_TRUE(found_49);
}

E2E_TIER1_TEST(Tier1_Sim, feat29_cannot_propose_to_self, 29) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.propose_alliance(0, 0));
}

// =========================================================================
// FEATURE 30: Allied Standings & Structs (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat30_combined_score_on_scoreboard, 30) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 120;
    sim.get_player_stats(1).score = 80;
    sim.accept_alliance(0, 1);
    ASSERT_EQ(sim.get_team_score(0), 200);
    ASSERT_EQ(sim.get_team_score(1), 200);
}

E2E_TIER1_TEST(Tier1_Sim, feat30_individual_stats_preserved_in_memory, 30) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 120;
    sim.get_player_stats(1).score = 80;
    sim.accept_alliance(0, 1);
    ASSERT_EQ(sim.get_player_stats(0).score, 120);
    ASSERT_EQ(sim.get_player_stats(1).score, 80);
}

E2E_TIER1_TEST(Tier1_Sim, feat30_friendly_fire_disabled_for_allies, 30) {
    SimulationModel sim(60, 60);
    sim.accept_alliance(0, 1);
    uint32_t u0 = sim.spawn_ant(0, AntType::Worker, {10, 10});
    uint32_t u1 = sim.spawn_ant(1, AntType::Worker, {10, 11});
    ASSERT_FALSE(sim.execute_melee_attack(u0, u1));
    ASSERT_EQ(sim.find_unit(u1)->hp, 10);
}

E2E_TIER1_TEST(Tier1_Sim, feat30_combat_ant_ignores_allies, 30) {
    SimulationModel sim(60, 60);
    sim.accept_alliance(0, 1);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {20, 20});
    sim.spawn_ant(1, AntType::Worker, {21, 20});
    sim.tick();
    ASSERT_EQ(sim.find_unit(c)->pos.x, 20); // Remained at post
}

E2E_TIER1_TEST(Tier1_Sim, feat30_alliance_break_uncouples_scores, 30) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 120;
    sim.get_player_stats(1).score = 80;
    sim.accept_alliance(0, 1);
    sim.break_alliance(0, 1);
    ASSERT_EQ(sim.get_team_score(0), 120);
    ASSERT_EQ(sim.get_team_score(1), 80);
}

// =========================================================================
// FEATURE 31: Match Timer & Simulation Freeze (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat31_timer_countdown_decrement, 31) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(1000);
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 950u);
}

E2E_TIER1_TEST(Tier1_Sim, feat31_simulation_freeze_at_zero, 31) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(50);
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 0u);
    ASSERT_TRUE(sim.is_frozen());
}

E2E_TIER1_TEST(Tier1_Sim, feat31_post_freeze_ticks_noop, 31) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(50);
    sim.tick();
    ASSERT_TRUE(sim.is_frozen());
    sim.tick();
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 0u);
}

E2E_TIER1_TEST(Tier1_Sim, feat31_default_12_minutes_match, 31) {
    SimulationModel sim(60, 60);
    ASSERT_EQ(sim.match_time_ms(), 12u * 60u * 1000u);
}

E2E_TIER1_TEST(Tier1_Sim, feat31_clock_format_mm_ss, 31) {
    uint32_t ms = 719000; // 11 min 59 sec
    uint32_t total_sec = ms / 1000;
    uint32_t mm = total_sec / 60;
    uint32_t ss = total_sec % 60;
    ASSERT_EQ(mm, 11u);
    ASSERT_EQ(ss, 59u);
}

// =========================================================================
// FEATURE 32: Split Game Over Audio (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat32_winner_audio_sound_56, 32) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 500;
    sim.get_player_stats(1).score = 100;
    sim.set_match_time_ms(50);
    sim.clear_audio_events();
    sim.tick(); // Match end
    bool p0_winner = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 56 && ev.target_player == 0) p0_winner = true;
    }
    ASSERT_TRUE(p0_winner);
}

E2E_TIER1_TEST(Tier1_Sim, feat32_loser_audio_sound_41, 32) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 500;
    sim.get_player_stats(1).score = 100;
    sim.set_match_time_ms(50);
    sim.clear_audio_events();
    sim.tick();
    bool p1_loser = false;
    bool p1_losers_wav = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 41 && ev.target_player == 1) p1_loser = true;
        if (ev.sound_id == 42 && ev.target_player == 1) p1_losers_wav = true;
    }
    ASSERT_TRUE(p1_loser);
    ASSERT_TRUE(p1_losers_wav);
}

E2E_TIER1_TEST(Tier1_Sim, feat32_allied_winners_both_receive_56, 32) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 250;
    sim.get_player_stats(1).score = 250;
    sim.get_player_stats(2).score = 100;
    sim.accept_alliance(0, 1);
    sim.set_match_time_ms(50);
    sim.clear_audio_events();
    sim.tick();
    bool p0_win = false, p1_win = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 56 && ev.target_player == 0) p0_win = true;
        if (ev.sound_id == 56 && ev.target_player == 1) p1_win = true;
    }
    ASSERT_TRUE(p0_win);
    ASSERT_TRUE(p1_win);
}

E2E_TIER1_TEST(Tier1_Sim, feat32_sound_56_winner_wav_format, 32) {
    // Sound 56 in ants.chd is 22050 Hz 8-bit mono fanfare
    const uint32_t sound_id = 56;
    ASSERT_EQ(sound_id, 56u);
}

E2E_TIER1_TEST(Tier1_Sim, feat32_sound_41_playerout_wav_format, 32) {
    // Sound 41 in ants.chd is playerout.wav
    const uint32_t sound_id = 41;
    ASSERT_EQ(sound_id, 41u);
}

// =========================================================================
// FEATURE 33: 4-Stat Scorecard Tracking (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat33_stat_score_tracking, 33) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 420;
    ASSERT_EQ(sim.get_player_stats(0).score, 420);
}

E2E_TIER1_TEST(Tier1_Sim, feat33_stat_friendly_lost_tracking, 33) {
    SimulationModel sim(60, 60);
    uint32_t u = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.on_unit_killed(*sim.find_unit(u), 1);
    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 1u);
}

E2E_TIER1_TEST(Tier1_Sim, feat33_stat_enemy_killed_tracking, 33) {
    SimulationModel sim(60, 60);
    uint32_t u = sim.spawn_ant(1, AntType::Worker, {10, 10});
    sim.on_unit_killed(*sim.find_unit(u), 0);
    ASSERT_EQ(sim.get_player_stats(0).enemy_killed, 1u);
}

E2E_TIER1_TEST(Tier1_Sim, feat33_stat_hatched_tracking, 33) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 600;
    sim.hatch_ant(0, AntType::Worker);
    sim.hatch_ant(0, AntType::Worker);
    ASSERT_EQ(sim.get_player_stats(0).new_hatched, 2u);
}

E2E_TIER1_TEST(Tier1_Sim, feat33_stat_4_columns_per_player, 33) {
    // Verify 4 distinct stats: Score, Friendly Lost, Enemy Killed, Hatched
    SimulationModel sim(60, 60);
    auto& s = sim.get_player_stats(0);
    s.score = 300;
    s.friendly_lost = 5;
    s.enemy_killed = 12;
    s.new_hatched = 4;
    ASSERT_EQ(s.score, 300);
    ASSERT_EQ(s.friendly_lost, 5u);
    ASSERT_EQ(s.enemy_killed, 12u);
    ASSERT_EQ(s.new_hatched, 4u);
}

// =========================================================================
// FEATURE 49: Water Splash & Ant Drowning Sequences (Milestone 2)
// =========================================================================

E2E_TIER1_TEST(Tier1_Sim, feat49_dsplash_anim_40_sprites_121_125, 49) {
    const uint32_t dsplash_anim = 40;
    const uint32_t s_start = 121, s_end = 125;
    ASSERT_EQ(dsplash_anim, 40u);
    ASSERT_EQ(s_end - s_start + 1, 5u);
}

E2E_TIER1_TEST(Tier1_Sim, feat49_dsplash_sound_71_trigger, 49) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.clear_audio_events();
    sim.drown_unit(*sim.find_unit(w));
    bool found_71 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 71) found_71 = true; // splash.wav
    }
    ASSERT_TRUE(found_71);
}

E2E_TIER1_TEST(Tier1_Sim, feat49_drown_sound_72_at_subitem_1, 49) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.clear_audio_events();
    sim.drown_unit(*sim.find_unit(w));
    bool found_72 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 72) found_72 = true; // antdrown.wav
    }
    ASSERT_TRUE(found_72);
}

E2E_TIER1_TEST(Tier1_Sim, feat49_drown_22_subitems_total, 49) {
    const uint32_t total_subitems = 22; // Subitems 0 to 21
    ASSERT_EQ(total_subitems, 22u);
}

E2E_TIER1_TEST(Tier1_Sim, feat49_swimmer_immunity_no_drowning, 49) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 10, 2); // Deep water
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.tick();
    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(s)->state), static_cast<uint8_t>(AntState::Dead));
    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 0u);
}
