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
// One thread for each store object (a connection is not shared between threads). The game server uses one, on its main loop; the history service (docs/HISTORY_SERVICE.md) has two connections to its file, a writer on
// the counter thread and a reader on the HTTP thread, each with a store of its own. The store never throws and never stops the server: the first database error is logged once and the store is UNAVAILABLE (the routes answer
// 503, the feeder waits); it tries again after Config::retry_ms (kRetryMs by default), and says so in the log when it works again. The match path does not use the store at all.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ants_db/database.hpp"
#include "ants_replay/match_stats.hpp"
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
    int color{-1};                                  // a seat that took part (0 - 3), -1 for any
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
        uint32_t retry_ms{kRetryMs};                        // how long an unavailable store waits before it looks again (the game server's own loop keeps the default; the history service's counter asks for less)
    };

    /// `database` must stay open and outlive the store (the server's main keeps it for its whole run); it must have the schema of migration 3.
    explicit HistoryStore(db::Database& database);
    HistoryStore(db::Database& database, Config config);

    /// Looks at the tables once (they are made by the migrations) and counts the records. False, with the reason, when the database cannot be read: the caller keeps no history then.
    bool prepare(std::string& why);

    /// True while the database answers; false after an error, until a retry works (maintain)
    bool available() const noexcept { return up_; }
    /// Why it is not (for the owner: SQLite's own words), "" while it is
    const std::string& unavailable_reason() const noexcept { return reason_; }
    /// An unavailable store tries again once every Config::retry_ms (a cheap read); `now_ms` is the server's clock. Cheap and safe to call every pass.
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

    /// The sum of the maps of deaths of the matches on one map (GET /history/deaths?map=NAME, docs/HEATMAPS.md "The map of deaths"): `name` is looked up among the names of the maps that have a map of deaths here
    /// (compared without regard to the case of an ASCII letter, so a name a browser or a file system folded finds the same map), never used as a path or a pattern; the matches are those of the newest map file
    /// of that name (its hash and size), at most the newest kDeathsSumMatches of them. `json` is the answer (history_deaths.hpp add_up_deaths); it is made once for each state of the history (cached, until a match
    /// is kept or taken out) and for at most kDeathsSumCache maps. Unknown: no map of that name has a map of deaths. Unavailable: the database does not answer.
    enum class DeathsSum : uint8_t { Ok, Unknown, Unavailable };
    static constexpr size_t kDeathsSumMatches = 2000;
    static constexpr size_t kDeathsSumCache = 48;
    DeathsSum map_deaths(const std::string& name, std::string& json) const;
    size_t deaths_cached() const noexcept { return deaths_cache_.size(); }          // (the sums that are kept now: for a test, never more than kDeathsSumCache)

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

    /// For a store that only READS a file that another connection writes (the history service's HTTP side): begin_read() opens a read transaction, so that everything one request asks is from one state of the file, and
    /// looks whether another connection committed since the last look (PRAGMA data_version): if so, the counts are read again and the sums of the maps of deaths are made again. False when the store is unavailable (or the
    /// database did not answer). end_read() closes it. A store that writes its file itself (the game server's, the service's counter) does not need them.
    bool begin_read() const;
    void end_read() const;
    class ReadScope {
    public:
        explicit ReadScope(const HistoryStore& store) : store_(store), open_(store.begin_read()) {}
        ~ReadScope() {
            if (open_) store_.end_read();
        }
        ReadScope(const ReadScope&) = delete;
        ReadScope& operator=(const ReadScope&) = delete;

    private:
        const HistoryStore& store_;
        bool open_;
    };

    /// The store's clock, seconds since 1970 UTC (Config::clock_s): the feeder judges which recordings are fresh by it
    int64_t clock_seconds() const { return now_s(); }

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
    mutable uint64_t deaths_revision_{0};            // goes up whenever a match is kept or taken out: the sums of the maps below are made again then
    mutable uint64_t deaths_cached_at_{~uint64_t{0}};
    mutable std::vector<std::string> deaths_names_;  // the maps that have a map of deaths, sorted
    mutable std::unordered_map<std::string, std::string> deaths_cache_;
    mutable bool reading_{false};                    // a read transaction is open (begin_read)
    mutable bool version_seen_{false};
    mutable int64_t data_version_{0};                // PRAGMA data_version at the last begin_read()
};

