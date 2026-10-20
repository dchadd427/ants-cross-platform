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
//   6. the converted map (rules 0) is read by the simulation as it is (Grid::init_from_level takes the relative anchors of version 9 through LevelData::object_anchor): its
//      Grid is equal to the Grid of its source cell by cell (every field, the anchors included, the hills, the plants, the food, the default ant type), and it PLAYS as its
//      source does, with the hashes of the source at the start and after every game second;
//   7. the rules word (the Grid carries it, the state hash mixes it when it is not 0): see the_rules_word_is_in_the_hash.
// The counts of the library (and of the dictionaries by the rule of the original maps) are those of docs/MAP_FORMAT.md. `test_map_roundtrip --ticks N` plays N ticks
// instead (the time of a CI run is the reason for the default), `--only TEXT` only the maps whose names hold TEXT (and checks no count).
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#include "map_library.hpp"
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
using ants_test::play_hashes;

namespace {

constexpr uint32_t kPlayTicks = 200;        // 10 game seconds: every map's start, its first orders, hatches and fights (all 540 maps, two plays each, in a few seconds of CI)
constexpr uint32_t kTicksPerSecond = ants_test::kLibraryTicksPerSecond;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "    FAILED: " << what << "\n";
    }
}

// The two grids are the same grid: the size, every cell, the hills, the plants, the food piles, the default ant type and the solid-bit source. "" when they are, else the first
// difference in words.
std::string grid_difference(const ants::assets::LevelData& a, const ants::assets::LevelData& b) {
    ants::sim::Grid ga;
    ants::sim::Grid gb;
    if (!ga.init_from_level(a) || !gb.init_from_level(b)) return "a grid did not load";
    if (ga.width() != gb.width() || ga.height() != gb.height()) return "the size";
    for (size_t i = 0; i < ga.cells().size(); ++i) {
        if (!ants_test::same_cell(ga.cells()[i], gb.cells()[i])) {
            return "the cell at row " + std::to_string(i / ga.width()) + ", column " + std::to_string(i % ga.width());
        }
    }
    if (ga.anthills().size() != gb.anthills().size()) return "the number of start markers";
    for (size_t i = 0; i < ga.anthills().size(); ++i) {
        const auto& x = ga.anthills()[i];
        const auto& y = gb.anthills()[i];
        if (x.tile_id != y.tile_id || x.x != y.x || x.y != y.y || x.team_id != y.team_id) return "a start marker";
    }
    if (ga.plants().size() != gb.plants().size()) return "the number of plants";
    for (size_t i = 0; i < ga.plants().size(); ++i) {
        const auto& x = ga.plants()[i];
        const auto& y = gb.plants()[i];
        if (x.tile_id != y.tile_id || x.x != y.x || x.y != y.y) return "a plant";
    }
    if (ga.food_objects().size() != gb.food_objects().size()) return "the number of food piles";
    for (size_t i = 0; i < ga.food_objects().size(); ++i) {
        const auto& x = ga.food_objects()[i];
        const auto& y = gb.food_objects()[i];
        if (x.row != y.row || x.col != y.col || x.units != y.units || x.value != y.value || x.remaining != y.remaining || x.thresholds != y.thresholds ||
            x.stage_tiles != y.stage_tiles) {
            return "a food pile";
        }
    }
    if (ga.default_ant_tile() != gb.default_ant_tile()) return "the default ant type";
    if (ga.exact_solid_bits() != gb.exact_solid_bits()) return "the source of the solid bits";
    return "";
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
    const std::vector<ants::sim::StateHash> plain = play_hashes(tiny, ticks);
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
        check(play_hashes(changed, ticks) != plain, std::string("the plays of TINY and of TINY with another ") + c.what + " differ");
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
    check(play_hashes(misplaced, ticks) != play_hashes(tiny, ticks), "TINY with every anchor one row off plays otherwise than TINY");
    check(play_hashes(up.level, ticks) == play_hashes(tiny, ticks), "TINY as an Extended map plays as TINY does");
}

