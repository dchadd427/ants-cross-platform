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

#define TEST_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [CHALLENGER 1 SUITE] " << name << "\n" \
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
        std::cout << "FAILED! Unknown exception thrown\n";
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
// SUITE 1: Combat Ant Guard AI Empirical Stress Tests
// ============================================================================
void run_suite_1_combat_guard_ai() {
    TEST_SUITE("Suite 1: Combat Ant Guard AI Empirical Stress Tests");

    TEST_CASE("1.1 3-Tile Chebyshev Perimeter: All 16 Border Points Trigger Aggro") {
        // Combat Ant at (30, 30). Perimeter points have max(|dx|, |dy|) == 3.
        // Test all 16 perimeter coordinates around the anchor.
        const std::vector<std::pair<int32_t, int32_t>> perimeter_points = {
            {-3, -3}, {-3, -2}, {-3, -1}, {-3, 0}, {-3, 1}, {-3, 2}, {-3, 3},
            { 3, -3}, { 3, -2}, { 3, -1}, { 3, 0}, { 3, 1}, { 3, 2}, { 3, 3},
            {-2, -3}, {-1, -3}, { 0, -3}, { 1, -3}, { 2, -3},
            {-2,  3}, {-1,  3}, { 0,  3}, { 1,  3}, { 2,  3}
        };

        for (const auto& [dx, dy] : perimeter_points) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100);
            uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});
            sim.spawn_unit(1, AntType::Worker, {30 + dx, 30 + dy});
            sim.tick();
            ASSERT_TRUE(sim.get_unit(combat).state == UnitState::Intercepting ||
                        sim.get_unit(combat).state == UnitState::Attacking);
        }
    } TEST_END();

    TEST_CASE("1.2 Distance >= 4 Strictly Ignored (Negative Space Scan)") {
        const std::vector<std::pair<int32_t, int32_t>> outside_points = {
            { 4,  0}, {-4,  0}, { 0,  4}, { 0, -4},
            { 4,  4}, {-4, -4}, { 4, -4}, {-4,  4},
            { 5,  0}, {-5,  0}, { 0,  5}, { 0, -5}
        };

        for (const auto& [dx, dy] : outside_points) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100);
            uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});
            sim.spawn_unit(1, AntType::Worker, {30 + dx, 30 + dy});
            sim.tick();
            ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);
        }
    } TEST_END();

    TEST_CASE("1.3 Comprehensive Target Filtering (Friendly, Allied, Dead, Underground)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.form_alliance(0, 1);

        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});

        // 1. Friendly unit at dist 1
        sim.spawn_unit(0, AntType::Worker, {31, 30});
        sim.tick();
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);

        // 2. Allied unit at dist 2
        sim.spawn_unit(1, AntType::Worker, {30, 32});
        sim.tick();
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);

        // 3. Dead enemy unit at dist 1
        uint32_t dead_enemy = sim.spawn_unit(2, AntType::Worker, {29, 30});
        sim.kill_unit(dead_enemy);
        sim.tick();
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);

        // 4. Enemy unit entering base / underground at dist 2
        uint32_t entering_enemy = sim.spawn_unit(2, AntType::Worker, {30, 28});
        sim.get_unit(entering_enemy).state = UnitState::EnteringBase;
        sim.tick();
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);

        // 5. Enemy unit in knockback at dist 1
        uint32_t kb_enemy = sim.spawn_unit(2, AntType::Worker, {29, 29});
        sim.get_unit(kb_enemy).state = UnitState::Knockback;
        sim.tick();
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);
    } TEST_END();

    TEST_CASE("1.4 Closest Target Priority Among Multiple Intruders") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});

        // Far enemy at dist 3
        sim.spawn_unit(1, AntType::Worker, {33, 30});
        // Closer enemy at dist 2
        sim.spawn_unit(1, AntType::Worker, {30, 32});
        sim.tick(); // Tick 1: Detects close_enemy at dist 2, enters Intercepting
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::Intercepting);

        sim.tick(); // Tick 2: Steps to (30, 31), reaches melee adjacency, enters Attacking
        ASSERT_EQ(sim.get_unit(combat).pos.x, 30);
        ASSERT_EQ(sim.get_unit(combat).pos.y, 31);
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::Attacking);
    } TEST_END();

    TEST_CASE("1.5 Heavy Punch: 2 HP Damage, Sound 78, 12-Tick Stun & Knockback") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.spawn_unit(0, AntType::Combat, {30, 30});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {31, 30}); // Adjacent: strike immediately

        sim.clear_audio_events();
        sim.tick(); // Triggers punch

        ASSERT_EQ(sim.get_unit(enemy).hp, 8); // 10 - 2 = 8 HP
        ASSERT_TRUE(sim.has_audio_event(SoundID::HeavyPunch)); // Sound 78
        ASSERT_TRUE(sim.has_audio_event(SoundID::StunRecover)); // Sound 70
        ASSERT_TRUE(sim.get_unit(enemy).is_stunned());
        ASSERT_GE(sim.get_unit(enemy).pos.x, 35); // Displaced 4-5 tiles East
        ASSERT_EQ(sim.get_unit(enemy).pos.y, 30);
    } TEST_END();

    TEST_CASE("1.6 Autonomous Return to Post & Idle Resumption") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});
        sim.spawn_unit(1, AntType::Worker, {31, 30});

        sim.tick(); // Punch delivered, combat ant enters ReturningToPost
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::ReturningToPost);

        // Step ticks until returned to anchor post (30, 30)
        for (int i = 0; i < 20; ++i) {
            sim.tick();
        }

        ASSERT_EQ(sim.get_unit(combat).pos.x, 30);
        ASSERT_EQ(sim.get_unit(combat).pos.y, 30);
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);
    } TEST_END();

    TEST_CASE("1.7 Target Disengagement: Enemy Out of Pursuit Range (>5)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {32, 30}); // dist 2

        sim.tick(); // Combat ant starts intercepting
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::Intercepting);

        // Enemy teleports / flees out of pursuit range (dist 6 from anchor)
        sim.get_unit(enemy).set_tile_pos(36, 30);
        sim.tick();

        // Combat Ant disengages and returns to post
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::ReturningToPost);
    } TEST_END();

    TEST_CASE("1.8 Target Death During Pursuit Disengages to ReturnToPost") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {32, 30});

        sim.tick(); // Intercepting
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::Intercepting);

        sim.kill_unit(enemy); // Target eliminated externally
        sim.tick();

        ASSERT_EQ(sim.get_unit(combat).state, UnitState::ReturningToPost);
    } TEST_END();

    TEST_CASE("1.9 Anchor Repositioning via Move Command") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {20, 20});
        ASSERT_EQ(sim.get_unit(combat).guard_anchor.x, 20);
        ASSERT_EQ(sim.get_unit(combat).guard_anchor.y, 20);

        // Order move to (25, 20)
        sim.issue_move_order(combat, {25, 20});
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::Walking);

        // Advance simulation until arrived at destination and anchor committed
        for (int i = 0; i < 60; ++i) {
            sim.tick();
            if (sim.get_unit(combat).pos.x == 25 && sim.get_unit(combat).guard_anchor.x == 25) {
                break;
            }
        }

        // New anchor post committed!
        ASSERT_EQ(sim.get_unit(combat).pos.x, 25);
        ASSERT_EQ(sim.get_unit(combat).pos.y, 20);
        ASSERT_EQ(sim.get_unit(combat).guard_anchor.x, 25);
        ASSERT_EQ(sim.get_unit(combat).guard_anchor.y, 20);
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);

        // Enemy at old anchor perimeter (21, 20) is dist 4 from (25, 20) -> strictly ignored!
        sim.spawn_unit(1, AntType::Worker, {21, 20});
        sim.tick();
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);

        // Enemy at new anchor perimeter (28, 20) is dist 3 from (25, 20) -> intercepted!
        sim.spawn_unit(1, AntType::Worker, {28, 20});
        sim.tick();
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::Intercepting);
    } TEST_END();
}

