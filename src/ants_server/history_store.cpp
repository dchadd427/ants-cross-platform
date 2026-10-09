#include "ants_server/history_store.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "ants_net/clock.hpp"
#include "ants_sim/start_teams.hpp"

namespace fs = std::filesystem;

namespace ants::server {

using ctl::JsonValue;

namespace {

const char* const kColours[sim::MAX_PLAYERS] = {"Green", "Red", "Blue", "Black"};
constexpr const char* kTempSuffix = ".tmp";
constexpr size_t kMaxUnreadableLines = 5;          // the first files that are not records are named in the log, the rest are counted

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool contains_ci(const std::string& hay, const std::string& needle_lower) { return lower(hay).find(needle_lower) != std::string::npos; }

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

uint32_t to_u32(int64_t v) { return v < 0 ? 0u : v > 0xFFFFFFFFll ? 0xFFFFFFFFu : static_cast<uint32_t>(v); }
int32_t to_i32(int64_t v) { return v < INT32_MIN ? INT32_MIN : v > INT32_MAX ? INT32_MAX : static_cast<int32_t>(v); }

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

// The order of the index: the end time, then the id (an id carries the "-2" of matches that ended in one second, so two records never tie)
bool older_record(const HistoryRecord& a, const HistoryRecord& b) {
    if (a.ended_s != b.ended_s) return a.ended_s < b.ended_s;
    return a.id < b.id;
}

int64_t ended_of_id(const std::string& id) {
    int64_t t = 0;
    return ReplayStore::time_of_name(history_recording_file(id), t) ? t : 0;
}

bool read_text(const std::string& path, std::string& out) {
    out.clear();
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec || size > kHistoryMaxFileBytes) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return !in.bad();
}

}  // namespace

ctl::JsonLimits history_file_limits() {
    ctl::JsonLimits limits;
    limits.max_bytes = kHistoryMaxFileBytes;
    limits.max_elements = 16384;
    limits.max_depth = 16;
    return limits;
}

const char* recording_name(Recording r) noexcept {
    switch (r) {
        case Recording::Watch: return "watch";
        case Recording::Old: return "old";
        case Recording::Removed: return "removed";
    }
    return "removed";
}

bool valid_history_id(const std::string& id) { return ReplayStore::valid_file_name(history_recording_file(id)); }

std::string history_recording_file(const std::string& id) { return id + ReplayStore::kExtension; }

// ---------------------------------------------------------------------------------------------------------------------------------
// The record of a match
// ---------------------------------------------------------------------------------------------------------------------------------

HistoryRecord make_history_record(const std::string& id, int64_t ended_s, const replay::Replay& replay, const replay::Outcome& outcome) {
    HistoryRecord rec;
    rec.id = id;
    rec.ended_s = ended_s;
    rec.map = replay.head.map_name;
    rec.game = replay.head.game_version;
    rec.turns = outcome.turns;
    rec.finished = outcome.match_over;
    rec.quitter = outcome.result.quitter < sim::MAX_PLAYERS ? static_cast<int>(outcome.result.quitter) : -1;

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
        s.score = st.score;
        s.killed = st.enemy_killed;
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

    // the results, as the match's own screen builds them (the same sort, a tie broken by the seat: nobody is "local" here)
    for (const sim::ResultRow& r : outcome.result.rows(sim::PLAYER_NEUTRAL)) {
        HistoryRow row;
        row.seats.push_back(r.first);
        if (r.has_second()) row.seats.push_back(r.second);
        row.score = r.score;
        row.killed = r.enemy_killed;
        row.lost = r.friendly_lost;
        row.hatched = r.new_hatched;
        rec.rows.push_back(std::move(row));
    }
    rec.draw = rec.quitter < 0 && rec.rows.size() >= 2 && rec.rows[0].score == rec.rows[1].score && rec.rows[0].killed == rec.rows[1].killed && rec.rows[0].lost == rec.rows[1].lost &&
               rec.rows[0].hatched == rec.rows[1].hatched;
    if (!rec.draw && !rec.rows.empty()) {
        rec.rows[0].winner = true;
        for (HistorySeat& s : rec.seats) {
            for (const uint8_t member : rec.rows[0].seats) s.winner = s.winner || s.seat == member;
        }
    }
    return rec;
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
    o.set("format", JsonValue::make_string(r.format));
    o.set("draw", JsonValue::make_bool(r.draw));
    o.set("computers_only", JsonValue::make_bool(r.computers_only));
    JsonValue seats = JsonValue::make_array();
    for (const HistorySeat& s : r.seats) seats.push_back(seat_json(s, full));
    o.set("seats", std::move(seats));
    JsonValue rows = JsonValue::make_array();
    for (const HistoryRow& row : r.rows) rows.push_back(row_json(row));
    o.set("rows", std::move(rows));
    return o;
}

