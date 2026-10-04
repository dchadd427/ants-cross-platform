#pragma once

// Restart records: what a game server keeps on its results volume so that the matches that are running survive a restart of the server (docs/NETWORK_PORT.md "Restart records"). A deploy
// recreates the container and a crash ends the process: without a record every running match ended with it and its players were thrown out of the game. With one, the server that starts again
// rebuilds the room from the file (the engine from the match's start message, every sealed turn replayed, checked against state hashes that the old server stored), holds the seat of every person,
// and the players come back with their keys (protocol 10, "Reconnect").
//
// One file per room: `room-<code>-<8 hex digits of FNV-1a of the code>.restart` (the code is not a secret, the digits keep two codes that differ in case apart on a file system that ignores case, and the
// prefix keeps a room named like a Windows device from naming one). All numbers are little endian.
//
//   file  := magic frame*                   magic: "ANTSRST1" (8 bytes; the digit is the format: a file of another format is refused as such)
//   frame := u8 type, u32 length, payload, u32 crc32   (the CRC-32 of the type, the length and the payload; a payload is at most 1 MiB)
//
//   HEAD   (1)  the first frame, exactly one: the identity of the build that wrote it (game version, protocol, build id: only the protocol decides), the room's specification (code, map and its hash, players,
//               fog, early start, the times, the reconnect settings, the limit of the turn log), the bots that sit in the room (the specification's and the leader's fill), the start message of the match (seed,
//               roster, names, fog: net::StartMsg in its wire form: the strict decoder of the protocol reads it back) and the KEYS of the four seats. The keys are secrets (a key is all that a seat's owner needs
//               to take it back): the file is for its owner only (POSIX: mode 600 in a folder of mode 700; on Windows it takes the permissions of its folder), and a key is never in a log line, a status or a result file.
//   TURNS  (2)  u32 first_turn, u16 count (1 - 4096), then count turns, each packed as the server's turn log packs it (u16 command count, the commands in their wire form: sim::encode); the turns are numbered
//               from 0 without a hole, and a frame's first_turn is the number of turns before it
//   CHECK  (3)  u32 turn, u64 hash: the referee's state hash (StateHash::total) after turn `turn`, written when the referee's own runner has run it (every 20th turn: turn + 1 is a multiple of net::kHashEveryTurns)
//
// WRITING. The head is made all at once: it is written to a temporary file next to the record (made new, mode 600), made durable (fsync), given its name by a rename that replaces what was there and
// the folder is made durable too, so a crash leaves either no record or a complete head, never half of one. After that the file only grows, one frame at a time, and a frame is handed to the operating
// system with ONE write() call BEFORE the turn that it holds is sent to anybody (HostSession::set_on_seal): when the process dies at any moment (a crash, kill -9, the container's death) the kernel
// keeps what was written, so the record always holds every turn that any player has run, and a restart loses NO turn. What a write() does not survive is the death of the machine (a power cut, a kernel
// panic): the data may still be in the page cache. So the file is also fsync'ed once a second while it is written (the room does it, RestartConfig::sync_every_ms) and when the server is told to stop:
// after a machine's death at most a second of turns (20) can be missing at the end of the record, and a player whose machine is AHEAD of the record (it ran turns that the record lost) is told BadRequest
// by the server (more turns than were ever sealed) and has to start the match from nothing again. One fsync per room and second, not one per turn: a turn is 50 ms, and a disk's flush takes
// milliseconds that all the rooms of the one-threaded server would wait for.
//
// READING. A file is read whole (it is bounded: RestartConfig::max_record_bytes) and checked frame by frame. Everything is validated before anything is built from it: the magic, every length against
// the bytes that are left, every CRC, the order of the frames, the sequence of the turns (at most kRestartMaxTurns, 25 hours of play, judged from a frame's header before any of its turns is decoded), the
// checkpoints (a CHECK must name a turn that the file holds, in increasing order), every field of the head (the strict decoders of the protocol read the start message, the commands, the names). A
// restore reads a record as Streaming: the turns are checked and counted, not kept, and the replay decodes them again one at a time (a hostile file of empty turns costs its own size and no more). A frame that is cut at the END of the file (a write that the death of the machine interrupted: a frame that
// reaches the end of the file and does not fit it, one whose CRC is wrong and that is the last, or nothing but zero bytes) is a torn tail: it is dropped, and the record is the frames before it. Any
// other fault, a bad CRC or length or order in the MIDDLE of the file, is corruption: the record is refused (the room is lost, as it was before records existed) and deleted at once. A record that was
// read and judged to be of a match that cannot go on (another protocol, a changed map, too old, ...) is moved to the folder `refused` and kept for a day (RestartStore::refuse_file), not deleted.
//
// WHAT IS NOT RESTORED. A record of another network protocol is not (a rules change would play the match out differently: the room is closed with that reason, in the log and in the status JSON);
// nor is one whose map file is gone or has changed, whose last write is older than RestartConfig::max_age_ms (its players are gone), or whose replay does not agree with the stored state hashes (the rules
// of this build are not those that played the match). A room that holds no seats (reconnect off) has no record, a room that has not started (waiting) has none either, and a room whose turn log passed
// its limit (RoomSpec::max_log_bytes) or whose record the disk refused stops keeping one (and says so in its status).
//
// ONE SERVER TO A FOLDER. RestartStore::prepare takes a lock on the folder (the file `.lock`) for the life of the store: a second server over the same folder would write the first one's records, so it
// is refused (an explicit --restart-dir stops it with status 1, the default folder is given up with a line in the log); the lock goes with the process, however it ends.
//
// LIMITS. A record is at most RestartConfig::max_record_bytes (48 MiB: 3 times the turn log's own limit); all the records together are at most RestartConfig::budget_bytes (256 MiB); a write that
// fails (a full disk, a quota, an error) ends the record at once: the file is deleted (a record that stops in the middle of a match would bring it back to the wrong tick), the room goes on playing and
// its status says why it has no record.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_net/attendance.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/turnlog.hpp"

