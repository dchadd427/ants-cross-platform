#pragma once

// The history service (docs/HISTORY_SERVICE.md): a program of its own that keeps the match history of the game server's recordings and gives it out. It counts every recording that appears in a folder, a few seconds after it
// is there, into a database file of its own, and answers GET /replays, GET /replays/<file>, GET /history, GET /history/<id> and GET /history/deaths with the same JSON, limits and errors as the game server's public door:
// the code that makes the answers is the game server's (control.hpp public_history_service_response), and the code that counts is the game server's too (HistoryFeeder, HistoryStore, ReplayStore in read-only mode).
//
// Two threads, each with everything it uses of its own, so that nothing is shared in memory:
//   - the COUNTER thread owns the writer connection to the service's database, a replay store that looks at the recordings folder, the feeder, and (with a game database) the list of approved maps. It looks at the folder every
//     look_ms and counts what is new; it is the only writer of the file.
//   - the HTTP thread (the caller of handle()) owns a read-only connection to the same file and a replay store of its own. Every /history answer is made in one read transaction, so a list and its counts are of one state of the file.
// The two meet in the file (SQLite's WAL: a reader never waits for the writer) and in the log callback, which the service serializes. The recordings folder is only read: the service deletes nothing, and it deletes no
// marker of its own file either (the markers of matches that the operator took out stay: a folder that is empty or not the right one must not make a deleted match public again).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ants_ctl/http.hpp"
#include "ants_db/database.hpp"
#include "ants_server/history_store.hpp"
#include "ants_server/replay_store.hpp"

namespace ants::history {

struct ServiceConfig {
    std::string db_path;                              // the service's own SQLite file (made when it is not there)
    std::string recordings_dir;                       // the folder that the game server keeps its recordings in (must be there; only read)
    std::string maps_dir;                             // the original game's maps (Original-Ants/Maps): a recording is played on its map
    std::string community_dir;                        // the players' maps (Community-Maps; empty: none)
    std::string game_db_path;                         // the game server's database file, read only: the history in it is copied at the first start, and its approved maps of the players are looked up (empty: neither)
    std::string submissions_dir;                      // the game server's folder of submitted maps (with game_db_path: the files of the approved maps are in it)
    uint32_t keep_days{30};                           // what the lists say (the game server's --replays-days): the service deletes nothing, the game server's purge does
    uint64_t max_bytes{100ull * 1024ull * 1024ull};   // (the game server's --replays-max-mb: shown to nobody on the public door, kept so that the store is made as the game server's)
    uint32_t settle_s{2};                             // a file that changed less than this many seconds ago is not trusted yet (ReplayConfig::settle_s); the counting is this late at the least
    uint32_t look_ms{250};                            // how often the counter looks at the folder and for work
    uint32_t pace_ms{0};                              // the least gap after a recording (HistoryFeeder::set_pace) ...
    uint32_t pace_factor{1};                          // ... and how many times what it took: the counter has a thread of its own, the game server's loop is not held up
    uint32_t approved_refresh_s{60};                  // how often the approved maps are read again from the game database (while a read has failed, or a recording waits for its map, at most every 5 s of it)
    uint32_t store_retry_ms{5000};                    // how long a store that lost its database waits before it looks again (both threads' stores; the game server's own is 60 s: its loop has the games to run)
    uint32_t http_look_ms{200};                       // the HTTP thread looks at the folder at most this often (once at the start of a request that is due)
    std::function<void(const std::string&)> log;      // lines for the log, from both threads (the service calls it one at a time); empty: none
    std::function<int64_t()> clock_s;                 // the replay stores' clock, seconds since 1970 UTC (empty: the system clock; tests give their own)
    std::function<uint64_t()> work_clock_us;          // the feeder's clock (empty: steady; tests give their own)
    std::function<uint64_t()> history_clock_us;       // the stores' clock for the time box and the budget of a list (empty: steady; tests give their own)
};

/// What the copy of the game server's history did at the start (import_game_history)
struct ImportReport {
    bool imported{false};                             // rows were copied now
    bool already_done{false};                         // an earlier start did it (the marker row of `meta`): nothing is copied
    bool not_empty{false};                            // the service's file held records of its own and no marker: nothing is copied (the play counts cannot be told apart)
    size_t matches{0};
    size_t seats{0};
    size_t markers{0};
    size_t deaths{0};
    size_t action{0};
    size_t map_plays{0};
    std::string note;                                 // one line for the log
};

/// Copies the match history of the game server's database file (`source_path`, opened read only) into `target`, once: the matches, their seats, the markers of matches that the operator took out, the maps of deaths, the actions of the matches and the
/// play counts of the maps, in ONE transaction together with the marker row `history_import` of `meta`. A target that has the marker is left as it is (`already_done`); one that has records and no marker is left as it
/// is too (`not_empty`). False, with `why`, when the source cannot be read (it has no history, is not a database, is this very file) or the copy fails: the target is then as it was. `target` must have the schema of
/// history_db_migrations().
bool import_game_history(db::Database& target, const std::string& source_path, ImportReport& report, std::string& why);

class HistoryService {
public:
    explicit HistoryService(ServiceConfig config);
    ~HistoryService();
    HistoryService(const HistoryService&) = delete;
    HistoryService& operator=(const HistoryService&) = delete;

