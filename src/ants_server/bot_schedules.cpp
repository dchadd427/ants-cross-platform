#include "ants_server/bot_schedules.hpp"

#include <algorithm>

#include "ants_db/database.hpp"
#include "ants_server/pacific_time.hpp"

namespace ants::server {

namespace {

constexpr size_t kMaxNameChars = 40;
constexpr int64_t kRestartDelayS = 60;                 // after a restart (and after a save) the first run is a minute away at the soonest
constexpr size_t kNoticesMax = 100;

// One code point of UTF-8 text (the text is valid: ctl::is_valid_utf8 was asked)
size_t decode_point(const std::string& s, size_t at, uint32_t& cp) {
    const unsigned char c = static_cast<unsigned char>(s[at]);
    if (c < 0x80) { cp = c; return 1; }
    if (c < 0xE0) { cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[at + 1]) & 0x3Fu); return 2; }
    if (c < 0xF0) { cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[at + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[at + 2]) & 0x3Fu); return 3; }
    cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[at + 1]) & 0x3Fu) << 12) | ((static_cast<unsigned char>(s[at + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[at + 3]) & 0x3Fu);
    return 4;
}

bool is_blank_point(uint32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || cp == 0x2028 || cp == 0x2029; }            // (controls and the line separators: a blank)
bool is_invisible_point(uint32_t cp) {                                                                                         // (left out: they do not show, or they turn the words around them)
    return cp == 0x00AD || cp == 0x061C || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x206F) || cp == 0xFEFF || (cp >= 0xFFF9 && cp <= 0xFFFB) || (cp >= 0xE0000 && cp <= 0xE007F);
}

const char* kind_word(BotSchedule::Kind kind) { return kind == BotSchedule::Kind::Times ? "times" : "every"; }

ctl::JsonValue times_json(const std::vector<int>& times) {
    ctl::JsonValue a = ctl::JsonValue::make_array();
    for (const int t : times) a.push_back(ctl::JsonValue::make_string(clock_time_text(t)));
    return a;
}

}  // namespace

bool clean_schedule_name(const std::string& in, std::string& out) {
    if (!ctl::is_valid_utf8(in) || in.size() > 4 * kMaxNameChars * 4) return false;
    std::string text;
    size_t points = 0;
    bool space_before = true;                                  // (the blanks at the start are cut)
    for (size_t at = 0; at < in.size();) {
        uint32_t cp = 0;
        const size_t n = decode_point(in, at, cp);
        if (is_invisible_point(cp)) {
            at += n;
            continue;
        }
        const bool blank = cp == ' ' || is_blank_point(cp);
        if (blank) {
            if (!space_before) { text += ' '; ++points; }
            space_before = true;
        } else {
            text.append(in, at, n);
            ++points;
            space_before = false;
        }
        at += n;
    }
    if (!text.empty() && text.back() == ' ') { text.pop_back(); --points; }
    if (points < 1 || points > kMaxNameChars) return false;
    out = std::move(text);
    return true;
}

bool parse_clock_time(const std::string& text, int& minutes) {
    const size_t colon = text.find(':');
    if (colon == std::string::npos || colon < 1 || colon > 2 || text.size() != colon + 3) return false;
    int hour = 0;
    for (size_t i = 0; i < colon; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        hour = hour * 10 + (text[i] - '0');
    }
    if (text[colon + 1] < '0' || text[colon + 1] > '5' || text[colon + 2] < '0' || text[colon + 2] > '9') return false;
    const int minute = (text[colon + 1] - '0') * 10 + (text[colon + 2] - '0');
    if (hour > 23) return false;
    minutes = hour * 60 + minute;
    return true;
}

std::string clock_time_text(int minutes) {
    const int h = minutes / 60, m = minutes % 60;
    return std::string(1, static_cast<char>('0' + h / 10)) + static_cast<char>('0' + h % 10) + ":" + static_cast<char>('0' + m / 10) + static_cast<char>('0' + m % 10);
}

