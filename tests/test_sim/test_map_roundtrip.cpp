// The round-trip law of the map writer (docs/MAP_FORMAT.md, "The round-trip law"), on every map of the library: the six original maps (Original-Ants/Maps) and the
// players' maps (Community-Maps/, every file that maps.json lists). For each map:
//   1. the file is read, written by write_level and the bytes are read again: the two levels are the same map (same_level: every field that a file holds);
//   2. the rewrite differs from the file only where the original never reads a byte or where the file was cut short (describe_rewrite: no unexplained byte), and
//      those differences are counted by kind (the numbers that the document gives);
//   3. the rewrite is canonical: written again it is the same bytes, and the same within the limits of a new map (LevelLimits::for_new_maps(), which every map fits);
//   4. the file and its rewrite PLAY the same: two engines, one on each, with the four teams and the scripted players of the map sweep (tests/common/scripted_players.hpp),
//      have the same full state hash at the start and after every game second, for kPlayTicks ticks.
//   5. the Extended map (version 9, docs/MAP_FORMAT.md): convert_to_extended, written, read again (the same level, written twice the same bytes, refused by a reader of
//      Classic maps only), convert_to_classic: the same bytes as the rewrite of step 1 (so a map goes up and comes back unchanged);
//   6. the converted map (rules 0) PLAYS as its source does: the simulation does not read version 9 yet (and a reader of Classic maps refuses it), so the level that it
//      gets is the converted one with its anchors turned absolute again by LevelData::object_anchor (as_sim_level below), which is all a version 9 loader will do;
//      the hashes are the source's, at the start and after every game second.
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

using ants::assets::LevelConversion;
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