/// Counts the matches that the replay store keeps and the history does not know yet, one at a time. A file is played again with no screen (0.4 - 0.5 s for a match of 10 - 12 minutes, more for a longer one) and its numbers go to the
/// history in one transaction, with the details that the same play counted (match_stats.hpp, history_details.hpp). When no new file is left the records that were counted before the pass kept details, and whose
/// recording is still kept and plays here, get theirs the same way, newest first (a recording that is read and cannot be played gets the marker, so that it is not tried again; one that cannot be tried, its map not being here, gets nothing and is tried again at the next start). The files that the replay store holds when the server starts (up to 30 days of them) are counted the same way, the newest first, and so are the matches that arrive later (the
/// room's own, and the files that the bot arena puts in the folder). A file that cannot be counted (another simulation's rules, a map that is not here, a file that does not play out the same here) is left out
/// with one line in the log, and tried again only when the server starts again (a file that was left out because its map was not there is tried again when the caller says that the maps may have changed:
/// retry_skipped_maps()). While the store is unavailable nothing is played.
///
/// Two ways to work, chosen by set_slice(). Whole (the default): a recording is played in one call, and only when the caller says the server is idle (no room with a person in it runs or restores a match), the gap after it at least
/// kPaceMs and kPaceFactor times what the file took: the history service's counter, which has a thread of its own, works so. Sliced (the game server's, whose one thread runs every match): the work is cut into steps (the file is read,
/// its map is loaded, the engine is set up, the turns are played a slice at a time, the record is made and kept), each step followed by a gap of kPaceFactor times its length, and a FRESH recording (a match with a person in it that ended
/// less than kFreshWindowS ago: the match that was just played) is worked at any time, `idle` is not asked for it, so that it is in the history within seconds whatever runs; a match of computer players alone, a file of the bot
/// arena, the older recordings and the backfill are still worked only while `idle` says yes, and one that a person's match stops part-way waits where it is and goes on first when the server is quiet again.
class HistoryFeeder {
public:
    static constexpr uint32_t kPaceMs = 250;          // the least gap between two recordings
    static constexpr uint32_t kPaceFactor = 4;        // ... and at least this many times the time that the last one took (a long match is paid for with a longer gap: the main thread is mostly free)
    static constexpr size_t kMaxSkipLines = 10;       // the lines about files that were left out, one each; the rest are counted in the summary
    static constexpr uint32_t kPruneEveryMs = 3600u * 1000u;
    static constexpr uint32_t kSliceUs = 2000;        // a sliced feeder's step plays turns for this long and stops after the turn that crosses it (the clock is looked at after every turn); a turn that takes the state hash starts a step of its own, so a step is as long as one turn and its hash where that is dear: 2 - 5 ms on a 256 x 256 map, 0.03 ms on a shipped map
    static constexpr uint32_t kSliceGapMs = 2;        // ... and the gap between two steps of one recording is at least this long (and kPaceFactor times the step)
    static constexpr int64_t kFreshWindowS = 3600;    // a recording that ended less than this long ago is fresh (the older ones are the pile that a start finds, and wait for a quiet server)

    /// `maps_dir` is the folder that replay::load_map looks for the match's map in, then `community_dir` (the players' maps: a match on one of them is counted too; empty: none). The three objects must outlive
    /// the feeder. `clock_us` is a steady clock in microseconds that times the work of a pass (empty: std::chrono::steady_clock; the tests inject one).
    HistoryFeeder(HistoryStore& history, const ReplayStore& replays, std::string maps_dir, std::function<uint64_t()> clock_us = std::function<uint64_t()>(), std::string community_dir = std::string());
    ~HistoryFeeder();
    HistoryFeeder(const HistoryFeeder&) = delete;
    HistoryFeeder& operator=(const HistoryFeeder&) = delete;

    /// The file of an approved map of the players that a recording was played on, by the name in its head: "" when the name is none of them (the maps folders are asked then). A match on an approved map is counted
    /// (its plays, its stats) like any other; the server gives only the maps that a room plays (OnlineLimits), because a room plays no other. Unset: no approved map is known.
    using ApprovedMapPath = std::function<std::string(const std::string& map_name)>;
    void set_approved_map_path(ApprovedMapPath fn) { approved_path_ = std::move(fn); }

    /// The pace: the least gap after a recording and how many times what it took (the defaults are kPaceMs and kPaceFactor, made for a server whose one thread must stay free). The history service, whose counter has a thread
    /// of its own, asks for less.
    void set_pace(uint32_t min_gap_ms, uint32_t factor) {
        pace_ms_ = min_gap_ms;
        pace_factor_ = factor;
    }

    /// Sliced work (see the class): a step works `budget_us` microseconds at the most (kSliceUs for the game server). 0, the default, is whole work. Set it before the first step().
    void set_slice(uint32_t budget_us) { slice_us_ = budget_us; }

    /// Whether the feeder deletes the markers whose recording has gone (once an hour; the default). A marker keeps a match that the operator took out from being counted again while its recording is still kept. The program that
    /// owns the recordings folder (the game server) deletes them, because it knows that its folder is the right one; a program that only reads the folder (the history service) must not: a folder that is empty or is not the
    /// right one would make every marker useless, and the matches the operator took out would be counted and shown again.
    void set_prune_markers(bool on) { prune_markers_ = on; }

    /// Puts the recordings that were left out because their map could not be found (not in the folders, not an approved map, or an approved map that could not be looked up) back in the line: the caller says that the
    /// list of approved maps has changed or can be read again. A recording left out for another reason (damaged, another simulation's rules, a play that did not end alike) is not tried again. The number put back.
    size_t retry_skipped_maps();
    /// How many recordings wait to be tried again by retry_skipped_maps()
    size_t left_out_for_map() const noexcept { return map_skipped_.size(); }

