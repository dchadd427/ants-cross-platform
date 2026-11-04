// Tests of the last side standing in the original's game (docs/GAMEPLAY.md, "The last side standing"; rules number 8, replay.hpp): when exactly one side still has an ant, an egg or a hatch in progress and
// no food is left that could score, the match ends and that side wins, whatever the scores are. The original's CHECKGO alone (rules number 5, SimulationEngine::set_last_side_rule(false)) plays such a match
// on to the clock; game type 187 has its own end rule and never reads this one.
//   1.x  the switch: what the engine says about the rule, set_game_mode and init, the state hash
//   2.x  who is alive: the lone survivor, an enemy egg or hatch, nobody, two sides
//   3.x  what keeps the match alive: food on the ground, a lunchbox, food carried, a bite in progress; what does not: a unit worth nothing, power-ups, bombs, fire
//   4.x  sides: allies, a side with a dead seat, a team that dropped out
//   5.x  who wins and what the results say: ties, a score of 0, a dead team ahead, the clock
//   6.x  one team in the match, 187 unchanged, the same match twice and a copy of the engine
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/game_mode.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "../common/test_assert.hpp"

using namespace ants::sim;
using ants::assets::LevelData;

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

constexpr uint16_t kGone = 0x7FFE;
constexpr int kLong = 600;      // ticks (30 s) that a match is watched when it must not end

// Four teams in a 60 x 60 world (every team has 10 eggs, so every team is alive until its eggs are taken away); hills for teams 0 and 1.
void make_world(SimulationEngine& sim, GameMode mode = GameMode::HighestScore, bool rule = true, uint32_t clock_ms = 720000) {
    sim.set_game_mode(mode);
    sim.set_last_side_rule(rule);                          // (after the mode: set_game_mode sets the rule of a match played now)
    sim.init_test_world(60, 60, 7, clock_ms);
    sim.set_anthill(0, TileCoord{2, 2});
    sim.set_anthill(1, TileCoord{50, 50});
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) sim.set_player_score(p, 0);
}

// Takes the eggs of every team away and gives one ant to each of `seats`: they are the teams that are alive. Returns the id of the first ant.
uint32_t only_alive(SimulationEngine& sim, const std::vector<uint8_t>& seats) {
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) sim.set_player_eggs(p, 0);
    uint32_t first = 0;
    int32_t x = 20;
    for (const uint8_t s : seats) {
        const uint32_t id = sim.spawn_unit(s, AntType::Worker, TileCoord{x, 20});
        if (first == 0) first = id;
        x += 3;
    }
    return first;
}

int32_t place_pile(SimulationEngine& sim, int32_t col, int32_t row, uint16_t units, uint16_t value) {
    FoodObject o;
    o.row = static_cast<uint16_t>(row);
    o.col = static_cast<uint16_t>(col);
    o.units = units;
    o.value = value;
    o.remaining = units;
    o.thresholds = {units, 0};
    o.stage_tiles = {369, kGone};
    return sim.grid_mut().add_food_object(std::move(o));
}

void take_units(SimulationEngine& sim, int32_t pile, uint16_t units) {
    bool changed = false;
    sim.grid_mut().take_food(pile, units, changed);
}

// The tick (counted from 1) after which the match is over, or -1 when it is not over after `max_ticks`
int ticks_to_end(SimulationEngine& sim, int max_ticks) {
    for (int t = 1; t <= max_ticks; ++t) {
        sim.tick();
        sim.clear_audio_events();
        sim.clear_news_events();
        if (sim.is_match_over()) return t;
    }
    return -1;
}

bool survives(SimulationEngine& sim, int ticks = kLong) { return ticks_to_end(sim, ticks) < 0; }

const MatchResult& result_of(const SimulationEngine& sim) { return sim.get_world_state().match_result; }

// The seats of the top row of the results as team `local` would see them
std::vector<uint8_t> top_row(const MatchResult& r, uint8_t local) {
    const std::vector<ResultRow> rows = r.rows(local);
    std::vector<uint8_t> seats;
    if (rows.empty()) return seats;
    seats.push_back(rows[0].first);
    if (rows[0].has_second()) seats.push_back(rows[0].second);
    return seats;
}

using Seats = std::vector<uint8_t>;

}  // namespace

