#pragma once

// The original-safe profile of the map checker: whether a map "works in the original game" (the Ants.exe of 1998). The yardstick, in one sentence: the original
// loads the map without refusing it or reading outside its tables AND plays what the map shows (four hills and the starting ants of the four teams, every food
// pile where the map puts it). It is stricter than "the remake plays it" (LevelData::validate): every file that the remake refuses or plays with a warning has a
// Fatal or a Warning here too. Each finding has a stable code, a severity and one sentence in plain words with the cell it is about. Fatal: the original refuses
// the file, reads outside its tables or walks memory that the file did not fill. Warning: it loads the map and plays it otherwise than the map shows. Note: a
// thing that the disassembly notes say the original tolerates and that has not been tried in the original itself. "Works in the original game" is true when the
// profile finds no Fatal and no Warning (notes do not count; neither does a limit of the place that reads the map, place_limit: the original has none of them).
// The rules, the addresses of the original's code that each one comes from, and the results of the library: docs/MAP_FORMAT.md, "The original-safe profile".
//
// The checker per kind (check_map, below): a Classic map (version 8) is judged by the original-safe profile; an Extended map (version 9), which the original cannot load,
// is judged by the remake profile, "plays in the new version", which is the same rules for what both games need (a hill and starting ants for each team, every tile inside
// the dictionary, every object inside the grid, every food pile with a stage) and none of the original's own quirks. One report answers with the kind, whether it works,
// the headline and the badge by kind, and the findings with stable codes. docs/MAP_FORMAT.md, "The checker per kind".

#include "ants_assets/lvl_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ants::assets {

/// What a finding is about. The text of a code (to_string) is stable: the editor, the server and their tests key on it.
enum class OriginalSafeCode : uint8_t {
    // the file, as the original's loader reads it (check_original_safe of bytes, and the reader's findings that a loaded level keeps in load_notes)
    FileUnreadable,          // file_unreadable: no bytes
    FileTooLarge,            // file_too_large: more bytes than the limits of the place that reads it (a place_limit: the map is not read, report.judged is false)
    FileCutShort,            // file_cut_short: the file ends before block 4: the original stops with "System read file error."
    FileCutInBlock4,         // file_cut_in_block_4 (fatal): the original walks the block-4 slots that the cut left unfilled, and leaves the egg stock to chance
    EggWordMissing,          // egg_word_missing: the file ends before the eggs each team starts with: that number is left to chance
    FoodBlockEndedEarly,     // food_block_ended_early: block 2 ends at a pile without stages, and the rest of the file is read from the bytes behind it
    // the level
    VersionNot8,             // version_not_8: the original plays version 8 only
    ModeNot1,                // mode_not_1 (a note): the original never reads the mode
    DescriptionTooLong,      // description_too_long: 30 bytes leave no zero byte to end the text
    GridEmpty,               // grid_empty: no cell
    GridTooLarge,            // grid_too_large: more than 256 rows or columns (or, as a place_limit, more than the place allows)
    LayersNotGrid,           // layers_not_grid: a level in memory whose layers do not hold rows x columns cells (no file has one)
    DictionaryTooLarge,      // dictionary_too_large: more than 1,344 names overrun the original's table
    TileOutsideDictionary,   // tile_outside_dictionary: the original reads past its tables
    GroundMissing,           // ground_missing (fatal): a layer-1 cell holds the empty tile 0x7FFE: the original reads its terrain table past its end
    StartMarkerOutsideMap,   // start_marker_outside_map: the original writes through the row and column without a check
    FoodPileOutsideMap,      // food_pile_outside_map: the same
    FoodPileWithoutStages,   // food_pile_without_stages: the original stops reading block 2 there
    HillMissing,             // hill_missing: a team without a hill
    HillWithoutEntrance,     // hill_without_entrance: hill cells, none of them the entrance
    HillsSeveral,            // hills_several: more than one hill of a color
    StartingAntsMissing,     // starting_ants_missing: a team without a start marker
    HillNotWhole,            // hill_not_whole (a note): hill cells that are not the 4 x 4 hill around its entrance
    TileNotDrawn,            // tile_not_drawn (a note): tiles that the dictionary names "." and that the original does not load
    FoodPileNotShown,        // food_pile_not_shown (a warning): a pile whose stage tile the original does not load: no pile is placed, nobody can see or harvest it
    ObjectOutsideMap,        // object_outside_map (a note): plants and waypoints outside the grid, which the original ignores
    // an Extended map (version 9)
    NotForOriginal,          // not_for_original (neither a failure nor a warning): the map is an Extended map, which the original game cannot load (it needs a version 8 file)
    // the checker per kind (check_map)
    RowsBeyondWall,          // rows_beyond_wall (a warning, Classic maps): more than 90 rows; the original's path finder never enters the 91st row, so ants cannot cross it
    ExtendedNotAcceptedHere, // extended_not_accepted_here (a place_limit, fatal, not judged): an Extended map given to a place that reads Classic maps only (LevelLimits::allow_extended)
    FormatFlagsUnknown,      // format_flags_unknown (fatal, Extended maps): a flag bit that this reader does not know, so the file is from a later version of the format
    RulesOutOfRange,         // rules_out_of_range (fatal, Extended maps): a rules word other than 0 and 1
    ObjectAnchorOutsideMap,  // object_anchor_outside_map (a note, Extended maps): an object whose anchor lies outside the grid
};

