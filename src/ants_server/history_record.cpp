#include "ants_server/history_record.hpp"

#include <algorithm>

#include "ants_server/replay_store.hpp"
#include "ants_sim/game_mode.hpp"
#include "ants_sim/start_teams.hpp"

namespace ants::server {

using ctl::JsonValue;

namespace {

// "Bot (Medium)" gives true and "Medium"; "Bot" gives true and "Bot"; anything else is a person's name
bool bot_level_of(const std::string& name, std::string& level) {
    const size_t first = name.find_first_not_of(' ');
    if (first == std::string::npos) return false;
    const size_t last = name.find_last_not_of(' ');
    const std::string text = name.substr(first, last - first + 1);
    if (text == "Bot") {
        level = "Bot";
        return true;
    }
    if (text.size() > 6 && text.compare(0, 5, "Bot (") == 0 && text.back() == ')') {
        level = text.substr(5, text.size() - 6);
        return true;
    }
    return false;
}

const char* const kColours[sim::MAX_PLAYERS] = {"Green", "Red", "Blue", "Black"};

bool row_has(const HistoryRow& r, int seat) {
    if (seat < 0 || seat >= sim::MAX_PLAYERS) return false;
    return std::find(r.seats.begin(), r.seats.end(), static_cast<uint8_t>(seat)) != r.seats.end();
}

JsonValue seat_json(const HistorySeat& s, bool full) {
    JsonValue o = JsonValue::make_object();
    o.set("seat", JsonValue::make_int(s.seat));
    o.set("colour", JsonValue::make_string(kColours[s.seat < sim::MAX_PLAYERS ? s.seat : 0]));
    o.set("name", JsonValue::make_string(s.name));
    o.set("bot", JsonValue::make_string(s.bot));
    o.set("score", JsonValue::make_int(s.score));
    o.set("killed", JsonValue::make_int(s.killed));
    o.set("lost", JsonValue::make_int(s.lost));
    o.set("hatched", JsonValue::make_int(s.hatched));
    if (full) {
        o.set("bombs_planted", JsonValue::make_int(s.bombs_planted));
        o.set("bombs_defused", JsonValue::make_int(s.bombs_defused));
        o.set("fires_lit", JsonValue::make_int(s.fires_lit));
    }
    o.set("left", JsonValue::make_bool(s.left));
    o.set("winner", JsonValue::make_bool(s.winner));
    return o;
}

JsonValue row_json(const HistoryRow& r) {
    JsonValue o = JsonValue::make_object();
    JsonValue seats = JsonValue::make_array();
    for (const uint8_t s : r.seats) seats.push_back(JsonValue::make_int(s));
    o.set("seats", std::move(seats));
    o.set("score", JsonValue::make_int(r.score));
    o.set("killed", JsonValue::make_int(r.killed));
    o.set("lost", JsonValue::make_int(r.lost));
    o.set("hatched", JsonValue::make_int(r.hatched));
    o.set("winner", JsonValue::make_bool(r.winner));
    return o;
}

// 0 the row of the side left standing (mode 187), 1 any other, 2 the quitter's
int rank_class(const HistoryRow& r, int quitter, uint8_t standing) {
    if (row_has(r, quitter)) return 2;
    if (standing < sim::MAX_PLAYERS && row_has(r, standing)) return 0;
    return 1;
}

}  // namespace

bool valid_history_id(const std::string& id) { return ReplayStore::valid_file_name(history_recording_file(id)); }

std::string history_recording_file(const std::string& id) { return id + ReplayStore::kExtension; }

bool rank_history_rows(const std::string& mode, int quitter, uint8_t standing, std::vector<HistoryRow>& rows) {
    const bool kills_only = mode == kHistoryMode187;
    for (HistoryRow& r : rows) r.winner = false;
    // the numbers that the rule ranks by: highest-score the score and then the kills, 187 the kills alone
    const auto primary = [kills_only](const HistoryRow& r) { return kills_only ? r.killed : r.score; };
    const auto secondary = [kills_only](const HistoryRow& r) { return kills_only ? int32_t{0} : r.killed; };
    std::stable_sort(rows.begin(), rows.end(), [&](const HistoryRow& a, const HistoryRow& b) {
        const int ca = rank_class(a, quitter, standing);
        const int cb = rank_class(b, quitter, standing);
        if (ca != cb) return ca < cb;
        if (primary(a) != primary(b)) return primary(a) > primary(b);
        return secondary(a) > secondary(b);
    });
    if (rows.empty()) return false;
    const bool draw = rows.size() >= 2 && rank_class(rows[0], quitter, standing) == 1 && rank_class(rows[1], quitter, standing) == 1 && primary(rows[0]) == primary(rows[1]) &&
                      secondary(rows[0]) == secondary(rows[1]);
    if (!draw && rank_class(rows[0], quitter, standing) != 2) rows[0].winner = true;
    return draw;
}

HistoryRecord make_history_record(const std::string& id, int64_t ended_s, const replay::Replay& replay, const replay::Outcome& outcome) {
    HistoryRecord rec;
    rec.id = id;
    rec.ended_s = ended_s;
    rec.map = replay.head.map_name;
    rec.game = replay.head.game_version;
    rec.turns = outcome.turns;
    rec.finished = outcome.match_over;
    rec.quitter = outcome.result.quitter < sim::MAX_PLAYERS ? static_cast<int>(outcome.result.quitter) : -1;
    rec.mode = replay.head.mode == static_cast<uint8_t>(sim::GameMode::Kills187) ? kHistoryMode187 : kHistoryModeScore;
    const bool kills_only = rec.mode == kHistoryMode187;

    size_t people = 0;
    size_t computers = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (((replay.head.roster >> seat) & 1u) == 0) continue;
        const sim::PlayerMatchStats& st = outcome.result.stats[seat];
        HistorySeat s;
        s.seat = seat;
        std::string level;
        if (bot_level_of(replay.head.names[seat], level)) {
            s.bot = level;
            ++computers;
        } else {
            s.name = replay.head.names[seat];
            ++people;
        }
        s.killed = st.enemy_killed;
        s.score = kills_only ? static_cast<int32_t>(st.enemy_killed) : st.score;       // (187 has no score: the engine's own is the kills, kept as the ranking value)
        s.lost = st.friendly_lost;
        s.hatched = st.new_hatched;
        s.bombs_planted = st.bombs_planted;
        s.bombs_defused = st.bombs_defused;
        s.fires_lit = st.fires_lit;
        s.left = ((outcome.dropped_mask >> seat) & 1u) != 0 || rec.quitter == static_cast<int>(seat);
        rec.seats.push_back(std::move(s));
    }
    rec.computers_only = people == 0 && computers > 0;