    /// Makes the `details` of a counted match (make_history_details by default); a test hands a maker that throws, to see that the counters of a match are written whatever the pass does
    using DetailsMaker = std::function<std::string(const replay::MatchStats&, const HistoryRecord&)>;
    void set_details_maker(DetailsMaker maker) { details_maker_ = std::move(maker); }

    /// Makes the map of deaths of a counted match (make_history_deaths by default; only for a match counted here, never for an older record: those have none, by decision). A test hands a maker that throws, to see
    /// that the counters and the details of a match are written whatever it does.
    using DeathsMaker = std::function<std::string(const replay::MatchStats&)>;
    void set_deaths_maker(DeathsMaker maker) { deaths_maker_ = std::move(maker); }

    /// Makes the action of a counted match, for the activity curve of the replay page (make_history_action by default; only for a match counted here: an older record has none, the page draws its deaths alone). A test hands a
    /// maker that throws, to see that the counters, the details and the map of deaths are written whatever it does.
    using ActionMaker = std::function<std::string(const replay::MatchStats&)>;
    void set_action_maker(ActionMaker maker) { action_maker_ = std::move(maker); }

    /// One piece of work: at most one file (whole work) or one step of one file (sliced work). `idle` is asked only when work that waits for a quiet server is due (no room with a person in it runs a match); a fresh recording
    /// of a sliced feeder never asks. True when a file was played to its end (counted or left out).
    bool step(const std::function<bool()>& idle, uint32_t now_ms);

    size_t pending() const noexcept { return queue_.size() + backfill_.size() + fresh_.size() + (job_ ? 1u : 0u) + (paused_ ? 1u : 0u); }
    size_t counted() const noexcept { return counted_; }
    size_t left_out() const noexcept { return skipped_.size(); }
    /// Records that were counted before the replay pass kept any details and have them now (counted again from their recording, which is still kept)
    size_t backfilled() const noexcept { return backfilled_; }

private:
    struct Job;
    /// What one step of a job came to: Go (the job goes on), Played (it is over and a file was counted or left out), Dropped (it is over and nothing was played: the file went, or it was counted already)
    enum class Move : uint8_t { Go, Played, Dropped };
    void rebuild();
    void look_fresh();
    void start_job(const std::string& file, bool fresh, bool backfill);
    bool start_waiting_job(const std::function<bool()>& idle);
    bool run_job(uint32_t now_ms);
    /// What a recording is refused for (refuse() says what becomes of it): Unreadable (the store did not give it), Damaged, Foreign (another simulation's rules), NoMap (its map is missing, is another file of the name or cannot be
    /// loaded), TooBig (its map is over a limit of a room), Engine (the engine refused it)
    enum class Refusal : uint8_t { Unreadable, Damaged, Foreign, NoMap, TooBig, Engine };
    Move refuse(Job& job, Refusal kind, const std::string& why);
    Move read_job(Job& job);
    Move map_job(Job& job);
    Move setup_job(Job& job);
    Move play_job(Job& job);
    Move finish_count(Job& job);
    Move finish_backfill(Job& job);
    Move settle_backfill(Job& job);
    void requeue(const Job& job);
    void build_backfill();
    void skip(const std::string& id, const std::string& why, bool for_map = false);
    bool load_level(const replay::Header& head, assets::LevelData& level, std::string& why, Refusal& kind) const;          // (the map of a recording: an approved map or the folders, within what a room plays)

    ApprovedMapPath approved_path_;
    HistoryStore& history_;
    const ReplayStore& replays_;
    std::vector<std::string> map_dirs_;               // the folders of the maps, asked in order
    DetailsMaker details_maker_;
    DeathsMaker deaths_maker_;
    ActionMaker action_maker_;
    std::function<uint64_t()> clock_us_;
    uint64_t seen_revision_{~uint64_t{0}};
    std::vector<std::string> queue_;                  // file names, the next one at the back (the newest are counted first)
    std::vector<std::string> fresh_;                  // the fresh recordings that wait (a sliced feeder): file names, the next one at the back
    std::set<std::string> fresh_seen_;                // the fresh recordings that were judged (queued, counted, known or left out): not looked at again
    uint64_t fresh_revision_{~uint64_t{0}};           // the revision of the replay store at the last look for fresh recordings
    bool fresh_retry_{false};                         // a recording of the window had no readable head yet: look again at the next step
    std::unique_ptr<Job> job_;                        // the recording that is being worked on (a sliced feeder: between steps)
    std::unique_ptr<Job> paused_;                     // a recording of the pile that a person's match stopped part-way (a sliced feeder): it goes on first when the server is quiet again
    uint32_t slice_us_{0};
    std::set<std::string> skipped_;                   // ids that are left out (since the server started, less those that retry_skipped_maps() put back)
    std::set<std::string> map_skipped_;               // those of them that were left out for want of their map: the ones that retry_skipped_maps() puts back
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
    uint32_t pace_ms_{kPaceMs};
    uint32_t pace_factor_{kPaceFactor};
    uint32_t next_ms_{0};
    bool prune_armed_{false};
    bool prune_markers_{true};
    uint32_t next_prune_ms_{0};
};

}  // namespace ants::server
