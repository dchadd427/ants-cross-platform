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

}  // namespace ants_test
