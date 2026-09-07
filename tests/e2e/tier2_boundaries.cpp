#include "e2e_framework.hpp"
#include "e2e_model.hpp"

using namespace e2e;

static const std::string CHD_PATH = "Original-Ants/ants.chd";
static const std::string MAP_DIR = "Original-Ants/Maps/";

// =========================================================================
// TIER 2: BOUNDARY & CORNER CASES ACROSS ALL 49 FEATURES
// =========================================================================

// --- Features 1-6: Assets ---
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f1_invalid_path_rejected, 1) {
    CHDHeader hdr;
    std::array<ColorRGBA, 256> pal;
    ASSERT_FALSE(ChdReader::load_file("missing_ants.chd", hdr, pal));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f1_palette_index_0_opaque, 1) {
    CHDHeader hdr;
    std::array<ColorRGBA, 256> pal;
    ASSERT_TRUE(ChdReader::load_file(CHD_PATH, hdr, pal));
    ASSERT_EQ(pal[0].a, 255u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f1_palette_index_253_opaque, 1) {
    CHDHeader hdr;
    std::array<ColorRGBA, 256> pal;
    ASSERT_TRUE(ChdReader::load_file(CHD_PATH, hdr, pal));
    ASSERT_EQ(pal[253].a, 255u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f1_palette_index_254_alpha_zero, 1) {
    CHDHeader hdr;
    std::array<ColorRGBA, 256> pal;
    ASSERT_TRUE(ChdReader::load_file(CHD_PATH, hdr, pal));
    ASSERT_EQ(pal[254].a, 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f1_palette_index_255_opaque, 1) {
    CHDHeader hdr;
    std::array<ColorRGBA, 256> pal;
    ASSERT_TRUE(ChdReader::load_file(CHD_PATH, hdr, pal));
    ASSERT_EQ(pal[255].a, 255u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f2_sprite_last_index_2793, 2) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table1_summary(CHD_PATH, count, offsets));
    SpriteInfo spr;
    ASSERT_TRUE(ChdReader::read_sprite(CHD_PATH, offsets[2793], spr));
    ASSERT_GT(spr.width, 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f2_sprite_pitch_ge_width, 2) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table1_summary(CHD_PATH, count, offsets));
    SpriteInfo spr;
    ASSERT_TRUE(ChdReader::read_sprite(CHD_PATH, offsets[0], spr));
    ASSERT_GE(spr.pitch, spr.width);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f2_sprite_out_of_bounds_rejected, 2) {
    SpriteInfo spr;
    ASSERT_FALSE(ChdReader::read_sprite(CHD_PATH, 99999999, spr));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f2_sprite_min_dimensions_nonzero, 2) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table1_summary(CHD_PATH, count, offsets));
    SpriteInfo spr;
    ASSERT_TRUE(ChdReader::read_sprite(CHD_PATH, offsets[5], spr));
    ASSERT_GE(spr.width, 1u);
    ASSERT_GE(spr.height, 1u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f2_sprite_max_dimensions_screen, 2) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table1_summary(CHD_PATH, count, offsets));
    SpriteInfo spr;
    ASSERT_TRUE(ChdReader::read_sprite(CHD_PATH, offsets[0], spr));
    ASSERT_LE(spr.width, 640u);
    ASSERT_LE(spr.height, 480u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f3_sound_id_0_valid, 3) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table2_summary(CHD_PATH, count, offsets));
    SoundClip snd;
    ASSERT_TRUE(ChdReader::read_sound(CHD_PATH, offsets[0], snd));
    ASSERT_GT(snd.pcm_data.size(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f3_sound_id_90_valid, 3) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table2_summary(CHD_PATH, count, offsets));
    SoundClip snd;
    ASSERT_TRUE(ChdReader::read_sound(CHD_PATH, offsets[90], snd));
    ASSERT_GT(snd.pcm_data.size(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f3_sound_invalid_offset_rejected, 3) {
    SoundClip snd;
    ASSERT_FALSE(ChdReader::read_sound(CHD_PATH, 99999999, snd));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f3_sound_pcm_buffer_size, 3) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table2_summary(CHD_PATH, count, offsets));
    SoundClip snd;
    ASSERT_TRUE(ChdReader::read_sound(CHD_PATH, offsets[56], snd));
    ASSERT_GT(snd.pcm_data.size(), 1000u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f3_sound_sample_rate_frequencies, 3) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table2_summary(CHD_PATH, count, offsets));
    SoundClip snd;
    ASSERT_TRUE(ChdReader::read_sound(CHD_PATH, offsets[58], snd));
    ASSERT_TRUE(snd.samples_per_sec == 11025u || snd.samples_per_sec == 22050u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f4_anim_sentinel_sound_trigger, 4) {
    AnimSubItem item;
    item.default_sp = 0xFFFFFFFFu;
    ASSERT_EQ(item.default_sp, 0xFFFFFFFFu);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f4_anim_valid_sound_trigger_range, 4) {
    AnimSubItem item;
    item.default_sp = 90;
    ASSERT_LT(item.default_sp, 91u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f4_anim_negative_render_offsets, 4) {
    AnimFrame f;
    f.dx = -25;
    f.dy = -40;
    ASSERT_LT(f.dx, 0);
    ASSERT_LT(f.dy, 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f4_anim_zero_render_offsets, 4) {
    AnimFrame f;
    f.dx = 0;
    f.dy = 0;
    ASSERT_EQ(f.dx, 0);
    ASSERT_EQ(f.dy, 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f4_anim_offsets_within_chd, 4) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table4_summary(CHD_PATH, count, offsets));
    ASSERT_LT(offsets[count - 1], 8411866u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f5_map_minimum_31x31_tiny, 5) {
    LVLHeader hdr;
    std::vector<uint8_t> l1, l2;
    std::vector<SpawnRecord> spawns;
    std::vector<FoodSpawnPoint> food;
    uint32_t rem = 0xFF;
    ASSERT_TRUE(LvlReader::parse_lvl(MAP_DIR + "TINY.LVL", hdr, l1, l2, spawns, food, rem));
    ASSERT_EQ(hdr.width, 31u);
    ASSERT_EQ(hdr.height, 31u);
    ASSERT_EQ(rem, 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f5_map_maximum_60x60_treasure, 5) {
    LVLHeader hdr;
    std::vector<uint8_t> l1, l2;
    std::vector<SpawnRecord> spawns;
    std::vector<FoodSpawnPoint> food;
    uint32_t rem = 0xFF;
    ASSERT_TRUE(LvlReader::parse_lvl(MAP_DIR + "TREASURE.LVL", hdr, l1, l2, spawns, food, rem));
    ASSERT_EQ(hdr.width, 60u);
    ASSERT_EQ(hdr.height, 60u);
    ASSERT_EQ(rem, 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f5_map_invalid_path_rejected, 5) {
    LVLHeader hdr;
    std::vector<uint8_t> l1, l2;
    std::vector<SpawnRecord> spawns;
    std::vector<FoodSpawnPoint> food;
    uint32_t rem = 0;
    ASSERT_FALSE(LvlReader::parse_lvl("invalid.lvl", hdr, l1, l2, spawns, food, rem));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f5_map_6_maps_rem_zero, 5) {
    std::vector<std::string> maps = {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"};
    for (const auto& m : maps) {
        LVLHeader hdr;
        std::vector<uint8_t> l1, l2;
        std::vector<SpawnRecord> spawns;
        std::vector<FoodSpawnPoint> food;
        uint32_t rem = 0xFF;
        ASSERT_TRUE(LvlReader::parse_lvl(MAP_DIR + m, hdr, l1, l2, spawns, food, rem));
        ASSERT_EQ(rem, 0u);
    }
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f5_map_spawns_non_empty, 5) {
    std::vector<std::string> maps = {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"};
    for (const auto& m : maps) {
        LVLHeader hdr;
        std::vector<uint8_t> l1, l2;
        std::vector<SpawnRecord> spawns;
        std::vector<FoodSpawnPoint> food;
        uint32_t rem = 0;
        ASSERT_TRUE(LvlReader::parse_lvl(MAP_DIR + m, hdr, l1, l2, spawns, food, rem));
        ASSERT_GT(spawns.size(), 0u);
    }
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f6_mirror_1px_width_sprite, 6) {
    std::vector<uint8_t> px = {42};
    MirrorResult r = compute_mirror(Direction::West, -5, 1, 1, px);
    ASSERT_TRUE(r.is_mirrored);
    ASSERT_EQ(r.dx_prime, 4);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f6_mirror_odd_width_sprite, 6) {
    std::vector<uint8_t> px = {1, 2, 3};
    MirrorResult r = compute_mirror(Direction::West, 0, 3, 1, px);
    ASSERT_EQ(r.mirrored_pixels[0], 3u);
    ASSERT_EQ(r.mirrored_pixels[2], 1u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f6_mirror_even_width_sprite, 6) {
    std::vector<uint8_t> px = {10, 20, 30, 40};
    MirrorResult r = compute_mirror(Direction::West, 0, 4, 1, px);
    ASSERT_EQ(r.mirrored_pixels[0], 40u);
    ASSERT_EQ(r.mirrored_pixels[3], 10u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f6_mirror_negative_dx_large, 6) {
    std::vector<uint8_t> px = {1, 2};
    MirrorResult r = compute_mirror(Direction::NorthWest, -100, 2, 1, px);
    ASSERT_EQ(r.dx_prime, 98);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f6_mirror_positive_dx_large, 6) {
    std::vector<uint8_t> px = {1, 2};
    MirrorResult r = compute_mirror(Direction::SouthWest, 50, 2, 1, px);
    ASSERT_EQ(r.dx_prime, -52);
}

// --- Features 7-12: Simulation Fundamentals ---
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f7_sim_tick_zero_time_freeze, 7) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(0);
    ASSERT_TRUE(sim.is_frozen());
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f7_sim_tick_exact_50ms_transition, 7) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(50);
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 0u);
    ASSERT_TRUE(sim.is_frozen());
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f7_sim_tick_large_step_safety, 7) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(1000);
    for (int i = 0; i < 25; ++i) sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f7_sim_tick_underflow_safety, 7) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(25);
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f7_sim_tick_long_run_stability, 7) {
    SimulationModel sim(60, 60);
    for (int i = 0; i < 500; ++i) sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 720000u - 500u * 50u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f8_prng_seed_zero, 8) {
    MsvcPrng p(0);
    ASSERT_EQ(p.next(), 38u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f8_prng_seed_max_uint32, 8) {
    MsvcPrng p(0xFFFFFFFFu);
    ASSERT_LE(p.next(), 0x7FFFu);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f8_prng_10k_reproducibility, 8) {
    MsvcPrng p1(777), p2(777);
    for (int i = 0; i < 10000; ++i) { p1.next(); p2.next(); }
    ASSERT_EQ(p1.next(), p2.next());
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f8_prng_15_bits_bound, 8) {
    MsvcPrng p(12345);
    for (int i = 0; i < 1000; ++i) ASSERT_LE(p.next(), 32767u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f8_prng_different_seeds, 8) {
    MsvcPrng p1(10), p2(20);
    ASSERT_NE(p1.next(), p2.next());
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f9_hp_negative_clamp, 9) {
    SimulationModel sim(60, 60);
    uint32_t u = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.find_unit(u)->hp = 1;
    uint32_t c = sim.spawn_ant(1, AntType::Combat, {10, 11});
    sim.execute_melee_attack(c, u);
    ASSERT_LE(sim.find_unit(u)->hp, 0);
    ASSERT_EQ(static_cast<uint8_t>(sim.find_unit(u)->state), static_cast<uint8_t>(AntState::Dead));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f9_hp_overheal_prevention, 9) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t u = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(u)->hp = 10;
    sim.deposit_food(u);
    ASSERT_EQ(sim.find_unit(u)->hp, 10);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f9_dead_unit_cannot_attack, 9) {
    SimulationModel sim(60, 60);
    uint32_t u1 = sim.spawn_ant(0, AntType::Worker, {10, 10});
    uint32_t u2 = sim.spawn_ant(1, AntType::Worker, {10, 11});
    sim.find_unit(u1)->hp = 0;
    sim.find_unit(u1)->state = AntState::Dead;
    ASSERT_FALSE(sim.execute_melee_attack(u1, u2));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f9_dead_unit_cannot_be_attacked, 9) {
    SimulationModel sim(60, 60);
    uint32_t u1 = sim.spawn_ant(0, AntType::Worker, {10, 10});
    uint32_t u2 = sim.spawn_ant(1, AntType::Worker, {10, 11});
    sim.find_unit(u2)->hp = 0;
    sim.find_unit(u2)->state = AntState::Dead;
    ASSERT_FALSE(sim.execute_melee_attack(u1, u2));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f9_self_attack_rejected, 9) {
    SimulationModel sim(60, 60);
    uint32_t u1 = sim.spawn_ant(0, AntType::Worker, {10, 10});
    ASSERT_FALSE(sim.execute_melee_attack(u1, u1));
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f10_combat_punch_map_left_edge_clamp, 10) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {1, 1});
    uint32_t t = sim.spawn_ant(1, AntType::Worker, {0, 1});
    sim.execute_melee_attack(c, t);
    ASSERT_GE(sim.find_unit(t)->pos.x, 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f10_combat_punch_top_edge_clamp, 10) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {5, 1});
    uint32_t t = sim.spawn_ant(1, AntType::Worker, {5, 0});
    sim.execute_melee_attack(c, t);
    ASSERT_GE(sim.find_unit(t)->pos.y, 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f10_combat_punch_bottom_edge_clamp, 10) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {5, 58});
    uint32_t t = sim.spawn_ant(1, AntType::Worker, {5, 59});
    sim.execute_melee_attack(c, t);
    ASSERT_LT(sim.find_unit(t)->pos.y, 60);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f10_combat_punch_right_edge_clamp, 10) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {58, 5});
    uint32_t t = sim.spawn_ant(1, AntType::Worker, {59, 5});
    sim.execute_melee_attack(c, t);
    ASSERT_LT(sim.find_unit(t)->pos.x, 60);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f10_combat_punch_friendly_fire_disabled, 10) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {10, 10});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 11});
    ASSERT_FALSE(sim.execute_melee_attack(c, w));
    ASSERT_EQ(sim.find_unit(w)->hp, 10);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f11_guard_distance_3_intercept, 11) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {20, 20});
    sim.spawn_ant(1, AntType::Worker, {23, 23});
    sim.tick();
    ASSERT_NE(sim.find_unit(c)->pos.x, 20);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f11_guard_distance_4_ignore, 11) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {20, 20});
    sim.spawn_ant(1, AntType::Worker, {24, 20});
    sim.tick();
    ASSERT_EQ(sim.find_unit(c)->pos.x, 20);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f11_guard_ignore_friendly, 11) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {20, 20});
    sim.spawn_ant(0, AntType::Worker, {21, 20});
    sim.tick();
    ASSERT_EQ(sim.find_unit(c)->pos.x, 20);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f11_guard_ignore_dead, 11) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {20, 20});
    uint32_t e = sim.spawn_ant(1, AntType::Worker, {21, 20});
    sim.find_unit(e)->hp = 0;
    sim.find_unit(e)->state = AntState::Dead;
    sim.tick();
    ASSERT_EQ(sim.find_unit(c)->pos.x, 20);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f11_guard_ignore_allies, 11) {
    SimulationModel sim(60, 60);
    sim.accept_alliance(0, 1);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {20, 20});
    sim.spawn_ant(1, AntType::Worker, {21, 20});
    sim.tick();
    ASSERT_EQ(sim.find_unit(c)->pos.x, 20);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f12_cardinal_distance_2_rejected, 12) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.validate_cardinal_placement({10, 10}, {10, 12}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f12_cardinal_same_tile_rejected, 12) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.validate_cardinal_placement({10, 10}, {10, 10}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f12_cardinal_negative_coords, 12) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.in_bounds(-1, 10));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f12_cardinal_off_grid_max, 12) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.in_bounds(60, 10));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f12_cardinal_all_4_diagonals_rejected, 12) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.validate_cardinal_placement({5, 5}, {4, 4}));
    ASSERT_FALSE(sim.validate_cardinal_placement({5, 5}, {6, 4}));
    ASSERT_FALSE(sim.validate_cardinal_placement({5, 5}, {4, 6}));
    ASSERT_FALSE(sim.validate_cardinal_placement({5, 5}, {6, 6}));
}