/// The stable text of a code ("hill_missing", ...)
const char* to_string(OriginalSafeCode code) noexcept;

/// One finding of the profile, as the editor and the server show it
struct OriginalSafeIssue {
    OriginalSafeCode code{OriginalSafeCode::FileUnreadable};
    LevelProblemSeverity severity{LevelProblemSeverity::Fatal};   // Fatal: the original refuses the file, reads outside its tables or walks memory the file did not fill;
                                                                  // Warning: it loads the map and plays it otherwise than the map shows; Info: a note (the notes say the
                                                                  // original tolerates it; not tried in the original itself), which does not stop "works in the original game"
    std::string message;   // one sentence in plain words ("Blue has no hill", "one food pile stands outside the map (row 12, column 40)")
    int32_t row{-1};       // the cell it is about (the first, when it covers several), -1 when it is about no cell
    int32_t column{-1};
    int8_t team{-1};       // the team it is about (0 green, 1 red, 2 blue, 3 black), -1 when none
    uint32_t count{1};     // how many cells, records or piles it covers
    bool place_limit{false};   // not a rule of the original but a limit of the place that reads the map (the map maker's 130 columns, its file size): the sentence says so,
                               // and it does not stop "works in the original game" (the map was judged without it), only "fits the place"
};

/// The profile's answer: every finding, in a fixed order (the file, the header, the grid, the tiles, the start markers, the food, the hills and the teams, the notes)
struct OriginalSafeReport {
    std::vector<OriginalSafeIssue> issues;
    bool judged{true};     // false when the file was too large to be read at all: nothing is known of the map

    bool works_in_original_game() const noexcept;      // judged, and no Fatal and no Warning that is a rule of the original (a limit of the place is not one), and not an Extended map
    bool has_no_failure() const noexcept;              // judged, and no Fatal and no Warning that is a rule of the profile that made the report (a limit of the place is not one)
    bool not_for_original() const noexcept;            // an Extended map (version 9): not a fault of the map, the original cannot load the kind (NotForOriginal, severity Info, the only finding)
    bool fits_the_place() const noexcept;              // no limit of the place (LevelLimits: a new map's grid, dictionary and file size) is broken
    bool has(OriginalSafeCode code) const noexcept;
    const OriginalSafeIssue* first(OriginalSafeCode code) const noexcept;
    std::string describe() const;                      // one line each: "<fatal|warning|note|limit> <code>: <message>"
};

/// The profile of a level: the editor's map in memory, or a level that the reader made (its load_notes say what its file left out). An Extended map (version 9) is not judged by
/// the original's rules: its one finding is NotForOriginal, a neutral outcome (severity Info, so no Fatal and no Warning), and works_in_original_game() is false.
OriginalSafeReport check_original_safe(const LevelData& level);