// The rules word is state: a hand-made level at rules 1 hashes otherwise than the same level at rules 0 (the Grid carries it and the state hash mixes it), while the version 8
// level and the Extended level of rules 0 hash as they always did (the word is mixed in only when it is not 0). The play is the same level, so only the word differs.
void the_rules_word_is_in_the_hash(uint32_t ticks) {
    LevelData tiny;
    if (!tiny.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) {
        check(false, "TINY.LVL loads");
        return;
    }
    const LevelConversion up = ants::assets::convert_to_extended(tiny);
    check(up.ok, "TINY.LVL goes up to an Extended map (rules)");
    if (!up.ok) return;
    LevelData rules_one = up.level;
    rules_one.rules = ants::assets::LVL_RULES_EXTENDED;
    ants::sim::Grid zero;
    ants::sim::Grid one;
    ants::sim::Grid classic;
    check(zero.init_from_level(up.level) && one.init_from_level(rules_one) && classic.init_from_level(tiny), "the three TINY levels load into a Grid");
    check(zero.rules() == 0 && one.rules() == 1 && classic.rules() == 0, "the Grid carries the rules word of its level (0, 1, and 0 for a Classic map)");
    const std::vector<ants::sim::StateHash> classic_play = play_hashes(tiny, ticks);
    const std::vector<ants::sim::StateHash> zero_play = play_hashes(up.level, ticks);
    const std::vector<ants::sim::StateHash> one_play = play_hashes(rules_one, ticks);
    check(classic_play.size() == zero_play.size() && classic_play.size() == one_play.size() && classic_play.size() > 1, "the three plays have the same number of checkpoints");
    bool classic_is_zero = classic_play.size() == zero_play.size();
    bool one_moves_every_checkpoint = classic_play.size() == one_play.size();
    for (size_t i = 0; i < classic_play.size() && i < zero_play.size() && i < one_play.size(); ++i) {
        classic_is_zero = classic_is_zero && same_hash(classic_play[i], zero_play[i]);
        one_moves_every_checkpoint = one_moves_every_checkpoint && classic_play[i].total != one_play[i].total && classic_play[i].grid != one_play[i].grid;
    }
    // v8 does not move: the total hashes of TINY at the start and at tick 200 are those of main before the grid read version 9 (the hash ledger of tests/test_sim/hash_ledger.cpp)
    check(classic_play[0].total == 0x54d30843a600048bULL, "TINY's state hash at tick 0 is the one it always had");
    if (ticks >= 200) {
        check(classic_play.size() > 10, "the play of TINY reaches tick 200 (11 checkpoints)");
        check(classic_play.size() > 10 && classic_play[10].total == 0x6fa24979434adcd3ULL, "TINY's state hash at tick 200 is the one it always had");
    }
    ants::sim::Grid reused;
    check(reused.init_from_level(rules_one) && reused.rules() == 1 && reused.init_empty(4, 4) && reused.rules() == 0, "a Grid emptied by init_empty is at rules 0 again");
    check(classic_is_zero, "TINY as a Classic map and as an Extended map of rules 0 have the same hash at every checkpoint");
    check(one_moves_every_checkpoint, "TINY at rules 1 has another total hash and another grid hash than at rules 0 at every checkpoint");
    // only the grid part carries the word: the ants, the food, the players and the rest of the state are those of rules 0 at the start
    check(one_play[0].players == zero_play[0].players && one_play[0].food == zero_play[0].food && one_play[0].ants == zero_play[0].ants && one_play[0].engine == zero_play[0].engine,
          "at the start, only the grid hash of rules 1 differs from rules 0");
    // the word is mixed with its value: another word is another hash (the format allows 0 and 1 today; the Grid takes any)
    LevelData rules_two = up.level;
    rules_two.rules = 2;
    ants::sim::SimulationEngine e1;
    ants::sim::SimulationEngine e2;
    e1.init(rules_one, ants_test::kLibrarySeed, 0x0F);
    e2.init(rules_two, ants_test::kLibrarySeed, 0x0F);
    check(e1.state_hash().grid != e2.state_hash().grid, "a rules word of 2 hashes otherwise than a rules word of 1");
}

