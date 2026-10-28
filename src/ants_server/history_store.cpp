#include "ants_server/history_store.hpp"

#include "ants_ai/bot_names.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <chrono>
#include <exception>
#include <unordered_map>
#include <unordered_set>

#include "ants_net/clock.hpp"
#include "ants_replay/match_stats.hpp"
#include "ants_replay/player.hpp"
#include "ants_server/history_deaths.hpp"
#include "ants_server/history_details.hpp"
#include "ants_server/online_limits.hpp"
#include "ants_sim/start_teams.hpp"

namespace ants::server {

namespace {

constexpr int kSqliteConstraint = 19;              // SQLITE_CONSTRAINT: the low byte of a refusal by a CHECK, a key or a NOT NULL (the extended codes keep it)

const char* const kColors[sim::MAX_PLAYERS] = {"Green", "Red", "Blue", "Black"};

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// ---- statements ---------------------------------------------------------------------------------------------------------------------
// A bound value. The SQL text of every statement below is made of fixed pieces; whatever comes from a request or a recording is bound.
struct Arg {
    enum class Kind : uint8_t { Int, Text, Null };
    Kind kind{Kind::Null};
    int64_t i{0};
    std::string s;
};
Arg int_arg(int64_t v) { return Arg{Arg::Kind::Int, v, std::string()}; }
Arg text_arg(std::string v) { return Arg{Arg::Kind::Text, 0, std::move(v)}; }
Arg null_arg() { return Arg{}; }

db::Error bind_args(db::Statement& st, const std::vector<Arg>& args) {
    for (size_t k = 0; k < args.size(); ++k) {
        const int at = static_cast<int>(k) + 1;
        db::Error e;
        switch (args[k].kind) {
            case Arg::Kind::Int: e = st.bind_int64(at, args[k].i); break;
            case Arg::Kind::Text: e = st.bind_text(at, args[k].s); break;
            case Arg::Kind::Null: e = st.bind_null(at); break;
        }
        if (!e.ok()) return e;
    }
    return db::Error();
}

// A statement that returns no row (or whose rows are not wanted)
db::Error run(db::Database& d, const char* sql, const std::vector<Arg>& args) {
    db::Statement st;
    db::Error e = d.prepare(st, sql);
    if (!e.ok()) return e;
    e = bind_args(st, args);
    if (!e.ok()) return e;
    for (;;) {
        const db::Statement::Step step = st.step();
        if (step == db::Statement::Step::Failed) return st.error();
        if (step == db::Statement::Step::Done) return db::Error();
    }
}

// One number from a statement that returns one row of one column (0 when none)
db::Error scalar(db::Database& d, const char* sql, const std::vector<Arg>& args, int64_t& out) {
    out = 0;
    db::Statement st;
    db::Error e = d.prepare(st, sql);
    if (!e.ok()) return e;
    e = bind_args(st, args);
    if (!e.ok()) return e;
    const db::Statement::Step step = st.step();
    if (step == db::Statement::Step::Failed) return st.error();
    if (step == db::Statement::Step::Row) out = st.column_int64(0);
    return db::Error();
}

// ---- the columns of a match ---------------------------------------------------------------------------------------------------------
const char* const kMatchColumns = "m.id, m.ended, m.map, m.game, m.turns, m.finished, m.quitter, m.mode, m.format, m.result, m.computers_only, m.rows_json, m.details";
const char* const kSeatColumns = "seat, name, bot, score, killed, lost, hatched, bombs_planted, bombs_defused, fires_lit, left_early, winner";

uint32_t to_u32(int64_t v) { return v < 0 ? 0u : v > 0xFFFFFFFFll ? 0xFFFFFFFFu : static_cast<uint32_t>(v); }
int32_t to_i32(int64_t v) { return v < INT32_MIN ? INT32_MIN : v > INT32_MAX ? INT32_MAX : static_cast<int32_t>(v); }

// The row that kMatchColumns selects, without its seats
bool read_match(db::Statement& st, HistoryRecord& r) {
    r = HistoryRecord();
    r.id = st.column_text(0);
    r.ended_s = st.column_int64(1);
    r.map = st.column_text(2);
    r.game = st.column_text(3);
    r.turns = to_u32(st.column_int64(4));
    r.finished = st.column_int64(5) != 0;
    r.quitter = st.column_is_null(6) ? -1 : static_cast<int>(st.column_int64(6));
    r.mode = st.column_text(7);
    r.format = st.column_text(8);
    const std::string result = st.column_text(9);
    r.draw = result == "draw";
    r.computers_only = st.column_int64(10) != 0;
    if (!history_rows_from_json(st.column_text(11), r.rows)) r.rows.clear();
    if (!st.column_is_null(12)) r.details = st.column_text(12);
    return true;
}

// The seats of many records with ONE statement (`WHERE match_id IN (?, ?, ...)`: the question marks are all the text that depends on the number of records, at most a page)
db::Error load_seats_many(db::Database& d, std::vector<HistoryRecord>& recs) {
    if (recs.empty()) return db::Error();
    std::string marks = "?";
    for (size_t k = 1; k < recs.size(); ++k) marks += ", ?";
    db::Statement st;
    db::Error e = d.prepare(st, (std::string("SELECT match_id, ") + kSeatColumns + " FROM history_seats WHERE match_id IN (" + marks + ") ORDER BY match_id, seat").c_str());
    if (!e.ok()) return e;
    std::unordered_map<std::string, size_t> at;
    for (size_t k = 0; k < recs.size(); ++k) {
        e = st.bind_text(static_cast<int>(k) + 1, recs[k].id);
        if (!e.ok()) return e;
        at[recs[k].id] = k;
        recs[k].seats.clear();
    }
    for (;;) {
        const db::Statement::Step step = st.step();
        if (step == db::Statement::Step::Failed) return st.error();
        if (step == db::Statement::Step::Done) return db::Error();
        const auto it = at.find(st.column_text(0));
        if (it == at.end()) continue;
        HistorySeat s;
        s.seat = static_cast<uint8_t>(st.column_int64(1));
        s.name = st.column_text(2);
        s.bot = st.column_text(3);
        s.score = to_i32(st.column_int64(4));
        s.killed = to_u32(st.column_int64(5));
        s.lost = to_u32(st.column_int64(6));
        s.hatched = to_u32(st.column_int64(7));
        s.bombs_planted = to_u32(st.column_int64(8));
        s.bombs_defused = to_u32(st.column_int64(9));
        s.fires_lit = to_u32(st.column_int64(10));
        s.left = st.column_int64(11) != 0;
        s.winner = st.column_int64(12) != 0;
        recs[it->second].seats.push_back(std::move(s));
    }
}

// The records `ids` (a page: at most 100), in that order, with their seats, in two statements whatever the number; an id that is not there is left out
db::Error load_many(db::Database& d, const std::vector<std::string>& ids, std::vector<HistoryRecord>& out) {
    out.clear();
    if (ids.empty()) return db::Error();
    std::string marks = "?";
    for (size_t k = 1; k < ids.size(); ++k) marks += ", ?";
    db::Statement st;
    db::Error e = d.prepare(st, (std::string("SELECT ") + kMatchColumns + " FROM history_matches m WHERE m.id IN (" + marks + ")").c_str());
    if (!e.ok()) return e;
    for (size_t k = 0; k < ids.size(); ++k) {
        e = st.bind_text(static_cast<int>(k) + 1, ids[k]);
        if (!e.ok()) return e;
    }
    std::unordered_map<std::string, HistoryRecord> got;
    for (;;) {
        const db::Statement::Step step = st.step();
        if (step == db::Statement::Step::Failed) return st.error();
        if (step == db::Statement::Step::Done) break;
        HistoryRecord r;
        read_match(st, r);
        const std::string id = r.id;
        got[id] = std::move(r);
    }
    for (const std::string& id : ids) {
        const auto it = got.find(id);
        if (it != got.end()) out.push_back(std::move(it->second));
    }
    return load_seats_many(d, out);
}

// One whole record by its id; `found` says whether there is one
db::Error load_one(db::Database& d, const std::string& id, HistoryRecord& out, bool& found) {
    std::vector<HistoryRecord> one;
    const db::Error e = load_many(d, {id}, one);
    found = e.ok() && !one.empty();
    if (found) out = std::move(one[0]);
    return e;
}

// What a seat shows on the pages: "Pip (bot, Hard)" ("Bot (Level)" for a match stored before the names), the name that was typed, or the color
std::string shown_name(const HistorySeat& s) {
    if (!s.bot.empty()) {
        if (s.name.empty()) return "Bot (" + s.bot + ")";                                    // (a match stored before the names, or a plain "Bot")
        std::string tag = s.bot;
        for (char& c : tag) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        const std::string label = ai::bot_label(s.name, tag);
        return label.empty() ? "Bot (" + s.bot + ")" : label;                               // ("Pip (bot, Hard)")
    }
    if (!s.name.empty()) return s.name;
    return kColors[s.seat < sim::MAX_PLAYERS ? s.seat : 0];
}

// The words that the list's q looks in: the map's name without its extension, then what each seat shows, one per line, lower case
std::string search_text(const HistoryRecord& r) {
    std::string text = lower(ReplayStore::map_stem(r.map));
    for (const HistorySeat& s : r.seats) text += "\n" + lower(shown_name(s));
    if (text.size() > HistoryStore::kMaxSearchBytes) text.resize(HistoryStore::kMaxSearchBytes);
    return text;
}

// A LIKE pattern that matches `needle` anywhere, whatever characters it holds (% _ and the escape character itself are literal)
std::string like_contains(const std::string& needle_lower) {
    std::string out = "%";
    for (const char c : needle_lower) {
        if (c == '%' || c == '_' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('%');
    return out;
}

int64_t best_of(const HistoryRecord& r, int64_t (*get)(const HistorySeat&)) {
    int64_t best = 0;
    for (const HistorySeat& s : r.seats) best = std::max(best, get(s));
    return best;
}

}  // namespace

const char* recording_name(Recording r) noexcept {
    switch (r) {
        case Recording::Watch: return "watch";
        case Recording::Old: return "old";
        case Recording::Removed: return "removed";
    }
    return "removed";
}

bool entry_plays_here(const ReplayEntry& entry) noexcept {
    const uint16_t wanted = replay::sim_rules_for_mode(entry.mode);                      // (0 for a mode that this build does not know: nothing equals it)
    return entry.readable && wanted != 0 && entry.sim_rules == wanted;
}

std::string map_hash_text(uint64_t hash) {
    static const char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i, hash >>= 4) out[static_cast<size_t>(i)] = digits[hash & 0xF];
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------------------------------------------------------------

HistoryStore::HistoryStore(db::Database& database, Config config) : db_(database), cfg_(std::move(config)) {}

int64_t HistoryStore::now_s() const { return cfg_.clock_s ? cfg_.clock_s() : static_cast<int64_t>(std::time(nullptr)); }

void HistoryStore::note(const std::string& line) const {
    if (notes_.size() < kMaxNoteLines) notes_.push_back(line);
}

std::vector<std::string> HistoryStore::take_notes() {
    std::vector<std::string> out;
    out.swap(notes_);
    return out;
}

void HistoryStore::fail(const std::string& what, const db::Error& error) const {
    reason_ = what + ": " + error.message;
    retry_armed_ = false;
    if (!up_) return;                                      // (one line for as long as it stays down)
    up_ = false;
    note("history: " + reason_ + "; the match history is unavailable until the database answers again (tried again every " + std::to_string(kRetryMs / 1000) + " s)");
}

db::Error HistoryStore::refresh_counts() const {
    int64_t matches = 0;
    int64_t markers = 0;
    db::Error e = scalar(db_, "SELECT count(*) FROM history_matches", {}, matches);
    if (e.ok()) e = scalar(db_, "SELECT count(*) FROM history_markers", {}, markers);
    if (e.ok()) e = run(db_, "SELECT 1 FROM history_seats LIMIT 1", {});               // (the third table: a look at the whole schema that the list needs)
    if (e.ok()) e = run(db_, "SELECT 1 FROM map_plays LIMIT 1", {});
    if (!e.ok()) return e;
    count_ = static_cast<size_t>(matches);
    markers_ = static_cast<size_t>(markers);
    return db::Error();
}

bool HistoryStore::prepare(std::string& why) {
    up_ = false;
    if (!db_.is_open()) {
        why = "the database is not open";
        return false;
    }
    db::Error e = refresh_counts();
    if (!e.ok()) {
        why = "the match history cannot be read from the database: " + e.message;
        return false;
    }
    up_ = true;
    reason_.clear();
    note("history: " + std::to_string(count_) + " match(es) kept for good in the database" + (markers_ > 0 ? ", " + std::to_string(markers_) + " taken out by the owner" : std::string()));
    return true;
}

void HistoryStore::maintain(uint32_t now_ms) {
    if (up_) return;
    if (!retry_armed_) {
        retry_armed_ = true;
        retry_at_ms_ = now_ms + kRetryMs;
        return;
    }
    if (!net::time_reached(now_ms, retry_at_ms_)) return;
    retry_at_ms_ = now_ms + kRetryMs;
    if (!db_.is_open() || !refresh_counts().ok()) return;       // (still down: no new line)
    up_ = true;
    reason_.clear();
    retry_armed_ = false;
    note("history: the match history is available again (the database answers); " + std::to_string(count_) + " match(es) kept");
}

bool HistoryStore::put(const HistoryRecord& record, std::string& why) {
    why.clear();
    if (!up_) {
        why = "the match history is unavailable";
        return false;
    }
    if (!valid_history_id(record.id)) {
        why = "the id is not the name of a match";
        return false;
    }
    if (record.seats.empty() || record.seats.size() > sim::MAX_PLAYERS) {
        why = "a record needs one to four seats";
        return false;
    }
    const int64_t best_score = best_of(record, [](const HistorySeat& s) { return int64_t{s.score}; });
    const int64_t best_kills = best_of(record, [](const HistorySeat& s) { return int64_t{s.killed}; });
    const int64_t best_hatched = best_of(record, [](const HistorySeat& s) { return int64_t{s.hatched}; });
    db::Transaction tx(db_);
    if (!tx.error().ok()) {
        why = tx.error().message;
        fail("a record could not be started", tx.error());
        return false;
    }
    const auto refused = [&](const char* what, const db::Error& e) {
        why = e.message;
        if ((e.code & 0xFF) == kSqliteConstraint) note("history: " + record.id + " is refused by the database: " + e.message);       // (this record only: the database itself is fine)
        else fail(what, e);
        return false;
    };
    // A match is one play of its map the first time it is kept: a record of an id that the history or the owner's marker already knows was counted then (or was taken out on purpose)
    int64_t known = 0;
    db::Error e = scalar(db_, "SELECT (SELECT count(*) FROM history_matches WHERE id = ?1) + (SELECT count(*) FROM history_markers WHERE id = ?1)", {text_arg(record.id)}, known);
    if (e.ok()) e = run(db_, "DELETE FROM history_seats WHERE match_id = ?1", {text_arg(record.id)});
    if (e.ok()) e = run(db_, "DELETE FROM history_deaths WHERE id = ?1", {text_arg(record.id)});
    if (e.ok()) e = run(db_, "DELETE FROM history_matches WHERE id = ?1", {text_arg(record.id)});
    if (e.ok()) e = run(db_, "DELETE FROM history_markers WHERE id = ?1", {text_arg(record.id)});
    if (!e.ok()) return refused("a record could not be replaced", e);
    e = run(db_,
            "INSERT INTO history_matches (id, ended, map, game, turns, finished, quitter, mode, format, result, computers_only, best_score, best_kills, best_hatched, search, rows_json, details, counted_at)"
            " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18)",
            {text_arg(record.id), int_arg(record.ended_s), text_arg(record.map), text_arg(record.game), int_arg(record.turns), int_arg(record.finished ? 1 : 0),
             record.quitter >= 0 ? int_arg(record.quitter) : null_arg(), text_arg(record.mode), text_arg(record.format), text_arg(record.result()), int_arg(record.computers_only ? 1 : 0),
             int_arg(best_score), int_arg(best_kills), int_arg(best_hatched), text_arg(search_text(record)), text_arg(history_rows_to_json(record.rows)),
             record.details.empty() ? null_arg() : text_arg(record.details), int_arg(now_s())});
    if (!e.ok()) return refused("a record could not be kept", e);
    for (const HistorySeat& s : record.seats) {
        e = run(db_,
                "INSERT INTO history_seats (match_id, seat, name, bot, score, killed, lost, hatched, bombs_planted, bombs_defused, fires_lit, left_early, winner)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)",
                {text_arg(record.id), int_arg(s.seat), text_arg(s.name), text_arg(s.bot), int_arg(s.score), int_arg(s.killed), int_arg(s.lost), int_arg(s.hatched), int_arg(s.bombs_planted),
                 int_arg(s.bombs_defused), int_arg(s.fires_lit), int_arg(s.left ? 1 : 0), int_arg(s.winner ? 1 : 0)});
        if (!e.ok()) return refused("a seat could not be kept", e);
    }
    DeathsView deaths;
    if (!record.deaths.empty() && record.deaths.size() <= kDeathsMaxBytes && parse_history_deaths(record.deaths, deaths)) {      // (a text that is not a map of deaths, or is over its cap, is not kept: the match is)
        e = run(db_,
                "INSERT INTO history_deaths (id, map, hash, ended, w, h, n, deaths) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
                {text_arg(record.id), text_arg(record.map), text_arg(map_hash_text(record.map_hash)), int_arg(record.ended_s), int_arg(deaths.width), int_arg(deaths.height), int_arg(deaths.total),
                 text_arg(record.deaths)});
        if (!e.ok()) return refused("a map of deaths could not be kept", e);
    }
    if (known == 0 && record.counts_as_map_play()) {
        e = run(db_,
                "INSERT INTO map_plays (map, hash, plays, last_played) VALUES (?1, ?2, 1, ?3)"
                " ON CONFLICT (map, hash) DO UPDATE SET plays = plays + 1, last_played = max(last_played, excluded.last_played)",
                {text_arg(record.map), text_arg(map_hash_text(record.map_hash)), int_arg(record.ended_s)});
        if (!e.ok()) return refused("a map's play could not be counted", e);
    }
    e = tx.commit();
    if (!e.ok()) return refused("a record could not be committed", e);
    ++deaths_revision_;
    e = refresh_counts();
    if (!e.ok()) fail("the records could not be counted", e);                              // (the record is kept; the list will say that the history is down)
    return true;
}

HistoryStore::Removal HistoryStore::remove(const std::string& id, bool keep_marker, std::string& why) {
    why.clear();
    if (!up_ || !valid_history_id(id)) return Removal::Missing;
    db::Transaction tx(db_);
    if (!tx.error().ok()) {
        why = tx.error().message;
        fail("a record could not be taken out", tx.error());
        return Removal::Failed;
    }
    int64_t present = 0;
    db::Error e = scalar(db_, "SELECT count(*) FROM history_matches WHERE id = ?1", {text_arg(id)}, present);
    if (e.ok() && present == 0) return Removal::Missing;                                   // (the transaction rolls back: nothing was written)
    if (e.ok()) e = run(db_, "DELETE FROM history_seats WHERE match_id = ?1", {text_arg(id)});
    if (e.ok()) e = run(db_, "DELETE FROM history_deaths WHERE id = ?1", {text_arg(id)});
    if (e.ok()) e = run(db_, "DELETE FROM history_matches WHERE id = ?1", {text_arg(id)});
    if (e.ok() && keep_marker) e = run(db_, "INSERT OR REPLACE INTO history_markers (id, deleted_at) VALUES (?1, ?2)", {text_arg(id), int_arg(now_s())});
    if (e.ok()) e = tx.commit();
    if (!e.ok()) {
        why = e.message;
        fail("a record could not be taken out", e);
        return Removal::Failed;
    }
    ++deaths_revision_;
    e = refresh_counts();
    if (!e.ok()) fail("the records could not be counted", e);
    return Removal::Done;
}

size_t HistoryStore::prune_markers(const std::function<bool(const std::string& id)>& still_needed) {
    if (!up_) return 0;
    std::vector<std::string> gone;
    {
        db::Statement st;
        db::Error e = db_.prepare(st, "SELECT id FROM history_markers ORDER BY id");
        if (!e.ok()) {
            fail("the markers could not be read", e);
            return 0;
        }
        for (;;) {
            const db::Statement::Step step = st.step();
            if (step == db::Statement::Step::Failed) {
                fail("the markers could not be read", st.error());
                return 0;
            }
            if (step == db::Statement::Step::Done) break;
            const std::string id = st.column_text(0);
            if (!still_needed(id)) gone.push_back(id);
        }
    }
    if (gone.empty()) return 0;
    db::Transaction tx(db_);
    db::Error e = tx.error();
    for (size_t k = 0; e.ok() && k < gone.size(); ++k) e = run(db_, "DELETE FROM history_markers WHERE id = ?1", {text_arg(gone[k])});
    if (e.ok()) e = tx.commit();
    if (!e.ok()) {
        fail("markers could not be deleted", e);
        return 0;
    }
    e = refresh_counts();
    if (!e.ok()) fail("the records could not be counted", e);
    return gone.size();
}

bool HistoryStore::map_plays(std::vector<MapPlays>& out) const {
    out.clear();
    if (!up_) return false;
    db::Statement st;
    db::Error e = db_.prepare(st, "SELECT map, hash, plays, last_played FROM map_plays ORDER BY map, hash");
    if (!e.ok()) {
        fail("the play counts could not be read", e);
        return false;
    }
    for (;;) {
        const db::Statement::Step step = st.step();
        if (step == db::Statement::Step::Failed) {
            fail("the play counts could not be read", st.error());
            out.clear();
            return false;
        }
        if (step == db::Statement::Step::Done) break;
        MapPlays row;
        row.map = st.column_text(0);
        row.hash = st.column_text(1);
        row.plays = st.column_int64(2);
        row.last_played = st.column_int64(3);
        out.push_back(std::move(row));
    }
    return true;
}

HistoryStore::DeathsSum HistoryStore::map_deaths(const std::string& name, std::string& json) const {
    json.clear();
    if (!up_) return DeathsSum::Unavailable;
    if (deaths_cached_at_ != deaths_revision_) {                                          // (the history changed since the last look: the names and the sums are read again)
        deaths_cache_.clear();
        deaths_names_.clear();
        db::Statement st;
        db::Error e = db_.prepare(st, "SELECT DISTINCT map FROM history_deaths ORDER BY map");
        while (e.ok()) {
            const db::Statement::Step step = st.step();
            if (step == db::Statement::Step::Failed) e = st.error();
            if (step != db::Statement::Step::Row) break;
            deaths_names_.push_back(st.column_text(0));
        }
        if (!e.ok()) {
            deaths_names_.clear();
            fail("the maps of deaths could not be looked up", e);
            return DeathsSum::Unavailable;
        }
        deaths_cached_at_ = deaths_revision_;
    }
    // the name must be one of the names that have a map of deaths here (the stored spelling is the one that is used; never the visitor's text)
    std::string known;
    const std::string want = lower(name);
    for (const std::string& have : deaths_names_) {
        if (lower(have) == want) {
            known = have;                                                                 // (two spellings that differ in case only: the first of the sorted names, always the same one)
            break;
        }
    }
    if (known.empty()) return DeathsSum::Unknown;
    const auto cached = deaths_cache_.find(known);
    if (cached != deaths_cache_.end()) {
        json = cached->second;
        return DeathsSum::Ok;
    }
    std::string hash;
    int64_t w = 0, h = 0;
    std::vector<std::string> fields;
    db::Statement st;
    db::Error e = db_.prepare(st, "SELECT hash, w, h FROM history_deaths WHERE map = ?1 ORDER BY ended DESC, id DESC LIMIT 1");
    if (e.ok()) e = st.bind_text(1, known);
    if (e.ok()) {
        const db::Statement::Step step = st.step();
        if (step == db::Statement::Step::Failed) e = st.error();
        else if (step == db::Statement::Step::Row) {
            hash = st.column_text(0);
            w = st.column_int64(1);
            h = st.column_int64(2);
        }
    }
    if (e.ok()) {
        db::Statement rows;
        e = db_.prepare(rows, "SELECT deaths FROM history_deaths WHERE map = ?1 AND hash = ?2 AND w = ?3 AND h = ?4 ORDER BY ended DESC, id DESC LIMIT ?5");
        if (e.ok()) e = rows.bind_text(1, known);
        if (e.ok()) e = rows.bind_text(2, hash);
        if (e.ok()) e = rows.bind_int64(3, w);
        if (e.ok()) e = rows.bind_int64(4, h);
        if (e.ok()) e = rows.bind_int64(5, static_cast<int64_t>(kDeathsSumMatches));
        while (e.ok()) {
            const db::Statement::Step step = rows.step();
            if (step == db::Statement::Step::Failed) e = rows.error();
            if (step != db::Statement::Step::Row) break;
            fields.push_back(rows.column_text(0));
        }
    }
    if (!e.ok()) {
        fail("the maps of deaths could not be read", e);
        return DeathsSum::Unavailable;
    }
    json = add_up_deaths(known, static_cast<uint32_t>(w), static_cast<uint32_t>(h), fields);
    if (json.empty()) return DeathsSum::Unknown;
    if (deaths_cache_.size() >= kDeathsSumCache) deaths_cache_.clear();
    deaths_cache_[known] = json;
    return DeathsSum::Ok;
}

bool HistoryStore::find(const std::string& id, HistoryRecord& out) const {
    if (!up_ || !valid_history_id(id)) return false;
    bool found = false;
    db::Error e = load_one(db_, id, out, found);
    if (e.ok() && found) {                                                                 // (the map of deaths has a table of its own: one more statement, for one record only)
        db::Statement st;
        e = db_.prepare(st, "SELECT deaths FROM history_deaths WHERE id = ?1");
        if (e.ok()) e = st.bind_text(1, id);
        if (e.ok()) {
            const db::Statement::Step step = st.step();
            if (step == db::Statement::Step::Failed) e = st.error();
            else if (step == db::Statement::Step::Row) out.deaths = st.column_text(0);
        }
    }
    if (!e.ok()) {
        fail("a record could not be read", e);
        return false;
    }
    return found;
}

bool HistoryStore::knows(const std::string& id) const {
    if (!up_) return true;
    int64_t n = 0;
    const db::Error e = scalar(db_, "SELECT (SELECT count(*) FROM history_matches WHERE id = ?1) + (SELECT count(*) FROM history_markers WHERE id = ?1)", {text_arg(id)}, n);
    if (!e.ok()) {
        fail("a record could not be looked up", e);
        return true;
    }
    return n > 0;
}

bool HistoryStore::known_ids(std::unordered_set<std::string>& out) const {
    out.clear();
    if (!up_) return false;
    db::Statement st;
    db::Error e = db_.prepare(st, "SELECT id FROM history_matches UNION ALL SELECT id FROM history_markers");
    while (e.ok()) {
        const db::Statement::Step step = st.step();
        if (step == db::Statement::Step::Failed) e = st.error();
        if (step != db::Statement::Step::Row) break;
        out.insert(st.column_text(0));
    }
    if (!e.ok()) {
        fail("the records could not be looked up", e);
        out.clear();
        return false;
    }
    return true;
}

bool HistoryStore::keep_unknown(std::vector<std::string>& ids) const {
    std::unordered_set<std::string> known;
    if (!known_ids(known)) {
        ids.clear();
        return false;
    }
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&](const std::string& id) { return known.count(id) != 0; }), ids.end());
    return true;
}