bool history_record_from_json(const JsonValue& j, HistoryRecord& out, std::string& why) {
    if (!j.is_object()) {
        why = "not a JSON object";
        return false;
    }
    if (j.get("v").as_int_or(0) < 1) {
        why = "no layout number (\"v\")";
        return false;
    }
    HistoryRecord r;
    r.id = j.get("id").as_string_or();
    if (!valid_history_id(r.id)) {
        why = "\"id\" is not the name of a match";
        return false;
    }
    if (!j.get("ended").is_int() || !j.get("turns").is_int() || !j.get("map").is_string() || !j.get("seats").is_array() || !j.get("rows").is_array()) {
        why = "a key that every record has is missing or of the wrong kind";
        return false;
    }
    r.ended_s = j.get("ended").as_int_or(0);
    r.map = j.get("map").str();
    r.game = j.get("game").as_string_or();
    r.turns = to_u32(j.get("turns").as_int_or(0));
    r.finished = j.get("finished").as_bool_or(false);
    const int64_t quitter = j.get("quitter").as_int_or(-1);
    r.quitter = quitter >= 0 && quitter < sim::MAX_PLAYERS ? static_cast<int>(quitter) : -1;
    r.format = j.get("format").as_string_or();
    r.draw = j.get("draw").as_bool_or(false);
    r.computers_only = j.get("computers_only").as_bool_or(false);
    const JsonValue& seats = j.get("seats");
    if (seats.size() == 0 || seats.size() > sim::MAX_PLAYERS) {
        why = "\"seats\" holds no seat or too many";
        return false;
    }
    for (const JsonValue& item : seats.items()) {
        const int64_t seat = item.get("seat").as_int_or(-1);
        if (!item.is_object() || seat < 0 || seat >= sim::MAX_PLAYERS) {
            why = "a seat that is not one";
            return false;
        }
        HistorySeat s;
        s.seat = static_cast<uint8_t>(seat);
        s.name = item.get("name").as_string_or();
        s.bot = item.get("bot").as_string_or();
        s.score = to_i32(item.get("score").as_int_or(0));
        s.killed = to_u32(item.get("killed").as_int_or(0));
        s.lost = to_u32(item.get("lost").as_int_or(0));
        s.hatched = to_u32(item.get("hatched").as_int_or(0));
        s.bombs_planted = to_u32(item.get("bombs_planted").as_int_or(0));
        s.bombs_defused = to_u32(item.get("bombs_defused").as_int_or(0));
        s.fires_lit = to_u32(item.get("fires_lit").as_int_or(0));
        s.left = item.get("left").as_bool_or(false);
        s.winner = item.get("winner").as_bool_or(false);
        r.seats.push_back(std::move(s));
    }
    for (const JsonValue& item : j.get("rows").items()) {
        HistoryRow row;
        for (const JsonValue& s : item.get("seats").items()) {
            const int64_t seat = s.as_int_or(-1);
            if (seat >= 0 && seat < sim::MAX_PLAYERS && row.seats.size() < 2) row.seats.push_back(static_cast<uint8_t>(seat));
        }
        if (row.seats.empty()) {
            why = "a row without a seat";
            return false;
        }
        row.score = to_i32(item.get("score").as_int_or(0));
        row.killed = to_i32(item.get("killed").as_int_or(0));
        row.lost = to_i32(item.get("lost").as_int_or(0));
        row.hatched = to_i32(item.get("hatched").as_int_or(0));
        row.winner = item.get("winner").as_bool_or(false);
        r.rows.push_back(std::move(row));
    }
    out = std::move(r);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------------------------------------------------------------

HistoryStore::HistoryStore(HistoryConfig config) : cfg_(std::move(config)) {}

std::string HistoryStore::path_of(const std::string& id, const char* suffix) const { return (fs::path(cfg_.dir) / (id + suffix)).string(); }

void HistoryStore::note(const std::string& line) {
    if (notes_.size() < kMaxNoteLines) notes_.push_back(line);
}

std::vector<std::string> HistoryStore::take_notes() {
    std::vector<std::string> out;
    out.swap(notes_);
    return out;
}

void HistoryStore::insert(HistoryRecord record) {
    const auto at = std::lower_bound(records_.begin(), records_.end(), record, older_record);
    if (at != records_.end() && at->id == record.id) {
        *at = std::move(record);
        return;
    }
    records_.insert(at, std::move(record));
}

bool HistoryStore::prepare(std::string& why) {
    ready_ = false;
    records_.clear();
    markers_.clear();
    if (cfg_.dir.empty()) {
        why = "no folder for the match history";
        return false;
    }
    std::error_code ec;
    fs::create_directories(cfg_.dir, ec);
    if (!fs::is_directory(cfg_.dir, ec)) {
        why = "the history folder '" + cfg_.dir + "' cannot be made or is not a folder";
        return false;
    }
    fs::directory_iterator it(cfg_.dir, ec);
    if (ec) {
        why = "the history folder '" + cfg_.dir + "' cannot be read: " + ec.message();
        return false;
    }
    size_t temps = 0;
    size_t unreadable = 0;
    std::vector<HistoryRecord> found;
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code entry_ec;
        const fs::file_status status = it->symlink_status(entry_ec);
        if (entry_ec || status.type() != fs::file_type::regular) continue;                 // (a link or a folder is not a file of the store)
        const std::string name = it->path().filename().string();
        const std::string ext = kHistoryExtension;
        const std::string temp_ext = ext + kTempSuffix;
        if (name.size() > temp_ext.size() && name.compare(name.size() - temp_ext.size(), temp_ext.size(), temp_ext) == 0 && valid_history_id(name.substr(0, name.size() - temp_ext.size()))) {
            fs::remove(it->path(), entry_ec);                                               // the write of a crash (one server writes here: nothing else makes these)
            ++temps;
            continue;
        }
        if (name.size() <= ext.size() || name.compare(name.size() - ext.size(), ext.size(), ext) != 0) continue;      // (not ours: left alone)
        const std::string id = name.substr(0, name.size() - ext.size());
        if (!valid_history_id(id)) continue;
        std::string text;
        JsonValue json;
        std::string reason;
        if (!read_text(it->path().string(), text) || !ctl::parse_json(text, json, &reason, history_file_limits())) {
            if (reason.empty()) reason = "it cannot be read";
            if (++unreadable <= kMaxUnreadableLines) note("history: " + name + " is not read: " + reason);
            continue;
        }
        if (json.get("deleted").as_bool_or(false)) {
            markers_.insert(id);
            continue;
        }
        HistoryRecord rec;
        if (!history_record_from_json(json, rec, reason) || rec.id != id) {
            if (reason.empty()) reason = "its id is not its name";
            if (++unreadable <= kMaxUnreadableLines) note("history: " + name + " is not a record: " + reason);
            continue;
        }
        found.push_back(std::move(rec));
    }
    if (ec) {
        why = "the history folder '" + cfg_.dir + "' cannot be read: " + ec.message();
        return false;
    }
    std::sort(found.begin(), found.end(), older_record);
    records_ = std::move(found);
    ready_ = true;
    std::string line = "history in " + cfg_.dir + ": " + std::to_string(records_.size()) + " match(es) kept for good";
    if (!markers_.empty()) line += ", " + std::to_string(markers_.size()) + " taken out by the owner";
    if (temps > 0) line += "; " + std::to_string(temps) + " half-written file(s) of an interrupted write deleted";
    if (unreadable > 0) line += "; " + std::to_string(unreadable) + " file(s) that are no records were left where they are";
    note(line);
    return true;
}

