#pragma once

// The match history of the game server (docs/REPLAYS.md "The match history"): one small JSON file for every match that the server has recorded, kept for good. The recording itself (the .antsrep file of
// replay_store.hpp) is deleted after 30 days; what the match came to (who won, the scores, who killed and lost how many, how many ants were hatched, the bombs and fires) stays here, with the names
// that the players typed.
//
// The numbers are not counted while the match is played. A match is played once more, with no screen, when it is over (HistoryFeeder, below): replay::play() runs the recorded file on the engine and the
// engine's own MatchResult, the one that its results screen is built from, says what each seat came to. The file is the match, so the numbers are exactly those that a player sees at the end of it.
//
// One file for a match, `<id>.json` in the history folder (the id is the replay's file name without ".antsrep": `ants-TREASURE-20261008-143209Z`), written whole to `<id>.json.tmp` and renamed (a crash leaves a
// whole file or none, a power cut may leave a short one: it is skipped, counted in the log and never deleted, and a match whose recording is still there is counted again over it). There is no database in
// the server's image and none is needed: a year of 40 matches a day is 15,000 files of about 1 KiB. The store reads them all once when the server starts and keeps the summaries in memory (the record
// itself is the summary), so a list, a filter or a sort never opens a file; the details of one match are read by its id when asked.
//
// A record outlives its recording and is never deleted by the server. The owner can delete one (`DELETE /history/<id>` of the control interface): the file is then replaced by a small marker that says so,
// so that the match whose recording is still there is not counted again, and the marker goes when the recording does.
//
// Single threaded like the server's loop.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "ants_ctl/json.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"
#include "ants_server/replay_store.hpp"

namespace ants::server {

/// The layout of a record file (the key "v"). A reader skips every key that it does not know, so a later version can add keys (the details of a match go under "details") without a new number.
inline constexpr int64_t kHistoryVersion = 1;
/// A record file is at most this long (a damaged or foreign file is not read further; half a MiB leaves room in the control interface's 1 MiB answer for the keys that the answer adds)
inline constexpr size_t kHistoryMaxFileBytes = 512u * 1024u;
inline constexpr const char* kHistoryExtension = ".json";

/// What one seat came to. All numbers are the engine's own (`replay::Outcome::result`).
struct HistorySeat {
    uint8_t seat{0};                     // 0 Green, 1 Red, 2 Blue, 3 Black
    std::string name;                    // the name that the player typed ("" when nothing was typed; a computer player has none: see `bot`)
    std::string bot;                     // a computer player's level ("Medium", "Hard", "Easy"; "Bot" when its name says no level); "" for a person
    int32_t score{0};                    // the seat's own score (a team's row adds its seats' up)
    uint32_t killed{0};                  // enemy ants it killed
    uint32_t lost{0};                    // its own ants that died
    uint32_t hatched{0};                 // ants it hatched
    uint32_t bombs_planted{0};
    uint32_t bombs_defused{0};
    uint32_t fires_lit{0};
    bool left{false};                    // it quit (and its quit ended the match) or dropped out of the match before the end
    bool winner{false};                  // in the first row of the results, and the match is no draw
};

/// One row of the results, as the match's own screen builds them: a seat alone, or two allied seats together with their numbers added up. Ranked: the first row won (unless `draw`).
struct HistoryRow {
    std::vector<uint8_t> seats;          // one or two
    int32_t score{0};
    int32_t killed{0};
    int32_t lost{0};
    int32_t hatched{0};
    bool winner{false};
};

struct HistoryRecord {
    std::string id;                      // the replay's file name without ".antsrep"
    int64_t ended_s{0};                  // seconds since 1970 UTC (the time in the file's name)
    std::string map;                     // "TREASURE.LVL"
    std::string game;                    // "v0.12.0": the game that recorded the match
    uint32_t turns{0};
    bool finished{false};                // the rules ended the match (false: a player left it, or the room's time ran out)
    int quitter{-1};                     // the seat whose quit ended the match, -1 when none did
    std::string format;                  // "1v1", "2v1", "2v2", "ffa3" or "ffa4": how the match began (the teams it started with)
    bool draw{false};                    // the first two rows are equal in score, kills, losses and hatched, and nobody quit
    bool computers_only{false};          // no person played
    std::vector<HistorySeat> seats;      // the seats that played, in seat order
    std::vector<HistoryRow> rows;        // the results, ranked
    uint32_t seconds() const noexcept { return turns / 20; }
};

/// Whether the recording of a match can still be watched: `watch` (the file is kept and this build plays it), `old` (kept, recorded by another simulation: it cannot be played here), `removed` (gone)
enum class Recording : uint8_t { Watch, Old, Removed };
const char* recording_name(Recording r) noexcept;

/// What the engine said about a match that was played again: the record of it. `id` and `ended_s` come from the file's name. Pure: no clock, no disk.
HistoryRecord make_history_record(const std::string& id, int64_t ended_s, const replay::Replay& replay, const replay::Outcome& outcome);

/// The record as JSON. `full` adds the numbers that the list leaves out (bombs, fires); the record file is the full one.
ctl::JsonValue history_record_to_json(const HistoryRecord& record, bool full);
/// A record from its JSON (a file); false with a reason when it is not one (the layout is the one above; unknown keys are ignored)
bool history_record_from_json(const ctl::JsonValue& json, HistoryRecord& out, std::string& why);

/// The limits that a record file is parsed with (its size, the elements of a list: the details of a match that a later version keeps in the same file are long lists)
ctl::JsonLimits history_file_limits();

/// True for an id that the replay store could have made as a file name ("ants-<MAP>-<YYYYMMDD>-<HHMMSS>Z[-<n>]"): the only ids that exist
bool valid_history_id(const std::string& id);
/// The file name of the recording of a match: its id and ".antsrep"
std::string history_recording_file(const std::string& id);

struct HistoryConfig {
    std::string dir;                                 // the folder of the records; empty: none are kept
    /// Gives a finished temporary file its name, `ec` set when it cannot (empty: std::filesystem::rename); the tests make a rename fail
    std::function<void(const std::string& from, const std::string& to, std::error_code& ec)> rename_file;
};

/// What a list asks for (`GET /history`): the controls of the page's list
struct HistoryQuery {
    enum class Sort : uint8_t { New, Old, Score, Kills, Hatched, Long, Short };
    enum class Format : uint8_t { All, OneVsOne, Teams, Free };            // all, 1v1, 2v1 and 2v2, ffa3 and ffa4
    enum class Result : uint8_t { All, Decided, Draw, Cut };               // cut: a player left, or the time ran out
    enum class Who : uint8_t { Anyone, People, Computers };
    enum class Rec : uint8_t { All, Watch, Removed, Old };
    size_t limit{50};
    size_t offset{0};
    Sort sort{Sort::New};
    Format format{Format::All};
    Result result{Result::All};
    Who who{Who::Anyone};
    Rec rec{Rec::All};
    int colour{-1};                                  // a seat that took part (0 - 3), -1 for any
    std::string text;                                // part of a map's name or of a name that a seat has (letters of any case)
};

struct HistoryPage {
    size_t count{0};                                 // the records that fit the filters
    std::vector<const HistoryRecord*> items;         // the page asked for; valid until the store is changed
};

class HistoryStore {
public:
    static constexpr size_t kMaxNoteLines = 200;     // notes that nobody took are dropped past this (the server's loop takes them every pass)