bool HistoryStore::ids_without_details(std::vector<std::string>& out) const {
    out.clear();
    if (!up_) return false;
    db::Statement st;
    db::Error e = db_.prepare(st, "SELECT id FROM history_matches WHERE details IS NULL ORDER BY ended DESC, id DESC");
    while (e.ok()) {
        const db::Statement::Step step = st.step();
        if (step == db::Statement::Step::Failed) e = st.error();
        if (step != db::Statement::Step::Row) break;
        out.push_back(st.column_text(0));
    }
    if (!e.ok()) {
        fail("the records without details could not be looked up", e);
        out.clear();
        return false;
    }
    return true;
}

bool HistoryStore::set_details(const std::string& id, const std::string& details, std::string& why) {
    why.clear();
    if (!up_ || !valid_history_id(id)) return false;
    const db::Error e = run(db_, "UPDATE history_matches SET details = ?2 WHERE id = ?1 AND details IS NULL", {text_arg(id), text_arg(details)});
    if (e.ok()) return true;
    why = e.message;
    if ((e.code & 0xFF) == kSqliteConstraint) note("history: the details of " + id + " are refused by the database: " + e.message);
    else fail("the details of a record could not be kept", e);
    return false;
}

uint64_t HistoryStore::now_us() const {
    if (cfg_.clock_us) return cfg_.clock_us();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

int HistoryStore::time_box_expired(void* self) {
    const HistoryStore* store = static_cast<const HistoryStore*>(self);
    return store->now_us() >= store->deadline_us_ ? 1 : 0;
}

HistoryPage HistoryStore::query(const HistoryQuery& q, const std::function<Recording(const std::string& id)>& recording, Door door) const {
    HistoryPage page;
    if (!up_) {
        page.ok = false;
        page.fail = HistoryPage::Fail::Unavailable;
        return page;
    }
    // The server-wide budget of the public door (a token bucket in microseconds of query time): over it, the answer is "busy" at once and the database is not touched
    const uint64_t started = now_us();
    if (door == Door::Public) {
        if (budget_armed_) {
            const uint64_t whole_ms = (started - budget_at_us_) / 1000u;                                  // (whole milliseconds only: the rest stays for the next look, it is not lost)
            budget_us_ = std::min<int64_t>(static_cast<int64_t>(kBudgetBurstMs) * 1000, budget_us_ + static_cast<int64_t>(whole_ms * kBudgetPerSecondMs));
            budget_at_us_ += whole_ms * 1000u;
        } else {
            budget_armed_ = true;
            budget_us_ = static_cast<int64_t>(kBudgetBurstMs) * 1000;
            budget_at_us_ = started;
        }
        if (budget_us_ <= 0) {
            page.ok = false;
            page.fail = HistoryPage::Fail::Busy;
            return page;
        }
    }
    // The time box of this one query (the count, the page, the seats): the database stops it when the clock passes the deadline
    deadline_us_ = started + static_cast<uint64_t>(door == Door::Owner ? kOwnerBoxMs : kPublicBoxMs) * 1000u;
    db_.set_progress_handler(&HistoryStore::time_box_expired, const_cast<HistoryStore*>(this), kProgressOps);
    struct Release {
        db::Database& d;
        ~Release() { d.set_progress_handler(nullptr, nullptr, 0); }
    } release{db_};
    const auto finish = [&](const db::Error& e) {
        if (door == Door::Public) budget_us_ -= static_cast<int64_t>(now_us() - started);       // (the plain list is not paid for: it is not held back, and it costs next to nothing)
        if (e.ok()) return;
        page = HistoryPage();
        page.ok = false;
        if (e.code == db::kErrInterrupted) {
            page.fail = HistoryPage::Fail::TooHeavy;                       // (the query was too heavy, the database is fine)
        } else {
            page.fail = HistoryPage::Fail::Unavailable;
            fail("the list could not be read", e);
        }
    };
    // WHERE: pieces of fixed text with a "?" for each value, and the values in the same order
    std::string where;
    std::vector<Arg> args;
    const auto add = [&](const char* piece) { where += where.empty() ? std::string(" WHERE ") + piece : std::string(" AND ") + piece; };
    switch (q.format) {
        case HistoryQuery::Format::All: break;
        case HistoryQuery::Format::OneVsOne: add("m.format = ?"); args.push_back(text_arg("1v1")); break;
        case HistoryQuery::Format::Teams: add("m.format IN (?, ?)"); args.push_back(text_arg("2v1")); args.push_back(text_arg("2v2")); break;
        case HistoryQuery::Format::Free: add("m.format IN (?, ?)"); args.push_back(text_arg("ffa3")); args.push_back(text_arg("ffa4")); break;
    }
    switch (q.result) {
        case HistoryQuery::Result::All: break;
        case HistoryQuery::Result::Decided: add("m.result = ?"); args.push_back(text_arg("decided")); break;
        case HistoryQuery::Result::Draw: add("m.result = ?"); args.push_back(text_arg("draw")); break;
        case HistoryQuery::Result::Cut: add("m.result = ?"); args.push_back(text_arg("cut")); break;
    }
    switch (q.who) {
        case HistoryQuery::Who::Anyone: break;
        case HistoryQuery::Who::People: add("m.computers_only = ?"); args.push_back(int_arg(0)); break;
        case HistoryQuery::Who::Computers: add("m.computers_only = ?"); args.push_back(int_arg(1)); break;
    }
    switch (q.mode) {
        case HistoryQuery::Mode::All: break;
        case HistoryQuery::Mode::HighestScore: add("m.mode = ?"); args.push_back(text_arg(kHistoryModeScore)); break;
        case HistoryQuery::Mode::Kills187: add("m.mode = ?"); args.push_back(text_arg(kHistoryMode187)); break;
    }
    if (q.color >= 0 && q.color < sim::MAX_PLAYERS) {
        add("EXISTS (SELECT 1 FROM history_seats s WHERE s.match_id = m.id AND s.seat = ?)");
        args.push_back(int_arg(q.color));
    }
    const std::string needle = lower(q.text);
    if (!needle.empty()) {
        add("m.search LIKE ? ESCAPE '\\'");
        args.push_back(text_arg(like_contains(needle)));
    }
    const char* order = " ORDER BY m.ended DESC, m.id DESC";
    switch (q.sort) {
        case HistoryQuery::Sort::New: break;
        case HistoryQuery::Sort::Old: order = " ORDER BY m.ended ASC, m.id ASC"; break;
        case HistoryQuery::Sort::Score: order = " ORDER BY m.best_score DESC, m.ended DESC, m.id DESC"; break;
        case HistoryQuery::Sort::Kills: order = " ORDER BY m.best_kills DESC, m.ended DESC, m.id DESC"; break;
        case HistoryQuery::Sort::Hatched: order = " ORDER BY m.best_hatched DESC, m.ended DESC, m.id DESC"; break;
        case HistoryQuery::Sort::Long: order = " ORDER BY m.turns DESC, m.ended DESC, m.id DESC"; break;
        case HistoryQuery::Sort::Short: order = " ORDER BY m.turns ASC, m.ended DESC, m.id DESC"; break;
    }
    const size_t limit = std::min(std::max<size_t>(q.limit, 1), HistoryQuery::kMaxLimit);

    db::Error e;
    if (q.rec == HistoryQuery::Rec::All) {
        int64_t total = 0;
        e = scalar(db_, ("SELECT count(*) FROM history_matches m" + where).c_str(), args, total);
        page.count = static_cast<size_t>(total);
        if (e.ok()) {
            std::vector<Arg> page_args = args;
            page_args.push_back(int_arg(static_cast<int64_t>(limit)));
            page_args.push_back(int_arg(static_cast<int64_t>(q.offset)));
            db::Statement st;
            e = db_.prepare(st, (std::string("SELECT ") + kMatchColumns + " FROM history_matches m" + where + order + " LIMIT ? OFFSET ?").c_str());
            if (e.ok()) e = bind_args(st, page_args);
            while (e.ok()) {
                const db::Statement::Step step = st.step();
                if (step == db::Statement::Step::Failed) e = st.error();
                if (step != db::Statement::Step::Row) break;
                HistoryRecord r;
                read_match(st, r);
                page.items.push_back(std::move(r));
            }
        }
        if (e.ok()) e = load_seats_many(db_, page.items);
    } else {
        // the state of a recording is not in the database: the ids of the matches that fit the other filters are read from the index alone and asked about in order (`recording` is a lookup in memory),
        // and the page is cut from those that have that state
        std::vector<std::string> window;
        db::Statement st;
        e = db_.prepare(st, ("SELECT m.id FROM history_matches m" + where + order).c_str());
        if (e.ok()) e = bind_args(st, args);
        while (e.ok()) {
            const db::Statement::Step step = st.step();
            if (step == db::Statement::Step::Failed) e = st.error();
            if (step != db::Statement::Step::Row) break;
            const std::string id = st.column_text(0);
            const Recording state = recording ? recording(id) : Recording::Removed;
            const bool wanted = q.rec == HistoryQuery::Rec::Watch ? state == Recording::Watch : q.rec == HistoryQuery::Rec::Old ? state == Recording::Old : state == Recording::Removed;
            if (!wanted) continue;
            if (page.count >= q.offset && window.size() < limit) window.push_back(id);
            ++page.count;
        }
        if (e.ok()) e = load_many(db_, window, page.items);
    }
    finish(e);
    return page;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Counting the matches that the replay store keeps
// ---------------------------------------------------------------------------------------------------------------------------------

HistoryFeeder::HistoryFeeder(HistoryStore& history, const ReplayStore& replays, std::string maps_dir, std::function<uint64_t()> clock_us, std::string community_dir)
    : history_(history), replays_(replays), map_dirs_{std::move(maps_dir)}, details_maker_(make_history_details), deaths_maker_(make_history_deaths), clock_us_(std::move(clock_us)) {
    if (!community_dir.empty()) map_dirs_.push_back(std::move(community_dir));
    if (!clock_us_) {
        clock_us_ = []() { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); };
    }
}

// The map of a recording: an approved map of the players (a file of the submissions folder under a name of its own), else the maps folder and the community folder
bool HistoryFeeder::load_map_of(const replay::Header& head, assets::LevelData& level, std::string& why) const {
    if (approved_path_) {
        const std::string path = approved_path_(head.map_name);
        if (!path.empty()) return replay::load_map_file(head, path, level, why);
    }
    return replay::load_map(head, map_dirs_, level, why);
}

// ... and a map that starts more ants than a room plays online is not played here either: a recording is played in one piece on the one thread, and setting up the ants of such a map would hold it for seconds
// (no room makes such a recording; one from elsewhere may be one)
bool HistoryFeeder::load_level(const replay::Header& head, assets::LevelData& level, std::string& why) const {
    if (!load_map_of(head, level, why)) return false;
    if (!OnlineLimits::allows_ants(OnlineLimits::start_ants(level))) {
        why = "the map starts more ants than a room plays online";
        return false;
    }
    return true;
}

void HistoryFeeder::skip(const std::string& id, const std::string& why) {
    skipped_.insert(id);
    if (++skip_lines_ <= kMaxSkipLines) history_.report("history: " + id + " is not counted: " + why);
}

// The files of the index that are not in the history yet, the oldest at the front of the list that is worked from its back: the newest first. A file of another simulation's rules cannot be played here and is
// not queued.
void HistoryFeeder::rebuild() {
    std::vector<std::string> ids;                                                        // (oldest first, as the index is)
    size_t other = 0;
    const size_t ext = std::string(ReplayStore::kExtension).size();
    for (const ReplayEntry& e : replays_.entries()) {
        if (!e.readable) continue;
        if (!entry_plays_here(e)) {
            ++other;
            continue;
        }
        const std::string id = e.file.substr(0, e.file.size() - ext);
        if (skipped_.count(id) == 0) ids.push_back(id);
    }
    if (!history_.keep_unknown(ids)) return;                                             // (the database did not answer: the revision stays unseen, the next pass looks again)
    seen_revision_ = replays_.revision();
    other_rules_ = other;
    queue_.clear();
    for (const std::string& id : ids) queue_.push_back(history_recording_file(id));
}

bool HistoryFeeder::work(const std::string& file) {
    const ReplayEntry* entry = replays_.find(file);
    if (entry == nullptr || !entry->readable) return false;                              // (it went in the meantime: its 30 days, or the owner)
    const std::string id = file.substr(0, file.size() - std::string(ReplayStore::kExtension).size());
    std::vector<uint8_t> bytes;
    if (!replays_.read(file, bytes)) {
        skip(id, "the recording cannot be read from the replay store");        // (it is listed but the read failed: told once, not tried again before the server starts again)
        return true;
    }
    replay::Replay rep;
    std::string why;
    if (!replay::decode(bytes.data(), bytes.size(), rep, why)) {
        skip(id, "the recording cannot be read: " + why);
        return true;
    }
    assets::LevelData level;
    if (!replay::plays_here(rep.head)) {
        skip(id, replay::rules_refusal(rep.head));
        return true;
    }
    if (!load_level(rep.head, level, why)) {
        skip(id, why);
        return true;
    }
    HistoryRecord record;
    replay::StatsPlay played;
    try {
        played = replay::play_with_stats(rep, level);                                    // (one play: the totals and the details; the totals never depend on the details)
        if (!played.outcome.ok) {
            skip(id, played.outcome.error.empty() ? std::string("the recording did not play out here") : played.outcome.error);
            return true;
        }
        record = make_history_record(id, entry->ended_s, rep, played.outcome);
    } catch (const std::exception& ex) {                                                 // (the engine refuses a recording that no room made: it must not take the server down)
        skip(id, std::string("the recording could not be played: ") + ex.what());
        return true;
    }
    std::string no_stats;                                                                // (the details have a try of their own: whatever they do, the counters of the match are written)
    try {
        record.details = details_maker_(played.stats, record);
        if (record.details.empty()) no_stats = played.stats.why.empty() ? std::string("the pass did not count this match") : played.stats.why;
    } catch (const std::exception& ex) {
        record.details.clear();
        no_stats = std::string("the details could not be made: ") + ex.what();
    }
    if (record.details.empty()) record.details = history_details_marker();
    try {                                                                                // (the map of deaths has a try of its own too; only a match counted here has one: an older record is not played again for it)
        record.deaths = deaths_maker_ ? deaths_maker_(played.stats) : std::string();
    } catch (const std::exception& ex) {
        record.deaths.clear();
        if (++skip_lines_ <= kMaxSkipLines) history_.report("history: " + id + " has no map of deaths: " + ex.what());
    }
    if (!no_stats.empty() && ++skip_lines_ <= kMaxSkipLines) history_.report("history: " + id + " has no stats: " + no_stats);
    if (!history_.put(record, why)) {
        if (history_.available()) skipped_.insert(id);                                    // (the database refused this record: put() has told the log, it is not tried again)
        else queue_.push_back(file);                                                      // (the database is down: the file waits for it, step() does nothing until it answers again)
        return true;
    }
    ++counted_;
    return true;
}

// The counted matches that have no details, whose recording is still kept and plays here: the newest at the back of the list that is worked from its back
void HistoryFeeder::build_backfill() {
    std::vector<std::string> ids;                                                        // (newest first)
    if (!history_.ids_without_details(ids)) return;                                      // (the database did not answer: looked at again the next time)
    backfill_built_ = true;
    backfill_.clear();
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        const std::string file = history_recording_file(*it);
        const ReplayEntry* entry = replays_.find(file);
        if (entry != nullptr && entry->readable && entry_plays_here(*entry)) backfill_.push_back(file);
    }
}