// The anchors of a version 9 map are relative to the cell and, in a map of more than 256 rows or columns, the plain sum (which a byte of version 8 cannot hold); the Grid takes
// them through LevelData::object_anchor, and a bomb tile (whose third word is the owner) and an empty cell have none.
void the_grid_reads_relative_anchors() {
    std::vector<std::string> names(1344, ".");
    LevelData tall = ants::assets::blank_extended_level(300, 20, names);
    check(tall.version == 9 && tall.height == 300, "a blank Extended map of 300 rows");
    const size_t width = tall.width;
    auto at = [&](uint32_t row, uint32_t column) -> ants::assets::MapCell& { return tall.layer2_interactive[row * width + column]; };
    // a cell of an object whose first cell is at row 287, column 4: three rows up and one column left of (290, 5)
    at(290, 5).tile_index = 97;
    at(290, 5).properties = static_cast<uint16_t>(0xFDu | (0xFFu << 8));
    // a cell whose anchor is 5 rows down and 3 columns right of it
    at(10, 2).tile_index = 97;
    at(10, 2).properties = static_cast<uint16_t>(0x05u | (0x03u << 8));
    // a cell whose anchor lies above the map (row -2): in a map of more than 256 rows the sum is plain, so the Grid keeps the negative number (Grid::food_object_at_cell reads a
    // negative anchor as "no anchor, the cell is its own": MAP_FORMAT.md)
    at(2, 3).tile_index = 97;
    at(2, 3).properties = static_cast<uint16_t>(0xFCu);
    // a bomb tile: the third word is the owner (here the team 2), no anchor
    at(20, 7).tile_index = 129;
    at(20, 7).properties = 2;
    ants::sim::Grid g;
    check(g.init_from_level(tall), "the tall Extended map loads into a Grid");
    const ants::sim::TileCell& far = g.get_cell(5, 290);
    check(far.anchor_y == 287 && far.anchor_x == 4, "an anchor above 255 is the sum of the cell and the signed offset: (287, 4), got (" + std::to_string(far.anchor_y) + ", " + std::to_string(far.anchor_x) + ")");
    const ants::sim::TileCell& down = g.get_cell(2, 10);
    check(down.anchor_y == 15 && down.anchor_x == 5, "a positive offset goes down and right: (15, 5), got (" + std::to_string(down.anchor_y) + ", " + std::to_string(down.anchor_x) + ")");
    const ants::sim::TileCell& above = g.get_cell(3, 2);
    check(above.anchor_y == -2 && above.anchor_x == 3, "an anchor above the map stays negative in a map of more than 256 rows: (-2, 3), got (" + std::to_string(above.anchor_y) + ", " + std::to_string(above.anchor_x) + ")");
    const ants::sim::TileCell& bomb = g.get_cell(7, 20);
    check(bomb.anchor_y == -1 && bomb.anchor_x == -1 && bomb.interactive_owner == 2, "a bomb tile has no anchor and keeps its owner");
    const ants::sim::TileCell& empty = g.get_cell(1, 1);
    check(empty.anchor_y == -1 && empty.anchor_x == -1, "an empty cell has no anchor");
    check(g.rules() == tall.rules && tall.rules == ants::assets::LVL_RULES_EXTENDED, "a blank Extended map above 90 rows is at rules 1 and the Grid says so");
}

