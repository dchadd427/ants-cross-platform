#pragma once

// The writer of the .lvl format: a LevelData as the canonical bytes of a version 8 file, the comparison of two levels, what a rewrite changes in a file, and the
// dictionary and the blank map of a new level. The format, the canonical form, the limits and the round-trip law that the writer keeps: docs/MAP_FORMAT.md.

#include "ants_assets/lvl_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ants::assets {

/// What write_level made of a level: the bytes of the file, or the one sentence that says why it was refused.
struct LevelWriteResult {
    bool ok{false};
    std::vector<uint8_t> bytes;
    std::string error;
};

/// The canonical bytes of `level`: version 8, the parts in the order the original's loader reads them (docs/MAP_FORMAT.md), every field as the level holds it, the
/// bytes after the end of the description and of each dictionary name zero, nothing after the egg word. Refused (ok false, `error` says why) is a level that the
/// reader cannot have made, so that what the writer returns always reads back (LVLParser::load_from_memory with the same limits) as the same level (same_level):
/// another version, a grid without a cell or beyond `limits`, layers that are not rows x columns, a description of more than 30 bytes or with a zero byte, an
/// empty dictionary or one beyond `limits`, a name of more than 11 bytes or with a zero byte, a tile_type_count that is not the number of names less one, a block
/// with more than 65,535 entries, a start marker or plant whose team is not that of its tile, a food pile without stages or with more than 65,535, a waypoint
/// without a trigger (flag 0) that holds an interval or chances, a file beyond `limits`. The writer reads its own bytes back before it answers: a level that
/// does not read back the same is refused too (that would be a fault of the writer).
LevelWriteResult write_level(const LevelData& level, const LevelLimits& limits = LevelLimits{});

/// Do two levels hold the same map? Every field that a file holds is compared (the five chances of a waypoint by their bits, so that a NaN equals itself), the
/// reader's findings (load_notes) are not. `difference`, when given, gets the first difference in words ("layer 1, row 3, column 7: tile 485 / 486").
bool same_level(const LevelData& a, const LevelData& b, std::string* difference = nullptr);

/// One way in which the canonical rewrite of a file differs from the file (describe_rewrite).
enum class RewriteChangeKind : uint8_t {
    NamePadding,         // bytes after the end of a dictionary name (the original compares a name up to its end; the map editors left leftovers there)
    DescriptionPadding,  // bytes after the end of the description (the same)
    GridSizeHighWords,   // the upper 16 bits of the two grid dwords (the original keeps the lower 16 bits)
    TrailingBytes,       // bytes after the egg word (never read)
    Restructured,        // the file's block 2 ended early, its block 4 or its egg word was cut short (the reader's Warning findings): the rewrite holds what was read, whole
    Unexplained          // any other difference: a fault of the writer (the round-trip proof fails on it)
};

struct RewriteChange {
    RewriteChangeKind kind{RewriteChangeKind::Unexplained};
    uint32_t count{0};     // how many names, how many bytes (1 for the description, the grid size and a restructured file)
    size_t offset{0};      // where in the file the first of them is
    std::string message;   // one sentence in plain words
};

const char* to_string(RewriteChangeKind kind) noexcept;

/// How the canonical bytes (`canonical`, write_level of `read`) differ from the file `source` that `read` was loaded from: empty when they are the same bytes.
/// Each byte that differs is accounted for by one of the kinds above, in file order; a difference that none of them explains is an Unexplained change.
std::vector<RewriteChange> describe_rewrite(const uint8_t* source, size_t size, const LevelData& read, const std::vector<uint8_t>& canonical);

/// The tile ids a level uses, in rising order: every layer cell, block 1 record, food stage and block 3's default type that is not the empty tile 0x7FFE.
std::vector<uint16_t> used_tile_ids(const LevelData& level);

/// The name of every tile id: the names of the 1,344 animations of Table 4 of ants.chd (the bytes of the file), which every named entry of the six original maps'
/// dictionaries is; empty when the bytes are no archive of the game.
std::vector<std::string> tile_names_from_chd(const uint8_t* data, size_t size);

/// The dictionary that each of the six original maps has for its content: one entry per tile id from 0 to the highest id the level uses, the name of a used
/// id (`tile_names`: the names of ants.chd's Table 4, the animation of each id), "." for every other one ("do not load this graphic"); one "." for a level that
/// uses no tile. A used id without a name in `tile_names` is ".".
std::vector<std::string> dictionary_of_used_tiles(const LevelData& level, const std::vector<std::string>& tile_names);

/// The ground of a blank map (Table-4 animation 485 "g01a", the commonest layer-1 tile of all six original maps) and the third word of every layer-1 cell of the
/// six original maps (and of 514 of the 534 players' maps): the defaults of blank_level.
constexpr uint16_t LVL_BLANK_GROUND_TILE = 485;
constexpr uint16_t LVL_LAYER1_THIRD_WORD = 0xCDCD;

/// A new map of `rows` x `columns` cells of plain ground, nothing on it, no start marker, no hill: version 8, mode 1, 10 minutes, 6 eggs a team, no default ant
/// type, an empty description, and the dictionary of its content (dictionary_of_used_tiles). The starting point of the map maker.
LevelData blank_level(uint16_t rows, uint16_t columns, const std::vector<std::string>& tile_names);

}  // namespace ants::assets
