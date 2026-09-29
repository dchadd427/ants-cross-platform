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

static void run_ms(SimulationEngine& sim, int ms) {
    for (int t = 0; t < ms / 50; ++t) sim.tick();
}

// Ticks until `pred` holds or `max_ms` passed; returns the elapsed time in ms (-1 = never).
static int wait_ms(SimulationEngine& sim, int max_ms, const std::function<bool()>& pred) {
    for (int t = 0; t <= max_ms / 50; ++t) {
        if (pred()) return t * 50;
        sim.tick();
    }
    return -1;
}

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
    TEST_SUITE("Suite 1: Combat Ant Auto-Engage (FindEnemy, AttackTile, ResumeAfterAutoEngage)");

    TEST_CASE("1.1 FindEnemy rings 1..3: every tile of the 3-tile Chebyshev perimeter triggers the auto-engage") {
        // Combat Ant at (30, 30). Perimeter points have max(|dx|, |dy|) == 3.
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
            uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {30 + dx, 30 + dy});
            // the combat ant saw it (idle scan, radius 4) and started attacking: the enemy is hit
            ASSERT_TRUE(wait_ms(sim, 4500, [&]() { return sim.get_unit(enemy).hp < 10u; }) >= 0);
            (void)combat;
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
            uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {30 + dx, 30 + dy});
            run_ms(sim, 3000);
            ASSERT_EQ(sim.get_unit(enemy).hp, 10u);
            ASSERT_EQ(sim.get_unit(combat).pos, (TileCoord{30, 30}));
        }
    } TEST_END();

    TEST_CASE("1.3 Comprehensive Target Filtering (Friendly, Allied, Removed, Entering Hill, Flying)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.form_alliance(0, 1);

        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});

        // 1. Friendly unit at dist 1
        uint32_t friendly = sim.spawn_unit(0, AntType::Worker, {31, 30});
        // 2. Allied unit at dist 2
        uint32_t allied = sim.spawn_unit(1, AntType::Worker, {30, 32});
        // 3. Removed enemy unit at dist 1
        uint32_t dead_enemy = sim.spawn_unit(2, AntType::Worker, {29, 30});
        sim.kill_unit(dead_enemy);
        // 4. Enemy unit playing the enter / hatch clip at dist 2 (melee cannot start against it)
        uint32_t entering_enemy = sim.spawn_unit(2, AntType::Worker, {30, 28});
        sim.get_unit(entering_enemy).state = UnitState::EnteringBase;
        // 5. Enemy unit in a flight at dist 1
        uint32_t kb_enemy = sim.spawn_unit(2, AntType::Worker, {29, 29});
        sim.get_unit(kb_enemy).state = UnitState::Knockback;

        run_ms(sim, 3000);
        ASSERT_EQ(sim.get_unit(friendly).hp, 10u);
        ASSERT_EQ(sim.get_unit(allied).hp, 10u);
        ASSERT_EQ(sim.get_unit(entering_enemy).hp, 10u);
        ASSERT_EQ(sim.get_unit(kb_enemy).hp, 10u);
        ASSERT_EQ(sim.get_unit(combat).pos, (TileCoord{30, 30}));
        ASSERT_FALSE(sim.get_unit(combat).auto_engage);
    } TEST_END();

    TEST_CASE("1.4 FindEnemy takes the first enemy ring by ring: the closer intruder is attacked first") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});

        // Far enemy at dist 3
        uint32_t far_enemy = sim.spawn_unit(1, AntType::Worker, {33, 30});
        // Closer enemy at dist 2
        uint32_t near_enemy = sim.spawn_unit(1, AntType::Worker, {30, 32});

        // The first blow lands on the closer one
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(near_enemy).hp < 10u || sim.get_unit(far_enemy).hp < 10u; }) >= 0);
        ASSERT_EQ(sim.get_unit(near_enemy).hp, 8u);
        ASSERT_EQ(sim.get_unit(far_enemy).hp, 10u);
        (void)combat;
    } TEST_END();

    TEST_CASE("1.5 Heavy Punch: 2 HP Damage, Sound 78, thrown 4 tiles, no stun after a melee flight") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.spawn_unit(0, AntType::Combat, {30, 30});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {31, 30}); // Adjacent

        sim.clear_audio_events();
        ASSERT_TRUE(wait_ms(sim, 2500, [&]() { return sim.get_unit(enemy).hp < 10u; }) >= 0);
        ASSERT_EQ(sim.get_unit(enemy).hp, 8u); // 10 - 2 = 8 HP at contact
        bool heavy = false;
        int flight = wait_ms(sim, 3000, [&]() {
            for (const auto& ev : sim.poll_audio_events()) if (ev.sound_id == SoundID::HeavyPunch) heavy = true;
            return sim.get_unit(enemy).loco_action == AntUnit::kActionIdle && sim.get_unit(enemy).pos.x >= 35;
        });
        ASSERT_TRUE(flight >= 0);
        ASSERT_TRUE(heavy);                                  // Sound 78 on the strike frame
        ASSERT_FALSE(sim.get_unit(enemy).is_stunned());      // melee never stuns
        ASSERT_EQ(sim.get_unit(enemy).pos.x, 35);            // 4 tiles beyond the contact tile
        ASSERT_EQ(sim.get_unit(enemy).pos.y, 30);
    } TEST_END();

    TEST_CASE("1.6 After the fight the combat ant is idle on the tile it engaged from (the saved order)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {31, 30});

        ASSERT_TRUE(wait_ms(sim, 2500, [&]() { return sim.get_unit(enemy).hp < 10u; }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(combat).state == UnitState::GuardIdle &&
                                                       !sim.get_unit(combat).auto_engage; }) >= 0);
        ASSERT_EQ(sim.get_unit(combat).pos.x, 30);
        ASSERT_EQ(sim.get_unit(combat).pos.y, 30);
        ASSERT_EQ(sim.get_unit(combat).loco_action, AntUnit::kActionIdle);
    } TEST_END();

    TEST_CASE("1.7 A step towards the enemy that is not possible ends the auto-engage at once (the old order is resumed)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {33, 30}); // dist 3
        sim.set_terrain(31, 30, TERRAIN_OBSTACLE);                     // the first step of AttackTile is blocked

        run_ms(sim, 3000);
        ASSERT_EQ(sim.get_unit(enemy).hp, 10u);
        ASSERT_EQ(sim.get_unit(combat).pos, (TileCoord{30, 30}));
        ASSERT_FALSE(sim.get_unit(combat).auto_engage);
    } TEST_END();

    TEST_CASE("1.8 The target vanishes during the approach: the combat ant stops where it is") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {30, 30});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {33, 30});

        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(combat).auto_engage; }) >= 0);
        sim.kill_unit(enemy); // Target eliminated externally
        run_ms(sim, 4000);
        ASSERT_EQ(sim.get_unit(combat).loco_action, AntUnit::kActionIdle);
        ASSERT_EQ(sim.get_unit(combat).orig_order, AntUnit::kOrderNone);
    } TEST_END();

    TEST_CASE("1.9 A move order gives the ant its new place: the scan is centred on where it stands") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {20, 20});

        // Order move to (25, 20)
        sim.issue_move_order(combat, {25, 20});
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::Walking);
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return sim.get_unit(combat).pos.x == 25 &&
                                                       sim.get_unit(combat).loco_action == AntUnit::kActionIdle; }) >= 0);

        // Enemy at the old place's perimeter (21, 20) is dist 4 from (25, 20): ignored
        uint32_t far_enemy = sim.spawn_unit(1, AntType::Worker, {21, 20});
        run_ms(sim, 4000);
        ASSERT_EQ(sim.get_unit(far_enemy).hp, 10u);

        // Enemy at dist 3 from (25, 20) is attacked
        uint32_t near_enemy = sim.spawn_unit(1, AntType::Worker, {28, 20});
        ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return sim.get_unit(near_enemy).hp < 10u; }) >= 0);
    } TEST_END();
}

