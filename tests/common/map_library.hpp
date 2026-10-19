// The 540 maps of the library (the six original maps and the players' maps that Community-Maps/maps.json lists) and the scripted play that the map proofs give them: used by the
// round-trip proof of the map writer (tests/test_sim/test_map_roundtrip.cpp) and by the hash ledger (tests/test_sim/hash_ledger.cpp). Header-only.
#pragma once

#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#include "scripted_players.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace ants_test {

constexpr uint32_t kLibrarySeed = 20240229u;       // the engine's seed (the map sweep's)
constexpr uint32_t kLibraryScriptSeed = 0x5EEDu;   // the scripted players' generator (the map sweep's)
constexpr uint32_t kLibraryTicksPerSecond = 20;

inline std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The file names that maps.json lists ("file": "..."; the names hold no quote or backslash, which tools/community_maps.py would escape)
inline std::vector<std::string> listed_community_maps(const std::string& folder) {
    std::ifstream in(folder + "/maps.json");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> names;
    const std::string key = "\"file\": \"";
    for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at)) {
        at += key.size();
        const size_t end = text.find('"', at);
        if (end == std::string::npos) break;
        names.push_back(text.substr(at, end - at));
        at = end;
    }
    return names;
}

struct LibraryMap {
    std::string name;
    std::string path;
};

