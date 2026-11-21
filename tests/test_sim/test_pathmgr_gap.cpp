// The gap before the path manager's answer (rules number 14 of the original's game, 15 of 187; docs/GAME_REVERSE_ENGINEERING.md 5.40, "The gap before the path manager's answer"): the original's PATHMGR task has a delay of 0 and
// a period of 50 ms, so its runs fall on the boundaries of the turns, and the clicks that wait are handled before a run; an ant that a GoTo has just sent (the end of a can't-go clip, of a raid clip) is idle with no path until
// the next run and takes an order. The engine gives the refusal ("no path") of a run that ends a tick at the start of the next tick, after the turn's commands, so that the commands of a turn find such an ant; a path that
// is found is given at once, as always (the ant stands idle for 250 ms, so an order takes it either way). With the refusal given at once (the rules numbers before 14) no command meets the ant.
//
//   PG1.1  a thief with loot whose way home is shut off: the order that is given at the boundary after the can't-go clip's end is taken (one boundary of the 18 of a 900 ms cycle), and the thief walks to a reachable tile and stays there
//   PG1.2  an ant in the can't-go clip refuses the order at every other boundary of the cycle, and the refusal changes nothing (the state hash, the clip's end)
//   PG1.3  the loop of the rules before 14: no boundary of any cycle takes an order, the thief goes on in the clip and the request again and again
//   PG1.4  a path that is found is given at the end of the tick in both rules, and the walk begins at the same time
//   PG1.5  a request that needs more than one slice has a gap of more than one boundary, and one that a slice answers has one
//   PG1.7  the refusal of an order that a command gave is given at once, as it always was: the rule is for what an event asked
//   PG1.6  the engine copy and the rule: a copy made in the gap keeps the rule and the request, and plays on to the same hash as the engine it was made from; the setting is kept by init() and set_game_mode puts it back
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
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
    std::cout << "  RUNNING: " << std::left << std::setw(72) << name << " ... " << std::flush;
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

constexpr TileCoord kHome0{20, 20};            // team 0's hill: mound 20..23, entrance (21, 21), tile42 (24, 24)
constexpr TileCoord kHome1{40, 40};            // team 1's hill: mound 40..43, raid tile (43, 42)
constexpr TileCoord kReachable{47, 42};        // a tile of the thief's own pocket, east of the raid tile

// The ring of rocks at Chebyshev distance `radius` from `centre`: what it holds is shut off from the rest of the map
void ring(SimulationEngine& sim, TileCoord centre, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) == radius) sim.set_terrain(centre.x + dx, centre.y + dy, TERRAIN_OBSTACLE);
        }
    }
}

// Team 0's thief has raided team 1's hill and holds the loot; team 0's hill (the entrance and tile42, the two goals of the loop) is in a ring of rocks, and the thief's own pocket (289 tiles) is shut off from the
// rest of the map, so that the first slice of a search answers: the way home is refused, and the refusal comes at the first run of the manager after the request.
struct Loop {
    SimulationEngine sim;
    uint32_t thief{0};
    explicit Loop(bool at_tick_start = true, uint32_t seed = 11) {
        sim.init_test_world(60, 60, seed, 720000);
        sim.set_path_answers_after_commands(at_tick_start);
        sim.set_anthill(0, kHome0);
        sim.set_anthill(1, kHome1);
        sim.set_player_score(0, 0);
        sim.set_player_score(1, 120);
        ring(sim, TileCoord{22, 22}, 6);
        ring(sim, TileCoord{43, 43}, 9);
        thief = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
        sim.start_thief_infiltration(thief, 1);
    }
    const AntUnit& unit() { return sim.get_unit(thief); }
    // Ticks until the loot has moved to the thief and the raid clip is over (the thief is no longer infiltrating)
    void run_to_raid_end() {
        for (int t = 0; t < 200 && unit().state == UnitState::Infiltrating; ++t) sim.tick();
    }
};