// LevelLimits::for_play_extended(): Extended maps, up to 256 a side and 65,536 cells.
void the_play_limits() {
    constexpr LevelLimits play = LevelLimits::for_play_extended();
    check(play.allow_extended, "the play limits allow Extended maps");
    check(play.rows() == 256 && play.columns() == 256 && play.cells() == 65536, "the play limits are 256 a side and 65,536 cells");
    check(play.rows() < LevelLimits::for_extended_maps().rows() && play.cells() < LevelLimits::for_extended_maps().cells(), "the play limits are tighter than those of an Extended map");
    check(play.read_bytes() == 4u * 1024u * 1024u && play.write_bytes() == 3584u * 1024u, "a file of at most 4 MiB is read and a canonical file of 3.5 MiB written");
    std::vector<std::string> names(1344, ".");
    for (const uint32_t side : {100u, 255u, 256u}) {
        const LevelData level = ants::assets::blank_extended_level(side, side, names, LevelLimits::for_extended_maps());
        const LevelWriteResult w = ants::assets::write_level(level, play);
        LevelData back;
        check(w.ok && back.load_from_memory(w.bytes.data(), w.bytes.size(), nullptr, play) && back.version == 9 && back.height == side, "a blank Extended map of " + std::to_string(side) + " a side passes the play limits");
    }
    const LevelData wide = ants::assets::blank_extended_level(257, 100, names, LevelLimits::for_extended_maps());
    check(!ants::assets::write_level(wide, play).ok, "257 rows are beyond the play limits");
    const LevelWriteResult wide_file = ants::assets::write_level(wide, LevelLimits::for_extended_maps());
    LevelData refused;
    check(wide_file.ok && !refused.load_from_memory(wide_file.bytes.data(), wide_file.bytes.size(), nullptr, play), "and a file of 257 rows is refused by the play limits");
    // 256 x 257 cells (one side within, the other and the product above) is refused by the play limits
    const LevelData dense = ants::assets::blank_extended_level(256, 257, names, LevelLimits::for_extended_maps());
    check(!ants::assets::write_level(dense, play).ok, "256 x 257 cells are beyond the play limits");
    // a Classic level keeps the limits of the game under them
    LevelData tiny;
    check(tiny.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL"), "TINY.LVL loads (play limits)");
    check(ants::assets::write_level(tiny, play).ok, "a Classic map is written under the play limits too");
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
    const std::vector<ants_test::LibraryMap> maps = ants_test::library();
    std::map<RewriteChangeKind, int> maps_with;     // how many maps have a change of each kind
    int total = 0;
    int same_map = 0;
    int byte_identical = 0;
    int same_where_read = 0;                        // the same bytes once the bytes the original never reads are zeroed or dropped (no restructured part)
    int same_play = 0;
    int extended_same_level = 0;                    // the version 9 file reads back as the converted level, and writes twice the same bytes
    int extended_same_bytes = 0;                    // and converts back to the bytes of the rewrite
    int extended_refused = 0;                       // and a reader of Classic maps only refuses it (version_not_supported)
    int extended_same_grid = 0;                     // and the simulation's Grid of it is the Grid of the Classic map, cell by cell
    int extended_same_play = 0;
    uint64_t ticks_played = 0;
    size_t largest = 0;
    std::string largest_name;
    size_t largest_file = 0;                        // the largest file as it is, and the size of its rewrite
    std::string largest_file_name;
    size_t largest_file_rewrite = 0;
    std::vector<std::string> beyond_new_limits;     // the maps whose rewrite LevelLimits::for_new_maps() refuses, with the reason
    std::vector<std::string> files_beyond;          // the files that a reader with those limits refuses as they are (before a rewrite), with the reason
    const std::vector<uint8_t> archive = ants_test::read_file_bytes(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd");
    const std::vector<std::string> tile_names = ants::assets::tile_names_from_chd(archive.data(), archive.size());
    check(tile_names.size() == 1344u, "the tile names of ants.chd");
    int originals_rule = 0;                         // maps whose dictionary is the one of the original maps' rule (dictionary_of_used_tiles)
    int players_rule = 0;
    for (const ants_test::LibraryMap& m : maps) {
        if (!only.empty() && m.name.find(only) == std::string::npos) continue;
        ++total;
        const std::vector<uint8_t> source = ants_test::read_file_bytes(m.path);
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
        const std::vector<ants::sim::StateHash> pa = play_hashes(a, ticks);
        const std::vector<ants::sim::StateHash> pb = play_hashes(b, ticks);
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
        // 6. the same grid, the same play: the simulation reads the Extended map as it is
        const std::string grid_diff = read9 ? grid_difference(a, r9) : "the Extended map did not read";
        check(grid_diff.empty(), m.name + ": the Extended map (rules 0) gives the Grid of the Classic map (differs in " + grid_diff + ")");
        extended_same_grid += grid_diff.empty() ? 1 : 0;
        const std::vector<ants::sim::StateHash> pc = read9 ? play_hashes(r9, ticks) : std::vector<ants::sim::StateHash>{};
        bool equal9 = pa.size() == pc.size();
        size_t first9 = 0;
        for (; equal9 && first9 < pa.size(); ++first9) equal9 = same_hash(pa[first9], pc[first9]);
        check(equal9, m.name + ": the Extended map (rules 0) plays as the Classic map does (the hashes differ at checkpoint " + std::to_string(first9) + ")");
        extended_same_play += equal9 ? 1 : 0;
        ticks_played += 2u * static_cast<uint64_t>(pa.size() > 1 ? (pa.size() - 1) * kTicksPerSecond : 0);
    }
    if (only.empty()) the_fingerprint_sees_a_changed_map(ticks);
    if (only.empty()) the_fingerprint_sees_a_misplaced_anchor(ticks);
    if (only.empty()) the_rules_word_is_in_the_hash(ticks);
    if (only.empty()) the_grid_reads_relative_anchors();
    if (only.empty()) the_play_limits();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "  maps " << total << ": the same map " << same_map << ", the same play " << same_play << " (" << ticks << " ticks, all four teams)\n"
              << "  as Extended maps (version 9): the same level " << extended_same_level << ", back to the same bytes " << extended_same_bytes << ", refused by Classic-only readers "
              << extended_refused << ", the same Grid " << extended_same_grid << ", the same play " << extended_same_play << "\n"
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
        check(extended_same_level == total && extended_same_bytes == total && extended_refused == total && extended_same_grid == total && extended_same_play == total,
              "every map goes up to an Extended map, comes back as the same bytes, is refused by a reader of Classic maps only, gives the same Grid and plays the same: " + std::to_string(extended_same_level) + " / " +
                  std::to_string(extended_same_bytes) + " / " + std::to_string(extended_refused) + " / " + std::to_string(extended_same_grid) + " / " + std::to_string(extended_same_play));
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
