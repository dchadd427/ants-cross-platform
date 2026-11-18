#pragma once

// The schedules of the server's own matches of computer players (docs/SERVER_REPLAYS.md, "Schedules"): what the admin page edits and BotMatches (bot_matches.hpp) runs. A schedule says when (every N minutes, or at times of the
// day by the Pacific clock, pacific_time.hpp), what (the choices of Start now, bot_match_spec.hpp), and whether it is on. The rows are kept in the server's database (table bot_schedules, migration 9) and in memory; the database is
// the copy that a restart reads back. A server without a database keeps them in memory only. One thread, like everything else of the server.
//
// When a schedule runs next:
//  * every N minutes (5 - 1440) counts from the schedule's last start, as the server's old timer did: a run is due N minutes after the last one was made (or was due and refused). A restart does not make one at once: the next run
//    of a schedule that is on is at the latest of the time that was kept and a minute from now (a server that has been down starts its first match a minute after it is up). A schedule that is saved or turned on counts from
//    its last start (from now when it never started), and never starts inside the request that saved it: the first run is a minute away at the soonest;
//  * at times of the day: the next time of the list by the Pacific clock after now, on the day of the clock change too (pacific_time.hpp).
// A run that is due while a limit holds it back (the matches that go, the places, the matches of people) is skipped, not queued: the next run is the next one by the rule above. A run that was due while the server was down is not made up.
// A time that cannot be true is mended when the rows are read and once a minute: a last run that is more than kClockSlackS after now is ignored (as if the schedule never ran), and a next run that is later than the schedule's period
// from now (every N minutes: N minutes; at times: the next time of the list) is brought back to it, so that a clock that was far ahead once, or a damaged row, cannot keep a schedule away for good.

#include <cstdint>
#include <string>
#include <vector>

#include "ants_ctl/json.hpp"
#include "ants_server/bot_match_spec.hpp"

namespace ants::db {
class Database;
}

namespace ants::server {

struct BotSchedule {
    enum class Kind : uint8_t { Every, Times };
    int64_t id{0};
    std::string name;                                 // typed by an admin: 1 - 40 characters, no control or direction characters
    bool on{true};
    bool is_default{false};                           // the schedule that the server made from --bot-match-every-min when it first had a table (the page tags it)
    Kind kind{Kind::Every};
    int minutes{60};                                  // Every: 5 - 1440
    std::vector<int> times;                           // Times: minutes since midnight by the Pacific clock, ascending, no repeat, 1 - kMaxTimes
    BotWhat what;
    int64_t last_run{0};                              // the second (UTC) of the last start that made a match (0: none yet)
    int64_t next_run{0};                              // the second of the next run (0: none: the schedule is off)
    int64_t created_at{0};
};

/// What a change of a schedule says; only what `has_*` marks is changed
struct BotSchedulePatch {
    bool has_name{false};
    std::string name;
    bool has_on{false};
    bool on{true};
    bool has_when{false};
    BotSchedule::Kind kind{BotSchedule::Kind::Every};
    int minutes{60};
    std::vector<int> times;
    bool has_what{false};
    BotWhat what;
};

constexpr size_t kMaxBotSchedules = 20;
constexpr size_t kMaxBotScheduleTimes = 12;
constexpr int kMinBotEveryMinutes = 5;
constexpr int kMaxBotEveryMinutes = 1440;
constexpr int64_t kClockSlackS = 3600;               // a last run more than this far after now was never made
constexpr int64_t kSameScheduleWindowS = 10;         // a schedule that is saved again within this many seconds with all the same choices is the same save (a double click), not a second schedule

/// The name as it is kept: control, invisible and direction characters taken out, the blanks at the ends cut, runs of blanks made one; false when it is not UTF-8 or has no characters or more than 40 left
bool clean_schedule_name(const std::string& in, std::string& out);
/// "18:30" to minutes since midnight (a 24-hour time: one or two digits of the hour, two of the minute); false for anything else
bool parse_clock_time(const std::string& text, int& minutes);
std::string clock_time_text(int minutes);
/// Reads the body of a change of a schedule (create: the name, when and what are all needed; update: any of them). False with the error code the page is told (`bad_name`, `bad_on`, `bad_every`, `no_times`, `bad_times`,
/// `bad_when`, and the codes of parse_bot_what).
bool parse_schedule_patch(const ctl::JsonValue& body, bool create, BotSchedulePatch& out, std::string& error);

/// When a schedule that is saved, turned on or changed first runs (see the top), and when it runs after it ran at `ran_s`, and when it runs again after a restart at `now_s`. 0 for a schedule that is off.
int64_t bot_schedule_planned_next(const BotSchedule& schedule, int64_t now_s);
int64_t bot_schedule_next_after_run(const BotSchedule& schedule, int64_t ran_s);
int64_t bot_schedule_resumed_next(const BotSchedule& schedule, int64_t now_s);
/// Brings the two times of a schedule that is on back to what can be true at `now_s` (see the top). True when something was changed.
bool bot_schedule_repair_clock(BotSchedule& schedule, int64_t now_s);
/// Whether two schedules are the same in everything that an admin chooses (the name, whether it is on, when and what), not in the times the server keeps
bool bot_schedule_same_choices(const BotSchedule& a, const BotSchedule& b);

class BotScheduleStore {
public:
    /// `database` may be null: the rows then live in memory only (a server without a database still has the schedule that --bot-match-every-min makes)
    explicit BotScheduleStore(db::Database* database);