// The details of one counted match, from its recording: played once more, with the stored record's own rows for who won. A recording that is read and cannot be played gets the marker (so that it is not
// played again at every start); one that could not be tried (the store did not give it, its map is not here) gets nothing and is tried again when the server starts again.
bool HistoryFeeder::backfill(const std::string& file) {
    const ReplayEntry* entry = replays_.find(file);
    if (entry == nullptr || !entry->readable) return false;                              // (it went in the meantime)
    const std::string id = file.substr(0, file.size() - std::string(ReplayStore::kExtension).size());
    HistoryRecord record;
    if (!history_.find(id, record)) return false;                                        // (the owner took it out, or the database does not answer)
    if (!record.details.empty()) return true;                                            // (it has some now)
    std::string details = history_details_marker(), why;
    bool for_now = false;                                                                // (the recording could not be tried at all: the store did not give it, or its map is not here: no marker, the next start tries again)
    std::vector<uint8_t> bytes;
    replay::Replay rep;
    assets::LevelData level;
    if (!replays_.read(file, bytes)) {
        why = "the recording cannot be read from the replay store";
        for_now = true;
    } else if (!replay::decode(bytes.data(), bytes.size(), rep, why)) why = "the recording cannot be read: " + why;
    else if (!replay::plays_here(rep.head)) why = replay::rules_refusal(rep.head);
    else if (!load_level(rep.head, level, why)) for_now = true;
    else {
        try {
            const replay::StatsPlay played = replay::play_with_stats(rep, level);
            if (played.outcome.ok && played.stats.ok) {
                const std::string text = details_maker_(played.stats, record);
                if (!text.empty()) details = text;
            } else {
                why = played.outcome.ok ? played.stats.why : (played.outcome.error.empty() ? std::string("the recording did not play out here") : played.outcome.error);
            }
        } catch (const std::exception& ex) {
            why = std::string("the recording could not be played: ") + ex.what();
        }
    }
    if (!why.empty() && ++skip_lines_ <= kMaxSkipLines) history_.report("history: " + id + (for_now ? " has no stats yet: " : " has no stats: ") + why);
    if (for_now) return true;                                                            // (nothing is written: a marker is for a recording that really cannot be played)
    std::string refused;
    if (!history_.set_details(id, details, refused)) {
        if (!history_.available()) backfill_.push_back(file);                            // (the database is down: the file waits for it)
        return true;
    }
    ++backfilled_;
    return true;
}