    // how the match began: one pair of seats as a team and the two others as the other team, or free for all (what the room's start data said; the engine ends a match that is all one team)
    const size_t n = rec.seats.size();
    const sim::StartTeamsPlan plan = sim::plan_start_teams(replay.head.teams, replay.head.roster);
    if (n <= 1) rec.format = "solo";
    else if (n == 2) rec.format = "1v1";
    else if (plan.pairs.empty()) rec.format = n == 3 ? "ffa3" : "ffa4";
    else rec.format = plan.pairs.size() >= 2 ? "2v2" : "2v1";

    // the rows: the engine's grouping of seats into sides (an alliance is one row), with the numbers added up, ranked by the owner's rule instead of the engine's order (see the header)
    for (const sim::ResultRow& r : outcome.result.rows(sim::PLAYER_NEUTRAL)) {
        HistoryRow row;
        row.seats.push_back(r.first);
        if (r.has_second()) row.seats.push_back(r.second);
        row.killed = r.enemy_killed;
        row.score = kills_only ? r.enemy_killed : r.score;
        row.lost = r.friendly_lost;
        row.hatched = r.new_hatched;
        rec.rows.push_back(std::move(row));
    }
    rec.draw = rank_history_rows(rec.mode, rec.quitter, outcome.result.standing, rec.rows);
    if (n < 2) {                                                                           // (a match of one team has nobody to beat)
        rec.draw = false;
        for (HistoryRow& row : rec.rows) row.winner = false;
    }
    for (HistorySeat& s : rec.seats) {
        for (const HistoryRow& row : rec.rows) s.winner = s.winner || (row.winner && row_has(row, s.seat));
    }
    return rec;
}