inline std::vector<LibraryMap> library() {
    std::vector<LibraryMap> maps;
    for (const char* name : {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"}) {
        maps.push_back({name, std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name});
    }
    const std::string folder = std::string(ANTS_SOURCE_DIR) + "/Community-Maps";
    for (const std::string& name : listed_community_maps(folder)) maps.push_back({name, folder + "/" + name});
    return maps;
}

// The full state hash at the start and after every game second of a play of `level` (the four teams, the map sweep's scripted players and seeds): the entry k is the
// state at tick 20 k (the last one at tick `ticks` when that is not a whole second, or where the match ended)
inline std::vector<ants::sim::StateHash> play_hashes(const ants::assets::LevelData& level, uint32_t ticks) {
    ants::sim::SimulationEngine engine;
    engine.init(level, kLibrarySeed, 0x0F);
    ScriptedPlayers script(level, 0x0F, kLibraryScriptSeed, true);
    std::vector<ants::sim::StateHash> hashes{engine.state_hash()};
    std::vector<ants::sim::Command> commands;
    for (uint32_t t = 0; t < ticks && !engine.is_match_over(); ++t) {
        if (t % kLibraryTicksPerSecond == 0) {
            commands.clear();
            script.orders(engine, commands);
            for (const ants::sim::Command& c : commands) engine.apply_command(c);
        }
        engine.tick();
        engine.clear_audio_events();
        engine.clear_news_events();
        if ((t + 1) % kLibraryTicksPerSecond == 0 || t + 1 == ticks) hashes.push_back(engine.state_hash());
    }
    return hashes;
}

// Every field of two cells (TileCell has no operator==; the fields are those that hash_cell of src/ants_sim/state_hash.cpp mixes)
inline bool same_cell(const ants::sim::TileCell& a, const ants::sim::TileCell& b) {
    return a.terrain_id == b.terrain_id && a.terrain_type == b.terrain_type && a.surface_type == b.surface_type && a.flags == b.flags && a.interactive_id == b.interactive_id &&
           a.interactive_owner == b.interactive_owner && a.timer_ticks == b.timer_ticks && a.anchor_x == b.anchor_x && a.anchor_y == b.anchor_y && a.is_food == b.is_food &&
           a.is_mud == b.is_mud && a.is_powerup == b.is_powerup && a.powerup_type == b.powerup_type && a.is_obstacle_overlay == b.is_obstacle_overlay &&
           a.is_thief_only == b.is_thief_only && a.is_corridor_team_locked == b.is_corridor_team_locked && a.is_base_hole == b.is_base_hole &&
           a.base_owner_team == b.base_owner_team && a.occupant_ant_id == b.occupant_ant_id && a.static_solid == b.static_solid;
}


// The play of `level` at rules 0 and, side by side, the same level with its rules word forced to 1 (the same engine seed, the same scripted players with the same seed): at
// rules 1 the wall at row 90 is down, and a map of at most 90 rows has no row 90, so nothing may change. At every checkpoint (the start and every game second) the two engines
// must agree on every part of the state hash but the grid's (which mixes the rules word in when it is not 0: that is its job), and on every cell, hill and track of the grid itself.
// `hashes` is the play at rules 0, exactly play_hashes; `difference` is "" when the two agree at every checkpoint, else the first thing that differs.
struct RulesOneComparison {
    std::vector<ants::sim::StateHash> hashes;
    std::string difference;
    uint32_t checkpoints{0};
};

inline std::string rules_one_difference(const ants::sim::SimulationEngine& zero, const ants::sim::SimulationEngine& one) {
    const ants::sim::StateHash a = zero.state_hash();
    const ants::sim::StateHash b = one.state_hash();
    if (a.engine != b.engine) return "the engine state (clocks, generators, match)";
    if (a.players != b.players) return "the players' state";
    if (a.food != b.food) return "the food";
    if (a.ants != b.ants) return "the ants";
    if (a.paths != b.paths) return "the path managers";
    if (a.droppers != b.droppers) return "the flower droppers";
    const ants::sim::Grid& ga = zero.grid();
    const ants::sim::Grid& gb = one.grid();
    if (ga.width() != gb.width() || ga.height() != gb.height() || ga.cells().size() != gb.cells().size()) return "the size of the grid";
    for (size_t i = 0; i < ga.cells().size(); ++i) {
        if (!same_cell(ga.cells()[i], gb.cells()[i])) return "the cell at row " + std::to_string(i / ga.width()) + ", column " + std::to_string(i % ga.width());
    }
    if (ga.anthills().size() != gb.anthills().size()) return "the hills";
    for (size_t i = 0; i < ga.anthills().size(); ++i) {
        const auto& x = ga.anthills()[i];
        const auto& y = gb.anthills()[i];
        if (x.tile_id != y.tile_id || x.x != y.x || x.y != y.y || x.team_id != y.team_id) return "a hill";
    }
    if (gb.rules() != 1) return "the rules word (the second engine is not at rules 1)";
    return "";
}

inline RulesOneComparison play_hashes_rules_one(const ants::assets::LevelData& level, uint32_t ticks) {
    ants::assets::LevelData forced = level;
    forced.rules = 1;
    ants::sim::SimulationEngine zero;
    ants::sim::SimulationEngine one;
    zero.init(level, kLibrarySeed, 0x0F);
    one.init(forced, kLibrarySeed, 0x0F);
    ScriptedPlayers script_zero(level, 0x0F, kLibraryScriptSeed, true);       // (a script also acts on its engine directly, so each engine has its own, with the same seed)
    ScriptedPlayers script_one(forced, 0x0F, kLibraryScriptSeed, true);
    RulesOneComparison out;
    out.hashes.push_back(zero.state_hash());
    const auto compare = [&](uint32_t tick) {
        ++out.checkpoints;
        if (out.difference.empty()) {
            const std::string d = rules_one_difference(zero, one);
            if (!d.empty()) out.difference = d + " at tick " + std::to_string(tick);
        }
    };
    compare(0);
    std::vector<ants::sim::Command> commands;
    for (uint32_t t = 0; t < ticks && !zero.is_match_over(); ++t) {
        if (t % kLibraryTicksPerSecond == 0) {
            commands.clear();
            script_zero.orders(zero, commands);
            for (const ants::sim::Command& c : commands) zero.apply_command(c);
            commands.clear();
            script_one.orders(one, commands);
            for (const ants::sim::Command& c : commands) one.apply_command(c);
        }
        zero.tick();
        one.tick();
        zero.clear_audio_events();
        one.clear_audio_events();
        zero.clear_news_events();
        one.clear_news_events();
        if ((t + 1) % kLibraryTicksPerSecond == 0 || t + 1 == ticks) {
            out.hashes.push_back(zero.state_hash());
            compare(t + 1);
        }
    }
    return out;
}

}  // namespace ants_test
