#pragma once

// The match history of the game server (docs/REPLAYS.md "The match history"): what every match came to, kept for good in the server's SQLite database (docs/SERVER.md "The database"). The recording itself (the
// .antsrep file of replay_store.hpp) is deleted after 30 days; the record (history_record.hpp: who won, the scores or kills, who killed and lost how many, the ants hatched, the bombs and fires, the names that
// were typed) stays.
//
// The numbers are not counted while the match is played. A match is played once more with no screen when it is over (HistoryFeeder, below): replay::play() runs the recorded file on the engine and the engine's
// own MatchResult says what each seat came to.
//
// The tables (migrations 2 and 3, src/ants_server/db_schema.cpp): `history_matches` (one row per match, a column for every field the list filters or sorts by, an index for each), `history_seats` (one row per
// seat) and `history_markers` (the matches that the owner deleted while their recording is still kept, so that they are not counted again) and `map_plays` (how many times each map was played: put() adds one in the transaction that keeps a
// new match that HistoryRecord::counts_as_map_play, so a match is counted once, however often the server restarts, and a match that is kept again, or that the owner took out, is not counted twice). A request's words never reach the SQL text: the text of a statement
// is made of fixed pieces, and every value is a bound parameter.
//
// One thread: the server's main loop (SQLite is built without mutexes). The store never throws and never stops the server: the first database error is logged once and the store is UNAVAILABLE (the routes answer
// 503, the feeder waits); it tries again after kRetryMs, and says so in the log when it works again. The match path does not use the store at all.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <unordered_set>
#include <string>
#include <vector>

#include "ants_db/database.hpp"
#include "ants_server/history_record.hpp"
#include "ants_server/replay_store.hpp"

namespace ants::server {

/// Whether the recording of a match can still be watched: `watch` (the file is kept and this build plays it), `old` (kept, recorded by another simulation: it cannot be played here), `removed` (gone)
enum class Recording : uint8_t { Watch, Old, Removed };
const char* recording_name(Recording r) noexcept;

/// True when this build plays the recording that `entry` describes exactly as it was played (its rules number is the one of its game mode: replay::plays_here, for an index entry)
bool entry_plays_here(const ReplayEntry& entry) noexcept;

/// What a list asks for (`GET /history`): the controls of the page's list
struct HistoryQuery {
    enum class Sort : uint8_t { New, Old, Score, Kills, Hatched, Long, Short };
    enum class Format : uint8_t { All, OneVsOne, Teams, Free };            // all, 1v1, 2v1 and 2v2, ffa3 and ffa4
    enum class Result : uint8_t { All, Decided, Draw, Cut };               // cut: a player left, or the time ran out
    enum class Who : uint8_t { Anyone, People, Computers };
    enum class Rec : uint8_t { All, Watch, Removed, Old };
    enum class Mode : uint8_t { All, HighestScore, Kills187 };
    static constexpr size_t kMaxLimit = 100;
    static constexpr size_t kDefaultLimit = 50;
    size_t limit{kDefaultLimit};                     // 1 - kMaxLimit (a larger value is cut to it)
    size_t offset{0};
    Sort sort{Sort::New};
    Format format{Format::All};
    Result result{Result::All};
    Who who{Who::Anyone};
    Rec rec{Rec::All};
    Mode mode{Mode::All};
    int colour{-1};                                  // a seat that took part (0 - 3), -1 for any
    std::string text;                                // part of a map's name or of a name that a seat has (letters of any case)
};

struct HistoryPage {
    /// Why a list could not be made: Unavailable (the database did not answer: the store is unavailable now), TooHeavy (the query ran into its time box: narrow the search), Busy (the public door's budget
    /// of query time is used up: nothing was asked of the database)
    enum class Fail : uint8_t { None, Unavailable, TooHeavy, Busy };
    bool ok{true};                                   // false: see `fail`
    Fail fail{Fail::None};
    size_t count{0};                                 // the records that fit the filters
    std::vector<HistoryRecord> items;                // the page asked for
};

/// How many times a map was played (one row of map_plays). The key is the map's file name and its hash, as Community-Maps/maps.json names a map ("file" and "hash").
struct MapPlays {
    std::string map;                                 // the file name, "TREASURE.LVL"
    std::string hash;                                // FNV-1a 64 of the file, 16 lower-case hex digits
    int64_t plays{0};
    int64_t last_played{0};                          // the end of the newest match that counted (seconds since 1970 UTC)
};

/// The hash as map_plays and maps.json write it: 16 lower-case hex digits
std::string map_hash_text(uint64_t hash);

class HistoryStore {
public:
    static constexpr uint32_t kRetryMs = 60u * 1000u;       // an unavailable store looks again after this long
    static constexpr size_t kMaxNoteLines = 200;            // notes that nobody took are dropped past this (the server's loop takes them every pass)
    /// Longest `search` text of a match (the map's name and the shown names of its seats): the column of the list's q
    static constexpr size_t kMaxSearchBytes = 512;

