#pragma once

// Replay files (.antsrep). A lock-step match is its start data plus the commands of its turns, so a recording is exactly that, with a few state hashes that say whether another
// machine plays it out the same way. docs/REPLAYS.md says how to record, read and watch one. This header is the file itself: the data, the writer and the strict reader.
//
// A file is a chunked container like PNG: a reader skips what it does not know, a file that was cut still plays up to the cut, and no library is needed. All numbers are little endian.
//
//   file  := magic chunk*                  magic = 89 41 52 50 4C 0D 0A 1A (a transfer that changes line ends is noticed)
//   chunk := tag[4] u32 length payload u32 crc32(tag + payload)       a tag whose first letter is UPPER case is critical (a reader that does not know it refuses the file), lower case is skipped
//   HEAD  := u16 format_version, u16 engine_rules, then fields { varint id, varint length, bytes }; unknown ids are skipped (field 16: sim_rules, u16; field 17: the game mode, u8, written only when it is not 0 (the
//            original's game) and then always with a sim_rules that only a mode of its own has (kSimRulesMode187), so that a build that skips field 17 refuses the file for its rules number)
//   CMDS  := u32 first_turn, u32 count, then count records { varint gap, the command in its wire form (ants_sim/command.hpp, 8 + 4 n bytes) }
//            gap = turns since the record before it (the first record: since first_turn); a chunk spans at most 30 s of game time and 4,096 commands
//   hash  := u32 period, then u32 values: the low 32 bits of the state hash after period, 2 period, 3 period ... turns
//   ENDS  := u32 total_turns, u8 flags (bit 0: the engine's rules ended the match), u64 state hash after the last turn; the last chunk, nothing follows it
//   live  := u32 turns       (optional, lower case: an older reader skips it) only in an incomplete file, the snapshot of a match that is still being played (encode_snapshot): the turns it had run when the
//            snapshot was made, which can be more than the last command and the last hash say
//   tmap  := the bytes of the map file, exactly (optional, lower case: an older reader skips it, and the format number stays 1) for a match on a map that the server keeps only for a while (a test map): written
//            right after HEAD, so that a copy that was cut still has it; at most kMaxEmbeddedMapBytes, at most one, and its FNV-1a 64 (net::hash_bytes) must be the head's map_hash. Never in a snapshot.
//
// Order: HEAD, then tmap (when there is one), then CMDS and hash chunks (the commands in turn order), then live (snapshots only) or ENDS. A file without ENDS is incomplete (a copy that was cut short: the game writes a file whole, when the match is
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
inline constexpr size_t kMaxFileBytes = 1u << 20;      // 1 MiB: a heavy half hour of four people is under 200 KiB (a click on a big selection adds up to 6.4 KiB: docs/CONTROLS.md). It is the size of the MATCH: a file that holds a map (tmap) may be that much longer than the map's chunk (max_file_bytes)
inline constexpr size_t kChunkFrameBytes = 4 + 4 + 4;  // a chunk's tag, length and checksum
inline constexpr size_t kMaxEmbeddedMapBytes = 1u << 20;   // 1 MiB: a map of 65,536 cells (the most that a room plays) is about 0.8 MB, a Classic map at most 512 KB
inline constexpr size_t kMaxFileBytesWithMap = kMaxFileBytes + kMaxEmbeddedMapBytes + kChunkFrameBytes;   // the longest file there can be: 2 MiB and the frame of the map's chunk (a reader refuses more before it reads a byte)
/// The longest file that holds `embedded_map_bytes` bytes of map (0: no tmap chunk): the match's kMaxFileBytes plus the map's whole chunk
constexpr size_t max_file_bytes(size_t embedded_map_bytes) noexcept { return kMaxFileBytes + (embedded_map_bytes == 0 ? size_t{0} : embedded_map_bytes + kChunkFrameBytes); }
inline constexpr uint32_t kMaxTurns = 1u << 21;        // 29 hours of play: more is not a match
inline constexpr size_t kMaxHeadBytes = 4096;
inline constexpr size_t kMaxTextBytes = 64;            // a text field of the head (a player's name is at most 32)
inline constexpr size_t kMaxChunkCommands = 4096;      // commands in a CMDS chunk
inline constexpr uint32_t kChunkTurns = 30 * net::kTurnsPerSecond;   // turns that a CMDS chunk spans at most
inline constexpr uint8_t kNoSeat = 255;