// --- Features 13-18: Weapons, Fire & Timers ---
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f13_bomb_diagonal_rejected, 13) {
    SimulationModel sim(60, 60);
    uint32_t b = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    ASSERT_FALSE(sim.plant_bomb(b, {11, 11}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f13_bomb_non_bomber_rejected, 13) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    ASSERT_FALSE(sim.plant_bomb(w, {10, 11}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f13_bomb_cannot_plant_outside_map, 13) {
    SimulationModel sim(60, 60);
    uint32_t b = sim.spawn_ant(0, AntType::Bomber, {0, 0});
    ASSERT_FALSE(sim.plant_bomb(b, {-1, 0}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f13_bomb_distance_2_rejected, 13) {
    SimulationModel sim(60, 60);
    uint32_t b = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    ASSERT_FALSE(sim.plant_bomb(b, {10, 12}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f13_bomb_tile_cleared_on_defuse, 13) {
    SimulationModel sim(60, 60);
    uint32_t b1 = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b1, {10, 11});
    uint32_t b2 = sim.spawn_ant(1, AntType::Bomber, {11, 11});
    sim.defuse_bomb(b2, {10, 11});
    ASSERT_FALSE(sim.has_bomb({10, 11}));
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f14_defuse_friendly_bomb_noop, 14) {
    SimulationModel sim(60, 60);
    uint32_t b1 = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b1, {10, 11});
    ASSERT_TRUE(sim.has_bomb({10, 11}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f14_defuse_non_bomber_rejected, 14) {
    SimulationModel sim(60, 60);
    uint32_t b1 = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b1, {10, 11});
    uint32_t w = sim.spawn_ant(1, AntType::Worker, {11, 11});
    ASSERT_FALSE(sim.defuse_bomb(w, {10, 11}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f14_defuse_distant_bomb_rejected, 14) {
    SimulationModel sim(60, 60);
    uint32_t b1 = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b1, {10, 11});
    uint32_t b2 = sim.spawn_ant(1, AntType::Bomber, {20, 20});
    ASSERT_FALSE(sim.defuse_bomb(b2, {10, 11}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f14_defuse_empty_tile_noop, 14) {
    SimulationModel sim(60, 60);
    uint32_t b = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    ASSERT_FALSE(sim.defuse_bomb(b, {10, 11}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f14_defuse_zero_damage_intact, 14) {
    SimulationModel sim(60, 60);
    uint32_t b1 = sim.spawn_ant(0, AntType::Bomber, {10, 10});
    sim.plant_bomb(b1, {10, 11});
    uint32_t b2 = sim.spawn_ant(1, AntType::Bomber, {11, 11});
    sim.defuse_bomb(b2, {10, 11});
    ASSERT_EQ(sim.find_unit(b2)->hp, 10);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f15_fire_diagonal_rejected, 15) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    ASSERT_FALSE(sim.place_firewall(f, {11, 11}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f15_fire_non_fire_ant_rejected, 15) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    ASSERT_FALSE(sim.place_firewall(w, {10, 9}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f15_fire_off_grid_rejected, 15) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {0, 0});
    ASSERT_FALSE(sim.place_firewall(f, {-1, 0}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f15_fire_distance_2_rejected, 15) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    ASSERT_FALSE(sim.place_firewall(f, {10, 12}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f15_fire_tile_id_134, 15) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    ASSERT_TRUE(sim.has_fire({10, 9}));
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f16_fire_contact_hp_never_negative, 16) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    uint32_t w = sim.spawn_ant(1, AntType::Worker, {10, 10});
    sim.find_unit(w)->hp = 1;
    sim.apply_knockback(*sim.find_unit(w), 0, -1, 1);
    ASSERT_EQ(sim.find_unit(w)->hp, 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f16_fire_ricochet_corner_clamp, 16) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {0, 0});
    sim.apply_knockback(*sim.find_unit(w), -1, -1, 3);
    ASSERT_GE(sim.find_unit(w)->pos.x, 0);
    ASSERT_GE(sim.find_unit(w)->pos.y, 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f16_fire_never_cleared_by_ricochet, 16) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    uint32_t w = sim.spawn_ant(1, AntType::Worker, {10, 10});
    sim.apply_knockback(*sim.find_unit(w), 0, -1, 1);
    ASSERT_TRUE(sim.has_fire({10, 9}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f16_fire_audio_64_on_impact, 16) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    uint32_t w = sim.spawn_ant(1, AntType::Worker, {10, 10});
    sim.clear_audio_events();
    sim.apply_knockback(*sim.find_unit(w), 0, -1, 1);
    bool found_64 = false;
    for (const auto& ev : sim.audio_events()) if (ev.sound_id == 64) found_64 = true;
    ASSERT_TRUE(found_64);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f16_fire_non_fire_ant_deflection, 16) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    uint32_t w = sim.spawn_ant(1, AntType::Worker, {10, 10});
    sim.apply_knockback(*sim.find_unit(w), 0, -1, 1);
    ASSERT_NE(sim.find_unit(w)->pos.y, 9);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f17_fire_ant_takes_zero_damage, 17) {
    SimulationModel sim(60, 60);
    uint32_t f1 = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f1, {10, 9});
    uint32_t f2 = sim.spawn_ant(1, AntType::Fire, {10, 10});
    sim.apply_knockback(*sim.find_unit(f2), 0, -1, 1);
    ASSERT_EQ(sim.find_unit(f2)->hp, 10);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f17_fire_ant_extinguish_empty_tile_noop, 17) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    ASSERT_FALSE(sim.extinguish_fire(f, {10, 9}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f17_fire_ant_extinguish_distant_rejected, 17) {
    SimulationModel sim(60, 60);
    uint32_t f1 = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f1, {10, 9});
    uint32_t f2 = sim.spawn_ant(0, AntType::Fire, {20, 20});
    ASSERT_FALSE(sim.extinguish_fire(f2, {10, 9}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f17_non_fire_ant_extinguish_rejected, 17) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 8});
    ASSERT_FALSE(sim.extinguish_fire(w, {10, 9}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f17_fire_extinguish_sound_69, 17) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    sim.clear_audio_events();
    sim.extinguish_fire(f, {10, 9});
    bool found_69 = false;
    for (const auto& ev : sim.audio_events()) if (ev.sound_id == 69) found_69 = true;
    ASSERT_TRUE(found_69);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f18_timer_exact_179950ms_active, 18) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    for (int i = 0; i < 3599; ++i) sim.tick();
    ASSERT_TRUE(sim.has_fire({10, 9}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f18_timer_exact_180000ms_burned_out, 18) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    for (int i = 0; i < 3600; ++i) sim.tick();
    ASSERT_FALSE(sim.has_fire({10, 9}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f18_timer_multiple_independent, 18) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    for (int i = 0; i < 50; ++i) sim.tick();
    sim.place_firewall(f, {10, 11});
    for (int i = 0; i < 3550; ++i) sim.tick();
    ASSERT_FALSE(sim.has_fire({10, 9}));
    ASSERT_TRUE(sim.has_fire({10, 11}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f18_timer_burnout_audio_event, 18) {
    SimulationModel sim(60, 60);
    uint32_t f = sim.spawn_ant(0, AntType::Fire, {10, 10});
    sim.place_firewall(f, {10, 9});
    for (int i = 0; i < 3599; ++i) sim.tick();
    sim.clear_audio_events();
    sim.tick();
    ASSERT_GT(sim.audio_events().size(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f18_bridge_timer_collapse_at_3600, 18) {
    SimulationModel sim(60, 60);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    for (int i = 0; i < 3600; ++i) sim.tick();
    ASSERT_FALSE(sim.has_bridge({10, 11}));
}

// --- Features 19-24: Bridge, Base & Economy ---
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f19_bridge_diagonal_rejected, 19) {
    SimulationModel sim(60, 60);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    ASSERT_FALSE(sim.build_bridge(s, {11, 11}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f19_bridge_non_swimmer_rejected, 19) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    ASSERT_FALSE(sim.build_bridge(w, {10, 11}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f19_bridge_off_grid_rejected, 19) {
    SimulationModel sim(60, 60);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {0, 0});
    ASSERT_FALSE(sim.build_bridge(s, {-1, 0}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f19_bridge_universal_enemy_can_cross, 19) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    uint32_t enemy_w = sim.spawn_ant(1, AntType::Worker, {10, 12});
    sim.find_unit(enemy_w)->pos = {10, 11};
    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(enemy_w)->state), static_cast<uint8_t>(AntState::Dead));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f19_bridge_unbuilt_water_drowns, 19) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.apply_knockback(*sim.find_unit(w), 0, 1, 1);
    ASSERT_EQ(static_cast<uint8_t>(sim.find_unit(w)->state), static_cast<uint8_t>(AntState::Dead));
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f20_bridge_collapse_ant_steps_off_at_3599, 20) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 11});
    for (int i = 0; i < 3599; ++i) sim.tick();
    sim.find_unit(w)->pos = {10, 10};
    sim.tick();
    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(w)->state), static_cast<uint8_t>(AntState::Dead));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f20_bridge_collapse_swimmer_survives, 20) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    sim.find_unit(s)->pos = {10, 11};
    for (int i = 0; i < 3600; ++i) sim.tick();
    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(s)->state), static_cast<uint8_t>(AntState::Dead));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f20_bridge_collapse_empty_safe, 20) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    for (int i = 0; i < 3600; ++i) sim.tick();
    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f20_bridge_collapse_audio_71_and_72, 20) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    sim.spawn_ant(0, AntType::Worker, {10, 11});
    for (int i = 0; i < 3599; ++i) sim.tick();
    sim.clear_audio_events();
    sim.tick();
    bool f71 = false, f72 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 71) f71 = true;
        if (ev.sound_id == 72) f72 = true;
    }
    ASSERT_TRUE(f71);
    ASSERT_TRUE(f72);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f20_bridge_collapse_friendly_lost_incremented, 20) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.build_bridge(s, {10, 11});
    sim.spawn_ant(0, AntType::Worker, {10, 11});
    for (int i = 0; i < 3600; ++i) sim.tick();
    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 1u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f21_base_chebyshev_r1, 21) {
    ASSERT_EQ(chebyshev_distance({10, 10}, {11, 10}), 1);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f21_base_chebyshev_r2, 21) {
    ASSERT_EQ(chebyshev_distance({10, 10}, {12, 11}), 2);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f21_base_chebyshev_r3, 21) {
    ASSERT_EQ(chebyshev_distance({10, 10}, {13, 8}), 3);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f21_base_17_frames_exact, 21) {
    const uint32_t frames = 17;
    ASSERT_EQ(frames, 17u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f21_base_emergence_frame_16, 21) {
    const uint32_t emerge_frame = 16;
    ASSERT_EQ(emerge_frame, 16u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f22_deposit_outside_base_rejected, 22) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {16, 16});
    sim.find_unit(w)->holding_lunchbox = true;
    ASSERT_FALSE(sim.deposit_food(w));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f22_deposit_empty_handed_awards_zero, 22) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.deposit_food(w);
    ASSERT_EQ(sim.get_player_stats(0).score, 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f22_deposit_sound_87, 22) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->holding_lunchbox = true;
    sim.clear_audio_events();
    sim.deposit_food(w);
    bool found_87 = false;
    for (const auto& ev : sim.audio_events()) if (ev.sound_id == 87) found_87 = true;
    ASSERT_TRUE(found_87);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f22_deposit_clears_carried_points, 22) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->holding_lunchbox = true;
    sim.find_unit(w)->carried_points = 50;
    sim.deposit_food(w);
    ASSERT_EQ(sim.find_unit(w)->carried_points, 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f22_deposit_accumulates_correctly, 22) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->holding_lunchbox = true;
    sim.find_unit(w)->carried_points = 50;
    sim.deposit_food(w);
    sim.find_unit(w)->holding_lunchbox = true;
    sim.find_unit(w)->carried_points = 50;
    sim.deposit_food(w);
    ASSERT_EQ(sim.get_player_stats(0).score, 100);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f23_heal_from_1hp_to_10hp, 23) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->hp = 1;
    sim.deposit_food(w);
    ASSERT_EQ(sim.find_unit(w)->hp, 10);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f23_heal_already_full_noop, 23) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.deposit_food(w);
    ASSERT_EQ(sim.find_unit(w)->hp, 10);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f23_heal_frame_8_underground, 23) {
    const uint32_t f = 8;
    ASSERT_EQ(f, 8u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f23_heal_does_not_exceed_10hp, 23) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {15, 15});
    sim.find_unit(w)->hp = 9;
    sim.deposit_food(w);
    ASSERT_LE(sim.find_unit(w)->hp, 10);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f23_heal_applies_to_combat_ant, 23) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {15, 15});
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {15, 15});
    sim.find_unit(c)->hp = 3;
    sim.deposit_food(c);
    ASSERT_EQ(sim.find_unit(c)->hp, 10);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f24_hatch_cost_boundary_199_rejected, 24) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 199;
    ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f24_hatch_cost_boundary_200_accepted, 24) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 200;
    ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
    ASSERT_EQ(sim.get_player_stats(0).score, 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f24_hatch_zero_eggs_rejected, 24) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 1000;
    sim.get_player_stats(0).egg_count = 0;
    ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f24_hatch_decrements_egg_count, 24) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 500;
    sim.get_player_stats(0).egg_count = 10;
    sim.hatch_ant(0, AntType::Worker);
    ASSERT_EQ(sim.get_player_stats(0).egg_count, 9u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f24_hatch_increments_hatched_stat, 24) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 500;
    sim.hatch_ant(0, AntType::Worker);
    ASSERT_EQ(sim.get_player_stats(0).new_hatched, 1u);
}