namespace ants::server {

inline constexpr char kRestartMagic[8] = {'A', 'N', 'T', 'S', 'R', 'S', 'T', '1'};
inline constexpr uint16_t kRestartFormat = 1;
inline constexpr const char* kRestartExtension = ".restart";
inline constexpr size_t kRestartFrameOverhead = 1 + 4 + 4;                 // type, length, CRC
inline constexpr size_t kRestartMaxFramePayload = 1u << 20;                // nothing that the server writes is bigger (a turn is at most one message: 64 KB)
inline constexpr size_t kRestartMaxHeadBytes = 8 * 1024;
inline constexpr size_t kRestartMaxBatchTurns = 4096;                      // turns in one TURNS frame (a decoder's bound: the server writes one turn per frame)
inline constexpr uint32_t kRestartMaxTurns = 1'800'000;                     // the turns of a whole record: 25 hours of play (a match ends by its own rules, run_ms is at most 24 h); a file with more is refused as corrupt, before its turns are decoded

enum class RestartFrame : uint8_t { Head = 1, Turns = 2, Check = 3 };

/// CRC-32 (IEEE 802.3, the one of zlib and PNG: CRC-32 of "123456789" is 0xCBF43926). `crc` is the value so far (0 to start): the CRC of a long message can be made in pieces.
uint32_t restart_crc32(const uint8_t* data, size_t size, uint32_t crc = 0) noexcept;

/// What the build that wrote a record was. The network protocol decides whether a record is restored (it moves with every change of the rules that the peers of a match must share, and the replay's state
/// hashes are the safety net); the game version and the build id are only for the log: a deploy that changes no rule must not end the matches.
struct RestartIdentity {
    std::string game_version;
    uint16_t protocol{0};
    std::string build_id;
    bool same_rules_as(const RestartIdentity& other) const noexcept { return protocol == other.protocol; }       // (the protocol moves with every change of the rules; the version moves every release: it is only for the log)
};

/// The first frame of a record (see above). The plain data of a room that is to be built again.
struct RestartHead {
    RestartIdentity identity;
    std::string code;
    std::string map;
    uint64_t map_hash{0};
    uint8_t players{2};                                     // the room's seats (RoomSpec::players)
    bool fog{false};
    bool early_start{true};
    uint32_t wait_ms{0};
    uint32_t load_ms{0};
    uint32_t keep_ms{0};
    uint32_t run_ms{0};
    uint32_t vote_after_ms{0};
    uint32_t max_pause_ms{0};
    uint32_t max_catch_up_ms{0};
    uint32_t resume_countdown_ms{0};
    uint64_t max_log_bytes{0};
    uint32_t max_connections{0};
    std::vector<ai::BotSpec> bots;                          // every bot of the room, by seat: the specification's and the leader's fill
    uint8_t fill_mask{0};                                   // bit s: the bot of seat s was seated by the leader's START (not by the room's specification)
    net::StartMsg start;                                    // the match: seed, map name and hash, fog, roster, names
    std::array<net::SeatKey, 4> keys{};                     // the keys of the seats (secrets): all zero for a seat without a person or without a key
};

/// The head as a whole frame (type, length, payload, CRC): what a record starts with after its magic. At most kRestartMaxHeadBytes of payload.
std::vector<uint8_t> encode_restart_head(const RestartHead& head);
/// Reads the payload of a head frame (the strict check of every field: see the file's head). False, with the reason, for anything that an honest writer would not write.
bool decode_restart_head(const uint8_t* payload, size_t size, RestartHead& out, std::string& why);

/// The state hash that the referee had after `turn`
struct RestartCheck {
    uint32_t turn{0};
    uint64_t hash{0};
};

/// A record that was read (read_restart_record, parse_restart_record)
struct RestartLoaded {
    enum class Status : uint8_t {
        Ok,
        Unreadable,         // the file cannot be opened or read, or is not a regular file
        TooBig,             // above the limit that the caller gave
        NotARecord,         // it does not start with the magic
        UnknownFormat,      // it starts with "ANTSRST" and another digit: a record of a format that this build does not read
        Corrupt             // the head is bad, or a frame in the middle of the file is
    };
    Status status{Status::Unreadable};
    std::string why;                                        // not Ok: one sentence
    std::string path;                                       // read_restart_record: where it came from
    RestartHead head;
    uint32_t turn_count{0};                                 // the turns that the record holds (at most kRestartMaxTurns): turns.size() when they are kept
    std::vector<net::TurnMsg> turns;                        // numbered from 0, no hole; not kept (empty) when the record was read as Streaming
    std::vector<uint8_t> bytes;                             // Streaming: the file's bytes, which for_each_restart_turn decodes the turns from again, one at a time
    std::vector<RestartCheck> checks;                       // increasing; each names a turn that the record holds
    uint64_t file_bytes{0};
    uint64_t good_bytes{0};                                 // the end of the last good frame: where a restored room goes on writing (what follows is a torn tail and is cut off)
    bool torn{false};                                       // bytes after good_bytes were dropped: a write that was cut short
    bool ok() const noexcept { return status == Status::Ok; }
};

/// Whether the turns of a record that is read are kept in the result (Whole: the tests, small tools) or only checked and counted (Streaming: a restore, which decodes them again from the file's bytes
/// one at a time, for_each_restart_turn, and so never holds more than one decoded turn: a hostile file of many empty turns costs its own size and no more)
enum class RestartRead : uint8_t { Whole, Streaming };

/// Parses the bytes of a record (no file: the tests and the fuzzer). Never crashes, whatever the bytes are; what it keeps is bounded by the input and by kRestartMaxTurns (the turns of a record
/// that passes are at most that many: about 58 MB decoded); with `keep_turns` false they are checked and counted and not kept (turn_count, checks, good_bytes are all the same).
RestartLoaded parse_restart_record(const uint8_t* data, size_t size, bool keep_turns = true);
/// Reads the file at `path` (a regular file, not a symbolic link, at most `max_bytes` bytes) and parses it. `path` is kept in the result.
RestartLoaded read_restart_record(const std::string& path, uint64_t max_bytes, RestartRead mode = RestartRead::Whole);
/// Gives `fn` every turn of a record that was read, in order, until it says false: the kept turns, or (a Streaming record) the turns decoded again from the file's bytes, one at a time. True when `fn`
/// was given every turn; false when it said stop, or when the bytes cannot be decoded (cannot be: they were checked when the record was read).
bool for_each_restart_turn(const RestartLoaded& rec, const std::function<bool(const net::TurnMsg&)>& fn);

/// What a server is told about its restart records
struct RestartConfig {
    std::string dir;                                        // where the records are kept; empty: the server keeps none
    RestartIdentity identity;                               // what this server is: a record of another network protocol is not restored
    uint64_t budget_bytes{256ull * 1024 * 1024};            // all the records together may take this much disk
    uint64_t max_record_bytes{48ull * 1024 * 1024};         // one record may
    uint32_t restart_vote_after_ms{net::kRestartVoteAfterMs};   // after a restart the others may vote on a seat that has not come back once it has been away this long (never less than the room's own time)
    uint32_t max_age_ms{60u * 60u * 1000u};                 // a record whose last write is older than this is not restored: its players have given up
    uint32_t replay_budget_ms{20u * 1000u};                 // the replay of one room may take this long, in real time; beyond it its record is refused as too slow
    uint32_t restore_budget_ms{30u * 1000u};                // the restore at the start of the server may take this long in all, in real time: a room that it does not reach (or does not finish) is deferred, its
                                                            // record stays on disk and the room is restored when its first player comes (RoomManager::restore_rooms)
    std::function<uint32_t()> clock_ms;                     // the clock that these budgets are measured with, in real milliseconds (empty: restart_steady_ms): the tests give one of their own
    uint32_t refused_keep_ms{24u * 60u * 60u * 1000u};      // a record that was read and refused is kept this long in the folder `refused` (the owner may want it back); the folder is held under budget_bytes
    uint32_t sync_every_ms{1000};                           // a room's record is made durable this often while it is written
};

/// Real milliseconds from a steady clock (they mean something only as differences, and wrap after 49 days): the clock of the restore's budgets unless RestartConfig::clock_ms is set
uint32_t restart_steady_ms() noexcept;

class RestartStore;

/// The record of one room, open for writing. It never deletes the file by itself: the room says (discard) when the room is over, and a server that stops leaves its records where they are.
class RestartWriter {
public:
    ~RestartWriter();
    RestartWriter(const RestartWriter&) = delete;
    RestartWriter& operator=(const RestartWriter&) = delete;