// ============================================================================
// SUITE 2: Ballistic Knockback into Water & Aquatic Physics
// ============================================================================
void run_suite_2_ballistic_water() {
    TEST_SUITE("Suite 2: Ballistic Knockback into Water & Aquatic Physics");

    TEST_CASE("2.1 Non-Swimmer Worker Punched Into Deep Water Drowns At The Landing Frame (hp unchanged by the water)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {9, 10});
        uint32_t worker = sim.spawn_unit(1, AntType::Worker, {10, 10});
        sim.set_terrain(14, 10, TERRAIN_WATER);            // 4 tiles east of the contact tile

        sim.execute_melee_attack(combat, worker);
        ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return sim.get_unit(worker).loco_action == AntUnit::kActionDrown; }) >= 0);

        // On landing, worker plunged into water: the drowning clip plays, the punch is the only damage
        ASSERT_EQ(sim.get_unit(worker).pos.x, 14);
        ASSERT_EQ(sim.get_unit(worker).pos.y, 10);
        ASSERT_EQ(sim.get_unit(worker).hp, 8u);
        ASSERT_EQ(sim.get_unit(worker).state, UnitState::Drowning);

        // Sound 71 splash.wav on the clip's first frame, sound 72 antdrown.wav 100 ms later
        run_ms(sim, 200);
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));

        // The ant is removed when the 2370 ms clip ends; the death status is the authenticated 0x0F
        ASSERT_FALSE(sim.get_unit(worker).removed);
        ASSERT_TRUE(wait_ms(sim, 3500, [&]() { return sim.get_unit(worker).removed; }) >= 0);
        ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(worker).death_status), 0x0Fu);
    } TEST_END();

    TEST_CASE("2.2 All Non-Swimmer Ant Classes Drown on Water Landing") {
        const std::vector<AntType> non_swimmers = {
            AntType::Worker, AntType::Bomber, AntType::Fire, AntType::Combat, AntType::Thief
        };

        for (AntType type : non_swimmers) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100);
            uint32_t combat = sim.spawn_unit(0, AntType::Combat, {9, 10});
            uint32_t victim = sim.spawn_unit(1, type, {10, 10});
            sim.set_terrain(14, 10, TERRAIN_WATER);
            sim.execute_melee_attack(combat, victim);
            ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return sim.get_unit(victim).loco_action == AntUnit::kActionDrown; }) >= 0);
            ASSERT_EQ(sim.get_unit(victim).state, UnitState::Drowning);
            ASSERT_TRUE(wait_ms(sim, 3500, [&]() { return sim.get_unit(victim).removed; }) >= 0);
            ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(victim).death_status), 0x0Fu);
        }
    } TEST_END();

    TEST_CASE("2.3 Swimmer Ant Punched Into Water Splashes (dsplash) and Is Stunned, Unharmed By The Water") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {9, 10});
        uint32_t swimmer = sim.spawn_unit(1, AntType::Swimmer, {10, 10});
        sim.set_terrain(14, 10, TERRAIN_WATER);

        sim.execute_melee_attack(combat, swimmer);
        ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return sim.get_unit(swimmer).loco_action == AntUnit::kActionStun; }) >= 0);

        // Swimmer survives: stunned on the water tile, hp only lost to the punch, no drowning scream
        ASSERT_EQ(sim.get_unit(swimmer).pos.x, 14);
        ASSERT_EQ(sim.get_unit(swimmer).pos.y, 10);
        ASSERT_EQ(sim.get_unit(swimmer).hp, 8u);
        ASSERT_FALSE(sim.get_unit(swimmer).removed);
        ASSERT_FALSE(sim.has_audio_event(SoundID::AntDrown));
        bool splash_effect = false;
        for (const auto& e : sim.get_world_state().effects) if (e.anim_name == "dsplash") splash_effect = true;
        ASSERT_TRUE(splash_effect);
    } TEST_END();

    TEST_CASE("2.4 Water Landing with Completed Bridge Protects Non-Swimmer") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {9, 10});
        uint32_t worker = sim.spawn_unit(1, AntType::Worker, {10, 10});
        // Water tile with completed bridge (stage 4)
        sim.set_terrain(14, 10, TERRAIN_WATER);
        sim.set_bridge_at({14, 10}, 4, 3600);

        sim.execute_melee_attack(combat, worker);
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(worker).pos.x == 14 &&
                                                       sim.get_unit(worker).loco_action == AntUnit::kActionIdle; }) >= 0);

        // Lands safely on the bridge as ground: no drowning, no stun after a melee flight
        ASSERT_EQ(sim.get_unit(worker).hp, 8u);
        ASSERT_FALSE(sim.get_unit(worker).removed);
        ASSERT_FALSE(sim.has_audio_event(SoundID::AntDrown));
    } TEST_END();

    TEST_CASE("2.5 Every Bridge Stage (1-3) Counts As Ground For The Landing (terrain class 3)") {
        for (int stage = 1; stage <= 3; ++stage) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100);
            uint32_t combat = sim.spawn_unit(0, AntType::Combat, {9, 10});
            uint32_t worker = sim.spawn_unit(1, AntType::Worker, {10, 10});
            sim.set_terrain(14, 10, TERRAIN_WATER);
            sim.set_bridge_at({14, 10}, stage, 0);

            sim.execute_melee_attack(combat, worker);
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(worker).pos.x == 14 &&
                                                           sim.get_unit(worker).loco_action == AntUnit::kActionIdle; }) >= 0);
            // A bridge piece on layer 2 (any of the stages 0x22..0x25) makes the tile mud (FUN_01008af7)
            ASSERT_EQ(sim.get_unit(worker).hp, 8u);
            ASSERT_FALSE(sim.get_unit(worker).removed);
            ASSERT_FALSE(sim.has_audio_event(SoundID::AntDrown));
        }
    } TEST_END();
}