bool HistoryStore::write_file(const std::string& id, const std::string& text, std::string& why) {
    const std::string temp = path_of(id, (std::string(kHistoryExtension) + kTempSuffix).c_str());
    std::error_code ec;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (out) out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            fs::remove(temp, ec);
            why = "the disk refused the write";
            return false;
        }
    }
    if (cfg_.rename_file) cfg_.rename_file(temp, path_of(id), ec);
    else fs::rename(temp, path_of(id), ec);                                                // (replaces a file of that name: a record that is counted again, a marker)
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        why = "the file could not be given its name: " + ec.message();
        return false;
    }
    return true;
}

bool HistoryStore::put(const HistoryRecord& record, std::string& why) {
    why.clear();
    if (!ready_) {
        why = "the server keeps no match history";
        return false;
    }
    if (!valid_history_id(record.id)) {
        why = "the id is not the name of a match";
        return false;
    }
    const std::string text = ctl::to_json(history_record_to_json(record, true)) + "\n";
    if (text.size() > kHistoryMaxFileBytes) {
        why = "the record is too long";
        return false;
    }
    if (!write_file(record.id, text, why)) {
        note("history: " + record.id + " could not be kept: " + why);
        return false;
    }
    markers_.erase(record.id);
    insert(record);
    return true;
}

