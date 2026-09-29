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

#define TEST_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [SUITE] " << name << "\n" \
              << "=======================================================\n"

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(60) << name << " ... " << std::flush;
    int prev_fails = g_test_failures;
    fn();
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

using namespace ants::sim;

// Original 1998 movement (Ants.exe): orders queue an asynchronous path request (PATHMGR, one path per
// 50 ms tick per team) and walking starts after the path arrives. Ticks until pred() holds (checked
// before every tick); returns whether it held within max_ticks ticks.
static bool tick_until(SimulationEngine& sim, const std::function<bool()>& pred, int max_ticks) {
    for (int t = 0; t < max_ticks; ++t) {
        if (pred()) return true;
        sim.tick();
    }
    return pred();
}

// ============================================================================
// SUITE 1: Simulation Clock, Monotonic Tick & Integer Math Purity
// ============================================================================
void run_suite_1_clock() {
    TEST_SUITE("Suite 1: Simulation Clock, Monotonic Tick & Integer Math Purity");

    TEST_CASE("1.1 20 Hz Tick 50ms Discrete Decrement") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 12345, 12 * 60 * 1000);
        uint32_t t0 = sim.get_match_time_remaining_ms();
        sim.tick();
        uint32_t t1 = sim.get_match_time_remaining_ms();
        ASSERT_EQ(t0 - t1, 50u);
    } TEST_END();

    TEST_CASE("1.2 Monotonic Decrement Over 20 Ticks (1 Second)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 12345, 10000);
        uint32_t last = sim.get_match_time_remaining_ms();
        for (int i = 0; i < 20; ++i) {
            sim.tick();
            uint32_t cur = sim.get_match_time_remaining_ms();
            ASSERT_EQ(last - cur, 50u);
            last = cur;
        }
    } TEST_END();

    TEST_CASE("1.3 Match Freeze at 0:00") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 12345, 100);
        sim.tick();
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 50u);
        ASSERT_FALSE(sim.is_match_over());
        sim.tick();
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);
        ASSERT_TRUE(sim.is_match_over());
        sim.tick();
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);
    } TEST_END();

    TEST_CASE("1.4 Pure Integer Arithmetic (No FPU Drift)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 9999, 100000);
        for (int i = 0; i < 200; ++i) sim.tick();
        ASSERT_EQ(sim.get_match_time_remaining_ms() % 50, 0u);
    } TEST_END();
}

// ============================================================================
// SUITE 2: PRNG Determinism & MSVC LCG Recurrence
// ============================================================================
void run_suite_2_prng() {
    TEST_SUITE("Suite 2: PRNG Determinism & MSVC LCG Recurrence");

    TEST_CASE("2.1 MSVC LCG Recurrence Formula (Seed 1 -> 41)") {
        MsvcPrng prng(1);
        uint16_t r0 = prng.next();
        ASSERT_EQ(r0, 41u);
    } TEST_END();

    TEST_CASE("2.2 Identical Seed Produces 100% Identical Sequence") {
        MsvcPrng p1(12345);
        MsvcPrng p2(12345);
        for (int i = 0; i < 1000; ++i) {
            ASSERT_EQ(p1.next(), p2.next());
        }
    } TEST_END();

    TEST_CASE("2.3 Value Range Invariant (0..32767)") {
        MsvcPrng prng(777);
        for (int i = 0; i < 1000; ++i) {
            uint16_t v = prng.next();
            ASSERT_LE(v, 32767u);
        }
    } TEST_END();

    TEST_CASE("2.4 latseed Command Line String Parsing") {
        PRNG p1 = PRNG::from_latseed("latseed:54321");
        PRNG p2(54321);
        ASSERT_EQ(p1.get_state(), p2.get_state());
        ASSERT_EQ(p1.rand(), p2.rand());
    } TEST_END();
}

// ============================================================================
// SUITE 3: Unit Attributes & Damage Matrix
// ============================================================================
void run_suite_3_damage() {
    TEST_SUITE("Suite 3: Unit Attributes & Damage Matrix");

    TEST_CASE("3.1 Universal 10 HP Health Cap") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        for (uint8_t t = 0; t < 6; ++t) {
            uint32_t id = sim.spawn_unit(0, static_cast<AntType>(t), {10, 10});
            ASSERT_EQ(sim.get_unit(id).hp, 10);
            ASSERT_EQ(sim.get_unit(id).max_hp, 10);
        }
    } TEST_END();

    TEST_CASE("3.2 Universal 1 HP Melee Strike Standard (attack clip sound per ant type)") {
        // The sound belongs to the attack clip of the attacker (worker attack_alt 75, bomber / fire attack 57, thief
        // theifwhip 83, swimmer waterattack 79) and plays a few hundred ms into the clip, not at the contact.
        struct Row { AntType type; uint32_t sound; };
        const Row rows[5] = { {AntType::Worker, 75}, {AntType::Thief, 83}, {AntType::Fire, 57}, {AntType::Bomber, 57}, {AntType::Swimmer, 79} };
        for (const Row& r : rows) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1);
            uint32_t attacker = sim.spawn_unit(0, r.type, {10, 10});
            uint32_t defender = sim.spawn_unit(1, AntType::Worker, {11, 10});
            sim.execute_melee_attack(attacker, defender);
            ASSERT_EQ(sim.get_unit(defender).hp, 9);
            for (int t = 0; t < 20; ++t) sim.tick();
            ASSERT_TRUE(sim.has_audio_event(r.sound));
        }
    } TEST_END();

    TEST_CASE("3.3 Combat Ant Heavy Punch (2 HP Damage + Sound 78 on frame 2 of the attack clip)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.clear_audio_events();
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {10, 10});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, {11, 10});
        sim.execute_melee_attack(combat, victim);
        ASSERT_EQ(sim.get_unit(victim).hp, 8);
        ASSERT_FALSE(sim.has_audio_event(78));            // the clip has not reached its sound frame yet
        for (int t = 0; t < 10; ++t) sim.tick();
        ASSERT_TRUE(sim.has_audio_event(78)); // Sound 78 attack2.wav
    } TEST_END();

    TEST_CASE("3.4 Ballistic Knockback: 4 Tiles, No Stun After A Melee Flight") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {10, 10});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, {11, 10});
        sim.execute_melee_attack(combat, victim);
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(victim).pos.x == 15 &&
                                                   sim.get_unit(victim).loco_action == AntUnit::kActionIdle; }, 60));
        ASSERT_EQ(sim.get_unit(victim).pos.x, 15);
        ASSERT_FALSE(sim.get_unit(victim).is_stunned());
        ASSERT_FALSE(sim.get_unit(victim).is_stunned());
    } TEST_END();

    TEST_CASE("3.5 A Knockback Flies Over Obstacles: Only The Landing Tile Is Tested") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_terrain(13, 10, TERRAIN_OBSTACLE); // Obstacle in the flight path (not the landing tile)
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {10, 10});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, {11, 10});
        sim.execute_melee_attack(combat, victim);
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(victim).pos.x == 15 &&
                                                   sim.get_unit(victim).loco_action == AntUnit::kActionIdle; }, 60));
        ASSERT_EQ(sim.get_unit(victim).pos.x, 15);
    } TEST_END();
}

