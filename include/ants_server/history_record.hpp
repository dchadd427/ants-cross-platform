#pragma once

// What one match came to, as the match history keeps it (docs/REPLAYS.md "The match history"). The record is made from a recording that was played once more with no screen (replay::play(): the engine's own
// MatchResult says what each seat came to), by make_history_record(), which is pure: no clock, no disk, no database. The store that keeps the records is history_store.hpp.
//
// WHO WON depends on the room's game mode (every record says which: `mode`, "highest-score" or "187", the names of sim::game_mode_name and of network protocol 17):
//   highest-score (the original's game): the score decides; equal scores go to the KILLS (enemy ants killed); equal kills too is a DRAW. Losses and hatched ants never break a tie.
//   187: the last side with ants or eggs wins outright (the engine's own end rule, MatchResult::standing); when nobody is left alone (the clock ran out, or all went at once) the most kills wins, and equal kills
//        is a DRAW. 187 has no food and no score: the engine's score IS the kills, and a record keeps that number as `score` (the ranking value, the same as `killed`) and never shows it as a score.
// A side that quit (the quit ended the match) is last; a row of allied seats is one side whose numbers are added up. The engine's own row order is NOT the rule: it breaks a tie in score by the seat (for the
// seat of the screen, and no seat is "local" here) and knows nothing of kills, so the rows are ranked here (rank_history_rows) and the engine's order only supplies which seats make a row.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_ctl/json.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"

namespace ants::server {

/// The layout of a record in JSON (the key "v" of GET /history/<id>). A reader of the answer skips every key it does not know, so a later version can add keys (the details of a match go under "details").
inline constexpr int64_t kHistoryVersion = 1;

/// The two game modes' words, as a record and the list's `mode` filter say them
inline constexpr const char* kHistoryModeScore = "highest-score";
inline constexpr const char* kHistoryMode187 = "187";

/// The shortest match that counts as a play of its map: 30 seconds at 20 ticks a second (the same as SiteStats::kMinTicks)
inline constexpr uint32_t kMapPlayMinTurns = 600;

/// What one seat came to. All numbers are the engine's own (`replay::Outcome::result`).
struct HistorySeat {
    uint8_t seat{0};                     // 0 Green, 1 Red, 2 Blue, 3 Black
    std::string name;                    // the name that the player typed ("" when nothing was typed; a computer player has none: see `bot`)
    std::string bot;                     // a computer player's level ("Medium", "Hard", "Easy"; "Bot" when its name says no level); "" for a person
    int32_t score{0};                    // the seat's own score (a team's row adds its seats' up). Mode 187: the same as `killed` (the ranking value; the mode has no score)
    uint32_t killed{0};                  // enemy ants it killed
    uint32_t lost{0};                    // its own ants that died
    uint32_t hatched{0};                 // ants it hatched
    uint32_t bombs_planted{0};
    uint32_t bombs_defused{0};
    uint32_t fires_lit{0};
    bool left{false};                    // it quit (and its quit ended the match) or dropped out of the match before the end
    bool winner{false};                  // on the winning row, and the match is no draw
};

/// One row of the results: a seat alone, or two allied seats together with their numbers added up. Ranked: the first row won (unless the match is a draw).
struct HistoryRow {
    std::vector<uint8_t> seats;          // one or two
    int32_t score{0};                    // (mode 187: the kills)
    int32_t killed{0};
    int32_t lost{0};
    int32_t hatched{0};
    bool winner{false};
};

struct HistoryRecord {
    std::string id;                      // the replay's file name without ".antsrep"
    int64_t ended_s{0};                  // seconds since 1970 UTC (the time in the file's name)
    std::string map;                     // "TREASURE.LVL"
    uint64_t map_hash{0};                // FNV-1a 64 of the map file the match was played on (not kept in the history's tables; the key of the map's play count with `map`)
    std::string game;                    // "v0.12.0": the game that recorded the match
    uint32_t turns{0};
    bool finished{false};                // the rules ended the match (false: a player left it, or the room's time ran out)
    int quitter{-1};                     // the seat whose quit ended the match, -1 when none did
    std::string mode;                    // kHistoryModeScore or kHistoryMode187
    std::string format;                  // "1v1", "2v1", "2v2", "ffa3", "ffa4" (or "solo"): how the match began (the teams it started with)
    bool draw{false};                    // the first two rows tie on the numbers of the rule above and neither is the quitter's or the side left standing
    bool computers_only{false};          // no person played
    std::vector<HistorySeat> seats;      // the seats that played, in seat order
    std::vector<HistoryRow> rows;        // the results, ranked
    std::string details;                 // JSON text, "" when none: the stats that the replay pass keeps about a match (ants_server/history_details.hpp); stored and given back as it is
    uint32_t seconds() const noexcept { return turns / 20; }
    /// Whether the match counts as one play of its map (docs/SERVER.md "The database", map_plays): a person took part (a match of computer players only does not count) and it ran at least kMapPlayMinTurns,
    /// however it ended (a draw, a quit and a time-out count as the history records them). The site's count of online games has the same two rules (SiteStats).
    bool counts_as_map_play() const noexcept { return !computers_only && turns >= kMapPlayMinTurns; }
    /// "draw", "cut" (a player left, or the time ran out, or the recording stops before the rules ended the match) or "decided"
    const char* result() const noexcept { return draw ? "draw" : (!finished || quitter >= 0) ? "cut" : "decided"; }
};

/// Ranks `rows` (any order, as the engine builds them) by the rule of `mode` and marks the winner: the quitter's row last (`quitter` is a seat or -1), mode 187 the row of `standing` (a seat or 255) first, then
/// by score and kills (highest-score) or by kills (187), the rest in the order they came in. Returns true when the first two rows are equal on the rule's numbers and neither is the quitter's or the one left
/// standing: a draw, in which case no row is marked. Pure; the rows' `winner` is set here.
bool rank_history_rows(const std::string& mode, int quitter, uint8_t standing, std::vector<HistoryRow>& rows);

/// What the engine said about a match that was played again: the record of it. `id` and `ended_s` come from the file's name. Pure: no clock, no disk.
HistoryRecord make_history_record(const std::string& id, int64_t ended_s, const replay::Replay& replay, const replay::Outcome& outcome);

/// The record as JSON. `full` adds the numbers that the list leaves out (bombs, fires, the layout number and the details).
ctl::JsonValue history_record_to_json(const HistoryRecord& record, bool full);

/// The rows of a record as a JSON array text (what the database keeps in `history_matches.rows_json`) and back; false when the text is not such an array
std::string history_rows_to_json(const std::vector<HistoryRow>& rows);
bool history_rows_from_json(const std::string& text, std::vector<HistoryRow>& rows);

/// True for an id that the replay store could have made as a file name ("ants-<MAP>-<YYYYMMDD>-<HHMMSS>Z[-<n>]", for example ants-TREASURE-20261008-143209Z): the only ids that exist
bool valid_history_id(const std::string& id);
/// The file name of the recording of a match: its id and ".antsrep"
std::string history_recording_file(const std::string& id);

}  // namespace ants::server
