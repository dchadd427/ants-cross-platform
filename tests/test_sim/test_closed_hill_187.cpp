// Tests of the closed hill of the game mode "187" (docs/GAMEPLAY.md "187", "The hill is closed"): in 187 an ant cannot enter its hill, so nothing heals, an order to enter is refused, an ant left with one
// hit point does not walk home, the waiting ring is idle and a thief's raid is refused; hatching, the end rule and the original's game are as they were. The earlier rule of 187 (the hill works, the rules
// number 6 of a recording) is the same engine with SimulationEngine::set_hill_open_187(true), and every case below that shows the closed hill shows the earlier rule beside it.
//   1.x  the switch: what the engine says about its hill, the state hash, the engine copy, the original's game never reads it
//   2.x  the order to enter: refused with "Can't go there." (a click, a group, the home order, the prediction), nobody enters, nothing heals
//   3.x  one hit point: no retreat, the stun clip like any other hurt ant, nobody queues at the ring
//   4.x  what stays: hatching, an ant on the entrance, the end rule, the thief
#include "ants_sim/command.hpp"
#include "ants_sim/game_mode.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"

#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "../common/test_assert.hpp"

using namespace ants::sim;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
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

namespace {

// Two hills: team 0 at (20,20) (entrance (21,21), ring tile (19,23), tile42 (24,24)), team 1 at (40,40). `open` is the earlier rule of 187 (a recording of the rules number 6).
void make_world(SimulationEngine& sim, GameMode mode, bool open = false, uint32_t seed = 11) {
    sim.set_game_mode(mode);
    sim.set_hill_open_187(open);
    sim.init_test_world(60, 60, seed, 720000);
    sim.set_anthill(0, TileCoord{20, 20});
    sim.set_anthill(1, TileCoord{40, 40});
    sim.set_player_score(0, 0);
    sim.set_player_score(1, 0);
}

enum class Rules { Original, Closed187, Open187 };

void make_world(SimulationEngine& sim, Rules r) {
    make_world(sim, r == Rules::Original ? GameMode::HighestScore : GameMode::Kills187, r == Rules::Open187);
}

const Rules kAllRules[] = {Rules::Original, Rules::Closed187, Rules::Open187};

Command click(uint8_t issuer, CommandType type, int16_t x, int16_t y, std::vector<uint32_t> ants) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = std::move(ants);
    return c;
}

void run_ticks(SimulationEngine& sim, int n) {
    for (int i = 0; i < n; ++i) sim.tick();
}

bool ever_entered(SimulationEngine& sim, uint32_t id, int ticks) {
    for (int t = 0; t < ticks; ++t) {
        sim.tick();
        if (sim.get_unit(id).state == UnitState::EnteringBase) return true;
    }
    return false;
}

}  // namespace