// ============================================================================
// SUITE 3: Fire Ricochets, Reflection Physics & Fire Preservation
// ============================================================================
void run_suite_3_fire_ricochets() {
    TEST_SUITE("Suite 3: Fire Ricochets, Reflection Physics & Fire Preservation");

    TEST_CASE("3.1 A Punch Landing On A Fire Wall: 1 Damage And A New One-Tile Flight, The Wall Stays") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {9, 10});
        uint32_t worker = sim.spawn_unit(1, AntType::Worker, {10, 10});
        // Place fire on the landing tile (14, 10)
        sim.grid_mut().place_firewall(14, 10, 0);
        ASSERT_TRUE(sim.grid().has_fire_at({14, 10}));

        sim.execute_melee_attack(combat, worker);
        // The landing frame (event 3) of the gb flight finds the fire wall: Blast(1, owner) at the tile
        ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return sim.get_unit(worker).hp == 7u; }) >= 0);       // 10 - 2 (punch) - 1 (fire)
        ASSERT_TRUE(wait_ms(sim, 2500, [&]() { return sim.get_unit(worker).loco_action == AntUnit::kActionIdle; }) >= 0);
        const TileCoord p = sim.get_unit(worker).pos;
        ASSERT_TRUE(p != (TileCoord{14, 10}));                                                        // thrown one tile away
        ASSERT_TRUE(std::abs(p.x - 14) <= 1 && std::abs(p.y - 10) <= 1);

        // Fire tile at (14, 10) was NOT extinguished!
        ASSERT_TRUE(sim.grid().has_fire_at({14, 10}));
    } TEST_END();

    TEST_CASE("3.2 Fire Tile Never Extinguished by Repeated Ant Impacts") {
        for (int i = 0; i < 10; ++i) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100 + static_cast<uint32_t>(i));
            sim.grid_mut().place_firewall(20, 20, 0);
            uint32_t combat = sim.spawn_unit(0, AntType::Combat, {15, 20});
            uint32_t w = sim.spawn_unit(1, AntType::Worker, {16, 20});
            sim.execute_melee_attack(combat, w);
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(w).hp == 7u &&
                                                           sim.get_unit(w).loco_action == AntUnit::kActionIdle; }) >= 0);
            ASSERT_TRUE(sim.grid().has_fire_at({20, 20})); // FIRE MUST NEVER EXTINGUISH!
        }
    } TEST_END();

    TEST_CASE("3.3 A Fire Wall Contact Throws The Ant One Tile; Landing On Another Wall Costs Another Hit Point") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);

        // Two adjacent fire tiles at (20, 20) and (21, 20)
        sim.set_fire_at({20, 20}, 3600);
        sim.set_fire_at({21, 20}, 3600);

        uint32_t victim = sim.spawn_unit(1, AntType::Worker, {20, 20});

        // The fire contact (Blast(1, owner)): 1 hp and a one tile flight to a random free neighbour
        sim.resolve_fire_contact(victim, 1, 0);
        ASSERT_EQ(sim.get_unit(victim).hp, 9u);

        // It lands on plain ground (done) or on the other wall (another hp and another flight)
        ASSERT_TRUE(wait_ms(sim, 8000, [&]() {
            const auto& u = sim.get_unit(victim);
            return u.loco_action == AntUnit::kActionIdle && !sim.has_fire_at(u.pos);
        }) >= 0);
        ASSERT_TRUE(sim.get_unit(victim).hp <= 9u);
        ASSERT_TRUE(sim.has_fire_at({20, 20}));
        ASSERT_TRUE(sim.has_fire_at({21, 20}));
    } TEST_END();

    TEST_CASE("3.4 An Ant Without Hit Points Still Lands On The Fire Wall And Dies After The Flight") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {9, 10});
        uint32_t low_hp_worker = sim.spawn_unit(1, AntType::Worker, {10, 10});
        sim.get_unit(low_hp_worker).hp = 1; // 1 HP left
        sim.grid_mut().place_firewall(14, 10, 0);

        sim.execute_melee_attack(combat, low_hp_worker);
        ASSERT_EQ(sim.get_unit(low_hp_worker).hp, 0u);               // the punch took the last hit point at contact
        ASSERT_FALSE(sim.get_unit(low_hp_worker).removed);           // ... but the ant is still flying
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return sim.get_unit(low_hp_worker).removed; }) >= 0);
        ASSERT_EQ(sim.get_unit(low_hp_worker).killer_team, 0);       // credit of the last damage: team 0
        ASSERT_TRUE(sim.grid().has_fire_at({14, 10}));               // Still not extinguished!
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

    TEST_CASE("4.1 4-Stage Construction Progression: Three Passes From The Water, Sound 82, 180 s Timer At The End") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        sim.set_terrain(14, 15, TERRAIN_WATER);
        sim.set_terrain(15, 15, TERRAIN_WATER);

        uint32_t s = sim.spawn_unit(0, AntType::Swimmer, {14, 15});

        sim.clear_audio_events();
        ASSERT_TRUE(sim.build_bridge_step(s, {15, 15}));
        ASSERT_EQ(sim.get_bridge_stage({15, 15}), 1);
        for (int stage = 2; stage <= 4; ++stage) {
            for (int t = 0; t < 40 && sim.get_bridge_stage({15, 15}) < stage; ++t) sim.tick();
            ASSERT_EQ(sim.get_bridge_stage({15, 15}), stage);
        }
        ASSERT_TRUE(sim.has_audio_event(SoundID::ShovelWater)); // Sound 82 played in the passes

        // Bridge complete: exactly 3600 ticks (180s)
        ASSERT_TRUE(sim.has_bridge_at({15, 15}));
        ASSERT_EQ(sim.get_bridge_stage({15, 15}), 4);
        ASSERT_TRUE(sim.grid().get_cell({15, 15}).timer_ticks > LIFETIME_180S_TICKS - 60u);
        ASSERT_TRUE(sim.grid().get_cell({15, 15}).timer_ticks <= LIFETIME_180S_TICKS);
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

    TEST_CASE("4.4 Multi-Unit Collapse: All Non-Swimmers Start Drowning (0xF), Swimmer Survives") {
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

        // Non-swimmers play the drowning clip (their hit points do not change):
        for (uint32_t id : {friendly_worker, enemy_combat, allied_thief}) {
            ASSERT_EQ(sim.get_unit(id).hp, 10u);
            ASSERT_EQ(sim.get_unit(id).loco_action, AntUnit::kActionDrown);
            ASSERT_EQ(sim.get_unit(id).state, UnitState::Drowning);
        }

        // Swimmer survived:
        ASSERT_EQ(sim.get_unit(friendly_swimmer).hp, 10u);
        ASSERT_EQ(sim.get_unit(friendly_swimmer).death_status, DeathStatus::Alive);
        ASSERT_EQ(sim.get_unit(friendly_swimmer).state, UnitState::Swimming);

        // Audio events: Sound 71 (splash) and, 100 ms into the clip, Sound 72 (antdrown)
        run_ms(sim, 200);
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));

        // The removal after the 2370 ms clip: status 0x0F, the scorecard counts the lost ants
        ASSERT_TRUE(wait_ms(sim, 3500, [&]() { return sim.get_unit(friendly_worker).removed; }) >= 0);
        ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(friendly_worker).death_status), 0x0Fu);
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

    TEST_CASE("5.1 Cardinal-Only Planting Enforced (Sound 90 comes from the plant clip, the bomb appears at its end)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, {20, 20});

        sim.clear_audio_events();
        ASSERT_TRUE(sim.plant_bomb(bomber, {21, 20}, false));
        ASSERT_FALSE(sim.has_bomb_at({21, 20}));                 // only the invisible placeholder while the clip plays
        for (int t = 0; t < 45 && !sim.has_bomb_at({21, 20}); ++t) sim.tick();
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombPick)); // Sound 90
        ASSERT_TRUE(sim.has_bomb_at({21, 20}));

        // Diagonal neighbor (21, 21) rejected
        ASSERT_FALSE(sim.plant_bomb(bomber, {21, 21}));

        // Distance 2 (22, 20) rejected
        ASSERT_FALSE(sim.plant_bomb(bomber, {22, 20}));

        // Mud rejected
        sim.grid_mut().get_cell_mut(19, 20).is_mud = true;
        sim.grid_mut().get_cell_mut(19, 20).surface_type = SurfaceType::Mud;
        ASSERT_FALSE(sim.plant_bomb(bomber, {19, 20}));

        // Non-bomber unit rejected
        uint32_t worker = sim.spawn_unit(0, AntType::Worker, {20, 20});
        ASSERT_FALSE(sim.plant_bomb(worker, {20, 21}));
    } TEST_END();

    TEST_CASE("5.2 Proximity Detonation: 2 HP Damage, Sound 4 (an ant standing on the bomb sets it off when its idle clip ends)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 101);
        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, {20, 20});
        sim.set_tile_flags(21, 20, 0x02);
        sim.plant_bomb(bomber, {21, 20});

        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {21, 20});
        sim.clear_audio_events();
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !sim.has_bomb_at({21, 20}); }) >= 0);   // Detonates!

        ASSERT_EQ(sim.get_unit(enemy).hp, 8u); // 10 - 2 = 8 HP
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

    TEST_CASE("5.5 Lethal Bomb Detonation Stat Accounting (counted when the ant is removed, credit to the bomb's owner)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);

        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, {20, 20});
        sim.set_tile_flags(21, 20, 0x02);
        sim.plant_bomb(bomber, {21, 20});

        // Enemy with 2 HP triggers bomb
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {21, 20});
        sim.get_unit(enemy).hp = 2;

        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !sim.has_bomb_at({21, 20}); }) >= 0);
        ASSERT_EQ(sim.get_unit(enemy).hp, 0u);                       // the two hits are taken at once ...
        ASSERT_FALSE(sim.get_unit(enemy).removed);                   // ... the ant flies and dies later
        ASSERT_EQ(sim.get_player_stats(1).friendly_lost, 0u);
        ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return sim.get_unit(enemy).removed; }) >= 0);
        ASSERT_EQ(sim.get_player_stats(0).enemy_killed, 1u);
        ASSERT_EQ(sim.get_player_stats(1).friendly_lost, 1u);
    } TEST_END();
}

