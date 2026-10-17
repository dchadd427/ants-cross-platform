#pragma once

// The map core for places that cannot link the C++ library: the reader, the checker, the writer, the blank map, the comparison of two levels, the check by kind of map and the
// conversions between the two kinds behind plain data (bytes in, a JSON text or bytes out). The browser module (lvl_core.wasm, src/ants_assets/lvl_core_c.cpp) is these calls and
// nothing else, so the page and the native tools run the one code. Every call is a pure function of its arguments: no state is kept between calls, nothing is thrown (a failure is a status), and the
// result is complete in memory. docs/MAP_FORMAT.md, "In the browser".

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ants::assets::lvl_core {

/// The limits a call applies. NewMap (the default of the page) is LevelLimits::for_new_maps(): at most 90 rows, 130 columns and 10,000 cells, a file of at most 512 KB read,
/// 144 KB written. Game is LevelLimits{}: what the game itself reads (256 a side, no cap on the file). Extended is LevelLimits::for_extended_maps(): a place that also takes
/// Extended maps (version 9), at most 1,024 a side and 262,144 cells, a file of 4 MiB read and 3.5 MiB written; a Classic map (version 8) keeps its 256 a side there.
enum class Profile : uint32_t { NewMap = 0, Game = 1, Extended = 2 };

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

/// The check of the bytes by the kind of map their version word says (include/ants_assets/lvl_checker.hpp, check_map), as JSON: "kind" ("classic": judged by the original-safe profile,
/// or "extended": by the remake profile, which the original cannot load), "judged", "works" (Classic: works in the original game; Extended: works in the new version),
/// "fits_the_place", "headline" ("Works in the original game", "Works in the new version", ...), "badge" ("New version only" for an Extended map, else "") and "findings" (as
/// check_original's, in the checker's order and words). An Extended map under a profile that does not take them (new_map, game) is not judged: the finding
/// extended_not_accepted_here. Always status 0.
Result check_map(const uint8_t* data, size_t size, Profile profile);

/// The canonical bytes of the file (read, then write_level): the same map, in the form the writer keeps. Status 1 with the sentence of lvl_tool when the file does not load,
/// does not play with all four teams, or cannot be written within the limits.
Result rewrite(const uint8_t* data, size_t size, Profile profile);

/// A new map of plain `ground_tile` ground (a tile id below 1,344, not the empty tile), `rows` x `columns` cells, `minutes` long, `eggs` for each team, no hill and no start
/// marker yet; the canonical bytes. Status 1 for an argument out of range, a grid beyond the profile's limits, or a level that cannot be written.
Result blank(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes, Profile profile);

/// The limits of a profile, as JSON, so that a page shows the numbers of the code that holds the map to them: "profile", "allow_extended", "classic" ("rows", "columns", "cells" of a
/// Classic map under it), "extended" (the same and "description_bytes" for an Extended map; null when the profile does not take Extended maps), "read_bytes" and "write_bytes" (null
/// when there is no limit) and "dictionary_names". Status 1 for a profile that does not exist.
Result profile_limits(Profile profile);

/// The bytes made an Extended map (convert_to_extended): every field carried, the anchors made relative, rules 0 (it plays as it did), a copy that is another file (another hash);
/// a file that is Extended already comes back as its canonical bytes. The file is read as an Extended place reads one (either kind, up to 1,024 a side) and the copy is written under
/// the same limits. Status 1 when the file does not load. The payload of a conversion (this and convert_to_classic, status 0): a dword (little endian) with the size of the JSON, the
/// JSON, then the bytes of the new file (none when the map cannot be converted). The JSON: "to" ("extended" or "classic"), "from" (the kind of the file), "ok", "bytes" (the size of
/// the new file, 0 when not ok), "reasons" (why it cannot be converted: each {"code", "message"}, the codes of LevelConversion::reason_codes in its order, and beyond_place_limits
/// when the copy is not within the limits of the place) and "notes" (what to tell the person when it can: each {"code", "message"}). A status 0 with "ok": false is an answer, not a
/// failure: the map stays as it is.
Result convert_to_extended(const uint8_t* data, size_t size);

/// The bytes made a Classic map (convert_to_classic) when they fit: both sides at most 256, at most 90 rows for an Extended map, the dictionary at most 1,344 names, the description
/// at most 30 bytes, no format flags, rules 0 or 1 (a note when 1), every anchor inside a byte, and the copy within the limits of `profile` (a new Classic map: Profile::NewMap). The file
/// is read as an Extended place reads one. The payload is that of convert_to_extended. Status 1 when the file does not load or the profile does not exist.
Result convert_to_classic(const uint8_t* data, size_t size, Profile profile);

/// Why the bytes cannot be a Classic map under `profile` (the reasons of convert_to_classic, the same code path), as JSON: {"needs_extended": true or false, "reasons": [{"code",
/// "message"}]}. A Classic map that fits has no reason. Status 1 when the file does not load or the profile does not exist.
Result needs_extended(const uint8_t* data, size_t size, Profile profile);

/// A new Extended map (version 9, blank_extended_level) of plain `ground_tile` ground, `rows` x `columns` cells, `minutes` long, `eggs` for each team, no hill and no start marker
/// yet; the canonical bytes, under the limits of Profile::Extended. Its rules word is 1 when the map is taller than 90 rows or wider than 256 columns, else 0. Status 1 for an
/// argument out of range or a grid beyond the Extended limits (1,024 a side, 262,144 cells).
Result blank_extended(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes);

/// Do two files hold the same map (same_level of the two levels the reader makes)? JSON {"same": true} or {"same": false, "difference": "..."}; status 1 when either file
/// does not load.
Result same_level(const uint8_t* a, size_t size_a, const uint8_t* b, size_t size_b, Profile profile);

/// The 1,344 tile names (the animations of Table 4 of ants.chd; lvl_tile_names.inc).
std::vector<std::string> tile_names();

}  // namespace ants::assets::lvl_core