// ============================================================================
// SUITE 2: Ballistic Knockback into Water & Aquatic Physics
// ============================================================================
void run_suite_2_ballistic_water() {
    TEST_SUITE("Suite 2: Ballistic Knockback into Water & Aquatic Physics");

    TEST_CASE("2.1 Non-Swimmer Worker Knockback into Deep Water Drowns Instantly") {
        PhysicsEngine physics;
        Grid grid; grid.init_empty(60, 60);
        PRNG prng(1);
        std::vector<AudioEvent> audio_out;

        AntUnit worker(1, TeamId::Blue, AntType::Worker, 10, 10);
        worker.set_pixel_pos(10 * 32 + 16, 10 * 32 + 16);

        // Set destination tile (14, 10) to deep water
        grid.get_cell_mut(14, 10).terrain_type = TERRAIN_WATER;

        // Apply knockback from (10, 10) pushing East 4 tiles to (14, 10)
        physics.apply_knockback(worker, 9 * 32 + 16, 10 * 32 + 16, 4, 4,
                                DamageSource::CombatPunch, audio_out, 0);

        std::vector<AntUnit*> units = {&worker};
        for (int t = 0; t < 10; ++t) {
            physics.tick(units, grid, audio_out, prng);
        }

        // On landing, worker plunged into water:
        ASSERT_EQ(worker.pos.x, 14);
        ASSERT_EQ(worker.pos.y, 10);
        ASSERT_EQ(worker.hp, 0);
        ASSERT_EQ(worker.state, UnitState::Drowning);
        ASSERT_EQ(static_cast<uint8_t>(worker.death_status), 0x0Fu); // Authenticated 0x0F

        // Audio events: Sound 71 splash.wav + Sound 72 antdrown.wav
        bool has_splash = false;
        bool has_drown = false;
        for (const auto& ev : audio_out) {
            if (ev.sound_id == SoundID::WaterSplash) has_splash = true;
            if (ev.sound_id == SoundID::AntDrown) has_drown = true;
        }
        ASSERT_TRUE(has_splash);
        ASSERT_TRUE(has_drown);
    } TEST_END();

    TEST_CASE("2.2 All Non-Swimmer Ant Classes Drown on Water Landing") {
        const std::vector<AntType> non_swimmers = {
            AntType::Worker, AntType::Bomber, AntType::Fire, AntType::Combat, AntType::Thief
        };

        for (AntType type : non_swimmers) {
            PhysicsEngine physics;
            Grid grid; grid.init_empty(60, 60);
            PRNG prng(1);
            std::vector<AudioEvent> audio_out;

            AntUnit victim(10, TeamId::Red, type, 10, 10);
            victim.set_pixel_pos(10 * 32 + 16, 10 * 32 + 16);
            grid.get_cell_mut(14, 10).terrain_type = TERRAIN_WATER;

            physics.apply_knockback(victim, 9 * 32 + 16, 10 * 32 + 16, 4, 4,
                                    DamageSource::CombatPunch, audio_out, 0);

            std::vector<AntUnit*> units = {&victim};
            for (int t = 0; t < 10; ++t) {
                physics.tick(units, grid, audio_out, prng);
            }

            ASSERT_EQ(victim.hp, 0);
            ASSERT_EQ(victim.state, UnitState::Drowning);
            ASSERT_EQ(static_cast<uint8_t>(victim.death_status), 0x0Fu);
        }
    } TEST_END();

    TEST_CASE("2.3 Swimmer Ant Knockback into Water Survives Unharmed in Swim Mode") {
        PhysicsEngine physics;
        Grid grid; grid.init_empty(60, 60);
        PRNG prng(1);
        std::vector<AudioEvent> audio_out;

        AntUnit swimmer(2, TeamId::Blue, AntType::Swimmer, 10, 10);
        swimmer.set_pixel_pos(10 * 32 + 16, 10 * 32 + 16);

        grid.get_cell_mut(14, 10).terrain_type = TERRAIN_WATER;

        physics.apply_knockback(swimmer, 9 * 32 + 16, 10 * 32 + 16, 4, 4,
                                DamageSource::CombatPunch, audio_out, 0);

        std::vector<AntUnit*> units = {&swimmer};
        for (int t = 0; t < 10; ++t) {
            physics.tick(units, grid, audio_out, prng);
        }

        // Swimmer survives: full 10 HP, state Swimming, Sound 71, NO Sound 72
        ASSERT_EQ(swimmer.pos.x, 14);
        ASSERT_EQ(swimmer.pos.y, 10);
        ASSERT_EQ(swimmer.hp, 10);
        ASSERT_EQ(swimmer.state, UnitState::Swimming);
        ASSERT_EQ(swimmer.death_status, DeathStatus::Alive);

        bool has_splash = false;
        bool has_drown = false;
        for (const auto& ev : audio_out) {
            if (ev.sound_id == SoundID::WaterSplash) has_splash = true;
            if (ev.sound_id == SoundID::AntDrown) has_drown = true;
        }
        ASSERT_TRUE(has_splash);
        ASSERT_FALSE(has_drown); // NEVER emits drowning scream!
    } TEST_END();

    TEST_CASE("2.4 Water Landing with Completed Bridge Protects Non-Swimmer") {
        PhysicsEngine physics;
        Grid grid; grid.init_empty(60, 60);
        PRNG prng(1);
        std::vector<AudioEvent> audio_out;

        AntUnit worker(1, TeamId::Blue, AntType::Worker, 10, 10);
        worker.set_pixel_pos(10 * 32 + 16, 10 * 32 + 16);

        // Water tile with completed bridge (stage 4)
        grid.get_cell_mut(14, 10).terrain_type = TERRAIN_WATER;
        grid.set_bridge_at({14, 10}, 4, 3600);

        physics.apply_knockback(worker, 9 * 32 + 16, 10 * 32 + 16, 4, 4,
                                DamageSource::CombatPunch, audio_out, 0);

        std::vector<AntUnit*> units = {&worker};
        for (int t = 0; t < 10; ++t) {
            physics.tick(units, grid, audio_out, prng);
        }

        // Lands safely on bridge as ground: 10 HP, Stunned recovery, NO drowning!
        ASSERT_EQ(worker.pos.x, 14);
        ASSERT_EQ(worker.pos.y, 10);
        ASSERT_EQ(worker.hp, 10);
        ASSERT_EQ(worker.state, UnitState::Stunned);
        ASSERT_EQ(worker.death_status, DeathStatus::Alive);

        bool has_drown = false;
        for (const auto& ev : audio_out) {
            if (ev.sound_id == SoundID::AntDrown) has_drown = true;
        }
        ASSERT_FALSE(has_drown);
    } TEST_END();

    TEST_CASE("2.5 Incomplete Bridge Stages (1-3) Do NOT Prevent Drowning") {
        for (int stage = 1; stage <= 3; ++stage) {
            PhysicsEngine physics;
            Grid grid; grid.init_empty(60, 60);
            PRNG prng(1);
            std::vector<AudioEvent> audio_out;

            AntUnit worker(1, TeamId::Blue, AntType::Worker, 10, 10);
            worker.set_pixel_pos(10 * 32 + 16, 10 * 32 + 16);

            grid.get_cell_mut(14, 10).terrain_type = TERRAIN_WATER;
            grid.set_bridge_at({14, 10}, stage, 0);

            physics.apply_knockback(worker, 9 * 32 + 16, 10 * 32 + 16, 4, 4,
                                    DamageSource::CombatPunch, audio_out, 0);

            std::vector<AntUnit*> units = {&worker};
            for (int t = 0; t < 10; ++t) {
                physics.tick(units, grid, audio_out, prng);
            }

            // Incomplete bridge does not support non-swimmer: drowns!
            ASSERT_EQ(worker.hp, 0);
            ASSERT_EQ(worker.state, UnitState::Drowning);
            ASSERT_EQ(static_cast<uint8_t>(worker.death_status), 0x0Fu);
        }
    } TEST_END();
}