// ============================================================================
// SUITE 4: Combat Ant Autonomous Guard AI
// ============================================================================
void run_suite_4_guard_ai() {
    TEST_SUITE("Suite 4: Combat Ant Auto-Engage");

    TEST_CASE("4.1 A Combat Ant Without An Enemy Stays Where It Stands (no guard post, no auto-engage)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {15, 15});
        for (int i = 0; i < 100; ++i) sim.tick();
        ASSERT_EQ(sim.get_unit(combat).pos.x, 15);
        ASSERT_EQ(sim.get_unit(combat).pos.y, 15);
        ASSERT_FALSE(sim.get_unit(combat).auto_engage);
    } TEST_END();

    TEST_CASE("4.2 3-Tile Chebyshev Scan Triggers The Auto-Engage") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {20, 20});
        sim.spawn_unit(1, AntType::Worker, {23, 22}); // dist = max(3, 2) = 3
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(combat).auto_engage; }, 60));
        ASSERT_EQ(sim.get_unit(combat).ae_target, (TileCoord{20, 20}));   // the saved order: stay on this tile
    } TEST_END();

    TEST_CASE("4.3 Enemy Outside 3 Tiles Ignored") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {20, 20});
        sim.spawn_unit(1, AntType::Worker, {24, 20}); // dist = 4 -> OUT
        for (int i = 0; i < 100; ++i) sim.tick();
        ASSERT_FALSE(sim.get_unit(combat).auto_engage);
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);
    } TEST_END();

    TEST_CASE("4.4 Friendly & Allied Units Ignored") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.form_alliance(0, 1);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {20, 20});
        sim.spawn_unit(0, AntType::Worker, {21, 20}); // friendly
        sim.spawn_unit(1, AntType::Worker, {20, 21}); // ally
        for (int i = 0; i < 100; ++i) sim.tick();
        ASSERT_FALSE(sim.get_unit(combat).auto_engage);
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);
    } TEST_END();

    TEST_CASE("4.5 The Saved Order Is Given Again After The Punch (back on the same tile)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {20, 20});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {21, 20});
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(enemy).hp < 10; }, 60));      // the punch
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(combat).state == UnitState::GuardIdle &&
                                                   !sim.get_unit(combat).auto_engage; }, 80));
        ASSERT_EQ(sim.get_unit(combat).pos.x, 20);
        ASSERT_EQ(sim.get_unit(combat).pos.y, 20);
    } TEST_END();

    TEST_CASE("4.6 The Target Vanishes: The Combat Ant Ends Its Approach Where It Is") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {20, 20});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {22, 20});
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(combat).auto_engage; }, 60));
        sim.kill_unit(enemy); // Target eliminated
        for (int i = 0; i < 80; ++i) sim.tick();
        ASSERT_EQ(sim.get_unit(combat).loco_action, AntUnit::kActionIdle);
        ASSERT_EQ(sim.get_unit(combat).orig_order, AntUnit::kOrderNone);
    } TEST_END();
}