    /// Checks the folders, opens the database (made when it is not there), copies the game server's history when it is asked for and the file has none yet, opens the readers and starts the counter thread. False, with the
    /// reason in plain words, when something cannot be done: the service has not started then. Call it once.
    bool start(std::string& why);
    /// Stops the counter thread and lets go of the database (the destructor does it too)
    void stop();

    /// One request of the public door, on the caller's thread (the HTTP thread: one at a time). `now_ms` is a steady millisecond clock.
    ctl::HttpResponse handle(const ctl::HttpRequest& request, uint32_t now_ms);

    /// What the counter has done, safe to ask from any thread
    struct Stats {
        size_t matches{0};                            // the records in the history, as the counter sees them
        size_t counted{0};                            // counted since the start
        size_t left_out{0};                           // recordings that cannot be counted (another simulation's rules, a map that is not here, ...), since the start
        size_t pending{0};                            // recordings that wait to be counted
        uint64_t passes{0};                           // how often the counter has looked (a test waits for it to move)
    };
    Stats stats() const;
    /// The result of the copy at the start (empty report: no game database was given, or the service has not started)
    const ImportReport& import_report() const noexcept { return import_report_; }
    /// True when, at the start, the recordings folder held no recording and the history held records: the folder may be the wrong one or a mount that is empty (the log says so). Nothing is deleted for it either way.
    bool recordings_folder_empty_with_history() const noexcept { return folder_empty_with_history_; }

private:
    class ApprovedMaps;
    void counter_main();
    void say(const std::string& line);
    void flush_notes(server::ReplayStore* replays, server::HistoryStore* history);

    ServiceConfig cfg_;
    bool started_{false};
    bool folder_empty_with_history_{false};
    ImportReport import_report_;
    std::mutex log_mutex_;

    // the counter's (touched by its thread only, from start() until it is joined)
    db::Database writer_db_;
    std::unique_ptr<server::ReplayStore> counter_replays_;
    std::unique_ptr<server::HistoryStore> counter_history_;
    std::unique_ptr<ApprovedMaps> approved_;
    std::unique_ptr<server::HistoryFeeder> feeder_;
    std::thread counter_;
    std::mutex wake_mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stop_{false};                   // (set under wake_mutex_, so that the wait cannot miss it)

    // the HTTP thread's
    db::Database reader_db_;
    std::unique_ptr<server::ReplayStore> http_replays_;
    std::unique_ptr<server::HistoryStore> http_history_;
    bool http_look_armed_{false};
    uint32_t http_next_look_ms_{0};

    // what the counter publishes
    std::atomic<size_t> matches_{0};
    std::atomic<size_t> counted_{0};
    std::atomic<size_t> left_out_{0};
    std::atomic<size_t> pending_{0};
    std::atomic<uint64_t> passes_{0};
};

}  // namespace ants::history
