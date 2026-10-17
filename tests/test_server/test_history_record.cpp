// Tests of what a record of the match history says of a match (ants_server/history_record.hpp, docs/REPLAYS.md "The match history"): WHO WON, by the rule of the room's game mode, for every kind of tie and for teams,
// a quitter and a seat that dropped out; the format, the computers, the JSON of a record. The matches are made up here as the engine's MatchResult (numbers per seat, alliances, the quitter, the side left standing
// in mode 187): the rule is a pure function of them, so every case says the numbers and the answer it must give. The real recordings (a match played again by the engine) are test_history_store.cpp and
// test_history_rooms.cpp; the engine's own outcome of play() is test_replay (RP2.11, RP2.12).
#include <array>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../common/test_assert.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"
#include "ants_server/history_record.hpp"
#include "ants_sim/game_mode.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_sim/start_teams.hpp"

using namespace ants;
using namespace ants::server;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; run_tests.sh clears it)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name.substr(0, 110) << " ... " << std::flush;
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

constexpr int64_t kT0 = 1791469929;       // 2026-10-08 14:32:09 UTC
const std::string kId = "ants-TINY-20261008-143209Z";

struct Num {
    int32_t score{0};
    uint32_t killed{0};
    uint32_t lost{0};
    uint32_t hatched{0};
};

// A match as the engine would hand it over when it ended
struct Fake {
    uint8_t mode{0};                                         // 0 highest-score, 1 187
    uint8_t roster{0x03};
    std::array<std::string, sim::MAX_PLAYERS> names{{"Ann", "Bob", "Cy", "Di"}};
    std::array<Num, sim::MAX_PLAYERS> n{};
    std::vector<std::array<uint8_t, 2>> allies;              // allied pairs (both ways)
    int quitter{-1};
    int standing{-1};                                        // 187: the side left (a seat), -1 none
    uint8_t dropped{0};
    sim::StartTeams teams;
    uint32_t turns{2400};
    bool over{true};

    Fake& seat(uint8_t s, int32_t score, uint32_t killed, uint32_t lost = 0, uint32_t hatched = 0) {
        n[s] = Num{score, killed, lost, hatched};
        return *this;
    }
};

HistoryRecord record_of(const Fake& f) {
    replay::Replay rep;
    rep.head.game_version = "v0.12.0-test";
    rep.head.map_name = "TINY.LVL";
    rep.head.roster = f.roster;
    rep.head.names = f.names;
    rep.head.teams = f.teams;
    rep.head.set_mode(f.mode);
    replay::Outcome out;
    out.ran = true;
    out.ok = true;
    out.turns = f.turns;
    out.match_over = f.over;
    out.dropped_mask = f.dropped;
    sim::MatchResult& r = out.result;
    r.is_over = true;
    r.present_mask = static_cast<uint8_t>(f.roster & ~f.dropped & 0x0F);
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        r.stats[s].score = f.n[s].score;
        r.stats[s].enemy_killed = f.n[s].killed;
        r.stats[s].friendly_lost = f.n[s].lost;
        r.stats[s].new_hatched = f.n[s].hatched;
        r.stats[s].bombs_planted = s + 1u;
        r.stats[s].bombs_defused = s + 2u;
        r.stats[s].fires_lit = s + 3u;
    }
    for (const auto& pair : f.allies) {
        r.ally[pair[0]] = pair[1];
        r.ally[pair[1]] = pair[0];
    }
    r.quitter = f.quitter >= 0 ? static_cast<uint16_t>(f.quitter) : sim::NO_QUITTER;
    r.standing = f.standing >= 0 ? static_cast<uint8_t>(f.standing) : sim::PLAYER_NEUTRAL;
    return make_history_record(kId, kT0, rep, out);
}

// "winner seats; draw": the seats of the winning row ("" for a draw or none), in the order of the row
std::string winners_of(const HistoryRecord& r) {
    std::string text;
    for (const HistoryRow& row : r.rows) {
        if (!row.winner) continue;
        for (const uint8_t s : row.seats) text += std::to_string(s);
    }
    return text;
}