// ============================================================================
// SUITE 5: Cardinal-Only Placement Rules
// ============================================================================
void run_suite_5_placement() {
    TEST_SUITE("Suite 5: Cardinal-Only Placement Rules");

    TEST_CASE("5.1 Orthogonal Neighbors Accepted") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        ASSERT_TRUE(sim.validate_cardinal_placement({10, 10}, {10, 9}));
        ASSERT_TRUE(sim.validate_cardinal_placement({10, 10}, {11, 10}));
        ASSERT_TRUE(sim.validate_cardinal_placement({10, 10}, {10, 11}));
        ASSERT_TRUE(sim.validate_cardinal_placement({10, 10}, {9, 10}));
    } TEST_END();

    TEST_CASE("5.2 Diagonal Placement Strictly Rejected") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        ASSERT_FALSE(sim.validate_cardinal_placement({10, 10}, {11, 11}));
        ASSERT_FALSE(sim.validate_cardinal_placement({10, 10}, {9, 9}));
        ASSERT_FALSE(sim.validate_cardinal_placement({10, 10}, {11, 9}));
        ASSERT_FALSE(sim.validate_cardinal_placement({10, 10}, {9, 11}));
    } TEST_END();

    TEST_CASE("5.3 Ground Flag Bit 0x02 (CAN_PLACE_BOMB) Enforced") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t b = sim.spawn_unit(0, AntType::Bomber, {10, 10});
        sim.set_tile_flags(11, 10, 0x02);
        ASSERT_TRUE(sim.plant_bomb(b, {11, 10}));
        sim.set_tile_flags(10, 11, 0x00);
        ASSERT_FALSE(sim.plant_bomb(b, {10, 11}));
    } TEST_END();

    TEST_CASE("5.4 Ground Flag Bit 0x04 (CAN_PLACE_FIRE) Implies 0x02") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t f = sim.spawn_unit(0, AntType::Fire, {10, 10});
        sim.set_tile_flags(11, 10, 0x06);
        ASSERT_TRUE(sim.ignite_fire(f, {11, 10}));
    } TEST_END();

    TEST_CASE("5.5 Forbidden Terrain (Water, Rocks) Rejection") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t b = sim.spawn_unit(0, AntType::Bomber, {10, 10});
        sim.set_terrain(11, 10, TERRAIN_WATER);
        sim.set_tile_flags(11, 10, 0x00);
        ASSERT_FALSE(sim.plant_bomb(b, {11, 10}));
        sim.set_terrain(10, 11, TERRAIN_OBSTACLE);
        sim.set_tile_flags(10, 11, 0x00);
        ASSERT_FALSE(sim.plant_bomb(b, {10, 11}));
    } TEST_END();
}

// ============================================================================
// SUITE 6: Bomb Planting, Detonation & Defusal
// ============================================================================
void run_suite_6_bombs() {
    TEST_SUITE("Suite 6: Bomb Planting, Detonation & Defusal");

    TEST_CASE("6.1 Bomber Plants Mine (Sound 90 bombpick.wav)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t b = sim.spawn_unit(0, AntType::Bomber, {10, 10});
        sim.set_tile_flags(11, 10, 0x02);
        ASSERT_TRUE(sim.plant_bomb(b, {11, 10}));
        ASSERT_TRUE(sim.has_audio_event(90));
        ASSERT_TRUE(sim.has_bomb_at({11, 10}));
    } TEST_END();

    TEST_CASE("6.2 Proximity Detonation (2 HP + a 4-tile flight or a dud + Sound 4)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t b = sim.spawn_unit(0, AntType::Bomber, {10, 10});
        sim.set_tile_flags(11, 10, 0x02);
        sim.plant_bomb(b, {11, 10});
        sim.clear_audio_events();

        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {11, 10});
        // an ant standing on the bomb sets it off when its idle clip ends
        ASSERT_TRUE(tick_until(sim, [&]() { return !sim.has_bomb_at({11, 10}); }, 100));

        ASSERT_EQ(sim.get_unit(enemy).hp, 8);
        ASSERT_TRUE(sim.has_audio_event(4)); // Sound 4 bombexp.wav
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));
    } TEST_END();

    TEST_CASE("6.3 Bomber-Only Body Squash Defusal (Sounds 73+74, 0 Damage)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t enemy_bomber = sim.spawn_unit(1, AntType::Bomber, {10, 10});
        sim.set_tile_flags(11, 10, 0x02);
        sim.plant_bomb(enemy_bomber, {11, 10});

        uint32_t friendly_bomber = sim.spawn_unit(0, AntType::Bomber, {12, 10});
        sim.clear_audio_events();
        ASSERT_TRUE(sim.defuse_bomb(friendly_bomber, {11, 10}));
        ASSERT_TRUE(sim.has_audio_event(73)); // Sound 73 bombdrop.wav
        ASSERT_TRUE(sim.has_audio_event(74)); // Sound 74 bombmuffle.wav
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));
        ASSERT_EQ(sim.get_unit(friendly_bomber).hp, 10);
    } TEST_END();

    TEST_CASE("6.4 Non-Bomber Cannot Defuse Bomb") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t b = sim.spawn_unit(1, AntType::Bomber, {10, 10});
        sim.set_tile_flags(11, 10, 0x02);
        sim.plant_bomb(b, {11, 10});

        uint32_t worker = sim.spawn_unit(0, AntType::Worker, {12, 10});
        ASSERT_FALSE(sim.defuse_bomb(worker, {11, 10}));
    } TEST_END();

    TEST_CASE("6.5 Friendly Ant Walks Over Friendly Bomb Safely") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t b = sim.spawn_unit(0, AntType::Bomber, {10, 10});
        sim.set_tile_flags(11, 10, 0x02);
        sim.plant_bomb(b, {11, 10});

        uint32_t friendly = sim.spawn_unit(0, AntType::Worker, {11, 10});
        sim.tick();
        ASSERT_EQ(sim.get_unit(friendly).hp, 10); // Does NOT detonate
        ASSERT_TRUE(sim.has_bomb_at({11, 10}));
    } TEST_END();
}

