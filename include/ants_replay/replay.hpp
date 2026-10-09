#pragma once

// Replay files (.antsrep). A lock-step match is its start data plus the commands of its turns, so a recording is exactly that, with a few state hashes that say whether another
// machine plays it out the same way. docs/REPLAYS.md says how to record and read one; docs/replays/DESIGN.md is the design this follows (the parts that are built: the file, the
// recorder, the player that checks a file and lists its orders; watching a replay inside the game is not built). This header is the file itself: the data, the writer and the strict reader.
//
// A file is a chunked container like PNG: a reader skips what it does not know, a file that was cut still plays up to the cut, and no library is needed. All numbers are little endian.
//
//   file  := magic chunk*                  magic = 89 41 52 50 4C 0D 0A 1A (a transfer that changes line ends is noticed)
//   chunk := tag[4] u32 length payload u32 crc32(tag + payload)       a tag whose first letter is UPPER case is critical (a reader that does not know it refuses the file), lower case is skipped
//   HEAD  := u16 format_version, u16 engine_rules, then fields { varint id, varint length, bytes }; unknown ids are skipped
//   CMDS  := u32 first_turn, u32 count, then count records { varint gap, the command in its wire form (ants_sim/command.hpp, 8 + 4 n bytes) }
//            gap = turns since the record before it (the first record: since first_turn); a chunk spans at most 30 s of game time and 4,096 commands
//   hash  := u32 period, then u32 values: the low 32 bits of the state hash after period, 2 period, 3 period ... turns
//   ENDS  := u32 total_turns, u8 flags (bit 0: the engine's rules ended the match), u64 state hash after the last turn; the last chunk, nothing follows it
//   live  := u32 turns       (optional, lower case: an older reader skips it) only in an incomplete file, the snapshot of a match that is still being played (encode_snapshot): the turns it had run when the
//            snapshot was made, which can be more than the last command and the last hash say
//
// Order: HEAD, then CMDS and hash chunks (the commands in turn order), then live (snapshots only) or ENDS. A file without ENDS is incomplete (a copy that was cut short: the game writes a file whole, when the match is
// over or left; or a snapshot of a match that still runs, which says so with its live chunk): it plays to its last full chunk, and a snapshot to its live turns. Turn t is the t-th call of the engine's tick(): the commands of turn t are applied, in the order they stand, and then the tick runs; commands at turn total_turns come after the
// last tick (a Quit that ends a match). `engine_rules` is net::kProtocolVersion of the recorder: by the repository's own rule that number moves with every change of what a state hash can be,
// so it IS the version of the rules, and a replay is played only by a build that has the same number (an older or newer file still has a readable header).
//
// The reader checks everything before it uses it: the size of the file, every length against the bytes that are left, every checksum, every count and number against its limit, every text,
// every command with the decoder that the network uses. A damaged file is refused with the chunk named, never a crash.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_net/protocol.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_sim/start_teams.hpp"

