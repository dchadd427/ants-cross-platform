#pragma once

// The matches that the game server keeps (docs/REPLAYS.md "On the game server"): every match that is played in a room of the server is recorded (ants_replay/recorder.hpp, fed by the referee's
// own runner) and, when it is over, its .antsrep file is kept in one folder of the results volume. The files are small (a heavy half hour of four players is under 200 KiB) and are kept
// for `keep_days`, and never more than `max_bytes` of them together (the oldest are deleted first): that volume also holds the control secret, the restart records and the site's
// counters, so a full disk must never stop the server and the store gives way before it does.
//
// The matches of computer players alone (the server's own, ReplayEntry::bots_only: every seat of the file is a bot's) have a budget of their own, so that they never crowd people's matches out: they may take
// `max_bot_saves_per_hour` of the hour's `max_saves_per_hour` and `bot_share_percent` of `max_bytes`, and whatever has to be deleted for room (the size limit, the disk's free space) is a bot's file first, the
// oldest first, and a person's only when no file of a bot is left. A match of bots never deletes a file of a person: when it does not fit among the bots' files it is not kept.
//
// The store only touches files that it could have made itself: `ants-<MAP>-<YYYYMMDD>-<HHMMSS>Z.antsrep` (the UTC time when the match ended, a "-2", "-3" ... when two matches of one map end in the
// same second) in its folder. Any other file in the folder is left alone and is not counted. A file is found, read and deleted by NAME from its index, never by a path that came from outside.
// A file is written whole to `<name>.tmp` and renamed, so a crash of the server leaves a whole file or none (a power cut may leave a short one: it is listed as a file that cannot be read, shown to the
// owner only, and deleted when it is old); a `.tmp` that is found at the start is a crash's and is deleted. The ages are the times in the names, so they are as right as the server's clock.
//
// What the files say about the players: the names that the room showed everybody. A person's seat has the name that was typed (printable ASCII, at most 32 characters: "Green (Ann)"; replay_person_name in room.hpp)
// and none when nothing was typed (the readers show the color: "Green"), a computer player's seat has its display name ("Bot (Medium)"). That is all a file says of the people: no address, room code,
// key or chat. The names are public when the server's public door is on (ReplayStore is also what the public list reads).
//
// Single threaded like the server's loop. Opening the store reads every file once; after that nothing takes longer than one small file write or one pass over the index (the hourly purge). The look at the folder for
// files that somebody else put there (update(): every kRescanEveryS, and only when the folder's change time moved) reads the folder in slices of kRescanBudgetUs a pass, never in one go: a folder of thousands of
// files takes tens of milliseconds to list, and the loop that runs every match of the server must not stop for that. The store's own writes and deletes keep its list as it is and do not call for a look.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace ants::server {

/// A match is kept when it ran at least this many turns (30 seconds: the matches that the front page counts, SiteStats::kMinTicks), however it ended; a shorter one is not
inline constexpr uint32_t kReplayMinTurns = 600;

struct ReplayConfig {
    std::string dir;                                 // the folder of the files; empty: the server keeps none
    uint32_t keep_days{30};                          // a file is deleted this many days after the match ended (1 .. 3650)
    uint64_t max_bytes{100ull * 1024ull * 1024ull};  // all the files together; past it the oldest are deleted first (at most 4 GiB), the files of bots before any file of a person
    uint32_t max_saves_per_hour{120};                // more matches than this in a rolling hour are not kept (a flood of short demo matches must not push the others out)
    uint32_t max_bot_saves_per_hour{30};             // ... of them at most this many matches of computer players alone (0: none is kept); they count among the hour's saves
    uint32_t bot_share_percent{25};                  // the files of matches of computer players alone may take this part of max_bytes (1 .. 100)
    uint64_t min_free_bytes{256ull * 1024ull * 1024ull};     // a file is not written when the disk would be left with less than this free (0: not asked)
    std::string game_version;                        // the build that records (the head of every file says it)
    std::string build_id;
    /// Seconds since 1970-01-01 00:00:00 UTC (empty: the system clock); the tests give their own
    std::function<int64_t()> clock_s;
    /// The free bytes of the disk that holds `dir` (empty: std::filesystem::space); false when it cannot be asked (then the disk is not checked)
    std::function<bool(const std::string& dir, uint64_t& available)> free_bytes;
    /// Deletes a file of the store, `ec` set when it cannot (empty: std::filesystem::remove); the tests make a delete fail
    std::function<void(const std::string& path, std::error_code& ec)> remove_file;
    /// Gives a finished temporary file its name, `ec` set when it cannot (empty: std::filesystem::rename); the tests make a rename fail
    std::function<void(const std::string& from, const std::string& to, std::error_code& ec)> rename_file;
    /// A file (or the folder) that changed less than this many seconds ago is not trusted yet by rescan(): a copy that is still growing would be read as a damaged file, and a folder that changed within
    /// the time stamp's grain may change again unseen. It is looked at again at the next look. The tests set 0.
    uint32_t settle_s{2};
    /// Microseconds on a steady clock, which the slices of the look at the folder are timed by (empty: std::chrono::steady_clock); the tests give their own
    std::function<int64_t()> clock_us;
    /// The store only looks at a folder that another program keeps (the history service, docs/HISTORY_SERVICE.md): it makes no folder, deletes no half-written file, applies no age or size limit, and keeps and
    /// deletes nothing (save() and remove() say no). A file that the keeper deletes is forgotten at the next rescan(). keep_days and max_bytes are then only what the readers show.
    bool read_only{false};
    /// prepare() only lists the files (their names and sizes: the age and the order are in the names) and update() reads the head of each one a few at a time, the newest first, so that opening a store of a gibibyte does not
    /// hold the server back (10 s for 100,000 small files). Until a file is read it is listed as `pending`, is not counted as readable and is not given to the history; find() reads it at once when somebody asks for it by name.
    /// Off by default (the tests, the history service and the tools read at once).
    bool index_in_background{false};
};