// ============================================================================
// SUITE 7: Fire Physics, Ricochet Dynamics & Extinguishing
// ============================================================================
void run_suite_7_fire() {
    TEST_SUITE("Suite 7: Fire Physics, Ricochet Dynamics & Extinguishing");

    TEST_CASE("7.1 Fire Ignition (Sounds 67+68, 180s Timer)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t f = sim.spawn_unit(0, AntType::Fire, {10, 10});
        sim.set_tile_flags(11, 10, 0x06);
        ASSERT_TRUE(sim.ignite_fire(f, {11, 10}));
        ASSERT_TRUE(sim.has_audio_event(67)); // Sound 67 firestarta.wav
        ASSERT_TRUE(sim.has_audio_event(68)); // Sound 68 firestartb.wav
        ASSERT_TRUE(sim.has_fire_at({11, 10}));
        ASSERT_EQ(sim.get_fire_timer({11, 10}), 3600u);
    } TEST_END();

    TEST_CASE("7.2 Non-Fire Ants Cannot Path Onto Fire (A* Obstacle)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_fire_at({15, 15}, 3600);
        ASSERT_FALSE(sim.can_unit_traverse(AntType::Worker, {15, 15}));
        ASSERT_FALSE(sim.can_unit_traverse(AntType::Combat, {15, 15}));
    } TEST_END();

    TEST_CASE("7.3 Fire Ants Traverse Fire Freely (Immunity)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_fire_at({15, 15}, 3600);
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Fire, {15, 15}));
    } TEST_END();

    TEST_CASE("7.4 Involuntary Knockback into Fire (+1 Fire Damage And A New Flight)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_fire_at({15, 10}, 3600);           // the landing tile of a punch at (11, 10)
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {10, 10});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, {11, 10});
        sim.execute_melee_attack(combat, victim);  // the 2 HP punch throws the ant onto the fire
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(victim).hp == 7; }, 60));   // 10 - 2 (punch) - 1 (fire)
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(victim).loco_action == AntUnit::kActionIdle; }, 80));
        ASSERT_TRUE(sim.get_unit(victim).pos != (TileCoord{15, 10})); // thrown off the fire
        ASSERT_TRUE(sim.has_fire_at({15, 10}));                       // Fire NOT extinguished!
    } TEST_END();

    TEST_CASE("7.5 Fire Contact Costs 1 hp And Throws The Ant One Tile; Every Fire Wall Stays") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_fire_at({12, 10}, 3600);
        sim.set_fire_at({13, 10}, 3600);
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, {12, 10});
        sim.resolve_fire_contact(victim, 1, 0);
        ASSERT_EQ(sim.get_unit(victim).hp, 9);
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(victim).loco_action == AntUnit::kActionIdle &&
                                                   !sim.has_fire_at(sim.get_unit(victim).pos); }, 200));
        ASSERT_TRUE(sim.get_unit(victim).hp <= 9);
        ASSERT_TRUE(sim.has_fire_at({12, 10}));
        ASSERT_TRUE(sim.has_fire_at({13, 10}));
    } TEST_END();

    TEST_CASE("7.6 Fire Ant Extinguishes Flame (Sound 69)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_fire_at({11, 10}, 3600);
        uint32_t f = sim.spawn_unit(0, AntType::Fire, {10, 10});
        sim.clear_audio_events();
        ASSERT_TRUE(sim.extinguish_fire(f, {11, 10}));
        ASSERT_TRUE(sim.has_audio_event(69)); // Sound 69 fireextinguish.wav
        ASSERT_FALSE(sim.has_fire_at({11, 10}));
    } TEST_END();

    TEST_CASE("7.7 Non-Fire Ant Cannot Extinguish Fire") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_fire_at({11, 10}, 3600);
        uint32_t w = sim.spawn_unit(0, AntType::Worker, {10, 10});
        ASSERT_FALSE(sim.extinguish_fire(w, {11, 10}));
        ASSERT_TRUE(sim.has_fire_at({11, 10}));
    } TEST_END();
}

// ============================================================================
// SUITE 8: Bridge Mechanics, Universal Traversal & Collapse Drowning
// ============================================================================
void run_suite_8_bridges() {
    TEST_SUITE("Suite 8: Bridge Mechanics, Universal Traversal & Collapse Drowning");

    TEST_CASE("8.1 4-Stage Construction on Water (Sound 82)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_terrain(11, 10, TERRAIN_WATER);
        uint32_t s = sim.spawn_unit(0, AntType::Swimmer, {10, 10});
        for (int stage = 1; stage <= 4; ++stage) {
            sim.clear_audio_events();
            sim.build_bridge_step(s, {11, 10});
            ASSERT_TRUE(sim.has_audio_event(82)); // Sound 82 shovelwater.wav
            ASSERT_EQ(sim.get_bridge_stage({11, 10}), stage);
        }
    } TEST_END();

    TEST_CASE("8.2 Universal Traversal (Enemy Ant Walks on Friendly Bridge)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_terrain(11, 10, TERRAIN_WATER);
        sim.set_bridge_at({11, 10}, 4, 3600);
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Worker, {11, 10}));
    } TEST_END();

    TEST_CASE("8.3 180s Collapse & Non-Swimmer Drowning (drowning clip: Sounds 71+72, action 0xF, hp unchanged)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_terrain(11, 10, TERRAIN_WATER);
        sim.set_bridge_at({11, 10}, 4, 1); // 1 tick remaining
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {11, 10});
        sim.clear_audio_events();
        sim.tick();

        ASSERT_FALSE(sim.has_bridge_at({11, 10}));
        ASSERT_EQ(sim.get_unit(enemy).hp, 10);
        ASSERT_EQ(sim.get_unit(enemy).loco_action, AntUnit::kActionDrown);
        for (int i = 0; i < 4; ++i) sim.tick();
        ASSERT_TRUE(sim.has_audio_event(71)); // Sound 71 splash.wav on the first frame of the clip
        ASSERT_TRUE(sim.has_audio_event(72)); // Sound 72 antdrown.wav 100 ms later
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(enemy).removed; }, 80));
        ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(enemy).death_status), 0x0Fu);
    } TEST_END();

    TEST_CASE("8.4 Swimmer Ant Survives Bridge Collapse Unharmed") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_terrain(11, 10, TERRAIN_WATER);
        sim.set_bridge_at({11, 10}, 4, 1);
        uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, {11, 10});
        sim.clear_audio_events();
        sim.tick();

        ASSERT_EQ(sim.get_unit(swimmer).hp, 10);
        ASSERT_EQ(sim.get_unit(swimmer).state, UnitState::Swimming);
        ASSERT_TRUE(sim.has_audio_event(71)); // Sound 71 splash.wav
        ASSERT_FALSE(sim.has_audio_event(72)); // NO antdrown.wav
    } TEST_END();

    TEST_CASE("8.5 Swimmer Shoveling Requires Water Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_terrain(11, 10, TERRAIN_WALKABLE); // Dirt, not water
        uint32_t s = sim.spawn_unit(0, AntType::Swimmer, {10, 10});
        ASSERT_FALSE(sim.build_bridge_step(s, {11, 10}));
    } TEST_END();
}