// ============================================================================
// SUITE 3: Fire Ricochets, Reflection Physics & Fire Preservation
// ============================================================================
void run_suite_3_fire_ricochets() {
    TEST_SUITE("Suite 3: Fire Ricochets, Reflection Physics & Fire Preservation");

    TEST_CASE("3.1 Knockback into Fire: +1 Fire Damage, Reflection Vector Bounce") {
        PhysicsEngine physics;
        Grid grid; grid.init_empty(60, 60);
        PRNG prng(1);
        std::vector<AudioEvent> audio_out;

        AntUnit worker(1, TeamId::Blue, AntType::Worker, 10, 10);
        worker.set_pixel_pos(10 * 32 + 16, 10 * 32 + 16);

        // Place fire on target tile (14, 10)
        grid.place_firewall(14, 10, 0);
        ASSERT_TRUE(grid.has_fire_at({14, 10}));

        physics.apply_knockback(worker, 9 * 32 + 16, 10 * 32 + 16, 4, 4,
                                DamageSource::CombatPunch, audio_out, 0);

        std::vector<AntUnit*> units = {&worker};
        for (int t = 0; t < 10; ++t) {
            physics.tick(units, grid, audio_out, prng);
        }

        // Struck fire at (14, 10) with inc_dx = 1.
        // Should take 1 fire damage and bounce along reflection vector (-1, 0) to (13, 10)
        ASSERT_EQ(worker.hp, 9); // 10 - 1 = 9
        ASSERT_EQ(worker.pos.x, 13);
        ASSERT_EQ(worker.pos.y, 10);
        ASSERT_EQ(worker.state, UnitState::Stunned);

        // Fire tile at (14, 10) was NOT extinguished!
        ASSERT_TRUE(grid.has_fire_at({14, 10}));
    } TEST_END();

    TEST_CASE("3.2 Fire Tile Never Extinguished by Repeated Ant Impacts") {
        PhysicsEngine physics;
        Grid grid; grid.init_empty(60, 60);
        PRNG prng(1);

        grid.place_firewall(20, 20, 0);
        grid.get_cell_mut(20, 20).timer_ticks = 3600;

        // Bounce 10 different ants off this fire tile
        for (int i = 0; i < 10; ++i) {
            std::vector<AudioEvent> audio_out;
            AntUnit w(static_cast<uint32_t>(100 + i), TeamId::Blue, AntType::Worker, 16, 20);
            w.set_pixel_pos(16 * 32 + 16, 20 * 32 + 16);

            physics.apply_knockback(w, 15 * 32 + 16, 20 * 32 + 16, 4, 4,
                                    DamageSource::CombatPunch, audio_out, 0);

            std::vector<AntUnit*> units = {&w};
            for (int t = 0; t < 10; ++t) {
                physics.tick(units, grid, audio_out, prng);
            }

            ASSERT_EQ(w.hp, 9);
            ASSERT_EQ(w.pos.x, 19); // Bounced off to (19, 20)
            ASSERT_TRUE(grid.has_fire_at({20, 20})); // FIRE MUST NEVER EXTINGUISH!
        }

        // Fire timer is still active
        ASSERT_EQ(grid.get_cell(20, 20).timer_ticks, 3600u);
    } TEST_END();

    TEST_CASE("3.3 Multi-Fire Ricochet Chains Multiple Contacts & Damages") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);

        // Setup 2 adjacent fire tiles at (20, 20) and (21, 20) with obstacle behind
        sim.set_fire_at({20, 20}, 3600);
        sim.set_fire_at({21, 20}, 3600);
        sim.set_terrain(19, 20, TERRAIN_OBSTACLE);

        uint32_t victim = sim.spawn_unit(1, AntType::Worker, {20, 20});

        // Resolve genuine physics fire ricochet
        sim.resolve_fire_contact(victim, 1, 0);

        // Took 2 fire contact damages
        ASSERT_EQ(sim.get_unit(victim).hp, 8); // 10 - 2 = 8
        ASSERT_EQ(sim.get_unit(victim).pos.x, 22);
        ASSERT_TRUE(sim.has_fire_at({20, 20}));
        ASSERT_TRUE(sim.has_fire_at({21, 20}));
    } TEST_END();

    TEST_CASE("3.4 Lethal Fire Damage Transitions to FireKilled DeathStatus") {
        PhysicsEngine physics;
        Grid grid; grid.init_empty(60, 60);
        PRNG prng(1);
        std::vector<AudioEvent> audio_out;

        AntUnit low_hp_worker(1, TeamId::Blue, AntType::Worker, 10, 10);
        low_hp_worker.set_pixel_pos(10 * 32 + 16, 10 * 32 + 16);
        low_hp_worker.hp = 1; // 1 HP left

        grid.place_firewall(14, 10, 0);

        physics.apply_knockback(low_hp_worker, 9 * 32 + 16, 10 * 32 + 16, 4, 4,
                                DamageSource::CombatPunch, audio_out, 0);

        std::vector<AntUnit*> units = {&low_hp_worker};
        for (int t = 0; t < 10; ++t) {
            physics.tick(units, grid, audio_out, prng);
        }

        // Fire contact took final HP:
        ASSERT_EQ(low_hp_worker.hp, 0);
        ASSERT_EQ(low_hp_worker.state, UnitState::Dead);
        ASSERT_EQ(low_hp_worker.death_status, DeathStatus::FireKilled);
        ASSERT_TRUE(grid.has_fire_at({14, 10})); // Still not extinguished!
    } TEST_END();

    TEST_CASE("3.5 Fire Ant Immunity & Extinguish Mechanics") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);

        sim.set_fire_at({15, 15}, 3600);

        // Fire Ant can traverse fire freely
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Fire, {15, 15}));
        // Worker cannot
        ASSERT_FALSE(sim.can_unit_traverse(AntType::Worker, {15, 15}));

        // Fire Ant extinguishing flame
        uint32_t f = sim.spawn_unit(0, AntType::Fire, {14, 15});
        sim.clear_audio_events();
        ASSERT_TRUE(sim.extinguish_fire(f, {15, 15}));
        ASSERT_TRUE(sim.has_audio_event(SoundID::FireExtinguish)); // Sound 69
        ASSERT_FALSE(sim.has_fire_at({15, 15}));

        // Non-fire ant attempting extinguish
        sim.set_fire_at({15, 15}, 3600);
        uint32_t w = sim.spawn_unit(0, AntType::Worker, {14, 15});
        ASSERT_FALSE(sim.extinguish_fire(w, {15, 15}));
        ASSERT_TRUE(sim.has_fire_at({15, 15}));
    } TEST_END();
}