std::string history_rows_to_json(const std::vector<HistoryRow>& rows) {
    JsonValue list = JsonValue::make_array();
    for (const HistoryRow& row : rows) list.push_back(row_json(row));
    return ctl::to_json(list);
}

bool history_rows_from_json(const std::string& text, std::vector<HistoryRow>& rows) {
    rows.clear();
    JsonValue list;
    ctl::JsonLimits limits;
    limits.max_bytes = 64u * 1024u;
    limits.max_elements = 256;
    limits.max_depth = 4;
    if (!ctl::parse_json(text, list, nullptr, limits) || !list.is_array()) return false;
    for (const JsonValue& item : list.items()) {
        HistoryRow row;
        for (const JsonValue& s : item.get("seats").items()) {
            const int64_t seat = s.as_int_or(-1);
            if (seat >= 0 && seat < sim::MAX_PLAYERS && row.seats.size() < 2) row.seats.push_back(static_cast<uint8_t>(seat));
        }
        if (row.seats.empty()) return false;
        row.score = static_cast<int32_t>(item.get("score").as_int_or(0));
        row.killed = static_cast<int32_t>(item.get("killed").as_int_or(0));
        row.lost = static_cast<int32_t>(item.get("lost").as_int_or(0));
        row.hatched = static_cast<int32_t>(item.get("hatched").as_int_or(0));
        row.winner = item.get("winner").as_bool_or(false);
        rows.push_back(std::move(row));
    }
    return true;
}

JsonValue history_record_to_json(const HistoryRecord& r, bool full) {
    JsonValue o = JsonValue::make_object();
    if (full) o.set("v", JsonValue::make_int(kHistoryVersion));
    o.set("id", JsonValue::make_string(r.id));
    o.set("ended", JsonValue::make_int(r.ended_s));
    o.set("map", JsonValue::make_string(r.map));
    o.set("game", JsonValue::make_string(r.game));
    o.set("turns", JsonValue::make_int(r.turns));
    o.set("seconds", JsonValue::make_int(r.seconds()));
    o.set("finished", JsonValue::make_bool(r.finished));
    o.set("quitter", r.quitter >= 0 ? JsonValue::make_int(r.quitter) : JsonValue::make_null());
    o.set("mode", JsonValue::make_string(r.mode));
    o.set("format", JsonValue::make_string(r.format));
    o.set("draw", JsonValue::make_bool(r.draw));
    o.set("result", JsonValue::make_string(r.result()));
    o.set("computers_only", JsonValue::make_bool(r.computers_only));
    JsonValue seats = JsonValue::make_array();
    for (const HistorySeat& s : r.seats) seats.push_back(seat_json(s, full));
    o.set("seats", std::move(seats));
    JsonValue rows = JsonValue::make_array();
    for (const HistoryRow& row : r.rows) rows.push_back(row_json(row));
    o.set("rows", std::move(rows));
    if (full && !r.details.empty()) {
        JsonValue details;
        ctl::JsonLimits limits;
        limits.max_bytes = 512u * 1024u;
        limits.max_elements = 16384;
        limits.max_depth = 16;
        if (ctl::parse_json(r.details, details, nullptr, limits)) o.set("details", std::move(details));
    }
    return o;
}

}  // namespace ants::server
