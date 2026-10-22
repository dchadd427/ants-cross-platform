#pragma once

// The map of deaths of a match (docs/REPLAYS.md "The map of deaths"): where each ant died, kept in a small field of its own next to the `details` of a match's record, with a cap of its own
// (kDeathsMaxBytes), so that the cap of the details (kDetailsMaxBytes) is not touched and the map is kept even when the details drop their list of kills. Pure: no clock, no disk, no database. The page
// (web/death_map.js) reads it as data from a server: every number is clamped, every character is checked.
//
// Layout v1 (the JSON object that GET /history/<id> gives under "deaths"):
//   v        1
//   w, h     the map's size in tiles
//   n        how many ants died (all of them, whatever the form below)
//   k        "x": every ant by itself (the list `d`), or "b": counts in squares (`s` tiles a side) and `f`, `l`: the first and the last kill of a seat as single ants
//   d        the packed text: base64url letters (A-Z a-z 0-9 - _), six bits each.
//            k = "x": 9 letters for each ant, in the order they died: x (2 letters, 12 bits), y (2), second of the match (3, 18 bits, never more than 262143), then 2 letters of flags: the ant's team
//                     (bits 0-1), its type (bits 2-4: a Worker is 0, then Bomber, Fire, Thief, Combat, Swimmer), drowned (bit 5), the team of the last damage (bits 6-8: 0-3, 7 = nobody).
//            k = "b": 8 letters for each (square, team, cause): the square's x and y in squares (2 letters each), 1 letter of flags (team in bits 0-1, drowned in bit 2) and the count (3 letters). Sorted
//                     by y, x, team, drowned. `f` and `l` are one ant each in the 9-letter form (the first and the last whose death a team is to blame for), left out when there is none.
//   s        (k = "b") the side of a square in tiles: 2, 4, 8 ... the smallest that makes the text fit
// A match with no death, and one the pass could not count, has no field at all (the column is empty and the page says so in one line). The text is at most kDeathsMaxBytes, always: a match with more deaths
// than fit one by one (about 330) keeps the squares, so that every death is still counted and the cap is never passed.
//
// The same packing carries the sum of the matches of a map (`add_up_deaths`, GET /history/deaths?map=NAME): k = "b" with s as small as lets it fit kDeathsSumMaxBytes, and "matches" beside it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_replay/match_stats.hpp"

namespace ants::server {

inline constexpr int64_t kDeathsVersion = 1;
inline constexpr size_t kDeathsMaxBytes = 3000;           // the field of one match, as JSON text
inline constexpr size_t kDeathsSumMaxBytes = 24000;       // the sum of the matches of a map, as JSON text (3,000 squares at most)
inline constexpr size_t kDeathsNone = 7;                  // the flag value of "no team to blame"

/// One ant that died: its tile, the second of the match, its team, type (0 Worker, 1 Bomber, 2 Fire, 3 Thief, 4 Combat, 5 Swimmer), whether it drowned, and the team of the last damage (kDeathsNone: none)
struct Death {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t second{0};
    uint8_t team{0};
    uint8_t type{0};
    bool drowned{false};
    uint8_t killer{static_cast<uint8_t>(kDeathsNone)};
};

/// The ants that died on one square (x, y in squares), of one team, by drowning or not
struct DeathSquare {
    uint32_t x{0};
    uint32_t y{0};
    uint8_t team{0};
    bool drowned{false};
    uint32_t count{0};
};

/// What a field holds, read back (the sum of a map reads every match's field)
struct DeathsView {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t total{0};                                    // `n`
    uint32_t side{1};                                     // tiles of a square (1 for the single ants)
    bool squares{false};
    std::vector<Death> ants;                              // k = "x"
    std::vector<DeathSquare> cells;                       // k = "b"
};

/// The field of a counted match as JSON text; "" when there is nothing to keep (the pass did not count it, nobody died, the map is too large to name a tile)
std::string make_history_deaths(const replay::MatchStats& stats);

/// Reads a field back; false for text that is not a field of this version (the sum of a map skips such a row)
bool parse_history_deaths(const std::string& text, DeathsView& out);

/// The packed forms alone (tests, and the sum)
std::string pack_deaths(const std::vector<Death>& ants);
std::string pack_squares(const std::vector<DeathSquare>& cells);
bool unpack_deaths(const std::string& packed, std::vector<Death>& out);
bool unpack_squares(const std::string& packed, std::vector<DeathSquare>& out);

/// The sum of the fields of many matches of one map (all on a map of `width` x `height` tiles; a field of another size is not added): the JSON text {"v", "map", "matches", "w", "h", "n", "k":"b", "s", "d"}.
/// `matches` is how many fields were added. "" when there is nothing to add.
std::string add_up_deaths(const std::string& map, uint32_t width, uint32_t height, const std::vector<std::string>& fields);

}  // namespace ants::server