/// The rules number of the simulation: it changes ONLY when a match plays out differently (a golden hash moves: the engine, the map reader, the original's tables). A file says the number of the build that
/// recorded it (the head's field `sim_rules`) and plays on a build with the same number, whatever the network protocol did meanwhile: a protocol bump that leaves the simulation alone strands no file.
/// tests/test_replay (RP7.x) fail when the reference match's final hash changes and this number does not.
/// History: 1 and 2 (game mode 187) were the numbers until the bombs of a map file (the layer-2 ids 129 .. 132) became the planted bombs of their colors (Grid::init_from_level, docs/reverse_engineering/map_file_bombs.md), which moves the plays of
/// the community maps that hold them: the original's game was 3 then and game mode 187 (which plays on those maps too) 4. The loader as the original's (docs/reverse_engineering/map_loader_fidelity.md, v0.12.10) moved the plays of the community maps again: 5 and 6. A file of 1 to 4 is an earlier version's.
/// The rules of the original's game and of game mode 187 are numbered in one row, each number belonging to ONE mode: the next change of either takes the next number of the row (12 now), whichever mode it changes.
/// Since v0.14 the original's game (mode 0) is 9: a match also ends when one side is left and nothing is left to score (8, SimulationEngine::set_last_side_rule, docs/GAMEPLAY.md, "The last side standing"), and a fire wall
/// of a map file loads as the original loads it (9, SimulationEngine::set_fire_wall_as_original, docs/reverse_engineering/map_loader_fidelity.md, section 8). The number 8 (the original's game with the last-side
/// rule and the old loader) and the number 5, the original's game as released until v0.13 (ended by the original's CHECKGO alone, with the old loader), are played too, and a recording of either plays as it always did.
inline constexpr uint16_t kSimRules = 9;
inline constexpr uint16_t kSimRulesBeforeFireWall = 8;
inline constexpr uint16_t kSimRulesBeforeLastSide = 5;
/// The rules numbers of a match played by game mode 1, "187" (sim::GameMode::Kills187, docs/GAMEPLAY.md): the head carries the mode in field 17 (written only when it is not 0) and one of these numbers in field 16. A build that
/// predates the mode skips field 17 and refuses the file for its rules number (it plays 5 only), so no older build plays a 187 match as the original's game; every file of mode 0 plays wherever it did. A change of the rules after
/// this takes the next number that is none of these (the static_assert below stops the one that would collide).
/// kSimRulesMode187Heal (6) is 187 as it was first released: the hill works, an ant that enters it is healed, an ant at 1 hp walks home and the waiting ring queues. kSimRulesMode187BeforeFireWall (7) is 187 with the hill
/// closed: an ant cannot enter its hill (docs/GAMEPLAY.md, "187"). kSimRulesMode187BeforeEndFix (10) is the same with a fire wall of a map file loaded as the original loads it. kSimRulesMode187 (11) is 187 now: the same, and
/// the end rule counts the teams that began and a team that has only eggs that cannot hatch is not standing (SimulationEngine::set_end_fix_187). This build plays all four: a file of 6 plays with the hill open
/// (SimulationEngine::set_hill_open_187, replay::begin_match), a file of 6 or 7 with the old loader, a file of 6, 7 or 10 without the end-rule fixes, and a match is recorded under kSimRulesMode187 (sim_rules_for_mode).
/// The last-side rule (8) is the original's game's: 187 has its own end rule and never reads it. The loader knows no mode, so its change took one number of each mode (9 and 10), not one for both.
inline constexpr uint16_t kSimRulesMode187Heal = 6;
inline constexpr uint16_t kSimRulesMode187BeforeFireWall = 7;
inline constexpr uint16_t kSimRulesMode187BeforeEndFix = 10;
inline constexpr uint16_t kSimRulesMode187 = 11;
/// The first number whose loader reads a fire wall of a map file as the original does (Grid::init_from_level): every number from here on has it, because the numbers are issued in one row and each change keeps the ones
/// before it; the numbers below it (5 to 8) keep the remake's name-based overlay on a named fire wall. replay::begin_match asks fire_wall_as_original_rules.
inline constexpr uint16_t kSimRulesFireWallFirst = 9;
/// The newest number of the row, whichever mode it belongs to: a file of a higher number is a newer build's, one of a lower number that this build does not play is an older one's
inline constexpr uint16_t kSimRulesNewest = kSimRulesMode187 > kSimRules ? kSimRulesMode187 : kSimRules;
static_assert(kSimRulesMode187BeforeEndFix != kSimRules && kSimRulesMode187BeforeEndFix != kSimRulesMode187 && kSimRulesMode187BeforeEndFix != kSimRulesMode187Heal && kSimRulesMode187BeforeEndFix != kSimRulesMode187BeforeFireWall
                  && kSimRulesMode187BeforeEndFix != kSimRulesBeforeFireWall && kSimRulesMode187BeforeEndFix != kSimRulesBeforeLastSide, "a number belongs to one mode");
