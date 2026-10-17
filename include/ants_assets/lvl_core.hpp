#pragma once

// The map core for places that cannot link the C++ library: the reader, the checker, the writer, the blank map and the comparison of two levels behind plain data
// (bytes in, a JSON text or bytes out). The browser module (lvl_core.wasm, src/ants_assets/lvl_core_c.cpp) is these five calls and nothing else, so the page and the
// native tools run the one code. Every call is a pure function of its arguments: no state is kept between calls, nothing is thrown (a failure is a status), and the
// result is complete in memory. docs/MAP_FORMAT.md, "In the browser".

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ants::assets::lvl_core {

/// The limits a call applies. NewMap (the default of the page) is LevelLimits::for_new_maps(): at most 10,000 cells and 130 a side, a file of at most 512 KB read,
/// 144 KB written. Game is LevelLimits{}: what the game itself reads (256 a side, no cap on the file).
enum class Profile : uint32_t { NewMap = 0, Game = 1 };

/// What a call answers: `status` 0 is the answer, in `payload` (JSON text, or the bytes of a file); 1 is a refusal, and `payload` is one sentence in plain words (a
/// file that does not load, a level that cannot be written, an argument out of range); 2 is a fault of the core or no memory left, with a sentence too.
struct Result {
    uint32_t status{0};
    std::string payload;
};

/// Reads the bytes as a map file and reports on it (JSON, keys in a fixed order): "bytes", "profile", "loaded", "reason" (why it does not load, else ""), "version",
/// "mode", "minutes", "description", "description_bytes", "rows", "columns", "dictionary_names", "tile_ids_used", "eggs", "default_type_tile", "food_piles", "waypoints",
/// "teams" (four: "team", "start_markers", "hill_cells"), "playable" (for all four teams) and "findings" (the reader's and the remake's checks: "severity" fatal, warning
/// or note, "code", "message", "team", "count"). A file that does not load has "loaded": false, a "reason" and the findings that say why. Always status 0.
Result read_report(const uint8_t* data, size_t size, Profile profile);

/// The original-safe profile of the bytes (include/ants_assets/lvl_checker.hpp), as JSON: "judged", "works_in_original_game", "fits_the_place" and "findings" ("severity",
/// "code", "message", "row", "column", "team", "count", "place_limit"), in the checker's order. Always status 0.
Result check_original(const uint8_t* data, size_t size, Profile profile);

/// The canonical bytes of the file (read, then write_level): the same map, in the form the writer keeps. Status 1 with the sentence of lvl_tool when the file does not load,
/// does not play with all four teams, or cannot be written within the limits.
Result rewrite(const uint8_t* data, size_t size, Profile profile);

/// A new map of plain `ground_tile` ground (a tile id below 1,344, not the empty tile), `rows` x `columns` cells, `minutes` long, `eggs` for each team, no hill and no start
/// marker yet; the canonical bytes. Status 1 for an argument out of range, a grid beyond the profile's limits, or a level that cannot be written.
Result blank(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes, Profile profile);

/// Do two files hold the same map (same_level of the two levels the reader makes)? JSON {"same": true} or {"same": false, "difference": "..."}; status 1 when either file
/// does not load.
Result same_level(const uint8_t* a, size_t size_a, const uint8_t* b, size_t size_b, Profile profile);

/// The 1,344 tile names (the animations of Table 4 of ants.chd; lvl_tile_names.inc).
std::vector<std::string> tile_names();

}  // namespace ants::assets::lvl_core