int main() {
    std::cout << "=== The last side standing (the original's game) ===\n";

    TEST_CASE("1.1 the rule is on in a match played now, set_game_mode sets it back, init keeps it, 187 never has it") {
        SimulationEngine sim;
        ASSERT_TRUE(sim.last_side_rule());                                                  // (the original's game, nothing set)
        sim.set_last_side_rule(false);
        ASSERT_FALSE(sim.last_side_rule());
        sim.init_test_world(20, 20, 3, 60000);
        ASSERT_FALSE(sim.last_side_rule());                                                 // (init keeps what was chosen, like the mode)
        sim.set_game_mode(GameMode::HighestScore);
        ASSERT_TRUE(sim.last_side_rule());                                                  // (a program that watched an old recording and then starts a match sets the mode: the rule of a match played now)
        sim.set_game_mode(GameMode::Kills187);
        ASSERT_FALSE(sim.last_side_rule());                                                 // (187 has an end rule of its own)
        sim.set_last_side_rule(true);
        ASSERT_FALSE(sim.last_side_rule());
    } TEST_END();

    TEST_CASE("1.2 the state hash does not carry the rule: the same match has the same hash with it on and off until the tick before it ends; the end is what differs") {
        SimulationEngine probe;
        make_world(probe);
        only_alive(probe, {0});
        const int end = ticks_to_end(probe, 200);
        ASSERT_GE(end, 1);
        SimulationEngine on;
        SimulationEngine off;
        make_world(on, GameMode::HighestScore, true);
        make_world(off, GameMode::HighestScore, false);
        only_alive(on, {0});
        only_alive(off, {0});
        ASSERT_TRUE(on.state_hash() == off.state_hash());
        for (int t = 1; t < end; ++t) {
            on.tick();
            off.tick();
            ASSERT_TRUE(on.state_hash() == off.state_hash());
        }
        on.tick();
        off.tick();
        ASSERT_TRUE(on.is_match_over() && !off.is_match_over());
        ASSERT_TRUE(on.state_hash().total != off.state_hash().total);                        // (the match state is in the hash: a peer or a file that plays the other rule is caught at the end)
    } TEST_END();

    TEST_CASE("2.1 a lone survivor and no food ends the match with every score tied at 0, and the survivor wins on every machine's screen") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0});
        const int end = ticks_to_end(sim, 200);
        ASSERT_GT(end, 0);
        ASSERT_LE(end, 12);                                                                  // (the first poll of CHECKGO: not a minute of waiting)
        const MatchResult& r = result_of(sim);
        ASSERT_TRUE(r.is_over && r.tie_standing == 0);
        ASSERT_TRUE(r.stats[0].score == 0 && r.stats[1].score == 0);
        for (uint8_t seat = 0; seat < MAX_PLAYERS; ++seat) ASSERT_TRUE(top_row(r, seat) == Seats({0}));     // (no tie goes to the local team)
        ASSERT_TRUE(r.winning_players == Seats({0}));
        ASSERT_TRUE(r.losing_players == Seats({1, 2, 3}));
        ASSERT_TRUE(r.is_winner(0) && !r.is_winner(1) && !r.is_winner(2) && !r.is_winner(3));
    } TEST_END();

    TEST_CASE("2.2 the original's CHECKGO alone (rules 5) plays the same state on to the clock: no end in 30 s, and a tie never wins") {
        SimulationEngine sim;
        make_world(sim, GameMode::HighestScore, false);
        only_alive(sim, {0});
        ASSERT_TRUE(survives(sim));
        ASSERT_TRUE(result_of(sim).tie_standing == PLAYER_NEUTRAL && !sim.is_match_over());
        // and when the clock ends such a match, the results rank by score as they always did: a dead team with the best score is first, nobody is put first for being the one left
        SimulationEngine clock;
        make_world(clock, GameMode::HighestScore, false, 4000);
        only_alive(clock, {0});
        clock.set_player_score(1, 90);
        ASSERT_GT(ticks_to_end(clock, 400), 0);
        ASSERT_TRUE(result_of(clock).tie_standing == PLAYER_NEUTRAL && top_row(result_of(clock), 0) == Seats({1}));
    } TEST_END();

    TEST_CASE("2.3 the survivor can be any seat; it needs no score and no egg of its own") {
        for (uint8_t seat = 0; seat < MAX_PLAYERS; ++seat) {
            SimulationEngine sim;
            make_world(sim);
            only_alive(sim, {seat});
            ASSERT_GT(ticks_to_end(sim, 200), 0);
            ASSERT_TRUE(result_of(sim).tie_standing == seat && result_of(sim).winning_players == Seats({seat}));
        }
    } TEST_END();

    TEST_CASE("2.4 an enemy egg keeps the match going: a team with an egg and no ant is alive; with the egg gone the survivor wins") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0});
        sim.set_player_eggs(1, 1);
        ASSERT_TRUE(survives(sim));
        sim.set_player_eggs(1, 0);
        ASSERT_GT(ticks_to_end(sim, 200), 0);
    } TEST_END();

    TEST_CASE("2.5 an enemy egg that is hatching keeps the match going, and so does the ant that it becomes") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0});
        sim.set_player_eggs(1, 1);
        ASSERT_TRUE(sim.try_hatch(1, AntType::Worker, true) == SimulationEngine::HatchResult::Started);
        ASSERT_EQ(sim.get_player_eggs(1), 0u);                                               // (the egg is in the incubator: only the hatch keeps the team alive)
        ASSERT_TRUE(survives(sim, 100));                                                     // (5 s: the hatch takes 8)
        ASSERT_TRUE(survives(sim, 200));                                                     // (and the hatched ant is an ant of the other side)
    } TEST_END();

    TEST_CASE("2.6 nobody left at all is the original's rule and ends the match, with no survivor to put first") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {});
        ASSERT_GT(ticks_to_end(sim, 200), 0);
        ASSERT_TRUE(result_of(sim).tie_standing == PLAYER_NEUTRAL);
        SimulationEngine off;
        make_world(off, GameMode::HighestScore, false);
        only_alive(off, {});
        ASSERT_GT(ticks_to_end(off, 200), 0);                                                // (the same with the old rules)
    } TEST_END();

    TEST_CASE("2.7 two ants of the same side are one survivor; two sides with an ant each are not") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0, 0});
        ASSERT_GT(ticks_to_end(sim, 200), 0);
        SimulationEngine two;
        make_world(two);
        only_alive(two, {0, 1});
        ASSERT_TRUE(survives(two));
    } TEST_END();

    TEST_CASE("3.1 food on the ground keeps the match going; when the last unit is taken it ends") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0});
        const int32_t pile = place_pile(sim, 40, 40, 3, 25);
        ASSERT_TRUE(survives(sim));
        take_units(sim, pile, 100);
        ASSERT_GT(ticks_to_end(sim, 200), 0);
    } TEST_END();

    TEST_CASE("3.2 one unit of one pile is enough to keep the match going, wherever the pile is") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0});
        const int32_t a = place_pile(sim, 40, 40, 1, 10);
        const int32_t b = place_pile(sim, 30, 30, 5, 50);
        take_units(sim, a, 1);
        take_units(sim, b, 4);
        ASSERT_TRUE(survives(sim, 100));                                                     // (one unit of the second pile is left)
        take_units(sim, b, 1);
        ASSERT_GT(ticks_to_end(sim, 200), 0);
    } TEST_END();

    TEST_CASE("3.3 a unit worth no points scores nothing and does not keep the match going") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0});
        place_pile(sim, 40, 40, 4, 0);
        ASSERT_GT(ticks_to_end(sim, 200), 0);
        SimulationEngine rules5;
        make_world(rules5, GameMode::HighestScore, false);
        only_alive(rules5, {0});
        place_pile(rules5, 40, 40, 4, 0);
        ASSERT_TRUE(survives(rules5));
    } TEST_END();

    TEST_CASE("3.4 a lunchbox on the ground is food; a lunchbox worth nothing (a thief's debt) is not") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0});
        sim.grid_mut().drop_lunchbox(40, 40, 25);
        ASSERT_TRUE(sim.has_lunchbox_at(TileCoord{40, 40}));
        ASSERT_TRUE(survives(sim));
        SimulationEngine empty;
        make_world(empty);
        only_alive(empty, {0});
        empty.grid_mut().drop_lunchbox(40, 40, 0);
        ASSERT_GT(ticks_to_end(empty, 200), 0);
    } TEST_END();

    TEST_CASE("3.5 food that the survivor carries keeps the match going; when it is deposited it ends") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t ant = only_alive(sim, {0});
        sim.get_unit(ant).pick_up_food(1, 25);
        ASSERT_TRUE(survives(sim, 100));                                                     // (it stands far from its hill and does nothing)
        sim.get_unit(ant).clear_inventory();                                                 // (what the deposit does: cleanup_enter)
        ASSERT_GT(ticks_to_end(sim, 200), 0);
    } TEST_END();

    TEST_CASE("3.6 loot that a thief carries keeps the match going as well") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t ant = only_alive(sim, {0});
        sim.get_unit(ant).steal_points(30);
        ASSERT_TRUE(survives(sim, 200));
        sim.get_unit(ant).clear_inventory();
        ASSERT_GT(ticks_to_end(sim, 200), 0);
    } TEST_END();

    TEST_CASE("3.7 power-ups, bombs and fire walls do not keep the match going: nothing in them scores") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0});
        sim.grid_mut().place_powerup(40, 40, 4);
        sim.grid_mut().place_bomb(41, 40, 0);
        sim.grid_mut().place_bomb(42, 40, 1);
        sim.grid_mut().place_firewall(43, 40, 0);
        ASSERT_GT(ticks_to_end(sim, 200), 0);
        ASSERT_TRUE(result_of(sim).tie_standing == 0);
    } TEST_END();

    TEST_CASE("3.8 a whole harvest: the pile, the bite, the walk home and the deposit keep the match going; it ends when the food is in the hill") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t ant = only_alive(sim, {0});
        const int32_t pile = place_pile(sim, 22, 20, 1, 25);
        sim.issue_move_order(ant, TileCoord{22, 20});
        ASSERT_EQ(sim.get_unit(ant).orig_order, AntUnit::kOrderHarvest);
        bool bit = false;
        bool over_while_carrying = false;
        for (int t = 0; t < 2000 && !sim.is_match_over(); ++t) {
            sim.tick();
            sim.clear_audio_events();
            sim.clear_news_events();
            bit = bit || sim.get_unit(ant).is_holding();
            over_while_carrying = over_while_carrying || (sim.is_match_over() && sim.get_unit(ant).is_holding());
        }
        ASSERT_TRUE(bit);
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 0u);
        ASSERT_TRUE(sim.is_match_over());
        ASSERT_FALSE(over_while_carrying);
        ASSERT_EQ(sim.get_player_score(0), 25);                                              // (the deposit scored: the match did not end with the food in the ant's hands)
        ASSERT_TRUE(result_of(sim).tie_standing == 0 && top_row(result_of(sim), 1) == Seats({0}));
    } TEST_END();

    TEST_CASE("3.9 an ant in the middle of a bite keeps the match going even when its pile already reads empty") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t ant = only_alive(sim, {0});
        const int32_t pile = place_pile(sim, 22, 20, 1, 25);
        sim.issue_move_order(ant, TileCoord{22, 20});
        bool in_clip = false;
        for (int t = 0; t < 400 && !in_clip; ++t) {
            sim.tick();
            in_clip = sim.get_unit(ant).loco_action == AntUnit::kActionHarvest;
        }
        ASSERT_TRUE(in_clip);
        take_units(sim, pile, 1);                                                            // (another ant took the last unit: the bite in progress still gives food at the end of the clip)
        int ticks_in_clip = 0;
        for (int t = 0; t < 60 && sim.get_unit(ant).loco_action == AntUnit::kActionHarvest; ++t) {       // (the clip lasts several checks of CHECKGO)
            sim.tick();
            sim.clear_news_events();
            sim.clear_audio_events();
            ++ticks_in_clip;
            ASSERT_FALSE(sim.is_match_over());
        }
        ASSERT_GE(ticks_in_clip, 4);
        ASSERT_TRUE(sim.get_unit(ant).is_holding());                                         // (the bite gave food from the pile that read empty)
    } TEST_END();

    TEST_CASE("4.1 allies: one live team of an alliance is its side; the dead ally is on the same row, and the row wins") {
        SimulationEngine sim;
        make_world(sim);
        sim.form_alliance(0, 1);
        only_alive(sim, {1});
        sim.set_player_score(0, 400);                                                       // (the dead ally has the best score)
        ASSERT_GT(ticks_to_end(sim, 200), 0);
        const MatchResult& r = result_of(sim);
        ASSERT_TRUE(r.tie_standing == 1);
        ASSERT_TRUE(top_row(r, 2) == Seats({0, 1}));
        ASSERT_TRUE(r.is_winner(0) && r.is_winner(1) && !r.is_winner(2) && !r.is_winner(3));
    } TEST_END();

    TEST_CASE("4.2 two allied teams that are both alive are one side and win together") {
        SimulationEngine sim;
        make_world(sim);
        sim.form_alliance(0, 3);
        only_alive(sim, {0, 3});
        ASSERT_GT(ticks_to_end(sim, 200), 0);
        const MatchResult& r = result_of(sim);
        ASSERT_TRUE(r.winning_players == Seats({0, 3}) && r.losing_players == Seats({1, 2}));
    } TEST_END();

    TEST_CASE("4.3 an ant of a team that is not an ally keeps the match going, and an alliance that was not formed is two sides") {
        SimulationEngine sim;
        make_world(sim);
        sim.form_alliance(0, 3);
        only_alive(sim, {0, 3, 1});
        ASSERT_TRUE(survives(sim));
        SimulationEngine none;
        make_world(none);
        only_alive(none, {0, 3});
        ASSERT_TRUE(survives(none));
    } TEST_END();

    TEST_CASE("4.4 a team that dropped out is not a side: the other one that has ants wins when nothing is left to score, and not under the rules 5") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0, 1});
        sim.drop_player(1);
        ASSERT_GT(ticks_to_end(sim, 200), 0);
        ASSERT_TRUE(result_of(sim).tie_standing == 0);
        SimulationEngine off;
        make_world(off, GameMode::HighestScore, false);
        only_alive(off, {0, 1});
        off.drop_player(1);
        ASSERT_TRUE(survives(off));
    } TEST_END();

    TEST_CASE("5.1 the highest score wins even for a team with no ant: a dead team ahead wins, the survivor ahead wins, a tie for the top goes to the survivor, all zero goes to the survivor") {
        SimulationEngine low;                                                               // a dead team leads: it wins, the survivor does not
        make_world(low);
        only_alive(low, {0});
        low.set_player_score(1, 150);
        low.set_player_score(0, 20);
        ASSERT_GT(ticks_to_end(low, 200), 0);                                               // (the rule ends the match: nothing can change the scores, so this is what the clock would give)
        ASSERT_TRUE(result_of(low).tie_standing == 0 && result_of(low).standing == PLAYER_NEUTRAL);
        for (uint8_t seat = 0; seat < MAX_PLAYERS; ++seat) ASSERT_TRUE(top_row(result_of(low), seat) == Seats({1}));
        ASSERT_TRUE(result_of(low).winning_players == Seats({1}) && result_of(low).losing_players == Seats({0, 2, 3}));
        SimulationEngine high;                                                              // the survivor leads
        make_world(high);
        only_alive(high, {0});
        high.set_player_score(0, 150);
        high.set_player_score(1, 20);
        ASSERT_GT(ticks_to_end(high, 200), 0);
        for (uint8_t seat = 0; seat < MAX_PLAYERS; ++seat) ASSERT_TRUE(top_row(result_of(high), seat) == Seats({0}));
        SimulationEngine tie;                                                               // a tie for the top: the survivor is first on every machine's screen, the dead team's own included
        make_world(tie);
        only_alive(tie, {2});
        tie.set_player_score(1, 100);
        tie.set_player_score(2, 100);
        tie.set_player_score(0, 40);
        ASSERT_GT(ticks_to_end(tie, 200), 0);
        for (uint8_t seat = 0; seat < MAX_PLAYERS; ++seat) ASSERT_TRUE(top_row(result_of(tie), seat) == Seats({2}));
        ASSERT_TRUE(result_of(tie).winning_players == Seats({2}));
        SimulationEngine zero;                                                              // all zero is a tie at 0
        make_world(zero);
        only_alive(zero, {3});
        ASSERT_GT(ticks_to_end(zero, 200), 0);
        for (uint8_t seat = 0; seat < MAX_PLAYERS; ++seat) ASSERT_TRUE(top_row(result_of(zero), seat) == Seats({3}));
    } TEST_END();

    TEST_CASE("5.1b two dead teams tied above the survivor are not the survivor's tie: the original's tie-break (the local team) decides between them; the survivor tied below them is last") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0});
        sim.set_player_score(1, 100);
        sim.set_player_score(2, 100);
        sim.set_player_score(0, 20);
        ASSERT_GT(ticks_to_end(sim, 200), 0);
        const MatchResult& r = result_of(sim);
        ASSERT_TRUE(top_row(r, 1) == Seats({1}) && top_row(r, 2) == Seats({2}));
        ASSERT_TRUE(r.rows(1)[2].first == 0 && r.rows(3)[2].first == 0);                 // (the survivor, at 20, is third: not first anywhere)
        SimulationEngine three;                                                             // the survivor ties with one of two dead teams at the top: it goes first of the tied rows, the other keeps the local tie-break
        make_world(three);
        only_alive(three, {0});
        three.set_player_score(0, 100);
        three.set_player_score(1, 100);
        three.set_player_score(2, 100);
        ASSERT_GT(ticks_to_end(three, 200), 0);
        for (uint8_t seat = 0; seat < MAX_PLAYERS; ++seat) ASSERT_TRUE(top_row(result_of(three), seat) == Seats({0}));
        ASSERT_TRUE(result_of(three).rows(2)[1].first == 2 && result_of(three).rows(1)[1].first == 1);
    } TEST_END();

    TEST_CASE("5.1c an alliance is one row: a survivor whose dead ally has the best score wins with the row, an alliance below a dead team's score loses, a tie of the row's sum goes to the alliance") {
        SimulationEngine best;
        make_world(best);
        best.form_alliance(0, 1);
        only_alive(best, {1});
        best.set_player_score(0, 200);
        best.set_player_score(2, 150);
        ASSERT_GT(ticks_to_end(best, 200), 0);
        for (uint8_t seat = 0; seat < MAX_PLAYERS; ++seat) ASSERT_TRUE(top_row(result_of(best), seat) == Seats({0, 1}));
        SimulationEngine below;
        make_world(below);
        below.form_alliance(0, 1);
        only_alive(below, {1});
        below.set_player_score(0, 30);
        below.set_player_score(1, 30);
        below.set_player_score(2, 100);
        ASSERT_GT(ticks_to_end(below, 200), 0);
        ASSERT_TRUE(top_row(result_of(below), 3) == Seats({2}) && result_of(below).winning_players == Seats({2}));
        SimulationEngine tied;
        make_world(tied);
        tied.form_alliance(0, 1);
        only_alive(tied, {1});
        tied.set_player_score(0, 30);
        tied.set_player_score(1, 30);
        tied.set_player_score(2, 60);
        ASSERT_GT(ticks_to_end(tied, 200), 0);
        for (uint8_t seat = 0; seat < MAX_PLAYERS; ++seat) ASSERT_TRUE(top_row(result_of(tied), seat) == Seats({0, 1}));
    } TEST_END();

    TEST_CASE("5.2 the clock ends a match with a survivor and food left by the scores alone: a dead team with the best score wins, as it always did") {
        SimulationEngine sim;
        make_world(sim, GameMode::HighestScore, true, 4000);
        only_alive(sim, {0});
        place_pile(sim, 40, 40, 2, 25);
        sim.set_player_score(1, 90);
        sim.set_player_score(0, 10);
        ASSERT_GT(ticks_to_end(sim, 400), 0);                                               // (4 s)
        const MatchResult& r = result_of(sim);
        ASSERT_TRUE(r.tie_standing == PLAYER_NEUTRAL);
        ASSERT_TRUE(top_row(r, 2) == Seats({1}));
        ASSERT_TRUE(r.winning_players == Seats({1}));
    } TEST_END();

    TEST_CASE("5.3 the original's rule still ends the match when the survivor has the strictly best score, with food on the map, and ranks by score") {
        SimulationEngine sim;
        make_world(sim);
        only_alive(sim, {0});
        place_pile(sim, 40, 40, 2, 25);
        sim.set_player_score(0, 60);
        ASSERT_GT(ticks_to_end(sim, 200), 0);
        ASSERT_TRUE(result_of(sim).tie_standing == PLAYER_NEUTRAL && top_row(result_of(sim), 3) == Seats({0}));
    } TEST_END();

    TEST_CASE("6.1 a match of one team has nobody to beat: it does not end because its team is the only side") {
        LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL"));
        SimulationEngine sim;
        sim.init(level, 5, 0x01);
        for (int32_t i = 0; i < static_cast<int32_t>(sim.grid().food_objects().size()); ++i) take_units(sim, i, 1000);
        ASSERT_TRUE(survives(sim, 200));
        SimulationEngine two;                                                               // (the same map and no food with two teams in the match: the second has its eggs and ants, so the match goes on)
        two.init(level, 5, 0x03);
        for (int32_t i = 0; i < static_cast<int32_t>(two.grid().food_objects().size()); ++i) take_units(two, i, 1000);
        ASSERT_TRUE(survives(two, 200));
        SimulationEngine ends;                                                              // (two teams in the match, one wiped out, no food: the lone survivor wins)
        ends.init(level, 5, 0x03);
        for (int32_t i = 0; i < static_cast<int32_t>(ends.grid().food_objects().size()); ++i) take_units(ends, i, 1000);
        ends.set_player_eggs(1, 0);
        for (const AntSnapshot& a : ends.get_world_state().ants) {
            if (a.player_id == 1) ends.kill_unit(a.id);
        }
        ASSERT_GT(ticks_to_end(ends, 400), 0);
        ASSERT_TRUE(result_of(ends).tie_standing == 0);
    } TEST_END();

    TEST_CASE("6.2 187 is unchanged: it ends at one side left with food on the map, whichever way the switch is set, and the other rule does not change its result") {
        for (const bool rule : {true, false}) {
            SimulationEngine sim;
            make_world(sim, GameMode::Kills187);
            sim.set_last_side_rule(rule);
            only_alive(sim, {0});
            place_pile(sim, 40, 40, 3, 25);
            ASSERT_GT(ticks_to_end(sim, 200), 0);
            ASSERT_TRUE(result_of(sim).standing == 0 && result_of(sim).tie_standing == PLAYER_NEUTRAL);        // (187's own field, unconditional; the tie field is the original's game's)
            ASSERT_EQ(result_of(sim).ants_left[0], 1u);
        }
    } TEST_END();

    TEST_CASE("6.3 the same match twice ends at the same tick with the same result; a copy of the engine ends at the same tick, and a copy keeps the rule it was given") {
        SimulationEngine a;
        SimulationEngine b;
        make_world(a);
        make_world(b);
        const uint32_t ant_a = only_alive(a, {0});
        const uint32_t ant_b = only_alive(b, {0});
        place_pile(a, 40, 40, 1, 25);
        place_pile(b, 40, 40, 1, 25);
        a.issue_move_order(ant_a, TileCoord{40, 40});
        b.issue_move_order(ant_b, TileCoord{40, 40});
        for (int t = 0; t < 40; ++t) {
            a.tick();
            b.tick();
        }
        SimulationEngine copy(a);                                                            // (a client's predicted engine is such a copy)
        int end_a = -1;
        int end_b = -1;
        int end_c = -1;
        for (int t = 1; t <= 3000 && (end_a < 0 || end_b < 0 || end_c < 0); ++t) {
            if (end_a < 0) { a.tick(); if (a.is_match_over()) end_a = t; }
            if (end_b < 0) { b.tick(); if (b.is_match_over()) end_b = t; }
            if (end_c < 0) { copy.tick(); if (copy.is_match_over()) end_c = t; }
            if (end_a < 0 && end_b < 0 && end_c < 0) ASSERT_TRUE(a.state_hash() == b.state_hash() && a.state_hash() == copy.state_hash());
        }
        ASSERT_GT(end_a, 0);
        ASSERT_TRUE(end_a == end_b && end_a == end_c);
        ASSERT_TRUE(a.state_hash() == b.state_hash() && a.state_hash() == copy.state_hash());
        ASSERT_TRUE(result_of(a).tie_standing == 0 && result_of(copy).tie_standing == 0);
        SimulationEngine off;
        make_world(off, GameMode::HighestScore, false);
        only_alive(off, {0});
        SimulationEngine off_copy(off);
        ASSERT_FALSE(off_copy.last_side_rule());
        ASSERT_TRUE(survives(off_copy));
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