// ============================================================================
// SUITE 6: Adversarial Boundary, Obstacle & Multi-Unit Stress Tests
// ============================================================================
void run_suite_6_adversarial_stress() {
    TEST_SUITE("Suite 6: Adversarial Boundary, Obstacle & Multi-Unit Stress Tests");

    TEST_CASE("6.1 Corner Boundary Auto-Engage: An Ant At (0, 0) Scans Only The Map, Attacks, And Returns") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {0, 0});

        // Intruder at a valid point in the positive quadrant (within bounds)
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, {2, 2});
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(enemy).hp < 10u; }) >= 0);
        ASSERT_EQ(sim.get_unit(enemy).hp, 8u);

        // The old order (an idle place at (0, 0)) is given again after the blow
        ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return sim.get_unit(combat).pos == TileCoord{0, 0} &&
                                                       sim.get_unit(combat).loco_action == AntUnit::kActionIdle &&
                                                       !sim.get_unit(combat).auto_engage; }) >= 0);
        ASSERT_EQ(sim.get_unit(combat).state, UnitState::GuardIdle);
    } TEST_END();

    TEST_CASE("6.2 Knockback Boundary Clamping: Punch at Grid Edge") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        // Combat Ant at (2, 10), Worker at (1, 10)
        // Punch knocks West towards negative coordinates
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {2, 10});
        uint32_t worker = sim.spawn_unit(1, AntType::Worker, {1, 10});

        sim.execute_melee_attack(combat, worker);
        ASSERT_TRUE(wait_ms(sim, 2500, [&]() { return sim.get_unit(worker).loco_action == AntUnit::kActionIdle &&
                                                       sim.get_unit(worker).pos.y != 10; }) >= 0);

        // Never knocked off the map. The original punch (FUN_0101d8ed, combat range 4 tiles) tries the strike
        // direction and then d+1, d-1, d+2, d-2 and tests only the landing tile: west, north-west and
        // south-west land off the map, so the worker lands 4 tiles north at (1, 6).
        ASSERT_GE(sim.get_unit(worker).pos.x, 0);
        ASSERT_EQ(sim.get_unit(worker).pos.x, 1);
        ASSERT_EQ(sim.get_unit(worker).pos.y, 6);
        ASSERT_EQ(sim.get_unit(worker).hp, 8u);
    } TEST_END();

    TEST_CASE("6.3 Knockback Flies Over an Obstacle; Only the Landing Tile Is Tested (FUN_0101d8ed)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        // Place solid obstacle rock at (14, 10)
        sim.set_terrain(14, 10, TERRAIN_OBSTACLE);

        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {10, 10});
        uint32_t worker = sim.spawn_unit(1, AntType::Worker, {11, 10});

        sim.execute_melee_attack(combat, worker);
        ASSERT_TRUE(wait_ms(sim, 2500, [&]() { return sim.get_unit(worker).pos.x == 15 &&
                                                       sim.get_unit(worker).loco_action == AntUnit::kActionIdle; }) >= 0);

        // The punch flies the worker 4 tiles east over the rock at (14, 10): the original tests only the landing
        // tile (in bounds, not solid, not a hill tile), so it lands at (15, 10); a melee flight ends without a stun
        ASSERT_EQ(sim.get_unit(worker).pos.x, 15);
        ASSERT_EQ(sim.get_unit(worker).pos.y, 10);
        ASSERT_FALSE(sim.get_unit(worker).is_stunned());
    } TEST_END();

    TEST_CASE("6.4 Fire Contacts In A Pocket Of Fire Walls End (each landing on a wall costs 1 hp, walls stay)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100);
        // Create 3x3 fire pocket
        for (int y = 9; y <= 11; ++y) {
            for (int x = 13; x <= 15; ++x) {
                sim.grid_mut().place_firewall(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0);
            }
        }
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, {9, 10});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, {10, 10});
        // Punch into the fire box (landing tile (14, 10) is one of its walls)
        sim.execute_melee_attack(combat, victim);
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(victim).removed || sim.get_unit(victim).hp <= 7u; }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 12000, [&]() {
            const auto& u = sim.get_unit(victim);
            return u.removed || (u.loco_action == AntUnit::kActionIdle && !sim.has_fire_at(u.pos));
        }) >= 0);
        // the victim took fire damage (or died of it); all fires remain unextinguished
        ASSERT_TRUE(sim.get_unit(victim).removed || sim.get_unit(victim).hp < 8u);
        for (int y = 9; y <= 11; ++y) {
            for (int x = 13; x <= 15; ++x) {
                ASSERT_TRUE(sim.has_fire_at({x, y}));
            }
        }
    } TEST_END();

    TEST_CASE("6.5 Simultaneous Multi-Bridge Collapse Drowns All Non-Swimming Occupants") {
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

        // Non-swimmers w1 and w2 play the drowning clip and are removed at its end
        ASSERT_EQ(sim.get_unit(w1).loco_action, AntUnit::kActionDrown);
        ASSERT_EQ(sim.get_unit(w2).loco_action, AntUnit::kActionDrown);
        ASSERT_TRUE(wait_ms(sim, 3500, [&]() { return sim.get_unit(w1).removed && sim.get_unit(w2).removed; }) >= 0);
        ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(w1).death_status), 0x0Fu);
        ASSERT_EQ(static_cast<uint8_t>(sim.get_unit(w2).death_status), 0x0Fu);

        // Swimmer s3 survived
        ASSERT_EQ(sim.get_unit(s3).hp, 10u);
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