bool parse_schedule_patch(const ctl::JsonValue& body, bool create, BotSchedulePatch& out, std::string& error) {
    BotSchedulePatch patch;
    if (create || body.has("name")) {
        if (!body.get("name").is_string() || !clean_schedule_name(body.get("name").str(), patch.name)) {
            error = "bad_name";
            return false;
        }
        patch.has_name = true;
    }
    if (body.has("on")) {
        if (!body.get("on").is_bool()) {
            error = "bad_on";
            return false;
        }
        patch.has_on = true;
        patch.on = body.get("on").as_bool_or(true);
    }
    if (create || body.has("when")) {
        const ctl::JsonValue& when = body.get("when");
        if (!when.is_object() || !when.get("kind").is_string()) {
            error = "bad_when";
            return false;
        }
        patch.has_when = true;
        if (when.get("kind").str() == "every") {
            patch.kind = BotSchedule::Kind::Every;
            const ctl::JsonValue& m = when.get("minutes");
            int64_t minutes = -1;
            if (m.is_int()) {
                minutes = m.as_int_or(-1);
            } else if (m.is_string() && !m.str().empty() && m.str().size() <= 4) {
                minutes = 0;
                for (const char c : m.str()) {
                    if (c < '0' || c > '9') { minutes = -1; break; }
                    minutes = minutes * 10 + (c - '0');
                }
            }
            if (minutes < kMinBotEveryMinutes || minutes > kMaxBotEveryMinutes) {
                error = "bad_every";
                return false;
            }
            patch.minutes = static_cast<int>(minutes);
        } else if (when.get("kind").str() == "times") {
            patch.kind = BotSchedule::Kind::Times;
            const ctl::JsonValue& times = when.get("times");
            if (!times.is_array() || times.size() == 0) {
                error = "no_times";
                return false;
            }
            if (times.size() > kMaxBotScheduleTimes) {
                error = "bad_times";
                return false;
            }
            for (size_t i = 0; i < times.size(); ++i) {
                int minutes = 0;
                if (!times.at(i).is_string() || !parse_clock_time(times.at(i).str(), minutes)) {
                    error = "bad_times";
                    return false;
                }
                patch.times.push_back(minutes);
            }
            std::sort(patch.times.begin(), patch.times.end());
            patch.times.erase(std::unique(patch.times.begin(), patch.times.end()), patch.times.end());
        } else {
            error = "bad_when";
            return false;
        }
    }
    if (create || body.has("what")) {
        if (!parse_bot_what(body.get("what"), patch.what, error)) return false;
        patch.has_what = true;
    }
    out = std::move(patch);
    return true;
}

int64_t bot_schedule_planned_next(const BotSchedule& schedule, int64_t now_s) {
    if (!schedule.on) return 0;
    if (schedule.kind == BotSchedule::Kind::Times) return next_pacific_run(now_s, schedule.times);
    const int64_t base = schedule.last_run > 0 ? schedule.last_run + static_cast<int64_t>(schedule.minutes) * 60 : now_s + static_cast<int64_t>(schedule.minutes) * 60;
    return std::max(base, now_s + kRestartDelayS);
}

int64_t bot_schedule_next_after_run(const BotSchedule& schedule, int64_t ran_s) {
    if (!schedule.on) return 0;
    if (schedule.kind == BotSchedule::Kind::Times) return next_pacific_run(ran_s, schedule.times);
    return ran_s + static_cast<int64_t>(schedule.minutes) * 60;
}

int64_t bot_schedule_resumed_next(const BotSchedule& schedule, int64_t now_s) {
    if (!schedule.on) return 0;
    if (schedule.kind == BotSchedule::Kind::Times) return next_pacific_run(now_s, schedule.times);
    return std::max(schedule.next_run, now_s + kRestartDelayS);
}

// ------------------------------------------------------------------------------------------------ the store

BotScheduleStore::BotScheduleStore(db::Database* database) : db_(database) {}

bool BotScheduleStore::database_on() const { return db_ != nullptr && db_->is_open(); }

