// Tests of the stats that a replay pass keeps in a match's record (ants_replay/match_stats.hpp: the collector that watches the engine while a recording plays; ants_server/history_details.hpp: the small JSON
// of the record's `details`; ants_server/history_store.hpp: the feeder that counts a match and its details in one play, and gives the older records theirs). What is checked:
//   - the collector is a watcher: a recording played with it ends in the same hashes, the same turns and the same totals as a plain play (HD1.1), what it finds agrees with the engine's own counters
//     (every kill is a seat's kill and another seat's lost ant, the hit points add up, the orders are the commands of the file by minute and kind: HD1.2, HD1.3), it is deterministic, and a recording
//     that does not play out is no stats (HD1.4);
//   - the JSON is made from what it found by rules that are written out here on hand-made numbers: the points of the series, who led (a change counts at two looks in a row or at the last look, a tie
//     leaves the lead, an ally is named), the comeback of the winning side, the best burst of kills, the minutes (a last short part counts in the minute before), the caps of the lists (HD2.x);
//   - the text never passes kDetailsMaxBytes, however long the match and however many events (the lists go first, then the points), and it is the layout that docs/REPLAYS.md describes (HD3.x);
//   - the feeder: a counted match has its details, an older record gets them from its recording (newest first, at the feeder's pace, only while the server is quiet), a recording that cannot be
//     counted leaves the marker so that it is not tried again, a record that has details is never touched, the totals do not depend on the details (HD4.x).
// The recordings are real matches of the standard computer players on the shipped TINY map (history_fixtures.hpp).
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>

#include "../common/test_assert.hpp"
#include "ants_replay/match_stats.hpp"
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

// ---- stats made by hand: a match of `turns` turns with a sample every 100 turns, all zero --------------------------------------------------------

MatchStats blank(uint32_t turns, uint8_t roster = 0x0F) {
    MatchStats s;
    s.ok = true;
    s.turns = turns;
    s.roster = roster;
    for (uint32_t t = 100; t <= turns; t += 100) {
        MatchStats::Sample sm;
        sm.turn = t;
        s.samples.push_back(sm);
    }
    return s;
}

MatchStats::Kill kill(uint32_t turn, uint8_t by, uint8_t of, uint8_t type = 0, bool drowned = false) {
    MatchStats::Kill k;
    k.turn = turn;
    k.killer = by;
    k.victim = of;
    k.victim_type = type;
    k.drowned = drowned;
    return k;
}

// A record whose first row is the winner: `winners` are the seats of that row
HistoryRecord record_for(std::vector<uint8_t> winners, bool draw = false, const char* mode = kHistoryModeScore) {
    HistoryRecord r;
    r.id = id_at(kT0);
    r.mode = mode;
    r.draw = draw;
    HistoryRow row;
    row.seats = std::move(winners);
    row.winner = !draw;
    r.rows.push_back(row);
    return r;
}

JsonValue details_of(const MatchStats& st, const HistoryRecord& rec) {
    const std::string text = make_history_details(st, rec);
    JsonValue v = parse_text(text);
    return v;
}

template <class A>
int64_t sum_of(const A& values) {
    int64_t n = 0;
    for (const auto& v : values) n += static_cast<int64_t>(v);
    return n;
}

int64_t at_int(const JsonValue& v, size_t i, size_t j) { return v.at(i).at(j).as_int_or(-999); }

// ---- the real matches ---------------------------------------------------------------------------------------------------------------------------

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

size_t commands_of(const replay::Replay& rep, uint8_t seat) {
    size_t n = 0;
    for (const replay::TimedCommand& tc : rep.commands) {
        const sim::CommandType t = tc.command.type;
        if (tc.command.issuer == seat && t != sim::CommandType::Quit && t != sim::CommandType::Drop && t != sim::CommandType::None) ++n;
    }
    return n;
}

uint32_t orders_of(const MatchStats& st, size_t seat) {
    uint32_t n = 0;
    for (const auto& by_minute : st.orders) {
        for (const auto& minute : by_minute) n += minute[seat];
    }
    return n;
}


// ---- an observer of its own: the same facts, found another way (the engine's own record of every ant, read by number each tick, not the world's list), to hold the collector against ------------

struct Oracle {
    struct Unit {
        bool seen{false};
        bool removed{false};
        uint16_t hp{0};
        uint8_t own_type{0};
        uint8_t target{255};
        bool holding{false};
        uint8_t player{0};
    };
    std::vector<Unit> units;
    std::array<std::array<uint32_t, 4>, 4> from{};
    std::array<uint32_t, 4> taken{};
    std::vector<std::array<uint32_t, 5>> kills;          // turn, killer (255: none), victim, type, drowned
    std::array<std::array<uint32_t, 5>, 4> powerups{};
    std::vector<std::array<uint32_t, 3>> raids;          // turn, thief, victim
    std::array<uint32_t, 4> trips{};
    std::array<int32_t, 4> wipe_turn{-1, -1, -1, -1}, out_turn{-1, -1, -1, -1};
    std::array<uint32_t, 4> wipes{};
    std::array<bool, 4> was_zero{};
    uint8_t roster{0x0F};

    void tick(uint32_t turns, sim::SimulationEngine& engine) {
        std::array<uint32_t, 4> alive{};
        for (uint32_t id = 1;; ++id) {
            sim::AntUnit* ant = nullptr;
            try {
                ant = &engine.get_unit(id);
            } catch (const std::runtime_error&) {
                break;
            }
            if (units.size() <= id) units.resize(id + 1);
            Unit& u = units[id];
            if (!ant->removed && ant->player_id < 4) ++alive[ant->player_id];
            if (!u.seen) {
                u = Unit{true, ant->removed, ant->hp, static_cast<uint8_t>(ant->type), ant->target_team_id, ant->is_holding(), ant->player_id};
                continue;
            }
            if (u.removed) continue;
            if (ant->removed) {
                const uint8_t killer = ant->killer_team < 4 && ant->killer_team != u.player ? ant->killer_team : uint8_t{255};
                kills.push_back({turns, killer, u.player, u.own_type, ant->death_status == sim::DeathStatus::Drowned ? 1u : 0u});
                u.removed = true;
                continue;
            }
            if (ant->hp < u.hp) {
                taken[u.player] += static_cast<uint32_t>(u.hp - ant->hp);
                if (ant->killer_team < 4) from[ant->killer_team][u.player] += static_cast<uint32_t>(u.hp - ant->hp);
            }
            const uint8_t type = static_cast<uint8_t>(ant->type);
            if (type != u.own_type && type >= 1 && type <= 5) ++powerups[u.player][type - 1u];
            if (ant->is_holding() && !u.holding && !ant->is_thief_steal) ++trips[u.player];
            if (ant->target_team_id < 4 && u.target >= 4 && engine.ant_type(*ant) == sim::AntType::Thief) raids.push_back({turns, u.player, ant->target_team_id});
            u.hp = ant->hp;
            u.own_type = type;
            u.target = ant->target_team_id;
            u.holding = ant->is_holding();
        }
        for (uint8_t s = 0; s < 4; ++s) {
            if (((roster >> s) & 1u) == 0 || turns <= 40) continue;
            if (alive[s] == 0 && !was_zero[s]) {
                was_zero[s] = true;
                ++wipes[s];
                if (wipe_turn[s] < 0) wipe_turn[s] = static_cast<int32_t>(turns);
            }
            if (alive[s] != 0) was_zero[s] = false;
            if (out_turn[s] < 0 && alive[s] == 0 && engine.get_player_eggs(s) == 0 && engine.get_pending_hatch_count(s) == 0) out_turn[s] = static_cast<int32_t>(turns);
        }
    }
};

}  // namespace