// The level that the simulation plays for a version 9 map of rules 0: the same grid, with every object anchor in the absolute form of version 8 (the sum that
// LevelData::object_anchor makes), and nothing else changed. Test-only: the simulation's loader takes version 8 maps only in this version of the game.
LevelData as_sim_level(const LevelData& extended) {
    LevelData out = extended;
    out.version = ants::assets::LVL_EXPECTED_VERSION;
    out.format_flags = 0;
    out.rules = 0;
    for (uint32_t row = 0; row < extended.height; ++row) {
        for (uint32_t column = 0; column < extended.width; ++column) {
            int32_t anchor_row = 0;
            int32_t anchor_column = 0;
            if (!extended.object_anchor(row, column, anchor_row, anchor_column)) continue;
            out.layer2_interactive[static_cast<size_t>(row) * extended.width + column].properties =
                static_cast<uint16_t>((static_cast<uint32_t>(anchor_row) & 0xFFu) | ((static_cast<uint32_t>(anchor_column) & 0xFFu) << 8));
        }
    }
    return out;
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

// The play comparison of step 6 sees a misplaced anchor: the anchors of a version 9 map moved by one cell (a converter that got the sign of the offset wrong)
// play otherwise than the map does. A hill is the object whose anchor the simulation reads at the start.
void the_fingerprint_sees_a_misplaced_anchor(uint32_t ticks) {
    LevelData tiny;
    if (!tiny.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) {
        check(false, "TINY.LVL loads");
        return;
    }
    const LevelConversion up = ants::assets::convert_to_extended(tiny);
    check(up.ok, "TINY.LVL goes up to an Extended map");
    if (!up.ok) return;
    LevelData misplaced = up.level;
    for (uint32_t row = 0; row < misplaced.height; ++row) {
        for (uint32_t column = 0; column < misplaced.width; ++column) {
            if (!misplaced.has_object_anchor(row, column)) continue;
            uint16_t& word = misplaced.layer2_interactive[static_cast<size_t>(row) * misplaced.width + column].properties;
            word = static_cast<uint16_t>((word & 0xFF00u) | ((word + 1u) & 0xFFu));                // one row further
        }
    }
    check(play(as_sim_level(misplaced), ticks) != play(tiny, ticks), "TINY with every anchor one row off plays otherwise than TINY");
    check(play(as_sim_level(up.level), ticks) == play(tiny, ticks), "TINY as an Extended map plays as TINY does");
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
    int extended_same_level = 0;                    // the version 9 file reads back as the converted level, and writes twice the same bytes
    int extended_same_bytes = 0;                    // and converts back to the bytes of the rewrite
    int extended_refused = 0;                       // and a reader of Classic maps only refuses it (version_not_supported)
    int extended_same_play = 0;
    uint64_t ticks_played = 0;
    size_t largest = 0;
    std::string largest_name;
    size_t largest_file = 0;                        // the largest file as it is, and the size of its rewrite
    std::string largest_file_name;
    size_t largest_file_rewrite = 0;
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
        if (source.size() > largest_file) {
            largest_file = source.size();
            largest_file_name = m.name;
            largest_file_rewrite = w.bytes.size();
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
        // 5. the Extended map: up, written, read, down
        const LevelConversion up = ants::assets::convert_to_extended(a);
        check(up.ok && up.reasons.empty(), m.name + ": convert_to_extended");
        if (!up.ok) continue;
        const LevelWriteResult w9 = ants::assets::write_level(up.level, LevelLimits::format_ceiling());
        check(w9.ok, m.name + ": the Extended map is written: " + w9.error);
        if (!w9.ok) continue;
        LevelData r9;
        const bool read9 = r9.load_from_memory(w9.bytes.data(), w9.bytes.size(), nullptr, LevelLimits::format_ceiling());
        std::string difference9;
        const LevelWriteResult w9_again = read9 ? ants::assets::write_level(r9, LevelLimits::format_ceiling()) : LevelWriteResult{};
        const bool level9 = read9 && ants::assets::same_level(up.level, r9, &difference9) && w9_again.ok && w9_again.bytes == w9.bytes;
        check(level9, m.name + ": the Extended map reads back as the converted level and is written twice as the same bytes (" + difference9 + ")");
        extended_same_level += level9 ? 1 : 0;
        LevelData classic_only;
        ants::assets::LevelValidation refusal;
        const bool refused = !classic_only.load_from_memory(w9.bytes.data(), w9.bytes.size(), &refusal) && refusal.has(LevelProblemKind::VersionNotSupported) &&
                             !classic_only.load_from_memory(w9.bytes.data(), w9.bytes.size(), &refusal, LevelLimits::for_new_maps());
        check(refused, m.name + ": a reader of Classic maps only refuses the Extended map");
        extended_refused += refused ? 1 : 0;
        const LevelConversion down = read9 ? ants::assets::convert_to_classic(r9) : LevelConversion{};
        const LevelWriteResult w8 = down.ok ? ants::assets::write_level(down.level) : LevelWriteResult{};
        const bool bytes8 = down.ok && w8.ok && w8.bytes == w.bytes;
        check(bytes8, m.name + ": the Extended map converts back to the bytes of the rewrite" + (down.reasons.empty() ? "" : " (" + down.reasons[0] + ")"));
        extended_same_bytes += bytes8 ? 1 : 0;
        // 6. the same play
        const std::vector<ants::sim::StateHash> pc = read9 ? play(as_sim_level(r9), ticks) : std::vector<ants::sim::StateHash>{};
        bool equal9 = pa.size() == pc.size();
        size_t first9 = 0;
        for (; equal9 && first9 < pa.size(); ++first9) equal9 = same_hash(pa[first9], pc[first9]);
        check(equal9, m.name + ": the Extended map (rules 0) plays as the Classic map does (the hashes differ at checkpoint " + std::to_string(first9) + ")");
        extended_same_play += equal9 ? 1 : 0;
        ticks_played += 2u * static_cast<uint64_t>(pa.size() > 1 ? (pa.size() - 1) * kTicksPerSecond : 0);
    }
    if (only.empty()) the_fingerprint_sees_a_changed_map(ticks);
    if (only.empty()) the_fingerprint_sees_a_misplaced_anchor(ticks);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "  maps " << total << ": the same map " << same_map << ", the same play " << same_play << " (" << ticks << " ticks, all four teams)\n"
              << "  as Extended maps (version 9): the same level " << extended_same_level << ", back to the same bytes " << extended_same_bytes << ", refused by Classic-only readers "
              << extended_refused << ", the same play " << extended_same_play << "\n"
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
        check(extended_same_level == total && extended_same_bytes == total && extended_refused == total && extended_same_play == total,
              "every map goes up to an Extended map, comes back as the same bytes, is refused by a reader of Classic maps only, and plays the same: " + std::to_string(extended_same_level) + " / " +
                  std::to_string(extended_same_bytes) + " / " + std::to_string(extended_refused) + " / " + std::to_string(extended_same_play));
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
        // the limits of a new map (10,000 cells, 130 a side, a canonical file of 144 KB) hold every map of the library: any of them can be the start of a new map
        check(beyond_new_limits.empty(), "every map of the library is within the limits of a new map: " + std::to_string(beyond_new_limits.size()) + " are not");
        // and every file of the library is read as it is by a reader with those limits (512 KB): the largest, BOMBERWORLD, has 235,594 bytes, 176,741 of them after
        // its egg word, which its rewrite (58,853 bytes, within the 144 KB of a new map's canonical file) drops
        check(files_beyond.empty(), "every file of the library is read with the limits of a new map: " + std::to_string(files_beyond.size()) + " are not");
        check(largest_file == 235594u && largest_file_name == "BOMBERWORLD.lvl" && largest_file_rewrite == 58853u,
              "the largest file is BOMBERWORLD's 235,594 bytes, rewritten as 58,853: " + largest_file_name + " " + std::to_string(largest_file) + " / " +
                  std::to_string(largest_file_rewrite));
    }
    for (const std::string& b : beyond_new_limits) std::cout << "  beyond the limits of a new map: " << b << "\n";
    for (const std::string& b : files_beyond) std::cout << "  as a file beyond the limits of a new map: " << b << "\n";
    std::cout << "\n" << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