    /// Reads the rows (a row that cannot be read is left out and told in the notices) and sets each next run by the rule of a restart. False with a sentence when the table cannot be read.
    bool open(int64_t now_s, std::string& why);
    const std::vector<BotSchedule>& list() const noexcept { return rows_; }
    const BotSchedule* find(int64_t id) const noexcept;

    /// The first start of a server with the default schedule: when the table has no rows and the database has never made the default (a row of `meta`), makes "Hourly rotation" from --bot-match-every-min (5 - 1440 minutes,
    /// every choice drawn: the match the old timer made) and remembers that it did, so that a schedule that was deleted is not made again. Without a database it is made at every start. False when nothing was made.
    bool seed_default(int every_minutes, int64_t now_s);

    /// A new schedule (the patch holds all of it). False with the error code: `too_many`, `db`. A schedule that is the same in every choice as the newest one, made within kSameScheduleWindowS, is not made again: `out` is
    /// the one that is there and `repeated` (when asked) is true.
    bool create(const BotSchedulePatch& patch, int64_t now_s, BotSchedule& out, std::string& error, bool* repeated = nullptr);
    /// A change. False with the error code: `gone`, `db`. A schedule that is turned on, or whose time changes, has its next run planned again. `changed` (when asked) says whether anything an admin chooses is different
    /// now; a change that changes nothing writes nothing and keeps the next run.
    bool update(int64_t id, const BotSchedulePatch& patch, int64_t now_s, BotSchedule& out, std::string& error, bool* changed = nullptr);
    bool remove(int64_t id, std::string& error);
    /// Mends the times that cannot be true (bot_schedule_repair_clock) in every row, once a minute; the rows that changed are written. Returns how many rows changed.
    size_t repair_clock(int64_t now_s);
    /// A due run was made (or skipped): its time and the next one are kept
    void note_run(int64_t id, int64_t last_run_s, int64_t next_run_s);

    std::vector<std::string> take_notices();

private:
    bool write_row(const BotSchedule& row, bool insert, int64_t& new_id);
    bool database_on() const;

    db::Database* db_;
    std::vector<BotSchedule> rows_;
    std::vector<std::string> notices_;
    int64_t last_id_{0};                              // the highest number that a schedule had in this run of the server: a number is not used again, with a database or without
};

}  // namespace ants::server
