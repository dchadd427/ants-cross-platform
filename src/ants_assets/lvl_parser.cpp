#include "ants_assets/lvl_parser.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstring>
#include <utility>

namespace ants::assets {

namespace {

class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    size_t pos() const noexcept { return pos_; }                      // (the loader's findings say where a file ends)
    size_t remaining() const noexcept { return (pos_ < size_) ? (size_ - pos_) : 0; }

    bool read_u16(uint16_t& val) noexcept {
        if (pos_ + 2 > size_) return false;
        val = static_cast<uint16_t>(
            static_cast<uint32_t>(data_[pos_]) |
            (static_cast<uint32_t>(data_[pos_ + 1]) << 8)
        );
        pos_ += 2;
        return true;
    }

    bool read_u32(uint32_t& val) noexcept {
        if (pos_ + 4 > size_) return false;
        val = static_cast<uint32_t>(data_[pos_]) |
              (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
              (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
              (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return true;
    }

    bool read_double(double& val) noexcept {
        if (pos_ + 8 > size_) return false;
        std::memcpy(&val, data_ + pos_, 8);
        pos_ += 8;
        return true;
    }

    bool read_bytes(void* dst, size_t len) noexcept {
        if (pos_ + len > size_) return false;
        std::memcpy(dst, data_ + pos_, len);
        pos_ += len;
        return true;
    }

private:
    const uint8_t* data_{nullptr};
    size_t size_{0};
    size_t pos_{0};
};

static const MapCell EMPTY_CELL{LVL_EMPTY_TILE, 0, 0};
static const std::string EMPTY_STRING;

} // anonymous namespace

const MapCell& LevelData::get_cell_layer1(uint32_t x, uint32_t y) const {
    if (x >= width || y >= height) return EMPTY_CELL;
    size_t idx = static_cast<size_t>(y) * width + x;
    if (idx >= layer1_terrain.size()) return EMPTY_CELL;
    return layer1_terrain[idx];
}

const MapCell& LevelData::get_cell_layer2(uint32_t x, uint32_t y) const {
    if (x >= width || y >= height) return EMPTY_CELL;
    size_t idx = static_cast<size_t>(y) * width + x;
    if (idx >= layer2_interactive.size()) return EMPTY_CELL;
    return layer2_interactive[idx];
}

const std::string& LevelData::get_tile_name(uint16_t tile_index) const {
    if (tile_index >= tile_dictionary.size()) return EMPTY_STRING;
    return tile_dictionary[tile_index];
}

int32_t LevelData::find_tile_index(const std::string& name) const noexcept {
    for (size_t i = 0; i < tile_dictionary.size(); ++i) {
        if (tile_dictionary[i] == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

namespace {
// The team whose hill a layer-2 tile is (GREENHILL 0, REDHILL 1, BLUEHILL 2, BLACKHILL 3), -1 for any other tile
int hill_team_of_tile(const std::string& name) {
    std::string low = name;
    for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (low == "greenhill") return 0;
    if (low == "redhill") return 1;
    if (low == "bluehill") return 2;
    if (low == "blackhill") return 3;
    return -1;
}
}  // namespace

LevelData LevelData::for_roster(uint8_t roster_mask) const {
    LevelData out(*this);
    if ((roster_mask & 0x0Fu) == 0x0Fu) return out;
    for (MapCell& cell : out.layer2_interactive) {
        if (cell.tile_index >= out.tile_dictionary.size()) continue;
        const int team = hill_team_of_tile(out.tile_dictionary[cell.tile_index]);
        if (team >= 0 && (roster_mask & (1u << team)) == 0) cell = MapCell{};
    }
    auto& spawns = out.anthill_spawns;
    spawns.erase(std::remove_if(spawns.begin(), spawns.end(),
                                [&](const AnthillSpawn& sp) { return sp.team_id < 4 && (roster_mask & (1u << sp.team_id)) == 0; }),
                 spawns.end());
    return out;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// Findings: the names and the verdict of a LevelValidation
// ------------------------------------------------------------------------------------------------------------------------------------------------------

const char* to_string(LevelProblemSeverity severity) noexcept {
    switch (severity) {
        case LevelProblemSeverity::Info: return "note";
        case LevelProblemSeverity::Warning: return "warning";
        case LevelProblemSeverity::Fatal: return "fatal";
    }
    return "?";
}

const char* to_string(LevelProblemKind kind) noexcept {
    switch (kind) {
        case LevelProblemKind::Unreadable: return "unreadable";
        case LevelProblemKind::WrongVersion: return "wrong_version";
        case LevelProblemKind::Truncated: return "truncated";
        case LevelProblemKind::GridEmpty: return "grid_empty";
        case LevelProblemKind::GridTooLarge: return "grid_too_large";
        case LevelProblemKind::DictionaryTooLarge: return "dictionary_too_large";
        case LevelProblemKind::StartMarkerOutsideGrid: return "start_marker_outside_grid";
        case LevelProblemKind::ModeIgnored: return "mode_ignored";
        case LevelProblemKind::FoodBlockEndedEarly: return "food_block_ended_early";
        case LevelProblemKind::WaypointBlockTruncated: return "waypoint_block_truncated";
        case LevelProblemKind::EggStockMissing: return "egg_stock_missing";
        case LevelProblemKind::TileOutsideDictionary: return "tile_outside_dictionary";
        case LevelProblemKind::FoodOutsideGrid: return "food_outside_grid";
        case LevelProblemKind::ObjectOutsideGrid: return "object_outside_grid";
    }
    return "?";
}

bool LevelValidation::has(LevelProblemKind kind) const noexcept {
    for (const LevelProblem& p : problems) {
        if (p.kind == kind) return true;
    }
    return false;
}

bool LevelValidation::clean() const noexcept {
    if (!playable) return false;
    for (const LevelProblem& p : problems) {
        if (p.severity != LevelProblemSeverity::Info) return false;
    }
    return true;
}

const LevelProblem* LevelValidation::first_fatal() const noexcept {
    for (const LevelProblem& p : problems) {
        if (p.severity == LevelProblemSeverity::Fatal) return &p;
    }
    return nullptr;
}

std::string LevelValidation::reason() const {
    const LevelProblem* p = first_fatal();
    if (p) return p->message;
    return playable ? std::string() : std::string("the file was not read");
}

std::string LevelValidation::describe() const {
    std::string out;
    for (const LevelProblem& p : problems) {
        out += to_string(p.severity);
        out += ' ';
        out += to_string(p.kind);
        out += ": ";
        out += p.message;
        out += '\n';
    }
    return out;
}

namespace {

using Severity = LevelProblemSeverity;
using Kind = LevelProblemKind;

constexpr const char* kTeamNames[4] = {"green", "red", "blue", "black"};

void add_problem(std::vector<LevelProblem>& list, Severity severity, Kind kind, std::string message, uint32_t count = 1, int team = -1) {
    LevelProblem p;
    p.severity = severity;
    p.kind = kind;
    p.team = static_cast<int8_t>(team);
    p.count = count;
    p.message = std::move(message);
    list.push_back(std::move(p));
}

std::string num(uint64_t v) { return std::to_string(v); }

// The checks that decide whether the data can be handed to the engine at all, whatever the roster: the shape of the grid and of the dictionary.
void collect_layout_problems(const LevelData& level, std::vector<LevelProblem>& out) {
    const uint32_t rows = level.height;
    const uint32_t columns = level.width;
    if (rows == 0 || columns == 0) {
        add_problem(out, Severity::Fatal, Kind::GridEmpty,
                    "the grid has " + num(rows) + " rows x " + num(columns) + " columns: the original loads such a file and goes on with a map that has no cell (nothing to stand on, "
                    "and a start marker would index its empty tile table), the remake has nothing to play on");
    } else if (rows > LVL_MAX_GRID_SIDE || columns > LVL_MAX_GRID_SIDE) {
        add_problem(out, Severity::Fatal, Kind::GridTooLarge,
                    "the grid has " + num(rows) + " rows x " + num(columns) + " columns, more than the " + num(LVL_MAX_GRID_SIDE) + " the remake hosts (the layer-2 object "
                    "anchors of the format are 8 bit); the map editor cannot make such a map");
    }
    if (level.tile_dictionary.size() > LVL_MAX_DICTIONARY_ENTRIES) {
        add_problem(out, Severity::Fatal, Kind::DictionaryTooLarge,
                    "the tile dictionary has " + num(level.tile_dictionary.size()) + " names, the original's tables hold " + num(LVL_MAX_DICTIONARY_ENTRIES) +
                    ": it overruns a 5376 byte buffer while it reads the names, there is no behaviour to copy");
    }
}

// The checks on the content of a loaded level (the grid has a shape): what lies outside the grid or the dictionary, and the start markers of the teams that play.
void collect_content_problems(const LevelData& level, uint8_t roster_mask, std::vector<LevelProblem>& out) {
    const uint32_t rows = level.height;
    const uint32_t columns = level.width;
    const size_t dictionary = level.tile_dictionary.size();
    auto outside_dictionary = [&](uint16_t tile) { return tile != LVL_EMPTY_TILE && tile >= dictionary; };

    // tile indexes beyond the dictionary: the original's remap table has one word per name and is read with the raw index
    uint64_t in_layer1 = 0, in_layer2 = 0, in_records = 0, in_stages = 0, in_ambient = 0;
    for (const MapCell& c : level.layer1_terrain) if (outside_dictionary(c.tile_index)) ++in_layer1;
    for (const MapCell& c : level.layer2_interactive) if (outside_dictionary(c.tile_index)) ++in_layer2;
    for (const AnthillSpawn& sp : level.anthill_spawns) if (sp.tile_id >= dictionary) ++in_records;     // block 1 has no exemption for the empty tile in the original
    for (const FoodSchedule& fs : level.food_schedules) for (const FoodItemVariant& v : fs.variants) if (outside_dictionary(v.tile_id)) ++in_stages;
    if (outside_dictionary(level.ambient_tile_or_sound)) in_ambient = 1;
    const uint64_t outside_total = in_layer1 + in_layer2 + in_records + in_stages + in_ambient;
    if (outside_total != 0) {
        add_problem(out, Severity::Warning, Kind::TileOutsideDictionary,
                    num(outside_total) + " tile indexes are outside the dictionary of " + num(dictionary) + " names (layer 1: " + num(in_layer1) + ", layer 2: " + num(in_layer2) +
                    ", block 1: " + num(in_records) + ", block 2: " + num(in_stages) + ", block 3: " + num(in_ambient) + "): the original reads its remap table out of bounds there "
                    "(a heap word, so an arbitrary tile); the remake keeps the raw index as a tile without a name",
                    static_cast<uint32_t>(outside_total));
    }

    if (rows == 0 || columns == 0) return;      // everything would be "outside" an empty grid; GridEmpty says it
    auto outside_grid = [&](uint16_t row, uint16_t column) { return row >= rows || column >= columns; };

    // food objects anchored outside the grid
    uint32_t food_outside = 0;
    for (const FoodSchedule& fs : level.food_schedules) if (outside_grid(fs.y, fs.x)) ++food_outside;
    if (food_outside != 0) {
        add_problem(out, Severity::Warning, Kind::FoodOutsideGrid,
                    num(food_outside) + " food objects are anchored outside the " + num(rows) + " x " + num(columns) + " grid: the original writes the object's cells without a bounds "
                    "check (words of the heap beside its tile table); the remake leaves the cells outside the grid alone",
                    food_outside);
    }

    // start markers: only the teams that play need them
    for (int team = 0; team < 4; ++team) {
        uint32_t bad = 0;
        size_t first = 0;
        for (size_t i = 0; i < level.anthill_spawns.size(); ++i) {
            const AnthillSpawn& sp = level.anthill_spawns[i];
            if (sp.team_id == team && outside_grid(sp.y, sp.x)) {
                if (bad == 0) first = i;
                ++bad;
            }
        }
        if (bad == 0) continue;
        const AnthillSpawn& sp = level.anthill_spawns[first];
        const bool plays = (roster_mask & (1u << team)) != 0;
        std::string text = std::string("team ") + kTeamNames[team] + ": " + num(bad) + " start marker(s) lie outside the " + num(rows) + " x " + num(columns) +
                           " grid (the first, record #" + num(first) + ", at row " + num(sp.y) + " column " + num(sp.x) + "): the original does not check the position and indexes "
                           "its row table with it (an access violation or a heap overwrite), so there is no behaviour to copy and the remake cannot place the ant";
        if (!plays) text += "; the team does not play in this roster, its markers are not used";
        add_problem(out, plays ? Severity::Fatal : Severity::Info, Kind::StartMarkerOutsideGrid, std::move(text), bad, team);
    }

    // plants and other decoration, and waypoints, outside the grid
    uint32_t plants_outside = 0;
    for (const AnthillSpawn& sp : level.anthill_spawns) if (sp.team_id == 255 && outside_grid(sp.y, sp.x)) ++plants_outside;
    uint32_t waypoints_outside = 0;
    for (const Waypoint& wp : level.waypoints) if (outside_grid(wp.y, wp.x)) ++waypoints_outside;
    if (plants_outside != 0 || waypoints_outside != 0) {
        add_problem(out, Severity::Info, Kind::ObjectOutsideGrid,
                    num(plants_outside) + " block 1 objects and " + num(waypoints_outside) + " waypoints lie outside the grid: the original ignores them (the waypoint reader checks "
                    "the cell, the dropper poll checks the drop tile), and so does the remake",
                    plants_outside + waypoints_outside);
    }
}

}  // anonymous namespace

LevelValidation LevelData::validate(uint8_t roster_mask) const {
    LevelValidation v;
    v.parsed = true;
    v.roster_mask = static_cast<uint8_t>(roster_mask & 0x0Fu);
    v.problems = load_notes;
    collect_layout_problems(*this, v.problems);
    collect_content_problems(*this, v.roster_mask, v.problems);
    v.playable = v.first_fatal() == nullptr;
    return v;
}

bool LevelData::load_from_file(const std::string& filepath, LevelValidation* report) {
    return LVLParser::load_from_file(filepath, *this, report);
}

bool LevelData::load_from_memory(const uint8_t* data, size_t size, LevelValidation* report) {
    return LVLParser::load_from_memory(data, size, *this, report);
}

bool LVLParser::load_from_file(const std::string& filepath, LevelData& out_level, LevelValidation* report) {
    try {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            out_level = LevelData{};
            if (report) {
                *report = LevelValidation{};
                add_problem(report->problems, Severity::Fatal, Kind::Unreadable, "the file cannot be opened");
            }
            return false;
        }

        std::streamsize size = static_cast<std::streamsize>(file.tellg());
        if (size < 0) size = 0;
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (size > 0 && !file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            out_level = LevelData{};
            if (report) {
                *report = LevelValidation{};
                add_problem(report->problems, Severity::Fatal, Kind::Unreadable, "the file cannot be read");
            }
            return false;
        }

        return load_from_memory(buffer.data(), buffer.size(), out_level, report);
    } catch (...) {
        out_level = LevelData{};
        if (report) {
            *report = LevelValidation{};
            add_problem(report->problems, Severity::Fatal, Kind::Unreadable, "the file cannot be read (out of memory)");
        }
        return false;
    }
}

namespace {

// The loader of Ants.exe (FUN_01006349 at 0x1006349, the only caller passes flag 0), read with Capstone; docs/GAME_REVERSE_ENGINEERING.md 4.5 has the addresses.
// Every read of the original goes through FUN_010065b1, which throws CBPException ("System read file error.") when the file holds fewer bytes than asked, except the
// description (30 bytes) and the names of the dictionary, whose reads are not checked (the next checked read then fails). Only the last two steps, block 4 and the
// final word, are inside the loader's single try block (0x1006510 .. 0x100655e): a read error there is caught (handler 0x1006542) and the loader returns 1 with what
// it has. An error anywhere before propagates; the only handler of its type above the start path is WinMain's, which reports it and ends the program.
//
// Returns false when the original's loader would not complete (the Fatal problem is in `problems`); `lvl` holds the data read so far in any case.
struct Parse {
    LevelData lvl;
    std::vector<LevelProblem> fatal;
    bool completed{false};
};

void fail_truncated(Parse& p, const char* where, size_t pos, size_t size) {
    add_problem(p.fatal, Severity::Fatal, Kind::Truncated,
                std::string("the file ends inside the ") + where + " (byte " + num(pos) + " of " + num(size) + "): the original's loader throws \"System read file error.\" there "
                "and the game does not start");
}

void parse_level(const uint8_t* data, size_t size, Parse& p) {
    LevelData& lvl = p.lvl;
    BinaryReader r(data, size);

    // header: version, then the mode dword (+0x74) and the minutes word (+0x6e), then the 30 bytes of the description (+0xa6)
    if (!r.read_u32(lvl.version)) { fail_truncated(p, "header", r.pos(), size); return; }
    if (lvl.version != LVL_EXPECTED_VERSION) {
        add_problem(p.fatal, Severity::Fatal, Kind::WrongVersion,
                    "header version " + num(lvl.version) + ", the original reads version " + num(LVL_EXPECTED_VERSION) + " only (0x1006386): it shows \"The MAP you tried to play "
                    "is the wrong version\" and goes on without a map");
        return;
    }
    if (!r.read_u32(lvl.game_mode) || !r.read_u16(lvl.default_minutes)) { fail_truncated(p, "header", r.pos(), size); return; }
    if (lvl.game_mode != LVL_GAME_MODE_STANDARD) {
        add_problem(lvl.load_notes, Severity::Info, Kind::ModeIgnored,
                    "the mode dword is " + num(lvl.game_mode) + ", not 1: the original stores it (0x100639f) and never reads it again, the remake does the same (the map "
                    "editor leaves garbage there)");
    }
    {
        char desc_buf[30] = {};
        const size_t take = std::min<size_t>(30, r.remaining());                 // unchecked read in the original
        r.read_bytes(desc_buf, take);
        size_t desc_len = 0;
        while (desc_len < 30 && desc_buf[desc_len] != '\0') ++desc_len;
        lvl.description.assign(desc_buf, desc_len);
    }

    // tile dictionary: a word T, then T + 1 names of 11 bytes (the names are read unchecked; the remap built from them is the identity)
    if (!r.read_u16(lvl.tile_type_count)) { fail_truncated(p, "header", r.pos(), size); return; }
    const size_t dict_count = static_cast<size_t>(lvl.tile_type_count) + 1;
    if (r.remaining() < dict_count * 11) { fail_truncated(p, "tile dictionary", size, size); return; }
    lvl.tile_dictionary.resize(dict_count);
    for (size_t i = 0; i < dict_count; ++i) {
        char name_buf[11] = {};
        if (!r.read_bytes(name_buf, 11)) { fail_truncated(p, "tile dictionary", size, size); return; }          // (cannot fail: the size was checked above)
        size_t nlen = 0;
        while (nlen < 11 && name_buf[nlen] != '\0') ++nlen;
        lvl.tile_dictionary[i].assign(name_buf, nlen);
    }

    // grid size: the FIRST dword is the number of ROWS, the second the number of COLUMNS; the original keeps both as 16 bit words (+0xd0, +0xd2)
    uint32_t rows_raw = 0;
    uint32_t columns_raw = 0;
    if (!r.read_u32(rows_raw) || !r.read_u32(columns_raw)) { fail_truncated(p, "grid size", r.pos(), size); return; }
    lvl.height.val = rows_raw & 0xFFFFu;
    lvl.width.val = columns_raw & 0xFFFFu;

    // the two layers, rows as the outer loop, three words per cell (0x10069d8): checked against what is left before anything is allocated
    const uint64_t cells = static_cast<uint64_t>(lvl.width.val) * lvl.height.val;
    if (static_cast<uint64_t>(r.remaining()) < cells * 12u) { fail_truncated(p, "layers", size, size); return; }
    const size_t cell_count = static_cast<size_t>(cells);
    lvl.layer1_terrain.resize(cell_count);
    for (size_t i = 0; i < cell_count; ++i) {
        MapCell& c = lvl.layer1_terrain[i];
        r.read_u16(c.tile_index); r.read_u16(c.flags); r.read_u16(c.properties);
    }
    lvl.layer2_interactive.resize(cell_count);
    for (size_t i = 0; i < cell_count; ++i) {
        MapCell& c = lvl.layer2_interactive[i];
        r.read_u16(c.tile_index); r.read_u16(c.flags); r.read_u16(c.properties);
    }

    // block 1 (0x1006c7d): a word count, then (tile, row, column) words
    uint16_t b1_count = 0;
    if (!r.read_u16(b1_count)) { fail_truncated(p, "block 1 (start markers and plants)", r.pos(), size); return; }
    if (r.remaining() < static_cast<size_t>(b1_count) * 6) { fail_truncated(p, "block 1 (start markers and plants)", size, size); return; }
    lvl.anthill_spawns.resize(b1_count);
    for (size_t i = 0; i < b1_count; ++i) {
        AnthillSpawn& sp = lvl.anthill_spawns[i];
        r.read_u16(sp.tile_id); r.read_u16(sp.y); r.read_u16(sp.x);

        const std::string& tname = lvl.get_tile_name(sp.tile_id);
        if (tname.find("GSTART") != std::string::npos || tname.find("GREENHILL") != std::string::npos ||
            tname.find("ghill") != std::string::npos || tname.find("gstart") != std::string::npos) {
            sp.team_id = 0; // Green
        } else if (tname.find("RSTART") != std::string::npos || tname.find("REDHILL") != std::string::npos ||
                   tname.find("rhill") != std::string::npos || tname.find("rstart") != std::string::npos) {
            sp.team_id = 1; // Red
        } else if (tname.find("USTART") != std::string::npos || tname.find("BLUEHILL") != std::string::npos ||
                   tname.find("blhill") != std::string::npos || tname.find("ustart") != std::string::npos) {
            sp.team_id = 2; // Blue
        } else if (tname.find("BSTART") != std::string::npos || tname.find("BLACKHILL") != std::string::npos ||
                   tname.find("bkhill") != std::string::npos || tname.find("bstart") != std::string::npos) {
            sp.team_id = 3; // Black
        } else {
            sp.team_id = 255;
        }
    }

    // block 2 (0x1006d19): a word count, then per object five words (row, column, units, points, stages) and `stages` pairs of words (threshold, tile). An object
    // WITHOUT stages ends the block on the spot (0x1006dcf: a jump to the end of the function): the objects read before it stay, nothing else of the block is read, and
    // the next read, block 3, takes the bytes right behind that object's stage count.
    uint16_t b2_count = 0;
    if (!r.read_u16(b2_count)) { fail_truncated(p, "block 2 (food objects)", r.pos(), size); return; }
    lvl.food_schedules.reserve(std::min<size_t>(b2_count, r.remaining() / 10));
    for (size_t i = 0; i < b2_count; ++i) {
        FoodSchedule fs;
        uint16_t item_count = 0;
        if (r.remaining() < 10) { fail_truncated(p, "block 2 (food objects)", size, size); return; }
        r.read_u16(fs.y); r.read_u16(fs.x); r.read_u16(fs.initial_delay); r.read_u16(fs.respawn_interval); r.read_u16(item_count);
        if (item_count == 0) {
            add_problem(lvl.load_notes, Severity::Warning, Kind::FoodBlockEndedEarly,
                        "food object #" + num(i) + " of " + num(b2_count) + " has no stages: the original ends block 2 there (the " + num(i) + " objects before it stay), reads block 3, "
                        "block 4 and the egg stock from the bytes behind its stage count (so from bytes that are not those blocks, in most files) and keeps the announced number of slots, "
                        "of which only the objects read are filled; the remake does the same and holds the objects read",
                        static_cast<uint32_t>(b2_count - i));
            break;
        }
        if (r.remaining() < static_cast<size_t>(item_count) * 4) { fail_truncated(p, "block 2 (food objects)", size, size); return; }
        fs.variants.resize(item_count);
        for (size_t v = 0; v < item_count; ++v) {
            FoodItemVariant& var = fs.variants[v];
            r.read_u16(var.weight); r.read_u16(var.tile_id);
        }
        lvl.food_schedules.push_back(std::move(fs));
    }

    // block 3 (0x1007025): two words
    if (!r.read_u16(lvl.ambient_flag) || !r.read_u16(lvl.ambient_tile_or_sound)) { fail_truncated(p, "block 3 (ambient parameters)", r.pos(), size); return; }

    // From here the original is inside its try block: the loader returns 1 whatever the rest of the file holds.
    p.completed = true;

    // block 4 (0x1006f0e): a word count, then per waypoint the row and column words (the cell is marked solid at once: 0x100660c, bounds checked), a flag dword and,
    // when the flag is not 0, the interval dword and five doubles. The record joins the original's table only when it is complete. Then the final word (+0x6c).
    uint16_t b4_count = 0;
    bool cut = false;
    std::string cut_text;
    size_t complete_records = 0;
    if (!r.read_u16(b4_count)) {
        cut = true;
        cut_text = "the file ends before the count of block 4";
    } else {
        lvl.waypoints.reserve(std::min<size_t>(b4_count, r.remaining() / 8));
        for (size_t i = 0; i < b4_count && !cut; ++i) {
            Waypoint wp;
            if (!r.read_u16(wp.y) || !r.read_u16(wp.x)) {
                cut = true;
                cut_text = "block 4 announces " + num(b4_count) + " waypoints and the file ends inside record #" + num(i) + " before its cell is complete";
                break;
            }
            uint32_t flag = 0;
            bool complete = r.read_u32(flag);
            wp.flag = flag;
            if (complete && flag != 0) {
                complete = r.read_u32(wp.param);
                for (size_t k = 0; complete && k < 5; ++k) complete = r.read_double(wp.probabilities[k]);
            }
            if (!complete) {
                // the half record: the original has marked its cell solid and drops the record; a record without a trigger gives the same in the remake (solid cell, no dropper)
                wp.flag = 0;
                wp.param = 0;
                wp.probabilities.fill(0.0);
                lvl.waypoints.push_back(wp);
                cut = true;
                cut_text = "block 4 announces " + num(b4_count) + " waypoints and the file ends inside record #" + num(i) + ", after its cell";
                break;
            }
            lvl.waypoints.push_back(wp);
            ++complete_records;
        }
    }
    if (cut) {
        std::string text = cut_text + ": the original catches the read error inside its loader and goes on with what it has read, " + num(complete_records) + " complete waypoints";
        if (b4_count > complete_records) {
            text += " (its table has " + num(b4_count) + " slots; the others stay uninitialised, and its dropper poll walks every slot when the map has a flower)";
        }
        text += "; the remake keeps the complete waypoints only";
        add_problem(lvl.load_notes, Severity::Warning, Kind::WaypointBlockTruncated, std::move(text), static_cast<uint32_t>(b4_count - std::min<size_t>(b4_count, complete_records)));
        add_problem(lvl.load_notes, Severity::Warning, Kind::EggStockMissing,
                    "the egg stock word was not read: the original leaves that field uninitialised (heap memory), the remake gives every team 0 eggs");
    } else if (!r.read_u16(lvl.boundary_param)) {
        add_problem(lvl.load_notes, Severity::Warning, Kind::EggStockMissing,
                    "the file ends before the egg stock word: the original leaves that field uninitialised (heap memory), the remake gives every team 0 eggs");
    }
    // 10. Nothing after the final word is read, and the file's length is not checked (the community map editor ends its maps with a template of filler).
}

}  // anonymous namespace

bool LVLParser::load_from_memory(const uint8_t* data, size_t size, LevelData& out_level, LevelValidation* report) {
    out_level = LevelData{};
    if (report) *report = LevelValidation{};
    try {
        if (!data || size == 0) {
            if (report) add_problem(report->problems, Severity::Fatal, Kind::Unreadable, "no data");
            return false;
        }
        Parse p;
        parse_level(data, size, p);
        if (!p.fatal.empty()) {
            if (report) report->problems = p.fatal;
            return false;
        }
        std::vector<LevelProblem> layout;
        collect_layout_problems(p.lvl, layout);
        bool layout_fatal = false;
        for (const LevelProblem& lp : layout) layout_fatal = layout_fatal || lp.severity == Severity::Fatal;
        if (layout_fatal) {
            if (report) {
                report->parsed = true;
                report->problems = p.lvl.load_notes;
                report->problems.insert(report->problems.end(), layout.begin(), layout.end());
            }
            return false;
        }
        out_level = std::move(p.lvl);
        if (report) *report = out_level.validate(0x0F);
        return true;
    } catch (...) {
        out_level = LevelData{};
        if (report) {
            *report = LevelValidation{};
            add_problem(report->problems, Severity::Fatal, Kind::Unreadable, "the file cannot be held (out of memory)");
        }
        return false;
    }
}

LevelValidation LVLParser::check_memory(const uint8_t* data, size_t size, uint8_t roster_mask) {
    LevelData level;
    LevelValidation report;
    if (!load_from_memory(data, size, level, &report)) {
        report.roster_mask = static_cast<uint8_t>(roster_mask & 0x0Fu);
        report.playable = false;
        return report;
    }
    return level.validate(roster_mask);
}

LevelValidation LVLParser::check_file(const std::string& filepath, uint8_t roster_mask) {
    LevelData level;
    LevelValidation report;
    if (!load_from_file(filepath, level, &report)) {
        report.roster_mask = static_cast<uint8_t>(roster_mask & 0x0Fu);
        report.playable = false;
        return report;
    }
    return level.validate(roster_mask);
}

} // namespace ants::assets