    /// Appends turn number turns() (in order, one after the other: a turn that is not the next one fails the writer). ONE write() call, nothing buffered: the data is in the operating system when this returns.
    bool append_turn(const net::TurnMsg& turn);
    /// Appends the referee's state hash after `turn`
    bool append_check(uint32_t turn, uint64_t hash);
    /// Makes what was written durable (fsync). False when the operating system refuses (the writer fails).
    bool sync();
    /// The room is over (or can no longer be restored): the file is closed and deleted, and its bytes go back to the server's budget. The writer fails: nothing is written any more.
    void discard();
    /// A write, a sync or a limit failed: the writer is dead and error() says why. The caller discards the record (a record that stopped in the middle of a match is wrong).
    bool failed() const noexcept { return failed_; }
    const std::string& error() const noexcept { return error_; }
    const std::string& path() const noexcept { return path_; }
    /// The size of the file and the turns in it (numbered from 0: the next turn to write)
    uint64_t bytes() const noexcept { return bytes_; }
    uint32_t turns() const noexcept { return turns_; }
    /// Something was written since the last sync
    bool dirty() const noexcept { return dirty_; }

private:
    friend class RestartStore;
    RestartWriter(RestartStore* store, std::string path, int fd, uint64_t bytes, uint32_t turns);
    bool write_frame(RestartFrame type, const std::vector<uint8_t>& payload);
    bool fail(const std::string& why);
    void close_file() noexcept;