Command move_order(uint32_t ant, TileCoord to) {
    Command c;
    c.type = CommandType::GroupMove;
    c.issuer = 0;
    c.tile_x = static_cast<int16_t>(to.x);
    c.tile_y = static_cast<int16_t>(to.y);
    c.ants = {ant};
    return c;
}

// Whether the thief takes a move order now: asked of a copy of the engine, so that nothing happens to the engine itself
bool takes_order(const SimulationEngine& sim, uint32_t thief) {
    SimulationEngine probe(sim);
    return probe.apply_command(move_order(thief, kReachable)).ack_ant == thief;
}

}  // namespace

int main() {
    std::cout << "=== The gap before the path manager's answer ===\n";

    TEST_CASE("PG1.1 A thief with loot, home shut off: the order at the boundary after the can't-go clip is taken, and the thief walks to a reachable tile") {
        Loop w;
        w.run_to_raid_end();
        ASSERT_TRUE(w.unit().is_holding());
        ASSERT_EQ(w.unit().carried_points, 50u);
        // The loop: the boundaries (the starts of ticks, where the commands of a turn are applied) at which the thief takes an order, over four cycles of the clip
        std::vector<int> open;
        for (int tick = 0; tick < 4 * 18 + 6; ++tick) {
            if (takes_order(w.sim, w.thief)) {
                ASSERT_TRUE(w.sim.has_pending_path(w.thief));                               // (asked and not answered: a bot sees the request that is still queued)
                open.push_back(tick);
            }
            w.sim.tick();
        }
        ASSERT_TRUE(open.size() >= 4);
        for (size_t i = 1; i < open.size(); ++i) ASSERT_EQ(open[i] - open[i - 1], 18);        // one boundary in every cycle of 900 ms (the can't-go clip's 890 ms and the manager's run after it)
        // The order itself: given at such a boundary it is taken, the path is answered at the start of its tick, the thief walks away from the raid tile and the loop is over
        Loop v;
        v.run_to_raid_end();
        for (int tick = 0; tick < open[1] + 1; ++tick) {
            if (tick == open[1]) {
                ASSERT_TRUE(v.sim.apply_command(move_order(v.thief, kReachable)).ack_ant == v.thief);
                ASSERT_TRUE(v.sim.has_pending_path(v.thief));                               // the request of the order replaced the one of the clip's end (the first with the ant's number goes)
            }
            v.sim.tick();
        }
        v.sim.tick();
        ASSERT_TRUE(!v.sim.has_pending_path(v.thief));                                      // the path is found by the search at the end of the tick and given at once
        ASSERT_TRUE(v.unit().state != UnitState::CantGo);
        for (int t = 0; t < 400; ++t) v.sim.tick();
        ASSERT_EQ(v.unit().pos.x, kReachable.x);
        ASSERT_EQ(v.unit().pos.y, kReachable.y);
        ASSERT_TRUE(v.unit().state == UnitState::Idle);
        ASSERT_TRUE(v.unit().is_holding());                                                 // the order is a plain move: the loot is still carried
        ASSERT_TRUE(!v.sim.has_pending_path(v.thief));
    } TEST_END();

    TEST_CASE("PG1.2 An ant in the can't-go clip refuses the order at every other boundary of the cycle, and the refusal changes nothing") {
        Loop w;
        w.run_to_raid_end();
        int refused_in_clip = 0;
        int open_boundaries = 0;
        for (int tick = 0; tick < 3 * 18 + 6; ++tick) {
            const bool takes = takes_order(w.sim, w.thief);
            if (takes) ++open_boundaries;
            if (w.unit().state == UnitState::CantGo) {
                ASSERT_TRUE(!takes);                                                        // the clip refuses (action 0xb is not 0, 1 or 3)
                SimulationEngine real(w.sim);                                              // and the order that is really given leaves everything as it was
                const StateHash before = real.state_hash();
                const CommandResult r = real.apply_command(move_order(w.thief, kReachable));
                ASSERT_EQ(r.ack_ant, 0u);
                ASSERT_TRUE(real.state_hash().total == before.total);
                ++refused_in_clip;
            }
            w.sim.tick();
        }
        ASSERT_TRUE(refused_in_clip >= 3 * 17);                                             // 17 of the 18 boundaries of a cycle are in the clip
        ASSERT_TRUE(open_boundaries >= 3 && open_boundaries <= 5);                          // and one is not
    } TEST_END();

    TEST_CASE("PG1.3 The rules before 14 (the manager at the end of the tick): no boundary takes an order, the loop goes on in the clip") {
        Loop w(false);
        w.run_to_raid_end();
        ASSERT_TRUE(w.unit().is_holding());
        int refusals = 0;
        w.sim.clear_news_events();
        for (int tick = 0; tick < 6 * 18; ++tick) {
            ASSERT_TRUE(!takes_order(w.sim, w.thief));
            ASSERT_TRUE(w.unit().state == UnitState::CantGo || tick == 0);               // the clip's end and the next refusal are in one tick: the ant is never seen between them
            w.sim.tick();
            if (w.sim.has_news_event(0, strings::kCantGoThere)) ++refusals;              // "Can't go there.": the manager answered a request with no path
            w.sim.clear_news_events();
        }
        ASSERT_TRUE(refusals >= 5);                                                         // a new refusal and a new clip about every 900 ms
        ASSERT_EQ(w.unit().pos.x, 43);                                                      // and the thief has not left the raid tile
        ASSERT_EQ(w.unit().pos.y, 42);
    } TEST_END();

    TEST_CASE("PG1.4 A path that is found is given at the end of the tick in both rules, and the walk begins at the same time: the first step comes at 300 ms") {
        for (const bool at_start : {true, false}) {
            SimulationEngine sim;
            sim.init_test_world(40, 40, 3, 720000);
            sim.set_path_answers_after_commands(at_start);
            sim.set_anthill(0, TileCoord{2, 2});
            sim.set_anthill(1, TileCoord{34, 34});
            const uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 20});
            const int32_t x0 = sim.get_unit(worker).pixel_x;
            ASSERT_TRUE(sim.apply_command(move_order(worker, TileCoord{13, 20})).ack_ant == worker);
            ASSERT_TRUE(sim.has_pending_path(worker));
            int first_move_tick = -1;
            for (int tick = 1; tick <= 12 && first_move_tick < 0; ++tick) {
                sim.tick();
                ASSERT_TRUE(!sim.has_pending_path(worker));                                // the path is found at the end of the first tick and given at once, in both rules
                if (sim.get_unit(worker).pixel_x != x0) first_move_tick = tick;
            }
            ASSERT_EQ(first_move_tick, 6);                                                 // 250 ms after the answer at 50 ms (the tick that holds the instant 300 ms is the sixth), in both rules
        }
    } TEST_END();

    TEST_CASE("PG1.5 A request that one slice answers keeps the gap open for one boundary, and one that needs more slices for as many boundaries") {
        // The thief's pocket of PG1.1 is 289 tiles: one slice answers. A world without the pocket's ring has about 3,000 reachable tiles: the search takes four slices (four runs, 150 ms later than the first).
        Loop shut;
        shut.run_to_raid_end();
        int shut_open_run = 0, shut_best = 0;
        for (int tick = 0; tick < 18; ++tick) {
            const bool t = takes_order(shut.sim, shut.thief);
            shut_open_run = t ? shut_open_run + 1 : 0;
            shut_best = std::max(shut_best, shut_open_run);
            shut.sim.tick();
        }
        ASSERT_EQ(shut_best, 1);
        SimulationEngine big;
        big.init_test_world(60, 60, 11, 720000);
        big.set_anthill(0, kHome0);
        big.set_anthill(1, kHome1);
        big.set_player_score(0, 0);
        big.set_player_score(1, 120);
        ring(big, TileCoord{22, 22}, 6);                                                     // the way home is shut off, the thief's side of the map is open (3,000 tiles)
        const uint32_t thief = big.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
        big.start_thief_infiltration(thief, 1);
        for (int t = 0; t < 200 && big.get_unit(thief).state == UnitState::Infiltrating; ++t) big.tick();
        int best = 0, run = 0;
        for (int tick = 0; tick < 60; ++tick) {
            const bool t = takes_order(big, thief);
            run = t ? run + 1 : 0;
            best = std::max(best, run);
            big.tick();
        }
        ASSERT_TRUE(best >= 3);                                                              // one boundary for each run that the search waits for: the original's gap is the same, 50 ms longer for every slice
    } TEST_END();

    TEST_CASE("PG1.6 A copy made in the gap keeps the rule and the request and plays on to the same hash; init keeps the setting and set_game_mode puts it back") {
        Loop w;
        w.run_to_raid_end();
        int gap_tick = -1;
        for (int tick = 0; tick < 40 && gap_tick < 0; ++tick) {
            if (takes_order(w.sim, w.thief)) gap_tick = tick;
            else w.sim.tick();
        }
        ASSERT_TRUE(gap_tick >= 0);
        SimulationEngine copy(w.sim);
        ASSERT_TRUE(copy.path_answers_after_commands());
        ASSERT_TRUE(copy.has_pending_path(w.thief) == w.sim.has_pending_path(w.thief));
        ASSERT_TRUE(copy.state_hash().total == w.sim.state_hash().total);
        ASSERT_TRUE(copy.apply_command(move_order(w.thief, kReachable)).ack_ant == w.thief);
        ASSERT_TRUE(w.sim.apply_command(move_order(w.thief, kReachable)).ack_ant == w.thief);
        for (int t = 0; t < 100; ++t) {
            copy.tick();
            w.sim.tick();
            ASSERT_TRUE(copy.state_hash().total == w.sim.state_hash().total);
        }
        SimulationEngine fresh;
        ASSERT_TRUE(fresh.path_answers_after_commands());                                    // (an engine that nobody set up plays the rules of now)
        SimulationEngine old_rules;
        old_rules.set_game_mode(GameMode::HighestScore);
        ASSERT_TRUE(old_rules.path_answers_after_commands());                                      // the rule of a match played now
        old_rules.set_path_answers_after_commands(false);
        SimulationEngine old_copy(old_rules);
        ASSERT_TRUE(!old_copy.path_answers_after_commands());                                      // a copy keeps the number's rule
        old_rules.set_game_mode(GameMode::Kills187);
        ASSERT_TRUE(old_rules.path_answers_after_commands());                                      // set_game_mode puts the setting back, as it does every rule
    } TEST_END();

    TEST_CASE("PG1.7 The refusal of an order that a command gave is given at once: the can't-go clip shows after the tick, as it always did") {
        SimulationEngine sim;
        sim.init_test_world(30, 30, 5, 720000);
        sim.set_anthill(0, TileCoord{2, 2});
        sim.set_anthill(1, TileCoord{24, 24});
        ring(sim, TileCoord{15, 15}, 2);                                                    // a tile inside a closed ring: no walk reaches it
        const uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{8, 8});
        ASSERT_TRUE(sim.apply_command(move_order(worker, TileCoord{15, 15})).ack_ant == worker);
        sim.tick();
        ASSERT_TRUE(!sim.has_pending_path(worker));
        ASSERT_TRUE(sim.get_unit(worker).state == UnitState::CantGo);                       // at the end of the first tick, the path manager's first run
        ASSERT_TRUE(sim.has_news_event(0, strings::kCantGoThere));
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Failures:         " << g_test_failures << "\n"
              << "=======================================================\n";
    if (g_test_failures == 0) {
        std::cout << " >>> ALL PATH MANAGER GAP TESTS PASSED CLEANLY <<<\n";
        return 0;
    }
    std::cout << " >>> " << g_test_failures << " TEST(S) FAILED <<<\n";
    return 1;
}