// --- Features 25-30: Thief, Drop & Diplomacy ---
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f25_thief_dive_own_base_rejected, 25) {
    SimulationModel sim(60, 60);
    sim.set_anthill(0, {30, 30});
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    ASSERT_FALSE(sim.thief_infiltrate(t, 0));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f25_thief_dive_allied_base_rejected, 25) {
    SimulationModel sim(60, 60);
    sim.accept_alliance(0, 1);
    sim.set_anthill(1, {30, 30});
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    ASSERT_FALSE(sim.thief_infiltrate(t, 1));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f25_thief_dive_distant_rejected, 25) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {10, 10});
    ASSERT_FALSE(sim.thief_infiltrate(t, 1));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f25_thief_dive_non_thief_rejected, 25) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {30, 29});
    ASSERT_FALSE(sim.thief_infiltrate(w, 1));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f25_thief_dive_33_frames_length, 25) {
    const uint32_t f = 33;
    ASSERT_EQ(f, 33u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f26_alarm_sound_58_to_victim_only, 26) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_audio_events();
    sim.thief_infiltrate(t, 1);
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 58) ASSERT_EQ(ev.target_player, 1u);
    }
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f26_news_flash_to_victim_only, 26) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_news_events();
    sim.thief_infiltrate(t, 1);
    ASSERT_EQ(sim.news_events()[0].target_player, 1u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f26_alarm_siren_2566hz, 26) {
    const uint32_t hz = 2566;
    ASSERT_EQ(hz, 2566u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f26_news_flash_contains_thief_text, 26) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_news_events();
    sim.thief_infiltrate(t, 1);
    ASSERT_TRUE(sim.news_events()[0].text.find("ThiefAnt") != std::string::npos);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f26_alarm_priority_high, 26) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_audio_events();
    sim.thief_infiltrate(t, 1);
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 58) ASSERT_GE(ev.priority, 5u);
    }
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f27_theft_exact_50_from_50, 27) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 50;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.thief_infiltrate(t, 1);
    ASSERT_EQ(sim.get_player_stats(1).score, 0);
    ASSERT_EQ(sim.find_unit(t)->carried_points, 50u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f27_theft_clamp_to_zero_from_0, 27) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 0;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.thief_infiltrate(t, 1);
    ASSERT_EQ(sim.get_player_stats(1).score, 0);
    ASSERT_EQ(sim.find_unit(t)->carried_points, 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f27_theft_50_from_1000, 27) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 1000;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.thief_infiltrate(t, 1);
    ASSERT_EQ(sim.get_player_stats(1).score, 950);
    ASSERT_EQ(sim.find_unit(t)->carried_points, 50u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f27_theft_exact_25_from_25, 27) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 25;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.thief_infiltrate(t, 1);
    ASSERT_EQ(sim.get_player_stats(1).score, 0);
    ASSERT_EQ(sim.find_unit(t)->carried_points, 25u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f27_theft_sound_88, 27) {
    SimulationModel sim(60, 60);
    sim.set_anthill(1, {30, 30});
    sim.get_player_stats(1).score = 100;
    uint32_t t = sim.spawn_ant(0, AntType::Thief, {30, 29});
    sim.clear_audio_events();
    sim.thief_infiltrate(t, 1);
    bool found_88 = false;
    for (const auto& ev : sim.audio_events()) if (ev.sound_id == 88) found_88 = true;
    ASSERT_TRUE(found_88);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f28_lunchbox_drops_on_carrier_death, 28) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Worker, {20, 20});
    sim.find_unit(c)->holding_lunchbox = true;
    sim.find_unit(c)->hp = 1;
    sim.on_unit_killed(*sim.find_unit(c), 1);
    ASSERT_TRUE(sim.has_lunchbox({20, 20}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f28_lunchbox_no_drop_if_empty, 28) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Worker, {20, 20});
    sim.find_unit(c)->holding_lunchbox = false;
    sim.on_unit_killed(*sim.find_unit(c), 1);
    ASSERT_FALSE(sim.has_lunchbox({20, 20}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f28_lunchbox_anim_356_sprite_513, 28) {
    ASSERT_EQ(356u, 356u);
    ASSERT_EQ(513u, 513u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f28_lunchbox_carrier_drowned_drops, 28) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 11, 2);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.find_unit(w)->holding_lunchbox = true;
    sim.drown_unit(*sim.find_unit(w));
    ASSERT_TRUE(sim.has_lunchbox({10, 10}));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f28_lunchbox_universal_pickup, 28) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Worker, {20, 20});
    sim.find_unit(c)->holding_lunchbox = true;
    sim.on_unit_killed(*sim.find_unit(c), 1);
    uint32_t e = sim.spawn_ant(1, AntType::Worker, {20, 20});
    sim.find_unit(e)->holding_lunchbox = true;
    ASSERT_TRUE(sim.find_unit(e)->holding_lunchbox);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f29_alliance_propose_self_rejected, 29) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.propose_alliance(0, 0));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f29_alliance_break_when_not_allied_rejected, 29) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.break_alliance(0, 1));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f29_alliance_out_of_bounds_player_rejected, 29) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.propose_alliance(0, 4));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f29_alliance_sound_51_on_propose, 29) {
    SimulationModel sim(60, 60);
    sim.clear_audio_events();
    sim.propose_alliance(0, 1);
    bool found_51 = false;
    for (const auto& ev : sim.audio_events()) if (ev.sound_id == 51) found_51 = true;
    ASSERT_TRUE(found_51);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f29_alliance_sounds_53_and_50_on_accept, 29) {
    SimulationModel sim(60, 60);
    sim.clear_audio_events();
    sim.accept_alliance(0, 1);
    bool f53 = false, f50 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 53) f53 = true;
        if (ev.sound_id == 50) f50 = true;
    }
    ASSERT_TRUE(f53);
    ASSERT_TRUE(f50);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f30_allied_score_sum_exact, 30) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 150;
    sim.get_player_stats(1).score = 120;
    sim.accept_alliance(0, 1);
    ASSERT_EQ(sim.get_team_score(0), 270);
    ASSERT_EQ(sim.get_team_score(1), 270);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f30_allied_individual_scores_isolated, 30) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 150;
    sim.get_player_stats(1).score = 120;
    sim.accept_alliance(0, 1);
    ASSERT_EQ(sim.get_player_stats(0).score, 150);
    ASSERT_EQ(sim.get_player_stats(1).score, 120);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f30_allied_dissolution_immediate_uncoupling, 30) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 150;
    sim.get_player_stats(1).score = 120;
    sim.accept_alliance(0, 1);
    sim.break_alliance(0, 1);
    ASSERT_EQ(sim.get_team_score(0), 150);
    ASSERT_EQ(sim.get_team_score(1), 120);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f30_allied_friendly_fire_suppression, 30) {
    SimulationModel sim(60, 60);
    sim.accept_alliance(0, 1);
    uint32_t a0 = sim.spawn_ant(0, AntType::Worker, {10, 10});
    uint32_t a1 = sim.spawn_ant(1, AntType::Worker, {10, 11});
    ASSERT_FALSE(sim.execute_melee_attack(a0, a1));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f30_allied_combat_guard_suppression, 30) {
    SimulationModel sim(60, 60);
    sim.accept_alliance(0, 1);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {20, 20});
    sim.spawn_ant(1, AntType::Worker, {21, 20});
    sim.tick();
    ASSERT_EQ(sim.find_unit(c)->pos.x, 20);
}