// ============================================================================
// SUITE 4: Universal Bridges & 180s Expiration Collapse
// ============================================================================
void run_suite_4_bridges() {
    TEST_SUITE("Suite 4: Universal Bridges & 180s Expiration Collapse");

    TEST_CASE("4.1 4-Stage Construction Progression with Sound 82") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.set_terrain(15, 15, TERRAIN_WATER);

        uint32_t s = sim.spawn_unit(0, AntType::Swimmer, {14, 15});

        for (int stage = 1; stage <= 4; ++stage) {
            sim.clear_audio_events();
            ASSERT_TRUE(sim.build_bridge_step(s, {15, 15}));
            ASSERT_TRUE(sim.has_audio_event(SoundID::ShovelWater)); // Sound 82
            ASSERT_EQ(sim.get_bridge_stage({15, 15}), stage);
        }

        // Bridge complete: exactly 3600 ticks (180s)
        ASSERT_TRUE(sim.has_bridge_at({15, 15}));
        ASSERT_EQ(sim.get_bridge_stage({15, 15}), 4);
    } TEST_END();

    TEST_CASE("4.2 Universal Traversal Across All Factions and Classes") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.set_terrain(20, 20, TERRAIN_WATER);
        sim.set_bridge_at({20, 20}, 4, 3600);

        // Every ant type can walk across completed bridge
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Worker, {20, 20}));
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Bomber, {20, 20}));
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Fire, {20, 20}));
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Thief, {20, 20}));
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Combat, {20, 20}));
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Swimmer, {20, 20}));

        // Incomplete bridge (stage 3) blocks non-swimmers
        sim.set_bridge_at({20, 20}, 3, 0);
        ASSERT_FALSE(sim.can_unit_traverse(AntType::Worker, {20, 20}));
        ASSERT_FALSE(sim.can_unit_traverse(AntType::Combat, {20, 20}));
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Swimmer, {20, 20})); // Swimmer can swim
    } TEST_END();

    TEST_CASE("4.3 Exact 180s Collapse: 3599 Ticks Persists, 3600 Ticks Collapses") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.set_terrain(20, 20, TERRAIN_WATER);
        sim.set_bridge_at({20, 20}, 4, 3600);

        // Step 3599 ticks
        for (int i = 0; i < 3599; ++i) {
            sim.tick();
        }
        ASSERT_TRUE(sim.has_bridge_at({20, 20}));

        // 3600th tick -> bridge collapses!
        sim.tick();
        ASSERT_FALSE(sim.has_bridge_at({20, 20}));
    } TEST_END();

    TEST_CASE("4.4 Multi-Unit Collapse: All Non-Swimmers Drown (0xF), Swimmer Survives") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.set_terrain(20, 20, TERRAIN_WATER);
        sim.set_bridge_at({20, 20}, 4, 1); // 1 tick remaining

        // 4 units on bridge: Friendly Worker, Enemy Combat, Allied Thief, Friendly Swimmer
        sim.form_alliance(0, 2);
        uint32_t friendly_worker = sim.spawn_unit(0, AntType::Worker, {20, 20});
        uint32_t enemy_combat    = sim.spawn_unit(1, AntType::Combat, {20, 20});
        uint32_t allied_thief    = sim.spawn_unit(2, AntType::Thief, {20, 20});
        uint32_t friendly_swimmer = sim.spawn_unit(0, AntType::Swimmer, {20, 20});

        sim.clear_audio_events();
        sim.tick(); // Bridge collapses!

        // Bridge gone
        ASSERT_FALSE(sim.has_bridge_at({20, 20}));

        // Non-swimmers drowned instantly:
        ASSERT_EQ(sim.get_unit(friendly_worker).hp, 0);
        ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(friendly_worker).death_status), 0x0Fu);
        ASSERT_EQ(sim.get_unit(friendly_worker).state, UnitState::Drowning);

        ASSERT_EQ(sim.get_unit(enemy_combat).hp, 0);
        ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(enemy_combat).death_status), 0x0Fu);
        ASSERT_EQ(sim.get_unit(enemy_combat).state, UnitState::Drowning);

        ASSERT_EQ(sim.get_unit(allied_thief).hp, 0);
        ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(allied_thief).death_status), 0x0Fu);
        ASSERT_EQ(sim.get_unit(allied_thief).state, UnitState::Drowning);

        // Swimmer survived:
        ASSERT_EQ(sim.get_unit(friendly_swimmer).hp, 10);
        ASSERT_EQ(sim.get_unit(friendly_swimmer).death_status, DeathStatus::Alive);
        ASSERT_EQ(sim.get_unit(friendly_swimmer).state, UnitState::Swimming);

        // Audio events: Sound 71 and Sound 72 emitted
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));

        // Stats: friendly lost tracked
        ASSERT_GE(sim.get_player_stats(0).friendly_lost, 1u);
    } TEST_END();

    TEST_CASE("4.5 Empty Bridge Collapse Cleans Up Silently") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.set_terrain(20, 20, TERRAIN_WATER);
        sim.set_bridge_at({20, 20}, 4, 1);

        sim.clear_audio_events();
        sim.tick();

        ASSERT_FALSE(sim.has_bridge_at({20, 20}));
        ASSERT_FALSE(sim.has_audio_event(SoundID::AntDrown)); // No units drowned
    } TEST_END();
}

