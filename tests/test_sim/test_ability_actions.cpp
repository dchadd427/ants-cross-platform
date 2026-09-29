// Golden tests of the abilities of the original game (Ants.exe): the bomber's plant and defuse, the fire ant's ignite and
// extinguish, the swimmer's bridge build and demolish. Every ability is an action: the ant walks to the neighbour tile of
// its target, the clip plays, the target tile holds an invisible solid placeholder while a bomb or a fire wall is placed,
// and the world changes when the action ends (or is cut short: a melee hit or a stun cancels, a blast completes it).
// Numbers come from the clip tables of ants.chd and the decoded routines (docs/GAME_REVERSE_ENGINEERING.md 5.37).
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/movement_tables.hpp"

#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace ants::sim;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(92) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))
#define ASSERT_NEAR(a, b, tol) ASSERT_TRUE(((a) > (b) ? (a) - (b) : (b) - (a)) <= (tol))

namespace {

constexpr int kTickMs = 50;

void make_world(SimulationEngine& sim, uint32_t seed = 11) {
    sim.init_test_world(60, 60, seed, 720000);
    sim.set_anthill(0, TileCoord{2, 2});
    sim.set_anthill(1, TileCoord{50, 50});
    sim.set_player_score(0, 0);
    sim.set_player_score(1, 0);
}

void run_ms(SimulationEngine& sim, int ms) {
    for (int t = 0; t < ms / kTickMs; ++t) sim.tick();
}

// Ticks until `pred` holds or `max_ms` passed; returns the elapsed time in ms (-1 = never).
int wait_ms(SimulationEngine& sim, int max_ms, const std::function<bool()>& pred) {
    for (int t = 0; t <= max_ms / kTickMs; ++t) {
        if (pred()) return t * kTickMs;
        sim.tick();
    }
    return -1;
}

AntOrder order(uint32_t ant, OrderType type, TileCoord target) {
    AntOrder o{};
    o.ant_id = ant;
    o.type = type;
    o.target_x = target.x;
    o.target_y = target.y;
    return o;
}

const TileCell& cell_at(const SimulationEngine& sim, TileCoord t) { return sim.grid().get_cell(t); }

void make_mud(SimulationEngine& sim, int x, int y) {
    sim.grid_mut().get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).is_mud = true;
    sim.grid_mut().get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).surface_type = SurfaceType::Mud;
}

} // namespace