    RestartStore* store_;
    std::string path_;
    int fd_{-1};                                            // (a Windows HANDLE is kept in handle_)
    void* handle_{nullptr};
    uint64_t bytes_{0};
    uint32_t turns_{0};
    bool dirty_{false};
    bool failed_{false};
    std::string error_;
    std::vector<uint8_t> scratch_;
};

/// The directory of a server's records, their disk budget and the notices that the server logs (never a key). One per server; it outlives its rooms.
class RestartStore {
public:
    explicit RestartStore(RestartConfig config);
    ~RestartStore();
    RestartStore(const RestartStore&) = delete;
    RestartStore& operator=(const RestartStore&) = delete;

    bool enabled() const noexcept { return !cfg_.dir.empty(); }
    const RestartConfig& config() const noexcept { return cfg_; }
    /// Makes the folder (POSIX: mode 700 when it is made here), takes the folder's lock for the life of this store (one server to a folder: the file `.lock`, held with flock (Windows: opened with no
    /// sharing); a lock that another process or another store of this process holds fails this), and only then removes the
    /// temporary files that a crashed start of a record left. False, with the reason, when the folder cannot be used. Calling it again on a store that holds the lock is fine.
    bool prepare(std::string& why);
    /// Where the record of the room with this code is
    std::string path_for(const std::string& code) const;
    /// The room code that the name of a record's file carries (the name that path_for makes), false for any other name: the restore defers a record by its name, without reading it
    bool code_of_path(const std::string& path, std::string& code) const;
    /// The paths of the records in the folder, sorted by name (files that are made like a record's name; temporary files are not records)
    std::vector<std::string> records() const;
    /// Makes the record of a room (the head is written all at once, see the head of this file) and opens it for appending. Null, with the reason, when the budget or the size limit does not allow it or the
    /// disk refuses; nothing is left behind then.
    std::unique_ptr<RestartWriter> create(const RestartHead& head, std::string& why);
    /// Opens the record of a room that was restored, to go on writing it: the file is cut to `good_bytes` (a torn tail goes) and holds `turns` turns. Null, with the reason, when the budget refuses or the
    /// disk does.
    std::unique_ptr<RestartWriter> reopen(const std::string& path, uint64_t good_bytes, uint32_t turns, std::string& why);
    /// Deletes a file of the folder (a record whose room is over, one that cannot be read, ...); true when it is gone. A file that cannot be deleted is remembered as STALE, because a restart would bring
    /// its room back from it (a match that had ended, or one that is not at its tick): the log gets a line, and retry_stale() tries again (the room manager does, every 10 s and when the server stops).
    bool remove_file(const std::string& path);
    /// Tries again to delete the files that could not be deleted; how many are still there. A line for the log says which were deleted at last.
    size_t retry_stale();
    /// A record that was READ and refused (its room is a failed room: another protocol, a map that changed, too old, a replay that disagrees, ...): moved to the folder `refused` of the records' folder
    /// and kept for RestartConfig::refused_keep_ms, not deleted: an owner whose map was missing for a moment, or whose server was a version behind, can move it back. The folder is held under
    /// budget_bytes (the oldest files go first) and a record that alone passes it is deleted. False when the record was deleted instead (a line says why). A record that cannot be READ is deleted at once.
    bool refuse_file(const std::string& path);
    /// Deletes what is older than refused_keep_ms in the folder `refused` (prepare() does it when the server starts); how many files
    size_t purge_refused();
    bool is_stale(const std::string& path) const;
    size_t stale_count() const noexcept { return stale_.size(); }

    /// A line for the server's log (a record that could not be written, a room that was restored); never a key. take_notes() hands them over once.
    void note(std::string line) { notes_.push_back(std::move(line)); }
    std::vector<std::string> take_notes();

    uint64_t used_bytes() const noexcept { return budget_.used(); }
    uint64_t budget_bytes() const noexcept { return budget_.limit(); }
    size_t open_records() const noexcept { return open_; }
    /// Records that ended because a write or a limit failed, since the server started (the statistics of the control interface could show them)
    uint32_t records_failed() const noexcept { return failed_; }

private:
    friend class RestartWriter;
    bool take_lock(std::string& why);
    void release_lock() noexcept;
    net::LogBudget budget_;
    RestartConfig cfg_;
    std::vector<std::string> notes_;
    std::vector<std::string> stale_;                        // the files that could not be deleted (see remove_file)
    size_t open_{0};
    uint32_t failed_{0};
    int lock_fd_{-1};                                       // the folder's lock (a Windows HANDLE is kept in lock_handle_)
    void* lock_handle_{nullptr};
};

}  // namespace ants::server