int main() {
    std::cout << "=== Closed hill of game mode 187 ===\n";

    TEST_CASE("1.1 the engine says whether an ant can enter its hill: always in the original's game, never in 187 now, again in 187 with the earlier rule; the switch is kept by init() like the mode") {
        SimulationEngine sim;
        ASSERT_TRUE(sim.own_hill_enterable());                                              // (the original's game, nothing set)
        sim.set_game_mode(GameMode::Kills187);
        ASSERT_FALSE(sim.own_hill_enterable());                                             // 187 is closed by default: a match that is played now
        sim.set_hill_open_187(true);
        ASSERT_TRUE(sim.own_hill_enterable());
        sim.init_test_world(20, 20, 3, 60000);
        ASSERT_TRUE(sim.own_hill_enterable() && sim.game_mode() == GameMode::Kills187);     // (init keeps it)
        sim.set_game_mode(GameMode::Kills187);
        ASSERT_FALSE(sim.own_hill_enterable());                                             // (a program that watched an old recording and starts a match sets the mode again: the rules of a match played now)
        sim.set_hill_open_187(true);
        sim.set_hill_open_187(false);
        ASSERT_FALSE(sim.own_hill_enterable());
        SimulationEngine original;
        original.set_hill_open_187(false);
        ASSERT_TRUE(original.own_hill_enterable());                                         // (the original's game never reads the switch)
        original.set_hill_open_187(true);
        ASSERT_TRUE(original.own_hill_enterable());
    } TEST_END();

    TEST_CASE("1.2 the state hash: the switch changes the hash of a 187 match and nothing else; the original's game has the hash it had whatever the switch says; an engine copy keeps the switch") {
        SimulationEngine original_a, original_b, closed, open;
        make_world(original_a, GameMode::HighestScore, false);
        make_world(original_b, GameMode::HighestScore, true);
        make_world(closed, GameMode::Kills187, false);
        make_world(open, GameMode::Kills187, true);
        ASSERT_TRUE(original_a.state_hash().total == original_b.state_hash().total);        // (mode 0: the switch is no state)
        ASSERT_TRUE(closed.state_hash().total != open.state_hash().total);                 // (a machine of the earlier rule is noticed at the first check)
        ASSERT_TRUE(closed.state_hash().total != original_a.state_hash().total && open.state_hash().total != original_a.state_hash().total);
        run_ticks(original_a, 40);
        run_ticks(original_b, 40);
        ASSERT_TRUE(original_a.state_hash().total == original_b.state_hash().total);
        const SimulationEngine copy_of_closed(closed);
        const SimulationEngine copy_of_open(open);
        ASSERT_FALSE(copy_of_closed.own_hill_enterable());
        ASSERT_TRUE(copy_of_open.own_hill_enterable());
        ASSERT_TRUE(copy_of_closed.state_hash().total == closed.state_hash().total && copy_of_open.state_hash().total == open.state_hash().total);
    } TEST_END();

    TEST_CASE("2.1 a click on the own hill: the original's game and the earlier rule of 187 send the ant in and heal it; 187 now refuses it with \"Can't go there.\", the can't-go clip, no entering and no healing") {
        for (const Rules r : kAllRules) {
            SimulationEngine sim;
            make_world(sim, r);
            const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{24, 24});
            sim.get_unit(id).hp = 4;
            sim.clear_news_events();
            const CommandResult res = sim.apply_command(click(0, CommandType::GroupMove, 21, 21, {id}));
            static_cast<void>(res);
            if (r == Rules::Closed187) {
                ASSERT_TRUE(sim.has_news_event(0, strings::kCantGoThere));
                ASSERT_TRUE(sim.get_unit(id).state == UnitState::CantGo);
                ASSERT_FALSE(ever_entered(sim, id, 400));
                ASSERT_EQ(sim.get_unit(id).hp, 4);                                           // nothing healed it
                ASSERT_EQ(sim.get_unit(id).pixel_x / 32, 24);                                // and it did not move
                ASSERT_EQ(sim.get_unit(id).pixel_y / 32, 24);
                ASSERT_EQ(sim.get_base_queue_size(0), 0u);
            } else {
                ASSERT_FALSE(sim.has_news_event(0, strings::kCantGoThere));
                ASSERT_TRUE(ever_entered(sim, id, 400));
                run_ticks(sim, 120);
                ASSERT_EQ(sim.get_unit(id).hp, 10);                                          // the heal of the enter clip
            }
        }
    } TEST_END();

    TEST_CASE("2.2 every tile of the 4 x 4 mound is the hill, for a click and for a group: all sixteen are refused in 187 now, a tile beside the mound is an ordinary goal") {
        SimulationEngine sim;
        make_world(sim, Rules::Closed187);
        std::vector<uint32_t> ants;
        for (int i = 0; i < 4; ++i) ants.push_back(sim.spawn_unit(0, AntType::Worker, TileCoord{26 + i, 26}));
        int refused = 0;
        for (int dy = 0; dy < 4; ++dy) {
            for (int dx = 0; dx < 4; ++dx) {
                SimulationEngine one;
                make_world(one, Rules::Closed187);
                const uint32_t id = one.spawn_unit(0, AntType::Worker, TileCoord{26, 26});
                one.clear_news_events();
                one.apply_command(click(0, CommandType::GroupMove, static_cast<int16_t>(20 + dx), static_cast<int16_t>(20 + dy), {id}));
                if (one.has_news_event(0, strings::kCantGoThere) && one.get_unit(id).state == UnitState::CantGo) ++refused;
            }
        }
        ASSERT_EQ(refused, 16);
        sim.clear_news_events();
        sim.apply_command(click(0, CommandType::GroupMove, 22, 22, ants));                   // a group of four: every ant that takes the order is refused
        for (const uint32_t id : ants) ASSERT_TRUE(sim.get_unit(id).state == UnitState::CantGo);
        SimulationEngine beside;
        make_world(beside, Rules::Closed187);
        const uint32_t id = beside.spawn_unit(0, AntType::Worker, TileCoord{26, 26});
        beside.apply_command(click(0, CommandType::GroupMove, 24, 24, {id}));                // tile42, beside the mound
        ASSERT_FALSE(beside.has_news_event(0, strings::kCantGoThere));
        ASSERT_TRUE(beside.get_unit(id).state == UnitState::Walking);
    } TEST_END();

    TEST_CASE("2.3 the refused ant is whole again for the next order: the can't-go clip ends and it takes an order to another tile; the enemy's hill is not the own hill (a worker's click on it only stops, as before)") {
        SimulationEngine sim;
        make_world(sim, Rules::Closed187);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{24, 24});
        sim.apply_command(click(0, CommandType::GroupMove, 21, 21, {id}));
        ASSERT_TRUE(sim.get_unit(id).state == UnitState::CantGo);
        run_ticks(sim, 60);
        ASSERT_TRUE(sim.get_unit(id).state != UnitState::CantGo);
        sim.clear_news_events();
        sim.apply_command(click(0, CommandType::GroupMove, 30, 30, {id}));
        ASSERT_TRUE(sim.get_unit(id).state == UnitState::Walking);
        run_ticks(sim, 300);
        ASSERT_EQ(sim.get_unit(id).pixel_x / 32, 30);
        // a click on the other team's hill: the engine stops the worker and says nothing, in every rule (this is not the own hill)
        const uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{28, 28});
        sim.clear_news_events();
        sim.apply_command(click(0, CommandType::GroupMove, 41, 41, {w2}));
        ASSERT_FALSE(sim.has_news_event(0, strings::kCantGoThere));
        ASSERT_TRUE(sim.get_unit(w2).state != UnitState::Walking && sim.get_unit(w2).state != UnitState::CantGo);
    } TEST_END();

    TEST_CASE("2.4 the home order (OrderType::ReturnToBase) is refused in 187 now (false, \"Can't go there.\") and accepted in the original's game and in the earlier rule; join_base_queue queues nobody") {
        for (const Rules r : kAllRules) {
            SimulationEngine sim;
            make_world(sim, r);
            const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{24, 24});
            AntOrder o{};
            o.ant_id = id;
            o.type = OrderType::ReturnToBase;
            sim.clear_news_events();
            const bool taken = sim.issue_order(o);
            if (r == Rules::Closed187) {
                ASSERT_FALSE(taken);
                ASSERT_TRUE(sim.has_news_event(0, strings::kCantGoThere));
                ASSERT_FALSE(sim.is_ant_in_base_queue(id));
            } else {
                ASSERT_TRUE(taken);
                ASSERT_FALSE(sim.has_news_event(0, strings::kCantGoThere));
            }
        }
    } TEST_END();

    TEST_CASE("2.5 the engine's guess at who acknowledges a click (predict_order_ack) is nobody for a click on the own hill in 187 now, the closest ant otherwise") {
        for (const Rules r : kAllRules) {
            SimulationEngine sim;
            make_world(sim, r);
            const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{24, 24});
            uint32_t needed = 0;
            const uint32_t ack = sim.predict_order_ack(click(0, CommandType::GroupMove, 21, 21, {id}), &needed);
            ASSERT_EQ(ack, r == Rules::Closed187 ? 0u : id);
            ASSERT_EQ(sim.predict_order_ack(click(0, CommandType::GroupMove, 30, 30, {id}), &needed), id);   // (an ordinary tile is acknowledged in every rule)
        }
    } TEST_END();

    TEST_CASE("2.6 an ant that stands next to the hill gets nothing from it: a hurt ant stays hurt for 3000 ticks in 187 now, and nothing sends it in by itself in the earlier rule either (only an order or the 1 hp retreat does)") {
        for (const Rules r : {Rules::Closed187, Rules::Open187}) {
            SimulationEngine sim;
            make_world(sim, r);
            const uint32_t id = sim.spawn_unit(0, AntType::Combat, TileCoord{24, 24});
            sim.get_unit(id).hp = 5;
            run_ticks(sim, 3000);
            ASSERT_EQ(sim.get_unit(id).hp, 5);                                               // (nothing sends it in by itself in the earlier rule either: only an order or a retreat does)
            ASSERT_FALSE(sim.is_ant_in_base_queue(id));
        }
    } TEST_END();

    TEST_CASE("3.1 an ant left with one hit point: the earlier rule walks it home by itself and heals it, 187 now leaves it where it stands (stunned like any hurt ant, then idle), at 1 hp and with no order") {
        for (const Rules r : {Rules::Closed187, Rules::Open187, Rules::Original}) {
            SimulationEngine sim;
            make_world(sim, r);
            const uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
            const uint32_t striker = sim.spawn_unit(1, AntType::Worker, TileCoord{31, 30});
            sim.get_unit(victim).hp = 2;
            sim.execute_melee_attack(striker, victim);                                       // a worker's blow takes 1 hit point
            bool went_home = false;
            bool queued = false;
            for (int t = 0; t < 700; ++t) {
                sim.tick();
                if (sim.get_unit(victim).state == UnitState::EnteringBase) went_home = true;
                if (sim.is_ant_in_base_queue(victim)) queued = true;
            }
            const AntUnit& u = sim.get_unit(victim);
            if (r == Rules::Closed187) {
                ASSERT_FALSE(went_home || queued);
                ASSERT_EQ(u.hp, 1);
                ASSERT_TRUE(u.is_alive());
                ASSERT_TRUE(u.waypoints.empty() && u.orig_order == AntUnit::kOrderNone);
                ASSERT_TRUE(u.pixel_x / 32 < 40 && u.pixel_x / 32 > 25 && u.pixel_y / 32 == 30);    // (thrown a tile by the blow, then still)
                ASSERT_TRUE(u.state == UnitState::Idle);
            } else {
                ASSERT_TRUE(went_home);                                                      // (the original's retreat, in the original's game and in the earlier rule of 187)
                ASSERT_EQ(u.hp, 10);
            }
        }
    } TEST_END();

    TEST_CASE("3.2 a second blow still kills the ant that was left with one hit point (no hill to hide in), and the kill counts") {
        SimulationEngine sim;
        make_world(sim, Rules::Closed187);
        const uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
        const uint32_t striker = sim.spawn_unit(1, AntType::Worker, TileCoord{31, 30});
        sim.get_unit(victim).hp = 2;
        sim.execute_melee_attack(striker, victim);
        run_ticks(sim, 80);
        ASSERT_EQ(sim.get_unit(victim).hp, 1);
        const TileCoord at{sim.get_unit(victim).pixel_x / 32, sim.get_unit(victim).pixel_y / 32};
        const uint32_t second = sim.spawn_unit(1, AntType::Worker, TileCoord{at.x + 1, at.y});
        sim.execute_melee_attack(second, victim);
        run_ticks(sim, 120);
        ASSERT_TRUE(sim.get_unit(victim).removed);
        ASSERT_EQ(sim.get_player_score(1), 1);
        ASSERT_EQ(sim.get_player_score(0), 0);
    } TEST_END();

    TEST_CASE("3.3 the waiting ring is idle in 187 now: the task still keeps its beat (the timer is in the state hash), an ant that stands on the ring tile queues nobody, the earlier rule queues it") {
        for (const Rules r : {Rules::Closed187, Rules::Open187}) {
            SimulationEngine sim;
            make_world(sim, r);
            const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 23});     // the ring tile
            sim.get_unit(id).home_state = 2;                                                // (queued at the ring, as the earlier rule leaves an ant that waits)
            sim.get_unit(id).home_time_ms = 0;
            const bool entered = ever_entered(sim, id, 100);
            ASSERT_EQ(entered, r == Rules::Open187);
        }
    } TEST_END();

    TEST_CASE("4.1 hatching is as it was: the egg hatches 8 seconds after the click on the entrance tile, the newborn plays the hatch clip, walks to tile42, costs no kills, and was never healed or counted twice") {
        SimulationEngine sim;
        make_world(sim, Rules::Closed187);
        sim.set_player_eggs(0, 1);
        sim.set_player_score(0, 0);
        sim.get_world_state();
        ASSERT_EQ(static_cast<int>(sim.try_hatch(0, AntType::Worker, true)), static_cast<int>(SimulationEngine::HatchResult::Started));
        int appeared = -1;
        for (int t = 0; t < 260 && appeared < 0; ++t) {
            sim.tick();
            if (sim.get_world_state().ants.size() == 1) appeared = t + 1;
        }
        ASSERT_TRUE(appeared > 0);
        ASSERT_NEAR(appeared * 50, 8000, 150);
        ASSERT_EQ(sim.get_player_score(0), 0);                                               // a hatch costs 187 no kills
        const uint32_t id = sim.get_world_state().ants[0].id;
        run_ticks(sim, 200);
        ASSERT_EQ(sim.get_unit(id).pixel_x / 32, 24);                                        // the newborn left the entrance for tile42, as in every rule
        ASSERT_EQ(sim.get_unit(id).pixel_y / 32, 24);
        ASSERT_EQ(sim.get_unit(id).hp, 10);
        ASSERT_FALSE(sim.has_news_event(0, strings::kCantGoThere));
    } TEST_END();

    TEST_CASE("4.2 an ant that stands on the entrance heals nothing and is no refusal: it is an ordinary ant that holds the door (a hatch waits for it) and walks out on an ordinary order") {
        SimulationEngine sim;
        make_world(sim, Rules::Closed187);
        sim.set_player_eggs(0, 1);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 21});
        sim.get_unit(id).hp = 3;
        ASSERT_EQ(static_cast<int>(sim.try_hatch(0, AntType::Worker, true)), static_cast<int>(SimulationEngine::HatchResult::Started));
        run_ticks(sim, 260);                                                                 // 13 s: the hatch has looked again and again, and found the door taken
        ASSERT_EQ(sim.get_world_state().ants.size(), 1u);
        ASSERT_EQ(sim.get_unit(id).hp, 3);
        sim.clear_news_events();
        sim.apply_command(click(0, CommandType::GroupMove, 26, 26, {id}));
        ASSERT_FALSE(sim.has_news_event(0, strings::kCantGoThere));
        run_ticks(sim, 200);
        ASSERT_EQ(sim.get_world_state().ants.size(), 2u);                                    // it walked out, and the egg hatched at the next look
        ASSERT_EQ(sim.get_unit(id).hp, 3);
    } TEST_END();

    TEST_CASE("4.3 the end rule does not know the hill: an ant that stands on its entrance is an ant of its team (the match goes on, and it is counted for the page of the results); walked out and killed, it ends the match") {
        SimulationEngine sim;
        make_world(sim, Rules::Closed187);
        for (uint8_t p = 0; p < MAX_PLAYERS; ++p) sim.set_player_eggs(p, 0);
        const uint32_t a = sim.spawn_unit(0, AntType::Combat, TileCoord{22, 22});
        const uint32_t b = sim.spawn_unit(1, AntType::Combat, TileCoord{41, 41});          // on the entrance of team 1
        run_ticks(sim, 60);
        ASSERT_FALSE(sim.is_match_over());
        ASSERT_EQ(sim.get_world_state().match_result.ants_left[1], 1u);
        sim.apply_command(click(1, CommandType::GroupMove, 46, 46, {b}));
        run_ticks(sim, 200);
        ASSERT_FALSE(sim.is_match_over());
        sim.get_unit(b).hp = 1;
        const TileCoord at{sim.get_unit(b).pixel_x / 32, sim.get_unit(b).pixel_y / 32};
        ASSERT_EQ(at.x, 46);
        const uint32_t killer = sim.spawn_unit(0, AntType::Combat, TileCoord{at.x - 1, at.y});
        sim.execute_melee_attack(killer, b);
        run_ticks(sim, 140);
        static_cast<void>(a);
        ASSERT_TRUE(sim.get_unit(b).removed);
        ASSERT_TRUE(sim.is_match_over());
        ASSERT_EQ(sim.get_world_state().match_result.standing, 0);
    } TEST_END();

    TEST_CASE("4.4 a thief's click on an enemy hill is refused in 187 now (a raid would take kills from the other team that nobody could bank); the original's game and the earlier rule raid as before") {
        for (const Rules r : kAllRules) {
            SimulationEngine sim;
            make_world(sim, r);
            sim.set_player_score(1, 30);
            const uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42}, true);
            sim.clear_news_events();
            sim.apply_command(click(0, CommandType::GroupSpecial, 41, 41, {thief}));
            ASSERT_EQ(sim.is_special_target_valid(AntType::Thief, TileCoord{41, 41}, false, 0), r != Rules::Closed187);      // (the thief's cursor on the enemy hill)
            ASSERT_FALSE(sim.is_special_target_valid(AntType::Thief, TileCoord{21, 21}, false, 0));                         // (its own hill is no raid in any rule)
            if (r == Rules::Closed187) {
                ASSERT_TRUE(sim.has_news_event(0, strings::kCantGoThere));
                ASSERT_TRUE(sim.get_unit(thief).state == UnitState::CantGo);
                run_ticks(sim, 200);
                ASSERT_EQ(sim.get_player_score(1), 30);
                ASSERT_FALSE(sim.get_unit(thief).is_holding());
            } else {
                ASSERT_FALSE(sim.has_news_event(0, strings::kCantGoThere));
                run_ticks(sim, 200);
                ASSERT_EQ(sim.get_player_score(1), 0);                                       // the loot of 30 left the victim
            }
        }
    } TEST_END();

    TEST_CASE("4.5 there is no food in 187, so no ant carries any: a carrier that the test makes is not healed or paid by a hill it cannot enter, and the original's game banks it") {
        for (const Rules r : {Rules::Original, Rules::Closed187}) {
            SimulationEngine sim;
            make_world(sim, r);
            const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{24, 24});
            sim.get_unit(id).pick_up_food(1, 25);
            sim.apply_command(click(0, CommandType::GroupMove, 21, 21, {id}));
            run_ticks(sim, 400);
            ASSERT_EQ(sim.get_player_score(0), r == Rules::Original ? 25 : 0);
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
