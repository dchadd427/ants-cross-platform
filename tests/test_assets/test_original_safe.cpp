// The original-safe profile of the map checker (include/ants_assets/lvl_checker.hpp, docs/MAP_FORMAT.md "The original-safe profile"):
//   S1 every rule: a level that breaks it (the code, its severity, its sentence and its cell) and one that keeps it, at the boundary where there is one
//   S2 the report: "works in the original game" is no Fatal and no Warning, a note does not count; the codes' texts are stable and distinct; the order
//   S3 files: what the reader refuses, with its sentence; a file that loads is the profile of its level and of what its file left out
//   S4 stricter than the remake: a file that the profile calls original-safe has no Fatal and no Warning in the remake's own check (hostile files, fixed seeds)
//   S6 the checker per kind (check_map): the 90 rows of a Classic map and the wall, the headline by kind, the remake profile of an Extended map, the library by kind
//   S5 the library: the six original maps and the 534 players' maps, the numbers of docs/MAP_FORMAT.md, and a hash of every finding (a golden taken before the two kinds)
#include "ants_assets/lvl_checker.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_test_paths.hpp"
#include "lvl_bytes.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace ants::assets;
using namespace lvl_edit;
using Code = OriginalSafeCode;
using Severity = LevelProblemSeverity;

int g_checks = 0;
int g_failures = 0;
int g_cases = 0;

void check(bool ok, const std::string& what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "    FAILED: " << what << " (line " << line << ")\n";
    }
}
#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_MSG(cond, text) check((cond), std::string(#cond) + ": " + (text), __LINE__)

void run_case(const std::string& name, const std::function<void()>& fn) {
    ++g_cases;
    std::cout << "  RUNNING: " << name << " ... " << std::flush;
    const int before = g_failures;
    fn();
    if (g_failures == before) std::cout << "PASS\n";
}

const std::string& maps_dir() {
    static const std::string dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
    return dir;
}