bool HistoryStore::remove(const std::string& id, bool keep_marker, std::string& why) {
    why.clear();
    if (!ready_ || !valid_history_id(id)) return false;
    HistoryRecord probe;
    probe.id = id;
    probe.ended_s = ended_of_id(id);
    const auto at = std::lower_bound(records_.begin(), records_.end(), probe, older_record);
    if (at == records_.end() || at->id != id) return false;
    if (keep_marker) {
        JsonValue marker = JsonValue::make_object();
        marker.set("v", JsonValue::make_int(kHistoryVersion));
        marker.set("id", JsonValue::make_string(id));
        marker.set("deleted", JsonValue::make_bool(true));
        if (!write_file(id, ctl::to_json(marker) + "\n", why)) {
            note("history: " + id + " could not be taken out: " + why);
            return false;
        }
        markers_.insert(id);
    } else {
        std::error_code ec;
        fs::remove(path_of(id), ec);
        if (ec) {
            why = "the file could not be deleted: " + ec.message();
            note("history: " + id + " could not be taken out: " + why);
            return false;
        }
    }
    records_.erase(at);
    return true;
}

size_t HistoryStore::prune_markers(const std::function<bool(const std::string& id)>& still_needed) {
    size_t deleted = 0;
    for (auto it = markers_.begin(); it != markers_.end();) {
        if (still_needed(*it)) {
            ++it;
            continue;
        }
        std::error_code ec;
        fs::remove(path_of(*it), ec);
        if (ec) {
            ++it;                                                                           // (tried again at the next look)
            continue;
        }
        it = markers_.erase(it);
        ++deleted;
    }
    return deleted;
}

const HistoryRecord* HistoryStore::find(const std::string& id) const {
    if (!valid_history_id(id)) return nullptr;
    HistoryRecord probe;
    probe.id = id;
    probe.ended_s = ended_of_id(id);
    const auto at = std::lower_bound(records_.begin(), records_.end(), probe, older_record);
    return at != records_.end() && at->id == id ? &*at : nullptr;
}

bool HistoryStore::knows(const std::string& id) const { return find(id) != nullptr || markers_.count(id) != 0; }

bool HistoryStore::read_file(const std::string& id, std::string& text) const {
    text.clear();
    return ready_ && find(id) != nullptr && read_text(path_of(id), text);
}