    /// A list is asked for by the public door (a visitor: a small time box and a share of the server's budget), by the public door with NO query at all (PublicPlain: the page's own list, a fraction of a
    /// millisecond and kept by the site's nginx for five seconds: it has the visitor's box but is not held back by the budget, so that a flood of heavy searches cannot lock the plain list out) or by the owner
    /// (a larger box, no budget)
    enum class Door : uint8_t { Public, PublicPlain, Owner };
    /// The time box of one list: the database stops a query that runs longer (the answer is TooHeavy). A query runs on the server's main thread, so a visitor's query is boxed small.
    static constexpr uint32_t kPublicBoxMs = 15;
    static constexpr uint32_t kOwnerBoxMs = 100;
    /// The budget of the public door's queries, a token bucket of milliseconds of query time: it holds kBudgetBurstMs at the most, earns kBudgetPerSecondMs for each second, and a query is paid for by the
    /// time it took. When it is empty the answer is Busy at once. So the public door can take at most about 5 % of the main thread, whatever the number of visitors.
    static constexpr uint32_t kBudgetBurstMs = 100;
    static constexpr uint32_t kBudgetPerSecondMs = 50;
    static constexpr int kProgressOps = 200;                // the database looks at the clock about every this many steps of a statement

    struct Config {
        std::function<int64_t()> clock_s;                   // seconds since 1970 UTC (empty: the system clock); the time a marker was made
        std::function<uint64_t()> clock_us;                 // a steady clock in microseconds (empty: std::chrono::steady_clock); the time box and the budget (the tests make it deterministic)
    };

    /// `database` must stay open and outlive the store (the server's main keeps it for its whole run); it must have the schema of migration 3.
    explicit HistoryStore(db::Database& database, Config config = Config());

    /// Looks at the tables once (they are made by the migrations) and counts the records. False, with the reason, when the database cannot be read: the caller keeps no history then.
    bool prepare(std::string& why);

    /// True while the database answers; false after an error, until a retry works (maintain)
    bool available() const noexcept { return up_; }
    /// Why it is not (for the owner: SQLite's own words), "" while it is
    const std::string& unavailable_reason() const noexcept { return reason_; }
    /// An unavailable store tries again once every kRetryMs (a cheap read); `now_ms` is the server's clock. Cheap and safe to call every pass.
    void maintain(uint32_t now_ms);

    /// Keeps a record in one transaction (a record of the same id is replaced; a marker of that id goes). False, with the reason, when the id is not one or the database refuses; a record that the database
    /// refuses as such (a constraint) is not a failure of the database: the store stays available. Any other error makes the store unavailable.
    bool put(const HistoryRecord& record, std::string& why);

    enum class Removal : uint8_t { Done, Missing, Failed };
    /// The owner takes a match out of the history. With `keep_marker` (the recording is still there) a marker stays that stops the match from being counted again, else nothing does. One transaction.
    Removal remove(const std::string& id, bool keep_marker, std::string& why);
    /// Deletes the markers of matches that `still_needed` does not know any more (their recording has gone). Returns how many it deleted.
    size_t prune_markers(const std::function<bool(const std::string& id)>& still_needed);

    /// The play count of every map that was played, ordered by file name and hash (for the list of maps). False, with `out` empty, when the database does not answer (the store is unavailable then).
    bool map_plays(std::vector<MapPlays>& out) const;

    size_t count() const noexcept { return count_; }
    size_t marker_count() const noexcept { return markers_; }
    /// One record by its id (with its seats and details); false when there is none (or the database does not answer)
    bool find(const std::string& id, HistoryRecord& out) const;
    /// True when the match is in the history, or the owner took it out (it must not be counted again). An unavailable store knows everything (nothing is counted while it is down).
    bool knows(const std::string& id) const;
    /// Loads the ids of every record and every marker with ONE statement. False (and `out` empty) when the database did not answer.
    bool known_ids(std::unordered_set<std::string>& out) const;
    /// Removes from `ids` the ones the history knows (a record or a marker), keeping the order: one statement for all (known_ids). False when the database did not answer (`ids` is then empty).
    bool keep_unknown(std::vector<std::string>& ids) const;
    /// The ids of the records that have no `details` yet (matches that were counted before the replay pass kept any), newest first: one statement. False (and `out` empty) when the database did not answer.
    bool ids_without_details(std::vector<std::string>& out) const;
    /// Gives a record its `details` (history_details.hpp) when it has none: one statement, and a record that already has some is left as it is. False, with the reason, when the database refuses or does not answer;
    /// true also when the record is gone or already had details (nothing to do).
    bool set_details(const std::string& id, const std::string& details, std::string& why);

    /// The page of records that fit `query`; `recording` says for each record what became of its recording (a lookup in memory; called only when the query filters by it). The query is time boxed and
    /// the public door's is paid from the budget (see Door); a page that could not be made has `ok` false and says why.
    HistoryPage query(const HistoryQuery& query, const std::function<Recording(const std::string& id)>& recording, Door door = Door::Public) const;

