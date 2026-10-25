// Big Extended maps for the server's tests: the shipped TINY map grown to any size (plain ground at the right and below, TINY's start markers, hills and food where they were), as bytes, and a Classic or
// Extended map with only the teams of a mask. Included by the tests that need a map over the size limits of a room (online_limits.hpp) or over 1 MiB. Everything is in an unnamed namespace.
#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ants_assets/lvl_core.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_test_paths.hpp"

namespace {

[[maybe_unused]] std::string big_tiny_bytes() {
    std::ifstream in(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// TINY as an Extended map of `rows` x `columns` (at least its own size): a map that works for all four teams, whatever its size. "" when it cannot be made.
[[maybe_unused]] std::string grown_tiny(uint32_t rows, uint32_t columns) {
    using namespace ants::assets;
    const std::string base = big_tiny_bytes();
    LevelData t;
    if (!LVLParser::load_from_memory(reinterpret_cast<const uint8_t*>(base.data()), base.size(), t, nullptr, LevelLimits::for_extended_maps())) return std::string();
    if (rows < t.height() || columns < t.width()) return std::string();
    LevelData big = blank_extended_level(rows, columns, lvl_core::tile_names(), LevelLimits::format_ceiling());       // (the ceiling of the format: a grid over what a place takes can be made, to be refused)
    if (big.layer1_terrain.size() != static_cast<size_t>(rows) * columns) return std::string();
    big.tile_dictionary = t.tile_dictionary;
    big.tile_type_count = t.tile_type_count;
    big.description = t.description;
    big.default_minutes = t.default_minutes;
    big.game_mode = t.game_mode;
    for (uint32_t y = 0; y < t.height(); ++y) {
        for (uint32_t x = 0; x < t.width(); ++x) {
            big.layer1_terrain[static_cast<size_t>(y) * columns + x] = t.layer1_terrain[static_cast<size_t>(y) * t.width() + x];
            big.layer2_interactive[static_cast<size_t>(y) * columns + x] = t.layer2_interactive[static_cast<size_t>(y) * t.width() + x];
        }
    }
    big.anthill_spawns = t.anthill_spawns;
    big.food_schedules = t.food_schedules;
    big.waypoints = t.waypoints;
    big.ambient_flag = t.ambient_flag;
    big.ambient_tile_or_sound = t.ambient_tile_or_sound;
    big.boundary_param = t.boundary_param;
    const LevelWriteResult w = write_level(big, LevelLimits::format_ceiling());
    return w.ok ? std::string(w.bytes.begin(), w.bytes.end()) : std::string();
}

// TINY grown to `rows` x `columns` as a CLASSIC map (version 8: the game's own limits, at most 90 rows because of the wall at row 90 and 256 columns), so that even the older machines of the tests load it. "" when it cannot be made.
[[maybe_unused]] std::string grown_classic(uint32_t rows, uint32_t columns) {
    using namespace ants::assets;
    const std::string extended = grown_tiny(rows, columns);
    LevelData level;
    if (extended.empty() || !LVLParser::load_from_memory(reinterpret_cast<const uint8_t*>(extended.data()), extended.size(), level, nullptr, LevelLimits::for_extended_maps())) return std::string();
    const LevelConversion c = convert_to_classic(level);
    if (!c.ok) return std::string();
    const LevelWriteResult w = write_level(c.level, LevelLimits{});
    return w.ok ? std::string(w.bytes.begin(), w.bytes.end()) : std::string();
}

// TINY (a Classic map) with only the start markers of the teams in `mask` (bit 0 green ... bit 3 black): a map that fewer teams can play
[[maybe_unused]] std::string tiny_with_teams(uint8_t mask) {
    using namespace ants::assets;
    const std::string base = big_tiny_bytes();
    LevelData t;
    if (!LVLParser::load_from_memory(reinterpret_cast<const uint8_t*>(base.data()), base.size(), t, nullptr, LevelLimits::for_extended_maps())) return std::string();
    std::vector<AnthillSpawn> kept;
    for (const AnthillSpawn& s : t.anthill_spawns) {
        if (s.team_id >= 4 || ((mask >> s.team_id) & 1u) != 0) kept.push_back(s);
    }
    t.anthill_spawns.assign(kept.begin(), kept.end());
    const LevelWriteResult w = write_level(t, LevelLimits::for_new_maps());
    return w.ok ? std::string(w.bytes.begin(), w.bytes.end()) : std::string();
}

// TINY (a Classic map, its own grid) that starts exactly `per_color[c]` ants for color c: its start markers of that color repeated (on the same cells) or cut short, its other block 1 records (plants) kept. A map that a
// room's limit on start ants judges: 0 for every color is a map with no start marker, {1500, 1500, 1500, 1500} the 6,000-ant map of the lag study. "" when it cannot be made.
[[maybe_unused]] std::string tiny_with_ants(const std::array<uint32_t, 4>& per_color) {
    using namespace ants::assets;
    const std::string base = big_tiny_bytes();
    LevelData t;
    if (!LVLParser::load_from_memory(reinterpret_cast<const uint8_t*>(base.data()), base.size(), t, nullptr, LevelLimits::for_extended_maps())) return std::string();
    std::vector<AnthillSpawn> kept;
    std::array<std::vector<AnthillSpawn>, 4> markers;
    for (const AnthillSpawn& s : t.anthill_spawns) {
        if (s.team_id < 4) markers[s.team_id].push_back(s);
        else kept.push_back(s);
    }
    for (size_t color = 0; color < 4; ++color) {
        if (markers[color].empty()) return std::string();
        for (uint32_t i = 0; i < per_color[color]; ++i) kept.push_back(markers[color][i % markers[color].size()]);
    }
    t.anthill_spawns.assign(kept.begin(), kept.end());
    const LevelWriteResult w = write_level(t, LevelLimits::for_extended_maps());
    return w.ok ? std::string(w.bytes.begin(), w.bytes.end()) : std::string();
}

}  // namespace