// ============================================================================
// SUITE 9: Anthill Queuing, 17-Frame Entry, 100% Heal & Egg Hatching
// ============================================================================
void run_suite_9_anthill() {
    TEST_SUITE("Suite 9: Anthill Queuing, 17-Frame Entry, 100% Heal & Egg Hatching");





    TEST_CASE("9.5 Egg Hatching (200 Pts Deducted, Egg Counter Decremented)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 10);
        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
        ASSERT_EQ(sim.get_player_score(0), 300);
        ASSERT_EQ(sim.get_player_eggs(0), 9u);
        ASSERT_EQ(sim.get_player_hatched(0), 1u);
    } TEST_END();

    TEST_CASE("9.6 Hatching Rejected on Insufficient Score or Zero Eggs") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_player_score(0, 150);
        sim.set_player_eggs(0, 5);
        ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));

        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 0);
        ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));
    } TEST_END();
}

// ============================================================================
// SUITE 10: Thief Ant Infiltration, Alarm Siren & Lunchbox Drops
// ============================================================================
void run_suite_10_thief() {
    TEST_SUITE("Suite 10: Thief Ant Infiltration, Alarm Siren & Lunchbox Drops");

    TEST_CASE("10.1 Thief Infiltration Dives into Enemy Base") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_anthill(1, {40, 40});
        uint32_t t = sim.spawn_unit(0, AntType::Thief, {40, 40});
        sim.start_thief_infiltration(t, 1);
        ASSERT_EQ(sim.get_unit(t).state, UnitState::Infiltrating);
    } TEST_END();




    TEST_CASE("10.5 Carrier Death Drops Physical Lunchbox on Layer 2") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t carrier = sim.spawn_unit(0, AntType::Worker, {15, 15});
        sim.get_unit(carrier).carried_points = 35;
        sim.get_unit(carrier).holding = 1;
        sim.kill_unit(carrier);

        ASSERT_TRUE(sim.has_lunchbox_at({15, 15}));
        ASSERT_EQ(sim.get_lunchbox_points({15, 15}), 35u);
    } TEST_END();

    TEST_CASE("10.6 Dropped Lunchbox Universal Pickup") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.grid_mut().drop_lunchbox(15, 15, 40);

        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {15, 15});
        sim.tick();
        ASSERT_EQ(sim.get_unit(enemy).carried_points, 40u);
        ASSERT_FALSE(sim.has_lunchbox_at({15, 15}));
    } TEST_END();
}

// ============================================================================
// SUITE 11: Dynamic In-Game Alliances & Discrete Scoring
// ============================================================================
void run_suite_11_alliances() {
    TEST_SUITE("Suite 11: Dynamic In-Game Alliances & Discrete Scoring");

    TEST_CASE("11.1 Alliance Proposal Audio (Sound 51 allypro.wav)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.propose_alliance(0, 1);
        ASSERT_TRUE(sim.has_targeted_audio_event(1, 51));
    } TEST_END();

    TEST_CASE("11.2 Acceptance Flow (Sounds 53 + 50)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.propose_alliance(0, 1);
        sim.clear_audio_events();
        sim.accept_alliance(1, 0);
        ASSERT_TRUE(sim.has_audio_event(53)); // Sound 53 allyyes.wav
        ASSERT_TRUE(sim.has_audio_event(50)); // Sound 50 allyon.wav
        ASSERT_EQ(sim.get_ally_id(0), 1u);
        ASSERT_EQ(sim.get_ally_id(1), 0u);
    } TEST_END();

    TEST_CASE("11.3 Denial Flow (Sound 52 allynot.wav)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.propose_alliance(0, 1);
        sim.clear_audio_events();
        sim.deny_alliance(1, 0);
        ASSERT_TRUE(sim.has_targeted_audio_event(0, 52));
        ASSERT_EQ(sim.get_ally_id(0), 4u); // Remains FFA
    } TEST_END();

    TEST_CASE("11.4 Dissolution Flow (Sound 49 allyoff.wav)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.form_alliance(0, 1);
        sim.clear_audio_events();
        sim.break_alliance(0, 1);
        ASSERT_TRUE(sim.has_audio_event(49)); // Sound 49
        ASSERT_EQ(sim.get_ally_id(0), 4u);
        ASSERT_EQ(sim.get_ally_id(1), 4u);
    } TEST_END();

    TEST_CASE("11.5 Combined Scoreboard vs Discrete Memory Preservation") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_player_score(0, 150);
        sim.set_player_score(1, 250);
        sim.form_alliance(0, 1);

        ASSERT_EQ(sim.get_display_score(0), 400);
        ASSERT_EQ(sim.get_display_score(1), 400);

        ASSERT_EQ(sim.get_player_stats(0).score, 150);
        ASSERT_EQ(sim.get_player_stats(1).score, 250);

        sim.break_alliance(0, 1);
        ASSERT_EQ(sim.get_display_score(0), 150);
        ASSERT_EQ(sim.get_display_score(1), 250);
    } TEST_END();
}