int main() {
    std::cout << "=======================================================\n"
              << " Ants abilities: bombs, fire walls, bridges\n"
              << "=======================================================\n";

    TEST_CASE("1.1 Plant: the bomber walks to the neighbour tile, the placeholder is solid at once, the bomb appears when the clip ends (1400 ms E)") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t b = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        sim.issue_order(order(b, OrderType::PlantBomb, TileCoord{13, 10}));
        ASSERT_EQ(sim.get_unit(b).orig_order, AntUnit::kOrderPlant);
        ASSERT_EQ(sim.get_unit(b).orig_special_tile, (TileCoord{13, 10}));
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(b).loco_action == AntUnit::kActionPlant; }) >= 0);
        ASSERT_EQ(sim.get_unit(b).pos, (TileCoord{12, 10}));                                   // the neighbour tile of the target
        ASSERT_EQ(sim.get_unit(b).facing, Direction::East);
        sim.clear_audio_events();
        sim.clear_news_events();
        // the placeholder: an invisible, solid tile that belongs to the team, no bomb yet
        ASSERT_TRUE(cell_at(sim, {13, 10}).has_reserved());
        ASSERT_EQ(cell_at(sim, {13, 10}).interactive_owner, 0u);
        ASSERT_TRUE(sim.grid().is_solid_object(TileCoord{13, 10}));
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{13, 10}));
        int elapsed = 0;
        while (!sim.has_bomb_at(TileCoord{13, 10}) && elapsed < 3000) {
            if (elapsed == 700) ASSERT_FALSE(sim.has_audio_event(90));                          // the cue is at 920 ms
            if (elapsed == 1000) ASSERT_TRUE(sim.has_audio_event(90));
            ASSERT_FALSE(sim.has_news_event(0, 0x38));
            sim.tick();
            elapsed += kTickMs;
        }
        ASSERT_TRUE(elapsed >= 1400 && elapsed <= 1550);                                        // absb east: 1400 ms
        ASSERT_FALSE(cell_at(sim, {13, 10}).has_reserved());
        ASSERT_EQ(cell_at(sim, {13, 10}).interactive_owner, 0u);
        ASSERT_TRUE(sim.has_news_event(0, 0x38));                                               // "Bomb dropped."
        ASSERT_EQ(sim.get_player_stats(0).bombs_planted, 1u);
        ASSERT_EQ(sim.get_unit(b).loco_action, AntUnit::kActionIdle);                           // idle and orderable at once
        ASSERT_EQ(sim.get_unit(b).orig_order, AntUnit::kOrderNone);
        ASSERT_EQ(sim.get_unit(b).pixel_x, 12 * 32 + 16);                                       // snapped to the tile centre
    } TEST_END();

    TEST_CASE("1.2 Plant targets: mud, water, solid tiles, hill tiles and stationary ants are refused with \"Can't do that...\"") {
        SimulationEngine sim;
        make_world(sim);
        make_mud(sim, 21, 10);
        sim.set_terrain(22, 10, TERRAIN_WATER);
        sim.grid_mut().place_powerup(23, 10, 4);                                                 // a solid power-up tile
        sim.spawn_unit(1, AntType::Worker, TileCoord{24, 10});                                   // a stationary ant
        const uint32_t b = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 12});
        for (int x = 21; x <= 24; ++x) {
            sim.clear_news_events();
            sim.issue_order(order(b, OrderType::PlantBomb, TileCoord{x, 10}));
            ASSERT_TRUE(sim.has_news_event(0, 0x30));                                            // "Can't do that..."
            ASSERT_EQ(sim.get_unit(b).loco_action, AntUnit::kActionCantGo);
            ASSERT_EQ(sim.get_unit(b).orig_order, AntUnit::kOrderNone);
            ASSERT_FALSE(cell_at(sim, {x, 10}).has_reserved());
            ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return sim.get_unit(b).loco_action != AntUnit::kActionCantGo; }) >= 0);   // the clip ends, the ant idles again
            run_ms(sim, 100);
        }
        // the hill's special tiles are refused as well: the queue tile above the entrance
        sim.clear_news_events();
        sim.issue_order(order(b, OrderType::PlantBomb, TileCoord{2, 1}));
        ASSERT_TRUE(sim.has_news_event(0, 0x30));
    } TEST_END();

    TEST_CASE("1.3 A melee hit cancels a plant: the placeholder disappears and no bomb is planted") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t b = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        const uint32_t e = sim.spawn_unit(1, AntType::Worker, TileCoord{10, 11});
        sim.issue_order(order(b, OrderType::PlantBomb, TileCoord{11, 10}));                      // the approach tile is (10, 10) itself
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(b).loco_action == AntUnit::kActionPlant; }) >= 0);
        ASSERT_TRUE(cell_at(sim, {11, 10}).has_reserved());
        run_ms(sim, 400);
        sim.execute_melee_attack(e, b);                                                          // the victim's cleanup runs with the cancel flag
        ASSERT_FALSE(cell_at(sim, {11, 10}).has_reserved());
        run_ms(sim, 3000);
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{11, 10}));
        ASSERT_EQ(sim.get_player_stats(0).bombs_planted, 0u);
    } TEST_END();

    TEST_CASE("1.4 A blast completes a plant at that very moment (the cleanup runs without the cancel flag)") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t b = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        sim.issue_order(order(b, OrderType::PlantBomb, TileCoord{11, 10}));
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(b).loco_action == AntUnit::kActionPlant; }) >= 0);
        run_ms(sim, 300);
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{11, 10}));
        sim.blast_tile_for_test(TileCoord{10, 10});                                              // Blast(0, 7) on the bomber's tile
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{11, 10}));
        ASSERT_FALSE(cell_at(sim, {11, 10}).has_reserved());
    } TEST_END();

    TEST_CASE("1.5 Defuse: the bomb stays until the clip ends (1140 ms), cues 73 and 74, \"Bomb defused.\", any team's bomb") {
        SimulationEngine sim;
        make_world(sim);
        sim.grid_mut().place_bomb(13, 10, 1);
        const uint32_t b = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        sim.issue_order(order(b, OrderType::PlantBomb, TileCoord{13, 10}));                      // the tile holds a bomb: the order is a defuse
        ASSERT_EQ(sim.get_unit(b).orig_order, AntUnit::kOrderDefuse);
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(b).loco_action == AntUnit::kActionDefuse; }) >= 0);
        sim.clear_audio_events();
        sim.clear_news_events();
        int elapsed = 0;
        while (sim.has_bomb_at(TileCoord{13, 10}) && elapsed < 3000) {
            if (elapsed == 150) ASSERT_FALSE(sim.has_audio_event(73));                           // 220 ms
            if (elapsed == 350) { ASSERT_TRUE(sim.has_audio_event(73)); ASSERT_FALSE(sim.has_audio_event(74)); }   // 620 ms
            if (elapsed == 800) ASSERT_TRUE(sim.has_audio_event(74));
            sim.tick();
            elapsed += kTickMs;
        }
        ASSERT_TRUE(elapsed >= 1140 && elapsed <= 1300);
        ASSERT_TRUE(sim.has_news_event(0, 0x39));                                                // "Bomb defused."
        ASSERT_EQ(sim.get_player_stats(0).bombs_defused, 1u);
        ASSERT_EQ(sim.get_unit(b).hp, 10u);                                                      // no explosion
    } TEST_END();

    TEST_CASE("1.6 A hit ant does not finish the defuse: the bomb stays") {
        SimulationEngine sim;
        make_world(sim);
        sim.grid_mut().place_bomb(11, 10, 1);
        const uint32_t b = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        const uint32_t e = sim.spawn_unit(1, AntType::Worker, TileCoord{10, 11});
        sim.issue_order(order(b, OrderType::PlantBomb, TileCoord{11, 10}));
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(b).loco_action == AntUnit::kActionDefuse; }) >= 0);
        run_ms(sim, 300);
        sim.execute_melee_attack(e, b);
        run_ms(sim, 3000);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{11, 10}));
        ASSERT_EQ(sim.get_player_stats(0).bombs_defused, 0u);
    } TEST_END();

    TEST_CASE("2.1 Ignite: \"Starting a fire...\" at the start, the fire wall when the clip ends (1810 ms E), cues 67 and 68, never on mud") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t f = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 10});
        make_mud(sim, 10, 12);
        sim.issue_order(order(f, OrderType::IgniteFire, TileCoord{10, 12}));                     // mud: refused
        ASSERT_TRUE(sim.has_news_event(0, 0x30));
        run_ms(sim, 1200);
        sim.clear_news_events();
        sim.issue_order(order(f, OrderType::IgniteFire, TileCoord{13, 10}));
        ASSERT_EQ(sim.get_unit(f).orig_order, AntUnit::kOrderIgnite);
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(f).loco_action == AntUnit::kActionIgnite; }) >= 0);
        ASSERT_TRUE(sim.has_news_event(0, 0x41));                                                // at the start
        ASSERT_TRUE(cell_at(sim, {13, 10}).has_reserved());
        sim.clear_audio_events();
        int elapsed = 0;
        while (!sim.has_fire_at(TileCoord{13, 10}) && elapsed < 4000) {
            if (elapsed == 400) ASSERT_FALSE(sim.has_audio_event(67));                           // 500 ms
            if (elapsed == 600) { ASSERT_TRUE(sim.has_audio_event(67)); ASSERT_FALSE(sim.has_audio_event(68)); }
            if (elapsed == 1500) ASSERT_TRUE(sim.has_audio_event(68));                           // 1350 ms
            sim.tick();
            elapsed += kTickMs;
        }
        ASSERT_TRUE(elapsed >= 1810 && elapsed <= 1950);
        ASSERT_EQ(cell_at(sim, {13, 10}).interactive_owner, 0u);
        ASSERT_EQ(sim.get_fire_timer(TileCoord{13, 10}), LIFETIME_180S_TICKS);                   // burns out 180 s later
        ASSERT_EQ(sim.get_player_stats(0).fires_lit, 1u);
    } TEST_END();

    TEST_CASE("2.2 A fire wall placed with 180 s or less of the match left never burns out (no timer)") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_match_time_remaining_ms(170000);
        const uint32_t f = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 10});
        sim.issue_order(order(f, OrderType::IgniteFire, TileCoord{11, 10}));
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return sim.has_fire_at(TileCoord{11, 10}); }) >= 0);
        ASSERT_EQ(sim.get_fire_timer(TileCoord{11, 10}), 0u);
    } TEST_END();

    TEST_CASE("2.3 Extinguish: the wall is put out when the clip ends (1300 ms E), sputter puff, cue 5, \"Fire put out.\"; a hit ant leaves it burning") {
        for (int mode = 0; mode < 2; ++mode) {
            SimulationEngine sim;
            make_world(sim);
            sim.grid_mut().place_firewall(11, 10, 0);
            const uint32_t f = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 10});
            const uint32_t e = sim.spawn_unit(1, AntType::Worker, TileCoord{10, 11});
            sim.issue_order(order(f, OrderType::IgniteFire, TileCoord{11, 10}));                 // a fire wall: the order is an extinguish
            ASSERT_EQ(sim.get_unit(f).orig_order, AntUnit::kOrderExtinguish);
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(f).loco_action == AntUnit::kActionExtinguish; }) >= 0);
            sim.clear_audio_events();
            sim.clear_news_events();
            if (mode == 1) {
                run_ms(sim, 300);
                sim.execute_melee_attack(e, f);
                run_ms(sim, 3000);
                ASSERT_TRUE(sim.has_fire_at(TileCoord{11, 10}));
                continue;
            }
            int elapsed = 0;
            while (sim.has_fire_at(TileCoord{11, 10}) && elapsed < 3000) {
                if (elapsed == 300) ASSERT_FALSE(sim.has_audio_event(69));                        // 400 ms
                if (elapsed == 500) ASSERT_TRUE(sim.has_audio_event(69));
                sim.tick();
                elapsed += kTickMs;
            }
            ASSERT_TRUE(elapsed >= 1300 && elapsed <= 1450);
            ASSERT_TRUE(sim.has_news_event(0, 0x40));
            ASSERT_TRUE(sim.has_audio_event(5));
            bool sputter = false;
            for (const auto& fx : sim.get_world_state().effects) {
                if (fx.anim_name == "sputter" && fx.px == 11 * 32 && fx.py == 10 * 32) sputter = true;
            }
            ASSERT_TRUE(sputter);
        }
    } TEST_END();

    TEST_CASE("3.1 Bridge build (from land): stage 0x22 at once, one stage per 480 ms pass, done after three passes, 180 s timer") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_terrain(11, 10, TERRAIN_WATER);
        sim.set_terrain(12, 10, TERRAIN_WATER);
        const uint32_t s = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
        sim.issue_order(order(s, OrderType::BuildBridge, TileCoord{11, 10}));
        ASSERT_EQ(sim.get_unit(s).orig_order, AntUnit::kOrderBridgeBuild);
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(s).loco_action == AntUnit::kActionBridgeBuild; }) >= 0);
        ASSERT_EQ(sim.grid().get_bridge_stage(TileCoord{11, 10}), 1);                            // 0x22 exists and is walkable from t = 0
        ASSERT_EQ(sim.grid().terrain_class_at(TileCoord{11, 10}), movement::kTerrainMud);
        ASSERT_EQ(cell_at(sim, {11, 10}).interactive_owner, 0u);
        int elapsed = 0;
        int stage = 1;
        int stage_time[5] = {0, 0, 0, 0, 0};
        while (sim.get_unit(s).loco_action == AntUnit::kActionBridgeBuild && elapsed < 4000) {
            sim.tick();
            elapsed += kTickMs;
            const int st = sim.grid().get_bridge_stage(TileCoord{11, 10});
            if (st != stage) { stage = st; stage_time[st] = elapsed; }
        }
        ASSERT_EQ(stage, 4);
        ASSERT_NEAR(stage_time[2], 480, 120);
        ASSERT_NEAR(stage_time[3], 960, 120);
        ASSERT_NEAR(stage_time[4], 1440, 150);
        ASSERT_TRUE(cell_at(sim, {11, 10}).has_completed_bridge());
        ASSERT_EQ(cell_at(sim, {11, 10}).timer_ticks, LIFETIME_180S_TICKS);
        ASSERT_EQ(sim.get_unit(s).orig_order, AntUnit::kOrderNone);
    } TEST_END();

    TEST_CASE("3.2 Bridge build cues: 81 on land, 82 in water, one per pass") {
        for (int mode = 0; mode < 2; ++mode) {                                                   // 0 = digging from land, 1 = from water
            SimulationEngine sim;
            make_world(sim);
            sim.set_terrain(11, 10, TERRAIN_WATER);
            if (mode == 1) sim.set_terrain(10, 10, TERRAIN_WATER);
            const uint32_t s = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
            sim.issue_order(order(s, OrderType::BuildBridge, TileCoord{11, 10}));
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(s).loco_action == AntUnit::kActionBridgeBuild; }) >= 0);
            sim.clear_audio_events();
            run_ms(sim, 700);
            ASSERT_TRUE(sim.has_audio_event(mode == 0 ? 81u : 82u));
            ASSERT_FALSE(sim.has_audio_event(mode == 0 ? 82u : 81u));
        }
    } TEST_END();

    TEST_CASE("3.3 An interrupted build removes the bridge; a completed bridge is not a build target (the order is a demolish)") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_terrain(11, 10, TERRAIN_WATER);
        const uint32_t s = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
        const uint32_t e = sim.spawn_unit(1, AntType::Worker, TileCoord{10, 11});
        sim.issue_order(order(s, OrderType::BuildBridge, TileCoord{11, 10}));
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(s).loco_action == AntUnit::kActionBridgeBuild; }) >= 0);
        run_ms(sim, 600);
        ASSERT_EQ(sim.grid().get_bridge_stage(TileCoord{11, 10}), 2);
        sim.execute_melee_attack(e, s);
        ASSERT_EQ(sim.grid().get_bridge_stage(TileCoord{11, 10}), 0);                           // the partial bridge is gone
        ASSERT_TRUE(cell_at(sim, {11, 10}).is_empty_overlay());
        sim.set_bridge_at(TileCoord{11, 10}, 4, 3600);                                           // a completed bridge instead
        const uint32_t s2 = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 9});
        sim.issue_order(order(s2, OrderType::BuildBridge, TileCoord{11, 10}));
        ASSERT_EQ(sim.get_unit(s2).orig_order, AntUnit::kOrderBridgeDemolish);
    } TEST_END();

    TEST_CASE("3.4 Demolish: the demolisher's team owns the tile, one stage less per pass, destroyed at the end of the fourth pass") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_terrain(11, 10, TERRAIN_WATER);
        sim.set_bridge_at(TileCoord{11, 10}, 4, 3600);
        sim.grid_mut().get_cell_mut(11, 10).interactive_owner = 1;                                // built by the enemy team
        const uint32_t s = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
        sim.issue_order(order(s, OrderType::BuildBridge, TileCoord{11, 10}));
        ASSERT_EQ(sim.get_unit(s).orig_order, AntUnit::kOrderBridgeDemolish);
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(s).loco_action == AntUnit::kActionBridgeDemolish; }) >= 0);
        ASSERT_EQ(cell_at(sim, {11, 10}).interactive_owner, 0u);                                  // the owner flips at the start
        int elapsed = 0;
        int stage = 4;
        int stage_time[5] = {0, 0, 0, 0, 0};
        while (sim.get_unit(s).loco_action == AntUnit::kActionBridgeDemolish && elapsed < 5000) {
            sim.tick();
            elapsed += kTickMs;
            const int st = sim.grid().get_bridge_stage(TileCoord{11, 10});
            if (st != stage) { stage = st; stage_time[st] = elapsed; }
        }
        ASSERT_NEAR(stage_time[3], 480, 120);
        ASSERT_NEAR(stage_time[2], 960, 120);
        ASSERT_NEAR(stage_time[1], 1440, 150);
        ASSERT_NEAR(elapsed, 1920, 200);                                                           // destroyed at the end of the fourth pass
        ASSERT_EQ(sim.grid().get_bridge_stage(TileCoord{11, 10}), 0);
        ASSERT_TRUE(cell_at(sim, {11, 10}).is_empty_overlay());
    } TEST_END();

    TEST_CASE("3.5 An interrupted demolish restores the completed bridge") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_terrain(11, 10, TERRAIN_WATER);
        sim.set_bridge_at(TileCoord{11, 10}, 4, 3600);
        const uint32_t s = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
        const uint32_t e = sim.spawn_unit(1, AntType::Worker, TileCoord{10, 11});
        sim.issue_order(order(s, OrderType::BuildBridge, TileCoord{11, 10}));
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(s).loco_action == AntUnit::kActionBridgeDemolish; }) >= 0);
        run_ms(sim, 1100);
        ASSERT_TRUE(sim.grid().get_bridge_stage(TileCoord{11, 10}) < 4);
        sim.execute_melee_attack(e, s);
        ASSERT_TRUE(cell_at(sim, {11, 10}).has_completed_bridge());                               // stage 0x25 again
    } TEST_END();

    TEST_CASE("3.6 Demolishing a bridge drowns the non-swimmers standing on it; a swimmer splashes (dsplash) and survives") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_terrain(11, 10, TERRAIN_WATER);
        sim.set_terrain(11, 11, TERRAIN_WATER);
        sim.set_bridge_at(TileCoord{11, 10}, 4, 3600);
        sim.set_bridge_at(TileCoord{11, 11}, 4, 3600);
        const uint32_t s = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
        const uint32_t worker = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        const uint32_t swimmer2 = sim.spawn_unit(1, AntType::Swimmer, TileCoord{11, 11});
        sim.issue_order(order(s, OrderType::BuildBridge, TileCoord{11, 10}));
        ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return sim.get_unit(worker).state == UnitState::Drowning; }) >= 0);
        ASSERT_EQ(sim.get_unit(worker).hp, 10u);                                                   // the drowning does not touch hit points
        ASSERT_TRUE(sim.get_unit(swimmer2).is_alive());
        // a second demolition under the swimmer splashes it
        run_ms(sim, 500);
        sim.issue_order(order(s, OrderType::BuildBridge, TileCoord{11, 11}));
        ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return sim.grid().get_bridge_stage(TileCoord{11, 11}) == 0; }) >= 0);
        bool splash = false;
        for (const auto& fx : sim.get_world_state().effects) {
            if (fx.anim_name == "dsplash") splash = true;
        }
        ASSERT_TRUE(splash);
        ASSERT_TRUE(sim.get_unit(swimmer2).is_alive());
        ASSERT_EQ(sim.get_unit(swimmer2).hp, 10u);
    } TEST_END();

    TEST_CASE("4.1 Order classification: the ant's type turns the special click into plant/defuse, ignite/extinguish, build/demolish or a plain walk") {
        SimulationEngine sim;
        make_world(sim);
        sim.grid_mut().place_bomb(21, 20, 1);
        sim.grid_mut().place_firewall(31, 20, 1);
        sim.set_terrain(41, 20, TERRAIN_WATER);
        sim.set_terrain(42, 20, TERRAIN_WATER);
        sim.set_bridge_at(TileCoord{42, 20}, 4, 3600);
        const uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 22});
        const uint32_t fire = sim.spawn_unit(0, AntType::Fire, TileCoord{30, 22});
        const uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{40, 22});
        const uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 26});
        sim.issue_order(order(bomber, OrderType::PlantBomb, TileCoord{21, 20}));
        ASSERT_EQ(sim.get_unit(bomber).orig_order, AntUnit::kOrderDefuse);
        sim.issue_order(order(fire, OrderType::IgniteFire, TileCoord{31, 20}));
        ASSERT_EQ(sim.get_unit(fire).orig_order, AntUnit::kOrderExtinguish);
        sim.issue_order(order(swimmer, OrderType::BuildBridge, TileCoord{42, 20}));
        ASSERT_EQ(sim.get_unit(swimmer).orig_order, AntUnit::kOrderBridgeDemolish);
        sim.issue_order(order(worker, OrderType::PlantBomb, TileCoord{25, 26}));                 // no ability: the order stays 0 and the ant walks there
        ASSERT_EQ(sim.get_unit(worker).orig_order, AntUnit::kOrderNone);
        ASSERT_EQ(sim.get_unit(worker).final_dest, (TileCoord{25, 26}));
    } TEST_END();

    TEST_CASE("4.2 A target without any free neighbour tile is refused at the order: \"Can't do that...\"") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_terrain(20, 10, TERRAIN_OBSTACLE);                                                // the four neighbours of (20, 11)
        sim.set_terrain(20, 12, TERRAIN_OBSTACLE);
        sim.set_terrain(19, 11, TERRAIN_OBSTACLE);
        sim.set_terrain(21, 11, TERRAIN_OBSTACLE);
        const uint32_t b = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 20});
        sim.issue_order(order(b, OrderType::PlantBomb, TileCoord{20, 11}));
        ASSERT_TRUE(sim.has_news_event(0, 0x30));
        ASSERT_EQ(sim.get_unit(b).loco_action, AntUnit::kActionCantGo);
        ASSERT_FALSE(cell_at(sim, {20, 11}).has_reserved());
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Failures:         " << g_test_failures << "\n"
              << "=======================================================\n";
    if (g_test_failures == 0) {
        std::cout << " >>> ALL ABILITY ACTION TESTS PASSED CLEANLY <<<\n";
        return 0;
    }
    std::cout << " >>> " << g_test_failures << " TEST(S) FAILED <<<\n";
    return 1;
}