const BotSchedule* BotScheduleStore::find(int64_t id) const noexcept {
    for (const BotSchedule& row : rows_) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

std::vector<std::string> BotScheduleStore::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

bool BotScheduleStore::open(int64_t now_s, std::string& why) {
    rows_.clear();
    if (!database_on()) return true;
    db::Statement st;
    if (!db_->prepare(st, "SELECT id, name, enabled, is_default, kind, minutes, times, what, last_run, next_run, created_at FROM bot_schedules ORDER BY id").ok()) {
        why = "the database has no table of schedules";
        return false;
    }
    size_t unreadable = 0;
    while (st.step() == db::Statement::Step::Row) {
        BotSchedule row;
        row.id = st.column_int64(0);
        row.name = st.column_text(1);
        row.on = st.column_int64(2) != 0;
        row.is_default = st.column_int64(3) != 0;
        row.kind = st.column_text(4) == "times" ? BotSchedule::Kind::Times : BotSchedule::Kind::Every;
        row.minutes = static_cast<int>(st.column_int64(5));
        row.last_run = st.column_int64(8);
        row.next_run = st.column_int64(9);
        row.created_at = st.column_int64(10);
        ctl::JsonValue times, what;
        bool good = ctl::parse_json(st.column_text(6), times, nullptr) && times.is_array() && ctl::parse_json(st.column_text(7), what, nullptr) && parse_bot_what(what, row.what, why);
        for (size_t i = 0; good && i < times.size(); ++i) {
            int minutes = 0;
            good = times.at(i).is_string() && parse_clock_time(times.at(i).str(), minutes);
            if (good) row.times.push_back(minutes);
        }
        if (good && row.kind == BotSchedule::Kind::Times && row.times.empty()) good = false;
        if (!good) {
            ++unreadable;
            continue;
        }
        row.next_run = bot_schedule_resumed_next(row, now_s);                      // (a restart: the first run is a minute away at the soonest, and a run that was due while the server was down is not made up)
        rows_.push_back(std::move(row));
    }
    if (unreadable != 0) notices_.push_back("bot schedules: " + std::to_string(unreadable) + " row(s) of the table could not be read and are left out");
    return true;
}

bool BotScheduleStore::write_row(const BotSchedule& row, bool insert, int64_t& new_id) {
    if (!database_on()) {
        if (insert) {
            int64_t top = 0;
            for (const BotSchedule& r : rows_) top = std::max(top, r.id);
            new_id = top + 1;
        }
        return true;
    }
    db::Statement st;
    const std::string times = ctl::to_json(times_json(row.times));
    const std::string what = ctl::to_json(bot_what_json(row.what));
    const char* sql = insert ? "INSERT INTO bot_schedules (name, enabled, is_default, kind, minutes, times, what, last_run, next_run, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
                             : "UPDATE bot_schedules SET name = ?, enabled = ?, is_default = ?, kind = ?, minutes = ?, times = ?, what = ?, last_run = ?, next_run = ?, created_at = ? WHERE id = ?";
    const bool done = db_->prepare(st, sql).ok() && st.bind_text(1, row.name).ok() && st.bind_int64(2, row.on ? 1 : 0).ok() && st.bind_int64(3, row.is_default ? 1 : 0).ok() && st.bind_text(4, kind_word(row.kind)).ok() &&
                      st.bind_int64(5, row.minutes).ok() && st.bind_text(6, times).ok() && st.bind_text(7, what).ok() && st.bind_int64(8, row.last_run).ok() && st.bind_int64(9, row.next_run).ok() &&
                      st.bind_int64(10, row.created_at).ok() && (insert || st.bind_int64(11, row.id).ok()) && st.step() == db::Statement::Step::Done;
    if (done && insert) new_id = db_->last_insert_rowid();
    return done;
}

bool BotScheduleStore::seed_default(int every_minutes, int64_t now_s) {
    if (every_minutes < kMinBotEveryMinutes || every_minutes > kMaxBotEveryMinutes || !rows_.empty()) return false;
    if (database_on()) {
        db::Statement seen;
        if (!db_->prepare(seen, "SELECT value FROM meta WHERE key = 'bot_schedules_seeded'").ok()) return false;
        if (seen.step() != db::Statement::Step::Done) return false;             // (a row: the default was made once, and an admin may have deleted it; Failed: the database says nothing)
    }
    BotSchedule row;
    row.name = every_minutes == 60 ? "Hourly rotation" : "Rotation, every " + std::to_string(every_minutes) + " minutes";
    row.is_default = true;
    row.kind = BotSchedule::Kind::Every;
    row.minutes = every_minutes;
    row.created_at = now_s;
    row.next_run = now_s + kRestartDelayS;                                          // (the first match a minute after the server is up, as the old timer made it)
    int64_t id = 0;
    if (database_on()) {
        db::Transaction tx(*db_);
        if (!tx.active() || !write_row(row, true, id) || !db_->exec("INSERT OR REPLACE INTO meta (key, value) VALUES ('bot_schedules_seeded', '1')").ok() || !tx.commit().ok()) {
            notices_.push_back("bot schedules: the database refused the default schedule");
            return false;
        }
    } else {
        write_row(row, true, id);
    }
    row.id = id;
    rows_.push_back(std::move(row));
    return true;
}

bool BotScheduleStore::create(const BotSchedulePatch& patch, int64_t now_s, BotSchedule& out, std::string& error) {
    if (rows_.size() >= kMaxBotSchedules) {
        error = "too_many";
        return false;
    }
    BotSchedule row;
    row.name = patch.name;
    row.on = patch.has_on ? patch.on : true;
    row.kind = patch.kind;
    row.minutes = patch.minutes;
    row.times = patch.times;
    row.what = patch.what;
    row.created_at = now_s;
    row.next_run = bot_schedule_planned_next(row, now_s);
    int64_t id = 0;
    if (!write_row(row, true, id)) {
        error = "db";
        return false;
    }
    row.id = id;
    rows_.push_back(row);
    out = row;
    return true;
}

bool BotScheduleStore::update(int64_t id, const BotSchedulePatch& patch, int64_t now_s, BotSchedule& out, std::string& error) {
    for (BotSchedule& row : rows_) {
        if (row.id != id) continue;
        BotSchedule next = row;
        if (patch.has_name) next.name = patch.name;
        if (patch.has_when) {
            next.kind = patch.kind;
            next.minutes = patch.minutes;
            next.times = patch.times;
        }
        if (patch.has_what) next.what = patch.what;
        const bool was_on = row.on;
        if (patch.has_on) next.on = patch.on;
        if (next.on && (!was_on || patch.has_when)) next.next_run = bot_schedule_planned_next(next, now_s);       // (turned on, or its time changed: planned again; a change of the name or the choices keeps the next run)
        if (!next.on) next.next_run = 0;
        int64_t unused = 0;
        if (!write_row(next, false, unused)) {
            error = "db";
            return false;
        }
        row = next;
        out = row;
        return true;
    }
    error = "gone";
    return false;
}

bool BotScheduleStore::remove(int64_t id, std::string& error) {
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id != id) continue;
        if (database_on()) {
            db::Statement st;
            if (!db_->prepare(st, "DELETE FROM bot_schedules WHERE id = ?").ok() || !st.bind_int64(1, id).ok() || st.step() != db::Statement::Step::Done) {
                error = "db";
                return false;
            }
        }
        rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
    }
    error = "gone";
    return false;
}

void BotScheduleStore::note_run(int64_t id, int64_t last_run_s, int64_t next_run_s) {
    for (BotSchedule& row : rows_) {
        if (row.id != id) continue;
        row.last_run = last_run_s;
        row.next_run = next_run_s;
        int64_t unused = 0;
        if (!write_row(row, false, unused) && notices_.size() < kNoticesMax) notices_.push_back("bot schedules: the database refused a run's time");
        return;
    }
}

}  // namespace ants::server