/// What the store knows about one file: from its name, and from its head and end (read once, when the file was made or the store was opened)
struct ReplayEntry {
    std::string file;                                // "ants-TREASURE-20261008-143209Z.antsrep": never a path
    uint64_t bytes{0};
    int64_t ended_s{0};                              // seconds since 1970 UTC when the match ended (the stamp in the name)
    uint32_t sequence{1};                            // 1 for the first match of a map that ended in that second, 2, 3 ... for the later ones (the "-2" of the name): the order of matches that ended together
    bool readable{false};                            // the file is a replay that this build can read (false: damaged, or made by a newer format; it is kept until it is old, and only its owner sees it)
    bool pending{false};                             // the file is listed and its head is not read yet (ReplayConfig::index_in_background): readable is false until it is
    bool bots_only{false};                           // every seat that played has a computer player's name (a name that a person cannot take): the server's own match, which has its own budget
    std::string map;                                 // the map's file name as the match named it
    uint16_t rules{0};                               // net::kProtocolVersion of the build that recorded it (information: a file plays on its sim_rules)
    uint16_t sim_rules{0};                           // the rules number of the simulation that the match needs (replay::sim_rules_of: the head's own, else the table's; 0: unknown): a build plays a file only when it has the same number
    std::string game;                                // "v0.10.1"
    uint8_t mode{0};                                 // the game mode of the match (sim::GameMode as a byte; the head's field 17): 0 the original's highest score, 1 "187"
    uint32_t turns{0};
    bool finished{false};                            // the rules ended the match (false: it was left, or it ran into the room's time limit)
    std::vector<std::string> players;                // the seats that played, "Green (Ann)", "Green" (no name) or "Red (Bot (Medium))", in seat order
};

struct ReplaySave {
    bool kept{false};
    std::string file;                                // the name it has now
    uint64_t bytes{0};
    std::string note;                                // not kept: why, in one sentence
};

class ReplayStore {
public:
    static constexpr size_t kMaxNameChars = 100;
    static constexpr const char* kExtension = ".antsrep";
    static constexpr const char* kTempExtension = ".tmp";
    static constexpr int64_t kPurgeEveryS = 3600;    // the age limit is looked at this often (and before every save, together with the size limit)
    static constexpr int64_t kRescanEveryS = 30;     // the folder's change time is looked at this often (update()): a file that somebody else put there, or took away, is noticed then
    static constexpr size_t kRescanBatch = 100;      // at most this many new files are read in one look (the rest at the next one: a big drop must not hold the server's loop)
    static constexpr int64_t kRescanBudgetUs = 2000; // a pass of the loop spends at most this long on the look at the folder (the look goes on in the next pass): a slice lists or reads files until it has, and ends with the one that passes it
    static constexpr int64_t kRescanSafetyEveryS = 600;    // the folder is looked at in full this often even when its change time did not move: a change of it that the time did not show (two changes within its grain) is found at the latest then
    static constexpr int64_t kRepeatReportEveryS = 3600;   // a line that keeps coming (a disk that refuses every match) is told once, and how often it came again at most this often

    static constexpr size_t kIndexBatch = 40;        // at most this many files get their head read in one pass of the loop (index_in_background)
    static constexpr int64_t kIndexBudgetUs = 4000;  // ... and a pass stops reading when it has spent this long
    static constexpr size_t kIndexRevisionFiles = 500;     // the index's revision moves after this many files were read in the background (and when the last one is), not at every pass