int main() {
    std::cout << "\n=======================================================\n  MATCH STATS: THE REPLAY PASS AND THE DETAILS OF A RECORD\n=======================================================\n";

    // ---- the collector ------------------------------------------------------------------------------------------------------------------------

    TEST_CASE("HD1.1 A recording played with the collector ends where a plain play ends: the same hashes (every one in the file), the same turns, the same totals; the collector only watches") {
        for (int k = 0; k < 6; ++k) {
            Real m = load(k);
            if (!m.ok) {
                std::cout << "SKIPPED (no map)\n";
                return;
            }
            const replay::Outcome plain = replay::play(m.rep, m.level);
            const replay::StatsPlay watched = replay::play_with_stats(m.rep, m.level);
            ASSERT_TRUE(plain.ok && watched.outcome.ok);
            ASSERT_TRUE(watched.outcome.hash == plain.hash && watched.outcome.turns == plain.turns && watched.outcome.hashes_checked == plain.hashes_checked && plain.hashes_checked > 0);
            ASSERT_TRUE(watched.outcome.match_over == plain.match_over && watched.outcome.dropped_mask == plain.dropped_mask);
            for (size_t s = 0; s < sim::MAX_PLAYERS; ++s) {
                const sim::PlayerMatchStats& a = watched.outcome.result.stats[s];
                const sim::PlayerMatchStats& b = plain.result.stats[s];
                ASSERT_TRUE(a.score == b.score && a.enemy_killed == b.enemy_killed && a.friendly_lost == b.friendly_lost && a.new_hatched == b.new_hatched);
                ASSERT_TRUE(a.bombs_planted == b.bombs_planted && a.bombs_defused == b.bombs_defused && a.fires_lit == b.fires_lit);
            }
            ASSERT_TRUE(watched.stats.ok && watched.stats.turns == plain.turns && watched.stats.roster == m.rep.head.roster);
            ASSERT_EQ(watched.stats.samples.size(), size_t{plain.turns / 100});
            for (size_t i = 0; i < watched.stats.samples.size(); ++i) ASSERT_EQ(watched.stats.samples[i].turn, static_cast<uint32_t>((i + 1) * 100));
        }
    } TEST_END();

    TEST_CASE("HD1.2 What the collector finds agrees with the engine's own counters: every lost ant is one kill record, a seat's kills and losses are its killed and lost, the hit points add up, the series never fall") {
        for (int k = 0; k < 6; ++k) {
            Real m = load(k);
            if (!m.ok) {
                std::cout << "SKIPPED (no map)\n";
                return;
            }
            const replay::StatsPlay p = replay::play_with_stats(m.rep, m.level);
            ASSERT_TRUE(p.outcome.ok && p.stats.ok);
            const MatchStats& st = p.stats;
            uint32_t killed_total = 0, lost_total = 0;
            for (size_t s = 0; s < sim::MAX_PLAYERS; ++s) {
                uint32_t as_killer = 0, as_victim = 0;
                for (const MatchStats::Kill& kl : st.kills) {
                    if (kl.killer == s) ++as_killer;
                    if (kl.victim == s) ++as_victim;
                }
                const sim::PlayerMatchStats& c = p.outcome.result.stats[s];
                ASSERT_EQ(as_killer, c.enemy_killed);
                ASSERT_EQ(as_victim, c.friendly_lost);
                killed_total += c.enemy_killed;
                lost_total += c.friendly_lost;
                uint32_t from_others = 0, dealt = 0;
                for (size_t src = 0; src < sim::MAX_PLAYERS; ++src) {
                    from_others += st.damage_from[src][s];
                    if (src != s) dealt += st.damage_from[s][src];
                }
                ASSERT_TRUE(st.damage_taken[s] >= from_others);                       // (the rest took hit points from no team: a blast of a pile-up)
                ASSERT_EQ(st.damage_dealt[s], dealt);
                ASSERT_TRUE(st.banked[s] >= 0 && st.hatch_cost[s] >= 0 && st.food_points[s] >= 0);
                ASSERT_TRUE(st.hatch_cost[s] <= int64_t{p.outcome.result.stats[s].new_hatched} * 200);
            }
            ASSERT_TRUE(st.kills.size() >= lost_total && killed_total <= lost_total);
            for (const MatchStats::Kill& kl : st.kills) ASSERT_TRUE(kl.victim < sim::MAX_PLAYERS && (kl.killer == MatchStats::kNoKiller || (kl.killer < sim::MAX_PLAYERS && kl.killer != kl.victim)));
            for (size_t i = 1; i < st.kills.size(); ++i) ASSERT_TRUE(st.kills[i].turn >= st.kills[i - 1].turn);
            for (size_t i = 1; i < st.samples.size(); ++i) {
                for (size_t s = 0; s < sim::MAX_PLAYERS; ++s) ASSERT_TRUE(st.samples[i].killed[s] >= st.samples[i - 1].killed[s]);
            }
            std::array<int64_t, 4> robbed{};
            for (const MatchStats::Raid& r : st.raids) robbed[r.victim < 4 ? r.victim : 0] += r.points;
            for (size_t s = 0; s < sim::MAX_PLAYERS; ++s) ASSERT_EQ(int64_t{p.outcome.result.stats[s].score}, st.banked[s] - st.hatch_cost[s] - robbed[s]);       // (the score is what the hill banked, less what it paid for hatching and what was raided: to the point)
            for (const MatchStats::Raid& r : st.raids) ASSERT_TRUE(r.thief < sim::MAX_PLAYERS && r.victim < sim::MAX_PLAYERS && r.thief != r.victim && r.turn > 0 && r.turn <= st.turns);
            if (!st.samples.empty() && p.outcome.turns % 100 == 0) {                   // (the last look is the end of the match)
                for (size_t s = 0; s < sim::MAX_PLAYERS; ++s) ASSERT_TRUE(st.samples.back().killed[s] == p.outcome.result.stats[s].enemy_killed && st.samples.back().score[s] == p.outcome.result.stats[s].score);
            }
        }
        // the four matches fight and hatch: the numbers that the checks above compare are not all zero
        Real big = load(0);
        const replay::StatsPlay p = replay::play_with_stats(big.rep, big.level);
        ASSERT_TRUE(p.stats.kills.size() > 0 && sum_of(p.stats.damage_taken) > 0 && sum_of(p.stats.hatch_cost) > 0);
    } TEST_END();

    TEST_CASE("HD1.3 The orders are the commands of the file: for every seat the kinds and minutes add up to its commands (a Quit is none), and each lands in the minute of its turn") {
        for (int k = 0; k < 6; ++k) {
            Real m = load(k);
            if (!m.ok) {
                std::cout << "SKIPPED (no map)\n";
                return;
            }
            const replay::StatsPlay p = replay::play_with_stats(m.rep, m.level);
            ASSERT_TRUE(p.stats.ok);
            for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) ASSERT_EQ(orders_of(p.stats, s), static_cast<uint32_t>(commands_of(m.rep, s)));
            // per minute: the hatch orders of one seat, counted by the turn in the file
            std::vector<uint32_t> expect;
            for (const replay::TimedCommand& tc : m.rep.commands) {
                if (tc.command.type != sim::CommandType::Hatch || tc.command.issuer != 0) continue;
                const size_t minute = tc.turn / 1200;
                if (expect.size() <= minute) expect.resize(minute + 1, 0);
                ++expect[minute];
            }
            const auto& hatch = p.stats.orders[static_cast<size_t>(MatchStats::OrderKind::Hatch)];
            for (size_t minute = 0; minute < expect.size(); ++minute) ASSERT_EQ(minute < hatch.size() ? hatch[minute][0] : 0u, expect[minute]);
        }
        Real big = load(0);
        const replay::StatsPlay p = replay::play_with_stats(big.rep, big.level);
        ASSERT_TRUE(!p.stats.orders[static_cast<size_t>(MatchStats::OrderKind::Move)].empty() && !p.stats.orders[static_cast<size_t>(MatchStats::OrderKind::Attack)].empty());
        uint32_t named = 0;
        for (const auto& seat : p.stats.orders_naming) named += seat[0];                       // (the ants of the start are workers)
        ASSERT_TRUE(named > 0);
        // the kinds and the ants that an order names, found another way: the tool's list of orders (it reads the ants' types off the engine as each order is given)
        for (int k : {0, 4}) {
            Real m = load(k);
            ASSERT_TRUE(m.ok);
            const replay::StatsPlay sp = replay::play_with_stats(m.rep, m.level);
            replay::Outcome oc;
            const std::vector<replay::OrderRecord> lines = replay::list_orders(m.rep, m.level, oc);
            ASSERT_TRUE(oc.ok);
            std::array<std::array<std::array<uint32_t, 4>, 1>, MatchStats::kOrderKinds> by_kind{};
            std::array<std::array<uint32_t, 6>, 4> naming{};
            for (const replay::OrderRecord& o : lines) {
                const sim::Command& c = o.command;
                if (c.issuer >= 4 || c.type == sim::CommandType::Quit || c.type == sim::CommandType::Drop) continue;
                MatchStats::OrderKind kind = MatchStats::OrderKind::Move;
                switch (c.type) {
                    case sim::CommandType::GroupMove: kind = MatchStats::OrderKind::Move; break;
                    case sim::CommandType::GroupAttack: kind = MatchStats::OrderKind::Attack; break;
                    case sim::CommandType::Stop: kind = MatchStats::OrderKind::Stop; break;
                    case sim::CommandType::Hatch: kind = MatchStats::OrderKind::Hatch; break;
                    case sim::CommandType::GroupSpecial:
                        kind = MatchStats::OrderKind::Special;
                        for (const replay::OrderRecord::AntCount& a : o.ants) {
                            if (a.type == sim::AntType::Bomber) kind = MatchStats::OrderKind::Bomb;
                            else if (a.type == sim::AntType::Fire) kind = MatchStats::OrderKind::Fire;
                            else if (a.type == sim::AntType::Swimmer) kind = MatchStats::OrderKind::Bridge;
                            else if (a.type == sim::AntType::Thief) kind = MatchStats::OrderKind::Raid;
                            else continue;
                            break;
                        }
                        break;
                    default: kind = MatchStats::OrderKind::Team; break;
                }
                ++by_kind[static_cast<size_t>(kind)][0][c.issuer];
                for (const replay::OrderRecord::AntCount& a : o.ants) ++naming[c.issuer][static_cast<size_t>(a.type)];
            }
            for (size_t kind = 0; kind < MatchStats::kOrderKinds; ++kind) {
                for (size_t seat = 0; seat < 4; ++seat) {
                    uint32_t n = 0;
                    for (const auto& minute : sp.stats.orders[kind]) n += minute[seat];
                    ASSERT_EQ(n, by_kind[kind][0][seat]);
                }
            }
            ASSERT_TRUE(sp.stats.orders_naming == naming);
            if (k == 4) {                                                                       // (the match with ability types: bombs, fires, raids and bridges are orders of their own)
                uint32_t bombs = 0, raids = 0;
                for (const auto& minute : sp.stats.orders[static_cast<size_t>(MatchStats::OrderKind::Bomb)]) bombs += minute[0] + minute[1] + minute[2] + minute[3];
                for (const auto& minute : sp.stats.orders[static_cast<size_t>(MatchStats::OrderKind::Raid)]) raids += minute[0] + minute[1] + minute[2] + minute[3];
                ASSERT_TRUE(bombs > 0 && raids > 0);
            }
        }
    } TEST_END();

    TEST_CASE("HD1.4 The pass is the same every time, and a recording that does not play out is no stats at all (and no exception): the totals are never made by a pass that went wrong") {
        Real m = load(0);
        if (!m.ok) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        const replay::StatsPlay a = replay::play_with_stats(m.rep, m.level), b = replay::play_with_stats(m.rep, m.level);
        const HistoryRecord rec = record_for({0});
        ASSERT_TRUE(a.stats.ok && make_history_details(a.stats, rec) == make_history_details(b.stats, rec) && !make_history_details(a.stats, rec).empty());
        replay::Replay bad = m.rep;
        ASSERT_TRUE(!bad.hashes.empty());
        bad.hashes[0] ^= 1u;                                                          // (a hash that this build does not reproduce)
        const replay::StatsPlay c = replay::play_with_stats(bad, m.level);
        ASSERT_TRUE(!c.outcome.ok && !c.stats.ok && !c.stats.why.empty());
        ASSERT_TRUE(make_history_details(c.stats, rec).empty());
        replay::Replay other = m.rep;
        other.head.sim_rules = 1;                                                     // (rules that this build does not play)
        const replay::StatsPlay d = replay::play_with_stats(other, m.level);
        ASSERT_TRUE(!d.outcome.ran && !d.stats.ok);
    } TEST_END();

    TEST_CASE("HD1.5 Held against an observer of its own that finds the same facts another way (the engine's record of every ant, read by number): the kills, the hit points of each team on each seat, the power-ups, the raids, the food trips and the seats that ran out of ants are the same, on matches with power-ups, thieves, bombs, drownings and a wiped out seat") {
        for (int k : {4, 5}) {
            Real m = load(k);
            if (!m.ok) {
                std::cout << "SKIPPED (no map)\n";
                return;
            }
            replay::MatchStatsCollector collector(m.rep);
            replay::Hooks hooks = collector.hooks();
            Oracle oracle;
            oracle.roster = m.rep.head.roster;
            const auto watch = hooks.after_tick;
            hooks.after_tick = [&](uint32_t turns, sim::SimulationEngine& engine) {
                watch(turns, engine);
                oracle.tick(turns, engine);
            };
            const replay::Outcome outcome = replay::play(m.rep, m.level, hooks);
            ASSERT_TRUE(outcome.ok);
            const MatchStats st = collector.take(outcome.turns);
            ASSERT_TRUE(st.ok);
            ASSERT_EQ(st.kills.size(), oracle.kills.size());
            for (size_t i = 0; i < st.kills.size(); ++i) {
                const auto& o = oracle.kills[i];
                ASSERT_TRUE(st.kills[i].turn == o[0] && st.kills[i].killer == o[1] && st.kills[i].victim == o[2] && st.kills[i].victim_type == o[3] && (st.kills[i].drowned ? 1u : 0u) == o[4]);
            }
            ASSERT_TRUE(st.damage_taken == oracle.taken && st.powerups == oracle.powerups && st.food_trips == oracle.trips);
            for (size_t src = 0; src < 4; ++src) ASSERT_TRUE(st.damage_from[src] == oracle.from[src]);
            ASSERT_TRUE(st.wipe_turn == oracle.wipe_turn && st.wipes == oracle.wipes && st.out_turn == oracle.out_turn);
            ASSERT_EQ(st.raids.size(), oracle.raids.size());
            for (size_t i = 0; i < st.raids.size(); ++i) ASSERT_TRUE(st.raids[i].turn == oracle.raids[i][0] && st.raids[i].thief == oracle.raids[i][1] && st.raids[i].victim == oracle.raids[i][2]);
            for (const MatchStats::Raid& r : st.raids) ASSERT_TRUE(r.points >= 0 && r.points <= 50);
            if (k == 5) ASSERT_TRUE(st.wipes[3] == 1 && st.wipe_turn[3] == 6592 && st.out_turn[3] == -1);           // (a seat with no ant gets one for free from an egg)
            if (k != 4) continue;
            // the numbers of this recording are fixed (seed 31, 7000 turns, four Hard computer players): the golden values
            using A4 = std::array<uint32_t, 4>;
            using L4 = std::array<int64_t, 4>;
            uint32_t drowned = 0;
            for (const MatchStats::Kill& kl : st.kills) drowned += kl.drowned ? 1 : 0;
            ASSERT_TRUE(st.kills.size() == 11 && drowned == 5 && st.raids.size() == 8);
            ASSERT_TRUE(st.bombs_off == A4({2, 1, 0, 0}) && st.hatch_cost == L4({200, 400, 400, 200}) && st.banked == L4({415, 400, 405, 450}));
            ASSERT_TRUE(st.food_trips == A4({13, 14, 15, 22}) && st.food_points == L4({365, 405, 375, 400}));
            ASSERT_TRUE(st.damage_dealt == A4({52, 41, 39, 15}) && st.damage_taken == A4({41, 18, 19, 69}));
            const std::array<std::array<uint32_t, 5>, 4> power = {{{1, 1, 1, 1, 0}, {1, 1, 1, 1, 0}, {0, 1, 1, 1, 0}, {1, 1, 1, 1, 0}}};
            ASSERT_TRUE(st.powerups == power);
            ASSERT_TRUE(st.wipes == A4({0, 0, 0, 0}) && st.samples.size() == 70 && st.samples.back().turn == 7000);
        }
    } TEST_END();

    // ---- the rules of the JSON on hand-made numbers ---------------------------------------------------------------------------------------------

    TEST_CASE("HD1.6 Facts that a world made by hand can show and the matches played in the tests do not: an ant hurt by its own team is nobody's kill and no damage dealt, a seat that runs out of ants twice is wiped out twice, the free hatch of a seat with under 200 points costs what it had, a bomb that was defused did not go off, a Stop is a Stop, and an order names only the ants of its issuer") {
        replay::Replay shell;
        shell.head.roster = 0x03;
        // a bench: an engine with no map and a collector that is told each tick, as play() tells it
        struct Bench {
            sim::SimulationEngine engine;
            replay::MatchStatsCollector collector;
            replay::Hooks hooks;
            uint32_t turns{0};
            explicit Bench(const replay::Replay& r) : collector(r), hooks(collector.hooks()) { engine.init_test_world(60, 60, 1); }
            void step(uint32_t n = 1) {
                for (uint32_t i = 0; i < n; ++i) {
                    engine.tick();
                    ++turns;
                    hooks.after_tick(turns, engine);
                    engine.clear_news_events();
                    engine.clear_audio_events();
                }
            }
            // the fire of its own team burns the ant, one point at a time, until it is gone
            void burn_to_death(uint32_t ant) {
                for (uint32_t guard = 0; guard < 4000 && !engine.get_unit(ant).removed; ++guard) {
                    if (engine.get_unit(ant).loco_action == sim::AntUnit::kActionIdle) engine.resolve_fire_contact(ant, 1, 0);
                    step();
                }
            }
        };
        {
            Bench b(shell);
            const uint32_t first = b.engine.spawn_unit(0, sim::AntType::Worker, {10, 10});
            b.engine.spawn_unit(1, sim::AntType::Worker, {40, 40});                           // (a seat with an ant all along is never wiped out)
            b.step(45);
            b.burn_to_death(first);
            ASSERT_TRUE(b.engine.get_unit(first).removed);
            b.step(5);
            const uint32_t second = b.engine.spawn_unit(0, sim::AntType::Worker, {14, 14});   // an ant again, and out of ants once more
            b.step(5);
            b.burn_to_death(second);
            ASSERT_TRUE(b.engine.get_unit(second).removed);
            b.step(5);
            const MatchStats st = b.collector.take(b.turns);
            ASSERT_TRUE(st.ok);
            ASSERT_EQ(st.kills.size(), size_t{2});
            for (const MatchStats::Kill& k : st.kills) ASSERT_TRUE(k.victim == 0 && k.killer == MatchStats::kNoKiller);   // (no seat killed it: its own fire did)
            ASSERT_EQ(sum_of(st.damage_dealt), int64_t{0});                                    // (hurting yourself deals nothing)
            ASSERT_TRUE(st.damage_taken[0] >= 18 && st.damage_from[0][0] == st.damage_taken[0]);
            ASSERT_TRUE(st.damage_taken[1] == 0 && st.damage_from[1][0] == 0);
            ASSERT_TRUE(st.wipes[0] == 2 && st.wipes[1] == 0);
            ASSERT_TRUE(st.wipe_turn[0] > 45 && st.wipe_turn[1] == -1);
        }
        {
            Bench b(shell);
            b.engine.spawn_unit(0, sim::AntType::Worker, {10, 10});
            b.engine.spawn_unit(1, sim::AntType::Worker, {40, 40});
            b.engine.stats_manager_mut().set_individual_score(0, 50);
            b.engine.stats_manager_mut().set_egg_count(0, 1);
            b.step(2);                                                                         // (the hill has 50 points: they were banked)
            ASSERT_EQ(static_cast<int>(b.engine.try_hatch(0, sim::AntType::Worker, true)), static_cast<int>(sim::SimulationEngine::HatchResult::Started));
            ASSERT_EQ(b.engine.stats_manager().get_player_stats(0).score, 0);                  // (the free hatch took all it had, not 200)
            b.step(2);
            const MatchStats st = b.collector.take(b.turns);
            ASSERT_TRUE(st.ok && st.banked[0] == 50 && st.hatch_cost[0] == 50);
        }
        {
            Bench b(shell);
            const uint32_t planter = b.engine.spawn_unit(0, sim::AntType::Bomber, {10, 10});
            const uint32_t clearer = b.engine.spawn_unit(1, sim::AntType::Bomber, {12, 10});
            b.step(2);
            ASSERT_TRUE(b.engine.plant_bomb(planter, {11, 10}));
            b.step(2);
            ASSERT_TRUE(b.engine.has_bomb_at({11, 10}));
            ASSERT_TRUE(b.engine.defuse_bomb(clearer, {11, 10}));
            b.step(2);
            ASSERT_FALSE(b.engine.has_bomb_at({11, 10}));
            const MatchStats st = b.collector.take(b.turns);
            ASSERT_TRUE(st.ok);
            ASSERT_EQ(sum_of(st.bombs_off), int64_t{0});                                       // (taken away by an ant: it did not go off)
        }
        {
            Bench b(shell);
            const uint32_t own = b.engine.spawn_unit(0, sim::AntType::Worker, {10, 10});
            const uint32_t other = b.engine.spawn_unit(1, sim::AntType::Bomber, {20, 20});
            b.step(2);
            sim::Command stop;
            stop.type = sim::CommandType::Stop;
            stop.issuer = 0;
            stop.ants = {own};
            b.hooks.before_command(b.turns, stop, b.engine);
            sim::Command aimed;                                                                // seat 0 names an ant of seat 1 (a damaged client): not its ant, so no type and no ability
            aimed.type = sim::CommandType::GroupSpecial;
            aimed.issuer = 0;
            aimed.ants = {other};
            b.hooks.before_command(b.turns, aimed, b.engine);
            aimed.ants = {other, own};
            b.hooks.before_command(b.turns, aimed, b.engine);
            const MatchStats st = b.collector.take(b.turns);
            ASSERT_TRUE(st.ok);
            ASSERT_EQ(orders_of(st, 0), 3u);
            ASSERT_EQ(st.orders[static_cast<size_t>(MatchStats::OrderKind::Stop)].size(), size_t{1});
            ASSERT_EQ(st.orders[static_cast<size_t>(MatchStats::OrderKind::Stop)][0][0], 1u);
            ASSERT_TRUE(st.orders[static_cast<size_t>(MatchStats::OrderKind::Move)].empty());
            ASSERT_TRUE(st.orders[static_cast<size_t>(MatchStats::OrderKind::Bomb)].empty());  // (the bomber is not the issuer's)
            ASSERT_EQ(st.orders[static_cast<size_t>(MatchStats::OrderKind::Special)][0][0], 2u);
            ASSERT_EQ(st.orders_naming[0][static_cast<size_t>(sim::AntType::Worker)], 2u);     // (its own worker is named by the Stop and by the second special order; the other seat's bomber by none)
            ASSERT_EQ(st.orders_naming[0][static_cast<size_t>(sim::AntType::Bomber)], 0u);
        }
        {
            Bench b(shell);
            b.engine.spawn_unit(0, sim::AntType::Worker, {10, 10});                           // (seat 1 plays and has no ant at all)
            sim::Command go;
            go.type = sim::CommandType::GroupMove;
            go.issuer = 0;
            b.hooks.before_command(1199, go, b.engine);                                        // the last turn of the first minute, and the first of the second
            b.hooks.before_command(1200, go, b.engine);
            b.step(40);
            ASSERT_TRUE(b.collector.take(b.turns).wipes[1] == 0);                              // (the first 40 turns are the ants being placed: nobody is wiped out then)
            Bench c(shell);
            c.engine.spawn_unit(0, sim::AntType::Worker, {10, 10});
            c.hooks.before_command(1199, go, c.engine);
            c.hooks.before_command(1200, go, c.engine);
            c.step(41);
            const MatchStats st = c.collector.take(c.turns);
            const auto& moves = st.orders[static_cast<size_t>(MatchStats::OrderKind::Move)];
            ASSERT_TRUE(moves.size() == 2 && moves[0][0] == 1 && moves[1][0] == 1);
            ASSERT_TRUE(st.wipes[1] == 1 && st.wipe_turn[1] == 41 && st.wipes[0] == 0 && st.wipe_turn[0] == -1);          // (the first look after the 40 turns)
        }
    } TEST_END();

    TEST_CASE("HD2.1 The series: a point every 15 seconds, at the look that ends it; more turns than 48 points of 15 seconds make the step longer; a seat that did not play has no points") {
        MatchStats st = blank(14405, 0x0B);
        for (MatchStats::Sample& sm : st.samples) {
            sm.score[0] = static_cast<int32_t>(sm.turn);
            sm.killed[1] = sm.turn / 100;
            sm.score[2] = 7;                                                          // (seat 2 is not in the roster)
        }
        const JsonValue d = details_of(st, record_for({0}));
        ASSERT_TRUE(d.get("v").as_int_or(0) == 1 && d.get("turns").as_int_or(0) == 14405 && d.get("step").as_int_or(0) == 300);
        const JsonValue& score = d.get("over_time").get("score");
        ASSERT_TRUE(score.size() == 4 && score.at(0).size() == 48 && score.at(2).size() == 0 && score.at(3).size() == 48);
        ASSERT_TRUE(at_int(score, 0, 0) == 300 && at_int(score, 0, 10) == 3300 && at_int(score, 0, 47) == 14400);
        ASSERT_TRUE(at_int(d.get("over_time").get("kills"), 1, 0) == 3 && at_int(d.get("over_time").get("kills"), 1, 47) == 144);
        MatchStats longer = blank(36000);
        for (MatchStats::Sample& sm : longer.samples) sm.score[1] = static_cast<int32_t>(sm.turn);
        const JsonValue e = details_of(longer, record_for({1}));
        ASSERT_TRUE(e.get("step").as_int_or(0) == 800 && e.get("over_time").get("score").at(1).size() == 45);          // (36000 / 800)
        ASSERT_TRUE(at_int(e.get("over_time").get("score"), 1, 0) == 800 && at_int(e.get("over_time").get("score"), 1, 44) == 36000);
        for (const uint32_t turns : {14700u, 14999u}) {                                              // (49 points of 15 seconds would not fit: the step is 400 turns, 36 or 37 points)
            MatchStats lg = blank(turns);
            for (MatchStats::Sample& sm : lg.samples) sm.score[0] = static_cast<int32_t>(sm.turn);
            const JsonValue g = details_of(lg, record_for({0}));
            ASSERT_TRUE(g.get("step").as_int_or(0) == 400 && g.get("over_time").get("score").at(0).size() == turns / 400);
        }
        MatchStats mode187 = blank(3000);
        const JsonValue f = details_of(mode187, record_for({0}, false, kHistoryMode187));
        ASSERT_TRUE(f.get("over_time").has("kills") && !f.get("over_time").has("score"));                                // (in 187 the score is the kills)
    } TEST_END();

    TEST_CASE("HD2.2 Who led: the box of the score decides, a change counts when the new leader holds the lead at two looks in a row (from the first of them) or at the last look, a tie leaves the lead, an ally is named") {
        const auto lead_of = [](const MatchStats& st) { return details_of(st, record_for({0})).get("highlights").get("lead"); };
        MatchStats a = blank(1000, 0x07);
        for (MatchStats::Sample& sm : a.samples) sm.box[0] = sm.score[0] = 100;
        a.samples[4].box[1] = a.samples[4].score[1] = 150;                              // one look ahead of the leader: no change
        JsonValue lead = lead_of(a);
        ASSERT_TRUE(lead.size() == 1 && at_int(lead, 0, 0) == 100 && at_int(lead, 0, 1) == 0 && at_int(lead, 0, 2) == -1);
        for (size_t i = 4; i < a.samples.size(); ++i) a.samples[i].box[1] = a.samples[i].score[1] = 150;      // two looks in a row (and on): a change, from the first of them
        lead = lead_of(a);
        ASSERT_TRUE(lead.size() == 2 && at_int(lead, 1, 0) == 500 && at_int(lead, 1, 1) == 1);
        MatchStats tie = blank(1000, 0x07);
        for (size_t i = 0; i < tie.samples.size(); ++i) {
            tie.samples[i].box[0] = tie.samples[i].score[0] = 100;
            if (i >= 4) tie.samples[i].box[1] = tie.samples[i].score[1] = 100;            // equal: seat 0 keeps the lead
        }
        lead = lead_of(tie);
        ASSERT_TRUE(lead.size() == 1 && at_int(lead, 0, 1) == 0);
        MatchStats held = blank(1000, 0x07);                                            // seat 1 leads and seat 0 draws level at the fifth look: the lead stays with seat 1 (the incumbent, not the lower number)
        for (size_t i = 0; i < held.samples.size(); ++i) {
            held.samples[i].box[1] = held.samples[i].score[1] = 100;
            held.samples[i].box[0] = held.samples[i].score[0] = i >= 4 ? 100 : 50;
        }
        lead = lead_of(held);
        ASSERT_TRUE(lead.size() == 1 && at_int(lead, 0, 1) == 1);
        MatchStats last = blank(1000, 0x07);
        for (MatchStats::Sample& sm : last.samples) sm.box[0] = sm.score[0] = 100;
        last.samples.back().box[2] = last.samples.back().score[2] = 400;                // ahead at the very last look only: it counts
        lead = lead_of(last);
        ASSERT_TRUE(lead.size() == 2 && at_int(lead, 1, 0) == 1000 && at_int(lead, 1, 1) == 2);
        MatchStats team = blank(500, 0x07);
        for (MatchStats::Sample& sm : team.samples) {                                   // seats 0 and 1 are allies (the box adds both); seat 1 has the most of its own
            sm.box[0] = sm.box[1] = 300;
            sm.score[0] = 100;
            sm.score[1] = 200;
            sm.box[2] = sm.score[2] = 250;
        }
        lead = lead_of(team);
        ASSERT_TRUE(lead.size() == 1 && at_int(lead, 0, 1) == 1 && at_int(lead, 0, 2) == 0);
        const MatchStats nobody = blank(500, 0x07);
        ASSERT_FALSE(details_of(nobody, record_for({0})).get("highlights").has("lead"));
    } TEST_END();

    TEST_CASE("HD2.3 The comeback is how far the winning side was behind the best other side at its worst (a side of two by its first seat, with its ally); none for a draw, for a winner that never trailed, or for a seat alone") {
        MatchStats st = blank(1000, 0x0F);
        for (size_t i = 0; i < st.samples.size(); ++i) {
            st.samples[i].box[0] = 500;
            st.samples[i].box[1] = 300;
            st.samples[i].box[2] = i == 3 ? 100 : 400;                                  // the winner (seat 2) is 400 behind at the 4th look, 100 behind otherwise
            st.samples[i].box[3] = 0;
        }
        JsonValue c = details_of(st, record_for({2})).get("highlights").get("comeback");
        ASSERT_TRUE(c.get("seat").as_int_or(-1) == 2 && c.get("behind").as_int_or(0) == 400 && c.get("t").as_int_or(0) == 400 && !c.has("ally"));
        c = details_of(st, record_for({2, 3})).get("highlights").get("comeback");
        ASSERT_TRUE(c.get("seat").as_int_or(-1) == 2 && c.get("ally").as_int_or(-1) == 3 && c.get("behind").as_int_or(0) == 400);
        ASSERT_FALSE(details_of(st, record_for({2}, true)).get("highlights").has("comeback"));                          // (a draw has no winner)
        ASSERT_FALSE(details_of(st, record_for({0})).get("highlights").has("comeback"));                                // (the leader never trailed)
        ASSERT_FALSE(details_of(blank(1000, 0x01), record_for({0})).get("highlights").has("comeback"));                 // (alone)
        HistoryRecord none = record_for({0});
        none.rows.clear();                                                              // (no rows: the best box at the last look)
        ASSERT_TRUE(details_of(st, none).get("highlights").get("comeback").size() == 0);
    } TEST_END();

    TEST_CASE("HD2.4 First blood and the last kill are the first and the last that a seat made (a death with no killer is neither); the best burst is three or more kills by one seat within a minute, the bounds included") {
        MatchStats st = blank(6000);
        st.kills = {kill(100, MatchStats::kNoKiller, 1), kill(500, 2, 1), kill(1000, 0, 1), kill(1400, 0, 2), kill(2200, 0, 3), kill(4000, 1, 0), kill(5000, MatchStats::kNoKiller, 0)};
        JsonValue h = details_of(st, record_for({0})).get("highlights");
        ASSERT_TRUE(h.get("first_blood").get("t").as_int_or(0) == 500 && h.get("first_blood").get("by").as_int_or(-1) == 2 && h.get("first_blood").get("of").as_int_or(-1) == 1);
        ASSERT_TRUE(h.get("last_kill").get("t").as_int_or(0) == 4000 && h.get("last_kill").get("by").as_int_or(-1) == 1);
        ASSERT_TRUE(h.get("burst").get("seat").as_int_or(-1) == 0 && h.get("burst").get("kills").as_int_or(0) == 3 && h.get("burst").get("t0").as_int_or(0) == 1000 && h.get("burst").get("t1").as_int_or(0) == 2200);
        st.kills[4].turn = 2201;                                                        // a minute and a turn: not a burst
        ASSERT_FALSE(details_of(st, record_for({0})).get("highlights").has("burst"));
        MatchStats quiet = blank(6000);
        quiet.kills = {kill(100, MatchStats::kNoKiller, 1)};
        h = details_of(quiet, record_for({0})).get("highlights");
        ASSERT_FALSE(h.has("first_blood") || h.has("last_kill") || h.has("burst"));
        MatchStats out = blank(6000);
        out.wipe_turn = {-1, 3000, 4000, -1};
        out.wipes = {0, 1, 2, 0};
        out.out_turn = {-1, -1, 5000, -1};
        h = details_of(out, record_for({0})).get("highlights");
        ASSERT_TRUE(h.get("wiped_out").size() == 2 && at_int(h.get("wiped_out"), 0, 0) == 1 && at_int(h.get("wiped_out"), 0, 1) == 3000 && at_int(h.get("wiped_out"), 1, 2) == 2);
        ASSERT_TRUE(h.get("out").size() == 1 && at_int(h.get("out"), 0, 0) == 2 && at_int(h.get("out"), 0, 1) == 5000);
    } TEST_END();

    TEST_CASE("HD2.5 The activity: orders by minute add up to the kinds; a last part of a match under 10 seconds counts in the minute before, a longer one is a minute of its own; a kind nobody used is left out") {
        MatchStats st = blank(14405, 0x03);
        auto& moves = st.orders[static_cast<size_t>(MatchStats::OrderKind::Move)];
        moves.resize(13);
        moves[0][0] = 5;
        moves[11][1] = 7;
        moves[12][1] = 2;                                                               // (the 5 turns after minute 12)
        st.orders[static_cast<size_t>(MatchStats::OrderKind::Bomb)].resize(3);
        st.orders[static_cast<size_t>(MatchStats::OrderKind::Bomb)][2][0] = 4;
        st.orders_naming[0][1] = 4;
        st.orders_naming[0][0] = 5;
        JsonValue a = details_of(st, record_for({0})).get("activity");
        ASSERT_TRUE(a.get("per_minute").size() == 4 && a.get("per_minute").at(0).size() == 12 && a.get("per_minute").at(2).size() == 0);
        ASSERT_TRUE(at_int(a.get("per_minute"), 0, 0) == 5 && at_int(a.get("per_minute"), 0, 2) == 4 && at_int(a.get("per_minute"), 1, 11) == 9 && at_int(a.get("per_minute"), 1, 10) == 0);
        ASSERT_TRUE(a.get("by_kind").size() == 2 && a.get("by_kind").get("move").at(1).as_int_or(0) == 9 && a.get("by_kind").get("bomb").at(0).as_int_or(0) == 4 && !a.get("by_kind").has("raid"));
        ASSERT_TRUE(a.get("ants").at(0).get("Bomber").as_int_or(0) == 4 && a.get("ants").at(0).get("Worker").as_int_or(0) == 5 && a.get("ants").at(0).size() == 2 && a.get("ants").at(1).size() == 0);
        MatchStats cut = blank(1705, 0x03);                                             // (1:25: a second minute of 25 seconds)
        cut.orders[0].resize(2);
        cut.orders[0][1][0] = 3;
        a = details_of(cut, record_for({0})).get("activity");
        ASSERT_TRUE(a.get("per_minute").at(0).size() == 2 && at_int(a.get("per_minute"), 0, 1) == 3);
        ASSERT_TRUE(details_of(blank(700, 0x01), record_for({0})).get("activity").get("per_minute").at(0).size() == 1);
    } TEST_END();

    TEST_CASE("HD2.6 Combat and economy: the matrix counts every kill (a kill of nobody is not in it); the lists of events are the first 30 and say how many there were; the sums of the raids count all of them") {
        MatchStats st = blank(20000);
        for (uint32_t i = 0; i < 40; ++i) st.kills.push_back(kill(100 + i * 10, static_cast<uint8_t>(i % 2), static_cast<uint8_t>(2 + i % 2), static_cast<uint8_t>(i % 6), i % 5 == 0));
        st.kills.push_back(kill(900, MatchStats::kNoKiller, 3));
        for (uint32_t i = 0; i < 35; ++i) st.raids.push_back(MatchStats::Raid{200 + i, static_cast<uint8_t>(i % 2 + 2), static_cast<uint8_t>(i % 2), 50});
        st.damage_dealt = {5, 6, 7, 8};
        st.damage_taken = {1, 2, 3, 4};
        st.damage_from[0][2] = 9;
        st.food_trips = {1, 2, 3, 4};
        st.food_points = {100, 200, 300, 400};
        st.banked = {10, 20, 30, 40};
        st.hatch_cost = {0, 200, 0, 0};
        st.powerups[1] = {1, 0, 2, 0, 3};
        st.bombs_off = {4, 3, 2, 1};
        const JsonValue c = details_of(st, record_for({0})).get("combat");
        ASSERT_TRUE(at_int(c.get("kills"), 0, 2) == 20 && at_int(c.get("kills"), 1, 3) == 20 && at_int(c.get("kills"), 0, 3) == 0 && at_int(c.get("kills"), 3, 3) == 0);
        ASSERT_TRUE(c.get("ev").size() == 30 && c.get("ev_n").as_int_or(0) == 41 && at_int(c.get("ev"), 0, 0) == 100 && at_int(c.get("ev"), 0, 4) == 1 && at_int(c.get("ev"), 1, 4) == 0);
        ASSERT_TRUE(at_int(c.get("ev"), 1, 1) == 1 && at_int(c.get("ev"), 1, 2) == 3 && at_int(c.get("ev"), 2, 3) == 2);
        ASSERT_TRUE(c.get("raids").size() == 30 && c.get("raids_n").as_int_or(0) == 35 && at_int(c.get("raids"), 0, 1) == 2);
        ASSERT_TRUE(c.get("stolen").at(2).as_int_or(0) == 18 * 50 && c.get("stolen").at(3).as_int_or(0) == 17 * 50 && c.get("robbed").at(0).as_int_or(0) == 18 * 50 && c.get("robbed").at(1).as_int_or(0) == 17 * 50);
        ASSERT_TRUE(c.get("dealt").at(3).as_int_or(0) == 8 && c.get("taken").at(2).as_int_or(0) == 3 && at_int(c.get("hp"), 0, 2) == 9);
        MatchStats few = blank(2000);
        few.kills.push_back(kill(300, 1, 0));
        const JsonValue f = details_of(few, record_for({0})).get("combat");
        ASSERT_TRUE(f.get("ev").size() == 1 && !f.has("ev_n") && !f.has("raids") && !f.has("raids_n"));
        const JsonValue e = details_of(st, record_for({0})).get("economy");
        ASSERT_TRUE(at_int(e.get("food"), 2, 0) == 3 && at_int(e.get("food"), 2, 1) == 300 && e.get("banked").at(3).as_int_or(0) == 40 && e.get("hatch_cost").at(1).as_int_or(0) == 200);
        ASSERT_TRUE(at_int(e.get("taken"), 1, 2) == 2 && at_int(e.get("taken"), 1, 4) == 3 && e.get("bombs_off").at(0).as_int_or(0) == 4);
    } TEST_END();

    TEST_CASE("HD2.7 A pass that did not count gives no text and the marker is one fixed text; a match with no look at all (shorter than 5 seconds) still has the rest") {
        MatchStats bad;
        bad.ok = false;
        bad.why = "x";
        ASSERT_TRUE(make_history_details(bad, record_for({0})).empty());
        ASSERT_EQ(history_details_marker(), std::string("{\"v\":1,\"n\":1}"));
        const JsonValue m = parse_text(history_details_marker());
        ASSERT_TRUE(m.get("v").as_int_or(0) == kDetailsVersion && m.get("n").as_int_or(0) == 1 && !m.has("over_time"));
        const JsonValue d = details_of(blank(50, 0x03), record_for({0}));
        ASSERT_TRUE(d.get("v").as_int_or(0) == 1 && !d.has("over_time") && d.has("activity") && d.has("combat") && d.has("economy"));
    } TEST_END();

    // ---- the text: its layout and its size ------------------------------------------------------------------------------------------------------

    TEST_CASE("HD3.1 The layout of a real match: the keys that docs/REPLAYS.md names, arrays of four where it says so, and a text well under the cap") {
        Real m = load(0);
        if (!m.ok) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        const replay::StatsPlay p = replay::play_with_stats(m.rep, m.level);
        const HistoryRecord rec = make_history_record(id_at(kT0), kT0, m.rep, p.outcome);
        const std::string text = make_history_details(p.stats, rec);
        ASSERT_TRUE(!text.empty() && text.size() < 3000 && text.find(' ') == std::string::npos);
        const JsonValue d = parse_text(text);
        ASSERT_TRUE(d.is_object() && d.get("v").as_int_or(0) == 1 && d.get("turns").as_int_or(0) == static_cast<int64_t>(m.rep.total_turns));
        for (const char* key : {"over_time", "highlights", "activity", "combat", "economy"}) ASSERT_TRUE(d.get(key).is_object());
        ASSERT_TRUE(d.get("over_time").get("score").size() == 4 && d.get("over_time").get("kills").size() == 4);
        for (const char* key : {"dealt", "taken", "stolen", "robbed"}) ASSERT_EQ(d.get("combat").get(key).size(), size_t{4});
        for (const char* key : {"banked", "hatch_cost", "bombs_off"}) ASSERT_EQ(d.get("economy").get(key).size(), size_t{4});
        ASSERT_TRUE(d.get("combat").get("kills").size() == 4 && d.get("combat").get("kills").at(0).size() == 4 && d.get("combat").get("hp").size() == 4 && d.get("economy").get("taken").at(0).size() == 5);
        ASSERT_TRUE(d.get("activity").get("per_minute").size() == 4 && d.get("activity").get("ants").size() == 4 && d.get("economy").get("food").at(0).size() == 2);
        // the keys are the ones that the page reads and nothing else
        ASSERT_TRUE(d.keys() == std::vector<std::string>({"v", "turns", "step", "over_time", "highlights", "activity", "combat", "economy"}));
        ASSERT_TRUE(d.get("combat").keys() == std::vector<std::string>({"kills", "ev", "dealt", "taken", "hp", "raids", "stolen", "robbed"}) || d.get("combat").keys() == std::vector<std::string>({"kills", "ev", "dealt", "taken", "hp", "stolen", "robbed"}) ||
                    d.get("combat").keys() == std::vector<std::string>({"kills", "dealt", "taken", "hp", "stolen", "robbed"}));
        // the record's own JSON gives it back whole under "details"
        HistoryRecord with = rec;
        with.details = text;
        const JsonValue j = history_record_to_json(with, true);
        ASSERT_TRUE(j.get("details").get("v").as_int_or(0) == 1 && j.get("details").get("activity").size() == 3);
        ASSERT_TRUE(history_record_to_json(with, false).get("details").is_null());                                  // (the list stays light)
    } TEST_END();

    TEST_CASE("HD3.2 The text never passes 5000 bytes: a 30 minute match of four seats with big numbers and hundreds of events sheds the list of kills, then the list of raids, then points of the series, and stays a valid text of the same layout") {
        MatchStats st = blank(36000);
        uint32_t x = 12345;
        const auto rnd = [&x](uint32_t n) { x = x * 1664525u + 1013904223u; return (x >> 8) % n; };
        for (MatchStats::Sample& sm : st.samples) {
            for (size_t s = 0; s < 4; ++s) {
                sm.score[s] = static_cast<int32_t>(10000 + rnd(80000));
                sm.box[s] = sm.score[s];
                sm.killed[s] = sm.turn / 20 + rnd(30);
            }
        }
        for (uint32_t i = 0; i < 250; ++i) st.kills.push_back(kill(100 + i * 100, static_cast<uint8_t>(rnd(4)), static_cast<uint8_t>(rnd(4)), static_cast<uint8_t>(rnd(6))));
        for (uint32_t i = 0; i < 250; ++i) st.raids.push_back(MatchStats::Raid{200 + i * 100, static_cast<uint8_t>(rnd(4)), static_cast<uint8_t>(rnd(4)), 50});
        for (auto& kind : st.orders) {
            kind.resize(30);
            for (auto& minute : kind) {
                for (size_t s = 0; s < 4; ++s) minute[s] = rnd(400);
            }
        }
        for (size_t s = 0; s < 4; ++s) {
            st.food_points[s] = 99999;
            st.banked[s] = 999999;
            for (size_t t = 0; t < 6; ++t) st.orders_naming[s][t] = 9999;
            for (size_t t = 0; t < 4; ++t) st.damage_from[s][t] = 9999;
        }
        const std::string text = make_history_details(st, record_for({0, 1}));
        ASSERT_EQ(kDetailsMaxBytes, size_t{5000});                                                  // (the cap is written out here: a constant that moves must be noticed)
        ASSERT_TRUE(!text.empty() && text.size() <= 5000);
        const JsonValue d = parse_text(text);
        ASSERT_TRUE(d.get("v").as_int_or(0) == 1 && d.get("combat").get("kills").size() == 4 && d.get("combat").has("dealt") && d.get("activity").get("per_minute").at(0).size() == 30);
        ASSERT_FALSE(d.get("combat").has("ev"));                                                    // (the lists went first)
        ASSERT_TRUE(d.get("over_time").get("score").at(0).size() <= kDetailsMaxPoints);
        // a match that fits keeps everything: the same match with few events
        MatchStats small = st;
        small.kills.resize(12);
        small.raids.resize(10);
        for (auto& kind : small.orders) kind.clear();
        for (MatchStats::Sample& sm : small.samples) {
            for (size_t s = 0; s < 4; ++s) sm.score[s] = sm.box[s] = static_cast<int32_t>(sm.turn / 10);
        }
        const JsonValue e = parse_text(make_history_details(small, record_for({0})));
        ASSERT_TRUE(e.get("combat").has("ev") && e.get("combat").has("raids") && e.get("combat").get("ev").size() == 12);
        // the series of the longest match that a recording can hold (29 hours) has the same number of points as any other
        MatchStats longest = blank(replay::kMaxTurns - replay::kMaxTurns % 100);
        ASSERT_TRUE(parse_text(make_history_details(longest, record_for({0}))).get("over_time").get("kills").at(0).size() <= kDetailsMaxPoints);
    } TEST_END();

    TEST_CASE("HD3.4 The cap is hard: a match so long that even its minutes of orders do not fit leaves the marker, and one that fits once combat and economy are dropped keeps the rest, a valid text under 5000 bytes") {
        const auto fill = [](uint32_t minutes) {
            MatchStats st = blank(minutes * 1200);
            for (MatchStats::Sample& sm : st.samples) {
                for (size_t s = 0; s < 4; ++s) {
                    sm.score[s] = sm.box[s] = 2000000000 - static_cast<int32_t>(sm.turn);
                    sm.killed[s] = 4000000000u - sm.turn;
                }
            }
            for (auto& kind : st.orders) {
                kind.resize(minutes);
                for (auto& minute : kind) {
                    for (size_t s = 0; s < 4; ++s) minute[s] = 4000000000u;
                }
            }
            for (size_t s = 0; s < 4; ++s) {
                st.food_points[s] = st.banked[s] = st.hatch_cost[s] = INT64_C(9000000000000000000);
                for (size_t t = 0; t < 6; ++t) st.orders_naming[s][t] = 4000000000u;
                for (size_t t = 0; t < 4; ++t) st.damage_from[s][t] = 4000000000u;
                st.damage_dealt[s] = st.damage_taken[s] = 4000000000u;
            }
            return st;
        };
        const std::string hour = make_history_details(fill(60), record_for({0}));
        ASSERT_TRUE(!hour.empty() && hour.size() <= 5000);
        const JsonValue h = parse_text(hour);
        ASSERT_TRUE(h.get("v").as_int_or(0) == 1 && h.has("activity") && h.get("activity").get("per_minute").at(0).size() == 60 && !h.has("combat") && !h.has("economy"));   // (the last lists to go)
        const std::string two_hours = make_history_details(fill(120), record_for({0}));
        ASSERT_EQ(two_hours, history_details_marker());                                         // (nothing short enough is left to say: the pass is told as not counted)
        ASSERT_TRUE(two_hours.size() <= 5000);
    } TEST_END();

    TEST_CASE("HD3.3 A text that is too long sheds its lists in a fixed order: the list of kills goes first, then the list of raids, then points; the sizes of a sweep of match lengths cross every step and the text stays under the cap") {
        bool both = false, only_raids = false, neither = false, fewer_points = false;
        uint32_t x = 99;
        const auto rnd = [&x](uint32_t n) { x = x * 1664525u + 1013904223u; return (x >> 8) % n; };
        for (uint32_t turns = 6000; turns <= 60000; turns += 1200) {
            MatchStats st = blank(turns);
            for (MatchStats::Sample& sm : st.samples) {
                for (size_t s = 0; s < 4; ++s) {
                    sm.score[s] = static_cast<int32_t>(30000 + rnd(60000));
                    sm.box[s] = sm.score[s];
                    sm.killed[s] = 100 + rnd(900);
                }
            }
            for (uint32_t i = 0; i < 40; ++i) st.kills.push_back(kill(100 + i * 100, static_cast<uint8_t>(rnd(4)), static_cast<uint8_t>(rnd(4)), static_cast<uint8_t>(rnd(6))));
            for (uint32_t i = 0; i < 40; ++i) st.raids.push_back(MatchStats::Raid{200 + i * 100, static_cast<uint8_t>(rnd(4)), static_cast<uint8_t>(rnd(4)), 50});
            for (auto& kind : st.orders) {
                kind.resize(turns / 1200);
                for (auto& minute : kind) {
                    for (size_t s = 0; s < 4; ++s) minute[s] = rnd(300);
                }
            }
            const std::string text = make_history_details(st, record_for({0}));
            ASSERT_TRUE(!text.empty() && text.size() <= 5000);
            const JsonValue d = parse_text(text);
            const bool ev = d.get("combat").has("ev"), raids = d.get("combat").has("raids");
            ASSERT_TRUE(!ev || raids);                                                      // (never the list of raids without that of kills gone first)
            if (ev && raids) both = true;
            if (!ev && raids) only_raids = true;
            if (!ev && !raids) neither = true;
            if (!ev && !raids && d.get("step").as_int_or(0) > 300 && d.get("over_time").get("kills").at(0).size() <= kDetailsMaxPoints) fewer_points = true;
        }
        ASSERT_TRUE(both && only_raids && neither && fewer_points);
    } TEST_END();

    // ---- the feeder -------------------------------------------------------------------------------------------------------------------------------

    TEST_CASE("HD4.1 A match that the feeder counts has its details in the same play: the text is the one that a pass of the stored recording makes, the totals are the plain play's, and the record's own JSON gives it back") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string id = rig.keep(match_file(0));
        rig.drain(4);
        HistoryRecord rec;
        ASSERT_TRUE(rig.history.find(id, rec) && !rec.details.empty() && rig.feeder.counted() == 1 && rig.feeder.backfilled() == 0);
        Real m = load(0);
        const replay::StatsPlay p = replay::play_with_stats(m.rep, m.level);
        HistoryRecord again = make_history_record(id, rec.ended_s, m.rep, p.outcome);
        ASSERT_EQ(rec.details, make_history_details(p.stats, again));
        again.details = rec.details;
        ASSERT_TRUE(rec.seats.size() == 4 && rec.rows.size() == again.rows.size());
        ASSERT_TRUE(rec.seats[0].killed == p.outcome.result.stats[0].enemy_killed && rec.seats[1].score == p.outcome.result.stats[1].score);
        ASSERT_TRUE(parse_text(rig.history.query(HistoryQuery(), [](const std::string&) { return Recording::Watch; }).items.front().details).get("v").as_int_or(0) == 1);
        const JsonValue j = history_record_to_json(rec, true);
        ASSERT_TRUE(j.get("details").get("highlights").is_object() && j.get("details").get("turns").as_int_or(0) == static_cast<int64_t>(rec.turns));
    } TEST_END();

    TEST_CASE("HD4.2 An older record without details gets them from its recording, newest first and one at a time at the feeder's pace, only while the server is quiet; its totals and the play counts of the maps stay as they were; a line says how many") {
        if (match_file(1).empty() || match_file(2).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string older = rig.keep(match_file(1));
        const std::string newer = rig.keep(match_file(2));
        std::vector<HistoryRecord> bare;
        for (const std::string& id : {older, newer}) {
            const Played pl = play_file(match_file(id == older ? 1 : 2));
            ASSERT_TRUE(pl.ok);
            HistoryRecord r = make_history_record(id, rig.now, pl.rep, pl.outcome);
            ASSERT_TRUE(r.details.empty());
            std::string why;
            ASSERT_TRUE(rig.history.put(r, why));                                     // (counted by a server that kept no details)
            bare.push_back(r);
        }
        int64_t plays_before = 0;
        std::vector<MapPlays> plays;
        ASSERT_TRUE(rig.history.map_plays(plays) && !plays.empty());
        for (const MapPlays& mp : plays) plays_before += mp.plays;
        std::vector<std::string> ids;
        ASSERT_TRUE(rig.history.ids_without_details(ids) && ids.size() == 2);
        ASSERT_FALSE(rig.step(false));                                               // (not quiet: nothing is played)
        HistoryRecord r1, r2;
        ASSERT_TRUE(rig.history.find(newer, r2) && r2.details.empty() && rig.feeder.backfilled() == 0);
        ASSERT_TRUE(rig.step());                                                     // the newest first
        ASSERT_TRUE(rig.history.find(newer, r2) && !r2.details.empty() && rig.history.find(older, r1) && r1.details.empty() && rig.feeder.backfilled() == 1 && rig.feeder.pending() == 1);
        const uint32_t before = rig.ms;
        ASSERT_FALSE(rig.feeder.step([] { return true; }, before + 100));            // (faster than the pace)
        ASSERT_TRUE(rig.history.find(older, r1) && r1.details.empty());
        ASSERT_TRUE(rig.step() && rig.history.find(older, r1) && !r1.details.empty() && rig.feeder.backfilled() == 2 && rig.feeder.pending() == 0);
        rig.drain(5);
        ASSERT_EQ(lines_with(rig.history.take_notes(), "the stats of 2 older match(es) counted from their recordings"), size_t{1});
        ASSERT_TRUE(rig.feeder.counted() == 0 && rig.history.count() == 2);          // (nothing was counted again)
        int64_t plays_after = 0;
        ASSERT_TRUE(rig.history.map_plays(plays));
        for (const MapPlays& mp : plays) plays_after += mp.plays;
        ASSERT_EQ(plays_after, plays_before);
        ASSERT_TRUE(rig.history.find(older, r1) && r1.seats.size() == bare[0].seats.size() && r1.rows.size() == bare[0].rows.size() && r1.turns == bare[0].turns);
        for (size_t s = 0; s < r1.seats.size(); ++s) ASSERT_TRUE(r1.seats[s].score == bare[0].seats[s].score && r1.seats[s].killed == bare[0].seats[s].killed && r1.seats[s].lost == bare[0].seats[s].lost);
        std::vector<std::string> none;
        ASSERT_TRUE(rig.history.ids_without_details(none) && none.empty());
        const size_t asked = static_cast<size_t>(rig.idle_asked);
        rig.drain(5);
        ASSERT_EQ(static_cast<size_t>(rig.idle_asked), asked);                       // (nothing left to do: not even asked)
    } TEST_END();

    TEST_CASE("HD4.3 A recording that cannot be played leaves the marker (so that it is not played again at every start) and a line in the log; a record that has details is never touched; one whose recording is gone is left alone") {
        if (match_file(1).empty() || match_file(2).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const Played pl = play_file(match_file(1));
        ASSERT_TRUE(pl.ok && !pl.rep.hashes.empty());
        replay::Replay bad = pl.rep;
        bad.hashes[0] ^= 1u;                                                           // (the first check of the file is wrong: a recording that is read and never plays out)
        std::string why;
        const std::vector<uint8_t> broken = replay::encode(bad, why);
        ASSERT_FALSE(broken.empty());
        const std::string unplayable = rig.keep(broken);
        ASSERT_FALSE(unplayable.empty());
        const std::string has = rig.keep(match_file(2));
        const std::string gone = id_at(kT0 + 777);
        HistoryRecord a = make_history_record(unplayable, rig.now, pl.rep, pl.outcome);
        HistoryRecord b = make_history_record(has, rig.now, pl.rep, pl.outcome);
        b.details = "{\"v\":1,\"x\":[1]}";
        HistoryRecord c = make_history_record(gone, kT0 + 777, pl.rep, pl.outcome);
        ASSERT_TRUE(rig.history.put(a, why) && rig.history.put(b, why) && rig.history.put(c, why));
        rig.drain(8);
        HistoryRecord got;
        ASSERT_TRUE(rig.history.find(unplayable, got) && got.details == history_details_marker());
        ASSERT_TRUE(rig.history.find(has, got) && got.details == "{\"v\":1,\"x\":[1]}");
        ASSERT_TRUE(rig.history.find(gone, got) && got.details.empty());
        ASSERT_EQ(rig.feeder.backfilled(), size_t{1});
        ASSERT_EQ(lines_with(rig.history.take_notes(), unplayable + " has no stats"), size_t{1});
        const size_t asked = static_cast<size_t>(rig.idle_asked);
        rig.drain(8);                                                                 // (not tried again)
        ASSERT_EQ(static_cast<size_t>(rig.idle_asked), asked);
        // set_details is a statement for a record without details only
        ASSERT_TRUE(rig.history.set_details(has, "{\"v\":1,\"y\":1}", why) && rig.history.find(has, got) && got.details == "{\"v\":1,\"x\":[1]}");
        ASSERT_TRUE(rig.history.set_details(gone, "{\"v\":1,\"y\":1}", why) && rig.history.find(gone, got) && got.details == "{\"v\":1,\"y\":1}");
        ASSERT_TRUE(rig.history.set_details(id_at(kT0 + 4242), "{\"v\":1}", why));    // (a record that is not there: nothing to do)
        ASSERT_FALSE(rig.history.set_details("not an id", "{\"v\":1}", why));
    } TEST_END();

    TEST_CASE("HD4.4 The new matches come before the old ones: a recording that the history does not know is counted (with its details) before any record without details is played again") {
        if (match_file(1).empty() || match_file(2).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string old_id = rig.keep(match_file(1));
        const Played pl = play_file(match_file(1));
        ASSERT_TRUE(pl.ok);
        std::string why;
        ASSERT_TRUE(rig.history.put(make_history_record(old_id, rig.now, pl.rep, pl.outcome), why));
        const std::string fresh = rig.keep(match_file(2));
        ASSERT_TRUE(rig.step());
        HistoryRecord r;
        ASSERT_TRUE(rig.history.knows(fresh) && rig.history.find(fresh, r) && !r.details.empty());
        ASSERT_TRUE(rig.history.find(old_id, r) && r.details.empty() && rig.feeder.counted() == 1 && rig.feeder.backfilled() == 0);
        ASSERT_TRUE(rig.step());
        ASSERT_TRUE(rig.history.find(old_id, r) && !r.details.empty() && rig.feeder.backfilled() == 1);
    } TEST_END();

    TEST_CASE("HD4.5 A database that stops answering stops the backfill (nothing is played) and the work goes on when it answers again; a record that the database refuses to update is left without details, with a line, and the store stays up") {
        if (match_file(1).empty() || match_file(2).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string a = rig.keep(match_file(1));
        const std::string b = rig.keep(match_file(2));
        std::string why;
        for (const std::string& id : {a, b}) {
            const Played pl = play_file(match_file(id == a ? 1 : 2));
            ASSERT_TRUE(pl.ok && rig.history.put(make_history_record(id, rig.now, pl.rep, pl.outcome), why));
        }
        db::Database away;
        ASSERT_TRUE(away.open((rig.temp.path / "ants.db").string()).ok());
        ASSERT_TRUE(away.exec("ALTER TABLE history_matches RENAME TO history_matches_away").ok());
        rig.drain(4);                                                                 // (the first look finds the database down; the feeder waits)
        ASSERT_TRUE(!rig.history.available() && rig.feeder.backfilled() == 0);
        const int asked = rig.idle_asked;
        rig.drain(10);
        ASSERT_TRUE(rig.feeder.backfilled() == 0 && rig.idle_asked == asked);
        ASSERT_TRUE(away.exec("ALTER TABLE history_matches_away RENAME TO history_matches").ok());
        // the update of the newer record is refused (a trigger of the test); the older one gets its details
        ASSERT_TRUE(away.exec("CREATE TRIGGER no_b BEFORE UPDATE ON history_matches WHEN NEW.id = '" + b + "' BEGIN SELECT RAISE(ABORT, 'refused by the test'); END").ok());
        for (int minute = 0; minute < 4; ++minute) {                                    // (the store looks again every minute of the server's clock)
            rig.ms += HistoryStore::kRetryMs;
            rig.drain(2);
        }
        rig.drain(6);
        HistoryRecord r;
        ASSERT_TRUE(rig.history.available() && rig.history.find(a, r) && !r.details.empty() && rig.history.find(b, r) && r.details.empty() && rig.feeder.backfilled() == 1);
        ASSERT_TRUE(lines_with(rig.history.take_notes(), "refused by the test") >= 1);
    } TEST_END();

    TEST_CASE("HD4.6 A record that gets its details while it waits in the queue is not played again: the guard of the pass itself, besides the one of the statement") {
        if (match_file(1).empty() || match_file(2).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string older = rig.keep(match_file(1));
        const std::string newer = rig.keep(match_file(2));
        std::string why;
        for (const std::string& id : {older, newer}) {
            const Played pl = play_file(match_file(id == older ? 1 : 2));
            ASSERT_TRUE(pl.ok && rig.history.put(make_history_record(id, rig.now, pl.rep, pl.outcome), why));
        }
        ASSERT_TRUE(rig.step());                                                      // the newer one is done, the older one waits
        ASSERT_TRUE(rig.feeder.backfilled() == 1 && rig.feeder.pending() == 1);
        ASSERT_TRUE(rig.history.set_details(older, "{\"v\":1,\"x\":[2]}", why));      // (another path gave it details meanwhile)
        rig.drain(6);
        HistoryRecord got;
        ASSERT_TRUE(rig.history.find(older, got) && got.details == "{\"v\":1,\"x\":[2]}");
        ASSERT_EQ(rig.feeder.backfilled(), size_t{1});                                // (it was not played, so it is not counted as played)
    } TEST_END();

    TEST_CASE("HD4.7 A map that is not here leaves NO marker (the next start, with the folder, plays the recording), and the map is found in the community folder too; the new files and the older records agree on it") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir nomaps;
        Rig rig(nomaps.path.string());                                                // (the feeder has no map folder: the server started without its maps)
        ASSERT_TRUE(rig.open());
        const std::string id = rig.keep(match_file(1));
        const Played pl = play_file(match_file(1));
        ASSERT_TRUE(pl.ok);
        std::string why;
        ASSERT_TRUE(rig.history.put(make_history_record(id, rig.now, pl.rep, pl.outcome), why));
        rig.drain(8);
        HistoryRecord got;
        ASSERT_TRUE(rig.history.find(id, got) && got.details.empty());                // no marker, no details
        ASSERT_EQ(rig.feeder.backfilled(), size_t{0});
        ASSERT_EQ(lines_with(rig.history.take_notes(), id + " has no stats yet"), size_t{1});
        const int asked = rig.idle_asked;
        rig.drain(8);
        ASSERT_EQ(rig.idle_asked, asked);                                             // (not tried again in this run, not told again)
        // the next start: a feeder with the maps plays it
        HistoryFeeder later(rig.history, rig.replays, maps_dir());
        uint32_t ms = 100000;
        for (int i = 0; i < 8; ++i) later.step([] { return true; }, ms += 300);
        ASSERT_TRUE(rig.history.find(id, got) && !got.details.empty() && got.details != history_details_marker() && later.backfilled() == 1);
        // the same recording where the maps folder has no map and the community folder has it
        Rig players(nomaps.path.string(), maps_dir());
        ASSERT_TRUE(players.open());
        const std::string pid = players.keep(match_file(1));
        ASSERT_TRUE(players.history.put(make_history_record(pid, players.now, pl.rep, pl.outcome), why));
        players.drain(8);
        ASSERT_TRUE(players.history.find(pid, got) && !got.details.empty() && got.details != history_details_marker() && players.feeder.backfilled() == 1);
        // a new file whose map is not here gets no record at all (and no marker): the same rule
        Rig fresh(nomaps.path.string());
        ASSERT_TRUE(fresh.open());
        const std::string fid = fresh.keep(match_file(1));
        fresh.drain(8);
        ASSERT_TRUE(fresh.history.count() == 0 && !fresh.history.knows(fid) && fresh.feeder.left_out() == 1);
    } TEST_END();

    TEST_CASE("HD4.8 The counters of a match are written whatever the pass of the details does: a maker that throws leaves the marker, a line in the log and the record (new file and older record alike)") {
        if (match_file(1).empty() || match_file(2).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        const auto boom = [](const replay::MatchStats&, const HistoryRecord&) -> std::string { throw std::runtime_error("the details broke"); };
        Rig rig;
        ASSERT_TRUE(rig.open());
        rig.feeder.set_details_maker(boom);
        const std::string fresh = rig.keep(match_file(1));
        rig.drain(6);
        HistoryRecord got, plain;
        ASSERT_TRUE(rig.history.find(fresh, got) && got.details == history_details_marker() && rig.feeder.counted() == 1 && rig.feeder.left_out() == 0);
        const Played pl = play_file(match_file(1));
        ASSERT_TRUE(pl.ok);
        plain = make_history_record(fresh, got.ended_s, pl.rep, pl.outcome);
        ASSERT_TRUE(got.seats.size() == plain.seats.size() && got.turns == plain.turns);
        for (size_t s = 0; s < got.seats.size(); ++s) ASSERT_TRUE(got.seats[s].score == plain.seats[s].score && got.seats[s].killed == plain.seats[s].killed && got.seats[s].lost == plain.seats[s].lost);
        const std::vector<std::string> told = rig.history.take_notes();
        ASSERT_TRUE(lines_with(told, fresh + " has no stats") == 1 && lines_with(told, "the details broke") == 1);
        // an older record
        Rig old;
        ASSERT_TRUE(old.open());
        old.feeder.set_details_maker(boom);
        const std::string oid = old.keep(match_file(2));
        const Played p2 = play_file(match_file(2));
        std::string why;
        ASSERT_TRUE(p2.ok && old.history.put(make_history_record(oid, old.now, p2.rep, p2.outcome), why));
        old.drain(6);
        ASSERT_TRUE(old.history.find(oid, got) && got.details == history_details_marker() && old.feeder.backfilled() == 1);
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
