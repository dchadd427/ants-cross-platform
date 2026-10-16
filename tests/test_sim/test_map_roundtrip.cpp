// The round-trip law of the map writer (docs/MAP_FORMAT.md, "The round-trip law"), on every map of the library: the six original maps (Original-Ants/Maps) and the
// players' maps (Community-Maps/, every file that maps.json lists). For each map:
//   1. the file is read, written by write_level and the bytes are read again: the two levels are the same map (same_level: every field that a file holds);
//   2. the rewrite differs from the file only where the original never reads a byte or where the file was cut short (describe_rewrite: no unexplained byte), and
//      those differences are counted by kind (the numbers that the document gives);
//   3. the rewrite is canonical: written again it is the same bytes, and the same within the limits of a new map (LevelLimits::for_new_maps(), which every map fits);
//   4. the file and its rewrite PLAY the same: two engines, one on each, with the four teams and the scripted players of the map sweep (tests/common/scripted_players.hpp),
//      have the same full state hash at the start and after every game second, for kPlayTicks ticks.
// The counts of the library (and of the dictionaries by the rule of the original maps) are those of docs/MAP_FORMAT.md. `test_map_roundtrip --ticks N` plays N ticks
// instead (the time of a CI run is the reason for the default), `--only TEXT` only the maps whose names hold TEXT (and checks no count).
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#include "scripted_players.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using ants::assets::LevelData;
using ants::assets::LevelLimits;
using ants::assets::LevelProblemKind;
using ants::assets::LevelWriteResult;
using ants::assets::RewriteChange;
using ants::assets::RewriteChangeKind;

namespace {

constexpr uint32_t kPlayTicks = 200;        // 10 game seconds: every map's start, its first orders, hatches and fights (all 540 maps, two plays each, in a few seconds of CI)
constexpr uint32_t kSeed = 20240229u;       // the engine's seed (the map sweep's)
constexpr uint32_t kScriptSeed = 0x5EEDu;   // the scripted players' generator (the map sweep's)
constexpr uint32_t kTicksPerSecond = 20;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "    FAILED: " << what << "\n";
    }
}