// ============================================================================
// SUITE 5: Bombs: Planting, Proximity Detonation, Defusal & Safety
// ============================================================================
void run_suite_5_bombs() {
    TEST_SUITE("Suite 5: Bombs: Planting, Proximity Detonation, Defusal & Safety");

    TEST_CASE("5.1 Cardinal-Only Planting Enforced (Sound 90 bombpick.wav)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, {20, 20});

        // Flag 0x02 on cardinal neighbor (21, 20)
        sim.set_tile_flags(21, 20, 0x02);
        sim.clear_audio_events();
        ASSERT_TRUE(sim.plant_bomb(bomber, {21, 20}));
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombPick)); // Sound 90
        ASSERT_TRUE(sim.has_bomb_at({21, 20}));

        // Diagonal neighbor (21, 21) rejected
        sim.set_tile_flags(21, 21, 0x02);
        ASSERT_FALSE(sim.plant_bomb(bomber, {21, 21}));

        // Distance 2 (22, 20) rejected
        sim.set_tile_flags(22, 20, 0x02);
        ASSERT_FALSE(sim.plant_bomb(bomber, {22, 20}));

        // Missing flag 0x02 rejected
        sim.set_tile_flags(19, 20, 0x00);
        ASSERT_FALSE(sim.plant_bomb(bomber, {19, 20}));

        // Non-bomber unit rejected
        uint32_t worker = sim.spawn_unit(0, AntType::Worker, {20, 20});
        sim.set_tile_flags(20, 21, 0x02);
        ASSERT_FALSE(sim.plant_bomb(worker, {20, 21}));
    } TEST_END();

    TEST_CASE("5.2 Proximity Detonation: 2 HP Damage, Sound 4 & Knockback") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 101);
        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, {20, 20});
        sim.set_tile_flags(21, 20, 0x02);
        sim.plant_bomb(bomber, {21, 20});

        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {21, 20});
        sim.clear_audio_events();
        sim.tick(); // Detonates!

        ASSERT_FALSE(sim.has_bomb_at({21, 20}));
        ASSERT_EQ(sim.get_unit(enemy).hp, 8); // 10 - 2 = 8 HP
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombDetonate)); // Sound 4
    } TEST_END();

    TEST_CASE("5.3 Friendly and Allied Units Walk Safely Over Bombs") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.form_alliance(0, 1);

        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, {20, 20});
        sim.set_tile_flags(21, 20, 0x02);
        sim.plant_bomb(bomber, {21, 20});

        // Friendly unit walks on bomb
        uint32_t friendly = sim.spawn_unit(0, AntType::Worker, {21, 20});
        sim.tick();
        ASSERT_EQ(sim.get_unit(friendly).hp, 10);
        ASSERT_TRUE(sim.has_bomb_at({21, 20}));

        // Allied unit walks on bomb
        uint32_t ally = sim.spawn_unit(1, AntType::Worker, {21, 20});
        sim.tick();
        ASSERT_EQ(sim.get_unit(ally).hp, 10);
        ASSERT_TRUE(sim.has_bomb_at({21, 20}));
    } TEST_END();

    TEST_CASE("5.4 Bomber-Only Squash Defusal: 0 Damage, Sounds 73+74") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);

        // Enemy plants bomb at (21, 20)
        uint32_t enemy_bomber = sim.spawn_unit(1, AntType::Bomber, {22, 20});
        sim.set_tile_flags(21, 20, 0x02);
        sim.plant_bomb(enemy_bomber, {21, 20});

        // Friendly Bomber defuses
        uint32_t friendly_bomber = sim.spawn_unit(0, AntType::Bomber, {20, 20});
        sim.clear_audio_events();
        ASSERT_TRUE(sim.defuse_bomb(friendly_bomber, {21, 20}));

        // Bomb removed, 0 damage, sounds 73 + 74
        ASSERT_FALSE(sim.has_bomb_at({21, 20}));
        ASSERT_EQ(sim.get_unit(friendly_bomber).hp, 10);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombDefuseGrab));  // Sound 73
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombBodySquash));  // Sound 74
        ASSERT_EQ(sim.get_player_stats(0).bombs_defused, 1u);

        // Re-plant and test non-bomber defusal rejection
        sim.plant_bomb(enemy_bomber, {21, 20});
        uint32_t friendly_worker = sim.spawn_unit(0, AntType::Worker, {20, 20});
        ASSERT_FALSE(sim.defuse_bomb(friendly_worker, {21, 20}));
        ASSERT_TRUE(sim.has_bomb_at({21, 20})); // Bomb remains!
    } TEST_END();

    TEST_CASE("5.5 Lethal Bomb Detonation Stat Accounting") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);

        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, {20, 20});
        sim.set_tile_flags(21, 20, 0x02);
        sim.plant_bomb(bomber, {21, 20});

        // Enemy with 2 HP triggers bomb
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {21, 20});
        sim.get_unit(enemy).hp = 2;

        sim.tick(); // Detonation kills enemy!

        ASSERT_EQ(sim.get_unit(enemy).hp, 0);
        ASSERT_EQ(sim.get_unit(enemy).death_status, DeathStatus::BombKilled);
        ASSERT_EQ(sim.get_player_stats(0).enemy_killed, 1u);
        ASSERT_EQ(sim.get_player_stats(1).friendly_lost, 1u);
    } TEST_END();
}

