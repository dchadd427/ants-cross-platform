// Golden tests of the combat actions of the original game (Ants.exe): the contact at the tile crossing, the strike frame,
// the hit and blown flights, the landing blocks, the blast dispersal, bomb victims, stun, deferred death and its removal
// effects, the retreat at 1 hp and the combat ants' auto-engage. Numbers come from the clip tables of ants.chd and the
// decoded routines (docs/GAME_REVERSE_ENGINEERING.md 5.36).
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

bool audio_has(const std::vector<AudioEvent>& ev, uint32_t id) {
    for (const auto& e : ev) if (e.sound_id == id) return true;
    return false;
}

// Ticks until `pred` holds or `max_ms` passed; returns the elapsed time in ms (-1 = never).
int wait_ms(SimulationEngine& sim, int max_ms, const std::function<bool()>& pred) {
    for (int t = 0; t <= max_ms / kTickMs; ++t) {
        if (pred()) return t * kTickMs;
        sim.tick();
    }
    return -1;
}

} // namespace

int main() {
    std::cout << "=======================================================\n"
              << " Ants combat actions: contact, strike, flights, blasts, death\n"
              << "=======================================================\n";

    TEST_CASE("1.1 A hit: hp is lost at contact, the victim is thrown at the strike frame (180 ms), lands idle after 970 ms") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.clear_audio_events();
        sim.execute_melee_attack(a, b);
        // t = 0: hp 10 -> 9, the victim stands engaged on its tile, the attacker plays its attack clip
        ASSERT_EQ(sim.get_unit(b).hp, 9u);
        ASSERT_TRUE(sim.get_unit(b).engaged);
        ASSERT_EQ(sim.get_unit(a).state, UnitState::Attacking);
        ASSERT_EQ(sim.get_unit(b).pixel_x, 11 * 32 + 16);
        ASSERT_TRUE(audio_has(sim.poll_audio_events(), SoundID::BaseAlarmSiren));
        // an engaged victim rejects orders
        sim.issue_move_order(b, TileCoord{20, 20});
        ASSERT_TRUE(sim.get_unit(b).waypoints.empty() && !sim.has_pending_path(b));
        run_ms(sim, 150);
        ASSERT_TRUE(sim.get_unit(b).engaged);                         // nothing moved before the strike frame
        ASSERT_EQ(sim.get_unit(b).pixel_x, 11 * 32 + 16);
        // strike frame at 180 ms: the flight (gh) starts, sound 64
        sim.clear_audio_events();
        run_ms(sim, 50);
        ASSERT_FALSE(sim.get_unit(b).engaged);
        ASSERT_EQ(sim.get_unit(b).state, UnitState::Flinch);
        ASSERT_TRUE(audio_has(sim.poll_audio_events(), SoundID::FlingThumpA));
        // the attacker is idle again at 420 ms with its order cleared
        run_ms(sim, 250);
        ASSERT_EQ(sim.get_unit(a).loco_action, AntUnit::kActionIdle);
        ASSERT_EQ(sim.get_unit(a).orig_order, AntUnit::kOrderNone);
        // the victim moved one tile away from the attacker and lands at the event frame (500 ms), no stun
        ASSERT_EQ(sim.get_unit(b).pos, (TileCoord{12, 10}));
        const int done = wait_ms(sim, 1200, [&]() { return sim.get_unit(b).loco_action == AntUnit::kActionIdle; });
        ASSERT_TRUE(done >= 0);
        ASSERT_NEAR(static_cast<uint32_t>(done + 450), 985u, 60u);           // 450 ms have passed since the contact
        ASSERT_EQ(sim.get_unit(b).pixel_x, 12 * 32 + 16);
        ASSERT_EQ(sim.get_unit(b).orig_order, AntUnit::kOrderNone);
        ASSERT_FALSE(sim.get_unit(b).is_stunned());
        ASSERT_EQ(sim.get_unit(b).hp, 9u);
    } TEST_END();

    TEST_CASE("1.2 Damage per attacker type: 1 hp, a combat ant 2 hp") {
        const AntType types[6] = {AntType::Worker, AntType::Bomber, AntType::Fire, AntType::Thief, AntType::Combat, AntType::Swimmer};
        for (AntType t : types) {
            SimulationEngine sim;
            make_world(sim);
            const uint32_t a = sim.spawn_unit(0, t, TileCoord{10, 10});
            const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
            sim.execute_melee_attack(a, b);
            ASSERT_EQ(sim.get_unit(b).hp, t == AntType::Combat ? 8u : 9u);
        }
    } TEST_END();

    TEST_CASE("1.3 A punch throws the victim 4 tiles at 300 ms (128 px in one step), gb lasts about 1 s") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.clear_audio_events();
        sim.execute_melee_attack(a, b);
        ASSERT_EQ(sim.get_unit(b).hp, 8u);
        run_ms(sim, 200);
        ASSERT_EQ(sim.get_unit(b).state, UnitState::Knockback);
        const auto ev = sim.poll_audio_events();
        ASSERT_TRUE(audio_has(ev, SoundID::HeavyPunch));              // attack2.wav on the strike frame
        ASSERT_TRUE(audio_has(ev, SoundID::FlingThumpA));
        run_ms(sim, 150);
        ASSERT_EQ(sim.get_unit(b).pos, (TileCoord{15, 10}));          // 4 tiles beyond the contact tile
        const int done = wait_ms(sim, 1500, [&]() { return sim.get_unit(b).loco_action == AntUnit::kActionIdle; });
        ASSERT_TRUE(done >= 0);
        ASSERT_FALSE(sim.get_unit(b).is_stunned());
        ASSERT_EQ(sim.get_unit(b).pixel_x, 15 * 32 + 16);
    } TEST_END();

    TEST_CASE("1.4 Death is deferred: hp 0 at contact, the victim still flies, dies at the end of the flight, is removed after the clip") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.get_unit(b).hp = 1;
        sim.execute_melee_attack(a, b);
        ASSERT_EQ(sim.get_unit(b).hp, 0u);
        ASSERT_FALSE(sim.get_unit(b).removed);
        ASSERT_EQ(sim.get_player_stats(1).friendly_lost, 0u);         // nothing is counted before the removal
        run_ms(sim, 500);
        ASSERT_EQ(sim.get_unit(b).state, UnitState::Flinch);         // still flying, alive and drawn
        ASSERT_EQ(sim.get_unit(b).pos, (TileCoord{12, 10}));
        const int dying = wait_ms(sim, 1500, [&]() { return sim.get_unit(b).state == UnitState::Dead; });
        ASSERT_TRUE(dying >= 0);
        ASSERT_NEAR(static_cast<uint32_t>(dying + 500), 970u, 100u);
        ASSERT_FALSE(sim.get_unit(b).removed);                       // the death clip plays first
        const int gone = wait_ms(sim, 1500, [&]() { return sim.get_unit(b).removed; });
        ASSERT_TRUE(gone >= 0);
        ASSERT_TRUE(gone >= 550 && gone <= 1050);                    // death1..death4: 600 .. 1000 ms
        ASSERT_EQ(sim.get_player_stats(1).friendly_lost, 1u);        // the scorecard counts at the removal
        ASSERT_EQ(sim.get_player_stats(0).enemy_killed, 1u);
        ASSERT_TRUE(sim.has_news_event(255, 51));                    // "Ant dead."
    } TEST_END();

    TEST_CASE("1.5 A dying ant drops its carried food at the removal; a typed ant leaves its power-up on a free neighbour tile") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.get_unit(b).hp = 1;
        sim.get_unit(b).pick_up_food(1, 40);
        sim.execute_melee_attack(a, b);
        ASSERT_FALSE(sim.has_lunchbox_at(TileCoord{12, 10}));
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(b).removed; }) >= 0);
        ASSERT_TRUE(sim.has_lunchbox_at(TileCoord{12, 10}));
        ASSERT_EQ(sim.get_lunchbox_points(TileCoord{12, 10}), 40u);
        // a bomber leaves a bomber power-up (never on its own tile)
        SimulationEngine sim2;
        make_world(sim2);
        const uint32_t a2 = sim2.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b2 = sim2.spawn_unit(1, AntType::Bomber, TileCoord{11, 10});
        sim2.get_unit(b2).hp = 1;
        sim2.execute_melee_attack(a2, b2);
        ASSERT_TRUE(wait_ms(sim2, 4000, [&]() { return sim2.get_unit(b2).removed; }) >= 0);
        int found = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (sim2.grid().has_powerup_at(TileCoord{12 + dx, 10 + dy})) {
                    ASSERT_TRUE(dx != 0 || dy != 0);
                    ++found;
                }
            }
        }
        ASSERT_EQ(found, 1);
    } TEST_END();

    TEST_CASE("1.6 The last ant of a team with eggs hatches a free egg when it is removed (cost min(score, 200))") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_player_eggs(1, 3);
        sim.set_player_score(1, 120);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.get_unit(b).hp = 1;
        sim.execute_melee_attack(a, b);
        ASSERT_EQ(sim.get_pending_hatch_count(1), 0u);
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(b).removed; }) >= 0);
        ASSERT_EQ(sim.get_pending_hatch_count(1), 1u);
        ASSERT_EQ(sim.get_player_eggs(1), 2u);
        ASSERT_EQ(sim.get_player_score(1), 0);
    } TEST_END();

    TEST_CASE("1.7 A 1 hp survivor of a hit goes home when the flight ends, not before") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.get_unit(b).hp = 2;
        sim.execute_melee_attack(a, b);
        ASSERT_EQ(sim.get_unit(b).hp, 1u);
        run_ms(sim, 600);
        ASSERT_EQ(sim.get_unit(b).orig_order, 0x0C);                    // still in the flight (order 0xC)
        ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return sim.get_unit(b).orig_order == AntUnit::kOrderHome; }) >= 0);
    } TEST_END();

    TEST_CASE("1.8 A victim that is cornered (no free landing tile in 5 directions) is stunned at the strike frame") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        // walls on the five landing tiles E, SE, NE, S, N of the victim (dir, +1, -1, +2, -2)
        for (const TileCoord t : {TileCoord{12, 10}, TileCoord{12, 9}, TileCoord{12, 11}, TileCoord{11, 9}, TileCoord{11, 11}}) {
            sim.set_terrain(t.x, t.y, TERRAIN_OBSTACLE);
        }
        sim.execute_melee_attack(a, b);
        ASSERT_EQ(sim.get_unit(b).hp, 9u);
        run_ms(sim, 250);
        ASSERT_EQ(sim.get_unit(b).loco_action, AntUnit::kActionStun);     // FUN_0102151a(1) at the strike frame
        ASSERT_EQ(sim.get_unit(b).pos, (TileCoord{11, 10}));
    } TEST_END();

    TEST_CASE("1.9 The landing direction is deflected in the order dir, +1, -1, +2, -2 when the landing tile is solid") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.set_terrain(12, 10, TERRAIN_OBSTACLE);                        // east is solid: the next try is dir+1 = south-east
        sim.execute_melee_attack(a, b);
        run_ms(sim, 250);
        ASSERT_EQ(sim.get_unit(b).flight_tile, (TileCoord{12, 11}));
        ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return sim.get_unit(b).loco_action == AntUnit::kActionIdle; }) >= 0);
        ASSERT_EQ(sim.get_unit(b).pos, (TileCoord{12, 11}));
    } TEST_END();

    TEST_CASE("1.10 Water on either tile refuses the contact (\"Can't go there.\"), a hit victim cannot be hit again") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Swimmer, TileCoord{11, 10});
        sim.set_terrain(11, 10, TERRAIN_WATER);
        sim.tick();
        sim.execute_melee_attack(a, b);
        ASSERT_EQ(sim.get_unit(b).hp, 10u);
        ASSERT_EQ(sim.get_unit(a).loco_action, AntUnit::kActionCantGo);
        ASSERT_TRUE(sim.has_news_event(0, 58));
        // an engaged victim refuses a second attacker
        SimulationEngine sim2;
        make_world(sim2);
        const uint32_t a1 = sim2.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t a2 = sim2.spawn_unit(0, AntType::Worker, TileCoord{12, 10});
        const uint32_t v = sim2.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim2.execute_melee_attack(a1, v);
        ASSERT_EQ(sim2.get_unit(v).hp, 9u);
        sim2.execute_melee_attack(a2, v);
        ASSERT_EQ(sim2.get_unit(v).hp, 9u);                                // refused: engaged
        ASSERT_TRUE(sim2.has_news_event(0, 48));                            // "Can't do that..."
    } TEST_END();

    TEST_CASE("2.1 \"Ouch!\" for the victim's team and allies at every contact, the alarm cue at most once per 10 s") {
        SimulationEngine sim;
        make_world(sim);
        sim.form_alliance(1, 2);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        const uint32_t c = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        const uint32_t d = sim.spawn_unit(1, AntType::Worker, TileCoord{21, 20});
        sim.clear_audio_events();
        sim.execute_melee_attack(a, b);
        ASSERT_TRUE(sim.has_news_event(1, 55));
        ASSERT_TRUE(sim.has_news_event(2, 55));                              // the ally reads it too
        ASSERT_FALSE(sim.has_news_event(0, 55));
        ASSERT_TRUE(sim.has_targeted_audio_event(1, SoundID::BaseAlarmSiren));
        sim.clear_audio_events();
        run_ms(sim, 3000);
        sim.clear_audio_events();
        sim.execute_melee_attack(c, d);                                       // 3 s later: text again, but no alarm
        ASSERT_FALSE(sim.has_audio_event(SoundID::BaseAlarmSiren));
        run_ms(sim, 9000);
        sim.clear_audio_events();
        const uint32_t e = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
        const uint32_t f = sim.spawn_unit(1, AntType::Worker, TileCoord{31, 30});
        sim.execute_melee_attack(e, f);                                       // 9 s after the last contact: still silent
        ASSERT_FALSE(sim.has_audio_event(SoundID::BaseAlarmSiren));
        run_ms(sim, 10500);
        sim.clear_audio_events();
        const uint32_t g = sim.spawn_unit(0, AntType::Worker, TileCoord{40, 30});
        const uint32_t h = sim.spawn_unit(1, AntType::Worker, TileCoord{41, 30});
        sim.execute_melee_attack(g, h);                                       // more than 10 s after the last contact: the alarm again
        ASSERT_TRUE(sim.has_audio_event(SoundID::BaseAlarmSiren));
    } TEST_END();

    TEST_CASE("3.1 A landing on water drowns a non-swimmer at the landing frame; a swimmer splashes and is stunned") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        for (int y = 8; y <= 12; ++y) sim.set_terrain(15, y, TERRAIN_WATER);   // the punch lands on (15, 10)
        sim.execute_melee_attack(a, b);
        ASSERT_TRUE(wait_ms(sim, 1200, [&]() { return sim.get_unit(b).loco_action == AntUnit::kActionDrown; }) >= 0);
        ASSERT_EQ(sim.get_unit(b).hp, 8u);                                     // drowning does not change the hit points
        ASSERT_TRUE(wait_ms(sim, 3500, [&]() { return sim.get_unit(b).removed; }) >= 0);
        ASSERT_TRUE(sim.has_news_event(255, 52));                              // "Ant drowned."
        SimulationEngine sim2;
        make_world(sim2);
        const uint32_t a2 = sim2.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        const uint32_t b2 = sim2.spawn_unit(1, AntType::Swimmer, TileCoord{11, 10});
        for (int y = 8; y <= 12; ++y) sim2.set_terrain(15, y, TERRAIN_WATER);
        sim2.execute_melee_attack(a2, b2);
        ASSERT_TRUE(wait_ms(sim2, 1500, [&]() { return sim2.get_unit(b2).loco_action == AntUnit::kActionStun; }) >= 0);
        ASSERT_FALSE(sim2.get_unit(b2).removed);
        ASSERT_EQ(sim2.get_unit(b2).pos, (TileCoord{15, 10}));
    } TEST_END();

    TEST_CASE("3.2 Landing on a bomb sets it off (the bomb comes before the pile-up, fire and water blocks)") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.grid_mut().place_bomb(12, 10, 0);                                 // the landing tile of a worker hit
        sim.execute_melee_attack(a, b);
        ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return !sim.grid().has_bomb_at(TileCoord{12, 10}); }) >= 0);
        ASSERT_EQ(sim.get_unit(b).hp, 9u - 2u);                                // 2 more hit points
    } TEST_END();

    TEST_CASE("3.3 Several ants on the landing tile are thrown apart without damage (pile-up dispersal)") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        const uint32_t idle = sim.spawn_unit(1, AntType::Worker, TileCoord{12, 10});   // stands on the landing tile
        sim.execute_melee_attack(a, b);
        ASSERT_TRUE(wait_ms(sim, 2500, [&]() { return sim.get_unit(b).loco_action == AntUnit::kActionIdle &&
                                                       sim.get_unit(idle).loco_action == AntUnit::kActionIdle &&
                                                       sim.get_unit(b).pos != sim.get_unit(idle).pos; }) >= 0);
        ASSERT_EQ(sim.get_unit(b).hp, 9u);
        ASSERT_EQ(sim.get_unit(idle).hp, 10u);                                 // nobody is hurt by the pile-up
    } TEST_END();

    TEST_CASE("3.4 A fire wall on the landing tile costs the ant on it 1 hp; the wall stays") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.grid_mut().place_firewall(12, 10, 0);
        sim.execute_melee_attack(a, b);
        ASSERT_EQ(sim.get_unit(b).hp, 9u);
        ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return sim.get_unit(b).hp == 8u; }) >= 0);
        ASSERT_TRUE(sim.grid().has_fire_at(TileCoord{12, 10}));
    } TEST_END();

    TEST_CASE("4.1 A bomb victim loses 2 hp, is thrown 4 tiles and is stunned when the flight ends (stun by type)") {
        struct Row { AntType type; uint32_t stun_ms; };
        const Row rows[3] = { {AntType::Worker, 3125}, {AntType::Bomber, 3835}, {AntType::Thief, 2610} };
        for (const Row& r : rows) {
            bool tested = false;
            for (uint32_t seed = 1; seed < 40 && !tested; ++seed) {           // the dud roll is random: find a real blast
                SimulationEngine sim;
                make_world(sim, seed);
                const uint32_t v = sim.spawn_unit(1, r.type, TileCoord{20, 20});
                sim.grid_mut().place_bomb(20, 20, 0);
                sim.trigger_bomb_detonation(v, TileCoord{20, 20}, 0, 0);
                if (sim.get_unit(v).knock_flag) continue;                       // a dud
                tested = true;
                ASSERT_EQ(sim.get_unit(v).hp, 8u);
                ASSERT_FALSE(sim.grid().has_bomb_at(TileCoord{20, 20}));
                ASSERT_TRUE(wait_ms(sim, 2500, [&]() { return sim.get_unit(v).loco_action == AntUnit::kActionStun; }) >= 0);
                int elapsed = 0;
                while (sim.get_unit(v).loco_action == AntUnit::kActionStun && elapsed < 6000) { sim.tick(); elapsed += kTickMs; }
                // the stun clip starts inside the step callback of the flight's last frame: its first frame is booked
                // twice (the re-entrancy of the original's stepper)
                const uint32_t first = movement::action_clip(movement::ActionClip::Stun, static_cast<uint8_t>(r.type), 0, false).duration(0);
                ASSERT_EQ(movement::action_clip(movement::ActionClip::Stun, static_cast<uint8_t>(r.type), 0, false).total_duration_ms(), r.stun_ms);
                ASSERT_NEAR(static_cast<uint32_t>(elapsed), r.stun_ms + first, 60u);
                ASSERT_EQ(sim.get_unit(v).loco_action, AntUnit::kActionIdle);
            }
            ASSERT_TRUE(tested);
        }
    } TEST_END();

    TEST_CASE("4.2 A dud (20 %) burns the frozen ant under the overlay for the ?bu clip, then it is stunned") {
        bool tested = false;
        for (uint32_t seed = 1; seed < 80 && !tested; ++seed) {
            SimulationEngine sim;
            make_world(sim, seed);
            const uint32_t v = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
            sim.grid_mut().place_bomb(20, 20, 0);
            sim.trigger_bomb_detonation(v, TileCoord{20, 20}, 0, 0);
            if (!sim.get_unit(v).knock_flag) continue;
            tested = true;
            ASSERT_TRUE(sim.get_unit(v).frozen);
            ASSERT_EQ(sim.get_unit(v).pos, (TileCoord{20, 20}));
            ASSERT_EQ(sim.get_unit(v).hp, 8u);
            sim.issue_move_order(v, TileCoord{25, 25});                          // a frozen ant takes no order
            ASSERT_TRUE(sim.get_unit(v).waypoints.empty());
            run_ms(sim, 1100);
            ASSERT_TRUE(sim.get_unit(v).frozen);
            run_ms(sim, 100);
            ASSERT_FALSE(sim.get_unit(v).frozen);
            ASSERT_EQ(sim.get_unit(v).loco_action, AntUnit::kActionStun);
        }
        ASSERT_TRUE(tested);
    } TEST_END();

    TEST_CASE("4.3 A stunned ant accepts an order and the order ends the stun") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        for (const TileCoord t : {TileCoord{12, 10}, TileCoord{12, 9}, TileCoord{12, 11}, TileCoord{11, 9}, TileCoord{11, 11}}) {
            sim.set_terrain(t.x, t.y, TERRAIN_OBSTACLE);
        }
        sim.execute_melee_attack(a, b);
        run_ms(sim, 250);
        ASSERT_EQ(sim.get_unit(b).loco_action, AntUnit::kActionStun);
        sim.issue_move_order(b, TileCoord{11, 14});
        ASSERT_NE(sim.get_unit(b).loco_action, AntUnit::kActionStun);
        ASSERT_EQ(sim.get_unit(b).state, UnitState::Walking);
    } TEST_END();

    TEST_CASE("5.1 Enemy ants that walk into each other never fight: they wait, re-plan or stop") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{14, 10});
        sim.issue_move_order(a, TileCoord{16, 10});
        sim.issue_move_order(b, TileCoord{8, 10});
        run_ms(sim, 6000);
        ASSERT_EQ(sim.get_unit(a).hp, 10u);
        ASSERT_EQ(sim.get_unit(b).hp, 10u);
        ASSERT_FALSE(sim.get_unit(a).removed);
        ASSERT_FALSE(sim.get_unit(b).removed);
    } TEST_END();

    TEST_CASE("5.2 An attack order walks to the target and the contact is the tile crossing (never at the click)") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        AntOrder o{};
        o.ant_id = a;
        o.type = OrderType::Attack;
        o.target_entity_id = static_cast<int32_t>(b);
        sim.issue_order(o);
        ASSERT_EQ(sim.get_unit(b).hp, 10u);                                     // no strike at the click
        const int hit = wait_ms(sim, 3000, [&]() { return sim.get_unit(b).hp < 10; });
        ASSERT_TRUE(hit >= 250 && hit <= 1000);                                 // 16 px of stride plus the path request
        ASSERT_EQ(sim.get_unit(b).hp, 9u);
        // one blow per order: the attacker is idle afterwards and does not strike again
        run_ms(sim, 4000);
        ASSERT_EQ(sim.get_unit(b).hp, 9u);
        ASSERT_EQ(sim.get_unit(a).orig_order, AntUnit::kOrderNone);
    } TEST_END();

    TEST_CASE("6.1 Auto-engage: an idle combat ant attacks an enemy within 3 tiles (rings 1..3), ignores one at 4") {
        for (int dist = 2; dist <= 4; ++dist) {
            SimulationEngine sim;
            make_world(sim);
            const uint32_t c = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
            const uint32_t e = sim.spawn_unit(1, AntType::Worker, TileCoord{30 + dist, 30});
            run_ms(sim, 3000);
            if (dist <= 3) {
                ASSERT_TRUE(sim.get_unit(e).hp < 10u);
            } else {
                ASSERT_EQ(sim.get_unit(e).hp, 10u);
                ASSERT_EQ(sim.get_unit(c).pos, (TileCoord{30, 30}));
            }
        }
    } TEST_END();

    TEST_CASE("6.2 Auto-engage never targets allies or the own team") {
        SimulationEngine sim;
        make_world(sim);
        sim.form_alliance(0, 1);
        const uint32_t c = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
        const uint32_t ally = sim.spawn_unit(1, AntType::Worker, TileCoord{31, 30});
        const uint32_t own = sim.spawn_unit(0, AntType::Worker, TileCoord{29, 30});
        run_ms(sim, 3000);
        ASSERT_EQ(sim.get_unit(ally).hp, 10u);
        ASSERT_EQ(sim.get_unit(own).hp, 10u);
        ASSERT_EQ(sim.get_unit(c).pos, (TileCoord{30, 30}));
    } TEST_END();

    TEST_CASE("6.3 After the fight a combat ant resumes its old order; no auto-engage within 2 s of any order") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t c = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
        sim.issue_move_order(c, TileCoord{30, 40});
        ASSERT_EQ(sim.get_unit(c).state, UnitState::Walking);
        const uint32_t e = sim.spawn_unit(1, AntType::Worker, TileCoord{32, 31});
        run_ms(sim, 1800);
        ASSERT_EQ(sim.get_unit(e).hp, 10u);
        // later the arrival on a tile next to the enemy engages it, and the walk goes on afterwards
        SimulationEngine sim2;
        make_world(sim2);
        const uint32_t c2 = sim2.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
        sim2.issue_move_order(c2, TileCoord{30, 45});
        run_ms(sim2, 2500);
        const uint32_t e2 = sim2.spawn_unit(1, AntType::Worker, TileCoord{32, sim2.get_unit(c2).pos.y + 1});
        run_ms(sim2, 6000);
        ASSERT_TRUE(sim2.get_unit(e2).hp < 10u);
        run_ms(sim2, 20000);
        ASSERT_EQ(sim2.get_unit(c2).pos, (TileCoord{30, 45}));               // the old order was resumed and completed
    } TEST_END();

    TEST_CASE("6.4 An ant standing on a power-up is immune from attacks: an attack order fails (Can't go there), the auto-engage never hits") {
        for (int mode = 0; mode < 2; ++mode) {                                    // 0 = attack order, 1 = auto-engage
            SimulationEngine sim;
            make_world(sim);
            sim.grid_mut().place_powerup(15, 15, 4);
            const uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{15, 15});
            sim.tick();
            sim.interrupt_transformation(victim);                                 // the can't-go trick: it stands on the power-up
            const uint32_t att = sim.spawn_unit(0, AntType::Combat, TileCoord{12, 15});
            if (mode == 0) {
                AntOrder o{};
                o.ant_id = att;
                o.type = OrderType::Attack;
                o.target_entity_id = static_cast<int32_t>(victim);
                sim.issue_order(o);
                ASSERT_TRUE(wait_ms(sim, 500, [&]() { return sim.has_news_event(0, 0x3A); }) >= 0);   // "Can't go there."
                ASSERT_TRUE(wait_ms(sim, 500, [&]() { return sim.get_unit(att).loco_action == AntUnit::kActionCantGo; }) >= 0);
            }
            run_ms(sim, 8000);
            ASSERT_EQ(sim.get_unit(victim).hp, 10u);
            ASSERT_EQ(sim.get_unit(victim).pos, (TileCoord{15, 15}));
            sim.execute_melee_attack(att, victim);                                // even a forced contact is refused
            ASSERT_EQ(sim.get_unit(victim).hp, 10u);
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Failures:         " << g_test_failures << "\n"
              << "=======================================================\n";
    if (g_test_failures == 0) {
        std::cout << " >>> ALL COMBAT ACTION TESTS PASSED CLEANLY <<<\n";
        return 0;
    }
    std::cout << " >>> " << g_test_failures << " TEST(S) FAILED <<<\n";
    return 1;
}
