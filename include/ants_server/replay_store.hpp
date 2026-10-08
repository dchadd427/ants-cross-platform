#pragma once

// The matches that the game server keeps (docs/REPLAYS.md "On the game server"): every match that is played in a room of the server is recorded (ants_replay/recorder.hpp, fed by the referee's
// own runner) and, when it is over, its .antsrep file is kept in one folder of the results volume. The files are small (a heavy half hour of four players is under 200 KiB) and are kept
// for `keep_days` (30), and never more than `max_bytes` of them together (the oldest are deleted first): that volume also holds the control secret, the restart records and the site's
// counters, so a full disk must never stop the server and the store gives way before it does.
//
// The store only touches files that it could have made itself: `ants-<MAP>-<YYYYMMDD>-<HHMMSS>Z.antsrep` (the UTC time when the match ended, a "-2", "-3" ... when two matches of one map end in the
// same second) in its folder. Any other file in the folder is left alone and is not counted. A file is found, read and deleted by NAME from its index, never by a path that came from outside.
// A file is written whole to `<name>.tmp` and renamed, so a crash of the server leaves a whole file or none (a power cut may leave a short one: it is listed as a file that cannot be read, shown to the
// owner only, and deleted when it is old); a `.tmp` that is found at the start is a crash's and is deleted. The ages are the times in the names, so they are as right as the server's clock.
//
// What the files say about the players: nothing that a person typed. A person's seat has no name in the file (the readers show the colour: "Green"), a computer player's seat has its display name
// ("Bot (Medium)"). So a copy of a file that is made public (ReplayStore is also what the public list reads) holds no name, address or room code.
//
// Single threaded like the server's loop. Opening the store reads every file once; after that nothing takes longer than one small file write or one pass over the index (the hourly purge).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace ants::server {

/// A match is kept when it ran at least this many turns (30 seconds: the matches that the front page counts, SiteStats::kMinTicks), however it ended; a shorter one is not
inline constexpr uint32_t kReplayMinTurns = 600;

struct ReplayConfig {
    std::string dir;                                 // the folder of the files; empty: the server keeps none
    uint32_t keep_days{30};                          // a file is deleted this many days after the match ended (1 .. 3650)
    uint64_t max_bytes{100ull * 1024ull * 1024ull};  // all the files together; past it the oldest are deleted first (at most 4 GiB)
    uint32_t max_saves_per_hour{120};                // more matches than this in a rolling hour are not kept (a flood of short demo matches must not push the others out)
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
};

/// What the store knows about one file: from its name, and from its head and end (read once, when the file was made or the store was opened)
struct ReplayEntry {
    std::string file;                                // "ants-TREASURE-20261008-143209Z.antsrep": never a path
    uint64_t bytes{0};
    int64_t ended_s{0};                              // seconds since 1970 UTC when the match ended (the stamp in the name)
    uint32_t sequence{1};                            // 1 for the first match of a map that ended in that second, 2, 3 ... for the later ones (the "-2" of the name): the order of matches that ended together
    bool readable{false};                            // the file is a replay that this build can read (false: damaged, or made by a newer format; it is kept until it is old, and only its owner sees it)
    std::string map;                                 // the map's file name as the match named it
    uint16_t rules{0};                               // net::kProtocolVersion of the build that recorded it: a build plays a file only when it has the same number
    std::string game;                                // "v0.10.1"
    uint32_t turns{0};
    bool finished{false};                            // the rules ended the match (false: it was left, or it ran into the room's time limit)
    std::vector<std::string> players;                // the seats that played, "Green" or "Red (Bot (Medium))", in seat order
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
    static constexpr int64_t kRepeatReportEveryS = 3600;   // a line that keeps coming (a disk that refuses every match) is told once, and how often it came again at most this often

    explicit ReplayStore(ReplayConfig config);

    const ReplayConfig& config() const noexcept { return cfg_; }
    bool enabled() const noexcept { return ready_; }

    /// Makes the folder when it is not there, deletes the half-written files of a crash, reads the name and head of every file that is the store's own, and applies the age and size limits. False, with the
    /// reason, when the folder cannot be made or used. Call it once.
    bool prepare(std::string& why);

    /// Keeps a match: `bytes` is a whole .antsrep file (it is read again here: a file that this build cannot read is refused). The age and size limits are applied first, so that the new file fits;
    /// it is not written when the hour's limit is spent, the disk would be left with less than min_free_bytes, or the file is bigger than the whole store may be.
    ReplaySave save(const std::vector<uint8_t>& bytes);

    /// The ages are looked at when kPurgeEveryS have passed since the last look. Call it every pass of the server's loop.
    void update();
    /// Deletes the files that are older than keep_days and, when the files together pass max_bytes, the oldest ones. Returns how many files it deleted.
    size_t purge();

    /// The files, newest first (matches of one map that ended in the same second: the one that was kept last first; of two maps: by name), as a copy
    std::vector<ReplayEntry> list() const;
    /// The index itself, OLDEST first (the newest are at the end), for a reader that goes through it without a copy; valid until the store is changed
    const std::vector<ReplayEntry>& entries() const noexcept { return entries_; }
    /// How many of the files this build can read (ReplayEntry::readable)
    size_t readable_count() const noexcept { return readable_; }
    const ReplayEntry* find(const std::string& file) const;
    /// The bytes of a file of the index. False for a name that is not in the index (nothing is opened for it) and for a file that cannot be read.
    bool read(const std::string& file, std::vector<uint8_t>& out) const;
    /// Deletes a file of the index. False when there is no such file or it cannot be deleted.
    bool remove(const std::string& file);

    size_t count() const noexcept { return entries_.size(); }
    uint64_t total_bytes() const noexcept { return total_; }
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

private:
    bool delete_file(ReplayEntry& entry, bool say = true);       // `say`: a file that cannot be deleted is a line in the log
    size_t trim(uint64_t incoming);                  // deletes what is too old, then the oldest until `incoming` more bytes fit; returns how many files went
    void note(const std::string& line);
    int64_t now_s() const;
    std::string path_of(const std::string& file) const;
    bool summarize(const std::vector<uint8_t>& bytes, ReplayEntry& out, std::string& why) const;

    ReplayConfig cfg_;
    bool ready_{false};
    std::vector<ReplayEntry> entries_;               // oldest first
    uint64_t total_{0};
    size_t readable_{0};
    std::vector<int64_t> saved_at_;                  // when the last files were kept (the hour's budget)
    int64_t next_purge_s_{0};
    std::vector<std::string> notes_;
    std::string last_note_;
    size_t repeats_{0};                              // how often last_note_ came again since it was told
    int64_t repeat_report_s_{0};                     // when the repeats are told if no other line comes first
};

}  // namespace ants::server