std::string row_order(const HistoryRecord& r) {
    std::string text;
    for (const HistoryRow& row : r.rows) {
        if (!text.empty()) text += " ";
        for (const uint8_t s : row.seats) text += std::to_string(s);
    }
    return text;
}

bool seat_wins(const HistoryRecord& r, uint8_t seat) {
    for (const HistorySeat& s : r.seats) {
        if (s.seat == seat) return s.winner;
    }
    return false;
}

}  // namespace

int main() {
    std::cout << "=======================================================\n MATCH HISTORY RECORD SUITE (who won, by the rule of the game mode)\n=======================================================\n";

    TEST_CASE("HR1.1 Highest score: the score decides, whatever the kills, the losses, the hatched ants or the seat") {
        Fake f;
        f.seat(0, 400, 9, 1, 1).seat(1, 500, 2, 8, 9);       // seat 1 has the score; seat 0 has far more kills and fewer losses
        HistoryRecord r = record_of(f);
        ASSERT_EQ(r.mode, std::string("highest-score"));
        ASSERT_FALSE(r.draw);
        ASSERT_EQ(winners_of(r), std::string("1"));
        ASSERT_EQ(row_order(r), std::string("1 0"));
        ASSERT_TRUE(seat_wins(r, 1) && !seat_wins(r, 0));
        ASSERT_EQ(std::string(r.result()), std::string("decided"));
        f.seat(0, 600, 0, 20, 0).seat(1, 500, 30, 0, 30);    // and the other way: the lower seat has the score
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "0" && row_order(r) == "0 1");
        f.seat(0, -50, 5).seat(1, -200, 5);                   // a score can be below zero: the higher still wins
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "0");
    } TEST_END();

    TEST_CASE("HR1.2 Highest score, tie 1 (equal scores): the KILLS decide, not the seat (the engine's own order would give the lower seat) and not the losses or the hatched") {
        Fake f;
        f.seat(0, 400, 3, 0, 9).seat(1, 400, 7, 9, 0);       // equal score; seat 1 has more kills though seat 0 has fewer losses and more hatched
        const HistoryRecord r = record_of(f);
        ASSERT_FALSE(r.draw);
        ASSERT_EQ(winners_of(r), std::string("1"));
        ASSERT_EQ(row_order(r), std::string("1 0"));
        ASSERT_TRUE(seat_wins(r, 1) && !seat_wins(r, 0));
        ASSERT_EQ(std::string(r.result()), std::string("decided"));
        // what the engine says of the same numbers: its row order breaks the tie by the seat (nobody is "local" here) and knows no kills, so its first row is seat 0
        sim::MatchResult engine;
        engine.is_over = true;
        engine.present_mask = 0x03;
        engine.stats[0].score = 400;
        engine.stats[0].enemy_killed = 3;
        engine.stats[1].score = 400;
        engine.stats[1].enemy_killed = 7;
        ASSERT_EQ(engine.rows(sim::PLAYER_NEUTRAL).front().first, uint8_t{0});
    } TEST_END();

    TEST_CASE("HR1.3 Highest score, tie 2 (equal scores AND equal kills): a DRAW whatever the losses and the hatched ants, and nobody is marked the winner") {
        Fake f;
        f.seat(0, 400, 5, 1, 9).seat(1, 400, 5, 9, 1);
        const HistoryRecord r = record_of(f);
        ASSERT_TRUE(r.draw);
        ASSERT_EQ(winners_of(r), std::string(""));
        ASSERT_TRUE(!seat_wins(r, 0) && !seat_wins(r, 1));
        ASSERT_EQ(std::string(r.result()), std::string("draw"));
        Fake zero;                                            // nothing happened: a draw too
        const HistoryRecord z = record_of(zero);
        ASSERT_TRUE(z.draw && winners_of(z).empty());
    } TEST_END();

    TEST_CASE("HR1.4 Highest score, tie 3 (different scores, equal kills): the score decides; equal kills do not make a draw when the scores differ") {
        Fake f;
        f.seat(0, 401, 5).seat(1, 400, 5);
        const HistoryRecord r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "0");
    } TEST_END();

    TEST_CASE("HR2.1 Mode 187, the last side with ants or eggs wins outright, whatever the kills: the side left stands first") {
        Fake f;
        f.mode = 1;
        f.seat(0, 9, 9).seat(1, 2, 2);                        // seat 0 killed more, but seat 1 is the one left
        f.standing = 1;
        HistoryRecord r = record_of(f);
        ASSERT_EQ(r.mode, std::string("187"));
        ASSERT_FALSE(r.draw);
        ASSERT_EQ(winners_of(r), std::string("1"));
        ASSERT_EQ(row_order(r), std::string("1 0"));
        ASSERT_TRUE(seat_wins(r, 1) && !seat_wins(r, 0));
        f.seat(0, 2, 2).seat(1, 2, 2);                        // equal kills: the side left still wins, there is no draw
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "1");
        f.standing = 0;                                       // and seat 0, the lower seat, when it is the one left
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "0");
    } TEST_END();

    TEST_CASE("HR2.2 Mode 187, nobody left alone (the clock ran out): the most KILLS wins, whatever the field called score says; equal kills is a DRAW") {
        Fake f;
        f.mode = 1;
        f.seat(0, 1, 5).seat(1, 100, 3);                      // a score that is not the kills (the engine's is: this proves the rule reads the kills)
        HistoryRecord r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "0");
        ASSERT_EQ(row_order(r), std::string("0 1"));
        f.seat(0, 1, 3).seat(1, 100, 5);
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "1" && row_order(r) == "1 0");     // (the higher seat, with more kills)
        f.seat(0, 7, 4, 1, 9).seat(1, 8, 4, 9, 1);            // equal kills: a draw, losses and hatched do not break it
        r = record_of(f);
        ASSERT_TRUE(r.draw && winners_of(r).empty() && std::string(r.result()) == "draw");
        Fake none;                                            // all went at once (nobody has anything left): kills decide, here nothing
        none.mode = 1;
        ASSERT_TRUE(record_of(none).draw);
    } TEST_END();

    TEST_CASE("HR2.3 Mode 187 stores the kills as the ranking value: a seat's and a row's `score` is its kills, whatever the engine's score was; highest-score keeps the score") {
        Fake f;
        f.mode = 1;
        f.seat(0, 77, 5, 2, 1).seat(1, 66, 3, 4, 2);
        HistoryRecord r = record_of(f);
        ASSERT_TRUE(r.seats[0].score == 5 && r.seats[0].killed == 5 && r.seats[1].score == 3 && r.rows[0].score == 5 && r.rows[0].killed == 5 && r.rows[1].score == 3);
        f.mode = 0;
        r = record_of(f);
        ASSERT_TRUE(r.seats[0].score == 77 && r.seats[0].killed == 5 && r.seats[1].score == 66 && r.rows[0].score == 77);
    } TEST_END();

    TEST_CASE("HR3.1 The same numbers give another answer in the other mode: score 100 against 1, kills 3 against 5 (highest-score: seat 0 by the score; 187: seat 1 by the kills)") {
        Fake f;
        f.seat(0, 100, 3).seat(1, 1, 5);
        HistoryRecord r = record_of(f);
        ASSERT_TRUE(r.mode == "highest-score" && !r.draw && winners_of(r) == "0");
        f.mode = 1;
        r = record_of(f);
        ASSERT_TRUE(r.mode == "187" && !r.draw && winners_of(r) == "1");
        // the three ties, in the two modes, from the same equal-score numbers (400 - 400) with kills 3 - 7: highest-score by kills, 187 by kills; and with equal kills 5 - 5: a draw in both
        Fake t;
        t.seat(0, 400, 3).seat(1, 400, 7);
        ASSERT_TRUE(winners_of(record_of(t)) == "1");
        t.mode = 1;
        ASSERT_TRUE(winners_of(record_of(t)) == "1");
        Fake e;
        e.seat(0, 400, 5).seat(1, 400, 5);
        ASSERT_TRUE(record_of(e).draw);
        e.mode = 1;
        ASSERT_TRUE(record_of(e).draw);
    } TEST_END();

    TEST_CASE("HR4.1 Teams: an alliance is one row with its seats' numbers added up (the engine's grouping); the sums are ranked by the rule, the winning row marks BOTH seats") {
        Fake f;
        f.roster = 0x0F;
        f.teams = sim::StartTeams{true, 0, 2};
        f.allies = {{0, 2}, {1, 3}};
        f.seat(0, 300, 2).seat(2, 300, 1).seat(1, 500, 1).seat(3, 100, 1);       // 600 against 600 on the score: the kills 3 against 2 decide
        HistoryRecord r = record_of(f);
        ASSERT_EQ(r.format, std::string("2v2"));
        ASSERT_EQ(r.rows.size(), size_t{2});
        ASSERT_TRUE(!r.draw && winners_of(r) == "02" && row_order(r) == "02 13");
        ASSERT_TRUE(r.rows[0].score == 600 && r.rows[0].killed == 3 && r.rows[1].score == 600 && r.rows[1].killed == 2);
        ASSERT_TRUE(seat_wins(r, 0) && seat_wins(r, 2) && !seat_wins(r, 1) && !seat_wins(r, 3));
        f.seat(3, 100, 2);                                    // 3 kills against 3: a draw
        r = record_of(f);
        ASSERT_TRUE(r.draw && winners_of(r).empty());
        f.seat(3, 150, 2);                                    // 600 against 650 on the score: the score decides
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "13");
        f.mode = 1;                                           // 187: the team with the most kills (3 against 3 is a draw; 3 against 2 a win)
        f.seat(3, 0, 2);
        ASSERT_TRUE(record_of(f).draw);
        f.standing = 1;                                       // the side left stands first: the pair of seat 1 (lowest alive seat of the side)
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "13" && row_order(r) == "13 02");
        Fake pair_one;                                        // a pair against a single seat (2v1)
        pair_one.roster = 0x07;
        pair_one.teams = sim::StartTeams{true, 0, 1};
        pair_one.allies = {{0, 1}};
        pair_one.seat(0, 100, 1).seat(1, 100, 1).seat(2, 150, 9);
        r = record_of(pair_one);
        ASSERT_TRUE(r.format == "2v1" && !r.draw && winners_of(r) == "01" && r.rows[0].score == 200);
    } TEST_END();

    TEST_CASE("HR5.1 A match that was left: the quitter's row is last however it stood, the other side wins (no draw), both modes; the quitter is marked left") {
        Fake f;
        f.seat(0, 900, 20).seat(1, 100, 1);                   // seat 0 leads and quits
        f.quitter = 0;
        HistoryRecord r = record_of(f);
        ASSERT_EQ(r.quitter, 0);
        ASSERT_TRUE(!r.draw && winners_of(r) == "1" && row_order(r) == "1 0");
        ASSERT_TRUE(r.seats[0].left && !r.seats[1].left && seat_wins(r, 1) && !seat_wins(r, 0));
        ASSERT_EQ(std::string(r.result()), std::string("cut"));
        f.seat(0, 100, 5).seat(1, 100, 5);                    // equal numbers: still no draw when one quit
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "1");
        f.mode = 1;
        f.seat(0, 50, 50).seat(1, 1, 1);
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "1" && r.mode == "187");
        f.quitter = 1;
        r = record_of(f);
        ASSERT_TRUE(winners_of(r) == "0" && row_order(r) == "0 1" && r.seats[1].left);
    } TEST_END();

    TEST_CASE("HR5.2 Free for all of three or four: the top row wins; a tie at the top is a draw only when the SECOND row ties the first; a tie below the top changes nothing; a seat that dropped out has no row and is marked left") {
        Fake f;
        f.roster = 0x07;
        f.seat(0, 300, 4).seat(1, 300, 4).seat(2, 100, 0);    // two share the top
        HistoryRecord r = record_of(f);
        ASSERT_EQ(r.format, std::string("ffa3"));
        ASSERT_TRUE(r.draw && winners_of(r).empty());
        f.seat(0, 300, 4).seat(1, 200, 3).seat(2, 200, 3);    // a tie for second place
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "0" && row_order(r) == "0 1 2");
        f.seat(0, 100, 1).seat(1, 200, 3).seat(2, 200, 7);    // equal scores, kills decide the top
        r = record_of(f);
        ASSERT_TRUE(!r.draw && winners_of(r) == "2" && row_order(r) == "2 1 0");
        Fake g;                                               // four seats, the third dropped out on the way
        g.roster = 0x0F;
        g.dropped = 0x04;
        g.seat(0, 50, 1).seat(1, 80, 2).seat(2, 900, 40).seat(3, 10, 0);
        r = record_of(g);
        ASSERT_EQ(r.format, std::string("ffa4"));
        ASSERT_TRUE(r.rows.size() == 3 && row_order(r) == "1 0 3" && winners_of(r) == "1");
        ASSERT_TRUE(r.seats.size() == 4 && r.seats[2].left && !r.seats[2].winner && !r.seats[0].left);
        Fake alone;                                           // two of three dropped: the seat left wins (a row of its own)
        alone.roster = 0x07;
        alone.dropped = 0x06;
        alone.seat(0, 0, 0).seat(1, 500, 5).seat(2, 500, 5);
        r = record_of(alone);
        ASSERT_TRUE(r.rows.size() == 1 && !r.draw && winners_of(r) == "0");
    } TEST_END();

    TEST_CASE("HR5.3 A match of one seat has nobody to beat: no winner and no draw") {
        Fake f;
        f.roster = 0x01;
        f.seat(0, 500, 3);
        const HistoryRecord r = record_of(f);
        ASSERT_TRUE(r.format == "solo" && !r.draw && winners_of(r).empty() && !r.seats[0].winner);
    } TEST_END();

    TEST_CASE("HR6.1 Who played: \"Bot (Hard)\" is a computer of level Hard, \"Bot\" a computer without a level, anything else a person; a match without a person is computers_only; the format is how the match began; the fields of a seat are the engine's") {
        Fake f;
        f.roster = 0x0F;
        f.names = {{"Ann", "", "Bot (Hard)", "Bot"}};
        f.seat(0, 10, 1, 2, 3).seat(1, 20, 4, 5, 6).seat(2, 30, 7, 8, 9).seat(3, 40, 10, 11, 12);
        HistoryRecord r = record_of(f);
        ASSERT_TRUE(r.seats[0].name == "Ann" && r.seats[0].bot.empty());
        ASSERT_TRUE(r.seats[1].name.empty() && r.seats[1].bot.empty());                  // (an empty name stays empty: the pages show the color)
        ASSERT_TRUE(r.seats[2].name.empty() && r.seats[2].bot == "Hard" && r.seats[3].bot == "Bot");
        ASSERT_FALSE(r.computers_only);
        ASSERT_TRUE(r.seats[2].score == 30 && r.seats[2].killed == 7 && r.seats[2].lost == 8 && r.seats[2].hatched == 9 && r.seats[2].bombs_planted == 3 && r.seats[2].bombs_defused == 4 && r.seats[2].fires_lit == 5);
        ASSERT_TRUE(r.id == kId && r.ended_s == kT0 && r.map == "TINY.LVL" && r.game == "v0.12.0-test" && r.turns == 2400 && r.seconds() == 120 && r.finished && r.quitter == -1);
        Fake bots;
        bots.names = {{"Bot (Medium)", "Bot (Hard)", "", ""}};
        ASSERT_TRUE(record_of(bots).computers_only);
        Fake cut;
        cut.over = false;                                     // the recording stops before the rules ended the match
        cut.seat(0, 5, 1).seat(1, 3, 1);
        const HistoryRecord c = record_of(cut);
        ASSERT_TRUE(!c.finished && !c.draw && std::string(c.result()) == "cut");
        Fake one_v_one;
        ASSERT_EQ(record_of(one_v_one).format, std::string("1v1"));
    } TEST_END();

    TEST_CASE("HR7.1 rank_history_rows on its own: no rows, one row, and the order that equal rows keep") {
        std::vector<HistoryRow> none;
        ASSERT_FALSE(rank_history_rows("highest-score", -1, 255, none));
        std::vector<HistoryRow> one(1);
        one[0].seats = {2};
        ASSERT_FALSE(rank_history_rows("187", -1, 255, one));
        ASSERT_TRUE(one[0].winner);
        std::vector<HistoryRow> three(3);
        for (uint8_t k = 0; k < 3; ++k) {
            three[k].seats = {k};
            three[k].score = 10;
            three[k].killed = 10;
        }
        three[2].killed = 11;
        ASSERT_FALSE(rank_history_rows("highest-score", -1, 255, three));
        ASSERT_TRUE(three[0].seats[0] == 2 && three[0].winner && three[1].seats[0] == 0 && three[2].seats[0] == 1 && !three[1].winner);       // (equal rows keep the order they came in)
    } TEST_END();

    TEST_CASE("HR8.1 A record as JSON: the full form has the layout number, the bombs and fires, the mode and the result; the list's form leaves out the numbers of the page; details are given back as they were stored") {
        Fake f;
        f.mode = 1;
        f.seat(0, 5, 5).seat(1, 3, 3);
        HistoryRecord r = record_of(f);
        ctl::JsonValue lean = history_record_to_json(r, false);
        ASSERT_TRUE(lean.get("v").is_null() && lean.get("mode").str() == "187" && lean.get("result").str() == "decided" && lean.get("id").str() == kId);
        ASSERT_TRUE(lean.get("seats").at(0).get("bombs_planted").is_null() && lean.get("seats").at(0).get("colour").str() == "Green" && lean.get("seats").at(1).get("colour").str() == "Red");
        ASSERT_TRUE(lean.get("details").is_null() && lean.get("quitter").is_null() && lean.get("draw").as_bool_or(true) == false);
        r.details = "{\"events\":[1,2,3]}";
        ctl::JsonValue full = history_record_to_json(r, true);
        ASSERT_TRUE(full.get("v").as_int_or(0) == kHistoryVersion && full.get("seats").at(0).get("bombs_planted").as_int_or(0) == 1 && full.get("details").get("events").size() == 3);
        ASSERT_TRUE(history_record_to_json(r, false).get("details").is_null());
        r.details = "{not json";
        ASSERT_TRUE(history_record_to_json(r, true).get("details").is_null());           // (a damaged text is left out, not passed on)
        std::vector<HistoryRow> back;
        ASSERT_TRUE(history_rows_from_json(history_rows_to_json(r.rows), back) && back.size() == r.rows.size() && back[0].winner && back[0].seats == r.rows[0].seats && back[1].killed == r.rows[1].killed);
        ASSERT_FALSE(history_rows_from_json("{}", back));
        ASSERT_FALSE(history_rows_from_json("[{\"seats\":[]}]", back));
    } TEST_END();

    TEST_CASE("HR9.1 An id is the name of a recording without its extension, and only a name that the replay store could have made is one") {
        ASSERT_TRUE(valid_history_id("ants-TREASURE-20261008-143209Z") && valid_history_id("ants-TINY-20261008-143209Z-2"));
        ASSERT_FALSE(valid_history_id(""));
        ASSERT_FALSE(valid_history_id("ants-TINY-20261008-143209Z.antsrep"));
        ASSERT_FALSE(valid_history_id("../ants-TINY-20261008-143209Z"));
        ASSERT_FALSE(valid_history_id("ants-TINY-20261008-143209"));
        ASSERT_EQ(history_recording_file("ants-TINY-20261008-143209Z"), std::string("ants-TINY-20261008-143209Z.antsrep"));
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