namespace ants::replay {

inline constexpr uint16_t kFormatVersion = 1;
inline constexpr uint32_t kHashPeriodTurns = 100;      // the state hash is kept after every 100th turn (5 s of play)
inline constexpr size_t kMaxFileBytes = 1u << 20;      // 1 MiB: a heavy half hour of four people is under 200 KiB
inline constexpr uint32_t kMaxTurns = 1u << 21;        // 29 hours of play: more is not a match
inline constexpr size_t kMaxHeadBytes = 4096;
inline constexpr size_t kMaxTextBytes = 64;            // a text field of the head (a player's name is at most 32)
inline constexpr size_t kMaxChunkCommands = 4096;      // commands in a CMDS chunk
inline constexpr uint32_t kChunkTurns = 30 * net::kTurnsPerSecond;   // turns that a CMDS chunk spans at most
inline constexpr uint8_t kNoSeat = 255;

/// The rules number of the simulation: it changes ONLY when a match plays out differently (a golden hash moves: the engine, the map reader, the original's tables). A file says the number of the build that
/// recorded it (the head's field `sim_rules`) and plays on a build with the same number, whatever the network protocol did meanwhile: a protocol bump that leaves the simulation alone strands no file.
/// tests/test_replay (RP7.x) fail when the reference match's final hash changes and this number does not.
inline constexpr uint16_t kSimRules = 1;

/// What the start of the match was: with the commands it is the whole match.
struct Header {
    uint16_t format_version{kFormatVersion};
    uint16_t engine_rules{net::kProtocolVersion};      // net::kProtocolVersion of the recorder (what the room spoke; kept for information: a file plays on its sim_rules)
    uint16_t sim_rules{kSimRules};                     // kSimRules of the recorder; 0 in a file that does not say (made before the field: sim_rules_of() asks the table below)
    std::string game_version;                          // "v0.9.2": the game that wrote the file
    std::string build_id;
    std::string venue;                                 // "local game" or "network game": where it was played
    std::string map_name;                              // the map's file name as the match named it ("TREASURE.LVL")
    uint64_t map_hash{0};                              // FNV-1a 64 of the map file (net::hash_file): another file under the same name is another map
    uint32_t seed{0};
    uint8_t roster{0};                                 // bit p: seat p takes part
    bool fog{false};
    std::array<std::string, sim::MAX_PLAYERS> names;   // printable ASCII, at most 32 characters (the game makes a '?' of any other); empty: the colour word
    sim::StartTeams teams;                             // the teams that the match started with (applied before the first tick)
    uint8_t recorder_seat{kNoSeat};                    // the seat of the machine that wrote the file (kNoSeat: none)
    uint32_t hash_period{kHashPeriodTurns};
};

struct TimedCommand {
    uint32_t turn{0};
    sim::Command command;
};

struct Replay {
    Header head;
    std::vector<TimedCommand> commands;                // in the order they were applied
    std::vector<uint32_t> hashes;                      // hashes[i]: low 32 bits of the state hash after (i + 1) * head.hash_period turns
    bool complete{false};                              // the ENDS chunk was there
    uint32_t total_turns{0};                           // the ticks that were run; an incomplete file: the last turn that it has (the largest of its last command, its hashes and its live chunk)
    bool match_over{false};                            // the engine's rules ended the match
    uint64_t final_hash{0};                            // the engine's state hash (StateHash::total) after the last turn and the last command; complete files only
};

/// The rules number of the simulation that `head` needs: its own `sim_rules`, else (a file made before the field) the one that the table of protocol numbers gives, else 0 (unknown: no build claims
/// it). The table (replay.cpp) holds the protocol numbers that are known to play a match out the same way, each proved with a reference file played under the newer engine.
uint16_t sim_rules_of(const Header& head) noexcept;
/// True when this build plays `head`'s match exactly as it was played: sim_rules_of(head) == kSimRules
bool plays_here(const Header& head) noexcept;

/// CRC-32 (IEEE 802.3, the one of zlib and PNG: CRC-32 of "123456789" is 0xCBF43926); `crc` is the value so far (0 to start)
uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0) noexcept;

/// The bytes of the file. Empty with `error` set when the replay cannot be written: a command that the wire form cannot hold, a turn out of order or past the end, a text that is
/// not printable, a file that would pass kMaxFileBytes.
std::vector<uint8_t> encode(const Replay& replay, std::string& error);

/// The bytes of a snapshot of a match that still runs: an INCOMPLETE replay (no ENDS; `replay.complete` must be false, the other end fields are not looked at) with the optional chunk `live` that says `turns`, the
/// ticks the match had run when it was taken. `turns` must be at least the turn of the last command and at least hashes * period, and at most kMaxTurns (decode() refuses the file otherwise); empty with `error` set
/// when that is not so, the replay is complete, or the file would be longer than kMaxFileBytes. decode() of it gives complete = false and total_turns = max(last command turn, hashes * period, turns).
std::vector<uint8_t> encode_snapshot(const Replay& replay, uint32_t turns, std::string& error);

/// Reads a file. True: `out` holds it (check `out.complete`: a file without ENDS is a replay that stops where it was cut). False: it is damaged, foreign, or made by a newer format, and `error`
/// says what and in which chunk. Nothing of `out` is meaningful then.
bool decode(const uint8_t* data, size_t size, Replay& out, std::string& error);

}  // namespace ants::replay