// ============================================================================
// SUITE 12: Match End Freeze, Audio Split & 4-Stat Scorecard
// ============================================================================
void run_suite_12_game_over() {
    TEST_SUITE("Suite 12: Match End Freeze, Audio Split & 4-Stat Scorecard");

    TEST_CASE("12.1 Immediate Simulation Freeze at 0:00") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1, 50);
        uint32_t u = sim.spawn_unit(0, AntType::Worker, {10, 10});
        sim.issue_move_order(u, {20, 20});
        sim.tick();

        ASSERT_TRUE(sim.is_match_over());
        TileCoord frozen_pos = sim.get_unit(u).pos;
        sim.tick();
        ASSERT_EQ(sim.get_unit(u).pos.x, frozen_pos.x);
        ASSERT_EQ(sim.get_unit(u).pos.y, frozen_pos.y);
    } TEST_END();

    TEST_CASE("12.2 Audio Split: Winner (Sound 56) vs Loser (Sound 42)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1, 50);
        sim.set_player_score(0, 500); // Winner
        sim.set_player_score(1, 200); // Loser
        sim.tick();

        ASSERT_TRUE(sim.has_targeted_audio_event(0, 56));  // Winner Sound 56
        ASSERT_FALSE(sim.has_targeted_audio_event(0, 42)); // Winner never hears 42
        ASSERT_TRUE(sim.has_targeted_audio_event(1, 42));  // Loser Sound 42 (losers.wav)
        ASSERT_FALSE(sim.has_targeted_audio_event(1, 56)); // Loser never hears 56
    } TEST_END();

    TEST_CASE("12.3 4-Stat Scorecard Accuracy") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.record_player_stat(0, StatType::Score, 450);
        sim.record_player_stat(0, StatType::FriendlyLost, 3);
        sim.record_player_stat(0, StatType::EnemyKilled, 14);
        sim.record_player_stat(0, StatType::NewHatched, 5);

        const auto stats = sim.get_player_stats(0);
        ASSERT_EQ(stats.score, 450);
        ASSERT_EQ(stats.friendly_lost, 3u);
        ASSERT_EQ(stats.enemy_killed, 14u);
        ASSERT_EQ(stats.ants_hatched, 5u);
    } TEST_END();

    TEST_CASE("12.4 Scorecard Memory Preservation Across Matches") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.record_player_stat(1, StatType::Score, 300);
        sim.record_player_stat(1, StatType::FriendlyLost, 2);
        sim.record_player_stat(1, StatType::EnemyKilled, 8);
        sim.record_player_stat(1, StatType::NewHatched, 4);

        const auto stats = sim.get_player_stats(1);
        ASSERT_EQ(stats.score, 300);
        ASSERT_EQ(stats.friendly_lost, 2u);
        ASSERT_EQ(stats.enemy_killed, 8u);
        ASSERT_EQ(stats.ants_hatched, 4u);
    } TEST_END();
}

