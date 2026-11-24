// Tests of the map of deaths of a match (docs/HEATMAPS.md "The map of deaths"): where each ant died, found by the replay pass (ants_replay/match_stats.hpp: the tile of every lost ant), packed into a field of its
// own with its own cap (ants_server/history_deaths.hpp), kept in its own table (db_schema.cpp, version 7) by the feeder and given back by GET /history/<id> under "deaths" and, added up for a game map, by
// GET /history/deaths?map=NAME. What is checked:
//   - the packing: every ant round trips (extreme tiles and seconds, every type, drowned, every team to blame and none), a text that is not a field is refused (DD1.x);
//   - the field: one ant after the other while the text fits kDeathsMaxBytes, else counts in squares of the smallest side that fits, every death still counted, the first and last kill kept as single ants, the
//     same text every time, the cap never passed however many ants and wherever they died (DD2.x);
//   - the collector: the tile of a lost ant is the one its last look showed (an independent observer on real recordings agrees), on the map, and the pass still only watches (DD3.x);
//   - the store: a match counted here has its field in its own table and in its record's JSON, next to the details; an older record gets no field (no backfill), a maker that throws costs nothing else, a text that is
//     not a field or is over its cap is not kept (the match is), the row goes with its match (DD4.x);
//   - the sum of a map: the matches of the newest file of that name, a name looked up among the known ones (case folded, never used as anything else), cached until the history changes, bounded (DD5.x);
//   - the route: the query is one key, every wrong query is a 400 with fixed words, an unknown map 404 without a word of the request in the answer, only a GET (DD6.x).
// The action of a match (docs/HEATMAPS.md "The activity curve": deaths, bombs, thefts, fires and hits in every 5 seconds, a sibling field with its own cap and table, ants_server/history_action.hpp) is tested here too, for it is
// found, kept and given back the way the map of deaths is (DD7.x):
//   - the collector: each kind is counted in the bucket of its own turn (turn / 100), the totals are the engine's own, an independent observer of the counters and the hit points agrees, the pass still only watches (DD7.1);
//   - the field: the letters, the count that is never more than 63, the bucket of 5 seconds and the buckets that double for a long match, no field for a match with none of the five, the reader refuses what is not a field (DD7.2 - DD7.4);
//   - the cap: never passed whatever the length of the match and the counts (DD7.5);
//   - the store: its own table, its own key in the record's JSON, an older record has none, a maker that throws costs nothing else, a text that is not a field or is over its cap is not kept, the row goes with its match, the
//     migration (DD7.6 - DD7.8).
// The recordings are real matches of the standard computer players on the shipped TINY map (history_fixtures.hpp).
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../common/test_assert.hpp"
#include "ants_replay/match_stats.hpp"
#include "ants_server/history_action.hpp"
#include "ants_server/history_deaths.hpp"
#include "ants_server/history_details.hpp"
#include "history_fixtures.hpp"

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

using replay::MatchStats;
using ctl::JsonValue;

// A counted match on a map of w x h tiles with the given kills (all four seats play)
MatchStats stats_of(uint32_t w, uint32_t h, std::vector<MatchStats::Kill> kills) {
    MatchStats s;
    s.ok = true;
    s.turns = 24000;
    s.roster = 0x0F;
    s.map_width = w;
    s.map_height = h;
    s.kills = std::move(kills);
    return s;
}

MatchStats::Kill kill_at(uint32_t turn, int32_t x, int32_t y, uint8_t victim, uint8_t killer, uint8_t type = 0, bool drowned = false) {
    MatchStats::Kill k;
    k.turn = turn;
    k.tile_x = x;
    k.tile_y = y;
    k.victim = victim;
    k.killer = killer;
    k.victim_type = type;
    k.drowned = drowned;
    return k;
}

// n kills spread over the map by a fixed generator (the same list every time)
std::vector<MatchStats::Kill> spread(size_t n, uint32_t w, uint32_t h, uint32_t seed) {
    uint32_t state = seed * 2654435761u + 12345u;                   // (xorshift32: the same numbers on every platform)
    const auto rng = [&state]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    std::vector<MatchStats::Kill> out;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t a = rng();
        const uint32_t b = rng();
        const uint32_t c = rng();
        out.push_back(kill_at(static_cast<uint32_t>(i) * 7u, static_cast<int32_t>(a % w), static_cast<int32_t>(b % h), static_cast<uint8_t>(c % 4), static_cast<uint8_t>((c >> 8) % 5 == 4 ? 255 : (c >> 8) % 4),
                              static_cast<uint8_t>((c >> 16) % 6), ((c >> 24) & 1u) != 0));
    }
    return out;
}

DeathsView view_of(const std::string& text) {
    DeathsView v;
    if (!parse_history_deaths(text, v)) v = DeathsView();
    return v;
}

struct Real {
    bool ok{false};
    replay::Replay rep;
    assets::LevelData level;
};

Real load(int k) {
    Real r;
    const std::vector<uint8_t>& bytes = match_file(k);
    std::string why;
    if (bytes.empty() || !replay::decode(bytes.data(), bytes.size(), r.rep, why)) return r;
    r.ok = replay::load_map(r.rep.head, maps_dir(), r.level, why);
    return r;
}

// Counts a real match in a rig and returns its id
std::string counted(Rig& rig, int k) {
    const std::string id = rig.keep(match_file(k));
    rig.drain(6);
    return id;
}

// A record that has the given field, for a store test that needs no recording
HistoryRecord synthetic(const std::string& id, const std::string& map, uint64_t hash, int64_t ended, const std::string& deaths) {
    HistoryRecord r;
    r.id = id;
    r.ended_s = ended;
    r.map = map;
    r.map_hash = hash;
    r.game = "v0.12.0-test";
    r.turns = 2400;
    r.finished = true;
    r.mode = kHistoryModeScore;
    r.format = "1v1";
    r.computers_only = true;
    for (uint8_t seat = 0; seat < 2; ++seat) {
        HistorySeat s;
        s.seat = seat;
        s.bot = "Hard";
        r.seats.push_back(s);
    }
    HistoryRow row;
    row.seats = {0};
    row.winner = true;
    r.rows.push_back(row);
    r.deaths = deaths;
    return r;
}

std::string field_at(uint32_t w, uint32_t h, std::vector<std::pair<int, int>> tiles, uint8_t team = 0) {
    std::vector<MatchStats::Kill> kills;
    uint32_t turn = 20;
    for (const auto& t : tiles) kills.push_back(kill_at(turn += 20, t.first, t.second, team, 1));
    return make_history_deaths(stats_of(w, h, kills));
}

// ---- the action of a match ----

// A counted match of `turns` turns with the given events: events[kind] is a list of (bucket of 5 seconds, count)
MatchStats action_stats(uint32_t turns, std::vector<std::vector<std::pair<size_t, uint32_t>>> events) {
    MatchStats s;
    s.ok = true;
    s.turns = turns;
    s.roster = 0x0F;
    for (size_t k = 0; k < events.size() && k < MatchStats::kActionKinds; ++k) {
        for (const auto& e : events[k]) {
            if (s.action[k].size() <= e.first) s.action[k].resize(e.first + 1);
            s.action[k][e.first] += e.second;
        }
    }
    return s;
}

ActionView action_view(const std::string& text) {
    ActionView v;
    if (!parse_history_action(text, v)) v = ActionView();
    return v;
}

constexpr size_t kDeath = static_cast<size_t>(MatchStats::Action::Death);
constexpr size_t kBomb = static_cast<size_t>(MatchStats::Action::Bomb);
constexpr size_t kTheft = static_cast<size_t>(MatchStats::Action::Theft);
constexpr size_t kFire = static_cast<size_t>(MatchStats::Action::Fire);
constexpr size_t kHit = static_cast<size_t>(MatchStats::Action::Hit);

uint64_t sum_of(const std::vector<uint32_t>& v) {
    uint64_t n = 0;
    for (const uint32_t x : v) n += x;
    return n;
}