std::vector<uint8_t> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The file names that maps.json lists ("file": "..."; the names hold no quote or backslash, which tools/community_maps.py would escape)
std::vector<std::string> listed_community_maps(const std::string& folder) {
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

struct Map {
    std::string name;
    std::string path;
};

std::vector<Map> library() {
    std::vector<Map> maps;
    for (const char* name : {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"}) {
        maps.push_back({name, std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name});
    }
    const std::string folder = std::string(ANTS_SOURCE_DIR) + "/Community-Maps";
    for (const std::string& name : listed_community_maps(folder)) maps.push_back({name, folder + "/" + name});
    return maps;
}

// The full state hash at the start and after every game second of a play of `level` (the four teams, the map sweep's scripted players and seeds)
std::vector<ants::sim::StateHash> play(const LevelData& level, uint32_t ticks) {
    ants::sim::SimulationEngine engine;
    engine.init(level, kSeed, 0x0F);
    ants_test::ScriptedPlayers script(level, 0x0F, kScriptSeed, true);
    std::vector<ants::sim::StateHash> hashes{engine.state_hash()};
    std::vector<ants::sim::Command> commands;
    for (uint32_t t = 0; t < ticks && !engine.is_match_over(); ++t) {
        if (t % kTicksPerSecond == 0) {
            commands.clear();
            script.orders(engine, commands);
            for (const ants::sim::Command& c : commands) engine.apply_command(c);
        }
        engine.tick();
        engine.clear_audio_events();
        engine.clear_news_events();
        if ((t + 1) % kTicksPerSecond == 0 || t + 1 == ticks) hashes.push_back(engine.state_hash());
    }
    return hashes;
}

bool same_hash(const ants::sim::StateHash& a, const ants::sim::StateHash& b) {
    return a.total == b.total && a.engine == b.engine && a.players == b.players && a.grid == b.grid && a.food == b.food && a.ants == b.ants && a.paths == b.paths &&
           a.droppers == b.droppers;
}

// The play comparison is no formality: a map that differs from TINY in one field of the file plays differently within the same ticks (else a rewrite that lost that
// field could pass step 4 unseen). One change of each kind that the engine reads at the start or soon after.
void the_fingerprint_sees_a_changed_map(uint32_t ticks) {
    LevelData tiny;
    if (!tiny.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) {
        check(false, "TINY.LVL loads");
        return;
    }
    const std::vector<ants::sim::StateHash> plain = play(tiny, ticks);
    const size_t inner = static_cast<size_t>(15) * tiny.width + 15;              // a cell in the middle of the map
    struct Change {
        const char* what;
        void (*apply)(LevelData&, size_t);
    };
    const Change changes[] = {
        {"the solid bit of a layer-1 cell", [](LevelData& l, size_t c) { l.layer1_terrain[c].flags ^= 1u; }},
        {"the tile of a layer-1 cell (grass to water)", [](LevelData& l, size_t c) { l.layer1_terrain[c].tile_index = 476; }},
        {"a power-up on a layer-2 cell", [](LevelData& l, size_t c) { l.layer2_interactive[c].tile_index = 62; }},
        {"the units of a food pile", [](LevelData& l, size_t) { l.food_schedules[0].initial_delay = static_cast<uint16_t>(l.food_schedules[0].initial_delay + 1); }},
        {"the points of a food pile", [](LevelData& l, size_t) { l.food_schedules[0].respawn_interval = static_cast<uint16_t>(l.food_schedules[0].respawn_interval + 3); }},
        {"a start marker", [](LevelData& l, size_t) { l.anthill_spawns[0].x = static_cast<uint16_t>(l.anthill_spawns[0].x + 1); }},
        {"the eggs of a team", [](LevelData& l, size_t) { l.boundary_param = static_cast<uint16_t>(l.boundary_param + 1); }},
        {"the minutes", [](LevelData& l, size_t) { l.default_minutes = static_cast<uint16_t>(l.default_minutes + 1); }},
        {"the default ant type (block 3)", [](LevelData& l, size_t) { l.ambient_tile_or_sound = 62; }},
    };
    for (const Change& c : changes) {
        LevelData changed = tiny;
        c.apply(changed, inner);
        check(play(changed, ticks) != plain, std::string("the plays of TINY and of TINY with another ") + c.what + " differ");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    uint32_t ticks = kPlayTicks;
    std::string only;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--ticks" && i + 1 < argc) {
            ticks = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--only" && i + 1 < argc) {
            only = argv[++i];
        } else {
            std::cerr << "usage: test_map_roundtrip [--ticks N] [--only TEXT]\n";
            return 2;
        }
    }
    std::cout << "=======================================================\n"
              << " The round-trip law of the map writer: every map of the library\n"
              << "=======================================================\n";
    const auto started = std::chrono::steady_clock::now();
    const std::vector<Map> maps = library();
    std::map<RewriteChangeKind, int> maps_with;     // how many maps have a change of each kind
    int total = 0;
    int same_map = 0;
    int byte_identical = 0;
    int same_where_read = 0;                        // the same bytes once the bytes the original never reads are zeroed or dropped (no restructured part)
    int same_play = 0;
    uint64_t ticks_played = 0;
    size_t largest = 0;
    std::string largest_name;
    std::vector<std::string> beyond_new_limits;     // the maps whose rewrite LevelLimits::for_new_maps() refuses, with the reason
    std::vector<std::string> files_beyond;          // the files that a reader with those limits refuses as they are (before a rewrite), with the reason
    const std::vector<uint8_t> archive = read_bytes(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd");
    const std::vector<std::string> tile_names = ants::assets::tile_names_from_chd(archive.data(), archive.size());
    check(tile_names.size() == 1344u, "the tile names of ants.chd");
    int originals_rule = 0;                         // maps whose dictionary is the one of the original maps' rule (dictionary_of_used_tiles)
    int players_rule = 0;
    for (const Map& m : maps) {
        if (!only.empty() && m.name.find(only) == std::string::npos) continue;
        ++total;
        const std::vector<uint8_t> source = read_bytes(m.path);
        LevelData a;
        if (!a.load_from_memory(source.data(), source.size())) {
            check(false, m.name + ": the library map does not load");
            continue;
        }
        const LevelWriteResult w = ants::assets::write_level(a);
        if (!w.ok) {
            check(false, m.name + ": not written: " + w.error);
            continue;
        }
        // 1. the same map
        LevelData b;
        ants::assets::LevelValidation report;
        check(b.load_from_memory(w.bytes.data(), w.bytes.size(), &report), m.name + ": the rewrite loads");
        std::string difference;
        const bool same = ants::assets::same_level(a, b, &difference);
        check(same, m.name + ": the rewrite reads back as the same map (" + difference + ")");
        same_map += same ? 1 : 0;
        for (const auto& p : b.load_notes) {
            check(p.kind != LevelProblemKind::FoodBlockEndedEarly && p.kind != LevelProblemKind::WaypointBlockTruncated && p.kind != LevelProblemKind::EggStockMissing,
                  m.name + ": the rewrite is whole (" + p.message + ")");
        }
        // 2. what differs, and why
        const std::vector<RewriteChange> changes = ants::assets::describe_rewrite(source.data(), source.size(), a, w.bytes);
        bool restructured = false;
        std::map<RewriteChangeKind, bool> seen;
        for (const RewriteChange& c : changes) {
            check(c.kind != RewriteChangeKind::Unexplained, m.name + ": " + c.message);
            restructured = restructured || c.kind == RewriteChangeKind::Restructured;
            seen[c.kind] = true;
        }
        for (const auto& k : seen) ++maps_with[k.first];
        byte_identical += changes.empty() ? 1 : 0;
        same_where_read += restructured ? 0 : 1;
        // 3. canonical: a fixed point, and within the limits of a new map
        const LevelWriteResult again = ants::assets::write_level(b);
        check(again.ok && again.bytes == w.bytes, m.name + ": the rewrite of the rewrite is the same bytes");
        check(ants::assets::describe_rewrite(w.bytes.data(), w.bytes.size(), b, w.bytes).empty(), m.name + ": a canonical file has nothing to change");
        const LevelWriteResult limited = ants::assets::write_level(a, LevelLimits::for_new_maps());
        if (limited.ok) {
            check(limited.bytes == w.bytes, m.name + ": within the limits of a new map, the same bytes");
        } else {
            beyond_new_limits.push_back(m.name + ": " + limited.error);
        }
        LevelData as_file;
        ants::assets::LevelValidation file_report;
        if (!as_file.load_from_memory(source.data(), source.size(), &file_report, LevelLimits::for_new_maps())) files_beyond.push_back(m.name + ": " + file_report.reason());
        if (w.bytes.size() > largest) {
            largest = w.bytes.size();
            largest_name = m.name;
        }
        // the dictionary: by the rule of the original maps?
        if (ants::assets::dictionary_of_used_tiles(a, tile_names) == a.tile_dictionary) ++(m.path.find("Community-Maps") == std::string::npos ? originals_rule : players_rule);
        // 4. the same play
        const std::vector<ants::sim::StateHash> pa = play(a, ticks);
        const std::vector<ants::sim::StateHash> pb = play(b, ticks);
        bool equal = pa.size() == pb.size();
        size_t first = 0;
        for (; equal && first < pa.size(); ++first) equal = same_hash(pa[first], pb[first]);
        check(equal, m.name + ": the rewrite plays as the file does (the hashes differ at checkpoint " + std::to_string(first) + ")");
        same_play += equal ? 1 : 0;
        ticks_played += 2u * static_cast<uint64_t>(pa.size() > 1 ? (pa.size() - 1) * kTicksPerSecond : 0);
    }
    if (only.empty()) the_fingerprint_sees_a_changed_map(ticks);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "  maps " << total << ": the same map " << same_map << ", the same play " << same_play << " (" << ticks << " ticks, all four teams)\n"
              << "  byte-identical rewrites " << byte_identical << "; the same bytes where the original reads " << same_where_read << "; restructured "
              << maps_with[RewriteChangeKind::Restructured] << "\n"
              << "  maps with name leftovers " << maps_with[RewriteChangeKind::NamePadding] << ", description leftovers " << maps_with[RewriteChangeKind::DescriptionPadding]
              << ", grid size high words " << maps_with[RewriteChangeKind::GridSizeHighWords] << ", bytes after the egg word " << maps_with[RewriteChangeKind::TrailingBytes] << "\n"
              << "  the largest rewrite " << largest << " bytes (" << largest_name << "); dictionaries by the originals' rule: " << originals_rule << " originals, " << players_rule
              << " players' maps; about " << ticks_played << " ticks played in " << seconds << " s\n";
    if (only.empty()) {
        // The numbers of docs/MAP_FORMAT.md ("The round-trip law"): a change of the library or of the writer that moves them updates the document with them.
        check(total == 540, "the library has 540 maps (6 originals and the 534 of maps.json): " + std::to_string(total));
        check(same_map == total && same_play == total, "every map reads back and plays the same as its rewrite");
        check(byte_identical == 0, "no map of the library is rewritten byte for byte: every one has leftovers after the end of its dictionary names");
        check(maps_with[RewriteChangeKind::NamePadding] == 540, "540 maps have leftovers after their names: " + std::to_string(maps_with[RewriteChangeKind::NamePadding]));
        check(maps_with[RewriteChangeKind::DescriptionPadding] == 14, "14 maps have leftovers after their description: " + std::to_string(maps_with[RewriteChangeKind::DescriptionPadding]));
        check(maps_with[RewriteChangeKind::GridSizeHighWords] == 0, "no map has bits above 16 in its grid size: " + std::to_string(maps_with[RewriteChangeKind::GridSizeHighWords]));
        check(maps_with[RewriteChangeKind::TrailingBytes] == 64, "64 maps have bytes after the egg word: " + std::to_string(maps_with[RewriteChangeKind::TrailingBytes]));
        check(maps_with[RewriteChangeKind::Restructured] == 12, "12 maps end inside block 4 or before the egg word: " + std::to_string(maps_with[RewriteChangeKind::Restructured]));
        check(same_where_read == 528, "528 maps are the same bytes where the original reads: " + std::to_string(same_where_read));
        check(largest == 113880u, "the largest rewrite is 113,880 bytes (Easy jungle): " + std::to_string(largest));
        // the dictionaries (docs/MAP_FORMAT.md, "The dictionary of a new map"): the six original maps follow the rule, 24 of the 534 players' maps do
        check(originals_rule == 6 && players_rule == 24, "the dictionaries by the originals' rule: " + std::to_string(originals_rule) + " originals, " + std::to_string(players_rule) + " players' maps");
        // the limits of a new map (10,000 cells, 130 a side, 144 KB) hold every map of the library: any of them can be the start of a new map
        check(beyond_new_limits.empty(), "every map of the library is within the limits of a new map: " + std::to_string(beyond_new_limits.size()) + " are not");
        // as it is, one file is not: BOMBERWORLD has 176,741 bytes after its egg word, which its rewrite (58,853 bytes) drops
        check(files_beyond.size() == 1u && files_beyond[0] == "BOMBERWORLD.lvl: the file has 235594 bytes, more than the 147456 allowed here: nothing of it is read",
              "one file of the library is larger than a new map as it is (BOMBERWORLD): " + std::to_string(files_beyond.size()));
    }
    for (const std::string& b : beyond_new_limits) std::cout << "  beyond the limits of a new map: " << b << "\n";
    for (const std::string& b : files_beyond) std::cout << "  as a file beyond the limits of a new map: " << b << "\n";
    std::cout << "\n" << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
