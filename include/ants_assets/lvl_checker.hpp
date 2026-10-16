#pragma once

// The original-safe profile of the map checker: whether the original game (the Ants.exe of 1998) loads a map without refusing it or reading outside its tables,
// and finds in it what the map shows (four hills and the starting ants of the four teams). It is stricter than "the remake plays it" (LevelData::validate): every
// file that the remake refuses or plays with a warning has a Fatal or a Warning here too. Each finding has a stable code, a severity and one sentence in plain
// words with the cell it is about; "Works in the original game" is true when the profile finds no Fatal and no Warning (notes do not count). The rules, the
// addresses of the original's code that each one comes from, and the results of the library: docs/MAP_FORMAT.md, "The original-safe profile".

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
    FileTooLarge,            // file_too_large: more bytes than the limits of the place that reads it
    FileCutShort,            // file_cut_short: the file ends before block 4: the original stops with "System read file error."
    FileCutInBlock4,         // file_cut_in_block_4: the original keeps the waypoints it read and leaves the egg stock to chance
    EggWordMissing,          // egg_word_missing: the file ends before its egg word: the egg stock is left to chance
    FoodBlockEndedEarly,     // food_block_ended_early: block 2 ends at a pile without stages, and the rest of the file is read from the bytes behind it
    // the level
    VersionNot8,             // version_not_8: the original plays version 8 only
    ModeNot1,                // mode_not_1 (a note): the original never reads the mode
    DescriptionTooLong,      // description_too_long: 30 characters leave no zero byte to end the text
    GridEmpty,               // grid_empty: no cell
    GridTooLarge,            // grid_too_large: more than 256 rows or columns
    LayersNotGrid,           // layers_not_grid: a level in memory whose layers do not hold rows x columns cells (no file has one)
    DictionaryTooLarge,      // dictionary_too_large: more than 1,344 names overrun the original's table
    TileOutsideDictionary,   // tile_outside_dictionary: the original reads past its tables
    GroundMissing,           // ground_missing: a layer-1 cell holds the empty tile 0x7FFE: the original reads its terrain table past its end
    StartMarkerOutsideMap,   // start_marker_outside_map: the original writes through the row and column without a check
    FoodPileOutsideMap,      // food_pile_outside_map: the same
    FoodPileWithoutStages,   // food_pile_without_stages: the original stops reading block 2 there
    HillMissing,             // hill_missing: a team without a hill
    HillWithoutEntrance,     // hill_without_entrance: hill cells, none of them the entrance
    HillsSeveral,            // hills_several: more than one hill of a colour
    StartingAntsMissing,     // starting_ants_missing: a team without a start marker
    HillNotWhole,            // hill_not_whole (a note): hill cells that are not the 4 x 4 hill around its entrance
    TileNotDrawn,            // tile_not_drawn (a note): tiles that the dictionary names "." and that the original does not load
    FoodPileNotShown,        // food_pile_not_shown (a note): a pile whose first stage tile the original does not load: no pile is placed
    ObjectOutsideMap,        // object_outside_map (a note): plants and waypoints outside the grid, which the original ignores
};

/// The stable text of a code ("hill_missing", ...)
const char* to_string(OriginalSafeCode code) noexcept;

/// One finding of the profile, as the editor and the server show it
struct OriginalSafeIssue {
    OriginalSafeCode code{OriginalSafeCode::FileUnreadable};
    LevelProblemSeverity severity{LevelProblemSeverity::Fatal};   // Fatal: the original refuses the file or reads outside its tables; Warning: it loads the map and
                                                                  // plays it otherwise than the map shows; Info: a note, which does not stop "works in the original game"
    std::string message;   // one sentence in plain words ("Blue has no hill", "one food pile stands outside the map (row 12, column 40)")
    int32_t row{-1};       // the cell it is about (the first, when it covers several), -1 when it is about no cell
    int32_t column{-1};
    int8_t team{-1};       // the team it is about (0 green, 1 red, 2 blue, 3 black), -1 when none
    uint32_t count{1};     // how many cells, records or piles it covers
};

/// The profile's answer: every finding, in a fixed order (the file, the header, the grid, the tiles, the start markers, the food, the hills and the teams, the notes)
struct OriginalSafeReport {
    std::vector<OriginalSafeIssue> issues;

    bool works_in_original_game() const noexcept;      // no Fatal and no Warning
    bool has(OriginalSafeCode code) const noexcept;
    const OriginalSafeIssue* first(OriginalSafeCode code) const noexcept;
    std::string describe() const;                      // one line each: "<fatal|warning|note> <code>: <message>"
};

/// The profile of a level: the editor's map in memory, or a level that the reader made (its load_notes say what its file left out).
OriginalSafeReport check_original_safe(const LevelData& level);

/// The profile of a file: what the reader refuses (with the reader's sentence) or, for a file that loads, the profile of its level.
OriginalSafeReport check_original_safe(const uint8_t* data, size_t size, const LevelLimits& limits = LevelLimits{});

}  // namespace ants::assets
