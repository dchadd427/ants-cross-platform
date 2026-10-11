#pragma once

// The match history of the game server (docs/REPLAYS.md "The match history"): what every match came to, kept for good in the server's SQLite database (docs/SERVER.md "The database"). The recording itself (the
// .antsrep file of replay_store.hpp) is deleted after 30 days; the record (history_record.hpp: who won, the scores or kills, who killed and lost how many, the ants hatched, the bombs and fires, the names that
// were typed) stays.
//
// The numbers are not counted while the match is played. A match is played once more with no screen when it is over (HistoryFeeder, below): replay::play() runs the recorded file on the engine and the engine's
// own MatchResult says what each seat came to.
//
// The tables (migration 2, src/ants_server/db_schema.cpp): `history_matches` (one row per match, a column for every field the list filters or sorts by, an index for each), `history_seats` (one row per
// seat) and `history_markers` (the matches that the owner deleted while their recording is still kept, so that they are not counted again). A request's words never reach the SQL text: the text of a statement
// is made of fixed pieces, and every value is a bound parameter.
//
// One thread: the server's main loop (SQLite is built without mutexes). The store never throws and never stops the server: the first database error is logged once and the store is UNAVAILABLE (the routes answer
// 503, the feeder waits); it tries again after kRetryMs, and says so in the log when it works again. The match path does not use the store at all.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
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
    bool ok{true};                                   // false: the database did not answer (the store is unavailable now)
    size_t count{0};                                 // the records that fit the filters
    std::vector<HistoryRecord> items;                // the page asked for
};

class HistoryStore {
public:
    static constexpr uint32_t kRetryMs = 60u * 1000u;       // an unavailable store looks again after this long
    static constexpr size_t kMaxNoteLines = 200;            // notes that nobody took are dropped past this (the server's loop takes them every pass)
    /// Longest `search` text of a match (the map's name and the shown names of its seats): the column of the list's q
    static constexpr size_t kMaxSearchBytes = 512;

    struct Config {
        std::function<int64_t()> clock_s;                   // seconds since 1970 UTC (empty: the system clock); the time a marker was made
    };

    /// `database` must stay open and outlive the store (the server's main keeps it for its whole run); it must have the schema of migration 2.
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

    size_t count() const noexcept { return count_; }
    size_t marker_count() const noexcept { return markers_; }
    /// One record by its id (with its seats and details); false when there is none (or the database does not answer)
    bool find(const std::string& id, HistoryRecord& out) const;
    /// True when the match is in the history, or the owner took it out (it must not be counted again). An unavailable store knows everything (nothing is counted while it is down).
    bool knows(const std::string& id) const;
    /// Removes from `ids` the ones the history knows (a record or a marker), keeping the order: one statement for all. False when the database did not answer (`ids` is then empty).
    bool keep_unknown(std::vector<std::string>& ids) const;

    /// The page of records that fit `query`; `recording` says for each record what became of its recording (called only when the query filters by it)
    HistoryPage query(const HistoryQuery& query, const std::function<Recording(const std::string& id)>& recording) const;

    /// Lines for the server's log (a failure of the database, the way back), once each
    std::vector<std::string> take_notes();
    void report(const std::string& line) { note(line); }

private:
    void note(const std::string& line) const;
    /// Records an error of the database: one line in the log, the store is unavailable from now on
    void fail(const std::string& what, const db::Error& error) const;
    db::Error refresh_counts() const;
    int64_t now_s() const;

    db::Database& db_;
    Config cfg_;
    mutable bool up_{false};
    mutable std::string reason_;
    mutable size_t count_{0};
    mutable size_t markers_{0};
    mutable bool retry_armed_{false};
    mutable uint32_t retry_at_ms_{0};
    mutable std::vector<std::string> notes_;
};

/// Counts the matches that the replay store keeps and the history does not know yet, one at a time. A file is played again (about a tenth of a second to a third) with no screen and its numbers go to the
/// history in one transaction. The work is never done on the server's loop in a burst: one file at most per call, at least kPaceMs after the last one, and only when the caller says the server is idle (no room
/// runs or restores a match). The files that the replay store holds when the server starts (up to 30 days of them) are counted the same way, the newest first, and so are the matches that arrive later (the
/// room's own, and the files that the bot arena puts in the folder). A file that cannot be counted (another simulation's rules, a map that is not here, a file that does not play out the same here) is left out
/// with one line in the log, and tried again only when the server starts again. While the store is unavailable nothing is played.
class HistoryFeeder {
public:
    static constexpr uint32_t kPaceMs = 250;
    static constexpr size_t kMaxSkipLines = 10;       // the lines about files that were left out, one each; the rest are counted in the summary
    static constexpr uint32_t kPruneEveryMs = 3600u * 1000u;

    /// `maps_dir` is the folder that replay::load_map looks for the match's map in. The three objects must outlive the feeder.
    HistoryFeeder(HistoryStore& history, const ReplayStore& replays, std::string maps_dir);

    /// One piece of work: at most one file. `idle` is asked only when a file is due (a quiet server: no room runs a match). True when a file was played (counted or left out).
    bool step(const std::function<bool()>& idle, uint32_t now_ms);

    size_t pending() const noexcept { return queue_.size(); }
    size_t counted() const noexcept { return counted_; }
    size_t left_out() const noexcept { return skipped_.size(); }

private:
    void rebuild();
    bool work(const std::string& file);
    void skip(const std::string& id, const std::string& why);

    HistoryStore& history_;
    const ReplayStore& replays_;
    std::string maps_dir_;
    uint64_t seen_revision_{~uint64_t{0}};
    std::vector<std::string> queue_;                  // file names, the next one at the back (the newest are counted first)
    std::set<std::string> skipped_;                   // ids that were left out since the server started
    size_t other_rules_{0};                           // files of the replay store that another build's rules made: never queued
    size_t counted_{0};                               // records made since the server started
    size_t reported_counted_{0};
    size_t reported_skipped_{0};
    size_t skip_lines_{0};
    bool paced_{false};
    uint32_t next_ms_{0};
    bool prune_armed_{false};
    uint32_t next_prune_ms_{0};
};

}  // namespace ants::server
