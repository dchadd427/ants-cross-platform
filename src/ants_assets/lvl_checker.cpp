#include "ants_assets/lvl_checker.hpp"

#include <algorithm>
#include <string>
#include <utility>

// The rules of the original-safe profile and where each comes from (the addresses of Ants.exe): docs/MAP_FORMAT.md, "The original-safe profile", and
// docs/GAME_REVERSE_ENGINEERING.md 4.4, 4.5 and 5.62, docs/reverse_engineering/map_loader_fidelity.md.

namespace ants::assets {

namespace {

using Severity = LevelProblemSeverity;
using Code = OriginalSafeCode;

constexpr const char* kTeams[4] = {"Green", "Red", "Blue", "Black"};      // the remake's teams: 0 green, 1 red, 2 blue, 3 black
constexpr uint16_t kHillOf[4] = {248, 247, 246, 245};                       // hill_team_of_id, the other way round
constexpr uint16_t kMarkerOf[4] = {154, 155, 153, 152};                     // start_marker_team_of_id, the other way round
constexpr uint32_t kMostSide = 256;                                         // LVL_MAX_GRID_SIDE: an object's anchor holds its row and its column in a byte each
constexpr size_t kLongestDescription = 29;                                  // the file's 30 bytes, one of them the zero that ends the text

std::string num(uint64_t v) { return std::to_string(v); }
std::string place(int64_t row, int64_t column) { return "row " + std::to_string(row) + ", column " + std::to_string(column); }

// Where a rule is broken: how many times, and the first time (its cell, its tile)
struct Tally {
    uint32_t count{0};
    int32_t row{-1};
    int32_t column{-1};
    uint32_t tile{0};
    uint32_t extra{0};
    void see(int64_t r, int64_t c, uint32_t t = 0, uint32_t x = 0) {
        if (count++ != 0) return;
        row = static_cast<int32_t>(r);
        column = static_cast<int32_t>(c);
        tile = t;
        extra = x;
    }
    // "(row 3, column 7" or "(the first at row 3, column 7", without the closing parenthesis
    std::string where() const { return count == 1 ? "(" + place(row, column) : "(the first at " + place(row, column); }
};

class Findings {
public:
    explicit Findings(OriginalSafeReport& report) : report_(report) {}
    void add(Code code, Severity severity, std::string message, int32_t row = -1, int32_t column = -1, int team = -1, uint32_t count = 1, bool place_limit = false) {
        OriginalSafeIssue i;
        i.code = code;
        i.severity = severity;
        i.message = std::move(message);
        i.row = row;
        i.column = column;
        i.team = static_cast<int8_t>(team);
        i.count = count;
        i.place_limit = place_limit;
        report_.issues.push_back(std::move(i));
    }

private:
    OriginalSafeReport& report_;
};

// The findings that stop "works in the original game" first, in the order of the rules, then the notes
void notes_last(OriginalSafeReport& report) {
    std::stable_partition(report.issues.begin(), report.issues.end(), [](const OriginalSafeIssue& i) { return i.severity != Severity::Info; });
}

bool has_note(const LevelData& level, LevelProblemKind kind, uint32_t* count = nullptr) {
    for (const LevelProblem& p : level.load_notes) {
        if (p.kind != kind) continue;
        if (count) *count = p.count;
        return true;
    }
    return false;
}

// What the file left out, as the reader noted it (a level of the editor has no notes)
void file_findings(const LevelData& level, Findings& f) {
    uint32_t unread = 0;
    if (has_note(level, LevelProblemKind::FoodBlockEndedEarly, &unread)) {
        // FUN_01006d19 (0x1006dc9 .. 0x1006dcf): an object without stages ends block 2; block 3 is read from the bytes right behind its stage count
        const size_t kept = level.food_schedules.size();
        f.add(Code::FoodBlockEndedEarly, Severity::Warning,
              "the list of food piles ends at pile " + num(kept + 1) + " of " + num(kept + unread) + ", which has no stages: the original keeps the " + num(kept) +
                  (kept == 1 ? " pile" : " piles") + " before it and reads the rest of the map from the bytes behind it");
    }
    if (has_note(level, LevelProblemKind::WaypointBlockTruncated)) {
        // the loader's one try block holds block 4 and the egg word (0x1006510 .. 0x100655e): a read error there is swallowed, the egg stock (+0x6c) is never set.
        // Fatal: the table of block 4 is sized to the announced count, and the slots that the cut left unfilled are walked by the dropper poll (FUN_0100fc0d ->
        // FUN_01008d79, no check) and freed by the Map's destructor (RE 4.5)
        f.add(Code::FileCutInBlock4, Severity::Fatal,
              "the file ends inside its list of flower droppers: the original keeps the complete records it read, goes on to read the places that were never filled, "
              "and leaves the number of eggs each team starts with to chance");
    } else if (has_note(level, LevelProblemKind::EggStockMissing)) {
        f.add(Code::EggWordMissing, Severity::Warning, "the file ends before it says how many eggs each team starts with: the original leaves that number to chance");
    }
}

void tiles_beyond_dictionary(const Tally& t, const char* one, const char* many, size_t names, Findings& f, bool in_a_pile = false) {
    if (t.count == 0) return;
    std::string where;
    if (in_a_pile) {
        where = std::string(t.count == 1 ? "(the pile at " : "(the first in the pile at ") + place(t.row, t.column);
    } else {
        where = t.where();
    }
    f.add(Code::TileOutsideDictionary, Severity::Fatal,
          (t.count == 1 ? std::string("one ") + one + " uses a tile" : num(t.count) + " " + many + " use tiles") + " beyond the dictionary of " + num(names) + " names " + where +
              ": tile " + num(t.tile) + "): the original reads past its tables there",
          t.row, t.column, -1, t.count);
}

// Each team's hill (layer-2 ids 245 .. 248, FUN_0100ecac): the 4 x 4 cells around its entrance, the one cell whose flag bit 0 says that it is the hill's origin
// (FUN_010076b6), every cell anchored at the entrance; the team's home tiles are made from that origin (FUN_0100ecdf)
void hill_findings(const LevelData& level, Findings& f) {
    const uint32_t rows = level.height;
    const uint32_t columns = level.width;
    const std::vector<MapCell>& layer = level.layer2_interactive;
    for (int team = 0; team < 4; ++team) {
        const uint16_t id = kHillOf[team];
        std::vector<size_t> cells;
        std::vector<size_t> entrances;
        for (size_t i = 0; i < layer.size(); ++i) {
            if (layer[i].tile_index != id) continue;
            cells.push_back(i);
            if ((layer[i].flags & 1u) != 0) entrances.push_back(i);
        }
        if (cells.empty()) {
            f.add(Code::HillMissing, Severity::Warning, std::string(kTeams[team]) + " has no hill", -1, -1, team);
            continue;
        }
        if (entrances.empty()) {
            f.add(Code::HillWithoutEntrance, Severity::Warning,
                  "the hill of " + std::string(kTeams[team]) + " has no entrance: none of its " + num(cells.size()) + " cells is marked as the entrance",
                  static_cast<int32_t>(cells[0] / columns), static_cast<int32_t>(cells[0] % columns), team, static_cast<uint32_t>(cells.size()));
            continue;
        }
        if (entrances.size() > 1) {
            std::string list;
            for (size_t k = 0; k < entrances.size(); ++k) {
                list += std::string(k == 0 ? "" : k + 1 == entrances.size() ? " and at " : ", at ") +
                        place(static_cast<int64_t>(entrances[k] / columns), static_cast<int64_t>(entrances[k] % columns));
            }
            f.add(Code::HillsSeveral, Severity::Warning, std::string(kTeams[team]) + " has " + num(entrances.size()) + " hills (entrances at " + list + ")",
                  static_cast<int32_t>(entrances[0] / columns), static_cast<int32_t>(entrances[0] % columns), team, static_cast<uint32_t>(entrances.size()));
            continue;
        }
        // one entrance: is the hill whole? (a note: the original takes the anchor of each cell for its hill's origin, whatever the anchor says)
        const int64_t er = static_cast<int64_t>(entrances[0] / columns);
        const int64_t ec = static_cast<int64_t>(entrances[0] % columns);
        const uint16_t anchor = static_cast<uint16_t>((er & 0xFF) | ((ec & 0xFF) << 8));
        uint32_t differ = 0;
        int64_t first = -1;                                                  // the first differing cell inside the grid, in row order
        auto in_footprint = [&](int64_t r, int64_t c) { return r >= er - 1 && r <= er + 2 && c >= ec - 1 && c <= ec + 2; };
        auto note = [&](int64_t r, int64_t c) {
            ++differ;
            if (r < 0 || c < 0 || r >= rows || c >= columns) return;
            const int64_t index = r * columns + c;
            if (first < 0 || index < first) first = index;
        };
        for (int64_t r = er - 1; r <= er + 2; ++r) {
            for (int64_t c = ec - 1; c <= ec + 2; ++c) {
                if (r < 0 || c < 0 || r >= rows || c >= columns) {
                    note(r, c);
                    continue;
                }
                const MapCell& cell = layer[static_cast<size_t>(r * columns + c)];
                const bool entrance = r == er && c == ec;                   // (the entrance is its own origin: its anchor is not read)
                if (cell.tile_index != id || ((cell.flags & 1u) != 0) != entrance || (!entrance && cell.properties != anchor)) note(r, c);
            }
        }
        for (const size_t i : cells) {
            if (!in_footprint(static_cast<int64_t>(i / columns), static_cast<int64_t>(i % columns))) note(static_cast<int64_t>(i / columns), static_cast<int64_t>(i % columns));
        }
        if (differ == 0) continue;
        // the cell it is about: the first that differs on the map; when every one is off the map, the entrance (which is then not said to differ)
        const int64_t fr = first >= 0 ? first / columns : er;
        const int64_t fc = first >= 0 ? first % columns : ec;
        std::string what;
        if (first < 0) {
            what = num(differ) + " cells of the 4 x 4 hill around its entrance (" + place(er, ec) + ") lie outside the map";
        } else {
            what = (differ == 1 ? "one cell (" + place(fr, fc) + ") differs" : num(differ) + " cells (the first at " + place(fr, fc) + ") differ") +
                   " from the 4 x 4 hill around its entrance (" + place(er, ec) + ")";
        }
        f.add(Code::HillNotWhole, Severity::Info, "the hill of " + std::string(kTeams[team]) + " is not whole: " + what, static_cast<int32_t>(fr), static_cast<int32_t>(fc), team, differ);
    }
}

}  // namespace

const char* to_string(OriginalSafeCode code) noexcept {
    switch (code) {
        case Code::FileUnreadable: return "file_unreadable";
        case Code::FileTooLarge: return "file_too_large";
        case Code::FileCutShort: return "file_cut_short";
        case Code::FileCutInBlock4: return "file_cut_in_block_4";
        case Code::EggWordMissing: return "egg_word_missing";
        case Code::FoodBlockEndedEarly: return "food_block_ended_early";
        case Code::VersionNot8: return "version_not_8";
        case Code::ModeNot1: return "mode_not_1";
        case Code::DescriptionTooLong: return "description_too_long";
        case Code::GridEmpty: return "grid_empty";
        case Code::GridTooLarge: return "grid_too_large";
        case Code::LayersNotGrid: return "layers_not_grid";
        case Code::DictionaryTooLarge: return "dictionary_too_large";
        case Code::TileOutsideDictionary: return "tile_outside_dictionary";
        case Code::GroundMissing: return "ground_missing";
        case Code::StartMarkerOutsideMap: return "start_marker_outside_map";
        case Code::FoodPileOutsideMap: return "food_pile_outside_map";
        case Code::FoodPileWithoutStages: return "food_pile_without_stages";
        case Code::HillMissing: return "hill_missing";
        case Code::HillWithoutEntrance: return "hill_without_entrance";
        case Code::HillsSeveral: return "hills_several";
        case Code::StartingAntsMissing: return "starting_ants_missing";
        case Code::HillNotWhole: return "hill_not_whole";
        case Code::TileNotDrawn: return "tile_not_drawn";
        case Code::FoodPileNotShown: return "food_pile_not_shown";
        case Code::ObjectOutsideMap: return "object_outside_map";
        case Code::NotForOriginal: return "not_for_original";
    }
    return "?";
}

bool OriginalSafeReport::not_for_original() const noexcept { return has(Code::NotForOriginal); }

bool OriginalSafeReport::works_in_original_game() const noexcept {
    if (!judged || not_for_original()) return false;
    for (const OriginalSafeIssue& i : issues) {
        if (i.severity != LevelProblemSeverity::Info && !i.place_limit) return false;
    }
    return true;
}

bool OriginalSafeReport::fits_the_place() const noexcept {
    for (const OriginalSafeIssue& i : issues) {
        if (i.place_limit) return false;
    }
    return true;
}

bool OriginalSafeReport::has(OriginalSafeCode code) const noexcept { return first(code) != nullptr; }

const OriginalSafeIssue* OriginalSafeReport::first(OriginalSafeCode code) const noexcept {
    for (const OriginalSafeIssue& i : issues) {
        if (i.code == code) return &i;
    }
    return nullptr;
}

std::string OriginalSafeReport::describe() const {
    std::string text;
    for (const OriginalSafeIssue& i : issues) text += std::string(i.place_limit ? "limit" : to_string(i.severity)) + " " + to_string(i.code) + ": " + i.message + "\n";
    return text;
}

namespace {

// The neutral outcome for an Extended map: the original cannot load the kind, which is no fault of the map (docs/MAP_FORMAT.md, "Version 9")
OriginalSafeReport not_for_original_report() {
    OriginalSafeReport report;
    Findings f(report);
    f.add(Code::NotForOriginal, Severity::Info, "not for the original game (needs a version 8 file): this is an Extended map, version 9, which only this remake loads");
    return report;
}

}  // namespace

OriginalSafeReport check_original_safe(const LevelData& level) {
    if (level.version == LVL_EXTENDED_VERSION) return not_for_original_report();
    OriginalSafeReport report;
    Findings f(report);
    file_findings(level, f);

    // the header (0x1006386: the version is the one check of the loader; the mode is stored at +0x74 and never read; the description is 30 bytes read unchecked)
    if (level.version != LVL_EXPECTED_VERSION) {
        f.add(Code::VersionNot8, Severity::Fatal, "the map is version " + num(level.version) + ": the original plays version 8 only (it says \"wrong version\" and starts without a map)");
    }
    if (level.game_mode != LVL_GAME_MODE_STANDARD) {
        f.add(Code::ModeNot1, Severity::Info, "the mode word is " + num(level.game_mode) + ", not 1: the original never reads it (the map maker writes 1)");
    }
    if (level.description.size() > kLongestDescription) {
        f.add(Code::DescriptionTooLong, Severity::Warning,
              "the description is " + num(level.description.size()) + " bytes long: at most 29 fit, because the original reads 30 bytes and a text needs a zero byte at its end " +
                  "(a letter outside a to z takes 2 or 3 bytes)");
    }

    // the grid (0x10068de: two 16-bit sizes; the remake hosts 256 a side) and the dictionary (0x1006795: one dword per name in a buffer of 0x1500 bytes)
    const uint32_t rows = level.height;
    const uint32_t columns = level.width;
    const uint64_t cells = static_cast<uint64_t>(rows) * columns;
    bool grid = true;
    if (rows == 0 || columns == 0) {
        f.add(Code::GridEmpty, Severity::Fatal, "the map has " + num(rows) + " rows x " + num(columns) + " columns: it needs at least one cell");
        grid = false;
    } else if (rows > kMostSide || columns > kMostSide) {
        f.add(Code::GridTooLarge, Severity::Fatal,
              "the map has " + num(rows) + " rows x " + num(columns) + " columns: at most 256 a side fit (an object's anchor holds its row and its column in a byte each)");
        grid = false;
    } else if (level.layer1_terrain.size() != cells || level.layer2_interactive.size() != cells) {
        f.add(Code::LayersNotGrid, Severity::Fatal,
              "the layers hold " + num(level.layer1_terrain.size()) + " and " + num(level.layer2_interactive.size()) + " cells, the grid has " + num(rows) + " x " + num(columns) +
                  " = " + num(cells));
        grid = false;
    }
    const size_t names = level.tile_dictionary.size();
    if (names > LVL_MAX_DICTIONARY_ENTRIES) {
        f.add(Code::DictionaryTooLarge, Severity::Fatal, "the dictionary has " + num(names) + " names: the original's table holds 1,344 and more overrun it");
    }

    // every tile inside the dictionary (0x10067a5: the remap table has one word per name and is read with the raw index; 0x100665a then indexes a 0x540-entry
    // array on the stack with what it read). The empty tile 0x7FFE of a layer cell, a food stage and block 3 is no tile; block 1 has no such exemption.
    auto beyond = [names](uint16_t tile) { return tile != LVL_EMPTY_TILE && tile >= names; };
    Tally ground, objects, records, stages, no_ground;
    if (grid) {
        for (size_t i = 0; i < level.layer1_terrain.size(); ++i) {
            const uint16_t tile = level.layer1_terrain[i].tile_index;
            if (beyond(tile)) ground.see(static_cast<int64_t>(i / columns), static_cast<int64_t>(i % columns), tile);
            if (tile == LVL_EMPTY_TILE) no_ground.see(static_cast<int64_t>(i / columns), static_cast<int64_t>(i % columns), tile);
        }
        for (size_t i = 0; i < level.layer2_interactive.size(); ++i) {
            const uint16_t tile = level.layer2_interactive[i].tile_index;
            if (beyond(tile)) objects.see(static_cast<int64_t>(i / columns), static_cast<int64_t>(i % columns), tile);
        }
    }
    for (const AnthillSpawn& sp : level.anthill_spawns) {
        if (sp.tile_id >= names) records.see(sp.y, sp.x, sp.tile_id);
    }
    for (const FoodSchedule& pile : level.food_schedules) {
        for (const FoodItemVariant& stage : pile.variants) {
            if (beyond(stage.tile_id)) stages.see(pile.y, pile.x, stage.tile_id);
        }
    }
    tiles_beyond_dictionary(ground, "ground cell", "ground cells", names, f);
    tiles_beyond_dictionary(objects, "object cell", "object cells", names, f);
    tiles_beyond_dictionary(records, "entry of the list of start markers and plants", "entries of the list of start markers and plants", names, f);
    tiles_beyond_dictionary(stages, "food stage", "food stages", names, f, true);
    if (beyond(level.ambient_tile_or_sound)) {
        f.add(Code::TileOutsideDictionary, Severity::Fatal,
              "the default ant type is tile " + num(level.ambient_tile_or_sound) + ", beyond the dictionary of " + num(names) + " names: the original reads past its tables there");
    }
    // the ground (FUN_01008af7: the terrain class is a table of the 1,344 ids, read with the layer-1 tile; 0x7FFE is past its end)
    if (no_ground.count != 0) {
        f.add(Code::GroundMissing, Severity::Fatal,
              (no_ground.count == 1 ? std::string("one cell has no ground tile") : num(no_ground.count) + " cells have no ground tile") + " (the empty tile 32766; " +
                  (no_ground.count == 1 ? "" : "the first at ") + place(no_ground.row, no_ground.column) + "): the original reads its terrain table past its end there",
              no_ground.row, no_ground.column, -1, no_ground.count);
    }

    // the start markers and the food piles inside the grid (FUN_0100ef18 / FUN_0100f17f and FUN_01007352 index the row table with the record's row and column, no check)
    if (grid) {
        for (int team = 0; team < 4; ++team) {
            Tally out;
            for (const AnthillSpawn& sp : level.anthill_spawns) {
                if (sp.team_id == team && (sp.y >= rows || sp.x >= columns)) out.see(sp.y, sp.x);
            }
            if (out.count == 0) continue;
            f.add(Code::StartMarkerOutsideMap, Severity::Fatal,
                  (out.count == 1 ? "a start marker of " + std::string(kTeams[team]) + " stands" : num(out.count) + " start markers of " + std::string(kTeams[team]) + " stand") +
                      " outside the map " + out.where() + ")",
                  out.row, out.column, team, out.count);
        }
        Tally out;
        for (const FoodSchedule& pile : level.food_schedules) {
            if (pile.y >= rows || pile.x >= columns) out.see(pile.y, pile.x);
        }
        if (out.count != 0) {
            f.add(Code::FoodPileOutsideMap, Severity::Fatal,
                  (out.count == 1 ? std::string("one food pile stands") : num(out.count) + " food piles stand") + " outside the map " + out.where() + ")", out.row, out.column, -1,
                  out.count);
        }
    }
    // every pile has a stage (0x1006dc9 .. 0x1006dcf: a pile without stages ends block 2 on the spot)
    {
        Tally empty;
        for (const FoodSchedule& pile : level.food_schedules) {
            if (pile.variants.empty()) empty.see(pile.y, pile.x);
        }
        if (empty.count == 1) {
            f.add(Code::FoodPileWithoutStages, Severity::Fatal,
                  "the food pile at " + place(empty.row, empty.column) + " has no stages: the original stops reading the food there and reads the rest of the map from the wrong place",
                  empty.row, empty.column);
        } else if (empty.count > 1) {
            f.add(Code::FoodPileWithoutStages, Severity::Fatal,
                  num(empty.count) + " food piles have no stages (the first at " + place(empty.row, empty.column) +
                      "): the original stops reading the food at the first and reads the rest of the map from the wrong place",
                  empty.row, empty.column, -1, empty.count);
        }
    }

    // the four teams: a hill each, and starting ants (FUN_0100ec18: the records of block 1 whose tile is the team's start marker, table 0x1002468)
    if (grid) hill_findings(level, f);
    for (int team = 0; team < 4; ++team) {
        bool marker = false;
        for (const AnthillSpawn& sp : level.anthill_spawns) marker = marker || sp.team_id == team;
        if (!marker) {
            f.add(Code::StartingAntsMissing, Severity::Warning,
                  std::string(kTeams[team]) + " has no starting ants (no start marker, tile " + num(kMarkerOf[team]) + ")", -1, -1, team);
        }
    }

    // what the original does not draw (a note; FUN_0100674e mode 0 keeps the picture of an id whose entry is not "."; FUN_010068be forces 62 ids), the piles that
    // it does not place (a warning: SetTile, FUN_01007352, places nothing of a tile without a picture, and nobody can see or harvest such a pile), and the objects
    // outside the grid that it ignores (a note)
    auto not_loaded = [&](uint16_t tile) { return tile != LVL_EMPTY_TILE && tile < names && level.tile_dictionary[tile] == "." && !tile_always_loaded(tile); };
    if (grid) {
        Tally unseen;
        for (size_t i = 0; i < level.layer1_terrain.size(); ++i) {
            if (not_loaded(level.layer1_terrain[i].tile_index)) unseen.see(static_cast<int64_t>(i / columns), static_cast<int64_t>(i % columns), level.layer1_terrain[i].tile_index);
        }
        Tally unseen2;
        for (size_t i = 0; i < level.layer2_interactive.size(); ++i) {
            if (not_loaded(level.layer2_interactive[i].tile_index)) {
                unseen2.see(static_cast<int64_t>(i / columns), static_cast<int64_t>(i % columns), level.layer2_interactive[i].tile_index);
            }
        }
        // the first of both layers, in row order (layer 1 before layer 2 on one cell)
        Tally all = unseen;
        if (unseen2.count != 0) {
            const bool second_first = unseen.count == 0 || static_cast<int64_t>(unseen2.row) * columns + unseen2.column < static_cast<int64_t>(unseen.row) * columns + unseen.column;
            if (second_first) {
                all = unseen2;
                all.count = unseen.count + unseen2.count;
            } else {
                all.count += unseen2.count;
            }
        }
        if (all.count != 0) {
            f.add(Code::TileNotDrawn, Severity::Info,
                  (all.count == 1 ? std::string("one cell uses a tile that the dictionary names \".\" (") + place(all.row, all.column) + ": tile " + num(all.tile) +
                                        "): the original does not load its picture, so it does not draw it"
                                  : num(all.count) + " cells use tiles that the dictionary names \".\" (the first at " + place(all.row, all.column) + ": tile " + num(all.tile) +
                                        "): the original does not load their pictures, so it does not draw them"),
                  all.row, all.column, -1, all.count);
        }
    }
    {
        Tally hidden;
        for (const FoodSchedule& pile : level.food_schedules) {
            for (size_t k = 0; k < pile.variants.size(); ++k) {
                const uint16_t tile = pile.variants[k].tile_id;
                if (tile == LVL_EMPTY_TILE) break;                              // the first stage without a tile ends the pile's stages
                if (not_loaded(tile)) {
                    hidden.see(pile.y, pile.x, tile, static_cast<uint32_t>(k + 1));
                    break;
                }
            }
        }
        if (hidden.count == 1) {
            f.add(Code::FoodPileNotShown, Severity::Warning,
                  "the food pile at " + place(hidden.row, hidden.column) + " does not show in the original from stage " + num(hidden.extra) + " on: its tile " + num(hidden.tile) +
                      " is named \".\" in the dictionary, the original places no pile of a tile whose picture it does not load, and nobody can see or harvest a pile that is not placed",
                  hidden.row, hidden.column);
        } else if (hidden.count > 1) {
            f.add(Code::FoodPileNotShown, Severity::Warning,
                  num(hidden.count) + " food piles do not show in the original from a stage whose tile the dictionary names \".\" (the first at " + place(hidden.row, hidden.column) +
                      ", from stage " + num(hidden.extra) + " on: tile " + num(hidden.tile) +
                      "): the original places no pile of a tile whose picture it does not load, and nobody can see or harvest a pile that is not placed",
                  hidden.row, hidden.column, -1, hidden.count);
        }
    }
    if (grid) {
        Tally objects_out;
        uint32_t plants = 0;
        uint32_t points = 0;
        for (const AnthillSpawn& sp : level.anthill_spawns) {
            if (sp.team_id == 255 && (sp.y >= rows || sp.x >= columns)) {
                objects_out.see(sp.y, sp.x);
                ++plants;
            }
        }
        for (const Waypoint& w : level.waypoints) {
            if (w.y >= rows || w.x >= columns) {
                objects_out.see(w.y, w.x);
                ++points;
            }
        }
        if (objects_out.count != 0) {
            std::string what;
            if (plants != 0) what = plants == 1 ? "one plant" : num(plants) + " plants";
            if (points != 0) what += std::string(what.empty() ? "" : " and ") + (points == 1 ? "one waypoint" : num(points) + " waypoints");
            f.add(Code::ObjectOutsideMap, Severity::Info,
                  what + (objects_out.count == 1 ? " stands" : " stand") + " outside the map " + objects_out.where() + "): the original ignores " +
                      (objects_out.count == 1 ? "it" : "them"),
                  objects_out.row, objects_out.column, -1, objects_out.count);
        }
    }
    notes_last(report);
    return report;
}

OriginalSafeReport check_original_safe(const uint8_t* data, size_t size, const LevelLimits& limits) {
    if (data != nullptr && size >= 4 && (static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16) |
                                         (static_cast<uint32_t>(data[3]) << 24)) == LVL_EXTENDED_VERSION) {
        return not_for_original_report();
    }
    LevelData level;
    LevelValidation read;
    if (level.load_from_memory(data, size, &read, limits)) return check_original_safe(level);
    // A grid or a dictionary beyond the limits of the place, but not beyond the original's: the map is judged by the original's rules (its profile is the report),
    // and the limit is a finding of its own, which is no rule of the original (place_limit)
    const bool place_grid = read.has(LevelProblemKind::GridTooLarge);
    const bool place_dictionary = read.has(LevelProblemKind::DictionaryTooLarge);
    LevelData whole;
    LevelValidation game;
    bool loads_in_the_game = false;
    if (place_grid || place_dictionary) loads_in_the_game = whole.load_from_memory(data, size, &game, LevelLimits{});
    OriginalSafeReport report = loads_in_the_game ? check_original_safe(whole) : OriginalSafeReport{};
    Findings f(report);
    if (!loads_in_the_game) {
        // what the reader refuses, with its own sentence (and what it noted on the way); a refusal of the original's own limits is the one of the original's reader
        const LevelValidation& refused = (place_grid || place_dictionary) ? game : read;
        const bool cut_in_block_4 = refused.has(LevelProblemKind::WaypointBlockTruncated);
        for (const LevelProblem& p : refused.problems) {
            switch (p.kind) {
                case LevelProblemKind::Unreadable: f.add(Code::FileUnreadable, Severity::Fatal, p.message); break;
                case LevelProblemKind::WrongVersion: f.add(Code::VersionNot8, Severity::Fatal, p.message); break;
                case LevelProblemKind::VersionNotSupported: f.add(Code::NotForOriginal, Severity::Info, p.message); break;   // (not reached: a version 9 file is answered above)
                case LevelProblemKind::Truncated: f.add(Code::FileCutShort, Severity::Fatal, p.message); break;
                case LevelProblemKind::GridEmpty: f.add(Code::GridEmpty, Severity::Fatal, p.message); break;
                case LevelProblemKind::GridTooLarge: f.add(Code::GridTooLarge, Severity::Fatal, p.message); break;
                case LevelProblemKind::DictionaryTooLarge: f.add(Code::DictionaryTooLarge, Severity::Fatal, p.message); break;
                case LevelProblemKind::ModeIgnored: f.add(Code::ModeNot1, Severity::Info, p.message); break;
                case LevelProblemKind::FoodBlockEndedEarly: f.add(Code::FoodBlockEndedEarly, Severity::Warning, p.message); break;
                case LevelProblemKind::WaypointBlockTruncated: f.add(Code::FileCutInBlock4, Severity::Fatal, p.message); break;
                case LevelProblemKind::EggStockMissing:
                    if (!cut_in_block_4) f.add(Code::EggWordMissing, Severity::Warning, p.message);
                    break;
                default: break;   // (the checks of a whole level: a refused file has no level to check)
            }
        }
    }
    for (const LevelProblem& p : read.problems) {
        switch (p.kind) {
            case LevelProblemKind::FileTooLarge:
                // not read at all: nothing is known of the map
                f.add(Code::FileTooLarge, Severity::Fatal, p.message, -1, -1, -1, 1, true);
                report.judged = false;
                break;
            case LevelProblemKind::GridTooLarge:
                if (!game.has(LevelProblemKind::GridTooLarge)) f.add(Code::GridTooLarge, Severity::Fatal, p.message, -1, -1, -1, 1, true);
                break;
            case LevelProblemKind::DictionaryTooLarge:
                if (!game.has(LevelProblemKind::DictionaryTooLarge)) f.add(Code::DictionaryTooLarge, Severity::Fatal, p.message, -1, -1, -1, 1, true);
                break;
            default: break;
        }
    }
    notes_last(report);
    return report;
}

}  // namespace ants::assets
