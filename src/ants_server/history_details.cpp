#include "ants_server/history_details.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "ants_ctl/json.hpp"

namespace ants::server {

namespace {

using ctl::JsonValue;
using replay::MatchStats;

constexpr size_t kSeats = MatchStats::kSeats;
constexpr const char* kTypeNames[6] = {"Worker", "Bomber", "Fire", "Thief", "Combat", "Swimmer"};

JsonValue num(int64_t v) { return JsonValue::make_int(v); }

template <class T>
JsonValue ints(const T& values) {            // an array of numbers (a std::array or a std::vector)
    JsonValue a = JsonValue::make_array();
    for (const auto& v : values) a.push_back(num(static_cast<int64_t>(v)));
    return a;
}

JsonValue row(std::initializer_list<int64_t> values) {
    JsonValue a = JsonValue::make_array();
    for (const int64_t v : values) a.push_back(num(v));
    return a;
}

bool plays(const MatchStats& st, size_t seat) { return seat < kSeats && ((st.roster >> seat) & 1u) != 0; }

// The turns between two points of the series: 15 seconds, and more when that would be more than kDetailsMaxPoints points (`stretch` doubles it, for a text that is too long)
uint32_t step_of(uint32_t turns, uint32_t stretch) {
    uint32_t step = 300;
    while (turns / step > kDetailsMaxPoints) step += 100;
    return step * stretch;
}

// ---- over time and highlights ------------------------------------------------------------------------------------------------------------

struct Lead {
    uint32_t turn{0};
    int seat{0};
    int ally{-1};
};

// Who had the most on the score box at each look: a tie leaves the lead where it is (between allies, who have the same box, the seat with more points of its own is named); a change counts when the new
// leader holds the lead at two looks in a row (10 seconds) or is still the leader at the last look
std::vector<Lead> leaders(const MatchStats& st) {
    std::vector<Lead> out;
    int cur = -1, cand = -1, cand_n = 0;
    for (size_t k = 0; k < st.samples.size(); ++k) {
        const MatchStats::Sample& sm = st.samples[k];
        int32_t top = 0;
        bool any = false;
        for (size_t s = 0; s < kSeats; ++s) {
            if (plays(st, s) && (!any || sm.box[s] > top)) {
                top = sm.box[s];
                any = true;
            }
        }
        if (!any || top <= 0) continue;
        std::vector<int> tied;
        for (size_t s = 0; s < kSeats; ++s) {
            if (plays(st, s) && sm.box[s] == top) tied.push_back(static_cast<int>(s));
        }
        std::sort(tied.begin(), tied.end(), [&](int a, int b) { return sm.score[static_cast<size_t>(a)] != sm.score[static_cast<size_t>(b)] ? sm.score[static_cast<size_t>(a)] > sm.score[static_cast<size_t>(b)] : a < b; });
        const int who = std::find(tied.begin(), tied.end(), cur) != tied.end() ? cur : tied[0];
        int ally = -1;                                               // the seat whose own score makes up the rest of the box
        if (sm.box[static_cast<size_t>(who)] != sm.score[static_cast<size_t>(who)]) {
            for (size_t o = 0; o < kSeats; ++o) {
                if (plays(st, o) && static_cast<int>(o) != who && sm.box[o] == sm.box[static_cast<size_t>(who)] && int64_t{sm.score[o]} + sm.score[static_cast<size_t>(who)] == sm.box[static_cast<size_t>(who)]) {
                    ally = static_cast<int>(o);
                    break;
                }
            }
        }
        if (cur < 0) {
            cur = who;
            out.push_back(Lead{sm.turn, who, ally});
            continue;
        }
        if (who == cur) {
            cand = -1;
            cand_n = 0;
            continue;
        }
        if (cand == who) ++cand_n;
        else {
            cand = who;
            cand_n = 1;
        }
        if (cand_n >= 2 || k + 1 == st.samples.size()) {
            cur = who;
            out.push_back(Lead{sm.turn - static_cast<uint32_t>(cand_n - 1) * MatchStats::kSampleTurns, who, ally});
            cand = -1;
            cand_n = 0;
        }
    }
    return out;
}

// The seats of the winning side, from the record; the best final box when the record has no rows; none for a draw
std::vector<int> winning_seats(const MatchStats& st, const HistoryRecord& record) {
    std::vector<int> win;
    if (record.draw) return win;
    if (!record.rows.empty()) {
        for (const uint8_t s : record.rows.front().seats) win.push_back(s);
        return win;
    }
    if (st.samples.empty()) return win;
    const MatchStats::Sample& last = st.samples.back();
    int best = -1;
    for (size_t s = 0; s < kSeats; ++s) {
        if (plays(st, s) && (best < 0 || last.box[s] > last.box[static_cast<size_t>(best)])) best = static_cast<int>(s);
    }
    if (best >= 0) win.push_back(best);
    return win;
}

JsonValue over_time(const MatchStats& st, const HistoryRecord& record, uint32_t step) {
    JsonValue o = JsonValue::make_object();
    const size_t points = std::min<size_t>(st.turns / step, kDetailsMaxPoints * 4);
    JsonValue score = JsonValue::make_array(), kills = JsonValue::make_array();
    for (size_t s = 0; s < kSeats; ++s) {
        JsonValue sc = JsonValue::make_array(), ki = JsonValue::make_array();
        if (plays(st, s)) {
            for (size_t i = 0; i < points; ++i) {
                const size_t at = std::min(st.samples.size() - 1, (i + 1) * step / MatchStats::kSampleTurns - 1);
                sc.push_back(num(st.samples[at].score[s]));
                ki.push_back(num(st.samples[at].killed[s]));
            }
        }
        score.push_back(std::move(sc));
        kills.push_back(std::move(ki));
    }
    if (record.mode != kHistoryMode187) o.set("score", std::move(score));      // (in 187 the score is the kills)
    o.set("kills", std::move(kills));
    return o;
}

JsonValue highlights(const MatchStats& st, const HistoryRecord& record) {
    JsonValue h = JsonValue::make_object();
    const MatchStats::Kill* first = nullptr;
    const MatchStats::Kill* last = nullptr;
    for (const MatchStats::Kill& k : st.kills) {
        if (k.killer >= kSeats) continue;
        if (first == nullptr) first = &k;
        last = &k;
    }
    const auto blood = [](const MatchStats::Kill& k) {
        JsonValue o = JsonValue::make_object();
        o.set("t", num(k.turn));
        o.set("by", num(k.killer));
        o.set("of", num(k.victim));
        return o;
    };
    if (first != nullptr) {
        h.set("first_blood", blood(*first));
        h.set("last_kill", blood(*last));
    }
    const std::vector<Lead> lead = leaders(st);
    if (!lead.empty()) {
        JsonValue l = JsonValue::make_array();
        for (const Lead& e : lead) l.push_back(row({e.turn, e.seat, e.ally}));
        h.set("lead", std::move(l));
    }
    // the comeback: how far behind the best other side the winning side was, at its worst
    const std::vector<int> win = winning_seats(st, record);
    if (!win.empty()) {
        int64_t behind = 0;
        uint32_t at = 0;
        for (const MatchStats::Sample& sm : st.samples) {
            bool any = false;
            int32_t other = 0;
            for (size_t s = 0; s < kSeats; ++s) {
                if (!plays(st, s) || std::find(win.begin(), win.end(), static_cast<int>(s)) != win.end()) continue;
                if (!any || sm.box[s] > other) other = sm.box[s];
                any = true;
            }
            if (!any) continue;
            const int64_t gap = int64_t{other} - sm.box[static_cast<size_t>(win.front())];
            if (gap > behind) {
                behind = gap;
                at = sm.turn;
            }
        }
        if (behind > 0) {
            JsonValue c = JsonValue::make_object();
            c.set("seat", num(win.front()));
            if (win.size() > 1) c.set("ally", num(win[1]));
            c.set("behind", num(behind));
            c.set("t", num(at));
            h.set("comeback", std::move(c));
        }
    }
    JsonValue wiped = JsonValue::make_array(), out = JsonValue::make_array();
    for (size_t s = 0; s < kSeats; ++s) {
        if (!plays(st, s)) continue;
        if (st.wipe_turn[s] >= 0) wiped.push_back(row({static_cast<int64_t>(s), st.wipe_turn[s], st.wipes[s]}));
        if (st.out_turn[s] >= 0) out.push_back(row({static_cast<int64_t>(s), st.out_turn[s]}));
    }
    if (wiped.size() > 0) h.set("wiped_out", std::move(wiped));
    if (out.size() > 0) h.set("out", std::move(out));
    // the best burst: the most kills by one seat inside a minute, at least three (the first found wins a tie)
    int best_seat = -1;
    size_t best_n = 0;
    uint32_t best_t0 = 0, best_t1 = 0;
    for (size_t s = 0; s < kSeats; ++s) {
        std::vector<uint32_t> ts;
        for (const MatchStats::Kill& k : st.kills) {
            if (k.killer == s) ts.push_back(k.turn);
        }
        size_t j = 0;
        for (size_t i = 0; i < ts.size(); ++i) {
            while (ts[i] - ts[j] > MatchStats::kMinuteTurns) ++j;
            const size_t n = i - j + 1;
            if (n >= 3 && n > best_n) {
                best_n = n;
                best_seat = static_cast<int>(s);
                best_t0 = ts[j];
                best_t1 = ts[i];
            }
        }
    }
    if (best_seat >= 0) {
        JsonValue b = JsonValue::make_object();
        b.set("seat", num(best_seat));
        b.set("kills", num(static_cast<int64_t>(best_n)));
        b.set("t0", num(best_t0));
        b.set("t1", num(best_t1));
        h.set("burst", std::move(b));
    }
    return h;
}

// ---- activity, combat, economy ------------------------------------------------------------------------------------------------------------

JsonValue activity(const MatchStats& st) {
    JsonValue a = JsonValue::make_object();
    // whole minutes; a last part of less than ten seconds counts in the minute before it
    const size_t minutes = std::max<size_t>(1, st.turns / MatchStats::kMinuteTurns + (st.turns % MatchStats::kMinuteTurns >= 200 ? 1u : 0u));
    std::array<std::vector<uint32_t>, kSeats> per_minute;
    for (auto& v : per_minute) v.assign(minutes, 0);
    JsonValue kinds = JsonValue::make_object();
    for (size_t k = 0; k < MatchStats::kOrderKinds; ++k) {
        std::array<uint32_t, kSeats> total{};
        for (size_t m = 0; m < st.orders[k].size(); ++m) {
            for (size_t s = 0; s < kSeats; ++s) {
                total[s] += st.orders[k][m][s];
                per_minute[s][std::min(m, minutes - 1)] += st.orders[k][m][s];
            }
        }
        if (total[0] + total[1] + total[2] + total[3] > 0) kinds.set(MatchStats::order_kind_name(static_cast<MatchStats::OrderKind>(k)), ints(total));
    }
    JsonValue pm = JsonValue::make_array(), ants = JsonValue::make_array();
    for (size_t s = 0; s < kSeats; ++s) {
        pm.push_back(plays(st, s) ? ints(per_minute[s]) : JsonValue::make_array());
        JsonValue types = JsonValue::make_object();
        for (size_t t = 0; t < 6; ++t) {
            if (st.orders_naming[s][t] > 0) types.set(kTypeNames[t], num(st.orders_naming[s][t]));
        }
        ants.push_back(std::move(types));
    }
    a.set("per_minute", std::move(pm));
    a.set("by_kind", std::move(kinds));
    a.set("ants", std::move(ants));
    return a;
}

JsonValue combat(const MatchStats& st, bool with_events, bool with_raids) {
    JsonValue c = JsonValue::make_object();
    std::array<std::array<uint32_t, kSeats>, kSeats> matrix{};
    for (const MatchStats::Kill& k : st.kills) {
        if (k.killer < kSeats && k.victim < kSeats) ++matrix[k.killer][k.victim];
    }
    JsonValue km = JsonValue::make_array();
    for (const auto& r : matrix) km.push_back(ints(r));
    c.set("kills", std::move(km));
    if (with_events && !st.kills.empty()) {
        JsonValue ev = JsonValue::make_array();
        for (size_t i = 0; i < st.kills.size() && i < kDetailsMaxEvents; ++i) {
            const MatchStats::Kill& k = st.kills[i];
            ev.push_back(row({k.turn, k.killer < kSeats ? int64_t{k.killer} : int64_t{-1}, k.victim, k.victim_type, k.drowned ? 1 : 0}));
        }
        c.set("ev", std::move(ev));
        if (st.kills.size() > kDetailsMaxEvents) c.set("ev_n", num(static_cast<int64_t>(st.kills.size())));
    }
    c.set("dealt", ints(st.damage_dealt));
    c.set("taken", ints(st.damage_taken));
    JsonValue hp = JsonValue::make_array();
    for (const auto& r : st.damage_from) hp.push_back(ints(r));
    c.set("hp", std::move(hp));
    std::array<int64_t, kSeats> stolen{}, robbed{};
    for (const MatchStats::Raid& r : st.raids) {
        if (r.thief < kSeats) stolen[r.thief] += r.points;
        if (r.victim < kSeats) robbed[r.victim] += r.points;
    }
    if (with_raids && !st.raids.empty()) {
        JsonValue rl = JsonValue::make_array();
        for (size_t i = 0; i < st.raids.size() && i < kDetailsMaxEvents; ++i) rl.push_back(row({st.raids[i].turn, st.raids[i].thief, st.raids[i].victim, st.raids[i].points}));
        c.set("raids", std::move(rl));
        if (st.raids.size() > kDetailsMaxEvents) c.set("raids_n", num(static_cast<int64_t>(st.raids.size())));
    }
    c.set("stolen", ints(stolen));
    c.set("robbed", ints(robbed));
    return c;
}

JsonValue economy(const MatchStats& st) {
    JsonValue e = JsonValue::make_object();
    JsonValue food = JsonValue::make_array(), taken = JsonValue::make_array();
    for (size_t s = 0; s < kSeats; ++s) {
        food.push_back(row({st.food_trips[s], st.food_points[s]}));
        taken.push_back(ints(st.powerups[s]));
    }
    e.set("food", std::move(food));
    e.set("banked", ints(st.banked));
    e.set("hatch_cost", ints(st.hatch_cost));
    e.set("taken", std::move(taken));
    e.set("bombs_off", ints(st.bombs_off));
    return e;
}

// `shed` makes the text smaller, step by step: 1 no list of kills, 2 no list of raids either, 3 and 4 a series of half and a quarter the points, 5 no combat and no economy
std::string build(const MatchStats& st, const HistoryRecord& record, int shed) {
    JsonValue d = JsonValue::make_object();
    d.set("v", num(kDetailsVersion));
    d.set("turns", num(st.turns));
    const uint32_t step = step_of(st.turns, shed >= 4 ? 4u : shed >= 3 ? 2u : 1u);
    d.set("step", num(step));
    if (!st.samples.empty() && st.turns >= step) d.set("over_time", over_time(st, record, step));
    d.set("highlights", highlights(st, record));
    d.set("activity", activity(st));
    if (shed < 5) {
        d.set("combat", combat(st, shed < 1, shed < 2));
        d.set("economy", economy(st));
    }
    return ctl::to_json(d);
}

}  // namespace

std::string make_history_details(const replay::MatchStats& stats, const HistoryRecord& record) {
    if (!stats.ok) return std::string();
    std::string text;
    for (int shed = 0; shed <= 5; ++shed) {
        text = build(stats, record, shed);
        if (text.size() <= kDetailsMaxBytes) return text;
    }
    return history_details_marker();                                  // (a match so long, with numbers so large, that its minutes of orders alone do not fit: told as not counted, the cap is never passed)
}

std::string history_details_marker() { return "{\"v\":1,\"n\":1}"; }

}  // namespace ants::server