namespace {

bool format_fits(HistoryQuery::Format want, const std::string& format) {
    switch (want) {
        case HistoryQuery::Format::All: return true;
        case HistoryQuery::Format::OneVsOne: return format == "1v1";
        case HistoryQuery::Format::Teams: return format == "2v1" || format == "2v2";
        case HistoryQuery::Format::Free: return format == "ffa3" || format == "ffa4";
    }
    return true;
}

bool result_fits(HistoryQuery::Result want, const HistoryRecord& r) {
    const bool cut = !r.finished || r.quitter >= 0;
    switch (want) {
        case HistoryQuery::Result::All: return true;
        case HistoryQuery::Result::Draw: return r.draw;
        case HistoryQuery::Result::Cut: return !r.draw && cut;
        case HistoryQuery::Result::Decided: return !r.draw && !cut;
    }
    return true;
}

std::string map_stem_lower(const std::string& map) { return lower(ReplayStore::map_stem(map)); }

// the words of a record that a search looks in: the map's name, and for each seat the name that was typed, "Bot (Level)" or the colour (what the pages show)
bool text_fits(const HistoryRecord& r, const std::string& needle_lower) {
    if (needle_lower.empty()) return true;
    if (map_stem_lower(r.map).find(needle_lower) != std::string::npos) return true;
    for (const HistorySeat& s : r.seats) {
        const std::string shown = !s.bot.empty() ? "Bot (" + s.bot + ")" : !s.name.empty() ? s.name : std::string(kColours[s.seat < sim::MAX_PLAYERS ? s.seat : 0]);
        if (contains_ci(shown, needle_lower)) return true;
    }
    return false;
}

template <typename Get>
int64_t best_of(const HistoryRecord& r, Get get) {
    int64_t best = 0;
    for (const HistorySeat& s : r.seats) best = std::max<int64_t>(best, get(s));
    return best;
}

}  // namespace

HistoryPage HistoryStore::query(const HistoryQuery& q, const std::function<Recording(const std::string& id)>& recording) const {
    HistoryPage page;
    const std::string needle = lower(q.text);
    std::vector<const HistoryRecord*> fits;
    // newest first: the index runs oldest first
    for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
        const HistoryRecord& r = *it;
        if (!format_fits(q.format, r.format) || !result_fits(q.result, r)) continue;
        if (q.who == HistoryQuery::Who::People && r.computers_only) continue;
        if (q.who == HistoryQuery::Who::Computers && !r.computers_only) continue;
        if (q.colour >= 0) {
            bool takes_part = false;
            for (const HistorySeat& s : r.seats) takes_part = takes_part || s.seat == q.colour;
            if (!takes_part) continue;
        }
        if (!text_fits(r, needle)) continue;
        if (q.rec != HistoryQuery::Rec::All) {
            const Recording state = recording ? recording(r.id) : Recording::Removed;
            const bool wanted = q.rec == HistoryQuery::Rec::Watch ? state == Recording::Watch : q.rec == HistoryQuery::Rec::Old ? state == Recording::Old : state == Recording::Removed;
            if (!wanted) continue;
        }
        fits.push_back(&r);
    }
    page.count = fits.size();
    switch (q.sort) {
        case HistoryQuery::Sort::New: break;
        case HistoryQuery::Sort::Old: std::reverse(fits.begin(), fits.end()); break;
        default: {
            const auto key = [&](const HistoryRecord& r) -> int64_t {
                switch (q.sort) {
                    case HistoryQuery::Sort::Score: return best_of(r, [](const HistorySeat& s) { return int64_t{s.score}; });
                    case HistoryQuery::Sort::Kills: return best_of(r, [](const HistorySeat& s) { return int64_t{s.killed}; });
                    case HistoryQuery::Sort::Hatched: return best_of(r, [](const HistorySeat& s) { return int64_t{s.hatched}; });
                    case HistoryQuery::Sort::Long: return int64_t{r.turns};
                    case HistoryQuery::Sort::Short: return -int64_t{r.turns};
                    default: return 0;
                }
            };
            std::stable_sort(fits.begin(), fits.end(), [&](const HistoryRecord* a, const HistoryRecord* b) { return key(*a) > key(*b); });      // (equal: the newer first, as they come)
        }
    }
    const size_t from = std::min(q.offset, fits.size());
    const size_t to = std::min(fits.size(), from + q.limit);
    page.items.assign(fits.begin() + static_cast<std::ptrdiff_t>(from), fits.begin() + static_cast<std::ptrdiff_t>(to));
    return page;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Counting the matches that the replay store keeps