    /// Lines for the server's log (a failure of the database, the way back), once each
    std::vector<std::string> take_notes();
    void report(const std::string& line) { note(line); }

private:
    void note(const std::string& line) const;
    /// Records an error of the database: one line in the log, the store is unavailable from now on
    void fail(const std::string& what, const db::Error& error) const;
    db::Error refresh_counts() const;
    int64_t now_s() const;
    uint64_t now_us() const;
    static int time_box_expired(void* self);

    db::Database& db_;
    Config cfg_;
    mutable bool up_{false};
    mutable std::string reason_;
    mutable size_t count_{0};
    mutable size_t markers_{0};
    mutable bool retry_armed_{false};
    mutable uint32_t retry_at_ms_{0};
    mutable std::vector<std::string> notes_;
    mutable uint64_t deadline_us_{0};                // the end of the query that runs now
    mutable bool budget_armed_{false};
    mutable int64_t budget_us_{0};                   // the public door's budget (microseconds of query time left)
    mutable uint64_t budget_at_us_{0};
};

/// Counts the matches that the replay store keeps and the history does not know yet, one at a time. A file is played again with no screen (0.4 - 0.5 s for a match of 10 - 12 minutes, more for a longer one) and its numbers go to the
/// history in one transaction, with the details that the same play counted (match_stats.hpp, history_details.hpp). When no new file is left the records that were counted before the pass kept details, and whose
/// recording is still kept and plays here, get theirs the same way, newest first (a recording that cannot be counted gets the marker, so that it is not tried again). The work is never done on the server's loop in a burst: one file at most per call, the gap after it at least kPaceMs and kPaceFactor times what the file took, and only when the caller says the server is idle (no room
/// runs or restores a match). The files that the replay store holds when the server starts (up to 30 days of them) are counted the same way, the newest first, and so are the matches that arrive later (the
/// room's own, and the files that the bot arena puts in the folder). A file that cannot be counted (another simulation's rules, a map that is not here, a file that does not play out the same here) is left out
/// with one line in the log, and tried again only when the server starts again. While the store is unavailable nothing is played.
class HistoryFeeder {
public:
    static constexpr uint32_t kPaceMs = 250;          // the least gap between two recordings
    static constexpr uint32_t kPaceFactor = 4;        // ... and at least this many times the time that the last one took (a long match is paid for with a longer gap: the main thread is mostly free)
    static constexpr size_t kMaxSkipLines = 10;       // the lines about files that were left out, one each; the rest are counted in the summary
    static constexpr uint32_t kPruneEveryMs = 3600u * 1000u;

    /// `maps_dir` is the folder that replay::load_map looks for the match's map in, then `community_dir` (the players' maps: a match on one of them is counted too; empty: none). The three objects must outlive
    /// the feeder. `clock_us` is a steady clock in microseconds that times the work of a pass (empty: std::chrono::steady_clock; the tests inject one).
    HistoryFeeder(HistoryStore& history, const ReplayStore& replays, std::string maps_dir, std::function<uint64_t()> clock_us = std::function<uint64_t()>(), std::string community_dir = std::string());

    /// One piece of work: at most one file. `idle` is asked only when a file is due (a quiet server: no room runs a match). True when a file was played (counted or left out).
    bool step(const std::function<bool()>& idle, uint32_t now_ms);

    size_t pending() const noexcept { return queue_.size() + backfill_.size(); }
    size_t counted() const noexcept { return counted_; }
    size_t left_out() const noexcept { return skipped_.size(); }
    /// Records that were counted before the replay pass kept any details and have them now (counted again from their recording, which is still kept)
    size_t backfilled() const noexcept { return backfilled_; }

private:
    void rebuild();
    bool work(const std::string& file);
    void build_backfill();
    bool backfill(const std::string& file);
    void skip(const std::string& id, const std::string& why);

    HistoryStore& history_;
    const ReplayStore& replays_;
    std::vector<std::string> map_dirs_;               // the folders of the maps, asked in order
    std::function<uint64_t()> clock_us_;
    uint64_t seen_revision_{~uint64_t{0}};
    std::vector<std::string> queue_;                  // file names, the next one at the back (the newest are counted first)
    std::set<std::string> skipped_;                   // ids that were left out since the server started
    size_t other_rules_{0};                           // files of the replay store that another build's rules made: never queued
    size_t counted_{0};                               // records made since the server started
    std::vector<std::string> backfill_;               // files of the counted matches that have no details yet, the next one at the back (the newest first); built once, when the new files are done
    bool backfill_built_{false};
    size_t backfilled_{0};
    size_t reported_backfilled_{0};
    size_t reported_counted_{0};
    size_t reported_skipped_{0};
    size_t skip_lines_{0};
    bool paced_{false};
    uint32_t next_ms_{0};
    bool prune_armed_{false};
    uint32_t next_prune_ms_{0};
};

}  // namespace ants::server