/// The profile of a file: what the reader refuses (with the reader's sentence) or, for a file that loads, the profile of its level. A file whose version word is 9 is an
/// Extended map whatever `limits` allow: it is answered with NotForOriginal alone, without reading it.
OriginalSafeReport check_original_safe(const uint8_t* data, size_t size, const LevelLimits& limits = LevelLimits{});


// ------------------------------------------------------------------------------------------------------------------------------------------------------
// The checker per kind
// ------------------------------------------------------------------------------------------------------------------------------------------------------

/// The two kinds of map: a Classic map is a version 8 file (works in the original game and in this remake), an Extended map a version 9 file (this remake only; the original cannot
/// load it, which is no fault of the map).
enum class MapKind : uint8_t { Classic, Extended };

/// "classic" or "extended": stable
const char* to_string(MapKind kind) noexcept;

/// The words of a kind, stable: the editor and the pages show them
constexpr const char* kHeadlineWorksInOriginal = "Works in the original game";
constexpr const char* kHeadlineFailsInOriginal = "Does not work in the original game";
constexpr const char* kHeadlineWorksInNewVersion = "Works in the new version";
constexpr const char* kHeadlineFailsInNewVersion = "Does not work in the new version";
constexpr const char* kHeadlineNotChecked = "Not checked";
constexpr const char* kBadgeNewVersionOnly = "New version only";       // the badge of an Extended map; a Classic map has none

/// The one answer of the checker for a map of either kind: the kind, whether it works (Classic: in the original game, by the original-safe profile; Extended: in the new version, by
/// the remake profile), the headline and the badge by kind, and the findings. Classic findings are exactly those of check_original_safe, except that a grid beyond 256 a side says
/// "too big for a Classic map (256 at most)... make it an Extended map?". An Extended map's findings are `not_for_original` (a note: the original cannot load the kind) and those of the
/// remake profile, which uses the same codes and the same words as the original-safe profile for the rules both share ("Blue has no hill").
struct MapCheckReport {
    MapKind kind{MapKind::Classic};
    OriginalSafeReport report;       // the findings; `judged` false when the map could not be read at all
    bool works{false};               // Classic: works in the original game; Extended: works in the new version (no Fatal and no Warning, a note or a limit of the place does not count)
    bool fits_the_place{true};       // no limit of the place (LevelLimits) is broken
    std::string headline;            // kHeadline...: by kind and outcome
    std::string badge;               // kBadgeNewVersionOnly for an Extended map, empty for a Classic map

    bool has(OriginalSafeCode code) const noexcept { return report.has(code); }
    std::string describe() const { return report.describe(); }
};

/// The checker's own sentence for a description of `bytes` bytes that is too long for the kind (the finding `description_too_long`: a warning for a Classic map of 30 bytes or more,
/// fatal for an Extended map of more than 255), so that whoever sets a description says the same words as the check of the map.
std::string description_too_long_text(MapKind kind, size_t bytes);

/// The kind of a version word: 9 is an Extended map, anything else is judged as a Classic map (a file that says another version is a Classic map that fails `version_not_8`).
MapKind kind_of_version(uint32_t version) noexcept;

/// The check of a level in memory (the editor's map, or a level the reader made), by its kind. `limits` are the limits of the place: a grid, a dictionary beyond them is a finding with
/// place_limit (the map is judged all the same, `fits_the_place` is false); an Extended map in a place without LevelLimits::allow_extended is not judged
/// (`extended_not_accepted_here`).
MapCheckReport check_map(const LevelData& level, const LevelLimits& limits = LevelLimits{});

/// The check of a file's bytes, by the kind its version word says. A file that the reader refuses is reported with its own sentence (and not judged when it was not read at all).
MapCheckReport check_map(const uint8_t* data, size_t size, const LevelLimits& limits = LevelLimits{});

}  // namespace ants::assets