static_assert(kSimRulesMode187Heal != kSimRules && kSimRulesMode187 != kSimRules && kSimRulesMode187 != kSimRulesMode187Heal && kSimRulesMode187BeforeFireWall != kSimRules
                  && kSimRulesMode187BeforeFireWall != kSimRulesMode187 && kSimRulesMode187BeforeFireWall != kSimRulesMode187Heal && kSimRulesBeforeFireWall != kSimRules
                  && kSimRulesBeforeFireWall != kSimRulesMode187 && kSimRulesBeforeFireWall != kSimRulesMode187Heal && kSimRulesBeforeFireWall != kSimRulesMode187BeforeFireWall,
              "a mode of its own has rules of its own");
static_assert(kSimRulesBeforeLastSide != kSimRules && kSimRulesBeforeLastSide != kSimRulesBeforeFireWall && kSimRulesBeforeLastSide != kSimRulesMode187Heal && kSimRulesBeforeLastSide != kSimRulesMode187BeforeFireWall
                  && kSimRulesBeforeLastSide != kSimRulesMode187, "a number belongs to one mode");
static_assert(kSimRulesNewest >= kSimRules && kSimRulesNewest >= kSimRulesMode187 && kSimRulesNewest > kSimRulesMode187BeforeEndFix && kSimRulesNewest > kSimRulesBeforeFireWall && kSimRulesNewest > kSimRulesMode187BeforeFireWall && kSimRulesNewest > kSimRulesMode187Heal && kSimRulesNewest > kSimRulesBeforeLastSide,
              "kSimRulesNewest is the newest number (the app calls a file of a lower number older and a higher one newer)");
static_assert(kSimRulesFireWallFirst > kSimRulesBeforeFireWall && kSimRulesFireWallFirst > kSimRulesMode187BeforeFireWall && kSimRules >= kSimRulesFireWallFirst && kSimRulesMode187BeforeEndFix >= kSimRulesFireWallFirst && kSimRulesMode187 >= kSimRulesFireWallFirst,
              "the numbers from kSimRulesFireWallFirst on have the loader of the original, the ones before it do not");

/// The rules number that a match of game mode `mode` is recorded under (0: no mode of that number exists)
constexpr uint16_t sim_rules_for_mode(uint8_t mode) noexcept {
    return mode == static_cast<uint8_t>(sim::GameMode::HighestScore) ? kSimRules : mode == static_cast<uint8_t>(sim::GameMode::Kills187) ? kSimRulesMode187 : uint16_t{0};
}

/// Whether this build plays a match of game mode `mode` under the rules number `rules`: the original's game under kSimRules, kSimRulesBeforeFireWall or kSimRulesBeforeLastSide, 187 under kSimRulesMode187,
/// kSimRulesMode187BeforeEndFix, kSimRulesMode187BeforeFireWall or kSimRulesMode187Heal (a mode that this build does not know: never)
constexpr bool plays_mode_rules(uint8_t mode, uint16_t rules) noexcept {
    return mode == static_cast<uint8_t>(sim::GameMode::HighestScore) ? (rules == kSimRules || rules == kSimRulesBeforeFireWall || rules == kSimRulesBeforeLastSide)
         : mode == static_cast<uint8_t>(sim::GameMode::Kills187) ? (rules == kSimRulesMode187 || rules == kSimRulesMode187BeforeEndFix || rules == kSimRulesMode187BeforeFireWall || rules == kSimRulesMode187Heal)
         : false;
}

/// Whether a match of the rules number `rules` loads a fire wall of its map file as the original does (SimulationEngine::set_fire_wall_as_original): every number from kSimRulesFireWallFirst on, in either mode
constexpr bool fire_wall_as_original_rules(uint16_t rules) noexcept {
    return rules >= kSimRulesFireWallFirst;
}