const char* const kOriginals[] = {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"};

const std::vector<uint8_t>& tiny_bytes() {
    static const std::vector<uint8_t> bytes = read_map(maps_dir() + "/TINY.LVL");
    return bytes;
}

// TINY as it is read: an original map, so original-safe; every case below changes one thing of it
const LevelData& tiny() {
    static const LevelData level = [] {
        LevelData l;
        l.load_from_file(maps_dir() + "/TINY.LVL");
        return l;
    }();
    return level;
}

size_t at(const LevelData& l, uint32_t row, uint32_t column) { return static_cast<size_t>(row) * l.width + column; }

// TINY's hills (rows x columns 31 x 31): the entrance of each color and its 4 x 4 footprint, one row and one column up and left of it
struct Hill {
    int team;
    uint32_t row;
    uint32_t column;
};
const Hill kTinyHills[4] = {{0, 5, 5}, {1, 24, 24}, {2, 5, 24}, {3, 24, 5}};

void clear_hill(LevelData& l, const Hill& h) {
    for (uint32_t r = h.row - 1; r <= h.row + 2; ++r) {
        for (uint32_t c = h.column - 1; c <= h.column + 2; ++c) l.layer2_interactive[at(l, r, c)] = MapCell{LVL_EMPTY_TILE, 0, 0};
    }
}

// a whole hill of `team` with its entrance at (row, column): 16 cells of its id, the entrance marked (flag bit 0), every cell anchored at the entrance
void put_hill(LevelData& l, int team, uint32_t row, uint32_t column) {
    const uint16_t id = static_cast<uint16_t>(team == 0 ? 248 : team == 1 ? 247 : team == 2 ? 246 : 245);
    for (uint32_t r = row - 1; r <= row + 2; ++r) {
        for (uint32_t c = column - 1; c <= column + 2; ++c) {
            const bool entrance = r == row && c == column;
            l.layer2_interactive[at(l, r, c)] = MapCell{id, static_cast<uint16_t>(entrance ? 1 : 0), static_cast<uint16_t>(row | (column << 8))};
        }
    }
}

std::vector<const OriginalSafeIssue*> all_of(const OriginalSafeReport& r, Code code) {
    std::vector<const OriginalSafeIssue*> out;
    for (const OriginalSafeIssue& i : r.issues) {
        if (i.code == code) out.push_back(&i);
    }
    return out;
}

// The report has exactly one finding of `code`, with this severity, sentence, cell and team
bool finds(const OriginalSafeReport& r, Code code, Severity severity, const std::string& message, int32_t row = -1, int32_t column = -1, int8_t team = -1) {
    const std::vector<const OriginalSafeIssue*> found = all_of(r, code);
    if (found.size() != 1) {
        std::cout << "      (" << found.size() << " findings of " << to_string(code) << " in:\n" << r.describe() << ")\n";
        return false;
    }
    const OriginalSafeIssue& i = *found[0];
    const bool ok = i.severity == severity && i.message == message && i.row == row && i.column == column && i.team == team;
    if (!ok) {
        std::cout << "      (found: " << to_string(i.severity) << " \"" << i.message << "\" row " << i.row << " column " << i.column << " team " << int(i.team) << ")\n";
    }
    return ok;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// S1 Every rule
// ------------------------------------------------------------------------------------------------------------------------------------------------------

void s1_rules() {
    run_case("S1.0 The Level Every Case Starts From: The Six Original Maps Work In The Original Game, With No Finding At All", [] {
        for (const char* name : kOriginals) {
            LevelData l;
            CHECK_MSG(l.load_from_file(maps_dir() + "/" + name), name);
            const OriginalSafeReport r = check_original_safe(l);
            CHECK_MSG(r.works_in_original_game() && r.issues.empty(), std::string(name) + ":\n" + r.describe());
        }
    });

    run_case("S1.1 Version 8: Version 10 Is Fatal (The Original Says \"Wrong Version\" And Starts Without A Map)", [] {
        LevelData l = tiny();
        l.version = 10;
        const OriginalSafeReport r = check_original_safe(l);
        CHECK(finds(r, Code::VersionNot8, Severity::Fatal, "the map is version 10: the original plays version 8 only (it says \"wrong version\" and starts without a map)"));
        CHECK(!r.works_in_original_game());
        l.version = 8;
        CHECK(!check_original_safe(l).has(Code::VersionNot8));
    });

    run_case("S1.2 Mode 1: Another Mode Is A Note (The Original Never Reads It), And The Map Still Works In The Original Game", [] {
        LevelData l = tiny();
        l.game_mode = 4631;
        const OriginalSafeReport r = check_original_safe(l);
        CHECK(finds(r, Code::ModeNot1, Severity::Info, "the mode word is 4631, not 1: the original never reads it (the map maker writes 1)"));
        CHECK(r.works_in_original_game());
        l.game_mode = 1;
        CHECK(!check_original_safe(l).has(Code::ModeNot1));
    });

    run_case("S1.3 A Description Of At Most 29 Bytes: 30 Are A Warning (No Zero Byte Is Left To End The Text), 29 Are None", [] {
        LevelData l = tiny();
        l.description = std::string(30, 'a');
        CHECK(finds(check_original_safe(l), Code::DescriptionTooLong, Severity::Warning,
                    "the description is 30 bytes long: at most 29 fit, because the original reads 30 bytes and a text needs a zero byte at its end (a letter outside a to z takes 2 or 3 bytes)"));
        l.description = std::string(29, 'a');
        CHECK(!check_original_safe(l).has(Code::DescriptionTooLong));
        // bytes, not letters: 15 letters of 2 bytes each (a browser editor writes UTF-8) are 30 bytes
        l.description.clear();
        for (int i = 0; i < 15; ++i) l.description += "\xC3\xA9";
        CHECK(finds(check_original_safe(l), Code::DescriptionTooLong, Severity::Warning,
                    "the description is 30 bytes long: at most 29 fit, because the original reads 30 bytes and a text needs a zero byte at its end (a letter outside a to z takes 2 or 3 bytes)"));
    });

    run_case("S1.4 A Grid With Cells, At Most 256 A Side, Whose Layers Hold Its Cells: 0 Rows, 257 Rows Or Layers Of Another Size Are Fatal", [] {
        LevelData l = tiny();
        l.height.val = 0;
        CHECK(finds(check_original_safe(l), Code::GridEmpty, Severity::Fatal, "the map has 0 rows x 31 columns: it needs at least one cell"));
        const std::vector<std::string> names(1344, "n");
        CHECK(finds(check_original_safe(blank_level(257, 2, names)), Code::GridTooLarge, Severity::Fatal,
                    "the map has 257 rows x 2 columns: at most 256 a side fit (an object's anchor holds its row and its column in a byte each)"));
        CHECK(!check_original_safe(blank_level(256, 2, names)).has(Code::GridTooLarge));
        l = tiny();
        l.layer2_interactive.pop_back();
        CHECK(finds(check_original_safe(l), Code::LayersNotGrid, Severity::Fatal, "the layers hold 961 and 960 cells, the grid has 31 x 31 = 961"));
        CHECK(!check_original_safe(tiny()).has(Code::LayersNotGrid));
    });

    run_case("S1.5 At Most 1,344 Dictionary Names: 1,345 Overrun The Original's Table (Fatal), 1,344 Do Not", [] {
        LevelData l = tiny();
        l.tile_dictionary.resize(1345, ".");
        l.tile_type_count = 1344;
        CHECK(finds(check_original_safe(l), Code::DictionaryTooLarge, Severity::Fatal, "the dictionary has 1345 names: the original's table holds 1,344 and more overrun it"));
        l.tile_dictionary.resize(1344);
        l.tile_type_count = 1343;
        CHECK(!check_original_safe(l).has(Code::DictionaryTooLarge));
    });

    run_case("S1.6 Every Tile Inside The Dictionary: A Ground Cell, An Object Cell, A Block-1 Record, A Food Stage And The Default Ant Type At Its Size Are Fatal, One Less Is Not", [] {
        const size_t names = tiny().tile_dictionary.size();     // 670
        const uint16_t beyond = static_cast<uint16_t>(names);
        const uint16_t last = static_cast<uint16_t>(names - 1);
        struct Place {
            std::function<void(LevelData&, uint16_t)> set;
            std::string words;
            int32_t row;
            int32_t column;
        };
        const std::vector<Place> places = {
            {[](LevelData& l, uint16_t t) { l.layer1_terrain[at(l, 3, 7)].tile_index = t; }, "one ground cell uses a tile beyond the dictionary of 670 names (row 3, column 7: tile 670): the original reads past its tables there", 3, 7},
            {[](LevelData& l, uint16_t t) { l.layer2_interactive[at(l, 12, 1)].tile_index = t; }, "one object cell uses a tile beyond the dictionary of 670 names (row 12, column 1: tile 670): the original reads past its tables there", 12, 1},
            {[](LevelData& l, uint16_t t) { l.anthill_spawns.push_back(AnthillSpawn{t, 2, 3, 255}); }, "one entry of the list of start markers and plants uses a tile beyond the dictionary of 670 names (row 2, column 3: tile 670): the original reads past its tables there", 2, 3},
            {[](LevelData& l, uint16_t t) { l.food_schedules[1].variants[0].tile_id = t; }, "one food stage uses a tile beyond the dictionary of 670 names (the pile at row 16, column 14: tile 670): the original reads past its tables there", 16, 14},
            {[](LevelData& l, uint16_t t) { l.ambient_tile_or_sound = t; }, "the default ant type is tile 670, beyond the dictionary of 670 names: the original reads past its tables there", -1, -1},
        };
        CHECK(tiny().food_schedules.size() > 1 && tiny().food_schedules[1].y == 16 && tiny().food_schedules[1].x == 14);
        for (const Place& p : places) {
            LevelData l = tiny();
            p.set(l, beyond);
            CHECK_MSG(finds(check_original_safe(l), Code::TileOutsideDictionary, Severity::Fatal, p.words, p.row, p.column), p.words);
            l = tiny();
            p.set(l, last);
            CHECK_MSG(!check_original_safe(l).has(Code::TileOutsideDictionary), p.words);
        }
        // several cells: counted, the first named; the empty tile 0x7FFE of an object cell, a food stage and block 3 is no tile
        LevelData l = tiny();
        l.layer2_interactive[at(l, 30, 30)].tile_index = 900;
        l.layer2_interactive[at(l, 0, 2)].tile_index = 0xFFFF;
        CHECK(finds(check_original_safe(l), Code::TileOutsideDictionary, Severity::Fatal,
                    "2 object cells use tiles beyond the dictionary of 670 names (the first at row 0, column 2: tile 65535): the original reads past its tables there", 0, 2));
        CHECK(all_of(check_original_safe(l), Code::TileOutsideDictionary)[0]->count == 2u);
        l = tiny();
        l.food_schedules[0].variants.back().tile_id = LVL_EMPTY_TILE;
        l.ambient_tile_or_sound = LVL_EMPTY_TILE;
        CHECK(!check_original_safe(l).has(Code::TileOutsideDictionary));
    });

    run_case("S1.7 Every Ground Cell Has A Ground Tile: The Empty Tile 0x7FFE On Layer 1 Is Fatal (The Original Reads Its Terrain Table Past Its End)", [] {
        LevelData l = tiny();
        l.layer1_terrain[at(l, 0, 3)].tile_index = LVL_EMPTY_TILE;
        l.layer1_terrain[at(l, 9, 9)].tile_index = LVL_EMPTY_TILE;
        CHECK(finds(check_original_safe(l), Code::GroundMissing, Severity::Fatal,
                    "2 cells have no ground tile (the empty tile 32766; the first at row 0, column 3): the original reads its terrain table past its end there", 0, 3));
        CHECK(!check_original_safe(tiny()).has(Code::GroundMissing));
    });

    run_case("S1.8 Every Start Marker Inside The Grid: One At Row 31 Of A 31-Row Map Is Fatal (The Original Writes Through It Without A Check), Row 30 Is Not", [] {
        LevelData l = tiny();
        AnthillSpawn* blue = nullptr;
        for (AnthillSpawn& sp : l.anthill_spawns) {
            if (sp.team_id == 2 && blue == nullptr) blue = &sp;
        }
        CHECK(blue != nullptr);
        if (blue == nullptr) return;
        blue->y = 31;
        blue->x = 12;
        CHECK(finds(check_original_safe(l), Code::StartMarkerOutsideMap, Severity::Fatal, "a start marker of Blue stands outside the map (row 31, column 12)", 31, 12, 2));
        blue->y = 30;
        CHECK(!check_original_safe(l).has(Code::StartMarkerOutsideMap));
        blue->x = 31;
        CHECK(check_original_safe(l).has(Code::StartMarkerOutsideMap));
        // two of one team: counted, the first named
        for (AnthillSpawn& sp : l.anthill_spawns) {
            if (sp.team_id == 3) sp.y = 400;
        }
        const OriginalSafeReport r = check_original_safe(l);
        const std::vector<const OriginalSafeIssue*> black = all_of(r, Code::StartMarkerOutsideMap);
        CHECK(black.size() == 2u && black[1]->team == 3 && black[1]->count == 3u && black[1]->message == "3 start markers of Black stand outside the map (the first at row 400, column 8)");
    });

    run_case("S1.9 Every Food Pile Inside The Grid: One At Column 31 Is Fatal, Column 30 Is Not", [] {
        LevelData l = tiny();
        l.food_schedules[2].y = 12;
        l.food_schedules[2].x = 31;
        CHECK(finds(check_original_safe(l), Code::FoodPileOutsideMap, Severity::Fatal, "one food pile stands outside the map (row 12, column 31)", 12, 31));
        l.food_schedules[2].x = 30;
        CHECK(!check_original_safe(l).has(Code::FoodPileOutsideMap));
        l.food_schedules[0].y = 31;
        l.food_schedules[5].y = 40;
        CHECK(finds(check_original_safe(l), Code::FoodPileOutsideMap, Severity::Fatal, "2 food piles stand outside the map (the first at row 31, column " +
                    std::to_string(l.food_schedules[0].x) + ")", 31, static_cast<int32_t>(l.food_schedules[0].x)));
    });

    run_case("S1.10 Every Food Pile Has A Stage: One Without Is Fatal (The Original Stops Reading The Food There), One Stage Is Enough", [] {
        LevelData l = tiny();
        l.food_schedules[3].variants.clear();
        const uint16_t row = l.food_schedules[3].y;
        const uint16_t column = l.food_schedules[3].x;
        CHECK(finds(check_original_safe(l), Code::FoodPileWithoutStages, Severity::Fatal,
                    "the food pile at row " + std::to_string(row) + ", column " + std::to_string(column) +
                        " has no stages: the original stops reading the food there and reads the rest of the map from the wrong place",
                    row, column));
        l.food_schedules[3].variants.push_back(FoodItemVariant{1, 253});
        CHECK(!check_original_safe(l).has(Code::FoodPileWithoutStages));
    });

    run_case("S1.11 Each Of The Four Teams Has A Hill: A Team Without Is A Warning (\"Blue Has No Hill\"), A Hill Moved Elsewhere Is None", [] {
        LevelData l = tiny();
        clear_hill(l, kTinyHills[2]);
        CHECK(finds(check_original_safe(l), Code::HillMissing, Severity::Warning, "Blue has no hill", -1, -1, 2));
        CHECK(!check_original_safe(l).works_in_original_game());
        put_hill(l, 2, 5, 14);                                                     // (rows 4 .. 7, columns 13 .. 16 of TINY are free)
        CHECK(!check_original_safe(l).has(Code::HillMissing));
        CHECK(check_original_safe(l).works_in_original_game());
        l = tiny();
        for (const Hill& h : kTinyHills) clear_hill(l, h);
        CHECK(all_of(check_original_safe(l), Code::HillMissing).size() == 4u);
    });

    run_case("S1.12 A Hill Has Its Entrance: Hill Cells Without The Entrance Mark Are A Warning, The Mark Back Is None", [] {
        LevelData l = tiny();
        l.layer2_interactive[at(l, 24, 24)].flags = 0;
        CHECK(finds(check_original_safe(l), Code::HillWithoutEntrance, Severity::Warning, "the hill of Red has no entrance: none of its 16 cells is marked as the entrance",
                    23, 23, 1));
        CHECK(!check_original_safe(l).has(Code::HillMissing));
        l.layer2_interactive[at(l, 24, 24)].flags = 1;
        CHECK(!check_original_safe(l).has(Code::HillWithoutEntrance));
    });

    run_case("S1.13 One Hill A Team: A Second Green Hill Is A Warning, Named By Its Entrances; One Hill Is None", [] {
        LevelData l = tiny();
        put_hill(l, 0, 5, 14);
        CHECK(finds(check_original_safe(l), Code::HillsSeveral, Severity::Warning, "Green has 2 hills (entrances at row 5, column 5 and at row 5, column 14)", 5, 5, 0));
        CHECK(!check_original_safe(tiny()).has(Code::HillsSeveral));
    });

    run_case("S1.14 Each Team Has Starting Ants: A Team Without A Start Marker Is A Warning (\"Black Has No Starting Ants\"), One Marker Is Enough; Each Team Has Its Own Tile", [] {
        const char* const names[4] = {"Green", "Red", "Blue", "Black"};
        const int tiles[4] = {154, 155, 153, 152};
        for (int team = 0; team < 4; ++team) {
            LevelData l = tiny();
            std::vector<AnthillSpawn> kept;
            for (const AnthillSpawn& sp : l.anthill_spawns) {
                if (sp.team_id != team) kept.push_back(sp);
            }
            l.anthill_spawns.assign(kept.begin(), kept.end());
            CHECK_MSG(finds(check_original_safe(l), Code::StartingAntsMissing, Severity::Warning,
                            std::string(names[team]) + " has no starting ants (no start marker, tile " + std::to_string(tiles[team]) + ")", -1, -1, static_cast<int8_t>(team)),
                      names[team]);
            bool one = false;
            for (const AnthillSpawn& sp : tiny().anthill_spawns) {
                if (sp.team_id == team && !one) {
                    l.anthill_spawns.push_back(sp);
                    one = true;
                }
            }
            CHECK_MSG(one && !check_original_safe(l).has(Code::StartingAntsMissing), names[team]);
            // the marker of another team does not stand in for it
            LevelData other = tiny();
            for (AnthillSpawn& sp : other.anthill_spawns) {
                if (sp.team_id == team) sp.team_id = static_cast<uint8_t>((team + 1) % 4);
            }
            CHECK_MSG(check_original_safe(other).has(Code::StartingAntsMissing), names[team]);
        }
    });

    run_case("S1.15 A Hill That Is Not Whole Is A Note: A Cell Anchored Elsewhere, A Cell Of The Color Outside Its 4 x 4 Hill; The Map Still Works", [] {
        LevelData l = tiny();
        l.layer2_interactive[at(l, 25, 26)].properties = static_cast<uint16_t>(0 | (0 << 8));
        OriginalSafeReport r = check_original_safe(l);
        CHECK(finds(r, Code::HillNotWhole, Severity::Info, "the hill of Red is not whole: one cell (row 25, column 26) differs from the 4 x 4 hill around its entrance (row 24, column 24)",
                    25, 26, 1));
        CHECK(r.works_in_original_game());
        l = tiny();
        l.layer2_interactive[at(l, 0, 0)] = MapCell{248, 0, static_cast<uint16_t>(5 | (5 << 8))};
        l.layer2_interactive[at(l, 6, 6)] = MapCell{LVL_EMPTY_TILE, 0, 0};
        r = check_original_safe(l);
        CHECK(finds(r, Code::HillNotWhole, Severity::Info,
                    "the hill of Green is not whole: 2 cells (the first at row 0, column 0) differ from the 4 x 4 hill around its entrance (row 5, column 5)", 0, 0, 0));
        CHECK(!check_original_safe(tiny()).has(Code::HillNotWhole));
        // a stray cell of the color just beside the 4 x 4 (Red's cover columns 23 .. 26, rows 23 .. 26): column 27, and the column before the first, 22
        for (const uint32_t column : {27u, 22u}) {
            l = tiny();
            l.layer2_interactive[at(l, 24, column)] = MapCell{247, 0, static_cast<uint16_t>(24 | (24 << 8))};
            CHECK_MSG(finds(check_original_safe(l), Code::HillNotWhole, Severity::Info,
                            "the hill of Red is not whole: one cell (row 24, column " + std::to_string(column) + ") differs from the 4 x 4 hill around its entrance (row 24, column 24)", 24,
                            static_cast<int32_t>(column), 1),
                      std::to_string(column));
        }
        // the same just below and above
        for (const uint32_t row : {27u, 22u}) {
            l = tiny();
            l.layer2_interactive[at(l, row, 24)] = MapCell{247, 0, static_cast<uint16_t>(24 | (24 << 8))};
            CHECK_MSG(check_original_safe(l).has(Code::HillNotWhole), std::to_string(row));
        }
    });

    run_case("S1.15b A Hill Whose Missing Cells Are All Off The Map Does Not Name Its Entrance As The Cell That Differs", [] {
        // Green's entrance in the corner (row 0, column 0): the cells of its 4 x 4 that lie above and left of the map are 7 of the 16
        LevelData l = tiny();
        clear_hill(l, kTinyHills[0]);
        for (uint32_t r = 0; r <= 2; ++r) {
            for (uint32_t c = 0; c <= 2; ++c) l.layer2_interactive[at(l, r, c)] = MapCell{248, static_cast<uint16_t>(r == 0 && c == 0 ? 1 : 0), 0};
        }
        const OriginalSafeReport r = check_original_safe(l);
        CHECK(finds(r, Code::HillNotWhole, Severity::Info,
                    "the hill of Green is not whole: 7 cells of the 4 x 4 hill around its entrance (row 0, column 0) lie outside the map", 0, 0, 0));
        CHECK(all_of(r, Code::HillNotWhole)[0]->count == 7u);
        CHECK(r.works_in_original_game());
        // one cell on the map that differs as well: it is the one named
        l.layer2_interactive[at(l, 2, 1)].properties = 99;
        CHECK(finds(check_original_safe(l), Code::HillNotWhole, Severity::Info,
                    "the hill of Green is not whole: 8 cells (the first at row 2, column 1) differ from the 4 x 4 hill around its entrance (row 0, column 0)", 2, 1, 0));
    });

    run_case("S1.16 Tiles That The Dictionary Names \".\" Are Not Drawn By The Original (A Note), Unless They Are Among The 62 It Always Loads", [] {
        LevelData l = tiny();
        const uint16_t ground = l.layer1_terrain[at(l, 0, 0)].tile_index;           // 666, m01e_b
        uint32_t cells = 0;
        for (const MapCell& c : l.layer1_terrain) cells += c.tile_index == ground ? 1u : 0u;
        for (const MapCell& c : l.layer2_interactive) cells += c.tile_index == ground ? 1u : 0u;
        l.tile_dictionary[ground] = ".";
        const OriginalSafeReport r = check_original_safe(l);
        CHECK(cells > 1u && finds(r, Code::TileNotDrawn, Severity::Info,
                                  std::to_string(cells) + " cells use tiles that the dictionary names \".\" (the first at row 0, column 0: tile 666): the original does not load "
                                  "their pictures, so it does not draw them", 0, 0));
        CHECK(r.first(Code::TileNotDrawn) != nullptr && r.first(Code::TileNotDrawn)->count == cells);
        CHECK(r.works_in_original_game());
        l = tiny();
        l.tile_dictionary[313] = ".";                                              // pin1, the object of one cell of TINY
        CHECK(finds(check_original_safe(l), Code::TileNotDrawn, Severity::Info,
                    "one cell uses a tile that the dictionary names \".\" (row 12, column 16: tile 313): the original does not load its picture, so it does not draw it", 12, 16));
        // a hill (245 .. 248) and a power-up (62 .. 66) are always loaded
        l = tiny();
        l.tile_dictionary[246] = ".";
        CHECK(!check_original_safe(l).has(Code::TileNotDrawn));
        CHECK(!check_original_safe(tiny()).has(Code::TileNotDrawn));
    });

    run_case("S1.17 A Food Pile Whose Stage Tile The Original Does Not Load Shows No Pile From That Stage On (A Warning: Nobody Can See Or Harvest It); Two Such Piles Are Counted", [] {
        LevelData l = tiny();
        l.tile_dictionary[285] = ".";                                              // the one stage of pile 7, at row 17, column 15
        OriginalSafeReport r = check_original_safe(l);
        CHECK(finds(r, Code::FoodPileNotShown, Severity::Warning,
                    "the food pile at row 17, column 15 does not show in the original from stage 1 on: its tile 285 is named \".\" in the dictionary, the original places no pile "
                    "of a tile whose picture it does not load, and nobody can see or harvest a pile that is not placed", 17, 15));
        CHECK(!r.works_in_original_game());
        l = tiny();
        l.tile_dictionary[273] = ".";                                              // stage 2 of piles 12 and 13
        r = check_original_safe(l);
        CHECK(finds(r, Code::FoodPileNotShown, Severity::Warning,
                    "2 food piles do not show in the original from a stage whose tile the dictionary names \".\" (the first at row 15, column 6, from stage 2 on: tile 273): "
                    "the original places no pile of a tile whose picture it does not load, and nobody can see or harvest a pile that is not placed", 15, 6));
        CHECK(!r.works_in_original_game());
        CHECK(!check_original_safe(tiny()).has(Code::FoodPileNotShown));
    });

    run_case("S1.18 Plants And Waypoints Outside The Grid Are A Note: The Original Ignores Them", [] {
        LevelData l = tiny();
        l.anthill_spawns.push_back(AnthillSpawn{421, 40, 2, 255});
        Waypoint w;
        w.y = 3;
        w.x = 99;
        l.waypoints.push_back(w);
        const OriginalSafeReport r = check_original_safe(l);
        CHECK(finds(r, Code::ObjectOutsideMap, Severity::Info, "one plant and one waypoint stand outside the map (the first at row 40, column 2): the original ignores them", 40, 2));
        CHECK(r.works_in_original_game());
        CHECK(!check_original_safe(tiny()).has(Code::ObjectOutsideMap));
        // the first row and the first column after the grid are outside, the last ones inside (a plant: TINY has 31 rows and 31 columns)
        l = tiny();
        l.anthill_spawns.push_back(AnthillSpawn{421, 31, 2, 255});
        CHECK(finds(check_original_safe(l), Code::ObjectOutsideMap, Severity::Info, "one plant stands outside the map (row 31, column 2): the original ignores it", 31, 2));
        l = tiny();
        l.anthill_spawns.push_back(AnthillSpawn{421, 2, 31, 255});
        CHECK(finds(check_original_safe(l), Code::ObjectOutsideMap, Severity::Info, "one plant stands outside the map (row 2, column 31): the original ignores it", 2, 31));
        l = tiny();
        l.anthill_spawns.push_back(AnthillSpawn{421, 30, 30, 255});
        CHECK(!check_original_safe(l).has(Code::ObjectOutsideMap));
        // several: counted, the first named
        l = tiny();
        l.anthill_spawns.push_back(AnthillSpawn{421, 40, 2, 255});
        l.anthill_spawns.push_back(AnthillSpawn{421, 3, 50, 255});
        CHECK(finds(check_original_safe(l), Code::ObjectOutsideMap, Severity::Info, "2 plants stand outside the map (the first at row 40, column 2): the original ignores them", 40, 2));
    });

    run_case("S1.19 Hills Of Two Teams Over Each Other Are No Rule (The Notes Are Silent): Red's 4 x 4 Over One Cell Of Green's Gives Only The Note On Green's Hill, And The Map Works", [] {
        LevelData l = tiny();
        clear_hill(l, kTinyHills[1]);
        put_hill(l, 1, 8, 8);                                                      // rows 7 .. 10, columns 7 .. 10; Green's 4 x 4 is rows 4 .. 7, columns 4 .. 7: one cell is shared
        const OriginalSafeReport r = check_original_safe(l);
        CHECK(finds(r, Code::HillNotWhole, Severity::Info,
                    "the hill of Green is not whole: one cell (row 7, column 7) differs from the 4 x 4 hill around its entrance (row 5, column 5)", 7, 7, 0));
        CHECK(r.issues.size() == 1u && r.works_in_original_game());
    });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// S2 The report
// ------------------------------------------------------------------------------------------------------------------------------------------------------

void s2_report() {
    run_case("S2.1 The Codes' Texts Are Stable And Distinct (The Editor And The Server Key On Them)", [] {
        const std::vector<std::pair<Code, std::string>> codes = {
            {Code::FileUnreadable, "file_unreadable"}, {Code::FileTooLarge, "file_too_large"}, {Code::FileCutShort, "file_cut_short"},
            {Code::FileCutInBlock4, "file_cut_in_block_4"}, {Code::EggWordMissing, "egg_word_missing"}, {Code::FoodBlockEndedEarly, "food_block_ended_early"},
            {Code::VersionNot8, "version_not_8"}, {Code::ModeNot1, "mode_not_1"}, {Code::DescriptionTooLong, "description_too_long"}, {Code::GridEmpty, "grid_empty"},
            {Code::GridTooLarge, "grid_too_large"}, {Code::LayersNotGrid, "layers_not_grid"}, {Code::DictionaryTooLarge, "dictionary_too_large"},
            {Code::TileOutsideDictionary, "tile_outside_dictionary"}, {Code::GroundMissing, "ground_missing"}, {Code::StartMarkerOutsideMap, "start_marker_outside_map"},
            {Code::FoodPileOutsideMap, "food_pile_outside_map"}, {Code::FoodPileWithoutStages, "food_pile_without_stages"}, {Code::HillMissing, "hill_missing"},
            {Code::HillWithoutEntrance, "hill_without_entrance"}, {Code::HillsSeveral, "hills_several"}, {Code::StartingAntsMissing, "starting_ants_missing"},
            {Code::HillNotWhole, "hill_not_whole"}, {Code::TileNotDrawn, "tile_not_drawn"}, {Code::FoodPileNotShown, "food_pile_not_shown"},
            {Code::ObjectOutsideMap, "object_outside_map"}, {Code::NotForOriginal, "not_for_original"},
        };
        std::set<std::string> texts;
        for (const auto& c : codes) {
            CHECK_MSG(c.second == to_string(c.first), c.second);
            texts.insert(to_string(c.first));
        }
        CHECK(texts.size() == codes.size());
        CHECK(std::string(to_string(static_cast<Code>(200))) == "?");
    });

    run_case("S2.2 Works In The Original Game Is No Fatal And No Warning: A Note Alone Keeps It, One Warning Or One Fatal Ends It", [] {
        OriginalSafeReport r;
        CHECK(r.works_in_original_game());
        r.issues.push_back(OriginalSafeIssue{Code::ModeNot1, Severity::Info, "a note", -1, -1, -1, 1});
        CHECK(r.works_in_original_game() && r.has(Code::ModeNot1) && !r.has(Code::HillMissing) && r.first(Code::HillMissing) == nullptr);
        r.issues.push_back(OriginalSafeIssue{Code::HillMissing, Severity::Warning, "Blue has no hill", -1, -1, 2, 1});
        CHECK(!r.works_in_original_game());
        r.issues.pop_back();
        r.issues.push_back(OriginalSafeIssue{Code::VersionNot8, Severity::Fatal, "version", -1, -1, -1, 1});
        CHECK(!r.works_in_original_game());
        CHECK(r.describe() == "note mode_not_1: a note\nfatal version_not_8: version\n");
    });

    run_case("S2.3 The Order Is Fixed: The Header, The Grid, The Tiles, The Start Markers, The Food, The Hills And The Teams, The Notes", [] {
        LevelData l = tiny();
        l.game_mode = 2;
        l.description = std::string(30, 'z');
        clear_hill(l, kTinyHills[0]);
        l.food_schedules[0].x = 200;
        l.layer1_terrain[at(l, 1, 1)].tile_index = 2000;
        const OriginalSafeReport r = check_original_safe(l);
        std::vector<std::string> order;
        for (const OriginalSafeIssue& i : r.issues) order.push_back(to_string(i.code));
        CHECK_MSG((order == std::vector<std::string>{"description_too_long", "tile_outside_dictionary", "food_pile_outside_map", "hill_missing", "mode_not_1"}), r.describe());
    });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// S3 Files
// ------------------------------------------------------------------------------------------------------------------------------------------------------

void s3_files() {
    const std::vector<uint8_t> tiny_file = read_map(maps_dir() + "/TINY.LVL");
    Spots t;
    const bool located = locate(tiny_file, t);

    run_case("S3.1 What The Reader Refuses Is Fatal, With The Reader's Sentence: No Bytes, Version 10, A File Cut Before Block 4, A File Over The Limits", [&] {
        CHECK(located);
        OriginalSafeReport r = check_original_safe(nullptr, 0);
        CHECK(finds(r, Code::FileUnreadable, Severity::Fatal, "no data"));
        std::vector<uint8_t> m = tiny_file;
        wr32(m, 0, 10);
        r = check_original_safe(m.data(), m.size());
        CHECK(r.issues.size() == 1u && r.has(Code::VersionNot8) && r.issues[0].severity == Severity::Fatal);
        CHECK(r.issues.size() == 1u && r.issues[0].message.find("header version 10") == 0);
        r = check_original_safe(tiny_file.data(), 10000);
        CHECK(r.issues.size() == 1u && r.has(Code::FileCutShort) && r.issues[0].message.find("the file ends inside the layers") == 0);
        r = check_original_safe(tiny_file.data(), tiny_file.size(), LevelLimits{256, 256, 1344, 1000});
        CHECK(r.issues.size() == 1u && r.has(Code::FileTooLarge) && r.issues[0].message == "the file has 19312 bytes, more than the 1000 allowed here: nothing of it is read");
        const std::vector<uint8_t> wide = flat_map(1, 1, 300, 1, 300);
        r = check_original_safe(wide.data(), wide.size());
        CHECK(r.issues.size() == 1u && r.has(Code::GridTooLarge) && r.issues[0].severity == Severity::Fatal);
        const std::vector<uint8_t> empty = flat_map(1, 0, 5, 0, 0);
        r = check_original_safe(empty.data(), empty.size());
        CHECK(r.issues.size() == 1u && r.has(Code::GridEmpty));
        const std::vector<uint8_t> names = flat_map(1345, 1, 1, 1, 1);
        r = check_original_safe(names.data(), names.size());
        CHECK(r.issues.size() == 1u && r.has(Code::DictionaryTooLarge));
    });

    run_case("S3.2 What A File Left Out Is A Warning, Or Fatal In Block 4: Block 4 Cut Short, The Egg Word Missing, A Block 2 That Ends At A Pile Without Stages; A Whole File Is The Level's Profile", [&] {
        CHECK(located);
        CHECK(check_original_safe(tiny_file.data(), tiny_file.size()).issues.empty());
        // TINY has no waypoint: a file cut before its egg word, and one cut inside block 4 (a count of 1 and half a record)
        OriginalSafeReport r = check_original_safe(tiny_file.data(), tiny_file.size() - 2);
        CHECK(finds(r, Code::EggWordMissing, Severity::Warning, "the file ends before it says how many eggs each team starts with: the original leaves that number to chance"));
        std::vector<uint8_t> m(tiny_file.begin(), tiny_file.begin() + static_cast<std::ptrdiff_t>(t.b4_count));
        put16(m, 1);
        put16(m, 3);
        r = check_original_safe(m.data(), m.size());
        CHECK(finds(r, Code::FileCutInBlock4, Severity::Fatal,
                    "the file ends inside its list of flower droppers: the original keeps the complete records it read, goes on to read the places that were never filled, "
                    "and leaves the number of eggs each team starts with to chance"));
        CHECK(!r.works_in_original_game());
        CHECK(!r.has(Code::EggWordMissing));
        m = tiny_file;
        wr16(m, t.food[2] + 8, 0);
        r = check_original_safe(m.data(), m.size());
        CHECK(finds(r, Code::FoodBlockEndedEarly, Severity::Warning,
                    "the list of food piles ends at pile 3 of 14, which has no stages: the original keeps the 2 piles before it and reads the rest of the map from the bytes behind it"));
        // the same level from the writer has none of them: the rewrite is whole
        LevelData read;
        CHECK(read.load_from_memory(m.data(), m.size()));
        const LevelWriteResult w = write_level(read);
        CHECK(w.ok && !check_original_safe(w.bytes.data(), w.bytes.size()).has(Code::FoodBlockEndedEarly));
    });

    run_case("S3.3 A Limit Of The Place Is Not A Rule Of The Original: TINY 131 Columns Wide Works In The Original Game, Does Not Fit A New Map, And Says So Apart", [] {
        LevelData broad = tiny();
        broad.width.val = 131;
        broad.layer1_terrain.assign(31u * 131u, tiny().layer1_terrain[0]);
        broad.layer2_interactive.assign(31u * 131u, MapCell{LVL_EMPTY_TILE, 0, 0});
        for (uint32_t row = 0; row < 31; ++row) {
            for (uint32_t column = 0; column < 31; ++column) {
                broad.layer1_terrain[row * 131 + column] = tiny().layer1_terrain[row * 31 + column];
                broad.layer2_interactive[row * 131 + column] = tiny().layer2_interactive[row * 31 + column];
            }
        }
        const LevelWriteResult file = write_level(broad);
        CHECK(file.ok);
        if (!file.ok) return;
        OriginalSafeReport r = check_original_safe(file.bytes.data(), file.bytes.size());
        CHECK(r.works_in_original_game() && r.fits_the_place() && r.issues.empty() && r.judged);
        r = check_original_safe(file.bytes.data(), file.bytes.size(), LevelLimits::for_new_maps());
        CHECK(r.works_in_original_game() && !r.fits_the_place() && r.judged);
        CHECK(r.issues.size() == 1u && r.issues[0].code == Code::GridTooLarge && r.issues[0].place_limit && r.issues[0].severity == Severity::Fatal);
        CHECK(r.issues.size() == 1u && r.issues[0].message.find("the grid has 31 rows x 131 columns, more than the 90 x 130 allowed here") == 0);
        CHECK(r.describe().find("limit grid_too_large: ") == 0);
        // the rules of the original still apply to it: no Blue hill, and it does not work
        LevelData none = broad;
        clear_hill(none, kTinyHills[2]);
        const LevelWriteResult none_file = write_level(none);
        CHECK(none_file.ok);
        if (!none_file.ok) return;
        r = check_original_safe(none_file.bytes.data(), none_file.bytes.size(), LevelLimits::for_new_maps());
        CHECK(!r.works_in_original_game() && !r.fits_the_place() && r.has(Code::HillMissing) && r.has(Code::GridTooLarge) && r.issues.size() == 2u);
        CHECK(!r.first(Code::HillMissing)->place_limit && r.first(Code::GridTooLarge)->place_limit);
        // a grid beyond the original's own 256 is the original's rule, whatever the place allows
        const std::vector<uint8_t> wide = flat_map(1, 1, 300, 1, 300);
        r = check_original_safe(wide.data(), wide.size(), LevelLimits::for_new_maps());
        CHECK(r.issues.size() == 1u && r.issues[0].code == Code::GridTooLarge && !r.issues[0].place_limit && !r.works_in_original_game() && r.fits_the_place());
        // more cells than a new map has (10,000) with each side within 130: the cell limit, a limit of the place
        LevelData cells = tiny();
        cells.width.val = 112;
        cells.height.val = 90;
        cells.layer1_terrain.assign(90u * 112u, tiny().layer1_terrain[0]);
        cells.layer2_interactive.assign(90u * 112u, MapCell{LVL_EMPTY_TILE, 0, 0});
        const LevelWriteResult cells_file = write_level(cells);
        CHECK(cells_file.ok);
        if (cells_file.ok) {
            r = check_original_safe(cells_file.bytes.data(), cells_file.bytes.size(), LevelLimits::for_new_maps());
            CHECK(r.has(Code::GridTooLarge) && r.first(Code::GridTooLarge)->place_limit && r.first(Code::GridTooLarge)->message.find("10080 cells, more than the 10000 allowed here") != std::string::npos);
        }
        // a dictionary of more names than the place allows (not more than 1,344)
        r = check_original_safe(tiny_bytes().data(), tiny_bytes().size(), LevelLimits{256, 256, 600, std::numeric_limits<size_t>::max(), 65536, std::numeric_limits<size_t>::max()});
        CHECK(r.has(Code::DictionaryTooLarge) && r.first(Code::DictionaryTooLarge)->place_limit && !r.fits_the_place() && r.works_in_original_game());
        // a file that is not read at all: nothing is known of the map
        r = check_original_safe(tiny_bytes().data(), tiny_bytes().size(), LevelLimits{256, 256, 1344, 1000});
        CHECK(!r.judged && !r.works_in_original_game() && !r.fits_the_place() && r.issues.size() == 1u && r.issues[0].place_limit);
        CHECK(r.describe().find("limit file_too_large: ") == 0);
    });

    run_case("S3.4 A File The Reader Refuses Keeps What Its Reading Noted: 300 Columns With The Egg Word Missing, Cut In Block 4, Or A Mode Other Than 1", [] {
        const std::vector<uint8_t> wide = flat_map(1, 1, 300, 1, 300);
        OriginalSafeReport r = check_original_safe(wide.data(), wide.size());
        CHECK(r.issues.size() == 1u && r.has(Code::GridTooLarge) && !r.issues[0].place_limit);
        std::vector<uint8_t> m(wide.begin(), wide.end() - 2);
        r = check_original_safe(m.data(), m.size());
        CHECK(r.issues.size() == 2u && r.has(Code::GridTooLarge) && r.has(Code::EggWordMissing) && r.first(Code::EggWordMissing)->severity == Severity::Warning);
        m.assign(wide.begin(), wide.end() - 4);                      // block 4 announces one waypoint and holds half a record
        put16(m, 1);
        put16(m, 3);
        r = check_original_safe(m.data(), m.size());
        CHECK(r.issues.size() == 2u && r.has(Code::GridTooLarge) && r.has(Code::FileCutInBlock4) && !r.has(Code::EggWordMissing));
        CHECK(r.first(Code::FileCutInBlock4) != nullptr && r.first(Code::FileCutInBlock4)->severity == Severity::Fatal);
        m = wide;
        wr32(m, 4, 4631);
        r = check_original_safe(m.data(), m.size());
        CHECK(r.issues.size() == 2u && r.has(Code::GridTooLarge) && r.has(Code::ModeNot1) && r.first(Code::ModeNot1)->severity == Severity::Info);
        CHECK(r.issues[0].code == Code::GridTooLarge && r.issues[1].code == Code::ModeNot1);
    });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// S4 Stricter than the remake
// ------------------------------------------------------------------------------------------------------------------------------------------------------

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }
};

// The remake plays the file for all four teams without a Fatal or a Warning finding
bool remake_clean(const uint8_t* data, size_t size) {
    const LevelValidation v = LVLParser::check_memory(data, size, 0x0F);
    if (!v.playable) return false;
    for (const LevelProblem& p : v.problems) {
        if (p.severity != LevelProblemSeverity::Info) return false;
    }
    return true;
}

void s4_stricter() {
    run_case("S4.1 Stricter Than The Remake: Of 3,000 Files With Bytes Changed At Random (Fixed Seeds), Every One That Works In The Original Is Clean In The Remake", [] {
        int works = 0;
        int refused = 0;
        for (const char* name : kOriginals) {
            const std::vector<uint8_t> d = read_map(maps_dir() + "/" + name);
            Lcg r(0x5AFEu ^ static_cast<uint32_t>(d.size()));
            for (int i = 0; i < 500; ++i) {
                std::vector<uint8_t> m = d;
                const uint32_t flips = 1 + r.below(8);
                for (uint32_t k = 0; k < flips; ++k) m[r.below(static_cast<uint32_t>(m.size()))] = static_cast<uint8_t>(r.next());
                const OriginalSafeReport report = check_original_safe(m.data(), m.size());
                const bool clean = remake_clean(m.data(), m.size());
                if (report.works_in_original_game()) {
                    ++works;
                    CHECK_MSG(clean, std::string(name) + " flip " + std::to_string(i) + ": the profile accepts what the remake does not");
                } else {
                    ++refused;
                }
                for (const OriginalSafeIssue& issue : report.issues) CHECK_MSG(!issue.message.empty(), std::string(name) + " flip " + std::to_string(i));
            }
        }
        CHECK_MSG(works > 100 && refused > 100, std::to_string(works) + " work, " + std::to_string(refused) + " do not");
    });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// S5 The library
// ------------------------------------------------------------------------------------------------------------------------------------------------------

std::vector<std::string> listed_community_maps(const std::string& folder) {
    std::ifstream in(folder + "/maps.json");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> names;
    const std::string key = "\"file\": \"";
    for (size_t pos = text.find(key); pos != std::string::npos; pos = text.find(key, pos)) {
        pos += key.size();
        const size_t end = text.find('"', pos);
        if (end == std::string::npos) break;
        names.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return names;
}

// The results of the library (docs/MAP_FORMAT.md, "The original-safe profile"): 485 of the 540 maps work in the original game; per code, how many maps have it
constexpr int kLibraryWorks = 485;
constexpr int kLibraryWorksAsExtended = 535;      // the same 540 maps as Extended maps, judged by the remake profile (S6.8)
const std::map<std::string, int> kLibraryCodes = {
    {"description_too_long", 8}, {"egg_word_missing", 1}, {"file_cut_in_block_4", 11}, {"food_pile_not_shown", 34}, {"food_pile_outside_map", 3},   // the 55 that do not
    {"hill_without_entrance", 2},
    {"hill_not_whole", 36}, {"mode_not_1", 14}, {"object_outside_map", 36}, {"tile_not_drawn", 93},                                                   // notes
};

void s5_library() {
    run_case("S5.1 The Library (The Six Originals And The 534 Players' Maps): How Many Work In The Original Game, And Why The Others Do Not (docs/MAP_FORMAT.md)", [] {
        std::vector<std::pair<std::string, std::string>> maps;
        for (const char* name : kOriginals) maps.push_back({name, maps_dir() + "/" + name});
        const std::string folder = std::string(ANTS_SOURCE_DIR) + "/Community-Maps";
        for (const std::string& name : listed_community_maps(folder)) maps.push_back({name, folder + "/" + name});
        CHECK(maps.size() == 540u);
        int works = 0;
        int originals_work = 0;
        std::map<std::string, int> maps_with;       // per code, the maps that have it
        std::vector<std::string> not_working;
        std::vector<std::string> rewrite_works;     // of the maps that do not work, those whose rewrite does (the writer completes a file cut short)
        for (const auto& m : maps) {
            const std::vector<uint8_t> bytes = read_map(m.second);
            const OriginalSafeReport r = check_original_safe(bytes.data(), bytes.size());
            std::set<std::string> codes;
            for (const OriginalSafeIssue& i : r.issues) codes.insert(to_string(i.code));
            for (const std::string& c : codes) ++maps_with[c];
            if (r.works_in_original_game()) {
                ++works;
                originals_work += (&m - maps.data()) < 6 ? 1 : 0;
                CHECK_MSG(remake_clean(bytes.data(), bytes.size()), m.first + ": the profile accepts what the remake does not");
            } else {
                std::string why;
                for (const OriginalSafeIssue& i : r.issues) {
                    if (i.severity != Severity::Info) why += std::string(why.empty() ? "" : ", ") + to_string(i.code);
                }
                not_working.push_back(m.first + ": " + why);
                LevelData level;
                if (level.load_from_memory(bytes.data(), bytes.size())) {
                    const LevelWriteResult w = write_level(level);
                    if (w.ok && check_original_safe(w.bytes.data(), w.bytes.size()).works_in_original_game()) rewrite_works.push_back(m.first);
                }
            }
        }
        std::cout << "\n    " << works << " of " << maps.size() << " maps work in the original game; per code, the maps that have it:";
        for (const auto& c : maps_with) std::cout << " " << c.first << " " << c.second << ";";
        std::cout << "\n";
        for (const std::string& n : not_working) std::cout << "      " << n << "\n";
        // the numbers of docs/MAP_FORMAT.md ("The original-safe profile"): a change of the library or of the profile that moves them updates the document with them
        CHECK(originals_work == 6);
        CHECK_MSG(works == kLibraryWorks, std::to_string(works));
        const std::map<std::string, int> expected = kLibraryCodes;
        CHECK_MSG(maps_with == expected, "the counts per code moved");
        // the 12 files that are cut short work once they are rewritten, but Conquer, whose description is 30 bytes long
        const std::vector<std::string> cut = {"1v1TREASURE.LVL", "AnTsCaStLe.lvl", "AntsTournament.lvl", "Ants_Town.lvl", "ArCaDeStYLe.lvl", "Arena101.lvl",
                                              "BetterArcadeStyle.lvl", "Bunker.lvl", "Food Island.lvl", "Food Rules The Ants!!!.lvl", "NW.LVL"};
        CHECK_MSG(rewrite_works == cut, std::to_string(rewrite_works.size()) + " work once rewritten");
    });
}

// The whole library's findings must not move: FNV-1a 64 of every map's report (check_original_safe(...).describe(), once with the game's limits and once with the limits
// of a new map), in library order. The values were taken on the profile as it was before the checker learned the two kinds and the 90-row rule (a map of the library has at
// most 81 rows), so a change that alters a sentence, a severity or the order of a finding for a Classic map within the new-map limits fails here.
constexpr uint64_t kGoldenGameLimits = 217177084291266274ull;
constexpr uint64_t kGoldenNewMapLimits = 217177084291266274ull;     // (the same: every map of the library is within the limits of a new map)

uint64_t fnv1a(uint64_t h, const std::string& text) {
    for (const char c : text) h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
    return (h ^ 0xFFu) * 1099511628211ull;      // (a separator: the end of one report)
}

void s5_golden() {
    run_case("S5.2 The Findings Of The 540 Library Maps Are Byte For Byte What They Were (Game Limits And New-Map Limits)", [] {
        std::vector<std::string> paths;
        for (const char* name : kOriginals) paths.push_back(maps_dir() + "/" + name);
        const std::string folder = std::string(ANTS_SOURCE_DIR) + "/Community-Maps";
        for (const std::string& name : listed_community_maps(folder)) paths.push_back(folder + "/" + name);
        uint64_t game = 14695981039346656037ull;
        uint64_t fresh = 14695981039346656037ull;
        for (const std::string& path : paths) {
            const std::vector<uint8_t> bytes = read_map(path);
            game = fnv1a(game, check_original_safe(bytes.data(), bytes.size()).describe());
            fresh = fnv1a(fresh, check_original_safe(bytes.data(), bytes.size(), LevelLimits::for_new_maps()).describe());
        }
        std::cout << "\n    golden game " << game << " new-map " << fresh << "\n";
        CHECK(paths.size() == 540u);
        CHECK_MSG(game == kGoldenGameLimits, std::to_string(game));
        CHECK_MSG(fresh == kGoldenNewMapLimits, std::to_string(fresh));
    });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// S6 The checker per kind (check_map) and the 90 rows of a Classic map
// ------------------------------------------------------------------------------------------------------------------------------------------------------

// `base` with `rows` rows: the rows below are plain ground (the ground of the first cell) with nothing on them
LevelData with_rows(const LevelData& base, uint32_t rows) {
    LevelData l = base;
    l.height.val = rows;
    l.layer1_terrain.resize(static_cast<size_t>(rows) * l.width, base.layer1_terrain[0]);
    l.layer2_interactive.resize(static_cast<size_t>(rows) * l.width, MapCell{LVL_EMPTY_TILE, 0, 0});
    return l;
}

const std::string kWallWarning = "ants cannot cross the 91st row of a taller map, in the original game too, so it plays otherwise than the map shows (a Classic map made now has at most 90 rows; "
                                 "an Extended map has no such wall)";

// TINY as an Extended map
LevelData tiny_extended() {
    const LevelConversion up = convert_to_extended(tiny());
    CHECK(up.ok);
    return up.level;
}

void s6_wall() {
    run_case("S6.1 Rows Beyond The Wall: 90 Rows Work In The Original Game, 91 Are A Warning (rows_beyond_wall) That Reads The Level All The Same; 257 Are Fatal As Before", [] {
        LevelData l = with_rows(tiny(), 90);
        OriginalSafeReport r = check_original_safe(l);
        CHECK(r.works_in_original_game() && !r.has(Code::RowsBeyondWall) && r.issues.empty());
        l = with_rows(tiny(), 91);
        r = check_original_safe(l);
        CHECK(finds(r, Code::RowsBeyondWall, Severity::Warning, "the map has 91 rows: " + kWallWarning, 90, -1, -1));
        CHECK(!r.works_in_original_game() && r.fits_the_place() && r.issues.size() == 1u && r.first(Code::RowsBeyondWall)->count == 1u);
        l = with_rows(tiny(), 256);
        r = check_original_safe(l);
        CHECK(finds(r, Code::RowsBeyondWall, Severity::Warning, "the map has 256 rows: " + kWallWarning, 90, -1, -1) && r.first(Code::RowsBeyondWall)->count == 166u);
        r = check_original_safe(with_rows(tiny(), 257));
        CHECK(r.has(Code::GridTooLarge) && !r.has(Code::RowsBeyondWall));      // (beyond the 256 of the format the grid is fatal, and that is the one finding)
        CHECK(std::string(to_string(Code::RowsBeyondWall)) == "rows_beyond_wall");
        // the wall is about rows: 256 columns of 90 rows are none
        LevelData wide = tiny();
        wide.width.val = 256;
        wide.layer1_terrain.assign(31u * 256u, tiny().layer1_terrain[0]);
        wide.layer2_interactive.assign(31u * 256u, MapCell{LVL_EMPTY_TILE, 0, 0});
        CHECK(!check_original_safe(wide).has(Code::RowsBeyondWall));
    });

    run_case("S6.2 The Wall In A File: The Game's Limits Read 91 Rows And Warn; The Limits Of A New Map Refuse Them With The Reason (A Limit Of The Place) And Still Warn; 90 Rows Fit Both", [] {
        const LevelWriteResult tall = write_level(with_rows(tiny(), 91));
        const LevelWriteResult ok90 = write_level(with_rows(tiny(), 90));
        CHECK(tall.ok && ok90.ok);
        if (!tall.ok || !ok90.ok) return;
        LevelData level;
        CHECK(level.load_from_memory(tall.bytes.data(), tall.bytes.size()));                // the reader does not fail on the wall
        OriginalSafeReport r = check_original_safe(tall.bytes.data(), tall.bytes.size());
        CHECK(r.has(Code::RowsBeyondWall) && !r.works_in_original_game() && r.fits_the_place() && r.issues.size() == 1u);
        r = check_original_safe(tall.bytes.data(), tall.bytes.size(), LevelLimits::for_new_maps());
        CHECK(r.has(Code::RowsBeyondWall) && r.has(Code::GridTooLarge) && r.issues.size() == 2u && !r.fits_the_place() && !r.works_in_original_game());
        CHECK(r.first(Code::GridTooLarge)->place_limit && !r.first(Code::RowsBeyondWall)->place_limit);
        CHECK(r.first(Code::GridTooLarge)->message == rows_beyond_wall_text(91, 31));
        CHECK(r.first(Code::GridTooLarge)->message.find("a Classic map made now has at most 90 rows") != std::string::npos);
        r = check_original_safe(ok90.bytes.data(), ok90.bytes.size(), LevelLimits::for_new_maps());
        CHECK(r.works_in_original_game() && r.fits_the_place() && r.issues.empty());
        // the limits of the game and of the Extended place read a Classic map of 91 rows; the Extended place does not refuse it either
        CHECK(check_map(tall.bytes.data(), tall.bytes.size(), LevelLimits::for_extended_maps()).has(Code::RowsBeyondWall));
    });
}

void s6_kinds() {
    run_case("S6.3 A Classic Map: The Kind, The Headline Of The Kind, No Badge; Its Findings Are Those Of The Original-Safe Profile", [] {
        MapCheckReport m = check_map(tiny());
        CHECK(m.kind == MapKind::Classic && m.works && m.fits_the_place && m.headline == "Works in the original game" && m.badge.empty() && m.report.judged);
        CHECK(std::string(to_string(MapKind::Classic)) == "classic" && std::string(to_string(MapKind::Extended)) == "extended");
        CHECK(kind_of_version(8) == MapKind::Classic && kind_of_version(9) == MapKind::Extended && kind_of_version(7) == MapKind::Classic && kind_of_version(10) == MapKind::Classic);
        CHECK(m.report.issues.empty() && m.describe().empty());
        LevelData none = tiny();
        clear_hill(none, kTinyHills[2]);
        m = check_map(none);
        CHECK(m.kind == MapKind::Classic && !m.works && m.headline == "Does not work in the original game" && m.badge.empty());
        CHECK(m.report.describe() == check_original_safe(none).describe());
        // 91 rows
        m = check_map(with_rows(tiny(), 91));
        CHECK(!m.works && m.headline == "Does not work in the original game" && m.has(Code::RowsBeyondWall));
        // the sentence of a grid beyond 256 is the kind's
        const std::string too_big = "too big for a Classic map (256 at most a side); make it an Extended map?";
        m = check_map(with_rows(tiny(), 257));
        CHECK(!m.works && m.has(Code::GridTooLarge) && m.report.first(Code::GridTooLarge)->message == too_big && !m.report.first(Code::GridTooLarge)->place_limit);
        CHECK(std::count_if(m.report.issues.begin(), m.report.issues.end(), [](const OriginalSafeIssue& i) { return i.code == Code::GridTooLarge; }) == 1);     // (one finding, not a second one as a limit of the place)
        const std::vector<uint8_t> wide = flat_map(1, 1, 300, 1, 300);
        m = check_map(wide.data(), wide.size());
        CHECK(!m.works && m.report.first(Code::GridTooLarge) != nullptr && m.report.first(Code::GridTooLarge)->message == too_big);
        // check_original_safe keeps its own sentence for it
        CHECK(check_original_safe(with_rows(tiny(), 257)).first(Code::GridTooLarge)->message.find("at most 256 a side fit") != std::string::npos);
        // a limit of the place: the headline is about the map, the place has its own flag
        const LevelWriteResult tall = write_level(with_rows(tiny(), 91));
        m = check_map(tall.bytes.data(), tall.bytes.size(), LevelLimits::for_new_maps());
        CHECK(!m.fits_the_place && m.has(Code::GridTooLarge) && m.has(Code::RowsBeyondWall));
        const LevelWriteResult ok90 = write_level(with_rows(tiny(), 90));
        m = check_map(ok90.bytes.data(), ok90.bytes.size(), LevelLimits::for_new_maps());
        CHECK(m.works && m.fits_the_place && m.headline == "Works in the original game");
        // a level in memory against the limits of a new map: 91 rows is the wall, as a limit of the place, and the warning besides
        m = check_map(with_rows(tiny(), 91), LevelLimits::for_new_maps());
        CHECK(!m.fits_the_place && m.report.first(Code::GridTooLarge) != nullptr && m.report.first(Code::GridTooLarge)->place_limit && m.has(Code::RowsBeyondWall));
        CHECK(check_map(with_rows(tiny(), 90), LevelLimits::for_new_maps()).fits_the_place);
        // a file too large is not judged
        m = check_map(tiny_bytes().data(), tiny_bytes().size(), LevelLimits{256, 256, 1344, 1000});
        CHECK(!m.report.judged && !m.works && m.headline == "Not checked" && !m.fits_the_place);
    });

    run_case("S6.4 An Extended Map That Is Whole: Works In The New Version, With The Badge \"New Version Only\"; The Original Cannot Load It, A Note And No Fault", [] {
        const LevelData ext = tiny_extended();
        const MapCheckReport m = check_map(ext, LevelLimits::for_extended_maps());
        CHECK(m.kind == MapKind::Extended && m.works && m.fits_the_place && m.headline == "Works in the new version" && m.badge == "New version only" && m.report.judged);
        CHECK(m.report.issues.size() == 1u && m.report.issues[0].code == Code::NotForOriginal && m.report.issues[0].severity == Severity::Info);
        CHECK(m.report.not_for_original() && !m.report.works_in_original_game());
        CHECK(ext.validate(0x0F, LevelLimits::for_extended_maps()).playable);
        // the same map as a file
        const LevelWriteResult w = write_level(ext, LevelLimits::for_extended_maps());
        CHECK_MSG(w.ok, w.error);
        const MapCheckReport f = check_map(w.bytes.data(), w.bytes.size(), LevelLimits::for_extended_maps());
        CHECK(f.kind == MapKind::Extended && f.works && f.headline == m.headline && f.badge == m.badge && f.report.describe() == m.report.describe());
        // a place that reads Classic maps only does not read it
        for (const LevelLimits& classic_only : {LevelLimits{}, LevelLimits::for_new_maps()}) {
            const MapCheckReport g = check_map(w.bytes.data(), w.bytes.size(), classic_only);
            CHECK(g.kind == MapKind::Extended && !g.works && !g.report.judged && g.headline == "Not checked" && g.badge == "New version only" && g.has(Code::ExtendedNotAcceptedHere));
            CHECK(g.report.issues.size() == 1u && g.report.issues[0].place_limit && g.report.issues[0].severity == Severity::Fatal && !g.fits_the_place);
            const MapCheckReport h = check_map(ext, classic_only);
            CHECK(!h.works && h.has(Code::ExtendedNotAcceptedHere) && !h.report.judged);
        }
        // check_original_safe of the same map is as it was: the one note
        CHECK(check_original_safe(ext).issues.size() == 1u && check_original_safe(w.bytes.data(), w.bytes.size()).not_for_original());
        // more than 90 rows is no finding for an Extended map: there is no wall
        for (const uint32_t rows : {91u, 120u, 400u}) {
            const MapCheckReport tall = check_map(with_rows(ext, rows), LevelLimits::for_extended_maps());
            CHECK_MSG(tall.works && !tall.has(Code::RowsBeyondWall) && tall.report.issues.size() == 1u, std::to_string(rows));
        }
        // the Classic kind of the same shape does warn
        CHECK(check_map(with_rows(tiny(), 120)).has(Code::RowsBeyondWall));
    });

    run_case("S6.5 The Remake Profile: The Rules Both Games Share Say The Same Words For Both Kinds (\"Blue Has No Hill\"); The Findings Of An Extended Map", [] {
        const LevelData classic = tiny();
        const LevelData ext = tiny_extended();
        const LevelLimits limits = LevelLimits::for_extended_maps();
        // a rule broken in both kinds: the same code, severity and sentence
        auto same_words = [&](const char* what, Code code, Severity severity, const std::function<void(LevelData&)>& break_it) {
            LevelData c = classic;
            LevelData e = ext;
            break_it(c);
            break_it(e);
            const OriginalSafeReport cr = check_original_safe(c);
            const MapCheckReport er = check_map(e, limits);
            const OriginalSafeIssue* ci = cr.first(code);
            const OriginalSafeIssue* ei = er.report.first(code);
            CHECK_MSG(ci != nullptr && ei != nullptr, what);
            if (ci == nullptr || ei == nullptr) return;
            CHECK_MSG(ci->message == ei->message && ci->severity == severity && ei->severity == severity && ci->row == ei->row && ci->column == ei->column && ci->team == ei->team &&
                          ci->count == ei->count,
                      std::string(what) + ": " + ci->message + " / " + ei->message);
            CHECK_MSG(!er.works && er.headline == "Does not work in the new version" && er.badge == "New version only", what);
            CHECK_MSG(er.report.issues.back().code == Code::NotForOriginal || er.report.issues.back().severity == Severity::Info, what);
        };
        same_words("a team without a hill", Code::HillMissing, Severity::Warning, [](LevelData& l) { clear_hill(l, kTinyHills[2]); });
        same_words("a hill without an entrance", Code::HillWithoutEntrance, Severity::Warning, [](LevelData& l) { l.layer2_interactive[at(l, kTinyHills[1].row, kTinyHills[1].column)].flags = 0; });
        same_words("a team with two hills", Code::HillsSeveral, Severity::Warning, [](LevelData& l) { put_hill(l, 0, 15, 15); });
        same_words("a team without starting ants", Code::StartingAntsMissing, Severity::Warning, [](LevelData& l) {
            std::vector<AnthillSpawn> kept;
            for (const AnthillSpawn& sp : l.anthill_spawns) {
                if (sp.team_id != 1) kept.push_back(sp);
            }
            l.anthill_spawns.assign(kept.begin(), kept.end());
        });
        same_words("a tile of the ground beyond the dictionary", Code::TileOutsideDictionary, Severity::Fatal, [](LevelData& l) { l.layer1_terrain[at(l, 3, 3)].tile_index = 1300; });
        same_words("a tile of an object beyond the dictionary", Code::TileOutsideDictionary, Severity::Fatal, [](LevelData& l) { l.layer2_interactive[at(l, 12, 12)] = MapCell{1301, 0, 0}; });
        same_words("a cell without ground", Code::GroundMissing, Severity::Fatal, [](LevelData& l) { l.layer1_terrain[at(l, 3, 3)].tile_index = LVL_EMPTY_TILE; });
        same_words("a start marker outside the map", Code::StartMarkerOutsideMap, Severity::Fatal, [](LevelData& l) {
            for (AnthillSpawn& sp : l.anthill_spawns) {
                if (sp.team_id == 0) { sp.y = 40; break; }
            }
        });
        same_words("a food pile outside the map", Code::FoodPileOutsideMap, Severity::Fatal, [](LevelData& l) {
            FoodSchedule pile;
            pile.y = 3;
            pile.x = 60;
            pile.variants.push_back(FoodItemVariant{10, 253});
            l.food_schedules.push_back(pile);
        });
        same_words("a food pile without stages", Code::FoodPileWithoutStages, Severity::Fatal, [](LevelData& l) {
            FoodSchedule pile;
            pile.y = 4;
            pile.x = 4;
            l.food_schedules.push_back(pile);
        });
        // a wrong dictionary and an empty grid
        {
            LevelData e = ext;
            e.tile_dictionary.assign(1345, ".");
            CHECK(check_map(e, limits).has(Code::DictionaryTooLarge));
            e = ext;
            e.height.val = 0;
            e.layer1_terrain.clear();
            e.layer2_interactive.clear();
            const MapCheckReport g = check_map(e, limits);
            CHECK(g.has(Code::GridEmpty) && !g.works);
            e = ext;
            e.layer1_terrain.pop_back();
            CHECK(check_map(e, limits).has(Code::LayersNotGrid));
            e = ext;
            e.description = std::string(256, 'a');
            const MapCheckReport d = check_map(e, limits);
            CHECK(d.has(Code::DescriptionTooLong) && !d.works && d.report.first(Code::DescriptionTooLong)->severity == Severity::Fatal);
            e.description = std::string(255, 'a');
            CHECK(check_map(e, limits).works);
        }
        // the description: 30 bytes are a warning for a Classic map (the original reads 30 bytes and needs a zero), and fine for an Extended map
        {
            LevelData c = classic;
            LevelData e = ext;
            c.description = e.description = std::string(30, 'a');
            CHECK(check_map(c).has(Code::DescriptionTooLong) && !check_map(c).works);
            const MapCheckReport m = check_map(e, limits);
            CHECK(m.works && !m.has(Code::DescriptionTooLong));
        }
        // what is the original's own is not judged in the remake profile: the mode, the pictures it does not load, a hill that is not whole
        {
            LevelData e = ext;
            e.game_mode = 4631;
            const uint16_t ground = e.layer1_terrain[0].tile_index;
            e.tile_dictionary[ground] = ".";
            LevelData c = classic;
            c.tile_dictionary[ground] = ".";
            CHECK(check_original_safe(c).has(Code::TileNotDrawn) || tile_always_loaded(ground));
            CHECK(check_map(e, limits).works && !check_map(e, limits).has(Code::ModeNot1) && !check_map(e, limits).has(Code::TileNotDrawn));
        }
        // a food pile whose stage tile the dictionary names "." is not placed by the original (a warning there), and not judged in the remake profile
        {
            LevelData c = classic;
            LevelData e = ext;
            FoodSchedule pile;
            pile.y = 3;
            pile.x = 3;
            pile.variants.push_back(FoodItemVariant{1, 253});
            c.food_schedules.push_back(pile);
            e.food_schedules.push_back(pile);
            c.tile_dictionary[1] = ".";
            e.tile_dictionary[1] = ".";
            CHECK(check_original_safe(c).has(Code::FoodPileNotShown) && !check_map(e, limits).has(Code::FoodPileNotShown) && check_map(e, limits).works);
        }
        // plants and waypoints outside the grid, and an object whose anchor is outside the grid: notes
        {
            LevelData e = ext;
            AnthillSpawn plant;
            plant.tile_id = 60;
            plant.y = 50;
            plant.x = 2;
            plant.team_id = 255;
            e.anthill_spawns.push_back(plant);
            MapCheckReport m = check_map(e, limits);
            const OriginalSafeIssue* out = m.report.first(Code::ObjectOutsideMap);
            CHECK(m.works && out != nullptr && out->severity == Severity::Info && out->message == "one plant stands outside the map (row 50, column 2): it is ignored");
            LevelData c = classic;
            c.anthill_spawns.push_back(plant);
            CHECK(check_original_safe(c).first(Code::ObjectOutsideMap)->message == "one plant stands outside the map (row 50, column 2): the original ignores it");     // (the Classic words)
            // the anchor of the object at (10, 10) is row 200: outside the 31 x 31 grid (the sum is taken modulo 256 up to 256 x 256)
            e = ext;
            // (the last row and column of the grid are inside: row offset 20 is row 30, column offset 20 is column 30; one more is outside)
            e.layer2_interactive[at(e, 10, 10)] = MapCell{60, 0, static_cast<uint16_t>(20 | (20 << 8))};
            CHECK(!check_map(e, limits).has(Code::ObjectAnchorOutsideMap));
            e.layer2_interactive[at(e, 10, 10)] = MapCell{60, 0, static_cast<uint16_t>(21)};
            CHECK(check_map(e, limits).has(Code::ObjectAnchorOutsideMap));
            e.layer2_interactive[at(e, 10, 10)] = MapCell{60, 0, static_cast<uint16_t>(21 << 8)};
            CHECK(check_map(e, limits).has(Code::ObjectAnchorOutsideMap));
            e.layer2_interactive[at(e, 10, 10)] = MapCell{60, 0, static_cast<uint16_t>(0xFF - 10 + 1)};      // a negative offset: row -1... of the sum modulo 256, row 0
            CHECK(!check_map(e, limits).has(Code::ObjectAnchorOutsideMap));
            e.layer2_interactive[at(e, 10, 10)] = MapCell{60, 0, static_cast<uint16_t>(190)};
            int32_t anchor_row = 0;
            int32_t anchor_column = 0;
            CHECK(e.object_anchor(10, 10, anchor_row, anchor_column) && anchor_row == 200 && anchor_column == 10);
            m = check_map(e, limits);
            const OriginalSafeIssue* anchor = m.report.first(Code::ObjectAnchorOutsideMap);
            CHECK(m.works && anchor != nullptr && anchor->severity == Severity::Info && anchor->row == 10 && anchor->column == 10 && anchor->count == 1u);
            CHECK(anchor != nullptr && anchor->message == "one object has its anchor outside the map (row 10, column 10): the anchor is the object's first cell, and it is not on the map");
            CHECK(std::string(to_string(Code::ObjectAnchorOutsideMap)) == "object_anchor_outside_map");
            // (a Classic map is not judged by it: the original takes the byte as it is)
            c = classic;
            c.layer2_interactive[at(c, 10, 10)] = MapCell{60, 0, static_cast<uint16_t>(200)};
            CHECK(!check_map(c).has(Code::ObjectAnchorOutsideMap) && check_map(c).works);
        }
        // the order: what stops the map first, then the notes, the note that the original cannot load it among them
        {
            LevelData e = ext;
            clear_hill(e, kTinyHills[2]);
            e.game_mode = 4631;
            AnthillSpawn plant;
            plant.tile_id = 60;
            plant.y = 50;
            plant.x = 2;
            plant.team_id = 255;
            e.anthill_spawns.push_back(plant);
            const MapCheckReport m = check_map(e, limits);
            CHECK(m.report.issues.size() == 3u && m.report.issues[0].code == Code::HillMissing && m.report.issues[1].code == Code::NotForOriginal && m.report.issues[2].code == Code::ObjectOutsideMap);
        }
    });

    run_case("S6.6 Limits Of The Place For An Extended Map: A Grid Beyond The Place Is A Limit Of The Place (Judged All The Same); The Ceiling Of The Format Is Fatal", [] {
        const LevelData ext = tiny_extended();
        MapCheckReport m = check_map(with_rows(ext, 1025), LevelLimits::for_extended_maps());
        CHECK(m.works && !m.fits_the_place && m.report.first(Code::GridTooLarge) != nullptr && m.report.first(Code::GridTooLarge)->place_limit);
        m = check_map(with_rows(ext, 1024), LevelLimits::for_extended_maps());
        CHECK(m.works && m.fits_the_place);
        // as a file too: judged by the remake profile, the limit is a finding of its own
        LevelData tall = with_rows(ext, 1025);
        const LevelWriteResult w = write_level(tall, LevelLimits::format_ceiling());
        CHECK_MSG(w.ok, w.error);
        m = check_map(w.bytes.data(), w.bytes.size(), LevelLimits::for_extended_maps());
        CHECK(m.kind == MapKind::Extended && m.works && !m.fits_the_place && m.report.judged && m.report.first(Code::GridTooLarge)->place_limit);
        m = check_map(w.bytes.data(), w.bytes.size(), LevelLimits::format_ceiling());
        CHECK(m.works && m.fits_the_place);
        // beyond the ceiling of the format the reader refuses the file, and the grid is a fault of the map
        tall = with_rows(ext, 2049);
        m = check_map(tall, LevelLimits::format_ceiling());
        CHECK(!m.works && m.report.first(Code::GridTooLarge) != nullptr && !m.report.first(Code::GridTooLarge)->place_limit);
        // a file too large is not read
        m = check_map(w.bytes.data(), w.bytes.size(), LevelLimits{256, 256, 1344, 1000, 65536, 1000, true});
        CHECK(!m.report.judged && m.headline == "Not checked" && m.kind == MapKind::Extended && m.badge == "New version only" && m.has(Code::FileTooLarge));
    });

    run_case("S6.7 Files Of The Extended Kind: A Flag Bit, A Rules Word, A Cut File And Hostile Bytes Are Findings With Stable Codes, Never A Crash", [] {
        const LevelWriteResult w = write_level(tiny_extended(), LevelLimits::for_extended_maps());
        CHECK(w.ok);
        if (!w.ok) return;
        const LevelLimits limits = LevelLimits::for_extended_maps();
        std::vector<uint8_t> flags = w.bytes;
        flags[10] = 1;                                                       // the format flags: a bit this reader does not know
        MapCheckReport m = check_map(flags.data(), flags.size(), limits);
        CHECK(m.kind == MapKind::Extended && !m.works && m.has(Code::FormatFlagsUnknown) && m.report.first(Code::FormatFlagsUnknown)->severity == Severity::Fatal && m.has(Code::NotForOriginal));
        CHECK(m.headline == "Does not work in the new version" && m.badge == "New version only" && m.report.judged);
        std::vector<uint8_t> rules = w.bytes;
        rules[14] = 2;                                                       // the rules word: only 0 and 1 are known
        m = check_map(rules.data(), rules.size(), limits);
        CHECK(!m.works && m.has(Code::RulesOutOfRange) && m.report.first(Code::RulesOutOfRange)->severity == Severity::Fatal);
        // a cut at several places: never works, never throws, the kind is Extended
        for (size_t cut = 4; cut < w.bytes.size(); cut += 1 + w.bytes.size() / 97) {
            m = check_map(w.bytes.data(), cut, limits);
            CHECK_MSG(m.kind == MapKind::Extended && !m.works && !m.report.issues.empty(), std::to_string(cut));
        }
        CHECK(check_map(w.bytes.data(), 0, limits).kind == MapKind::Classic);            // (no bytes: no version word)
        CHECK(!check_map(w.bytes.data(), 0, limits).works && check_map(w.bytes.data(), 0, limits).has(Code::FileUnreadable));
        // a food pile without a stage in a file
        std::vector<uint8_t> description = w.bytes;
        description[16] = 0xFF;                                              // the description length: 255 + the rest of the header is misread, whatever it is, the answer is a finding
        description[17] = 0xFF;
        m = check_map(description.data(), description.size(), limits);
        CHECK(!m.works && !m.report.issues.empty());
        // bytes changed at random (fixed seeds): the answer is always a report whose "works" agrees with the playable question of the reader
        uint32_t seed = 0x9E3779B9u;
        auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        int works = 0;
        for (int round = 0; round < 400; ++round) {
            std::vector<uint8_t> b = w.bytes;
            for (int k = 0; k < 6; ++k) b[next() % b.size()] = static_cast<uint8_t>(next());
            const MapCheckReport r = check_map(b.data(), b.size(), limits);
            CHECK(!r.headline.empty());
            if (!r.works) continue;
            ++works;
            LevelData level;
            CHECK(level.load_from_memory(b.data(), b.size(), nullptr, limits) && level.validate(0x0F, limits).playable);
        }
        CHECK(works > 0);
    });

    run_case("S6.8 The Library, By Kind: The Counts Of The Classic Kind Are The Original-Safe Counts; Every Map That Works In The Original Game Works In The New Version As An Extended Map", [] {
        std::vector<std::string> paths;
        for (const char* name : kOriginals) paths.push_back(maps_dir() + "/" + name);
        const std::string folder = std::string(ANTS_SOURCE_DIR) + "/Community-Maps";
        for (const std::string& name : listed_community_maps(folder)) paths.push_back(folder + "/" + name);
        int classic_works = 0;
        int extended_works = 0;
        int walls = 0;
        size_t most_rows = 0;
        for (const std::string& path : paths) {
            const std::vector<uint8_t> bytes = read_map(path);
            const MapCheckReport c = check_map(bytes.data(), bytes.size(), LevelLimits::for_new_maps());
            const OriginalSafeReport o = check_original_safe(bytes.data(), bytes.size(), LevelLimits::for_new_maps());
            CHECK_MSG(c.kind == MapKind::Classic && c.works == o.works_in_original_game() && c.report.describe() == o.describe() && c.fits_the_place, path);
            CHECK_MSG(c.headline == (c.works ? "Works in the original game" : "Does not work in the original game") && c.badge.empty(), path);
            classic_works += c.works ? 1 : 0;
            walls += c.has(Code::RowsBeyondWall) ? 1 : 0;
            LevelData level;
            if (!level.load_from_memory(bytes.data(), bytes.size())) continue;
            most_rows = std::max<size_t>(most_rows, level.height);
            const LevelConversion up = convert_to_extended(level);
            CHECK_MSG(up.ok, path);
            if (!up.ok) continue;
            const MapCheckReport e = check_map(up.level, LevelLimits::for_extended_maps());
            CHECK_MSG(e.kind == MapKind::Extended && e.badge == "New version only" && e.has(Code::NotForOriginal) && !e.has(Code::RowsBeyondWall), path);
            extended_works += e.works ? 1 : 0;
            CHECK_MSG(!c.works || e.works, path + ": works in the original game, not in the new version");
            if (e.works) CHECK_MSG(up.level.validate(0x0F, LevelLimits::for_extended_maps()).playable, path);
        }
        std::cout << "\n    " << classic_works << " Classic maps work in the original game, " << extended_works << " work as Extended maps in the new version; the most rows: " << most_rows << "\n";
        CHECK(classic_works == kLibraryWorks && walls == 0 && most_rows <= 81u);
        CHECK_MSG(extended_works == kLibraryWorksAsExtended, std::to_string(extended_works));
    });
}

}  // namespace

int main() {
    std::cout << "=======================================================\n"
              << " The original-safe profile of the map checker\n"
              << "=======================================================\n";
    s1_rules();
    s2_report();
    s3_files();
    s4_stricter();
    s5_library();
    s5_golden();
    s6_wall();
    s6_kinds();
    std::cout << "\n" << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