    explicit ReplayStore(ReplayConfig config);
    ~ReplayStore();

    const ReplayConfig& config() const noexcept { return cfg_; }
    bool enabled() const noexcept { return ready_; }

    /// Makes the folder when it is not there, deletes the half-written files of a crash, reads the name and head of every file that is the store's own, and applies the age and size limits. False, with the
    /// reason, when the folder cannot be made or used. Call it once.
    bool prepare(std::string& why);

    /// Keeps a match: `bytes` is a whole .antsrep file (it is read again here: a file that this build cannot read is refused). The age and size limits are applied first, so that the new file fits;
    /// it is not written when the hour's limit is spent (a match of computer players alone has its own, smaller one), the disk would be left with less than min_free_bytes after the files that may go for it were
    /// deleted, or the file is bigger than the whole store (a bots' match: the bots' share of it) may be.
    ReplaySave save(const std::vector<uint8_t>& bytes);
    /// How many more matches of computer players alone could be kept this hour (the bots' limit and the hour's, whichever is smaller): what the server asks before it starts one, since a match that would not be kept
    /// is not played
    size_t bot_saves_left() const;

    /// The ages are looked at when kPurgeEveryS have passed since the last look, and the folder when kRescanEveryS have. Call it every pass of the server's loop: a look at the folder that is under way goes on by one
    /// slice (kRescanBudgetUs) in each call.
    void update();
    /// Reads the heads of files that are still `pending` (index_in_background): at most kIndexBatch and kIndexBudgetUs. update() calls it. Returns how many files it read.
    size_t index_step();
    /// How many files wait for their head to be read
    size_t unindexed() const noexcept { return unindexed_; }
    /// Reads the folder again when its change time moved: files of the store's own kind that somebody else put there (bot_arena --save-replays) are listed, files that were taken away are forgotten (nothing is deleted
    /// from the disk), and the limits are applied. A file that is known is not read again. Returns how many files were added or forgotten. This is the whole look in one go (the history service and the tests
    /// call it; a look that update() has begun is finished first); update() makes the same look in slices.
    size_t rescan();
    /// A look at the folder is under way (update() goes on with it in the next pass), and how many looks were begun since the store was opened (the folder was listed that many times)
    bool rescanning() const noexcept { return look_ != nullptr; }
    uint64_t looks() const noexcept { return looks_; }
    /// Deletes the files that are older than keep_days and, when the files together pass max_bytes, the oldest ones. Returns how many files it deleted.
    size_t purge();

    /// The files, newest first (matches of one map that ended in the same second: the one that was kept last first; of two maps: by name), as a copy
    std::vector<ReplayEntry> list() const;
    /// The index itself, OLDEST first (the newest are at the end), for a reader that goes through it without a copy; valid until the store is changed
    const std::vector<ReplayEntry>& entries() const noexcept { return entries_; }
    /// How many of the files this build can read (ReplayEntry::readable)
    size_t readable_count() const noexcept { return readable_; }
    /// The entry of a file of the index; a file that is `pending` is read first, so that a request for one file by its name is answered whatever the background pass has reached
    const ReplayEntry* find(const std::string& file) const;
    /// The bytes of a file of the index. False for a name that is not in the index (nothing is opened for it) and for a file that cannot be read.
    bool read(const std::string& file, std::vector<uint8_t>& out) const;
    /// Deletes a file of the index. False when there is no such file or it cannot be deleted.
    bool remove(const std::string& file);

    size_t count() const noexcept { return entries_.size(); }
    uint64_t total_bytes() const noexcept { return total_; }
    /// The files and bytes of the matches of computer players alone
    size_t bot_count() const noexcept { return bot_count_; }
    uint64_t bot_bytes() const noexcept { return bot_total_; }
    /// The most that the matches of computer players alone may take together (bot_share_percent of max_bytes)
    uint64_t bot_cap_bytes() const noexcept;
    /// A number that moves every time the index changes (a file kept, found, forgotten, deleted): a reader that works through the files, the match history's feeder, looks at it to know when to look again
    uint64_t revision() const noexcept { return revision_; }
    /// Lines for the server's log, once each: what prepare() found, files that were purged, a file that was refused or could not be written. A repeat of the last line is counted, not repeated: the count
    /// comes as one line when another line follows, every kRepeatReportEveryS (update()) and when the server stops (report_repeats()).
    std::vector<std::string> take_notes();
    /// Puts the count of the repeats of the last line (if there is one) among the notes now
    void report_repeats();
    /// A line for that log from outside the store (a room that could not make a file of its match)
    void report(const std::string& line) { note(line); }

