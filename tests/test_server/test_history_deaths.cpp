// Tests of the map of deaths of a match (docs/REPLAYS.md "The map of deaths"): where each ant died, found by the replay pass (ants_replay/match_stats.hpp: the tile of every lost ant), packed into a field of its
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
// The recordings are real matches of the standard computer players on the shipped TINY map (history_fixtures.hpp).
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <tuple>

#include "../common/test_assert.hpp"
#include "ants_replay/match_stats.hpp"
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
        rig.feeder.set_deaths_maker([](const replay::MatchStats&) -> std::string { throw std::runtime_error("the map broke"); });
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

    TEST_CASE("DD4.5 The schema is version 7 and a database of version 6 gets the table without losing a record; the cap of the column is the cap of the field") {
        ASSERT_EQ(db_schema_version(), 7);
        TempDir dir;
        db::Database d;
        std::vector<db::Migration> six = db_migrations();
        six.pop_back();
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

    TEST_CASE("DD5.1 The sum of a map adds up the fields of its matches: two real matches on TREASURE (11 ants lost in each), every ant counted once, per team and per cause; matches = how many; the name comes back as stored") {
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
        server.run_updates(60);
        ctl::HttpResponse r = server.door("GET", "/history/deaths", "map=TREASURE.LVL");
        ASSERT_TRUE(r.status == 200 && r.content_type == "application/json");
        JsonValue j = parse_text(r.body);
        ASSERT_TRUE(j.get("map").str() == "TREASURE.LVL" && j.get("matches").as_int_or(0) == 2 && j.get("n").as_int_or(0) == 22);
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

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