/// What the start of the match was: with the commands it is the whole match.
struct Header {
    uint16_t format_version{kFormatVersion};
    uint16_t engine_rules{net::kProtocolVersion};      // net::kProtocolVersion of the recorder (what the room spoke; kept for information: a file plays on its sim_rules)
    uint16_t sim_rules{kSimRules};                     // the rules number of the recorder (kSimRules for the original's game); 0 in a file that does not say (made before the field: sim_rules_of() asks the table below)
    std::string game_version;                          // "v0.9.2": the game that wrote the file
    std::string build_id;
    std::string venue;                                 // "local game" or "network game": where it was played
    std::string map_name;                              // the map's file name as the match named it ("TREASURE.LVL")
    uint64_t map_hash{0};                              // FNV-1a 64 of the map file (net::hash_file): another file under the same name is another map
    uint32_t seed{0};
    uint8_t roster{0};                                 // bit p: seat p takes part
    bool fog{false};
    uint8_t mode{0};                                   // sim::GameMode as a byte (head field 17, written only when it is not 0): the rules the match was played by; set_mode() keeps it and sim_rules together
    std::array<std::string, sim::MAX_PLAYERS> names;   // printable ASCII, at most 32 characters (the game makes a '?' of any other); empty: the color word
    sim::StartTeams teams;                             // the teams that the match started with (applied before the first tick)
    uint8_t recorder_seat{kNoSeat};                    // the seat of the machine that wrote the file (kNoSeat: none)
    uint32_t hash_period{kHashPeriodTurns};
    /// The game mode of the match and the rules number that goes with it (sim_rules_for_mode): what a recorder stamps from the Start of a match
    void set_mode(uint8_t game_mode) noexcept {
        mode = game_mode;
        sim_rules = sim_rules_for_mode(game_mode);
    }
};

struct TimedCommand {
    uint32_t turn{0};
    sim::Command command;
};

struct Replay {
    Header head;
    std::vector<uint8_t> embedded_map;                 // the bytes of the map file (the tmap chunk), or empty: the writer puts it in a complete file only (encode_file), never in a snapshot; the reader has checked its hash against head.map_hash
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
/// True when this build plays `head`'s match exactly as it was played: its rules number is one of its game mode (kSimRules, kSimRulesBeforeFireWall or kSimRulesBeforeLastSide with mode 0, kSimRulesMode187, kSimRulesMode187BeforeEndFix, kSimRulesMode187BeforeFireWall or kSimRulesMode187Heal with mode 1). The numbers that this build plays,
/// as a sentence for a refusal: "5, 6, 7, 8, 9, 10 and 11".
bool plays_here(const Header& head) noexcept;
std::string played_rules_text();

/// CRC-32 (IEEE 802.3, the one of zlib and PNG: CRC-32 of "123456789" is 0xCBF43926); `crc` is the value so far (0 to start)
uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0) noexcept;

/// The bytes of the file. Empty with `error` set when the replay cannot be written: a command that the wire form cannot hold, a turn out of order or past the end, a text that is
/// not printable, a file whose match would pass kMaxFileBytes, an embedded map that is longer than kMaxEmbeddedMapBytes or whose hash is not the head's map_hash.
std::vector<uint8_t> encode(const Replay& replay, std::string& error);

/// The bytes of a snapshot of a match that still runs: an INCOMPLETE replay (no ENDS; `replay.complete` must be false, the other end fields are not looked at) with the optional chunk `live` that says `turns`, the
/// ticks the match had run when it was taken. `turns` must be at least the turn of the last command and at least hashes * period, and at most kMaxTurns (decode() refuses the file otherwise); empty with `error` set
/// when that is not so, the replay is complete, or the file would be longer than kMaxFileBytes. `replay.embedded_map` is left out. decode() of it gives complete = false and total_turns = max(last command turn, hashes * period, turns).
std::vector<uint8_t> encode_snapshot(const Replay& replay, uint32_t turns, std::string& error);

/// Reads a file. True: `out` holds it (check `out.complete`: a file without ENDS is a replay that stops where it was cut). False: it is damaged, foreign, or made by a newer format, and `error`
/// says what and in which chunk. Nothing of `out` is meaningful then. A file longer than kMaxFileBytesWithMap, a match part (the file without its tmap chunk) longer than kMaxFileBytes, a second tmap chunk, a tmap
/// chunk that claims more than kMaxEmbeddedMapBytes (refused from its length, before its bytes are looked at), an empty one, or one whose hash is not head.map_hash is refused. A file that was cut inside its tmap chunk is
/// cut like any other: it plays, with no embedded map (what is left of the chunk counts as the map's part of the file, not the match's, at any length).
bool decode(const uint8_t* data, size_t size, Replay& out, std::string& error);

}  // namespace ants::replay