    /// True for a name that the store could have made: `ants-<A-Z a-z 0-9 _>-<YYYYMMDD>-<HHMMSS>Z[-<n>].antsrep`, at most kMaxNameChars characters
    static bool valid_file_name(const std::string& name);
    /// The end time (seconds since 1970 UTC) that a valid name carries; false for any other name
    static bool time_of_name(const std::string& name, int64_t& seconds);
    /// The pieces of a name, for the id of a live match (live_board.hpp) to be made of the same: "TREASURE.LVL" gives "TREASURE" (the letters, digits and '_' of the map's file name without its extension, any other
    /// character a '_', at most 24 of them; "match" when nothing is left), and a time gives "20261008-143209Z" (UTC)
    static std::string map_stem(const std::string& map_name);
    static std::string time_stamp(int64_t seconds);

private:
    bool delete_file(ReplayEntry& entry, bool say = true);       // `say`: a file that cannot be deleted is a line in the log
    /// Deletes what is too old, then the files of bots (the oldest first) and, only when the match that comes is a person's, the files of people (the oldest first) until `incoming` more bytes fit under max_bytes (a bots' match:
    /// and under the bots' share) and `free_needed` bytes were given back to the disk. Returns how many files went.
    size_t trim(uint64_t incoming, bool incoming_bot, uint64_t free_needed = 0);
    size_t evict(bool bots, const std::function<bool()>& wanted, size_t& failed);     // one pass over the files of bots or of people, the oldest first, while `wanted()`
    void count_in(const ReplayEntry& e);             // the totals take a file in / let one go
    void count_out(const ReplayEntry& e);
    bool ask_free(uint64_t& available) const;        // the free bytes of the disk of the folder; false when it cannot be asked
    void report_headroom();                          // a line when the disk's free space (less the guard) is less than the room that the size limit still has
    bool index_entry(ReplayEntry& entry) const;      // reads the head of a pending file
    ReplayEntry* raw_find(const std::string& file) const;      // the entry of a name, by bisection, without reading it
    void note(const std::string& line);
    int64_t now_s() const;
    std::string path_of(const std::string& file) const;
    bool summarize(const std::vector<uint8_t>& bytes, ReplayEntry& out, std::string& why) const;
    ReplayEntry read_entry(const std::string& name, uint64_t size) const;
    int64_t folder_stamp() const;                    // the folder's change time as a number (0: it cannot be asked)
    int64_t clock_us() const;
    // The look at the folder, in slices (replay_store.cpp)
    struct Look;
    bool look_begin(bool force);                     // starts a look when the folder's change time moved, a look is asked for or `force`; false: nothing to look at
    void look_step(int64_t budget_us);               // goes on with the look for about `budget_us` (negative: to the end); the look is over (look_ is empty) when it has read everything
    void look_end();
    void own_change(const std::string& file, int64_t stamp_before);       // the store itself wrote or deleted `file`: the list has it already, and the folder's new change time is not a reason to look when nothing else had moved it

    ReplayConfig cfg_;
    bool ready_{false};
    mutable std::vector<ReplayEntry> entries_;       // oldest first (find() reads the head of a pending file: the entry changes, not its place)
    uint64_t total_{0};
    mutable size_t readable_{0};
    mutable uint64_t bot_total_{0};
    mutable size_t bot_count_{0};
    mutable size_t unindexed_{0};
    mutable uint64_t revision_{0};                   // moves with every change of entries_ (revision())
    mutable size_t read_since_revision_{0};
    std::vector<std::string> index_queue_;           // the files whose head is not read, the next one at the back (the newest first)
    std::vector<int64_t> saved_at_;                  // when the last files were kept (the hour's budget)
    std::vector<int64_t> bot_saved_at_;              // ... and of those, the matches of computer players alone
    int64_t next_purge_s_{0};
    int64_t next_rescan_s_{0};
    int64_t next_safety_s_{0};
    int64_t folder_stamp_{0};                        // the folder's change time when it was last read
    std::unique_ptr<Look> look_;                     // the look that is under way
    uint64_t looks_{0};
    size_t last_look_{0};                            // what the look that ended last found: files added and forgotten
    bool rescan_more_{false};                        // a look stopped at kRescanBatch: the next one goes on even if the folder did not change
    std::vector<std::string> notes_;
    std::string last_note_;
    size_t repeats_{0};                              // how often last_note_ came again since it was told
    int64_t repeat_report_s_{0};                     // when the repeats are told if no other line comes first
};

}  // namespace ants::server
