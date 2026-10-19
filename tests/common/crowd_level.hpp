// A big Extended map for the proofs that a game of this computer plays on a map taller than 90 rows (tests/test_ai/test_ai_extended.cpp, tests/test_app/test_extended_play_app.cpp, the
// view fingerprints and the proof by hand in docs/TESTING.md): rows x columns cells of open ground (dirt, with a few patches of another ground), one hill for each of the four teams near its
// corner, and `per_team` starting ants of every team mixed in a block in the middle (a block of ten ants to a row, the team of an ant taking turns, so that no team stands apart); the
// level's default ant type is the Combat Ant, so that every starting ant fights. Rules 1 (the blank Extended map above 90 rows). Header-only.
#pragma once

#include "ants_assets/lvl_core.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace ants_test {

struct CrowdHill {
    uint32_t row{0};
    uint32_t column{0};
};

// The corners the hills stand at (4 x 4 cells each, 4 cells from the edges), by the remake's team number: green top left, red top right, blue bottom left, black bottom right
inline std::array<CrowdHill, 4> crowd_hills(uint32_t rows, uint32_t columns) {
    return {{{4, 4}, {4, columns - 8}, {rows - 8, 4}, {rows - 8, columns - 8}}};
}

// The hill of a team as the library's map TINY draws it, made relative: the cells of the 4 x 4 block with their flags and third words (a hill's cells point at the hill, so the words are the
// same wherever the block stands once the anchors are relative, as an Extended map keeps them)
struct HillCell {
    uint32_t drow{0};
    uint32_t dcolumn{0};
    ants::assets::MapCell cell;
};

inline std::vector<HillCell> hill_of_team(const ants::assets::LevelData& extended_tiny, int team) {
    std::vector<HillCell> out;
    uint32_t top = ~0u, left = ~0u;
    const uint32_t rows = extended_tiny.height, columns = extended_tiny.width;
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < columns; ++c) {
            const ants::assets::MapCell& m = extended_tiny.layer2_interactive[static_cast<size_t>(r) * columns + c];
            if (ants::assets::hill_team_of_id(m.tile_index) == team) {
                top = std::min(top, r);
                left = std::min(left, c);
            }
        }
    }
    if (top == ~0u) return out;
    for (uint32_t r = top; r < rows && r < top + 4; ++r) {
        for (uint32_t c = left; c < columns && c < left + 4; ++c) {
            const ants::assets::MapCell& m = extended_tiny.layer2_interactive[static_cast<size_t>(r) * columns + c];
            if (ants::assets::hill_team_of_id(m.tile_index) == team) out.push_back({r - top, c - left, m});
        }
    }
    return out;
}

// The map. `per_team` ants of each team (at most 25: a block of 10 x 10 cells is the middle), `combat` makes the Combat Ant the level's default ant type. An empty level (no cell) when TINY cannot
// be read.
inline ants::assets::LevelData crowd_level(uint32_t rows = 100, uint32_t columns = 100, uint32_t per_team = 25, bool combat = true) {
    using namespace ants::assets;
    const std::vector<std::string> names = lvl_core::tile_names();
    LevelData level = blank_extended_level(rows, columns, names, LevelLimits::for_extended_maps());
    LevelData tiny;
    if (!tiny.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) return LevelData{};
    const LevelConversion up = convert_to_extended(tiny);
    if (!up.ok || level.layer1_terrain.empty()) return LevelData{};
    // a few patches of another ground (gravel, tile 486 is walkable like the blank ground's 485), so that the map is not one tile
    for (uint32_t r = 10; r + 3 < rows; r += 17) {
        for (uint32_t c = 9; c + 3 < columns; c += 23) {
            for (uint32_t dr = 0; dr < 3; ++dr) {
                for (uint32_t dc = 0; dc < 3; ++dc) level.layer1_terrain[static_cast<size_t>(r + dr) * columns + (c + dc)].tile_index = 486;
            }
        }
    }
    const std::array<CrowdHill, 4> hills = crowd_hills(rows, columns);
    for (int team = 0; team < 4; ++team) {
        for (const HillCell& h : hill_of_team(up.level, team)) {
            level.layer2_interactive[static_cast<size_t>(hills[static_cast<size_t>(team)].row + h.drow) * columns + (hills[static_cast<size_t>(team)].column + h.dcolumn)] = h.cell;
        }
    }
    // the ants: block of 10 to a row in the middle, the team taking turns (i % 4: 25 each for 100 ants)
    const uint32_t first_row = rows / 2 - 5, first_column = columns / 2 - 5;
    const std::array<uint16_t, 4> marker = {154, 155, 153, 152};                      // the block-1 id of the start marker of green, red, blue, black
    for (uint32_t i = 0; i < 4 * per_team && i < 100; ++i) {
        AnthillSpawn s;
        s.team_id = static_cast<uint8_t>(i % 4);
        s.tile_id = marker[s.team_id];
        s.y = static_cast<uint16_t>(first_row + i / 10);
        s.x = static_cast<uint16_t>(first_column + i % 10);
        level.anthill_spawns.push_back(s);
    }
    level.ambient_tile_or_sound = combat ? uint16_t{62} : LVL_EMPTY_TILE;                // 62: the Combat Ant's power-up id is the level's default ant type
    level.description = "100 ants in the middle";
    level.tile_dictionary = dictionary_of_used_tiles(level, names);
    level.tile_type_count = static_cast<uint16_t>(level.tile_dictionary.size() - 1);
    return level;
}

// The canonical file of the map, or an empty string
inline std::string crowd_bytes(const ants::assets::LevelData& level) {
    const ants::assets::LevelWriteResult w = ants::assets::write_level(level, ants::assets::LevelLimits::for_extended_maps());
    return w.ok ? std::string(w.bytes.begin(), w.bytes.end()) : std::string();
}

inline bool write_crowd_file(const std::string& path, const ants::assets::LevelData& level) {
    const std::string bytes = crowd_bytes(level);
    if (bytes.empty()) return false;
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

}  // namespace ants_test