    explicit HistoryStore(HistoryConfig config);

    const HistoryConfig& config() const noexcept { return cfg_; }
    bool enabled() const noexcept { return ready_; }

    /// Makes the folder when it is not there, deletes the half-written files of a crash, and reads every record once. A file that is not a record is left where it is and counted in the log. False, with the
    /// reason, when the folder cannot be made or read. Call it once.
    bool prepare(std::string& why);

    /// Keeps a record (a record of the same id is replaced): the file is written whole under a temporary name and renamed. False, with the reason, when the id is not one or the disk refuses; the store is as it was.
    bool put(const HistoryRecord& record, std::string& why);
    /// The owner takes a match out of the history. With `keep_marker` (the recording is still there) the file becomes a marker that stops the match from being counted again, else the file is deleted. False when there is
    /// no such record.
    bool remove(const std::string& id, bool keep_marker, std::string& why);
    /// Deletes the markers of matches that `still_needed` does not know any more (their recording has gone). Returns how many it deleted.
    size_t prune_markers(const std::function<bool(const std::string& id)>& still_needed);

    size_t count() const noexcept { return records_.size(); }
    size_t marker_count() const noexcept { return markers_.size(); }
    /// The records, oldest first (the newest are at the end)
    const std::vector<HistoryRecord>& records() const noexcept { return records_; }
    const HistoryRecord* find(const std::string& id) const;
    /// True when the match is in the history, or the owner took it out (it must not be counted again)
    bool knows(const std::string& id) const;
    /// The text of a record's file; false when there is none (or it cannot be read)
    bool read_file(const std::string& id, std::string& text) const;

    /// The page of records that fit `query`, as `recording` says for each record (called only when a filter asks for it, never for a record twice)
    HistoryPage query(const HistoryQuery& query, const std::function<Recording(const std::string& id)>& recording) const;

    /// Lines for the server's log (what prepare() found, a record that the disk refused), once each
    std::vector<std::string> take_notes();
    void report(const std::string& line) { note(line); }

private:
    void note(const std::string& line);
    std::string path_of(const std::string& id, const char* suffix = kHistoryExtension) const;
    bool write_file(const std::string& id, const std::string& text, std::string& why);
    void insert(HistoryRecord record);

    HistoryConfig cfg_;
    bool ready_{false};
    std::vector<HistoryRecord> records_;             // oldest first: the end time, then the id
    std::set<std::string> markers_;                  // the ids that the owner took out
    std::vector<std::string> notes_;
};

/// Counts the matches that the replay store keeps and the history does not know yet, one at a time. A file is played again (about a tenth of a second to a third) with no screen and its numbers go to
/// the history. The work is never done on the server's loop in a burst: one file at most per call, at least kPaceMs after the last one, and only when the caller says the server is idle (no room runs a
/// match). The files that the replay store holds when the server starts (up to 30 days of them) are counted the same way, the newest first, and so are the matches that arrive later (the room's own, and
/// the files that the bot arena puts in the folder). A file that cannot be counted (another simulation's rules, a map that is not here, a file that does not play out the same here) is left out with one line
/// in the log, and tried again only when the server starts again.
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