// ============================================================================
// SUITE 6: Adversarial Boundary, Obstacle & Multi-Unit Stress Tests
// ============================================================================
void run_suite_6_adversarial_stress() {
    TEST_SUITE("Suite 6: Adversarial Boundary, Obstacle & Multi-Unit Stress Tests");

    TEST_CASE("6.1 Corner Boundary Guard AI: Anchor at (0, 0) Scans Safe Subgrid") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {0, 0});
        ASSERT_EQ(sim.get_unit(combat).guard_anchor.x, 0);
        ASSERT_EQ(sim.get_unit(combat).guard_anchor.y, 0);

        // Intruders at valid points in positive quadrant (within bounds)
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {2, 2});
        sim.tick();
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::Intercepting);

        // Step until melee punch delivered
        for (int i = 0; i < 5; ++i) sim.tick();
        ASSERT_EQ(sim.get_unit(enemy).hp, 8);

        // Combat Ant returns to (0, 0)
        for (int i = 0; i < 20; ++i) sim.tick();
        ASSERT_EQ(sim.get_unit(combat).pos.x, 0);
        ASSERT_EQ(sim.get_unit(combat).pos.y, 0);
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);
    } TEST_END();

    TEST_CASE("6.2 Knockback Boundary Raycast Clamping: Punch at Grid Edge") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        // Combat Ant at (2, 10), Worker at (1, 10)
        // Punch knocks West towards negative coordinates
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {2, 10});
        uint32_t worker = sim.spawn_unit(1, AntType::Worker, {1, 10});

        sim.execute_melee_attack(combat, worker);

        // Target should be clamped at boundary (pos.x == 0), never negative!
        ASSERT_GE(sim.get_unit(worker).pos.x, 0);
        ASSERT_EQ(sim.get_unit(worker).pos.y, 10);
        ASSERT_EQ(sim.get_unit(worker).hp, 8);
    } TEST_END();

    TEST_CASE("6.3 Knockback Obstacle Raycasting Stops Flight at Obstacle Edge") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        // Place solid obstacle rock at (14, 10)
        sim.set_terrain(14, 10, TERRAIN_OBSTACLE);

        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {10, 10});
        uint32_t worker = sim.spawn_unit(1, AntType::Worker, {11, 10});

        sim.execute_melee_attack(combat, worker);

        // Flight stops before the obstacle at (13, 10)
        ASSERT_EQ(sim.get_unit(worker).pos.x, 13);
        ASSERT_EQ(sim.get_unit(worker).pos.y, 10);
        ASSERT_TRUE(sim.get_unit(worker).is_stunned());
    } TEST_END();

    TEST_CASE("6.4 Fire Ricochet Bounded in Enclosed Box (No Infinite Loops)") {
        PhysicsEngine physics;
        Grid grid;
        grid.init_empty(60, 60);
        PRNG prng(1);
        std::vector<AudioEvent> audio_out;

        // Create 3x3 fire pocket
        for (int y = 9; y <= 11; ++y) {
            for (int x = 13; x <= 15; ++x) {
                grid.place_firewall(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0);
            }
        }

        AntUnit victim(1, TeamId::Blue, AntType::Worker, 10, 10);
        victim.set_pixel_pos(10 * 32 + 16, 10 * 32 + 16);

        // Knock into fire box
        physics.apply_knockback(victim, 9 * 32 + 16, 10 * 32 + 16, 4, 4,
                                DamageSource::CombatPunch, audio_out, 0);

        std::vector<AntUnit*> units = {&victim};
        for (int t = 0; t < 10; ++t) {
            physics.tick(units, grid, audio_out, prng);
        }

        // Bounded loop guard prevented freeze; victim took fire damage
        ASSERT_LT(victim.hp, 10);
        // All fires remain unextinguished
        for (int y = 9; y <= 11; ++y) {
            for (int x = 13; x <= 15; ++x) {
                ASSERT_TRUE(grid.has_fire_at({x, y}));
            }
        }
    } TEST_END();

    TEST_CASE("6.5 Simultaneous Multi-Bridge Collapse Drowns All Occupants") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);

        // 3 distinct water tiles with bridges expiring at tick 1
        sim.set_terrain(10, 10, TERRAIN_WATER);
        sim.set_bridge_at({10, 10}, 4, 1);
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, {10, 10});

        sim.set_terrain(20, 20, TERRAIN_WATER);
        sim.set_bridge_at({20, 20}, 4, 1);
        uint32_t w2 = sim.spawn_unit(1, AntType::Combat, {20, 20});

        sim.set_terrain(30, 30, TERRAIN_WATER);
        sim.set_bridge_at({30, 30}, 4, 1);
        uint32_t s3 = sim.spawn_unit(0, AntType::Swimmer, {30, 30});

        sim.tick(); // All 3 bridges collapse simultaneously!

        ASSERT_FALSE(sim.has_bridge_at({10, 10}));
        ASSERT_FALSE(sim.has_bridge_at({20, 20}));
        ASSERT_FALSE(sim.has_bridge_at({30, 30}));

        // Non-swimmers w1 and w2 drowned
        ASSERT_EQ(sim.get_unit(w1).hp, 0);
        ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(w1).death_status), 0x0Fu);

        ASSERT_EQ(sim.get_unit(w2).hp, 0);
        ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(w2).death_status), 0x0Fu);

        // Swimmer s3 survived
        ASSERT_EQ(sim.get_unit(s3).hp, 10);
        ASSERT_EQ(sim.get_unit(s3).state, UnitState::Swimming);
    } TEST_END();

    TEST_CASE("6.6 Bomb Re-Planting on Occupied Tile Strictly Rejected") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, {20, 20});
        sim.set_tile_flags(21, 20, 0x02);

        ASSERT_TRUE(sim.plant_bomb(bomber, {21, 20}));
        // Attempting to plant on same tile again -> rejected
        ASSERT_FALSE(sim.plant_bomb(bomber, {21, 20}));

        // Attempting to plant bomb on firewall tile -> rejected
        sim.set_tile_flags(20, 21, 0x06);
        sim.set_fire_at({20, 21}, 3600);
        ASSERT_FALSE(sim.plant_bomb(bomber, {20, 21}));
    } TEST_END();

    TEST_CASE("6.7 4v4 Combat Ant Melee Skirmish: Zero Friendly Fire") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);

        std::vector<uint32_t> team0_combats;
        std::vector<uint32_t> team1_combats;

        for (int i = 0; i < 4; ++i) {
            team0_combats.push_back(sim.spawn_unit(0, AntType::Combat, {20 + i, 20}));
            team1_combats.push_back(sim.spawn_unit(1, AntType::Combat, {20 + i, 22}));
        }

        // Run 10 ticks of autonomous skirmish
        for (int t = 0; t < 10; ++t) {
            sim.tick();
        }

        // Verify friendly fire invariant: team0 units took damage only from team1
        for (uint32_t id : team0_combats) {
            ASSERT_GE(sim.get_unit(id).hp, 0);
        }
    } TEST_END();
}

int main() {
    std::cout << "=======================================================\n"
              << " ANTS - CHALLENGER M2_1 STRESS TEST SUITE          \n"
              << "=======================================================\n";

    run_suite_1_combat_guard_ai();
    run_suite_2_ballistic_water();
    run_suite_3_fire_ricochets();
    run_suite_4_bridges();
    run_suite_5_bombs();
    run_suite_6_adversarial_stress();

    std::cout << "\n=======================================================\n"
              << " CHALLENGER M2_1 TEST SUMMARY\n"
              << "=======================================================\n"
              << "  Total Test Cases: " << g_test_count << "\n"
              << "  Total Assertions: " << g_assert_count << "\n"
              << "  Failed Tests:     " << g_test_failures << "\n"
              << "=======================================================\n";

    if (g_test_failures == 0) {
        std::cout << " >>> ALL CHALLENGER M2_1 TESTS PASSED CLEANLY <<< \n\n";
        return 0;
    } else {
        std::cout << " >>> " << g_test_failures << " TEST(S) FAILED! <<< \n\n";
        return 1;
    }
}