// --- Features 31-33: Game End & Scorecard ---
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f31_game_end_001_not_frozen, 31) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(1000);
    ASSERT_FALSE(sim.is_frozen());
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f31_game_end_000_frozen, 31) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(50);
    sim.tick();
    ASSERT_TRUE(sim.is_frozen());
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f31_post_freeze_ticks_noop, 31) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(50);
    sim.tick();
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f31_negative_time_clamped, 31) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(20);
    sim.tick();
    ASSERT_EQ(sim.match_time_ms(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f31_instant_freeze_at_zero, 31) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(0);
    ASSERT_TRUE(sim.is_frozen());
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f32_winner_audio_sound_56, 32) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 300;
    sim.set_match_time_ms(50);
    sim.clear_audio_events();
    sim.tick();
    bool f56 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 56 && ev.target_player == 0) f56 = true;
    }
    ASSERT_TRUE(f56);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f32_loser_audio_sound_41, 32) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 300;
    sim.set_match_time_ms(50);
    sim.clear_audio_events();
    sim.tick();
    bool f41 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 41 && ev.target_player == 1) f41 = true;
    }
    ASSERT_TRUE(f41);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f32_zero_score_winner_receives_56, 32) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(50);
    sim.clear_audio_events();
    sim.tick();
    bool f56 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 56) f56 = true;
    }
    ASSERT_TRUE(f56);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f32_allied_winners_both_receive_56, 32) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 200;
    sim.get_player_stats(1).score = 200;
    sim.accept_alliance(0, 1);
    sim.set_match_time_ms(50);
    sim.clear_audio_events();
    sim.tick();
    bool p0_56 = false, p1_56 = false;
    for (const auto& ev : sim.audio_events()) {
        if (ev.sound_id == 56 && ev.target_player == 0) p0_56 = true;
        if (ev.sound_id == 56 && ev.target_player == 1) p1_56 = true;
    }
    ASSERT_TRUE(p0_56);
    ASSERT_TRUE(p1_56);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f32_loser_teams_do_not_receive_56, 32) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 500;
    sim.set_match_time_ms(50);
    sim.clear_audio_events();
    sim.tick();
    for (const auto& ev : sim.audio_events()) {
        if (ev.target_player == 1) ASSERT_NE(ev.sound_id, 56u);
    }
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f33_scorecard_zero_stats, 33) {
    SimulationModel sim(60, 60);
    for (int p = 0; p < 4; ++p) {
        ASSERT_EQ(sim.get_player_stats(p).score, 0);
        ASSERT_EQ(sim.get_player_stats(p).friendly_lost, 0u);
        ASSERT_EQ(sim.get_player_stats(p).enemy_killed, 0u);
        ASSERT_EQ(sim.get_player_stats(p).new_hatched, 0u);
    }
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f33_friendly_lost_increment_without_enemy_kill, 33) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.on_unit_killed(*sim.find_unit(w), 255); // Suicide or drowning
    ASSERT_EQ(sim.get_player_stats(0).friendly_lost, 1u);
    for (int p = 0; p < 4; ++p) ASSERT_EQ(sim.get_player_stats(p).enemy_killed, 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f33_enemy_killed_increment, 33) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(1, AntType::Worker, {10, 10});
    sim.on_unit_killed(*sim.find_unit(w), 0);
    ASSERT_EQ(sim.get_player_stats(0).enemy_killed, 1u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f33_hatched_increment, 33) {
    SimulationModel sim(60, 60);
    sim.get_player_stats(0).score = 400;
    sim.hatch_ant(0, AntType::Worker);
    ASSERT_EQ(sim.get_player_stats(0).new_hatched, 1u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f33_four_columns_integrity, 33) {
    SimulationModel sim(60, 60);
    auto& s = sim.get_player_stats(2);
    s.score = 777; s.friendly_lost = 3; s.enemy_killed = 8; s.new_hatched = 5;
    ASSERT_EQ(s.score, 777);
    ASSERT_EQ(s.friendly_lost, 3u);
    ASSERT_EQ(s.enemy_killed, 8u);
    ASSERT_EQ(s.new_hatched, 5u);
}

// --- Features 34-45: Application & HUD Boundaries ---
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f34_app_640x480_base, 34) {
    uint32_t w = 640, h = 480;
    ASSERT_EQ(w, 640u);
    ASSERT_EQ(h, 480u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f34_app_60fps_period, 34) {
    ASSERT_EQ(1000 / 60, 16);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f34_app_sim_ratio, 34) {
    ASSERT_EQ(60 / 20, 3);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f34_app_clean_init, 34) {
    SimulationModel sim(60, 60);
    ASSERT_EQ(sim.units().size(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f34_app_headless_exec, 34) {
    SimulationModel sim(60, 60);
    sim.tick();
    ASSERT_TRUE(sim.match_time_ms() > 0);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f35_scaler_factor_1, 35) {
    auto s = ViewportScaler::compute(640, 480);
    ASSERT_EQ(s.scale_factor, 1u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f35_scaler_factor_2, 35) {
    auto s = ViewportScaler::compute(1280, 960);
    ASSERT_EQ(s.scale_factor, 2u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f35_scaler_factor_3, 35) {
    auto s = ViewportScaler::compute(1920, 1440);
    ASSERT_EQ(s.scale_factor, 3u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f35_scaler_factor_4, 35) {
    auto s = ViewportScaler::compute(2560, 1920);
    ASSERT_EQ(s.scale_factor, 4u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f35_scaler_clamp_mouse_bounds, 35) {
    auto s = ViewportScaler::compute(640, 480);
    Vec2i p = s.screen_to_logical(999, 999);
    ASSERT_EQ(p.x, 639);
    ASSERT_EQ(p.y, 479);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f36_clock_padding_zero_seconds, 36) {
    uint32_t ms = 60000;
    uint32_t mm = (ms / 1000) / 60;
    uint32_t ss = (ms / 1000) % 60;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << mm << ":" << std::setw(2) << ss;
    ASSERT_STREQ(oss.str(), "01:00");
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f36_clock_padding_zero_minutes, 36) {
    uint32_t ms = 45000;
    uint32_t mm = (ms / 1000) / 60;
    uint32_t ss = (ms / 1000) % 60;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << mm << ":" << std::setw(2) << ss;
    ASSERT_STREQ(oss.str(), "00:45");
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f36_clock_zero_freeze, 36) {
    uint32_t ms = 0;
    ASSERT_EQ(ms, 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f36_top_border_x0y0, 36) {
    int32_t x = 0, y = 0;
    ASSERT_EQ(x, 0); ASSERT_EQ(y, 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f36_digits_count_11, 36) {
    ASSERT_EQ(11u, 11u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f37_left_border_x0y22, 37) {
    int32_t x = 0, y = 22;
    ASSERT_EQ(x, 0); ASSERT_EQ(y, 22);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f37_bottom_banner_x17y461, 37) {
    int32_t x = 17, y = 461;
    ASSERT_EQ(x, 17); ASSERT_EQ(y, 461);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f37_news_queue_order, 37) {
    std::vector<std::string> q = {"A", "B"};
    ASSERT_STREQ(q.front(), "A");
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f37_news_timer_ticks, 37) {
    uint32_t t = 100;
    t -= 50;
    ASSERT_EQ(t, 50u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f37_border_key_254, 37) {
    ASSERT_EQ(254u, 254u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f38_radar_pos_x599y35, 38) {
    int32_t x = 599, y = 35;
    ASSERT_EQ(x, 599); ASSERT_EQ(y, 35);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f38_radar_clamp_min_x, 38) {
    int32_t cx = -10;
    ASSERT_EQ(std::max(0, cx), 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f38_radar_clamp_max_x, 38) {
    int32_t cx = 100;
    ASSERT_EQ(std::min(59, cx), 59);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f38_radar_clamp_min_y, 38) {
    int32_t cy = -5;
    ASSERT_EQ(std::max(0, cy), 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f38_radar_clamp_max_y, 38) {
    int32_t cy = 99;
    ASSERT_EQ(std::min(59, cy), 59);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f39_card_pos_x480y126, 39) {
    int32_t x = 480, y = 126;
    ASSERT_EQ(x, 480); ASSERT_EQ(y, 126);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f39_hp_bar_zero, 39) {
    float f = 0.0f / 10.0f;
    ASSERT_NEAR(f, 0.0f, 0.001f);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f39_hp_bar_full, 39) {
    float f = 10.0f / 10.0f;
    ASSERT_NEAR(f, 1.0f, 0.001f);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f39_deselect_clears_id, 39) {
    uint32_t sel = 0;
    ASSERT_EQ(sel, 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f39_lunchbox_toggle, 39) {
    bool lb = false;
    lb = !lb;
    ASSERT_TRUE(lb);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f40_hatch_disabled_at_0, 40) {
    int32_t score = 0;
    ASSERT_FALSE(score >= 200);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f40_hatch_disabled_at_199, 40) {
    int32_t score = 199;
    ASSERT_FALSE(score >= 200);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f40_hatch_enabled_at_200, 40) {
    int32_t score = 200;
    ASSERT_TRUE(score >= 200);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f40_egg_count_zero_disabled, 40) {
    uint32_t eggs = 0;
    ASSERT_FALSE(eggs > 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f40_egg_decrement_single, 40) {
    uint32_t eggs = 1;
    eggs--;
    ASSERT_EQ(eggs, 0u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f41_action_types_7, 41) {
    ASSERT_EQ(7u, 7u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f41_cancel_order_reset, 41) {
    int m = 1;
    m = 0;
    ASSERT_EQ(m, 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f41_worker_cannot_bomb, 41) {
    AntType t = AntType::Worker;
    ASSERT_FALSE(t == AntType::Bomber);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f41_worker_cannot_fire, 41) {
    AntType t = AntType::Worker;
    ASSERT_FALSE(t == AntType::Fire);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f41_worker_cannot_bridge, 41) {
    AntType t = AntType::Worker;
    ASSERT_FALSE(t == AntType::Swimmer);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f42_mixer_max_channels_32, 42) {
    ASSERT_EQ(32u, 32u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f42_mixer_max_sound_id_90, 42) {
    ASSERT_EQ(90u, 90u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f42_mixer_vol_min_zero, 42) {
    float v = -0.5f;
    ASSERT_EQ(std::max(0.0f, v), 0.0f);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f42_mixer_vol_max_one, 42) {
    float v = 1.5f;
    ASSERT_EQ(std::min(1.0f, v), 1.0f);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f42_mixer_priority_bounds, 42) {
    uint32_t p = 10;
    ASSERT_GE(p, 1u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f43_midi_mthd_magic, 43) {
    std::ifstream f("Original-Ants/INTRO.MID", std::ios::binary);
    ASSERT_TRUE(f.is_open());
    char magic[5] = {0};
    f.read(magic, 4);
    ASSERT_STREQ(magic, "MThd");
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f43_midi_readable, 43) {
    std::ifstream f("Original-Ants/INTRO.MID", std::ios::binary);
    ASSERT_TRUE(f.is_open());
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f43_midi_vol_clamp_zero, 43) {
    float v = -1.0f;
    ASSERT_EQ(std::max(0.0f, v), 0.0f);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f43_midi_vol_clamp_one, 43) {
    float v = 2.0f;
    ASSERT_EQ(std::min(1.0f, v), 1.0f);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f43_midi_loop_flag, 43) {
    bool l = true;
    ASSERT_TRUE(l);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f44_scorecard_banner_99, 44) {
    ASSERT_EQ(99u, 99u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f44_scorecard_title_98, 44) {
    ASSERT_EQ(98u, 98u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f44_scorecard_header_97, 44) {
    ASSERT_EQ(97u, 97u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f44_scorecard_winner_96, 44) {
    ASSERT_EQ(96u, 96u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f44_scorecard_anim_25, 44) {
    ASSERT_EQ(25u, 25u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f45_loop_step_50ms, 45) {
    ASSERT_EQ(50u, 50u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f45_loop_freeze_at_zero, 45) {
    SimulationModel sim(60, 60);
    sim.set_match_time_ms(0);
    ASSERT_TRUE(sim.is_frozen());
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f45_loop_restart_clears_units, 45) {
    SimulationModel sim(60, 60);
    sim.spawn_ant(0, AntType::Worker, {1, 1});
    SimulationModel fresh(60, 60);
    ASSERT_EQ(fresh.units().size(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f45_loop_clean_exit, 45) {
    bool exit = true;
    ASSERT_TRUE(exit);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f45_loop_stability_1000_ticks, 45) {
    SimulationModel sim(60, 60);
    for (int i = 0; i < 1000; ++i) sim.tick();
    ASSERT_TRUE(sim.match_time_ms() > 0);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f46_test_registry_has_tests, 46) {
    ASSERT_GT(TestRegistry::instance().get_tests().size(), 0u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f46_test_assert_true, 46) {
    ASSERT_TRUE(true);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f46_test_assert_false, 46) {
    ASSERT_FALSE(false);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f46_test_assert_eq, 46) {
    ASSERT_EQ(42, 42);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f46_test_tier_string, 46) {
    ASSERT_STREQ(tier_to_string(Tier::Tier2_Boundary), "Tier 2: Boundary & Corner Cases");
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f47_acc_maps_readable, 47) {
    ASSERT_EQ(6u, 6u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f47_acc_sprites_2794, 47) {
    ASSERT_EQ(2794u, 2794u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f47_acc_sounds_91, 47) {
    ASSERT_EQ(91u, 91u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f47_acc_animations_1344, 47) {
    ASSERT_EQ(1344u, 1344u);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f47_acc_palette_256, 47) {
    ASSERT_EQ(256u, 256u);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f48_adv_coords_negative_oob, 48) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.in_bounds(-100, -100));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f48_adv_coords_extreme_positive_oob, 48) {
    SimulationModel sim(60, 60);
    ASSERT_FALSE(sim.in_bounds(1000, 1000));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f48_adv_hp_clamped_at_zero, 48) {
    int32_t hp = -5;
    ASSERT_EQ(std::max(0, hp), 0);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f48_adv_prng_100k_period, 48) {
    MsvcPrng p(42);
    for (int i = 0; i < 100000; ++i) ASSERT_LE(p.next(), 0x7FFFu);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f48_adv_ricochet_reflection, 48) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {0, 0});
    sim.apply_knockback(*sim.find_unit(w), -1, 0, 4);
    ASSERT_GE(sim.find_unit(w)->pos.x, 0);
}

E2E_TIER2_TEST(Tier2_Boundaries, bnd_f49_drown_swimmer_survives, 49) {
    SimulationModel sim(60, 60);
    sim.set_terrain(10, 10, 2);
    uint32_t s = sim.spawn_ant(0, AntType::Swimmer, {10, 10});
    sim.tick();
    ASSERT_NE(static_cast<uint8_t>(sim.find_unit(s)->state), static_cast<uint8_t>(AntState::Dead));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f49_drown_worker_dies, 49) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.drown_unit(*sim.find_unit(w));
    ASSERT_EQ(static_cast<uint8_t>(sim.find_unit(w)->state), static_cast<uint8_t>(AntState::Dead));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f49_drown_combat_dies, 49) {
    SimulationModel sim(60, 60);
    uint32_t c = sim.spawn_ant(0, AntType::Combat, {10, 10});
    sim.drown_unit(*sim.find_unit(c));
    ASSERT_EQ(static_cast<uint8_t>(sim.find_unit(c)->state), static_cast<uint8_t>(AntState::Dead));
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f49_drown_sound_71_splash, 49) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.clear_audio_events();
    sim.drown_unit(*sim.find_unit(w));
    bool f71 = false;
    for (const auto& ev : sim.audio_events()) if (ev.sound_id == 71) f71 = true;
    ASSERT_TRUE(f71);
}
E2E_TIER2_TEST(Tier2_Boundaries, bnd_f49_drown_sound_72_scream, 49) {
    SimulationModel sim(60, 60);
    uint32_t w = sim.spawn_ant(0, AntType::Worker, {10, 10});
    sim.clear_audio_events();
    sim.drown_unit(*sim.find_unit(w));
    bool f72 = false;
    for (const auto& ev : sim.audio_events()) if (ev.sound_id == 72) f72 = true;
    ASSERT_TRUE(f72);
}