// The head of a field as the layout says it, for a text that is not one
std::string action_text(int64_t v, int64_t s, int64_t n, const std::string& death, const std::string& bomb, const std::string& theft, const std::string& fire, const std::string& hit) {
    return "{\"v\":" + std::to_string(v) + ",\"s\":" + std::to_string(s) + ",\"n\":" + std::to_string(n) + ",\"death\":\"" + death + "\",\"bomb\":\"" + bomb + "\",\"theft\":\"" + theft + "\",\"fire\":\"" + fire +
           "\",\"hit\":\"" + hit + "\"}";
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n  THE MAP OF DEATHS: WHERE THE ANTS DIED\n=======================================================\n";

    // ---- the packing ----------------------------------------------------------------------------------------------------------------------

    TEST_CASE("DD1.1 Every ant round trips through the packed text: extreme tiles and seconds, every type, drowned or not, every team to blame and nobody; nine letters each, of the 64 letters of base64url") {
        std::vector<Death> in;
        for (uint8_t type = 0; type <= 5; ++type) {
            for (uint8_t killer : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}, static_cast<uint8_t>(kDeathsNone)}) {
                Death d;
                d.x = type * 700u + killer;
                d.y = 4095u - type * 11u;
                d.second = type * 40000u + killer;
                d.team = static_cast<uint8_t>((type + killer) % 4);
                d.type = type;
                d.drowned = ((type + killer) & 1u) != 0;
                d.killer = killer;
                in.push_back(d);
            }
        }
        Death top;
        top.x = 4095;
        top.y = 4095;
        top.second = 262143;
        top.team = 3;
        top.type = 5;
        top.drowned = true;
        top.killer = 3;
        in.push_back(top);
        const std::string packed = pack_deaths(in);
        ASSERT_EQ(packed.size(), in.size() * 9);
        for (const char c : packed) ASSERT_TRUE((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_');
        std::vector<Death> out;
        ASSERT_TRUE(unpack_deaths(packed, out) && out.size() == in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            ASSERT_TRUE(out[i].x == in[i].x && out[i].y == in[i].y && out[i].second == in[i].second && out[i].team == in[i].team && out[i].type == in[i].type);
            ASSERT_TRUE(out[i].drowned == in[i].drowned && out[i].killer == in[i].killer);
        }
        ASSERT_TRUE(unpack_deaths("", out) && out.empty());
    } TEST_END();

    TEST_CASE("DD1.2 A text that is not packed ants is refused: a length that is not a multiple of nine, a letter outside the 64, a type above 5, a team to blame of 4 to 6; the packer clamps what does not fit its bits") {
        std::vector<Death> out;
        const std::string good = pack_deaths({Death{}});
        ASSERT_TRUE(unpack_deaths(good, out));
        ASSERT_FALSE(unpack_deaths(good + "A", out));
        ASSERT_FALSE(unpack_deaths(good.substr(0, 8), out));
        for (const char bad : {'+', '/', '=', ' ', '.', '\n', '"', '\\', '<'}) {
            std::string t = good;
            t[3] = bad;
            ASSERT_FALSE(unpack_deaths(t, out));
        }
        Death d;
        d.type = 5;
        std::string t = pack_deaths({d});
        ASSERT_TRUE(unpack_deaths(t, out));
        for (uint32_t type : {6u, 7u}) {
            Death e;
            std::string p = pack_deaths({e});
            const uint32_t f = (type << 2);
            p[7] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"[f & 63u];
            ASSERT_FALSE(unpack_deaths(p, out));
        }
        for (uint32_t killer : {4u, 5u, 6u}) {
            Death e;
            std::string p = pack_deaths({e});
            const uint32_t f = killer << 6;
            p[7] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"[f & 63u];
            p[8] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"[(f >> 6) & 63u];
            ASSERT_FALSE(unpack_deaths(p, out));
        }
        Death big;
        big.x = 70000;
        big.y = 99999;
        big.second = 9999999;
        ASSERT_TRUE(unpack_deaths(pack_deaths({big}), out) && out[0].x == 4095 && out[0].y == 4095 && out[0].second == 262143);
    } TEST_END();

    TEST_CASE("DD1.3 Squares round trip (x, y, team, drowned, count; eight letters each) and a count of 0 or a flag above 7 is refused") {
        std::vector<DeathSquare> in = {{0, 0, 0, false, 1}, {4095, 4095, 3, true, 262143}, {17, 9, 2, false, 40}, {3, 5, 1, true, 2}};
        const std::string packed = pack_squares(in);
        ASSERT_EQ(packed.size(), in.size() * 8);
        std::vector<DeathSquare> out;
        ASSERT_TRUE(unpack_squares(packed, out) && out.size() == in.size());
        for (size_t i = 0; i < in.size(); ++i) ASSERT_TRUE(out[i].x == in[i].x && out[i].y == in[i].y && out[i].team == in[i].team && out[i].drowned == in[i].drowned && out[i].count == in[i].count);
        ASSERT_FALSE(unpack_squares(packed + "AAAAAAA", out));
        ASSERT_FALSE(unpack_squares(pack_squares({DeathSquare{1, 1, 0, false, 0}}), out));
        std::string t = packed;
        t[4] = 'I';                                       // flags 8: above 7
        ASSERT_FALSE(unpack_squares(t, out));
        t = packed;
        t[2] = '!';
        ASSERT_FALSE(unpack_squares(t, out));
        ASSERT_TRUE(unpack_squares(pack_squares({DeathSquare{1, 1, 0, false, 99999999}}), out) && out[0].count == 262143);       // (clamped to its 18 bits)
    } TEST_END();

    // ---- the field of a match -----------------------------------------------------------------------------------------------------------------

    TEST_CASE("DD2.1 A match with no ant lost, a pass that failed, and a grid that is not known have no field at all (an empty text); a field is the layout of the header") {
        ASSERT_EQ(make_history_deaths(stats_of(60, 60, {})), std::string());
        MatchStats bad = stats_of(60, 60, {kill_at(100, 1, 1, 0, 1)});
        bad.ok = false;
        ASSERT_EQ(make_history_deaths(bad), std::string());
        ASSERT_EQ(make_history_deaths(stats_of(0, 60, {kill_at(100, 1, 1, 0, 1)})), std::string());
        ASSERT_EQ(make_history_deaths(stats_of(60, 0, {kill_at(100, 1, 1, 0, 1)})), std::string());
        ASSERT_EQ(make_history_deaths(stats_of(5000, 60, {kill_at(100, 1, 1, 0, 1)})), std::string());          // (a tile of that map has no name in 12 bits: no field, never a wrong one)
        ASSERT_EQ(make_history_deaths(stats_of(4097, 60, {kill_at(100, 1, 1, 0, 1)})), std::string());          // (4,096 tiles is the most that x can name: 0 to 4,095)
        ASSERT_EQ(make_history_deaths(stats_of(60, 4097, {kill_at(100, 1, 1, 0, 1)})), std::string());
        ASSERT_TRUE(view_of(make_history_deaths(stats_of(4096, 4096, {kill_at(100, 4095, 4095, 0, 1)}))).ants.size() == 1);
        const DeathsView wide = view_of(make_history_deaths(stats_of(100, 81, {kill_at(100, 99, 80, 1, 2)})));      // (a map that is not square: the width is the width)
        ASSERT_TRUE(wide.width == 100 && wide.height == 81 && wide.ants.size() == 1 && wide.ants[0].x == 99 && wide.ants[0].y == 80);
        const std::string text = make_history_deaths(stats_of(60, 40, {kill_at(1999, 7, 9, 2, 3, 4, true)}));
        const JsonValue j = parse_text(text);
        ASSERT_TRUE(j.get("v").as_int_or(0) == kDeathsVersion && j.get("w").as_int_or(0) == 60 && j.get("h").as_int_or(0) == 40 && j.get("n").as_int_or(0) == 1 && j.get("k").str() == "x");
        ASSERT_EQ(j.get("d").str().size(), size_t{9});
        const DeathsView v = view_of(text);
        ASSERT_TRUE(!v.squares && v.ants.size() == 1 && v.total == 1 && v.width == 60 && v.height == 40);
        ASSERT_TRUE(v.ants[0].x == 7 && v.ants[0].y == 9 && v.ants[0].second == 99 && v.ants[0].team == 2 && v.ants[0].killer == 3 && v.ants[0].type == 4 && v.ants[0].drowned);       // (the second is the turn / 20, rounded down)
    } TEST_END();

    TEST_CASE("DD2.2 Each ant that died is in the field in the order it died, with its tile, second, team, type, whether it drowned and who it was killed by; nobody to blame is 7; a type above 5 is a Worker; a tile off the map is put on its edge") {
        std::vector<MatchStats::Kill> kills = {kill_at(40, 0, 0, 0, 255), kill_at(61, 59, 59, 1, 0, 1), kill_at(200, 30, 31, 2, 3, 5, true), kill_at(201, -4, 99, 3, 1, 9), kill_at(205, 12, 12, 0, 255, 3, true)};
        const DeathsView v = view_of(make_history_deaths(stats_of(60, 60, kills)));
        ASSERT_EQ(v.ants.size(), size_t{5});
        ASSERT_TRUE(v.ants[0].second == 2 && v.ants[0].killer == kDeathsNone && v.ants[0].team == 0 && !v.ants[0].drowned);
        ASSERT_TRUE(v.ants[1].x == 59 && v.ants[1].y == 59 && v.ants[1].second == 3 && v.ants[1].killer == 0 && v.ants[1].type == 1 && v.ants[1].team == 1);
        ASSERT_TRUE(v.ants[2].x == 30 && v.ants[2].y == 31 && v.ants[2].type == 5 && v.ants[2].drowned && v.ants[2].killer == 3 && v.ants[2].team == 2);
        ASSERT_TRUE(v.ants[3].x == 0 && v.ants[3].y == 59 && v.ants[3].type == 0 && v.ants[3].team == 3);
        ASSERT_TRUE(v.ants[4].drowned && v.ants[4].killer == kDeathsNone && v.ants[4].type == 3);
        for (size_t i = 1; i < v.ants.size(); ++i) ASSERT_GE(v.ants[i].second, v.ants[i - 1].second);
    } TEST_END();

    TEST_CASE("DD2.3 The field never passes its cap: ants one by one while the text fits (the 328th fits, the 329th makes squares), and the busiest real match, 34 deaths, is about 270 letters") {
        const std::string real = make_history_deaths(stats_of(60, 60, spread(34, 60, 60, 5)));
        ASSERT_TRUE(real.size() < 400 && real.size() > 300 - 40);
        DeathsView v = view_of(real);
        ASSERT_TRUE(!v.squares && v.ants.size() == 34);
        size_t last_exact = 0;
        for (size_t n = 300; n <= 360; ++n) {
            const std::string text = make_history_deaths(stats_of(60, 60, spread(n, 60, 60, 9)));
            ASSERT_TRUE(!text.empty() && text.size() <= kDeathsMaxBytes);
            if (!view_of(text).squares) last_exact = n;
        }
        ASSERT_TRUE(last_exact >= 320 && last_exact <= 330);
        ASSERT_TRUE(view_of(make_history_deaths(stats_of(60, 60, spread(last_exact, 60, 60, 9)))).ants.size() == last_exact);
        ASSERT_TRUE(view_of(make_history_deaths(stats_of(60, 60, spread(last_exact + 1, 60, 60, 9)))).squares);
        ASSERT_TRUE(make_history_deaths(stats_of(60, 60, spread(last_exact, 60, 60, 9))).size() <= kDeathsMaxBytes);
        ASSERT_TRUE(make_history_deaths(stats_of(60, 60, spread(last_exact, 60, 60, 9))).size() > kDeathsMaxBytes - 20);       // (the cap is the cap of the whole text: nothing is wasted)
    } TEST_END();

    TEST_CASE("DD2.4 More ants than fit make squares of the smallest side that fits: every death is still counted (the sum of the counts is n, per team and per cause), the squares are 8 letters each and sorted, the first and last kill are single ants") {
        std::vector<MatchStats::Kill> kills = spread(940, 512, 512, 3);
        const MatchStats st = stats_of(512, 512, kills);
        const std::string text = make_history_deaths(st);
        ASSERT_TRUE(!text.empty() && text.size() <= kDeathsMaxBytes);
        const JsonValue j = parse_text(text);
        ASSERT_TRUE(j.get("k").str() == "b" && j.get("n").as_int_or(0) == 940 && j.get("s").as_int_or(0) >= 2 && j.get("w").as_int_or(0) == 512);
        const DeathsView v = view_of(text);
        ASSERT_TRUE(v.squares && v.total == 940 && v.side == static_cast<uint32_t>(j.get("s").as_int_or(0)));
        uint64_t sum = 0, by_team[4] = {0, 0, 0, 0}, drowned = 0;
        for (const DeathSquare& c : v.cells) {
            sum += c.count;
            by_team[c.team] += c.count;
            drowned += c.drowned ? c.count : 0;
            ASSERT_TRUE(c.x * v.side < 512 && c.y * v.side < 512 && c.count >= 1);
        }
        uint64_t want_team[4] = {0, 0, 0, 0}, want_drowned = 0;
        for (const MatchStats::Kill& k : kills) {
            ++want_team[k.victim];
            want_drowned += k.drowned ? 1 : 0;
        }
        ASSERT_EQ(sum, uint64_t{940});
        for (int t = 0; t < 4; ++t) ASSERT_EQ(by_team[t], want_team[t]);
        ASSERT_EQ(drowned, want_drowned);
        for (size_t i = 1; i < v.cells.size(); ++i) {
            const DeathSquare &a = v.cells[i - 1], &b = v.cells[i];
            ASSERT_TRUE(std::make_tuple(a.y, a.x, a.team, a.drowned) < std::make_tuple(b.y, b.x, b.team, b.drowned));
        }
        // the smallest side: squares of half the side would not have fitted
        if (v.side > 2) {
            std::vector<Death> ants;
            for (const MatchStats::Kill& k : kills) {
                Death d;
                d.x = static_cast<uint32_t>(k.tile_x);
                d.y = static_cast<uint32_t>(k.tile_y);
                d.team = k.victim;
                d.drowned = k.drowned;
                ants.push_back(d);
            }
            std::map<std::tuple<uint32_t, uint32_t, uint8_t, bool>, int> cells;
            for (const Death& d : ants) ++cells[std::make_tuple(d.y / (v.side / 2), d.x / (v.side / 2), d.team, d.drowned)];
            ASSERT_GT(cells.size() * 8 + 90, kDeathsMaxBytes);                          // (the packed text of squares of half the side, with the least header there is, is over the cap)
        }
        // the first and the last kill that a team is to blame for
        const MatchStats::Kill* first = nullptr;
        const MatchStats::Kill* last = nullptr;
        for (const MatchStats::Kill& k : kills) {
            if (k.killer >= 4) continue;
            if (!first) first = &k;
            last = &k;
        }
        ASSERT_TRUE(first && j.get("f").is_string() && j.get("l").is_string());
        std::vector<Death> f, l;
        ASSERT_TRUE(unpack_deaths(j.get("f").str(), f) && unpack_deaths(j.get("l").str(), l) && f.size() == 1 && l.size() == 1);
        ASSERT_TRUE(f[0].x == static_cast<uint32_t>(first->tile_x) && f[0].second == first->turn / 20 && l[0].x == static_cast<uint32_t>(last->tile_x) && l[0].second == last->turn / 20 && f[0].killer < 4 && l[0].killer < 4);
        ASSERT_EQ(make_history_deaths(st), text);                                           // (the same text every time)
    } TEST_END();

    TEST_CASE("DD2.4b Squares are the smallest that fit, in every case: over many matches and maps the text is never over the cap, and squares of half the side would have been (with the shortest header there is); the first and last kill are the first and last that a team is to blame for, whoever died before") {
        size_t checked = 0, halved = 0;
        for (const uint32_t side : {64u, 128u, 256u, 512u, 1024u}) {
            for (size_t n = 330; n <= 1900; n += 61) {
                const MatchStats st = stats_of(side, side, spread(n, side, side, static_cast<uint32_t>(n) + side));
                const std::string text = make_history_deaths(st);
                ASSERT_TRUE(!text.empty() && text.size() <= kDeathsMaxBytes);
                const DeathsView v = view_of(text);
                ASSERT_TRUE(v.squares && v.total == n);
                ++checked;
                if (v.side > 2) {
                    std::map<std::tuple<uint32_t, uint32_t, uint8_t, bool>, int> cells;
                    for (const MatchStats::Kill& k : st.kills) ++cells[std::make_tuple(static_cast<uint32_t>(k.tile_y) / (v.side / 2), static_cast<uint32_t>(k.tile_x) / (v.side / 2), k.victim, k.drowned)];
                    ASSERT_GT(cells.size() * 8 + 90, kDeathsMaxBytes);                   // (the packed text of squares of half the side is over the cap even with the shortest header)
                    ++halved;
                }
            }
        }
        ASSERT_TRUE(checked > 50 && halved > 10);
        // the first and last kill that a team is to blame for: the ants that died before the first one, and after the last one, had nobody to blame
        std::vector<MatchStats::Kill> kills = spread(600, 512, 512, 21);
        for (MatchStats::Kill& k : kills) k.killer = 255;
        kills[3].killer = 2;
        kills[500].killer = 1;
        const std::string text = make_history_deaths(stats_of(512, 512, kills));
        const JsonValue j = parse_text(text);
        std::vector<Death> f, l;
        ASSERT_TRUE(j.get("k").str() == "b" && unpack_deaths(j.get("f").str(), f) && unpack_deaths(j.get("l").str(), l) && f.size() == 1 && l.size() == 1);
        ASSERT_TRUE(f[0].second == kills[3].turn / 20 && f[0].killer == 2 && l[0].second == kills[500].turn / 20 && l[0].killer == 1);
        for (MatchStats::Kill& k : kills) k.killer = 255;                                  // (nobody to blame for any: no first and no last)
        const JsonValue none = parse_text(make_history_deaths(stats_of(512, 512, kills)));
        ASSERT_TRUE(none.get("k").str() == "b" && !none.has("f") && !none.has("l"));
    } TEST_END();

    TEST_CASE("DD2.5 The cap holds for the worst case: any number of ants, on any map up to 2,048 a side, spread over every tile, all four teams, drowned or not; the text is a field that parses and counts every ant") {
        for (const uint32_t side : {8u, 60u, 256u, 2048u}) {
            for (const size_t n : {330u, 331u, 1000u, 5000u, 40000u}) {
                const std::string text = make_history_deaths(stats_of(side, side, spread(n, side, side, side + static_cast<uint32_t>(n))));
                ASSERT_TRUE(!text.empty() && text.size() <= kDeathsMaxBytes);
                const DeathsView v = view_of(text);
                ASSERT_TRUE(v.total == n && v.width == side && v.height == side);
                uint64_t sum = v.squares ? 0 : v.ants.size();
                for (const DeathSquare& c : v.cells) sum += c.count;
                ASSERT_EQ(sum, uint64_t{n});
            }
        }
        // every tile of an 8 x 8 map holds an ant of each team that drowned and each that did not: 512 kinds of ant, far more than the text keeps one by one
        std::vector<MatchStats::Kill> all;
        for (int t = 0; t < 4; ++t) {
            for (int dr = 0; dr < 2; ++dr) {
                for (int y = 0; y < 8; ++y) {
                    for (int x = 0; x < 8; ++x) all.push_back(kill_at(static_cast<uint32_t>(all.size()), x, y, static_cast<uint8_t>(t), 255, 0, dr != 0));
                }
            }
        }
        const std::string text = make_history_deaths(stats_of(8, 8, all));
        ASSERT_TRUE(text.size() <= kDeathsMaxBytes && view_of(text).total == all.size());
    } TEST_END();

    TEST_CASE("DD2.6 The drowned flag is kept in both forms, so that one switch of the page can hide those ants; and the field is plain letters: nothing in it needs an escape") {
        std::vector<MatchStats::Kill> few = {kill_at(40, 1, 1, 0, 1, 0, true), kill_at(60, 2, 2, 0, 1, 0, false)};
        const DeathsView a = view_of(make_history_deaths(stats_of(60, 60, few)));
        ASSERT_TRUE(a.ants.size() == 2 && a.ants[0].drowned && !a.ants[1].drowned);
        std::vector<MatchStats::Kill> many = spread(900, 60, 60, 4);
        const DeathsView b = view_of(make_history_deaths(stats_of(60, 60, many)));
        uint64_t drowned = 0, want = 0;
        for (const DeathSquare& c : b.cells) drowned += c.drowned ? c.count : 0;
        for (const MatchStats::Kill& k : many) want += k.drowned ? 1 : 0;
        ASSERT_TRUE(b.squares && drowned == want && want > 0 && want < 900);
        const std::string text = make_history_deaths(stats_of(60, 60, many));
        ASSERT_EQ(text.find('\\'), std::string::npos);
    } TEST_END();

    TEST_CASE("DD2.7 A text that is not a field is refused by the reader: another version, a missing or wrong number, an unknown kind, a packed text of the wrong length, a size that no map has") {
        const std::string good = field_at(60, 60, {{1, 1}, {2, 2}});
        ASSERT_TRUE(view_of(good).total == 2);
        const auto refused = [](const std::string& t) {
            DeathsView v;
            return !parse_history_deaths(t, v);
        };
        ASSERT_TRUE(refused(""));
        ASSERT_TRUE(refused("[]"));
        ASSERT_TRUE(refused("{\"v\":2,\"w\":60,\"h\":60,\"n\":1,\"k\":\"x\",\"d\":\"AAAAAAAAA\"}"));
        ASSERT_TRUE(refused("{\"v\":1,\"w\":0,\"h\":60,\"n\":1,\"k\":\"x\",\"d\":\"AAAAAAAAA\"}"));
        ASSERT_TRUE(refused("{\"v\":1,\"w\":60,\"h\":9999,\"n\":1,\"k\":\"x\",\"d\":\"AAAAAAAAA\"}"));
        ASSERT_TRUE(refused("{\"v\":1,\"w\":60,\"h\":60,\"n\":0,\"k\":\"x\",\"d\":\"\"}"));
        ASSERT_TRUE(refused("{\"v\":1,\"w\":60,\"h\":60,\"n\":1,\"k\":\"q\",\"d\":\"AAAAAAAAA\"}"));
        ASSERT_TRUE(refused("{\"v\":1,\"w\":60,\"h\":60,\"n\":1,\"k\":\"x\",\"d\":\"AAAAAAAA\"}"));
        ASSERT_TRUE(refused("{\"v\":1,\"w\":60,\"h\":60,\"n\":1,\"k\":\"b\",\"d\":\"AAAAAAAA\"}"));            // (squares with no side)
        ASSERT_TRUE(refused("{\"v\":1,\"w\":60,\"h\":60,\"n\":1,\"k\":\"x\",\"d\":5}"));
        ASSERT_FALSE(refused("{\"v\":1,\"w\":60,\"h\":60,\"n\":1,\"k\":\"x\",\"d\":\"AAAAAAAAA\"}"));
    } TEST_END();

    // ---- the collector ------------------------------------------------------------------------------------------------------------------------

    TEST_CASE("DD3.1 The tile of a lost ant is the tile of its last look: an independent observer on real recordings finds the same ants, the same turns and the same tiles, all on the map; the grid's size is the engine's") {
        for (int k = 0; k < 6; ++k) {
            Real m = load(k);
            if (!m.ok) {
                std::cout << "SKIPPED (no map)\n";
                return;
            }
            struct Last {
                bool alive{false};
                uint8_t player{255};
                int32_t x{0}, y{0};
                uint32_t seen{0};
            };
            std::vector<Last> ants;
            struct Gone {
                uint32_t turn;
                int32_t x, y;
                uint8_t player;
            };
            std::vector<Gone> gone;
            uint32_t w = 0, h = 0;
            replay::Hooks hooks;
            hooks.after_tick = [&](uint32_t turns, sim::SimulationEngine& engine) {
                w = engine.grid().width();
                h = engine.grid().height();
                for (const sim::AntSnapshot& s : engine.get_world_state().ants) {
                    if (s.id >= ants.size()) ants.resize(std::max<size_t>(ants.size() * 2, size_t{s.id} + 1));
                    Last& a = ants[s.id];
                    a.seen = turns;
                    a.alive = true;
                    a.player = s.player_id;
                    a.x = s.tile_x;
                    a.y = s.tile_y;
                }
                for (size_t id = 1; id < ants.size(); ++id) {
                    Last& a = ants[id];
                    if (!a.alive || a.seen == turns) continue;
                    a.alive = false;
                    if (a.player < 4) gone.push_back(Gone{turns, a.x, a.y, a.player});
                }
            };
            const replay::Outcome plain = replay::play(m.rep, m.level, hooks);
            const replay::StatsPlay p = replay::play_with_stats(m.rep, m.level);
            ASSERT_TRUE(plain.ok && p.outcome.ok && p.stats.ok && p.outcome.hash == plain.hash);
            ASSERT_TRUE(p.stats.map_width == w && p.stats.map_height == h && w > 0 && h > 0);
            ASSERT_EQ(p.stats.kills.size(), gone.size());
            for (size_t i = 0; i < gone.size(); ++i) {
                const MatchStats::Kill& kl = p.stats.kills[i];
                ASSERT_TRUE(kl.turn == gone[i].turn && kl.tile_x == gone[i].x && kl.tile_y == gone[i].y && kl.victim == gone[i].player);
                ASSERT_TRUE(kl.tile_x >= 0 && static_cast<uint32_t>(kl.tile_x) < w && kl.tile_y >= 0 && static_cast<uint32_t>(kl.tile_y) < h);
            }
        }
    } TEST_END();

    TEST_CASE("DD3.2 A real match gives a field that agrees with the scoreboard: n is the ants its seats lost, every ant is on the map, the seconds do not run backwards and none is after the end") {
        for (int k = 0; k < 6; ++k) {
            Real m = load(k);
            if (!m.ok) {
                std::cout << "SKIPPED (no map)\n";
                return;
            }
            const replay::StatsPlay p = replay::play_with_stats(m.rep, m.level);
            ASSERT_TRUE(p.outcome.ok && p.stats.ok);
            uint32_t lost = 0;
            for (size_t s = 0; s < sim::MAX_PLAYERS; ++s) lost += p.outcome.result.stats[s].friendly_lost;
            const std::string text = make_history_deaths(p.stats);
            if (p.stats.kills.empty()) {
                ASSERT_EQ(text, std::string());
                continue;
            }
            const DeathsView v = view_of(text);
            ASSERT_TRUE(v.total == p.stats.kills.size() && v.total == lost && v.width == p.stats.map_width && v.height == p.stats.map_height);
            ASSERT_TRUE(!v.squares && v.ants.size() == v.total);
            for (size_t i = 0; i < v.ants.size(); ++i) {
                ASSERT_TRUE(v.ants[i].x < v.width && v.ants[i].y < v.height && v.ants[i].second <= p.outcome.turns / 20 + 1);
                if (i > 0) ASSERT_GE(v.ants[i].second, v.ants[i - 1].second);
            }
        }
    } TEST_END();

    TEST_CASE("DD3.3 A map that is not square: the collector gives the width as the width and the height as the height (a recording on a community map of 100 x 81 tiles), and the field of its deaths names both") {
        const std::string community = std::string(ANTS_SOURCE_DIR) + "/Community-Maps";
        MatchSpec spec;
        spec.file = "Easy jungle.lvl";
        spec.dir = community;
        spec.seed = 3;
        spec.turns = 700;
        spec.names = {"Bot (Hard)", "Bot (Hard)", "Bot (Hard)", "Bot (Hard)"};
        const std::vector<uint8_t> bytes = record_match(spec);
        if (bytes.empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        replay::Replay rep;
        assets::LevelData level;
        std::string why;
        ASSERT_TRUE(replay::decode(bytes.data(), bytes.size(), rep, why) && replay::load_map(rep.head, std::vector<std::string>{maps_dir(), community}, level, why));
        const replay::StatsPlay p = replay::play_with_stats(rep, level);
        ASSERT_TRUE(p.outcome.ok && p.stats.ok);
        ASSERT_TRUE(p.stats.map_width == 100 && p.stats.map_height == 81);
        MatchStats st = p.stats;
        st.kills.push_back(kill_at(700, 99, 80, 1, 2));                                    // (a death in the far corner, to see both numbers in the field)
        const DeathsView v = view_of(make_history_deaths(st));
        ASSERT_TRUE(v.width == 100 && v.height == 81 && !v.ants.empty() && v.ants.back().x == 99 && v.ants.back().y == 80);
    } TEST_END();

    // ---- the store ----------------------------------------------------------------------------------------------------------------------------

    TEST_CASE("DD4.1 A match that the feeder counts has its map of deaths next to its details, from the same play: in its own table, in the record's JSON under \"deaths\", and the details are what they were") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string id = counted(rig, 0);
        HistoryRecord rec;
        ASSERT_TRUE(rig.history.find(id, rec) && !rec.details.empty() && !rec.deaths.empty() && rig.feeder.counted() == 1);
        Real m = load(0);
        const replay::StatsPlay p = replay::play_with_stats(m.rep, m.level);
        ASSERT_EQ(rec.deaths, make_history_deaths(p.stats));
        ASSERT_EQ(rec.details, make_history_details(p.stats, rec));
        ASSERT_TRUE(rec.details.size() <= kDetailsMaxBytes && rec.deaths.size() <= kDeathsMaxBytes);
        const JsonValue j = history_record_to_json(rec, true);
        ASSERT_TRUE(j.get("deaths").get("k").is_string() && j.get("deaths").get("n").as_int_or(0) == static_cast<int64_t>(p.stats.kills.size()) && j.get("details").get("highlights").is_object());
        ASSERT_FALSE(history_record_to_json(rec, false).has("deaths"));                      // (the list leaves it out, like the details)
        const HistoryPage page = rig.history.query(HistoryQuery(), [](const std::string&) { return Recording::Watch; });
        ASSERT_TRUE(page.ok && page.items.size() == 1 && page.items[0].deaths.empty());       // (and does not read it)
    } TEST_END();

    TEST_CASE("DD4.2 An older record without details gets its details from its recording and NO map of deaths (no backfill: older matches do not matter); a match that nobody lost an ant in has none either") {
        if (match_file(1).empty() || match_file(2).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string older = rig.keep(match_file(1));
        const Played pl = play_file(match_file(1));
        ASSERT_TRUE(pl.ok);
        std::string why;
        ASSERT_TRUE(rig.history.put(make_history_record(older, rig.now, pl.rep, pl.outcome), why));
        rig.drain(8);
        HistoryRecord rec;
        ASSERT_TRUE(rig.history.find(older, rec) && !rec.details.empty() && rig.feeder.backfilled() == 1);
        ASSERT_TRUE(rec.deaths.empty());
        std::string sum;
        ASSERT_TRUE(rig.history.map_deaths("TINY.LVL", sum) == HistoryStore::DeathsSum::Unknown);
        // a match with no lost ant
        HistoryRecord quiet = synthetic(id_at(kT0 + 5), "TINY.LVL", 7, kT0 + 5, std::string());
        ASSERT_TRUE(rig.history.put(quiet, why) && rig.history.find(quiet.id, rec) && rec.deaths.empty());
    } TEST_END();

    TEST_CASE("DD4.3 The counters and the details of a match are written whatever the pass of the map of deaths does: a maker that throws leaves no map, a line in the log and the record") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        rig.feeder.set_deaths_maker([](const replay::MatchStats&) -> std::string { throw std::out_of_range("the map broke"); });       // (not a runtime_error: the feeder holds back any exception)
        const std::string id = counted(rig, 1);
        HistoryRecord rec;
        ASSERT_TRUE(rig.history.find(id, rec) && rec.deaths.empty() && !rec.details.empty() && rec.details != history_details_marker() && rec.seats.size() == 2 && rig.feeder.counted() == 1 && rig.feeder.left_out() == 0);
        ASSERT_EQ(lines_with(rig.history.take_notes(), "the map broke"), size_t{1});
        Rig none;
        ASSERT_TRUE(none.open());
        none.feeder.set_deaths_maker(nullptr);
        const std::string id2 = counted(none, 1);
        ASSERT_TRUE(none.history.find(id2, rec) && rec.deaths.empty() && !rec.details.empty());
    } TEST_END();

    TEST_CASE("DD4.4 A text that is not a field, or is over its cap, is not kept (the match is, without a map); a field is replaced with its match; the row goes with the match, with and without a marker") {
        Rig rig;
        ASSERT_TRUE(rig.open());
        std::string why;
        const std::string field = field_at(60, 60, {{3, 4}, {5, 6}});
        HistoryRecord a = synthetic(id_at(kT0 + 10), "TINY.LVL", 5, kT0 + 10, field);
        ASSERT_TRUE(rig.history.put(a, why));
        HistoryRecord got;
        ASSERT_TRUE(rig.history.find(a.id, got) && got.deaths == field);
        HistoryRecord junk = synthetic(id_at(kT0 + 20), "TINY.LVL", 5, kT0 + 20, "not a field");
        ASSERT_TRUE(rig.history.put(junk, why) && rig.history.find(junk.id, got) && got.deaths.empty() && got.seats.size() == 2);
        HistoryRecord big = synthetic(id_at(kT0 + 30), "TINY.LVL", 5, kT0 + 30, field + std::string(kDeathsMaxBytes, ' '));
        ASSERT_TRUE(rig.history.put(big, why) && rig.history.find(big.id, got) && got.deaths.empty());
        HistoryRecord bad_pack = synthetic(id_at(kT0 + 40), "TINY.LVL", 5, kT0 + 40, "{\"v\":1,\"w\":60,\"h\":60,\"n\":1,\"k\":\"x\",\"d\":\"!!!!!!!!!\"}");
        ASSERT_TRUE(rig.history.put(bad_pack, why) && rig.history.find(bad_pack.id, got) && got.deaths.empty());
        a.deaths.clear();                                                                       // (kept again without it: the old row goes)
        ASSERT_TRUE(rig.history.put(a, why) && rig.history.find(a.id, got) && got.deaths.empty());
        std::string sum;
        ASSERT_TRUE(rig.history.map_deaths("TINY.LVL", sum) == HistoryStore::DeathsSum::Unknown);
        a.deaths = field;
        ASSERT_TRUE(rig.history.put(a, why) && rig.history.map_deaths("TINY.LVL", sum) == HistoryStore::DeathsSum::Ok);
        ASSERT_TRUE(rig.history.remove(a.id, true, why) == HistoryStore::Removal::Done);
        ASSERT_TRUE(rig.history.map_deaths("TINY.LVL", sum) == HistoryStore::DeathsSum::Unknown);       // (the row went with its match)
        ASSERT_TRUE(rig.history.put(a, why) && rig.history.find(a.id, got) && got.deaths == field);        // (kept again: the marker goes, the row is back)
    } TEST_END();

    TEST_CASE("DD4.5 The map of deaths is schema version 7 and a database of version 6 gets the table without losing a record; the cap of the column is the cap of the field") {
        ASSERT_TRUE(db_schema_version() >= 7);
        TempDir dir;
        db::Database d;
        const std::vector<db::Migration> every = db_migrations();
        const std::vector<db::Migration> six(every.begin(), every.begin() + 6);
        ASSERT_TRUE(db::open_and_migrate(d, (dir.path / "six.db").string(), six, db::OpenOptions()).ok());
        {
            db::Statement st;
            ASSERT_TRUE(d.prepare(st, "SELECT count(*) FROM sqlite_master WHERE name = 'history_deaths'").ok() && st.step() == db::Statement::Step::Row && st.column_int64(0) == 0);
        }
        d.close();
        db::Database up;
        ASSERT_TRUE(db::open_and_migrate(up, (dir.path / "six.db").string(), db_migrations(), db::OpenOptions()).ok());
        db::Statement st;
        ASSERT_TRUE(up.prepare(st, "SELECT count(*) FROM sqlite_master WHERE name IN ('history_deaths', 'history_deaths_map')").ok() && st.step() == db::Statement::Step::Row && st.column_int64(0) == 2);
        db::Statement over;
        ASSERT_TRUE(up.prepare(over, "INSERT INTO history_deaths (id, map, hash, ended, w, h, n, deaths) VALUES ('x', 'M', '0123456789abcdef', 1, 1, 1, 1, ?1)").ok());
        ASSERT_TRUE(over.bind_text(1, std::string(kDeathsMaxBytes + 1, 'A')).ok());
        ASSERT_TRUE(over.step() == db::Statement::Step::Failed);                                 // (and without a match of that id the foreign key refuses too)
    } TEST_END();

    // ---- the sum of a map ---------------------------------------------------------------------------------------------------------------

    TEST_CASE("DD5.1 The sum of a map adds up the fields of its matches: two real matches on TREASURE (13 and 15 ants lost), every ant counted once, per team and per cause; matches = how many; the name comes back as stored") {
        if (match_file(4).empty() || match_file(5).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        uint64_t want = 0;
        int with_deaths = 0;
        for (const int k : {4, 5}) {
            const std::string id = counted(rig, k);
            HistoryRecord rec;
            ASSERT_TRUE(rig.history.find(id, rec));
            want += view_of(rec.deaths).total;
            with_deaths += rec.deaths.empty() ? 0 : 1;
        }
        ASSERT_EQ(with_deaths, 2);
        ASSERT_GT(want, uint64_t{0});
        std::string json;
        ASSERT_TRUE(rig.history.map_deaths("TREASURE.LVL", json) == HistoryStore::DeathsSum::Ok);
        const JsonValue j = parse_text(json);
        ASSERT_TRUE(j.get("map").str() == "TREASURE.LVL" && j.get("matches").as_int_or(0) == with_deaths && j.get("n").as_int_or(0) == static_cast<int64_t>(want) && j.get("k").str() == "b" && j.get("s").as_int_or(0) == 1);
        const DeathsView v = view_of(json);
        uint64_t sum = 0;
        for (const DeathSquare& c : v.cells) sum += c.count;
        ASSERT_EQ(sum, want);
        ASSERT_TRUE(json.size() <= kDeathsSumMaxBytes);
    } TEST_END();

    TEST_CASE("DD5.2 The name is looked up among the names that have a map of deaths, without regard to the case of an ASCII letter, and is never used as anything else: a path, a pattern or a quote finds nothing") {
        Rig rig;
        ASSERT_TRUE(rig.open());
        std::string why, json;
        ASSERT_TRUE(rig.history.put(synthetic(id_at(kT0 + 10), "Pop Corn.LVL", 5, kT0 + 10, field_at(60, 60, {{3, 4}})), why));
        ASSERT_TRUE(rig.history.map_deaths("Pop Corn.LVL", json) == HistoryStore::DeathsSum::Ok && parse_text(json).get("map").str() == "Pop Corn.LVL");
        ASSERT_TRUE(rig.history.map_deaths("POP CORN.lvl", json) == HistoryStore::DeathsSum::Ok && parse_text(json).get("map").str() == "Pop Corn.LVL");        // (the stored spelling, not the visitor's)
        for (const char* bad : {"", "../TINY.LVL", "..", "/etc/passwd", "Pop Corn", "Pop Corn.LVL ", "Pop%20Corn.LVL", "Pop_Corn.LVL", "%", "_", "Pop Corn.LV_", "Pop Corn.LV%", "' OR 1=1 --", "Pop Corn.LVL\"", "Pop Corn.LVL\n", "*", "Pop*"}) {
            ASSERT_TRUE(rig.history.map_deaths(bad, json) == HistoryStore::DeathsSum::Unknown);
            ASSERT_TRUE(json.empty());
        }
        ASSERT_TRUE(rig.history.map_deaths(std::string(5000, 'a'), json) == HistoryStore::DeathsSum::Unknown);
    } TEST_END();

    TEST_CASE("DD5.3 Only the newest file of a map name counts: a match on another file of that name (another hash) or another size is not added; the matches that count are the newest ones, at most kDeathsSumMatches") {
        Rig rig;
        ASSERT_TRUE(rig.open());
        std::string why, json;
        ASSERT_TRUE(rig.history.put(synthetic(id_at(kT0 + 10, "MAP"), "MAP.LVL", 1, kT0 + 10, field_at(60, 60, {{1, 1}, {2, 2}})), why));
        ASSERT_TRUE(rig.history.put(synthetic(id_at(kT0 + 20, "MAP"), "MAP.LVL", 1, kT0 + 20, field_at(60, 60, {{3, 3}})), why));
        ASSERT_TRUE(rig.history.put(synthetic(id_at(kT0 + 30, "MAP"), "MAP.LVL", 2, kT0 + 30, field_at(60, 60, {{9, 9}})), why));          // (a newer file of that name)
        ASSERT_TRUE(rig.history.map_deaths("MAP.LVL", json) == HistoryStore::DeathsSum::Ok);
        JsonValue j = parse_text(json);
        ASSERT_TRUE(j.get("matches").as_int_or(0) == 1 && j.get("n").as_int_or(0) == 1);
        ASSERT_TRUE(rig.history.put(synthetic(id_at(kT0 + 40, "MAP"), "MAP.LVL", 2, kT0 + 40, field_at(30, 30, {{9, 9}})), why));         // (the same file, another size: not added)
        ASSERT_TRUE(rig.history.map_deaths("MAP.LVL", json) == HistoryStore::DeathsSum::Ok);
        j = parse_text(json);
        ASSERT_TRUE(j.get("w").as_int_or(0) == 30 && j.get("matches").as_int_or(0) == 1);                                                  // (the newest match has the newest size)
        ASSERT_TRUE(rig.history.put(synthetic(id_at(kT0 + 50, "MAP"), "MAP.LVL", 2, kT0 + 50, field_at(30, 30, {{1, 9}, {2, 2}})), why));
        ASSERT_TRUE(rig.history.map_deaths("MAP.LVL", json) == HistoryStore::DeathsSum::Ok);
        j = parse_text(json);
        ASSERT_TRUE(j.get("matches").as_int_or(0) == 2 && j.get("n").as_int_or(0) == 3);
    } TEST_END();

    TEST_CASE("DD5.4 The sum is made once for each state of the history: the second answer is the cached one, a kept match or a deleted one makes it again; the cache is bounded") {
        Rig rig;
        ASSERT_TRUE(rig.open());
        std::string why, a, b;
        ASSERT_TRUE(rig.history.put(synthetic(id_at(kT0 + 10, "MAP"), "MAP.LVL", 1, kT0 + 10, field_at(60, 60, {{1, 1}})), why));
        ASSERT_TRUE(rig.history.map_deaths("MAP.LVL", a) == HistoryStore::DeathsSum::Ok && rig.history.map_deaths("map.lvl", b) == HistoryStore::DeathsSum::Ok && a == b);
        ASSERT_TRUE(rig.history.put(synthetic(id_at(kT0 + 20, "MAP"), "MAP.LVL", 1, kT0 + 20, field_at(60, 60, {{2, 2}})), why));
        ASSERT_TRUE(rig.history.map_deaths("MAP.LVL", b) == HistoryStore::DeathsSum::Ok && a != b && parse_text(b).get("matches").as_int_or(0) == 2);
        ASSERT_TRUE(rig.history.remove(id_at(kT0 + 20, "MAP"), false, why) == HistoryStore::Removal::Done);
        ASSERT_TRUE(rig.history.map_deaths("MAP.LVL", b) == HistoryStore::DeathsSum::Ok && a == b);
        // more maps than the cache holds: every one still answers (the cache is cleared, never grown without end)
        for (size_t i = 0; i < HistoryStore::kDeathsSumCache + 5; ++i) {
            const std::string map = "M" + std::to_string(i) + ".LVL";
            ASSERT_TRUE(rig.history.put(synthetic(id_at(kT0 + 100 + static_cast<int64_t>(i), "M" + std::to_string(i)), map, 3, kT0 + 100 + static_cast<int64_t>(i), field_at(60, 60, {{1, 2}})), why));
        }
        for (size_t i = 0; i < HistoryStore::kDeathsSumCache + 5; ++i) {
            ASSERT_TRUE(rig.history.map_deaths("m" + std::to_string(i) + ".lvl", a) == HistoryStore::DeathsSum::Ok);
            ASSERT_LE(rig.history.deaths_cached(), HistoryStore::kDeathsSumCache);
        }
        ASSERT_GT(rig.history.deaths_cached(), size_t{0});
    } TEST_END();

    TEST_CASE("DD5.5 The sum never passes its cap: many matches with every tile of a big map take squares of a larger side, every ant still counted") {
        std::vector<std::string> fields;
        for (uint32_t i = 0; i < 40; ++i) fields.push_back(make_history_deaths(stats_of(256, 256, spread(300, 256, 256, 100 + i))));
        const std::string text = add_up_deaths("BIG.LVL", 256, 256, fields);
        ASSERT_TRUE(!text.empty() && text.size() <= kDeathsSumMaxBytes);
        const JsonValue j = parse_text(text);
        ASSERT_TRUE(j.get("matches").as_int_or(0) == 40 && j.get("n").as_int_or(0) == 40 * 300 && j.get("s").as_int_or(0) >= 2);
        const DeathsView v = view_of(text);
        uint64_t sum = 0;
        for (const DeathSquare& c : v.cells) sum += c.count;
        ASSERT_EQ(sum, uint64_t{12000});
        ASSERT_EQ(add_up_deaths("BIG.LVL", 256, 256, {}), std::string());
        ASSERT_EQ(add_up_deaths("BIG.LVL", 256, 256, {"junk", field_at(60, 60, {{1, 1}})}), std::string());       // (a field that is not one, and one of another size, are skipped: nothing left to add)
        // fields in squares and fields one by one are added at the larger side
        std::vector<std::string> mixed = {make_history_deaths(stats_of(512, 512, spread(940, 512, 512, 1))), make_history_deaths(stats_of(512, 512, spread(10, 512, 512, 2)))};
        const DeathsView m = view_of(add_up_deaths("M.LVL", 512, 512, mixed));
        uint64_t total = 0;
        for (const DeathSquare& c : m.cells) total += c.count;
        ASSERT_TRUE(total == 950 && m.side >= view_of(mixed[0]).side);
    } TEST_END();

    TEST_CASE("DD5.5b The sum is the smallest that fits, in every case: from 10 to 90 matches of one big map the text is never over its cap and squares of half the side would have been") {
        size_t halved = 0;
        for (uint32_t count = 10; count <= 90; count += 8) {
            std::vector<std::string> fields;
            std::vector<MatchStats> stats;
            for (uint32_t i = 0; i < count; ++i) {
                stats.push_back(stats_of(256, 256, spread(300, 256, 256, 500 + i)));
                fields.push_back(make_history_deaths(stats.back()));
            }
            const std::string text = add_up_deaths("BIG.LVL", 256, 256, fields);
            ASSERT_TRUE(!text.empty() && text.size() <= kDeathsSumMaxBytes);
            const DeathsView v = view_of(text);
            uint64_t sum = 0;
            for (const DeathSquare& c : v.cells) sum += c.count;
            ASSERT_EQ(sum, uint64_t{count} * 300);
            if (v.side > 1) {
                std::map<std::tuple<uint32_t, uint32_t, uint8_t, bool>, int> cells;
                for (const MatchStats& st : stats) {
                    for (const MatchStats::Kill& k : st.kills) ++cells[std::make_tuple(static_cast<uint32_t>(k.tile_y) / (v.side / 2), static_cast<uint32_t>(k.tile_x) / (v.side / 2), k.victim, k.drowned)];
                }
                ASSERT_GT(cells.size() * 8 + 100, kDeathsSumMaxBytes);                       // (squares of half the side do not fit, even with the shortest header)
                ++halved;
            }
        }
        ASSERT_GT(halved, size_t{3});
    } TEST_END();

    TEST_CASE("DD5.6 The sum counts the newest matches only (kDeathsSumMatches of them), and it is kept: a change that the store was not told of is not seen until the store is told by a match that is kept or taken out") {
        TempDir dir;
        db::Database database;
        ASSERT_TRUE(open_server_db(database, ""));
        HistoryStore store(database);
        std::string why, json;
        ASSERT_TRUE(store.prepare(why));
        const std::string old_field = field_at(60, 60, {{59, 59}}), new_field = field_at(60, 60, {{1, 1}});
        for (size_t i = 0; i < HistoryStore::kDeathsSumMatches + 5; ++i) {
            const int64_t at = kT0 + static_cast<int64_t>(i);
            ASSERT_TRUE(store.put(synthetic(id_at(at, "BIG"), "BIG.LVL", 1, at, i < 5 ? old_field : new_field), why));
        }
        ASSERT_TRUE(store.map_deaths("BIG.LVL", json) == HistoryStore::DeathsSum::Ok);
        const JsonValue j = parse_text(json);
        ASSERT_TRUE(j.get("matches").as_int_or(0) == static_cast<int64_t>(HistoryStore::kDeathsSumMatches) && j.get("n").as_int_or(0) == static_cast<int64_t>(HistoryStore::kDeathsSumMatches));
        const DeathsView v = view_of(json);
        ASSERT_TRUE(v.cells.size() == 1 && v.cells[0].x == 1 && v.cells[0].y == 1 && v.cells[0].count == HistoryStore::kDeathsSumMatches);       // (the five oldest, on the other tile, are not in it)
        // a row that the store was not told of: not seen while the sum is kept
        const int64_t late = kT0 + 100000;
        ASSERT_TRUE(database.exec(("INSERT INTO history_matches (id, ended, map, game, turns, finished, quitter, mode, format, result, computers_only, best_score, best_kills, best_hatched, search, rows_json, counted_at) VALUES ('" + id_at(late, "BIG") + "', " + std::to_string(late) + ", 'BIG.LVL', 'v1', 10, 1, NULL, 'highest-score', '1v1', 'decided', 1, 0, 0, 0, 'big', '[]', 1)").c_str()).ok());
        ASSERT_TRUE(database.exec(("INSERT INTO history_deaths (id, map, hash, ended, w, h, n, deaths) VALUES ('" + id_at(late, "BIG") + "', 'BIG.LVL', '0000000000000001', " + std::to_string(late) + ", 60, 60, 1, '" + field_at(60, 60, {{30, 30}}) + "')").c_str()).ok());
        std::string again;
        ASSERT_TRUE(store.map_deaths("big.lvl", again) == HistoryStore::DeathsSum::Ok && again == json);
        ASSERT_TRUE(store.put(synthetic(id_at(late + 1, "BIG"), "BIG.LVL", 1, late + 1, new_field), why));         // (the store is told: the sum is made again, and holds the two new rows)
        ASSERT_TRUE(store.map_deaths("BIG.LVL", again) == HistoryStore::DeathsSum::Ok && again != json);
        const DeathsView w = view_of(again);
        bool middle = false;
        for (const DeathSquare& c : w.cells) middle = middle || (c.x == 30 && c.y == 30);
        ASSERT_TRUE(middle);
    } TEST_END();

    // ---- the route ----------------------------------------------------------------------------------------------------------------------------

    TEST_CASE("DD6.1 GET /history/deaths?map=NAME answers the sum on both doors; the match's own record has its field under \"deaths\" next to the details; every wrong query is a 400 with fixed words; an unknown map is 404 and nothing of the request is in the answer") {
        if (match_file(4).empty() || match_file(5).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Server server;
        ASSERT_TRUE(server.open());
        const std::string id = server.keep(match_file(4));
        server.keep(match_file(5));
        for (int round = 0; round < 60 && server.mgr.history_store()->count() < 2; ++round) server.run_updates(10);       // (the feeder's pace follows the real time that a play takes: a slow build needs more passes)
        ASSERT_EQ(server.mgr.history_store()->count(), size_t{2});
        ctl::HttpResponse r = server.door("GET", "/history/deaths", "map=TREASURE.LVL");
        ASSERT_TRUE(r.status == 200 && r.content_type == "application/json");
        JsonValue j = parse_text(r.body);
        ASSERT_TRUE(j.get("map").str() == "TREASURE.LVL" && j.get("matches").as_int_or(0) == 2 && j.get("n").as_int_or(0) == 24);
        ASSERT_EQ(server.control("GET", "/history/deaths", "map=treasure.lvl").body, r.body);
        ASSERT_EQ(server.door("GET", "/history/deaths", "map=treasure.lvl").body, r.body);
        const ctl::HttpResponse item = server.door("GET", "/history/" + id);
        const JsonValue rec = parse_text(item.body);
        ASSERT_TRUE(item.status == 200 && rec.get("deaths").get("k").is_string() && rec.get("details").is_object() && rec.get("deaths").get("n").as_int_or(0) > 0);
        for (const char* bad : {"", "map=", "map", "x=TREASURE.LVL", "map=TREASURE.LVL&map=TREASURE.LVL", "map=TREASURE.LVL&x=1", "map=%zz", "map=%0A", "map=%FF", "map=a%00b", "map=TREASURE.LVL&", "Map=TREASURE.LVL", " map=TREASURE.LVL"}) {
            r = server.door("GET", "/history/deaths", bad);
            ASSERT_TRUE(r.status == 400);
            ASSERT_TRUE(r.body.find("TREASURE") == std::string::npos && r.body.find("%zz") == std::string::npos);
        }
        r = server.door("GET", "/history/deaths", "map=" + std::string(121, 'a'));
        ASSERT_TRUE(r.status == 400);
        r = server.door("GET", "/history/deaths", "map=../../etc/passwd");
        ASSERT_TRUE(r.status == 404 && r.body.find("passwd") == std::string::npos && r.body.find("..") == std::string::npos && parse_text(r.body).get("error").is_string());
        r = server.door("GET", "/history/deaths", "map=NOT%20A%20MAP.LVL");
        ASSERT_TRUE(r.status == 404 && r.body.find("NOT") == std::string::npos);
    } TEST_END();

    TEST_CASE("DD6.2 Only a GET goes through: the public door says 404 and the control door 405 to every other method, a path under /history/deaths is no match, and the id of a match is not touched") {
        Server server;
        ASSERT_TRUE(server.open());
        ASSERT_TRUE(server.door("POST", "/history/deaths", "map=TINY.LVL").status == 404);
        ASSERT_TRUE(server.control("POST", "/history/deaths", "map=TINY.LVL").status == 405);
        ASSERT_TRUE(server.door("GET", "/history/deaths/x", "map=TINY.LVL").status == 404);
        ASSERT_TRUE(server.door("GET", "/history/deaths", "map=TINY.LVL").status == 404);                   // (no map of deaths yet)
        ASSERT_TRUE(server.control("DELETE", "/history/deaths").status == 404);
        ASSERT_TRUE(server.door("GET", "/history/deaths", "").status == 400);
        ASSERT_TRUE(server.door("GET", "/history/ants-NONE-20261008-143209Z").status == 404);
        ASSERT_TRUE(server.door("GET", "/history").status == 200);
    } TEST_END();

    TEST_CASE("DD6.3 A database that stops answering is a 503 (the public door says only that it is so), and a store that is not up knows no map") {
        Rig rig;
        ASSERT_TRUE(rig.open());
        std::string why, json;
        ASSERT_TRUE(rig.history.put(synthetic(id_at(kT0 + 10), "TINY.LVL", 5, kT0 + 10, field_at(60, 60, {{1, 1}})), why));
        rig.database.close();
        ASSERT_TRUE(rig.history.map_deaths("TINY.LVL", json) == HistoryStore::DeathsSum::Unavailable || rig.history.map_deaths("TINY.LVL", json) == HistoryStore::DeathsSum::Ok);
        HistoryRecord r;
        ASSERT_FALSE(rig.history.find(id_at(kT0 + 10), r));
        ASSERT_TRUE(rig.history.map_deaths("TINY.LVL", json) == HistoryStore::DeathsSum::Unavailable);
        ASSERT_TRUE(json.empty());
    } TEST_END();


    // ---- the action of a match: deaths, bombs, thefts, fires and hits in every 5 seconds ----------------------------------------------------

    TEST_CASE("DD7.1 The collector counts each kind in the bucket of its own turn, and the totals are the engine's own: deaths and thefts are the entries of kills and raids at turn / 100, bombs the bombs that went off, fires the seats' fires lit, hits the ants that lost hit points; an independent observer of the counters and the hit points agrees") {
        bool any = false, hits = false;
        for (int k = 0; k < 6; ++k) {
            Real m = load(k);
            if (!m.ok) {
                std::cout << "SKIPPED (no map)\n";
                return;
            }
            std::vector<uint32_t> fires_seen, hits_seen;                                         // (what an observer of the world finds, by bucket)
            std::array<uint32_t, 4> fires_before{};
            std::vector<int32_t> hp;
            std::vector<bool> there;
            std::vector<uint32_t> hurt_in;                                                       // (the bucket in which an ant was last counted as hurt)
            replay::Hooks hooks;
            hooks.after_tick = [&](uint32_t turns, sim::SimulationEngine& engine) {
                const size_t bucket = turns / MatchStats::kSampleTurns;
                for (size_t seat = 0; seat < 4; ++seat) {
                    const uint32_t lit = engine.stats_manager().get_player_stats(static_cast<uint8_t>(seat)).fires_lit;
                    if (lit > fires_before[seat]) {
                        if (fires_seen.size() <= bucket) fires_seen.resize(bucket + 1);
                        fires_seen[bucket] += lit - fires_before[seat];
                    }
                    fires_before[seat] = lit;
                }
                for (const sim::AntSnapshot& snap : engine.get_world_state().ants) {
                    if (snap.id >= hp.size()) {
                        hp.resize(std::max<size_t>(hp.size() * 2, size_t{snap.id} + 1), 0);
                        there.resize(hp.size(), false);
                        hurt_in.resize(hp.size(), 0xFFFFFFFFu);
                    }
                    if (there[snap.id] && snap.player_id < 4 && snap.hp < hp[snap.id] && hurt_in[snap.id] != bucket) {      // (an ant that is hurt is one hit in the bucket, however often it is hurt in it)
                        hurt_in[snap.id] = static_cast<uint32_t>(bucket);
                        if (hits_seen.size() <= bucket) hits_seen.resize(bucket + 1);
                        ++hits_seen[bucket];
                    }
                    there[snap.id] = true;
                    hp[snap.id] = snap.hp;
                }
            };
            const replay::Outcome plain = replay::play(m.rep, m.level, hooks);
            const replay::StatsPlay p = replay::play_with_stats(m.rep, m.level);
            ASSERT_TRUE(plain.ok && p.outcome.ok && p.stats.ok && p.outcome.hash == plain.hash);                // (the collector changes nothing: the same hash)
            const MatchStats& st = p.stats;
            std::vector<uint32_t> deaths_at, thefts_at;
            for (const MatchStats::Kill& kl : st.kills) {
                if (deaths_at.size() <= kl.turn / 100) deaths_at.resize(kl.turn / 100 + 1);
                ++deaths_at[kl.turn / 100];
            }
            for (const MatchStats::Raid& r : st.raids) {
                if (thefts_at.size() <= r.turn / 100) thefts_at.resize(r.turn / 100 + 1);
                ++thefts_at[r.turn / 100];
            }
            ASSERT_TRUE(st.action[kDeath] == deaths_at);                                                       // (each death in the bucket of its second: the same as the map of deaths gives it)
            ASSERT_TRUE(st.action[kTheft] == thefts_at);
            uint64_t bombs = 0, fires = 0;
            for (size_t seat = 0; seat < 4; ++seat) bombs += st.bombs_off[seat];
            for (const HistorySeat& seat : make_history_record("ants-TINY-20261008-143209Z", kT0, m.rep, p.outcome).seats) fires += seat.fires_lit;
            ASSERT_EQ(sum_of(st.action[kBomb]), bombs);
            ASSERT_EQ(sum_of(st.action[kFire]), fires);
            ASSERT_TRUE(st.action[kFire] == fires_seen);                                                       // (a fire in the bucket of the tick that lit it)
            ASSERT_TRUE(st.action[kHit] == hits_seen);                                                         // (a hit in the bucket of the tick that took the points, once for an ant in a bucket)
            for (size_t kind = 0; kind < MatchStats::kActionKinds; ++kind) ASSERT_TRUE(st.action[kind].size() <= st.turns / 100 + 1);
            any = any || !st.kills.empty();
            hits = hits || sum_of(st.action[kHit]) > 0;
        }
        ASSERT_TRUE(any && hits);                                                                              // (the recordings are matches in which ants were lost and hit)
    } TEST_END();

    TEST_CASE("DD7.2 The field is one letter for each count of each kind: A is none, a count is its letter, 63 is _ and a bucket with more says 63; a match of up to 20 minutes has buckets of 5 seconds, one for each 100 turns, and the last tick of a match that ends on a bucket's edge is counted in its last bucket") {
        const MatchStats st = action_stats(2399, {{{0, 1}, {2, 3}, {5, 200}}, {{1, 2}}, {{3, 26}}, {{4, 27}}, {{6, 63}, {6, 1}}});
        const std::string text = make_history_action(st);
        const auto quiet = [](const std::string& head) { return head + std::string(24 - head.size(), 'A'); };
        ASSERT_EQ(text, action_text(1, 5, 24, quiet("BADAA_"), quiet("AC"), quiet("AAAa"), quiet("AAAAb"), quiet("AAAAAA_")));         // (B is 1, D is 3, a is 26, b is 27, _ is 63: the letters are the alphabet's)
        const ActionView v = action_view(text);
        ASSERT_TRUE(v.seconds == 5 && v.buckets == 24);                                                        // (2399 turns: the buckets 0 to 23)
        ASSERT_TRUE(v.counts[kDeath][0] == 1 && v.counts[kDeath][2] == 3 && v.counts[kDeath][5] == 63 && v.counts[kDeath][1] == 0);
        ASSERT_TRUE(v.counts[kBomb][1] == 2 && v.counts[kTheft][3] == 26 && v.counts[kFire][4] == 27 && v.counts[kHit][6] == 63);
        const ActionView edge = action_view(make_history_action(action_stats(100, {{{1, 4}, {0, 1}}})));      // (turn 100 is bucket 1 of a match of 100 turns, which has one: the last tick is counted in it)
        ASSERT_TRUE(edge.buckets == 1 && edge.seconds == 5 && edge.counts[kDeath][0] == 5);
        ASSERT_EQ(action_view(make_history_action(action_stats(99, {{{0, 1}}}))).buckets, uint32_t{1});
        ASSERT_EQ(action_view(make_history_action(action_stats(101, {{{0, 1}}}))).buckets, uint32_t{2});
        const ActionView twenty = action_view(make_history_action(action_stats(24000, {{{0, 1}, {240, 2}}})));  // (20 minutes: 240 buckets of 5 seconds, and the last tick is in the last)
        ASSERT_TRUE(twenty.buckets == 240 && twenty.seconds == 5 && twenty.counts[kDeath][0] == 1 && twenty.counts[kDeath][239] == 2);
    } TEST_END();

    TEST_CASE("DD7.3 A match of more than 20 minutes has buckets that double (10, 20, 40 seconds ...) until at most 240 hold it, and a bucket is the sum of the 5 seconds it holds (never more than 63)") {
        const MatchStats st = action_stats(24100, {{{0, 1}, {1, 2}, {2, 4}, {239, 5}, {240, 7}}, {{3, 40}, {2, 30}}});
        const ActionView v = action_view(make_history_action(st));
        ASSERT_TRUE(v.seconds == 10 && v.buckets == 121);                                                      // (241 buckets of 5 seconds in 121 of 10)
        ASSERT_TRUE(v.counts[kDeath][0] == 3 && v.counts[kDeath][1] == 4 && v.counts[kDeath][119] == 5 && v.counts[kDeath][120] == 7);
        ASSERT_EQ(static_cast<int>(v.counts[kBomb][1]), 63);                                                   // (30 + 40 is more than a letter holds)
        uint64_t total = 0;
        for (const uint8_t c : v.counts[kDeath]) total += c;
        ASSERT_EQ(total, uint64_t{19});                                                                        // (no event is lost to a bucket: 1 + 2 + 4 + 5 + 7)
        const ActionView longer = action_view(make_history_action(action_stats(100 * 960, {{{0, 1}, {959, 1}}})));      // (80 minutes: 960 buckets of 5 seconds, 4 in each of 240)
        ASSERT_TRUE(longer.seconds == 20 && longer.buckets == 240 && longer.counts[kDeath][0] == 1 && longer.counts[kDeath][239] == 1);
        const ActionView huge = action_view(make_history_action(action_stats(4000000000u, {{{0, 1}}})));
        ASSERT_TRUE(huge.seconds == 5u * 262144u && huge.buckets == 153 && huge.counts[kDeath][0] == 1 && huge.seconds <= kActionMaxSeconds);
    } TEST_END();

    TEST_CASE("DD7.4 A match with none of the five kinds, a pass that failed and a match that never began have no field at all (an empty text); and the reader refuses a text that is not a field") {
        ASSERT_TRUE(make_history_action(action_stats(24000, {})).empty());
        MatchStats failed = action_stats(24000, {{{3, 2}}});
        failed.ok = false;
        ASSERT_TRUE(make_history_action(failed).empty());
        ASSERT_TRUE(make_history_action(MatchStats()).empty());
        const std::string a2(2, 'A');
        ActionView out;
        ASSERT_TRUE(parse_history_action(action_text(1, 5, 2, a2, a2, a2, a2, a2), out) && out.buckets == 2 && out.seconds == 5);
        ASSERT_TRUE(parse_history_action(action_text(1, 5, 2, "A_", "AA", "AA", "AA", "AA"), out) && out.counts[kDeath][1] == 63);
        ASSERT_FALSE(parse_history_action(action_text(2, 5, 2, a2, a2, a2, a2, a2), out));                       // (another version)
        ASSERT_FALSE(parse_history_action(action_text(1, 4, 2, a2, a2, a2, a2, a2), out));                       // (a bucket that is not a multiple of 5 seconds, or is none)
        ASSERT_FALSE(parse_history_action(action_text(1, 0, 2, a2, a2, a2, a2, a2), out));
        ASSERT_FALSE(parse_history_action(action_text(1, 7, 2, a2, a2, a2, a2, a2), out));
        ASSERT_FALSE(parse_history_action(action_text(1, static_cast<int64_t>(kActionMaxSeconds) + 5, 2, a2, a2, a2, a2, a2), out));
        ASSERT_FALSE(parse_history_action(action_text(1, 5, 0, "", "", "", "", ""), out));                       // (no bucket, and one more than the cap)
        ASSERT_FALSE(parse_history_action(action_text(1, 5, static_cast<int64_t>(kActionMaxBuckets) + 1, std::string(kActionMaxBuckets + 1, 'A'), std::string(kActionMaxBuckets + 1, 'A'), std::string(kActionMaxBuckets + 1, 'A'),
                                                      std::string(kActionMaxBuckets + 1, 'A'), std::string(kActionMaxBuckets + 1, 'A')), out));
        ASSERT_FALSE(parse_history_action(action_text(1, 5, 2, a2, a2, a2, a2, "A"), out));                      // (a text that is not n letters)
        ASSERT_FALSE(parse_history_action(action_text(1, 5, 2, a2, a2, a2, a2, "AAA"), out));
        ASSERT_FALSE(parse_history_action(action_text(1, 5, 2, a2, a2, a2, a2, "A!"), out));                     // (a letter outside the 64)
        ASSERT_FALSE(parse_history_action(action_text(1, 5, 2, a2, a2, a2, a2, "A+"), out));
        ASSERT_FALSE(parse_history_action("{\"v\":1,\"s\":5,\"n\":2,\"death\":\"AA\",\"bomb\":\"AA\",\"theft\":\"AA\",\"fire\":\"AA\"}", out));      // (a kind that is missing)
        ASSERT_FALSE(parse_history_action("{\"v\":1,\"s\":5,\"n\":2,\"death\":1,\"bomb\":\"AA\",\"theft\":\"AA\",\"fire\":\"AA\",\"hit\":\"AA\"}", out));
        ASSERT_FALSE(parse_history_action("not a field", out));
        ASSERT_FALSE(parse_history_action("[]", out));
        ASSERT_FALSE(parse_history_action("", out));
    } TEST_END();

    TEST_CASE("DD7.5 The field never passes its cap, whatever the match: every length from nothing to 4 billion turns, every count, every kind at its largest; the text is a field that parses and never has more than 240 buckets") {
        size_t longest = 0;
        for (const uint32_t turns : {0u, 1u, 99u, 100u, 23999u, 24000u, 24100u, 48000u, 48100u, 100000u, 1000000u, 12960000u, 123456789u, 4000000000u, 4294967295u}) {
            MatchStats st = action_stats(turns, {});
            for (size_t kind = 0; kind < MatchStats::kActionKinds; ++kind) {
                st.action[kind].assign(static_cast<size_t>(std::min<uint64_t>(turns / 100 + 1, 70000)), 1000000u);       // (every bucket of every kind at more than a letter holds; a match of 4 billion turns has more buckets than a test needs)
            }
            const std::string text = make_history_action(st);
            ASSERT_FALSE(text.empty());
            ASSERT_TRUE(text.size() <= kActionMaxBytes);
            const ActionView v = action_view(text);
            ASSERT_TRUE(v.buckets >= 1 && v.buckets <= kActionMaxBuckets && v.seconds >= 5 && v.seconds % 5 == 0);
            for (size_t kind = 0; kind < MatchStats::kActionKinds; ++kind) ASSERT_TRUE(v.counts[kind].size() == v.buckets && v.counts[kind].front() == 63);
            longest = std::max(longest, text.size());
        }
        ASSERT_TRUE(longest > 1200 && longest <= kActionMaxBytes);                                             // (the longest is 5 x 240 letters and a short head)
        MatchStats beyond = action_stats(100, {{{0, 1}}});
        beyond.action[kHit].assign(1000, 5u);                                                                  // (a series longer than the match: the field still holds it, in the last bucket)
        const std::string text = make_history_action(beyond);
        ActionView whole;
        ASSERT_TRUE(!text.empty() && text.size() <= kActionMaxBytes && parse_history_action(text, whole) && whole.buckets == 1 && whole.counts[kHit][0] == 63);
    } TEST_END();

    TEST_CASE("DD7.6 A match that the feeder counts has its action next to its details and its map of deaths, from the same play: in its own table, in the record's JSON under \"action\"; the list leaves it out; an older record gets none (no backfill) and the page draws its deaths alone") {
        if (match_file(0).empty() || match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string id = counted(rig, 0);
        HistoryRecord rec;
        ASSERT_TRUE(rig.history.find(id, rec) && !rec.action.empty() && !rec.deaths.empty() && !rec.details.empty());
        Real m = load(0);
        const replay::StatsPlay p = replay::play_with_stats(m.rep, m.level);
        ASSERT_EQ(rec.action, make_history_action(p.stats));
        ASSERT_TRUE(rec.action.size() <= kActionMaxBytes && rec.details.size() <= kDetailsMaxBytes);
        const ActionView v = action_view(rec.action);
        ASSERT_TRUE(v.buckets >= 1 && v.seconds == 5);
        uint64_t deaths = 0;
        for (const uint8_t c : v.counts[kDeath]) deaths += c;
        ASSERT_TRUE(deaths > 0 && deaths <= p.stats.kills.size());                                             // (the deaths of the field are the ants that were lost, none more)
        const JsonValue j = history_record_to_json(rec, true);
        ASSERT_TRUE(j.get("action").is_object() && j.get("action").get("n").as_int_or(0) == static_cast<int64_t>(v.buckets) && j.get("action").get("death").is_string() && j.get("deaths").is_object() && j.get("details").is_object());
        ASSERT_FALSE(history_record_to_json(rec, false).has("action"));                                        // (the list leaves it out, like the details)
        const HistoryPage page = rig.history.query(HistoryQuery(), [](const std::string&) { return Recording::Watch; });
        ASSERT_TRUE(page.ok && page.items.size() == 1 && page.items[0].action.empty());                        // (and does not read it)
        // an older record: its details come from its recording, and it gets no action
        Rig other;
        ASSERT_TRUE(other.open());
        const std::string older = other.keep(match_file(1));
        const Played pl = play_file(match_file(1));
        ASSERT_TRUE(pl.ok);
        std::string why;
        ASSERT_TRUE(other.history.put(make_history_record(older, other.now, pl.rep, pl.outcome), why));
        other.drain(8);
        HistoryRecord old;
        ASSERT_TRUE(other.history.find(older, old) && !old.details.empty() && other.feeder.backfilled() == 1 && old.action.empty() && old.deaths.empty());
        ASSERT_FALSE(history_record_to_json(old, true).has("action"));
    } TEST_END();

    TEST_CASE("DD7.7 The counters, the details and the map of deaths of a match are written whatever the pass of the action does: a maker that throws leaves no action, a line in the log and the record; a text that is not a field, or is over its cap, is not kept (the match is); a field is replaced with its match and goes with it") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        rig.feeder.set_action_maker([](const replay::MatchStats&) -> std::string { throw std::out_of_range("the action broke"); });       // (not a runtime_error: the feeder holds back any exception)
        const std::string id = counted(rig, 1);
        HistoryRecord rec;
        ASSERT_TRUE(rig.history.find(id, rec) && rec.action.empty() && !rec.details.empty() && rec.details != history_details_marker() && rec.seats.size() == 2 && rig.feeder.counted() == 1 && rig.feeder.left_out() == 0);
        ASSERT_EQ(lines_with(rig.history.take_notes(), "the action broke"), size_t{1});
        Rig none;
        ASSERT_TRUE(none.open());
        none.feeder.set_action_maker(nullptr);
        const std::string id2 = counted(none, 1);
        ASSERT_TRUE(none.history.find(id2, rec) && rec.action.empty() && !rec.details.empty());
        // the store
        Rig store;
        ASSERT_TRUE(store.open());
        std::string why;
        const std::string field = make_history_action(action_stats(2400, {{{2, 3}}, {{5, 1}}, {}, {{7, 2}}, {{1, 9}}}));
        HistoryRecord a = synthetic(id_at(kT0 + 10), "TINY.LVL", 5, kT0 + 10, std::string());
        a.action = field;
        ASSERT_TRUE(store.history.put(a, why));
        HistoryRecord got;
        ASSERT_TRUE(store.history.find(a.id, got) && got.action == field && got.deaths.empty());
        HistoryRecord junk = synthetic(id_at(kT0 + 20), "TINY.LVL", 5, kT0 + 20, std::string());
        junk.action = "not a field";
        ASSERT_TRUE(store.history.put(junk, why) && store.history.find(junk.id, got) && got.action.empty() && got.seats.size() == 2);
        HistoryRecord big = synthetic(id_at(kT0 + 30), "TINY.LVL", 5, kT0 + 30, std::string());
        big.action = field + std::string(kActionMaxBytes, ' ');
        ASSERT_TRUE(store.history.put(big, why) && store.history.find(big.id, got) && got.action.empty());
        HistoryRecord bad = synthetic(id_at(kT0 + 40), "TINY.LVL", 5, kT0 + 40, std::string());
        const std::string two(2, 'A');
        bad.action = action_text(1, 5, 2, two, two, two, two, "A!");
        ASSERT_TRUE(store.history.put(bad, why) && store.history.find(bad.id, got) && got.action.empty());
        HistoryRecord shown = synthetic(id_at(kT0 + 50), "TINY.LVL", 5, kT0 + 50, std::string());           // (the record's JSON gives the action only when it is a field of this version, as put() keeps it)
        shown.action = field;
        ASSERT_TRUE(history_record_to_json(shown, true).get("action").get("n").as_int_or(0) == static_cast<int64_t>(action_view(field).buckets));
        for (const std::string& text : {std::string("not a field"), std::string("{\"v\":1}"), action_text(2, 5, 2, two, two, two, two, two), field + std::string(kActionMaxBytes, ' ')}) {
            shown.action = text;
            ASSERT_FALSE(history_record_to_json(shown, true).has("action"));
        }
        a.action.clear();                                                                                      // (kept again without it: the old row goes)
        ASSERT_TRUE(store.history.put(a, why) && store.history.find(a.id, got) && got.action.empty());
        a.action = field;
        ASSERT_TRUE(store.history.put(a, why) && store.history.find(a.id, got) && got.action == field);
        ASSERT_TRUE(store.history.remove(a.id, true, why) == HistoryStore::Removal::Done);
        ASSERT_FALSE(store.history.find(a.id, got));
        ASSERT_TRUE(store.history.put(a, why) && store.history.find(a.id, got) && got.action == field);        // (kept again: the marker goes, the row is back)
    } TEST_END();

    TEST_CASE("DD7.8 The action is schema version 11 (history service: 5) and a database of the version before it, with a match in it, gets the table without losing a row; the cap of the column is the cap of the field and the row goes with its match") {
        ASSERT_TRUE(db_schema_version() >= 11);
        const std::vector<db::Migration> every = db_migrations();
        const std::vector<db::Migration>& service = history_db_migrations();
        ASSERT_TRUE(every.size() >= 11 && every[10].version == 11 && std::string(every[10].name) == "history_action");
        ASSERT_TRUE(service.size() == 5 && service[4].version == 5 && std::string(service[4].name) == "history_action");
        const auto one = [](db::Database& d, const std::string& sql) {
            db::Statement st;
            return d.prepare(st, sql).ok() && st.step() == db::Statement::Step::Row ? st.column_int64(0) : int64_t{-1};
        };
        const std::string id = "ants-TINY-20261008-143209Z";
        // the game server's file (10 -> 11) and the history service's own (4 -> 5): a match with its seats, its map of deaths and its play count is kept before the upgrade and is all there after it
        for (const bool server_file : {true, false}) {
            const std::vector<db::Migration>& all = server_file ? every : service;
            const std::vector<db::Migration> before(all.begin(), all.end() - 1);
            TempDir dir;
            const std::string path = (dir.path / "history.db").string();
            {
                db::Database d;
                ASSERT_TRUE(db::open_and_migrate(d, path, before, db::OpenOptions()).ok());
                ASSERT_EQ(one(d, "SELECT count(*) FROM sqlite_master WHERE name = 'history_action'"), int64_t{0});
                ASSERT_TRUE(d.exec("INSERT INTO history_matches (id, ended, map, game, turns, finished, quitter, mode, format, result, computers_only, best_score, best_kills, best_hatched, search, rows_json, counted_at)"
                                   " VALUES ('" + id + "', 1, 'TINY.LVL', 'v1', 2400, 1, NULL, 'highest-score', '1v1', 'decided', 0, 5, 5, 0, 'tiny ann', '[]', 1)").ok());
                ASSERT_TRUE(d.exec("INSERT INTO history_seats VALUES ('" + id + "', 0, 'Ann', '', 5, 5, 0, 0, 0, 0, 0, 0, 1)").ok());
                ASSERT_TRUE(d.exec("INSERT INTO history_seats VALUES ('" + id + "', 1, 'Bo', 'Hard', 0, 0, 5, 0, 0, 0, 0, 0, 0)").ok());
                ASSERT_TRUE(d.exec("INSERT INTO history_deaths (id, map, hash, ended, w, h, n, deaths) VALUES ('" + id + "', 'TINY.LVL', '0123456789abcdef', 1, 60, 60, 1, 'kept as it was')").ok());
                ASSERT_TRUE(d.exec("INSERT INTO map_plays (map, hash, plays, last_played) VALUES ('TINY.LVL', '0123456789abcdef', 3, 1)").ok());
            }
            db::Database up;
            ASSERT_TRUE(db::open_and_migrate(up, path, all, db::OpenOptions()).ok());
            ASSERT_EQ(one(up, "SELECT count(*) FROM history_matches WHERE id = '" + id + "' AND turns = 2400 AND best_score = 5 AND search = 'tiny ann'"), int64_t{1});
            ASSERT_EQ(one(up, "SELECT count(*) FROM history_seats WHERE match_id = '" + id + "' AND ((seat = 0 AND name = 'Ann' AND winner = 1) OR (seat = 1 AND name = 'Bo' AND bot = 'Hard' AND lost = 5))"), int64_t{2});
            ASSERT_EQ(one(up, "SELECT count(*) FROM history_deaths WHERE id = '" + id + "' AND deaths = 'kept as it was'"), int64_t{1});
            ASSERT_EQ(one(up, "SELECT plays FROM map_plays WHERE map = 'TINY.LVL'"), int64_t{3});
            ASSERT_EQ(one(up, "SELECT count(*) FROM history_action"), int64_t{0});
            const auto insert = [&](const std::string& text) {
                db::Statement st;
                if (!up.prepare(st, "INSERT INTO history_action (id, action) VALUES ('" + id + "', ?1)").ok() || !st.bind_text(1, text).ok()) return false;
                return st.step() == db::Statement::Step::Done;
            };
            ASSERT_FALSE(insert(std::string(kActionMaxBytes + 1, 'A')));                      // (the cap of the column is the cap of the field)
            ASSERT_TRUE(insert(std::string(kActionMaxBytes, 'A')));
            ASSERT_TRUE(up.exec("DELETE FROM history_matches WHERE id = '" + id + "'").ok());
            ASSERT_EQ(one(up, "SELECT count(*) FROM history_action"), int64_t{0});             // (the row went with its match)
        }
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