bool HistoryFeeder::step(const std::function<bool()>& idle, uint32_t now_ms) {
    history_.maintain(now_ms);
    if (!history_.available()) return false;
    if (!prune_armed_ || net::time_reached(now_ms, next_prune_ms_)) {                     // (cheap: only the markers of matches that the owner took out are looked at)
        prune_armed_ = true;
        next_prune_ms_ = now_ms + kPruneEveryMs;
        history_.prune_markers([this](const std::string& id) { return replays_.find(history_recording_file(id)) != nullptr; });
        if (!history_.available()) return false;
    }
    if (paced_ && !net::time_reached(now_ms, next_ms_)) return false;
    const bool backfill_due = !backfill_built_ || !backfill_.empty() || backfilled_ != reported_backfilled_;
    if (replays_.revision() == seen_revision_ && queue_.empty() && counted_ == reported_counted_ && skipped_.size() == reported_skipped_ && !backfill_due) return false;      // (nothing to do: the usual pass)
    if (!idle()) return false;
    if (replays_.revision() != seen_revision_) rebuild();
    if (!history_.available()) return false;
    if (queue_.empty()) {
        if (counted_ != reported_counted_ || skipped_.size() != reported_skipped_) {      // (the work of this run is done: one line says what came of it)
            history_.report("history: " + std::to_string(counted_ - reported_counted_) + " match(es) counted, " + std::to_string(skipped_.size() - reported_skipped_) + " left out; " + std::to_string(history_.count()) +
                            " kept in all" + (other_rules_ > 0 ? "; " + std::to_string(other_rules_) + " recording(s) of other simulation rules cannot be counted" : std::string()));
            reported_counted_ = counted_;
            reported_skipped_ = skipped_.size();
        }
        if (!backfill_built_) build_backfill();                                           // (the new files first: then the older records that have no details)
        if (!history_.available()) return false;
        if (backfill_.empty()) {
            if (backfilled_ != reported_backfilled_) {
                history_.report("history: the stats of " + std::to_string(backfilled_ - reported_backfilled_) + " older match(es) counted from their recordings");
                reported_backfilled_ = backfilled_;
            }
            return false;
        }
    }
    const bool fresh = !queue_.empty();
    std::string file;
    if (fresh) {
        file = queue_.back();
        queue_.pop_back();
    } else {
        file = backfill_.back();
        backfill_.pop_back();
    }
    paced_ = true;
    const uint64_t began = clock_us_();
    const bool played = fresh ? work(file) : backfill(file);
    const uint64_t spent_ms = (clock_us_() - began + 999u) / 1000u;                      // (what the pass really took, which the server's clock of this pass does not show)
    const uint64_t gap = std::max<uint64_t>(kPaceMs, spent_ms * kPaceFactor);
    next_ms_ = now_ms + static_cast<uint32_t>(std::min<uint64_t>(spent_ms + gap, 60u * 1000u));       // (counted from the END of the work, so passes never run back to back)
    return played;
}

}  // namespace ants::server