static void run_suite_13_authentic_fidelity() {
    std::cout << "\n=======================================================\n";
    std::cout << " [SUITE] Suite 13: Combat Range, Mud Cancel, Anthill Passability & PowerUp\n";
    std::cout << "=======================================================\n";

    TEST_CASE("13.1 Distant Attack Order Paths to Melee Range Instead of Teleport Strike") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t a1 = sim.spawn_unit(0, AntType::Combat, {10, 10});
        uint32_t a2 = sim.spawn_unit(1, AntType::Worker, {25, 25});

        AntOrder order{};
        order.ant_id = a1;
        order.type = OrderType::Attack;
        order.target_x = 25;
        order.target_y = 25;
        order.target_entity_id = static_cast<int32_t>(a2);
        sim.issue_order(order);

        // Distant defender should take NO damage immediately
        ASSERT_EQ(sim.get_unit(a2).hp, 10);
        // Attacker should receive a movement path
        ASSERT_EQ(sim.get_unit(a1).state, UnitState::Walking);
    } TEST_END();

    TEST_CASE("13.2 Combat AI Respects User Walking Orders and Does Not Auto-Hijack") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {10, 10});
        [[maybe_unused]] uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {12, 10}); // within 3 tiles aggro

        AntOrder move{};
        move.ant_id = combat;
        move.type = OrderType::Move;
        move.target_x = 10;
        move.target_y = 20;
        sim.issue_order(move);

        ASSERT_EQ(sim.get_unit(combat).state, UnitState::Walking);
        sim.tick();
        // Should remain walking towards user destination, not hijacked to Intercepting
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::Walking);
    } TEST_END();

    TEST_CASE("13.3 Re-Ordering Mid-Stride Snaps to the Tile Centre (Authentic Mud \"Humping\")") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        // Set path to mud surface
        for (uint32_t y = 9; y <= 15; ++y) {
            for (uint32_t x = 9; x <= 15; ++x) {
                sim.grid_mut().get_cell_mut(x, y).surface_type = SurfaceType::Mud;
                sim.grid_mut().get_cell_mut(x, y).is_mud = true;
            }
        }
        uint32_t ant = sim.spawn_unit(0, AntType::Worker, {10, 10});
        AntOrder move1{};
        move1.ant_id = ant;
        move1.type = OrderType::Move;
        move1.target_x = 10;
        move1.target_y = 15;
        sim.issue_order(move1);

        // Walk until the ant has just crossed into the next tile, still short of that tile's centre
        const AntUnit& u = sim.get_unit(ant);
        ASSERT_TRUE(tick_until(sim, [&]() {
            return u.loco_action == AntUnit::kActionWalk && u.pixel_y > 11 * 32 && (u.pixel_y % 32) < 16;
        }, 200));
        ASSERT_TRUE(u.is_on_mud);

        // Re-issuing the order snaps the ant to the centre of the tile under it (GoTo, FUN_0101fc50): on slow
        // mud this forward jump is what makes rapid re-ordering ("humping") faster than walking.
        int32_t prev_y = u.pixel_y;
        sim.issue_order(move1);
        ASSERT_GT(u.pixel_y, prev_y);
        ASSERT_EQ(u.pixel_y, 11 * 32 + 16);
        ASSERT_TRUE(u.waypoints.empty());               // idle until the new path arrives
        ASSERT_TRUE(sim.has_pending_path(ant));
    } TEST_END();

    TEST_CASE("13.4 Anthill Mound Impassability & Queue Staging") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        // Place a 4x4 anthill base at (20, 20)
        sim.set_anthill(0, {20, 20});
        // Set up 4x4 base: entrance at (21, 21), bottom-left queuing spot at (20, 23), mound elsewhere
        for (uint32_t y = 20; y < 24; ++y) {
            for (uint32_t x = 20; x < 24; ++x) {
                if ((x == 21 && (y == 20 || y == 21)) || (x == 20 && y == 23)) {
                    sim.grid_mut().get_cell_mut(x, y).is_obstacle_overlay = false;
                } else {
                    sim.grid_mut().get_cell_mut(x, y).is_obstacle_overlay = true;
                }
            }
        }

        // Mound tile is impassable
        ASSERT_FALSE(sim.grid().get_cell(20, 20).is_passable());
        ASSERT_FALSE(sim.grid().get_cell(22, 22).is_passable());
        // Entrance (when entering base) and queue staging spot (to the left of base: 19, 23) are passable
        ASSERT_TRUE(sim.grid().get_cell(21, 21).is_passable(false, false, false, 0, true));
        ASSERT_TRUE(sim.grid().get_cell(19, 23).is_passable());

        // First worker gets priority immediately and routes straight in to base entrance (21, 21)
        uint32_t worker = sim.spawn_unit(0, AntType::Worker, {10, 10});
        AntOrder ret{};
        ret.ant_id = worker;
        ret.type = OrderType::ReturnToBase;
        sim.issue_order(ret);

        ASSERT_EQ(sim.get_unit(worker).orig_order, AntUnit::kOrderHome);   // Order(home): path to the entrance
        ASSERT_TRUE(tick_until(sim, [&]() { return !sim.has_pending_path(worker); }, 20));
        ASSERT_FALSE(sim.get_unit(worker).waypoints.empty());
        TileCoord last_wp = sim.get_unit(worker).waypoints.back();
        ASSERT_EQ(last_wp.x, 21);
        ASSERT_EQ(last_wp.y, 21);

        // Second worker: the entrance is claimed by worker 1, so it waits on the ring tile in front of the hill (19, 23)
        uint32_t worker2 = sim.spawn_unit(0, AntType::Worker, {10, 15});
        AntOrder ret2{};
        ret2.ant_id = worker2;
        ret2.type = OrderType::ReturnToBase;
        sim.issue_order(ret2);

        ASSERT_TRUE(tick_until(sim, [&]() { return !sim.has_pending_path(worker2); }, 20));
        ASSERT_FALSE(sim.get_unit(worker2).waypoints.empty());
        TileCoord last_wp2 = sim.get_unit(worker2).waypoints.back();
        ASSERT_EQ(last_wp2.x, 19);
        ASSERT_EQ(last_wp2.y, 23);
    } TEST_END();


    TEST_CASE("13.6 Power-Up Pickup Emits Sounds 1 & 2 and Starts 15-Tick Transformation") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.grid_mut().place_powerup(10, 10, 4); // Combat power-up
        uint32_t ant = sim.spawn_unit(0, AntType::Worker, {10, 10});

        sim.clear_audio_events();
        sim.tick();

        ASSERT_EQ(sim.get_unit(ant).type, AntType::Combat);
        ASSERT_EQ(sim.get_unit(ant).transform_timer, 15);
        ASSERT_TRUE(sim.get_unit(ant).is_transforming());
        ASSERT_TRUE(sim.has_audio_event(SoundID::PowerUpHeal)); // Sound 1 (tick 0)
        ASSERT_FALSE(sim.has_audio_event(SoundID::PowerUpChime)); // Sound 2 fires at tick 8 (~420 ms)

        // Advance 8 ticks (transform_timer decrements each tick)
        for (int i = 0; i < 8; ++i) {
            sim.clear_audio_events();
            sim.tick();
        }
        ASSERT_TRUE(sim.has_audio_event(SoundID::PowerUpChime)); // Sound 2 triggered at tick 8

        const auto& ws = sim.get_world_state();
        ASSERT_TRUE(ws.ants[0].is_transforming);
    } TEST_END();

    TEST_CASE("13.7 Walking Off Power-Up During Dwell Window Without Consuming It") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.grid_mut().place_powerup(10, 10, 4); // Combat power-up at (10, 10)
        uint32_t ant = sim.spawn_unit(0, AntType::Worker, {9, 10});

        // Order ant to walk onto powerup at (10, 10)
        sim.issue_move_order(ant, {10, 10});
        auto arrived_at = [&](TileCoord t) {
            const AntUnit& u = sim.get_unit(ant);
            return u.pos == t && u.waypoints.empty() && !sim.has_pending_path(ant) && u.state == UnitState::Idle;
        };

        // Advance until ant reaches (10, 10): path at the first 50 ms run, then 250 ms + 8 steps of 4 px / 50 ms
        ASSERT_TRUE(tick_until(sim, [&]() { return arrived_at(TileCoord{10, 10}); }, 40));
        ASSERT_EQ(sim.get_unit(ant).pos, (TileCoord{10, 10}));
        ASSERT_EQ(sim.get_unit(ant).state, UnitState::Idle);
        // Ant has started dwell window (5 ticks remaining after arrival tick)
        ASSERT_GT(sim.get_unit(ant).powerup_dwell_timer, 0);
        ASSERT_FALSE(sim.get_unit(ant).is_transforming());
        ASSERT_TRUE(sim.grid().has_powerup_at({10, 10}));

        // Quick redirection! Order ant to walk to (11, 10) before dwell finishes
        sim.issue_move_order(ant, {11, 10});
        ASSERT_EQ(sim.get_unit(ant).powerup_dwell_timer, 0);

        // Advance until the ant has walked to (11, 10)
        ASSERT_TRUE(tick_until(sim, [&]() { return arrived_at(TileCoord{11, 10}); }, 40));
        ASSERT_EQ(sim.get_unit(ant).pos, (TileCoord{11, 10}));
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker); // Still Worker!
        ASSERT_FALSE(sim.get_unit(ant).is_transforming());
        // Power-up at (10, 10) remains on the ground unconsumed!
        ASSERT_TRUE(sim.grid().has_powerup_at({10, 10}));
    } TEST_END();

    TEST_CASE("13.8 Forced CantGo Standing on Power-Up Without Consuming It") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.grid_mut().place_powerup(10, 10, 4); // Combat power-up at (10, 10)
        uint32_t ant = sim.spawn_unit(0, AntType::Worker, {9, 10});

        // Order ant to walk onto powerup at (10, 10)
        sim.issue_move_order(ant, {10, 10});
        ASSERT_TRUE(tick_until(sim, [&]() {
            const AntUnit& u = sim.get_unit(ant);
            return u.pos == TileCoord{10, 10} && u.waypoints.empty() && !sim.has_pending_path(ant) &&
                   u.state == UnitState::Idle;
        }, 40));
        ASSERT_EQ(sim.get_unit(ant).pos, (TileCoord{10, 10}));

        // Force ant into CantGo while on powerup: order it to an unreachable island (land at (20, 20) inside a
        // ring of water), so the path manager reports "Can't go there." (clicking plain water would only send
        // the ant to the nearest shore tile of the goal ring scan).
        for (int32_t y = 19; y <= 21; ++y) {
            for (int32_t x = 19; x <= 21; ++x) {
                if (x != 20 || y != 20) sim.grid_mut().get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).terrain_type = TERRAIN_WATER;
            }
        }
        sim.issue_move_order(ant, {20, 20});
        ASSERT_TRUE(tick_until(sim, [&]() { return sim.get_unit(ant).state == UnitState::CantGo; }, 10));
        ASSERT_EQ(sim.get_unit(ant).powerup_dwell_timer, 0);

        // Advance through CantGo duration (12 ticks) and beyond
        for (int i = 0; i < 20; ++i) {
            sim.tick();
        }
        // Ant returned to Idle, standing directly on powerup without consuming it!
        ASSERT_EQ(sim.get_unit(ant).pos, (TileCoord{10, 10}));
        ASSERT_EQ(sim.get_unit(ant).state, UnitState::Idle);
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker);
        ASSERT_FALSE(sim.get_unit(ant).is_transforming());
        ASSERT_TRUE(sim.grid().has_powerup_at({10, 10}));

        // Now player specifically clicks on the powerup tile to consume it: GoTo gives the ant a one-tile path,
        // and when that path completes on the power-up the 6-tick pickup dwell starts
        sim.issue_move_order(ant, {10, 10});
        bool dwelled = false;
        ASSERT_TRUE(tick_until(sim, [&]() {
            if (sim.get_unit(ant).powerup_dwell_timer > 0) dwelled = true;
            return sim.get_unit(ant).is_transforming();
        }, 30));
        ASSERT_TRUE(dwelled);
        // Now it consumed and started transformation!
        ASSERT_TRUE(sim.get_unit(ant).is_transforming());
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Combat);
        ASSERT_FALSE(sim.grid().has_powerup_at({10, 10}));
    } TEST_END();
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << " ANTS - LIBANTS-SIM HEADLESS RULES TEST SUITE            \n";
    std::cout << "=======================================================\n";

    run_suite_1_clock();
    run_suite_2_prng();
    run_suite_3_damage();
    run_suite_4_guard_ai();
    run_suite_5_placement();
    run_suite_6_bombs();
    run_suite_7_fire();
    run_suite_8_bridges();
    run_suite_9_anthill();
    run_suite_10_thief();
    run_suite_11_alliances();
    run_suite_12_game_over();
    run_suite_13_authentic_fidelity();

    std::cout << "\n=======================================================\n";
    std::cout << " TEST SUMMARY\n";
    std::cout << "=======================================================\n";
    std::cout << "  Total Test Cases: " << g_test_count << "\n";
    std::cout << "  Total Assertions: " << g_assert_count << "\n";
    std::cout << "  Failed Tests:     " << g_test_failures << "\n";
    std::cout << "=======================================================\n";

    if (g_test_failures == 0) {
        std::cout << " >>> ALL 13 TEST SUITES PASSED CLEANLY (100% PASS) <<<\n\n";
        return 0;
    } else {
        std::cout << " >>> " << g_test_failures << " TEST(S) FAILED <<<\n\n";
        return 1;
    }
}
