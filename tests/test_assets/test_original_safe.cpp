// The original-safe profile of the map checker (include/ants_assets/lvl_checker.hpp, docs/MAP_FORMAT.md "The original-safe profile"):
//   S1 every rule: a level that breaks it (the code, its severity, its sentence and its cell) and one that keeps it, at the boundary where there is one
//   S2 the report: "works in the original game" is no Fatal and no Warning, a note does not count; the codes' texts are stable and distinct; the order
//   S3 files: what the reader refuses, with its sentence; a file that loads is the profile of its level and of what its file left out
//   S4 stricter than the remake: a file that the profile calls original-safe has no Fatal and no Warning in the remake's own check (hostile files, fixed seeds)
//   S5 the library: the six original maps and the 534 players' maps, the numbers of docs/MAP_FORMAT.md
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

// TINY's hills (rows x columns 31 x 31): the entrance of each colour and its 4 x 4 footprint, one row and one column up and left of it
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

    run_case("S1.1 Version 8: Version 9 Is Fatal (The Original Says \"Wrong Version\" And Starts Without A Map)", [] {
        LevelData l = tiny();
        l.version = 9;
        const OriginalSafeReport r = check_original_safe(l);
        CHECK(finds(r, Code::VersionNot8, Severity::Fatal, "the map is version 9: the original plays version 8 only (it says \"wrong version\" and starts without a map)"));
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

    run_case("S1.3 A Description Of At Most 29 Characters: 30 Are A Warning (No Zero Byte Is Left To End The Text), 29 Are None", [] {
        LevelData l = tiny();
        l.description = std::string(30, 'a');
        CHECK(finds(check_original_safe(l), Code::DescriptionTooLong, Severity::Warning,
                    "the description has 30 characters: at most 29 fit, because the original reads 30 bytes and a text needs a zero byte at its end"));
        l.description = std::string(29, 'a');
        CHECK(!check_original_safe(l).has(Code::DescriptionTooLong));
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
            {[](LevelData& l, uint16_t t) { l.anthill_spawns.push_back(AnthillSpawn{t, 2, 3, 255}); }, "one block-1 record uses a tile beyond the dictionary of 670 names (row 2, column 3: tile 670): the original reads past its tables there", 2, 3},
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

    run_case("S1.7 Every Ground Cell Has A Ground Tile: The Empty Tile 0x7FFE On Layer 1 Is A Warning (The Original Reads Its Terrain Table Past Its End)", [] {
        LevelData l = tiny();
        l.layer1_terrain[at(l, 0, 3)].tile_index = LVL_EMPTY_TILE;
        l.layer1_terrain[at(l, 9, 9)].tile_index = LVL_EMPTY_TILE;
        CHECK(finds(check_original_safe(l), Code::GroundMissing, Severity::Warning,
                    "2 cells have no ground tile (layer-1 tile 32766; the first at row 0, column 3): the original reads its terrain table past its end there", 0, 3));
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

    run_case("S1.14 Each Team Has Starting Ants: A Team Without A Start Marker Is A Warning (\"Black Has No Starting Ants\"), One Marker Is Enough", [] {
        LevelData l = tiny();
        std::vector<AnthillSpawn> kept;
        bool one = false;
        for (const AnthillSpawn& sp : l.anthill_spawns) {
            if (sp.team_id != 3) kept.push_back(sp);
        }
        l.anthill_spawns.assign(kept.begin(), kept.end());
        CHECK(finds(check_original_safe(l), Code::StartingAntsMissing, Severity::Warning, "Black has no starting ants (no start marker, block-1 tile 152)", -1, -1, 3));
        for (const AnthillSpawn& sp : tiny().anthill_spawns) {
            if (sp.team_id == 3 && !one) {
                l.anthill_spawns.push_back(sp);
                one = true;
            }
        }
        CHECK(one && !check_original_safe(l).has(Code::StartingAntsMissing));
    });

    run_case("S1.15 A Hill That Is Not Whole Is A Note: A Cell Anchored Elsewhere, A Cell Of The Colour Outside Its 4 x 4 Hill; The Map Still Works", [] {
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

    run_case("S1.17 A Food Pile Whose Stage Tile The Original Does Not Load Shows No Pile From That Stage On (A Note); Two Such Piles Are Counted", [] {
        LevelData l = tiny();
        l.tile_dictionary[285] = ".";                                              // the one stage of pile 7, at row 17, column 15
        OriginalSafeReport r = check_original_safe(l);
        CHECK(finds(r, Code::FoodPileNotShown, Severity::Info,
                    "the food pile at row 17, column 15 does not show in the original from stage 1 on: its tile 285 is named \".\" in the dictionary, and the original "
                    "places no pile of a tile whose picture it does not load", 17, 15));
        CHECK(r.works_in_original_game());
        l = tiny();
        l.tile_dictionary[273] = ".";                                              // stage 2 of piles 12 and 13
        r = check_original_safe(l);
        CHECK(finds(r, Code::FoodPileNotShown, Severity::Info,
                    "2 food piles do not show in the original from a stage whose tile the dictionary names \".\" (the first at row 15, column 6, from stage 2 on: tile 273): "
                    "the original places no pile of a tile whose picture it does not load", 15, 6));
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
        CHECK(finds(r, Code::ObjectOutsideMap, Severity::Info, "one block-1 object and one waypoint stand outside the map (the first at row 40, column 2): the original ignores them", 40, 2));
        CHECK(r.works_in_original_game());
        CHECK(!check_original_safe(tiny()).has(Code::ObjectOutsideMap));
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
            {Code::ObjectOutsideMap, "object_outside_map"},
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

    run_case("S3.1 What The Reader Refuses Is Fatal, With The Reader's Sentence: No Bytes, Version 9, A File Cut Before Block 4, A File Over The Limits", [&] {
        CHECK(located);
        OriginalSafeReport r = check_original_safe(nullptr, 0);
        CHECK(finds(r, Code::FileUnreadable, Severity::Fatal, "no data"));
        std::vector<uint8_t> m = tiny_file;
        wr32(m, 0, 9);
        r = check_original_safe(m.data(), m.size());
        CHECK(r.issues.size() == 1u && r.has(Code::VersionNot8) && r.issues[0].severity == Severity::Fatal);
        CHECK(r.issues.size() == 1u && r.issues[0].message.find("header version 9") == 0);
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

    run_case("S3.2 What A File Left Out Is A Warning: Block 4 Cut Short, The Egg Word Missing, A Block 2 That Ends At A Pile Without Stages; A Whole File Is The Level's Profile", [&] {
        CHECK(located);
        CHECK(check_original_safe(tiny_file.data(), tiny_file.size()).issues.empty());
        // TINY has no waypoint: a file cut before its egg word, and one cut inside block 4 (a count of 1 and half a record)
        OriginalSafeReport r = check_original_safe(tiny_file.data(), tiny_file.size() - 2);
        CHECK(finds(r, Code::EggWordMissing, Severity::Warning, "the file ends before its egg word: the original leaves the egg stock of every team to chance"));
        std::vector<uint8_t> m(tiny_file.begin(), tiny_file.begin() + static_cast<std::ptrdiff_t>(t.b4_count));
        put16(m, 1);
        put16(m, 3);
        r = check_original_safe(m.data(), m.size());
        CHECK(finds(r, Code::FileCutInBlock4, Severity::Warning,
                    "the file ends inside block 4 (the flower droppers): the original keeps the complete records it read and leaves the egg stock of every team to chance"));
        CHECK(!r.has(Code::EggWordMissing));
        m = tiny_file;
        wr16(m, t.food[2] + 8, 0);
        r = check_original_safe(m.data(), m.size());
        CHECK(finds(r, Code::FoodBlockEndedEarly, Severity::Warning,
                    "block 2 (the food) ends at pile 3 of 14, which has no stages: the original keeps the 2 piles before it and reads the rest of the map from the bytes behind it"));
        // the same level from the writer has none of them: the rewrite is whole
        LevelData read;
        CHECK(read.load_from_memory(m.data(), m.size()));
        const LevelWriteResult w = write_level(read);
        CHECK(w.ok && !check_original_safe(w.bytes.data(), w.bytes.size()).has(Code::FoodBlockEndedEarly));
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

// The results of the library (docs/MAP_FORMAT.md, "The original-safe profile"): 516 of the 540 maps work in the original game; per code, how many maps have it
constexpr int kLibraryWorks = 516;
const std::map<std::string, int> kLibraryCodes = {
    {"description_too_long", 8}, {"egg_word_missing", 1}, {"file_cut_in_block_4", 11}, {"food_pile_outside_map", 3}, {"hill_without_entrance", 2},   // the 24 that do not
    {"food_pile_not_shown", 34}, {"hill_not_whole", 36}, {"mode_not_1", 14}, {"object_outside_map", 36}, {"tile_not_drawn", 93},                        // notes
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
        // the 12 files that are cut short work once they are rewritten, but Conquer, whose description has 30 characters
        const std::vector<std::string> cut = {"1v1TREASURE.LVL", "AnTsCaStLe.lvl", "AntsTournament.lvl", "Ants_Town.lvl", "ArCaDeStYLe.lvl", "Arena101.lvl",
                                              "BetterArcadeStyle.lvl", "Bunker.lvl", "Food Island.lvl", "Food Rules The Ants!!!.lvl", "NW.LVL"};
        CHECK_MSG(rewrite_works == cut, std::to_string(rewrite_works.size()) + " work once rewritten");
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
    std::cout << "\n" << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