// ---------------------------------------------------------------------------------------------------------------------------------

HistoryFeeder::HistoryFeeder(HistoryStore& history, const ReplayStore& replays, std::string maps_dir) : history_(history), replays_(replays), maps_dir_(std::move(maps_dir)) {}

void HistoryFeeder::skip(const std::string& id, const std::string& why) {
    skipped_.insert(id);
    if (++skip_lines_ <= kMaxSkipLines) history_.report("history: " + id + " is not counted: " + why);
}

// The files of the index that are not in the history yet, the oldest at the front of the list that is worked from its back: the newest first. A file of another simulation's rules cannot be played here and is
// not queued.
void HistoryFeeder::rebuild() {
    seen_revision_ = replays_.revision();
    queue_.clear();
    other_rules_ = 0;
    const std::vector<ReplayEntry>& index = replays_.entries();                          // (oldest first)
    for (const ReplayEntry& e : index) {
        if (!e.readable) continue;
        if (e.sim_rules != replay::kSimRules) {
            ++other_rules_;
            continue;
        }
        const std::string id = e.file.substr(0, e.file.size() - std::string(ReplayStore::kExtension).size());
        if (history_.knows(id) || skipped_.count(id) != 0) continue;
        queue_.push_back(e.file);
    }
}

bool HistoryFeeder::work(const std::string& file) {
    const ReplayEntry* entry = replays_.find(file);
    if (entry == nullptr || !entry->readable) return false;                              // (it went in the meantime: its 30 days, or the owner)
    const std::string id = file.substr(0, file.size() - std::string(ReplayStore::kExtension).size());
    std::vector<uint8_t> bytes;
    if (!replays_.read(file, bytes)) return false;
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
    if (!replay::load_map(rep.head, maps_dir_, level, why)) {
        skip(id, why);
        return true;
    }
    const replay::Outcome outcome = replay::play(rep, level);
    if (!outcome.ok) {
        skip(id, outcome.error.empty() ? std::string("the recording did not play out here") : outcome.error);
        return true;
    }
    if (!history_.put(make_history_record(id, entry->ended_s, rep, outcome), why)) {
        skipped_.insert(id);                                                              // (put() has told the log)
        return true;
    }
    ++counted_;
    return true;
}

bool HistoryFeeder::step(const std::function<bool()>& idle, uint32_t now_ms) {
    if (!history_.enabled()) return false;
    if (!prune_armed_ || net::time_reached(now_ms, next_prune_ms_)) {                     // (cheap: only the markers of matches that the owner took out are looked at)
        prune_armed_ = true;
        next_prune_ms_ = now_ms + kPruneEveryMs;
        history_.prune_markers([this](const std::string& id) { return replays_.find(history_recording_file(id)) != nullptr; });
    }
    if (paced_ && !net::time_reached(now_ms, next_ms_)) return false;
    if (replays_.revision() == seen_revision_ && queue_.empty() && counted_ == reported_counted_ && skipped_.size() == reported_skipped_) return false;      // (nothing to do: the usual pass)
    if (!idle()) return false;
    if (replays_.revision() != seen_revision_) rebuild();
    if (queue_.empty()) {
        if (counted_ != reported_counted_ || skipped_.size() != reported_skipped_) {      // (the work of this run is done: one line says what came of it)
            history_.report("history: " + std::to_string(counted_ - reported_counted_) + " match(es) counted, " + std::to_string(skipped_.size() - reported_skipped_) + " left out; " + std::to_string(history_.count()) +
                            " kept in all" + (other_rules_ > 0 ? "; " + std::to_string(other_rules_) + " recording(s) of other simulation rules cannot be counted" : std::string()));
            reported_counted_ = counted_;
            reported_skipped_ = skipped_.size();
        }
        return false;
    }
    const std::string file = queue_.back();
    queue_.pop_back();
    paced_ = true;
    const bool played = work(file);
    next_ms_ = now_ms + kPaceMs;
    return played;
}

}  // namespace ants::server
